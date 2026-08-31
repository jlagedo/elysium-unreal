"""Synthetic contract tests for the isolated Script GLB seam.

Every byte here is written by the test: no test reads the real install, so a machine with no
game on it runs the whole module.
"""

from __future__ import annotations

import copy
import hashlib
import struct

import pytest

from elysium_pipeline.exporters import script_glb
from elysium_pipeline.formats.script_glb import (
    SCRIPT_EXTENSION,
    ScriptDecodeError,
    decode_script,
    lexer,
    opcodes,
    pyc as pyc_reader,
    source as source_module,
)
from elysium_pipeline.formats.script_glb.model import SCHEMA_VERSION, normalize_script_key
from elysium_pipeline.formats.unit_contract import (
    ByteLedger,
    ByteLedgerError,
    ranges_sha256,
    read_glb,
)
from elysium_pipeline.validation import script_glb as validation

# ----------------------------------------------------------------------------------------------
# one synthetic install
# ----------------------------------------------------------------------------------------------

SOURCE_LINES = [
    'print "loading test level script"',
    "",
    "import __main__",
    "from __main__ import G as Globals",
    "",
    "Find = __main__.FindEntityByName",
    "Finds = __main__.FindEntitiesByName",
    "Grab = __main__.FindEntityByName",
    "flags = 0777",
    'label = ur"raw\\path"',
    'paths = ["maps/la_hub_1.bsp",',
    '         "dlg/main characters/jack.dlg",',
    '         "sound/ambient/rain.wav"]',
    "",
    "def talk(who, line=1, *rest, **kw):",
    '\tif who <> "none":',
    "\t\tprint `who`",
    '\t\tPlayDialogFile("Character/dlg/test/line1_col_e.mp3")',
    '\t\tPlayDialogFile("Character/dlg/test/line%d_col_e.mp3")',
    '\t\tPlayDialogFile("Character/dlg/test/line" + line)',
    '\treturn Find("door_01")',
    "",
    "def talk(who):\t# the second definition shadows the first",
    '\tGrab("lamp")',
    '\tFinds("crowd")',
    '\treturn "models/scenery/misc/trashcan01.mdl"',
    "",
    "class Helper:",
    "\tdef step(self):",
    '\t\treturn "vdata/system/credits.txt"',
    "",
]

SOURCE = b"".join(line.encode("latin-1") + b"\r\n" for line in SOURCE_LINES)

KEY = "tutorial/tutorial"
PY_PATH = "python/tutorial/tutorial.py"
PYC_PATH = "python/tutorial/tutorial.pyc"

PY_ENTRY = ("loose", "C:/game/Unofficial_Patch/python/tutorial/tutorial.py")
PYC_VPK_ENTRY = ("vpk", ("C:/game/Vampire/pack008.vpk", 195791969, 25044))
PYC_LOOSE_ENTRY = ("loose", "C:/game/Unofficial_Patch/python/tutorial/tutorial.pyc")


def line_of(fragment: str) -> int:
    """The 1-based line of the first synthetic source line that starts with `fragment`."""

    for index, line in enumerate(SOURCE_LINES):
        if line.startswith(fragment):
            return index + 1
    raise AssertionError(f"no synthetic source line starts with {fragment!r}")


# -- a hand-built Python 2.1 marshal stream -----------------------------------------------------


def _long(value: int) -> bytes:
    return struct.pack("<i", value)


def _short(value: int) -> bytes:
    return struct.pack("<h", value)


def _string(raw: bytes) -> bytes:
    return b"s" + _long(len(raw)) + raw


def _tuple(items) -> bytes:
    items = list(items)
    return b"(" + _long(len(items)) + b"".join(items)


def _code(
    *,
    code: bytes = b"",
    consts=(),
    names=(),
    varnames=(),
    filename: bytes = b"J:/Remaster/Vampire_v409_041008_LOCS/vampire/python/tutorial/tutorial.py",
    name: bytes = b"?",
    firstlineno: int = 1,
    lnotab: bytes = b"",
    argcount: int = 0,
    nlocals: int = 0,
    stacksize: int = 2,
    flags: int = 0,
) -> bytes:
    return (
        b"c"
        + _short(argcount)
        + _short(nlocals)
        + _short(stacksize)
        + _short(flags)
        + _string(code)
        + _tuple(consts)
        + _tuple(names)
        + _tuple(varnames)
        + _tuple(())
        + _tuple(())
        + _string(filename)
        + _string(name)
        + _short(firstlineno)
        + _string(lnotab)
    )


def _pyc(
    root: bytes, *, magic: bytes = opcodes.PYTHON_21_MAGIC_BYTES, mtime: int = 0x415F5F4B
) -> bytes:
    return magic + struct.pack("<I", mtime) + root


#: `SET_LINENO 1; LOAD_CONST 0; PRINT_ITEM; PRINT_NEWLINE; LOAD_CONST 1; RETURN_VALUE`
MODULE_BYTECODE = (
    bytes([127, 1, 0])
    + bytes([100, 0, 0])
    + bytes([71])
    + bytes([72])
    + bytes([100, 1, 0])
    + bytes([83])
)


def module_pyc(*, talk_line: int | None = None, helper_line: int | None = None, **kwargs) -> bytes:
    talk = _code(
        code=bytes([127, 1, 0, 100, 0, 0, 83]),
        consts=[b"N"],
        name=b"talk",
        argcount=1,
        firstlineno=talk_line if talk_line is not None else line_of("def talk(who, line"),
    )
    helper = _code(
        code=bytes([127, 1, 0, 100, 0, 0, 83]),
        consts=[b"N"],
        name=b"Helper",
        firstlineno=helper_line if helper_line is not None else line_of("class Helper"),
    )
    root = _code(
        code=MODULE_BYTECODE,
        consts=[_string(b"loading test level script"), talk, helper, b"N"],
        names=[_string(b"__main__"), _string(b"talk")],
        name=b"?",
        lnotab=bytes([3, 1, 4, 2]),
    )
    return _pyc(root, **kwargs)


PYC = module_pyc()


