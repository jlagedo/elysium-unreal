#include "Visual/ElysiumNativeAnimationData.h"
#include "ElysiumCastData.h"
#include "ElysiumBodyData.h"
#include "ElysiumClipData.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Visual/ElysiumCharacterModel.h"
#include "Containers/Ticker.h"
#include "UObject/StrongObjectPtr.h"
#if WITH_EDITOR
#include "Animation/IAnimationSequenceCompiler.h"
#endif

struct FElysiumAsyncModelAdmission
{
	uint64 Id = 0, Epoch = 0, Generation = 0;
	FString ModelId;
	TFunction<void(bool, const FString&)> Completion;
	bool bRootDiscovered = false, bDependenciesRequested = false;
	struct FOwner
	{
		FString AssetId, OwnerRoot;
		TSoftObjectPtr<UElysiumBodyData> Data;
	};
	TArray<FOwner> Pending;
	TMap<FString, TObjectPtr<UElysiumBodyData>> Discovered;
	TArray<TStrongObjectPtr<UElysiumBodyData>> Pins;
	TSet<FSoftObjectPath> Dependencies;
	TArray<TSharedPtr<FStreamableHandle>> Handles;
	FTSTicker::FDelegateHandle Ticker;
};

namespace
{
	bool AnimationReferencesResident(const UElysiumBodyData& Data, FString& Error)
	{
		auto Check = [&Error](const auto& Ref, bool bRequired)
		{
			if ((!bRequired && Ref.IsNull()) || Ref.Get()) return true;
			Error = TEXT("native animation reference is absent or has the wrong class: ") + Ref.ToSoftObjectPath().ToString();
			return false;
		};
		for (const auto& Pair : Data.NativeSequences) if (!Check(Pair.Value, true)) return false;
		for (const auto& Pair : Data.NativeBlendSpaces) if (!Check(Pair.Value, true)) return false;
		auto CheckRow = [&Check](const FElysiumBodyAnimationRef& Ref)
		{ return Check(Ref.Sequence, false) && Check(Ref.BlendSpace, false) && Check(Ref.BaseCell, false); };
		for (const auto& Row : Data.Sequences)
		{
			if (!CheckRow(Row.Assets)) return false;
			for (const auto& Layer : Row.Layers) if (!CheckRow(Layer)) return false;
		}
		return true;
	}

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
	++AdmissionGeneration;
	TArray<uint64> PendingIds; ModelAdmissions.GetKeys(PendingIds);
	for (uint64 Id : PendingIds) CancelModelAdmission(Id);
	for (auto& Handle : Loads) if (Handle.IsValid())
	{
		if (!Handle->HasLoadCompleted()) Handle->CancelHandle();
		else Handle->ReleaseHandle();
	}
	Loads.Reset(); Bodies.Reset(); Vocabularies.Reset(); Tables.Reset();
}

uint64 UElysiumNativeAnimationData::AdmitModelAsync(const FString& ModelId, uint64 OwnerEpoch,
	TFunction<void(bool, const FString&)> Completion, FString& OutError)
{
	OutError.Reset();
	if (!IsInGameThread() || !ElysiumCharacterModel::IsCanonicalId(ModelId) || OwnerEpoch != PreparedEpoch || !Completion)
	{ OutError = TEXT("async model admission requires a canonical ID, live preparation epoch, callback and game thread"); return 0; }
	auto Request = MakeShared<FElysiumAsyncModelAdmission>();
	Request->Id = ++NextAdmissionId; Request->Epoch = OwnerEpoch; Request->Generation = AdmissionGeneration;
	Request->ModelId = ModelId; Request->Completion = MoveTemp(Completion);
	ModelAdmissions.Add(Request->Id, Request);
	// Even resident/missing metadata completes after the caller has stored its request token.
	Request->Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this,
		[this, Id = Request->Id](float)
		{
			if (auto* Found = ModelAdmissions.Find(Id)) (*Found)->Ticker.Reset();
			AdvanceModelAdmission(Id); return false;
		}));
	return Request->Id;
}

