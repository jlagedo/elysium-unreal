"""The legacy map sidecars, produced from the published V2 map units (R3.2, MP-2.2).

R3.2 asks for one producer that reads
a map's four published GLB units and writes the sidecars the running game already reads --
`.ents`, `.hulls`, `.dispcol`, `.lights`, `.env`, `.sky`, `.spawn`, `.ropes`. The R2.4
export-readiness marker `<map>.ready` is gone with the travel gate that read it (0018 story 21-3);
so is the runtime's `.ropes` read. The join it performs is the entities+root join behind
`.ents`; this module is that specification executed, and nothing here decides anything the
specification does not already state.

Three rules govern every line below.

**The lump is read, not reconstructed** (0018 story 21-7). Every reader here takes the entities
unit's own `keyValues[]` -- `entity_pair_blocks`, and `folded_keys` / `first_of_class` for the
scans that want one entity's scalar keys. The unit's lexer is retail's tokeniser `0x10136ce0`,
escapes and all, so this is what `CEntityMapData::GetNextKey 0x10136ee0` hands `ParseMapData`.
Until 21-7 the lump was rebuilt as text and read back with regexes that had no escape rule, which
lost `sm_hub_1`'s `logic_auto` origin and refused three maps outright.

**Retail decides, one answered question at a time.** The output WAS diffed against
`UE_bsp_to_scene.py`'s sidecars by the R3.3 differ, until 0018 story 21-5 deleted both the differ
and the decoder. R3.4 kept the six places where the two readings disagreed as opt-in flags on
`EntityDivergences`, defaulting to the legacy one, because a byte diff was the only judge
available. 21-7 recovered what retail does for each (`docs/vtmb/entity_io.md`) and settles them
one per commit, each with the rows it changed named; a flag still on `EntityDivergences` is a
question not yet answered. Silently changing one without that evidence is the one thing this
module must not produce.

**Binary32.** Root positional tables are published in glTF metres and the BSP stores them as
float32, so every recovered plane, vertex and bound is rounded back to binary32 before it is used
(inverting the transform in binary64 leaves
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
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Sequence

import numpy as np

from elysium_pipeline import paths, shared_corpus
from elysium_pipeline.formats import contents_signature
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
from elysium_pipeline.formats.material_glb.model import MATERIAL_EXTENSION
from elysium_pipeline.formats.texture_glb.model import TEXTURE_EXTENSION
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
    bit-for-bit; leaving it in binary64 does not.
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
    """What is left of the R3.4 opt-in switches, as 0018 story 21-7 settles them one at a time.

    Each flag was a place the legacy sidecar and the entities unit's own reading disagreed, kept
    at the legacy default so `write_sidecars` stayed byte-comparable with a differ 21-1 deleted.
    21-7 answers each against retail (`docs/vtmb/entity_io.md` -> "The lump tokeniser and what a
    keyvalue actually becomes") and removes it: a flag here is one that has not been answered yet.
    """

    #: `False` (legacy, default): a repeated key is the same `keys` slot only when it repeats under
    #: the *exact same spelling*, so `"Origin"` and `"origin"` survive as two independent last-wins
    #: slots. `True`: two spellings of one key are the same slot -- the entities unit's own
    #: identity rule (`decode.py`'s
    #: `occurrences` map, keyed by the already-folded `pair.key`) -- and the slot's value and its
    #: printed spelling both become the *last* occurrence's, in that occurrence's own casing (never
    #: forced lowercase: the authored-spelling rule for `keys` still holds).
    #: Measured zero effect on the three-map corpus: no entity repeats a key under two spellings.
    fold_keys: bool = False

    #: `False` (legacy, default): an output row's `param` field (index 2) is carried verbatim,
    #: whitespace and all -- the one string field `split_output` does not `.strip()`.
    #: `True`: `param` gets the same strip every other string field already gets. Measured zero
    #: effect on the three-map
    #: corpus: no output's `parameter` carries leading or trailing whitespace.
    strip_param: bool = False

    #: `False` (legacy, default): an output row's `delay` field (index 3) is read with a plain
    #: `float()`, `0.0` on any parse failure -- reject-the-whole-token, unlike every positional
    #: keyvalue in `.ents` (`origin`, `hingeaxis`, `floor1..8`), which reads with `atof`, the
    #: engine's own longest-numeric-prefix rule. `True`: `delay` reads with this module's own
    #: `atof()` instead, matching every other number `.ents` carries. Measured zero effect on the
    #: three-map corpus: every authored `delay` is already a plain `float()`-parseable token.
    delay_atof: bool = False

    #: `False` (legacy, default): field 6 (`extra`, everything after `python`) is dropped -- the
    #: legacy split reads exactly six fields and never looks past them. `True`: `extra` is added
    #: to the row, verbatim and
    #: unjoined-comma-restored (`",".join(fields[6:])`, matching the entities unit's own
    #: `Output.extra`), present only when the value's split actually reached a 7th field. **Not**
    #: zero effect: retail always writes seven comma-separated fields even when the 7th is empty,
    #: so this is the one R3.4 flag whose measured delta is the size of the whole `outputs` list,
    #: not a rare edge case.
    keep_extra: bool = False


#: The default: every flag still here is legacy. 0018 story 21-7 removes them one per commit, with
#: the retail evidence and the rows each one changed.
LEGACY_ENTITY_FIELDS = EntityDivergences()


def is_output_key(classname: str, key: str) -> bool:
    """Whether one keyvalue is an output row -- the gate `write_entities` applies before
    `split_output`.

    **The class's datamap decides, never the key's text** (0018 story 21-7). `0x101a5a80` admits a
    record only when its flags carry `FTYPEDESC_KEY 0x4`, and an output is the type 10 custom
    dispatch at `101a5c3b` -- `CEventsSaveDataOps::vfunc4 0x100cdb20` -> `0x100cd6d0` -> the row
    parser. The key's spelling is not consulted, and both counterexamples ship:
    `CMomentaryRotButton.Position` is an output named neither `On*` nor `Out*`, and
    `CNPC_VGhoulCroucher.on_fire` is a plain `SAVE|KEY` bool that looks like one. The reading this
    replaced was `^(On|Out)` case-insensitively.

    Mirrors `map_entities_glb.decode._is_output` verbatim; restated here rather than imported
    because that function is private to the entities-unit decode.
    """

    folded = key.lower()
    if folded.endswith(entity_model.DISABLED_KEY_SUFFIX):
        return False
    folded_class = (classname or "").strip().lower()
    if (folded_class, folded) in entity_model.NOT_OUTPUT_KEYS:
        return False
    if entity_model.OUTPUT_KEY.match(key) is not None:
        return True
    return folded in entity_model.OUTPUT_KEYS_BY_CLASS.get(folded_class, frozenset())


def collect_entity_fields(
    pairs: Sequence[tuple[str, str]], fields: EntityDivergences = LEGACY_ENTITY_FIELDS
) -> tuple[list[dict[str, Any]], dict[str, str]]:
    """One block's pairs split into output rows and the `keys` catch-all, the R3.4-aware read
    `write_entities` performs per entity.

    The `keys` catch-all is always emitted with **authored spelling** -- every remaining keyvalue,
    authored spelling, last-wins -- what `fold_keys`
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
        row = split_output(value, fields) if is_output_key(classname_probe, key) else None
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


#: The value tests the lump's own readers apply, at the same width they always had. Before 0018
#: story 21-7 each was written as `"key"\s+"(...)"` and run over a block's reconstructed text; the
#: capture groups and the character classes are unchanged, only the subject is now the value.
_NUMERIC = re.compile(r"-?[\d.]+")
_NUMERIC_TRIPLE = re.compile(r"(-?[\d.]+)\s+(-?[\d.]+)\s+(-?[\d.]+)")


def entity_pair_blocks(
    entity_rows: Sequence[dict[str, Any]],
) -> list[list[tuple[str, str]]]:
    """Each entity's keyvalues as `(authored key, value)`, in authored order with repeats kept.

    This is what `CEntityMapData::GetNextKey 0x10136ee0` hands `CBaseEntity::ParseMapData
    0x1009e280`, pair by pair. The entities unit's lexer **is** retail's tokeniser `0x10136ce0`
    (`formats/map_entities_glb/lexer.py`), escapes included, so the unit's `keyValues[]` needs no
    re-derivation: `sourceKey` is the authored spelling and `value` is the token the engine
    actually stores.

    Until 0018 story 21-7 this list was produced by rebuilding the lump as text and re-running the
    legacy regexes over it. Those regexes have no escape rule, so an authored `\\"` ended the token
    early and the remainder of the block re-paired: `sm_hub_1`'s `logic_auto` lost its `origin` and
    gained a key spelled `),`, and three maps could not be reconstructed at all
    (`la_ventruetower_2`, `la_ventruetower_3`, `sp_giovanni_2b`). Nine keyvalues in the whole
    corpus carry an embedded quote and all nine are read correctly here.
    """

    return [
        [
            (str(kv.get("sourceKey", kv.get("key", ""))), str(kv.get("value", "")))
            for kv in (row.get("keyValues") or [])
        ]
        for row in entity_rows
    ]


def folded_keys(pairs: Sequence[tuple[str, str]]) -> dict[str, str]:
    """One block's keyvalues, folded and last-wins -- how the engine resolves a scalar key.

    `CBaseEntity::KeyValue` compares the external name with `__strcmpi` (`101a5b0a`), so matching
    is case-insensitive, and `ParseMapData 0x1009e280` applies the pairs in authored order, so the
    last occurrence is what the live field holds. The key's trailing spaces are already gone in
    retail (`10136f48-10136f5e`); this strips both ends, which is what the producer's own
    `classname` probe has always done.
    """

    out: dict[str, str] = {}
    for key, value in pairs:
        out[key.strip().lower()] = value
    return out


def blocks_of_class(
    pair_blocks: Sequence[Sequence[tuple[str, str]]], classname: str
) -> list[dict[str, str]]:
    """Every block whose `classname` is `classname`, folded, in lump order."""

    wanted = classname.lower()
    out: list[dict[str, str]] = []
    for pairs in pair_blocks:
        keys = folded_keys(pairs)
        if keys.get("classname", "").strip().lower() == wanted:
            out.append(keys)
    return out


def first_of_class(
    pair_blocks: Sequence[Sequence[tuple[str, str]]], classname: str
) -> dict[str, str]:
    """The first block of a classname, folded, or an empty mapping.

    First-in-lump-order is the engine's own `FindEntityByName(NULL, ...)` rule, which is what the
    two-`sky_camera` map (`la_malkavian_4`) resolves by.
    """

    found = blocks_of_class(pair_blocks, classname)
    return found[0] if found else {}


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

    def __init__(
        self, units: MapUnits, pair_blocks: Sequence[Sequence[tuple[str, str]]]
    ) -> None:
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

        camera = first_of_class(pair_blocks, "sky_camera")
        if not camera:
            return
        origin = _NUMERIC_TRIPLE.fullmatch(camera.get("origin", ""))
        if not origin:
            return
        self.origin_src = tuple(float(value) for value in origin.groups())
        scale = _NUMERIC.fullmatch(camera.get("scale", ""))
        if scale:
            # CSkyCamera.scale is FIELD_INTEGER, so a fractional authored value truncates.
            self.scale = float(int(float(scale.group(0)))) or 16.0
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


def meshed_faces(
    units: MapUnits,
    sky: SkyScope,
    backings: set[int],
    compile_water=None,
) -> dict[str, Any]:
    """Which faces survive the exporter's filters, split by the scene each one lands in.

    `UE_bsp_to_scene` drops a face with fewer than three edges or no texinfo, drops every face of a
    `func_areaportalwindow` backing model, and drops the `tools/` namespace apart from
    `DRAWN_TOOL_MATERIALS`. What is left goes to the world scene, the 3D-sky scene, or that brush
    model's own scene -- which is what decides `brush_mesh` in `.ents` and whether `.sky` is
    written at all.

    **`SURF_NODRAW` is honoured too** (R7.1 follow-up), which the name test alone could not do: a
    `%compilenodraw` unit outside the `tools/` namespace escaped it and drew as an opaque
    `tools/toolsinvisible` sheet -- `water/invisible_water`, `sm_pier_1`'s ocean, a volume with
    *no drawn surface* by family resolution. The
    name test stays because the trigger textures leave the flag clear (`UE_bsp_to_scene.py:32-39`);
    the flag test is added because a `water/`-pathed nodraw unit leaves the name clear. Measured
    2026-09-04 over all 108 published root units: the two tests overlap on every `tools/` face and
    the flag alone catches exactly 50 more, all on `sm_pier_1` (41 `water/invisible_water`, 9 of its
    `maps/sm_pier_1/water/invisible_water_depth_33` patch), all world-scene, none displacement --
    so no other map's mesh, `.dispcol` or `.sky` moves.

    **`%compilewater` is exempt from that drop** (R7.4, owner decision 2 -- the named modernization
    "surface on nodraw water"). `compile_water(material key) -> bool` answers "vbsp read
    `%compilewater` off this unit's own VMT, through its `patchBase`" -- it takes the face's own
    TEXDATA key, not the base fold, because a patched water unit
    (`maps/sm_pier_1/water/invisible_water_depth_33`) is staged in its own right and only that
    spelling reaches the key. A face whose unit says yes is
    a water face, and a water face is never dropped for `SURF_NODRAW` however the author flagged it.
    VtMB drew nothing there because a 2004 engine could not draw a live ocean under a painted
    skybox card; Unreal draws the authored plane, which is the whole of `sm_pier_1`'s swimmable
    surface (50 faces: 41 `water/invisible_water` -- 18 down-facing, 23 vertical -- and 9 of the
    `maps/sm_pier_1/water/invisible_water_depth_33` patch, all up-facing). `None` (the default) is
    "nobody can answer that here", which is the legacy behaviour byte for byte: the material lane's
    staged provenance is the only place the key lives, and the `.hulls`/`.ents`/`.dispcol` producer
    reads no material sidecar. Nothing that producer writes moves either way, and nothing the R4.1
    entity asset reads does either: measured over all 108 published root units, no `noDraw` face
    naming a water material sits on a brush model at all, so `scenes["brush"]` -- and therefore
    `brush_meshes` and every `.ents` `brush_mesh` -- is identical with the predicate and without it.
    A map that broke that would give the two callers two brush-mesh sets, which is the one thing to
    re-measure if the exemption ever widens.
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
        # The predicate takes the unit's own key, not the base fold: a patched water unit
        # (`maps/sm_pier_1/water/invisible_water_depth_33`) is staged in its own right and only its
        # own key reaches `%compilewater` through `patchBase`.
        if face.get("noDraw") and not (compile_water is not None and compile_water(material)):
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
    name; R3.4 decided to accept it rather than publish DISP_VERTS numerically in the root unit.

    **The winding is reversed from the legacy fan, deliberately (0018 story 21-2).** The coordinate
    contract pairs the Source-to-Unreal Y reflection with a winding reversal, and the legacy fan
    never applied it here: nothing drew this soup and a Chaos trimesh collides from both sides, so
    nothing could tell. Recast can. Once the terrain stood in the level, the engine's own chain --
    `bFlipNormals` swaps v0/v1 at the cook (`ChaosCooking.cpp:41`), the navigation export feeds
    indices `{2,1,0}` (`RecastNavMeshGenerator.cpp:417`), `Unreal2RecastPoint` reflects, and Recast
    walks a triangle whose normal then has +Y (`Recast.cpp:379`) -- read every displacement floor
    as a ceiling and every ceiling as a floor. Measured on `sp_soc_3`: the mesh stood 5.75-6.5 m
    above six graph nodes, on the cavern's roof, with nothing on the floor under them. Rows stay
    one per legacy row and in legacy order; only the corner order inside a row is reversed.
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
                rows.append([c for corner in (v00, v11, v10) for c in unreal[corner]])
                rows.append([c for corner in (v00, v01, v11) for c in unreal[corner]])
    return rows


# ---------------------------------------------------------------- infodecal projectors

#: `R_DecalShoot` binds a decal to a face whose plane its origin stands within this many Source
#: inches of. Verbatim from `UE_bsp_to_scene`'s decal pass.
DECAL_PLANE_RADIUS = 64.0
#: How far outside a face's winding the projected centre may fall and still bind it (inches).
DECAL_EDGE_TOLERANCE = 1.0
#: `_room_normal` samples BSP leaf solidity this far either side of the projected centre (inches).
DECAL_SOLIDITY_PROBE = 2.0
#: A VMT with no `$decalscale` sizes at 1.0, and so does one authoring a literal 0 -- `vmt._find_f`
#: returns 0.0 and the `or 1.0` in `vmt.parse` swallows it. Reproduced rather than corrected.
DECAL_DEFAULT_SCALE = 1.0

#: The tight vector test the `infodecal`, `info_player_start` and brush-origin readers share,
#: kept exactly as narrow as it was: a single space between components, no exponent and no comma
#: decimal, because a decal whose origin it cannot read is one the legacy exporter silently
#: dropped. Only the text it is applied to changed in 0018 story 21-7: the keyvalue's own value,
#: not the whole block.
_SPACED_TRIPLE = re.compile(r"(-?[\d.]+) (-?[\d.]+) (-?[\d.]+)")
_BRUSH_MODEL_KEY = re.compile(r"\*(\d+)")


class MaterialUnits:
    """The VMT facts a map's placement lanes need, read off the published material units.

    Both consumers here used to read `shared/materials.json` + `shared/manifest.json`, the two
    documents 0018 story 21-4 stops consulting:

      * `decal_projector` wants the albedo's pixel dimensions and `$decalscale`, which is what
        `R_DecalSize` multiplies to size a decal's quad. The corpus stated the dimensions so the
        decal pass did not have to decode the image; the texture unit states them under
        `dimensions`, and the material unit carries every VMT key under `parameters` and names
        its albedo in `textureBindings`.
      * `render_flags` wants the three render semantics the rain-cover filter drops a surface
        for. Each is the same test `vmt.parse` makes, against the unit's own published keys.

    A material with no resolvable albedo, or an albedo with no dimensions, answers `None` from
    `decal_projector`: the legacy pass skipped and counted such a decal rather than guessing a
    size, and so does this.
    """

    def __init__(self, root: Path | None = None) -> None:
        self._root = Path(root) if root is not None else paths.export_v2_root()
        self._cache: dict[str, tuple[int, int, float] | None] = {}
        self._flags: dict[str, dict[str, bool]] = {}
        self._units: dict[str, dict[str, Any] | None] = {}

    def _material_unit(self, key: str) -> dict[str, Any] | None:
        if key not in self._units:
            path = self._root / "materials" / f"{key}.glb"
            block = None
            if path.is_file():
                document, _binary = read_glb(path)
                candidate = (document.get("extensions") or {}).get(MATERIAL_EXTENSION)
                block = candidate if isinstance(candidate, dict) else None
            self._units[key] = block
        return self._units[key]

    def _texture_dimensions(self, key: str) -> tuple[int, int] | None:
        path = self._root / "textures" / f"{key}.glb"
        if not path.is_file():
            return None
        document, _binary = read_glb(path)
        block = (document.get("extensions") or {}).get(TEXTURE_EXTENSION)
        dimensions = (block or {}).get("dimensions") or {}
        width = int(dimensions.get("width") or 0)
        height = int(dimensions.get("height") or 0)
        return (width, height) if width and height else None

    def decal_projector(self, key: str) -> tuple[int, int, float] | None:
        """`(albedo width, albedo height, $decalscale)` for one material key, or `None`."""

        if key in self._cache:
            return self._cache[key]
        self._cache[key] = self._resolve(key)
        return self._cache[key]

    def render_flags(self, key: str) -> dict[str, bool]:
        """`water` / `refract` / `additive` for one material key, by `vmt.parse`'s own tests.

        `water` is the `Water` shader OR `%compilewater` (vbsp reads the compile key, not the
        shader name, which is why four corpus units declare something else); `refract` is the
        `Refract` shader; `additive` is `$additive 1`. A key with no published unit answers all
        three false, which is what a material the corpus could not resolve answered before.
        """

        if key in self._flags:
            return self._flags[key]
        unit = self._material_unit(key) or {}
        shader = str(unit.get("shader") or "").lower()
        parameters = {
            str(row.get("key", "")).lower(): str(row.get("value", "")).strip()
            for row in (unit.get("parameters") or ())
        }
        self._flags[key] = {
            "water": shader == "water" or "%compilewater" in parameters,
            "refract": shader == "refract",
            "additive": parameters.get("$additive") == "1",
        }
        return self._flags[key]

    def _resolve(self, key: str) -> tuple[int, int, float] | None:
        # One level of `patch` indirection, which is all `vmt.parse` follows.
        unit = self._material_unit(key)
        for _step in range(2):
            if unit is None:
                return None
            albedo = _material_albedo(unit)
            scale = _material_decal_scale(unit)
            if albedo is not None:
                size = self._texture_dimensions(albedo)
                return None if size is None else (size[0], size[1], scale)
            base = (unit.get("patchOf") or unit.get("patch") or {})
            base_key = base.get("materialPath") if isinstance(base, dict) else None
            if not base_key:
                return None
            unit = self._material_unit(shared_corpus.material_key(base_key))
        return None


def _material_albedo(unit: dict[str, Any]) -> str | None:
    """The texture key this material's `$basetexture` resolves to, or `None`."""

    for binding in unit.get("textureBindings") or ():
        if str(binding.get("parameter", "")).lower() != "$basetexture":
            continue
        asset = str(binding.get("asset") or "")
        prefix = "vtmb:texture:"
        if asset.startswith(prefix):
            return asset[len(prefix):]
        value = binding.get("value")
        return shared_corpus.texture_key(value) if value else None
    return None


def _material_decal_scale(unit: dict[str, Any]) -> float:
    """`$decalscale`, with `vmt.parse`'s own default and its own zero quirk."""

    for parameter in unit.get("parameters") or ():
        if str(parameter.get("key", "")).lower() != "$decalscale":
            continue
        try:
            value = float(str(parameter.get("value", "")).strip())
        except ValueError:
            return DECAL_DEFAULT_SCALE
        return value or DECAL_DEFAULT_SCALE
    return DECAL_DEFAULT_SCALE


def _brush_model_origins(
    pair_blocks: Sequence[Sequence[tuple[str, str]]],
) -> dict[int, tuple[float, float, float]]:
    """Each brush model's authored world offset, from the entity that carries it.

    vbsp leaves a brush entity's faces at their authored coordinates and hands the entity an
    `origin` the engine adds back; the decal projector search needs the face where the player
    sees it. `UE_bsp_to_scene` built the same table (`model_origin`) with the same two tests.
    """

    out: dict[int, tuple[float, float, float]] = {}
    for pairs in pair_blocks:
        keys = folded_keys(pairs)
        model = _BRUSH_MODEL_KEY.fullmatch(keys.get("model", ""))
        if not model:
            continue
        origin = _SPACED_TRIPLE.fullmatch(keys.get("origin", ""))
        if origin:
            out[int(model.group(1))] = tuple(float(value) for value in origin.groups())
    return out


def _projector_faces(
    units: MapUnits, backings: set[int], offsets: dict[int, tuple[float, float, float]]
) -> tuple[np.ndarray, np.ndarray, list[np.ndarray], list[tuple[Any, ...]]]:
    """Every face a decal may project onto, indexed the way the legacy pass indexed it.

    **The filter is the decoder's, not `meshed_faces`'.** A decal binds any face with three or
    more edges and a texinfo that is not a `func_areaportalwindow` backing and not an undrawn
    `tools/` surface -- with no `SURF_NODRAW` test, and including the 3D-skybox miniature's faces
    and every brush model's. `meshed_faces` honours `SURF_NODRAW` (R7.1), which is right for a
    mesh and wrong here: reproducing the decoder is what keeps this story a transport change.

    **One face class the published unit cannot offer: a displacement.** Its geometry is stated by
    its own displacement mesh and it contributes no vertices to a model mesh, so it carries no
    `firstVertex` span, and its FLAT winding -- which is what the decal pass projects onto, not
    the sculpted surface -- is published nowhere: the root unit carries neither VERTEXES
    numerically nor the DISP_VERTS offsets that would let the flat quad be recovered from the
    displaced grid. R3.4 already declined to publish those for `.dispcol`'s sake. The legacy pass
    read the winding straight out of VERTEXES/EDGES and so could bind a decal to sculpted terrain.

    **Measured, 2026-09-21, over all 92 maps with a legacy `.decals` and 5,037 lines** (the probe
    is `research/tooling/probes/decal_weather_parity.py`): 84 maps are byte-identical, three are
    the lump-reader refusals 21-7 owns (`la_ventruetower_2`, `la_ventruetower_3`,
    `sp_giovanni_2b`), and five differ by 22 rows in total -- `la_library_1` 11, `sm_oceanhouse_2`
    6, `sp_soc_1` 2, `sm_warehouse_1` 1, `la_malkavian_5` 1. **Every one of the 22 is this gap**,
    and every one is an `unbound` row (no projector face), never an `unresolved` one (no material
    size): 21 sit inside a displacement's own bounds, and the 22nd was walked to its face --
    `la_malkavian_5`'s `decals/damage/malkfire3` at Source (946.022, 1582.15, -16) projects onto
    face 2610, `dispInfo 103`, whose flat plane is z = -64 exactly, which is the legacy line's
    own position. **All six maps of 0018 story 21-4 are byte-identical**, so the story's own
    acceptance is unaffected; the five are 21-8's to judge when it reaches them. `decal_rows`
    reports `dispFacesSkipped` per map so the exposure is visible rather than inferred.
    """

    faces = units.root["faces"]
    models = {int(row["index"]): row for row in units.root["models"]}
    nodes = units.document["nodes"]
    planes = source_planes(units.root["planes"])
    texinfos = units.root["texinfos"]
    face_model = _face_models(units.root["models"])

    mesh_of: dict[int, int | None] = {}
    for index, row in models.items():
        node = row.get("node")
        mesh_of[index] = None if node is None else int(nodes[int(node)].get("mesh"))

    positions: dict[tuple[int, int], np.ndarray] = {}

    def primitive_positions(mesh: int, primitive: int) -> np.ndarray:
        key = (mesh, primitive)
        hit = positions.get(key)
        if hit is None:
            attributes = units.document["meshes"][mesh]["primitives"][primitive]["attributes"]
            hit = units.accessor(attributes["POSITION"])
            positions[key] = hit
        return hit

    normals: list[np.ndarray] = []
    distances: list[float] = []
    polygons: list[np.ndarray] = []
    basis: list[tuple[Any, ...]] = []
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
        if face.get("primitive") is None or face.get("firstVertex") is None:
            continue                                   # a displacement face; see the docstring
        mesh = mesh_of.get(model)
        if mesh is None:
            continue
        first = int(face["firstVertex"])
        count = int(face["vertexCount"])
        published = primitive_positions(mesh, int(face["primitive"]))[first:first + count]
        offset = np.asarray(offsets.get(model, (0.0, 0.0, 0.0)), dtype=np.float64)
        points = np.asarray(
            [source_position(point) for point in published], dtype=np.float64
        ) + offset

        plane = int(face["plane"])
        normal = np.asarray(planes[plane, :3], dtype=np.float64)
        distance = float(planes[plane, 3]) + float(np.dot(normal, offset))
        if int(face["side"]):
            normal, distance = -normal, -distance

        edge = points[1] - points[0]
        length = float(np.linalg.norm(edge))
        if length <= 0.0:
            continue
        along = edge / length
        across = np.cross(normal, along)
        polygon = np.asarray(
            [[float(np.dot(p - points[0], along)), float(np.dot(p - points[0], across))]
             for p in points]
        )
        vectors = texinfos[int(face["texInfo"])]["textureVecs"]
        normals.append(normal)
        distances.append(distance)
        polygons.append(polygon)
        basis.append(
            (points[0], along, across, index,
             np.asarray(vectors[0][:3], dtype=np.float64),
             np.asarray(vectors[1][:3], dtype=np.float64))
        )
    return (
        np.asarray(normals) if normals else np.zeros((0, 3)),
        np.asarray(distances) if distances else np.zeros((0,)),
        polygons,
        basis,
    )


def _inplane(polygon: np.ndarray, point: np.ndarray) -> float:
    """Distance from a 2D point to a convex polygon, 0 inside. Verbatim from the legacy `_inplane`,
    seam tolerance included: either winding counts as inside, because a face's authored winding is
    arbitrary and a decal that lands on a shared edge must still bind."""

    edges = np.roll(polygon, -1, axis=0) - polygon
    lengths = np.hypot(edges[:, 0], edges[:, 1])
    lengths[lengths < 1e-9] = 1e-9
    offsets = point - polygon
    cross = (edges[:, 0] * offsets[:, 1] - edges[:, 1] * offsets[:, 0]) / lengths
    if np.all(cross >= -0.5) or np.all(cross <= 0.5):
        return 0.0
    travel = np.clip(
        (offsets[:, 0] * edges[:, 0] + offsets[:, 1] * edges[:, 1]) / (lengths * lengths), 0, 1
    )
    nearest = polygon + edges * travel[:, None]
    return float(np.min(np.hypot(*(point - nearest).T)))


def decal_rows(
    join: "MapJoin", *, materials: MaterialUnits | None = None, root: Path | None = None
) -> dict[str, Any]:
    """VtMB's decal layer as one projector row per placed `infodecal`, in entity-lump order.

    VtMB leaves the OVERLAYS lump empty: every poster, stain, sign and spray is an `infodecal`
    entity carrying a `texture` and an `origin`, and the engine projects it onto the surfaces
    within its radius (`engine.dll R_DecalShoot -> R_DecalNode -> R_DecalCreate`). This recovers
    each decal's projector -- the visible face its origin projects squarely onto (nearest by
    plane distance), the room-facing normal, the face's texture axes and the half-extents
    (`R_DecalSize`: the albedo's pixel dimensions x `$decalscale`) -- and states it in Unreal
    space, which is what the bake stands one `ADecalActor` per row from.

    Ported from `UE_bsp_to_scene.py`'s decal pass for 0018 story 21-4, which retired the legacy
    `<map>.decals` sidecar and the BSP decode behind it. The port matches the decoder rather than
    improving on it; the two places it cannot are `_projector_faces`' displacement note and the
    entity lump 21-7 owns.

    **Row order is the decal sort order.** Two decals on one wall layer in the order the entity
    lump names them, so the rows are never sorted or de-duplicated.
    """

    units = join.units
    materials = materials if materials is not None else MaterialUnits(root)
    offsets = _brush_model_origins(join.pair_blocks)
    backings = visibility_backing_models(join.pair_blocks)
    normals, distances, polygons, basis = _projector_faces(units, backings, offsets)

    disp_skipped = sum(
        1 for face in units.root["faces"]
        if int(face["numEdges"]) >= 3 and int(face["texInfo"]) >= 0
        and face.get("primitive") is None and int(face.get("dispInfo", -1)) >= 0
    )

    rows: list[dict[str, Any]] = []
    unresolved = 0
    unbound = 0
    keys: set[str] = set()
    for decal in blocks_of_class(join.pair_blocks, "infodecal"):
        origin = _SPACED_TRIPLE.fullmatch(decal.get("origin", ""))
        texture = decal.get("texture", "")
        if not (origin and texture):
            continue
        key = shared_corpus.base_material(texture)
        resolved = materials.decal_projector(key)
        if resolved is None or len(normals) == 0:
            unresolved += 1
            continue
        width, height, scale = resolved
        point = np.asarray([float(value) for value in origin.groups()], dtype=np.float64)
        gaps = np.abs(normals @ point - distances)
        best: tuple[float, int] | None = None
        for candidate in np.where(gaps <= DECAL_PLANE_RADIUS)[0]:
            anchor, along, across, _face, _sax, _tax = basis[candidate]
            local = np.asarray(
                [float(np.dot(point - anchor, along)), float(np.dot(point - anchor, across))]
            )
            if _inplane(polygons[candidate], local) <= DECAL_EDGE_TOLERANCE and (
                best is None or gaps[candidate] < best[0]
            ):
                best = (float(gaps[candidate]), int(candidate))
        if best is None:
            unbound += 1
            continue

        chosen = best[1]
        anchor, _along, _across, face_index, s_axis, t_axis = basis[chosen]
        normal = normals[chosen]
        projected = point - normal * (float(np.dot(normal, point)) - float(distances[chosen]))
        normal = _room_normal(join, projected, normal, point)
        # The decal's frame is the face's texture axes made perpendicular to the normal. The
        # texinfo s/t sign is authored per face, so it is normalised: t (V) stays as authored,
        # which keeps text upright, and s (U) is signed so the frame is left-handed with respect
        # to the outward normal -- what reads un-mirrored from the room side. The test runs in
        # SOURCE space, before the reflection: `source_dir_to_unreal` negates Y, so the same
        # comparison after the transform would choose the opposite sign.
        s_dir = s_axis - normal * float(np.dot(normal, s_axis))
        s_dir = s_dir / np.linalg.norm(s_dir)
        t_dir = t_axis - normal * float(np.dot(normal, t_axis))
        t_dir = t_dir / np.linalg.norm(t_dir)
        if float(np.dot(np.cross(s_dir, t_dir), normal)) > 0:
            s_dir = -s_dir

        keys.add(key)
        rows.append(
            {
                "index": len(rows),
                "materialId": "vtmb:material:" + shared_corpus.material_key(key),
                "face": int(face_index),
                "locCm": list(source_to_unreal(*projected)),
                "normal": list(source_dir_to_unreal(*normal)),
                "sDir": list(source_dir_to_unreal(*s_dir)),
                "tDir": list(source_dir_to_unreal(*t_dir)),
                "halfWCm": width * scale / 2.0 * INCH_TO_CM,
                "halfHCm": height * scale / 2.0 * INCH_TO_CM,
            }
        )
    return {
        "rows": rows,
        "placed": len(rows),
        # The legacy pass counted both of these as one `n_decal_miss`. They are split because they
        # mean opposite things: a material the units cannot size is authored dirt the decoder
        # skipped too, while a decal that binds no face is the one way this port can lose a row
        # the decoder placed -- see `_projector_faces` on displacement faces.
        "unresolved": unresolved,
        "unbound": unbound,
        "unmatched": unresolved + unbound,
        "materials": len(keys),
        "projectorFaces": len(basis),
        "dispFacesSkipped": disp_skipped,
    }


def _room_normal(
    join: "MapJoin", projected: np.ndarray, normal: np.ndarray, origin: np.ndarray
) -> np.ndarray:
    """Turn a face's normal toward the open side, where the decal is meant to be seen.

    A decal's host face is drawn double-sided (world `CullMode` disabled), so VtMB authors the
    wall with arbitrary winding and the plane normal may point into the sealed interior. The
    engine resolves the room side from BSP leaf solidity; so does this. Ambiguous -- both sides
    solid, or both open -- falls back to the side the entity's own origin stands on, then to the
    authored normal. Verbatim from `UE_bsp_to_scene._room_normal`.
    """

    leafs = join.units.root["bsp"]["leafs"]

    def solid(point: np.ndarray) -> bool:
        leaf = join.sky._point_leaf(point)
        return bool(int(leafs[leaf]["contents"]) & 1) if 0 <= leaf < len(leafs) else False

    plus = solid(projected + normal * DECAL_SOLIDITY_PROBE)
    minus = solid(projected - normal * DECAL_SOLIDITY_PROBE)
    if plus != minus:
        return normal if minus else -normal
    side = float(np.dot(origin - projected, normal))
    return -normal if side < -1e-3 else normal


def decal_line(row: dict[str, Any]) -> str:
    """One `decal_rows` row as its 15-token `.decals` line, the only place the format is written.

    The line names the bare material key, which is the `materialId` less its `vtmb:material:`
    prefix -- `shared_corpus.material_key` is the identity on every decal key the install ships
    (measured over all 5,095 legacy `.decals` lines, 2026-09-21), so the row needs one spelling.
    """

    location, normal = row["locCm"], row["normal"]
    s_dir, t_dir = row["sDir"], row["tDir"]
    return (
        f"{row['materialId'][len('vtmb:material:'):]} "
        f"{location[0]:.4f} {location[1]:.4f} {location[2]:.4f} "
        f"{normal[0]:.6f} {normal[1]:.6f} {normal[2]:.6f} "
        f"{s_dir[0]:.6f} {s_dir[1]:.6f} {s_dir[2]:.6f} "
        f"{t_dir[0]:.6f} {t_dir[1]:.6f} {t_dir[2]:.6f} "
        f"{row['halfWCm']:.4f} {row['halfHCm']:.4f}"
    )


# ---------------------------------------------------------------- the sidecar writers


def write_hulls(units: MapUnits, sky: SkyScope, out_dir: Path) -> dict[str, int]:
    """`<map>.hulls`: one world brush per line, its CONTENTS word then flat Unreal-space verts (cm).

    The miniature's own brushes are dropped -- backdrop the player can never reach, drawn at
    `scale * (v - origin)` while a hull would collide at the raw miniature coordinates. A brush is
    classified by its hull's centroid, the same BSP-area test every other content class uses.

    Every brush answering ANY retail mask is written, not just the player-solid ones, and each row
    leads with `0x%08x` so the reader can partition by signature (0018 story 3, job 1). Retail asks
    four questions of a brush and the old `BLOCK_MASK` filter could only keep the answer to one, so
    the NPC-only clips, the sight-only brushes and the pedestrian volumes never left the BSP at
    all -- on `sp_tutorial_1`, 5 brushes an NPC cannot pass and 17 that stop its sight.

    The leading token makes every row an odd token count where the old format was a multiple of
    three, so a reader built for the old format fails loudly instead of reading a contents word as
    a coordinate. The retired sidecar differ carried the divergence: strip column 0, keep the
    rows answering `BLOCK_MASK`, and the bytes are the legacy exporter's again.
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
    by_signature: Counter[str] = Counter()
    lines: list[str] = []
    for index in sorted(world):
        brush = brushes[index]
        first = int(brush["firstSide"])
        contents, points = brush_hull(
            planes, sides[first:first + int(brush["numSides"])], int(brush["contents"])
        )
        if points is None:
            continue
        signature = contents_signature.signature_of(contents)
        if not signature:
            continue
        if sky.is_sky(tuple(np.asarray(points).mean(axis=0))):
            skipped_sky += 1
            continue
        verts = " ".join(f"{c:.4f}" for c in hull_vertices(points))
        lines.append(f"0x{contents & 0xFFFFFFFF:08x} {verts}")
        by_signature[contents_signature.spell(signature)] += 1
        written += 1
    write_sidecar_lines(out_dir / f"{units.name}.hulls", lines)
    return {
        "brushes": written,
        "skyBrushes": skipped_sky,
        "signatures": dict(by_signature.most_common()),
    }


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
    """R6.4: the distance beyond which a brush
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

    The field list, its emission order and every rounding are what `.ents` must
    reproduce. `entities[]` is one row per lump block in lump order with no drops and
    no reorders: `ElysiumEntityWorldPersistence.cpp` applies saved entity state by index, so the
    ordinal is a save key. `fields` opts into the R3.4 divergences one at a time; the default
    reproduces `UE_bsp_to_scene.py` byte for byte **except** for one unconditional ruling: a
    3D-skybox brush entity carries no hulls (R7.4 / G25, at the model branch below). That one is not
    a flag because it is not a disagreement about how to read the lump -- both readings agree on the
    vertices, and the ruling is that a miniature has no collider in the play volume at all.
    the retired sidecar differ reported it as a real `.ents` delta on every map with a sky brush entity,
    which is what that report is for.

    `write_entities` writes these rows to `<map>.ents`; R4.1's `UElysiumMapEntities` stage
    (`importers/map_entities.py`) lands the same rows as
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
                # G25: a 3D-skybox brush entity contributes no collision. Its hulls are authored in
                # miniature units and the runtime scales them by the sky scale to place the visual
                # (`UElysiumMapEntities::Deserialize`), which on `sm_pier_1` walks `brush_8/9/10`
                # -- sky-flagged `func_brush` Solids that lived at raw z ~= 4939, above the map's
                # own `world_maxs.z 512` -- down to world z -644..-628, three collision slabs 21
                # inches under the harbour surface that VtMB never had anywhere near the play
                # volume. VtMB draws the miniature from its own camera and no body ever travels
                # there, so the faithful hull count for a miniature is zero. The visual
                # (`brush_mesh`), the contents word and `blocks_player` are untouched: this drops
                # the collider, not the entity.
                hulls: list[list[float]] = []
                # Per-hull contents, parallel to `hulls`. The OR below answers for the entity as a
                # whole, which is all `blocks_player` ever needed, but a mover's body wears the
                # collision profile of its own brushes' signature -- a glass `func_brush` must not
                # block sight while the door beside it does -- and an OR cannot say that when an
                # entity's brushes disagree. Keeping the column lets the consumer decide.
                hull_contents: list[int] = []
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
                    hull_contents.append(contents & 0xFFFFFFFF)
                    hulls.append(hull_vertices(points))
                if entity.get("sky"):
                    hulls = []
                    hull_contents = []
                entity["model"] = index
                entity["hulls"] = hulls
                entity["hull_contents"] = hull_contents
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


def _key_number(keys: dict[str, str], key: str, default: float = 0.0) -> float:
    match = _NUMERIC.fullmatch(keys.get(key, ""))
    return float(match.group(0)) if match else default


def _key_vector(keys: dict[str, str], key: str) -> list[float] | None:
    raw = keys.get(key)
    if not raw:
        return None
    try:
        return [float(value) for value in raw.split()][:3]
    except ValueError:
        return None


def _fog(keys: dict[str, str], distance_scale: float) -> dict[str, Any]:
    colour = _key_vector(keys, "fogcolor") or [0.0, 0.0, 0.0]
    return {
        "on": _key_number(keys, "fogenable") >= 1.0,
        "rgb": [max(0.0, c) / 255.0 for c in colour],
        "start": _key_number(keys, "fogstart") * distance_scale * INCH_TO_CM,
        "end": _key_number(keys, "fogend") * distance_scale * INCH_TO_CM,
    }


def write_environment(
    units: MapUnits, sky: SkyScope, pair_blocks: Sequence[Sequence[tuple[str, str]]],
    out_dir: Path, root: Path | None = None,
) -> dict[str, Any]:
    """`<map>.env`: the 2D sky name, the face-set flag and the two fog sets.

    Fog is two different things belonging to two different renders: `worldspawn`'s set is the
    world's, `sky_camera`'s is the 3D-skybox pass's own and its distances are in miniature units,
    so a world-space equivalent is `x scale`. The 2D backdrop is fogged by neither -- every sky
    face in the game carries `$nofog 1`.

    `skybox` states whether the install publishes all six faces of this map's sky. The legacy
    exporter asked that of `shared/manifest.json`; since 0018 story 21-4 it is asked of the
    published texture units (`sky_faces_published`), which is what leaves this producer reading
    nothing outside `$ELYSIUM_EXPORT_V2_ROOT`.
    """

    # The first non-empty `skyname` in lump order, whichever entity authors it -- the legacy scan
    # ran one case-insensitive search over the whole concatenated text and took the first hit.
    skyname = None
    for pairs in pair_blocks:
        for key, value in pairs:
            if key.strip().lower() == "skyname" and value:
                skyname = value.lower()
                break
        if skyname is not None:
            break
    sky_ok = bool(skyname) and sky_faces_published(skyname, root)

    world_fog = _fog(first_of_class(pair_blocks, "worldspawn"), 1.0)
    sky_fog = _fog(first_of_class(pair_blocks, "sky_camera"), sky.scale if sky.ok else 1.0)
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


def write_spawn(
    units: MapUnits, pair_blocks: Sequence[Sequence[tuple[str, str]]], out_dir: Path
) -> bool:
    """`<map>.spawn`: `info_player_start` origin and yaw, Unreal cm.

    Source angles are "pitch yaw roll"; yaw is the middle value and is negated, because the Y flip
    reverses its sense. The first `info_player_start` block wins, as in the legacy loop.
    """

    for keys in blocks_of_class(pair_blocks, "info_player_start"):
        origin = _SPACED_TRIPLE.fullmatch(keys.get("origin", ""))
        angles = _SPACED_TRIPLE.fullmatch(keys.get("angles", ""))
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


def rope_material_id(start: dict[str, str]) -> str:
    """The `vtmb:material:<key>` id one rope segment binds (R6.5).

    `RopeShader` (0/1/2 -> `cable/cable`, `cable/rope`, `cable/chain`, from
    `CRopeKeyframe::KeyValue`) overrides `RopeMaterial`; a node authoring neither is
    `cable/cable`. The key is `shared_corpus.material_key`, the identity every other placement
    lane names a material by, so the runtime resolves it through the R5.4 rule
    (`importers.materials.asset_path_for`) to the imported `MI_`.
    """
    if "ropeshader" in start:
        try:
            shader = int(atof(start.get("ropeshader", "0")))
        except ValueError:
            shader = 0
        material = ROPE_SHADER.get(shader, "cable/cable")
    else:
        material = (start.get("ropematerial") or "cable/cable").replace("\\", "/").lower()
    return "vtmb:material:" + shared_corpus.material_key(material)


#: The two classnames that construct `CRopeKeyframe`. Roles are resolved topologically, so which
#: one a node carries decides nothing.
ROPE_CLASSNAMES = ("keyframe_rope", "move_rope")


def rope_nodes(blocks: Sequence[Sequence[tuple[str, str]]]) -> list[dict[str, str]]:
    """Every rope node the map authors, folded case-insensitively, in lump order."""
    return [
        folded
        for pairs in blocks
        for folded in ({key.lower(): value for key, value in pairs},)
        if folded.get("classname") in ROPE_CLASSNAMES
    ]


def rope_rows(blocks: Sequence[Sequence[tuple[str, str]]]) -> list[dict[str, Any]]:
    """One row per cable segment: the overhead cables VtMB strings between poles and buildings.

    A rope is a chain of `move_rope`/`keyframe_rope` nodes linked by `NextKey`; both classnames
    construct the same `CRopeKeyframe`, so the roles are resolved topologically. Every parameter
    here is the RE'd runtime state rather than the raw keyvalue -- the rest length applies `Slack`
    twice, subtracts a flat 100 units and truncates through an integer divide; `nodes` comes from
    `Type`, not `Subdiv`. All of it is ported from `UE_bsp_to_scene.write_ropes`. R6.5: the row
    carries the material's `vtmb:material:` id and no
    decoded texture or shader flag -- the imported `MI_` owns those.

    0018 story 21-3 split this out of `write_ropes`, as `light_rows` was split for R5.6: the
    map-geometry stage projects these rows into the manifest and the map bake stands one actor per
    row in the level, so the runtime opens no `.ropes` file. `write_ropes` below still formats the
    same rows into the same bytes, because the sidecar remains an offline intermediate.
    """

    nodes = rope_nodes(blocks)
    by_name: dict[str, dict[str, str]] = {}
    for pairs in blocks:
        folded = {key.lower(): value for key, value in pairs}
        name = folded.get("targetname", "").lower()
        if name and name not in by_name:
            by_name[name] = folded                    # engine's FindEntityByName: first wins

    def origin_of(entity: dict[str, str]) -> tuple[float, float, float] | None:
        tokens = entity.get("origin", "0 0 0").split()
        return source_to_unreal(*(atof(c) for c in tokens)) if len(tokens) == 3 else None

    def number(entity: dict[str, str], key: str, default: str) -> float:
        value = entity.get(key, "")
        return atof(value) if value else float(default)

    rows: list[dict[str, Any]] = []
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
        material_id = rope_material_id(start)
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
        rows.append(
            {
                "index": len(rows),
                "materialId": material_id,
                "aCm": [a[0], a[1], a[2]],
                "bCm": [b[0], b[1], b[2]],
                "widthCm": width_cm,
                "restCm": rest_cm,
                "nodes": node_count,
                "texScale": texscale,
                "flags": flags,
            }
        )
    return rows


def rope_line(row: dict[str, Any]) -> str:
    """One `rope_rows` row as its 12-token `.ropes` line, the only place the format is written."""
    a, b = row["aCm"], row["bCm"]
    return (
        f"{row['materialId']} "
        f"{a[0]:.4f} {a[1]:.4f} {a[2]:.4f} {b[0]:.4f} {b[1]:.4f} {b[2]:.4f} "
        f"{row['widthCm']:.4f} {row['restCm']:.4f} {row['nodes']} "
        f"{row['texScale']:.4f} {row['flags']}"
    )


def write_ropes(
    units: MapUnits,
    blocks: Sequence[Sequence[tuple[str, str]]],
    out_dir: Path,
) -> dict[str, int]:
    """`<map>.ropes`, an offline intermediate since 0018 story 21-3: `rope_rows` formatted one line
    per segment. The bytes are unchanged -- the runtime reads the baked actors instead."""
    lines = [rope_line(row) for row in rope_rows(blocks)]
    if lines:
        write_sidecar_lines(out_dir / f"{units.name}.ropes", lines)
    return {"segments": len(lines), "nodes": len(rope_nodes(blocks))}


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


# ---------------------------------------------------------------- the published sky faces


def sky_faces_published(sky_name: str, root: Path | None = None) -> bool:
    """Does the install publish all six faces of this sky?

    `<map>.env`'s `skybox` flag. Until 0018 story 21-4 this was a membership test over
    `shared/manifest.json`'s texture table -- the legacy exporter's own question, asked of the
    corpus the deleted `UE_extract_corpus` built. It is the same question asked of the published texture
    units instead, which is the one thing that let this producer stop reading the legacy export
    root at all. `importers.sky_composites.plan_composites` already names exactly these files,
    and refuses a partial set the same way.
    """

    faces = (root or paths.export_v2_root()) / "textures" / shared_corpus.SKY_DIR
    return all(
        (faces / f"{sky_name}{face}.glb").is_file() for face in shared_corpus.SKY_FACES
    )


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
    scenes: dict[str, Any]
    brush_meshes: dict[int, str]


def prepare_join(map_name: str, root: Path | None = None, compile_water=None) -> MapJoin:
    """Read one map's units and derive the shared tables.

    `compile_water` is `meshed_faces`' own argument, passed through: the map-geometry stage has the
    material lane's staged provenance and can answer "is this unit `%compilewater`", the sidecar
    producer has not and passes `None`. One classifier either way -- the caller supplies the
    evidence, the classifier stays here.
    """

    units = read_units(map_name, root)
    pair_blocks = entity_pair_blocks(units.entities["entities"])
    sky = SkyScope(units, pair_blocks)
    backings = visibility_backing_models(pair_blocks)
    scenes = meshed_faces(units, sky, backings, compile_water)
    brush_meshes = {index: f"brush_{index}" for index in sorted(scenes["brush"])}
    return MapJoin(units, sky, pair_blocks, scenes, brush_meshes)


def write_sidecars(
    map_name: str,
    *,
    root: Path | None = None,
    out_dir: Path | None = None,
    entity_fields: EntityDivergences = LEGACY_ENTITY_FIELDS,
) -> dict[str, Any]:
    """Produce one map's legacy sidecars from its published units and return the run's numbers.

    The run still fails rather than half-finishing -- every sidecar a later lane reads must be on
    disk when it returns -- but it no longer writes a `<map>.ready` marker: 0018 story 21-3 moved
    the travel gate onto the bake's own four packages, and nothing reads the marker any more.
    `entity_fields` opts `.ents` into the R3.4 divergences one at
    a time; the default keeps this run on the reading the deleted decoder had.

    **This run reads nothing outside `$ELYSIUM_EXPORT_V2_ROOT`** (0018 story 21-4). The one tie
    left was `corpus_root`, which fed `shared/manifest.json` to the `.env` sky-face test and is
    gone with it; that is what lets `bake map` call this itself instead of requiring a legacy
    export directory somebody ran `export map` to produce.
    """

    join = prepare_join(map_name, root)
    units = join.units
    out_dir = Path(out_dir) if out_dir is not None else sidecar_dir(map_name, root)
    out_dir.mkdir(parents=True, exist_ok=True)

    pair_blocks = join.pair_blocks
    sky = join.sky
    scenes = join.scenes
    brush_meshes = join.brush_meshes

    report: dict[str, Any] = {"map": map_name, "outputDir": str(out_dir)}
    report["hulls"] = write_hulls(units, sky, out_dir)
    report["ents"] = write_entities(units, sky, pair_blocks, brush_meshes, out_dir, entity_fields)
    report["lights"] = write_lights(units, sky, out_dir)
    report["env"] = write_environment(units, sky, pair_blocks, out_dir, root)
    report["sky"] = write_sky(units, sky, bool(scenes["sky"]), out_dir)
    report["spawn"] = write_spawn(units, pair_blocks, out_dir)
    report["ropes"] = write_ropes(units, pair_blocks, out_dir)
    report["dispcol"] = write_displacement_collision(units, scenes["world"], out_dir)
    report["brushMeshes"] = len(brush_meshes)
    report["skyArea"] = sky.area if sky.ok else None

    # The completeness check the retired `.ready` marker used to stand for: the run fails rather
    # than leaving a later lane to read a sidecar that was never written.
    required = [f"{map_name}{suffix}" for suffix in (".ents", ".hulls", ".lights", ".env")]
    missing = [name for name in required if not (out_dir / name).is_file()]
    if missing:
        raise MapSidecarError(f"{map_name}: sidecars missing after the run: {', '.join(missing)}")
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
    arguments = parser.parse_args(argv)
    for map_name in arguments.maps:
        out_dir = (arguments.out_root / map_name) if arguments.out_root else None
        report = write_sidecars(map_name, out_dir=out_dir)
        print(json.dumps(report, separators=(",", ":")))
    return 0


if __name__ == "__main__":                                          # pragma: no cover
    raise SystemExit(main())
