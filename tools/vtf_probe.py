"""Probe a VTF texture header (extracted from a VPK) to learn version + format."""
import struct, sys
import vpk

# VTF high-res image format enum (Valve's IMAGE_FORMAT). Common ones:
IMAGE_FORMATS = {
    0: "RGBA8888", 1: "ABGR8888", 2: "RGB888", 3: "BGR888", 4: "RGB565",
    5: "I8", 6: "IA88", 7: "P8", 8: "A8", 9: "RGB888_BLUESCREEN",
    10: "BGR888_BLUESCREEN", 11: "ARGB8888", 12: "BGRA8888", 13: "DXT1",
    14: "DXT3", 15: "DXT5", 16: "BGRX8888", 17: "BGR565", 18: "BGRX5551",
    19: "BGRA4444", 20: "DXT1_ONEBITALPHA", 21: "BGRA5551", 22: "UV88",
    23: "UVWQ8888", 24: "RGBA16161616F", 25: "RGBA16161616", 26: "UVLX8888",
    0xFFFFFFFF: "NONE",
}

GAME = r"E:/dev_game/Vampire The Masquerade - Bloodlines/Vampire"
idx = vpk.index_all(GAME)

target = sys.argv[1] if len(sys.argv) > 1 else "materials/ground/streetb.vtf"
raw = vpk.extract(idx[target])
print(f"{target}: {len(raw):,} bytes\n")

sig = raw[:4]
vmaj, vmin = struct.unpack_from("<II", raw, 4)
header_size = struct.unpack_from("<I", raw, 12)[0]
width, height = struct.unpack_from("<HH", raw, 16)
flags = struct.unpack_from("<I", raw, 20)[0]
frames, first_frame = struct.unpack_from("<HH", raw, 24)
# reflectivity vec3 at 32; then padding
reflectivity = struct.unpack_from("<fff", raw, 32)
bump_scale = struct.unpack_from("<f", raw, 48)[0]
hi_format = struct.unpack_from("<I", raw, 52)[0]
mip_count = raw[56]
lo_format = struct.unpack_from("<I", raw, 57)[0]
lo_w, lo_h = raw[61], raw[62]

print(f"signature     : {sig!r}")
print(f"version       : {vmaj}.{vmin}")
print(f"header size   : {header_size}")
print(f"dimensions    : {width} x {height}")
print(f"flags         : 0x{flags:08x}")
print(f"frames        : {frames} (first {first_frame})")
print(f"reflectivity  : {tuple(round(v,3) for v in reflectivity)}")
print(f"bump scale    : {bump_scale}")
print(f"high-res fmt  : {hi_format} ({IMAGE_FORMATS.get(hi_format,'?')})")
print(f"mip count     : {mip_count}")
print(f"low-res fmt   : {lo_format} ({IMAGE_FORMATS.get(lo_format,'?')})")
print(f"low-res dims  : {lo_w} x {lo_h}")
print(f"\nfirst 72 bytes:")
print(" ".join(f"{b:02x}" for b in raw[:72]))
