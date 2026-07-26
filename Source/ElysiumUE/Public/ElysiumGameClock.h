#pragma once

#include "CoreMinimal.h"

// R4 — one clock, no engine timers. Game-visible time is a single absolute-seconds value
// (VtMB's `curtime`): pausable and scalable, variable step (no fixed tick). Everything
// time-based in Track B keys off this — delayed I/O, ScheduleTask strings, per-entity
// next-think — through the one FElysiumEventQueue (P1.4); the queue and think times both
// serialize, so game time must be ours, never FTimerManager.
//
// The clock lives on UElysiumGameStateSubsystem and persists across map travel (only the
// entity world and its queue die with the map actor).
//
// S1 — one clock, advanced in exactly one place. `Advance` is private and reachable only
// through FElysiumTimeControl, which the map actor's gameplay tick calls as its first
// statement (runtime-architecture.md §3, step 2). Pause and scale are recorded here and
// mirrored onto the engine by that same facade; nothing else writes them.
struct FElysiumGameClock
{
	// The one pause/scale facade is the only advance site (S1).
	friend struct FElysiumTimeControl;

	// Absolute game seconds (curtime) — the key space for the event queue and think times.
	double GetNow() const { return Now; }

	bool IsPaused() const { return bPaused; }

	// The recorded time scale, mirrored onto engine time dilation by FElysiumTimeControl.
	// The clock never multiplies by it: engine dilation has already scaled the frame delta
	// handed to Advance, and scale is applied exactly once (runtime-architecture.md §4).
	double GetScale() const { return Scale; }

private:
	// Advance by the frame's ALREADY-DILATED delta; returns the game-time delta applied
	// (0 while held). No factor of its own — see GetScale.
	double Advance(double DilatedDeltaSeconds)
	{
		if (bPaused)
		{
			return 0.0;
		}
		Now += DilatedDeltaSeconds;
		return DilatedDeltaSeconds;
	}

	void SetPaused(bool bInPaused) { bPaused = bInPaused; }

	// Recorded only (clamped non-negative); the facade is what applies it, as engine dilation.
	void SetScale(double InScale) { Scale = FMath::Max(0.0, InScale); }

	// Rewind to a fixed start (a fresh session, or a load restoring a saved curtime).
	void Reset(double StartSeconds)
	{
		Now = StartSeconds;
		Scale = 1.0;
		bPaused = false;
	}

	double Now = 0.0;
	double Scale = 1.0;
	bool bPaused = false;
};
