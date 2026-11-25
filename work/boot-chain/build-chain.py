"""Build the slot-b chain image: XNU loader (auto-launch) + embedded Android kernel from boot_a.

用法：python build-chain.py <輸出目錄名> [--force-linux] [--auto-launch-s N] [--payload <loader 目錄名>]
  --force-linux   測試版：不管開機模式一律跳 Android 核心（用 fastboot boot 驗證 Linux 分支）
  --auto-launch-s loader 等電腦的秒數（預設 5；b 槽獨立開機不需要等 20 秒）
  --payload       payload（ADT、root extents、boot-args、XNU 核心）取自哪個 loader（預設 native-auto-cool-loader＝chain-v1；
                  chain-v2 用 native-dly1-user-loader）
Android 核心取自 boot_a 備份。
不寫入手機。
"""
import os
import hashlib
import importlib.util
import json
import re
import struct
import subprocess
import sys
from pathlib import Path

here = Path(__file__).resolve().parent
name = sys.argv[1]
force_linux = '--force-linux' in sys.argv
auto_s = int(sys.argv[sys.argv.index('--auto-launch-s') + 1]) if '--auto-launch-s' in sys.argv else 5
assert re.fullmatch(r'chain-[a-z0-9-]+', name) and 5 <= auto_s <= 60
out = here / name

tools = Path(os.environ.get('LOCALAPPDATA', ''), r'Android\Sdk\ndk\26.1.10909125\toolchains\llvm\prebuilt\windows-x86_64\bin')
src = Path(r'D:\xiaomi13\fuxi_ios\ios-fuxi\shim')
ref = Path(r'D:\xiaomi13\fuxi_ios\work\xiaomi13-ios-gpu-20261108\cpu-fallback-gpu-c-loader')
payload_name = sys.argv[sys.argv.index('--payload') + 1] if '--payload' in sys.argv else 'native-auto-cool-loader'
ram_sku = sys.argv[sys.argv.index('--ram-sku') + 1] if '--ram-sku' in sys.argv else 'auto'
assert ram_sku in ('auto', '8g', '12g')
assert re.fullmatch(r'native-[a-z0-9-]+-loader', payload_name)
cool = Path(r'D:\xiaomi13\fuxi_ios\work\Codex-Xiaomi13-GPU-20261109') / payload_name
payload = (cool / 'payload.bin').read_bytes()
assert payload[:8] == b'SWEETXNU'
physical_base, memory_size = struct.unpack_from('<2Q', payload, 56)
assert physical_base == 0x8c0000000 and memory_size in (0x13e000000, 0x23e000000)
ram9g = memory_size == 0x23e000000
source_tools = here.parents[1] / 'work/adsp-battery/ksrc/tools'
sys.path.insert(0, str(source_tools))
specification = importlib.util.spec_from_file_location('chain_ram_profile', source_tools / 'mkadt-fuxi.py')
profile = importlib.util.module_from_spec(specification)
specification.loader.exec_module(profile)
profile.configure_ram('9g' if ram9g else 'full')
sku_evidence = profile.validate_live(here.parents[1] / 'work/hw-survey/fuxi-live-20261116.dtb', True, ram_sku)
if ram9g:
    layout = json.loads((cool / 'memory-layout.json').read_text())
    assert layout['layout'] == '9g' and layout['usable'] == memory_size
    assert layout['scratch'] == 0xafe000000 and layout['scratch_size'] == 0x2000000
out.mkdir(exist_ok=False)
boot_a = Path(r'D:\xiaomi13\fuxi_Backup\android-partitions-20261107\boot_a.img')
mkbootimg = Path(r'D:\xiaomi13\fuxi_ios\uefi-fuxi\Resources\Scripts\mkbootimg.py')
sha = lambda b: hashlib.sha256(b).hexdigest()
run = lambda *a: subprocess.run([str(x) for x in a], check=True)

# Android 核心：boot_a 備份（= 手機上的 boot_a/boot_b，2026-11-14 比對過）裡的未壓縮 arm64 Image
b = boot_a.read_bytes()
assert sha(b) == '3d957296bb0db5596067ff1795ce521070f3dff5f5552834c2622bfeaa42d145'
ksize = struct.unpack_from('<I', b, 8)[0]
assert b[:8] == b'ANDROID!' and struct.unpack_from('<I', b, 40)[0] == 4 and struct.unpack_from('<I', b, 12)[0] == 0
kernel = b[4096:4096 + ksize]
assert kernel[56:60] == b'ARM\x64'
text_offset, image_size, flags = struct.unpack_from('<QQQ', kernel, 8)
assert text_offset == 0 and flags & 8 and image_size >= len(kernel)
(out / 'linux-Image').write_bytes(kernel)
(out / 'linux.S').write_text('.section .linux,"a"\n.incbin "' + (out / 'linux-Image').as_posix() + '"\n')

flags_c = ['--target=aarch64-none-elf', '-O2', '-ffreestanding', '-fno-builtin', '-nostdlib', '-fno-pic', '-fno-stack-protector',
           '-mgeneral-regs-only', '-mstrict-align', '-Wall', '-Wextra', '-Werror', '-DFUXI=1', '-DFUXI_XNU_LOADER=1', '-I' + str(src)]
