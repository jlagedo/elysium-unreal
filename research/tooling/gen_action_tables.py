# -*- coding: utf-8 -*-
"""Generate the committed activity tables and action rules from the pinned binary.

Owner-run archaeology, not part of any build.  The retail tables are a game
*rule* — the same category as the ``CGameMovement`` constants and the compiled
slot tables this repository already commits — so they are generated once,
reviewed as text, and maintained by hand-free regeneration afterwards.  Nothing
in ``uv run elysium build`` or ``uv run elysium export`` reads ``vampire.dll``.

Two artifacts, one generator.

**The weapon activity-translation tables.**  The 9,214 ordered rows over 61
weapon classes are stored compressed, because the compressed form is the one a
reviewer can read and the one the resolver queries:

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

**The player action rules.**  The ordinary player selector at ``0x10164870`` is
code rather than a table, so what is emitted is that code as ordered predicate
rows: three pose writes, an unconditional eight-row gait ladder, one arm per
compact ``PLAYER_*`` code, the two player-side translations, and the effective
``Player_Anim`` fields.  Every activity a row names is emitted with its
registered ID and validated against the binary's own 4,460-entry activity
registry, the seventeen code names against the pointer table at ``0x106ac3a0``,
and the reachability column against the complete ``+0x704`` caller pass — so a
drifted binary fails generation rather than producing a plausible file.

Usage::

    uv run elysium research gen_action_tables
    uv run elysium research gen_action_tables --check
    uv run elysium research gen_action_tables --only player
    uv run elysium research gen_action_tables --only weapons --out <path>
"""
from __future__ import print_function

import argparse
import collections
import hashlib
import os
import sys

from elysium_pipeline.paths import repo_root, vtmb_root

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from probes import player_action_survey, weapon_activity_survey  # noqa: E402


DEFAULT_OUTPUT = ("Source", "ElysiumUE", "Private", "Visual",
                  "ElysiumWeaponActivityTables.cpp")
DEFAULT_PLAYER_OUTPUT = ("Source", "ElysiumUE", "Private", "Visual",
                         "ElysiumPlayerActionRules.cpp")

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


# ---------------------------------------------------------------------------
# The player action rules
# ---------------------------------------------------------------------------

# The predicate vocabulary, in the order `EPlayerPredicate` declares it.  The
# emitted `static_assert` on `EPlayerPredicate::Count` is what keeps the header
# and this list from drifting apart: a header edit without a regeneration stops
# compiling rather than silently renumbering a row.
PLAYER_PREDICATES = (
    "Always",
    "Ducking",
    "Airborne",
    "Moving",
    "BelowMoveThreshold",
    "AboveGaitThreshold",
    "CombatReady",
    "Relaxed",
    "GaitIsWalkOrRun",
    "JumpPhase",
    "HasActiveWeapon",
    "MeleeCapable",
    "RangedCapable",
    "SwimStroke",
    "ClimbingUp",
    "InteractionSupplies",
    "ReleasedFeeding",
    "ReleasedSeductive",
    "SequenceUnfinished",
    "NotInPrayer",
    "PrayerReleased",
    "VomitNotEntered",
    "PurgeComplete",
    "IdealActivityIs",
)

# The two predicates that read `Operand`; every other row leaves it zero.
_PHASE_PREDICATE = "JumpPhase"
_IDEAL_PREDICATE = "IdealActivityIs"


def _prule(predicates, activity=None, layer=None, phase=None, ideal=None, note=None):
    """One ordered rule row.

    ``phase`` and ``ideal`` are the two operand forms; ``ideal`` is spelled as an
    activity name and resolved to its registered ID, so the operand is joined to
    the binary's vocabulary exactly as the row's own activity is.
    """
    if phase is not None and ideal is not None:
        raise ValueError("a row carries at most one operand")
    return {
        "predicates": tuple(predicates),
        "activity": activity,
        "layer": layer,
        "phase": phase,
        "ideal": ideal,
        "note": note,
    }


