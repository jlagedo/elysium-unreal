#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ElysiumEntity.h"   // FElysiumFlexWrite (passed by view)
#include "ElysiumEntityBodies.generated.h"

class UAnimSequence;
class UElysiumPropSkinSet;
class UglTFRuntimeAsset;
class USkeletalMesh;
class USkeletalMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;
class USceneComponent;

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
	bool SeekCinematicClip(USkeletalMeshComponent* Body, float PositionSeconds);
	void StopCinematicClip(USkeletalMeshComponent* Body);

	// 12.3 — write named flex controllers on a body's facial rig. INDEX_NONE when the body has no
	// Elysium animation host or no rig on it; otherwise the number of writes that landed, with the
	// names the rig does not carry appended to OutMissing.
	int32 SetFlexControllers(USkeletalMeshComponent* Body, TArrayView<const FElysiumFlexWrite> Writes,
		TArray<FString>* OutMissing);

	// 12.5 — the amplitude jaw. False on a body with no animation host, no rig, or a rig carrying no
	// `mstudiomouth_t` record.
	bool SetMouthOpen(USkeletalMeshComponent* Body, float Open);

	bool PlayNpcClip(USkeletalMeshComponent* Body, const FString& Stem, const FString& ClipName,
		bool bLoop, float* OutSeconds);
	bool PlayNpcActivity(USkeletalMeshComponent* Body, const FString& Stem,
		const FString& Activity, int32 Variant, bool bLoop, float* OutSeconds);

	// v4 skeletal props. The model-path lookup chooses the animated representation; building and
	// clip resolution stay separate so ordinary props never load glTF or animation data.
	FString AnimatedPropStemForModel(const FString& ModelPath) const;
	USkeletalMeshComponent* BuildAnimatedPropVisual(const FString& Stem, const FVector& Location,
		const FQuat& Rotation, float UniformScale);
	bool PlayAnimatedPropClip(USkeletalMeshComponent* Body, const FString& Stem,
		const FString& ClipName, bool bLoop, float* OutSeconds);
	void ApplyAnimatedPropSkin(USkeletalMeshComponent* Comp, const FString& Stem, int32 Family);

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

private:
	FString MapName;

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
