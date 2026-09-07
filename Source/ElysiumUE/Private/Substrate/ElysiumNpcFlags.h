#pragma once

#include "CoreMinimal.h"

struct FElysiumSaveArchive;

// VtMB's per-NPC flag word, and the obliviousness refcount that travels with it.
//
// Retail carries two 32-bit words on `CBaseCombatCharacter` -- `m_bfAINPCFlags` at `+0x14b8` and
// `m_bfAINPCFlags2` at `+0x14bc`, both datamap members (builder `0x1031a600`) -- plus an `int`
// refcount `CAI_BaseNPC::m_iIsOblivious` at `+0x5bb4`. They are one system rather than three
// fields: the same task writes the bit and bumps the counter, and the same schedule-change virtual
// clears both.
//
// Schedules address the words BY NAME. `TASK_SET_NPC_FLAG` (0x100) and `TASK_CLEAR_NPC_FLAG`
// (0x101) take an operand spelled `NPCFlag:<name>`, which the schedule compiler resolves through
// the `strcmpi` chain at `0x1030cbd0` into a mask -- returning the bare mask for a word-one name and
// `0x80000000 | bit` for a word-two name, so the task arm can route on the sign bit. An unknown
// name is a load-time `Error`. That sign convention is why word two's bit 31 is not a flag: it is
// the routing marker, and every clear mask in the binary clears it again.
//
// The name tables below are the WHOLE recovered vocabulary, transcribed from `0x1030cbd0`. The bit
// order of word one is independently confirmed by the AI debug overlay `0x1028d990`, which walks
// bits 0-29 against the legend string `"RSCPFCNFIPCDHVAEFSBDSLIAMFDPOIO"` at `0x105d88b8`.
//
// Only the three bits `SCHED_TROIKA_MESMERIZED` writes have consumers in this runtime today
// (`IsBusyWithDiscipline`, the dialogue gate and the interest predicate). The rest are enumerated
// rather than omitted for the reason `EElysiumNpcCond` enumerates its unimplemented identities: a
// recovered name with a decoded value is data, and a schedule that lands later must be able to
// spell its own operand instead of inventing one. Nothing reads a bit that has no consumer named
// beside it.

// `m_bfAINPCFlags`, `+0x14b8`. Word one -- every name the parser returns a bare mask for.
enum class EElysiumNpcFlag : uint32
{
	None                    = 0x00000000,

	// The three `SCHED_TROIKA_MESMERIZED` writes. These are the only ones wired to a consumer.
	// `D_IS_BUSY` has exactly ONE reader in the whole binary: `CBaseCombatCharacter::
	// IsBusyWithDiscipline` (`0x1033e2b0`), whose entire body is `return (m_bfAINPCFlags & 1) != 0`.
	// The bit therefore means precisely "that predicate answers true", and its behaviour is its 17
	// callers' -- among them `CAI_BaseNPCTroika::SelectSchedule` (`0x102af660`), the dialogue gate
	// (`0x102c21c0`) and all three `StartPlayerDialog` inputs.
	D_IS_BUSY               = 0x00000001,
	// Exactly two readers, both the same virtual slot 295: `CAI_BaseNPCTroika` `0x102c21c0` and the
	// `CPayphone` override `0x101aaee0`. It is one link of the "can the player talk to me" chain, so
	// the NPC is not conversable while it is set. `NO_DIALOG_PERSISTENT` (word two, `0x10000000`) is
	// the separate form tested in the same chain that a schedule change does NOT clear.
	NO_DIALOG               = 0x00080000,
	// Exactly one reader: `0x102b3270`, the per-candidate interest predicate, which rejects on the
	// mask `DONT_INVESTIGATE | IN_FLEE_SCHED` in its first line. That predicate is reached only from
	// the two per-entity sweeps `CAI_BaseNPCTroika::GatherConditions` (`0x102b27f0`) runs back to
	// back, so the bit makes every sensed entity fail the interest test.
	DONT_INVESTIGATE        = 0x04000000,

