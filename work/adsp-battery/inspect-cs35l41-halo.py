"""Read the Fuxi lower speaker firmware and coefficient metadata offline.

Record layouts follow Linux v6.12 include/linux/firmware/cirrus/wmfw.h
and drivers/firmware/cirrus/cs_dsp.c. Control offsets are relative to
the algorithm base that the driver must verify on the physical DSP.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct


# CS35L41 HALO windows and last 32-bit register from Linux cs35l41.h.
PACKED_MEMORY = {
    0x10: (0x03800000, 0x03804FE8, 5),
    0x11: (0x02000000, 0x02002FF0, 3),
    0x12: (0x02C00000, 0x02C017F0, 3),
}
STOCK_PROTECTION_SHA256 = "c11cb3e25825c673a955a3519370ad77fbcd8bcf68808e8e34140a70caefcec4"
BOTTOM_TUNING_SHA256 = "67a3023c8c616f828961b0a649d39c7923d36029ffdf5e98dcc85c891189ded7"


def mapped_section(kind, offset, length):
    base, last, word_bytes = PACKED_MEMORY[kind]
    register = (base + offset * word_bytes) & ~3
    if length == 0 or length % 4 or register + length > last + 4:
        raise ValueError(f"Firmware section escapes CS35L41 window {kind:#x}")
    return dict(memory=kind, offset_words=offset, register=register, bytes=length)


def framed(data, offset, layout, alignment):
    header = struct.Struct(layout)
    while offset < len(data):
        fields = header.unpack_from(data, offset)
        start = offset + header.size
        end = start + fields[-1]
        next_offset = (end + alignment - 1) & -alignment
        if next_offset > len(data):
            raise ValueError("Truncated HALO section")
        yield fields[:-1], data[start:end]
        offset = next_offset


def read_string(data, offset, prefix_bytes):
    if offset + prefix_bytes > len(data):
        raise ValueError("Truncated HALO metadata string")
    start = offset + prefix_bytes
    length = int.from_bytes(data[offset:start], "little")
    end = start + length
    next_offset = (end + 3) & ~3
    if next_offset > len(data):
        raise ValueError("Truncated HALO metadata string")
    return data[start:end].decode("ascii"), next_offset


def controls(data):
    algorithm, = struct.unpack_from("<I", data)
    label, offset = read_string(data, 4, 1)
    _, offset = read_string(data, offset, 2)
    count, = struct.unpack_from("<I", data, offset)
    result = []
    for (word_offset, memory), block in framed(data, offset + 4, "<HHI", 1):
        name, offset = read_string(block, 0, 1)
        _, offset = read_string(block, offset, 1)
        _, offset = read_string(block, offset, 2)
        control_type, flags, size = struct.unpack_from("<HHI", block, offset)
        result.append(dict(name=name, memory=memory, offset_words=word_offset,
                           control_type=control_type, flags=flags, bytes=size))
    if len(result) != count:
        raise ValueError("HALO control count differs from the metadata header")
    return dict(id=algorithm, name=label, controls=result)


def inspect(firmware, coefficients):
    wmfw = firmware.read_bytes()
    wmdr = coefficients.read_bytes()
    magic, header_length, _, core, version = struct.unpack_from("<4sIHBB", wmfw)
    if (magic, header_length, core, version) != (b"WMFW", 40, 4, 3):
        raise ValueError("Expected the stock CS35L41 HALO v3 firmware format")
    magic, header_length, revision = struct.unpack_from("<4sI4s", wmdr)
    if magic != b"WMDR" or header_length != 16 or revision[-1] not in (1, 2):
        raise ValueError("Unexpected speaker coefficient header")
    algorithms = []
    sections = []
    for (location,), block in framed(wmfw, 40, "<II", 1):
        kind, offset = location >> 24, location & 0xFFFFFF
        if kind == 0xF2:
            algorithms.append(controls(block))
        elif kind in PACKED_MEMORY:
            sections.append(mapped_section(kind, offset, len(block)))
        elif kind not in (0xFC, 0xFE, 0xFF):
            raise ValueError(f"Unexpected firmware region {kind:#x}")
    sections.sort(key=lambda item: item["register"])
    for previous, current in zip(sections, sections[1:]):
        if previous["register"] + previous["bytes"] > current["register"]:
            raise ValueError("Overlapping HALO firmware sections")
    tuning = []
    algorithm_ids = {algorithm["id"] for algorithm in algorithms}
    for (offset, kind, algorithm, version, _), block in framed(wmdr, 16, "<HHIIII", 4):
        if kind in (0xFC00, 0xFE00, 0xFF00):
            continue
        if kind not in (5, 6, 0x11, 0x12) or algorithm not in algorithm_ids:
            raise ValueError("Unexpected coefficient memory or algorithm")
        tuning.append(dict(memory=kind, algorithm=algorithm, version=version >> 8,
                           offset_bytes=offset, bytes=len(block)))
    names = {control["name"]: control for algo in algorithms for control in algo["controls"]}
    required = ("HALO_STATE", "CSPL_STATE", "CAL_R", "CAL_STATUS", "CAL_CHECKSUM")
    if any(names.get(name, {}).get("bytes") != 4 for name in required):
        raise ValueError("Expected four-byte HALO state and speaker calibration controls")
    return dict(firmware_sha256=hashlib.sha256(wmfw).hexdigest(),
                coefficients_sha256=hashlib.sha256(wmdr).hexdigest(),
                firmware_sections=sections, algorithms=algorithms, coefficients=tuning)


def pack_stock_protection(firmware, full=False):
    """Build the muted probe's two XM sections, or the complete stock memory image."""
    data = firmware.read_bytes()
    if hashlib.sha256(data).hexdigest() != STOCK_PROTECTION_SHA256:
        raise ValueError("CS35L41 probe needs the Fuxi stock protection firmware")
    sections = []
    for (location,), block in framed(data, 40, "<II", 1):
        kind, offset = location >> 24, location & 0xFFFFFF
        if (full and kind in PACKED_MEMORY) or (kind == 0x11 and offset in (0, 0x34)):
            info = mapped_section(kind, offset, len(block))
            sections.append((info["register"], block))
    sections.sort(key=lambda section: section[0])
    if len(sections) != (6 if full else 2):
        raise ValueError("Unexpected number of stock CS35L41 memory sections")
    return struct.pack("<4sI", b"CSP2" if full else b"CSP1", len(sections)) + b"".join(
        struct.pack("<II", register, len(block)) + block for register, block in sections)


