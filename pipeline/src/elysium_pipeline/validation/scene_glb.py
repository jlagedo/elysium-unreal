"""Independent validator for Scene GLB products.

Structural checks (container shape, extension-root key order, the byte ledger, the absence of an
source capsule) run through `elysium_pipeline.formats.unit_contract`, the shared owner of
those rules. Everything semantic to the scene format -- what an actor, a channel and an event say,
and whether a `speak`'s dependency or a `firetrigger`'s `trigger` actually follows from its own
raw `param` -- is re-derived here from a fresh tokenize-and-walk of the source bytes, written
independently of `formats.scene_glb.decode` and never by calling the exporter's `build_document`.
"""

from __future__ import annotations

import re
from collections import Counter
from pathlib import Path
from typing import Any

from elysium_pipeline.formats.scene_glb import DEGENERATE_DURATION_SECONDS, lexer
from elysium_pipeline.formats.scene_glb.model import (
    EVENT_TYPE_IDS,
    RECOGNISED_UNUSED_TOKENS,
    SCENE_EXTENSION,
    SCHEMA_VERSION,
    UNHANDLED_EVENT_TYPES,
    asset_id,
)
from elysium_pipeline.formats.unit_contract import (
    asset_id as _asset_id,
    completeness,
    read_glb,
    validate_capsules,
    validate_container,
    validate_extension_root,
    validate_ledgers,
    validate_sceneless,
)

__all__ = [
    "SceneGlbValidationError",
    "read_glb",
    "validate",
    "validate_document",
    "warnings_for",
]


class SceneGlbValidationError(ValueError):
    pass


ANOMALY_ROLES = {
    "degenerate-time", "marker-duration-mismatch", "missing-version-line",
    "inactive-block", "snap-on", "unclosed-block-at-end-of-file", "malformed-ramp-row",
    "misnested-channel", "misnested-bonerename", "misnested-faceposermodel",
    "misnested-footer-token", "unterminated-quoted-string",
}
OMISSION_ROLES = {"empty-member", "insignificant-whitespace"}
UNSUPPORTED_REASONS = {"key-outside-the-scene-vocabulary", "unknown-event-type"}
DEPENDENCY_ROLES = {"sound", "expression-table"}

_VERSION_RE = re.compile(r"choreo\s+version\s+(\d+)", re.IGNORECASE)
_DB_RE = re.compile(r"[-+]?\d+(?:\.\d+)?")


def _number(text: str | None) -> float | None:
    if text is None:
        return None
    try:
        return float(text.strip().strip('"'))
    except (TypeError, ValueError):
        return None


def _int_or_none(text: str | None) -> int | None:
    if text is None:
        return None
    try:
        return int(text.strip().strip('"'))
    except (TypeError, ValueError):
        return None


def _atoi(text: str | None) -> int:
    match = re.match(r"\s*([+-]?\d+)", text or "")
    return int(match.group(1)) if match else 0


def _parse_db(text: str | None) -> float | None:
    if not text:
        return None
    match = _DB_RE.search(text)
    return float(match.group(0)) if match else None


# --- an independent tokenize-and-walk of the raw member, for export-time comparison -----------


def _leading_version(tokens):
    for token in tokens:
        if token.kind == "whitespace":
            continue
        if token.kind == "comment":
            match = _VERSION_RE.match(token.text[2:].strip())
            if match:
                return int(match.group(1)), token
        return None, None
    return None, None


def _mp3_first_candidates(param: str) -> list[str]:
    """The `.mp3`-then-authored candidate order `decode._mp3_first_candidates` derives -- a pure
    string transform of `param`, reimplemented here rather than imported so the check does not
    depend on `formats.scene_glb.decode` agreeing with itself."""

    normalized = param.replace("\\", "/").strip()
    last = normalized.rsplit("/", 1)[-1]
    if "." in last:
        stem, _, ext = normalized.rpartition(".")
    else:
        stem, ext = normalized, ""
    candidates = []
    if ext.lower() != "mp3":
        candidates.append(f"{stem}.mp3" if stem else normalized + ".mp3")
    candidates.append(normalized)
    seen: set[str] = set()
    ordered: list[str] = []
    for candidate in candidates:
        folded = candidate.lower()
        if folded in seen:
            continue
        seen.add(folded)
        ordered.append(candidate)
    return ordered