# The gait ladder.  It is **not** inside any compact code's arm: the selector
# computes it unconditionally right after the pose writes, and codes 0, 1 and the
# classifier's -1 match no arm, so it survives to the apply path verbatim.
#
# Written first-match-wins, which the retail body reaches by assigning `ACT_IDLE`,
# overwriting it with `ACT_AIM`, and then letting the speed/duck ladder overwrite
# that in every branch except the stationary one.  `CombatReady` and `Relaxed` are
# deliberately not complements: a morphed body in combat stance satisfies neither
# and keeps the plain gait.
PLAYER_GAIT_LADDER = (
    _prule(("BelowMoveThreshold", "Ducking"), "ACT_CROUCH"),
    _prule(("BelowMoveThreshold", "CombatReady"), "ACT_AIM"),
    _prule(("BelowMoveThreshold",), "ACT_IDLE"),
    # Reached only above the idle floor, so the ducked ladder is a flat two-state
    # pair with no walk/run split and no relaxed variant.
    _prule(("Ducking",), "ACT_SNEAK"),
    _prule(("AboveGaitThreshold", "Relaxed"), "ACT_RUN_RELAXED"),
    _prule(("AboveGaitThreshold",), "ACT_RUN"),
    _prule(("Relaxed",), "ACT_WALK_RELAXED"),
    _prule((), "ACT_WALK"),
)


# One arm per compact code that has one.  A code absent here has no distinct
# selector branch and the gait ladder's answer stands.
PLAYER_ARMS = {
    # The jump/landing phase switch.  Phases 8 and 9 defer to a moving gait
    # rather than dropping a running body into a land, and phases 10 and 11 share
    # the hard landing.  A phase outside 1..11 leaves the ladder's answer alone.
    2: (
        _prule(("JumpPhase",), "ACT_LEAP", phase=1),
        _prule(("JumpPhase",), "ACT_HOP", phase=2),
        _prule(("JumpPhase",), "ACT_HOP_UP", phase=3),
        _prule(("JumpPhase",), "ACT_HOP_DOWN", phase=4),
        _prule(("JumpPhase",), "ACT_LEAP_ASCEND", phase=5),
        _prule(("JumpPhase",), "ACT_LEAP_DESCEND", phase=6),
        _prule(("JumpPhase",), "ACT_FALLING", phase=7),
        _prule(("JumpPhase", "GaitIsWalkOrRun"), None, phase=8,
               note="a moving body keeps its gait through the landing"),
        _prule(("JumpPhase", "Ducking"), "ACT_LAND_CROUCH", phase=8),
        _prule(("JumpPhase",), "ACT_LAND", phase=8),
        _prule(("JumpPhase", "GaitIsWalkOrRun"), None, phase=9,
               note="a moving body keeps its gait through the landing"),
        _prule(("JumpPhase", "Ducking"), "ACT_LAND_CROUCH", phase=9),
        _prule(("JumpPhase",), "ACT_LAND", phase=9),
        _prule(("JumpPhase",), "ACT_LAND_HARD", phase=10),
        _prule(("JumpPhase",), "ACT_LAND_HARD", phase=11),
    ),
    # Melee capability replaces the base; ranged capability only adds a layer, so
    # an armed player keeps walking while the upper body fires.
    5: (
        _prule(("HasActiveWeapon", "MeleeCapable", "Airborne"), "ACT_MELEE_AIR_ATTACK"),
        _prule(("HasActiveWeapon", "MeleeCapable"), "ACT_MELEE_ATTACK"),
        _prule(("HasActiveWeapon", "RangedCapable"), None,
               layer="ACT_RANGE_ATTACK1_LAYER"),
    ),
    # A protected retained chain: begin, loop while the prayer is held, end, idle.
    # The first two rows are the unfinished-clip case, so every later row is
    # already the finished one.
    7: (
        _prule(("SequenceUnfinished", "NotInPrayer"), "ACT_PRAYING_BEGIN"),
        _prule(("SequenceUnfinished",), None, note="hold the stored ideal"),
        _prule(("IdealActivityIs",), "ACT_PRAYING_IDLE", ideal="ACT_PRAYING_BEGIN"),
        _prule(("IdealActivityIs", "PrayerReleased"), "ACT_PRAYING_END",
               ideal="ACT_PRAYING_IDLE"),
        _prule(("IdealActivityIs",), "ACT_PRAYING_IDLE", ideal="ACT_PRAYING_IDLE"),
        _prule(("IdealActivityIs",), "ACT_IDLE", ideal="ACT_PRAYING_END"),
        _prule((), "ACT_PRAYING_BEGIN"),
    ),
    # The attacker side of a released grapple.  Neither flag leaves the gait.
    8: (
        _prule(("ReleasedFeeding",), "ACT_FEEDING_RELEASED_IDLE_ATTACKER"),
        _prule(("ReleasedSeductive",), "ACT_SEDUCTIVE_RELEASED_IDLE_ATTACKER"),
    ),
    9: (
        _prule(("SwimStroke",), "ACT_SWIM"),
        _prule((), "ACT_TREADWATER"),
    ),
    # The interaction entity answers with its own activity, so the row names none.
    10: (
        _prule(("InteractionSupplies",), None,
               note="the interaction entity supplies the activity"),
    ),
    11: (
        _prule(("ClimbingUp",), "ACT_CLIMB_UP"),
        _prule((), "ACT_CLIMB_DOWN"),
    ),
    # The other protected retained chain, and the only code the action latch keeps.
    # `PurgeComplete` is what releases the loop: the idle holds while the blood
    # pool drains and the purge effect is applied.
    12: (
        _prule(("SequenceUnfinished", "VomitNotEntered"), "ACT_VOMIT_INTO"),
        _prule(("SequenceUnfinished",), None, note="hold the stored ideal"),
        _prule(("IdealActivityIs",), "ACT_VOMIT_IDLE", ideal="ACT_VOMIT_INTO"),
        _prule(("IdealActivityIs", "PurgeComplete"), "ACT_VOMIT_GETOUT",
               ideal="ACT_VOMIT_IDLE"),
        _prule(("IdealActivityIs",), "ACT_VOMIT_IDLE", ideal="ACT_VOMIT_IDLE"),
        _prule(("IdealActivityIs",), "ACT_IDLE", ideal="ACT_VOMIT_GETOUT"),
        _prule((), "ACT_VOMIT_INTO"),
    ),
    13: (
        _prule((), "ACT_PREBLOCK"),
    ),
    # Reload is a layer too, so the gait keeps the legs.
    14: (
        _prule(("HasActiveWeapon",), None, layer="ACT_RELOAD_LAYER"),
    ),
}


