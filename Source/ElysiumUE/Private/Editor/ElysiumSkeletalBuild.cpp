#include "ElysiumSkeletalBuild.h"

#if WITH_EDITOR
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendProfile.h"
#include "Animation/BlendSpace.h"
#include "Animation/BlendSpace1D.h"
#include "Animation/MorphTarget.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "BoneWeights.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetCommon.h"
#include "MeshDescription.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "SkeletalMeshAttributes.h"
#include "StaticMeshAttributes.h"
#include "ElysiumContentPaths.h"
#include "Visual/ElysiumAnimLayerMask.h"
#include "Visual/ElysiumBlendGrids.h"
#include "Visual/ElysiumSkeletalSource.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumSkeletalBuild, Log, All);

namespace
{
	const FName ProbeMorphName(TEXT("ElysiumProbeMorph"));
	const FName ProbeMaterialSlot(TEXT("ElysiumProbeSlot"));

	/** Create the package, or take over the one already on disk so a re-bake overwrites. */
	UPackage* OpenPackage(const FString& PackageName)
	{
		// A package half-resident from an earlier asset-registry scan cannot be saved
		// ("only been partially loaded"), so anything already on disk is loaded whole first.
		if (FPackageName::DoesPackageExist(PackageName))
		{
			LoadPackage(nullptr, *PackageName, LOAD_None);
		}
		return CreatePackage(*PackageName);
	}

	/** Move any object squatting on the name out of the way so NewObject can claim it. */
	void ClearForRewrite(UPackage* Package, const FString& AssetName)
	{
		if (UObject* Existing = StaticFindObject(nullptr, Package, *AssetName))
		{
			Existing->ClearFlags(RF_Public | RF_Standalone);
			Existing->Rename(nullptr, GetTransientPackage(),
				REN_DontCreateRedirectors | REN_NonTransactional);
		}
	}

	/**
	 * Delete the package a previous export wrote for a clip this one does not build.
	 *
	 * A bake that stops writing an asset does not remove it, and the mount is resolved by NAME --
	 * so an orphan stays loadable and answers for a label whose meaning has changed underneath it.
	 * That is worse than a missing asset, which fails visibly.
	 */
	bool SweepOrphan(const FString& PackageName)
	{
		// Resolved to a filename directly rather than through `DoesPackageExist`, which answers
		// false here for a package on disk that no asset-registry scan has reached in this
		// commandlet -- the orphan is exactly the package nothing in this run opens.
		const FString FileName = FPackageName::LongPackageNameToFilename(
			PackageName, FPackageName::GetAssetPackageExtension());
		if (FileName.IsEmpty() || !IFileManager::Get().FileExists(*FileName))
		{
			UE_LOG(LogElysiumSkeletalBuild, Verbose, TEXT("sweep: nothing at %s (%s)"),
				*PackageName, FileName.IsEmpty() ? TEXT("unresolved") : *FileName);
			return false;
		}
		// NOT loaded first. Loading it to detach its objects leaves the linker holding the file
		// open, and Windows refuses to delete an open file -- the delete then fails silently and
		// the orphan survives the sweep that reported it swept. Nothing in this run references an
		// orphan, so there is nothing in memory to detach; the file is simply removed, read-only
		// bit included, since a generated mount carries no authored state.
		const bool bDeleted = IFileManager::Get().Delete(*FileName, /*RequireExists=*/false,
			/*EvenReadOnly=*/true, /*Quiet=*/true);
		if (!bDeleted)
		{
			UE_LOG(LogElysiumSkeletalBuild, Warning, TEXT("sweep: could not delete %s"), *FileName);
		}
		return bDeleted;
	}

