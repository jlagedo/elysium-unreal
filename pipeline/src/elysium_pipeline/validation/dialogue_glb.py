"""Independent structural validator for Dialogue GLB products.

Every check here re-derives its answer from the published document alone -- the row fields, the
raw condition/action text, the key and the line ids -- and never from `exporters.dialogue_glb`.
The row-classification rules (`formats.dialogue_glb.rules`) and the expression tokenizer
(`formats.dialogue_glb.exprlex`) are shared with the decoder by design: they are the seam's
vocabulary, not the writer, so re-running them here is the re-decode the contract asks for.
"""

from __future__ import annotations

from pathlib import Path
from typing import Any, Sequence

from elysium_pipeline.formats.dialogue_glb import exprlex, rules
from elysium_pipeline.formats.dialogue_glb.model import (
    AUDIO_LANGUAGES,
    FIELD_COUNT,
    KIND,
    RESERVED_COLUMNS,
    ROLE_NPC_LINE,
    ROLE_PC_CHOICE,
    SCHEMA_VERSION,
    DIALOGUE_EXTENSION,
    audio_dir_for,
    normalize_dlg_key,
)
from elysium_pipeline.formats.unit_contract import SourceMember
from elysium_pipeline.formats.unit_contract import asset_id as _stable_asset_id
from elysium_pipeline.formats.unit_contract import read_glb as _read_glb
from elysium_pipeline.formats.unit_contract import (
    completeness,
    validate_capsules,
    validate_container,
    validate_extension_root,
    validate_ledgers,
    validate_sceneless,
)
from elysium_pipeline.formats.unit_contract.capsule import declares_capsule
from elysium_pipeline.formats.unit_contract.validate import UnitValidationError

ASSET_PREFIX = f"vtmb:{KIND}:"


class DialogueGlbValidationError(ValueError):
    pass


def read_glb(path: Path) -> tuple[dict[str, Any], bytes]:
    return _read_glb(path)


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise DialogueGlbValidationError(message)


def _field_offset(fields: list[dict[str, Any]], column: int, default: int) -> int:
    for record in fields:
        if record.get("index") == column:
            return int(record.get("sourceOffset", default))
    return default


def _row_matches(published: list[dict[str, Any]], **fields: Any) -> bool:
    """Whether some row in `published` (an `anomalies[]` or `coverage.unresolved[]` list) carries
    exactly these field values -- shared because both lists are matched the same way."""

    return any(all(row.get(key) == value for key, value in fields.items()) for row in published)



