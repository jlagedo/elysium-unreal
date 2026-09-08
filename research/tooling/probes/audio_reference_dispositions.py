# -*- coding: utf-8 -*-
"""AUD0.6 -- reference validation on the corpus index.

Every audio-facing reference on a map -- file paths and I/O wires -- gets one explicit
disposition; nothing is left `unclassified` on the three playable-path maps
(``sp_tutorial_1``, ``sm_pawnshop_1``, ``sm_hub_1``).

Sources, in the order the spec names them:

* The legacy ``.ents`` JSON (``$ELYSIUM_EXPORT_ROOT/<map>/<map>.ents``) for the entity census,
  the wire graph and the keyvalues the V2 map-entities reference vocabulary does not type
  (``soundgroup`` tokens with no extension, ``locksnd``/``actsnd``/``deactsnd``/``snd_name``).
* The V2 ``map-entities`` GLB unit's own ``dependencies[]`` (role ``sound``/``sound-scheme``/
  ``scene``/``dialogue``) for every keyvalue the reference vocabulary does type -- this already
  covers ``ambient_generic.message``, ``locked_sound``/``unlocked_sound``/``startsound``/
  ``stopsound`` (they contain "sound"), ``scheme_file``, ``SceneFile`` and ``dialogname``.
* Two more hops for what a map keyvalue only names indirectly: the referenced ``sound-scheme``
  unit's own ``dependencies[]`` (every scheme ``Filename``), the referenced ``dialogue`` unit's
  own ``dependencies[]`` (every voiced line's derived mp3, mp3-first already applied), and the
  referenced ``scene`` unit's own ``dependencies[]`` (a `.vcd`'s ``speak``/``bodysound`` params).
* ``Content/ElysiumCorpus/vdata/system/sndscheme_{openable,switch,computer}.txt`` for the
  soundgroup subkey vocabulary, to synthesize ``usable/<category>/<group>/<subkey>.wav``
  candidates the reference vocabulary cannot see (``soundgroup`` carries no extension).
* The mirrored level script (``scripts/<levelscript>/<levelscript>.py``) for ``PlayDialogFile``
  literals.

Disposition vocabulary (exactly six, per the spec): ``corpus``, ``mp3-first``, ``absent-subkey``,
``never-voiced``, ``silent-in-retail``, ``unclassified``. Wire dispositions: ``missing_target``,
``unimplemented_input``.

Read-only. No repo file is written except the paths passed to ``--json``/``--md``, both under
``$ELYSIUM_WORK_ROOT``.
"""
from __future__ import annotations

import argparse
import collections
import json
import re
from pathlib import Path
from typing import Any

from elysium_pipeline.formats.unit_contract.container import read_document
from elysium_pipeline.paths import export_root, export_v2_root, repo_root, vtmb_root

from research.tooling.probes.audio_surface_survey import AUDIO_INPUTS, resolve_targets

THREE_MAPS = ("sp_tutorial_1", "sm_pawnshop_1", "sm_hub_1")

MOVER_CLASSES = {
    "func_door", "func_door_rotating", "func_button", "prop_button", "prop_switch",
    "func_elevator", "prop_hacking", "item_container", "item_container_animated",
    "item_container_lock",
}
CLASS_CATEGORY = {
    "func_door": "openable",
    "func_door_rotating": "openable",
    "item_container": "openable",
    "item_container_animated": "openable",
    "item_container_lock": "openable",
    "func_button": "switches",
    "prop_button": "switches",
    "prop_switch": "switches",
    "prop_hacking": "computers",
}
CATEGORY_VOCAB_FILE = {
    "openable": "sndscheme_openable.txt",
    "switches": "sndscheme_switch.txt",
    "computers": "sndscheme_computer.txt",
}
#: Retail input the class does not implement, per `docs/vtmb/audio_pipeline.md` #7:
#: ambient_generic has no FadeIn/FadeOut input (those are `m_dpv` keyvalues, not wires);
#: ambient_soundscheme's dispatcher never declares Disable.
UNIMPLEMENTED_INPUTS = {
    ("ambient_generic", "FadeIn"),
    ("ambient_generic", "FadeOut"),
    ("ambient_soundscheme", "Disable"),
}
UNIMPLEMENTED_CITE = "docs/vtmb/audio_pipeline.md #7"
INTERESTING_INPUTS = AUDIO_INPUTS | {"Kill"} | {name for _, name in UNIMPLEMENTED_INPUTS}

