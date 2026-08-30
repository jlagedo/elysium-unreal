"""The canonical decode for both shader-program unit kinds.

`decode_shader_source` parses one `.psh` into its version line, `def` declarations, instructions
and comments while every byte stays attached to the line it came from. `decode_shader_program`
reads one `.vcs` header, its combo table and every distinct token stream it points at, and
cross-checks a `psh/` bundle against the readable source of the same stem.

Neither decoder repairs what it reads. A header the file states inconsistently, a combo entry
that leaves the file, a stream that does not close with its end token and fill the file did not
zero are recorded as `anomalies[]` or `omissions[]` rows with the evidence that classifies them,
and the ledger keeps naming the record that paid for every byte.
"""

from __future__ import annotations

import hashlib
import re
import struct
from typing import Any

from elysium_pipeline.formats.shader_program_glb import ctab as ctab_module
from elysium_pipeline.formats.shader_program_glb import lexer, tokens as tokens_module
from elysium_pipeline.formats.shader_program_glb.assemble import (
    MNEMONIC_OPCODES,
    AssemblyError,
    assemble,
)
from elysium_pipeline.formats.shader_program_glb.coverage import (
    build_ledger,
    shader_program_coverage,
    shader_source_coverage,
)
from elysium_pipeline.formats.shader_program_glb.model import (
    Define,
    Destination,
    Instruction,
    Operand,
    Register,
    ShaderProgramModel,
    ShaderSourceModel,
    ShaderVersion,
    SourceLine,
)
from elysium_pipeline.formats.shader_program_glb.source import (
    ShaderProgramClosure,
    ShaderSourceClosure,
)
from elysium_pipeline.formats.unit_contract import dependency, source_resolution

#: The five int32 words every `.vcs` opens with.
HEADER_BYTES = 20

#: The register letters the ps.1.x source vocabulary spells, and what they name.
REGISTER_CLASSES = {"r": "temp", "v": "input", "c": "const", "t": "texture"}

#: Instruction modifiers the mnemonic carries as a suffix.
INSTRUCTION_MODIFIERS = ("_sat", "_x2", "_x4", "_x8", "_d2", "_d4", "_d8")

_VERSION_RE = re.compile(r"^(?P<kind>ps|vs)\.(?P<major>\d+)\.(?P<minor>\d+)$")
_MNEMONIC_RE = re.compile(r"^(?P<mnemonic>[A-Za-z_][A-Za-z_0-9]*)(?:\s+(?P<rest>.*))?$", re.S)
_OPERAND_RE = re.compile(
    r"^(?P<negate>-)?(?P<complement>1-)?(?P<cls>[a-z])(?P<index>\d+)"
    r"(?:\.(?P<selector>[a-z]+))?(?P<modifier>_[a-z0-9]+)?$"
)
_FLOAT_RE = re.compile(r"^[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?f?$")


class ShaderSourceDecodeError(ValueError):
    """The readable source is not the ps.1.x assembly this seam parses."""


class ShaderProgramDecodeError(ValueError):
    """The compiled bundle is not the combo container this seam reads."""


# ---------------------------------------------------------------------------- readable source


def _parse_operand(text: str, line_index: int) -> Operand:
    match = _OPERAND_RE.match(text.strip().lower())
    if match is None or match.group("cls") not in REGISTER_CLASSES:
        raise ShaderSourceDecodeError(f"line {line_index + 1}: unclassified operand {text!r}")
    return Operand(
        register=Register(
            register_class=REGISTER_CLASSES[match.group("cls")],
            index=int(match.group("index")),
        ),
        selector=match.group("selector"),
        negate=bool(match.group("negate")),
        complement=bool(match.group("complement")),
        modifier=match.group("modifier"),
        text=text.strip(),
    )


def _parse_destination(text: str, line_index: int) -> Destination:
    operand = _parse_operand(text, line_index)
    if operand.negate or operand.complement or operand.modifier:
        raise ShaderSourceDecodeError(
            f"line {line_index + 1}: destination {text!r} carries a source modifier"
        )
    return Destination(
        register=operand.register,
        write_mask=operand.selector or "",
        text=operand.text,
    )


