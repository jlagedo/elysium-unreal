"""Decode one script closure into the records the unit publishes.

Everything here is read from the members the closure resolved: the source is tokenized under the
Python 2.1 grammar, the module's syntactic structure is derived from that token stream, the
literals that name install members become references, and the compiled companion is unmarshalled
and disassembled. Nothing is compiled and nothing is executed -- where a `.pyc` and its `.py`
sibling disagree, the disagreement is recorded rather than resolved.
"""

from __future__ import annotations

import re
from typing import Any, Callable, Iterable, Sequence

from elysium_pipeline.formats.script_glb import lexer, pyc as pyc_reader
from elysium_pipeline.formats.script_glb.model import (
    ArgumentRecord,
    AssignmentRecord,
    ClassRecord,
    EntityNameRecord,
    FunctionRecord,
    ImportRecord,
    ReferenceRecord,
    ScriptToken,
    SourceLine,
)
from elysium_pipeline.formats.unit_contract import dependency, missing_sentinel, normalize_key

#: The two entity lookups the script surface offers, and the aliases every level script binds
#: them to at module scope.
ENTITY_LOOKUPS = {
    "FindEntityByName": "FindEntityByName",
    "FindEntitiesByName": "FindEntitiesByName",
    "Find": "FindEntityByName",
    "Finds": "FindEntitiesByName",
}

#: The module the engine executes a level script inside, and the only qualifier a call to the
#: script surface can carry: `__main__.FindEntityByName("x")` and `FindEntityByName("x")` are one
#: call written two ways.
MODULE_QUALIFIER = "__main__"

#: Why a member the install resolves as zero bytes yields no record, keyed by its role.
EMPTY_MEMBER_REASONS = {
    "py": "the install resolves the executed source as zero bytes",
    "pyc": "the install resolves the compiled companion as zero bytes",
}

#: The calls whose first argument is a sound path even though the path names no directory.
SOUND_CALLS = ("PlayDialogFile", "PlaySound")

#: The two spellings `seam_map_sound.md` publishes a unit for. A member below `sound/` under any
#: other extension -- the `.lip` lipsync companions the patch probes for -- is no sound unit, so
#: naming one `vtmb:sound:` would invent an identity the corpus will never hold.
SOUND_EXTENSIONS = (".wav", ".mp3")

#: A `%` conversion makes the literal a template the script completes at run time, so no install
#: member has that name and none ever will.
_CONVERSION = re.compile(r"%(?:\([^)]*\))?[-#0 +]*[0-9*]*(?:\.[0-9*]+)?[hlL]?[diouxXeEfFgGcrs]")

#: What a name the source binds looks like. The compiler also names code objects things no
#: source name can be -- `<lambda>` is the one this corpus holds.
_IDENTIFIER = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")

_IGNORED = frozenset({"COMMENT", "NL"})
_BLOCK_KEYWORDS = frozenset(
    {"if", "elif", "else", "while", "for", "try", "except", "finally", "def", "class"}
)


class ScriptDecodeError(ValueError):
    """The script's members do not decode into a complete unit."""


# ----------------------------------------------------------------------------------------------
# source block
# ----------------------------------------------------------------------------------------------


def _line_ending(lines: Sequence[lexer.Line]) -> str:
    kinds = {line.terminator for line in lines if line.terminator}
    if not kinds:
        return "none"
    if len(kinds) == 1:
        return next(iter(kinds))
    return "mixed"


def describe_source(data: bytes) -> tuple[dict[str, Any], list[SourceLine], list[dict[str, Any]]]:
    """The `source` block, its line spans and the departures the bytes themselves carry."""

    text = lexer.decode_text(data)
    has_bom = text.startswith(lexer.BOM)
    lines = lexer.split_lines(text)
    anomalies: list[dict[str, Any]] = []
    ending = _line_ending(lines)
    if ending == "mixed":
        anomalies.append(
            {
                "role": "mixed-line-endings",
                "terminators": sorted({line.terminator for line in lines if line.terminator}),
                "lines": [line.index + 1 for line in lines if line.terminator == "lf"][:16],
            }
        )
    # The mark is a mapped datum with its own ledger owner, not a departure from the format, so
    # the scan for stray high bytes starts after it.
    body = len(lexer.BOM) if has_bom else 0
    for offset in range(body, len(data)):
        value = data[offset]
        if value > 0x7F:
            anomalies.append(
                {
                    "role": "non-ascii-byte",
                    "sourceOffset": offset,
                    "byte": value,
                    "latin1": chr(value),
                }
            )
    # The byte-order mark is its own ledger owner, so the line it opens starts after it and the
    # span, the ledger range and the validator's cursor all name the same place.
    records: list[SourceLine] = []
    for line in lines:
        offset, length = line.offset, line.length
        if has_bom and offset < len(lexer.BOM):
            shift = len(lexer.BOM) - offset
            offset += shift
            length -= shift
        if length <= 0:
            continue
        records.append(SourceLine(len(records), offset, length, line.terminator))
    block = {
        # A mark is the member's own declaration of its encoding; without one the bytes say it.
        "encoding": (
            "utf-8"
            if has_bom
            else ("ascii" if all(value < 0x80 for value in data) else "latin-1")
        ),
        "lineEnding": ending,
        "bom": {"offset": 0, "length": len(lexer.BOM)} if has_bom else None,
        "byteLength": len(data),
        "lineCount": len(records),
        "lines": [record.to_json() for record in records],
    }
    return block, records, anomalies


