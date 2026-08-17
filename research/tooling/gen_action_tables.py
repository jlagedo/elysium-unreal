# -*- coding: utf-8 -*-
"""Generate the committed weapon activity-translation tables from the pinned binary.

Owner-run archaeology, not part of any build.  The retail weapon tables are a
game *rule* — the same category as the ``CGameMovement`` constants and the
compiled slot tables this repository already commits — so they are generated
once, reviewed as text, and maintained by hand-free regeneration afterwards.
Nothing in ``uv run elysium build`` or ``uv run elysium export`` reads
``vampire.dll``.

The 9,214 ordered rows over 61 weapon classes are stored compressed, because the
compressed form is the one a reviewer can read and the one the resolver queries:

* 18 shared ordered **base sequences** (792 entries) — the ordered base
  activities one table block walks;
* 110 **block headers**, each naming one base sequence and one animation family;
* 595 **exception rows**, the literal targets a block's family does not produce;
* 57 **required flags**, carried as provenance on the base-sequence entry;
* 3 **rename rules** and 8 **substitute bases**, the two rewrite kinds that are
  not a plain ``_<FAMILY>`` append.

Expanding that model reproduces every recovered row, in the recovered walk order,
with the ``required`` column intact.  The generator asserts the round trip in
process and refuses to write when it fails; the committed file also carries a
digest of the retail row stream so the runtime round-trip test re-proves it with
no game files present.

Usage::

    uv run elysium research gen_action_tables
    uv run elysium research gen_action_tables --check
    uv run elysium research gen_action_tables --out <path>
"""
from __future__ import print_function

import argparse
import collections
import os
import sys

from elysium_pipeline.paths import repo_root

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from probes import weapon_activity_survey  # noqa: E402


DEFAULT_OUTPUT = ("Source", "ElysiumUE", "Private", "Visual",
                  "ElysiumWeaponActivityTables.cpp")

# The three recovered renames.  A rename replaces the whole base literal with a
# fixed prefix plus the block's family, rather than decorating the base.
RENAME_RULES = (
    ("ACT_AIM", "ACT_READY_"),
    ("ACT_RANGE_ATTACK1", "ACT_RANGE_ATTACK_"),
    ("ACT_RANGE_ATTACK1_LAYER", "ACT_RANGE_ATTACK_LAYER_"),
)

# Bases whose trailing token is a family slot rather than a literal direction, so
# the family replaces it instead of being appended to it.  The mirror case —
# `ACT_KNOCKBACK_*_BACK`, where `_BACK` is a real direction — appends, and the
# table gets both right.
SUBSTITUTE_BASES = (
    "ACT_SNEAKATTACK_SUCCESS_ATTACKER_SHORTVICTIM_BACK",
    "ACT_SNEAKATTACK_SUCCESS_ATTACKER_TALLVICTIM_BACK",
    "ACT_SNEAKATTACK_SUCCESS_VICTIM_SHORTATTACKER_BACK",
    "ACT_SNEAKATTACK_SUCCESS_VICTIM_TALLATTACKER_BACK",
    "ACT_SNEAKATTACK_FAILURE_ATTACKER_SHORTVICTIM_BACK",
    "ACT_SNEAKATTACK_FAILURE_ATTACKER_TALLVICTIM_BACK",
    "ACT_SNEAKATTACK_FAILURE_VICTIM_SHORTATTACKER_BACK",
    "ACT_SNEAKATTACK_FAILURE_VICTIM_TALLATTACKER_BACK",
)

_RENAME_BY_BASE = dict(RENAME_RULES)
_SUBSTITUTE_SET = set(SUBSTITUTE_BASES)


def rewrite(base, family):
    """The recovered rewrite: base + block family -> translated activity."""
    if not family:
        return base
    prefix = _RENAME_BY_BASE.get(base)
    if prefix is not None:
        return prefix + family
    if base in _SUBSTITUTE_SET:
        return base.rsplit("_", 1)[0] + "_" + family
    return base + "_" + family


