#pragma once

#include "CoreMinimal.h"
// The sequence-event record the timeline seam below hands down, by array.
#include "ElysiumAnimEvent.h"
// By value: the activity-resolve seam below takes one request record and answers with one clip
// record, and both are this header's types.
#include "ElysiumAnimationIntent.h"
#include "ElysiumAudioSubsystem.h"   // FElysiumAudioVoiceHandle + FElysiumPlayParams (passed by value)
// By value: the ideal-activity record the melee seam below reports, and the substrate's own
// predicates over it.
#include "ElysiumClipMovement.h"
// The authored combo-chain block the clip seam below hands down, by pointer.
#include "ElysiumComboChain.h"
#include "ElysiumEntity.h"           // FElysiumFlexWrite (passed by view)
#include "ElysiumEntityHandle.h"
#include "ElysiumCharacterModelAdmission.h"
#include "ElysiumInteraction.h"
#include "ElysiumLocomotionSample.h" // FElysiumLocomotionSample (returned by value)
// The authored swing-contact record the clip seam below hands down, by array.
#include "ElysiumSwingRecord.h"

class FElysiumDlgConversation;
class IElysiumCameraService;
class USceneComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UPrimitiveComponent;
struct FElysiumCameraShot;
// The pusher's exposure policy for a scripted shot (`ElysiumCameraSolve.h`).
enum class EElysiumShotExposure : uint8;
struct FElysiumEntityDef;
struct FElysiumSignData;
struct FElysiumStanceClips;
struct FElysiumDisposition;
// A2 (footsteps): the surface sound row `ResolveSurfaceSounds` fills, declared in
// `ElysiumSurfaceSounds.h`. By out-reference, so the declaration is enough here.
struct FElysiumSurfaceSounds;

enum class EElysiumPlacedModelPhysics : uint8
{
	None,
	CollisionProxy,
	SimulatedProxy,
};

// Generated availability of an item definition's shared ground model. Geometryless is an
// authored empty body (w_null and definitions with the same shape), not a failed asset load.
enum class EElysiumItemGroundModelState : uint8
{
	Geometry,
	Geometryless,
	Unavailable,
};

// One engine-neutral request for a non-character MDL embodiment. The returned body separates the
// posed visual from the component gameplay attaches/collides against; those are the same only for
// a collision-free placement.
struct FElysiumPlacedModelRequest
{
	FString ModelPath;
	FString StaticStem;
	FVector Location = FVector::ZeroVector;
	FQuat Rotation = FQuat::Identity;
	float UniformScale = 1.0f;
	int32 PlacementToken = 0;
	int32 Skin = 0;
	EElysiumPlacedModelPhysics Physics = EElysiumPlacedModelPhysics::None;
};

struct FElysiumPlacedModelBody
{
	UPrimitiveComponent* Visual = nullptr;
	USkeletalMeshComponent* SkeletalVisual = nullptr;
	UStaticMeshComponent* StaticVisual = nullptr;
	UPrimitiveComponent* Attach = nullptr;
	UStaticMeshComponent* PhysicsProxy = nullptr;
	FString Stem;

	bool IsValid() const { return Visual != nullptr && Attach != nullptr; }
};

// Every authored runtime placement is expressed in Source feet space. The save/stage payload stores
// Unreal's capsule centre instead. Carry the space with the value and convert exactly once, when
// the real body's half-height is known.
enum class EElysiumPlayerPlacementSpace : uint8
{
	Feet,
	CapsuleCenter,
};

namespace ElysiumPlayerPlacement
{
	inline FVector ToCapsuleCenter(const FVector& Placement, EElysiumPlayerPlacementSpace Space,
		float BodyHalfHeight)
	{
		return Space == EElysiumPlayerPlacementSpace::Feet
			? Placement + FVector(0.f, 0.f, BodyHalfHeight)
			: Placement;
	}
}

// Engine-neutral view of a native Unreal NPC movement body. The implementation is an ACharacter
// possessed by an AI controller; the substrate only owns the request/state contract, so no Unreal
// AI or navigation type crosses this seam.
enum class EElysiumNpcMoveStatus : uint8
{
	Unavailable,
	Idle,
	Moving,
	Reached,
	Failed,
};

// Which of the body's own authored locomotion fans a travel request rides. Retail NPC travel speed
// is the selected cycle's own authored movement rather than a scalar, so a caller names the gait and
// the body answers with the number (`docs/architecture/movement-architecture.md` → "The speed
// authority").
enum class EElysiumNpcGaitKind : uint8
{
	Walk,
	Run,
	Sneak,
};

// What a launched body is doing right now, as the motor can observe it.
//
// **Geometry and engine state only — no verdict.** Whether a flight has ENDED, and whether a
// contact counts as the wall that diverts a chain into its `..._WALL_FALL` sub-chain, are the
// substrate's rules and are decided from these facts rather than reported by them (S11).
struct FElysiumBallisticSample
{
	// The two the terminator reads. Retail ends a flight when the body is grounded, OR is falling
	// and a ground probe succeeds.
	bool bGrounded = false;
	bool bFalling = false;
	// The body's current velocity, world centimetres/second. The chain reads it to build the wall
	// rebound, which is the wall vector times a recovered scalar.
	FVector VelocityCmPerSecond = FVector::ZeroVector;
	// The most recent blocking contact since the last sample, and its surface normal. The NORMAL,
	// not a verdict about walls: a floor is a blocking contact too, and which of them is a wall is
	// the substrate's call.
	bool bContacted = false;
	FVector ContactNormal = FVector::ZeroVector;
};

class IElysiumNpcMotor
{
public:
	virtual ~IElysiumNpcMotor() = default;

	// --- The ballistic pair ---
	//
	// EXECUTION. Retail's `SetAbsVelocity(m_KnockbackVelocity)` — a straight ASSIGNMENT, not an
	// impulse and not a physics solve (`docs/vtmb/combat-and-damage.md` -> "Launch is a velocity
	// assignment, in two stages"). Chaos never sees it: the body stays an animated character
	// playing an authored cell, which is why this is not the physics architecture's own
	// `AddBodyImpulse` — that one is for ragdolls and props, and a live launched body is neither.
	//
	// The magnitude, the direction and the think it happens on are all decided before this call.
	//
	// **False is the default and it is part of the contract**: this motor cannot carry a ballistic
	// body at all — a headless world, or a recording double that models no flight. A caller that
	// gets false must END its chain rather than wait for a landing that will never be reported,
	// because the sample below will answer "neither grounded nor falling" forever.
	virtual bool Launch(const FVector& VelocityCmPerSecond) { return false; }

	// QUERY, and **polled rather than pushed** — the same direction `IElysiumEmbodiment::
	// QueryNpcReactionHold` runs in, and for the same reason: a callback from the body into the
	// substrate is a presentation backchannel, which the gameplay contract forbids outright. A
	// producer already has a think, and asks on that.
	//
	// False means this motor has nothing to report, which is also the default. `Out` is untouched.
	//
	// **The divergence, named here.** Retail's flight is integrated by its own `CGameMovement` over
	// its own traces; ours is integrated by `UCharacterMovementComponent` over Unreal's. The rules
	// that shape the arc — the launch magnitude, the direction, the one-think delay, the rebound
	// scalar — stay in the substrate and are reproduced exactly; the solve between two samples is
	// the engine's and will not agree frame-for-frame with retail's. What the player observes is
	// the cell that plays and where the body ends up, and both are decided by the substrate.
	virtual bool SampleBallistic(FElysiumBallisticSample& Out) const { return false; }
	// `bAllowPartialPath` takes the best path the graph can offer instead of refusing the request.
	// A route point wants the refusal — it must never silently skip authored route data. A
	// scripted_sequence mark wants the partial walk: the transit is the point of the beat, and its
	// caller places the NPC on the mark when the walk ends short.
	//
	// `GaitKind` names which of this body's own authored fans `SpeedCmPerSecond` came from. Naming
	// one hands the leg to the speed authority: the body's own animation pass re-commands its mover
	// every frame with the cell the selection record published that frame, so a leg that turns, or
	// whose tables move under it on an equip, travels at the cycle it is playing rather than at the
	// number this call happened to resolve. `SpeedCmPerSecond` is then only the opening command,
	// before the first classification.
	//
	// **Unset is the default and means "the caller authored this exact number; never re-derive
	// it."** The scripted Walk/Custom gaits hand this a resolved clip's own authored ground speed —
	// not the gait fan's forward cell — and re-deriving it would silently overwrite a number the
	// beat asked for by name.
	virtual bool MoveTo(const FVector& FeetDestination, float AcceptanceRadiusCm,
		float SpeedCmPerSecond, bool bAllowPartialPath = false,
		TOptional<EElysiumNpcGaitKind> GaitKind = TOptional<EElysiumNpcGaitKind>()) = 0;
	// Turn in place toward a yaw without travelling — HL1 CCineMonster's TASK_FACE_SCRIPT, which a
	// beat runs after reaching its mark and which `m_fMoveTo 5` runs on its own. Cancelled by
	// Stop/Teleport/MoveTo like any other request.
	virtual void Face(float YawDegrees) = 0;
	virtual void Stop() = 0;
	virtual void Teleport(const FVector& FeetOrigin, float YawDegrees) = 0;
	virtual void SetEnabled(bool bEnabled) = 0;
	// Immobilise the body without hiding it — a choreographed scene's `position_start` placement
	// (VtMB's MOVETYPE_NONE + SOLID_NONE + FSOLID_NOT_SOLID). Distinct from SetEnabled, which also
	// takes the body off screen and so cannot hold a cast that has to stay on camera.
	virtual void SetFrozen(bool bFrozen) = 0;
	// Pass through other characters and the player for a scripted beat's duration
	// (`scripted_sequence` spawnflag 4096). World collision is retained.
	virtual void SetIgnoreCharacterCollision(bool bIgnore) = 0;
	// The body's live feet/yaw plus what its outstanding request is doing. A turn-in-place reports
	// Moving until it is aligned, then falls back to Idle — there is only one request at a time.
	virtual EElysiumNpcMoveStatus Sample(FVector& OutFeetOrigin, float& OutYawDegrees) = 0;

	// This body's own authored travel speed for one gait at one facing-relative direction, cm/s —
	// the cell of that gait's resolved fan the body is about to play. It is the number a travel
	// request must command with, or the body slides through a cycle authored for a different speed.
	//
	// `MoveYawDegrees` defaults to zero, the forward cell, which is where a settled path-following
	// body sits. It is not where a turning one sits: a patrol turnaround plays several strafe cells
	// on its way round, and those are different authored numbers.
	//
	// **Zero means this body resolves no fan for that gait**, and it is also the default: a motor
	// with no animation behind it (a headless world, a recording double) genuinely has no authored
	// number to give. The caller supplies the fallback — `ElysiumNpcGait::TravelSpeed` is the one
	// place that states it — rather than the motor inventing a constant of its own.
	virtual float GaitSpeed(EElysiumNpcGaitKind Gait, float MoveYawDegrees = 0.0f) const { return 0.f; }

