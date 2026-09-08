"""Decode one nav-graph's `.ain`/`.loc` pair into its complete model plus a gapless byte ledger.

The nav-graph seam's specification describes the node stream as a fixed 32 tokens per node, verified against
`sp_tutorial_1` alone. Walking the full retail corpus (100 `.ain` files) shows the per-node width
is a per-map constant that is *not* always 32 -- it ranges from 29 to 47 tokens once `NumHulls`
per-hull floats are accounted for -- while the trailing 2-token `lead` pair and the 25-token link
row stay exactly as documented on every file. This decoder derives the node width from the file
itself (`tail` is carried as whatever is left over, `lead` stays the fixed trailing pair) rather
than assuming 32; see the exporter's `specDeviations` for the corpus evidence.
"""

from __future__ import annotations

from dataclasses import replace
from typing import Any

from elysium_pipeline.formats.nav_graph_glb import coverage, lexer
from elysium_pipeline.formats.nav_graph_glb.model import (
    EXPECTED_VERSION,
    Header,
    HeaderField,
    Link,
    Node,
    NodeLabel,
    NavGraphModel,
    Stamp,
    WCLookup,
    Zones,
)
from elysium_pipeline.formats.nav_graph_glb.source import NavGraphSourceClosure, bsp_path
from elysium_pipeline.formats.unit_contract import dependency

#: Tokens per link line: `src`, `dst`, and 23 further fields.
LINK_TOKEN_WIDTH = 25

NODE_LABEL = "Nodes:"


class NavGraphDecodeError(RuntimeError):
    """The `.ain`/`.loc` pair cannot be read as a nav-graph unit."""


def _int(token: lexer.Token, owner: str) -> int:
    try:
        return int(token.text)
    except ValueError as error:
        raise NavGraphDecodeError(f"{owner}: {token.text!r} at byte {token.offset} is not an integer") from error


def _float(token: lexer.Token, owner: str) -> float:
    try:
        return float(token.text)
    except ValueError as error:
        raise NavGraphDecodeError(f"{owner}: {token.text!r} at byte {token.offset} is not a number") from error


def _scan_to(tokens: list[lexer.Token], start: int, label: str) -> int:
    """The index of the next `label` token at or after `start`, or a decode error at EOF."""

    for index in range(start, len(tokens)):
        if tokens[index].text == label:
            return index
    raise NavGraphDecodeError(f"no {label!r} token found from byte offset onward")


def _expect_label(
    tokens: list[lexer.Token],
    pos: int,
    label: str,
    anomalies: list[dict[str, Any]],
    claims: list[tuple[int, int, str, str]],
    unknown_line_counter: list[int],
) -> int:
    """`pos` when the label sits there already; otherwise scan for it, naming every skipped
    token `unknown-line` -- bytes that belong to none of the grammar's five known streams.

    Each skipped token is also claimed (`unknown-line[n]`), because a byte the grammar cannot
    place is still a byte the ledger must account for; leaving it to the whitespace catch-all
    would fail publication instead of naming the departure. `unknown_line_counter` is shared
    across every call this decode makes, so two separate scans that each skip a token never both
    claim `unknown-line[0]` -- the index is unique file-wide, not per scan.
    """

    if pos < len(tokens) and tokens[pos].text == label:
        return pos
    found = _scan_to(tokens, pos, label)
    for token in tokens[pos:found]:
        index = unknown_line_counter[0]
        unknown_line_counter[0] += 1
        anomalies.append(
            {"role": "unknown-line", "offset": token.offset, "line": token.line, "text": token.text}
        )
        claims.append((token.offset, token.length, "mapped-text", f"unknown-line[{index}]"))
    return found


