"""Decode one `.dlg` source closure into its complete model plus a gapless byte ledger.

`docs/architecture/seam_map_dialogue.md` owns the rules this module implements literally: the
row/cell shape, the column schema, the marker classifications, the audio-join path convention and
the named anomalies. Nothing here resolves a `dlgexpr` condition or action -- only the runtime
normalizer does that -- this module only segments and carries it.
"""

from __future__ import annotations

from typing import Any

from elysium_pipeline.formats.dialogue_glb import exprlex, lexer, rules
from elysium_pipeline.formats.dialogue_glb.coverage import SequentialClaimer, finish_coverage, new_ledger
from elysium_pipeline.formats.dialogue_glb.model import (
    AUDIO_LANGUAGES,
    FIELD_COUNT,
    RESERVED_COLUMNS,
    ROLE_NPC_LINE,
    ROLE_PC_CHOICE,
    DialogueModel,
    audio_dir_for,
)
from elysium_pipeline.formats.dialogue_glb.source import DialogueSourceClosure
from elysium_pipeline.formats.unit_contract import asset_id as _stable_asset_id
from elysium_pipeline.formats.unit_contract import dependency

#: The seam doc's own literal naming convention for the whole audio family at one line -- all
#: four sibling extensions exist at this path, even though this unit's own join (`candidates[]`
#: below) only ever names `.mp3`/`.wav`/`.vcd`: the `.lip` phoneme document is not this unit's
#: reference to resolve, so `pathTemplate` documents the convention without the join naming it.
_AUDIO_PATH_TEMPLATE = (
    "sound/character/dlg/<hub>/<dlg-stem>/line<id>_col_<lang>.{mp3,wav,lip,vcd}"
)


def _cell_text(row_cells, index: int) -> str:
    return row_cells[index].text if index < len(row_cells) else ""


def _cell_offset(row_cells, index: int, fallback: int) -> int:
    return row_cells[index].content_offset if index < len(row_cells) else fallback


def _non_latin1_anomalies(
    text: str, base_offset: int, *, line: int | None = None, column: int | None = None
) -> list[dict[str, Any]]:
    """Every C1-range byte (`0x80`-`0x9F`) in `text`, as a `non-latin1-byte` anomaly row.

    Scans the raw span handed to it, not only well-formed cell content, so a C1 byte in a
    malformed cell or in the trailing region is flagged too, matching the seam's own row
    definition (a byte the file's own encoding does not assign, carried raw, no qualification
    on where it sits).
    """

    rows: list[dict[str, Any]] = []
    for local, ch in enumerate(text):
        if 0x80 <= ord(ch) <= 0x9F:
            row: dict[str, Any] = {"role": "non-latin1-byte", "sourceOffset": base_offset + local}
            if line is not None:
                row["line"] = line
            if column is not None:
                row["column"] = column
            row["byte"] = ord(ch)
            rows.append(row)
    return rows


