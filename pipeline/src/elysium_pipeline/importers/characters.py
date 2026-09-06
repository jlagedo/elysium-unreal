"""Stage native skeletal payloads from model units, with an explicit preservation inventory."""
from __future__ import annotations

import hashlib
import json
import os
from functools import lru_cache
from pathlib import Path

from elysium_pipeline.asset_paths import baked_unit, assert_unique_paths, validate_landing
from elysium_pipeline.formats import eskm, mdl_skel
from elysium_pipeline.formats.model_glb.model import MODEL_EXTENSION, plain
from elysium_pipeline.skeletal_stage import payload, sequences
from elysium_pipeline.skeletal_stage import cinematics
from elysium_pipeline.skeletal_stage import families
from elysium_pipeline.skeletal_stage import wield
from elysium_pipeline.skeletal_stage.geometry import geometry
from elysium_pipeline.skeletal_stage.unit import ModelUnit, read_document, SkeletalUnitError
from elysium_pipeline.placed_models import rest_candidates
from elysium_pipeline.model_usage import skeletal_candidate, discover_placed_animation_models
from elysium_pipeline import ornament_models

#: The code half of every staged character entry: bump when a stage rule changes what it emits.
#: The data half is the unit's sha256 and the bank/wield inputs. Code is never hashed
#: (`docs/architecture/seam_map_unit_contract.md` -> "Recipes").
SETTINGS_VERSION = "elysium-character-stage-v2"


@lru_cache(maxsize=1)
def staging_root(work_root):
    return Path(work_root) / "import" / "characters"


def _write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.is_file() and path.read_bytes() == data:
        return
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_bytes(data)
    os.replace(temporary, path)


def _json(path, value):
    _write(path, _json_bytes(value))


def _json_bytes(value):
    return json.dumps(plain(value), ensure_ascii=False, allow_nan=False,
                      separators=(",", ":")).encode("utf-8")


def _cached_entry(destination, key, input_recipe):
    path = destination / (key + ".provenance.json")
    if not path.is_file():
        return None
    try:
        previous = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    if not isinstance(previous, dict):
        return None
    entry = previous.get("stageEntry")
    if not isinstance(entry, dict) or not isinstance(entry.get("recipe"), dict):
        return None
    if any(entry["recipe"].get(k) != v for k, v in input_recipe.items()):
        return None
    try:
        products = [(entry["payload"], entry["recipe"]["payloadSha256"]),
                    (entry["body"], entry["recipe"]["bodySha256"])]
        products.extend((a["payload"], a["payloadSha256"]) for a in entry.get("actors", ()))
        if entry.get("wieldSkeletonSource"):
            products.append((entry["wieldSkeletonSource"], entry["recipe"]["wieldRigSha256"]))
    except (KeyError, TypeError):
        return None
    for relative, expected in products:
        if not isinstance(relative, str):
            return None
        product = (destination / relative).resolve()
        if not product.is_relative_to(destination.resolve()):
            return None
        if not product.is_file() or hashlib.sha256(product.read_bytes()).hexdigest() != expected:
            return None
    return {**entry, "reused": True}