	bool SavePackageTo(UPackage* Package, const FString& PackageName)
	{
		Package->MarkPackageDirty();
		const FString FileName = FPackageName::LongPackageNameToFilename(
			PackageName, FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		return UPackage::SavePackage(Package, nullptr, *FileName, SaveArgs);
	}

	/**
	 * One saved material instance for one mesh section, or the engine default when the bake was
	 * given no master to parent to.
	 *
	 * The master's parameter names are glTF's (`baseColorTexture`, ...), which is what lets the
	 * same instance serve a body drawn as an NPC and the same body worn by the player -- those
	 * differ only in the ModelAlpha the runtime drives, not in the asset.
	 */
	UMaterialInterface* MakeSectionMaterial(UMaterialInterface* Parent,
		const FString& PackagePath, const FString& MeshAssetName, const FString& Slot,
		const FString& TexturePath)
	{
		if (Parent == nullptr || PackagePath.IsEmpty())
		{
			return UMaterial::GetDefaultMaterial(MD_Surface);
		}
		// Named after the mesh asset, not the model: two meshes of one model would otherwise write
		// the same instance names and the second would take the first's slots out from under it.
		const FString AssetName = FString::Printf(TEXT("MI_%s_%s"),
			*FElysiumContentPaths::BakedAssetName(MeshAssetName),
			*FElysiumContentPaths::BakedAssetName(Slot));
		const FString PackageName = PackagePath / AssetName;
		UPackage* Package = OpenPackage(PackageName);
		if (Package == nullptr)
		{
			return Parent;
		}
		ClearForRewrite(Package, AssetName);

		UMaterialInstanceConstant* Instance = NewObject<UMaterialInstanceConstant>(
			Package, *AssetName, RF_Public | RF_Standalone);
		Instance->SetParentEditorOnly(Parent);
		if (UTexture* Albedo = TexturePath.IsEmpty()
			? nullptr : LoadObject<UTexture>(nullptr, *TexturePath))
		{
			Instance->SetTextureParameterValueEditorOnly(
				FMaterialParameterInfo(TEXT("baseColorTexture")), Albedo);
		}
		Instance->PostEditChange();
		FAssetRegistryModule::AssetCreated(Instance);
		SavePackageTo(Package, PackageName);
		return Instance;
	}

	/**
	 * Register this container's own bind pose on the skeleton as a named retarget source, and
	 * return the name a sequence built from it must carry.
	 *
	 * This is the input `EBoneTranslationRetargetingMode::OrientAndScale` cannot work without: the
	 * pose the clip was AUTHORED against, which the correction is a difference from. Retail reads
	 * it straight off the chained bank's own header when it compiles the include-model remap
	 * (`docs/vtmb/animation_and_movers.md` A.4b); Unreal reads it from `AnimRetargetSources`, and
	 * with nothing registered `GetRefLocalPoses(NAME_None)` hands back the skeleton's own reference
	 * pose -- whichever member the partition listed first, which is an arbitrary body and is why
	 * every per-bone mode tried before this one was computing against noise.
	 *
	 * The array is indexed by SKELETON bone, because that is how the engine subscripts it
	 * (`AuthoredOnRefSkeleton[SourceSkeletonBoneIndex]`). It is seeded from the skeleton's own
	 * reference pose so a bone this container does not declare still reads as itself -- a difference
	 * of zero, which the mode skips -- and overwritten wherever the container has an opinion.
	 */
	FName RegisterRetargetSource(USkeleton* Skeleton, const FString& SourcePath,
		const FElysiumSkeletalSource& Source)
	{
		const FName Name(*FPaths::GetBaseFilename(SourcePath));
		const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
		FReferencePose Pose;
		Pose.PoseName = Name;
		Pose.ReferencePose = RefSkeleton.GetRefBonePose();
		for (const FElysiumSourceBone& Bone : Source.Bones)
		{
			const int32 Index = RefSkeleton.FindRawBoneIndex(Bone.Name);
			if (Pose.ReferencePose.IsValidIndex(Index))
			{
				Pose.ReferencePose[Index] = Bone.Local;
			}
		}
		Skeleton->AnimRetargetSources.Add(Name, MoveTemp(Pose));
		return Name;
	}

	/** How far a bone has to travel across a clip's frames before the container counts as animating it. */
	constexpr float AnimatedTranslationCm = 0.1f;
	constexpr float AnimatedRotationDeg = 0.5f;

	/**
	 * The bones outside the `Bip01` biped that this container's clips actually ANIMATE.
	 *
	 * Everything else in the appendix -- the generic `BoneNN` hair chains -- ships a track only
	 * because the overlay and host rules force one for every bone a mask owns, and that track states
	 * the EMITTING model's bind pose frame after frame. On the body that authored it that is
	 * harmless; through a shared bank it is a foreign rest pose delivered by name to whatever chain
	 * happens to share the name. `Bone19` is `Bip01 Head`'s child on `character_shared_female_pc_g2`
	 * and `Bone18`'s on `tremere_female_armor_0` -- one name, two chains, 179.8 degrees apart --
	 * and VtMB's own loader answers that case in a branch whose semantics are not recovered.
	 *
	 * Rotation has no retargeting mode to correct it the way `OrientAndScale` corrects translation,
	 * so the answer is to not bind the track: an untracked bone resolves to the playing MESH's
	 * reference pose, which is the body's own bind and exactly what retail leaves it at.
	 *
	 * Measured across every shared bank: 25,208 of 25,410 appendix tracks are constant, and all 202
	 * that move are weapon props (`Bat`, `Sledgehammer`, `tire iron` ...) travelling hundreds of
	 * centimetres. No bank rotates a hair bone at all, so the separation costs no animation.
	 *
	 * Decided per CONTAINER rather than per clip, which is what keeps an additive symmetric: a delta
	 * and the base it is a difference from drop the same bones, so the bone resolves the same way on
	 * both sides and subtracts to identity.
	 */
	TSet<int32> SilentAppendixBones(const FElysiumSkeletalSource& Source)
	{
		TSet<int32> Silent;
		for (int32 Index = 0; Index < Source.Bones.Num(); ++Index)
		{
			if (!Source.Bones[Index].Name.ToString().StartsWith(TEXT("Bip01")))
			{
				Silent.Add(Index);
			}
		}
		for (const FElysiumSourceClip& Clip : Source.Clips)
		{
			for (const FElysiumSourceTrack& Track : Clip.Tracks)
			{
				if (!Silent.Contains(Track.Bone))
				{
					continue;
				}
				bool bMoves = false;
				for (int32 Frame = 1; !bMoves && Frame < Track.Translations.Num(); ++Frame)
				{
					bMoves = (Track.Translations[Frame] - Track.Translations[0]).Size()
						> AnimatedTranslationCm;
				}
				for (int32 Frame = 1; !bMoves && Frame < Track.Rotations.Num(); ++Frame)
				{
					bMoves = FMath::RadiansToDegrees(
						Track.Rotations[0].AngularDistance(Track.Rotations[Frame]))
						> AnimatedRotationDeg;
				}
				if (bMoves)
				{
					Silent.Remove(Track.Bone);
				}
			}
		}
		return Silent;
	}
}
#endif // WITH_EDITOR

FString UElysiumSkeletalBuildLibrary::BuildProbeSkeletalMesh(const FString& PackageName)
{
#if WITH_EDITOR
	const FString AssetName = FPackageName::GetShortName(PackageName);
	UPackage* Package = CreatePackage(*PackageName);
	if (Package == nullptr)
	{
		return FString::Printf(TEXT("could not create package %s"), *PackageName);
	}

	USkeletalMesh* Mesh = NewObject<USkeletalMesh>(Package, *AssetName, RF_Public | RF_Standalone);
	USkeleton* Skeleton = NewObject<USkeleton>(Package, *(AssetName + TEXT("_Skeleton")),
		RF_Public | RF_Standalone);

	// The reference skeleton is authored on the MESH; the USkeleton then takes its bone tree from
	// it. That is the same order an importer uses, and it is what keeps a body's own proportions on
	// the body rather than on the shared skeleton.
	FReferenceSkeleton RefSkeleton;
	{
		FReferenceSkeletonModifier Modifier(RefSkeleton, Skeleton);
		Modifier.Add(FMeshBoneInfo(TEXT("Root"), TEXT("Root"), INDEX_NONE), FTransform::Identity);
		Modifier.Add(FMeshBoneInfo(TEXT("Bone1"), TEXT("Bone1"), 0),
			FTransform(FVector(0.0, 0.0, 10.0)));
	}
	Mesh->SetRefSkeleton(RefSkeleton);

	// An LOD is two parallel arrays, and both entries have to exist. The imported model's LODModels
	// is the one that is easy to miss: nothing needs it while the mesh is being built, so the bake
	// runs clean and the asset saves, and then PostLoad indexes it and asserts on an empty array in
	// whichever process opens the package next.
	Mesh->GetImportedModel()->LODModels.Add(new FSkeletalMeshLODModel());

	// A mesh description belongs to an LOD, so the LOD has to exist before one can be created for
	// it -- CreateMeshDescription(0) simply returns null otherwise, with nothing logged.
	FSkeletalMeshLODInfo& LodInfo = Mesh->AddLODInfo();
	LodInfo.ReductionSettings.NumOfTrianglesPercentage = 1.0f;
	LodInfo.BuildSettings.bRecomputeNormals = false;
	LodInfo.BuildSettings.bRecomputeTangents = true;

	FMeshDescription* MeshDescription = Mesh->CreateMeshDescription(0);
	if (MeshDescription == nullptr)
	{
		return TEXT("CreateMeshDescription(0) returned null");
	}

	FSkeletalMeshAttributes Attributes(*MeshDescription);
	Attributes.Register();

	FStaticMeshAttributes& Static = Attributes;
	TVertexAttributesRef<FVector3f> Positions = Static.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> Normals = Static.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector2f> UVs = Static.GetVertexInstanceUVs();
	TPolygonGroupAttributesRef<FName> SlotNames = Static.GetPolygonGroupMaterialSlotNames();

	const FPolygonGroupID Group = MeshDescription->CreatePolygonGroup();
	SlotNames.Set(Group, ProbeMaterialSlot);

	// One triangle: enough to exercise geometry, skin weights and a morph delta at once.
	const FVector3f Corners[3] = {
		FVector3f(0.0f, 0.0f, 0.0f),
		FVector3f(10.0f, 0.0f, 0.0f),
		FVector3f(0.0f, 10.0f, 0.0f)
	};
	TArray<FVertexInstanceID> Instances;
	TArray<FVertexID> Vertices;
	for (const FVector3f& Corner : Corners)
	{
		const FVertexID Vertex = MeshDescription->CreateVertex();
		Positions.Set(Vertex, Corner);
		Vertices.Add(Vertex);

		const FVertexInstanceID Instance = MeshDescription->CreateVertexInstance(Vertex);
		Normals.Set(Instance, FVector3f(0.0f, 0.0f, 1.0f));
		UVs.Set(Instance, 0, FVector2f(Corner.X / 10.0f, Corner.Y / 10.0f));
		Instances.Add(Instance);
	}
	MeshDescription->CreatePolygon(Group, Instances);

	// Skin weights, all on the second bone so a wrong binding is visible rather than plausible.
	FSkinWeightsVertexAttributesRef SkinWeights = Attributes.GetVertexSkinWeights();
	for (const FVertexID Vertex : Vertices)
	{
		UE::AnimationCore::FBoneWeight Weight(1, 1.0f);
		SkinWeights.Set(Vertex, UE::AnimationCore::FBoneWeights::Create({ Weight }));
	}

	// The whole point of the probe. glTFRuntime registers a morph target's NAME here and never its
	// deltas, which is why its baked meshes lose their faces on reload.
	if (!Attributes.RegisterMorphTargetAttribute(ProbeMorphName, /*bIncludeNormals=*/false))
	{
		return TEXT("RegisterMorphTargetAttribute failed");
	}
	TVertexAttributesRef<FVector3f> MorphDeltas =
		Attributes.GetVertexMorphPositionDelta(ProbeMorphName);
	for (const FVertexID Vertex : Vertices)
	{
		MorphDeltas.Set(Vertex, FVector3f(0.0f, 0.0f, 5.0f));
	}

	Mesh->CommitMeshDescription(0);

	FSkeletalMaterial Material;
	Material.MaterialInterface = UMaterial::GetDefaultMaterial(MD_Surface);
	Material.MaterialSlotName = ProbeMaterialSlot;
	Mesh->GetMaterials().Add(Material);

	Skeleton->MergeAllBonesToBoneTree(Mesh);
	Mesh->SetSkeleton(Skeleton);

	// Skinning reads RefBasesInvMatrix, which nothing fills in for a mesh built from scratch.
	Mesh->CalculateInvRefMatrices();
	Mesh->SetImportedBounds(FBoxSphereBounds(FBox(FVector(0.0), FVector(10.0, 10.0, 10.0))));
	Mesh->Build();
	Mesh->PostEditChange();

	FAssetRegistryModule::AssetCreated(Skeleton);
	FAssetRegistryModule::AssetCreated(Mesh);
	Package->MarkPackageDirty();

	const FString FileName = FPackageName::LongPackageNameToFilename(
		PackageName, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	if (!UPackage::SavePackage(Package, nullptr, *FileName, SaveArgs))
	{
		return FString::Printf(TEXT("could not save %s"), *PackageName);
	}
	return FString();
#else
	return TEXT("editor only");
#endif
}

FString UElysiumSkeletalBuildLibrary::BuildSkeletalMeshFromSource(const FString& SourcePath,
	const FString& PackageName, const FString& SkeletonPackageName,
	const FString& MaterialParentPath, const FString& MaterialPackagePath,
	const TMap<FString, FString>& MaterialTextures,
	const TMap<FString, FString>& MaterialParents)
{
#if WITH_EDITOR
	FElysiumSkeletalSource Source;
	FString Error;
	if (!FElysiumSkeletalSource::Load(SourcePath, Source, Error))
	{
		return Error;
	}
	if (Source.Vertices.IsEmpty() || Source.Indices.IsEmpty())
	{
		return FString::Printf(TEXT("%s carries no geometry"), *SourcePath);
	}

	// --- the skeleton, shared across a rig family -------------------------------------------
	// It is created on first use and merged into afterwards, so the bone tree ends up the union
	// of every body in the family. That union is what lets one baked clip play on all of them.
	const bool bSharedSkeleton = !SkeletonPackageName.IsEmpty();
	const FString SkeletonAsset = bSharedSkeleton
		? FPackageName::GetShortName(SkeletonPackageName)
		: FPackageName::GetShortName(PackageName) + TEXT("_Skeleton");
	UPackage* SkeletonPackage = OpenPackage(bSharedSkeleton ? SkeletonPackageName : PackageName);
	if (SkeletonPackage == nullptr)
	{
		return FString::Printf(TEXT("could not create package %s"), *SkeletonPackageName);
	}
	USkeleton* Skeleton = FindObject<USkeleton>(SkeletonPackage, *SkeletonAsset);
	const bool bNewSkeleton = Skeleton == nullptr;
	if (bNewSkeleton)
	{
		ClearForRewrite(SkeletonPackage, SkeletonAsset);
		Skeleton = NewObject<USkeleton>(SkeletonPackage, *SkeletonAsset,
			RF_Public | RF_Standalone);
	}

	UPackage* Package = OpenPackage(PackageName);
	if (Package == nullptr)
	{
		return FString::Printf(TEXT("could not create package %s"), *PackageName);
	}
	const FString AssetName = FPackageName::GetShortName(PackageName);
	ClearForRewrite(Package, AssetName);
	USkeletalMesh* Mesh = NewObject<USkeletalMesh>(Package, *AssetName, RF_Public | RF_Standalone);

	// --- the reference skeleton -------------------------------------------------------------
	// Authored on the MESH, which the USkeleton then takes its bone tree from. That order is
	// what keeps a body's own proportions on the body rather than on the shared skeleton.
	FReferenceSkeleton RefSkeleton;
	{
		FReferenceSkeletonModifier Modifier(RefSkeleton, Skeleton);
		for (const FElysiumSourceBone& Bone : Source.Bones)
		{
			Modifier.Add(FMeshBoneInfo(Bone.Name, Bone.Name.ToString(), Bone.Parent), Bone.Local);
		}
	}
	Mesh->SetRefSkeleton(RefSkeleton);

	Mesh->GetImportedModel()->LODModels.Add(new FSkeletalMeshLODModel());
	FSkeletalMeshLODInfo& LodInfo = Mesh->AddLODInfo();
	LodInfo.ReductionSettings.NumOfTrianglesPercentage = 1.0f;
	LodInfo.ReductionSettings.NumOfVertPercentage = 1.0f;
	LodInfo.LODHysteresis = 0.02f;
	LodInfo.bImportWithBaseMesh = true;
	// The container carries VtMB's own authored per-vertex normal, so the build must NOT derive
	// one. Recomputing averages each vertex's adjacent face normals, which cannot reproduce a
	// split normal by construction -- and studiomdl duplicated vertices precisely so a hard edge
	// could carry two. Measured against the authored values, a recomputed set sits a mean 12-15
	// degrees off with a quarter of vertices beyond 20, which reads as rougher skin.
	// Tangents are still derived: MikkTSpace builds them from these normals and the UVs.
	LodInfo.BuildSettings.bRecomputeNormals = false;
	LodInfo.BuildSettings.bRecomputeTangents = true;
	// Stated, not inherited. FSkeletalMeshBuildSettings defaults this to 0.015 cm, a heuristic
	// sized for FBX character rigs, and FLODUtilities::BuildMorphTargets drops every delta under it
	// -- 163,750 of the cast's 1,034,295 authored deltas, which is the soft outer ring of every
	// FACS shape truncated to exactly zero. VtMB's flexes are small by construction and overlap in
	// dozens, so the falloff is the expression. Nothing here is an import approximation of a DCC
	// mesh, so nothing wants a weld threshold.
	LodInfo.BuildSettings.MorphThresholdPosition = 0.0f;

	FMeshDescription* MeshDescription = Mesh->CreateMeshDescription(0);
	if (MeshDescription == nullptr)
	{
		return TEXT("CreateMeshDescription(0) returned null");
	}
	FSkeletalMeshAttributes Attributes(*MeshDescription);
	Attributes.Register();

	FStaticMeshAttributes& Static = Attributes;
	TVertexAttributesRef<FVector3f> Positions = Static.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector2f> UVs = Static.GetVertexInstanceUVs();
	TVertexInstanceAttributesRef<FVector3f> Normals = Static.GetVertexInstanceNormals();
	TPolygonGroupAttributesRef<FName> SlotNames = Static.GetPolygonGroupMaterialSlotNames();

	MeshDescription->ReserveNewVertices(Source.Vertices.Num());
	TArray<FVertexID> Vertices;
	Vertices.Reserve(Source.Vertices.Num());
	FBox Bounds(ForceInit);
	for (const FElysiumSourceVertex& Vertex : Source.Vertices)
	{
		const FVertexID Id = MeshDescription->CreateVertex();
		Positions.Set(Id, Vertex.Position);
		Vertices.Add(Id);
		Bounds += FVector(Vertex.Position);
	}

	// One polygon group per material section, in section order, so the slot a triangle lands in
	// is the slot the material array is indexed by.
	const int32 TriangleTotal = Source.Indices.Num() / 3;
	MeshDescription->ReserveNewVertexInstances(TriangleTotal * 3);
	MeshDescription->ReserveNewPolygons(TriangleTotal);

	// A morph's position delta is per vertex but its NORMAL delta is per vertex instance, and a
	// vertex has one instance per triangle corner that uses it. Collected here, while the
	// instances are being made, because nothing later can reconstruct the mapping cheaply.
	TArray<TArray<FVertexInstanceID, TInlineAllocator<6>>> InstancesOfVertex;
	InstancesOfVertex.SetNum(Source.Vertices.Num());
	UMaterialInterface* MaterialParent = MaterialParentPath.IsEmpty()
		? nullptr : LoadObject<UMaterialInterface>(nullptr, *MaterialParentPath);
	if (!MaterialParentPath.IsEmpty() && MaterialParent == nullptr)
	{
		return FString::Printf(
			TEXT("%s is missing; run: uv run elysium export bundle policy"), *MaterialParentPath);
	}
	// A slot named here takes a different master. The eyeballs are the reason: the runtime's eye
	// rig finds an eye by asking whether the slot's base material IS the eye master, and every
	// section parented to the body master answers no -- so the iris, its gaze and its fidget grid
	// were never installed on anything, with no diagnostic, because the test that would have
	// reported it is behind the test that failed.
	TMap<FString, UMaterialInterface*> SlotParents;
	for (const TPair<FString, FString>& Pair : MaterialParents)
	{
		UMaterialInterface* Master = LoadObject<UMaterialInterface>(nullptr, *Pair.Value);
		if (Master == nullptr)
		{
			return FString::Printf(
				TEXT("%s is missing; run: uv run elysium export bundle policy"), *Pair.Value);
		}
		SlotParents.Add(Pair.Key, Master);
	}

	for (const FElysiumSourceSection& Section : Source.Sections)
	{
		const FPolygonGroupID Group = MeshDescription->CreatePolygonGroup();
		const FName SlotName(*Section.Material);
		SlotNames.Set(Group, SlotName);

		FSkeletalMaterial Material;
		UMaterialInterface* const Parent = SlotParents.FindRef(Section.Material) != nullptr
			? SlotParents.FindRef(Section.Material) : MaterialParent;
		Material.MaterialInterface = MakeSectionMaterial(Parent, MaterialPackagePath,
			AssetName, Section.Material, MaterialTextures.FindRef(Section.Material));
		Material.MaterialSlotName = SlotName;
		Material.ImportedMaterialSlotName = SlotName;
		Mesh->GetMaterials().Add(Material);

		const int32 Last = Section.FirstTriangle + Section.TriangleCount;
		for (int32 Triangle = Section.FirstTriangle; Triangle < Last; ++Triangle)
		{
			FVertexInstanceID Corners[3];
			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				const uint32 VertexIndex = Source.Indices[Triangle * 3 + Corner];
				if (!Vertices.IsValidIndex(static_cast<int32>(VertexIndex)))
				{
					return FString::Printf(TEXT("%s: triangle %d indexes vertex %u of %d"),
						*SourcePath, Triangle, VertexIndex, Vertices.Num());
				}
				Corners[Corner] = MeshDescription->CreateVertexInstance(Vertices[VertexIndex]);
				UVs.Set(Corners[Corner], 0, Source.Vertices[VertexIndex].UV);
				// Per vertex INSTANCE, which is what carries a split normal: a hard edge is a
				// duplicated source vertex, so its copies already differ here and the build keeps
				// them apart rather than welding the pair to one averaged direction.
				Normals.Set(Corners[Corner], Source.Vertices[VertexIndex].Normal);
				InstancesOfVertex[VertexIndex].Add(Corners[Corner]);
			}
			MeshDescription->CreatePolygon(Group, TArrayView<FVertexInstanceID>(Corners, 3));
		}
	}

	// --- skin weights -----------------------------------------------------------------------
	FSkinWeightsVertexAttributesRef SkinWeights = Attributes.GetVertexSkinWeights();
	const int32 BoneCount = Source.Bones.Num();
	for (int32 Index = 0; Index < Source.Vertices.Num(); ++Index)
	{
		const FElysiumSourceVertex& Vertex = Source.Vertices[Index];
		TArray<UE::AnimationCore::FBoneWeight, TInlineAllocator<3>> Influences;
		for (int32 Slot = 0; Slot < 3; ++Slot)
		{
			const int32 Bone = static_cast<int32>(Vertex.Bones[Slot]);
			if (Vertex.Weights[Slot] > 0.0f && Bone >= 0 && Bone < BoneCount)
			{
				Influences.Emplace(static_cast<FBoneIndexType>(Bone), Vertex.Weights[Slot]);
			}
		}
		if (Influences.IsEmpty())
		{
			// Every vertex must bind somewhere or it collapses onto the component origin.
			Influences.Emplace(0, 1.0f);
		}
		SkinWeights.Set(Vertices[Index], UE::AnimationCore::FBoneWeights::Create(Influences));
	}

	// --- morph targets ----------------------------------------------------------------------
	for (const FElysiumSourceMorph& Morph : Source.Morphs)
	{
		const FName MorphName(*Morph.Name);
		if (!Attributes.RegisterMorphTargetAttribute(MorphName, /*bIncludeNormals=*/true))
		{
			return FString::Printf(TEXT("%s: could not register morph target %s"),
				*SourcePath, *Morph.Name);
		}
		TVertexAttributesRef<FVector3f> PositionDeltas =
			Attributes.GetVertexMorphPositionDelta(MorphName);
		TVertexInstanceAttributesRef<FVector3f> NormalDeltas =
			Attributes.GetVertexInstanceMorphNormalDelta(MorphName);
		for (const FElysiumSourceMorphDelta& Delta : Morph.Deltas)
		{
			if (!Vertices.IsValidIndex(static_cast<int32>(Delta.Vertex)))
			{
				continue;
			}
			PositionDeltas.Set(Vertices[Delta.Vertex], Delta.Position);
			for (const FVertexInstanceID Instance : InstancesOfVertex[Delta.Vertex])
			{
				NormalDeltas.Set(Instance, Delta.Normal);
			}
		}
	}

	Mesh->CommitMeshDescription(0);

	if (!Skeleton->MergeAllBonesToBoneTree(Mesh))
	{
		return FString::Printf(
			TEXT("%s: bone tree is incompatible with skeleton %s -- it belongs to another rig family"),
			*SourcePath, *SkeletonAsset);
	}
	Mesh->SetSkeleton(Skeleton);

	Mesh->CalculateInvRefMatrices();
	Mesh->SetImportedBounds(FBoxSphereBounds(Bounds));
	Mesh->Build();
	Mesh->PostEditChange();

	// A morph target only animates through a curve of the same name, and a curve only reaches a
	// morph target if the SKELETON says that is what it is. Serialised here, this is what the
	// runtime otherwise has to write on every transient skeleton it builds.
	for (const TObjectPtr<UMorphTarget>& Morph : Mesh->GetMorphTargets())
	{
		if (Morph == nullptr)
		{
			continue;
		}
		const FName CurveName = Morph->GetFName();
		Skeleton->AddCurveMetaData(CurveName, /*bTransact=*/false);
		if (FCurveMetaData* MetaData = Skeleton->GetCurveMetaData(CurveName))
		{
			MetaData->Type.bMorphtarget = true;
		}
	}

	if (bNewSkeleton)
	{
		FAssetRegistryModule::AssetCreated(Skeleton);
	}
	FAssetRegistryModule::AssetCreated(Mesh);

	if (!SavePackageTo(SkeletonPackage, bSharedSkeleton ? SkeletonPackageName : PackageName))
	{
		return FString::Printf(TEXT("could not save %s"), *SkeletonPackageName);
	}
	if (SkeletonPackage != Package && !SavePackageTo(Package, PackageName))
	{
		return FString::Printf(TEXT("could not save %s"), *PackageName);
	}
	return FString();
#else
	return TEXT("editor only");
#endif
}

int32 UElysiumSkeletalBuildLibrary::ReleaseBakedPackages(const FString& PackagePath)
{
#if WITH_EDITOR
	// `unreal.SystemLibrary.collect_garbage()` frees NOTHING on this path, for two independent
	// reasons. It forwards to UEngine::ForceGarbageCollection, which only raises flags that
	// UEngine::ConditionalCollectGarbage consumes off the engine tick -- and a `-run=pythonscript`
	// commandlet returns without ever ticking. And GARBAGE_COLLECTION_KEEPFLAGS is RF_Standalone
	// whenever GIsEditor, which every baked asset carries by construction. So the standalone flag
	// has to come off before a synchronous collect will take anything.
	TArray<UPackage*> Releasing;
	for (TObjectIterator<UPackage> It; It; ++It)
	{
		UPackage* Package = *It;
		// A dirty package is unsaved work; releasing it would discard the bake's own output.
		if (Package == nullptr || Package->IsDirty() || Package == GetTransientPackage())
		{
			continue;
		}
		if (Package->GetName().StartsWith(PackagePath))
		{
			Releasing.Add(Package);
		}
	}
	for (UPackage* Package : Releasing)
	{
		ForEachObjectWithPackage(Package, [](UObject* Object)
			{
				Object->ClearFlags(RF_Standalone);
				return true;
			}, EGetObjectsFlags::None);
	}
	if (!Releasing.IsEmpty())
	{
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, /*bPerformFullPurge=*/true);
	}
	return Releasing.Num();
#else
	return 0;
#endif
}

