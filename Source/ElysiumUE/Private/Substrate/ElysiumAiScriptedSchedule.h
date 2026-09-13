#pragma once

#include "CoreMinimal.h"

#include "ElysiumEntityHandle.h"
#include "ElysiumNpcMindTypes.h"
#include "Substrate/ElysiumSchedule.h"

class FElysiumEntity;
class FElysiumEntityWorld;

// `aiscripted_schedule` — the authored AI director.
//
// It is NOT a scripted sequence, and collapsing the two is the mistake this file exists to prevent:
// "Unlike a scripted sequence, this entity pushes an AI policy and goal rather than claiming the
// body for one exact animation" (`docs/vtmb/npc-ai/authored-control.md` -> "`aiscripted_schedule`").
// The sequence claims bodies; the schedule pushes a state and a goal and lets the ordinary kernel
// run. Everything below follows from that one distinction — the pushed state persists as the mind's
// own state rather than as a hold, cognition keeps gathering, and only the two MOVING programs take
// a body-owner token at all.
//
// The recovered surface: 13 corpus entities, spawn validator `0x101a9730`, executor `0x101a98c0`,
// the mode table (1/2 scheduled move-to-goal-entity, 3 assign-goal-as-enemy plus condition 0x54,
// 4/5 scheduled follow-path), the NON-IDENTICAL `forcestate` mapping, "a missing goal logs and
// stops", "spawn warns when neither a schedule nor forced state is supplied" and "spawn flag
// `0x800` suppresses the route-failure warning".

namespace ElysiumAiScriptedSchedule
{
	/**
	 * The authored `schedule` keyvalue.
	 *
	 * The `A`/`B` suffixes are deliberate: the survey states that modes 1 and 2 call *variants* of
	 * scheduled move-to-goal-entity and 4 and 5 *variants* of scheduled follow-path, and that "the
	 * exact gait or policy label distinguishing 1 from 2 and 4 from 5 is not yet proven". Naming the
	 * members after a guessed label would hide the open question inside the type.
	 */
	enum class EMode : uint8
	{
		None        = 0,
		MoveToGoalA = 1,
		MoveToGoalB = 2,
		AssignEnemy = 3,
		FollowPathA = 4,
		FollowPathB = 5,
	};

	bool IsKnownMode(int32 Authored);
	const TCHAR* ModeName(int32 Authored);
	bool IsMoveToGoal(int32 Authored);
	bool IsFollowPath(int32 Authored);

	/**
	 * CHOSEN, NOT RECOVERED — the 1 vs 2 and 4 vs 5 distinction, taken as WALK versus RUN.
	 *
	 * Quoted from `docs/vtmb/npc-ai/README.md`: "The exact gait or policy label
	 * distinguishing 1 from 2 and 4 from 5 is not yet proven." Gait is chosen over a policy label
	 * because gait is the one difference between two otherwise identical move orders that a player
	 * can see, and because the corpus splits cleanly along it: the two rows carrying `forcestate 0`
	 * are the unhurried ones (mode 1, the Santa Monica blueblood strolling into an alley; mode 4,
	 * Mercurio turning around in the apartment), while every row carrying an alert or combat force
	 * state is an urgent response (the three warehouse thug retreats and the three clinic guards
	 * answering a security camera, all mode 2, plus the one mode 5). The LOWER value of each pair is
	 * therefore read as the walking variant and the higher one as the running variant: 1 walks and 2
	 * runs, 4 walks and 5 runs.
	 *
	 * Replace this function when the discriminator is decoded; nothing else in the family depends on
	 * which way round it is.
	 */
	bool IsRunVariant(int32 AuthoredMode);

	// The recovered authored->native `forcestate` table. Returns false for authored 0 ("no forced
	// state", an ordinary value on two corpus rows) and for a value outside the table.
	bool ForcedState(int32 Authored, EElysiumNpcState& OutState);
	bool IsKnownForceState(int32 Authored);

	// Spawn flag 0x800 — "suppresses the route-failure warning". No corpus row authors it; the one
	// row with any spawnflags at all writes 4.
	inline constexpr int32 SpawnFlagSuppressRouteWarning = 0x800;

	// How many nodes a follow-path route may chain before it is refused as authored nonsense.
	inline constexpr int32 MaxRouteNodes = 32;

	// Is `Id` one of the two programs this family registers? The save path asks, because the order
	// behind them is not save state.
	bool IsScriptedProgram(EElysiumScheduleId Id);

	// Which program a mode runs, or `None` for mode 3 (which runs no program at all) and for an
	// unknown mode.
	EElysiumScheduleId ProgramFor(int32 AuthoredMode);

	/**
	 * Build the route a follow-path order walks: the goal's own origin, then each entity its
	 * `target` keyfield chains to.
	 *
	 * CHOSEN, NOT RECOVERED, and deliberately degenerate for everything the corpus ships. No mode 4
	 * or 5 row wires a multi-node path: the one live mode-4 goal is the `aiscripted_schedule`
	 * entity's own targetname and the one mode-5 goal (`cs_target`) does not exist in its map at
	 * all, which is the recovered log-and-stop case firing in shipped content. VtMB's own patrol
	 * routes are addressed by an `info_node_patrol_point`'s `Group` key rather than by `goalent`, so
	 * the patrol substrate is not what `goalent` names and reusing it here would be inventing a
	 * wire. What is used instead is Source's own `target` chain, which costs nothing and collapses
	 * to a single leg — identical to a move-to-goal — for every authored row.
	 */
	void BuildRoute(FElysiumEntityWorld& World, const FElysiumEntity& Goal, TArray<FVector>& OutRoute);
}

/**
 * The order one `aiscripted_schedule` pushed onto one NPC, and the whole of what the two moving
 * programs read.
 *
 * SESSION STATE, NOT SAVE STATE. It carries a live goal handle and a route resolved out of the
 * current map epoch; the two things a push durably changes — the mind's state and, for mode 3, the
 * committed enemy — are already carried by the `NpcMind` and `NpcSenses` save blocks. A save cannot
 * normally be taken while an order is in flight either, because `FElysiumNpc::SaveBlockReason`
 * refuses one while the `ScriptedSchedule` owner holds the body.
 */
struct FElysiumScriptedScheduleOrder
{
	int32 Mode = 0;
	FElysiumEntityHandle Source;   // the `aiscripted_schedule` that pushed it, for diagnostics
	FElysiumEntityHandle Goal;
	TArray<FVector> Route;
	int32 Leg = 0;
	bool bRun = false;
	bool bSuppressRouteWarning = false;

	// The forced state travels WITH the order, and only for the deferred case below. Admission
	// establishes idle on an NPC's first think, so a state pushed ahead of it would be wiped.
	bool bHasForcedState = false;
	EElysiumNpcState ForcedState = EElysiumNpcState::Idle;
	// The whole push is waiting for this NPC's first think. `sm_medical_1` fires `guard_to_nurse`
	// from an `npc_maker`'s `OnSpawnNPC`, so a director can reach an NPC that has never thought.
	bool bPending = false;
	// One route-failure report per pushed order. A body that cannot take its first leg will not take
	// the next one either, and this is a director's mistake rather than a per-think event.
	bool bWarnedRoute = false;

	bool IsSet() const { return Mode != 0; }
	void Reset() { *this = FElysiumScriptedScheduleOrder(); }
};
