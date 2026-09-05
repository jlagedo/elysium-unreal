#include "Visual/ElysiumNativeAnimationData.h"
#include "ElysiumCastData.h"
#include "ElysiumBodyData.h"
#include "ElysiumClipData.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#if WITH_EDITOR
#include "Animation/IAnimationSequenceCompiler.h"
#endif

namespace
{
	template<typename T>
	const T* FindAnimationLabel(const TMap<FString,T>& Values,const FString& Label)
	{
		if (const T* Exact=Values.Find(Label)) return Exact;
		// Asset names and authored sequence lookup ignore case. Preserve original labels
		// in the cooked table while accepting the same names at the native lookup seam.
		for (const auto& Pair : Values)
			if (Pair.Key.Equals(Label,ESearchCase::IgnoreCase)) return &Pair.Value;
		return nullptr;
	}
}

void UElysiumNativeAnimationData::Deinitialize()
{
	ReleasePrepared(); Cast=nullptr;
	Super::Deinitialize();
}

void UElysiumNativeAnimationData::ReleasePrepared()
{
	for (auto& Handle : Loads) if (Handle.IsValid())
	{
		if (!Handle->HasLoadCompleted()) Handle->CancelHandle();
		else Handle->ReleaseHandle();
	}
	Loads.Reset(); Bodies.Reset(); Vocabularies.Reset(); Tables.Reset();
}

void UElysiumNativeAnimationData::ReleaseEpoch(uint64 Epoch)
{
	if (PreparedEpoch==Epoch) ReleasePrepared();
}

TSharedPtr<FStreamableHandle> UElysiumNativeAnimationData::Prepare(const FString& Model, FString& OutError)
{
	return PrepareMany({Model},OutError);
}

TSharedPtr<FStreamableHandle> UElysiumNativeAnimationData::PrepareMany(const TArray<FString>& Models, FString& OutError)
{
	if (!LoadCast(OutError)) return nullptr;
	TArray<TSoftObjectPtr<UElysiumBodyData>> Pending;
	TMap<FString,TObjectPtr<UElysiumBodyData>> Prepared;
	TSet<FSoftObjectPath> Paths;
	for (const FString& Model : Models)
	{
		const FElysiumCastModel* Entry=Cast->FindModel(Model,OutError);
		if (!Entry) return nullptr;
		if (!Entry->Mesh.IsNull()) Paths.Add(Entry->Mesh.ToSoftObjectPath());
		if (!Entry->BodyData.IsNull()) Pending.Add(Entry->BodyData);
		const auto* Cinematic=Cast->Cinematics.Find(Entry->AssetId);
		if (Cinematic)
			for (const auto& Pair : Cinematic->Roots) Pending.Add(Pair.Value.BodyData);
		if (Entry->BodyData.IsNull() && (!Cinematic || Cinematic->Roots.IsEmpty()))
		{
			OutError=TEXT("native model has no animation owner: ")+Entry->AssetId; return nullptr;
		}
	}
	while (!Pending.IsEmpty())
	{
		const auto Ref=Pending.Pop(EAllowShrinking::No);
		const FString Key=Ref.ToSoftObjectPath().ToString();
		if (Prepared.Contains(Key)) continue;
		UElysiumBodyData* Data=Ref.LoadSynchronous();
		if (!Data) { OutError=TEXT("native body table is absent: ")+Key; return nullptr; }
		Prepared.Add(Key,Data);
		for (const auto& Include : Data->IncludeOwners)
		{
			// The source descriptor index includes its own owner at base zero. For a
			// cinematic slice that means this actor, not a nonexistent main body table.
			if (Include.AssetId==Data->AssetId) continue;
			const auto* Owner=Cast->FindModel(Include.AssetId,OutError);
			if (!Owner) return nullptr;
			if (Owner->BodyData.IsNull())
			{
				OutError=TEXT("native include has no main owner: ")+Include.AssetId; return nullptr;
			}
			Pending.Add(Owner->BodyData);
		}
		Data->GatherAnimationPaths(Paths);
	}
	TSharedPtr<FStreamableHandle> Handle;
	if (!Paths.IsEmpty())
	{
		Handle=UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths.Array());
		if (!Handle.IsValid()) { OutError=TEXT("native animation request could not be created"); return nullptr; }
		Loads.Add(Handle);
	}
	// Failed discovery cannot publish a partial owner graph. These references pin metadata;
	// the caller still waits on the returned request before using animation objects.
	Bodies.Append(MoveTemp(Prepared));
	OutError.Reset();
	return Handle;
}

