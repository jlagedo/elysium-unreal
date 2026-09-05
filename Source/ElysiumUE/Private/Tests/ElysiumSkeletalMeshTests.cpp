#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "ElysiumSkeletalMesh.h"
#include "Animation/MorphTarget.h"
#include "MeshDescription.h"
#include "Misc/AutomationTest.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/StaticMeshVertexBuffer.h"
#include "SkeletalMeshAttributes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumAuthoredMorphRetention,
	"Elysium.Substrate.AuthoredMorphs.NormalOnlyAndRenderCopies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FElysiumAuthoredMorphRetention::RunTest(const FString& Parameters)
{
	auto* Mesh = NewObject<UElysiumSkeletalMesh>();
	Mesh->AddLODInfo();
	auto* Lod = new FSkeletalMeshLODModel();
	Mesh->GetImportedModel()->LODModels.Add(Lod);
	Lod->NumVertices = 4;
	Lod->MeshToImportVertexMap = {0, 1, 0, 2};
	auto& Section = Lod->Sections.AddDefaulted_GetRef();
	Section.BaseVertexIndex = 0; Section.NumVertices = 4;
	FMeshDescription* Description = Mesh->CreateMeshDescription(0);
	FSkeletalMeshAttributes Attributes(*Description);
	Attributes.Register();
	const FName Name(TEXT("normal_only"));
	Attributes.RegisterMorphTargetAttribute(Name, true);
	const FVector3f SourceNormal(0.0000001f, -0.0000002f, 0.f);
	auto Normals = Attributes.GetVertexInstanceMorphNormalDelta(Name);
	TArray<FVertexInstanceID> Instances;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const FVertexID Vertex = Description->CreateVertex();
		Instances.Add(Description->CreateVertexInstance(Vertex));
		Normals[Instances.Last()] = Index == 0 ? SourceNormal : FVector3f::ZeroVector;
	}
	const auto ExtraCorner = Description->CreateVertexInstance(FVertexID(0));
	Normals[ExtraCorner] = SourceNormal;
	// The engine may replace zero tangents while building its soft-vertex array.
	// Restoration must retain the authored basis at every source/render copy.
	Section.SoftVertices.SetNum(4);
	Mesh->GetLODInfo(0)->BuildSettings.bRecomputeTangents = false;
	Mesh->GetLODInfo(0)->BuildSettings.bUseHighPrecisionTangentBasis = true;
	auto BaseNormals = Attributes.GetVertexInstanceNormals();
	auto BaseTangents = Attributes.GetVertexInstanceTangents();
	auto Signs = Attributes.GetVertexInstanceBinormalSigns();
	for (const auto Instance : Description->VertexInstances().GetElementIDs())
	{
		const bool Zero = Description->GetVertexInstanceVertex(Instance) == FVertexID(0);
		BaseNormals[Instance] = FVector3f::ZAxisVector;
		BaseTangents[Instance] = Zero ? FVector3f::ZeroVector : FVector3f::XAxisVector;
		Signs[Instance] = Zero ? -1.f : 1.f;
	}
	TestTrue(TEXT("authored soft-vertex basis restores"), Mesh->RestoreAuthoredBasis(0).IsEmpty());
	for (const int32 Index : {0, 2})
	{
		const auto& Vertex = Section.SoftVertices[Index];
		TStaticMeshVertexTangentDatum<FPackedRGBA16N> Packed;
		Packed.SetTangents(Vertex.TangentX, Vertex.TangentY, FVector3f(Vertex.TangentZ));
		TestTrue(TEXT("native codec keeps zero XYZ exactly in each render copy"), Packed.TangentX == FPackedRGBA16N(FVector3f::ZeroVector));
		TestTrue(TEXT("native codec retains original negative handedness"), Packed.TangentZ == FPackedRGBA16N(FVector4f(0.f,0.f,1.f,-1.f)));
	}
	TestEqual(TEXT("soft-vertex source handedness retained"), Section.SoftVertices[0].TangentZ.W, -1.f);
	TestTrue(TEXT("nonzero authored basis retained"), Section.SoftVertices[1].TangentY == FVector3f::YAxisVector);
	// Establish the concrete engine filtering defect, independent of the restoration helper.
	auto* Stock = NewObject<UMorphTarget>();
	FMorphTargetDelta Source;
	Source.SourceIdx = 0; Source.PositionDelta = FVector3f::ZeroVector; Source.TangentZDelta = SourceNormal;
	Stock->PopulateDeltas({Source}, 0, Lod->Sections, true, false, 0.f);
	TestEqual(TEXT("stock filter discards the nonzero normal-only row"), Stock->GetMorphLODModels()[0].Vertices.Num(), 0);
	TestTrue(TEXT("authored restoration succeeds"), Mesh->RestoreAuthoredMorphs(0).IsEmpty());
	const auto* Morph = Mesh->FindMorphTarget(Name);
	if (!TestNotNull(TEXT("normal-only target is registered"), Morph)) return false;
	const auto& Rows = Morph->GetMorphLODModels()[0].Vertices;
	if (!TestEqual(TEXT("both render copies retain the row"), Rows.Num(), 2)) return false;
	TestEqual(TEXT("first render copy"), Rows[0].SourceIdx, uint32(0));
	TestEqual(TEXT("second render copy"), Rows[1].SourceIdx, uint32(2));
	for (const auto& Row : Rows)
	{
		TestTrue(TEXT("normal delta retained exactly"), Row.TangentZDelta == SourceNormal);
		TestTrue(TEXT("no invented position delta"), Row.PositionDelta == FVector3f::ZeroVector);
	}
	Normals[ExtraCorner] = FVector3f(0.f, 1.f, 0.f);
	TestFalse(TEXT("conflicting corner data refuses restoration"), Mesh->RestoreAuthoredMorphs(0).IsEmpty());
	TestTrue(TEXT("failed preflight preserves previous native data"), Morph->GetMorphLODModels()[0].Vertices[0].TangentZDelta == SourceNormal);
	return true;
}
#endif
