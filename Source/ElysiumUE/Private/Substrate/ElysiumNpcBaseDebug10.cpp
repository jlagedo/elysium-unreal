// `CAI_BaseNPC`'s bodies of the `Debug10` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseDebug10.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcDebug10Shared.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumSchedule.h"

// --- File-scope helpers moved with the base bodies (story 5 step 5) ---

namespace
{
	constexpr int32 GDebug10BitSquad = 0x80000;        // 0x102767ef
	constexpr int32 GDebug10BitTaskList = 0x100000;    // 0x10276dfc
	constexpr TCHAR GDebug10FmtHealth[] = TEXT("Health: %i");            // 0x105cc8cc
	constexpr TCHAR GDebug10FmtSquad[] = TEXT("Squad: %c : ");           // 0x105cc8bc
	constexpr TCHAR GDebug10SquadNone[] = TEXT(" - \n");                 // 0x105cc8b4, a 4-char string
	constexpr TCHAR GDebug10EnemyPrefix[] = TEXT("Enemy: ");             // 0x105cc8a8
	constexpr TCHAR GDebug10FmtSlot[] = TEXT("Slot:  %s \n");            // 0x105cc898
	constexpr TCHAR GDebug10FmtMem[] = TEXT("MEM%02d: %s");              // 0x105cc888
	constexpr TCHAR GDebug10FmtWeapon[] = TEXT("Weapon: %s (%d/%d) (%d/%d)"); // 0x105cc868
	constexpr TCHAR GDebug10Unarmed[] = TEXT("UNARMED");                 // 0x105cc85c
	constexpr TCHAR GDebug10FmtStat[] = TEXT("Stat: %s, ");              // 0x105cc84c
	constexpr TCHAR GDebug10FmtMove[] = TEXT("Move: %s, ");              // 0x105cc83c
	constexpr TCHAR GDebug10FmtSchd[] = TEXT("Schd: %s, ");              // 0x105cc82c
	constexpr TCHAR GDebug10Unknown[] = TEXT("Unknown");                 // 0x1053c828
	constexpr TCHAR GDebug10FmtTaskRow[] = TEXT("%s%s%s%s");             // 0x105cc804
	constexpr TCHAR GDebug10TaskLead[] = TEXT("->");                     // 0x105cc800
	constexpr TCHAR GDebug10TaskNoLead[] = TEXT("   ");                  // 0x105cc824
	constexpr TCHAR GDebug10TaskTrail[] = TEXT("<-");                    // 0x105cc828
	constexpr TCHAR GDebug10TaskFirst[] = TEXT("Task:");                 // 0x105cc81c
	constexpr TCHAR GDebug10TaskRest[] = TEXT("       ");                // 0x105cc810
	constexpr TCHAR GDebug10TaskNone[] = TEXT("Task: None");             // 0x105cc7dc
	constexpr TCHAR GDebug10FmtTask[] = TEXT("Task: %s (#%d), ");        // 0x105cc7ec
	constexpr TCHAR GDebug10FmtActv[] = TEXT("Actv: %s (%s)\n");         // 0x105cc7c8
	constexpr TCHAR GDebug10ActvInvalid[] = TEXT("Actv: INVALID");       // 0x105860c0
	constexpr TCHAR GDebug10ActvReset[] = TEXT("Actv: RESET");           // 0x105860d0
	constexpr TCHAR GDebug10FmtIntr[] = TEXT("Intr: %s (%s)\n");         // 0x105cc7b4
	constexpr TCHAR GDebug10FmtFail[] = TEXT("Fail: %s (%s)\n");         // 0x105cc7a0
	constexpr TCHAR GDebug10FmtVel[] =
		TEXT("Vel %.1f %.1f %.1f   Ang: %.1f %.1f %.1f\n");               // 0x105cc750
	constexpr int32 GDebug10RingWrapAt = 0x3dff;
}

// --- Moved from `ElysiumNpcDebug10.cpp` (story 5 step 5) ---

