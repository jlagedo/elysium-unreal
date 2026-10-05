#include "Substrate/ElysiumNpcBase.h"

#include "Debug/ElysiumNpcDebugLogging.h"
#include "ElysiumAnimEvent.h"
#include "ElysiumAnimationIntent.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSaveTypes.h"
#include "ElysiumSessionSubsystem.h"
#include "ElysiumSoundLevel.h"
#include "ElysiumStub.h"
#include "ElysiumSurfaceSounds.h"
#include "ElysiumWorldServices.h"
#include "HAL/IConsoleManager.h"
#include "Substrate/ElysiumScriptedScheduleOrder.h"
#include "Substrate/ElysiumScriptedSequence.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumDisciplines.h"
#include "Substrate/ElysiumFeed.h"
#include "Substrate/ElysiumFootsteps.h"
#include "Substrate/ElysiumHint.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcCombatSchedules.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcLoadout.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcThinkCadence.h"
#include "Substrate/ElysiumPhysProp.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumWeaponClasses.h"

#include <cmath>

FElysiumNpcBase::FElysiumNpcBase() { EnemyMemory.BindOwner(*this); } // 0x102df320 +0

// --- `m_hCine` and the ideal-state writers a director uses (story 5 fold A3) ------------------

FElysiumScriptedSequence* FElysiumNpcBase::ResolveCine() const
{
	FElysiumEntity* Owner = World != nullptr && ScriptOwner.IsSet() ? World->Resolve(ScriptOwner) : nullptr;
	FElysiumNpcBase* AsBase = Owner != nullptr ? Owner->AsNpcBase() : nullptr;
	return AsBase != nullptr ? AsBase->AsSpecies<FElysiumScriptedSequence>() : nullptr;
}

void FElysiumNpcBase::RequestIdealStateRetail(int32 RetailId, int32 SourceLine)
{
	Mind.RequestDesiredState(RetailId, SourceLine);
}

// --- Moved from `ElysiumNpc.cpp` (story 5 step 5) ---

const FElysiumEntity* FElysiumNpcBase::FindPatrolPoint(const FString& Name) const
{
	// `0x102d2840`, which `0x102d2900` wraps for `InputFollowPatrolPath` (`0x1029ed90`): walk the
	// global hint list from its head, admit a hint of type 10000 or 800 only, and compare its
	// `Group` (`+0x5f0`) to the token with a byte-for-byte, CASE-SENSITIVE compare. First match wins.
	// No targetname path, and no disabled/owner/cooldown or group-mask test (`python_bridge.md`).
	//
	// This is `0x102d2840` alone and answers the hint; its wrapper `0x102d2900`
	// (`FElysiumNpc::PatrolNodeIdFor`) turns it into the hint's network node (`+0x5e4`, -1 on a miss
	// or a standalone hint), and the patrol readers walk to that node's position
	// (`PatrolNodePosition`), not to the hint's origin.
	if (World == nullptr)
	{
		return nullptr;
	}
	for (const int32 Index : World->HintList())
	{
		const FElysiumHint* Hint = World->Entities().IsValidIndex(Index)
			? FElysiumHint::Cast(World->Entities()[Index].Get()) : nullptr;
		if (Hint == nullptr || (Hint->HintType != 10000 && Hint->HintType != 800))
		{
			continue;
		}
		if (Hint->Group.Equals(Name, ESearchCase::CaseSensitive))
		{
			return Hint;
		}
	}
	return nullptr;
}

