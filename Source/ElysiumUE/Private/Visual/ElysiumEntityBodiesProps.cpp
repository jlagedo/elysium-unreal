#include "Visual/ElysiumEntityBodies.h"

#include "ElysiumContentPaths.h"
#include "ElysiumPropSkins.h"
#include "Visual/ElysiumBipedAnimInstance.h"
#include "Visual/ElysiumAnimSubsystem.h"
#include "Visual/ElysiumEntityBodiesLog.h"

#include "Animation/AnimSequence.h"
#if WITH_EDITOR
#include "Animation/IAnimationSequenceCompiler.h"
#endif
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

void UElysiumEntityBodies::LoadItemGroundModelCatalogue()
{
	if (bItemGroundModelsLoaded)
	{
		return;
	}
	bItemGroundModelsLoaded = true;

	FString Text;
	const FString Path = FElysiumContentPaths::ItemGroundModels();
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		UE_LOG(LogElysiumBodies, Warning,
			TEXT("item ground-model catalogue is missing: %s (run: uv run elysium export bundle items)"),
			*Path);
		return;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	FString Schema;
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()
		|| !Root->TryGetStringField(TEXT("schema"), Schema)
		|| Schema != TEXT("elysium.item-ground-models"))
	{
		UE_LOG(LogElysiumBodies, Warning,
			TEXT("item ground-model catalogue is invalid: %s"), *Path);
		return;
	}

	const TSharedPtr<FJsonObject>* Models = nullptr;
	if (!Root->TryGetObjectField(TEXT("models"), Models) || Models == nullptr)
	{
		UE_LOG(LogElysiumBodies, Warning,
			TEXT("item ground-model catalogue has no models table: %s"), *Path);
		return;
	}
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Models)->Values)
	{
		const TSharedPtr<FJsonObject>* Row = nullptr;
		double Faces = 0.0;
		if (!Pair.Value.IsValid() || !Pair.Value->TryGetObject(Row) || Row == nullptr
			|| !(*Row)->TryGetNumberField(TEXT("faces"), Faces))
		{
			UE_LOG(LogElysiumBodies, Warning,
				TEXT("item ground-model catalogue row '%s' has no face count"), *Pair.Key);
			ItemGroundModels.Add(Pair.Key, EElysiumItemGroundModelState::Unavailable);
			continue;
		}
		ItemGroundModels.Add(Pair.Key, Faces > 0.0
			? EElysiumItemGroundModelState::Geometry
			: EElysiumItemGroundModelState::Geometryless);
	}

	const TArray<TSharedPtr<FJsonValue>>* Skipped = nullptr;
	if (Root->TryGetArrayField(TEXT("skipped"), Skipped) && Skipped != nullptr)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Skipped)
		{
			const TSharedPtr<FJsonObject>* Row = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(Row) || Row == nullptr)
			{
				continue;
			}
			FString Model, Reason;
			if ((*Row)->TryGetStringField(TEXT("model"), Model))
			{
				(*Row)->TryGetStringField(TEXT("reason"), Reason);
				ItemGroundModels.Add(Model, EElysiumItemGroundModelState::Unavailable);
				UE_LOG(LogElysiumBodies, Warning,
					TEXT("item ground model '%s' is unavailable: %s"), *Model, *Reason);
			}
		}
	}
}

EElysiumItemGroundModelState UElysiumEntityBodies::ItemGroundModelState(const FString& ModelPath)
{
	LoadItemGroundModelCatalogue();
	FString Key = ModelPath.TrimStartAndEnd().ToLower().Replace(TEXT("\\"), TEXT("/"));
	if (!Key.EndsWith(TEXT(".mdl")))
	{
		Key += TEXT(".mdl");
	}
	if (const EElysiumItemGroundModelState* State = ItemGroundModels.Find(Key))
	{
		return *State;
	}
	if (!ReportedMissingItemGroundModels.Contains(Key))
	{
		ReportedMissingItemGroundModels.Add(Key);
		UE_LOG(LogElysiumBodies, Warning,
			TEXT("item ground model '%s' is absent from the generated catalogue"), *Key);
	}
	return EElysiumItemGroundModelState::Unavailable;
}