# The three pose parameters the ordinary selector writes, in write order.
PLAYER_POSE_WRITES = (
    {
        "parameter": "move_yaw",
        "source": "VelocityAgainstFacing",
        "value": 0.0,
        "gate": "Moving",
        "slew": 720.0,
        "note": "right-positive with zero forward; a stationary body holds its "
                "last value rather than returning it to zero",
    },
    {
        "parameter": "aim_yaw",
        "source": "Literal",
        "value": 0.0,
        "gate": "Always",
        "slew": 0.0,
        "note": "a literal zero on every call in every state, so a weapon aim "
                "grid is pitch-only and there is nothing for a torso follower "
                "to drive",
    },
    {
        "parameter": "aim_pitch",
        "source": "AimPitchField",
        "value": 0.0,
        "gate": "Always",
        "slew": 0.0,
        "note": "an absolute angle from +0x206c, wrapped by 360",
    },
)


# `CBasePlayer::NPC_TranslateActivity` at 0x101647a0.  Two rows, everything else
# unchanged.
PLAYER_TRANSLATIONS = (
    ("ACT_WALK_RELAXED", "ACT_WALK"),
    ("ACT_RUN_RELAXED", "ACT_RUN"),
)


PLAYER_TUNING = {
    "move_threshold": 5.0,
    "gait_threshold_offset": 1.0,
    "pose_slew_degrees_per_second": 720.0,
    "pose_slew_window_seconds": 0.3,
    "latched_code": 12,
    "melee_hold_ideal": "ACT_MELEE_ATTACK",
    "melee_hold_refuses": ("ACT_IDLE", "ACT_AIM"),
}


