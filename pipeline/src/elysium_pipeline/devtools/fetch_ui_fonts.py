"""Fetch the UI typeface set into `Content/Fonts/`.

The UI type system is **Nocturne** (`docs/architecture/ui-architecture.md`): Spectral SC for
small-caps labels, Spectral for body copy, Inter for data and numerals. All three are
SIL OFL 1.1 with **no Reserved Font Name**, so they are redistributable as-is and a
static instance cut from a variable source needs no rename.

Spectral and Spectral SC ship real static weights upstream and are copied verbatim.
Inter ships only a variable file, so the weights we use are instanced out of it with
`fontTools.varLib.instancer` -- deterministic, and re-runnable from this script.

Run once when a face is added or replaced; `uv run elysium export bundle policy` does **not** touch these
(they are static files, not editor-built `.uasset`s). Requires network.

    uv run elysium deps sync
"""
import argparse
import io
import os
import sys
import urllib.request

GF = "https://raw.githubusercontent.com/google/fonts/main/ofl"
DEST = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Content", "Fonts")

# Copied verbatim from upstream: (family dir, filename).
STATIC = [
    ("spectralsc", "SpectralSC-Regular.ttf"),
    ("spectralsc", "SpectralSC-SemiBold.ttf"),
    ("spectral",   "Spectral-Regular.ttf"),
    ("spectral",   "Spectral-Italic.ttf"),
    ("spectral",   "Spectral-SemiBold.ttf"),
]

# Cut from a variable source: (family dir, source file, axis pins, output name).
# opsz is pinned to Inter's text optical size -- the UI never sets it above 28 px in the
# 768-tall virtual canvas, and a pinned axis keeps the asset a plain static face.
INSTANCED = [
    ("inter", "Inter[opsz,wght].ttf", {"wght": 400, "opsz": 20}, "Inter-Regular.ttf"),
    ("inter", "Inter[opsz,wght].ttf", {"wght": 600, "opsz": 20}, "Inter-SemiBold.ttf"),
]

LICENCES = [
    ("spectral",   "OFL-Spectral.txt"),
    ("spectralsc", "OFL-SpectralSC.txt"),
    ("inter",      "OFL-Inter.txt"),
]


def fetch(url):
    req = urllib.request.Request(url, headers={"User-Agent": "elysium-ui-fonts"})
    return urllib.request.urlopen(req, timeout=60).read()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--force", action="store_true", help="re-download files that already exist")
    args = ap.parse_args()

    dest = os.path.normpath(DEST)
    os.makedirs(dest, exist_ok=True)

    def skip(name):
        if os.path.exists(os.path.join(dest, name)) and not args.force:
            print(f"  have    {name}")
            return True
        return False

    for fam, fn in STATIC:
        if skip(fn):
            continue
        data = fetch(f"{GF}/{fam}/{fn}")
        open(os.path.join(dest, fn), "wb").write(data)
        print(f"  copied  {fn}  {len(data) // 1024} KB")

    if any(not os.path.exists(os.path.join(dest, out)) or args.force
           for _, _, _, out in INSTANCED):
        try:
            from fontTools.ttLib import TTFont
            from fontTools.varLib import instancer
        except ImportError:
            sys.exit("fontTools is required to instance Inter; install it in the uv environment")

        cache = {}
        for fam, src, axes, out in INSTANCED:
            if skip(out):
                continue
            if src not in cache:
                cache[src] = fetch(f"{GF}/{fam}/{src.replace('[', '%5B').replace(']', '%5D')}")
            font = TTFont(io.BytesIO(cache[src]))
            instancer.instantiateVariableFont(font, axes, inplace=True, updateFontNames=True)
            path = os.path.join(dest, out)
            font.save(path)
            print(f"  cut     {out}  {os.path.getsize(path) // 1024} KB  {axes}")

    for fam, out in LICENCES:
        if skip(out):
            continue
        data = fetch(f"{GF}/{fam}/OFL.txt")
        open(os.path.join(dest, out), "wb").write(data)
        print(f"  licence {out}")

    print(f"  -> {dest}")


if __name__ == "__main__":
    main()