FString UElysiumSkeletalBuildLibrary::BuildFamilySkeleton(const TArray<FString>& SourcePaths,
	const FString& SkeletonPackageName, bool bRebuild, int32& OutBones)
{
	OutBones = 0;
#if WITH_EDITOR
	if (SourcePaths.IsEmpty())
	{
		return FString::Printf(TEXT("%s: a rig family names no member"), *SkeletonPackageName);
	}

	UPackage* Package = OpenPackage(SkeletonPackageName);
	if (Package == nullptr)
	{
		return FString::Printf(TEXT("could not create package %s"), *SkeletonPackageName);
	}
	const FString AssetName = FPackageName::GetShortName(SkeletonPackageName);
	USkeleton* Skeleton = FindObject<USkeleton>(Package, *AssetName);
	// `bRebuild` is not an optimisation switch. USkeleton::MergeBonesToBoneTree only rebuilds an
	// EMPTY tree and otherwise unions, and OpenPackage loads whatever is already on disk -- so
	// without this a family skeleton can only ever grow, and keeps the bones of members that left
	// the family in an earlier partition. Their bind poses then answer for bones no current member
	// declares, which is exactly the reference pose an untracked bone falls back to.
	const bool bNewSkeleton = Skeleton == nullptr || bRebuild;

	// What a rebuild must NOT take with it. This function owns the BONE TREE and nothing else; the
	// morph-curve metadata and the layer blend masks are registered per member, by the mesh and
	// clip stages, and a slice runs those only for the members it named. So replacing the asset
	// wholesale silently unbinds every facial curve on every member the slice left alone -- the
	// mesh keeps its morph targets, the animation keeps its curves, and nothing connects the two.
	// Carried by NAME rather than by index, because the tree about to be rebuilt is what indices
	// mean; a bone no current member declares simply fails to re-register, which is correct.
	TArray<TPair<FName, FCurveMetaData>> CarriedCurves;
	struct FCarriedProfile
	{
		FName Name;
		EBlendProfileMode Mode = EBlendProfileMode::TimeFactor;
		TArray<TPair<FName, float>> Bones;
	};
	TArray<FCarriedProfile> CarriedProfiles;
	// Carried for the same reason, and it matters as much: a retarget source is registered by the
	// CLIP stage, which a slice runs only for the owners it named, so replacing the asset wholesale
	// would leave every sequence built by an earlier slice naming a pose that is no longer there --
	// and a missing retarget source does not fail, it silently falls back to this skeleton's own
	// reference pose, which is the arbitrary-member bind the mode exists to avoid.
	//
	// Unlike those two it cannot be carried by name, because a reference pose is an array indexed
	// by bone with no names in it. So the OLD tree's names are captured beside it and the pose is
	// re-indexed through them below; a bone the new tree does not carry simply drops out, and one
	// it gains takes the new reference pose's own value.
	TMap<FName, FReferencePose> CarriedRetargetSources;
	TArray<FName> PreviousBoneNames;
	if (Skeleton != nullptr && bNewSkeleton)
	{
		CarriedRetargetSources = Skeleton->AnimRetargetSources;
		const FReferenceSkeleton& Previous = Skeleton->GetReferenceSkeleton();
		PreviousBoneNames.Reserve(Previous.GetRawBoneNum());
		for (int32 Index = 0; Index < Previous.GetRawBoneNum(); ++Index)
		{
			PreviousBoneNames.Add(Previous.GetBoneName(Index));
		}
		Skeleton->ForEachCurveMetaData([&CarriedCurves](FName Name, const FCurveMetaData& Data)
			{
				CarriedCurves.Emplace(Name, Data);
			});
		for (const TObjectPtr<UBlendProfile>& Profile : Skeleton->BlendProfiles)
		{
			if (Profile == nullptr)
			{
				continue;
			}
			FCarriedProfile Carried;
			Carried.Name = Profile->GetFName();
			Carried.Mode = Profile->GetMode();
			for (int32 Index = 0; Index < Profile->GetNumBlendEntries(); ++Index)
			{
				const FBlendProfileBoneEntry& Entry = Profile->GetEntry(Index);
				Carried.Bones.Emplace(Entry.BoneReference.BoneName, Entry.BlendScale);
			}
			CarriedProfiles.Add(MoveTemp(Carried));
		}
	}

	if (bNewSkeleton)
	{
		ClearForRewrite(Package, AssetName);
		Skeleton = NewObject<USkeleton>(Package, *AssetName, RF_Public | RF_Standalone);
	}

	// Authored straight onto the USkeleton, because a bank has no mesh to take a tree from -- and
	// because a body family's tree must not depend on which of its members a slice named. The
	// modifier's USkeleton* constructor is the same door an importer uses; its destructor is what
	// rebuilds the remapping tables, so the scope has to close before anything reads the tree.
	// One scope over every member, so the whole family costs one save rather than one per member.
	{
		FReferenceSkeletonModifier Modifier(Skeleton);
		const FReferenceSkeleton& Ref = Skeleton->GetReferenceSkeleton();
		for (const FString& SourcePath : SourcePaths)
		{
			FElysiumSkeletalSource Source;
			FString Error;
			if (!FElysiumSkeletalSource::LoadBones(SourcePath, Source, Error))
			{
				return Error;
			}
			if (Source.Bones.IsEmpty())
			{
				return FString::Printf(TEXT("%s carries no bones"), *SourcePath);
			}

			// Source bone index -> index in the skeleton being built. A bone the tree already
			// carries keeps the index it has, so a later member merges in rather than duplicating
			// the core. The FIRST member to declare a bone owns its reference transform, which is
			// why the caller passes the family's members in a declared, stable order.
			TArray<int32> Mapped;
			Mapped.Init(INDEX_NONE, Source.Bones.Num());
			int32 Next = Ref.GetRawBoneNum();
			for (int32 Index = 0; Index < Source.Bones.Num(); ++Index)
			{
				const FElysiumSourceBone& Bone = Source.Bones[Index];
				const int32 Existing = Ref.FindRawBoneIndex(Bone.Name);
				if (Existing != INDEX_NONE)
				{
					Mapped[Index] = Existing;
					continue;
				}
				const int32 Parent = Bone.Parent == INDEX_NONE ? INDEX_NONE : Mapped[Bone.Parent];
				if (Parent == INDEX_NONE && Next > 0)
				{
					// A second root is what USkeleton refuses at mesh-build time, long after this.
					// Reported here, where the tree that caused it is still in hand.
					return FString::Printf(
						TEXT("%s: '%s' would be a second root on %s"),
						*SourcePath, *Bone.Name.ToString(), *AssetName);
				}
				// Authored ROTATION-FLAT: translations verbatim, every local rotation identity.
				// Nothing skins from this pose -- a body skins from its own mesh reference
				// skeleton, an untracked bone resolves to that same mesh pose, and
				// `OrientAndScale` compares against the mesh's bind -- but `FSkeletonRemapping`
				// reads it, and reads it at BOTH ends. `DecompressPose` builds `Q0 = PT^-1 * PS`
				// from the two skeletons' component rotations and applies it to every translation
				// and rotation it decodes; `OrientAndScale` then declines to correct any bone whose
				// authored and target bind translations agree, which is exactly the body the clip
				// was authored on. A clip's limb tracks carry no translation of their own, so what
				// reaches the skin is the authoring rig's limb lengths pointed along a foreign
				// body's axes -- a folded arm on the bodies that match the clip best.
				//
				// Identity locals accumulate to identity component rotations in ANY tree, so `Q0`
				// and `Q1` are identity for every skeleton pair however far two rigs diverge: the
				// decoded quaternion reaches the body verbatim the way retail's does
				// (`docs/vtmb/animation_and_movers.md` A.4b), and the only translation correction
				// left is the one comparison that reads the playing body, `OrientAndScale`.
				//
				// Flat is what makes the result independent of the choice, and the choice is not
				// free. One shared rotation per bone NAME does not survive: a generic appendix name
				// sits under a different ancestor chain on two rigs -- `Bone19` is `Bip01 Head`'s
				// child on one bank and `Bone18`'s on another -- so equal locals still give unequal
				// component rotations, and `Q0` returns at up to 180 degrees on exactly those bones.
				Modifier.Add(FMeshBoneInfo(Bone.Name, Bone.Name.ToString(), Parent),
					FTransform(FQuat::Identity, Bone.Local.GetTranslation(), Bone.Local.GetScale3D()));
				Mapped[Index] = Next++;
			}
		}
	}

	// **The bone tree is not the reference skeleton, and authoring one does not grow the other.**
	// `FReferenceSkeletonModifier` owns bones and bind poses; `BoneTree` is `USkeleton`'s own
	// parallel array, and it is where per-bone retargeting lives. Nothing above touches it, so a
	// skeleton built straight through the modifier carries a full reference skeleton and an EMPTY
	// tree — a state the engine's own merge asserts against
	// (`check(NumBones == ReferenceSkeleton.GetRawRefBoneInfo().Num())`) and which leaves every bone
	// with no retargeting data at all.
	//
	// `MergeAllBonesToBoneTree` is the one public door that fills both: with an empty tree it takes
	// `CreateReferenceSkeletonFromMesh`, which rebuilds the reference skeleton and sizes the tree to
	// match. The carrier holds the skeleton we just authored, so this re-states the same bones rather
	// than changing any of them — it supplies the half the modifier does not own.
	{
		USkeletalMesh* Carrier =
			NewObject<USkeletalMesh>(GetTransientPackage(), NAME_None, RF_Transient);
		Carrier->SetRefSkeleton(Skeleton->GetReferenceSkeleton());
		if (!Skeleton->MergeAllBonesToBoneTree(Carrier))
		{
			return FString::Printf(
				TEXT("%s: the bone tree refused the reference skeleton just authored onto it"),
				*AssetName);
		}
	}

	OutBones = Skeleton->GetReferenceSkeleton().GetRawBoneNum();

	// **`OrientAndScale` is VtMB's own rule, not an approximation of it**, and it is the only mode
	// this skeleton ever sets — there is no per-bone table, because the correction is per bone and
	// the mode computes it.
	//
	// When VtMB's loader chains a shared bank into a body (`$includemodel`), it builds one 56-byte
	// remap record per body bone by case-insensitive NAME match, and where the two models' BIND
	// positions for a matched bone differ it compiles a 3x4 into that record: an axis-angle taken
	// between the two bind direction vectors, scaled by the ratio of their lengths. The pose path
	// then applies that matrix to the decoded position and copies the quaternion VERBATIM
	// (`docs/vtmb/animation_and_movers.md` A.4b). Unreal's `OrientAndScale` builds the same thing —
	// skip when the two translations are equal, `FQuat::FindBetweenNormals(SourceDir, TargetDir)`,
	// `Scale = TargetLength / SourceLength`, rotation untouched (`BoneContainer.cpp`) — so the mode
	// reproduces retail's compiled matrix rather than standing in for it.
	//
	// It also subsumes what the two earlier modes were reaching for. A bone whose bind the two
	// models agree on gets no cached entry and passes through verbatim, which is right because
	// agreeing binds make verbatim correct; a CONSTANT translation track — 96.6% of the corpus, the
	// emitting model's bind written into the clip — hits the mode's own shortcut and resolves to the
	// playing body's bind. `Animation` got the first case right and the second catastrophically
	// wrong (the clavicle's bind spread across the cast is 17.53 cm against 15.16 cm of authored
	// travel); `Skeleton` got the second right and discarded the first, and ZEROED translation
	// outright on a baked additive. `OrientAndScale` skips baked additives too, but there the bind
	// cancels out of the difference on its own, so nothing is lost.
	//
	// **The mode is meaningless without a retarget source**, which is the bind pose the clip was
	// authored against — retail reads it straight off the bank's own header. Unreal takes it from
	// `UAnimSequence::RetargetSource`, and with none set it falls back to this skeleton's reference
	// pose, i.e. whichever member the partition happened to list first. `RegisterRetargetSource`
	// below is what supplies it; a sequence built without one has no correction to compute.
	Skeleton->SetBoneTranslationRetargetingMode(0, EBoneTranslationRetargetingMode::OrientAndScale,
		/*bChildrenToo=*/true);

	// Re-indexed into the tree just authored, one bone at a time through the names captured above.
	// A pose that survives this is the same authored bind it always was; only its subscripts moved.
	for (TPair<FName, FReferencePose>& Carried : CarriedRetargetSources)
	{
		const TArray<FTransform> Previous = MoveTemp(Carried.Value.ReferencePose);
		Carried.Value.ReferencePose = Skeleton->GetReferenceSkeleton().GetRefBonePose();
		for (int32 Old = 0; Old < PreviousBoneNames.Num(); ++Old)
		{
			const int32 New = Skeleton->GetReferenceSkeleton().FindRawBoneIndex(PreviousBoneNames[Old]);
			if (Previous.IsValidIndex(Old) && Carried.Value.ReferencePose.IsValidIndex(New))
			{
				Carried.Value.ReferencePose[New] = Previous[Old];
			}
		}
	}
	Skeleton->AnimRetargetSources = MoveTemp(CarriedRetargetSources);

	// Re-registered only now: both resolve bone names against the tree, so they need the modifier
	// scope closed and the remapping tables rebuilt.
	for (const TPair<FName, FCurveMetaData>& Curve : CarriedCurves)
	{
		Skeleton->AddCurveMetaData(Curve.Key, /*bTransact=*/false);
		if (FCurveMetaData* MetaData = Skeleton->GetCurveMetaData(Curve.Key))
		{
			*MetaData = Curve.Value;
		}
	}
	for (const FCarriedProfile& Carried : CarriedProfiles)
	{
		UBlendProfile* Profile = Skeleton->GetBlendProfile(Carried.Name);
		if (Profile == nullptr)
		{
			Profile = Skeleton->CreateNewBlendProfile(Carried.Name);
		}
		if (Profile == nullptr)
		{
			continue;
		}
		// The mode before the scales, always: an entry equal to the mode's own default is not
		// stored, and that default is 0 for BlendMask against 1 for every other mode.
		Profile->Mode = Carried.Mode;
		for (const TPair<FName, float>& Bone : Carried.Bones)
		{
			Profile->SetBoneBlendScale(Bone.Key, Bone.Value, /*bRecurse=*/false, /*bCreate=*/true);
		}
	}

	if (bNewSkeleton)
	{
		FAssetRegistryModule::AssetCreated(Skeleton);
	}
	if (!SavePackageTo(Package, SkeletonPackageName))
	{
		return FString::Printf(TEXT("could not save %s"), *SkeletonPackageName);
	}
	return FString();
#else
	return TEXT("editor only");
#endif
}