def _decode_activity_registry(binary_path):
    """The binary's own 4,460-entry activity table, as name <-> ID both ways."""
    with open(binary_path, "rb") as handle:
        data = handle.read()
    digest = hashlib.sha256(data).hexdigest()
    if digest != weapon_activity_survey.PINNED_SHA256:
        raise ValueError("unsupported vampire.dll SHA-256 %s" % digest)
    image = weapon_activity_survey.PEImage(data)
    by_name = {}
    for row in weapon_activity_survey.decode_activity_registry(image):
        previous = by_name.setdefault(row["name"], row["id"])
        if previous != row["id"]:
            raise ValueError("activity %s registers as both %d and %d" %
                             (row["name"], previous, row["id"]))
    return by_name


def _resolve_activity(by_name, name):
    if name is None:
        return None, 0
    activity_id = by_name.get(name)
    if activity_id is None:
        raise ValueError("activity %s is not registered in the pinned binary" % name)
    return name, activity_id


def build_player_model(report, activity_by_name):
    """Join the recovered rules to the decoded code table, registry and vdata."""
    unknown = set()
    for rows in (PLAYER_GAIT_LADDER,) + tuple(PLAYER_ARMS.values()):
        for row in rows:
            unknown |= set(row["predicates"]) - set(PLAYER_PREDICATES)
            if len(row["predicates"]) > 3:
                raise ValueError("a rule row conjoins more than three predicates")
            operands = [row["phase"] is not None, row["ideal"] is not None]
            wants_operand = (_PHASE_PREDICATE in row["predicates"],
                             _IDEAL_PREDICATE in row["predicates"])
            if operands != list(wants_operand):
                raise ValueError("operand and predicate disagree on %r" % (row,))
    if unknown:
        raise ValueError("unknown predicates %s" % sorted(unknown))

    def resolve(rows):
        resolved = []
        for row in rows:
            activity, activity_id = _resolve_activity(activity_by_name, row["activity"])
            layer, layer_id = _resolve_activity(activity_by_name, row["layer"])
            operand = 0
            if row["phase"] is not None:
                operand = row["phase"]
            elif row["ideal"] is not None:
                _, operand = _resolve_activity(activity_by_name, row["ideal"])
            resolved.append({
                "predicates": row["predicates"],
                "operand": operand,
                "operand_note": row["ideal"],
                "activity": activity,
                "activity_id": activity_id,
                "layer": layer,
                "layer_id": layer_id,
                "note": row["note"],
            })
        return resolved

    actions = []
    for row in report["actions"]:
        code = row["code"]
        dormant = row["reachability"] != "reachable"
        rules = resolve(PLAYER_ARMS.get(code, ()))
        if dormant and rules:
            raise ValueError("dormant code %d carries an arm" % code)
        actions.append({
            "code": code,
            "name": row["expected_name"],
            "dormant": dormant,
            "producer": row["producer_policy"],
            "rules": rules,
        })
    if len(actions) != player_action_survey.PLAYER_ACTION_COUNT:
        raise ValueError("the compact action vocabulary is not %d codes" %
                         player_action_survey.PLAYER_ACTION_COUNT)
    for code in PLAYER_ARMS:
        if code >= len(actions):
            raise ValueError("arm %d addresses no compiled code" % code)

    by_code = {row["name"]: row["code"] for row in actions}
    anim_fields = []
    for row in sorted(report["patch_first_player_anim_values"],
                      key=lambda item: (item["path"], item["byte_offset"])):
        if row["value"] not in by_code:
            raise ValueError("Player_Anim %s names no compiled code" % row["value"])
        anim_fields.append({
            "path": row["path"],
            "value": row["value"],
            "code": by_code[row["value"]],
        })

    translations = []
    for source, target in PLAYER_TRANSLATIONS:
        from_name, from_id = _resolve_activity(activity_by_name, source)
        to_name, to_id = _resolve_activity(activity_by_name, target)
        translations.append({"from": from_name, "from_id": from_id,
                             "to": to_name, "to_id": to_id})

    tuning = dict(PLAYER_TUNING)
    tuning["melee_hold_ideal_id"] = _resolve_activity(
        activity_by_name, tuning["melee_hold_ideal"])[1]
    for name in tuning["melee_hold_refuses"]:
        _resolve_activity(activity_by_name, name)

    model = {
        "actions": actions,
        "gait": resolve(PLAYER_GAIT_LADDER),
        "pose_writes": PLAYER_POSE_WRITES,
        "translations": translations,
        "anim_fields": anim_fields,
        "tuning": tuning,
        "binary": report["binary"],
        "addresses": report["addresses"],
        "latch": report["retained_action"],
    }
    model["activities"] = player_activities(model)
    model["census"] = player_census(model)
    return model


