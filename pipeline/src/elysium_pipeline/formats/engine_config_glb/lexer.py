"""Offset-carrying lexers for the engine-config seam's text grammars.

Two grammars need a byte-offset-aware tokenizer: the console script (`cfg/*.cfg`, `valve.rc`,
`dummy.txt`) and the nested KeyValues tree (`detail.vbsp`). Both are decoded Latin-1 so one
character is one byte and a token span is a byte span directly, the same convention
`formats/surface_property_glb/lexer.py` uses (copied and adapted here per that package's own
instructions, rather than imported, because a seam owns its own decode).
"""

from __future__ import annotations

from dataclasses import dataclass, field

_WHITESPACE = " \t\r\n\v\f"
_BARE_END_CONSOLE = set(' \t\r\n\v\f";')
_BARE_END_KV = set(' \t\r\n\v\f{}"')


def decode_text(data: bytes) -> str:
    """One character per byte, so a character offset is a byte offset."""

    return data.decode("latin-1")


@dataclass(frozen=True, slots=True)
class Token:
    kind: str          # "string" | "semicolon" | "open" | "close" | "comment" | "whitespace"
    text: str          # decoded content; for a quoted string this is the unescaped value
    offset: int
    length: int
    quoted: bool = False
    anomaly: str = ""

    @property
    def end(self) -> int:
        return self.offset + self.length


class EngineConfigLexError(ValueError):
    """The byte stream does not tokenize under either grammar."""


def _tokenize(text: str, *, bare_end: set[str], braces: bool, semicolons: bool) -> list[Token]:
    tokens: list[Token] = []
    index, total = 0, len(text)
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
                if semicolons and text[index] == "\n":
                    break
                out.append(text[index])
                index += 1
            anomaly = "" if closed else "unterminated-quote"
            tokens.append(Token("string", "".join(out), start, index - start, True, anomaly))
        elif semicolons and char == ";":
            tokens.append(Token("semicolon", ";", index, 1))
            index += 1
        elif braces and char in "{}":
            kind = "open" if char == "{" else "close"
            tokens.append(Token(kind, char, index, 1))
            index += 1
        else:
            start = index
            while (
                index < total
                and text[index] not in bare_end
                and text[index:index + 2] != "//"
            ):
                index += 1
            if index == start:
                raise EngineConfigLexError(f"lexer stalled at byte {start}")
            tokens.append(Token("string", text[start:index], start, index - start))
    return tokens


def tokenize_console(text: str) -> list[Token]:
    """Console-script tokens: bare/quoted strings, `;` separators, `//` comments, whitespace."""

    return _tokenize(text, bare_end=_BARE_END_CONSOLE, braces=False, semicolons=True)


def tokenize_keyvalues(text: str) -> list[Token]:
    """Nested-KeyValues tokens: bare/quoted strings, `{`/`}`, `//` comments, whitespace."""

    return _tokenize(text, bare_end=_BARE_END_KV, braces=True, semicolons=False)


@dataclass(slots=True)
class KVPair:
    key: Token
    value: Token | None = None


@dataclass(slots=True)
class KVNode:
    """One named nested block: its name token, its braces, its pairs and its child nodes."""

    name: Token
    open_token: Token
    close_token: Token | None = None
    pairs: list[KVPair] = field(default_factory=list)
    children: list["KVNode"] = field(default_factory=list)
    anomalies: list[dict[str, object]] = field(default_factory=list)

    @property
    def offset(self) -> int:
        return self.name.offset

    @property
    def end(self) -> int:
        return (self.close_token or self.open_token).end


@dataclass(slots=True)
class KVParse:
    """Everything `parse_keyvalues_tree` found: the root nodes plus what fits nowhere in one.

    Retail `detail.vbsp` is not a well-formed brace tree (`specDeviations`): one file carries an
    extra, unnamed `{` with no name to attach to. The parser never raises over that -- it always
    consumes every token and always terminates -- so `file_anomalies` and `orphan_pairs` are
    where a token that cannot nest into any block still gets a home a decoder can claim bytes
    against.
    """

    roots: list[KVNode]
    file_anomalies: list[dict[str, object]]
    orphan_pairs: list[KVPair]


def parse_keyvalues_tree(tokens: list[Token]) -> KVParse:
    """The root-level nodes of a nested KeyValues tree (`detail.vbsp`'s `detail { ... }`).

    A node's body is a free mix of `key value` pairs and further named blocks, exactly the
    shape `detail.vbsp` nests three deep (`detail.<type>.Group<n>.Model<n>`). The walk is an
    explicit stack, not recursion into a grammar the bytes are not guaranteed to honour: an
    unnamed `{` becomes an anonymous block, and a `}` or a pair with nothing open to attach to is
    recorded rather than raised over.
    """

    significant = [t for t in tokens if t.kind in ("string", "open", "close")]
    roots: list[KVNode] = []
    file_anomalies: list[dict[str, object]] = []
    orphan_pairs: list[KVPair] = []
    stack: list[KVNode] = []

    def attach(node: KVNode) -> None:
        (stack[-1].children if stack else roots).append(node)

    index, total = 0, len(significant)
    while index < total:
        token = significant[index]
        if token.kind == "open":
            name = Token("string", "", token.offset, 0)
            node = KVNode(name, token)
            node.anomalies.append({"role": "anonymous-block", "offset": token.offset})
            attach(node)
            stack.append(node)
            index += 1
            continue
        if token.kind == "close":
            if stack:
                stack.pop().close_token = token
            else:
                file_anomalies.append({"role": "unmatched-close-brace", "offset": token.offset})
            index += 1
            continue
        following = significant[index + 1] if index + 1 < total else None
        if following is not None and following.kind == "open":
            node = KVNode(token, following)
            if token.anomaly:
                node.anomalies.append(
                    {"role": token.anomaly, "offset": token.offset, "length": token.length}
                )
            attach(node)
            stack.append(node)
            index += 2
            continue
        if following is not None and following.kind == "string":
            pair = KVPair(token, following)
            index += 2
        else:
            pair = KVPair(token, None)
            index += 1
        target = stack[-1] if stack else None
        if target is None:
            orphan_pairs.append(pair)
        else:
            if pair.value is None:
                target.anomalies.append(
                    {"role": "valueless-key", "offset": token.offset, "key": token.text}
                )
            target.pairs.append(pair)
        for tok in (pair.key, pair.value):
            if tok is not None and tok.anomaly:
                anomaly = {"role": tok.anomaly, "offset": tok.offset, "length": tok.length}
                (target.anomalies if target is not None else file_anomalies).append(anomaly)

    while stack:
        node = stack.pop()
        node.anomalies.append(
            {"role": "unclosed-block-at-end-of-file", "offset": node.open_token.offset}
        )

    return KVParse(roots=roots, file_anomalies=file_anomalies, orphan_pairs=orphan_pairs)
