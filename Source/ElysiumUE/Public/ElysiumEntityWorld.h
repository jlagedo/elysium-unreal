#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityHandle.h"
#include "ElysiumEventQueue.h"
#include "ElysiumIOSink.h"
#include "ElysiumVariant.h"

class AActor;
class APawn;
class UElysiumAudioSubsystem;
class UElysiumBrushComponent;
class UElysiumGameStateSubsystem;
class UElysiumMapSubsystem;

// R1/R5 — the Track-B substrate: one plain-C++ object per map, owned by AElysiumMapActor, that
// dies with it. It parses `.ents` into live entities, indexes them by name and class, and routes
// every input delivery and every deferred output through the two chokepoints (AcceptInput and the
// event queue) with the debug sinks always installed. It is ticked once per frame with the game
// clock's `now`, think-first (retail order): run due thinks, then service the queue.
//
// Identity (R3) is generation-checked: each world instance takes a unique epoch, every handle it
// mints carries that epoch, and Resolve returns null for a stale-epoch, out-of-range, or dead
// handle — the "falsy when dead/stale" contract VtMB scripts rely on. Teardown bumps the epoch,
// invalidating all outstanding handles at once.
class FElysiumEntityWorld
{
public:
	FElysiumEntityWorld(AActor* InOwner, UElysiumGameStateSubsystem* InGameState);
	~FElysiumEntityWorld();

	FElysiumEntityWorld(const FElysiumEntityWorld&) = delete;
	FElysiumEntityWorld& operator=(const FElysiumEntityWorld&) = delete;

	// --- Lifecycle ---------------------------------------------------------------------
	// Build one entity per def via the registry (inert record when the classname is
	// unregistered), index names/classes, run the spawn pass (Spawn() on each).
	void Load(FElysiumEntityDefs&& InDefs);

	// Per-frame drive (map actor Tick). Think-first: RunThinks(Now) then ServiceEvents(Now).
	void Tick(double Now);

	// --- Chokepoints (R5) --------------------------------------------------------------
	// Deliver an input to a target: resolve `!self`/`!activator`, fan out over the name index,
	// walk each target's class-chain input table (case-folded), invoke the thunk, notify sinks.
	// Unknown target/input: notify (log-once) and keep going. The only input path in the game.
	void AcceptInput(const FString& Target, FName Input, const FElysiumVariant& Param,
		const FElysiumEntityHandle& Activator, const FElysiumEntityHandle& Caller);
	// Fire a named output from an entity: for each matching def row whose `times` is not spent,
	// count it down and queue the delivery at now + delay (attaching field-6 Python). The only
	// way outputs become queue entries. `ValueOverride` (P4.5) is a Source COutput<T> runtime value:
	// it fills any wire whose map-authored param is empty; a Void override leaves the empty param.
	void FireOutput(FElysiumEntity& Source, FName OutputName, const FElysiumEntityHandle& Activator,
		const FElysiumVariant& ValueOverride = FElysiumVariant::Void());

	// Debug/console injection (P2.2 inspector fire buttons, P2.3 `ent_fire`): queue a hand-made
	// input delivery through the real event queue (chokepoint 2) at now + delay — the same code
	// path a game output takes, so manual tests are faithful, show up in the queue window, and are
	// single-steppable. Targeting one specific entity uses Target "!self" with Caller = its handle.
	void EnqueueInput(const FString& Target, FName Input, const FElysiumVariant& Param, double Delay,
		const FElysiumEntityHandle& Activator, const FElysiumEntityHandle& Caller);

	// P5 5.4 — ScheduleTask(delay, "<source>"): defer a field-6 Python source string on the same
	// event queue, evaluated at now+delay through the installed script host (DeliverEvent's Python
	// half). No I/O target — it is a python-only event, exactly the shape a field-6-only output
	// produces — so it single-steps in the queue window and serializes into a save (R8). The source
	// resolves against the delivery's provenance (Caller = the scheduling entity, `!self`).
	void EnqueuePython(const FString& Source, double Delay,
		const FElysiumEntityHandle& Activator, const FElysiumEntityHandle& Caller);

	// P5 5.4 — the logic_pythoncheck / condition path: evaluate an expression string through the
	// installed script host with the given provenance and return its value (Void when the source is
	// empty, there is no host/state, or the eval failed — error-to-false, so a gate over it reads
	// OnFalse). Goes through the host (not ElysiumExpr directly) so `elysium.script.live 0` disables
	// conditions in lockstep with field-6 for A/B, and the eval lands in the recent-eval debug log.
	FElysiumVariant EvalCondition(const FString& Source, const FElysiumEntityHandle& Self,
		const FElysiumEntityHandle& Activator);

