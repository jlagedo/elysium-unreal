"""Copy the game's sign/popup definitions into $ELYSIUM_EXPORT_ROOT/signs.

P4 4.10 (PL5c) asset-delivery step. A `game_sign` / `prop_sign` entity names a
`definition_file` -- `vdata/Signs/<name>.txt`, a Source KeyValues panel rooted `SignData`
that the engine draws as a full-screen window (`CSignUI` in `client.dll`). This step
mirrors those definitions **verbatim** (no parse, no transcode). Their `BackgroundImage`
materials are the texture lane's `T_` assets: the runtime resolves `BackgroundImage.Name` to the
imported texture by the same install path, so nothing is decoded here any more.

Whole-game, not map-scoped -- one mirror, like the script/dialogue copy (PL2). Resolution
is patch-first (patch loose > retail loose > VPK), matching the engine's search order.

Output is map-independent, so it lands in the global `$ELYSIUM_EXPORT_ROOT/signs/` folder (next to the
per-map `$ELYSIUM_EXPORT_ROOT/<map>/` dirs), read 1:1 by the runtime.

Definition names are written **lowercased** (the install index is case-folded, and a
`definition_file` keyvalue's authored case varies); the runtime lowercases its lookup to
match, so a packaged case-sensitive filesystem behaves like Windows.

Produces (under $ELYSIUM_EXPORT_ROOT/signs/):
  <name>.txt          each sign definition, byte-for-byte

Source bytes stay the user's install; the output is gitignored and regenerable.

Usage:
  uv run elysium export bundle signs            # copy definitions
  uv run elysium export bundle signs --force    # redo files already present
"""
import os
import sys

from elysium_pipeline.formats import install
from elysium_pipeline.formats.install import read
from elysium_pipeline.paths import export_root

OUT = os.path.join(os.fspath(export_root()), "signs")

SIGN_DIR = "vdata/signs/"


def collect_definitions(idx):
    """{dest filename (lowercased) -> install key} for every `vdata/Signs/*.txt`."""
    picked = {}
    for key in idx:
        if key.startswith(SIGN_DIR) and key.endswith(".txt"):
            picked[key[len(SIGN_DIR):]] = key
    return picked


def main(force=False, index=None):
    os.makedirs(OUT, exist_ok=True)
    # A shared full install index is a superset whose extra loose dirs fall outside every
    # prefix this exporter filters on, so it resolves identically to the scoped build.
    if index is None:
        print("indexing install...")
        index = install.build_index(dirs=("vdata",))
    idx = index

    # --- definitions, verbatim -------------------------------------------------------
    picked = collect_definitions(idx)
    written = cached = 0
    for name in sorted(picked):
        data = read(idx, picked[name])
        if data is None:
            continue
        dest = os.path.join(OUT, name)
        if not force and os.path.exists(dest):
            cached += 1
            continue
        with open(dest, "wb") as f:
            f.write(data)
        written += 1
    print(f"[signs] {len(picked)} definitions ({written} copied, {cached} already present)")
    print(f"wrote signs -> {os.path.normpath(OUT)}")


if __name__ == "__main__":
    main(force="--force" in sys.argv[1:])
