#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ElysiumNpcSubsystem.generated.h"

class AActor;
class USkeletalMesh;
class UAnimSequence;
class UElysiumBipedAnimInstance;
class IConsoleObject;

// One test NPC standing off the baked mount. Records the body and the applied clip so the Cog NPC
// window can display them without re-reading the packages. The spawned actor is a plain AActor with
// a USkeletalMeshComponent root playing the clip on UElysiumBipedAnimInstance — the game's own host,
// so the preview body carries the facial flex track too.
USTRUCT()
struct FElysiumLoadedNpc
{
	GENERATED_BODY()

	UPROPERTY() TObjectPtr<AActor> Actor = nullptr;
	UPROPERTY() TObjectPtr<USkeletalMesh> Mesh = nullptr;
	UPROPERTY() TObjectPtr<UAnimSequence> Anim = nullptr;

	FString Stem;                 // model stem on the baked mount (e.g. "gangmember_male_2")
	FString AnimName;             // the clip that was applied ("" if none)
	int32   NumBones = 0;         // ref-skeleton bone count
	int32   NumAnims = 0;         // clips this body owns itself (its dialogue), not what it can play
	TArray<FString> AnimNames;    // their names (for the window's per-clip re-play buttons)
	FVector Location = FVector::ZeroVector;
	double  LoadMilliseconds = 0.0;
};

// GameInstance-scoped test harness for the character path. Stands one baked body in front of the
// player, playing a named clip resolved through the same vocabulary the game resolves against, so
// what the harness stands is what a map stands. Drives the elysium.npc.* verbs and the Cog
// "Elysium.NPC" window.
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
	static TArray<FString> AvailableBodyStems();

	// Every live NPC animation host in this world carrying a facial flex rig (12.3) — the map's own
	// characters as readily as this harness's preview bodies, since both stand on the same anim
	// instance. StemFilter, when non-empty, keeps only rigs whose model stem contains it. The
	// elysium.npc.flex verbs and the Cog NPC window's Facial tab read this one list, so they always
	// address the same bodies.
	TArray<UElysiumBipedAnimInstance*> FacialBodies(const FString& StemFilter = FString()) const;

private:
	// Feet-of-player + a short forward offset, facing the player (yaw only).
	FVector ComputeSpawnLocation(FRotator& OutRotation) const;
	// Drop records whose actor was torn down (e.g. by map travel) so the list stays truthful.
	void PruneDead();
	// The map-epoch boundary (S4): a preview body and the assets it resolved belong to the map it was
	// spawned into, so both go when that map does.
	void OnMapEpochRetired(uint64 Epoch);

	UPROPERTY() TArray<FElysiumLoadedNpc> Loaded;
	TArray<IConsoleObject*> ConsoleObjects;
	FDelegateHandle MapEpochRetiredHandle;
};
