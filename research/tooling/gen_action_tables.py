# -*- coding: utf-8 -*-
"""Generate the committed activity tables and action rules from the pinned binary.

Owner-run archaeology, not part of any build.  The retail tables are a game
*rule* — the same category as the ``CGameMovement`` constants and the compiled
slot tables this repository already commits — so they are generated once,
reviewed as text, and maintained by hand-free regeneration afterwards.  Nothing
in ``uv run elysium build`` or ``uv run elysium export`` reads ``vampire.dll``.

Three artifacts, one generator.

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

**The NPC translation surface.**  The 77 ``CAI_BaseNPC`` descendants collapse to
ten ``+0x5dc`` pre-translation bodies, five ``+0x5e0`` class-translation bodies
and two implementations each of the ``+0x8e4`` cover and ``+0x8e8`` reload
delegates, plus the one non-virtual ``NPC_EarlyTranslateActivity`` tail.  Each
body is stored the way the player selector is — ordered rules over a closed
predicate vocabulary, with the inherited body chained before or after them — and
every address, inheritor count and owning class is re-decoded from the pinned
RTTI/vtable walk at generation time, so a ledger that drifted fails rather than
writes.  The 100 task policies over 111 task routes come from the decompiled
override ledger in ``npc_task_override_survey``, joined to both task registries.
The 232 paired-action grapple variants are not enumerated: each of the 29
registered bases carries a role order, and the generator requires that order to
reproduce all eight registered names before it emits the base.

Usage::

    uv run elysium research gen_action_tables
    uv run elysium research gen_action_tables --check
    uv run elysium research gen_action_tables --only player
    uv run elysium research gen_action_tables --only npc
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

from probes import (  # noqa: E402
    npc_task_override_survey,
    npc_translation_survey,
    player_action_survey,
    weapon_activity_survey,
)


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
    head = "%s{ { %s }, %d," % (indent, ", ".join("PlayerP::%s" % name for name in padded),
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
    add("\t// A row's predicates are AND-ed and `Always`-padded; the alias is what lets one fit a")
    add("\t// line. Unit-prefixed because the module builds adaptive-unity: this anonymous")
    add("\t// namespace is regularly merged with the NPC and weapon tables' own.")
    add("\tusing PlayerP = EPlayerPredicate;")
    add("\tstatic_assert(static_cast<int32>(PlayerP::Count) == %d," % len(PLAYER_PREDICATES))
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
        add("\t\t{ %s, EPlayerPoseSource::%s, %sf, PlayerP::%s, %sf },"
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
# The NPC translation surface
# ---------------------------------------------------------------------------

DEFAULT_NPC_OUTPUT = ("Source", "ElysiumUE", "Private", "Visual",
                      "ElysiumNpcActivityTables.cpp")

# The predicate vocabulary, in the order `ENpcPredicate` declares it.  The
# emitted `static_assert` on `ENpcPredicate::Count` is what keeps the header and
# this list from drifting apart.
NPC_PREDICATES = (
    "Always",
    "GaitOverrideRun",
    "GaitOverrideWalk",
    "MovementPolicyFrenzy",
    "MovementPolicyRun",
    "ReloadFastCapable",
    "CoverCapable",
    "CoverIdleFlagged",
    "NoAimGait",
    "ArmedAlert",
    "NotArmedAlert",
    "RangedAimCapable",
    "LaughIdleFlagged",
    "FormBit",
    "BodySideLeft",
    "RunnerVariantIs",
    "CoverContextIs",
    "ForcedLowCover",
)

# The three predicates that read `Operand`; every other row leaves it zero.
_OPERAND_PREDICATES = ("RunnerVariantIs", "CoverContextIs")

# The eight contiguous role variants a grapple base registers, as the suffix each
# one appends.  Which order a family uses is *decided against the binary* rather
# than assumed: `build_npc_model` requires one of the two to reproduce all eight
# registered names exactly, so a base that matched neither would fail generation.
GRAPPLE_ROLE_SUFFIXES = {
    "Canonical": (
        "ATTACKER_SHORTVICTIM_FRONT",
        "ATTACKER_TALLVICTIM_FRONT",
        "VICTIM_SHORTATTACKER_FRONT",
        "VICTIM_TALLATTACKER_FRONT",
        "ATTACKER_SHORTVICTIM_BACK",
        "ATTACKER_TALLVICTIM_BACK",
        "VICTIM_SHORTATTACKER_BACK",
        "VICTIM_TALLATTACKER_BACK",
    ),
    "AttackerVictimSwapped": (
        "VICTIM_SHORTATTACKER_FRONT",
        "VICTIM_TALLATTACKER_FRONT",
        "ATTACKER_SHORTVICTIM_FRONT",
        "ATTACKER_TALLVICTIM_FRONT",
        "VICTIM_SHORTATTACKER_BACK",
        "VICTIM_TALLATTACKER_BACK",
        "ATTACKER_SHORTVICTIM_BACK",
        "ATTACKER_TALLVICTIM_BACK",
    ),
}

# The probe's route vocabulary, joined to `ENpcTaskRoute`.
NPC_TASK_ROUTES = {
    "set_ideal": "SetIdeal",
    "restart_ideal": "RestartIdeal",
    "set_activity": "SetActivity",
    "restart_ideal_choice": "RestartIdealChoice",
    "set_ideal_argument": "SetIdealArgument",
    "set_ideal_navigator": "SetIdealNavigator",
    "remap_shared_task": "RemapSharedTask",
}


def _nrule(predicates=(), frm=None, to=None, route="Rewrite", operand=0,
           family=None, delegate=None):
    """One ordered translation row.

    ``family`` stands in for ``frm`` where the recovered reading names a family of
    requests without enumerating it; such a row carries no literal and the runtime
    walk refuses to guess one.
    """
    if frm is not None and family is not None:
        raise ValueError("a row matches a literal or a family, not both")
    return {
        "predicates": tuple(predicates),
        "operand": operand,
        "from": frm,
        "family": family,
        "to": to,
        "route": route,
        "delegate": delegate,
    }


# The recovered body ledger.  `docs/vtmb/animation_and_movers.md` A.3 is the
# reading; every address, inheritor count and owner below is re-decoded from the
# pinned binary at generation time and disagreement fails rather than writes.
NPC_BODIES = (
    {
        "name": "PreTranslate_Base",
        "address": 0x10271F50,
        "slot": "PreTranslate",
        "inheritors": 13,
        "policy": "identity",
        "rules": (),
    },
    {
        "name": "PreTranslate_Troika",
        "address": 0x10295590,
        "slot": "PreTranslate",
        "inheritors": 17,
        "policy": "the common Troika gait/frenzy/cover/reload and paired-action "
                  "pre-translation",
        "chain": "EarlyTranslate_Grapple",
        "chain_order": "AfterRules",
        "rules": (
            _nrule(("GaitOverrideRun",), "ACT_WALK", "ACT_RUN"),
            _nrule(("GaitOverrideRun",), "ACT_HUNT_WALK", "ACT_RUN"),
            _nrule(("GaitOverrideWalk",), "ACT_RUN", "ACT_WALK"),
            _nrule(("MovementPolicyFrenzy",), None, "ACT_RUN_FRENZY",
                   family="MoveWalkRunRelaxedHuntCombat"),
            _nrule(("MovementPolicyRun",), None, "ACT_RUN", family="MoveWalking"),
            _nrule((), "ACT_FIDGET", "ACT_IDLE"),
            _nrule(("ReloadFastCapable",), "ACT_RELOAD_FAST", None, "Delegate",
                   delegate="Reload"),
            _nrule((), "ACT_COVER", None, "Delegate", delegate="Cover"),
            _nrule(("CoverIdleFlagged",), "ACT_IDLE", None, "Delegate", delegate="Cover"),
        ),
    },
    {
        "name": "PreTranslate_Human",
        "address": 0x103854F0,
        "slot": "PreTranslate",
        "inheritors": 39,
        "policy": "armed/alert translation, then the common Troika body",
        "chain": "PreTranslate_Troika",
        "chain_order": "AfterRules",
        "rules": (
            _nrule(("NoAimGait",), "ACT_WALK_AIM", "ACT_WALK"),
            _nrule(("NoAimGait",), "ACT_RUN_AIM", "ACT_RUN"),
            _nrule(("NotArmedAlert",), "ACT_WALK", "ACT_WALK_RELAXED"),
            _nrule(("NotArmedAlert",), "ACT_RUN", "ACT_RUN_RELAXED"),
            _nrule(("ArmedAlert", "RangedAimCapable"), "ACT_IDLE", "ACT_AIM"),
            _nrule(("ArmedAlert",), "ACT_TURN_LEFT", "ACT_TURN_LEFT_ALERT"),
            _nrule(("ArmedAlert",), "ACT_TURN_RIGHT", "ACT_TURN_RIGHT_ALERT"),
            _nrule(("ArmedAlert",), "ACT_90_LEFT", "ACT_90_LEFT_ALERT"),
            _nrule(("ArmedAlert",), "ACT_90_RIGHT", "ACT_90_RIGHT_ALERT"),
            _nrule(("ArmedAlert",), "ACT_180_LEFT", "ACT_180_LEFT_ALERT"),
            _nrule(("ArmedAlert",), "ACT_180_RIGHT", "ACT_180_RIGHT_ALERT"),
        ),
    },
    {
        "name": "PreTranslate_Camera",
        "address": 0x103690A0,
        "slot": "PreTranslate",
        "inheritors": 2,
        "policy": "identity",
        "rules": (),
    },
    {
        "name": "PreTranslate_Dog",
        "address": 0x10374AD0,
        "slot": "PreTranslate",
        "inheritors": 1,
        "owner": "CNPC_VDog",
        "policy": "preserves ACT_FIDGET directly; every other request enters the "
                  "common Troika body",
        "chain": "PreTranslate_Troika",
        "chain_order": "AfterRules",
        "rules": (
            _nrule((), "ACT_FIDGET", "ACT_FIDGET", "RewriteAndReturn"),
        ),
    },
    {
        "name": "PreTranslate_Hengeyokai",
        "address": 0x10381B50,
        "slot": "PreTranslate",
        "inheritors": 1,
        "owner": "CNPC_VHengeyokai",
        "policy": "under the +0x14b8 form bit the carried-fish idle and carry replace "
                  "idle and gait; otherwise human translation",
        "chain": "PreTranslate_Human",
        "chain_order": "AfterRules",
        "rules": (
            _nrule(("FormBit",), "ACT_IDLE", "ACT_PICKUP_LIGHTIDLE", "RewriteAndReturn"),
            _nrule(("FormBit",), "ACT_WALK", "ACT_PICKUP_LIGHTCARRY", "RewriteAndReturn"),
            _nrule(("FormBit",), "ACT_RUN", "ACT_PICKUP_LIGHTCARRY", "RewriteAndReturn"),
        ),
    },
    {
        "name": "PreTranslate_Stalker",
        "address": 0x103B2E60,
        "slot": "PreTranslate",
        "inheritors": 1,
        "owner": "CNPC_VStalker",
        "policy": "walk/run/hunt-walk become ACT_COMBATMOVE; every other request is "
                  "identity",
        "rules": (
            _nrule((), "ACT_WALK", "ACT_COMBATMOVE", "RewriteAndReturn"),
            _nrule((), "ACT_RUN", "ACT_COMBATMOVE", "RewriteAndReturn"),
            _nrule((), "ACT_HUNT_WALK", "ACT_COMBATMOVE", "RewriteAndReturn"),
        ),
    },
    {
        "name": "PreTranslate_Tzimisce",
        "address": 0x103BDE40,
        "slot": "PreTranslate",
        "inheritors": 1,
        "owner": "CNPC_VTzimisce",
        "policy": "under its form bit, idle and gait select the body-carry variants "
                  "+0x6688 sides; otherwise common Troika translation",
        "chain": "PreTranslate_Troika",
        "chain_order": "AfterRules",
        "rules": (
            _nrule(("FormBit", "BodySideLeft"), "ACT_IDLE", "ACT_IDLE_BODY_L",
                   "RewriteAndReturn"),
            _nrule(("FormBit", "BodySideLeft"), "ACT_WALK", "ACT_WALK_BODY_L",
                   "RewriteAndReturn"),
            _nrule(("FormBit", "BodySideLeft"), "ACT_RUN", "ACT_WALK_BODY_L",
                   "RewriteAndReturn"),
            _nrule(("FormBit",), "ACT_IDLE", "ACT_IDLE_BODY", "RewriteAndReturn"),
            _nrule(("FormBit",), "ACT_WALK", "ACT_WALK_BODY", "RewriteAndReturn"),
            _nrule(("FormBit",), "ACT_RUN", "ACT_WALK_BODY", "RewriteAndReturn"),
        ),
    },
    {
        "name": "PreTranslate_TzimisceRunner",
        "address": 0x103C3E10,
        "slot": "PreTranslate",
        "inheritors": 1,
        "owner": "CNPC_VTzimisceRunner",
        "policy": "after common Troika translation, +0x6672 selects one of the four "
                  "TZ variants",
        "chain": "PreTranslate_Troika",
        "chain_order": "BeforeRules",
        "rules": (
            _nrule(("RunnerVariantIs",), None, "ACT_TZ_IDLE2", "RewriteAndReturn",
                   operand=0),
            _nrule(("RunnerVariantIs",), None, "ACT_TZ_FIDGET2", "RewriteAndReturn",
                   operand=1),
            _nrule(("RunnerVariantIs",), None, "ACT_TZ_WALK2", "RewriteAndReturn",
                   operand=2),
            _nrule(("RunnerVariantIs",), None, "ACT_TZ_RUN2", "RewriteAndReturn",
                   operand=3),
        ),
    },
    {
        "name": "PreTranslate_WolfMorph",
        "address": 0x103DCDC0,
        "slot": "PreTranslate",
        "inheritors": 1,
        "owner": "CNPC_VWolfMorph",
        "policy": "every request becomes ACT_WOLF_MORPH",
        "rules": (
            _nrule((), None, "ACT_WOLF_MORPH", "RewriteAndReturn"),
        ),
    },

    {
        "name": "ClassTranslate_Base",
        "address": 0x10271F70,
        "slot": "ClassTranslate",
        "inheritors": 13,
        "policy": "identity except the capability-gated cover and reload delegates",
        "rules": (
            _nrule(("ReloadFastCapable",), "ACT_RELOAD_FAST", None, "Delegate",
                   delegate="Reload"),
            _nrule(("CoverCapable",), "ACT_COVER", None, "Delegate", delegate="Cover"),
        ),
    },
    {
        "name": "ClassTranslate_Troika",
        "address": 0x10295710,
        "slot": "ClassTranslate",
        "inheritors": 19,
        "policy": "ACT_IDLE becomes ACT_LAUGH_IDLE under +0x14bc & 0x80000",
        "rules": (
            _nrule(("LaughIdleFlagged",), "ACT_IDLE", "ACT_LAUGH_IDLE"),
        ),
    },
    {
        "name": "ClassTranslate_Human",
        "address": 0x103858B0,
        "slot": "ClassTranslate",
        "inheritors": 42,
        "policy": "alert turn/90/180 activities return to their ordinary forms, then "
                  "the Troika rule",
        "chain": "ClassTranslate_Troika",
        "chain_order": "AfterRules",
        "rules": (
            _nrule((), "ACT_TURN_LEFT_ALERT", "ACT_TURN_LEFT"),
            _nrule((), "ACT_TURN_RIGHT_ALERT", "ACT_TURN_RIGHT"),
            _nrule((), "ACT_90_LEFT_ALERT", "ACT_90_LEFT"),
            _nrule((), "ACT_90_RIGHT_ALERT", "ACT_90_RIGHT"),
            _nrule((), "ACT_180_LEFT_ALERT", "ACT_180_LEFT"),
            _nrule((), "ACT_180_RIGHT_ALERT", "ACT_180_RIGHT"),
        ),
    },
    {
        "name": "ClassTranslate_Camera",
        "address": 0x103690C0,
        "slot": "ClassTranslate",
        "inheritors": 2,
        "policy": "identity",
        "rules": (),
    },
    {
        "name": "ClassTranslate_MingXiao",
        "address": 0x10394690,
        "slot": "ClassTranslate",
        "inheritors": 1,
        "owner": "CNPC_VMingXiao",
        "policy": "the same six alert-turn normalizations, otherwise the Troika rule",
        "chain": "ClassTranslate_Troika",
        "chain_order": "AfterRules",
        "rules": (
            _nrule((), "ACT_TURN_LEFT_ALERT", "ACT_TURN_LEFT"),
            _nrule((), "ACT_TURN_RIGHT_ALERT", "ACT_TURN_RIGHT"),
            _nrule((), "ACT_90_LEFT_ALERT", "ACT_90_LEFT"),
            _nrule((), "ACT_90_RIGHT_ALERT", "ACT_90_RIGHT"),
            _nrule((), "ACT_180_LEFT_ALERT", "ACT_180_LEFT"),
            _nrule((), "ACT_180_RIGHT_ALERT", "ACT_180_RIGHT"),
        ),
    },

    {
        "name": "Cover_Base",
        "address": 0x10274AA0,
        "slot": "Cover",
        "inheritors": 13,
        "policy": "medium or low cover for context 100/101 when the model has it, "
                  "otherwise available ACT_COVER, otherwise ACT_IDLE",
        "rules": (
            _nrule(("CoverContextIs",), None, "ACT_COVER_MED", "RewriteIfAvailable",
                   operand=100),
            _nrule(("CoverContextIs",), None, "ACT_COVER_LOW", "RewriteIfAvailable",
                   operand=101),
            _nrule((), None, "ACT_COVER", "RewriteIfAvailable"),
            _nrule((), None, "ACT_IDLE", "RewriteAndReturn"),
        ),
    },
    {
        "name": "Cover_Troika",
        "address": 0x10297560,
        "slot": "Cover",
        "inheritors": 64,
        "policy": "forced low cover, then the crunch idles for context 100, 101 or "
                  "0x27d8, before the base fallback",
        "chain": "Cover_Base",
        "chain_order": "AfterRules",
        "rules": (
            _nrule(("ForcedLowCover",), None, None, "ForceCoverContext", operand=101),
            _nrule(("CoverContextIs",), None, "ACT_MIDCRUNCH_IDLE", "RewriteIfAvailable",
                   operand=100),
            _nrule(("CoverContextIs",), None, "ACT_CRUNCH_IDLE", "RewriteIfAvailable",
                   operand=101),
            _nrule(("CoverContextIs",), None, "ACT_CORNER_COVER_IDLE",
                   "RewriteIfAvailable", operand=0x27D8),
        ),
    },
    {
        "name": "Reload_Base",
        "address": 0x10274820,
        "slot": "Reload",
        "inheritors": 13,
        "policy": "ACT_RELOAD_LOW for a compatible 100/101 cover context when the "
                  "model and environment tests pass, otherwise ACT_RELOAD",
        "rules": (
            _nrule(("CoverContextIs",), None, "ACT_RELOAD_LOW", "RewriteIfAvailable",
                   operand=100),
            _nrule(("CoverContextIs",), None, "ACT_RELOAD_LOW", "RewriteIfAvailable",
                   operand=101),
            _nrule((), None, "ACT_RELOAD", "RewriteAndReturn"),
        ),
    },
    {
        "name": "Reload_Troika",
        "address": 0x102954B0,
        "slot": "Reload",
        "inheritors": 64,
        "policy": "ACT_RELOAD_LOW, then the corresponding crunch idle through the "
                  "whole translator, then ACT_RELOAD_FAST",
        "rules": (
            _nrule((), None, "ACT_RELOAD_LOW", "RewriteIfAvailable"),
            _nrule(("CoverContextIs",), None, "ACT_MIDCRUNCH_IDLE",
                   "RewriteThroughTranslator", operand=100),
            _nrule(("CoverContextIs",), None, "ACT_CRUNCH_IDLE",
                   "RewriteThroughTranslator", operand=101),
            _nrule((), None, "ACT_RELOAD_FAST", "RewriteAndReturn"),
        ),
    },

    {
        "name": "EarlyTranslate_Grapple",
        "address": 0x10328030,
        "slot": "EarlyTranslate",
        "inheritors": 0,
        "policy": "CBaseCombatCharacter::NPC_EarlyTranslateActivity — the 29 "
                  "registered paired-action bases, resolved by the role arithmetic",
        "rules": (
            _nrule((), None, None, "Grapple"),
        ),
    },
)

_NPC_BODY_INDEX = {row["name"]: index for index, row in enumerate(NPC_BODIES)}
_NPC_SLOT_KEYS = {
    "PreTranslate": "pre_translate",
    "ClassTranslate": "class_translate",
    "Cover": "cover_activity",
    "Reload": "reload_activity",
}


def _npc_activity(by_name, name):
    """Resolve one activity to its registered ID, or to `runtime-registered`.

    `ACT_CROW_TAKEOFF` is registered at runtime rather than in the static table,
    which the task ledger already records as its condition; it is the one name
    with no compiled ID, and it is carried with `0` rather than dropped.
    """
    if name is None:
        return None, 0
    return name, by_name.get(name, 0)


def build_npc_model(image):
    """Join the recovered NPC ledger to the pinned RTTI, vtable and task decode."""
    registrations = list(npc_translation_survey.decode_activity_registry(image))
    activity_by_name = {}
    for row in registrations:
        activity_by_name.setdefault(row["name"], row["id"])
    classes = npc_task_override_survey.decode_task_virtuals(
        image, npc_translation_survey.find_npc_classes(image))
    classes = npc_translation_survey.decode_translation_slots(image, classes)

    # --- the bodies, checked against the vtable decode ---------------------
    observed = {}
    for slot, key in _NPC_SLOT_KEYS.items():
        counts = collections.Counter(row["translation_functions"][key] for row in classes)
        observed[slot] = counts
    for body in NPC_BODIES:
        slot = body["slot"]
        address = "0x%x" % body["address"]
        if slot == "EarlyTranslate":
            continue
        counts = observed[slot]
        if address not in counts:
            raise ValueError("%s is not a live %s body" % (address, slot))
        if counts[address] != body["inheritors"]:
            raise ValueError("%s inherits %s at %s, ledger says %d" %
                             (counts[address], address, slot, body["inheritors"]))
        owner = body.get("owner")
        if owner is not None:
            holders = [row["cpp_class"] for row in classes
                       if row["translation_functions"][_NPC_SLOT_KEYS[slot]] == address]
            if holders != [owner]:
                raise ValueError("%s is held by %s, ledger says %s" %
                                 (address, holders, owner))
    for slot, counts in observed.items():
        ledger = {"0x%x" % row["address"] for row in NPC_BODIES if row["slot"] == slot}
        if ledger != set(counts):
            raise ValueError("%s bodies disagree: binary %s, ledger %s" %
                             (slot, sorted(counts), sorted(ledger)))

    bodies = []
    for body in NPC_BODIES:
        rules = []
        for rule in body["rules"]:
            unknown = set(rule["predicates"]) - set(NPC_PREDICATES)
            if unknown:
                raise ValueError("unknown NPC predicates %s" % sorted(unknown))
            if len(rule["predicates"]) > 2:
                raise ValueError("an NPC rule conjoins more than two predicates")
            reads_operand = any(name in _OPERAND_PREDICATES for name in rule["predicates"])
            if rule["route"] != "ForceCoverContext" and reads_operand != bool(rule["operand"] or
                    ("RunnerVariantIs" in rule["predicates"])):
                raise ValueError("operand and predicate disagree on %r" % (rule,))
            source, source_id = _npc_activity(activity_by_name, rule["from"])
            target, target_id = _npc_activity(activity_by_name, rule["to"])
            if rule["from"] is not None and source_id == 0:
                raise ValueError("request %s is not registered" % rule["from"])
            if rule["to"] is not None and target_id == 0:
                raise ValueError("target %s is not registered" % rule["to"])
            rules.append({
                "predicates": rule["predicates"],
                "operand": rule["operand"],
                "from": source,
                "from_id": source_id,
                "family": rule["family"],
                "to": target,
                "to_id": target_id,
                "route": rule["route"],
                "delegate": rule["delegate"] or "Cover",
            })
        chain = body.get("chain")
        bodies.append({
            "name": body["name"],
            "address": "0x%x" % body["address"],
            "slot": body["slot"],
            "policy": body["policy"],
            "rules": rules,
            "chain_to": _NPC_BODY_INDEX[chain] if chain else -1,
            "chain": body.get("chain_order", "None") if chain else "None",
            "inheritors": body["inheritors"],
        })

    # --- the task handlers and their policies -----------------------------
    task_registrations = npc_task_override_survey.decode_task_registrations(image)
    policies = npc_task_override_survey.materialize_policies(
        task_registrations, registrations)
    groups = npc_task_override_survey.group_handlers(classes, policies)
    handler_index = {(row["phase"], row["handler"]): index
                     for index, row in enumerate(groups)}
    handlers = [{
        "address": row["handler"],
        "phase": row["phase"],
        "shared": row["shared_dispatcher"],
        "classes": row["class_count"],
        "policies": row["action_policy_count"],
    } for row in groups]

    task_rows = []
    for policy in policies:
        key = (policy["phase"], policy["handler"])
        if key not in handler_index:
            raise ValueError("policy handler %s has no vtable group" % (key,))
        if policy["route"] not in NPC_TASK_ROUTES:
            raise ValueError("unknown task route %s" % policy["route"])
        task_rows.append({
            "handler": handler_index[key],
            "tasks": list(policy["tasks"]),
            "route": NPC_TASK_ROUTES[policy["route"]],
            "activities": list(policy["activities"]),
            "condition": policy["condition"],
        })

    # --- the classes and their entity aliases -----------------------------
    class_rows = []
    for row in classes:
        functions = row["translation_functions"]
        class_rows.append({
            "cpp_class": row["cpp_class"],
            "direct_base": row["direct_base"],
            "entity_classnames": list(row["entity_classnames"]),
            "pre_translate": _npc_body_at("PreTranslate", functions["pre_translate"]),
            "class_translate": _npc_body_at("ClassTranslate", functions["class_translate"]),
            "cover": _npc_body_at("Cover", functions["cover_activity"]),
            "reload": _npc_body_at("Reload", functions["reload_activity"]),
            "start_task": handler_index[("start", row["task_functions"]["start"])],
            "run_task": handler_index[("run", row["task_functions"]["run"])],
        })

    by_class = {row["cpp_class"]: row for row in classes}
    class_position = {row["cpp_class"]: index for index, row in enumerate(class_rows)}
    alias_map, _ = npc_translation_survey._alias_index(classes)
    aliases = []
    for alias in sorted(alias_map, key=str.casefold):
        winners = npc_translation_survey._most_derived(alias_map[alias], by_class)
        if len(winners) != 1:
            raise ValueError("entity classname %s resolves to %s" % (alias, winners))
        aliases.append({"classname": alias, "class_index": class_position[winners[0]]})

    # --- the grapple families ---------------------------------------------
    by_id = {row["id"]: row["name"] for row in registrations}
    families = []
    variants = 0
    for base in sorted((row for row in registrations if row["kind"] == "special"),
                       key=lambda row: row["id"]):
        order = None
        for name, suffixes in GRAPPLE_ROLE_SUFFIXES.items():
            if all(by_id.get(base["id"] + offset) == "%s_%s" % (base["name"], suffix)
                   for offset, suffix in enumerate(suffixes, start=1)):
                order = name
                break
        if order is None:
            raise ValueError("%s registers no recognised role order" % base["name"])
        variants += len(GRAPPLE_ROLE_SUFFIXES[order])
        families.append({
            "base": base["name"],
            "base_id": base["id"],
            "order": order,
        })

    model = {
        "bodies": bodies,
        "classes": class_rows,
        "aliases": aliases,
        "handlers": handlers,
        "policies": task_rows,
        "grapple": families,
        "grapple_variants": variants,
        "task_registrations": task_registrations,
    }
    model["census"] = npc_census(model)
    return model


def _npc_body_at(slot, address):
    for index, body in enumerate(NPC_BODIES):
        if body["slot"] == slot and "0x%x" % body["address"] == address:
            return index
    raise ValueError("no ledger body for %s %s" % (slot, address))


def npc_census(model):
    slots = collections.Counter(row["slot"] for row in model["bodies"])
    phases = collections.Counter(row["phase"] for row in model["handlers"])
    custom = [row for row in model["handlers"] if not row["shared"]]
    return {
        "subclasses": len(model["classes"]),
        "pre_translate_bodies": slots["PreTranslate"],
        "class_translate_bodies": slots["ClassTranslate"],
        "cover_bodies": slots["Cover"],
        "reload_bodies": slots["Reload"],
        "tail_bodies": slots["EarlyTranslate"],
        "translation_rules": sum(len(row["rules"]) for row in model["bodies"]),
        "unrecovered_family_rules": sum(
            1 for row in model["bodies"] for rule in row["rules"] if rule["family"]),
        "entity_aliases": len(model["aliases"]),
        "start_task_handlers": phases["start"],
        "run_task_handlers": phases["run"],
        "custom_task_handlers": len(custom),
        "animation_bearing_handlers": sum(1 for row in custom if row["policies"]),
        "task_policies": len(model["policies"]),
        "task_routes": sum(len(row["tasks"]) for row in model["policies"]),
        "exact_label_routes": 0,
        "layer_routes": 0,
        "grapple_families": len(model["grapple"]),
        "grapple_variants": model["grapple_variants"],
        "shared_task_registrations": sum(
            1 for row in model["task_registrations"] if row["owner"] == "shared"),
        "class_local_task_registrations": sum(
            1 for row in model["task_registrations"] if row["owner"] != "shared"),
        "predicates": len(NPC_PREDICATES),
    }


def _npc_rule_line(rule, indent):
    """One `FNpcRule` initialiser, wrapped field-wise under the 100-column rule.

    `Delegate` is the last member and is meaningful only on a delegating row, so
    every other row simply stops before it and takes the default.
    """
    padded = list(rule["predicates"]) + ["Always"] * (2 - len(rule["predicates"]))
    fields = [
        "{ %s }" % ", ".join("NpcP::%s" % name for name in padded),
        str(rule["operand"]),
        _literal(rule["from"]) if rule["from"] else "nullptr",
        str(rule["from_id"]),
        _literal(rule["family"]) if rule["family"] else "nullptr",
        _literal(rule["to"]) if rule["to"] else "nullptr",
        str(rule["to_id"]),
        "NpcR::%s" % rule["route"],
    ]
    if rule["route"] == "Delegate":
        fields.append("NpcS::%s" % rule["delegate"])

    lines = []
    current = indent + "{"
    for index, field in enumerate(fields):
        piece = field + ("," if index + 1 < len(fields) else " },")
        candidate = "%s %s" % (current, piece)
        if lines or index:
            if len(candidate.expandtabs(4)) > 100:
                lines.append(current)
                current = indent + "\t" + piece
                continue
        current = candidate
    lines.append(current)
    return lines


def render_npc_cpp(model):
    out = []
    add = out.append
    counts = model["census"]

    add("// Generated by `uv run elysium research gen_action_tables`. Do not hand-edit.")
    add("//")
    add("// VtMB's NPC activity-translation surface, recovered from the pinned retail `vampire.dll`")
    add("// and committed as project source beside the weapon tables and the player action rules.")
    add("// Nothing in the build or the export reads the binary.")
    add("//")
    add("// %d `CAI_BaseNPC` descendants collapse to %d pre-translation bodies, %d class-translation"
        % (counts["subclasses"], counts["pre_translate_bodies"],
           counts["class_translate_bodies"]))
    add("// bodies and %d implementations each of the `+0x8e4` cover and `+0x8e8` reload delegates,"
        % counts["cover_bodies"])
    add("// plus the one non-virtual `NPC_EarlyTranslateActivity` tail. %d task policies over %d task"
        % (counts["task_policies"], counts["task_routes"]))
    add("// routes carry the class-local animation surface, with %d exact sequence-label routes and"
        % counts["exact_label_routes"])
    add("// %d overlay-layer routes. The %d paired-action grapple variants are not stored: they are"
        % (counts["layer_routes"], counts["grapple_variants"]))
    add("// %d registered bases plus the recovered `+1`…`+8` role arithmetic."
        % counts["grapple_families"])
    add("//")
    add("// The recovered behaviour is `docs/vtmb/animation_and_movers.md` A.3. Every activity below")
    add("// carries its registered ID, taken from the binary's own registration table.")
    add("")
    add("#include \"Visual/ElysiumActionTables.h\"")
    add("")
    add("namespace ElysiumActionTables")
    add("{")
    add("namespace")
    add("{")
    add("\t// A row's predicates are AND-ed and `Always`-padded; the aliases are what let one fit a")
    add("\t// line. Unit-prefixed because the module builds adaptive-unity: this anonymous")
    add("\t// namespace is regularly merged with the player and weapon tables' own.")
    add("\tusing NpcP = ENpcPredicate;")
    add("\tusing NpcR = ENpcRoute;")
    add("\tusing NpcS = ENpcSlot;")
    add("\tstatic_assert(static_cast<int32>(NpcP::Count) == %d," % len(NPC_PREDICATES))
    add("\t\t\"the predicate vocabulary changed; regenerate the NPC activity tables\");")

    for body in model["bodies"]:
        if not body["rules"]:
            continue
        add("")
        for line in _comment("%s (%s) — %s." % (body["name"], body["address"],
                                                body["policy"]), "\t"):
            add(line)
        add("\tconstexpr FNpcRule G%sRules[] =" % body["name"].replace("_", ""))
        add("\t{")
        for rule in body["rules"]:
            out.extend(_npc_rule_line(rule, "\t\t"))
        add("\t};")

    add("")
    add("\t// The %d recovered bodies, pre-translation first. `InheritorCount` is how many of the %d"
        % (len(model["bodies"]), counts["subclasses"]))
    add("\t// subclasses reach the body at its slot; the tail is reached by chain, not by vtable.")
    add("\tconstexpr FNpcTranslationBody GBodies[] =")
    add("\t{")
    for body in model["bodies"]:
        symbol = ("G%sRules" % body["name"].replace("_", "")) if body["rules"] else "nullptr"
        add("\t\t{ %s, %s, ENpcSlot::%s," % (_literal(body["name"]), _literal(body["address"]),
                                             body["slot"]))
        for line in _wrap_text(body["policy"], "\t\t\t", width=98):
            add(line)
        chain_to = ("%d" % body["chain_to"]) if body["chain_to"] >= 0 else "INDEX_NONE"
        add("\t\t\t%s, %d, %s, ENpcChain::%s, %d }," %
            (symbol, len(body["rules"]), chain_to, body["chain"], body["inheritors"]))
    add("\t};")
    add("\tstatic_assert(UE_ARRAY_COUNT(GBodies) == %d, \"the NPC body ledger changed\");"
        % len(model["bodies"]))

    # --- the classes -------------------------------------------------------
    for row in model["classes"]:
        if not row["entity_classnames"]:
            continue
        names = [_literal(name) for name in row["entity_classnames"]]
        line = "\tconstexpr const TCHAR* %s_Classnames[] = { %s };" % (
            row["cpp_class"], ", ".join(names))
        add("")
        if len(line.expandtabs(4)) > 100:
            add("\tconstexpr const TCHAR* %s_Classnames[] =" % row["cpp_class"])
            add("\t{")
            for wrapped in _wrap(names, "\t\t"):
                add(wrapped)
            add("\t};")
        else:
            add(line)

    add("")
    add("\t// The %d `CAI_BaseNPC` descendants, sorted by class name. The four body columns and the"
        % counts["subclasses"])
    add("\t// two task columns are indices, because a body is shared and a name is not.")
    add("\tconstexpr FNpcClass GClasses[] =")
    add("\t{")
    for row in model["classes"]:
        classnames = ("%s_Classnames" % row["cpp_class"]) if row["entity_classnames"] \
            else "nullptr"
        add("\t\t{ %s, %s," % (_literal(row["cpp_class"]), _literal(row["direct_base"])))
        add("\t\t\t%s, %d," % (classnames, len(row["entity_classnames"])))
        add("\t\t\t%d, %d, %d, %d, %d, %d }," %
            (row["pre_translate"], row["class_translate"], row["cover"], row["reload"],
             row["start_task"], row["run_task"]))
    add("\t};")
    add("\tstatic_assert(UE_ARRAY_COUNT(GClasses) == %d, \"the NPC subclass count changed\");"
        % counts["subclasses"])

    add("")
    add("\t// Every entity classname a map or an `npc_maker` can author, resolved to the most-derived")
    add("\t// class that claims it, sorted case-folded so the ledger reads as one list.")
    add("\tconstexpr FNpcEntityAlias GAliases[] =")
    add("\t{")
    for row in model["aliases"]:
        add("\t\t{ %s, %d }," % (_literal(row["classname"]), row["class_index"]))
    add("\t};")
    add("\tstatic_assert(UE_ARRAY_COUNT(GAliases) == %d, \"the entity alias set changed\");"
        % counts["entity_aliases"])

    add("")
    add("\t// The %d `StartTask` and %d `RunTask` bodies, start phase first, each sorted by address."
        % (counts["start_task_handlers"], counts["run_task_handlers"]))
    add("\t// %d are custom; %d of those carry a direct animation policy."
        % (counts["custom_task_handlers"], counts["animation_bearing_handlers"]))
    add("\tconstexpr FNpcTaskHandler GTaskHandlers[] =")
    add("\t{")
    for row in model["handlers"]:
        add("\t\t{ %s, ENpcTaskPhase::%s, %s, %d, %d }," %
            (_literal(row["address"]), row["phase"].capitalize(),
             "true" if row["shared"] else "false", row["classes"], row["policies"]))
    add("\t};")
    add("\tstatic_assert(UE_ARRAY_COUNT(GTaskHandlers) == %d, \"the task handler set changed\");"
        % len(model["handlers"]))

    # --- the task policies -------------------------------------------------
    for index, row in enumerate(model["policies"]):
        add("")
        names = [_literal(name) for name in row["tasks"]]
        line = "\tconstexpr const TCHAR* GPolicy%dTasks[] = { %s };" % (index, ", ".join(names))
        if len(line.expandtabs(4)) > 100:
            add("\tconstexpr const TCHAR* GPolicy%dTasks[] =" % index)
            add("\t{")
            for wrapped in _wrap(names, "\t\t"):
                add(wrapped)
            add("\t};")
        else:
            add(line)
        if row["activities"]:
            values = [_literal(name) for name in row["activities"]]
            line = "\tconstexpr const TCHAR* GPolicy%dActivities[] = { %s };" % (
                index, ", ".join(values))
            if len(line.expandtabs(4)) > 100:
                add("\tconstexpr const TCHAR* GPolicy%dActivities[] =" % index)
                add("\t{")
                for wrapped in _wrap(values, "\t\t"):
                    add(wrapped)
                add("\t};")
            else:
                add(line)

    add("")
    add("\t// %d policy rows over %d task routes. A row names more than one task where the recovered"
        % (counts["task_policies"], counts["task_routes"]))
    add("\t// body routes them identically.")
    add("\tconstexpr FNpcTaskPolicy GTaskPolicies[] =")
    add("\t{")
    for index, row in enumerate(model["policies"]):
        activities = ("GPolicy%dActivities" % index) if row["activities"] else "nullptr"
        add("\t\t{ %d, GPolicy%dTasks, %d, ENpcTaskRoute::%s," %
            (row["handler"], index, len(row["tasks"]), row["route"]))
        head = "\t\t\t%s, %d," % (activities, len(row["activities"]))
        # An unconditional route carries an empty string rather than no field, so the wrap has to
        # produce one literal for it too.
        condition = (_wrap_text(row["condition"], "\t\t\t", width=96) if row["condition"].strip()
                     else ["\t\t\tTEXT(\"\"),"])
        if len(condition) == 1 and len((head + " " + condition[0].strip()).expandtabs(4)) <= 98:
            add("%s %s }," % (head, condition[0].strip().rstrip(",")))
        else:
            add(head)
            for offset, line in enumerate(condition):
                add(line.rstrip(",") + (" }," if offset + 1 == len(condition) else ""))
    add("\t};")
    add("\tstatic_assert(UE_ARRAY_COUNT(GTaskPolicies) == %d, \"the task policy ledger changed\");"
        % counts["task_policies"])

    add("")
    add("\t// The eight role suffixes, indexed by `Offset - 1`.")
    for name in ("Canonical", "AttackerVictimSwapped"):
        add("\tconstexpr const TCHAR* GGrappleRoles%s[] =" % name)
        add("\t{")
        for line in _wrap([_literal(suffix) for suffix in GRAPPLE_ROLE_SUFFIXES[name]],
                          "\t\t"):
            add(line)
        add("\t};")

    add("")
    add("\t// The %d specially registered paired-action bases. Their %d variants are generated, not"
        % (counts["grapple_families"], counts["grapple_variants"]))
    add("\t// stored: the base plus one role suffix, at ID `BaseId + Offset`.")
    add("\tconstexpr FNpcGrappleFamily GGrappleFamilies[] =")
    add("\t{")
    for row in model["grapple"]:
        add("\t\t{ %s, %d, ENpcGrappleRoleOrder::%s }," %
            (_literal(row["base"]), row["base_id"], row["order"]))
    add("\t};")
    add("\tstatic_assert(UE_ARRAY_COUNT(GGrappleFamilies) == %d, \"the grapple base set changed\");"
        % counts["grapple_families"])
    add("}")

    for symbol, kind, table in (
            ("NpcTranslationBodies", "FNpcTranslationBody", "GBodies"),
            ("NpcClasses", "FNpcClass", "GClasses"),
            ("NpcEntityAliases", "FNpcEntityAlias", "GAliases"),
            ("NpcTaskHandlers", "FNpcTaskHandler", "GTaskHandlers"),
            ("NpcTaskPolicies", "FNpcTaskPolicy", "GTaskPolicies"),
            ("NpcGrappleFamilies", "FNpcGrappleFamily", "GGrappleFamilies")):
        add("")
        add("TArrayView<const %s> %s()" % (kind, symbol))
        add("{")
        add("\treturn MakeArrayView(%s);" % table)
        add("}")

    add("")
    add("TArrayView<const TCHAR* const> NpcGrappleRoleSuffixes(ENpcGrappleRoleOrder Order)")
    add("{")
    add("\tswitch (Order)")
    add("\t{")
    add("\tcase ENpcGrappleRoleOrder::AttackerVictimSwapped:")
    add("\t\treturn MakeArrayView(GGrappleRolesAttackerVictimSwapped);")
    add("\tcase ENpcGrappleRoleOrder::Canonical:")
    add("\t\tbreak;")
    add("\t}")
    add("\treturn MakeArrayView(GGrappleRolesCanonical);")
    add("}")

    add("")
    add("const FNpcTableCensus& NpcCensus()")
    add("{")
    add("\tstatic const FNpcTableCensus GCensus =")
    add("\t{")
    for comment, key in (
            ("Subclasses                  ", "subclasses"),
            ("PreTranslateBodies          ", "pre_translate_bodies"),
            ("ClassTranslateBodies        ", "class_translate_bodies"),
            ("CoverBodies                 ", "cover_bodies"),
            ("ReloadBodies                ", "reload_bodies"),
            ("TailBodies                  ", "tail_bodies"),
            ("TranslationRules            ", "translation_rules"),
            ("UnrecoveredFamilyRules      ", "unrecovered_family_rules"),
            ("EntityAliases               ", "entity_aliases"),
            ("StartTaskHandlers           ", "start_task_handlers"),
            ("RunTaskHandlers             ", "run_task_handlers"),
            ("CustomTaskHandlers          ", "custom_task_handlers"),
            ("AnimationBearingHandlers    ", "animation_bearing_handlers"),
            ("TaskPolicies                ", "task_policies"),
            ("TaskRoutes                  ", "task_routes"),
            ("ExactLabelRoutes            ", "exact_label_routes"),
            ("LayerRoutes                 ", "layer_routes"),
            ("GrappleFamilies             ", "grapple_families"),
            ("GrappleVariants             ", "grapple_variants"),
            ("SharedTaskRegistrations     ", "shared_task_registrations"),
            ("ClassLocalTaskRegistrations ", "class_local_task_registrations"),
            ("Predicates                  ", "predicates")):
        add("\t\t/* %s*/ %d," % (comment, counts[key]))
    add("\t};")
    add("\treturn GCensus;")
    add("}")
    add("}")
    add("")
    return "\n".join(out)


def print_npc_report(model, output):
    counts = model["census"]
    print("=" * 78)
    print("VtMB NPC activity translation — generated model")
    print("=" * 78)
    print("subclasses          %5d" % counts["subclasses"])
    print("bodies              pre=%d class=%d cover=%d reload=%d tail=%d" %
          (counts["pre_translate_bodies"], counts["class_translate_bodies"],
           counts["cover_bodies"], counts["reload_bodies"], counts["tail_bodies"]))
    print("translation rules   %5d  (%d over an unenumerated request family)" %
          (counts["translation_rules"], counts["unrecovered_family_rules"]))
    print("entity aliases      %5d" % counts["entity_aliases"])
    print("task handlers       start=%d run=%d | custom=%d animation-bearing=%d" %
          (counts["start_task_handlers"], counts["run_task_handlers"],
           counts["custom_task_handlers"], counts["animation_bearing_handlers"]))
    print("task policies       %5d rows / %d routes  (%d exact-label, %d layer)" %
          (counts["task_policies"], counts["task_routes"],
           counts["exact_label_routes"], counts["layer_routes"]))
    print("task registrations  shared=%d class-local=%d" %
          (counts["shared_task_registrations"], counts["class_local_task_registrations"]))
    print("grapple             %d bases -> %d generated variants" %
          (counts["grapple_families"], counts["grapple_variants"]))
    swapped = [row["base"] for row in model["grapple"]
               if row["order"] != "Canonical"]
    print("  attacker/victim registered the other way round: %s" %
          (", ".join(swapped) if swapped else "none"))
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


def generate_npc(args):
    binary = args.binary or os.fspath(vtmb_root() / "Vampire" / "dlls" / "vampire.dll")
    with open(binary, "rb") as handle:
        data = handle.read()
    digest = hashlib.sha256(data).hexdigest()
    if digest != weapon_activity_survey.PINNED_SHA256:
        raise ValueError("unsupported vampire.dll SHA-256 %s" % digest)
    model = build_npc_model(npc_translation_survey.PEImage(data))

    output = args.out or os.fspath(repo_root().joinpath(*DEFAULT_NPC_OUTPUT))
    text = render_npc_cpp(model)
    print_npc_report(model, output)
    return _emit(output, text, args.check)


ARTIFACTS = (
    ("weapons", generate_weapons),
    ("player", generate_player),
    ("npc", generate_npc),
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", help="override the pinned retail vampire.dll path")
    parser.add_argument("--root", help="override the exported corpus root")
    parser.add_argument("--only", choices=tuple(name for name, _ in ARTIFACTS),
                        help="generate one artifact instead of all three")
    parser.add_argument("--out", help="override the generated .cpp path; needs --only")
    parser.add_argument("--check", action="store_true",
                        help="verify the committed file matches; write nothing")
    args = parser.parse_args()

    if args.out and not args.only:
        parser.error("--out addresses one artifact; pass --only %s" %
                     "|".join(name for name, _ in ARTIFACTS))

    status = 0
    for index, (name, generate) in enumerate(ARTIFACTS):
        if args.only not in (None, name):
            continue
        if args.only is None and index:
            print()
        status |= generate(args)
    return status


if __name__ == "__main__":
    sys.exit(main())
