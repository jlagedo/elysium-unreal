"""Read-only item ground-model enumeration shared by source-corpus consumers.

Native item joins are owned by the V2 catalogues. This module writes no legacy manifest.
"""
from pathlib import PurePosixPath

from elysium_pipeline.formats import install, kv

ITEM_DIR = "vdata/items/"


def normalize_model(model):
    key = model.strip().replace("\\", "/").lower()
    return key if not key or key.endswith(".mdl") else key + ".mdl"


def ground_models(index):
    """Map every declared playermodel to its sorted item classnames, including absent sources."""
    by_model = {}
    for key in sorted(index):
        if not key.startswith(ITEM_DIR) or not key.endswith(".txt"):
            continue
        raw = install.read(index, key)
        if raw is None:
            continue
        data = kv.parse(raw.decode("ascii", "replace"))
        block = data.get("weapondata")
        if not isinstance(block, dict):
            block = data
        raw_model = block.get("playermodel", "")
        if not isinstance(raw_model, str):
            continue
        model = normalize_model(raw_model)
        if model:
            classname = str(PurePosixPath(key[len(ITEM_DIR):]).with_suffix(""))
            by_model.setdefault(model, []).append(classname)
    return {model: sorted(names) for model, names in sorted(by_model.items())}