def stage_unit(source, destination, *, mesh=True, content_root=None, cinematic=False, bank_owners=None, wield_bodies=None):
    """One unit, one skeleton/clip owner; its full source semantics accompany the projection."""
    unit = ModelUnit(source)
    baked_unit(unit.id, "SKEL")  # validate the key before consulting any derived file
    key = unit.id.removeprefix("vtmb:model:")
    destination = Path(destination)
    owners = [value for (id, _), value in (bank_owners or {}).items() if id == unit.id]
    input_recipe = {"settings": SETTINGS_VERSION,
                    "unitSha256": unit.digest, "mesh": mesh, "cinematic": cinematic,
                    "wieldBodyTrees": hashlib.sha256(_json_bytes(wield_bodies)).hexdigest() if wield_bodies is not None else None,
                    "bankOwners": sorted(owners, key=lambda row: row["root"])}
    cached = _cached_entry(destination, key, input_recipe)
    if cached is not None:
        return cached
    rows, bone_map, reparented = payload.unreal_bones(unit.bones)
    if not rows:
        raise SkeletalUnitError(f"{unit.id}: no skeleton; belongs to the static model lane")
    wield_data = wield.describe(unit, wield_bodies, rows, bone_map, reparented) if wield_bodies is not None else None
    wield_rig = None
    if wield_data:
        posed_rows = payload._ref_pose_rows(rows, bone_map, wield_data["referencePoseSourceLocals"], unit.id, reparented)
        wield_rig = payload._assemble([(b"SKEL", payload._skel_section(posed_rows))])
    extra_attachments = []
    if wield_data and wield_data["trailTipSource"]:
        tip = wield_data["trailTipSource"]
        bone = next(b.index for b in unit.bones if b.name == tip["bone"])
        extra_attachments.append(mdl_skel.Attachment("TrailTip", 0, bone, tip["position"], tip["rotation"]))
    extra, grids = sequences.blend_clip_plan(unit, unit.sequences)
    forced_rest = ([clip.label for clip in rest_candidates(unit.sequences)
                    if not any(unit.bone_weights(clip.base))]
                   if mesh and unit.extension["vtx"]["lods"] else [])
    masks = {}
    animations, clip_count = payload._anim_section(
        unit, unit.bones, unit.sequences + extra, bone_map, len(rows), masks,
        reparented=reparented, ensure_labels=forced_rest)
    mesh_bytes, morph_bytes, names, bindings, vertex_map, tangent_bytes = (
        geometry(unit, bone_map) if mesh else (b"", b"", [], [], [], b""))
    sections = [(b"SKEL", payload._skel_section(rows))]
    if mesh:
        sections.extend([(b"ATCH", payload._attachment_section(unit, bone_map, extra_attachments)),
                         (b"MATL", payload._matl_section(names, {}) if names else b""),
                         (b"MESH", mesh_bytes), (b"MORF", morph_bytes), (b"TANG", tangent_bytes)])
    sections.extend([(b"MASK", payload._mask_section(masks, len(rows))), (b"ANIM", animations)])
    compiled = payload._assemble(sections)
    clips = eskm.clip_payloads(compiled)
    actor_products = cinematics.actor_payloads(unit) if cinematic else []
    native_main = not actor_products or bool(mesh_bytes)
    aggregate_only = (not mesh_bytes and not clip_count and bool(unit.mdl["includeModels"])
                      and not unit.mdl["localAnimations"] and not unit.mdl["sequences"])
    main_family = (bank_owners or {}).get((unit.id, ""))
    own_skeleton = native_main and not aggregate_only and (bool(mesh_bytes) or not main_family)
    assets = [(unit.id + ":skeleton", baked_unit(unit.id, "SKEL"))] if own_skeleton else []
    if mesh_bytes:
        assets.append((unit.id + ":mesh", baked_unit(unit.id, "SK")))
    if native_main:
        assets.extend((unit.id + ":clip:" + clip.name, baked_unit(unit.id, "A", label=clip.name)) for clip in clips)
    actors = []
    for root, data in actor_products:
        actor_clips = eskm.clip_payloads(data)
        family = (bank_owners or {}).get((unit.id, root))
        actor = {"root": root, "payload": f"{key}/{root}.skel", "clipCount": len(actor_clips),
                 "skeletonAsset": None if family else baked_unit(unit.id, "SKEL", role=root),
                 "animationSkeletonAsset": family["skeletonAsset"] if family else baked_unit(unit.id, "SKEL", role=root),
                 "bankFamilyTreeSha256": family["treeSha256"] if family else None,
                 "animationAssets": [baked_unit(unit.id, "A", role=root, label=c.name) for c in actor_clips],
                 "payloadSha256": hashlib.sha256(data).hexdigest()}
        actors.append(actor)
        if actor["skeletonAsset"]:
            assets.append((unit.id + ":actor-skeleton:" + root, actor["skeletonAsset"]))
        assets.extend((unit.id + ":actor-clip:" + root + ":" + clip.name, path)
                      for clip, path in zip(actor_clips, actor["animationAssets"]))
    assert_unique_paths(assets)
    if content_root is not None:
        for _, package in assets:
            validate_landing(package, content_root)
    row = {
        "assetId": unit.id, "key": key, "unitGlb": f"models/{key}.glb",
        "payload": key + ".skel", "body": key + ".body.json", "provenance": key + ".provenance.json",
        "aggregateOnly": aggregate_only,
        "nativeMainOwner": native_main,
        "cinematic": cinematic, "actors": actors,
        "skeletonAsset": baked_unit(unit.id, "SKEL") if own_skeleton else None,
        "animationSkeletonAsset": (main_family["skeletonAsset"] if main_family else baked_unit(unit.id, "SKEL")) if native_main and not aggregate_only else None,
        "meshAsset": baked_unit(unit.id, "SK") if mesh_bytes else None,
        "animationAssets": [path for identity, path in assets if ":clip:" in identity],
        "clipCount": clip_count, "materials": bindings,
        "wieldBinding": wield_data["binding"] if wield_data else None,
        "wieldSkeletonSource": key + ".wield.rig.skel" if wield_rig else None,
        "recipe": {**input_recipe,
                   "bankFamilyTreeSha256": main_family["treeSha256"] if main_family else None,
                   "wieldRigSha256": hashlib.sha256(wield_rig).hexdigest() if wield_rig else None,
                   "payloadSha256": hashlib.sha256(compiled).hexdigest()},
    }
    body = {
        "schemaVersion": "1.0.0", "assetId": unit.id,
        "sequences": [plain(clip) for clip in unit.sequences],
        "discardedSequenceDescriptors": unit.dropped_sequences,
        "forcedRestClips": forced_rest,
        "wield": wield_data,
        "grids": grids,
        "events": mdl_skel.event_table(unit.sequences),
        "movement": mdl_skel.movement_table(unit.sequences),
        "renderVertexMap": vertex_map,
        "skinFamilies": unit.extension["materialBindings"]["skinFamilies"],
        "cinematicRoots": ([{"root": a["root"], "ownerRoot": a["root"]} for a in actors]
                            if actors else [{"root": root, "ownerRoot": ""}
                                             for root in (mdl_skel.cinematic_roots(unit.bones) or [None])]
                            if cinematic and clip_count else []),
        # Source records stay in their declared domains; runtime projections are authored by
        # dedicated writers. Keeping this complete avoids guessing which unused fields matter.
        "sourceSemantics": {name: unit.extension[name] for name in (
            "mdl", "facial", "procedural", "secondaryMotion", "cloth", "physics")},
    }
    provenance = {"assetId": unit.id, "producer": "characters", "recipe": row["recipe"],
                  "sourceResolution": unit.extension["sourceResolution"],
                  "coverage": unit.extension["coverage"],
                  "projection": {"bones": len(rows), "clips": clip_count, "mesh": bool(mesh_bytes),
                                 "lodsRetainedInUnit": [r["index"] for r in unit.extension["vtx"]["lods"]],
                                 "materialSlots": bindings, "discardedSequences": unit.dropped_sequences}}
    body_bytes = _json_bytes(body)
    row["recipe"]["bodySha256"] = hashlib.sha256(body_bytes).hexdigest()
    provenance["stageEntry"] = row
    provenance_bytes = _json_bytes(provenance)
    # Derivation/serialization finish before atomic per-file writes. The caller publishes the
    # manifest entry only after all writes succeed; cached files are always digest-checked.
    _write(destination / row["payload"], compiled)
    if wield_rig:
        _write(destination / row["wieldSkeletonSource"], wield_rig)
    for actor, (_, data) in zip(actors, actor_products):
        _write(destination / actor["payload"], data)
    _write(destination / row["body"], body_bytes)
    _write(destination / row["provenance"], provenance_bytes)
    return row


