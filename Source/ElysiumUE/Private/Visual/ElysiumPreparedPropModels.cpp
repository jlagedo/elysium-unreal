#include "Visual/ElysiumPreparedPropModels.h"
#include "Visual/ElysiumCharacterModel.h"
#include "Visual/ElysiumEntityBodies.h"
#include "Visual/ElysiumNpcVisual.h"
#include "ElysiumBodyData.h"
#include "ElysiumCharacterProvenance.h"
#include "ElysiumClipData.h"
#include "ElysiumContentPaths.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "ChaosClothAsset/ClothAsset.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"

namespace
{
	TMap<FObjectKey, TWeakPtr<FElysiumPreparedPropModels>> Prepared;

	template<typename T> const T* Named(const TMap<FString, T>& Values, const FString& Name)
	{
		for (const auto& Pair : Values) if (Pair.Key.Equals(Name, ESearchCase::IgnoreCase)) return &Pair.Value;
		return nullptr;
	}
	const FElysiumCatalogueSkinRepresentation* Representation(const UElysiumPropSkinCatalogue* Skins,
		const FString& Id, const TCHAR* Kind)
	{
		const auto* Model = Skins ? Skins->Data.Models.Find(Id) : nullptr;
		return Model ? Model->Representations.FindByPredicate([&](const auto& Row) { return Row.Kind == Kind; }) : nullptr;
	}
}

FString ElysiumPreparedProps::ModelId(const FString& SourceOrId)
{
	return ElysiumCharacterModel::IdFromSource(SourceOrId);
}

FElysiumPreparedPropModels::FElysiumPreparedPropModels(UObject* InOwner, uint64 InEpoch)
	: OwnerKey(InOwner), Owner(InOwner), Epoch(InEpoch) {}

FElysiumPreparedPropModels::~FElysiumPreparedPropModels()
{
	if (const auto* Found = Prepared.Find(OwnerKey))
		if (const auto Current = Found->Pin(); !Current || Current.Get() == this) Prepared.Remove(OwnerKey);
}

void FElysiumPreparedPropModels::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Placed);
	Collector.AddReferencedObject(Skins);
	for (auto& Pair : Assets) Collector.AddReferencedObject(Pair.Value);
}

TSharedPtr<FElysiumPreparedPropModels> ElysiumPreparedProps::ForOwner(const UObject* Owner)
{
	if (!Owner || !IsInGameThread()) return nullptr;
	const auto* Found = Prepared.Find(FObjectKey(Owner));
	return Found ? Found->Pin() : nullptr;
}

void ElysiumPreparedProps::Release(const UObject* Owner)
{
	if (Owner && IsInGameThread()) Prepared.Remove(FObjectKey(Owner));
}

bool FElysiumPreparedPropModels::GatherPaths(const UElysiumPlacedModelCatalogue* Placed,
	const UElysiumPropSkinCatalogue* Skins, const TArray<FString>& ModelIds,
	TSet<FSoftObjectPath>& OutPaths, FString& Error)
{
	Error.Reset(); TSet<FSoftObjectPath> Paths;
	if (!Placed || !Skins) { Error = TEXT("placed and skin catalogues must already be resident"); return false; }
	for (const FString& Id : ModelIds)
	{
		if (!ElysiumCharacterModel::IsCanonicalId(Id)) { Error = TEXT("preparation requires a canonical model ID: ") + Id; return false; }
		const auto* Skin = Skins->Data.Models.Find(Id);
		const auto* Row = Placed->FindModel(Id);
		if (!Skin && !(Row && Row->bSourceAbsent)) { Error = TEXT("model is not in the cooked catalogue: ") + Id; return false; }
		if (Skin && Skin->AssetId != Id) { Error = TEXT("skin model identity differs: ") + Id; return false; }
		if (Row) Row->GatherPaths(Paths);
		if (Skin) for (const auto& Rep : Skin->Representations)
		{
			if ((Rep.Kind != TEXT("static") && Rep.Kind != TEXT("skeletal"))
				|| (Rep.Kind == TEXT("static") && Rep.StaticMesh.IsNull())
				|| (Rep.Kind == TEXT("skeletal") && Rep.SkeletalMesh.IsNull()))
			{ Error = TEXT("invalid native skin representation: ") + Id; return false; }
			if ((!Rep.StaticMesh.IsNull() && Rep.StaticMesh.ToSoftObjectPath().ToString() != FElysiumContentPaths::BakedUnit(Id, TEXT("SM")))
				|| (!Rep.SkeletalMesh.IsNull() && Rep.SkeletalMesh.ToSoftObjectPath().ToString() != FElysiumContentPaths::BakedUnit(Id, TEXT("SK"))))
			{ Error = TEXT("skin representation is not at its canonical model address: ") + Id; return false; }
			if (!Rep.StaticMesh.IsNull()) Paths.Add(Rep.StaticMesh.ToSoftObjectPath());
			if (!Rep.SkeletalMesh.IsNull()) Paths.Add(Rep.SkeletalMesh.ToSoftObjectPath());
		}
	}
	OutPaths.Append(Paths); return true;
}

