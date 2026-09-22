"""The parser's operand vocabulary: thirteen tables, the namespaces, the seeded squad slots.

These are what `0x1030d850` resolves an operand through, and they belong to the PARSER rather than
to any one class, which is why the root unit carries them and no space unit does.

Two shapes ship. `MiscFlag:` is a pointer table in the image (`0x10619ec8`, 22 cells) and is READ;
every other resolver is a `strcmpi` chain whose names and values are inline immediates spread
through its body, and those are transcribed from
`docs/vtmb/npc-ai/schedule-kernel.md` § "The schedule-text parser `0x1030d850`, walked".

A transcription is not trusted on its word. `verify` requires every transcribed name to be present
in the image as a NUL-terminated string, and a name that is not refuses the unit -- so a typo, a
stale row or a table that changed between images fails the export rather than being published.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any

from elysium_pipeline.formats.ai_schedule_glb.image import PEImage


class VocabularyError(ValueError):
    """A transcribed vocabulary row does not hold against the image."""


#: `MiscFlag:` -- the one table stored as pointers, and therefore read rather than transcribed.
#: The parser's resolver `0x1030d390` answers the raw INDEX 0-21 and reads 0 (`Unconscious`)
#: for an unknown name, silently. That is distinct from `0x1033cb00`, which answers a MASK over
#: the same 22 names and is NOT the parser's.
MISC_FLAG_TABLE = 0x10619EC8
MISC_FLAG_COUNT = 22

#: `State:` -- `0x1030c600`. 12 has no name.
STATE = {
    "NONE": 0, "IDLE": 1, "COMBAT": 2, "ALERT": 3, "SCRIPT": 4, "PLAYDEAD": 5,
    "PRONE": 6, "DEAD": 7, "FLEEING": 8, "RETREATING": 9, "COWERING": 10,
    "HUNTING": 11, "OBLIVIOUS": 13, "CRIMINAL_SUSPICION": 14,
}

#: `Memory:` -- `0x1030c800`. Masks, not ordinals.
MEMORY = {
    "PROVOKED": 0x1, "INCOVER": 0x2, "SUSPICIOUS": 0x4, "PATH_FAILED": 0x20,
    "FLINCHED": 0x40, "TOURGUIDE": 0x100, "LOCKED_HINT": 0x400, "TURNING": 0x2000,
    "TURNHACK": 0x4000, "HAD_ENEMY": 0x8000, "HAD_PLAYER": 0x10000, "HAD_LOS": 0x20000,
    "INVESTIGATING": 0x08000000, "CUSTOM4": 0x10000000, "CUSTOM3": 0x20000000,
    "CUSTOM2": 0x40000000, "CUSTOM1": 0x80000000,
}

#: `Path:` -- `0x1030ca70`.
PATH = {"TRAVEL": 0, "LOS": 1, "COVER": 2}

#: `Goal:` -- `0x1030cb00`.
GOAL = {"ENEMY": 0, "TARGET": 1, "ENEMY_LKP": 2, "TARGET_LKP": 3, "SAVED_POSITION": 4}

#: `HintFlags:` -- `0x102d3f50`. A SUBSTRING search of the lowered token, not an equality test;
#: a token matching both `nearest` and `random` warns and reads as nearest.
HINT_FLAGS = {"none": 0, "visible": 1, "nearest": 2, "random": 4}

#: `EXPRESSION:` -- `0x1030f5f0`.
EXPRESSION = {"FLINCH": 0, "KNOCKBACK": 1}

#: `STO:` -- `0x1030d480`, the shoot-target override.
STO = {"DEFAULT": 0, "SHOOT_AT_HINT": 1}

#: `DIST:` -- `0x1030d4f0`. Negative sentinels that slot 418 `ResolveTaskDistance` turns into
#: distances at run time; the name table and the run-time enum are one vocabulary.
DIST = {
    "ACCUM": -1000000,
    "TZIMISCE_CLAW": -1000001,
    "DIALOG": -1000002,
    "COMBATMOVE": -1000003,
    "MINGXIAO_IDEAL_RANGE": -1000004,
    "FOLLOWER_DISTANCE_BACKAWAY": -1000005,
    "FOLLOWER_DISTANCE_WALKTO": -1000006,
    "FOLLOWER_DISTANCE_RUNTO": -1000007,
    "FOLLOWER_DISTANCE_OVERLAP": -1000008,
}

#: `MXTPHASE:` -- `0x1030d650`.
MXT_PHASE = {"TENTACLE": 0, "TENTACLE_TO_GRUB": 1, "GRUB": 2, "GRUB_TO_PROXY": 3}

#: `TOMODE:` -- `0x1030d710`, the thrown-object mode.
TO_MODE = {"NONE": 0, "PATHING": 1, "GRABBING": 2, "CARRYING": 3, "THROWING": 4}

#: `Flags` -- `0x1030d7e0`. `DELAY_INTERRUPTS` is the ONLY schedule flag the engine has.
SCHEDULE_FLAGS = {"NONE": 0, "DELAY_INTERRUPTS": 1}

#: `NPCFlag:` word one, `m_bfAINPCFlags` (`+0x14b8`), bits 0..30.
NPC_FLAGS_WORD_ONE = [
    "D_IS_BUSY", "DO_STARTLED", "AT_CROSSWALK", "PRESERVE_PATH", "FINDING_BODY",
    "CARRYING_BODY", "NAV_IGNORE_NPC", "IN_FLEE_SCHED", "INITIAL_FLEE", "COWER_PATH",
    "COWERING", "DODGING", "MADE_HUNT_PATH", "AT_COVER_HINT", "ANIM_MOVEMENT",
    "DONE_EXTRAPOLATING", "FORCE_RELAXED_ANIMS", "SLEEPING", "BOTCHED_ATTACK", "NO_DIALOG",
    "SKIPPED_SOUND", "LOOKED_AT_UNKNOWN", "IGNORE_UNKNOWN", "ATTACK_UNKNOWN",
    "MADE_INITIAL_RESPONSE", "FINISHED_IGNORE_UNKNOWN", "DONT_INVESTIGATE",
    "PLAYING_FACE_ANIM", "FORCED_OCCLUDE", "INTERESTING_INTO", "ONE_HIT_KILL",
]

#: `NPCFlag:` word two, `m_bfAINPCFlags2` (`+0x14bc`), bits 0..30. The resolver returns
#: `0x80000000 | bit` for these, and bit 31 is the ROUTING MARKER rather than a flag.
NPC_FLAGS_WORD_TWO = [
    "SLEEP_BOUNDING_BOX", "FINISH_SPECIAL_NAV", "SCHEDULE_CHANGED", "INTERESTING_LOST",
    "TASKS_FACE_ENEMY", "TASKS_FACE_TARGET", "IGNORE_SQUAD_SEE_ENEMY", "NO_UNKNOWN_ATTACK",
    "COVER_VS_MELEE_MODE", "IGNORE_DOOR_FAILURE", "MOVE_FACE_ENEMY", "DISALLOW_TGT_DISCIPLINE",
    "MADE_OBLIVIOUS", "SQUAD_NEW_ENEMY", "DONT_FALL_TO_GROUND", "DISABLE_BURST_FIRE",
    "D_CALM", "D_INSANE", "D_POSSESSED", "D_MILDLY_CRAZY", "D_FOLLOW", "D_NIGHTMARE",
    "D_AUTO_FEEDABLE", "D_DISCONNECT_SQUAD", "D_WPN_HIDDEN", "CHOOSE_NEW_SCHEDULE",
    "NO_UNKNOWN_VISION", "NOT_FEEDABLE", "NO_DIALOG_PERSISTENT", "DISAPPEAR",
    "ACTIVITY_COPY_PROP_CLEAN",
]

#: The word-two routing marker the resolver ORs in.
NPC_FLAG_WORD_TWO_MARKER = 0x80000000

#: The three global namespaces and the squad-slot one. A namespace's id counter is SEEDED at
#: 1,000,000,000 -- `CAI_LocalIdSpace::Init` READS a global base off the namespace's next-free
#: counter rather than taking one, so a registered global id is the counter's value and not an
#: ordinal with an offset applied afterwards.
NAMESPACES = {
    "schedule": 0x109203CC,
    "task": 0x109203D4,
    "condition": 0x109203DC,
    "squadslot": 0x10936C74,
}

#: The seed every namespace's counter starts at.
GLOBAL_ID_BASE = 1_000_000_000

#: The two squad-slot names, seeded globally by `0x10316e80`. No class registers a local one.
SQUAD_SLOTS = {"SQUAD_SLOT_ATTACK1": GLOBAL_ID_BASE, "SQUAD_SLOT_ATTACK2": GLOBAL_ID_BASE + 1}

#: Each table's resolver, and the quirks a consumer must reproduce.
RESOLVERS: dict[str, dict[str, Any]] = {
    "activity": {"resolver": "0x1025d760", "registry": "0x1090fbe0", "resolution": "runtime"},
    "task": {"resolver": "0x10316fd0", "then": "class-local translation"},
    "schedule": {"resolver": "0x102cadb0", "then": "class-local translation"},
    "state": {"resolver": "0x1030c600"},
    "memory": {"resolver": "0x1030c800", "stores": "mask"},
    "path": {"resolver": "0x1030ca70"},
    "goal": {"resolver": "0x1030cb00"},
    "hintflags": {"resolver": "0x102d3f50", "match": "substring",
                  "note": "nearest + random warns and reads as nearest"},
    "npcflag": {"resolver": "0x1030cbd0", "stores": "raw",
                "word2Marker": f"{NPC_FLAG_WORD_TWO_MARKER:#010x}"},
    "miscflag": {"resolver": "0x1030d390", "stores": "raw", "value": "index",
                 "unknownReads": 0, "table": f"{MISC_FLAG_TABLE:#010x}"},
    "model": {"resolver": "0x1030d3d0", "stores": "raw", "resolution": "runtime",
              "symbols": "0x10936b74", "note": "the parser also precaches the model"},
    "sound": {"resolver": "0x1030d400", "resolution": "runtime", "table": None,
              "note": "DAT_1073dc3c / DAT_1073dc40 are filled at load from the VSound table"},
    "expression": {"resolver": "0x1030f5f0"},
    "sto": {"resolver": "0x1030d480"},
    "dist": {"resolver": "0x1030d4f0", "resolution": "runtime-sentinel",
             "note": "slot 418 ResolveTaskDistance turns these into distances"},
    "mxtphase": {"resolver": "0x1030d650"},
    "tomode": {"resolver": "0x1030d710"},
    "scheduleFlags": {"resolver": "0x1030d7e0"},
}


@dataclass(frozen=True, slots=True)
class Vocabulary:
    """The tables as one unit publishes them."""

    tables: dict[str, Any]
    misc_flags: list[str]

    def to_json(self) -> dict[str, Any]:
        return dict(self.tables)


def read_misc_flags(image: PEImage) -> list[str]:
    """The 22 `MiscFlag:` names, read from the image's own pointer table."""

    names: list[str] = []
    for index in range(MISC_FLAG_COUNT):
        pointer = image.read_u32_va(MISC_FLAG_TABLE + 4 * index)
        if not pointer:
            raise VocabularyError(
                f"the MiscFlag table at {MISC_FLAG_TABLE:#010x} has no cell {index}"
            )
        name = image.read_cstring_va(pointer)
        if not name:
            raise VocabularyError(f"MiscFlag cell {index} does not name a string")
        names.append(name)
    return names


