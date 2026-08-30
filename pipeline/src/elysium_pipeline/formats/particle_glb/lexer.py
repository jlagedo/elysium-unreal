"""Offset-carrying KeyValues lexer and parser for one particle definition.

The tokenizer is `formats/surface_property_glb/lexer.py`'s: `//` outside a quoted string starts a
line comment, `"` quotes a string that may span lines, `{`/`}` nest, and anything else runs to
the next whitespace, brace, quote or comment. `formats/particles.py` documents the same grammar
for this seam's own source (`kv.parse`), but discards offsets and repeats on the way; this module
keeps both, because the unit's byte ledger is written against the spans and a repeated key is a
recorded anomaly rather than a silent overwrite.

A particle document nests one level deeper than a surface-property table: the root is always one
named block (`Particle { ... }`), and `spawn`, `collide` and `decal` are named sub-blocks inside
it and inside each other. `parse` walks the token stream by hand instead of recursing so that a
malformed file -- an orphaned `{` behind a commented-out `spawn`, a quote left open across several
keys -- degrades into a best-effort tree plus one trailing `unparsed` region instead of raising:
`fire2_emitter`, the Tourette suicide definition, and sixteen more of the shipped 1,698 ship this
way, and the corpus is read, not curated.
"""

from __future__ import annotations

from dataclasses import dataclass, field

from elysium_pipeline.formats.particle_glb.model import BLOCK_KINDS

BOM = "\xef\xbb\xbf"
_WHITESPACE = " \t\r\n\v\f"
_BARE_END = set(' \t\r\n\v\f{}"')

#: The three sub-block keywords a particle document nests. Every other bare/quoted key names a
#: scalar pair.
BLOCK_KEYWORDS = frozenset(BLOCK_KINDS)


class ParticleLexError(ValueError):
    """The byte stream cannot be tokenized at all (never raised by a well-formed KeyValues file;
    kept for a lexer that stalls, which `tokenize` treats as a defensive impossibility)."""


@dataclass(frozen=True, slots=True)
class Token:
    kind: str          # "string" | "open" | "close" | "comment" | "whitespace" | "bom"
    text: str          # decoded content; for a string this is the unescaped value
    offset: int        # byte offset of the token's first byte
    length: int        # byte length of the whole token span, quotes and escapes included
    quoted: bool = False
    anomaly: str = ""  # a named departure from the grammar, or "" for a well-formed token

    @property
    def end(self) -> int:
        return self.offset + self.length


def decode_text(data: bytes) -> str:
    """One character per byte, so a character offset is a byte offset."""

    return data.decode("latin-1")


def tokenize(text: str) -> list[Token]:
    """Every byte of `text` lands in exactly one token, in order."""

    tokens: list[Token] = []
    index, total = 0, len(text)
    if text.startswith(BOM):
        tokens.append(Token("bom", BOM, 0, len(BOM)))
        index = len(BOM)
    while index < total:
        char = text[index]
        if char in _WHITESPACE:
            start = index
            while index < total and text[index] in _WHITESPACE:
                index += 1
            tokens.append(Token("whitespace", text[start:index], start, index - start))
        elif char == "/" and text[index + 1:index + 2] == "/":
            start = index
            stop = text.find("\n", index)
            index = total if stop < 0 else stop
            tokens.append(Token("comment", text[start:index], start, index - start))
        elif char == '"':
            start = index
            index += 1
            out: list[str] = []
            closed = False
            while index < total:
                if text[index] == "\\" and text[index + 1:index + 2] == '"':
                    out.append('"')
                    index += 2
                    continue
                if text[index] == '"':
                    index += 1
                    closed = True
                    break
                out.append(text[index])
                index += 1
            anomaly = "" if closed else "unterminated-quoted-string"
            tokens.append(Token("string", "".join(out), start, index - start, True, anomaly))
        elif char in "{}":
            kind = "open" if char == "{" else "close"
            tokens.append(Token(kind, char, index, 1))
            index += 1
        else:
            start = index
            while (
                index < total
                and text[index] not in _BARE_END
                and text[index:index + 2] != "//"
            ):
                index += 1
            if index == start:                       # defensive: never emit a zero-width token
                raise ParticleLexError(f"lexer stalled at byte {start}")
            tokens.append(Token("string", text[start:index], start, index - start))
    return tokens


@dataclass(slots=True)
class KeyRecord:
    """One `key value` pair, wherever in the tree it was declared."""

    key: Token
    value: Token
    block: int | None          # index into `ParsedDocument.blocks`, or None for the root


@dataclass(slots=True)
class BlockRecord:
    """One `spawn`, `collide` or `decal` sub-block."""

    index: int
    kind: str
    parent: int | None
    name: Token
    open_token: Token
    close_token: Token | None

    @property
    def offset(self) -> int:
        return self.name.offset

    @property
    def end(self) -> int:
        return (self.close_token or self.open_token).end