TSharedPtr<FElysiumPreparedPropModels> FElysiumPreparedPropModels::Create(UObject* InOwner, uint64 InEpoch,
	UElysiumPlacedModelCatalogue* InPlaced, UElysiumPropSkinCatalogue* InSkins,
	const TArray<FString>& ModelIds, const TArray<UObject*>& ResidentAssets, FString& Error)
{
	Error.Reset();
	if (!IsInGameThread() || !InOwner || !InEpoch) { Error = TEXT("prop preparation requires an owner, epoch and game thread"); return nullptr; }
	if (!InPlaced || !InSkins) { Error = TEXT("placed and skin catalogues must already be resident"); return nullptr; }
	Prepared.Remove(FObjectKey(InOwner)); // A failed refresh cannot expose the previous epoch's data.
	TSharedPtr<FElysiumPreparedPropModels> Result = MakeShareable(new FElysiumPreparedPropModels(InOwner, InEpoch));
	Result->Placed = InPlaced; Result->Skins = InSkins;
	if (!Result->Admit(ModelIds, ResidentAssets, Error)) return nullptr;
	Prepared.Add(Result->OwnerKey, Result); return Result;
}

bool FElysiumPreparedPropModels::Knows(const FString& Id) const
{
	if (!Placed || !Skins) return false;
	if (Skins->Data.Models.Contains(Id)) return true;
	const auto* Row = Placed->FindModel(Id);
	return Row && Row->bSourceAbsent;
}

bool FElysiumPreparedPropModels::Admit(const TArray<FString>& ModelIds, const TArray<UObject*>& ResidentAssets, FString& Error)
{
	Error.Reset();
	if (!IsInGameThread()) { Error = TEXT("model admission requires the game thread"); return false; }
	TSet<FSoftObjectPath> Required;
	if (!GatherPaths(Placed, Skins, ModelIds, Required, Error)) return false;
	for (UObject* Asset : ResidentAssets)
	{
		if (!IsValid(Asset)) { Error = TEXT("preparation was given a missing native asset"); return false; }
		Assets.Add(FSoftObjectPath(Asset), Asset);
	}
	for (const auto& Path : Required)
		if (!Assets.Contains(Path)) { Error = TEXT("native model asset was not prepared: ") + Path.ToString(); return false; }
	// Validate every model before admitting any: a late batch that fails halfway leaves the
	// context exactly as it was, so a retry or a report sees one consistent answer.
	TArray<FString> Fresh;
	for (const FString& Id : ModelIds)
	{
		if (Admitted.Contains(Id) || Fresh.Contains(Id)) continue;
		if (!AdmitOne(Id, Error))
		{
			for (const FString& Undo : Fresh) { Views.Remove(Undo); Grids.Remove(Undo); Rigs.Remove(Undo); }
			Views.Remove(Id); Grids.Remove(Id); Rigs.Remove(Id);
			return false;
		}
		Fresh.Add(Id);
	}
	for (const FString& Id : Fresh) Admitted.Add(Id);
	return true;
}

