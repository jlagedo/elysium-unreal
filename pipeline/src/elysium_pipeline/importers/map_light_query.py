"""Native gameplay-light inputs from the authoritative V2 map units (0005 requirement 1).

Rendering calibration is absent. The world-light service needs the original BSP partition/PVS,
mask-0x4191 world brushes, displacement collision and SURF_SKY brush faces. The existing hull
and displacement producers own geometry; this stage selects the light-query mask only.
"""
from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path

import numpy as np

from elysium_pipeline.exporters import UE_map_sidecars as sidecars
from elysium_pipeline.formats.bsp import INCH_TO_CM, source_dir_to_unreal, source_to_unreal
from elysium_pipeline.importers import map_visibility

RECIPE_VERSION = 1
SHADOW_MASK = 0x4191
SURF_SKY = 4


def _vector(values):
    return dict(zip(("x", "y", "z"), map(float, values)))


def worldlight_record(raw):
    """Mod_LoadWorldlights fixups, then coefficients expressed in native centimetres."""
    kind = int(raw["type"])
    constant, linear, quadratic = (float(raw[key]) for key in
                                   ("constantAttn", "linearAttn", "quadraticAttn"))
    if kind in (1, 2) and constant == linear == quadratic == 0:
        quadratic = 1.0
    exponent = float(raw["exponent"])
    if kind == 2 and exponent == 0:
        exponent = 1.0
    radius = float(raw["radius"]["source"])
    return {
        "position": _vector(source_to_unreal(*raw["origin"]["source"])),
        "normal": _vector(source_dir_to_unreal(*raw["normal"]["source"])),
        "intensity": _vector(raw["intensity"]), "type": kind,
        "cluster": int(raw["cluster"]), "style": int(raw["style"]),
        "radius": radius * INCH_TO_CM if radius >= 1 else 0,
        "constant": constant, "linear": linear / INCH_TO_CM,
        "quadratic": quadratic / INCH_TO_CM ** 2,
        "stopDot": float(raw["stopdot"]), "stopDot2": float(raw["stopdot2"]),
        "exponent": exponent, "quakeDistance": linear * INCH_TO_CM,
    }


def _sky_side_triangles(points, normal, distance):
    # A convex side is its coplanar hull vertices sorted around their centroid. This reconstructs
    # only the named SURF_SKY boundary; all blocking volume geometry stays in the hull producer.
    unique = {tuple(np.round(p, 4)): p for p in points
              if abs(float(np.dot(normal, p)) - distance) <= 0.06}
    vertices = list(unique.values())
    if len(vertices) < 3:
        return []
    centre = np.mean(vertices, axis=0)
    axis = np.array((1, 0, 0) if abs(normal[0]) < 0.9 else (0, 1, 0))
    u = np.cross(normal, axis)
    v = np.cross(normal, u)
    vertices.sort(key=lambda p: math.atan2(float(np.dot(p-centre, v)), float(np.dot(p-centre, u))))
    converted = [source_to_unreal(*p) for p in vertices]
    return [[float(c) for i in (0, k+1, k) for c in converted[i]] for k in range(1, len(vertices)-1)]


def stage_for_join(join, root: Path | None = None):
    units = join.units
    visibility = map_visibility.read_visibility(units.name, root)
    if visibility is None or visibility.num_clusters <= 0:
        raise ValueError(f"{units.name}: gameplay light query requires the V2 visibility unit")
    pvs = bytearray()
    width = (visibility.num_clusters + 7) // 8
    for cluster in range(visibility.num_clusters):
        row = visibility.row(cluster)
        # CM_ClusterPVS uses an all-visible row when the authored offset is -1.
        if row is None:
            row = bytes([255]) * width
        if len(row) != width:
            raise ValueError(f"{units.name}: invalid PVS row {cluster}")
        pvs.extend(row)
    bsp = units.root["bsp"]
    planes = sidecars.source_planes(units.root["planes"])
    nodes = []
    for raw in bsp["nodes"]:
        plane = planes[int(raw["plane"])]
        nodes.append({"normal": _vector(source_dir_to_unreal(*plane[:3])),
                      "distance": float(plane[3]) * INCH_TO_CM,
                      "front": int(raw["children"][0]), "back": int(raw["children"][1])})
    hulls, sky = [], []
    world = units.root["models"][0]
    brush_indices = sidecars.model_brushes(bsp["nodes"], bsp["leafs"],
                                          bsp["leafBrushes"]["values"], int(world["headNode"]))
    collision = units.root["collision"]
    for index in sorted(brush_indices):
        brush = collision["brushes"][index]
        if not int(brush["contents"]) & SHADOW_MASK:
            continue
        first = int(brush["firstSide"])
        sides = collision["brushSides"][first:first+int(brush["numSides"])]
        _, points = sidecars.brush_hull(planes, sides, int(brush["contents"]))
        if points is None:
            raise ValueError(f"{units.name}: shadow brush {index} has no convex hull")
        hulls.append(sidecars.hull_vertices(points))
        for side in sides:
            texinfo = int(side["texInfo"])
            if texinfo < 0 or not int(units.root["texinfos"][texinfo]["flags"]) & SURF_SKY:
                continue
            plane = planes[int(side["plane"])]
            sky.extend(_sky_side_triangles(points, plane[:3], float(plane[3])))
    first = int(world["firstFace"])
    displacements = sidecars.displacement_triangles(units, range(first, first+int(world["numFaces"])))
    result = {
        "map": units.name, "recipeVersion": RECIPE_VERSION,
        "records": {"lights": [worldlight_record(r) for r in units.lighting["worldLights"]],
                    "nodes": nodes, "leafClusters": [int(r["cluster"]) for r in bsp["leafs"]],
                    "headNode": int(world["headNode"]), "numClusters": visibility.num_clusters,
                    "pvs": list(pvs)},
        "hulls": hulls, "sky": sky, "displacements": displacements,
    }
    result["sha256"] = hashlib.sha256(json.dumps(result, separators=(",", ":"), sort_keys=True).encode()).hexdigest()
    return result
