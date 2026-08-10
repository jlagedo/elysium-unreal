#include "ElysiumCharacterBakeLibrary.h"

#if WITH_EDITOR
#include "ElysiumContentPaths.h"
#include "Visual/ElysiumEyeRig.h"
#include "Visual/ElysiumNpcVisual.h"

#include "glTFRuntimeAsset.h"
#include "glTFRuntimeParser.h"

#include "Animation/AnimSequence.h"
#include "Animation/IAnimationSequenceCompiler.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MaterialTypes.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumCharacterBake, Log, All);

namespace
{
	// VtMB's delta/additive sequence bit (StudioSeqDesc.flags@8). Every shipped `*_delta` sequence
	// carries 0x14 — 0x4 alongside 0x10 — and nothing else does
	// (docs/vtmb/animation_and_movers.md).
	constexpr int32 SeqFlagDelta = 0x4;

	// Every package this batch created or dirtied, by name. Held as names rather than pointers
	// because the flush runs after the objects have been through at least one GC opportunity; the
	// assets themselves survive on RF_Standalone, so a name that no longer resolves means the
	// package really is gone and there is nothing to write.
	TArray<FString> PendingPackages;
	// The sequences whose asynchronous compression the flush has to drain before saving. A
	// UAnimSequence saved mid-compression serialises without its compressed data and loads as an
	// empty clip — silently, because nothing about the asset looks wrong.
	TArray<TWeakObjectPtr<UAnimSequence>> PendingSequences;

	void TrackPackage(UPackage* Package)
	{
		if (Package != nullptr)
		{
			Package->MarkPackageDirty();
			PendingPackages.AddUnique(Package->GetName());
		}
	}

	// Make a package that a previous bake already wrote safe to write again.
	//
	// CreatePackage on a name that exists on disk hands back the registry's HEADER-ONLY view of it,
	// and SavePackage asserts outright on "only been partially loaded" rather than overwriting. The
	// bake regenerates every asset from the .glb each run, so the old object is not merged with the
	// new one: the package is fully loaded, then whatever held the name is stripped of its asset
	// flags and moved to the transient package, leaving the name free.
	UPackage* PrepareForRewrite(const FString& PackageName, const FString& AssetName)
	{
		if (FPackageName::DoesPackageExist(PackageName))
		{
			if (UPackage* Loaded = LoadPackage(nullptr, *PackageName, LOAD_None))
			{
				if (UObject* Stale = StaticFindObject(UObject::StaticClass(), Loaded, *AssetName))
				{
					Stale->ClearFlags(RF_Public | RF_Standalone);
					Stale->Rename(nullptr, GetTransientPackage(),
						REN_DontCreateRedirectors | REN_NonTransactional);
				}
				return Loaded;
			}
		}
		return CreatePackage(*PackageName);
	}

	// Move an object glTFRuntime created in the transient package into a real one and make it a
	// saveable top-level asset. Every UAnimSequence the plugin builds needs this: its outer is
	// hardcoded to GetTransientPackage() and FglTFRuntimeSkeletalAnimationConfig exposes no Outer,
	// unlike the skeletal-mesh config.
	template <typename T>
	T* AdoptIntoPackage(T* Object, const FString& PackageName, FString& OutError)
	{
		if (Object == nullptr)
		{
			OutError = TEXT("nothing to adopt");
			return nullptr;
		}
		UPackage* Package = PrepareForRewrite(PackageName, FPackageName::GetShortName(PackageName));
		if (Package == nullptr)
		{
			OutError = FString::Printf(TEXT("could not create package %s"), *PackageName);
			return nullptr;
		}
		const FString AssetName = FPackageName::GetShortName(PackageName);
		if (Object->GetOuter() != Package || Object->GetName() != AssetName)
		{
			Object->Rename(*AssetName, Package, REN_DontCreateRedirectors | REN_NonTransactional);
		}
		// RF_Standalone is what keeps the asset alive between the call that built it and the flush
		// that saves it — nothing else references a freshly baked mesh or sequence.
		Object->SetFlags(RF_Public | RF_Standalone);
		Object->ClearFlags(RF_Transient);
		FAssetRegistryModule::AssetCreated(Object);
		TrackPackage(Package);
		return Object;
	}

