"""Decode one choreographed-scene `.vcd` unit into its complete model plus a gapless byte ledger.

`lexer.parse_records` supplies the one grammar rule the format has; this module supplies the
meaning -- which keyword opens which kind of record, which per-type fields a `speak` or
`expression` event carries beside its two raw payload strings, and the sound / expression-table
references a scene makes. The unit is scene-less (`docs/architecture/seam_map_scene.md`): there
is no node, no skeleton, no sampled channel, so nothing here ever touches core glTF.
"""

from __future__ import annotations

import re
from typing import Any, Callable

from elysium_pipeline.formats.scene_glb import lexer
from elysium_pipeline.formats.scene_glb.model import (
    EVENT_TYPE_IDS,
    RECOGNISED_UNUSED_TOKENS,
    UNHANDLED_EVENT_TYPES,
    SceneModel,
    asset_id,
    normalize_scene_key,
)
from elysium_pipeline.formats.unit_contract import ByteLedger
from elysium_pipeline.formats.unit_contract import asset_id as _asset_id
from elysium_pipeline.formats.unit_contract import dependency


class SceneDecodeError(RuntimeError):
    """The selected member cannot be read as a choreographed scene."""


#: An event range this far past its own start is authoring garbage, not a long cinematic -- the
#: longest legitimate scene in the corpus runs a little under two minutes. The spec's own
#: "far outside the audio" wording names no threshold this unit can compute without decoding the
#: referenced sound member, which is out of scope for a scene decode; this is the documented
#: substitute (see `specDeviations`).
DEGENERATE_DURATION_SECONDS = 3600.0

VERSION_RE = re.compile(r"choreo\s+version\s+(\d+)", re.IGNORECASE)
_DB_RE = re.compile(r"[-+]?\d+(?:\.\d+)?")


def _word_text(token: lexer.Token) -> str:
    return token.text


def number(text: str | None) -> float | None:
    if text is None:
        return None
    try:
        return float(text.strip().strip('"'))
    except (TypeError, ValueError):
        return None


def int_or_none(text: str | None) -> int | None:
    if text is None:
        return None
    try:
        return int(text.strip().strip('"'))
    except (TypeError, ValueError):
        return None


def atoi(text: str | None) -> int:
    """A C-`atoi`-shaped parse: leading sign and digits, zero when none are found."""

    match = re.match(r"\s*([+-]?\d+)", text or "")
    return int(match.group(1)) if match else 0


def parse_db(text: str | None) -> float | None:
    if not text:
        return None
    match = _DB_RE.search(text)
    return float(match.group(0)) if match else None


def leading_version(tokens: list[lexer.Token]) -> tuple[int | None, lexer.Token | None]:
    """The file's own `// Choreo version N` header, when the first non-whitespace token is one."""

    for token in tokens:
        if token.kind == "whitespace":
            continue
        if token.kind == "comment":
            match = VERSION_RE.match(token.text[2:].strip())
            if match:
                return int(match.group(1)), token
        return None, None
    return None, None


def _claim_words(ledger: ByteLedger, words: list[lexer.Token], owner: str) -> None:
    for word in words:
        ledger.claim(word.offset, word.length, "mapped-text", owner)


def _claim_braces(ledger: ByteLedger, record: lexer.Record, owner: str) -> None:
    if record.open is not None:
        ledger.claim(record.open.offset, record.open.length, "mapped-text", owner)
    if record.close is not None:
        ledger.claim(record.close.offset, record.close.length, "mapped-text", owner)


def _claim_record_verbatim(ledger: ByteLedger, record: lexer.Record, owner: str) -> None:
    """Claim a record and everything nested in it under one flat owner.

    Used for a leaf record that never grows children in well-formed content (`time`, `param`,
    `bonerename`, ...) and for content this decode carries verbatim rather than routing further
    (`extras[]`, an unsupported word): either way, nothing under the record is left unclaimed.
    """

    _claim_words(ledger, record.words, owner)
    if record.open is not None:
        ledger.claim(record.open.offset, record.open.length, "mapped-text", owner)
    for child in record.children:
        _claim_record_verbatim(ledger, child, owner)
    if record.close is not None:
        ledger.claim(record.close.offset, record.close.length, "mapped-text", owner)


def _extras_row(record: lexer.Record, text: str) -> dict[str, Any]:
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


def _mp3_first_candidates(param: str) -> list[str]:
    """The `.mp3`-then-authored candidate order `FUN_10081700` tries, deduplicated."""

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


