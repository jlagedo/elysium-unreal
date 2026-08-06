#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ElysiumEntity.h"   // FElysiumFlexWrite (passed by view)
#include "Visual/ElysiumEyeRig.h"
#include "Visual/ElysiumTextureCache.h"
#include "ElysiumEntityBodies.generated.h"

class UAnimSequence;
class UElysiumPropSkinSet;
class UMaterialInstanceDynamic;
class UglTFRuntimeAsset;
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
	// is retargeted onto this skeleton by bone name — UElysiumNpcAnimSubsystem owns that resolution
	// and the session-lifetime bank cache. Null on a missing/failed glb or an empty stem.
	USkeletalMeshComponent* BuildNpcVisual(const FString& Stem, const FVector& Location,
		const FRotator& Rotation, float UniformScale, const FString& Disposition, int32 IdleVariant,
		bool bPlayerMaterial = false);

	// Re-run the default-idle policy on a live body and crossfade to the result. The seam a
	// disposition change reaches animation through: 9.9's `SetDisposition` is 2,510 calls, 2,467
	// of them a .dlg line's action, so an NPC's stance follows the conversation.
	bool RefreshNpcIdle(USkeletalMeshComponent* Body, const FString& Stem,
		const FString& Disposition, int32 IdleVariant);

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
	// Compose a `_delta` autolayer over whatever this body is already playing. Same resolution
	// chain as PlayNpcClip, so a layer owned by a shared bank is reached by label; the layer itself
	// is independent of the standing clip and survives a stance change. False when the label does
	// not resolve, when the body has no animation host, or when the resolved sequence is not a
	// baked additive (`UElysiumNpcAnimInstance::PlayLayer`).
	bool PlayNpcLayer(USkeletalMeshComponent* Body, const FString& Stem, const FString& ClipName,
		float Weight);
	void StopNpcLayers(USkeletalMeshComponent* Body);

	// Drop every cached NPC mesh, parsed glb and resolved clip so the next build re-resolves from
	// scratch.
	//
	// **This is what makes `elysium.BakedCharacters` switchable without a map reload.** The cvar is
	// read in exactly one place, `ElysiumNpcVisual::LoadMesh`, and the mesh cache is keyed by stem
	// and material permutation -- NOT by which path answered. So a stem resolved once is pinned to
	// that path for the rest of the map epoch: rebuilding the component hits the cache, LoadMesh is
	// never reached, and the toggle appears to do nothing. Only the A/B harness should call this;
	// gameplay has no reason to, and dropping the caches mid-map re-parses every bank glb.
	void ForgetNpcVisuals();
	// The transition a clip asks for when something fades INTO it: its authored `mstudioseqdesc_t`
	// fade, or 0 when it carries the no-transition bit. The host takes the larger of this and the
	// clip already playing, so this answers for one clip rather than for the pair.
	float ClipFadeSeconds(const FString& Stem, const FString& ClipName) const;
	bool PlayNpcActivity(USkeletalMeshComponent* Body, const FString& Stem,
		const FString& Activity, int32 Variant, bool bLoop, float* OutSeconds);
	bool ResolveNpcActivityClip(const FString& Stem, const FString& Activity, int32 Variant,
		FString& OutLabel, FString& OutAnimName, float& OutGroundSpeedCmPerSecond);

	// v4 skeletal props. The model-path lookup chooses the animated representation; building and
	// clip resolution stay separate so ordinary props never load glTF or animation data.
	FString AnimatedPropStemForModel(const FString& ModelPath) const;
	USkeletalMeshComponent* BuildAnimatedPropVisual(const FString& Stem, const FVector& Location,
		const FQuat& Rotation, float UniformScale);
	bool PlayAnimatedPropClip(USkeletalMeshComponent* Body, const FString& Stem,
		const FString& ClipName, bool bLoop, float* OutSeconds);
	int32 PreloadAnimatedPropClips(USkeletalMeshComponent* Body, const FString& Stem);
	int32 FinishAnimationPreload();
	void ApplyAnimatedPropSkin(USkeletalMeshComponent* Comp, const FString& Stem, int32 Family);
	// The model's resting clip, and whether a named clip loops. Both read the manifest only — no
	// glb, no mesh — so a prop can ask before deciding which representation to stand.
	FString AnimatedPropRestClip(const FString& Stem) const;
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