def make_index(*, pyc: bool = True, pyc_entry=PYC_VPK_ENTRY, members: dict | None = None) -> dict:
    index = {
        "models/scenery/misc/trashcan01.mdl": ("vpk", ("C:/game/Vampire/pack001.vpk", 0, 10)),
        "vdata/system/credits.txt": ("vpk", ("C:/game/Vampire/pack001.vpk", 10, 10)),
        "maps/la_hub_1.bsp": ("loose", "C:/game/Unofficial_Patch/maps/la_hub_1.bsp"),
        "dlg/main characters/jack.dlg": ("loose", "C:/game/Unofficial_Patch/dlg/jack.dlg"),
        "sound/ambient/rain.wav": ("vpk", ("C:/game/Vampire/pack002.vpk", 0, 10)),
    }
    if members is None:
        index[PY_PATH] = PY_ENTRY
    else:
        index.update(members)
    if pyc:
        index[PYC_PATH] = pyc_entry
    return index


def reader(bodies: dict[str, bytes]):
    return lambda index, key: bodies.get(key)


def resolver(index=None):
    """The membership question export-time validation asks the index again."""

    index = make_index() if index is None else index
    return lambda path: path in index


DEFAULT_BODIES = {PY_PATH: SOURCE, PYC_PATH: PYC}


def closure_for(index=None, bodies=None, key: str = KEY):
    index = make_index() if index is None else index
    bodies = DEFAULT_BODIES if bodies is None else bodies
    return source_module.load_source_closure(index, key, read_bytes=reader(bodies))


def model_for(index=None, bodies=None, key: str = KEY):
    index = make_index() if index is None else index
    closure = closure_for(index, bodies, key)
    return closure, decode_script(closure, member_exists=lambda path: path in index)


def document_for(index=None, bodies=None, key: str = KEY):
    closure, model = model_for(index, bodies, key)
    document, binary = script_glb.build_document(model, closure.executed)
    return closure, model, document, binary


def extension_of(document) -> dict:
    return document["extensions"][SCRIPT_EXTENSION]


# ----------------------------------------------------------------------------------------------
# identity, container and the extension root
# ----------------------------------------------------------------------------------------------


def test_the_key_is_the_path_below_python_without_its_extension():
    assert normalize_script_key("python/tutorial/tutorial.py") == KEY
    assert normalize_script_key("Python\\Tutorial\\Tutorial.pyc") == KEY
    assert normalize_script_key(KEY) == KEY
    assert script_glb.output_relative_path(KEY).as_posix() == "tutorial/tutorial.glb"


def test_one_identity_names_one_file_and_one_file_names_one_identity():
    _, _, document, _ = document_for()
    extension = extension_of(document)
    assert extension["identity"]["asset"] == "vtmb:script:tutorial/tutorial"
    assert extension["identity"]["scriptPath"] == KEY
    assert extension["identity"]["sourcePaths"] == [PY_PATH, PYC_PATH]
    assert extension["identity"]["sourcePolicy"] == "up-first"


def test_the_extension_is_used_and_required_and_the_generator_names_the_seam():
    _, _, document, _ = document_for()
    assert document["extensionsUsed"] == [SCRIPT_EXTENSION]
    assert document["extensionsRequired"] == [SCRIPT_EXTENSION]
    assert document["asset"]["generator"] == "Elysium Script GLB Exporter"
    assert document["asset"]["version"] == "2.0"


def test_the_extension_root_opens_with_the_contracts_keys_in_order():
    _, _, document, _ = document_for()
    keys = list(extension_of(document))
    assert keys[:5] == [
        "schemaVersion",
        "identity",
        "sourceResolution",
        "dependencies",
        "coverage",
    ]
    assert keys[5:] == [
        "source",
        "tokens",
        "structure",
        "references",
        "entityNames",
        "pyc",
        "anomalies",
        "omissions",
    ]
    assert extension_of(document)["schemaVersion"] == SCHEMA_VERSION


def test_a_script_unit_is_scene_less_and_carries_no_binary_chunk():
    _, _, document, binary = document_for()
    assert binary == b""
    for core in ("scenes", "nodes", "meshes", "images", "textures", "samplers", "buffers"):
        assert core not in document


def test_a_companion_inside_a_vpk_is_provenance_the_interpreter_never_executes():
    _, _, document, _ = document_for()
    rows = {row["role"]: row for row in extension_of(document)["sourceResolution"]["members"]}
    assert rows["py"]["executed"] is True
    assert rows["pyc"]["executed"] is False
    assert rows["pyc"]["origin"]["kind"] == "vpk"


def test_a_loose_companion_is_one_the_interpreter_executes():
    index = make_index(pyc_entry=PYC_LOOSE_ENTRY)
    _, _, document, _ = document_for(index)
    rows = {row["role"]: row for row in extension_of(document)["sourceResolution"]["members"]}
    assert rows["pyc"]["executed"] is True


def test_the_source_kind_follows_from_the_members_the_install_resolved():
    assert closure_for().source_kind == "py+pyc"
    assert closure_for(make_index(pyc=False)).source_kind == "py"
    compiled_only = {"python/lib/orphan.pyc": PYC_LOOSE_ENTRY}
    closure = source_module.load_source_closure(
        compiled_only, "lib/orphan", read_bytes=reader({"python/lib/orphan.pyc": PYC})
    )
    assert closure.source_kind == "pyc-only"


def test_an_install_with_neither_member_names_no_unit():
    with pytest.raises(source_module.ScriptSourceError):
        source_module.load_source_closure({}, "lib/absent", read_bytes=reader({}))


def test_source_keys_lists_a_py_once_and_a_companion_only_when_it_stands_alone():
    index = {
        "python/tutorial/tutorial.py": PY_ENTRY,
        "python/tutorial/tutorial.pyc": PYC_VPK_ENTRY,
        "python/lib/orphan.pyc": PYC_LOOSE_ENTRY,
        "python/warehouse/warehouse.old": PY_ENTRY,
        "materials/x.vmt": PY_ENTRY,
    }
    assert script_glb.source_keys(index) == ["lib/orphan", "tutorial/tutorial"]


# ----------------------------------------------------------------------------------------------
# the Python 2.1 grammar
# ----------------------------------------------------------------------------------------------


def test_the_tokenizer_reads_the_python_2_syntax_this_interpreter_refuses():
    tokens = lexer.tokenize(lexer.decode_text(SOURCE))
    spellings = [token.string for token in tokens]
    assert "print" in spellings           # a statement, not a builtin
    assert "`" in spellings               # backtick repr
    assert "<>" in spellings              # the second spelling of !=
    assert "0777" in spellings            # an octal literal with no 0o prefix
    assert 'ur"raw\\path"' in spellings   # a raw unicode literal


