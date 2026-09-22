#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntity.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumStub.h"
#include "Substrate/ElysiumLaw.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"

// Story 29c-1, family **Schedule** — the schedule host and the task surface of `order.md`
// layers 0–9.
//
// 64 rows: the two species tables (slot 580 `GetClassScheduleIdSpace`, slot 452 `LoadedSchedules`),
// the schedule-change door `0x10280de0`, the task surface (`NextScheduledTask`, `TaskComplete`, the
// two `CAI_Motor` forwards, `IsTaskIndexCurrent`, `WaitFinished`), the two stubbed slots 418
// `ResolveTaskDistance` and 547 `GetSlotSchedule`, and the selectors — slot 604
// `SelectScheduleMeleeCombat` with its five species overrides, the slot-437/438 species overrides,
// the melee failure gate `0x102b6fe0` and the entrenched cover/kick selector `0x102b7690`. The
// walked prose is `docs/vtmb/npc-ai/schedule-kernel.md`.
//
// TWO STANDING FACTS OF THIS FAMILY.
//
//   1. **There is no per-class schedule id space.** Retail's `InitCustomSchedules` parses each
//      class's schedule text into its `CAI_ClassScheduleIdSpace` and records the parse in the flag
//      slot 452 answers; this runtime registers programs by identity and parses no text. Every
//      species row below carries the retail globals so the tables are checkable, and the
//      translation says what it cannot do rather than inventing a range.
//
//   2. **Most of what these selectors answer is not a registered program.** The species selectors
//      answer raw retail schedule numbers (0x156, 0x15a–0x165, 0x9b–0xa9, 0xe4, 0xe7, 0xe9 …), and
//      this runtime registers about thirty programs. Every body here therefore answers the RETAIL
//      NUMBER — the recovered deliverable — and the one caller that installs anything
//      (`FElysiumNpc::SelectSchedule`) maps it through the registry and tallies the miss instead of
//      installing some other program under a recovered number.

namespace
{
	// ---------------------------------------------------------------------------------------------
	// Retail data constants. Everything whose value IS in the corpus is named; everything whose
	// value is a bare `.rdata` float the corpus does not carry says UNRECOVERED and is named so one
	// edit closes it.
	// ---------------------------------------------------------------------------------------------

	// `0x3b9aca00` — the "already a global id" boundary `0x10280de0` compares against. It is the same
	// 1,000,000,000 family Squad found the two squad-slot symbols registered under.
	constexpr int32 GScheduleGlobalIdBase = 1000000000;

	// `CAI_LocalIdSpace`'s "this space holds no ids" sentinel, tested by name in `0x102ea2d0`.
	constexpr int32 GScheduleEmptyIdSpace = 9999;

	// `_DAT_104454c4` — the shared float zero, 1,328 readers.
	constexpr float GScheduleZero = 0.0f;

	// `_DAT_1044e664` — 10.0f, recovered by story 16a as the follower-distance overlap
	// (`Substrate/ElysiumNpcKernelSquad.cpp`). `ResolveTaskDistance`'s fourth table entry and the
	// tentacle's phase-2 duration both read it.
	constexpr float GScheduleFollowerOverlap = 10.0f;

	// `_DAT_104492b8` — 200.0f, the same constant `ElysiumFootsteps.h` names as the fall-punch
	// threshold. `CNPC_VChangBros` / `CNPC_VTzimisceRunner` add it to the melee-range convar.
	constexpr float GScheduleChangMeleeMargin = 200.0f;

	// `DAT_10924a1c` — the melee-range convar every melee selector thresholds on. Retail reads its
	// bool through the ConVar vtable (`+0x04`) and substitutes `0.0` when it is SET, otherwise reads
	// the float at `+0x28`.
	//
	// UNRECOVERED: neither the convar's name nor its default is in the corpus. Answering 0.0 is the
	// arm a SET bool takes, which is why it is the stand-in rather than an invented range — with a
	// range of zero every `m_flEnemyDist <= range` test reads false and every selector takes its
	// far arm. One edit closes it.
	constexpr float GScheduleMeleeRangeUnits = 0.0f;

	// `_DAT_10451acc` — the height-difference threshold the melee height-diff timer arms above.
	//
	// **RECOVERED as 64.0f by story 29d** and no longer the 0.0 stand-in this line carried. The cell
	// is at file offset `0x451acc` of the pinned `vampire.dll` (`.rdata` is identity-mapped off
	// image base `0x10000000`) and reads `00 00 80 42`. Three of 29d's families reached it
	// independently from three different bodies — family Combat10 as the yaw sweep's right leg
	// (`0x102a1650`, paired with `_DAT_10462950` = 40.0), family Debug10 as the witness-box clamp in
	// `0x10292500`, and this one — and the reviewer read the bytes back a fourth time before the
	// constant was changed, because it is a live threshold rather than a comment.
	//
	// It matters: at 0.0 only an enemy exactly level or below disarmed the timer, so nearly every
	// melee selector armed it. At retail's 64.0 an enemy within 64 units of this NPC's own height
	// counts as level and the timer is held at -1.0 instead.
	constexpr float GScheduleMeleeHeightDiffUnits = 64.0f;

	// `_DAT_104c3cd4` — `CNPC_VSabbatLeader`'s `TOO_FAR_TO_ATTACK` distance bound. UNRECOVERED.
	constexpr float GScheduleSabbatTooFarUnits = 0.0f;

	// `_DAT_1044ddb0` — `CNPC_VMingXiaoTentacle`'s enemy-distance split. UNRECOVERED.
	constexpr float GScheduleTentacleEnemyDistUnits = 0.0f;

	// `_DAT_10450aa0`, `_DAT_10449270`, `_DAT_104bea38` — the tentacle's phase-0/3, phase-1 and
	// phase-3 expire durations. Phase 2's is `_DAT_1044e664`, which IS recovered (10.0).
	// UNRECOVERED.
	constexpr float GScheduleTentaclePhase0Seconds = 0.0f;
	constexpr float GScheduleTentaclePhase1Seconds = 0.0f;
	constexpr float GScheduleTentaclePhase3Seconds = 0.0f;

	// `_DAT_10457f60` — `CNPC_VTzimisce`'s answer for task distance sentinel -1000001. UNRECOVERED.
	constexpr float GScheduleTzimisceTaskDistance = 0.0f;

	// The melee height-diff timer's "unarmed" value, `0xbf800000` = -1.0f, written as an absolute
	// curtime in retail and carried as a double here.
	constexpr double GScheduleMeleeTimerUnarmed = -1.0;

	// The three hint types the cover selector's table splits on, the same three family Hints found
	// on the five activity lookups.
	constexpr int32 GScheduleHintTypeCoverLow = 100;
	constexpr int32 GScheduleHintTypeCoverMed = 0x65;
	constexpr int32 GScheduleHintTypeLean = 0x27d8;
	constexpr int32 GScheduleHintTypeShootAtA = 0x283c;
	constexpr int32 GScheduleHintTypeShootAtB = 0x283d;

	// Slot 308 `HasUsableRangedWeapon`, the split every melee selector turns on. The generated slot
	// is a stub answering false; the port already carries the fact through the item catalogue, and
	// the brief's rule is to read the port's member rather than a stub, so this is the real answer
	// and slot 308 is named beside it.
	bool HasUsableRangedWeaponPort(const FElysiumNpc& Npc)
	{
		return ElysiumNpcCond::WeaponCapability(Npc) == ElysiumNpcCond::ECapability::Ranged;
	}

	// `flags2 &= 0x7ffffeff` — the mask `0x102b7690` clears on four of its arms. Its complement is
	// `0x80000100`: the unnamed bit 31 and `COVER_VS_MELEE_MODE`.
	constexpr uint32 GScheduleCoverVsMeleeClearMask =
		0x80000000u | static_cast<uint32>(EElysiumNpcFlag2::COVER_VS_MELEE_MODE);

	// `(wpn->slot360() & 0x6000)` — the weapon-capability bits the cover selector reads off the
	// enemy's active weapon to decide whether it faces a ranged threat.
	constexpr uint32 GScheduleRangedThreatWeaponBits = 0x6000;

}

