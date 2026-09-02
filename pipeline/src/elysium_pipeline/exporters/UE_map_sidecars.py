"""The legacy map sidecars, produced from the published V2 map units (R3.2, MP-2.2).

`docs/project/seam_migration.md` -> "Roadmap -- one pipeline" R3.2 asks for one producer that reads
a map's four published GLB units and writes the sidecars the running game already reads --
`.ents`, `.hulls`, `.dispcol`, `.lights`, `.env`, `.sky`, `.spawn`, `.ropes` -- plus the R2.4
export-readiness marker `<map>.ready`. The join it performs is stated in
`docs/architecture/seam_map_map.md` -> "Producer join: the entities+root join behind `.ents`";
this module is that specification executed, and nothing here decides anything the doc does not
already state.

Two rules govern every line below.

**Byte-comparability, not equivalence.** The output is diffed against `UE_bsp_to_scene.py`'s
sidecars by the R3.3 differ, so every legacy quirk is reproduced verbatim by default -- the
`^(On|Out)` output test rather than the datamap typing the entities unit uses, unfolded keys,
`param` left unstripped, `delay` through a plain `float()`, the dropped `extra` field, `times`
normalized to `-1`. Those six are named divergences owned by R3.4. Six landed as an opt-in flag on
`EntityDivergences` (default: legacy/off, so a caller that asks for nothing still gets the
byte-comparable sidecars R3.3 diffs) plus a doc line at the flag; the sixth -- `times`
normalization -- has exactly one owner already (`ElysiumEntityDefs.cpp`, not the exporter) and
needed no flag. Silently changing a default would be the one thing this task must not produce.

**Binary32.** Root positional tables are published in glTF metres and the BSP stores them as
float32, so every recovered plane, vertex and bound is rounded back to binary32 before it is used
(`seam_map_map.md` -> "Which table owns which fact": inverting the transform in binary64 leaves
4,772 of the three test maps' 35,594 plane distances off by up to 9.09e-13 Source inches, which
measurably changes hull vertex sets; in binary32 none of them move).

The producer never writes over the legacy files: it writes under
`$ELYSIUM_EXPORT_V2_ROOT/_sidecars/<map>/`, beside `_census/` from R2.2.

    uv run python -m elysium_pipeline.exporters.UE_map_sidecars sp_tutorial_1 sm_pawnshop_1 sm_hub_1

One known limit, measured not assumed: `.dispcol` is *not* byte-reproducible from the published
units. See `displacement_triangles` for the numbers.
"""

from __future__ import annotations

import argparse
import json
import os
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Sequence

import numpy as np

from elysium_pipeline import paths, shared_corpus
from elysium_pipeline.formats.bsp import (
    INCH_TO_CM,
    source_angles_to_unreal_quat,
    source_dir_to_unreal,
    source_to_unreal,
)
from elysium_pipeline.formats.map_entities_glb import model as entity_model
from elysium_pipeline.formats.map_entities_glb.model import MAP_ENTITIES_EXTENSION
from elysium_pipeline.formats.map_glb.model import MAP_EXTENSION
from elysium_pipeline.formats.map_lighting_glb.model import MAP_LIGHTING_EXTENSION
from elysium_pipeline.formats.unit_contract import read_glb

#: `$ELYSIUM_EXPORT_V2_ROOT/_sidecars/<map>/` -- a separate tree from the legacy export root, so a
#: differ can hold both at once and nothing this module writes can shadow a legacy sidecar.
SIDECAR_DIR_NAME = "_sidecars"

#: The unit contract's scale: one Source inch is 0.0254 glTF metres.
GLTF_SCALE = 0.0254

#: CONTENTS flags that block the player: SOLID|WINDOW|GRATE|MOVEABLE|PLAYERCLIP. Water and pure
#: MONSTERCLIP stay passable. Verbatim from `UE_bsp_to_scene.BLOCK_MASK`.
BLOCK_MASK = 0x1 | 0x2 | 0x8 | 0x4000 | 0x10000

#: The two `tools/` materials VtMB builds real geometry out of; the rest of the namespace is
#: compile-tool surface and never meshes. Verbatim from `UE_bsp_to_scene.DRAWN_TOOL_MATERIALS`.
DRAWN_TOOL_MATERIALS = frozenset({"tools/black", "tools/toolsblack"})

#: The sky-face orientation contract version `<map>.env` publishes.
SKY_CONVENTION = 1

#: `RopeShader` index -> material, from `CRopeKeyframe::KeyValue` (vampire.dll 0x1019f2b0).
ROPE_SHADER = {0: "cable/cable", 1: "cable/rope", 2: "cable/chain"}
#: `Type` -> `m_nSegments`, same function; every other value falls to 2.
ROPE_TYPE_NODES = {0: 10, 1: 4}
ROPE_DEFAULT_NODES = 5
#: The flat shortening `RecomputeSprings` applies, in Source units (client.dll 0x100bf1a1).
ROPE_SLACK_FUDGE = -100
ROPE_MAT_MASKED, ROPE_MAT_TRANSLUCENT, ROPE_MAT_ENVMAP = 1, 2, 4

#: C `atof`: the longest numeric prefix, 0.0 when there is none. Verbatim from the legacy `_ATOF`.
_ATOF = re.compile(r"^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?")

_ACCESSOR_COMPONENT = {5120: "b", 5121: "B", 5122: "h", 5123: "H", 5125: "I", 5126: "f"}
_ACCESSOR_WIDTH = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}


class MapSidecarError(ValueError):
    """A unit the producer needs is missing, or carries something the join cannot read."""


# ---------------------------------------------------------------- pure numeric helpers


def atof(token: str) -> float:
    """C `atof` of one keyvalue token -- the longest numeric prefix, 0.0 when there is none.

    `hw_jewelry_1` ships origins with comma decimals (`"-3496,92"`), which the engine, and
    therefore this producer, read as `-3496`.
    """

    match = _ATOF.match((token or "").strip())
    return float(match.group(0)) if match else 0.0


def write_sidecar_lines(path: Path, lines: Iterable[str]) -> None:
    """Write one line-oriented sidecar the way `UE_bsp_to_scene` wrote it.

    The legacy writers all use a bare `open(path, "w")`, i.e. Python text mode with the platform's
    newline translation, so every sidecar the game reads today carries CRLF on the machine that
    exported it. A byte-comparable producer has to do the same -- forcing LF here shortens
    `sm_pawnshop_1.hulls` by exactly its 1,376 rows.
    """

    with path.open("w", encoding="ascii") as handle:
        for line in lines:
            handle.write(line + "\n")


def source_position(gltf: Sequence[float]) -> tuple[float, float, float]:
    """A published glTF-metre position back in Source inches, **held as binary32**.

    The contract's forward rule is `(x, y, z)_gltf = (x, z, -y)_source * 0.0254`, so the inverse
    is `(x, -z, y)_gltf / 0.0254`. Rounding the quotient to binary32 returns the BSP's own float32
    bit-for-bit; leaving it in binary64 does not (`seam_map_map.md`).
    """

    x, y, z = (float(gltf[0]), float(gltf[1]), float(gltf[2]))
    return (
        float(np.float32(x / GLTF_SCALE)),
        float(np.float32(-z / GLTF_SCALE)),
        float(np.float32(y / GLTF_SCALE)),
    )


def source_direction(gltf: Sequence[float]) -> tuple[float, float, float]:
    """A published glTF direction back in Source axes, binary32. No scale is applied."""

    x, y, z = (float(gltf[0]), float(gltf[1]), float(gltf[2]))
    return (float(np.float32(x)), float(np.float32(-z)), float(np.float32(y)))


def source_planes(plane_rows: Sequence[dict[str, Any]]) -> np.ndarray:
    """`planes[]` as the legacy solver's float32 `(N, 4)` array of `n.x <= d` halfspaces."""

    table = np.zeros((len(plane_rows), 4), dtype=np.float32)
    for index, row in enumerate(plane_rows):
        normal = source_direction(row["normal"])
        table[index, 0:3] = np.float32(normal)
        table[index, 3] = np.float32(float(row["dist"]) / GLTF_SCALE)
    return table


# ---------------------------------------------------------------- the hull solver


