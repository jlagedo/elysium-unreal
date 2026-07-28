// 12.1 — the choreo timeline: a parsed scene walked against a clock.
//
// Split out of the entity so the per-line dialogue path (12.2's CInstancedSceneEntity — ~5,300 of
// the 5,444 shipped scenes are one `.vcd` per spoken line) can reuse it without an entity, a world
// or a map. Nothing here knows what an event *means*; it decides only when each one starts,
// continues and ends, and hands that to a callback.
//
// The model is Valve's, which VtMB forked unchanged: each frame every event is classified
// START / CONTINUE / STOP / IGNORE against the current time, and the callback is the four-method
// `IChoreoEventCallback` (vampire.dll carries its RTTI, `.?AVIChoreoEventCallback@@`). Scene end is
// `SimulationFinished()` — the clock past the last event AND nothing still running.
//
// Spec: `docs/choreographed_scenes.md` → "The timing model".

#pragma once

#include "CoreMinimal.h"
#include "Substrate/ElysiumSceneData.h"

// What a scene does with its events. One implementer today (the `logic_choreographed_scene`
// entity); 12.2 adds the dialogue one.
class IElysiumChoreoCallback
{
public:
	virtual ~IElysiumChoreoCallback() = default;

	// The clock crossed this event's start. `SceneTime` is the scene-relative time of the frame
	// that crossed it, not the event's authored start.
	virtual void StartEvent(const FElysiumSceneData& Scene, const FElysiumSceneEvent& Event, float SceneTime) = 0;
	// Still inside the event's range this frame. Only events with a real end time ever continue.
	virtual void ProcessEvent(const FElysiumSceneData& Scene, const FElysiumSceneEvent& Event, float SceneTime) {}
	// The clock left the event's range (or the scene stopped while it was running).
	virtual void EndEvent(const FElysiumSceneData& Scene, const FElysiumSceneEvent& Event, float SceneTime) {}
};

class FElysiumScenePlayer
{
public:
	// Bind a scene and reset the clock to zero. Latency is the audio mixahead (see AdvanceTo).
	void Begin(TSharedPtr<const FElysiumSceneData> InScene, float InLatency, float InMaxDuration);

	// Drop every latch and return the clock to zero, without calling back. Used by a re-Start and
	// by the completion path (VtMB's CChoreoScene::ResetSimulation).
	void Reset();

	// Advance the clock to an absolute scene time and dispatch what that crossing implies.
	// SceneTime is elapsed seconds since the scene started — an absolute value, not a delta, so it
	// cannot drift (VtMB's map-scene clock is `curtime - m_flStartTime`, re19_think.txt).
	void AdvanceTo(float SceneTime, IElysiumChoreoCallback& Callback);

	// Call EndEvent on everything still running and clear the latches. This is what stops a voice
	// or an animation when a scene is cancelled or finishes mid-event.
	void StopActiveEvents(IElysiumChoreoCallback& Callback);

	// VtMB's SimulationFinished: past the last event AND nothing still active. Both halves matter —
	// an instantaneous event at the very end still has to be dispatched before the scene may end.
	bool IsFinished() const { return CurrentTime > EffectiveLatestTime && ActiveCount == 0; }

	// Mark every event whose start has already passed as started-but-not-active, without calling
	// back. This is how a save restored mid-scene resumes without re-firing what already fired.
	void PreLatchTo(float SceneTime);

	bool  IsBound() const { return Scene.IsValid(); }
	float GetTime() const { return CurrentTime; }
	float GetLatest() const { return EffectiveLatestTime; }
	int32 ActiveEvents() const { return ActiveCount; }
	const FElysiumSceneData* GetScene() const { return Scene.Get(); }

private:
	// A speak event's start is pulled earlier by the mixahead so the sound reaches the ear on time;
	// the scene's end is pushed later by the same amount. Every other type is untouched.
	float EffectiveStart(const FElysiumSceneEvent& Event) const;

	TSharedPtr<const FElysiumSceneData> Scene;
	TBitArray<> bStarted;
	TBitArray<> bActive;
	float CurrentTime = 0.f;
	float Latency = 0.f;
	float EffectiveLatestTime = 0.f;
	int32 ActiveCount = 0;
};
