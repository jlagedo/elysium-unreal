"""The offset-carrying `.lip` lexer and the phoneme document it parses.

A `.lip` is a line-oriented text file: a version line, then brace-delimited sections. The lexer
keeps every line's byte offset so a ledger range and the row it pays for name the same place, and
the parser reads the rows without normalising them -- a word may begin or end with `"` and may
carry the CP-1252 ellipsis `0x85`, and both are part of the word.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Iterable

from elysium_pipeline.formats.sound_glb.model import LipPhoneme, LipPhrase, LipWord

@dataclass(frozen=True, slots=True)
class LipLine:
    """One physical line: where its content starts, how long it is, and its terminator."""

    offset: int
    text: str
    terminator: int

    @property
    def length(self) -> int:
        return len(self.text)

    @property
    def end(self) -> int:
        """One past the line's content, before its terminator."""

        return self.offset + len(self.text)


def decode_text(data: bytes) -> str:
    """`.lip` bytes as text. CP-1252 is single-byte, so one character is one source byte."""

    return data.decode("cp1252", "replace")


def lines_of(text: str) -> tuple[LipLine, ...]:
    """Every line of a `.lip`, with the byte offset the member wrote it at.

    4,848 members use CRLF and 2,288 use LF alone, so both terminators are read and the one the
    line actually carried is what the ledger claims.
    """

    lines: list[LipLine] = []
    position = 0
    while position < len(text):
        newline = text.find("\n", position)
        if newline < 0:
            lines.append(LipLine(position, text[position:], 0))
            return tuple(lines)
        content_end = newline
        if content_end > position and text[content_end - 1] == "\r":
            content_end -= 1
        lines.append(
            LipLine(position, text[position:content_end], newline + 1 - content_end)
        )
        position = newline + 1
    return tuple(lines)


def _quote_parity(text: str) -> int:
    return text.count('"') % 2


def section_extents(lines: Iterable[LipLine]) -> list[tuple[str, int, int, int]]:
    """`(name, header line, opening brace line, closing brace line)` for each top-level section.

    Brace depth is tracked only outside an unterminated `PHRASE` caption, because a caption runs
    across lines and may carry a brace of its own. Only a `PHRASE` row opens one: a `WORD` row's
    text may begin or end with `"` and that quote is part of the word, not a string delimiter.
    """

    rows: list[tuple[str, int, int, int]] = []
    ordered = list(lines)
    index = 0
    in_string = False
    while index < len(ordered):
        stripped = ordered[index].text.strip()
        if not stripped or stripped in ("{", "}"):
            index += 1
            continue
        name = stripped.split()[0]
        header = index
        index += 1
        while index < len(ordered) and not ordered[index].text.strip():
            index += 1
        if index >= len(ordered) or ordered[index].text.strip() != "{":
            rows.append((name, header, -1, -1))
            continue
        opening = index
        depth = 1
        index += 1
        while index < len(ordered) and depth:
            body = ordered[index].text
            if in_string:
                if _quote_parity(body):
                    in_string = False
            else:
                token = body.strip()
                if token == "{":
                    depth += 1
                elif token == "}":
                    depth -= 1
                elif token.startswith("PHRASE") and _quote_parity(body):
                    in_string = True
            index += 1
        rows.append((name, header, opening, index - 1 if not depth else len(ordered) - 1))
    return rows


def _floats(tokens: list[str], count: int) -> list[float]:
    return [float(token) for token in tokens[-count:]]


def parse_words(
    lines: tuple[LipLine, ...], opening: int, closing: int
) -> tuple[list[LipWord], list[dict[str, Any]], list[tuple[int, str]]]:
    """Every `WORD` row and its phonemes, with the per-line ledger owners they produce."""

    words: list[LipWord] = []
    anomalies: list[dict[str, Any]] = []
    owners: list[tuple[int, str]] = []                 # (line index, ledger owner)
    index = opening + 1
    while index < closing:
        line = lines[index]
        stripped = line.text.strip()
        if not stripped:
            index += 1
            continue
        ordinal = len(words)
        owner = f"lip.words[{ordinal}]"
        owners.append((index, owner))
        tokens = stripped.split()
        malformed = not (len(tokens) == 4 and tokens[0] == "WORD")
        if malformed:
            anomalies.append({
                "role": "malformed-word-row",
                "sourceOffset": line.offset,
                "line": line.text,
            })
        if len(tokens) >= 4 and tokens[0] == "WORD":
            text = " ".join(tokens[1:-2])
            start, end = _floats(tokens, 2)
        else:
            text, start, end = stripped, 0.0, 0.0
        phonemes: list[LipPhoneme] = []
        index += 1
        if index < closing and lines[index].text.strip() == "{":
            owners.append((index, owner))
            index += 1
            while index < closing and lines[index].text.strip() != "}":
                row = lines[index]
                fields = row.text.strip().split()
                if len(fields) in (5, 6) and fields[0].lstrip("-").isdigit():
                    phonemes.append(LipPhoneme(
                        code=int(fields[0]),
                        text=fields[1],
                        start=float(fields[2]),
                        end=float(fields[3]),
                        volume=float(fields[4]),
                        flag=int(fields[5]) if len(fields) == 6 else None,
                        source_offset=row.offset,
                    ))
                    owners.append((index, f"{owner}.phonemes[{len(phonemes) - 1}]"))
                elif fields:
                    anomalies.append({
                        "role": "malformed-word-row",
                        "sourceOffset": row.offset,
                        "line": row.text,
                    })
                    owners.append((index, owner))
                index += 1
            if index < closing:
                owners.append((index, owner))
                index += 1
        words.append(LipWord(
            text=text,
            start=start,
            end=end,
            phonemes=tuple(phonemes),
            source_offset=line.offset,
            malformed=malformed,
        ))
    return words, anomalies, owners


