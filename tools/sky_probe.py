"""RE-A2 — the labelled-sky probe: author six identifiable sky faces and drop them
into the original game as a loose set, to watch where the engine puts them.

`docs/sky-ambience.md` -> K1 reads the Source sky convention out of `engine.dll`'s own
tables: which texture binds to which world axis, each face's basis, and the `1 - t`
texcoord flip. `probe_sky_orientation.py` corroborates the *relative* half on the
decoded faces. Neither can speak for `dn` — every VtMB ground plate is a uniform black
square, so no seam discriminates it. This probe closes that by making the faces
self-describing and letting the shipped engine draw them.

Each face carries its own suffix in large type, the world axis K1 predicts for it, a
TOP banner and a to-image-up arrow (so a rotation or a mirror reads at a glance), four
tagged corner markers, and the neighbour each edge should meet in the unfolded cross:

          up
     bk   rt   ft   lf
          dn

The set installs as loose `materials/skybox/<skyname>{rt,lf,bk,ft,up,dn}.{tth,ttz}`
under the Unofficial Patch, which the engine searches before the VPKs. A manifest records
every file written and the sha256 it was written with, so `--uninstall` restores the stock
sky by touching only what it put there. Most sky sets live solely in the VPKs, so the drop
purely *adds* files; `pier` is the one set the patch itself ships loose (as a BGR888
re-export), and those originals are moved into `out/_skyprobe/backup/` on install and put
back on uninstall. The `.vmt`s are left alone — they already point at these texture names.

Faces are encoded with the format, flags and mip policy of the face they shadow
(`tex_from_png.encode_like`), so the engine sees the container it expects. `pier` and
`santamonica` are uncompressed BGR888 and round-trip bit-exact; the DXT5 sets (`la`,
`holly`, `chinatown`) take a block-compression hit that the labels easily survive.

    python tools/sky_probe.py                      # render + report, touch nothing
    python tools/sky_probe.py --install            # write the loose set
    python tools/sky_probe.py --uninstall          # remove it again
    python tools/sky_probe.py --skyname la --install
"""
import argparse
import hashlib
import json
import os
import sys

from PIL import Image, ImageDraw, ImageFont

import install
import tex_from_png as tfp
import tex_to_png as t2p

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")
WORK = os.path.join(OUT, "_skyprobe")
SKYBOX_DIR = os.path.join(install.PATCH, "materials", "skybox")

# --- what K1 predicts ------------------------------------------------------
# Source axis each face binds to, the view angles that look down it, its fill
# colour, and the neighbour each image edge meets in the unfolded cross.
# Source angles: yaw 0 = +X increasing toward +Y; pitch is inverted (negative = up).
FACES = {
    "rt": dict(axis="+X", look="yaw 0",     rgb=(198,  48,  44),
               edges=dict(top="up", bottom="dn", left="bk", right="ft")),
    "bk": dict(axis="+Y", look="yaw 90",    rgb=( 46, 150,  70),
               edges=dict(top="up", bottom="dn", left="lf", right="rt")),
    "lf": dict(axis="-X", look="yaw 180",   rgb=( 32, 158, 182),
               edges=dict(top="up", bottom="dn", left="ft", right="bk")),
    "ft": dict(axis="-Y", look="yaw 270",   rgb=(176,  56, 158),
               edges=dict(top="up", bottom="dn", left="rt", right="lf")),
    "up": dict(axis="+Z", look="pitch -90", rgb=( 58,  84, 206),
               edges=dict(top="lf", bottom="rt", left="bk", right="ft")),
    "dn": dict(axis="-Z", look="pitch +90", rgb=(206, 126,  30),
               edges=dict(top="rt", bottom="lf", left="bk", right="ft")),
}
ORDER = ["rt", "bk", "lf", "ft", "up", "dn"]

INK, SHADE = (250, 250, 245), (18, 18, 22)


# --- the label art ---------------------------------------------------------
def _font(px):
    for path in ("C:/Windows/Fonts/arialbd.ttf", "C:/Windows/Fonts/segoeuib.ttf",
                 "C:/Windows/Fonts/DejaVuSans-Bold.ttf", "C:/Windows/Fonts/arial.ttf"):
        try:
            return ImageFont.truetype(path, px)
        except OSError:
            continue
    return ImageFont.load_default(px)