bool UElysiumNativeAnimationData::LoadCast(FString& OutError)
{
	if (!Cast) Cast=LoadObject<UElysiumCastData>(nullptr,TEXT("/ElysiumBaked/Models/_Corpus/DA_Cast.DA_Cast"));
	if (!Cast) { OutError=TEXT("native cast table is absent"); return false; }
	OutError.Reset(); return true;
}

bool UElysiumNativeAnimationData::KnowsModel(const FString& Model) const
{
	if (!Cast) return false;
	FString Error;
	return Cast->FindModel(Model,Error)!=nullptr;
}

bool UElysiumNativeAnimationData::FinishPreparation(const TSharedPtr<FStreamableHandle>& Handle,FString& OutError)
{
	if (!Handle.IsValid() || !Handle->HasLoadCompleted() || Handle->HasError())
	{
		OutError=TEXT("native animation request did not complete successfully"); return false;
	}
	TArray<UObject*> Assets; Handle->GetLoadedAssets(Assets);
	if (Assets.Contains(nullptr)) { OutError=TEXT("native animation request contains missing assets"); return false; }
#if WITH_EDITOR
	TArray<UAnimSequence*> Sequences;
	for (UObject* Asset : Assets) if (auto* Sequence=::Cast<UAnimSequence>(Asset)) Sequences.Add(Sequence);
	if (!Sequences.IsEmpty()) UE::Anim::IAnimSequenceCompilingManager::FinishCompilation(Sequences);
#endif
	OutError.Reset(); return true;
}

TSharedPtr<FStreamableHandle> UElysiumNativeAnimationData::PrepareMapModels(const TArray<FString>& Models,
	FString& OutError,uint64 Epoch,const TArray<FString>& CinematicModels)
{
	if (PreparedEpoch!=Epoch) { ReleasePrepared(); PreparedEpoch=Epoch; }
	if (!LoadCast(OutError)) return nullptr;
	TArray<FString> Selected;
	for (const FString& Model : Models)
	{
		FString Ignored;
		const auto* Entry=Cast->FindModel(Model,Ignored);
		if (Entry && !Entry->Mesh.IsNull() && !Entry->BodyData.IsNull()) Selected.AddUnique(Entry->AssetId);
	}
	for (const FString& Model : CinematicModels)
	{
		if (Model.IsEmpty()) continue;
		const auto* Entry=Cast->FindModel(Model,OutError);
		if (!Entry) return nullptr;
		if (!Cast->Cinematics.Contains(Entry->AssetId))
		{
			OutError=TEXT("model has no cinematic owner table: ")+Entry->AssetId; return nullptr;
		}
		Selected.AddUnique(Entry->AssetId);
	}
	OutError.Reset();
	return Selected.IsEmpty()?nullptr:PrepareMany(Selected,OutError);
}

const UElysiumBodyData* UElysiumNativeAnimationData::Body(const FString& Model) const
{
	if (!Cast) return nullptr;
	FString Error;
	const auto* Entry=Cast->FindModel(Model,Error);
	return Entry?PreparedBody(Entry->BodyData):nullptr;
}

const UElysiumBodyData* UElysiumNativeAnimationData::CinematicBody(const FString& Model,const FString& Root) const
{
	if (!Cast) return nullptr;
	FString Error;
	const auto* Entry=Cast->FindCinematic(Model,Root,Error);
	return Entry?PreparedBody(Entry->BodyData):nullptr;
}

USkeletalMesh* UElysiumNativeAnimationData::Mesh(const FString& Model,FString& OutError) const
{
	if (!Cast || !Body(Model)) { OutError=TEXT("native model is not prepared: ")+Model; return nullptr; }
	const auto* Entry=Cast->FindModel(Model,OutError);
	auto* Result=Entry?Entry->Mesh.Get():nullptr;
	if (!Result) { OutError=TEXT("native mesh is absent or not resident: ")+Model; return nullptr; }
	OutError.Reset(); return Result;
}

const UElysiumBodyData* UElysiumNativeAnimationData::PreparedBody(const TSoftObjectPtr<UElysiumBodyData>& Ref) const
{
	const auto* Found=Bodies.Find(Ref.ToSoftObjectPath().ToString());
	return Found?Found->Get():nullptr;
}

