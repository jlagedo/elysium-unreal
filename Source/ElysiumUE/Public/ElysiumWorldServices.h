#pragma once

#include "CoreMinimal.h"
#include "ElysiumAudioSubsystem.h"   // FElysiumAudioVoiceHandle + FElysiumPlayParams (passed by value)
#include "ElysiumEntity.h"           // FElysiumFlexWrite (passed by view)
#include "ElysiumEntityHandle.h"
#include "ElysiumInteraction.h"
#include "ElysiumLocomotionSample.h" // FElysiumLocomotionSample (returned by value)

class FElysiumDlgConversation;
class IElysiumCameraService;
class USceneComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UPrimitiveComponent;
struct FElysiumCameraShot;
struct FElysiumEntityDef;
struct FElysiumSignData;
struct FElysiumStanceClips;
struct FElysiumDisposition;

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
	USkeletalMeshComponent* Visual = nullptr;
	UPrimitiveComponent* Attach = nullptr;
	UStaticMeshComponent* PhysicsProxy = nullptr;
	FString Stem;

	bool IsValid() const { return Visual != nullptr && Attach != nullptr; }
};

// Every authored runtime placement is expressed in Source feet space. The one exception is the
// existing save/stage payload, which predates the player entity and stores Unreal's capsule centre.
// Carry the space with the value and convert exactly once, when the real body's half-height is known.
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

class IElysiumNpcMotor
{
public:
	virtual ~IElysiumNpcMotor() = default;
	// `bAllowPartialPath` takes the best path the graph can offer instead of refusing the request.
	// A route point wants the refusal — it must never silently skip authored route data. A
	// scripted_sequence mark wants the partial walk: the transit is the point of the beat, and its
	// caller places the NPC on the mark when the walk ends short.
	virtual bool MoveTo(const FVector& FeetDestination, float AcceptanceRadiusCm,
		float SpeedCmPerSecond, bool bAllowPartialPath = false) = 0;
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

	// The body's realized locomotion (CCC1) — **the same record the player's mover publishes**, so
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

// The substrate's outbound seam (runtime-architecture.md §7, roadmap 11.2).
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

// --------------------------------------------------------------------------------------------
// Embodiment — bodies, meshes, clips, skins, and the player's own body.
//
// Implemented by AElysiumMapActor: every component it builds belongs to it and dies with it, so
// "the world logically owns the embodiments, the actor physically owns them" stays true (R1).
// The player half is here because the pawn IS the player's body (S3); 11.4 re-homes the player's
// *state* onto an entity, and these calls become ordinary entity operations at that point.
// --------------------------------------------------------------------------------------------
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