def statement_start_lines(tokens: Sequence[lexer.Token]) -> set[int]:
    """The 1-based physical lines a statement begins on.

    Only those lines carry block indentation: the leading whitespace of a bracket-continuation
    line lines an expression up, and CPython's tokenizer never measures it.
    """

    statements, _ = _statements(tokens)
    return {tokens[statement.indexes[0]].line for statement in statements}


def indent_anomaly(
    lines: Sequence[lexer.Line], statement_lines: set[int]
) -> list[dict[str, Any]]:
    """One row when the module indents its blocks with both tabs and spaces."""

    indented = [
        line for line in lines if line.indent and (line.index + 1) in statement_lines
    ]
    characters = sorted({char for line in indented for char in line.indent})
    if len(characters) < 2:
        return []
    tab_lines = [line.index + 1 for line in indented if "\t" in line.indent]
    space_lines = [line.index + 1 for line in indented if " " in line.indent]
    # The evidence is the lines that prove the mix, so the row names the minority spelling: the
    # module indents its blocks one way except on these, and a line that mixes both within its
    # own indent is in that list whichever way the count falls.
    minority = tab_lines if len(tab_lines) <= len(space_lines) else space_lines
    return [
        {
            "role": "tab-space-indent-mix",
            "characters": characters,
            "lines": minority[:16],
            "tabLines": len(tab_lines),
            "spaceLines": len(space_lines),
        }
    ]


def verify_token_partition(text: str, tokens: Sequence[lexer.Token]) -> None:
    """Every byte the token stream does not carry is insignificant whitespace.

    The unit's ledger runs over lines, not tokens, so this is what proves the token stream read
    the whole module rather than the part of it the tokenizer happened to recognise.
    """

    cursor = 0
    for token in tokens:
        if token.offset > cursor:
            gap = text[cursor:token.offset]
            stripped = gap.replace(" ", "").replace("\t", "").replace("\f", "")
            stripped = stripped.replace("\\\r\n", "").replace("\\\n", "").replace("\\\r", "")
            if stripped.replace(lexer.BOM, ""):
                raise ScriptDecodeError(
                    f"byte {cursor}: {gap!r} lies between two tokens and is not whitespace"
                )
        cursor = max(cursor, token.end)
    if text[cursor:].strip(" \t\f\r\n"):
        raise ScriptDecodeError(f"byte {cursor}: the module ends in bytes no token carries")


# ----------------------------------------------------------------------------------------------
# structure
# ----------------------------------------------------------------------------------------------


def _spell(tokens: Sequence[lexer.Token], indexes: Iterable[int]) -> str:
    """The source spelling of a token run, spaced only where two names would otherwise merge."""

    out: list[str] = []
    for index in indexes:
        piece = tokens[index].string
        if out and (out[-1][-1:].isalnum() or out[-1][-1:] == "_") and (
            piece[:1].isalnum() or piece[:1] == "_"
        ):
            out.append(" ")
        out.append(piece)
    return "".join(out)


class _Statement:
    """One simple or compound-header statement: its tokens and the block depth it sits at."""

    __slots__ = ("indexes", "level", "line")

    def __init__(self, indexes: list[int], level: int, line: int) -> None:
        self.indexes = indexes
        self.level = level
        self.line = line


def _statements(tokens: Sequence[lexer.Token]) -> tuple[list[_Statement], dict[int, int]]:
    """Split the token stream into statements, and say what block depth each token sits at."""

    statements: list[_Statement] = []
    levels: dict[int, int] = {}
    level = 0
    depth = 0
    current: list[int] = []
    for index, token in enumerate(tokens):
        levels[index] = level
        if token.type == "INDENT":
            level += 1
            continue
        if token.type == "DEDENT":
            level -= 1
            levels[index] = level
            continue
        if token.type in _IGNORED:
            continue
        if token.type in ("NEWLINE", "ENDMARKER"):
            if current:
                statements.append(_Statement(current, level, tokens[current[0]].line))
                current = []
            continue
        if token.type == "OP" and token.string in "([{":
            depth += 1
        elif token.type == "OP" and token.string in ")]}":
            depth = max(0, depth - 1)
        if token.type == "OP" and token.string == ";" and depth == 0:
            if current:
                statements.append(_Statement(current, level, tokens[current[0]].line))
                current = []
            continue
        if token.type == "OP" and token.string == ":" and depth == 0 and current:
            head = tokens[current[0]]
            if head.type == "NAME" and head.string in _BLOCK_KEYWORDS:
                current.append(index)
                statements.append(_Statement(current, level, tokens[current[0]].line))
                current = []
                continue
        current.append(index)
    if current:
        statements.append(_Statement(current, level, tokens[current[0]].line))
    return statements, levels


