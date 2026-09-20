#include "ElysiumMapCollisionPayload.h"

#include "ElysiumContentsSignature.h"
#include "PhysicsEngine/BodySetup.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumCollisionPayload, Log, All);

namespace
{
	// The union of a convex set's element boxes -- what the collision-only component needs for its
	// bounds, without rebuilding a hull.
	FBox ConvexBounds(const UBodySetup* Setup)
	{
		FBox Box(ForceInit);
		if (Setup)
		{
			for (const FKConvexElem& Convex : Setup->AggGeom.ConvexElems)
			{
				Box += Convex.ElemBox;
			}
		}
		return Box;
	}

#if WITH_EDITOR
	// Detach a body setup a re-author is replacing: it is a subobject of the asset's package, so
	// leaving it behind would keep it in the saved package for nothing. Renaming it into the
	// transient package takes it out of the save without touching the live one.
	void DiscardAuthored(UBodySetup* Old)
	{
		if (Old)
		{
			Old->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
		}
	}

	// One convex element per authored hull, with the vertices verbatim. Fewer than four vertices
	// cannot bound a volume, so such a hull contributes nothing -- the same rule
	// `UElysiumBrushComponent::InitBrush` applies to a def's hulls.
	int32 FillConvexElems(UBodySetup& Setup, const TArray<FElysiumCollisionHull>& Hulls)
	{
		Setup.AggGeom.ConvexElems.Reset();
		Setup.AggGeom.ConvexElems.Reserve(Hulls.Num());
		for (const FElysiumCollisionHull& Hull : Hulls)
		{
			if (Hull.Vertices.Num() < 4)
			{
				continue;
			}
			FKConvexElem Convex;
			Convex.VertexData = Hull.Vertices;
			Convex.UpdateElemBox();
			Setup.AggGeom.ConvexElems.Add(MoveTemp(Convex));
		}
		return Setup.AggGeom.ConvexElems.Num();
	}
#endif
}

UBodySetup* UElysiumMapCollisionPayload::FindBrushBody(int32 EntityIndex) const
{
	const FElysiumBrushCollisionBody* Row = FindBrushRow(EntityIndex);
	return Row ? Row->Body : nullptr;
}

const FElysiumBrushCollisionBody* UElysiumMapCollisionPayload::FindBrushRow(
	int32 EntityIndex) const
{
	for (const FElysiumBrushCollisionBody& Row : BrushBodies)
	{
		if (Row.EntityIndex == EntityIndex)
		{
			return &Row;
		}
	}
	return nullptr;
}

uint8 UElysiumMapCollisionPayload::LegacyWorldSignature()
{
	// A version-1 payload was staged from the `BLOCK_MASK`-filtered sidecar, so its one body is
	// the player-solid set: blocks both pawns, says nothing about sight. That is exactly what the
	// `BlockAll` component it wore did, the sight channel defaulting to Ignore.
	return static_cast<uint8>(
		EElysiumContentsSignature::Player | EElysiumContentsSignature::Npc);
}

int32 UElysiumMapCollisionPayload::WorldHullCount() const
{
	if (WorldBodies.Num() > 0)
	{
		int32 Count = 0;
		for (const FElysiumSignatureCollisionBody& Row : WorldBodies)
		{
			Count += Row.HullCount;
		}
		return Count;
	}
	return WorldHulls ? WorldHulls->AggGeom.ConvexElems.Num() : 0;
}

FBox UElysiumMapCollisionPayload::WorldHullBounds() const
{
	if (WorldBodies.Num() > 0)
	{
		FBox Box(ForceInit);
		for (const FElysiumSignatureCollisionBody& Row : WorldBodies)
		{
			if (Row.Bounds.IsValid)
			{
				Box += Row.Bounds;
			}
		}
		return Box;
	}
	return ConvexBounds(WorldHulls);
}

FBox UElysiumMapCollisionPayload::DisplacementBounds() const
{
	FBox Box(ForceInit);
	for (const FVector3f& Vertex : DisplacementVertices)
	{
		Box += FVector(Vertex);
	}
	return Box;
}