const FElysiumNpcClipSet* UElysiumNativeAnimationData::Vocabulary(const FString& Model)
{
	const auto* Data=Body(Model);
	if (!Data) return nullptr;
	const FString Key=Data->GetPathName();
	if (const auto* Found=Vocabularies.Find(Key)) return Found->Get();
	FString Error;
	const auto* Entry=Cast->FindModel(Model,Error);
	auto Values=MakeShared<FElysiumNpcClipSet>(Data->SelectionVocabulary(Entry?Entry->Stem:Model));
	Vocabularies.Add(Key,Values);
	return &Values.Get();
}

UAnimSequence* UElysiumNativeAnimationData::Sequence(const FString& Owner,const FString& Label) const
{
	return Sequence(Body(Owner),Label);
}

UAnimSequence* UElysiumNativeAnimationData::Sequence(const UElysiumBodyData* Data,const FString& Label) const
{
	const auto* Ref=Data?FindAnimationLabel(Data->NativeSequences,Label):nullptr;
	return Ref?Ref->Get():nullptr;
}

UBlendSpace* UElysiumNativeAnimationData::BlendSpace(const FString& Owner,const FString& Label) const
{
	return BlendSpace(Body(Owner),Label);
}

UBlendSpace* UElysiumNativeAnimationData::BlendSpace(const UElysiumBodyData* Data,const FString& Label) const
{
	const auto* Ref=Data?FindAnimationLabel(Data->NativeBlendSpaces,Label):nullptr;
	return Ref?Ref->Get():nullptr;
}

UAnimSequence* UElysiumNativeAnimationData::Sequence(const FElysiumBodyAnimationRef& Ref) const { return Ref.Sequence.Get(); }
UBlendSpace* UElysiumNativeAnimationData::BlendSpace(const FElysiumBodyAnimationRef& Ref) const { return Ref.BlendSpace.Get(); }

const UElysiumClipData* UElysiumNativeAnimationData::ClipData(const FString& Owner,const FString& Label) const
{
	return ClipData(Body(Owner),Label);
}

const UElysiumClipData* UElysiumNativeAnimationData::ClipData(const UElysiumBodyData* Data,const FString& Label) const
{
	if (Data)
		if (const auto* Row=Data->Find(Label))
		{
			if (auto* Asset=Row->Assets.Sequence.Get()) return Asset->FindMetaDataByClass<UElysiumClipData>();
			if (auto* Asset=Row->Assets.BlendSpace.Get()) return Asset->FindMetaDataByClass<UElysiumClipData>();
		}
	if (auto* Asset=Sequence(Data,Label)) return Asset->FindMetaDataByClass<UElysiumClipData>();
	if (auto* Asset=BlendSpace(Data,Label)) return Asset->FindMetaDataByClass<UElysiumClipData>();
	return nullptr;
}

TSharedPtr<const FElysiumBlendTable> UElysiumNativeAnimationData::BlendTable(const FString& Owner)
{
	return BlendTable(Body(Owner));
}

TSharedPtr<const FElysiumBlendTable> UElysiumNativeAnimationData::BlendTable(const UElysiumBodyData* Data)
{
	if (!Data) return nullptr;
	const FString Key=Data->GetPathName();
	if (const auto* Found=Tables.Find(Key)) return *Found;
	auto Table=MakeShared<FElysiumBlendTable>(); Table->Stem=Data->AssetId;
	Table->bMovementStated=true;
	for (const auto& Pair : Data->NativeSequences)
	{
		const UAnimSequence* Asset=Pair.Value.Get();
		const auto* Meta=Asset?Asset->FindMetaDataByClass<UElysiumClipData>():nullptr;
		if (!Meta || !Meta->bMovementStated) return nullptr;
		if (!Meta->Events.IsEmpty()) Table->Events.Add(Meta->SourceLabel,Meta->Events);
		if (!Meta->Movement.Records.IsEmpty()) Table->Movement.Add(Meta->SourceLabel,Meta->Movement);
	}
	for (const auto& Row : Data->Sequences)
		if (Row.Owner==Data->AssetId && !Row.DeclaredLayers.IsEmpty()) Table->AutoLayers.Add(Row.Label,{Row.DeclaredLayers});
	for (const auto& Pair : Data->NativeBlendSpaces)
	{
		const UBlendSpace* Asset=Pair.Value.Get();
		const auto* Meta=Asset?Asset->FindMetaDataByClass<UElysiumClipData>():nullptr;
		if (!Meta || !Meta->bHasGrid) return nullptr; // preparation is incomplete; never cache a partial table
		Table->PoseParams=Meta->PoseParams;
		Table->Grids.Add(Meta->SourceLabel,Meta->Grid);
	}
	Tables.Add(Key,Table);
	return Table;
}
