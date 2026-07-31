"""Survey VtMB's choreographed-scene files (`.vcd`) across the whole install.

The `logic_choreographed_scene` entity's `SceneFile` key names a **Faceposer choreo
scene** — a plain-text, brace-nested `actor { channel { event { ... } } }` document
shipped under `sound/`. This probe reads every `.vcd` the merged (patch-first)
install resolves and reports the grammar the runtime parser has to satisfy:

  * `// Choreo version N` histogram + the top-level statement inventory
  * actor / channel / event nesting, channel-name histogram
  * event **type** histogram and, per type, the sub-key histogram + value shapes
  * the flag/tag words (`fixedlength`, `resumecondition`, `active`, …)
  * ramp / tag / relative-tag / absolute-tag sub-blocks
  * which scenes the maps actually reference (`SceneFile`), and which are orphans

Usage:
    uv run elysium research probe_scenes                    # rollup over every .vcd
    uv run elysium research probe_scenes --markdown         # the doc tables
    uv run elysium research probe_scenes --dump <path>      # pretty-print one parsed scene
    uv run elysium research probe_scenes --referenced       # only map-referenced scenes
    uv run elysium research probe_scenes --json <path>      # write the full survey

Findings are written up in ../docs/vtmb/choreographed_scenes.md (roadmap RE19).
Read-only over the user's own install; produces no runtime intermediate.
"""
import argparse
import json
import os
import re
import sys
from collections import Counter, defaultdict

from elysium_pipeline.formats import bsp, install

VERSION_RE = re.compile(r"^//\s*Choreo\s+version\s+(\d+)", re.I)
# One token: a quoted string (quotes stripped) or a bare word.
TOKEN_RE = re.compile(r'"([^"]*)"|(\S+)')


# --------------------------------------------------------------------------- parse

def tokenize_line(line):
    """A choreo line -> list of tokens, comments stripped, quotes removed."""
    line = line.strip()
    if not line or line.startswith("//"):
        return []
    return [m.group(1) if m.group(1) is not None else m.group(2)
            for m in TOKEN_RE.finditer(line)]


def parse(text):
    """`.vcd` text -> {'version': int|None, 'statements': [node]}.

    A node is {'words': [...], 'children': [node]} — the grammar is uniform:
    every line is a word list, optionally followed by a `{ ... }` block. That
    covers `actor`/`channel`/`event` and every sub-block (`tags`, `flexanimations`,
    `ramp`, …) without special-casing any of them.
    """
    version = None
    for line in text.splitlines():
        m = VERSION_RE.match(line.strip())
        if m:
            version = int(m.group(1))
            break

    stack = [{"words": ["<root>"], "children": []}]
    pending = None
    for raw in text.splitlines():
        toks = tokenize_line(raw)
        if not toks:
            continue
        # A line may end with '{' or be a bare '{' / '}'.
        while toks:
            t = toks[0]
            if t == "{":
                toks.pop(0)
                node = pending or {"words": [], "children": []}
                pending = None
                stack[-1]["children"].append(node)
                stack.append(node)
                continue
            if t == "}":
                toks.pop(0)
                if pending is not None:
                    stack[-1]["children"].append(pending)
                    pending = None
                if len(stack) > 1:
                    stack.pop()
                continue
            break
        if not toks:
            continue
        if pending is not None:
            stack[-1]["children"].append(pending)
        # trailing '{' on the same line
        if toks[-1] == "{":
            pending = {"words": toks[:-1], "children": []}
            stack[-1]["children"].append(pending)
            stack.append(pending)
            pending = None
        else:
            pending = {"words": toks, "children": []}
    if pending is not None:
        stack[-1]["children"].append(pending)
    return {"version": version, "statements": stack[0]["children"]}


# ------------------------------------------------------------------- scene walking

def iter_events(scene):
    """Yield (actor_name, channel_name, event_node) for every event in a scene."""
    for st in scene["statements"]:
        if not st["words"] or st["words"][0] != "actor":
            continue
        actor = st["words"][1] if len(st["words"]) > 1 else ""
        for ch in st["children"]:
            if not ch["words"] or ch["words"][0] != "channel":
                continue
            chan = ch["words"][1] if len(ch["words"]) > 1 else ""
            for ev in ch["children"]:
                if ev["words"] and ev["words"][0] == "event":
                    yield actor, chan, ev
        # some scenes hang events straight off the actor
        for ev in st["children"]:
            if ev["words"] and ev["words"][0] == "event":
                yield actor, "<actor>", ev


