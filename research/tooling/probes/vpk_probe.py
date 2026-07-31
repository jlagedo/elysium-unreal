"""Probe the VtMB .vpk container format by eyeballing header bytes and tail."""
import struct, sys, os

def main():
    path = sys.argv[1]
    size = os.path.getsize(path)
    with open(path, "rb") as f:
        head = f.read(64)
        f.seek(max(0, size - 64))
        tail = f.read(64)

    print(f"file : {path}")
    print(f"size : {size:,} bytes")

    def hexdump(label, b, base=0):
        print(f"\n{label}")
        for i in range(0, len(b), 16):
            chunk = b[i:i+16]
            hx = " ".join(f"{x:02x}" for x in chunk)
            asc = "".join(chr(x) if 32 <= x < 127 else "." for x in chunk)
            print(f"  {base+i:08x}  {hx:<48}  {asc}")

    hexdump("first 64 bytes:", head, 0)
    hexdump("last 64 bytes:", tail, size - 64)

    # Interpret first ints a few ways to spot a signature / offsets.
    u32 = struct.unpack_from("<8I", head, 0)
    print("\nfirst 8 little-endian uint32s:")
    for i, v in enumerate(u32):
        print(f"  [{i}] @{i*4:>2}: {v:>12}  (0x{v:08x})")
    sig = head[:4]
    print(f"\nfirst 4 bytes as ascii: {sig!r}")

if __name__ == '__main__':
    main()