EXPLICIT_SOUND_KEYS_NOT_TYPED = ("locksnd", "actsnd", "deactsnd", "snd_name")

DLG_LINE_ID_RE = re.compile(r"line(\d+)_col_([a-z])", re.IGNORECASE)
PLAYDIALOGFILE_RE = re.compile(r"\.PlayDialogFile\(\s*[\"']([^\"']+)[\"']")


def key_ci(keys: dict, name: str, default: str = "") -> str:
    needle = name.casefold()
    for key, value in keys.items():
        if key.casefold() == needle:
            return value
    return default


def normalize(path: str) -> str:
    """Lower, forward-slashed, `sound/`-relative -- the corpus deploys lower-cased keys."""

    text = str(path).strip().strip('"').replace("\\", "/").lower()
    text = text.lstrip("/")
    if text.startswith("sound/"):
        text = text[len("sound/"):]
    return text


def mp3_sibling(path: str) -> str | None:
    return path[:-4] + ".mp3" if path.lower().endswith(".wav") else None


class Corpus:
    """Filesystem checks against the deployed corpus and every retail source tree."""

    def __init__(self) -> None:
        self.sound_root = repo_root() / "Content" / "ElysiumCorpus" / "sound"
        self.vdata_root = repo_root() / "Content" / "ElysiumCorpus" / "vdata" / "system"
        self.v1_root = export_root() / "sound"
        self.loose_root = vtmb_root() / "Vampire" / "sound"
        self.patch_root = vtmb_root() / "Unofficial_Patch" / "sound"

    def in_corpus(self, relpath: str) -> bool:
        return (self.sound_root / relpath).exists()

    def in_any_legacy(self, relpath: str) -> bool:
        return (
            (self.v1_root / relpath).exists()
            or (self.loose_root / relpath).exists()
            or (self.patch_root / relpath).exists()
        )

    def classify(
        self, relpath: str, *, kind: str, note: str = "", forced: str | None = None,
    ) -> tuple[str, str]:
        """One of the six dispositions, plus a short note."""

        norm = normalize(relpath)
        if forced == "mp3-first" and self.in_corpus(norm):
            # `relpath` is already the swapped mp3 (retail's mp3-first rule); the fallback the
            # authored .wav never shipped is the point being reported, not hidden as "corpus".
            return "mp3-first", note
        if self.in_corpus(norm):
            return "corpus", note
        mp3 = mp3_sibling(norm)
        if mp3 and self.in_corpus(mp3):
            return "mp3-first", note
        if kind == "mover.soundgroup":
            return "absent-subkey", note
        if kind == "never-voiced":
            return "never-voiced", note
        if not self.in_any_legacy(norm) and not (mp3 and self.in_any_legacy(mp3)):
            return "silent-in-retail", note
        return "unclassified", (
            note + " -- exists in a legacy/install tree but not the deployed corpus"
        ).strip()


def soundgroup_subkeys(corpus: Corpus, category: str) -> list[str]:
    """The subkey vocabulary declared in `sndscheme_{openable,switch,computer}.txt`."""

    path = corpus.vdata_root / CATEGORY_VOCAB_FILE[category]
    if not path.exists():
        return []
    text = path.read_text(encoding="latin-1", errors="replace")
    subkeys = []
    for block in re.finditer(r"Sound\s*\{([^{}]*)\}", text):
        m = re.search(r'"Name"\s*"([^"]+)"', block.group(1))
        if m:
            subkeys.append(m.group(1))
    return subkeys