	// Where on the navigable surface this arbitrary point lands. Geometry only. The caller keeps
	// the decision — whether the projected point is still the point it wanted — which is what stops
	// this from becoming "give me somewhere good to stand" (S11/K13).
	//
	// **False means unprojectable**, and that is also the default: a motor implementation with no
	// navigation behind it (a headless world, a recording double that has not opted in) genuinely
	// cannot answer, and a caller must fail rather than walk to a guess. `OutProjectedCm` is
	// untouched on false.
	virtual bool ProjectToNavigable(const FVector& PointCm, FVector& OutProjectedCm) const
	{
		return false;
	}

	// The body's realized locomotion — **the same record the player's mover publishes**, so
	// the cast's locomotion and the player's cannot become two systems that happen to play the same
	// files (`docs/architecture/animation-architecture.md` §3.2). Distinct from `Sample` above, which
	// answers where the body is and whether its request is done; this answers how it is moving.
	//
	// Computed on demand rather than cached, and that is not an inconsistency with the player's
	// stored sample: the mover's carries the wish direction of the command it *integrated*, which a
	// pull would desynchronise the moment the next command lands. An NPC has no user command, so
	// there is nothing to desynchronise against.
	virtual FElysiumLocomotionSample SampleLocomotion() const = 0;
};

// The substrate's outbound seam (`docs/architecture/runtime-architecture.md` §7).
//
// FElysiumEntityWorld and every entity class under it are plain C++. What they need from the
// engine — a body to stand, a voice to play, a map to travel to, a panel to put on screen — comes
// through these four interfaces and nothing else: no Cast<AElysiumMapActor>, no
// GetWorld()->GetFirstPlayerController(), no GetSubsystem<> walk off the owning actor. The
// direction of dependency is one-way (§2): the substrate knows IElysiumWorldServices, actors know
// the substrate.
//
// **Any member of the bundle may be null**, and every call site must handle it. That is not a
// defensive habit — it is the existing A/B path formalised: `elysium.NpcBodies 0` and
// `elysium.BrushBodies 0` already run the whole logic layer with no embodiment, and a Substrate-
// tier test runs it with no engine at all. A null service means "this capability is absent", which
// is a state the game already ships.
//
// What it buys: a Substrate-tier test can drive a whole map's logic headlessly against a recording
// stub — with no RHI, no actors and no `$ELYSIUM_EXPORT_ROOT` — which is the missing middle tier between
// variant arithmetic and launching the game (Elysium.Substrate.WorldServices).

// One sub-step of a melee swing's swept contact, as `QuerySwingContacts` takes it.
//
// The segment is the authored bone-local contact record placed in the world twice: where it was at
// the start of the sub-step and where it is at its end. Both placements are the substrate's, built
// from the bone's transform and the attacker's interpolated origin/facing, because the sub-step
// rate is a game rule and the interpolation between two of them is arithmetic rather than a query.
struct FElysiumSwingSweep
{
	// The swinging character, excluded from its own contact.
	FElysiumEntityHandle Attacker;
	// The segment's two endpoints at the sub-step's start, world centimetres.
	FVector PrevA = FVector::ZeroVector;
	FVector PrevB = FVector::ZeroVector;
	// The same two endpoints at its end.
	FVector CurA = FVector::ZeroVector;
	FVector CurB = FVector::ZeroVector;
};

// --------------------------------------------------------------------------------------------
// Embodiment — bodies, meshes, clips, skins, and the player's own body.
//
// Implemented by AElysiumMapActor: every component it builds belongs to it and dies with it, so
// "the world logically owns the embodiments, the actor physically owns them" stays true (R1).
// The player half is here because the pawn IS the player's body (S3); player state lives on the
// entity, and these calls are ordinary entity operations.
// --------------------------------------------------------------------------------------------
// R6.2: what a `light_dynamic` publishes to stand its light -- the same raw terms a `.lights` row
// carries, so the rig derives it through `ApplyToSource` like any legacy source.
struct FElysiumDynamicLightSpec
{
	FVector LocationCm = FVector::ZeroVector;
	FVector Forward = FVector(1.f, 0.f, 0.f);   // Unreal frame; a spot points along it
	FLinearColor Color = FLinearColor::White;   // rgb / max(rgb)
	float Mag = 0.f;                            // the VRAD-scale magnitude (max of rgb)
	float RadiusCm = 0.f;                       // `distance` x 2.54; 0 = the rig's fallback reach
	float StopDot = 0.f;                        // cos(_inner_cone); spot only
	float StopDot2 = 0.f;                       // cos(_cone); spot only
	int32 Style = 0;
	bool bSpot = false;                         // `_cone` > 0
};

// R7.3 (`effects-architecture.md` §5.9): one transient particle root a producer started through
// `SpawnParticleRoot`. Invalid when the root's tree is unknown or there is no world to stand it in.
struct FElysiumEffectHandle
{
	int32 Id = INDEX_NONE;
	bool IsValid() const { return Id != INDEX_NONE; }
};

class IElysiumEmbodiment
{
public:
	virtual ~IElysiumEmbodiment() = default;
	// True only when v7 owns complete `.ents` + GAME_LUMP model coverage. Callers use this to
	// distinguish a fail-closed catalogue miss from the supported stale-index developer fallback.
	virtual bool HasPlacedModelCatalogue() const { return false; }
	virtual FElysiumPlacedModelBody BuildPlacedModelBody(
		const FElysiumPlacedModelRequest& Request) { return {}; }

	// The uniform scale a body built for this def takes (the 3D-skybox miniature's scale for a
	// sky-scope entity, 1 for everything else).
	virtual float BodyScaleFor(const FElysiumEntityDef& Def) const = 0;

	// Stand one NPC skeletal body, playing the standing idle its disposition selects.
	// Null on a missing/failed glb or an empty stem.
	virtual USkeletalMeshComponent* BuildNpcVisual(const FString& Stem, const FVector& Location,
		const FRotator& Rotation, float UniformScale, const FString& Disposition, int32 IdleVariant) = 0;
	// Headless implementations need no asset admission. The real map returns Pending until
	// native metadata, mesh and clips are resident; it never blocks this gameplay call.
	virtual EElysiumCharacterModelAdmission RequestCharacterModel(const FElysiumEntityHandle& Entity,
		const FString& ModelId, uint64 Generation, FString& OutError)
	{ OutError.Reset(); return EElysiumCharacterModelAdmission::Ready; }
	virtual void CancelCharacterModel(const FElysiumEntityHandle& Entity) {}
	// Promote an ordinary NPC's visual to a native movement body. Owner preserves the logical entity
	// identity through collision ingress. Null is the supported headless, backdrop,
	// disabled-navigation, or failed-spawn path; the NPC remains a standing entity.
	//
	// `Stem` and `Variant` are what the body's own animation selection is keyed on: the model
	// names its clip vocabulary, and the variant is the repeatable token weighted choice rides on. The
	// caller is the one place that knows both, so they travel with the body rather than being looked
	// back up from it.
	virtual IElysiumNpcMotor* BuildNpcMotor(USkeletalMeshComponent* Body,
		const FElysiumEntityHandle& EntityOwner, const FVector& FeetOrigin, float YawDegrees,
		const FString& Stem, int32 Variant)
	{
		return nullptr;
	}
	// The motor is engine-owned but logically belongs to the entity. Called on a model swap and
	// entity-world teardown so a reload on a surviving map actor cannot leak collision capsules.
	virtual void DestroyNpcMotor(IElysiumNpcMotor* Motor) {}
	// Re-run the default-idle policy on a live body and crossfade to the result (a disposition change).
	virtual bool RefreshNpcIdle(USkeletalMeshComponent* Body, const FString& Stem,
		const FString& Disposition, int32 DispositionLevel, int32 IdleVariant) = 0;
	// Update presentation state retained beside the body (currently the blink cadence) without
	// rebuilding it. The logical disposition remains on the plain-C++ character.
	virtual void UpdateNpcDisposition(USkeletalMeshComponent* Body, const FString& Disposition,
		int32 DispositionLevel) {}
	// Crossfade a live body to a named clip; OutSeconds (optional) receives its authored length,
	// which is what a scripted_sequence schedules its OnEndSequence off.
	//
	// **The one montage-slot mechanism**: every named clip a producer puts on a body arrives
	// here, takes its base-channel claim at the band the segment states, and plays into the body's
	// `DefaultSlot`. A `scripted_sequence`'s idle/play/post-idle and an interesting place's
	// enter/hold/leave are the same run through it, differing only in band and in where the run's
	// claim is given back.
	//
	// The claim is taken FIRST and decides whether the clip plays at all: playing before asking would
	// leave a refused producer's pose standing on a channel it was refused the right to replace.
	virtual bool PlayNpcClip(USkeletalMeshComponent* Body, const FString& Stem,
		const FElysiumClipSegment& Segment, float* OutSeconds) = 0;

	// Give back the HELD segment claim standing on this body.
	//
	// The release half of a `FElysiumClipSegment::bHoldUntilReleased` run. That claim has no duration
	// — it spans every segment of the run — so this call is the only thing that ends one, and every
	// path that ends a run goes through it: a beat completing, a beat cancelled, an interesting place
	// released, a body torn down. **It is also what a run must call before handing the body back to
	// its own idle**, because the resting pose comes in at `Ambient` and a standing `Scripted` claim
	// would refuse it. A body holding no such claim releases nothing, which is an ordinary absence.
	virtual void ReleaseNpcSegment(USkeletalMeshComponent* Body) {}
	// Resolve and retain a clip without changing the body's current animation. The map-load walker
	// uses this before activation so runtime-created UAnimSequences and their compression work belong
	// to the loading barrier, not to a choreographed scene's clock.
	virtual bool PreloadNpcClip(USkeletalMeshComponent* Body, const FString& Stem,
		const FString& ClipName) { return false; }
	// The same operation when the future body does not exist yet. `!playercontroller` is created by
	// map logic immediately before a scene starts; its ordinary NPC material permutation therefore
	// has to be made resident from the player's model stem without spawning the stand-in early.
	virtual bool PreloadNpcClipForModel(const FString& Stem, bool bPlayerMaterial,
		const FString& ClipName) { return false; }
	// **The one activity seam.** Resolve a VtMB `ACT_*` request through the body's whole translation
	// chain and answer with the clip it selected — the vocabulary key that has to go back through
	// PlayNpcClip so the shared-bank owner is preserved, the concrete neutral-pose cell whose
	// optional authored speed configures a motor, and the row's own loop bit. Ambient
	// interesting-place data, the schedule's activities and patrol travel are all authored in
	// activities rather than clip labels; `Request.Variant` makes the weighted pick repeatable.
	//
	// The request carries the whole translation context because none of it is derivable here: a
	// player weapon and an NPC's are the same `FElysiumWeapon` entity, and the stem names a model
	// rather than the character standing in it, so a seam that filled any of it itself would resolve
	// for a different body than the one being posed.
	//
	// A negative answer is a named miss on the resolver's own selection record, not a failure the
	// caller has to restate: each caller keeps its own stated fallback.
	virtual bool ResolveNpcActivityClip(const FElysiumActivityClipRequest& Request,
		FElysiumActivityClip& Out)
	{
		Out = FElysiumActivityClip();
		return false;
	}
	// The label-route sibling of ResolveNpcActivityClip, for a caller that already names an exact
	// clip -- `m_iszCustomMove` and the like -- rather than an ACT_* to weigh-pick. OutAnimName is
	// the concrete cell ClipName resolves to (itself, unless ClipName names a blend grid); the speed
	// is zero when that cell carries no authored movement metadata, which is the ordinary case for a
	// single-cell clip today.
	virtual bool ResolveNpcSequenceClip(const FString& Stem, const FString& ClipName,
		EElysiumAnimBodyKind BodyKind, FString& OutAnimName, float& OutGroundSpeedCmPerSecond)
	{
		OutAnimName.Reset();
		OutGroundSpeedCmPerSecond = 0.f;
		return false;
	}
	// Play one already-resolved cell over whatever owns the base pose. The (owner, animation
	// name) pair addresses the baked clip directly, so nothing here consults the clip vocabulary — a
	// vocabulary lookup would re-resolve the label at neutral pose parameters and collapse a
	// directional fan onto its forward cell.
	//
	// The claim is taken first and decides whether the clip plays at all: a body a choreographed
	// scene owns refuses a Reaction claim, and a reaction must not ride over a scene. `OutSeconds`
	// (optional) receives the clip's authored length.
	virtual bool PlayNpcOneShot(USkeletalMeshComponent* Body,
		const FElysiumOneShotClipRequest& Request, float* OutSeconds)
	{
		return false;
	}

