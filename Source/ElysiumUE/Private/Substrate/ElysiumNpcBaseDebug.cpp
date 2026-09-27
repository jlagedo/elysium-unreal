// `CAI_BaseNPC`'s bodies of the `Debug` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseDebug.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumStub.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcDebugShared.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumSchedule.h"

// --- File-scope helpers moved with the base bodies (story 5 step 5) ---

namespace
{
	const TCHAR* const GNpcKernelDebugChannelDevMsg = TEXT("DevMsg");
	const TCHAR* const GNpcKernelDebugChannelMsg = TEXT("Msg");
	const TCHAR* const GNpcKernelDebugChannelEntityText = TEXT("EntityText");
	// A vector that is ALREADY in source units — a literal box extent out of the retail body.
	FString GNpcKernelDebugUnitVec(const FVector& Units)
	{
		return FString::Printf(TEXT("(%.1f %.1f %.1f)"), Units.X, Units.Y, Units.Z);
	}
	// `0x1027e7f0`'s `default:` arm, `&DAT_105cd454`.
	const TCHAR* const GNpcKernelDebugShortConditionDefault = TEXT("***");
	// `0x1027e760`'s jump table, the navigation-type names slot 407 forwards to. Read out of
	// `.rdata`: `0x105cd44c`, `0x105cd444`, `0x105cd440`, `0x105cd438` for 0..3 and `0x10547418` for
	// -1, with `0x105477a4` the `default:`.
	const TCHAR* const GNpcKernelDebugNavTypeNames[] = {
		TEXT("Ground"), TEXT("Jump"), TEXT("Fly"), TEXT("Climb") };
	const TCHAR* const GNpcKernelDebugNavTypeNone = TEXT("None");
	const TCHAR* const GNpcKernelDebugNavTypeUnknown = TEXT("**UNKNOWN**");
	// `CAI_GlobalNamespace::IdToSymbol`'s answer for -1 (`s_<<null>>_1060f71c`, `0x102ea020`).
	const TCHAR* const GNpcKernelDebugNullSymbol = TEXT("<<null>>");
}

// --- Moved from `ElysiumNpcDebug.cpp` (story 5 step 5) ---

void FElysiumNpcBase::EmitDevMsg(const TCHAR* RetailFormat, const FString& Text)
{
	// Retail's `DevMsg(fmt, …)`. `ReportAIState` is the only body in this family that uses it.
	NpcKernelDebugShared::GNpcKernelDebugRecord(GNpcKernelDebugChannelDevMsg, RetailFormat, CopyTemp(Text), INDEX_NONE);
}

void FElysiumNpcBase::EmitDebugMsg(const TCHAR* RetailFormat, const FString& Text)
{
	// Retail's `0x10119750`: four ConVar gates, then `Q_vsnprintf` into 512 bytes, then `strncpy`
	// of the first 0x60 into a growing ring of 0x60-byte rows. The gates are console variables this
	// runtime has no counterpart for, so the write is unconditional here — a stated divergence, and
	// the only one in this family: retail's gate decides WHETHER a developer sees the line, never
	// what the line says or in what order the arms ran.
	NpcKernelDebugShared::GNpcKernelDebugRecord(GNpcKernelDebugChannelMsg, RetailFormat, CopyTemp(Text), INDEX_NONE);
}

void FElysiumNpcBase::EmitEntityText(int32 Line, const TCHAR* RetailFormat, const FString& Text)
{
	// Retail's `DAT_1070b22c`+0x8c — `IVEngineServer::AddEntityTextOverlay(edictIndex, line, 0,
	// 255, 255, 255, 255, text)` followed by `0x101434b0`. The colour is white and opaque on every
	// call site in this family, so it is not carried.
	NpcKernelDebugShared::GNpcKernelDebugRecord(GNpcKernelDebugChannelEntityText, RetailFormat, CopyTemp(Text), Line);
}