def _reference_extras_row(record: lexer.Record, text: str) -> dict[str, Any]:
    row: dict[str, Any] = {
        "token": record.keyword,
        "words": [word.text for word in record.words[1:]],
        "offset": record.offset,
    }
    if record.open is not None:
        span = lexer.record_span_text(text, record)
        row["block"] = span[record.open.offset - record.offset:]
    else:
        row["block"] = None
    return row


def _reference_event(record: lexer.Record, text: str) -> dict[str, Any]:
    type_token = record.words[1] if len(record.words) > 1 else None
    name_token = record.words[2] if len(record.words) > 2 else None
    type_text = type_token.text if type_token is not None else ""
    result: dict[str, Any] = {
        "type": type_text,
        "typeId": EVENT_TYPE_IDS.get(type_text.strip().lower()),
        "name": name_token.text if name_token is not None else "",
        "start": None, "end": None, "param": None, "param2": None,
        "fixedLength": False, "sequenceDuration": None, "ramp": [], "extras": [],
        "offset": record.offset,
    }
    for child in record.children:
        keyword = child.keyword
        if keyword == "time":
            result["start"] = _number(child.words[1].text) if len(child.words) > 1 else None
            result["end"] = _number(child.words[2].text) if len(child.words) > 2 else None
        elif keyword == "param":
            result["param"] = child.words[1].text if len(child.words) > 1 else ""
        elif keyword == "param2":
            result["param2"] = child.words[1].text if len(child.words) > 1 else ""
        elif keyword == "fixedlength":
            result["fixedLength"] = True
        elif keyword == "sequenceduration":
            result["sequenceDuration"] = (
                _number(child.words[1].text) if len(child.words) > 1 else None
            )
        elif keyword == "event_ramp":
            for row in child.children:
                if len(row.words) >= 2:
                    result["ramp"].append(
                        {
                            "time": _number(row.words[0].text),
                            "value": _number(row.words[1].text),
                            "offset": row.offset,
                        }
                    )
        elif keyword in RECOGNISED_UNUSED_TOKENS:
            result["extras"].append(_reference_extras_row(child, text))
    return result


def _reference_bone_rename(child: lexer.Record, actor_state: dict[str, Any]) -> None:
    frm = child.words[1].text if len(child.words) > 1 else ""
    to = child.words[2].text if len(child.words) > 2 else ""
    actor_state["boneRenames"].append({"from": frm, "to": to, "offset": child.offset})


def _reference_channel(
    record: lexer.Record,
    actor_state: dict[str, Any],
    text: str,
    footer: dict[str, Any],
    anomalies: list[dict[str, Any]],
) -> dict[str, Any]:
    name_token = record.words[1] if len(record.words) > 1 else None
    result: dict[str, Any] = {
        "name": name_token.text if name_token is not None else "", "active": True, "events": [],
        "extras": [], "offset": record.offset,
    }
    for child in record.children:
        keyword = child.keyword
        if keyword == "event":
            result["events"].append(_reference_event(child, text))
        elif keyword == "active":
            value = child.words[1].text if len(child.words) > 1 else "1"
            result["active"] = _atoi(value) != 0
            if not result["active"]:
                anomalies.append(
                    {"role": "inactive-block", "offset": child.offset, "scope": "channel"}
                )
        elif keyword == "channel":
            # Mirrors `decode.decode_channel`: a channel can only reach here when an ancestor
            # channel's own block never closed, and it is still the actor's, not this channel's.
            # The slot is reserved before recursing so a nested channel's ledger owner (and, here,
            # its published position) is fixed by the time a further-nested one asks for its own.
            slot = len(actor_state["channels"])
            actor_state["channels"].append(None)
            actor_state["channels"][slot] = _reference_channel(
                child, actor_state, text, footer, anomalies
            )
        elif keyword == "bonerename":
            _reference_bone_rename(child, actor_state)
        elif keyword == "faceposermodel":
            actor_state["facePoserModel"] = child.words[1].text if len(child.words) > 1 else None
        elif keyword == "fps":
            # Mirrors `decode.decode_channel`'s own footer-recovery branch: the same
            # missing-brace defect can push `fps`/`snap` under a channel, not just an actor.
            footer["fps"] = _int_or_none(child.words[1].text) if len(child.words) > 1 else None
        elif keyword == "snap":
            value = child.words[1].text.strip().lower() if len(child.words) > 1 else None
            footer["snap"] = None if value is None else value == "on"
            if footer["snap"]:
                anomalies.append({"role": "snap-on", "offset": child.offset})
        elif keyword in RECOGNISED_UNUSED_TOKENS:
            result["extras"].append(_reference_extras_row(child, text))
    return result