def rewrite_kind(base, family):
    if not family:
        return "identity"
    if base in _RENAME_BY_BASE:
        return "rename"
    if base in _SUBSTITUTE_SET:
        return "substitute"
    return "append"


def split_blocks(rows):
    """Split one weapon's ordered rows into blocks.

    A block ends where a base repeats.  The split is structural rather than
    assumed, which is what makes the ladder a finding: block 1 is the weapon's
    own animation set, block 2 the shared class set, block 3 a cousin weapon —
    and that is exactly the order retail's front-to-back ``ActivityOverride``
    walk consumes.
    """
    blocks = []
    current = []
    seen = set()
    for row in rows:
        base = row["base_activity"]
        if base in seen:
            blocks.append(current)
            current = []
            seen = set()
        current.append(row)
        seen.add(base)
    if current:
        blocks.append(current)
    return blocks


def _family_candidates(classes):
    """Every token that appears as a family somewhere, plus the empty family.

    A block whose rows are all literals carries the empty family and states each
    target as an exception; that is a value rather than a missing one.
    """
    families = {""}
    for row in classes:
        for item in row["rows"]:
            base, target = item["base_activity"], item["weapon_activity"]
            if target.startswith(base + "_"):
                families.add(target[len(base) + 1:])
            prefix = _RENAME_BY_BASE.get(base)
            if prefix is not None and target.startswith(prefix):
                families.add(target[len(prefix):])
            if base in _SUBSTITUTE_SET:
                trunk = base.rsplit("_", 1)[0] + "_"
                if target.startswith(trunk):
                    families.add(target[len(trunk):])
    return sorted(families)


def _fit_family(block, candidates):
    """The block's animation family: the token that leaves the fewest literals.

    Deterministic on a tie — fewest exceptions, then the token that appears most
    often as a direct append, then lexicographic — so a regeneration cannot
    silently reshuffle which rows are exceptions.
    """
    appended = collections.Counter()
    for row in block:
        base, target = row["base_activity"], row["weapon_activity"]
        if target.startswith(base + "_"):
            appended[target[len(base) + 1:]] += 1
    best = None
    for family in candidates:
        misses = sum(1 for row in block
                     if row["weapon_activity"] != rewrite(row["base_activity"], family))
        key = (misses, -appended[family], family)
        if best is None or key < best[0]:
            best = (key, family)
    return best[1]


def _sequence_name(bases, taken):
    stem = "Bases_%d" % len(bases)
    for suffix in "ABCDEFGHIJKLMNOPQRSTUVWXYZ":
        name = "%s_%s" % (stem, suffix)
        if name not in taken:
            return name
    raise ValueError("more than 26 base sequences of length %d" % len(bases))


