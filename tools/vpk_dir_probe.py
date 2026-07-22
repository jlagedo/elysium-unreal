"""Dump the suspected directory region of a VtMB .vpk to derive the entry format."""
import sys, struct

path = sys.argv[1]
ofs = int(sys.argv[2], 0) if len(sys.argv) > 2 else None

with open(path, "rb") as f:
    data = f.read()

# If no offset given, read the last 4 bytes as a footer pointer candidate,
# and also try the uint32 that sits near the tail.
if ofs is None:
    footer = struct.unpack_from("<I", data, len(data) - 4)[0]
    print(f"last 4 bytes as uint32 : {footer} (0x{footer:08x})  filesize={len(data)}")
    ofs = footer

print(f"dumping from offset 0x{ofs:08x} ({ofs})\n")
region = data[ofs:ofs + 512]
for i in range(0, len(region), 16):
    chunk = region[i:i+16]
    hx = " ".join(f"{x:02x}" for x in chunk)
    asc = "".join(chr(x) if 32 <= x < 127 else "." for x in chunk)
    print(f"  {ofs+i:08x}  {hx:<48}  {asc}")
