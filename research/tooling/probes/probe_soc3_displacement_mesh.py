"""Where is `sp_soc_3`'s navigation mesh, at the nodes its graph says an NPC can stand?

0018 story 21-2 left one map open. `sp_soc_3`'s graph asserts 19 ground links whose endpoints
project nowhere, in two rooms whose ground is displacement terrain rather than brush. Offline
reading of the triangle soup says those nodes DO have a nearly-flat displacement surface a few
centimetres below them and a ceiling six metres above, which should be a walkable span -- so the
question is what the mesh actually did there, and that can only be asked of the mesh.

This sweeps z at each failing node's XY and reports every height at which the Human mesh answers,
plus the area class it answers with. Three outcomes tell three different stories:

  * nothing at any height -> the region was not rasterised at all;
  * the mesh at the displacement's CEILING height (~+456 in room A, ~+150 in room B) and not at its
    floor -> Recast took the wrong surface, and the double-sided trimesh cook is the suspect;
  * the mesh at the floor height but a null area -> a nav-area mark is covering it.

Run inside a headless editor:
  UnrealEditor-Cmd.exe ElysiumUE.uproject -run=pythonscript
      -script=research/tooling/probes/probe_soc3_displacement_mesh.py -unattended -nullrhi
"""

from __future__ import annotations

import unreal

MAP = "sp_soc_3"
LEVEL = "/ElysiumBaked/%s/%s" % (MAP, MAP)
AGENT = "Human"

#: The failing links' endpoints, from `$ELYSIUM_WORK_ROOT/verify/nav/report.json`, with the node
#: positions the acceptance key states (hull height, centimetres).
NODES = {
    0: (3396.0, -782.0, -145.0),
    1: (3780.0, -742.0, -162.0),
    5: (4064.0, -1006.0, -162.0),
    7: (3386.0, -922.0, -132.0),
    8: (3000.0, -823.0, -148.0),
    9: (3343.0, -767.0, -140.0),
    13: (4201.0, -1285.0, -162.0),
    112: (4542.0, -538.0, -162.0),
    69: (-9230.0, 470.0, -325.0),
    70: (-9530.0, 20.0, -325.0),
    72: (-9733.0, 584.0, -325.0),
    73: (-9883.0, 178.0, -344.0),
    74: (-10406.0, 203.0, -429.0),
    75: (-10132.0, 460.0, -367.0),
    76: (-9733.0, 10.0, -325.0),
    77: (-9456.0, 528.0, -325.0),
}

#: A node that PASSES, as the control: if the sweep finds nothing even here, the probe is wrong
#: rather than the mesh. Node 20 carries ground links the gate did not report.
CONTROL = {20: None}

#: How far above and below the node to look, and in what steps. 700 cm reaches the displacement
#: ceiling in room A (+456 above a floor at -153) without reaching the cavern roof at +2,900.
SWEEP_CM = 700.0
STEP_CM = 25.0
#: The projection box at each height. Tight in z so a hit names the height it was found at; the
#: harness itself uses a taller one, which is why it cannot say WHERE the mesh is.
EXTENT = unreal.Vector(40.0, 40.0, 12.0)


def log(message):
    unreal.log("[probe-soc3-disp] %s" % message)


def sweep(x, y, z):
    """Every height at which the agent's mesh answers, as (offset from the node, area class)."""
    hits = []
    steps = int(SWEEP_CM / STEP_CM)
    for step in range(-steps, steps + 1):
        offset = step * STEP_CM
        area = unreal.ElysiumNavBakeLibrary.nav_area_at(
            WORLD, AGENT, unreal.Vector(x, y, z + offset), EXTENT)
        if area:
            hits.append((offset, area))
    return hits


def collapse(hits):
    """Runs of adjacent heights with the same answer, so a 2 m slab reads as one row."""
    runs = []
    for offset, area in hits:
        if runs and runs[-1][2] == area and offset - runs[-1][1] <= STEP_CM + 0.01:
            runs[-1][1] = offset
        else:
            runs.append([offset, offset, area])
    return runs


if not unreal.EditorLoadingAndSavingUtils.load_map(LEVEL):
    raise SystemExit("[probe-soc3-disp] could not open %s" % LEVEL)
WORLD = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
if WORLD is None:
    raise SystemExit("[probe-soc3-disp] no editor world after opening %s" % LEVEL)

log("meshes in the level: %s"
    % ", ".join(unreal.ElysiumNavBakeLibrary.nav_mesh_tile_counts(WORLD)) or "none")
log("navigation-relevant components: %d"
    % unreal.ElysiumNavBakeLibrary.count_navigation_relevant_components(WORLD))

for index in sorted(NODES) + sorted(CONTROL):
    position = NODES.get(index) or CONTROL.get(index)
    if position is None:
        continue
    x, y, z = position
    runs = collapse(sweep(x, y, z))
    if not runs:
        log("node %-4d (%.0f, %.0f, %.0f): the mesh answers at NO height within +/-%.0f cm"
            % (index, x, y, z, SWEEP_CM))
        continue
    log("node %-4d (%.0f, %.0f, %.0f): %s"
        % (index, x, y, z,
           "; ".join("%+.0f..%+.0f cm -> %s" % (lo, hi, area) for lo, hi, area in runs)))
