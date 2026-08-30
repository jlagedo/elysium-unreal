"""Independent structural validator for the Font GLB units.

`validate_document` re-decodes the selected `.fnt` (or `fontlist.txt`) bytes through
`elysium_pipeline.formats.font_glb.decode` -- the same canonical decoder the exporter calls -- and
compares the result against what the document actually published, rather than trusting
`exporters.font_glb.build_document`'s JSON assembly. The font-to-registry join is checked
structurally instead: a font unit's `source_members` never include `fontlist.txt`, so its half of
the join is self-consistency (`fontList.resolved` agrees with the `fontlist-row-missing`
anomaly and with the dependency row), not a byte-level re-parse.
"""

from __future__ import annotations

from pathlib import Path
import re
from typing import Any, Sequence

from elysium_pipeline.formats.font_glb.decode import decode_font, decode_font_list
from elysium_pipeline.formats.font_glb.model import (
    FONT_EXTENSION,
    FONT_LIST_EXTENSION,
    SCHEMA_VERSION,
    page_material_path,
    page_texture_path,
    registry_key,
)
from elysium_pipeline.formats.font_glb.source import FontListSourceClosure, FontSourceClosure, PageAvailability
from elysium_pipeline.formats.unit_contract import (
    GlbContainerError,
    SourceMember,
    UnitValidationError,
    completeness,
    generator,
    reject_opaque_source,
    validate_container,
    validate_extension_root,
    validate_ledgers,
    validate_sceneless,
)
from elysium_pipeline.formats.unit_contract import read_glb as _contract_read_glb
from elysium_pipeline.formats.unit_contract import warnings_for as _contract_warnings_for

FONT_ASSET_PREFIX = "vtmb:font:"
FONT_LIST_ASSET_PREFIX = "vtmb:font-list:"

#: The `asset.generator` title each kind signs its product with.
FONT_GENERATOR_TITLE = "Font"
FONT_LIST_GENERATOR_TITLE = "Font List"

#: A material row's own `sourcePath` is always this font's own `<stem>-page<n>.vmt` -- never
#: redirected -- unlike a texture row's `sourcePath`, which may name a `.vmt`-declared target
#: belonging to a different stem or page.
_PAGE_MATERIAL_RE = re.compile(r"-page(?P<index>\d+)\.vmt$")


class FontGlbValidationError(ValueError):
    pass


def read_glb(path: Path) -> tuple[dict[str, Any], bytes]:
    try:
        return _contract_read_glb(path)
    except GlbContainerError as error:
        raise FontGlbValidationError(str(error)) from error


def _kind_of(document: dict[str, Any]) -> str:
    extensions = document.get("extensions") or {}
    present = [name for name in (FONT_EXTENSION, FONT_LIST_EXTENSION) if name in extensions]
    if len(present) != 1:
        raise FontGlbValidationError(
            f"a unit of this seam declares exactly one of {FONT_EXTENSION}, {FONT_LIST_EXTENSION}"
        )
    return "font-list" if present[0] == FONT_LIST_EXTENSION else "font"


def _check_generator(document: dict[str, Any], title: str) -> None:
    stated = (document.get("asset") or {}).get("generator")
    if stated != generator(title):
        raise FontGlbValidationError(f"asset.generator is {stated!r}, not {generator(title)!r}")


