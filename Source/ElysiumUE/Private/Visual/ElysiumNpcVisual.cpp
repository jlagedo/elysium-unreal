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
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"

// The simulated-garment spike (docs/architecture/asset-enhancement.md). VtMB has no cloth solver at
// all — a skirt or coat is skinned rigidly to one bone and never moves — so this adds motion the
// original never had rather than reproducing any. It selects a side-by-side enhanced mesh built by
// pipeline/src/elysium_pipeline/enhancement/cloth_spike.py; the faithful npc/<stem>.glb is never
// written and is what loads whenever this is off or the enhanced pair is absent.
//
// Default 1 is an explicit owner call for the spike and diverges from the enhancement layer's
// otherwise-uniform "opt in" default, which the blast radius makes affordable: the toggle selects
// nothing on a stem the spike did not build, and npc/cloth/ holds two models.
//
// Applied at map load; NPC meshes and their rigs are resolved once per map epoch.
// The character bake's A/B (ANM1). 1 loads the cast from the /ElysiumBaked mount — one shared
// skeleton, a mesh per model, a UAnimSequence per clip, all built offline by
// pipeline/unreal/bake_characters.py; 0 keeps the glTFRuntime load that builds the same objects at
// map load. Both paths exist for one cycle so a wrong pose can be diagnosed by flipping one switch
// rather than by bisecting a migration.
//
// Default 0 until the baked slice passes its path-equality test. A stem the bake has not covered
// falls back to glTFRuntime with the toggle on, the same shape as `elysium.Cloth` and
// `elysium.EnhancedTextures`: the switch selects between two sets and never turns a present model
// into a missing one. Applied at map load, like every other mesh-resolution toggle.
static TAutoConsoleVariable<int32> CVarBakedCharacters(
	TEXT("elysium.BakedCharacters"), 0,
	TEXT("Load characters from the baked /ElysiumBaked mount (1) or build them from .glb through "
		 "glTFRuntime at map load (0). Applied at map load."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarCloth(
	TEXT("elysium.Cloth"), 1,
	TEXT("Simulate garments on the models the cloth spike built (1) or wear the faithful rigid "
		 "mesh (0). Applied at map load."),
	ECVF_Default);

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

}

namespace ElysiumNpcVisual
{
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
				// AccumulateCurveMetaData adds a missing entry with transactions enabled. That is
				// correct for an editor import, but these skeletons are transient runtime objects and
				// can be created while GEditor has no transaction buffer; the default path then calls
				// through a null GEditor and crashes before the first NPC stands. Add without a
				// transaction, then set the same metadata bit directly.
				const FName CurveName = Morph->GetFName();
				Skeleton->AddCurveMetaData(CurveName, /*bTransact=*/false);
				if (FCurveMetaData* MetaData = Skeleton->GetCurveMetaData(CurveName))
				{
					MetaData->Type.bMorphtarget = true;
				}
			}
		}
	}
}

namespace
{
	// The one config every .glb in this namespace is parsed with. Held so the basis and scale a
	// sidecar has to follow are read back off the loader rather than restated beside it.
	const FglTFRuntimeConfig& AssetConfig()
	{
		static const FglTFRuntimeConfig Config;
		return Config;
	}
}

namespace ElysiumNpcVisual
{
	// FglTFRuntimeParser::GetNodeTransform, applied to something that is not a node: conjugate by
	// the scene basis, then scale the translation. A rule's `pos`/`quat` entries are the same kind
	// of quantity as a bone's own local, so they take the same treatment and land in the same space.
	FMatrix AssetImportBasis(bool bBaked)
	{
		// A sidecar states its geometry in the glb's own basis, once, whichever path builds the
		// body -- so which frame it has to be carried into is a property of the body, not of the
		// sidecar. glTFRuntime imports under its own Default basis. The baked assets are written
		// by `UE_mdl_skeletal.py` in the repo's canonical Source->Unreal frame, and that frame is
		// exactly what glTFRuntime calls YForward: the composite of the exporter's Source->glTF
		// rotation inverted with `bsp.source_to_unreal`. The two differ by a 90 degree yaw, which
		// is visible on nothing until the two are mixed.
		static const FMatrix Canonical = FBasisVectorMatrix(
			FVector(1.0, 0.0, 0.0), FVector(0.0, 0.0, 1.0), FVector(0.0, 1.0, 0.0),
			FVector::ZeroVector);
		return bBaked ? Canonical : AssetConfig().GetMatrix();
	}

