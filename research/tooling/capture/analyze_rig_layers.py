"""Name the animation channels retail composed on each captured frame.

`analyze_rig_pose` records what retail *drew* — a frame's bone matrices beside
the contributions that produced them. This reads those contributions back as
**named channels in the export's own namespace**, so the runtime's own layer
decision can be held against them without the install or the capture tooling.

A contribution names a model and an index, and **the index space depends on
which model it names**:

* a **bank** contribution indexes that bank's own `local_sequence_labels` — the
  positional, undeduped list, because the engine numbers descriptors as they sit
  on disk;
* a **body** contribution indexes the flat global space the body's include tree
  builds, because the body is that space's root. Its own model declares a
  handful of sequences while the numbers run past a thousand, which is what
  makes the two spaces impossible to confuse in practice.

Reading either one through the wrong space yields a label that exists and is
wrong — `#401` is `supershotgun_aim_layer` locally and `steyr_attack_leaning_left`
globally — so the space is chosen by the model's own kind rather than by trying
one and falling back.

**A repeated contribution is structure, not noise.** One pose build recurses
once per autolayer entry, so a clip reached by two paths is accumulated twice;
the weights agree on 99% of repeats. The channel set is therefore the
*deduplicated* (owner, label) set, with the multiplicity reported rather than
discarded.

A channel is **additive** when its clip carries the delta flag — that is the
same test the runtime's own `IsAdditive` makes, and it is what decides which of
the graph's nodes could carry it.

The fixture is written beside the session as `layer_oracle.json`. It is
game-derived and stays under `ELYSIUM_WORK_ROOT`.

Usage:
    uv run elysium research analyze_rig_layers
    uv run elysium research analyze_rig_layers --session <capture directory>
"""

from __future__ import annotations

import argparse
from collections import Counter
import json
from pathlib import Path
import sys
from typing import Any, Callable

REPORT_NAME = "layer_oracle.json"
POSE_REPORT = "pose_oracle.json"
#: How many divergent rows a section keeps verbatim; the counts are complete.
EXAMPLES = 25
#: `StudioSeqDesc` flags bit for a delta (additive) sequence. The runtime's
#: `FElysiumNpcClip::IsAdditive` reads the same bit.
FLAG_DELTA = 0x4
#: Column index of `flags` in a clip-slice row.
ROW_FLAGS = 3


def is_body_model(model: str) -> bool:
    """Whether a contribution's model is the playing body rather than a bank.

    A body sits under `character/pc/` or is the session's own stem model; a bank
    is a shared include. The distinction decides the index space, so it is made
    on the path the capture recorded rather than on a name comparison that a
    stem collision could defeat.
    """
    lowered = model.replace("\\", "/").lower()
    return "/pc/" in lowered or "/npc/" in lowered


class ClipSpace:
    """One body's two index spaces, read once and answered from memory."""

    def __init__(self, stem: str, export_root: Path, load_bank: Callable[[str], list[str]]):
        self.stem = stem
        self.slice = json.loads(
            (export_root / "npc" / "clips" / f"{stem}.json").read_text(encoding="utf-8")
        )
        self._load_bank = load_bank
        self._global: dict[int, tuple[str, str, int]] = {}
        for label, numbers in self.slice.get("seq", {}).items():
            rows = self.slice["clips"][label]
            for index, number in enumerate(numbers):
                if number is None or index >= len(rows):
                    continue
                row = rows[index]
                self._global[int(number)] = (
                    label,
                    self.slice["owners"][row[0]],
                    row[ROW_FLAGS] if len(row) > ROW_FLAGS else 0,
                )

    def _by_label(self, label: str) -> tuple[str, str, int] | None:
        """A label's first row, matched the way retail matches a label."""
        rows = self.slice["clips"].get(label)
        if rows is None:
            lowered = label.lower()
            for spelled, candidate in self.slice["clips"].items():
                if spelled.lower() == lowered:
                    label, rows = spelled, candidate
                    break
        if not rows:
            return None
        row = rows[0]
        return (
            label,
            self.slice["owners"][row[0]],
            row[ROW_FLAGS] if len(row) > ROW_FLAGS else 0,
        )

    def resolve(self, model: str, sequence: int) -> tuple[str, str, int] | None:
        """One contribution -> (label, owner stem, flags), or None if unreadable."""
        if is_body_model(model):
            return self._global.get(int(sequence))
        labels = self._load_bank(model)
        if not 0 <= sequence < len(labels):
            return None
        return self._by_label(labels[sequence])


