"""Author native skeletal core products from the GLB stage manifest."""
import hashlib
import json
from pathlib import Path
import re
import time

import unreal
from pipeline.unreal import _bootstrap  # noqa: F401
from pipeline.unreal import bake_lib as bl
from elysium_pipeline.asset_paths import baked_unit, assert_unique_paths
from elysium_pipeline.formats import eskm

PRODUCER = "characters"


def argument(name, default=""):
    needle = "-" + name.lower() + "="
    for token in re.findall(r'"[^\"]*"|\S+', unreal.SystemLibrary.get_command_line()):
        token = token.strip('"')
        if token.lower().startswith(needle):
            return token[len(needle):].strip('"')
    return default


def _json(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def _check_file(root, relative, digest):
    path = (root / relative).resolve()
    if not path.is_relative_to(root.resolve()):
        raise RuntimeError("staged path escapes its root: %s" % relative)
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != digest:
        raise RuntimeError("staged digest mismatch: %s" % relative)
    return path, data


def _material_path(id, material_root):
    if id.startswith("vtmb:missing-material:"):
        return "/Game/ElysiumGenerated/Materials/V2/MI_V2_Missing"
    baked_unit(id, "MI")  # validate identity before following a staged path
    key = id.removeprefix("vtmb:material:")
    provenance = _json(material_root / (key + ".provenance.json"))
    if not provenance.get("skinnedAsset"):
        raise RuntimeError("material has no staged skeletal routing: %s" % id)
    return provenance["skinnedAsset"]


def _transform(row):
    transform = unreal.Transform()
    transform.translation = unreal.Vector(*row["position"])
    transform.rotation = unreal.Quat(*row["rotation"])
    transform.scale3d = unreal.Vector(1., 1., 1.)
    return transform


def blend_plan(asset_id, body, clips, role=""):
    """Expected native grids and every unresolved source cell, scoped to this clip owner."""
    table = {"grids": body["grids"], "pose_parameters": body["sourceSemantics"]["mdl"]["poseParameters"],
             "autolayers": {s["label"]: s["autolayers"] for s in body["sequences"] if s["autolayers"]}}
    hosts = {}
    for host, layers in table["autolayers"].items():
        for layer in layers:
            hosts.setdefault(layer.removeprefix("@"), set()).add(host)
    live = {baked_unit(asset_id, "A", role=role or None, label=c.name).casefold() for c in clips}
    expected, omissions, identities = [], [], []
    for label, grid in sorted(table["grids"].items()):
        for host in sorted(hosts.get(label, {""})):
            suffix = "@" + host if host else ""
            missing = []
            for cell in grid["cells"]:
                name = cell["clip"]
                candidate = baked_unit(asset_id, "A", role=role or None, label=name.removeprefix("@") + suffix) if name else None
                if not candidate or candidate.casefold() not in live:
                    missing.append({"axis": cell["axis"], "clip": name, "asset": candidate})
            count = len(grid["cells"]) - len(missing)
            product = baked_unit(asset_id, "BS", role=role or None, label=label + suffix)
            if count >= 2:
                expected.append(product)
                identities.append(((label, host, role), product))
            if missing or count < 2:
                omissions.append({"grid": label, "host": host, "missingCells": missing, "skippedGrid": count < 2})
    assert_unique_paths(identities)
    return table, expected, omissions


def run(manifest_path, material_root, force=False):
    started = time.time()
    root = Path(manifest_path).parent
    manifest = _json(manifest_path)
    if manifest.get("producer") != PRODUCER or manifest.get("packageRoot") != "/ElysiumBaked/Models":
        raise RuntimeError("invalid character manifest producer/root")
    if manifest["stageFailures"]:
        raise RuntimeError("character stage has failures")
    selected = set(manifest["selectedUnits"])
    entries = [e for e in manifest["assets"] if e["assetId"] in selected]
    by_id = {e["assetId"]: e for e in manifest["assets"]}
    lib = unreal.ElysiumSkeletalBuildLibrary
    drain = unreal.ElysiumMapBakeLibrary
    dll = Path(unreal.Paths.project_dir()) / "Binaries/Win64/UnrealEditor-ElysiumUE.dll"
    tool_digest = hashlib.sha256(Path(__file__).read_bytes()
                                 + Path(__file__).with_name("bake_lib.py").read_bytes() + dll.read_bytes()).hexdigest()
    report = {"schemaVersion": "1.0.0", "scope": "native-core-products", "producer": PRODUCER, "imported": 0, "reused": 0,
              "failed": [], "pruned": 0, "foreign": 0, "unstamped": 0, "pendingProjections": {},
              "blendOmissions": {}, "suppressedAppendixTracks": {}, "assets": []}
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(
        [manifest["packageRoot"], "/ElysiumBaked/Materials"], force_rescan=True)
    protected = set()
    family_fingerprints = {}
    skeleton_receipts = {}
    includes = {}

    def checkpoint():
        report["seconds"] = round(time.time() - started, 2)
        (root / "import_report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")

    checkpoint()  # a failed/crashed editor must not leave a previous success report

    def fingerprint(kind, path, recipe):
        return bl.recipe_fingerprint("characters." + kind, path, {"tool": tool_digest, "inputs": recipe})

    def current(path, digest):
        stored = bl.stored_recipe(path, producer=PRODUCER)
        return not force and stored == digest

    def publish_data_asset(path, digest, kind, encoded, asset_id=""):
        protected.add(path)
        if current(path, digest):
            report["reused"] += 1
            return
        asset = unreal.load_asset(path)
        if asset is None:
            directory, name = path.rsplit("/", 1)
            factory = unreal.DataAssetFactory()
            factory.set_editor_property("data_asset_class", kind)
            asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, directory, kind, factory)
        result, error = kind.apply_json(asset, encoded.decode("utf-8"))
        if result is None:
            raise RuntimeError(error)
        bl.stamp_recipe(asset, digest, producer=PRODUCER)
        if asset_id:
            unreal.EditorAssetLibrary.set_metadata_tag(asset, "ElysiumAssetId", asset_id)
        if not bl.save(path):
            raise RuntimeError("could not save data asset: " + path)
        report["imported"] += 1
        report["assets"].append(path)

    def stamp(path, digest, evidence, clip_data=None):
        asset = unreal.load_asset(path)
        if asset is None:
            raise RuntimeError("native builder did not create %s" % path)
        evidence = {**evidence, "recipeFingerprint": digest}
        record, error = unreal.ElysiumCharacterProvenance.apply_json(asset, json.dumps(evidence))
        if record is None:
            raise RuntimeError("character provenance: %s" % error)
        if clip_data is not None:
            metadata, error = unreal.ElysiumClipData.apply_json(asset, json.dumps(clip_data))
            if metadata is None:
                raise RuntimeError("clip metadata: %s" % error)
        bl.stamp_recipe(asset, digest, producer=PRODUCER)
        if not bl.save(path):
            raise RuntimeError("could not save stamped asset: %s" % path)
        protected.add(path)
        report["assets"].append(path)

    needed_families = {e.get("animationSkeletonAsset") for e in entries}
    needed_families.update(a["animationSkeletonAsset"] for e in entries for a in e.get("actors", ()))
    for family in manifest["bankPartition"]["families"]:
        path = family["skeletonAsset"]
        if path not in needed_families:
            continue
        digest = fingerprint("family", path, family)
        family_fingerprints[path] = digest
        protected.add(path)
        sources = [str(_check_file(root, m["rig"], m["rigSha256"])[0]) for m in family["members"]]
        if current(path, digest):
            report["reused"] += 1
            continue
        error, count = lib.build_family_skeleton(sources, path, True, recipe_fingerprint=digest)
        if error or count != family["boneCount"]:
            report["failed"].append({"assetId": path, "reason": "family build %s (%d/%d bones)" % (error, count, family["boneCount"])})
            checkpoint()
            return report
        stamp(path, digest, {"sourceUnits": [m["assetId"] for m in family["members"]], "bankFamilyTreeSha256": family["treeSha256"]})
        report["imported"] += 1

    def animations(entry, body, owner):
        role = owner.get("root", "")
        relative = owner.get("payload", entry["payload"])
        payload_hash = owner.get("payloadSha256", entry["recipe"]["payloadSha256"])
        path, blob = _check_file(root, relative, payload_hash)
        skeleton = owner.get("animationSkeletonAsset", entry["animationSkeletonAsset"])
        if not skeleton:
            return
        clips = [c for c in eskm.clip_payloads(blob) if not (c.flags & 4 and c.base)]
        _, metadata_bytes = _check_file(root, owner["clipData"], owner["clipDataSha256"])
        metadata = json.loads(metadata_bytes)
        expected = [baked_unit(entry["assetId"], "A", role=role or None, label=c.name) for c in clips]
        if not expected:
            return
        package = expected[0].rsplit("/", 1)[0]
        grids, spaces_expected, omissions = blend_plan(entry["assetId"], body, clips, role)
        if omissions:
            report["blendOmissions"][entry["assetId"] + ":" + role] = omissions
        digest = fingerprint("animations", package + ":" + role,
                             {"payload": payload_hash, "metadata": owner["clipDataSha256"], "skeleton": skeleton,
                              "family": family_fingerprints.get(skeleton), "stage": entry["recipe"]})
        protected.update(expected + spaces_expected)
        if all(current(asset, digest) for asset in expected):
            report["reused"] += len(expected)
        else:
            error, count, dropped, suppressed, suppressed_bones = lib.build_anim_sequences_from_stage(
                str(path), package, skeleton, role, recipe_fingerprint=digest)
            if error or dropped or count != len(expected):
                raise RuntimeError("animations %s: %s (%d dropped tracks; %d/%d clips)" % (entry["assetId"], error, dropped, count, len(expected)))
            suppression = {"tracks": suppressed, "bones": [str(name) for name in suppressed_bones],
                           "rule": "dormant donor appendix uses the playing mesh bind; full tracks remain in the GLB and stage"}
            if suppressed:
                report["suppressedAppendixTracks"][entry["assetId"] + ":" + role] = suppression
            for clip, asset in zip(clips, expected):
                stamp(asset, digest, {"assetId": entry["assetId"], "ownerRoot": role, "unitSha256": entry["recipe"]["unitSha256"],
                                      "payloadSha256": payload_hash, "suppressedAppendix": suppression}, metadata["clips"][clip.name])
            report["imported"] += len(expected)
        if all(current(asset, digest) for asset in spaces_expected):
            report["reused"] += len(spaces_expected)
        elif grids["grids"]:
            error, spaces, skipped_grids, skipped_cells = lib.build_blend_spaces_from_stage(json.dumps(grids), package, skeleton, role, recipe_fingerprint=digest)
            if error or spaces != len(spaces_expected):
                raise RuntimeError("blend spaces %s: %s (%d/%d products)" % (entry["assetId"], error, spaces, len(spaces_expected)))
            for asset in spaces_expected:
                matches = [record for label, record in metadata["blendSpaces"].items()
                           if baked_unit(entry["assetId"], "BS", role=role or None, label=label) == asset]
                if len(matches) != 1:
                    raise RuntimeError("blend space has no unique metadata record: " + asset)
                stamp(asset, digest, {"assetId": entry["assetId"], "ownerRoot": role,
                                      "unitSha256": entry["recipe"]["unitSha256"]}, matches[0])
            report["imported"] += spaces

    for ordinal, entry in enumerate(entries):
        id = entry["assetId"]
        try:
            body_path, body_bytes = _check_file(root, entry["body"], entry["recipe"]["bodySha256"])
            body = json.loads(body_bytes)
            includes[id] = [row["asset"] for row in body["sourceSemantics"]["mdl"]["includeModels"]]
            source, source_blob = _check_file(root, entry["payload"], entry["recipe"]["payloadSha256"])
            skeleton = entry["skeletonAsset"]
            if skeleton:
                digest = fingerprint("skeleton", skeleton, entry["recipe"])
                skeleton_receipts[skeleton] = (digest, {"assetId": id, "unitSha256": entry["recipe"]["unitSha256"]})
                protected.add(skeleton)
                if not current(skeleton, digest):
                    rig = source
                    if entry.get("wieldSkeletonSource"):
                        rig, _ = _check_file(root, entry["wieldSkeletonSource"], entry["recipe"]["wieldRigSha256"])
                    error, count = lib.build_family_skeleton([str(rig)], skeleton, True, recipe_fingerprint=digest)
                    if error or count != len(eskm.bones(source_blob)):
                        raise RuntimeError(error or "singleton skeleton lost source bones")
                    stamp(skeleton, digest, {"assetId": id, "unitSha256": entry["recipe"]["unitSha256"]})
                    report["imported"] += 1
                else:
                    report["reused"] += 1
            if entry["meshAsset"]:
                asset = entry["meshAsset"]
                _, mesh_data_bytes = _check_file(root, entry["meshData"], entry["recipe"]["meshDataSha256"])
                mesh_data = json.loads(mesh_data_bytes)
                bindings = {m["slot"]: _material_path(m["assetId"], Path(material_root)) for m in entry["materials"]}
                material_recipes = {p: bl.stored_recipe(p, producer="materials") for p in bindings.values()
                                    if p.startswith("/ElysiumBaked/")}
                if any(not value for value in material_recipes.values()):
                    raise RuntimeError("material dependency is not imported/stamped: %s" % material_recipes)
                digest = fingerprint("mesh", asset, {"stage": entry["recipe"], "materials": bindings, "materialRecipes": material_recipes})
                protected.add(asset)
                if not current(asset, digest):
                    pose = [_transform(row) for row in body["wield"]["referencePose"]] if body.get("wield") else []
                    error = lib.build_skeletal_mesh_from_stage(str(source), asset, skeleton, bindings, pose, recipe_fingerprint=digest)
                    if error:
                        raise RuntimeError(error)
                    stamp(asset, digest, {"assetId": id, "unitSha256": entry["recipe"]["unitSha256"],
                                          "payloadSha256": entry["recipe"]["payloadSha256"], "meshData": mesh_data})
                    report["imported"] += 1
                else:
                    report["reused"] += 1
            if entry.get("nativeMainOwner", True):
                animations(entry, body, entry)
            for actor in entry.get("actors", ()):
                if actor["skeletonAsset"]:
                    asset = actor["skeletonAsset"]
                    digest = fingerprint("actor-skeleton", asset, actor)
                    protected.add(asset)
                    if not current(asset, digest):
                        actor_source, actor_blob = _check_file(root, actor["payload"], actor["payloadSha256"])
                        error, count = lib.build_family_skeleton([str(actor_source)], asset, True, recipe_fingerprint=digest)
                        if error or count != len(eskm.bones(actor_blob)):
                            raise RuntimeError(error or "actor skeleton lost source bones")
                        stamp(asset, digest, {"assetId": id, "ownerRoot": actor["root"], "unitSha256": entry["recipe"]["unitSha256"]})
                        report["imported"] += 1
                    else:
                        report["reused"] += 1
                animations(entry, body, actor)
            semantics = body["sourceSemantics"]
            present = {"facial": any((semantics.get("facial") or {}).values()),
                       "procedural": (semantics.get("procedural") or {}).get("axisInterpolation"),
                       "cloth": (semantics.get("cloth") or {}).get("garments"),
                       "secondaryMotion": semantics.get("secondaryMotion"),
                       "physics": (semantics.get("physics") or {}).get("solids"),
                       "eyes": any(model.get("eyeballs") for part in semantics["mdl"].get("bodyParts", ())
                                   for model in part["models"])}
            pending = [name for name, present in present.items() if present
                       and not (entry["meshAsset"] and name in ("facial", "eyes", "procedural"))]
            if pending:
                report["pendingProjections"][id] = pending
        except Exception as exc:
            report["failed"].append({"assetId": id, "reason": str(exc)})
            unreal.log_warning("[import-characters] %s: %s" % (id, exc))
        checkpoint()
        drain.finish_asset_compilation()
        drain.unload_baked_packages(manifest["packageRoot"])
        unreal.log("[import-characters] %d/%d: %d imported, %d reused, %d failed" %
                   (ordinal + 1, len(entries), report["imported"], report["reused"], len(report["failed"])))
    if not report["failed"]:
        # All bank owners have now authored their masks. Compatibility alone does not copy them.
        for skeleton, (digest, evidence) in skeleton_receipts.items():
            sources, visited, pending = set(), set(), list(includes[evidence["assetId"]])
            while pending:
                owner = pending.pop()
                if owner in visited:
                    continue
                visited.add(owner)
                source = by_id[owner]["animationSkeletonAsset"]
                if source and source != skeleton:
                    sources.add(source)
                pending.extend(includes.get(owner, ()))
            error = lib.declare_compatible_skeletons(skeleton, sorted(sources), recipe_fingerprint=digest)
            if error:
                report["failed"].append({"assetId": evidence["assetId"], "reason": error})
            else:
                stamp(skeleton, digest, evidence)
        checkpoint()
    if not report["failed"]:
        for entry in entries:
            for owner in ([entry] if entry.get("nativeMainOwner", True) else []) + entry.get("actors", []):
                if not owner.get("bodyData"):
                    continue
                asset_path = owner["bodyDataAsset"]
                try:
                    _, encoded = _check_file(root, owner["bodyData"], owner["bodyDataSha256"])
                    digest = fingerprint("body-data", asset_path, owner["bodyDataSha256"])
                    publish_data_asset(asset_path, digest, unreal.ElysiumBodyData, encoded, entry["assetId"])
                except Exception as exc:
                    report["failed"].append({"assetId":entry["assetId"],"reason":"body data: " + str(exc)})
                checkpoint()
            drain.unload_baked_packages(manifest["packageRoot"])
    if not report["failed"] and manifest.get("castData"):
        try:
            cast = manifest["castData"]
            _, encoded = _check_file(root, cast["file"], cast["sha256"])
            publish_data_asset(cast["assetPath"], fingerprint("cast", cast["assetPath"], cast["sha256"]), unreal.ElysiumCastData, encoded)
        except Exception as exc:
            report["failed"].append({"assetId":"cast-corpus","reason":str(exc)})
        checkpoint()
    if not report["failed"]:
        counts = {}
        report["pruned"] = bl.prune_owned(manifest["packageRoot"], protected, manifest.get("pruneScope"), PRODUCER, counts)
        report.update(counts)
    checkpoint()
    return report


if __name__ == "__main__":
    manifest = argument("ImportCharacters")
    materials = argument("ImportMaterialsRoot")
    if not manifest or not materials:
        raise SystemExit("-ImportCharacters and -ImportMaterialsRoot are required")
    try:
        result = run(manifest, materials, argument("ImportForce") == "1")
    except Exception as exc:
        report_path = Path(manifest).parent / "import_report.json"
        result = _json(report_path) if report_path.is_file() else {"producer": PRODUCER, "failed": []}
        result["failed"].append({"assetId": "manifest", "reason": str(exc)})
        report_path.write_text(json.dumps(result, indent=2), encoding="utf-8")
        unreal.log_error("[import-characters] %s" % exc)
        raise SystemExit(1)
    if result["failed"]:
        raise SystemExit(1)