def test_a_long_suffix_belongs_to_its_number_and_not_to_a_name_beside_it():
    tokens = lexer.tokenize("count = 10L\r\n")
    assert [
        (token.type, token.string) for token in tokens if token.type in ("NAME", "NUMBER")
    ] == [("NAME", "count"), ("NUMBER", "10L")]


def test_every_byte_between_two_tokens_is_insignificant_whitespace():
    text = lexer.decode_text(SOURCE)
    tokens = lexer.tokenize(text)
    # The unit's ledger runs over lines, so this is what proves the token stream read the whole
    # module rather than the part of it the tokenizer happened to recognise.
    from elysium_pipeline.formats.script_glb import verify_token_partition

    verify_token_partition(text, tokens)
    for token in tokens:
        assert text[token.offset:token.end] == token.string


def test_a_character_the_grammar_has_no_rule_for_fails_the_unit():
    with pytest.raises(ScriptDecodeError):
        decode_script(
            closure_for(
                make_index(pyc=False), {PY_PATH: b"value = 1 $ 2\r\n"}
            )
        )


def test_an_unterminated_string_fails_the_unit_rather_than_being_skipped():
    with pytest.raises(ScriptDecodeError):
        decode_script(closure_for(make_index(pyc=False), {PY_PATH: b'name = "open\r\n'}))


def test_a_triple_quoted_literal_spans_the_lines_it_covers():
    body = b'text = """one\r\ntwo"""\r\nvalue = 1\r\n'
    _, model = model_for(make_index(pyc=False), {PY_PATH: body})
    strings = [token for token in model.tokens if token.type == "STRING"]
    assert strings[0].string == '"""one\r\ntwo"""'
    assert model.source["lineCount"] == 3


# ----------------------------------------------------------------------------------------------
# the source block and the byte ledger
# ----------------------------------------------------------------------------------------------


def test_every_source_byte_is_claimed_exactly_once_by_its_line():
    _, model = model_for()
    ledger = model.coverage["byteLedger"][0]
    assert ledger["sourcePath"] == PY_PATH
    assert ledger["coveragePercent"] == 100.0
    assert ledger["byteLength"] == len(SOURCE)
    cursor = 0
    for row in ledger["ranges"]:
        assert row["offset"] == cursor
        cursor += row["length"]
    assert cursor == len(SOURCE)
    assert set(ledger["stateBytes"]) == {"mapped-text"}
    assert ledger["sourceSha256"] == hashlib.sha256(SOURCE).hexdigest()


def test_every_companion_byte_is_claimed_exactly_once_by_its_marshal_field():
    _, model = model_for()
    ledger = model.coverage["byteLedger"][1]
    assert ledger["sourcePath"] == PYC_PATH
    assert ledger["byteLength"] == len(PYC)
    owners = [row["owner"] for row in ledger["ranges"]]
    assert owners[0] == "pyc.magic"
    assert owners[1] == "pyc.mtime"
    assert "pyc.code.argcount" in owners
    assert "pyc.code.consts[1].name" in owners
    cursor = 0
    for row in ledger["ranges"]:
        assert row["offset"] == cursor
        cursor += row["length"]
    assert cursor == len(PYC)
    assert set(ledger["stateBytes"]) == {"mapped"}


def test_neither_format_pads_so_no_range_is_claimed_zero():
    _, model = model_for()
    for ledger in model.coverage["byteLedger"]:
        assert "padding-zero" not in ledger["stateBytes"]
        assert "reserved-zero" not in ledger["stateBytes"]


def test_a_zero_state_claim_over_a_non_zero_byte_aborts_publication():
    ledger = ByteLedger(PY_PATH, b"abc")
    with pytest.raises(ByteLedgerError):
        ledger.claim(0, 3, "reserved-zero", "source.bom")
    zeros = ByteLedger(PYC_PATH, b"\0\0\0")
    zeros.claim(0, 3, "padding-zero", "pyc.padding")
    assert zeros.finish()["stateBytes"] == {"padding-zero": 3}


def test_a_byte_order_mark_is_claimed_apart_from_the_line_it_opens():
    body = lexer.BOM.encode("latin-1") + b"value = 1\r\n"
    _, model = model_for(make_index(pyc=False), {PY_PATH: body})
    ranges = model.coverage["byteLedger"][0]["ranges"]
    assert ranges[0] == {"offset": 0, "length": 3, "state": "mapped", "owner": "source.bom"}
    assert ranges[1]["owner"] == "source.lines[0]"
    assert model.source["bom"] == {"offset": 0, "length": 3}


def test_the_line_table_states_spans_and_never_a_copy_of_the_text():
    _, model = model_for()
    assert set(model.source["lines"][0]) == {"index", "offset", "length", "terminator"}
    assert model.source["encoding"] == "ascii"
    assert model.source["lineEnding"] == "crlf"


def test_an_empty_member_publishes_with_an_omission_and_an_empty_ledger():
    _, model = model_for(make_index(pyc=False), {PY_PATH: b""})
    assert [row["role"] for row in model.omissions] == ["empty-member"]
    ledger = model.coverage["byteLedger"][0]
    assert ledger["byteLength"] == 0
    assert ledger["ranges"] == []
    assert ledger["coveragePercent"] == 100.0


# ----------------------------------------------------------------------------------------------
# structure
# ----------------------------------------------------------------------------------------------


def test_a_name_defined_twice_appears_twice_with_the_later_naming_the_earlier():
    _, model = model_for()
    functions = model.structure["functions"]
    assert [row["name"] for row in functions] == ["talk", "talk"]
    assert functions[0]["shadows"] is None
    assert functions[1]["shadows"] == {"kind": "function", "index": 0}
    assert functions[0]["lineSpan"][0] == line_of("def talk(who, line")
    assert functions[1]["lineSpan"][0] == line_of("def talk(who):")
    roles = [row["role"] for row in model.anomalies]
    assert "duplicate-definition" in roles


def test_the_parameter_list_keeps_its_forms_and_its_defaults():
    _, model = model_for()
    first = model.structure["functions"][0]
    assert [argument["name"] for argument in first["args"]] == ["who", "line", "rest", "kw"]
    assert [argument["form"] for argument in first["args"]] == [
        "positional", "positional", "star", "double-star"
    ]
    assert first["defaults"] == ["1"]