	// The rest of the recovered word, no consumer in this runtime.
	DO_STARTLED             = 0x00000002,
	AT_CROSSWALK            = 0x00000004,
	// Named here because retail's schedule-change clear is GATED on it: `OnScheduleChange` skips its
	// whole reset, this word's clear mask included, when `PRESERVE_PATH` is set.
	PRESERVE_PATH           = 0x00000008,
	FINDING_BODY            = 0x00000010,
	CARRYING_BODY           = 0x00000020,
	NAV_IGNORE_NPC          = 0x00000040,
	IN_FLEE_SCHED           = 0x00000080,
	INITIAL_FLEE            = 0x00000100,
	COWER_PATH              = 0x00000200,
	COWERING                = 0x00000400,
	DODGING                 = 0x00000800,
	MADE_HUNT_PATH          = 0x00001000,
	AT_COVER_HINT           = 0x00002000,
	ANIM_MOVEMENT           = 0x00004000,
	DONE_EXTRAPOLATING      = 0x00008000,
	FORCE_RELAXED_ANIMS     = 0x00010000,
	SLEEPING                = 0x00020000,
	BOTCHED_ATTACK          = 0x00040000,
	SKIPPED_SOUND           = 0x00100000,
	LOOKED_AT_UNKNOWN       = 0x00200000,
	IGNORE_UNKNOWN          = 0x00400000,
	ATTACK_UNKNOWN          = 0x00800000,
	MADE_INITIAL_RESPONSE   = 0x01000000,
	FINISHED_IGNORE_UNKNOWN = 0x02000000,
	PLAYING_FACE_ANIM       = 0x08000000,
	FORCED_OCCLUDE          = 0x10000000,
	INTERESTING_INTO        = 0x20000000,
	ONE_HIT_KILL            = 0x40000000,
};

ENUM_CLASS_FLAGS(EElysiumNpcFlag);

// `m_bfAINPCFlags2`, `+0x14bc`. Word two, spelled as the BIT rather than as the parser's return
// value: the `0x80000000` the parser ORs in is the routing marker for the task arm, not part of the
// flag. No bit of this word has a consumer in this runtime yet; `MADE_OBLIVIOUS` is here because
// `TASK_MAKE_OBLIVIOUS` writes it and the schedule-change clear reads it to decide whether to
// decrement the refcount.
enum class EElysiumNpcFlag2 : uint32
{
	None                     = 0x00000000,
	SLEEP_BOUNDING_BOX       = 0x00000001,
	FINISH_SPECIAL_NAV       = 0x00000002,
	SCHEDULE_CHANGED         = 0x00000004,
	INTERESTING_LOST         = 0x00000008,
	TASKS_FACE_ENEMY         = 0x00000010,
	TASKS_FACE_TARGET        = 0x00000020,
	IGNORE_SQUAD_SEE_ENEMY   = 0x00000040,
	NO_UNKNOWN_ATTACK        = 0x00000080,
	COVER_VS_MELEE_MODE      = 0x00000100,
	IGNORE_DOOR_FAILURE      = 0x00000200,
	MOVE_FACE_ENEMY          = 0x00000400,
	DISALLOW_TGT_DISCIPLINE  = 0x00000800,
	// Pure bookkeeping. It has ZERO readers anywhere in retail -- a scan of every access to `+0x14bc`
	// finds no test of `0x1000`. Its only job is to tell the schedule-change virtual that this NPC
	// owes a refcount decrement. All the behaviour hangs off `FElysiumNpcFlags::ObliviousCount`.
	MADE_OBLIVIOUS           = 0x00001000,
	SQUAD_NEW_ENEMY          = 0x00002000,
	DONT_FALL_TO_GROUND      = 0x00004000,
	DISABLE_BURST_FIRE       = 0x00008000,
	D_CALM                   = 0x00010000,
	D_INSANE                 = 0x00020000,
	D_POSSESSED              = 0x00040000,
	D_MILDLY_CRAZY           = 0x00080000,
	D_FOLLOW                 = 0x00100000,
	D_NIGHTMARE              = 0x00200000,
	D_AUTO_FEEDABLE          = 0x00400000,
	D_DISCONNECT_SQUAD       = 0x00800000,
	D_WPN_HIDDEN             = 0x01000000,
	CHOOSE_NEW_SCHEDULE      = 0x02000000,
	NO_UNKNOWN_VISION        = 0x04000000,
	NOT_FEEDABLE             = 0x08000000,
	NO_DIALOG_PERSISTENT     = 0x10000000,
	DISAPPEAR                = 0x20000000,
	ACTIVITY_COPY_PROP_CLEAN = 0x40000000,
};

ENUM_CLASS_FLAGS(EElysiumNpcFlag2);

/**
 * The two flag words and the obliviousness refcount, as one saved object.
 *
 * Every mutation retail performs on these is a method here, so a caller cannot write a bit without
 * going through the recovered rule that owns it -- particularly `OnScheduleChange`, which is what
 * makes an incapacitating schedule unwind by itself.
 */