def _reference_actor(
    record: lexer.Record, footer: dict[str, Any], text: str, anomalies: list[dict[str, Any]]
) -> dict[str, Any]:
    name_token = record.words[1] if len(record.words) > 1 else None
    result: dict[str, Any] = {
        "name": name_token.text if name_token is not None else "",
        "active": True, "facePoserModel": None, "boneRenames": [], "channels": [], "extras": [],
        "offset": record.offset,
    }
    actor_state = result
    for child in record.children:
        keyword = child.keyword
        if keyword == "channel":
            slot = len(actor_state["channels"])
            actor_state["channels"].append(None)
            actor_state["channels"][slot] = _reference_channel(
                child, actor_state, text, footer, anomalies
            )
        elif keyword == "bonerename":
            _reference_bone_rename(child, actor_state)
        elif keyword == "faceposermodel":
            actor_state["facePoserModel"] = child.words[1].text if len(child.words) > 1 else None
        elif keyword == "active":
            value = child.words[1].text if len(child.words) > 1 else "1"
            result["active"] = _atoi(value) != 0
            if not result["active"]:
                anomalies.append(
                    {"role": "inactive-block", "offset": child.offset, "scope": "actor"}
                )
        elif keyword == "fps":
            footer["fps"] = _int_or_none(child.words[1].text) if len(child.words) > 1 else None
        elif keyword == "snap":
            value = child.words[1].text.strip().lower() if len(child.words) > 1 else None
            footer["snap"] = None if value is None else value == "on"
            if footer["snap"]:
                anomalies.append({"role": "snap-on", "offset": child.offset})
        elif keyword in RECOGNISED_UNUSED_TOKENS:
            result["extras"].append(_reference_extras_row(child, text))
    return result


def _reference_tree(data: bytes) -> dict[str, Any]:
    text = lexer.decode_text(data)
    tokens = lexer.tokenize(text)
    significant = [token for token in tokens if token.kind in ("word", "open", "close")]
    records, _ = lexer.parse_records(text, significant)
    version, version_token = _leading_version(tokens)
    comment_rows = [
        {"offset": token.offset, "text": token.text.rstrip("\r")}
        for token in tokens
        if token.kind == "comment" and token is not version_token
    ]
    actors: list[dict[str, Any]] = []
    footer: dict[str, Any] = {"fps": None, "snap": None}
    anomalies: list[dict[str, Any]] = []
    if version_token is None:
        anomalies.append({"role": "missing-version-line", "offset": 0})
    for token in tokens:
        if token.anomaly:
            anomalies.append({"role": token.anomaly, "offset": token.offset})
    for record in records:
        keyword = record.keyword
        if keyword == "actor":
            actors.append(_reference_actor(record, footer, text, anomalies))
        elif keyword == "fps":
            footer["fps"] = _int_or_none(record.words[1].text) if len(record.words) > 1 else None
        elif keyword == "snap":
            value = record.words[1].text.strip().lower() if len(record.words) > 1 else None
            footer["snap"] = None if value is None else value == "on"
            if footer["snap"]:
                anomalies.append({"role": "snap-on", "offset": record.offset})
    fps, snap = footer["fps"], footer["snap"]
    return {
        "version": version, "fps": fps, "snap": snap, "actors": actors,
        "comments": comment_rows, "anomalies": anomalies,
    }