bool FElysiumPreparedPropModels::AdmitOne(const FString& Id, FString& Error)
{
	const auto* Skin = Skins->Data.Models.Find(Id);
	if (Skin) for (const auto& Rep : Skin->Representations)
	{
		if ((!Rep.StaticMesh.IsNull() && !Cast<UStaticMesh>(FindResident(Rep.StaticMesh.ToSoftObjectPath(), Error)))
			|| (!Rep.SkeletalMesh.IsNull() && !Cast<USkeletalMesh>(FindResident(Rep.SkeletalMesh.ToSoftObjectPath(), Error))))
		{ Error = TEXT("prepared mesh has the wrong native type: ") + Id; return false; }
		for (const auto& Family : Rep.Families) for (const auto& Cell : Family.Cells)
			if (!IsValid(Cell.Material.Get())) { Error = TEXT("skin family has an unprepared hard material reference: ") + Id; return false; }
	}
	const auto* Row = Placed->FindModel(Id);
	if (!Row) return true; // A character/wield/ground skin owner need not be placed by a map.
	if (Row->AssetId != Id || !Row->AcceptanceIssues.IsEmpty()) { Error = TEXT("unaccepted placed model: ") + Id; return false; }
	if (Row->bSourceAbsent) return true;
	if (Row->StaticMesh.IsNull() && Row->SkeletalMesh.IsNull()) { Error = TEXT("placed model has no declared native geometry: ") + Id; return false; }
	if (!Row->StaticMesh.IsNull())
	{
		const auto* Rep = Representation(Skins, Id, TEXT("static"));
		if (!Rep || Rep->StaticMesh != Row->StaticMesh) { Error = TEXT("static catalogue references disagree: ") + Id; return false; }
	}
	if (!Row->SkeletalMesh.IsNull())
	{
		const auto* Rep = Representation(Skins, Id, TEXT("skeletal"));
		if (!Rep || Rep->SkeletalMesh != Row->SkeletalMesh) { Error = TEXT("skeletal catalogue references disagree: ") + Id; return false; }
		auto* Mesh = Cast<USkeletalMesh>(FindResident(Row->SkeletalMesh.ToSoftObjectPath(), Error));
		const auto* Record = ElysiumCharacterModel::Validate(Id, Mesh, Error);
		if (!Record) return false;
		if (Row->bHasCloth != (Record->SourceGarmentCount > 0) || Record->SourceGarmentCount < 0
			|| Record->ClothAssets.Num() != Record->SourceGarmentCount || Record->ClothAssets.Contains(nullptr))
		{ Error = TEXT("cloth owner has incomplete native garments: ") + Id; return false; }
		for (const auto& Cloth : Record->ClothAssets)
			if (!IsValid(Cloth.Get())) { Error = TEXT("native garment was not resident at preparation: ") + Id; return false; }
		if (Record->Composition.HasWork()) Rigs.Add(Id, MakeShared<FElysiumCompositionRig>(Record->Composition));
		const auto* Body = Cast<UElysiumBodyData>(FindResident(Row->BodyData.ToSoftObjectPath(), Error));
		if (!Body || Body->AssetId != Id || Row->BodyData.ToSoftObjectPath().ToString() != FElysiumContentPaths::BakedUnit(Id, TEXT("DA")))
		{ Error = TEXT("placed BodyData was not prepared for its owner: ") + Id; return false; }
	}
	else if (Row->bHasCloth) { Error = TEXT("cloth cannot use a static-only representation: ") + Id; return false; }
	for (const auto& Pair : Row->NativeSequences)
		if (Pair.Value.ToSoftObjectPath().ToString() != FElysiumContentPaths::BakedUnit(Id, TEXT("A"), FString(), Pair.Key)
			|| !Cast<UAnimSequence>(FindResident(Pair.Value.ToSoftObjectPath(), Error)))
		{ Error = TEXT("sequence not prepared at its canonical address: ") + Id + TEXT(" / ") + Pair.Key; return false; }
	auto Table = MakeShared<FElysiumBlendTable>(); Table->Stem = Id;
	for (const auto& Pair : Row->NativeBlendSpaces)
	{
		const auto* Space = Cast<UBlendSpace>(FindResident(Pair.Value.ToSoftObjectPath(), Error));
		const auto* Meta = Space ? Space->FindMetaDataByClass<UElysiumClipData>() : nullptr;
		if (!Meta || Meta->AssetId != Id || !Meta->bHasGrid
			|| Pair.Value.ToSoftObjectPath().ToString() != FElysiumContentPaths::BakedUnit(Id, TEXT("BS"), FString(), Pair.Key))
		{ Error = TEXT("native grid metadata absent/mismatched: ") + Id + TEXT(" / ") + Pair.Key; return false; }
		Table->Grids.Add(Pair.Key, Meta->Grid); Table->PoseParams = Meta->PoseParams;
	}
	Grids.Add(Id, Table);
	auto& View = Views.Add(Id); View.Stem = View.StaticStem = Id; View.Model = Row->ModelPath;
	View.bStaticEquivalent = Row->CanUseStatic(false);
	View.ClipMode = Row->bFullClipsRequired ? TEXT("full") : Row->RequiredClips.IsEmpty() ? TEXT("rest") : TEXT("required");
	for (const auto& Clip : Row->Clips)
	{
		if (Clip.Index != View.Clips.Num()) { Error = TEXT("placed clip index space differs: ") + Id; return false; }
		auto& Legacy = View.Clips.AddDefaulted_GetRef();
		Legacy.Name = Clip.Label; Legacy.Activity = Clip.Activity; Legacy.Weight = Clip.Weight; Legacy.Flags = Clip.Flags;
		Legacy.Index = Clip.Index; Legacy.Frames = Clip.Frames; Legacy.Fps = float(Clip.Fps); Legacy.BoundsRadiusMeters = float(Clip.BoundsRadiusCm / 100.);
	}
	for (int32 Index : Row->RestCandidates)
	{
		if (!Row->Clips.IsValidIndex(Index)) { Error = TEXT("invalid placed rest candidate: ") + Id; return false; }
		View.RestCandidates.Add(Row->Clips[Index].Label);
	}
	return true;
}

