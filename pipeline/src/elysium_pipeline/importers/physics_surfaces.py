"""Deploy the exact whole physics table; no flattening, re-encoding, or file-order folding."""
from pathlib import Path
import json

from elysium_pipeline.exporters.physics_surfaces_glb import EXTENSION, FAMILY, SOURCE
from elysium_pipeline.importers import corpus_deploy


def target_of(path: str) -> tuple[str, ...]:
    if path != SOURCE:
        raise corpus_deploy.CorpusImportError(f"unexpected physics surface member {path!r}")
    return ("physics/surfaceproperties.txt",)


def semantic_product(root):
    # Independently decoded named values for verification; game input remains the raw capsule.
    return {"physics/surfaceproperties.semantic.json":
            (json.dumps(root["semantics"], ensure_ascii=True, allow_nan=False) + "\n").encode("utf-8")}


LANE_SPEC = corpus_deploy.Lane(
    name="physics-surfaces", recipe_version="elysium-physics-surfaces-v2",
    families=((FAMILY, EXTENSION),), target_of=target_of,
    owned_directories=("physics",),
    empty_hint="run `uv run elysium export_v2 physics-surfaces-glb` first",
    derived_of=semantic_product,
)


def import_physics_surfaces(export_v2_root: Path, destination_root: Path):
    return corpus_deploy.deploy_lane(export_v2_root, destination_root, LANE_SPEC)
