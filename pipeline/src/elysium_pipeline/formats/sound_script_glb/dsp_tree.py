"""Positional brace blocks for `dsp_presets.txt`.

Unlike the four KeyValues tables, a DSP preset carries no key names in its own bytes: a preset is
`{ <id> <configuration> <mix-min> <mix-max> <p1> <p2> <p3> <p4> <processor>... }` and a processor
is `{ <type> <param0> <param1> ... }`, every field positional and every token a bareword. A block
is therefore a flat, ordered sequence of tokens and nested blocks rather than named pairs.
"""

from __future__ import annotations

from dataclasses import dataclass, field

from elysium_pipeline.formats.sound_script_glb.lexer import SoundScriptLexError, Token, significant


@dataclass(slots=True)
class Block:
    open_token: Token
    close_token: Token | None = None
    items: list[object] = field(default_factory=list)  # Token | Block, in source order
    anomalies: list[dict[str, object]] = field(default_factory=list)

    @property
    def offset(self) -> int:
        return self.open_token.offset

    @property
    def end(self) -> int:
        return (self.close_token or self.open_token).end

    def tokens(self) -> list[Token]:
        return [item for item in self.items if isinstance(item, Token)]

    def blocks(self) -> list["Block"]:
        return [item for item in self.items if isinstance(item, Block)]


def _parse_block(open_token: Token, tokens: list[Token], cursor: int) -> tuple[Block, int]:
    block = Block(open_token)
    while cursor < len(tokens):
        token = tokens[cursor]
        if token.kind == "close":
            block.close_token = token
            cursor += 1
            return block, cursor
        if token.kind == "open":
            nested, cursor = _parse_block(token, tokens, cursor + 1)
            block.items.append(nested)
            block.anomalies.extend(nested.anomalies)
            continue
        block.items.append(token)
        cursor += 1
    block.anomalies.append({"role": "unclosed-block-at-end-of-file", "offset": open_token.offset})
    return block, cursor


def parse(tokens: list[Token]) -> tuple[list[Block], list[Token]]:
    """Every top-level `{ ... }` block the token stream declares, in source order.

    `dsp_presets.txt` carries one line between presets 99 and 100 that annotates a column header
    with `#` instead of the file's own `//` (`\t#\ttype\tmix min\t\tdur fade dbmin dbdrop`); a
    stray bareword token at the top level -- outside any brace -- is table-owned annotation like
    that, not a reason to fail the whole table, so it is returned alongside the blocks rather than
    raising.
    """

    tokens = significant(tokens)
    blocks: list[Block] = []
    stray: list[Token] = []
    cursor = 0
    while cursor < len(tokens):
        token = tokens[cursor]
        if token.kind != "open":
            if token.kind == "close":
                raise SoundScriptLexError(f"byte {token.offset}: unmatched closing brace")
            stray.append(token)
            cursor += 1
            continue
        block, cursor = _parse_block(token, tokens, cursor + 1)
        blocks.append(block)
    return blocks, stray
