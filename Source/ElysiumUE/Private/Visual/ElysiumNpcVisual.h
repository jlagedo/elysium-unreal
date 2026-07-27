#pragma once

#include "CoreMinimal.h"

class UAnimSequence;
class UglTFRuntimeAsset;
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
	USkeletalMesh* LoadMesh(const FString& Stem, UglTFRuntimeAsset*& OutAsset, FString& OutError);

	// Parse any .glb by absolute path — the shared animation banks (out/npc/banks/<stem>.glb), which
	// carry a skeleton and clips but no mesh. Same config as LoadMesh, so a bank reorients into
	// Unreal space identically to the NPC it will be applied to. Caching is the caller's job; this
	// is the expensive step (a bank is 2-35 MB).
	UglTFRuntimeAsset* LoadAssetFromPath(const FString& FullPath, FString& OutError);

	// Retarget one named clip from Asset onto Mesh's skeleton, **by bone name**. Asset may be the
	// NPC's own glb or any bank: glTFRuntime keys its tracks by bone name and resolves each against
	// the target ref skeleton, skipping a name the skeleton lacks and leaving that bone at its bind
	// pose (glTFRuntimeParserSkeletalMeshes.cpp, LoadSkeletalAnimationFromTracksAndMorphTargets).
	// Every bank bone name is present in every VtMB NPC skeleton, so no proportion retarget is
	// needed — this is VtMB's own virtualmodel bank-sharing (`docs/animation_and_movers.md` A.7).
	// Returns null and fills OutError when the asset has no clip by that name.
	UAnimSequence* RetargetClip(UglTFRuntimeAsset* Asset, USkeletalMesh* Mesh, const FString& ClipName,
		FString& OutError);
}