def build_model(report):
    """Compress the decoded per-class tables into the committed model."""
    classes = [row for row in report["weapon_classes"] if row["row_count"]]
    classes.sort(key=lambda row: row["cpp_class"])
    candidates = _family_candidates(classes)

    sequences = []            # ordered, as emitted
    sequence_index = {}       # bases tuple -> position in `sequences`
    required_by_sequence = {}  # bases tuple -> {index: bool}
    weapons = []

    for entry in classes:
        blocks = split_blocks(entry["rows"])
        weapon = {
            "cpp_class": entry["cpp_class"],
            "entity_classnames": list(entry["entity_classnames"]),
            "table_va": entry["table_va"],
            "blocks": [],
            "exceptions": [],
        }
        for block_index, block in enumerate(blocks):
            bases = tuple(row["base_activity"] for row in block)
            if bases not in sequence_index:
                sequence_index[bases] = len(sequences)
                sequences.append({"bases": bases, "uses": 0})
                required_by_sequence[bases] = {}
            sequences[sequence_index[bases]]["uses"] += 1

            # `required` is a property of the base-sequence entry: every block
            # that walks a sequence agrees on it, across all 61 classes.  A
            # disagreement would mean the flag belongs to the block instead, so
            # it is an error rather than a merge.
            flags = required_by_sequence[bases]
            for position, row in enumerate(block):
                if position in flags and flags[position] != row["required"]:
                    raise ValueError(
                        "`required` disagrees on %s[%d] (%s)" %
                        (bases[position], position, entry["cpp_class"]))
                flags[position] = row["required"]

            family = _fit_family(block, candidates)
            weapon["blocks"].append({"sequence": sequence_index[bases], "family": family})
            for row in block:
                base, target = row["base_activity"], row["weapon_activity"]
                if target != rewrite(base, family):
                    weapon["exceptions"].append({
                        "block": block_index, "base": base, "target": target,
                    })
        weapon["exceptions"].sort(key=lambda row: (row["block"], row["base"]))
        weapons.append(weapon)

    for record in sequences:
        flags = required_by_sequence[record["bases"]]
        record["required"] = tuple(sorted(i for i, value in flags.items() if value))
    taken = set()
    for record in sequences:
        record["name"] = _sequence_name(record["bases"], taken)
        taken.add(record["name"])

    model = {
        "sequences": sequences,
        "weapons": weapons,
        "renames": RENAME_RULES,
        "substitutes": SUBSTITUTE_BASES,
        "binary": report["binary"],
        "translator": report["translator"],
    }
    model["census"] = census(model)
    return model


def expand(model):
    """Walk the model back out as ordered (class, base, target, required) rows."""
    rows = []
    for weapon in model["weapons"]:
        exceptions = {(row["block"], row["base"]): row["target"]
                      for row in weapon["exceptions"]}
        for block_index, block in enumerate(weapon["blocks"]):
            record = model["sequences"][block["sequence"]]
            required = set(record["required"])
            for position, base in enumerate(record["bases"]):
                target = exceptions.get((block_index, base))
                if target is None:
                    target = rewrite(base, block["family"])
                rows.append((weapon["cpp_class"], base, target, position in required))
    return rows


def retail_rows(report):
    classes = [row for row in report["weapon_classes"] if row["row_count"]]
    classes.sort(key=lambda row: row["cpp_class"])
    return [(entry["cpp_class"], row["base_activity"], row["weapon_activity"],
             bool(row["required"]))
            for entry in classes for row in entry["rows"]]