def pack_bottom_controls(firmware, calibration):
    """The handset's B tuning and persisted resistance, for muted readback only."""
    tuning = firmware.with_name("B-cs35l41-dsp1-spk-prot.bin").read_bytes()
    if hashlib.sha256(tuning).hexdigest() != BOTTOM_TUNING_SHA256:
        raise ValueError("Expected original bottom-channel protection coefficients")
    records = [(kind, offset, alg, version >> 8, block)
               for (offset, kind, alg, version, _), block in framed(tuning, 16, "<HHIIII", 4)
               if kind not in (0xFC00, 0xFE00, 0xFF00)]
    if [(k, o, a, v, len(b)) for k, o, a, v, b in records] != [
            (0x12, 308, 0xcd, 0x1d3f01, 4200), (6, 6008, 0xcd, 0x1d3f01, 4)]:
        raise ValueError("Unexpected CSPL coefficient layout")
    manifest = json.loads((calibration / "audio-config.json").read_text(encoding="utf-8"))["files"]
    def captured(suffix):
        name = "/mnt/vendor/persist/audio/crus_calr." + suffix
        data = (calibration / name.lstrip("/")).read_bytes()
        if len(data) != manifest[name]["bytes"] or hashlib.sha256(data).hexdigest() != manifest[name]["sha256"]:
            raise ValueError("Captured calibration file differs from its readback manifest")
        return data
    top, bottom = struct.unpack("<II", captured("bin"))
    lines = captured("txt").decode("ascii").splitlines()
    if len(lines) != 2 or not all(re.fullmatch(rf"CH\[{i}\]\s+{label}\s*:\s*{value}\s+\([0-9.]+\)\s*", line)
                                  for i, (label, value, line) in enumerate(zip("TB", (top, bottom), lines))):
        raise ValueError("Persisted channel labels and binary resistance values disagree")
    if not 0 < bottom < 0xffffff:
        raise ValueError("Bottom resistance or checksum exceeds a 24-bit control")
    return b"CSB1" + struct.pack(">II", bottom, bottom + 1) + records[0][-1] + records[1][-1]


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("firmware", type=Path)
    parser.add_argument("coefficients", type=Path)
    args = parser.parse_args()
    print(json.dumps(inspect(args.firmware, args.coefficients), indent=2))
