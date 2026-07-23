#!/usr/bin/env python3
import mmap
import struct
import sys
from pathlib import Path

def read_cstring(data, offset):
    end = data.find(b'\x00', offset)
    if end == -1:
        return None
    return data[offset:end].decode('ascii', errors='ignore')

def main():
    path = Path(sys.argv[1])
    data = path.read_bytes()
    
    # Find all "execute" strings
    execute_offs = []
    needle = b'execute\x00'
    start = 0
    while True:
        idx = data.find(needle, start)
        if idx == -1:
            break
        execute_offs.append(idx)
        start = idx + 1
    
    print(f'Found {len(execute_offs)} execute strings')
    
    # Find 8-byte pairs (name_ptr, fn_ptr) in file - note: file offsets != runtime for PIE
    # Look for execute string file offset appearing as qword in rodata/data sections
    for ex_off in execute_offs[:10]:
        ex_bytes = struct.pack('<Q', ex_off)  # won't match PIE runtime
        
    # Scan for luaL_Reg pattern with string names we know
    names = [b'clock\x00', b'date\x00', b'difftime\x00', b'execute\x00', b'remove\x00', b'rename\x00']
    name_offsets = {}
    for n in names:
        idx = data.find(n)
        if idx >= 0:
            name_offsets[n.decode().strip('\x00')] = idx
    print('String file offsets:', name_offsets)
    
    # Search for consecutive file offsets as pointer pairs (unrelocated - may show as offsets in ET_DYN)
    for section_name in [b'.rodata', b'.data.rel.ro', b'.data']:
        pass
    
    # Brute: find sequence of string refs in rodata
    rodata_start = data.find(b'Permission denied\x00')
    print(f'Permission denied at file offset {rodata_start:#x}')
    
    # Find references to execute string - as 4-byte or 8-byte in binary
    if 'execute' in name_offsets:
        ex = name_offsets['execute']
        refs = []
        packed = struct.pack('<Q', ex)
        pos = 0
        while True:
            idx = data.find(packed, pos)
            if idx == -1:
                break
            refs.append(idx)
            pos = idx + 1
        print(f'File-offset refs to execute string: {len(refs)}')
        for r in refs[:5]:
            print(f'  pair at {r:#x}: next qword = {struct.unpack("<Q", data[r+8:r+16])[0]:#x}')

if __name__ == '__main__':
    main()
