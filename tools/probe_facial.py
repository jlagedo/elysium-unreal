"""Survey VtMB's facial-animation data across the whole install (roadmap RE20).

Three surfaces, one probe:

  * **the `.mdl` flex chunks** — the studiohdr facial block (flex descs, flex
    controllers, the flex-rule RPN, mouths), the per-mesh `StudioFlex` array and
    its two `StudioVertAnim` encodings, and the `StudioEyeball` array. Every
    struct offset here is validated *by the data*, not asserted: the probe reports
    how many records survive a range/containment check so a bad layout is a number.
  * **the `.lip` phoneme files** — the plain-text sentence documents shipped beside
    the line audio: `WORDS`/phoneme rows with their timings, `CLOSECAPTION`,
    `OPTIONS`.
  * **`expressions/*.txt`** — the phoneme -> flex-controller weight tables that
    turn a `.lip` row into flex-controller values.

The compressed vertex-animation record stores **directions, not deltas**: its two
`u16` slots are byte offsets into a 5314-entry unit-vector table compiled into
`StudioRender.dll`, and the two trailing bytes are magnitudes. `--anorms` extracts
that table from the user's own DLL (nothing is committed) and `--verify` decodes
every flex through it.

Usage:
    python tools/probe_facial.py                 # rollup over the whole install
    python tools/probe_facial.py --markdown      # the doc tables
    python tools/probe_facial.py --model <key>   # dump one model's facial data
    python tools/probe_facial.py --lip <path>    # pretty-print one .lip
    python tools/probe_facial.py --anorms <out>  # write the unit-vector table as JSON
    python tools/probe_facial.py --json <path>   # write the full survey

Findings are written up in ../docs/facial_animation.md (roadmap RE20).
Read-only over the user's own install; produces no runtime intermediate.
"""
import argparse
import json
import math
import os
import struct
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import install
import mdl_skel as S
import vpk

# The struct map lives once, in the decoder the NPC export uses (`mdl_skel`); the probe is
# the survey over it, so a layout fixed here is fixed for the bake too. Re-exported under
# the probe's own names because the doc tables and the CLI quote them.
from mdl_skel import (ANORM_COUNT, ANORM_VA, DELTA_SCALE, FLEX_OPS, FLEX_STRIDE,
                      H_FLEXCTRL, H_FLEXDESC, H_FLEXRULE, H_MOUTH, MESH_STRIDE,
                      MODEL_STRIDE, NDELTA_SCALE, H_NUM_FLEXCTRL, H_NUM_FLEXDESC,
                      H_NUM_FLEXRULE, H_NUM_MOUTH, VA_STRIDE, flex_controllers,
                      flex_descs, flex_rules, mesh_flexes, mouths, read_anorms, vert_anims)

# Located by the same field walk but read only here -- the export has no consumer for them.
H_NUM_IKCHAIN, H_IKCHAIN = 368, 372
H_NUM_POSEPARAM, H_POSEPARAM = 384, 388
H_SURFACEPROP = 392

EYEBALL_STRIDE = 140        # StudioEyeball

_i32 = lambda b, o: struct.unpack_from("<i", b, o)[0]
_u16 = lambda b, o: struct.unpack_from("<H", b, o)[0]
_f32 = lambda b, o: struct.unpack_from("<f", b, o)[0]
_v3 = lambda b, o: struct.unpack_from("<3f", b, o)


def _cstr(b, o):
    return b[o:b.index(b"\0", o)].decode("ascii", "replace")


def _cstr_rel(b, base, field=0):
    """A studio string index stored relative to its own record base."""
    rel = _i32(b, base + field)
    return _cstr(b, base + rel) if rel else ""


# --- the .mdl facial chunks -------------------------------------------------------

