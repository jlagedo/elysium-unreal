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

	// (V3c: the scripted-move seam's constants -- mark acceptance, crowd net, stall windows, the
	// watchdog and the travel cap -- went with the seam (M11). Retail's scripted travel is tasks
	// 8/9/10's navigator goal on the cine.)
}