def _compare_extras(published: Any, expected: list[dict[str, Any]], where: str) -> None:
    extras = published or []
    if len(extras) != len(expected):
        raise SceneGlbValidationError(f"{where} extras count disagrees with the source")
    for index, (prow, erow) in enumerate(zip(extras, expected)):
        if not isinstance(prow, dict) or any(prow.get(key) != erow[key] for key in erow):
            raise SceneGlbValidationError(f"{where} extras[{index}] disagrees with the source")


def _compare_event(published: Any, expected: dict[str, Any], where: str) -> None:
    if not isinstance(published, dict):
        raise SceneGlbValidationError(f"{where} is not a record")
    for field in (
        "type", "typeId", "name", "start", "end", "param", "param2",
        "fixedLength", "sequenceDuration", "offset",
    ):
        if published.get(field) != expected[field]:
            raise SceneGlbValidationError(f"{where} {field} disagrees with the source")
    ramp = published.get("ramp") or []
    if len(ramp) != len(expected["ramp"]):
        raise SceneGlbValidationError(f"{where} ramp row count disagrees with the source")
    for index, (prow, erow) in enumerate(zip(ramp, expected["ramp"])):
        if (
            prow.get("time") != erow["time"]
            or prow.get("value") != erow["value"]
            or prow.get("offset") != erow["offset"]
        ):
            raise SceneGlbValidationError(f"{where} ramp row {index} disagrees with the source")
    _compare_extras(published.get("extras"), expected["extras"], where)


def _compare_channel(published: Any, expected: dict[str, Any], where: str) -> None:
    if not isinstance(published, dict):
        raise SceneGlbValidationError(f"{where} is not a record")
    if published.get("name") != expected["name"]:
        raise SceneGlbValidationError(f"{where} name disagrees with the source")
    if published.get("offset") != expected["offset"]:
        raise SceneGlbValidationError(f"{where} offset disagrees with the source")
    if bool(published.get("active", True)) != expected["active"]:
        raise SceneGlbValidationError(f"{where} active flag disagrees with the source")
    events = published.get("events") or []
    if len(events) != len(expected["events"]):
        raise SceneGlbValidationError(f"{where} event count disagrees with the source")
    for index, (pevent, eevent) in enumerate(zip(events, expected["events"])):
        _compare_event(pevent, eevent, f"{where} event {index}")
    _compare_extras(published.get("extras"), expected["extras"], where)


def _compare_actor(published: Any, expected: dict[str, Any], where: str) -> None:
    if not isinstance(published, dict):
        raise SceneGlbValidationError(f"{where} is not a record")
    if published.get("name") != expected["name"]:
        raise SceneGlbValidationError(f"{where} name disagrees with the source")
    if published.get("offset") != expected["offset"]:
        raise SceneGlbValidationError(f"{where} offset disagrees with the source")
    if bool(published.get("active", True)) != expected["active"]:
        raise SceneGlbValidationError(f"{where} active flag disagrees with the source")
    if published.get("facePoserModel") != expected["facePoserModel"]:
        raise SceneGlbValidationError(f"{where} facePoserModel disagrees with the source")
    renames = [
        {"from": row.get("from"), "to": row.get("to"), "offset": row.get("offset")}
        for row in published.get("boneRenames") or []
    ]
    if renames != expected["boneRenames"]:
        raise SceneGlbValidationError(f"{where} bone renames disagree with the source")
    channels = published.get("channels") or []
    if len(channels) != len(expected["channels"]):
        raise SceneGlbValidationError(f"{where} channel count disagrees with the source")
    for index, (pchannel, echannel) in enumerate(zip(channels, expected["channels"])):
        _compare_channel(pchannel, echannel, f"{where} channel {index}")
    _compare_extras(published.get("extras"), expected["extras"], where)