	// Give back the HELD reaction claim standing on this body and take its pose down.
	//
	// The release half of a `EElysiumReactionRelease::Predicate` play. That claim carries no duration
	// at all — the pose stands for exactly as long as the predicate that asked for it — so this call
	// is the only thing that ends one, and every path that ends the predicate goes through it: the
	// button releasing, the character dying, the body being torn down. A body holding no such claim
	// releases nothing, which is an ordinary absence rather than a failure.
	virtual void ReleaseNpcReaction(USkeletalMeshComponent* Body) {}

	// Whether the HELD reaction claim taken on this body still stands, and if not, whether the base
	// channel is free for one to be re-taken.
	//
	// **A query, not a callback**, which is the direction this seam runs in: the substrate asks the
	// engine and nothing in the engine reaches back into an entity — a presentation backchannel is
	// one of the shapes the gameplay contract forbids outright. A producer holding a
	// predicate-released pose therefore POLLS this on the think it already has, rather than being
	// notified when its claim is displaced.
	//
	// `Free` is the honest default and the honest answer for a body nothing arbitrates: it holds no
	// claim, so nothing can be holding the channel against a resume either.
	virtual EElysiumHeldReactionState QueryNpcReactionHold(USkeletalMeshComponent* Body) const
	{
		return EElysiumHeldReactionState::Free;
	}

	// --- The death handoff ---
	//
	// Three calls, because death is the one transaction that ends every claim a body holds at once
	// and then takes the pose away from animation altogether.

	// Give back EVERY channel claim standing on this body, whichever producer took it. A death is not
	// one producer handing a channel back: the character stops having behaviour, so a scene beat, a
	// reaction and an ambient stance all stop owning it in the same instant. A body no driver
	// arbitrates holds nothing, which is an ordinary absence.
	virtual void ReleaseBodyAnimClaims(USkeletalMeshComponent* Body) {}

	// Hand this body to Unreal's physics, seeded from the pose it is standing in right now — the
	// simulation starts at the current bone transforms, so the death sequence's last frame is the
	// ragdoll's first. **Unreal owns the physics**: nothing about Source's ragdoll solver, its force
	// envelope or its bone mapping is reproduced.
	//
	// False means this body carries no physics asset to simulate, and the caller's stated fallback is
	// `HoldBodyFinalPose`. That is the shipped case today: the character bake writes no physics
	// asset, so the corpse holds its final frame instead of falling.
	virtual bool StartBodyRagdoll(USkeletalMeshComponent* Body) { return false; }

	// Stop evaluating animation and leave the last drawn pose on screen. The body stays visible and
	// keeps its transform; only the pose stops advancing.
	virtual void HoldBodyFinalPose(USkeletalMeshComponent* Body) {}

	// --- The sequence-event seam ---
	//
	// Where one channel of a body is standing on its clip THIS frame. The pose layer is the only
	// thing that knows: a montage, a graph state and a blended fan all carry their own position, and
	// a substrate reader that timed a clip off its own clock would drift from the frame the body is
	// actually drawing.
	//
	// **A phase, never a time** — the recovered dispatcher compares normalized cycles, and a fan has
	// no single length to divide by (`docs/vtmb/animation_and_movers.md` → "Sequence events and
	// native dispatch").
	//
	// False is the ordinary answer for a body whose channel is playing nothing, and the answer every
	// implementation that has no phase clock gives. `Out` is cleared either way, so a caller cannot
	// read a stale record off a negative answer.
	virtual bool GetBodyClipPhase(USkeletalMeshComponent* Body, EElysiumAnimChannel Channel,
		FElysiumClipPhase& Out)
	{
		Out = FElysiumClipPhase();
		return false;
	}
	// The timeline one sequence declares, off the owning model's own blend sidecar — the array
	// `FElysiumBlendTable::FindEvents` answers, in the file's own order. Null when that sequence
	// declares none, which is most of them and an absence rather than a fault.
	//
	// A pointer rather than a copy: the table is cached whole and immutable by the animation
	// subsystem, so this aliases storage that outlives the frame it was asked in. Same frame rule as
	// the other cached-sidecar getters — a caller does not retain it across a map epoch.
	virtual const TArray<FElysiumAnimEvent>* GetNpcEventTimeline(const FString& OwnerStem,
		const FString& Label, const FString& OwnerRoot = FString())
	{
		return nullptr;
	}

	// The ORNAMENT a character's own clip hung on it — retail's `m_hAnimFollowModel` (`+0x5a8`),
	// created by `CBaseCombatCharacter::HandleAnimEvent` (`0x1032e330`) events 4100 and 4102 and
	// taken away by 4101 (`docs/vtmb/animation_events.md` -> "Port status — combat character band").
	//
	// `RetailPath` is the model path the handler FORMATTED, not a model id: `"%s.mdl"` for 4100 and
	// `"%s_%s.mdl"` with the gender word for 4102, lowercased. The seam looks it up in
	// `DA_OrnamentModels` under exactly that key, so nothing on either side has to un-format it.
	//
	// Retail spawns a `prop_dynamic_ornament` (`FUN_10190e50`), sets its model, and bone-merges it
	// onto the character (`FUN_10191170`: `SetAimEnt`, `SetParent` attachment 0, `SetOwnerEntity`,
	// movetype none). The shipped rigs are 13-bone `Bip01` merge skeletons with one rest sequence,
	// so the port's expression is a leader-posed skeletal component, as a wield model is.
	//
	// **Attaching REPLACES.** Retail removes the standing follow model before it creates the next
	// one, unconditionally, so a second attach is the whole of what a swap is. False is the ordinary
	// negative — the failure tail retail takes when `GetModelPtr` comes back null, which leaves the
	// slot empty rather than raising.
	virtual bool AttachOrnamentModel(USkeletalMeshComponent* Body, const FString& RetailPath)
	{
		return false;
	}
	// Event 4101, and the same removal the 4100/4102 arms run first. A body wearing nothing is the
	// ordinary case and not a fault.
	virtual void DetachOrnamentModel(USkeletalMeshComponent* Body) {}

	// A quiet vocabulary probe: does Stem's clip vocabulary name ClipName, with no play attempted and
	// no warning logged either way. The one caller today is an unauthored cross-disposition stance
	// transition, which is a normal absence rather than a failure -- so it probes here before ever
	// calling PlayNpcClip, whose miss IS a logged warning for every other caller.
	virtual bool HasNpcClip(const FString& Stem, const FString& ClipName) { return false; }

	// The `ACT_*` the ATTACKER plays when the swing this clip realized is blocked — the sequence
	// descriptor's `+0x2E0` column, read off the same `(stem, label)` key `PlayNpcClip` uses
	// (`docs/vtmb/combat-and-damage.md` § "Block and stagger reactions").
	//
	// A quiet non-resolving query like `HasNpcClip` beside it: EMPTY is the ordinary answer and not
	// a failure. Most sequences name no blocked reaction, an unresolved swing addresses no clip at
	// all, and the caller's own documented fallback is `ACT_BLOCKED_REACTION_RIGHT` — so a miss here
	// is a column that was never authored rather than a lookup that went wrong.
	virtual FString NpcClipBlockedReaction(const FString& Stem, const FString& ClipLabel)
	{
		return FString();
	}

	// The authored swing-contact records of the sequence this clip realizes — the descriptor's
	// `+0x2C4`/`+0x2C8` array, read off the same `(stem, label)` key the blocked reaction above is
	// (`docs/vtmb/combat-and-damage.md`). NULL is the ordinary answer and not a failure: 574 of the
	// install's 14,012 descriptors declare records, so every other clip legitimately has none and a
	// clip with none simply never opens a contact window.
	//
	// A pointer rather than a copy, on `GetNpcEventTimeline`'s contract: the clip vocabulary is
	// cached whole and immutable by the animation subsystem, so this aliases storage that outlives
	// the frame it was asked in, and a caller does not retain it across a map epoch.
	virtual const TArray<FElysiumSwingRecord>* NpcClipSwings(const FString& Stem,
		const FString& ClipLabel)
	{
		return nullptr;
	}
	// The authored combo-chain block of the sequence this clip realizes — the descriptor's
	// `+0x2D4`..`+0x2F8` fields, read off the same `(stem, label)` key the swing records are
	// (`docs/vtmb/combat-and-damage.md`). NULL is the ordinary answer and not a failure: 208 of the
	// install's 14,012 descriptors author one, so every other attack is a terminal one whose press
	// chains nothing.
	//
	// Same aliasing contract as `NpcClipSwings`: the clip vocabulary is cached whole and immutable by
	// the animation subsystem, and a caller does not retain the pointer across a map epoch.
	virtual const FElysiumComboChain* NpcClipCombo(const FString& Stem, const FString& ClipLabel)
	{
		return nullptr;
	}
	// The bank the include DAG named for one label of a body's vocabulary — the OWNER half of the
	// `(owner, label)` key every phase, timeline and grid cell is addressed by.
	//
	// EMPTY means the body's vocabulary does not name the label at all, which is retail's
	// `LookupSequence` returning -1. That is what a combo chain naming a sequence its own bank never
	// defines answers with, and the caller reports it rather than substituting anything.
	virtual FString NpcClipOwner(const FString& Stem, const FString& ClipLabel)
	{
		return FString();
	}
	// One model's disposition stance set: three idles, three fidgets and the 3x3 transition matrix
	// for `AnimName`, with the precache fallbacks already applied. Resolved once per (stem,
	// disposition) and cached by the caller, because that is when retail resolves it — a body that
	// authored no `Fidget_2` gets `fidget[2] == idle[2]` at load and is never asked again.
	//
	// The whole set rather than one label at a time: the selector's fidget-availability test is an
	// equality between two entries, so handing it anything less would make it ask the vocabulary
	// mid-decision and put engine access inside a substrate rule.
	virtual bool ResolveStanceClips(const FString& Stem, const FString& AnimName,
		FElysiumStanceClips& OutClips) { return false; }

