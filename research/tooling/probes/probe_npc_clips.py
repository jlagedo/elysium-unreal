"""What each NPC's clip vocabulary actually resolves to, off `$ELYSIUM_EXPORT_ROOT/npc/npc_manifest.json`.

The manifest's own consistency check and the answer to "which idle does this NPC play" --
the question the runtime's default-idle policy asks (roadmap 8.5). Reads only `$ELYSIUM_EXPORT_ROOT/`; touches
no game install.

The policy it reports is VtMB's own, read out of shipped data rather than guessed:

  default_disposition (on 242/243 npc_* entities) -> vdata/system/dispositiontable.txt
  -> "Animation Name" -> an ACT_DISPOSITION clip named Stance_<Name>_Idle_*
  -> fallback ACT_IDLE, then any activity-less clip whose label reads as an idle

Selection is by **activity**, not by label: `regular_cop` resolves 229 clips with "idle" in
the name, of which `Stance_Dead_Idle_1` and `Bed_Left_Idle` are not standing idles.

CLI:
  uv run elysium research probe_npc_clips              # per-NPC rollup + the manifest invariants
  uv run elysium research probe_npc_clips --acts       # the activity histogram over every baked clip
  uv run elysium research probe_npc_clips <stem> ...   # one NPC's resolved idle set, in detail
"""
import json
import os
import re
import sys
from collections import Counter
from elysium_pipeline.paths import export_root

OUT = os.fspath(export_root())
MANIFEST = os.path.join(OUT, "npc", "npc_manifest.json")
DISPOSITIONS = os.path.join(OUT, "vdata", "system", "dispositiontable.txt")

ACT_IDLE = "ACT_IDLE"
ACT_DISPOSITION = "ACT_DISPOSITION"


def load():
    with open(MANIFEST, encoding="utf-8") as f:
        m = json.load(f)
    if m.get("manifest_version") != 2:
        sys.exit(f"{MANIFEST}: expected manifest_version 2, got {m.get('manifest_version')!r} "
                 f"-- re-run `uv run elysium export bundle npc --force`")
    return m


def clip_meta(m, stem, label):
    """The owning stem's metadata for one of an NPC's resolved clips, or None."""
    owner = m["npcs"][stem]["clips"].get(label)
    if owner is None:
        return None
    src = (m["npcs"][owner]["own_clips"] if owner == stem
           else m["banks"].get(owner, {}).get("clips", {}))
    return src.get(label)


def disposition_anim_names():
    """{disposition -> "Animation Name"} out of the shipped table. The stance clip an NPC
    idles in is named for the value, not the key (they coincide for most rows)."""
    if not os.path.exists(DISPOSITIONS):
        return {}
    txt = open(DISPOSITIONS, encoding="utf-8", errors="replace").read()
    out, cur = {}, None
    for line in txt.splitlines():
        s = line.split("//", 1)[0].strip()
        if not s or s in "{}":
            continue
        q = re.findall(r'"([^"]*)"', s)
        if not q:                       # a bare token opening a block: the disposition name
            cur = s
        elif len(q) >= 2 and q[0].lower() == "animation name" and cur:
            out[cur] = q[1]
    return out


def _by_weight(m, stem, labels):
    """Highest `actweight` first, then by label. The weight is the engine's weighted-random
    share among the clips sharing an activity, so it -- not alphabetical order -- decides
    which one is the resting pick: `brujah_male_armor_0`'s ACT_IDLE set is `idle01` at 30
    against three fidgets at 1, i.e. the idle ~91% of the time and a fidget ~9%."""
    return sorted(labels, key=lambda l: (-(clip_meta(m, stem, l) or {}).get("weight", 0), l))


def idle_candidates(m, stem, anim_name="Neutral"):
    """The stance set, the ACT_IDLE set, and the loose fallback -- in policy order.

    The stance bank names idles `Stance_<Name>_Idle_<N>` and the transitions between them
    `Stance_<Name>_Trans_<a>_<b>`; both are ACT_DISPOSITION, so the idle filter has to spell
    out `_Idle`. The transitions are returned separately -- they are what a stance change
    plays between two idles rather than snapping."""
    clips = m["npcs"][stem]["clips"]
    stance, trans, act_idle, loose = [], [], [], []
    pre = ("stance_%s_" % anim_name).lower()
    for label in clips:
        meta = clip_meta(m, stem, label)
        act = (meta or {}).get("activity", "")
        ll = label.lower()
        if act == ACT_DISPOSITION and ll.startswith(pre):
            (stance if ll.startswith(pre + "idle") else trans).append(label)
        elif act == ACT_IDLE:
            act_idle.append(label)
        elif not act and "idle" in ll:
            loose.append(label)
    return (_by_weight(m, stem, stance), _by_weight(m, stem, act_idle),
            sorted(loose), sorted(trans))


def check_invariants(m):
    """Every resolvable clip carries metadata on its owner, and every owner exists."""
    bad = []
    for stem, rec in m["npcs"].items():
        for label, owner in rec["clips"].items():
            if owner != stem and owner not in m["banks"]:
                bad.append((stem, label, owner, "owner not in `banks`"))
            elif clip_meta(m, stem, label) is None:
                bad.append((stem, label, owner, "no metadata on owner"))
    return bad


def main(argv):
    m = load()
    npcs, banks = m["npcs"], m["banks"]

    if "--acts" in argv:
        c = Counter()
        for rec in banks.values():
            c.update(v["activity"] or "<none>" for v in rec["clips"].values())
        for rec in npcs.values():
            c.update(v["activity"] or "<none>" for v in rec["own_clips"].values())
        print(f"activity histogram over {sum(c.values())} baked clips, "
              f"{len(c) - (1 if '<none>' in c else 0)} distinct activities:")
        for a, n in c.most_common(40):
            print(f"   {a:<34} {n}")
        return 0

    dispo = disposition_anim_names()
    neutral = dispo.get("Neutral", "Neutral")
    wanted = [a for a in argv if not a.startswith("-")] or sorted(npcs)

    print(f"{len(npcs)} NPCs / {len(banks)} banks; dispositiontable rows: {len(dispo)} "
          f"(Neutral -> {neutral!r})\n")
    tiers = Counter()
    for stem in wanted:
        if stem not in npcs:
            print(f"  {stem}: not in the manifest")
            continue
        rec = npcs[stem]
        stance, act_idle, loose, trans = idle_candidates(m, stem, neutral)
        tier = "STANCE" if stance else "ACT_IDLE" if act_idle else "loose" if loose else "NONE"
        tiers[tier] += 1
        pick = (stance or act_idle or loose or ["-"])[0]
        print(f"  {tier:<8} {stem:<28} clips={len(rec['clips']):<5} own={len(rec['own_clips']):<4}"
              f" stance={len(stance):<2} trans={len(trans):<2} act_idle={len(act_idle):<2}"
              f" -> {pick}")
        if len(wanted) <= 4:
            for name, group in (("stance", stance), ("trans", trans),
                                ("ACT_IDLE", act_idle), ("loose", loose)):
                for label in group[:12]:
                    meta = clip_meta(m, stem, label) or {}
                    print(f"        {name:<9} {label:<32} {rec['clips'][label]:<38}"
                          f" w={meta.get('weight')} {meta.get('frames')}f @ {meta.get('fps')}")
    print("\n  " + "  ".join(f"{k}={v}" for k, v in tiers.most_common()))

    bad = check_invariants(m)
    print(f"\ninvariants: {'PASS' if not bad else 'FAIL'} ({len(bad)} violations)")
    for row in bad[:10]:
        print("   !! %s: clip %r owner %r -- %s" % row)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