// -------------------------------------------------------------------------------------------------
// Slot 580 `GetClassScheduleIdSpace` — one method and a species table.
// -------------------------------------------------------------------------------------------------
//
// All fourteen bodies are one line: `return &DAT_<class schedule id space>;`. The table below is the
// twelve distinct ones this story's rows cover (`CNPC_VCamera` is shared with
// `CNPC_VCameraSecurity` and `CNPC_VVampire` with `CNPC_VPlayerController`) plus the Troika line's
// own `0x101aa790` → `DAT_10924248`.
//
// The ranges are `0x102ea090(isRoot = false)`'s leftovers, exactly as family Squad found for the
// squadslot spaces — but for a DIFFERENT reason, and the difference is the recovery. Retail's
// species schedule spaces really are filled: `CNPC_VBrujah::InitCustomSchedules` (`0x10367a40`)
// calls `CAI_LocalIdSpace::Init` (`0x102ea0e0`) on `DAT_1093a740` with the schedule namespace
// `0x109203cc` and the parent `DAT_1093d258` (`CNPC_VVampire`'s), then registers
// `SCHED_VBRUJAH_WALK` 0x158 and `SCHED_VBRUJAH_WATCH` 0x159 through `0x102ea130`. This runtime
// runs no such registration, so the rows keep the empty range and `ScheduleLocalToGlobal` says so.

namespace
{
	constexpr FElysiumNpc::FScheduleIdSpace GNpcKernelScheduleIdSpaces[] = {
		// The Troika line itself.
		{ TEXT("CAI_BaseNPCTroika"), TEXT("0x101aa790"), TEXT("0x10924248") },
		{ TEXT("CNPC_VBrujah"), TEXT("0x10367870"), TEXT("0x1093a740") },
		{ TEXT("CNPC_VCamera"), TEXT("0x103683b0"), TEXT("0x1093a7d8") },
		{ TEXT("CNPC_VCameraSecurity"), TEXT("0x103683b0"), TEXT("0x1093a7d8") },
		{ TEXT("CNPC_VChangBros"), TEXT("0x1036a1b0"), TEXT("0x1093a888") },
		{ TEXT("CNPC_VChangBrosBlade"), TEXT("0x1036eab0"), TEXT("0x1093a8f0") },
		{ TEXT("CNPC_VChangBrosClaw"), TEXT("0x1036f2b0"), TEXT("0x1093a938") },
		{ TEXT("CNPC_VCombatman"), TEXT("0x1036fb10"), TEXT("0x1093ab68") },
		{ TEXT("CNPC_VCop"), TEXT("0x10370930"), TEXT("0x1093ac60") },
		{ TEXT("CNPC_VDog"), TEXT("0x10373530"), TEXT("0x1093acd8") },
		{ TEXT("CNPC_VFrenzyShadow"), TEXT("0x10375240"), TEXT("0x1093ae48") },
		{ TEXT("CNPC_VGangrel"), TEXT("0x10377070"), TEXT("0x1093aef8") },
		{ TEXT("CNPC_VGargoyle"), TEXT("0x10377b20"), TEXT("0x1093aff0") },
		{ TEXT("CNPC_VPlayerController"), TEXT("0x103750e0"), TEXT("0x1093d258") },
		{ TEXT("CNPC_VVampire"), TEXT("0x103750e0"), TEXT("0x1093d258") },
	};
}

const FElysiumNpc::FScheduleIdSpace* FElysiumNpc::ScheduleIdSpaceRows(int32& OutCount)
{
	OutCount = UE_ARRAY_COUNT(GNpcKernelScheduleIdSpaces);
	return GNpcKernelScheduleIdSpaces;
}

const FElysiumNpc::FScheduleIdSpace* FElysiumNpc::ScheduleIdSpaceOf(const TCHAR* InRetailClass)
{
	if (InRetailClass == nullptr)
	{
		return nullptr;
	}
	for (const FScheduleIdSpace& Row : GNpcKernelScheduleIdSpaces)
	{
		if (FCString::Strcmp(Row.RetailClass, InRetailClass) == 0)
		{
			return &Row;
		}
	}
	return nullptr;
}

// slot 580, the species half of 0x101aa790
const FElysiumLocalIdSpace* FElysiumNpc::ClassScheduleIdSpace() const
{
	// The table above is documentation now -- the retail class, the slot-580 body and the id space
	// that body returns, all checkable against `docs/vtmb/npc-kernel/slots.md`. The LIVE space is
	// the corpus's, keyed on this NPC's own retail class rather than on the slot-580 override row,
	// because the sidecar already folds the classes that share one space.
	return IdSpace(EElysiumIdCategory::Schedule);
}

int32 FElysiumNpc::ScheduleLocalToGlobal(const FElysiumLocalIdSpace* Space, int32 LocalId)
{
	// 0x102ea2d0, arm for arm:
	//
	//   if (id == -1) return -1;
	//   do {
	//     if (localBase != 9999 && localBase <= id && id <= localTop)
	//       return (globalBase - localBase) + id;
	//     space = space->parent;   // +0x10
	//   } while (space);
	//   return -1;
	//
	// The seam is CLOSED: the parent chain and every range are the corpus's, filled by the
	// registration pass that runs each class's `InitCustomSchedules` recipe. The arms themselves
	// live on `FElysiumLocalIdSpace`, so this body and the global->local direction cannot drift.
	return Space != nullptr ? Space->LocalToGlobal(LocalId) : INDEX_NONE;
}

// -------------------------------------------------------------------------------------------------
// Slot 452 `LoadedSchedules` — the stubbed slot, plus its species table.
// -------------------------------------------------------------------------------------------------

namespace
{
	constexpr FElysiumNpc::FScheduleLoadFlag GNpcKernelScheduleLoadFlags[] = {
		// The Troika line's own override. `CAI_BaseNPC::LoadedSchedules` (`0x1027c2e0`) is the
		// literal `true` this one replaces with a global.
		{ TEXT("CAI_BaseNPCTroika"), TEXT("0x102b97f0"), TEXT("0x105d1058") },
		{ TEXT("CNPC_VBrujah"), TEXT("0x103679b0"), TEXT("0x1062f278") },
		{ TEXT("CNPC_VCamera"), TEXT("0x103684f0"), TEXT("0x1062f668") },
		{ TEXT("CNPC_VCameraSecurity"), TEXT("0x103684f0"), TEXT("0x1062f668") },
		{ TEXT("CNPC_VChangBros"), TEXT("0x1036a390"), TEXT("0x1062fe14") },
		{ TEXT("CNPC_VChangBrosBlade"), TEXT("0x1036ec90"), TEXT("0x1062fe38") },
		{ TEXT("CNPC_VChangBrosClaw"), TEXT("0x1036f490"), TEXT("0x1062fe5c") },
		{ TEXT("CNPC_VCombatman"), TEXT("0x1036fc50"), TEXT("0x10631678") },
		{ TEXT("CNPC_VCop"), TEXT("0x10370a70"), TEXT("0x10631ba8") },
		{ TEXT("CNPC_VDog"), TEXT("0x10373670"), TEXT("0x106368a8") },
		{ TEXT("CNPC_VFrenzyShadow"), TEXT("0x103753e0"), TEXT("0x10637b44") },
		{ TEXT("CNPC_VGangrel"), TEXT("0x103771b0"), TEXT("0x10639090") },
		{ TEXT("CNPC_VGargoyle"), TEXT("0x10377c70"), TEXT("0x106395c0") },
	};
}

const FElysiumNpc::FScheduleLoadFlag* FElysiumNpc::LoadedSchedulesRows(int32& OutCount)
{
	OutCount = UE_ARRAY_COUNT(GNpcKernelScheduleLoadFlags);
	return GNpcKernelScheduleLoadFlags;
}

const FElysiumNpc::FScheduleLoadFlag* FElysiumNpc::LoadedSchedulesRowOf(const TCHAR* InRetailClass)
{
	if (InRetailClass == nullptr)
	{
		return nullptr;
	}
	for (const FScheduleLoadFlag& Row : GNpcKernelScheduleLoadFlags)
	{
		if (FCString::Strcmp(Row.RetailClass, InRetailClass) == 0)
		{
			return &Row;
		}
	}
	return nullptr;
}

// slot 452 0x102b97f0 `bool LoadedSchedules()`, with the twelve species bodies
bool FElysiumNpc::LoadedSchedules()
{
	// Every one of the thirteen bodies is `return <global bool>;`. Each of those globals is written
	// in exactly ONE place — the owning class's `InitCustomSchedules` parse loop, which seeds itself
	// from the flag (`uVar2 = DAT_1062f278`), breaks on the first parse failure and stores the
	// parser's answer back — so the shipped value is `true` and only a malformed schedule text
	// clears it. `CAI_BaseNPC::Precache` (`0x1027bb50`) is the reader: a false return is
	// `"ERROR: Rejecting spawn of %s as error in NPC's schedules"`.
	//
	// The seam is CLOSED: this runtime parses the class's texts and records the parse, so the answer
	// is the real one. It is `true` for all 56 loaded spaces today -- every shipped text parses --
	// and it goes false the moment one does not, which is exactly what retail's `Precache` refusal
	// (`"ERROR: Rejecting spawn of %s as error in NPC's schedules"`) reads.
	const FElysiumScheduleSpaceUnit* Unit =
		FElysiumScheduleCorpus::Get().UnitForClass(RetailClass() != nullptr ? FString(RetailClass()->Name) : FString());
	return Unit == nullptr || Unit->bAllTextsLoaded;
}