void UElysiumNativeAnimationData::CancelModelAdmission(uint64 RequestId)
{
	TSharedPtr<FElysiumAsyncModelAdmission> Request;
	if (!ModelAdmissions.RemoveAndCopyValue(RequestId, Request)) return;
	if (Request->Ticker.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(Request->Ticker);
	for (const auto& Handle : Request->Handles) if (Handle.IsValid())
	{
		if (Handle->HasLoadCompleted()) Handle->ReleaseHandle(); else Handle->CancelHandle();
	}
	Request->Completion = nullptr;
}

void UElysiumNativeAnimationData::LoadAdmissionPaths(uint64 RequestId, const TArray<FSoftObjectPath>& Paths)
{
	const auto* Found = ModelAdmissions.Find(RequestId);
	if (!Found) return;
	const auto Request = *Found;
	if (Paths.IsEmpty()) { FinishModelAdmission(RequestId, TEXT("async admission produced an empty load batch")); return; }
	auto Handle = UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths,
		FStreamableDelegate::CreateWeakLambda(this, [this, RequestId, Paths]
		{
			const auto* Active = ModelAdmissions.Find(RequestId);
			if (!Active) return;
			const auto& Handles = (*Active)->Handles;
			if (Handles.IsEmpty() || !Handles.Last()->HasLoadCompleted() || Handles.Last()->HasError())
			{ FinishModelAdmission(RequestId, TEXT("async native model load failed")); return; }
			for (const auto& Path : Paths)
				if (!Path.ResolveObject())
				{ FinishModelAdmission(RequestId, TEXT("async native dependency is absent: ") + Path.ToString()); return; }
			AdvanceModelAdmission(RequestId);
		}), FStreamableManager::DefaultAsyncLoadPriority, false, true);
	if (!Handle.IsValid()) { FinishModelAdmission(RequestId, TEXT("could not create async native model load")); return; }
	Request->Handles.Add(Handle);
	Handle->StartStalledHandle();
}

bool UElysiumNativeAnimationData::AdmissionAssetsCompiling(const FElysiumAsyncModelAdmission& Request) const
{
#if WITH_EDITOR
	for (const auto& Path : Request.Dependencies)
	{
		UObject* Asset = Path.ResolveObject();
		if (const auto* Sequence = ::Cast<UAnimSequence>(Asset); Sequence && Sequence->IsCompiling()) return true;
		if (const auto* Mesh = ::Cast<USkeletalMesh>(Asset); Mesh && Mesh->IsCompiling()) return true;
	}
#endif
	return false;
}

void UElysiumNativeAnimationData::AdvanceModelAdmission(uint64 RequestId)
{
	const auto* Found = ModelAdmissions.Find(RequestId);
	if (!Found) return;
	const auto Request = *Found;
	if (Request->Generation != AdmissionGeneration || Request->Epoch != PreparedEpoch)
	{ CancelModelAdmission(RequestId); return; }
	if (!Cast)
	{
		const TSoftObjectPtr<UElysiumCastData> Ref(FSoftObjectPath(TEXT("/ElysiumBaked/Models/_Corpus/DA_Cast.DA_Cast")));
		Cast = Ref.Get();
		if (!Cast)
		{
			if (Ref.ToSoftObjectPath().ResolveObject())
			{ FinishModelAdmission(RequestId, TEXT("native cast path resolves to the wrong UObject class")); return; }
			LoadAdmissionPaths(RequestId, {Ref.ToSoftObjectPath()}); return;
		}
	}
	FString Error;
	if (!Request->bRootDiscovered)
	{
		const auto* Entry = Cast->FindModel(Request->ModelId, Error);
		if (!Entry || Entry->Mesh.IsNull() || Entry->BodyData.IsNull())
		{ FinishModelAdmission(RequestId, Error.IsEmpty() ? TEXT("character has no native mesh/body reference: ") + Request->ModelId : Error); return; }
		Request->Dependencies.Add(Entry->Mesh.ToSoftObjectPath());
		Request->Pending.Add({Entry->AssetId, FString(), Entry->BodyData});
		if (const auto* Cinematic = Cast->Cinematics.Find(Entry->AssetId))
			for (const auto& Pair : Cinematic->Roots)
				Request->Pending.Add({Pair.Value.AssetId, Pair.Value.OwnerRoot, Pair.Value.BodyData});
		Request->bRootDiscovered = true;
	}
	while (!Request->Pending.IsEmpty())
	{
		const auto Owner = Request->Pending.Pop(EAllowShrinking::No);
		const FString Key = Owner.Data.ToSoftObjectPath().ToString();
		if (Owner.Data.IsNull()) { FinishModelAdmission(RequestId, TEXT("native include has no body reference: ") + Owner.AssetId); return; }
		if (const auto* Prior = Request->Discovered.Find(Key))
		{
			if ((*Prior)->AssetId != Owner.AssetId || (*Prior)->OwnerRoot != Owner.OwnerRoot)
			{ FinishModelAdmission(RequestId, TEXT("native body address has conflicting owner identities: ") + Key); return; }
			continue;
		}
		UElysiumBodyData* Data = Owner.Data.Get();
		if (!Data)
		{
			if (Owner.Data.ToSoftObjectPath().ResolveObject())
			{ FinishModelAdmission(RequestId, TEXT("native BodyData path resolves to the wrong UObject class: ") + Key); return; }
			Request->Pending.Add(Owner);
			TSet<FSoftObjectPath> Wave;
			for (const auto& Pending : Request->Pending)
				if (!Pending.Data.IsNull() && !Pending.Data.Get()) Wave.Add(Pending.Data.ToSoftObjectPath());
			LoadAdmissionPaths(RequestId, Wave.Array()); return;
		}
		if (Data->AssetId != Owner.AssetId || Data->OwnerRoot != Owner.OwnerRoot)
		{ FinishModelAdmission(RequestId, TEXT("native BodyData identity differs from its reference: ") + Key); return; }
		Request->Pins.Emplace(Data);
		Request->Discovered.Add(Key, Data);
		Data->GatherAnimationPaths(Request->Dependencies);
		for (const auto& Include : Data->IncludeOwners)
		{
			if (Include.AssetId == Data->AssetId) continue;
			const auto* Included = Cast->FindModel(Include.AssetId, Error);
			if (!Included || Included->BodyData.IsNull())
			{ FinishModelAdmission(RequestId, TEXT("native include owner is absent: ") + Include.AssetId); return; }
			Request->Pending.Add({Included->AssetId, FString(), Included->BodyData});
		}
	}
	if (!Request->bDependenciesRequested)
	{
		Request->bDependenciesRequested = true;
		LoadAdmissionPaths(RequestId, Request->Dependencies.Array()); return;
	}
	for (const auto& Pair : Request->Discovered)
		if (!AnimationReferencesResident(*Pair.Value, Error)) { FinishModelAdmission(RequestId, Error); return; }
	if (AdmissionAssetsCompiling(*Request))
	{
		if (!Request->Ticker.IsValid())
			Request->Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this,
				[this, RequestId](float)
				{
					if (auto* Active = ModelAdmissions.Find(RequestId)) (*Active)->Ticker.Reset();
					AdvanceModelAdmission(RequestId); return false;
				}), .05f);
		return;
	}
	const auto* Entry = Cast->FindModel(Request->ModelId, Error);
	if (!Entry || !ElysiumCharacterModel::Validate(Request->ModelId, Entry->Mesh.Get(), Error))
	{ FinishModelAdmission(RequestId, Error); return; }
	// One publication point: selectors cannot observe a partially discovered/loading owner graph.
	for (const auto& Pair : Request->Discovered) { Vocabularies.Remove(Pair.Key); Tables.Remove(Pair.Key); }
	Bodies.Append(Request->Discovered);
	Loads.Append(MoveTemp(Request->Handles));
	FinishModelAdmission(RequestId, FString());
}