void FElysiumNpcBase::CompleteDeathHandoff()
{
	if (bDeathHandoffDone)
	{
		return;
	}
	bDeathHandoffDone = true;
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (Embodiment == nullptr || Visual == nullptr)
	{
		return;   // headless, or a bodiless record — an ordinary absence, not a failure
	}
	// **The named divergence.** Retail creates its ragdoll inside the shared `Event_Killed` body,
	// from the model's own `ACT_DIERAGDOLL` seed pose and with a force envelope composed from the
	// killing blow (`docs/vtmb/combat-and-damage.md`). Ours hands over at the END of the death
	// program, seeded from whatever pose that program left on the body, and with no impulse: the
	// force envelope is unrecovered (the launch slice cannot start before the impulse is), so an
	// invented one would be a behaviour rather than a reproduction.
	if (Embodiment->StartBodyRagdoll(Visual))
	{
		// Physics owns the pose now, so the animation claims mean nothing and go back.
		Embodiment->ReleaseBodyAnimClaims(Visual);
		Mind.RecordExternal(TEXT("death: the body handed to physics from its current pose"));
		return;
	}
	// The stated fallback, and the shipped one. The claims deliberately STAY: the pose stops being
	// evaluated at all, and releasing them would hand the base channel back to a locomotion publish
	// on a body that no longer answers it, leaving the verdict surface naming no holder for a pose it
	// is holding.
	Embodiment->HoldBodyFinalPose(Visual);
	Mind.RecordExternal(TEXT("death: no physics behind this body — holding its final frame"));
}

uint8 FElysiumNpcBase::NpcStateFlags() const
{
	// This runtime's state vocabulary onto retail's `m_NPCState` ids (`0x1026e3e0`'s cases): idle
	// 1, combat 2, alert 3, script 4, dead 7. Prone is Source's `NPC_STATE_PRONE` 6. The flee (8)
	// and hunt (0xb) states are 21a's and 10h's; until they exist no body can carry `0x85` or
	// `0x7f`.
	int32 RetailState = 0;
	switch (Mind.State())
	{
	case EElysiumNpcState::Idle:     RetailState = 1; break;
	case EElysiumNpcState::Combat:   RetailState = 2; break;
	case EElysiumNpcState::Alert:    RetailState = 3; break;
	case EElysiumNpcState::Scripted: RetailState = 4; break;
	case EElysiumNpcState::Prone:    RetailState = 6; break;
	case EElysiumNpcState::Dead:     RetailState = 7; break;
	}
	return FElysiumNpcFlags::NpcStateFlagsForRetailState(RetailState);
}


float FElysiumNpcBase::RandomSeconds(float Max)
{
	// `RandomFloat(0.1, arg)` (`0x10283dae`). `FRandRange` is the same `low + (high - low) * frac`,
	// so an operand below the floor draws between it and 0.1, as retail's does.
	return ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).FRandRange(0.1f, Max);
}

void FElysiumNpcBase::ClearSchedule()
{
	// `CAI_BaseNPC::ClearSchedule` (`0x10280d30`), reached by every site that drops a running
	// program. The six schedule words, `PRESERVE_PATH` and the slot-435 dispatch are
	// `ElysiumSchedule::ClearSchedule`'s; what this adds is the name, so no site clears the
	// schedule record directly and skips the release the dispatch performs.
	ElysiumSchedule::ClearSchedule(Schedule, *this);
}

void FElysiumNpcBase::DisconnectFromSquad()
{
	// 0x1026d050: the refcount is real even while no named squad exists. R17 supplies
	// the shared/global enemy-memory redirection; this host must not invent a local squad.
	// `0x1026d05b`: only a count <= 0 leaves the squad (`0x1026d068` through `+0x5da4`, no port
	// squad) and formats the `ClearMemory` reason DevMsg (`0x1026d08b`, debug text, absent); then
	// `++m_iSquadDisconnected` (`0x1026d097`). Retail writes NO flag here: `D_DISCONNECT_SQUAD`
	// (`m_bfAINPCFlags2` bit 23) is set by the callers that want it (the task at `0x102a536e`). The
	// port's `NpcFlags.Set(D_DISCONNECT_SQUAD)` is deleted (story 8 L11 integration): it made every
	// grapple (`0x1026cdc0`) and feed-begin (`0x1026cec0`) carry the bit.
	++BaseScheduleHost.SquadDisconnected;
}