def stage_characters(export_root, destination, *, bodies=None, content_root=None, log=print):
    """Inventory every model, then stage the chosen skeletal units plus their include closure."""
    root, destination = Path(export_root), Path(destination)
    units, failures = {}, []
    body_trees = {}
    ornament_requests = []
    for path in sorted((root / "models").rglob("*.glb")):
        document = read_document(path)
        extension = document["extensions"][MODEL_EXTENSION]
        identity = extension["identity"]
        if identity["asset"] in units:
            raise SkeletalUnitError(f"duplicate model unit {identity['asset']}")
        ornament_requests.extend(
            ornament_models.collect_requests(extension["mdl"]["sequences"], identity["asset"]))
        units[identity["asset"]] = {
            "path": path, "identity": identity,
            "boneCount": len(extension["mdl"]["bones"]),
            "includes": [row["asset"] for row in extension["mdl"]["includeModels"]],
            "physics": bool(extension.get("physics")),
            "cloth": bool((extension.get("cloth") or {}).get("garments")),
        }
        if identity["family"] == "character" and extension["mdl"]["bones"]:
            bones = extension["mdl"]["bones"]
            body_trees[identity["asset"]] = {
                b["name"].lower(): bones[b["parent"]]["name"].lower() if b["parent"] >= 0 else "" for b in bones}
    if not units:
        raise SkeletalUnitError(f"no model GLBs under {root}")
    cinematic_sets = cinematics.discover_sets(root)
    wield_catalogue = wield.discover(root, units)
    if wield_catalogue["failures"]:
        raise SkeletalUnitError(f"wield unit closure is incomplete: {wield_catalogue['failures']}")
    wield_models = set(wield_catalogue["modelIds"])
    # 4100/4102 ornaments (`ornament_models`): the event's own options string is the only place
    # these models are demanded from -- no entity, item or include edge reaches them.
    ornaments = ornament_models.resolve(ornament_requests, published_ids=units)
    ornament_ids = set(ornaments["modelIds"])
    if ornaments["sourceGaps"]:
        log(f"ornament source gaps: {len(ornaments['sourceGaps'])} animation events name a model "
            f"absent from the published source; retained in the catalogue")
    if wield_catalogue["sourceGaps"]:
        log(f"wield source gaps: {len(wield_catalogue['sourceGaps'])} authored references have no install member; retained in the catalogue")
    missing_sets = set(cinematic_sets) - units.keys()
    if missing_sets:
        raise SkeletalUnitError(f"cinematic source units are missing: {sorted(missing_sets)}")
    partition, rig_payloads = families.build(units, cinematic_sets)
    bank_owners = {(row["assetId"], row["root"]): row for row in partition["owners"]}
    for relative, data in rig_payloads.items():
        _write(destination / relative, data)
    log(f"bank partition: {len(partition['owners'])} owners, {len(partition['families'])} complete families")
    selected = set()
    if bodies:
        for selector in bodies:
            candidates = [id for id in units if selector in (
                id, id.removeprefix("vtmb:model:"), id.rsplit("/", 1)[-1])]
            if len(candidates) != 1:
                raise SkeletalUnitError(f"body {selector!r} resolves to {len(candidates)} units: {candidates}")
            selected.add(candidates[0])
    else:
        selected = {id for id, row in units.items()
                    if skeletal_candidate(row["identity"], row["boneCount"], has_cloth=row["cloth"])}
        selected.update(discover_placed_animation_models(root, units))
        selected.update(cinematic_sets)
        selected.update(wield_models)
        selected.update(ornament_ids)
    pending = list(selected)
    while pending:
        id = pending.pop()
        for target in units[id]["includes"]:
            if target not in units:
                raise SkeletalUnitError(f"{id}: missing include {target}")
            if target not in selected:
                selected.add(target)
                pending.append(target)
    assets = []
    for index, id in enumerate(sorted(selected)):
        try:
            row = units[id]
            roles = set(row["identity"].get("roles", ()))
            cinematic = id in cinematic_sets
            # An ornament is spawned as a visible prop; it always needs its own mesh.
            bank = (roles == {"include-only"} or (cinematic and not roles.intersection({"character-body", "placed-prop"}))) and id not in ornament_ids
            assets.append(stage_unit(row["path"], destination, mesh=not bank,
                                     content_root=content_root, cinematic=cinematic, bank_owners=bank_owners,
                                     wield_bodies=body_trees if id in wield_models else None))
        except (ValueError, KeyError, IndexError, OSError) as exc:
            failures.append({"assetId": id, "reason": str(exc)})
        state = "FAILED" if failures and failures[-1]["assetId"] == id else "reused" if assets[-1].get("reused") else "staged"
        log(f"[{index+1}/{len(selected)}] {id}: {state}")
    products = []
    if bodies and (destination / "manifest.json").is_file():
        previous = json.loads((destination / "manifest.json").read_text(encoding="utf-8"))
        if previous.get("producer") != "characters" or previous.get("packageRoot") != "/ElysiumBaked/Models":
            raise SkeletalUnitError("previous character manifest belongs to a different producer/root")
        replaced = {row["assetId"] for row in assets}
        assets.extend(row for row in previous["assets"] if row["assetId"] not in replaced)
    for row in assets:
        for path in [row["skeletonAsset"], row["meshAsset"], *row["animationAssets"]]:
            if path:
                products.append((row["assetId"], path))
        for actor in row.get("actors", ()):
            for path in [actor["skeletonAsset"], *actor["animationAssets"]]:
                if path:
                    products.append(((row["assetId"], actor["root"]), path))
    for family in partition["families"]:
        products.append((tuple((m["assetId"], m["root"]) for m in family["members"]), family["skeletonAsset"]))
    assert_unique_paths(products)
    manifest = {
        "schemaVersion": "1.0.0", "producer": "characters", "packageRoot": "/ElysiumBaked/Models",
        "pruneScope": None, "assets": assets, "stageFailures": failures,
        "selectedUnits": sorted(selected),
        "cinematicSets": cinematic_sets,
        "bankPartition": partition,
        "wieldCatalogue": wield_catalogue,
        "ornamentDemand": ornaments,
        "inventory": [{"assetId": id, "shape": row["identity"]["shape"],
                       "roles": row["identity"].get("roles", []), "physics": row["physics"], "cloth": row["cloth"],
                       "selected": id in selected,
                       "reason": "selected" if id in selected else "outside-selected-skeletal-lane"}
                      for id, row in units.items()],
    }
    _json(destination / "manifest.json", manifest)
    return manifest
