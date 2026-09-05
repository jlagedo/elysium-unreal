#include "Visual/ElysiumExpressionPreparation.h"
#include "Visual/ElysiumExpressionTable.h"
#include "ElysiumCastData.h"
#include "ElysiumCharacterProvenance.h"
#include "ElysiumExpressionData.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"

namespace
{
	// Lookup transport only: entity identity, lifecycle, timing and dispatch stay in the world.
	TMap<uint32, TWeakPtr<FElysiumExpressionPreparation>> PreparedByEpoch;
}

FElysiumExpressionPreparation::FElysiumExpressionPreparation(uint32 InEpoch, UElysiumCastData* InCast)
	: Epoch(InEpoch), CastData(InCast), Corpus(InCast ? InCast->ExpressionTables.Get() : nullptr)
{
}

TSharedPtr<FElysiumExpressionPreparation> FElysiumExpressionPreparation::Create(uint32 Epoch,
	UElysiumCastData* Cast, FString& OutError)
{
	OutError.Reset();
	if (!IsInGameThread() || Epoch == 0)
	{ OutError = TEXT("expression preparation requires the game thread and a map epoch"); return nullptr; }
	// A rejected refresh cannot leave old data discoverable under the same epoch.
	PreparedByEpoch.Remove(Epoch);
	if (!Cast || !Cast->ExpressionTables || Cast->ExpressionTables->Tables.IsEmpty())
	{ OutError = TEXT("expression preparation requires a loaded cast with its cooked expression corpus reference"); return nullptr; }
	TSharedPtr<FElysiumExpressionPreparation> Prepared = MakeShareable(new FElysiumExpressionPreparation(Epoch, Cast));
	for (const auto& Pair : Prepared->Corpus->Tables)
	{
		const auto* Data = Pair.Value.Get();
		if (!Data || Data->AssetId != Pair.Key)
		{ OutError = TEXT("expression corpus contains an absent/mismatched native reference: ") + Pair.Key; return nullptr; }
		if (Data->RuntimeStatus == TEXT("ready"))
		{
			auto View = Data->PrepareLegacyView(OutError);
			if (!View.IsValid()) return nullptr;
			Prepared->Views.Add(Pair.Key, MoveTemp(View));
		}
		else if (Data->RuntimeStatus != TEXT("authoring-only") && Data->RuntimeStatus != TEXT("undecoded-vfe")
			&& Data->RuntimeStatus != TEXT("unsupported-vfe"))
		{ OutError = TEXT("unknown expression runtime status: ") + Pair.Key + TEXT(" (") + Data->RuntimeStatus + TEXT(")"); return nullptr; }
		// Non-evaluable units remain hard-referenced in Corpus and fail explicitly if selected.
	}
	PreparedByEpoch.Add(Epoch, Prepared);
	return Prepared;
}

FElysiumExpressionPreparation::~FElysiumExpressionPreparation()
{
	if (const auto* Entry = PreparedByEpoch.Find(Epoch))
	{
		const auto Current = Entry->Pin();
		// An older handle must never unregister its replacement for the same epoch.
		if (!Current.IsValid() || Current.Get() == this) PreparedByEpoch.Remove(Epoch);
	}
}

void FElysiumExpressionPreparation::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(CastData);
	Collector.AddReferencedObject(Corpus);
}

TSharedPtr<const FElysiumExpressionTable> FElysiumExpressionPreparation::Event(
	const FString& Param, const FString& Class, FString& OutError) const
{
	const auto* Data = Corpus->ResolveEvent(Param, Class, OutError);
	if (!Data) return nullptr;
	if (const auto* View = Views.Find(Data->AssetId)) return *View;
	OutError = TEXT("expression view was not prepared: ") + Data->AssetId;
	return nullptr;
}

TSharedPtr<const FElysiumExpressionTable> FElysiumExpressionPreparation::Phonemes(
	const FElysiumEntity& Actor, FString& OutDiagnostic) const
{
	return ModelSelection(Actor, TEXT("phonemes"), OutDiagnostic);
}

