// Lumen card baking, editor side (docs/lumen-coverage-spike.md).
//
// A runtime-built UStaticMesh has no card representation, so Lumen drops it from the surface
// cache. `FElysiumStaticMeshBuilder::AttachLumenCards` stands in six cards on the mesh bounds,
// which covers a prop but not architecture: a box card captures only the nearest surface along
// its axis, so anything behind it reads as missing coverage.
//
// The real builder is `IMeshUtilities::GenerateCardRepresentationData` — surfel clustering that
// fits up to MaxLumenMeshCards to the actual surface. It ray-traces the mesh through Embree, so
// it exists only in the editor. This file is the probe for driving it on a mesh *constructed in
// code* rather than imported as an asset, which is the one unknown behind baking cards offline
// into a sidecar the runtime deserializes.

#include "CoreMinimal.h"

#if ELYSIUM_WITH_CARDGEN

#include "Engine/StaticMesh.h"
#include "HAL/IConsoleManager.h"
#include "MeshCardBuild.h"
#include "MeshDescription.h"
#include "MeshUtilities.h"
#include "Modules/ModuleManager.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"
#include "StaticMeshResources.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumCardGen, Log, All);

namespace
{
	// A closed box with an open front, i.e. the shape our world chunks actually are: surfaces
	// facing several ways with interior faces a bounds card cannot see. If the builder fits cards
	// to these, it will fit them to a chunk.
	FMeshDescription BuildProbeMesh()
	{
		FMeshDescription Mesh;
		FStaticMeshAttributes Attr(Mesh);
		Attr.Register();
		Attr.GetVertexInstanceUVs().SetNumChannels(1);

		TVertexAttributesRef<FVector3f> Positions = Attr.GetVertexPositions();
		TVertexInstanceAttributesRef<FVector2f> UVs = Attr.GetVertexInstanceUVs();
		TPolygonGroupAttributesRef<FName> SlotNames = Attr.GetPolygonGroupMaterialSlotNames();

		const FPolygonGroupID PG = Mesh.CreatePolygonGroup();
		SlotNames[PG] = TEXT("Probe");

		auto Quad = [&](const FVector3f& A, const FVector3f& B, const FVector3f& C, const FVector3f& D)
		{
			FVertexInstanceID Inst[4];
			const FVector3f Corner[4] = { A, B, C, D };
			for (int32 K = 0; K < 4; ++K)
			{
				const FVertexID V = Mesh.CreateVertex();
				Positions[V] = Corner[K];
				Inst[K] = Mesh.CreateVertexInstance(V);
				UVs.Set(Inst[K], 0, FVector2f(K == 1 || K == 2 ? 1.f : 0.f, K >= 2 ? 1.f : 0.f));
			}
			Mesh.CreatePolygon(PG, { Inst[0], Inst[1], Inst[2] });
			Mesh.CreatePolygon(PG, { Inst[0], Inst[2], Inst[3] });
		};

		const float S = 500.f;   // 5 m room, VtMB-ish scale
		// floor, ceiling, back, left, right — front left open
		Quad({ -S, -S, -S }, { S, -S, -S }, { S, S, -S }, { -S, S, -S });
		Quad({ -S, -S,  S }, { -S, S,  S }, { S, S,  S }, { S, -S,  S });
		Quad({ -S, S, -S }, { S, S, -S }, { S, S, S }, { -S, S, S });
		Quad({ -S, -S, -S }, { -S, S, -S }, { -S, S, S }, { -S, -S, S });
		Quad({ S, -S, -S }, { S, -S, S }, { S, S, S }, { S, S, -S });

		FStaticMeshOperations::ComputeTriangleTangentsAndNormals(Mesh);
		FStaticMeshOperations::ComputeTangentsAndNormals(Mesh,
			EComputeNTBsFlags::Normals | EComputeNTBsFlags::Tangents | EComputeNTBsFlags::WeightedNTBs);
		return Mesh;
	}

