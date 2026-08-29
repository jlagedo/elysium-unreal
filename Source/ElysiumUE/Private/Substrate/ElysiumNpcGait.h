#pragma once

#include "CoreMinimal.h"

#include "ElysiumGraphState.h"      // the projected state a published cell is a cell of
#include "ElysiumWorldServices.h"   // IElysiumNpcMotor — the body that answers with its own speeds

// An NPC's travel speeds and the bounds a scripted move runs under.
//
// Retail NPC locomotion speed is the cycle's own authored movement, which the body answers with
// through `IElysiumNpcMotor::GaitSpeed`. These constants are the fallback for a body whose export
// resolves no fan for the gait asked for, and Troika's stated `speed_walk`/`speed_runbase` are what
// that fallback states.
namespace ElysiumNpcGait
{
	inline constexpr float WalkSpeed = 254.0f;   // speed_walk 100 in/s
	inline constexpr float RunSpeed  = 571.5f;   // speed_runbase 225 in/s

	// Which of the body's own fans a projected graph state is a state of. Only the three gaits have
	// one; every other state plays a cell whose speed the selection record already carries, and
	// asking a fan for it would be inventing a number.
	//
	// It lives here, beside the travel speed it selects, because the speed authority has to be one
	// number: the record publishes its cell through this mapping and the motor commands through the
	// same one, so the two cannot name different fans.
	inline bool GaitKindForState(EElysiumGraphState State, EElysiumNpcGaitKind& OutKind)
	{
		switch (State)
		{
		case EElysiumGraphState::Walk:  OutKind = EElysiumNpcGaitKind::Walk;  return true;
		case EElysiumGraphState::Run:   OutKind = EElysiumNpcGaitKind::Run;   return true;
		case EElysiumGraphState::Sneak: OutKind = EElysiumNpcGaitKind::Sneak; return true;
		default: return false;
		}
	}

	// What a travel request commands, cm/s: the body's own authored cell for the gait at the
	// direction it is travelling in, or the stated constant when this body resolves no fan for it.
	// One place, because every producer — patrol, ambient, scripted travel, a combat chase, a
	// retreat — wants the same answer, and a producer that reached for the constant directly is the
	// constant-speed slide.
	//
	// `MoveYawDegrees` is the realized direction the body's own fan is being steered by. A request
	// being *issued* leaves it at zero — the leg has not started, so forward is the honest answer —
	// and the body's own animation pass re-commands with the live angle every frame after that.
	inline float TravelSpeed(const IElysiumNpcMotor* Motor, EElysiumNpcGaitKind Gait,
		float MoveYawDegrees = 0.0f)
	{
		const float Authored = Motor ? Motor->GaitSpeed(Gait, MoveYawDegrees) : 0.f;
		if (FMath::IsFinite(Authored) && Authored > 0.f)
		{
			return Authored;
		}
		return Gait == EElysiumNpcGaitKind::Run ? RunSpeed : WalkSpeed;
	}

	// How close to the mark counts as standing on it.
	inline constexpr float ScriptAcceptanceCm = 24.0f;
	// And how close counts for a body that has stopped closing. A beat sends several NPCs to marks
	// a few centimetres apart — sp_theatre's walk-out lands five of them inside 30 cm — which two
	// 34 cm crowd agents cannot resolve while they collide. Spawnflag 4096 turns character
	// collision off for the beat's duration, so that cluster does not form and the walkers reach
	// the tight acceptance above. What remains is a failure net for a
	// mark a body genuinely cannot stand on (world geometry, a bad graph), and a net wants to be
	// small — declaring a body "arrived" a metre and a half out would hide exactly that failure.
	inline constexpr float ScriptCrowdedCm = 90.0f;
	// The distance that counts as progress, and the two windows without it. Within the net above a
	// short one gives up quickly, because a body that close and no longer improving is stuck rather
	// than working; outside it a long one leaves room for a detour whose straight-line distance to
	// the mark is not falling yet.
	inline constexpr float ScriptProgressCm = 8.0f;
	inline constexpr double ScriptCrowdSettleSeconds = 1.5;
	inline constexpr double ScriptStallSeconds = 4.0;
	// The turn-in-place budget, and how long an unadvanced move waits before the NPC frees itself.
	inline constexpr double ScriptFaceSeconds = 2.0;
	inline constexpr double ScriptWatchdogSeconds = 1.0;

	// The absolute cap on a travel phase. It has to sit under the cleanup timers the map hangs off
	// its own camera track — sp_theatre kills the walk-out beats 20 s after the shot starts, having
	// been authored against a walk that takes about half that — so the budget is the straight-line
	// time plus half again for the route the navmesh actually takes, and a floor for a short hop.
	// A beat that hits the cap places its NPC on the mark and ends, which is always better than
	// being killed mid-travel with its OnEndSequence unfired.
	inline double TravelCapSeconds(float DistanceCm, float SpeedCmPerSecond)
	{
		return 1.5 * static_cast<double>(DistanceCm) / FMath::Max(1.0, static_cast<double>(SpeedCmPerSecond))
			+ 3.0;
	}
}
