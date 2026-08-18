"""Propose a LoadSurvey-compatible advisor JSON for a map's fill lights.

Offline neighborhood pack + conservative rules + optional per-batch verdicts.
Does not read the standing `_lights/<map>.json` survey. Writes:

  $ELYSIUM_EXPORT_ROOT/_lights/<map>.advisor.json
  $ELYSIUM_EXPORT_ROOT/_lights/<map>.advisor.report.json

Apply by copying the advisor file over <map>.json. Usage:

  uv run elysium research propose_hub_lights
  uv run elysium research propose_hub_lights --map sp_tutorial_1
  uv run elysium research propose_hub_lights --verdicts path.json
"""
from __future__ import annotations

import argparse
import json
import math
import sys
from collections import Counter, defaultdict
from datetime import datetime, timezone
from pathlib import Path

import numpy as np

from elysium_pipeline.paths import export_root
from elysium_pipeline.shared_corpus import (
    CUBEMAP_TAG,
    check_manifest,
    check_materials,
    manifest_path,
    materials_path,
)

DEFAULT_MAP = "sm_hub_1"
FALLBACK_RADIUS_CM = 2500.0
SPRITE_KEEP_CM = 100.0
EMIT_KEEP_CM = 80.0
ENTITY_MATCH_CM = 8.0
FILL_MIN_BATCH = 4
DETACH_PERCENTILE = 75
HERO_MAG = 2000.0
WASH_REACH_CM = 1000.0
OUTDOOR_PREFIXES = ("grass/", "ground/", "asphalt/", "sand/")
OUTDOOR_TOKENS = ("sidewalk", "streetd", "street_")
PROTECTED = {"texlight", "styled", "scripted", "sun_sky"}
TYPE_LABEL = {0: "tex", 1: "point", 2: "spot", 3: "sun", 5: "skyamb"}
WINDOW_TOKENS = ("wndw", "window")
SPRITE_FAMILIES = (
    "glowa",
    "candle",
    "volumelight",
    "coplights",
    "streetlight",
    "neon",
)
CALIBRATION = {
    "point_spot_scale": 0.003,
    "max_brightness": 8.0,
    "falloff_exponent": 1.0,
    "radius_scale": 1.0,
    "specular_scale": 0.0,
    "indirect_lighting_scale": 1.0,
    "volumetric_scattering_scale": 1.0,
    "sun_lux_scale": 8.0,
    "sun_source_angle_deg": 0.5357,
    "sun_soft_source_angle_deg": 0.0,
    "point_shadows": True,
    "spot_shadows": True,
    "sun_shadows": True,
}