bool FElysiumPreparedPropModels::IsCurrent() const
{
	if (!IsInGameThread() || !Owner.IsValid()) return false;
	const auto* Found = Prepared.Find(OwnerKey);
	const auto Current = Found ? Found->Pin() : nullptr;
	return Current.Get() == this;
}

UObject* FElysiumPreparedPropModels::FindResident(const FSoftObjectPath& Path, FString& Error) const
{
	Error.Reset(); const auto* Found = Assets.Find(Path);
	if (Found && IsValid(Found->Get())) return Found->Get();
	Error = TEXT("native reference is not resident in this preparation: ") + Path.ToString(); return nullptr;
}

UObject* FElysiumPreparedPropModels::Resident(const FSoftObjectPath& Path, FString& Error) const
{
	if (!IsCurrent()) { Error = TEXT("model preparation is released or superseded"); return nullptr; }
	return FindResident(Path, Error);
}

const FElysiumCataloguePlacedModel* FElysiumPreparedPropModels::Model(const FString& Id, FString& Error) const
{
	Error.Reset();
	if (!IsCurrent() || !Admitted.Contains(Id)) { Error = TEXT("model was not admitted for the current owner/epoch: ") + Id; return nullptr; }
	const auto* Row = Placed->FindModel(Id);
	if (!Row) Error = TEXT("model has no placed catalogue row: ") + Id;
	else if (Row->bSourceAbsent) { Error = TEXT("placed source model is absent: ") + Id + TEXT(" / ") + Row->SourceReason; return nullptr; }
	return Row;
}

const FElysiumAnimatedPropEntry* FElysiumPreparedPropModels::CompatibilityView(const FString& Id) const { return IsCurrent() ? Views.Find(Id) : nullptr; }

const FElysiumCataloguePlacedClip* FElysiumPreparedPropModels::Clip(const FString& Id, const FString& Label, FString& Error) const
{
	const auto* Row = Model(Id, Error); if (!Row) return nullptr;
	const auto* Clip = Row->Clips.FindByPredicate([&](const auto& Value) { return Value.Label.Equals(Label, ESearchCase::IgnoreCase); });
	if (!Clip) Error = TEXT("model has no authored clip: ") + Id + TEXT(" / ") + Label;
	return Clip;
}

UStaticMesh* FElysiumPreparedPropModels::StaticMesh(const FString& Id, FString& Error) const
{
	if (!IsCurrent() || !Admitted.Contains(Id)) { Error = TEXT("static model was not admitted for this epoch: ") + Id; return nullptr; }
	const auto* Rep = Representation(Skins, Id, TEXT("static"));
	if (!Rep) { Error = TEXT("model has no native static representation: ") + Id; return nullptr; }
	return Cast<UStaticMesh>(Resident(Rep->StaticMesh.ToSoftObjectPath(), Error));
}