def channels_for_frame(
    frame: dict[str, Any], space: ClipSpace
) -> tuple[list[dict[str, Any]], int]:
    """A frame's deduplicated channel list, and how many contributions failed to resolve.

    Deduplication is by (owner, label): a clip the autolayer walk reached twice
    is one channel. The weight kept is the first seen, and a repeat that
    disagrees is recorded on the channel so the disagreement is visible rather
    than averaged away.
    """
    seen: dict[tuple[str, str], dict[str, Any]] = {}
    unresolved = 0
    for contribution in frame.get("contributions", ()):
        resolved = space.resolve(contribution["model"], contribution["sequence"])
        if resolved is None:
            unresolved += 1
            continue
        label, owner, flags = resolved
        weight = float(contribution["weight"])
        key = (owner.lower(), label.lower())
        channel = seen.get(key)
        if channel is None:
            seen[key] = {
                "label": label,
                "owner_stem": owner,
                "weight": round(weight, 6),
                "additive": bool(flags & FLAG_DELTA),
                "times_accumulated": 1,
            }
            continue
        channel["times_accumulated"] += 1
        if abs(channel["weight"] - weight) > 1e-4:
            channel["weight_disagreement"] = round(weight, 6)
    return sorted(seen.values(), key=lambda c: (c["owner_stem"], c["label"])), unresolved


def summarise(rows: list[dict[str, Any]]) -> dict[str, Any]:
    """The shape of what retail composed, against what one overlay and one additive holds.

    The runtime's graph carries one overlay node and one `_delta` node, and
    `ResolveLayerAssets` says so out loud when a record declares a second of
    either. This counts how often a captured frame asks for more than that.
    """
    shape = Counter()
    over_overlay = 0
    over_additive = 0
    for row in rows:
        additive = sum(1 for c in row["channels"] if c["additive"])
        plain = len(row["channels"]) - additive
        shape[f"{plain} plain + {additive} additive"] += 1
        # One plain channel is the base the frame already names; a second is the
        # overlay node. Anything past that has nowhere to go.
        over_overlay += 1 if plain > 2 else 0
        over_additive += 1 if additive > 1 else 0
    return {
        "frames": len(rows),
        "shape": dict(sorted(shape.items())),
        "frames_over_one_overlay": over_overlay,
        "frames_over_one_additive": over_additive,
        "frames_beyond_the_graph": sum(
            1
            for row in rows
            if sum(1 for c in row["channels"] if c["additive"]) > 1
            or len(row["channels"]) - sum(1 for c in row["channels"] if c["additive"]) > 2
        ),
    }


