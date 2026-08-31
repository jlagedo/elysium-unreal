"""Independent structural validator for Script GLB products.

The writer is never consulted here. Where the install is present the members are re-read,
re-tokenized and re-unmarshalled from scratch and the result is weighed against what the unit
published; where it is absent the unit is checked against itself -- the line table against the
member length, the structure against the token stream that produced it, and every disassembled
instruction against the code length it claims to tile.
"""

from __future__ import annotations

from pathlib import Path
from typing import Any, Callable, Mapping, Sequence

from elysium_pipeline.formats.script_glb import lexer
from elysium_pipeline.formats.script_glb.coverage import sentinel_rows
from elysium_pipeline.formats.script_glb.decode import (
    build_dependencies,
    collect_entity_names,
    collect_references,
    compare_pyc_to_source,
    decode_tokens,
    derive_structure,
    describe_source,
    empty_member_omissions,
    entity_aliases,
    source_anomalies,
    source_string_constants,
)
from elysium_pipeline.formats.script_glb.model import (
    ANOMALY_ROLES,
    COMPILED_EXTENSION,
    DEPENDENCY_ROLES,
    SCHEMA_VERSION,
    SCRIPT_EXTENSION,
    SCRIPT_ROOT,
    SOURCE_EXTENSION,
    SOURCE_KINDS,
    ReferenceRecord,
)
from elysium_pipeline.formats.script_glb.pyc import PycDecodeError, decode_pyc, walk_codes
from elysium_pipeline.formats.unit_contract import (
    UnitValidationError,
    completeness,
    read_glb as _read_glb,
    validate_accessors,
    validate_capsules,
    validate_container,
    validate_extension_root,
    validate_ledgers,
    validate_sceneless,
    warnings_for as _contract_warnings,
)

ASSET_PREFIX = "vtmb:script:"

#: The roles a script unit's members fill, and the extension each one is read from.
MEMBER_EXTENSIONS = {"py": SOURCE_EXTENSION, "pyc": COMPILED_EXTENSION}

#: The token types the seam publishes; a type outside them is a tokenizer that widened itself.
TOKEN_TYPES = frozenset(lexer.TOKEN_TYPES)

#: How `identity.sourceKind` follows from the roles the member table carries.
KIND_FOR_ROLES = {
    ("py", "pyc"): "py+pyc",
    ("py",): "py",
    ("pyc",): "pyc-only",
}


class ScriptGlbValidationError(ValueError):
    """A published script unit contradicts the seam or the unit contract."""


def read_glb(path: Path) -> tuple[dict[str, Any], bytes]:
    return _read_glb(Path(path))


def _fail(message: str) -> None:
    raise ScriptGlbValidationError(message)


def _tokens_from(rows: Sequence[Mapping[str, Any]]) -> list[lexer.Token]:
    tokens: list[lexer.Token] = []
    for index, row in enumerate(rows):
        if not isinstance(row, Mapping):
            _fail(f"token {index} is not a record")
        if row.get("index") != index:
            _fail(f"token {index} is out of stream order")
        kind = str(row.get("type", ""))
        if kind not in TOKEN_TYPES:
            _fail(f"token {index} carries type {kind!r}, which the 2.1 grammar has no rule for")
        tokens.append(
            lexer.Token(
                kind,
                str(row.get("string", "")),
                int(row.get("line", 0)),
                int(row.get("col", 0)),
                int(row.get("offset", 0)),
            )
        )
    return tokens


def _check_identity(root: Mapping[str, Any]) -> tuple[str, str, list[Mapping[str, Any]]]:
    identity = root.get("identity") or {}
    asset = str(identity.get("asset", ""))
    key = str(identity.get("scriptPath", ""))
    if not key or asset != ASSET_PREFIX + key:
        _fail(f"identity {asset!r} disagrees with its script path {key!r}")
    if key != key.lower() or key.startswith("/") or key.endswith("/") or "\\" in key:
        _fail(f"the script key {key!r} is not a normalized install-relative path")
    if key.endswith(SOURCE_EXTENSION) or key.endswith(COMPILED_EXTENSION):
        _fail(f"the script key {key!r} keeps a source extension")
    members = list((root.get("sourceResolution") or {}).get("members") or [])
    if not members:
        _fail("a script unit is decoded from at least one member")
    roles = tuple(str(member.get("role")) for member in members)
    if len(set(roles)) != len(roles) or set(roles) - set(MEMBER_EXTENSIONS):
        _fail(f"the member table carries roles {list(roles)}")
    for member in members:
        role = str(member.get("role"))
        expected = SCRIPT_ROOT + key + MEMBER_EXTENSIONS[role]
        if str(member.get("path")) != expected:
            _fail(f"the {role} member is {member.get('path')!r}, not {expected!r}")
        if not isinstance(member.get("executed"), bool):
            _fail(f"the {role} member does not say whether the interpreter executes it")
        if role == "pyc" and (member.get("origin") or {}).get("kind") == "vpk":
            if member["executed"]:
                _fail("a companion inside a VPK cannot be executed: 2.1 has no zipimport")
    source_kind = str(identity.get("sourceKind", ""))
    if source_kind not in SOURCE_KINDS:
        _fail(f"identity.sourceKind is {source_kind!r}")
    if KIND_FOR_ROLES.get(tuple(sorted(roles))) != source_kind:
        _fail(f"identity.sourceKind {source_kind!r} disagrees with the member roles {list(roles)}")
    return asset, key, members