	// The bake keys its imported textures by the slot's trailing material name, which is the name
	// mdl_gltf.py wrote. `UElysiumEntityBodies::InstallEyes` joins its eye records on the same name,
	// so the two share one implementation rather than a convention each has to keep.
	using ElysiumEyes::MaterialNameFromSlot;

	bool IsUnsaveable(const UObject* Object)
	{
		return Object != nullptr &&
			(Object->HasAnyFlags(RF_Transient) || Object->GetOutermost() == GetTransientPackage());
	}

	// Turn one of glTFRuntime's dynamic instances into a saveable constant one.
	//
	// The plugin builds a UMaterialInstanceDynamic per glTF material over the master this project
	// chose (M_PlayerBody, M_Eyes, or one of the plugin's uber materials) and injects the glTF
	// factors and textures into it. A MID cannot be serialised, so the bake copies the whole
	// uniform-parameter set across and then re-points every texture parameter at a real imported
	// asset — the MID's own texture is a transient object decoded from the sibling PNG.
	UMaterialInstanceConstant* SnapshotMaterial(UMaterialInstanceDynamic* Source,
		const FString& PackageName, UTexture2D* Texture, FString& OutError)
	{
		UMaterialInterface* Parent = Source->Parent;
		if (Parent == nullptr)
		{
			OutError = TEXT("dynamic instance has no parent master");
			return nullptr;
		}

		const FString AssetName = FPackageName::GetShortName(PackageName);
		UPackage* Package = PrepareForRewrite(PackageName, AssetName);
		UMaterialInstanceConstant* Instance =
			NewObject<UMaterialInstanceConstant>(Package, *AssetName, RF_Public | RF_Standalone);
		if (Instance == nullptr)
		{
			OutError = FString::Printf(TEXT("could not create %s"), *PackageName);
			return nullptr;
		}

		Instance->SetParentEditorOnly(Parent);
		Instance->CopyMaterialUniformParametersEditorOnly(Source);

		// Every texture the copy brought over is one of the plugin's transient decodes. Replace it
		// with the imported asset, or clear it — leaving a transient reference behind saves a null
		// texture into the package and the section renders untextured with nothing logged.
		TArray<FMaterialParameterInfo> TextureParams;
		TArray<FGuid> TextureIds;
		Parent->GetAllParameterInfoOfType(EMaterialParameterType::Texture, TextureParams, TextureIds);
		for (const FMaterialParameterInfo& Info : TextureParams)
		{
			UTexture* Current = nullptr;
			Instance->GetTextureParameterValue(Info, Current);
			if (!IsUnsaveable(Current))
			{
				continue;
			}
			Instance->SetTextureParameterValueEditorOnly(Info, Texture);
		}

		Instance->PostEditChange();
		FAssetRegistryModule::AssetCreated(Instance);
		TrackPackage(Package);
		return Instance;
	}
}
#endif // WITH_EDITOR

USkeleton* UElysiumCharacterBakeLibrary::EnsureSharedSkeleton(const FString& PackageName)
{
#if WITH_EDITOR
	const FString AssetName = FPackageName::GetShortName(PackageName);
	if (USkeleton* Existing =
		LoadObject<USkeleton>(nullptr, *(PackageName + TEXT(".") + AssetName)))
	{
		return Existing;
	}

	UPackage* Package = CreatePackage(*PackageName);
	if (Package == nullptr)
	{
		UE_LOG(LogElysiumCharacterBake, Error, TEXT("could not create package %s"), *PackageName);
		return nullptr;
	}
	USkeleton* Skeleton =
		NewObject<USkeleton>(Package, *AssetName, RF_Public | RF_Standalone);
	FAssetRegistryModule::AssetCreated(Skeleton);
	TrackPackage(Package);
	return Skeleton;
#else
	return nullptr;
#endif
}

