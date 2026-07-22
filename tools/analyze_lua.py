import struct
import sys

path = sys.argv[1] if len(sys.argv) > 1 else r"c:\PrivateProject\fx-wrapper\server\citizen-scripting-lua.dll"
with open(path, "rb") as f:
    data = f.read()

pe = struct.unpack_from("<I", data, 0x3C)[0]
num_sec = struct.unpack_from("<H", data, pe + 6)[0]
opt = pe + 24
sec = opt + struct.unpack_from("<H", data, pe + 20)[0]


def rva_to_off(rva):
    for i in range(num_sec):
        o = sec + i * 40
        vsize, vaddr, rawsize, rawptr = struct.unpack_from("<IIII", data, o + 8)
        if vaddr <= rva < vaddr + max(vsize, rawsize):
            return rawptr + (rva - vaddr)
    return None


def read_cstr(rva):
    off = rva_to_off(rva)
    if off is None:
        return None
    end = data.find(b"\x00", off)
    return data[off:end].decode("ascii", errors="replace")


def dump_table(file_off, count=12):
    print(f"\nTable at file {file_off:#x}:")
    for i in range(count):
        name = struct.unpack_from("<Q", data, file_off + i * 16)[0]
        func = struct.unpack_from("<Q", data, file_off + i * 16 + 8)[0]
        label = read_cstr(name - 0x180000000) if name else ""
        print(f"  [{i}] {label!r:12} name={name:#x} func={func:#x}")


dump_table(0x1FA470)
dump_table(0x1FA378 - 0x30, 8)

target = struct.pack("<Q", 0x180012C80)
idx = 0
while True:
    i = data.find(target, idx)
    if i < 0:
        break
    print(f"\nPointer to 180012C80 at file {i:#x} (va {0x180000000 + (i - rva_to_off(0) if False else 0):#x})")
    dump_table(i - 8, 2)
    idx = i + 1
