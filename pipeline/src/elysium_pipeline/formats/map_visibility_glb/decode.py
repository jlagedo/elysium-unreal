"""Lump 4 decompressed into bitsets, and lumps 22-25 carried with their meaning left open.

Lump 4 opens with `int numclusters` and `int byteofs[numclusters][2]` -- `[0]` the PVS row, `[1]`
the PAS row, both relative to the lump's first byte -- followed by the run-length coded rows: a
non-zero byte is copied, a `0x00` byte is followed by a count of zero bytes to emit, and the row
ends once `ceil(numclusters / 8)` bytes have been produced.

The decompressed row is the product (one `UNSIGNED_BYTE` accessor, or none at all where the row
decoded to no byte); the compressed span is `derived` in the ledger either way, because its
decoded content is what the accessor holds and its extent is what was read. Two clusters
whose `byteofs` coincide name one row: it is recorded on the lowest-indexed cluster and the other
entry carries `sharedWith`, so a shared row is one accessor and one ledger claim rather than two.

The portal graph (lumps 22-25, five maps) is carried rather than read: the record layouts are not
established against these maps' bytes, so each lump becomes one `typedUnidentified` entry stating
its extent, its digest, the candidate struct and whether the length divides by that struct's size,
and the ledger claims the whole span `mapped` under the lump's owner name.
"""

from __future__ import annotations

import hashlib
import struct
from typing import Any

from elysium_pipeline.formats.map_visibility_glb import model as vis_model
from elysium_pipeline.formats.map_visibility_glb.payload import BitsetPayload
from elysium_pipeline.formats.map_visibility_glb.source import MapVisibilitySourceClosure
from elysium_pipeline.formats.unit_contract import dependency


class MapVisibilityDecodeError(ValueError):
    """The closure's spans are not the ones its own partition proof names.

    Every departure the *bytes* can produce is carried -- an anomaly, an omission or a
    `typedUnidentified` row -- so the only thing this decode refuses is a closure that
    contradicts itself: a member whose data is not the extent its lump directory row declares.
    There is then no offset the published records could be relative to.
    """


def decompress_row(vis: bytes, offset: int, row_bytes: int) -> tuple[bytes, int, bool, bool]:
    """One run-length coded row -> (bitset, compressed length, ran-past-the-lump, overshot).

    `ran-past-the-lump` is set when the reader needed a byte the lump does not hold; `overshot`
    is set when the final zero run emitted past `row_bytes`, in which case the row is truncated to
    the declared length. Neither happens on any of the 108 retail maps.
    """

    out = bytearray()
    reader = int(offset)
    limit = len(vis)
    exhausted = False
    while len(out) < row_bytes:
        if reader >= limit:
            exhausted = True
            break
        byte = vis[reader]
        reader += 1
        if byte:
            out.append(byte)
            continue
        if reader >= limit:
            exhausted = True
            break
        out.extend(b"\0" * vis[reader])
        reader += 1
    overshot = len(out) > row_bytes
    if overshot:
        del out[row_bytes:]
    return bytes(out), min(reader, limit) - int(offset), exhausted, overshot


class _SpanClaims:
    """Claims over one member, refusing to pay for a byte twice.

    Compressed rows are addressed by an offset table rather than laid end to end, so two rows may
    in principle name overlapping extents. The overlap is reported rather than claimed again: the
    first owner keeps the bytes, the second records a `row-span-overlap` anomaly, and the ledger
    still partitions the span.
    """

    __slots__ = ("length", "claimed", "rows", "overlaps")

    def __init__(self, length: int) -> None:
        self.length = int(length)
        self.claimed = bytearray(self.length)
        self.rows: list[tuple[int, int, str, str]] = []
        self.overlaps: list[tuple[int, int]] = []

    def claim(self, offset: int, length: int, state: str, owner: str) -> None:
        start = max(0, int(offset))
        end = min(self.length, start + max(0, int(length)))
        cursor = start
        while cursor < end:
            if self.claimed[cursor]:
                stop = cursor
                while stop < end and self.claimed[stop]:
                    stop += 1
                self.overlaps.append((cursor, stop - cursor))
                cursor = stop
                continue
            stop = cursor
            while stop < end and not self.claimed[stop]:
                stop += 1
            self.claimed[cursor:stop] = b"\1" * (stop - cursor)
            self.rows.append((cursor, stop - cursor, state, owner))
            cursor = stop

    def unclaimed(self) -> list[tuple[int, int]]:
        runs: list[tuple[int, int]] = []
        cursor = 0
        while cursor < self.length:
            if self.claimed[cursor]:
                cursor += 1
                continue
            stop = cursor
            while stop < self.length and not self.claimed[stop]:
                stop += 1
            runs.append((cursor, stop - cursor))
            cursor = stop
        return runs


