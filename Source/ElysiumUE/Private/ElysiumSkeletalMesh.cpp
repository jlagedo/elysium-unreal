#include "ElysiumSkeletalMesh.h"

#if WITH_EDITOR
#include "Animation/MorphTarget.h"
#include "MeshDescription.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/StaticMeshVertexBuffer.h"
#include "SkeletalMeshAttributes.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumSkeletalMesh, Log, All);

FString UElysiumSkeletalMesh::BuildDerivedDataKey(const ITargetPlatform* TargetPlatform)
{
	return Super::BuildDerivedDataKey(TargetPlatform) + TEXT("_ElysiumAuthoredGeometry_3");
}

void UElysiumSkeletalMesh::BuildLODModel(FSkeletalMeshRenderData& RenderData, const ITargetPlatform* TargetPlatform, int32 LODIndex)
{
	Super::BuildLODModel(RenderData, TargetPlatform, LODIndex);
	// This hook runs before FSkeletalMeshRenderData serializes morph rows to DDC or builds
	// platform render resources. It also runs on a cook/DDC miss, so retention survives a rebuild.
	FString Error = RestoreAuthoredBasis(LODIndex);
	if (Error.IsEmpty()) Error = RestoreAuthoredMorphs(LODIndex);
	MorphBuildErrors.Add(LODIndex, Error);
	if (!Error.IsEmpty()) UE_LOG(LogElysiumSkeletalMesh, Warning, TEXT("%s LOD%d: %s"), *GetPathName(), LODIndex, *Error);
}

FString UElysiumSkeletalMesh::GetMorphBuildError() const
{
	for (const auto& Row : MorphBuildErrors) if (!Row.Value.IsEmpty()) return Row.Value;
	return FString();
}

FString UElysiumSkeletalMesh::RestoreAuthoredBasis(int32 LODIndex)
{
	const auto* Info = GetLODInfo(LODIndex);
	const FMeshDescription* Description = GetMeshDescription(LODIndex);
	// Older products retain their old build policy until they are reimported from TANG.
	if (!Info || Info->BuildSettings.bRecomputeTangents || !Description) return FString();
	if (!Info->BuildSettings.bUseHighPrecisionTangentBasis) return TEXT("authored basis requires 16-bit tangent storage");
	FSkeletalMeshModel* Model = GetImportedModel();
	if (!Model || !Model->LODModels.IsValidIndex(LODIndex)) return TEXT("authored basis has no built LOD");
	auto& Lod = Model->LODModels[LODIndex];
	if (Lod.MeshToImportVertexMap.Num() != int32(Lod.NumVertices)) return TEXT("authored basis render/source map is incomplete");
	const FSkeletalMeshConstAttributes Attributes(*Description);
	const auto Normals = Attributes.GetVertexInstanceNormals();
	const auto Tangents = Attributes.GetVertexInstanceTangents();
	const auto Signs = Attributes.GetVertexInstanceBinormalSigns();
	if (!Normals.IsValid() || !Tangents.IsValid() || !Signs.IsValid()) return TEXT("authored basis attributes are absent");
	for (auto& Section : Lod.Sections)
	{
		if (Section.SoftVertices.Num() != int32(Section.NumVertices)) return TEXT("authored basis section vertices are incomplete");
		Section.bRecomputeTangent = false;
		for (int32 Local = 0; Local < Section.SoftVertices.Num(); ++Local)
		{
			const uint32 RenderVertex = Section.BaseVertexIndex + Local;
			if (!Lod.MeshToImportVertexMap.IsValidIndex(RenderVertex)) return TEXT("authored basis render vertex is invalid");
			const FVertexID SourceVertex(Lod.MeshToImportVertexMap[RenderVertex]);
			if (!Description->Vertices().IsValid(SourceVertex)) return TEXT("authored basis source vertex is invalid");
			const auto Instances = Description->GetVertexVertexInstanceIDs(SourceVertex);
			if (Instances.IsEmpty()) return TEXT("authored basis source vertex has no corners");
			const FVector3f X = Tangents[Instances[0]], Z = Normals[Instances[0]];
			const float Sign = Signs[Instances[0]];
			if (X.ContainsNaN() || Z.ContainsNaN() || (Sign != -1.f && Sign != 1.f)) return TEXT("authored basis is nonfinite or has invalid handedness");
			for (const auto Instance : Instances)
				if (Tangents[Instance] != X || Normals[Instance] != Z || Signs[Instance] != Sign)
					return TEXT("source vertex corners disagree on their authored basis");
			// SetVertexTangents infers W from a determinant instead of copying TangentZ.W.
			// Choose an intermediate input only when the actual engine packer proves identical
			// XYZ bits and the original W. Source TANG and MeshDescription remain unchanged.
			const FPackedRGBA16N PackedX(X), PackedZ(FVector4f(Z.X, Z.Y, Z.Z, Sign));
			FVector3f CodecX, CodecY;
			const auto TryCodecInput = [&](const FVector3f& Candidate)
			{
				if (FPackedRGBA16N(Candidate) != PackedX) return false;
				const FVector3f Y(FVector3d::CrossProduct(FVector3d(Z), FVector3d(Candidate)) * double(Sign));
				TStaticMeshVertexTangentDatum<FPackedRGBA16N> Encoded;
				Encoded.SetTangents(Candidate, Y, Z);
				if (Encoded.TangentX != PackedX || Encoded.TangentZ != PackedZ) return false;
				CodecX = Candidate; CodecY = Y;
				return true;
			};
			bool Encoded = TryCodecInput(X);
			const FVector3f Center = PackedX.ToFVector3f();
			if (!Encoded) Encoded = TryCodecInput(Center);
			// 32767 * 2^-17 is strictly inside the zero quantization cell. Each candidate
			// still must pass native packed-byte equality; this is never a comparison tolerance.
			constexpr float CellStep = 1.f / 131072.f;
			for (int32 Axis = 0; !Encoded && Axis < 3; ++Axis)
				for (const float Direction : {1.f, -1.f})
				{
					FVector3f Candidate = Center;
					Candidate[Axis] += Direction * CellStep;
					if (TryCodecInput(Candidate)) { Encoded = true; break; }
				}
			if (!Encoded) return TEXT("no lossless native tangent codec input preserves XYZ and handedness");
			auto& Vertex = Section.SoftVertices[Local];
			Vertex.TangentX = CodecX;
			Vertex.TangentY = CodecY;
			Vertex.TangentZ = FVector4f(Z.X, Z.Y, Z.Z, Sign);
		}
	}
	return FString();
}

