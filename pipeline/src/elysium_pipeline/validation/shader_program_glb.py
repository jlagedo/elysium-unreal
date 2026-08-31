"""Independent structural validator for the two shader-program GLB unit kinds.

`validate_document` never asks the writer what it meant. Given the selected members it re-decodes
them through `elysium_pipeline.formats.shader_program_glb.decode` -- the canonical decoder, not
`exporters.shader_program_glb.build_document` -- and weighs the result against what the document
actually published. Given no members it still reassembles every combo's token stream out of the
published rows and checks the digest the unit claims for it, which is the standalone half of the
same promise: the unit keeps no copy of the bytecode, so what it states has to be enough to put
the bytecode back.
"""

from __future__ import annotations

import hashlib
from pathlib import Path
from typing import Any, Sequence

from elysium_pipeline.formats.shader_program_glb import ctab as ctab_module
from elysium_pipeline.formats.shader_program_glb import tokens as tokens_module
from elysium_pipeline.formats.shader_program_glb.decode import (
    decode_shader_program,
    decode_shader_source,
)
from elysium_pipeline.formats.shader_program_glb.model import (
    PROGRAM_GENERATOR_TITLE,
    SCHEMA_VERSION,
    SHADER_PROGRAM_ASSET_PREFIX,
    SHADER_PROGRAM_EXTENSION,
    SHADER_SOURCE_ASSET_PREFIX,
    SHADER_SOURCE_EXTENSION,
    SOURCE_GENERATOR_TITLE,
)
from elysium_pipeline.formats.shader_program_glb.source import (
    ShaderProgramClosure,
    ShaderSourceClosure,
)
from elysium_pipeline.formats.unit_contract import (
    GlbContainerError,
    SourceMember,
    UnitValidationError,
    completeness,
    generator,
    missing_sentinel,
    validate_capsules,
    validate_container,
    validate_extension_root,
    validate_index_rows,
    validate_ledgers,
    validate_sceneless,
)
from elysium_pipeline.formats.unit_contract import read_glb as _contract_read_glb
from elysium_pipeline.formats.unit_contract import warnings_for as _contract_warnings_for

#: The ledger states the two kinds are entitled to. A text member is `mapped-text` end to end; a
#: compiled bundle is `mapped`, verified zero fill, or an evidence-backed omission.
SOURCE_LEDGER_STATES = {"mapped-text"}
PROGRAM_LEDGER_STATES = {"mapped", "padding-zero", "omitted-proven"}


class ShaderProgramGlbValidationError(ValueError):
    """A published shader-source or shader-program unit contradicts the seam."""


def read_glb(path: Path) -> tuple[dict[str, Any], bytes]:
    try:
        return _contract_read_glb(path)
    except GlbContainerError as error:
        raise ShaderProgramGlbValidationError(str(error)) from error


def _kind_of(document: dict[str, Any]) -> str:
    extensions = document.get("extensions") or {}
    present = [name for name in (SHADER_SOURCE_EXTENSION, SHADER_PROGRAM_EXTENSION)
               if name in extensions]
    if len(present) != 1:
        raise ShaderProgramGlbValidationError(
            "a unit of this seam declares exactly one of "
            f"{SHADER_SOURCE_EXTENSION}, {SHADER_PROGRAM_EXTENSION}"
        )
    return "shader-source" if present[0] == SHADER_SOURCE_EXTENSION else "shader-program"


# ------------------------------------------------------------------------------- shared checks


def _check_no_binary(binary: bytes) -> None:
    if binary:
        raise ShaderProgramGlbValidationError(
            f"a shader unit carries no BIN chunk; this one carries {len(binary)} bytes"
        )


def _check_generator(document: dict[str, Any], title: str) -> None:
    stated = (document.get("asset") or {}).get("generator")
    if stated != generator(title):
        raise ShaderProgramGlbValidationError(
            f"asset.generator is {stated!r}, not {generator(title)!r}"
        )


