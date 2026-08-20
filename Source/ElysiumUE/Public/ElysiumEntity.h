#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "ElysiumEntityHandle.h"
#include "ElysiumInteraction.h"
#include "ElysiumNpcMindTypes.h"
#include "ElysiumVariant.h"

struct FElysiumEntityDef;
struct FElysiumClassDesc;
struct FElysiumSaveArchive;
class FElysiumEntityWorld;
class UElysiumBrushComponent;
class FElysiumDoorBase;
class FElysiumLockableEntity;

// Next-think sentinel: an entity with this next-think never runs Think(). Matches VtMB's
// `0x7f7fffff` (FLT_MAX) write in CBaseEntity::ScriptHide (entity_io.md).
inline constexpr float ELYSIUM_NEVER_THINK = FLT_MAX;

// How a `scripted_sequence` sends its NPC to the mark — `m_fMoveTo`'s travelling values. 0 ("No")
// and 4 ("Instantaneous") never reach the seam: the first touches nothing, the second is a
// placement.
enum class EElysiumScriptGait : uint8
{
	Face,     // 5 "No - Turn to Face" — take the mark's angles without travelling
	Walk,     // 1
	Run,      // 2
	Custom,   // 3 — travel playing `m_iszCustomMove`'s own cycle instead of the gait activity
};

// Where an accepted scripted move stands. `Unsupported` is what an entity that never accepted one
// answers, and what a released move reads as.
enum class EElysiumScriptMove : uint8
{
	Unsupported,
	Moving,
	Arrived,
	Failed,
};

// Why an owned entity left its owner's live set. `npc_maker` is the first user: a dead child
// consumes its finite slot and fires OnNPCDied, while removing a still-live child refunds it.
enum class EElysiumOwnedEntityTermination : uint8
{
	Died,
	RemovedAlive,
};

// The context an input carries to its thunk. `Param` is the marshalled parameter string
// (field 2 of the output); `Activator`/`Caller` are the I/O provenance the entity world
// resolves in P1.4 (Invalid for a hand-fired input). Kept a struct so P1.4 can grow the
// context without re-signing every registered thunk.
struct FElysiumInputArgs
{
	FElysiumVariant Param;
	FElysiumEntityHandle Activator;
	FElysiumEntityHandle Caller;
	// The name this input was dispatched under. A thunk is a captureless function pointer, so a
	// handler shared by several inputs — the stub reporter is the one that needs it — has no other
	// way to say which of them ran. Set by DeliverInputTo; empty on a hand-built probe.
	FName Input;
};

// One flex-controller write, by name. A face's only writable state is its 44 flex controllers;
// everything under them — the RPN rules, the flexdesc weights, the target ramps — is arithmetic
// re-derived on every write. A choreo scene's `expression` events compose a set of these out of a
// Faceposer weight table, and lipsync will compose one out of the phoneme table.
struct FElysiumFlexWrite
{
	FString Name;
	float Value = 0.f;
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
	// Generic skeletal embodiment for a model-backed point entity whose leaf supplied no body.
	// It carries no inferred interaction semantics; the world creates it between Spawn/PostSpawn.
	USkeletalMeshComponent* GenericModelBody = nullptr;

	// --- Base keyfields — the CBaseEntity contract (python_bridge.md) -------------------
	// Registered once on the base class field table; every subclass inherits them through
	// the chain walk. These are the live, writable copies (TargetName/Model mirror the def).
	FString TargetName;
	FString Target;
	FString ParentName;                            // parentname — carry this body with another entity
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
	FString SoundOverrideEntityName;                 // SetSoundOverrideEnt logical attachment owner
	bool    bFakeSilence = false;                    // SetFakeSilence gates direct/line playback
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
	// Activate() has run. Unlike PostSpawn(), this late phase is admitted only after the player has
	// reached its final frozen placement and the complete entity graph is available.
	bool  bActivateCalled = false;
	float NextThink = ELYSIUM_NEVER_THINK;
	// The resolved logical move parent. ParentName remains the authored key; the handle exists even
	// when either entity is deliberately bodiless, because gameplay parenting (for example the
	// point_teleport refusal) is independent of whether Unreal has components to attach.
	FElysiumEntityHandle MoveParent;