def _check_source(root: Mapping[str, Any], members: Sequence[Mapping[str, Any]]) -> int:
    """The line table partitions the source member exactly, byte-order mark included."""

    source = root.get("source")
    if not isinstance(source, Mapping):
        _fail("the unit publishes no source block")
    py = [member for member in members if str(member.get("role")) == "py"]
    if not py:
        if source.get("lines"):
            _fail("a companion-only unit publishes a line table for a source it has no member for")
        return 0
    length = int(py[0].get("byteLength", -1))
    if int(source.get("byteLength", -2)) != length:
        _fail("source.byteLength disagrees with the member it was read from")
    if source.get("lineEnding") not in ("crlf", "lf", "cr", "mixed", "none"):
        _fail(f"source.lineEnding is {source.get('lineEnding')!r}")
    bom = source.get("bom")
    cursor = 0
    if bom is not None:
        if not isinstance(bom, Mapping) or int(bom.get("offset", -1)) != 0:
            _fail("source.bom is not a span at the head of the member")
        cursor = int(bom.get("length", 0))
    lines = source.get("lines")
    if not isinstance(lines, list):
        _fail("source.lines is not a table")
    for index, row in enumerate(lines):
        if not isinstance(row, Mapping) or row.get("index") != index:
            _fail(f"source line {index} is out of order")
        offset, size = int(row.get("offset", -1)), int(row.get("length", -1))
        if offset != cursor or size <= 0:
            _fail(f"source line {index} does not continue the previous one")
        if row.get("terminator") not in ("crlf", "lf", "cr", ""):
            _fail(f"source line {index} ends with {row.get('terminator')!r}")
        cursor = offset + size
    if cursor != length:
        _fail(f"source.lines account for {cursor} of {length} bytes")
    if int(source.get("lineCount", -1)) != len(lines):
        _fail("source.lineCount disagrees with the line table")
    return length


def _reference_records(rows: Sequence[Mapping[str, Any]]) -> list[ReferenceRecord]:
    """The published reference table, back in the shape the dependency rule reads."""

    return [
        ReferenceRecord(
            token=int(row.get("token", 0)),
            literal=str(row.get("literal", "")),
            kind=str(row.get("kind", "")),
            asset=str(row.get("asset", "")),
            source_path=str(row.get("sourcePath", "")),
            resolved=bool(row.get("resolved")),
            sentinel_reason=(str(row["omittedProven"]) if row.get("omittedProven") else None),
            source_offset=int(row.get("sourceOffset", 0)),
        )
        for row in rows
    ]