def load_ents(map_name: str) -> dict | None:
    path = export_root() / map_name / f"{map_name}.ents"
    if not path.exists():
        return None
    try:
        return json.load(open(path, "r", encoding="utf-8", errors="replace"))
    except (OSError, ValueError):
        return None


def load_v2_json(path: Path) -> dict | None:
    if not path.exists():
        return None
    try:
        return read_document(path)
    except Exception:
        return None


def map_entities_extension(doc: dict) -> dict | None:
    for value in doc.get("extensions", {}).values():
        if "dependencies" in value and "map" in value:
            return value
    return None


def unit_extension(doc: dict) -> dict | None:
    for value in doc.get("extensions", {}).values():
        if "dependencies" in value:
            return value
    return None


def dependency_relpath(dep: dict) -> str:
    """The path that actually resolves: the mp3-first `asset` when swapped, else `sourcePath`."""

    if dep.get("resolution") == "mp3-first":
        asset = dep.get("asset", "")
        return asset.split(":", 2)[-1] if asset.startswith("vtmb:sound:") else normalize(
            dep.get("sourcePath", "")
        )
    return dep.get("sourcePath", "")


def asset_key(asset: str, prefix: str) -> str:
    return asset[len(prefix):] if asset.startswith(prefix) else asset


class MapAudit:
    def __init__(self, map_name: str, corpus: Corpus):
        self.map = map_name
        self.corpus = corpus
        self.references: list[dict[str, Any]] = []
        self.wires_missing: list[dict[str, Any]] = []
        self.wires_unimplemented: list[dict[str, Any]] = []
        self.disagreements: list[dict[str, Any]] = []
        self.notes: list[str] = []
        self.ents = load_ents(map_name)
        self.map_glb = load_v2_json(export_v2_root() / "maps" / f"{map_name}.entities.glb")
        self.map_ext = map_entities_extension(self.map_glb) if self.map_glb else None

    # -- references ----------------------------------------------------------------------

    def add_reference(
        self, path: str, kind: str, referrer: str, *, note: str = "", forced: str | None = None,
    ) -> None:
        disposition, note = self.corpus.classify(path, kind=kind, note=note, forced=forced)
        self.references.append({
            "map": self.map,
            "path": normalize(path),
            "kind": kind,
            "referrer": referrer,
            "disposition": disposition,
            "note": note,
        })

    def from_map_dependencies(self) -> dict[str, list[dict]]:
        """The V2 map-entities dependencies, bucketed by role; () if the unit is missing."""

        if not self.map_ext:
            self.notes.append("no exports_v2 map-entities unit; V2 cross-check skipped")
            return {}
        buckets: dict[str, list[dict]] = collections.defaultdict(list)
        for dep in self.map_ext["dependencies"]:
            buckets[dep["role"]].append(dep)
        for dep in buckets.get("sound", []):
            self.add_reference(
                dependency_relpath(dep), "map-entities.sound", dep["sourcePath"],
                note="" if dep["resolved"] else "not in the VtMB install index",
                forced=dep.get("resolution"),
            )
        return buckets

    def from_sound_scheme(self, dep: dict) -> None:
        key = asset_key(dep["asset"], "vtmb:sound-scheme:")
        doc = load_v2_json(export_v2_root() / "sound-schemes" / f"{key}.glb")
        ext = unit_extension(doc) if doc else None
        if ext is None:
            self.notes.append(f"sound-scheme unit missing for {key}; scheme_file={dep['sourcePath']}")
            return
        for sdep in ext["dependencies"]:
            if sdep["role"] != "sound":
                continue
            self.add_reference(
                dependency_relpath(sdep), "scheme.Filename",
                f"{dep['sourcePath']} -> {sdep['sourcePath']}",
                forced=sdep.get("resolution"),
            )

    def from_dialogue(self, dep: dict) -> set[tuple[str, str]]:
        """Every voiced line's mp3; the (id, lang) pairs a line got a dependency for."""

        key = asset_key(dep["asset"], "vtmb:dialogue:")
        doc = load_v2_json(export_v2_root() / "dialogues" / f"{key}.glb")
        ext = unit_extension(doc) if doc else None
        covered: set[tuple[str, str]] = set()
        if ext is None:
            self.notes.append(f"dialogue unit missing for {key}; dialogname={dep['sourcePath']}")
            return covered
        for ddep in ext["dependencies"]:
            if ddep["role"] != "sound":
                continue
            self.add_reference(
                dependency_relpath(ddep), "dlg.line_mp3", f"{key} <- {ddep['sourcePath']}",
                forced=ddep.get("resolution"),
            )
            m = DLG_LINE_ID_RE.search(ddep["asset"])
            if m:
                covered.add((m.group(1), m.group(2)))
        # Never-voiced: an npc-line row with authored text (even stage-direction-only) that
        # produced no sound dependency at all -- the dialogue GLB model already excludes it.
        for line in ext.get("lines", []):
            if line.get("role") != "npc-line":
                continue
            texts = (line.get("textMale", ""), line.get("textFemale", ""),
                     line.get("textMalkavian", ""))
            if not any(t.strip() for t in texts):
                continue  # reserved-but-unauthored row slot, not a reference at all
            lid = str(line["id"])
            if any(lid == cid for cid, _ in covered):
                continue
            direction = line.get("stageDirections") or []
            note = (
                direction[0]["text"] if direction
                else "text present, no voiced take shipped"
            )
            candidate = f"character/dlg/{key}/line{lid}_col_e.mp3"
            self.add_reference(candidate, "never-voiced", f"{key} id={lid}", note=note)
        return covered

    def from_scene(self, dep: dict) -> None:
        key = asset_key(dep["asset"], "vtmb:scene:")
        doc = load_v2_json(export_v2_root() / "scenes" / f"{key}.glb")
        ext = unit_extension(doc) if doc else None
        if ext is None:
            self.notes.append(f"scene unit missing for {key}; SceneFile={dep['sourcePath']}")
            return
        for sdep in ext["dependencies"]:
            if sdep["role"] != "sound":
                continue
            self.add_reference(
                dependency_relpath(sdep), "vcd.speak", f"{key} <- {sdep['sourcePath']}",
                forced=sdep.get("resolution"),
            )

    # -- soundgroup subkeys and the keys the V2 reference vocabulary does not type -----------

    def from_raw_ents(self) -> None:
        if not self.ents:
            self.notes.append("no legacy .ents export; entity-scoped references skipped")
            return
        entities = self.ents["entities"]
        seen_groups: set[tuple[str, str]] = set()
        for ent in entities:
            classname = str(ent.get("classname", ""))
            keys = ent.get("keys", {})
            tn = ent.get("targetname", "") or f"<{classname}>"
            if classname in MOVER_CLASSES:
                group = str(key_ci(keys, "soundgroup", "")).strip()
                category = CLASS_CATEGORY.get(classname)
                if group and category:
                    gkey = (category, group)
                    if gkey not in seen_groups:
                        seen_groups.add(gkey)
                        for subkey in soundgroup_subkeys(self.corpus, category):
                            path = f"usable/{category}/{group}/{subkey}.wav"
                            self.add_reference(
                                path, "mover.soundgroup",
                                f"{classname} category={category} group={group} subkey={subkey}",
                            )
            for key in EXPLICIT_SOUND_KEYS_NOT_TYPED:
                value = key_ci(keys, key, "")
                if value:
                    self.add_reference(value, "mover.explicit." + key, f"{classname} {tn}")

    def playdialogfile_literals(self) -> None:
        if not self.ents:
            return
        worldspawn = next(
            (e for e in self.ents["entities"]
             if str(e.get("classname", "")).casefold() == "worldspawn"), None
        )
        script = str(key_ci(worldspawn.get("keys", {}), "levelscript", "")) if worldspawn else ""
        if not script:
            return
        path = export_root() / "scripts" / script / f"{script}.py"
        if not path.exists():
            return
        for line_no, raw in enumerate(open(path, "r", encoding="latin-1", errors="replace"), 1):
            for m in PLAYDIALOGFILE_RE.finditer(raw):
                self.add_reference(
                    m.group(1), "python.PlayDialogFile", f"{script}.py:{line_no}",
                )

    # -- wires -----------------------------------------------------------------------------

    def wires(self) -> None:
        if not self.ents:
            return
        entities = self.ents["entities"]
        for ent in entities:
            classname = str(ent.get("classname", ""))
            for output in ent.get("outputs", []):
                input_name = str(output.get("input", ""))
                if input_name not in INTERESTING_INPUTS:
                    continue
                target = output.get("target", "")
                targets = resolve_targets(entities, target)
                if input_name == "Kill":
                    targets = [t for t in targets
                               if str(t.get("classname", "")).casefold() == "ambient_generic"]
                if not targets:
                    if target and not str(target).startswith("!") and input_name in AUDIO_INPUTS:
                        self.wires_missing.append({
                            "map": self.map,
                            "source": ent.get("targetname") or classname,
                            "event": output.get("name", ""),
                            "target": target,
                            "input": input_name,
                            "disposition": "missing_target",
                        })
                    continue
                for t in targets:
                    tclass = str(t.get("classname", ""))
                    if (tclass, input_name) in UNIMPLEMENTED_INPUTS:
                        self.wires_unimplemented.append({
                            "map": self.map,
                            "source": ent.get("targetname") or classname,
                            "event": output.get("name", ""),
                            "target": target,
                            "target_class": tclass,
                            "input": input_name,
                            "disposition": "unimplemented_input",
                            "cite": UNIMPLEMENTED_CITE,
                        })

    # -- cross-check -------------------------------------------------------------------------

    def cross_check(self) -> None:
        """Compare the V2 map-entities `resolved` set against a direct corpus check.

        `resolved` answers "is this in the VtMB install," which is a superset test of "is this
        deployed to the corpus" only once AUD0's deploy is complete; a disagreement here is a
        deploy gap worth naming, not silently absorbed into a disposition.
        """

        if not self.map_ext:
            return
        for dep in self.map_ext["dependencies"]:
            if dep["role"] != "sound":
                continue
            norm = normalize(dep["sourcePath"])
            in_corpus = self.corpus.in_corpus(norm) or (
                mp3_sibling(norm) and self.corpus.in_corpus(mp3_sibling(norm))
            )
            if dep["resolved"] and not in_corpus:
                self.disagreements.append({
                    "map": self.map, "path": norm,
                    "v2_resolved": True, "corpus_present": False,
                    "detail": "resolves in the VtMB install index but not in the deployed corpus",
                })
            elif not dep["resolved"] and in_corpus:
                self.disagreements.append({
                    "map": self.map, "path": norm,
                    "v2_resolved": False, "corpus_present": True,
                    "detail": "absent from the VtMB install index but present in the corpus",
                })

    def run(self) -> None:
        buckets = self.from_map_dependencies()
        for dep in buckets.get("sound-scheme", []):
            self.from_sound_scheme(dep)
        for dep in buckets.get("dialogue", []):
            self.from_dialogue(dep)
        for dep in buckets.get("scene", []):
            self.from_scene(dep)
        self.from_raw_ents()
        self.playdialogfile_literals()
        self.wires()
        self.cross_check()

    def to_json(self) -> dict[str, Any]:
        counts = collections.Counter(r["disposition"] for r in self.references)
        return {
            "map": self.map,
            "reference_counts": dict(counts),
            "references": self.references,
            "wires_missing_target": self.wires_missing,
            "wires_unimplemented_input": self.wires_unimplemented,
            "disagreements": self.disagreements,
            "notes": self.notes,
        }