TSharedPtr<const FElysiumExpressionTable> FElysiumExpressionPreparation::ModelSelection(
	const FElysiumEntity& Actor, const FString& Class, FString& OutDiagnostic) const
{
	OutDiagnostic.Reset();
	if (Actor.Handle.Epoch != Epoch || (Actor.World && Actor.World->GetEpoch() != Epoch))
	{ OutDiagnostic = TEXT("expression actor belongs to a different map epoch"); return nullptr; }
	const auto* Model = CastData->FindModel(Actor.Model, OutDiagnostic);
	if (!Model) return nullptr;
	const USkeletalMeshComponent* Body = Actor.GetSkeletalBody();
	if (!Body) Body = Actor.GenericModelBody;
	const auto* Record = UElysiumCharacterProvenance::Find(Body ? Body->GetSkeletalMeshAsset() : nullptr);
	if (!Record || !Record->bHasMeshData || !Record->bHasExpressionData)
	{ OutDiagnostic = TEXT("actor's prepared body has no cooked expression selection: ") + Model->AssetId; return nullptr; }
	if (Record->AssetId != Model->AssetId)
	{ OutDiagnostic = TEXT("actor model and prepared expression body disagree: ") + Model->AssetId + TEXT(" / ") + Record->AssetId; return nullptr; }
	// NPC stat templates do not initialize model-derived gender here. Retail SetModel
	// owns it through the MDL flag; never borrow the player's sheet for an NPC or proxy.
	bool bMale = Record->bExpressionModelIsMale;
	if (Actor.World)
		if (const FElysiumPlayer* Player = Actor.World->FindPlayer(); Player == &Actor)
			bMale = Player->Sheet.IsMale();
	const FElysiumExpressionSelection* Selection = Record->ExpressionSelections.FindByPredicate(
		[&Class](const FElysiumExpressionSelection& Row) { return Row.TableClass == Class; });
	if (!Selection)
	{ OutDiagnostic = TEXT("actor's cooked model declares no ") + Class + TEXT(" selection: ") + Model->AssetId; return nullptr; }
	const auto* Data = Corpus->ResolveSelection(*Selection, bMale, OutDiagnostic);
	if (!Data) return nullptr;
	if (const auto* View = Views.Find(Data->AssetId)) return *View;
	OutDiagnostic = TEXT("model expression view was not prepared: ") + Data->AssetId;
	return nullptr;
}

TSharedPtr<FElysiumExpressionPreparation> ElysiumExpressions::PrepareResident(uint32 Epoch,
	UElysiumCastData* Cast, FString& OutError)
{
	return FElysiumExpressionPreparation::Create(Epoch, Cast, OutError);
}

TSharedPtr<const FElysiumExpressionTable> ElysiumExpressions::LoadPreparedEvent(uint32 Epoch,
	const FString& Param, const FString& Class, FString& OutError)
{
	OutError.Reset();
	if (!IsInGameThread()) { OutError = TEXT("expression lookup requires the game thread"); return nullptr; }
	if (const auto* Entry = PreparedByEpoch.Find(Epoch))
		if (const auto Prepared = Entry->Pin()) return Prepared->Event(Param, Class, OutError);
#if WITH_DEV_AUTOMATION_TESTS
	// Explicit existing headless-test injection only; never consult the legacy disk cache.
	if (const auto Inline = FindInlineForTest(Param, Class)) return Inline;
#endif
	OutError = FString::Printf(TEXT("expression corpus/views were not prepared for map epoch %u"), Epoch);
	return nullptr;
}

TSharedPtr<const FElysiumExpressionTable> ElysiumExpressions::LoadPreparedPhonemes(
	const FElysiumEntity& Actor, FString& OutDiagnostic)
{
	return LoadPreparedModelSelection(Actor, TEXT("phonemes"), OutDiagnostic);
}

TSharedPtr<const FElysiumExpressionTable> ElysiumExpressions::LoadPreparedModelSelection(
	const FElysiumEntity& Actor, const FString& Class, FString& OutDiagnostic)
{
	OutDiagnostic.Reset();
	if (!IsInGameThread()) { OutDiagnostic = TEXT("expression lookup requires the game thread"); return nullptr; }
	if (const auto* Entry = PreparedByEpoch.Find(Actor.Handle.Epoch))
		if (const auto Prepared = Entry->Pin()) return Prepared->ModelSelection(Actor, Class, OutDiagnostic);
#if WITH_DEV_AUTOMATION_TESTS
	if (const auto Inline = FindInlineForTest(Actor.Model, Class)) return Inline;
#endif
	OutDiagnostic = FString::Printf(TEXT("%s corpus/views were not prepared for map epoch %u"), *Class, Actor.Handle.Epoch);
	return nullptr;
}
