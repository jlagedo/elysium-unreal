"""Which of `la_ventruetower_3`'s four agents reach the nodes hull 20 fails on?

0018 story 21-8. The map is the corpus's only four-agent map and its only Manbat/Sheriff map.
Hulls 0, 7 and 21 are clean on it; every one of the 70 findings is hull 20, the Manbat -- and
they all trace back to a handful of nodes. Sheriff and Manbat stand at nearly the same place on
those nodes (their hull offsets differ by 7-8 units), so if the Sheriff's mesh answers where the
Manbat's does not, the cause is the AGENT and not the geometry.

Manbat is radius 101.6 cm / height 406.4 cm (retail's MANBAT_HULL, 80x80x160 source units);
Sheriff is 50.8 / 254. Both cut on 15 cm cells.
"""
from __future__ import annotations
import json
import unreal

LEVEL = "/ElysiumBaked/la_ventruetower_3/la_ventruetower_3"
AGENTS = ("Human", "TinyCentered", "Sheriff", "Manbat")
NODES = json.loads(open(r"E:\elysium-work\scratch\21-8\vt3_nodes.json").read())
EXTENT = unreal.Vector(40.0, 40.0, 12.0)


def log(m):
    unreal.log("[probe-vt3] %s" % m)


if not unreal.EditorLoadingAndSavingUtils.load_map(LEVEL):
    raise SystemExit("could not open level")
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
log("meshes: %s" % ", ".join(unreal.ElysiumNavBakeLibrary.nav_mesh_tile_counts(world)))

for key in sorted(NODES, key=int):
    row = NODES[key]
    log("node %s" % key)
    for agent in AGENTS:
        # Each agent asked at the position ITS OWN hull offset puts it at, where the node
        # carries one; Human and TinyCentered share hull 0's.
        x, y, z = row["cm20"] if agent == "Manbat" else (row["cm21"] if agent == "Sheriff" else row["cm20"])
        here = unreal.ElysiumNavBakeLibrary.nav_area_at(world, agent, unreal.Vector(x, y, z), EXTENT)
        # And swept, to say whether the mesh is merely at another height.
        found = []
        for step in range(-24, 25):
            zz = z + step * 25.0
            if unreal.ElysiumNavBakeLibrary.nav_area_at(world, agent, unreal.Vector(x, y, zz), EXTENT):
                found.append(zz)
        log("   %-14s at (%8.1f,%8.1f,%8.1f): %-16s   swept: %s"
            % (agent, x, y, z, here or "NO MESH",
               ", ".join("%.0f" % f for f in found) if found else "nothing within +/-600 cm"))
