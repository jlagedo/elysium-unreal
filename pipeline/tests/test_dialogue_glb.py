"""Contract tests for the Dialogue GLB seam (`formats.dialogue_glb`, `exporters.dialogue_glb`,
`validation.dialogue_glb`). Every fixture is synthetic bytes built in this module; nothing here
touches the real VtMB install.
"""

from __future__ import annotations

from pathlib import PurePosixPath

import pytest

from elysium_pipeline.exporters import dialogue_glb as exporter
from elysium_pipeline.formats.dialogue_glb import exprlex, rules
from elysium_pipeline.formats.dialogue_glb import source as dlg_source
from elysium_pipeline.formats.dialogue_glb.decode import decode_dialogue
from elysium_pipeline.formats.dialogue_glb.model import (
    DIALOGUE_EXTENSION,
    ROLE_NPC_LINE,
    ROLE_PADDING,
    ROLE_PC_CHOICE,
    SCHEMA_VERSION,
    asset_id,
    normalize_dlg_key,
    output_relative_path,
    source_path_for,
)
from elysium_pipeline.formats.dialogue_glb.source import DialogueSourceClosure
from elysium_pipeline.formats.unit_contract import Origin, SourceMember, verify_ledger_row
from elysium_pipeline.formats.unit_contract.validate import ROOT_KEYS
from elysium_pipeline.validation import dialogue_glb as validation
from elysium_pipeline.validation.dialogue_glb import DialogueGlbValidationError

# --- fixture helpers -----------------------------------------------------------------------


def cell(text: str) -> str:
    return "{\t" + text + "\t}"


def row13(fields: list[str]) -> str:
    assert len(fields) == 13
    return "".join(cell(field) for field in fields)


def make_row(
    id_: str = "1", male: str = "", female: str = "", link: str = "", cond: str = "",
    action: str = "", col6: str = "", col7: str = "", col8: str = "", col9: str = "",
    col10: str = "", col11: str = "", malk: str = "",
) -> str:
    return row13([id_, male, female, link, cond, action, col6, col7, col8, col9, col10, col11, malk])


def dlg_text(rows: list[str]) -> str:
    return "\r\n".join(rows) + "\r\n"


def make_closure(text: str, *, key: str = "hub/stem", resolved: frozenset = frozenset()) -> DialogueSourceClosure:
    data = text.encode("latin-1")
    folded = normalize_dlg_key(key)
    member = SourceMember(
        role="dlg", path=source_path_for(folded), data=data,
        origin=Origin(kind="loose", root="Unofficial_Patch"),
    )
    return DialogueSourceClosure(
        key=folded, asset=asset_id(folded), member=member, resolved=lambda p: p in resolved
    )


# --- lexer -----------------------------------------------------------------------------------


def test_a_well_formed_row_splits_into_thirteen_cells_with_content_offsets():
    from elysium_pipeline.formats.dialogue_glb import lexer

    text = dlg_text([make_row(id_="10", male="Hi", link="#")])
    rows, trailing_start = lexer.split_rows(text)
    assert len(rows) == 1
    row = rows[0]
    assert len(row.cells) == 13
    assert all(field.well_formed for field in row.cells)
    assert row.cells[0].text == "10" and row.cells[1].text == "Hi"
    # The content offset always points exactly two bytes past the cell's own `{` TAB.
    assert all(field.content_offset == field.offset + 2 for field in row.cells)
    assert trailing_start == len(text)


def test_a_malformed_cells_whole_span_is_still_claimable_but_its_text_sheds_the_broken_wrapper():
    """A malformed cell's `offset`/`length` still name its whole raw span (so the caller still
    ledger-claims every one of its bytes), but a leading `{`+TAB and a trailing TAB?+`}` -- the
    recognisable pieces of the broken wrapper -- are stripped from the published `text` rather
    than carried into it verbatim; `well_formed` (and the `malformed-cell` anomaly it drives) is
    what records the departure."""

    from elysium_pipeline.formats.dialogue_glb import lexer

    fields = ["50", "Text male", "", "#", "", "", "", "", "", "", "", "", ""]
    broken = "".join(
        cell(value) if index != 2 else "{BROKEN}" for index, value in enumerate(fields)
    )
    text = dlg_text([broken])
    rows, _ = lexer.split_rows(text)
    cells = rows[0].cells
    assert len(cells) == 13
    assert not cells[2].well_formed
    assert cells[2].length == len("{BROKEN}")
    assert cells[2].text == "{BROKEN"
    assert cells[2].content_length == len(cells[2].text)
    assert cells[0].well_formed and cells[0].text == "50"


def test_a_fourteen_cell_row_is_split_intact():
    from elysium_pipeline.formats.dialogue_glb import lexer

    fields = ["40", "M", "F", "#", "", "", "", "", "", "", "", "", "", "EXTRA"]
    text = dlg_text(["".join(cell(f) for f in fields)])
    rows, _ = lexer.split_rows(text)
    assert len(rows[0].cells) == 14
    assert rows[0].cells[13].text == "EXTRA"


def test_trailing_bytes_start_exactly_after_the_last_terminator():
    from elysium_pipeline.formats.dialogue_glb import lexer

    text = dlg_text([make_row(id_="1", link="#")]) + "   "
    rows, trailing_start = lexer.split_rows(text)
    assert len(rows) == 1
    assert text[trailing_start:] == "   "


# --- rules (pure classification) --------------------------------------------------------------


def test_link_classification_covers_padding_npc_and_pc_choice():
    assert rules.classify_link("") == (ROLE_PADDING, None, None)
    assert rules.classify_link("  ") == (ROLE_PADDING, None, None)
    assert rules.classify_link("#") == (ROLE_NPC_LINE, None, None)
    assert rules.classify_link("87") == (ROLE_PC_CHOICE, 87, None)
    assert rules.classify_link("0") == (ROLE_PC_CHOICE, 0, None)
    role, target, reason = rules.classify_link("??")
    assert role == ROLE_PADDING and target is None and reason is not None