	// The disposition row a `default_disposition` name resolves to. It carries both halves the
	// stance machine needs — the `Stance_<AnimName>_*` token that keys the clips, and the
	// fidget/stance-change pacing the selector rolls against — so a caller resolves once rather
	// than asking the table twice for two fields of the same row.
	//
	// Handing the row down by value is what keeps the selector a pure rule: the table is engine-side
	// and its Load() needs the export root, so a substrate test that reached for it could not run
	// content-free. The same shape as the gaze layer's `FElysiumEyeTargetTuning`.
	virtual bool ResolveDisposition(const FString& Disposition, int32 DispositionLevel,
		FElysiumDisposition& OutRow)
	{
		return false;
	}

	// --- Footsteps (A2): the surface sound table -------------------------------------------
	// One surfaceprop's audio row, by the name the locomotion sample publishes under
	// `GroundSurface` (`concrete`, `default`). A QUERY, and the same posture `ResolveDisposition`
	// has: the table is engine-side because it is baked into
	// `/ElysiumBaked/SurfaceProperties/PM_<name>` assets, and the row crosses the seam as DATA so
	// the substrate can pick a step wav with no `UObject` in reach.
	//
	// **Headless answers false.** A recording double with no table set, and any embodiment that
	// carries no baked content, report "no such surface" — which is retail's null `surfacedata_t`
	// at `+0x5b90` and a silent step (`vampire.dll 1026d460`), not an error. `true` with empty
	// pools is a DIFFERENT answer: the surface exists and authors no footsteps.
	virtual bool ResolveSurfaceSounds(FName Surface, FElysiumSurfaceSounds& Out) const
	{
		return false;
	}

	// Whether this body was drawn recently enough to count as visible, standing in for the leaf-set
	// answer Source's PVS gives `TASK_WAIT_PVS`.
	//
	// Divergence, enumerated: Source's PVS is leaf-to-leaf and view-independent, so retail keeps
	// running the idle schedule for an NPC standing behind the player in the same room. A render-time
	// query is frustum-dependent, so that NPC holds its pose and resumes a frame after it comes back
	// into view.
	//
	// The `true` default is load-bearing, not a placeholder: a `-nullrhi` test run and an editor
	// commandlet never render, so an implementation that reported "not visible" there would stall
	// every idle schedule in exactly the headless runs meant to prove it.
	virtual bool IsNpcBodyVisible(USkeletalMeshComponent* Body) { return true; }

	// A choreo scene's whole-cast performance. The clip lives in a cinematic anim set that no NPC's
	// include tree names, so it is addressed by the scene's own anim-set model plus the actor's
	// `bonerename` root rather than through the clip vocabulary.
	virtual bool PlayCinematicClip(USkeletalMeshComponent* Body, const FString& Stem,
		const FString& AnimSetModel, const FString& BoneRoot, const FString& ClipName,
		bool bLoop, float* OutSeconds) = 0;
	virtual bool PreloadCinematicClip(USkeletalMeshComponent* Body, const FString& Stem,
		const FString& AnimSetModel, const FString& BoneRoot, const FString& ClipName) { return false; }
	virtual bool PreloadCinematicClipForModel(const FString& Stem, bool bPlayerMaterial,
		const FString& AnimSetModel, const FString& BoneRoot, const FString& ClipName) { return false; }
	virtual bool SeekCinematicClip(USkeletalMeshComponent* Body, float PositionSeconds) = 0;
	virtual void StopCinematicClip(USkeletalMeshComponent* Body) = 0;
	// Give the scene's base-channel claim back WITHOUT stopping the clip. The stop path that
	// crossfades into an idle never calls StopCinematicClip — stopping the host first snaps the
	// pose (see FElysiumAnimating::StopCinematicClip) — yet the idle it crossfades to submits its
	// own ambient claim, which a scene claim left standing would refuse forever. Every stop path
	// releases through this; StopCinematicClip also releases, so the reference-pose fallback needs
	// no second call. Default no-op: an embodiment that arbitrates no channels holds no claim.
	virtual void ReleaseCinematicClaim(USkeletalMeshComponent* Body) {}

	// The free-run counterpart of SeekCinematicClip, for a caller that phases a clip against the
	// substrate clock rather than driving it. Seek pins the body at play rate 0 and collapses any
	// crossfade, which is right for a scene that re-seeks every frame and wrong for a 10 Hz think
	// that wants the clip to keep running smoothly between corrections.
	//
	// This is how retail's `StudioFrameAdvance` behaves without meaning to: it recomputes the cycle
	// from `curtime - m_flAnimTime` every call, so a 10 Hz prop think and a per-frame scene actor
	// stay in lockstep. Reading the position back is what lets a caller tell drift from agreement.
	// False means the body cannot answer — no anim host, or nothing playing — which is an ordinary
	// answer, not an error.
	virtual bool GetCinematicClipPosition(USkeletalMeshComponent* Body, float& OutSeconds) const { return false; }
	virtual bool ResyncCinematicClip(USkeletalMeshComponent* Body, float PositionSeconds) { return false; }

	// 12.3 — write named flex controllers on a body's facial rig, evaluating the rule/ramp chain once
	// for the whole set. INDEX_NONE when the body carries no rig, which is the majority of the cast.
	virtual int32 SetFlexControllers(USkeletalMeshComponent* Body,
		TArrayView<const FElysiumFlexWrite> Writes, TArray<FString>* OutMissing) { return INDEX_NONE; }

	// 12.5 — the amplitude jaw, 0 closed to 1 at the line's own peak. Not a controller write: it
	// targets the flexdesc `mstudiomouth_t` names and so lands below the rule layer. False when the
	// body carries no rig or no mouth record.
	virtual bool SetMouthOpen(USkeletalMeshComponent* Body, float Open) { return false; }

	// Start one non-looping generated effect on a named mesh attachment. The caller names VtMB's
	// semantic attachment (`mouth`); the baked mesh owns the attachment transform and Niagara owns
	// the particle simulation. False means the body, socket, asset or spawn could not be resolved.
	virtual bool PlayAttachedEffect(USkeletalMeshComponent* Body, const FString& Definition,
		FName Attachment) { return false; }

	// R7.3 (`effects-architecture.md` §5.9): "spawn root X at attachment Y with mode Z" -- one
	// transient effect actor on the slotted floor (or its family override), the tree from the shared
	// `DA_ElysiumParticleTrees`. `Root` is the folded `vtmb:particle` key or the bare root name;
	// `Parent` may be null for a world spawn; `AttachMode` the 19-value enum; `AttachName` the bone /
	// attachment string; `AttachPoint` the numbered attachment; `OriginCm` / `Angles` the Unreal
	// placement for a world spawn. Supersedes PlayAttachedEffect for every deferred producer (impact
	// table, muzzle pair, the 511x animation events, the discipline auras). No producer is wired at
	// R7.3; the entry point stands. Invalid handle: unknown root, or headless.
	virtual FElysiumEffectHandle SpawnParticleRoot(const FString& Root,
		const FElysiumEntityHandle* Parent, int32 AttachMode, FName AttachName, int32 AttachPoint,
		const FVector& OriginCm, const FRotator& Angles) { return FElysiumEffectHandle(); }
	virtual void StopParticleRoot(const FElysiumEffectHandle& Handle) {}   // TurnOff: let finish
	virtual void KillParticleRoot(const FElysiumEffectHandle& Handle) {}   // remove now

	// 12.5 — this body's own phoneme filter (`studiohdr` +232/+236), the bounds a `.lip` phoneme's
	// span is clamped to for the viseme envelope's blend width. A read rather than a write, and the
	// only one on this interface: the pair is a property of the model, so the substrate's lipsync
	// binding asks for it once per line instead of carrying a copy of the cast's rig data. False when
	// the body carries no rig, and the caller then keeps the modal default — which is right for
	// sp_theatre's three speakers and for the majority of the rigged cast.
	virtual bool GetPhonemeFilter(USkeletalMeshComponent* Body, float& OutMin, float& OutMax) const
	{
		return false;
	}

	// 12.4 — where this body's eyes are looking, in world space. One vector per character per frame
	// is the whole seam between the half that decides where to look and the half that draws it,
	// which is the same hop retail networks as `m_viewtarget`. False when the body carries no eye
	// record, which is most of the cast.
	virtual bool SetViewTarget(USkeletalMeshComponent* Body, const FVector& WorldTarget) { return false; }

	// The head frame the gaze cascade measures its ±30° cone and its fidget grid in. Retail reads
	// the live animated head bone and falls back to EyePosition()/EyeAngles() on a model that has
	// none, so the caller needs to know which it got. False when there is no head bone.
	virtual bool GetHeadFrame(USkeletalMeshComponent* Body, FVector& OutPosition,
		FVector& OutForward) const { return false; }

	// One bone's CURRENT world transform on a body, by name.
	//
	// The swing's contact segment is stated bone-local, so the frame it is stated in has to be
	// asked for rather than derived: where a limb IS this frame is the pose layer's answer and the
	// skeleton's, not the substrate's (K13). The name is the durable key for the same reason it is
	// on every other bone-addressed record — an NPC and the bank it fights from are separate images
	// with separate bone tables.
	//
	// False means the body carries no bone by that name, which the caller reports: a swing record
	// naming a bone its own model does not have is an authored defect, not an absence.
	virtual bool GetBodyBoneTransform(USkeletalMeshComponent* Body, const FString& BoneName,
		FTransform& OutWorld) const { return false; }

	// A model-authored `$attachment` on a placed prop body, by the owner it was registered under,
	// world cm. Retail reads `screen` / `screen_axis` through `CBaseAnimating::GetAttachment01`
	// (`docs/vtmb/computer-terminals.md` §7.2, `FUN_10218710`).
	//
	// It is keyed by owner rather than by component because the body a `prop_hacking` stands is
	// normally the model's **static** reduction, which carries no socket table; the two transforms
	// still live on the same model's baked skeletal asset, and the map actor composes that asset's
	// ref-pose socket with the standing body's transform (`Visual/ElysiumPlacedAttachments.h`).
	//
	// False = this embodiment carries no attachment data for that owner, which is the headless
	// answer and the answer for a model that does not author the attachment.
	virtual bool GetBodyAttachment(const FElysiumEntityHandle& Owner, FName Attachment,
		FTransform& OutWorld) const { return false; }

	// The world-space bounds of the body registered as `Owner`'s use anchor. Retail's held-use
	// maintenance (`FUN_10167e00` 10167e41) transforms the held entity's collision mins/maxs into
	// world space and measures the player's reach against the closest point on that box; this is
	// the same box. False = no body, which every reader treats as "no reach test".
	virtual bool GetUseBodyWorldBounds(const FElysiumEntityHandle& Owner, FBox& OutWorld) const
	{
		return false;
	}

