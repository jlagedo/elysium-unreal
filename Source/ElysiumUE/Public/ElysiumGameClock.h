#pragma once

#include "CoreMinimal.h"

// R4 — one clock, no engine timers. Game-visible time is a single absolute-seconds value
// (VtMB's `curtime`): pausable and scalable, variable step (no fixed tick). Everything
// time-based in Track B keys off this — delayed I/O, ScheduleTask strings, per-entity
// next-think — through the one FElysiumEventQueue (P1.4); the queue and think times both
// serialize, so game time must be ours, never FTimerManager.
//
// The clock lives on UElysiumGameStateSubsystem and persists across map travel (only the
// entity world and its queue die with the map actor). It is passive here in P1.1 — the map
// actor advances it each tick with the frame's game delta once the world exists (P1.4).
struct FElysiumGameClock
{
	// Advance by a real-time frame delta; returns the game-time delta actually applied
	// (0 while paused). Scale dilates game time relative to real time.
	double Advance(double RealDeltaSeconds)
	{
		if (bPaused)
		{
			return 0.0;
		}
		const double GameDelta = RealDeltaSeconds * Scale;
		Now += GameDelta;
		return GameDelta;
	}

	// Absolute game seconds (curtime) — the key space for the event queue and think times.
	double GetNow() const { return Now; }

	void Pause() { bPaused = true; }
	void Resume() { bPaused = false; }
	void SetPaused(bool bInPaused) { bPaused = bInPaused; }
	bool IsPaused() const { return bPaused; }

	// Time dilation (clamped non-negative). 1.0 = real time.
	void SetScale(double InScale) { Scale = FMath::Max(0.0, InScale); }
	double GetScale() const { return Scale; }

	// Rewind to a fixed start (a fresh session, or a load restoring a saved curtime).
	void Reset(double StartSeconds = 0.0)
	{
		Now = StartSeconds;
		Scale = 1.0;
		bPaused = false;
	}

private:
	double Now = 0.0;
	double Scale = 1.0;
	bool bPaused = false;
};
