"""Read-only staged UV precision inventory; no clamping or expected-data changes."""
import numpy as np

from elysium_pipeline.validation.skeletal_diff import records, sections
from elysium_pipeline.validation.native_geometry import finite_array


def uv_precision_inventory(payload):
    fields = {key: value for key, value, _ in records("MESH", sections(payload)["MESH"])}
    uv = finite_array(fields["uv"], "staged UV vertex/component")
    limit = float(np.finfo(np.float16).max)
    outside = np.abs(uv) > limit
    with np.errstate(over="ignore", invalid="ignore"):
        nonfinite_half = ~np.isfinite(uv.astype(np.float16))
    examples = [{"sourceVertex": int(vertex), "component": "UV"[component], "value": float(uv[vertex, component])}
                for vertex, component in np.argwhere(outside)[:8]]
    return {"vertices": len(uv), "components": int(uv.size), "sourceFinite": True,
            "minimum": float(uv.min()) if uv.size else None, "maximum": float(uv.max()) if uv.size else None,
            "finiteHalfMaximum": limit, "outsideFiniteHalfRangeComponents": int(outside.sum()),
            "roundedHalfNonfiniteComponents": int(nonfinite_half.sum()),
            "affectedVertices": int(np.any(outside, axis=1).sum()), "examples": examples}