def test_a_class_carries_the_methods_its_block_declares():
    _, model = model_for()
    classes = model.structure["classes"]
    assert [row["name"] for row in classes] == ["Helper"]
    assert [method["name"] for method in classes[0]["methods"]] == ["step"]
    assert classes[0]["lineSpan"] == [line_of("class Helper"), line_of("\t\treturn \"vdata")]


def test_imports_keep_the_module_the_names_and_the_alias():
    _, model = model_for()
    imports = model.structure["imports"]
    assert imports[0] == {
        "kind": "import", "module": "__main__", "names": ["__main__"], "aliases": [],
        "line": line_of("import __main__"), "token": imports[0]["token"],
    }
    assert imports[1]["kind"] == "from"
    assert imports[1]["module"] == "__main__"
    assert imports[1]["names"] == ["G"]
    assert imports[1]["aliases"] == ["Globals"]


def test_module_level_assignments_name_their_targets():
    _, model = model_for()
    targets = [row["targets"][0] for row in model.structure["assignments"]]
    assert targets == ["Find", "Finds", "Grab", "flags", "label", "paths"]


def test_a_bracket_continuation_line_is_not_block_indentation():
    _, model = model_for()
    # `paths = [...]` lines its elements up with spaces while every block uses tabs; only a
    # statement's own leading whitespace is indentation.
    assert "tab-space-indent-mix" not in [row["role"] for row in model.anomalies]


def test_a_block_indented_with_tabs_and_spaces_is_an_anomaly():
    body = (
        b"def a():\r\n\treturn 1\r\n\r\ndef b():\r\n    return 2\r\n"
    )
    _, model = model_for(make_index(pyc=False), {PY_PATH: body})
    rows = [row for row in model.anomalies if row["role"] == "tab-space-indent-mix"]
    assert rows and rows[0]["characters"] == ["\t", " "]


def test_both_line_terminators_in_one_file_is_an_anomaly():
    body = b"a = 1\r\nb = 2\nc = 3\r\n"
    _, model = model_for(make_index(pyc=False), {PY_PATH: body})
    rows = [row for row in model.anomalies if row["role"] == "mixed-line-endings"]
    assert rows and model.source["lineEnding"] == "mixed"


def test_a_byte_above_the_ascii_range_is_carried_with_its_offset():
    body = b'name = "caf\xe9"\r\n'
    _, model = model_for(make_index(pyc=False), {PY_PATH: body})
    rows = [row for row in model.anomalies if row["role"] == "non-ascii-byte"]
    assert rows == [{"role": "non-ascii-byte", "sourceOffset": 11, "byte": 0xE9, "latin1": "é"}]
    assert model.source["encoding"] == "latin-1"


# ----------------------------------------------------------------------------------------------
# references, dependencies and entity names
# ----------------------------------------------------------------------------------------------


def test_each_dependency_role_comes_from_the_literal_that_produced_it():
    _, model = model_for()
    rows = {row["role"]: row for row in model.dependencies}
    assert rows["map"]["asset"] == "vtmb:map:la_hub_1"
    assert rows["map"]["sourcePath"] == "maps/la_hub_1.bsp"
    assert rows["dialogue"]["asset"] == "vtmb:dialogue:main characters/jack"
    assert rows["dialogue"]["sourcePath"] == "dlg/main characters/jack.dlg"
    assert rows["model"]["asset"] == "vtmb:model:scenery/misc/trashcan01"
    assert rows["vdata"]["asset"] == "vtmb:vdata:system/credits"
    sounds = sorted(row["asset"] for row in model.dependencies if row["role"] == "sound")
    assert sounds == [
        "vtmb:sound:ambient/rain.wav",
        "vtmb:sound:character/dlg/test/line1_col_e.mp3",
    ]


def test_every_reference_owns_exactly_one_dependency_row():
    _, model = model_for()
    named = {
        reference.asset for reference in model.references if reference.sentinel_reason is None
    }
    assert named == {row["asset"] for row in model.dependencies}
    assert len(model.dependencies) == len(named)


def test_a_literal_the_install_does_not_hold_resolves_false_and_warns():
    index = make_index()
    del index["maps/la_hub_1.bsp"]
    _, _, document, binary = document_for(index)
    summary = validation.validate_document(document, binary)
    rows = extension_of(document)["dependencies"]
    row = [entry for entry in rows if entry["role"] == "map"][0]
    assert row["resolved"] is False
    assert any("maps/la_hub_1.bsp" in warning for warning in validation.warnings_for(summary))


def test_a_run_time_composed_sound_path_keeps_a_sentinel_and_no_dependency():
    _, model = model_for()
    sentinels = [row for row in model.references if row.sentinel_reason is not None]
    assert {row.asset for row in sentinels} == {
        "vtmb:missing-sound:character/dlg/test/line%d_col_e.mp3",
        "vtmb:missing-sound:character/dlg/test/line",
    }
    assert all(row.resolved is False for row in sentinels)
    assert not {row.asset for row in sentinels} & {
        row["asset"] for row in model.dependencies
    }
    graded = {row["asset"] for row in model.coverage["omittedProven"]}
    assert graded == {row.asset for row in sentinels}
    reasons = {row["reason"] for row in model.coverage["omittedProven"]}
    assert any("printf conversion" in reason for reason in reasons)
    assert any("continues past this literal" in reason for reason in reasons)


def test_a_member_below_sound_that_is_no_wav_or_mp3_names_no_sound_unit():
    body = b'probe = "sound/character/dlg/andrei/line5.lip"\r\n'
    _, model = model_for(make_index(pyc=False), {PY_PATH: body})
    assert model.references == ()
    assert model.dependencies == ()


def test_entity_names_carry_their_call_and_produce_no_dependency_row():
    _, model = model_for()
    rows = {(record.name, record.call) for record in model.entity_names}
    assert rows == {("door_01", "Find"), ("crowd", "Finds"), ("lamp", "Grab")}
    assert not any(
        row["asset"].startswith("vtmb:map-entities:") for row in model.dependencies
    )


# ----------------------------------------------------------------------------------------------
# the compiled companion
# ----------------------------------------------------------------------------------------------