def discover_maps() -> list[str]:
    root = export_root()
    if not root.exists():
        return []
    return sorted(
        p.parent.name for p in root.glob("*/*.ents")
    )


def doors_dir_note(corpus: Corpus) -> str:
    doors = corpus.sound_root / "usable" / "doors"
    openable = corpus.sound_root / "usable" / "openable"
    doors_n = sum(1 for _ in doors.rglob("*")) if doors.exists() else 0
    openable_n = sum(1 for _ in openable.rglob("*")) if openable.exists() else 0
    return (
        f"usable/doors/ exists ({doors_n} entries) but the soundgroup resolver never reads it; "
        f"the door category directory is usable/openable/ ({openable_n} entries)."
    )


def render_markdown(reports: list[dict], corpus: Corpus) -> str:
    lines = ["# AUD0.6 reference validation", "", doors_dir_note(corpus), ""]
    all_dispositions = ("corpus", "mp3-first", "absent-subkey", "never-voiced",
                         "silent-in-retail", "unclassified")
    lines.append("## Per-map disposition counts")
    lines.append("")
    header = ["map"] + list(all_dispositions) + ["missing_target", "unimplemented_input"]
    lines.append("| " + " | ".join(header) + " |")
    lines.append("|" + "---|" * len(header))
    for r in reports:
        row = [r["map"]] + [str(r["reference_counts"].get(d, 0)) for d in all_dispositions]
        row += [str(len(r["wires_missing_target"])), str(len(r["wires_unimplemented_input"]))]
        lines.append("| " + " | ".join(row) + " |")
    lines.append("")
    lines.append("## Non-corpus rows (every reference not disposed `corpus`)")
    lines.append("")
    for r in reports:
        non_corpus = [x for x in r["references"] if x["disposition"] != "corpus"]
        if not non_corpus and not r["wires_missing_target"] and not r["wires_unimplemented_input"]:
            continue
        lines.append(f"### {r['map']}")
        lines.append("")
        if non_corpus:
            lines.append("| path | kind | referrer | disposition | note |")
            lines.append("|---|---|---|---|---|")
            for row in non_corpus:
                lines.append(
                    f"| {row['path']} | {row['kind']} | {row['referrer']} | "
                    f"{row['disposition']} | {row['note']} |"
                )
            lines.append("")
        for w in r["wires_missing_target"]:
            lines.append(f"- missing_target: `{w['source']}.{w['event']} -> {w['target']}.{w['input']}`")
        for w in r["wires_unimplemented_input"]:
            lines.append(
                f"- unimplemented_input: `{w['source']}.{w['event']} -> {w['target']}.{w['input']}` "
                f"({w['cite']})"
            )
        for d in r["disagreements"]:
            lines.append(f"- DISAGREEMENT: {d['path']}: {d['detail']}")
        lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--maps", default="three", choices=["three", "all"],
        help="three playable-path maps (default) or every exported map",
    )
    parser.add_argument("--json", help="write the full machine report here")
    parser.add_argument("--md", help="write the human summary here")
    args = parser.parse_args()

    corpus = Corpus()
    if args.maps == "three":
        map_names = list(THREE_MAPS)
    else:
        map_names = discover_maps()
        for name in THREE_MAPS:
            if name not in map_names:
                map_names.append(name)

    reports = []
    for name in map_names:
        audit = MapAudit(name, corpus)
        audit.run()
        reports.append(audit.to_json())

    for r in reports:
        counts = r["reference_counts"]
        print("%-18s %s  missing_target=%d unimplemented_input=%d" % (
            r["map"], counts, len(r["wires_missing_target"]), len(r["wires_unimplemented_input"]),
        ))

    if args.json:
        with open(args.json, "w", encoding="utf-8") as handle:
            json.dump({"maps": reports, "notes": [doors_dir_note(corpus)]}, handle, indent=1)
        print("wrote %s" % args.json)
    if args.md:
        with open(args.md, "w", encoding="utf-8") as handle:
            handle.write(render_markdown(reports, corpus))
        print("wrote %s" % args.md)

    unclassified_on_three = [
        row for r in reports if r["map"] in THREE_MAPS
        for row in r["references"] if row["disposition"] == "unclassified"
    ]
    if unclassified_on_three:
        print("FAIL: %d unclassified reference(s) on the three playable-path maps" %
              len(unclassified_on_three))
        for row in unclassified_on_three:
            print("  ", row)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