	// CBaseEntity ownership reduced to the lifecycle seam gameplay needs. The owner is assigned only
	// after a runtime child has spawned, and the notification latch makes death followed by Kill a
	// single transition. Maker NPCs persist both values in their leaf save block.
	FElysiumEntityHandle OwnerEntity;
	bool bOwnerTerminationNotified = false;

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
	void PlayDialogFile(const FString& AuthoredPath);
	void SetSoundOverrideEnt(const FString& EntityName);
	void SetFakeSilence(bool bEnabled);
	void SetOwnerEntity(const FElysiumEntityHandle& InOwner) { OwnerEntity = InOwner; }
	FElysiumEntityHandle GetOwnerEntity() const { return OwnerEntity; }

	// Fire a named output through the world (R2 → the event queue). No-op on a worldless probe
	// entity. Leaf classes (P1.6+) call this from their inputs and touch hooks.
	void FireOutput(FName Output, const FElysiumEntityHandle& Activator);

	// Value-carrying variant (P4.5): a Source COutput<T> fires with a runtime value that fills any
	// wire whose map-authored param is empty (math_counter OutValue, logic_case OnCaseNN, …). Wires
	// that DID specify a param keep their override. `Value` is ignored (Void) by the plain overload.
	void FireOutput(FName Output, const FElysiumEntityHandle& Activator, const FElysiumVariant& Value);

	// VtMB's `CBaseEntity::EyePosition()` — `GetAbsOrigin() + m_vecViewOffset`. A fixed per-entity
	// offset, NOT a bounds fraction and NOT a head bone: it is what a scripted `LookAtEntityEye`
	// aims at and what an NPC looks at across a conversation, so a gaze that used a head attachment
	// instead would drift with the animation where retail's holds still.
	//
	// The base is a point entity, whose offset is zero. The character chain overrides with the
	// standing view height; the player leaf additionally accounts for ducking.
	virtual FVector EyePosition() const { return Origin; }

	// --- Runtime writers (9.3 — VtMB's Entity.SetOrigin/SetAngles/SetModel) ------------
	// Scripts move, re-face, and re-skin live entities. These mutate the authoritative field
	// (so GetOrigin/GetAngles/GetModelName reflect it and other entities' logic reads it), then
	// hand off to the body-follow hook. SetName re-keys the world name index and so lives on the
	// world (FElysiumEntityWorld::RenameEntity), not here.
	void SetRuntimeOrigin(const FVector& NewOrigin);
	void SetRuntimeAngles(const FVector& NewAngles);
	void SetRuntimeTransform(const FVector& NewOrigin, const FVector& NewAngles);
	void SetRuntimeModel(const FString& NewModel);

	// Body-follow hooks the runtime writers call after mutating the field. Base: a brush/point
	// entity's body is static, so only notify the visualizer (the field is what logic reads).
	// A leaf with a movable body (FElysiumNpc) overrides OnRuntimeTransformChanged to move/re-face
	// its skeletal component and OnRuntimeModelChanged to rebuild it with the new model.
	virtual void OnRuntimeTransformChanged();
	virtual void OnRuntimeModelChanged();

	// Overlap terminus (P1.5 routing): a brush body's begin/end overlap lands here. Base no-op;
	// P1.6 trigger classes override to fire OnStartTouch/OnEndTouch (respecting spawnflags).
	virtual void OnTouchStart(const FElysiumEntityHandle& Activator) {}
	virtual void OnTouchEnd(const FElysiumEntityHandle& Activator) {}
	// Admission precedes the world's active-touch latch. A rejected observation must never suppress
	// a later real begin after a trigger is enabled or its activator filter changes.
	virtual bool CanBeginTouch(const FElysiumEntityHandle& Activator) const { return !IsInert(); }
	// CBaseFilter's virtual verdict. Ordinary entities pass so a stale/mistyped retail filter handle
	// remains non-blocking; registered filter leaves override this exact seam.
	virtual bool PassesFilter(const FElysiumEntityHandle& Activator) const { return true; }

