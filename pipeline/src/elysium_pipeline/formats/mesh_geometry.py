"""Shared source-mesh projection rules; safe in the editor's standard-library Python."""
from elysium_pipeline.formats.bsp import source_to_unreal, source_dir_to_unreal


def unreal_surface_normals(surface):
    """One surface's per-vertex shading normals, Unreal-native and unit length.

    VtMB authors a normal on every skinned vertex (`StudioVertex.VecNormal`,
    `docs/vtmb/mdl_v2531.md`), and it carries the artist's smoothing -- including the split
    normals at a hard edge, where studiomdl duplicated the vertex so each copy could face its own
    way. Averaging adjacent face normals cannot reproduce a split by construction, so the authored
    value is the only source for it.

    **Converted, never negated.** The Source->Unreal Y flip is orthogonal, so a normal carries
    through `_conv_dir` exactly like a position and needs no sign compensation. The reflection is
    answered once, by reversing triangle winding in `_mesh_section`, and answering it a second
    time here would leave every normal facing into the surface it belongs to.

    The check that settles the sign is the divergence theorem, because a wrong answer fails it: a
    closed mesh whose triangles wind so that `cross(b - a, c - a)` faces out has positive signed
    volume. Over the exported winding that quantity is NEGATIVE on every model in the cast, so
    outward is the other order -- which is what the fallback below computes, and what the
    converted authored normal already agrees with. Comparing a normal against a face normal of
    one's own choosing does not settle it; both can carry the same convention error and agree.

    A few vertices across the corpus store `(0,0,0)`. Those, and only those, take the
    area-weighted geometric normal, so every exported vertex carries a usable one and the bake is
    never left deciding what a missing normal means.
    """
    out = [source_dir_to_unreal(*n) for n in surface["nrm"]]
    missing = [i for i, n in enumerate(out) if n == (0.0, 0.0, 0.0)]
    if not missing:
        return out

    positions = [source_to_unreal(*p) for p in surface["pos"]]
    accumulated = [[0.0, 0.0, 0.0] for _ in out]
    for i, j, k in surface["tris"]:
        # The exported winding is (i, k, j), whose outward direction is cross(j - i, k - i) --
        # the order that makes the mesh's signed volume positive, per the docstring.
        ax, ay, az = (positions[j][c] - positions[i][c] for c in range(3))
        bx, by, bz = (positions[k][c] - positions[i][c] for c in range(3))
        # Unnormalised: the magnitude is twice the triangle area, which is the weighting.
        face = (ay * bz - az * by, az * bx - ax * bz, ax * by - ay * bx)
        for corner in (i, j, k):
            for c in range(3):
                accumulated[corner][c] += face[c]

    for i in missing:
        x, y, z = accumulated[i]
        length = (x * x + y * y + z * z) ** 0.5
        # A vertex no triangle references, or one whose faces cancel exactly. Up is arbitrary but
        # deterministic, and it beats emitting a zero the bake would have to interpret.
        out[i] = (x / length, y / length, z / length) if length > 1e-12 else (0.0, 0.0, 1.0)
    return out
