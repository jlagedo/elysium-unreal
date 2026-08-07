"""Read the header sections of Elysium's `.eskm` skeletal container.

The container is written by `exporters/UE_mdl_skeletal.py` and consumed in full by
`FElysiumSkeletalSource` on the C++ side. This module reads only the two sections the offline
bake orchestration needs before it hands a file to the editor -- the bone tree, to partition
models into rig families, and the material table, to know which textures to import. Geometry,
morph targets and clips are never decoded here; they are the editor's business.

Layout is documented once, at the top of `UE_mdl_skeletal.py`.
"""
import struct

MAGIC = b"ESKM"
VERSION = 3


def _string(blob, offset):
    length = struct.unpack_from("<I", blob, offset)[0]
    offset += 4
    return blob[offset:offset + length].decode("utf-8"), offset + length


def directory(blob):
    """{section tag: (offset, size)}. Raises ValueError on a foreign or stale container."""
    magic, version, count, _reserved = struct.unpack_from("<4sIII", blob, 0)
    if magic != MAGIC:
        raise ValueError("not an .eskm container")
    if version != VERSION:
        raise ValueError(f"container is version {version}, expected {VERSION} - re-export")
    out, offset = {}, 16
    for _ in range(count):
        tag, at, size = struct.unpack_from("<4sQQ", blob, offset)
        offset += 20
        out[tag] = (at, size)
    return out


def read(path):
    with open(path, "rb") as handle:
        return handle.read()


def bones(blob):
    """[(name, parent index)] in file order. The parent of a root is -1."""
    where = directory(blob).get(b"SKEL")
    if where is None:
        return []
    offset = where[0]
    count = struct.unpack_from("<I", blob, offset)[0]
    offset += 4
    out = []
    for _ in range(count):
        name, offset = _string(blob, offset)
        parent = struct.unpack_from("<i", blob, offset)[0]
        offset += 4 + 12 + 16                       # parent, then the transform this skips
        out.append((name, parent))
    return out


def bone_parents(blob):
    """{bone name: parent bone name}, with "" for a root -- the shape rig-family comparison
    wants, because a family is defined by names agreeing on their parent, not by indices."""
    rows = bones(blob)
    return {name: (rows[parent][0] if 0 <= parent < len(rows) else "")
            for name, parent in rows}


def materials(blob):
    """{material name: albedo path relative to the export root}. The albedo is "" when the
    material resolved no `$basetexture`."""
    where = directory(blob).get(b"MATL")
    if where is None:
        return {}
    offset = where[0]
    count = struct.unpack_from("<I", blob, offset)[0]
    offset += 4
    out = {}
    for _ in range(count):
        name, offset = _string(blob, offset)
        albedo, offset = _string(blob, offset)
        out[name] = albedo
    return out