def _split_mnemonic(mnemonic: str, line_index: int) -> tuple[str, tuple[str, ...]]:
    base = mnemonic.lower()
    modifiers: list[str] = []
    while True:
        for suffix in INSTRUCTION_MODIFIERS:
            if base.endswith(suffix) and len(base) > len(suffix):
                modifiers.insert(0, suffix)
                base = base[: -len(suffix)]
                break
        else:
            break
    if not base:
        raise ShaderSourceDecodeError(f"line {line_index + 1}: empty mnemonic {mnemonic!r}")
    return base, tuple(modifiers)


def _parse_float(text: str, line_index: int) -> float:
    if not _FLOAT_RE.match(text.strip()):
        raise ShaderSourceDecodeError(f"line {line_index + 1}: unclassified literal {text!r}")
    return float(text.strip().rstrip("fF"))


def _parse_source_text(data: bytes) -> tuple[
    ShaderVersion,
    list[SourceLine],
    list[Define],
    list[Instruction],
    list[dict[str, Any]],
    list[Instruction | Define],
    str,
]:
    """Parse one `.psh` member into the records the unit publishes.

    Returns the version, the classified lines, the `def` rows, the instruction rows, the comment
    rows, the two in the order they were written (which is the order an assembler emits them in)
    and the encoding the text was read as.
    """

    _, encoding = lexer.decode_text(data)
    raw_lines = lexer.split_lines(data)
    lines: list[SourceLine] = []
    defines: list[Define] = []
    instructions: list[Instruction] = []
    comments: list[dict[str, Any]] = []
    ordered: list[Instruction | Define] = []
    version: ShaderVersion | None = None
    pending_co_issue = False

    for line in raw_lines:
        if line.marker is not None:
            comments.append(
                {
                    "line": line.index,
                    "sourceOffset": line.comment_offset,
                    "marker": line.marker,
                    "text": line.comment,
                }
            )
        code = line.code.strip()
        if not code:
            lines.append(lexer.classify(line, "comment" if line.marker else "blank"))
            continue
        version_match = _VERSION_RE.match(code.lower())
        if version_match is not None:
            if version is not None:
                raise ShaderSourceDecodeError(
                    f"line {line.index + 1}: a second version line {code!r}"
                )
            if version_match.group("kind") != "ps":
                raise ShaderSourceDecodeError(
                    f"line {line.index + 1}: {code!r} is not pixel-shader assembly"
                )
            version = ShaderVersion(
                major=int(version_match.group("major")),
                minor=int(version_match.group("minor")),
                text=code,
                line=line.index,
                source_offset=line.offset + line.code.index(code[0]),
            )
            lines.append(lexer.classify(line, "version"))
            continue
        if version is None:
            raise ShaderSourceDecodeError(
                f"line {line.index + 1}: {code!r} precedes the version line"
            )
        co_issued = pending_co_issue
        co_issue_marker = "trailing-on-previous" if pending_co_issue else None
        pending_co_issue = False
        if code.startswith("+"):
            co_issued = True
            co_issue_marker = "leading"
            code = code[1:].strip()
        if code.endswith("+"):
            pending_co_issue = True
            code = code[:-1].strip()
        match = _MNEMONIC_RE.match(code)
        if match is None:
            raise ShaderSourceDecodeError(f"line {line.index + 1}: unclassified line {code!r}")
        mnemonic, modifiers = _split_mnemonic(match.group("mnemonic"), line.index)
        if mnemonic not in MNEMONIC_OPCODES:
            # The ps.1.x vocabulary is closed, so an unknown mnemonic is a parse this seam does
            # not understand rather than a token to pass through untyped.
            raise ShaderSourceDecodeError(
                f"line {line.index + 1}: unknown ps.1.x mnemonic {mnemonic!r}"
            )
        rest = (match.group("rest") or "").strip()
        offset = line.offset + line.code.index(line.code.strip()[0])
        if mnemonic == "def":
            arguments = [part for part in re.split(r"[,\s]+", rest) if part]
            if len(arguments) != 5:
                raise ShaderSourceDecodeError(
                    f"line {line.index + 1}: def declares {len(arguments) - 1} value(s), not four"
                )
            destination = _parse_destination(arguments[0], line.index)
            if destination.register.register_class != "const":
                raise ShaderSourceDecodeError(
                    f"line {line.index + 1}: def targets {destination.text!r}, not a constant"
                )
            define = Define(
                line=line.index,
                source_offset=offset,
                register=destination.register,
                values=tuple(_parse_float(value, line.index) for value in arguments[1:]),
                text=code,
            )
            defines.append(define)
            ordered.append(define)
            lines.append(lexer.classify(line, "define"))
            continue
        if mnemonic == "phase":
            if rest:
                raise ShaderSourceDecodeError(
                    f"line {line.index + 1}: phase carries operands {rest!r}"
                )
            instruction = Instruction(
                line=line.index,
                source_offset=offset,
                co_issued=False,
                co_issue_marker=None,
                opcode="phase",
                modifiers=(),
                destination=None,
                sources=(),
                text=code,
            )
            instructions.append(instruction)
            ordered.append(instruction)
            lines.append(lexer.classify(line, "phase"))
            continue
        arguments = [part.strip() for part in rest.split(",") if part.strip()]
        if not arguments:
            raise ShaderSourceDecodeError(
                f"line {line.index + 1}: {mnemonic!r} carries no operands"
            )
        instruction = Instruction(
            line=line.index,
            source_offset=offset,
            co_issued=co_issued,
            co_issue_marker=co_issue_marker,
            opcode=mnemonic,
            modifiers=modifiers,
            destination=_parse_destination(arguments[0], line.index),
            sources=tuple(_parse_operand(argument, line.index) for argument in arguments[1:]),
            text=code,
        )
        instructions.append(instruction)
        ordered.append(instruction)
        lines.append(lexer.classify(line, "instruction"))
    if version is None:
        raise ShaderSourceDecodeError("the source declares no ps.x.y version line")
    if pending_co_issue:
        raise ShaderSourceDecodeError("the source ends on a co-issue marker with nothing to pair")
    return version, lines, defines, instructions, comments, ordered, encoding


