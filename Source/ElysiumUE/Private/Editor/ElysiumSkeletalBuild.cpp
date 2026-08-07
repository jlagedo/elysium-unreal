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
	if (ProfilesCreated > 0)
	{
		UPackage* SkeletonPackage = Skeleton->GetPackage();
		if (SkeletonPackage == nullptr || !SavePackageTo(SkeletonPackage, SkeletonPackage->GetName()))
		{
			return FString::Printf(TEXT("could not save %s after adding %d blend mask(s)"),
				*SkeletonPackageName, ProfilesCreated);
		}
		UE_LOG(LogElysiumSkeletalBuild, Display, TEXT("%s: %d blend mask(s) on %s"),
			*FPaths::GetBaseFilename(SourcePath), ProfilesCreated,
			*FPackageName::GetShortName(SkeletonPackageName));
	}

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

		// The `_delta` family. `STUDIO_DELTA` (0x4) marks a clip whose tracks state a DIFFERENCE
		// from the running pose rather than a pose, which is what Unreal calls a local-space
		// additive against the reference pose. Declared before the tracks are written because it
		// changes what is written -- see the composition below.
		const bool bAdditive = (Clip.Flags & 0x4) != 0;
		if (bAdditive)
		{
			Sequence->AdditiveAnimType = AAT_LocalSpaceBase;
			Sequence->RefPoseType = ABPT_RefPose;
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

			// A channel the clip leaves alone holds the bind value from the file that AUTHORED the
			// clip, not from the shared skeleton. The two differ on exactly the bones a rig family
			// disagrees about -- the `[2]` fork and the appendix chains, whose generic names denote
			// different chains on different bodies -- and taking the skeleton's would hand every
			// member of a family whichever member happened to seed it.
			//
			// An ADDITIVE clip inverts both halves. Its untouched channel is a zero delta rather
			// than a bind value, and its base is the SHARED SKELETON's reference pose rather than
			// the container's bind -- because the base is not a choice here, it is whatever Unreal
			// subtracts back out. `FCompressibleAnimData::BakeOutAdditiveIntoRawData` composes an
			// additive sequence down by `Target * Base^-1` against the skeleton's reference pose
			// before compressing, so the raw keys have to be the delta composed ONTO that pose or
			// the shipped asset carries `delta * refpose^-1` -- every layered bone rotated by its
			// own inverse bind, from a bake that logs nothing wrong. Composing here is what makes
			// that subtraction hand VtMB's delta straight back.
			const FTransform& Bind = Source.Bones[Track.Bone].Local;
			const FTransform& Base = RefSkeleton.GetRefBonePose()[SkeletonBone];
			TArray<FVector3f> Positions;
			TArray<FQuat4f> Rotations;
			TArray<FVector3f> Scales;
			Positions.Reserve(KeyCount);
			Rotations.Reserve(KeyCount);
			Scales.Init(FVector3f::OneVector, KeyCount);
			for (int32 Key = 0; Key < KeyCount; ++Key)
			{
				const int32 Frame = FMath::Min(Key, Clip.FrameCount - 1);
				const FVector3f Fallback = bAdditive
					? FVector3f::ZeroVector : FVector3f(Bind.GetTranslation());
				const FQuat4f FallbackRotation = bAdditive
					? FQuat4f::Identity : FQuat4f(Bind.GetRotation());
				FVector3f Position = Track.Translations.IsValidIndex(Frame)
					? Track.Translations[Frame] : Fallback;
				FQuat4f Rotation = Track.Rotations.IsValidIndex(Frame)
					? Track.Rotations[Frame] : FallbackRotation;
				if (bAdditive)
				{
					// `Target = Delta * Base`, matching the order `ConvertPoseToAdditive` undoes.
					Position += FVector3f(Base.GetTranslation());
					Rotation = (Rotation * FQuat4f(Base.GetRotation())).GetNormalized();
				}
				Positions.Add(Position);
				Rotations.Add(Rotation);
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
				if (Mask.Bones[Bone] == 0 || Tracked.Contains(Bone)
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
		if (!SavePackageTo(Package, PackageName))
		{
			return FString::Printf(TEXT("could not save %s"), *PackageName);
		}
		++OutClipCount;
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

	// Sorted so a re-bake writes the same assets in the same order; TMap iteration is not stable.
	TArray<FString> Labels;
	Table.Grids.GetKeys(Labels);
	Labels.Sort([](const FString& A, const FString& B) { return A < B; });

	for (const FString& Label : Labels)
	{
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
			const FString ClipAsset = TEXT("A_") + FElysiumContentPaths::BakedAssetName(Cell.Clip);
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

		const FString AssetName = TEXT("BS_") + FElysiumContentPaths::BakedAssetName(Label);
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
			// Left OFF even on `move_yaw`, which does wrap. VtMB authors the wrap by DUPLICATING the
			// clip at both ends of the fan -- cell 0 and cell 8 are the same animation -- so clamped
			// interpolation already reproduces retail across the seam. Turning wrapping on would make
			// -180 and +180 one point carrying two samples, and `IsTooCloseToExistingSamplePoint`
			// would reject the second. The caller wraps its input instead, which is what the pose
			// parameter's own `loop` is for.
			Parameter.bWrapInput = false;
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
		++OutSpaceCount;
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