// -------------------------------------------------------------------------------------------------
// The schedule-change door.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpc::ResolveIdealScheduleStamp(int32 RawRetailId) const
{
	// 0x10280de0, first half, from the listing:
	//
	//   CMP EDI, 0x3b9aca00 / JL translate / CMP EDI, -1 / JNZ keep
	//
	// so an id at or above 1,000,000,000 that is not -1 is stamped unchanged, and everything else —
	// including -1 — goes through slot 580's id space.
	if (RawRetailId >= GScheduleGlobalIdBase && RawRetailId != INDEX_NONE)
	{
		return RawRetailId;
	}
	return ScheduleLocalToGlobal(ClassScheduleIdSpace(), RawRetailId);
}

// 0x10280de0 `CAI_BaseNPC::SetSchedule(int)`, and the body slot 619's five species overrides forward
// to unchanged
void FElysiumNpc::ChangeSchedule(int32 Id)
{
	// The raw number retail passes. Every program this runtime registers carries retail's own
	// registered number and every one of them is below 1,000,000,000, so the translate arm is the
	// one taken — which is exactly what retail does for an id named in a schedule table.
	const int32 RawRetailId = Id;
	const int32 Stamp = ResolveIdealScheduleStamp(RawRetailId);

	// `m_IdealSchedule = <stamp>` (`+0x5c3c`) keeps the raw int32 exactly, including -1 and the
	// global >=1e9 namespace that the typed installed-program enum cannot spell.
	ScheduleHost.IdealScheduleRetail = Stamp;
	if (Stamp == INDEX_NONE)
	{
		// SEAM, stated once per call rather than swallowed: with no class schedule id space the
		// translation answers -1 and the stamp is empty. Retail would have stamped the global id
		// the class registered.
		RecordScheduleEvent(FString::Printf(
			TEXT("ChangeSchedule(%s): m_IdealSchedule left empty — no class schedule id space "
				"(0x10280de0 / 0x102ea2d0)"),
			ElysiumScheduleName(Id)));
	}

	// Then `SetSchedule(int)` (`0x102cc1f0`: slot 440 `TranslateSchedule`, slot 446
	// `GetScheduleOfType`, the `"GetScheduleOfType(): No CASE for %d"` miss arm installing base 1)
	// and `CAI_BaseNPC::SetSchedule(CAI_Schedule*)` (`0x10280e50`). Both halves are
	// `ElysiumSchedule::Start`, which already carries the translate, the miss trace and the
	// condition clear.
	ElysiumSchedule::Start(Schedule, Id, *this);
}

// slot 586 0x101a95d0 `CCineAI::FixScriptNPCSchedule`
void FElysiumNpc::FixScriptNPCSchedule(int32 FinishSchedule)
{
	// `m_iFinishSchedule` is the DIRECTOR's word (`CCineAI +0x5f64`), passed in rather than read off
	// this NPC. Retail's arms, in order:
	if (FinishSchedule != 0)
	{
		if (FinishSchedule == 1)
		{
			// `thunk_FUN_10280de0(npc, 0x2a)` — `SCHED_AISCRIPT`, installed with NO clear.
			//
			// SEAM: 0x2a is not a registered program here. The scripted family this runtime carries
			// is `ScriptedMoveToGoal` / `ScriptedFollowPath`, which the director pushes directly, so
			// naming one of them here would be a different program under a recovered number.
			ElysiumStub::Fired(TEXT("schedule"),
				TEXT("CCineAI::FixScriptNPCSchedule SCHED_AISCRIPT 0x2a"), DebugString(),
				TEXT("m_iFinishSchedule=1"), TEXT("0002/0003: SCHED_AISCRIPT is not registered"));
			return;
		}
		UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s FixScriptNPCSchedule - no case!"), *DebugString());
	}
	// Both the 0 arm and the no-case arm fall through to the clear.
	ClearSchedule();
}

// 0x103a9d00 `CNPC_VSabbatLeader::FlipFailureType`
void FElysiumNpc::FlipFailureType()
{
	// The whole body inside the scope-trace push/pop. The trace stack is retail's debug aid and has
	// no port counterpart; the mind's transition trace carries the same account.
	FailureType = 1 - FailureType;
}

// 0x102ae840 — the scripted-schedule order push
void FElysiumNpc::AcceptScriptedScheduleOrder(int32 OrderId, bool bForce)
{
	// `if (m_NPCState != 7 && m_IdealNPCState != 7)` — retail state 7 is dead.
	if (Mind.State() == EElysiumNpcState::Dead || Mind.IdealState() == EElysiumNpcState::Dead)
	{
		return;
	}
	// `if (IsAlive() || param_2)` — slot 158. Its generated stub answers false; the port carries the
	// fact on the entity, so `!IsDead()` is the answer and the slot is named beside it.
	if (IsDead() && !bForce)
	{
		return;
	}
	// `+0x65cc = param_1` — the port's `+0x65cc` is the whole pushed order (`the forced state
	// travels with the pushed order`), so the id is recorded on it rather than beside it.
	ScriptedScheduleOrder.RetailOrderId = OrderId;
	// `m_bForceStateChange (+0x1b28) = 1`.
	Mind.ForceStateChange();
	// `m_bfAINPCFlags2 |= 0x82000000` — `CHOOSE_NEW_SCHEDULE` and the unnamed bit 31.
	NpcFlags.Set(EElysiumNpcFlag2::CHOOSE_NEW_SCHEDULE);
	NpcFlags.SetRawWord2Bits(FElysiumNpcFlags::Word2UnnamedBit31);
}

// -------------------------------------------------------------------------------------------------
// The task surface.
// -------------------------------------------------------------------------------------------------

// 0x10280f40 `NextScheduledTask`
void FElysiumNpc::NextScheduledTask()
{
	// `m_ScheduleState.fTaskStatus (+0x5c44) = 0` (TASKSTATUS_NEW) and `m_iScheduleIndex (+0x5c40)
	// += 1`, in that order.
	Schedule.TaskStatus = EElysiumTaskStatus::New;
	++Schedule.TaskIndex;
	// `if (IsTaskIndexCurrent())` — `0x10280db0`, which is "the index reached the program's task
	// count", i.e. the program is exhausted.
	if (FElysiumNpcScheduleHost::IsTaskIndexCurrent(Schedule))
	{
		// `m_failedSchedule (+0x5f38) = m_interuptSchedule (+0x5f3c) = 0`. The listing's `EDX` is
		// zeroed at `0x10280f43` and never rewritten, so both stores are literal zero.
		ScheduleHost.FailedSchedule = ElysiumScheduleId::None;
		ScheduleHost.InterruptSchedule = ElysiumScheduleId::None;
		// `(*DAT_10924a6c + 4)()` — a global object's slot 1, taking no argument and discarding its
		// answer. UNRECOVERED: the object is not identified and the call has no observable here.
		// `SetCondition(COND_SCHEDULE_DONE 0x5d)`.
		Cognition.Conditions.Set(EElysiumNpcCond::ScheduleDone);
	}
}

// 0x10273e80 `CAI_BaseNPC::TaskComplete(bool)`
void FElysiumNpc::TaskComplete(bool bIgnoreTaskFailed)
{
	// `if (!ignore && HasCondition(COND_TASK_FAILED 0x5c)) return;` — a completion cannot overwrite
	// an already-raised failure. Then `fTaskStatus (+0x5c44) = 4` (TASKSTATUS_COMPLETE).
	if (!bIgnoreTaskFailed && Cognition.Conditions.Has(EElysiumNpcCond::TaskFailed))
	{
		return;
	}
	Schedule.TaskStatus = EElysiumTaskStatus::Complete;
}

// 0x102623c0 `CAI_Motor` slot 2
void FElysiumNpc::MotorTaskComplete(bool bIgnoreTaskFailed)
{
	// `thunk_FUN_10273e80(this->m_pOuter (+0x4), param)`. The motor is a sub-object of the NPC here,
	// so the back-pointer hop is the identity.
	TaskComplete(bIgnoreTaskFailed);
}

// 0x102623a0 `CAI_Motor` slot 1
void FElysiumNpc::MotorTaskFail(int32 Reason)
{
	// `MOV ECX,[ECX+4]; JMP [[ECX]+0x700]` — the owner's slot 448, unchanged. `FElysiumNpc::TaskFail`
	// IS slot 448 (`0x1029adb0`), ported in story 13; family Motor's `ValidateNavGoal` already calls
	// it with `0x1b`. This adds the motor's door and nothing else.
	TaskFail(Reason);
}