	// --- Player interaction -------------------------------------------------------------
	// Spatial focus is separate from the class verb. The modern query supplies a context, the
	// substrate owns eligibility/session state, and scripted AcceptInput("Use") continues to call
	// Use directly without pretending it came from a player standing in front of the entity.
	virtual bool IsUsable() const { return false; }
	virtual bool CanPlayerFocus(const FElysiumUseContext& Context) const
	{
		return IsUsable() && !IsInert();
	}
	virtual FElysiumUseBeginResult BeginPlayerUse(const FElysiumUseContext& Context)
	{
		Use(Context.Activator);
		return FElysiumUseBeginResult::Completed();
	}
	virtual void EndPlayerUse(const FElysiumUseContext& Context, EElysiumUseEndReason Reason) {}
	virtual void OnUseCursorEnter() {}                                  // look-cursor entered (OnIn)
	virtual void OnUseCursorLeave() {}                                  // look-cursor left (OnOut)
	virtual void Use(const FElysiumEntityHandle& Activator) {}          // +use / Press pressed it

	// The reticle icon this entity shows while it is the +use look-cursor target (P4.4). VtMB's
	// GetUseIcon (FUN_100c8940) returns locked_icon when the locked byte +0x5c4 is set, else
	// use_icon; IsUseLocked() is the leaf's locked flag (doors/buttons). 0 = draw no icon.
	virtual bool IsUseLocked() const { return false; }
	int32 GetUseIcon() const { return (IsUseLocked() && LockedIcon != 0) ? LockedIcon : UseIcon; }
	// The HUD query normally needs only the entity's lock state. Lockables are the exception: while
	// locked they can publish a distinct key icon when this activator actually carries the authored
	// key. Keep that inventory-dependent rule on the entity rather than teaching presentation about
	// item ownership.
	virtual int32 ResolveUseIcon(const FElysiumEntityHandle& Activator) const { return GetUseIcon(); }

	// --- Debug introspection (P4.3) ----------------------------------------------------
	// Runtime, non-keyfield state a leaf class wants surfaced in the Cog inspector's "Live state"
	// section (mover toggle-state, current move, resolved links, spawnflag decode) — the fields the
	// registry tables don't carry because they are internal state, not keyvalues. Base emits nothing.
	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const {}
	// Non-null while this entity owns a scripted session that cannot be represented by a save
	// payload. Ordinary animation deliberately returns null; only explicit session owners override.
	virtual const TCHAR* SaveBlockReason() const { return nullptr; }

	// The primitive used for parentname/physics attachment. Base returns a brush body; rendered
	// point props return their standing component and physics props return their simulating body.
	// Null = nothing physical to attach.
	virtual class UPrimitiveComponent* GetAttachBody() const;
	void EnsurePlacedModelBody();

	// The skeletal body a camera shot's `Bone:` / `Attachment:` attach point resolves against (11.7),
	// and the bone lookup a look-at rig will want (P12). Base returns null; `FElysiumAnimating`
	// returns its standing `Visual`. Declared here for the same no-RTTI reason `GetAttachBody` is.
	virtual class USkeletalMeshComponent* GetSkeletalBody() const { return nullptr; }

	// --- The sequence-event chain (LIFE5) -------------------------------------------------
	// Walk this entity's playing clips one frame further along their own timelines and dispatch
	// whatever the interval contained. Called once per frame by the world's event pass, before the
	// thinks; the base is a no-op because only an entity that owns a skeletal body has a clip to
	// advance. Declared here for the same no-RTTI reason `GetSkeletalBody` is — the world's pass
	// walks `FElysiumEntity`s and must not know which leaves carry bodies.
	virtual void AdvanceAnimEvents() {}

	// Retail's virtual `HandleAnimEvent` `+0x40c`: one fired record, offered to the entity whose
	// clip declared it. True means this entity claimed the id and acted on it; false means it did
	// not, and the caller counts the record in the census instead.
	//
	// The base answers false for every id, which is exactly what retail's own empty handler bodies
	// do — two of the twenty recovered bodies accept nothing at all, the combat-weapon body and the
	// camera-NPC family (`docs/vtmb/animation_and_movers.md` → "Sequence events and native
	// dispatch"). Same no-RTTI rationale again: the dispatcher holds a `FElysiumEntity&`.
	virtual bool HandleAnimEvent(const struct FElysiumAnimEvent& Event) { return false; }

