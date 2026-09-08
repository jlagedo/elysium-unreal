// R7.2 -- the one owner of every decal in a world (ruling 4 and owner call B). The header carries
// the design; this file is its mechanics.

#include "ElysiumDecalSubsystem.h"

#include "ElysiumContentPaths.h"
#include "ElysiumFog.h"
#include "ElysiumMoveSolve.h"          // ElysiumMove::U -- the Source unit, for $decalscale
#include "ElysiumPhysicalMaterial.h"
#include "ElysiumSurfaceSettings.h"

#include "Components/DecalComponent.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "MaterialDomain.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumDecals, Log, All);

namespace
{
	// `bake_map.py::DECAL_HALF_DEPTH` -- how far a decal's box reaches either way along its
	// projection axis. Kept in step with the bake so a laid stain catches its host wall and not
	// the geometry behind it, exactly as a placed one does.
	constexpr float DecalHalfDepthCm = 16.f;

	// How long a decal with a lifetime takes to fade once its life is up. Long enough to read as
	// a fade rather than a pop; VtMB never faded a decal at all (it recycled under `r_decals`), so
	// this is presentation, not a reproduced number.
	constexpr float DecalFadeSeconds = 1.f;
}

// -------------------------------------------------------------------------------- the subsystem

void UElysiumDecalSubsystem::Deinitialize()
{
	AdoptedDecals.Reset();
	AdoptedMids.Reset();
	LaidDecals.Reset();
	PoolActor = nullptr;
	Super::Deinitialize();
}

bool UElysiumDecalSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

const TCHAR* UElysiumDecalSubsystem::DecalMasterPath()
{
	return TEXT("/Game/ElysiumGenerated/Materials/V2/M_V2_Decal.M_V2_Decal");
}