def eyeballs(d, model_base):
    """[dict] -- StudioEyeball, stride 140. Empty on every shipped VtMB model."""
    n, rel = _i32(d, model_base + 80), _i32(d, model_base + 84)
    out = []
    for i in range(n):
        b = model_base + rel + i * EYEBALL_STRIDE
        out.append(dict(
            name=_cstr_rel(d, b), bone=_i32(d, b + 4), org=_v3(d, b + 8),
            zoffset=_f32(d, b + 20), radius=_f32(d, b + 24),
            up=_v3(d, b + 28), forward=_v3(d, b + 40),
            texture=_i32(d, b + 52), iris_material=_i32(d, b + 56),
            iris_scale=_f32(d, b + 60), glint_material=_i32(d, b + 64),
            upper_flexdesc=[_i32(d, b + 68 + 4 * q) for q in range(3)],
            lower_flexdesc=[_i32(d, b + 80 + 4 * q) for q in range(3)],
            upper_target=[_f32(d, b + 92 + 4 * q) for q in range(3)],
            lower_target=[_f32(d, b + 104 + 4 * q) for q in range(3)],
            upper_lid_flexdesc=_i32(d, b + 116), lower_lid_flexdesc=_i32(d, b + 120),
            pitch=[_f32(d, b + 124 + 4 * q) for q in range(2)],
            yaw=[_f32(d, b + 132 + 4 * q) for q in range(2)],
        ))
    return out


def walk_models(d):
    """yield (bodypart_index, model_index, model_base)."""
    nbp, bpi = _i32(d, 320), _i32(d, 324)
    for bp in range(nbp):
        bpb = bpi + bp * 16
        nmod, modi = _i32(d, bpb + 4), _i32(d, bpb + 12)
        for m in range(nmod):
            yield bp, m, bpb + modi + m * MODEL_STRIDE


# --- .lip -------------------------------------------------------------------------

def parse_lip(text):
    """Parse a `.lip` sentence document into a dict.

    VERSION <v> / PLAINTEXT { <line> } / WORDS { WORD <w> <start> <end> {
    <code> <phoneme> <start> <end> <volume> [<flag>] } } / EMPHASIS { } /
    CLOSECAPTION { <lang> { PHRASE char <n> "<text>" <start> <end> } } /
    OPTIONS { <key> <value> }
    """
    lines = [l.strip() for l in text.splitlines()]
    out = {"version": None, "plaintext": "", "words": [], "emphasis": [],
           "closecaption": {}, "options": {}}
    if lines and lines[0].upper().startswith("VERSION"):
        out["version"] = lines[0].split()[-1]
    section = lang = None
    word = None
    depth = 0
    for raw in lines[1:]:
        l = raw.strip("\x00").strip()
        if l == "{":
            depth += 1
            continue
        if l == "}":
            depth -= 1
            if section == "WORDS" and depth == 1:
                word = None
            continue
        if not l:
            continue
        if depth == 0:
            section = l.split()[0].upper()
            continue
        if section == "PLAINTEXT":
            out["plaintext"] += (" " if out["plaintext"] else "") + l
        elif section == "WORDS":
            if depth == 1 and l.upper().startswith("WORD"):
                f = l.split()
                word = {"word": " ".join(f[1:-2]), "start": float(f[-2]),
                        "end": float(f[-1]), "phonemes": []}
                out["words"].append(word)
            elif depth >= 2 and word is not None:
                f = l.split()
                if len(f) >= 5:
                    word["phonemes"].append({
                        "code": int(f[0]), "phoneme": f[1], "start": float(f[2]),
                        "end": float(f[3]), "volume": float(f[4]),
                        "flag": int(f[5]) if len(f) > 5 else None})
        elif section == "EMPHASIS":
            out["emphasis"].append(l)
        elif section == "CLOSECAPTION":
            if depth == 1:
                lang = l
                out["closecaption"].setdefault(lang, [])
            elif depth >= 2 and l.upper().startswith("PHRASE"):
                f = l.split()
                # PHRASE char <n> "<text>" <start> <end>
                try:
                    start, end = float(f[-2]), float(f[-1])
                except ValueError:
                    start = end = 0.0
                txt = l[l.find('"') + 1:l.rfind('"')] if '"' in l else ""
                out["closecaption"].setdefault(lang or "", []).append(
                    {"text": txt, "start": start, "end": end})
        elif section == "OPTIONS":
            f = l.split(None, 1)
            out["options"][f[0]] = f[1] if len(f) > 1 else ""
    return out