def _check_references(root: Mapping[str, Any], tokens: Sequence[lexer.Token]) -> list[str]:
    """Every reference names a token of this unit and owns exactly one dependency or a sentinel."""

    references = root.get("references")
    if not isinstance(references, list):
        _fail("the unit publishes no reference table")
    declared: dict[str, str] = {}
    sentinels: set[str] = set()
    unresolved: list[str] = []
    for index, row in enumerate(references):
        if not isinstance(row, Mapping):
            _fail(f"reference {index} is not a record")
        kind = str(row.get("kind", ""))
        if kind not in DEPENDENCY_ROLES:
            _fail(f"reference {index} names role {kind!r}")
        slot = row.get("token")
        if not isinstance(slot, int) or not 0 <= slot < len(tokens):
            _fail(f"reference {index} names no token of this unit")
        if tokens[slot].type != "STRING":
            _fail(f"reference {index} points at a {tokens[slot].type} token")
        if tokens[slot].offset != int(row.get("sourceOffset", -1)):
            _fail(f"reference {index} disagrees with its token's source offset")
        if lexer.string_value(tokens[slot].string) != row.get("literal"):
            _fail(f"reference {index} disagrees with the literal its token spells")
        asset = str(row.get("asset", ""))
        if row.get("omittedProven"):
            if not asset.startswith(f"vtmb:missing-{kind}:"):
                _fail(f"reference {index} is graded omitted-proven without a sentinel identity")
            if row.get("resolved"):
                _fail(f"reference {index} is both a sentinel and resolved")
            sentinels.add(asset)
            continue
        if not asset.startswith(f"vtmb:{kind}:"):
            _fail(f"reference {index} names no {kind} identity")
        declared[asset] = kind
        if not row.get("resolved"):
            unresolved.append(str(row.get("sourcePath") or asset))
    rows = {str(row["asset"]): str(row["role"]) for row in root.get("dependencies") or []}
    if len(rows) != len(root.get("dependencies") or []):
        _fail("a dependency identity is declared twice")
    if rows != declared:
        _fail("the dependency table disagrees with the references that produced it")
    # Every column of the dependency table is re-derived from the published references, so a
    # row whose `sourcePath` or `resolved` disagrees with the reference that produced it fails
    # with no install present.
    rebuilt = build_dependencies(_reference_records(references))
    if rebuilt != list(root.get("dependencies") or []):
        _fail("the dependency table disagrees with the rows its references produce")
    if sentinels & set(rows):
        _fail("a sentinel reference produced a dependency row")
    graded = {
        str(entry.get("asset"))
        for entry in (root.get("coverage") or {}).get("omittedProven") or []
    }
    if not sentinels <= graded:
        _fail("a sentinel reference is not graded omitted-proven in coverage")
    return unresolved


def _check_entity_names(root: Mapping[str, Any], tokens: Sequence[lexer.Token]) -> None:
    names = root.get("entityNames")
    if not isinstance(names, list):
        _fail("the unit publishes no entity-name table")
    assets = {str(row.get("asset")) for row in root.get("dependencies") or []}
    for index, row in enumerate(names):
        slot = row.get("token")
        if not isinstance(slot, int) or not 0 <= slot < len(tokens):
            _fail(f"entity name {index} names no token of this unit")
        if tokens[slot].type != "STRING":
            _fail(f"entity name {index} points at a {tokens[slot].type} token")
        if lexer.string_value(tokens[slot].string) != row.get("name"):
            _fail(f"entity name {index} disagrees with the literal its token spells")
        # A script is loaded per map and the map is not a datum of the file, so an entity name
        # joins to a map-entities unit through the corpus index and never through this table.
        if f"vtmb:map-entities:{row.get('name')}" in assets:
            _fail(f"entity name {index} produced a dependency row")


def _check_instructions(root: Mapping[str, Any]) -> int:
    """Every code object's disassembly tiles the code string it was read from."""

    compiled = root.get("pyc")
    if "pyc" not in root:
        _fail("the unit does not say whether it has a compiled companion")
    if compiled is None:
        return 0
    if not isinstance(compiled, Mapping):
        _fail("pyc is neither a decode nor absent")
    for field in ("magic", "magicBytes", "magicExpected", "mtime", "code"):
        if field not in compiled:
            _fail(f"the companion decode is missing {field}")
    total = 0
    for code in walk_codes(dict(compiled.get("code") or {})):
        length = int(code.get("codeLength", -1))
        base = int(code.get("codeOffset", -1))
        if base < 0 or length < 0:
            _fail(f"{code.get('path')}: the code string names no extent in the member")
        cursor = 0
        for instruction in code.get("instructions") or []:
            if int(instruction.get("offset", -1)) != cursor:
                _fail(f"{code.get('path')}: instruction at {cursor} is missing")
            if int(instruction.get("sourceOffset", -1)) != base + cursor:
                _fail(f"{code.get('path')}: instruction at {cursor} names the wrong file offset")
            argument = instruction.get("arg")
            cursor += 1 if argument is None else 3
            total += 1
        if cursor != length:
            _fail(f"{code.get('path')}: the disassembly covers {cursor} of {length} code bytes")
    return total