// 0x1027db30 — the navigator node-index guard (NOT `StartTaskByIndex`; see the declaration)
bool FElysiumNpc::IsUnusableNodeIndex(int32 NodeIndex) const
{
	// EAX = m_pNavigator (+0x5d34); EAX = [EAX + 0x2c] — the node list, count at +0x00, array at
	// +0x04. A negative index, or one at or past the count, increments `DAT_0x106c994c` and answers
	// false; a null node answers false without touching the counter; otherwise slot 527.
	//
	// SEAM: the port's motor carries no node list, so the count is zero and every index is out of
	// range. The error counter is a global diagnostic with no port home, so the out-of-range arm is
	// tallied instead.
	ElysiumStub::Fired(TEXT("navigator"), TEXT("CAI_BaseNPC::IsUnusableNodeIndex 0x1027db30"),
		DebugString(), FString::FromInt(NodeIndex),
		TEXT("0002: no node list on the motor; retail bumps 0x106c994c"));
	return false;
}

// -------------------------------------------------------------------------------------------------
// slot 418 `ResolveTaskDistance` — the stubbed slot, plus two species overrides.
// -------------------------------------------------------------------------------------------------

// slot 418 0x102bf6e0 `float ResolveTaskDistance(float)`
float FElysiumNpc::ResolveTaskDistance(float Distance)
{
	// The species overrides first, because both of them test their own sentinel and only then
	// delegate to the Troika-line body below.
	const FElysiumNpcClassSlot* Override = ElysiumNpcKernelClass::OverrideOf(RetailClass(), 418);
	const TCHAR* SlotBody = Override != nullptr ? Override->Address : nullptr;
	const int32 Truncated = static_cast<int32>(Distance);   // retail's `__ftol`

	if (SlotBody != nullptr && FCString::Strcmp(SlotBody, TEXT("0x10392a10")) == 0)
	{
		// CNPC_VMingXiao: `if ((int)param != -1000004) return base(param); else return
		// m_flIdealRange (+0x6748);`
		if (Truncated == -1000004)
		{
			// SEAM: `m_flIdealRange` is a SPECIES word above `+0x665c` with no port member and no
			// producer; family Squad declares the four species words its bodies read and this is
			// not one of them.
			ElysiumStub::Fired(TEXT("species"),
				TEXT("CNPC_VMingXiao::ResolveTaskDistance m_flIdealRange +0x6748"), DebugString(),
				TEXT("-1000004"), TEXT("0002/29c-1: no MingXiao ideal range"));
			return 0.0f;
		}
	}
	else if (SlotBody != nullptr && FCString::Strcmp(SlotBody, TEXT("0x103b9120")) == 0)
	{
		// CNPC_VTzimisce: `if ((int)param != -1000001) return base(param); else return
		// _DAT_10457f60;`
		if (Truncated == -1000001)
		{
			return GScheduleTzimisceTaskDistance;
		}
	}

	// 0x102bf6e0, the Troika line: a four-entry jump table on `(int)param + 1000008`.
	switch (Truncated + 1000008)
	{
	case 0:   // -1000008
		return FollowerDistanceBackAway;
	case 1:   // -1000007
		return FollowerDistanceWalkTo;
	case 2:   // -1000006
		return FollowerDistanceRunTo;
	case 3:   // -1000005
		return GScheduleFollowerOverlap;
	default:
		break;
	}
	// `CAI_BaseNPC::ResolveTaskDistance` (`0x102702d0`), layer 0 and NOT one of this story's rows:
	// it splits -1000003 (through a global's slot 1), -1000002 and -1000000 and otherwise answers
	// the truncated value. Only the pass-through arm is reproduced; the three sentinels are named.
	if (Truncated == -1000003 || Truncated == -1000002 || Truncated == -1000000)
	{
		ElysiumStub::Fired(TEXT("schedule"), TEXT("CAI_BaseNPC::ResolveTaskDistance 0x102702d0"),
			DebugString(), FString::FromInt(Truncated), TEXT("0002/29c: the base sentinel arms"));
	}
	return static_cast<float>(Truncated);
}

// -------------------------------------------------------------------------------------------------
// slot 547 `GetSlotSchedule` — the stubbed slot.
// -------------------------------------------------------------------------------------------------

// slot 547 0x1028b0f0 `int GetSlotSchedule(int)`
int32 FElysiumNpc::GetSlotSchedule(int32 SquadSlot)
{
	// The whole retail body: `DevMsg("ERROR: Subclass missing GetSlotSchedule()!\n"); return 0;`.
	// No class in the family overrides it, so this warning IS the behaviour, and family Squad's
	// recovery says why nothing ever reaches it usefully — every class registers zero squad slots.
	(void)SquadSlot;
	UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s ERROR: Subclass missing GetSlotSchedule()!"),
		*DebugString());
	return 0;
}

// -------------------------------------------------------------------------------------------------
// The pre-selector.
// -------------------------------------------------------------------------------------------------

// 0x1028a2a0 `CAI_BaseNPC::PreSelectSchedule` (slot 437 for the base classes)
int32 FElysiumNpc::BasePreSelectSchedule()
{
	// `field_0x1b2c = 1` — retail's selector trace, ELYSIUM_NPC_WORD_ABSENT: "this runtime records
	// selections in the mind's transition trace". The tag is recorded there rather than dropped.
	RecordScheduleEvent(TEXT("PreSelectSchedule trace 1 (CAI_BaseNPC 0x1028a2a0)"));

	// Arm 1, and it is NOT a return: floating off the ground restores gravity and clears the ground
	// entity, then falls through to the three tests.
	if (Cognition.Conditions.Has(EElysiumNpcCond::FloatingOffGround))
	{
		Gravity = 1.0f;
		SetGroundEntity(nullptr);   // slot 208
	}
	// Retail's order, corrected by `docs/vtmb/npc-ai/conditions-and-states.md` § "The idle branch,
	// decided": NPC_FREEZE, then ON_FIRE, then FLOATING_OFF_GROUND again.
	if (Cognition.Conditions.Has(EElysiumNpcCond::NpcFreeze))
	{
		RecordScheduleEvent(TEXT("PreSelectSchedule AI_BaseNPC.cpp:3627 -> 0x3a NPC_FREEZE"));
		return 0x3a;
	}
	if (Cognition.Conditions.Has(EElysiumNpcCond::OnFire))
	{
		RecordScheduleEvent(TEXT("PreSelectSchedule AI_BaseNPC.cpp:3634 -> 0x151"));
		return 0x151;
	}
	if (Cognition.Conditions.Has(EElysiumNpcCond::FloatingOffGround))
	{
		RecordScheduleEvent(TEXT("PreSelectSchedule AI_BaseNPC.cpp:3639 -> 0x3e FALL_TO_GROUND"));
		return 0x3e;
	}
	return 0;
}

// The three species overrides of slot 437 this story carries
int32 FElysiumNpc::SpeciesPreSelectSchedule()
{
	const FElysiumNpcClassSlot* Override = ElysiumNpcKernelClass::OverrideOf(RetailClass(), 437);
	if (Override == nullptr)
	{
		return 0;
	}
	if (FCString::Strcmp(Override->Address, TEXT("0x10368f20")) == 0)
	{
		// CNPC_VCamera / CNPC_VCameraSecurity: `field_0x1b2c = 9; return 0x156;`
		RecordScheduleEvent(TEXT("PreSelectSchedule trace 9 (CNPC_VCamera 0x10368f20) -> 0x156"));
		return 0x156;
	}
	if (FCString::Strcmp(Override->Address, TEXT("0x1039de00")) == 0)
	{
		// CNPC_VMingXiaoTentacle: `field_0x1b2c = 0x1a; return 0;`
		RecordScheduleEvent(
			TEXT("PreSelectSchedule trace 0x1a (CNPC_VMingXiaoTentacle 0x1039de00) -> 0"));
		return 0;
	}
	if (FCString::Strcmp(Override->Address, TEXT("0x103a43f0")) == 0)
	{
		// CNPC_VPlaceholder: `field_0x1b2c = 0x1e; return 0x157;`
		RecordScheduleEvent(
			TEXT("PreSelectSchedule trace 0x1e (CNPC_VPlaceholder 0x103a43f0) -> 0x157"));
		return 0x157;
	}
	// Every other slot-437 override is `CAI_BaseNPCTroika`'s `0x102ae920`, story 29e's.
	return 0;
}

