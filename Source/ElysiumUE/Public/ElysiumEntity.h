#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntityHandle.h"
#include "ElysiumVariant.h"

struct FElysiumEntityDef;
struct FElysiumClassDesc;

// Next-think sentinel: an entity with this next-think never runs Think(). Matches VtMB's
// `0x7f7fffff` (FLT_MAX) write in CBaseEntity::ScriptHide (entity_io.md).
inline constexpr float ELYSIUM_NEVER_THINK = FLT_MAX;

// The context an input carries to its thunk. `Param` is the marshalled parameter string
// (field 2 of the output); `Activator`/`Caller` are the I/O provenance the entity world
// resolves in P1.4 (Invalid for a hand-fired input). Kept a struct so P1.4 can grow the
// context without re-signing every registered thunk.
struct FElysiumInputArgs
{
	FElysiumVariant Param;
	FElysiumEntityHandle Activator;
	FElysiumEntityHandle Caller;
};

// R1 — a live entity is a plain C++ object: no UObject, no actor, no reflection. Unreal
// actors/components are optional *bodies* attached in P1.5 for rendering/physics/overlap;
// all game state lives here. This base owns identity (R3), the CBaseEntity keyfield
// contract (python_bridge.md), the whole-entity dormancy switch (R6), and the three base
// inputs (Kill/ScriptHide/ScriptUnhide) that reach every subclass through the class chain
// (R2). Leaf classes (P1.6+) derive from this and register their own inputs/fields.
class FElysiumEntity
{
public:
	FElysiumEntity() = default;
	virtual ~FElysiumEntity() = default;

	// Identity is a world slot; an entity is neither copied nor moved.
	FElysiumEntity(const FElysiumEntity&) = delete;
	FElysiumEntity& operator=(const FElysiumEntity&) = delete;

	// --- Identity / data ---------------------------------------------------------------
	// Handle/Def/Class are bound by the registry at Construct; Def and Class outlive the
	// entity (the def array is the map asset, the descriptor is module-static).
	FElysiumEntityHandle Handle;
	const FElysiumEntityDef* Def = nullptr;
	const FElysiumClassDesc* Class = nullptr;   // resolved descriptor; base desc when a record
	bool bRecordOnly = false;                   // no leaf class registered — inert record (set by the registry)

	// --- Base keyfields — the CBaseEntity contract (python_bridge.md) -------------------
	// Registered once on the base class field table; every subclass inherits them through
	// the chain walk. These are the live, writable copies (TargetName/Model mirror the def).
	FString TargetName;
	FString Target;
	FString Model;
	int32   SpawnFlags = 0;
	int32   Health = 0;
	int32   MaxHealth = 0;
	int32   Flags = 0;
	FVector Velocity = FVector::ZeroVector;
	FVector AngularVelocity = FVector::ZeroVector;   // avelocity
	FVector BaseVelocity = FVector::ZeroVector;      // basevelocity
	FVector Angles = FVector::ZeroVector;
	float   Gravity = 0.0f;
	float   Friction = 0.0f;
	float   LocalTime = 0.0f;                        // ltime
	int32   WaterLevel = 0;
	int32   WaterType = 0;
	FString SoundGroup;                              // soundgroup
	FString UseScript;                               // usescript — the level-script module
	bool    bNpcTransparent = false;                 // npc_transparent
	bool    bBlocksTraces = false;                   // blocks_traces
	FString DamageFilterName;                        // dmg_filter_name
	FString UseFilterName;                           // use_filter_name

	// --- Dormancy (R6) + liveness (R3) -------------------------------------------------
	// ScriptHide/StartHidden is one reversible whole-entity OFF switch: non-solid, undrawn,
	// next-think = never — all together. `bDead` (Kill) is terminal; the world reaps the
	// slot and bumps handles in P1.4. NextThink is absolute game seconds (ELYSIUM_NEVER_THINK
	// = never); the queue services thinks in P1.4.
	bool  bHidden = false;
	bool  bDead = false;
	float NextThink = ELYSIUM_NEVER_THINK;

	// --- Output firing state (R2) ------------------------------------------------------
	// The runtime `times` countdown, one entry per Def->Outputs row (the def is immutable, so
	// the mutable counter lives here). Seeded from each row's `Times` at Construct: -1 stays
	// unlimited, N counts down to 0 (spent). The entity world decrements it as it fires outputs.
	TArray<int32> OutputTimesRemaining;

	bool IsHidden() const { return bHidden; }
	bool IsDead() const { return bDead; }
	bool IsRecordOnly() const { return bRecordOnly; }
	// Fully OFF for game purposes: cannot be touched, traced, used, or thought.
	bool IsInert() const { return bDead || bHidden; }

	// --- Base inputs (reach every class through the chain) -----------------------------
	void Kill();          // terminal: mark dead + go inert (world reaps in P1.4)
	void ScriptHide();    // whole-entity OFF (saves prior think; body gated in P1.5)
	void ScriptUnhide();  // the exact inverse

	// --- Lifecycle --------------------------------------------------------------------
	// Bind identity and copy the base keyfields out of the def's raw keys through the class
	// chain field table (R2), honouring start_hidden. The world's spawn pass (P1.4) calls
	// this, then Spawn(); a leaf class overrides Spawn() for its own wiring.
	void Construct(const FElysiumEntityDef& InDef, FElysiumEntityHandle InHandle, const FElysiumClassDesc& InClass);
	virtual void Spawn() {}
	virtual void Think() {}

	// Body hook (P1.5): mirror dormancy onto the attached body's collision + visibility.
	// No-op while an entity has no body (all of P1.3).
	virtual void OnDormancyChanged() {}

	// `#<idx> <targetname>(<classname>)` — the canonical debug string (R3), used everywhere.
	FString DebugString() const;

protected:
	float SavedNextThink = ELYSIUM_NEVER_THINK;   // restored by ScriptUnhide
};
