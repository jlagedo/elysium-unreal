"""Does the ported producer place the decals and the rain cover the BSP decoder placed?

0018 story 21-4 retires `<map>.decals` and `<map>.weather.json`, the last two decoder-only
products the map bake still read, by porting their derivations into `UE_map_sidecars`. The story's
rule is that the port MATCHES the decoder and every place it cannot is named rather than absorbed
-- and the comparison can only be run while the decoder still exists, which 21-5 ends.

This is that comparison. For every map with a legacy export directory it formats the ported rows
through `UE_map_sidecars.decal_line` -- the same 15-token writer the legacy sidecar used -- and
diffs them against `<map>.decals` line for line, reporting the first mismatching token. For
`sm_hub_1` it also compares the rain cover: the triangle count, the world AABB the height map is
rasterised over, and the encoded R16 bytes.

One structural difference is expected and is what this probe exists to size: a displacement face
carries no published vertex span (its geometry is stated by its own displacement mesh), so it is
absent from the port's projector index while the decoder, reading VERTEXES/EDGES, could bind a
decal to sculpted terrain. `dispFacesSkipped` counts the faces at risk per map; a map whose lines
all match proves no decal chose one.

    uv run elysium research decal_weather_parity
    uv run elysium research decal_weather_parity --maps sp_tutorial_1 sm_hub_1
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any, Sequence

from elysium_pipeline import paths
from elysium_pipeline.exporters import UE_map_sidecars as producer


def _legacy_lines(export_root: Path, map_name: str) -> list[str] | None:
    path = export_root / map_name / f"{map_name}.decals"
    if not path.is_file():
        return None
    return path.read_text(encoding="utf-8", errors="replace").splitlines()


def _first_difference(legacy: str, ported: str) -> dict[str, Any] | None:
    left, right = legacy.split(), ported.split()
    if len(left) != len(right):
        return {"reason": "token count", "legacy": len(left), "ported": len(right)}
    for index, (one, other) in enumerate(zip(left, right)):
        if one != other:
            return {"reason": "token", "token": index, "legacy": one, "ported": other}
    return None


def compare_decals(map_name: str, export_root: Path) -> dict[str, Any]:
    """One map's ported decal rows against its legacy `.decals`, line for line."""

    join = producer.prepare_join(map_name)
    result = producer.decal_rows(join)
    ported = [producer.decal_line(row) for row in result["rows"]]
    legacy = _legacy_lines(export_root, map_name)

    row: dict[str, Any] = {
        "map": map_name,
        "placed": result["placed"],
        # Split deliberately: `unresolved` is a material the units cannot size, which the decoder
        # skipped too; `unbound` is a decal that found no projector face, which is the only way
        # this port loses a row the decoder placed.
        "unresolved": result["unresolved"],
        "unbound": result["unbound"],
        "materials": result["materials"],
        "projectorFaces": result["projectorFaces"],
        "dispFacesSkipped": result["dispFacesSkipped"],
        "legacyLines": None if legacy is None else len(legacy),
    }
    if legacy is None:
        # The decoder writes no file when it placed nothing, so "no sidecar" and "no rows" agree.
        row["verdict"] = "identical" if not ported else "no legacy sidecar to compare with"
        return row
    if len(legacy) != len(ported):
        row["verdict"] = "line count differs"
        row["differences"] = [{"reason": "line count"}]
        return row
    differences = [
        {"line": index, **difference}
        for index, (one, other) in enumerate(zip(legacy, ported))
        for difference in (_first_difference(one, other),)
        if difference is not None
    ]
    row["identical"] = len(ported) - len(differences)
    row["differences"] = differences[:16]
    row["differing"] = len(differences)
    row["verdict"] = "identical" if not differences else "differs"
    return row


