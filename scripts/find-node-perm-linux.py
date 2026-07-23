#!/usr/bin/env python3
import struct
import subprocess
from pathlib import Path

path = Path(__file__).resolve().parents[1] / "server-linux/alpine/opt/cfx-server/libcitizen-scripting-node.so"
data = path.read_bytes()

needle = b"Filesystem permission check from '%s' for permission %s on resource '%s' - path traversal detected"
perm_off = data.find(needle)
print("path traversal string at", hex(perm_off))

text_start = text_size = 0
out = subprocess.check_output(["readelf", "-S", str(path)], text=True)
for line in out.splitlines():
    if ".text" in line and "PROGBITS" in line:
        parts = line.split()
        text_start = int(parts[4], 16)
        text_size = int(parts[5], 16)
        print("text", hex(text_start), hex(text_size))

text = data[text_start : text_start + text_size]
hits = []
for i in range(len(text) - 7):
    b = text[i : i + 7]
    if b[0] not in (0x48, 0x4C) or b[1] != 0x8D:
        continue
    if (b[2] & 0xC7) != 0x05:
        continue
    disp = struct.unpack_from("<i", b, 3)[0]
    ref = text_start + i + 7 + disp
    if ref == perm_off:
        hits.append(text_start + i)

print("LEA hits:", [hex(h) for h in hits])


def find_prologue(off):
    rel = off - text_start
    for back in range(0, 0x800):
        if rel - back < 0:
            break
        cand = text[rel - back : rel - back + 4]
        if cand[:1] == b"\x55":  # push rbp
            return text_start + rel - back
        if cand[:3] == b"\x48\x89\x5c":
            return text_start + rel - back
        if cand[:2] == b"\x41\x57":
            return text_start + rel - back
    return None


for h in hits:
    fn = find_prologue(h)
    print("hit", hex(h), "fn", hex(fn) if fn else None)
    if fn:
        dis = subprocess.check_output(
            ["objdump", "-d", f"--start-address={fn}", f"--stop-address={fn + 0x60}", str(path)],
            text=True,
        )
        print(dis.split("Disassembly of section .text:")[1][:800])