def _text(d, xy, s, px, anchor="mm", fill=INK):
    d.text(xy, s, font=_font(px), fill=fill, anchor=anchor,
           stroke_width=max(1, px // 14), stroke_fill=SHADE)


def _corner(d, box, shape, tag, k):
    """A tagged marker: a distinct shape per corner, so a rotation is unmistakable."""
    x0, y0, x1, y1 = box
    ty = (y0 + y1) / 2
    if shape == "circle":
        d.ellipse(box, fill=INK, outline=SHADE, width=k(3))
    elif shape == "square":
        d.rectangle(box, fill=INK, outline=SHADE, width=k(3))
    elif shape == "triangle":
        d.polygon([((x0 + x1) / 2, y0), (x1, y1), (x0, y1)], fill=INK, outline=SHADE,
                  width=k(3))
        ty = y0 + 0.68 * (y1 - y0)                          # where the triangle is wide
    else:                                                   # diamond
        d.polygon([((x0 + x1) / 2, y0), (x1, (y0 + y1) / 2),
                   ((x0 + x1) / 2, y1), (x0, (y0 + y1) / 2)], fill=INK, outline=SHADE,
                  width=k(3))
    d.text(((x0 + x1) / 2, ty), tag, font=_font(k(26)), fill=SHADE, anchor="mm")


def _edge_label(img, side, text, k):
    """Neighbour tag, rotated so it reads outward from the edge it names."""
    strip = Image.new("RGBA", (k(190), k(52)), (0, 0, 0, 0))
    _text(ImageDraw.Draw(strip), (k(95), k(26)), text, k(38))
    w, h = img.size
    if side == "top":
        img.alpha_composite(strip, (w // 2 - strip.width // 2, k(96)))
    elif side == "bottom":
        img.alpha_composite(strip, (w // 2 - strip.width // 2, h - k(148)))
    elif side == "left":
        s = strip.rotate(90, expand=True)
        img.alpha_composite(s, (k(30), h // 2 - s.height // 2))
    else:
        s = strip.rotate(270, expand=True)
        img.alpha_composite(s, (w - k(30) - s.width, h // 2 - s.height // 2))


def face_image(suffix: str, skyname: str, size: int) -> Image.Image:
    """One labelled face, `size` square. Every mark is asymmetric top-to-bottom and
    left-to-right, so any dihedral transform the engine applies is visible."""
    f = FACES[suffix]
    k = lambda v: max(1, round(v * size / 512))             # noqa: E731  (512-space)
    img = Image.new("RGBA", (size, size), (*f["rgb"], 255))
    d = ImageDraw.Draw(img)

    d.rectangle([k(14), k(14), size - k(15), size - k(15)], outline=INK, width=k(6))
    _text(d, (size / 2, k(52)), "T O P", k(44))

    _text(d, (size / 2, size * 0.44), suffix.upper(), k(196))
    _text(d, (size / 2, size * 0.63), f"{skyname}  {f['axis']}", k(46))
    _text(d, (size / 2, size * 0.71), f"look {f['look']}", k(30))

    # arrow to image-up, drawn low so it cannot be confused with the TOP banner
    cx, y = size / 2, size * 0.86
    d.polygon([(cx, y - k(46)), (cx + k(26), y - k(6)), (cx + k(10), y - k(6)),
               (cx + k(10), y + k(34)), (cx - k(10), y + k(34)),
               (cx - k(10), y - k(6)), (cx - k(26), y - k(6))],
              fill=INK, outline=SHADE, width=k(3))

    m, s = k(22), k(66)
    for (tag, shape, box) in (
            ("TL", "circle",   (m, m, m + s, m + s)),
            ("TR", "square",   (size - m - s, m, size - m, m + s)),
            ("BL", "triangle", (m, size - m - s, m + s, size - m)),
            ("BR", "diamond",  (size - m - s, size - m - s, size - m, size - m))):
        _corner(d, box, shape, tag, k)

    for side, tag in (("top", f"^ {f['edges']['top']}"),
                      ("bottom", f"v {f['edges']['bottom']}"),
                      ("left", f"< {f['edges']['left']}"),
                      ("right", f"{f['edges']['right']} >")):
        _edge_label(img, side, tag, k)
    return img


# --- install / uninstall ---------------------------------------------------
def _sha(b):
    return hashlib.sha256(b).hexdigest()


def manifest_path(skyname):
    return os.path.join(WORK, f"{skyname}.installed.json")


def maps_using(skyname):
    """Exported maps whose `.env` names this skyname."""
    found = []
    if not os.path.isdir(OUT):
        return found
    for name in sorted(os.listdir(OUT)):
        d = os.path.join(OUT, name)
        if name.startswith("_") or not os.path.isdir(d):
            continue
        for fn in os.listdir(d):
            if fn.endswith(".env"):
                with open(os.path.join(d, fn)) as fh:
                    for line in fh:
                        if line.startswith("skyname ") and line.split(None, 1)[1].strip() == skyname:
                            found.append(name)
    return found


def build(skyname, idx):
    """Encode the six faces against the shipped set -> {relative name: bytes}."""
    files, notes = {}, []
    for suf in ORDER:
        base = f"materials/skybox/{skyname}{suf}"
        template = install.read(idx, base + ".tth")
        if template is None:
            raise SystemExit(f"the install has no {base}.tth — is '{skyname}' a real skyname?")
        params = tfp.template_params(template)
        w, h, fmt, _ = t2p.parse_tth(template)
        img = face_image(suf, skyname, w)
        tth, ttz = tfp.encode(img, **params)
        files[f"{skyname}{suf}.tth"] = tth
        files[f"{skyname}{suf}.ttz"] = ttz
        os.makedirs(WORK, exist_ok=True)
        img.convert("RGB").save(os.path.join(WORK, f"{skyname}{suf}.png"))
        notes.append(f"  {skyname}{suf:<3} {w}x{h} fmt={fmt} "
                     f"{'BGR888 (lossless)' if fmt == 3 else 'DXT5'}  "
                     f"tth {len(tth):>6}  ttz {len(ttz):>8}")
    return files, notes


def do_install(skyname, idx, force):
    files, notes = build(skyname, idx)
    print(f"authored six labelled faces for '{skyname}':")
    print("\n".join(notes))
    print(f"  reference PNGs -> {WORK}")

    if _load_manifest(skyname) and not force:
        raise SystemExit(f"'{skyname}' is already installed — --uninstall first "
                         f"(or --force to re-write over it)")

    os.makedirs(SKYBOX_DIR, exist_ok=True)
    backup_dir = os.path.join(WORK, "backup", skyname)
    entries = []
    for name, data in files.items():
        p = os.path.join(SKYBOX_DIR, name)
        backup = None
        if os.path.exists(p):                       # a real loose file: keep the original
            os.makedirs(backup_dir, exist_ok=True)
            backup = os.path.join(backup_dir, name)
            os.replace(p, backup)
        with open(p, "wb") as fh:
            fh.write(data)
        entries.append(dict(path=p, sha256=_sha(data), backup=backup))
    with open(manifest_path(skyname), "w") as fh:
        json.dump(dict(skyname=skyname, files=entries), fh, indent=2)

    shadowed = sum(1 for e in entries if e["backup"])
    print(f"\ninstalled {len(entries)} loose files -> {SKYBOX_DIR}")
    if shadowed:
        print(f"  {shadowed} pre-existing patch file(s) moved aside -> {backup_dir}")
    print(f"manifest -> {manifest_path(skyname)}")


def _load_manifest(skyname):
    p = manifest_path(skyname)
    if not os.path.exists(p):
        return None
    with open(p) as fh:
        return json.load(fh)


def do_uninstall(skyname, force):
    man = _load_manifest(skyname)
    if not man:
        print(f"no manifest for '{skyname}' — nothing recorded as installed")
        return
    removed = kept = restored = 0
    for e in man["files"]:
        if os.path.exists(e["path"]):
            with open(e["path"], "rb") as fh:
                same = _sha(fh.read()) == e["sha256"]
            if not (same or force):
                print(f"  changed since install, kept: {e['path']}")
                kept += 1
                continue
            os.remove(e["path"])
            removed += 1
        if e.get("backup") and os.path.exists(e["backup"]):
            os.replace(e["backup"], e["path"])
            restored += 1
    os.remove(manifest_path(skyname))
    print(f"removed {removed} probe file(s)"
          f"{f', restored {restored} patch original(s)' if restored else ''}"
          f"{f', kept {kept}' if kept else ''} — the stock '{skyname}' sky is back")


def do_status(skyname):
    man = _load_manifest(skyname)
    if not man:
        print(f"'{skyname}': not installed")
        return
    live = sum(os.path.exists(e["path"]) for e in man["files"])
    print(f"'{skyname}': installed, {live}/{len(man['files'])} files present in {SKYBOX_DIR}")


def report(skyname):
    print(f"\nThe prediction (docs/sky-ambience.md -> K1). Facing each Source axis, the "
          f"face\nlabelled with that axis should fill the view, upright and unmirrored:\n")
    print(f"  {'look':<12} {'Source axis':<12} {'expect the face labelled':<26} corners")
    for suf in ORDER:
        f = FACES[suf]
        print(f"  {f['look']:<12} {f['axis']:<12} {suf.upper():<26} "
              f"TL top-left, BR bottom-right")
    maps = maps_using(skyname)
    print(f"\nExported maps drawing '{skyname}': {', '.join(maps) if maps else '(none)'}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--skyname", default="pier",
                    help="the sky set to shadow (default: pier — the Santa Monica "
                         "exterior maps, and uncompressed, so the labels stay sharp)")
    ap.add_argument("--install", action="store_true", help="write the loose set")
    ap.add_argument("--uninstall", action="store_true", help="remove it again")
    ap.add_argument("--status", action="store_true")
    ap.add_argument("--force", action="store_true",
                    help="overwrite/remove files the manifest does not vouch for")
    a = ap.parse_args()

    if a.uninstall:
        do_uninstall(a.skyname, a.force)
        return 0
    if a.status:
        do_status(a.skyname)
        return 0

    idx = install.build_index(verbose=False)
    if a.install:
        do_install(a.skyname, idx, a.force)
    else:
        _, notes = build(a.skyname, idx)
        print(f"rendered six labelled faces for '{a.skyname}' (nothing installed):")
        print("\n".join(notes))
        print(f"  reference PNGs -> {WORK}")
    report(a.skyname)
    return 0


if __name__ == "__main__":
    sys.exit(main())
