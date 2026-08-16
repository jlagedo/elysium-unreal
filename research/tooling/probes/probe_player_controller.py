"""Targeted binary probe of CPlayerEvents, CNPC_VPlayerController, and player controller embodiment in vampire.dll.
"""
import os, struct, re, sys
from elysium_pipeline.formats import install

def main():
    p = os.path.join(install.GAME, "dlls", "vampire.dll")
    d = open(p, "rb").read()

    e = struct.unpack_from("<I", d, 0x3C)[0]
    machine, nsect = struct.unpack_from("<HH", d, e+4)
    opt_size = struct.unpack_from("<H", d, e+20)[0]
    opt = e + 24
    image_base = struct.unpack_from("<I", d, opt+28)[0]
    sec_off = opt + opt_size
    sections = []
    for i in range(nsect):
        b = sec_off + i*40
        name = d[b:b+8].rstrip(b"\x00").decode("latin-1")
        vsize, va, rsize, rptr = struct.unpack_from("<IIII", d, b+8)
        sections.append((name, va, vsize, rptr, rsize))

    def va2off(va):
        rva = va - image_base
        for name, sva, vsize, rptr, rsize in sections:
            if sva <= rva < sva + max(vsize, rsize):
                return rptr + (rva - sva)
        return None

    def dump_hex(va, length=128, label=""):
        off = va2off(va)
        print(f"\n--- 0x{va:08x} ({label}) ({length} bytes) ---")
        chunk = d[off:off+length]
        for row in range(0, len(chunk), 16):
            h = " ".join(f"{b:02x}" for b in chunk[row:row+16])
            ascii_str = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk[row:row+16])
            print(f"  0x{va+row:08x}: {h:<48} |{ascii_str}|")

    dump_hex(0x101619d0, 160, "Player::RemoveControllerNPC tail")

if __name__ == "__main__":
    main()