bool UElysiumMapCollisionPayload::CreatePhysicsMeshes()
{
	bool bAllCreated = true;
	auto Create = [&bAllCreated](UBodySetup* Setup)
	{
		if (!Setup)
		{
			return;
		}
		if (!Setup->bCreatedPhysicsMeshes)
		{
			Setup->CreatePhysicsMeshes();
		}
		bAllCreated &= !Setup->bFailedToCreatePhysicsMeshes;
	};

	Create(WorldHulls);
	for (const FElysiumSignatureCollisionBody& Row : WorldBodies)
	{
		Create(Row.Body);
	}
	Create(Displacement);
	for (const FElysiumBrushCollisionBody& Row : BrushBodies)
	{
		Create(Row.Body);
	}
	return bAllCreated;
}

bool UElysiumMapCollisionPayload::GetPhysicsTriMeshData(FTriMeshCollisionData* CollisionData,
	bool /*InUseAllTriData*/)
{
	if (!CollisionData || DisplacementIndices.Num() < 3)
	{
		return false;
	}

	CollisionData->Vertices = DisplacementVertices;
	CollisionData->Indices.Reserve(DisplacementIndices.Num() / 3);
	for (int32 Base = 0; Base + 2 < DisplacementIndices.Num(); Base += 3)
	{
		FTriIndices Triangle;
		Triangle.v0 = DisplacementIndices[Base];
		Triangle.v1 = DisplacementIndices[Base + 1];
		Triangle.v2 = DisplacementIndices[Base + 2];
		CollisionData->Indices.Add(Triangle);
	}

	// `bFlipNormals` matches what the procedural-mesh component states for the same soup (a Chaos
	// trimesh is two-sided either way). `bDeformableMesh`/`bFastCook` deliberately do NOT: those
	// two exist because a procedural mesh cooks while the game runs, and this payload cooks
	// offline.
	CollisionData->bFlipNormals = true;
	CollisionData->bDeformableMesh = false;
	CollisionData->bFastCook = false;
	return true;
}

bool UElysiumMapCollisionPayload::ContainsPhysicsTriMeshData(bool /*InUseAllTriData*/) const
{
	return DisplacementIndices.Num() >= 3;
}

#if WITH_EDITOR

void UElysiumMapCollisionPayload::AuthorWorldHulls(const TArray<FElysiumCollisionHull>& Hulls)
{
	DiscardAuthored(WorldHulls);
	WorldHulls = NewObject<UBodySetup>(this);
	// The world collider's own recipe, as `UProceduralMeshComponent::CreateBodySetupHelper` states
	// it for `bUseComplexAsSimpleCollision = false` -- reproduced here rather than inherited, so the
	// cooked path and the sidecar path are the same physics.
	WorldHulls->CollisionTraceFlag = CTF_UseDefault;
	WorldHulls->bGenerateMirroredCollision = false;
	WorldHulls->bDoubleSidedGeometry = true;
	WorldHulls->bHasCookedCollisionData = true;
	WorldHulls->BodySetupGuid = FGuid::NewGuid();
	const int32 Count = FillConvexElems(*WorldHulls, Hulls);
	UE_LOG(LogElysiumCollisionPayload, Log, TEXT("%s: authored %d world convex hull(s) of %d row(s)"),
		*MapName, Count, Hulls.Num());
}

void UElysiumMapCollisionPayload::AuthorWorldBody(uint8 Signature,
	const TArray<FElysiumCollisionHull>& Hulls)
{
	FElysiumSignatureCollisionBody Row;
	Row.Signature = Signature;
	// Auto-named for the same reason the brush bodies are: two `UBodySetup`s under one outer may
	// not share a name, and a re-author would collide with the subobject it is replacing.
	Row.Body = NewObject<UBodySetup>(this);
	// The world collider's recipe, as `AuthorWorldHulls` states it -- same physics on both paths.
	Row.Body->CollisionTraceFlag = CTF_UseDefault;
	Row.Body->bGenerateMirroredCollision = false;
	Row.Body->bDoubleSidedGeometry = true;
	Row.Body->bHasCookedCollisionData = true;
	Row.Body->BodySetupGuid = FGuid::NewGuid();
	Row.HullCount = FillConvexElems(*Row.Body, Hulls);
	Row.Bounds = ConvexBounds(Row.Body);
	WorldBodies.Add(MoveTemp(Row));
	// Any signature body makes this a version-2 payload, read by signature and never through
	// `WorldHulls`.
	PayloadVersion = 2;
	UE_LOG(LogElysiumCollisionPayload, Log,
		TEXT("%s: authored world body %s with %d convex hull(s) of %d row(s)"),
		*MapName, *ElysiumContents::Spell(static_cast<EElysiumContentsSignature>(Signature)),
		WorldBodies.Last().HullCount, Hulls.Num());
}

