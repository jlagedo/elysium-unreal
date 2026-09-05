#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Editor/ElysiumStaticMeshPrecisionLibrary.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "MeshDescription.h"
#include "Misc/AutomationTest.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshCompiler.h"
#include "StaticMeshResources.h"

namespace
{
	UStaticMesh* PrecisionMesh(int32 LodCount)
	{
		auto* Mesh = NewObject<UStaticMesh>();
		Mesh->bAllowCPUAccess = true;
		Mesh->SetNumSourceModels(LodCount);
		Mesh->SetAutoComputeLODScreenSize(false);
		Mesh->GetStaticMaterials().Add(FStaticMaterial(UMaterial::GetDefaultMaterial(MD_Surface), FName(TEXT("body"))));
		Mesh->CreateBodySetup();
		Mesh->GetBodySetup()->CollisionTraceFlag = CTF_UseSimpleAndComplex;
		Mesh->GetBodySetup()->bDoubleSidedGeometry = true;
		auto& Box = Mesh->GetBodySetup()->AggGeom.BoxElems.AddDefaulted_GetRef();
		Box.X = 10.f; Box.Y = 11.f; Box.Z = 12.f;
		for (int32 Lod = 0; Lod < LodCount; ++Lod)
		{
			auto& Source = Mesh->GetSourceModel(Lod);
			Source.BuildSettings.bRecomputeNormals = false;
			Source.BuildSettings.bRecomputeTangents = false;
			Source.BuildSettings.bRemoveDegenerates = false;
			Source.BuildSettings.bGenerateLightmapUVs = false;
			Source.BuildSettings.bUseBackwardsCompatibleF16TruncUVs = true;
			Source.ScreenSize.Default = 1.f / float(Lod + 1);
			FMeshDescription* Description = Mesh->CreateMeshDescription(Lod);
			FStaticMeshAttributes Attributes(*Description);
			Attributes.Register();
			Attributes.GetVertexInstanceUVs().SetNumChannels(1);
			const FPolygonGroupID Group = Description->CreatePolygonGroup();
			Attributes.GetPolygonGroupMaterialSlotNames()[Group] = TEXT("body");
			TArray<FVertexInstanceID> Corners;
			const FVector3f Positions[] = {{0.f, 0.f, 0.f}, {100.f, 0.f, 0.f}, {0.f, 100.f, 0.f}};
			for (int32 Index = 0; Index < 3; ++Index)
			{
				const FVertexID Vertex = Description->CreateVertex();
				Attributes.GetVertexPositions()[Vertex] = Positions[Index];
				const auto Corner = Description->CreateVertexInstance(Vertex);
				Attributes.GetVertexInstanceNormals()[Corner] = FVector3f(0.f, 0.f, 1.f);
				Attributes.GetVertexInstanceTangents()[Corner] = FVector3f(1.f, 0.f, 0.f);
				Attributes.GetVertexInstanceBinormalSigns()[Corner] = 1.f;
				Attributes.GetVertexInstanceColors()[Corner] = FVector4f(1.f, 1.f, 1.f, 1.f);
				Attributes.GetVertexInstanceUVs().Set(Corner, 0, FVector2f(-1.097e24f, 1.e21f));
				Corners.Add(Corner);
			}
			Description->CreatePolygon(Group, Corners);
			Mesh->CommitMeshDescription(Lod);
		}
		return Mesh;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumStaticPrecisionInvalidInputs,
	"Elysium.Content.StaticMeshPrecision.InvalidInputs", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FElysiumStaticPrecisionInvalidInputs::RunTest(const FString&)
{
	TestFalse(TEXT("null mesh refused"), UElysiumStaticMeshPrecisionLibrary::ApplyPrecision(nullptr, 1).IsEmpty());
	TestFalse(TEXT("null verification refused"), UElysiumStaticMeshPrecisionLibrary::VerifyPrecision(nullptr, 1).IsEmpty());
	auto* Mesh = NewObject<UStaticMesh>(); Mesh->SetNumSourceModels(1);
	TestFalse(TEXT("wrong source count refused"), UElysiumStaticMeshPrecisionLibrary::ApplyPrecision(Mesh, 2).IsEmpty());
	TestFalse(TEXT("missing source description refused"), UElysiumStaticMeshPrecisionLibrary::ApplyPrecision(Mesh, 1).IsEmpty());
	TestFalse(TEXT("invalid call did not modify precision"), Mesh->GetSourceModel(0).BuildSettings.bUseFullPrecisionUVs);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumStaticPrecisionAllLods,
	"Elysium.Content.StaticMeshPrecision.AllLodsAndFiniteUVs", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FElysiumStaticPrecisionAllLods::RunTest(const FString&)
{
	auto* Mesh = PrecisionMesh(3);
	TArray<FMeshBuildSettings> Expected;
	for (int32 Lod = 0; Lod < 3; ++Lod)
	{
		auto Settings = Mesh->GetSourceModel(Lod).BuildSettings;
		Settings.bUseFullPrecisionUVs = Settings.bUseHighPrecisionTangentBasis = true;
		Expected.Add(Settings);
	}
	const auto Nanite = Mesh->GetNaniteSettings();
	const auto* Body = Mesh->GetBodySetup();
	TestFalse(TEXT("verify does not silently build"), UElysiumStaticMeshPrecisionLibrary::VerifyPrecision(Mesh, 3).IsEmpty());
	const FString Error = UElysiumStaticMeshPrecisionLibrary::ApplyPrecision(Mesh, 3);
	if (!TestTrue(*(TEXT("precision build succeeds: ") + Error), Error.IsEmpty())) return false;
	TestFalse(TEXT("compilation complete before returning"), Mesh->IsCompiling());
	TestTrue(TEXT("native precision verifies"), UElysiumStaticMeshPrecisionLibrary::VerifyPrecision(Mesh, 3).IsEmpty());
	TestTrue(TEXT("Nanite settings unchanged"), Mesh->GetNaniteSettings() == Nanite);
	TestFalse(TEXT("automatic LOD flag unchanged"), Mesh->GetAutoComputeLODScreenSize());
	TestTrue(TEXT("collision body retained"), Mesh->GetBodySetup() == Body);
	TestTrue(TEXT("collision mode unchanged"), Body->CollisionTraceFlag == CTF_UseSimpleAndComplex);
	TestTrue(TEXT("double sided collision unchanged"), Body->bDoubleSidedGeometry);
	TestEqual(TEXT("simple hull retained"), Body->AggGeom.BoxElems.Num(), 1);
	TestEqual(TEXT("simple hull size retained"), Body->AggGeom.BoxElems[0].X, 10.f);
	for (int32 Lod = 0; Lod < 3; ++Lod)
	{
		TestTrue(TEXT("only the two build settings changed"), Mesh->GetSourceModel(Lod).BuildSettings == Expected[Lod]);
		TestEqual(TEXT("source screen size retained"), Mesh->GetSourceModel(Lod).ScreenSize.Default, 1.f / float(Lod + 1));
		const auto& Buffer = Mesh->GetRenderData()->LODResources[Lod].VertexBuffers.StaticMeshVertexBuffer;
		if (!TestTrue(TEXT("CPU UV evidence present"), Buffer.GetTexCoordData() != nullptr)) return false;
		if (!TestTrue(TEXT("triangle has render vertices"), Buffer.GetNumVertices() >= 3)) return false;
		for (uint32 Vertex = 0; Vertex < Buffer.GetNumVertices(); ++Vertex)
		{
			const auto UV = Buffer.GetVertexUV(Vertex, 0);
			TestTrue(TEXT("large UV remains finite"), FMath::IsFinite(UV.X) && FMath::IsFinite(UV.Y));
			TestEqual(TEXT("U is not clamped or half rounded"), UV.X, -1.097e24f);
			TestEqual(TEXT("V is not clamped or half rounded"), UV.Y, 1.e21f);
		}
	}
	TestTrue(TEXT("reapplication is valid"), UElysiumStaticMeshPrecisionLibrary::ApplyPrecision(Mesh, 3).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumStaticPrecisionStaleRender,
	"Elysium.Content.StaticMeshPrecision.StaleRenderBuffers", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FElysiumStaticPrecisionStaleRender::RunTest(const FString&)
{
	auto* Mesh = PrecisionMesh(1);
	if (!TestTrue(TEXT("initial precision build"), UElysiumStaticMeshPrecisionLibrary::ApplyPrecision(Mesh, 1).IsEmpty())) return false;
	// Produce a valid low-precision buffer, then change only the source declaration. Do not
	// flip the buffer flag in isolation: that would misdescribe the allocated tangent storage.
	Mesh->GetSourceModel(0).BuildSettings.bUseHighPrecisionTangentBasis = false;
	TArray<FText> BuildErrors;
	UStaticMesh::FBuildParameters Parameters;
	Parameters.bInSilent = true; Parameters.OutErrors = &BuildErrors;
	Mesh->Build(Parameters);
	FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
	if (!TestTrue(TEXT("low-precision fixture builds"), BuildErrors.IsEmpty())) return false;
	Mesh->GetSourceModel(0).BuildSettings.bUseHighPrecisionTangentBasis = true;
	TestFalse(TEXT("verify detects stale built precision"), UElysiumStaticMeshPrecisionLibrary::VerifyPrecision(Mesh, 1).IsEmpty());
	TestTrue(TEXT("apply repairs built data even with source flags already set"), UElysiumStaticMeshPrecisionLibrary::ApplyPrecision(Mesh, 1).IsEmpty());
	return true;
}
#endif
