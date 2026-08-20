#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ElysiumEntity.h"   // FElysiumFlexWrite (passed by view)
#include "ElysiumWorldServices.h" // placed-model request/body value types
// By value: the grid a review body is standing on is a member, so the resolver's own header.
#include "Visual/ElysiumAnimGraph.h"
#include "Visual/ElysiumAnimSubsystem.h"
// By value: the eye pass is a plain member, and its debug/readout types stay reachable through
// this class's aliases below.
#include "Visual/ElysiumEyePass.h"
#include "UObject/ObjectKey.h"
#include "ElysiumEntityBodies.generated.h"

class UAnimSequence;
class UElysiumPropSkinSet;
class USkeletalMesh;
class USkeletalMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;
class USceneComponent;

// A skeletal prop is animated IN PLACE — nothing moves its component — so it draws where its bones
// go while it is culled on where its component sits. With no physics asset,
// `USkinnedMeshComponent::CalcMeshBound` falls through to the mesh's bind-pose bounds, and for a
// cinematic rig those can be twenty metres from the geometry they are supposed to contain: the
// courtroom sword's vertices span 2 m about its anchor while its scene clip draws it 9-22 m away.
namespace ElysiumPropBounds
{
	// The bounds extension that widens `Bind` to reach `RadiusCm` about the MODEL origin — which is
	// what a clip's reach is measured from, and is not the bind-pose centre. False when the bind
	// bounds already cover it, so a caller can leave the mesh alone.
	bool ExtensionFor(const FBoxSphereBounds& Bind, double RadiusCm,
		FVector& OutPositive, FVector& OutNegative);
}

// What a channel claim was answered with. Three values rather than a handle-or-zero, because "this
// body has no driver arbitrating anything" and "the driver refused this claim" are opposite
// instructions to the caller: the first plays (a green-room stand, a preview, a prop), the second
// must not (a body a higher-ranked claim already owns).
enum class EElysiumAnimClaim : uint8
{
	NoArbiter,
	Refused,
	Granted,
};

// The BODY FACTORY behind IElysiumEmbodiment's mesh half: every render component an entity stands
// in the world, plus the per-map asset caches behind them. AElysiumMapActor stays the interface's
// implementer — the substrate's engine side is the actor (`ElysiumWorldServices.h`) — and forwards
// the mesh calls here, keeping the player-body half (the eye, the teleport, the use trace, the
// camera channel) where the pawn is.
//
// Everything it builds is a component of the OWNING ACTOR, not of this component, so the existing
// ownership rule is unchanged: the world logically owns the embodiments, the actor physically owns
// them, and both die on map unload. The caches are UPROPERTY-rooted here, so a model shared by
// several entities loads once and is released with the map.
UCLASS()
class UElysiumEntityBodies : public UActorComponent
{
	GENERATED_BODY()

public:
	UElysiumEntityBodies();

	// The map whose baked assets these bodies draw. Set once at map load, before the spawn pass.
	void SetMap(const FString& InMapName) { MapName = InMapName; }

	// B3/8.5 — build one NPC skeletal body: load (cached per stem) out/npc/<Stem>.glb through
	// glTFRuntime and stand a movable USkeletalMeshComponent on the owning actor at the given
	// transform, playing the standing idle its disposition selects (reference pose when nothing
	// resolves). The idle usually lives in a **shared animation bank**, not the NPC's own glb, and
	// is retargeted onto this skeleton by bone name — UElysiumAnimSubsystem owns that resolution
	// and the session-lifetime bank cache. Null on a missing/failed glb or an empty stem.
	USkeletalMeshComponent* BuildNpcVisual(const FString& Stem, const FVector& Location,
		const FRotator& Rotation, float UniformScale, const FString& Disposition, int32 IdleVariant,
		bool bPlayerMaterial = false);

	// Re-run the default-idle policy on a live body and crossfade to the result. The seam a
	// disposition change reaches animation through: 9.9's `SetDisposition` is 2,510 calls, 2,467
	// of them a .dlg line's action, so an NPC's stance follows the conversation.
	bool RefreshNpcIdle(USkeletalMeshComponent* Body, const FString& Stem,
		const FString& Disposition, int32 DispositionLevel, int32 IdleVariant);
	void UpdateNpcDisposition(USkeletalMeshComponent* Body, const FString& Disposition,
		int32 DispositionLevel);