def player_activities(model):
    """Every distinct activity the surface names, in the order C++ collects them."""
    ordered = []
    seen = set()

    def add(name):
        if name is None or name.upper() in seen:
            return
        seen.add(name.upper())
        ordered.append(name)

    for row in model["gait"]:
        add(row["activity"])
        add(row["layer"])
    for action in model["actions"]:
        for row in action["rules"]:
            add(row["activity"])
            add(row["layer"])
    for row in model["translations"]:
        add(row["to"])
    add(model["tuning"]["melee_hold_ideal"])
    return ordered


def player_census(model):
    return {
        "actions": len(model["actions"]),
        "dormant": sum(1 for row in model["actions"] if row["dormant"]),
        "gait_rules": len(model["gait"]),
        "arm_rules": sum(len(row["rules"]) for row in model["actions"]),
        "pose_writes": len(model["pose_writes"]),
        "translations": len(model["translations"]),
        "anim_fields": len(model["anim_fields"]),
        "activities": len(model["activities"]),
        "predicates": len(PLAYER_PREDICATES),
    }


def _rule_line(row, indent):
    """One `FPlayerRule` initialiser, wrapped after the operand when it is long."""
    padded = list(row["predicates"]) + ["Always"] * (3 - len(row["predicates"]))
    head = "%s{ { %s }, %d," % (indent, ", ".join("P::%s" % name for name in padded),
                                row["operand"])
    activity = _literal(row["activity"]) if row["activity"] else "nullptr"
    layer = _literal(row["layer"]) if row["layer"] else "nullptr"
    tail = " %s, %d, %s, %d }," % (activity, row["activity_id"], layer, row["layer_id"])
    if len((head + tail).expandtabs(4)) <= 100:
        return [head + tail]
    return [head, indent + "\t" + tail.strip()]


def _comment(text, indent):
    """Wrap prose as `//` lines under the repository's 100-column rule."""
    lines = []
    current = ""
    for word in text.split():
        candidate = (current + " " + word) if current else word
        if len((indent + "// " + candidate).expandtabs(4)) > 100 and current:
            lines.append("%s// %s" % (indent, current))
            current = word
        else:
            current = candidate
    if current:
        lines.append("%s// %s" % (indent, current))
    return lines


def _rule_block(name, rows, comment_lines):
    out = []
    for line in comment_lines:
        out.extend(_comment(line, "\t"))
    out.append("\tconstexpr FPlayerRule %s[] =" % name)
    out.append("\t{")
    for row in rows:
        if row["note"]:
            out.extend(_comment(row["note"], "\t\t"))
        if row["operand_note"]:
            out.extend(_comment("... while the stored ideal is %s." % row["operand_note"],
                                "\t\t"))
        out.extend(_rule_line(row, "\t\t"))
    out.append("\t};")
    return out


