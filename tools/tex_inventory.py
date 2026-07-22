"""List the materials a BSP references.

TEXDATA_STRING_DATA (lump 43) is just a blob of null-terminated strings - the
material names/paths. TEXDATA_STRING_TABLE (lump 44) is int32 byte-offsets into
that blob. TEXDATA (lump 2) entries point (via nameStringTableID) at a table
entry. We can list every string directly (version-independent), and also count
how many the texdata actually uses.
"""
import struct, sys

def read_lump(data, index):
    fileofs, filelen = struct.unpack_from("<ii", data, 8 + index * 16)
    return data[fileofs:fileofs + filelen]

def strings_from_blob(blob):
    """Split the string-data lump into (offset -> string)."""
    out, start = {}, 0
    for i, b in enumerate(blob):
        if b == 0:
            out[start] = blob[start:i].decode("ascii", "replace")
            start = i + 1
    return out

def main(path):
    data = open(path, "rb").read()
    blob = read_lump(data, 43)
    table = read_lump(data, 44)
    texdata = read_lump(data, 2)

    by_offset = strings_from_blob(blob)
    offsets = list(struct.unpack_from("<%di" % (len(table)//4), table, 0))

    # string-table entry i -> material name at byte offset offsets[i]
    names = [by_offset.get(o, f"<bad@{o}>") for o in offsets]

    print(f"string-table entries : {len(offsets)}")
    print(f"distinct materials    : {len(set(names))}")
    print(f"texdata blob size     : {len(texdata)} bytes "
          f"({len(texdata)//32} entries @32B, {len(texdata)//24} @24B)")
    print()
    print("=== materials referenced by this map ===")
    for n in sorted(set(names), key=str.lower):
        print("  " + n)

if __name__ == "__main__":
    main(sys.argv[1])
