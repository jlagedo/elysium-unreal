#pragma once

#include "CoreMinimal.h"

class UAnimSequence;
class UglTFRuntimeAsset;
class UMaterialInterface;
class USkeletalMesh;
class USkeleton;

// What a mesh build needs beyond the file itself. The first two are the runtime's own material
// steering; the last two are the bake seam (UElysiumCharacterBakeLibrary), which needs the built
// objects to land in a package rather than the transient one and to share a single skeleton across
// the cast. Both are null at runtime, which is what glTFRuntime's own defaults mean.
struct FElysiumGlbMeshOptions
{
	// Draw every section with M_PlayerBody rather than the plugin's uber materials.
	bool bPlayerMaterial = false;
	// glTF material names whose sections are drawn with M_Eyes ("eyeball_l" / "eyeball_r").
	const TArray<FString>* EyeMaterials = nullptr;
	// The package the mesh and its dependents are created in. Null means the transient package.
	UObject* Outer = nullptr;
	// The skeleton every baked body shares. When set, the built mesh keeps its own reference
	// skeleton — its own bone subset and proportions — and merges its bones into this one's tree,
	// so one bank clip is one UAnimSequence for the whole cast instead of one per body.
	USkeleton* Skeleton = nullptr;
};

// Shared runtime glTF NPC loader (P8 8.2 / B3). One code path both the interactive test harness
// (UElysiumNpcSubsystem) and the game NPC bodies (FElysiumNpc, via AElysiumMapActor::BuildNpcVisual)
// use to turn an out/npc/<stem>.glb into a runtime USkeletalMesh through glTFRuntime. glTF is
// self-describing (Y-up, metres, right-handed); the loader's default config reorients it to Unreal
// space, so there is no UE_-style pre-conversion (repo-root CLAUDE.md: mdl_gltf.py is the standing
// glTF exemption). Confines the glTFRuntime include surface to this TU + the two callers.
namespace ElysiumNpcVisual
{
	// Load out/npc/<Stem>.glb and its mesh 0 / skin 0 into a runtime USkeletalMesh. Returns null and
	// fills OutError on any failure (missing file, parse error, no mesh). On success OutAsset carries
	// the parsed glTFRuntime asset so the caller can pull animations off it (LoadIdleAnim, or the
	// harness's own per-clip auditioning). Needs no UWorld — glTFLoadAssetFromFilename is world-free.
	USkeletalMesh* LoadMesh(const FString& Stem, UglTFRuntimeAsset*& OutAsset, FString& OutError,
		bool bPlayerMaterial = false, const TArray<FString>* EyeMaterials = nullptr);
	// The same strict skeletal loader for a manifest-provided absolute path (v4 animated props).
	USkeletalMesh* LoadMeshFromPath(const FString& FullPath, UglTFRuntimeAsset*& OutAsset,
		FString& OutError, bool bPlayerMaterial = false,
		const TArray<FString>* EyeMaterials = nullptr);
	// The same loader with the bake's two extra knobs. Every other caller wants the overload above;
	// this exists so the offline bake builds a character through the identical configuration the
	// runtime does — the morph-target merge strategy, the strict bone binding, the material
	// overrides — rather than through a second implementation that can drift from it.
	USkeletalMesh* LoadMeshFromPath(const FString& FullPath, UglTFRuntimeAsset*& OutAsset,
		FString& OutError, const FElysiumGlbMeshOptions& Options);

	// Declare every morph target a mesh carries as a morph-target *curve* on its skeleton. An anim
	// curve only reaches USkeletalMeshComponent::ActiveMorphTargets when the bone container flags
	// it, and the bone container takes those flags from this metadata — so without this the facial
	// track evaluates correctly and moves nothing. The mesh loader calls it; the bake calls it again
	// on the shared skeleton so the metadata is serialised rather than rebuilt per load.
	void RegisterMorphTargetCurves(USkeletalMesh* Mesh);

	// The master an eye section is drawn with, or null when the policy content has not been
	// generated. Callers compare a built slot's base material against this to find the eye slots.
	UMaterialInterface* EyeMaster();