class FElysiumNpcFlags
{
public:
	bool Has(EElysiumNpcFlag Flag) const { return (Word1 & static_cast<uint32>(Flag)) != 0; }
	bool Has(EElysiumNpcFlag2 Flag) const { return (Word2 & static_cast<uint32>(Flag)) != 0; }

	void Set(EElysiumNpcFlag Flag) { Word1 |= static_cast<uint32>(Flag); }
	void Set(EElysiumNpcFlag2 Flag) { Word2 |= static_cast<uint32>(Flag); }
	void Clear(EElysiumNpcFlag Flag) { Word1 &= ~static_cast<uint32>(Flag); }
	void Clear(EElysiumNpcFlag2 Flag) { Word2 &= ~static_cast<uint32>(Flag); }

	/**
	 * Is this NPC oblivious -- `m_iIsOblivious > 0`?
	 *
	 * A REFCOUNT, not a flag, and that is load-bearing: retail nests the sources (a scripted scene,
	 * a grapple, being fed upon, and this task all increment it), so a body that is oblivious for
	 * two reasons stays oblivious when one of them ends.
	 */
	bool IsOblivious() const { return ObliviousCount > 0; }

	/**
	 * `CAI_BaseNPC` `0x1026d130` / `0x1026d160` -- the increment and decrement halves.
	 *
	 * The caller owns the rest of what `TASK_MAKE_OBLIVIOUS` does (clearing the enemy, the outputs),
	 * because those reach the world and this object does not. What lives here is the counter and the
	 * bookkeeping bit, which is the pair the schedule-change clear has to keep consistent.
	 */
	void AddOblivious();
	void RemoveOblivious();

	/**
	 * `CAI_BaseNPCTroika::OnScheduleChange`, virtual slot 435 (`0x102a0940`) -- the tail of every
	 * `SetSchedule`.
	 *
	 * Returns whether the oblivious refcount was released, so the caller can run the world-facing
	 * half (rejoining a squad, resuming sensing) it owns.
	 *
	 * The recovered body, in order: set `SCHEDULE_CHANGED`; then, ONLY when `PRESERVE_PATH` is
	 * clear, reset the navigator and apply `Word1 &= 0xbbf4b97e` and `Word2 &= 0x77fff14f`, and if
	 * `MADE_OBLIVIOUS` was set, clear it too and decrement the refcount. `~0xbbf4b97e` is
	 * `0x440b4681` -- `D_IS_BUSY`, `IN_FLEE_SCHED`, `COWER_PATH`, `COWERING`, `ANIM_MOVEMENT`,
	 * `FORCE_RELAXED_ANIMS`, `SLEEPING`, `NO_DIALOG`, `DONT_INVESTIGATE` and `ONE_HIT_KILL` -- so all
	 * three bits the mesmerize program sets are released by the next schedule this NPC is given.
	 *
	 * The two unconditional tail clears (`Word2 &= 0x3fffffff`, `Word1 &= 0xd7ffffff`) run outside
	 * the `PRESERVE_PATH` guard and are applied here too.
	 *
	 * The navigator/motor/goal reset inside the same guard is the movement half of the virtual and
	 * reaches the world, so it lives on the runner (`FElysiumNpc::OnScheduleChange`), which reads
	 * `PRESERVE_PATH` off this object before calling in here. Same guard, same order.
	 */
	bool OnScheduleChange();

	void Serialize(FElysiumSaveArchive& Ar);

	/** One trace row: the set bits by name, or `-` when both words are empty. */
	FString Describe() const;

	/**
	 * The `NPCFlag:<name>` operand parser, `0x1030cbd0`.
	 *
	 * Answers which word the name belongs to. False for a name this runtime's tables do not carry,
	 * which is a refusal rather than a zero mask: retail makes an unknown name a load-time `Error`,
	 * and silently setting nothing would be a schedule that reads as if it ran.
	 */
	static bool ParseName(const FString& Name, EElysiumNpcFlag& OutWord1, EElysiumNpcFlag2& OutWord2);
	static const TCHAR* LexToString(EElysiumNpcFlag Flag);

private:
	// `m_bfAINPCFlags` / `m_bfAINPCFlags2`.
	uint32 Word1 = 0;
	uint32 Word2 = 0;
	// `CAI_BaseNPC::m_iIsOblivious`, `+0x5bb4`.
	int32 ObliviousCount = 0;
};