void FElysiumNpcBase::EmitOverlayBox(const TCHAR* RetailCall, const FVector& OriginUnits,
	const FVector& MinsUnits, const FVector& MaxsUnits, int32 R, int32 G, int32 B, int32 A)
{
	NpcKernelDebugShared::GNpcKernelDebugRecord(NpcKernelDebugShared::GNpcKernelDebugChannelOverlay, RetailCall,
		FString::Printf(TEXT("%s mins=%s maxs=%s rgba=(%d %d %d %d)"),
			*GNpcKernelDebugUnitVec(OriginUnits), *GNpcKernelDebugUnitVec(MinsUnits),
			*GNpcKernelDebugUnitVec(MaxsUnits), R, G, B, A),
		INDEX_NONE);
}

void FElysiumNpcBase::EmitOverlayBoxDirection(const TCHAR* RetailCall, const FVector& OriginUnits,
	const FVector& MinsUnits, const FVector& MaxsUnits, const FVector& Direction, int32 R, int32 G,
	int32 B, int32 A)
{
	NpcKernelDebugShared::GNpcKernelDebugRecord(NpcKernelDebugShared::GNpcKernelDebugChannelOverlay, RetailCall,
		FString::Printf(TEXT("%s mins=%s maxs=%s dir=%s rgba=(%d %d %d %d)"),
			*GNpcKernelDebugUnitVec(OriginUnits), *GNpcKernelDebugUnitVec(MinsUnits),
			*GNpcKernelDebugUnitVec(MaxsUnits), *GNpcKernelDebugUnitVec(Direction), R, G, B, A),
		INDEX_NONE);
}

void FElysiumNpcBase::EmitOverlayLine(const TCHAR* RetailCall, const FVector& StartUnits,
	const FVector& EndUnits, int32 R, int32 G, int32 B, bool bNoDepthTest)
{
	NpcKernelDebugShared::GNpcKernelDebugRecord(NpcKernelDebugShared::GNpcKernelDebugChannelOverlay, RetailCall,
		FString::Printf(TEXT("%s -> %s rgb=(%d %d %d) nodepth=%d"),
			*GNpcKernelDebugUnitVec(StartUnits), *GNpcKernelDebugUnitVec(EndUnits), R, G, B,
			bNoDepthTest ? 1 : 0),
		INDEX_NONE);
}

void FElysiumNpcBase::EmitOverlayText(const TCHAR* RetailCall, const FVector& OriginUnits,
	const FString& Text)
{
	NpcKernelDebugShared::GNpcKernelDebugRecord(NpcKernelDebugShared::GNpcKernelDebugChannelOverlay, RetailCall,
		FString::Printf(TEXT("%s \"%s\""), *GNpcKernelDebugUnitVec(OriginUnits), *Text),
		INDEX_NONE);
}

const TCHAR* FElysiumNpcBase::ShortConditionNameTable(int32 ConditionId)
{
	// `0x1027e7f0`. A dense switch 0x00..0x76 and a `default:`; no -1 arm, so -1 gets `"***"` here
	// exactly as it does in retail.
	if (ConditionId >= 0 && ConditionId < UE_ARRAY_COUNT(NpcKernelDebugShared::GNpcKernelDebugShortConditionNames))
	{
		return NpcKernelDebugShared::GNpcKernelDebugShortConditionNames[ConditionId];
	}
	return GNpcKernelDebugShortConditionDefault;
}

// -------------------------------------------------------------------------------------------------
// Slot 408 `GetShortConditionName`.
// -------------------------------------------------------------------------------------------------

const TCHAR* FElysiumNpcBase::GetShortConditionName(int32 ConditionId)
{
	// slot 408, `CAI_BaseNPC::GetShortConditionName` `0x1027ede0` — sixteen bytes:
	//     thunk_FUN_1027e7f0(param_1); return;
	// `CNPC_VMingXiao`, `CNPC_VMingXiaoTentacle` and `CNPC_VWerewolf` override this method with
	// their own blocks of names (story 5 step 4), each missing into a direct call to this body.
	return ShortConditionNameTable(ConditionId);
}