	// B3/8.5 — stand one NPC skeletal body, playing the standing idle its disposition selects.
	// Null on a missing/failed glb or an empty stem.
	virtual USkeletalMeshComponent* BuildNpcVisual(const FString& Stem, const FVector& Location,
		const FRotator& Rotation, float UniformScale, const FString& Disposition, int32 IdleVariant) = 0;
	// Promote an ordinary NPC's visual to a native movement body. Owner preserves the logical entity
	// identity through collision ingress. Null is the supported headless, backdrop,
	// disabled-navigation, or failed-spawn path; the NPC remains a standing entity.
	//
	// `Stem` and `Variant` are what the body's own animation selection is keyed on (CCC4): the model
	// names its clip vocabulary, and the variant is the repeatable token weighted choice rides on. The
	// caller is the one place that knows both, so they travel with the body rather than being looked
	// back up from it.
	virtual IElysiumNpcMotor* BuildNpcMotor(USkeletalMeshComponent* Body,
		const FElysiumEntityHandle& Owner, const FVector& FeetOrigin, float YawDegrees,
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
	virtual bool PlayNpcClip(USkeletalMeshComponent* Body, const FString& Stem, const FString& ClipName,
		bool bLoop, float* OutSeconds) = 0;
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
	// Select and play a manifest clip by VtMB ACT_* activity. Ambient interesting-place data is
	// authored in activities rather than clip labels; Variant makes its weighted pick repeatable.
	virtual bool PlayNpcActivity(USkeletalMeshComponent* Body, const FString& Stem,
		const FString& Activity, int32 Variant, bool bLoop, float* OutSeconds) { return false; }
	// Resolve the same deterministic activity selection without playing it. OutLabel is the NPC
	// vocabulary key that must go back through PlayNpcClip so the shared-bank owner is preserved;
	// OutAnimName is the concrete neutral-pose cell whose optional authored speed configures the motor.
	virtual bool ResolveNpcActivityClip(const FString& Stem, const FString& Activity, int32 Variant,
		FString& OutLabel, FString& OutAnimName, float& OutGroundSpeedCmPerSecond)
	{
		OutLabel.Reset();
		OutAnimName.Reset();
		OutGroundSpeedCmPerSecond = 0.f;
		return false;
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

	// 12.1 — a choreo scene's whole-cast performance. The clip lives in a cinematic anim set that
	// no NPC's include tree names, so it is addressed by the scene's own anim-set model plus the
	// actor's `bonerename` root (PL16) rather than through the clip vocabulary.
	virtual bool PlayCinematicClip(USkeletalMeshComponent* Body, const FString& Stem,
		const FString& AnimSetModel, const FString& BoneRoot, const FString& ClipName,
		bool bLoop, float* OutSeconds) = 0;
	virtual bool PreloadCinematicClip(USkeletalMeshComponent* Body, const FString& Stem,
		const FString& AnimSetModel, const FString& BoneRoot, const FString& ClipName) { return false; }
	virtual bool PreloadCinematicClipForModel(const FString& Stem, bool bPlayerMaterial,
		const FString& AnimSetModel, const FString& BoneRoot, const FString& ClipName) { return false; }
	virtual bool SeekCinematicClip(USkeletalMeshComponent* Body, float PositionSeconds) = 0;
	virtual void StopCinematicClip(USkeletalMeshComponent* Body) = 0;

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

	// 8.3 — stand a non-solid dynamic-prop body. 8.4 — stand the same mesh with its `.phy` collision
	// and authored mass, ready for the leaf to drive SetSimulatePhysics. Null on an unbaked model.
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
	// A scene-owned player-controller body is a stand-in, not a second visible subject. The authored
	// controller lifecycle owns this presentation gate; dialogue camera code never calls it.
	virtual void SetPlayerVisualSuppressed(bool bSuppressed) {}

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
	// trigger_hurt / a door closing on the player. No-op when there is no player.
	virtual void DamagePlayer(float Amount) = 0;
	// Modern +use embodiment. Registration names engine components with substrate handles; the
	// query returns geometry only and leaves class eligibility/session policy to EntityWorld.
	virtual void RegisterUseAnchor(UPrimitiveComponent* Source,
		const FElysiumEntityHandle& Owner) = 0;
	virtual void SetUseAnchorEnabled(const FElysiumEntityHandle& Owner, bool bEnabled) = 0;
	virtual void ClearUseAnchors() = 0;
	// Loose-item DefaultTouch embodiment. The component remains presentation only: overlap produces
	// an entity touch edge, and the item/inventory transaction decides whether acquisition succeeds.
	virtual void RegisterTouchAnchor(UPrimitiveComponent* Source,
		const FElysiumEntityHandle& Owner) {}
	virtual void SetTouchAnchorEnabled(const FElysiumEntityHandle& Owner, bool bEnabled) {}
	virtual void ClearTouchAnchors() {}
	virtual FElysiumUseQueryResult QueryPlayerUse(
		const FElysiumEntityHandle& CurrentFocus) const = 0;

	// B6 — `CBasePlayer::Replenish`'s direct victim search (`docs/vtmb/feeding.md` § "Target
	// acquisition and acceptance"): a hull trace from the view position toward the local offset
	// (32 forward, 0 right, -32 vertical) with extents (-8,-8,-8)..(8,8,8). It is a separate query
	// from `+use` because it is a separate retail search with its own shape and its own mask — the
	// feed reaches DOWN and forward for a body, where `+use` reaches along the aim for a control.
	//
	// Geometry only: the handle it returns is a candidate, and every eligibility question
	// (paired state, automatic acceptance, `ResistsFeeding`, the opposed check) stays in the
	// substrate. Invalid means nothing was in the hull, which is the ordinary answer. The cone /
	// radius survey that supplies the small-animal route (`rat_feed_arc`, `rat_feed_radius`) is
	// deliberately absent: rat feeding is out of B6's scope.
	virtual FElysiumEntityHandle QueryFeedTarget() const { return FElysiumEntityHandle::Invalid(); }

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

	// 11.7 — the legacy scripted-shot channel. `SetCamera(shotfile)`, `camera_keyframe`, and the feed
	// camera push onto the player camera's one weight stack through here, and
	// `RemoveCamera` pops. `ShotFile` keys `vdata/camerashots/`; `Subject` is the entity the shot is
	// about, which is what its `DialogTarget` anchors resolve to. Returns 0 when the shot does not
	// parse, nothing it anchors to is there, or there is no camera (a headless world runs the
	// consumer without one). The channel is here, on the player's *body*, rather than on
	// IElysiumPresenter: the camera is part of the body (S3), and the presenter carries what is put
	// on *screen*, not what the player's body does.
	virtual int32 PushCameraShot(const FString& ShotFile, const FElysiumEntityHandle& Subject) = 0;
	// Value shots are the camera_track path: the entity world owns the sampler and publishes the
	// resulting world-space value without teaching the camera component about entities.
	virtual int32 PushCameraShotValue(const FElysiumCameraShot& Shot) = 0;
	virtual bool UpdateCameraShotValue(int32 ShotId, const FElysiumCameraShot& Shot) = 0;
	virtual bool PopCameraShot(int32 ShotId, float BlendOutSeconds = -1.0f) = 0;
};

// --------------------------------------------------------------------------------------------
// Audio — the voice pool and the map's SoundScheme control.
//
// Implemented by AElysiumMapActor, which owns the per-map FElysiumSoundSchemeManager and forwards
// the voice calls to the GI-scoped UElysiumAudioSubsystem. The substrate never holds either.
// --------------------------------------------------------------------------------------------
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

	// P4.6 trigger_changelevel — queue a landmark transition. Offset is the player's displacement
	// from THIS map's landmark, re-added to the destination's same-named one; Yaw is carried across.
	virtual void RequestLandmarkTravel(const FString& Map, const FString& Landmark,
		const FVector& Offset, float Yaw) = 0;
	// A plain map change with no landmark (the player is placed at the destination's own spawn).
	virtual void ChangeMap(const FString& Map) = 0;
};

// --------------------------------------------------------------------------------------------
// Presenter — what the substrate puts on screen.
//
// Implemented by UElysiumPresentationSubsystem (11.8), the world-scoped publisher of
// FElysiumViewState. These are **announcements of discrete moments**, not the state itself: the
// fade, the open panel and the open conversation stay on FElysiumEntityWorld, because each is world
// state with a lifetime (the map epoch owns it, and 11.9 saves it). The publisher samples that state
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