def _compare_with_source(root: dict[str, Any], data: bytes) -> None:
    reference = _reference_tree(data)
    if root.get("version") != reference["version"]:
        raise SceneGlbValidationError("published version disagrees with the source")
    if root.get("fps") != reference["fps"]:
        raise SceneGlbValidationError("published fps disagrees with the source")
    if root.get("snap") != reference["snap"]:
        raise SceneGlbValidationError("published snap disagrees with the source")
    published_comments = [
        {"offset": row.get("offset"), "text": row.get("text")} for row in root.get("comments") or []
    ]
    if published_comments != reference["comments"]:
        raise SceneGlbValidationError("published comments disagree with the source")
    actors = root.get("actors")
    if not isinstance(actors, list) or len(actors) != len(reference["actors"]):
        raise SceneGlbValidationError("published actor count disagrees with the source")
    for index, (published, expected) in enumerate(zip(actors, reference["actors"])):
        _compare_actor(published, expected, f"actor {index}")

    tree_derived_roles = {
        "missing-version-line", "inactive-block", "snap-on", "unterminated-quoted-string",
    }
    published_tree_anomalies = sorted(
        tuple(sorted(row.items()))
        for row in root.get("anomalies") or []
        if isinstance(row, dict) and row.get("role") in tree_derived_roles
    )
    expected_tree_anomalies = sorted(
        tuple(sorted(row.items())) for row in reference["anomalies"]
    )
    if published_tree_anomalies != expected_tree_anomalies:
        raise SceneGlbValidationError(
            "published missing-version-line/inactive-block/snap-on/unterminated-quoted-string "
            "anomalies disagree with the source"
        )


# --- structural / cross-reference checks on the published document alone -----------------------


def _iter_events(root: dict[str, Any]):
    for ai, actor in enumerate(root.get("actors") or []):
        for ci, channel in enumerate((actor or {}).get("channels") or []):
            for ei, event in enumerate((channel or {}).get("events") or []):
                yield ai, ci, ei, event