namespace ElysiumPropBounds
{
	bool ExtensionFor(const FBoxSphereBounds& Bind, double RadiusCm,
		FVector& OutPositive, FVector& OutNegative)
	{
		OutPositive = FVector::ZeroVector;
		OutNegative = FVector::ZeroVector;
		if (!(RadiusCm > 0.0))
		{
			return false;
		}
		bool bWidened = false;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			// The box has to contain [-R, +R] about the origin, and it is centred on `Bind.Origin`,
			// so each side carries the centre offset with its own sign.
			const double Positive = FMath::Max(0.0, RadiusCm - Bind.Origin[Axis] - Bind.BoxExtent[Axis]);
			const double Negative = FMath::Max(0.0, RadiusCm + Bind.Origin[Axis] - Bind.BoxExtent[Axis]);
			OutPositive[Axis] = Positive;
			OutNegative[Axis] = Negative;
			bWidened |= Positive > 0.0 || Negative > 0.0;
		}
		return bWidened;
	}
}

// A/B toggle for the prop skin pass (8.3/8.4). 1 applies alternate skin families; 0 leaves every
// prop on its authored materials, so a look change can be attributed. Read per apply, so it takes
// effect on the next Skin input without a reload.
static TAutoConsoleVariable<int32> CVarPropSkins(
	TEXT("elysium.PropSkins"), 1,
	TEXT("Apply alternate prop skin families (1, default) or keep every prop on skin 0 (0)."),
	ECVF_Default);

FString UElysiumEntityBodies::AnimatedPropStemForModel(const FString& ModelPath) const
{
	const AActor* Owner = GetOwner();
	UGameInstance* GI = Owner ? Owner->GetGameInstance() : nullptr;
	UElysiumAnimSubsystem* Anims = GI ? GI->GetSubsystem<UElysiumAnimSubsystem>() : nullptr;
	const FElysiumAnimatedPropEntry* Entry = Anims
		? (Anims->GetIndex().ManifestVersion >= 7
			? Anims->GetIndex().FindPlacedModel(ModelPath)
			: Anims->GetIndex().FindAnimatedProp(ModelPath)) : nullptr;
	return Entry ? Entry->Stem : FString();
}

bool UElysiumEntityBodies::HasPlacedModelCatalogue() const
{
	const AActor* Owner = GetOwner();
	UGameInstance* GI = Owner ? Owner->GetGameInstance() : nullptr;
	UElysiumAnimSubsystem* Anims = GI ? GI->GetSubsystem<UElysiumAnimSubsystem>() : nullptr;
	return Anims && Anims->GetIndex().ManifestVersion >= 7;
}

