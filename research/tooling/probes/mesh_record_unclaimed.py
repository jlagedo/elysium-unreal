"""What the per-texture, per-model and per-mesh records carry that nothing decodes.

Usage:
    uv run elysium research mesh_record_unclaimed [--limit N] [--report PATH]
    uv run elysium research mesh_record_unclaimed --dump models/character/x.mdl

`studiohdr_unclaimed_fields` swept the file header and closed it: no second
count/index table hides there. But a renderer-side cloth system is not addressed
from the header. `engine.dll` registers `r_cloth`, while StudioRender walks
**textures, models and meshes** -- so an authored gate it reads is a field in one
of those records, and
those are exactly the records our decoders touch a handful of bytes of:

    StudioTexture   20 B, of which `nameindex`@0 is read.
    StudioModel    224 B, of which @128..@158 and the eyeball pair @192/@196.
    StudioMesh      60 B, of which material@0, numverts@8, vertexoffset@12,
                    numflexes@16, flexindex@20, materialtype@24.

That leaves the majority of every StudioModel and every StudioTexture unread. A
per-material or per-mesh flag would sit there and no tool -- ours, Crowbar, or
VAMPTools -- would ever have surfaced it.

The census reports, for every 4-byte slot of all three records:

    * how many records carry a non-zero;
    * the values it actually takes, which separates a flag word (a few small
      values, often powers of two) from an offset (large, file-scaled) and from
      a float (garbage as an int);
    * whether it resolves inside the file image, i.e. could be an index;
    * **the garment split** -- the same numbers computed over meshes whose
      material names a garment against every other mesh in the corpus.

The split is the point. A slot that is non-zero on 4% of meshes corpus-wide and
on most garment meshes is a candidate gate; a slot non-zero everywhere or nowhere
is not. Naming only classifies here, it never filters: every slot is reported for
both halves, so a garment authored into a generically named body material
(`sheriffbody2`) lands in the "other" column and depresses the signal rather than
vanishing from it. `--dump` then reads one model's records in full, which is how
a named surface is checked directly rather than through the aggregate.

Nothing here names a field. The output is the ranked shortlist and the evidence,
the same shape the chain table's own discovery had before its constructor was
located.
"""

from __future__ import annotations

import argparse
import json
import struct
from collections import Counter
from pathlib import Path
from typing import Any

from elysium_pipeline.formats import install, mdl_skel
from elysium_pipeline.paths import research_root

REPORT_NAME = "mesh-record-unclaimed.json"

MDL_VERSION = 2531

TEXTURE_STRIDE = 20
MODEL_STRIDE = 224
MESH_STRIDE = 60

#: Header pairs this walk needs, all already claimed by `docs/vtmb/mdl_v2531.md`.
H_NUM_TEXTURES, H_TEXTURE_INDEX = 292, 296
H_NUM_BODYPARTS, H_BODYPART_INDEX = 320, 324

#: Slots our decoders read, per record. Everything else in the stride is swept.
#: `StudioModel`'s name and known pre-vertex fields occupy the first 128 bytes,
#: so that already-owned span is excluded from this unclaimed-slot survey.
TEXTURE_CLAIMED = {0}
MODEL_CLAIMED = set(range(0, 128, 4)) | {128, 132, 136, 140, 144, 148, 156, 192, 196}
MESH_CLAIMED = {0, 8, 12, 16, 20, 24}

#: How a material name is classified. This annotates the census; it never gates
#: which records are read.
GARMENT_TOKENS = ("skirt", "dress", "coat", "robe", "cloth", "cape", "cloak",
                  "gown", "jacket", "trench")


def _i32(d: bytes, o: int) -> int:
    return struct.unpack_from("<i", d, o)[0]


def _f32(d: bytes, o: int) -> float:
    return struct.unpack_from("<f", d, o)[0]


def _cstr(d: bytes, o: int) -> str:
    end = d.find(b"\0", o)
    return d[o:end if end >= 0 else len(d)].decode("ascii", "replace")


def model_keys(idx: dict) -> list[str]:
    return sorted(k for k in idx if k.startswith("models/") and k.endswith(".mdl"))


def read_materials(d: bytes) -> list[str]:
    """Every StudioTexture's name, in header order."""
    n = _i32(d, H_NUM_TEXTURES)
    base = _i32(d, H_TEXTURE_INDEX)
    out = []
    for i in range(n):
        rec = base + i * TEXTURE_STRIDE
        if rec + TEXTURE_STRIDE > len(d):
            break
        out.append(_cstr(d, rec + _i32(d, rec)))
    return out


def is_garment(name: str) -> bool:
    low = name.lower()
    return any(token in low for token in GARMENT_TOKENS)


class SlotTally:
    """Per-offset behaviour for one record type, split garment / other."""

    def __init__(self, claimed: set[int], stride: int):
        self.offsets = [o for o in range(0, stride, 4) if o not in claimed]
        self.total = Counter()
        self.nonzero = Counter()
        self.in_image = Counter()
        self.values: dict[int, Counter] = {o: Counter() for o in self.offsets}
        self.g_total = Counter()
        self.g_nonzero = Counter()

    def observe(self, d: bytes, base: int, garment: bool) -> None:
        size = len(d)
        for off in self.offsets:
            if base + off + 4 > size:
                continue
            value = _i32(d, base + off)
            self.total[off] += 1
            if garment:
                self.g_total[off] += 1
            if value != 0:
                self.nonzero[off] += 1
                if garment:
                    self.g_nonzero[off] += 1
            if 0 < value < size:
                self.in_image[off] += 1
            # Cap the distinct-value set: an offset is only interesting as a flag
            # if it takes FEW values, so a slot that blows the cap has answered
            # the question by blowing it.
            counter = self.values[off]
            if value in counter or len(counter) < 24:
                counter[value] += 1

    def rows(self) -> list[dict[str, Any]]:
        out = []
        for off in self.offsets:
            total = self.total[off] or 1
            g_total = self.g_total[off]
            g_rate = (self.g_nonzero[off] / g_total) if g_total else None
            o_total = self.total[off] - g_total
            o_nonzero = self.nonzero[off] - self.g_nonzero[off]
            o_rate = (o_nonzero / o_total) if o_total else None
            out.append(
                {
                    "offset": off,
                    "records": self.total[off],
                    "nonzero": self.nonzero[off],
                    "nonzero_rate": self.nonzero[off] / total,
                    "resolves_inside_image": self.in_image[off],
                    "distinct_values_capped": len(self.values[off]),
                    "most_common_values": [
                        {"value": v, "records": c}
                        for v, c in self.values[off].most_common(6)
                    ],
                    "garment_records": g_total,
                    "garment_nonzero": self.g_nonzero[off],
                    "garment_nonzero_rate": g_rate,
                    "other_nonzero_rate": o_rate,
                    "separation": (
                        None if g_rate is None or o_rate is None else g_rate - o_rate
                    ),
                }
            )
        return out