def _decode_loc(
    closure: NavGraphSourceClosure,
) -> tuple[Stamp | None, list[dict[str, Any]], list[dict[str, Any]], list[dict[str, Any]], list[dict[str, Any]], list[str]]:
    """Decode the optional `.loc` companion in isolation: `(stamp, anomalies, omissions,
    typedUnidentified, byteLedgerRows, mappedNames)`.

    Shared between the ordinary decode and the empty-`.ain` shortcut, since the companion
    resolves and is decoded independently of whether the `.ain` itself carried any bytes.
    """

    anomalies: list[dict[str, Any]] = []
    omissions: list[dict[str, Any]] = []
    typed_unidentified: list[dict[str, Any]] = []
    byte_ledger: list[dict[str, Any]] = []
    mapped: list[str] = []
    stamp: Stamp | None = None

    if closure.loc is None:
        omissions.append(
            {"role": "missing-loc", "reason": "no .loc companion resolved for this map"}
        )
        return stamp, anomalies, omissions, typed_unidentified, byte_ledger, mapped

    loc_text = closure.loc.data
    if not loc_text:
        # An empty member is recorded with its own zero-byte ledger row and an `empty-member`
        # omission rather than being folded
        # into the malformed-stamp anomaly below -- the two departures are distinct and a reader
        # of `omissions[]` should not have to infer "zero bytes" from a `malformed-loc` reason.
        omissions.append(
            {"role": "empty-member", "sourcePath": closure.loc.path, "byteLength": 0}
        )
        byte_ledger.append(coverage.build_loc_raw_ledger(closure.loc))
        return stamp, anomalies, omissions, typed_unidentified, byte_ledger, mapped

    if loc_text.endswith(b"\r\n") and loc_text[:-2].isdigit():
        raw = loc_text[:-2].decode("ascii")
        stamp = Stamp(
            raw=raw,
            value=int(raw),
            byte_length=len(closure.loc.data),
            sha256=closure.loc.sha256,
            offset=0,
            length=len(raw),
            line_end_offset=len(raw),
            line_end_length=2,
        )
        byte_ledger.append(
            coverage.build_loc_ledger(
                closure.loc,
                stamp_offset=stamp.offset,
                stamp_length=stamp.length,
                line_end_offset=stamp.line_end_offset,
                line_end_length=stamp.line_end_length,
            )
        )
        mapped.append("stamp")
        typed_unidentified.append(
            {"field": "stamp", "sourceOffset": 0, "value": stamp.value, "sha256": stamp.sha256}
        )
    else:
        # The `.loc` companion is named optional, so a companion that
        # resolved but does not parse as a decimal stamp plus CRLF still publishes the
        # `.ain`-derived unit: the anomaly is recorded and `stamp` carries no parsed value. `raw`
        # is `None`, never the file's own bytes -- a unit never embeds an opaque copy of its
        # source member; the member's own
        # `byteLength`/`sha256` (already published in `sourceResolution`) is what identifies it.
        anomalies.append(
            {
                "role": "malformed-loc",
                "reason": "not a decimal stamp plus CRLF",
                "byteLength": len(loc_text),
            }
        )
        stamp = Stamp(
            raw=None,
            value=None,
            byte_length=len(loc_text),
            sha256=closure.loc.sha256,
            offset=0,
            length=0,
            line_end_offset=0,
            line_end_length=0,
        )
        byte_ledger.append(coverage.build_loc_raw_ledger(closure.loc))
        typed_unidentified.append(
            {
                "field": "stamp.raw",
                "sourceOffset": 0,
                "reason": "not a decimal stamp plus CRLF",
                "byteLength": len(loc_text),
            }
        )
    return stamp, anomalies, omissions, typed_unidentified, byte_ledger, mapped


def _build_dependencies(closure: NavGraphSourceClosure) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    """The `map`/`map-entities` dependency rows, plus an anomaly naming either as unresolved.

    Both roles resolve against the same `.bsp`, so both warn identically -- a reference whose
    target is another seam's data and merely fails to resolve warns rather than failing the unit;
    the anomaly is what makes that
    warning actually surface through `unit_contract.warnings_for`, which does not itself read
    `dependencies[].resolved`.
    """

    key = closure.key
    bsp = bsp_path(key)
    dependencies = [
        dependency("map", f"vtmb:map:{key}", bsp, closure.map_resolved),
        dependency("map-entities", f"vtmb:map-entities:{key}", bsp, closure.map_resolved),
    ]
    anomalies: list[dict[str, Any]] = []
    if not closure.map_resolved:
        anomalies.append(
            {
                "role": "unresolved-map-dependency",
                "reason": f"the install holds no {bsp} for this graph's map/map-entities dependency",
                "sourcePath": bsp,
            }
        )
    return dependencies, anomalies