def _leaf_clusters(leaf_bytes: bytes) -> list[int]:
    """The `cluster` short of every 32-byte `dleaf_t` in the root's lump 10."""

    count = len(leaf_bytes) // vis_model.LEAF_RECORD_BYTES
    return [
        struct.unpack_from(
            "<h", leaf_bytes, index * vis_model.LEAF_RECORD_BYTES + vis_model.LEAF_CLUSTER_OFFSET
        )[0]
        for index in range(count)
    ]


def _map_block(
    closure: MapVisibilitySourceClosure, leaf_bytes: bytes, anomalies: list[dict[str, Any]]
) -> dict[str, Any]:
    part = closure.partition
    lumps = []
    for index in vis_model.OWNED_LUMPS:
        span = part.lump(index)
        row: dict[str, Any] = {
            "lump": index,
            "name": vis_model.lump_name(index),
            "present": bool(span.populated),
            # The directory row's own value, published the way the map root's partition
            # publishes it, so the two units never state one lump's position two ways;
            # `present` and `length` are what say the lump holds nothing.
            "offset": span.offset,
            "length": span.length,
            # Where the directory row that named the lump sits, spelled the way the map root's
            # own partition and the sibling map sub-units spell it.
            "sourceOffset": span.row_offset,
        }
        # Lump 4 selects the unit, so it is a member even when the map stores none of it; a
        # portal lump becomes a member only when the directory gives it bytes.
        if span.populated or index == vis_model.VISIBILITY_LUMP:
            row["member"] = vis_model.member_path(closure.key, index)
        lumps.append(row)
    leaf_span = part.lump(vis_model.LEAF_LUMP)
    remainder = len(leaf_bytes) % vis_model.LEAF_RECORD_BYTES
    if remainder:
        anomalies.append(
            {
                "role": "leaf-table-not-divisible",
                "lump": vis_model.LEAF_LUMP,
                "byteLength": len(leaf_bytes),
                "recordBytes": vis_model.LEAF_RECORD_BYTES,
                "remainder": remainder,
                "reason": f"leaf-table-not-divisible: the root's leaf table of "
                          f"{len(leaf_bytes)} bytes does not divide by the "
                          f"{vis_model.LEAF_RECORD_BYTES}-byte dleaf_t, so its trailing "
                          f"{remainder} byte(s) name no leaf this unit can derive from",
            }
        )
    return {
        "stem": closure.key,
        "mapRevision": part.map_revision,
        "lumps": lumps,
        # The bytes the `derived` leaf lists were read from belong to the root; naming their
        # extent and digest is what lets a reader tell which leaf table produced those lists.
        "leafTable": {
            "lump": vis_model.LEAF_LUMP,
            "offset": leaf_span.offset if leaf_span.populated else 0,
            "byteLength": len(leaf_bytes),
            "recordBytes": vis_model.LEAF_RECORD_BYTES,
            "count": len(leaf_bytes) // vis_model.LEAF_RECORD_BYTES,
            "sha256": hashlib.sha256(leaf_bytes).hexdigest(),
        },
    }


def _portals(
    closure: MapVisibilitySourceClosure,
    claims: dict[str, list[tuple[int, int, str, str]]],
    anomalies: list[dict[str, Any]],
    typed_unidentified: list[dict[str, Any]],
) -> dict[str, dict[str, Any]]:
    published: dict[str, dict[str, Any]] = {}
    for lump in vis_model.PORTAL_LUMPS:
        member = closure.portal_member(lump)
        if member is None:
            continue
        key = vis_model.PORTAL_KEYS[lump]
        candidate, record_bytes = vis_model.PORTAL_CANDIDATES[lump]
        entry = vis_model.PortalLump(
            lump=lump,
            key=key,
            member=member.path,
            source_offset=member.span_offset,
            byte_length=member.byte_length,
            sha256=member.sha256,
            candidate=candidate,
            record_bytes=record_bytes,
        )
        published[key] = entry.to_json()
        typed_unidentified.append(entry.coverage_row())
        if not entry.divides:
            anomalies.append(entry.anomaly_row())
        # A portal member exists exactly when its directory row gave the lump bytes, so the
        # whole span is always one claim.
        claims[member.path] = [(0, member.byte_length, "mapped", f"portals.{key}")]
    return published


