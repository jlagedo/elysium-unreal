"""The two halves of `members[]` and `units[]` that only hold when they are compared.

The member table and the unit table are built from two different reads of the same install: the
walk hashes every member, and each seam decodes the members its own selector handed it. Neither
half can notice on its own that the other disagrees, so both comparisons live here and the corpus
export fails on either:

| Row | The claim | Proved against |
|---|---|---|
| a `unit` or `companion` member | `asset`/`assets[]` name the unit it became | `units[]` |
| a `unit` member | `origin`, `byteLength`, `sha256` are the bytes that unit was decoded from | the owning unit's own `sourceResolution.members[]` |

A member row cut from part of a file states the part's length and digest, so a source row carrying
a `span` -- or a `#` in its path, which is the same statement spelled in the key -- is compared on
its origin alone. Everything else is compared byte for byte.
"""

from __future__ import annotations

from typing import Any, Iterable, Mapping, Sequence

from elysium_pipeline.formats.corpus_index_glb.model import Member, Unit


def source_rows(root: Mapping[str, Any]) -> dict[str, list[dict[str, Any]]]:
    """One unit's `sourceResolution.members[]`, keyed by the install member each was cut from."""

    table: dict[str, list[dict[str, Any]]] = {}
    for row in (root.get("sourceResolution") or {}).get("members") or ():
        if not isinstance(row, Mapping):
            continue
        path = str(row.get("path", ""))
        table.setdefault(path.split("#", 1)[0], []).append(dict(row))
    return table


def _whole_file(row: Mapping[str, Any]) -> bool:
    """True where the source row states the member's whole bytes rather than a cut span."""

    return "span" not in row and "#" not in str(row.get("path", ""))


def origin_agrees(member: Mapping[str, Any], source: Mapping[str, Any]) -> bool:
    """The unit's origin answers the same search path the member's does.

    A seam may say more about where its bytes came from than the walk does -- the cut-table
    seams carry the table they were cut out of inside the origin -- so the member's own fields
    are what must agree, not the record as a whole.
    """

    published = source.get("origin")
    stated = member.get("origin")
    if not isinstance(published, Mapping) or not isinstance(stated, Mapping):
        return published == stated
    return all(published.get(key) == value for key, value in stated.items())


def _disagreement(
    path: str,
    asset: str,
    member: Mapping[str, Any],
    rows: Sequence[Mapping[str, Any]],
) -> dict[str, Any] | None:
    """What one member row and its unit's source rows disagree about, or None."""

    if not rows:
        return {
            "path": path,
            "asset": asset,
            "field": "path",
            "member": path,
            "unit": None,
        }
    for row in rows:
        if not origin_agrees(member, row):
            continue
        if not _whole_file(row):
            return None
        if (
            int(row.get("byteLength", -1)) == int(member.get("byteLength", -2))
            and str(row.get("sha256")) == str(member.get("sha256"))
        ):
            return None
    first = rows[0]
    if not origin_agrees(member, first):
        return {"path": path, "asset": asset, "field": "origin",
                "member": member.get("origin"), "unit": first.get("origin")}
    for field in ("byteLength", "sha256"):
        if first.get(field) != member.get(field):
            return {
                "path": path,
                "asset": asset,
                "field": field,
                "member": member.get(field),
                "unit": first.get(field),
            }
    return None


def unpublished_claims(
    members: Iterable[Member], published: Iterable[str]
) -> list[dict[str, Any]]:
    """Every member that names a unit the corpus does not publish."""

    units = set(published)
    rows: list[dict[str, Any]] = []
    for member in members:
        if member.disposition not in ("unit", "companion"):
            continue
        for asset in member.assets or ((member.asset,) if member.asset else ()):
            if asset not in units:
                rows.append(
                    {"path": member.path, "asset": asset, "disposition": member.disposition}
                )
    return rows


def source_disagreements(
    members: Iterable[Member], roots: Mapping[str, Mapping[str, Any]]
) -> list[dict[str, Any]]:
    """Every `unit` member whose winning bytes the owning unit says it decoded other bytes for."""

    tables: dict[str, dict[str, list[dict[str, Any]]]] = {}
    rows: list[dict[str, Any]] = []
    for member in members:
        if member.disposition != "unit":
            continue
        for asset in member.assets or ((member.asset,) if member.asset else ()):
            root = roots.get(asset)
            if root is None:
                continue                      # `unpublished_claims` already states this one
            table = tables.get(asset)
            if table is None:
                table = tables[asset] = source_rows(root)
            row = _disagreement(member.path, asset, member.source.to_json(), table.get(member.path, ()))
            if row is not None:
                rows.append(row)
    return rows


def failures(
    members: Iterable[Member],
    units: Sequence[Unit],
    roots: Mapping[str, Mapping[str, Any]],
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    """Both comparisons over one walk and one published corpus."""

    members = list(members)
    return (
        unpublished_claims(members, (unit.asset for unit in units)),
        source_disagreements(members, roots),
    )