def render_player_cpp(model):
    out = []
    add = out.append
    counts = model["census"]

    add("// Generated by `uv run elysium research gen_action_tables`. Do not hand-edit.")
    add("//")
    add("// VtMB's player action selection, recovered from the pinned retail `vampire.dll` and")
    add("// committed as project source. The ordinary selector at `%s` is code rather than a"
        % model["addresses"]["ordinary_selector"])
    add("// table, so what is stored is that code as ordered predicate rows: the pose writes, the")
    add("// unconditional gait ladder, one arm per compact `PLAYER_*` code, the two player-side")
    add("// translations and the effective `Player_Anim` fields.")
    add("//")
    add("// %d compact codes, %d of them dormant — compiled vocabulary with no classifier edge, no"
        % (counts["actions"], counts["dormant"]))
    add("// native caller, no retained-latch writer and no effective `Player_Anim` value. %d gait"
        % counts["gait_rules"])
    add("// rules, %d arm rules, %d pose writes and %d distinct activities."
        % (counts["arm_rules"], counts["pose_writes"], counts["activities"]))
    add("//")
    add("// The recovered behaviour is `docs/vtmb/animation_and_movers.md` A.3. Every activity below")
    add("// carries its registered ID, taken from the binary's own registration table, so a row is")
    add("// joined to VtMB's vocabulary rather than to a spelling.")
    add("")
    add("#include \"Visual/ElysiumActionTables.h\"")
    add("")
    add("namespace ElysiumActionTables")
    add("{")
    add("namespace")
    add("{")
    add("\t// A row's predicates are AND-ed and `Always`-padded; the alias is what lets one fit a line.")
    add("\tusing P = EPlayerPredicate;")
    add("\tstatic_assert(static_cast<int32>(P::Count) == %d," % len(PLAYER_PREDICATES))
    add("\t\t\"the predicate vocabulary changed; regenerate the player action rules\");")
    add("")

    add_lines = _rule_block("GGaitLadder", model["gait"], (
        "The gait ladder. It is **not** inside any code's arm: the selector computes it",
        "unconditionally right after the pose writes, and codes `0`, `1` and the classifier's",
        "`-1` match no arm, so it survives to the apply path verbatim. Every operand is",
        "recomputed each call — no gait memory, and the threshold offset is a fixed bias applied",
        "in both directions rather than a hysteresis band.",
    ))
    out.extend(add_lines)

    for action in model["actions"]:
        if not action["rules"]:
            continue
        add("")
        out.extend(_rule_block("G%sRules" % _camel(action["name"]), action["rules"],
                               ("%s — %s." % (action["name"], action["producer"]),)))

    add("")
    add("\t// The seventeen compiled codes, in table order. An empty arm is a value: the gait")
    add("\t// ladder's answer stands, which is the whole selection for codes `0` and `1`.")
    add("\tconstexpr FPlayerAction GActions[] =")
    add("\t{")
    for action in model["actions"]:
        reach = "EPlayerReach::Dormant" if action["dormant"] else "EPlayerReach::Reachable"
        rules = ("G%sRules" % _camel(action["name"])) if action["rules"] else "nullptr"
        add("\t\t{ %d, %s, %s," % (action["code"], _literal(action["name"]), reach))
        for line in _wrap_text(action["producer"], "\t\t\t", prefix="", width=98):
            add(line)
        add("\t\t\t%s, %d }," % (rules, len(action["rules"])))
    add("\t};")
    add("\tstatic_assert(UE_ARRAY_COUNT(GActions) == %d, \"the compact action vocabulary changed\");"
        % counts["actions"])

    add("")
    add("\t// The three pose parameters the selector writes, in write order.")
    add("\tconstexpr FPlayerPoseWrite GPoseWrites[] =")
    add("\t{")
    for row in model["pose_writes"]:
        out.extend(_comment("%s: %s." % (row["parameter"], row["note"]), "\t\t"))
        add("\t\t{ %s, EPlayerPoseSource::%s, %sf, P::%s, %sf },"
            % (_literal(row["parameter"]), row["source"], _float(row["value"]),
               row["gate"], _float(row["slew"])))
    add("\t};")

    add("")
    add("\t// `CBasePlayer::NPC_TranslateActivity`. Without these two rows an unarmed gait request")
    add("\t// selects nothing: a player body carries no relaxed sequence, only weapon-suffixed ones.")
    add("\tconstexpr FPlayerTranslation GTranslations[] =")
    add("\t{")
    for row in model["translations"]:
        add("\t\t{ %s, %d, %s, %d }," % (_literal(row["from"]), row["from_id"],
                                         _literal(row["to"]), row["to_id"]))
    add("\t};")

    add("")
    add("\t// The effective (patch-first) vdata search path's complete `Player_Anim` surface.")
    add("\tconstexpr FPlayerAnimField GAnimFields[] =")
    add("\t{")
    for row in model["anim_fields"]:
        add("\t\t{ %s, %s, %d }," % (_literal(row["path"]), _literal(row["value"]),
                                     row["code"]))
    add("\t};")

    add("")
    add("\t// An unfinished swing refuses these: the selector returns without applying rather than")
    add("\t// cutting the attack short.")
    add("\tconstexpr const TCHAR* GMeleeHoldRefuses[] = { %s };"
        % ", ".join(_literal(name) for name in model["tuning"]["melee_hold_refuses"]))
    add("}")

    add("")
    add("TArrayView<const FPlayerAction> PlayerActions()")
    add("{")
    add("\treturn MakeArrayView(GActions);")
    add("}")
    add("")
    add("TArrayView<const FPlayerRule> PlayerGaitLadder()")
    add("{")
    add("\treturn MakeArrayView(GGaitLadder);")
    add("}")
    add("")
    add("TArrayView<const FPlayerPoseWrite> PlayerPoseWrites()")
    add("{")
    add("\treturn MakeArrayView(GPoseWrites);")
    add("}")
    add("")
    add("TArrayView<const FPlayerTranslation> PlayerTranslations()")
    add("{")
    add("\treturn MakeArrayView(GTranslations);")
    add("}")
    add("")
    add("TArrayView<const FPlayerAnimField> PlayerAnimFields()")
    add("{")
    add("\treturn MakeArrayView(GAnimFields);")
    add("}")
    add("")
    tuning = model["tuning"]
    add("const FPlayerActionTuning& PlayerTuning()")
    add("{")
    add("\tstatic const FPlayerActionTuning GTuning =")
    add("\t{")
    add("\t\t/* MoveThreshold             */ %sf," % _float(tuning["move_threshold"]))
    add("\t\t/* GaitThresholdOffset       */ %sf," % _float(tuning["gait_threshold_offset"]))
    add("\t\t/* PoseSlewDegreesPerSecond  */ %sf,"
        % _float(tuning["pose_slew_degrees_per_second"]))
    add("\t\t/* PoseSlewWindowSeconds     */ %sf,"
        % _float(tuning["pose_slew_window_seconds"]))
    add("\t\t/* LatchedCode               */ %d," % tuning["latched_code"])
    add("\t\t/* MeleeHoldIdeal            */ %s," % _literal(tuning["melee_hold_ideal"]))
    add("\t\t/* MeleeHoldIdealId          */ %d," % tuning["melee_hold_ideal_id"])
    add("\t\t/* MeleeHoldRefuses          */ GMeleeHoldRefuses,")
    add("\t\t/* MeleeHoldRefuseCount      */ %d," % len(tuning["melee_hold_refuses"]))
    add("\t};")
    add("\treturn GTuning;")
    add("}")
    add("")
    add("const FPlayerActionCensus& PlayerCensus()")
    add("{")
    add("\tstatic const FPlayerActionCensus GCensus =")
    add("\t{")
    add("\t\t/* Actions       */ %d," % counts["actions"])
    add("\t\t/* Dormant       */ %d," % counts["dormant"])
    add("\t\t/* GaitRules     */ %d," % counts["gait_rules"])
    add("\t\t/* ArmRules      */ %d," % counts["arm_rules"])
    add("\t\t/* PoseWrites    */ %d," % counts["pose_writes"])
    add("\t\t/* Translations  */ %d," % counts["translations"])
    add("\t\t/* AnimFields    */ %d," % counts["anim_fields"])
    add("\t\t/* Activities    */ %d," % counts["activities"])
    add("\t\t/* Predicates    */ %d," % counts["predicates"])
    add("\t};")
    add("\treturn GCensus;")
    add("}")
    add("}")
    add("")
    return "\n".join(out)


