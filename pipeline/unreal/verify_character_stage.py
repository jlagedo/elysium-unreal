"""Read saved R8 native core products in a fresh editor; no legacy export dependency."""
import json
from pathlib import Path
import struct
import time

import unreal
from pipeline.unreal import _bootstrap  # noqa: F401
from pipeline.unreal import bake_lib as bl
from pipeline.unreal.import_characters import argument, _check_file, _material_path, blend_plan
from elysium_pipeline.asset_paths import baked_unit
from elysium_pipeline.formats import eskm


def morph_names(blob):
    where = eskm.directory(blob).get(b"MORF")
    if where is None:
        return []
    at, size = where
    end = at + size
    count = struct.unpack_from("<I", blob, at)[0]
    at += 4
    names = []
    for _ in range(count):
        name, at = eskm._string(blob, at)
        names.append(name)
        rows = struct.unpack_from("<I", blob, at)[0]
        at += 4 + rows * 28
    if at != end:
        raise ValueError("invalid staged morph record sizes")
    return names


def package(asset):
    return asset.get_path_name().split(".", 1)[0] if asset else None


def load(path, evidence):
    asset = unreal.load_asset(path)
    if asset is None:
        raise ValueError("native asset is absent: " + path)
    if not bl.stored_recipe(path, producer="characters"):
        raise ValueError("native asset has no character recipe: " + path)
    records = [r for r in asset.get_editor_property("asset_user_data")
               if r and r.get_class().get_name() == "ElysiumCharacterProvenance"]
    if len(records) != 1:
        raise ValueError("native asset has no unique provenance: " + path)
    for key, value in evidence.items():
        if records[0].get_editor_property(key) != value:
            raise ValueError("native provenance mismatch: %s %s" % (path, key))
    return asset