	// One model's disposition stance set for `AnimName`, with the precache fallbacks applied. The
	// substrate's stance machine reads the resolved table; this is the only place that touches the
	// clip vocabulary on its behalf.
	bool ResolveStanceClips(const FString& Stem, const FString& AnimName,
		struct FElysiumStanceClips& OutClips);

	// The disposition table row behind a `default_disposition` name — the stance token and the
	// pacing the selector rolls against, resolved together because they are one row.
	bool ResolveDisposition(const FString& Disposition, int32 DispositionLevel,
		struct FElysiumDisposition& OutRow);

	// Whether this body was drawn recently enough to count as visible. `TASK_WAIT_PVS`'s oracle.
	bool IsNpcBodyVisible(USkeletalMeshComponent* Body) const;

	// Crossfade a live NPC body to a named clip, resolved through the manifest. Returns false when
	// the name resolves nothing. OutSeconds receives the clip's authored length — what a
	// `scripted_sequence` schedules its `OnEndSequence` off.
	// 12.1 — play a clip out of a named cinematic bank (a choreo scene's anim set), resolved and
	// cached per (target stem, bank, clip). The target stem is load-bearing: glTFRuntime binds the
	// returned UAnimSequence to that model's USkeleton.
	bool PlayCinematicClip(USkeletalMeshComponent* Body, const FString& Stem, const FString& BankStem,
		const FString& ClipName, bool bLoop, float* OutSeconds);
	bool PreloadNpcClip(USkeletalMeshComponent* Body, const FString& Stem, const FString& ClipName);
	bool PreloadNpcClipForModel(const FString& Stem, bool bPlayerMaterial, const FString& ClipName);
	bool PreloadCinematicClip(USkeletalMeshComponent* Body, const FString& Stem,
		const FString& BankStem, const FString& ClipName);
	bool PreloadCinematicClipForModel(const FString& Stem, bool bPlayerMaterial,
		const FString& BankStem, const FString& ClipName);
	bool SeekCinematicClip(USkeletalMeshComponent* Body, float PositionSeconds);
	void StopCinematicClip(USkeletalMeshComponent* Body);
	// Give the scene's base-channel claim back without stopping the clip — the crossfade stop path
	// releases through this so the idle's own claim is not refused by a dead scene's.
	void ReleaseCinematicClaim(USkeletalMeshComponent* Body);
	bool GetCinematicClipPosition(USkeletalMeshComponent* Body, float& OutSeconds) const;
	bool ResyncCinematicClip(USkeletalMeshComponent* Body, float PositionSeconds);

	// 12.3 — write named flex controllers on a body's facial rig. INDEX_NONE when the body has no
	// Elysium animation host or no rig on it; otherwise the number of writes that landed, with the
	// names the rig does not carry appended to OutMissing.
	int32 SetFlexControllers(USkeletalMeshComponent* Body, TArrayView<const FElysiumFlexWrite> Writes,
		TArray<FString>* OutMissing);

	// 12.5 — the amplitude jaw. False on a body with no animation host, no rig, or a rig carrying no
	// `mstudiomouth_t` record.
	bool SetMouthOpen(USkeletalMeshComponent* Body, float Open);
	bool GetPhonemeFilter(USkeletalMeshComponent* Body, float& OutMin, float& OutMax) const;

	// 12.4 — the one value crossing from the gaze decision to the eye pass, and the head frame the
	// decision measures itself in.
	bool SetViewTarget(USkeletalMeshComponent* Body, const FVector& WorldTarget);
	bool GetHeadFrame(USkeletalMeshComponent* Body, FVector& OutPosition, FVector& OutForward) const;