def test_the_companion_is_decoded_completely_from_its_marshal_stream():
    _, model = model_for()
    assert model.pyc["magic"] == opcodes.PYTHON_21_MAGIC
    assert model.pyc["magicExpected"] is True
    assert model.pyc["mtime"] == 0x415F5F4B
    root = model.pyc["code"]
    assert root["filename"].startswith("J:/Remaster/")
    assert root["names"] == ["__main__", "talk"]
    assert [const["name"] for const in root["consts"] if isinstance(const, dict)] == [
        "talk", "Helper"
    ]


def test_the_bytecode_is_disassembled_against_the_2_1_table():
    _, model = model_for()
    instructions = model.pyc["code"]["instructions"]
    assert [row["name"] for row in instructions] == [
        "SET_LINENO", "LOAD_CONST", "PRINT_ITEM", "PRINT_NEWLINE", "LOAD_CONST", "RETURN_VALUE"
    ]
    assert instructions[1]["argValue"] == "loading test level script"
    # `SET_LINENO`'s operand is the line, and the instruction already publishes `line`; the
    # datum is stated once.
    assert instructions[0]["argValue"] is None
    assert instructions[0]["arg"] == 1
    assert instructions[-1]["line"] == 4


def test_a_marshal_type_outside_the_2_1_set_stops_the_decode_at_its_offset():
    broken = bytearray(PYC)
    broken[8 + 9] = ord("q")            # the code string's type byte
    with pytest.raises(pyc_reader.PycDecodeError) as caught:
        pyc_reader.decode_pyc(bytes(broken), PYC_PATH)
    assert "Python 2.1 does not write" in str(caught.value)


def test_an_opcode_outside_the_2_1_table_is_carried_as_typed_but_unidentified():
    root = _code(code=bytes([1, 250, 0, 0, 83]), consts=[b"N"], name=b"?")
    data = _pyc(root)
    decoded = pyc_reader.decode_pyc(data, PYC_PATH)
    rows = decoded.typed_unidentified
    assert rows and rows[0]["opcode"] == 250
    assert rows[0]["sourceOffset"] == 1
    names = [row["name"] for row in decoded.code["instructions"]]
    assert names == ["POP_TOP", None, "RETURN_VALUE"]


def test_a_companion_whose_magic_is_not_60202_is_an_anomaly():
    data = module_pyc(magic=b"\x2b\xeb\x0d\x0a")
    decoded = pyc_reader.decode_pyc(data, PYC_PATH)
    assert decoded.magic_expected is False
    assert [row["role"] for row in decoded.anomalies] == ["pyc-magic-mismatch"]
    assert decoded.anomalies[0]["expected"] == opcodes.PYTHON_21_MAGIC


def test_bytes_after_the_code_object_are_claimed_and_proven_omitted():
    data = PYC + b"\x01\x02\x03\x04"
    decoded = pyc_reader.decode_pyc(data, PYC_PATH)
    assert [row["role"] for row in decoded.anomalies] == ["trailing-bytes-after-code"]
    # The ledger grades the range `omitted-proven`, so the reason lives where that state says
    # it must: one `coverage.omittedProven` row naming the same field.
    assert [row["field"] for row in decoded.omitted_proven] == ["pyc.trailing"]
    assert decoded.omitted_proven[0]["length"] == 4
    trailing = [claim for claim in decoded.claims if claim.owner == "pyc.trailing"]
    assert [claim.state for claim in trailing] == ["omitted-proven"]
    assert sum(claim.length for claim in decoded.claims) == len(data)


def test_a_proven_omission_of_the_companion_reaches_the_units_coverage():
    index = make_index(pyc_entry=PYC_LOOSE_ENTRY)
    bodies = {PY_PATH: SOURCE, PYC_PATH: PYC + b"\x01\x02\x03\x04"}
    _, model = model_for(index, bodies)
    graded = {row["field"] for row in model.coverage["omittedProven"] if "field" in row}
    assert "pyc.trailing" in graded
    assert model.coverage["unresolved"] == []


def test_the_companion_and_the_source_are_compared_and_never_reconciled():
    _, model = model_for()
    assert "pyc-source-drift" not in [row["role"] for row in model.anomalies]
    drifted = module_pyc(talk_line=999)
    _, other = model_for(bodies={PY_PATH: SOURCE, PYC_PATH: drifted})
    rows = [row for row in other.anomalies if row["role"] == "pyc-source-drift"]
    assert rows and rows[0]["aspect"] == "firstlineno"
    assert rows[0]["names"][0] == {
        "name": "talk", "pyc": 999, "source": line_of("def talk(who, line")
    }


def test_a_companion_only_unit_publishes_a_decode_and_no_token_stream():
    index = {"python/lib/orphan.pyc": PYC_LOOSE_ENTRY}
    closure = source_module.load_source_closure(
        index, "lib/orphan", read_bytes=reader({"python/lib/orphan.pyc": PYC})
    )
    model = decode_script(closure, member_exists=lambda path: path in index)
    document, binary = script_glb.build_document(model, closure.executed)
    summary = validation.validate_document(document, binary, source_members=closure.members(), member_exists=resolver()
    )
    assert summary["sourceKind"] == "pyc-only"
    assert summary["tokens"] == 0
    # six in the module body, three in each of the two code objects its consts carry
    assert summary["instructions"] == 12


# ----------------------------------------------------------------------------------------------
# validation
# ----------------------------------------------------------------------------------------------


def test_a_published_unit_is_complete_and_accounts_for_every_byte():
    closure, _, document, binary = document_for()
    summary = validation.validate_document(document, binary, source_members=closure.members(), member_exists=resolver()
    )
    assert summary["unresolved"] == 0
    assert summary["unsupported"] == 0
    assert summary["typedUnidentified"] == 0
    assert summary["byteCoveragePercent"] == [100.0, 100.0]


def test_the_validator_refuses_a_ledger_whose_ranges_leave_a_gap():
    closure, _, document, binary = document_for()
    tampered = copy.deepcopy(document)
    ledger = extension_of(tampered)["coverage"]["byteLedger"][0]
    ledger["ranges"][0]["length"] -= 1
    with pytest.raises(validation.ScriptGlbValidationError):
        validation.validate_document(tampered, binary, source_members=closure.members(), member_exists=resolver()
    )