int32 FElysiumNpcBase::DebugLogRingAdvance(int32 Cursor, int32 Written, bool& bOutWrapped)
{
	// `0x1027ef20`'s cursor arm verbatim:
	//
	//     n = sprintf(this + 0x1b4e + cursor, text);
	//     cursor += n;
	//     if (0x3dff < cursor) {
	//         memset(this + 0x1b4e + cursor, 0, 0x4000 - cursor);
	//         *(byte*)(this + 0x5b54) = 1;
	//         cursor = 0;
	//     }
	//
	// The test is `>` against 0x3dff, so a cursor landing EXACTLY on 0x3dff does not wrap and a
	// cursor at 0x3e00 does; the zero-fill length is computed from the POST-advance cursor, so a
	// cursor past 0x4000 memsets a negative length, which retail's `rep stosd` treats as a very large
	// unsigned one. That overrun is retail's and is not reproduced — there is no ring to overrun.
	bOutWrapped = false;
	int32 Next = Cursor + Written;
	if (Next > GDebug10RingWrapAt)
	{
		bOutWrapped = true;
		Next = 0;
	}
	return Next;
}

void FElysiumNpcBase::AppendDebugLogLine(const TCHAR* Text)
{
	// `0x1027ef20`. A null line does nothing at all — the whole body is under `if (param_1 != 0)`.
	//
	// ABSENT (story 29b): the 16 KB ring at `+0x1b4e` and its cursor/latch at `+0x5b50`/`+0x5b54`.
	// The LINE is what a program can observe of this body, and it goes to the one channel this
	// runtime has. `DebugLogRingAdvance` above carries the cursor rule.
	if (Text == nullptr)
	{
		return;
	}
	EmitDevMsg(TEXT("0x1027ef20"), FString(Text));
}

void FElysiumNpcBase::AppendGlobalDebugLogLine(const TCHAR* Text) const
{
	// `0x1027ee20`, the GLOBAL trace ring slots 17 and 19 use. Seventy-nine bytes and the same shape
	// as `0x1027ef20` over a buffer that is not the NPC's. ABSENT for the same reason; story 29c-1's
	// slot-19 body (`ElysiumNpcKernelBaseHelpers.cpp`) already records it.
	if (Text == nullptr)
	{
		return;
	}
	EmitDevMsg(TEXT("0x1027ee20"), FString(Text));
}

void FElysiumNpcBase::DumpDebugLogRing() const
{
	// `0x1027efb0` — the dump `NPCThinkDebugPre`'s tail runs, walking the 16 KB buffer from the
	// `+0x5b50` cursor in 512-byte chunks and printing each. ABSENT: there is no ring to walk, and
	// the row's own verdict in band 0–4 is `mechanism → UE_LOG`, which is where every line the ring
	// would have held has already gone. Recorded so the arm is visible in a capture.
	EmitDevMsg(TEXT("0x1027efb0"), TEXT("0x1027efb0"));
}

