#include "Visual/ElysiumNpcVisual.h"

#include "ElysiumContentPaths.h"

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

	UMaterialInterface* EyeMaster()
	{
		return LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/VtMB/Materials/M_Eyes.M_Eyes"));
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

	USkeletalMesh* LoadMesh(const FString& Stem, FString& OutError, bool bPlayerMaterial)
	{
		// The mount is the only build of a character. A stem the export has not covered is a
		// missing export and fails here by name, rather than quietly standing a second body built
		// by a different path with different rules in it.
		if (USkeletalMesh* Baked = LoadBakedMesh(Stem, bPlayerMaterial))
		{
			OutError.Reset();
			return Baked;
		}
		OutError = FString::Printf(
			TEXT("'%s' is not on the baked mount -- run `uv run elysium export characters`"), *Stem);
		return nullptr;
	}

}
