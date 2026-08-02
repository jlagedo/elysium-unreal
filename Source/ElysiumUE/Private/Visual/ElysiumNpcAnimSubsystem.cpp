#include "Visual/ElysiumNpcAnimSubsystem.h"

#include "ElysiumContentPaths.h"
#include "Visual/ElysiumNpcVisual.h"

#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/FileManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumNpcAnim, Log, All);

void UElysiumNpcAnimSubsystem::Deinitialize()
{
	BankAssets.Reset();
	ClipSets.Reset();
	FacialRigs.Reset();
	CompositionRigs.Reset();
	Super::Deinitialize();
}

const FElysiumNpcIndex& UElysiumNpcAnimSubsystem::GetIndex()
{
	if (!bIndexLoaded)
	{
		bIndexLoaded = true;
		FString Error;
		if (!Index.Load(Error))
		{
			UE_LOG(LogElysiumNpcAnim, Warning, TEXT("npc index: %s"), *Error);
		}
		else
		{
			UE_LOG(LogElysiumNpcAnim, Log, TEXT("npc index: %d NPCs, %d animation banks"),
				Index.Npcs.Num(), Index.Banks.Num());
		}
	}
	return Index;
}

const FElysiumDispositionTable& UElysiumNpcAnimSubsystem::GetDispositions()
{
	if (!bDispositionsLoaded)
	{
		bDispositionsLoaded = true;
		FString Error;
		if (!Dispositions.Load(Error))
		{
			UE_LOG(LogElysiumNpcAnim, Warning, TEXT("disposition table: %s"), *Error);
		}
	}
	return Dispositions;
}

const FElysiumNpcClipSet* UElysiumNpcAnimSubsystem::GetClipSet(const FString& Stem)
{
	if (Stem.IsEmpty())
	{
		return nullptr;
	}
	if (const TSharedPtr<FElysiumNpcClipSet>* Cached = ClipSets.Find(Stem))
	{
		return Cached->Get();
	}

	TSharedPtr<FElysiumNpcClipSet> Set = MakeShared<FElysiumNpcClipSet>();
	FString Error;
	if (!Set->Load(Stem, Error))
	{
		UE_LOG(LogElysiumNpcAnim, Warning, TEXT("npc clips '%s': %s"), *Stem, *Error);
		Set.Reset();   // remembered as a miss, so this is not retried per NPC sharing the stem
	}
	ClipSets.Add(Stem, Set);
	return Set.Get();
}

TSharedPtr<const FElysiumFacialRig> UElysiumNpcAnimSubsystem::GetFacialRig(const FString& Stem)
{
	if (Stem.IsEmpty())
	{
		return nullptr;
	}
	if (const TSharedPtr<const FElysiumFacialRig>* Cached = FacialRigs.Find(Stem))
	{
		return *Cached;
	}

	// The index names the sidecar, so a model with no flex rig is answered without touching the
	// disk — and answered null, which is a normal load, not a failure.
	const FElysiumNpcIndexEntry* Entry = GetIndex().Npcs.Find(Stem);
	TSharedPtr<const FElysiumFacialRig> Result;
	if (Entry != nullptr && !Entry->Facial.IsEmpty())
	{
		TSharedPtr<FElysiumFacialRig> Rig = MakeShared<FElysiumFacialRig>();
		FString Error;
		if (!Rig->Load(Entry->Facial, Error))
		{
			UE_LOG(LogElysiumNpcAnim, Warning, TEXT("facial '%s': %s"), *Stem, *Error);
		}
		else if (!Rig->IsValid())
		{
			// A rig with nothing to weight is answered the same way as no rig at all, so no body
			// carries a facial track that cannot move anything.
			UE_LOG(LogElysiumNpcAnim, Verbose,
				TEXT("facial '%s': %d controllers, %d rules, no morph targets — no face to drive"),
				*Stem, Rig->Controllers.Num(), Rig->Rules.Num());
		}
		else
		{
			UE_LOG(LogElysiumNpcAnim, Verbose,
				TEXT("facial '%s': %d controllers, %d rules, %d morphs, %d lid(s)"), *Stem,
				Rig->Controllers.Num(), Rig->Rules.Num(), Rig->Morphs.Num(), Rig->Lids.Num());
			Result = Rig;
		}
	}
	FacialRigs.Add(Stem, Result);
	return Result;
}