int32 FElysiumNpcBase::DrawDebugTextOverlays()
{
	// `0x102767d0`, 2,872 bytes, arm by arm and in retail's order. Recovered from the LISTING: the
	// decompiler lost eleven of the sixteen format strings to the 0x408-byte stack frame and mislaid
	// the whole of the weapon line's argument order.
	//
	// **Four corrections to the checklist's walk, all from the listing.** (1) The `0x80000` arm emits
	// FOUR lines, not three: the first is `"Health: %i"` and the walk omits it, which is why the
	// index advances by four. (2) The squad format is `"Squad: %c : "` and carries NO `%s` — the
	// squad name is `strncat`ed after it, and the no-squad literal is `" - \n"` (`0x105cc8b4`), not
	// `"none"`. (3) The `0x1` arm opens with a SECOND `"Health: %i"` line, immediately after the
	// memory walk. (4) The activity line is `"Actv: %s (%s)\n"`, the same string the stat overlay
	// uses, and the velocity line is `"Vel %.1f …"` with no colon.
	//
	// The starting index is `CBaseCombatCharacter::DrawDebugTextOverlays()`'s answer; story 29c-1's
	// `EntityDrawDebugTextOverlays` is that seam (the chain from `CBaseCombatCharacter` down to
	// `CBaseEntity` adds nothing this runtime stands) and answers 0.
	int32 Line = EntityDrawDebugTextOverlays();

	// --- Arm 1, `0x80000` — health, squad, enemy, squad slot -------------------------------------
	if ((DebugOverlays & GDebug10BitSquad) != 0)
	{
		// 1. `Q_snprintf(buf, 512, "Health: %i", m_iHealth)`.
		EmitEntityText(Line, GDebug10FmtHealth, FString::Printf(GDebug10FmtHealth, Health));
		++Line;

		// 2. `Q_snprintf(buf, 512, "Squad: %c : ", ch)` where the character is chosen by
		//    `SETLE AL; DEC AL; AND AL,9; ADD AL,0x4f` over `m_iSquadDisconnected` — 'O' (0x4f) when
		//    it is at or below zero and 'X' (0x58) when it is above. Then `m_pSquad`'s name, or the
		//    literal `" - \n"` when there is no squad; the name arm appends `"\n"` after it.
		const TCHAR SquadMark = BaseScheduleHost.SquadDisconnected < 1 ? TEXT('O') : TEXT('X');
		FString SquadLine = FString::Printf(GDebug10FmtSquad, SquadMark);
		FString SquadNameText;
		if (!SquadObjectName(SquadNameText))
		{
			SquadLine += GDebug10SquadNone;
		}
		else
		{
			SquadLine += SquadNameText;
			SquadLine += NpcKernelDebug10Shared::GDebug10Newline;
		}
		EmitEntityText(Line, GDebug10FmtSquad, MoveTemp(SquadLine));
		++Line;

		// 3. `Q_strncpy(buf, "Enemy: ", 512)`, then the enemy's `m_iName` (`+0x26c`) when set else its
		//    `m_iClassname` (`+0x11c`), both with the empty string for a null pointer, then `"\n"`.
		//    With no enemy the SAME `" - \n"` block is written straight over the terminator.
		FString EnemyLine(GDebug10EnemyPrefix);
		const FElysiumEntity* Enemy = GetEnemy();
		if (Enemy == nullptr)
		{
			EnemyLine += GDebug10SquadNone;
		}
		else
		{
			EnemyLine += NpcKernelDebug10Shared::GDebug10DebugName(Enemy);
			EnemyLine += NpcKernelDebug10Shared::GDebug10Newline;
		}
		EmitEntityText(Line, GDebug10EnemyPrefix, MoveTemp(EnemyLine));
		++Line;

		// 4. `Q_snprintf(buf, 512, "Slot:  %s \n", SquadSlotName(m_iMySquadSlot))` — slot 546, family
		//    Squad's body, over `+0x5dac`.
		const TCHAR* const SlotName = SquadSlotName(MySquadSlot);
		EmitEntityText(Line, GDebug10FmtSlot,
			FString::Printf(GDebug10FmtSlot, SlotName != nullptr ? SlotName : TEXT("")));
		++Line;
	}

	// --- Everything below is under `m_debugOverlays & 1` ------------------------------------------
	if ((DebugOverlays & NpcKernelDebug10Shared::GDebug10BitText) == 0)
	{
		return Line;
	}

	// --- Arm 2, the enemy-memory walk -------------------------------------------------------------
	//
	//     for (mem = GetEnemies()->+0xc, i = 0; mem; mem = mem->+0x38, ++i)
	//         if (mem->+0x24 resolves)
	//             Q_snprintf(buf, 512, "MEM%02d: %s", i, GetDebugName(ent));
	//
	// The record ORDINAL is what `%02d` prints, and it advances for every record including the ones
	// whose handle no longer resolves — `INC EDI` sits outside the `if`. The line index advances only
	// for the printed ones. This runtime's `FElysiumNpcEnemyMemory` IS `CAI_Memory`'s list.
	{
		int32 RecordIndex = 0;
		for (const FElysiumNpcEnemyMemoryRecord& Record : EnemyMemory.Records())
		{
			const FElysiumEntity* Remembered =
				World != nullptr ? World->Resolve(Record.Handle) : nullptr;
			if (Remembered != nullptr)
			{
				EmitEntityText(Line, GDebug10FmtMem,
					FString::Printf(GDebug10FmtMem, RecordIndex, *NpcKernelDebug10Shared::GDebug10DebugName(Remembered)));
				++Line;
			}
			++RecordIndex;
		}
	}

	// --- Arm 3, the second health line ------------------------------------------------------------
	EmitEntityText(Line, GDebug10FmtHealth, FString::Printf(GDebug10FmtHealth, Health));
	++Line;

	// --- Arm 4, the weapon line -------------------------------------------------------------------
	//
	//     if (!GetActiveWeapon()) Q_snprintf(buf, 512, "UNARMED");
	//     else {
	//         secondary = wpn->m_iSecondaryAmmoType (+0x748) < 0 ? -1 : GetAmmoCount(that);
	//         primary   = wpn->m_iPrimaryAmmoType   (+0x744) < 0 ? -1 : GetAmmoCount(that);
	//         Q_snprintf(buf, 512, "Weapon: %s (%d/%d) (%d/%d)",
	//                    wpn->vtable[0x570](), wpn->m_iClip1 (+0x74c), primary,
	//                    wpn->m_iClip2 (+0x750), secondary);
	//     }
	//
	// The listing's push order is what settles the pairing: clip1 goes with the PRIMARY ammo count
	// and clip2 with the SECONDARY, and the two negative-type guards answer -1 without asking for a
	// count at all. The checklist's walk had the two indices the other way round.
	{
		FString WeaponName;
		int32 Clip1 = 0;
		int32 Ammo1 = 0;
		int32 Clip2 = 0;
		int32 Ammo2 = 0;
		if (!ActiveWeaponTextWords(WeaponName, Clip1, Ammo1, Clip2, Ammo2))
		{
			EmitEntityText(Line, GDebug10Unarmed, GDebug10Unarmed);
		}
		else
		{
			EmitEntityText(Line, GDebug10FmtWeapon,
				FString::Printf(GDebug10FmtWeapon, *WeaponName, Clip1, Ammo1, Clip2, Ammo2));
		}
		++Line;
	}

	// --- Arm 5, the state line, `"Stat: %s, "` with slot 406 over `m_NPCState` -------------------
	{
		const TCHAR* const StateName = GetStateName(GetMind().State());
		EmitEntityText(Line, GDebug10FmtStat,
			FString::Printf(GDebug10FmtStat, StateName != nullptr ? StateName : TEXT("")));
		++Line;
	}

	// --- Arm 6, the movement line, `"Move: %s, "` with slot 407 over `GetNavType()` ---------------
	EmitEntityText(Line, GDebug10FmtMove,
		FString::Printf(GDebug10FmtMove, GetNavTypeName(RetailNavType())));
	++Line;

	// --- Arm 7, the schedule block ----------------------------------------------------------------
	if (Schedule.IsRunning())
	{
		// `"Schd: %s, "` with the program's name at `m_pSchedule + 0x40`, or `"Unknown"` for a null
		// name pointer.
		const TCHAR* const ScheduleName = ElysiumScheduleName(Schedule.Current);
		EmitEntityText(Line, GDebug10FmtSchd, FString::Printf(GDebug10FmtSchd,
			ScheduleName != nullptr ? ScheduleName : GDebug10Unknown));
		++Line;

		const FElysiumScheduleProgram* const Program = ElysiumScheduleFor(Schedule.Current);
		if ((DebugOverlays & GDebug10BitTaskList) != 0)
		{
			// One line per task, re-reading `m_pSchedule->numTasks` at the top of every iteration:
			//
			//     prefix = (i == 0) ? "Task:" : "       "
			//     if (i == m_ScheduleState) { lead = "->"; trail = "<-"; } else { lead = "   "; trail = ""; }
			//     Q_snprintf(buf, 512, "%s%s%s%s", prefix, lead, TaskName(tasks[i].iTask), trail)
			//
			// The step is named through `ElysiumTaskName` rather than slot 449, for the reason story
			// 29c-1 states at the identical block in `0x102775e0`: the task namespace is a seam.
			if (Program != nullptr)
			{
				for (int32 Index = 0; Index < Program->Tasks.Num(); ++Index)
				{
					const TCHAR* const Prefix = Index == 0 ? GDebug10TaskFirst : GDebug10TaskRest;
					const bool bCurrent = Index == Schedule.TaskIndex;
					const TCHAR* const Lead = bCurrent ? GDebug10TaskLead : GDebug10TaskNoLead;
					const TCHAR* const Trail = bCurrent ? GDebug10TaskTrail : TEXT("");
					EmitEntityText(Line, GDebug10FmtTaskRow, FString::Printf(GDebug10FmtTaskRow,
						Prefix, Lead, *FElysiumScheduleCorpus::Get().TaskOps().NameOf(Program->Tasks[Index].TaskId), Trail));
					++Line;
				}
			}
		}
		else
		{
			// `GetCurTask()` (`0x1028a150`): `"Task: None"` for no task, else
			// `"Task: %s (#%d), "` with the task's name and `m_ScheduleState`.
			if (Program == nullptr || !Program->Tasks.IsValidIndex(Schedule.TaskIndex))
			{
				EmitEntityText(Line, GDebug10TaskNone, GDebug10TaskNone);
			}
			else
			{
				EmitEntityText(Line, GDebug10FmtTask, FString::Printf(GDebug10FmtTask,
					*FElysiumScheduleCorpus::Get().TaskOps().NameOf(Program->Tasks[Schedule.TaskIndex].TaskId), Schedule.TaskIndex));
			}
			++Line;
		}
	}

	// --- Arm 8, the activity line -----------------------------------------------------------------
	//
	//     if (m_Activity == -1 || m_IdealActivity == -1)
	//         -> m_Activity == 0 ? "Actv: RESET" : "Actv: INVALID"
	//     else if (m_Activity == 0) -> "Actv: RESET"
	//     else "Actv: %s (%s)\n" through slots 0x5dc / 0x5f4 / 0x5e0 then SelectWeightedSequence and
	//          GetSequenceActivityName for the current and the ideal.
	//
	// This line prints UNCONDITIONALLY — it is outside the schedule block, so an NPC with no program
	// still advances the index by one here.
	if (ActivityNumber == INDEX_NONE || IdealActivityNumber == INDEX_NONE)
	{
		const TCHAR* const Text = ActivityNumber == 0 ? GDebug10ActvReset : GDebug10ActvInvalid;
		EmitEntityText(Line, Text, Text);
	}
	else if (ActivityNumber == 0)
	{
		EmitEntityText(Line, GDebug10ActvReset, GDebug10ActvReset);
	}
	else
	{
		EmitEntityText(Line, GDebug10FmtActv, FString::Printf(GDebug10FmtActv,
			*ActivityNameForNumber(ActivityNumber), *ActivityNameForNumber(IdealActivityNumber)));
	}
	++Line;

	// --- Arm 9, the two scheduling-diagnostic lines -----------------------------------------------
	//
	//     if (m_interuptSchedule (+0x5f3c)) "Intr: %s (%s)\n" with its name and m_interruptText (+0x5f34)
	//     if (m_failedSchedule  (+0x5f38)) "Fail: %s (%s)\n" with its name and m_failText      (+0x5f30)
	//
	// The checklist called these "two conditional lines gated on two non-zero ints just past the
	// decompiler's view"; the listing names all four words and both strings. A null schedule NAME
	// prints `"Unknown"`; the text pointer is printed raw, so a null one reaches `printf` as a null
	// `%s` — which is the empty string here.
	if (BaseScheduleHost.InterruptSchedule != ElysiumScheduleId::None)
	{
		const TCHAR* const Name = ElysiumScheduleName(BaseScheduleHost.InterruptSchedule);
		EmitEntityText(Line, GDebug10FmtIntr, FString::Printf(GDebug10FmtIntr,
			Name != nullptr ? Name : GDebug10Unknown, *BaseScheduleHost.InterruptText));
		++Line;
	}
	if (BaseScheduleHost.FailedSchedule != ElysiumScheduleId::None)
	{
		const TCHAR* const Name = ElysiumScheduleName(BaseScheduleHost.FailedSchedule);
		EmitEntityText(Line, GDebug10FmtFail, FString::Printf(GDebug10FmtFail,
			Name != nullptr ? Name : GDebug10Unknown, *BaseScheduleHost.FailText));
		++Line;
	}

	// --- Arm 10, `COND_ENEMY_TOO_FAR` -------------------------------------------------------------
	//
	// Printed as a bare literal, with no `Q_snprintf` at all.
	if (Cognition.Conditions.Has(EElysiumNpcCond::EnemyTooFar))
	{
		EmitEntityText(Line, NpcKernelDebug10Shared::GDebug10EnemyTooFar, NpcKernelDebug10Shared::GDebug10EnemyTooFar);
		++Line;
	}

	// --- Arm 11, the velocity line ----------------------------------------------------------------
	//
	//     if (m_iEFlags & EFL_DIRTY_ABSVELOCITY) CalcAbsoluteVelocity();
	//     if (m_vecAbsVelocity != vec3_origin || m_vecAngVelocity != vec3_angle) {
	//         CalcAbsoluteVelocity();  CalcAbsoluteVelocity();  CalcAbsoluteVelocity();   // x3, again
	//         Q_snprintf(buf, 512, "Vel %.1f %.1f %.1f   Ang: %.1f %.1f %.1f\n", …);
	//     }
	//
	// `DAT_1070d1b0`/`DAT_1070d9d0` are `vec3_origin` and `vec3_angle`, both BSS zero vectors, so the
	// gate is "either velocity is non-zero". The four recompute calls are an artefact of the SDK body
	// this was cut down from — each of the six arguments re-asks under its own flag test — and the
	// port has no dirty-velocity flag, so they are recorded here and not made.
	{
		const FVector VelUnits = Velocity / ElysiumMove::U;
		const FVector& AngVel = AngularVelocity;
		if (!VelUnits.IsZero() || !AngVel.IsZero())
		{
			EmitEntityText(Line, GDebug10FmtVel, FString::Printf(GDebug10FmtVel,
				VelUnits.X, VelUnits.Y, VelUnits.Z, AngVel.X, AngVel.Y, AngVel.Z));
			++Line;
		}
	}

	return Line;
}

bool FElysiumNpcBase::ActiveWeaponTextWords(FString& OutName, int32& OutClip1, int32& OutAmmo1,
	int32& OutClip2, int32& OutAmmo2) const
{
	// SEAM for the weapon line's five words. `ActiveWeaponEntity()` is null, so this answers false
	// and the line is retail's `UNARMED`.
	OutName.Reset();
	OutClip1 = -1;
	OutAmmo1 = -1;
	OutClip2 = -1;
	OutAmmo2 = -1;
	return ActiveWeaponEntity() != nullptr;
}

bool FElysiumNpcBase::SquadObjectName(FString& OutName) const
{
	// `m_pSquad` (`+0x5da4`, ABSENT in the shape map) and its name at `+0x4`. Retail reads the squad
	// OBJECT here without the `m_iSquadDisconnected` gate `ConnectedSquad()` applies — the gate only
	// decides the `%c`. `CAI_BaseNPC::InitSquad` stands a squad object for any NPC whose
	// `m_SquadName` (`+0x5da8`) is set and the object's `+0x4` IS that name, so the port's own
	// `SquadName` is the recovered answer.
	if (SquadName.IsEmpty())
	{
		return false;
	}
	OutName = SquadName;
	return true;
}