FElysiumPlacedModelBody UElysiumEntityBodies::BuildPlacedModelBody(
	const FElysiumPlacedModelRequest& Request)
{
	FElysiumPlacedModelBody Result;
	if (IConsoleVariable* Gate = IConsoleManager::Get().FindConsoleVariable(TEXT("elysium.PropBodies"));
		Gate && Gate->GetInt() == 0)
	{
		return Result;
	}
	Result.Stem = AnimatedPropStemForModel(Request.ModelPath);
	if (Result.Stem.IsEmpty())
	{
		UE_LOG(LogElysiumBodies, Error,
			TEXT("placed model '%s' is absent from npc_index v7"), *Request.ModelPath);
		return Result;
	}
	const FElysiumAnimatedPropEntry* Entry = FindAnimatedPropEntry(Result.Stem);
	const FString StaticStem = !Request.StaticStem.IsEmpty()
		? Request.StaticStem : (Entry ? Entry->StaticStem : FString());
	if (StaticStem.IsEmpty())
	{
		UE_LOG(LogElysiumBodies, Error,
			TEXT("placed model '%s' has no static material/collision stem"), *Request.ModelPath);
		return Result;
	}

	Result.Visual = BuildAnimatedPropVisualWithStaticStem(Result.Stem, StaticStem,
		Request.Location, Request.Rotation, Request.UniformScale, Request.PlacementToken);
	if (!Result.Visual)
	{
		return Result;
	}
	ApplyAnimatedPropSkin(Result.Visual, StaticStem, Request.Skin);

	if (Request.Physics != EElysiumPlacedModelPhysics::None)
	{
		Result.PhysicsProxy = BuildPhysPropVisual(StaticStem, Request.Location,
			Request.Rotation, Request.UniformScale);
		if (!Result.PhysicsProxy)
		{
			Result.Visual->DestroyComponent();
			Result.Visual = nullptr;
			return Result;
		}
		Result.PhysicsProxy->SetVisibility(false, true);
		if (Request.Physics == EElysiumPlacedModelPhysics::CollisionProxy)
		{
			Result.PhysicsProxy->SetSimulatePhysics(false);
		}
		Result.Visual->AttachToComponent(Result.PhysicsProxy,
			FAttachmentTransformRules::KeepWorldTransform);
		Result.Attach = Result.PhysicsProxy;
	}
	else
	{
		Result.Attach = Result.Visual;
	}
	return Result;
}

const FElysiumAnimatedPropEntry* UElysiumEntityBodies::FindAnimatedPropEntry(const FString& Stem) const
{
	const AActor* Owner = GetOwner();
	UGameInstance* GI = Owner ? Owner->GetGameInstance() : nullptr;
	UElysiumAnimSubsystem* Anims = GI ? GI->GetSubsystem<UElysiumAnimSubsystem>() : nullptr;
	if (!Anims)
	{
		return nullptr;
	}
	if (const FElysiumAnimatedPropEntry* Placed = Anims->GetIndex().PlacedModels.Find(Stem))
	{
		return Placed;
	}
	return Anims->GetIndex().AnimatedProps.Find(Stem);
}

FString UElysiumEntityBodies::AnimatedPropRestClip(const FString& Stem, int32 PlacementToken) const
{
	const FElysiumAnimatedPropEntry* Entry = FindAnimatedPropEntry(Stem);
	return Entry ? Entry->RestSequence(PlacementToken) : FString();
}

bool UElysiumEntityBodies::FindAnimatedPropClip(const FString& Stem, const FString& ClipName,
	bool& bOutLoops) const
{
	const FElysiumAnimatedPropEntry* Entry = FindAnimatedPropEntry(Stem);
	const FElysiumPropClip* Clip = Entry ? Entry->FindClip(ClipName) : nullptr;
	bOutLoops = Clip && Clip->IsLooping();
	return Clip != nullptr;
}

USkeletalMeshComponent* UElysiumEntityBodies::BuildAnimatedPropVisual(const FString& Stem,
	const FVector& Location, const FQuat& Rotation, float UniformScale, int32 PlacementToken)
{
	const FElysiumAnimatedPropEntry* Entry = FindAnimatedPropEntry(Stem);
	return BuildAnimatedPropVisualWithStaticStem(Stem,
		Entry ? Entry->StaticStem : FString(), Location, Rotation, UniformScale, PlacementToken);
}

