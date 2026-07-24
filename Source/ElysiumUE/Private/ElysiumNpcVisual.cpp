#include "ElysiumNpcVisual.h"

#include "ElysiumContentPaths.h"

#include "glTFRuntimeAsset.h"
#include "glTFRuntimeFunctionLibrary.h"
#include "glTFRuntimeParser.h"

#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/Paths.h"

namespace ElysiumNpcVisual
{
	USkeletalMesh* LoadMesh(const FString& Stem, UglTFRuntimeAsset*& OutAsset, FString& OutError)
	{
		OutAsset = nullptr;
		OutError.Reset();

		const FString FullPath = FElysiumContentPaths::NpcGlb(Stem);
		if (!FPaths::FileExists(FullPath))
		{
			OutError = FString::Printf(TEXT("not found: %s"), *FullPath);
			return nullptr;
		}

		// Default config: SceneScale 100 (m->cm), TransformBaseType::Default, bAllowExternalFiles so the
		// sibling tex/*.png resolve relative to the .glb — the raw glb mdl_gltf.py writes loads 1:1.
		FglTFRuntimeConfig Config;
		UglTFRuntimeAsset* Asset = UglTFRuntimeFunctionLibrary::glTFLoadAssetFromFilename(FullPath, false, Config);
		if (Asset == nullptr)
		{
			OutError = TEXT("glTFRuntime could not parse the .glb");
			return nullptr;
		}

		FglTFRuntimeSkeletalMeshConfig SkeletalMeshConfig;
		USkeletalMesh* Mesh = Asset->LoadSkeletalMesh(0, 0, SkeletalMeshConfig);
		if (Mesh == nullptr)
		{
			OutError = TEXT("LoadSkeletalMesh(mesh 0, skin 0) returned null");
			return nullptr;
		}

		OutAsset = Asset;
		return Mesh;
	}

	UAnimSequence* LoadIdleAnim(UglTFRuntimeAsset* Asset, USkeletalMesh* Mesh, FString& OutAppliedName)
	{
		OutAppliedName.Reset();
		if (Asset == nullptr || Mesh == nullptr)
		{
			return nullptr;
		}

		const TArray<FString> AnimNames = Asset->GetAnimationsNames(true);
		for (const FString& Name : AnimNames)
		{
			if (Name.Contains(TEXT("idle"), ESearchCase::IgnoreCase))
			{
				FglTFRuntimeSkeletalAnimationConfig AnimConfig;
				if (UAnimSequence* Anim = Asset->LoadSkeletalAnimationByName(Mesh, Name, AnimConfig, /*bCaseSensitive=*/false))
				{
					OutAppliedName = Name;
					return Anim;
				}
			}
		}
		return nullptr;   // no idle clip — caller leaves the mesh in its reference pose
	}
}