def _split_top_level(tokens: Sequence[lexer.Token], indexes: Sequence[int], separator: str):
    """Split a token run at a separator that sits outside every bracket."""

    groups: list[list[int]] = [[]]
    depth = 0
    for index in indexes:
        token = tokens[index]
        if token.type == "OP" and token.string in "([{":
            depth += 1
        elif token.type == "OP" and token.string in ")]}":
            depth -= 1
        if depth == 0 and token.type == "OP" and token.string == separator:
            groups.append([])
            continue
        groups[-1].append(index)
    return groups


def _parse_parameters(
    tokens: Sequence[lexer.Token], indexes: Sequence[int]
) -> list[ArgumentRecord]:
    arguments: list[ArgumentRecord] = []
    for group in _split_top_level(tokens, indexes, ","):
        if not group:
            continue
        name_part = group
        default: str | None = None
        halves = _split_top_level(tokens, group, "=")
        if len(halves) > 1:
            name_part = halves[0]
            default = _spell(tokens, [i for half in halves[1:] for i in half])
        form = "positional"
        cursor = 0
        while cursor < len(name_part) and tokens[name_part[cursor]].string in ("*", "**"):
            form = "star" if tokens[name_part[cursor]].string == "*" else "double-star"
            cursor += 1
        arguments.append(
            ArgumentRecord(_spell(tokens, name_part[cursor:]), form, default)
        )
    return arguments


def _matching(tokens: Sequence[lexer.Token], indexes: Sequence[int], start: int) -> int:
    """The position in `indexes` of the bracket that closes the one at position `start`."""

    depth = 0
    for position in range(start, len(indexes)):
        text = tokens[indexes[position]].string
        if tokens[indexes[position]].type == "OP" and text in "([{":
            depth += 1
        elif tokens[indexes[position]].type == "OP" and text in ")]}":
            depth -= 1
            if depth == 0:
                return position
    raise ScriptDecodeError(f"line {tokens[indexes[start]].line}: a bracket never closes")


class _Block:
    __slots__ = ("kind", "name", "line", "token", "args", "defaults", "bases", "children",
                 "body_level", "end_line", "shadows")

    def __init__(self, kind: str, name: str, line: int, token: int, body_level: int) -> None:
        self.kind = kind
        self.name = name
        self.line = line
        self.token = token
        self.args: list[ArgumentRecord] = []
        self.defaults: list[str] = []
        self.bases: list[str] = []
        self.children: list[_Block] = []
        self.body_level = body_level
        self.end_line = line
        self.shadows: dict[str, Any] | None = None

    def to_function(self) -> FunctionRecord:
        return FunctionRecord(
            name=self.name,
            line_span=(self.line, self.end_line),
            token=self.token,
            args=tuple(self.args),
            defaults=tuple(self.defaults),
            nested=tuple(
                child.to_function() for child in self.children if child.kind == "function"
            ),
            shadows=self.shadows,
        )

    def to_class(self) -> ClassRecord:
        return ClassRecord(
            name=self.name,
            line_span=(self.line, self.end_line),
            token=self.token,
            bases=tuple(self.bases),
            methods=tuple(
                child.to_function() for child in self.children if child.kind == "function"
            ),
            shadows=self.shadows,
        )