	// The animation seam (8.5). Play a named sequence on this entity's body, resolved through the
	// NPC clip manifest. Base answers false — only an entity that owns a skeletal body can play
	// one. Every script-facing animation call lands here: the `SetAnimation` input (21 sites), the
	// `SetGesture` Character method, and `scripted_sequence`'s `m_iszPlay`. Kept virtual on the base
	// so the scripting host can reach it without knowing the leaf type (the same no-RTTI reason
	// `GetAttachBody` is here). OutSeconds receives the clip's authored length — a
	// `scripted_sequence` times its `OnEndSequence` off it.
	virtual bool PlayAnimClip(const FString& ClipName, bool bLoop, float* OutSeconds = nullptr) { return false; }
	virtual bool PreloadAnimClip(const FString& ClipName) { return false; }

	// Hand the body back to its resting pose — the disposition idle 8.5 picked for it. What a
	// `scripted_sequence` does to its NPC on `CancelSequence`: VtMB returns the NPC to AI, which
	// idles it, and the stance idle is the closest thing this runtime has to that. Base answers false.
	virtual bool ResetAnimToIdle() { return false; }

	// --- The scripted-move seam (8.5) ---------------------------------------------------
	// Send this entity to a beat's mark under the script's ownership, travelling at `Gait`. Only a
	// character standing on a movement motor can travel, so the base answers false and the caller
	// places it on the mark instead — the supported path for the player stand-in, a bodiless
	// record, a disabled navigation graph and every headless test. `CustomClip` is
	// `m_iszCustomMove`, the travel cycle `EElysiumScriptGait::Custom` plays. Kept on the base for
	// the same no-RTTI reason `GetAttachBody` is.
	virtual bool BeginScriptMove(const FVector& Mark, const FVector& MarkAngles,
		EElysiumScriptGait Gait, const FString& CustomClip) { return false; }

	// Advance an accepted move by one beat tick, sampling the moved body back into this entity's
	// origin/angles. The owning beat calls this until it stops answering `Moving`; the pose is the
	// beat's business, so nothing here touches the animation.
	virtual EElysiumScriptMove AdvanceScriptMove() { return EElysiumScriptMove::Unsupported; }

	// Release the script's ownership: stop the motor and hand the body back to its own behaviour
	// (a parked patrol route or interesting-place search resumes). Leaves the pose alone.
	virtual void EndScriptMove() {}

	// The beat's claim on the body arbiter, held for the whole beat — from a successful
	// BeginSequence through travel, `m_iszPlay` and a held post-idle, to EndSequence, CancelSequence
	// or teardown. `BeginScriptMove`'s own claim nests inside this one and shares its token, so the
	// arrival that ends the travel does not hand the body back mid-beat. Base answers true: an entity
	// with no arbiter — the `!playercontroller` stand-in, a bodiless record — has nothing to take,
	// which is a claim that succeeded. A refused claim never refuses the beat; the caller logs it and
	// runs on.
	virtual bool ClaimScriptBody(const TCHAR* Reason) { return true; }
	virtual void ReleaseScriptBody(const TCHAR* Reason) {}
	// Dialogue supersedes a `scripted_sequence` that still owns this body. The NPC calls this on
	// its ScriptOwner before acquiring the dialogue token; only the owning sequence accepts the
	// matching body handle. Base false also covers choreographed-scene claims, which have their own
	// cast lifetime rather than CCineNPC's CancelSequence transition.
	virtual bool CancelScriptedSequenceForDialogue(const FElysiumEntityHandle& NpcHandle) { return false; }

	// --- Dialogue body ownership (K7) --------------------------------------------------
	// The open world session holds this token beside its camera handle. Only the NPC leaf backs
	// these calls; keeping the seam on the base avoids RTTI and lets replacement/teardown release
	// exactly the resolved owner. A null token means acquisition was refused.
	virtual FElysiumBodyOwnerToken BeginDialogueBodySession() { return FElysiumBodyOwnerToken(); }
	// Silent release also clears the NPC's dialogue latch. Normal release leaves it for the queued
	// EndDialog input, preserving OnDialogEnd ordering through the one event transport.
	virtual void EndDialogueBodySession(const FElysiumBodyOwnerToken& Token, bool bSilent) {}

