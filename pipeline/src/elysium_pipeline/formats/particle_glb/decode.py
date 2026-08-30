"""Decode one particle definition into its complete model plus a gapless byte ledger.

`docs/vtmb/weather.md` -> "The particle-definition format" documents the grammar this reads;
`formats/particles.py` compiles the same files for the outdoor-rain slice's Niagara closure, but
rejects any key outside its own contract. This decode never rejects: a key the vocabulary has not
established keeps `meaning: null` and a `typedUnidentified` row, because the unit is complete when
every key is accounted for, not when every key is understood.
"""

from __future__ import annotations

import re
from typing import Any, Callable

from elysium_pipeline.formats.unit_contract import dependency
from elysium_pipeline.formats.particle_glb import lexer
from elysium_pipeline.formats.particle_glb.coverage import new_ledger
from elysium_pipeline.formats.particle_glb.model import (
    BlockEntry,
    KeyEntry,
    ParsedValue,
    ParticleModel,
    asset_id,
    material_asset_id,
    normalize_material_path,
    normalize_particle_key,
    source_path,
    sprite_asset_id,
    sprite_source_path,
)
from elysium_pipeline.formats.unit_contract.validate import OPAQUE_JSON_MINIMUM

_NUMBER = re.compile(r"[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?")

#: Emitter-lifetime keys directly on the root.
DEFINITION_VOCAB = {
    key: "lifetime and looping"
    for key in ("loop", "precipitation", "fps", "frames", "min_frames", "max_frames")
}

#: Drawing-particle keys directly on the root, one meaning per vocabulary row.
PARTICLE_VOCAB: dict[str, str] = {}
for _keys, _meaning in (
    (("sprite", "movealign", "flat", "sortfront", "lighting"), "sprite binding and orientation"),
    (
        ("x_speed", "y_speed", "z_speed", "parent_speed", "radius_speed", "elevation_speed",
         "theta_speed", "phi_speed"),
        "motion",
    ),
    (("size", "radius", "height", "width", "rotation", "rotate", "depth_offset"),
     "extent and roll"),
    (("color", "red", "green", "blue", "mask"), "colour ramps"),
    (("normal", "refract"), "a second texture used as a refraction card"),
    (("collide", "burst"), "collision behaviour"),
):
    for _key in _keys:
        PARTICLE_VOCAB[_key] = _meaning
del _keys, _meaning, _key

SPAWN_VOCAB = {
    key: "child emission"
    for key in (
        "particle", "rate", "burst", "loop", "radius", "theta", "phi", "friction", "bounce",
        "x", "y", "z", "depth_offset", "timescale", "frames",
    )
}

#: `collide` nests only `spawn`/`decal` sub-blocks; the vocabulary names no scalar key of its own.
COLLIDE_VOCAB: dict[str, str] = {}

DECAL_VOCAB = {"particle": "the decal definition"}


def _split_top_level(text: str, separator: str) -> list[str]:
    """`text.split(separator)`, except a separator inside `( )` does not split.

    Only a ramp's `v(n)` position annotation ever nests parentheses, so this is the one place a
    literal comma is not a keyframe boundary.
    """

    parts: list[str] = []
    depth = 0
    current: list[str] = []
    for char in text:
        if char == "(":
            depth += 1
            current.append(char)
        elif char == ")":
            depth = max(0, depth - 1)
            current.append(char)
        elif char == separator and depth == 0:
            parts.append("".join(current))
            current = []
        else:
            current.append(char)
    parts.append("".join(current))
    return parts


def _number(text: str) -> float | None:
    text = text.strip()
    return float(text) if _NUMBER.fullmatch(text) else None


def _ramp_element(segment: str) -> tuple[Any, float | None]:
    """One ramp keyframe: a plain number, a nested range, or (rare) raw text; plus its `v(n)`
    position when the segment carries one."""

    segment = segment.strip()
    position: float | None = None
    if segment.endswith(")"):
        open_paren = segment.rfind("(")
        if open_paren != -1:
            candidate = _number(segment[open_paren + 1:-1])
            if candidate is not None:
                position = candidate
                segment = segment[:open_paren].strip()
    if "~" in segment:
        bounds = segment.split("~")
        if len(bounds) == 2:
            low, high = _number(bounds[0]), _number(bounds[1])
            if low is not None and high is not None:
                return {"kind": "range", "values": [low, high]}, position
    scalar = _number(segment)
    if scalar is not None:
        return scalar, position
    return segment, position