def row_digest(rows):
    """FNV-1a 64 over the ordered row stream, reproduced verbatim in C++."""
    digest = 0xCBF29CE484222325
    for cpp_class, base, target, required in rows:
        line = "%s|%s|%s|%d\n" % (cpp_class, base, target, 1 if required else 0)
        for byte in line.encode("utf-8"):
            digest ^= byte
            digest = (digest * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return digest


def census(model):
    return {
        "weapon_classes": len(model["weapons"]),
        "base_sequences": len(model["sequences"]),
        "base_entries": sum(len(row["bases"]) for row in model["sequences"]),
        "blocks": sum(len(row["blocks"]) for row in model["weapons"]),
        "exceptions": sum(len(row["exceptions"]) for row in model["weapons"]),
        "required_flags": sum(len(row["required"]) for row in model["sequences"]),
        "renames": len(model["renames"]),
        "substitutes": len(model["substitutes"]),
    }


def verify(model, report):
    """Round trip and rewrite-kind census.  Raises on any disagreement."""
    produced = expand(model)
    recovered = retail_rows(report)
    if len(produced) != len(recovered):
        raise ValueError("model expands to %d rows against %d recovered" %
                         (len(produced), len(recovered)))
    for index, (left, right) in enumerate(zip(produced, recovered)):
        if left != right:
            raise ValueError("row %d disagrees: model %r against retail %r" %
                             (index, left, right))
    kinds = collections.Counter()
    for weapon in model["weapons"]:
        exceptions = {(row["block"], row["base"]) for row in weapon["exceptions"]}
        for block_index, block in enumerate(weapon["blocks"]):
            for base in model["sequences"][block["sequence"]]["bases"]:
                if (block_index, base) in exceptions:
                    kinds["exception"] += 1
                else:
                    kinds[rewrite_kind(base, block["family"])] += 1
    for kind in ("append", "substitute", "rename"):
        if not kinds[kind]:
            raise ValueError("rewrite kind %s produces no row" % kind)
    return produced, kinds


# ---------------------------------------------------------------------------
# Emission
# ---------------------------------------------------------------------------

def _literal(text):
    escaped = text.replace("\\", "\\\\").replace("\"", "\\\"")
    return "TEXT(\"%s\")" % escaped


def _wrap(items, indent, width=100):
    lines = []
    current = indent
    for index, item in enumerate(items):
        piece = item + ("," if index + 1 < len(items) else "")
        if current != indent and len((current + " " + piece).expandtabs(4)) > width:
            lines.append(current)
            current = indent
        current += (" " if current != indent else "") + piece
    if current != indent:
        lines.append(current)
    return lines


def render_cpp(model, digest):
    out = []
    add = out.append
    counts = model["census"]
    rows = expand(model)

    add("// Generated by `uv run elysium research gen_action_tables`. Do not hand-edit.")
    add("//")
    add("// VtMB's weapon activity-translation tables, recovered from the pinned retail")
    add("// `vampire.dll` and committed as project source. A translation table is a game rule, the")
    add("// same category as the `CGameMovement` constants in `ElysiumMoveSolve.h` and the compiled")
    add("// slot tables in `ElysiumSheetSlots.h`; nothing in the build or the export reads the binary.")
    add("// The recovered behaviour is `docs/vtmb/animation_and_movers.md` A.3.")
    add("//")
    add("// %d ordered rows over %d weapon classes are stored as %d shared base sequences"
        % (len(rows), counts["weapon_classes"], counts["base_sequences"]))
    add("// (%d entries), %d block headers, %d exception rows and %d `required` flags."
        % (counts["base_entries"], counts["blocks"], counts["exceptions"],
           counts["required_flags"]))
    add("//")
    add("// Rows are never materialised: retail's `ActivityOverride` walks the ladder front to back")
    add("// and takes the first translated activity the model can play, so the resolver synthesizes a")
    add("// candidate per rung and tests it against the body's own clip vocabulary.")
    add("")
    add("#include \"Visual/ElysiumActionTables.h\"")
    add("")
    add("namespace ElysiumActionTables")
    add("{")
    add("namespace")
    add("{")

    # --- base sequences ---------------------------------------------------
    add("\t// The ordered base activities one block walks. Shared: %d blocks draw on %d sequences."
        % (counts["blocks"], counts["base_sequences"]))
    for record in model["sequences"]:
        add("")
        add("\t// %d entries, walked by %d block(s)." % (len(record["bases"]), record["uses"]))
        add("\tconstexpr const TCHAR* %s[] =" % record["name"])
        add("\t{")
        for line in _wrap([_literal(base) for base in record["bases"]], "\t\t"):
            add(line)
        add("\t};")
        add("\tstatic_assert(UE_ARRAY_COUNT(%s) == %d, \"base sequence %s changed length\");"
            % (record["name"], len(record["bases"]), record["name"]))
        if record["required"]:
            add("\t// Authored `required` positions. Provenance only — the pinned server translator")
            add("\t// never reads the flag, so a flagged row follows the same path as any other.")
            add("\tconstexpr int32 %s_Required[] =" % record["name"])
            add("\t{")
            for line in _wrap([str(index) for index in record["required"]], "\t\t"):
                add(line)
            add("\t};")

    # --- per-weapon blocks and exceptions ---------------------------------
    for weapon in model["weapons"]:
        stem = weapon["cpp_class"]
        add("")
        add("\t// %s%s" % (stem, (" — " + ", ".join(weapon["entity_classnames"]))
                          if weapon["entity_classnames"] else ""))
        add("\tconstexpr FActionBlock %s_Blocks[] =" % stem)
        add("\t{")
        for block in weapon["blocks"]:
            record = model["sequences"][block["sequence"]]
            required = ("%s_Required" % record["name"]) if record["required"] else "nullptr"
            required_count = len(record["required"])
            add("\t\t{ %s, %d, %s, %d, %s },"
                % (record["name"], len(record["bases"]), required, required_count,
                   _literal(block["family"])))
        add("\t};")
        if weapon["entity_classnames"]:
            names = ", ".join(_literal(name) for name in weapon["entity_classnames"])
            line = "\tconstexpr const TCHAR* %s_Classnames[] = { %s };" % (stem, names)
            if len(line.expandtabs(4)) > 100:
                add("\tconstexpr const TCHAR* %s_Classnames[] =" % stem)
                add("\t{")
                for wrapped in _wrap([_literal(name) for name in weapon["entity_classnames"]],
                                     "\t\t"):
                    add(wrapped)
                add("\t};")
            else:
                add(line)
        if weapon["exceptions"]:
            add("\t// Literal targets this ladder's families do not produce, sorted by (block, base).")
            add("\tconstexpr FActionException %s_Exceptions[] =" % stem)
            add("\t{")
            for row in weapon["exceptions"]:
                line = "\t\t{ %d, %s, %s }," % (row["block"], _literal(row["base"]),
                                                _literal(row["target"]))
                if len(line.expandtabs(4)) > 100:
                    add("\t\t{ %d, %s," % (row["block"], _literal(row["base"])))
                    add("\t\t\t%s }," % _literal(row["target"]))
                else:
                    add(line)
            add("\t};")

    # --- the ladder table --------------------------------------------------
    add("")
    add("\t// The 61 classes carrying a non-empty table. The 108 subclasses with an empty one")
    add("\t// translate nothing, exactly as an unarmed body does, and are absent by construction.")
    add("\tconstexpr FWeaponLadder GLadders[] =")
    add("\t{")
    for weapon in model["weapons"]:
        stem = weapon["cpp_class"]
        classnames = ("%s_Classnames" % stem) if weapon["entity_classnames"] else "nullptr"
        exceptions = ("%s_Exceptions" % stem) if weapon["exceptions"] else "nullptr"
        add("\t\t{ %s," % _literal(stem))
        add("\t\t\t%s, %d," % (classnames, len(weapon["entity_classnames"])))
        add("\t\t\t%s_Blocks, %d," % (stem, len(weapon["blocks"])))
        add("\t\t\t%s, %d }," % (exceptions, len(weapon["exceptions"])))
    add("\t};")
    add("\tstatic_assert(UE_ARRAY_COUNT(GLadders) == %d, \"weapon class count changed\");"
        % counts["weapon_classes"])

    # --- rewrite rules -----------------------------------------------------
    add("")
    add("\t// A rename replaces the whole base rather than decorating it. Three recovered rules;")
    add("\t// `docs/vtmb/animation_and_movers.md` A.3 carries the decompiled reading.")
    add("\tconstexpr FActionRename GRenames[] =")
    add("\t{")
    for base, prefix in model["renames"]:
        add("\t\t{ %s, %s }," % (_literal(base), _literal(prefix)))
    add("\t};")
    add("")
    add("\t// Bases whose trailing token is a family slot, so the family replaces it. The mirror")
    add("\t// case `ACT_KNOCKBACK_*_BACK` is a literal direction and appends.")
    add("\tconstexpr const TCHAR* GSubstituteBases[] =")
    add("\t{")
    for base in model["substitutes"]:
        add("\t\t%s," % _literal(base))
    add("\t};")

    add("}")
    add("")
    add("TArrayView<const FWeaponLadder> WeaponLadders()")
    add("{")
    add("\treturn MakeArrayView(GLadders);")
    add("}")
    add("")
    add("TArrayView<const FActionRename> RenameRules()")
    add("{")
    add("\treturn MakeArrayView(GRenames);")
    add("}")
    add("")
    add("TArrayView<const TCHAR* const> SubstituteBases()")
    add("{")
    add("\treturn MakeArrayView(GSubstituteBases);")
    add("}")
    add("")
    add("const FActionTableCensus& Census()")
    add("{")
    add("\tstatic const FActionTableCensus GCensus =")
    add("\t{")
    add("\t\t/* WeaponClasses    */ %d," % counts["weapon_classes"])
    add("\t\t/* BaseSequences    */ %d," % counts["base_sequences"])
    add("\t\t/* BaseEntries      */ %d," % counts["base_entries"])
    add("\t\t/* Blocks           */ %d," % counts["blocks"])
    add("\t\t/* Exceptions       */ %d," % counts["exceptions"])
    add("\t\t/* RequiredFlags    */ %d," % counts["required_flags"])
    add("\t\t/* RetailRows       */ %d," % len(rows))
    add("\t\t/* RetailRequired   */ %d," % sum(1 for row in rows if row[3]))
    add("\t\t/* RetailRowDigest  */ 0x%016xull," % digest)
    add("\t};")
    add("\treturn GCensus;")
    add("}")
    add("}")
    add("")
    return "\n".join(out)


def print_report(model, kinds, digest, output):
    counts = model["census"]
    print("=" * 78)
    print("VtMB weapon activity tables — generated model")
    print("=" * 78)
    print("binary: %s" % model["binary"]["path"])
    print("sha256: %s" % model["binary"]["sha256"])
    print()
    total = len(expand(model))
    print("round trip: %d rows over %d classes reproduced in the recovered order"
          % (total, counts["weapon_classes"]))
    print("row digest: 0x%016x" % digest)
    print()
    print("stored units")
    print("  base sequences      %5d entries over %d sequences"
          % (counts["base_entries"], counts["base_sequences"]))
    print("  block headers       %5d" % counts["blocks"])
    print("  exception rows      %5d" % counts["exceptions"])
    print("  required flags      %5d" % counts["required_flags"])
    print("  rename rules        %5d" % counts["renames"])
    print("  substitute bases    %5d" % counts["substitutes"])
    stored = (counts["base_entries"] + counts["blocks"] + counts["exceptions"] +
              counts["required_flags"] + counts["renames"] + counts["substitutes"])
    print("  total               %5d  (%.1f%% of %d rows)"
          % (stored, 100.0 * stored / total, total))
    print()
    print("rewrite kinds")
    for kind, count in kinds.most_common():
        print("  %-12s %6d" % (kind, count))
    print()
    print("output: %s" % output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", help="override the pinned retail vampire.dll path")
    parser.add_argument("--root", help="override the exported corpus root")
    parser.add_argument("--out", help="override the generated .cpp path")
    parser.add_argument("--check", action="store_true",
                        help="verify the committed file matches; write nothing")
    args = parser.parse_args()

    report = weapon_activity_survey.build_report(args.binary, args.root)
    model = build_model(report)
    produced, kinds = verify(model, report)
    digest = row_digest(produced)
    if digest != row_digest(retail_rows(report)):
        raise ValueError("row digest disagrees with the recovered stream")

    output = args.out or os.fspath(repo_root().joinpath(*DEFAULT_OUTPUT))
    text = render_cpp(model, digest)
    print_report(model, kinds, digest, output)

    if args.check:
        try:
            with open(output, "r", encoding="utf-8", newline="") as handle:
                current = handle.read()
        except OSError:
            print("\nCHECK FAILED: %s is missing" % output)
            return 1
        if current.replace("\r\n", "\n") != text:
            print("\nCHECK FAILED: %s is stale; regenerate it" % output)
            return 1
        print("\ncheck: the committed table matches the pinned binary")
        return 0

    parent = os.path.dirname(os.path.abspath(output))
    if parent:
        os.makedirs(parent, exist_ok=True)
    with open(output, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)
    print("\nwrote %s" % output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