def test_marker_classification_recognises_auto_rows_and_starting_condition():
    assert rules.classify_marker(ROLE_PC_CHOICE, "(Auto-Link)") == "auto-link"
    assert rules.classify_marker(ROLE_PC_CHOICE, "  (auto-end)  ") == "auto-end"
    assert rules.classify_marker(ROLE_PC_CHOICE, "Keep going") is None
    assert rules.classify_marker(ROLE_NPC_LINE, "This is the Starting Condition line") == (
        "starting-condition"
    )
    assert rules.classify_marker(ROLE_NPC_LINE, "starting_condition here") == "starting-condition"
    assert rules.classify_marker(ROLE_NPC_LINE, "an ordinary line") is None


def test_stage_directions_finds_every_bracket_pair_and_flags_the_unterminated_one():
    directions, anomalies = rules.stage_directions(1, "Hi [chuckle] there [pause] ok", 100)
    assert [d["text"] for d in directions] == ["[chuckle]", "[pause]"]
    assert directions[0]["sourceOffset"] == 100 + 3
    assert anomalies == []
    directions, anomalies = rules.stage_directions(1, "Wait [unfinished", 0)
    assert directions == []
    assert anomalies == [{"role": "unterminated-stage-direction", "sourceOffset": 5}]


# --- exprlex (dlgexpr tokenization) -----------------------------------------------------------


def test_a_bare_skill_check_uses_the_implicit_relop():
    tokens = exprlex.tokenize_expression("Persuasion 7")
    assert len(tokens) == 1
    token = tokens[0]
    assert token["kind"] == "skill-check"
    assert token["skill"] == "Persuasion"
    assert token["sexGate"] is None
    assert token["relop"] == ">="
    assert token["implicitRelop"] is True
    assert token["threshold"] == "7"
    assert token["text"] == "Persuasion 7"


def test_a_skill_check_with_an_explicit_relop_and_a_sex_gate():
    tokens = exprlex.tokenize_expression("M_Persuasion >= 3")
    assert len(tokens) == 1
    token = tokens[0]
    assert token["skill"] == "Persuasion"
    assert token["sexGate"] == "male"
    assert token["relop"] == ">="
    assert token["implicitRelop"] is False
    assert token["threshold"] == "3"

    tokens = exprlex.tokenize_expression("F_Seduction == 5")
    assert tokens[0]["sexGate"] == "female" and tokens[0]["skill"] == "Seduction"


def test_condition_joiners_split_the_expression_into_named_segments():
    tokens = exprlex.tokenize_expression("Humanity 5 & G.Foo == 1")
    kinds = [t["kind"] for t in tokens]
    # The space either side of `&` is not part of the skill-check or the join token itself, so it
    # surfaces as its own (whitespace) python-expression segment -- gapless, nothing dropped.
    assert kinds == ["skill-check", "python-expression", "join", "python-expression"]
    assert tokens[1]["text"] == " "
    assert tokens[2]["symbol"] == "&"
    assert tokens[3]["text"] == " G.Foo == 1"


def test_the_action_separator_splits_statements_without_touching_a_skill_check_call():
    tokens = exprlex.tokenize_expression("G.Bar = 2; G.Baz.Call()")
    kinds = [t["kind"] for t in tokens]
    assert kinds == ["python-expression", "separator", "python-expression"]
    assert tokens[1]["symbol"] == ";"


def test_a_dotted_or_called_identifier_is_never_mistaken_for_a_skill_check():
    tokens = exprlex.tokenize_expression("pc.CalcFeat(3)")
    assert [t["kind"] for t in tokens] == ["python-expression"]
    assert tokens[0]["text"] == "pc.CalcFeat(3)"


def test_tokenization_is_gapless_over_the_raw_cell():
    text = ' Humanity 5 & M_Persuasion 3 ; G.Foo = "a & b" '
    tokens = exprlex.tokenize_expression(text)
    cursor = 0
    for token in tokens:
        assert token["offset"] == cursor
        assert text[token["offset"]:token["offset"] + token["length"]] == token["text"]
        cursor += token["length"]
    assert cursor == len(text)


# --- decode: role, marker, expressions --------------------------------------------------------


def test_decode_classifies_every_role_and_carries_raw_and_derived_fields():
    text = dlg_text(
        [
            make_row(id_="10", male="Starting condition: hi [wave]", link="#", cond="G.Foo = 1"),
            make_row(id_="21", male="Continue", link="11", cond="Persuasion 7"),
            make_row(id_="11", male="Alright.", link="#"),
            make_row(id_="22", male="(Auto-End)", link="0"),
            make_row(id_="1", male="", link=""),
        ]
    )
    closure = make_closure(text)
    model = decode_dialogue(closure)
    by_id = {line["id"]: line for line in model.lines}

    assert by_id[10]["role"] == ROLE_NPC_LINE
    assert by_id[10]["marker"] == "starting-condition"
    assert by_id[10]["stageDirections"][0]["text"] == "[wave]"
    assert by_id[10]["conditionExpression"] is not None
    assert model.expressions[by_id[10]["conditionExpression"]]["kind"] == "action"

    assert by_id[21]["role"] == ROLE_PC_CHOICE and by_id[21]["linkTarget"] == 11
    assert model.expressions[by_id[21]["conditionExpression"]]["kind"] == "condition"
    assert model.expressions[by_id[21]["conditionExpression"]]["tokens"][0]["kind"] == "skill-check"

    assert by_id[11]["role"] == ROLE_NPC_LINE
    assert by_id[22]["role"] == ROLE_PC_CHOICE and by_id[22]["marker"] == "auto-end"
    assert by_id[1]["role"] == ROLE_PADDING
    assert "marker" not in by_id[1]
    assert "linkTarget" not in by_id[1]