	// Overlap routing (P1.5): a brush body's begin/end overlap lands here. Resolve the brush
	// entity, skip if inert (R6), and call its OnTouchStart/OnTouchEnd (P1.6 triggers override).
	void RouteBrushTouch(const FElysiumEntityHandle& Brush, const FElysiumEntityHandle& Activator, bool bBegin);

	// +use look-cursor (P4.2): the minimal reticle slice. UpdateUseCursor camera-ray-picks the
	// nearest usable, non-inert brush entity within arm's reach and arbitrates OnIn/OnOut as the aim
	// enters/leaves it (fired through the entities, so a StartHidden→ScriptUnhide arm gates it for
	// free); PlayerUse presses whatever the cursor is on. Driven from AElysiumMapActor::Tick and the
	// pawn's E key. The full use-only channel + use-icon HUD are P4.4; this lands activation + OnIn/
	// OnOut. Player is not an entity yet (P4-later), so the activator is Invalid, matching triggers.
	void UpdateUseCursor();
	void PlayerUse();
	FElysiumEntityHandle GetAimedUsable() const { return AimedUsable; }
	// The reticle icon index for the currently aimed usable (0 = nothing usable aimed at). The HUD
	// (P4.4) reads this each frame to pick the atlas cell; resolves locked -> locked_icon (GetUseIcon).
	int32 GetAimedUseIcon() const;

	// --- Screen fade (P4.5 env_fade) ---------------------------------------------------
	// A full-screen colour fade driven by env_fade's `Fade` input, advanced off the game clock and
	// drawn by AElysiumHUD (which polls GetScreenFade each frame). Held on the world so it dies with
	// the map. One active fade at a time — a new Fade replaces the running one, matching Source's
	// single g_pScreenFade. `bReverse` = SF_FADE_IN (reveal: start opaque, clear over Duration);
	// `bStayOut` = SF_FADE_STAYOUT (hold at MaxAlpha after covering, never fade back).
	void StartScreenFade(const FLinearColor& Color, float Duration, float HoldTime, float MaxAlpha,
		bool bReverse, bool bStayOut);
	// Fills OutColor (rgb = fade colour, a = current 0..1 alpha) and returns true while a fade is
	// visible; false when idle. Const — the HUD polls it; a finished non-stayout fade reports idle.
	bool GetScreenFade(FLinearColor& OutColor) const;

	// The player's pawn via the owning world's first controller, or null. The seam point_teleport /
	// trigger_hurt use to reach the player from the plain-C++ substrate (the player is not an entity).
	APawn* GetPlayerPawn() const;

	// Debug tap seam (P2.3 `ent_*`): install an extra I/O sink, owned by the world and torn down
	// with it. The ent_* debug subsystem taps the two chokepoints for its overlay/break tooling
	// through the same sink interface the ring buffer and log stream already use — no I/O side
	// channel (R5). Re-installed by the subsystem whenever a new world epoch appears.
	void AddSink(TUniquePtr<IElysiumIOSink> InSink);

	// Visual-change seam (P2.4 retained gizmo layer): a callback fired whenever an entity's
	// dormancy/liveness flips (from FElysiumEntity::OnDormancyChanged / Kill). It lets a retained
	// visualizer dirty just that one instance on the event instead of polling every entity every
	// frame. Optional (unset in normal play); the debug subsystem sets it per epoch. Called by the
	// entity through its World back-pointer.
	void SetVisualChangedHook(TFunction<void(const FElysiumEntity&)> Hook) { VisualChangedHook = MoveTemp(Hook); }
	void NotifyVisualChanged(const FElysiumEntity& Ent) const { if (VisualChangedHook) { VisualChangedHook(Ent); } }

	// --- Resolution / iteration --------------------------------------------------------
	FElysiumEntity* Resolve(const FElysiumEntityHandle& Handle);
	const FElysiumEntity* Resolve(const FElysiumEntityHandle& Handle) const;
	FElysiumEntity* FindByName(const FString& Name);   // first live match, or null
	// First live info_landmark with this targetname (the P4.6 landmark-transition anchor), or null.
	FElysiumEntity* FindLandmark(const FString& Name);
	void ForEachNamed(FName Name, TFunctionRef<void(FElysiumEntity&)> Fn);