def _source_claims(lines: list[SourceLine]) -> list[tuple[int, int, str, str]]:
    """One claim for each line's code, one for the comment that followed it."""

    claims: list[tuple[int, int, str, str]] = []
    for line in lines:
        if line.comment_offset is None:
            claims.append((line.offset, line.length, "mapped-text", f"source.lines[{line.index}]"))
            continue
        code_length = line.comment_offset - line.offset
        claims.append((line.offset, code_length, "mapped-text", f"source.lines[{line.index}]"))
        claims.append(
            (
                line.comment_offset,
                line.offset + line.length - line.comment_offset,
                "mapped-text",
                f"source.lines[{line.index}].comment",
            )
        )
    return claims


def decode_shader_source(closure: ShaderSourceClosure) -> ShaderSourceModel:
    """One `vtmb:shader-source:` unit from one readable `.psh`.

    A member the install carries as zero bytes has no version line to read, so it publishes an
    empty source block, an `empty-member` omission and a zero-length ledger rather than refusing:
    an absent member and a member that is present and empty are different facts about the install.
    """

    member = closure.psh
    anomalies: list[dict[str, Any]] = []
    omissions: list[dict[str, Any]] = []
    if member.data:
        version, lines, defines, instructions, comments, _, encoding = _parse_source_text(
            member.data
        )
    else:
        version, lines, defines, instructions, comments = None, [], [], [], []
        _, encoding = lexer.decode_text(b"")
        omissions.append(
            {
                "role": "empty-member",
                "sourcePath": member.path,
                "byteLength": 0,
                "evidence": "the install resolves this member and it holds zero bytes",
            }
        )
    if encoding != "ascii":
        anomalies.append(
            {
                "role": "non-ascii-source",
                "encoding": encoding,
                "evidence": "the member holds bytes outside ASCII; latin-1 round-trips them",
            }
        )
    markers = {line.marker for line in lines if line.marker}
    if "//" in markers:
        anomalies.append(
            {
                "role": "cpp-comment-marker",
                "evidence": "the source spells C++ // comments beside the assembler's ;",
                "lines": [line.index for line in lines if line.marker == "//"],
            }
        )
    model = ShaderSourceModel(
        key=closure.key,
        asset_id=closure.asset_id,
        members=closure.members(),
        version=version,
        defines=defines,
        instructions=instructions,
        comments=comments,
        lines=lines,
        encoding=encoding,
        compiled_twin={
            "present": closure.compiled_twin_present,
            "asset": closure.compiled_twin_asset,
            "sourcePath": closure.compiled_twin_path,
        },
        anomalies=anomalies,
        omissions=omissions,
    )
    if closure.compiled_twin_present:
        # The twin is another unit of this seam, so it is named by stable ID and declared once
        # in the dependency table rather than carried as a path alone. No hash: this unit parses
        # its own text and reads nothing of the compiled bundle, so it pins no referenced bytes.
        model.dependencies = [
            dependency(
                "compiled-twin",
                closure.compiled_twin_asset,
                closure.compiled_twin_path,
                True,
            )
        ]
    else:
        # The reference is still made -- the identity names it -- so it keeps the missing
        # sentinel, declares no dependency row, and enters coverage as an omission that says why.
        model.omissions.append(
            {
                "role": "absent-compiled-twin",
                "asset": closure.compiled_twin_asset,
                "sourcePath": closure.compiled_twin_path,
                "reason": "the install resolves no compiled bundle of this stem",
            }
        )
    model.byte_ledger = [build_ledger(member, _source_claims(lines))]
    return model