def _rebuild_font_closure(root: dict[str, Any], member: SourceMember) -> FontSourceClosure:
    asset = str(root["identity"]["asset"])
    key = asset[len(FONT_ASSET_PREFIX):]
    pages: dict[int, dict[str, Any]] = {}
    # Pages are keyed off the material row's own, never-redirected `-page<n>.vmt` path; the
    # texture row for that same page is attached by pairing order (decode.py always appends one
    # texture row immediately before its page's material row), because a texture row's
    # `sourcePath` may itself be a `.vmt`-declared redirect naming a different stem or page.
    pending_texture: dict[str, Any] | None = None
    for row in root["dependencies"]:
        role = row.get("role")
        if role == "texture":
            pending_texture = row
            continue
        if role != "material":
            pending_texture = None
            continue
        texture_row = pending_texture
        pending_texture = None
        match = _PAGE_MATERIAL_RE.search(str(row.get("sourcePath", "")))
        if match is None:
            continue
        index = int(match.group("index"))
        entry = pages.setdefault(index, {"texture_path": None, "texture_present": False, "vmt_present": False})
        if texture_row is not None:
            entry["texture_path"] = str(texture_row.get("sourcePath", ""))
            entry["texture_present"] = bool(texture_row["resolved"])
        entry["vmt_present"] = bool(row["resolved"])
    # `_scan_pages` only creates an entry for an index the install holds at least one file for;
    # a page with neither a resolved texture nor a resolved material is exactly the "no entry at
    # all" case, so it must not be reconstructed here either or a spurious `unreferenced-page`
    # appears. `basetexture_declared` cannot be recovered from `resolved_texture_path` alone --
    # a `.vmt` that correctly names its own page's naive path looks identical to one that names
    # nothing at all -- so it is read back from whether `page-vmt-missing-basetexture` was
    # published for this page instead.
    missing_basetexture_pages = {
        int(row["page"])
        for row in root.get("anomalies") or []
        if isinstance(row, dict) and row.get("role") == "page-vmt-missing-basetexture"
    }
    available_pages: dict[int, PageAvailability] = {}
    for index, entry in pages.items():
        if not (entry["texture_present"] or entry["vmt_present"]):
            continue
        naive_tth_path = page_texture_path(key, index)
        resolved_texture_path = entry["texture_path"] or naive_tth_path
        basetexture_declared = entry["vmt_present"] and index not in missing_basetexture_pages
        available_pages[index] = PageAvailability(
            index=index,
            vmt_path=page_material_path(key, index),
            vmt_present=entry["vmt_present"],
            resolved_texture_path=resolved_texture_path,
            resolved_texture_present=entry["texture_present"],
            basetexture_declared=basetexture_declared,
        )
    font_list_row = next((row for row in root["dependencies"] if row["role"] == "font-list"), None)
    font_list_present = bool(font_list_row and font_list_row["resolved"])
    return FontSourceClosure(
        key=key,
        asset_id=asset,
        fnt=member,
        available_pages=available_pages,
        font_list_present=font_list_present,
    )


def _validate_font_list_join(root: dict[str, Any], key: str) -> None:
    dependency_row = next((row for row in root["dependencies"] if row["role"] == "font-list"), None)
    if dependency_row is None:
        raise FontGlbValidationError("a font unit declares no font-list dependency")
    field = root.get("fontList")
    has_missing_anomaly = any(
        isinstance(row, dict) and row.get("role") == "fontlist-row-missing"
        for row in root.get("anomalies") or []
    )
    if not dependency_row["resolved"]:
        if field is not None:
            raise FontGlbValidationError("fontList is populated though the registry did not resolve")
        return
    if field is None:
        raise FontGlbValidationError("fontList is absent though the registry resolved")
    if set(field) != {"resolved", "index"}:
        raise FontGlbValidationError("fontList carries fields beyond the join outcome")
    if field["resolved"]:
        if not isinstance(field["index"], int):
            raise FontGlbValidationError("fontList resolved but carries no matched row index")
        if has_missing_anomaly:
            raise FontGlbValidationError("fontList resolved but fontlist-row-missing was also recorded")
    else:
        if field["index"] is not None:
            raise FontGlbValidationError("fontList is unresolved but carries a row index")
        if not has_missing_anomaly:
            raise FontGlbValidationError("fontList is unresolved but no fontlist-row-missing was recorded")


def _redecode_font(root: dict[str, Any], member: SourceMember) -> None:
    closure = _rebuild_font_closure(root, member)
    redecoded = decode_font(closure, font_list_rows=None)
    if redecoded.header != root["header"]:
        raise FontGlbValidationError("decoded header disagrees with the published header")
    if redecoded.char_map != root["charMap"]:
        raise FontGlbValidationError("decoded char map disagrees with the published char map")
    if redecoded.glyphs != root["glyphs"]:
        raise FontGlbValidationError("decoded glyphs disagree with the published glyphs")
    if redecoded.trailer != root["trailer"]:
        raise FontGlbValidationError("decoded trailer disagrees with the published trailer")
    if redecoded.pages != root["pages"]:
        raise FontGlbValidationError("decoded pages disagree with the published pages")
    if redecoded.dependencies != root["dependencies"]:
        raise FontGlbValidationError("decoded dependencies disagree with the published dependencies")

    def _without_join(rows: Sequence[Any]) -> list[Any]:
        return [row for row in rows if not (isinstance(row, dict) and row.get("role") == "fontlist-row-missing")]

    if _without_join(redecoded.anomalies) != _without_join(root["anomalies"]):
        raise FontGlbValidationError("decoded anomalies disagree with the published anomalies")
    if redecoded.omissions != root["omissions"]:
        raise FontGlbValidationError("decoded omissions disagree with the published omissions")
    if redecoded.omissions != root["coverage"]["omittedProven"]:
        raise FontGlbValidationError("decoded omissions disagree with the published omittedProven")
    if redecoded.typed_unidentified != root["coverage"]["typedUnidentified"]:
        raise FontGlbValidationError("decoded typed-unidentified values disagree with the published ones")
    _validate_font_list_join(root, closure.key)