namespace
{
	// Build one composition rig out of the two things that declare it: the index's own split-bone
	// inventory and, when the model declares any driven bone, the rule table beside its glb. Either
	// half may be empty; a model with neither is answered null, and that is a normal load.
	TSharedPtr<const FElysiumCompositionRig> BuildCompositionRig(const FString& Stem,
		const TArray<FString>& SplitBones, const FString& ProceduralRelPath, FString& OutError)
	{
		TSharedPtr<FElysiumCompositionRig> Rig = MakeShared<FElysiumCompositionRig>();
		Rig->Stem = Stem;
		Rig->SplitBones.Reserve(SplitBones.Num());
		for (const FString& BoneName : SplitBones)
		{
			Rig->SplitBones.Add(FName(*BoneName));
		}
		if (!ProceduralRelPath.IsEmpty() && !Rig->LoadAxisRules(ProceduralRelPath, OutError))
		{
			// A named-but-unreadable table is a fault, not a model without one: the split half is
			// still installed so the body keeps whatever composition it can have.
			Rig->AxisRules.Reset();
		}
		return Rig->HasWork() ? TSharedPtr<const FElysiumCompositionRig>(Rig) : nullptr;
	}
}

TSharedPtr<const FElysiumCompositionRig> UElysiumNpcAnimSubsystem::GetCompositionRig(const FString& Stem)
{
	if (Stem.IsEmpty())
	{
		return nullptr;
	}
	if (const TSharedPtr<const FElysiumCompositionRig>* Cached = CompositionRigs.Find(Stem))
	{
		return *Cached;
	}

	const FElysiumNpcIndexEntry* Entry = GetIndex().Npcs.Find(Stem);
	TSharedPtr<const FElysiumCompositionRig> Result;
	if (Entry != nullptr)
	{
		FString Error;
		Result = BuildCompositionRig(Stem, Entry->SplitRotationBones, Entry->Procedural, Error);
		if (!Error.IsEmpty())
		{
			UE_LOG(LogElysiumNpcAnim, Warning, TEXT("procedural '%s': %s"), *Stem, *Error);
		}
		else if (Result.IsValid())
		{
			UE_LOG(LogElysiumNpcAnim, Verbose, TEXT("composition '%s': %d split bone(s), %d rule(s)"),
				*Stem, Result->SplitBones.Num(), Result->AxisRules.Num());
		}
	}
	CompositionRigs.Add(Stem, Result);
	return Result;
}

TSharedPtr<const FElysiumCompositionRig> UElysiumNpcAnimSubsystem::GetAnimatedPropCompositionRig(
	const FString& ModelPath)
{
	if (ModelPath.IsEmpty())
	{
		return nullptr;
	}
	const FElysiumAnimatedPropEntry* Entry = GetIndex().FindAnimatedProp(ModelPath);
	if (Entry == nullptr)
	{
		return nullptr;
	}
	if (const TSharedPtr<const FElysiumCompositionRig>* Cached = CompositionRigs.Find(Entry->Stem))
	{
		return *Cached;
	}

	FString Error;
	TSharedPtr<const FElysiumCompositionRig> Result =
		BuildCompositionRig(Entry->Stem, Entry->SplitRotationBones, Entry->Procedural, Error);
	if (!Error.IsEmpty())
	{
		UE_LOG(LogElysiumNpcAnim, Warning, TEXT("procedural prop '%s': %s"), *Entry->Stem, *Error);
	}
	CompositionRigs.Add(Entry->Stem, Result);
	return Result;
}