USkeletalMeshComponent* UElysiumEntityBodies::BuildAnimatedPropVisualWithStaticStem(
	const FString& Stem, const FString& StaticStem, const FVector& Location,
	const FQuat& Rotation, float UniformScale, int32 PlacementToken)
{
	AActor* Owner = GetOwner();
	USceneComponent* Root = Owner ? Owner->GetRootComponent() : nullptr;
	UGameInstance* GI = Owner ? Owner->GetGameInstance() : nullptr;
	UElysiumAnimSubsystem* Anims = GI ? GI->GetSubsystem<UElysiumAnimSubsystem>() : nullptr;
	const FElysiumAnimatedPropEntry* Entry = FindAnimatedPropEntry(Stem);
	if (!Root || !Entry)
	{
		return nullptr;
	}

	USkeletalMesh* Mesh = nullptr;
	if (const TObjectPtr<USkeletalMesh>* Cached = AnimatedPropMeshCache.Find(Stem))
	{
		Mesh = Cached->Get();
	}
	if (!Mesh)
	{
		Mesh = LoadObject<USkeletalMesh>(nullptr,
			*FElysiumContentPaths::BakedPropSkeletalMesh(Stem));
		if (!Mesh)
		{
			UE_LOG(LogElysiumBodies, Warning,
				TEXT("animated prop '%s' is not on the baked mount -- run: uv run elysium export characters"),
				*Stem);
			return nullptr;
		}
		AnimatedPropMeshCache.Add(Stem, Mesh);

		// Widen the bind-pose bounds to the furthest reach of any clip this model owns, once, here —
		// see ElysiumPropBounds. The union rather than the playing clip's own radius: one mesh is
		// cached per stem and serves every prop standing that model, and the haven stake's five
		// entities play five different clips off this one asset. `GetImportedBounds` stays the true
		// bind pose; only the extended bounds move.
		double ReachCm = 0.0;
		for (const FElysiumPropClip& Clip : Entry->Clips)
		{
			// The container is centimetres by the `UE_` convention; the index states the reach in
			// metres, which is a magnitude and survives the change of basis unchanged.
			ReachCm = FMath::Max(ReachCm, static_cast<double>(Clip.BoundsRadiusMeters) * 100.0);
		}
		FVector Positive, Negative;
		if (ElysiumPropBounds::ExtensionFor(Mesh->GetImportedBounds(), ReachCm, Positive, Negative))
		{
			Mesh->SetPositiveBoundsExtension(Positive);
			Mesh->SetNegativeBoundsExtension(Negative);
			Mesh->CalculateExtendedBounds();
			UE_LOG(LogElysiumBodies, Verbose,
				TEXT("animated prop '%s': bounds %.0f -> %.0f cm for a %.0f cm clip reach"),
				*Stem, Mesh->GetImportedBounds().SphereRadius, Mesh->GetBounds().SphereRadius, ReachCm);
		}
	}

	USkeletalMeshComponent* Comp = NewObject<USkeletalMeshComponent>(Owner);
	Comp->SetCanEverAffectNavigation(false);
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetSkeletalMeshAsset(Mesh);
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Comp->SetVisibility(false, true);
	Comp->SetupAttachment(Root);
	Comp->SetRelativeLocationAndRotation(Location, Rotation);
	if (UniformScale != 1.0f)
	{
		Comp->SetRelativeScale3D(FVector(UniformScale));
	}
	// The NATIVE host, not the graph: a skeletal prop is a named clip and nothing else — no gait, no
	// activity, no selection is ever published for one — so it poses off the clip player alone and a
	// compiled locomotion machine would sit inert behind it.
	Comp->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	Comp->SetAnimInstanceClass(UElysiumBipedAnimInstance::StaticClass());
	Comp->RegisterComponent();
	Owner->AddInstanceComponent(Comp);
	// A skeletal prop declares the same two composition stages a character does — 19 of them carry
	// a rule table — so it takes the same install (CAP7.2).
	if (UElysiumBodyAnimInstance* Inst = Cast<UElysiumBodyAnimInstance>(Comp->GetAnimInstance()))
	{
		Inst->SetCompositionRig(Anims->GetAnimatedPropCompositionRig(Entry->Model));
	}

	const FString MaterialStem = StaticStem.IsEmpty() ? Stem : StaticStem;
	if (!BindMapMaterials(Comp, MaterialStem) && !StaticStem.IsEmpty())
	{
		UE_LOG(LogElysiumBodies, Error,
			TEXT("placed model '%s' has no map static mesh for its material slots"), *Stem);
		Comp->DestroyComponent();
		return nullptr;
	}

	const FString Rest = Entry->RestSequence(PlacementToken);
	if (Rest.IsEmpty() || !PlayAnimatedPropClip(Comp, Stem, Rest, false, nullptr)
		|| !SeekCinematicClip(Comp, 0.0f))
	{
		UE_LOG(LogElysiumBodies, Error,
			TEXT("placed model '%s' cannot install authored rest pose before visibility"), *Stem);
		Comp->DestroyComponent();
		return nullptr;
	}
	Comp->TickAnimation(0.0f, false);
	Comp->RefreshBoneTransforms();
	Comp->SetComponentTickEnabled(false);
	Comp->SetVisibility(true, true);
	return Comp;
}