private:
	USkeletalMesh* ResolveNpcMesh(const FString& Stem, bool bPlayerMaterial);
	UAnimSequence* ResolveCinematicClip(USkeletalMesh* Mesh, const FString& Stem,
		const FString& BankStem, const FString& ClipName);
	UAnimSequence* ResolveAnimatedPropClip(USkeletalMesh* Mesh, const FString& Stem,
		const FString& ClipName);
	// The manifest record for a prop stem, or null. Shared by the two query members above.
	const struct FElysiumAnimatedPropEntry* FindAnimatedPropEntry(const FString& Stem) const;

	FString MapName;

	// One eye section on one body: which slot draws it, which record it draws, the head bone it
	// rides, and the material instance whose parameters carry the basis.
	//
	// The MID is per COMPONENT, never per mesh. NpcMeshCache shares one USkeletalMesh across every
	// NPC of a stem, and glTFRuntime's own material instances live on that shared mesh's slots —
	// writing an iris plane there would make every NPC of the model look wherever the last one
	// looked, which reads as a feature rather than a bug.
	struct FElysiumEyeSlot
	{
		TWeakObjectPtr<UMaterialInstanceDynamic> Mid;
		int32 EyeIndex = 0;
		int32 BoneIndex = INDEX_NONE;
		// Resolved once; the per-frame writes go by index and never look a name up again.
		int32 ParamIrisU = INDEX_NONE;
		int32 ParamIrisV = INDEX_NONE;
		int32 ParamIrisOrigin = INDEX_NONE;
		int32 ParamNormalOrigin = INDEX_NONE;
		int32 ParamEyeUp = INDEX_NONE;
	};
	struct FElysiumEyeBinding
	{
		TWeakObjectPtr<USkeletalMeshComponent> Comp;
		TSharedPtr<const FElysiumEyeSet> Set;
		TArray<FElysiumEyeSlot> Slots;
		// The body's disposition, resolved against the table for this character's blink cadence.
		// Latched at build: a disposition change rebuilds the body.
		FString Disposition;

		// Blink is two halves in retail: the server picks *when* (a random interval from the
		// disposition table) and the client runs the 300 ms envelope. Both sit here until 12.4's
		// gaze cascade lands, which owns the cadence and pushes the toggle through the seam.
		//
		// The envelope is asymmetric and that is authored: `w = 2*sqrt(cos(pi*u/2))` folded about
		// 1 closes the lid 48 ms after the toggle and reopens it over the remaining 252 ms.
		float NextBlinkTime = 0.f;
		float BlinkEndsAt = 0.f;

		// Where the substrate says this character is looking, world space, pushed once per frame
		// through IElysiumEmbodiment::SetViewTarget. Held rather than pulled because the two halves
		// tick in different passes: the gaze decision runs over the entity world, the eye pass runs
		// over the bodies, and this is the one value that crosses.
		FVector ViewTarget = FVector::ZeroVector;
		bool bHasViewTarget = false;

		// The head bone, resolved once at build. The gaze cone and the fidget grid are measured in
		// the live animated head frame, so this is looked up by name and cached — never mixed with
		// the `.mdl`'s own bone ordering, which is not the USkeleton's.
		int32 HeadBoneIndex = INDEX_NONE;
	};
	TArray<FElysiumEyeBinding> EyeBindings;
	// The per-character iris is the `.vmt`'s `$iris`, decoded beside the glb rather than carried
	// inside it, so it loads through the same per-map dedup index the world uses. Strong-ref'd for
	// the epoch, released with this component.
	FElysiumTextureCache EyeTextures;

	// Find the eye sections on a freshly built body, build their per-component material instances,
	// and register them for TickEyes. A body whose model authors no eyeball binds nothing.
	void InstallEyes(USkeletalMeshComponent* Comp, const TSharedPtr<const FElysiumEyeSet>& Set,
		const FString& Disposition);

	// B3/8.5 NPC skeletal bodies: per-stem mesh cache and a per-(stem, clip) animation cache,
	// GC-rooted here so a model shared by several NPCs loads once and survives until unload. The
	// USkeletalMeshComponents themselves are components of the owning actor (rooted via
	// AddInstanceComponent), freed with it.
	//
	// The animation cache is keyed `<stem>|<clip>` and lives HERE rather than on the GI-scoped
	// UElysiumNpcAnimSubsystem, because glTFRuntime binds every UAnimSequence it builds to one
	// USkeletalMesh's USkeleton — and meshes are per-map-epoch. The subsystem caches what is
	// skeleton-independent: the parsed bank glbs and the clip vocabularies. An entry may be null
	// (nothing resolved → reference pose); it is still cached, so a miss is not retried per NPC.
	UPROPERTY() TMap<FString, TObjectPtr<USkeletalMesh>> NpcMeshCache;
	UPROPERTY() TMap<FString, TObjectPtr<UAnimSequence>> NpcAnimCache;

	// The NPC's own parsed glb, kept for the epoch so a clip it owns itself (its dialogue anims)
	// can still be retargeted after the mesh is cached — the bank path does not go through it.
	UPROPERTY() TMap<FString, TObjectPtr<UglTFRuntimeAsset>> NpcAssetCache;
	FString NpcVisualKeyForMesh(const FString& Stem, const USkeletalMesh* Mesh) const;

	// v4 animated props are their own model/asset/clip namespace. Keeping separate maps prevents a
	// prop and NPC with the same basename from aliasing skeleton-bound UAnimSequences.
	UPROPERTY() TMap<FString, TObjectPtr<USkeletalMesh>> AnimatedPropMeshCache;
	UPROPERTY() TMap<FString, TObjectPtr<UglTFRuntimeAsset>> AnimatedPropAssetCache;
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
};

// Skeleton-bound animation cache keys. Kept outside the UObject so the invariant is testable
// without constructing a world: no key may alias sequences retargeted onto different models.
namespace ElysiumEntityAnimation
{
	FString NpcVisualCacheKey(const FString& Stem, bool bPlayerMaterial);
	FString NpcClipCacheKey(const FString& Stem, const FString& ClipName);
	FString CinematicClipCacheKey(const FString& Stem, const FString& BankStem, const FString& ClipName);
}
