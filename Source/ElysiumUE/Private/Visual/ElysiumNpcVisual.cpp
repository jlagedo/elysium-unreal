#include "Visual/ElysiumNpcVisual.h"

#include "ElysiumContentPaths.h"

#include "glTFRuntimeAsset.h"
#include "glTFRuntimeFunctionLibrary.h"
#include "glTFRuntimeParser.h"

#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Animation/MorphTarget.h"
#include "Animation/Skeleton.h"
#include "ChaosClothAsset/ClothAsset.h"
#include "ChaosClothAsset/ClothComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectGlobals.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"

// The legacy simulated-garment spike (docs/architecture/asset-enhancement.md). Retail garment
// motion is a StudioRender particle solve carried by the model, independently from its hair/body
// bone-chain solver. The approximation artifacts are built by
// pipeline/src/elysium_pipeline/enhancement/cloth_spike.py, but the current baked-character path
// does not select them; every body comes from the mount.
//
// Applied at map load; NPC meshes and their rigs are resolved once per map epoch.

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
	FMatrix AssetImportBasis()
	{
		// A sidecar states its geometry in the glb's own basis, and the body it is fitted to is
		// built by `UE_mdl_skeletal.py` in the repo's canonical Source->Unreal frame -- which is
		// exactly what glTFRuntime calls YForward: the composite of the exporter's Source->glTF
		// rotation inverted with `bsp.source_to_unreal`. Carrying a sidecar into any other frame
		// yaws it 90 degrees, which is visible on nothing until an eye or a driven bone aims
		// sideways off an otherwise correct head.
		static const FMatrix Canonical = FBasisVectorMatrix(
			FVector(1.0, 0.0, 0.0), FVector(0.0, 0.0, 1.0), FVector(0.0, 1.0, 0.0),
			FVector::ZeroVector);
		return Canonical;
	}

	FTransform ImportGlbLocal(const FTransform& GlbLocal)
	{
		const FMatrix Basis = AssetImportBasis();
		FTransform Imported(Basis.Inverse() * GlbLocal.ToMatrixWithScale() * Basis);
		Imported.ScaleTranslation(AssetConfig().SceneScale);
		return Imported;
	}

	FVector ImportGlbDirection(const FVector& GlbDirection)
	{
		return AssetImportBasis().TransformVector(GlbDirection);
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

	/**
	 * Stop the body drawing the surface the garment now simulates.
	 *
	 * VtMB does not add a garment to a character — it SUBSTITUTES simulated positions into the
	 * body's own render vertices, so the skirt is drawn once either way. Here the garment is a
	 * separate component drawing the same surface, so without this the body's skinned copy stays
	 * on screen underneath and the character wears two skirts: one that moves and one that does
	 * not.
	 *
	 * Removing the whole section is right BECAUSE the garment redraws the whole section: its
	 * render patterns carry the material's entire surface, and only the vertices VtMB substitutes
	 * take their position from the solver. What is left behind on the body would be a duplicate of
	 * every vertex, not just of the moving ones.
	 *
	 * Matched on the material object rather than on a name, because the generated cloth asset
	 * references the body's own material instance — the same pointer, so a rename cannot
	 * desynchronise the two halves.
	 */
	void HideGarmentSectionsOnBody(USkeletalMeshComponent* Body, const UChaosClothAsset& Asset)
	{
		const USkinnedAsset* const Skinned = Body->GetSkinnedAsset();
		const FSkeletalMeshRenderData* const Render =
			Skinned != nullptr ? Skinned->GetResourceForRendering() : nullptr;
		if (Render == nullptr)
		{
			return;
		}

		TSet<const UMaterialInterface*> Garment;
		for (const FSkeletalMaterial& Slot : Asset.GetMaterials())
		{
			if (Slot.MaterialInterface != nullptr)
			{
				Garment.Add(Slot.MaterialInterface);
			}
		}
		if (Garment.IsEmpty())
		{
			return;
		}

		const TArray<FSkeletalMaterial>& BodyMaterials = Skinned->GetMaterials();
		for (int32 Lod = 0; Lod < Render->LODRenderData.Num(); ++Lod)
		{
			const TArray<FSkelMeshRenderSection>& Sections = Render->LODRenderData[Lod].RenderSections;
			for (int32 Section = 0; Section < Sections.Num(); ++Section)
			{
				const int32 Material = Sections[Section].MaterialIndex;
				if (BodyMaterials.IsValidIndex(Material)
					&& Garment.Contains(BodyMaterials[Material].MaterialInterface))
				{
					Body->ShowMaterialSection(Material, Section, /*bShow*/ false, Lod);
				}
			}
		}
	}

	/**
	 * Destroy garments this owner is still carrying that no longer follow a body.
	 *
	 * A garment component belongs to the OWNING ACTOR, and the map actor owns every NPC body on
	 * the map — so rebuilding one body leaves its garment behind, parented to an actor that is
	 * still alive. The leader pose is a weak reference, so the leftover does not error: it keeps
	 * simulating, against a bind pose, and draws wherever the identity transform puts it. Several
	 * rebuilds and a character has one skirt on her hips and a pile of them on the floor.
	 *
	 * Only the previous body's garments are removed. The others belong to bodies that are still
	 * standing, and one owner holds all of them.
	 */
	void SweepStaleGarments(AActor* Owner, const USkeletalMeshComponent* Body)
	{
		TArray<UChaosClothComponent*> Garments;
		Owner->GetComponents(Garments);
		for (UChaosClothComponent* Garment : Garments)
		{
			// Two ways to be stale, and both happen on a model swap: the body this garment
			// followed was destroyed, or the body is being rebuilt and is about to be dressed
			// again. Either way what is here now is the previous model's.
			if (!Garment->LeaderPoseComponent.IsValid()
				|| Garment->LeaderPoseComponent.Get() == Body)
			{
				Garment->DestroyComponent();
			}
		}
	}

	UChaosClothComponent* InstallGarment(USkeletalMeshComponent* Body, const FString& Stem)
	{
		AActor* const Owner = Body != nullptr ? Body->GetOwner() : nullptr;
		if (Owner == nullptr)
		{
			return nullptr;
		}
		// FIRST, and unconditionally. Every reason this function has for declining to dress a body
		// -- no garment on the model, no stem -- is a reason the PREVIOUS body's garment still
		// needs taking away. Sweeping only on the path that installs one means a model swap cleans
		// up exactly when the next model also happens to wear something, and leaves the old skirt
		// hanging in the air the rest of the time.
		SweepStaleGarments(Owner, Body);

		if (Stem.IsEmpty())
		{
			return nullptr;
		}
		// Absence is the ordinary answer: 60 of 4,445 installed models author a garment at all, so
		// this is a soft load rather than a resolve-or-fail. LoadObject logs nothing on a miss.
		const FString AssetPath = FString::Printf(
			TEXT("/Game/VtMB/Cloth/CLOTH_%s.CLOTH_%s"), *Stem, *Stem);
		UChaosClothAsset* Asset = LoadObject<UChaosClothAsset>(nullptr, *AssetPath, nullptr,
			LOAD_NoWarn | LOAD_Quiet);
		if (Asset == nullptr)
		{
			return nullptr;
		}

		UChaosClothComponent* Cloth = NewObject<UChaosClothComponent>(Owner);
		Cloth->SetAsset(Asset);
		// The garment is skinned by the body it hangs on, so it follows rather than animates: the
		// leader pose supplies every bone the cloth's kinematic anchors are bound to. Attaching
		// before registering keeps the component from ticking against an unset leader for a frame.
		Cloth->SetupAttachment(Body);
		Cloth->RegisterComponent();
		Cloth->AttachToComponent(Body, FAttachmentTransformRules::SnapToTargetIncludingScale);
		Cloth->SetLeaderPoseComponent(Body);
		HideGarmentSectionsOnBody(Body, *Asset);
		return Cloth;
	}

	USkeletalMesh* LoadBakedMesh(const FString& Stem, bool bPlayerMaterial)
	{
		return LoadObject<USkeletalMesh>(nullptr,
			*FElysiumContentPaths::BakedCharacterMesh(Stem, bPlayerMaterial));
	}

	bool IsStemBaked(const FString& Stem)
	{
		// Whether the mount actually carries this body, answered without loading the package. There
		// is no other build of a character, so a false here is a missing export rather than a
		// choice between two sets.
		return FPackageName::DoesPackageExist(
			FPackageName::ObjectPathToPackageName(
				FElysiumContentPaths::BakedCharacterMesh(Stem)));
	}

	// Whether the mount carries this asset, answered off the registry rather than by loading it.
	// A bank is baked once, under `_banks`, and reached through the compatible-skeleton declaration
	// on the body's own rig; a body's dialogue clips stay under their rig family. Which of the two
	// an owner is, is a property of the owner and not of the caller — a bank stem is never a body
	// stem — so the folder that actually carries the asset is the answer, and no call site has to
	// hold a flag that could disagree with what was baked.
	bool IsOnMount(const FString& ObjectPath)
	{
		return FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(ObjectPath));
	}

	// The rig family a body's own clips were baked under, off the body's own skeleton rather than a
	// second lookup table: the mesh was built against exactly one family's USkeleton, so asking the
	// mesh is the only answer that cannot disagree with what will actually play.
	FString MeshFamily(const USkeletalMesh* Mesh)
	{
		const USkeleton* Skeleton = Mesh != nullptr ? Mesh->GetSkeleton() : nullptr;
		return Skeleton != nullptr
			? FElysiumContentPaths::BakedCharacterFamily(Skeleton->GetName()) : FString();
	}

	UAnimSequence* LoadBakedClip(const USkeletalMesh* Mesh, const FString& Owner,
		const FString& ClipName)
	{
		const FString BankPath = FElysiumContentPaths::BakedCharacterAnim(
			FElysiumContentPaths::BakedBankFolder(), Owner, ClipName);
		if (IsOnMount(BankPath))
		{
			return LoadObject<UAnimSequence>(nullptr, *BankPath);
		}
		const FString Family = MeshFamily(Mesh);
		if (Family.IsEmpty())
		{
			return nullptr;
		}
		return LoadObject<UAnimSequence>(nullptr,
			*FElysiumContentPaths::BakedCharacterAnim(Family, Owner, ClipName));
	}

	UBlendSpace* LoadBakedBlendSpace(const USkeletalMesh* Mesh, const FString& Owner,
		const FString& Label, const FString& Host)
	{
		const FString BankPath = FElysiumContentPaths::BakedCharacterBlendSpace(
			FElysiumContentPaths::BakedBankFolder(), Owner, Label, Host);
		if (IsOnMount(BankPath))
		{
			return LoadObject<UBlendSpace>(nullptr, *BankPath);
		}
		const FString Family = MeshFamily(Mesh);
		if (Family.IsEmpty())
		{
			return nullptr;
		}
		return LoadObject<UBlendSpace>(nullptr,
			*FElysiumContentPaths::BakedCharacterBlendSpace(Family, Owner, Label, Host));
	}

	USkeletalMesh* LoadMesh(const FString& Stem, UglTFRuntimeAsset*& OutAsset, FString& OutError,
		bool bPlayerMaterial, const TArray<FString>* EyeMaterials)
	{
		// The mount is the only build of a character. A stem the export has not covered is a
		// missing export and fails here by name, rather than quietly standing a second body built
		// by a different path with different rules in it.
		(void)EyeMaterials;
		if (USkeletalMesh* Baked = LoadBakedMesh(Stem, bPlayerMaterial))
		{
			// No parsed asset on this path, and none is needed: a baked clip is addressed by its
			// owner and label rather than pulled out of the body's own glb. Every caller already
			// handles a null asset, because a bank-owned clip never had one.
			OutAsset = nullptr;
			OutError.Reset();
			return Baked;
		}
		OutAsset = nullptr;
		OutError = FString::Printf(
			TEXT("'%s' is not on the baked mount -- run `uv run elysium export characters`"), *Stem);
		return nullptr;
	}

}