def _check_semantic_fields(root: dict[str, Any]) -> set[tuple[str, str]]:
    dependencies = root.get("dependencies") or []
    script_expressions = root.get("scriptExpressions") or []
    referenced: set[tuple[str, str]] = set()

    def dependency_at(index: Any) -> dict[str, Any]:
        if not isinstance(index, int) or not 0 <= index < len(dependencies):
            raise SceneGlbValidationError("an event names a dependency row this unit does not carry")
        return dependencies[index]

    for ai, ci, ei, event in _iter_events(root):
        where = f"actor {ai} channel {ci} event {ei}"
        if not isinstance(event, dict):
            raise SceneGlbValidationError(f"{where} is not a record")
        type_key = str(event.get("type", "")).strip().lower()
        param, param2 = event.get("param"), event.get("param2")
        start, end = event.get("start"), event.get("end")

        if type_key in ("silence", "loud"):
            marker_duration = _number(param)
            if event.get("markerDuration") != marker_duration:
                raise SceneGlbValidationError(f"{where} markerDuration disagrees with param")
            if (
                marker_duration is not None
                and start is not None and end is not None and end != -1
                and round(end - start, 3) != round(marker_duration, 3)
            ):
                has_row = any(
                    row.get("role") == "marker-duration-mismatch"
                    and row.get("offset") == event.get("offset")
                    for row in root.get("anomalies") or []
                )
                if not has_row:
                    raise SceneGlbValidationError(
                        f"{where} marker duration mismatches its own time but carries no "
                        "anomaly row"
                    )
        elif type_key in ("speak", "bodysound"):
            index = event.get("sound")
            expected_level = _parse_db(param2)
            if type_key == "bodysound":
                expected_level = 80.0 if expected_level is None else max(expected_level, 75.0)
            if event.get("level") != expected_level:
                raise SceneGlbValidationError(f"{where} level disagrees with param2")
            if param:
                row = dependency_at(index)
                if row.get("role") != "sound":
                    raise SceneGlbValidationError(f"{where} sound dependency has the wrong role")
                expected_source_path = "sound/" + param.replace("\\", "/")
                if row.get("sourcePath") != expected_source_path:
                    raise SceneGlbValidationError(
                        f"{where} sound dependency sourcePath disagrees with param"
                    )
                if type_key == "speak":
                    candidates = _mp3_first_candidates(param)
                    expected_assets = {_asset_id("sound", candidate) for candidate in candidates}
                    if row.get("asset") not in expected_assets:
                        raise SceneGlbValidationError(
                            f"{where} sound dependency asset is not one of the mp3-first candidates"
                        )
                    if row.get("resolution") != "mp3-first":
                        raise SceneGlbValidationError(
                            f"{where} speak dependency must carry mp3-first resolution"
                        )
                else:
                    expected_asset = _asset_id("sound", param.replace("\\", "/").strip())
                    if row.get("asset") != expected_asset:
                        raise SceneGlbValidationError(
                            f"{where} bodysound dependency asset disagrees with param"
                        )
                referenced.add(("sound", str(row.get("asset"))))
            elif index is not None:
                raise SceneGlbValidationError(f"{where} names a sound dependency with no param")
        elif type_key in ("expression", "flexanimation"):
            stem = (param or "").strip()
            if event.get("expressionTable") != stem:
                raise SceneGlbValidationError(f"{where} expressionTable disagrees with param")
            if event.get("expressionName") != param2:
                raise SceneGlbValidationError(f"{where} expressionName disagrees with param2")
            if stem:
                asset = _asset_id("expression-table", stem)
                expected_table_source_path = f"expressions/{stem.lower()}.vfe"
                match = next(
                    (row for row in dependencies
                     if row.get("role") == "expression-table" and row.get("asset") == asset),
                    None,
                )
                if match is None:
                    raise SceneGlbValidationError(f"{where} names no expression-table dependency")
                if match.get("sourcePath") != expected_table_source_path:
                    raise SceneGlbValidationError(
                        f"{where} expression-table dependency sourcePath disagrees with param"
                    )
                referenced.add(("expression-table", asset))
        elif type_key in ("gesture", "sequence"):
            if event.get("clipLabel") != param:
                raise SceneGlbValidationError(f"{where} clipLabel disagrees with param")
        elif type_key == "firetrigger":
            if event.get("trigger") != _atoi(param):
                raise SceneGlbValidationError(f"{where} trigger disagrees with param")
        elif type_key == "python":
            index = event.get("expression")
            if not isinstance(index, int) or not 0 <= index < len(script_expressions):
                raise SceneGlbValidationError(f"{where} names no scriptExpressions row")
            row = script_expressions[index]
            if row.get("param") != param or row.get("param2") != param2:
                raise SceneGlbValidationError(f"{where} scriptExpressions row disagrees")
        elif type_key in ("cameramove", "lookat", "face"):
            if event.get("entityNames") != [param or "", param2 or ""]:
                raise SceneGlbValidationError(f"{where} entityNames disagree with param/param2")

        if type_key in UNHANDLED_EVENT_TYPES and not event.get("unhandled"):
            raise SceneGlbValidationError(f"{where} is a {type_key} event and must carry unhandled")

        if start is not None and end is not None and end != -1:
            degenerate = end < start or (end - start) > DEGENERATE_DURATION_SECONDS
            if degenerate:
                has_row = any(
                    row.get("role") == "degenerate-time" and row.get("offset") == event.get("offset")
                    for row in root.get("anomalies") or []
                )
                if not has_row:
                    raise SceneGlbValidationError(f"{where} is degenerate but carries no anomaly row")

    declared = {(row.get("role"), row.get("asset")) for row in dependencies}
    if declared != referenced:
        raise SceneGlbValidationError(
            "the dependency set disagrees with the events that produced it"
        )
    return referenced