const TCHAR* FElysiumNpcBase::ConditionName(int32 ConditionId)
{
	// slot 458, `0x102cc300`:
	//
	//     if (param_1 < 1000000000 || param_1 == -1)
	//         param_1 = ConditionLocalToGlobal(GetClassScheduleIdSpace() + 0x30, param_1);
	//     IdToSymbol(&DAT_109203dc, param_1);
	//
	// The 1e9 gate is retail's "this is already a GLOBAL id" test — the same constant family Squad
	// found seeding the squad-slot namespace (`0x3b9aca00`) — so a script-registered id above it
	// skips the translation and goes straight to the namespace.
	int32 GlobalId = ConditionId;
	if (ConditionId < 1000000000 || ConditionId == INDEX_NONE)
	{
		int32 Count = 0;
		const FKernelIdSpace* Rows = ConditionIdSpaceRows(Count);
		GlobalId = IdSpaceLocalToGlobal(Rows, Count, ConditionId);
	}
	return GlobalConditionName(GlobalId);
}

TCHAR* FElysiumNpcBase::TaskName(int32 TaskId)
{
	// slot 449, `0x102cc350` — byte-for-byte `ConditionName` against the TASK sub-space (`+0x18`)
	// and the task namespace `DAT_109203d4`. Ten direct callers and eight dispatch sites, all of
	// them debug output.
	//
	// The return is retail's non-const `char*` into `.rdata`; the generated declaration keeps the
	// non-constness, so the literal is cast the same way retail's pointer is.
	int32 GlobalId = TaskId;
	if (TaskId < 1000000000 || TaskId == INDEX_NONE)
	{
		int32 Count = 0;
		const FKernelIdSpace* Rows = TaskIdSpaceRows(Count);
		GlobalId = IdSpaceLocalToGlobal(Rows, Count, TaskId);
	}
	return const_cast<TCHAR*>(GlobalTaskName(GlobalId));
}

const TCHAR* FElysiumNpcBase::GetNavTypeName(int32 NavType)
{
	// slot 407, `0x1027e7d0` — sixteen bytes forwarding to the table `0x1027e760`:
	//     0 Ground, 1 Jump, 2 Fly, 3 Climb, -1 None, default **UNKNOWN**.
	//
	// 29c's walk read this as "Ground / two unnamed / Climb, with -1 answering the same empty string
	// state 0 does"; the `.rdata` says otherwise on both counts and is what is ported. This runtime
	// stands no `Navigation_t` yet — the motor family's navigator seam has no type word — so nothing
	// calls this with a live value; the table is the recovered answer all the same.
	if (NavType == INDEX_NONE)
	{
		return GNpcKernelDebugNavTypeNone;
	}
	if (NavType >= 0 && NavType < UE_ARRAY_COUNT(GNpcKernelDebugNavTypeNames))
	{
		return GNpcKernelDebugNavTypeNames[NavType];
	}
	return GNpcKernelDebugNavTypeUnknown;
}

const TCHAR* FElysiumNpcBase::BaseSchedulingErrorName()
{
	// `0x101a6660`, slot 451's BASE body — six bytes, `return s_CAI_BaseNPC_10594820;` = the
	// classname string `"CAI_BaseNPC"`. Not an error message: it is the name a scheduling error is
	// REPORTED AGAINST, which is why each tier answers its own class name.
	return TEXT("CAI_BaseNPC");
}

// -------------------------------------------------------------------------------------------------
// Slot 581 `ReportAIState` — `0x102779a0`, the SDK dev dump.
// -------------------------------------------------------------------------------------------------

