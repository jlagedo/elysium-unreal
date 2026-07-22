#include "ElysiumBrushComponent.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMapActor.h"

#include "PhysicsEngine/BodySetup.h"

EElysiumBrushSolidity ElysiumBrushSolidityForClass(const FString& Classname)
{
	// trigger_* are non-solid overlap volumes (SOLID_TRIGGER in Source); func_illusionary is a
	// purely visual brush with no collision. Everything else (func_door/button/brush/door_rotating/
	// elevator/rotating/lod) is a solid blocker.
	if (Classname.StartsWith(TEXT("trigger_")))
	{
		return EElysiumBrushSolidity::Trigger;
	}
	if (Classname.Equals(TEXT("func_illusionary"), ESearchCase::IgnoreCase))
	{
		return EElysiumBrushSolidity::None;
	}
	return EElysiumBrushSolidity::Solid;
}

UElysiumBrushComponent::UElysiumBrushComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	// Runtime-placed (SetRelativeLocation after spawn) and moved by the P4 movers — Movable, not
	// Static, so those transforms don't warn/no-op.
	Mobility = EComponentMobility::Movable;
	SetCastShadow(false);
	// Collision-only: no render sections, no scene proxy (base returns nullptr) — invisible.
}

void UElysiumBrushComponent::InitBrush(const FElysiumEntityHandle& InOwner,
	const TArray<FElysiumConvexHull>& Hulls, EElysiumBrushSolidity Solidity)
{
	OwningEntity = InOwner;
	BuiltSolidity = Solidity;

	// One convex element per hull, cooked once. Same recipe as the world .hulls and the static-prop
	// collision (ElysiumStaticMesh.cpp): simple-as-complex, verts verbatim (entity-local cm).
	BrushBodySetup = NewObject<UBodySetup>(this);
	BrushBodySetup->CollisionTraceFlag = CTF_UseSimpleAsComplex;
	BrushBodySetup->bGenerateMirroredCollision = false;
	BrushBodySetup->bDoubleSidedGeometry = false;

	LocalBounds = FBox(ForceInit);
	for (const FElysiumConvexHull& Hull : Hulls)
	{
		if (Hull.Vertices.Num() < 4)
		{
			continue;   // a convex needs at least a tetrahedron
		}
		FKConvexElem Convex;
		Convex.VertexData = Hull.Vertices;
		Convex.UpdateElemBox();
		LocalBounds += Convex.ElemBox;
		BrushBodySetup->AggGeom.ConvexElems.Add(MoveTemp(Convex));
	}
	BrushBodySetup->InvalidatePhysicsData();
	BrushBodySetup->CreatePhysicsMeshes();

	ApplySolidity(Solidity);

	// The single overlap tap (bound once; events fire only after RegisterComponent).
	OnComponentBeginOverlap.AddDynamic(this, &UElysiumBrushComponent::HandleBeginOverlap);
	OnComponentEndOverlap.AddDynamic(this, &UElysiumBrushComponent::HandleEndOverlap);
}

void UElysiumBrushComponent::ApplySolidity(EElysiumBrushSolidity Solidity)
{
	switch (Solidity)
	{
	case EElysiumBrushSolidity::Solid:
		// Blocks like the world brushes. A solid brush blocking the pawn produces Hit, not
		// begin/end overlap, so no overlap events are generated here (P4 movers handle OnBlocked).
		SetCollisionProfileName(TEXT("BlockAll"));
		SetGenerateOverlapEvents(false);
		break;
	case EElysiumBrushSolidity::Trigger:
		// Query-only overlap volume: the pawn overlaps it (its response to Pawn is Overlap, so the
		// pawn's Block-of-WorldDynamic is not a mutual block) and walks through it, raising touch.
		SetCollisionProfileName(TEXT("OverlapAllDynamic"));
		SetGenerateOverlapEvents(true);
		break;
	case EElysiumBrushSolidity::None:
		SetCollisionProfileName(TEXT("NoCollision"));
		SetGenerateOverlapEvents(false);
		break;
	}
}

void UElysiumBrushComponent::SetDormant(bool bDormant)
{
	if (bDormant)
	{
		SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SetGenerateOverlapEvents(false);
	}
	else
	{
		ApplySolidity(BuiltSolidity);
	}
}

FBoxSphereBounds UElysiumBrushComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	if (LocalBounds.IsValid)
	{
		return FBoxSphereBounds(LocalBounds).TransformBy(LocalToWorld);
	}
	return FBoxSphereBounds(LocalToWorld.GetLocation(), FVector::ZeroVector, 0.f);
}

void UElysiumBrushComponent::HandleBeginOverlap(UPrimitiveComponent*, AActor*, UPrimitiveComponent*,
	int32, bool, const FHitResult&)
{
	RouteTouch(/*bBegin*/ true);
}

void UElysiumBrushComponent::HandleEndOverlap(UPrimitiveComponent*, AActor*, UPrimitiveComponent*, int32)
{
	RouteTouch(/*bBegin*/ false);
}

void UElysiumBrushComponent::RouteTouch(bool bBegin) const
{
	// Reach the world through the owning map actor: on map unload the actor drops its world
	// (TPimplPtr reset) before destroying its components, so a late overlap sees a null world
	// rather than a dangling pointer.
	const AElysiumMapActor* Map = Cast<AElysiumMapActor>(GetOwner());
	if (!Map)
	{
		return;
	}
	if (FElysiumEntityWorld* World = Map->GetEntityWorld())
	{
		// The player pawn is not yet an entity, so the activator is unresolved (Invalid). P1.6
		// triggers filter by class/spawnflags and translate the touch into OnStartTouch/OnEndTouch.
		World->RouteBrushTouch(OwningEntity, FElysiumEntityHandle::Invalid(), bBegin);
	}
}