def model_brushes(
    nodes: Sequence[dict[str, Any]],
    leafs: Sequence[dict[str, Any]],
    leaf_brushes: Sequence[int],
    headnode: int = 0,
) -> set[int]:
    """Walk a BSP model's node tree collecting its leaf brushes.

    Ported from `UE_bsp_to_scene._model_brushes`. `headnode` 0 is the world model -- world plus
    `func_detail`, and never a separate brush entity's brushes; a brush entity's own
    `models[N].headNode` yields that entity's brushes instead.
    """

    out: set[int] = set()
    stack = [int(headnode)]
    while stack:
        child = stack.pop()
        if child < 0:                                   # child < 0 -> leaf -(child)-1
            leaf = leafs[-child - 1]
            first = int(leaf["firstLeafBrush"])
            count = int(leaf["numLeafBrushes"])
            out.update(int(leaf_brushes[k]) for k in range(first, first + count))
        else:
            first_child, second_child = nodes[child]["children"]
            stack += [int(first_child), int(second_child)]
    return out


def brush_hull(
    planes: np.ndarray, side_rows: Sequence[dict[str, Any]], contents: int
) -> tuple[int, np.ndarray | None]:
    """A brush is the convex intersection of its sides' halfspaces (`n.x <= d`).

    Ported verbatim from `UE_bsp_to_scene._brush_hull`, tolerances included: bevel sides are
    skipped (they are redundant AABB planes), fewer than four survivors means no hull, every plane
    triple whose `|det|` reaches `1e-6` is solved, and a point is kept when `n.x - d <= 0.05` for
    *every* surviving plane. The three numbers are the contract -- they are what the shipped
    collision was built with, and changing one changes hull vertex counts on real maps.

    `planes` must be the float32 table `source_planes` builds; solving in binary64 measurably
    changes the vertex set.
    """

    halfspaces = []
    for side in side_rows:
        if side["bevel"]:
            continue
        index = int(side["plane"])
        halfspaces.append((planes[index, :3], planes[index, 3]))
    if len(halfspaces) < 4:
        return contents, None
    points, count = [], len(halfspaces)
    for a in range(count):
        for b in range(a + 1, count):
            for c in range(b + 1, count):
                matrix = np.array([halfspaces[a][0], halfspaces[b][0], halfspaces[c][0]])
                if abs(np.linalg.det(matrix)) < 1e-6:
                    continue
                point = np.linalg.solve(
                    matrix,
                    np.array([halfspaces[a][1], halfspaces[b][1], halfspaces[c][1]]),
                )
                if all(
                    np.dot(halfspaces[i][0], point) - halfspaces[i][1] <= 0.05
                    for i in range(count)
                ):
                    points.append(point)
    return contents, (np.array(points) if len(points) >= 4 else None)


def hull_vertices(points: np.ndarray) -> list[float]:
    """One brush's accepted points as the flat Unreal-centimetre array a sidecar row carries.

    The dedupe key is `round(., 1)` in **Source** units and the map keeps first-appearance order
    with the last-seen value -- what a Python dict comprehension over the point list does, and what
    a byte-comparable port has to reproduce.
    """

    unique = {(round(p[0], 1), round(p[1], 1), round(p[2], 1)): p for p in points}
    converted = [source_to_unreal(sx, sy, sz) for sx, sy, sz in unique.values()]
    return [round(float(c), 4) for vertex in converted for c in vertex]


# ---------------------------------------------------------------- the entity lump


@dataclass(frozen=True)
class EntityDivergences:
    """Opt-in switches for the `.ents` behaviours where the legacy sidecar and the entities unit's
    own reading disagree (`seam_migration.md` -> R3.4; `seam_map_map.md` -> "Producer join", the
    six-item list). Every flag defaults to the legacy behaviour, so `write_sidecars` stays
    byte-comparable against `UE_bsp_to_scene.py` unless a caller asks for the corrected reading --
    each flag is documented at its own R3.4 commit.
    """

    #: `False` (legacy, default): a key is an output when it matches `^(On|Out)` case-insensitively
    #: and its value holds >= 4 commas. `True`: the class's datamap decides instead
    #: (`entity_model.OUTPUT_KEY`/`NOT_OUTPUT_KEYS`/`OUTPUT_KEYS_BY_CLASS`/`DISABLED_KEY_SUFFIX`),
    #: matching the entities unit's own `outputLike` demotions and promotions
    #: (`seam_map_map_entities.md` -> "Outputs"). Measured zero effect on the three-map corpus:
    #: `game_ui`'s promoted keys and `trigger_player_activity_level`'s demotion are both authored
    #: on maps outside it (`la_hub_1`, `sm_diner_1`).
    datamap_output_typing: bool = False

    #: `False` (legacy, default): a repeated key is the same `keys` slot only when it repeats under
    #: the *exact same spelling*, so `"Origin"` and `"origin"` survive as two independent last-wins
    #: slots (`seam_map_map.md` -> "Producer join": "Keys are **not** folded"). `True`: two spellings
    #: of one key are the same slot -- the entities unit's own identity rule (`decode.py`'s
    #: `occurrences` map, keyed by the already-folded `pair.key`) -- and the slot's value and its
    #: printed spelling both become the *last* occurrence's, in that occurrence's own casing (never
    #: forced lowercase: `seam_map_map.md`'s "authored spelling" rule for `keys` still holds).
    #: Measured zero effect on the three-map corpus: no entity repeats a key under two spellings.
    fold_keys: bool = False

    #: `False` (legacy, default): an output row's `param` field (index 2) is carried verbatim,
    #: whitespace and all -- the one string field `split_output` does not `.strip()`
    #: (`seam_map_map.md` -> "Producer join": "`param` **not** stripped"). `True`: `param` gets the
    #: same strip every other string field already gets. Measured zero effect on the three-map
    #: corpus: no output's `parameter` carries leading or trailing whitespace.
    strip_param: bool = False

    #: `False` (legacy, default): an output row's `delay` field (index 3) is read with a plain
    #: `float()`, `0.0` on any parse failure -- reject-the-whole-token, unlike every positional
    #: keyvalue in `.ents` (`origin`, `hingeaxis`, `floor1..8`), which reads with `atof`, the
    #: engine's own longest-numeric-prefix rule (`seam_map_map.md` -> "Producer join": "`delay` a
    #: plain `float()` with `0.0` on failure"). `True`: `delay` reads with this module's own
    #: `atof()` instead, matching every other number `.ents` carries. Measured zero effect on the
    #: three-map corpus: every authored `delay` is already a plain `float()`-parseable token.
    delay_atof: bool = False

    #: `False` (legacy, default): field 6 (`extra`, everything after `python`) is dropped -- the
    #: legacy split reads exactly six fields and never looks past them (`seam_map_map.md`'s field
    #: list: "field 6 (`extra`) dropped"). `True`: `extra` is added to the row, verbatim and
    #: unjoined-comma-restored (`",".join(fields[6:])`, matching the entities unit's own
    #: `Output.extra`), present only when the value's split actually reached a 7th field. **Not**
    #: zero effect: retail always writes seven comma-separated fields even when the 7th is empty,
    #: so this is the one R3.4 flag whose measured delta is the size of the whole `outputs` list,
    #: not a rare edge case -- see the R3.4 doc line in `seam_map_map.md` for the exact count.
    keep_extra: bool = False


#: The default: every flag legacy, so a caller that asks for nothing gets the byte-comparable
#: sidecars R3.3 diffs against `UE_bsp_to_scene.py`.
LEGACY_ENTITY_FIELDS = EntityDivergences()


def _is_datamap_output(classname: str, source_key: str) -> bool:
    """Mirrors `map_entities_glb.decode._is_output` verbatim: whether the class's datamap types
    `source_key` as an output. Restated here rather than imported because that function is private
    to the entities-unit decode, and this producer's byte-comparable default must not depend on it
    -- only the opt-in `datamap_output_typing` path does."""

    folded = source_key.lower()
    if folded.endswith(entity_model.DISABLED_KEY_SUFFIX):
        return False
    folded_class = (classname or "").strip().lower()
    if (folded_class, folded) in entity_model.NOT_OUTPUT_KEYS:
        return False
    if entity_model.OUTPUT_KEY.match(source_key) is not None:
        return True
    return folded in entity_model.OUTPUT_KEYS_BY_CLASS.get(folded_class, frozenset())