UAnimSequence* UElysiumEntityBodies::ResolveAnimatedPropClip(USkeletalMesh* Mesh,
	const FString& Stem, const FString& ClipName)
{
	if (!Mesh || Stem.IsEmpty() || ClipName.IsEmpty())
	{
		return nullptr;
	}
	const AActor* Owner = GetOwner();
	UGameInstance* GI = Owner ? Owner->GetGameInstance() : nullptr;
	UElysiumAnimSubsystem* Anims = GI ? GI->GetSubsystem<UElysiumAnimSubsystem>() : nullptr;
	const FElysiumAnimatedPropEntry* Entry = FindAnimatedPropEntry(Stem);
	if (!Entry || !Entry->HasClip(ClipName))
	{
		return nullptr;
	}

	// CAP7.3 — collapse a grid label to its cell BEFORE the key is built, or the label would cache one
	// cell forever and no pose parameter could ever move it. `wolf_form` is the one skeletal prop
	// declaring a grid; every other prop label resolves to itself.
	const FString AnimName = Anims->ResolveGridClip(Stem, ClipName);
	const FString Key = Stem + TEXT("|") + AnimName.ToLower();
	UAnimSequence* Anim = nullptr;
	if (const TObjectPtr<UAnimSequence>* Cached = AnimatedPropAnimCache.Find(Key))
	{
		Anim = Cached->Get();
	}
	else
	{
		Anim = LoadObject<UAnimSequence>(nullptr,
			*FElysiumContentPaths::BakedPropAnim(Stem, AnimName));
		if (!Anim)
		{
			UE_LOG(LogElysiumBodies, Warning,
				TEXT("animated prop '%s' clip '%s' (%s) is not on the baked mount"),
				*Stem, *ClipName, *AnimName);
		}
		AnimatedPropAnimCache.Add(Key, Anim);
	}
	return Anim;
}

bool UElysiumEntityBodies::PlayAnimatedPropClip(USkeletalMeshComponent* Body, const FString& Stem,
	const FString& ClipName, bool bLoop, float* OutSeconds)
{
	UAnimSequence* Anim = Body
		? ResolveAnimatedPropClip(Body->GetSkeletalMeshAsset(), Stem, ClipName)
		: nullptr;
	if (Anim == nullptr)
	{
		return false;
	}
	Body->SetComponentTickEnabled(true);
	if (OutSeconds)
	{
		*OutSeconds = Anim->GetPlayLength();
	}
	if (UElysiumBipedAnimInstance* Inst = Cast<UElysiumBipedAnimInstance>(Body->GetAnimInstance()))
	{
		Inst->PlayClip(Anim, bLoop);
	}
	else
	{
		Body->PlayAnimation(Anim, bLoop);
	}
	return true;
}

int32 UElysiumEntityBodies::PreloadAnimatedPropClips(USkeletalMeshComponent* Body,
	const FString& Stem)
{
	if (!Body || Stem.IsEmpty())
	{
		return 0;
	}
	const FElysiumAnimatedPropEntry* Entry = FindAnimatedPropEntry(Stem);
	USkeletalMesh* Mesh = Body->GetSkeletalMeshAsset();
	if (!Entry || !Mesh)
	{
		return 0;
	}
	int32 Resolved = 0;
	for (const FElysiumPropClip& Clip : Entry->Clips)
	{
		Resolved += ResolveAnimatedPropClip(Mesh, Stem, Clip.Name) != nullptr ? 1 : 0;
	}
	return Resolved;
}