def _recheck_line(
    line: dict[str, Any],
    published_anomalies: list[dict[str, Any]],
    published_unresolved: list[dict[str, Any]],
    published_typed_unidentified: list[dict[str, Any]],
) -> None:
    """Re-derive one published line's role/marker/stage-directions from its own raw fields."""

    index = line.get("index")
    fields = line.get("fields") or []

    # Column 0 is a named field like the other six below, but its published `id` is a parsed
    # int, not a text copy, so it gets its own re-parse rather than a plain string comparison.
    id_record = next((r for r in fields if r.get("index") == 0), None)
    if id_record is not None:
        id_text = str(id_record.get("text", "")).strip()
        try:
            id_from_field = int(id_text)
        except ValueError:
            id_from_field = None
        _require(
            line.get("id") == id_from_field,
            f"line {index}: id disagrees with a fresh parse of its own fields[0] cell",
        )

    role, link_target, link_reason = rules.classify_link(str(line.get("link", "")))
    _require(line.get("role") == role, f"line {index}: published role disagrees with its link")
    if link_target is not None:
        _require(line.get("linkTarget") == link_target, f"line {index}: linkTarget missing")
    else:
        _require("linkTarget" not in line, f"line {index}: linkTarget present on a non-choice row")
    if link_reason is not None:
        _require(
            _row_matches(
                published_unresolved, line=index, field="link", reason=link_reason
            ),
            f"line {index}: a malformed link is not carried in coverage.unresolved",
        )
    if line.get("id") is None:
        # The only way `decode_dialogue` ever publishes a null `id` is a column-0 cell that did
        # not parse as an integer, which it always carries as `non-integer-line-id`.
        _require(
            _row_matches(
                published_unresolved, line=index, field="id", reason="non-integer-line-id"
            ),
            f"line {index}: a null id is not carried in coverage.unresolved",
        )

    # Every named column is a copy of the `fields[]` cell it was read from; tampering one alone
    # (leaving the other untouched) is what an opaque, unchecked copy would let through.
    for column, key in (
        (1, "textMale"), (2, "textFemale"), (12, "textMalkavian"),
        (3, "link"), (4, "condition"), (5, "action"),
    ):
        record = next((r for r in fields if r.get("index") == column), None)
        if record is None:
            continue
        _require(
            line.get(key) == record.get("text"),
            f"line {index}: {key} disagrees with its own fields[{column}] cell",
        )

    marker = rules.classify_marker(role, str(line.get("textMale", "")))
    if marker is not None:
        _require(line.get("marker") == marker, f"line {index}: published marker disagrees")
    else:
        _require("marker" not in line, f"line {index}: unexpected marker")

    expected_directions: list[dict[str, Any]] = []
    for column, key in ((1, "textMale"), (2, "textFemale"), (12, "textMalkavian")):
        text = line.get(key)
        if text is None:
            continue
        if column == 12 and not any(record.get("index") == 12 for record in fields):
            continue
        offset = _field_offset(fields, column, int(line.get("sourceOffset", 0)))
        directions, direction_anomalies = rules.stage_directions(column, str(text), offset)
        expected_directions += directions
        for anomaly in direction_anomalies:
            _require(
                _row_matches(published_anomalies, role="unterminated-stage-direction",
                              sourceOffset=anomaly["sourceOffset"]),
                f"line {index}: an unterminated stage direction is not carried in anomalies[]",
            )
    _require(
        line.get("stageDirections") == expected_directions,
        f"line {index}: stage directions disagree with a fresh scan of its own text",
    )

    for column in RESERVED_COLUMNS:
        record = next((r for r in fields if r.get("index") == column), None)
        if record is None or record.get("malformed"):
            continue
        # Literal non-empty, matching decode.py's own predicate (and the identical rule for
        # `expressions[]`'s column-4/5 cells): a whitespace-only cell is still a use of a
        # reserved column, not proven-empty.
        non_empty = str(record.get("text", "")) != ""
        present = _row_matches(
            published_anomalies, role="reserved-column-used", line=index, column=column
        )
        _require(
            present == non_empty,
            f"line {index} column {column}: reserved-column-used anomaly disagrees with the "
            "field's own content",
        )
        if non_empty:
            _require(
                _row_matches(
                    published_typed_unidentified,
                    line=index,
                    column=column,
                    text=record.get("text"),
                    sourceOffset=record.get("sourceOffset"),
                ),
                f"line {index} column {column}: reserved-column-used carries no matching "
                "coverage.typedUnidentified row",
            )


def _recheck_expressions(root: dict[str, Any]) -> None:
    expressions = root.get("expressions") or []
    for line in root.get("lines") or []:
        role = line.get("role")
        condition_raw = str(line.get("condition", ""))
        action_raw = str(line.get("action", ""))
        condition_idx = line.get("conditionExpression")
        action_idx = line.get("actionExpression")
        _require(bool(condition_raw) == (condition_idx is not None),
                  f"line {line.get('index')}: conditionExpression disagrees with its raw text")
        _require(bool(action_raw) == (action_idx is not None),
                  f"line {line.get('index')}: actionExpression disagrees with its raw text")
        if condition_idx is not None:
            entry = expressions[condition_idx]
            expected_kind = "condition" if role == ROLE_PC_CHOICE else "action"
            _require(entry.get("line") == line.get("index") and entry.get("column") == 4,
                      "a conditionExpression index names the wrong row/column")
            _require(entry.get("kind") == expected_kind, "a condition expression has the wrong kind")
            _require(entry.get("text") == condition_raw, "a condition expression text disagrees")
            _require(
                entry.get("tokens") == exprlex.tokenize_expression(condition_raw),
                "a condition expression's tokens disagree with a fresh tokenization",
            )
        if action_idx is not None:
            entry = expressions[action_idx]
            _require(entry.get("line") == line.get("index") and entry.get("column") == 5,
                      "an actionExpression index names the wrong row/column")
            _require(entry.get("kind") == "action", "an action expression has the wrong kind")
            _require(entry.get("text") == action_raw, "an action expression text disagrees")
            _require(
                entry.get("tokens") == exprlex.tokenize_expression(action_raw),
                "an action expression's tokens disagree with a fresh tokenization",
            )