def test_the_validator_refuses_a_false_zero_claim_against_the_member():
    closure, _, document, binary = document_for()
    tampered = copy.deepcopy(document)
    ledger = extension_of(tampered)["coverage"]["byteLedger"][0]
    ledger["ranges"][0]["state"] = "reserved-zero"
    totals: dict[str, int] = {}
    for row in ledger["ranges"]:
        totals[row["state"]] = totals.get(row["state"], 0) + row["length"]
    ledger["stateBytes"] = dict(sorted(totals.items()))
    ledger["rangesSha256"] = ranges_sha256(
        ledger["sourcePath"], ledger["byteLength"], ledger["ranges"]
    )
    with pytest.raises(validation.ScriptGlbValidationError):
        validation.validate_document(tampered, binary, source_members=closure.members(), member_exists=resolver()
    )
    # Without the members the claim cannot be weighed: that is what export-time validation is for.
    validation.validate_document(tampered, binary)


def test_the_validator_refuses_a_ledger_whose_digest_disagrees():
    _, _, document, binary = document_for()
    tampered = copy.deepcopy(document)
    extension_of(tampered)["coverage"]["byteLedger"][1]["rangesSha256"] = "0" * 64
    with pytest.raises(validation.ScriptGlbValidationError):
        validation.validate_document(tampered, binary)


def test_the_validator_refuses_an_identity_that_disagrees_with_its_member():
    _, _, document, binary = document_for()
    tampered = copy.deepcopy(document)
    extension_of(tampered)["identity"]["scriptPath"] = "tutorial/other"
    with pytest.raises(validation.ScriptGlbValidationError):
        validation.validate_document(tampered, binary)


def test_the_validator_refuses_a_structure_the_tokens_do_not_produce():
    _, _, document, binary = document_for()
    tampered = copy.deepcopy(document)
    extension_of(tampered)["structure"]["functions"][0]["name"] = "invented"
    with pytest.raises(validation.ScriptGlbValidationError):
        validation.validate_document(tampered, binary)


def test_the_validator_refuses_a_sentinel_that_produced_a_dependency():
    _, _, document, binary = document_for()
    tampered = copy.deepcopy(document)
    extension = extension_of(tampered)
    sentinel = [row for row in extension["references"] if row.get("omittedProven")][0]
    extension["dependencies"].append(
        {"role": sentinel["kind"], "asset": sentinel["asset"],
         "sourcePath": sentinel["sourcePath"], "resolved": False}
    )
    with pytest.raises(validation.ScriptGlbValidationError):
        validation.validate_document(tampered, binary)


def test_the_validator_refuses_a_declared_scene():
    _, _, document, binary = document_for()
    tampered = copy.deepcopy(document)
    tampered["scenes"] = [{"nodes": []}]
    with pytest.raises(validation.ScriptGlbValidationError):
        validation.validate_document(tampered, binary)


def test_a_member_capsule_the_seam_never_declared_is_refused():
    """The script seam has not adopted the source capsule, so it may not carry one unannounced."""

    closure, _, document, binary = document_for()
    validation.validate_document(document, binary, source_members=closure.members(), member_exists=resolver()
    )
    tampered = copy.deepcopy(document)
    members = extension_of(tampered)["sourceResolution"]["members"]
    members[0]["capsule"] = {"byteLength": 0}
    with pytest.raises(validation.ScriptGlbValidationError, match="does not declare"):
        validation.validate_document(tampered, binary, source_members=closure.members(), member_exists=resolver()
    )


# ----------------------------------------------------------------------------------------------
# the written product
# ----------------------------------------------------------------------------------------------


def test_the_written_unit_round_trips_through_standalone_validation(tmp_path):
    index = make_index()
    path = script_glb.export(index, KEY, tmp_path, read_bytes=reader(DEFAULT_BODIES))
    assert path == tmp_path / "tutorial" / "tutorial.glb"
    summary = validation.validate(path)
    assert summary["asset"] == "vtmb:script:tutorial/tutorial"
    assert summary["key"] == KEY
    assert summary["members"] == [PY_PATH, PYC_PATH]
    assert summary["byteCoveragePercent"] == [100.0, 100.0]
    assert summary["unresolved"] == 0 and summary["unsupported"] == 0
    document, binary = read_glb(path)
    assert binary == b""
    assert document["extensionsRequired"] == [SCRIPT_EXTENSION]


def test_one_source_closure_yields_one_byte_identical_product(tmp_path):
    index = make_index()
    first = script_glb.export(index, KEY, tmp_path / "a", read_bytes=reader(DEFAULT_BODIES))
    second = script_glb.export(index, KEY, tmp_path / "b", read_bytes=reader(DEFAULT_BODIES))
    assert first.read_bytes() == second.read_bytes()


def test_the_singular_command_tolerates_the_root_prefix_and_the_extension(tmp_path):
    index = make_index()
    path = script_glb.export(index, PY_PATH, tmp_path, read_bytes=reader(DEFAULT_BODIES))
    assert path == tmp_path / "tutorial" / "tutorial.glb"


def test_the_validator_refuses_a_companion_decode_no_member_backs():
    _, _, document, binary = document_for(make_index(pyc=False))
    assert extension_of(document)["pyc"] is None
    validation.validate_document(document, binary)
    tampered = copy.deepcopy(document)
    extension_of(tampered)["pyc"] = extension_of(document_for()[2])["pyc"]
    with pytest.raises(validation.ScriptGlbValidationError):
        validation.validate_document(tampered, binary)


def test_an_empty_companion_publishes_no_decode_and_still_owns_a_ledger_row():
    _, model = model_for(bodies={PY_PATH: SOURCE, PYC_PATH: b""})
    assert model.pyc is None
    assert [row["role"] for row in model.omissions] == ["empty-member"]
    assert [row["sourcePath"] for row in model.coverage["byteLedger"]] == [PY_PATH, PYC_PATH]
    closure, _, document, binary = document_for(bodies={PY_PATH: SOURCE, PYC_PATH: b""})
    validation.validate_document(document, binary, source_members=closure.members(), member_exists=resolver()
    )


# ----------------------------------------------------------------------------------------------
# a byte-order mark, the authored spelling and the compiled twin's own constants
# ----------------------------------------------------------------------------------------------


BOM_SOURCE = lexer.BOM.encode("latin-1") + b"value = 1\r\nother = 2\r\n"


