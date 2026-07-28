#include "Visual/ElysiumNpcVisual.h"

#include "ElysiumContentPaths.h"

#include "glTFRuntimeAsset.h"
#include "glTFRuntimeFunctionLibrary.h"
#include "glTFRuntimeParser.h"

#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/Paths.h"

namespace ElysiumNpcVisual
{
	UglTFRuntimeAsset* LoadAssetFromPath(const FString& FullPath, FString& OutError)
	{
		OutError.Reset();
		if (!FPaths::FileExists(FullPath))
		{
			OutError = FString::Printf(TEXT("not found: %s"), *FullPath);
			return nullptr;
		}
		// Default config: SceneScale 100 (m->cm), TransformBaseType::Default, bAllowExternalFiles so
		// the sibling tex/*.png resolve relative to the .glb — the raw glb mdl_gltf.py writes loads
		// 1:1. A bank carries no materials, so only the basis/scale half of this applies to one.
		FglTFRuntimeConfig Config;
		UglTFRuntimeAsset* Asset = UglTFRuntimeFunctionLibrary::glTFLoadAssetFromFilename(FullPath, false, Config);
		if (Asset == nullptr)
		{
			OutError = FString::Printf(TEXT("glTFRuntime could not parse %s"), *FPaths::GetCleanFilename(FullPath));
		}
		return Asset;
	}

	UAnimSequence* RetargetClip(UglTFRuntimeAsset* Asset, USkeletalMesh* Mesh, const FString& ClipName,
		FString& OutError)
	{
		OutError.Reset();
		if (Asset == nullptr || Mesh == nullptr || ClipName.IsEmpty())
		{
			OutError = TEXT("null asset/mesh or empty clip name");
			return nullptr;
		}
		FglTFRuntimeSkeletalAnimationConfig AnimConfig;
		UAnimSequence* Anim = Asset->LoadSkeletalAnimationByName(Mesh, ClipName, AnimConfig,
			/*bCaseSensitive=*/false);
		if (Anim == nullptr)
		{
			OutError = FString::Printf(TEXT("clip '%s' not in the asset"), *ClipName);
		}
		return Anim;
	}

	USkeletalMesh* LoadMesh(const FString& Stem, UglTFRuntimeAsset*& OutAsset, FString& OutError)
	{
		OutAsset = nullptr;
		OutError.Reset();

		UglTFRuntimeAsset* Asset = LoadAssetFromPath(FElysiumContentPaths::NpcGlb(Stem), OutError);
		if (Asset == nullptr)
		{
			return nullptr;
		}

		// The exporter guarantees every weighted joint slot maps to skin.joints and every skin joint
		// reaches the declared skeleton root. Keep glTFRuntime strict here: silently dropping an
		// influence hides exporter damage as a warped model and used to make skeleton bugs intermittent.
		// Elysium.Content.SkeletalGlbContracts validates the same contract over the complete export.
		FglTFRuntimeSkeletalMeshConfig SkeletalMeshConfig;
		SkeletalMeshConfig.bIgnoreMissingBones = false;
		USkeletalMesh* Mesh = Asset->LoadSkeletalMesh(0, 0, SkeletalMeshConfig);
		if (Mesh == nullptr)
		{
			OutError = TEXT("LoadSkeletalMesh(mesh 0, skin 0) returned null");
			return nullptr;
		}

		OutAsset = Asset;
		return Mesh;
	}

}