def _check_extension_declaration(document: dict[str, Any], extension: str) -> None:
    for key in ("extensionsUsed", "extensionsRequired"):
        declared = list(document.get(key) or [])
        if declared != [extension]:
            raise ShaderProgramGlbValidationError(
                f"{key} declares {declared}, not [{extension!r}]"
            )


def _empty_member(root: dict[str, Any]) -> bool:
    """Whether the unit states that its selecting member holds zero bytes.

    The claim is not taken on trust: the omission row and the ledger row have to agree, so a unit
    cannot excuse a missing header by declaring itself empty.
    """

    rows = [row for row in root["omissions"] if row.get("role") == "empty-member"]
    ledger = root["coverage"]["byteLedger"][0]
    if not rows:
        if not ledger["byteLength"]:
            raise ShaderProgramGlbValidationError(
                "an empty member publishes no empty-member omission"
            )
        return False
    if len(rows) != 1:
        raise ShaderProgramGlbValidationError(f"{len(rows)} empty-member omission row(s)")
    if ledger["byteLength"]:
        raise ShaderProgramGlbValidationError(
            f"the unit calls its member empty and the ledger accounts {ledger['byteLength']} bytes"
        )
    return True


def _check_ledger_states(root: dict[str, Any], allowed: set[str]) -> None:
    for row in root["coverage"]["byteLedger"]:
        states = set(row.get("stateBytes") or {})
        unexpected = sorted(states - allowed)
        if unexpected:
            raise ShaderProgramGlbValidationError(
                f"{row['sourcePath']}: byte ledger claims {', '.join(unexpected)}"
            )


# ------------------------------------------------------------------------------ readable source


def _validate_source_lines(root: dict[str, Any]) -> None:
    """The published lines partition the member and reproduce its length exactly."""

    ledger = root["coverage"]["byteLedger"][0]
    cursor = 0
    endings = {"crlf": 2, "lf": 1, "none": 0}
    for line in root["source"]["lines"]:
        if line["sourceOffset"] != cursor:
            raise ShaderProgramGlbValidationError(
                f"line {line['index']} starts at {line['sourceOffset']}, not {cursor}"
            )
        if line["ending"] not in endings:
            raise ShaderProgramGlbValidationError(
                f"line {line['index']} states line ending {line['ending']!r}"
            )
        expected = len(line["text"].encode("latin-1")) + endings[line["ending"]]
        if line["byteLength"] != expected:
            raise ShaderProgramGlbValidationError(
                f"line {line['index']} spans {line['byteLength']} bytes and holds {expected}"
            )
        comment = line.get("comment")
        if comment is not None:
            if not line["text"][comment["sourceOffset"] - line["sourceOffset"]:].startswith(
                comment["marker"]
            ):
                raise ShaderProgramGlbValidationError(
                    f"line {line['index']} states a comment its text does not carry"
                )
        cursor += line["byteLength"]
    if cursor != ledger["byteLength"]:
        raise ShaderProgramGlbValidationError(
            f"the published lines span {cursor} of {ledger['byteLength']} member bytes"
        )
    instruction_lines = {row["line"] for row in root["source"]["instructions"]}
    define_lines = {row["line"] for row in root["source"]["defines"]}
    classified = {
        line["index"]
        for line in root["source"]["lines"]
        if line["kind"] in ("instruction", "phase")
    }
    if instruction_lines != classified:
        raise ShaderProgramGlbValidationError(
            "the instruction rows and the lines classified as instructions disagree"
        )
    if define_lines != {
        line["index"] for line in root["source"]["lines"] if line["kind"] == "define"
    }:
        raise ShaderProgramGlbValidationError(
            "the define rows and the lines classified as defines disagree"
        )


