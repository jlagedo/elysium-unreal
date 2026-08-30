"""Nested KeyValues blocks for the four quoted tables this seam cuts.

`game_sounds_surfaceproperties.txt`, `sounds.txt`, `soundscapes.txt` and
`game_sounds_manifest.txt` all nest to more than one level -- a game-sound's `rndwave`, a
soundscape's `playlooping`/`playrandom` each carrying their own `rndwave` -- so this generalizes
`surface_property_glb.lexer.parse`'s one-level block into a tree: an item is either a pair
(`"key" "value"`) or a nested block (`"key" { ... }`), in source order, at any depth.
"""

from __future__ import annotations

from dataclasses import dataclass, field

from elysium_pipeline.formats.sound_script_glb.lexer import SoundScriptLexError, Token, significant


@dataclass(slots=True)
class Pair:
    """One `"key" "value"` leaf, or a valueless `"key"` the engine reads as present-and-empty."""

    key: Token
    value: Token | None = None


@dataclass(slots=True)
class Block:
    """One named brace block: its name token, its braces, and its items in source order."""

    name: Token
    open_token: Token
    close_token: Token | None = None
    items: list[tuple[str, object]] = field(default_factory=list)  # ("pair", Pair) | ("block", Block)
    anomalies: list[dict[str, object]] = field(default_factory=list)

    @property
    def offset(self) -> int:
        return self.name.offset

    @property
    def end(self) -> int:
        return (self.close_token or self.open_token).end

    def pairs(self) -> list[Pair]:
        return [item for kind, item in self.items if kind == "pair"]

    def blocks(self) -> list["Block"]:
        return [item for kind, item in self.items if kind == "block"]


def _parse_block(name: Token, open_token: Token, tokens: list[Token], cursor: int) -> tuple[Block, int]:
    block = Block(name, open_token)
    if name.anomaly:
        block.anomalies.append({"role": name.anomaly, "offset": name.offset, "length": name.length})
    while cursor < len(tokens):
        token = tokens[cursor]
        if token.kind == "close":
            block.close_token = token
            cursor += 1
            return block, cursor
        if token.kind == "open":
            raise SoundScriptLexError(f"byte {token.offset}: a block opens with no name")
        cursor += 1
        following = tokens[cursor] if cursor < len(tokens) else None
        if following is not None and following.kind == "open":
            nested, cursor = _parse_block(token, following, tokens, cursor + 1)
            block.items.append(("block", nested))
            block.anomalies.extend(nested.anomalies)
            continue
        if following is None or following.kind != "string":
            block.anomalies.append({"role": "valueless-key", "offset": token.offset, "key": token.text})
            block.items.append(("pair", Pair(token, None)))
            continue
        pair = Pair(token, following)
        block.items.append(("pair", pair))
        cursor += 1
        for candidate in (pair.key, pair.value):
            if candidate is not None and candidate.anomaly:
                block.anomalies.append(
                    {"role": candidate.anomaly, "offset": candidate.offset, "length": candidate.length}
                )
    block.anomalies.append({"role": "unclosed-block-at-end-of-file", "offset": open_token.offset})
    return block, cursor


def parse(tokens: list[Token]) -> list[Block]:
    """Every top-level named block the token stream declares, in source order."""

    tokens = significant(tokens)
    blocks: list[Block] = []
    cursor = 0
    while cursor < len(tokens):
        name = tokens[cursor]
        if name.kind != "string":
            raise SoundScriptLexError(f"byte {name.offset}: {name.text!r} outside any block")
        if cursor + 1 >= len(tokens) or tokens[cursor + 1].kind != "open":
            raise SoundScriptLexError(f"byte {name.offset}: {name.text!r} opens no block")
        block, cursor = _parse_block(name, tokens[cursor + 1], tokens, cursor + 2)
        blocks.append(block)
    return blocks
