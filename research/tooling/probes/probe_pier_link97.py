"""Why does `sm_pier_1`'s link 37->59 route 3.5x its straight line?

0018 story 21-8's first nav finding. The link's ends both project and the Human mesh joins them,
so it is neither a hole nor a wall -- the mesh simply will not take the short way. Node 37 stands
4.6 m BELOW node 59 over 11.7 m of run (a 21.5 deg climb), which is a ramp rather than a step, so
the question is whether the surface between them became walkable mesh at all.

This walks the straight line from 37 to 59 in even steps and, at each station, sweeps z for every
height the Human mesh answers at. Three outcomes tell three stories:

  * mesh along the whole line, at the ramp's own rising height -> the corridor exists and the
    detour is a cost function or an obstruction, not a gap;
  * a gap in the middle stations -> the ramp did not rasterise, and the path must go around;
  * mesh at ONE height across every station -> there is no ramp here and the graph asserts a
    climb the geometry does not offer, which is the `sp_theatre` finding in another form.

Run inside a headless editor:
  UnrealEditor-Cmd.exe ElysiumUE.uproject -run=pythonscript
      -script=research/tooling/probes/probe_pier_link97.py -unattended -nullrhi
"""

from __future__ import annotations

import unreal

MAP = "sm_pier_1"
LEVEL = "/ElysiumBaked/%s/%s" % (MAP, MAP)
AGENT = "Human"

#: The link's two ends in the frame the acceptance key states (source inches * 2.54, Y reflected,
#: node z plus the hull-0 offset).
START = (6908.8, 680.7, -1537.3)
END = (6898.6, -487.7, -1076.6)
#: Node 47, the other end of node 37's only other link, and the four other nodes on node 59's
#: upper deck -- context for whichever way the path actually goes.
CONTEXT = {
    47: (6844.1, 1736.6, -1491.2),
    74: (7173.0, -315.0, -1076.6),
    75: (7142.5, -508.0, -1076.6),
    76: (6898.6, -508.0, -1076.6),
    77: (7152.6, 457.2, -1076.6),
}

STATIONS = 12
#: Wide enough to find the upper deck from a station standing under it (460 cm of separation)
#: and the water below, without reaching another storey.
SWEEP_CM = 900.0
STEP_CM = 25.0
EXTENT = unreal.Vector(40.0, 40.0, 12.0)


def log(message):
    unreal.log("[probe-pier-97] %s" % message)


def sweep(x, y, z):
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
    runs = []
    for offset, area in hits:
        if runs and runs[-1][2] == area and offset - runs[-1][1] <= STEP_CM + 0.01:
            runs[-1][1] = offset
        else:
            runs.append([offset, offset, area])
    return runs


def report(label, x, y, z):
    runs = collapse(sweep(x, y, z))
    if not runs:
        log("%-22s (%7.0f, %7.0f, %8.0f): NO mesh within +/-%.0f cm" % (label, x, y, z, SWEEP_CM))
        return
    log("%-22s (%7.0f, %7.0f, %8.0f): %s"
        % (label, x, y, z,
           "; ".join("%+.0f..%+.0f -> %s" % (lo, hi, area) for lo, hi, area in runs)))


if not unreal.EditorLoadingAndSavingUtils.load_map(LEVEL):
    raise SystemExit("[probe-pier-97] could not open %s" % LEVEL)
WORLD = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
if WORLD is None:
    raise SystemExit("[probe-pier-97] no editor world after opening %s" % LEVEL)

log("meshes: %s" % (", ".join(unreal.ElysiumNavBakeLibrary.nav_mesh_tile_counts(WORLD)) or "none"))
log("navigation-relevant components: %d"
    % unreal.ElysiumNavBakeLibrary.count_navigation_relevant_components(WORLD))
log("station offsets are relative to the straight line's own height at that station")

for step in range(STATIONS + 1):
    t = step / float(STATIONS)
    x = START[0] + (END[0] - START[0]) * t
    y = START[1] + (END[1] - START[1]) * t
    z = START[2] + (END[2] - START[2]) * t
    report("station %2d (t=%.2f)" % (step, t), x, y, z)

for index in sorted(CONTEXT):
    x, y, z = CONTEXT[index]
    report("node %d" % index, x, y, z)