def _present(image: PEImage, names: list[str], label: str) -> None:
    """Every transcribed name must exist in the image as a NUL-terminated string.

    This is what keeps a transcription from being a claim: a row that is not in the image is a row
    the resolver cannot answer, and publishing it would put a name in the corpus that retail has
    never heard of.
    """

    pool = image.section_bytes(".data")[1]
    other = image.section_bytes(".rdata")[1]
    missing = [
        name
        for name in names
        if (name.encode("ascii", "ignore") + bytes([0])) not in pool
        and (name.encode("ascii", "ignore") + bytes([0])) not in other
    ]
    if missing:
        raise VocabularyError(
            f"{label}: {len(missing)} transcribed name(s) are not in the image: "
            + ", ".join(sorted(missing)[:8])
        )


def build(image: PEImage) -> Vocabulary:
    """The vocabulary, read where it is readable and verified where it is transcribed."""

    misc = read_misc_flags(image)

    for label, table in (
        ("State:", list(STATE)),
        ("Memory:", list(MEMORY)),
        ("Path:", list(PATH)),
        ("Goal:", list(GOAL)),
        ("EXPRESSION:", list(EXPRESSION)),
        ("STO:", list(STO)),
        ("DIST:", list(DIST)),
        ("MXTPHASE:", list(MXT_PHASE)),
        ("TOMODE:", list(TO_MODE)),
        ("Flags", list(SCHEDULE_FLAGS)),
        ("NPCFlag: word one", NPC_FLAGS_WORD_ONE),
        ("NPCFlag: word two", NPC_FLAGS_WORD_TWO),
        ("squad slots", list(SQUAD_SLOTS)),
    ):
        _present(image, table, label)

    tables: dict[str, Any] = {
        "state": {"values": dict(STATE), **RESOLVERS["state"]},
        "memory": {"values": dict(MEMORY), **RESOLVERS["memory"]},
        "path": {"values": dict(PATH), **RESOLVERS["path"]},
        "goal": {"values": dict(GOAL), **RESOLVERS["goal"]},
        "hintFlags": {"values": dict(HINT_FLAGS), **RESOLVERS["hintflags"]},
        "npcFlag": {
            "wordOne": list(NPC_FLAGS_WORD_ONE),
            "wordTwo": list(NPC_FLAGS_WORD_TWO),
            **RESOLVERS["npcflag"],
        },
        "miscFlag": {"values": {name: index for index, name in enumerate(misc)},
                     **RESOLVERS["miscflag"]},
        "expression": {"values": dict(EXPRESSION), **RESOLVERS["expression"]},
        "sto": {"values": dict(STO), **RESOLVERS["sto"]},
        "dist": {"values": dict(DIST), **RESOLVERS["dist"]},
        "mxtPhase": {"values": dict(MXT_PHASE), **RESOLVERS["mxtphase"]},
        "toMode": {"values": dict(TO_MODE), **RESOLVERS["tomode"]},
        "scheduleFlags": {"values": dict(SCHEDULE_FLAGS), **RESOLVERS["scheduleFlags"]},
        "activity": dict(RESOLVERS["activity"]),
        "sound": dict(RESOLVERS["sound"]),
        "model": dict(RESOLVERS["model"]),
        "task": dict(RESOLVERS["task"]),
        "schedule": dict(RESOLVERS["schedule"]),
    }
    return Vocabulary(tables=tables, misc_flags=misc)


def namespaces_json() -> list[dict[str, Any]]:
    return [
        {
            "category": category,
            "address": f"{address:#010x}",
            "idSeed": GLOBAL_ID_BASE,
            "note": (
                "Init reads the namespace's next-free counter as its global base; a registered "
                "global id is that counter's value, not an ordinal plus an offset"
            ),
        }
        for category, address in NAMESPACES.items()
    ]


def squad_slots_json() -> list[dict[str, Any]]:
    return [
        {"name": name, "globalId": value, "seededBy": "0x10316e80"}
        for name, value in SQUAD_SLOTS.items()
    ]