@dataclass(slots=True)
class ParsedDocument:
    """The best-effort tree one particle file's significant tokens produce.

    `unparsed_offset` is set the moment the walk can no longer attribute a token to the tree; from
    there to end of file is one `unparsed` region the decoder claims verbatim, so the ledger stays
    gapless over a file the walk could not fully structure.
    """

    root: Token | None
    root_close: Token | None
    keys: list[KeyRecord] = field(default_factory=list)
    blocks: list[BlockRecord] = field(default_factory=list)
    anomalies: list[dict] = field(default_factory=list)
    unparsed_offset: int | None = None


@dataclass(slots=True)
class _Frame:
    block_index: int | None
    seen_keys: set[str] = field(default_factory=set)


def parse(tokens: list[Token]) -> ParsedDocument:
    """Structure the token stream into one root, its scalar keys and its sub-blocks.

    The root is the first significant token when it is immediately followed by `{`; a shipped
    handful of files carry no wrapper at all (`"bumpscale" "0.3"`, no `Particle` and no braces),
    so a first token with no following `{` is read as the beginning of a flat, rootless pair list
    instead -- `root-not-particle` still fires, because there is no root to be `Particle`.
    """

    significant = [token for token in tokens if token.kind in ("string", "open", "close")]
    doc = ParsedDocument(root=None, root_close=None)
    cursor = 0

    if significant and significant[0].kind == "string":
        candidate = significant[0]
        has_wrapper = len(significant) > 1 and significant[1].kind == "open"
        if has_wrapper:
            doc.root = candidate
            cursor = 2
            if candidate.text != "Particle":
                doc.anomalies.append(
                    {"role": "root-not-particle", "offset": candidate.offset, "root": candidate.text}
                )
        else:
            doc.anomalies.append(
                {
                    "role": "root-not-particle",
                    "offset": candidate.offset,
                    "root": "",
                    "reason": "no-root-block",
                }
            )
    elif significant:
        doc.anomalies.append(
            {
                "role": "root-not-particle",
                "offset": significant[0].offset,
                "root": "",
                "reason": "no-root-token",
            }
        )

    if doc.root is not None and cursor < len(significant) and significant[cursor].kind == "close":
        doc.anomalies.append({"role": "empty-definition", "offset": doc.root.offset})

    stack = [_Frame(block_index=None)]
    while cursor < len(significant):
        token = significant[cursor]
        frame = stack[-1]
        if token.kind == "close":
            if len(stack) > 1:
                closed_index = stack.pop().block_index
                assert closed_index is not None
                doc.blocks[closed_index].close_token = token
                cursor += 1
                continue
            if doc.root is not None and doc.root_close is None:
                doc.root_close = token
                cursor += 1
                if cursor < len(significant):
                    # Trailing content the grammar has no place for; the walk cannot tell whether
                    # it belongs to a second (invalid) root or is simply garbage, so it is claimed
                    # as one more unparsed region rather than guessed at.
                    doc.unparsed_offset = significant[cursor].offset
                break
            # A stray close with nothing open to match: bail rather than guess what it closes.
            doc.anomalies.append({"role": "unbalanced-braces", "offset": token.offset})
            doc.unparsed_offset = token.offset
            break
        if token.kind == "open":
            # An orphan `{` with no key naming it -- the shape a commented-out `spawn` leaves
            # behind. The tree the walk already built stands; everything from here is unparsed.
            doc.anomalies.append({"role": "unbalanced-braces", "offset": token.offset})
            doc.unparsed_offset = token.offset
            break
        # token.kind == "string": either a sub-block name or a scalar key.
        folded = token.text.strip().lower()
        following = significant[cursor + 1] if cursor + 1 < len(significant) else None
        if folded in BLOCK_KEYWORDS and following is not None and following.kind == "open":
            block_index = len(doc.blocks)
            doc.blocks.append(
                BlockRecord(
                    index=block_index,
                    kind=folded,
                    parent=frame.block_index,
                    name=token,
                    open_token=following,
                    close_token=None,
                )
            )
            stack.append(_Frame(block_index=block_index))
            cursor += 2
            continue
        if following is not None and following.kind == "string":
            if folded in frame.seen_keys:
                doc.anomalies.append(
                    {"role": "repeated-scalar-key", "offset": token.offset, "key": folded}
                )
            frame.seen_keys.add(folded)
            doc.keys.append(KeyRecord(key=token, value=following, block=frame.block_index))
            cursor += 2
            continue
        # A key with nothing usable behind it: not a value, not a block. The walk cannot resolve
        # it into either shape, so it is where the tree stops.
        doc.anomalies.append({"role": "unbalanced-braces", "offset": token.offset})
        doc.unparsed_offset = token.offset
        break

    if doc.unparsed_offset is None and len(stack) > 1:
        # Ran out of tokens with one or more blocks still open (a missing final `}`). The open
        # block's own name/open span is already claimed; nothing further remains to mark.
        unclosed = doc.blocks[stack[-1].block_index]
        doc.anomalies.append({"role": "unbalanced-braces", "offset": unclosed.offset})
    elif doc.unparsed_offset is None and doc.root is not None and doc.root_close is None:
        # The root itself never closed (the file ends mid-body with every block above it closed).
        doc.anomalies.append({"role": "unbalanced-braces", "offset": doc.root.offset})

    return doc