UglTFRuntimeAsset* UElysiumNpcAnimSubsystem::GetBankAsset(const FString& BankStem, FString& OutError)
{
	OutError.Reset();
	if (const TObjectPtr<UglTFRuntimeAsset>* Cached = BankAssets.Find(BankStem))
	{
		if (*Cached != nullptr)
		{
			return Cached->Get();
		}
	}

	const FString Path = GetIndex().BankGlbPath(BankStem);
	if (Path.IsEmpty())
	{
		OutError = FString::Printf(TEXT("'%s' is not a known animation bank"), *BankStem);
		return nullptr;
	}

	const double Start = FPlatformTime::Seconds();
	UglTFRuntimeAsset* Asset = ElysiumNpcVisual::LoadAssetFromPath(Path, OutError);
	if (Asset == nullptr)
	{
		return nullptr;
	}
	BankAssets.Add(BankStem, Asset);
	UE_LOG(LogElysiumNpcAnim, Log, TEXT("npc bank '%s' parsed in %.0f ms (%.1f MB)"), *BankStem,
		(FPlatformTime::Seconds() - Start) * 1000.0,
		static_cast<double>(IFileManager::Get().FileSize(*Path)) / 1e6);
	return Asset;
}

UAnimSequence* UElysiumNpcAnimSubsystem::ResolveClip(const FString& Stem, const FString& ClipName,
	USkeletalMesh* Mesh, UglTFRuntimeAsset* OwnAsset, FString& OutError)
{
	OutError.Reset();
	const FElysiumNpcClipSet* Set = GetClipSet(Stem);
	if (Set == nullptr)
	{
		OutError = FString::Printf(TEXT("no clip vocabulary for '%s'"), *Stem);
		return nullptr;
	}
	const FElysiumNpcClip* Clip = Set->Find(ClipName);
	if (Clip == nullptr)
	{
		OutError = FString::Printf(TEXT("'%s' resolves no clip named '%s'"), *Stem, *ClipName);
		return nullptr;
	}

	// The NPC's own dialogue clips live in the glb the mesh came from; everything else is a bank.
	UglTFRuntimeAsset* Asset = OwnAsset;
	if (!Clip->IsOwnedBy(Stem))
	{
		Asset = GetBankAsset(Clip->Owner, OutError);
	}
	else if (Asset == nullptr)
	{
		OutError = FString::Printf(TEXT("clip '%s' is owned by '%s' itself, but its glb was not passed"),
			*ClipName, *Stem);
	}
	if (Asset == nullptr)
	{
		return nullptr;
	}
	return ElysiumNpcVisual::RetargetClip(Asset, Mesh, ClipName, OutError);
}

UAnimSequence* UElysiumNpcAnimSubsystem::ResolveClipFromBank(const FString& BankStem,
	const FString& ClipName, USkeletalMesh* Mesh, FString& OutError)
{
	OutError.Reset();
	if (BankStem.IsEmpty() || Mesh == nullptr)
	{
		OutError = TEXT("no bank or no mesh");
		return nullptr;
	}
	UglTFRuntimeAsset* Asset = GetBankAsset(BankStem, OutError);
	if (Asset == nullptr)
	{
		return nullptr;
	}
	return ElysiumNpcVisual::RetargetClip(Asset, Mesh, ClipName, OutError);
}

TArray<FString> UElysiumNpcAnimSubsystem::IdleCandidates(const FString& Stem,
	const FString& Disposition, EElysiumIdleTier& OutTier)
{
	OutTier = EElysiumIdleTier::None;
	const FElysiumNpcClipSet* Set = GetClipSet(Stem);
	if (Set == nullptr)
	{
		return {};
	}

	// 1. The disposition stance set. `default_disposition` names a row whose "Animation Name"
	//    keys the `Stance_<Name>_Idle_*` clips in the NPC's gendered stances bank — the include
	//    DAG already picked male vs female, so there is no gender branch here.
	const FString AnimName = GetDispositions().AnimNameFor(Disposition);
	TArray<FString> Candidates = Set->StanceClips(AnimName);
	if (!Candidates.IsEmpty())
	{
		OutTier = EElysiumIdleTier::Stance;
	}
	else
	{
		// 2. ACT_IDLE. Weight discriminates here: `idle01` carries 30 against three fidgets at 1.
		Candidates = Set->ByActivity(ElysiumActivity::Idle);
		if (!Candidates.IsEmpty())
		{
			OutTier = EElysiumIdleTier::ActIdle;
		}
		else
		{
			// 3. The monsters and one-offs (`rat`, `tzim3`, `newscaster_male`) whose clips carry no
			//    activity at all. Only here does a label read decide anything.
			for (const TPair<FString, FElysiumNpcClip>& Pair : Set->Clips)
			{
				if (Pair.Value.Activity.IsEmpty() && Pair.Key.Contains(TEXT("idle"), ESearchCase::IgnoreCase))
				{
					Candidates.Add(Pair.Key);
				}
			}
			if (!Candidates.IsEmpty())
			{
				OutTier = EElysiumIdleTier::Loose;
			}
		}
	}
	Set->SortByWeight(Candidates);
	return Candidates;
}