def sweep(limit: int | None = None) -> dict[str, Any]:
    idx = install.build_index()
    keys = model_keys(idx)
    if limit is not None:
        keys = keys[:limit]

    counts = {"models_seen": 0, "models_unreadable": 0, "models_wrong_version": 0,
              "textures": 0, "models_records": 0, "meshes": 0, "garment_meshes": 0}
    tex = SlotTally(TEXTURE_CLAIMED, TEXTURE_STRIDE)
    mdl_t = SlotTally(MODEL_CLAIMED, MODEL_STRIDE)
    mesh_t = SlotTally(MESH_CLAIMED, MESH_STRIDE)
    # Which models carry a non-zero in each mesh slot, so a candidate can be
    # opened with --dump rather than only counted.
    witnesses: dict[str, dict[int, list[str]]] = {"texture": {}, "mesh": {}, "model": {}}

    for key in keys:
        d = install.read(idx, key)
        if not d or len(d) < 412:
            counts["models_unreadable"] += 1
            continue
        counts["models_seen"] += 1
        if _i32(d, 4) != MDL_VERSION:
            counts["models_wrong_version"] += 1
            continue

        materials = read_materials(d)
        tex_base = _i32(d, H_TEXTURE_INDEX)
        for i, name in enumerate(materials):
            rec = tex_base + i * TEXTURE_STRIDE
            garment = is_garment(name)
            tex.observe(d, rec, garment)
            counts["textures"] += 1
            for off in tex.offsets:
                if rec + off + 4 <= len(d) and _i32(d, rec + off):
                    witnesses["texture"].setdefault(off, [])
                    if len(witnesses["texture"][off]) < 6:
                        witnesses["texture"][off].append(f"{key}::{name}")

        num_bp = _i32(d, H_NUM_BODYPARTS)
        bp_index = _i32(d, H_BODYPART_INDEX)
        for bp in range(num_bp):
            mbp = bp_index + bp * 16
            if mbp + 16 > len(d):
                break
            num_models = _i32(d, mbp + 4)
            model_index = _i32(d, mbp + 12)
            for m in range(num_models):
                model_base = mbp + model_index + m * MODEL_STRIDE
                if model_base < 0 or model_base + MODEL_STRIDE > len(d):
                    break
                num_meshes = _i32(d, model_base + 136)
                mesh_index = _i32(d, model_base + 140)
                # A StudioModel is garment-bearing if any of its meshes is; that
                # is decided after the mesh walk, so the model record is observed
                # against the union.
                model_meshes = []
                for mi in range(num_meshes):
                    mesh_base = model_base + mesh_index + mi * MESH_STRIDE
                    if mesh_base < 0 or mesh_base + MESH_STRIDE > len(d):
                        break
                    material = _i32(d, mesh_base + 0)
                    name = materials[material] if 0 <= material < len(materials) else ""
                    model_meshes.append((mesh_base, name))

                model_garment = any(is_garment(n) for _, n in model_meshes)
                mdl_t.observe(d, model_base, model_garment)
                counts["models_records"] += 1
                for off in mdl_t.offsets:
                    if model_base + off + 4 <= len(d) and _i32(d, model_base + off):
                        witnesses["model"].setdefault(off, [])
                        if len(witnesses["model"][off]) < 6:
                            witnesses["model"][off].append(key)

                for mesh_base, name in model_meshes:
                    garment = is_garment(name)
                    mesh_t.observe(d, mesh_base, garment)
                    counts["meshes"] += 1
                    if garment:
                        counts["garment_meshes"] += 1
                    for off in mesh_t.offsets:
                        if _i32(d, mesh_base + off):
                            witnesses["mesh"].setdefault(off, [])
                            if len(witnesses["mesh"][off]) < 6:
                                witnesses["mesh"][off].append(f"{key}::{name}")

    return {
        "counts": counts,
        "texture_slots": tex.rows(),
        "model_slots": mdl_t.rows(),
        "mesh_slots": mesh_t.rows(),
        "witnesses": {k: {str(o): v for o, v in d.items()} for k, d in witnesses.items()},
    }


def dump(model_path: str) -> dict[str, Any]:
    """Every texture, model and mesh record of one model, slot by slot."""
    idx = install.build_index()
    key = model_path if model_path.startswith("models/") else f"models/{model_path}"
    if not key.endswith(".mdl"):
        key += ".mdl"
    d = install.read(idx, key)
    if not d:
        raise SystemExit(f"not found in the engine-resolved install: {key}")

    materials = read_materials(d)
    tex_base = _i32(d, H_TEXTURE_INDEX)
    out: dict[str, Any] = {"model": key, "version": _i32(d, 4), "size": len(d),
                           "textures": [], "models": []}

    for i, name in enumerate(materials):
        rec = tex_base + i * TEXTURE_STRIDE
        out["textures"].append(
            {
                "index": i,
                "name": name,
                "garment_named": is_garment(name),
                "slots": {
                    str(o): _i32(d, rec + o) for o in range(0, TEXTURE_STRIDE, 4)
                },
            }
        )

    num_bp = _i32(d, H_NUM_BODYPARTS)
    bp_index = _i32(d, H_BODYPART_INDEX)
    for bp in range(num_bp):
        mbp = bp_index + bp * 16
        num_models = _i32(d, mbp + 4)
        model_index = _i32(d, mbp + 12)
        for m in range(num_models):
            model_base = mbp + model_index + m * MODEL_STRIDE
            if model_base + MODEL_STRIDE > len(d):
                break
            num_meshes = _i32(d, model_base + 136)
            mesh_index = _i32(d, model_base + 140)
            entry = {
                "bodypart": bp,
                "model": m,
                "name": _cstr(d, model_base),
                "num_meshes": num_meshes,
                "num_vertices": _i32(d, model_base + 144),
                "slots": {
                    str(o): {"i": _i32(d, model_base + o), "f": _f32(d, model_base + o)}
                    for o in range(128, MODEL_STRIDE, 4)
                },
                "meshes": [],
            }
            for mi in range(num_meshes):
                mesh_base = model_base + mesh_index + mi * MESH_STRIDE
                if mesh_base + MESH_STRIDE > len(d):
                    break
                material = _i32(d, mesh_base + 0)
                name = materials[material] if 0 <= material < len(materials) else ""
                entry["meshes"].append(
                    {
                        "index": mi,
                        "material": name,
                        "garment_named": is_garment(name),
                        "num_vertices": _i32(d, mesh_base + 8),
                        "slots": {
                            str(o): {"i": _i32(d, mesh_base + o),
                                     "f": _f32(d, mesh_base + o)}
                            for o in range(0, MESH_STRIDE, 4)
                        },
                    }
                )
            out["models"].append(entry)
    return out


