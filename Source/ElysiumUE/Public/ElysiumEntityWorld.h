#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityHandle.h"
#include "ElysiumEventQueue.h"
#include "ElysiumIOSink.h"
#include "ElysiumVariant.h"

class AActor;
class UElysiumBrushComponent;
class UElysiumGameStateSubsystem;

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
	// way outputs become queue entries.
	void FireOutput(FElysiumEntity& Source, FName OutputName, const FElysiumEntityHandle& Activator);

	// Debug/console injection (P2.2 inspector fire buttons, P2.3 `ent_fire`): queue a hand-made
	// input delivery through the real event queue (chokepoint 2) at now + delay — the same code
	// path a game output takes, so manual tests are faithful, show up in the queue window, and are
	// single-steppable. Targeting one specific entity uses Target "!self" with Caller = its handle.
	void EnqueueInput(const FString& Target, FName Input, const FElysiumVariant& Param, double Delay,
		const FElysiumEntityHandle& Activator, const FElysiumEntityHandle& Caller);

	// Overlap routing (P1.5): a brush body's begin/end overlap lands here. Resolve the brush
	// entity, skip if inert (R6), and call its OnTouchStart/OnTouchEnd (P1.6 triggers override).
	void RouteBrushTouch(const FElysiumEntityHandle& Brush, const FElysiumEntityHandle& Activator, bool bBegin);

	// Debug tap seam (P2.3 `ent_*`): install an extra I/O sink, owned by the world and torn down
	// with it. The ent_* debug subsystem taps the two chokepoints for its overlay/break tooling
	// through the same sink interface the ring buffer and log stream already use — no I/O side
	// channel (R5). Re-installed by the subsystem whenever a new world epoch appears.
	void AddSink(TUniquePtr<IElysiumIOSink> InSink);

	// --- Resolution / iteration --------------------------------------------------------
	FElysiumEntity* Resolve(const FElysiumEntityHandle& Handle);
	const FElysiumEntity* Resolve(const FElysiumEntityHandle& Handle) const;
	FElysiumEntity* FindByName(const FString& Name);   // first live match, or null
	void ForEachNamed(FName Name, TFunctionRef<void(FElysiumEntity&)> Fn);

	int32 NumEntities() const { return EntityList.Num(); }
	const TArray<TUniquePtr<FElysiumEntity>>& Entities() const { return EntityList; }
	const FElysiumEventQueue& Queue() const { return EventQueue; }
	FElysiumEventQueue& Queue() { return EventQueue; }
	const FElysiumRingBufferSink& RingBuffer() const { return *Ring; }
	uint32 GetEpoch() const { return Epoch; }
	AActor* GetOwnerActor() const { return Owner; }
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

	// Brush bodies (P1.5): the map actor owns them (they are its components); we hold weak refs to
	// gate them and to destroy them on teardown (the world logically owns the embodiments).
	TArray<TWeakObjectPtr<UElysiumBrushComponent>> Bodies;
	int32 TouchBeginCount = 0;
	int32 TouchEndCount = 0;

	// Unknown target/input aggregation: log once per unique (target.Input), count the rest.
	TSet<FString> UnknownLogged;
	int32 UnknownTargetCount = 0;
	int32 UnknownInputCount = 0;
};
