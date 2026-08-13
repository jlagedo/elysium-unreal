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
#: Must match `UE_mdl_skeletal.VERSION` and `EskmVersion` in `ElysiumSkeletalSource.cpp`. This
#: reader touches only the sections above the clips, so a bump it does not otherwise care about
#: still lands here -- reading a stale container is what the check exists to prevent.
VERSION = 7


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


def bone_locals(blob):
    """[(name, parent index, translation, rotation)] in file order.

    Values are the float32 Unreal-native locals stated by the container. Keeping the decoded
    floats, rather than rounding them into a presentation form, lets the character partition ask
    whether two targets are genuinely the same baked pose.
    """
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
        offset += 4
        translation = struct.unpack_from("<3f", blob, offset)
        offset += 12
        rotation = struct.unpack_from("<4f", blob, offset)
        offset += 16
        out.append((name, parent, translation, rotation))
    return out


def bone_translations(blob):
    """{bone name: float32 local translation}, for exact family-pose compatibility."""
    return {name: translation for name, _parent, translation, _rotation in bone_locals(blob)}


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


def _casefold_tree(tree):
    """`tree` keyed and valued by lowercased bone name.

    **`FName` is case-insensitive, so a comparison that is not agrees with Unreal by luck.** The
    generic appendix names are exactly where the cast disagrees about case: `heather` hangs
    `bone01` off `Bip01 Spine1` while `buch` hangs `Bone01` off `Bip01 HeadNub`, which a
    case-sensitive dict reads as two unrelated bones and a skeleton reads as one bone with two
    parents."""
    return {bone.lower(): (par or "").lower() for bone, par in tree.items()}


def rig_trees_compatible(base, tree):
    """Whether `tree` can merge into `base`: every bone they share must agree on its parent, and
    the merge must leave exactly one root.

    Strict about the parent, including the root. A bone that is parentless in one tree and parented
    in the other is a conflict, because `USkeleton::MergeBonesToBoneTree` rejects exactly that -- a
    model whose VtMB skeleton forks carries a synthetic root above Bip01, and no amount of
    interpretation makes that the same shape as a Bip01-rooted one.

    **Agreeing on every shared bone is not sufficient, because two trees can share nothing.** A
    prop rooted at `Phone_bone_01` conflicts with no bone of a biped family and would merge in,
    giving the skeleton a second parentless bone -- which Unreal's single-rooted reference skeleton
    refuses at mesh-build time, long after the partition was decided."""
    low_base = _casefold_tree(base)
    low_tree = _casefold_tree(tree)
    if not all(bone not in low_base or low_base[bone] == par
               for bone, par in low_tree.items()):
        return False
    roots = {bone for bone, par in low_base.items() if not par}
    roots |= {bone for bone, par in low_tree.items() if not par}
    return len(roots) <= 1


def rig_families(trees, stems):
    """Partition the models into sets that one USkeleton can carry -> [{name, tree, stems}].

    The cast is not one rig. VtMB's Bip01 biped is consistent across every model and every bank --
    which is what makes a bank clip shareable at all -- but the appendix chains are not: the generic
    `BoneNN` hair names denote a different chain on different bodies, and Unreal's reference
    skeleton is single-rooted so the models whose VtMB skeleton forks carry a synthetic root above
    Bip01. A family is a maximal set that agrees on every bone it shares.

    Greedy and order-dependent by construction, so the stems are sorted: the partition has to be
    the same on every run or a re-bake renames the families out from under the meshes that point at
    them. **It is also a property of the whole set it is given**, which is why the caller partitions
    the entire cast even when it intends to bake the result a few families at a time.

    A PREDICTION of what Unreal will accept, not a guarantee -- `MergeAllBonesToBoneTree` is the
    authority, and the bake re-homes a model it refuses."""
    families = []
    for stem in sorted(stems):
        tree = trees[stem]
        for family in families:
            if rig_trees_compatible(family["tree"], tree):
                family["tree"].update(tree)
                family["stems"].append(stem)
                break
        else:
            families.append({"name": stem, "tree": dict(tree), "stems": [stem]})
    return families