def is_output_key(
    classname: str, key: str, fields: EntityDivergences = LEGACY_ENTITY_FIELDS
) -> bool:
    """Whether one keyvalue's key is tried as an output row at all -- the gate `write_entities`
    applies before `split_output`. `fields.datamap_output_typing` picks which of the two rules in
    `seam_map_map.md` -> "Producer join" decides it."""

    if fields.datamap_output_typing:
        return _is_datamap_output(classname, key)
    return bool(re.match(r"^(On|Out)", key, re.I))


def collect_entity_fields(
    pairs: Sequence[tuple[str, str]], fields: EntityDivergences = LEGACY_ENTITY_FIELDS
) -> tuple[list[dict[str, Any]], dict[str, str]]:
    """One block's pairs split into output rows and the `keys` catch-all, the R3.4-aware read
    `write_entities` performs per entity.

    The `keys` catch-all is always emitted with **authored spelling** (`seam_map_map.md` ->
    "Producer join": "every remaining keyvalue, authored spelling, last-wins") -- what `fold_keys`
    changes is only *which* occurrences are considered the same slot. Legacy (`fold_keys=False`)
    treats `"Origin"` and `"origin"` as two independent slots, each keeping its own last value.
    `fold_keys=True` treats them as one slot -- the entities unit's own identity rule (`decode.py`'s
    `occurrences` map, keyed by the already-folded `pair.key`) -- so the slot's *spelling* becomes
    whichever occurrence was last, in **that** occurrence's own casing, not a forced lowercase: an
    entity that never repeats a key under two spellings is unaffected either way.
    """

    classname_probe = next(
        (v for k, v in reversed(pairs) if k.strip().lower() == "classname"), ""
    )
    outputs: list[dict[str, Any]] = []
    keys: dict[str, str] = {}
    fold_index: dict[str, str] = {}   # folded key -> the spelling currently holding `keys`'s slot
    for key, value in pairs:
        row = split_output(value, fields) if is_output_key(classname_probe, key, fields) else None
        if row:
            row["name"] = key
            outputs.append(row)
            continue
        if fields.fold_keys:
            folded = key.lower()
            previous = fold_index.get(folded)
            if previous is not None and previous != key:
                del keys[previous]
            fold_index[folded] = key
        keys[key] = value                             # last wins for plain keyvalues
    return outputs, keys


def split_output(
    value: str, fields: EntityDivergences = LEGACY_ENTITY_FIELDS
) -> dict[str, Any] | None:
    """`target,input,param,delay,times[,python[,extra]]` -> a `.ents` output row, or `None`.

    Ported verbatim from `UE_bsp_to_scene._split_output` by default, including the R3.4 quirks:
    `param` is **not** stripped, `delay` is a plain `float()` (not `atof`), `times` normalizes an
    authored `0` to `-1`, and field 6 (`extra`) is dropped. A value with fewer than four commas is
    not an output at all and stays a plain keyvalue. `fields.strip_param` opts field 2 into the
    same whitespace strip every other string field already gets; `fields.delay_atof` opts field 3
    into the module's own `atof()` instead of a plain `float()`.
    """

    if value.count(",") < 4:
        return None
    parts = value.split(",")

    def number(text: str, default: float) -> float:
        try:
            return float(text)
        except ValueError:
            return default

    row = {
        "target": parts[0].strip(),
        "input": parts[1].strip(),
        "param": parts[2].strip() if fields.strip_param else parts[2],
        "delay": atof(parts[3]) if fields.delay_atof else number(parts[3], 0.0),
        "times": int(number(parts[4], -1)),
        "python": parts[5].strip() if len(parts) > 5 else "",
    }
    if fields.keep_extra and len(parts) > 6:
        row["extra"] = ",".join(parts[6:])   # verbatim; commas past field 6 are part of it
    return row


def _requote(text: str, quoted: bool) -> str:
    """One decoded token back as the source wrote it.

    The entities unit's lexer unescapes a quoted string -- `\\x` yields `x`, `\\n` yields a
    newline -- so the published `value` is shorter than the bytes the legacy regex scanned.
    `sm_hub_1`'s `logic_auto` authors `setArea(\\"santa_monica\\")` and the legacy `.ents` records
    the value the embedded `\\"` truncates it to, so the escapes have to be put back before the
    regex runs. `entity_lump_text` checks every reconstruction against the unit's own
    `byteLength`, so an escape rule that does not round-trip fails loudly instead of quietly
    producing a different lump.
    """

    if not quoted:
        return text
    escaped = text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")
    return f'"{escaped}"'


def entity_lump_text(entity_rows: Sequence[dict[str, Any]]) -> str:
    """The ENTITIES lump text, rebuilt from the entities unit's ordered `keyValues[]`.

    Every legacy reader of lump 0 is a regex over the raw text -- the pair scan behind `.ents`, the
    `sky_camera` scope, `.env`'s fog blocks, `.spawn`. Reconstructing the text and running those
    regexes verbatim is the only way to stay byte-comparable, because the legacy scan is **not**
    equivalent to a structured read: `sm_hub_1`'s `logic_auto` at block 1611 authors
    `setArea("santa_monica")` inside an output value, and the embedded quotes re-pair the whole
    tail of that block -- the legacy `.ents` loses the entity's `origin` and gains a key spelled
    `),`. A structured producer would silently "fix" that; this one reproduces it, and R3.3 sees
    zero diff instead of an unexplained one.

    The reconstruction is faithful where the regexes can see it: the quote characters (source
    spelling and the unit's `quotedKey`/`quotedValue` flags) and the brace sequence, which is all
    `\\{[^{}]*\\}` and `"([^"]*)"\\s+"([^"]*)"` depend on. Whitespace between tokens is free.
    """

    parts: list[str] = []
    for row in entity_rows:
        parts.append("{\n")
        for keyvalue in row.get("keyValues") or []:
            key = _requote(
                str(keyvalue.get("sourceKey", keyvalue.get("key", ""))),
                bool(keyvalue.get("quotedKey", True)),
            )
            value = _requote(
                str(keyvalue.get("value", "")), bool(keyvalue.get("quotedValue", True))
            )
            length = int(keyvalue.get("byteLength", len(key) + len(value) + 1))
            if len(key) + len(value) + 1 != length:
                raise MapSidecarError(
                    f"entity {row.get('index')} keyvalue {keyvalue.get('sourceKey')!r} re-escapes to "
                    f"{len(key) + len(value) + 1} bytes, and the unit read it from {length}; the "
                    "reconstructed lump would not be the text the legacy regexes ran over"
                )
            parts.append(f"{key} {value}\n")
        parts.append("}\n")
    return "".join(parts)


def parse_entity_blocks(text: str) -> list[list[tuple[str, str]]]:
    """ENTITIES lump text -> `[[(key, value), ...]]`, order and repeats preserved.

    Verbatim from `UE_bsp_to_scene._parse_ent_blocks`; an entity may carry several outputs on the
    same key, so repeats are kept and the last-wins rule is applied by the consumer.
    """

    return [
        re.findall(r'"([^"]*)"\s+"([^"]*)"', block)
        for block in re.findall(r"\{([^{}]*)\}", text, re.S)
    ]


def entity_block_texts(text: str) -> list[str]:
    """The same blocks with their braces, the form the legacy `re.finditer` scans hand around."""

    return [match.group(0) for match in re.finditer(r"\{[^{}]*\}", text)]


def visibility_backing_models(blocks: Sequence[Sequence[tuple[str, str]]]) -> set[int]:
    """Brush-model indexes used only as Source visibility backings.

    `func_areaportalwindow.target` names the black brush Source fades over its background model
    while opening and closing a PVS portal. Unreal owns visibility, so that render-only backing
    does not become a brush mesh. Ported from `UE_bsp_to_scene.source_visibility_backing_models`;
    target matching follows the engine's case-insensitive, first-entity rule.
    """

    folded = [{key.casefold(): value for key, value in pairs} for pairs in blocks]
    by_name: dict[str, dict[str, str]] = {}
    for keys in folded:
        name = keys.get("targetname", "").strip().casefold()
        if name and name not in by_name:
            by_name[name] = keys
    models: set[int] = set()
    for keys in folded:
        if keys.get("classname", "").casefold() != "func_areaportalwindow":
            continue
        backing = by_name.get(keys.get("target", "").strip().casefold())
        if backing is None:
            continue
        match = re.fullmatch(r"\*(\d+)", backing.get("model", "").strip())
        if match is not None:
            models.add(int(match.group(1)))
    return models


