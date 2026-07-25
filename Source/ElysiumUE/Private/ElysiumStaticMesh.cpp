#include "ElysiumStaticMesh.h"

#include "ElysiumMaterialFactory.h"
#include "ElysiumCardBake.h"
#include "ElysiumObjModel.h"

#include "Engine/StaticMesh.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MeshCardBuild.h"
#include "MeshCardRepresentation.h"
#include "Misc/FileHelper.h"
#include "MeshDescription.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"
#include "StaticMeshResources.h"

// Lumen surface-cache coverage for runtime-built meshes (docs/lumen-coverage-spike.md).
// Read at mesh-build time, so re-travel or `elysium.reload` to A/B a change.
static TAutoConsoleVariable<int32> CVarLumenCards(
	TEXT("elysium.LumenCards"), 1,
	TEXT("Give runtime-built meshes a bounds-derived Lumen card representation, so they enter "
	     "Lumen's surface cache. 0 = off (the mesh stays invisible to Lumen GI/reflections)."),
	ECVF_Default);

// A/B the two card sources: 1 = install the `<map>.cards` bake where it matches (surfel-fitted,
// sits on the real surfaces), 0 = ignore the sidecar entirely and give every mesh the bounds
// fallback. The comparison the spike is about, without moving files. Read at map load.
static TAutoConsoleVariable<int32> CVarLumenCardsBaked(
	TEXT("elysium.LumenCardsBaked"), 1,
	TEXT("Use the <map>.cards bake (1) or force bounds cards everywhere (0). Applied at map load."),
	ECVF_Default);

bool FElysiumStaticMeshBuilder::BakedCardsEnabled()
{
	return CVarLumenCardsBaked.GetValueOnAnyThread() != 0;
}

// How many cards the offline builder may fit to one mesh (`cards.bat`). Epic's own default for a
// static mesh asset; raising it is their documented answer to leftover uncovered area, at the cost
// of surface-cache pages. Read by the bake only — the runtime just installs what was written.
static TAutoConsoleVariable<int32> CVarLumenCardMax(
	TEXT("elysium.LumenCardMax"), 12,
	TEXT("Max Lumen cards the -ElysiumCards bake fits per mesh. Applied at bake time."),
	ECVF_Default);

bool FElysiumStaticMeshBuilder::LumenCardsEnabled()
{
	return CVarLumenCards.GetValueOnAnyThread() != 0;
}

// A runtime-built UStaticMesh carries no card representation: the offline builder that produces
// one is editor-only, so LODResources[0].CardRepresentationData is null and Lumen drops the
// primitive from its scene (LumenMeshCards.cpp AddMeshCards -> bValidMeshCards false). Hand it
// the same six-faces-from-bounds representation UDynamicMeshComponent builds for its own runtime
// geometry: the LOD owns the allocation from here on (~FStaticMeshLODResources deletes it).
void FElysiumStaticMeshBuilder::AttachLumenCards(UStaticMesh* Mesh)
{
	FStaticMeshRenderData* RenderData = Mesh ? Mesh->GetRenderData() : nullptr;
	if (!RenderData || RenderData->LODResources.Num() == 0)
	{
		return;
	}

	FStaticMeshLODResources& LOD = RenderData->LODResources[0];
	if (LOD.CardRepresentationData)
	{
		return;
	}

	LOD.CardRepresentationData = new FCardRepresentationData();
	FMeshCardsBuildData& CardData = LOD.CardRepresentationData->MeshCardsBuildData;
	CardData.Bounds = Mesh->GetBoundingBox();
	// Bounds-derived cards do not follow the real surface, so hits are accepted under a high
	// sampling bias rather than a close match — the same allowance the engine's own runtime
	// path makes.
	CardData.bMostlyTwoSided = true;
	// Two-sided cards dilate by a texel, the pairing the engine's own runtime path uses — it keeps
	// a capture from falling off the edge of a thin surface.
	MeshCardRepresentation::SetCardsFromBounds(CardData, ELumenCardDilationMode::DilateOneTexel);
}