FString UElysiumSkeletalBuildLibrary::BuildSkeletonFromSource(const FString& SourcePath,
	const FString& SkeletonPackageName)
{
	int32 Bones = 0;
	return BuildFamilySkeleton({ SourcePath }, SkeletonPackageName, /*bRebuild=*/false, Bones);
}

FString UElysiumSkeletalBuildLibrary::DeclareCompatibleSkeletons(const FString& SkeletonPackageName,
	const TArray<FString>& SourceSkeletonPackageNames)
{
#if WITH_EDITOR
	USkeleton* Target = LoadObject<USkeleton>(nullptr,
		*(SkeletonPackageName + TEXT(".") + FPackageName::GetShortName(SkeletonPackageName)));
	if (Target == nullptr)
	{
		return FString::Printf(TEXT("skeleton %s did not load"), *SkeletonPackageName);
	}

	// The direction is "this skeleton may play animations authored on that one", so the bank is the
	// argument and the body's own rig is the target. Declaring it is not a merge: the engine builds
	// a name-keyed bone map per pair, and a bank bone this rig has never had maps to INDEX_NONE and
	// is dropped -- the same rule the per-family bake applied by leaving the track unbound.
	for (const FString& SourceName : SourceSkeletonPackageNames)
	{
		USkeleton* Source = LoadObject<USkeleton>(nullptr,
			*(SourceName + TEXT(".") + FPackageName::GetShortName(SourceName)));
		if (Source == nullptr)
		{
			return FString::Printf(TEXT("compatible skeleton %s did not load"), *SourceName);
		}
		if (Source != Target)
		{
			Target->AddCompatibleSkeleton(Source);
		}
	}

	if (!SavePackageTo(Target->GetPackage(), SkeletonPackageName))
	{
		return FString::Printf(TEXT("could not save %s"), *SkeletonPackageName);
	}
	return FString();
#else
	return TEXT("editor only");
#endif
}

