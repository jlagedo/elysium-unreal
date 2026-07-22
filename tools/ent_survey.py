"""Survey the VtMB entity / Source-I/O layer across the game's maps.

Reads the ENTITIES lump (0) of every .bsp and reports the data the runtime port
has to satisfy:

  * classname histogram
  * the output value shape (VtMB writes 7 comma fields, Source writes 5)
  * classname -> INPUTS it actually receives (targets resolved via targetname)
  * the usable set: entities carrying `use_icon` / `locked_icon`
  * the visibility subsystem: StartHidden / ScriptHide / ScriptUnhide

Usage:
    python tools/ent_survey.py [<maps_dir>] [--map <name>]

Findings and the use_icon enum are written up in docs/entity_io.md.
"""
import os, re, sys, glob
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bsp

DEFAULT_MAPS = r"E:\dev_game\Vampire The Masquerade - Bloodlines\Vampire\maps"

OUT_KEY = re.compile(r"^(On|Out)", re.I)
KV = re.compile(r'"([^"]*)"\s+"([^"]*)"')
BLOCK = re.compile(r"\{([^{}]*)\}", re.S)


def parse_entities(text):
    """ENTITIES lump text -> [ {key: [values]} ], preserving repeated keys."""
    out = []
    for b in BLOCK.findall(text):
        d = defaultdict(list)
        for k, v in KV.findall(b):
            d[k].append(v)
        out.append(d)
    return out


def split_output(value):
    """target,input,param,delay,times[,python[,extra]] -> tuple or None."""
    if value.count(",") < 4:
        return None
    f = value.split(",")
    return (f[0].strip(), f[1].strip(), f[2], f[3], f[4],
            f[5].strip() if len(f) > 5 else "",
            f[6].strip() if len(f) > 6 else "")


def survey(paths):
    cls_count = Counter()
    inputs_by_cls = defaultdict(Counter)
    icon_by_cls = defaultdict(Counter)
    field_hist = Counter()
    hidden = Counter()
    unresolved = Counter()
    py_calls = 0
    total_out = 0

    for path in paths:
        try:
            ents = bsp.read_lump(open(path, "rb").read(), 0).decode("ascii", "replace")
        except Exception as e:
            print("  !! %s: %s" % (os.path.basename(path), e))
            continue
        parsed = parse_entities(ents)
        name2cls = {d["targetname"][0].lower(): d["classname"][0]
                    for d in parsed if "targetname" in d and "classname" in d}

        for d in parsed:
            cls = d.get("classname", ["?"])[0]
            cls_count[cls] += 1
            if d.get("StartHidden", ["0"])[0] == "1":
                hidden[cls] += 1
            for k in ("use_icon", "locked_icon"):
                for v in d.get(k, []):
                    icon_by_cls[cls]["%s=%s" % (k, v)] += 1
            for k, vs in d.items():
                if not OUT_KEY.match(k):
                    continue
                for v in vs:
                    o = split_output(v)
                    if not o:
                        continue
                    total_out += 1
                    field_hist[v.count(",") + 1] += 1
                    if o[5]:
                        py_calls += 1
                    if not o[1]:
                        continue
                    t = o[0].lower()
                    if t in name2cls:
                        inputs_by_cls[name2cls[t]][o[1]] += 1
                    elif t and not t.startswith("!"):
                        unresolved[o[0]] += 1
    return dict(cls_count=cls_count, inputs_by_cls=inputs_by_cls,
                icon_by_cls=icon_by_cls, field_hist=field_hist, hidden=hidden,
                unresolved=unresolved, py_calls=py_calls, total_out=total_out)


def main():
    args = [a for a in sys.argv[1:]]
    maps_dir = DEFAULT_MAPS
    only = None
    if "--map" in args:
        only = args[args.index("--map") + 1]
        args = args[:args.index("--map")]
    if args:
        maps_dir = args[0]
    paths = sorted(glob.glob(os.path.join(maps_dir, "*.bsp")))
    if only:
        paths = [p for p in paths if os.path.basename(p)[:-4] == only]
    if not paths:
        print("no .bsp found in", maps_dir)
        return 1

    r = survey(paths)
    print("maps: %d   entities: %d   classnames: %d"
          % (len(paths), sum(r["cls_count"].values()), len(r["cls_count"])))
    print("outputs: %d   with a python payload (field 6): %d"
          % (r["total_out"], r["py_calls"]))
    print("output field counts:", dict(sorted(r["field_hist"].items())))

    print("\n### StartHidden entities (spawn invisible; ScriptUnhide reveals) ###")
    for c, n in r["hidden"].most_common():
        print("   %-34s %5d" % (c, n))

    print("\n### usable set (carries use_icon / locked_icon) ###")
    for cls, c in sorted(r["icon_by_cls"].items(), key=lambda kv: -sum(kv[1].values())):
        print("   %-30s %5d  %s" % (cls, sum(c.values()), dict(c.most_common(6))))

    print("\n### classname -> inputs received ###")
    for cls, c in sorted(r["inputs_by_cls"].items(), key=lambda kv: -sum(kv[1].values())):
        print("\n %s (%d wires, %d inputs)" % (cls, sum(c.values()), len(c)))
        print("    " + " ".join("%s(%d)" % kv for kv in c.most_common()))

    print("\n### unresolved targets (runtime/python-created), top 15 ###")
    for t, n in r["unresolved"].most_common(15):
        print("   %-34s %5d" % (t, n))
    return 0


if __name__ == "__main__":
    sys.exit(main())