def test_a_whitespace_only_condition_or_action_cell_is_still_non_empty_and_gets_an_expression():
    """`expressions[]` covers every non-empty column-4 and column-5 cell, tokenized -- a cell
    holding a single space is non-empty by that wording even though it is
    meaningless once stripped, so it still publishes a record."""

    text = dlg_text([make_row(id_="1", male="Hi", link="#", cond=" ", action=" ")])
    model = decode_dialogue(make_closure(text))
    line = model.lines[0]
    assert line["conditionExpression"] is not None
    assert line["actionExpression"] is not None
    assert model.expressions[line["conditionExpression"]]["text"] == " "
    assert model.expressions[line["actionExpression"]]["text"] == " "
    assert model.coverage["unresolved"] == []
    assert model.coverage["unsupported"] == []


def test_duplicate_line_ids_are_flagged_without_losing_either_row():
    text = dlg_text(
        [make_row(id_="10", male="First", link="#"), make_row(id_="10", male="Second", link="#")]
    )
    model = decode_dialogue(make_closure(text))
    assert len(model.lines) == 2
    roles = [a["role"] for a in model.anomalies]
    assert "duplicate-line-id" in roles
    dup = next(a for a in model.anomalies if a["role"] == "duplicate-line-id")
    assert dup["line"] == 1 and dup["id"] == 10


def test_a_link_to_a_missing_line_is_an_anomaly_not_a_crash():
    text = dlg_text([make_row(id_="23", male="Nonsense", link="999")])
    model = decode_dialogue(make_closure(text))
    assert any(a["role"] == "link-to-missing-line" and a["linkTarget"] == 999
               for a in model.anomalies)


def test_a_link_to_a_non_npc_line_is_flagged():
    text = dlg_text(
        [
            make_row(id_="21", male="Choice A", link="0"),
            make_row(id_="24", male="Loops to a PC row", link="21"),
        ]
    )
    model = decode_dialogue(make_closure(text))
    assert any(a["role"] == "link-to-non-npc-line" and a["linkTarget"] == 21
               for a in model.anomalies)


def test_a_link_to_zero_ends_the_conversation_without_any_link_anomaly():
    text = dlg_text([make_row(id_="22", male="Bye", link="0")])
    model = decode_dialogue(make_closure(text))
    assert not any(a["role"].startswith("link-to-") for a in model.anomalies)


def test_a_non_integer_link_is_unresolved_not_guessed_at():
    text = dlg_text([make_row(id_="5", male="Odd", link="??")])
    model = decode_dialogue(make_closure(text))
    assert model.lines[0]["role"] == ROLE_PADDING
    assert any(row["reason"] == "link-neither-hash-nor-integer-nor-empty"
               for row in model.coverage["unresolved"])


def test_a_non_integer_line_id_is_unresolved_and_the_id_is_null():
    text = dlg_text([make_row(id_="ABC", male="Weird", link="#")])
    model = decode_dialogue(make_closure(text))
    assert model.lines[0]["id"] is None
    assert any(row["reason"] == "non-integer-line-id" for row in model.coverage["unresolved"])


def test_a_reserved_column_left_empty_is_silent():
    text = dlg_text([make_row(id_="30", male="Clean", link="#")])
    model = decode_dialogue(make_closure(text))
    assert model.coverage["typedUnidentified"] == []
    assert not any(a["role"] == "reserved-column-used" for a in model.anomalies)


def test_a_non_empty_reserved_column_becomes_typed_unidentified_and_an_anomaly():
    text = dlg_text([make_row(id_="30", male="Reserved test", link="#", col6="SPARE")])
    model = decode_dialogue(make_closure(text))
    typed = model.coverage["typedUnidentified"]
    assert len(typed) == 1 and typed[0]["column"] == 6 and typed[0]["text"] == "SPARE"
    assert any(a["role"] == "reserved-column-used" and a["column"] == 6 for a in model.anomalies)


def test_a_whitespace_only_reserved_column_is_still_typed_unidentified():
    """The reserved-column rule is literal non-empty, matching the identical rule for
    `expressions[]`'s column-4/5 cells: a lone space is a use of the column, not proven-empty."""

    text = dlg_text([make_row(id_="31", male="Space test", link="#", col6=" ")])
    model = decode_dialogue(make_closure(text))
    typed = model.coverage["typedUnidentified"]
    assert len(typed) == 1 and typed[0]["column"] == 6 and typed[0]["text"] == " "
    assert any(a["role"] == "reserved-column-used" and a["column"] == 6 for a in model.anomalies)


def test_a_fourteen_cell_row_is_a_field_count_mismatch_with_its_extra_cell_carried():
    fields = ["40", "M", "F", "#", "", "", "", "", "", "", "", "", "", "EXTRA"]
    text = dlg_text(["".join(cell(f) for f in fields)])
    model = decode_dialogue(make_closure(text))
    assert any(a["role"] == "field-count-mismatch" and a["count"] == 14 for a in model.anomalies)
    typed = model.coverage["typedUnidentified"]
    assert any(row["column"] == 13 and row["text"] == "EXTRA" for row in typed)
    assert len(model.lines[0]["fields"]) == 14


def test_a_malformed_cell_is_carried_and_flagged_without_dropping_the_row():
    fields = ["50", "Text male", "", "#", "", "", "", "", "", "", "", "", ""]
    broken = "".join(cell(v) if i != 2 else "{BROKEN}" for i, v in enumerate(fields))
    text = dlg_text([broken])
    model = decode_dialogue(make_closure(text))
    assert model.lines[0]["id"] == 50
    assert model.lines[0]["fields"][2]["malformed"] is True
    assert any(a["role"] == "malformed-cell" and a["column"] == 2 for a in model.anomalies)