FString UElysiumCharacterBakeLibrary::MergeSkeletonFromGlb(const FString& GlbPath, USkeleton* Skeleton)
{
#if WITH_EDITOR
	if (Skeleton == nullptr)
	{
		return TEXT("no shared skeleton");
	}

	FString Error;
	UglTFRuntimeAsset* Asset = ElysiumNpcVisual::LoadAssetFromPath(GlbPath, Error);
	if (Asset == nullptr)
	{
		return Error;
	}

	// A body declares a skin; a bank declares neither skin nor mesh and is a bare node tree rooted
	// at Bip01, so the skin lookup returns nothing and the node-tree reader is the one that applies.
	FglTFRuntimeSkeletonConfig SkeletonConfig;
	USkeleton* Source = Asset->LoadSkeleton(0, SkeletonConfig);
	if (Source == nullptr)
	{
		Source = Asset->LoadSkeletonFromNodeTree(0, SkeletonConfig);
	}
	if (Source == nullptr)
	{
		return FString::Printf(TEXT("no skeleton in %s"), *FPaths::GetCleanFilename(GlbPath));
	}

	// MergeAllBonesToBoneTree reads nothing but the reference skeleton, so a bare mesh carrying one
	// is enough to merge a bank — which has no mesh of its own to offer.
	USkeletalMesh* Carrier = NewObject<USkeletalMesh>(GetTransientPackage(), NAME_None, RF_Transient);
	Carrier->SetRefSkeleton(Source->GetReferenceSkeleton());
	if (!Skeleton->MergeAllBonesToBoneTree(Carrier))
	{
		return FString::Printf(TEXT("%s does not merge into the shared skeleton"),
			*FPaths::GetCleanFilename(GlbPath));
	}
	TrackPackage(Skeleton->GetOutermost());
	return FString();
#else
	return TEXT("editor only");
#endif
}

int32 UElysiumCharacterBakeLibrary::SkeletonBoneCount(const USkeleton* Skeleton)
{
#if WITH_EDITOR
	return Skeleton != nullptr ? Skeleton->GetReferenceSkeleton().GetNum() : 0;
#else
	return 0;
#endif
}

bool UElysiumCharacterBakeLibrary::SkeletonHasMorphCurve(const USkeleton* Skeleton,
	const FName CurveName)
{
#if WITH_EDITOR
	if (Skeleton == nullptr)
	{
		return false;
	}
	const FCurveMetaData* MetaData = Skeleton->GetCurveMetaData(CurveName);
	return MetaData != nullptr && MetaData->Type.bMorphtarget;
#else
	return false;
#endif
}

FString UElysiumCharacterBakeLibrary::BakedAssetName(const FString& Raw)
{
#if WITH_EDITOR
	return FElysiumContentPaths::BakedAssetName(Raw);
#else
	return Raw;
#endif
}

bool UElysiumCharacterBakeLibrary::MaterialHasTexture(const UMaterialInterface* Material)
{
#if WITH_EDITOR
	if (Material == nullptr)
	{
		return false;
	}
	// OVERRIDDEN values only. `GetTextureParameterValue` defaults to resolving through the instance
	// chain to the parent's own default, and both character masters give every texture parameter a
	// real engine asset as its default -- WhiteSquareTexture and DefaultNormal. Reading the resolved
	// value therefore returns true on the first parameter of every material ever passed here, which
	// makes this the one check that cannot fail: an instance whose albedo never reached disk still
	// answers yes.
	const UMaterialInstance* Instance = Cast<UMaterialInstance>(Material);
	TArray<FMaterialParameterInfo> Params;
	TArray<FGuid> Ids;
	Material->GetAllParameterInfoOfType(EMaterialParameterType::Texture, Params, Ids);
	for (const FMaterialParameterInfo& Info : Params)
	{
		UTexture* Value = nullptr;
		const bool bFound = Instance != nullptr
			? Instance->GetTextureParameterValue(Info, Value, /*bOveriddenOnly=*/true)
			: Material->GetTextureParameterValue(Info, Value);
		if (bFound && Value != nullptr && !IsUnsaveable(Value))
		{
			return true;
		}
	}
	return false;
#else
	return false;
#endif
}