def build(session: Path, export_root: Path) -> dict[str, Any]:
    # Imported here rather than at module scope: `install` resolves the VtMB root
    # the moment it loads, and everything above is pure and tested without one.
    from elysium_pipeline.formats import install, mdl_skel

    index = install.build_index(dirs=("models",), verbose=False)
    cache: dict[str, list[str]] = {}

    def load_bank(model: str) -> list[str]:
        key = model.replace("\\", "/").lower()
        if not key.startswith("models/"):
            key = "models/" + key
        if key not in cache:
            data = install.read(index, key)
            cache[key] = mdl_skel.local_sequence_labels(data) if data else []
        return cache[key]

    pose = json.loads((session / POSE_REPORT).read_text(encoding="utf-8"))
    spaces: dict[str, ClipSpace] = {}
    rows: list[dict[str, Any]] = []
    unresolved = 0
    without_slice: Counter = Counter()

    for frame in pose.get("frames", ()):
        if not frame.get("contributions"):
            continue
        stem = frame.get("stem")
        if stem is None:
            continue
        if stem not in spaces:
            try:
                spaces[stem] = ClipSpace(stem, export_root, load_bank)
            except FileNotFoundError:
                without_slice[stem] += 1
                spaces[stem] = None  # type: ignore[assignment]
        space = spaces[stem]
        if space is None:
            continue
        channels, missed = channels_for_frame(frame, space)
        unresolved += missed
        if not channels:
            continue
        state = frame.get("player_state") or {}
        rows.append(
            {
                "stem": stem,
                "curtime": frame.get("curtime"),
                "is_player": bool(frame.get("is_player")),
                "activity": state.get("activity"),
                "sequence": state.get("sequence"),
                "cycle": state.get("cycle"),
                "active_weapon": state.get("active_weapon"),
                "aim_yaw": state.get("aim_yaw"),
                "channels": channels,
            }
        )

    by_stem: dict[str, list[dict[str, Any]]] = {}
    for row in rows:
        by_stem.setdefault(row["stem"], []).append(row)

    report = {
        "schema": "elysium.layer-oracle",
        "version": 1,
        "session": str(session),
        "export_root": str(export_root),
        "contributions_unresolved": unresolved,
        "frames_without_a_clip_slice": dict(without_slice),
        "overall": summarise(rows),
        "by_stem": {stem: summarise(subset) for stem, subset in sorted(by_stem.items())},
        "frames": rows,
    }
    (session / REPORT_NAME).write_text(
        json.dumps(report, indent=1, sort_keys=True) + "\n", encoding="utf-8"
    )
    return report


def print_summary(report: dict[str, Any]) -> None:
    print(f"Layer oracle: {report['session']}")
    overall = report["overall"]
    print(f"  frames with channels    {overall['frames']}")
    print(f"  contributions unresolved {report['contributions_unresolved']}")
    print("  composed shape (plain = base and overlays, additive = deltas):")
    for shape, count in overall["shape"].items():
        print(f"    {shape:<26} {count}")
    print(f"  frames needing a second overlay  {overall['frames_over_one_overlay']}")
    print(f"  frames needing a second additive {overall['frames_over_one_additive']}")
    print(f"  frames beyond what the graph holds {overall['frames_beyond_the_graph']}")
    for stem, summary in report["by_stem"].items():
        print(
            f"    {stem}: {summary['frames']} frames, "
            f"{summary['frames_beyond_the_graph']} beyond the graph"
        )
    if report["frames_without_a_clip_slice"]:
        print(f"  no clip slice: {report['frames_without_a_clip_slice']}")
    print(f"  report {Path(report['session']) / REPORT_NAME}")


def latest_session() -> Path:
    from elysium_pipeline.paths import research_root

    root = research_root() / "frida"
    candidates = sorted(
        (path for path in root.glob("*-life_rig_pose") if (path / POSE_REPORT).is_file()),
        key=lambda path: path.name,
    )
    if not candidates:
        raise FileNotFoundError(
            f"no life_rig_pose session below {root} carries a {POSE_REPORT}; run "
            "`uv run elysium research analyze_rig_pose` first"
        )
    return candidates[-1]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--session", type=Path, help="A capture directory; the newest when omitted."
    )
    parser.add_argument("--export", type=Path, help="The export root; $ELYSIUM_EXPORT_ROOT when omitted.")
    arguments = parser.parse_args()
    session = arguments.session.resolve() if arguments.session else latest_session()
    if not session.is_dir():
        raise NotADirectoryError(session)
    if arguments.export:
        export_root = arguments.export.resolve()
    else:
        from elysium_pipeline.paths import export_root as default_export_root

        export_root = default_export_root()
    report = build(session, export_root)
    print_summary(report)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        raise SystemExit(130)
    except Exception as exc:  # noqa: BLE001 - the CLI reports its own failure
        print(f"WARNING - layer analysis failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