def index_sound_tree():
    """The merged install as the engine sees it, over `sound/` + `expressions/`
    (install.ASSET_DIRS covers neither)."""
    idx = {k: ("vpk", v) for k, v in vpk.index_all(install.GAME).items()}
    # Retail loose beats the VPKs, the patch beats both -- so walk GAME first.
    for root in [install.GAME] + list(install.LOOSE_ROOTS):
        for sub in ("sound", "expressions"):
            for dp, _, fs in os.walk(os.path.join(root, sub)):
                for f in fs:
                    p = os.path.join(dp, f)
                    idx[os.path.relpath(p, root).replace("\\", "/").lower()] = ("loose", p)
    return idx


# --- expressions/*.txt ------------------------------------------------------------

def parse_expression_table(text):
    """`$keys <name...>` + optional `$hasweighting`, then one row per class:
    `"<name>" "<name>" <floats...> "<description>"`."""
    keys, rows, weighted = [], [], False
    for l in text.splitlines():
        l = l.strip()
        if l.startswith("$keys"):
            keys = l.split()[1:]
        elif l.startswith("$hasweighting"):
            weighted = True
        elif l.startswith('"'):
            parts = l.split('"')
            names = [p for p in parts[1::2]]
            nums = [float(x) for x in " ".join(parts[2::2]).split() if _isnum(x)]
            rows.append((names, nums))
    return keys, weighted, rows


def _isnum(s):
    try:
        float(s)
        return True
    except ValueError:
        return False


# --- the survey -------------------------------------------------------------------

def survey_models(anorms, verbose=True):
    idx = install.build_index(("models",), verbose=verbose)
    keys = sorted(k for k in idx if k.endswith(".mdl"))
    s = dict(models=0, unreadable=0, with_flex=0, with_mouth=0, with_eyeballs=0,
             flexdesc_hist=Counter(), flexctrl_hist=Counter(), flexrule_hist=Counter(),
             ctrl_types=Counter(), flexdesc_names=Counter(), ctrl_names=Counter(),
             op_hist=Counter(), flexes=0, flexes_by_type=Counter(),
             vertanims=0, bad_flexdesc=0, bad_index=0, noncontiguous=0,
             delta_mag={0: [], 1: []}, multi_model_bodyparts=0)
    for k in keys:
        d = install.read(idx, k)
        if not d or d[:4] != b"IDST" or len(d) < 440 or _i32(d, 140) != len(d):
            s["unreadable"] += 1
            continue
        s["models"] += 1
        for bp in range(_i32(d, 320)):
            if _i32(d, _i32(d, 324) + bp * 16 + 4) > 1:
                s["multi_model_bodyparts"] += 1
        nfd = _i32(d, H_NUM_FLEXDESC)
        s["flexdesc_hist"][nfd] += 1
        s["flexctrl_hist"][_i32(d, H_NUM_FLEXCTRL)] += 1
        s["flexrule_hist"][_i32(d, H_NUM_FLEXRULE)] += 1
        if _i32(d, H_NUM_MOUTH):
            s["with_mouth"] += 1
        if not nfd:
            continue
        s["with_flex"] += 1
        for nm in flex_descs(d):
            s["flexdesc_names"][nm] += 1
        for t, nm, _lo, _hi in flex_controllers(d):
            s["ctrl_types"][t] += 1
            s["ctrl_names"][nm] += 1
        for _flex, ops in flex_rules(d):
            for op, _i, _f in ops:
                s["op_hist"][op] += 1
        for _bp, _m, mb in walk_models(d):
            if eyeballs(d, mb):
                s["with_eyeballs"] += 1
            nmesh = _i32(d, mb + 136)
            if not 0 <= nmesh < 64:
                continue
            for mi in range(nmesh):
                mshb = mb + _i32(d, mb + 140) + mi * MESH_STRIDE
                mnv = _i32(d, mshb + 8)
                prev_end = None
                for fx in mesh_flexes(d, mb, mi):
                    s["flexes"] += 1
                    s["flexes_by_type"][fx["vertanimtype"]] += 1
                    if not 0 <= fx["flexdesc"] < nfd:
                        s["bad_flexdesc"] += 1
                        continue
                    stride = VA_STRIDE.get(fx["vertanimtype"])
                    if stride is None or fx["numverts"] <= 0:
                        continue
                    va = fx["base"] + fx["vertindex"]
                    if prev_end is not None and va != prev_end:
                        s["noncontiguous"] += 1
                    prev_end = va + fx["numverts"] * stride
                    for i, dv, _nv in vert_anims(d, fx, anorms):
                        s["vertanims"] += 1
                        if i >= mnv:
                            s["bad_index"] += 1
                        if dv is not None:
                            s["delta_mag"][fx["vertanimtype"]].append(
                                math.sqrt(sum(c * c for c in dv)))
    return s