void FElysiumNpcBase::ReconnectToSquad()
{
	// 0x1026d0c0 (reached through the jump thunk 0x10009601), read whole in story 29c-1:
	//
	//   if (--m_iSquadDisconnected < 1) {
	//       if (m_pSquad) AddSelfToSquadMemory(m_pSquad, this);   // 0x10316720
	//       m_iSquadDisconnected = 0;
	//   }
	//   m_bfAINPCFlags2 &= 0x7f7fffff;
	//
	// The clamp below is retail's `< 1 -> 0` arm; the mask clears D_DISCONNECT_SQUAD (0x00800000)
	// and bit 31, which `docs/vtmb/npc-ai/schedule-kernel.md` records as the flag-name resolver's
	// word-two routing marker and not a flag, so the one named bit is the whole observable clear.
	const bool bReachedZero = BaseScheduleHost.SquadDisconnected - 1 < 1;
	BaseScheduleHost.SquadDisconnected = FMath::Max(0, BaseScheduleHost.SquadDisconnected - 1);
	if (bReachedZero)
	{
		// The squad seam (`ElysiumNpcSquad.cpp`): no `CAI_Squad`, so nothing to rejoin.
		AddSelfToSquadMemory(const_cast<void*>(ConnectedSquad()));
	}
	NpcFlags.Clear(EElysiumNpcFlag2::D_DISCONNECT_SQUAD);
}

void FElysiumNpcBase::AddOblivious()
{
	NpcFlags.Set(EElysiumNpcFlag2::MADE_OBLIVIOUS);
	++ObliviousCount;
}

void FElysiumNpcBase::RemoveOblivious()
{
	// Retail clamps at zero (`0x1026d160`) rather than trusting the pairing, and so does this: the
	// binary itself has a path that drops the bookkeeping bit without decrementing, so the counter is
	// not provably balanced even in retail.
	ObliviousCount = FMath::Max(0, ObliviousCount - 1);
	NpcFlags.Clear(EElysiumNpcFlag2::MADE_OBLIVIOUS);
}

FString FElysiumNpcBase::DescribeNpcFlags() const
{
	const FString Words = NpcFlags.Describe();
	if (ObliviousCount <= 0)
	{
		return Words;
	}
	const FString Count = FString::Printf(TEXT("oblivious=%d"), ObliviousCount);
	return Words == TEXT("-") ? Count : Words + TEXT("|") + Count;
}

void FElysiumNpcBase::ArmThinkAt(double Stamp)
{
	// `m_flNextThink := Stamp`. `NextThink` is a float and the world clock a double, so
	// `float(Stamp)` rounds ABOVE `Stamp` for most values -- and `RunThinks`' `NextThink > Now` test
	// would then skip the very frame the stamp names, which is visible for any armed delay
	// (`NPCInit`'s `curtime + 0.1` most of all). One ULP down where it does. (Retail has no such
	// problem: `m_flNextThink` and `gpGlobals->curtime` are both floats there.)
	float Armed = static_cast<float>(Stamp);
	if (static_cast<double>(Armed) > Stamp)
	{
		Armed = std::nextafterf(Armed, -FLT_MAX);
	}
	NextThink = Armed;
}

// `AIExtendedSaveHeader_t` -- the one block retail's own `Save` writes by hand (`0x1027bc60`), and
// the only thing `FElysiumNpcBase::OnRestore` `0x1027bf50` needs in order to re-find the program the save was
// taken during: its NAME, the checksum of its task array, and the three-bit liveness word.
void FElysiumNpcBase::SerializeExtendedHeader(FElysiumSaveArchive& Ar)
{
	FAiExtendedSaveHeader Header = Ar.IsLoading()
		? FAiExtendedSaveHeader() : BuildExtendedSaveHeader();
	Ar << Header.Version;
	Ar << Header.Flags;
	Ar << Header.ScheduleName;
	Ar << Header.ScheduleCrc;
	if (Ar.IsLoading())
	{
		LastSavedExtendedHeader = Header;
	}
}