void FElysiumNpcBase::ReportAIState()
{
	// `0x102779a0`, arm by arm and in retail's exact order. Every line is a separate `DevMsg`, and
	// several of them are deliberately unterminated (`"%s: "`, `"State: %s, "`) because the dump is
	// one console line assembled from fragments.
	//
	// Retail evaluates `m_NPCState` into a register BEFORE the first `DevMsg`, then uses that same
	// register as `GetClassname`'s dead second argument on all three calls — an artefact, not a
	// behaviour, and not reproduced.

	// 1. `DevMsg("%s: ", GetClassname())` then `DevMsg("State: %s, ", GetStateName(m_NPCState))`.
	EmitDevMsg(TEXT("%s: "), FString::Printf(TEXT("%s: "),
		Def != nullptr ? *Def->Classname : TEXT("")));
	const TCHAR* StateName = GetStateName(GetMind().State());
	EmitDevMsg(TEXT("State: %s, "), FString::Printf(TEXT("State: %s, "),
		StateName != nullptr ? StateName : TEXT("")));

	// 2. `if (m_Activity != -1 && m_IdealActivity != -1)`: BOTH must be set, and the line prints the
	//    activity names resolved through `SelectWeightedSequence(act, -1)` +
	//    `GetSequenceActivityName(seq)` — the name of the activity the CHOSEN SEQUENCE carries, not
	//    the name of the activity asked for. `ActivityNumber` (+0x0fec, family Positions) and
	//    `IdealActivityNumber` (+0x0ff0, family Facing) are the port's two words.
	if (ActivityNumber != INDEX_NONE && IdealActivityNumber != INDEX_NONE)
	{
		EmitDevMsg(TEXT("Activity: %s  -  Ideal Activity: %s\n"),
			FString::Printf(TEXT("Activity: %s  -  Ideal Activity: %s\n"),
				*ActivityNameForNumber(ActivityNumber),
				*ActivityNameForNumber(IdealActivityNumber)));
	}

	// 3. `if (!m_pSchedule) DevMsg("No Schedule, ")` else the name (or `"Unknown"` for a null name
	//    pointer) and, when `GetCurTask()` answers a task, `"Task %d (#%d), "` with the task's
	//    retail NUMBER and `m_ScheduleState`. The number is `CurrentRetailTaskNumber`'s seam.
	if (!Schedule.IsRunning())
	{
		EmitDevMsg(TEXT("No Schedule, "), TEXT("No Schedule, "));
	}
	else
	{
		const TCHAR* ScheduleName = ElysiumScheduleName(Schedule.Current);
		EmitDevMsg(TEXT("Schedule %s, "), FString::Printf(TEXT("Schedule %s, "),
			ScheduleName != nullptr ? ScheduleName : TEXT("Unknown")));
		int32 TaskNumber = INDEX_NONE;
		if (CurrentRetailTaskNumber(TaskNumber))
		{
			EmitDevMsg(TEXT("Task %d (#%d), "),
				FString::Printf(TEXT("Task %d (#%d), "), TaskNumber, Schedule.TaskIndex));
		}
	}

	// 4. `if (!GetEnemy()) DevMsg("No enemy ")` else — and this is the arm to read carefully —
	//    retail RE-DISPATCHES `GetEnemy()` three times, takes the enemy's `GetAbsOrigin()`, builds
	//    `(x, y, z + 64.0)` (`_DAT_10451acc` = 64.0) and hands it to `PTR_DAT_10566258`'s `+0xc`
	//    with `(1, 1, 0)`. That is a DEBUG OVERLAY call on the engine's effect interface, not a
	//    state write: the vector is built and dropped. The line itself is `"\nEnemy is %s"`.
	const FElysiumEntity* Enemy = GetEnemy();
	if (Enemy == nullptr)
	{
		EmitDevMsg(TEXT("No enemy "), TEXT("No enemy "));
	}
	else
	{
		// `_DAT_10451acc` = 64.0f, source units, added to the enemy's origin Z.
		const FVector MarkerUnits = Enemy->Origin / ElysiumMove::U + FVector(0.f, 0.f, 64.f);
		EmitOverlayText(TEXT("PTR_DAT_10566258+0xc"), MarkerUnits, FString());
		EmitDevMsg(TEXT("\nEnemy is %s"), FString::Printf(TEXT("\nEnemy is %s"),
			Enemy->Def != nullptr ? *Enemy->Def->Classname : TEXT("")));
	}

	// 5. `if (IsMoving())`: `" Moving "`, then ONE of two sub-arms.
	//    `m_flMoveWaitFinished > curtime` -> `": Stopped for %.2f. "` with the remaining wait;
	//    otherwise, and only if `m_IdealActivity == GetStoppedActivity()` (`0x1027a6c0`),
	//    `": In stopped anim. "`. Note the comparison is `<=` in the binary, so a wait that expires
	//    exactly on this frame takes the stopped-anim arm.
	if (IsMoving())
	{
		EmitDevMsg(TEXT(" Moving "), TEXT(" Moving "));
		const double Now = World != nullptr ? World->NowSeconds() : 0.0;
		if (BaseScheduleHost.MoveWaitFinished > Now)
		{
			EmitDevMsg(TEXT(": Stopped for %.2f. "), FString::Printf(TEXT(": Stopped for %.2f. "),
				BaseScheduleHost.MoveWaitFinished - Now));
		}
		else if (IsIdealActivityCurrent())
		{
			// SEAM for `GetStoppedActivity()` (`0x1027a6c0`) compared against `m_IdealActivity`:
			// this runtime's ideal activity is a clip identity, and the nearest recovered question
			// it can answer is "is the ideal activity the one currently playing".
			EmitDevMsg(TEXT(": In stopped anim. "), TEXT(": In stopped anim. "));
		}
	}

	// 6. `DevMsg("Leader.")` then `DevMsg("\n")` — UNCONDITIONAL in retail. SDK 2013 gates the same
	//    line on `IsLeader()`; Troika's build prints it for every NPC, and that is reproduced.
	EmitDevMsg(TEXT("Leader."), TEXT("Leader."));
	EmitDevMsg(TEXT("\n"), TEXT("\n"));

	// 7. `DevMsg("Yaw speed:%3.1f,Health: %3d\n", m_pMotor->m_YawSpeed, m_iHealth)`.
	EmitDevMsg(TEXT("Yaw speed:%3.1f,Health: %3d\n"),
		FString::Printf(TEXT("Yaw speed:%3.1f,Health: %3d\n"), MotorYawSpeed(), Health));

	// 8. `GetGroundEntity()` twice: the classname line or the NULL line.
	const FElysiumEntity* Ground = GetGroundEntity();
	if (Ground != nullptr)
	{
		EmitDevMsg(TEXT("Groundent:%s\n\n"), FString::Printf(TEXT("Groundent:%s\n\n"),
			Ground->Def != nullptr ? *Ground->Def->Classname : TEXT("")));
	}
	else
	{
		EmitDevMsg(TEXT("Groundent: NULL\n\n"), TEXT("Groundent: NULL\n\n"));
	}
}

