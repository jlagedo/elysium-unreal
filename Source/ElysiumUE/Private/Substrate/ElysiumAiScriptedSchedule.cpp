#include "Substrate/ElysiumAiScriptedSchedule.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "Substrate/ElysiumClassFields.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcLog.h"

// --- The recovered tables ---

bool ElysiumAiScriptedSchedule::IsKnownMode(int32 Authored)
{
	return Authored >= static_cast<int32>(EMode::MoveToGoalA)
		&& Authored <= static_cast<int32>(EMode::FollowPathB);
}

const TCHAR* ElysiumAiScriptedSchedule::ModeName(int32 Authored)
{
	switch (static_cast<EMode>(Authored))
	{
	case EMode::None:        return TEXT("none");
	case EMode::MoveToGoalA: return TEXT("move-to-goal A");
	case EMode::MoveToGoalB: return TEXT("move-to-goal B");
	case EMode::AssignEnemy: return TEXT("assign-goal-as-enemy");
	case EMode::FollowPathA: return TEXT("follow-path A");
	case EMode::FollowPathB: return TEXT("follow-path B");
	default:                 return TEXT("unknown");
	}
}

bool ElysiumAiScriptedSchedule::IsMoveToGoal(int32 Authored)
{
	return static_cast<EMode>(Authored) == EMode::MoveToGoalA
		|| static_cast<EMode>(Authored) == EMode::MoveToGoalB;
}

bool ElysiumAiScriptedSchedule::IsFollowPath(int32 Authored)
{
	return static_cast<EMode>(Authored) == EMode::FollowPathA
		|| static_cast<EMode>(Authored) == EMode::FollowPathB;
}

bool ElysiumAiScriptedSchedule::IsRunVariant(int32 AuthoredMode)
{
	// The CHOSEN reading, stated once at the declaration: within each pair the lower authored value
	// is the unhurried variant and the higher one the urgent variant. That is 1 walk / 2 run and
	// 4 walk / 5 run — not a parity rule, which would put mode 5 on a stroll.
	return static_cast<EMode>(AuthoredMode) == EMode::MoveToGoalB
		|| static_cast<EMode>(AuthoredMode) == EMode::FollowPathB;
}

bool ElysiumAiScriptedSchedule::ForcedState(int32 Authored, EElysiumNpcState& OutState)
{
	// Recovered verbatim, and the asymmetry is the whole reason this is a table rather than a cast:
	//
	//     authored 0 -> no forced state
	//     authored 1 -> native idle   (1)
	//     authored 2 -> native ALERT  (3)
	//     authored 3 -> native COMBAT (2)
	//
	// "The non-identical numbering is load-bearing. Treating the keyvalue as the native enum would
	// swap combat and alert" — which would put the three warehouse thugs into a fight and the eight
	// combat rows on a lookaround.
	switch (Authored)
	{
	case 1: OutState = EElysiumNpcState::Idle;   return true;
	case 2: OutState = EElysiumNpcState::Alert;  return true;
	case 3: OutState = EElysiumNpcState::Combat; return true;
	default: return false;
	}
}

bool ElysiumAiScriptedSchedule::IsKnownForceState(int32 Authored)
{
	return Authored >= 0 && Authored <= 3;
}

bool ElysiumAiScriptedSchedule::IsScriptedProgram(EElysiumScheduleId Id)
{
	return Id == EElysiumScheduleId::ScriptedMoveToGoal
		|| Id == EElysiumScheduleId::ScriptedFollowPath;
}

EElysiumScheduleId ElysiumAiScriptedSchedule::ProgramFor(int32 AuthoredMode)
{
	if (IsMoveToGoal(AuthoredMode))
	{
		return EElysiumScheduleId::ScriptedMoveToGoal;
	}
	if (IsFollowPath(AuthoredMode))
	{
		return EElysiumScheduleId::ScriptedFollowPath;
	}
	return EElysiumScheduleId::None;
}

void ElysiumAiScriptedSchedule::BuildRoute(FElysiumEntityWorld& World, const FElysiumEntity& Goal,
	TArray<FVector>& OutRoute)
{
	OutRoute.Reset();
	TSet<int32> Visited;
	const FElysiumEntity* Node = &Goal;
	for (int32 Guard = 0; Guard < MaxRouteNodes && Node != nullptr; ++Guard)
	{
		bool bAlreadySeen = false;
		Visited.Add(Node->Handle.Index, &bAlreadySeen);
		if (bAlreadySeen)
		{
			break;   // an authored cycle ends the route rather than walking it forever
		}
		OutRoute.Add(Node->Origin);
		if (Node->Target.IsEmpty())
		{
			break;
		}
		Node = World.FindByName(Node->Target);
	}
}