	FTransform ImportGlbLocal(const FTransform& GlbLocal, bool bBaked)
	{
		const FMatrix Basis = AssetImportBasis(bBaked);
		FTransform Imported(Basis.Inverse() * GlbLocal.ToMatrixWithScale() * Basis);
		Imported.ScaleTranslation(AssetConfig().SceneScale);
		return Imported;
	}

	FVector ImportGlbDirection(const FVector& GlbDirection, bool bBaked)
	{
		return AssetImportBasis(bBaked).TransformVector(GlbDirection);
	}

	float ImportGlbScale()
	{
		return AssetConfig().SceneScale;
	}

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
		const FglTFRuntimeConfig& Config = AssetConfig();
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

	UMaterialInterface* EyeMaster()
	{
		return LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/VtMB/Materials/M_Eyes.M_Eyes"));
	}

	USkeletalMesh* LoadMeshFromPath(const FString& FullPath, UglTFRuntimeAsset*& OutAsset,
		FString& OutError, bool bPlayerMaterial, const TArray<FString>* EyeMaterials)
	{
		FElysiumGlbMeshOptions Options;
		Options.bPlayerMaterial = bPlayerMaterial;
		Options.EyeMaterials = EyeMaterials;
		return LoadMeshFromPath(FullPath, OutAsset, OutError, Options);
	}

