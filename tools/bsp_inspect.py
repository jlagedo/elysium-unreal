"""
BSP inspector: read the Source .bsp header and dump the 64-lump directory.

A Source .bsp is a container. The header is:
    int   ident        # magic, ASCII "VBSP"
    int   version      # BSP format version
    lump_t lumps[64]   # the table of contents
    int   mapRevision

Each lump_t entry is 16 bytes:
    int  fileofs   # byte offset where this lump's data begins
    int  filelen   # length in bytes
    int  version   # per-lump format version
    char fourCC[4] # compression tag (usually all zero = uncompressed)
"""
import struct
import sys

# The 64 lump slots have fixed, well-known meanings. Names for the ones we care
# about now; the rest are labeled generically.
LUMP_NAMES = {
    0: "ENTITIES",        1: "PLANES",          2: "TEXDATA",        3: "VERTEXES",
    4: "VISIBILITY",      5: "NODES",           6: "TEXINFO",        7: "FACES",
    8: "LIGHTING",        9: "OCCLUSION",      10: "LEAFS",         11: "FACEIDS",
    12: "EDGES",         13: "SURFEDGES",      14: "MODELS",        15: "WORLDLIGHTS",
    16: "LEAFFACES",     17: "LEAFBRUSHES",    18: "BRUSHES",       19: "BRUSHSIDES",
    20: "AREAS",         21: "AREAPORTALS",    22: "UNUSED22",      23: "UNUSED23",
    24: "UNUSED24",      25: "UNUSED25",       26: "DISPINFO",      27: "ORIGINALFACES",
    28: "PHYSDISP",      29: "PHYSCOLLIDE",    30: "VERTNORMALS",   31: "VERTNORMALINDICES",
    32: "DISP_LIGHTMAP_ALPHAS", 33: "DISP_VERTS", 34: "DISP_LIGHTMAP_SAMPLE_POSITIONS",
    35: "GAME_LUMP",     36: "LEAFWATERDATA",  37: "PRIMITIVES",    38: "PRIMVERTS",
    39: "PRIMINDICES",   40: "PAKFILE",        41: "CLIPPORTALVERTS", 42: "CUBEMAPS",
    43: "TEXDATA_STRING_DATA", 44: "TEXDATA_STRING_TABLE", 45: "OVERLAYS",
    46: "LEAFMINDISTTOWATER", 47: "FACE_MACRO_TEXTURE_INFO", 48: "DISP_TRIS",
    49: "PHYSCOLLIDESURFACE", 50: "WATEROVERLAYS", 51: "LEAF_AMBIENT_INDEX_HDR",
    52: "LEAF_AMBIENT_INDEX", 53: "LIGHTING_HDR", 54: "WORLDLIGHTS_HDR",
    55: "LEAF_AMBIENT_LIGHTING_HDR", 56: "LEAF_AMBIENT_LIGHTING", 57: "XZIPPAKFILE",
    58: "FACES_HDR",     59: "MAP_FLAGS",      60: "OVERLAY_FADES", 61: "UNUSED61",
    62: "UNUSED62",      63: "UNUSED63",
}

def inspect(path):
    with open(path, "rb") as f:
        data = f.read()

    ident, version = struct.unpack_from("<ii", data, 0)
    magic = struct.pack("<i", ident).decode("ascii", "replace")

    print(f"File          : {path}")
    print(f"Size          : {len(data):,} bytes")
    print(f"Magic (ident) : {ident:#010x}  ('{magic}')")
    print(f"BSP version   : {version}")
    print()

    # Lump directory starts right after ident+version (8 bytes in).
    offset = 8
    print(f"{'#':>2}  {'lump name':<26} {'fileofs':>10} {'filelen':>10} {'ver':>4}  {'fourCC'}")
    print("-" * 72)
    lumps = []
    for i in range(64):
        fileofs, filelen, lversion = struct.unpack_from("<iii", data, offset)
        fourcc = data[offset + 12: offset + 16]
        lumps.append((fileofs, filelen, lversion, fourcc))
        offset += 16
        if filelen > 0:  # only show lumps that actually contain data
            cc = " ".join(f"{b:02x}" for b in fourcc)
            name = LUMP_NAMES.get(i, f"LUMP_{i}")
            print(f"{i:>2}  {name:<26} {fileofs:>10} {filelen:>10} {lversion:>4}  {cc}")

    map_revision = struct.unpack_from("<i", data, offset)[0]
    print("-" * 72)
    print(f"map revision  : {map_revision}")

    # A couple of derived facts that matter for the geometry step.
    vtx_ofs, vtx_len = lumps[3][0], lumps[3][1]
    print()
    print(f"VERTEXES lump : {vtx_len:,} bytes -> {vtx_len // 12:,} vertices (12 bytes each)")
    face_len = lumps[7][1]
    print(f"FACES lump    : {face_len:,} bytes -> {face_len // 56:,} faces (56 bytes each, v19)")
    edge_len = lumps[12][1]
    print(f"EDGES lump    : {edge_len:,} bytes -> {edge_len // 4:,} edges (4 bytes each)")

if __name__ == "__main__":
    inspect(sys.argv[1] if len(sys.argv) > 1 else "ch_hub_1.bsp")