FString UElysiumSkeletalMesh::RestoreAuthoredMorphs(int32 LODIndex)
{
	const FMeshDescription* Description = GetMeshDescription(LODIndex);
	// Generated LODs have no independent authored attributes to restore.
	if (!Description) return FString();
	const FSkeletalMeshConstAttributes Attributes(*Description);
	const auto Names = Attributes.GetMorphTargetNames();
	if (Names.IsEmpty()) return FString();
	const FSkeletalMeshModel* Model = GetImportedModel();
	if (!Model || !Model->LODModels.IsValidIndex(LODIndex)) return TEXT("authored morphs have no built LOD");
	const FSkeletalMeshLODModel& Lod = Model->LODModels[LODIndex];
	if (Lod.MeshToImportVertexMap.Num() != int32(Lod.NumVertices)) return TEXT("authored morphs have no complete render/source map");
	TArray<int32> SectionForVertex;
	SectionForVertex.Init(INDEX_NONE, Lod.NumVertices);
	for (int32 Section = 0; Section < Lod.Sections.Num(); ++Section)
	{
		const auto& Row = Lod.Sections[Section];
		for (uint32 Vertex = Row.BaseVertexIndex; Vertex < Row.BaseVertexIndex + Row.NumVertices; ++Vertex)
		{
			if (!SectionForVertex.IsValidIndex(Vertex) || SectionForVertex[Vertex] != INDEX_NONE)
				return TEXT("authored morphs encounter overlapping or invalid render sections");
			SectionForVertex[Vertex] = Section;
		}
	}
	TMap<FName, FMorphTargetLODModel> Restored;
	for (const FName Name : Names)
	{
		const auto Positions = Attributes.GetVertexMorphPositionDelta(Name);
		const auto Normals = Attributes.GetVertexInstanceMorphNormalDelta(Name);
		if (!Positions.IsValid() || !Normals.IsValid()) return TEXT("authored morph attributes are absent: ") + Name.ToString();
		FMorphTargetLODModel& Target = Restored.Add(Name);
		Target.NumBaseMeshVerts = Lod.NumVertices;
		Target.bGeneratedByEngine = false;
		for (int32 RenderVertex = 0; RenderVertex < Lod.MeshToImportVertexMap.Num(); ++RenderVertex)
		{
			const FVertexID SourceVertex(Lod.MeshToImportVertexMap[RenderVertex]);
			if (!Description->Vertices().IsValid(SourceVertex) || SectionForVertex[RenderVertex] == INDEX_NONE)
				return TEXT("authored morph source vertex or render section is absent");
			const auto Instances = Description->GetVertexVertexInstanceIDs(SourceVertex);
			if (Instances.IsEmpty()) return TEXT("authored morph source vertex has no triangle corner");
			const FVector3f Position = Positions[SourceVertex];
			const FVector3f Normal = Normals[Instances[0]];
			for (const FVertexInstanceID Instance : Instances)
				if (Normals[Instance] != Normal) return TEXT("morph normals differ across source vertex corners: ") + Name.ToString();
			if (Position.ContainsNaN() || Normal.ContainsNaN()) return TEXT("nonfinite authored morph delta");
			// Explicit zero records remain in the GLB ledger. Every nonzero component reaches
			// the render copy, including normal-only values below UE's stock 0.01 squared gate.
			if (Position != FVector3f::ZeroVector || Normal != FVector3f::ZeroVector)
			{
				FMorphTargetDelta& Delta = Target.Vertices.AddDefaulted_GetRef();
				Delta.SourceIdx = RenderVertex;
				Delta.PositionDelta = Position;
				Delta.TangentZDelta = Normal;
				Target.SectionIndices.AddUnique(SectionForVertex[RenderVertex]);
			}
		}
		Target.NumVertices = Target.Vertices.Num();
	}
	// Validate all attributes before replacing any native morph data.
	for (auto& Row : Restored)
	{
		UMorphTarget* Morph = FindObject<UMorphTarget>(this, *Row.Key.ToString());
		if (!Morph) Morph = NewObject<UMorphTarget>(this, Row.Key);
		auto& Lods = Morph->GetMorphLODModels();
		if (Lods.Num() <= LODIndex) Lods.SetNum(LODIndex + 1);
		Lods[LODIndex] = MoveTemp(Row.Value);
		Morph->BaseSkelMesh = this;
		if (!GetMorphTargets().Contains(Morph)) GetMorphTargets().Add(Morph);
		Morph->ClearInternalFlags(EInternalObjectFlags::Async);
	}
	InitMorphTargets(/*bInKeepEmptyMorphTargets=*/true);
	return FString();
}
#endif