// --- The two programs ---

namespace
{
	using EAiTask = EElysiumTask;
	using EAiId = EElysiumScheduleId;

	FElysiumTaskStep AiScriptedStep(EAiTask Task, float Param = 0.f)
	{
		FElysiumTaskStep Out;
		Out.Task = Task;
		Out.Param = Param;
		return Out;
	}

	FElysiumTaskStep AiScriptedScheduleStep(EAiTask Task, EAiId Target)
	{
		FElysiumTaskStep Out;
		Out.Task = Task;
		Out.Target = Target;
		return Out;
	}

	void RegisterAiScriptedSchedulePrograms()
	{
		// --- Move to the goal entity (modes 1 and 2) ---------------------------------------------
		// CHOSEN, NOT RECOVERED (contents). The survey names the family — "modes 1 and 2 call
		// variants of scheduled move-to-goal-entity" — and decodes no task list for either variant.
		// The program is composed from the recovered vocabulary only: cancel whatever the NPC was
		// doing, issue the route's one leg, hold until the body arrives or gives up. The gait is NOT
		// a task: `TASK_RUN_PATH` states a locomotion for a whole program, and this family's
		// locomotion is the pushed ORDER's (the 1-vs-2 variant), so the path task places it.
		//
		// The interrupt mask is EMPTY, and that is the decision this family turns on rather than an
		// unfilled default. `aiscripted_schedule` is an authored director: the recovered entity
		// "pushes an AI policy and goal", and a program that a fresh stimulus could abort would make
		// the push advisory. The starvation this costs is bounded — the program ends on arrival or
		// on the first refused leg and hands the NPC straight back to ordinary selection, at which
		// point the enemy transaction's interrupt gate opens again.
		{
			FElysiumSchedule S;
			S.Id = EAiId::ScriptedMoveToGoal;
			S.Tasks = {
				AiScriptedStep(EAiTask::StopMoving),
				AiScriptedStep(EAiTask::GetPathToGoal),
				AiScriptedStep(EAiTask::WaitForMovement),
			};
			ElysiumSchedule::Register(MoveTemp(S));
		}

		// --- Follow the goal's path (modes 4 and 5) ----------------------------------------------
		// Same composition, plus the leg loop: `TASK_SET_SCHEDULE` back to this program is the
		// kernel's own in-place transfer, and `TASK_GET_PATH_TO_GOAL` fails by name once the route
		// is exhausted, which ends the program and returns the NPC to selection. For every row the
		// corpus authors the route has exactly one leg, so the loop runs once and stops.
		{
			FElysiumSchedule S;
			S.Id = EAiId::ScriptedFollowPath;
			S.Tasks = {
				AiScriptedStep(EAiTask::StopMoving),
				AiScriptedStep(EAiTask::GetPathToGoal),
				AiScriptedStep(EAiTask::WaitForMovement),
				AiScriptedScheduleStep(EAiTask::SetSchedule, EAiId::ScriptedFollowPath),
			};
			ElysiumSchedule::Register(MoveTemp(S));
		}
	}

}

// --- FElysiumAiScriptedSchedule — the entity ---

// A point entity that carries five keyvalues and one input. It derives from the base rather than
// from the `scripted_sequence` leaf on purpose: the two share a keyfield spelling (`m_iszEntity`)
// and nothing else — no pre-idle, no play, no post-idle, no queue lock, no body claim of its own.
class FElysiumAiScriptedSchedule final : public FElysiumEntity
{
public:
	// The authored record, under the corpus's own key spellings (read back off the exported `.ents`
	// of `sm_apartment_1`, `sm_diner_1`, `sm_hub_1`, `sm_medical_1` and `sm_warehouse_1`).
	FString TargetEntity;    // m_iszEntity — the NPC this director drives. 13 of 13 rows author it.
	FString GoalEntity;      // goalent — `!player` on 7 rows, a named entity on 6
	int32   Mode = 0;        // schedule — the recovered mode table
	int32   ForceState = 0;  // forcestate — authored, NOT the native state enum
	// m_flRadius — authored on all 13 rows (64, 128, 512, 1024, 1026).
	//
	// SEAM (parsed, unread): no recovered site in the spawn validator or the executor is stated to
	// read it, and `scripted_sequence`'s identically named keyfield is decoded as inert. Carrying it
	// keeps an authored row round-tripping through the field walk with what it was authored with,
	// and gives a recovered distance gate somewhere to land.
	float   Radius = 0.f;