def _camel(name):
    """`PLAYER_START_AIMING` -> `PlayerStartAiming`, for a generated symbol."""
    return "".join(part.capitalize() for part in name.split("_"))


def _float(value):
    text = repr(float(value))
    return text


def _wrap_text(text, indent, prefix="", width=98):
    """Wrap a prose field as a C++ string literal continuation."""
    words = (prefix + text).split()
    lines = []
    current = ""
    for word in words:
        candidate = (current + " " + word) if current else word
        if len((indent + "TEXT(\"" + candidate + "\")").expandtabs(4)) > width and current:
            lines.append(current)
            current = word
        else:
            current = candidate
    if current:
        lines.append(current)
    return ["%sTEXT(\"%s\")%s" % (indent, line + (" " if index + 1 < len(lines) else ""),
                                  "" if index + 1 < len(lines) else ",")
            for index, line in enumerate(lines)]


def print_player_report(model, output):
    counts = model["census"]
    print("=" * 78)
    print("VtMB player action rules — generated model")
    print("=" * 78)
    print("binary: %s" % model["binary"]["path"])
    print("sha256: %s" % model["binary"]["sha256"])
    print()
    print("compact codes       %5d  (%d dormant: %s)"
          % (counts["actions"], counts["dormant"],
             ", ".join(row["name"] for row in model["actions"] if row["dormant"])))
    print("gait rules          %5d" % counts["gait_rules"])
    print("arm rules           %5d over %d codes"
          % (counts["arm_rules"], sum(1 for row in model["actions"] if row["rules"])))
    print("pose writes         %5d  (%s)"
          % (counts["pose_writes"],
             ", ".join(row["parameter"] for row in model["pose_writes"])))
    print("translations        %5d" % counts["translations"])
    print("Player_Anim fields  %5d  (%s)"
          % (counts["anim_fields"],
             ", ".join(sorted({row["value"] for row in model["anim_fields"]}))))
    print("distinct activities %5d" % counts["activities"])
    print()
    print("output: %s" % output)


