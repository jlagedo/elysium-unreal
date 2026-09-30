"""Author the map's AIN jump links as non-traversable NavLinkProxy records.

Retail never plans a jump-only link: `0x102ff960` step 2 ANDs the NPC's capabilities (slot 513,
`+0x5cec`) with the link's per-hull word and no shipped NPC holds bit 2, so the `IsJumpLegal` arm
(step 4, slot 521) is unreachable (`map_jump_links` docstring, 0018/7). Each actor is therefore the
map's RECORD of one AIN link -- endpoints, pair, per-hull verdicts -- with no agent and its smart
link disabled; a link that routed would add routes retail does not have and change event order.

The per-direction geometry verdicts are asked here, because only the editor carries the ported
geometry: one batched call per map into `UElysiumNavBakeLibrary::JumpLinkVerdicts`, which runs
`FElysiumNpcBase::IsJumpLegalGeometry` with slot 521's own 80 / 250 / 160. They are data for the
report and the debugger view; nothing routes on them.

The per-hull counts are written as this map's jump-link bake report,
`$ELYSIUM_WORK_ROOT/bake/jump-links/<map>.json`, beside the nav gate's own
(`verify/nav/by-map/<map>.json`), which reads the same verdicts back off the saved level.
"""
import json
import os

import unreal

PAYLOAD_VERSION = 2


def _vector(cm):
    return unreal.Vector(float(cm[0]), float(cm[1]), float(cm[2]))


def _legality(links):
    """Every jump-only hull's forward and back verdict, in one editor call.

    Returns {(row index, hull): (forward, back)}. Forward is src -> dst at that hull's own
    endpoints; back is the reverse (`0x102ff960` binds the far end as both apex and end).
    """
    asked, starts, ends = [], [], []
    for row in links:
        for entry in row["hulls"]:
            if not entry["jumpOnly"]:
                continue
            start, end = _vector(entry["startCm"]), _vector(entry["endCm"])
            asked.append((row["index"], entry["hull"]))
            starts += [start, end]
            ends += [end, start]
    if not asked:
        return {}
    legal = list(unreal.ElysiumNavBakeLibrary.jump_link_verdicts(starts, ends))
    if len(legal) != len(starts):
        raise ValueError("JumpLinkVerdicts answered %d of %d jump directions"
                         % (len(legal), len(starts)))
    return {key: (bool(legal[2 * i]), bool(legal[2 * i + 1])) for i, key in enumerate(asked)}


def _report_path(map_name):
    """Where this map's jump-link bake report lands, or None when no work root is configured."""
    from elysium_pipeline import paths
    try:
        root = paths.work_root()
    except RuntimeError:
        return None
    return os.path.join(os.fspath(root), "bake", "jump-links", "%s.json" % map_name)


def _write_report(payload, counts):
    """The per-hull counts as a file, the way `verify nav` leaves its verdicts per map."""
    report = {
        "map": payload["map"],
        "records": len(payload["links"]),
        "usedHulls": payload["usedHulls"],
        "perHull": {str(hull): row for hull, row in sorted(counts.items())},
        "graph": payload.get("summary", {}).get("graph"),
        "sha256": payload.get("sha256"),
    }
    path = _report_path(payload["map"])
    if path is None:
        unreal.log_warning("AIN jump links: no ELYSIUM_WORK_ROOT; bake report not written")
        return report
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(report, handle, indent=2)
    return report


def _verdict(entry, forward, back):
    verdict = unreal.ElysiumNavJumpHullVerdict()
    verdict.set_editor_properties({
        "hull": int(entry["hull"]),
        "motion_word": int(entry["motion"]),
        "jump_only": bool(entry["jumpOnly"]),
        # The staged answer, not recomputed here: `map_jump_links.capability_usable`.
        "capability_usable": bool(entry.get("capabilityUsable", False)),
        "legal_forward": bool(forward),
        "legal_back": bool(back),
    })
    return verdict


def author(actors, payload):
    if payload.get("version") != PAYLOAD_VERSION:
        raise ValueError("missing/unsupported map jump-link bake payload; stage the V2 map again")
    links = payload["links"]
    legality = _legality(links)
    counts = {hull: dict(records=0, jumpOnly=0, legalForward=0, legalBack=0, usable=0)
              for hull in payload["usedHulls"]}
    for row in links:
        start, end = row["startCm"], row["endCm"]
        actor = actors.spawn_actor_from_class(unreal.ElysiumNavJumpLink, unreal.Vector(*start))
        if actor is None:
            raise ValueError("cannot spawn AIN jump link %s" % row["index"])
        actor.configure_jump_link(unreal.Vector(0, 0, 0),
                                  unreal.Vector(*[end[i] - start[i] for i in range(3)]),
                                  row["index"], row["src"], row["dst"])
        verdicts = []
        for entry in row["hulls"]:
            fwd, bwd = legality.get((row["index"], entry["hull"]), (False, False))
            verdicts.append(_verdict(entry, fwd, bwd))
            tally = counts.setdefault(entry["hull"], dict(records=0, jumpOnly=0, legalForward=0,
                                                          legalBack=0, usable=0))
            tally["records"] += 1
            tally["jumpOnly"] += int(bool(entry["jumpOnly"]))
            tally["legalForward"] += int(fwd)
            tally["legalBack"] += int(bwd)
            tally["usable"] += int(bool(entry.get("capabilityUsable")))
        if not actor.set_hull_verdicts(verdicts):
            raise ValueError("AIN jump link %s refused its per-hull verdicts" % row["index"])
        actor.set_bidirectional(bool(row["bidirectional"]))
        if actor.is_traversable():
            raise ValueError("AIN jump link %s is traversable after configuration" % row["index"])
        actor.set_actor_label("AIN_Jump_%d_%d_%d" % (row["index"], row["src"], row["dst"]))
        actor.set_folder_path("Navigation")
        actor.tags = ["elysium.nav-jump"]
    _write_report(payload, counts)
    unreal.log("AIN jump links: %d disabled record actor(s); %s" % (len(links), "; ".join(
        "hull %d: %d records, %d jump-only, %d/%d legal fwd/back, %d usable" % (
            hull, c["records"], c["jumpOnly"], c["legalForward"], c["legalBack"], c["usable"])
        for hull, c in sorted(counts.items()))))
    return len(links)