USkeletalMesh* FElysiumPreparedPropModels::SkeletalMesh(const FString& Id, FString& Error) const
{
	if (!IsCurrent() || !Admitted.Contains(Id)) { Error = TEXT("skeletal model was not admitted for this epoch: ") + Id; return nullptr; }
	const auto* Rep = Representation(Skins, Id, TEXT("skeletal"));
	if (!Rep) { Error = TEXT("model has no native skeletal representation: ") + Id; return nullptr; }
	return Cast<USkeletalMesh>(Resident(Rep->SkeletalMesh.ToSoftObjectPath(), Error));
}

UAnimSequence* FElysiumPreparedPropModels::Sequence(const FString& Id, const FString& Label, FString& Error, const FElysiumPoseParams& Pose) const
{
	const auto* C = Clip(Id, Label, Error); if (!C) return nullptr;
	if (C->State != TEXT("native")) { Error = TEXT("clip has no native animation representation: ") + Id + TEXT(" / ") + Label; return nullptr; }
	if (!C->BlendSpace.IsNull())
	{
		const auto* Table = Grids.Find(Id);
		const auto* Grid = Table ? (*Table)->Find(Label) : nullptr;
		if (!Grid) { Error = TEXT("prepared blend grid is absent: ") + Id + TEXT(" / ") + Label; return nullptr; }
		const auto Pick = ElysiumBlendGrids::SelectCell(*Grid, **Table, Pose);
		const auto* Row = Placed->FindModel(Id);
		const auto* Ref = Pick.Cell ? Named(Row->NativeSequences, Pick.Cell->Clip) : nullptr;
		if (!Ref) { Error = TEXT("blend grid selected an unprepared/absent cell: ") + Id + TEXT(" / ") + Label; return nullptr; }
		return Cast<UAnimSequence>(Resident(Ref->ToSoftObjectPath(), Error));
	}
	return Cast<UAnimSequence>(Resident(C->Sequence.ToSoftObjectPath(), Error));
}

TSharedPtr<const FElysiumCompositionRig> FElysiumPreparedPropModels::Composition(const FString& Id) const { return IsCurrent() ? Rigs.FindRef(Id) : nullptr; }

bool FElysiumPreparedPropModels::IsExplicitlyGeometryless(const FString& Id) const
{
	const auto* Row = IsCurrent() && Admitted.Contains(Id) ? Skins->Data.Models.Find(Id) : nullptr;
	return Row && Row->Representations.IsEmpty()
		&& Row->SourceOnlyReason == TEXT("geometryless source has skin index 0 but no texture slots");
}

void FElysiumPreparedPropModels::GatherResidentSequences(TSet<UAnimSequence*>& Out) const
{
	if (!IsCurrent()) return;
	for (const auto& Pair : Assets) if (auto* Sequence = Cast<UAnimSequence>(Pair.Value.Get())) Out.Add(Sequence);
}

bool FElysiumPreparedPropModels::ApplySkin(UMeshComponent* Component, const FString& Id, int32 Family, FString& Error) const
{
	Error.Reset();
	if (!IsCurrent() || !Component || Component->GetOwner() != Owner.Get() || !Admitted.Contains(Id))
	{ Error = TEXT("skin requires this epoch's admitted model/component owner: ") + Id; return false; }
	auto* Skeletal = Cast<USkeletalMeshComponent>(Component);
	auto* Static = Cast<UStaticMeshComponent>(Component);
	if ((!Skeletal && !Static) || (Skeletal && (!Skeletal->GetSkeletalMeshAsset() || Skeletal->GetSkeletalMeshAsset() != SkeletalMesh(Id, Error)))
		|| (Static && (!Static->GetStaticMesh() || Static->GetStaticMesh() != StaticMesh(Id, Error))))
	{ Error = TEXT("skin model/component reference mismatch: ") + Id; return false; }
	TArray<FElysiumCatalogueResolvedMaterial> Assignments;
	if (!Skins->ResolveMaterials(Id, Skeletal != nullptr, Family, Assignments, Error)) return false;
	if (Assignments.Num() != Component->GetNumMaterials()) { Error = TEXT("skin render-slot inventory differs: ") + Id; return false; }
	TArray<UMaterialInterface*> Previous;
	for (const auto& Assignment : Assignments)
	{
		if (!Assignment.Material || Component->GetMaterialIndex(Assignment.SlotName) != Assignment.SlotIndex)
		{ Error = TEXT("skin material/slot is not prepared: ") + Id; return false; }
		Previous.Add(Component->GetMaterial(Assignment.SlotIndex));
	}
	for (const auto& Assignment : Assignments) Component->SetMaterial(Assignment.SlotIndex, Assignment.Material);
	if (Skeletal && !ElysiumNpcVisual::SyncGarmentMaterials(Skeletal, Error))
	{
		for (int32 Index = 0; Index < Assignments.Num(); ++Index) Component->SetMaterial(Assignments[Index].SlotIndex, Previous[Index]);
		return false;
	}
	return true;
}