void FElysiumNpcBase::DrawDebugStatOverlays()
{
	// `0x102775e0`. Recovered from the LISTING, not the decompiled C: the decompiler lost four of
	// the six `Q_snprintf` format strings to the 512-byte stack frame, and mislabelled which of the
	// task line's two markers is which.
	//
	// The whole body is gated on `GetModelPtr() != NULL` — no model, no lines at all. The port's
	// question for "does this entity have a model" is `FElysiumEntity::Model`; the studio HEADER
	// behind it is the seam every other read below goes through.
	//
	// 1. `Q_snprintf(buf, 512, "Seq: ")`, then `GetSeqDesc(m_nSequence)`:
	//      no descriptor -> `Q_strncat(buf, "(INVALID)")`
	//      a descriptor  -> `Q_strncat(buf, seqdesc->pszLabel)`, `" / "`, `seqdesc->pszActivityName`
	//    and the assembled buffer is the FIRST line.
	if (Model.IsEmpty())
	{
		return;
	}
	FString Line(TEXT("Seq: "));
	FString SequenceLabel;
	FString SequenceActivityName;
	if (!SequenceDescriptor(SequenceNumber, SequenceLabel, SequenceActivityName))
	{
		Line += TEXT("(INVALID)");
		EmitDebugMsg(TEXT("Seq: (INVALID)"), MoveTemp(Line));
	}
	else
	{
		Line += SequenceLabel;
		Line += TEXT(" / ");
		Line += SequenceActivityName;
		EmitDebugMsg(TEXT("Seq: %s / %s"), MoveTemp(Line));
	}

	// 2. `"Cycle: %.2f"` with `m_flCycle` (+0x6f8).
	EmitDebugMsg(TEXT("Cycle: %.2f"), FString::Printf(TEXT("Cycle: %.2f"), SequenceCycle));

	// 3. The activity line. THREE arms, and the order of the tests matters:
	//      m_Activity == -1 || m_IdealActivity == -1
	//          -> m_Activity == 0  ? "Actv: RESET"   : "Actv: INVALID"
	//      m_Activity == 0         -> "Actv: RESET"
	//      otherwise -> "Actv: %s (%s)\n" with both activities pushed through the SAME three-step
	//          translation retail applies before naming them: slot 375 (NPC_EarlyTranslateActivity),
	//          slot 381 (Weapon_TranslateActivity), slot 376 (NPC_TranslateActivity), then
	//          SelectWeightedSequence + GetSequenceActivityName.
	//    `ACT_RESET` is 0 and `ACT_INVALID` is -1, which is what the two literals name.
	if (ActivityNumber == INDEX_NONE || IdealActivityNumber == INDEX_NONE)
	{
		EmitDebugMsg(ActivityNumber == 0 ? TEXT("Actv: RESET") : TEXT("Actv: INVALID"),
			ActivityNumber == 0 ? TEXT("Actv: RESET") : TEXT("Actv: INVALID"));
	}
	else if (ActivityNumber == 0)
	{
		EmitDebugMsg(TEXT("Actv: RESET"), TEXT("Actv: RESET"));
	}
	else
	{
		EmitDebugMsg(TEXT("Actv: %s (%s)\n"), FString::Printf(TEXT("Actv: %s (%s)\n"),
			*ActivityNameForNumber(ActivityNumber),
			*ActivityNameForNumber(IdealActivityNumber)));
	}

	// 4. `"State: %s, "` with `GetStateName(m_NPCState)` (slot 406).
	const TCHAR* StateName = GetStateName(GetMind().State());
	EmitDebugMsg(TEXT("State: %s, "), FString::Printf(TEXT("State: %s, "),
		StateName != nullptr ? StateName : TEXT("")));

	// 5. `"Move: %s, "` with `GetNavTypeName(GetNavType())` (slot 407, the table above).
	//    SEAM: `GetNavType()` (`0x1027d990`) has no port counterpart — the motor carries no
	//    navigation type — so the name asked for is the one -1 answers, `"None"`.
	EmitDebugMsg(TEXT("Move: %s, "), FString::Printf(TEXT("Move: %s, "),
		GetNavTypeName(RetailNavType())));

	// 6. `if (!m_pSchedule) return;` — the schedule half is skipped entirely, with no "no schedule"
	//    line. `ReportAIState` prints one; this body does not.
	if (!Schedule.IsRunning())
	{
		return;
	}

	// 7. `"Schd: %s, "`, the program's name or `"Unknown"`.
	const TCHAR* ScheduleName = ElysiumScheduleName(Schedule.Current);
	EmitDebugMsg(TEXT("Schd: %s, "), FString::Printf(TEXT("Schd: %s, "),
		ScheduleName != nullptr ? ScheduleName : TEXT("Unknown")));

	// 8. `GetCurTask()`: `"Task: None"` for no task, else `"Task: %s (#%d), "` with `TaskName(id)`
	//    and `m_ScheduleState`. The listing pre-pushes `m_ScheduleState` before the `TaskName` call
	//    so that the one `%d` is fed by it; the decompiler dropped that push.
	const FElysiumScheduleProgram* Program = ElysiumScheduleFor(Schedule.Current);
	if (Program == nullptr || !Program->Tasks.IsValidIndex(Schedule.TaskIndex))
	{
		EmitDebugMsg(TEXT("Task: None"), TEXT("Task: None"));
	}
	else
	{
		EmitDebugMsg(TEXT("Task: %s (#%d), "), FString::Printf(TEXT("Task: %s (#%d), "),
			*FElysiumScheduleCorpus::Get().TaskOps().NameOf(Program->Tasks[Schedule.TaskIndex].TaskId), Schedule.TaskIndex));
	}

	// 9. Then EVERY task of the program, one line each, re-reading `m_pSchedule->numTasks` at the
	//    top of every iteration:
	//
	//      prefix = (i == 0) ? "Task:" : "       "
	//      if (i == m_ScheduleState) { lead = "->";  trail = "<-"; }
	//      else                      { lead = "   "; trail = "";   }
	//      Q_snprintf(buf, 512, "%s%s%s%s", prefix, lead, TaskName(tasks[i].iTask), trail)
	//
	//    The decompiled C swaps `lead` and `trail`; the listing's register assignment is what is
	//    ported. `tasks[i]` strides EIGHT bytes — `Task_t` is `{ int iTask; float flTaskData; }`.
	//
	//    Retail names each step through slot 449 `TaskName(number)`, which this runtime's seam
	//    cannot answer (see `TaskIdSpaceRows`). The step is named through `ElysiumTaskName` instead
	//    — the port's typed identity for the same step — and that substitution is stated here and in
	//    the story report. Nothing else about the line changes.
	if (Program != nullptr)
	{
		for (int32 Index = 0; Index < Program->Tasks.Num(); ++Index)
		{
			const TCHAR* Prefix = Index == 0 ? TEXT("Task:") : TEXT("       ");
			const bool bCurrent = Index == Schedule.TaskIndex;
			const TCHAR* Lead = bCurrent ? TEXT("->") : TEXT("   ");
			const TCHAR* Trail = bCurrent ? TEXT("<-") : TEXT("");
			EmitDebugMsg(TEXT("%s%s%s%s"), FString::Printf(TEXT("%s%s%s%s"), Prefix, Lead,
				*FElysiumScheduleCorpus::Get().TaskOps().NameOf(Program->Tasks[Index].TaskId), Trail));
		}
	}
}

