"""Compare the three story-5 automation reports with exact, consumed expectations.

Regression bookkeeping, not proof of behaviour equivalence. Input JSON descriptors contain a
`suites` object mapping all three suite prefixes to index.json paths and a `provenance` object.
Provenance is carried to the output; historical reports without asset/harness pins are references.

    uv run elysium research test_delta --before <run.json> --after <run.json> \
        --expectations <expectations.json> --out <work-root delta.json>
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re

from elysium_pipeline.paths import research_root

SUITES = ("Elysium.Substrate", "Elysium.Content", "Elysium.PlayerWorld")
SLOT_STUB = re.compile(r"\[slot\] (?P<label>[^|]+) \| (?P<address>0x[0-9a-fA-F]{8}) "
                       r"\([^)]*\) \| on (?P<receiver>.*?) \| owner:")


class InvalidReport(ValueError):
    pass


def file_digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def semantic_stub(message: str) -> dict | None:
    match = SLOT_STUB.search(message)
    if not match:
        return None
    receiver = re.search(r"\(([^()]*)\)$", match["receiver"])
    # The old diagnostic carries an address but no slot number/signature. Do not manufacture
    # a declaring slot family from its owner prefix: it can name the flattened Troika owner.
    return {"module": "vampire.dll", "address": match["address"].lower(),
            "receiver_classname": receiver[1] if receiver else None,
            "receiver_diagnostic": match["receiver"], "declaring_slot_family": None}


def load_run(descriptor: Path) -> tuple[dict, dict]:
    run = json.loads(descriptor.read_text(encoding="utf-8-sig"))
    if set(run.get("suites", {})) != set(SUITES) or not isinstance(run.get("provenance"), dict):
        raise InvalidReport("run must name all three suites and explicit provenance")
    tests, reports = {}, {}
    for suite in SUITES:
        path = Path(run["suites"][suite])
        if not path.is_absolute():
            path = descriptor.parent / path
        report = json.loads(path.read_text(encoding="utf-8-sig"))
        if not report.get("tests"):
            raise InvalidReport(f"{suite}: empty/missing test list")
        for key in ("failed", "notRun", "inProcess"):
            if not isinstance(report.get(key), int) or report[key] != 0:
                raise InvalidReport(f"{suite}: {key}={report.get(key)!r}; expectations cannot pass a failed/incomplete run")
        if report.get("succeeded", -1) + report.get("succeededWithWarnings", -1) != len(report["tests"]):
            raise InvalidReport(f"{suite}: success counters disagree with test count")
        for test in report["tests"]:
            name = test.get("fullTestPath", "")
            if not name.startswith(suite + ".") or name in tests:
                raise InvalidReport(f"{suite}: duplicate or foreign test {name!r}")
            if test.get("state") != "Success":
                raise InvalidReport(f"{name}: state={test.get('state')!r}; expectations cannot pass it")
            diagnostics = []
            for entry in test.get("entries", []):
                event = entry.get("event", {})
                kind, message = event.get("type"), event.get("message")
                if not isinstance(kind, str) or not isinstance(message, str):
                    raise InvalidReport(f"{name}: malformed diagnostic")
                if kind in {"Warning", "Error"} or "LogElysiumStub:" in message:
                    diagnostics.append((kind, message))
            tests[name] = diagnostics
        reports[suite] = {"path": str(path.resolve()), "sha256": file_digest(path),
                          "tests": len(report["tests"]), "duration_seconds": report.get("totalDuration")}
    return tests, {"provenance": run["provenance"], "reports": reports,
                   "descriptor_sha256": file_digest(descriptor)}


def inventory(events: list[tuple[str, str]]) -> list[list]:
    return [[kind, text, count] for (kind, text), count in sorted(Counter(events).items())]


def compare(before: dict, after: dict, expectations: dict) -> dict:
    if expectations.get("schema_version") != 1 or set(expectations) != {
            "schema_version", "ordered_tests", "changes"}:
        raise InvalidReport("invalid expectation schema")
    ordered = expectations["ordered_tests"]
    if len(ordered) != len(set(ordered)) or any(name not in after for name in ordered):
        raise InvalidReport("ordered tests must be distinct, exact tests in the after run")
    left = {name: list(events) for name, events in before.items()}
    right = {name: list(events) for name, events in after.items()}
    applied = []
    ids = set()
    for change in expectations["changes"]:
        if any(not isinstance(change.get(key), str) or not change[key].strip()
               for key in ("id", "kind", "test", "evidence", "packet")):
            raise InvalidReport("expectation needs exact test, evidence, packet and id")
        if change["id"] in ids:
            raise InvalidReport("duplicate expectation id")
        ids.add(change["id"])
        name, kind = change["test"], change["kind"]
        if kind == "addition":
            if name in left or name not in right:
                raise InvalidReport(f"unused addition: {name}")
            del right[name]
        elif kind == "removal":
            if name not in left or name in right or not change.get("coverage_disposition"):
                raise InvalidReport(f"unused removal or missing reviewed coverage disposition: {name}")
            del left[name]
        elif kind == "rename":
            target = change.get("to")
            if name not in left or name in right or target in left or target not in right:
                raise InvalidReport(f"unused/ambiguous rename: {name}")
            left[target] = left.pop(name)
        elif kind == "diagnostic_remap":
            old, new = change.get("before"), change.get("after")
            if not isinstance(old, list) or len(old) != 2 or not isinstance(new, list) or len(new) != 2:
                raise InvalidReport("diagnostic remap needs exact [severity, message] pairs")
            old, new = tuple(old), tuple(new)
            if (name not in left or name not in right or old == new or old not in left[name]
                    or new not in right[name] or old[0] != new[0]):
                raise InvalidReport(f"unused diagnostic remap: {name}")
            old_identity, new_identity = semantic_stub(old[1]), semantic_stub(new[1])
            if old_identity != new_identity:
                raise InvalidReport("diagnostic remap changes retail/receiver identity; needs a behaviour delta")
            left[name] = [new if event == old else event for event in left[name]]
        elif kind == "diagnostics":
            if name not in left or name not in right or left[name] == right[name]:
                raise InvalidReport(f"unused diagnostic change: {name}")
            before_value = [list(e) for e in left[name]] if name in ordered else inventory(left[name])
            after_value = [list(e) for e in right[name]] if name in ordered else inventory(right[name])
            if before_value == after_value or change.get("before") != before_value or change.get("after") != after_value:
                raise InvalidReport(f"diagnostic expectation does not exactly match observations: {name}")
            left[name] = list(right[name])
        else:
            raise InvalidReport(f"unknown expectation kind {kind!r}")
        applied.append(change["id"])
    differences = []
    for name in sorted(set(left) | set(right)):
        if name not in left or name not in right:
            differences.append({"test": name, "kind": "addition" if name in right else "removal"})
            continue
        old = [list(e) for e in left[name]] if name in ordered else inventory(left[name])
        new = [list(e) for e in right[name]] if name in ordered else inventory(right[name])
        if old != new:
            differences.append({"test": name, "kind": "diagnostics", "before": old, "after": new,
                                "ordered": name in ordered})
    stub_observations = {}
    for name, events in after.items():
        stubs = [{"identity": semantic_stub(message), "diagnostic": message}
                 for _, message in events if "LogElysiumStub:" in message]
        if stubs:
            stub_observations[name] = stubs
    return {"comparison_passed": not differences, "before_tests": len(before), "after_tests": len(after),
            "applied_expectations": applied, "differences": differences,
            "stub_observations_after": stub_observations,
            "limitations": ["Legacy slot diagnostics omit declaring slot family/signature.",
                            "Only named deterministic tests compare diagnostic order; all compare counts.",
                            "Equal reports do not establish retail equivalence or provenance equivalence."]}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("before", "after", "expectations", "out"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        before, before_provenance = load_run(args.before)
        after, after_provenance = load_run(args.after)
        expectations = json.loads(args.expectations.read_text(encoding="utf-8-sig"))
        result = compare(before, after, expectations)
        result.update(before=before_provenance, after=after_provenance,
                      expectations_sha256=file_digest(args.expectations))
        out = args.out.resolve()
        if not out.is_relative_to(research_root().resolve()):
            raise InvalidReport("generated comparison must stay under the work research root")
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print(f"{'PASS' if result['comparison_passed'] else 'FAIL'}: "
              f"{len(result['differences'])} unmatched differences, {len(result['applied_expectations'])} "
              f"consumed expectations; report {out}")
        return 0 if result["comparison_passed"] else 1
    except (ValueError, OSError) as exc:
        print(f"REFUSED: {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