def test_a_source_that_opens_with_a_byte_order_mark_publishes_and_validates(tmp_path):
    index = make_index(pyc=False)
    closure, model, document, binary = document_for(index, {PY_PATH: BOM_SOURCE})
    lines = model.source["lines"]
    # The mark is its own owner, so the line it opens is the text after it.
    assert lines[0] == {"index": 0, "offset": 3, "length": 11, "terminator": "crlf"}
    assert lines[1]["offset"] == 14
    ranges = model.coverage["byteLedger"][0]["ranges"]
    assert ranges[0] == {"offset": 0, "length": 3, "state": "mapped", "owner": "source.bom"}
    assert ranges[1] == {
        "offset": 3, "length": 11, "state": "mapped-text", "owner": "source.lines[0]"
    }
    validation.validate_document(
        document, binary, source_members=closure.members(), member_exists=resolver(index)
    )
    path = script_glb.export(index, KEY, tmp_path, read_bytes=reader({PY_PATH: BOM_SOURCE}))
    summary = validation.validate(path)
    assert summary["sourceBytes"] == len(BOM_SOURCE)
    assert summary["byteCoveragePercent"] == [100.0]


def test_a_file_that_is_nothing_but_a_byte_order_mark_still_accounts_for_it():
    body = lexer.BOM.encode("latin-1")
    index = make_index(pyc=False)
    closure, _, document, binary = document_for(index, {PY_PATH: body})
    assert extension_of(document)["source"]["lines"] == []
    validation.validate_document(
        document, binary, source_members=closure.members(), member_exists=resolver(index)
    )


def test_a_dependency_keeps_the_path_the_referrer_authored():
    body = b'model = "MODELS/Scenery/Misc/TrashCan01.MDL"\r\n'
    index = make_index(pyc=False)
    _, model = model_for(index, {PY_PATH: body})
    row = model.dependencies[0]
    assert row["asset"] == "vtmb:model:scenery/misc/trashcan01"
    assert row["sourcePath"] == "MODELS/Scenery/Misc/TrashCan01.MDL"
    assert row["resolved"] is True


def test_export_time_validation_refuses_a_reference_that_lies_about_its_index():
    index = make_index()
    closure, _, document, binary = document_for(index)
    validation.validate_document(
        document, binary, source_members=closure.members(), member_exists=resolver(index)
    )
    for field, value in (("resolved", False), ("sourcePath", "models/invented.mdl")):
        tampered = copy.deepcopy(document)
        root = extension_of(tampered)
        model_reference = [
            row for row in root["references"] if row["kind"] == "model"
        ][0]
        model_reference[field] = value
        for row in root["dependencies"]:
            if row["role"] == "model":
                row[field] = value
        with pytest.raises(validation.ScriptGlbValidationError):
            validation.validate_document(
                tampered, binary,
                source_members=closure.members(),
                member_exists=resolver(index),
            )


def test_standalone_validation_refuses_a_dependency_its_references_do_not_produce():
    _, _, document, binary = document_for()
    tampered = copy.deepcopy(document)
    for row in extension_of(tampered)["dependencies"]:
        if row["role"] == "model":
            row["sourcePath"] = "models/invented.mdl"
    with pytest.raises(validation.ScriptGlbValidationError):
        validation.validate_document(tampered, binary)


# -- what the compiler emits and no author wrote ------------------------------------------------

DRIFT_SOURCE = b'"""doc line\r\nsecond line"""\r\nfrom __main__ import G\r\nclass Helper:\r\n\tpass\r\n'


def drift_pyc(*, extra_consts=(), lambda_body: bool = False) -> bytes:
    body = _code(
        code=bytes([100, 0, 0, 83]), consts=[b"N"], name=b"Helper", firstlineno=4
    )
    consts = [
        _string(b"doc line\nsecond line"),
        _tuple([_string(b"G")]),
        _string(b"Helper"),
        body,
        b"N",
    ]
    if lambda_body:
        consts.append(
            _code(code=bytes([100, 0, 0, 83]), consts=[b"N"], name=b"<lambda>", firstlineno=6)
        )
    consts.extend(_string(value) for value in extra_consts)
    code = bytes(
        [100, 0, 0]           # LOAD_CONST the docstring
        + [90, 3, 0]          # STORE_NAME __doc__
        + [100, 1, 0]         # LOAD_CONST the from list ('G',)
        + [107, 0, 0]         # IMPORT_NAME __main__
        + [108, 1, 0]         # IMPORT_FROM G
        + [90, 1, 0]          # STORE_NAME G
        + [1]                 # POP_TOP
        + [100, 2, 0]         # LOAD_CONST 'Helper'
        + [102, 0, 0]         # BUILD_TUPLE 0
        + [100, 3, 0]         # LOAD_CONST the class body
        + [132, 0, 0]         # MAKE_FUNCTION 0
        + [131, 0, 0]         # CALL_FUNCTION 0
        + [89]                # BUILD_CLASS
        + [90, 2, 0]          # STORE_NAME Helper
        + [100, 4, 0]         # LOAD_CONST None
        + [83]                # RETURN_VALUE
    )
    root = _code(
        code=code,
        consts=consts,
        names=[_string(b"__main__"), _string(b"G"), _string(b"Helper"), _string(b"__doc__")],
        name=b"?",
    )
    return _pyc(root)


def test_the_constants_the_compiler_emitted_are_no_disagreement_with_the_source():
    index = make_index(pyc_entry=PYC_LOOSE_ENTRY)
    _, model = model_for(index, {PY_PATH: DRIFT_SOURCE, PYC_PATH: drift_pyc()})
    # The from list, the class name and a docstring stored with LF are the compiler's own
    # constants, not literals the author left out.
    assert [row["role"] for row in model.anomalies] == []


def test_a_module_level_lambda_is_no_top_level_definition():
    index = make_index(pyc_entry=PYC_LOOSE_ENTRY)
    _, model = model_for(index, {PY_PATH: DRIFT_SOURCE, PYC_PATH: drift_pyc(lambda_body=True)})
    assert [row["role"] for row in model.anomalies] == []


def test_a_constant_no_literal_spells_is_still_a_disagreement():
    index = make_index(pyc_entry=PYC_LOOSE_ENTRY)
    _, model = model_for(
        index, {PY_PATH: DRIFT_SOURCE, PYC_PATH: drift_pyc(extra_consts=[b"unspoken"])}
    )
    rows = [row for row in model.anomalies if row["aspect"] == "constants"]
    assert rows and rows[0]["pycOnly"] == ["unspoken"]


