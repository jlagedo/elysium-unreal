"""Ask every baked level whether it agrees with retail's graph.

Runs inside the editor because only the editor can open a `.umap` and query its Recast meshes. It
holds no judgement: it opens each level, runs the queries the answer key names, and writes what
came back. The verdicts are `validation/nav_acceptance.py`, which is pure arithmetic over two
dictionaries and is tested without an editor at all.

    uv run elysium verify nav --maps sp_tutorial_1 --maps sm_hub_1
"""

import json
import re

import unreal


def cmdline_arg(key, default=""):
    """Read -Key=value off the editor command line (a quoted path is one token)."""
    needle = "-%s=" % key
    for token in re.findall(r'"[^"]*"|\S+', unreal.SystemLibrary.get_command_line()):
        token = token.strip('"')
        if token.lower().startswith(needle.lower()):
            return token[len(needle):].strip('"')
    return default

#: How far from a stated endpoint a projection may land. Retail states a node position at hull
#: height; Recast's surface sits on the rasterised floor, which is not the same plane. Generous on
#: Z for that, tight on X/Y so a point does not silently find a different room's floor.
PROJECT_EXTENT = unreal.Vector(60.0, 60.0, 250.0)


def log(message):
    unreal.log("[verify-nav] %s" % message)


def _vectors(rows, field):
    return [unreal.Vector(float(r[field][0]), float(r[field][1]), float(r[field][2]))
            for r in rows]


def verify_map(key, agent_names):
    """Open one baked level and answer every query its key asks. Judgement happens elsewhere."""
    level = "/ElysiumBaked/%s/%s" % (key["map"], key["map"])
    if not unreal.EditorAssetLibrary.does_asset_exist(level):
        return {"map": key["map"], "skipped": "no baked level"}
    if not unreal.EditorLoadingAndSavingUtils.load_map(level):
        raise RuntimeError("could not open %s" % level)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()

    answers = {
        "map": key["map"],
        "meshes": list(unreal.ElysiumNavVerifyLibrary.agent_meshes(world)),
        "perHull": {},
        "bridging": {},
    }

    base_hull = str(key["baseHull"])
    for hull, rows in key["perHull"].items():
        agent = agent_names.get(hull)
        if agent is None:
            continue
        ground = rows["ground"]
        jump = rows["jump"]
        answers["perHull"][hull] = {
            "agent": agent,
            "groundLengths": [float(v) for v in unreal.ElysiumNavVerifyLibrary.path_lengths(
                world, agent, _vectors(ground, "startCm"), _vectors(ground, "endCm"),
                PROJECT_EXTENT)],
            # Jump-link endpoints are asked to PROJECT, not to path: the jump itself is a smart
            # link, so a missing path between its ends is the link doing its job.
            "jumpStartsLanded": [bool(v) for v in unreal.ElysiumNavVerifyLibrary.project_points(
                world, agent, _vectors(jump, "startCm"), PROJECT_EXTENT)],
            "jumpEndsLanded": [bool(v) for v in unreal.ElysiumNavVerifyLibrary.project_points(
                world, agent, _vectors(jump, "endCm"), PROJECT_EXTENT)],
        }
        log("%s hull %s (%s): %d ground link(s), %d jump endpoint pair(s)"
            % (key["map"], hull, agent, len(ground), len(jump)))

    # The bridging links, asked TWICE: of their own agent's mesh, which must join them, and of the
    # base agent's, which must not. One answer without the other proves nothing.
    base_agent = agent_names.get(base_hull)
    for hull, rows in key["agentOnly"].items():
        agent = agent_names.get(hull)
        bridging = rows["bridging"]
        if agent is None or base_agent is None or not bridging:
            continue
        # The SAME two nodes asked of both meshes, each at its own hull's Z offset. The key
        # carries both sets because a bridging link has no row in the base hull's link table.
        answers["bridging"][hull] = {
            "agent": agent,
            "baseAgent": base_agent,
            "agentLengths": [float(v) for v in unreal.ElysiumNavVerifyLibrary.path_lengths(
                world, agent, _vectors(bridging, "startCm"), _vectors(bridging, "endCm"),
                PROJECT_EXTENT)],
            "baseLengths": [float(v) for v in unreal.ElysiumNavVerifyLibrary.path_lengths(
                world, base_agent, _vectors(bridging, "baseStartCm"),
                _vectors(bridging, "baseEndCm"), PROJECT_EXTENT)],
        }
        log("%s: %d bridging link(s) asked of both %s and %s"
            % (key["map"], len(bridging), agent, base_agent))
    return answers


def main():
    # A fresh commandlet has not indexed the baked mount, so `does_asset_exist` answers false for
    # every level that is plainly there. The collision import scans for the same reason.
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(
        ["/ElysiumBaked"], force_rescan=True)

    manifest_path = cmdline_arg("VerifyNavKey")
    out_path = cmdline_arg("VerifyNavOut")
    if not manifest_path or not out_path:
        raise SystemExit("verify_nav needs -VerifyNavKey=<key.json> -VerifyNavOut=<answers.json>")
    with open(manifest_path, "r", encoding="utf-8") as handle:
        manifest = json.load(handle)

    results = []
    for key in manifest["maps"]:
        agent_names = {str(hull): name for hull, name in key["agentNames"].items()}
        results.append(verify_map(key, agent_names))

    with open(out_path, "w", encoding="utf-8") as handle:
        json.dump({"maps": results}, handle, indent=2)
    log("wrote %s" % out_path)


main()