def shader_source_coverage_for(model: ShaderSourceModel) -> dict[str, Any]:
    return shader_source_coverage(
        typed_unidentified=model.typed_unidentified,
        omitted_proven=model.omissions,
        byte_ledger=model.byte_ledger,
        unresolved=model.unresolved,
        unsupported=model.unsupported,
    )


# --------------------------------------------------------------------------- compiled bundle


def _header_typed_unidentified(header: dict[str, Any]) -> list[dict[str, Any]]:
    """The three header words this generation of the toolchain does not explain.

    They are carried as stored, at their own offsets, rather than dropped or interpreted:
    `dynamicCombos` is not read as a divisor of `totalCombos` until its role is verified.
    """

    return [
        {
            "field": "header.dynamicCombos",
            "sourceOffset": 8,
            "value": header["dynamicCombos"],
            "reason": "the dynamic-combo count's role in the combo index is not verified",
        },
        {
            "field": "header.flags",
            "sourceOffset": 12,
            "value": header["flags"],
            "reason": "the flag word's meaning in this toolchain generation is not verified",
        },
        {
            "field": "header.centroidMask",
            "sourceOffset": 16,
            "value": header["centroidMask"],
            "reason": "the centroid mask's per-register meaning is not verified",
        },
    ]


#: Why a combo table entry owns no stream, and the evidence that proves it of every entry the
#: run collapses. An entry demoted by `combo-out-of-bounds` states a stream the member cannot
#: hold, which is a different fact from an entry that states no stream at all, so the two never
#: share a run and never share an omission row.
ABSENT_REASONS = {
    "absent-no-stream": "the entries state a non-positive offset or a size of zero and point at no stream",
    "absent-out-of-bounds": "the entries name bytes outside the member, each recorded as combo-out-of-bounds",
}


def _absent_runs(entries: list[tuple[int, int, bool, str]]) -> list[dict[str, Any]]:
    """Consecutive table entries that point at no stream, collapsed into runs.

    One shipped bundle declares 65536 combos and fills 63436 of them with `(-1, 0)`; a row each
    would state the same absence sixty thousand times. A run carries the offset of its first
    entry and the offset/size words those entries actually hold, so the run and the table bytes
    it describes name the same place.
    """

    runs: list[dict[str, Any]] = []
    for index, (offset, size, present, reason) in enumerate(entries):
        if present:
            continue
        last = runs[-1] if runs else None
        if last is not None and last["start"] + last["count"] == index \
                and last["offset"] == offset and last["size"] == size \
                and last["reason"] == reason:
            last["count"] += 1
            continue
        runs.append(
            {
                "start": index,
                "count": 1,
                "sourceOffset": HEADER_BYTES + 8 * index,
                "offset": offset,
                "size": size,
                "reason": reason,
            }
        )
    return runs