def _validate_compiled_twin(root: dict[str, Any]) -> None:
    """The compiled twin is named by stable ID and declared once, or by the missing sentinel.

    A reference that does not resolve is still a reference: it keeps the authored spelling in the
    `vtmb:missing-shader-program:` namespace, produces no dependency row, and enters coverage as
    the `absent-compiled-twin` omission that says why.
    """

    twin = root["identity"]["compiledTwin"]
    rows = [row for row in root["dependencies"] if row["role"] == "compiled-twin"]
    key = str(root["identity"]["asset"])[len(SHADER_SOURCE_ASSET_PREFIX):]
    if not twin["present"]:
        sentinel = missing_sentinel("shader-program", f"psh/{key}")
        if twin["asset"] != sentinel:
            raise ShaderProgramGlbValidationError(
                f"an unresolved compiled twin is {twin['asset']!r}, not the sentinel {sentinel!r}"
            )
        if rows:
            raise ShaderProgramGlbValidationError(
                "a compiled-twin dependency is declared for a twin the unit calls absent"
            )
        omitted = [row for row in root["omissions"] if row.get("role") == "absent-compiled-twin"]
        if len(omitted) != 1 or omitted[0].get("asset") != sentinel:
            raise ShaderProgramGlbValidationError(
                "an unresolved compiled twin publishes no absent-compiled-twin omission"
            )
        return
    if twin["asset"] != SHADER_PROGRAM_ASSET_PREFIX + f"psh/{key}":
        raise ShaderProgramGlbValidationError(
            f"the compiled twin of {key} is {twin['asset']!r}, not the psh/ program of that stem"
        )
    if len(rows) != 1:
        raise ShaderProgramGlbValidationError(
            f"{len(rows)} compiled-twin dependency row(s) against one compiled twin"
        )
    if rows[0]["asset"] != twin["asset"] or rows[0]["sourcePath"] != twin["sourcePath"]:
        raise ShaderProgramGlbValidationError(
            "the compiled-twin dependency and the identity name different units"
        )
    if not rows[0]["resolved"]:
        raise ShaderProgramGlbValidationError(
            "the identity holds a compiled twin its dependency row says did not resolve"
        )


def _redecode_source(root: dict[str, Any], member: SourceMember) -> None:
    twin = root["identity"]["compiledTwin"]
    closure = ShaderSourceClosure(
        key=str(root["identity"]["asset"])[len(SHADER_SOURCE_ASSET_PREFIX):],
        asset_id=str(root["identity"]["asset"]),
        psh=member,
        compiled_twin_path=str(twin["sourcePath"]),
        compiled_twin_present=bool(twin["present"]),
    )
    redecoded = decode_shader_source(closure)
    if redecoded.dependencies != root["dependencies"]:
        raise ShaderProgramGlbValidationError(
            "the re-parsed dependency table disagrees with the published one"
        )
    if redecoded.source_block() != root["source"]:
        raise ShaderProgramGlbValidationError("the re-parsed source disagrees with the published one")
    if redecoded.anomalies != root["anomalies"]:
        raise ShaderProgramGlbValidationError("the re-parsed anomalies disagree with the published ones")
    if redecoded.omissions != root["omissions"]:
        raise ShaderProgramGlbValidationError("the re-parsed omissions disagree with the published ones")
    if redecoded.byte_ledger != root["coverage"]["byteLedger"]:
        raise ShaderProgramGlbValidationError("the re-parsed byte ledger disagrees with the published one")


def _validate_source_version(root: dict[str, Any], empty: bool) -> None:
    """A source states its version line, unless the member the install holds has no bytes at all."""

    if root["source"]["version"] is None:
        if not empty:
            raise ShaderProgramGlbValidationError(
                "a source that holds bytes publishes no version line"
            )
        if root["identity"]["shaderModel"] is not None:
            raise ShaderProgramGlbValidationError(
                "the identity states a shader model the source does not declare"
            )
        return
    version = root["source"]["version"]
    stated = f"ps_{version['major']}_{version['minor']}"
    if root["identity"]["shaderModel"] != stated:
        raise ShaderProgramGlbValidationError(
            f"the identity states {root['identity']['shaderModel']!r} and the source {stated!r}"
        )


# ----------------------------------------------------------------------------- compiled bundle


def _validate_empty_program(root: dict[str, Any]) -> None:
    """An empty bundle publishes no container at all, rather than an invented one."""

    if root["header"] is not None or root["comboTable"] is not None:
        raise ShaderProgramGlbValidationError(
            "a bundle whose member holds no bytes states a header or a combo table"
        )
    if root["combos"] or root["coverage"]["typedUnidentified"]:
        raise ShaderProgramGlbValidationError(
            "a bundle whose member holds no bytes states combos or header words"
        )



