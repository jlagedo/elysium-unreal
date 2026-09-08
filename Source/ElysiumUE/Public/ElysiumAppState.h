#pragma once

#include "CoreMinimal.h"

// The application's six states. Plain C++, no UObject reflection:
// the transition table below is the whole rule set, so it is testable with no game instance, no
// world and no RHI — `Elysium.Substrate.AppState`.
//
// The state is owned by UElysiumGameFlowSubsystem and by nothing else. A per-world object cannot
// own it: it must survive travel, and travel is exactly when it changes.
enum class EElysiumAppState : uint8
{
	Boot,        // process start; nothing loaded
	FrontEnd,    // static menu in the empty boot world, no run
	Loading,     // travel in flight; the loading screen is up
	Playing,     // a session is running with a pawn
	Paused,      // Playing + the world held + the pause menu
	GameOver,    // death / Masquerade 5 -> load or quit
};

namespace ElysiumAppState
{
	inline const TCHAR* Name(EElysiumAppState State)
	{
		switch (State)
		{
		case EElysiumAppState::Boot:     return TEXT("Boot");
		case EElysiumAppState::FrontEnd: return TEXT("FrontEnd");
		case EElysiumAppState::Loading:  return TEXT("Loading");
		case EElysiumAppState::Playing:  return TEXT("Playing");
		case EElysiumAppState::Paused:   return TEXT("Paused");
		case EElysiumAppState::GameOver: return TEXT("GameOver");
		}
		return TEXT("?");
	}

	// Case-insensitive parse of the names above, for the console verbs and the agent surface.
	inline bool Parse(const FString& In, EElysiumAppState& Out)
	{
		static const EElysiumAppState All[] = {
			EElysiumAppState::Boot,     EElysiumAppState::FrontEnd, EElysiumAppState::Loading,
			EElysiumAppState::Playing,  EElysiumAppState::Paused,   EElysiumAppState::GameOver };
		for (const EElysiumAppState S : All)
		{
			if (In.Equals(Name(S), ESearchCase::IgnoreCase))
			{
				Out = S;
				return true;
			}
		}
		return false;
	}

	// A run exists: the session record, the player sheet and `G` are live. FrontEnd is the empty
	// shell without a run, which is exactly why it does not pause.
	inline bool IsInSession(EElysiumAppState State)
	{
		return State == EElysiumAppState::Playing
			|| State == EElysiumAppState::Paused
			|| State == EElysiumAppState::GameOver;
	}

	// The states that hold the world (FElysiumTimeControl::SetPaused). Note this is a property of
	// the *state*, not of the time facade — `elysium.pause` can hold a Playing world by hand, and
	// the flow never takes that hold away behind the dev's back.
	inline bool HoldsWorld(EElysiumAppState State)
	{
		return State == EElysiumAppState::Paused || State == EElysiumAppState::GameOver;
	}

	// The transition table. Self-transitions are legal (every entry point is idempotent); anything
	// not listed is a bug in the caller and is refused with a warning rather than silently taken.
	//
	// The two load-bearing rows:
	//   Paused   is reachable only from Playing — the empty front-end shell has no run to hold.
	//   Boot     is reachable from nowhere. The boot decision is made once, at game-instance init.
	inline bool CanEnter(EElysiumAppState From, EElysiumAppState To)
	{
		if (From == To)
		{
			return true;
		}
		switch (To)
		{
		case EElysiumAppState::Boot:
			return false;
		case EElysiumAppState::FrontEnd:
			// Cold boot straight to the menu, or arriving in the empty shell after quit-to-menu.
			return From == EElysiumAppState::Boot || From == EElysiumAppState::Loading;
		case EElysiumAppState::Loading:
			// Any state can travel: New Game, Load, Reload, quit-to-menu, a trigger_changelevel.
			return true;
		case EElysiumAppState::Playing:
			// Arriving in a play world, or leaving the pause menu.
			return From == EElysiumAppState::Boot
				|| From == EElysiumAppState::Loading
				|| From == EElysiumAppState::Paused;
		case EElysiumAppState::Paused:
			return From == EElysiumAppState::Playing;
		case EElysiumAppState::GameOver:
			return From == EElysiumAppState::Playing || From == EElysiumAppState::Paused;
		}
		return false;
	}
}