int32 UElysiumEntityBodies::FinishAnimationPreload()
{
	TSet<UAnimSequence*> Unique;
	for (const TPair<FString, TObjectPtr<UAnimSequence>>& Pair : NpcAnimCache)
	{
		if (Pair.Value)
		{
			Unique.Add(Pair.Value.Get());
		}
	}
	for (const TPair<FString, TObjectPtr<UAnimSequence>>& Pair : AnimatedPropAnimCache)
	{
		if (Pair.Value)
		{
			Unique.Add(Pair.Value.Get());
		}
	}
	TArray<UAnimSequence*> Sequences = Unique.Array();
#if WITH_EDITOR
	if (!Sequences.IsEmpty())
	{
		UE::Anim::IAnimSequenceCompilingManager::FinishCompilation(Sequences);
	}
#endif
	return Sequences.Num();
}

bool UElysiumEntityBodies::BindMapMaterials(USkeletalMeshComponent* Comp, const FString& StaticStem)
{
	if (!Comp || StaticStem.IsEmpty())
	{
		return false;
	}
	UStaticMesh* StaticMesh = ResolvePropMesh(StaticStem);
	if (!StaticMesh)
	{
		return false;
	}

	TArray<FString> Unbound;
	for (const FStaticMaterial& StaticMaterial : StaticMesh->GetStaticMaterials())
	{
		const int32 Slot = Comp->GetMaterialIndex(StaticMaterial.MaterialSlotName);
		if (Slot == INDEX_NONE || StaticMaterial.MaterialInterface == nullptr)
		{
			Unbound.Add(StaticMaterial.MaterialSlotName.ToString());
			continue;
		}
		Comp->SetMaterial(Slot, StaticMaterial.MaterialInterface);
	}
	// A slot the skeletal body does not carry, or a static twin slot the bake left empty, draws the
	// engine default — reported once per stem rather than once per body and per skin change.
	if (!Unbound.IsEmpty() && !ReportedUnboundMaterialStems.Contains(StaticStem))
	{
		ReportedUnboundMaterialStems.Add(StaticStem);
		UE_LOG(LogElysiumBodies, Warning,
			TEXT("placed model '%s': %d of %d static material slot(s) bind nothing on the skeletal body [%s]"),
			*StaticStem, Unbound.Num(), StaticMesh->GetStaticMaterials().Num(),
			*FString::Join(Unbound, TEXT(", ")));
	}
	return true;
}

void UElysiumEntityBodies::ApplyAnimatedPropSkin(USkeletalMeshComponent* Comp,
	const FString& Stem, int32 Family)
{
	if (!Comp || CVarPropSkins.GetValueOnGameThread() == 0)
	{
		return;
	}
	// One skin table for the whole corpus: a model's alternate families and the materials they
	// repaint are both properties of the install, so a stage world reads the same one a map does.
	if (!bPropSkinsLoaded)
	{
		bPropSkinsLoaded = true;
		PropSkins = LoadObject<UElysiumPropSkinSet>(
			nullptr, *FElysiumContentPaths::BakedPropSkins());
	}
	// A skeletal prop's authored surfaces are overrides copied off its static twin, so clearing them
	// drops the body onto the skeletal asset's own neutral materials. Re-bind that base first and let
	// the family's repaints layer over it, which is also what restores skin 0 and any family this
	// model does not carry.
	Comp->EmptyOverrideMaterials();
	BindMapMaterials(Comp, Stem);
	const FElysiumSkinFamily* Row = PropSkins ? PropSkins->Find(FName(*Stem), Family) : nullptr;
	if (!Row)
	{
		return;
	}
	for (const FElysiumSkinOverride& Override : Row->Overrides)
	{
		const int32 Slot = Comp->GetMaterialIndex(Override.SlotName);
		if (Override.Material && Slot != INDEX_NONE)
		{
			Comp->SetMaterial(Slot, Override.Material);
		}
	}
}

