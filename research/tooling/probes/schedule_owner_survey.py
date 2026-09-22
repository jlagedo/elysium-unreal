"""The schedule owners, read from the pinned image and checked against the oracle's own table.

`docs/vtmb/npc-ai/schedule-kernel.md` § "The schedule owners and their registrations" carries a
57-row table a worker walked by hand. Nothing re-checks a table like that once it is written, and
this story depends on it being right, so this probe re-derives the same rows from the image through
the `ai-schedule` seam's census and diffs them.

    uv run elysium research schedule_owner_survey            # print the survey
    uv run elysium research schedule_owner_survey --check    # diff it against the oracle, exit 1

It imports the seam's census rather than re-implementing one: two decoders that agree because they
share a bug agree about nothing. What is independent here is the ORACLE, which was written by a
different reader from a different pass.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "pipeline" / "src"))

from elysium_pipeline.formats.ai_schedule_glb import census as census_module  # noqa: E402
from elysium_pipeline.formats.ai_schedule_glb.image import PINNED_SHA256  # noqa: E402
from elysium_pipeline.paths import repo_root, vtmb_root  # noqa: E402

ORACLE = ("docs", "vtmb", "npc-ai", "schedule-kernel.md")
SECTION = "## The schedule owners and their registrations"

#: `| Owner | init body | spaces | parent | txt | registrations | shape |`
_ROW = re.compile(r"^\|\s*`?(?P<owner>[A-Za-z_][A-Za-z0-9_]*)`?\s*\|(?P<rest>.*)\|\s*$")
_REGISTRATIONS = re.compile(r"R\[(?P<count>\d+);")


def _image() -> bytes:
    return (vtmb_root() / "Vampire" / "dlls" / "vampire.dll").read_bytes()


def _oracle_rows() -> dict[str, dict]:
    """The committed table, parsed back out of the oracle page."""

    text = (repo_root().joinpath(*ORACLE)).read_text(encoding="utf-8")
    start = text.find(SECTION)
    if start < 0:
        raise SystemExit(f"the oracle has no section {SECTION!r}")
    body = text[start:]
    rows: dict[str, dict] = {}
    for line in body.splitlines():
        match = _ROW.match(line.strip())
        if not match:
            continue
        owner = match.group("owner")
        if not owner.startswith(("CAI_", "CNPC_")):
            continue
        cells = [cell.strip() for cell in match.group("rest").split("|")]
        texts = None
        for cell in cells:
            head = cell.split(";")[0].strip().strip("`")
            if head.isdigit():
                texts = int(head)
                break
        counts = [int(hit) for hit in _REGISTRATIONS.findall(match.group("rest"))]
        rows[owner] = {"texts": texts, "registrations": counts}
    return rows


def survey() -> dict:
    data = _image()
    census = census_module.build(data)
    return {
        "sha256": hashlib.sha256(data).hexdigest(),
        "matchesPin": hashlib.sha256(data).hexdigest() == PINNED_SHA256,
        "summary": census_module.summary(census),
        "owners": {
            owner.class_name: {
                "initBody": f"{owner.init_body:#010x}",
                "texts": len(owner.texts),
                "registrations": {
                    category: len(owner.registrations_of(category))
                    for category in census_module.CATEGORIES
                },
                "scheduleSpace": (
                    f"{owner.schedule_space:#010x}" if owner.schedule_space else None
                ),
            }
            for owner in census.owners
        },
        "deadDoors": census.dead_doors,
        "anomalies": census.anomalies,
    }


def _print(report: dict) -> None:
    summary = report["summary"]
    print(f"image sha256 {report['sha256']} (pinned: {report['matchesPin']})")
    print(
        f"{summary['initBodies']} init bodies, {summary['feedingOwners']} feeding, "
        f"{summary['texts']} texts, {summary['regionRuns']} region runs, "
        f"{summary['deadDoors']} dead door(s)"
    )
    registrations = summary["registrations"]
    print(
        "registrations: "
        + ", ".join(f"{name} {count}" for name, count in sorted(registrations.items()))
    )
    print()
    print(f"{'Owner':<34}{'init body':>12}{'txt':>6}{'sch':>6}{'task':>6}{'cond':>6}")
    for name, row in sorted(report["owners"].items()):
        counts = row["registrations"]
        print(
            f"{name:<34}{row['initBody']:>12}{row['texts']:>6}"
            f"{counts['schedule']:>6}{counts['task']:>6}{counts['condition']:>6}"
        )
    if report["anomalies"]:
        print()
        for row in report["anomalies"]:
            print(f"  anomaly {row.get('row')}: {row.get('unit', '')} {row.get('name', '')}")


def check(report: dict) -> int:
    """Diff the image against the oracle's committed table. Non-zero on any difference."""

    oracle = _oracle_rows()
    measured = report["owners"]
    status = 0

    missing = sorted(set(oracle) - set(measured))
    extra = sorted(set(measured) - set(oracle))
    for name in missing:
        print(f"CHECK FAILED: the oracle lists {name} and the image has no such owner")
        status = 1
    for name in extra:
        print(f"CHECK FAILED: the image holds {name} and the oracle's table does not list it")
        status = 1

    for name in sorted(set(oracle) & set(measured)):
        want, got = oracle[name], measured[name]
        if want["texts"] is not None and want["texts"] != got["texts"]:
            print(
                f"CHECK FAILED: {name} feeds {got['texts']} text(s); the oracle says "
                f"{want['texts']}"
            )
            status = 1

    total = report["summary"]["texts"]
    if total != 691:
        print(f"CHECK FAILED: the image feeds {total} texts; the oracle attributes 691")
        status = 1
    if not report["matchesPin"]:
        print("CHECK FAILED: this image is not the pinned one, so the oracle's addresses are not its")
        status = 1
    if status == 0:
        print(f"check: the image and the oracle's table agree on {len(oracle)} owner(s)")
    return status


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="diff the image against the oracle's committed table and exit non-zero on a difference",
    )
    args = parser.parse_args(argv)
    report = survey()
    if args.check:
        return check(report)
    _print(report)
    return 0


if __name__ == "__main__":
    sys.exit(main())
