"""An offset-carrying tokenizer for the Python 2.1 grammar the level scripts are written in.

This interpreter's `tokenize` refuses the syntax the corpus uses -- `print x`, backtick repr,
`<>`, `0777` octal literals and `ur''` prefixes are all Python 2 -- so the seam brings its own
tokenizer rather than a partial read of the files. It follows the CPython tokenizer's shape:
physical lines carry indentation, a logical line ends at a `NEWLINE` outside every bracket, and a
blank or comment-only line ends with `NL` and leaves the indent stack alone.

The text is decoded Latin-1 so one character is one byte and a token offset is a byte offset. A
character the 2.1 grammar has no rule for stops the tokenizer with the offset that carried it:
a token that cannot be classified fails its unit rather than being skipped.
"""

from __future__ import annotations

from dataclasses import dataclass
import re

#: The token types the seam publishes, the same vocabulary CPython's `tokenize` speaks.
TOKEN_TYPES = (
    "NAME",
    "NUMBER",
    "STRING",
    "OP",
    "COMMENT",
    "NL",
    "NEWLINE",
    "INDENT",
    "DEDENT",
    "ENDMARKER",
)

#: How wide a tab is when a line's indentation is measured, as CPython 2 measured it.
TAB_SIZE = 8

BOM = "\xef\xbb\xbf"

#: Python 2.1 keywords. `print`, `exec` and `raise` are statements here, not builtins.
KEYWORDS = frozenset(
    {
        "and", "assert", "break", "class", "continue", "def", "del", "elif", "else", "except",
        "exec", "finally", "for", "from", "global", "if", "import", "in", "is", "lambda", "not",
        "or", "pass", "print", "raise", "return", "try", "while", "yield",
    }
)

_NAME = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
_NUMBER = re.compile(
    r"""
    (?: 0[xX][0-9a-fA-F]+[lL]?                              # hexadecimal, 2.x long suffix
      | (?:[0-9]+\.[0-9]*|\.[0-9]+) (?:[eE][-+]?[0-9]+)? [jJ]?   # a float, either way round
      | [0-9]+ [eE][-+]?[0-9]+ [jJ]?                        # an exponent with no point
      | [0-9]+ [lLjJ]?                                      # decimal, octal (`0777`), long
    )
    """,
    re.VERBOSE,
)
_STRING_PREFIX = re.compile(r"(?:[uU][rR]?|[rR])?(?='''|\"\"\"|'|\")")

#: The operators 2.1 spells, longest first so a prefix never wins over the whole token. `<>` is
#: 2.x's second spelling of `!=`; `//` is not here because floor division arrives in 2.2.
_OPERATORS = (
    "**=", ">>=", "<<=",
    "**", ">>", "<<", "<=", ">=", "==", "!=", "<>",
    "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=",
    "(", ")", "[", "]", "{", "}", ",", ":", ".", "`", "=", ";",
    "+", "-", "*", "/", "%", "&", "|", "^", "~", "<", ">",
)

_OPEN = {"(": ")", "[": "]", "{": "}"}
_CLOSE = {")", "]", "}"}


class ScriptLexError(ValueError):
    """The byte stream is not a Python 2.1 module."""


@dataclass(frozen=True, slots=True)
class Token:
    """One token, and the bytes of the source that spell it.

    `string` is the source spelling -- quotes, prefix and all -- because that is what a reader
    checking the token against the file at `offset` will see there. `INDENT` carries the
    indentation it opened; `DEDENT` and `ENDMARKER` are zero-width markers.
    """

    type: str
    string: str
    line: int
    col: int
    offset: int

    @property
    def length(self) -> int:
        return len(self.string)

    @property
    def end(self) -> int:
        return self.offset + len(self.string)


@dataclass(frozen=True, slots=True)
class Line:
    """One physical line of the source: where it starts, how long it is, how it ends."""

    index: int
    offset: int
    length: int
    terminator: str            # "crlf" | "lf" | "cr" | "" for a final line with no terminator
    indent: str                # the leading whitespace of the physical line


def decode_text(data: bytes) -> str:
    """One character per byte, so a character offset is a byte offset."""

    return data.decode("latin-1")


