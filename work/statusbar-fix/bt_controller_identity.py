"""Extract the local controller identity from a complete HCI Read BD_ADDR exchange."""
import re


def controller_from_console(path):
    # The traced Read BD_ADDR completion can span RX chunks. Never join across
    # a new TX command, a truncated chunk, or two different reads of BD_ADDR.
    candidates = set()
    chunks = bytearray()
    collecting = complete = False
    for line in path.read_bytes().splitlines() + [b"FUXI BT H4: tx opcode=end"]:
        if b"FUXI BT H4: tx opcode=" in line:
            if collecting and complete:
                candidates.update(re.findall(rb"\x04\x0e\x0a\x01\x09\x10\x00(.{6})", chunks, re.S))
            chunks.clear()
            collecting = b"tx opcode=0x1009 " in line
            complete = False
        if collecting:
            complete |= b"rx opcode=0x1009 event=0xe status=0 bytes=10" in line
            raw = re.search(rb"FUXI BT RXRAW: \+\d+ms n=(\d+) ([a-f0-9]+)$", line)
            if raw:
                data = bytes.fromhex(raw[2].decode("ascii"))
                if len(data) == int(raw[1]):
                    chunks.extend(data)
                else:
                    chunks.clear()
    if len(candidates) != 1:
        raise ValueError("No unique complete controller identity in the reference boot")
    address = int.from_bytes(candidates.pop(), "little")
    if address in (0, (1 << 48) - 1):
        raise ValueError("Invalid local controller identity")
    return address