def test_an_unterminated_stage_direction_is_flagged_and_not_carried():
    text = dlg_text([make_row(id_="60", male="Wait [unfinished", link="#")])
    model = decode_dialogue(make_closure(text))
    assert model.lines[0]["stageDirections"] == []
    assert any(a["role"] == "unterminated-stage-direction" for a in model.anomalies)


def test_a_c1_range_byte_is_kept_raw_and_flagged_non_latin1():
    text = dlg_text([make_row(id_="70", male="It\x92s a test", link="#")])
    model = decode_dialogue(make_closure(text))
    assert model.lines[0]["textMale"] == "It\x92s a test"
    anomaly = next(a for a in model.anomalies if a["role"] == "non-latin1-byte")
    assert anomaly["byte"] == 0x92


def test_a_c1_range_byte_inside_a_malformed_cell_is_still_flagged():
    """The scan is not gated on `well_formed` -- a C1 byte in a broken cell's own text is just as
    real a departure from the file's declared encoding as one in a clean cell."""

    fields = ["71", "Text male", "", "#", "", "", "", "", "", "", "", "", ""]
    broken = "".join(
        cell(v) if i != 2 else "{BAD\x93}" for i, v in enumerate(fields)
    )
    text = dlg_text([broken])
    model = decode_dialogue(make_closure(text))
    assert model.lines[0]["fields"][2]["malformed"] is True
    anomaly = next(
        a for a in model.anomalies if a["role"] == "non-latin1-byte" and a["column"] == 2
    )
    assert anomaly["byte"] == 0x93


def test_the_non_latin1_scan_helper_omits_line_and_column_when_none_are_given():
    """`lexer.split_rows` sweeps any non-whitespace remainder into one more row (so a C1 byte
    there is already covered by the malformed-cell case above); the `trailing` claim only ever
    sees a genuinely-empty or defensively-incidental remainder. The scan helper itself still
    supports that context-free call, exercised directly here."""

    from elysium_pipeline.formats.dialogue_glb.decode import _non_latin1_anomalies

    rows = _non_latin1_anomalies("A\x94B\x95", 10)
    assert rows == [
        {"role": "non-latin1-byte", "sourceOffset": 11, "byte": 0x94},
        {"role": "non-latin1-byte", "sourceOffset": 13, "byte": 0x95},
    ]


# --- decode: audio join + dependencies ---------------------------------------------------------


def test_the_audio_join_resolves_mp3_first_and_falls_back_to_wav_and_only_npc_lines_carry_it():
    key = "main characters/jack_tutorial"
    hub = "character/dlg/main characters/jack_tutorial"
    resolved = frozenset(
        {
            f"sound/{hub}/line191_col_e.mp3",
            f"sound/{hub}/line191_col_f.wav",
            f"sound/{hub}/line191_col_e.vcd",
        }
    )
    text = dlg_text(
        [
            make_row(id_="191", male="Some subtitle", link="#", action="G.Test = 1"),
            make_row(id_="192", male="A PC choice", link="0"),
        ]
    )
    model = decode_dialogue(make_closure(text, key=key, resolved=resolved))

    sound_rows = [d for d in model.dependencies if d["role"] == "sound"]
    scene_rows = [d for d in model.dependencies if d["role"] == "scene"]
    # Only a candidate this join actually resolves declares a dependency row: `_col_e` resolves
    # as `.mp3` (mp3-first), `_col_f` resolves only as `.wav` (no `.mp3` beside it to shadow it),
    # `_col_m`/`_col_n` resolve as neither -- so exactly two `sound` rows and, since only
    # `_col_e`'s `.vcd` exists, exactly one `scene` row.
    assert len(sound_rows) == 2
    assert len(scene_rows) == 1
    assert all(row["resolved"] for row in sound_rows + scene_rows)
    e_row = next(row for row in sound_rows if row["asset"] == f"vtmb:sound:{hub}/line191_col_e.mp3")
    assert e_row["resolution"] == "mp3-first"
    assert e_row["sourcePath"] == f"sound/{hub}/line191_col_e.wav"
    f_row = next(row for row in sound_rows if row["asset"] == f"vtmb:sound:{hub}/line191_col_f.wav")
    assert "resolution" not in f_row
    assert f_row["sourcePath"] == f"sound/{hub}/line191_col_f.wav"
    assert scene_rows[0]["asset"] == f"vtmb:scene:{hub}/line191_col_e"

    audio_line = next(a for a in model.audio["lines"] if a["id"] == 191)
    by_lang = {c["lang"]: c for c in audio_line["candidates"]}
    assert by_lang["e"]["sound"]["resolved"] is True
    assert by_lang["e"]["sound"]["asset"] == f"vtmb:sound:{hub}/line191_col_e.mp3"
    assert by_lang["f"]["sound"]["resolved"] is True
    assert by_lang["f"]["sound"]["asset"] == f"vtmb:sound:{hub}/line191_col_f.wav"
    assert by_lang["m"]["sound"]["resolved"] is False and by_lang["m"]["sound"]["asset"] is None
    assert by_lang["n"]["sound"]["resolved"] is False
    # `.lip` is not a candidate this unit names an identity for -- it belongs to the sound unit
    # that owns the member, not to the dialogue unit that merely joins to it by path.
    assert "lip" not in by_lang["e"]

    # An unresolved candidate is not an anomaly -- most lines have no _col_m/_col_n take.
    assert not any("audio" in str(a.get("role", "")) for a in model.anomalies)
    # The PC choice (id 192) is silent: it owns no audio row at all.
    assert all(a["id"] != 192 for a in model.audio["lines"])