def survey_lips(sidx):
    keys = sorted(k for k in sidx if k.endswith(".lip"))
    s = dict(files=len(keys), version=Counter(), blocks=Counter(), phonemes=Counter(),
             options=Counter(), langs=Counter(), words=[], duration=[],
             phoneme_fields=Counter(), emphasis_rows=0, unparsed=0, orphans=0)
    audio = set(k[:-4] for k in sidx if k.endswith((".wav", ".mp3")))
    for k in keys:
        try:
            lip = parse_lip(install.read(sidx, k).decode("utf-8", "replace"))
        except Exception:
            s["unparsed"] += 1
            continue
        s["version"][lip["version"]] += 1
        s["emphasis_rows"] += len(lip["emphasis"])
        for lang in lip["closecaption"]:
            s["langs"][lang] += 1
        for opt in lip["options"]:
            s["options"][opt] += 1
        s["words"].append(len(lip["words"]))
        end = 0.0
        for w in lip["words"]:
            end = max(end, w["end"])
            for p in w["phonemes"]:
                s["phonemes"][p["phoneme"]] += 1
                s["phoneme_fields"][6 if p["flag"] is not None else 5] += 1
        s["duration"].append(end)
        if k[:-4] not in audio:
            s["orphans"] += 1
    return s


def survey_expressions(sidx):
    keys = sorted(k for k in sidx if k.startswith("expressions/"))
    txt = [k for k in keys if k.endswith(".txt")]
    s = dict(total=len(keys), vfe=sum(1 for k in keys if k.endswith(".vfe")),
             txt=len(txt), keysets=Counter(), classes=Counter(), weighted=0,
             keynames=Counter())
    for k in txt:
        try:
            keys_, weighted, rows = parse_expression_table(
                install.read(sidx, k).decode("utf-8", "replace"))
        except Exception:
            continue
        s["keysets"][len(keys_)] += 1
        s["weighted"] += 1 if weighted else 0
        s["classes"][len(rows)] += 1
        for nm in keys_:
            s["keynames"][nm] += 1
    return s


def _pct(arr, p):
    if not arr:
        return 0.0
    arr = sorted(arr)
    return arr[min(len(arr) - 1, int(len(arr) * p))]