FElysiumCharacterBakeResult UElysiumCharacterBakeLibrary::BakeMesh(
	const FString& GlbPath,
	const FString& PackageName,
	USkeleton* Skeleton,
	bool bPlayerMaterial,
	const TArray<FString>& EyeMaterials,
	const TMap<FString, UTexture2D*>& Textures,
	const FString& MaterialPackagePath)
{
	FElysiumCharacterBakeResult Result;
#if WITH_EDITOR
	if (Skeleton == nullptr)
	{
		Result.Errors.Add(TEXT("no shared skeleton"));
		return Result;
	}

	FElysiumGlbMeshOptions Options;
	Options.bPlayerMaterial = bPlayerMaterial;
	Options.EyeMaterials = EyeMaterials.IsEmpty() ? nullptr : &EyeMaterials;
	Options.Outer = PrepareForRewrite(PackageName, FPackageName::GetShortName(PackageName));
	Options.Skeleton = Skeleton;

	FString Error;
	UglTFRuntimeAsset* Asset = nullptr;
	USkeletalMesh* Mesh = ElysiumNpcVisual::LoadMeshFromPath(GlbPath, Asset, Error, Options);
	if (Mesh == nullptr)
	{
		Result.Errors.Add(Error);
		return Result;
	}

	// The mesh went in with the shared skeleton, so this writes the morph-target curve metadata
	// onto the asset that is about to be saved rather than onto a per-load throwaway. Serialising
	// it is what lets the runtime stop registering it at load, where AddCurveMetaData's default
	// transaction reaches an editor transaction buffer that does not exist under -game.
	ElysiumNpcVisual::RegisterMorphTargetCurves(Mesh);
	TrackPackage(Skeleton->GetOutermost());

	// Instances are named off the MESH asset, not off the .glb stem: the two material variants of
	// one body are built from the same file and would otherwise write the same instance names, and
	// the second variant's PrepareForRewrite would move the first variant's instances into the
	// transient package — leaving a mesh whose every slot serialises as null.
	const FString Stem = FPackageName::GetShortName(PackageName).RightChop(3);
	TArray<FSkeletalMaterial>& Slots = Mesh->GetMaterials();
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		UMaterialInstanceDynamic* Dynamic = Cast<UMaterialInstanceDynamic>(Slots[Index].MaterialInterface);
		if (Dynamic == nullptr)
		{
			// Already a saveable material — an override that resolved straight to its master.
			continue;
		}
		const FString MaterialName = MaterialNameFromSlot(Slots[Index].MaterialSlotName);
		UTexture2D* const* Texture = Textures.Find(MaterialName);
		const FString InstanceName =
			FString::Printf(TEXT("MI_%s_%s"), *Stem, *FElysiumContentPaths::BakedAssetName(MaterialName));

		FString SnapshotError;
		UMaterialInstanceConstant* Instance = SnapshotMaterial(Dynamic,
			MaterialPackagePath / InstanceName, Texture != nullptr ? *Texture : nullptr, SnapshotError);
		if (Instance == nullptr)
		{
			Result.Errors.Add(FString::Printf(TEXT("%s slot %s: %s"), *Stem,
				*Slots[Index].MaterialSlotName.ToString(), *SnapshotError));
			continue;
		}
		Slots[Index].MaterialInterface = Instance;
		++Result.Count;
	}

	if (AdoptIntoPackage(Mesh, PackageName, Error) == nullptr)
	{
		Result.Errors.Add(Error);
		return Result;
	}
	++Result.Count;
#else
	Result.Errors.Add(TEXT("editor only"));
#endif
	return Result;
}

