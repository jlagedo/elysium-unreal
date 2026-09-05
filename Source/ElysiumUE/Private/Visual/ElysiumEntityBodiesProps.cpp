#include "Visual/ElysiumEntityBodies.h"

#include "ElysiumContentPaths.h"
#include "ElysiumFog.h"          // ElysiumLightStyle::StampUnstyled -- CPD slot 6 neutral
#include "Visual/ElysiumPreparedPropModels.h"
#include "Visual/ElysiumNpcVisual.h"
#include "ElysiumCharacterProvenance.h"
#include "Visual/ElysiumBipedAnimInstance.h"
#include "Visual/ElysiumAnimSubsystem.h"
#include "Visual/ElysiumEntityBodiesLog.h"

#include "Animation/AnimSequence.h"
#if WITH_EDITOR
#include "Animation/IAnimationSequenceCompiler.h"
#endif
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"

void UElysiumEntityBodies::LoadItemGroundModelCatalogue()
{
	// Transitional declaration only: preparation owns the catalogue, and this never loads.
	if (!ElysiumPreparedProps::ForOwner(GetOwner()))
		ReportNativeModelFailure(TEXT("ground-catalogue"), TEXT("native model catalogues/assets were not prepared"));
}

EElysiumItemGroundModelState UElysiumEntityBodies::ItemGroundModelState(const FString& ModelPath)
{
	const FString Id = ElysiumPreparedProps::ModelId(ModelPath);
	const auto Ready = ElysiumPreparedProps::ForOwner(GetOwner());
	FString Error;
	if (Ready && !Id.IsEmpty())
	{
		if (Ready->IsExplicitlyGeometryless(Id)) return EElysiumItemGroundModelState::Geometryless;
		if (Ready->StaticMesh(Id, Error)) return EElysiumItemGroundModelState::Geometry;
	}
	else Error = TEXT("ground model requires a canonical identity and prepared native references");
	ReportNativeModelFailure(TEXT("ground:") + ModelPath, Error);
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

FString UElysiumEntityBodies::AnimatedPropStemForModel(const FString& ModelPath) const
{
	// Signature retained; the returned value is now the full model ID.
	const FString Id = ElysiumPreparedProps::ModelId(ModelPath);
	const auto Ready = ElysiumPreparedProps::ForOwner(GetOwner());
	FString Error;
	if (Ready && !Id.IsEmpty() && Ready->Model(Id, Error)) return Id;
	ReportNativeModelFailure(TEXT("placed:") + ModelPath, Error.IsEmpty() ? TEXT("placed model identity/catalogue is not prepared") : Error);
	return FString();
}

bool UElysiumEntityBodies::HasPlacedModelCatalogue() const
{
	return ElysiumPreparedProps::ForOwner(GetOwner()).IsValid();
}

FElysiumPlacedModelBody UElysiumEntityBodies::BuildPlacedModelBody(const FElysiumPlacedModelRequest& Request)
{
	FElysiumPlacedModelBody Result;
	const auto Ready = ElysiumPreparedProps::ForOwner(GetOwner());
	FString Error; const FString Id = ElysiumPreparedProps::ModelId(Request.ModelPath);
	const auto* Row = Ready ? Ready->Model(Id, Error) : nullptr;
	if (!Row)
	{
		ReportNativeModelFailure(TEXT("placed-body:") + Request.ModelPath, Error.IsEmpty() ? TEXT("placed model was not prepared") : Error);
		return Result;
	}
	if (Row->CanUseStatic(Row->bFullClipsRequired || !Row->RequiredClips.IsEmpty()))
	{
		// The shared result's Visual is USkeletalMeshComponent*. A static body created here
		// would be orphaned from the entity's skin/state/cleanup pointers.
		ReportNativeModelFailure(TEXT("placed-result:") + Id,
			TEXT("static placement requires the main embodiment adapter to consume ElysiumPreparedProps::Build's general result"));
		return Result;
	}
	const auto Built = ElysiumPreparedProps::Build(*this, Request, false, Error);
	if (!Built.IsValid())
	{
		ReportNativeModelFailure(TEXT("placed-body:") + Id, Error);
		return Result;
	}
	Result.Stem = Built.ModelId; Result.Visual = Built.SkeletalVisual;
	Result.Attach = Built.Attach; Result.PhysicsProxy = Built.PhysicsProxy;
	return Result;
}

const FElysiumAnimatedPropEntry* UElysiumEntityBodies::FindAnimatedPropEntry(const FString& Name) const
{
	const auto Ready = ElysiumPreparedProps::ForOwner(GetOwner());
	return Ready ? Ready->CompatibilityView(ElysiumPreparedProps::ModelId(Name)) : nullptr;
}

FString UElysiumEntityBodies::AnimatedPropRestClip(const FString& Name, int32 PlacementToken) const
{
	const auto Ready = ElysiumPreparedProps::ForOwner(GetOwner()); FString Error;
	const auto* Row = Ready ? Ready->Model(ElysiumPreparedProps::ModelId(Name), Error) : nullptr;
	if (!Row)
	{
		ReportNativeModelFailure(TEXT("prop-rest:") + Name, Error.IsEmpty() ? TEXT("placed model was not prepared") : Error);
		return FString();
	}
	const auto* Clip = Row->SelectRest(PlacementToken);
	return Clip ? Clip->Label : FString();
}

bool UElysiumEntityBodies::FindAnimatedPropClip(const FString& Name, const FString& ClipName, bool& bOutLoops) const
{
	const auto Ready = ElysiumPreparedProps::ForOwner(GetOwner()); FString Error;
	const auto* Clip = Ready ? Ready->Clip(ElysiumPreparedProps::ModelId(Name), ClipName, Error) : nullptr;
	bOutLoops = Clip && (Clip->Flags & 1) != 0;
	return Clip != nullptr;
}

USkeletalMeshComponent* UElysiumEntityBodies::BuildAnimatedPropVisual(const FString& Name,
	const FVector& Location, const FQuat& Rotation, float UniformScale, int32 PlacementToken)
{
	const FString Id = ElysiumPreparedProps::ModelId(Name);
	return BuildAnimatedPropVisualWithStaticStem(Id, Id, Location, Rotation, UniformScale, PlacementToken);
}

USkeletalMeshComponent* UElysiumEntityBodies::BuildAnimatedPropVisualWithStaticStem(
	const FString& Name, const FString& StaticStem, const FVector& Location,
	const FQuat& Rotation, float UniformScale, int32 PlacementToken)
{
	// StaticStem was a derived material/collision address; the source ID now owns both.
	const FString Id = ElysiumPreparedProps::ModelId(Name);
	const auto Ready = ElysiumPreparedProps::ForOwner(GetOwner());
	AActor* Owner = GetOwner(); USceneComponent* Root = Owner ? Owner->GetRootComponent() : nullptr;
	FString Error;
	const auto* Row = Ready ? Ready->Model(Id, Error) : nullptr;
	USkeletalMesh* Mesh = Row ? Ready->SkeletalMesh(Id, Error) : nullptr;
	if (!Root || !Row || !Mesh)
	{
		ReportNativeModelFailure(TEXT("prop-mesh:") + Name, Error.IsEmpty() ? TEXT("owner root or prepared skeletal representation is absent") : Error);
		return nullptr;
	}
	const auto* Provenance = UElysiumCharacterProvenance::Find(Mesh);
	if (!Provenance || !Provenance->bHasMeshData || Provenance->AssetId != Id)
	{
		ReportNativeModelFailure(TEXT("prop-provenance:") + Id, TEXT("prepared mesh has no matching cooked model data"));
		return nullptr;
	}
	if (AnimatedPropMeshCache.FindRef(Id).Get() != Mesh)
	{
		double ReachCm = 0.;
		for (const auto& Clip : Row->Clips) ReachCm = FMath::Max(ReachCm, Clip.BoundsRadiusCm);
		FVector Positive, Negative;
		if (ElysiumPropBounds::ExtensionFor(Mesh->GetImportedBounds(), ReachCm, Positive, Negative))
		{
			Mesh->SetPositiveBoundsExtension(Positive); Mesh->SetNegativeBoundsExtension(Negative);
			Mesh->CalculateExtendedBounds();
		}
		AnimatedPropMeshCache.Add(Id, Mesh);
	}
	USkeletalMeshComponent* Comp = NewObject<USkeletalMeshComponent>(Owner);
	Comp->SetCanEverAffectNavigation(false); Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetSkeletalMeshAsset(Mesh); ElysiumLightStyle::StampUnstyled(Comp);
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision); Comp->SetVisibility(false, true);
	Comp->SetupAttachment(Root); Comp->SetRelativeLocationAndRotation(Location, Rotation);
	if (UniformScale != 1.f) Comp->SetRelativeScale3D(FVector(UniformScale));
	Comp->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	Comp->SetAnimInstanceClass(UElysiumBipedAnimInstance::StaticClass());
	Comp->RegisterComponent(); Owner->AddInstanceComponent(Comp);
	if (auto* Inst = Cast<UElysiumBodyAnimInstance>(Comp->GetAnimInstance())) Inst->SetCompositionRig(Ready->Composition(Id));
	else Error = TEXT("native prop animation host did not initialize");
	const auto* Rest = Row->SelectRest(PlacementToken);
	if (Error.IsEmpty() && Rest && (!PlayAnimatedPropClip(Comp, Id, Rest->Label, false, nullptr) || !SeekCinematicClip(Comp, 0.f)))
		Error = TEXT("authored rest pose could not be installed");
	if (Error.IsEmpty() && !Rest && !Row->Clips.IsEmpty()) Error = TEXT("authored clip vocabulary has no rest candidate");
	if (Error.IsEmpty() && !Ready->ApplySkin(Comp, Id, 0, Error) && Error.IsEmpty())
		Error = TEXT("native base skin assignment failed");
	if (!Error.IsEmpty())
	{
		ReportNativeModelFailure(TEXT("prop-install:") + Id, Error);
		ElysiumPreparedProps::DestroyVisual(Comp); return nullptr;
	}
	Comp->TickAnimation(0.f, false); Comp->RefreshBoneTransforms(); Comp->SetComponentTickEnabled(false);
	if (Row->bHasCloth)
	{
		// Preparation proved complete cooked provenance, so the garment host cannot enter
		// its legacy loading branch. Keep one existing cloth/material installation authority.
		if (!ElysiumNpcVisual::InstallGarment(Comp, Id) || !ElysiumNpcVisual::SyncGarmentMaterials(Comp, Error))
		{
			ReportNativeModelFailure(TEXT("prop-cloth:") + Id, Error.IsEmpty() ? TEXT("native garment installation failed") : Error);
			ElysiumPreparedProps::DestroyVisual(Comp); return nullptr;
		}
	}
	Comp->SetVisibility(true, true); ElysiumNpcVisual::GateLeaderCloth(Comp, true);
	return Comp;
}