#: The three per-mesh slots the census isolates: non-zero on 84 of 9,741 meshes,
#: all three together or none, every value resolving inside the file image.
MESH_TRIPLE = (48, 52, 56)


def carriers(limit: int | None = None) -> dict[str, Any]:
    """Every mesh carrying the +48/+52/+56 triple, with its block spans.

    The spans are what says whether the three are arrays and what their element
    size is: `next_offset - offset` over a known vertex count. They are reported
    raw, per mesh, rather than reduced to a stride, because the corpus does not
    agree on one and a probe that averaged them would hide that.
    """
    idx = install.build_index()
    keys = model_keys(idx)
    if limit is not None:
        keys = keys[:limit]

    rows: list[dict[str, Any]] = []
    for key in keys:
        d = install.read(idx, key)
        if not d or len(d) < 412 or _i32(d, 4) != MDL_VERSION:
            continue
        materials = read_materials(d)
        num_bp = _i32(d, H_NUM_BODYPARTS)
        bp_index = _i32(d, H_BODYPART_INDEX)
        for bp in range(num_bp):
            mbp = bp_index + bp * 16
            if mbp + 16 > len(d):
                break
            for m in range(_i32(d, mbp + 4)):
                model_base = mbp + _i32(d, mbp + 12) + m * MODEL_STRIDE
                if model_base < 0 or model_base + MODEL_STRIDE > len(d):
                    break
                num_meshes = _i32(d, model_base + 136)
                mesh_index = _i32(d, model_base + 140)
                for mi in range(num_meshes):
                    mesh_base = model_base + mesh_index + mi * MESH_STRIDE
                    if mesh_base < 0 or mesh_base + MESH_STRIDE > len(d):
                        break
                    triple = [_i32(d, mesh_base + o) for o in MESH_TRIPLE]
                    if not any(triple):
                        continue
                    material = _i32(d, mesh_base + 0)
                    name = materials[material] if 0 <= material < len(materials) else ""
                    nv = _i32(d, mesh_base + 8)
                    rows.append(
                        {
                            "model": key,
                            "material": name,
                            "garment_named": is_garment(name),
                            "vertices": nv,
                            "offsets": triple,
                            "all_three_set": all(triple),
                            "span_48_52": triple[1] - triple[0],
                            "span_52_56": triple[2] - triple[1],
                            "per_vertex_48": (triple[1] - triple[0]) / nv if nv else None,
                            "per_vertex_52": (triple[2] - triple[1]) / nv if nv else None,
                        }
                    )
    rows.sort(key=lambda r: (r["model"], r["material"]))
    return {"carrier_meshes": len(rows), "carriers": rows}


def raw(model_path: str, material: str, length: int = 128) -> dict[str, Any]:
    """Hex the head of each of the three blocks for one named mesh."""
    idx = install.build_index()
    key = model_path if model_path.startswith("models/") else f"models/{model_path}"
    if not key.endswith(".mdl"):
        key += ".mdl"
    d = install.read(idx, key)
    if not d:
        raise SystemExit(f"not found in the engine-resolved install: {key}")
    materials = read_materials(d)
    num_bp = _i32(d, H_NUM_BODYPARTS)
    bp_index = _i32(d, H_BODYPART_INDEX)
    for bp in range(num_bp):
        mbp = bp_index + bp * 16
        for m in range(_i32(d, mbp + 4)):
            model_base = mbp + _i32(d, mbp + 12) + m * MODEL_STRIDE
            for mi in range(_i32(d, model_base + 136)):
                mesh_base = model_base + _i32(d, model_base + 140) + mi * MESH_STRIDE
                mat = _i32(d, mesh_base + 0)
                name = materials[mat] if 0 <= mat < len(materials) else ""
                if name.lower() != material.lower():
                    continue
                out = {"model": key, "material": name,
                       "vertices": _i32(d, mesh_base + 8), "blocks": []}
                for off in MESH_TRIPLE:
                    base = _i32(d, mesh_base + off)
                    chunk = d[base:base + length]
                    out["blocks"].append(
                        {
                            "slot": off,
                            "file_offset": base,
                            "hex": chunk.hex(),
                            "as_u8": list(chunk[:48]),
                            "as_i16": list(struct.unpack_from(
                                f"<{min(24, len(chunk) // 2)}h", chunk)),
                            "as_f32": [round(x, 5) for x in struct.unpack_from(
                                f"<{min(12, len(chunk) // 4)}f", chunk)],
                        }
                    )
                return out
    raise SystemExit(f"material not found on {key}: {material}")


#: `studiohdr.Flags`. Bit 0x400 is the gate engine.dll tests before allocating a
#: StudioRender cloth instance; the other bits are carried, not interpreted.
H_FLAGS = 228
CLOTH_FLAG = 0x400