def _decode_empty_ain(closure: NavGraphSourceClosure) -> NavGraphModel:
    """The `.ain` selecting member resolved but holds zero bytes.

    The unit contract's source-resolution rule is that a unit whose selecting member is empty
    publishes with a warning -- there is nothing to tokenize, so every stream publishes empty
    rather than the tokenizer raising on an file with no `Version` token.
    """

    zero = HeaderField(value=0, offset=0, length=0)
    header = Header(
        version="",
        version_field=zero,
        num_hulls=zero,
        used_hull_bits=zero,
        zone_count=zero,
        num_nodes=zero,
        total_num_links=zero,
    )
    zones = Zones(values=(), offset=0, length=0)
    wc_lookup = WCLookup(values=(), offset=0, length=0)
    mapped: list[str] = ["header", "sourceResolution", "identity", "zones", "nodes", "links", "wcLookup"]
    omissions: list[dict[str, Any]] = [
        {"role": "empty-member", "sourcePath": closure.ain.path, "byteLength": 0}
    ]
    byte_ledger: list[dict[str, Any]] = [coverage.build_ain_ledger(closure.ain, [])]

    stamp, loc_anomalies, loc_omissions, loc_typed, loc_ledger, loc_mapped = _decode_loc(closure)
    byte_ledger.extend(loc_ledger)
    omissions.extend(loc_omissions)
    mapped.extend(loc_mapped)

    dependencies, dependency_anomalies = _build_dependencies(closure)

    return NavGraphModel(
        key=closure.key,
        asset_id=closure.asset_id,
        ain_path=closure.ain.path,
        loc_path=closure.loc.path if closure.loc is not None else None,
        members=closure.members(),
        header=header,
        zones=zones,
        nodes=[],
        node_labels=[],
        links=[],
        wc_lookup=wc_lookup,
        stamp=stamp,
        dependencies=dependencies,
        anomalies=[*loc_anomalies, *dependency_anomalies],
        omissions=omissions,
        typed_unidentified=loc_typed,
        mapped=mapped,
        byte_ledger=byte_ledger,
    )