# ---------------------------------------------------------------------------
# Driver
# ---------------------------------------------------------------------------

def _emit(output, text, check):
    if check:
        try:
            with open(output, "r", encoding="utf-8", newline="") as handle:
                current = handle.read()
        except OSError:
            print("\nCHECK FAILED: %s is missing" % output)
            return 1
        if current.replace("\r\n", "\n") != text:
            print("\nCHECK FAILED: %s is stale; regenerate it" % output)
            return 1
        print("\ncheck: %s matches the pinned binary" % os.path.basename(output))
        return 0

    parent = os.path.dirname(os.path.abspath(output))
    if parent:
        os.makedirs(parent, exist_ok=True)
    with open(output, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)
    print("\nwrote %s" % output)
    return 0


def generate_weapons(args):
    report = weapon_activity_survey.build_report(args.binary, args.root)
    model = build_model(report)
    produced, kinds = verify(model, report)
    digest = row_digest(produced)
    if digest != row_digest(retail_rows(report)):
        raise ValueError("row digest disagrees with the recovered stream")

    output = args.out or os.fspath(repo_root().joinpath(*DEFAULT_OUTPUT))
    text = render_cpp(model, digest)
    print_report(model, kinds, digest, output)
    return _emit(output, text, args.check)


def generate_player(args):
    binary = args.binary or os.fspath(vtmb_root() / "Vampire" / "dlls" / "vampire.dll")
    report = player_action_survey.build_report(binary)
    model = build_player_model(report, _decode_activity_registry(binary))

    output = args.out or os.fspath(repo_root().joinpath(*DEFAULT_PLAYER_OUTPUT))
    text = render_player_cpp(model)
    print_player_report(model, output)
    return _emit(output, text, args.check)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", help="override the pinned retail vampire.dll path")
    parser.add_argument("--root", help="override the exported corpus root")
    parser.add_argument("--only", choices=("weapons", "player"),
                        help="generate one artifact instead of both")
    parser.add_argument("--out", help="override the generated .cpp path; needs --only")
    parser.add_argument("--check", action="store_true",
                        help="verify the committed file matches; write nothing")
    args = parser.parse_args()

    if args.out and not args.only:
        parser.error("--out addresses one artifact; pass --only weapons or --only player")

    status = 0
    if args.only in (None, "weapons"):
        status |= generate_weapons(args)
        if args.only is None:
            print()
    if args.only in (None, "player"):
        status |= generate_player(args)
    return status


if __name__ == "__main__":
    sys.exit(main())