def derive_structure(tokens: Sequence[lexer.Token]) -> dict[str, Any]:
    """The module's syntactic index, read from the token stream and nothing else.

    It is an index, not an execution model: a name defined twice appears twice, in order, and the
    later record names the earlier one in `shadows`.
    """

    statements, _ = _statements(tokens)
    imports: list[ImportRecord] = []
    assignments: list[AssignmentRecord] = []
    top: list[_Block] = []
    open_blocks: list[_Block] = []
    last_line = tokens[-1].line if tokens else 0

    def close_to(level: int, line: int) -> None:
        while open_blocks and open_blocks[-1].body_level > level:
            finished = open_blocks.pop()
            finished.end_line = max(finished.end_line, line)

    for statement in statements:
        indexes = statement.indexes
        head = tokens[indexes[0]]
        close_to(statement.level, last_line)
        last_line = tokens[indexes[-1]].line
        for block in open_blocks:
            block.end_line = max(block.end_line, last_line)
        container = open_blocks[-1].children if open_blocks else top

        if head.type == "NAME" and head.string == "def" and len(indexes) > 2:
            name = tokens[indexes[1]].string
            open_position = 2
            close_position = _matching(tokens, indexes, open_position)
            block = _Block("function", name, head.line, indexes[0], statement.level + 1)
            block.args = _parse_parameters(
                tokens, indexes[open_position + 1:close_position]
            )
            block.defaults = [
                argument.default for argument in block.args if argument.default is not None
            ]
            container.append(block)
            open_blocks.append(block)
            continue
        if head.type == "NAME" and head.string == "class" and len(indexes) > 1:
            name = tokens[indexes[1]].string
            block = _Block("class", name, head.line, indexes[0], statement.level + 1)
            if len(indexes) > 2 and tokens[indexes[2]].string == "(":
                close_position = _matching(tokens, indexes, 2)
                block.bases = [
                    _spell(tokens, group)
                    for group in _split_top_level(tokens, indexes[3:close_position], ",")
                    if group
                ]
            container.append(block)
            open_blocks.append(block)
            continue
        if head.type == "NAME" and head.string in ("import", "from"):
            imports.append(_parse_import(tokens, indexes))
            continue
        if not open_blocks and statement.level == 0:
            assignment = _parse_assignment(tokens, indexes)
            if assignment is not None:
                assignments.append(assignment)

    close_to(0, last_line)

    functions = [block for block in top if block.kind == "function"]
    classes = [block for block in top if block.kind == "class"]
    # One module namespace holds both, so a `def` and a `class` of the same name shadow each
    # other exactly as two `def`s do. `shadows` names the earlier definition by the list it is
    # published in and its ordinal there.
    ordinals = {"function": 0, "class": 0}
    seen: dict[str, dict[str, Any]] = {}
    for block in top:
        if block.kind not in ordinals:
            continue
        block.shadows = seen.get(block.name)
        seen[block.name] = {"kind": block.kind, "index": ordinals[block.kind]}
        ordinals[block.kind] += 1
    return {
        "imports": [record.to_json() for record in imports],
        "functions": [block.to_function().to_json() for block in functions],
        "classes": [block.to_class().to_json() for block in classes],
        "assignments": [record.to_json() for record in assignments],
    }


def _parse_import(tokens: Sequence[lexer.Token], indexes: Sequence[int]) -> ImportRecord:
    head = tokens[indexes[0]].string
    names: list[str] = []
    aliases: list[str] = []
    if head == "import":
        module_groups = _split_top_level(tokens, indexes[1:], ",")
        modules = []
        for group in module_groups:
            halves = [
                position
                for position, index in enumerate(group)
                if tokens[index].type == "NAME" and tokens[index].string == "as"
            ]
            if halves:
                modules.append(_spell(tokens, group[: halves[0]]))
                aliases.append(_spell(tokens, group[halves[0] + 1:]))
            else:
                modules.append(_spell(tokens, group))
        module = modules[0] if modules else ""
        names = modules
    else:
        split = [
            position
            for position, index in enumerate(indexes)
            if tokens[index].type == "NAME" and tokens[index].string == "import"
        ]
        boundary = split[0] if split else len(indexes)
        module = _spell(tokens, indexes[1:boundary])
        for group in _split_top_level(tokens, indexes[boundary + 1:], ","):
            if not group:
                continue
            positions = [
                position
                for position, index in enumerate(group)
                if tokens[index].type == "NAME" and tokens[index].string == "as"
            ]
            if positions:
                names.append(_spell(tokens, group[: positions[0]]))
                aliases.append(_spell(tokens, group[positions[0] + 1:]))
            else:
                names.append(_spell(tokens, group))
    return ImportRecord(
        kind="import" if head == "import" else "from",
        module=module,
        names=tuple(names),
        aliases=tuple(aliases),
        line=tokens[indexes[0]].line,
        token=indexes[0],
    )


def _parse_assignment(
    tokens: Sequence[lexer.Token], indexes: Sequence[int]
) -> AssignmentRecord | None:
    head = tokens[indexes[0]]
    if head.type == "NAME" and head.string in lexer.KEYWORDS:
        return None
    augmented = {"+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", ">>=", "<<=", "**="}
    depth = 0
    for position, index in enumerate(indexes):
        token = tokens[index]
        if token.type == "OP" and token.string in "([{":
            depth += 1
        elif token.type == "OP" and token.string in ")]}":
            depth -= 1
        elif depth == 0 and token.type == "OP" and token.string in augmented:
            return AssignmentRecord(
                targets=(_spell(tokens, indexes[:position]),),
                operator=token.string,
                line=head.line,
                token=indexes[0],
            )
    groups = _split_top_level(tokens, indexes, "=")
    if len(groups) < 2:
        return None
    targets: list[str] = []
    for group in groups[:-1]:
        for part in _split_top_level(tokens, group, ","):
            spelled = _spell(tokens, part)
            if spelled:
                targets.append(spelled)
    if not targets:
        return None
    return AssignmentRecord(
        targets=tuple(targets), operator="=", line=head.line, token=indexes[0]
    )


# ----------------------------------------------------------------------------------------------
# references and entity names
# ----------------------------------------------------------------------------------------------


def _authored_literal(value: str) -> str:
    """The path the referrer authored: its own spelling, on one separator, root-relative.

    The contract's `sourcePath` is what the referrer wrote, so nothing here folds case. Only the
    separator and a leading or doubled slash are unified, because those are spellings of one
    path rather than of one name.
    """

    authored = str(value).strip().replace("\\", "/")
    while authored.startswith("/"):
        authored = authored[1:]
    while "//" in authored:
        authored = authored.replace("//", "/")
    return authored