def _fill_claim(
    data: bytes,
    start: int,
    end: int,
    owner: str,
    role: str,
    claims: list[tuple[int, int, str, str]],
    omissions: list[dict[str, Any]],
) -> None:
    """Account for bytes between or after combo streams, zero or not."""

    if end <= start:
        return
    span = data[start:end]
    if not any(span):
        claims.append((start, end - start, "padding-zero", "vcs.padding"))
        return
    claims.append((start, end - start, "omitted-proven", owner))
    omissions.append(
        {
            "role": role,
            "sourceOffset": start,
            "byteLength": end - start,
            "sha256": hashlib.sha256(span).hexdigest(),
            "evidence": "no combo entry points at these bytes and they are not zero",
        }
    )


def _decode_stream(
    data: bytes,
    index: int,
    offset: int,
    size: int,
    claims: list[tuple[int, int, str, str]],
    anomalies: list[dict[str, Any]],
    omissions: list[dict[str, Any]],
) -> dict[str, Any]:
    """One combo stream: its tokens, its disassembly, its constant table and its comments."""

    stream = data[offset:offset + size]
    row: dict[str, Any] = {
        "index": index,
        "sourceOffset": HEADER_BYTES + 8 * index,
        "offset": offset,
        "size": size,
        "model": None,
        "tokens": [],
        "instructions": [],
        "constantTable": None,
        "comments": [],
        "sha256": hashlib.sha256(stream).hexdigest(),
    }
    if size >= 4 and struct.unpack_from("<I", stream, size - 4)[0] != tokens_module.END_TOKEN:
        anomalies.append(
            {
                "role": "stream-without-end-token",
                "combo": index,
                "sourceOffset": offset + size - 4,
                "evidence": "the stream's last token is not 0x0000FFFF",
            }
        )
    try:
        decoded = tokens_module.decode_stream(stream, base_offset=offset)
    except tokens_module.TokenStreamError as error:
        anomalies.append(
            {
                "role": "stream-not-tokenizable",
                "combo": index,
                "sourceOffset": offset,
                "evidence": str(error),
            }
        )
        claims.append((offset, size, "omitted-proven", f"vcs.combos[{index}].undecoded"))
        omissions.append(
            {
                "role": "undecoded-combo-stream",
                "combo": index,
                "sourceOffset": offset,
                "byteLength": size,
                "sha256": row["sha256"],
                "evidence": str(error),
            }
        )
        return row
    row["tokens"] = decoded
    row["model"] = decoded[0]["model"]
    row["instructions"] = tokens_module.disassemble(decoded)
    region_start = offset
    for token in decoded:
        if token["kind"] != "comment" or not token["payloadDwords"]:
            continue
        payload_start = token["payloadOffset"]
        payload_length = 4 * token["payloadDwords"]
        payload = data[payload_start:payload_start + payload_length]
        decoded_table = None
        if payload[:4] == ctab_module.FOURCC and row["constantTable"] is None:
            try:
                decoded_table = ctab_module.decode(payload, payload_start)
            except ctab_module.ConstantTableError as error:
                anomalies.append(
                    {
                        "role": "constant-table-undecodable",
                        "combo": index,
                        "sourceOffset": payload_start,
                        "evidence": str(error),
                    }
                )
            else:
                row["constantTable"] = decoded_table
                if decoded_table["unaccountedBytes"]:
                    anomalies.append(
                        {
                            "role": "constant-table-unaccounted",
                            "combo": index,
                            "sourceOffset": payload_start,
                            "byteLength": decoded_table["unaccountedBytes"],
                            "evidence": "the constant table holds bytes no section describes",
                        }
                    )
        if decoded_table is not None:
            owner = f"vcs.combos[{index}].constantTable"
        else:
            owner = f"vcs.combos[{index}].comments[{len(row['comments'])}]"
            row["comments"].append(
                {
                    "tokenIndex": token["index"],
                    "sourceOffset": payload_start,
                    "byteLength": payload_length,
                    "fourCC": token["fourCC"],
                    "hex": payload.hex(),
                }
            )
        claims.append((region_start, payload_start - region_start, "mapped",
                       f"vcs.combos[{index}].tokens"))
        claims.append((payload_start, payload_length, "mapped", owner))
        region_start = payload_start + payload_length
    claims.append((region_start, offset + size - region_start, "mapped",
                   f"vcs.combos[{index}].tokens"))
    return row


