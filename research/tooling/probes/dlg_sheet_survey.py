# -*- coding: utf-8 -*-
"""Survey which *player-sheet* data the exported maps' NPC dialogues consume.

Walks every exported map's `.ents` (`$ELYSIUM_EXPORT_ROOT/*/*.ents`), collects each NPC's `dialogname`,
parses the referenced `.dlg`, and extracts every reference to player-sheet data off `pc`:
clan (`IsClan(pc,...)`), gender (`pc.IsMale()`), ability skill-checks (the dlgexpr
`Skill <n>` form, validated against `vdata/system/feats.txt` InternalNames), explicit
`CalcFeat("...")`, direct `pc.<attr>` reads, and `func(pc,...)` helpers. Splits gating
*reads* (which decide whether a choice appears) from state *mutations* (writes), and flags
what the tutorial (`sp_tutorial_1`) consumes first — the 9.4 sheet/Character-API build order.

Read-only analysis over `$ELYSIUM_EXPORT_ROOT/` (the gitignored, regenerable mirror); writes nothing. Sibling
of `ent_survey.py`. Usage: `uv run elysium research dlg_sheet_survey` (findings recorded 2026-07-24 in
`docs/vtmb/game_runtime.md`).
"""
import collections
import glob
import os
import re

from elysium_pipeline.paths import export_root

OUT = os.fspath(export_root())
TUTORIAL = "sp_tutorial_1"


def feat_vocab():
    """The ability/feat InternalNames a `Skill <n>` check can name (vdata/system/feats.txt)."""
    names = set()
    path = os.path.join(OUT, "vdata", "system", "feats.txt")
    try:
        txt = open(path, encoding="latin-1").read()
        for m in re.finditer(r'"InternalName"\s*"([^"]+)"', txt):
            names.add(m.group(1))
    except IOError:
        pass
    return names


def parse_dlg(path):
    """13-field `.dlg` rows -> [{cond, act}] (col-4 condition, col-5 action)."""
    raw = open(path, "rb").read().decode("latin-1").replace("\r\n", "\n")
    rows = []
    for line in raw.split("\n"):
        if not line.strip():
            continue
        segs = line.split("}{")
        if len(segs) < 13:
            continue

        def uw(s):
            s = s.strip()
            if s.startswith("{"):
                s = s[1:]
            if s.endswith("}"):
                s = s[:-1]
            return s.strip()

        f = [uw(x) for x in segs]
        rows.append(dict(cond=f[4], act=f[5]))
    return rows


RE_ISCLAN = re.compile(r'IsClan\s*\(\s*pc\s*,\s*"([^"]+)"', re.I)
RE_ISMALE = re.compile(r"\bpc\.IsMale\s*\(|IsMale\s*\(\s*pc\b", re.I)
RE_PC_ATTR = re.compile(r"\bpc\.([A-Za-z_]\w*)")
RE_CALCFEAT = re.compile(r'CalcFeat\s*\(\s*"?([A-Za-z_ ]+)"?')
RE_FUNC_PC = re.compile(r"\b([A-Za-z_]\w*)\s*\(\s*pc\b")
RE_SKILL = re.compile(r"(?<![.\w])([A-Za-z_][A-Za-z_ ]*?)\s*(>=|<=|==|!=|>|<)?\s*(\d+)")


def main():
    import json

    feats = feat_vocab()
    dlg_to_maps = collections.defaultdict(set)
    for ents in glob.glob(os.path.join(OUT, "*", "*.ents")):
        mp = os.path.basename(os.path.dirname(ents))
        d = json.load(open(ents, encoding="utf-8", errors="replace"))
        for e in d.get("entities", []):
            dn = e.get("keys", {}).get("dialogname")
            if dn:
                dlg_to_maps[dn].add(mp)

    ref = collections.defaultdict(collections.Counter)
    first_map = {}

    def note(cat, field, mp):
        ref[cat][field] += 1
        key = (cat, field)
        if key not in first_map or (mp == TUTORIAL and first_map[key] != TUTORIAL):
            first_map[key] = mp

    for dn, maps in dlg_to_maps.items():
        path = dn if os.path.exists(dn) else os.path.join(OUT, dn)
        if not os.path.exists(path):
            continue
        mp = TUTORIAL if TUTORIAL in maps else sorted(maps)[0]
        for r in parse_dlg(path):
            for field in (r["cond"], r["act"]):
                if not field:
                    continue
                for m in RE_ISCLAN.finditer(field):
                    note("clan", m.group(1), mp)
                if RE_ISMALE.search(field):
                    note("gender", "IsMale", mp)
                for m in RE_CALCFEAT.finditer(field):
                    note("skill(CalcFeat)", m.group(1).strip(), mp)
                for m in RE_PC_ATTR.finditer(field):
                    note("pc.attr/method", m.group(1), mp)
                for m in RE_FUNC_PC.finditer(field):
                    if m.group(1).lower() not in ("isclan", "ismale"):
                        note("helper(pc,...)", m.group(1), mp)
            for m in RE_SKILL.finditer(r["cond"] or ""):
                if m.group(1).strip() in feats:
                    note("skillcheck", m.group(1).strip(), mp)

    print("player-sheet consumption -- %d dialogs, %d maps, feat vocab %d"
          % (len(dlg_to_maps), len({m for ms in dlg_to_maps.values() for m in ms}), len(feats)))
    for cat in ["clan", "gender", "skillcheck", "skill(CalcFeat)", "pc.attr/method", "helper(pc,...)"]:
        items = ref[cat].most_common()
        print("\n### %s (%d distinct, %d refs)" % (cat, len(items), sum(ref[cat].values())))
        for field, n in items:
            fm = first_map[(cat, field)]
            print("  %4d  %-22s first=%s%s" % (n, field, fm, " [TUTORIAL]" if fm == TUTORIAL else ""))


if __name__ == "__main__":
    main()