	void InputStartSchedule(const FElysiumInputArgs& Args);

	virtual void Spawn() override;

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override;

private:
	// The NPC this director drives, resolved LATE — at service time, not at spawn. One corpus row
	// (`guard_to_nurse`) is fired from an `npc_maker`'s `OnSpawnNPC`, so its NPC does not exist when
	// the schedule entity is built.
	FElysiumNpc* ResolveNpc() const;

	// The goal, resolved the same way. `!player` is the corpus's commonest goal and is the one
	// engine alias this entity accepts; every other leading-`!` name resolves to nothing.
	FElysiumEntity* ResolveGoal() const;

	// One report each. A director wired to a missing name is an authoring fact, not a per-fire event.
	bool bWarnedMissingNpc = false;
	bool bWarnedMissingGoal = false;
	bool bWarnedUnknownMode = false;
	bool bWarnedUnknownForceState = false;
};

FElysiumNpc* FElysiumAiScriptedSchedule::ResolveNpc() const
{
	if (World == nullptr || TargetEntity.IsEmpty() || TargetEntity.StartsWith(TEXT("!")))
	{
		return nullptr;
	}
	FElysiumEntity* Entity = World->FindByName(TargetEntity);
	return Entity != nullptr ? Entity->AsNpc() : nullptr;
}

FElysiumEntity* FElysiumAiScriptedSchedule::ResolveGoal() const
{
	if (World == nullptr || GoalEntity.IsEmpty())
	{
		return nullptr;
	}
	if (GoalEntity.Equals(TEXT("!player"), ESearchCase::IgnoreCase))
	{
		return World->Resolve(World->PlayerHandle());
	}
	if (GoalEntity.StartsWith(TEXT("!")))
	{
		return nullptr;
	}
	return World->FindByName(GoalEntity);
}

void FElysiumAiScriptedSchedule::Spawn()
{
	// The spawn validator (`0x101a9730`): "spawn warns when neither a schedule nor forced state is
	// supplied". Such a row has nothing to push and its `StartSchedule` wire is dead, which is worth
	// saying at load rather than at the moment nothing happens.
	if (Mode == 0 && ForceState == 0)
	{
		UE_LOG(LogElysiumNpcEnt, Warning,
			TEXT("%s authors neither a schedule mode nor a forced state — it can push nothing"),
			*DebugString());
	}
}

void FElysiumAiScriptedSchedule::InputStartSchedule(const FElysiumInputArgs& Args)
{
	if (IsInert() || World == nullptr)
	{
		return;
	}
	(void)Args;   // the corpus fires all 8 wires with an empty parameter; the record is the order

	if (!ElysiumAiScriptedSchedule::IsKnownMode(Mode) && Mode != 0)
	{
		if (!bWarnedUnknownMode)
		{
			bWarnedUnknownMode = true;
			UE_LOG(LogElysiumNpcEnt, Warning,
				TEXT("%s authors schedule mode %d, which is outside the recovered 1..5 table — "
					 "refused rather than approximated"), *DebugString(), Mode);
		}
		return;
	}
	if (!ElysiumAiScriptedSchedule::IsKnownForceState(ForceState))
	{
		if (!bWarnedUnknownForceState)
		{
			bWarnedUnknownForceState = true;
			UE_LOG(LogElysiumNpcEnt, Warning,
				TEXT("%s authors forcestate %d, which is outside the recovered 0..3 table — refused"),
				*DebugString(), ForceState);
		}
		return;
	}

	FElysiumNpc* Npc = ResolveNpc();
	if (Npc == nullptr || Npc->IsInert())
	{
		if (!bWarnedMissingNpc)
		{
			bWarnedMissingNpc = true;
			UE_LOG(LogElysiumNpcEnt, Warning,
				TEXT("%s cannot push its schedule: `m_iszEntity` '%s' resolves to no living NPC"),
				*DebugString(), *TargetEntity);
		}
		return;
	}

	// The recovered refusal, and it fires in shipped content: `sm_medical_1`'s `guard_to_cs` names
	// `cs_target`, which that map does not contain. "A missing goal logs and stops" — including the
	// forced state, which is a push this director never got far enough to make.
	FElysiumEntity* Goal = ResolveGoal();
	if (Goal == nullptr)
	{
		if (!bWarnedMissingGoal)
		{
			bWarnedMissingGoal = true;
			UE_LOG(LogElysiumNpcEnt, Warning,
				TEXT("%s cannot push its schedule: `goalent` '%s' resolves to nothing — the "
					 "director logs and stops"), *DebugString(), *GoalEntity);
		}
		return;
	}

	FElysiumScriptedScheduleOrder Order;
	Order.Mode = Mode;
	Order.Source = Handle;
	Order.Goal = Goal->Handle;
	Order.bRun = ElysiumAiScriptedSchedule::IsRunVariant(Mode);
	Order.bSuppressRouteWarning =
		(SpawnFlags & ElysiumAiScriptedSchedule::SpawnFlagSuppressRouteWarning) != 0;
	if (ElysiumAiScriptedSchedule::IsFollowPath(Mode))
	{
		ElysiumAiScriptedSchedule::BuildRoute(*World, *Goal, Order.Route);
	}
	else if (ElysiumAiScriptedSchedule::IsMoveToGoal(Mode))
	{
		Order.Route.Add(Goal->Origin);
	}

	EElysiumNpcState Forced = EElysiumNpcState::Idle;
	const bool bHasForced = ElysiumAiScriptedSchedule::ForcedState(ForceState, Forced);

	Npc->BeginScriptedSchedule(Order, bHasForced, Forced);
}

