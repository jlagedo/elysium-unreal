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

from elysium_pipeline.validation.nav_acceptance import FLYING_HULLS


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
#: Z for that, tight on X/Y so a point does not silently find a different room's floor. The place
#: reports ask it first; the wider extents they ask after it come from the key
#: (`map_nav_acceptance.WIDER_PROJECT_EXTENTS_CM`).
PROJECT_EXTENT = unreal.Vector(60.0, 60.0, 250.0)


def log(message):
    unreal.log("[verify-nav] %s" % message)


def _vectors(rows, field):
    return [unreal.Vector(float(r[field][0]), float(r[field][1]), float(r[field][2]))
            for r in rows]


def _jump_link_answer(actor):
    """One `AElysiumNavJumpLink` as the level holds it: its pair, its traversability, its verdicts."""
    return {
        "index": int(actor.get_editor_property("source_link_index")),
        "src": int(actor.get_editor_property("source_node")),
        "dst": int(actor.get_editor_property("destination_node")),
        "enabled": bool(actor.is_smart_link_enabled()),
        "agentBits": int(actor.supported_agent_bits()),
        "traversable": bool(actor.is_traversable()),
        "verdicts": [{"hull": int(v.hull), "motion": int(v.motion_word),
                      "jumpOnly": bool(v.jump_only), "capabilityUsable": bool(v.capability_usable),
                      "legalForward": bool(v.legal_forward), "legalBack": bool(v.legal_back)}
                     for v in actor.get_editor_property("hull_verdicts")],
    }


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
            # Jump-link endpoints are asked to PROJECT, not to path: retail plans no jump link
            # (`0x102ff960` step 2 -- no NPC holds bit 2), so a missing path between its ends is
            # retail's own answer; the ends must still land, because ground links reach them.
            "jumpStartsLanded": [bool(v) for v in unreal.ElysiumNavVerifyLibrary.project_points(
                world, agent, _vectors(jump, "startCm"), PROJECT_EXTENT)],
            "jumpEndsLanded": [bool(v) for v in unreal.ElysiumNavVerifyLibrary.project_points(
                world, agent, _vectors(jump, "endCm"), PROJECT_EXTENT)],
        }
        # 0018/7: retail has NO edge for this hull across a jump-only pair (the graph builder's
        # walk failed there; the path-finder refuses the jump), so a mesh path between the two
        # ends is the modernization reaching further than retail. Asked, and only reported.
        if int(hull) not in FLYING_HULLS:
            answers["perHull"][hull]["jumpLengths"] = [
                float(v) for v in unreal.ElysiumNavVerifyLibrary.path_lengths(
                    world, agent, _vectors(jump, "startCm"), _vectors(jump, "endCm"),
                    PROJECT_EXTENT)]
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

    # The baked jump-link records (0018/7), read back as the level holds them: the gate expects
    # every staged pair present and none of them traversable.
    answers["jumpLinks"] = [_jump_link_answer(actor) for actor in
                            unreal.GameplayStatics.get_all_actors_of_class(
                                world, unreal.ElysiumNavJumpLink)]
    log("%s: %d baked jump-link record(s)" % (key["map"], len(answers["jumpLinks"])))

    # The place reports (0018 story 4): every place and authored point asked to project at the
    # gate's own extent and then at each wider one, so an off-mesh point is answered with how far
    # off it is; every zone pair's candidate node pairs and every zone's anchor-to-member pairs
    # asked to path. Observations -- the verdicts never fail on them.
    queries = key.get("placeQueries") or {}
    answers["places"] = {}
    extents = [PROJECT_EXTENT] + [unreal.Vector(float(e[0]), float(e[1]), float(e[2]))
                                  for e in queries.get("widerExtentsCm", [])]
    for hull, asked in queries.get("perHull", {}).items():
        agent = agent_names.get(hull)
        if agent is None:
            continue
        points = [unreal.Vector(float(p[0]), float(p[1]), float(p[2]))
                  for p in asked["pointsCm"]]
        candidates = [row for pair in asked["zonePairs"] for row in pair["candidates"]]
        same_zone = asked["sameZone"]
        answers["places"][hull] = {
            "agent": agent,
            "landed": [[bool(v) for v in unreal.ElysiumNavVerifyLibrary.project_points(
                world, agent, points, extent)] for extent in extents],
            "zonePairLengths": [float(v) for v in unreal.ElysiumNavVerifyLibrary.path_lengths(
                world, agent, _vectors(candidates, "startCm"), _vectors(candidates, "endCm"),
                PROJECT_EXTENT)],
            "sameZoneLengths": [float(v) for v in unreal.ElysiumNavVerifyLibrary.path_lengths(
                world, agent, _vectors(same_zone, "startCm"), _vectors(same_zone, "endCm"),
                PROJECT_EXTENT)],
        }
        log("%s hull %s (%s): %d place point(s), %d zone-pair candidate(s), %d same-zone pair(s)"
            % (key["map"], hull, agent, len(points), len(candidates), len(same_zone)))
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