# ---------------------------------------------------------------- the published units


@dataclass(frozen=True)
class MapUnits:
    """One map's published units, as far as the sidecars need them."""

    name: str
    root: dict[str, Any]
    entities: dict[str, Any]
    lighting: dict[str, Any]
    document: dict[str, Any]
    binary: bytes

    def accessor(self, index: int) -> np.ndarray:
        """One glTF accessor as its `(count, width)` array, read out of the BIN chunk."""

        accessor = self.document["accessors"][index]
        view = self.document["bufferViews"][accessor["bufferView"]]
        start = int(view.get("byteOffset", 0)) + int(accessor.get("byteOffset", 0))
        width = _ACCESSOR_WIDTH[accessor["type"]]
        dtype = np.dtype(_ACCESSOR_COMPONENT[accessor["componentType"]]).newbyteorder("<")
        flat = np.frombuffer(
            self.binary, dtype=dtype, count=accessor["count"] * width, offset=start
        )
        return flat.reshape(accessor["count"], width)


def unit_paths(map_name: str, root: Path | None = None) -> dict[str, Path]:
    """The four published unit paths for one map. Visibility is listed for completeness: the
    sidecars this producer writes read no lump the visibility unit owns."""

    maps_dir = (root or paths.export_v2_root()) / "maps"
    return {
        "root": maps_dir / f"{map_name}.glb",
        "entities": maps_dir / f"{map_name}.entities.glb",
        "lighting": maps_dir / f"{map_name}.lighting.glb",
        "visibility": maps_dir / f"{map_name}.visibility.glb",
    }


def read_units(map_name: str, root: Path | None = None) -> MapUnits:
    """Load the map root, entities and lighting units a sidecar run needs."""

    files = unit_paths(map_name, root)
    documents: dict[str, tuple[dict[str, Any], bytes]] = {}
    for unit in ("root", "entities", "lighting"):
        path = files[unit]
        if not path.is_file():
            raise MapSidecarError(f"{path} does not exist -- export_v2 the map's units first")
        documents[unit] = read_glb(path)
    extensions = {}
    for unit, key in (
        ("root", MAP_EXTENSION),
        ("entities", MAP_ENTITIES_EXTENSION),
        ("lighting", MAP_LIGHTING_EXTENSION),
    ):
        block = (documents[unit][0].get("extensions") or {}).get(key)
        if not isinstance(block, dict):
            raise MapSidecarError(f"{files[unit]} carries no {key!r} extension")
        extensions[unit] = block
    return MapUnits(
        name=map_name,
        root=extensions["root"],
        entities=extensions["entities"],
        lighting=extensions["lighting"],
        document=documents["root"][0],
        binary=documents["root"][1],
    )


# ---------------------------------------------------------------- the 3D skybox scope


class SkyScope:
    """Which of a map's content is 3D-skybox miniature, and the transform that places it.

    The engine's own membership rule: `Draw3dSkyboxworld` hands the world draw path an area-bit
    vector holding only `m_skybox3d.area`, so the test is
    `area(point_leaf(x)) == area(point_leaf(sky_camera.origin))`, applied to every content class
    alike. Ported from `UE_bsp_to_scene.SkyScope`, reading the root unit's `bsp` tables instead of
    the lumps. Two `sky_camera`s (only `la_malkavian_4`) resolve first-in-lump-order, the engine's
    own `FindEntityByName(NULL, ...)` rule.
    """

    def __init__(self, units: MapUnits, blocks: Sequence[str]) -> None:
        self.ok = False
        self.scale = 16.0
        self.origin_src: tuple[float, float, float] | None = None
        self.area = -1
        self.faces: set[int] = set()
        self._nodes = units.root["bsp"]["nodes"]
        self._planes = source_planes(units.root["planes"])
        self._leafs = units.root["bsp"]["leafs"]
        self._leaf_faces = units.root["bsp"]["leafFaces"]["values"]
        self._model_bbox: dict[int, tuple[float, float, float]] = {}
        for row in units.root["models"]:
            mins = source_position(row["mins"])
            maxs = source_position(row["maxs"])
            self._model_bbox[int(row["index"])] = tuple(
                (mins[k] + maxs[k]) * 0.5 for k in range(3)
            )

        cameras = [block for block in blocks if '"sky_camera"' in block]
        if not cameras:
            return
        origin = re.search(
            r'"origin"\s+"(-?[\d.]+)\s+(-?[\d.]+)\s+(-?[\d.]+)"', cameras[0]
        )
        if not origin:
            return
        self.origin_src = tuple(float(value) for value in origin.groups())
        scale = re.search(r'"scale"\s+"(-?[\d.]+)"', cameras[0])
        if scale:
            # CSkyCamera.scale is FIELD_INTEGER, so a fractional authored value truncates.
            self.scale = float(int(float(scale.group(1)))) or 16.0
        self.area = self.area_of(self.origin_src)
        self.faces = self._area_faces(self.area)
        self.ok = True

    def _point_leaf(self, point: Sequence[float]) -> int:
        node = 0
        while node >= 0:
            row = self._nodes[node]
            plane = int(row["plane"])
            normal = self._planes[plane, :3]
            distance = self._planes[plane, 3]
            side = (
                float(point[0] * normal[0] + point[1] * normal[1] + point[2] * normal[2])
                - float(distance)
            )
            first_child, second_child = row["children"]
            node = int(first_child) if side > 0 else int(second_child)
        return -node - 1

    def _area_faces(self, want_area: int) -> set[int]:
        out: set[int] = set()
        for leaf in self._leafs:
            if int(leaf["area"]) != want_area:
                continue
            first = int(leaf["firstLeafFace"])
            count = int(leaf["numLeafFaces"])
            out.update(int(self._leaf_faces[k]) for k in range(first, first + count))
        return out

    def area_of(self, point: Sequence[float]) -> int:
        leaf = self._point_leaf(point)
        return int(self._leafs[leaf]["area"]) if 0 <= leaf < len(self._leafs) else -1

    def is_sky(self, point: Sequence[float] | None) -> bool:
        return bool(self.ok) and point is not None and self.area_of(point) == self.area

    def entity_point(
        self, origin_src: Sequence[float], model_key: str = ""
    ) -> tuple[float, float, float] | None:
        """The Source-space point an entity classifies by.

        A point entity classifies by its own `origin`; a brush entity by its model's bbox centre
        plus that origin, because vbsp re-centres the brushes of an entity that carries one.
        """

        match = re.match(r"\*(\d+)$", (model_key or "").strip())
        if match:
            centre = self._model_bbox.get(int(match.group(1)))
            return None if centre is None else tuple(
                origin_src[k] + centre[k] for k in range(3)
            )
        return tuple(origin_src)


# ---------------------------------------------------------------- brush meshes and displacements


def _face_models(model_rows: Sequence[dict[str, Any]]) -> dict[int, int]:
    face_model: dict[int, int] = {}
    for row in model_rows:
        first = int(row["firstFace"])
        for face in range(first, first + int(row["numFaces"])):
            face_model[face] = int(row["index"])
    return face_model


def _face_material(units: MapUnits, face: dict[str, Any]) -> str | None:
    """The face's raw TEXDATA material string, lowercased -- or `None` when it names none."""

    texinfos = units.root["texinfos"]
    textures = units.root["textures"]
    tex_info = int(face["texInfo"])
    if not 0 <= tex_info < len(texinfos):
        return None
    tex_data = int(texinfos[tex_info]["texData"])
    if not 0 <= tex_data < len(textures):
        return None
    asset = textures[tex_data].get("asset") or ""
    prefix = "vtmb:material:"
    return asset[len(prefix):] if asset.startswith(prefix) else None