	// Whether `elysium.BakedCharacters` selects the offline-baked cast (ANM1). Necessary but not
	// sufficient: a stem the bake has not covered still loads through glTFRuntime, so every reader
	// asks for the baked asset and accepts null rather than treating the toggle as a guarantee.
	bool UseBakedCharacters();
	// Whether this stem's body comes off the baked mount rather than the loader. Same predicate
	// the mesh choice makes, exposed because the sidecar rigs are carried into whichever frame
	// the body landed in.
	bool IsStemBaked(const FString& Stem);
	// Whether a mesh that ALREADY LOADED came off the baked mount, asked of the mesh rather than
	// re-derived from the toggle. The two can disagree — `LoadMesh` selects the baked branch on
	// `UseBakedCharacters() && !UseClothMesh` and then falls through when the package is absent,
	// while `IsStemBaked` additionally asks the file system — so anything that must agree with the
	// body actually standing there asks this. A glTFRuntime mesh answers false.
	bool IsBakedMesh(const USkeletalMesh* Mesh);
	// Whether a sequence that ALREADY RESOLVED came off the baked mount. Necessary separately from
	// IsBakedMesh because the two are chosen independently: `ResolveClip` falls back to a
	// glTFRuntime-built sequence whenever the bake has not covered a bank or a clip name, so a
	// baked BODY routinely plays a non-baked CLIP. Only the baked ones carry the split correction.
	bool IsBakedClip(const UAnimSequence* Sequence);
	// One baked body / one baked clip off the /ElysiumBaked mount, or null when the bake has not
	// covered it. `Owner` is the stem that owns the clip — the body for its own dialogue clips, the
	// bank stem otherwise — and `ClipName` is the resolved animation name, after any blend-grid
	// cell selection, never the label. A clip is addressed through the MESH because a sequence is
	// bound to one rig family's skeleton and the mesh is what knows which family it belongs to.
	USkeletalMesh* LoadBakedMesh(const FString& Stem, bool bPlayerMaterial = false);
	UAnimSequence* LoadBakedClip(const USkeletalMesh* Mesh, const FString& Owner,
		const FString& ClipName);
	// One baked blend grid (ANM3), addressed the same way and for the same reason. Unlike a clip
	// this takes the LABEL — a grid is the thing a label names when it does not name one animation,
	// so there is no cell selection to resolve first. Null for every label that is one clip, which
	// is most of them, and for every body the bake has not covered.
	class UBlendSpace* LoadBakedBlendSpace(const USkeletalMesh* Mesh, const FString& Owner,
		const FString& Label);

	// Parse any .glb by absolute path — the shared animation banks (out/npc/banks/<stem>.glb), which
	// carry a skeleton and clips but no mesh. Same config as LoadMesh, so a bank reorients into
	// Unreal space identically to the NPC it will be applied to. Caching is the caller's job; this
	// is the expensive step (a bank is 2-35 MB).
	UglTFRuntimeAsset* LoadAssetFromPath(const FString& FullPath, FString& OutError);

	// The import transform every .glb loaded through this namespace goes through: glTFRuntime
	// conjugates each node transform by its configured scene basis and scales translation by its
	// scene scale (metres to centimetres). A sidecar written in the glb's own space — the procedural
	// rule table (CAP7.1) is one — reaches the skeleton built from that same file by passing through
	// these, which is why the constants are read back off the loader's own config rather than
	// restated. Nothing here is a Source-to-Unreal conversion; that half already happened offline.
	//
	// `bBaked` selects which frame the body being fitted actually landed in. glTFRuntime imports
	// under its own basis; the baked `.eskm` assets are in the repo's canonical Source-to-Unreal
	// frame. The two are a 90 degree yaw apart, so a sidecar carried into the wrong one aims an eye
	// or a driven bone sideways while everything else looks correct.
	FTransform ImportGlbLocal(const FTransform& GlbLocal, bool bBaked = false);
	FVector ImportGlbDirection(const FVector& GlbDirection, bool bBaked = false);
	// The loader's own metres->centimetres factor. A sidecar carrying a plain *length* — a radius,
	// a body extent — has no basis to change and only needs this, and taking it from the same
	// configuration the mesh is imported under is what keeps the two from drifting apart.
	float ImportGlbScale();

	// Whether the simulated-garment spike is engaged for this stem: `elysium.Cloth` is on AND both
	// of its artifacts exist on disk. One predicate, called by the mesh loader and by the body
	// factory that installs the rig, because the two must never disagree — the enhanced mesh with
	// no rig is a lattice that never moves, and the faithful mesh with a rig is a set of chains
	// naming bones the skeleton does not have.
	bool UseClothMesh(const FString& Stem);

	// Bind one named clip from Asset onto Mesh's compatible skeleton by bone name. VtMB banks author
	// biped-local tracks directly; generic rest-pose retargeting corrupts those locals and Bip01's
	// absolute scene placement. Optional source tracks absent from the target are filtered before
	// construction and remain at bind pose. Returns null and fills OutError when the clip is absent.
	UAnimSequence* RetargetClip(UglTFRuntimeAsset* Asset, USkeletalMesh* Mesh, const FString& ClipName,
		FString& OutError);
}