	bool PlayNpcClip(USkeletalMeshComponent* Body, const FString& Stem, const FString& ClipName,
		bool bLoop, float* OutSeconds);
	// LIFE5 — play one already-resolved cell over whatever owns the base pose. The (owner, animation
	// name) pair goes straight at the baked clip, never through the vocabulary. The channel claim is
	// submitted BEFORE the clip: a refused claim plays nothing, which is how a reaction is kept off a
	// body a choreographed scene owns.
	bool PlayNpcOneShot(USkeletalMeshComponent* Body, const FElysiumOneShotClipRequest& Request,
		float* OutSeconds);
	// LIFE5 — where one channel of a body stands on its clip this frame, and the timeline that clip
	// declares. Both are passthroughs: the phase is the animation host's own (only it knows whether
	// a montage, a graph state or a fan is producing the pose), and the timeline is the owning
	// model's blend sidecar, cached whole by `UElysiumAnimSubsystem`.
	bool GetBodyClipPhase(USkeletalMeshComponent* Body, EElysiumAnimChannel Channel,
		FElysiumClipPhase& Out);
	const TArray<FElysiumAnimEvent>* GetNpcEventTimeline(const FString& OwnerStem,
		const FString& Label);
	// Compose an autolayer over whatever this body is already playing — a `_delta` additive, a masked
	// partial-body `_layer`, or a masked aim grid, decided from the asset. Same resolution chain as
	// PlayNpcClip, so a layer owned by a shared bank is reached by label; the layer itself is
	// independent of the standing clip and survives a stance change. False when the label does not
	// resolve, when the body has no compiled graph, or when the resolved sequence is neither kind.
	//
	// Composed by the graph's own layered blend (CCC10), armed as the lab's hand driver — an
	// **override** over the published record, so it survives a driven body republishing every frame
	// and the owner can judge a layer over a moving host. The overlay and the additive are separate
	// slots and do not displace each other.
	// `OutError`, when given, names WHICH of the refusals happened. They have five different fixes —
	// no graph, no vocabulary entry, nothing on the mount, an unmasked pose clip — and one bare
	// `false` for all of them is a silence a caller cannot act on.
	// `OutArmed` names the form that loaded (derived `<label>@<host>`, plain-label fallback, or
	// grid) so a console verb cannot report a ride over a miss. `StandingHint` is the sequence
	// the body is posing when no selection has been published yet — a lab one-shot — and loses
	// to `GetAppliedSelection().SequenceLabel` whenever that is set.
	bool PlayNpcLayer(USkeletalMeshComponent* Body, const FString& Stem, const FString& ClipName,
		float Weight, FString* OutError = nullptr, FString* OutArmed = nullptr,
		const FString* StandingHint = nullptr);
	// Steer the armed aim grid. The ordinary player producer pins both at zero, so without this a lab
	// could not tell a 3x3 aim grid from a still pose.
	void SetNpcLayerAim(USkeletalMeshComponent* Body, float Yaw, float Pitch);
	void StopNpcLayers(USkeletalMeshComponent* Body);

	// Stand this body on a label's whole blend grid rather than on the single cell the pose
	// parameters resolve to (ANM3). `OutGrid` comes back with the axes the caller steers through
	// `SetNpcGridPosition` and can label a control with. False when the label names no grid — which
	// is most labels — when the bake has not covered it, when the grid is a layer's, or when the
	// compiled target state has no blend-space player. `OutError` names which of those it was;
	// `OutArmed` names the form that loaded. `StandingHint` is the lab's standing clip when no
	// selection has been published.
	//
	// It is stood by **publishing a selection that names it**, over the graph's own blend-space
	// player, so what a review body stands on is the path the game plays through rather than a
	// second one that could drift from it.
	bool PlayNpcGrid(USkeletalMeshComponent* Body, const FString& Stem, const FString& ClipName,
		struct FElysiumResolvedGrid& OutGrid,
		EElysiumGraphState State = EElysiumGraphState::Walk,
		FString* OutError = nullptr, FString* OutArmed = nullptr,
		const FString* StandingHint = nullptr);
	void SetNpcGridPosition(USkeletalMeshComponent* Body, float Axis0, float Axis1);
	// Take the grid back off the body. The graph holds the pose it has, so the caller's next clip
	// owns the body outright rather than riding over a fan that is still playing underneath it.
	void StopNpcGrid(USkeletalMeshComponent* Body);

	// Drop every cached NPC mesh, parsed glb and resolved clip so the next build re-resolves from
	// scratch.
	//
	// **This is what lets a re-export be picked up without a map reload.** The mesh cache is keyed
	// by stem and material permutation, so a stem resolved once is pinned for the rest of the map
	// epoch: rebuilding the component hits the cache and `ElysiumNpcVisual::LoadMesh` is never
	// reached again. Only the green room should call this; gameplay has no reason to, and dropping
	// the caches mid-map re-resolves every body.
	void ForgetNpcVisuals();
	// The transition a clip asks for when something fades INTO it: its authored `mstudioseqdesc_t`
	// fade, or 0 when it carries the no-transition bit. The host takes the larger of this and the
	// clip already playing, so this answers for one clip rather than for the pair.
	float ClipFadeSeconds(const FString& Stem, const FString& ClipName) const;
	bool ResolveNpcActivityClip(const struct FElysiumActivityClipRequest& Request,
		struct FElysiumActivityClip& Out);
	bool ResolveNpcSequenceClip(const FString& Stem, const FString& ClipName,
		EElysiumAnimBodyKind BodyKind, FString& OutAnimName, float& OutGroundSpeedCmPerSecond);
	bool HasNpcClip(const FString& Stem, const FString& ClipName);
	FString NpcClipBlockedReaction(const FString& Stem, const FString& ClipLabel);