def split_lines(text: str) -> list[Line]:
    """Every physical line, with its terminator, partitioning the text exactly."""

    lines: list[Line] = []
    index = 0
    start = 0
    total = len(text)
    while start < total:
        cursor = start
        while cursor < total and text[cursor] not in "\r\n":
            cursor += 1
        if cursor >= total:
            terminator = ""
            end = total
        elif text[cursor] == "\r" and text[cursor + 1:cursor + 2] == "\n":
            terminator, end = "crlf", cursor + 2
        elif text[cursor] == "\r":
            terminator, end = "cr", cursor + 1
        else:
            terminator, end = "lf", cursor + 1
        body = text[start:cursor]
        indent = body[: len(body) - len(body.lstrip(" \t\f"))]
        lines.append(Line(index, start, end - start, terminator, indent))
        index += 1
        start = end
    return lines


def _column(indent: str) -> int:
    """The column an indentation string reaches, tabs rounded up to `TAB_SIZE`."""

    column = 0
    for char in indent:
        if char == "\t":
            column = (column // TAB_SIZE + 1) * TAB_SIZE
        elif char == "\f":
            column = 0
        else:
            column += 1
    return column


def _string_end(text: str, start: int) -> int:
    """The offset just past a string literal that begins at `start`, prefix included."""

    match = _STRING_PREFIX.match(text, start)
    if match is None:
        raise ScriptLexError(f"byte {start}: not a string literal")
    cursor = match.end()
    quote = text[cursor]
    triple = text[cursor:cursor + 3] in ("'''", '"""')
    closing = quote * 3 if triple else quote
    cursor += len(closing)
    while True:
        if cursor >= len(text):
            raise ScriptLexError(
                f"byte {start}: the {'triple-' if triple else ''}quoted string never closes"
            )
        char = text[cursor]
        if char == "\\":
            # A backslash escapes the next character even in a raw literal: `r'\''` is legal
            # Python 2 and the quote it precedes does not end the string.
            cursor += 2
            continue
        if not triple and char in "\r\n":
            raise ScriptLexError(f"byte {start}: a single-quoted string crosses a line end")
        if text.startswith(closing, cursor):
            return cursor + len(closing)
        cursor += 1


def tokenize(text: str) -> list[Token]:
    """The Python 2.1 token stream of one module, every token carrying its byte offset."""

    lines = split_lines(text)
    tokens: list[Token] = []
    indents = [0]
    depth = 0
    continued = False
    in_logical = False
    line_number = 0
    position = 0
    if text.startswith(BOM):
        position = len(BOM)

    while line_number < len(lines):
        line = lines[line_number]
        limit = line.offset + line.length
        if position < line.offset:
            position = line.offset
        if depth == 0 and not continued:
            indent = text[position:position + len(line.indent)] if position == line.offset else ""
            cursor = position + len(indent) if position == line.offset else position
            rest = text[cursor:limit].rstrip("\r\n")
            if not rest or rest.lstrip().startswith("#"):
                # A blank or comment-only line carries no statement, so the indent stack is
                # untouched -- exactly what CPython's tokenizer does with it.
                if rest.lstrip().startswith("#"):
                    comment_start = cursor + (len(rest) - len(rest.lstrip()))
                    comment = text[comment_start:limit].rstrip("\r\n")
                    tokens.append(
                        Token("COMMENT", comment, line.index + 1, comment_start - line.offset,
                              comment_start)
                    )
                terminator = text[limit - _terminator_length(line):limit]
                tokens.append(
                    Token("NL", terminator, line.index + 1, limit - line.offset - len(terminator),
                          limit - len(terminator))
                )
                position = limit
                line_number += 1
                continue
            column = _column(indent)
            if column > indents[-1]:
                indents.append(column)
                tokens.append(Token("INDENT", indent, line.index + 1, 0, line.offset))
            else:
                while column < indents[-1]:
                    indents.pop()
                    tokens.append(Token("DEDENT", "", line.index + 1, len(indent), cursor))
                if column != indents[-1]:
                    raise ScriptLexError(
                        f"line {line.index + 1}: indentation of {column} columns matches no "
                        f"enclosing block"
                    )
            position = cursor
            in_logical = True
        continued = False

        while position < limit:
            char = text[position]
            if char in " \t\f":
                position += 1
                continue
            if char == "\\" and text[position + 1:position + 2] in ("\r", "\n", ""):
                # An explicit line join: the backslash and the newline spell no token.
                continued = True
                position = limit
                break
            if char in "\r\n":
                terminator = text[position:limit]
                kind = "NEWLINE" if depth == 0 and in_logical else "NL"
                tokens.append(
                    Token(kind, terminator, line.index + 1, position - line.offset, position)
                )
                if kind == "NEWLINE":
                    in_logical = False
                position = limit
                break
            if char == "#":
                comment = text[position:limit].rstrip("\r\n")
                tokens.append(
                    Token("COMMENT", comment, line.index + 1, position - line.offset, position)
                )
                position += len(comment)
                continue
            if _STRING_PREFIX.match(text, position):
                end = _string_end(text, position)
                tokens.append(
                    Token("STRING", text[position:end], line.index + 1,
                          position - line.offset, position)
                )
                position = end
                if end > limit:
                    # A triple-quoted literal swallowed the following physical lines.
                    while line_number < len(lines) and lines[line_number].offset + \
                            lines[line_number].length < end:
                        line_number += 1
                    line = lines[line_number]
                    limit = line.offset + line.length
                continue
            number = _NUMBER.match(text, position)
            if number and (char.isdigit() or (char == "." and number.group(0) != ".")):
                tokens.append(
                    Token("NUMBER", number.group(0), line.index + 1,
                          position - line.offset, position)
                )
                position = number.end()
                continue
            name = _NAME.match(text, position)
            if name:
                tokens.append(
                    Token("NAME", name.group(0), line.index + 1, position - line.offset, position)
                )
                position = name.end()
                continue
            for operator in _OPERATORS:
                if text.startswith(operator, position):
                    tokens.append(
                        Token("OP", operator, line.index + 1, position - line.offset, position)
                    )
                    if operator in _OPEN:
                        depth += 1
                    elif operator in _CLOSE:
                        depth = max(0, depth - 1)
                    position += len(operator)
                    break
            else:
                raise ScriptLexError(
                    f"line {line.index + 1}, byte {position}: {text[position]!r} spells no "
                    f"Python 2.1 token"
                )
        else:
            # The physical line ran out with no terminator: only the last line of a file can.
            if position >= limit and line_number == len(lines) - 1 and in_logical:
                tokens.append(Token("NEWLINE", "", line.index + 1, limit - line.offset, limit))
                in_logical = False
        line_number += 1
        position = max(position, limit)

    if depth:
        raise ScriptLexError("the module ends inside an unclosed bracket")
    end = len(text)
    for _ in indents[1:]:
        tokens.append(Token("DEDENT", "", len(lines) + 1, 0, end))
    tokens.append(Token("ENDMARKER", "", len(lines) + 1, 0, end))
    return tokens


def _terminator_length(line: Line) -> int:
    return {"crlf": 2, "lf": 1, "cr": 1, "": 0}[line.terminator]


def string_value(spelling: str) -> str:
    """The value of a Python 2 string literal, escapes resolved, one character per byte.

    Only the escapes 2.1 gives a meaning to are resolved, and a raw literal keeps its
    backslashes, because a reference the seam resolves against the install has to be the path the
    interpreter would have opened.
    """

    match = _STRING_PREFIX.match(spelling, 0)
    prefix = match.group(0).lower() if match else ""
    body = spelling[len(prefix):]
    quote = body[:3] if body[:3] in ("'''", '"""') else body[:1]
    body = body[len(quote):-len(quote)] if len(body) >= 2 * len(quote) else ""
    if "r" in prefix:
        return body
    simple = {
        "n": "\n", "t": "\t", "r": "\r", "\\": "\\", "'": "'", '"': '"',
        "a": "\a", "b": "\b", "f": "\f", "v": "\v", "0": "\0",
    }
    out: list[str] = []
    index = 0
    while index < len(body):
        char = body[index]
        if char != "\\":
            out.append(char)
            index += 1
            continue
        nxt = body[index + 1:index + 2]
        if nxt == "x" and re.match(r"[0-9a-fA-F]{2}", body[index + 2:index + 4] or ""):
            out.append(chr(int(body[index + 2:index + 4], 16)))
            index += 4
        elif nxt and nxt in "01234567":
            digits = re.match(r"[0-7]{1,3}", body[index + 1:]).group(0)
            out.append(chr(int(digits, 8) & 0xFF))
            index += 1 + len(digits)
        elif nxt in ("\n", "\r"):
            index += 2 + (1 if body[index + 1:index + 3] == "\r\n" else 0)
        elif nxt in simple:
            out.append(simple[nxt])
            index += 2
        else:
            out.append(char)
            index += 1
    return "".join(out)