def scene_events(scene):
    """Also yield the scene-level (actor-less) events, e.g. `event section`."""
    for st in scene["statements"]:
        if st["words"] and st["words"][0] == "event":
            yield "<scene>", "<scene>", st


# --------------------------------------------------------------------------- survey

def collect_vcds(idx):
    return sorted(k for k in idx if k.endswith(".vcd"))


def map_referenced():
    """{normalized SceneFile: [map names]} over every map in the install."""
    KV = re.compile(r'"([^"]*)"\s+"([^"]*)"')
    BLOCK = re.compile(r"\{([^{}]*)\}", re.S)
    refs = defaultdict(list)
    ents = []
    for name in install.all_map_names():
        data = open(install.map_path(name), "rb").read()
        text = bsp.read_lump(data, 0).decode("latin-1")
        for block in BLOCK.findall(text):
            d = defaultdict(list)
            for k, v in KV.findall(block):
                d[k].append(v)
            if d.get("classname", [""])[0] != "logic_choreographed_scene":
                continue
            ents.append((name, {k: v for k, v in d.items()}))
            for sf in d.get("SceneFile", []):
                refs[sf.replace("\\", "/").lower()].append(name)
    return refs, ents


def survey(idx, keys):
    s = {
        "files": len(keys),
        "versions": Counter(),
        "toplevel": Counter(),
        "channels": Counter(),
        "event_types": Counter(),
        "event_keys": defaultdict(Counter),
        "event_flags": defaultdict(Counter),
        "event_subblocks": defaultdict(Counter),
        "actors_per_scene": Counter(),
        "events_per_scene": Counter(),
        "unparsed": [],
        "param_ext": defaultdict(Counter),
        "durations": [],
        "scene_flags": Counter(),
        "channel_flags": Counter(),
        "actor_flags": Counter(),
        "event_names": defaultdict(Counter),
    }
    for k in keys:
        try:
            text = install.read(idx, k).decode("latin-1")
            scene = parse(text)
        except Exception as exc:  # pragma: no cover - corrupt file guard
            s["unparsed"].append((k, str(exc)))
            continue
        s["versions"][scene["version"]] += 1
        actors = 0
        nev = 0
        for st in scene["statements"]:
            w = st["words"]
            if not w:
                continue
            s["toplevel"][w[0]] += 1
            if w[0] == "actor":
                actors += 1
                for c in st["children"]:
                    if c["words"] and c["words"][0] not in ("channel",):
                        s["actor_flags"][c["words"][0]] += 1
            elif w[0] not in ("event",):
                # scene-level setting: record the whole statement shape
                s["scene_flags"][" ".join(w)] += 1
        for actor, chan, ev in list(iter_events(scene)) + list(scene_events(scene)):
            nev += 1
            etype = ev["words"][1] if len(ev["words"]) > 1 else "?"
            ename = ev["words"][2] if len(ev["words"]) > 2 else ""
            s["event_types"][etype] += 1
            s["event_names"][etype][ename] += 1
            if chan not in ("<scene>",):
                s["channels"][chan] += 1
            for c in ev["children"]:
                cw = c["words"]
                if not cw:
                    continue
                if c["children"]:
                    s["event_subblocks"][etype][cw[0]] += 1
                elif len(cw) == 1:
                    s["event_flags"][etype][cw[0]] += 1
                else:
                    s["event_keys"][etype][cw[0]] += 1
                if cw[0] == "time" and len(cw) >= 3:
                    try:
                        s["durations"].append(float(cw[2]) - float(cw[1]))
                    except ValueError:
                        pass
                if cw[0] in ("param", "param2", "param3") and len(cw) >= 2:
                    v = cw[1]
                    ext = os.path.splitext(v)[1].lower() or ("<bare>" if v else "<empty>")
                    s["param_ext"][etype + "." + cw[0]][ext] += 1
        for ch_stmt in scene["statements"]:
            if ch_stmt["words"] and ch_stmt["words"][0] == "actor":
                for c in ch_stmt["children"]:
                    if c["words"] and c["words"][0] == "channel":
                        for cc in c["children"]:
                            if cc["words"] and cc["words"][0] != "event":
                                s["channel_flags"][cc["words"][0]] += 1
        s["actors_per_scene"][actors] += 1
        s["events_per_scene"][nev] += 1
    return s