def test_dependencies_are_never_duplicated_for_the_same_resolved_candidate():
    """Two rows sharing a duplicate line id name the same audio stem; the identity is declared
    once in `dependencies` even though the audio join visits it from two different lines."""

    key = "hub/stem"
    hub = "character/dlg/hub/stem"
    resolved = frozenset({f"sound/{hub}/line10_col_e.mp3"})
    text = dlg_text(
        [make_row(id_="10", male="Hi", link="#"), make_row(id_="10", male="Hi again", link="#")]
    )
    model = decode_dialogue(make_closure(text, key=key, resolved=resolved))
    target = f"vtmb:sound:{hub}/line10_col_e.mp3"
    sound_rows = [d for d in model.dependencies if d["role"] == "sound" and d["asset"] == target]
    assert len(sound_rows) == 1


# --- byte ledger: gaplessness, zero-states, and the shared verifier ----------------------------


def test_records_read_from_a_file_offset_publish_sourceOffset_not_offset():
    """`lines[]`, `fields[]`, `stageDirections[]`, `typedUnidentified[]` and `anomalies[]` all
    name their file position with the contract's own key -- `sourceOffset` -- not a bare
    `offset`, so a ledger range and the record it pays for use the same vocabulary."""

    text = dlg_text(
        [make_row(id_="10", male="Hi [wave]", link="#", col6="SPARE")]
    )
    model = decode_dialogue(make_closure(text))
    line = model.lines[0]
    assert "sourceOffset" in line and "offset" not in line
    for field in line["fields"]:
        assert "sourceOffset" in field and "offset" not in field
    assert line["stageDirections"] and "sourceOffset" in line["stageDirections"][0]
    assert "offset" not in line["stageDirections"][0]
    typed = model.coverage["typedUnidentified"][0]
    assert "sourceOffset" in typed and "offset" not in typed
    for anomaly in model.anomalies:
        assert "offset" not in anomaly


def test_every_byte_of_the_member_is_claimed_exactly_once():
    text = dlg_text(
        [
            make_row(id_="10", male="Hi [wave]", link="#", cond="G.Foo = 1", col6="SPARE"),
            make_row(id_="21", male="Continue", link="0", cond="Persuasion 7"),
        ]
    )
    closure = make_closure(text)
    model = decode_dialogue(closure)
    row = model.coverage["byteLedger"][0]
    assert row["byteLength"] == len(closure.member.data)
    assert row["accountedBytes"] == row["byteLength"]
    assert row["coveragePercent"] == 100.0
    ranges = row["ranges"]
    cursor = 0
    for entry in ranges:
        assert entry["offset"] == cursor
        cursor += entry["length"]
    assert cursor == row["byteLength"]
    verify_ledger_row(row, closure.member.data)


def test_the_dialogue_ledger_never_claims_a_zero_state():
    """The format is pure text with no declared-zero field and no alignment padding, so a
    dialogue ledger has nothing to prove with `reserved-zero`/`padding-zero`; and since the unit
    carries no binary record, every claimed range -- cell framing included -- is `mapped-text`,
    never the binary-record state `mapped`."""

    text = dlg_text([make_row(id_="10", male="Hi", link="#")])
    model = decode_dialogue(make_closure(text))
    states = {entry["state"] for entry in model.coverage["byteLedger"][0]["ranges"]}
    assert states == {"mapped-text"}


def test_whitespace_only_trailing_bytes_are_omitted_proven():
    clean = make_closure(dlg_text([make_row(id_="1", male="Hi", link="#")]) + "  ")
    model = decode_dialogue(clean)
    assert any(o["role"] == "trailing-whitespace" for o in model.omissions)
    row = model.coverage["byteLedger"][0]
    assert row["accountedBytes"] == row["byteLength"]
    assert model.lines[-1]["id"] == 1


def test_a_final_row_with_no_trailing_crlf_is_still_a_real_row():
    """Four shipped `.dlg` files end on a `(Starting Condition)` row with no final newline; the
    row is decoded like any other, it just owns no `lineBreak` range."""

    text = dlg_text([make_row(id_="1", male="Hi", link="#")]) + make_row(
        id_="99", male="(Starting Condition)", link="1", cond="G.Foo == 1"
    )
    model = decode_dialogue(make_closure(text))
    assert len(model.lines) == 2
    last = model.lines[-1]
    assert last["id"] == 99 and last["role"] == ROLE_PC_CHOICE
    row = model.coverage["byteLedger"][0]
    assert row["accountedBytes"] == row["byteLength"] == len(model.member.data)
    assert row["coveragePercent"] == 100.0
    assert not any(o.get("path") == "trailing" for o in model.coverage["unresolved"])


def test_a_zero_byte_member_publishes_the_contracts_empty_member_omission_with_no_lines():
    """An empty member is recorded with
    `byteLength: 0`, the SHA-256 of the empty string, and an `omissions` row `empty-member`;
    a unit whose selecting member is empty publishes rather than raising."""

    model = decode_dialogue(make_closure(""))
    assert model.lines == []
    assert any(o["role"] == "empty-member" for o in model.omissions)
    row = model.coverage["byteLedger"][0]
    assert row["byteLength"] == 0
    assert row["accountedBytes"] == 0
    assert row["coveragePercent"] == 100.0
    import hashlib

    assert row["sourceSha256"] == hashlib.sha256(b"").hexdigest()


def test_an_empty_member_dialogue_unit_exports_and_validates_with_a_warning(tmp_path):
    key = "hub/empty"
    dlg_path = source_path_for(key)
    index = {dlg_path: ("loose", "C:/fake/empty.dlg")}

    def read_bytes(idx, k):
        return b"" if k == dlg_path else None

    destination = exporter.export(index, key, tmp_path, read_bytes=read_bytes)
    summary = validation.validate(destination)
    assert summary["lines"] == 0
    assert summary["omissions"] == ["empty-member"]
    assert any("empty-member" in w for w in validation.warnings_for(summary))