bool FElysiumNpcBase::SequenceDescriptor(int32 Sequence, FString& OutLabel,
	FString& OutActivityName) const
{
	// SEAM for `CBaseAnimating::GetSeqDesc(m_nSequence)` (`0x1000b4f6`) and the two string offsets
	// off the returned `mstudioseqdesc_t` (`+0x00` the label, `+0x04` the activity name, both
	// relative to the descriptor itself). No studio header stands here — family Anim records the
	// same refusal — so this answers FALSE and both strings stay empty, which is retail's
	// `"(INVALID)"` arm in `0x102775e0`.
	(void)Sequence;
	OutLabel.Reset();
	OutActivityName.Reset();
	return false;
}

FString FElysiumNpcBase::ActivityNameForNumber(int32 Activity) const
{
	// SEAM for `SelectWeightedSequence(activity, -1)` + `GetSequenceActivityName(sequence)`, the
	// two-step both stat bodies and `ReportAIState` use to NAME an activity. It ends in the studio
	// header `SequenceDescriptor` refuses, so it answers the empty string.
	(void)Activity;
	return FString();
}

int32 FElysiumNpcBase::RetailNavType() const
{
	// SEAM for `CAI_BaseNPC::GetNavType()` (`0x1027d990`). `m_pNavigator` is an
	// `ELYSIUM_NPC_WORD_CHAIN` row onto the motor and no navigation-type word exists; -1 is what
	// slot 407 names `"None"`.
	return INDEX_NONE;
}