UAnimSequence* UElysiumEntityBodies::ResolveAnimatedPropClip(USkeletalMesh* Mesh, const FString& Name, const FString& ClipName)
{
	const auto Ready = ElysiumPreparedProps::ForOwner(GetOwner()); FString Error;
	const FString Id = ElysiumPreparedProps::ModelId(Name);
	if (!Ready || !Mesh || Ready->SkeletalMesh(Id, Error) != Mesh)
	{
		ReportNativeModelFailure(TEXT("prop-animation:") + Name, TEXT("animation requires this epoch's prepared model mesh")); return nullptr;
	}
	UAnimSequence* Sequence = Ready->Sequence(Id, ClipName, Error);
	if (!Sequence) ReportNativeModelFailure(TEXT("prop-animation:") + Id + TEXT("|") + ClipName, Error);
	return Sequence;
}

bool UElysiumEntityBodies::PlayAnimatedPropClip(USkeletalMeshComponent* Body, const FString& Name,
	const FString& ClipName, bool bLoop, float* OutSeconds)
{
	if (OutSeconds) *OutSeconds = 0.f;
	UAnimSequence* Anim = Body ? ResolveAnimatedPropClip(Body->GetSkeletalMeshAsset(), Name, ClipName) : nullptr;
	if (!Anim) return false;
	auto* Inst = Cast<UElysiumBipedAnimInstance>(Body->GetAnimInstance());
	if (!Inst)
	{
		ReportNativeModelFailure(TEXT("prop-host:") + Name, TEXT("prepared prop requires its native animation host")); return false;
	}
	Body->SetComponentTickEnabled(true);
	if (OutSeconds) *OutSeconds = Anim->GetPlayLength();
	Inst->PlayClip(FElysiumClipIdentity(ElysiumPreparedProps::ModelId(Name), ClipName), Anim, bLoop);
	return true;
}