def _compare_with_source(
    closure: ShaderProgramClosure,
    combos: list[dict[str, Any]],
    data: bytes,
    anomalies: list[dict[str, Any]],
) -> dict[str, Any] | None:
    """Assemble the readable twin and weigh every combo against it, token for token."""

    twin = closure.twin
    if twin is None:
        return None
    comparison: dict[str, Any] = {
        "source": twin.asset_id,
        "sourcePath": twin.path,
        "assembled": False,
        "assembledTokens": 0,
        "combosMatched": [],
        "combosDiffering": [],
        "state": "not-assembled",
    }
    try:
        version, _, _, _, _, ordered, _ = _parse_source_text(twin.data)
        words = assemble(version, ordered)
    except (ShaderSourceDecodeError, AssemblyError) as error:
        comparison["error"] = str(error)
        anomalies.append(
            {
                "role": "source-not-assembled",
                "source": twin.asset_id,
                "evidence": str(error),
            }
        )
        return comparison
    comparison["assembled"] = True
    comparison["assembledTokens"] = len(words)
    for combo in combos:
        if "tokens" not in combo:
            continue                            # an alias or an overlap owns no stream of its own
        offset, size = combo["offset"], combo["size"]
        stream = struct.unpack_from(f"<{size // 4}I", data, offset) if size >= 4 else ()
        if list(stream) == words:
            comparison["combosMatched"].append(combo["index"])
            continue
        position = next(
            (
                place
                for place, (assembled, compiled) in enumerate(zip(words, stream))
                if assembled != compiled
            ),
            min(len(words), len(stream)),
        )
        row = {
            "combo": combo["index"],
            "firstDifferingToken": position,
            "assembledTokens": len(words),
            "comboTokens": len(stream),
            "assembledValue": words[position] if position < len(words) else None,
            "comboValue": stream[position] if position < len(stream) else None,
        }
        comparison["combosDiffering"].append(row)
        anomalies.append(
            {
                "role": "source-binary-drift",
                "combo": combo["index"],
                "source": twin.asset_id,
                "sourceOffset": offset + 4 * position,
                "firstDifferingToken": position,
                "evidence": (
                    "the readable source assembles to a token the compiled combo does not carry"
                ),
            }
        )
    comparison["state"] = (
        "equivalent" if not comparison["combosDiffering"] else "source-binary-drift"
    )
    return comparison


#: Why a `vsh/` or `fxc/` bundle names no readable source even where the install holds a
#: same-stem `.psh`. 25 shipped `vsh/` and `fxc/` stems have one, and it is pixel-shader assembly
#: in every case, so joining on the stem would manufacture drift against a different program.
NOT_COMPARABLE_REASON = (
    "materials/dxshaders holds pixel-shader assembly only, so a same-stem member there is a "
    "different program of that name rather than this bundle's source"
)


def _readable_source(closure: ShaderProgramClosure) -> dict[str, Any]:
    """What the install actually holds at `materials/dxshaders/<stem>.psh`, and whether it counts.

    `present` is the index's answer for the joined path whatever the subdirectory is, so the
    block never denies a member the install carries. `comparable` is the seam's rule, and only a
    comparable bundle names a `vtmb:shader-source:` unit at all -- resolved, or as the missing
    sentinel when the install holds no source of that stem.
    """

    block: dict[str, Any] = {
        "present": closure.twin_present,
        "comparable": closure.twin_expected,
        "sourcePath": closure.twin_path,
        "asset": closure.twin_asset,
    }
    if not closure.twin_expected:
        block["reason"] = NOT_COMPARABLE_REASON
    elif not closure.twin_present:
        block["reason"] = "the install resolves no readable source of this stem"
    return block