def _redecode_font_list(root: dict[str, Any], member: SourceMember) -> None:
    resolved_keys = {
        registry_key(row["face"], row["size"], row["weight"], row["flags"])
        for row in root["rows"]
        if row["resolved"]
    }
    closure = FontListSourceClosure(source=member)
    redecoded = decode_font_list(closure, font_keys=resolved_keys)
    if redecoded.rows != root["rows"]:
        raise FontGlbValidationError("decoded rows disagree with the published rows")
    if redecoded.comments != root["comments"]:
        raise FontGlbValidationError("decoded comments disagree with the published comments")
    if redecoded.dependencies != root["dependencies"]:
        raise FontGlbValidationError("decoded dependencies disagree with the published dependencies")
    if redecoded.anomalies != root["anomalies"]:
        raise FontGlbValidationError("decoded anomalies disagree with the published anomalies")
    if redecoded.unresolved != root["coverage"]["unresolved"]:
        raise FontGlbValidationError("decoded unresolved rows disagree with the published ones")
    if redecoded.omitted_proven != root["coverage"]["omittedProven"]:
        raise FontGlbValidationError("decoded omitted-proven spans disagree with the published ones")


def validate_document(
    document: dict[str, Any], binary: bytes, *, source_members: Sequence[SourceMember] | None = None
) -> dict[str, Any]:
    kind = _kind_of(document)
    extension = FONT_LIST_EXTENSION if kind == "font-list" else FONT_EXTENSION
    asset_prefix = FONT_LIST_ASSET_PREFIX if kind == "font-list" else FONT_ASSET_PREFIX
    title = FONT_LIST_GENERATOR_TITLE if kind == "font-list" else FONT_GENERATOR_TITLE
    try:
        root = validate_extension_root(
            document, extension, asset_prefix=asset_prefix, schema_version=SCHEMA_VERSION
        )
        validate_container(document, binary)
        validate_sceneless(document)
        _check_generator(document, title)
        validate_ledgers(root, source_members)
        reject_opaque_source(document, binary, source_members)
    except UnitValidationError as error:
        raise FontGlbValidationError(str(error)) from error

    complete = completeness(root)
    if complete["unresolved"] or complete["unsupported"]:
        raise FontGlbValidationError(f"{kind} unit is incomplete: {complete}")

    if source_members is not None:
        member = next(iter(source_members))
        if kind == "font":
            _redecode_font(root, member)
        else:
            _redecode_font_list(root, member)
    elif kind == "font":
        _validate_font_list_join(root, str(root["identity"]["asset"])[len(FONT_ASSET_PREFIX):])

    warnings = _contract_warnings_for(root)
    identity = root["identity"]
    if kind == "font":
        summary = {
            "kind": "font",
            "asset": identity["asset"],
            "key": identity["asset"][len(FONT_ASSET_PREFIX):],
            "pages": len(root["pages"]),
            "glyphs": len(root["glyphs"]),
            "dependencies": len(root["dependencies"]),
        }
    else:
        summary = {
            "kind": "font-list",
            "asset": identity["asset"],
            "rows": len(root["rows"]),
            "dependencies": len(root["dependencies"]),
        }
    ledger = root["coverage"]["byteLedger"][0]
    summary["sourceBytes"] = ledger["byteLength"]
    summary["accountedBytes"] = ledger["accountedBytes"]
    summary["byteCoveragePercent"] = ledger["coveragePercent"]
    summary["warnings"] = warnings
    return summary


def validate(path: Path) -> dict[str, Any]:
    document, binary = read_glb(path)
    try:
        return validate_document(document, binary)
    except UnitValidationError as error:
        raise FontGlbValidationError(str(error)) from error


def warnings_for(summary: dict[str, Any]) -> list[str]:
    return list(summary.get("warnings") or [])
