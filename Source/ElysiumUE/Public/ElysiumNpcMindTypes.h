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
