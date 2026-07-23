#!/usr/bin/env python3
import struct
from pathlib import Path

data = Path('/mnt/c/PrivateProject/fx-wrapper/server-linux/alpine/opt/cfx-server/libcitizen-scripting-lua.so').read_bytes()

def scan_table(table_off, max_entries=40):
    for i in range(max_entries):
        o = table_off + i * 16
        if o + 24 > len(data):
            break
        a, b = struct.unpack('<QQ', data[o:o + 16])
        next_fn = struct.unpack('<Q', data[o + 16:o + 24])[0]
        sa = ''
        if a and a < len(data):
            sa = data[a:a + 30].split(b'\0')[0].decode('ascii', 'replace')
        if sa.isprintable() and sa:
            print(f'  [{i}] {sa!r} meta={b:#x} next_fn={next_fn:#x}')

for off in [0x8920, 0x10ea0]:
    print('table', hex(off))
    scan_table(off)