def compare_weather(map_name: str, export_root: Path) -> dict[str, Any] | None:
    """`sm_hub_1`'s rain cover: the triangle set, the bounds and the encoded height map."""

    legacy_path = export_root / map_name / f"{map_name}.weather.json"
    if not legacy_path.is_file():
        return None
    legacy = json.loads(legacy_path.read_text(encoding="utf-8"))

    from elysium_pipeline.importers import map_weather

    staged = map_weather.stage_map(map_name)
    if staged is None:
        return {"map": map_name, "verdict": "the port states no weather for this map"}

    legacy_bounds = legacy.get("world_bounds_cm") or {}
    ported_bounds = staged["document"]["world_bounds_cm"]
    legacy_height = legacy.get("height_texture") or {}
    ported_height = staged["document"]["height_texture"]
    legacy_png = (export_root / map_name / legacy_height.get("path", "")).read_bytes() \
        if legacy_height.get("path") else b""
    ported_png = map_weather.height_png(map_name)

    row = {
        "map": map_name,
        "coverTriangles": {
            "legacy": legacy_height.get("triangle_count"),
            "ported": ported_height["triangle_count"],
        },
        "boundsMin": {"legacy": legacy_bounds.get("min"), "ported": ported_bounds["min"]},
        "boundsMax": {"legacy": legacy_bounds.get("max"), "ported": ported_bounds["max"]},
        "coveredTexels": {
            "legacy": legacy_height.get("covered_texels"),
            "ported": ported_height["covered_texels"],
        },
        "heightBytes": {"legacy": len(legacy_png), "ported": len(ported_png)},
        "heightIdentical": legacy_png == ported_png,
    }
    # The R16 bytes are NOT the test, and `map_weather`'s docstring says why: a quarter of the
    # cover triangles are near edge-on from above, so the binary32 vertex recovery moves their
    # interpolated height by decimetres at a fraction of a percent of texels. What must reproduce
    # is the cover set, the coverage and the bounds -- the last within the 0.01 cm the bake's own
    # footprint pin allows.
    bounds_gap = max(
        (abs(float(one) - float(other))
         for side in ("boundsMin", "boundsMax")
         for one, other in zip(row[side]["legacy"] or (), row[side]["ported"] or ())),
        default=float("inf"),
    )
    row["boundsGapCm"] = bounds_gap
    same = (
        row["coverTriangles"]["legacy"] == row["coverTriangles"]["ported"]
        and row["coveredTexels"]["legacy"] == row["coveredTexels"]["ported"]
        and bounds_gap <= 0.01
    )
    row["verdict"] = "identical" if same else "differs"
    return row


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="uv run elysium research decal_weather_parity",
        description=__doc__,
    )
    parser.add_argument("--maps", nargs="*", default=None,
                        help="map stems (default: every map with a legacy export directory)")
    parser.add_argument("--json", type=Path, default=None, help="write the full report here")
    parser.add_argument("--weather", action="store_true",
                        help="also compare the rain cover (sm_hub_1 is the only map with one)")
    arguments = parser.parse_args(list(argv) if argv is not None else None)

    export_root = paths.export_root()
    if arguments.maps:
        names = list(dict.fromkeys(arguments.maps))
    else:
        names = sorted(
            path.name for path in export_root.iterdir()
            if path.is_dir() and (path / f"{path.name}.decals").is_file()
        )

    rows: list[dict[str, Any]] = []
    weather: list[dict[str, Any]] = []
    for name in names:
        try:
            row = compare_decals(name, export_root)
        except Exception as exc:                                  # noqa: BLE001 - a survey
            row = {"map": name, "verdict": "failed", "error": f"{type(exc).__name__}: {exc}"}
        rows.append(row)
        print(
            "%-24s %-22s placed=%-5s legacy=%-5s identical=%-5s unresolved=%-4s unbound=%-4s "
            "disp=%s"
            % (name, row.get("verdict", ""), row.get("placed", "-"),
               row.get("legacyLines", "-"), row.get("identical", "-"),
               row.get("unresolved", "-"), row.get("unbound", "-"),
               row.get("dispFacesSkipped", "-"))
        )
        for difference in row.get("differences") or ():
            print("    %s" % json.dumps(difference, separators=(",", ":")))
        if arguments.weather:
            found = compare_weather(name, export_root)
            if found is not None:
                weather.append(found)
                print("  weather: " + json.dumps(found, separators=(",", ":")))

    verdicts: dict[str, int] = {}
    for row in rows:
        verdicts[row.get("verdict", "?")] = verdicts.get(row.get("verdict", "?"), 0) + 1
    print("\n%d map(s): %s" % (len(rows), json.dumps(verdicts, sort_keys=True)))
    print("decal lines compared: %d" % sum(row.get("legacyLines") or 0 for row in rows))
    print("displacement faces skipped, over the whole set: %d"
          % sum(row.get("dispFacesSkipped") or 0 for row in rows))

    if arguments.json:
        arguments.json.parent.mkdir(parents=True, exist_ok=True)
        arguments.json.write_text(
            json.dumps({"decals": rows, "weather": weather}, indent=1), encoding="utf-8")
        print("wrote %s" % arguments.json)
    return 0 if all(row.get("verdict") == "identical" for row in rows) else 1


if __name__ == "__main__":                                        # pragma: no cover
    raise SystemExit(main())