void FElysiumNpcBase::TaskFail(int32 Reason)
{
	// `CAI_BaseNPC::TaskFail` `0x10273fc0`, slot 448's base body; the Troika override `0x1029adb0`
	// ends in a direct call to it. Its head is a debug arm under `developer != 0` (`DAT_1070af4c`,
	// `0x10273fc3`..`0x10273fe3`): the overlay record `+0x5f30` (the failure text, `0x10316fa0`),
	// `+0x5f38` (the schedule), `+0x5f3c = 0`, and — under `m_debugOverlays & 0x8000000`
	// (`npc_task_text`, `0x10274000`) — `DevMsg("   TaskFail -> %s\n")` (`0x105cc5e0`). The port
	// prints that line through the NPC trace; NAMED DIVERGENCE (debug output only): the `developer`
	// half of the gate (shipped 0) is dropped for the print and the overlay record is not kept.
	// The AI trace's `taskfail` event is the same name and number (debug output only).
	const bool bPrint = (DebugOverlays & OverlayTaskTextBit) != 0;
	if (bPrint || IsAiTraced())
	{
		const FString Failure = FString::Printf(TEXT("%s (0x%x)"), ElysiumTaskFailureName(Reason), Reason);
		if (bPrint)
		{
			NpcTraceMessage(FString::Printf(TEXT("   TaskFail -> %s"), *Failure));
		}
		EmitAiTrace(TEXT("taskfail"), Failure);
	}
	// The three writes are the body.
	BaseScheduleHost.bShouldMove = false;                                    // +0x1a40
	BaseScheduleHost.FailureReason = Reason;                                 // +0x5c50 taskFailureCode
	Cognition.Conditions.Set(EElysiumNpcCond::TaskFailed);                   // SetCondition(0x5c)
}

void FElysiumNpcBase::Serialize(FElysiumSaveArchive& Ar)
{
	// The `CAI_BaseNPC` half of the NPC record, written ahead of the Troika's (the Troika `Save`
	// calls `0x1027bc60` first): retail's one hand block (`AIExtendedSaveHeader_t`), then the base words no
	// retail datamap row reaches through the generated walk.
	SerializeExtendedHeader(Ar);
	// The combat character's flag words travel with the record and not with the walk: retail's
	// `m_bfAINPCFlags` pair is a concern the shape map reaches no compiled path into. The NPC's own
	// `m_iIsOblivious` and `m_bfNPCFrenziedFlags` are generated bindings and ride the walk.
	NpcFlags.Serialize(Ar);
	Relationships.Serialize(Ar);
	BaseMemory.Serialize(Ar);
	FElysiumNpcPendingSound::SerializeQueue(Ar, PendingSounds);
	EnemyMemory.Serialize(Ar);
	BaseScheduleHost.Serialize(Ar);
	// `CBaseEntity::m_vecAttackExtents` (+0x50), on the entity since story 5 step 6; saved where the
	// base host saved it, so the record's order is unchanged.
	Ar << AttackExtentsCm;
	// `m_hTargetEnt`, a retail `SAVE` row and a recorded gap in the generated walk.
	Ar << TargetEnt;
}

// --- Moved from `ElysiumNpc.cpp` (story 5 step 5) ---

bool FElysiumNpcBase::IsIdealActivityCurrent() const
{
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (Embodiment == nullptr || Visual == nullptr || !ScheduleIdealActivity.IsValid())
	{
		return false;
	}

	FElysiumClipPhase Current;
	if (!Embodiment->GetBodyClipPhase(Visual, EElysiumAnimChannel::Base, Current)
		|| !Current.IsValid())
	{
		return false;
	}
	// Both sides are canonicalized before they cross their seams: the activity resolver supplies the
	// owning include-DAG bank, and the body publishes that same committed identity. Case-insensitive
	// comparison is the convention at every other identity join; no source-model alias participates.
	return Current.OwnerStem.Equals(ScheduleIdealActivity.OwnerStem, ESearchCase::IgnoreCase)
		&& Current.OwnerRoot.Equals(ScheduleIdealActivity.OwnerRoot, ESearchCase::IgnoreCase)
		&& Current.Label.Equals(ScheduleIdealActivity.Label, ESearchCase::IgnoreCase);
}

void FElysiumNpcBase::RecordScheduleEvent(const FString& Row)
{
	Mind.RecordExternal(Row);
	// NAMED MODERNIZATION (debug output only): the port's schedule rows — among them every
	// selector's `+0x1b2c`/`+0x1b30`/`+0x1b34` stamp (retail writes the selector id, `__FILE__` and
	// `__LINE__` and prints nothing) — join the NPC trace under `npc_task_text`, indented 2.
	if ((DebugOverlays & OverlayTaskTextBit) != 0)
	{
		NpcTraceMessage(Row, 2);
	}
}

void FElysiumNpcBase::StopMoving()
{
	if (Motor != nullptr)
	{
		Motor->Stop();
		ClearMoveIgnores();
	}
	bMoveIssued = false;
}