def _recheck_links(root: dict[str, Any]) -> None:
    lines = root.get("lines") or []
    anomalies = root.get("anomalies") or []
    by_id: dict[int, dict[str, Any]] = {}
    seen: set[int] = set()
    for line in lines:
        line_id = line.get("id")
        if line_id is None:
            continue
        if line_id in seen:
            _require(
                _row_matches(anomalies, role="duplicate-line-id", line=line.get("index"), id=line_id),
                f"line {line.get('index')}: repeats id {line_id} with no duplicate-line-id anomaly",
            )
        else:
            seen.add(line_id)
            by_id[line_id] = line
    for line in lines:
        if line.get("role") != ROLE_PC_CHOICE or "linkTarget" not in line:
            continue
        target = line["linkTarget"]
        if target == 0:
            continue
        target_line = by_id.get(target)
        if target_line is None:
            _require(
                _row_matches(anomalies, role="link-to-missing-line",
                              line=line.get("index"), linkTarget=target),
                f"line {line.get('index')}: link {target} names no row and carries no anomaly",
            )
            continue
        if target_line.get("role") != ROLE_NPC_LINE:
            _require(
                _row_matches(anomalies, role="link-to-non-npc-line",
                              line=line.get("index"), linkTarget=target),
                f"line {line.get('index')}: link {target} does not name an NPC line and carries "
                "no anomaly",
            )


def _recheck_omitted_proven(root: dict[str, Any]) -> None:
    """Every `omitted-proven` byte-ledger range is backed by an `omissions` row that names the
    same owner, so an evidence-backed range can never be paired with an empty explanation."""

    coverage = root.get("coverage") or {}
    omissions = root.get("omissions") or []
    owners_with_evidence = {row.get("owner") for row in omissions if isinstance(row, dict)}
    for ledger_row in coverage.get("byteLedger") or []:
        for span in ledger_row.get("ranges") or []:
            if span.get("state") != "omitted-proven":
                continue
            owner = span.get("owner")
            _require(
                owner in owners_with_evidence,
                f"an omitted-proven range (owner {owner!r}) carries no matching omissions row",
            )


def _expected_stem(key: str, line_id: int, lang: str) -> str:
    return f"{audio_dir_for(key)}/line{line_id}_col_{lang}"


def _recheck_audio(root: dict[str, Any], key: str) -> None:
    """Every candidate path is the deterministic function of the key and the line id, and every
    sound/scene candidate this join actually resolves traces to exactly one `dependencies` row
    with the matching `sourcePath`/`resolution`. An unresolved candidate declares no row at all
    -- it stays visible only inside this line's own `audio.lines[].candidates[]`."""

    audio = root.get("audio") or {}
    audio_lines = {row["line"]: row for row in audio.get("lines") or []}
    lines = root.get("lines") or []
    dependencies = root.get("dependencies") or []
    declared: dict[tuple[str, str], dict[str, Any]] = {
        (row["role"], row["asset"]): row
        for row in dependencies
        if row.get("role") in ("sound", "scene")
    }
    expected: dict[tuple[str, str], dict[str, Any]] = {}

    for line in lines:
        if line.get("role") != ROLE_NPC_LINE or line.get("id") is None:
            continue
        row = audio_lines.get(line["index"])
        _require(row is not None, f"line {line['index']}: no audio candidates published")
        _require(row.get("id") == line["id"], f"line {line['index']}: audio row names the wrong id")
        candidates = {c["lang"]: c for c in row.get("candidates") or []}
        _require(set(candidates) == set(AUDIO_LANGUAGES), "audio candidates miss a language")
        for lang in AUDIO_LANGUAGES:
            stem = _expected_stem(key, line["id"], lang)
            candidate = candidates[lang]
            sound = candidate["sound"]
            mp3_asset = _stable_asset_id("sound", f"{stem}.mp3")
            wav_asset = _stable_asset_id("sound", f"{stem}.wav")
            scene_asset = _stable_asset_id("scene", stem)
            wav_path = f"sound/{stem}.wav"
            vcd_path = f"sound/{stem}.vcd"
            sound_candidates = {c["asset"]: c["resolved"] for c in sound["candidates"]}
            _require(
                sound_candidates == {mp3_asset: sound_candidates.get(mp3_asset, False),
                                      wav_asset: sound_candidates.get(wav_asset, False)}
                and mp3_asset in sound_candidates and wav_asset in sound_candidates,
                f"line {line['index']}/{lang}: sound candidates disagree with the path convention",
            )
            mp3_resolved = sound_candidates[mp3_asset]
            wav_resolved = sound_candidates[wav_asset]
            expected_sound_resolved = mp3_resolved or wav_resolved
            _require(sound["resolved"] == expected_sound_resolved,
                      f"line {line['index']}/{lang}: sound.resolved disagrees with its candidates")
            if expected_sound_resolved:
                expected_asset = mp3_asset if mp3_resolved else wav_asset
                _require(sound["asset"] == expected_asset,
                          f"line {line['index']}/{lang}: mp3-first resolution disagrees")
            else:
                _require(sound["asset"] is None, f"line {line['index']}/{lang}: resolved but no candidate")
            _require(candidate["scene"]["asset"] == scene_asset,
                      f"line {line['index']}/{lang}: scene asset disagrees with the path convention")
            _require("lip" not in candidate,
                      f"line {line['index']}/{lang}: a candidate names a `.lip` identity this "
                      "unit does not own")
            # Only a resolved candidate declares a `dependencies` row; the mp3-first rule means a
            # resolved `.mp3` always wins over a `.wav` at the same stem, so it alone carries the
            # `resolution` marker (the shadowed row's own `sourcePath` is the `.wav` convention
            # path either way, matching `seam_map_scene.md`'s worked example).
            if mp3_resolved:
                expected[("sound", mp3_asset)] = {
                    "sourcePath": wav_path, "resolved": True, "resolution": "mp3-first",
                }
            elif wav_resolved:
                expected[("sound", wav_asset)] = {"sourcePath": wav_path, "resolved": True}
            if candidate["scene"]["resolved"]:
                expected[("scene", scene_asset)] = {"sourcePath": vcd_path, "resolved": True}

    _require(
        declared.keys() == expected.keys(),
        "sound/scene dependencies disagree with the resolved audio candidates",
    )
    for dep_key, expected_row in expected.items():
        actual_row = declared[dep_key]
        for field, value in expected_row.items():
            _require(
                actual_row.get(field) == value,
                f"dependency {dep_key}: {field} disagrees with the resolved audio candidate",
            )
        if "resolution" not in expected_row:
            _require(
                "resolution" not in actual_row,
                f"dependency {dep_key}: an unshadowed resolution carries a stray resolution key",
            )