	int32 NumEntities() const { return EntityList.Num(); }
	const TArray<TUniquePtr<FElysiumEntity>>& Entities() const { return EntityList; }
	const FElysiumEventQueue& Queue() const { return EventQueue; }
	FElysiumEventQueue& Queue() { return EventQueue; }
	const FElysiumRingBufferSink& RingBuffer() const { return *Ring; }
	uint32 GetEpoch() const { return Epoch; }
	AActor* GetOwnerActor() const { return Owner; }
	// The GameInstance audio subsystem (voice pool + decode cache), or null. The seam ambient_generic
	// and the SoundScheme manager use to reach playback from the plain-C++ substrate.
	UElysiumAudioSubsystem* AudioSubsystem() const;
	// The map-lifecycle subsystem (Travel + the P4.6 deferred landmark-transition queue), or null.
	// trigger_changelevel reaches it through here to request a travel to another map.
	UElysiumMapSubsystem* MapSubsystem() const;
	double NowSeconds() const;
	int32 UnknownTargets() const { return UnknownTargetCount; }
	int32 UnknownInputs() const { return UnknownInputCount; }
	int32 NumBrushBodies() const { return Bodies.Num(); }
	int32 TouchBegins() const { return TouchBeginCount; }
	int32 TouchEnds() const { return TouchEndCount; }

	// --- Formatting (used by the sinks; resolves handles to the canonical debug string) ----
	// `#<idx> <name>(<class>)` for a handle, or `#<null>` / `#<stale>` when it cannot resolve.
	FString DescribeHandle(const FElysiumEntityHandle& Handle) const;
	// `(t) <caller> -> <target>.Input(param)` plus a `[py]`/`[no target]`/`[no input]` note.
	FString FormatEventLine(double Now, const FElysiumIOEvent& Event, const FString& TargetLabel,
		const TCHAR* Note) const;

private:
	void Teardown();
	// Build the brush body (P1.5) for one entity, if it is a brush with hulls: cook the convex
	// UBodySetup from the def, place it at the def origin, attach it to the owner actor, store it
	// on the entity, and start it dormant when born hidden. Point/logic entities get no body (R1).
	void BuildBrushBody(FElysiumEntity& Ent);
	// The queue.Add wrapper: assigns time/serial upstream, notifies OnQueued.
	void AddEvent(FElysiumIOEvent&& Event);
	void ServiceEvents(double Now);
	void RunThinks(double Now);
	void DeliverEvent(const FElysiumIOEvent& Event, double Now);
	// Resolve a due event's target string to live entities (skips dead), honouring !self/!activator.
	void ResolveTargets(const FElysiumIOEvent& Event, TArray<FElysiumEntity*>& Out);

	AActor* Owner = nullptr;                          // for VLOG; not owned
	UElysiumGameStateSubsystem* GameState = nullptr;  // clock + script host; outlives the world
	uint32 Epoch = 0;

	FElysiumEntityDefs Defs;
	TArray<TUniquePtr<FElysiumEntity>> EntityList;    // parallel to Defs.Defs; index = handle index
	TMultiMap<FName, int32> NameIndex;                // targetname -> entity index (non-unique)
	TMultiMap<FName, int32> ClassIndex;               // classname  -> entity index

	FElysiumEventQueue EventQueue;
	TArray<TUniquePtr<IElysiumIOSink>> Sinks;
	FElysiumRingBufferSink* Ring = nullptr;           // owned in Sinks; the always-on history
	TFunction<void(const FElysiumEntity&)> VisualChangedHook;   // P2.4 gizmo dirty seam (debug-only)

	// Brush bodies (P1.5): the map actor owns them (they are its components); we hold weak refs to
	// gate them and to destroy them on teardown (the world logically owns the embodiments).
	TArray<TWeakObjectPtr<UElysiumBrushComponent>> Bodies;
	int32 TouchBeginCount = 0;
	int32 TouchEndCount = 0;

	// The usable brush entity currently under the +use look-cursor (P4.2), or Invalid when the aim
	// is off every usable body / out of reach. OnIn/OnOut fire on the transitions of this handle.
	FElysiumEntityHandle AimedUsable;

	// P4.5 env_fade screen-fade state (one at a time). GetScreenFade derives the current alpha from
	// NowSeconds() against StartTime, so no per-frame advance is needed; a finished non-stayout fade
	// simply reports idle (bActive stays set but the phase math returns false past its return ramp).
	struct FScreenFade
	{
		bool         bActive = false;
		FLinearColor Color = FLinearColor::Black;
		float        MaxAlpha = 1.0f;   // 0..1 (renderamt/255)
		float        Duration = 0.0f;   // fade-in (and fade-back) seconds
		float        HoldTime = 0.0f;   // seconds held at MaxAlpha before fading back
		bool         bReverse = false;  // SF_FADE_IN: reveal (opaque -> clear), no hold/return
		bool         bStayOut = false;  // SF_FADE_STAYOUT: hold at MaxAlpha forever after covering
		double       StartTime = 0.0;
	};
	FScreenFade ScreenFade;

	// Unknown target/input aggregation: log once per unique (target.Input), count the rest.
	TSet<FString> UnknownLogged;
	int32 UnknownTargetCount = 0;
	int32 UnknownInputCount = 0;
};