	// v4 animated props. Model selection is explicit and manifest-backed; ordinary props stay on
	// the existing static representation. The skeletal surface remains non-solid.
	virtual FString AnimatedPropStemForModel(const FString& ModelPath) const = 0;
	virtual USkeletalMeshComponent* BuildAnimatedPropVisual(const FString& Stem,
		const FVector& Location, const FQuat& Rotation, float UniformScale,
		int32 PlacementToken = 0) = 0;
	virtual bool PlayAnimatedPropClip(USkeletalMeshComponent* Body, const FString& Stem,
		const FString& ClipName, bool bLoop, float* OutSeconds) = 0;
	// Animated props are a small, map-scoped catalog. Warm every clip on each model represented in
	// the map so SetAnimation calls originating in Python as well as entity I/O cannot hitch a shot.
	virtual int32 PreloadAnimatedPropClips(USkeletalMeshComponent* Body, const FString& Stem)
	{
		return 0;
	}
	// Complete the exact UAnimSequences retained by this map's body cache. Editor builds may have
	// queued asynchronous compression; packaged builds have no compiler work and simply return.
	virtual int32 FinishAnimationPreload() { return 0; }
	virtual void ApplyAnimatedPropSkin(USkeletalMeshComponent* Comp,
		const FString& StaticStem, int32 Family) = 0;

	// The clip this model rests on — `SelectWeightedSequence(ACT_IDLE)` with retail's sequence-0
	// fallback. **Empty exactly when the model bakes no clip**, which is also the test a prop uses
	// to keep its baked static mesh instead of standing a bind-pose skeleton.
	virtual FString AnimatedPropRestClip(const FString& Stem,
		int32 PlacementToken = 0) const { return FString(); }
	// Does this model bake a clip by this name, and does that clip's own STUDIO_LOOPING bit ask for
	// looping playback? One lookup answering both, because the two callers need different halves:
	// `SetAnimation` plays on the loop bit instead of forcing one shot, and `Activate` only needs
	// to know whether `LoopSequence` resolves at all.
	virtual bool FindAnimatedPropClip(const FString& Stem, const FString& ClipName,
		bool& bOutLoops) const { bOutLoops = false; return false; }

	// A BSP brush entity's baked, local-pivot render surface. ParentBody owns its transform and
	// collision; the returned mesh is visual-only and attached at identity.
	virtual UStaticMeshComponent* BuildBrushVisual(const FString& Stem,
		USceneComponent* ParentBody, float UniformScale, bool bSky) = 0;

	// Stand a non-solid dynamic-prop body, or the same mesh with its `.phy` collision and authored
	// mass, ready for the leaf to drive SetSimulatePhysics. Null on an unbaked model.
	virtual UStaticMeshComponent* BuildPropVisual(const FString& Stem, const FVector& Location,
		const FQuat& Rotation, float UniformScale) = 0;
	virtual EElysiumItemGroundModelState ItemGroundModelState(const FString& ModelPath)
	{
		return EElysiumItemGroundModelState::Geometry;
	}
	virtual UStaticMeshComponent* BuildPhysPropVisual(const FString& Stem, const FVector& Location,
		const FQuat& Rotation, float UniformScale) = 0;
	// Repaint a prop body to one of its model's alternate skin families (VtMB's `skin`).
	virtual void ApplyPropSkin(UStaticMeshComponent* Comp, const FString& Stem, int32 Family) = 0;

	// Stand/clear the player surface through the same glTF skeletal path as NPCs, then seat it on
	// whichever movement pawn is active. The returned component is also the FElysiumPlayer visual,
	// so scene clips and understudy model-copying use the ordinary animating contract.
	virtual USkeletalMeshComponent* BuildPlayerVisual(const FString& Stem,
		const FString& Disposition, int32 IdleVariant) = 0;
	virtual void ClearPlayerVisual() = 0;
	// The player entity's **own** draw gate — `ScriptHide`, dormancy, a `Spawn()`-time `Kill` — pushed
	// at the pawn, which ANDs it with the camera's eligibility. It is not a cutscene gate: creating an
	// `npc_VPlayerController` double does not hide the real body, because retail does not hide it
	// either (`docs/vtmb/camera-view-modes.md` §6).
	virtual void SetPlayerBodyEntityHidden(bool bHidden) {}

	// --- The player's body ------------------------------------------------------------------
	// The eye: where the player is looking from and along. False when there is no player (the menu
	// backdrop seats no pawn), which every caller treats as "the player cannot see it".
	virtual bool GetPlayerViewPoint(FVector& OutLocation, FRotator& OutRotation) const = 0;
	// The body-relative eye/pivot used for authoritative reach. It remains on the pawn when a
	// third-person camera moves behind it.
	virtual bool GetPlayerUseOrigin(FVector& OutLocation) const = 0;
	// The entity transform: Source absorigin at the feet plus the complete Unreal view rotation.
	virtual bool GetPlayerFeetTransform(FVector& OutFeetOrigin, FRotator& OutViewRotation) const = 0;
	// The presentation/save placement: Unreal's centred body plus view. Kept separate so legacy save
	// payloads retain their capsule-centre contract while entity logic never sees it as absorigin.
	virtual bool GetPlayerCapsuleTransform(FVector& OutCapsuleCenter, FRotator& OutViewRotation) const = 0;
	// Place the player at a Source absorigin (feet). The body owns capsule compensation and applies
	// body yaw plus the complete controller view rotation.
	virtual void TeleportPlayer(const FVector& FeetOrigin, const FRotator& ViewRotation) = 0;

	// The held-use pin, the FAR arm of retail's maintenance (`CBaseTerminal` slot 43,
	// `vampire.dll` 0x10218320). `Ray_t::Init(start = player origin, end = (Target.X, Target.Y,
	// player.Z), mins/maxs = the player's own collision OBB)` with a `CTraceFilterSimple` on the
	// player, traced against `MASK_PLAYERSOLID` (`0x0201400b`); the trace's `endpos` then goes
	// straight through `SetAbsOrigin` + `Relink` — **unconditionally**. There is no fraction test
	// and no start-solid test; the conditional the decompiler shows around it gates only a debug
	// overlay line (`slice-bc-decompiles.md` §1.4, correction C1).
	//
	// **Named modernization.** `MASK_PLAYERSOLID` is the union of world solidity and the terminal's
	// own `SOLID_BBOX` (raised by its Spawn, §1.3). This port keeps placed prop bodies non-solid, so
	// the union is reached as two sweeps: the pawn's own movement channel for the world, and
	// `ELYSIUM_USE_CHANNEL` where the use anchor stands in for the terminal's box. The nearer
	// contact wins.
	//
	// This is not `TeleportPlayer`: that is a one-shot relocate that also writes the view rotation,
	// and the pin runs every frame of a live session and must leave the view alone.
	//
	// False = no player, no movement body, or a headless world; `OutContactCm` is untouched.
	virtual bool SweepPlayerHullToward(const FVector& TargetCm, FVector& OutContactCm)
	{
		return false;
	}

	// The NEAR arm of the same maintenance (`CBaseTerminal` slot 41, `0x102182b0` ->
	// `FUN_10178590`): `dir = Target - playerEye`, `VectorAngles(dir)`, and the player's eye angles
	// are **snapped** to it. Terminals never raise the view-angle lock bit (`m_iVFlags 0x8`), so the
	// usercmd's own angles still arrive every tick and this re-snaps over them — the player cannot
	// look away from the machine while a session is live.
	//
	// False = no player or a headless world.
	virtual bool SnapPlayerViewTo(const FVector& TargetCm) { return false; }

	// trigger_hurt / a door closing on the player. No-op when there is no player.
	virtual void DamagePlayer(float Amount) = 0;
	// Modern +use embodiment. Registration names engine components with substrate handles; the
	// query returns geometry only and leaves class eligibility/session policy to EntityWorld.
	virtual void RegisterUseAnchor(UPrimitiveComponent* Source,
		const FElysiumEntityHandle& Owner) = 0;
	virtual void SetUseAnchorEnabled(const FElysiumEntityHandle& Owner, bool bEnabled) = 0;
	// Drop every anchor this owner registered, destroy the query proxies that were made for it and
	// release whatever the registration stood on the side (a terminal's glass). The mirror of
	// `RegisterUseAnchor`, needed because that call de-duplicates on the COMPONENT and a rebuilt
	// body is a new component every time — without an unregister, a runtime `SetModel` appends
	// records instead of replacing them.
	virtual void UnregisterUseAnchor(const FElysiumEntityHandle& Owner) {}
	virtual void ClearUseAnchors() = 0;
	// Loose-item DefaultTouch embodiment. The component remains presentation only: overlap produces
	// an entity touch edge, and the item/inventory transaction decides whether acquisition succeeds.
	virtual void RegisterTouchAnchor(UPrimitiveComponent* Source,
		const FElysiumEntityHandle& Owner) {}
	virtual void SetTouchAnchorEnabled(const FElysiumEntityHandle& Owner, bool bEnabled) {}
	virtual void ClearTouchAnchors() {}
	virtual FElysiumUseQueryResult QueryPlayerUse(
		const FElysiumEntityHandle& CurrentFocus) const = 0;

	// `CBasePlayer::Replenish`'s direct victim search (`docs/vtmb/feeding.md` § "Target
	// acquisition and acceptance"): a hull trace from the view position toward the local offset
	// (32 forward, 0 right, -32 vertical) with extents (-8,-8,-8)..(8,8,8). It is a separate query
	// from `+use` because it is a separate retail search with its own shape and its own mask — the
	// feed reaches DOWN and forward for a body, where `+use` reaches along the aim for a control.
	//
	// Geometry only: the handle it returns is a candidate, and every eligibility question
	// (paired state, automatic acceptance, `ResistsFeeding`, the opposed check) stays in the
	// substrate. Invalid means nothing was in the hull, which is the ordinary answer. The cone /
	// radius survey that supplies the small-animal route (`rat_feed_arc`, `rat_feed_radius`) is
	// deliberately absent: rat feeding is out of this ordinary-feed scope.
	virtual FElysiumEntityHandle QueryFeedTarget() const { return FElysiumEntityHandle::Invalid(); }

	// The ranged shot's aim query: what the player's crosshair is on, out to `MaxRangeCm`.
	//
	// **A stated divergence, not a reproduction.** Retail's ranged attack builds a fire packet
	// carrying a muzzle origin, an aim vector and a spread cone, and traces one ray per `Ammo_Fired`
	// through that cone. The cone itself is the authored `SpreadAngle`/`SpreadAngleMax` pair
	// selected by the live ranged-accuracy value, and THAT interpolation input is unrecovered
	// (`docs/vtmb/combat-and-damage.md`) — so no honest reproduction of the dispersion exists to write yet.
	// This answers the cone's degenerate zero-spread case: one ray down the aim, reduced to the
	// single victim handle the attack transaction's `Swing.Opponent` already carries. When the input
	// is recovered, the packet replaces this rather than wrapping it.
	//
	// Geometry only, like every other query on this seam: candidacy, damage and the volley share
	// stay in the substrate. Invalid is the ORDINARY answer — a shot fired at nothing in particular
	// is the common case, and the headless/null answer is the same Invalid for the same reason.
	virtual FElysiumEntityHandle QueryAimTarget(float MaxRangeCm) const
	{
		return FElysiumEntityHandle::Invalid();
	}

	// The two perception queries (`docs/architecture/gameplay-systems-architecture.md`
	// §5.5.3). Each supplies a missing WORLD TERM and never a verdict: cone, range, cadence, grace,
	// debounce, the `vision`/`hearing`/`npc_perception` tuning and every threshold stay substrate
	// rules (K13). A service that answered "this NPC can see the player" would have taken the
	// decision instead of supplying the term.