	USkeletalMesh* LoadMeshFromPath(const FString& FullPath, UglTFRuntimeAsset*& OutAsset,
		FString& OutError, const FElysiumGlbMeshOptions& Options)
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
		// The bake's two knobs. Outer puts the built objects in a package instead of the transient
		// one; Skeleton hands every body the same skeleton asset and merges its bones into that
		// tree, while `bOverwriteRefSkeleton` stays false so the mesh keeps its OWN reference
		// skeleton — VtMB bodies differ in proportion and a shared reference pose would reshape
		// every character that is not the donor.
		SkeletalMeshConfig.Outer = Options.Outer;
		SkeletalMeshConfig.Skeleton = Options.Skeleton;
		SkeletalMeshConfig.bMergeAllBonesToBoneTree = Options.Skeleton != nullptr;
		if (Options.bPlayerMaterial)
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
		// The eye sections are drawn with M_Eyes rather than the plugin's uber material, keyed by
		// the glTF material name the exporter wrote (`eyeball_l` / `eyeball_r`).
		//
		// `bMaterialsOverrideMapInjectParams` is not optional and its absence fails silently: with
		// it false the override short-circuits and returns the bare master, so the section gets no
		// material instance and therefore no sclera texture — white eyes. With it true glTFRuntime
		// builds a MID over M_Eyes and injects `baseColorTexture` and the glTF factors, which is
		// what the master is written to receive. `UberMaterialsOverrideMap` cannot be used here: it
		// keys on material *type*, not on the section.
		if (Options.EyeMaterials != nullptr && !Options.EyeMaterials->IsEmpty())
		{
			if (UMaterialInterface* Master = EyeMaster())
			{
				SkeletalMeshConfig.MaterialsConfig.bMaterialsOverrideMapInjectParams = true;
				for (const FString& Name : *Options.EyeMaterials)
				{
					SkeletalMeshConfig.MaterialsConfig.MaterialsOverrideByNameMap.Add(Name, Master);
				}
			}
			else
			{
				UE_LOG(LogTemp, Warning,
					TEXT("M_Eyes is missing; run: uv run elysium export bundle policy"));
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

	bool UseClothMesh(const FString& Stem)
	{
		// Necessary but not sufficient, the same shape as `elysium.EnhancedTextures` over `tex_hi/`:
		// the toggle selects between two sets, and a stem the spike never built silently keeps the
		// faithful one rather than failing to load. Deleting npc/cloth/ reverts the spike whether
		// the cvar is on or not.
		return CVarCloth.GetValueOnAnyThread() != 0
			&& FPaths::FileExists(FElysiumContentPaths::NpcClothGlb(Stem))
			&& FPaths::FileExists(FElysiumContentPaths::NpcClothRig(Stem));
	}

	bool UseBakedCharacters()
	{
		return CVarBakedCharacters.GetValueOnAnyThread() != 0;
	}

	USkeletalMesh* LoadBakedMesh(const FString& Stem, bool bPlayerMaterial)
	{
		return LoadObject<USkeletalMesh>(nullptr,
			*FElysiumContentPaths::BakedCharacterMesh(Stem, bPlayerMaterial));
	}

	bool IsStemBaked(const FString& Stem)
	{
		// Exactly the branch `LoadMesh` takes, answered without loading the package -- the sidecar
		// rigs are built per stem and must be carried into the frame the body actually landed in,
		// so the two decisions have to agree or an eye aims ninety degrees off a correct head.
		return UseBakedCharacters() && !UseClothMesh(Stem)
			&& FPackageName::DoesPackageExist(
				FPackageName::ObjectPathToPackageName(
					FElysiumContentPaths::BakedCharacterMesh(Stem)));
	}

	bool IsBakedMesh(const USkeletalMesh* Mesh)
	{
		// The same question `LoadBakedClip` asks, and for the same reason: the mesh was built
		// against exactly one skeleton, so the skeleton's name is the only answer that cannot
		// disagree with the body that actually loaded.
		const USkeleton* Skeleton = Mesh != nullptr ? Mesh->GetSkeleton() : nullptr;
		return Skeleton != nullptr
			&& !FElysiumContentPaths::BakedCharacterFamily(Skeleton->GetName()).IsEmpty();
	}

	bool IsBakedClip(const UAnimSequence* Sequence)
	{
		// Asked of the package the sequence lives in, for the same reason IsBakedMesh asks the
		// skeleton: a glTFRuntime-built sequence is a transient object with no package on the
		// mount, so the two cannot be confused however the toggle is set.
		const UPackage* Package = Sequence != nullptr ? Sequence->GetPackage() : nullptr;
		return Package != nullptr
			&& Package->GetName().StartsWith(FElysiumContentPaths::BakedMount() + TEXT("/"));
	}

	UAnimSequence* LoadBakedClip(const USkeletalMesh* Mesh, const FString& Owner,
		const FString& ClipName)
	{
		// The family comes off the body's own skeleton rather than a second lookup table: the mesh
		// was baked against exactly one family's USkeleton, and a sequence is bound to that same
		// asset, so asking the mesh is the only answer that cannot disagree with what will actually
		// play. A mesh built through glTFRuntime answers empty and takes no baked clip.
		const USkeleton* Skeleton = Mesh != nullptr ? Mesh->GetSkeleton() : nullptr;
		if (Skeleton == nullptr)
		{
			return nullptr;
		}
		const FString Family = FElysiumContentPaths::BakedCharacterFamily(Skeleton->GetName());
		if (Family.IsEmpty())
		{
			return nullptr;
		}
		return LoadObject<UAnimSequence>(nullptr,
			*FElysiumContentPaths::BakedCharacterAnim(Family, Owner, ClipName));
	}

	USkeletalMesh* LoadMesh(const FString& Stem, UglTFRuntimeAsset*& OutAsset, FString& OutError,
		bool bPlayerMaterial, const TArray<FString>* EyeMaterials)
	{
		// The garment spike is deliberately not baked: it selects a *different* .glb, and the rig
		// that drives its lattice is installed from the same predicate the mesh choice comes from.
		// A baked body under an enhanced rig is a set of chains naming bones the skeleton does not
		// have, so a cloth stem stays on the loader that can give it the enhanced mesh.
		if (UseBakedCharacters() && !UseClothMesh(Stem))
		{
			if (USkeletalMesh* Baked = LoadBakedMesh(Stem, bPlayerMaterial))
			{
				// No parsed asset on this path, and none is needed: a baked clip is addressed by
				// its owner and label rather than pulled out of the body's own glb. Every caller
				// already handles a null asset, because a bank-owned clip never had one.
				OutAsset = nullptr;
				OutError.Reset();
				return Baked;
			}
			// Not baked yet — the slice is a subset of the cast. Fall through rather than fail: the
			// toggle selects between two sets and must not turn a present model into a missing one.
		}
		const FString Path = UseClothMesh(Stem)
			? FElysiumContentPaths::NpcClothGlb(Stem)
			: FElysiumContentPaths::NpcGlb(Stem);
		return LoadMeshFromPath(Path, OutAsset, OutError, bPlayerMaterial, EyeMaterials);
	}

}