def _combo_payloads(combo: dict[str, Any]) -> dict[int, bytes]:
    """The comment payloads a combo's tokens need to be put back together."""

    payloads: dict[int, bytes] = {}
    table = combo.get("constantTable")
    if table is not None:
        token = next(
            (row for row in combo["tokens"]
             if row["kind"] == "comment" and row["payloadOffset"] == table["sourceOffset"]),
            None,
        )
        if token is None:
            raise ShaderProgramGlbValidationError(
                f"combo {combo['index']} states a constant table no comment token points at"
            )
        payloads[token["index"]] = ctab_module.encode(table)
    for comment in combo.get("comments") or []:
        payloads[comment["tokenIndex"]] = bytes.fromhex(comment["hex"])
    return payloads


def _reassemble(combo: dict[str, Any]) -> bytes:
    try:
        return tokens_module.reassemble(combo["tokens"], _combo_payloads(combo))
    except (tokens_module.TokenStreamError, ctab_module.ConstantTableError) as error:
        raise ShaderProgramGlbValidationError(
            f"combo {combo['index']} does not reassemble: {error}"
        ) from error


def _source_token_counts(rows: list[dict[str, Any]]) -> list[int]:
    """How many source operands each instruction token actually carries, in publication order.

    A relative-address token belongs to the source before it and a predicate token to the
    instruction word, so neither is a source; an instruction row that publishes one as a source
    would state an operand the opcode does not have.
    """

    counts: list[int] = []
    current: int | None = None
    for row in rows:
        kind = row["kind"]
        if kind in ("instruction", "phase"):
            counts.append(0)
            current = len(counts) - 1
        elif kind in ("version", "end", "comment"):
            current = None
        elif kind == "source" and current is not None:
            counts[current] += 1
    return counts


def _validate_combos(root: dict[str, Any], data: bytes | None) -> None:
    """Every combo reassembles to its own digest, and to its own source bytes when they are here."""

    header = root["header"]
    table = root["comboTable"]
    accounted = table["presentEntries"] + table["absentEntries"]
    if accounted != table["entries"]:
        raise ShaderProgramGlbValidationError(
            f"the combo table accounts {accounted} of {table['entries']} entries"
        )
    # A header that states a negative combo count declares no table at all: the count clamps to
    # zero, the anomaly records what the file said, and the unit publishes the rest of itself.
    declared = max(0, header["totalCombos"])
    negative = any(row["role"] == "negative-combo-count" for row in root["anomalies"])
    if negative != (header["totalCombos"] < 0):
        raise ShaderProgramGlbValidationError(
            f"the header states {header['totalCombos']} combos with no anomaly to classify it"
        )
    if table["declaredEntries"] != declared:
        raise ShaderProgramGlbValidationError(
            f"the combo table declares {table['declaredEntries']} entries against the header's "
            f"{header['totalCombos']}"
        )
    if table["entries"] > declared:
        raise ShaderProgramGlbValidationError(
            "the combo table states more entries than the header declares"
        )
    present = [combo for combo in root["combos"] if "tokens" in combo]
    for combo in root["combos"]:
        if "tokens" in combo:
            continue
        if combo.get("aliasOf") is None and not combo.get("overlapping"):
            raise ShaderProgramGlbValidationError(
                f"combo {combo['index']} owns no stream and names no entry that does"
            )
    if len(root["combos"]) != table["presentEntries"]:
        raise ShaderProgramGlbValidationError(
            f"{len(root['combos'])} combo row(s) against {table['presentEntries']} present entries"
        )
    undecoded = {
        row["combo"] for row in root["omissions"] if row["role"] == "undecoded-combo-stream"
    }
    for combo in present:
        if combo["index"] in undecoded:
            continue
        if not combo["tokens"]:
            raise ShaderProgramGlbValidationError(
                f"combo {combo['index']} publishes no tokens and no omission for them"
            )
        rebuilt = _reassemble(combo)
        if len(rebuilt) != combo["size"]:
            raise ShaderProgramGlbValidationError(
                f"combo {combo['index']} reassembles to {len(rebuilt)} of {combo['size']} bytes"
            )
        digest = hashlib.sha256(rebuilt).hexdigest()
        if digest != combo["sha256"]:
            raise ShaderProgramGlbValidationError(
                f"combo {combo['index']} reassembles to a stream its own digest disowns"
            )
        if data is not None and rebuilt != data[combo["offset"]:combo["offset"] + combo["size"]]:
            raise ShaderProgramGlbValidationError(
                f"combo {combo['index']} reassembles to bytes the member does not hold"
            )
        instruction_tokens = sum(
            1 for row in combo["tokens"] if row["kind"] in ("instruction", "phase")
        )
        if instruction_tokens != len(combo["instructions"]):
            raise ShaderProgramGlbValidationError(
                f"combo {combo['index']} disassembles {len(combo['instructions'])} instruction(s) "
                f"from {instruction_tokens} instruction token(s)"
            )
        if combo["tokens"][0]["kind"] != "version" or combo["model"] != combo["tokens"][0]["model"]:
            raise ShaderProgramGlbValidationError(
                f"combo {combo['index']} states a model its version token does not"
            )
        stated = [len(row["sources"]) for row in combo["instructions"]]
        if _source_token_counts(combo["tokens"]) != stated:
            raise ShaderProgramGlbValidationError(
                f"combo {combo['index']} publishes instruction operands its tokens do not carry"
            )