	// v4 skeletal props. The model-path lookup chooses the animated representation; building and
	// clip resolution stay separate so ordinary props never load glTF or animation data.
	FString AnimatedPropStemForModel(const FString& ModelPath) const;
	FElysiumPlacedModelBody BuildPlacedModelBody(const FElysiumPlacedModelRequest& Request);
	bool HasPlacedModelCatalogue() const;
	USkeletalMeshComponent* BuildAnimatedPropVisual(const FString& Stem, const FVector& Location,
		const FQuat& Rotation, float UniformScale, int32 PlacementToken = 0);
	bool PlayAnimatedPropClip(USkeletalMeshComponent* Body, const FString& Stem,
		const FString& ClipName, bool bLoop, float* OutSeconds);
	int32 PreloadAnimatedPropClips(USkeletalMeshComponent* Body, const FString& Stem);
	int32 FinishAnimationPreload();
	void ApplyAnimatedPropSkin(USkeletalMeshComponent* Comp, const FString& Stem, int32 Family);
	// The model's resting clip, and whether a named clip loops. Both read the manifest only — no
	// glb, no mesh — so a prop can ask before deciding which representation to stand.
	FString AnimatedPropRestClip(const FString& Stem, int32 PlacementToken = 0) const;
	bool FindAnimatedPropClip(const FString& Stem, const FString& ClipName, bool& bOutLoops) const;

	// Retarget one named clip onto an already-built NPC model's skeleton, cached per (stem, clip).
	// The clip may live in the NPC's own glb or in any shared bank — the manifest says which, and
	// the bank is loaded once per session. Null when the stem has no body yet or the name resolves
	// nothing.
	UAnimSequence* ResolveNpcClip(const FString& Stem, const FString& ClipName,
		USkeletalMesh* TargetMesh = nullptr);

	// The baked SM_<Stem> asset for a prop model, cached per stem (one load per model however many
	// entities place it). Null + a warning naming the bake command when the map has no such asset.
	// Shared by both prop build paths, so a model used by a dynamic and a physics prop loads once.
	UStaticMesh* ResolvePropMesh(const FString& Stem);
	UStaticMesh* ResolveBrushMesh(const FString& Stem);
	UStaticMeshComponent* BuildBrushVisual(const FString& Stem, USceneComponent* ParentBody,
		float UniformScale, bool bSky);

	// 8.3 — build one dynamic-prop body: stand a movable UStaticMeshComponent on the owning actor
	// at the given transform, drawing the baked prop mesh. Non-solid — a prop_dynamic is dressing,
	// and the mesh's own collision belongs to the physics props that share it. Null on an empty
	// stem / unbaked model. The Rotation is the exporter's pre-converted Unreal-space model_quat,
	// read verbatim.
	UStaticMeshComponent* BuildPropVisual(const FString& Stem, const FVector& Location,
		const FQuat& Rotation, float UniformScale);
	EElysiumItemGroundModelState ItemGroundModelState(const FString& ModelPath);

	// 8.4 — build one physics-prop body: the same baked mesh BuildPropVisual stands, which for a
	// physics model carries VtMB's own convex collision (one shape per `.phy` ledge, from the
	// props/<Stem>.phys sidecar) and its authored mass on the body setup, under
	// CTF_UseSimpleAndComplex so a Chaos body can simulate against the simple shapes while the debug
	// pick still gets a per-poly face index. The returned component carries the PhysicsActor profile
	// with collision enabled but is NOT yet simulating — the FElysiumPhysProp leaf drives
	// SetSimulatePhysics / mass / the elysium.PhysicsProps gate.
	UStaticMeshComponent* BuildPhysPropVisual(const FString& Stem, const FVector& Location,
		const FQuat& Rotation, float UniformScale);

	// Repaint a prop body to one of its model's alternate skin families (VtMB's `skin` keyfield /
	// `Skin` input — a material remap over the model's own slots, applied instantly). Family 0 and
	// any family the model does not carry restore the authored materials, which is what Source does
	// with an out-of-range skin. Safe on any prop body from either build path. `elysium.PropSkins 0`
	// disables the whole pass.
	void ApplyPropSkin(UStaticMeshComponent* Comp, const FString& Stem, int32 Family);