	// --- Body state a cutscene borrows (entity_io.md, choreographed_scenes.md) -----------
	// A choreographed scene with `position_start 1` places its cast once and then immobilises it:
	// VtMB's FUN_10081ed0 follows the placement with SetMoveType(MOVETYPE_NONE), SetSolid(SOLID_NONE)
	// and AddSolidFlags(FSOLID_NOT_SOLID), restoring all four at OnSceneFinished. The actors hold
	// position because they cannot move, not because anything rewrites their transform — so this is
	// deliberately NOT ScriptHide, which would also undraw a cast that has to stay on camera.
	virtual void SetBodyFrozen(bool bFrozen) {}

	// `scripted_sequence` spawnflag 4096 ORs troika bit 0x40 for the beat's duration, which makes
	// CBaseAnimating::IsIgnoreCollisionEntity answer true for every NPC and for the player. It is
	// how five NPCs walk sp_theatre's aisle past each other without jamming. World collision is
	// unaffected — only character-vs-character.
	virtual void SetIgnoreCharacterCollision(bool bIgnore) {}

	// --- Scripted-beat ownership (VtMB's m_pCine) ---------------------------------------
	// The `scripted_sequence` currently driving this entity, and whether that owner refuses to be
	// kicked out of the queue (spawnflag 512, or an authored `m_iszNextScript`). Claimed on a
	// successful BeginSequence and released when the beat ends or is cancelled. Not part of the
	// base snapshot: the owning sequence serializes its own claim and re-stamps this on load, so
	// the two can never disagree.
	FElysiumEntityHandle ScriptOwner;
	bool bScriptOwnerLocked = false;

	// 12.1 — play a clip out of a choreographed scene's own anim set (the whole-cast cinematic
	// model), selecting this actor's skeleton inside it by the scene's `bonerename` source. Kept
	// beside PlayAnimClip for the same no-RTTI reason; base answers false.
	virtual bool PlayCinematicClip(const FString& AnimSetModel, const FString& BoneRoot,
		const FString& ClipName, bool bLoop, float* OutSeconds = nullptr) { return false; }
	virtual bool PreloadCinematicClip(const FString& AnimSetModel, const FString& BoneRoot,
		const FString& ClipName) { return false; }
	virtual bool SeekCinematicClip(float PositionSeconds) { return false; }
	virtual void StopCinematicClip() {}

	// 12.3 — write named flex controllers on this entity's face. Purely additive: a controller the
	// write set does not name keeps whatever it held, so the caller owns clearing what it stopped
	// driving. That is what lets a scene's expression track and a line's lipsync write the same face
	// without a layer stack between them.
	//
	// Returns how many writes landed, or INDEX_NONE when there is no face here at all — a body with
	// no facial sidecar, a sidecar whose rig deforms nothing (`shovelhead`, `female_raver_1`), an
	// entity with no body, or the player, whose every model carries zero flexdescs. All of those are
	// ordinary no-ops, not errors. Names the rig does not carry are appended to OutMissing so a
	// caller can report them once instead of dropping them silently.
	virtual int32 SetFlexControllers(TArrayView<const FElysiumFlexWrite> Writes,
		TArray<FString>* OutMissing = nullptr) { return INDEX_NONE; }

	// The amplitude jaw, the face's one non-controller input: `mstudiomouth_t` names a flexdesc, so
	// this lands below the rule layer the writes above feed. 0 is a closed mouth, 1 the speaking
	// line's own peak. False when there is no face, no body, or no mouth record — all ordinary, and
	// the answer for the whole unrigged half of the cast. Base answers false.
	virtual bool SetMouthOpen(float Open) { return false; }

	// This character's own phoneme filter — the bounds a `.lip` phoneme's span is clamped to for the
	// viseme envelope's blend width. Read once per spoken line, not per frame: it is a property of the
	// model, and the two drivers that own a line hold the answer on the binding. False when there is
	// no face here, and the caller keeps the modal default. Base answers false.
	virtual bool GetPhonemeFilter(float& OutMin, float& OutMax) const { return false; }

