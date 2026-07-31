"""Verify the Source sky-face orientation convention (K1) against the decoded faces.

`engine.dll` fixes the convention in three tables read by `R_DrawSkyBox` (0x200880c0)
and `MakeSkyVec` (0x20087f10) — the axis->texture binding, the per-axis basis, and the
texcoord flip. That decompile anchors each face to a world axis; this probe checks the
*relative* half on real data: that the six decoded images really do assemble, with no
rotation or mirror, into the unfolded cross the tables predict.

The predicted cross (image row 0 = top, all six faces upright as decoded):

        [up]
  [bk]  [rt]  [ft]  [lf]      <- the horizon ring, cyclic, left to right
        [dn]

Twelve edge adjacencies follow from it. The probe scores the ring exhaustively
(every cyclic order x per-face mirror) and the poles over all eight dihedral
transforms, and reports the margin between the predicted assembly and the runner-up
so an ambiguous face (a near-flat `up`, a near-black `dn`) shows up as a small margin
rather than a confident wrong answer.

Usage:
    uv run elysium research probe_sky_orientation                # every exported map
    uv run elysium research probe_sky_orientation sp_tutorial_1  # one map
"""
import sys
from itertools import permutations
from pathlib import Path

import numpy as np
from PIL import Image
from elysium_pipeline.paths import export_root

OUT_ROOT = export_root()
RING = ["bk", "rt", "ft", "lf"]     # predicted cyclic order, left to right


# --- the RE'd convention ---------------------------------------------------
# MakeSkyVec builds b = (s*d, t*d, d) and permutes it by st_to_vec[axis]; the
# texcoords are u = (s+1)/2, v = 1 - (t+1)/2. Substituting s = 2u-1, t = 1-2v
# gives the Source-space direction a face's pixel (u, v) looks at:
FACE_DIR = {
    "rt": lambda s, t: (1.0, -s, t),    # axis 0, +X
    "lf": lambda s, t: (-1.0, s, t),    # axis 1, -X
    "bk": lambda s, t: (s, 1.0, t),     # axis 2, +Y
    "ft": lambda s, t: (-s, -1.0, t),   # axis 3, -Y
    "up": lambda s, t: (-t, -s, 1.0),   # axis 4, +Z
    "dn": lambda s, t: (t, -s, -1.0),   # axis 5, -Z
}


def load_faces(texdir: Path) -> dict[str, np.ndarray]:
    faces = {}
    for n in (*RING, "up", "dn"):
        p = texdir / f"sky_{n}.png"
        if not p.exists():
            raise FileNotFoundError(p)
        faces[n] = np.asarray(Image.open(p).convert("RGB")).astype(np.int16)
    return faces


def err(a: np.ndarray, b: np.ndarray) -> float:
    return float(np.abs(a - b).mean())


def score_ring(faces, order, flips) -> float:
    """Total seam error around the four-face horizon loop."""
    arr = [faces[n][:, ::-1] if f else faces[n] for n, f in zip(order, flips)]
    return sum(err(arr[i][:, -1], arr[(i + 1) % 4][:, 0]) for i in range(4))


def best_ring(faces):
    """Every cyclic order x per-face mirror, ranked. `bk` is pinned first and
    unmirrored — the loop is cyclic, so pinning one face costs no generality."""
    out = []
    for perm in permutations([n for n in RING if n != "bk"]):
        order = ["bk", *perm]
        for bits in range(1 << 4):
            flips = [(bits >> i) & 1 for i in range(4)]
            if flips[0]:
                continue
            out.append((score_ring(faces, order, flips), order, flips))
    out.sort(key=lambda r: r[0])
    return out


# --- the eight dihedral transforms of a square image -----------------------
DIHEDRAL = [
    ("id",         lambda a: a),
    ("rot90ccw",   lambda a: np.rot90(a, 1)),
    ("rot180",     lambda a: np.rot90(a, 2)),
    ("rot270ccw",  lambda a: np.rot90(a, 3)),
    ("mirror",     lambda a: a[:, ::-1]),
    ("mirror+90",  lambda a: np.rot90(a[:, ::-1], 1)),
    ("mirror+180", lambda a: np.rot90(a[:, ::-1], 2)),
    ("mirror+270", lambda a: np.rot90(a[:, ::-1], 3)),
]


def score_up(faces, up) -> float:
    """`up`'s four edges against the ring's top rows, per the predicted cross."""
    return (err(up[-1, :], faces["rt"][0, :])
            + err(up[0, ::-1], faces["lf"][0, :])
            + err(up[:, 0], faces["bk"][0, :])
            + err(up[:, -1], faces["ft"][0, ::-1])) / 4.0


def score_dn(faces, dn) -> float:
    """`dn`'s four edges against the ring's bottom rows."""
    return (err(dn[0, :], faces["rt"][-1, :])
            + err(dn[-1, ::-1], faces["lf"][-1, :])
            + err(dn[::-1, 0], faces["bk"][-1, :])
            + err(dn[:, -1], faces["ft"][-1, :])) / 4.0


def best_pole(faces, name, scorer):
    out = [(scorer(faces, t(faces[name])), label) for label, t in DIHEDRAL]
    out.sort(key=lambda r: r[0])
    return out


# Below this ratio the runner-up is inside the noise: the faces carry too little
# detail (a flat gradient sky, a uniformly black ground) for a seam to discriminate.
DECISIVE = 1.10


def verdict(won: float, runner: float, is_predicted: bool) -> tuple[str, bool]:
    """-> (label, counts_as_disagreement). A tie is uninformative, not a mismatch."""
    ratio = runner / won if won > 1e-6 else 1.0
    if ratio < DECISIVE:
        return f"tie      (x{ratio:.2f})", False
    return ("OK       ", False) if is_predicted else ("MISMATCH ", True)


def skyname(map_name: str) -> str:
    for env in (OUT_ROOT / map_name).glob("*.env"):
        for line in env.read_text().splitlines():
            if line.startswith("skyname "):
                return line.split(None, 1)[1].strip()
    return "?"


def probe(map_name: str) -> bool:
    texdir = OUT_ROOT / map_name / "tex"
    try:
        faces = load_faces(texdir)
    except FileNotFoundError:
        return True

    bad = False
    print(f"{map_name}  sky={skyname(map_name)}  {faces['bk'].shape[1]}px faces")

    ranked = best_ring(faces)
    won, order, flips = ranked[0]
    runner = next(r[0] for r in ranked[1:] if r[1] != order or r[2] != flips)
    label, disagrees = verdict(won, runner, order == RING and not any(flips))
    bad |= disagrees
    print(f"  ring  {label} best={'-'.join(order)} flips={''.join(map(str, flips))}"
          f"  err={won:.2f}  runner-up={runner:.2f}")

    for name, scorer in (("up", score_up), ("dn", score_dn)):
        (won, best), (runner, _) = best_pole(faces, name, scorer)[:2]
        label, disagrees = verdict(won, runner, best == "id")
        bad |= disagrees
        print(f"  {name:<4}  {label} best={best:<11}"
              f"  err={won:.2f}  runner-up={runner:.2f}")
    return not bad


def main():
    maps = sys.argv[1:] or sorted(
        p.name for p in OUT_ROOT.iterdir()
        if p.is_dir() and not p.name.startswith("_") and (p / "tex").is_dir())
    ok = True
    for m in maps:
        ok &= probe(m)
    print("\nno map contradicts the RE'd convention" if ok
          else "\nAT LEAST ONE MAP DISAGREES — see MISMATCH above")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