def meshed_faces(units: MapUnits, sky: SkyScope, backings: set[int]) -> dict[str, Any]:
    """Which faces survive the exporter's filters, split by the scene each one lands in.

    `UE_bsp_to_scene` drops a face with fewer than three edges or no texinfo, drops every face of a
    `func_areaportalwindow` backing model, and drops the `tools/` namespace apart from
    `DRAWN_TOOL_MATERIALS`. What is left goes to the world scene, the 3D-sky scene, or that brush
    model's own scene -- which is what decides `brush_mesh` in `.ents` and whether `.sky` is
    written at all.
    """

    faces = units.root["faces"]
    face_model = _face_models(units.root["models"])
    world: list[int] = []
    sky_scene: list[int] = []
    brush: dict[int, list[int]] = {}
    for index, face in enumerate(faces):
        if int(face["numEdges"]) < 3 or int(face["texInfo"]) < 0:
            continue
        model = face_model.get(index, 0)
        if model in backings:
            continue
        material = _face_material(units, face)
        if material is None:
            continue
        base = shared_corpus.base_material(material)
        if base.startswith("tools/") and base not in DRAWN_TOOL_MATERIALS:
            continue
        if model > 0:
            brush.setdefault(model, []).append(index)
        elif index in sky.faces:
            sky_scene.append(index)
        else:
            world.append(index)
    return {"world": world, "sky": sky_scene, "brush": brush}


def displacement_triangles(units: MapUnits, world_faces: Sequence[int]) -> list[list[float]]:
    """The world's sculpted terrain as one triangle per row, Unreal centimetres.

    Convex brushes cannot represent sculpted terrain, so `.dispcol` carries the displacement grids
    as a triangle soup. The grid vertex order (`row * side + column`) and the legacy fan
    (`v00,v10,v11` then `v00,v11,v01`) are reproduced exactly, so row count and row order match the
    legacy sidecar.

    **The values do not, to the last printed digit.** The legacy exporter builds each grid vertex
    in binary64 from the VERTEXES and DISP_VERTS lumps; the root unit publishes neither of those
    numerically -- a displacement face contributes no world-scene vertices (`decode.py` skips it:
    "the displacement mesh states this face's geometry") and `displacements[]` carries no
    `vector`/`dist` rows -- so the only published form of this geometry is the displacement mesh's
    float32 `POSITION` accessor. Recovering it under the binary32 rule leaves it within
    2e-3 Unreal centimetres of the legacy value, which still moves the 4th decimal the sidecar
    prints. Measured 2026-08-31 against the legacy sidecars: row counts and row order match
    exactly (3,584 rows on `sp_tutorial_1`, 288 on `sm_hub_1`), 228 and 100 of those rows are
    byte-identical, 17,908 of 32,256 and 372 of 2,592 printed floats differ, max |delta| 0.0019 cm
    and 0.0006 cm. That is a seam-precision limit, not a choice made here; R3.3 must expect it by
    name; R3.4 decided to accept it rather than publish DISP_VERTS numerically in the root unit
    (`seam_map_map.md` -> "R3.4 -- the two the port surfaced").
    """

    faces = units.root["faces"]
    displacements = units.root["displacements"]
    rows: list[list[float]] = []
    for index in world_faces:
        face = faces[index]
        disp = int(face["dispInfo"])
        if not 0 <= disp < len(displacements):
            continue
        record = displacements[disp]
        if int(face["numEdges"]) != 4 or record.get("mesh") is None:
            continue
        mesh = units.document["meshes"][record["mesh"]]
        points = units.accessor(mesh["primitives"][0]["attributes"]["POSITION"]).astype(np.float64)
        unreal = [
            source_to_unreal(*source_position(point)) for point in points
        ]
        side = (1 << int(record["power"])) + 1
        for row in range(side - 1):
            for column in range(side - 1):
                v00 = row * side + column
                v01 = v00 + 1
                v10 = v00 + side
                v11 = v10 + 1
                rows.append([c for corner in (v00, v10, v11) for c in unreal[corner]])
                rows.append([c for corner in (v00, v11, v01) for c in unreal[corner]])
    return rows


# ---------------------------------------------------------------- the sidecar writers


def write_hulls(units: MapUnits, sky: SkyScope, out_dir: Path) -> dict[str, int]:
    """`<map>.hulls`: one world brush per line as flat Unreal-space verts (cm).

    The miniature's own brushes are dropped -- backdrop the player can never reach, drawn at
    `scale * (v - origin)` while a hull would collide at the raw miniature coordinates. A brush is
    classified by its hull's centroid, the same BSP-area test every other content class uses.
    """

    planes = source_planes(units.root["planes"])
    brushes = units.root["collision"]["brushes"]
    sides = units.root["collision"]["brushSides"]
    world = model_brushes(
        units.root["bsp"]["nodes"],
        units.root["bsp"]["leafs"],
        units.root["bsp"]["leafBrushes"]["values"],
        0,
    )
    written = skipped_sky = 0
    lines: list[str] = []
    for index in sorted(world):
        brush = brushes[index]
        first = int(brush["firstSide"])
        contents, points = brush_hull(
            planes, sides[first:first + int(brush["numSides"])], int(brush["contents"])
        )
        if points is None or not (contents & BLOCK_MASK):
            continue
        if sky.is_sky(tuple(np.asarray(points).mean(axis=0))):
            skipped_sky += 1
            continue
        lines.append(" ".join(f"{c:.4f}" for c in hull_vertices(points)))
        written += 1
    write_sidecar_lines(out_dir / f"{units.name}.hulls", lines)
    return {"brushes": written, "skyBrushes": skipped_sky}


def light_rows(units: MapUnits, sky: SkyScope) -> list[dict[str, Any]]:
    """Every WORLDLIGHTS source as one row, in lump order, in Unreal space -- the one statement
    both `<map>.lights` (`write_lights`) and the V2 map bake's staged `lights[]` table
    (`importers.map_geometry.stage_map`, R5.6) are formatted from, so the two cannot disagree.

    The engine's own load-time fixups from `Mod_LoadWorldlights` are applied here, because the
    lighting unit publishes lump 15 verbatim and the values the game lit with are the fixed ones:
    a type 1/2 with no attenuation gets `quadratic = 1`, a type 2 with `exponent == 0` gets
    `exponent = 1`, and any `radius < 1` becomes no cutoff at all rather than a tiny one.

    A row's `index` is the lump-15 ordinal: the `.lights` line index, the baked actor's
    `elysium.src=<n>` tag and the `UElysiumLightCalibration` row key, one number.
    """

    rows: list[dict[str, Any]] = []
    for index, light in enumerate(units.lighting["worldLights"]):
        light_type = int(light["type"])
        origin = tuple(float(c) for c in light["origin"]["source"])
        ox, oy, oz = source_to_unreal(*origin)
        ux, uy, uz = source_dir_to_unreal(*(float(c) for c in light["normal"]["source"]))
        length = (ux * ux + uy * uy + uz * uz) ** 0.5
        if length > 1e-6:
            ux, uy, uz = ux / length, uy / length, uz / length
        else:
            ux = uy = uz = 0.0
        red, green, blue = (float(c) for c in light["intensity"])
        exponent = float(light["exponent"])
        if light_type == 2 and exponent == 0.0:
            exponent = 1.0
        radius = float(light["radius"]["source"])
        if radius < 1.0:
            radius = 0.0
        # The sun and the skyambient are directionless global terms, never miniature content.
        in_sky = int(light_type not in (3, 5) and sky.is_sky(origin))
        rows.append({
            "index": index,
            "type": light_type,
            "position": [ox, oy, oz],
            "direction": [ux, uy, uz],
            "rgb": [red, green, blue],
            "radiusCm": radius * INCH_TO_CM,
            "stopdot": float(light["stopdot"]),
            "stopdot2": float(light["stopdot2"]),
            "exponent": exponent,
            "style": int(light["style"]),
            "sky": in_sky,
        })
    return rows


def format_light_line(row: dict[str, Any]) -> str:
    """One `.lights` line: `type ox oy oz  dx dy dz  ir ig ib  radius_cm  stopdot stopdot2
    exponent  style  sky`."""

    ox, oy, oz = row["position"]
    ux, uy, uz = row["direction"]
    red, green, blue = row["rgb"]
    return (
        f"{row['type']} {ox:.4f} {oy:.4f} {oz:.4f} "
        f"{ux:.4f} {uy:.4f} {uz:.4f} "
        f"{red:.6f} {green:.6f} {blue:.6f} {row['radiusCm']:.4f} "
        f"{row['stopdot']:.4f} {row['stopdot2']:.4f} {row['exponent']:.3f} "
        f"{row['style']} {row['sky']}"
    )