def decode_nav_graph(closure: NavGraphSourceClosure) -> NavGraphModel:
    if not closure.ain.data:
        return _decode_empty_ain(closure)

    text = lexer.decode_text(closure.ain.data)
    tokens = lexer.tokenize(text)
    anomalies: list[dict[str, Any]] = []
    claims: list[tuple[int, int, str, str]] = []
    mapped: list[str] = ["header", "sourceResolution", "identity"]
    unknown_line_counter = [0]

    pos = 0

    def read_labelled_int(label: str, owner: str) -> HeaderField:
        nonlocal pos
        pos = _expect_label(tokens, pos, label, anomalies, claims, unknown_line_counter)
        label_token = tokens[pos]
        if pos + 1 >= len(tokens):
            raise NavGraphDecodeError(f"{label!r} names no value")
        value_token = tokens[pos + 1]
        value = _int(value_token, owner)
        field = HeaderField(
            value=value, offset=label_token.offset, length=value_token.end - label_token.offset
        )
        pos += 2
        return field

    pos = _expect_label(tokens, pos, "Version", anomalies, claims, unknown_line_counter)
    version_label = tokens[pos]
    version_value = tokens[pos + 1]
    version_field = HeaderField(
        value=0, offset=version_label.offset, length=version_value.end - version_label.offset
    )
    if version_value.text != EXPECTED_VERSION:
        anomalies.append(
            {"role": "version-not-30", "offset": version_value.offset, "value": version_value.text}
        )
    pos += 2

    num_hulls_field = read_labelled_int("NumHulls:", "header.numHulls")
    used_hull_bits_field = read_labelled_int("UsedHullBits:", "header.usedHullBits")
    zone_count_field = read_labelled_int("ZoneCount:", "header.zoneCount")

    zone_count = zone_count_field.value
    num_nodes_label_index = _scan_to(tokens, pos, "NumNodes:")
    zone_tokens = tokens[pos:num_nodes_label_index]
    if len(zone_tokens) != zone_count:
        anomalies.append(
            {
                "role": "zone-count-mismatch",
                "declared": zone_count,
                "actual": len(zone_tokens),
                "offset": zone_tokens[0].offset if zone_tokens else tokens[num_nodes_label_index].offset,
            }
        )
    zone_values = tuple(_int(token, "zones") for token in zone_tokens)
    zones = Zones(
        values=zone_values,
        offset=zone_tokens[0].offset if zone_tokens else 0,
        length=(zone_tokens[-1].end - zone_tokens[0].offset) if zone_tokens else 0,
    )
    if zone_tokens:
        claims.append((zones.offset, zones.length, "mapped-text", "zones"))
    mapped.append("zones")
    pos = num_nodes_label_index

    num_nodes_field = read_labelled_int("NumNodes:", "header.numNodes")
    num_nodes = num_nodes_field.value
    num_hulls = num_hulls_field.value

    header = Header(
        version=version_value.text,
        version_field=version_field,
        num_hulls=num_hulls_field,
        used_hull_bits=used_hull_bits_field,
        zone_count=zone_count_field,
        num_nodes=num_nodes_field,
        total_num_links=HeaderField(value=0, offset=0, length=0),  # filled once parsed below
    )
    claims.append((version_field.offset, version_field.length, "mapped-text", "header.version"))
    claims.append((num_hulls_field.offset, num_hulls_field.length, "mapped-text", "header.numHulls"))
    claims.append(
        (used_hull_bits_field.offset, used_hull_bits_field.length, "mapped-text", "header.usedHullBits")
    )
    claims.append((zone_count_field.offset, zone_count_field.length, "mapped-text", "header.zoneCount"))
    claims.append((num_nodes_field.offset, num_nodes_field.length, "mapped-text", "header.numNodes"))

    # --- node stream ---------------------------------------------------------------------------

    total_links_label_index = _scan_to(tokens, pos, "TotalNumLinks")
    node_region = tokens[pos:total_links_label_index]
    node_labels: list[NodeLabel] = []
    real_node_tokens: list[lexer.Token] = []
    for token in node_region:
        if token.text == NODE_LABEL:
            node_labels.append(NodeLabel(index=len(node_labels), offset=token.offset, length=token.length))
        else:
            real_node_tokens.append(token)

    nodes: list[Node] = []
    misaligned_label_indices: set[int] = set()
    typed_unidentified: list[dict[str, Any]] = []

    if num_nodes:
        node_width = len(real_node_tokens) // num_nodes
        remainder = len(real_node_tokens) % num_nodes
        residual_tokens: list[lexer.Token] = []
        if remainder:
            # The node region does not divide evenly by NumNodes -- a stray token (or a few)
            # among the node data. This is exactly the departure the nav-graph seam names
            # publishable (a unit recoverable only in part still publishes), so the graph is
            # decoded at the floor width for every node and the leftover tokens are claimed
            # under their own owner rather than aborting the whole unit.
            split = len(real_node_tokens) - remainder
            residual_tokens = real_node_tokens[split:]
            real_node_tokens = real_node_tokens[:split]
            anomalies.append(
                {
                    "role": "node-count-mismatch",
                    "reason": "node region token count is not a multiple of NumNodes",
                    "realTokens": len(real_node_tokens) + remainder,
                    "numNodes": num_nodes,
                    "derivedNodeWidth": node_width,
                    "remainder": remainder,
                }
            )
        expected_total = num_nodes * 32 + len(node_labels)
        if len(node_region) != expected_total:
            anomalies.append(
                {
                    "role": "node-count-mismatch",
                    "expected": expected_total,
                    "actual": len(node_region),
                    "derivedNodeWidth": node_width,
                }
            )
        if node_width < num_hulls + 2 + 2:
            raise NavGraphDecodeError(
                f"{closure.ain.path}: node width {node_width} is smaller than "
                f"origin+yaw+hullOffsets+lead ({num_hulls + 4})"
            )

        # Every node's span, computed up front from the token boundaries alone (no `Node` needed
        # yet), so the (rare) case of a `Nodes:` label landing inside a node -- rather than at a
        # boundary -- can be found in one linear merge below instead of comparing every label
        # against every node. Real corpus files carry as many labels as nodes (one per node,
        # every one exactly at a boundary, per this seam's own corpus survey), so an O(nodes x
        # labels) comparison here would be quadratic in node count on real data, not just on the
        # adversarial fixture the anomaly exists for.
        node_spans = [
            (
                real_node_tokens[index * node_width].offset,
                real_node_tokens[(index + 1) * node_width - 1].end,
            )
            for index in range(num_nodes)
        ]
        misaligned_node_indices: set[int] = set()
        span_index = 0
        for label in node_labels:
            while span_index < len(node_spans) - 1 and node_spans[span_index][1] <= label.offset:
                span_index += 1
            start, end = node_spans[span_index]
            if start < label.offset < end:
                misaligned_node_indices.add(span_index)
                misaligned_label_indices.add(label.index)

        for index in range(num_nodes):
            group = real_node_tokens[index * node_width : (index + 1) * node_width]
            origin_token = group[0]
            yaw_token = group[1]
            hull_tokens = group[2 : 2 + num_hulls]
            trailer = group[2 + num_hulls :]
            lead_tokens = trailer[-2:]
            tail_tokens = trailer[:-2]
            origin_parts = origin_token.text.split(",")
            if len(origin_parts) != 3:
                raise NavGraphDecodeError(
                    f"{closure.ain.path}: node {index} origin {origin_token.text!r} is not x,y,z"
                )
            origin_source = tuple(float(part) for part in origin_parts)
            node = Node(
                index=index,
                origin_source=origin_source,  # type: ignore[arg-type]
                yaw=_float(yaw_token, f"nodes[{index}].yaw"),
                hull_offsets=tuple(_float(token, f"nodes[{index}].hullOffsets") for token in hull_tokens),
                tail=tuple(_int(token, f"nodes[{index}].tail") for token in tail_tokens),
                lead=tuple(_int(token, f"nodes[{index}].lead") for token in lead_tokens),
                wc_id=None,
                source_line=origin_token.line,
                source_offset=origin_token.offset,
                offset=group[0].offset,
                length=group[-1].end - group[0].offset,
            )
            nodes.append(node)
            if index in misaligned_node_indices:
                # A `Nodes:` label landed inside this node's own token span (rather than at a
                # boundary): a single contiguous span claim would overlap the label's own claim
                # over the same bytes, so this (rare) node is claimed per token instead, leaving
                # the label's bytes for its own claim and any other separator to the whitespace
                # catch-all. Every other node keeps the cheap single-span claim.
                for token in group:
                    claims.append((token.offset, token.length, "mapped-text", f"nodes[{index}]"))
            else:
                claims.append((node.offset, node.length, "mapped-text", f"nodes[{index}]"))
            if tail_tokens:
                typed_unidentified.append(
                    {
                        "field": f"nodes[{index}].tail",
                        "index": index,
                        "sourceOffset": tail_tokens[0].offset,
                        "sourceLine": tail_tokens[0].line,
                        "count": len(tail_tokens),
                    }
                )
            typed_unidentified.append(
                {
                    "field": f"nodes[{index}].lead",
                    "index": index,
                    "sourceOffset": lead_tokens[0].offset,
                    "sourceLine": lead_tokens[0].line,
                    "count": len(lead_tokens),
                }
            )
        for residual_index, token in enumerate(residual_tokens):
            claims.append((token.offset, token.length, "mapped-text", f"nodes.residual[{residual_index}]"))
    elif node_region:
        raise NavGraphDecodeError(
            f"{closure.ain.path}: NumNodes is 0 but the node region holds {len(node_region)} tokens"
        )

    for label in node_labels:
        if label.index in misaligned_label_indices:
            # The label physically landed inside a node's own token span rather than between
            # two nodes -- still recoverable (the label is claimed apart from the node data
            # either way), but a departure from the corpus's observed boundary alignment worth
            # naming rather than letting the two claims silently coexist.
            anomalies.append(
                {
                    "role": "node-count-mismatch",
                    "reason": "a Nodes: label falls inside a node's own token span",
                    "offset": label.offset,
                    "index": label.index,
                }
            )
        claims.append((label.offset, label.length, "mapped-text", f"nodes.label[{label.index}]"))
    mapped.append("nodes")

    pos = total_links_label_index
    total_links_field = read_labelled_int("TotalNumLinks", "header.totalNumLinks")
    header = replace(header, total_num_links=total_links_field)
    claims.append(
        (total_links_field.offset, total_links_field.length, "mapped-text", "header.totalNumLinks")
    )
    total_num_links = total_links_field.value

    # --- link stream -----------------------------------------------------------------------------

    wc_label_index = _scan_to(tokens, pos, "WCLookup:")
    link_region = tokens[pos:wc_label_index]
    expected_link_tokens = total_num_links * LINK_TOKEN_WIDTH
    if len(link_region) != expected_link_tokens:
        anomalies.append(
            {
                "role": "link-count-mismatch",
                "declared": total_num_links,
                "expected": expected_link_tokens,
                "actual": len(link_region),
            }
        )
    links: list[Link] = []
    usable_links = min(total_num_links, len(link_region) // LINK_TOKEN_WIDTH)
    for index in range(usable_links):
        row = link_region[index * LINK_TOKEN_WIDTH : (index + 1) * LINK_TOKEN_WIDTH]
        if len({token.line for token in row}) != 1:
            anomalies.append(
                {"role": "link-count-mismatch", "index": index, "offset": row[0].offset}
            )
        src = _int(row[0], f"links[{index}].src")
        dst = _int(row[1], f"links[{index}].dst")
        if not (0 <= src < num_nodes) or not (0 <= dst < num_nodes):
            anomalies.append(
                {"role": "link-index-out-of-range", "index": index, "src": src, "dst": dst}
            )
        fields = tuple(_int(token, f"links[{index}].fields") for token in row[2:])
        link = Link(
            index=index,
            src=src,
            dst=dst,
            fields=fields,
            source_line=row[0].line,
            source_offset=row[0].offset,
            offset=row[0].offset,
            length=row[-1].end - row[0].offset,
        )
        links.append(link)
        claims.append((link.offset, link.length, "mapped-text", f"links[{index}]"))
        typed_unidentified.append(
            {
                "field": f"links[{index}].fields",
                "index": index,
                "sourceOffset": row[2].offset,
                "sourceLine": row[2].line,
                "count": len(fields),
            }
        )
    # A declared `TotalNumLinks` that disagrees with the link region's real token count (too few
    # tokens for the declared count, or leftover tokens the declared count does not reach) is the
    # `link-count-mismatch` anomaly above -- published, never fatal. Whatever the link rows did
    # not consume is still bytes the ledger must account for, so it is claimed here rather than
    # left to fail the whitespace catch-all.
    for residual_index, token in enumerate(link_region[usable_links * LINK_TOKEN_WIDTH :]):
        claims.append((token.offset, token.length, "mapped-text", f"links.residual[{residual_index}]"))
    mapped.append("links")
    pos = wc_label_index

    # --- WCLookup --------------------------------------------------------------------------------

    wc_label_token = tokens[pos]
    wc_tokens = tokens[pos + 1 :]
    if len(wc_tokens) != num_nodes:
        anomalies.append(
            {"role": "wclookup-count-mismatch", "declared": num_nodes, "actual": len(wc_tokens)}
        )
    wc_values = tuple(_int(token, "wcLookup") for token in wc_tokens)
    wc_lookup = WCLookup(
        values=wc_values,
        offset=wc_label_token.offset,
        length=(wc_tokens[-1].end if wc_tokens else wc_label_token.end) - wc_label_token.offset,
    )
    claims.append((wc_lookup.offset, wc_lookup.length, "mapped-text", "wcLookup"))
    mapped.append("wcLookup")

    # A short `wcLookup` (`wclookup-count-mismatch`, above) leaves some nodes past the end of the
    # declared table; the nav-graph seam gives `wcId: 0` the distinct meaning "no entity
    # carries the id", so a node the table never reached publishes no fabricated value at all
    # (`None`, not `0`) rather than the exporter inventing one. `Node` is frozen, so the id is
    # applied by rebuilding the record, never by mutating a published instance in place.
    nodes = [
        replace(node, wc_id=wc_values[index] if index < len(wc_values) else None)
        for index, node in enumerate(nodes)
    ]

    # --- `.loc` stamp ------------------------------------------------------------------------------

    stamp, loc_anomalies, omissions, loc_typed, byte_ledger, loc_mapped = _decode_loc(closure)
    anomalies.extend(loc_anomalies)
    typed_unidentified.extend(loc_typed)
    mapped.extend(loc_mapped)

    byte_ledger.insert(0, coverage.build_ain_ledger(closure.ain, claims))

    # --- dependencies ------------------------------------------------------------------------------

    key = closure.key
    dependencies, dependency_anomalies = _build_dependencies(closure)
    anomalies.extend(dependency_anomalies)

    return NavGraphModel(
        key=key,
        asset_id=closure.asset_id,
        ain_path=closure.ain.path,
        loc_path=closure.loc.path if closure.loc is not None else None,
        members=closure.members(),
        header=header,
        zones=zones,
        nodes=nodes,
        node_labels=node_labels,
        links=links,
        wc_lookup=wc_lookup,
        stamp=stamp,
        dependencies=dependencies,
        anomalies=anomalies,
        omissions=omissions,
        typed_unidentified=typed_unidentified,
        mapped=mapped,
        byte_ledger=byte_ledger,
    )
