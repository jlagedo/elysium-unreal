#!/usr/bin/env python3
"""Split `docs/vtmb/npc-ai-reverse-engineering.md` into `docs/vtmb/npc-ai/`, one file per kernel
subsystem, and rewrite every citation of the old path.

A pure move: every section's text is copied verbatim, in order, under a header whose text is
unchanged (spec `Oracle: § "…"` cites and the ledger's index key on that text). Only the header
LEVEL changes, so a section lifted out of its old parent reads as a top-level section of its new
file. Run once, from the repository root, through `uv run elysium research split_npc_oracle`;
kept in the tree so the move is reproducible and reviewable, not because it runs again.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

from elysium_pipeline.paths import repo_root

SRC = Path("docs/vtmb/npc-ai-reverse-engineering.md")
DST = Path("docs/vtmb/npc-ai")
DATE = "2026-09-13"

# Header text (exact, without the `#` prefix) → file. A header not listed inherits its parent's.
ASSIGN = {
    "Scope and confidence": "README.md",
    "Executive reconstruction": "README.md",
    "Evidence set and reproducibility": "README.md",
    "The authored NPC population": "population.md",
    "How a map defines an NPC": "population.md",
    "Native NPC object and state": "lifecycle.md",
    "The idle branch, decided": "conditions-and-states.md",
    "`GetSchedule` `0x102ae920` runs ahead of `SelectSchedule`": "conditions-and-states.md",
    "`m_bReturnToInitialPos` is a one-shot armed only by alert or combat": "conditions-and-states.md",
    "Door-obstruction schedule selection": "programs.md",
    "Interesting-place eligibility": "programs.md",
    "`no_alert_state` does not suppress the alert state": "conditions-and-states.md",
    "Perception, sound, memory, and hostility": "senses.md",
    "Relationship table, exactly decoded": "social.md",
    "Enemy acquisition and replacement": "social.md",
    "Emotional disposition and social reaction are different domains": "social.md",
    "Schedules and tasks: the behavior program": "schedule-kernel.md",
    "`m_hFollowerBoss` — the follower controller": "social.md",
    "The three `GatherConditions` sweeps and the interest predicate": "conditions-and-states.md",
    "The base condition table": "conditions-and-states.md",
    "Interrupt conditions": "conditions-and-states.md",
    "Ordinary humanoid combat selection": "programs.md",
    "What fires when aggression begins": "authored-control.md",
    "Authored NPC outputs": "authored-control.md",
    "Python, dialogue, and level orchestration": "authored-control.md",
    "Scripted control and authority": "authored-control.md",
    "Reaction beyond immediate combat": "social.md",
    "What a complete game-side NPC AI requires": "rebuild.md",
    "Faithful-first reconstruction order": "rebuild.md",
    "Current rebuild coverage and gap boundary": "rebuild.md",
    "Open questions and targeted capture programme": "rebuild.md",
    "Final model": "rebuild.md",
    "The sense pass for a hated player, walked (2026-09-08)": "senses.md",
    "The enemy memory — `CAI_Memory` (2026-09-08)": "senses.md",
    "Dialogue does not gate bystanders (2026-09-08)": "social.md",
    "Hearing, walked (2026-09-08)": "senses.md",
    "`ambient_generic` as an AI sound source (2026-09-08)": "senses.md",
    "Interesting places: the selector, the programs, the wait (2026-09-08)": "programs.md",
    "The `INVESTIGATE` family, decoded (2026-09-08)": "programs.md",
    "The kernel's failure route and the base programs, walked (2026-09-12, story 25)": "schedule-kernel.md",
    "Patrol paths, walked (2026-09-12, story 10g)": "programs.md",
    "The alert programs, verbatim (2026-09-12, story 10d)": "programs.md",
    "The hunt programs and the expiry chain, verbatim (2026-09-12, story 10h)": "programs.md",
    "Disciplines that possess or frenzy an NPC; the `AI_NPCFlag` payload (2026-09-08)": "authored-control.md",
    "Sense and investigate leftovers, closed (2026-09-08)": "senses.md",
    "The think cadence, decoded (2026-09-08)": "lifecycle.md",
    "Squads, decoded (2026-09-08)": "social.md",
    "The flee state and the cower, disoriented and lost programs (2026-09-08)": "programs.md",
    "Species slot-435 overrides all chain (2026-09-08)": "schedule-kernel.md",
    "The cover and kick chooser, and the combat leftovers (2026-09-08)": "programs.md",
    "The navigation and reaction keyfields (2026-09-08)": "authored-control.md",
}

TITLES = {
    "README.md": "The NPC AI oracle",
    "population.md": "The authored population",
    "lifecycle.md": "Lifecycle: spawn, activation, the think",
    "senses.md": "Senses, sound and memory",
    "conditions-and-states.md": "Conditions and states",
    "schedule-kernel.md": "The schedule kernel",
    "programs.md": "The programs",
    "social.md": "Relationships, enemies, squads and reactions",
    "authored-control.md": "Authored control: outputs, scripts, keyfields",
    "rebuild.md": "What the rebuild needs",
}
ORDER = ["README.md", "population.md", "lifecycle.md", "senses.md", "conditions-and-states.md",
         "schedule-kernel.md", "programs.md", "social.md", "authored-control.md", "rebuild.md"]

HEADER_RE = re.compile(r"^(#{2,6}) (.*?)\s*$")
CITE_PATH_RE = re.compile(r"(?:docs/vtmb/)?npc-ai-reverse-engineering\.md")
PREFIX_RE = re.compile(r"""(?:->|→|§|,)\s*["“]([^"”\n]*)""")


def _norm(text: str) -> str:
    return re.sub(r"\s+", " ", text.replace("`", "").replace("*", "")).strip().casefold()


def split(repo: Path) -> dict[str, str]:
    lines = (repo / SRC).read_text(encoding="utf-8").splitlines()
    if not any(line.startswith("## ") for line in lines):
        raise SystemExit(f"{SRC} holds no sections: the split has already run; nothing to do")
    units: list[dict] = []
    for number, line in enumerate(lines):
        match = HEADER_RE.match(line)
        if match:
            units.append({"level": len(match.group(1)), "text": match.group(2), "start": number})
    for i, unit in enumerate(units):
        unit["end"] = units[i + 1]["start"] if i + 1 < len(units) else len(lines)
    # assignment: explicit, else the nearest preceding shallower unit's file
    stack: list[dict] = []
    for unit in units:
        while stack and stack[-1]["level"] >= unit["level"]:
            stack.pop()
        explicit = ASSIGN.get(unit["text"])
        unit["file"] = explicit or (stack[-1]["file"] if stack else "README.md")
        unit["explicit"] = explicit is not None
        unit["parent"] = stack[-1] if stack else None
        stack.append(unit)
    unknown = [u["text"] for u in units if u["level"] == 2 and not u["explicit"]]
    if unknown:
        raise SystemExit("unassigned top-level sections: " + "; ".join(unknown))
    # level normalisation: a unit whose parent is in another file becomes `##` in its own
    for unit in units:
        top = unit
        while top["parent"] is not None and top["parent"]["file"] == unit["file"] \
                and not (top["parent"]["level"] < top["level"] and top["explicit"] and top["parent"]["file"] != unit["file"]):
            if top["parent"]["file"] != unit["file"]:
                break
            top = top["parent"]
        unit["new_level"] = 2 + (unit["level"] - top["level"])
    files: dict[str, list[str]] = {name: [] for name in ORDER}
    for unit in units:
        body = lines[unit["start"] + 1:unit["end"]]
        files[unit["file"]].append("#" * unit["new_level"] + " " + unit["text"])
        files[unit["file"]].extend(body)
    rendered: dict[str, str] = {}
    for name in ORDER:
        head = [f"# NPC AI — {TITLES[name]}", ""]
        if name == "README.md":
            head += [
                "The recovered retail facts about VtMB's NPC AI, one file per kernel subsystem. The",
                f"tables under [`../npc-kernel/`](../npc-kernel/README.md) are the generated ledger of the",
                "same kernel (every class, slot, field and reachable function); the prose here is what",
                "has been *walked*. `../npc-kernel/index.md` maps an address to the section that walks it.",
                "",
                "| File | Holds |",
                "|---|---|",
            ]
            for other in ORDER[1:]:
                head.append(f"| [`{other}`](./{other}) | {TITLES[other]} |")
            head += [
                "",
                "Conventions for new sections: a walked retail function is a `###` whose header carries",
                "the address (`### \\`MaintainSchedule\\` \\`0x102817c0\\``); provenance is a one-line",
                "`_Recovered YYYY-MM-DD, story N._` under the header, never in it; every section ends with",
                "`**Unrecovered:**` (or `nothing`). Specs cite `§ \"<header>\"`; the header text is the key.",
                "",
                f"_Split from `npc-ai-reverse-engineering.md` on {DATE}; the sections below were moved verbatim._",
                "",
            ]
        else:
            head += [
                f"Part of the [NPC AI oracle](./README.md). Sections moved verbatim from",
                f"`npc-ai-reverse-engineering.md` on {DATE}; their headers are the citation keys.",
                "",
            ]
        text = "\n".join(head + files[name]).rstrip("\n") + "\n"
        rendered[name] = text
    return rendered, units


def header_index(rendered: dict[str, str]) -> list[tuple[str, str]]:
    index = []
    for name, text in rendered.items():
        for line in text.splitlines():
            match = HEADER_RE.match(line)
            if match:
                index.append((_norm(match.group(2)), name))
    return index


def resolve(prefix: str, index: list[tuple[str, str]]) -> str | None:
    key = _norm(prefix)
    if not key:
        return None
    hits = {name for text, name in index if text.startswith(key)}
    if len(hits) == 1:
        return hits.pop()
    exact = {name for text, name in index if text == key}
    return exact.pop() if len(exact) == 1 else None


def rewrite(repo: Path, index: list[tuple[str, str]]) -> list[str]:
    report = []
    targets = [p for p in (repo / "Source" / "ElysiumUE").rglob("*") if p.suffix in (".h", ".cpp")]
    targets += list((repo / "docs" / "specs").rglob("*.md"))
    targets += [p for p in (repo / "docs" / "vtmb").glob("*.md") if p.name != SRC.name]
    targets += list((repo / "docs" / "contracts").glob("*.md"))
    targets += [repo / "AGENTS.md", repo / "CLAUDE.md", repo / "docs" / "vision.md"]
    for path in targets:
        if not path.is_file():
            continue
        text = path.read_text(encoding="utf-8")
        if "npc-ai-reverse-engineering" not in text:
            continue
        lines = text.splitlines(keepends=True)
        changed = False
        for i, line in enumerate(lines):
            if "npc-ai-reverse-engineering" not in line:
                continue
            context = line + (lines[i + 1] if i + 1 < len(lines) else "")

            def repl(match: re.Match) -> str:
                after = context[match.end() - (0 if match.end() <= len(line) else 0):][:260] \
                    if match.end() <= len(line) else ""
                after = context[match.end():match.end() + 260]
                found = PREFIX_RE.search(after)
                target = resolve(found.group(1), index) if found else None
                target = target or "README.md"
                prefix = "docs/vtmb/" if match.group(0).startswith("docs/vtmb/") else ""
                if path.suffix in (".h", ".cpp"):
                    prefix = "docs/vtmb/"
                return f"{prefix}npc-ai/{target}"

            new = CITE_PATH_RE.sub(repl, line)
            if new != line:
                lines[i] = new
                changed = True
                report.append(f"{path.relative_to(repo).as_posix()}:{i + 1}: {new.strip()[:140]}")
        if changed:
            path.write_text("".join(lines), encoding="utf-8", newline="")
    return report


def main() -> int:
    repo = repo_root()
    rendered, units = split(repo)
    (repo / DST).mkdir(parents=True, exist_ok=True)
    for name, text in rendered.items():
        (repo / DST / name).write_text(text, encoding="utf-8", newline="\n")
    index = header_index(rendered)
    stub = [
        "# VtMB NPC AI reverse-engineering survey — moved",
        "",
        f"On {DATE} this file was split into [`npc-ai/`](npc-ai/README.md), one file per kernel",
        "subsystem, with every section moved verbatim and its header text unchanged. The generated",
        "kernel ledger lives beside it under [`npc-kernel/`](npc-kernel/README.md). This stub stays",
        "for one release so stale links land somewhere; cite the new files.",
        "",
    ] + [f"- [`npc-ai/{n}`](npc-ai/{n}) — {TITLES[n]}" for n in ORDER]
    (repo / SRC).write_text("\n".join(stub) + "\n", encoding="utf-8", newline="\n")
    report = rewrite(repo, index)
    for unit in units:
        if unit["explicit"] or unit["level"] <= 3:
            print(f"{'#' * unit['level']:6} → {unit['file']:26} {'#' * unit['new_level']} {unit['text'][:70]}")
    print(f"\n{len(units)} sections into {len(rendered)} files; {len(report)} citations rewritten:")
    for line in report:
        print("  " + line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