FString UElysiumSkeletalBuildLibrary::BuildAnimSequencesFromSource(const FString& SourcePath,
	const FString& PackagePath, const FString& SkeletonPackageName, int32& OutClipCount,
	int32& OutDroppedTracks)
{
	OutClipCount = 0;
	OutDroppedTracks = 0;
#if WITH_EDITOR
	FElysiumSkeletalSource Source;
	FString Error;
	if (!FElysiumSkeletalSource::Load(SourcePath, Source, Error))
	{
		return Error;
	}

	USkeleton* Skeleton = LoadObject<USkeleton>(nullptr,
		*(SkeletonPackageName + TEXT(".") + FPackageName::GetShortName(SkeletonPackageName)));
	if (Skeleton == nullptr)
	{
		return FString::Printf(TEXT("skeleton %s did not load"), *SkeletonPackageName);
	}
	const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();

	// A body's own container carries geometry; a shared animation bank carries none. That is the
	// difference that decides whether an unresolved bone is a defect or a fact of sharing, and it is
	// checked here rather than per track so the bake stops before writing a single short clip.
	//
	// The whole bone list, not just the ones some clip animates: the skeleton was merged from this
	// body's mesh, so every one of its bones must be on it whether or not anything drives it yet,
	// and asserting the wider set catches a lost bone where it happens instead of wherever a clip
	// first misses it.
	if (!Source.Vertices.IsEmpty())
	{
		TArray<FString> Missing;
		for (const FElysiumSourceBone& Bone : Source.Bones)
		{
			if (RefSkeleton.FindBoneIndex(Bone.Name) == INDEX_NONE)
			{
				Missing.Add(Bone.Name.ToString());
			}
		}
		if (!Missing.IsEmpty())
		{
			// Deliberately does not name a cause. The mesh build for this stem may have failed
			// earlier in the same run -- `bake_characters.py` records that and carries on -- in
			// which case the skeleton never saw these bones rather than losing them, and the real
			// error is already in the log above this one. Naming the merge would send the operator
			// past it.
			return FString::Printf(
				TEXT("%s: %d of %d bones are not on skeleton %s (%s) -- every clip here would bake ")
				TEXT("that many tracks short, so nothing is written; check whether this model's ")
				TEXT("mesh built at all earlier in this run"),
				*SourcePath, Missing.Num(), Source.Bones.Num(),
				*FPackageName::GetShortName(SkeletonPackageName), *FString::Join(Missing, TEXT(", ")));
		}
	}

	// Bank bones this family has never had. Named rather than only counted: which ones they are is
	// what says "another clan's hair chain" rather than "the merge dropped something".
	TSet<FName> Unresolved;

	// The pose every sequence below is a difference from, and the appendix bones none of them
	// animates. Both are properties of the whole container, so both are resolved once.
	//
	// **Silencing applies to a BANK and not to a body's own container**, on the same test that
	// separates them above. The leak it closes is a rest pose delivered to a body that did not
	// author it, which only a shared clip can do; a body's own clips play on that body alone, where
	// the track states its own bind and dropping it would trade a correct authored pose for a
	// reliance on the mesh supplying the same value.
	const FName RetargetSource = RegisterRetargetSource(Skeleton, SourcePath, Source);
	const TSet<int32> Silent = Source.Vertices.IsEmpty()
		? SilentAppendixBones(Source) : TSet<int32>();

	// --- blend masks ---------------------------------------------------------------------------
	// A layer sequence owns some of the rig and leaves the rest to the pose it is composed over,
	// stated per bone as the animation record's `weight`@0 (`docs/vtmb/animation_and_movers.md`
	// A.4). That gate becomes one `UBlendProfile` in BlendMask mode on the SHARED skeleton, which
	// is the asset a layered blend already consumes, and the sequence carries its name.
	//
	// The profile is content-addressed by the bones it owns, so the same gate reached from two
	// banks resolves to one asset and a re-bake of a different slice cannot rename it out from
	// under a sequence already pointing at it. The whole install states four distinct layer masks,
	// so this is a handful of assets per family rather than a table per clip.
	struct FMaskProfile
	{
		FName Profile = NAME_None;
		int32 OwnedBones = 0;
		bool bResolved = false;
	};
	TArray<FMaskProfile> MaskProfiles;
	MaskProfiles.SetNum(Source.Masks.Num());
	int32 ProfilesCreated = 0;

	auto MaskProfileFor = [&](const int32 MaskIndex) -> const FMaskProfile&
	{
		FMaskProfile& Entry = MaskProfiles[MaskIndex];
		if (Entry.bResolved)
		{
			return Entry;
		}
		Entry.bResolved = true;

		TArray<FName> Owned;
		const FElysiumSourceMask& Mask = Source.Masks[MaskIndex];
		for (int32 Bone = 0; Bone < Source.Bones.Num(); ++Bone)
		{
			// A bank names bones this family has never had, and they leave the mask for the same
			// reason their tracks are dropped: there is nothing here for them to own.
			if (Mask.Bones[Bone] != 0 && RefSkeleton.FindBoneIndex(Source.Bones[Bone].Name) != INDEX_NONE)
			{
				Owned.Add(Source.Bones[Bone].Name);
			}
		}
		if (Owned.IsEmpty())
		{
			return Entry;
		}
		Owned.Sort([](const FName& A, const FName& B) { return A.Compare(B) < 0; });
		FString Joined;
		for (const FName& BoneName : Owned)
		{
			Joined += BoneName.ToString();
			Joined += TEXT("|");
		}
		Entry.Profile = FName(*FString::Printf(TEXT("ElysiumLayerMask_%08X"), FCrc::StrCrc32(*Joined)));
		Entry.OwnedBones = Owned.Num();
		if (Skeleton->GetBlendProfile(Entry.Profile) == nullptr)
		{
			UBlendProfile* Profile = Skeleton->CreateNewBlendProfile(Entry.Profile);
			// The mode FIRST, and it is not cosmetic. An entry equal to the mode's own default is
			// not stored, and that default is 0 for a blend mask against 1 for every other mode --
			// so writing the weights while the profile is still WeightFactor discards every one of
			// them and leaves an empty profile, which reads as owning the whole rig.
			Profile->Mode = EBlendProfileMode::BlendMask;
			for (const FName& BoneName : Owned)
			{
				Profile->SetBoneBlendScale(BoneName, 1.0f, /*bRecurse=*/false, /*bCreate=*/true);
			}
			++ProfilesCreated;
		}
		return Entry;
	};

	// Resolved and saved BEFORE the first sequence is written, rather than as each masked clip is
	// reached. The profiles live on the skeleton, which this pass does not otherwise touch — the
	// mesh pass saved it before any clip was read — so leaving the write until the end would
	// interleave a save of the skeleton with saves of sequences bound to it. Doing it up front
	// keeps the bake's order the same whether or not a container happens to carry a mask.
	//
	// An additive is skipped here for the reason given at its own branch below: its mask needs no
	// asset, and creating one would put a profile on the skeleton that nothing ever reads.
	for (const FElysiumSourceClip& Clip : Source.Clips)
	{
		if ((Clip.Flags & 0x4) == 0 && Source.Masks.IsValidIndex(Clip.Mask))
		{
			MaskProfileFor(Clip.Mask);
		}
	}
	// Unconditional, because the retarget source registered above already changed the skeleton
	// whether or not this container also contributed a blend mask. A sequence whose
	// `RetargetSource` names a pose the saved skeleton does not carry retargets against the
	// skeleton's own reference pose instead, silently and only in whichever process reloads it.
	{
		UPackage* SkeletonPackage = Skeleton->GetPackage();
		if (SkeletonPackage == nullptr || !SavePackageTo(SkeletonPackage, SkeletonPackage->GetName()))
		{
			return FString::Printf(TEXT("could not save %s after registering retarget source %s%s"),
				*SkeletonPackageName, *RetargetSource.ToString(),
				ProfilesCreated > 0 ? TEXT(" and its blend mask(s)") : TEXT(""));
		}
		if (ProfilesCreated > 0)
		{
			UE_LOG(LogElysiumSkeletalBuild, Display, TEXT("%s: %d blend mask(s) on %s"),
				*FPaths::GetBaseFilename(SourcePath), ProfilesCreated,
				*FPackageName::GetShortName(SkeletonPackageName));
		}
	}

	// Poses first, then the additives that are differences from them. An additive names its base
	// as an asset, so the base has to exist and be resolvable by the time it is set; ordering the
	// two passes here is what guarantees that without a second lookup pass or a fixup.
	TArray<const FElysiumSourceClip*> Ordered;
	Ordered.Reserve(Source.Clips.Num());
	int32 UnboundAdditives = 0;
	// Every asset name this run wrote into the owner's folder, which is what the sweep below
	// measures the folder's contents against.
	TSet<FString> WrittenAssets;
	for (const FElysiumSourceClip& Clip : Source.Clips)
	{
		if (Clip.BaseName.IsEmpty())
		{
			// A `_delta` no host declares has nothing to be a difference FROM. Retail only ever
			// reaches one through the autolayer binding that names its base, so an asset here would
			// be a clip that is arithmetically a delta and semantically nothing -- exactly the trap
			// of composing a delta over a base that did not declare it. It is not built.
			if ((Clip.Flags & 0x4) != 0)
			{
				++UnboundAdditives;
				continue;
			}
			Ordered.Add(&Clip);
		}
	}
	for (const FElysiumSourceClip& Clip : Source.Clips)
	{
		if (!Clip.BaseName.IsEmpty())
		{
			Ordered.Add(&Clip);
		}
	}
	if (UnboundAdditives > 0)
	{
		UE_LOG(LogElysiumSkeletalBuild, Verbose,
			TEXT("%s: %d additive(s) no host declares, not built"),
			*FPaths::GetBaseFilename(SourcePath), UnboundAdditives);
	}

	TMap<FString, UAnimSequence*> BuiltByName;
	BuiltByName.Reserve(Ordered.Num());
	for (const FElysiumSourceClip* ClipPtr : Ordered)
	{
		const FElysiumSourceClip& Clip = *ClipPtr;
		if (Clip.FrameCount <= 0 || Clip.Tracks.IsEmpty())
		{
			continue;
		}
		const FString AssetName = TEXT("A_") + FElysiumContentPaths::BakedAssetName(Clip.Name);
		const FString PackageName = PackagePath / AssetName;
		UPackage* Package = OpenPackage(PackageName);
		if (Package == nullptr)
		{
			return FString::Printf(TEXT("could not create package %s"), *PackageName);
		}
		ClearForRewrite(Package, AssetName);

		UAnimSequence* Sequence = NewObject<UAnimSequence>(Package, *AssetName,
			RF_Public | RF_Standalone);
		Sequence->SetSkeleton(Skeleton);
		// Names the bind pose these keys were authored against, which is what `OrientAndScale`
		// measures the playing body's own bind against. Set before anything reads the sequence.
		Sequence->RetargetSource = RetargetSource;

		IAnimationDataController& Controller = Sequence->GetController();
		Controller.OpenBracket(NSLOCTEXT("Elysium", "BakeClip", "Baking VtMB clip"),
			/*bShouldTransact=*/false);
		Controller.InitializeModel();
		// Rational, not rounded. FFrameRate carries a numerator and a denominator, and
		// IAnimationDataController accepts any rate whose interval is non-zero -- it does not
		// require an integer, and while the model is unpopulated it does not even require a
		// multiple of the current rate. Rounding cost `walk_0` 0.53% of its cycle, a permanent
		// phase drift against every other cell of the same move_yaw fan; flooring at 1 fps made
		// the two 0.1 fps payphone idles play ten times too fast.
		const double AuthoredRate = FMath::Max(static_cast<double>(Clip.FrameRate), UE_KINDA_SMALL_NUMBER);
		Controller.SetFrameRate(FFrameRate(FMath::RoundToInt(AuthoredRate * 1000.0), 1000), false);
		// A model's playable frame count is one less than its key count, and it must be at least
		// one -- so a single-frame pose becomes a two-key clip holding that pose.
		const int32 KeyCount = FMath::Max(Clip.FrameCount, 2);
		Controller.SetNumberOfFrames(FFrameNumber(KeyCount - 1), false);

		// The `_delta` family. `STUDIO_DELTA` (0x4) marks a clip whose tracks state a DIFFERENCE
		// from a base pose rather than a pose of its own, which is what Unreal calls a local-space
		// additive. The container names that base, because VtMB post-multiplies its delta and every
		// `EAdditiveAnimationType` pre-multiplies: the conversion is a conjugation by the base's
		// rotation, so which base is not a detail. The clip's tracks hold the base with the delta
		// already composed onto it, and the compressor's subtraction is what performs that
		// conjugation -- `(Base * Delta) * Base^-1` is the delta in Unreal's own order.
		// Keyed on the studio flag, NOT on carrying a base. A derived OVERLAY carries one too --
		// it is a masked layer written against the host whose chain resolves its split bone -- but
		// it states a pose and composes through a layered blend, so it is an ordinary clip with a
		// blend profile and no additive stamp.
		const bool bAdditive = (Clip.Flags & 0x4) != 0 && !Clip.BaseName.IsEmpty();
		UAnimSequence* const* BaseSequence = BuiltByName.Find(Clip.BaseName);
		if (bAdditive)
		{
			if (BaseSequence == nullptr)
			{
				return FString::Printf(TEXT("%s is a difference from %s, which did not build"),
					*Clip.Name, *Clip.BaseName);
			}
			Sequence->AdditiveAnimType = AAT_LocalSpaceBase;
			Sequence->RefPoseType = ABPT_AnimFrame;
			Sequence->RefPoseSeq = *BaseSequence;
			Sequence->RefFrameIndex = 0;
		}

		// Resolved in the pre-pass above. An ADDITIVE's mask needs no asset, and giving it one would
		// be an asset nothing reads: a bone outside the mask animates nothing, the exporter drops
		// its channel-less track, and a missing track on an additive evaluates to the additive
		// identity -- so the bone already contributes no change and gating it would be the same
		// no-op. That equivalence belongs to the additive identity, not to the mask, so it does not
		// carry over to an ordinary layer, where a masked bone must keep the BASE pose and an owned
		// one with no track holds its BIND.
		const FMaskProfile* MaskProfile = !bAdditive && MaskProfiles.IsValidIndex(Clip.Mask)
			&& MaskProfiles[Clip.Mask].Profile != NAME_None ? &MaskProfiles[Clip.Mask] : nullptr;

		int32 BoundTracks = 0;
		TSet<int32> Tracked;
		Tracked.Reserve(Clip.Tracks.Num());
		for (const FElysiumSourceTrack& Track : Clip.Tracks)
		{
			if (!Source.Bones.IsValidIndex(Track.Bone))
			{
				continue;
			}
			const FName BoneName = Source.Bones[Track.Bone].Name;
			const int32 SkeletonBone = RefSkeleton.FindBoneIndex(BoneName);
			if (SkeletonBone == INDEX_NONE)
			{
				// Only reachable from a bank -- an own body returned above rather than get here.
				Unresolved.Add(BoneName);
				++OutDroppedTracks;
				continue;
			}
			// An appendix bone this container never animates states the emitting model's rest pose,
			// and binding it by name hands that rest pose to whatever chain shares the name. Left
			// untracked the bone resolves to the playing mesh's own bind instead.
			if (Silent.Contains(Track.Bone))
			{
				++OutDroppedTracks;
				continue;
			}

			// A channel the clip leaves alone holds the bind value from the file that AUTHORED the
			// clip, not from the shared skeleton. The two differ on exactly the bones a rig family
			// disagrees about -- the `[2]` fork and the appendix chains, whose generic names denote
			// different chains on different bodies -- and taking the skeleton's would hand every
			// member of a family whichever member happened to seed it.
			//
			// An ADDITIVE needs no special case here, and that is the point of the container naming
			// its base. `FCompressibleAnimData::BakeOutAdditiveIntoRawData` composes an additive
			// down by `Target * Base^-1` before compressing, so the raw keys have to be the delta
			// composed ONTO the base it is declared against -- and that composed pose is exactly
			// what the exporter wrote. A derived clip carries every bone for the same subtraction:
			// a bone with no track evaluates to the shared skeleton's reference pose rather than to
			// the base, which would subtract into a spurious delta rather than an identity one.
			const FTransform& Bind = Source.Bones[Track.Bone].Local;
			TArray<FVector3f> Positions;
			TArray<FQuat4f> Rotations;
			TArray<FVector3f> Scales;
			Positions.Reserve(KeyCount);
			Rotations.Reserve(KeyCount);
			Scales.Init(FVector3f::OneVector, KeyCount);
			for (int32 Key = 0; Key < KeyCount; ++Key)
			{
				const int32 Frame = FMath::Min(Key, Clip.FrameCount - 1);
				Positions.Add(Track.Translations.IsValidIndex(Frame)
					? Track.Translations[Frame] : FVector3f(Bind.GetTranslation()));
				Rotations.Add(Track.Rotations.IsValidIndex(Frame)
					? Track.Rotations[Frame] : FQuat4f(Bind.GetRotation()));
			}
			Controller.AddBoneCurve(BoneName, false);
			Controller.SetBoneTrackKeys(BoneName, Positions, Rotations, Scales, false);
			Tracked.Add(Track.Bone);
			++BoundTracks;
		}

		// A bone the overlay OWNS and does not animate holds its BIND pose, and that is a real
		// authored pose rather than an absence -- 1,346 records across the shipped `*_layer` clips.
		// The exporter drops a channel-less track, so without this the sequence evaluates to the
		// SHARED SKELETON's reference pose there, which is whichever body of the family seeded it,
		// and the overlay would quietly pull those bones onto another model's bind.
		if (MaskProfile != nullptr)
		{
			const FElysiumSourceMask& Mask = Source.Masks[Clip.Mask];
			for (int32 Bone = 0; Bone < Source.Bones.Num(); ++Bone)
			{
				const FName BoneName = Source.Bones[Bone].Name;
				// `Silent` for the same reason as above, and it has to be honoured here too: the
				// mask is exactly what forces a bind track onto an appendix bone nothing animates.
				if (Mask.Bones[Bone] == 0 || Tracked.Contains(Bone) || Silent.Contains(Bone)
					|| RefSkeleton.FindBoneIndex(BoneName) == INDEX_NONE)
				{
					continue;
				}
				const FTransform& Bind = Source.Bones[Bone].Local;
				TArray<FVector3f> Positions;
				TArray<FQuat4f> Rotations;
				TArray<FVector3f> Scales;
				Positions.Init(FVector3f(Bind.GetTranslation()), KeyCount);
				Rotations.Init(FQuat4f(Bind.GetRotation()), KeyCount);
				Scales.Init(FVector3f::OneVector, KeyCount);
				Controller.AddBoneCurve(BoneName, false);
				Controller.SetBoneTrackKeys(BoneName, Positions, Rotations, Scales, false);
				++BoundTracks;
			}
		}

		Controller.NotifyPopulated();
		Controller.CloseBracket(false);

		if (BoundTracks == 0)
		{
			// Nothing on this skeleton to drive: the package would be a sequence that poses
			// nothing, which reads as a successful bake and is not one.
			Sequence->ClearFlags(RF_Public | RF_Standalone);
			Sequence->Rename(nullptr, GetTransientPackage(),
				REN_DontCreateRedirectors | REN_NonTransactional);
			continue;
		}

		// Carried ON the sequence rather than in a table beside it: a clip that says which bones it
		// owns can be composed correctly by anything that opens it, which is the whole point of
		// baking assets instead of rules.
		if (MaskProfile != nullptr)
		{
			UElysiumAnimLayerMask* LayerMask = NewObject<UElysiumAnimLayerMask>(Sequence);
			LayerMask->Profile = MaskProfile->Profile;
			LayerMask->OwnedBones = MaskProfile->OwnedBones;
			Sequence->AddMetaData(LayerMask);
		}

		Sequence->PostEditChange();
		FAssetRegistryModule::AssetCreated(Sequence);
		// Only a clip that actually built is a base anything may name, which is why this is
		// recorded here rather than when the package was opened.
		BuiltByName.Add(Clip.Name, Sequence);
		WrittenAssets.Add(AssetName);
		if (!SavePackageTo(Package, PackageName))
		{
			return FString::Printf(TEXT("could not save %s"), *PackageName);
		}
		++OutClipCount;
	}

	// Sweep this owner's folder against what the run actually wrote.
	//
	// Deleting per skipped clip is not enough, because the strongest kind of orphan is one the
	// CONTAINER no longer lists at all -- a clip that used to bake under its plain label and now
	// ships only in derived form. There is no loop over those; the only record that they are stale
	// is that nothing wrote them. The mount resolves by name, so an orphan keeps answering for a
	// label whose meaning changed underneath it, which is worse than a missing asset.
	//
	// Scoped to the `A_` prefix: blend spaces are `BS_` and are written by a later pass over the
	// same folder, so sweeping everything here would delete assets that have not been built yet.
	{
		const FString Directory = FPaths::GetPath(FPackageName::LongPackageNameToFilename(
			PackagePath / TEXT("A"), FPackageName::GetAssetPackageExtension()));
		TArray<FString> OnDisk;
		IFileManager::Get().FindFiles(OnDisk, *(Directory / TEXT("A_*.uasset")), true, false);
		int32 Swept = 0;
		for (const FString& File : OnDisk)
		{
			if (!WrittenAssets.Contains(FPaths::GetBaseFilename(File)))
			{
				Swept += SweepOrphan(PackagePath / FPaths::GetBaseFilename(File)) ? 1 : 0;
			}
		}
		if (Swept > 0)
		{
			UE_LOG(LogElysiumSkeletalBuild, Display, TEXT("%s: swept %d orphaned sequence(s)"),
				*FPaths::GetBaseFilename(SourcePath), Swept);
		}
	}

	if (!Unresolved.IsEmpty())
	{
		TArray<FString> Names;
		Names.Reserve(Unresolved.Num());
		for (const FName& Name : Unresolved)
		{
			Names.Add(Name.ToString());
		}
		Names.Sort();
		UE_LOG(LogElysiumSkeletalBuild, Display,
			TEXT("%s: %d track(s) dropped across %d bone(s) absent from %s (%s)"),
			*FPaths::GetBaseFilename(SourcePath), OutDroppedTracks, Names.Num(),
			*FPackageName::GetShortName(SkeletonPackageName), *FString::Join(Names, TEXT(", ")));
	}
	return FString();
#else
	return TEXT("editor only");
#endif
}