def flags(limit: int | None = None) -> dict[str, Any]:
    """Cross the header flag against the payload, both directions.

    A gate is only a gate if it agrees with what it gates. This tallies, per
    model: whether `Flags & 0x400` is set, whether any mesh carries the
    +48/+52/+56 triple, and whether any StudioModel carries the +200/+204 pair.
    The four-cell contingency is the result -- in particular the two off-diagonal
    cells, because a flagged model with no payload and a payload with no flag say
    different things about where the count comes from.

    Every distinct flag word is reported with its bit population, so the bits
    sharing the field with 0x400 are visible rather than assumed.
    """
    idx = install.build_index()
    keys = model_keys(idx)
    if limit is not None:
        keys = keys[:limit]

    cells = Counter()
    words = Counter()
    bit_tally = Counter()
    flagged_without_payload: list[str] = []
    payload_without_flag: list[str] = []
    flagged: list[dict[str, Any]] = []

    for key in keys:
        d = install.read(idx, key)
        if not d or len(d) < 412 or _i32(d, 4) != MDL_VERSION:
            continue
        word = _i32(d, H_FLAGS)
        words[word] += 1
        for bit in range(32):
            if word & (1 << bit):
                bit_tally[1 << bit] += 1
        has_flag = bool(word & CLOTH_FLAG)

        mesh_payload = 0
        model_payload = 0
        num_bp = _i32(d, H_NUM_BODYPARTS)
        bp_index = _i32(d, H_BODYPART_INDEX)
        for bp in range(num_bp):
            mbp = bp_index + bp * 16
            if mbp + 16 > len(d):
                break
            for m in range(_i32(d, mbp + 4)):
                model_base = mbp + _i32(d, mbp + 12) + m * MODEL_STRIDE
                if model_base < 0 or model_base + MODEL_STRIDE > len(d):
                    break
                if _i32(d, model_base + 200):
                    model_payload += 1
                num_meshes = _i32(d, model_base + 136)
                mesh_index = _i32(d, model_base + 140)
                for mi in range(num_meshes):
                    mesh_base = model_base + mesh_index + mi * MESH_STRIDE
                    if mesh_base < 0 or mesh_base + MESH_STRIDE > len(d):
                        break
                    if any(_i32(d, mesh_base + o) for o in MESH_TRIPLE):
                        mesh_payload += 1

        has_payload = mesh_payload > 0
        cells[(has_flag, has_payload)] += 1
        if has_flag and not has_payload:
            flagged_without_payload.append(key)
        if has_payload and not has_flag:
            payload_without_flag.append(key)
        if has_flag or has_payload:
            flagged.append(
                {
                    "model": key,
                    "flags": f"0x{word & 0xFFFFFFFF:x}",
                    "cloth_flag": has_flag,
                    "cloth_meshes": mesh_payload,
                    "model_records_with_pair": model_payload,
                }
            )

    flagged.sort(key=lambda r: r["model"])
    return {
        "contingency": {
            "flag_and_payload": cells[(True, True)],
            "flag_without_payload": cells[(True, False)],
            "payload_without_flag": cells[(False, True)],
            "neither": cells[(False, False)],
        },
        "flag_words": [{"flags": f"0x{w & 0xFFFFFFFF:x}", "models": c}
                       for w, c in words.most_common(24)],
        "bit_population": [{"bit": f"0x{b:x}", "models": c}
                           for b, c in sorted(bit_tally.items())],
        "flagged_without_payload": sorted(flagged_without_payload)[:40],
        "payload_without_flag": sorted(payload_without_flag)[:40],
        "cloth_models": flagged,
    }


def maps(model_path: str, material: str) -> dict[str, Any]:
    """Read the three slots as per-vertex maps and check them against geometry.

    Tests the decoded render substitution rather than restating it. If +48 is a
    per-vertex selector where 0xFF means ordinary skinning, and +52/+56 are
    signed 16-bit indices into the simulated control points, then three things
    must hold and each fails loudly if the reading is wrong:

        * the maps are exactly `numvertices` entries long -- 1, 2 and 2 bytes
          each -- and the selector's non-0xFF values form a small set;
        * the indices stay small in magnitude, because they address control
          points rather than render vertices, and the sign bit is a flag rather
          than part of the number;
        * on a mesh holding both a skinned body and a simulated garment the
          selector SPLITS IT SPATIALLY. `sheriffbody2` is one mesh containing
          the Sheriff's body and his coat, so the simulated vertices must occupy
          a different band of the bind pose than the skinned ones. This is the
          test naming could never do, and a decode that is merely plausible will
          fail it.

    Positions come from the same `decode_skinned` the shipped exporter builds
    its glb from, joined through its `mesh_map` remap, so a vertex reported here
    is a vertex the bake skins.
    """
    idx = install.build_index()
    key = model_path if model_path.startswith("models/") else f"models/{model_path}"
    if not key.endswith(".mdl"):
        key += ".mdl"
    d = install.read(idx, key)
    if not d:
        raise SystemExit(f"not found in the engine-resolved install: {key}")
    v = install.read(idx, key[:-4] + ".dx80.vtx")
    if not v:
        raise SystemExit(f"no dx80.vtx beside {key}")

    materials = read_materials(d)
    mesh_map: list[dict[str, Any]] = []
    surfaces = mdl_skel.decode_skinned(d, v, mesh_map)

    entry = None
    for rec in mesh_map:
        if rec["material"].lower() == material.lower():
            entry = rec
            break
    if entry is None:
        raise SystemExit(f"material not found on {key}: {material}")

    mesh_base = entry["model_base"] + _i32(d, entry["model_base"] + 140) \
        + entry["mesh_index"] * MESH_STRIDE
    nv = _i32(d, mesh_base + 8)
    offs = [_i32(d, mesh_base + o) for o in MESH_TRIPLE]
    surf = surfaces[entry["material"]]
    remap = entry["remap"]
    vertex_offset = entry["vertex_offset"]

    sel = list(d[offs[0]:offs[0] + nv])
    idx_a = list(struct.unpack_from(f"<{nv}h", d, offs[1]))
    idx_b = list(struct.unpack_from(f"<{nv}h", d, offs[2]))

    def band(local_ids: list[int]) -> dict[str, Any] | None:
        pts = []
        for local in local_ids:
            si = remap.get(vertex_offset + local)
            if si is not None and si < len(surf["pos"]):
                pts.append(surf["pos"][si])
        if not pts:
            return None
        return {
            "vertices": len(pts),
            "x": [round(min(p[0] for p in pts), 2), round(max(p[0] for p in pts), 2)],
            "y": [round(min(p[1] for p in pts), 2), round(max(p[1] for p in pts), 2)],
            "z": [round(min(p[2] for p in pts), 2), round(max(p[2] for p in pts), 2)],
        }

    simulated = [i for i, s in enumerate(sel) if s != 0xFF]
    skinned = [i for i, s in enumerate(sel) if s == 0xFF]

    signed_a = sum(1 for x in idx_a if x < 0)
    mag_a = [abs(x) & 0x7FFF for x in idx_a]
    mag_b = [abs(x) & 0x7FFF for x in idx_b]

    return {
        "model": key,
        "material": entry["material"],
        "vertices": nv,
        "offsets": offs,
        "selector": {
            "distinct": sorted(set(sel)),
            "histogram": dict(Counter(sel).most_common(12)),
            "simulated": len(simulated),
            "skinned_0xFF": len(skinned),
        },
        "position_normal_index": {
            "min": min(idx_a), "max": max(idx_a),
            "negative_high_bit_set": signed_a,
            "magnitude_max": max(mag_a),
            "distinct_magnitudes": len(set(mag_a)),
        },
        "tangent_index": {
            "min": min(idx_b), "max": max(idx_b),
            "magnitude_max": max(mag_b),
            "distinct_magnitudes": len(set(mag_b)),
        },
        "bind_band_simulated": band(simulated),
        "bind_band_skinned": band(skinned),
        "bind_band_all": band(list(range(nv))),
    }