	// P4.5 env_fade — a full-screen colour fade. Parameters are the Fade input's, verbatim.
	virtual void StartFade(const FLinearColor& Color, float Duration, float HoldTime, float MaxAlpha,
		bool bFadeIn, bool bAutoReverse) = 0;

	// P4.10 game_sign — the one sign panel on screen. CloseSign is the dismissal (left-click,
	// CloseWindow, or the owner dying).
	virtual void OpenSign(const FElysiumEntityHandle& Owner, const TSharedPtr<const FElysiumSignData>& Data,
		float FadeInSeconds) = 0;
	virtual void CloseSign() = 0;

	// 9.1/B4 — the one conversation on screen.
	virtual void OpenDialog(const FElysiumEntityHandle& Owner, FElysiumDlgConversation& Conversation) = 0;
	virtual void CloseDialog() = 0;

	// 8.9 — a FIFO HUD notification. Unlike the replaceable retained surfaces above, every admitted
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
	// VtMB's `attach_type`. The definition-side parser names the low values origin/tree/point/
	// treecolor, so 2 = `point`: ride `AttachBone` on `ParentName`'s body. Higher values are used
	// but unresolved, and are carried rather than interpreted.
	int32 AttachType = 0;
	FString ParentName;
	FString AttachBone;
	float BoundsCm = 0.0f;
	float RateScale = 0.0f;
	float RampStartScale = 0.0f;
	float RampTargetScale = 0.0f;
	double RampStartTime = 0.0;
	float RampDuration = 0.0f;
};

class IElysiumWeather
{
public:
	virtual ~IElysiumWeather() = default;
	virtual void ApplyWetness(const FElysiumWeatherTransition& Transition) = 0;
	virtual void ApplyEmitter(const FElysiumWeatherEmitterState& Emitter) = 0;
	virtual void RemoveEmitter(const FElysiumEntityHandle& Entity) = 0;
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