// --- Moved from `ElysiumNpcDebug.cpp` (story 5 step 5) ---

int32 FElysiumNpcBase::IdSpaceLocalToGlobal(const FKernelIdSpace* Rows, int32 Count, int32 LocalId)
{
	// `0x102ea2d0` verbatim:
	//
	//     if (id != -1) do {
	//         if (this->localBase != 9999 && this->localBase <= id && id <= this->localTop)
	//             return (this->globalBase - this->localBase) + id;
	//         this = this->parent;                     // +0x10
	//     } while (this != NULL);
	//     return -1;
	//
	// The 9999 test comes FIRST, so an empty space is skipped even when the id would fall inside a
	// `[9999, -1]` range that cannot hold anything anyway.
	if (LocalId == INDEX_NONE || Rows == nullptr)
	{
		return INDEX_NONE;
	}
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FKernelIdSpace& Space = Rows[Index];
		if (Space.LocalBase != 9999 && Space.LocalBase <= LocalId && LocalId <= Space.LocalTop)
		{
			return (Space.GlobalBase - Space.LocalBase) + LocalId;
		}
	}
	return INDEX_NONE;
}

const FElysiumNpcBase::FKernelIdSpace* FElysiumNpcBase::ConditionIdSpaceRows(int32& OutCount)
{
	// The chain slot 458 walks, from this leaf upward. Two rows, because this runtime stands the
	// Troika line for every classname and no SPECIES class registers a condition into a space of
	// its own that the port could carry (the three that DO add conditions — `CNPC_VMingXiao`,
	// `CNPC_VMingXiaoTentacle`, `CNPC_VWerewolf` — are slot 408's species table below, and their
	// spaces are runtime state no static read recovers).
	//
	//   * `CAI_BaseNPCTroika`'s own condition space is `DAT_10924248 + 0x30`. Nothing in the image
	//     registers a condition into it, so it keeps the empty state `0x102ea090(isRoot = false)`
	//     left: `LocalBase` 9999, and the walk falls straight through to its parent.
	//   * `CAI_BaseNPC`'s is `DAT_1090ff08 + 0x30` = `DAT_1090ff38`, and `0x102c8ce0` registers 119
	//     conditions into it at local ids 0x00..0x76 — the FIRST is `COND_NONE` at 0, so
	//     `0x102ea130`'s "expand an empty space" arm sets `LocalBase` 0 and the last raises
	//     `LocalTop` to 0x76. `GlobalBase` is 0 because this is the namespace's root space
	//     (`0x1030c4e0` seeds it with `0x102ea0e0(&DAT_1090ff38, &DAT_109203dc, 0)`).
	//
	// So the recovered translation for a base condition is the IDENTITY, which is why this runtime's
	// `EElysiumNpcCond` values are retail's registered numbers and read correctly here.
	static const FKernelIdSpace GRows[] = {
		{ TEXT("CAI_BaseNPCTroika"), TEXT("0x10924278"), INDEX_NONE, 9999, INDEX_NONE },
		{ TEXT("CAI_BaseNPC"), TEXT("0x1090ff38"), 0, 0, 0x76 },
	};
	OutCount = UE_ARRAY_COUNT(GRows);
	return GRows;
}