def decode_shader_program(closure: ShaderProgramClosure) -> ShaderProgramModel:
    """One `vtmb:shader-program:` unit from one compiled `.vcs` bundle.

    A member the install carries as zero bytes holds no container to read, so it publishes a
    null header and combo table, an `empty-member` omission and a zero-length ledger. A member
    that holds bytes but too few for the 20-byte header is a different fact -- something is there
    and it is not this container -- and is refused.
    """

    member = closure.vcs
    data = member.data
    readable_source = _readable_source(closure)
    twin_omissions: list[dict[str, Any]] = []
    if closure.twin_expected and not closure.twin_present:
        twin_omissions.append(
            {
                "role": "absent-readable-source",
                "asset": closure.twin_asset,
                "sourcePath": closure.twin_path,
                "reason": "the install resolves no readable source of this stem",
            }
        )
    if not data:
        empty = ShaderProgramModel(
            key=closure.key,
            asset_id=closure.asset_id,
            members=closure.members(),
            header=None,
            combo_table=None,
            combos=[],
            source_comparison=None,
            readable_source=readable_source,
            omissions=[
                {
                    "role": "empty-member",
                    "sourcePath": member.path,
                    "byteLength": 0,
                    "evidence": "the install resolves this member and it holds zero bytes",
                },
                *twin_omissions,
            ],
        )
        empty.byte_ledger = [build_ledger(member, [])]
        return empty
    if len(data) < HEADER_BYTES:
        raise ShaderProgramDecodeError(
            f"{member.path}: {len(data)} bytes cannot hold the 20-byte header"
        )
    version, total_combos, dynamic_combos, flags, centroid_mask = struct.unpack_from(
        "<5i", data, 0
    )
    header = {
        "sourceOffset": 0,
        "version": version,
        "totalCombos": total_combos,
        "dynamicCombos": dynamic_combos,
        "flags": flags,
        "centroidMask": centroid_mask,
    }
    claims: list[tuple[int, int, str, str]] = [(0, HEADER_BYTES, "mapped", "vcs.header")]
    anomalies: list[dict[str, Any]] = []
    omissions: list[dict[str, Any]] = []
    if version != 0:
        anomalies.append(
            {
                "role": "unexpected-container-version",
                "sourceOffset": 0,
                "value": version,
                "evidence": "every shipped bundle states container version 0",
            }
        )
    fits = max(0, (len(data) - HEADER_BYTES) // 8)
    declared = max(0, total_combos)
    readable = min(declared, fits)
    if total_combos < 0:
        anomalies.append(
            {
                "role": "negative-combo-count",
                "sourceOffset": 4,
                "value": total_combos,
                "evidence": "the header states a negative combo count",
            }
        )
    if readable < declared:
        anomalies.append(
            {
                "role": "combo-table-truncated",
                "sourceOffset": HEADER_BYTES,
                "declared": declared,
                "readable": readable,
                "evidence": "the file ends inside the combo table it declares",
            }
        )
    table_length = 8 * readable
    claims.append((HEADER_BYTES, table_length, "mapped", "vcs.comboTable"))
    table_end = HEADER_BYTES + table_length

    entries: list[tuple[int, int, bool, str]] = []
    for index in range(readable):
        offset, size = struct.unpack_from("<2i", data, HEADER_BYTES + 8 * index)
        present = size > 0 and offset > 0
        reason = "" if present else "absent-no-stream"
        if present and (offset < table_end or offset + size > len(data)):
            anomalies.append(
                {
                    "role": "combo-out-of-bounds",
                    "combo": index,
                    "sourceOffset": HEADER_BYTES + 8 * index,
                    "offset": offset,
                    "size": size,
                    "evidence": f"the entry leaves the {len(data)}-byte member",
                }
            )
            present, reason = False, "absent-out-of-bounds"
        entries.append((offset, size, present, reason))

    combos: list[dict[str, Any]] = []
    owners: dict[tuple[int, int], int] = {}
    overlapping: set[int] = set()
    ordered = sorted(
        (
            (offset, size, index)
            for index, (offset, size, present, _) in enumerate(entries)
            if present
        )
    )
    cursor = table_end
    decoded_rows: dict[int, dict[str, Any]] = {}
    for offset, size, index in ordered:
        key = (offset, size)
        if key in owners:
            continue
        if offset < cursor:
            anomalies.append(
                {
                    "role": "combo-table-overlap",
                    "combo": index,
                    "sourceOffset": HEADER_BYTES + 8 * index,
                    "offset": offset,
                    "size": size,
                    "evidence": f"the stream starts inside bytes already claimed at {cursor}",
                }
            )
            overlapping.add(index)
            continue
        _fill_claim(data, cursor, offset, "vcs.interComboFill", "inter-combo-fill",
                    claims, omissions)
        owners[key] = index
        decoded_rows[index] = _decode_stream(
            data, index, offset, size, claims, anomalies, omissions
        )
        cursor = offset + size
    _fill_claim(data, cursor, len(data), "vcs.trailingFill", "trailing-fill", claims, omissions)

    for index, (offset, size, present, _) in enumerate(entries):
        if not present:
            continue
        owner = owners.get((offset, size))
        if owner == index:
            combos.append(decoded_rows[index])
            continue
        row = {
            "index": index,
            "sourceOffset": HEADER_BYTES + 8 * index,
            "offset": offset,
            "size": size,
            "aliasOf": owner,
            "model": decoded_rows[owner]["model"] if owner is not None else None,
            "sha256": hashlib.sha256(data[offset:offset + size]).hexdigest(),
        }
        if index in overlapping:
            # The entry names bytes another entry already owns, so this row states the entry the
            # table holds without claiming a stream of its own.
            row["overlapping"] = True
        combos.append(row)
    combos.sort(key=lambda combo: combo["index"])

    absent = _absent_runs(entries)
    combo_table = {
        "sourceOffset": HEADER_BYTES,
        "byteLength": table_length,
        "entries": readable,
        "declaredEntries": declared,
        "presentEntries": sum(1 for _, _, present, _ in entries if present),
        "absentEntries": sum(run["count"] for run in absent),
        "absentRuns": absent,
    }
    for reason, evidence in ABSENT_REASONS.items():
        reason_runs = [run for run in absent if run["reason"] == reason]
        if not reason_runs:
            continue
        omissions.append(
            {
                "role": "absent-combo-entries",
                "sourceOffset": reason_runs[0]["sourceOffset"],
                "reason": reason,
                "entries": sum(run["count"] for run in reason_runs),
                "evidence": evidence,
            }
        )

    omissions.extend(twin_omissions)
    model = ShaderProgramModel(
        key=closure.key,
        asset_id=closure.asset_id,
        members=closure.members(),
        header=header,
        combo_table=combo_table,
        combos=combos,
        source_comparison=None,
        readable_source=readable_source,
        anomalies=anomalies,
        omissions=omissions,
        typed_unidentified=_header_typed_unidentified(header),
    )
    model.source_comparison = _compare_with_source(closure, combos, data, anomalies)
    if closure.twin is not None:
        model.dependencies = [
            dependency(
                "shader-source",
                closure.twin.asset_id,
                closure.twin.path,
                True,
                byteLength=closure.twin.byte_length,
                sha256=closure.twin.sha256,
            )
        ]
    model.byte_ledger = [build_ledger(member, claims)]
    return model


def shader_program_coverage_for(model: ShaderProgramModel) -> dict[str, Any]:
    return shader_program_coverage(
        typed_unidentified=model.typed_unidentified,
        omitted_proven=model.omissions,
        byte_ledger=model.byte_ledger,
        unresolved=model.unresolved,
        unsupported=model.unsupported,
    )


def source_resolution_for(model: ShaderSourceModel | ShaderProgramModel) -> dict[str, Any]:
    """The member table, built by the contract from the members the closure resolved."""

    return source_resolution(model.members)