FString UElysiumNpcAnimSubsystem::PickIdleClip(const FString& Stem, const FString& Disposition,
	EElysiumIdleTier& OutTier, int32 Variant)
{
	const TArray<FString> Candidates = IdleCandidates(Stem, Disposition, OutTier);
	if (Candidates.IsEmpty())
	{
		return FString();
	}
	// Variant only spreads across a *stance* set, whose members are equal-weight alternatives of
	// one pose. An ACT_IDLE set is not interchangeable — `idle01` carries weight 30 against three
	// fidgets at 1, so index 0 is the resting pick and the rest are one-shot fidgets.
	if (OutTier != EElysiumIdleTier::Stance || Variant <= 0)
	{
		return Candidates[0];
	}
	return Candidates[Variant % Candidates.Num()];
}

FString UElysiumNpcAnimSubsystem::PickActivityClip(const FString& Stem, const FString& Activity,
	int32 Variant)
{
	const FElysiumNpcClipSet* Set = GetClipSet(Stem);
	if (!Set || Activity.IsEmpty())
	{
		return FString();
	}
	TArray<FString> Candidates = Set->ByActivity(Activity);
	if (Candidates.IsEmpty())
	{
		return FString();
	}
	Candidates.Sort();
	int32 TotalWeight = 0;
	for (const FString& Label : Candidates)
	{
		const FElysiumNpcClip* Clip = Set->Find(Label);
		TotalWeight += FMath::Max(1, Clip ? Clip->Weight : 1);
	}
	const uint32 Seed = HashCombineFast(GetTypeHash(Stem.ToLower()), static_cast<uint32>(FMath::Max(0, Variant)));
	int32 Pick = static_cast<int32>(Seed % static_cast<uint32>(TotalWeight));
	for (const FString& Label : Candidates)
	{
		const FElysiumNpcClip* Clip = Set->Find(Label);
		Pick -= FMath::Max(1, Clip ? Clip->Weight : 1);
		if (Pick < 0)
		{
			return Label;
		}
	}
	return Candidates[0];
}

const TCHAR* UElysiumNpcAnimSubsystem::TierName(EElysiumIdleTier Tier)
{
	switch (Tier)
	{
	case EElysiumIdleTier::Stance:  return TEXT("stance");
	case EElysiumIdleTier::ActIdle: return TEXT("ACT_IDLE");
	case EElysiumIdleTier::Loose:   return TEXT("loose");
	default:                        return TEXT("none");
	}
}

void UElysiumNpcAnimSubsystem::GetBankStats(int32& OutCount, int64& OutBytes) const
{
	OutCount = 0;
	OutBytes = 0;
	for (const TPair<FString, TObjectPtr<UglTFRuntimeAsset>>& Pair : BankAssets)
	{
		if (Pair.Value == nullptr)
		{
			continue;
		}
		++OutCount;
		const FElysiumNpcIndexEntry* E = Index.Banks.Find(Pair.Key);
		if (E != nullptr)
		{
			OutBytes += IFileManager::Get().FileSize(*FElysiumContentPaths::NpcBankGlb(E->Glb));
		}
	}
}