def _normalize_literal(value: str) -> str:
    """The joinable install key for a literal: the authored path, folded."""

    return normalize_key(_authored_literal(value))


def classify_literal(value: str) -> tuple[str, str, str, str] | None:
    """`(role, key, install key path, authored path)` for a literal that names a member.

    The folded key path is what the index is asked about and what the identity is built from;
    the authored path is what the referrer spelled and what `dependencies[].sourcePath` carries.
    """

    normalized = _normalize_literal(value)
    authored = _authored_literal(value)
    if not normalized or normalized.endswith("/"):
        return None
    if normalized.endswith(".dlg"):
        cut = 4 if normalized.startswith("dlg/") else 0
        stem = normalized[cut:]
        return "dialogue", stem[:-4], "dlg/" + stem, "dlg/" + authored[cut:]
    if normalized.startswith("maps/") and normalized.endswith(".bsp"):
        return "map", normalized[5:-4], normalized, authored
    if normalized.startswith("models/") and normalized.endswith(".mdl"):
        return "model", normalized[7:-4], normalized, authored
    if normalized.startswith("sound/") and normalized.endswith(SOUND_EXTENSIONS):
        return "sound", normalized[6:], normalized, authored
    if normalized.startswith("vdata/") and normalized.endswith(".txt"):
        return "vdata", normalized[6:-4], normalized, authored
    return None


def _is_template(value: str) -> bool:
    return bool(_CONVERSION.search(value))


def _sound_call_arguments(tokens: Sequence[lexer.Token]) -> dict[int, bool]:
    """Token index of a sound call's leading literal -> is that literal the whole argument."""

    found: dict[int, bool] = {}
    for index, token in enumerate(tokens):
        if token.type != "NAME" or token.string not in SOUND_CALLS:
            continue
        significant = [
            position
            for position in range(index + 1, len(tokens))
            if tokens[position].type not in _IGNORED
        ]
        if not significant or tokens[significant[0]].string != "(":
            continue
        depth = 0
        argument: list[int] = []
        for position in significant:
            text = tokens[position].string
            if tokens[position].type == "OP" and text in "([{":
                depth += 1
                if depth == 1:
                    continue
            elif tokens[position].type == "OP" and text in ")]}":
                depth -= 1
                if depth == 0:
                    break
            argument.append(position)
        if argument and tokens[argument[0]].type == "STRING":
            found[argument[0]] = len(argument) == 1
    return found


def entity_aliases(tokens: Sequence[lexer.Token], structure: dict[str, Any]) -> dict[str, str]:
    """The names this module calls the two entity lookups by.

    Every level script rebinds them at module scope (`Find = __main__.FindEntityByName`), so the
    alias table is read out of the module's own assignments rather than assumed.
    """

    aliases = dict(ENTITY_LOOKUPS)
    for record in structure.get("assignments") or []:
        targets = record.get("targets") or []
        if len(targets) != 1:
            continue
        index = int(record.get("token", -1))
        run = []
        for position in range(index, len(tokens)):
            if tokens[position].type in ("NEWLINE", "ENDMARKER"):
                break
            if tokens[position].type not in _IGNORED:
                run.append(position)
        spelled = _spell(tokens, run)
        for canonical in ("FindEntitiesByName", "FindEntityByName"):
            if spelled.endswith("=" + canonical) or spelled.endswith("." + canonical):
                aliases[str(targets[0])] = canonical
                break
    return aliases


def _previous_significant(tokens: Sequence[lexer.Token], index: int) -> int | None:
    """The token before `index` that carries meaning, comments and blank lines skipped."""

    for position in range(index - 1, -1, -1):
        if tokens[position].type not in _IGNORED:
            return position
    return None


def collect_entity_names(
    tokens: Sequence[lexer.Token], aliases: dict[str, str]
) -> list[EntityNameRecord]:
    """Every literal handed to an entity lookup, with the spelling of the call that took it."""

    records: list[EntityNameRecord] = []
    for index, token in enumerate(tokens):
        if token.type != "NAME" or token.string not in aliases:
            continue
        call = token.string
        previous = _previous_significant(tokens, index)
        attribute = previous is not None and tokens[previous].type == "OP"
        if attribute and tokens[previous].string == ".":
            qualifier = _previous_significant(tokens, previous)
            # The engine executes a level script inside `__main__`, so the corpus writes the
            # lookup both bare and module-qualified and both are the same call. A qualifier that
            # is anything else is some other object's method and no entity lookup.
            if (
                qualifier is None
                or tokens[qualifier].type != "NAME"
                or tokens[qualifier].string != MODULE_QUALIFIER
            ):
                continue
            call = tokens[qualifier].string + "." + token.string
        following = [
            position
            for position in range(index + 1, min(index + 4, len(tokens)))
            if tokens[position].type not in _IGNORED
        ]
        if len(following) < 2:
            continue
        if tokens[following[0]].string != "(" or tokens[following[1]].type != "STRING":
            continue
        literal = tokens[following[1]]
        records.append(
            EntityNameRecord(
                token=following[1],
                name=lexer.string_value(literal.string),
                call=call,
                line=literal.line,
                source_offset=literal.offset,
            )
        )
    return records