void FElysiumAiScriptedSchedule::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	Out.Emplace(TEXT("NPC"), TargetEntity.IsEmpty() ? TEXT("(none)") : TargetEntity);
	Out.Emplace(TEXT("Goal"), GoalEntity.IsEmpty() ? TEXT("(none)") : GoalEntity);
	Out.Emplace(TEXT("Mode"), FString::Printf(TEXT("%d (%s)%s"), Mode,
		ElysiumAiScriptedSchedule::ModeName(Mode),
		ElysiumAiScriptedSchedule::IsRunVariant(Mode) ? TEXT(" run") : TEXT(" walk")));
	EElysiumNpcState Forced = EElysiumNpcState::Idle;
	Out.Emplace(TEXT("Force state"),
		ElysiumAiScriptedSchedule::ForcedState(ForceState, Forced)
			? FString::Printf(TEXT("%d -> %s"), ForceState, LexToString(Forced))
			: FString::Printf(TEXT("%d (none)"), ForceState));
	Out.Emplace(TEXT("Radius"), FString::Printf(TEXT("%.0f (parsed, unread)"), Radius));
	if ((SpawnFlags & ElysiumAiScriptedSchedule::SpawnFlagSuppressRouteWarning) != 0)
	{
		Out.Emplace(TEXT("Spawnflags"), TEXT("0x800 — route-failure warning suppressed"));
	}
}

// --- Registration ---

static TUniquePtr<FElysiumEntity> MakeAiScriptedSchedule()
{
	return MakeUnique<FElysiumAiScriptedSchedule>();
}

static void BuildAiScriptedScheduleClass(FElysiumClassDesc& D)
{
	D.Input(TEXT("StartSchedule"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumAiScriptedSchedule&>(E).InputStartSchedule(Args); });

	ElysiumAddClassField(D, TEXT("m_iszEntity"), &FElysiumAiScriptedSchedule::TargetEntity);
	ElysiumAddClassField(D, TEXT("goalent"),     &FElysiumAiScriptedSchedule::GoalEntity);
	ElysiumAddClassField(D, TEXT("schedule"),    &FElysiumAiScriptedSchedule::Mode);
	ElysiumAddClassField(D, TEXT("forcestate"),  &FElysiumAiScriptedSchedule::ForceState);
	ElysiumAddClassField(D, TEXT("m_flRadius"),  &FElysiumAiScriptedSchedule::Radius);
}

// The class and its two programs install together, following the combat family's precedent: a
// program and the policy that pushes it are one decision, and this file owns both halves.
struct FElysiumAiScriptedScheduleRegistrar
{
	FElysiumAiScriptedScheduleRegistrar()
	{
		RegisterAiScriptedSchedulePrograms();
		BuildAiScriptedScheduleClass(FElysiumClassRegistry::Get().Register(
			TEXT("aiscripted_schedule"), ElysiumBaseClassName(), &MakeAiScriptedSchedule));
	}
};

static FElysiumAiScriptedScheduleRegistrar GElysiumAiScriptedScheduleRegistrar;