	// 12.4 — rebuild every bound eye's basis against this frame's final pose and publish it to the
	// material. Driven from AElysiumMapActor::PostMoveTick for the reason the camera director is:
	// it reads the frame's settled bone transforms, so the iris never lags the head by a frame.
	void TickEyes(float DeltaSeconds);

	// --- the eye pass's debug seam ---------------------------------------------------------------
	//
	// The debug override surface and the per-body readout are the eye pass's own types
	// (`Visual/ElysiumEyePass.h`); the aliases keep this component the name external callers — the
	// green room, the map actor — reach them through.
	using FElysiumEyeDebug = ::FElysiumEyeDebug;
	using FElysiumEyeReadout = ::FElysiumEyeReadout;
	FElysiumEyeDebug& EyeDebug() { return EyePass.EyeDebug(); }

	// Fill `Out` for `Comp`, or return false when this factory has no eye binding for it.
	bool DescribeEyes(const USkeletalMeshComponent* Comp, FElysiumEyeReadout& Out) const;

private:
	void LoadItemGroundModelCatalogue();
	USkeletalMesh* ResolveNpcMesh(const FString& Stem, bool bPlayerMaterial);
	UAnimSequence* ResolveCinematicClip(USkeletalMesh* Mesh, const FString& Stem,
		const FString& BankStem, const FString& ClipName);
	UAnimSequence* ResolveAnimatedPropClip(USkeletalMesh* Mesh, const FString& Stem,
		const FString& ClipName);
	USkeletalMeshComponent* BuildAnimatedPropVisualWithStaticStem(const FString& Stem,
		const FString& StaticStem, const FVector& Location, const FQuat& Rotation,
		float UniformScale, int32 PlacementToken);
	// Bind a skeletal prop body's slots to the surfaces baked onto its static twin SM_<StaticStem>,
	// matched by slot name. The placed-model bake carries neutral materials on the skeletal asset so
	// it does not import the prop texture corpus a second time, so this is where a skeletal prop's
	// look comes from — at build and again whenever the override array is cleared. False when the
	// static twin does not resolve; the caller owns what an absent twin means.
	bool BindMapMaterials(USkeletalMeshComponent* Comp, const FString& StaticStem);
	// The manifest record for a prop stem, or null. Shared by the two query members above.
	const struct FElysiumAnimatedPropEntry* FindAnimatedPropEntry(const FString& Stem) const;

	// LIFE4 — route a channel claim to the driver of the body it is armed on: an NPC motor's driver
	// through the visual's attach parent, the player's through the owning map actor. A body with no
	// driver — a green-room stand, a preview, a prop — answers `NoArbiter`, which is a body nothing
	// arbitrates against rather than a failure. The release mirror answers false for the same bodies.
	// `OutHandle` is non-zero only on `Granted`.
	EElysiumAnimClaim SubmitBodyAnimRequest(USkeletalMeshComponent* Body,
		const struct FElysiumAnimationRequest& Request, uint32& OutHandle);
	// LIFE5 — the baked clip one already-resolved (owner, animation name) pair names, cached per
	// (mesh, owner, animation) exactly as the cinematic path is. Never consults the vocabulary.
	UAnimSequence* ResolveOneShotClip(USkeletalMesh* Mesh, const FString& OwnerStem,
		const FString& AnimationName);
	// One-shot requests already reported — a missing bank or asset, and a request a producer built
	// unplayable — so a reaction re-armed every time a body is hit warns once rather than per hit.
	TSet<FString> ReportedMissingOneShots;
	bool ReleaseBodyAnimRequest(USkeletalMeshComponent* Body, uint32 Handle);
	// The standing cinematic claims, keyed by body, so `StopCinematicClip` releases the claim its
	// own `PlayCinematicClip` submitted. A scene-pinned clip has no natural end, so its claim holds
	// until this map gives it back.
	TMap<FObjectKey, uint32> CinematicClaims;

	// Publish the selection that stands `Grid` at a point on its axes. Shared by the two grid
	// members so the record a review body poses from is built in exactly one place.
	void StandGridSelection(class UElysiumBipedAnimInstance& Inst,
		const struct FElysiumResolvedGrid& Grid, float Axis0, float Axis1,
		EElysiumGraphState State);
	// The grid a body was last stood on, so steering it needs only the new axis values. One, because
	// standing a grid is a review path and exactly one body is under review at a time.
	struct FElysiumResolvedGrid StandingGrid;
	EElysiumGraphState StandingGridState = EElysiumGraphState::Walk;
	// Advanced only when the grid changes, never when it is steered: the graph asks for a blend on a
	// generation change, and a slider drag must move the sample point rather than transition.
	uint32 StandingGridGeneration = 0;

