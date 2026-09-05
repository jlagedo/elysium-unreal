"""Compare native cloth build counters with the frozen legacy build log."""
import re

FIELDS = ("sim_vertices", "sim_faces", "kinematic_vertices", "collision_bodies", "config_properties",
          "built_sim_vertices", "built_kinematic_vertices", "built_tethers", "driven_vertices",
          "skinned_vertices", "orphaned_bindings", "root_bound_particles", "degenerate_sim_normals")
HEADER = re.compile(r"\[make_cloth_assets\] (\S+) -> (\S+) \[([^\]]*)\] "
                    r"\((\d+) sim vertices, (\d+) faces, (\d+) pinned, (\d+) collision bodies, (\d+) config properties\)")
BUILT = re.compile(r"\[make_cloth_assets\] (\S+)\s+built: (\d+) sim vertices, (\d+) kinematic, (\d+) tethers")
RENDER = re.compile(r"\[make_cloth_assets\] (\S+)\s+render: (\d+) driven, (\d+) skinned, (\d+) orphaned, "
                    r"(\d+) root-bound particles, (\d+) particles with no normal")


def read_legacy_counts(text):
    rows = {}
    for line in text.splitlines():
        if match := HEADER.search(line):
            stem, path, material, *values = match.groups()
            row = {"assetPath": path, "material": material, **dict(zip(FIELDS[:5], map(int, values)))}
            rows.setdefault(stem, []).append(row)
        else:
            match = BUILT.search(line) or RENDER.search(line)
            if match:
                stem, *values = match.groups()
                if stem not in rows:
                    raise ValueError("cloth counters precede their garment header: " + stem)
                fields = FIELDS[5:8] if len(values) == 3 else FIELDS[8:]
                rows[stem][-1].update(zip(fields, map(int, values)))
    if not rows or any(set(FIELDS) - row.keys() for group in rows.values() for row in group):
        raise ValueError("legacy cloth log has incomplete build counters")
    return rows


def compare_counts(legacy, native, model_for_stem):
    result = {"compared": 0, "differences": [], "missing": [], "additional": []}
    expected = set()
    for stem, rows in legacy.items():
        id = model_for_stem[stem]
        for index, before in enumerate(rows):
            expected.add((id, index))
            after = native.get(id, [])
            if index >= len(after):
                result["missing"].append({"assetId": id, "garment": index})
                continue
            result["compared"] += 1
            for field in (*FIELDS, "material"):
                if before[field] != after[index].get(field):
                    result["differences"].append({"assetId": id, "garment": index, "field": field,
                                                   "before": before[field], "after": after[index].get(field)})
    for id, rows in native.items():
        for index in range(len(rows)):
            if (id, index) not in expected:
                result["additional"].append({"assetId": id, "garment": index})
    return result