	// Is the straight segment between two world points clear of solid world geometry?
	//
	// The headless/null answer is **true**, and it is load-bearing rather than a placeholder: a
	// `-nullrhi` Substrate run and an editor commandlet have no collision world, and an
	// implementation that reported "blocked" there would blind every NPC in exactly the runs meant
	// to prove they can see.
	//
	// Divergence, named: retail traces with content mask `0x4091` (solid world + opaque). Source
	// content masks are not portable to Unreal's channel set, so the semantics are adapted rather
	// than the number — the implementation traces `ELYSIUM_USE_CHANNEL`, which is the project's
	// solid-world channel and the one the map's brush bodies and the material-less `.hulls` walkable
	// surface already answer on. Characters are deliberately NOT occluders here: retail's mask
	// carries no NPC/player bits, so a body standing between two points does not break the line.
	virtual bool QueryLineOfSight(const FVector& FromCm, const FVector& ToCm) const { return true; }

	// `CBaseCineCam::FindBestShot`'s visibility predicate `FUN_1006db10` (SC8): can the camera see
	// what the shot is aimed at?
	//
	//   mins = (-1,-1,-1); maxs = (1,1,1);                   // a 2-unit hull
	//   filter = CTraceFilterSimple(shot subject, 0);        // the SUBJECT never blocks
	//   UTIL_TraceHull(anchor, lookAt, mins, maxs, 0x1400b, filter, &tr);
	//   fail on tr.fraction < 1.0 || tr.startsolid || tr.allsolid
	//
	// It is a distinct seam from `QueryLineOfSight` because it is a distinct retail query with its
	// own shape (a swept hull, not a ray), its own mask and its own filter: the visibility test that
	// admits a candidate shot must be able to exclude one entity — retail's `CTraceFilterSimple`
	// ignoring `m_hSubject` is what stops the player's own body from rejecting every shot framed on
	// him.
	//
	// **Mask, verified.** `0x1400b` is `CONTENTS_SOLID 0x1 | CONTENTS_WINDOW 0x2 | CONTENTS_GRATE 0x8
	// | CONTENTS_MOVEABLE 0x4000 | CONTENTS_PLAYERCLIP 0x10000` — Source's `MASK_PLAYERSOLID_BRUSHONLY`.
	// It is `MASK_PLAYERSOLID` (`0x201400b`, the mask `CanStartGrappleAttack`'s crouch trace uses)
	// **minus `CONTENTS_MONSTER 0x2000000`**, so no character is an occluder at all and the subject
	// filter is belt-and-braces on top of that. The port keeps the semantics rather than the number:
	// the trace runs on `ELYSIUM_USE_CHANNEL`, this project's solid-world channel, exactly as
	// `QueryLineOfSight` does and for the same reason (the `.hulls` walkable surface is
	// material-less and answers only there).
	//
	// `OutStartSolid` carries **both** retail flags: Source distinguishes `startsolid` from
	// `allsolid`, Unreal's `FHitResult::bStartPenetrating` is their union, and the predicate ORs the
	// two anyway.
	//
	// The return value is "a collision world answered", not "blocked". False is the headless answer
	// — a `-nullrhi` Substrate run has no geometry — and it leaves `OutFraction` at 1 and
	// `OutStartSolid` false, so a candidate shot is admitted rather than silently refused.
	virtual bool TraceCameraHull(const FVector& FromCm, const FVector& ToCm,
		const FVector& HalfExtentCm, const FElysiumEntityHandle& IgnoreEntity,
		float& OutFraction, bool& OutStartSolid) const
	{
		OutFraction = 1.0f;
		OutStartSolid = false;
		return false;
	}

	// One sub-step of a melee swing's swept contact: which live characters' bodies the
	// authored contact segment passed through as it moved from where it was at the start of the
	// sub-step to where it is at its end.
	//
	// Geometry only, like `QueryAimTarget` beside it: WHEN the segment is live, which record it
	// belongs to, whether the victim has already been hit and what a hit costs all stay in the
	// substrate. An empty answer is the ORDINARY outcome — most sub-steps of most swings touch
	// nothing — and is also what a headless run answers, for the same reason and with the same
	// meaning.
	//
	// **Boundaried, and named.** The victim volume is the candidate body's own rendered bounds and
	// the swept segment is tested against it directly, exactly as `QueryAimTarget`/`QueryFeedTarget`
	// test their ray and their hull; the engine trace is spent on the occlusion half, where a wall
	// between the limb and the body is what has to be asked of the collision world. Sweeping a
	// physics shape instead would answer with a primitive component, and this runtime keeps no
	// component -> entity map outside the `+use` and touch anchor registries — building one is a
	// change to the body factory rather than to this query.
	virtual void QuerySwingContacts(const FElysiumSwingSweep& Sweep,
		TArray<FElysiumEntityHandle>& OutHits) const
	{
		OutHits.Reset();
	}

	// How lit is this point, normalized 0 (dark) to 1 (fully lit)?
	//
	// The headless/null answer is **1.0** — full light — for the mirror of the reason above: a run
	// with no light rig must not read as pitch dark and hand the stealth surface a free pass it
	// never earned.
	//
	// Divergence, named: retail samples the BAKED LIGHTMAP at the point. This runtime has no
	// lightmap — the world is fully dynamic — so the estimate is computed from the authored runtime
	// light set instead (`UElysiumLightRig`, the same `.lights` rows re-derived at load), as the sum
	// of the attenuated contributions of the sources whose authored radius covers the point. Two
	// consequences follow and both are accepted for this landing: the estimate ignores OCCLUSION, so
	// a point in a lit room's shadow reads as lit; and it ignores the sky/sun terms, which are
	// unoccluded whole-map values that would otherwise read every interior as fully lit.
	virtual float QueryLightAtPoint(const FVector& PointCm) const { return 1.0f; }

	// R6.2 (`seam_map_map_lighting.md` -> "Switched lights and lightstyles"): Source's
	// `engine->LightStyle(style, pattern)`. A `light`/`light_spot` writes its style's pattern here
	// and the rig's clock reaches every baked source carrying that style. Headless: nothing.
	virtual void SetLightStylePattern(int32 Style, const FString& Pattern) {}
	virtual FString LightStylePattern(int32 Style) const { return FString(); }
	// R6.2: a `light_dynamic`'s runtime light, stood on the legacy derivation path (a raw
	// magnitude, reach and cosines the rig derives from). Attached under `Parent` when given, at
	// the spec's world transform. Null headless. `DestroyDynamicLight` unregisters it from the rig
	// and the actor; the leaf calls it on Kill and teardown.
	virtual class ULightComponent* BuildDynamicLight(const struct FElysiumDynamicLightSpec& Spec,
		USceneComponent* Parent) { return nullptr; }
	virtual void DestroyDynamicLight(class ULightComponent* Light) {}
	// R6.1 (`seam_map_map.md` -> "Sprites (R6.1)"): CSprite's draw switch. The billboard is the
	// bake's actor tagged with the entity's lump ordinal; the `env_sprite` leaf writes its
	// `bOn && !IsInert()` here on spawn, on every input and on load. Headless, and on a map with
	// no baked sprites: nothing.
	virtual void SetBakedSpriteVisible(int32 EntityIndex, bool bVisible) {}

	// Is the player's body in the sneak posture? (13.1, `docs/vtmb/stealth.md` -> "Player
	// target-surface update", step 3.)
	//
	// The headless/null answer is **false** — the non-stealth fallback — because a run with no body
	// has no posture, and the fallback is what a player who is not sneaking gets.
	//
	// CHOSEN, NOT RECOVERED: the recovered eligibility predicate is "a player-state flag bit plus a
	// second state helper", and neither has a recovered human-readable name. What IS recovered is
	// that the stealth-kill transaction's own player gate reads "sneak posture, ducking, or active
	// Obfuscate" — so the duck is a named term of the same family, and it is the one player-state
	// term this runtime's body can answer. The Obfuscate arm is deliberately NOT folded in here:
	// that is the stealth-kill gate's, and inventing a second consumer for it would be a rule we
	// do not have. Replace the implementation when the predicate is recovered; the substrate's own
	// decision stays on its side of this call either way (K13).
	virtual bool IsPlayerSneaking() const { return false; }

	// Whether the player's body has ground contact, read off the same locomotion record its mover
	// publishes. One term of retail's block input predicate (`0x10160ec0`: the `+wpn_secondaryatk`
	// bit, ground contact and an active melee weapon — `docs/vtmb/controls.md` § "Attack, block and
	// weapon commands"), and the only one of the three that is a physics fact rather than a game
	// one, so it is asked here instead of derived in the substrate.
	//
	// False with no body: a player who is not standing anywhere is not standing on the ground. The
	// substrate's own predicate short-circuits ahead of this call, so a world with no pawn is never
	// asked in the first place.
	virtual bool IsPlayerOnGround() const { return false; }

	// ---- A1, footsteps: the player's whole locomotion record ------------------------------------
	// The record the player's mover published at its tick tail, copied out whole.
	//
	// A QUERY (S11): every field of it is something only a body that moved can know — the realized
	// velocity, ground contact, water depth, the duck ramp and the surfaceprop under the foot — and
	// the step clock this feeds is the substrate's own rule over those premises, ported from
	// `CBasePlayer::UpdateStepSound 0x1011e940`. Nothing here is arbitrated on this side of the
	// seam: the whole record crosses, and which interval, which foot, which pool and how loud stay
	// in plain C++.
	//
	// **The headless answer is `false`, and `Out` is left untouched.** A run with no pawn has no
	// published record at all, and that is a different fact from a record full of zeroes: a zeroed
	// sample reads as a body standing still on the ground with no surface, which a step clock would
	// happily tick forever. The caller must branch on the bool rather than on the sample, which is
	// why the record travels in an out-parameter instead of being returned by value.
	//
	// Deliberately the WHOLE sample and not the three scalars the clock needs, unlike
	// `IsPlayerOnGround` and `GetPlayerBaseActivity` above: those two are single terms of predicates
	// that ask nothing else of the body, while this is the one producer's one record, and splitting
	// it into per-consumer accessors is how the player's locomotion becomes two systems.
	virtual bool SamplePlayerLocomotion(FElysiumLocomotionSample& Out) const { return false; }

	// The LOGICAL activity the player's body is currently classified into — retail's ideal activity
	// (`m_IdealActivity`), un-translated. The melee primary's airborne fork switches on exactly this
	// (`docs/vtmb/animation_and_movers.md` § "Player action selection is code around the model
	// table"), so the substrate asks for the published value rather than re-deriving a body state
	// of its own.
	//
	// It is the same shape and the same reason as `IsPlayerOnGround` above: one producer publishes
	// the fact, and a substrate predicate reads it. Deliberately a single string rather than the
	// whole selection record — that record is the resolution's diagnostic, and a substrate caller
	// able to read it would be a second reader of the resolution.
	//
	// **Translation stays out of it.** The value is the request as classified, never the sequence
	// set that realizes it: an `ACT_FALLING` translated per weapon is still a falling body.
	//
	// Empty is the honest answer for a world with no body, a body whose driver has never ticked, and
	// a headless run. An empty activity matches no fork, so a caller reads the grounded form — which
	// is what a player who is not airborne anywhere gets.
	virtual FString GetPlayerBaseActivity() const { return FString(); }