	void ProbeCardGen(const TArray<FString>& Args)
	{
		const int32 MaxCards = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 12;

		FMeshDescription MeshDesc = BuildProbeMesh();

		UStaticMesh* Mesh = NewObject<UStaticMesh>(GetTransientPackage(), NAME_None, RF_Transient);
		Mesh->SetLightingGuid();
		Mesh->GetStaticMaterials().Add(FStaticMaterial(nullptr, TEXT("Probe"), TEXT("Probe")));

		UStaticMesh::FBuildMeshDescriptionsParams Params;
		Params.bFastBuild = true;              // the runtime-safe path the world chunks use
		Params.bBuildSimpleCollision = false;
		Params.bCommitMeshDescription = false;
		Params.bMarkPackageDirty = false;
		Params.bUseHashAsGuid = true;
		// The card builder reads the LOD's index/vertex buffers on the CPU.
		Params.bAllowCpuAccess = true;

		const TArray<const FMeshDescription*> Descs = { &MeshDesc };
		Mesh->BuildFromMeshDescriptions(Descs, Params);

		FStaticMeshRenderData* RenderData = Mesh->GetRenderData();
		if (!RenderData || RenderData->LODResources.Num() == 0)
		{
			UE_LOG(LogElysiumCardGen, Error, TEXT("probe: BuildFromMeshDescriptions produced no LOD"));
			return;
		}

		const FStaticMeshLODResources& LOD = RenderData->LODResources[0];
		UE_LOG(LogElysiumCardGen, Log, TEXT("probe: mesh built - %d verts, %d indices, %d sections"),
			LOD.VertexBuffers.PositionVertexBuffer.GetNumVertices(),
			LOD.IndexBuffer.GetNumIndices(), LOD.Sections.Num());

		TArray<FSignedDistanceFieldBuildSectionData> SectionData;
		SectionData.SetNum(FMath::Max(1, LOD.Sections.Num()));
		for (FSignedDistanceFieldBuildSectionData& SD : SectionData)
		{
			SD.BlendMode = BLEND_Opaque;
			SD.bTwoSided = false;
			SD.bAffectDistanceFieldLighting = true;
		}

		FMeshDataForDerivedDataTask MeshData;
		MeshData.SourceMeshData = nullptr;
		MeshData.LODModel = &LOD;
		MeshData.SectionData = SectionData;
		MeshData.Bounds = (FBoxSphereBounds3f)RenderData->Bounds;

		IMeshUtilities& MeshUtilities = FModuleManager::Get().LoadModuleChecked<IMeshUtilities>(TEXT("MeshUtilities"));

		FCardRepresentationData CardData;
		const double Start = FPlatformTime::Seconds();
		// No distance field: card generation treats it as optional and falls back to the mesh
		// bounds (MeshCardRepresentationUtilities.cpp), which is what lets us skip the DF build.
		const bool bOk = MeshUtilities.GenerateCardRepresentationData(
			TEXT("ElysiumCardProbe"),
			MeshData,
			/*DistanceFieldVolumeData=*/nullptr,
			MaxCards,
			/*bGenerateAsIfTwoSided=*/false,
			CardData);
		const double Ms = (FPlatformTime::Seconds() - Start) * 1000.0;

		if (!bOk)
		{
			UE_LOG(LogElysiumCardGen, Error, TEXT("probe: GenerateCardRepresentationData FAILED (%.1f ms)"), Ms);
			return;
		}

		const FMeshCardsBuildData& Build = CardData.MeshCardsBuildData;
		UE_LOG(LogElysiumCardGen, Log,
			TEXT("probe: OK - %d cards (max %d) in %.1f ms; bounds %s; twoSided %d"),
			Build.CardBuildData.Num(), MaxCards, Ms, *Build.Bounds.ToString(), Build.bMostlyTwoSided ? 1 : 0);

		for (int32 I = 0; I < Build.CardBuildData.Num(); ++I)
		{
			const FLumenCardBuildData& Card = Build.CardBuildData[I];
			UE_LOG(LogElysiumCardGen, Log, TEXT("  card %d: dir %d, origin (%.0f %.0f %.0f), extent (%.0f %.0f %.0f)"),
				I, Card.AxisAlignedDirectionIndex,
				Card.OBB.Origin.X, Card.OBB.Origin.Y, Card.OBB.Origin.Z,
				Card.OBB.Extent.X, Card.OBB.Extent.Y, Card.OBB.Extent.Z);
		}
	}

	FAutoConsoleCommand GProbeCardGen(
		TEXT("elysium.cards.probe"),
		TEXT("Build a runtime-style UStaticMesh and run the editor Lumen card builder on it. "
		     "Optional arg: max cards (default 12). Editor builds only."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ProbeCardGen));
}

#endif   // ELYSIUM_WITH_CARDGEN
