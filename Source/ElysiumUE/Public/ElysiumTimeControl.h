#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class UGameInstance;
class UWorld;
struct FElysiumGameClock;

// The one pause / time-scale facade, over the substrate clock AND engine time.
// It lives on UElysiumGameStateSubsystem beside the clock it
// drives, and it is the clock's only writer: FElysiumGameClock keeps Advance/SetPaused/
// SetScale/Reset private and friends this struct, so "one clock, advanced in one place" is
// a compile-time property rather than a convention.
//
// Two halves, both needed. Engine pause freezes actor ticks, physics and animation; the clock
// hold freezes thinks, the event queue, movers and ScheduleTask. Either alone leaves half the
// world moving. The same goes for scale: VtMB's third-person blend is scaled by the player's
// time scale and disciplines schedule on the same queue as everything else, so bullet time has
// to be a property of time, not a per-system multiplier.
//
// **Scale is applied exactly once.** Engine dilation already scales the tick's DeltaSeconds, so
// AdvanceFrame is handed a dilated delta and the clock multiplies by nothing — the facade sets
// both sides, and Clock.GetScale() is the mirror of what the world settings actually took
// (a clamped dilation is read back, so the two can never disagree).
struct FElysiumTimeControl
{
	explicit FElysiumTimeControl(FElysiumGameClock& InClock) : Clock(InClock) {}

	// The owning subsystem hands over its game instance at Initialize. Every engine-side call
	// resolves the live world through it, so travel never leaves a stale world pointer behind.
	void Bind(UGameInstance* InGameInstance) { GameInstance = InGameInstance; }

	// The frame.
	// Step 2 — the only place `Now` moves, called first thing in the map actor's gameplay tick.
	// DeltaSeconds is that tick's own delta, which the engine has already dilated. Returns the
	// game-time delta applied (0 while held).
	double AdvanceFrame(double DeltaSeconds);

	// The tail of step 7 — the map actor's post-move tick, i.e. the end of a released frame.
	// Consumes one armed dev step and re-holds the world when the last one is spent.
	void EndFrame();

	// The facade.
	void SetPaused(bool bPaused);
	bool IsPaused() const;

	// Set game time scale on both sides. A scale of 0 is not pause — the world settings clamp
	// dilation to a small positive value, so use SetPaused to actually hold the world.
	void SetScale(double Scale);
	double GetScale() const;

	// Dev: release N frames while held, then hold again. No-op when the world is running.
	void StepFrames(int32 NumFrames);
	int32 StepsPending() const { return PendingSteps; }

	// Rewind the clock (a fresh session, or a load restoring a saved curtime); clears the hold,
	// the scale and any armed steps, and re-stamps the engine side.
	void ResetClock(double StartSeconds = 0.0);

	// Re-stamp the current hold + scale onto the live world. Engine pause and dilation are
	// per-world state and the clock is not, so a travel would otherwise silently drop them.
	// The map actor calls this from BeginPlay.
	void ApplyToWorld();

private:
	// The clock half of a hold, plus the engine half. Kept apart from SetPaused so a dev step
	// can release the world without clearing the steps it is counting down.
	void ApplyPaused(bool bPaused);
	UWorld* ResolveWorld() const;

	FElysiumGameClock& Clock;
	TWeakObjectPtr<UGameInstance> GameInstance;

	// Frames still owed to a StepFrames call; > 0 means the world is running only because of it.
	int32 PendingSteps = 0;
};