	// Discard the player body's carried motion, `CBasePlayer::PostThink`'s melee stop.
	//
	// Retail's pair is `SetAbsVelocity(vec3_origin)` then `SetLocalVelocity(vec3_origin)`, and this
	// runtime's body carries the same two: the mover's world velocity, and the locomotion sample
	// published off it that every animation reader takes the body's motion from. Both go, or the
	// selector still reads the lunge out of a record the component no longer agrees with.
	//
	// It is a command rather than a query because stopping a body is the engine's to perform — the
	// substrate decides WHEN, on the rule in `ElysiumClipMovement::StopsMeleeTailMotion`, and never
	// touches a component.
	virtual void StopPlayerBody() {}

	// CNPCMaker's host geometry. The substrate owns admission order and all policy; these four calls
	// only answer the engine-shaped questions at the point each guard is reached. Defaults are the
	// supported headless/fail-open posture.
	virtual float ResolveNpcMakerGroundZ(const FVector& MakerOriginCm, float TraceDepthCm) const
	{
		return MakerOriginCm.Z;
	}
	virtual bool IsNpcMakerVisibleFromPlayer(const FVector&) const { return false; }
	virtual bool IsNpcMakerInPlayerViewCone(const FVector&) const { return false; }
	virtual bool IsNpcMakerSpawnAreaOccupied(const FVector&, float) const { return false; }

	// R7.2 -- the ranged shot's forward world trace and the stain it leaves
	// (`docs/project/seam_migration.md` -> "R7.2 Decals", owner call B).
	//
	// The substrate owns WHEN (a shot that has been paid for) and the variation roll; the trace,
	// the hit's `UElysiumPhysicalMaterial` and the decal itself are engine questions and live on
	// the other side of this call. Trace the render-surface channel from `FromCm` along `Direction`
	// for `RangeCm`, read the hit's surface character, and lay that character's hole through
	// `UElysiumDecalSubsystem::Lay` (`ElysiumImpactDecals`, `docs/vtmb/effects.md` §3.5).
	//
	// `Direction` need not be normalized. `Variation` is 1..5, the pool's own five variations.
	// False is the ordinary answer for a shot that hit nothing, a headless run and a world with no
	// decal subsystem — a stain nobody sees is never a failure.
	virtual bool LayShotImpactDecal(const FVector& FromCm, const FVector& Direction, float RangeCm,
		int32 Variation) { return false; }

	// The legacy scripted-shot channel. `SetCamera(shotfile)`, `camera_keyframe`, and the feed
	// camera push onto the player camera's one weight stack through here, and
	// `RemoveCamera` pops. `ShotFile` keys `vdata/camerashots/`; `Subject` is the entity the shot is
	// about, which is what its `DialogTarget` anchors resolve to. Returns 0 when the shot does not
	// parse, nothing it anchors to is there, or there is no camera (a headless world runs the
	// consumer without one). The channel is here, on the player's *body*, rather than on
	// IElysiumPresenter: the camera is part of the body (S3), and the presenter carries what is put
	// on *screen*, not what the player's body does.
	virtual int32 PushCameraShot(const FString& ShotFile, const FElysiumEntityHandle& Subject) = 0;
	// One named block of a **multi-shot** file. `vdata/camerashots/special-case.txt` carries five
	// siblings under `CameraShotTable`, and retail addresses one of them by name
	// (`FUN_10070470("Hacking", NULL, terminal, terminal, NULL)`, §7.3 step 6) rather than by file.
	// `Subject` is bound as the shot's Named slots, which is what its bare `Position: Named` anchors
	// resolve to. Returns 0 when the file, the block or its anchors do not resolve — a terminal
	// takes that as a refusal, because the shot IS the framing.
	// `Exposure` has no retail counterpart at all -- this engine has no tonemapper and no exposure
	// cvar (`slice-bc-decompiles.md` §7, correction C20). It is the pusher's own ask, carried for the
	// handle's lifetime, and the one named Presentation modernization in
	// `docs/architecture/computer-terminal-architecture.md` §6.4: a terminal shot fills the frame
	// with one bright emissive panel at close range, which UE's auto-exposure would answer by ramping
	// the rest of the room into black.
	// `Exposure` is stated at every call site rather than defaulted: this header only forward-declares
	// the enum, and a shot's exposure policy is a decision, not a fallback.
	virtual int32 PushCameraShotNamed(const FString& ShotFile, const FString& ShotName,
		const FElysiumEntityHandle& Subject, EElysiumShotExposure Exposure)
	{
		return 0;
	}
	// Value shots are the camera_track path: the entity world owns the sampler and publishes the
	// resulting world-space value without teaching the camera component about entities.
	virtual int32 PushCameraShotValue(const FElysiumCameraShot& Shot) = 0;
	virtual bool UpdateCameraShotValue(int32 ShotId, const FElysiumCameraShot& Shot) = 0;
	// A **shot start on a handle that is already up** — `camera_cinematic`'s re-shot branch
	// (`FUN_10070780` step 4: `SetShot` then `FUN_1006e8e0` on the camera the player has already
	// adopted). It re-stamps retail's `m_nClientResetFrame` so the consumer arms shot start again,
	// without changing the entity, its id or `m_iCameraOverrideIdx`. The per-tick goal publish
	// (`UpdateCameraShotValue`) deliberately does NOT re-stamp it.
	virtual bool RestartCameraShot(int32 ShotId) { return false; }

	// The equipped item's authored `camera_class` bits. Pushed whenever the **player's** active weapon
	// changes, because drawing a weapon re-runs the arbitration that can force the view mode
	// (`docs/vtmb/camera-view-modes.md` §2). The camera takes the bits and never learns what an item
	// is; an NPC's weapon does not arbitrate the player's view, so only the player publishes.
	virtual void SetEquippedCameraClass(int32 CameraClass) {}
	virtual bool PopCameraShot(int32 ShotId, float BlendOutSeconds = -1.0f) = 0;
};

// --------------------------------------------------------------------------------------------
// Audio — the voice pool and the map's SoundScheme control.
//
// Implemented by AElysiumMapActor, which owns the per-map FElysiumSoundSchemeManager and forwards
// the voice calls to the GI-scoped UElysiumAudioSubsystem. The substrate never holds either.
// --------------------------------------------------------------------------------------------

// A2 (footsteps): Source's `CHAN_*`, which is what an `IEngineSound::EmitSound` call selects with
// its third argument. A channel is a SLOT ON THE ENTITY, not a mixer bus: an entity holds one voice
// per channel and a second sound on the same channel REPLACES the first. That is the whole reason
// this enum exists here rather than as a routing hint — `PlayBodySound` implements the replacement.
//
// The numbers are Source's own (`CHAN_AUTO` 0 ... `CHAN_STATIC` 6, with `CHAN_ITEM` 3 and
// `CHAN_BODY` 4); footsteps use `Body`, both for the NPC event path (`vampire.dll 1026d460` passes
// a literal 4) and for the player's clock (`CGameMovement::PlayStepSound`, `1011e430`).
enum class EElysiumSoundChannel : uint8
{
	Auto   = 0,
	Weapon = 1,
	Voice  = 2,
	Item   = 3,
	Body   = 4,
	Static = 6,
};

// A2 (footsteps): one body-attached one-shot, in the terms `EmitSound` takes.
//
// `SoundLevelDb` is Source's `soundlevel_t` — the dB number
// `ElysiumSoundLevel::FromDistanceUnits` computes from the authored audible distance, NOT VtMB's
// AI-hearing level. 75 is the player's own fixed step level (`1011e430` pushes `0x4b`); an NPC's
// is computed per step. `Pitch` is a multiplier (retail's `PITCH_NORM` 100 is 1.0 here, and the
// player's `95 + RandomInt(0,10)` jitter is 0.95..1.05).
struct FElysiumBodySound
{
	// The engine-relative path under `sound/`, as `PlayVoice` takes it.
	FString Rel;
	float Volume = 1.f;
	// 75 = `ElysiumSoundLevel::PlayerStepLevelDb`, spelled as a literal so this header does not have
	// to pull `Sound/SoundAttenuation.h` in behind `ElysiumSoundLevel.h`.
	int32 SoundLevelDb = 75;
	float Pitch = 1.f;
	EElysiumSoundChannel Channel = EElysiumSoundChannel::Body;
};

class IElysiumAudio
{
public:
	virtual ~IElysiumAudio() = default;

	// The map implementation stamps its active epoch before forwarding. Stable ownership remains
	// logical here; a weak attachment is only resolved by the subsystem on the game thread.
	virtual FElysiumVoiceHandle Submit(FElysiumAudioRequest Request) = 0;
	virtual void Prefetch(const FElysiumAudioSource& Source) = 0;
	virtual void PauseVoice(FElysiumVoiceHandle Handle, bool bPaused) = 0;
	virtual void SeekVoice(FElysiumVoiceHandle Handle, float MediaOffsetSeconds) = 0;
	virtual void SetVoicePitch(FElysiumVoiceHandle Handle, float Pitch) = 0;
	virtual void CancelAudioOwner(FElysiumAudioOwner Owner, float FadeSeconds = 0.f) = 0;

	// Compatibility translation for the existing entity leaves. New integrations submit a typed
	// request above; this method constructs exactly that request rather than owning a second path.
	virtual FElysiumAudioVoiceHandle PlayVoice(const FString& Rel, const FElysiumPlayParams& Params) = 0;

	// A2 (footsteps): an EXECUTION — one body-attached one-shot on one of the owner's channels.
	// The seam every `EmitSound(..., CHAN_BODY, ...)` port lands on (footsteps first; impacts,
	// 4020 and the weapon foley later), so the sound-level model and the channel rule are
	// implemented once instead of per producer.
	//
	// The map actor plays it attached to the owner's `GetAttachBody()` when the entity has one and
	// at the entity's origin when it does not, with attenuation from
	// `ElysiumSoundLevel::MakeAttenuation(SoundLevelDb)`, and REPLACES whatever it was already
	// playing on the same `(Owner, Channel)` — Source's channels hold one voice each, which is why
	// two footsteps 40 ms apart never overlap in retail. A recording double records the request.
	//
	// The returned handle is `FElysiumAudioVoiceHandle::Invalid()` when nothing was played (an
	// empty `Rel`, an owner that does not resolve, no audio device).
	virtual FElysiumAudioVoiceHandle PlayBodySound(const FElysiumEntityHandle& Owner,
		const FElysiumBodySound& Sound) = 0;

	virtual void StopVoice(FElysiumAudioVoiceHandle Handle, float FadeSeconds) = 0;
	virtual void SetVoiceVolume(FElysiumAudioVoiceHandle Handle, float Volume) = 0;
	virtual bool IsVoicePlaying(FElysiumAudioVoiceHandle Handle) const = 0;