UMaterialInterface* UElysiumDecalSubsystem::LoadDecalMaster()
{
	if (UMaterialInterface* Master =
		LoadObject<UMaterialInterface>(nullptr, DecalMasterPath(), nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		return Master;
	}
	// The generated master is absent (a checkout that has not run `uv run elysium export bundle
	// policy`, or a content-free automation run). The engine's own deferred-decal default is the
	// only other material a `UDecalComponent` will actually draw -- the shared V2 error material
	// is surface-domain, and `FDeferredDecalProxy` substitutes this exact asset for it anyway
	// (`DecalComponent.cpp` 51-62), so binding it explicitly is the honest form of what would
	// otherwise happen silently.
	return UMaterial::GetDefaultMaterial(MD_DeferredDecal);
}

UMaterialInterface* UElysiumDecalSubsystem::LoadProjectorInstance(const FString& MaterialId)
{
	const FString Path = FElysiumContentPaths::BakedDecalMaterial(MaterialId);
	if (Path.IsEmpty())
	{
		return nullptr;
	}
	return LoadObject<UMaterialInterface>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
}

void UElysiumDecalSubsystem::AdoptBaked(const TArray<UDecalComponent*>& Components)
{
	// A second `AdoptBakedLevel` in one world (a map rebuilt without travel) leaves the previous
	// level's components behind. Drop them here rather than growing the fog walk forever; the two
	// arrays are index-parallel, so they are pruned together.
	for (int32 I = AdoptedDecals.Num() - 1; I >= 0; --I)
	{
		if (!IsValid(AdoptedDecals[I]))
		{
			AdoptedDecals.RemoveAt(I, EAllowShrinking::No);
			AdoptedMids.RemoveAt(I, EAllowShrinking::No);
		}
	}

	int32 Added = 0;
	for (UDecalComponent* Comp : Components)
	{
		if (!IsValid(Comp) || AdoptedDecals.Contains(Comp))
		{
			continue;
		}
		// One MID per component, parented to whatever the bake bound. `CreateDynamicMaterialInstance`
		// both creates it and sets it back on the component, which is the whole point: the fog term
		// has nowhere else to live, because a bake cannot save an MID into a level.
		UMaterialInstanceDynamic* Mid = Comp->CreateDynamicMaterialInstance();
		if (!Mid)
		{
			continue;
		}
		AdoptedDecals.Add(Comp);
		AdoptedMids.Add(Mid);
		ElysiumFog::ApplyToDecalMID(Mid, bFogEnabled, FogColor, FogStartCm, FogEndCm);
		// The bake numbers a map's decals 0..N-1 by `.decals` line order (`_place_decals`), and
		// this subsystem's own serial starts at 0 -- so a stain laid in the first seconds of a map
		// would sort UNDER every baked poster and blood pool on the same wall. Push the serial past
		// what the bake wrote: `R_DecalCreate` appended runtime decals to the same list the map's
		// own decals were already in, so a hole shot today is always later than a hole shipped
		// with the map.
		NextSortOrder = FMath::Max(NextSortOrder, Comp->SortOrder + 1);
		++Added;
	}
	if (Added > 0)
	{
		UE_LOG(LogElysiumDecals, Log, TEXT("adopted %d baked decal(s) (%d held)"), Added,
			AdoptedDecals.Num());
	}
}

int32 UElysiumDecalSubsystem::ApplyFog(bool bEnabled, const FLinearColor& Color, float StartCm,
	float EndCm)
{
	bFogEnabled = bEnabled;
	FogColor = Color;
	FogStartCm = StartCm;
	FogEndCm = EndCm;

	int32 Stamped = 0;
	for (const TObjectPtr<UMaterialInstanceDynamic>& Mid : AdoptedMids)
	{
		if (Mid)
		{
			ElysiumFog::ApplyToDecalMID(Mid, bEnabled, Color, StartCm, EndCm);
			++Stamped;
		}
	}
	for (const FElysiumLaidDecal& Laid : LaidDecals)
	{
		if (Laid.Mid)
		{
			ElysiumFog::ApplyToDecalMID(Laid.Mid, bEnabled, Color, StartCm, EndCm);
			++Stamped;
		}
	}
	return Stamped;
}

AActor* UElysiumDecalSubsystem::PoolHost()
{
	if (IsValid(PoolActor))
	{
		return PoolActor;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
	if (Actor)
	{
		USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("DecalPoolRoot"));
		Actor->SetRootComponent(Root);
		Root->RegisterComponent();
#if WITH_EDITOR
		Actor->SetActorLabel(TEXT("ElysiumDecalPool"));
		Actor->SetFolderPath(TEXT("Elysium"));
#endif
	}
	PoolActor = Actor;
	return PoolActor;
}

void UElysiumDecalSubsystem::PruneDead()
{
	// A lifetime stain is destroyed by the engine's own fade timer (`UDecalComponent::SetFadeOut`
	// -> `SetLifeSpan` -> `DestroyComponent`), so the pool learns about it here rather than by
	// holding a second timer of its own.
	LaidDecals.RemoveAll([](const FElysiumLaidDecal& Laid) { return !IsValid(Laid.Component); });
}

UDecalComponent* UElysiumDecalSubsystem::Lay(const FElysiumDecalRequest& Request)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	// What it draws. An id names the shared projector instance the materials stage staged; a
	// texture is a collide sprite, which has no instance of its own and rides the master directly.
	UMaterialInterface* Parent = nullptr;
	if (!Request.MaterialId.IsEmpty())
	{
		Parent = LoadProjectorInstance(Request.MaterialId);
		if (!Parent)
		{
			bool bAlready = false;
			ReportedMissing.Add(Request.MaterialId, &bAlready);
			if (!bAlready)
			{
				UE_LOG(LogElysiumDecals, Warning,
					TEXT("no projector instance for '%s' (%s) -- nothing laid. The materials stage "
						 "stages one `MI_<unit>_Decal` per `$decal`/`decalmodulate` unit (R7.2 "
						 "ruling 2); the error material is surface-domain and would draw nothing."),
					*Request.MaterialId, *FElysiumContentPaths::BakedDecalMaterial(Request.MaterialId));
			}
			return nullptr;
		}
	}
	else if (Request.Texture)
	{
		Parent = LoadDecalMaster();
	}
	if (!Parent)
	{
		return nullptr;
	}

	AActor* Host = PoolHost();
	if (!Host)
	{
		return nullptr;
	}

	PruneDead();

	// The cap. `MaxLaidDecals` is `r_decals`' home; oldest-first is what the engine's own list did
	// when it ran out (`R_DecalCreate`), so the recycled slot is the front of ours.
	const int32 Cap = FMath::Max(1, GetDefault<UElysiumSurfaceSettings>()->MaxLaidDecals);
	FElysiumLaidDecal Entry;
	while (LaidDecals.Num() >= Cap)
	{
		// Only the last one taken is re-used; anything above the cap (the cap was lowered between
		// two shots) is destroyed rather than kept as a second free list.
		if (Entry.Component)
		{
			Entry.Component->DestroyComponent();
		}
		Entry = LaidDecals[0];
		LaidDecals.RemoveAt(0, EAllowShrinking::No);
	}

	UDecalComponent* Comp = Entry.Component;
	if (!Comp)
	{
		Comp = NewObject<UDecalComponent>(Host, NAME_None, RF_Transient);
		Comp->SetupAttachment(Host->GetRootComponent());
		Comp->RegisterComponent();
	}

	// The orientation, exactly as `bake_map.py::_place_decals` builds it: local +X is the
	// room-facing normal (the component projects along its -X into the wall) and the surface's
	// horizontal axis goes on local Z, because a deferred decal maps texture U to local Z and V to
	// local Y. `MakeFromXZ` builds a right-handed frame from the two.
	const FVector Normal = Request.Normal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	FVector Tangent = Request.Tangent - Normal * FVector::DotProduct(Request.Tangent, Normal);
	if (!Tangent.Normalize())
	{
		// A runtime stain carries no authored s/t frame. Any axis in the surface plane is as true
		// as any other, so take a stable one rather than rolling for a rotation nobody authored.
		Tangent = FVector::CrossProduct(Normal, FMath::Abs(Normal.Z) < 0.9f
			? FVector::UpVector : FVector::ForwardVector).GetSafeNormal();
	}
	const FRotator Rotation = FRotationMatrix::MakeFromXZ(Normal, Tangent).Rotator();

	Comp->SetWorldLocationAndRotation(Request.Location, Rotation);
	if (Request.Attach)
	{
		Comp->AttachToComponent(Request.Attach, FAttachmentTransformRules::KeepWorldTransform);
	}
	else if (Comp->GetAttachParent() != Host->GetRootComponent())
	{
		Comp->AttachToComponent(Host->GetRootComponent(),
			FAttachmentTransformRules::KeepWorldTransform);
	}

	// `DecalSize` is the box HALF-size: X the projection reach, Y vertical, Z horizontal.
	Comp->DecalSize = FVector(DecalHalfDepthCm,
		FMath::Max(Request.HalfSizeCm.X, UE_KINDA_SMALL_NUMBER),
		FMath::Max(Request.HalfSizeCm.Y, UE_KINDA_SMALL_NUMBER));
	// VtMB decals persist at any distance -- no screen-size fade-out, the same call the bake makes.
	Comp->SetFadeScreenSize(0.f);
	Comp->SetSortOrder(NextSortOrder++);
	Comp->SetVisibility(true);

	UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Parent, Comp);
	if (Request.Texture)
	{
		Mid->SetTextureParameterValue(ElysiumSurfaceParamsDecal::Textures::BaseTexture,
			Request.Texture);
	}
	ElysiumFog::ApplyToDecalMID(Mid, bFogEnabled, FogColor, FogStartCm, FogEndCm);
	Comp->SetDecalMaterial(Mid);

	const bool bPersistent = Request.LifetimeSeconds <= 0.f;
	// `SetFadeOut` is also what CLEARS a recycled component's previous fade timer (a zero duration
	// clears the life span), which is what makes the pool's slots interchangeable.
	Comp->SetFadeOut(bPersistent ? 0.f : Request.LifetimeSeconds,
		bPersistent ? 0.f : DecalFadeSeconds, /*bDestroyOwnerAfterFade*/ false);

	Entry.Component = Comp;
	Entry.Mid = Mid;
	Entry.MaterialId = Request.MaterialId;
	Entry.Position = Request.Location;
	Entry.Normal = Normal;
	Entry.EntityIndex = Request.EntityIndex;
	Entry.bPersistent = bPersistent;
	LaidDecals.Add(Entry);
	return Comp;
}