// The bake already produced each BSP submodel as a local-pivot asset, so the moving collision body
// can carry the same authored surfaces without rebuilding render geometry at runtime. Cached per
// stem; a failed load is not cached, so it can retry after a bake.
UStaticMesh* UElysiumEntityBodies::ResolveBrushMesh(const FString& Stem)
{
	const TObjectPtr<UStaticMesh>* Cached = BrushMeshCache.Find(Stem);
	if (UStaticMesh* Mesh = Cached ? Cached->Get() : nullptr)
	{
		return Mesh;
	}
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(
		nullptr, *FElysiumContentPaths::BakedBrushMesh(MapName, Stem));
	if (!Mesh)
	{
		UE_LOG(LogElysiumBodies, Warning,
			TEXT("brush '%s': no baked mesh (run: uv run elysium export map %s --force)"), *Stem, *MapName);
		return nullptr;
	}
	BrushMeshCache.Add(Stem, Mesh);
	return Mesh;
}

UStaticMeshComponent* UElysiumEntityBodies::BuildBrushVisual(const FString& Stem,
	USceneComponent* ParentBody, float UniformScale, bool bSky)
{
	AActor* Owner = GetOwner();
	if (!Owner || !ParentBody || Stem.IsEmpty())
	{
		return nullptr;
	}
	UStaticMesh* Mesh = ResolveBrushMesh(Stem);
	if (!Mesh)
	{
		return nullptr;
	}

	UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(Owner);
	Comp->SetCanEverAffectNavigation(false);
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetStaticMesh(Mesh);
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Comp->SetupAttachment(ParentBody);
	Comp->SetRelativeLocationAndRotation(FVector::ZeroVector, FQuat::Identity);
	if (UniformScale != 1.f)
	{
		Comp->SetRelativeScale3D(FVector(UniformScale));
	}
	if (bSky)
	{
		Comp->SetCastShadow(false);
		Comp->SetVisibleInRayTracing(false);
	}
	Comp->RegisterComponent();
	Owner->AddInstanceComponent(Comp);
	return Comp;
}

UStaticMesh* UElysiumEntityBodies::ResolvePropMesh(const FString& Stem)
{
	const TObjectPtr<UStaticMesh>* Cached = PropMeshCache.Find(Stem);
	if (UStaticMesh* Mesh = Cached ? Cached->Get() : nullptr)
	{
		return Mesh;
	}
	// One mesh per model, wherever it stands: a prop, an item's ground body and a piece of map
	// dressing are all the same static model, so they are all this one asset.
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *FElysiumContentPaths::BakedPropMesh(Stem));
	if (Mesh == nullptr)
	{
		UE_LOG(LogElysiumBodies, Warning,
			TEXT("prop '%s': no baked mesh (run: uv run elysium export bundle corpus)"), *Stem);
		return nullptr;
	}
	PropMeshCache.Add(Stem, Mesh);
	return Mesh;
}

UStaticMeshComponent* UElysiumEntityBodies::BuildPropVisual(const FString& Stem, const FVector& Location,
	const FQuat& Rotation, float UniformScale)
{
	AActor* Owner = GetOwner();
	USceneComponent* Root = Owner ? Owner->GetRootComponent() : nullptr;
	if (Stem.IsEmpty() || Root == nullptr)
	{
		return nullptr;
	}

	UStaticMesh* Mesh = ResolvePropMesh(Stem);
	if (Mesh == nullptr)
	{
		return nullptr;
	}

	// Standard runtime-component recipe (mirrors BuildNpcVisual): NewObject → mesh → attach → place →
	// register. The map actor sits at the origin, so relative == world for these Unreal-space values.
	UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(Owner);
	Comp->SetCanEverAffectNavigation(false);
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetStaticMesh(Mesh);
	// prop_dynamic is visual-only (8.3); prop_physics owns collision (8.4). The baked mesh carries
	// collision geometry for the props that do need it, so it is switched off here per component.
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Comp->SetupAttachment(Root);
	Comp->SetRelativeLocationAndRotation(Location, Rotation);
	// B7 — a 3D-skybox body is the miniature at its own scale: scenery the player can never
	// reach, so it is never solid, casts nothing, and stays out of the ray-tracing scene (a mesh
	// blown up 16x overlaps the whole playable space, the canonical HWRT overlap cost).
	if (UniformScale != 1.f)
	{
		Comp->SetRelativeScale3D(FVector(UniformScale));
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetCastShadow(false);
		Comp->SetVisibleInRayTracing(false);
	}
	Comp->RegisterComponent();
	Owner->AddInstanceComponent(Comp);
	return Comp;
}