def collect_references(
    tokens: Sequence[lexer.Token],
    member_exists: Callable[[str], bool],
) -> list[ReferenceRecord]:
    """Every string literal that names an install member, classified and resolved."""

    sound_arguments = _sound_call_arguments(tokens)
    records: list[ReferenceRecord] = []
    for index, token in enumerate(tokens):
        if token.type != "STRING":
            continue
        value = lexer.string_value(token.string)
        classified = classify_literal(value)
        reason: str | None = None
        if classified is None and index in sound_arguments:
            normalized = _normalize_literal(value)
            if not normalized:
                continue
            classified = (
                "sound", normalized, "sound/" + normalized, "sound/" + _authored_literal(value)
            )
            if not sound_arguments[index]:
                reason = (
                    "the argument continues past this literal, so the script composes the "
                    "sound path at run time and no install member carries this name"
                )
            elif not normalized.endswith(SOUND_EXTENSIONS):
                reason = (
                    "the literal names no .wav or .mp3, so the sound seam publishes no unit "
                    "the call could reach"
                )
        if classified is None:
            continue
        role, key, path, authored = classified
        if reason is None and _is_template(value):
            reason = (
                "the literal carries a printf conversion the script fills in at run time, so "
                "no install member carries this name"
            )
        if reason is not None:
            records.append(
                ReferenceRecord(
                    token=index,
                    literal=value,
                    kind=role,
                    asset=missing_sentinel(role, key),
                    source_path=authored,
                    resolved=False,
                    sentinel_reason=reason,
                    source_offset=token.offset,
                )
            )
            continue
        records.append(
            ReferenceRecord(
                token=index,
                literal=value,
                kind=role,
                asset=f"vtmb:{role}:{key}",
                source_path=authored,
                resolved=bool(member_exists(path)),
                sentinel_reason=None,
                source_offset=token.offset,
            )
        )
    return records


def build_dependencies(references: Sequence[ReferenceRecord]) -> list[dict[str, Any]]:
    """One row per referenced identity. A sentinel names no unit, so it produces no row."""

    rows: dict[str, dict[str, Any]] = {}
    for reference in references:
        if reference.sentinel_reason is not None:
            continue
        if reference.asset in rows:
            continue
        rows[reference.asset] = dependency(
            reference.kind, reference.asset, reference.source_path, reference.resolved
        )
    return [rows[asset] for asset in sorted(rows)]


# ----------------------------------------------------------------------------------------------
# the compiled companion
# ----------------------------------------------------------------------------------------------


def _pyc_top_level(code: dict[str, Any]) -> list[dict[str, Any]]:
    """The module's compiled `def`/`class` bodies, by the name each was compiled under.

    A `lambda` compiles to a code object too and the compiler names it `<lambda>`, which is no
    definition the source binds a top-level name to, so it is not one of these.
    """

    return [
        {"name": const.get("name"), "firstlineno": const.get("firstlineno")}
        for const in code.get("consts") or []
        if isinstance(const, dict)
        and "instructions" in const
        and _IDENTIFIER.match(str(const.get("name") or ""))
    ]


def _compiler_emitted_strings(code: dict[str, Any]) -> set[str]:
    """The string constants one code object carries because the compiler put them there.

    None of these is a literal the source spells, so subtracting them is what keeps a constants
    disagreement evidence-backed: the `from` list of an import, and the name a class body is
    built under, are emitted by `IMPORT_NAME` and `BUILD_CLASS` and by nothing the author wrote.
    """

    emitted: set[str] = set()
    instructions = list(code.get("instructions") or [])
    for position, instruction in enumerate(instructions):
        name = instruction.get("name")
        if name == "IMPORT_NAME" and position:
            previous = instructions[position - 1]
            if previous.get("name") != "LOAD_CONST":
                continue
            value = previous.get("argValue")
            if isinstance(value, str):
                emitted.add(value)
            elif isinstance(value, list):
                emitted.update(item for item in value if isinstance(item, str))
        elif name == "BUILD_CLASS":
            # `class X(bases):` pushes 'X', its base tuple and the body code object; the body
            # carries the same name, so naming it once covers the string the compiler emitted.
            for earlier in reversed(instructions[max(0, position - 4):position]):
                value = earlier.get("argValue")
                if earlier.get("name") == "LOAD_CONST" and isinstance(value, dict):
                    if isinstance(value.get("code"), str):
                        emitted.add(value["code"])
                    break
    return emitted