def _validate_readable_source(root: dict[str, Any], empty: bool) -> None:
    """`identity.readableSource` states the install's answer, and names a unit only where it may.

    A bundle outside `psh/` names no `vtmb:shader-source:` unit at all -- so it declares no
    dependency and owes no omission -- but it still publishes whether the install holds the
    joined path, which is a fact about the install rather than a claim about this seam's rule.
    """

    block = root["identity"]["readableSource"]
    key = str(root["identity"]["asset"])[len(SHADER_PROGRAM_ASSET_PREFIX):]
    subdir, stem = key.split("/", 1)
    if block["comparable"] != (subdir == "psh"):
        raise ShaderProgramGlbValidationError(
            f"a {subdir}/ bundle states comparable={block['comparable']}"
        )
    omitted = [row for row in root["omissions"] if row.get("role") == "absent-readable-source"]
    if not block["comparable"]:
        if block["asset"] is not None:
            raise ShaderProgramGlbValidationError(
                "a bundle outside psh/ names a readable source it cannot have been built from"
            )
        if omitted:
            raise ShaderProgramGlbValidationError(
                "a bundle outside psh/ omits a readable source it never referenced"
            )
        if not block.get("reason"):
            raise ShaderProgramGlbValidationError(
                "a bundle outside psh/ states no reason for not comparing"
            )
        return
    if block["present"]:
        if block["asset"] != SHADER_SOURCE_ASSET_PREFIX + stem:
            raise ShaderProgramGlbValidationError(
                f"the readable source of {key} is {block['asset']!r}, not the source of that stem"
            )
        if omitted:
            raise ShaderProgramGlbValidationError(
                "a resolved readable source publishes an absent-readable-source omission"
            )
        if root["sourceComparison"] is None and not empty:
            raise ShaderProgramGlbValidationError(
                "the identity claims a readable source the unit did not compare against"
            )
        return
    sentinel = missing_sentinel("shader-source", stem)
    if block["asset"] != sentinel:
        raise ShaderProgramGlbValidationError(
            f"an unresolved readable source is {block['asset']!r}, not the sentinel {sentinel!r}"
        )
    if len(omitted) != 1 or omitted[0].get("asset") != sentinel:
        raise ShaderProgramGlbValidationError(
            "an unresolved readable source publishes no absent-readable-source omission"
        )


