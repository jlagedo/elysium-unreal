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

	// Pick a resting animation for a static standing NPC (B3 has no AI): the first clip whose name
	// contains "idle" (case-insensitive), retargeted onto Mesh. Returns null when the glb carries no
	// idle-named clip — the caller then leaves the mesh in its reference pose (a clean stand), which
	// reads better than looping an arbitrary gesture/line clip. OutAppliedName is the chosen clip name.
	UAnimSequence* LoadIdleAnim(UglTFRuntimeAsset* Asset, USkeletalMesh* Mesh, FString& OutAppliedName);
}
