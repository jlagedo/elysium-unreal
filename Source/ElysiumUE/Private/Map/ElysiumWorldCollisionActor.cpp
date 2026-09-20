#include "ElysiumWorldCollisionActor.h"

#include "ElysiumMapCollisionPayload.h"
#include "PhysicsEngine/BodySetup.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumWorldCollision, Log, All);

UElysiumWorldCollisionComponent::UElysiumWorldCollisionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	// The world does not move, and a static component is what lets Recast bake a mesh from it
	// rather than treat it as a dynamic obstacle.
	Mobility = EComponentMobility::Static;
	bUseEditorCompositing = false;
	SetGenerateOverlapEvents(false);
}

void UElysiumWorldCollisionComponent::ApplySignature()
{
	const EElysiumContentsSignature Kind = static_cast<EElysiumContentsSignature>(Signature);
	// A NAME, not a set of responses: only the name survives the `.umap` save/load round trip.
	SetCollisionProfileName(ElysiumContents::ProfileName(Kind));
	// Navigation follows the profile rather than being set beside it -- Unreal calls a body
	// navigation-relevant exactly when it blocks ECC_Pawn, which the profile decides.
	SetCanEverAffectNavigation(ElysiumContents::AffectsNavigation(Kind));
}

FBoxSphereBounds UElysiumWorldCollisionComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	if (!LocalCollisionBounds.IsValid)
	{
		return FBoxSphereBounds(LocalToWorld.GetLocation(), FVector::ZeroVector, 0.0f);
	}
	return FBoxSphereBounds(LocalCollisionBounds).TransformBy(LocalToWorld);
}

void UElysiumWorldCollisionComponent::PostLoad()
{
	Super::PostLoad();
	// Re-applied on every load: the profile name round-trips, but the navigation flag and the
	// responses the name implies are re-derived here rather than trusted from the package.
	ApplySignature();
}

AElysiumWorldCollisionActor::AElysiumWorldCollisionActor()
{
	PrimaryActorTick.bCanEverTick = false;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Static);
	SetRootComponent(Root);
}

#if WITH_EDITOR

int32 AElysiumWorldCollisionActor::AuthorFromPayload(UElysiumMapCollisionPayload* InPayload)
{
	for (UElysiumWorldCollisionComponent* Existing : Bodies)
	{
		if (Existing)
		{
			Existing->DestroyComponent();
		}
	}
	Bodies.Reset();

	Payload = InPayload;
	if (InPayload == nullptr)
	{
		return 0;
	}
	MapName = InPayload->MapName;

	for (const FElysiumSignatureCollisionBody& Row : InPayload->GetWorldBodies())
	{
		if (Row.Body == nullptr || Row.HullCount == 0)
		{
			continue;
		}
		const FString Spelling =
			ElysiumContents::Spell(static_cast<EElysiumContentsSignature>(Row.Signature));
		UElysiumWorldCollisionComponent* Component = NewObject<UElysiumWorldCollisionComponent>(
			this, FName(*FString::Printf(TEXT("World_%s"), *Spelling)));
		Component->Signature = Row.Signature;
		Component->Body = Row.Body;
		Component->LocalCollisionBounds = Row.Bounds;
		Component->ApplySignature();
		Component->SetupAttachment(GetRootComponent());
		Component->RegisterComponent();
		AddInstanceComponent(Component);
		Bodies.Add(Component);
		UE_LOG(LogElysiumWorldCollision, Log,
			TEXT("%s: world body %s, %d hull(s), profile '%s'%s"), *MapName, *Spelling,
			Row.HullCount, *Component->GetCollisionProfileName().ToString(),
			Component->CanEverAffectNavigation() ? TEXT(" (cuts the NavMesh)") : TEXT(""));
	}
	return Bodies.Num();
}

#endif   // WITH_EDITOR