def write_lights(units: MapUnits, sky: SkyScope, out_dir: Path) -> dict[str, int]:
    """`<map>.lights`: one WORLDLIGHTS source per line (`light_rows`, `format_light_line`)."""

    rows = light_rows(units, sky)
    write_sidecar_lines(out_dir / f"{units.name}.lights", [format_light_line(row) for row in rows])
    return {"lights": len(rows), "skyLights": sum(row["sky"] for row in rows)}


def brush_cull_max_cm(classname: str, keys: dict[str, str]) -> float | None:
    """R6.4 (`seam_map_map.md` -> "Brush fade distances"): the distance beyond which a brush
    entity's baked mesh is not drawn, in Unreal centimetres, or None when the row has none.

    Only `func_lod` carries one -- `DisappearDist`, VtMB's hard client-side draw cutoff
    (`C_Func_LOD::ShouldDraw`), read with C `atof` like every other keyvalue and converted
    exactly as R5.1 converts a FADES prop's `fadeMaxDist`. A zero or negative distance is "never
    culled", not "culled at zero". `func_areaportalwindow`'s `FadeDist` governs the black backing
    brush the exporter omits, so it is deliberately not mapped here (the R7 owner call).
    """

    if classname.lower() != "func_lod":
        return None
    distance = atof(keys.get("DisappearDist", "0"))
    if distance <= 0.0:
        return None
    return round(distance * INCH_TO_CM, 4)


def build_entities(
    units: MapUnits,
    sky: SkyScope,
    blocks: Sequence[Sequence[tuple[str, str]]],
    brush_meshes: dict[int, str],
    fields: EntityDivergences = LEGACY_ENTITY_FIELDS,
) -> tuple[list[dict[str, Any]], dict[str, int]]:
    """The `.ents` entity rows and this run's numbers -- the join, without the file.

    The field list, its emission order and every rounding are `seam_map_map.md` -> "The field list
    `.ents` must reproduce". `entities[]` is one row per lump block in lump order with no drops and
    no reorders: `ElysiumEntityWorldPersistence.cpp` applies saved entity state by index, so the
    ordinal is a save key. `fields` opts into the R3.4 divergences one at a time; the default
    reproduces `UE_bsp_to_scene.py` byte for byte.

    `write_entities` writes these rows to `<map>.ents`; R4.1's `UElysiumMapEntities` stage
    (`importers/map_entities.py`, `seam_map_map_entities.md` -> "Import") lands the same rows as
    cooked content. Both read the join here so neither can drift from the other.
    """

    planes = source_planes(units.root["planes"])
    nodes = units.root["bsp"]["nodes"]
    leafs = units.root["bsp"]["leafs"]
    leaf_brushes = units.root["bsp"]["leafBrushes"]["values"]
    brushes = units.root["collision"]["brushes"]
    sides = units.root["collision"]["brushSides"]
    models = units.root["models"]

    out: list[dict[str, Any]] = []
    brush_count = hull_count = output_count = sky_count = 0
    for pairs in blocks:
        outputs, keys = collect_entity_fields(pairs, fields)
        output_count += len(outputs)
        entity: dict[str, Any] = {
            "classname": keys.pop("classname", ""),
            "targetname": keys.pop("targetname", ""),
        }

        tokens = keys.get("origin", "").split()
        origin_src = [atof(token) for token in tokens] if len(tokens) == 3 else [0.0, 0.0, 0.0]
        entity["origin"] = [round(float(c), 5) for c in source_to_unreal(*origin_src)]

        point = sky.entity_point(origin_src, keys.get("model", ""))
        if sky.is_sky(point):
            entity["sky"] = True
            sky_count += 1

        hinge = keys.get("hingeaxis", "").split()
        if len(hinge) == 3 and len(tokens) == 3:
            delta = np.array(
                source_dir_to_unreal(
                    atof(hinge[0]) - origin_src[0],
                    atof(hinge[1]) - origin_src[1],
                    atof(hinge[2]) - origin_src[2],
                )
            )
            norm = float(np.linalg.norm(delta))
            entity["hinge_axis"] = (
                [round(float(c), 6) for c in (delta / norm)] if norm > 1e-6 else [0.0, 0.0, 1.0]
            )

        model_key = keys.get("model", "")
        if model_key.startswith("*"):
            index = int(model_key[1:])
            # The legacy guard is `mi * 48 + 48 <= len(lump 14)`, i.e. the model row exists.
            if 0 <= index < len(models):
                hulls: list[list[float]] = []
                contents_or = 0
                head = int(models[index]["headNode"])
                for brush_index in sorted(model_brushes(nodes, leafs, leaf_brushes, head)):
                    brush = brushes[brush_index]
                    first = int(brush["firstSide"])
                    contents, points = brush_hull(
                        planes,
                        sides[first:first + int(brush["numSides"])],
                        int(brush["contents"]),
                    )
                    if points is None:
                        continue
                    contents_or |= contents
                    hulls.append(hull_vertices(points))
                entity["model"] = index
                entity["hulls"] = hulls
                entity["contents"] = contents_or
                entity["blocks_player"] = bool(contents_or & BLOCK_MASK)
                if index in brush_meshes:
                    entity["brush_mesh"] = brush_meshes[index]
                    cull = brush_cull_max_cm(entity["classname"], keys)
                    if cull is not None:
                        entity["cull_max_cm"] = cull
                brush_count += 1
                hull_count += len(hulls)

        if entity["classname"].lower() == "func_elevator":
            entity["elevator_floors"] = [
                round(float(source_to_unreal(0.0, 0.0, atof(keys.get("floor%d" % floor, "0")))[2]), 5)
                for floor in range(1, 9)
            ]

        entity["start_hidden"] = keys.get("StartHidden", "0") == "1"
        if outputs:
            entity["outputs"] = outputs
        entity["keys"] = keys
        out.append(entity)

    placed = 0
    for entity in out:
        if entity["classname"].lower().startswith("npc_"):
            continue                                  # the skeletal lane owns those
        model_path = entity["keys"].get("model", "").replace("\\", "/").lower()
        if not model_path.endswith(".mdl"):
            continue
        stem = shared_corpus.static_stem(model_path)
        if not stem:
            continue
        entity["model_mesh"] = stem
        placed += 1
        angles = entity["keys"].get("angles", "").split()
        pitch_yaw_roll = [atof(a) for a in angles] if len(angles) == 3 else [0.0, 0.0, 0.0]
        entity["model_quat"] = [
            round(float(c), 6) for c in source_angles_to_unreal_quat(*pitch_yaw_roll)
        ]

    return out, {
        "entities": len(out),
        "brushEntities": brush_count,
        "hulls": hull_count,
        "outputs": output_count,
        "skyEntities": sky_count,
        "modelMeshes": placed,
    }


def write_entities(
    units: MapUnits,
    sky: SkyScope,
    blocks: Sequence[Sequence[tuple[str, str]]],
    brush_meshes: dict[int, str],
    out_dir: Path,
    fields: EntityDivergences = LEGACY_ENTITY_FIELDS,
) -> dict[str, int]:
    """`<map>.ents`: `build_entities`' rows, written as the one JSON document the runtime reads."""

    out, stats = build_entities(units, sky, blocks, brush_meshes, fields)
    path = out_dir / f"{units.name}.ents"
    with path.open("w", encoding="ascii") as handle:
        json.dump({"map": units.name, "entities": out}, handle, separators=(",", ":"))
    return stats


def _block_number(block: str, key: str, default: float = 0.0) -> float:
    match = re.search(rf'"{key}"\s+"(-?[\d.]+)"', block, re.I)
    return float(match.group(1)) if match else default


def _block_vector(block: str, key: str) -> list[float] | None:
    match = re.search(rf'"{key}"\s+"([^"]+)"', block, re.I)
    if not match:
        return None
    try:
        return [float(value) for value in match.group(1).split()][:3]
    except ValueError:
        return None