def test_non_whitespace_trailing_content_is_carried_as_a_malformed_row_not_dropped():
    """Garbage after the last terminator that is not shaped like a row still owns every one of
    its bytes -- as a `malformed-cell` on a carried row, never as silently dropped bytes."""

    text = dlg_text([make_row(id_="1", male="Hi", link="#")]) + "GARBAGE"
    model = decode_dialogue(make_closure(text))
    assert len(model.lines) == 2
    assert any(a["role"] == "malformed-cell" for a in model.anomalies)
    row = model.coverage["byteLedger"][0]
    assert row["accountedBytes"] == row["byteLength"]


# --- document build: contract shape -------------------------------------------------------------


def test_build_document_opens_the_extension_root_with_the_contract_key_order():
    text = dlg_text([make_row(id_="10", male="Hi", link="#")])
    model = decode_dialogue(make_closure(text))
    document, binary = exporter.build_document(model)
    # Schema 1.1.0: the BIN chunk is the source capsule and nothing else. `build_document`
    # returns the unpadded payload; the container pads the chunk itself.
    assert binary == model.member.data
    root = document["extensions"][DIALOGUE_EXTENSION]
    assert tuple(list(root)[: len(ROOT_KEYS)]) == ROOT_KEYS
    assert list(root)[len(ROOT_KEYS):] == ["encoding", "lines", "expressions", "audio",
                                            "anomalies", "omissions"]
    assert root["schemaVersion"] == SCHEMA_VERSION
    assert document["asset"]["generator"] == "Elysium Dialogue GLB Exporter"
    assert document["extensionsUsed"] == [DIALOGUE_EXTENSION]
    assert document["extensionsRequired"] == [DIALOGUE_EXTENSION]
    for forbidden in ("scenes", "nodes", "meshes", "images", "textures", "samplers",
                       "animations", "skins", "accessors"):
        assert forbidden not in document
    assert document["buffers"] == [{"byteLength": len(binary)}]
    assert document["bufferViews"] == [
        {"buffer": 0, "byteOffset": 0, "byteLength": len(binary)}
    ]


def test_the_identity_names_the_dlg_root_key():
    text = dlg_text([make_row(id_="1", male="Hi", link="#")])
    model = decode_dialogue(make_closure(text, key="Main Characters/Jack_Tutorial"))
    document, _ = exporter.build_document(model)
    identity = document["extensions"][DIALOGUE_EXTENSION]["identity"]
    assert identity["asset"] == "vtmb:dialogue:main characters/jack_tutorial"
    assert identity["sourcePath"] == "dlg/main characters/jack_tutorial.dlg"
    assert identity["sourcePolicy"] == "up-first"
    assert output_relative_path("Main Characters/Jack_Tutorial") == PurePosixPath(
        "main characters/jack_tutorial.glb"
    )


# --- validation --------------------------------------------------------------------------------


def _build(text: str, *, key: str = "hub/stem", resolved: frozenset = frozenset()):
    closure = make_closure(text, key=key, resolved=resolved)
    model = decode_dialogue(closure)
    document, binary = exporter.build_document(model)
    return closure, document, binary


def test_a_well_formed_document_passes_validation():
    closure, document, binary = _build(
        dlg_text([make_row(id_="10", male="Hi", link="#", cond="G.Foo = 1"),
                  make_row(id_="21", male="Bye", link="0")])
    )
    summary = validation.validate_document(document, binary, source_members=closure.members())
    assert summary["lines"] == 2
    assert summary["coverage"] == {"unresolved": 0, "unsupported": 0, "typedUnidentified": 0}
    assert validation.warnings_for(summary) == []


def test_validation_rejects_a_tampered_byte_ledger():
    closure, document, binary = _build(dlg_text([make_row(id_="10", male="Hi", link="#")]))
    root = document["extensions"][DIALOGUE_EXTENSION]
    root["coverage"]["byteLedger"][0]["ranges"][0]["length"] += 1
    with pytest.raises(DialogueGlbValidationError):
        validation.validate_document(document, binary, source_members=closure.members())


def test_validation_rejects_a_tampered_field_that_disagrees_with_its_own_raw_text():
    closure, document, binary = _build(
        dlg_text([make_row(id_="21", male="(Auto-Link)", link="11"),
                  make_row(id_="11", male="Alright.", link="#")])
    )
    root = document["extensions"][DIALOGUE_EXTENSION]
    del root["lines"][0]["marker"]
    with pytest.raises(DialogueGlbValidationError, match="marker"):
        validation.validate_document(document, binary, source_members=closure.members())


def test_validation_rejects_a_named_column_that_disagrees_with_its_own_fields_cell():
    closure, document, binary = _build(dlg_text([make_row(id_="10", male="Hi", link="#")]))
    root = document["extensions"][DIALOGUE_EXTENSION]
    root["lines"][0]["textMale"] = "Tampered"
    with pytest.raises(DialogueGlbValidationError, match="textMale"):
        validation.validate_document(document, binary, source_members=closure.members())


def test_validation_rejects_an_encoding_other_than_latin1():
    closure, document, binary = _build(dlg_text([make_row(id_="10", male="Hi", link="#")]))
    root = document["extensions"][DIALOGUE_EXTENSION]
    root["encoding"] = "utf-8"
    with pytest.raises(DialogueGlbValidationError, match="encoding"):
        validation.validate_document(document, binary, source_members=closure.members())