def _check_independent_decode(root: dict[str, Any], source_members: Sequence[SourceMember]) -> None:
    """Re-decode the selected `.dlg` member through this format's own decoder -- never the writer
    -- and compare every published cell text, offset, length, role, marker, stage direction and
    expression token, plus `coverage.typedUnidentified`/`unresolved`/`omittedProven`, against a
    fresh decode of the source bytes, so a document tampered with after decoding is caught rather
    than only re-checked against itself.

    The audio join is not re-derived here: it needs the whole install's sound/scene resolution,
    which a standalone `.dlg` member does not carry, so `resolved` is stubbed to always miss and
    `lines`/`expressions`/`anomalies`/`omissions` -- none of which the audio join can influence --
    are the ones compared. `_recheck_audio` is the audio join's own independent check, run against
    the document's internal agreement between its `audio` and `dependencies` blocks.
    """

    from elysium_pipeline.formats.dialogue_glb.decode import decode_dialogue
    from elysium_pipeline.formats.dialogue_glb.source import DialogueSourceClosure

    member = next((entry for entry in source_members if entry.role == "dlg"), None)
    _require(member is not None, "prepublication source members omit this unit's .dlg member")

    identity = root.get("identity") or {}
    asset = str(identity.get("asset", ""))
    key = normalize_dlg_key(str(identity.get("sourcePath", "")))
    closure = DialogueSourceClosure(key=key, asset=asset, member=member, resolved=lambda _path: False)
    model = decode_dialogue(closure)

    _require(list(model.lines) == list(root.get("lines") or []),
              "lines disagree with an independent re-decode of the source member")
    _require(list(model.expressions) == list(root.get("expressions") or []),
              "expressions disagree with an independent re-decode of the source member")
    _require(list(model.anomalies) == list(root.get("anomalies") or []),
              "anomalies disagree with an independent re-decode of the source member")
    _require(list(model.omissions) == list(root.get("omissions") or []),
              "omissions disagree with an independent re-decode of the source member")

    published_coverage = root.get("coverage") or {}
    for key_name in ("typedUnidentified", "unresolved", "omittedProven"):
        _require(
            list(model.coverage.get(key_name) or []) == list(published_coverage.get(key_name) or []),
            f"coverage.{key_name} disagrees with an independent re-decode of the source member",
        )