FString UElysiumSkeletalBuildLibrary::BuildBlendSpacesFromGrids(const FString& BlendsRelPath,
	const FString& PackagePath, const FString& SkeletonPackageName, int32& OutSpaceCount,
	int32& OutSkippedGrids, int32& OutSkippedCells)
{
	OutSpaceCount = 0;
	OutSkippedGrids = 0;
	OutSkippedCells = 0;
#if WITH_EDITOR
	// Every blend space this run wrote, which the sweep at the end measures the folder against.
	TSet<FString> WrittenSpaces;
	// The runtime's own reader, not a second parse of the same document. It already knows the
	// sidecar's shape -- the pose-parameter array a grid's axes index into, the null cell, the
	// leading '@' a raw label can carry -- and a bake that read the file its own way could disagree
	// with the runtime about what a grid says while both looked correct.
	FElysiumBlendTable Table;
	FString Error;
	if (!Table.Load(BlendsRelPath, Error))
	{
		return FString::Printf(TEXT("%s: %s"), *BlendsRelPath, *Error);
	}

	USkeleton* Skeleton = LoadObject<USkeleton>(nullptr,
		*(SkeletonPackageName + TEXT(".") + FPackageName::GetShortName(SkeletonPackageName)));
	if (Skeleton == nullptr)
	{
		return FString::Printf(TEXT("skeleton %s did not load"), *SkeletonPackageName);
	}

	// Which hosts declare each grid, from the autolayer binding the same sidecar carries. A grid
	// whose cells ship only in derived form -- an aim grid, whose cells own the split bone -- has
	// to be built once per declaring host over that host's cells, because there is no host-free
	// form of those cells to sample. Every other grid builds once and this stays empty for it.
	TMap<FString, TArray<FString>> HostsByTarget;
	for (const TPair<FString, FElysiumAutoLayerBinding>& Binding : Table.AutoLayers)
	{
		for (const FString& Target : Binding.Value.Clips)
		{
			HostsByTarget.FindOrAdd(Target).AddUnique(Binding.Key);
		}
	}
	for (TPair<FString, TArray<FString>>& Row : HostsByTarget)
	{
		Row.Value.Sort([](const FString& A, const FString& B) { return A < B; });
	}

	// Sorted so a re-bake writes the same assets in the same order; TMap iteration is not stable.
	TArray<FString> Labels;
	Table.Grids.GetKeys(Labels);
	Labels.Sort([](const FString& A, const FString& B) { return A < B; });

	// One entry per asset to write: the grid, and the host suffix its cells carry ("" for none).
	TArray<TPair<FString, FString>> Builds;
	for (const FString& Label : Labels)
	{
		const TArray<FString>* Hosts = HostsByTarget.Find(Label);
		if (Hosts == nullptr || Hosts->IsEmpty())
		{
			Builds.Emplace(Label, FString());
			continue;
		}
		for (const FString& Host : *Hosts)
		{
			Builds.Emplace(Label, Host);
		}
	}

	for (const TPair<FString, FString>& Build : Builds)
	{
		const FString& Label = Build.Key;
		const FString& Host = Build.Value;
		const FString Suffix = Host.IsEmpty() ? FString() : TEXT("@") + Host;
		const FElysiumBlendGrid& Grid = Table.Grids[Label];

		// Which axes this grid actually spans. Axis 1 is absent on every 9x1 locomotion fan, and its
		// range is then a degenerate 0/0 that must not reach a divisor.
		const int32 Axes = (Grid.GroupSize[1] > 1 && Grid.ParamIndex[1] != INDEX_NONE) ? 2 : 1;

		// Resolve the samples BEFORE creating the package, so a grid that cannot be built leaves no
		// half-written asset on the mount for the next run to load and trust.
		struct FGridSample
		{
			UAnimSequence* Sequence = nullptr;
			FVector Value = FVector::ZeroVector;
			int32 Axis[2] = { 0, 0 };
		};
		TArray<FGridSample> Samples;
		Samples.Reserve(Grid.Cells.Num());
		int32 SkippedHere = 0;

		for (const FElysiumBlendCell& Cell : Grid.Cells)
		{
			if (Cell.Clip.IsEmpty())
			{
				++SkippedHere;
				continue;
			}
			const FString ClipAsset = TEXT("A_")
				+ FElysiumContentPaths::BakedAssetName(Cell.Clip + Suffix);
			UAnimSequence* Sequence = LoadObject<UAnimSequence>(nullptr,
				*(PackagePath / ClipAsset + TEXT(".") + ClipAsset));
			if (Sequence == nullptr)
			{
				++SkippedHere;
				continue;
			}

			FGridSample Sample;
			Sample.Sequence = Sequence;
			Sample.Axis[0] = Cell.Axis[0];
			Sample.Axis[1] = Cell.Axis[1];
			for (int32 Axis = 0; Axis < Axes; ++Axis)
			{
				// Where cell k sits on its axis. `ElysiumBlendGrids::ResolveAxis` normalizes the
				// parameter over the DESCRIPTOR's start..end and then remaps through the grid's own
				// paramstart..paramend -- and the descriptor cancels out of that pair exactly, leaving
				// `(value - ParamStart) / (ParamEnd - ParamStart)`. So the sample positions are the
				// grid's own range and owe the pose parameter nothing; only the wrap consults it.
				//
				// Divided by `GroupSize - 1` because the cells are the range's ENDPOINTS, not its
				// buckets: on a 9-cell -180..180 fan cell 0 IS -180 and cell 8 IS +180, which is why
				// those two share one clip, and why 0 degrees lands exactly on cell 4.
				const int32 Count = Grid.GroupSize[Axis];
				const float Alpha = Count > 1
					? static_cast<float>(Sample.Axis[Axis]) / static_cast<float>(Count - 1) : 0.f;
				Sample.Value[Axis] = Grid.ParamStart[Axis]
					+ Alpha * (Grid.ParamEnd[Axis] - Grid.ParamStart[Axis]);
			}
			Samples.Add(Sample);
		}

		OutSkippedCells += SkippedHere;
		if (Samples.Num() < 2)
		{
			// One sample is a clip, not a blend space, and the reader drops such a grid too. Reported
			// rather than failed: a hole is a fact about the export, and refusing the whole owner over
			// one damaged grid would cost every sound one beside it.
			++OutSkippedGrids;
			UE_LOG(LogElysiumSkeletalBuild, Warning,
				TEXT("%s: grid '%s' resolved %d of %d cell(s) and is not a blend space"),
				*BlendsRelPath, *Label, Samples.Num(), Grid.Cells.Num());
			continue;
		}

		// A grid whose cells are partial-body `*_layer` overlays composes as ONE layer under ONE bone
		// mask, so every cell has to name the same mask or there is no single gate to compose it
		// under -- no Unreal blend node masks per sample. Fatal rather than reported: the whole reason
		// these grids are baked now is to settle this, and a quiet warning would let a grid that
		// cannot be layered ship looking like one that can.
		auto ProfileOf = [](const UAnimSequence* Sequence)
		{
			const UElysiumAnimLayerMask* Mask = Sequence->FindMetaDataByClass<UElysiumAnimLayerMask>();
			return Mask != nullptr ? Mask->Profile : NAME_None;
		};
		const FName LayerProfile = ProfileOf(Samples[0].Sequence);
		for (int32 Index = 1; Index < Samples.Num(); ++Index)
		{
			const FName Profile = ProfileOf(Samples[Index].Sequence);
			if (Profile != LayerProfile)
			{
				return FString::Printf(
					TEXT("%s: grid '%s' mixes bone masks -- '%s' names %s and '%s' names %s. A grid ")
					TEXT("composes as one layer under one mask, so this one cannot be layered at all"),
					*BlendsRelPath, *Label, *Samples[0].Sequence->GetName(),
					LayerProfile.IsNone() ? TEXT("no mask") : *LayerProfile.ToString(),
					*Samples[Index].Sequence->GetName(),
					Profile.IsNone() ? TEXT("no mask") : *Profile.ToString());
			}
		}

		const FString AssetName = TEXT("BS_")
			+ FElysiumContentPaths::BakedAssetName(Label + Suffix);
		const FString PackageName = PackagePath / AssetName;
		UPackage* Package = OpenPackage(PackageName);
		if (Package == nullptr)
		{
			return FString::Printf(TEXT("could not create package %s"), *PackageName);
		}
		ClearForRewrite(Package, AssetName);

		// The class is the editor's view of the asset and what `GetAxisToScale` answers; it does not
		// pick the evaluation path. `ResampleData` infers dimensionality from the samples' own bounding
		// box, so a 9x1 fan takes the 1D path whichever class carries it.
		UBlendSpace* Space = Axes == 2
			? NewObject<UBlendSpace>(Package, *AssetName, RF_Public | RF_Standalone)
			: NewObject<UBlendSpace1D>(Package, *AssetName, RF_Public | RF_Standalone);

		// BEFORE the first sample. `AddSample` validates the sequence against the blend space's
		// skeleton and drops it silently if they disagree, so a skeleton set afterwards yields an
		// asset with no samples that saves perfectly well.
		Space->SetSkeleton(Skeleton);

		for (int32 Axis = 0; Axis < Axes; ++Axis)
		{
			const FElysiumPoseParamDesc* Desc = Table.Param(Grid.ParamIndex[Axis]);
			// `BlendParameters` is protected and befriends only the editor's detail customizations,
			// and the one public accessor is const. Nothing here is actually const -- the asset was
			// constructed three lines ago -- so this reaches the axis the same way the details panel
			// does, without a reflection walk over a fixed-size struct array to say the same thing.
			FBlendParameter& Parameter = const_cast<FBlendParameter&>(Space->GetBlendParameter(Axis));
			Parameter.DisplayName = Desc != nullptr ? Desc->Name : FString::Printf(TEXT("axis%d"), Axis);
			Parameter.Min = Grid.ParamStart[Axis];
			Parameter.Max = Grid.ParamEnd[Axis];
			// One division per gap between cells, so every cell lands exactly on a grid point.
			Parameter.GridNum = FMath::Max(1, Grid.GroupSize[Axis] - 1);
			// ON for an axis whose pose parameter declares a loop, which is what `move_yaw` is:
			// -180 and +180 are the same heading, and VtMB duplicates the clip at both ends of the
			// fan to say so. Without it `GetClampedAndWrappedBlendInput` CLAMPS, so a heading past
			// the end plays the far extreme -- a body told to move at 260 degrees walks backwards.
			// Duplicating the endpoint stays legal: `IsSameSamplePoint` compares raw components and
			// never consults bWrapInput, so -180 and +180 remain two distinct sample positions.
			// Leave `bInterpolateUsingGrid` alone -- the triangulation path wraps the input into
			// range before its segment search, and turning the grid on would empty the blend data.
			Parameter.bWrapInput = Desc != nullptr && !FMath::IsNearlyZero(Desc->Loop);
		}

		for (const FGridSample& Sample : Samples)
		{
			if (Space->AddSample(Sample.Sequence, Sample.Value) == INDEX_NONE)
			{
				// AddSample reports failure only through this return, so an unchecked add is how a
				// blend space ends up on the mount with fewer samples than cells and nothing said.
				return FString::Printf(
					TEXT("%s: grid '%s' cell [%d,%d] at %s rejected sequence %s -- check the ")
					TEXT("skeleton binding and that the value lies inside %.3f..%.3f"),
					*BlendsRelPath, *Label, Sample.Axis[0], Sample.Axis[1], *Sample.Value.ToString(),
					*Sample.Sequence->GetName(), Grid.ParamStart[0], Grid.ParamEnd[0]);
			}
		}

		// `AddSample` widens the axis range to fit anything that falls outside it, so a range that no
		// longer matches the grid is how a placement error shows itself -- the samples all took, and
		// the asset spans something the sequence never declared.
		for (int32 Axis = 0; Axis < Axes; ++Axis)
		{
			const FBlendParameter& Parameter = Space->GetBlendParameter(Axis);
			if (!FMath::IsNearlyEqual(Parameter.Min, Grid.ParamStart[Axis], 1e-3f)
				|| !FMath::IsNearlyEqual(Parameter.Max, Grid.ParamEnd[Axis], 1e-3f))
			{
				return FString::Printf(
					TEXT("%s: grid '%s' axis %d widened to %.3f..%.3f from the declared %.3f..%.3f, ")
					TEXT("so a sample was placed outside the range the grid states"),
					*BlendsRelPath, *Label, Axis, Parameter.Min, Parameter.Max,
					Grid.ParamStart[Axis], Grid.ParamEnd[Axis]);
			}
		}

		// Builds the triangulation the evaluator reads. Without it the asset carries its samples and
		// blends nothing, which looks like a correct asset in every list that counts samples.
		Space->ResampleData();
		if (Space->GetBlendSpaceData().IsEmpty())
		{
			return FString::Printf(TEXT("%s: grid '%s' produced no blend data from %d sample(s)"),
				*BlendsRelPath, *Label, Samples.Num());
		}

		Space->PostEditChange();
		FAssetRegistryModule::AssetCreated(Space);
		if (!SavePackageTo(Package, PackageName))
		{
			return FString::Printf(TEXT("could not save %s"), *PackageName);
		}
		WrittenSpaces.Add(AssetName);
		++OutSpaceCount;
	}

	// The same sweep the sequence pass runs, over this pass's own prefix. A blend space that stops
	// resolving enough cells is not rewritten, so its previous asset survives -- still pointing at
	// sequences the sequence pass may since have swept. That is a package which loads with
	// "a sample with no/invalid animation" and fails the run, from a bake that logged nothing.
	{
		const FString Directory = FPaths::GetPath(FPackageName::LongPackageNameToFilename(
			PackagePath / TEXT("BS"), FPackageName::GetAssetPackageExtension()));
		TArray<FString> OnDisk;
		IFileManager::Get().FindFiles(OnDisk, *(Directory / TEXT("BS_*.uasset")), true, false);
		int32 Swept = 0;
		for (const FString& File : OnDisk)
		{
			if (!WrittenSpaces.Contains(FPaths::GetBaseFilename(File)))
			{
				Swept += SweepOrphan(PackagePath / FPaths::GetBaseFilename(File)) ? 1 : 0;
			}
		}
		if (Swept > 0)
		{
			UE_LOG(LogElysiumSkeletalBuild, Display, TEXT("%s: swept %d orphaned blend space(s)"),
				*BlendsRelPath, Swept);
		}
	}

	return FString();
#else
	return TEXT("editor only");
#endif
}