const FElysiumNpcBase::FKernelIdSpace* FElysiumNpcBase::TaskIdSpaceRows(int32& OutCount)
{
	// The same chain for TASKS — `DAT_10924248 + 0x18` and `DAT_1090ff08 + 0x18` = `DAT_1090ff20`.
	//
	// **Both rows are the empty sentinel, and the base one is UNRECOVERED rather than known-empty.**
	// `0x10316ff0` registers the 441 `TASK_*` symbols into `DAT_1090ff20` at runtime and this family
	// did not transcribe that table: this runtime's task vocabulary is `EElysiumTask`, a typed
	// ~30-identity subset with NO registered numbers at all (family BaseHelpers'
	// `CurrentRetailTaskNumber` answers false for exactly this reason), so a number to translate
	// never arrives. The row is left empty and says so rather than carrying a range nothing can use.
	static const FKernelIdSpace GRows[] = {
		{ TEXT("CAI_BaseNPCTroika"), TEXT("0x10924260"), INDEX_NONE, 9999, INDEX_NONE },
		{ TEXT("CAI_BaseNPC"), TEXT("0x1090ff20"), INDEX_NONE, 9999, INDEX_NONE },
	};
	OutCount = UE_ARRAY_COUNT(GRows);
	return GRows;
}

const TCHAR* FElysiumNpcBase::GlobalConditionName(int32 GlobalConditionId)
{
	// `0x102ea020`: `if (id == -1) return "<<null>>"; return IdToSymbol(this->table, id);`, and the
	// table walk (`0x10249c70`) answers NULL for an id it does not carry — which retail then hands
	// to `printf` as a `%s`.
	if (GlobalConditionId == INDEX_NONE)
	{
		return GNpcKernelDebugNullSymbol;
	}
	if (GlobalConditionId >= 0
		&& GlobalConditionId < UE_ARRAY_COUNT(NpcKernelDebugShared::GNpcKernelDebugConditionNames))
	{
		return NpcKernelDebugShared::GNpcKernelDebugConditionNames[GlobalConditionId];
	}
	return nullptr;
}

const TCHAR* FElysiumNpcBase::GlobalTaskName(int32 GlobalTaskId)
{
	// The same body over `DAT_109203d4`. SEAM: see `TaskIdSpaceRows` — the 441 `TASK_*` symbols
	// `0x10316ff0` registers are not carried here, so every id but -1 answers the namespace's
	// "no such symbol", which is null.
	return GlobalTaskId == INDEX_NONE ? GNpcKernelDebugNullSymbol : nullptr;
}

float FElysiumNpcBase::MotorYawSpeed() const
{
	// SEAM for `CAI_Motor::m_YawSpeed` (`m_pMotor` `+0x38`), the STORED yaw speed `SetYawSpeed`
	// writes. Slot 516 `MaxYawSpeed` computes a different word — the CEILING — and answering with
	// it would be an invention, so this answers 0.
	return 0.f;
}
