"""The nine properties no single unit can validate.

Cross-unit consistency -- an inheritance chain, a model's include tree, a map's material closure
-- is a corpus property checked here, not something one unit can be validated against
(`seam_map_unit_contract.md`, "Validation"). Each check is one `crossUnitChecks[]` row with
`name`, `passed` and `failures[]`, and a failed check fails the corpus export like an unclaimed
member does. A check whose rule the owning seam map has not identified yet carries the pair it
would compare in `observations[]` and passes: `nav-graph-stamp` is the one such check.

Every check reads the published units through the two surfaces the unit contract makes uniform --
the reference graph and the extension root's own named fields -- so a check never re-decodes the
install.
"""

from __future__ import annotations

from typing import Any, Iterable, Mapping, Sequence

from elysium_pipeline.formats.corpus_index_glb.graph import unpublished_map_references
from elysium_pipeline.formats.corpus_index_glb.model import CHECK_NAMES, Reference, Unit


def _row(
    name: str,
    failures: Sequence[Mapping[str, Any]],
    observations: Sequence[Mapping[str, Any]] | None = None,
) -> dict[str, Any]:
    """One `crossUnitChecks[]` row.

    `observations[]` is the second column a check may carry: a disagreement the corpus records
    without calling it a defect, for a rule the owning seam map has not identified yet. It is
    published only by a check that has one.
    """

    if name not in CHECK_NAMES:
        raise ValueError(f"unknown cross-unit check {name!r}")
    row = {"name": name, "passed": not failures, "failures": [dict(item) for item in failures]}
    if observations is not None:
        row["observations"] = [dict(item) for item in observations]
    return row


def _edges(edges: Iterable[Reference], *, from_kind: str, role: str) -> list[Reference]:
    prefix = f"vtmb:{from_kind}:"
    return [edge for edge in edges if edge.source.startswith(prefix) and edge.role == role]


def _cycle(graph: Mapping[str, Sequence[str]]) -> list[str] | None:
    """The first cycle a depth-first walk of `graph` finds, as the path that closes it."""

    colour: dict[str, int] = {}
    stack: list[str] = []

    def visit(node: str) -> list[str] | None:
        colour[node] = 1
        stack.append(node)
        for target in graph.get(node, ()):
            state = colour.get(target, 0)
            if state == 1:
                return stack[stack.index(target):] + [target]
            if state == 0:
                found = visit(target)
                if found is not None:
                    return found
        stack.pop()
        colour[node] = 2
        return None

    for node in sorted(graph):
        if colour.get(node, 0) == 0:
            found = visit(node)
            if found is not None:
                return found
    return None


def _chain_check(
    name: str,
    edges: Sequence[Reference],
    published: Mapping[str, Any],
    *,
    from_kind: str,
    role: str,
    label: str,
) -> dict[str, Any]:
    """Every edge of one role resolves to a published unit, and the graph it forms is acyclic."""

    selected = _edges(edges, from_kind=from_kind, role=role)
    failures: list[dict[str, Any]] = []
    graph: dict[str, list[str]] = {}
    for edge in selected:
        graph.setdefault(edge.source, []).append(edge.target)
        if not edge.resolved or edge.target not in published:
            failures.append(
                {
                    "from": edge.source,
                    "to": edge.target,
                    "sourcePath": edge.source_path,
                    "reason": f"{label} names no published unit",
                }
            )
    cycle = _cycle(graph)
    if cycle is not None:
        failures.append({"from": cycle[0], "cycle": cycle, "reason": f"{label} is cyclic"})
    return _row(name, failures)


def surface_property_inheritance(edges, published) -> dict[str, Any]:
    """Every `base` chain terminates and is acyclic."""

    return _chain_check(
        "surface-property-inheritance", edges, published,
        from_kind="surface-property", role="surface-property", label="a base chain",
    )


def model_include_tree(edges, published) -> dict[str, Any]:
    """Every include edge resolves and the tree is acyclic."""

    return _chain_check(
        "model-include-tree", edges, published,
        from_kind="model", role="model", label="an include tree",
    )


def _table_rows(root: Mapping[str, Any]) -> set[str]:
    """The row names one expression-table unit carries, folded to lower case.

    The VFE's decoded table is where the rows live when the install ships one; a TXT-only unit
    carries the authored table instead, so both are read.
    """

    names: set[str] = set()
    for section in ("table", "txt", "authoring"):
        block = root.get(section)
        if not isinstance(block, Mapping):
            continue
        for key in ("rows", "expressions", "entries"):
            for row in block.get(key) or ():
                if isinstance(row, Mapping):
                    for field in ("name", "key", "expression"):
                        if row.get(field):
                            names.add(str(row[field]).lower())
                            break
                elif isinstance(row, str):
                    names.add(row.lower())
    return names