def rollup(m, l, e):
    print("\n=== .mdl facial chunks ===")
    print(f"  models read           : {m['models']}  (unreadable {m['unreadable']})")
    print(f"  with flex data        : {m['with_flex']}")
    print(f"  with a mouth          : {m['with_mouth']}")
    print(f"  with eyeball data     : {m['with_eyeballs']}")
    print(f"  multi-model bodyparts : {m['multi_model_bodyparts']}")
    print(f"  NumFlexDescs hist     : {m['flexdesc_hist'].most_common(6)}")
    print(f"  NumFlexControllers    : {m['flexctrl_hist'].most_common(6)}")
    print(f"  NumFlexRules          : {m['flexrule_hist'].most_common(6)}")
    print(f"  controller types      : {m['ctrl_types'].most_common()}")
    print(f"  flex records          : {m['flexes']}  by vertanimtype {dict(m['flexes_by_type'])}")
    print(f"  vertex-anim records   : {m['vertanims']}")
    print(f"  flexdesc out of range : {m['bad_flexdesc']}")
    print(f"  vert index out of mesh: {m['bad_index']}")
    print(f"  non-contiguous blocks : {m['noncontiguous']}")
    for t, label in ((1, "compressed"), (0, "raw float")):
        dm = m["delta_mag"][t]
        if dm:
            print(f"  |position delta| ({label}, in): p50={_pct(dm,.5):.4f} "
                  f"p90={_pct(dm,.9):.4f} p99={_pct(dm,.99):.4f} max={max(dm):.4f}")
    print(f"  distinct flexdesc names: {len(m['flexdesc_names'])}")
    print(f"  distinct controller names: {len(m['ctrl_names'])}")
    print(f"  flex-rule opcodes     : {m['op_hist'].most_common()}")

    print("\n=== .lip phoneme files ===")
    print(f"  files                 : {l['files']}  (unparsed {l['unparsed']}, "
          f"no sibling audio {l['orphans']})")
    print(f"  VERSION               : {l['version'].most_common()}")
    print(f"  CLOSECAPTION langs    : {l['langs'].most_common()}")
    print(f"  OPTIONS keys          : {l['options'].most_common()}")
    print(f"  EMPHASIS rows         : {l['emphasis_rows']}")
    print(f"  phoneme row fields    : {l['phoneme_fields'].most_common()}")
    print(f"  words/file            : p50={_pct(l['words'],.5)} p90={_pct(l['words'],.9)} "
          f"max={max(l['words']) if l['words'] else 0}")
    print(f"  line length (s)       : p50={_pct(l['duration'],.5):.2f} "
          f"p90={_pct(l['duration'],.9):.2f} max={max(l['duration']) if l['duration'] else 0:.2f}")
    print(f"  distinct phonemes     : {len(l['phonemes'])}")
    print(f"    {l['phonemes'].most_common(20)}")

    print("\n=== expressions/ ===")
    print(f"  files                 : {e['total']}  (.vfe {e['vfe']} / .txt {e['txt']})")
    print(f"  $keys count histogram : {e['keysets'].most_common(6)}")
    print(f"  $hasweighting         : {e['weighted']}/{e['txt']}")
    print(f"  rows per table        : {e['classes'].most_common(6)}")
    print(f"  distinct key names    : {len(e['keynames'])}")


