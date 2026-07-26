#include "ElysiumNpcAnimSubsystem.h"

#include "ElysiumContentPaths.h"
#include "ElysiumNpcVisual.h"

#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/FileManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysium, Log, All);

void UElysiumNpcAnimSubsystem::Deinitialize()
{
	BankAssets.Reset();
	ClipSets.Reset();
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
			UE_LOG(LogElysium, Warning, TEXT("npc index: %s"), *Error);
		}
		else
		{
			UE_LOG(LogElysium, Log, TEXT("npc index: %d NPCs, %d animation banks"),
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
			UE_LOG(LogElysium, Warning, TEXT("disposition table: %s"), *Error);
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
		UE_LOG(LogElysium, Warning, TEXT("npc clips '%s': %s"), *Stem, *Error);
		Set.Reset();   // remembered as a miss, so this is not retried per NPC sharing the stem
	}
	ClipSets.Add(Stem, Set);
	return Set.Get();
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
	UE_LOG(LogElysium, Log, TEXT("npc bank '%s' parsed in %.0f ms (%.1f MB)"), *BankStem,
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
