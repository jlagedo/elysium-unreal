"""VtMB's authored garment payload as an Unreal-native simulation mesh sidecar.

`UE_`-prefixed because it emits coordinates: centimetres, Z-up, left-handed, read
1:1 by the asset generator with no conversion at the far end
(`docs/project/rebuild-strategy.md` -> "Coordinate conventions"). The decode is
`formats/mdl_cloth.py`, which stays in the model's own Source frame; everything
that changes basis lives here.

The space is the same one the baked character mesh already occupies, and that is
not a coincidence to be re-derived per consumer. A character reaches Unreal as
Source -> glTF `(x, z, -y) * 0.0254` -> glTFRuntime's canonical basis
`(X, Z, Y) * 100`, and composing those two collapses exactly to
`(x, -y, z) * 2.54` -- which is `bsp.source_to_unreal`. So the garment is written
through the repository's own conversion rather than through the glTF import path
it never travelled, and it still lands where the mesh it hangs on is.

The Y negation makes that conversion a reflection, so **triangle winding is
reversed here**, once, exactly as the OBJ exporters do at write time.

The test that decides the sign is the LEFT-hand normal, not the right-hand one.
`FVector::Cross` computes the usual determinant, but Unreal's basis is left
handed, so the result follows the left-hand rule geometrically -- and both of the
things that go wrong follow it too. Backface culling takes Unreal's clockwise
front face, and the cloth render path derives its shading normal as
`-SimNormal`, which lands on the same vector. So a garment is wound correctly
when its faces' LEFT-hand normals point away from its own axis; getting that
backwards culls the whole surface and the garment reads as transparent rather
than as inside-out.
"""

from __future__ import annotations

import json
import os

from elysium_pipeline.asset_names import rig_bone_name
from elysium_pipeline.formats import install, mdl_cloth
from elysium_pipeline.formats.bsp import source_to_unreal

#: Source inches -> Unreal centimetres. The one scalar `source_to_unreal` applies to
#: a position, needed on its own for a radius and squared for a squared length.
INCH_TO_CM = 2.54


def _point(p):
    return list(source_to_unreal(p[0], p[1], p[2]))


def build_sidecar(data: bytes, vtx: bytes) -> list[dict]:
    """Every garment on this model, converted into Unreal-native space."""
    garments = mdl_cloth.build(data, vtx)
    for g in garments:
        # Bone references bind by name against the baked mesh, whose skeleton names pass
        # `rig_bone_name` -- the sidecar has to say the same name.
        for cap in g["capsules"]:
            for key in ("bone_a_name", "bone_b_name"):
                if cap.get(key):
                    cap[key] = rig_bone_name(cap[key])
        for sph in g["spheres"]:
            if sph.get("bone_name"):
                sph["bone_name"] = rig_bone_name(sph["bone_name"])
        g["rest_positions"] = [_point(p) for p in g["rest_positions"]]
        # Winding reversed with the reflection, once and here. See the module docstring for
        # which normal decides the sign -- it is the left-hand one.
        g["triangles"] = [[t[0], t[2], t[1]] for t in g["triangles"]]
        for c in g["constraints"]:
            c["rest_length_squared"] *= INCH_TO_CM * INCH_TO_CM
        # The render surfaces travel with the garment, in the same basis as everything else.
        # Their winding follows the same rule as the simulation mesh's, for the same reason.
        for entry in g["render_maps"]:
            entry["positions"] = [_point(p) for p in entry["positions"]]
            entry["triangles"] = [[t[0], t[2], t[1]] for t in entry["triangles"]]
            entry["skin"] = [[[rig_bone_name(name), weight] for name, weight in influences]
                             for influences in entry["skin"]]

        # Colliders are carried in BIND space, not in their bone's local frame.
        #
        # Bind space is invariant across bone-index renumbering and keeps this payload independent
        # of which compatible rig-family skeleton consumes it. The generator converts it to the
        # actual mesh bone's local frame from that mesh's authored reference skeleton.
        for cap in g["capsules"]:
            cap["a_bind"] = _point(cap["a_bind"])
            cap["b_bind"] = _point(cap["b_bind"])
            cap["radius"] *= INCH_TO_CM
            cap.pop("a_local", None)
            cap.pop("b_local", None)
        for sph in g["spheres"]:
            sph["centre_bind"] = _point(sph["centre_bind"])
            sph["radius"] *= INCH_TO_CM
            sph.pop("centre_local", None)
    return garments


def write(stem: str, model_key: str, idx: dict, out_dir: str) -> dict:
    """Write `<out_dir>/<stem>.json`, or `{}` when this model authors no garment."""
    data = install.read(idx, model_key)
    if not data or not mdl_cloth.has_cloth(data):
        return {}
    vtx = install.read(idx, model_key[:-4] + ".dx80.vtx")
    if not vtx:
        return {}
    garments = build_sidecar(data, vtx)
    if not garments:
        return {}

    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, stem + ".json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(
            {
                "stem": stem,
                "model": model_key,
                "note": "Unreal-native: centimetres, Z-up, left-handed, read 1:1 with no "
                        "conversion. `rest_length_squared` is in those centimetres SQUARED, "
                        "and triangle winding is already reversed for the Source->Unreal "
                        "reflection, leaving each face's LEFT-hand normal pointing outward - "
                        "which is what both backface culling and the cloth render path's "
                        "shading normal follow. `rest_uvs` are the garment's own texture coordinates, "
                        "one per particle, carried through unconverted because a UV is not a "
                        "coordinate in this basis -- they address the same material page the "
                        "baked body does. `triangles` index `rest_positions` and are the simulation "
                        "topology induced from the render surface, not the authored collision "
                        "proxy, which is not a manifold. `render_maps[].vertices` is indexed by "
                        "the glb's own vertex order for that material; a null entry keeps "
                        "ordinary skinning. `anchor_skin` and the collider bones are named, not "
                        "indexed, because the host skeleton renumbers. Collider points are in BIND space "
                        "so the generator resolves them through the consuming mesh's authored bind. See "
                        "docs/vtmb/secondary_motion.md.",
                "garments": garments,
            },
            f,
            separators=(",", ":"),
        )
    return {
        "garment": "garment/" + os.path.basename(path),
        "garment_particles": sum(g["particle_count"] for g in garments),
    }
