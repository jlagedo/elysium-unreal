# -*- coding: utf-8 -*-
"""0018 story 21-7: snapshot every map's producer sidecars, so a reading change can be diffed.

`write_sidecars` is the producer's whole output for one map. Story 21-7 replaces the text-and-regex
reading of the entity lump with a structural one, and the claim it has to make is that nothing else
moved: run this before the change, run it after, diff the two trees, and every difference must be
one the measurement (`entity_reading_delta`) named.

Read-only with respect to the repository; writes only under the given root, which belongs under
`$ELYSIUM_WORK_ROOT/research`.

Usage::

    uv run elysium research sidecar_snapshot --out <external-path>/before
    uv run elysium research sidecar_snapshot --out <external-path>/after
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

from elysium_pipeline.exporters import UE_map_sidecars as producer
from elysium_pipeline.paths import export_v2_root


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(prog="sidecar_snapshot", description=__doc__)
    parser.add_argument("--out", type=Path, required=True, help="the snapshot root to write")
    parser.add_argument("--maps", nargs="*", default=None, help="map stems; default every unit")
    arguments = parser.parse_args(argv)

    root = export_v2_root()
    names = arguments.maps or sorted(
        path.name.split(".")[0] for path in (root / "maps").glob("*.entities.glb"))

    out = arguments.out
    out.mkdir(parents=True, exist_ok=True)
    refused: dict[str, str] = {}
    written = 0
    for name in names:
        try:
            producer.write_sidecars(name, out_dir=out / name)
        except Exception as error:                       # noqa: BLE001 - recorded, not raised
            refused[name] = f"{type(error).__name__}: {error}"
            print(f"  refused {name}: {type(error).__name__}")
            continue
        written += 1
    (out / "_refused.json").write_text(json.dumps(refused, indent=2, sort_keys=True),
                                       encoding="utf-8")
    print(f"{written} map(s) written, {len(refused)} refused -> {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