def parse_value(raw: str) -> ParsedValue:
    """The value grammar: `a~b` is a range, `a,b,...` a ramp, `v(n)` a positioned keyframe, and a
    value with no number at all is text -- `formats/particles.py`'s three forms, offset-free."""

    text = raw.strip()
    if not text or not _NUMBER.search(text):
        return ParsedValue(kind="text", values=[text], positions=[None])
    segments = _split_top_level(text, ",")
    if len(segments) > 1:
        values: list[Any] = []
        positions: list[float | None] = []
        for segment in segments:
            value, position = _ramp_element(segment)
            values.append(value)
            positions.append(position)
        return ParsedValue(kind="ramp", values=values, positions=positions)
    if "~" in text:
        bounds = text.split("~")
        if len(bounds) == 2:
            low, high = _number(bounds[0]), _number(bounds[1])
            if low is not None and high is not None:
                return ParsedValue(kind="range", values=[low, high], positions=[None, None])
    scalar = _number(text)
    if scalar is not None:
        return ParsedValue(kind="scalar", values=[scalar], positions=[None])
    return ParsedValue(kind="text", values=[text], positions=[None])


def _resolve_sprite(raw: str) -> tuple[str, str, str]:
    """`(role, asset, sourcePath)` for a `sprite` value: `materials/...` is a material, anything
    else resolves to `particles/<name>.tga` case-insensitively, exactly as the compiler does."""

    value = raw.strip().strip('"').replace("\\", "/")
    if value.lower().startswith("materials/"):
        return "material", material_asset_id(value), value
    key = normalize_particle_key(value)
    if key.endswith(".tga"):
        key = key[: -len(".tga")]
    return "image", sprite_asset_id(key), sprite_source_path(key)