def source_string_constants(tokens: Sequence[lexer.Token]) -> set[str]:
    """Every string constant the source spells, implicit concatenation included.

    The 2.1 grammar joins adjacent string literals at compile time, so a message the author wrote
    across two lines is two tokens here and one constant in the compiled twin. Every contiguous
    join of a run of adjacent literals is therefore a constant the source spells.
    """

    values: set[str] = set()
    run: list[str] = []

    def flush() -> None:
        for start in range(len(run)):
            joined = ""
            for end in range(start, len(run)):
                joined += run[end]
                values.add(joined)
        run.clear()

    for token in tokens:
        if token.type in _IGNORED:
            continue
        if token.type == "STRING":
            run.append(lexer.string_value(token.string))
            continue
        flush()
    flush()
    return values


def _fold_newlines(value: str) -> str:
    """A line ending as the compiler stores it.

    The tokenizer reads a triple-quoted docstring with the file's CRLF inside it and the compiler
    stores the same docstring with LF, so the two spell one constant on two line endings.
    """

    return value.replace("\r\n", "\n").replace("\r", "\n")


def _source_literal_strings(code: dict[str, Any]) -> set[str]:
    """Every string constant of the whole code tree a source literal could have spelled."""

    out: set[str] = set()
    for node in pyc_reader.walk_codes(code):
        emitted = _compiler_emitted_strings(node)
        for const in node.get("consts") or []:
            values = [const] if isinstance(const, str) else (
                [item for item in const if isinstance(item, str)]
                if isinstance(const, list) else []
            )
            out.update(_fold_newlines(value) for value in values if value not in emitted)
    return out


def compare_pyc_to_source(
    code: dict[str, Any],
    structure: dict[str, Any],
    literals: set[str],
) -> list[dict[str, Any]]:
    """Every disagreement between the compiled twin and the source, with both sides.

    The export compiles nothing: a `.pyc` older than the `.py` beside it is a fact of the
    install, and the unit records what the two say rather than picking one.
    """

    anomalies: list[dict[str, Any]] = []
    compiled = _pyc_top_level(code)
    compiled_names = [str(row["name"]) for row in compiled if row["name"]]
    source_names = [
        str(row["name"]) for row in (structure.get("functions") or [])
    ] + [str(row["name"]) for row in (structure.get("classes") or [])]
    if set(compiled_names) != set(source_names):
        anomalies.append(
            {
                "role": "pyc-source-drift",
                "aspect": "top-level-names",
                "pycOnly": sorted(set(compiled_names) - set(source_names)),
                "sourceOnly": sorted(set(source_names) - set(compiled_names)),
            }
        )
    source_lines: dict[str, list[int]] = {}
    for row in (structure.get("functions") or []) + (structure.get("classes") or []):
        source_lines.setdefault(str(row["name"]), []).append(int(row["lineSpan"][0]))
    disagreeing = []
    seen: dict[str, int] = {}
    for row in compiled:
        name = str(row["name"])
        candidates = source_lines.get(name)
        if not candidates:
            continue
        ordinal = seen.get(name, 0)
        seen[name] = ordinal + 1
        # A name the module defines twice compiles to one code object per definition, so the
        # compiled line agrees as soon as any definition of that name is written there.
        if int(row["firstlineno"] or 0) in candidates:
            continue
        disagreeing.append(
            {
                "name": name,
                "pyc": row["firstlineno"],
                "source": candidates[min(ordinal, len(candidates) - 1)],
            }
        )
    if disagreeing:
        anomalies.append(
            {"role": "pyc-source-drift", "aspect": "firstlineno", "names": disagreeing}
        )
    compiled_strings = _source_literal_strings(code)
    missing = sorted(compiled_strings - {_fold_newlines(value) for value in literals})
    if missing:
        anomalies.append(
            {
                "role": "pyc-source-drift",
                "aspect": "constants",
                "pycOnly": missing,
                "pycCount": len(compiled_strings),
                "sourceCount": len(literals),
            }
        )
    return anomalies


# ----------------------------------------------------------------------------------------------
# the whole unit
# ----------------------------------------------------------------------------------------------


def decode_tokens(data: bytes) -> tuple[list[lexer.Token], list[ScriptToken]]:
    """Tokenize one member and number the stream the unit publishes."""

    text = lexer.decode_text(data)
    try:
        raw = lexer.tokenize(text)
    except lexer.ScriptLexError as error:
        raise ScriptDecodeError(str(error)) from error
    verify_token_partition(text, raw)
    published = [
        ScriptToken(index, token.type, token.string, token.line, token.col, token.offset)
        for index, token in enumerate(raw)
    ]
    return raw, published


def duplicate_definition_anomalies(structure: dict[str, Any]) -> list[dict[str, Any]]:
    """One row per top-level definition that shadows an earlier one of the same name."""

    rows: list[dict[str, Any]] = []
    for kind, key in (("function", "functions"), ("class", "classes")):
        for row in structure.get(key) or []:
            if row.get("shadows") is None:
                continue
            rows.append(
                {
                    "role": "duplicate-definition",
                    "kind": kind,
                    "name": row["name"],
                    "line": row["lineSpan"][0],
                    "shadows": row["shadows"],
                }
            )
    rows.sort(key=lambda row: row["line"])
    return rows