	FString MapName;
	bool bItemGroundModelsLoaded = false;
	TMap<FString, EElysiumItemGroundModelState> ItemGroundModels;
	TSet<FString> ReportedMissingItemGroundModels;

	// 12.4 — the whole eye system as one unit: the per-body bindings, the blink cadence, the gaze
	// debug seam and the strong-ref'd iris textures, released with this component. The public eye
	// methods above are thin forwarders into it.
	FElysiumEyePass EyePass;

	// The GI-scoped animation subsystem, or null when the owner is not in a world yet. Every call
	// site keeps its own null branch — this only owns the three-hop lookup.
	UElysiumAnimSubsystem* GetAnims() const;

	// B3/8.5 NPC skeletal bodies: per-stem mesh cache and a per-(stem, clip) animation cache,
	// GC-rooted here so a model shared by several NPCs loads once and survives until unload. The
	// USkeletalMeshComponents themselves are components of the owning actor (rooted via
	// AddInstanceComponent), freed with it.
	//
	// The animation cache is keyed `<stem>|<clip>` and lives HERE rather than on the GI-scoped
	// UElysiumAnimSubsystem, because a baked UAnimSequence is bound to one rig family's USkeleton
	// and the meshes that carry it are per-map-epoch. The subsystem caches what is
	// skeleton-independent: the clip vocabularies. An entry may be null (nothing resolved →
	// reference pose); it is still cached, so a miss is not retried per NPC.
	UPROPERTY() TMap<FString, TObjectPtr<USkeletalMesh>> NpcMeshCache;
	UPROPERTY() TMap<FString, TObjectPtr<UAnimSequence>> NpcAnimCache;

	FString NpcVisualKeyForMesh(const FString& Stem, const USkeletalMesh* Mesh) const;

	// v4 animated props are their own model/asset/clip namespace. Keeping separate maps prevents a
	// prop and NPC with the same basename from aliasing skeleton-bound UAnimSequences.
	UPROPERTY() TMap<FString, TObjectPtr<USkeletalMesh>> AnimatedPropMeshCache;
	UPROPERTY() TMap<FString, TObjectPtr<UAnimSequence>> AnimatedPropAnimCache;

	// 8.3 dynamic-prop static meshes: per-stem cache, GC-rooted here so a model placed by several
	// prop entities builds once and survives until unload.
	UPROPERTY() TMap<FString, TObjectPtr<UStaticMesh>> PropMeshCache;
	UPROPERTY() TMap<FString, TObjectPtr<UStaticMesh>> BrushMeshCache;
	// The map's baked prop skin table, loaded once on first use. bPropSkinsLoaded separates
	// "not looked for yet" from "this map has none" (most maps have none, and a miss must not
	// re-hit LoadObject per prop).
	UPROPERTY() TObjectPtr<UElysiumPropSkinSet> PropSkins;
	bool bPropSkinsLoaded = false;
	// Stems whose slot-name bind has already been reported incomplete. A skin change rebinds the
	// base materials every time it lands, so without this a doorknob toggling its lock state would
	// restate one bake defect on every toggle.
	TSet<FString> ReportedUnboundMaterialStems;
};

// Skeleton-bound animation cache keys. Kept outside the UObject so the invariant is testable
// without constructing a world: no key may alias sequences retargeted onto different models.
namespace ElysiumEntityAnimation
{
	FString NpcVisualCacheKey(const FString& Stem, bool bPlayerMaterial);
	FString NpcClipCacheKey(const FString& Stem, const FString& ClipName);
	FString CinematicClipCacheKey(const FString& Stem, const FString& BankStem, const FString& ClipName);

	// How long the pose a fan strikes at `AxisValue` actually lasts (LIFE5) — **the engine's own
	// answer**, over the samples the blend input selects and weighted the way it weights them.
	//
	// It is not the longest cell, the base cell's length, or an average: a nine-cell hit fan's
	// reactions differ by frames, the graph plays a blend of the two the angle sits between, and a
	// caller timing a reaction off anything else ends it early or late by exactly that difference.
	// Zero when the space is null, carries no samples, or was never resampled — all of which are
	// "cannot say", and a caller must not read one as an instant clip.
	float BlendedGridLengthSeconds(class UBlendSpace* Space, float AxisValue);
}