def decode_particle(
    closure,
    *,
    particle_exists: Callable[[str], bool] | None = None,
    sprite_exists: Callable[[str], bool] | None = None,
    material_exists: Callable[[str], bool] | None = None,
) -> ParticleModel:
    """The complete particle unit for one source closure.

    `particle_exists`, `sprite_exists` and `material_exists` answer whether a named target is
    present in the install; without them every dependency publishes `resolved: false` rather than
    asserting a target this decode never looked for.
    """

    member = closure.member
    ledger = new_ledger(member.path, member.data)

    if not member.data:
        omissions = [{"role": "empty-member", "reason": "zero-byte-source"}]
        return ParticleModel(
            key=closure.key,
            asset_id=closure.asset_id,
            sources=[member.to_json()],
            root="",
            keys=[],
            blocks=[],
            projection={"definition": {}, "particle": {}, "spawns": [], "collide": []},
            role="neither",
            precipitation=False,
            comments=[],
            dependencies=[],
            anomalies=[],
            omissions=omissions,
            byte_ledger=[ledger.finish()],
        )

    text = lexer.decode_text(member.data)
    tokens = lexer.tokenize(text)
    doc = lexer.parse(tokens)

    comments: list[dict[str, Any]] = []
    quote_anomalies: list[dict[str, Any]] = []
    has_insignificant_whitespace = False
    for token in tokens:
        in_unparsed = doc.unparsed_offset is not None and token.offset >= doc.unparsed_offset
        # A token in the trailing unparsed region has its bytes claimed as one span below (the
        # `unparsed[0]` owner); a comment there is still published in `comments[]` -- the spec
        # states that table as "every `//` comment with offset", not just the structured ones --
        # but its bytes are not separately (and doubly) claimed under a `comments[i]` owner.
        if not in_unparsed and token.anomaly == "unterminated-quoted-string":
            # A quote left open runs the string to EOF, so it mis-pairs the key/value that
            # follow it; the token is still claimed as `mapped` below (it is a real key or
            # value span), but the departure itself is named once here.
            quote_anomalies.append({"role": "unterminated-quoted-string", "offset": token.offset})
        if token.kind == "whitespace":
            if not in_unparsed:
                ledger.claim(token.offset, token.length, "omitted-proven", "whitespace")
                has_insignificant_whitespace = True
        elif token.kind == "comment":
            comment_index = len(comments)
            comments.append({"offset": token.offset, "text": token.text})
            if not in_unparsed:
                ledger.claim(token.offset, token.length, "mapped", f"comments[{comment_index}]")
        elif token.kind == "bom":
            # No shipped file carries a BOM; when one would, its bytes are insignificant leading
            # bytes exactly like whitespace, and the byte-ledger owner table names no separate
            # `bom` owner, so it is folded onto the one it already declares.
            if not in_unparsed:
                ledger.claim(token.offset, token.length, "omitted-proven", "whitespace")
                has_insignificant_whitespace = True

    if doc.root is not None:
        ledger.claim(doc.root.offset, doc.root.length, "mapped", "root")

    keys: list[KeyEntry] = []
    blocks: list[BlockEntry | None] = [None] * len(doc.blocks)
    block_key_indexes: list[list[int]] = [[] for _ in doc.blocks]
    typed_unidentified: list[dict[str, Any]] = []
    anomalies: list[dict[str, Any]] = sorted(
        [*doc.anomalies, *quote_anomalies], key=lambda row: row["offset"]
    )

    def vocabulary_for(block_index: int | None) -> tuple[str, dict[str, str]]:
        if block_index is None:
            return "root", {}
        kind = doc.blocks[block_index].kind
        if kind == "spawn":
            return "spawn", SPAWN_VOCAB
        if kind == "decal":
            return "decal", DECAL_VOCAB
        return "collide", COLLIDE_VOCAB

    definition_projection: dict[str, Any] = {}
    particle_projection: dict[str, Any] = {}
    spawn_projection: dict[int, dict[str, Any]] = {b.index: {} for b in doc.blocks if b.kind == "spawn"}
    decal_projection: dict[int, dict[str, Any]] = {b.index: {} for b in doc.blocks if b.kind == "decal"}

    for record in doc.keys:
        index = len(keys)
        folded = record.key.text.strip().lower()
        parsed = parse_value(record.value.text)
        entry = KeyEntry(
            index=index,
            block=record.block,
            key=folded,
            source_key=record.key.text,
            value=record.value.text,
            parsed=parsed,
            offset=record.key.offset,
            length=record.key.length,
            quoted_key=record.key.quoted,
            quoted_value=record.value.quoted,
        )
        keys.append(entry)
        if folded == "":
            # An empty quoted key (`"" "1"`) is still a real key/value pair the lexer paired
            # cleanly; this decode never rejects a malformed file, so the spelling is named as an
            # anomaly rather than the export failing.
            anomalies.append({"role": "empty-key", "offset": record.key.offset})
        ledger.claim(record.key.offset, record.key.length, "mapped", f"keys[{index}].key")
        ledger.claim(record.value.offset, record.value.length, "mapped", f"keys[{index}].value")

        if record.block is not None:
            block_key_indexes[record.block].append(index)

        scope, vocab = vocabulary_for(record.block)
        if record.block is None:
            meaning = DEFINITION_VOCAB.get(folded) or PARTICLE_VOCAB.get(folded)
        else:
            meaning = vocab.get(folded)
        if meaning is None:
            typed_unidentified.append(
                {"key": folded, "sourceKey": record.key.text, "offset": record.key.offset,
                 "block": record.block}
            )
        if record.block is None:
            target = definition_projection if folded in DEFINITION_VOCAB else particle_projection
            target[folded] = {"meaning": meaning, "keyIndex": index}
        elif scope == "spawn":
            spawn_projection[record.block][folded] = {"meaning": meaning, "keyIndex": index}
        elif scope == "decal":
            decal_projection[record.block][folded] = {"meaning": meaning, "keyIndex": index}
        # A `collide` block's own vocabulary is empty (it nests only `spawn`/`decal`); a scalar key
        # found directly inside one has no documented home in `projection.collide`'s shape, so it
        # stays visible through `keys[]` and `typedUnidentified` alone.

    for block in doc.blocks:
        blocks[block.index] = BlockEntry(
            index=block.index,
            kind=block.kind,
            parent=block.parent,
            offset=block.name.offset,
            length=block.end - block.name.offset,
            keys=block_key_indexes[block.index],
        )
        ledger.claim(block.name.offset, block.name.length, "mapped", f"blocks[{block.index}].name")
        ledger.claim(
            block.open_token.offset, block.open_token.length, "mapped",
            f"blocks[{block.index}].braces",
        )
        if block.close_token is not None:
            ledger.claim(
                block.close_token.offset, block.close_token.length, "mapped",
                f"blocks[{block.index}].braces",
            )

    # `doc.root` claims only the name token; the root's own open/close braces are the same "root"
    # owner, recovered here the same way `lexer.parse` consumed them, so the owner covers all
    # three spans rather than the name alone.
    significant = [t for t in tokens if t.kind in ("string", "open", "close")]
    if doc.root is not None and len(significant) > 1 and significant[1].kind == "open":
        open_token = significant[1]
        ledger.claim(open_token.offset, open_token.length, "mapped", "root")
    if doc.root is not None and doc.root_close is not None:
        ledger.claim(doc.root_close.offset, doc.root_close.length, "mapped", "root")

    omissions: list[dict[str, Any]] = []
    if has_insignificant_whitespace:
        omissions.append({
            "role": "keyvalues-insignificant-whitespace",
            "reason": "separator-bytes-carry-no-keyvalues-meaning",
        })
    if doc.unparsed_offset is not None:
        end = len(member.data)
        length = end - doc.unparsed_offset
        if length > 0:
            ledger.claim(doc.unparsed_offset, length, "omitted-proven", "unparsed[0]")
        raw = text[doc.unparsed_offset:end]
        if doc.unparsed_offset == 0 and len(raw) >= OPAQUE_JSON_MINIMUM:
            # The whole member is unparsed (a malformed file with no root at all); publishing it
            # verbatim would embed a full source member end to end in the GLB's JSON, which the
            # shared contract's opaque-source rule forbids. The evidence stays -- `offset`/`length`
            # still name the exact span -- but `raw` is a bounded excerpt, not a source mirror.
            raw = raw[: OPAQUE_JSON_MINIMUM - 1]
        omissions.append({
            "role": "unparsed-region", "offset": doc.unparsed_offset, "length": max(length, 0),
            "raw": raw,
        })

    has_sprite = "sprite" in definition_projection or "sprite" in particle_projection
    has_top_spawn = any(block.parent is None and block.kind == "spawn" for block in doc.blocks)
    if has_sprite and has_top_spawn:
        role = "both"
    elif has_sprite:
        role = "drawing"
    elif has_top_spawn:
        role = "emitter"
    else:
        role = "neither"

    if role == "emitter":
        for entry in keys:
            if entry.block is None and entry.key == "frames" and entry.parsed.kind == "scalar":
                if entry.parsed.values and entry.parsed.values[0] == 0:
                    anomalies.append({"role": "frames-zero-on-wrapper", "offset": entry.offset})

    dependencies: list[dict[str, Any]] = []
    seen_assets: set[str] = set()

    def add_dependency(role_name: str, asset: str, path: str, resolved: bool) -> None:
        if asset in seen_assets:
            return
        seen_assets.add(asset)
        dependencies.append(dependency(role_name, asset, path, resolved))

    def add_normal_or_refract(raw_value: str) -> None:
        """`normal`/`refract` names a second texture: either a `materials/...` path (a material
        dependency, exactly like `sprite`'s own material branch) or a bare name that resolves to
        both a sprite bitmap (`particles/<name>.tga`) and a same-named particle definition
        (`particles/<name>.txt`) -- the shipped `*_normal.txt` rootless bumpscale files are that
        second target, and both may exist for one name."""

        value = raw_value.strip().strip('"').replace("\\", "/")
        if not value:
            return
        if value.lower().startswith("materials/"):
            exists = bool(material_exists(normalize_material_path(value))) \
                if material_exists is not None else False
            add_dependency("material", material_asset_id(value), value, exists)
            return
        key = normalize_particle_key(value)
        if key.endswith(".tga"):
            key = key[: -len(".tga")]
        image_exists = bool(sprite_exists(key)) if sprite_exists is not None else False
        add_dependency("image", sprite_asset_id(key), sprite_source_path(key), image_exists)
        particle_target_exists = (
            bool(particle_exists(key)) if particle_exists is not None else False
        )
        add_dependency("particle", asset_id(key), source_path(key), particle_target_exists)

    def add_decal_material(raw_value: str) -> None:
        """`vdecal_first`/`vdecal_last` name a material path below `materials/` without the
        prefix spelled out (`decals/stains/blooda` for `materials/decals/stains/blooda.vmt`)."""

        value = raw_value.strip().strip('"').replace("\\", "/")
        if not value:
            return
        exists = bool(material_exists(normalize_material_path(value))) \
            if material_exists is not None else False
        source = value if value.lower().startswith("materials/") else f"materials/{value}"
        add_dependency("material", material_asset_id(value), source, exists)

    for entry in keys:
        if entry.block is None and entry.key == "sprite":
            role_name, asset, path = _resolve_sprite(entry.value)
            if role_name == "material":
                exists = bool(material_exists(normalize_material_path(entry.value))) \
                    if material_exists is not None else False
            else:
                sprite_key = path[len("particles/"):-len(".tga")]
                exists = bool(sprite_exists(sprite_key)) if sprite_exists is not None else False
            add_dependency(role_name, asset, path, exists)
        elif entry.block is None and entry.key in ("normal", "refract") and entry.parsed.kind == "text":
            # Every `refract` in the shipped corpus is a numeric ramp (compiler-dead cruft, per
            # the vocabulary table), never a name; a dependency only names a real reference, so a
            # non-text value publishes no dependency row -- it stays an ordinary vocabulary key.
            add_normal_or_refract(entry.value)
        elif entry.key == "particle" and vocabulary_for(entry.block)[0] in ("spawn", "decal"):
            child_key = normalize_particle_key(entry.value)
            if not child_key:
                continue
            asset = asset_id(child_key)
            path = source_path(child_key)
            exists = bool(particle_exists(child_key)) if particle_exists is not None else False
            add_dependency("particle", asset, path, exists)
        elif (
            entry.block is not None
            and vocabulary_for(entry.block)[0] == "decal"
            and entry.key in ("vdecal_first", "vdecal_last")
        ):
            add_decal_material(entry.value)

    projection = {
        "definition": definition_projection,
        "particle": particle_projection,
        "spawns": [
            {"blockIndex": block.index, "keys": spawn_projection.get(block.index, {})}
            for block in doc.blocks
            if block.kind == "spawn" and block.parent is None
        ],
        # A `collide` block may nest more than one `decal` (six shipped units do, e.g.
        # `vomit4`'s two `decal` blocks), so every top-level `collide` and every `spawn`/`decal`
        # it nests is published, not just the first of each -- the meaning this decode already
        # computed for a second decal's keys otherwise has no projection home at all.
        "collide": [
            {
                "blockIndex": collide_block.index,
                "spawns": [
                    {"blockIndex": nested.index, "keys": spawn_projection.get(nested.index, {})}
                    for nested in doc.blocks
                    if nested.parent == collide_block.index and nested.kind == "spawn"
                ],
                "decals": [
                    {"blockIndex": nested.index, "keys": decal_projection.get(nested.index, {})}
                    for nested in doc.blocks
                    if nested.parent == collide_block.index and nested.kind == "decal"
                ],
            }
            for collide_block in doc.blocks
            if collide_block.kind == "collide" and collide_block.parent is None
        ],
    }

    root_text = doc.root.text if doc.root is not None else ""

    precipitation = False
    precipitation_entry = definition_projection.get("precipitation")
    if precipitation_entry is not None:
        parsed = keys[precipitation_entry["keyIndex"]].parsed
        if parsed.kind == "scalar" and parsed.values:
            precipitation = bool(parsed.values[0])
        else:
            precipitation = True

    return ParticleModel(
        key=closure.key,
        asset_id=closure.asset_id,
        sources=[member.to_json()],
        root=root_text,
        keys=keys,
        blocks=blocks,
        projection=projection,
        role=role,
        precipitation=precipitation,
        comments=comments,
        dependencies=dependencies,
        anomalies=anomalies,
        omissions=omissions,
        typed_unidentified=typed_unidentified,
        byte_ledger=[ledger.finish()],
    )
