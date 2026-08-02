#include "Visual/ElysiumNpcVisual.h"

#include "ElysiumContentPaths.h"

#include "glTFRuntimeAsset.h"
#include "glTFRuntimeFunctionLibrary.h"
#include "glTFRuntimeParser.h"

#include "Animation/AnimSequence.h"
#include "Animation/MorphTarget.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#include "UObject/UObjectGlobals.h"
#include "Misc/Paths.h"

namespace
{
	void ConfigureRetarget(UglTFRuntimeAsset* Asset, USkeletalMesh* Mesh,
		FglTFRuntimeSkeletalAnimationConfig& Config)
	{
		const FReferenceSkeleton& TargetRef = Mesh->GetRefSkeleton();
		if (TargetRef.GetNum() == 0)
		{
			return;
		}

		// The current importer binds sparse donor tracks by bone name. Do not enable glTFRuntime's
		// generic rest-pose retargeter: cinematic roots carry absolute placement inside the scene,
		// so it expands a normal 1-3 m body into a 4-8 m pose or double-applies Bip01 placement.
		// This is only the current loading policy, not VtMB's complete virtual-model contract:
		// retail evaluates donor-bind fallback and an optional outer position map. See
		// animation_and_movers.md, "The shared animation library".
		//
		// Banks may carry optional hair, toe and nub tracks absent from a particular body. Filter
		// those tracks before animation construction; the target leaves those bones at bind pose.
		const TArray<FglTFRuntimeNode> SourceNodes = Asset->GetNodes();
		for (const FglTFRuntimeNode& Node : SourceNodes)
		{
			const FName NodeName(*Node.Name);
			if (!Node.Name.IsEmpty() && TargetRef.FindBoneIndex(NodeName) == INDEX_NONE)
			{
				Config.RemoveTracks.AddUnique(Node.Name);
			}
		}
	}

	// Declare every morph target the mesh came back with as a morph-target *curve* on its skeleton
	// (12.3). An anim curve only reaches USkeletalMeshComponent::ActiveMorphTargets when the bone
	// container flags it, and the bone container takes those flags from this metadata — so without
	// this registration the facial track evaluates correctly and moves nothing.
	void RegisterMorphTargetCurves(USkeletalMesh* Mesh)
	{
		USkeleton* Skeleton = Mesh ? Mesh->GetSkeleton() : nullptr;
		if (Skeleton == nullptr)
		{
			return;
		}
		for (const TObjectPtr<UMorphTarget>& Morph : Mesh->GetMorphTargets())
		{
			if (Morph != nullptr)
			{
				Skeleton->AccumulateCurveMetaData(Morph->GetFName(), /*bMaterialSet=*/false,
					/*bMorphtargetSet=*/true);
			}
		}
	}
}

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
		ConfigureRetarget(Asset, Mesh, AnimConfig);
		UAnimSequence* Anim = Asset->LoadSkeletalAnimationByName(Mesh, ClipName, AnimConfig,
			/*bCaseSensitive=*/false);
		if (Anim == nullptr)
		{
			OutError = FString::Printf(TEXT("clip '%s' not in the asset"), *ClipName);
		}
		return Anim;
	}

	USkeletalMesh* LoadMeshFromPath(const FString& FullPath, UglTFRuntimeAsset*& OutAsset,
		FString& OutError, bool bPlayerMaterial)
	{
		OutAsset = nullptr;
		OutError.Reset();

		UglTFRuntimeAsset* Asset = LoadAssetFromPath(FullPath, OutError);
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
		// A face is not one mesh: `AU27Z` on `nines` moves head, molar, lower-teeth, tongue and a
		// neck-seam vertex across five of the model's eight material primitives. glTF weights are
		// mesh-level, so the bake writes the same target list on every primitive and a target that
		// spans two materials arrives as one same-named piece per primitive. Merge stitches those
		// pieces into one UMorphTarget; the plugin default, Ignore, keeps the first piece and
		// silently drops the rest — a jaw that moves and leaves its teeth behind.
		SkeletalMeshConfig.MorphTargetsDuplicateStrategy = EglTFRuntimeMorphTargetsDuplicateStrategy::Merge;
		if (bPlayerMaterial)
		{
			UMaterialInterface* BodyMaterial = LoadObject<UMaterialInterface>(nullptr,
				TEXT("/Game/VtMB/Materials/M_PlayerBody.M_PlayerBody"));
			if (BodyMaterial == nullptr)
			{
				OutError = TEXT("M_PlayerBody is missing; run: uv run elysium export bundle policy");
				return nullptr;
			}
			for (uint8 Raw = static_cast<uint8>(EglTFRuntimeMaterialType::Opaque);
				Raw <= static_cast<uint8>(EglTFRuntimeMaterialType::TwoSidedMasked); ++Raw)
			{
				SkeletalMeshConfig.MaterialsConfig.UberMaterialsOverrideMap.Add(
					static_cast<EglTFRuntimeMaterialType>(Raw), BodyMaterial);
			}
		}
		USkeletalMesh* Mesh = Asset->LoadSkeletalMesh(0, 0, SkeletalMeshConfig);
		if (Mesh == nullptr)
		{
			OutError = TEXT("LoadSkeletalMesh(mesh 0, skin 0) returned null");
			return nullptr;
		}
		RegisterMorphTargetCurves(Mesh);

		OutAsset = Asset;
		return Mesh;
	}

	USkeletalMesh* LoadMesh(const FString& Stem, UglTFRuntimeAsset*& OutAsset, FString& OutError,
		bool bPlayerMaterial)
	{
		return LoadMeshFromPath(FElysiumContentPaths::NpcGlb(Stem), OutAsset, OutError, bPlayerMaterial);
	}

}