def scene_events(root: Mapping[str, Any]) -> Iterable[Mapping[str, Any]]:
    """Every event of one scene unit: a scene nests them under `actors[].channels[].events[]`."""

    for actor in root.get("actors") or ():
        if not isinstance(actor, Mapping):
            continue
        for channel in actor.get("channels") or ():
            if not isinstance(channel, Mapping):
                continue
            for event in channel.get("events") or ():
                if isinstance(event, Mapping):
                    yield event


def _resolved_targets(root: Mapping[str, Any], role: str) -> set[str]:
    """The identities one unit's own dependency rows of `role` say the install answers."""

    return {
        str(row.get("asset"))
        for row in root.get("dependencies") or ()
        if isinstance(row, Mapping) and row.get("role") == role and row.get("resolved")
    }


#: The role `danglingReferences[]` groups this check's absent expression rows under. A row is
#: not an edge any unit declares -- it names a row inside a table, not the table -- so it takes a
#: role of its own rather than being folded into the `expression-table` edges.
EXPRESSION_ROW_ROLE = "expression-row"


def dangling_rows(rows: Iterable[Mapping[str, Any]]) -> dict[str, list[dict[str, Any]]]:
    """The `danglingReferences[]` groups the checks contribute, keyed by role.

    A check that finds a retail inconsistency the corpus should document rather than refuse
    publishes it here instead of in `failures[]`; see `scene_expression_rows`.
    """

    extra: dict[str, list[dict[str, Any]]] = {}
    for row in rows:
        if row.get("name") == "scene-expression-rows":
            edges = [dict(item) for item in row.get("observations") or ()]
            if edges:
                extra[EXPRESSION_ROW_ROLE] = edges
    return extra


def scene_expression_rows(roots: Mapping[str, Mapping[str, Any]]) -> dict[str, Any]:
    """Every `expression` event names a row its table carries.

    An event whose table the install does not ship -- every shipped scene names the stem `dialog`,
    which no `expressions/` member answers -- is a dangling reference and is reported as one, the
    same reading `dialogue-line-audio` takes of a line whose audio is absent. What this check owns
    is the table a scene *does* resolve: it must be published.

    A published table that does not carry the named row is a third case, and it is retail's, not
    the corpus's: `cinematic/la/chambers/{camarilla_bip1,sabbat_bip1}` animate
    `Concern No Deform` against `regent_expressions`, and that table's 33 rows do not include it
    under any spelling -- five other tables do. Refusing the export would make a shipped
    authoring mistake fatal to decoding the corpus, which the unit contract's non-canonical
    storage rule says is exactly backwards: the disagreement is recorded with its evidence and
    the corpus is published. Each such row becomes an `observations[]` entry and is published in
    `danglingReferences[]` under `expression-row`, where the index already warns on the total.
    """

    tables = {
        asset: _table_rows(root)
        for asset, root in roots.items()
        if asset.startswith("vtmb:expression-table:")
    }
    failures: list[dict[str, Any]] = []
    absent: list[dict[str, Any]] = []
    seen: set[tuple[str, str, str]] = set()
    for asset in sorted(roots):
        if not asset.startswith("vtmb:scene:"):
            continue
        resolved = _resolved_targets(roots[asset], "expression-table")
        for event in scene_events(roots[asset]):
            stem = event.get("expressionTable")
            row = event.get("expressionName")
            if not stem or not row:
                continue
            table = f"vtmb:expression-table:{str(stem).lower()}"
            if table not in resolved:
                continue
            rows = tables.get(table)
            if rows is None:
                failures.append(
                    {"from": asset, "to": table, "row": str(row),
                     "reason": "the scene names no published expression table"}
                )
            elif rows and str(row).lower() not in rows:
                key = (asset, table, str(row))
                if key in seen:
                    continue
                seen.add(key)
                absent.append(
                    {"from": asset, "to": table, "row": str(row), "sourcePath": str(row),
                     "reason": "the table carries no such row"}
                )
    return _row("scene-expression-rows", failures, absent)


def surface_sound_scripts(edges, published) -> dict[str, Any]:
    """Every `impact`/`scrape` script name is an entry of a sound-script unit."""

    failures: list[dict[str, Any]] = []
    for edge in _edges(edges, from_kind="surface-property", role="sound-script"):
        if edge.target not in published:
            failures.append(
                {"from": edge.source, "to": edge.target, "sourcePath": edge.source_path,
                 "reason": "no sound-script unit declares this entry"}
            )
    return _row("surface-sound-scripts", failures)


