#include "Substrate/ElysiumAiScriptedSchedule.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcLog.h"

// `aiscripted_schedule` — `CCineAISchedule`, story 5 fold A3. Each body is the retail body at the
// address its comment names (`vtmb_code`).

namespace
{
	constexpr int32 GScheduleSfQuietRoute = ElysiumAiScriptedSchedule::SpawnFlagSuppressRouteWarning;
}

// Slot 82: `0x101a9620`.
void* FElysiumAiScriptedSchedule::GetDataDescMap()
{
	return const_cast<FElysiumClassDesc*>(FElysiumClassRegistry::Get().Find(FName(RetailClassName)));
}

// Slot 103: `0x101a9730`.
void FElysiumAiScriptedSchedule::Spawn()
{
	FElysiumScriptedSequence::Spawn();   // `CCineNPC::Spawn` 0x101a6f10, DIRECT
	if (ForceState == 0 && Mode == 0)
	{
		UE_LOG(LogElysiumNpcEnt, Log, TEXT("aiscripted_schedule - no schedule or state has been set!"));
	}
}

// Slot 583: `0x101a9790` (`CCineAISchedule::vfunc583`), 233 bytes. No `m_hCine`, no script state, no
// ideal state, nothing written on this object: straight to this class's own slot 586.
void FElysiumAiScriptedSchedule::PossessEntity()
{
	FElysiumNpcBase* Npc = TargetNpc();   // 0x101a9794..0x101a97cd m_hTargetEnt, +0x94
	if (Npc == nullptr)
	{
		return;                           // 0x101a979d / 0x101a97bd / 0x101a97c7 / 0x101a97d5 -> 0x101a9876
	}
	if (!Npc->BaseScheduleHost.bRanAi)    // 0x101a97db m_bRanAI +0x1b4c / 0x101a97e3
	{
		// `DevMsg` (`[0x109f3630]`, held in ESI) per line: 0x101a97f1 stars, 0x101a97f8 WARNING,
		// 0x101a97ff GetDebugName(this) / 0x101a980a "scripted sequence(%s)", 0x101a9811
		// GetDebugName(npc) / 0x101a981c "is targeting an entity(%s)", 0x101a9823 hidden, 0x101a982a
		// immediately, 0x101a9831 .1 second, 0x101a9838 ScriptUnhide, 0x101a983f scripted sequence,
		// 0x101a9846 programmer, 0x101a984d WARNING, 0x101a9854 stars.
		NotRunAiWarning(*Npc, nullptr);   // 0x101a97e6..0x101a9854, the "has not run" line skipped
	}
	if (!bInterruptable)                  // 0x101a985a +0x5f90 / 0x101a9862
	{
		MakeNpcOblivious(*Npc);           // 0x101a9866 0x1026d130
	}
	FixScriptNPCSchedule(*Npc);           // 0x101a986b..0x101a9870 slot 586, virtual (0x101a98c0)
}                                         // 0x101a9878

// Slot 585: `0x101a9770`.
bool FElysiumAiScriptedSchedule::FCanOverrideState() const
{
	return true;
}

FElysiumEntity* FElysiumAiScriptedSchedule::ResolveGoal()
{
	// `0x100f7f20(NULL, m_sGoalEnt, this, 0)`: `FindEntityByName` with THIS director as the searching
	// entity and a null activator, then `FindEntityByClassname` when no name matches. With the
	// activator argument null, `0x100f7460` answers `!activator` from the searching entity's own
	// `m_hLastInputActivator` (`+0x10c`) — the listing's fall-through, so the input's activator.
	if (World == nullptr || GoalEntity.IsEmpty())
	{
		return nullptr;
	}
	if (GoalEntity.StartsWith(TEXT("!")))
	{
		return ResolveProceduralName(GoalEntity);
	}
	if (FElysiumEntity* Named = World->FindByName(GoalEntity))
	{
		return Named;
	}
	for (const TUniquePtr<FElysiumEntity>& Candidate : World->Entities())
	{
		if (Candidate.IsValid() && !Candidate->IsDead() && Candidate->Def != nullptr
			&& FElysiumEntityWorld::NameMatches(Candidate->Def->Classname, GoalEntity))
		{
			return Candidate.Get();
		}
	}
	return nullptr;
}