def anchors(audit_path: Path) -> dict[str, Any]:
    """Check a decoded cloth payload's anchor set against the geometry.

    Two falsifiers for the `anchored_particles` reading, both static, neither
    needing a retail run:

        1. **Where they sit.** A pinned particle is the attachment boundary, so
           the anchored set must occupy a different band of the bind pose than
           the mesh as a whole -- the waistband of a skirt, the shoulders of a
           coat. If the anchored band equals the full band, the flag is not an
           anchor flag.
        2. **What moves them.** A pinned particle is bone-skinned, so its
           vertices must be dominated by the bones the garment hangs from. This
           tallies the anchor set's own skin weights by bone name, which is
           independently measurable and was already measured for these two
           surfaces before the payload was decoded.

    Reads the audit JSON rather than re-deriving it: the payload decode belongs
    to the tool that proved it against the binary. This only joins its output to
    the geometry the shipped exporter skins.
    """
    report = json.loads(Path(audit_path).read_text(encoding="utf-8"))
    idx = install.build_index()
    out: list[dict[str, Any]] = []

    for entry in report.get("models", []):
        key = entry["model_path"]
        if not key.startswith("models/"):
            key = f"models/{key}"
        d = install.read(idx, key)
        v = install.read(idx, key[:-4] + ".dx80.vtx")
        if not d or not v:
            out.append({"model": key, "error": "model or dx80.vtx not readable"})
            continue

        names = mdl_skel.bone_names(d)
        materials = read_materials(d)
        mesh_map: list[dict[str, Any]] = []
        surfaces = mdl_skel.decode_skinned(d, v, mesh_map)

        # Every mesh on this model that carries the payload triple, as candidate
        # owners for a definition's anchor indices.
        candidates = []
        for rec in mesh_map:
            mb = rec["model_base"] + _i32(d, rec["model_base"] + 140) \
                + rec["mesh_index"] * MESH_STRIDE
            if not any(_i32(d, mb + o) for o in MESH_TRIPLE):
                continue
            candidates.append({"rec": rec, "numvertices": _i32(d, mb + 8)})

        for sm in entry.get("models", []):
            for di, defn in enumerate(sm.get("definitions", [])):
                anchor_ids = defn.get("anchor_vertex_indices") or []
                if not anchor_ids:
                    continue
                hi = max(anchor_ids)
                for cand in candidates:
                    rec = cand["rec"]
                    surf = surfaces[rec["material"]]
                    remap = rec["remap"]
                    voff = rec["vertex_offset"]

                    # `anchor_vertex_indices` are StudioModel-global, so the
                    # mesh's own `vertex_offset` must NOT be added. Mesh 0 has
                    # offset 0 and resolves either way, which is exactly the
                    # case that hides the mistake -- so both joins are tried and
                    # the one that resolves more vertices is reported by name.
                    def band(local_ids, add_offset):
                        pts, bones = [], Counter()
                        for local in local_ids:
                            si = remap.get((voff + local) if add_offset else local)
                            if si is None or si >= len(surf["pos"]):
                                continue
                            pts.append(surf["pos"][si])
                            for j, w in zip(surf["joints"][si], surf["weights"][si]):
                                if w > 0:
                                    bones[names[j] if j < len(names) else j] += w
                        if not pts:
                            return None, bones
                        return (
                            {
                                "vertices": len(pts),
                                "x": [round(min(p[0] for p in pts), 2),
                                      round(max(p[0] for p in pts), 2)],
                                "y": [round(min(p[1] for p in pts), 2),
                                      round(max(p[1] for p in pts), 2)],
                                "z": [round(min(p[2] for p in pts), 2),
                                      round(max(p[2] for p in pts), 2)],
                            },
                            bones,
                        )

                    global_band, global_bones = band(anchor_ids, False)
                    local_band, local_bones = band(anchor_ids, True)
                    used_global = (
                        (global_band["vertices"] if global_band else 0)
                        >= (local_band["vertices"] if local_band else 0)
                    )
                    anchor_band = global_band if used_global else local_band
                    anchor_bones = global_bones if used_global else local_bones
                    if not anchor_band:
                        continue
                    full_band, _ = band(list(range(cand["numvertices"])), True)
                    total = sum(anchor_bones.values()) or 1.0
                    out.append(
                        {
                            "model": key,
                            "definition": di,
                            "candidate_mesh": rec["material"],
                            "mesh_vertices": cand["numvertices"],
                            "join": "model-global" if used_global else "mesh-local",
                            "max_anchor_index": hi,
                            "particles": defn.get("particles"),
                            "anchored": defn.get("anchored_particles"),
                            "dynamic": defn.get("dynamic_particles"),
                            "anchor_band": anchor_band,
                            "mesh_band": full_band,
                            "anchor_bone_share": [
                                {"bone": b, "share": round(w / total, 4)}
                                for b, w in anchor_bones.most_common(6)
                            ],
                        }
                    )
    return {"checks": out}