def _check_against_members(
    root: Mapping[str, Any],
    tokens: Sequence[lexer.Token],
    source_members: Sequence[Any],
    member_exists: Callable[[str], bool] | None,
) -> None:
    """Re-read every member and re-derive everything the unit claims from it."""

    by_role = {member.role: member for member in source_members}
    py = by_role.get("py")
    rebuilt_anomalies: list[dict[str, Any]] = []
    rebuilt_typed: list[dict[str, Any]] = []
    rebuilt_omitted: list[dict[str, Any]] = []
    rebuilt_references: list[Any] = []
    rebuilt_structure: dict[str, Any] = {}
    raw: list[lexer.Token] = []
    if py is not None:
        source_block, _, _ = describe_source(py.data)
        published = dict(root.get("source") or {})
        for field in ("encoding", "lineEnding", "byteLength", "lineCount"):
            if published.get(field) != source_block[field]:
                _fail(f"source.{field} disagrees with the member re-read from the install")
        if published.get("lines") != source_block["lines"]:
            _fail("source.lines disagrees with the member re-read from the install")
        # The contract's own check: the line spans concatenate to the member exactly, so the
        # unit accounts for the source without carrying a copy of it.
        bom = published.get("bom") or {}
        rebuilt_bytes = py.data[: int(bom.get("length", 0))] + b"".join(
            py.data[int(row["offset"]): int(row["offset"]) + int(row["length"])]
            for row in published.get("lines") or []
        )
        if rebuilt_bytes != py.data:
            _fail("source.lines do not concatenate to the member they were cut from")
        _, rebuilt = decode_tokens(py.data)
        if [token.to_json() for token in rebuilt] != list(root.get("tokens") or []):
            _fail("the published token stream disagrees with an independent tokenization")
        raw = [lexer.Token(t.type, t.string, t.line, t.col, t.offset) for t in rebuilt]
        rebuilt_structure = derive_structure(raw)
        if rebuilt_structure != root.get("structure"):
            _fail("the published structure disagrees with one re-derived from the source")
        rebuilt_entities = collect_entity_names(
            raw, entity_aliases(raw, rebuilt_structure)
        )
        if [record.to_json() for record in rebuilt_entities] != list(
            root.get("entityNames") or []
        ):
            _fail("the published entity names disagree with an independent read of the source")
        # With the install in hand the membership question can be asked for real, so every
        # column of every reference -- its identity, the path the referrer authored and whether
        # the index holds a member for it -- is re-derived rather than assumed.
        if member_exists is None:
            _fail("the members were supplied without the index that resolves what they name")
        rebuilt_references = collect_references(raw, member_exists)
        if [record.to_json() for record in rebuilt_references] != list(
            root.get("references") or []
        ):
            _fail("the published reference table disagrees with an independent read")
        if build_dependencies(rebuilt_references) != list(root.get("dependencies") or []):
            _fail("the published dependency table disagrees with an independent read")
        rebuilt_anomalies = source_anomalies(py.data, raw, rebuilt_structure)
    compiled = by_role.get("pyc")
    if compiled is not None and compiled.data:
        try:
            rebuilt_pyc = decode_pyc(compiled.data, compiled.path)
        except PycDecodeError as error:
            raise ScriptGlbValidationError(str(error)) from error
        published = root.get("pyc") or {}
        if published.get("magic") != rebuilt_pyc.magic or published.get(
            "mtime"
        ) != rebuilt_pyc.mtime:
            _fail("the published companion header disagrees with the member")
        if published.get("code") != rebuilt_pyc.code:
            _fail("the published code tree disagrees with an independent unmarshalling")
        rebuilt_anomalies.extend(rebuilt_pyc.anomalies)
        rebuilt_typed.extend(rebuilt_pyc.typed_unidentified)
        rebuilt_omitted.extend(rebuilt_pyc.omitted_proven)
        if py is not None:
            rebuilt_anomalies.extend(
                compare_pyc_to_source(
                    rebuilt_pyc.code, rebuilt_structure, source_string_constants(raw)
                )
            )
    elif compiled is not None and root.get("pyc") is not None:
        _fail("an empty companion published a decode")
    # An anomaly row and a completeness counter are decode results like any other, so they are
    # re-derived from the members too: a unit that drops a `typedUnidentified` row or invents a
    # drift row disagrees with the re-decode here rather than passing on the writer's word.
    if list(root.get("anomalies") or []) != rebuilt_anomalies:
        _fail("the published anomalies disagree with an independent decode of the members")
    if list(root.get("omissions") or []) != empty_member_omissions(source_members):
        _fail("the published omissions disagree with the members the install resolved")
    coverage = root.get("coverage") or {}
    if list(coverage.get("typedUnidentified") or []) != rebuilt_typed:
        _fail("coverage.typedUnidentified disagrees with an independent decode of the members")
    if list(coverage.get("omittedProven") or []) != rebuilt_omitted + sentinel_rows(
        rebuilt_references
    ):
        _fail("coverage.omittedProven disagrees with an independent decode of the members")