def run(manifest_path, material_root):
    started = time.time()
    root = Path(manifest_path).parent
    manifest = json.loads(Path(manifest_path).read_text())
    if manifest.get("producer") != "characters" or manifest["stageFailures"]:
        raise ValueError("invalid or failed character stage")
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(
        [manifest["packageRoot"], "/ElysiumBaked/Materials"], force_rescan=True)
    report = {"scope": "native-core-products", "meshes": 0, "meshData": 0, "clipData": 0, "bodyData": 0, "skeletons": 0, "clips": 0,
              "blendSpaces": 0, "failed": [], "geometryAndAnimationSamplesCompared": False,
              "renderedAcceptance": False}
    report["rawAnimationSamples"] = {"scope": "retained staged source tracks in saved editor data models",
                                     "owners": 0, "tracks": 0, "keys": 0, "declaredOmittedTracks": 0,
                                     "compressedPoseCompared": False, "addedBindTracksCompared": False}
    def checkpoint():
        report["seconds"] = round(time.time() - started, 2)
        (root / "native_verify_report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    checkpoint()
    lib = unreal.ElysiumCharacterBakeLibrary
    if manifest.get("castData"):
        try:
            cast = manifest["castData"]
            _, data = _check_file(root, cast["file"], cast["sha256"])
            asset = unreal.load_asset(cast["assetPath"])
            if asset is None or not bl.stored_recipe(cast["assetPath"], producer="characters"):
                raise ValueError("cast table or recipe is absent")
            error = unreal.ElysiumCastData.verify(asset, data.decode("utf-8"))
            if error:
                raise ValueError(error)
            report["castData"] = True
        except Exception as exc:
            report["failed"].append({"assetId":"cast-corpus","reason":str(exc)})
    selected = set(manifest["selectedUnits"])
    entries = [e for e in manifest["assets"] if e["assetId"] in selected]
    families = {e.get("animationSkeletonAsset") for e in entries}
    families.update(a["animationSkeletonAsset"] for e in entries for a in e.get("actors", ()))
    for family in manifest["bankPartition"]["families"]:
        if family["skeletonAsset"] not in families:
            continue
        try:
            skeleton = load(family["skeletonAsset"], {"bank_family_tree_sha256": family["treeSha256"]})
            if lib.skeleton_bone_count(skeleton) != family["boneCount"]:
                raise ValueError("family skeleton bone count differs from the whole partition")
            report["skeletons"] += 1
        except Exception as exc:
            report["failed"].append({"assetId": family["skeletonAsset"], "reason": str(exc)})
    for entry in entries:
        id = entry["assetId"]
        try:
            _, blob = _check_file(root, entry["payload"], entry["recipe"]["payloadSha256"])
            _, body_blob = _check_file(root, entry["body"], entry["recipe"]["bodySha256"])
            body = json.loads(body_blob)
            evidence = {"asset_id": id, "unit_sha256": entry["recipe"]["unitSha256"]}
            if entry["skeletonAsset"]:
                skeleton = load(entry["skeletonAsset"], evidence)
                if lib.skeleton_bone_count(skeleton) != len(eskm.bones(blob)):
                    raise ValueError("singleton skeleton lost source bones")
                report["skeletons"] += 1
            if entry["meshAsset"]:
                mesh = load(entry["meshAsset"], {**evidence, "payload_sha256": entry["recipe"]["payloadSha256"]})
                if entry.get("meshData"):
                    _, mesh_data = _check_file(root, entry["meshData"], entry["recipe"]["meshDataSha256"])
                    error = unreal.ElysiumCharacterProvenance.verify_mesh_data(mesh, mesh_data.decode("utf-8"))
                    if error:
                        raise ValueError(error)
                    report["meshData"] += 1
                if package(mesh.get_editor_property("skeleton")) != entry["skeletonAsset"]:
                    raise ValueError("mesh binds the wrong skeleton")
                expected_bones = [name.casefold() for name, _ in eskm.bones(blob)]
                if [str(name).casefold() for name in lib.mesh_bones(mesh)] != expected_bones:
                    raise ValueError("mesh bone names/order differ from stage")
                rows = eskm.bone_locals(blob)
                if body.get("wield"):
                    rows = [(b["name"], b["parent"], b["position"], b["rotation"]) for b in body["wield"]["referencePose"]]
                for name, parent, position, rotation in rows:
                    local = lib.ref_pose_bone_transform(mesh, unreal.Name(name))
                    if isinstance(local, tuple):
                        ok, local = local
                        if not ok:
                            raise ValueError("reference bone is absent: " + name)
                    actual_pos = (local.translation.x, local.translation.y, local.translation.z)
                    actual_rot = (local.rotation.x, local.rotation.y, local.rotation.z, local.rotation.w)
                    pos_error = max(abs(a-b) for a,b in zip(actual_pos, position))
                    rot_error = min(max(abs(a-sign*b) for a,b in zip(actual_rot, rotation)) for sign in (1, -1))
                    if pos_error > 1e-4 or rot_error > 1e-6:
                        raise ValueError("reference transform differs: %s position=%g rotation=%g" % (name, pos_error, rot_error))
                wanted = {m["slot"].casefold(): _material_path(m["assetId"], Path(material_root)) for m in entry["materials"]}
                actual = {str(m.material_slot_name).casefold(): package(m.material_interface) for m in mesh.get_editor_property("materials")}
                if actual != wanted:
                    raise ValueError("mesh material slot bindings differ from material routing")
                morphs = mesh.get_editor_property("morph_targets")
                if {m.get_name().casefold() for m in morphs} != {n.casefold() for n in morph_names(blob)}:
                    raise ValueError("mesh morph inventory differs from staged source")
                for morph in morphs:
                    if not lib.skeleton_has_morph_curve(mesh.get_editor_property("skeleton"), morph.get_name()):
                        raise ValueError("morph curve metadata missing: " + morph.get_name())
                report["meshes"] += 1
            owners = ([entry] if entry.get("nativeMainOwner", True) else []) + entry.get("actors", [])
            for owner in owners:
                role = owner.get("root", "")
                if owner.get("bodyData"):
                    _, vocabulary = _check_file(root, owner["bodyData"], owner["bodyDataSha256"])
                    asset = unreal.load_asset(owner["bodyDataAsset"])
                    if asset is None or not bl.stored_recipe(owner["bodyDataAsset"], producer="characters"):
                        raise ValueError("body vocabulary asset or recipe is absent")
                    error = unreal.ElysiumBodyData.verify(asset, vocabulary.decode("utf-8"))
                    if error:
                        raise ValueError(error)
                    report["bodyData"] += 1
                skeleton = owner.get("animationSkeletonAsset", entry["animationSkeletonAsset"])
                if not skeleton:
                    continue
                relative = owner.get("payload", entry["payload"])
                digest = owner.get("payloadSha256", entry["recipe"]["payloadSha256"])
                _, owner_blob = _check_file(root, relative, digest)
                clips = [c for c in eskm.clip_payloads(owner_blob) if not (c.flags & 4 and c.base)]
                metadata = None
                if owner.get("clipData"):
                    _, metadata_blob = _check_file(root, owner["clipData"], owner["clipDataSha256"])
                    metadata = json.loads(metadata_blob)
                native_sequences, declared_omissions = {}, None
                for clip in clips:
                    path = baked_unit(id, "A", role=role or None, label=clip.name)
                    sequence = load(path, {**evidence, "owner_root": role, "payload_sha256": digest})
                    provenance = next(r for r in sequence.get_editor_property("asset_user_data")
                                      if r and r.get_class().get_name() == "ElysiumCharacterProvenance")
                    receipt = json.loads(provenance.get_editor_property("authoring_evidence"))
                    omitted = sorted(receipt.get("suppressedAppendix", {}).get("bones", []))
                    if declared_omissions is not None and omitted != declared_omissions:
                        raise ValueError("owner clips disagree on dormant-donor omissions: " + path)
                    declared_omissions = omitted
                    native_sequences[clip.name] = sequence
                    if metadata is not None:
                        error = unreal.ElysiumClipData.verify(sequence, json.dumps(metadata["clips"][clip.name]))
                        if error:
                            raise ValueError(path + ": " + error)
                        report["clipData"] += 1
                    if package(sequence.get_editor_property("skeleton")) != skeleton:
                        raise ValueError("clip binds the wrong skeleton: " + path)
                    if not lib.sequence_track_bones(sequence):
                        raise ValueError("clip has no stored bone tracks: " + path)
                    tagged = any(m and m.get_class().get_name() == "ElysiumAnimPostAdditive" for m in sequence.get_editor_property("meta_data"))
                    if tagged != bool(clip.flags & 4) or sequence.get_editor_property("additive_anim_type") != unreal.AdditiveAnimationType.AAT_NONE:
                        raise ValueError("post-additive contract differs: " + path)
                    report["clips"] += 1
                if native_sequences:
                    error, tracks, keys, omitted = lib.verify_animation_samples(
                        str(root / relative), native_sequences, [unreal.Name(n) for n in declared_omissions])
                    if error:
                        raise ValueError(error)
                    sample_report = report["rawAnimationSamples"]
                    sample_report["owners"] += 1
                    sample_report["tracks"] += tracks
                    sample_report["keys"] += keys
                    sample_report["declaredOmittedTracks"] += omitted
                _, spaces, omissions = blend_plan(id, body, clips, role)
                for path in spaces:
                    space = load(path, {**evidence, "owner_root": role})
                    if metadata is not None:
                        matches = [record for label, record in metadata["blendSpaces"].items()
                                   if baked_unit(id, "BS", role=role or None, label=label) == path]
                        if len(matches) != 1:
                            raise ValueError("blend space has no unique metadata: " + path)
                        error = unreal.ElysiumClipData.verify(space, json.dumps(matches[0]))
                        if error:
                            raise ValueError(path + ": " + error)
                        report["clipData"] += 1
                    if package(space.get_editor_property("skeleton")) != skeleton:
                        raise ValueError("blend space binds the wrong skeleton: " + path)
                    samples = space.get_editor_property("sample_data")
                    if len(samples) < 2 or any(not sample.get_editor_property("animation") for sample in samples):
                        raise ValueError("blend space lost sample references: " + path)
                    report["blendSpaces"] += 1
        except Exception as exc:
            report["failed"].append({"assetId": id, "reason": str(exc)})
            unreal.log_warning("[verify-characters] %s: %s" % (id, exc))
        checkpoint()
        unreal.ElysiumMapBakeLibrary.unload_baked_packages(manifest["packageRoot"])
    checkpoint()
    return report


if __name__ == "__main__":
    result = run(argument("ImportCharacters"), argument("ImportMaterialsRoot"))
    unreal.log("[verify-characters] " + json.dumps(result))
    if result["failed"]:
        raise SystemExit(1)