void UElysiumEntityBodies::ApplyPropSkin(UStaticMeshComponent* Comp, const FString& Stem, int32 Family)
{
	UStaticMesh* Mesh = Comp ? Comp->GetStaticMesh() : nullptr;
	if (!Mesh || CVarPropSkins.GetValueOnGameThread() == 0)
	{
		return;
	}

	if (!bPropSkinsLoaded)
	{
		bPropSkinsLoaded = true;
		PropSkins = LoadObject<UElysiumPropSkinSet>(
			nullptr, *FElysiumContentPaths::BakedPropSkins());
	}

	// Restore first, so a swap back to skin 0 -- or to a family this model does not carry, which
	// Source draws as the authored set -- undoes whatever the previous skin painted. Clearing the
	// overrides puts every slot back on the mesh's own material.
	Comp->EmptyOverrideMaterials();
	const FElysiumSkinFamily* Row = PropSkins ? PropSkins->Find(FName(*Stem), Family) : nullptr;
	if (!Row)
	{
		return;
	}

	for (const FElysiumSkinOverride& Override : Row->Overrides)
	{
		if (!Override.Material)
		{
			continue;
		}
		// Every prop body stands a baked mesh, whose slots the bake named safe_name(material) --
		// the same key the skin table is written with, so the slot name resolves directly.
		const int32 Slot = Mesh->GetMaterialIndex(Override.SlotName);
		if (Slot != INDEX_NONE)
		{
			Comp->SetMaterial(Slot, Override.Material);
		}
	}
}

UStaticMeshComponent* UElysiumEntityBodies::BuildPhysPropVisual(const FString& Stem, const FVector& Location,
	const FQuat& Rotation, float UniformScale)
{
	AActor* Owner = GetOwner();
	USceneComponent* Root = Owner ? Owner->GetRootComponent() : nullptr;
	if (Stem.IsEmpty() || Root == nullptr)
	{
		return nullptr;
	}

	// The same baked asset every other prop stands: the bake gave a physics model VtMB's own
	// convex collision (props/<stem>.phys, one shape per `.phy` ledge) and its authored mass on
	// the body setup, under CTF_UseSimpleAndComplex — so one mesh serves both a simulating body
	// and a static placement of the same model, and the cache is shared with BuildPropVisual.
	UStaticMesh* Mesh = ResolvePropMesh(Stem);
	if (Mesh == nullptr)
	{
		return nullptr;
	}

	UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(Owner);
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetStaticMesh(Mesh);
	Comp->SetupAttachment(Root);
	Comp->SetRelativeLocationAndRotation(Location, Rotation);
	// Collide as a physics body (blocks the world's BlockAll hull colliders). Simulation, mass and
	// the elysium.PhysicsProps gate are the leaf's call — the body stands here inert until it decides.
	Comp->SetCollisionProfileName(TEXT("PhysicsActor"));
	// B7 — a 3D-skybox body is the miniature at its own scale: scenery the player can never
	// reach, so it is never solid, casts nothing, and stays out of the ray-tracing scene (a mesh
	// blown up 16x overlaps the whole playable space, the canonical HWRT overlap cost).
	if (UniformScale != 1.f)
	{
		Comp->SetRelativeScale3D(FVector(UniformScale));
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetCastShadow(false);
		Comp->SetVisibleInRayTracing(false);
	}
	Comp->RegisterComponent();
	Owner->AddInstanceComponent(Comp);
	return Comp;
}