def validate_document(
    document: dict[str, Any], binary: bytes, *, source_members=None
) -> dict[str, Any]:
    root = validate_extension_root(
        document, SCENE_EXTENSION, asset_prefix="vtmb:scene:", schema_version=SCHEMA_VERSION
    )
    validate_container(document, binary)
    validate_sceneless(document)
    validate_ledgers(root, source_members)
    validate_capsules(document, binary, root, source_members)

    identity = root.get("identity") or {}
    key = identity.get("key")
    if not isinstance(key, str) or not key or key != key.lower():
        raise SceneGlbValidationError("scene identity carries no normalized key")
    if identity.get("asset") != asset_id(key):
        raise SceneGlbValidationError("scene identity disagrees with its key")

    for row in root.get("anomalies") or []:
        if not isinstance(row, dict) or row.get("role") not in ANOMALY_ROLES:
            raise SceneGlbValidationError(f"unknown source anomaly {row!r}")
    for row in root.get("omissions") or []:
        if not isinstance(row, dict) or row.get("role") not in OMISSION_ROLES:
            raise SceneGlbValidationError(f"unknown source omission {row!r}")
    for row in (root.get("coverage") or {}).get("unsupported") or []:
        if not isinstance(row, dict) or row.get("reason") not in UNSUPPORTED_REASONS:
            raise SceneGlbValidationError(f"unknown unsupported reason {row!r}")
    for row in root.get("dependencies") or []:
        if not isinstance(row, dict) or row.get("role") not in DEPENDENCY_ROLES:
            raise SceneGlbValidationError(f"unknown scene dependency role {row!r}")

    _check_semantic_fields(root)

    stats = completeness(root)
    if stats["unresolved"] or stats["unsupported"]:
        raise SceneGlbValidationError("scene extension is incomplete")

    if source_members is not None:
        for member in source_members:
            _compare_with_source(root, member.data)

    unresolved_dependencies = [
        str(row.get("sourcePath") or row.get("asset"))
        for row in root.get("dependencies") or []
        if not row.get("resolved")
    ]
    ledger_row = (root.get("coverage") or {}).get("byteLedger") or [{}]

    return {
        "asset": identity.get("asset"),
        "key": key,
        "version": root.get("version"),
        "actors": len(root.get("actors") or []),
        "events": sum(1 for _ in _iter_events(root)),
        "dependencies": len(root.get("dependencies") or []),
        "comments": len(root.get("comments") or []),
        "anomalies": [str(row.get("role")) for row in root.get("anomalies") or []],
        "omissions": [row for row in root.get("omissions") or [] if isinstance(row, dict)],
        "unresolvedDependencies": unresolved_dependencies,
        "sourceBytes": ledger_row[0].get("byteLength", 0),
        "accountedBytes": ledger_row[0].get("accountedBytes", 0),
        "byteCoveragePercent": ledger_row[0].get("coveragePercent", 0.0),
    }


def validate(path: Path) -> dict[str, Any]:
    document, binary = read_glb(path)
    return validate_document(document, binary)


def warnings_for(summary: dict[str, Any]) -> list[str]:
    """What a published unit could not resolve, phrased for the operator.

    `insignificant-whitespace` is not surfaced here even though it is a genuine root-level
    omission (and a genuine `coverage.omittedProven` row): it is present on essentially every
    `.vcd` in the corpus, so treating it as an operator warning would drown out the omissions --
    an empty member, most notably -- that actually call for attention.
    """

    warnings: list[str] = []
    missing = summary.get("unresolvedDependencies") or []
    if missing:
        uniq = sorted(set(missing))
        shown = uniq[:4]
        warnings.append(
            "the install carries no member for " + ", ".join(shown)
            + (f" and {len(uniq) - len(shown)} more" if len(uniq) > len(shown) else "")
        )
    for row in summary.get("omissions") or []:
        if row.get("role") == "insignificant-whitespace":
            continue
        warnings.append(f"omitted: {row.get('reason') or row.get('role')}")
    anomalies = summary.get("anomalies") or []
    if anomalies:
        counted = ", ".join(f"{role}x{count}" for role, count in sorted(Counter(anomalies).items()))
        warnings.append(f"the source departs from the scene format's conventions: {counted}")
    return warnings