def test_a_name_defined_twice_agrees_with_whichever_definition_was_compiled():
    later = module_pyc(talk_line=line_of("def talk(who):"))
    _, model = model_for(bodies={PY_PATH: SOURCE, PYC_PATH: later})
    assert "pyc-source-drift" not in [row["role"] for row in model.anomalies]
    assert "duplicate-definition" in [row["role"] for row in model.anomalies]


# ----------------------------------------------------------------------------------------------
# a module-qualified call, the evidence an anomaly carries, and what validation re-derives
# ----------------------------------------------------------------------------------------------


QUALIFIED_SOURCE = (
    b"import __main__\r\n"
    b"Find = __main__.FindEntityByName\r\n"
    b'steam = __main__.FindEntityByName( "SteamHurt" )\r\n'
    b'crowd = __main__.Finds("ValveSteam1")\r\n'
    b'door = Find("door_01")\r\n'
    b'other = zone.FindEntityByName("not_a_lookup")\r\n'
)


def test_a_module_qualified_lookup_is_the_same_call_written_two_ways():
    index = make_index(pyc=False)
    _, model = model_for(index, {PY_PATH: QUALIFIED_SOURCE})
    assert [(record.name, record.call) for record in model.entity_names] == [
        ("SteamHurt", "__main__.FindEntityByName"),
        ("ValveSteam1", "__main__.Finds"),
        ("door_01", "Find"),
    ]


def test_a_lookup_qualified_by_anything_but_the_module_is_some_other_objects_method():
    index = make_index(pyc=False)
    _, model = model_for(index, {PY_PATH: QUALIFIED_SOURCE})
    assert "not_a_lookup" not in [record.name for record in model.entity_names]


def test_a_qualified_lookup_survives_export_time_and_standalone_validation(tmp_path):
    index = make_index(pyc=False)
    bodies = {PY_PATH: QUALIFIED_SOURCE}
    closure, _, document, binary = document_for(index, bodies)
    validation.validate_document(
        document, binary, source_members=closure.members(), member_exists=resolver(index)
    )
    summary = validation.validate(
        script_glb.export(index, KEY, tmp_path, read_bytes=reader(bodies))
    )
    assert summary["entityNames"] == 3


def test_a_byte_order_mark_is_a_mapped_datum_and_no_departure_from_the_format():
    index = make_index(pyc=False)
    _, model = model_for(index, {PY_PATH: BOM_SOURCE})
    assert list(model.anomalies) == []
    # The mark declares the encoding; nothing else in the member does.
    assert model.source["encoding"] == "utf-8"


def test_the_indent_anomaly_names_the_lines_that_prove_the_mix():
    body = (
        b"def a():\r\n    return 1\r\n\r\ndef b():\r\n    return 2\r\n"
        b"\r\ndef c():\r\n\treturn 3\r\n"
    )
    index = make_index(pyc=False)
    _, model = model_for(index, {PY_PATH: body})
    row = [row for row in model.anomalies if row["role"] == "tab-space-indent-mix"][0]
    assert row["lines"] == [8]
    assert (row["tabLines"], row["spaceLines"]) == (1, 2)


def test_a_def_and_a_class_of_one_name_shadow_each_other():
    body = b"def A():\r\n\tpass\r\n\r\nclass A:\r\n\tpass\r\n"
    index = make_index(pyc=False)
    _, model = model_for(index, {PY_PATH: body})
    assert model.structure["classes"][0]["shadows"] == {"kind": "function", "index": 0}
    rows = [row for row in model.anomalies if row["role"] == "duplicate-definition"]
    assert [(row["kind"], row["name"], row["line"]) for row in rows] == [("class", "A", 4)]


def test_script_dirs_widens_the_index_with_the_trees_the_seam_reads():
    assert source_module.script_dirs(("materials", "models")) == (
        "materials", "models", "python", "sound", "dlg"
    )
    assert source_module.script_dirs(("python", "models")) == (
        "python", "models", "sound", "dlg"
    )


def test_export_time_validation_re_derives_the_anomaly_table_from_the_members():
    index = make_index()
    closure, _, document, binary = document_for(index)
    assert "duplicate-definition" in [
        row["role"] for row in extension_of(document)["anomalies"]
    ]
    for mutate in (
        lambda rows: rows.pop(0),
        lambda rows: rows.append({"role": "pyc-magic-mismatch", "expected": 60202}),
    ):
        tampered = copy.deepcopy(document)
        mutate(extension_of(tampered)["anomalies"])
        with pytest.raises(validation.ScriptGlbValidationError):
            validation.validate_document(
                tampered,
                binary,
                source_members=closure.members(),
                member_exists=resolver(index),
            )


def test_export_time_validation_re_derives_the_completeness_counters():
    index = make_index(pyc_entry=PYC_LOOSE_ENTRY)
    unknown = _pyc(_code(code=bytes([1, 250, 0, 0, 83]), consts=[b"N"], name=b"?"))
    bodies = {PY_PATH: SOURCE, PYC_PATH: unknown + b"\x01\x02"}
    closure, model, document, binary = document_for(index, bodies)
    assert model.coverage["typedUnidentified"] and model.coverage["omittedProven"]
    validation.validate_document(
        document, binary, source_members=closure.members(), member_exists=resolver(index)
    )
    for key in ("typedUnidentified", "omittedProven"):
        tampered = copy.deepcopy(document)
        extension_of(tampered)["coverage"][key] = []
        with pytest.raises(validation.ScriptGlbValidationError):
            validation.validate_document(
                tampered,
                binary,
                source_members=closure.members(),
                member_exists=resolver(index),
            )


def test_export_time_validation_re_derives_the_omission_table():
    index = make_index(pyc=False)
    bodies = {PY_PATH: b""}
    closure, _, document, binary = document_for(index, bodies)
    validation.validate_document(
        document, binary, source_members=closure.members(), member_exists=resolver(index)
    )
    tampered = copy.deepcopy(document)
    extension_of(tampered)["omissions"] = []
    with pytest.raises(validation.ScriptGlbValidationError):
        validation.validate_document(
            tampered, binary, source_members=closure.members(), member_exists=resolver(index)
        )
