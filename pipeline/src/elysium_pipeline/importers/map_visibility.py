"""The map visibility sub-unit's PVS, read for the water stage (R7.4).

`<map>.visibility.glb` has been exported since R2 and consumed by nothing (AUDIT G23). One thing in
it is worth reading: the potentially-visible set. VtMB annotates every leaf that can see water with
`CONTENTS_TESTFOGVOLUME` (`0x200`) and gates
`ViewDrawScene_EyeAboveWater`/`_EyeUnderWater`/`_WaterDX7` against `_NoWater` on it, i.e. "is there
any water worth testing from here" (AUDIT G9). That bit is **derived**, not authored: it is exactly
`union(PVS(cluster) for cluster in the water leaves' clusters)`, measured here on the two maps that
still carry it -- `sm_hub_1` 97 leaves and `sp_soc_3` 126 leaves, set-equal with zero discrepancy.

Deriving it rather than reading it is what makes `sm_pier_1` work: its Unofficial-Patch recompile
lost the annotation entirely (retail carries the stock-Source `0x100` spelling on 702 leaves, the UP
build carries `0x200` on 0), and the owner's decision 1 keeps the port on the UP build. The PVS
survived both recompiles, so the derivation answers on every map and the leaf bit answers on some.

The reader is deliberately thin: one row per cluster is already published (`clusters[].leaves`, and
`clusters[].pvs` naming a BIN accessor holding the decompressed `rowByteLength` bitset), so there is
no run-length decode here and no second implementation of one -- `formats.map_visibility_glb.decode`
did that at export time and this walks its rows.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

from elysium_pipeline.exporters import UE_map_sidecars as sidecars
from elysium_pipeline.formats.unit_contract import read_glb

#: The glTF extension the visibility sub-unit publishes under (`formats.map_visibility_glb.model`).
VISIBILITY_EXTENSION = "ELYSIUM_vtmb_map_visibility"


class MapVisibilityError(ValueError):
    """The visibility sub-unit is not the document this reader accounts for."""


@dataclass(frozen=True)
class MapVisibility:
    """One map's cluster table, with its PVS rows reachable.

    `units` is a `sidecars.MapUnits` over the visibility document -- the sub-unit has no `root`
    extension of its own shape, but the accessor decode is the same one every other unit reads
    through, and stating it twice is how two implementations of one buffer walk start.
    """

    name: str
    num_clusters: int
    clusters: list[dict[str, Any]]
    units: sidecars.MapUnits

    def row(self, cluster: int) -> bytes | None:
        """One cluster's decompressed PVS bitset, or `None` where the source stated none.

        A `null` accessor is the export's own "this cluster's `byteofs` entry named no row"; it is
        not an empty set, and callers must not read it as "nothing is visible from here".
        """

        if not 0 <= cluster < len(self.clusters):
            return None
        entry = (self.clusters[cluster].get("pvs") or {}).get("accessor")
        if entry is None:
            return None
        return self.units.accessor(int(entry)).tobytes()

    def pvs_union(self, clusters: Iterable[int]) -> set[int]:
        """`union(PVS(c) for c in clusters)` as cluster indices, the seed clusters included.

        A cluster is visible from itself and Source's rows say so, but a row the export declined to
        state would silently drop its own seed; adding the seeds back keeps "the water's own leaves
        are near water" true whatever the source said.
        """

        seen: set[int] = set()
        for cluster in clusters:
            if not 0 <= int(cluster) < self.num_clusters:
                continue
            seen.add(int(cluster))
            row = self.row(int(cluster))
            if row is None:
                continue
            for byte_index, byte in enumerate(row):
                if not byte:
                    continue
                for bit in range(8):
                    if byte & (1 << bit):
                        index = byte_index * 8 + bit
                        if index < self.num_clusters:
                            seen.add(index)
        return seen


def unit_path(map_name: str, root: Path | None = None) -> Path:
    return sidecars.unit_paths(map_name, root)["visibility"]


def read_visibility(map_name: str, root: Path | None = None) -> MapVisibility | None:
    """One map's visibility sub-unit, or `None` when the export does not carry one.

    `None` rather than an error: the sub-unit is a per-map export product like the lighting one, and
    a map exported before it existed still stages every other product. What reads the PVS says what
    it could not derive, in its own row.
    """

    path = unit_path(map_name, root)
    if not path.is_file():
        return None
    document, binary = read_glb(path)
    block = (document.get("extensions") or {}).get(VISIBILITY_EXTENSION)
    if not isinstance(block, dict):
        raise MapVisibilityError(f"{path} carries no {VISIBILITY_EXTENSION!r} extension")
    clusters = list(block.get("clusters") or [])
    return MapVisibility(
        name=map_name,
        num_clusters=int(block.get("numClusters") or len(clusters)),
        clusters=clusters,
        units=sidecars.MapUnits(
            name=map_name, root=block, entities={}, lighting={},
            document=document, binary=binary),
    )