def markdown(m, l, e):
    print("| Surface | Count |")
    print("|---|---|")
    print(f"| `.mdl` read | {m['models']} |")
    print(f"| …with flex data | {m['with_flex']} |")
    print(f"| …with a `mstudiomouth_t` | {m['with_mouth']} |")
    print(f"| …with eyeball data | {m['with_eyeballs']} |")
    print(f"| `StudioFlex` records | {m['flexes']} "
          f"({m['flexes_by_type'].get(1,0)} compressed / {m['flexes_by_type'].get(0,0)} raw) |")
    print(f"| vertex-animation records | {m['vertanims']} |")
    print(f"| `.lip` files | {l['files']} |")
    print(f"| `expressions/*.vfe` | {e['vfe']} (+{e['txt']} `.txt`) |")
    print()
    print("| Check | Failures |")
    print("|---|---|")
    print(f"| `FlexDesc` index in range | {m['bad_flexdesc']} / {m['flexes']} |")
    print(f"| vertex index inside its mesh | {m['bad_index']} / {m['vertanims']} |")
    print(f"| vertex-anim block contiguous with the flex array | {m['noncontiguous']} / {m['flexes']} |")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--model", metavar="KEY", help="dump one model's facial data")
    ap.add_argument("--lip", metavar="PATH", help="pretty-print one .lip document")
    ap.add_argument("--anorms", metavar="OUT", help="write the unit-vector table as JSON")
    ap.add_argument("--json", metavar="PATH", help="write the full survey as JSON")
    ap.add_argument("--markdown", action="store_true", help="emit the doc tables")
    a = ap.parse_args()

    if a.anorms:
        tbl = read_anorms()
        with open(a.anorms, "w", encoding="utf-8") as fh:
            json.dump({"source": "StudioRender.dll", "va": ANORM_VA,
                       "count": len(tbl), "vectors": tbl}, fh)
        print(f"wrote {len(tbl)} unit vectors -> {a.anorms}")
        return

    if a.lip:
        sidx = index_sound_tree()
        raw = install.read(sidx, a.lip) if a.lip.lower() in sidx else open(a.lip, "rb").read()
        lip = parse_lip(raw.decode("utf-8", "replace"))
        print(json.dumps(lip, indent=2)[:20000])
        return

    if a.model:
        anorms = read_anorms()
        idx = install.build_index(("models",))
        d = install.read(idx, a.model)
        if not d:
            sys.exit(f"not in the install: {a.model}")
        print(f"{a.model}  surfaceprop={_cstr(d, _i32(d, H_SURFACEPROP))!r}")
        fd = flex_descs(d)
        print(f"\nflex descs ({len(fd)}):\n  {fd}")
        print(f"\nflex controllers ({_i32(d, H_NUM_FLEXCTRL)}):")
        for i, (t, nm, lo, hi) in enumerate(flex_controllers(d)):
            print(f"  [{i:2d}] {t:<8s} {nm:<24s} range=[{lo}, {hi}]")
        print(f"\nflex rules ({_i32(d, H_NUM_FLEXRULE)}), first 6:")
        for flex, ops in flex_rules(d)[:6]:
            rpn = " ".join(f"{op}({iv})" if op == "FETCH1" else
                           (f"{op}({fv:g})" if op == "CONST" else op)
                           for op, iv, fv in ops)
            print(f"  {fd[flex] if flex < len(fd) else flex}: {rpn}")
        for bone, fwd, fdesc in mouths(d):
            print(f"\nmouth: bone={bone} forward={tuple(round(x, 3) for x in fwd)} "
                  f"flexdesc={fdesc} ({fd[fdesc] if fdesc < len(fd) else '?'})")
        for bp, m, mb in walk_models(d):
            eyes = eyeballs(d, mb)
            nmesh = _i32(d, mb + 136)
            print(f"\nmodel[{bp}.{m}] {_cstr(d, mb)!r} meshes={nmesh} eyeballs={len(eyes)}")
            for eye in eyes:
                print(f"    eyeball {eye}")
            for mi in range(max(nmesh, 0)):
                fx = mesh_flexes(d, mb, mi)
                if not fx:
                    continue
                print(f"    mesh[{mi}] flexes={len(fx)}")
                for f in fx[:3]:
                    va = vert_anims(d, f, anorms)
                    mag = [math.sqrt(sum(c * c for c in dv)) for _i, dv, _n in va if dv]
                    print(f"      {fd[f['flexdesc']] if f['flexdesc'] < len(fd) else '?':<24s}"
                          f" type={f['vertanimtype']} verts={f['numverts']}"
                          f" targets={[round(t, 3) for t in f['targets']]}"
                          f" |delta| max={max(mag) if mag else 0:.3f} in")
        return

    anorms = read_anorms()
    m = survey_models(anorms)
    sidx = index_sound_tree()
    l = survey_lips(sidx)
    e = survey_expressions(sidx)
    if a.markdown:
        markdown(m, l, e)
    else:
        rollup(m, l, e)
    if a.json:
        out = {"models": {k: (dict(v) if isinstance(v, Counter) else
                              (None if k == "delta_mag" else v)) for k, v in m.items()},
               "lip": {k: (dict(v) if isinstance(v, Counter) else v) for k, v in l.items()},
               "expressions": {k: (dict(v) if isinstance(v, Counter) else v)
                               for k, v in e.items()}}
        out["models"]["delta_mag_percentiles"] = {
            str(t): {"p50": _pct(v, .5), "p90": _pct(v, .9), "p99": _pct(v, .99),
                     "max": max(v) if v else 0}
            for t, v in m["delta_mag"].items()}
        os.makedirs(os.path.dirname(os.path.abspath(a.json)), exist_ok=True)
        with open(a.json, "w", encoding="utf-8") as fh:
            json.dump(out, fh, indent=1)
        print(f"\nwrote {a.json}")


if __name__ == "__main__":
    main()