def nav_graph_stamp(roots: Mapping[str, Mapping[str, Any]]) -> dict[str, Any]:
    """Each `.loc` value against its map's `mapRevision`, recorded rather than asserted.

    `seam_map_nav_graph.md`, "The `.loc` stamp": the stamp's meaning is typed-unidentified -- on
    sp_tutorial_1 the value equals neither the BSP's `mapRevision` nor the CRC-32 of the patched
    BSP -- and "the corpus index carries the cross-check once the rule is found". Until it is,
    this check states the pair it would compare: every nav graph that ships a `.loc` beside a
    published map becomes one `observations[]` row carrying the stamp, the revision and whether
    they agree. Asserting equality here would fail every map on a rule the seam map documents as
    not holding.
    """

    revisions: dict[str, Any] = {}
    for asset, root in roots.items():
        if not asset.startswith("vtmb:map:"):
            continue
        header = root.get("header")
        if isinstance(header, Mapping) and header.get("mapRevision") is not None:
            revisions[asset[len("vtmb:map:"):]] = header["mapRevision"]
    observations: list[dict[str, Any]] = []
    for asset in sorted(roots):
        if not asset.startswith("vtmb:nav-graph:"):
            continue
        stamp = roots[asset].get("stamp")
        if not isinstance(stamp, Mapping) or stamp.get("value") is None:
            continue
        key = asset[len("vtmb:nav-graph:"):]
        if key not in revisions:
            continue
        observations.append(
            {
                "from": asset,
                "to": f"vtmb:map:{key}",
                "stamp": stamp.get("value"),
                "mapRevision": revisions[key],
                "agrees": int(stamp["value"]) == int(revisions[key]),
            }
        )
    return _row("nav-graph-stamp", (), observations)


def font_list(edges, published) -> dict[str, Any]:
    """Every `fontlist.txt` row matches a font unit."""

    failures: list[dict[str, Any]] = []
    for edge in _edges(edges, from_kind="font-list", role="font"):
        if not edge.resolved or edge.target not in published:
            failures.append(
                {"from": edge.source, "to": edge.target, "sourcePath": edge.source_path,
                 "reason": "the registry row matches no font unit"}
            )
    return _row("font-list", failures)


def dialogue_line_audio(edges, published) -> dict[str, Any]:
    """Every dialogue line whose audio the convention names resolves to a sound unit,
    `.mp3`-first."""

    # An unresolved line is a dangling reference rather than a broken corpus rule: the convention
    # names an audio path the install may simply not ship, and `danglingReferences[]` is where
    # that is reported. What this check owns is the line the install *does* resolve and the sound
    # seam nevertheless did not publish.
    failures: list[dict[str, Any]] = []
    for edge in _edges(edges, from_kind="dialogue", role="sound"):
        if not edge.resolved:
            continue
        if edge.target not in published:
            failures.append(
                {"from": edge.source, "to": edge.target, "sourcePath": edge.source_path,
                 "reason": "the line resolved to a sound no unit publishes"}
            )
    return _row("dialogue-line-audio", failures)


#: The four units one BSP is cut into: the root, then the three lump-family sub-units. The names
#: are the ones the root's own `partition[]` rows carry in their `unit` column.
MAP_UNITS = ("map", "map-entities", "map-lighting", "map-visibility")


def _member_spans(root: Mapping[str, Any]) -> list[tuple[int, int]]:
    """Every span one sub-unit was cut from, in the coordinates of the file it was cut from.

    A member with no `span` is the whole file; that is the root's own member, and the root is not
    one of the sub-units this reads.
    """

    spans: list[tuple[int, int]] = []
    for member in (root.get("sourceResolution") or {}).get("members") or ():
        if not isinstance(member, Mapping):
            continue
        span = member.get("span")
        if isinstance(span, Mapping):
            spans.append((int(span.get("offset", 0)), int(span.get("length", 0))))
    return spans