def analyze(model_path: str, material: str) -> dict[str, Any]:
    """Read the whole span the three offsets cover and let its shape speak.

    Typed statistics rather than a hex window: what separates a per-vertex weight
    map from a particle list from an index table is the RANGE its elements take,
    not what the first 128 bytes happen to look like. Each block is scored under
    u8 / u16 / i32 / f32, and the f32 pass is compared against the mesh's own
    decoded bind positions -- the one test that tells a geometry copy (values
    inside the mesh's own bounding box) apart from weights (0..1) apart from
    indices (integers below the vertex count).

    `1.0f` run markers are reported separately. A repeated 0x3f800000 at an
    irregular stride is a record terminator, and where it falls is the record
    length the arithmetic on block sizes cannot recover.
    """
    idx = install.build_index()
    key = model_path if model_path.startswith("models/") else f"models/{model_path}"
    if not key.endswith(".mdl"):
        key += ".mdl"
    d = install.read(idx, key)
    if not d:
        raise SystemExit(f"not found in the engine-resolved install: {key}")

    materials = read_materials(d)
    target = None
    num_bp = _i32(d, H_NUM_BODYPARTS)
    bp_index = _i32(d, H_BODYPART_INDEX)
    for bp in range(num_bp):
        mbp = bp_index + bp * 16
        for m in range(_i32(d, mbp + 4)):
            model_base = mbp + _i32(d, mbp + 12) + m * MODEL_STRIDE
            for mi in range(_i32(d, model_base + 136)):
                mesh_base = model_base + _i32(d, model_base + 140) + mi * MESH_STRIDE
                mat = _i32(d, mesh_base + 0)
                name = materials[mat] if 0 <= mat < len(materials) else ""
                if name.lower() == material.lower():
                    target = (model_base, mesh_base, name)
                    break
            if target:
                break
        if target:
            break
    if not target:
        raise SystemExit(f"material not found on {key}: {material}")
    model_base, mesh_base, name = target

    nv = _i32(d, mesh_base + 8)
    offs = [_i32(d, mesh_base + o) for o in MESH_TRIPLE]

    # The mesh's own bind geometry, so a float block can be checked against the
    # box it would have to live in to be a copy of this surface.
    box = None
    try:
        from elysium_pipeline.formats import mdl as _mdl  # noqa: F401
        surfaces = mdl_skel.decode_skinned(d, install.read(idx, key[:-4] + ".dx80.vtx"))
        surf = surfaces.get(name)
        if surf and surf["pos"]:
            xs = [p[0] for p in surf["pos"]]
            ys = [p[1] for p in surf["pos"]]
            zs = [p[2] for p in surf["pos"]]
            box = {"x": [min(xs), max(xs)], "y": [min(ys), max(ys)],
                   "z": [min(zs), max(zs)], "vertices": len(surf["pos"])}
    except Exception as exc:  # the vtx may be absent for a bank model
        box = {"error": f"{type(exc).__name__}: {exc}"}

    def score(base: int, length: int) -> dict[str, Any]:
        chunk = d[base:base + length]
        n8 = len(chunk)
        u8 = list(chunk)
        n16 = n8 // 2
        u16 = list(struct.unpack_from(f"<{n16}H", chunk)) if n16 else []
        n32 = n8 // 4
        i32 = list(struct.unpack_from(f"<{n32}i", chunk)) if n32 else []
        f32 = list(struct.unpack_from(f"<{n32}f", chunk)) if n32 else []
        finite = [x for x in f32 if x == x and abs(x) < 1e30]
        in_box = None
        if box and "x" in box and finite:
            lo = min(box["x"][0], box["y"][0], box["z"][0]) - 1.0
            hi = max(box["x"][1], box["y"][1], box["z"][1]) + 1.0
            in_box = sum(1 for x in finite if lo <= x <= hi) / len(finite)
        ones = [i for i, v in enumerate(i32) if v == 0x3F800000]
        gaps = [b - a for a, b in zip(ones, ones[1:])]
        # Re-phase so a marker ends a record rather than sitting mid-record: the
        # window start is arbitrary, the marker is not. With a uniform gap G the
        # record is G words long and the first record starts at ones[0]-(G-1).
        records = []
        if len(ones) >= 2 and len(set(gaps)) == 1:
            stride = gaps[0]
            start = ones[0] - (stride - 1)
            while start < 0:
                start += stride
            for r in range(min(6, (len(i32) - start) // stride)):
                w = start + r * stride
                records.append(
                    {
                        "word": w,
                        "as_i32": i32[w:w + stride],
                        "as_f32": [round(x, 5) if abs(x) < 1e12 else x
                                   for x in f32[w:w + stride]],
                    }
                )
        return {
            "record_words": (gaps[0] if len(set(gaps)) == 1 and gaps else None),
            "record_count": len(ones),
            "records": records,
            "file_offset": base,
            "bytes": n8,
            "bytes_per_vertex": n8 / nv if nv else None,
            "u8": {"min": min(u8) if u8 else None, "max": max(u8) if u8 else None,
                   "distinct": len(set(u8)), "zero_fraction":
                   (u8.count(0) / len(u8)) if u8 else None},
            "u16": {"max": max(u16) if u16 else None,
                    "fraction_below_vertex_count":
                    (sum(1 for v in u16 if v < nv) / len(u16)) if u16 else None},
            "i32": {"min": min(i32) if i32 else None, "max": max(i32) if i32 else None,
                    "fraction_resolving_in_image":
                    (sum(1 for v in i32 if 0 < v < len(d)) / len(i32)) if i32 else None},
            "f32": {"finite": len(finite), "of": len(f32),
                    "min": min(finite) if finite else None,
                    "max": max(finite) if finite else None,
                    "fraction_unit_range":
                    (sum(1 for x in finite if -1.001 <= x <= 1.001) / len(finite))
                    if finite else None,
                    "fraction_inside_mesh_box": in_box},
            "one_float_markers": {"count": len(ones), "first": ones[:12],
                                  "gap_histogram": dict(Counter(gaps).most_common(8))},
        }

    spans = [offs[1] - offs[0], offs[2] - offs[1]]
    # Block 3 has no successor pointer. Bound it by the nearest larger offset the
    # record set knows about, so the read never runs past the section.
    horizon = min(
        [o for o in (
            _i32(d, mesh_base + 20),                      # this mesh's flexindex
            _i32(d, model_base + 204), _i32(d, model_base + 212),
            _i32(d, model_base + 220),
        ) if o > offs[2]] or [min(len(d), offs[2] + spans[1])]
    )
    spans.append(horizon - offs[2])

    return {
        "model": key, "material": name, "vertices": nv,
        "offsets": offs, "block_bytes": spans,
        "mesh_bind_box": box,
        "blocks": [score(offs[i], spans[i]) for i in range(3)],
    }


def summarize(report: dict[str, Any]) -> str:
    c = report["counts"]
    lines = [
        f"models {c['models_seen']:,} seen  textures {c['textures']:,}  "
        f"StudioModel {c['models_records']:,}  meshes {c['meshes']:,} "
        f"({c['garment_meshes']:,} garment-named)",
        "",
    ]
    for label, rows in (("StudioTexture (20B)", report["texture_slots"]),
                        ("StudioModel (224B)", report["model_slots"]),
                        ("StudioMesh (60B)", report["mesh_slots"])):
        lines.append(f"{label} -- unclaimed slots:")
        for row in rows:
            if not row["nonzero"]:
                lines.append(f"    +{row['offset']:<4d} zero on all {row['records']:,}")
                continue
            g = row["garment_nonzero_rate"]
            o = row["other_nonzero_rate"]
            split = (
                f"  garment {g:6.1%} vs other {o:6.1%}"
                if g is not None and o is not None
                else ""
            )
            common = ", ".join(
                f"{r['value']}x{r['records']}" for r in row["most_common_values"][:4]
            )
            lines.append(
                f"    +{row['offset']:<4d} nonzero {row['nonzero']:7,}/{row['records']:,} "
                f"({row['nonzero_rate']:5.1%})  distinct {row['distinct_values_capped']:2d}"
                f"{split}   [{common}]"
            )
        lines.append("")

    lines.append("slots separating garment from other by more than 10 points:")
    any_row = False
    for label, rows in (("texture", report["texture_slots"]),
                        ("model", report["model_slots"]),
                        ("mesh", report["mesh_slots"])):
        for row in rows:
            sep = row["separation"]
            if sep is None or abs(sep) < 0.10:
                continue
            any_row = True
            lines.append(
                f"    {label} +{row['offset']}: {sep:+.1%}  "
                f"(garment {row['garment_nonzero_rate']:.1%}, "
                f"other {row['other_nonzero_rate']:.1%})"
            )
            for w in report["witnesses"].get(label, {}).get(str(row["offset"]), [])[:4]:
                lines.append(f"        {w}")
    if not any_row:
        lines.append("    none")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--limit", type=int)
    parser.add_argument("--dump", metavar="MODEL")
    parser.add_argument("--carriers", action="store_true")
    parser.add_argument("--raw", nargs=2, metavar=("MODEL", "MATERIAL"))
    parser.add_argument("--analyze", nargs=2, metavar=("MODEL", "MATERIAL"))
    parser.add_argument("--flags", action="store_true")
    parser.add_argument("--maps", nargs=2, metavar=("MODEL", "MATERIAL"))
    parser.add_argument("--anchors", metavar="AUDIT_JSON", type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()

    if args.anchors:
        report = anchors(args.anchors)
        destination = args.report or (research_root() / "mesh-record-anchors.json")
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        for c in report["checks"]:
            if "error" in c:
                print(f"{c['model']}: {c['error']}")
                continue
            print(f"\n{c['model']}  def{c['definition']} -> {c['candidate_mesh']} "
                  f"({c['mesh_vertices']:,} v)  max anchor {c['max_anchor_index']}  "
                  f"join {c['join']}")
            print(f"  particles {c['particles']}  anchored {c['anchored']}  "
                  f"dynamic {c['dynamic']}")
            for label, band in (("anchors", c["anchor_band"]), ("mesh", c["mesh_band"])):
                if band:
                    print(f"  {label:<8s} {band['vertices']:5,} v  "
                          f"x{band['x']} y{band['y']} z{band['z']}")
            if c["anchor_bone_share"]:
                share = "  ".join(f"{r['bone']} {r['share']:.1%}"
                                  for r in c["anchor_bone_share"])
                print(f"  anchor weight: {share}")
        print(f"\n  report: {destination}")
        return 0

    if args.maps:
        report = maps(args.maps[0], args.maps[1])
        destination = args.report or (research_root() / "mesh-record-maps.json")
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        s = report["selector"]
        print(f"{report['model']}::{report['material']}  "
              f"{report['vertices']:,} vertices")
        print(f"\n  selector +48   simulated {s['simulated']:,}   "
              f"skinned(0xFF) {s['skinned_0xFF']:,}")
        print(f"    distinct values {s['distinct'][:16]}")
        print(f"    histogram {s['histogram']}")
        a = report["position_normal_index"]
        b = report["tangent_index"]
        print(f"\n  pos/normal +52  range [{a['min']}, {a['max']}]  "
              f"high-bit set {a['negative_high_bit_set']:,}  "
              f"max magnitude {a['magnitude_max']:,}  "
              f"distinct {a['distinct_magnitudes']:,}")
        print(f"  tangent   +56  range [{b['min']}, {b['max']}]  "
              f"max magnitude {b['magnitude_max']:,}  "
              f"distinct {b['distinct_magnitudes']:,}")
        for label in ("bind_band_all", "bind_band_simulated", "bind_band_skinned"):
            band = report[label]
            if not band:
                print(f"\n  {label[10:]:<10s} none")
                continue
            print(f"\n  {label[10:]:<10s} {band['vertices']:,} verts  "
                  f"x{band['x']} y{band['y']} z{band['z']}")
        print(f"\n  report: {destination}")
        return 0

    if args.flags:
        report = flags(limit=args.limit)
        destination = args.report or (research_root() / "mesh-record-flags.json")
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        c = report["contingency"]
        print("Flags & 0x400 against the +48/+52/+56 payload:")
        print(f"    flag and payload      {c['flag_and_payload']:6,}")
        print(f"    flag, no payload      {c['flag_without_payload']:6,}")
        print(f"    payload, no flag      {c['payload_without_flag']:6,}")
        print(f"    neither               {c['neither']:6,}")
        print("\nbit population across every 2531 model:")
        for row in report["bit_population"]:
            print(f"    {row['bit']:>10s}  {row['models']:6,}")
        print("\nmost common flag words:")
        for row in report["flag_words"][:12]:
            print(f"    {row['flags']:>10s}  {row['models']:6,}")
        if report["flagged_without_payload"]:
            print("\nflagged with no mesh payload:")
            for m in report["flagged_without_payload"][:20]:
                print(f"    {m}")
        if report["payload_without_flag"]:
            print("\npayload with no flag:")
            for m in report["payload_without_flag"][:20]:
                print(f"    {m}")
        print(f"\n  {len(report['cloth_models'])} models in either set")
        print(f"  report: {destination}")
        return 0

    if args.analyze:
        report = analyze(args.analyze[0], args.analyze[1])
        destination = args.report or (research_root() / "mesh-record-analyze.json")
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(f"{report['model']}::{report['material']}  "
              f"{report['vertices']:,} vertices")
        box = report["mesh_bind_box"]
        if box and "x" in box:
            print(f"  bind box  x[{box['x'][0]:.1f},{box['x'][1]:.1f}] "
                  f"y[{box['y'][0]:.1f},{box['y'][1]:.1f}] "
                  f"z[{box['z'][0]:.1f},{box['z'][1]:.1f}]  "
                  f"({box['vertices']:,} deduped)")
        elif box:
            print(f"  bind box  {box.get('error')}")
        for i, b in enumerate(report["blocks"]):
            print(f"\n  block {i + 1}  slot +{MESH_TRIPLE[i]}  at {b['file_offset']:,}"
                  f"  {b['bytes']:,} bytes  ({b['bytes_per_vertex']:.2f}/vertex)")
            print(f"    u8   min {b['u8']['min']} max {b['u8']['max']} "
                  f"distinct {b['u8']['distinct']} zeros {b['u8']['zero_fraction']:.1%}")
            print(f"    u16  max {b['u16']['max']}  "
                  f"below vertex count {b['u16']['fraction_below_vertex_count']:.1%}")
            print(f"    i32  min {b['i32']['min']} max {b['i32']['max']}  "
                  f"in-image {b['i32']['fraction_resolving_in_image']:.1%}")
            f = b["f32"]
            box_frac = ("n/a" if f["fraction_inside_mesh_box"] is None
                        else f"{f['fraction_inside_mesh_box']:.1%}")
            print(f"    f32  finite {f['finite']}/{f['of']} "
                  f"range [{f['min']:.4g}, {f['max']:.4g}]  "
                  f"unit {f['fraction_unit_range']:.1%}  in mesh box {box_frac}")
            mk = b["one_float_markers"]
            print(f"    1.0f markers {mk['count']}  first {mk['first']}  "
                  f"gaps {mk['gap_histogram']}")
            if b["record_words"]:
                print(f"    -> {b['record_count']} records of "
                      f"{b['record_words']} words ({b['record_words'] * 4} bytes)")
                for rec in b["records"]:
                    print(f"       i32 {rec['as_i32']}")
                    print(f"       f32 {rec['as_f32']}")
        print(f"\n  report: {destination}")
        return 0

    if args.raw:
        report = raw(args.raw[0], args.raw[1])
        destination = args.report or (research_root() / "mesh-record-raw.json")
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(f"{report['model']}::{report['material']}  "
              f"{report['vertices']:,} vertices")
        for block in report["blocks"]:
            print(f"\n  +{block['slot']} at {block['file_offset']:,}")
            print(f"    u8   {block['as_u8']}")
            print(f"    i16  {block['as_i16']}")
            print(f"    f32  {block['as_f32']}")
            print(f"    hex  {block['hex'][:128]}")
        print(f"\n  report: {destination}")
        return 0

    if args.carriers:
        report = carriers(limit=args.limit)
        destination = args.report or (research_root() / "mesh-record-carriers.json")
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(f"{report['carrier_meshes']} meshes carry the +48/+52/+56 triple\n")
        for row in report["carriers"]:
            mark = " <garment-named>" if row["garment_named"] else ""
            pv48 = row["per_vertex_48"]
            pv52 = row["per_vertex_52"]
            print(f"  {row['material']:<34s} {row['vertices']:6,}v  "
                  f"b1={row['span_48_52']:>8,} ({pv48:5.2f}/v)  "
                  f"b2={row['span_52_56']:>8,} ({pv52:5.2f}/v)"
                  f"{'' if row['all_three_set'] else '  PARTIAL'}{mark}")
            print(f"      {row['model']}")
        print(f"\n  report: {destination}")
        return 0

    if args.dump:
        report = dump(args.dump)
        destination = args.report or (research_root() / "mesh-record-dump.json")
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(f"{report['model']}  version {report['version']}  {report['size']:,} bytes")
        print(f"\ntextures ({len(report['textures'])}):")
        for t in report["textures"]:
            slots = "  ".join(f"+{o}={v}" for o, v in t["slots"].items() if o != "0")
            mark = " <garment-named>" if t["garment_named"] else ""
            print(f"    {t['index']:3d} {t['name']:<40s} {slots}{mark}")
        for entry in report["models"]:
            print(f"\nStudioModel bp{entry['bodypart']}/m{entry['model']} "
                  f"'{entry['name']}'  {entry['num_vertices']:,} verts, "
                  f"{entry['num_meshes']} meshes")
            live = {o: v for o, v in entry["slots"].items()
                    if v["i"] != 0 and o not in ("136", "140", "144", "148", "156")}
            for o, v in live.items():
                print(f"        +{o:<4s} i={v['i']:<12d} f={v['f']:.6g}")
            for mesh in entry["meshes"]:
                mark = " <garment-named>" if mesh["garment_named"] else ""
                print(f"      mesh {mesh['index']:2d} {mesh['material']:<36s} "
                      f"{mesh['num_vertices']:5,} verts{mark}")
                mlive = {o: v for o, v in mesh["slots"].items()
                         if v["i"] != 0 and o not in ("0", "8", "12")}
                for o, v in mlive.items():
                    print(f"          +{o:<4s} i={v['i']:<12d} f={v['f']:.6g}")
        print(f"\n  report: {destination}")
        return 0

    report = sweep(limit=args.limit)
    destination = args.report or (research_root() / REPORT_NAME)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(summarize(report))
    print(f"\n  report: {destination}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
