#!/usr/bin/env python3
"""Offline Fuxi dual-boot menu build. Does not discover or communicate with devices.

Reuse a verified local chain/payload, prove the original objects reproduce that
chain, build the menu, run sanitizer host tests and produce native-rendered PNGs.
"""
import argparse
import hashlib
import json
import os
import re
import shlex
import struct
import subprocess
import sys
from pathlib import Path

SOURCE = Path(__file__).resolve().parent
WORKSPACE = SOURCE.parent


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def need(condition, message):
    if not condition:
        raise ValueError(message)


def manifest(path):
    result = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        checksum, name = line.split(maxsplit=1)
        name = name.strip().lstrip("*")
        need(re.fullmatch(r"[0-9a-f]{64}", checksum), f"Invalid manifest: {path}")
        need(name not in result, f"Duplicate checksum entry: {name}")
        result[name] = checksum
    return result


def wsl_path(path):
    path = path.resolve()
    return "/mnt/" + path.drive[0].lower() + path.as_posix()[2:]


def run(argv, log):
    process = subprocess.run([str(x) for x in argv], capture_output=True, text=True, encoding="utf-8", errors="replace")
    with log.open("a", encoding="utf-8", newline="\n") as stream:
        stream.write(subprocess.list2cmdline([str(x) for x in argv]) + "\n")
        stream.write(process.stdout + process.stderr)
    if process.returncode:
        raise RuntimeError(f"Command failed ({process.returncode}); see {log}\n{process.stdout[-2000:]}{process.stderr[-2000:]}")
    return process.stdout


def snapshot(path, destination, inputs):
    data = path.read_bytes()
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(data)
    inputs[str(path.relative_to(WORKSPACE))] = hashlib.sha256(data).hexdigest()
    return destination