// -------------------------------------------------------------------------------------------------
// The species half of slot 438 `SelectSchedule`.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpc::SpeciesSelectSchedule()
{
	const FElysiumNpcClassSlot* Override = ElysiumNpcKernelClass::OverrideOf(RetailClass(), 438);
	if (Override == nullptr)
	{
		return 0;
	}
	const TCHAR* SlotBody = Override->Address;

	if (FCString::Strcmp(SlotBody, TEXT("0x1035d010")) == 0)
	{
		// CNPC_VAndreiBlood, a strict priority ladder under `field_0x1b2c = 4`.
		RecordScheduleEvent(TEXT("SelectSchedule trace 4 (CNPC_VAndreiBlood 0x1035d010)"));
		// SEAM: `m_bActivated`, `m_bDead`, `m_bForceTeleport`, `m_iHitCounter` and `m_iHitMax` are
		// SPECIES words above `+0x665c` with no port member. The ladder is written out and the first
		// gate answers "activated" so the recovered order is visible; `AndreiBloodSelectGate`
		// carries the `0x1035e920` split and refuses.
		if (AndreiBloodSelectGate())
		{
			return 0x15d;
		}
		return 0x15c;
	}
	if (FCString::Strcmp(SlotBody, TEXT("0x10368f40")) == 0)
	{
		// CNPC_VCamera / CNPC_VCameraSecurity: `field_0x1b2c = 9; return 0x156;` — the same hardcode
		// as its slot-437 body, so a camera never reaches the state switch at all.
		RecordScheduleEvent(TEXT("SelectSchedule trace 9 (CNPC_VCamera 0x10368f40) -> 0x156"));
		return 0x156;
	}
	if (FCString::Strcmp(SlotBody, TEXT("0x103a4410")) == 0)
	{
		// CNPC_VPlaceholder: `field_0x1b2c = 0x1e; return 0x157;`
		RecordScheduleEvent(TEXT("SelectSchedule trace 0x1e (CNPC_VPlaceholder 0x103a4410) -> 0x157"));
		return 0x157;
	}
	if (FCString::Strcmp(SlotBody, TEXT("0x1038e340")) == 0)
	{
		// CNPC_VManBat. `GetGoalType`-shaped navigator probe first; anything but 2 writes
		// `m_iMoveGoalNodeID = 1` and answers 0x158.
		if (NavigatorGoalType() != 2)
		{
			MoveGoalNodeId = 1;
			return 0x158;
		}
		// The obfuscated equality: `Hash((f & 0x710935 ^ 0x148739) + 0x4094ab & 0x18ef6ca ^ f ^
		// 0x412a96ec) == Hash(0xfa0b0694)`, where `f` is `field_0x6670`, a species word with no port
		// member, and `Hash` is `0x1042fbf0`. Reproduced as the arithmetic it is, over a zero word;
		// the comparison therefore fails, which is the `m_iMoveGoalNodeID = 1` / 0x159 arm.
		constexpr uint32 ManBatWord = 0u;   // SEAM: `CNPC_VManBat +0x6670`
		const uint32 Obfuscated =
			(((((ManBatWord & 0x710935u) ^ 0x148739u) + 0x4094abu) & 0x18ef6cau) ^ ManBatWord)
			^ 0x412a96ecu;
		if (Obfuscated != 0xfa0b0694u)
		{
			MoveGoalNodeId = 1;
			return 0x159;
		}
		if (ElysiumSchedule::HasInterruptCondition(Schedule, *this, Cognition.Conditions,
			EElysiumNpcCond::HeavyDamage))
		{
			return 0x15f;
		}
		// `(*DAT_1093b814 + 4)()` and `DAT_1093b814[0xb]` — a CNPC_VManBat-owned global object and
		// its eleventh word. UNRECOVERED; the false/non-zero arm is the one that answers 0x15d.
		const int32 Roll = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(1, 10);
		switch (Roll)
		{
		case 1:  return 0x15c;
		case 2:  return 0x15d;
		case 3:  return 0x161;
		case 4:
		case 5:
		case 6:  return 0x163;
		default: return 0x159;
		}
	}
	if (FCString::Strcmp(SlotBody, TEXT("0x1039de20")) == 0)
	{
		// CNPC_VMingXiaoTentacle. A four-phase state machine on `m_ePhase`, all of whose words —
		// `m_ePhase`, `m_flPhaseExpireTimer`, `m_flFailedEvadeTimer`, `m_flHideReadyTimer` — are
		// SPECIES words above `+0x665c` with no port member and no producer.
		RecordScheduleEvent(
			TEXT("SelectSchedule trace 0x1a (CNPC_VMingXiaoTentacle 0x1039de20)"));
		ElysiumStub::Fired(TEXT("species"), TEXT("CNPC_VMingXiaoTentacle::SelectSchedule 0x1039de20"),
			DebugString(), TEXT(""),
			TEXT("0002/29c-1: m_ePhase and the three phase timers have no port words"));
		// Phase 0 with an unexpired timer is the arm a freshly spawned tentacle takes, and it is the
		// only one reachable without the species words: `curtime < m_flPhaseExpireTimer` -> 0x156.
		return 0x156;
	}
	return 0;
}

// -------------------------------------------------------------------------------------------------
// The melee failure gate and slot 604 `SelectScheduleMeleeCombat`.
// -------------------------------------------------------------------------------------------------

// 0x102b6fe0
int32 FElysiumNpc::MeleeScheduleFailureGate(FElysiumEntity* Enemy)
{
	// Two identical shapes, one per condition, in retail's order.
	const bool bOccluded = Cognition.Conditions.Has(EElysiumNpcCond::EnemyOccluded);
	const bool bBlocked = !bOccluded && Cognition.Conditions.Has(EElysiumNpcCond::EnemyBlocked);
	if (!bOccluded && !bBlocked)
	{
		return 0;
	}
	if (HasUsableRangedWeaponPort(*this))   // slot 308
	{
		if (bInMelee)
		{
			Slot601(Enemy);   // vtable +0x964, `void vfunc601(CBaseEntity*)`; retail name unrecovered
		}
		RecordScheduleEvent(bOccluded
			? TEXT("MeleeScheduleFailureGate AI_BaseNPCTroika.cpp:23135 -> 0xe9")
			: TEXT("MeleeScheduleFailureGate AI_BaseNPCTroika.cpp:23159 -> 0xe9"));
		return 0xe9;
	}
	if (bOccluded)
	{
		RecordScheduleEvent(TEXT("MeleeScheduleFailureGate AI_BaseNPCTroika.cpp:23145 -> 0xcd"));
		return 0xcd;
	}
	RecordScheduleEvent(TEXT("MeleeScheduleFailureGate AI_BaseNPCTroika.cpp:23169 -> 0xce"));
	return 0xce;
}

namespace
{
	// The height-difference timer every melee selector runs, byte-identical in all six bodies:
	//
	//   armed = false;
	//   if (m_flEnemyHeightDiff <= _DAT_10451acc)      m_flMeleeHeightDiffTimer = -1.0f;
	//   else if (m_flMeleeHeightDiffTimer == -1.0f)    m_flMeleeHeightDiffTimer = curtime +
	//                                                      RandomFloat(3.0, 4.0);
	//   else if (m_flMeleeHeightDiffTimer <= curtime)  armed = true;
	//
	// `CNPC_VSabbatLeader` runs only the first two arms and never reads the answer, which is
	// reproduced by discarding it there.
	bool TickMeleeHeightDiffTimer(FElysiumNpc& Npc, double Now)
	{
		if (Npc.ScheduleHost.EnemyHeightDiffUnits <= GScheduleMeleeHeightDiffUnits)
		{
			Npc.MeleeHeightDiffTimer = GScheduleMeleeTimerUnarmed;
			return false;
		}
		if (Npc.MeleeHeightDiffTimer == GScheduleMeleeTimerUnarmed)
		{
			Npc.MeleeHeightDiffTimer = Now
				+ ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).FRandRange(3.0f, 4.0f);
			return false;
		}
		return Npc.MeleeHeightDiffTimer <= Now;
	}
}

