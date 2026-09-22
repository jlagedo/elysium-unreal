// The schedule-text parser `0x1030d850`, and the record it builds.
//
// One text is one or more `Schedule <name> Tasks ... [Interrupts ...] [Flags ...]` records, and
// this is the body that turns them into programs. The grammar, the seventeen operand forms, the
// failure table and the two things that do NOT fail are walked in
// `docs/vtmb/npc-ai/schedule-kernel.md` § "The schedule-text parser `0x1030d850`, walked"; that
// section is the contract, and where this file and it disagree, it is right.
//
//   text      ::= { record }
//   record    ::= "Schedule" name "Tasks" { task } [ "Interrupts" { [ "!" ] cond } ] [ "Flags" { flag } ]
//   task      ::= task_name operand
//   operand   ::= prefix ":" value | "TRUE" | "ON" | "FALSE" | "OFF" | number
//
// `Tasks` is required; `Interrupts` and `Flags` are optional and in that order. A text whose first
// token is not `Schedule` -- an empty text included -- returns SUCCESS and loads nothing.
//
// The pipeline runs the same body in Python (`formats/ai_schedule_glb/parser.py`) so the export can
// refuse to publish a text retail would refuse. Both were written from the same oracle section, and
// the Python one reads all 691 shipped texts with zero refusals; it is the fixture this port is
// written against.

#pragma once

#include "CoreMinimal.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumScheduleOperands.h"

struct FElysiumIdNamespace;
struct FElysiumLocalIdSpace;
struct FElysiumSymbolRegistry;

/**
 * One task record: retail's two 32-bit words, id then data (`+0x20`, `count x 8`).
 *
 * **Stated divergence.** Retail stores the CLASS-LOCAL task id here; this port stores the GLOBAL
 * one. One leaf runtime stands for every retail class, so a local id would collide across sibling
 * spaces exactly as schedule id `0x156` names six different programs in six different tables. The
 * local number is still one `GlobalToLocal` away through the owning class's task space when a
 * consumer wants it; nothing in this runtime does.
 */
struct FElysiumScheduleStep
{
	int32 TaskId = INDEX_NONE;   // +0x00
	float Data = 0.0f;           // +0x04

	/** The data word as retail's raw 32 bits. `NPCFlag:`, `MiscFlag:` and `Model:` store their
	 *  answer unconverted, and this is how it comes back out. */
	uint32 RawWord() const { return FMath::AsUInt(Data); }
	void SetRawWord(uint32 Word) { Data = FMath::AsFloat(Word); }
};

/**
 * One compiled schedule: `CAI_Schedule`, 0x48 bytes, field for field.
 *
 * The two masks are 256-bit here where retail's are 192. That is width, not meaning: the bits are
 * global condition ordinals in both, retail's top identity is `0x76`, and nothing reads word four.
 */
struct FElysiumScheduleProgram
{
	/** +0x00. The `!COND_*` mask: these interrupt on the ABSENCE of the condition. */
	FElysiumNpcConditions InvertedInterrupts;

	/** +0x18. `DELAY_INTERRUPTS`, the ONLY schedule flag retail has.
	 *
	 *  The token table (`0x1030d7e0`) answers exactly two spellings -- `NONE` -> 0 and
	 *  `DELAY_INTERRUPTS` -> bit 0 -- so this word is the whole flag set rather than one bit of a
	 *  larger one.
	 *
	 *  It is NOT a property the interrupt check can consult on its own. The sole tester,
	 *  `CAI_BaseNPC::IsScheduleValid` (`0x10280ff0`, called only from `MaintainSchedule`
	 *  `0x102817c0`), ANDs it with the NPC's own `m_bDidMaintainSchedule` (`+0x5bb8`):
	 *
	 *      if (!(!m_bDidMaintainSchedule && (schedule->flags & 1)))  evaluate the interrupt mask
	 *
	 *  so the flag buys a schedule exactly ONE think of immunity, re-armed by every install and
	 *  bounded by nothing else -- no timer, no task boundary, no deferral store. See
	 *  `FElysiumScheduleState::bDidMaintainSchedule` for the other half, and `ElysiumSchedule::Start`
	 *  for the condition clear that goes with it.
	 *
	 *  42 of retail's 691 schedules carry it, and they are one family: the Discipline effects and
	 *  the externally forced states (`D_MESMERIZE`, `D_DAZE`, `D_BERSERK`, `D_TRANCE`,
	 *  `FLEE_AND_DIE`, `TROIKA_MESMERIZED`). All of them are installed from OUTSIDE the AI think,
	 *  which is the case the flag exists for: without it a forced state is re-selected away on the
	 *  same think that forced it. */
	int32 Flags = 0;

	/** +0x1c. The GLOBAL schedule id, which is what `FindById` keys on. */
	int32 GlobalId = INDEX_NONE;

	/** +0x20 / +0x24. Retail caps the list at 64; a 65th task is a failure row. */
	TArray<FElysiumScheduleStep> Tasks;

