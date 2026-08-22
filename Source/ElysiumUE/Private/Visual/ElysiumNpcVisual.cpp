#include "Visual/ElysiumNpcVisual.h"

#include "ElysiumContentPaths.h"
#include "ElysiumWieldTable.h"
#include "Visual/ElysiumBodyAnimInstance.h"
#include "Visual/ElysiumHairDynamicsConfig.h"
#include "Visual/ElysiumHairDynamicsData.h"

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

DEFINE_LOG_CATEGORY_STATIC(LogElysiumNpcVisual, Log, All);

// The legacy simulated-garment spike (docs/architecture/asset-enhancement.md). Retail garment
// motion is a StudioRender particle solve carried by the model, independently from its hair/body
// bone-chain solver. The approximation artifacts are built by
// pipeline/src/elysium_pipeline/enhancement/cloth_spike.py, but the current baked-character path
// does not select them; every body comes from the mount.
//
// Applied at map load; NPC meshes and their rigs are resolved once per map epoch.

namespace ElysiumNpcVisual
{
	bool InstallHairDynamics(USkeletalMeshComponent* Body, const FString& Stem)
	{
		USkeletalMesh* const Mesh = Body != nullptr ? Body->GetSkeletalMeshAsset() : nullptr;
		if (Mesh == nullptr)
		{
			return false;
		}
		// The authored table's key set IS the scope gate: hair dynamics are owner-tuned
		// presentation, so a body simulates hair exactly when the owner gave it an entry. No entry
		// is the ordinary answer for almost the whole cast and is not a failure -- the one thing
		// worth reporting, a table that is missing entirely, `Load` already reports once.
		const FElysiumHairDynamicsStem* const Authored = UElysiumHairDynamicsConfig::FindStem(Stem);
		if (Authored == nullptr)
		{
			return false;
		}

		const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
		TArray<FElysiumHairDynamicsChainConfig> Chains;
		Chains.Reserve(Authored->Chains.Num());
		for (const FElysiumHairDynamicsChain& Chain : Authored->Chains)
		{
			// The asset is hand-authored against one body, so a chain can name bones this mesh does
			// not have or a value the solver cannot use. Each bad chain is reported and skipped on
			// its own; the rest of the body's hair still swings.
			const int32 Bound = Ref.FindBoneIndex(Chain.BoundBone);
			const int32 End = Ref.FindBoneIndex(Chain.ChainEnd);
			bool bDescends = Bound != INDEX_NONE && End != INDEX_NONE && Bound != End;
			for (int32 Bone = End; bDescends && Bone != Bound;)
			{
				Bone = Ref.GetParentIndex(Bone);
				bDescends = Bone != INDEX_NONE;
			}
			const bool bFinite = FMath::IsFinite(Chain.GravityScale)
				&& FMath::IsFinite(Chain.Damping)
				&& FMath::IsFinite(Chain.AngularSpring)
				&& FMath::IsFinite(Chain.ConeAngleDegrees);
			if (!bDescends || !bFinite || Chain.GravityScale < 0.0f
				|| Chain.Damping < 0.7f || Chain.Damping > 1.0f
				|| Chain.AngularSpring < 0.0f
				|| Chain.ConeAngleDegrees < 0.0f || Chain.ConeAngleDegrees > 90.0f)
			{
				UE_LOG(LogElysiumNpcVisual, Warning,
					TEXT("hair dynamics: authored chain %s -> %s on stem '%s' is invalid against "
					     "'%s' and is skipped"),
					*Chain.BoundBone.ToString(), *Chain.ChainEnd.ToString(), *Stem,
					*Mesh->GetPathName());
				continue;
			}

			FElysiumHairDynamicsChainConfig& Config = Chains.AddDefaulted_GetRef();
			Config.BoundBone = Chain.BoundBone;
			Config.ChainEnd = Chain.ChainEnd;
			Config.GravityScale = Chain.GravityScale;
			Config.Damping = Chain.Damping;
			Config.AngularSpring = Chain.AngularSpring;
			Config.ConeAngleDegrees = Chain.ConeAngleDegrees;
		}

		// Single-body breast recipes are not authored and not installed; the proxy's vocabulary
		// still takes the list.
		const TArray<FElysiumHairDynamicsBodyConfig> Bodies;

		if (Chains.IsEmpty() && Bodies.IsEmpty())
		{
			return false;
		}

		UElysiumBodyAnimInstance* const Instance =
			Cast<UElysiumBodyAnimInstance>(Body->GetAnimInstance());
		if (Instance == nullptr)
		{
			UE_LOG(LogElysiumNpcVisual, Warning,
				TEXT("Hair AnimDynamics body '%s' has no UElysiumBodyAnimInstance"), *Mesh->GetPathName());
			return false;
		}
		Instance->SetHairDynamics(Chains, Bodies, Ref);
		return true;
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

	void GateLeaderCloth(USkeletalMeshComponent* Body, bool bShown)
	{
		if (!Body)
		{
			return;
		}

		TArray<USceneComponent*> Children;
		Body->GetChildrenComponents(/*bIncludeAllDescendants=*/false, Children);
		for (USceneComponent* Child : Children)
		{
			UChaosClothComponent* Garment = Cast<UChaosClothComponent>(Child);
			if (!Garment || Garment->LeaderPoseComponent.Get() != Body)
			{
				continue;
			}

			// HiddenInGame is distinct from bVisible: UChaosClothComponent::UpdateVisibility may
			// restore bVisible after an asset update, but it does not override this gameplay gate.
			Garment->SetHiddenInGame(!bShown);
			if (bShown)
			{
				Garment->ForceNextUpdateTeleportAndReset();
				Garment->ResumeSimulation();
			}
			else
			{
				Garment->SuspendSimulation();
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

	FName WieldComponentTag()
	{
		static const FName Tag(TEXT("ElysiumWieldModel"));
		return Tag;
	}

	USkeletalMeshComponent* FindWieldModel(const USkeletalMeshComponent* Body)
	{
		const AActor* const Owner = Body != nullptr ? Body->GetOwner() : nullptr;
		if (Owner == nullptr)
		{
			return nullptr;
		}
		TArray<USkeletalMeshComponent*> Components;
		Owner->GetComponents(Components);
		for (USkeletalMeshComponent* Component : Components)
		{
			if (Component->ComponentHasTag(WieldComponentTag())
				&& Component->LeaderPoseComponent.Get() == Body)
			{
				return Component;
			}
		}
		return nullptr;
	}

	/**
	 * Take away the wield models this body is holding, and the ones whose body is already gone.
	 *
	 * The same ownership trap `SweepStaleGarments` documents: a wield model belongs to the OWNING
	 * ACTOR, and the map actor owns every character standing on the map — so rebuilding one body
	 * leaves its weapon behind, parented to an actor that is still alive. The leader pose is a weak
	 * reference, so the leftover keeps drawing at the identity transform rather than erroring.
	 *
	 * The tag is what keeps this from reaching the bodies themselves, which are skeletal components
	 * on the same owner.
	 */
	void SweepWieldModels(AActor* Owner, const USkeletalMeshComponent* Body)
	{
		TArray<USkeletalMeshComponent*> Components;
		Owner->GetComponents(Components);
		for (USkeletalMeshComponent* Component : Components)
		{
			if (!Component->ComponentHasTag(WieldComponentTag()))
			{
				continue;
			}
			// Two ways to be stale, and both happen on a restand: the body this weapon followed was
			// destroyed, or the body is about to be handed a different weapon.
			if (!Component->LeaderPoseComponent.IsValid()
				|| Component->LeaderPoseComponent.Get() == Body)
			{
				Component->DestroyComponent();
			}
		}
	}

	void ClearWieldModel(USkeletalMeshComponent* Body)
	{
		AActor* const Owner = Body != nullptr ? Body->GetOwner() : nullptr;
		if (Owner != nullptr)
		{
			SweepWieldModels(Owner, Body);
		}
	}

	USkeletalMeshComponent* InstallWieldModel(USkeletalMeshComponent* Body,
		const FElysiumWieldModelRef& Ref, const FString& Context)
	{
		AActor* const Owner = Body != nullptr ? Body->GetOwner() : nullptr;
		if (Owner == nullptr)
		{
			return nullptr;
		}
		// FIRST, and unconditionally, for the reason InstallGarment states: every reason this
		// function has for declining is also a reason the previous weapon still needs taking away.
		SweepWieldModels(Owner, Body);

		// A row that reaches here carries geometry — the authored no-geometry answers are resolved
		// and reported by UElysiumWieldTable::FindRow, so a load miss here is a bake that did not
		// produce a package its own table references.
		USkeletalMesh* const Mesh = Ref.Mesh.LoadSynchronous();
		if (Mesh == nullptr)
		{
			UE_LOG(LogElysiumNpcVisual, Warning,
				TEXT("wield model '%s' for '%s' is referenced by the wield table but is not on the "
				     "mount -- nothing is drawn. Run `uv run elysium export wield`."),
				*Ref.Mesh.ToString(), *Context);
			return nullptr;
		}

		USkeletalMeshComponent* const Wield = NewObject<USkeletalMeshComponent>(Owner);
		Wield->ComponentTags.Add(WieldComponentTag());
		Wield->SetSkeletalMeshAsset(Mesh);
		// The weapon is posed by the body it hangs on, so it follows rather than animates: no graph,
		// no clip player, nothing for its own skeleton to evaluate. Its clips are baked and a later
		// rung may play one (`w_m_lockpick`'s pick wiggle is the corpus's only visible own-motion),
		// which is a graph added here rather than a different attachment.
		Wield->SetAnimationMode(EAnimationMode::AnimationCustomMode);
		Wield->SetCanEverAffectNavigation(false);
		// Attaching before registering keeps the component from ticking against an unset leader for a
		// frame, the same ordering InstallGarment depends on.
		Wield->SetupAttachment(Body);
		Wield->RegisterComponent();
		Wield->AttachToComponent(Body, FAttachmentTransformRules::SnapToTargetIncludingScale);
		Wield->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Wield->SetLeaderPoseComponent(Body);
		return Wield;
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

	bool IsOnMount(const FString& ObjectPath)
	{
		return FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(ObjectPath));
	}

	// The bank folder is probed before the owner's own folder because a bank owns clips no body
	// declares, and the two namespaces are disjoint on the mount: `_banks` is not a legal model
	// stem, so an owner that answers as a bank can never also be a body. A body's non-bank clips
	// are always its own, so `Owner` is the whole address on the fallback.
	//
	// `Mesh` is the liveness guard rather than part of the address: a caller with no body has
	// nothing to play the sequence on, so it resolves nothing. This is an ordinary negative query
	// and not a failure — the callers that need to distinguish "no body" from "no clip" already
	// hold the mesh they passed.
	UAnimSequence* LoadBakedClip(const USkeletalMesh* Mesh, const FString& Owner,
		const FString& ClipName)
	{
		if (Mesh == nullptr)
		{
			return nullptr;
		}
		const FString BankPath = FElysiumContentPaths::BakedBankAnim(Owner, ClipName);
		if (IsOnMount(BankPath))
		{
			return LoadObject<UAnimSequence>(nullptr, *BankPath);
		}
		return LoadObject<UAnimSequence>(nullptr,
			*FElysiumContentPaths::BakedCharacterAnim(Owner, ClipName));
	}

	UBlendSpace* LoadBakedBlendSpace(const USkeletalMesh* Mesh, const FString& Owner,
		const FString& Label, const FString& Host)
	{
		if (Mesh == nullptr)
		{
			return nullptr;
		}
		const FString BankPath = FElysiumContentPaths::BakedBankBlendSpace(Owner, Label, Host);
		if (IsOnMount(BankPath))
		{
			return LoadObject<UBlendSpace>(nullptr, *BankPath);
		}
		return LoadObject<UBlendSpace>(nullptr,
			*FElysiumContentPaths::BakedCharacterBlendSpace(Owner, Label, Host));
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