def validate_document(
    document: Mapping[str, Any],
    binary: bytes,
    *,
    source_members: Sequence[Any] | None = None,
    member_exists: Callable[[str], bool] | None = None,
) -> dict[str, Any]:
    """Check one script unit, with the install's members where the caller still holds them.

    `member_exists` answers the index question the references were resolved against; it is
    required whenever `source_members` is given, because a `resolved` flag can only be re-derived
    by asking the same index again.
    """

    try:
        validate_container(document, binary)
        validate_sceneless(document)
        validate_accessors(document, binary)
        root = validate_extension_root(
            document,
            SCRIPT_EXTENSION,
            asset_prefix=ASSET_PREFIX,
            schema_version=SCHEMA_VERSION,
        )
        validate_ledgers(root, source_members)
        validate_capsules(document, binary, root, source_members)
    except UnitValidationError as error:
        raise ScriptGlbValidationError(str(error)) from error
    if binary:
        _fail("a script unit carries no BIN chunk")
    asset, key, members = _check_identity(root)
    has_companion = any(str(member.get("role")) == "pyc" for member in members)
    if not has_companion and root.get("pyc") is not None:
        _fail("the unit publishes a companion decode it resolved no member for")
    if has_companion and root.get("pyc") is None:
        empty = [
            member for member in members
            if str(member.get("role")) == "pyc" and int(member.get("byteLength", -1)) == 0
        ]
        if not empty:
            _fail("the unit resolved a companion and published no decode of it")
    source_bytes = _check_source(root, members)
    tokens = _tokens_from(root.get("tokens") or [])
    structure = root.get("structure")
    if not isinstance(structure, Mapping) or set(structure) != {
        "imports", "functions", "classes", "assignments"
    }:
        _fail("the unit publishes no module structure")
    if derive_structure(tokens) != structure:
        _fail("the published structure disagrees with one derived from the published tokens")
    unresolved_references = _check_references(root, tokens)
    _check_entity_names(root, tokens)
    for row in root.get("anomalies") or []:
        if not isinstance(row, Mapping) or row.get("role") not in ANOMALY_ROLES:
            _fail(f"unknown source anomaly {row!r}")
    for row in root.get("omissions") or []:
        if not isinstance(row, Mapping) or row.get("role") != "empty-member":
            _fail(f"unknown omission {row!r}")
    instructions = _check_instructions(root)
    if source_members is not None:
        _check_against_members(root, tokens, source_members, member_exists)
    counts = completeness(root)
    ledgers = (root.get("coverage") or {}).get("byteLedger") or []
    return {
        "asset": asset,
        "key": key,
        "sourceKind": (root.get("identity") or {}).get("sourceKind"),
        "members": [str(member.get("path")) for member in members],
        "sourceBytes": source_bytes,
        "tokens": len(tokens),
        "functions": len(structure.get("functions") or []),
        "classes": len(structure.get("classes") or []),
        "imports": len(structure.get("imports") or []),
        "assignments": len(structure.get("assignments") or []),
        "references": len(root.get("references") or []),
        "entityNames": len(root.get("entityNames") or []),
        "dependencies": len(root.get("dependencies") or []),
        "instructions": instructions,
        "anomalies": [str(row.get("role")) for row in root.get("anomalies") or []],
        "omissions": [str(row.get("role")) for row in root.get("omissions") or []],
        "unresolvedReferences": unresolved_references,
        "unresolved": counts["unresolved"],
        "unsupported": counts["unsupported"],
        "typedUnidentified": counts["typedUnidentified"],
        "byteCoveragePercent": [float(row.get("coveragePercent", 0.0)) for row in ledgers],
        "contractWarnings": _contract_warnings(root),
    }


def validate(path: Path) -> dict[str, Any]:
    """Read one published unit with no install present and check it against itself."""

    document, binary = read_glb(Path(path))
    return validate_document(document, binary)


def warnings_for(summary: Mapping[str, Any]) -> list[str]:
    """What a published unit could not resolve, phrased for the operator."""

    warnings = list(summary.get("contractWarnings") or [])
    missing = sorted(set(summary.get("unresolvedReferences") or []))
    if missing:
        warnings.append(
            "the install carries no member for "
            + ", ".join(missing[:4])
            + (f" and {len(missing) - 4} more" if len(missing) > 4 else "")
        )
    return warnings