	// 6.3 — crossfade a SoundScheme in as the active one (bed + music stems + random scheduler),
	// or fade it out if it is the one running. Anchor is the ambient_soundscheme entity's origin.
	virtual void FadeInScheme(const FString& SchemeRel, const FVector& Anchor, float FadeSeconds) = 0;
	virtual void FadeOutScheme(const FString& SchemeRel, float FadeSeconds) = 0;
	// The scheme currently running, or empty. What an ambient_soundscheme reports as its own state.
	virtual FString ActiveSchemeRel() const = 0;

	// 12.2b — how far ahead of an authored instant a cue must be submitted for its first sample to
	// be heard at that instant. VtMB hands its scenes `snd_mixahead`, which is Source's mixer's own
	// lead; the behaviour that constant encodes is reproduced by leading with *this* path's latency
	// instead, composed by UElysiumAudioSubsystem from the device it is actually running on.
	//
	// The default is the no-device fallback, which is also what a substrate-only world answers.
	virtual float OutputLeadSeconds() const { return ElysiumAudioLatency::FallbackLeadSeconds; }
};

// --------------------------------------------------------------------------------------------
// Travel — the map lifecycle.
//
// Implemented by AElysiumMapActor, forwarding to the GI-scoped UElysiumMapSubsystem. Both calls
// are requests, not transitions: the subsystem decides when the travel actually happens.
// --------------------------------------------------------------------------------------------
class IElysiumTravel
{
public:
	virtual ~IElysiumTravel() = default;

	// `trigger_changelevel` — queue a landmark transition. Offset is the player's displacement
	// from THIS map's landmark, re-added to the destination's same-named one; Yaw is carried across.
	virtual void RequestLandmarkTravel(const FString& Map, const FString& Landmark,
		const FVector& Offset, float Yaw) = 0;
	// A plain map change with no landmark (the player is placed at the destination's own spawn).
	virtual void ChangeMap(const FString& Map) = 0;
};

// --------------------------------------------------------------------------------------------
// Presenter — what the substrate puts on screen.
//
// Implemented by UElysiumPresentationSubsystem, the world-scoped publisher of
// FElysiumViewState. These are **announcements of discrete moments**, not the state itself: the
// fade, the open panel and the open conversation stay on FElysiumEntityWorld, because each is world
// state with a lifetime (the map epoch owns it and saves it). The publisher samples that state
// once per frame and uses these calls to know *when* something happened, which is what its discrete
// delegates carry — a diff cannot tell a conversation that closed and reopened in one frame from one
// that never moved.
//
// Null where there is no publisher at all: a Substrate-tier world with no engine behind it, or an
// editor preview world. A test stub implements it to assert "the chain faded the screen and opened
// this panel" with no HUD to look at.
// --------------------------------------------------------------------------------------------

// One transient HUD announcement. Gameplay chooses the semantic kind and source-derived subject;
// the player UI owns fixed labels, typography, animation and placement. It is deliberately not
// save state: an event retained here belongs only to the current world epoch.
enum class EElysiumNotificationKind : uint8
{
	ItemAcquired,
	QuestUpdated,
	QuestCompleted,
	QuestFailed,
	Generic,
};

inline const TCHAR* ElysiumNotificationKindName(EElysiumNotificationKind Kind)
{
	switch (Kind)
	{
	case EElysiumNotificationKind::ItemAcquired:   return TEXT("ItemAcquired");
	case EElysiumNotificationKind::QuestUpdated:   return TEXT("QuestUpdated");
	case EElysiumNotificationKind::QuestCompleted: return TEXT("QuestCompleted");
	case EElysiumNotificationKind::QuestFailed:    return TEXT("QuestFailed");
	case EElysiumNotificationKind::Generic:        return TEXT("Generic");
	}
	return TEXT("Unknown");
}

struct FElysiumNotification
{
	EElysiumNotificationKind Kind = EElysiumNotificationKind::Generic;
	FString Subject;
	int32 Quantity = 1;

	bool operator==(const FElysiumNotification& Other) const
	{
		return Kind == Other.Kind && Subject == Other.Subject && Quantity == Other.Quantity;
	}
};

class IElysiumPresenter
{
public:
	virtual ~IElysiumPresenter() = default;

	// `env_fade` — a full-screen colour fade. Parameters are the Fade input's, verbatim.
	virtual void StartFade(const FLinearColor& Color, float Duration, float HoldTime, float MaxAlpha,
		bool bFadeIn, bool bAutoReverse) = 0;

	// `game_sign` — the one sign panel on screen. CloseSign is the dismissal (left-click,
	// CloseWindow, or the owner dying).
	virtual void OpenSign(const FElysiumEntityHandle& Owner, const TSharedPtr<const FElysiumSignData>& Data,
		float FadeInSeconds) = 0;
	virtual void CloseSign() = 0;

	// The one conversation on screen.
	virtual void OpenDialog(const FElysiumEntityHandle& Owner, FElysiumDlgConversation& Conversation) = 0;
	virtual void CloseDialog() = 0;

	// A FIFO HUD notification. Unlike the replaceable retained surfaces above, every admitted
	// event matters: same-frame grants stay distinct and are presented in arrival order.
	virtual void PostNotification(const FElysiumNotification& Notification) = 0;
};

struct FElysiumWeatherTransition
{
	float CurrentWetness = 0.0f;
	float TargetWetness = 0.0f;
	double StartTime = 0.0;
	float Duration = 0.0f;
};

struct FElysiumWeatherEmitterState
{
	FElysiumEntityHandle Entity;
	FVector LocationCm = FVector::ZeroVector;
	FString ParticleDefinition;
	bool bActive = false;
	// The entity is dead (`Kill`): the placed actor is removed now rather than let finish.
	bool bDead = false;
	// Bumped by every `TurnOn` input. VtMB's client rebuilds the emitter whenever the activation
	// timestamp changes, so a `TurnOn` on an already-active emitter restarts it; the embodiment
	// restarts when this changes while `bActive` holds (`effects-architecture.md` §5.2).
	uint32 TurnOnSerial = 0;
	// VtMB's `attach_type`, the 19-value enum (`effects-architecture.md` §5.7). 0/-1 origin,
	// 1/3 bone tree, 2 bone point, 6/17 attachment (follow / snap once), 9 the parent's box,
	// 10/11 the viewer box (weather's rain follow), 15 the brush box (`func_particle`).
	int32 AttachType = 0;
	FString ParentName;
	FString AttachBone;
	int32 AttachPoint = 0;           // `attach_point` (m_nAttachPoint), the numbered attachment
	float BoundsCm = 0.0f;           // `spawnbounds` x 2.54 -- the viewer cube of modes 10/11
	float RateScale = 0.0f;
	float RampStartScale = 0.0f;
	float RampTargetScale = 0.0f;
	double RampStartTime = 0.0;
	float RampDuration = 0.0f;
	// `func_particle` only: the brush's world AABB (mode 15's spawn box) and CFuncParticle::
	// Activate's size scalar, clamp(volume / 128^3, 0.01, 100), folded into the one rate float.
	FBox BrushBoundsCm = FBox(ForceInit);
	float VolumeScale = 1.0f;
};

// R7.3 (`effects-architecture.md` §5.6): what a `func_dustmotes` publishes to stand its motes.
// Points are world cm, pre-sampled inside the brush solid by the leaf (ten retries each, the
// convex set off the entity's own hulls); the actor re-expresses them in its own frame.
struct FElysiumDustState
{
	FElysiumEntityHandle Entity;
	bool bActive = false;
	bool bFrozen = false;
	TArray<FVector> SpawnPointsCm;
	FBox BoundsCm = FBox(ForceInit);
	float SpawnRate = 0.0f;          // motes / s
	FLinearColor Color = FLinearColor::White;   // rgb = Color / 255, a = Alpha / 255
	float SpeedMaxCm = 0.0f;
	float SizeMinCm = 0.0f;
	float SizeMaxCm = 0.0f;
	float LifetimeMin = 0.0f;
	float LifetimeMax = 0.0f;
	float DistMaxCm = 0.0f;
};

// What an `env_steam` publishes: Valve's CSteamJet keys in cm / s / 0..1.
struct FElysiumSteamState
{
	FElysiumEntityHandle Entity;
	bool bActive = false;
	int32 Type = 0;                  // 0 normal, 1 heatwave (the refract material)
	float SpreadSpeedCm = 0.0f;
	float SpeedCm = 0.0f;
	float StartSizeCm = 0.0f;
	float EndSizeCm = 0.0f;
	float Rate = 0.0f;
	float JetLengthCm = 0.0f;
	float Lifetime = 0.0f;           // JetLength / Speed
	FLinearColor Color = FLinearColor::White;   // rendercolor / 255, renderamt / 255
};

// What an `env_beam` publishes per strike or state change: the resolved endpoints (world cm),
// the taper's start width, the raw noise amplitude (the actor scales it by length / 100), the
// scroll rate and the colour. `bActive` is the continuous state or the striker's window.
struct FElysiumBeamState
{
	FElysiumEntityHandle Entity;
	bool bActive = false;
	FVector StartCm = FVector::ZeroVector;
	FVector EndCm = FVector::ZeroVector;
	float WidthCm = 0.0f;
	float NoiseAmplitudeCm = 0.0f;
	float TextureScroll = 0.0f;
	FString Texture;                 // `vtmb:material:sprites/beama`, the material lane's MI_
	FLinearColor Color = FLinearColor::White;
};

class IElysiumWeather
{
public:
	virtual ~IElysiumWeather() = default;
	virtual void ApplyWetness(const FElysiumWeatherTransition& Transition) = 0;
	virtual void ApplyEmitter(const FElysiumWeatherEmitterState& Emitter) = 0;
	virtual void RemoveEmitter(const FElysiumEntityHandle& Entity) = 0;
	// R7.3: the three Valve classes drive their bake-placed actors by entity index through here.
	// Headless, and on a map that bakes no such actor: nothing.
	virtual void ApplyDust(const FElysiumDustState& Dust) {}
	virtual void ApplySteam(const FElysiumSteamState& Steam) {}
	virtual void ApplyBeam(const FElysiumBeamState& Beam) {}
};

// The bundle FElysiumEntityWorld is constructed with. By value — four raw pointers to objects that
// outlive the world (the map actor owns the world; the subsystems outlive the map). Default-
// constructed is the fully headless case: a world with no engine behind it at all.
struct FElysiumWorldServices
{
	IElysiumEmbodiment* Embodiment = nullptr;
	IElysiumAudio*      Audio      = nullptr;
	IElysiumTravel*     Travel     = nullptr;
	IElysiumPresenter*  Presenter  = nullptr;
	IElysiumWeather*    Weather    = nullptr;
	IElysiumCameraService* Camera  = nullptr;
};
namespace ElysiumPlayerView
{
	// Entity angles remain Source QAngles. The Source->Unreal handedness reflection reverses view
	// pitch and yaw; roll retains its rotation about the reflected forward axis. Keep the inverse
	// beside it so player synchronization and teleport delivery cannot drift.
	inline FRotator ToUnreal(const FVector& SourceAngles)
	{
		return FRotator(-SourceAngles.X, -SourceAngles.Y, SourceAngles.Z);
	}
	inline FVector ToSource(const FRotator& UnrealRotation)
	{
		return FVector(-UnrealRotation.Pitch, -UnrealRotation.Yaw, UnrealRotation.Roll);
	}
}