def map_partition(roots: Mapping[str, Mapping[str, Any]]) -> dict[str, Any]:
    """Each map's four ledgers together claim every BSP byte once.

    The root publishes the whole partition -- one `partition[]` row per region, each naming the
    unit that owns it -- and each sub-unit publishes the spans it was cut from. The property is
    that the two agree: the regions tile the BSP once, and every region a sub-unit owns is a span
    that sub-unit actually cut, with nothing cut that no region delegates.
    """

    keys = sorted(
        asset[len("vtmb:map:"):] for asset in roots if asset.startswith("vtmb:map:")
    )
    failures: list[dict[str, Any]] = []
    for key in keys:
        missing = [kind for kind in MAP_UNITS if f"vtmb:{kind}:{key}" not in roots]
        if missing:
            failures.append(
                {"map": key, "missing": missing,
                 "reason": "the map publishes fewer than its four units"}
            )
            continue
        root = roots[f"vtmb:map:{key}"]
        members = (root.get("sourceResolution") or {}).get("members") or []
        length = int(members[0].get("byteLength", 0)) if members else 0
        regions = sorted(
            (int(row.get("offset", 0)), int(row.get("length", 0)), str(row.get("unit", "")))
            for row in root.get("partition") or ()
            if isinstance(row, Mapping)
        )
        cursor = 0
        broken = False
        delegated: dict[str, set[tuple[int, int]]] = {kind: set() for kind in MAP_UNITS[1:]}
        for offset, span, unit in regions:
            if offset != cursor:
                failures.append(
                    {"map": key, "offset": min(offset, cursor),
                     "reason": "two partition regions overlap" if offset < cursor
                               else "a partition region leaves a gap"}
                )
                broken = True
                break
            if unit in delegated:
                delegated[unit].add((offset, span))
            cursor = offset + span
        if broken:
            continue
        if cursor != length:
            failures.append(
                {"map": key, "offset": cursor, "byteLength": length,
                 "reason": "the partition stops before the end of the BSP"}
            )
            continue
        for kind, regions_owned in delegated.items():
            cut = {span for span in _member_spans(roots[f"vtmb:{kind}:{key}"]) if span[1]}
            owned = {span for span in regions_owned if span[1]}
            if cut != owned:
                failures.append(
                    {"map": key, "unit": f"vtmb:{kind}:{key}",
                     "delegated": [list(span) for span in sorted(owned - cut)],
                     "cut": [list(span) for span in sorted(cut - owned)],
                     "reason": "the sub-unit's spans are not the regions the root delegates"}
                )
    return _row("map-partition", failures)


def texture_material_roles(units: Sequence[Unit], edges: Sequence[Reference]) -> dict[str, Any]:
    """Every texture unit is bound by at least one material, is a map's own reflection-probe
    placement, or is an orphan.

    A texture some other kind reaches without any material binding it is the failure: it means a
    referrer composed a texture path the material layer never declares. A baked reflection probe
    (SF-1.3, `maps/<map>/c<x>_<y>_<z>`) is the one named exception -- the owner call "Baked
    reflection probes are not reflection content" (`seam_migration.md`, 2026-08-31) is that a
    probe's pixels are never sampled by a surface, only its origin placed as a capture, so a map
    binding one by its own `cubemaps[]` lump (role `texture`, source kind `map`) is exactly as
    legitimate a binder as a material and is not the gap this check exists to catch.
    """

    binders: dict[str, set[str]] = {}
    for edge in edges:
        if edge.target.startswith("vtmb:texture:"):
            binders.setdefault(edge.target, set()).add(edge.source.split(":")[1])
    failures: list[dict[str, Any]] = []
    for unit in units:
        if unit.kind != "texture":
            continue
        bound = binders.get(unit.asset)
        if bound and "material" not in bound and "map" not in bound:
            failures.append(
                {"to": unit.asset, "boundBy": sorted(bound),
                 "reason": "no material binds this texture"}
            )
    return _row("texture-material-roles", failures)


def map_references_published(
    roots: Mapping[str, Mapping[str, Any]], units: Sequence[Unit]
) -> dict[str, Any]:
    """Every unit a map root names in `textures[]`, `cubemaps[]` or `pakfile.entries[].unit` is
    published by the corpus (`seam_migration.md` -> "Plan -- surfaces track" -> SF-1.5)."""

    return _row("map-references-published", unpublished_map_references(roots, units))


def run(
    units: Sequence[Unit],
    edges: Sequence[Reference],
    roots: Mapping[str, Mapping[str, Any]],
) -> list[dict[str, Any]]:
    """Every check, in the order the seam map tabulates them."""

    published = set(roots)
    rows = [
        surface_property_inheritance(edges, published),
        model_include_tree(edges, published),
        scene_expression_rows(roots),
        surface_sound_scripts(edges, published),
        nav_graph_stamp(roots),
        font_list(edges, published),
        dialogue_line_audio(edges, published),
        map_partition(roots),
        texture_material_roles(units, edges),
        map_references_published(roots, units),
    ]
    assert [row["name"] for row in rows] == list(CHECK_NAMES)
    return rows


def failed(rows: Iterable[Mapping[str, Any]]) -> list[str]:
    """The names of the checks that did not pass."""

    return [str(row["name"]) for row in rows if not row.get("passed")]