if ram9g:
    flags_c.append('-DFUXI_RAM_9G=1')
chain_defs = ['-DFUXI_CHAIN=1'] + (['-DFUXI_CHAIN_FORCE_LINUX=1'] if force_linux else [])
run(tools / 'clang.exe', *flags_c, *chain_defs, '-c', src / 'fuxi-loader' / 'start.S', '-o', out / 'start.o')
run(tools / 'clang.exe', *flags_c, *chain_defs, '-c', src / 'fuxi-loader' / 'chain.c', '-o', out / 'chain.o')
run(tools / 'clang.exe', *flags_c, f'-DFUXI_AUTO_LAUNCH_S={auto_s}', '-c', src / 'fuxi-probe' / 'probe.c', '-o', out / 'probe.o')
run(tools / 'clang.exe', '--target=aarch64-none-elf', '-c', out / 'linux.S', '-o', out / 'linux.o')
for o in ('chain.o', 'probe.o'):
    rel = subprocess.check_output([str(tools / 'llvm-readelf.exe'), '-r', str(out / o)], text=True)
    assert 'R_AARCH64_ABS' not in rel, f'absolute relocation in {o}'

loader_objects = cool if ram9g else ref
loader_obj = loader_objects / ('loader-speaker.o' if (loader_objects / 'loader-speaker.o').exists() else 'loader.o')
audio_dma_obj = cool / 'loader-audio-dma.o'
extra_link_flags = ['--wrap=probe_main'] if audio_dma_obj.exists() else []
objects = [out / 'start.o', loader_obj, loader_objects / 'console.o', out / 'probe.o', out / 'chain.o',
           cool / 'gpu-fw.o', cool / 'payload.o', out / 'linux.o']
if audio_dma_obj.exists():
    objects.append(audio_dma_obj)
run(tools / 'ld.lld.exe', '-T', src / 'fuxi-loader' / 'loader-chain.ld', '--build-id=none',
    f'--defsym=LINUX_IMAGE_SIZE={image_size:#x}', *extra_link_flags, *objects, '-o', out / 'loader.elf')
run(tools / 'llvm-objcopy.exe', '-O', 'binary', out / 'loader.elf', out / 'Image')
img = (out / 'Image').read_bytes()
syms = {l.split()[-1]: int(l.split()[0], 16) for l in subprocess.check_output(
    [str(tools / 'llvm-nm.exe'), str(out / 'loader.elf')], text=True).splitlines() if len(l.split()) == 3}
lin, pay, end = syms['_linux_start'], syms['_payload_start'], syms['_image_end']
assert img[56:60] == b'ARM\x64' and struct.unpack_from('<Q', img, 16)[0] == end
assert lin % 0x200000 == 0 and img[lin:lin + len(kernel)] == kernel and len(img) == lin + len(kernel)
assert end == lin + image_size and img[pay:pay + len(payload)] == payload
if audio_dma_obj.exists():
    assert '__wrap_probe_main' in syms and 'audio_dma' in syms
    assert syms['_bss_start'] <= syms['audio_dma'] and syms['audio_dma'] + 0x10000 <= syms['_bss_end'] <= pay
    assert syms['audio_dma'] % 0x4000 == 0
if '_sensor_pack_start' in syms:
    sp0, sp1 = syms['_sensor_pack_start'], syms['_sensor_pack_end']
    assert pay + len(payload) <= sp0 < sp1 <= lin and sp0 % 8 == 0
    assert img[sp0:sp0 + 8] == b'SSCPACK1'
pinned = dict(l.split()[::-1] for l in (cool / 'sha256.txt').read_text().splitlines())
assert sha(payload) == pinned['payload.bin']
print(f'payload at {pay:#x}, linux at {lin:#x} (load base 0xa8000000 -> {0xa8000000 + lin:#x}), image end {end:#x} '
      f'({end / 2**20:.1f} MiB), file {len(img)} bytes')
# 開機映像：v4、無 ramdisk、os_version 0（同 boot_a，recovery 模式下 TWRP 的 keymint 看到的系統版本不變）
run(sys.executable, mkbootimg, '--header_version', '4', '--kernel', out / 'Image', '-o', out / 'boot.img')
boot = (out / 'boot.img').read_bytes()
assert boot[:8] == b'ANDROID!' and boot.count(img) == 1 and len(boot) <= 201326592
(out / 'sha256.txt').write_text(f'{sha(kernel)}  linux-Image\n{sha(payload)}  payload.bin\n{sha(img)}  Image\n{sha(boot)}  boot.img\n')
(out / 'build.txt').write_text(f'force_linux={force_linux}\nauto_launch_s={auto_s}\n'
                             f'physical_ram_sku={sku_evidence["ram_sku"]}\n'
                             f'ram_layout={"9g" if ram9g else "full"}\npayload from {cool.name}\nkernel from {boot_a}\n')
print('boot.img', sha(boot), len(boot))