def _fog(block: str, distance_scale: float) -> dict[str, Any]:
    colour = _block_vector(block, "fogcolor") or [0.0, 0.0, 0.0]
    return {
        "on": _block_number(block, "fogenable") >= 1.0,
        "rgb": [max(0.0, c) / 255.0 for c in colour],
        "start": _block_number(block, "fogstart") * distance_scale * INCH_TO_CM,
        "end": _block_number(block, "fogend") * distance_scale * INCH_TO_CM,
    }


def write_environment(
    units: MapUnits, sky: SkyScope, blocks: Sequence[str], out_dir: Path, corpus_root: Path
) -> dict[str, Any]:
    """`<map>.env`: the 2D sky name, the face-set flag and the two fog sets.

    Fog is two different things belonging to two different renders: `worldspawn`'s set is the
    world's, `sky_camera`'s is the 3D-skybox pass's own and its distances are in miniature units,
    so a world-space equivalent is `x scale`. The 2D backdrop is fogged by neither -- every sky
    face in the game carries `$nofog 1`.

    `skybox` states whether the shared corpus holds all six faces of this map's sky, which is the
    same question the legacy exporter asked of the same corpus.
    """

    def first_block(classname: str) -> str:
        for block in blocks:
            if f'"{classname}"' in block:
                return block
        return ""

    text = "".join(blocks)
    match = re.search(r'"skyname"\s+"([^"]+)"', text, re.I)
    skyname = match.group(1).lower() if match else None
    sky_ok = False
    if skyname:
        textures = _corpus_textures(corpus_root)
        sky_ok = all(
            shared_corpus.sky_texture_key(skyname, face) in textures
            for face in shared_corpus.SKY_FACES
        )

    world_fog = _fog(first_block("worldspawn"), 1.0)
    sky_fog = _fog(first_block("sky_camera"), sky.scale if sky.ok else 1.0)
    lines = [f"skybox {1 if sky_ok else 0}"]
    if skyname:
        lines.append(f"skyname {skyname}")
    lines.append(f"skyconv {SKY_CONVENTION}")
    for prefix, fog in (("", world_fog), ("sky", sky_fog)):
        lines.append(f"{prefix}fog {1 if fog['on'] else 0}")
        lines.append(
            f"{prefix}fogcolor {fog['rgb'][0]:.4f} {fog['rgb'][1]:.4f} {fog['rgb'][2]:.4f}"
        )
        lines.append(f"{prefix}fogstart {fog['start']:.4f}")
        lines.append(f"{prefix}fogend {fog['end']:.4f}")
    write_sidecar_lines(out_dir / f"{units.name}.env", lines)
    return {"skyname": skyname, "skybox": bool(sky_ok), "worldFog": world_fog["on"]}


def write_sky(units: MapUnits, sky: SkyScope, has_sky_geometry: bool, out_dir: Path) -> bool:
    """`<map>.sky`: the miniature's anchor, so the backdrop places at `(v - origin) * scale`.

    Written only when the map has a `sky_camera` **and** the sky scene carries geometry, matching
    the legacy `sky_anchor is not None and scenes["sky"][0]`.
    """

    if sky.origin_src is None or not has_sky_geometry:
        return False
    ox, oy, oz = source_to_unreal(*sky.origin_src)
    write_sidecar_lines(
        out_dir / f"{units.name}.sky",
        [f"origin {ox:.4f} {oy:.4f} {oz:.4f}", f"scale {sky.scale}"],
    )
    return True


def write_spawn(units: MapUnits, blocks: Sequence[str], out_dir: Path) -> bool:
    """`<map>.spawn`: `info_player_start` origin and yaw, Unreal cm.

    Source angles are "pitch yaw roll"; yaw is the middle value and is negated, because the Y flip
    reverses its sense. The first `info_player_start` block wins, as in the legacy loop.
    """

    for block in blocks:
        if '"info_player_start"' not in block:
            continue
        origin = re.search(r'"origin"\s+"(-?[\d.]+) (-?[\d.]+) (-?[\d.]+)"', block)
        angles = re.search(r'"angles"\s+"(-?[\d.]+) (-?[\d.]+) (-?[\d.]+)"', block)
        if not origin:
            return False
        yaw = float(angles.group(2)) if angles else 0.0
        sx, sy, sz = source_to_unreal(*(float(value) for value in origin.groups()))
        write_sidecar_lines(
            out_dir / f"{units.name}.spawn",
            [f"origin {sx:.4f} {sy:.4f} {sz:.4f}", f"yaw {-yaw}"],
        )
        return True
    return False


def write_ropes(
    units: MapUnits,
    blocks: Sequence[Sequence[tuple[str, str]]],
    out_dir: Path,
    corpus_root: Path,
) -> dict[str, int]:
    """`<map>.ropes`: the overhead cables VtMB strings between poles and buildings.

    A rope is a chain of `move_rope`/`keyframe_rope` nodes linked by `NextKey`; both classnames
    construct the same `CRopeKeyframe`, so the roles are resolved topologically. Every parameter
    here is the RE'd runtime state rather than the raw keyvalue -- the rest length applies `Slack`
    twice, subtracts a flat 100 units and truncates through an integer divide; `nodes` comes from
    `Type`, not `Subdiv`. All of it is ported from `UE_bsp_to_scene.write_ropes`; the material
    files and their shader flags resolve against the same shared corpus the legacy exporter read.
    """

    materials = _corpus_materials(corpus_root)
    cache: dict[str, tuple[str | None, str | None, int]] = {}

    def rope_material(name: str) -> tuple[str | None, str | None, int]:
        if name not in cache:
            albedo = bump = None
            flags = 0
            record = materials.get(shared_corpus.material_key(name))
            if record:
                albedo = os.path.basename(record["albedo"]) or None
                bump = os.path.basename(record["bump"]) or None
                flags = (
                    (ROPE_MAT_MASKED if record["scissor"] else 0)
                    | (ROPE_MAT_TRANSLUCENT if record["blend"] else 0)
                    | (ROPE_MAT_ENVMAP if record["env_cube"] else 0)
                )
            cache[name] = (albedo, bump, flags)
        return cache[name]

    nodes: list[dict[str, str]] = []
    by_name: dict[str, dict[str, str]] = {}
    for pairs in blocks:
        folded = {key.lower(): value for key, value in pairs}
        if folded.get("classname") in ("keyframe_rope", "move_rope"):
            nodes.append(folded)
        name = folded.get("targetname", "").lower()
        if name and name not in by_name:
            by_name[name] = folded                    # engine's FindEntityByName: first wins

    def origin_of(entity: dict[str, str]) -> tuple[float, float, float] | None:
        tokens = entity.get("origin", "0 0 0").split()
        return source_to_unreal(*(atof(c) for c in tokens)) if len(tokens) == 3 else None

    def number(entity: dict[str, str], key: str, default: str) -> float:
        value = entity.get(key, "")
        return atof(value) if value else float(default)

    lines: list[str] = []
    for start in nodes:
        next_key = start.get("nextkey", "")
        if not next_key:
            continue                                  # chain end -> no outgoing segment
        end = by_name.get(next_key.lower())
        if end is None:
            continue                                  # a mapper typo; the engine draws nothing
        a, b = origin_of(start), origin_of(end)
        if a is None or b is None:
            continue
        if sum((x - y) ** 2 for x, y in zip(a, b)) < 1.0:      # < 1 cm apart
            continue
        if "ropeshader" in start:
            material = ROPE_SHADER.get(int(number(start, "ropeshader", "0")), "cable/cable")
        else:
            material = (start.get("ropematerial") or "cable/cable").replace("\\", "/").lower()
        albedo, bump, matflags = rope_material(material)
        width_cm = number(start, "width", "2") * INCH_TO_CM
        node_count = (
            max(2, min(10, ROPE_TYPE_NODES.get(int(number(start, "type", "0")), 2)))
            if "type" in start
            else ROPE_DEFAULT_NODES
        )
        span = sum((x - y) ** 2 for x, y in zip(a, b)) ** 0.5 / INCH_TO_CM
        slack = int(number(start, "slack", "0"))
        rope_length = int(span) + slack
        # C integer division truncates toward zero, so int(x / y), not x // y.
        spring = max(0, int((rope_length + slack + ROPE_SLACK_FUDGE) / (node_count - 1)))
        rest_cm = spring * (node_count - 1) * INCH_TO_CM
        texscale = min(10.0, max(0.1, number(start, "texturescale", "4")))
        flags = (
            (1 if number(start, "dangling", "0") else 0)
            | (2 if number(start, "collide", "0") else 0)
            | (4 if number(start, "barbed", "0") else 0)
            | (8 if number(start, "breakable", "0") else 0)
        )
        lines.append(
            f"{shared_corpus.map_relative(albedo) if albedo else '-'} "
            f"{a[0]:.4f} {a[1]:.4f} {a[2]:.4f} {b[0]:.4f} {b[1]:.4f} {b[2]:.4f} "
            f"{width_cm:.4f} {rest_cm:.4f} {node_count} {texscale:.4f} {flags} "
            f"{shared_corpus.map_relative(bump) if bump else '-'} "
            f"{matflags}"
        )
    if lines:
        write_sidecar_lines(out_dir / f"{units.name}.ropes", lines)
    return {"segments": len(lines), "nodes": len(nodes)}