def test_validation_rejects_a_dependency_row_with_an_unexpected_role():
    closure, document, binary = _build(dlg_text([make_row(id_="10", male="Hi", link="#")]))
    root = document["extensions"][DIALOGUE_EXTENSION]
    root["dependencies"].append(
        {"role": "camera-shot", "asset": "vtmb:camera-shot:x", "sourcePath": "x", "resolved": False}
    )
    with pytest.raises(DialogueGlbValidationError, match="role"):
        validation.validate_document(document, binary, source_members=closure.members())


def test_validation_rejects_a_dependency_that_disagrees_with_the_resolved_audio_candidates():
    key = "hub/stem"
    hub = "character/dlg/hub/stem"
    resolved = frozenset({f"sound/{hub}/line10_col_e.mp3"})
    closure, document, binary = _build(
        dlg_text([make_row(id_="10", male="Hi", link="#")]), key=key, resolved=resolved
    )
    root = document["extensions"][DIALOGUE_EXTENSION]
    root["dependencies"] = []
    with pytest.raises(DialogueGlbValidationError, match="audio candidates"):
        validation.validate_document(document, binary, source_members=closure.members())


def test_validation_rejects_a_stray_mp3_first_resolution_on_an_unshadowed_wav_dependency():
    """A resolved `.wav` candidate with no `.mp3` beside it declares a plain dependency row; the
    `mp3-first` marker on it would claim a shadowing that never happened."""

    key = "hub/stem"
    hub = "character/dlg/hub/stem"
    resolved = frozenset({f"sound/{hub}/line10_col_e.wav"})
    closure, document, binary = _build(
        dlg_text([make_row(id_="10", male="Hi", link="#")]), key=key, resolved=resolved
    )
    root = document["extensions"][DIALOGUE_EXTENSION]
    for row in root["dependencies"]:
        if row["role"] == "sound":
            row["resolution"] = "mp3-first"
    with pytest.raises(DialogueGlbValidationError, match="stray resolution"):
        validation.validate_document(document, binary, source_members=closure.members())


# --- install-facing surface: source_keys, key tolerance, export + round-trip --------------------


def test_source_keys_finds_every_dlg_member_below_the_dlg_root():
    index = {
        "dlg/main characters/jack_tutorial.dlg": ("loose", "C:/fake/jack_tutorial.dlg"),
        "dlg/kiki.dlg": ("vpk", ("pack001.vpk", 0, 10)),
        "materials/a.vmt": ("loose", "C:/fake/a.vmt"),
    }
    assert dlg_source.source_keys(index) == ["kiki", "main characters/jack_tutorial"]


def test_load_source_closure_tolerates_the_root_prefix_and_the_extension():
    key = "main characters/jack_tutorial"
    text = dlg_text([make_row(id_="1", male="Hi", link="#")])
    index = {source_path_for(key): ("loose", "C:/fake.dlg")}
    read_bytes = lambda idx, k: text.encode("latin-1") if k == source_path_for(key) else None

    for spelling in (key, "DLG/" + key + ".dlg", key + ".dlg"):
        closure = dlg_source.load_source_closure(index, spelling, read_bytes=read_bytes)
        assert closure.key == key
        assert closure.asset == f"vtmb:dialogue:{key}"


def test_load_source_closure_raises_when_the_install_holds_nothing_for_the_key():
    with pytest.raises(dlg_source.DialogueSourceError):
        dlg_source.load_source_closure({}, "missing/hub", read_bytes=lambda idx, k: None)


def test_export_then_validate_round_trips_through_a_real_glb_file(tmp_path):
    key = "main characters/jack_tutorial"
    hub = "character/dlg/main characters/jack_tutorial"
    text = dlg_text(
        [
            make_row(id_="191", male="Some subtitle", link="#", action="G.Test = 1"),
            make_row(id_="192", male="Continue", link="0"),
        ]
    )
    dlg_path = source_path_for(key)
    sound_path = f"sound/{hub}/line191_col_e.mp3"
    index = {
        dlg_path: ("loose", "C:/fake/jack_tutorial.dlg"),
        sound_path: ("loose", "C:/fake/line191_col_e.mp3"),
    }

    def read_bytes(idx, k):
        return text.encode("latin-1") if k == dlg_path else None

    destination = exporter.export(index, key, tmp_path, read_bytes=read_bytes)
    assert destination == tmp_path / "main characters" / "jack_tutorial.glb"
    assert destination.exists()

    summary = validation.validate(destination)
    assert summary["key"] == key
    assert summary["asset"] == f"vtmb:dialogue:{key}"
    assert summary["coverage"] == {"unresolved": 0, "unsupported": 0, "typedUnidentified": 0}
    # Only `_col_e.mp3` resolves in this install; every other language/extension candidate
    # declares no dependency row at all.
    assert summary["dependencies"] == 1


def test_exporting_the_same_source_twice_yields_byte_identical_products(tmp_path):
    key = "main characters/jack_tutorial"
    text = dlg_text(
        [
            make_row(id_="191", male="Some subtitle", link="#", action="G.Test = 1"),
            make_row(id_="192", male="Continue", link="0"),
        ]
    )
    dlg_path = source_path_for(key)
    index = {dlg_path: ("loose", "C:/fake/jack_tutorial.dlg")}

    def read_bytes(idx, k):
        return text.encode("latin-1") if k == dlg_path else None

    first = exporter.export(index, key, tmp_path / "a", read_bytes=read_bytes)
    second = exporter.export(index, key, tmp_path / "b", read_bytes=read_bytes)
    assert first.read_bytes() == second.read_bytes()