int32 UElysiumEntityBodies::PreloadAnimatedPropClips(USkeletalMeshComponent* Body, const FString& Name)
{
	// Presence validation only. Main's preparation handle owns all actual loading.
	const auto Ready = ElysiumPreparedProps::ForOwner(GetOwner()); FString Error;
	const FString Id = ElysiumPreparedProps::ModelId(Name);
	const auto* Row = Ready ? Ready->Model(Id, Error) : nullptr;
	if (!Row || !Body || Ready->SkeletalMesh(Id, Error) != Body->GetSkeletalMeshAsset()) return 0;
	int32 Resolved = 0;
	for (const auto& Clip : Row->Clips) Resolved += ResolveAnimatedPropClip(Body->GetSkeletalMeshAsset(), Id, Clip.Label) != nullptr;
	return Resolved;
}

int32 UElysiumEntityBodies::FinishAnimationPreload()
{
	TSet<UAnimSequence*> Unique;
	for (const auto& Pair : NpcAnimCache) if (Pair.Value) Unique.Add(Pair.Value.Get());
	if (const auto Ready = ElysiumPreparedProps::ForOwner(GetOwner())) Ready->GatherResidentSequences(Unique);
	TArray<UAnimSequence*> Sequences = Unique.Array();
#if WITH_EDITOR
	if (!Sequences.IsEmpty()) UE::Anim::IAnimSequenceCompilingManager::FinishCompilation(Sequences);
#endif
	return Sequences.Num();
}