void UElysiumMapCollisionPayload::AuthorDisplacement(const TArray<FVector>& Vertices,
	const TArray<int32>& Indices)
{
	DisplacementVertices.Reset(Vertices.Num());
	for (const FVector& Vertex : Vertices)
	{
		DisplacementVertices.Add(FVector3f(Vertex));
	}
	DisplacementIndices = Indices;

	DiscardAuthored(Displacement);
	if (DisplacementIndices.Num() < 3)
	{
		return;   // a map with no displacements authors no trimesh
	}
	Displacement = NewObject<UBodySetup>(this);
	Displacement->CollisionTraceFlag = CTF_UseComplexAsSimple;
	Displacement->bGenerateMirroredCollision = false;
	Displacement->bDoubleSidedGeometry = true;
	Displacement->bHasCookedCollisionData = true;
	Displacement->BodySetupGuid = FGuid::NewGuid();
	UE_LOG(LogElysiumCollisionPayload, Log, TEXT("%s: authored %d displacement triangle(s)"),
		*MapName, DisplacementTriangleCount());
}

void UElysiumMapCollisionPayload::AuthorBrushBody(int32 EntityIndex,
	const TArray<FElysiumCollisionHull>& Hulls)
{
	FElysiumBrushCollisionBody Row;
	Row.EntityIndex = EntityIndex;
	// Auto-named, not `BrushBody_<ordinal>`: two `UBodySetup`s under one outer may not share a name,
	// and a re-author of a live asset would then collide with the subobject it is replacing.
	Row.Body = NewObject<UBodySetup>(this);
	// `UElysiumBrushComponent::InitBrush`'s own recipe.
	Row.Body->CollisionTraceFlag = CTF_UseSimpleAsComplex;
	Row.Body->bGenerateMirroredCollision = false;
	Row.Body->bDoubleSidedGeometry = false;
	Row.Body->bHasCookedCollisionData = true;
	Row.Body->BodySetupGuid = FGuid::NewGuid();
	FillConvexElems(*Row.Body, Hulls);
	BrushBodies.Add(MoveTemp(Row));
}

void UElysiumMapCollisionPayload::AuthorBrushBodyWithSignature(int32 EntityIndex, uint8 Signature,
	const TArray<FElysiumCollisionHull>& Hulls)
{
	AuthorBrushBody(EntityIndex, Hulls);
	BrushBodies.Last().Signature = Signature;
}

void UElysiumMapCollisionPayload::ResetAuthoring()
{
	DiscardAuthored(WorldHulls);
	DiscardAuthored(Displacement);
	for (const FElysiumSignatureCollisionBody& Row : WorldBodies)
	{
		DiscardAuthored(Row.Body);
	}
	for (const FElysiumBrushCollisionBody& Row : BrushBodies)
	{
		DiscardAuthored(Row.Body);
	}
	WorldHulls = nullptr;
	Displacement = nullptr;
	DisplacementVertices.Reset();
	DisplacementIndices.Reset();
	WorldBodies.Reset();
	BrushBodies.Reset();
	// Back to the shape a fresh asset has; `AuthorWorldBody` is what makes it 2 again.
	PayloadVersion = 1;
}

FString UElysiumMapCollisionPayload::CookAuthored()
{
	TArray<FString> Failures;
	auto Cook = [&Failures](UBodySetup* Setup, const FString& Name)
	{
		if (!Setup)
		{
			return;
		}
		Setup->InvalidatePhysicsData();
		Setup->CreatePhysicsMeshes();
		if (Setup->bFailedToCreatePhysicsMeshes)
		{
			Failures.Add(Name);
		}
	};

	Cook(WorldHulls, TEXT("world hulls"));
	for (const FElysiumSignatureCollisionBody& Row : WorldBodies)
	{
		Cook(Row.Body, FString::Printf(TEXT("world body %s"),
			*ElysiumContents::Spell(static_cast<EElysiumContentsSignature>(Row.Signature))));
	}
	Cook(Displacement, TEXT("displacement"));
	for (const FElysiumBrushCollisionBody& Row : BrushBodies)
	{
		Cook(Row.Body, FString::Printf(TEXT("brush body %d"), Row.EntityIndex));
	}

	return FString::Join(Failures, TEXT(", "));
}

#endif // WITH_EDITOR