// Slot 586: `0x101a98c0`. No refusal on an unknown mode or force state: an unknown force state is
// simply not applied and an unknown mode installs nothing, exactly as retail's two switches fall out.
void FElysiumAiScriptedSchedule::FixScriptNPCSchedule(FElysiumNpcBase& Npc)
{
	FElysiumEntity* Goal = ResolveGoal();
	if (Goal == nullptr)
	{
		// `DevMsg(1, …)` in retail; a Warning here because it fires in shipped content (`sm_medical_1`'s
		// `guard_to_cs` names `cs_target`, which that map does not contain).
		UE_LOG(LogElysiumNpcEnt, Warning, TEXT("Can't find goal entity %s\nCan't execute script %s"),
			*GoalEntity, *DebugString());
		return;
	}
	// The executor is the Troika's (`FElysiumNpc::BeginScriptedSchedule`, the port's program host for
	// `ScheduledMoveToGoalEntity` `0x102800c0` / `ScheduledFollowPath` `0x102801e0` / `SetEnemy`). A
	// base-only NPC has no port executor: SEAM, it receives nothing.
	FElysiumNpc* Troika = Npc.AsNpc();
	if (Troika == nullptr || World == nullptr)
	{
		return;
	}
	// `forcestate` 1 -> 1, 2 -> 3, 3 -> 2 through `SetState` (`0x1026e340`); anything else, none.
	EElysiumNpcState Forced = EElysiumNpcState::Idle;
	const bool bHasForced = ElysiumAiScriptedSchedule::ForcedState(ForceState, Forced);

	FElysiumScriptedScheduleOrder Order;
	Order.Mode = Mode;
	Order.Source = Handle;
	Order.Goal = Goal->Handle;
	Order.bRun = ElysiumAiScriptedSchedule::IsRunVariant(Mode);
	Order.bSuppressRouteWarning = (SpawnFlags & GScheduleSfQuietRoute) != 0;
	if (ElysiumAiScriptedSchedule::IsFollowPath(Mode))
	{
		ElysiumAiScriptedSchedule::BuildRoute(*World, *Goal, Order.Route);
	}
	else if (ElysiumAiScriptedSchedule::IsMoveToGoal(Mode))
	{
		Order.Route.Add(Goal->Origin);
	}
	Troika->BeginScriptedSchedule(Order, bHasForced, Forced);
}

// `InputStartSchedule` `0x101a9b30`: `ThinkSet(CineThink)`, `m_flNextThink = curtime`. The search,
// the radius gate and the push all happen at that think.
void FElysiumAiScriptedSchedule::InputStartSchedule(const FElysiumInputArgs& Args)
{
	RecordInput(Args);
	ArmCineThink(EThinkFunction::CineThink, World != nullptr ? World->NowSeconds() : 0.0);
}

void FElysiumAiScriptedSchedule::AddInputs(FElysiumClassDesc& D, const TCHAR* RetailClass)
{
	if (FCString::Strcmp(RetailClass, TEXT("CCineAISchedule")) != 0)
	{
		return;
	}
	D.Input(TEXT("StartSchedule"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumAiScriptedSchedule&>(*E.AsNpcBase()).InputStartSchedule(Args); });
}

void FElysiumAiScriptedSchedule::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	FElysiumScriptedSequence::GetDebugState(Out);
	Out.Emplace(TEXT("Goal"), GoalEntity.IsEmpty() ? TEXT("(none)") : GoalEntity);
	Out.Emplace(TEXT("Mode"), FString::Printf(TEXT("%d (%s)%s"), Mode, ElysiumAiScriptedSchedule::ModeName(Mode),
		ElysiumAiScriptedSchedule::IsRunVariant(Mode) ? TEXT(" run") : TEXT(" walk")));
	EElysiumNpcState Forced = EElysiumNpcState::Idle;
	Out.Emplace(TEXT("Force state"), ElysiumAiScriptedSchedule::ForcedState(ForceState, Forced)
		? FString::Printf(TEXT("%d -> %s"), ForceState, LexToString(Forced))
		: FString::Printf(TEXT("%d (none)"), ForceState));
	if ((SpawnFlags & GScheduleSfQuietRoute) != 0)
	{
		Out.Emplace(TEXT("Spawnflags"), TEXT("0x800 — route-failure warning suppressed"));
	}
}
