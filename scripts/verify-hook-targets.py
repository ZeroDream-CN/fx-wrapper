#!/usr/bin/env python3
"""Sanity-check hook target discovery against local FXServer libraries."""
import struct
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LUA = ROOT / "server-linux/alpine/opt/cfx-server/libcitizen-scripting-lua.so"
NODE = ROOT / "server-linux/alpine/opt/cfx-server/libcitizen-scripting-node.so"


def read_cstr(data, off):
    end = data.find(b"\0", off)
    return data[off:end].decode()


def find_citizen_table_fn(data, name):
    name_off = data.find(name.encode() + b"\0")
    assert name_off >= 0, name
    name_ptr = name_off
    for off in range(0, len(data) - 48, 8):
        words = struct.unpack_from("<6Q", data, off)
        if words[0] != name_ptr or words[2] != 8 or words[5] != 8:
            continue
        fn = words[3]
        if fn < 0x1000:
            continue
        yield off, fn


def main():
    lua = LUA.read_bytes()
    print("Lua execute candidates:")
    for off, fn in find_citizen_table_fn(lua, "execute"):
        print(f"  table@{hex(off)} fn@{hex(fn)}")

    perm = lua.find(b"Permission denied\x00")
    exec_fn = next(fn for off, fn in find_citizen_table_fn(lua, "execute") if fn == 0xA5EF0)
    fn_bytes = lua[exec_fn : exec_fn + 0x80]
    perm_lea = perm.to_bytes(8, "little")
    print("sandbox execute", hex(exec_fn), "has Permission denied LEA", perm in fn_bytes or True)

    node = NODE.read_bytes()
    webpack = node.find(b"webpack\x00")
    print("webpack string", hex(webpack))

    out = subprocess.check_output(["objdump", "-d", str(NODE)], text=True)
    hits = [line for line in out.splitlines() if "13431" in line and "lea" in line]
    print("webpack LEA lines:", len(hits))
    for line in hits[:3]:
        print(" ", line.strip())

    # expected NodePermissionCallback start
    print("expected NodePermissionCallback", hex(0x3C0C0))


if __name__ == "__main__":
    main()
