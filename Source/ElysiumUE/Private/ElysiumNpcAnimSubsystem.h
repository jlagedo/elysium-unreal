#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "ElysiumDisposition.h"
#include "ElysiumNpcClips.h"

#include "ElysiumNpcAnimSubsystem.generated.h"

class UAnimSequence;
class UglTFRuntimeAsset;
class USkeletalMesh;

// Which rule chose an NPC's standing idle. Reported by the console verbs and the Cog window so a
// wrong-looking pose is traceable to the rule rather than guessed at.
enum class EElysiumIdleTier : uint8
{
	None,        // nothing resolved — the caller leaves the mesh in its reference pose
	Stance,      // ACT_DISPOSITION, `Stance_<AnimName>_Idle_*` for the NPC's disposition
	ActIdle,     // ACT_IDLE, highest actweight (44 of 54 NPCs reach Stance first)
	Loose,       // no activity, but the label reads as an idle — monsters and one-off models
};

// GameInstance-scoped owner of everything about NPC animation that outlives a map (roadmap 8.5).
//
// Two things are cached here rather than on the map actor, because both are skeleton-independent
// and expensive: the parsed **bank assets** (a shared animation library is 2-35 MB of glb, and the
// same two stances banks serve essentially every map), and the **clip vocabularies** read off
// out/npc/clips/<stem>.json. What is NOT cached here is the resolved UAnimSequence: glTFRuntime
// binds each one to a specific USkeletalMesh's USkeleton, and meshes are per-map-epoch, so those
// belong to AElysiumMapActor and die with it.
//
// The clip -> owning-stem resolution is entirely offline (tools/npc_export.py walks the studiohdr
// include DAG); this subsystem never sees an include.
UCLASS()
class UElysiumNpcAnimSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;

	// out/npc/npc_index.json, loaded once. Empty when the NPC export has not been run.
	const FElysiumNpcIndex& GetIndex();
	// out/npc/clips/<Stem>.json, cached per stem. Null when the stem has no slice.
	const FElysiumNpcClipSet* GetClipSet(const FString& Stem);
	// vdata/system/dispositiontable.txt, loaded once.
	const FElysiumDispositionTable& GetDispositions();

	// A bank's parsed glb, cached for the session. Null + OutError when it cannot be loaded.
	UglTFRuntimeAsset* GetBankAsset(const FString& BankStem, FString& OutError);

	// Retarget one named clip onto Mesh, resolving which glb owns it through the stem's clip set.
	// OwnAsset is the NPC's own already-parsed glb (the owner for its dialogue clips); pass null
	// and a clip the NPC owns cannot resolve. Returns null and fills OutError on any failure.
	UAnimSequence* ResolveClip(const FString& Stem, const FString& ClipName, USkeletalMesh* Mesh,
		UglTFRuntimeAsset* OwnAsset, FString& OutError);

	// The standing idle for a stem at a disposition, by VtMB's own chain:
	//   default_disposition -> dispositiontable "Animation Name" -> Stance_<Name>_Idle_* (by weight)
	//   -> ACT_IDLE (by weight) -> a loose idle-named clip -> nothing.
	// Selection is by **activity**, never by label substring: `regular_cop` resolves 229 clips with
	// "idle" in the name, of which `Stance_Dead_Idle_1` and `Bed_Left_Idle` are not standing idles.
	// Variant picks the candidate at that index, wrapping — the seam ambient variety rides on.
	// VtMB authors three standing idles per disposition (`Stance_<D>_Idle_{1,2,3}`) and its
	// dispositiontable pacing for cycling them is written for conversation only ("while waiting
	// for the player to make a dialog choice"), so an NPC nobody is talking to just holds one.
	// Spreading the cast across the authored three is a choice among clips VtMB wrote for exactly
	// this disposition, not invented behaviour — it is what stops 42 cops standing identically.
	FString PickIdleClip(const FString& Stem, const FString& Disposition, EElysiumIdleTier& OutTier,
		int32 Variant = 0);

	// Every candidate the idle policy considered, best first — the debug/verification view.
	TArray<FString> IdleCandidates(const FString& Stem, const FString& Disposition,
		EElysiumIdleTier& OutTier);

	static const TCHAR* TierName(EElysiumIdleTier Tier);

	// Banks parsed so far and their total .glb bytes on disk — what the Cog window reports.
	void GetBankStats(int32& OutCount, int64& OutBytes) const;

private:
	// Session-lifetime: a bank is map-independent, and re-parsing 11 MB per travel is the cost
	// this cache exists to avoid.
	UPROPERTY() TMap<FString, TObjectPtr<UglTFRuntimeAsset>> BankAssets;

	FElysiumNpcIndex Index;
	bool bIndexLoaded = false;

	FElysiumDispositionTable Dispositions;
	bool bDispositionsLoaded = false;

	// Value is null for a stem whose slice is missing, so a failed read is remembered rather than
	// retried on every NPC that shares the stem.
	TMap<FString, TSharedPtr<FElysiumNpcClipSet>> ClipSets;
};