def decode_dialogue(closure: DialogueSourceClosure) -> DialogueModel:
    member = closure.member
    data = member.data
    text = lexer.decode_text(data)
    rows, trailing_start = lexer.split_rows(text)

    ledger = new_ledger(member.path, data)
    # `.dlg` rows are walked strictly in offset order, so every claim below can use the O(1)
    # sequential fast path instead of `ByteLedger.claim()`'s O(existing-ranges) overlap scan --
    # see `SequentialClaimer`.
    claimer = SequentialClaimer(ledger)
    anomalies: list[dict[str, Any]] = []
    typed_unidentified: list[dict[str, Any]] = []
    unresolved: list[dict[str, Any]] = []
    # The contract's empty-member rule: a zero-byte `.dlg` publishes with a zero-length ledger
    # row and this omission -- rather than failing the "at least one line" rule -- and the unit
    # carries no lines, no expressions and no audio join to run.
    omissions: list[dict[str, Any]] = [{"role": "empty-member"}] if not data else []
    expressions: list[dict[str, Any]] = []
    lines: list[dict[str, Any]] = []
    dependencies: list[dict[str, Any]] = []
    seen_dependencies: set[tuple[str, str]] = set()
    audio_lines: list[dict[str, Any]] = []
    id_index: dict[int, int] = {}

    def depend(role: str, asset: str, source_path: str, resolved: bool, **optional: Any) -> None:
        key = (role, asset)
        if key in seen_dependencies:
            return
        seen_dependencies.add(key)
        dependencies.append(dependency(role, asset, source_path, resolved, **optional))

    for row in rows:
        cells = row.cells
        if len(cells) != FIELD_COUNT:
            anomalies.append(
                {
                    "role": "field-count-mismatch",
                    "sourceOffset": row.offset,
                    "line": row.index,
                    "count": len(cells),
                }
            )

        fields_out: list[dict[str, Any]] = []
        for cell in cells:
            record = {
                "index": cell.index,
                "sourceOffset": cell.content_offset,
                "length": cell.content_length,
                "text": cell.text,
            }
            if not cell.well_formed:
                record["malformed"] = True
                anomalies.append(
                    {
                        "role": "malformed-cell",
                        "sourceOffset": cell.offset,
                        "line": row.index,
                        "column": cell.index,
                    }
                )
            fields_out.append(record)

            if cell.well_formed:
                # An empty cell shares its one TAB between `open` and `close` (`{` TAB `}`, three
                # bytes, not four), so `close` is whatever is left after `open` and the content --
                # one byte (`}` alone) when empty, two (TAB `}`) otherwise -- never a fixed count.
                close_offset = cell.content_offset + cell.content_length
                close_length = cell.offset + cell.length - close_offset
                # A dialogue unit has no BIN payload and no binary record -- `mapped` grades a
                # binary decode -- so the cell's `{`/TAB framing bytes, like its content, are
                # graded `mapped-text`.
                claimer.claim(
                    cell.offset, 2, "mapped-text", f"lines[{row.index}].fields[{cell.index}].open"
                )
                claimer.claim(
                    cell.content_offset,
                    cell.content_length,
                    "mapped-text",
                    f"lines[{row.index}].fields[{cell.index}].content",
                )
                claimer.claim(
                    close_offset,
                    close_length,
                    "mapped-text",
                    f"lines[{row.index}].fields[{cell.index}].close",
                )
                anomalies.extend(
                    _non_latin1_anomalies(
                        cell.text, cell.content_offset, line=row.index, column=cell.index
                    )
                )
            else:
                claimer.claim(
                    cell.offset,
                    cell.length,
                    "mapped-text",
                    f"lines[{row.index}].fields[{cell.index}].malformed",
                )
                anomalies.extend(
                    _non_latin1_anomalies(
                        cell.text, cell.content_offset, line=row.index, column=cell.index
                    )
                )
        if row.has_line_break:
            claimer.claim(
                row.offset + row.length, 2, "mapped-text", f"lines[{row.index}].lineBreak"
            )

        id_text = _cell_text(cells, 0).strip()
        id_val: int | None
        try:
            id_val = int(id_text)
        except ValueError:
            id_val = None
            unresolved.append(
                {"line": row.index, "field": "id", "text": id_text, "reason": "non-integer-line-id"}
            )

        text_male = _cell_text(cells, 1)
        text_female = _cell_text(cells, 2)
        text_malkavian = _cell_text(cells, 12)
        link_raw = _cell_text(cells, 3)
        condition_raw = _cell_text(cells, 4)
        action_raw = _cell_text(cells, 5)

        role, link_target, link_reason = rules.classify_link(link_raw)
        if link_reason is not None:
            unresolved.append(
                {"line": row.index, "field": "link", "text": link_raw, "reason": link_reason}
            )
        marker = rules.classify_marker(role, text_male)

        stage_directions: list[dict[str, Any]] = []
        for column, col_text in ((1, text_male), (2, text_female), (12, text_malkavian)):
            if column == 12 and len(cells) <= 12:
                continue
            directions, direction_anomalies = rules.stage_directions(
                column, col_text, _cell_offset(cells, column, row.offset)
            )
            stage_directions += directions
            anomalies.extend(direction_anomalies)

        for column in RESERVED_COLUMNS:
            if column >= len(cells):
                continue
            cell = cells[column]
            if not cell.well_formed:
                continue          # already carried as `malformed-cell`; not a reserved-column use
            if cell.text != "":
                typed_unidentified.append(
                    {
                        "line": row.index,
                        "column": column,
                        "text": cell.text,
                        "sourceOffset": cell.content_offset,
                    }
                )
                anomalies.append(
                    {
                        "role": "reserved-column-used",
                        "sourceOffset": cell.content_offset,
                        "line": row.index,
                        "column": column,
                    }
                )
        for column in range(FIELD_COUNT, len(cells)):
            cell = cells[column]
            if not cell.well_formed:
                continue          # already carried as `malformed-cell`
            typed_unidentified.append(
                {
                    "line": row.index,
                    "column": column,
                    "text": cell.text,
                    "sourceOffset": cell.content_offset,
                    "reason": "field-count-mismatch",
                }
            )

        condition_expression = None
        if condition_raw:
            kind = "condition" if role == ROLE_PC_CHOICE else "action"
            condition_expression = len(expressions)
            expressions.append(
                {
                    "line": row.index,
                    "column": 4,
                    "kind": kind,
                    "text": condition_raw,
                    "tokens": exprlex.tokenize_expression(condition_raw),
                }
            )
        action_expression = None
        if action_raw:
            action_expression = len(expressions)
            expressions.append(
                {
                    "line": row.index,
                    "column": 5,
                    "kind": "action",
                    "text": action_raw,
                    "tokens": exprlex.tokenize_expression(action_raw),
                }
            )

        line_record: dict[str, Any] = {
            "index": row.index,
            "sourceOffset": row.offset,
            "length": row.length,
            "fields": fields_out,
            "id": id_val,
            "textMale": text_male,
            "textFemale": text_female,
            "textMalkavian": text_malkavian,
            "stageDirections": stage_directions,
            "link": link_raw,
            "role": role,
        }
        if link_target is not None:
            line_record["linkTarget"] = link_target
        if marker is not None:
            line_record["marker"] = marker
        line_record["condition"] = condition_raw
        line_record["action"] = action_raw
        if condition_expression is not None:
            line_record["conditionExpression"] = condition_expression
        if action_expression is not None:
            line_record["actionExpression"] = action_expression
        lines.append(line_record)

        if id_val is not None:
            if id_val in id_index:
                anomalies.append(
                    {
                        "role": "duplicate-line-id",
                        "sourceOffset": row.offset,
                        "line": row.index,
                        "id": id_val,
                    }
                )
            else:
                id_index[id_val] = row.index

    # Link targets can only be checked once every id is known.
    for line_record in lines:
        if line_record["role"] != ROLE_PC_CHOICE or "linkTarget" not in line_record:
            continue
        target = line_record["linkTarget"]
        if target == 0:
            continue
        target_index = id_index.get(target)
        if target_index is None:
            anomalies.append(
                {
                    "role": "link-to-missing-line",
                    "sourceOffset": line_record["sourceOffset"],
                    "line": line_record["index"],
                    "linkTarget": target,
                }
            )
        elif lines[target_index]["role"] != ROLE_NPC_LINE:
            anomalies.append(
                {
                    "role": "link-to-non-npc-line",
                    "sourceOffset": line_record["sourceOffset"],
                    "line": line_record["index"],
                    "linkTarget": target,
                }
            )

    # The audio join: only NPC lines carry voice; the PC is silent.
    hub_stem = audio_dir_for(closure.key)
    for line_record in lines:
        if line_record["role"] != ROLE_NPC_LINE or line_record["id"] is None:
            continue
        line_id = line_record["id"]
        candidates: list[dict[str, Any]] = []
        for lang in AUDIO_LANGUAGES:
            stem = f"{hub_stem}/line{line_id}_col_{lang}"
            # The `.lip` phoneme document beside a resolved line is not a candidate this unit
            # publishes an identity for: the seam's own audio-join section calls whether it (and
            # `.vcd`) agree with the line's text "a corpus-index check across three units, not a
            # property of this one", and its "four candidate stems" are the four languages, not
            # four file extensions -- so `.lip` is left to the sound unit that owns it.
            mp3_path, wav_path, vcd_path = (
                f"sound/{stem}.mp3", f"sound/{stem}.wav", f"sound/{stem}.vcd",
            )
            mp3_asset = _stable_asset_id("sound", f"{stem}.mp3")
            wav_asset = _stable_asset_id("sound", f"{stem}.wav")
            scene_asset = _stable_asset_id("scene", stem)

            mp3_resolved = closure.resolved(mp3_path)
            wav_resolved = closure.resolved(wav_path)
            vcd_resolved = closure.resolved(vcd_path)

            # A `dependencies` row is declared only for a candidate this join actually resolves
            # (the spec's literal "every candidate the UP-first index resolves"/"every resolved
            # `.vcd`") -- the negative probe across all four languages still lives in this line's
            # own `audio.lines[].candidates[]` below, so an unresolved language/extension combo
            # (most of the corpus, since only `_col_e` usually has a take) is not also published
            # as a dangling `dependencies` identity with nothing behind it.
            #
            # The engine's own mp3-first rule means an `.mp3` that resolves always wins over any
            # `.wav` at the same stem, so `resolution: "mp3-first"` is stamped only on the row
            # that actually records that shadowing -- asset the `.mp3`, sourcePath the `.wav`
            # convention path -- never on a plain, unshadowed `.wav` resolution.
            if mp3_resolved:
                depend("sound", mp3_asset, wav_path, True, resolution="mp3-first")
            elif wav_resolved:
                depend("sound", wav_asset, wav_path, True)
            if vcd_resolved:
                depend("scene", scene_asset, vcd_path, True)

            sound_resolved = mp3_resolved or wav_resolved
            sound_asset = mp3_asset if mp3_resolved else (wav_asset if wav_resolved else None)
            candidates.append(
                {
                    "lang": lang,
                    "sound": {
                        "candidates": [
                            {"asset": mp3_asset, "resolved": mp3_resolved},
                            {"asset": wav_asset, "resolved": wav_resolved},
                        ],
                        "resolved": sound_resolved,
                        "asset": sound_asset,
                    },
                    "scene": {"asset": scene_asset, "resolved": vcd_resolved},
                }
            )
        audio_lines.append({"line": line_record["index"], "id": line_id, "candidates": candidates})

    # `lexer.split_rows` already proved `text[trailing_start:]` whitespace-only under Python's own
    # `str.strip()`: it sweeps any remainder that isn't into one more row (carried above, not
    # here), so a non-empty `trailing_text` can only ever be whitespace here too. The two modules
    # used to spell "whitespace" differently (this one via a fixed `"\r\n\t "` set), which made a
    # lone `\x0b`/`\x0c`/`\x85`/`\xa0` -- whitespace by `str.strip()`'s definition, not by the
    # fixed set's -- fail as an `unresolved` row over a predicate disagreement rather than the
    # content difference the two states exist to distinguish. One predicate, one outcome now.
    trailing_text = text[trailing_start:]
    if trailing_text:
        claimer.claim(trailing_start, len(trailing_text), "omitted-proven", "trailing")
        omissions.append(
            {
                "role": "trailing-whitespace",
                "reason": "bytes after the last row terminator carry no dialogue meaning",
                # Ties this evidence row to the ledger's own `omitted-proven` range: same owner,
                # same source offset, so the two can be cross-checked rather than merely coexist.
                "owner": "trailing",
                "sourceOffset": trailing_start,
            }
        )
        # Whitespace-only does not mean Latin-1-assigned-only: `\x85` is both.
        anomalies.extend(_non_latin1_anomalies(trailing_text, trailing_start))

    coverage = finish_coverage(
        ledger,
        # `identity`, `sourceResolution`, `dependencies` and `coverage` are the contract's own
        # keys, not this seam's semantic output, and are not restated here -- matching
        # `sound_script_glb.coverage.MAPPED_FIELDS`'s own comment on the identical point.
        mapped=["encoding", "lines", "expressions", "audio"],
        typed_unidentified=typed_unidentified,
        # `omissions` is the same evidence-backed list the extension root publishes under its own
        # `omissions` key (the sibling seams that route through this same shared helper -- e.g.
        # `scene_glb`, `sound_script_glb`, `particle_glb` -- do the same); routed here too, so a
        # byte ledger that claims `omitted-proven` ranges is never paired with an empty
        # `coverage.omittedProven`.
        omitted_proven=omissions,
        unresolved=unresolved,
        unsupported=[],
    )

    audio = {"pathTemplate": _AUDIO_PATH_TEMPLATE, "lines": audio_lines}

    return DialogueModel(
        key=closure.key,
        asset=closure.asset,
        source_path=member.path,
        member=member,
        lines=lines,
        expressions=expressions,
        audio=audio,
        dependencies=dependencies,
        anomalies=anomalies,
        omissions=omissions,
        coverage=coverage,
    )
