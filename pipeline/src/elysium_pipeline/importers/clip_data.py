"""Native clip metadata from the GLB sequence records and the staged product inventory."""
from copy import deepcopy
import math
import hashlib
from pathlib import Path

from elysium_pipeline.formats import mdl_skel
from elysium_pipeline.skeletal_stage import payload, sequences
from elysium_pipeline.skeletal_stage.unit import ModelUnit
from elysium_pipeline.formats import eskm


def descriptor(clip):
    """The existing gameplay columns, with explicit absences and native coordinate domains."""
    combo = clip.combo
    return {"activity": clip.activity, "weight": clip.actweight, "flags": clip.flags,
            "frames": clip.frames, "fps": clip.fps, "fade": clip.fade,
            "reach_cm": clip.reach * 2.54 if clip.reach is not None else None,
            "low_reach_cm": clip.low_reach * 2.54 if clip.low_reach is not None else None,
            "blocked_reaction": clip.blocked_reaction or "",
            "swings": payload.unreal_swings(clip.swings),
            "envelopes": payload.unreal_envelopes(clip.envelopes),
            "combo": {"mask": combo.mask, "dodge": combo.dodge, "chain": combo.chain,
                      "chain_alt": combo.chain_alt, "w_open": combo.w_open,
                      "w_close": combo.w_close, "w_hold": combo.w_hold} if combo else None}


def product_metadata(unit, native_clips, role=""):
    """One record per native clip, including grid cells and derived overlay forms."""
    extra, _ = sequences.blend_clip_plan(unit, unit.sequences)
    by_name = {clip.label.casefold(): clip for clip in [*unit.sequences, *extra]}
    raw_positions = {}
    for row in unit.mdl["sequences"]:
        raw_positions.setdefault(row["label"].casefold(), row["index"])
    output = {}
    for native in native_clips:
        if native.flags & 4 and native.base:
            continue
        name = native.name
        source_name = name.rsplit("@", 1)[0] if native.base else name
        source = by_name.get(source_name.casefold())
        if source is None:
            raise ValueError(f"{unit.id}: no semantic source for native clip {name}")
        meta = descriptor(source)
        meta["flags"], meta["frames"] = native.flags, native.frames
        row = [0, 0, meta["weight"], meta["flags"], meta["frames"], meta["fps"], meta["fade"],
               meta["reach_cm"], meta["blocked_reaction"], meta["swings"], meta["combo"],
               meta["low_reach_cm"], meta["envelopes"]]
        event_table = mdl_skel.event_table([source])
        movement_table = mdl_skel.movement_table([source])
        events = event_table.get("events", {}).get(source.label, [])
        movement = movement_table.get("movement", {}).get(source.label, [])
        duration = (native.frames - 1) / source.fps if native.frames > 1 and source.fps > 0 else 0.
        distance = math.sqrt(sum(value * value for value in source.movement[-1].position)) * 2.54 if source.movement else 0.
        output[name] = {
            "schemaVersion": "1.0.0", "assetId": unit.id, "ownerRoot": role, "label": name, "sourceLabel": source.label,
            "slice": {"owners": [unit.id], "activities": [source.activity], "clips": {name: [row]},
                      "seq": {name: [raw_positions.get(source.label.casefold(), -1)]}},
            "timelines": {"grids": {}, "event_options": event_table.get("event_options", []),
                          "events": {name: events}, "movement_fields": movement_table["movement_fields"],
                          "movement": {name: movement}},
            "counts": {"swings": len(meta["swings"]), "envelopes": len(meta["envelopes"]),
                       "events": len(events), "movement": len(movement), "combo": meta["combo"] is not None,
                       "knockbacks": [[len(bucket) for bucket in swing["kb_names"]] for swing in meta["swings"]]},
            "cycleSeconds": duration, "groundDistanceCm": distance,
            "groundSpeedCmPerSecond": distance / duration if duration else 0., "axes": [],
        }
    return output


def grid_metadata(unit, grids, native_metadata, role=""):
    """Grid-level descriptor/timelines plus its pose-parameter names and wrap declarations."""
    by_name = {clip.label.casefold(): clip for clip in unit.sequences}
    hosts = {}
    for clip in unit.sequences:
        for layer in clip.autolayers:
            hosts.setdefault(layer.removeprefix("@"), set()).add(clip.label)
    output = {}
    for label, grid in grids.items():
        source = by_name[label.casefold()]
        for host in sorted(hosts.get(label, {""})):
            name = label + ("@" + host if host else "")
            available = [cell["clip"] + ("@" + host if host else "") for cell in grid["cells"] if cell["clip"]]
            available = [key for key in available if key in native_metadata]
            if len(available) < 2:
                continue
            # The logical sequence describes the grid; a cell only supplies its payload header.
            from types import SimpleNamespace
            record = product_metadata(unit, [SimpleNamespace(name=label, base="", flags=source.flags, frames=source.frames)], role)[label]
            record = deepcopy(record)
            record["label"] = name
            for section, key in (("slice", "clips"), ("slice", "seq"), ("timelines", "events"), ("timelines", "movement")):
                record[section][key] = {name: record[section][key][label]}
            record["axes"] = [dict(unit.mdl["poseParameters"][index]) for index in grid["paramindex"] if index >= 0]
            record["timelines"]["grids"] = {label: deepcopy(grid)}
            record["timelines"]["pose_parameters"] = deepcopy(unit.mdl["poseParameters"])
            output[name] = record
    return output


def stage_clip_data(export_root, stage_root, manifest, log=print):
    """Stage complete clip metadata per owner; all product files are digest-bound to the manifest."""
    from elysium_pipeline.importers.characters import _write, _json_bytes

    selected = set(manifest["selectedUnits"])
    count = 0
    for entry in manifest["assets"]:
        if entry["assetId"] not in selected:
            continue
        try:
            unit = ModelUnit.metadata(Path(export_root) / entry["unitGlb"])
            _, grids = sequences.blend_clip_plan(unit, unit.sequences)
            owners = ([entry] if entry.get("nativeMainOwner", True) else []) + entry.get("actors", [])
            for owner in owners:
                if not owner.get("animationSkeletonAsset"):
                    continue
                role = owner.get("root", "")
                source = Path(stage_root) / owner.get("payload", entry["payload"])
                blob = source.read_bytes()
                expected = owner.get("payloadSha256", entry["recipe"]["payloadSha256"])
                if hashlib.sha256(blob).hexdigest() != expected:
                    raise ValueError("native payload changed after staging")
                records = product_metadata(unit, eskm.clip_payloads(blob), role)
                spaces = grid_metadata(unit, grids, records, role)
                relative = entry["key"] + ("/" + role if role else "") + ".clips.json"
                data = _json_bytes({"schemaVersion":"1.0.0", "assetId":unit.id, "ownerRoot":role,
                                    "clips":records,"blendSpaces":spaces})
                _write(Path(stage_root) / relative, data)
                owner["clipData"] = relative
                owner["clipDataSha256"] = hashlib.sha256(data).hexdigest()
                count += len(records) + len(spaces)
        except (ValueError, KeyError, OSError, IndexError) as exc:
            manifest["stageFailures"].append({"assetId":entry["assetId"],"reason":"clip data: " + str(exc)})
    _write(Path(stage_root) / "manifest.json", _json_bytes(manifest))
    log(f"character clip data: {count} projected products, {len(manifest['stageFailures'])} stage failures")
    return manifest