TArray<FElysiumDecalRecord> UElysiumDecalSubsystem::Records() const
{
	TArray<FElysiumDecalRecord> Out;
	Out.Reserve(LaidDecals.Num());
	for (const FElysiumLaidDecal& Laid : LaidDecals)
	{
		// A stain with a lifetime is gone before the save is read, and one with no material id is
		// a collide sprite -- a particle's own texture, which `DECALLIST` has no name for.
		if (!Laid.bPersistent || Laid.MaterialId.IsEmpty() || !IsValid(Laid.Component))
		{
			continue;
		}
		FElysiumDecalRecord Record;
		Record.MaterialId = Laid.MaterialId;
		Record.Position = Laid.Position;
		Record.Normal = Laid.Normal;
		Record.EntityIndex = Laid.EntityIndex;
		Out.Add(MoveTemp(Record));
	}
	return Out;
}

int32 UElysiumDecalSubsystem::Restore(const TArray<FElysiumDecalRecord>& Records)
{
	int32 Laid = 0;
	for (const FElysiumDecalRecord& Record : Records)
	{
		FElysiumDecalRequest Request;
		Request.MaterialId = Record.MaterialId;
		Request.Location = Record.Position;
		Request.Normal = Record.Normal;
		// The size is the unit's, not the save's: `$decalscale` x the texture dimensions, exactly
		// what the shot that laid it read.
		Request.HalfSizeCm = ElysiumImpactDecals::HalfSizeCmFor(Record.MaterialId);
		Request.EntityIndex = Record.EntityIndex;
		Laid += Lay(Request) != nullptr ? 1 : 0;
	}
	return Laid;
}