	// A disposition write from script — the animation half of `SetDisposition` (2,510 calls, the
	// largest single engine demand in the game). Re-picks the standing stance; the emotional-state
	// and reaction half is 9.9's. Base answers false.
	virtual bool SetDispositionName(const FString& Disposition) { return false; }

	// No-RTTI downcast to the door base (UE builds compile without RTTI, so no dynamic_cast). Base
	// returns null; FElysiumDoorBase overrides to return itself, so a resolved `linked_door` name can
	// be recognised as a door without reflection.
	virtual FElysiumDoorBase* AsDoorBase() { return nullptr; }
	virtual FElysiumLockableEntity* AsLockableEntity() { return nullptr; }
	const FElysiumLockableEntity* AsLockableEntity() const
	{
		return const_cast<FElysiumEntity*>(this)->AsLockableEntity();
	}
	virtual class FElysiumTerminal* AsTerminal() { return nullptr; }
	const class FElysiumTerminal* AsTerminal() const
	{
		return const_cast<FElysiumEntity*>(this)->AsTerminal();
	}

	// No-RTTI downcast to the combat character (11.4), for the callers that need the sheet or the
	// damage receiver off a base pointer — the same reason AsDoorBase exists.
	virtual class FElysiumCombatCharacter* AsCombatCharacter() { return nullptr; }
	const class FElysiumCombatCharacter* AsCombatCharacter() const
	{
		return const_cast<FElysiumEntity*>(this)->AsCombatCharacter();
	}

	// The same, for the item leaf (9.8): an inventory holds handles, and resolving one has to
	// recognise an item without reflection. Base returns null; FElysiumItem overrides.
	virtual class FElysiumItem* AsItem() { return nullptr; }
	const class FElysiumItem* AsItem() const
	{
		return const_cast<FElysiumEntity*>(this)->AsItem();
	}

	// The same, for the loot-container leaf (9.8). Containers sit on the combat-character chain,
	// but callers resolving an arbitrary entity still need to distinguish the leaf without RTTI.
	virtual class FElysiumItemContainer* AsItemContainer() { return nullptr; }
	const class FElysiumItemContainer* AsItemContainer() const
	{
		return const_cast<FElysiumEntity*>(this)->AsItemContainer();
	}

	// The same, for the living-NPC leaf. The AI's own producers hand each other base pointers — an
	// attack notice reaches its victim as an entity handle — and only `FElysiumNpc` carries the
	// senses, memory and cognition those producers write. It is deliberately NOT a classname test:
	// `npc_VPlayerController` shares the `npc_` prefix and is a different leaf entirely.
	virtual class FElysiumNpc* AsNpc() { return nullptr; }
	const class FElysiumNpc* AsNpc() const
	{
		return const_cast<FElysiumEntity*>(this)->AsNpc();
	}

	// --- Open-ended attribute names (11.4) ----------------------------------------------
	// The registry's field table is a static list of names, which is exactly right for a datamap
	// and wrong for the part of the character sheet that is `vdata`-driven (`base_<discipline>`,
	// the attribute/ability ratings). Both script hosts consult these AFTER the class-chain walk
	// and before their Character-method fallback, so a sheet name reads a number instead of
	// binding as a method. Base answers false — no dynamic names. 9.4 shrinks the bag as it turns
	// the names VtMB's own datamap carries into registered fields.
	virtual bool GetDynamicField(FName Name, FElysiumVariant& Out) const { return false; }
	virtual bool SetDynamicField(FName Name, const FElysiumVariant& Value) { return false; }

	// --- Persistence (11.9) -------------------------------------------------------------
	// Everything a class registers as a `Save` field is enumerated by the R2 walk and needs no code
	// here (`docs/architecture/save-architecture.md` §4). This is the hook for the one thing that does not fit it: a
	// leaf's *derived* runtime state — a mover's phase and its move endpoints, a sequence cursor —
	// state that is neither a keyvalue nor a field, and that a rebuild from the def cannot re-derive.
	// Called after the field walk, in both directions (the archive knows which). Bodies are never
	// saved: they are disposable presentation (R1) and rebuild from the def plus the restored state.
	virtual void Serialize(FElysiumSaveArchive& Ar) {}

