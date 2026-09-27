#pragma once

#include "CoreMinimal.h"

#include "ElysiumEntityHandle.h"
#include "ElysiumNpcMindTypes.h"
#include "Substrate/ElysiumSchedule.h"

class FElysiumEntity;
class FElysiumEntityWorld;

// The order an `aiscripted_schedule` (`CCineAISchedule`, `Substrate/ElysiumAiScriptedSchedule.h`)
// pushes onto its NPC, and the recovered tables the push reads: the NPC-side half of
// `CCineAISchedule::FixScriptNPCSchedule` (`0x101a98c0`). Split out of the director's own header in
// story 5 fold A3 so `FElysiumNpc`, which runs the pushed programs, does not include the director
// class it no longer shares a base with.
//
// The recovered surface: the mode table (1/2 scheduled move-to-goal-entity, 3 assign-goal-as-enemy
// plus condition 0x54, 4/5 scheduled follow-path), the NON-IDENTICAL `forcestate` mapping, and spawn
// flag `0x800` suppressing the route-failure warning (`docs/vtmb/npc-ai/authored-control.md`).

namespace ElysiumAiScriptedSchedule
{
    // 0x101a98c0: 1/4 ACT_WALK (9), 2/5 ACT_RUN (19); MoveType 5/6 uses ACT_FLY (34).
    enum class EMode : uint8
    {
        None = 0, MoveToGoalA = 1, MoveToGoalB = 2, AssignEnemy = 3,
        FollowPathA = 4, FollowPathB = 5,
    };
    bool IsKnownMode(int32 Authored);
    const TCHAR* ModeName(int32 Authored);
    bool IsMoveToGoal(int32 Authored);
    bool IsFollowPath(int32 Authored);
    bool IsRunVariant(int32 AuthoredMode);
    bool ForcedState(int32 Authored, EElysiumNpcState& OutState);
    bool IsKnownForceState(int32 Authored);
    inline constexpr int32 SpawnFlagSuppressRouteWarning = 0x800;
    inline constexpr int32 MaxRouteNodes = 128;

    // The number passed to ScheduledMoveToGoalEntity/FollowPath: base IDLE_WALK (2).
    // Slot 440 then translates it for the receiving NPC, normally to Troika IDLE_PATROL.
    int32 ProgramFor(int32 AuthoredMode);

    // Type-3 navigator goal: follow ordinary target keys until NULL or 128 entries.
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
	int32 Program = 0; // Installed GLOBAL id; ordinary corpus programs can also run without an order.
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

	// Story 29c-1, family Schedule. `0x102ae840` writes a bare `int` into `+0x65cc` — the offset
	// the shape map binds to THIS struct ("the forced state travels with the pushed order") — beside
	// `m_bForceStateChange` and `CHOOSE_NEW_SCHEDULE`.
	//
	// **CORRECTED, story 29d, family Combat10.** 29c-1 read it as "the director's own order id, not
	// a schedule number", read by nothing. It is `m_eForcedState`, a raw retail `NPC_STATE`:
	// `0x102ae840(this, NPC_STATE, bForce)` stores its FIRST argument here, and slot 460
	// `PreSelectIdealState` (`0x102ad340`, `102ad34f`) is its ONE consumer — it copies the word into
	// `m_IdealNPCState`, clears it, and returns it ahead of every other arm.
	// `FElysiumNpc::ForcedNpcState` / `ClearForcedNpcState` are the accessors that say so.
	int32 RetailOrderId = 0;  // +0x65cc m_eForcedState, the word `0x102ae840` stamps

	bool IsSet() const { return Mode != 0; }
	void Reset() { *this = FElysiumScriptedScheduleOrder(); }
};