// slot 604 0x102b6c30 `int SelectScheduleMeleeCombat(int)`, with the five species overrides
int32 FElysiumNpc::SelectScheduleMeleeCombat(int32 Unused)
{
	(void)Unused;   // retail's argument is read by no arm of any of the six bodies
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	FElysiumEntity* Enemy = const_cast<FElysiumEntity*>(World != nullptr
		? ElysiumNpcCond::ResolveEnemyHandle(*World, Senses.Memory.Enemy) : nullptr);
	const FElysiumNpcClassSlot* Override = ElysiumNpcKernelClass::OverrideOf(RetailClass(), 604);
	const TCHAR* SlotBody = Override != nullptr ? Override->Address : TEXT("0x102b6c30");
	const FElysiumNpcConditions& Conds = Cognition.Conditions;

	// --- Story 29d, family SpeciesAnim10: three further species bodies -----------------------------
	// `CNPC_VHuman` `0x10385e40` (34 census classes), `CNPC_VMingXiao` `0x10396050` and `CNPC_VBach`
	// `0x10364080`. All three REPLACE the Troika body wholesale; the bodies are in
	// `ElysiumNpcKernelAnim10_2.cpp`.
	if (FCString::Strcmp(SlotBody, TEXT("0x10385e40")) == 0)
	{
		return SelectScheduleMeleeCombatHuman();
	}
	if (FCString::Strcmp(SlotBody, TEXT("0x10396050")) == 0)
	{
		return SelectScheduleMeleeCombatMingXiao();
	}
	if (FCString::Strcmp(SlotBody, TEXT("0x10364080")) == 0)
	{
		return SelectScheduleMeleeCombatBach();
	}

	// --- CNPC_VChangBros 0x1036d800 and CNPC_VTzimisceRunner 0x103c4430 ---------------------------
	// One shape, two schedule id sets. The `0x59` and `0x15a`/`0xe1` arms are where they differ.
	if (FCString::Strcmp(SlotBody, TEXT("0x1036d800")) == 0
		|| FCString::Strcmp(SlotBody, TEXT("0x103c4430")) == 0)
	{
		const bool bChang = FCString::Strcmp(SlotBody, TEXT("0x1036d800")) == 0;
		if (!bInMelee && !Slot599(0))
		{
			// SEAM: slot 599 takes the enemy in retail (`GetEnemy()` then `vfunc599(enemy)`); the
			// ledger types its parameter `int`, so the generated signature cannot carry a pointer
			// and the port passes 0. Named rather than hidden.
			const float Threshold = GScheduleMeleeRangeUnits + GScheduleChangMeleeMargin;
			if (ScheduleHost.EnemyDistUnits <= Threshold)
			{
				return 0xe4;
			}
			return 0xe7;
		}
		if (Conds.Has(EElysiumNpcCond::EnemyOccluded))
		{
			return 0xcd;
		}
		if (ElysiumSchedule::HasInterruptCondition(Schedule, *this, Conds,
			EElysiumNpcCond::ShouldDodge))
		{
			return 0xd5;
		}
		if (Conds.Has(EElysiumNpcCond::CanMeleeAttack1))
		{
			return 0xdd;
		}
		const bool bHeightArmed = TickMeleeHeightDiffTimer(*this, Now);
		if (Conds.Has(EElysiumNpcCond::EnemyUnreachable))
		{
			return bChang ? 0x15d : 0x17;
		}
		if (!Conds.Has(EElysiumNpcCond::TooFarForMelee)
			&& !Conds.Has(EElysiumNpcCond::TooFarToAttack))
		{
			return 199;
		}
		// `GetEnemy()` twice, then the enemy's `WorldSpaceCenter` (slot 192) into a discarded
		// stack vector, then `0x102a11d0`.
		if (Enemy != nullptr && ScheduleMeleeReachGate())
		{
			return bChang ? 0x15a : 0xe1;
		}
		if (ScheduleHost.EnemyDistUnits <= GScheduleMeleeRangeUnits && !bHeightArmed)
		{
			return 0xd2;
		}
		return 0xcb;
	}

	// --- CNPC_VAsianVampire 0x10361be0 -------------------------------------------------------------
	if (FCString::Strcmp(SlotBody, TEXT("0x10361be0")) == 0)
	{
		if (!bInMelee)
		{
			if (!Slot599(0))
			{
				return HasUsableRangedWeaponPort(*this) ? 0x15c : 0xe4;
			}
		}
		else if (Slot602())   // vtable +0x968
		{
			Slot601(Enemy);
			if (HasUsableRangedWeaponPort(*this))
			{
				return 0x15c;
			}
			// `if (range + range < dist != (range + range == dist))` — the decompiler's spelling of
			// the FPU compare; it is `dist > 2 * range`.
			return ScheduleHost.EnemyDistUnits > GScheduleMeleeRangeUnits * 2.0f ? 0xe7 : 0xe4;
		}
		if (Conds.Has(EElysiumNpcCond::EnemyUnreachable))
		{
			Slot601(Enemy);
			return GetJumpSchedule(Enemy);
		}
		if (Conds.Has(EElysiumNpcCond::CanMeleeAttack1))
		{
			return HasUsableRangedWeaponPort(*this) ? 0xdc : 0xdd;
		}
		const bool bHeightArmed = TickMeleeHeightDiffTimer(*this, Now);
		if (HasUsableRangedWeaponPort(*this)
			&& (Conds.Has(EElysiumNpcCond::TooFarForMelee)
				|| Conds.Has(EElysiumNpcCond::InterruptTime) || bHeightArmed)
			&& !bSuppressRanged)
		{
			Slot601(Enemy);
			return 0x15c;
		}
		if (!Conds.Has(EElysiumNpcCond::TooFarForMelee)
			&& !Conds.Has(EElysiumNpcCond::TooFarToAttack)
			&& !Conds.Has(EElysiumNpcCond::EnemyOccluded))
		{
			return 199;
		}
		if (HasUsableRangedWeaponPort(*this) && !bSuppressRanged)
		{
			return 0xca;
		}
		return 0xcb;
	}

	// --- CNPC_VSheriffMan 0x103af960 ---------------------------------------------------------------
	if (FCString::Strcmp(SlotBody, TEXT("0x103af960")) == 0)
	{
		if (!bInMelee)
		{
			if (!Slot599(0))
			{
				return HasUsableRangedWeaponPort(*this) ? 0xe9 : 0x15a;
			}
		}
		else if (Slot602())
		{
			Slot601(Enemy);
			return HasUsableRangedWeaponPort(*this) ? 0xe9 : 0xe7;
		}
		if (Conds.Has(EElysiumNpcCond::EnemyUnreachable))
		{
			Slot601(Enemy);
			return 0x15a;
		}
		if (Conds.Has(EElysiumNpcCond::CanMeleeAttack1))
		{
			return HasUsableRangedWeaponPort(*this) ? 0xdc : 0xdd;
		}
		const bool bHeightArmed = TickMeleeHeightDiffTimer(*this, Now);
		if (HasUsableRangedWeaponPort(*this)
			&& (Conds.Has(EElysiumNpcCond::TooFarForMelee)
				|| Conds.Has(EElysiumNpcCond::InterruptTime) || bHeightArmed))
		{
			Slot601(Enemy);
			return 0xe9;
		}
		if (!Conds.Has(EElysiumNpcCond::TooFarForMelee)
			&& !Conds.Has(EElysiumNpcCond::TooFarToAttack)
			&& !Conds.Has(EElysiumNpcCond::EnemyOccluded))
		{
			return 199;
		}
		return HasUsableRangedWeaponPort(*this) ? 0xca : 0xcb;
	}

	// --- CNPC_VSabbatLeader 0x103aa060 -------------------------------------------------------------
	if (FCString::Strcmp(SlotBody, TEXT("0x103aa060")) == 0)
	{
		if (!bInMelee && !Slot599(0))
		{
			return ScheduleHost.EnemyDistUnits <= GScheduleMeleeRangeUnits * 2.0f ? 0x15f : 0xe7;
		}
		if (Conds.Has(EElysiumNpcCond::EnemyOccluded))
		{
			return 0xcd;
		}
		if (Conds.Has(EElysiumNpcCond::CanMeleeAttack1))
		{
			return 0xdd;
		}
		// The leader runs the timer's first two arms only and never reads the answer.
		(void)TickMeleeHeightDiffTimer(*this, Now);
		if (Conds.Has(EElysiumNpcCond::EnemyUnreachable))
		{
			Slot601(Enemy);
			return 0x15b;
		}
		if (Conds.Has(EElysiumNpcCond::TooFarToAttack)
			&& ScheduleHost.EnemyDistUnits < GScheduleSabbatTooFarUnits)
		{
			return 0xdd;
		}
		if (!Conds.Has(EElysiumNpcCond::TooFarForMelee)
			&& !Conds.Has(EElysiumNpcCond::TooFarToAttack))
		{
			return 199;
		}
		return 0xcb;
	}

	// --- The Troika line 0x102b6c30 ----------------------------------------------------------------
	if (!bInMelee)
	{
		if (!Slot599(0))
		{
			if (const int32 Gate = MeleeScheduleFailureGate(Enemy); Gate != 0)
			{
				return Gate;
			}
			RecordScheduleEvent(TEXT("SelectScheduleMeleeCombat AI_BaseNPCTroika.cpp:22943 -> 0xe4"));
			return 0xe4;
		}
	}
	else if (Slot602())
	{
		Slot601(Enemy);
		if (HasUsableRangedWeaponPort(*this))
		{
			return 0xe9;
		}
		return ScheduleHost.EnemyDistUnits > GScheduleMeleeRangeUnits * 2.0f ? 0xe7 : 0xe4;
	}
	if (const int32 Gate = MeleeScheduleFailureGate(Enemy); Gate != 0)
	{
		return Gate;
	}
	if (Conds.Has(EElysiumNpcCond::CanMeleeAttack1))
	{
		return HasUsableRangedWeaponPort(*this) ? 0xdc : 0xdd;
	}
	const bool bHeightArmed = TickMeleeHeightDiffTimer(*this, Now);
	if (HasUsableRangedWeaponPort(*this)
		&& (Conds.Has(EElysiumNpcCond::TooFarForMelee) || Conds.Has(EElysiumNpcCond::InterruptTime)
			|| Conds.Has(EElysiumNpcCond::EnemyUnreachable) || bHeightArmed))
	{
		Slot601(Enemy);
		return 0xe9;
	}
	if (Conds.Has(EElysiumNpcCond::EnemyUnreachable))
	{
		Slot601(Enemy);
		return 0x17;
	}
	if (!Conds.Has(EElysiumNpcCond::TooFarForMelee) && !Conds.Has(EElysiumNpcCond::TooFarToAttack))
	{
		return 199;
	}
	return HasUsableRangedWeaponPort(*this) ? 0xca : 0xcb;
}

