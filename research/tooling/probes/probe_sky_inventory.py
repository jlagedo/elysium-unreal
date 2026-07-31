"""RE-A7: the full-game sky + ambience inventory (K8).

Ten exported maps are the sample every sky/ambience finding so far rests on. This
scans **every** map in the install and reports, per map, the facts those findings
generalise over:

  * `worldspawn.skyname` and whether the named 2D sky set actually resolves;
  * the `light_environment` count (D6 - the engine takes the FIRST, never the sum)
    and each one's `_light` / `_ambient` / pitch;
  * the WORLDLIGHTS (lump 15) type histogram, so "does this map carry a sky pair"
    (a type-3 skylight and/or a type-5 skyambient) is answered by data;
  * `toolsskybox` face presence - the brushwork that shows the 2D sky at all;
  * the `sky_camera` (count, `scale`, origin) and the RE-A8 3D-skybox split: the
    sky BSP `area`, its face count, and the static props / point entities / brush
    entities / worldlights that live in it;
  * both fog sets - `worldspawn`'s (the world's own) and the `sky_camera`'s (the
    skybox pass's), which differ on most maps (RE-A8 -> B8).

Everything comes from the BSP alone; no engine run, no export. The membership rule
is the engine's own: `area(point_leaf(x)) == area(point_leaf(sky_camera.origin))`,
where `area` is the 9-bit low half of the uint16 at leaf offset 6.

Usage:
    python research/tooling/probes/probe_sky_inventory.py                 # every map (108), rollup
    python research/tooling/probes/probe_sky_inventory.py sp_tutorial_1 sm_hub_1
    python research/tooling/probes/probe_sky_inventory.py --markdown      # + the doc tables
    python research/tooling/probes/probe_sky_inventory.py --verbose       # + a line per map

Writes `$ELYSIUM_EXPORT_ROOT/_sky/inventory.json` (every measured field, per map) so a later
pass can re-table it without re-scanning.
"""
import collections
import json
import os
import re
import struct
import sys
from pathlib import Path

from elysium_pipeline.formats import bsp, install, tex_to_png
from elysium_pipeline.paths import research_root

FS = bsp.FACE_SIZE                      # 104
LEAF_AREA = 6                           # uint16 @6: area = low 9 bits, flags = top 7
MODEL_SIZE = 48                         # dmodel_t: mins[3] maxs[3] origin[3] head ff nf
SKY_FACES = ("rt", "lf", "bk", "ft", "up", "dn")
FOG_KEYS = ("fogenable", "fogblend", "fogdir", "fogcolor", "fogcolor2",
            "fogstart", "fogend")
# dworldlight_t.type (bsp.read_worldlights): 3 = skylight (the sun), 5 = skyambient.
WL_TYPES = {0: "surface", 1: "point", 2: "spot", 3: "sky", 4: "quake", 5: "skyamb"}

OUT_DIR = research_root() / "sky"
SKYPROBE_DIR = research_root() / "skyprobe"


def installed_probe_skies():
    """Skynames whose shipped faces are currently shadowed by `sky_probe.py`'s labelled
    set (RE-A2). Their loose `.tth`/`.ttz` are ours, not the game's, so the texture
    columns for those skies read the probe."""
    if not SKYPROBE_DIR.is_dir():
        return set()
    return {p.name[:-len(".installed.json")] for p in SKYPROBE_DIR.glob("*.installed.json")}


# --- entity lump -------------------------------------------------------------

def parse_ents(data):
    """ENTITIES (lump 0) -> [dict], repeats collapsed (last wins; only `output`-ish
    keys repeat and none of them matter here)."""
    text = bsp.read_lump(data, 0).split(b"\x00")[0].decode("latin-1")
    out = []
    for block in re.findall(r"\{([^{}]*)\}", text, re.S):
        out.append(dict(re.findall(r'"([^"]*)"\s+"([^"]*)"', block)))
    return out


def fog_set(ent):
    """The six-key fog block as a plain dict (missing keys omitted, so `no fogenable`
    stays distinguishable from `fogenable 0`)."""
    return {k: ent[k] for k in FOG_KEYS if k in ent}


def fog_norm(f):
    """The fog block with its numbers parsed, so `500` and `500.0` (Hammer wrote both)
    compare equal. A missing key stays missing — `no fogenable` is not `fogenable 0`."""
    out = {}
    for k, v in f.items():
        nums = re.findall(r"-?\d+(?:\.\d+)?", v)
        out[k] = tuple(float(x) for x in nums) if nums else v
    return out