def _validate_comparison(root: dict[str, Any]) -> None:
    """The source comparison agrees with the dependency table and with the drift anomalies."""

    comparison = root["sourceComparison"]
    rows = [row for row in root["dependencies"] if row["role"] == "shader-source"]
    if comparison is None:
        if rows:
            raise ShaderProgramGlbValidationError(
                "a shader-source dependency is declared without a source comparison"
            )
        return
    if len(rows) != 1:
        raise ShaderProgramGlbValidationError(
            f"{len(rows)} shader-source dependency row(s) against one source comparison"
        )
    if rows[0]["asset"] != comparison["source"]:
        raise ShaderProgramGlbValidationError(
            "the shader-source dependency and the source comparison name different sources"
        )
    if not rows[0]["resolved"]:
        raise ShaderProgramGlbValidationError(
            "the source comparison read a source its dependency row says did not resolve"
        )
    states = {"equivalent", "source-binary-drift", "not-assembled"}
    if comparison["state"] not in states:
        raise ShaderProgramGlbValidationError(
            f"the source comparison states {comparison['state']!r}"
        )
    if not comparison["assembled"]:
        if comparison["state"] != "not-assembled":
            raise ShaderProgramGlbValidationError(
                "the source did not assemble but the comparison states a result"
            )
        return
    matched = list(comparison["combosMatched"])
    differing = [row["combo"] for row in comparison["combosDiffering"]]
    expected = comparison["state"] == ("source-binary-drift" if differing else "equivalent")
    if not expected:
        raise ShaderProgramGlbValidationError(
            f"the comparison states {comparison['state']!r} with {len(differing)} differing combo(s)"
        )
    compared = sorted(matched + differing)
    present = sorted(combo["index"] for combo in root["combos"] if "tokens" in combo)
    if compared != present:
        raise ShaderProgramGlbValidationError(
            "the source comparison does not weigh every combo of the bundle"
        )
    drift = {row["combo"] for row in root["anomalies"] if row["role"] == "source-binary-drift"}
    if drift != set(differing):
        raise ShaderProgramGlbValidationError(
            "the differing combos and the source-binary-drift anomalies disagree"
        )


def _redecode_program(root: dict[str, Any], member: SourceMember) -> None:
    """Re-decode the member with no twin, and compare everything the twin does not touch."""

    block = root["identity"]["readableSource"]
    closure = ShaderProgramClosure(
        key=str(root["identity"]["asset"])[len(SHADER_PROGRAM_ASSET_PREFIX):],
        asset_id=str(root["identity"]["asset"]),
        vcs=member,
        twin=None,
        twin_path=str(block["sourcePath"]),
        twin_expected=bool(block["comparable"]),
        twin_present=bool(block["present"]),
    )
    redecoded = decode_shader_program(closure)
    if redecoded.header != root["header"]:
        raise ShaderProgramGlbValidationError("the re-read header disagrees with the published one")
    if redecoded.combo_table != root["comboTable"]:
        raise ShaderProgramGlbValidationError(
            "the re-read combo table disagrees with the published one"
        )
    if redecoded.combos != root["combos"]:
        raise ShaderProgramGlbValidationError("the re-read combos disagree with the published ones")
    if redecoded.typed_unidentified != root["coverage"]["typedUnidentified"]:
        raise ShaderProgramGlbValidationError(
            "the re-read typed-unidentified header words disagree with the published ones"
        )
    if redecoded.omissions != root["omissions"]:
        raise ShaderProgramGlbValidationError("the re-read omissions disagree with the published ones")
    if redecoded.byte_ledger != root["coverage"]["byteLedger"]:
        raise ShaderProgramGlbValidationError(
            "the re-read byte ledger disagrees with the published one"
        )
    twin_roles = {"source-binary-drift", "source-not-assembled"}
    without_drift = [row for row in root["anomalies"] if row["role"] not in twin_roles]
    if redecoded.anomalies != without_drift:
        raise ShaderProgramGlbValidationError("the re-read anomalies disagree with the published ones")