// -------------------------------------------------------------------------------------------------
// 0x102b7690 — the entrenched cover / kick-prop selector.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpc::SelectCoverOrKickSchedule(const FScheduleHintSearchRequest& Request)
{
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	const bool bHasHintNode = ScheduleHost.HintNode != INDEX_NONE;
	const bool bDodging = NpcFlags.Has(EElysiumNpcFlag::DODGING);   // flags1 0x800
	FElysiumEntity* Prop = World != nullptr ? World->Resolve(ScheduleHost.KickProp) : nullptr;

	// A. The kick-physics-prop refresh, on its own two-second timer.
	if (Request.bRequest4 && ScheduleHost.bAllowKickHintUse && !bHasHintNode && !bDodging
		&& Prop == nullptr && ScheduleHost.KickPropSearchTimer <= Now)
	{
		// `RandomFloat(2.0, 2.0)` — a draw whose bounds are equal, so the interval is exactly two
		// seconds. Reproduced as the draw retail makes, because it advances the stream.
		ScheduleHost.KickPropSearchTimer =
			Now + ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).FRandRange(2.0f, 2.0f);
		Prop = FindKickPhysicsProp();
		ScheduleHost.KickProp = Prop != nullptr ? Prop->Handle : FElysiumEntityHandle::Invalid();
	}

	// B. A live kick prop short-circuits the whole hint search.
	if (Prop != nullptr)
	{
		return 0xa9;   // SCHED_TROIKA_KICK_PROP_AT_ENEMY
	}

	// C. The hint search, under the same three gates.
	if (!bHasHintNode && !bDodging && Prop == nullptr)
	{
		const FElysiumEntity* Enemy = World != nullptr
			? ElysiumNpcCond::ResolveEnemyHandle(*World, Senses.Memory.Enemy) : nullptr;
		ScheduleHost.HintCoverObject =
			Enemy != nullptr ? Enemy->Handle : FElysiumEntityHandle::Invalid();
		uint32 SearchMask = Request.bRequest1 ? 1u : 0u;
		if (Request.bRequest2)
		{
			SearchMask |= 2u;
		}
		if (Request.bRequest3 && ScheduleHost.bAllowKickHintUse)
		{
			SearchMask |= 4u;
		}
		if (Request.bRequest4 && ScheduleHost.bAllowKickHintUse)
		{
			SearchMask |= 8u;
		}
		SearchForCoverHint(SearchMask);
	}

	// D. The hint-type table. Retail reads `m_pHintNode->m_nHintType` (`+0x5dc`).
	FHintWords Hint;
	if (ScheduleHost.HintNode == INDEX_NONE || !HintWords(ScheduleHost.HintNode, Hint))
	{
		return 0;
	}
	if (Hint.HintType == GScheduleHintTypeShootAtA)
	{
		RecordScheduleEvent(TEXT("SelectCoverOrKickSchedule AI_BaseNPCTroika.cpp:23812 -> 0xa7"));
		return 0xa7;
	}
	if (Hint.HintType == GScheduleHintTypeShootAtB)
	{
		RecordScheduleEvent(TEXT("SelectCoverOrKickSchedule AI_BaseNPCTroika.cpp:23818 -> 0xa8"));
		return 0xa8;
	}
	if (Hint.HintType != GScheduleHintTypeCoverLow && Hint.HintType != GScheduleHintTypeCoverMed
		&& Hint.HintType != GScheduleHintTypeLean)
	{
		return 0;
	}

	// The cover-hint tail.
	if (!NpcFlags.Has(EElysiumNpcFlag::AT_COVER_HINT))   // flags1 0x2000
	{
		RecordScheduleEvent(TEXT("SelectCoverOrKickSchedule AI_BaseNPCTroika.cpp:23805 -> 0x9b"));
		NpcFlags.ClearRawWord2Bits(GScheduleCoverVsMeleeClearMask);
		return 0x9b;
	}

	// "Does my enemy carry a ranged threat" — the enemy's own enemy's active weapon reporting
	// `0x6000` in its capability word (slot 360). `bNoRangedThreat` is retail's `bVar1`.
	bool bNoRangedThreat = true;
	if (const FElysiumEntity* Enemy = World != nullptr
		? ElysiumNpcCond::ResolveEnemyHandle(*World, Senses.Memory.Enemy) : nullptr)
	{
		// SEAM: retail reads `enemy->+0x9c` — `CBaseCombatCharacter`'s cached downcast of ITSELF
		// (`docs/vtmb/npc-ai/schedule-kernel.md` § "The three cached downcasts") — and asks that
		// character's active weapon. This substrate has no cross-entity weapon-capability reader, so
		// the threat is never seen and the `bVar1 == true` arm is the one taken.
		ElysiumStub::Fired(TEXT("schedule"),
			TEXT("SelectCoverOrKickSchedule enemy ranged-threat bits 0x6000"), DebugString(),
			*Enemy->DebugString(), TEXT("0002/29c-1: no cross-entity weapon capability reader"));
		(void)GScheduleRangedThreatWeaponBits;
	}

	if (ScheduleHost.ShootAtHintNode == 0)
	{
		// `m_pShootAtHint = vfunc609(false)` (vtable `+0x984`, `CAI_Hint*`). The port carries hints
		// as node indices, and slot 609 is a generated stub answering null, so the write is null and
		// the inner arm below is the one taken.
		void* const ShootAtHint = Slot609(false);
		if (ShootAtHint == nullptr)
		{
			if (!HintIdleActivityGate())   // 0x102b5de0
			{
				++PeekOutCount;
				if (PeekOutCount < 5 && !Cognition.Conditions.Has(EElysiumNpcCond::EnemyOccluded))
				{
					RecordScheduleEvent(
						TEXT("SelectCoverOrKickSchedule AI_BaseNPCTroika.cpp:23797 -> 0x9d"));
					return 0x9d;
				}
				NpcFlags.ClearRawWord2Bits(GScheduleCoverVsMeleeClearMask);
				if (!bStayEntrenched)
				{
					// `CAI_BaseNPCTroika::ClearHintNode(60.0)` and no schedule.
					ClearScheduleHint(60.0f);
					return 0;
				}
				RecordScheduleEvent(
					TEXT("SelectCoverOrKickSchedule AI_BaseNPCTroika.cpp:23792 -> 0x9e"));
				return 0x9e;
			}
			// The gate passed: the peek-out counter decays by two with a floor at zero.
			PeekOutCount = FMath::Max(PeekOutCount - 2, 0);
			// `m_pSchedule == NULL || m_pSchedule != GetScheduleOfType(0x9e)` — "I am not already
			// running 0x9e" — and `COVER_VS_MELEE_MODE` (flags2 0x100) clear.
			const bool bNotAlready9e = !Schedule.IsRunning()
				|| GetLocalScheduleId(Schedule.Current) != 0x9e;
			const bool bCoverVsMelee = NpcFlags.Has(EElysiumNpcFlag2::COVER_VS_MELEE_MODE);
			if (!bNoRangedThreat)
			{
				const int32 Roll =
					ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(0, 99);
				if (bNotAlready9e && !bCoverVsMelee)
				{
					RecordScheduleEvent(
						TEXT("SelectCoverOrKickSchedule AI_BaseNPCTroika.cpp:23769 -> 0xa3"));
					return 0xa3;
				}
				NpcFlags.ClearRawWord2Bits(GScheduleCoverVsMeleeClearMask);
				return Roll > 0x1d ? 0xa0 : 0xa1;
			}
			if (bNotAlready9e && !bCoverVsMelee)
			{
				RecordScheduleEvent(
					TEXT("SelectCoverOrKickSchedule AI_BaseNPCTroika.cpp:23747 -> 0xa4"));
				return 0xa4;
			}
			RecordScheduleEvent(TEXT("SelectCoverOrKickSchedule AI_BaseNPCTroika.cpp:23743 -> 0xa5"));
			NpcFlags.Set(EElysiumNpcFlag2::COVER_VS_MELEE_MODE);
			NpcFlags.SetRawWord2Bits(FElysiumNpcFlags::Word2UnnamedBit31);
			return 0xa5;
		}
	}
	RecordScheduleEvent(TEXT("SelectCoverOrKickSchedule AI_BaseNPCTroika.cpp:23725 -> 0xa2"));
	return 0xa2;
}