def fog_same(w, c, keys=("fogenable", "fogstart", "fogend", "fogcolor")):
    w, c = fog_norm(w), fog_norm(c)
    return all(w.get(k) == c.get(k) for k in keys)


def blend_same(w, c):
    """The two-colour blend keys. An absent `fogblend` is `fogblend 0` (blend off), so
    `worldspawn` omitting it while `sky_camera` writes `0` is not a difference; the other
    two only count when both sides state them."""
    w, c = fog_norm(w), fog_norm(c)
    if w.get("fogblend", (0.0,)) != c.get("fogblend", (0.0,)):
        return False
    return all(k not in w or k not in c or w[k] == c[k] for k in ("fogcolor2", "fogdir"))


def vec3(s):
    try:
        v = [float(x) for x in s.split()][:3]
    except (ValueError, AttributeError):
        return None
    return v if len(v) == 3 else None


# --- BSP geometry ------------------------------------------------------------

# The membership rule lives once, in the shared reader, so this probe and the exporter that
# acts on it can never drift apart.
leaf_areas = bsp.leaf_areas
area_faces = bsp.area_faces


def face_materials(data):
    """Counter of {material name upper: face count} over every model-0..N face."""
    import numpy as np
    faces = bsp.read_lump(data, bsp.L_FACES)
    texinfo = bsp.read_lump(data, bsp.L_TEXINFO)
    texdata = bsp.read_lump(data, bsp.L_TEXDATA)
    table = np.frombuffer(bsp.read_lump(data, bsp.L_TEXDATA_STR_TABLE), dtype=np.int32)
    blob = bsp.strings_from_blob(bsp.read_lump(data, bsp.L_TEXDATA_STR_DATA))

    names = []
    for td in range(len(texdata) // bsp.TEXDATA_SIZE):
        sid = struct.unpack_from("<i", texdata, td * bsp.TEXDATA_SIZE + bsp.TD_NAMEID)[0]
        names.append(blob.get(int(table[sid]), "") if 0 <= sid < len(table) else "")
    ti_td = [struct.unpack_from("<i", texinfo, t * bsp.TEXINFO_SIZE + bsp.TI_TEXDATA)[0]
             for t in range(len(texinfo) // bsp.TEXINFO_SIZE)]

    ti = np.frombuffer(faces, dtype=np.int16)[bsp.TI_OFS // 2::FS // 2]
    out = collections.Counter()
    for t in ti:
        t = int(t)
        if 0 <= t < len(ti_td):
            td = ti_td[t]
            if 0 <= td < len(names):
                out[names[td].upper()] += 1
    return out


def model_centres(data):
    """{model index: bbox centre} from MODELS (lump 14, 48B).

    The centre is model-local: vbsp re-centres a brush entity that carries an `origin`
    key (every mover — the sky's `func_rotating` ferris wheel sits at (0, 0, -15) here),
    so a brush entity's world point is this centre PLUS its `origin`. For a static brush
    entity the key is absent and the stored bbox is already world-space."""
    raw = bsp.read_lump(data, bsp.L_MODELS)
    out = {}
    for i in range(len(raw) // MODEL_SIZE):
        mins = struct.unpack_from("<3f", raw, i * MODEL_SIZE)
        maxs = struct.unpack_from("<3f", raw, i * MODEL_SIZE + 12)
        out[i] = tuple((a + b) / 2.0 for a, b in zip(mins, maxs))
    return out


def static_props(data):
    """GAME_LUMP `sprp` -> [(model_path, origin)]. Same v4 parse as the exporter's
    `write_props`, minus the decode."""
    gl = bsp.read_game_lump(data)
    if "sprp" not in gl:
        return []
    version, payload = gl["sprp"]
    p = 0
    name_count = struct.unpack_from("<i", payload, p)[0]; p += 4
    names = [payload[p + i * 128:p + i * 128 + 128].split(b"\0", 1)[0].decode("ascii", "replace")
             for i in range(name_count)]
    p += name_count * 128
    leaf_count = struct.unpack_from("<i", payload, p)[0]; p += 4
    p += leaf_count * (4 if version >= 12 else 2)
    prop_count = struct.unpack_from("<i", payload, p)[0]; p += 4
    if not prop_count:
        return []
    size = (len(payload) - p) // prop_count
    out = []
    for i in range(prop_count):
        po = p + i * size
        origin = struct.unpack_from("<3f", payload, po)
        pt = struct.unpack_from("<H", payload, po + 24)[0]
        out.append((names[pt].replace("\\", "/").lower() if pt < len(names) else "?", origin))
    return out


# --- per-map scan ------------------------------------------------------------

def scan(name, data):
    import numpy as np
    row = {"map": name}
    ents = parse_ents(data)
    by_class = collections.defaultdict(list)
    for e in ents:
        by_class[e.get("classname", "")].append(e)

    ws = by_class.get("worldspawn", [{}])[0]
    row["skyname"] = (ws.get("skyname") or "").lower() or None
    row["ws_fog"] = fog_set(ws)

    # light_environment: the sky pair's authoring entity. D6/RE-A3 = first-wins.
    lenv = by_class.get("light_environment", [])
    row["light_environment"] = len(lenv)
    row["light_environment_rows"] = [
        {"pitch": e.get("pitch"), "angles": e.get("angles"),
         "_light": e.get("_light"), "_ambient": e.get("_ambient")} for e in lenv]

    # WORLDLIGHTS type histogram; the sky pair is types 3 (sun) and 5 (skyambient).
    wl = bsp.read_worldlights(data)
    hist = collections.Counter(l["type"] for l in wl)
    row["worldlights"] = len(wl)
    row["wl_hist"] = {WL_TYPES.get(k, str(k)): v for k, v in sorted(hist.items())}
    row["sky_pair"] = (hist.get(3, 0), hist.get(5, 0))
    row["skyamb_intensity"] = [list(l["intensity"]) for l in wl if l["type"] == 5]
    row["skylight_intensity"] = [list(l["intensity"]) for l in wl if l["type"] == 3]

    # toolsskybox brushwork: what actually shows the 2D sky.
    mats = face_materials(data)
    row["toolsskybox_faces"] = sum(c for m, c in mats.items() if "SKYBOX" in m)
    row["faces"] = sum(mats.values())

    # sky_camera + the RE-A8 area split.
    scams = by_class.get("sky_camera", [])
    row["sky_camera"] = len(scams)
    row["sky_scale"] = None
    row["sky_area"] = None
    row["areas"] = int(len(set(leaf_areas(data).tolist())))
    row["area_faces"] = 0
    row["sky_props"] = row["sky_point_ents"] = row["sky_brush_ents"] = 0
    row["sky_worldlights"] = 0
    row["sky_prop_models"] = {}
    row["sky_ent_classes"] = {}
    row["cam_fog"] = {}

    if scams:
        cam = scams[0]
        row["cam_fog"] = fog_set(cam)
        try:
            row["sky_scale"] = float(cam.get("scale", 16))
        except ValueError:
            row["sky_scale"] = None
        origin = vec3(cam.get("origin", ""))
        if origin:
            nodes = bsp.read_lump(data, bsp.L_NODES)
            planes = np.frombuffer(bsp.read_lump(data, bsp.L_PLANES),
                                   dtype=np.float32).reshape(-1, 5)
            areas = leaf_areas(data)

            def area_of(pt):
                li = bsp.point_leaf(data, pt, nodes, planes)
                return int(areas[li]) if 0 <= li < len(areas) else -1

            sky_area = area_of(origin)
            row["sky_area"] = sky_area
            row["area_faces"] = len(area_faces(data, areas, sky_area))

            props = static_props(data)
            in_sky = [m for m, o in props if area_of(o) == sky_area]
            row["sky_props"] = len(in_sky)
            row["sky_prop_models"] = dict(collections.Counter(in_sky).most_common())

            centres = model_centres(data)
            classes = collections.Counter()
            for e in ents:
                cls = e.get("classname", "")
                if cls == "worldspawn":
                    continue
                mdl = e.get("model", "")
                if mdl.startswith("*"):                   # brush entity
                    pt = centres.get(int(mdl[1:]) if mdl[1:].isdigit() else -1)
                    org = vec3(e.get("origin", "")) or (0.0, 0.0, 0.0)
                    pt = tuple(a + b for a, b in zip(pt, org)) if pt else None
                    kind = "brush"
                else:
                    pt = vec3(e.get("origin", ""))
                    kind = "point"
                if pt is None or area_of(pt) != sky_area:
                    continue
                classes[cls] += 1
                row["sky_brush_ents" if kind == "brush" else "sky_point_ents"] += 1
            row["sky_ent_classes"] = dict(classes.most_common())

            row["sky_worldlights"] = sum(1 for l in wl if area_of(l["origin"]) == sky_area)

    return row


def sky_texture_set(idx, skyname):
    """Does `materials/skybox/<skyname><face>` resolve, and at what size?
    -> (faces_present, "WxH" of the `rt` face or None, loose_faces)."""
    got, dims, loose = 0, None, 0
    for face in SKY_FACES:
        key = f"materials/skybox/{skyname}{face}"
        tth = install.read(idx, key + ".tth")
        if not tth:
            continue
        got += 1
        if idx.get(key + ".tth", ("", ""))[0] == "loose":
            loose += 1
        if face == "rt" or dims is None:
            try:
                w, h, _, _ = tex_to_png.parse_tth(tth)
                dims = f"{w}x{h}"
            except Exception:                              # noqa: BLE001
                pass
    return got, dims, loose


# --- reporting ---------------------------------------------------------------

def fog_str(f):
    if not f:
        return "-"
    if "fogenable" not in f:
        return "no fogenable"
    n = fog_norm(f)
    num = lambda k: "%g" % n[k][0] if k in n else "-"
    return f"{num('fogenable')}, {num('fogstart')}→{num('fogend')}, `{f.get('fogcolor', '-')}`"


def fog_on(f):
    """`fogenable` as a tri-state: True/False, or None when the key is absent."""
    if "fogenable" not in f:
        return None
    v = fog_norm(f)["fogenable"]
    return bool(v[0]) if isinstance(v, tuple) else bool(v)


def markdown(rows, idx):
    print("\n<!-- RE-A7 table A: per-map inventory -->\n")
    print("| Map | `skyname` | sky brush | `light_env` | sun/amb | worldlights | "
          "`sky_camera` | sky area | props | ents | lights in sky | fog ws/cam |")
    print("|---|---|---|---|---|---|---|---|---|---|---|---|")
    for r in sorted(rows, key=lambda r: r["map"]):
        s3, s5 = r["sky_pair"]
        f = {True: "on", False: "off", None: "–"}
        fogs = f"{f[fog_on(r['ws_fog'])]}/{f[fog_on(r['cam_fog'])] if r['sky_camera'] else '–'}"
        area = "–" if r["sky_area"] is None else f"{r['sky_area']}/{r['areas']}"
        ents = r["sky_point_ents"] + r["sky_brush_ents"]
        print(f"| `{r['map']}` | `{r['skyname'] or '–'}` | {r['toolsskybox_faces']} | "
              f"{r['light_environment']} | {s3}/{s5} | {r['worldlights']} | "
              f"{r['sky_camera']} | {area} | {r['sky_props'] or '–'} | {ents or '–'} | "
              f"{r['sky_worldlights'] or '–'} | {fogs} |")

    print("\n<!-- RE-A7 table B: skyname inventory -->\n")
    per_sky = collections.defaultdict(list)
    for r in rows:
        if r["skyname"]:
            per_sky[r["skyname"]].append(r["map"])
    probed = installed_probe_skies()
    print("| `skyname` | maps | faces | size | maps that show it (`toolsskybox`) |")
    print("|---|---|---|---|---|")
    for sky, maps in sorted(per_sky.items(), key=lambda kv: (-len(kv[1]), kv[0])):
        got, dims, loose = sky_texture_set(idx, sky)
        shows = sum(1 for r in rows if r["skyname"] == sky and r["toolsskybox_faces"])
        note = " *(probe installed)*" if sky in probed else ""
        print(f"| `{sky}` | {len(maps)} | {got}/6 | {dims or '?'}{note} | {shows} |")

    print("\n<!-- RE-A7 table C: the sky-pair maps (types 3 + 5) -->\n")
    print("| Map | `light_env` | `_light` | `_ambient` | lump-15 type 3 | lump-15 type 5 |")
    print("|---|---|---|---|---|---|")
    for r in sorted(rows, key=lambda r: r["map"]):
        if not any(r["sky_pair"]):
            continue
        e = (r["light_environment_rows"] or [{}])[0]
        v3 = r["skylight_intensity"][0] if r["skylight_intensity"] else None
        v5 = r["skyamb_intensity"][0] if r["skyamb_intensity"] else None
        fmt = lambda v: "`%s`" % " ".join("%g" % x for x in v) if v else "–"
        print(f"| `{r['map']}` | {r['light_environment']} | `{e.get('_light', '–')}` | "
              f"`{e.get('_ambient', '–')}` | {fmt(v3)} | {fmt(v5)} |")

    print("\n<!-- RE-A7 table D: fog — the maps where the two sets disagree -->\n")
    print("| Map | `worldspawn` (enable, start→end, colour) | `sky_camera` | verdict |")
    print("|---|---|---|---|")
    for r in sorted(rows, key=lambda r: r["map"]):
        w, c = r["ws_fog"], r["cam_fog"]
        if not r["sky_camera"]:
            continue
        same = fog_same(w, c)
        if same and blend_same(w, c):
            continue
        verdict = "differ only in the blend keys" if same else "**differ**"
        if fog_on(w) is None and fog_on(c):
            verdict = "**we fog a map whose `worldspawn` does not**"
        print(f"| `{r['map']}` | {fog_str(w)} | {fog_str(c)} | {verdict} |")


def main(argv):
    verbose = "--verbose" in argv
    as_md = "--markdown" in argv
    argv_names = [a for a in argv if not a.startswith("-")]
    names = argv_names or None
    idx = install.build_index()
    if not names:
        names = install.all_map_names()

    rows, failed = [], []
    for nm in names:
        data = install.read(idx, "maps/%s.bsp" % nm)
        if not data:
            failed.append(nm)
            continue
        try:
            r = scan(nm, data)
        except Exception as e:                              # noqa: BLE001
            print("  ! %s: %s" % (nm, e))
            failed.append(nm)
            continue
        rows.append(r)
        if verbose:
            s3, s5 = r["sky_pair"]
            print("%-22s sky=%-14s lenv=%d pair=%d/%d wl=%-4d skybrush=%-4d cam=%d "
                  "area=%s props=%d ents=%d lights=%d"
                  % (nm, r["skyname"] or "-", r["light_environment"], s3, s5,
                     r["worldlights"], r["toolsskybox_faces"], r["sky_camera"],
                     r["sky_area"], r["sky_props"],
                     r["sky_point_ents"] + r["sky_brush_ents"], r["sky_worldlights"]))

    n = len(rows)
    print("\n=== RE-A7: full-game sky + ambience inventory — %d maps ===" % n)
    if failed:
        print("  ! unreadable: %s" % ", ".join(failed))

    with_sky = [r for r in rows if r["skyname"]]
    with_brush = [r for r in rows if r["toolsskybox_faces"]]
    with_cam = [r for r in rows if r["sky_camera"]]
    with_pair = [r for r in rows if any(r["sky_pair"])]
    print("\n-- coverage --")
    print("  maps with a `skyname`            %3d / %d" % (len(with_sky), n))
    print("  maps with toolsskybox faces      %3d" % len(with_brush))
    print("  maps with a sky_camera           %3d" % len(with_cam))
    print("  maps with a sky pair (type 3/5)  %3d  (type3 %d, type5 %d)"
          % (len(with_pair), sum(1 for r in rows if r["sky_pair"][0]),
             sum(1 for r in rows if r["sky_pair"][1])))
    print("  maps with a skyname but NO pair  %3d"
          % sum(1 for r in rows if r["skyname"] and not any(r["sky_pair"])))
    print("  maps with a pair but NO skyname  %3d"
          % sum(1 for r in rows if not r["skyname"] and any(r["sky_pair"])))
    print("  maps with a skyname but no sky brushwork  %3d"
          % sum(1 for r in rows if r["skyname"] and not r["toolsskybox_faces"]))

    # The C2 cross-tab: showing the sky and being lit by it are independent facts.
    print("\n-- shows the sky (toolsskybox) x lit by it (type 3/5 pair) --")
    for shows in (True, False):
        for pair in (True, False):
            sel = [r for r in rows
                   if bool(r["toolsskybox_faces"]) == shows and bool(any(r["sky_pair"])) == pair]
            print("  brush %-3s + pair %-3s  %3d maps%s"
                  % ("yes" if shows else "no", "yes" if pair else "no", len(sel),
                     ("   e.g. " + ", ".join(r["map"] for r in sel[:4])) if sel else ""))
    print("  sky_camera without a pair: %d   pair without a sky_camera: %d"
          % (sum(1 for r in rows if r["sky_camera"] and not any(r["sky_pair"])),
             sum(1 for r in rows if any(r["sky_pair"]) and not r["sky_camera"])))

    print("\n-- skyname distribution --")
    probed = installed_probe_skies()
    if probed:
        print("  ! sky_probe.py's labelled set is installed for: %s — the texture columns"
              " for those skies read the probe, not the shipped faces" % ", ".join(sorted(probed)))
    per_sky = collections.Counter(r["skyname"] for r in rows if r["skyname"])
    for sky, c in per_sky.most_common():
        got, dims, loose = sky_texture_set(idx, sky)
        print("  %-16s %3d maps   faces %d/6%s  %s"
              % (sky, c, got, " (loose)" if loose else "", dims or "?"))

    print("\n-- light_environment count --")
    for k, c in sorted(collections.Counter(r["light_environment"] for r in rows).items()):
        print("  %d light_environment: %3d maps" % (k, c))
    multi = [r for r in rows if r["light_environment"] > 1]
    print("  multi-light_environment maps (D6 / C0a — engine takes the FIRST):")
    for r in sorted(multi, key=lambda r: -r["light_environment"]):
        print("    %-22s %d" % (r["map"], r["light_environment"]))
        for e in r["light_environment_rows"]:
            print("        pitch=%-8s angles=%-14s _light=%-20s _ambient=%s"
                  % (e["pitch"], e["angles"], e["_light"], e["_ambient"]))

    print("\n-- worldlight type histogram (all maps) --")
    tot = collections.Counter()
    for r in rows:
        tot.update(r["wl_hist"])
    for k, v in sorted(tot.items(), key=lambda kv: -kv[1]):
        print("  %-8s %7d" % (k, v))
    print("  maps with >1 type-5 skyambient: %s"
          % ", ".join("%s(%d)" % (r["map"], r["sky_pair"][1])
                      for r in rows if r["sky_pair"][1] > 1) or "none")

    print("\n-- 3D skybox (RE-A8 columns) --")
    print("  sky_camera `scale` values: %s"
          % dict(collections.Counter(r["sky_scale"] for r in with_cam)))
    print("  maps with >1 sky_camera: %s"
          % ", ".join(r["map"] for r in rows if r["sky_camera"] > 1) or "none")
    print("  sky-area content totals: props %d, point ents %d, brush ents %d, worldlights %d"
          % (sum(r["sky_props"] for r in rows), sum(r["sky_point_ents"] for r in rows),
             sum(r["sky_brush_ents"] for r in rows), sum(r["sky_worldlights"] for r in rows)))
    ent_tot = collections.Counter()
    for r in rows:
        ent_tot.update(r["sky_ent_classes"])
    print("  sky-area entity classes: %s"
          % ", ".join("%s %d" % (k, v) for k, v in ent_tot.most_common(12)))
    worst = sorted(rows, key=lambda r: -r["sky_worldlights"])[:10]
    print("  most sky-area worldlights (out of the map's total):")
    for r in worst:
        if r["sky_worldlights"]:
            print("    %-22s %4d / %-5d" % (r["map"], r["sky_worldlights"], r["worldlights"]))

    print("\n-- fog --")
    same = differ = only_cam = only_ws = neither = 0
    for r in rows:
        w, c = r["ws_fog"], r["cam_fog"]
        if not w and not c:
            neither += 1
        elif not c:
            only_ws += 1
        elif not w:
            only_cam += 1
        elif fog_same(w, c):
            same += 1
        else:
            differ += 1
    print("  worldspawn == sky_camera: %d   differ: %d   sky_camera only: %d   "
          "worldspawn only: %d   neither: %d" % (same, differ, only_cam, only_ws, neither))
    print("  maps where we fog a map whose worldspawn has no fogenable:")
    for r in rows:
        if r["cam_fog"].get("fogenable") == "1" and "fogenable" not in r["ws_fog"]:
            print("    %-22s cam %s" % (r["map"], fog_str(r["cam_fog"])))
    # The exporter sources `.env` fog from the sky_camera alone, so a map without one
    # exports with no fog at all, whatever worldspawn says (B8).
    dropped = [r for r in rows if not r["sky_camera"] and fog_on(r["ws_fog"])]
    print("  maps with worldspawn fog ON and NO sky_camera (we export no fog): %d"
          % len(dropped))
    for r in dropped:
        print("    %-22s ws %s" % (r["map"], fog_str(r["ws_fog"])))

    if as_md:
        markdown(rows, idx)

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    # A named-map run is a spot check; it must not overwrite the whole-game inventory.
    out = OUT_DIR / ("inventory.json" if not argv_names else "inventory-partial.json")
    with open(out, "w", encoding="utf-8") as f:
        json.dump(rows, f, indent=1)
    print("\nwrote %s (%d maps)" % (os.path.relpath(out), len(rows)))


if __name__ == "__main__":
    main(sys.argv[1:])