bool UElysiumEntityBodies::BindMapMaterials(USkeletalMeshComponent* Comp, const FString& Name)
{
	// Transitional declaration only: base slots now come from the native skeletal skin row.
	const auto Ready = ElysiumPreparedProps::ForOwner(GetOwner()); FString Error;
	if (Ready && Ready->ApplySkin(Comp, ElysiumPreparedProps::ModelId(Name), 0, Error)) return true;
	ReportNativeModelFailure(TEXT("prop-base-materials:") + Name, Error.IsEmpty() ? TEXT("native skin catalogue was not prepared") : Error);
	return false;
}

void UElysiumEntityBodies::ApplyAnimatedPropSkin(USkeletalMeshComponent* Comp, const FString& Name, int32 Family)
{
	const auto Ready = ElysiumPreparedProps::ForOwner(GetOwner()); FString Error;
	if (Ready && Ready->ApplySkin(Comp, ElysiumPreparedProps::ModelId(Name), Family, Error)) return;
	ReportNativeModelFailure(TEXT("prop-skin:") + Name, Error.IsEmpty() ? TEXT("native skin catalogue was not prepared") : Error);
}

// The bake already produced each BSP submodel as a local-pivot asset, so the moving collision body
// can carry the same authored surfaces without rebuilding render geometry at runtime. Cached per
// stem; a failed load is not cached, so it can retry after a bake.
UStaticMesh* UElysiumEntityBodies::ResolveBrushMesh(const FString& Stem)
{
	const auto Ready = ElysiumPreparedProps::ForOwner(GetOwner()); FString Error;
	const FSoftObjectPath Path(FElysiumContentPaths::BakedBrushMesh(MapName, Stem));
	UStaticMesh* Mesh = Ready ? Cast<UStaticMesh>(Ready->Resident(Path, Error)) : nullptr;
	if (!Mesh) ReportNativeModelFailure(TEXT("brush:") + Stem, Error.IsEmpty() ? TEXT("native brush reference was not prepared") : Error);
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
	// R7.4 (G6): a brush body binds the map's own V2 instances, whose masters multiply the lit base
	// colour and the emissive by custom primitive data slot 6. The bake stamps that slot on the
	// components IT places; this one is made here, so it is stamped here or it renders black.
	ElysiumLightStyle::StampUnstyled(Comp);
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

UStaticMesh* UElysiumEntityBodies::ResolvePropMesh(const FString& Name)
{
	const auto Ready = ElysiumPreparedProps::ForOwner(GetOwner()); FString Error;
	const FString Id = ElysiumPreparedProps::ModelId(Name);
	UStaticMesh* Mesh = Ready && !Id.IsEmpty() ? Ready->StaticMesh(Id, Error) : nullptr;
	if (!Mesh) ReportNativeModelFailure(TEXT("prop-static:") + Name, Error.IsEmpty() ? TEXT("canonical model ID and prepared static reference are required") : Error);
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
	ElysiumLightStyle::StampUnstyled(Comp);   // R7.4 (G6), as in BuildBrushVisual
	// prop_dynamic is visual-only; prop_physics owns collision. The baked mesh carries
	// collision geometry for the props that do need it, so it is switched off here per component.
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Comp->SetupAttachment(Root);
	Comp->SetRelativeLocationAndRotation(Location, Rotation);
	// A 3D-skybox body is the miniature at its own scale: scenery the player can never
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

void UElysiumEntityBodies::ApplyPropSkin(UStaticMeshComponent* Comp, const FString& Name, int32 Family)
{
	const auto Ready = ElysiumPreparedProps::ForOwner(GetOwner()); FString Error;
	if (Ready && Ready->ApplySkin(Comp, ElysiumPreparedProps::ModelId(Name), Family, Error)) return;
	ReportNativeModelFailure(TEXT("prop-static-skin:") + Name, Error.IsEmpty() ? TEXT("native skin catalogue was not prepared") : Error);
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
	ElysiumLightStyle::StampUnstyled(Comp);   // R7.4 (G6), as in BuildBrushVisual
	Comp->SetupAttachment(Root);
	Comp->SetRelativeLocationAndRotation(Location, Rotation);
	// Collide as a physics body (blocks the world's BlockAll hull colliders). Simulation, mass and
	// the elysium.PhysicsProps gate are the leaf's call — the body stands here inert until it decides.
	Comp->SetCollisionProfileName(TEXT("PhysicsActor"));
	// A 3D-skybox body is the miniature at its own scale: scenery the player can never
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
