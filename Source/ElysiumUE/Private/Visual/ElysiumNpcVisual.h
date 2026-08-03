#pragma once

#include "CoreMinimal.h"

class UAnimSequence;
class UglTFRuntimeAsset;
class UMaterialInterface;
class USkeletalMesh;

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

	// The master an eye section is drawn with, or null when the policy content has not been
	// generated. Callers compare a built slot's base material against this to find the eye slots.
	UMaterialInterface* EyeMaster();

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
	FTransform ImportGlbLocal(const FTransform& GlbLocal);
	FVector ImportGlbDirection(const FVector& GlbDirection);

	// Bind one named clip from Asset onto Mesh's compatible skeleton by bone name. VtMB banks author
	// biped-local tracks directly; generic rest-pose retargeting corrupts those locals and Bip01's
	// absolute scene placement. Optional source tracks absent from the target are filtered before
	// construction and remain at bind pose. Returns null and fills OutError when the clip is absent.
	UAnimSequence* RetargetClip(UglTFRuntimeAsset* Asset, USkeletalMesh* Mesh, const FString& ClipName,
		FString& OutError);
}
