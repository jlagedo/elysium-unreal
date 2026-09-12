#include "Visual/ElysiumLightRig.h"
#include "ElysiumContentPaths.h"
#include "ElysiumMapLightQueryData.h"
#include "ElysiumMapCollisionPayload.h"
#include "ElysiumMoveSolve.h"
#include "Map/ElysiumMapCollision.h"
#include "EngineUtils.h"
#include "PhysicsEngine/BodySetup.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumLightQuery, Log, All);

void UElysiumLightRig::AdoptGameplayLight(const FString& InMapName)
{
	bGameplayLightAvailable = false;
	for (UPrimitiveComponent* Component : GameplayLightColliders)
		if (Component) Component->DestroyComponent();
	if (GameplaySkyCollider) GameplaySkyCollider->DestroyComponent();
	GameplayLightColliders.Reset();
	GameplaySkyCollider = nullptr;
	GameplayShadowProps.Reset();
	GameplayLightData = nullptr;
	if (!GetOwner() || !GetWorld()) return;
	const FString Path = FElysiumContentPaths::BakedMapLightQuery(InMapName);
	GameplayLightData = LoadObject<UElysiumMapLightQueryData>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!GameplayLightData || !GameplayLightData->IsValidQuery()
		|| !GameplayLightData->Occluders->CreatePhysicsMeshes() || !GameplayLightData->Sky->CreatePhysicsMeshes())
	{
		UE_LOG(LogElysiumLightQuery, Warning, TEXT("%s: gameplay light query requires a valid cooked V2 light-query asset; bake map %s"), *Path, *InMapName);
		return;
	}
	auto Make = [this](UBodySetup* Setup, const FBox& QueryBounds, bool bComplex) -> UPrimitiveComponent*
	{
		if (!Setup) return nullptr;
		UElysiumCollisionOnlyMeshComponent* C = NewObject<UElysiumCollisionOnlyMeshComponent>(GetOwner());
		C->SetupAttachment(this);
		C->bUseComplexAsSimpleCollision = bComplex;
		C->bUseAsyncCooking = false;
		C->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		C->SetCollisionResponseToAllChannels(ECR_Ignore);
		C->SetGenerateOverlapEvents(false);
		C->SetCanEverAffectNavigation(false);
		C->SetLocalCollisionBounds(QueryBounds);
		C->ProcMeshBodySetup = Setup;
		C->RegisterComponent();
		return C;
	};
	UElysiumMapCollisionPayload* Blockers = GameplayLightData->Occluders;
	if (UPrimitiveComponent* C = Make(Blockers->GetWorldHulls(), Blockers->WorldHullBounds(), false))
		GameplayLightColliders.Add(C);
	if (UPrimitiveComponent* C = Make(Blockers->GetDisplacement(), Blockers->DisplacementBounds(), true))
		GameplayLightColliders.Add(C);
	GameplaySkyCollider = Make(GameplayLightData->Sky->GetDisplacement(), GameplayLightData->Sky->DisplacementBounds(), true);
	// The bake stamps only original GAME_LUMP static props without flag 0x10. Neither entity
	// physics bodies nor brush entities enter this list, including doors and func_brush.
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (!It->ActorHasTag(TEXT("elysium.stealth-shadow"))) continue;
		TInlineComponentArray<UPrimitiveComponent*> Components(*It);
		for (UPrimitiveComponent* C : Components)
			if (C->IsRegistered() && C->IsPhysicsStateCreated()) GameplayShadowProps.Add(C);
	}
	bGameplayLightAvailable = true;
	UE_LOG(LogElysiumLightQuery, Log, TEXT("%s: %d authored worldlights, %d PVS clusters, %d static shadow props"),
		*InMapName, GameplayLightData->Records.Lights.Num(), GameplayLightData->Records.NumClusters, GameplayShadowProps.Num());
}

bool UElysiumLightRig::ArePointsInSamePvs(const FVector& APointCm, const FVector& BPointCm) const
{
	if (!bGameplayLightAvailable || !GameplayLightData) return true;
	const int32 A = GameplayLightData->ClusterAt(APointCm);
	const int32 B = GameplayLightData->ClusterAt(BPointCm);
	// A point in solid, or off the partition entirely, resolves to no cluster. Retail's `NPCInit`
	// (`0x1029a0b0`) seeds `m_bInPlayerPVS = 1`, so "visible" is the state an NPC starts in and
	// the answer that costs it nothing: an unplaceable point must not silently throttle a body's
	// think or delete it through `NPCThink`'s `DISAPPEAR` arm.
	if (A < 0 || B < 0) return true;
	return GameplayLightData->ClusterVisible(A, B);
}

float UElysiumLightRig::QueryGameplayLight(const FVector& PointCm) const
{
	if (!bGameplayLightAvailable || !GameplayLightData) return 0.f;
	const int32 Cluster = GameplayLightData->ClusterAt(PointCm);
	if (Cluster < 0) return 0.f;
	auto Trace = [&](const FVector& End, bool bSun)
	{
		float BlockTime = 1.f;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ElysiumStealthLight), false);
		FHitResult Hit;
		for (UPrimitiveComponent* C : GameplayLightColliders)
		{
			if (C && C->LineTraceComponent(Hit, PointCm, End, Params)) BlockTime = FMath::Min(BlockTime, Hit.Time);
		}
		Params.bTraceComplex = true;
		for (const TWeakObjectPtr<UPrimitiveComponent>& Weak : GameplayShadowProps)
		{
			if (UPrimitiveComponent* C = Weak.Get())
				if (C->LineTraceComponent(Hit, PointCm, End, Params)) BlockTime = FMath::Min(BlockTime, Hit.Time);
		}
		if (!bSun) return BlockTime == 1.f;
		// A clear ray alone is not sky: it must end on an authored SURF_SKY plane before any
		// other blocker. The convex world and its sky face share the same boundary.
		return GameplaySkyCollider && GameplaySkyCollider->LineTraceComponent(Hit, PointCm, End, Params)
			&& Hit.Time <= BlockTime + 0.000001f;
	};
	return ElysiumWorldLight::Query(*GameplayLightData, PointCm,
		[this](int32 Style) { return GameplayStyleMultiplier(Style); }, Trace);
}
