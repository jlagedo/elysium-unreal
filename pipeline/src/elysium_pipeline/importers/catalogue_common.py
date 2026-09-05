"""Read-only, pure-data support for R8 global catalogues. No publication side effects."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path

from elysium_pipeline.asset_paths import baked_unit, corpus_path


def evidence(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False)


def object_path(package):
    if not package:
        return ""
    if "." in package.rsplit("/", 1)[-1]:
        return package
    return package + "." + package.rsplit("/", 1)[-1]


def native_ref(asset_id, prefix, actual, *, label=None):
    """An expected address is not evidence that a producer declared the product."""
    expected = object_path(baked_unit(asset_id, prefix, label=label))
    if object_path(actual) != expected:
        raise ValueError(f"{asset_id}: {prefix} producer address {actual!r} != {expected}")
    return expected


def catalogue(kind, data):
    return {"schemaVersion": "1.0.0", "catalogueKind": kind,
            "assetPath": corpus_path("model", "DA", kind), "data": data}


def unique(rows, field):
    result, folded = {}, set()
    for row in rows:
        key = row[field]
        if not key or key.casefold() in folded:
            raise ValueError(f"duplicate or empty {field}: {key!r}")
        folded.add(key.casefold())
        result[key] = row
    return result


def require_coverage(actual, expected):
    actual, expected = set(actual), set(expected)
    if actual != expected:
        raise ValueError(f"catalogue model coverage differs: missing={sorted(expected - actual)}, extra={sorted(actual - expected)}")


def staged_json(root, entry, field="body"):
    root = Path(root).resolve()
    path = (root / entry[field]).resolve()
    if not path.is_relative_to(root):
        raise ValueError(f"stage path escapes its root: {path}")
    raw = path.read_bytes()
    if hashlib.sha256(raw).hexdigest() != entry["recipe"][field + "Sha256"]:
        raise ValueError(f"stale staged {field}: {entry['assetId']}")
    value = json.loads(raw)
    if value["assetId"] != entry["assetId"]:
        raise ValueError(f"staged {field} identity mismatch: {entry['assetId']}")
    return value


def vector(values):
    if len(values) != 3:
        raise ValueError("expected three vector components")
    return dict(zip(("x", "y", "z"), values))


def quaternion(values):
    if len(values) != 4:
        raise ValueError("expected four quaternion components")
    return dict(zip(("x", "y", "z", "w"), values))
