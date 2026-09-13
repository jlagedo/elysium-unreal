"""Every citation of the oracle resolves: a spec's `§ "<header>"` names a real section, and a
source comment's `docs/vtmb/<file>.md` names a real file.

The oracle is cited by header text, not by line number, so a file can be split or a section
moved without breaking the specs — as long as nothing renames a header silently. This test is
what makes that safe: it fails on the first dangling citation and names it.

A `§ "…"` cite may name a header at any level, or a bold run-in label (`**Label.**`) inside a
section, because the specs cite sub-anchors that way — in the oracle or in the spec itself.
Whitespace is collapsed and backticks and asterisks are ignored on both sides; a cite may be a
prefix or a substring of the anchor it names (a long header is often cut at a line break in
the citing prose, and a numbered header is cited without its number), and `...` in a cite
stands for anything.

The NPC oracle (`docs/vtmb/npc-ai/`) is held to the rule outright. Citations into the other
oracle files that dangle today are reported as warnings, not failures: they are debt from
before this test existed, listed so they can be paid, and a new dangling one still shows up.
"""

from __future__ import annotations

import re
import warnings
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
VTMB = REPO / "docs" / "vtmb"
HEADER_RE = re.compile(r"^#{1,6} (.+?)\s*$", re.M)
BOLD_RE = re.compile(r"\*\*([^*\n]{3,160}?)\*\*")
SECTION_CITE_RE = re.compile(r"§\s*[\"“]([^\"”]+)[\"”]")
PATH_CITE_RE = re.compile(r"docs/vtmb/([A-Za-z0-9_./-]+\.md)")
# A source comment cites a section as `-> "…"`, `→ "…"` or `§ "…"` right after the path.
SOURCE_SECTION_RE = re.compile(
    r"docs/vtmb/([A-Za-z0-9_./-]+\.md)`?\s*(?:->|→|§)\s*\"([^\"\n]+)")


def _norm(text: str) -> str:
    return re.sub(r"\s+", " ", text.replace("`", "").replace("*", "")).strip().casefold()


NUMBERED_RE = re.compile(r"^\d+(?:\.\d+)*\.?\s+")


def _anchors_of(path: Path) -> set[str]:
    text = path.read_text(encoding="utf-8", errors="replace")
    keys = {_norm(m.group(1)) for m in HEADER_RE.finditer(text)}
    keys |= {_norm(m.group(1)).rstrip(".:") for m in BOLD_RE.finditer(text)}
    return keys | {NUMBERED_RE.sub("", k) for k in keys}


def _anchors() -> dict[str, set[str]]:
    """file → every header and bold label it holds, normalised; oracle files keyed relative to
    `docs/vtmb`, spec files to the repository."""
    found: dict[str, set[str]] = {}
    for path in sorted(VTMB.rglob("*.md")):
        found[path.relative_to(VTMB).as_posix()] = _anchors_of(path)
    for path in sorted((REPO / "docs" / "specs").rglob("*.md")):
        found[path.relative_to(REPO).as_posix()] = _anchors_of(path)
    return found


def _resolves(cite: str, keys: set[str]) -> bool:
    key = _norm(cite).rstrip(".:")
    if not key:
        return True
    pattern = ".*".join(re.escape(part) for part in re.split(r"\.\.\.|…", key))
    matcher = re.compile(pattern)
    return any(matcher.search(k) for k in keys)


def _report(label: str, hard: list[str], soft: list[str]) -> None:
    if soft:
        warnings.warn(f"{label} (pre-existing, outside the NPC oracle):\n  " + "\n  ".join(soft))
    assert not hard, f"{label}:\n  " + "\n  ".join(hard)


def test_spec_section_cites_resolve_to_an_oracle_section():
    anchors = _anchors()
    everything = set().union(*anchors.values())
    npc = set().union(*(v for k, v in anchors.items() if k.startswith("npc-ai/")))
    hard, soft = [], []
    for path in sorted((REPO / "docs" / "specs").rglob("*.md")):
        rel = path.relative_to(REPO).as_posix()
        text = re.sub(r"\s*\n\s*", " ", path.read_text(encoding="utf-8", errors="replace"))
        for match in SECTION_CITE_RE.finditer(text):
            cite = match.group(1)
            if _resolves(cite, everything):
                continue
            (hard if rel.startswith("docs/specs/0002-") else soft).append(f"{rel}: § \"{cite[:80]}\"")
    _report("dangling section citations", hard, soft)


def test_source_path_cites_name_existing_oracle_files():
    anchors = _anchors()
    missing = []
    hard, soft = [], []
    roots = [REPO / "Source" / "ElysiumUE", REPO / "docs" / "specs", REPO / "docs" / "contracts"]
    for root in roots:
        for path in sorted(root.rglob("*")):
            if path.suffix not in (".h", ".cpp", ".md"):
                continue
            text = path.read_text(encoding="utf-8", errors="replace")
            if "docs/vtmb/" not in text:
                continue
            rel = path.relative_to(REPO).as_posix()
            for match in PATH_CITE_RE.finditer(text):
                if match.group(1) not in anchors:
                    missing.append(f"{rel}: docs/vtmb/{match.group(1)}")
            joined = re.sub(r"\s*\n\s*(?://|\*)?\s*", " ", text)
            for match in SOURCE_SECTION_RE.finditer(joined):
                file, cite = match.group(1), match.group(2)
                if file in anchors and not _resolves(cite, anchors[file]):
                    row = f"{rel}: docs/vtmb/{file} -> \"{cite[:70]}\""
                    (hard if file.startswith("npc-ai/") else soft).append(row)
    assert not missing, "citations of oracle files that do not exist:\n  " + "\n  ".join(sorted(set(missing)))
    _report("citations of sections their file does not hold", hard, soft)


def test_oracle_cross_references_name_existing_files():
    anchors = _anchors()
    missing = []
    for path in sorted(VTMB.rglob("*.md")):
        text = path.read_text(encoding="utf-8", errors="replace")
        for match in re.finditer(r"\]\(([A-Za-z0-9_./-]+\.md)(?:#[^)]*)?\)", text):
            target = (path.parent / match.group(1)).resolve()
            try:
                rel = target.relative_to(VTMB).as_posix()
            except ValueError:
                continue
            if rel not in anchors:
                missing.append(f"{path.relative_to(REPO).as_posix()}: {match.group(1)}")
    assert not missing, "oracle links to files that do not exist:\n  " + "\n  ".join(missing)
