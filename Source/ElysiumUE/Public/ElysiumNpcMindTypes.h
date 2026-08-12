#pragma once

#include "CoreMinimal.h"

// Stable, engine-neutral NPC state vocabulary. The first mind slice admits Idle, Scripted and
// Dead; the other values reserve the recovered state-machine surface without pretending their
// transitions have landed.
enum class EElysiumNpcState : uint8
{
	Idle,
	Alert,
	Combat,
	Scripted,
	Prone,
	Dead,
};

// K7's closed body-owner set. This is gameplay policy, not an Unreal movement mode: the one
// IElysiumNpcMotor remains the only engine-facing locomotion seam.
enum class EElysiumBodyOwner : uint8
{
	None,
	Schedule,
	Patrol,
	Ambient,
	Sequence,
	ScriptedSchedule,
	Follower,
	Dialogue,
};

// A transient ownership capability. Generation rejects a release from an owner displaced earlier
// in the same map epoch. Tokens are session state and never enter a save payload.
struct FElysiumBodyOwnerToken
{
	EElysiumBodyOwner Owner = EElysiumBodyOwner::None;
	uint32 Generation = 0;

	bool IsSet() const { return Owner != EElysiumBodyOwner::None && Generation != 0; }
	void Reset() { Owner = EElysiumBodyOwner::None; Generation = 0; }
};

inline const TCHAR* LexToString(EElysiumNpcState State)
{
	switch (State)
	{
	case EElysiumNpcState::Idle:      return TEXT("Idle");
	case EElysiumNpcState::Alert:     return TEXT("Alert");
	case EElysiumNpcState::Combat:    return TEXT("Combat");
	case EElysiumNpcState::Scripted:  return TEXT("Scripted");
	case EElysiumNpcState::Prone:     return TEXT("Prone");
	case EElysiumNpcState::Dead:      return TEXT("Dead");
	default:                          return TEXT("unknown");
	}
}

inline const TCHAR* LexToString(EElysiumBodyOwner Owner)
{
	switch (Owner)
	{
	case EElysiumBodyOwner::None:              return TEXT("None");
	case EElysiumBodyOwner::Schedule:          return TEXT("Schedule");
	case EElysiumBodyOwner::Patrol:            return TEXT("Patrol");
	case EElysiumBodyOwner::Ambient:           return TEXT("Ambient");
	case EElysiumBodyOwner::Sequence:          return TEXT("Sequence");
	case EElysiumBodyOwner::ScriptedSchedule:  return TEXT("ScriptedSchedule");
	case EElysiumBodyOwner::Follower:          return TEXT("Follower");
	case EElysiumBodyOwner::Dialogue:          return TEXT("Dialogue");
	default:                                   return TEXT("unknown");
	}
}