def _validate_selected_by(root: dict[str, Any]) -> None:
    """`selectedBy` is the corpus index's, and is the index's row shape or nothing.

    The program seam publishes it empty, because a material naming a shader program is a fact
    about the material; the index writes the inverse in as its last step and re-hashes the unit,
    so a published program is read back in either state and in no other.
    """

    if "selectedBy" not in root:
        raise ShaderProgramGlbValidationError("a shader-program unit publishes selectedBy")
    try:
        validate_index_rows(root["selectedBy"], "selectedBy")
    except UnitValidationError as error:
        raise ShaderProgramGlbValidationError(str(error)) from error


# --------------------------------------------------------------------------------- entry points


def validate_document(
    document: dict[str, Any],
    binary: bytes,
    *,
    source_members: Sequence[SourceMember] | None = None,
) -> dict[str, Any]:
    kind = _kind_of(document)
    is_source = kind == "shader-source"
    extension = SHADER_SOURCE_EXTENSION if is_source else SHADER_PROGRAM_EXTENSION
    prefix = SHADER_SOURCE_ASSET_PREFIX if is_source else SHADER_PROGRAM_ASSET_PREFIX
    title = SOURCE_GENERATOR_TITLE if is_source else PROGRAM_GENERATOR_TITLE
    try:
        root = validate_extension_root(
            document, extension, asset_prefix=prefix, schema_version=SCHEMA_VERSION
        )
        validate_container(document, binary)
        validate_sceneless(document)
        validate_ledgers(root, source_members)
        validate_capsules(document, binary, root, source_members)
    except UnitValidationError as error:
        raise ShaderProgramGlbValidationError(str(error)) from error
    _check_no_binary(binary)
    _check_generator(document, title)
    _check_extension_declaration(document, extension)
    _check_ledger_states(root, SOURCE_LEDGER_STATES if is_source else PROGRAM_LEDGER_STATES)

    complete = completeness(root)
    if complete["unresolved"] or complete["unsupported"]:
        raise ShaderProgramGlbValidationError(f"{kind} unit is incomplete: {complete}")

    empty = _empty_member(root)
    member = next(iter(source_members), None) if source_members is not None else None
    if is_source:
        _validate_source_lines(root)
        _validate_source_version(root, empty)
        _validate_compiled_twin(root)
        if member is not None:
            _redecode_source(root, member)
    else:
        # An empty member holds no container, so the header and the combo table are published as
        # null and there is nothing for the combo checks to weigh.
        if empty:
            _validate_empty_program(root)
        else:
            _validate_combos(root, member.data if member is not None else None)
        _validate_readable_source(root, empty)
        _validate_comparison(root)
        _validate_selected_by(root)
        if member is not None:
            _redecode_program(root, member)

    ledger = root["coverage"]["byteLedger"][0]
    identity = root["identity"]
    summary: dict[str, Any] = {
        "kind": kind,
        "asset": identity["asset"],
        "key": str(identity["asset"])[len(prefix):],
        "dependencies": len(root["dependencies"]),
        "sourceBytes": ledger["byteLength"],
        "accountedBytes": ledger["accountedBytes"],
        "byteCoveragePercent": ledger["coveragePercent"],
    }
    if is_source:
        summary["instructions"] = len(root["source"]["instructions"])
        summary["lines"] = len(root["source"]["lines"])
        summary["shaderModel"] = identity["shaderModel"]
        summary["compiledTwin"] = identity["compiledTwin"]["present"]
    else:
        header = root["header"] or {}
        table = root["comboTable"] or {}
        summary["totalCombos"] = header.get("totalCombos")
        summary["presentCombos"] = table.get("presentEntries", 0)
        summary["absentCombos"] = table.get("absentEntries", 0)
        summary["tokens"] = sum(len(combo.get("tokens") or []) for combo in root["combos"])
        summary["comparisonState"] = (
            None if root["sourceComparison"] is None else root["sourceComparison"]["state"]
        )
    summary["warnings"] = _contract_warnings_for(root)
    return summary


def validate(path: Path) -> dict[str, Any]:
    document, binary = read_glb(path)
    try:
        return validate_document(document, binary)
    except UnitValidationError as error:
        raise ShaderProgramGlbValidationError(str(error)) from error


def warnings_for(summary: dict[str, Any]) -> list[str]:
    return list(summary.get("warnings") or [])
