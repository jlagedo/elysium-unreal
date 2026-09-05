#include "ElysiumStaticMeshPrecisionLibrary.h"

#if WITH_EDITOR
#include "Engine/StaticMesh.h"
#include "StaticMeshCompiler.h"
#include "StaticMeshResources.h"

namespace
{
	FString CheckInput(UStaticMesh* Mesh, int32 ExpectedLodCount)
	{
		if (!IsInGameThread()) return TEXT("static precision requires the editor game thread");
		if (!Mesh) return TEXT("static precision requires a mesh");
		if (ExpectedLodCount <= 0) return TEXT("static precision requires a positive source LOD count");
		return FString();
	}

	FString CheckPrecision(const UStaticMesh* Mesh, int32 ExpectedLodCount)
	{
		if (Mesh->IsCompiling()) return TEXT("static precision verification requires completed compilation");
		if (Mesh->GetNumSourceModels() != ExpectedLodCount)
			return FString::Printf(TEXT("static precision source LOD count is %d, expected %d"), Mesh->GetNumSourceModels(), ExpectedLodCount);
		for (int32 Lod = 0; Lod < ExpectedLodCount; ++Lod)
		{
			const FMeshBuildSettings& Settings = Mesh->GetSourceModel(Lod).BuildSettings;
			if (!Settings.bUseFullPrecisionUVs || !Settings.bUseHighPrecisionTangentBasis)
				return FString::Printf(TEXT("static source LOD %d lacks full UV/high tangent precision"), Lod);
		}
		const FStaticMeshRenderData* Render = Mesh->GetRenderData();
		if (!Render || Render->LODResources.Num() != ExpectedLodCount)
			return TEXT("static precision render LOD inventory is absent or differs from source");
		for (int32 Lod = 0; Lod < ExpectedLodCount; ++Lod)
		{
			const auto& Vertices = Render->LODResources[Lod].VertexBuffers.StaticMeshVertexBuffer;
			if (!Vertices.GetUseFullPrecisionUVs() || !Vertices.GetUseHighPrecisionTangentBasis())
				return FString::Printf(TEXT("static render LOD %d lacks full UV/high tangent precision"), Lod);
		}
		return FString();
	}
}
#endif

FString UElysiumStaticMeshPrecisionLibrary::VerifyPrecision(UStaticMesh* Mesh, int32 ExpectedLodCount)
{
#if WITH_EDITOR
	const FString InputError = CheckInput(Mesh, ExpectedLodCount);
	if (!InputError.IsEmpty()) return InputError;
	FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
	return CheckPrecision(Mesh, ExpectedLodCount);
#else
	return TEXT("static precision verification is editor only");
#endif
}

FString UElysiumStaticMeshPrecisionLibrary::ApplyPrecision(UStaticMesh* Mesh, int32 ExpectedLodCount)
{
#if WITH_EDITOR
	const FString InputError = CheckInput(Mesh, ExpectedLodCount);
	if (!InputError.IsEmpty()) return InputError;
	// Finish both this mesh and reverse dependents before modifying their source data.
	FStaticMeshCompilingManager::FFinishCompilationOptions FinishOptions;
	FinishOptions.bIncludeDependentMeshes = true;
	FStaticMeshCompilingManager::Get().FinishCompilation({Mesh}, FinishOptions);
	if (Mesh->GetNumSourceModels() != ExpectedLodCount)
		return FString::Printf(TEXT("static precision source LOD count is %d, expected %d"), Mesh->GetNumSourceModels(), ExpectedLodCount);
	if (!Mesh->IsMeshDescriptionValid(0))
		return TEXT("static precision rebuild requires a valid source mesh description at LOD 0");
	if (CheckPrecision(Mesh, ExpectedLodCount).IsEmpty()) return FString();

	TArray<FMeshBuildSettings> ExpectedSettings;
	ExpectedSettings.Reserve(ExpectedLodCount);
	const FMeshNaniteSettings NaniteBefore = Mesh->GetNaniteSettings();
	const bool bAutoLodBefore = Mesh->GetAutoComputeLODScreenSize();
	Mesh->Modify();
	for (int32 Lod = 0; Lod < ExpectedLodCount; ++Lod)
	{
		FMeshBuildSettings& Settings = Mesh->GetSourceModel(Lod).BuildSettings;
		Settings.bUseFullPrecisionUVs = true;
		Settings.bUseHighPrecisionTangentBasis = true;
		ExpectedSettings.Add(Settings);
	}

	// Direct Build manages resource release/fences and component render state itself. Unlike
	// StaticMeshEditorSubsystem::SetLodBuildSettings, it never opens/closes an asset window.
	// Supplying OutErrors makes this build synchronous; finish explicitly again for readback.
	TArray<FText> BuildErrors;
	UStaticMesh::FBuildParameters Parameters;
	Parameters.bInSilent = true;
	Parameters.OutErrors = &BuildErrors;
	Parameters.bInRebuildUVChannelData = false;
	Parameters.bInEnforceLightmapRestrictions = false;
	Mesh->Build(Parameters);
	FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
	if (!BuildErrors.IsEmpty())
	{
		TArray<FString> Errors;
		for (const FText& Error : BuildErrors) Errors.Add(Error.ToString());
		return TEXT("static precision rebuild failed: ") + FString::Join(Errors, TEXT("; "));
	}
	const FString PrecisionError = CheckPrecision(Mesh, ExpectedLodCount);
	if (!PrecisionError.IsEmpty()) return PrecisionError;
	for (int32 Lod = 0; Lod < ExpectedLodCount; ++Lod)
		if (!(Mesh->GetSourceModel(Lod).BuildSettings == ExpectedSettings[Lod]))
			return FString::Printf(TEXT("static precision rebuild changed unrelated LOD %d build settings"), Lod);
	if (!(Mesh->GetNaniteSettings() == NaniteBefore) || Mesh->GetAutoComputeLODScreenSize() != bAutoLodBefore)
		return TEXT("static precision rebuild changed Nanite or automatic LOD settings");
	Mesh->MarkPackageDirty();
	return FString();
#else
	return TEXT("static precision authoring is editor only");
#endif
}