def write_displacement_collision(
    units: MapUnits, world_faces: Sequence[int], out_dir: Path
) -> int:
    """`<map>.dispcol`: one world displacement triangle per line, nine Unreal-space floats."""

    rows = displacement_triangles(units, world_faces)
    if rows:
        write_sidecar_lines(
            out_dir / f"{units.name}.dispcol",
            (" ".join(f"{c:.4f}" for c in row) for row in rows),
        )
    return len(rows)


# ---------------------------------------------------------------- the shared corpus


_CORPUS_CACHE: dict[Path, tuple[dict[str, Any], dict[str, Any]]] = {}


def _corpus(root: Path) -> tuple[dict[str, Any], dict[str, Any]]:
    """`shared/materials.json` and `shared/manifest.json` -- the same two documents the legacy
    exporter resolved rope materials and sky faces against. They are produced by
    `UE_extract_corpus`, not by the map exporter, so R3.5 does not retire them."""

    root = Path(root)
    if root in _CORPUS_CACHE:
        return _CORPUS_CACHE[root]
    materials_file = shared_corpus.materials_path(str(root))
    manifest_file = shared_corpus.manifest_path(str(root))
    for path in (materials_file, manifest_file):
        if not Path(path).is_file():
            raise MapSidecarError(
                f"no shared corpus at {path}; run: uv run elysium export bundle corpus"
            )
    with open(materials_file, encoding="utf-8") as handle:
        materials = shared_corpus.check_materials(json.load(handle))["materials"]
    with open(manifest_file, encoding="utf-8") as handle:
        manifest = shared_corpus.check_manifest(json.load(handle))
    _CORPUS_CACHE[root] = (materials, manifest["textures"])
    return _CORPUS_CACHE[root]


def _corpus_materials(root: Path) -> dict[str, Any]:
    return _corpus(root)[0]


def _corpus_textures(root: Path) -> dict[str, Any]:
    return _corpus(root)[1]


# ---------------------------------------------------------------- the run


def sidecar_dir(map_name: str, root: Path | None = None) -> Path:
    return (root or paths.export_v2_root()) / SIDECAR_DIR_NAME / map_name


@dataclass(frozen=True)
class MapJoin:
    """One map's published units plus the derived tables every sidecar writer shares.

    The join is stated once here because two callers need it: `write_sidecars` (which writes all
    eight legacy files) and R4.1's entity-asset stage (which needs only `entities`, and must read
    exactly the same join or the asset and the file it replaces could disagree).
    """

    units: MapUnits
    sky: SkyScope
    pair_blocks: list[list[tuple[str, str]]]
    text_blocks: list[str]
    scenes: dict[str, Any]
    brush_meshes: dict[int, str]


def prepare_join(map_name: str, root: Path | None = None) -> MapJoin:
    """Read one map's units and derive the shared tables (`seam_map_map.md` -> "Producer join")."""

    units = read_units(map_name, root)
    lump_text = entity_lump_text(units.entities["entities"])
    pair_blocks = parse_entity_blocks(lump_text)
    text_blocks = entity_block_texts(lump_text)
    sky = SkyScope(units, text_blocks)
    backings = visibility_backing_models(pair_blocks)
    scenes = meshed_faces(units, sky, backings)
    brush_meshes = {index: f"brush_{index}" for index in sorted(scenes["brush"])}
    return MapJoin(units, sky, pair_blocks, text_blocks, scenes, brush_meshes)


def write_sidecars(
    map_name: str,
    *,
    root: Path | None = None,
    out_dir: Path | None = None,
    corpus_root: Path | None = None,
    entity_fields: EntityDivergences = LEGACY_ENTITY_FIELDS,
) -> dict[str, Any]:
    """Produce one map's legacy sidecars from its published units and return the run's numbers.

    The `.ready` marker is written **last** and only when every sidecar Travel depends on is on
    disk, which is exactly what `docs/architecture/map-architecture.md` -> "The export-readiness
    gate" defines it to mean (R2.4). `entity_fields` opts `.ents` into the R3.4 divergences one at
    a time; the default keeps this run byte-comparable to `UE_bsp_to_scene.py`.
    """

    join = prepare_join(map_name, root)
    units = join.units
    out_dir = Path(out_dir) if out_dir is not None else sidecar_dir(map_name, root)
    out_dir.mkdir(parents=True, exist_ok=True)
    corpus_root = Path(corpus_root) if corpus_root is not None else paths.export_root()

    pair_blocks = join.pair_blocks
    text_blocks = join.text_blocks
    sky = join.sky
    scenes = join.scenes
    brush_meshes = join.brush_meshes

    # The marker vouches for the sidecars of *this* run, so a stale one comes off before the run
    # starts: a crash halfway through must leave the directory un-ready, not falsely ready.
    marker = out_dir / f"{map_name}.ready"
    marker.unlink(missing_ok=True)

    report: dict[str, Any] = {"map": map_name, "outputDir": str(out_dir)}
    report["hulls"] = write_hulls(units, sky, out_dir)
    report["ents"] = write_entities(units, sky, pair_blocks, brush_meshes, out_dir, entity_fields)
    report["lights"] = write_lights(units, sky, out_dir)
    report["env"] = write_environment(units, sky, text_blocks, out_dir, corpus_root)
    report["sky"] = write_sky(units, sky, bool(scenes["sky"]), out_dir)
    report["spawn"] = write_spawn(units, text_blocks, out_dir)
    report["ropes"] = write_ropes(units, pair_blocks, out_dir, corpus_root)
    report["dispcol"] = write_displacement_collision(units, scenes["world"], out_dir)
    report["brushMeshes"] = len(brush_meshes)
    report["skyArea"] = sky.area if sky.ok else None

    # R2.4: presence-only, empty, and written only after the sidecars it vouches for exist.
    required = [f"{map_name}{suffix}" for suffix in (".ents", ".hulls", ".lights", ".env")]
    missing = [name for name in required if not (out_dir / name).is_file()]
    if missing:
        raise MapSidecarError(f"{map_name}: sidecars missing after the run: {', '.join(missing)}")
    marker.write_bytes(b"")
    report["ready"] = True
    return report


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="python -m elysium_pipeline.exporters.UE_map_sidecars",
        description="Produce the legacy map sidecars from a map's published V2 units (R3.2).",
    )
    parser.add_argument("maps", nargs="+", help="map stems, e.g. sp_tutorial_1")
    parser.add_argument(
        "--out-root",
        type=Path,
        default=None,
        help="write under this root instead of $ELYSIUM_EXPORT_V2_ROOT/_sidecars",
    )
    parser.add_argument(
        "--corpus-root",
        type=Path,
        default=None,
        help="the export root holding shared/materials.json (default: $ELYSIUM_EXPORT_ROOT)",
    )
    arguments = parser.parse_args(argv)
    for map_name in arguments.maps:
        out_dir = (arguments.out_root / map_name) if arguments.out_root else None
        report = write_sidecars(
            map_name, out_dir=out_dir, corpus_root=arguments.corpus_root
        )
        print(json.dumps(report, separators=(",", ":")))
    return 0


if __name__ == "__main__":                                          # pragma: no cover
    raise SystemExit(main())
