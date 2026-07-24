#include "ElysiumStaticMesh.h"

#include "ElysiumMaterialFactory.h"
#include "ElysiumObjModel.h"

#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "MeshDescription.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"

UStaticMesh* FElysiumStaticMeshBuilder::Build(const FElysiumObjModel& Model, const FString& Dir,
	bool bConvexCollision, UObject* Outer, const TArray<TArray<FVector>>* ConvexHulls)
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
		Slots.Add({ SlotName, FElysiumMaterialFactory::Build(Def, Dir, Outer) });

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

	const TArray<const FMeshDescription*> Descs = { &MeshDesc };
	Mesh->BuildFromMeshDescriptions(Descs, Params);

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