	/** +0x28. The ordinary interrupt mask: which newly gathered conditions may abort this program
	 *  (`docs/vtmb/npc-ai/conditions-and-states.md` -> "Interrupt conditions").
	 *
	 *  **Empty means interruptible by nothing**, and that is a real authored posture rather than an
	 *  unfilled default: `SCHED_TROIKA_MELEE_ATTACK1_SWING` declares no interrupts at all, so once
	 *  that terminal attack task owns the NPC it is not reevaluated as a fresh attack choice each
	 *  tick. The schedule -- not the mere existence of a condition -- decides whether a new stimulus
	 *  pre-empts behaviour, which is why a faithful AI cannot be one global priority list.
	 *
	 *  An interrupt is NOT a task failure: a failed task goes to the fail route, while an interrupt
	 *  ends the program and returns the NPC to selection. Routing an interrupt through the fail
	 *  schedule would send an NPC that just acquired an enemy into a cover or flinch program instead
	 *  of re-selecting. */
	FElysiumNpcConditions Interrupts;

	/** +0x40. A copy of the authored name, which is what `FindByName` compares. */
	FString Name;

	static constexpr int32 MaxTasks = 64;

	bool HasFlag(int32 Flag) const { return (Flags & Flag) != 0; }
};

/** `Flags`'s whole vocabulary (`0x1030d7e0`). */
namespace ElysiumScheduleFlags
{
	inline constexpr int32 None = 0;
	inline constexpr int32 DelayInterrupts = 1;
}

/** The rows of retail's failure table, each of which stops the owning class's load. */
enum class EElysiumScheduleParseFailure : uint8
{
	None = 0,
	UnknownToken,            // a record keyword that is not `Schedule`
	DuplicateScheduleName,   // the text declares one name twice
	UnknownScheduleName,     // the name is not registered in the schedule namespace
	MissingTasks,            // a `Schedule <name>` with no `Tasks`
	TaskCap,                 // a 65th task
	UnknownTask,             // a task name not registered in the task namespace
	MissingOperand,          // a task, or a prefix, with nothing after it (retail's one Warning)
	BadSyntaxAtTask,         // the operand slot holds a section keyword or another `TASK_*`
	UnknownPrefix,           // `Foo:bar`, where `Foo` is none of the seventeen
	StrayColon,              // a bare `:` operand, or a second `:` after a resolved value
	UnknownValue,            // a prefix whose value is not in its table
};

/** The row's name, spelled as the pipeline's parser spells it so the two can be diffed. */
const TCHAR* ElysiumScheduleParseFailureName(EElysiumScheduleParseFailure Failure);

/**
 * What a parse needs besides the text.
 *
 * Every pointer is optional, and a null one means "resolve nothing of this kind": the id stays
 * `INDEX_NONE`, the operand stays 0, and no text fails for it. That is the grammar-only mode the
 * tokenizer and syntax tests run in. A real load hands all of them.
 */
struct FElysiumScheduleParseContext
{
	/** The class whose text this is, for the diagnostics. Retail passes it to every resolver. */
	FString ClassName;

	/** The class's schedule space (`space+0x00`): where a record's own name is looked up, and
	 *  where a `Schedule:` operand resolves. */
	const FElysiumLocalIdSpace* ScheduleSpace = nullptr;

	/** The class's task space (`space+0x18`): the task name at each statement head, and `Task:`. */
	const FElysiumLocalIdSpace* TaskSpace = nullptr;

	/** The condition namespace. Interrupts are NOT translated -- the mask is in global ordinals,
	 *  so this is the flat namespace rather than the class's space. */
	const FElysiumIdNamespace* Conditions = nullptr;

	FElysiumSymbolRegistry* Activities = nullptr;
	FElysiumSymbolRegistry* Models = nullptr;
	FElysiumSymbolRegistry* Sounds = nullptr;
};

/** What one text parsed into, and how it stopped. */
struct FElysiumScheduleParseResult
{
	/** In authored order.
	 *
	 *  On a failure this still carries the record the parse died inside, with whatever it had read
	 *  so far. That is not tidiness: retail links the node into the manager BEFORE parsing its
	 *  tasks and no error path unlinks it, so a failed text really does leave a task-less program
	 *  behind that `FindByName` then answers. */
	TArray<FElysiumScheduleProgram> Programs;

	EElysiumScheduleParseFailure Failure = EElysiumScheduleParseFailure::None;
	FString Message;
	int32 Offset = 0;

	/** Interrupt names the condition namespace did not know. Retail `DevMsg`s each and skips the
	 *  bit without failing; this is that count, so a load can report it once instead of per text. */
	int32 SkippedConditions = 0;

	/** `Flags` tokens that resolved to 0 -- including an authored `NONE`, which is retail's own
	 *  harmless diagnostic rather than a defect. */
	int32 UnknownFlags = 0;

	bool Failed() const { return Failure != EElysiumScheduleParseFailure::None; }
};

namespace ElysiumScheduleText
{
	/** Parse one text. See `FElysiumScheduleParseResult` for what a failure still returns. */
	FElysiumScheduleParseResult Parse(const FString& Body, const FElysiumScheduleParseContext& Context);
}