FElysiumCharacterBakeResult UElysiumCharacterBakeLibrary::BakeClips(
	const FString& GlbPath,
	const FString& PackagePath,
	const TArray<FString>& ClipNames,
	const TArray<int32>& ClipFlags,
	USkeleton* Skeleton)
{
	FElysiumCharacterBakeResult Result;
#if WITH_EDITOR
	if (Skeleton == nullptr)
	{
		Result.Errors.Add(TEXT("no shared skeleton"));
		return Result;
	}
	if (ClipNames.Num() != ClipFlags.Num())
	{
		Result.Errors.Add(TEXT("clip name and flag arrays differ in length"));
		return Result;
	}

	FString Error;
	UglTFRuntimeAsset* Asset = ElysiumNpcVisual::LoadAssetFromPath(GlbPath, Error);
	if (Asset == nullptr)
	{
		Result.Errors.Add(Error);
		return Result;
	}

	// Tracks this family's skeleton has no bone for, named up front so the drop is deliberate
	// rather than a plugin complaint per clip. Most sources merge whole and this is empty; it fills
	// only where VtMB reuses a generic appendix name for a different chain, and there the track
	// MUST be dropped -- binding it would drive a bone the animator did not author it against.
	// Left unnamed, glTFRuntime logs an error per track, which is also what makes the commandlet
	// exit non-zero on a bake that did exactly what it was asked to.
	FglTFRuntimeSkeletalAnimationConfig SharedConfig;
	{
		const FReferenceSkeleton& TargetRef = Skeleton->GetReferenceSkeleton();
		for (const FglTFRuntimeNode& Node : Asset->GetNodes())
		{
			if (!Node.Name.IsEmpty() && TargetRef.FindBoneIndex(FName(*Node.Name)) == INDEX_NONE)
			{
				SharedConfig.RemoveTracks.AddUnique(Node.Name);
			}
		}
	}
	if (!SharedConfig.RemoveTracks.IsEmpty())
	{
		UE_LOG(LogElysiumCharacterBake, Display,
			TEXT("%s: %d track(s) name a bone this rig family does not carry and are dropped"),
			*FPaths::GetCleanFilename(GlbPath), SharedConfig.RemoveTracks.Num());
	}

	for (int32 Index = 0; Index < ClipNames.Num(); ++Index)
	{
		const FString& ClipName = ClipNames[Index];
		// Bound to the SKELETON, not to a body: one sequence serves every body in the family
		// instead of one per body, which is what the shared skeleton is for.
		FglTFRuntimeSkeletalAnimationConfig AnimConfig = SharedConfig;
		UAnimSequence* Sequence = Asset->LoadSkeletalAnimationByNameOnSkeleton(
			Skeleton, ClipName, AnimConfig, /*bCaseSensitive=*/false);
		if (Sequence == nullptr)
		{
			Result.Errors.Add(FString::Printf(TEXT("clip '%s' not in %s"), *ClipName,
				*FPaths::GetCleanFilename(GlbPath)));
			continue;
		}

		if ((ClipFlags[Index] & SeqFlagDelta) != 0)
		{
			Sequence->AdditiveAnimType = AAT_LocalSpaceBase;
			Sequence->RefPoseType = ABPT_RefPose;
			Sequence->RefPoseSeq = nullptr;
			// The sequence was already built and compressed as an ordinary clip; changing what it
			// means has to go back through the standard recompression path or the additive base is
			// never subtracted.
			Sequence->PostEditChange();
		}

		const FString PackageName =
			PackagePath / (TEXT("A_") + FElysiumContentPaths::BakedAssetName(ClipName));
		if (AdoptIntoPackage(Sequence, PackageName, Error) == nullptr)
		{
			Result.Errors.Add(FString::Printf(TEXT("clip '%s': %s"), *ClipName, *Error));
			continue;
		}
		PendingSequences.Add(Sequence);
		++Result.Count;
	}
#else
	Result.Errors.Add(TEXT("editor only"));
#endif
	return Result;
}

FElysiumCharacterBakeResult UElysiumCharacterBakeLibrary::FlushCharacterBake()
{
	FElysiumCharacterBakeResult Result;
#if WITH_EDITOR
	TArray<UAnimSequence*> Sequences;
	for (const TWeakObjectPtr<UAnimSequence>& Weak : PendingSequences)
	{
		if (UAnimSequence* Sequence = Weak.Get())
		{
			Sequences.Add(Sequence);
		}
	}
	if (!Sequences.IsEmpty())
	{
		UE::Anim::IAnimSequenceCompilingManager::FinishCompilation(Sequences);
	}
	PendingSequences.Reset();

	for (const FString& PackageName : PendingPackages)
	{
		UPackage* Package = FindPackage(nullptr, *PackageName);
		if (Package == nullptr)
		{
			Result.Errors.Add(FString::Printf(TEXT("%s vanished before it was saved"), *PackageName));
			continue;
		}
		const FString FileName = FPackageName::LongPackageNameToFilename(
			PackageName, FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		if (!UPackage::SavePackage(Package, nullptr, *FileName, SaveArgs))
		{
			Result.Errors.Add(FString::Printf(TEXT("could not save %s"), *PackageName));
			continue;
		}
		++Result.Count;
	}
	PendingPackages.Reset();
#else
	Result.Errors.Add(TEXT("editor only"));
#endif
	return Result;
}