def empty_member_omissions(members: Sequence[Any]) -> list[dict[str, Any]]:
    """One row per resolved member the install holds no bytes for, in member-table order."""

    return [
        {
            "role": "empty-member",
            "path": member.path,
            "reason": EMPTY_MEMBER_REASONS[member.role],
        }
        for member in members
        if member.role in EMPTY_MEMBER_REASONS and not member.data
    ]


def source_anomalies(
    data: bytes, tokens: Sequence[lexer.Token], structure: dict[str, Any]
) -> list[dict[str, Any]]:
    """Every departure the source member carries, in the order the unit publishes them.

    One function so that a reader re-deriving the anomaly table from the member it re-read gets
    the same list the writer published, row for row.
    """

    _, _, anomalies = describe_source(data)
    anomalies.extend(
        indent_anomaly(
            lexer.split_lines(lexer.decode_text(data)), statement_start_lines(tokens)
        )
    )
    anomalies.extend(duplicate_definition_anomalies(structure))
    return anomalies


def decode_script(
    closure,
    *,
    member_exists: Callable[[str], bool] = lambda path: False,
) -> "ScriptModel":
    """Decode one script closure completely into the records the unit publishes."""

    from elysium_pipeline.formats.script_glb import coverage as coverage_module
    from elysium_pipeline.formats.script_glb.model import ScriptModel

    anomalies: list[dict[str, Any]] = []
    omissions: list[dict[str, Any]] = []
    ledgers: list[dict[str, Any]] = []
    typed_unidentified: list[dict[str, Any]] = []
    omitted_proven: list[dict[str, Any]] = []

    source_block: dict[str, Any] = {
        "encoding": None,
        "lineEnding": "none",
        "bom": None,
        "byteLength": 0,
        "lineCount": 0,
        "lines": [],
    }
    published_tokens: list[ScriptToken] = []
    raw_tokens: list[lexer.Token] = []
    structure: dict[str, Any] = {
        "imports": [], "functions": [], "classes": [], "assignments": []
    }
    references: list[ReferenceRecord] = []
    entity_names: list[EntityNameRecord] = []

    if closure.py is not None:
        source_block, lines, _ = describe_source(closure.py.data)
        raw_tokens, published_tokens = decode_tokens(closure.py.data)
        structure = derive_structure(raw_tokens)
        anomalies.extend(source_anomalies(closure.py.data, raw_tokens, structure))
        references = collect_references(raw_tokens, member_exists)
        entity_names = collect_entity_names(
            raw_tokens, entity_aliases(raw_tokens, structure)
        )
        if not closure.py.data:
            omissions.append(
                {
                    "role": "empty-member",
                    "path": closure.py.path,
                    "reason": EMPTY_MEMBER_REASONS["py"],
                }
            )
        ledgers.append(
            coverage_module.source_ledger(
                closure.py, lines, source_block["bom"] is not None
            )
        )

    compiled_block: dict[str, Any] | None = None
    if closure.pyc is not None:
        if not closure.pyc.data:
            omissions.append(
                {
                    "role": "empty-member",
                    "path": closure.pyc.path,
                    "reason": EMPTY_MEMBER_REASONS["pyc"],
                }
            )
            ledgers.append(coverage_module.compiled_ledger(closure.pyc, ()))
        else:
            try:
                compiled = pyc_reader.decode_pyc(closure.pyc.data, closure.pyc.path)
            except pyc_reader.PycDecodeError as error:
                raise ScriptDecodeError(str(error)) from error
            anomalies.extend(compiled.anomalies)
            typed_unidentified.extend(compiled.typed_unidentified)
            omitted_proven.extend(compiled.omitted_proven)
            compiled_block = {
                "magic": compiled.magic,
                "magicBytes": compiled.magic_bytes,
                "magicExpected": compiled.magic_expected,
                "mtime": compiled.mtime,
                "code": compiled.code,
            }
            if closure.py is not None:
                anomalies.extend(
                    compare_pyc_to_source(
                        compiled.code, structure, source_string_constants(raw_tokens)
                    )
                )
            ledgers.append(coverage_module.compiled_ledger(closure.pyc, compiled.claims))

    coverage = coverage_module.build_coverage(
        ledgers=ledgers,
        has_pyc=compiled_block is not None,
        references=references,
        typed_unidentified=typed_unidentified,
        omitted_proven=omitted_proven,
    )
    return ScriptModel(
        key=closure.key,
        asset=closure.asset,
        source_kind=closure.source_kind,
        members=closure.members(),
        source=source_block,
        tokens=tuple(published_tokens),
        structure=structure,
        references=tuple(references),
        entity_names=tuple(entity_names),
        pyc=compiled_block,
        dependencies=tuple(build_dependencies(references)),
        anomalies=tuple(anomalies),
        omissions=tuple(omissions),
        coverage=coverage,
    )