def _dist(a, b):
    return float(math.sqrt((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2))


def _material_key(name):
    text = (name or "").replace("\\", "/").strip()
    if CUBEMAP_TAG in text:
        text = text.split(CUBEMAP_TAG, 1)[0]
    if text.startswith("MI_"):
        text = text[3:]
    return text.lower()


def _is_window_name(key):
    return any(token in (key or "") for token in WINDOW_TOKENS)


def _sprite_family(tex):
    name = Path(tex).stem.lower()
    for family in SPRITE_FAMILIES:
        if family in name:
            return family
    return "other"


def parse_lights(path):
    rows = []
    for index, line in enumerate(path.read_text(encoding="utf-8").splitlines()):
        if not line.strip():
            continue
        parts = line.split()
        rgb = (float(parts[7]), float(parts[8]), float(parts[9]))
        mag = max(rgb)
        radius = float(parts[10])
        rows.append(
            {
                "index": index,
                "type": int(parts[0]),
                "pos": (float(parts[1]), float(parts[2]), float(parts[3])),
                "dir": (float(parts[4]), float(parts[5]), float(parts[6])),
                "rgb": rgb,
                "mag": mag,
                "radius_cm": radius,
                "reach_cm": radius if radius > 1.0 else FALLBACK_RADIUS_CM,
                "stopdot": float(parts[11]),
                "stopdot2": float(parts[12]),
                "exponent": float(parts[13]),
                "style": int(parts[14]),
                "sky": int(parts[15]),
            }
        )
    return rows


def parse_sprites(path):
    rows = []
    if not path.is_file():
        return rows
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        parts = line.split()
        rows.append(
            {
                "tex": parts[0],
                "pos": (float(parts[1]), float(parts[2]), float(parts[3])),
                "family": _sprite_family(parts[0]),
                "sky": int(parts[-1]) if parts[-1] in ("0", "1") else 0,
            }
        )
    return rows


def parse_props(path):
    rows = []
    if not path.is_file():
        return rows
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        parts = line.split()
        rows.append(
            {
                "stem": parts[0],
                "pos": (float(parts[1]), float(parts[2]), float(parts[3])),
                "sky": int(parts[10]) if len(parts) > 10 else 0,
                "model": parts[11] if len(parts) > 11 else "",
            }
        )
    return rows


def parse_ents(path):
    lights = []
    dynamics = []
    sky_camera = None
    data = json.loads(path.read_text(encoding="utf-8"))
    for entity in data.get("entities", []):
        classname = entity.get("classname", "")
        origin = entity.get("origin") or [0.0, 0.0, 0.0]
        pos = (float(origin[0]), float(origin[1]), float(origin[2]))
        keys = entity.get("keys") or {}
        if classname in ("light", "light_spot"):
            lights.append(
                {
                    "classname": classname,
                    "targetname": entity.get("targetname") or "",
                    "pos": pos,
                    "light": keys.get("_light", ""),
                    "style": keys.get("style", "0"),
                    "distance": keys.get("_distance", ""),
                    "sky": bool(entity.get("sky")),
                }
            )
        elif classname == "prop_dynamic":
            dynamics.append(
                {
                    "stem": entity.get("model_mesh") or "",
                    "pos": pos,
                    "sky": bool(entity.get("sky")),
                    "model": keys.get("model", ""),
                    "targetname": entity.get("targetname") or "",
                }
            )
        elif classname == "sky_camera":
            sky_camera = pos
    return lights, dynamics, sky_camera


def parse_obj_centroids(path):
    if not path.is_file():
        return np.zeros((0, 3), dtype=np.float64), []
    verts = []
    cents = []
    mats = []
    current = ""
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("v "):
            _, x, y, z = line.split()[:4]
            verts.append((float(x), float(y), float(z)))
        elif line.startswith("usemtl "):
            current = _material_key(line.split(None, 1)[1])
        elif line.startswith("f "):
            idxs = []
            for token in line.split()[1:]:
                idxs.append(int(token.split("/", 1)[0]) - 1)
            if len(idxs) < 3 or current.startswith("tools/"):
                continue
            pts = [verts[i] for i in idxs if 0 <= i < len(verts)]
            if len(pts) < 3:
                continue
            cents.append(
                (
                    sum(p[0] for p in pts) / len(pts),
                    sum(p[1] for p in pts) / len(pts),
                    sum(p[2] for p in pts) / len(pts),
                )
            )
            mats.append(current)
    if not cents:
        return np.zeros((0, 3), dtype=np.float64), []
    return np.asarray(cents, dtype=np.float64), mats


def load_corpus(root):
    materials = check_materials(
        json.loads(materials_path(root).read_text(encoding="utf-8"))
    )["materials"]
    manifest = check_manifest(json.loads(manifest_path(root).read_text(encoding="utf-8")))
    return materials, manifest.get("models", {})


def material_flags(materials, key):
    rec = materials.get(key) or materials.get(key.replace("\\", "/"))
    if rec is None:
        return {
            "emissive": False,
            "blend": False,
            "glass": False,
            "window": _is_window_name(key),
        }
    return {
        "emissive": bool(rec.get("emissive")),
        "blend": bool(rec.get("blend")),
        "glass": bool(rec.get("glass")),
        "window": _is_window_name(key),
    }


def stem_flags(materials, models, stem):
    model = models.get(stem)
    if not model:
        return {"emissive": False, "keys": []}
    keys = list((model.get("materials") or {}).values())
    return {
        "emissive": any(material_flags(materials, _material_key(k))["emissive"] for k in keys),
        "keys": keys,
    }


def nearest_rows(origin, rows, *, skip_sky=True, limit=3):
    scored = []
    for row in rows:
        if skip_sky and row.get("sky"):
            continue
        scored.append((_dist(origin, row["pos"]), row))
    scored.sort(key=lambda item: item[0])
    return scored[:limit]


def nearest_world(origin, cents, mats, k=3):
    if cents.shape[0] == 0:
        return []
    delta = cents - np.asarray(origin, dtype=np.float64)
    dist = np.sqrt(np.einsum("ij,ij->i", delta, delta))
    order = np.argpartition(dist, min(k, len(dist) - 1))[:k]
    order = order[np.argsort(dist[order])]
    return [(float(dist[i]), mats[int(i)]) for i in order]


def load_probe(path):
    if not path.is_file():
        return {}
    data = json.loads(path.read_text(encoding="utf-8"))
    return {int(item["index"]): item for item in data.get("lights", [])}


def load_verdicts(path):
    if path is None:
        return {}, {}
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    batches = {str(key): value for key, value in (data.get("batches") or {}).items()}
    indexes = {int(key): value for key, value in (data.get("indexes") or {}).items()}
    return batches, indexes


def batch_id(light):
    rgb = light["rgb"]
    return (
        f"{light['type']}|{light['style']}|"
        f"{rgb[0]:.2f},{rgb[1]:.2f},{rgb[2]:.2f}|"
        f"{light['radius_cm']:.1f}"
    )


def classify_light(light, neigh, detach_cut):
    evidence = []
    if light["type"] in (3, 5):
        return "sun_sky", "sun or skyambient, not a placed fill", [f"type{light['type']}"]
    if light["type"] == 0:
        return "texlight", "texlight is a visible emit-surface", ["type0"]
    if 1 <= light["style"] <= 11:
        return "styled", "animated lightstyle", [f"style{light['style']}"]
    if light["style"] >= 32:
        return "scripted", "switchable lightstyle", [f"style{light['style']}"]
    entity = neigh.get("entity")
    if entity and entity.get("targetname"):
        return "scripted", "named light entity", ["targetname"]

    sprite = neigh.get("sprite")
    if sprite and sprite["dist"] <= SPRITE_KEEP_CM:
        evidence.append(f"sprite:{sprite['family']}@{sprite['dist']:.0f}")
        return "implied_emitter", "corona sprite within 100 cm", evidence

    world = neigh.get("world")
    if world and world["dist"] <= EMIT_KEEP_CM:
        flags = world["flags"]
        if flags["window"] and flags["emissive"] and not flags["blend"]:
            return "shopfront", "opaque emissive window", [f"world:{world['key']}"]
        if flags["glass"] or (flags["blend"] and flags["window"]):
            return "window_spill", "translucent glass nearest", [f"world:{world['key']}"]
        if flags["emissive"]:
            return "fixture", "nearest world surface emits", [f"world:{world['key']}"]

    prop = neigh.get("prop")
    if prop and prop["dist"] <= EMIT_KEEP_CM and prop.get("emissive"):
        return "fixture", "nearest prop material emits", [f"prop:{prop['stem']}"]

    probe = neigh.get("probe")
    if probe:
        if probe.get("near_emissive") and float(probe.get("near_dist", 1e9)) <= EMIT_KEEP_CM:
            surface = probe.get("near_surface") or ""
            key = _material_key(surface)
            flags = {
                "window": _is_window_name(key),
                "blend": "blend" in key or "glass" in key,
                "emissive": True,
            }
            if flags["window"] and not flags["blend"]:
                return "shopfront", "probe nearest surface is a lit window", [surface]
            if flags["blend"]:
                return "window_spill", "probe nearest surface is glass", [surface]
            return "fixture", "probe nearest surface emits", [surface]

    # Owner pass on this map: a hot, wide point with no nearby corona was always wash.
    # Mag>=2000 and reach>=1000 with no sprite<=100 had zero false positives against that pass.
    if (
        light["type"] == 1
        and light["mag"] >= HERO_MAG
        and light["reach_cm"] >= WASH_REACH_CM
        and (sprite is None or sprite["dist"] > SPRITE_KEEP_CM)
    ):
        return (
            "fill",
            "high-energy wide point with no nearby corona",
            [f"mag={light['mag']:.0f}", f"reach={light['reach_cm']:.0f}"],
        )

    # Auto-fill is points only, large copy-paste batches, clearly detached, no keep signal.
    # Do not auto-kill outdoor ground lights (hub has no sky pair) or a point whose
    # nearest surface already emits — those are the eastern-strip / sky-glow cases.
    outdoor = False
    world_emits = False
    if world is not None:
        key = world.get("key") or ""
        flags = world.get("flags") or {}
        world_emits = bool(flags.get("emissive"))
        outdoor = key.startswith(OUTDOOR_PREFIXES) or any(token in key for token in OUTDOOR_TOKENS)
    if (
        light["type"] == 1
        and light["mag"] < HERO_MAG
        and not outdoor
        and not world_emits
        and neigh.get("batch_size", 1) >= FILL_MIN_BATCH
        and neigh.get("near_any", 1e9) >= detach_cut
        and (sprite is None or sprite["dist"] > SPRITE_KEEP_CM * 2)
        and (prop is None or prop["dist"] > EMIT_KEEP_CM * 2)
        and (world is None or world["dist"] > EMIT_KEEP_CM * 2)
    ):
        return (
            "fill",
            "naked detached point in a copy-paste batch",
            [f"near_any={neigh.get('near_any', -1):.0f}", f"batch={neigh.get('batch_size')}"],
        )
    return "review", "no cheap keep or fill rule fired", evidence


def vote_batches(results):
    keep_classes = {
        "fixture",
        "shopfront",
        "texlight",
        "styled",
        "scripted",
    }
    by_batch = defaultdict(list)
    for item in results:
        by_batch[item["batch"]].append(item)
    for members in by_batch.values():
        decided = [m["class"] for m in members if m["class"] in keep_classes]
        if not decided:
            continue
        winner = Counter(decided).most_common(1)[0]
        if winner[1] < (len(decided) + 1) // 2:
            continue
        if len(set(decided)) > 1 and winner[1] == len(decided) / 2:
            continue
        for member in members:
            if member["class"] == "review":
                member["class"] = winner[0]
                member["reason"] = f"batch vote -> {winner[0]}"
                member["evidence"] = list(member["evidence"]) + ["batch_vote"]


def veto_auto_fill(results):
    """A copy-paste batch that includes a real source is not a fill spray.

    Auto-fill (not a model verdict) is withdrawn for every sibling.
    """
    keep_classes = {
        "fixture",
        "implied_emitter",
        "shopfront",
        "texlight",
        "styled",
        "scripted",
    }
    by_batch = defaultdict(list)
    for item in results:
        by_batch[item["batch"]].append(item)
    for members in by_batch.values():
        has_source = False
        for member in members:
            if member["class"] in keep_classes:
                has_source = True
                break
            world = member.get("world") or {}
            flags = world.get("flags") or {}
            if flags.get("emissive"):
                has_source = True
                break
            sprite = member.get("sprite")
            if sprite and sprite["dist"] <= SPRITE_KEEP_CM:
                has_source = True
                break
        if has_source:
            for member in members:
                if member["class"] == "fill" and "model" not in member["evidence"]:
                    member["class"] = "review"
                    member["reason"] = "fill veto: batch has a source sibling"
                    member["evidence"] = list(member["evidence"]) + ["fill_veto"]
        mixed_fill = [
            member
            for member in members
            if member["class"] == "fill" and "model" not in member["evidence"]
        ]
        mixed_rest = [member for member in members if member["class"] != "fill"]
        if mixed_fill and mixed_rest:
            for member in mixed_fill:
                member["class"] = "review"
                member["reason"] = "fill veto: mixed batch, keep unanimous"
                member["evidence"] = list(member["evidence"]) + ["fill_veto_mixed"]


def apply_verdicts(results, batch_verdicts, index_verdicts):
    if not batch_verdicts and not index_verdicts:
        return
    allowed = {
        "fixture",
        "implied_emitter",
        "shopfront",
        "window_spill",
        "fill",
        "mood",
        "keep",
        "texlight",
        "styled",
        "scripted",
    }
    for item in results:
        if item["class"] in PROTECTED:
            continue
        verdict = index_verdicts.get(item["index"]) or batch_verdicts.get(item["batch"])
        if not verdict:
            continue
        klass = verdict.get("class", "keep")
        if klass not in allowed:
            klass = "keep"
        item["class"] = klass
        item["reason"] = verdict.get("notes") or f"model verdict {klass}"
        item["evidence"] = list(item["evidence"]) + ["model"]


def runtime_intensity(light):
    scale = CALIBRATION["point_spot_scale"]
    ceiling = CALIBRATION["max_brightness"]
    return min(light["mag"] * scale, ceiling)


def normalized_color(light):
    mag = light["mag"]
    if mag <= 0:
        return [1.0, 1.0, 1.0]
    return [light["rgb"][0] / mag, light["rgb"][1] / mag, light["rgb"][2] / mag]


def build_edit(light, klass):
    edit = {
        "index": light["index"],
        "row": light["index"],
        "type": TYPE_LABEL.get(light["type"], "?"),
        "disabled": klass in ("fill", "window_spill"),
        "overridden": klass == "mood",
        "mag": light["mag"],
        "radius_cm": light["radius_cm"],
        "style": light["style"],
        "intensity": runtime_intensity(light),
        "pos_cm": list(light["pos"]),
        "color": normalized_color(light),
    }
    if klass == "mood":
        edit["indirect_lighting_scale"] = 0.0
    return edit


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--map", default=DEFAULT_MAP, help="Map folder / sidecar stem")
    parser.add_argument(
        "--verdicts",
        default="",
        help="JSON {batches, indexes}. Default: sibling verdicts for this map, if present",
    )
    args = parser.parse_args(argv)
    map_name = args.map
    default_verdicts = Path(__file__).with_name("propose_hub_lights_verdicts.json")
    if map_name != DEFAULT_MAP:
        default_verdicts = Path(__file__).with_name(f"propose_{map_name}_verdicts.json")
    verdicts_path = args.verdicts or (str(default_verdicts) if default_verdicts.is_file() else "")

    root = export_root()
    world = root / map_name
    lights_path = world / f"{map_name}.lights"
    if not lights_path.is_file():
        raise SystemExit(f"no {lights_path}")

    lights = parse_lights(lights_path)
    sprites = parse_sprites(world / f"{map_name}.sprites")
    props = parse_props(world / f"{map_name}.props")
    ent_lights, dynamics, _sky_camera = parse_ents(world / f"{map_name}.ents")
    props.extend(dynamics)
    cents, mats = parse_obj_centroids(world / f"{map_name}.obj")
    materials, models = load_corpus(root)
    probe_by_index = load_probe(root / "_lights" / f"{map_name}.probe.json")
    batch_verdicts, index_verdicts = load_verdicts(verdicts_path or None)

    batches = defaultdict(list)
    for light in lights:
        if light["sky"] or light["type"] in (3, 5):
            continue
        batches[batch_id(light)].append(light["index"])

    playable = [light for light in lights if not light["sky"] and light["type"] not in (3, 5)]
    near_any = []
    neighborhoods = {}
    for light in playable:
        origin = light["pos"]
        sprite_hits = nearest_rows(origin, sprites)
        prop_hits = nearest_rows(origin, props)
        ent_hits = nearest_rows(origin, ent_lights)
        world_hits = nearest_world(origin, cents, mats)
        sprite = None
        if sprite_hits:
            dist, row = sprite_hits[0]
            sprite = {"dist": dist, "family": row["family"], "tex": row["tex"]}
        prop = None
        if prop_hits:
            dist, row = prop_hits[0]
            flags = stem_flags(materials, models, row["stem"])
            prop = {
                "dist": dist,
                "stem": row["stem"],
                "emissive": flags["emissive"],
                "model": row.get("model", ""),
            }
        world_n = None
        if world_hits:
            dist, key = world_hits[0]
            world_n = {"dist": dist, "key": key, "flags": material_flags(materials, key)}
        entity = None
        if ent_hits and ent_hits[0][0] <= ENTITY_MATCH_CM:
            entity = dict(ent_hits[0][1])
            entity["dist"] = ent_hits[0][0]
        geom = []
        if sprite:
            geom.append(sprite["dist"])
        if prop:
            geom.append(prop["dist"])
        if world_n:
            geom.append(world_n["dist"])
        nearest_geom = min(geom) if geom else 1e9
        near_any.append(nearest_geom)
        neighborhoods[light["index"]] = {
            "sprite": sprite,
            "prop": prop,
            "world": world_n,
            "entity": entity,
            "probe": probe_by_index.get(light["index"]),
            "near_any": nearest_geom,
            "batch": batch_id(light),
            "batch_size": len(batches[batch_id(light)]),
            "band": "low" if origin[2] < -5000 else "main",
        }

    detach_cut = float(np.percentile(np.asarray(near_any, dtype=np.float64), DETACH_PERCENTILE))
    results = []
    for light in playable:
        neigh = neighborhoods[light["index"]]
        klass, reason, evidence = classify_light(light, neigh, detach_cut)
        results.append(
            {
                "index": light["index"],
                "class": klass,
                "reason": reason,
                "evidence": evidence,
                "batch": neigh["batch"],
                "batch_size": neigh["batch_size"],
                "band": neigh["band"],
                "type": light["type"],
                "style": light["style"],
                "mag": light["mag"],
                "reach_cm": light["reach_cm"],
                "pos": list(light["pos"]),
                "near_any": neigh["near_any"],
                "sprite": neigh["sprite"],
                "prop": neigh["prop"],
                "world": {
                    "dist": neigh["world"]["dist"],
                    "key": neigh["world"]["key"],
                    "flags": neigh["world"]["flags"],
                }
                if neigh["world"]
                else None,
                "entity": {
                    "classname": neigh["entity"]["classname"],
                    "targetname": neigh["entity"]["targetname"],
                    "light": neigh["entity"]["light"],
                    "dist": neigh["entity"]["dist"],
                }
                if neigh["entity"]
                else None,
            }
        )

    vote_batches(results)
    veto_auto_fill(results)
    apply_verdicts(results, batch_verdicts, index_verdicts)

    covered = {row["index"] for row in results}
    lights_by = {light["index"]: light for light in lights}
    for idx, verdict in index_verdicts.items():
        if idx in covered:
            continue
        light = lights_by.get(idx)
        if light is None:
            continue
        klass = verdict.get("class", "fill")
        results.append(
            {
                "index": idx,
                "class": klass,
                "reason": verdict.get("notes") or "index verdict",
                "evidence": ["model", "sky" if light["sky"] else "index"],
                "batch": batch_id(light),
                "batch_size": 1,
                "band": "sky" if light["sky"] else "main",
                "type": light["type"],
                "style": light["style"],
                "mag": light["mag"],
                "reach_cm": light["reach_cm"],
                "pos": list(light["pos"]),
                "near_any": -1,
                "sprite": None,
                "prop": None,
                "world": None,
                "entity": None,
            }
        )

    for item in results:
        if item["class"] == "review":
            item["class"] = "keep"
            item["reason"] = item["reason"] + " (default keep)"

    sky_n = sum(1 for light in lights if light["sky"])
    counts = Counter(item["class"] for item in results)
    result_by = {row["index"]: row for row in results}
    edits = []
    for light in lights:
        item = result_by.get(light["index"])
        if item is None:
            continue
        if item["class"] in ("fill", "window_spill", "mood"):
            edits.append(build_edit(light, item["class"]))

    disabled = sum(1 for edit in edits if edit["disabled"])
    overridden = sum(1 for edit in edits if edit["overridden"])
    advisor = {
        "schema": 2,
        "map": map_name,
        "saved_utc": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%f")[:-3] + "Z",
        "source": "propose_hub_lights",
        "counts": {
            "sources": len(lights),
            "disabled": disabled,
            "overridden": overridden,
        },
        "calibration": CALIBRATION,
        "edits": edits,
    }

    leftover = [item for item in results if "default keep" in item["reason"]]
    leftover_batches = []
    grouped = defaultdict(list)
    for item in leftover:
        grouped[item["batch"]].append(item)
    for bid, members in sorted(grouped.items(), key=lambda kv: (-len(kv[1]), kv[0])):
        sample = members[0]
        leftover_batches.append(
            {
                "batch": bid,
                "n": len(members),
                "type": TYPE_LABEL.get(sample["type"], "?"),
                "indices": [m["index"] for m in members],
                "near_any_med": float(np.median([m["near_any"] for m in members])),
                "sprite": sample["sprite"],
                "prop": sample["prop"],
                "world": sample["world"],
                "entity": sample["entity"],
                "band": sample["band"],
                "reason": sample["reason"],
            }
        )

    report = {
        "map": map_name,
        "playable": len(playable),
        "sky_excluded": sky_n,
        "detach_cut_cm": detach_cut,
        "probe": bool(probe_by_index),
        "verdicts": {
            "batches": sorted(batch_verdicts),
            "indexes": sorted(index_verdicts),
        },
        "classes": dict(counts),
        "edits": len(edits),
        "lights": results,
        "leftover_batches": leftover_batches,
    }

    out_dir = root / "_lights"
    out_dir.mkdir(parents=True, exist_ok=True)
    advisor_path = out_dir / f"{map_name}.advisor.json"
    report_path = out_dir / f"{map_name}.advisor.report.json"
    advisor_path.write_text(json.dumps(advisor, indent="\t") + "\n", encoding="utf-8")
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    # Contract checks — fail the process rather than emit a quiet bad file.
    edit_indexes = [edit["index"] for edit in edits]
    playable_indexes = {light["index"] for light in playable}
    sky_indexes = {light["index"] for light in lights if light["sky"]}
    tex_off = [
        item["index"]
        for item in results
        if item["type"] == 0 and item["class"] in ("fill", "window_spill")
    ]
    styled_off = [
        item["index"]
        for item in results
        if 1 <= item["style"] <= 11 and item["class"] in ("fill", "window_spill")
    ]
    errors = []
    unknown = [
        index
        for index in edit_indexes
        if index not in playable_indexes and index not in index_verdicts
    ]
    if unknown:
        errors.append(f"edit index is not playable or verdicited: {unknown}")
    unexpected_sky = [
        index
        for index in edit_indexes
        if index in sky_indexes and index not in index_verdicts
    ]
    if unexpected_sky:
        errors.append(f"skybox light disabled without a verdict: {unexpected_sky}")
    if tex_off:
        errors.append(f"texlight disabled: {tex_off}")
    if styled_off:
        errors.append(f"styled light disabled: {styled_off}")
    if len([row for row in results if row["band"] != "sky"]) != len(playable):
        errors.append("report does not cover every playable light")
    if disabled > 250:
        errors.append(f"fill count {disabled} is too large for a conservative hub pass")
    if errors:
        raise SystemExit("advisor contract failed: " + "; ".join(errors))

    print(f"map {map_name}: {len(lights)} sidecar rows, {len(playable)} playable, {sky_n} sky excluded")
    if verdicts_path:
        print(
            f"  verdicts: {verdicts_path} "
            f"({len(batch_verdicts)} batches, {len(index_verdicts)} indexes)"
        )
    print(f"  detach cut (p{DETACH_PERCENTILE}): {detach_cut:.1f} cm")
    print(f"  classes: {dict(counts)}")
    print(f"  edits: {disabled} disabled, {overridden} mood overrides")
    print(f"  leftover batches (kept by default): {len(leftover_batches)}")
    print(f"  wrote {advisor_path}")
    print(f"  wrote {report_path}")
    if leftover_batches:
        print("  leftover (first 20):")
        for batch in leftover_batches[:20]:
            spr = batch["sprite"]
            spr_s = f"{spr['family']}@{spr['dist']:.0f}" if spr else "-"
            world = batch["world"]
            world_s = f"{world['key']}@{world['dist']:.0f}" if world else "-"
            print(
                f"    n={batch['n']:<3} {batch['type']:<5} {batch['band']:<4} "
                f"near={batch['near_any_med']:.0f} sprite={spr_s} world={world_s} "
                f"batch={batch['batch']}"
            )


if __name__ == "__main__":
    main(sys.argv[1:])