def validate_document(
    document: dict[str, Any],
    binary: bytes,
    *,
    source_members: Sequence[SourceMember] | None = None,
) -> dict[str, Any]:
    try:
        root = validate_extension_root(
            document, DIALOGUE_EXTENSION, asset_prefix=ASSET_PREFIX, schema_version=SCHEMA_VERSION
        )
        validate_container(document, binary)
        validate_sceneless(document)
        validate_ledgers(root, source_members)
        validate_capsules(document, binary, root, source_members)
        # `validate_capsules` returns early when `sourceResolution.capsule` is absent -- correct
        # for a seam that has not adopted the capsule, but dialogue (schema 1.1.0) has, so the
        # declaration itself is required here rather than merely honoured when present.
        _require(
            declares_capsule(root.get("sourceResolution")),
            "a dialogue unit declares no source capsule",
        )
        # The BIN chunk is the capsule and nothing else: a dialogue unit owns no accessor, so any
        # buffer view it declares is one `encapsulate` wrote for a member.
        _require(not document.get("accessors"), "a dialogue unit declares no accessor")
    except UnitValidationError as error:
        raise DialogueGlbValidationError(str(error)) from error

    identity = root["identity"]
    asset = str(identity["asset"])
    source_path = str(identity.get("sourcePath", ""))
    key = normalize_dlg_key(source_path)
    _require(asset == ASSET_PREFIX + key, "identity disagrees with its own source path")
    _require(root.get("encoding") == "latin-1", "a dialogue unit's encoding is not latin-1")

    for row in root.get("dependencies") or []:
        _require(
            row.get("role") in ("sound", "scene"),
            f"a dependencies row names an unexpected role {row.get('role')!r}",
        )

    lines = root.get("lines")
    is_empty_member = any(
        row.get("role") == "empty-member" for row in root.get("omissions") or []
    )
    if is_empty_member:
        _require(isinstance(lines, list) and not lines, "an empty-member unit publishes no lines")
    else:
        _require(isinstance(lines, list) and bool(lines), "a dialogue unit publishes at least one line")
    for line in lines:
        _require(isinstance(line.get("fields"), list), f"line {line.get('index')}: no fields[]")
        _require(len(line["fields"]) == FIELD_COUNT or any(
            row.get("role") == "field-count-mismatch" and row.get("line") == line.get("index")
            for row in root.get("anomalies") or []
        ), f"line {line.get('index')}: field count disagrees and carries no anomaly")

    published_anomalies = root.get("anomalies") or []
    published_coverage = root.get("coverage") or {}
    published_unresolved = published_coverage.get("unresolved") or []
    published_typed_unidentified = published_coverage.get("typedUnidentified") or []
    for line in lines:
        _recheck_line(line, published_anomalies, published_unresolved, published_typed_unidentified)
    _recheck_expressions(root)
    _recheck_links(root)
    _recheck_audio(root, key)
    _recheck_omitted_proven(root)
    if source_members is not None:
        _check_independent_decode(root, source_members)

    return {
        "asset": asset,
        "key": key,
        "lines": len(lines),
        "expressions": len(root.get("expressions") or []),
        "dependencies": len(root.get("dependencies") or []),
        "anomalies": [str(row.get("role")) for row in root.get("anomalies") or []],
        "omissions": [str(row.get("reason") or row.get("role")) for row in root.get("omissions") or []],
        "coverage": completeness(root),
    }


def validate(path: Path) -> dict[str, Any]:
    document, binary = read_glb(path)
    return validate_document(document, binary)


def warnings_for(summary: dict[str, Any]) -> list[str]:
    warnings: list[str] = []
    coverage = summary.get("coverage") or {}
    for key, label in (
        ("unresolved", "unresolved"),
        ("unsupported", "unsupported"),
        ("typedUnidentified", "typed but unidentified"),
    ):
        count = coverage.get(key) or 0
        if count:
            warnings.append(f"{count} {label} row(s)")
    for reason in summary.get("omissions") or []:
        warnings.append(f"omitted: {reason}")
    anomalies = summary.get("anomalies") or []
    if anomalies:
        from collections import Counter

        counted = ", ".join(f"{role}x{count}" for role, count in sorted(Counter(anomalies).items()))
        warnings.append(f"the source departs from the format's conventions: {counted}")
    return warnings