FString UElysiumSkeletalBuildLibrary::DescribeAnimSequence(const FString& AssetPath)
{
#if WITH_EDITOR
	UAnimSequence* Sequence = LoadObject<UAnimSequence>(nullptr, *AssetPath);
	if (Sequence == nullptr)
	{
		return FString::Printf(TEXT("%s did not load"), *AssetPath);
	}
	const IAnimationDataModel* Model = Sequence->GetDataModel();
	return FString::Printf(TEXT("frames=%d rate=%s duration=%.4f tracks=%d additive=%d skeleton=%s"),
		Sequence->GetNumberOfSampledKeys(),
		*Sequence->GetSamplingFrameRate().ToPrettyText().ToString(),
		Sequence->GetPlayLength(),
		Model != nullptr ? Model->GetNumBoneTracks() : -1,
		static_cast<int32>(Sequence->AdditiveAnimType),
		Sequence->GetSkeleton() != nullptr ? *Sequence->GetSkeleton()->GetName() : TEXT("none"));
#else
	return TEXT("editor only");
#endif
}

FString UElysiumSkeletalBuildLibrary::DescribeSkeletalMesh(const FString& AssetPath)
{
#if WITH_EDITOR
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *AssetPath);
	if (Mesh == nullptr)
	{
		return FString::Printf(TEXT("%s did not load"), *AssetPath);
	}
	const FMeshDescription* MeshDescription = Mesh->GetMeshDescription(0);

	// A morph target that survives as a name and loses its deltas still counts in GetMorphTargets(),
	// looks right in the editor, and moves nothing. Count the deltas, not the entries.
	int32 MorphDeltas = 0;
	for (const UMorphTarget* Morph : Mesh->GetMorphTargets())
	{
		if (Morph == nullptr)
		{
			continue;
		}
		for (const FMorphTargetLODModel& Lod : Morph->GetMorphLODModels())
		{
			MorphDeltas += Lod.Vertices.Num();
		}
	}

	const FSkeletalMeshModel* Imported = Mesh->GetImportedModel();
	const int32 RenderSections = Mesh->GetResourceForRendering() != nullptr
		&& Mesh->GetResourceForRendering()->LODRenderData.Num() > 0
		? Mesh->GetResourceForRendering()->LODRenderData[0].RenderSections.Num() : -1;

	return FString::Printf(
		TEXT("bones=%d verts=%d morphs=%d morphdeltas=%d slots=%d lodmodels=%d rendersections=%d skeleton=%s"),
		Mesh->GetRefSkeleton().GetNum(),
		MeshDescription != nullptr ? MeshDescription->Vertices().Num() : -1,
		Mesh->GetMorphTargets().Num(),
		MorphDeltas,
		Mesh->GetMaterials().Num(),
		Imported != nullptr ? Imported->LODModels.Num() : -1,
		RenderSections,
		Mesh->GetSkeleton() != nullptr ? *Mesh->GetSkeleton()->GetName() : TEXT("none"));
#else
	return TEXT("editor only");
#endif
}
