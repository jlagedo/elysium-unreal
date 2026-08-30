"""Offset-carrying tokenizer shared by every `scripts/*.txt` table this seam cuts.

The six tables split into two families of grammar: `game_sounds_surfaceproperties.txt`,
`game_sounds_manifest.txt`, `sounds.txt` and `soundscapes.txt` are Source KeyValues (quoted
strings, `{`/`}` nesting, `//` line comments); `dsp_presets.txt` is the same brace-and-comment
skeleton with bareword numeric/symbol tokens and no quotes at all. One character-level tokenizer
answers both, because a KeyValues table never puts a quote character where the DSP table needs a
bareword, and the DSP table never opens a quote the tokenizer would have to special-case away.

The text is decoded Latin-1 so one character is one byte and a token span is a byte span
directly, matching `surface_property_glb/lexer.py`, which this module is cut from.
"""

from __future__ import annotations

from dataclasses import dataclass


class SoundScriptLexError(ValueError):
    """The byte stream does not tokenize at all."""


BOM = "\xef\xbb\xbf"
_WHITESPACE = " \t\r\n\v\f"
_BARE_END = set(' \t\r\n\v\f{}"')


@dataclass(frozen=True, slots=True)
class Token:
    kind: str          # "string" | "open" | "close" | "comment" | "whitespace" | "bom"
    text: str          # the decoded content; for a quoted string this is the unescaped value
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
                raise SoundScriptLexError(f"lexer stalled at byte {start}")
            tokens.append(Token("string", text[start:index], start, index - start))
    return tokens


def significant(tokens: list[Token]) -> list[Token]:
    """The tokens a parser walks: strings and braces, comments and whitespace stripped."""

    return [token for token in tokens if token.kind in ("string", "open", "close")]
