"""Build a repeatable, local Ghidra context pack from a tracked RE specification.

The analyzed Ghidra project and every binary-derived dump remain under
``$ELYSIUM_WORK_ROOT/research/ghidra/`` and are intentionally gitignored.  A specification outside that
tree preserves the seed addresses, working aliases, call edges, confidence, and
open questions needed to reconstruct an investigation after a re-import.

The local Ghidra workspace supplies ``run.ps1`` plus ``DumpFuncs``, ``DumpAsm``,
and ``DumpXrefs``.  This driver serializes their invocations and enforces the
post-process lock gap documented by ``research/tooling/ghidra/driver/README.md``.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import subprocess
import sys
import time
from pathlib import Path
from elysium_pipeline.paths import repo_root, research_root


KINDS = ("funcs", "asm", "xrefs", "fields", "vtables", "grep")


def _load_spec(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as stream:
        spec = json.load(stream)
    if spec.get("version") != 1:
        raise ValueError("only context specification version 1 is supported")
    for field in ("topic", "program", "binary", "functions"):
        if not spec.get(field):
            raise ValueError(f"missing required field: {field}")
    binary = spec["binary"]
    for field in ("size", "sha256", "md5", "image_base"):
        if not binary.get(field):
            raise ValueError(f"missing required binary field: {field}")
    if len(binary["sha256"]) != 64 or any(ch not in "0123456789abcdef" for ch in binary["sha256"]):
        raise ValueError("binary sha256 must be 64 lowercase hexadecimal characters")
    if len(binary["md5"]) != 32 or any(ch not in "0123456789abcdef" for ch in binary["md5"]):
        raise ValueError("binary md5 must be 32 lowercase hexadecimal characters")
    seen = set()
    for function in spec["functions"]:
        address = function.get("address", "").lower().removeprefix("0x")
        if not address or any(ch not in "0123456789abcdef" for ch in address):
            raise ValueError(f"invalid function address: {function.get('address')!r}")
        if address in seen:
            raise ValueError(f"duplicate function address: {address}")
        seen.add(address)
        function["address"] = address
    for probe in spec.get("vtable_probes", []):
        address = probe.get("address", "").lower().removeprefix("0x")
        if not address or any(ch not in "0123456789abcdef" for ch in address):
            raise ValueError(f"invalid vtable address: {probe.get('address')!r}")
        probe["address"] = address
    return spec


def _selected_functions(spec: dict, addresses: list[str]) -> list[dict]:
    if not addresses:
        return spec["functions"]
    wanted = {address.lower().removeprefix("0x") for address in addresses}
    selected = [item for item in spec["functions"] if item["address"] in wanted]
    missing = sorted(wanted - {item["address"] for item in selected})
    if missing:
        raise ValueError("addresses absent from specification: " + ", ".join(missing))
    return selected


def _verify_binary(spec: dict, path: Path) -> None:
    expected = spec["binary"]
    size = path.stat().st_size
    if size != expected["size"]:
        raise ValueError(f"binary size mismatch: expected {expected['size']}, got {size}")
    sha256 = hashlib.sha256()
    md5 = hashlib.md5()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            sha256.update(chunk)
            md5.update(chunk)
    actual_sha256 = sha256.hexdigest()
    actual_md5 = md5.hexdigest()
    if actual_sha256 != expected["sha256"] or actual_md5 != expected["md5"]:
        raise ValueError(
            "binary hash mismatch: the research addresses are pinned to "
            f"sha256={expected['sha256']} md5={expected['md5']}"
        )


def _invocations(function: dict, kinds: set[str], output_dir: Path) -> list[tuple[str, str, Path]]:
    address = function["address"]
    label = function.get("label", address)
    stem = f"{address}_{label}"
    calls = []
    if "funcs" in kinds:
        output = output_dir / f"{stem}.decomp.txt"
        args = (
            f"funcs={address} "
            f"depth={int(function.get('depth', 1))} "
            f"cap={int(function.get('cap', 80))} "
            f"out={output.as_posix()}"
        )
        calls.append(("DumpFuncs", args, output))
    if "asm" in kinds:
        output = output_dir / f"{stem}.asm.txt"
        args = (
            f"funcs={address} "
            f"count={int(function.get('asm_count', 800))} "
            f"out={output.as_posix()}"
        )
        calls.append(("DumpAsm", args, output))
    if "xrefs" in kinds:
        output = output_dir / f"{stem}.xrefs.txt"
        calls.append(("DumpXrefs", f"addrs={address} out={output.as_posix()}", output))
    return calls


def _field_invocations(spec: dict, kinds: set[str], output_dir: Path) -> list[tuple[str, str, Path]]:
    if "fields" not in kinds:
        return []
    calls = []
    for probe in spec.get("field_probes", []):
        displacement = probe.get("displacement", "").lower().removeprefix("0x")
        if not displacement or any(ch not in "0123456789abcdef" for ch in displacement):
            raise ValueError(f"invalid field displacement: {probe.get('displacement')!r}")
        label = probe.get("label", displacement)
        output = output_dir / f"field_{displacement}_{label}.txt"
        # Ghidra tokenizes punctuation in script arguments.  Keep one displacement
        # per invocation so DumpFieldRefs receives an unambiguous key/value pair.
        args = f"disps={displacement} out={output.as_posix()}"
        calls.append(("DumpFieldRefs", args, output))
    return calls


def _vtable_invocations(spec: dict, kinds: set[str], output_dir: Path) -> list[tuple[str, str, Path]]:
    if "vtables" not in kinds:
        return []
    calls = []
    for probe in spec.get("vtable_probes", []):
        address = probe["address"]
        label = probe.get("label", address)
        output = output_dir / f"vtable_{address}_{label}.txt"
        args = (
            f"vtables={address} "
            f"vtslots={int(probe.get('slots', 384))} "
            f"cap={int(probe.get('cap', 384))} "
            f"out={output.as_posix()}"
        )
        calls.append(("DumpFuncs", args, output))
    return calls


def _grep_invocations(spec: dict, kinds: set[str], output_dir: Path) -> list[tuple[str, str, Path]]:
    if "grep" not in kinds:
        return []
    calls = []
    for probe in spec.get("grep_probes", []):
        label = probe.get("label", "grep")
        output = output_dir / f"grep_{label}.txt"
        parts = []
        for key in ("str", "cls", "fn"):
            value = probe.get(key)
            if value:
                if any(ch in value for ch in (" ", ",", "|")):
                    raise ValueError(f"grep {key} must use '~' for alternation and contain no spaces")
                parts.append(f"{key}={value}")
        parts.extend([f"cap={int(probe.get('cap', 80))}", f"out={output.as_posix()}"])
        calls.append(("DumpGrep", " ".join(parts), output))
    return calls


def _write_index(spec: dict, selected: list[dict], output_dir: Path, command: str) -> None:
    selected_addresses = {item["address"] for item in selected}
    lines = [
        f"# Ghidra context: {spec['topic']}",
        "",
        f"- Program: `{spec['program']}`",
        f"- Image base: `{spec['binary']['image_base']}`",
        f"- Binary size: `{spec['binary']['size']}` bytes",
        f"- Binary SHA-256: `{spec['binary']['sha256']}`",
        f"- Binary MD5: `{spec['binary']['md5']}`",
        f"- Specification version: `{spec['version']}`",
        f"- Generated UTC: `{dt.datetime.now(dt.timezone.utc).isoformat()}`",
        f"- Rebuild command: `{command}`",
        "",
        "## Functions",
        "",
        "| Address | Working alias | Confidence | Role |",
        "|---|---|---|---|",
    ]
    for function in selected:
        lines.append(
            f"| `0x{function['address']}` | `{function.get('label', '')}` | "
            f"{function.get('confidence', 'unknown')} | {function.get('role', '')} |"
        )
    relationships = [
        edge for edge in spec.get("relationships", [])
        if edge.get("from", "").lower().removeprefix("0x") in selected_addresses
        or edge.get("to", "").lower().removeprefix("0x") in selected_addresses
    ]
    if relationships:
        lines.extend([
            "",
            "## Relationships",
            "",
            "| From | To | Confidence | Evidence |",
            "|---|---|---|---|",
        ])
        for edge in relationships:
            lines.append(
                f"| `0x{edge['from']}` | `0x{edge['to']}` | "
                f"{edge.get('confidence', 'unknown')} | {edge.get('evidence', '')} |"
            )
    findings = spec.get("findings", [])
    if findings:
        lines.extend([
            "",
            "## Findings",
            "",
            "| Confidence | Fact | Evidence | Consequence |",
            "|---|---|---|---|",
        ])
        for finding in findings:
            lines.append(
                f"| {finding.get('confidence', 'unknown')} | {finding.get('fact', '')} | "
                f"{finding.get('evidence', '')} | {finding.get('consequence', '')} |"
            )
    questions = spec.get("questions", [])
    if questions:
        lines.extend(["", "## Open questions", ""])
        lines.extend(f"- {question}" for question in questions)
    lines.extend(["", "## Dumps", ""])
    for path in sorted(output_dir.glob("*.txt")):
        if path.name != "driver.log":
            lines.append(f"- [{path.name}]({path.name})")
    lines.append("")
    (output_dir / "INDEX.md").write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("spec", type=Path, help="tracked JSON context specification")
    parser.add_argument("--address", action="append", default=[],
                        help="only process this specified address; repeatable")
    parser.add_argument("--binary", type=Path,
                        help="verify a source DLL against the specification before running Ghidra")
    parser.add_argument("--project-dir", type=Path,
                        help="private analyzed Ghidra project directory to use instead of the shared default")
    parser.add_argument("--project-name", default="vtmb",
                        help="Ghidra project name inside --project-dir (default: vtmb)")
    parser.add_argument("--kinds", default=",".join(KINDS),
                        help="comma-separated subset of funcs,asm,xrefs,fields,vtables,grep")
    parser.add_argument("--dry-run", action="store_true",
                        help="print invocations and write no derived files")
    parser.add_argument("--index-only", action="store_true",
                        help="rebuild INDEX.md from the specification and existing local dumps")
    args = parser.parse_args()

    repo = repo_root()
    spec_path = args.spec if args.spec.is_absolute() else repo / args.spec
    spec = _load_spec(spec_path)
    if args.binary:
        _verify_binary(spec, args.binary)
    selected = _selected_functions(spec, args.address)
    kinds = {item.strip() for item in args.kinds.split(",") if item.strip()}
    unknown = kinds - set(KINDS)
    if unknown or not kinds:
        raise ValueError("invalid dump kinds: " + ", ".join(sorted(unknown or {"<empty>"})))

    runner = repo / "research" / "tooling" / "ghidra" / "driver" / "run.ps1"
    if not runner.is_file():
        raise FileNotFoundError(f"local Ghidra runner is absent: {runner}")
    output_dir = research_root() / "ghidra" / "out" / spec["topic"]
    calls = []
    for function in selected:
        calls.extend(_invocations(function, kinds, output_dir))
    if not args.address:
        calls.extend(_field_invocations(spec, kinds, output_dir))
        calls.extend(_vtable_invocations(spec, kinds, output_dir))
        calls.extend(_grep_invocations(spec, kinds, output_dir))

    command_text = " ".join(sys.argv)
    run_label = "planned Ghidra runs" if args.index_only else "Ghidra runs"
    print(f"{spec['topic']}: {len(selected)} functions, {len(calls)} {run_label}")
    if args.dry_run:
        for script, script_args, output in calls:
            print(f"{script}: {script_args} -> {output.name}")
        return 0

    output_dir.mkdir(parents=True, exist_ok=True)
    if args.index_only:
        _write_index(spec, selected, output_dir, command_text)
        print(f"context index: {output_dir / 'INDEX.md'}")
        return 0

    delay = max(float(spec.get("lock_delay_seconds", 9.0)), 8.0)
    log_path = output_dir / "driver.log"
    with log_path.open("a", encoding="utf-8") as log:
        for index, (script, script_args, output) in enumerate(calls, start=1):
            if index > 1:
                time.sleep(delay)
            if output.exists():
                output.unlink()
            subject = script_args.split(" ", 1)[0]
            print(f"[{index}/{len(calls)}] {script} {subject}")
            command = [
                "powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass",
                "-File", str(runner),
            ]
            if args.project_dir:
                command.extend(["-ProjDir", str(args.project_dir), "-ProjName", args.project_name])
            command.extend([
                "-Program", spec["program"],
                "-Script", script,
                "-ScriptArgs", script_args,
            ])
            result = subprocess.run(
                command, cwd=repo, stdout=log, stderr=subprocess.STDOUT,
                text=True, check=False,
            )
            log.flush()
            if result.returncode != 0:
                raise RuntimeError(f"{script} failed with exit code {result.returncode}; see {log_path}")
            if not output.is_file() or output.stat().st_size == 0:
                raise RuntimeError(f"{script} produced no output at {output}; see {log_path}")

    _write_index(spec, selected, output_dir, command_text)
    print(f"context index: {output_dir / 'INDEX.md'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
