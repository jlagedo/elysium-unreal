#include "ElysiumSkeletalBuild.h"

#if WITH_EDITOR
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimSequence.h"
#include "Animation/MorphTarget.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "BoneWeights.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetCommon.h"
#include "MeshDescription.h"
#include "Misc/PackageName.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "SkeletalMeshAttributes.h"
#include "StaticMeshAttributes.h"
#include "ElysiumContentPaths.h"
#include "Visual/ElysiumSkeletalSource.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

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
	const TMap<FString, FString>& MaterialTextures)
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
	// VtMB's skinned vertex carries no normal, so there is nothing to preserve and both are
	// derived from the geometry the same way the glTF path derived them.
	LodInfo.BuildSettings.bRecomputeNormals = true;
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
	TVertexInstanceAttributesRef<FVector2f> UVs = Static.GetVertexInstanceUVs();
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

	for (const FElysiumSourceSection& Section : Source.Sections)
	{
		const FPolygonGroupID Group = MeshDescription->CreatePolygonGroup();
		const FName SlotName(*Section.Material);
		SlotNames.Set(Group, SlotName);

		FSkeletalMaterial Material;
		Material.MaterialInterface = MakeSectionMaterial(MaterialParent, MaterialPackagePath,
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

FString UElysiumSkeletalBuildLibrary::BuildAnimSequencesFromSource(const FString& SourcePath,
	const FString& PackagePath, const FString& SkeletonPackageName, int32& OutClipCount)
{
	OutClipCount = 0;
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

	for (const FElysiumSourceClip& Clip : Source.Clips)
	{
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

		IAnimationDataController& Controller = Sequence->GetController();
		Controller.OpenBracket(NSLOCTEXT("Elysium", "BakeClip", "Baking VtMB clip"),
			/*bShouldTransact=*/false);
		Controller.InitializeModel();
		Controller.SetFrameRate(FFrameRate(FMath::RoundToInt(FMath::Max(Clip.FrameRate, 1.0f)), 1),
			false);
		// A model's playable frame count is one less than its key count, and it must be at least
		// one -- so a single-frame pose becomes a two-key clip holding that pose.
		const int32 KeyCount = FMath::Max(Clip.FrameCount, 2);
		Controller.SetNumberOfFrames(FFrameNumber(KeyCount - 1), false);

		int32 BoundTracks = 0;
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
				continue;
			}

			// A channel the clip leaves alone holds the bind value from the file that AUTHORED the
			// clip, not from the shared skeleton. The two differ on exactly the bones a rig family
			// disagrees about -- the `[2]` fork and the appendix chains, whose generic names denote
			// different chains on different bodies -- and taking the skeleton's would hand every
			// member of a family whichever member happened to seed it.
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
			++BoundTracks;
		}

		// The `_delta` family. VtMB composes these on top of the bind pose rather than replacing
		// it, which is exactly Unreal's local-space additive against the reference pose.
		if ((Clip.Flags & 0x4) != 0)
		{
			Sequence->AdditiveAnimType = AAT_LocalSpaceBase;
			Sequence->RefPoseType = ABPT_RefPose;
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

		Sequence->PostEditChange();
		FAssetRegistryModule::AssetCreated(Sequence);
		if (!SavePackageTo(Package, PackageName))
		{
			return FString::Printf(TEXT("could not save %s"), *PackageName);
		}
		++OutClipCount;
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