	// True when this entity leaves the map with the player rather than staying behind — an inventory
	// item, once 9.8 makes items owned entities. The freeze records such an entity in the snapshot's
	// `AbsentEntities` set instead of its state, which is what stops walking back into a map from
	// re-materialising everything carried out of it (VtMB's `.HL3`, `docs/architecture/save-architecture.md` §5).
	virtual bool TravelsWithPlayer() const { return false; }

	// ScriptUnhide's stashed think. Exposed because the snapshot carries it: an entity frozen while
	// hidden restores with the think it will resume on, not with "never".
	float GetSavedNextThink() const { return SavedNextThink; }
	void  SetSavedNextThink(float InThink) { SavedNextThink = InThink; }

	// --- Lifecycle --------------------------------------------------------------------
	// Bind identity and copy the base keyfields out of the def's raw keys through the class
	// chain field table (R2), honouring start_hidden. The world's spawn pass (P1.4) calls
	// this, then Spawn(); a leaf class overrides Spawn() for its own wiring.
	void Construct(const FElysiumEntityDef& InDef, FElysiumEntityHandle InHandle, const FElysiumClassDesc& InClass);
	virtual void Spawn() {}

	// Second-phase construction, run after EVERY entity on the map has Spawn()'d. The base resolves
	// parentname and attaches this entity's body while preserving its
	// exported world pose; constraints and other leaves extend this after every body exists.
	virtual void PostSpawn();
	// Re-resolve logical parenting and attach bodies when both are available. The initial PostSpawn
	// pass is quiet because maker-owned parents can appear during Activate; the world calls this
	// again at that lifecycle barrier with diagnostics enabled.
	bool ResolveParentAttachment(bool bWarnIfPending);

	// Source's late Activate pass. The entity world calls this exactly once after the player has been
	// placed and synchronized but before gameplay ingress opens. Runtime spawns in an active world
	// receive it immediately after PostSpawn.
	virtual void Activate() {}
	// Activation normally becomes part of a fresh world's omission baseline. A leaf whose cached
	// activation result depends on transient residency state can keep the pre-activation baseline
	// so that result remains explicit in snapshots and survives a later rebuild.
	virtual bool ActivationStateMustPersist() const { return false; }

	// Map-load residency pass. All map entities and the player have spawned, but the entity world is
	// still dormant: a leaf contributes its authored animation references here without starting any
	// clip, think, output, camera or scene clock.
	virtual void PreloadForActivation() {}

	virtual void Think() {}

	// A playing choreographed scene blocks NPC-maker admission. The world derives the global gate
	// from live entities so overlapping scenes and save restoration need no separate latch.
	virtual bool BlocksNpcMakerSpawns() const { return false; }

	// Owner callbacks remain ordinary substrate calls. Any outputs produced by the owner still join
	// FElysiumEventQueue through FireOutput; this is lifecycle notification, not a second transport.
	virtual void OnOwnedEntityTerminated(FElysiumEntity&, EElysiumOwnedEntityTermination) {}

	// A mover whose endpoints were computed from exported world coordinates can translate them
	// into its new parent-local space here. Point visuals and non-movers need no adjustment.
	virtual void OnParentAttached(const FTransform& ParentWorldTransform) {}

	// Body hook (R6): mirror dormancy onto the attached body's collision. No-op while an entity
	// has no body (all point/logic entities).
	virtual void OnDormancyChanged();
	// Class state such as CBaseTrigger::StartDisabled participates in the same physical gate as
	// hidden/dead without pretending the entity itself is dormant.
	virtual bool IsBrushBodyEnabled() const { return !IsInert(); }
	void RefreshBrushBodyState();

	// `#<idx> <targetname>(<classname>)` — the canonical debug string (R3), used everywhere.
	FString DebugString() const;

protected:
	void NotifyOwnerOfTermination(EElysiumOwnedEntityTermination Reason);
	float SavedNextThink = ELYSIUM_NEVER_THINK;   // restored by ScriptUnhide
};