FElysiumPreparedPropBody ElysiumPreparedProps::Build(UElysiumEntityBodies& Factory,
	const FElysiumPlacedModelRequest& Request, bool bNeedsAnimation, FString& Error)
{
	FElysiumPreparedPropBody Result; Error.Reset(); Result.ModelId = ModelId(Request.ModelPath);
	const auto Ready = ForOwner(Factory.GetOwner());
	if (!Ready) { Error = TEXT("placed catalogues/assets were not prepared for the owner"); return Result; }
	const auto* Row = Ready->Model(Result.ModelId, Error); if (!Row) return Result;
	const bool bStatic = Row->CanUseStatic(bNeedsAnimation || Row->bFullClipsRequired || !Row->RequiredClips.IsEmpty());
	if (bStatic)
	{
		Result.StaticVisual = Request.Physics == EElysiumPlacedModelPhysics::None
			? Factory.BuildPropVisual(Result.ModelId, Request.Location, Request.Rotation, Request.UniformScale)
			: Factory.BuildPhysPropVisual(Result.ModelId, Request.Location, Request.Rotation, Request.UniformScale);
		Result.Visual = Result.Attach = Result.StaticVisual;
		if (Request.Physics != EElysiumPlacedModelPhysics::None) Result.PhysicsProxy = Result.StaticVisual;
	}
	else
	{
		Result.SkeletalVisual = Factory.BuildAnimatedPropVisual(Result.ModelId, Request.Location, Request.Rotation, Request.UniformScale, Request.PlacementToken);
		Result.Visual = Result.Attach = Result.SkeletalVisual;
		if (Result.SkeletalVisual && Request.Physics != EElysiumPlacedModelPhysics::None)
		{
			Result.PhysicsProxy = Factory.BuildPhysPropVisual(Result.ModelId, Request.Location, Request.Rotation, Request.UniformScale);
			if (Result.PhysicsProxy)
			{
				Result.PhysicsProxy->SetVisibility(false, true);
				Result.SkeletalVisual->AttachToComponent(Result.PhysicsProxy, FAttachmentTransformRules::KeepWorldTransform);
				Result.Attach = Result.PhysicsProxy;
			}
			else { DestroyVisual(Result.SkeletalVisual); Result.SkeletalVisual = nullptr; Result.Visual = Result.Attach = nullptr; }
		}
	}
	if (!Result.IsValid()) { Error = TEXT("native placed body construction failed: ") + Result.ModelId; return Result; }
	if (Result.PhysicsProxy && Request.Physics == EElysiumPlacedModelPhysics::CollisionProxy) Result.PhysicsProxy->SetSimulatePhysics(false);
	if (!Ready->ApplySkin(Cast<UMeshComponent>(Result.Visual), Result.ModelId, Request.Skin, Error))
	{
		if (Result.PhysicsProxy && Result.PhysicsProxy != Result.StaticVisual) Result.PhysicsProxy->DestroyComponent();
		DestroyVisual(Result.Visual); Result.Visual = Result.Attach = nullptr;
		Result.SkeletalVisual = nullptr; Result.StaticVisual = Result.PhysicsProxy = nullptr;
	}
	return Result;
}

void ElysiumPreparedProps::DestroyVisual(UPrimitiveComponent* Visual)
{
	if (!Visual) return;
	TArray<USceneComponent*> Children; Visual->GetChildrenComponents(true, Children);
	for (int32 Index = Children.Num() - 1; Index >= 0; --Index) if (Children[Index]) Children[Index]->DestroyComponent();
	Visual->DestroyComponent();
}