def test_validation_rejects_a_line_text_that_disagrees_with_the_source_bytes():
    """A published cell text tampered with after decoding is caught by the export-time
    independent re-decode, not just by the internal-consistency re-derivation checks."""

    closure, document, binary = _build(
        dlg_text([make_row(id_="10", male="Hi", link="#", cond="G.Foo = 1")])
    )
    root = document["extensions"][DIALOGUE_EXTENSION]
    root["lines"][0]["textMale"] = "TOTALLY DIFFERENT TEXT"
    root["lines"][0]["fields"][1]["text"] = "TOTALLY DIFFERENT TEXT"
    with pytest.raises(DialogueGlbValidationError, match="independent re-decode"):
        validation.validate_document(document, binary, source_members=closure.members())


def test_validation_rejects_a_dropped_typed_unidentified_row_at_export_time():
    """A `coverage.typedUnidentified` row deleted after decoding still agrees with the
    `reserved-column-used` anomaly (untouched), but the reserved-column re-check below now also
    requires a matching `typedUnidentified` row for a non-empty reserved cell, so this is caught
    standalone -- and, redundantly, by the export-time independent re-decode too."""

    closure, document, binary = _build(
        dlg_text([make_row(id_="30", male="Reserved test", link="#", col6="SPARE")])
    )
    root = document["extensions"][DIALOGUE_EXTENSION]
    root["coverage"]["typedUnidentified"] = []
    with pytest.raises(DialogueGlbValidationError, match="typedUnidentified"):
        validation.validate_document(document, binary, source_members=closure.members())


def test_standalone_validation_rejects_a_dropped_typed_unidentified_row_via_the_reserved_column_check():
    """Even without an independent re-decode (no `source_members`), a non-empty reserved column
    whose `coverage.typedUnidentified` row was deleted is caught by the reserved-column re-check
    alone, since it now requires that row to exist and agree with the field's own text/offset."""

    closure, document, binary = _build(
        dlg_text([make_row(id_="30", male="Reserved test", link="#", col6="SPARE")])
    )
    root = document["extensions"][DIALOGUE_EXTENSION]
    root["coverage"]["typedUnidentified"] = []
    with pytest.raises(DialogueGlbValidationError, match="typedUnidentified"):
        validation.validate_document(document, binary)


def test_standalone_validation_rejects_a_reserved_column_anomaly_dropped_alone():
    """Deleting only the `reserved-column-used` anomaly (leaving `typedUnidentified` and the
    field's own text untouched) is caught without needing a re-decode."""

    closure, document, binary = _build(
        dlg_text([make_row(id_="30", male="Reserved test", link="#", col6="SPARE")])
    )
    root = document["extensions"][DIALOGUE_EXTENSION]
    root["anomalies"] = [a for a in root["anomalies"] if a.get("role") != "reserved-column-used"]
    with pytest.raises(DialogueGlbValidationError, match="reserved-column-used"):
        validation.validate_document(document, binary)


def test_standalone_validation_rejects_a_line_id_that_disagrees_with_its_own_field():
    """`lines[].id` is a named column like the other six, cross-checked against its own
    `fields[0]` cell -- a tampered `id` alone (leaving `fields[0].text` untouched) is caught
    without needing a re-decode."""

    closure, document, binary = _build(
        dlg_text([make_row(id_="50", male="Hi", link="#"), make_row(id_="51", male="Bye", link="0")])
    )
    root = document["extensions"][DIALOGUE_EXTENSION]
    root["lines"][1]["id"] = 5000
    with pytest.raises(DialogueGlbValidationError, match="id disagrees"):
        validation.validate_document(document, binary)


def test_standalone_validation_rejects_an_omitted_proven_range_with_no_omissions_row():
    """The trailing-whitespace `omitted-proven` ledger range is backed by an `omissions` row
    naming the same owner; deleting that row while the range stays claimed is caught."""

    closure, document, binary = _build(
        dlg_text([make_row(id_="1", male="Hi", link="#")]) + "  "
    )
    root = document["extensions"][DIALOGUE_EXTENSION]
    assert root["omissions"], "the trailing bytes should have produced an omission row"
    root["omissions"] = []
    root["coverage"]["omittedProven"] = []
    with pytest.raises(DialogueGlbValidationError, match="omitted-proven"):
        validation.validate_document(document, binary)


def test_standalone_validation_rejects_a_malformed_link_dropped_from_coverage_unresolved():
    """A non-`#`/integer/empty link is `unresolved`; deleting that coverage row while the raw
    `link` cell still names the same malformed text is caught without needing a re-decode."""

    closure, document, binary = _build(dlg_text([make_row(id_="5", male="Odd", link="??")]))
    root = document["extensions"][DIALOGUE_EXTENSION]
    root["coverage"]["unresolved"] = []
    with pytest.raises(DialogueGlbValidationError, match="coverage.unresolved"):
        validation.validate_document(document, binary)


def test_standalone_validation_rejects_a_null_id_dropped_from_coverage_unresolved():
    closure, document, binary = _build(dlg_text([make_row(id_="ABC", male="Weird", link="#")]))
    root = document["extensions"][DIALOGUE_EXTENSION]
    root["coverage"]["unresolved"] = []
    with pytest.raises(DialogueGlbValidationError, match="coverage.unresolved"):
        validation.validate_document(document, binary)


def test_validation_rejects_a_candidate_that_names_a_lip_identity():
    """The dialogue unit does not own the `.lip` phoneme document's identity; a candidate that
    names one anyway is rejected rather than silently accepted."""

    key = "hub/stem"
    closure, document, binary = _build(
        dlg_text([make_row(id_="10", male="Hi", link="#")]), key=key
    )
    root = document["extensions"][DIALOGUE_EXTENSION]
    candidate = root["audio"]["lines"][0]["candidates"][0]
    candidate["lip"] = {"asset": "vtmb:sound:character/dlg/hub/stem/line10_col_e.lip", "resolved": False}
    with pytest.raises(DialogueGlbValidationError, match="lip"):
        validation.validate_document(document, binary, source_members=closure.members())