// -------------------------------------------------------------------------------------------------
// The species half of slot 453 `BuildScheduleTestBits`.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::SpeciesBuildScheduleTestBits(FElysiumNpcConditions& InOutMask)
{
	const FElysiumNpcClassSlot* Override = ElysiumNpcKernelClass::OverrideOf(RetailClass(), 453);
	if (Override == nullptr)
	{
		return;
	}
	const TCHAR* SlotBody = Override->Address;

	// CNPC_VTzimisceHeadClaw 0x103c16f0: base, then `SetScheduleTestBits(SHOULD_CHARGE 0x35)`,
	// unconditionally.
	if (FCString::Strcmp(SlotBody, TEXT("0x103c16f0")) == 0)
	{
		InOutMask.Set(EElysiumNpcCond::ShouldCharge);
		return;
	}

	// CNPC_VPedestrian 0x103a2980: base, then `if (!IsBusyWithDiscipline()) SetScheduleTestBits(
	// PASS_OUT 0x24)`.
	if (FCString::Strcmp(SlotBody, TEXT("0x103a2980")) == 0)
	{
		if (!IsBusyWithDiscipline())
		{
			InOutMask.Set(EElysiumNpcCond::PassOut);
		}
		return;
	}

	// CNPC_VHumanCombatant 0x10387520 (nine census classes): base, then `if (IsAlive() &&
	// GetState() == 1 && !(bit7(m_edtDerivedType) && m_bCameFromSpawner)) SetScheduleTestBits(
	// SEE_CORPSE_FRIEND 0x3e)`.
	if (FCString::Strcmp(SlotBody, TEXT("0x10387520")) == 0)
	{
		// Slot 158 `IsAlive` and slot 464 `GetState` (retail state 1 is idle). Both generated slots
		// are stubs; the port carries both facts, so they are read from the entity and the mind.
		if (!IsDead() && Mind.State() == EElysiumNpcState::Idle)
		{
			// SEAM: `m_edtDerivedType` (`+0x004c`) is a chain word of `CBaseEntity` with no port
			// member; the port asks it and reads bit 7 as clear, which is the arm that admits the
			// condition regardless of `m_bCameFromSpawner`.
			constexpr bool bDerivedTypeBit7 = false;
			if (!bDerivedTypeBit7 || !bCameFromSpawner)
			{
				InOutMask.Set(EElysiumNpcCond::SeeCorpseFriend);
			}
		}
		return;
	}

	// CNPC_VGuard1 0x1037cdf0. This one does NOT call the Troika-line body — it calls the empty
	// BASE `CAI_BaseNPC::BuildScheduleTestBits` (`0x10280fb0`) — and then branches on `m_NPCState`.
	// The port's `BuildScheduleTestBits` has already run the Troika overlay by the time this is
	// reached, which is a DIVERGENCE and is named at the call site.
	if (FCString::Strcmp(SlotBody, TEXT("0x1037cdf0")) == 0)
	{
		const EElysiumNpcState State = Mind.State();
		if (State == EElysiumNpcState::Idle)
		{
			// Retail state 1: `SetScheduleTestBits(COMFORT 0x27)` and nothing else — and then it
			// falls into the shared state-3 tail below, which is what the listing's fallthrough
			// from `iVar5 == 1` does.
			InOutMask.Set(EElysiumNpcCond::Comfort);
		}
		else if (State != EElysiumNpcState::Alert)
		{
			// Retail state 0xb is the HUNT state, which this runtime's `EElysiumNpcState` does not
			// carry; every other state returns without touching the mask.
			//
			// SEAM: the hunt arm sets or clears `SEE_PLAYER` (0x5a) and `HEAR_PLAYER` (0x6f) on the
			// same five-threshold test as the alert tail below, plus `m_fHatesPlayer`. It is
			// unreachable here and is named rather than folded into another state.
			return;
		}

		// The state-3 (alert) tail, shared with the state-1 fallthrough: the five `pl_*` thresholds
		// against the CLOSEST PLAYER's current levels.
		bool bAnyThresholdPassed = false;
		if (World != nullptr && Senses.Memory.ClosestPlayer.IsSet())
		{
			if (const FElysiumPlayer* Player = World->FindPlayer())
			{
				if (Player->Handle == Senses.Memory.ClosestPlayer && !Player->IsInert())
				{
					bAnyThresholdPassed =
						PlInvestigate <= Player->Law.Investigate
						|| PlCriminalFlee <= Player->Law.Criminal
						|| PlCriminalAttack <= Player->Law.Criminal
						|| PlSupernaturalFlee <= Player->Law.Supernatural
						|| PlSupernaturalAttack <= Player->Law.Supernatural;
				}
			}
		}
		if (bAnyThresholdPassed)
		{
			InOutMask.Set(EElysiumNpcCond::InvestigateLevel);
			InOutMask.Set(EElysiumNpcCond::CriminalFleeLevel);
			InOutMask.Set(EElysiumNpcCond::CriminalAttackLevel);
			InOutMask.Set(EElysiumNpcCond::SupernaturalFleeLevel);
			InOutMask.Set(EElysiumNpcCond::SupernaturalAttackLevel);
			return;
		}
		// The miss arm clears ONE bit, `HEAR_PLAYER` (0x6f), and leaves the five alone.
		InOutMask.Clear(EElysiumNpcCond::HearPlayer);
	}
}

// -------------------------------------------------------------------------------------------------
// The seams.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpc::GetJumpSchedule(FElysiumEntity* Enemy) const
{
	// SEAM for `GetJumpSchedule` (`CNPC_VAsianVampire`'s `COND_ENEMY_UNREACHABLE` arm). No jump
	// schedule family is registered here and the retail body is not one of this story's rows.
	(void)Enemy;
	return 0;
}

FElysiumEntity* FElysiumNpc::FindKickPhysicsProp() const
{
	// SEAM for `0x102b6650`, which reads `GetEnemy()` (`+0x29c`) and `GetAbsOrigin()` (`+0x364`) and
	// sweeps for a kickable `prop_physics`. `FElysiumNpc::TaskFail` already knows how to release one
	// (`ScheduleHost.KickProp`), so only the producer is missing.
	return nullptr;
}

void FElysiumNpc::SearchForCoverHint(uint32 SearchMask)
{
	// SEAM for `0x102b7110`, the hint search that writes `m_pHintNode` (`+0x5ddc`). It reads
	// `m_bLeaningLeft`, the cover-LOS counters (`+0x6404`/`+0x6408`), `m_iPeekOutCount` and
	// `m_bStayEntrenched` and is the Hints layer's producer, not this family's.
	ElysiumStub::Fired(TEXT("hints"), TEXT("CAI_BaseNPCTroika::SearchForCoverHint 0x102b7110"),
		DebugString(), FString::Printf(TEXT("mask=0x%x"), SearchMask),
		TEXT("0002/29c-1: no hint-node store"));
}

bool FElysiumNpc::ScheduleMeleeReachGate() const
{
	// SEAM for `0x102a11d0`. Retail name unrecovered; four direct callers, reads `+0x300` (the
	// enemy's `WorldSpaceCenter`) and `+0x650`.
	return false;
}

bool FElysiumNpc::AndreiBloodSelectGate() const
{
	// NO LONGER A SEAM. `thunk_FUN_1035e920` reads `+0x66b8`, which family **Species** recovered as
	// `CNPC_VAndreiBlood::m_iActiveRunnerCount` and declared, and landed the body as `FUN_1035e920`
	// in `ElysiumNpcKernelSpecies2.cpp`. False is still the arm that answers `0x15c`; it is now
	// false because the runner budget says so rather than because nothing answered.
	return FUN_1035e920();
}

int32 FElysiumNpc::NavigatorGoalType() const
{
	// SEAM for `0x1027d990`, the navigator probe `CNPC_VManBat::SelectSchedule` opens with. It reads
	// the navigator at `+0x5d34`; the port's motor carries no goal-type word. Answering anything but
	// 2 is retail's "no active goal" arm, and 0 is the value `GoalType_t` spells for it.
	return 0;
}