def build(args, output):
    inputs = {}
    src = output / "source"
    for path in sorted(SOURCE.rglob("*")):
        if path.is_file() and path.suffix in (".c", ".h", ".py", ".txt", ".md") and "__pycache__" not in path.parts:
            snapshot(path, src / path.relative_to(SOURCE), inputs)
    host = output / "host"
    host.mkdir()
    log = output / "commands.log"
    saved_fdt = snapshot(args.fdt.resolve(), output / "inputs/saved-fdt.dtb", inputs)
    snapshot(WORKSPACE / "ios-fuxi/shim/fuxi-loader/fuxi-ram-profile.h", src / "fuxi-ram-profile.h", inputs)
    common = ["menu.c", "render.c", "bootinfo.c", "spmi.c"]
    wsrc, whost = wsl_path(src), wsl_path(host)
    compile_args = ["clang", "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-I", wsrc,
                    *[f"{wsrc}/{x}" for x in common], f"{wsrc}/tests/host.c", "-o", f"{whost}/menu-host"]
    run(["wsl.exe", "-d", args.wsl, "--", "bash", "-c", shlex.join(compile_args)], log)
    host_stdout = run(["wsl.exe", "-d", args.wsl, "--", "bash", "-c",
                       shlex.join([f"{whost}/menu-host", whost, wsl_path(saved_fdt)])], log)
    print(host_stdout.strip())
    from PIL import Image
    for ppm in host.glob("*.ppm"):
        with Image.open(ppm) as image:
            image.save(ppm.with_suffix(".png"))
        ppm.unlink()
    report = {"result": "ok", "phone_operations": 0, "device_validation": "NOT_RUN",
              "host_tests": host_stdout.strip(), "default": args.default, "timeout_seconds": args.timeout,
              "input_sha256": inputs, "candidate": None}
    if args.host_only:
        for local, expected in inputs.items():
            need(sha(WORKSPACE / local) == expected, f"Input changed during host tests: {local}")
        return report
    base = args.base_chain.resolve()
    base_sums = manifest(base / "sha256.txt")
    for name in ("boot.img", "Image", "linux-Image"):
        need(sha(base / name) == base_sums[name], f"Base chain checksum mismatch: {name}")
    build_text = (base / "build.txt").read_text(encoding="utf-8")
    need("force_linux=False\n" in build_text, "Base must be a normal dual-boot chain")
    payload_name = re.search(r"^payload from (native-[a-z0-9-]+-loader)$", build_text, re.M)
    need(payload_name, "Base has no explicit payload provenance")
    payload = WORKSPACE / "work/Codex-Xiaomi13-GPU-20261109" / payload_name[1]
    payload_sums = manifest(payload / "sha256.txt")
    need(sha(payload / "payload.bin") == base_sums["payload.bin"] == payload_sums["payload.bin"], "Payload checksum mismatch")
    payload_data = (payload / "payload.bin").read_bytes()
    need(payload_data[:8] == b"SWEETXNU", "Not a Fuxi XNU payload")
    phys, usable = struct.unpack_from("<QQ", payload_data, 56)
    need(phys == 0x8c0000000 and usable in (0x13e000000, 0x23e000000), "Unsupported payload RAM map")
    ram9g = usable == 0x23e000000
    linux = (base / "linux-Image").read_bytes()
    need(linux[56:60] == b"ARM\x64", "Base Android kernel header missing")
    text_offset, image_size, flags = struct.unpack_from("<QQQ", linux, 8)
    need(text_offset == 0 and flags & 8 and image_size >= len(linux), "Android kernel cannot run at chain address")
    ndk = args.ndk
    if ndk is None:
        sdk = Path(os.environ.get("LOCALAPPDATA", "")) / "Android/Sdk/ndk"
        candidates = sorted(sdk.glob("*/toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe"))
        need(candidates, "NDK clang not found; specify --ndk <bin directory>")
        ndk = candidates[-1].parent
    need((ndk / "clang.exe").is_file(), "NDK directory must contain clang.exe")
    staged = output / "inputs"
    staged.mkdir(exist_ok=True)
    objects = {}
    for name in ("start.o", "probe.o", "chain.o", "linux.o"):
        objects[name] = snapshot(base / name, staged / name, inputs)
    loader_name = "loader-speaker.o" if (payload / "loader-speaker.o").is_file() else "loader.o"
    has_audio_dma = (payload / "loader-audio-dma.o").is_file()
    payload_objs = [loader_name, "console.o", "gpu-fw.o", "payload.o"]
    if has_audio_dma:
        payload_objs.append("loader-audio-dma.o")
    for name in payload_objs:
        objects[name] = snapshot(payload / name, staged / name, inputs)
    linker = snapshot(WORKSPACE / "ios-fuxi/shim/fuxi-loader/loader-chain.ld", staged / "loader-chain.ld", inputs)
    link_args = [ndk / "ld.lld.exe", "-T", linker, "--build-id=none", f"--defsym=LINUX_IMAGE_SIZE={image_size:#x}"]
    if has_audio_dma:
        link_args.append("--wrap=probe_main")
    order = ("start.o", loader_name, "console.o", "probe.o", "chain.o", "gpu-fw.o", "payload.o", "linux.o")
    if has_audio_dma:
        order = order + ("loader-audio-dma.o",)
    # Prove input-object identity against the known chain, including all padding.
    run([*link_args, *[objects[n] for n in order], "-o", output / "base-relink.elf"], log)
    run([ndk / "llvm-objcopy.exe", "-O", "binary", output / "base-relink.elf", output / "base-relink.Image"], log)
    need(sha(output / "base-relink.Image") == base_sums["Image"], "Base objects no longer reproduce the verified chain")
    print("BASE_RELINK_IDENTICAL")
    cflags = ["--target=aarch64-none-elf", "-O2", "-ffreestanding", "-fno-builtin", "-nostdlib", "-fno-pic",
              "-fno-stack-protector", "-fno-jump-tables", "-mgeneral-regs-only", "-mstrict-align", "-Wall", "-Wextra", "-Werror",
              "-std=c11", "-I", src, f"-DMENU_DEFAULT_IOS={int(args.default == 'ios')}",
              f"-DMENU_TIMEOUT_MS={args.timeout * 1000}"]
    if ram9g:
        cflags.append("-DFUXI_RAM_9G=1")
    menu_objects = []
    for name in [*common, "board.c"]:
        target = output / (Path(name).stem + ".o")
        run([ndk / "clang.exe", *cflags, "-c", src / name, "-o", target], log)
        relocs = run([ndk / "llvm-readelf.exe", "-r", target], log)
        need("R_AARCH64_ABS" not in relocs, f"Runtime relocation in {name}")
        menu_objects.append(target)
    final_objects = [objects[n] for n in order if n != "chain.o"] + menu_objects
    run([*link_args, *final_objects, "-o", output / "loader.elf"], log)
    run([ndk / "llvm-objcopy.exe", "-O", "binary", output / "loader.elf", output / "Image"], log)
    nm = run([ndk / "llvm-nm.exe", output / "loader.elf"], log)
    symbols = {parts[2]: int(parts[0], 16) for line in nm.splitlines()
               if len(parts := line.split()) == 3 and re.fullmatch(r"[0-9a-fA-F]+", parts[0])}
    image = (output / "Image").read_bytes()
    lp, pp, end = symbols["_linux_start"], symbols["_payload_start"], symbols["_image_end"]
    need(image[56:60] == b"ARM\x64" and struct.unpack_from("<Q", image, 16)[0] == end, "Candidate Image header invalid")
    need(lp % 0x200000 == 0 and end == lp + image_size, "Android Image placement invalid")
    need(image[lp:] == linux, "Embedded Android kernel changed")
    need(image[pp:pp + len(payload_data)] == payload_data, "Embedded iOS payload changed")
    need(symbols["_payload_end"] == pp + len(payload_data), "Payload size changed")
    if has_audio_dma:
        need("__wrap_probe_main" in symbols and "audio_dma" in symbols, "Audio DMA probe_main wrap or buffer missing")
        audio_dma_off = symbols["audio_dma"]
        need(symbols["_bss_start"] <= audio_dma_off and audio_dma_off + 0x10000 <= symbols["_bss_end"] <= pp,
             "Audio DMA buffer escapes BSS before payload")
        need(audio_dma_off % 0x4000 == 0, "Audio DMA buffer misaligned")
    has_sensor_pack = "_sensor_pack_start" in symbols
    if has_sensor_pack:
        sp0, sp1 = symbols["_sensor_pack_start"], symbols["_sensor_pack_end"]
        need(symbols["_payload_end"] <= sp0 < sp1 <= lp and sp0 % 8 == 0, "Sensor pack placement invalid")
        need(image[sp0:sp0 + 8] == b"SSCPACK1", "Sensor pack magic invalid")
    mkbootimg = snapshot(WORKSPACE / "uefi-fuxi/Resources/Scripts/mkbootimg.py", staged / "mkbootimg.py", inputs)
    run([sys.executable, mkbootimg, "--header_version", "4", "--kernel", output / "Image", "-o", output / "boot.img"], log)
    boot = (output / "boot.img").read_bytes()
    need(boot[:8] == b"ANDROID!" and struct.unpack_from("<I", boot, 40)[0] == 4, "Wrong boot header")
    need(struct.unpack_from("<I", boot, 12)[0] == 0 and boot[4096:4096 + len(image)] == image, "Boot image body mismatch")
    need(len(boot) <= 201326592, "Boot image exceeds the recorded boot partition size")
    for local, expected in inputs.items():
        need(sha(WORKSPACE / local) == expected, f"Input changed during build: {local}")
    report.update({"ram_layout": "9g" if ram9g else "5g", "base_chain": str(base), "base_image_sha256": base_sums["Image"],
                   "base_relink_identical": True, "payload": payload_name[1], "payload_sha256": sha(payload / "payload.bin"),
                   "kernel_sha256": payload_sums["kernel.development.fuxi"], "android_sha256": base_sums["linux-Image"],
                   "loader_object": loader_name,
                   "audio_dma_offset": symbols["audio_dma"] if has_audio_dma else None,
                   "sensor_pack_offset": symbols["_sensor_pack_start"] if has_sensor_pack else None,
                   "sensor_pack_size": (symbols["_sensor_pack_end"] - symbols["_sensor_pack_start"]) if has_sensor_pack else None,
                   "candidate": {"boot_img": str(output / "boot.img"), "sha256": sha(output / "boot.img"), "bytes": len(boot),
                                 "linux_offset": lp, "payload_offset": pp, "ram_footprint": end}})
    (output / "sha256.txt").write_text("".join(f"{sha(output / name)}  {name}\n" for name in ("boot.img", "Image", "loader.elf")), encoding="utf-8", newline="\n")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base-chain", type=Path, default=WORKSPACE / "work/boot-chain/chain-v7-ram9g-rtctorch")
    parser.add_argument("--output", type=Path, default=WORKSPACE / "work/dualboot/menu-v1")
    parser.add_argument("--default", choices=("android", "ios"), default="android")
    parser.add_argument("--timeout", type=int, default=10, help="Seconds before automatic Android boot (default: 10; 0 waits)")
    parser.add_argument("--wsl", default="Ubuntu-26.04")
    parser.add_argument("--ndk", type=Path, help="Windows NDK bin directory")
    parser.add_argument("--host-only", action="store_true")
    parser.add_argument("--fdt", type=Path, default=WORKSPACE / "work/hw-survey/fuxi-live-20261116.dtb",
                        help="Saved local FDT to replay; never read from a device")
    args = parser.parse_args()
    parser.error("--timeout must be 0..60") if not 0 <= args.timeout <= 60 else None
    output = args.output.resolve()
    need(output.is_relative_to(WORKSPACE / "work/dualboot"), "Output must be under work/dualboot")
    output.mkdir(parents=True, exist_ok=False)
    try:
        report = build(args, output)
    except Exception as exc:
        (output / "BUILD.txt").write_text(f"result FAILED\nphone_operations=0\n{exc}\n", encoding="utf-8", newline="\n")
        raise
    (output / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    (output / "BUILD.txt").write_text("result ok\nphone_operations=0\ndevice_validation=NOT_RUN\n", encoding="utf-8", newline="\n")
    print(f"BUILD_OK {output}\nphone_operations=0; device_validation=NOT_RUN")


if __name__ == "__main__":
    main()
