"""The corridor `sm_pier_1`'s Human mesh actually offers between nodes 37 and 59."""
from __future__ import annotations
import unreal

LEVEL = "/ElysiumBaked/sm_pier_1/sm_pier_1"
START = unreal.Vector(6908.8, 680.7, -1537.3)
END = unreal.Vector(6898.6, -487.7, -1076.6)


def log(m):
    unreal.log("[probe-pier-path] %s" % m)


if not unreal.EditorLoadingAndSavingUtils.load_map(LEVEL):
    raise SystemExit("could not open level")
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()

path = unreal.NavigationSystemV1.find_path_to_location_synchronously(world, START, END)
if path is None:
    raise SystemExit("[probe-pier-path] no path object")
points = list(path.path_points)
log("valid=%s points=%d" % (path.is_valid(), len(points)))
total = 0.0
previous = None
for index, p in enumerate(points):
    step = 0.0 if previous is None else (p - previous).length()
    total += step
    log("  %2d  (%8.1f, %8.1f, %8.1f)  +%7.1f cm  cumulative %8.1f" % (index, p.x, p.y, p.z, step, total))
    previous = p
log("path length %.1f cm; straight %.1f cm" % (total, (END - START).length()))

# Lateral fan at the stations where the climbing surface vanished, to find where it goes.
EXTENT = unreal.Vector(40.0, 40.0, 12.0)
for label, base in (("station 7", (6903.0, -1.0, -1269.0)),
                    ("station 8", (6902.0, -98.0, -1230.0)),
                    ("station 9", (6901.0, -196.0, -1192.0))):
    for dx in (-600, -400, -200, 0, 200, 400, 600):
        hits = []
        for step in range(-36, 37):
            z = base[2] + step * 25.0
            area = unreal.ElysiumNavBakeLibrary.nav_area_at(
                world, "Human", unreal.Vector(base[0] + dx, base[1], z), EXTENT)
            if area:
                hits.append(z)
        log("%s dx%+5d: %s" % (label, dx,
                               ", ".join("%.0f" % h for h in hits) if hits else "no mesh"))
