#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ElysiumNpcSubsystem.generated.h"

class AActor;
class USkeletalMesh;
class UAnimSequence;
class UglTFRuntimeAsset;
class IConsoleObject;

// One test NPC loaded through the runtime skeletal path (P8 8.2 spike). Records what glTFRuntime
// produced from a single out/npc/<stem>.glb -- mesh + skeleton + one applied animation -- so the Cog
// NPC window can display it without re-reading the asset. The spawned actor is a plain AActor with a
// USkeletalMeshComponent root playing the clip on a single-node anim instance.
USTRUCT()
struct FElysiumLoadedNpc
{
	GENERATED_BODY()

	UPROPERTY() TObjectPtr<AActor> Actor = nullptr;
	UPROPERTY() TObjectPtr<USkeletalMesh> Mesh = nullptr;
	UPROPERTY() TObjectPtr<UAnimSequence> Anim = nullptr;
	// The model's own parsed glb, kept so a clip it owns itself (its dialogue anims) can be
	// re-applied later — the shared-bank path does not go through it.
	UPROPERTY() TObjectPtr<UglTFRuntimeAsset> Asset = nullptr;

	FString Stem;                 // glb stem under out/npc (e.g. "gangmember_male_2")
	FString AnimName;             // the clip that was applied ("" if none)
	int32   NumBones = 0;         // ref-skeleton bone count
	int32   NumAnims = 0;         // animations present in the glb
	TArray<FString> AnimNames;    // their names (for the window's per-clip re-play buttons)
	FVector Location = FVector::ZeroVector;
	double  LoadMilliseconds = 0.0;
};

// GameInstance-scoped test harness for the glTFRuntime skeletal path (roadmap 8.2). Loads a VtMB NPC
// exported to out/npc/<stem>.glb (mdl_gltf.py: mesh + StudioBone skeleton + one RLE animation, a
// standard glTF 2.0 file) into a runtime USkeletalMesh + UAnimSequence via glTFRuntime, and spawns it
// in front of the player. This is the proof-of-concept for the P8 NPC track (8.5 builds real NPC
// presence on the same mechanism). Drives the elysium.npc.* verbs and the Cog "Elysium.NPC" window.
// GI scope (like UElysiumAudioSubsystem) so its console commands register exactly once, not once per
// world.
UCLASS()
class UElysiumNpcSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Load out/npc/<Stem>.glb, apply AnimName (empty = first animation), spawn near the player.
	// Returns the spawned actor (null on failure; OutError explains why) and records the result.
	AActor* LoadTestNpc(const FString& Stem, const FString& AnimName, FString& OutError);

	// Re-apply a clip to an already-spawned test NPC, resolving it through the manifest so a shared
	// bank's clip plays as readily as one of the model's own. Returns false + OutError on a bad
	// index or an unresolvable name. Drives `elysium.npc.play` and the Cog window's clip browser.
	bool PlayClipOn(int32 Index, const FString& ClipName, FString& OutError);

	// Destroy every spawned test NPC and drop the records.
	void ClearNpcs();

	const TArray<FElysiumLoadedNpc>& GetLoaded() const { return Loaded; }

	// glb stems available under out/npc (scanned from disk; for the window's picker + npc.list).
	static TArray<FString> AvailableGlbStems();

private:
	// Feet-of-player + a short forward offset, facing the player (yaw only).
	FVector ComputeSpawnLocation(FRotator& OutRotation) const;
	// Drop records whose actor was torn down (e.g. by map travel) so the list stays truthful.
	void PruneDead();

	UPROPERTY() TArray<FElysiumLoadedNpc> Loaded;
	TArray<IConsoleObject*> ConsoleObjects;
};