void UElysiumNativeAnimationData::FinishModelAdmission(uint64 RequestId, const FString& Error)
{
	const auto* Found = ModelAdmissions.Find(RequestId);
	if (!Found) return;
	const auto Request = *Found;
	auto Completion = MoveTemp(Request->Completion);
	const bool bCurrent = Request->Generation == AdmissionGeneration && Request->Epoch == PreparedEpoch;
	CancelModelAdmission(RequestId);
	if (bCurrent && Completion) Completion(Error.IsEmpty(), Error);
}

bool UElysiumNativeAnimationData::IsModelReady(const FString& ModelId) const
{
	if (!ElysiumCharacterModel::IsCanonicalId(ModelId) || !Cast) return false;
	FString Error;
	USkeletalMesh* RootMesh = Mesh(ModelId, Error);
	if (!ElysiumCharacterModel::Validate(ModelId, RootMesh, Error)) return false;
	TArray<const UElysiumBodyData*> Pending{Body(ModelId)};
	if (const auto* Cinematic = Cast->Cinematics.Find(ModelId))
		for (const auto& Pair : Cinematic->Roots) Pending.Add(PreparedBody(Pair.Value.BodyData));
	TSet<const UElysiumBodyData*> Seen;
	while (!Pending.IsEmpty())
	{
		const auto* Data = Pending.Pop(EAllowShrinking::No);
		if (!Data) return false;
		if (Seen.Contains(Data)) continue;
		Seen.Add(Data);
		if (!AnimationReferencesResident(*Data, Error)) return false;
		for (const auto& Include : Data->IncludeOwners)
			if (Include.AssetId != Data->AssetId) Pending.Add(Body(Include.AssetId));
		TSet<FSoftObjectPath> Paths; Data->GatherAnimationPaths(Paths);
		for (const auto& Path : Paths)
		{
			if (!Path.ResolveObject()) return false;
#if WITH_EDITOR
			if (const auto* Sequence = ::Cast<UAnimSequence>(Path.ResolveObject()); Sequence && Sequence->IsCompiling()) return false;
#endif
		}
	}
#if WITH_EDITOR
	if (RootMesh->IsCompiling()) return false;
#endif
	return true;
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