uint64 FElysiumStaticMeshBuilder::HashPropGeometry(const FElysiumObjModel& Model)
{
	FElysiumCardHasher Hasher;
	Hasher.Add(Model.Positions);
	for (const TPair<FString, TArray<int32>>& Group : Model.Groups)
	{
		Hasher.Add(Group.Value);
	}
	return Hasher.Finalize();
}

UStaticMesh* FElysiumStaticMeshBuilder::Build(const FElysiumObjModel& Model, const FString& Dir,
	bool bConvexCollision, UObject* Outer, FElysiumTextureCache& Cache,
	const TArray<TArray<FVector>>* ConvexHulls, const FPropCards* Cards)
{
	if (Model.Positions.Num() == 0)
	{
		return nullptr;
	}

	FMeshDescription MeshDesc;
	FStaticMeshAttributes Attr(MeshDesc);
	Attr.Register();
	Attr.GetVertexInstanceUVs().SetNumChannels(1);

	TVertexAttributesRef<FVector3f> Positions = Attr.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector2f> UVs = Attr.GetVertexInstanceUVs();
	TPolygonGroupAttributesRef<FName> SlotNames = Attr.GetPolygonGroupMaterialSlotNames();

	// Shared vertex positions (the OBJ's global vertex list; groups index into it).
	MeshDesc.ReserveNewVertices(Model.Positions.Num());
	TArray<FVertexID> Verts;
	Verts.Reserve(Model.Positions.Num());
	for (const FVector& P : Model.Positions)
	{
		const FVertexID V = MeshDesc.CreateVertex();
		Positions[V] = FVector3f(P);
		Verts.Add(V);
	}

	// One polygon group (= material slot + render section) per OBJ material, in a stable
	// order so the StaticMaterials array below lines up by slot name.
	struct FSlot { FName Name; UMaterialInterface* Mat; };
	TArray<FSlot> Slots;
	for (const TPair<FString, TArray<int32>>& Group : Model.Groups)
	{
		const TArray<int32>& Idx = Group.Value;
		if (Idx.Num() < 3)
		{
			continue;
		}
		const FPolygonGroupID PG = MeshDesc.CreatePolygonGroup();
		const FName SlotName(*Group.Key);
		SlotNames[PG] = SlotName;

		const FElysiumMaterialDef* Def = Model.Materials.Find(Group.Key);
		Slots.Add({ SlotName, FElysiumMaterialFactory::Build(Def, Dir, Outer, Cache) });

		for (int32 T = 0; T + 2 < Idx.Num(); T += 3)
		{
			FVertexInstanceID Tri[3];
			for (int32 K = 0; K < 3; ++K)
			{
				const int32 GI = Idx[T + K];
				const FVertexID V = Verts.IsValidIndex(GI) ? Verts[GI] : Verts[0];
				const FVertexInstanceID Inst = MeshDesc.CreateVertexInstance(V);
				UVs.Set(Inst, 0, Model.Uvs.IsValidIndex(GI) ? FVector2f(Model.Uvs[GI]) : FVector2f::ZeroVector);
				Tri[K] = Inst;
			}
			MeshDesc.CreatePolygon(PG, { Tri[0], Tri[1], Tri[2] });
		}
	}

	if (MeshDesc.Polygons().Num() == 0)
	{
		return nullptr;
	}

	// The fast build does not recompute normals/tangents (that path is editor-only), so the
	// OBJ — which carries neither — needs them derived from geometry + UVs here. The per-vertex
	// pass reads per-triangle NTBs, so the triangle pass must run first (else it asserts).
	FStaticMeshOperations::ComputeTriangleTangentsAndNormals(MeshDesc);
	FStaticMeshOperations::ComputeTangentsAndNormals(MeshDesc,
		EComputeNTBsFlags::Normals | EComputeNTBsFlags::Tangents | EComputeNTBsFlags::WeightedNTBs);

	UStaticMesh* Mesh = NewObject<UStaticMesh>(Outer, NAME_None, RF_Transient);
	Mesh->SetLightingGuid();
	for (const FSlot& S : Slots)
	{
		Mesh->GetStaticMaterials().Add(FStaticMaterial(S.Mat, S.Name, S.Name));
	}

	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bFastBuild = true;              // required outside the editor
	Params.bBuildSimpleCollision = false;  // convex added manually below
	Params.bCommitMeshDescription = false;
	Params.bMarkPackageDirty = false;
	Params.bUseHashAsGuid = true;
	// A -ElysiumCards run needs the built LOD readable on the CPU: the card builder ray-traces
	// the index/vertex buffers. Off in every other run, where it would be dead memory.
	Params.bAllowCpuAccess = ElysiumCardBake::IsBaking();

	const TArray<const FMeshDescription*> Descs = { &MeshDesc };
	Mesh->BuildFromMeshDescriptions(Descs, Params);

	if (LumenCardsEnabled())
	{
		// Baked cards follow the real surface; the bounds fallback boxes the mesh. A prop is
		// small enough that the box is usually close, but the bake is still the better fit.
		const uint64 Hash = Cards ? HashPropGeometry(Model) : 0;
		const bool bBaked = Cards && Cards->Store
			&& Cards->Store->InstallProp(Cards->Stem, Hash, Mesh);
		if (!bBaked)
		{
			AttachLumenCards(Mesh);
		}
		if (Cards && Cards->BakeItems)
		{
			FElysiumCardBakeItem& Item = Cards->BakeItems->AddDefaulted_GetRef();
			Item.Stem = Cards->Stem;
			Item.Hash = Hash;
			Item.Mesh = Mesh;
		}
	}

	// Solid props: cook convex collision. Physics props (8.4) pass a decomposed hull set —
	// one FKConvexElem per part, a tighter fit for concave shapes than a single hull; every
	// other solid prop (and a physics prop whose decomposition is absent) gets one hull of the
	// whole model (matches the Godot prop collision). Cooked now so instances collide/simulate
	// without an offline cook step.
	if (bConvexCollision)
	{
		Mesh->CreateBodySetup();
		if (UBodySetup* BS = Mesh->GetBodySetup())
		{
			BS->CollisionTraceFlag = CTF_UseSimpleAsComplex;
			auto AddHull = [BS](const TArray<FVector>& Verts)
			{
				FKConvexElem Convex;
				Convex.VertexData = Verts;
				Convex.UpdateElemBox();
				BS->AggGeom.ConvexElems.Add(MoveTemp(Convex));
			};
			if (ConvexHulls && ConvexHulls->Num() > 0)
			{
				for (const TArray<FVector>& Hull : *ConvexHulls)
				{
					if (Hull.Num() >= 4)
					{
						AddHull(Hull);
					}
				}
			}
			if (BS->AggGeom.ConvexElems.Num() == 0)   // no sidecar / all hulls degenerate → single hull
			{
				AddHull(Model.Positions);
			}
			BS->InvalidatePhysicsData();
			BS->CreatePhysicsMeshes();
		}
	}

	return Mesh;
}

bool FElysiumStaticMeshBuilder::LoadConvexHulls(const FString& Path, TArray<TArray<FVector>>& Out)
{
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *Path))
	{
		return false;
	}
	// One convex hull per line: flat Unreal-cm verts (x y z x y z ...), >= 4 verts, order
	// irrelevant — the cooker builds the hull. Same format the world collider reads (LoadHulls).
	for (const FString& Line : Lines)
	{
		TArray<FString> Tok;
		Line.ParseIntoArray(Tok, TEXT(" "), true);
		if (Tok.Num() < 12 || Tok.Num() % 3 != 0)
		{
			continue;
		}
		TArray<FVector> Verts;
		Verts.Reserve(Tok.Num() / 3);
		for (int32 I = 0; I + 2 < Tok.Num(); I += 3)
		{
			Verts.Emplace(FCString::Atod(*Tok[I]), FCString::Atod(*Tok[I + 1]), FCString::Atod(*Tok[I + 2]));
		}
		Out.Add(MoveTemp(Verts));
	}
	return Out.Num() > 0;
}
