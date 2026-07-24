#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntityHandle.h"
#include "ElysiumVariant.h"

struct FElysiumEntityDef;
struct FElysiumClassDesc;
class FElysiumEntityWorld;
class UElysiumBrushComponent;
class FElysiumDoorBase;

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

	// The world that owns this entity, bound at Load before Spawn(). The seam an entity uses to
	// reach world services — firing outputs (FireOutput) above all. Null on a throwaway probe
	// entity (elysium.classes), so every world call guards on it.
	FElysiumEntityWorld* World = nullptr;

	// The brush body (P1.5), or null for point/logic entities (R1: logic ents never get a body).
	// Owned by the map actor; the entity only gates its collision on dormancy (R6). Non-owning.
	UElysiumBrushComponent* Body = nullptr;

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
	// The live origin, seeded from Def->Origin at Construct (the def's is immutable). GetOrigin and
	// the body placement read this, so SetOrigin moves the entity for real (VtMB's Entity.SetOrigin).
	FVector Origin = FVector::ZeroVector;
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
	int32   UseIcon = 0;                             // use_icon — reticle icon index (1-based; 0 = none)
	int32   LockedIcon = 0;                          // locked_icon — reticle icon when use-locked

	// --- Dormancy (R6) + liveness (R3) -------------------------------------------------
	// ScriptHide/StartHidden is one reversible whole-entity OFF switch: non-solid, undrawn,
	// next-think = never — all together. `bDead` (Kill) is terminal; the world reaps the
	// slot and bumps handles in P1.4. NextThink is absolute game seconds (ELYSIUM_NEVER_THINK
	// = never); the queue services thinks in P1.4.
	bool  bHidden = false;
	bool  bDead = false;
	// Spawn() has run. The world's spawn pass and the two-phase runtime create (CreateEntityNoSpawn
	// → CallEntitySpawn) both gate on this so an entity is never Spawn()'d twice.
	bool  bSpawnCalled = false;
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

	// Fire a named output through the world (R2 → the event queue). No-op on a worldless probe
	// entity. Leaf classes (P1.6+) call this from their inputs and touch hooks.
	void FireOutput(FName Output, const FElysiumEntityHandle& Activator);

	// Value-carrying variant (P4.5): a Source COutput<T> fires with a runtime value that fills any
	// wire whose map-authored param is empty (math_counter OutValue, logic_case OnCaseNN, …). Wires
	// that DID specify a param keep their override. `Value` is ignored (Void) by the plain overload.
	void FireOutput(FName Output, const FElysiumEntityHandle& Activator, const FElysiumVariant& Value);

	// --- Runtime writers (9.3 — VtMB's Entity.SetOrigin/SetAngles/SetModel) ------------
	// Scripts move, re-face, and re-skin live entities. These mutate the authoritative field
	// (so GetOrigin/GetAngles/GetModelName reflect it and other entities' logic reads it), then
	// hand off to the body-follow hook. SetName re-keys the world name index and so lives on the
	// world (FElysiumEntityWorld::RenameEntity), not here.
	void SetRuntimeOrigin(const FVector& NewOrigin);
	void SetRuntimeAngles(const FVector& NewAngles);
	void SetRuntimeModel(const FString& NewModel);

	// Body-follow hooks the runtime writers call after mutating the field. Base: a brush/point
	// entity's body is static, so only notify the visualizer (the field is what logic reads).
	// A leaf with a movable body (FElysiumNpc) overrides OnRuntimeTransformChanged to move/re-face
	// its skeletal component and OnRuntimeModelChanged to rebuild it with the new model.
	virtual void OnRuntimeTransformChanged();
	virtual void OnRuntimeModelChanged() {}

	// Overlap terminus (P1.5 routing): a brush body's begin/end overlap lands here. Base no-op;
	// P1.6 trigger classes override to fire OnStartTouch/OnEndTouch (respecting spawnflags).
	virtual void OnTouchStart(const FElysiumEntityHandle& Activator) {}
	virtual void OnTouchEnd(const FElysiumEntityHandle& Activator) {}

	// --- +use look-cursor terminus (P4.2) ----------------------------------------------
	// The minimal look-cursor the entity world runs each frame (a camera-ray pick against the
	// usable brush bodies in range) routes here. Base is un-usable and no-ops; func_button (P4.2)
	// overrides IsUsable and translates cursor enter/leave into its OnIn/OnOut outputs and a +use
	// (or the E key / a fired Press input) into a press. The full use-only trace channel + the
	// use-icon HUD land in P4.4 — this is the activation + OnIn/OnOut slice it builds on.
	virtual bool IsUsable() const { return false; }
	virtual void OnUseCursorEnter() {}                                  // look-cursor entered (OnIn)
	virtual void OnUseCursorLeave() {}                                  // look-cursor left (OnOut)
	virtual void Use(const FElysiumEntityHandle& Activator) {}          // +use / Press pressed it

	// The reticle icon this entity shows while it is the +use look-cursor target (P4.4). VtMB's
	// GetUseIcon (FUN_100c8940) returns locked_icon when the locked byte +0x5c4 is set, else
	// use_icon; IsUseLocked() is the leaf's locked flag (doors/buttons). 0 = draw no icon.
	virtual bool IsUseLocked() const { return false; }
	int32 GetUseIcon() const { return (IsUseLocked() && LockedIcon != 0) ? LockedIcon : UseIcon; }

	// --- Debug introspection (P4.3) ----------------------------------------------------
	// Runtime, non-keyfield state a leaf class wants surfaced in the Cog inspector's "Live state"
	// section (mover toggle-state, current move, resolved links, spawnflag decode) — the fields the
	// registry tables don't carry because they are internal state, not keyvalues. Base emits nothing.
	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const {}

	// The primitive body a physics constraint (phys_hinge) should attach to (8.4). Base returns the
	// brush body (a movable brush entity can be constrained); a physics prop overrides to its
	// simulating static-mesh body. Null = nothing to constrain (attach to world / skip).
	virtual class UPrimitiveComponent* GetAttachBody() const;

	// No-RTTI downcast to the door base (UE builds compile without RTTI, so no dynamic_cast). Base
	// returns null; FElysiumDoorBase overrides to return itself, so a resolved `linked_door` name can
	// be recognised as a door without reflection.
	virtual FElysiumDoorBase* AsDoorBase() { return nullptr; }

	// --- Lifecycle --------------------------------------------------------------------
	// Bind identity and copy the base keyfields out of the def's raw keys through the class
	// chain field table (R2), honouring start_hidden. The world's spawn pass (P1.4) calls
	// this, then Spawn(); a leaf class overrides Spawn() for its own wiring.
	void Construct(const FElysiumEntityDef& InDef, FElysiumEntityHandle InHandle, const FElysiumClassDesc& InClass);
	virtual void Spawn() {}

	// Second-phase init, run after EVERY entity on the map has Spawn()'d (Source's Activate()
	// pass). A constraint (phys_hinge) resolves and wires its attached bodies here, because they
	// must already exist — a Spawn()-time resolve would race the def order. Base no-op.
	virtual void PostSpawn() {}

	virtual void Think() {}

	// Body hook (R6): mirror dormancy onto the attached body's collision. No-op while an entity
	// has no body (all point/logic entities).
	virtual void OnDormancyChanged();

	// `#<idx> <targetname>(<classname>)` — the canonical debug string (R3), used everywhere.
	FString DebugString() const;

protected:
	float SavedNextThink = ELYSIUM_NEVER_THINK;   // restored by ScriptUnhide
};