def decode_scene(
    closure,
    *,
    sound_exists: Callable[[str], bool] | None = None,
    expression_table_exists: Callable[[str], bool] | None = None,
) -> SceneModel:
    """The complete choreographed-scene unit for one source closure.

    `sound_exists` answers whether the install carries `sound/<candidate>`; `expression_table_exists`
    answers whether it carries `expressions/<stem>.vfe` or `.txt`. Without them every reference
    publishes `resolved: false` rather than asserting a target this decode never looked for.
    """

    member = closure.member
    text = lexer.decode_text(member.data)
    tokens = lexer.tokenize(text)
    significant = [token for token in tokens if token.kind in ("word", "open", "close")]
    try:
        records, structural_anomalies = lexer.parse_records(text, significant)
    except lexer.SceneLexError as error:
        raise SceneDecodeError(f"{member.path}: {error}") from error

    ledger = ByteLedger(member.path, member.data)
    anomalies: list[dict[str, Any]] = list(structural_anomalies)
    for token in tokens:
        if token.anomaly:
            anomalies.append({"role": token.anomaly, "offset": token.offset})
    comments: list[dict[str, Any]] = []
    omissions: list[dict[str, Any]] = []
    unresolved: list[dict[str, Any]] = []
    unsupported: list[dict[str, Any]] = []
    dependencies: list[dict[str, Any]] = []
    seen_dependencies: dict[tuple[str, str, str], int] = {}
    script_expressions: list[dict[str, Any]] = []

    def depend(role: str, asset: str, source_path: str, resolved: bool, **extra: Any) -> int:
        key = (role, asset, source_path)
        if key in seen_dependencies:
            return seen_dependencies[key]
        row_index = len(dependencies)
        dependencies.append(dependency(role, asset, source_path, resolved, **extra))
        seen_dependencies[key] = row_index
        return row_index

    # --- the version header, every comment and all insignificant whitespace -------------------

    version, version_token = leading_version(tokens)
    comment_index = 0
    whitespace_bytes = 0
    for token in tokens:
        if token.kind == "whitespace":
            ledger.claim(token.offset, token.length, "omitted-proven", "whitespace")
            whitespace_bytes += token.length
        elif token.kind == "comment":
            if token is version_token:
                ledger.claim(token.offset, token.length, "mapped-text", "header.version")
            else:
                owner = f"comments[{comment_index}]"
                ledger.claim(token.offset, token.length, "mapped-text", owner)
                # A `//` comment's own span runs to (not through) the line's `\n`, so on a CRLF
                # file it carries a trailing `\r`; the ledger claims that byte regardless; the
                # published text drops it as an artifact of the line ending, not the comment.
                comments.append({"offset": token.offset, "text": token.text.rstrip("\r")})
                comment_index += 1
    if version_token is None:
        anomalies.append({"role": "missing-version-line", "offset": 0})

    # --- the actor / channel / event tree, and the trailing footer -----------------------------

    actors: list[dict[str, Any]] = []
    fps: int | None = None
    snap: bool | None = None

    def leaf_extras_or_unsupported(record: lexer.Record, owner_prefix: str, extras: list) -> None:
        keyword = record.keyword
        if keyword in RECOGNISED_UNUSED_TOKENS:
            owner = f"{owner_prefix}.extras[{len(extras)}]"
            _claim_record_verbatim(ledger, record, owner)
            extras.append(_extras_row(record, text))
            return
        owner = f"{owner_prefix}.unsupported[{len(unsupported)}]"
        _claim_record_verbatim(ledger, record, owner)
        unsupported.append({
            "key": keyword, "offset": record.offset,
            "reason": "key-outside-the-scene-vocabulary",
        })

    def decode_event(record: lexer.Record, prefix: str, index: int) -> dict[str, Any]:
        eprefix = f"{prefix}.events[{index}]"
        _claim_words(ledger, record.words, f"{eprefix}.line")
        _claim_braces(ledger, record, f"{eprefix}.braces")
        type_token = record.words[1] if len(record.words) > 1 else None
        name_token = record.words[2] if len(record.words) > 2 else None
        type_text = _word_text(type_token) if type_token is not None else ""
        type_key = type_text.strip().lower()
        type_id = EVENT_TYPE_IDS.get(type_key)
        name = _word_text(name_token) if name_token is not None else ""

        start = end = None
        param: str | None = None
        param2: str | None = None
        fixed_length = False
        sequence_duration = None
        ramp_rows: list[dict[str, Any]] = []
        extras: list[dict[str, Any]] = []

        for child in record.children:
            keyword = child.keyword
            if keyword == "time":
                _claim_record_verbatim(ledger, child, f"{eprefix}.time")
                start = number(_word_text(child.words[1])) if len(child.words) > 1 else None
                end = number(_word_text(child.words[2])) if len(child.words) > 2 else None
            elif keyword == "param":
                _claim_record_verbatim(ledger, child, f"{eprefix}.param")
                param = _word_text(child.words[1]) if len(child.words) > 1 else ""
            elif keyword == "param2":
                _claim_record_verbatim(ledger, child, f"{eprefix}.param2")
                param2 = _word_text(child.words[1]) if len(child.words) > 1 else ""
            elif keyword == "fixedlength":
                _claim_record_verbatim(ledger, child, f"{eprefix}.fixedlength")
                fixed_length = True
            elif keyword == "sequenceduration":
                _claim_record_verbatim(ledger, child, f"{eprefix}.sequenceduration")
                sequence_duration = (
                    number(_word_text(child.words[1])) if len(child.words) > 1 else None
                )
            elif keyword == "event_ramp":
                _claim_record_verbatim(ledger, child, f"{eprefix}.ramp")
                for row_record in child.children:
                    if len(row_record.words) >= 2:
                        ramp_rows.append({
                            "time": number(_word_text(row_record.words[0])),
                            "value": number(_word_text(row_record.words[1])),
                            "offset": row_record.offset,
                        })
                    else:
                        anomalies.append(
                            {"role": "malformed-ramp-row", "offset": row_record.offset}
                        )
            else:
                leaf_extras_or_unsupported(child, eprefix, extras)

        event: dict[str, Any] = {
            "type": type_text,
            "typeId": type_id,
            "name": name,
            "start": start,
            "end": end,
            "param": param,
            "param2": param2,
            "fixedLength": fixed_length,
            "sequenceDuration": sequence_duration,
            "ramp": ramp_rows,
            "extras": extras,
            "offset": record.offset,
        }

        if type_id is None:
            unsupported.append({
                "key": "event", "offset": record.offset, "type": type_text,
                "reason": "unknown-event-type",
            })

        if start is not None and end is not None and end != -1:
            if end < start or (end - start) > DEGENERATE_DURATION_SECONDS:
                anomalies.append({
                    "role": "degenerate-time", "offset": record.offset,
                    "start": start, "end": end,
                })

        if type_key in ("silence", "loud"):
            marker_duration = number(param)
            event["markerDuration"] = marker_duration
            if (
                marker_duration is not None
                and start is not None and end is not None and end != -1
                and round(end - start, 3) != round(marker_duration, 3)
            ):
                anomalies.append({
                    "role": "marker-duration-mismatch", "offset": record.offset,
                    "markerDuration": marker_duration, "duration": end - start,
                })
        elif type_key in ("speak", "bodysound"):
            level = parse_db(param2)
            if type_key == "bodysound":
                level = 80.0 if level is None else max(level, 75.0)
            event["level"] = level
            if param:
                candidates = (
                    _mp3_first_candidates(param) if type_key == "speak"
                    else [param.replace("\\", "/").strip()]
                )
                chosen = None
                for candidate in candidates:
                    check_key = "sound/" + candidate.lower()
                    if sound_exists is not None and sound_exists(check_key):
                        chosen = candidate
                        break
                final = chosen if chosen is not None else candidates[0]
                sound_asset = _asset_id("sound", final)
                extra = {"resolution": "mp3-first"} if type_key == "speak" else {}
                sound_source_path = "sound/" + param.replace("\\", "/")
                event["sound"] = depend(
                    "sound", sound_asset, sound_source_path, chosen is not None, **extra
                )
            else:
                event["sound"] = None
        elif type_key in ("expression", "flexanimation"):
            stem = (param or "").strip()
            event["expressionTable"] = stem
            event["expressionName"] = param2
            if stem:
                table_asset = _asset_id("expression-table", stem)
                resolved = bool(expression_table_exists(stem)) if expression_table_exists else False
                # `expressions/<stem>.vfe` names the compiled table, the selecting member of the
                # `vfe`+`txt` pair an expression-table unit may resolve (see
                # `formats/expression_table_glb/model.py`), whichever of the two the install
                # actually carries.
                table_source_path = f"expressions/{stem.lower()}.vfe"
                depend("expression-table", table_asset, table_source_path, resolved)
        elif type_key in ("gesture", "sequence"):
            event["clipLabel"] = param
        elif type_key == "firetrigger":
            event["trigger"] = atoi(param)
        elif type_key == "python":
            script_expressions.append({"param": param, "param2": param2, "path": eprefix})
            event["expression"] = len(script_expressions) - 1
        elif type_key in ("cameramove", "lookat", "face"):
            event["entityNames"] = [param or "", param2 or ""]

        if type_key in UNHANDLED_EVENT_TYPES:
            event["unhandled"] = True

        return event

    def decode_bone_rename(child: lexer.Record, prefix: str, actor_state: dict[str, Any]) -> None:
        owner = f"{prefix}.boneRenames[{len(actor_state['boneRenames'])}]"
        _claim_record_verbatim(ledger, child, owner)
        frm = _word_text(child.words[1]) if len(child.words) > 1 else ""
        to = _word_text(child.words[2]) if len(child.words) > 2 else ""
        actor_state["boneRenames"].append({"from": frm, "to": to, "offset": child.offset})

    def decode_face_poser_model(child: lexer.Record, prefix: str, actor_state: dict[str, Any]) -> None:
        _claim_record_verbatim(ledger, child, f"{prefix}.facePoserModel")
        actor_state["facePoserModel"] = _word_text(child.words[1]) if len(child.words) > 1 else None

    def decode_channel(
        record: lexer.Record, prefix: str, index: int, actor_state: dict[str, Any]
    ) -> dict[str, Any]:
        nonlocal fps, snap
        cprefix = f"{prefix}.channels[{index}]"
        _claim_words(ledger, record.words, f"{cprefix}.line")
        _claim_braces(ledger, record, f"{cprefix}.braces")
        name_token = record.words[1] if len(record.words) > 1 else None
        name = _word_text(name_token) if name_token is not None else ""
        active = True
        events: list[dict[str, Any]] = []
        extras: list[dict[str, Any]] = []
        for child in record.children:
            keyword = child.keyword
            if keyword == "event":
                events.append(decode_event(child, cprefix, len(events)))
            elif keyword == "active":
                _claim_record_verbatim(ledger, child, f"{cprefix}.active")
                value = _word_text(child.words[1]) if len(child.words) > 1 else "1"
                active = atoi(value) != 0
                if not active:
                    anomalies.append(
                        {"role": "inactive-block", "offset": child.offset, "scope": "channel"}
                    )
            elif keyword == "channel":
                # A channel never nests in a well-formed file; reaching one here means an
                # ancestor channel's own `{ }` never closed (a retail authoring defect: one
                # shipped `.vcd` is short exactly one closing brace) and the grammar's single
                # word-list/brace rule folded the next sibling channel in as a child instead. The
                # channel is still the actor's, so it is decoded and attached there, not dropped.
                # The nested channel's final position is the actor's current channel list length
                # *after* this reservation -- reserve the slot before recursing so the record's
                # own ledger owner path (`channels[N]`) names the index it actually publishes at,
                # even though the enclosing (still-open) channel has not been appended yet.
                anomalies.append({"role": "misnested-channel", "offset": child.offset})
                slot = len(actor_state["channels"])
                actor_state["channels"].append(None)
                actor_state["channels"][slot] = decode_channel(child, prefix, slot, actor_state)
            elif keyword == "bonerename":
                anomalies.append({"role": "misnested-bonerename", "offset": child.offset})
                decode_bone_rename(child, prefix, actor_state)
            elif keyword == "faceposermodel":
                anomalies.append({"role": "misnested-faceposermodel", "offset": child.offset})
                decode_face_poser_model(child, prefix, actor_state)
            elif keyword == "fps":
                # The same missing-brace defect can push the trailing footer under a channel
                # (not just under the last actor) when the unclosed block is a channel's own;
                # `fps`/`snap` are file-scope regardless of where the grammar's brace count
                # happened to land them. Mirrors `decode_actor`'s own recovery branch.
                anomalies.append(
                    {"role": "misnested-footer-token", "offset": child.offset, "token": "fps"}
                )
                _claim_record_verbatim(ledger, child, "footer.fps")
                fps = int_or_none(_word_text(child.words[1])) if len(child.words) > 1 else None
            elif keyword == "snap":
                anomalies.append(
                    {"role": "misnested-footer-token", "offset": child.offset, "token": "snap"}
                )
                _claim_record_verbatim(ledger, child, "footer.snap")
                value = _word_text(child.words[1]).strip().lower() if len(child.words) > 1 else None
                snap = None if value is None else value == "on"
                if snap:
                    anomalies.append({"role": "snap-on", "offset": child.offset})
            else:
                leaf_extras_or_unsupported(child, cprefix, extras)
        return {
            "name": name, "active": active, "events": events, "extras": extras,
            "offset": record.offset,
        }

    def decode_actor(record: lexer.Record, index: int) -> dict[str, Any]:
        nonlocal fps, snap
        prefix = f"actors[{index}]"
        _claim_words(ledger, record.words, f"{prefix}.line")
        _claim_braces(ledger, record, f"{prefix}.braces")
        name_token = record.words[1] if len(record.words) > 1 else None
        name = _word_text(name_token) if name_token is not None else ""
        active = True
        extras: list[dict[str, Any]] = []
        actor_state: dict[str, Any] = {
            "channels": [], "boneRenames": [], "facePoserModel": None,
        }
        for child in record.children:
            keyword = child.keyword
            if keyword == "channel":
                # Reserve the slot before recursing: a misnested channel folded inside this one
                # (a missing-brace defect) reaches `actor_state["channels"]` mid-recursion, so
                # this channel's own eventual index must be fixed before that happens.
                slot = len(actor_state["channels"])
                actor_state["channels"].append(None)
                actor_state["channels"][slot] = decode_channel(child, prefix, slot, actor_state)
            elif keyword == "bonerename":
                decode_bone_rename(child, prefix, actor_state)
            elif keyword == "faceposermodel":
                decode_face_poser_model(child, prefix, actor_state)
            elif keyword == "active":
                _claim_record_verbatim(ledger, child, f"{prefix}.active")
                value = _word_text(child.words[1]) if len(child.words) > 1 else "1"
                active = atoi(value) != 0
                if not active:
                    anomalies.append(
                        {"role": "inactive-block", "offset": child.offset, "scope": "actor"}
                    )
            elif keyword == "fps":
                # The same missing-brace defect can push the trailing footer under the last
                # actor's own (also unclosed) block; `fps`/`snap` are file-scope regardless of
                # where the grammar's brace count happened to land them.
                anomalies.append({"role": "misnested-footer-token", "offset": child.offset, "token": "fps"})
                _claim_record_verbatim(ledger, child, "footer.fps")
                fps = int_or_none(_word_text(child.words[1])) if len(child.words) > 1 else None
            elif keyword == "snap":
                anomalies.append({"role": "misnested-footer-token", "offset": child.offset, "token": "snap"})
                _claim_record_verbatim(ledger, child, "footer.snap")
                value = _word_text(child.words[1]).strip().lower() if len(child.words) > 1 else None
                snap = None if value is None else value == "on"
                if snap:
                    anomalies.append({"role": "snap-on", "offset": child.offset})
            else:
                leaf_extras_or_unsupported(child, prefix, extras)
        return {
            "name": name, "active": active, "facePoserModel": actor_state["facePoserModel"],
            "boneRenames": actor_state["boneRenames"], "channels": actor_state["channels"],
            "extras": extras, "offset": record.offset,
        }

    for record in records:
        keyword = record.keyword
        if keyword == "actor":
            actors.append(decode_actor(record, len(actors)))
        elif keyword == "fps":
            _claim_record_verbatim(ledger, record, "footer.fps")
            fps = int_or_none(_word_text(record.words[1])) if len(record.words) > 1 else None
        elif keyword == "snap":
            _claim_record_verbatim(ledger, record, "footer.snap")
            value = _word_text(record.words[1]).strip().lower() if len(record.words) > 1 else None
            snap = None if value is None else value == "on"
            if snap:
                anomalies.append({"role": "snap-on", "offset": record.offset})
        else:
            owner = f"file.unsupported[{len(unsupported)}]"
            _claim_record_verbatim(ledger, record, owner)
            unsupported.append({
                "key": keyword, "offset": record.offset,
                "reason": "key-outside-the-scene-vocabulary",
            })

    if whitespace_bytes:
        omissions.append({
            "role": "insignificant-whitespace",
            "reason": "blank lines and indentation carry no payload byte",
            "bytes": whitespace_bytes,
        })

    if member.byte_length == 0:
        omissions.append({"role": "empty-member", "reason": "the member holds zero bytes"})

    byte_ledger_row = ledger.finish()

    key = normalize_scene_key(closure.key)
    return SceneModel(
        key=key,
        asset_id=asset_id(key),
        source_path=member.path,
        member=member,
        version=version,
        fps=fps,
        snap=snap,
        actors=actors,
        script_expressions=script_expressions,
        dependencies=dependencies,
        comments=comments,
        anomalies=anomalies,
        omissions=omissions,
        unresolved=unresolved,
        unsupported=unsupported,
        byte_ledger=[byte_ledger_row],
    )