void UElysiumDecalSubsystem::ClearLaid()
{
	for (const FElysiumLaidDecal& Laid : LaidDecals)
	{
		if (IsValid(Laid.Component))
		{
			Laid.Component->DestroyComponent();
		}
	}
	LaidDecals.Reset();
}

// --------------------------------------------------------------- the C_TEGunshotDecal decal half

namespace ElysiumImpactDecals
{

FString PoolFor(const FString& GameMaterial, bool bSoak)
{
	// The soak column is a hit the target soaked, and it is a flesh hit whatever it landed on
	// (`effects.md` §3.5: the soak remap `F -> K` is the PARTICLE's; the decal reads the
	// unremapped character and takes `flesh/soak`).
	if (bSoak)
	{
		return TEXT("decals/hits/flesh/soak");
	}
	const TCHAR Ch = GameMaterial.IsEmpty() ? TEXT('\0') : FChar::ToUpper(GameMaterial[0]);
	switch (Ch)
	{
	case TEXT('F'):                                     // flesh, fish_fresh/frozen, watermelon
		return TEXT("decals/hits/flesh/blood");
	case TEXT('W'):                                     // wood, woodpanel
		return TEXT("decals/hits/wood/shot");
	case TEXT('Y'):                                     // bottle, glass, glass_shard, glassbottle
		return TEXT("decals/hits/glass/shot");
	case TEXT('M'):                                     // metal, metalpanel, canister, armorflesh
	case TEXT('V'):                                     // metalvent, metal_barrel, the kitchen pans
	case TEXT('G'):                                     // metalgrate
	case TEXT('P'):                                     // computer
		return TEXT("decals/hits/metal/shot");
	default:
		// brick/concrete/default, the dirt and carpet group, rock, tile, water, the gargoyle and
		// Ming Xiao rows, and the 7 units with no `gamematerial` at all: the table's own index 0.
		return TEXT("decals/hits/concrete/shot");
	}
}

FString MaterialIdFor(const FString& GameMaterial, bool bSoak, int32 Variation)
{
	const int32 N = FMath::Clamp(Variation, 1, PoolSize);
	return FString::Printf(TEXT("vtmb:material:%s%d"), *PoolFor(GameMaterial, bSoak), N);
}

FVector2D HalfSizeCmFor(const FString& PoolOrMaterialId)
{
	// 64 texels x `$decalscale`, in Source units, halved and taken to cm.
	constexpr float TextureDim = 64.f;
	const float Scale = PoolOrMaterialId.Contains(TEXT("decals/hits/glass/"),
		ESearchCase::IgnoreCase) ? 0.25f : 0.10f;
	const float Half = TextureDim * Scale * ElysiumMove::U * 0.5f;
	return FVector2D(Half, Half);
}

bool BuildImpactRequest(UWorld* World, const FVector& FromCm, const FVector& Direction,
	float RangeCm, int32 Variation, FElysiumDecalRequest& OutRequest, FHitResult& OutHit)
{
	FVector Dir = Direction;
	if (!World || RangeCm <= 0.f || !Dir.Normalize())
	{
		return false;
	}

	FCollisionQueryParams Params(FName(TEXT("ElysiumShotImpact")), /*bTraceComplex*/ false);
	// The surface character is the whole question this trace exists to answer.
	Params.bReturnPhysicalMaterial = true;
	if (!World->LineTraceSingleByChannel(OutHit, FromCm, FromCm + Dir * RangeCm,
		SurfaceTraceChannel, Params))
	{
		return false;
	}

	FString GameMaterial;
	if (const UElysiumPhysicalMaterial* Surface =
		Cast<UElysiumPhysicalMaterial>(OutHit.PhysMaterial.Get()))
	{
		GameMaterial = Surface->GameMaterial;
	}

	OutRequest = FElysiumDecalRequest();
	// The soak column belongs to the damage resolution, not to a wall; a world trace never takes it.
	OutRequest.MaterialId = MaterialIdFor(GameMaterial, /*bSoak*/ false, Variation);
	OutRequest.Location = OutHit.ImpactPoint;
	OutRequest.Normal = OutHit.ImpactNormal;
	OutRequest.HalfSizeCm = HalfSizeCmFor(OutRequest.MaterialId);
	return true;
}

} // namespace ElysiumImpactDecals