# ---------------------------------------------------------------------------- CLI

def hist(counter, limit=None):
    items = counter.most_common(limit)
    return "\n".join("    %-28s %d" % (str(k), v) for k, v in items)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--referenced", action="store_true",
                    help="survey only the scenes the maps reference")
    ap.add_argument("--dump", metavar="PATH", help="pretty-print one parsed scene")
    ap.add_argument("--json", metavar="PATH", help="write the full survey as JSON")
    ap.add_argument("--markdown", action="store_true", help="emit the doc tables")
    args = ap.parse_args()

    idx = install.build_index(["sound"])
    keys = collect_vcds(idx)

    if args.dump:
        key = args.dump.replace("\\", "/").lower()
        text = install.read(idx, key).decode("latin-1")
        scene = parse(text)

        def show(node, d=0):
            print("  " * d + " ".join(node["words"]))
            for c in node["children"]:
                show(c, d + 1)
        print("// version", scene["version"])
        for st in scene["statements"]:
            show(st)
        return

    refs, ents = map_referenced()
    resolved = {r for r in refs if r in idx}
    if args.referenced:
        keys = sorted(resolved)

    s = survey(idx, keys)

    if args.markdown:
        print("| Event type | Count | Distinct `name` | Sub-keys |")
        print("|---|---:|---:|---|")
        for t, c in s["event_types"].most_common():
            sub = ", ".join("`%s`" % k for k, _ in s["event_keys"][t].most_common())
            print("| `%s` | %d | %d | %s |" % (t, c, len(s["event_names"][t]), sub or "—"))
        return

    print("scenes surveyed: %d (%s)" %
          (len(keys), "map-referenced only" if args.referenced
           else "every .vcd in the patch-first merge"))
    print("referenced by a logic_choreographed_scene: %d entities, %d distinct SceneFile, "
          "%d resolve" % (len(ents), len(refs), len(resolved)))
    missing = sorted(r for r in refs if r not in idx)
    if missing:
        print("  UNRESOLVED SceneFile (%d):" % len(missing))
        for m in missing:
            print("    %s  <- %s" % (m, ", ".join(sorted(set(refs[m])))))
    print("\nChoreo version:\n%s" % hist(s["versions"]))
    print("\nTop-level statements:\n%s" % hist(s["toplevel"]))
    print("\nScene-level settings (whole statement):\n%s" % hist(s["scene_flags"], 25))
    print("\nActor sub-statements (non-channel):\n%s" % hist(s["actor_flags"]))
    print("\nChannel names:\n%s" % hist(s["channels"]))
    print("\nChannel sub-statements (non-event):\n%s" % hist(s["channel_flags"]))
    print("\nEvent types:\n%s" % hist(s["event_types"]))
    for t, _ in s["event_types"].most_common():
        print("\n  event %s" % t)
        if s["event_keys"][t]:
            print("    keys:\n%s" % hist(s["event_keys"][t]))
        if s["event_flags"][t]:
            print("    flags:\n%s" % hist(s["event_flags"][t]))
        if s["event_subblocks"][t]:
            print("    sub-blocks:\n%s" % hist(s["event_subblocks"][t]))
        names = s["event_names"][t]
        print("    distinct names: %d  e.g. %s" %
              (len(names), ", ".join(repr(n) for n, _ in names.most_common(5))))
        for pk in ("param", "param2", "param3"):
            key = t + "." + pk
            if key in s["param_ext"]:
                print("    %s value shape: %s" %
                      (pk, dict(s["param_ext"][key].most_common(8))))
    if s["durations"]:
        d = sorted(s["durations"])
        print("\nEvent duration (end-start), s: min %.3f  median %.3f  max %.3f  n=%d"
              % (d[0], d[len(d) // 2], d[-1], len(d)))
    if s["unparsed"]:
        print("\nUNPARSED (%d):" % len(s["unparsed"]))
        for k, e in s["unparsed"][:20]:
            print("   ", k, e)

    if args.json:
        def plain(o):
            if isinstance(o, defaultdict) or isinstance(o, Counter):
                return {str(k): plain(v) for k, v in o.items()}
            if isinstance(o, dict):
                return {str(k): plain(v) for k, v in o.items()}
            return o
        with open(args.json, "w") as fh:
            json.dump(plain(s), fh, indent=1, default=str)
        print("\nwrote", args.json)


if __name__ == "__main__":
    main()