def decode_map_visibility(closure: MapVisibilitySourceClosure) -> vis_model.MapVisibilityModel:
    """Decode one map's visibility spans into the model the exporter publishes."""

    for member in closure.members():
        lump = int(str(member.path).rsplit("#lump", 1)[-1])
        declared = closure.partition.lump(lump)
        if member.span_offset != declared.offset or member.byte_length != declared.length:
            raise MapVisibilityDecodeError(
                f"{member.path}: the member holds {member.byte_length} bytes at "
                f"{member.span_offset}, and its directory row names "
                f"{declared.length} at {declared.offset}"
            )

    vis = closure.visibility.data
    anomalies: list[dict[str, Any]] = []
    omissions: list[dict[str, Any]] = []
    typed_unidentified: list[dict[str, Any]] = []
    omitted_proven: list[dict[str, Any]] = []
    claims: dict[str, list[tuple[int, int, str, str]]] = {}

    leaf_bytes = closure.leaf_bytes()
    map_block = _map_block(closure, leaf_bytes, anomalies)
    portals = _portals(closure, claims, anomalies, typed_unidentified)

    span = _SpanClaims(len(vis))
    num_clusters = 0
    declared_clusters = 0
    if len(vis) >= vis_model.VIS_HEADER_BYTES:
        declared_clusters = struct.unpack_from("<i", vis, 0)[0]
        span.claim(0, vis_model.VIS_HEADER_BYTES, "mapped", "vis.header")
        num_clusters = max(0, declared_clusters)
        table_end = vis_model.VIS_HEADER_BYTES + num_clusters * vis_model.BYTEOFS_ENTRY_BYTES
        if declared_clusters < 0:
            anomalies.append(
                {
                    "role": "byteofs-table-truncated",
                    "declaredClusters": int(declared_clusters),
                    "clustersRead": 0,
                    "byteLength": len(vis),
                    "reason": f"byteofs-table-truncated: the lump declares "
                              f"{declared_clusters} clusters, which names no offset table "
                              f"at all",
                }
            )
        elif table_end > len(vis):
            fits = max(0, (len(vis) - vis_model.VIS_HEADER_BYTES)
                       // vis_model.BYTEOFS_ENTRY_BYTES)
            anomalies.append(
                {
                    "role": "byteofs-table-truncated",
                    "declaredClusters": declared_clusters,
                    "clustersRead": fits,
                    "byteLength": len(vis),
                    "reason": f"byteofs-table-truncated: the lump declares "
                              f"{declared_clusters} clusters, whose offset table needs "
                              f"{table_end} bytes of a {len(vis)}-byte lump",
                }
            )
            num_clusters = fits
    elif vis:
        anomalies.append(
            {
                "role": "byteofs-table-truncated",
                "declaredClusters": None,
                "clustersRead": 0,
                "byteLength": len(vis),
                "reason": f"byteofs-table-truncated: the lump holds {len(vis)} bytes, fewer "
                          f"than the {vis_model.VIS_HEADER_BYTES}-byte header",
            }
        )

    row_bytes = vis_model.row_byte_length(num_clusters)
    header_end = vis_model.VIS_HEADER_BYTES + num_clusters * vis_model.BYTEOFS_ENTRY_BYTES
    payload = BitsetPayload()

    # First pass: the offset table, then the row each entry names. Clusters are visited in index
    # order and each set in PVS-then-PAS order, so the lowest-indexed cluster records a shared row
    # and every accessor index is a function of the source alone.
    recorded: dict[int, tuple[int, str, int, bytes]] = {}      # offset -> cluster, set, accessor
    refs: list[tuple[vis_model.RowRef, vis_model.RowRef]] = []
    for index in range(num_clusters):
        entry_offset = vis_model.VIS_HEADER_BYTES + index * vis_model.BYTEOFS_ENTRY_BYTES
        span.claim(
            entry_offset, vis_model.BYTEOFS_ENTRY_BYTES, "mapped", f"vis.byteofs[{index}]"
        )
        pair: list[vis_model.RowRef] = []
        for slot, kind in enumerate(("pvs", "pas")):
            offset = struct.unpack_from("<i", vis, entry_offset + slot * 4)[0]
            if offset < header_end or offset >= len(vis):
                anomalies.append(
                    {
                        "role": "byteofs-out-of-range",
                        "cluster": index,
                        "set": kind,
                        "offset": int(offset),
                        "sourceOffset": entry_offset + slot * 4,
                        "reason": f"byteofs-out-of-range: cluster {index}'s {kind} offset names "
                                  f"byte {offset} of a {len(vis)}-byte lump whose rows start "
                                  f"at {header_end}",
                    }
                )
                pair.append(vis_model.RowRef(offset=int(offset), out_of_range=True))
                continue
            shared = recorded.get(int(offset))
            if shared is not None:
                pair.append(
                    vis_model.RowRef(
                        offset=int(offset),
                        shared_with=shared[0],
                        shared_set=shared[1],
                    )
                )
                continue
            row, compressed, exhausted, overshot = decompress_row(vis, int(offset), row_bytes)
            if exhausted or overshot:
                anomalies.append(
                    {
                        "role": f"{kind}-row-overrun",
                        "cluster": index,
                        "set": kind,
                        "offset": int(offset),
                        "compressedLength": compressed,
                        "reason": (
                            f"{kind}-row-overrun: cluster {index}'s row needed a byte past "
                            f"the {len(vis)}-byte lump"
                            if exhausted
                            else f"{kind}-row-overrun: cluster {index}'s final run emits "
                            f"past the {row_bytes}-byte row"
                        ),
                    }
                )
            if len(row) < row_bytes:
                anomalies.append(
                    {
                        "role": f"{kind}-row-short",
                        "cluster": index,
                        "set": kind,
                        "offset": int(offset),
                        "byteLength": len(row),
                        "rowByteLength": row_bytes,
                        "reason": f"{kind}-row-short: cluster {index}'s row decodes to "
                                  f"{len(row)} of {row_bytes} bytes",
                    }
                )
            before = len(span.overlaps)
            span.claim(int(offset), compressed, "derived", f"vis.{kind}[{index}]")
            for start, length in span.overlaps[before:]:
                anomalies.append(
                    {
                        "role": "row-span-overlap",
                        "cluster": index,
                        "set": kind,
                        "offset": start,
                        "byteLength": length,
                        "reason": f"row-span-overlap: another owner already pays for "
                                  f"{length} byte(s) at {start}, so cluster {index}'s {kind} "
                                  f"span is not claimed a second time",
                    }
                )
            # A row that produced no byte gets no accessor: `accessor.count` has a minimum of one
            # in glTF 2.0, so the entry states `emptyRow` and its `-row-short` anomaly says why.
            accessor = payload.add(row) if row else None
            recorded[int(offset)] = (index, kind, accessor, row)
            pair.append(
                vis_model.RowRef(
                    offset=int(offset),
                    accessor=accessor,
                    compressed_length=compressed,
                    empty_row=not row,
                )
            )
        refs.append((pair[0], pair[1]))

    # Second pass: the leaves the root's table resolves to each cluster.
    leaf_lists: list[list[int]] = [[] for _ in range(num_clusters)]
    unclustered: list[int] = []
    for leaf, cluster in enumerate(_leaf_clusters(leaf_bytes)):
        if cluster < 0:
            unclustered.append(leaf)
        elif cluster < num_clusters:
            leaf_lists[cluster].append(leaf)
        else:
            anomalies.append(
                {
                    "role": "leaf-cluster-out-of-range",
                    "leaf": leaf,
                    "cluster": int(cluster),
                    "numClusters": num_clusters,
                    "reason": f"leaf-cluster-out-of-range: leaf {leaf} names cluster "
                              f"{cluster}, and the lump declares {num_clusters}",
                }
            )

    def _row_of(ref: vis_model.RowRef) -> bytes | None:
        """The bitset an entry resolves to, or `None` where the source declined to state one.

        A count of zero would say that nothing is visible from the cluster; an entry that names
        no row, or whose row decoded to no byte at all, said nothing, so it publishes `null`.
        """

        if ref.out_of_range:
            return None
        entry = recorded.get(ref.offset)
        if entry is None or not entry[3]:
            return None
        return entry[3]

    clusters: list[vis_model.ClusterRow] = []
    for index in range(num_clusters):
        pvs, pas = refs[index]
        pvs_row, pas_row = _row_of(pvs), _row_of(pas)
        clusters.append(
            vis_model.ClusterRow(
                index=index,
                source_offset=vis_model.VIS_HEADER_BYTES
                + index * vis_model.BYTEOFS_ENTRY_BYTES,
                pvs=pvs,
                pas=pas,
                leaves=tuple(leaf_lists[index]),
                visible_count=(
                    None if pvs_row is None else vis_model.set_bits(pvs_row, num_clusters)
                ),
                audible_count=(
                    None if pas_row is None else vis_model.set_bits(pas_row, num_clusters)
                ),
            )
        )

    # Whatever no header field and no compressed row paid for. These runs contribute no payload
    # byte, so they are omitted with the evidence of their extent and digest rather than dropped.
    # Each run is stated once, in `omissions[]`; `coverage.omittedProven` carries one summary row
    # per role that points back at it rather than a second copy of every range.
    unreferenced = 0
    for ordinal, (offset, length) in enumerate(span.unclaimed()):
        chunk = vis[offset:offset + length]
        owner = f"vis.unreferenced[{ordinal}]"
        span.rows.append((offset, length, "omitted-proven", owner))
        row = {
            "role": "unreferenced-vis-bytes",
            "owner": owner,
            "sourceOffset": offset,
            "byteLength": length,
            "sha256": hashlib.sha256(chunk).hexdigest(),
            "reason": f"unreferenced-vis-bytes: no cluster's byteofs entry names the "
                      f"{length} byte(s) at {offset} and no row's compressed span reaches "
                      f"them",
        }
        omissions.append(row)
        unreferenced += length

    if unreferenced:
        omitted_proven.append(
            {
                "role": "unreferenced-vis-bytes",
                "sourcePath": closure.visibility.path,
                "runs": sum(1 for entry in omissions if entry["role"] == "unreferenced-vis-bytes"),
                "byteLength": unreferenced,
                "reason": "unreferenced-vis-bytes: each run is carried in omissions[] with its "
                          "offset, length and SHA-256",
            }
        )

    if not vis:
        row = {
            "role": "empty-member",
            "owner": closure.visibility.path,
            "sourceOffset": 0,
            "byteLength": 0,
            "sha256": hashlib.sha256(b"").hexdigest(),
            "reason": "empty-member: the map's VISIBILITY lump is unpopulated, so the "
                      "unit's selecting member holds no bytes",
        }
        omissions.append(row)
        omitted_proven.append(
            {
                "role": "empty-member",
                "sourcePath": closure.visibility.path,
                "runs": 1,
                "byteLength": 0,
                "reason": "empty-member: the selecting member is carried in omissions[] with "
                          "the SHA-256 of the empty string",
            }
        )

    claims[closure.visibility.path] = span.rows

    mapped: list[dict[str, Any]] = [
        {"field": "map", "state": "mapped"},
        {"field": "numClusters", "state": "mapped"},
        {"field": "rowByteLength", "state": "derived"},
        {"field": "clusters[].pvs", "state": "equivalent"},
        {"field": "clusters[].pas", "state": "equivalent"},
        {"field": "clusters[].leaves", "state": "derived"},
        {"field": "clusters[].visibleCount", "state": "derived"},
        {"field": "clusters[].audibleCount", "state": "derived"},
        {"field": "unclusteredLeaves", "state": "derived"},
    ]

    dependencies = [
        dependency(
            "map",
            vis_model.map_asset_id(closure.key),
            closure.source_path,
            True,
            byteLength=closure.byte_length,
            sha256=closure.sha256,
        )
    ]

    return vis_model.MapVisibilityModel(
        key=closure.key,
        asset_id=closure.asset_id,
        source_path=closure.source_path,
        members=closure.members(),
        map_block=map_block,
        num_clusters=num_clusters,
        row_byte_length=row_bytes,
        clusters=clusters,
        unclustered_leaves=unclustered,
        portals=portals,
        accessors=payload.accessors,
        buffer_views=payload.views(),
        binary=payload.binary,
        dependencies=dependencies,
        anomalies=anomalies,
        omissions=omissions,
        claims=claims,
        mapped=mapped,
        typed_unidentified=typed_unidentified,
        omitted_proven=omitted_proven,
        unresolved=[],
        unsupported=[],
    )


__all__ = ["MapVisibilityDecodeError", "decode_map_visibility", "decompress_row"]