def _is_number(token: bytes) -> bool:
    try:
        float(token)
    except ValueError:
        return False
    return True


def _phrase_text(region: bytes, wide: bool) -> str:
    """One caption's bytes as text.

    A `unicode` row writes the region as UTF-16LE while the tokens around it stay ASCII, and the
    corpus quotes it inconsistently -- some rows carry both quote characters inside the counted
    region, some only the closing one, some neither -- so a boundary quote is removed where the
    row wrote one and kept where it did not.
    """

    text = region.decode("utf-16-le" if wide else "cp1252", "replace")
    if text.startswith('"'):
        text = text[1:]
    if text.endswith('"'):
        text = text[:-1]
    return text


def parse_phrases(body: bytes, base_offset: int) -> list[LipPhrase]:
    """`PHRASE <kind> <count> <caption> <start> <end>`.

    `count` is the caption region's byte length, which is what makes the row readable at all: the
    region runs across lines, a `unicode` caption is UTF-16LE, and the quote characters that
    delimit it are inside the count rather than around it. Where the count does not land on the
    two times the row ends with, the closing quote is searched for instead.
    """

    phrases: list[LipPhrase] = []
    position = 0
    while True:
        found = body.find(b"PHRASE ", position)
        if found < 0:
            return phrases
        cursor = found + len(b"PHRASE ")
        while cursor < len(body) and body[cursor:cursor + 1] == b" ":
            cursor += 1
        kind_end = body.find(b" ", cursor)
        if kind_end < 0:
            return phrases
        kind = body[cursor:kind_end].decode("cp1252", "replace")
        cursor = kind_end + 1
        count_end = body.find(b" ", cursor)
        if count_end < 0:
            return phrases
        token = body[cursor:count_end]
        count = int(token) if token.lstrip(b"-").isdigit() else -1
        wide = kind == "unicode"
        start = count_end + 1
        region, tail, after = _phrase_region(body, start, count, wide)
        if region is None:
            position = count_end + 1
            continue
        phrases.append(LipPhrase(
            kind=kind,
            count=count,
            text=_phrase_text(region, wide),
            start=float(tail[0]),
            end=float(tail[1]),
            source_offset=base_offset + found,
        ))
        position = after


def _phrase_region(
    body: bytes, start: int, count: int, wide: bool
) -> tuple[bytes | None, list[bytes], int]:
    """The caption's bytes, the two times behind it, and where the row ends."""

    def times(at: int) -> list[bytes] | None:
        rest = body[at:].split(b"\n", 1)[0].split()
        if len(rest) >= 2 and _is_number(rest[0]) and _is_number(rest[1]):
            return rest[:2]
        return None

    if 0 <= count <= len(body) - start:
        tail = times(start + count)
        if tail is not None:
            return body[start:start + count], tail, start + count
    step = 2 if wide else 1
    cursor = start
    while cursor < len(body):
        index = body.find(b'"', cursor)
        if index < 0:
            return None, [], start
        if wide and (index - start) % 2:
            cursor = index + 1
            continue
        tail = times(index + step)
        if tail is not None:
            return body[start:index + step], tail, index + step
        cursor = index + step
    return None, [], start


def parse_options(lines: tuple[LipLine, ...], opening: int, closing: int) -> dict[str, Any]:
    options: dict[str, Any] = {"voiceDuck": None, "speakerName": None}
    for index in range(opening + 1, closing):
        tokens = lines[index].text.strip().split(None, 1)
        if not tokens:
            continue
        if tokens[0] == "voice_duck":
            value = tokens[1].strip() if len(tokens) > 1 else ""
            options["voiceDuck"] = int(value) if value.lstrip("-").isdigit() else value
        elif tokens[0] == "speaker_name":
            options["speakerName"] = tokens[1].strip() if len(tokens) > 1 else ""
    return options
