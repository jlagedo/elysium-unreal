// ELYSIUM PROBE — TEMPORARY — dancer-decode-probe2

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/Skeleton.h"
#include "Animation/SkeletonRemapping.h"
#include "Animation/SkeletonRemappingRegistry.h"
#include "AnimationRuntime.h"
#include "BoneContainer.h"
#include "BonePose.h"
#include "Components/PoseableMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "ReferenceSkeleton.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"

#include "ElysiumContentPaths.h"
#include "Visual/ElysiumSkeletalSource.h"

static constexpr EAutomationTestFlags GElysiumDancerDecodeProbe2Flags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace ElysiumProbe2
{
	const TCHAR* GJoyPath =
		TEXT("/ElysiumBaked/Characters/Anims/_banks/character_shared_female_stances/"
			"A_Stance_Joy_Idle_1.A_Stance_Joy_Idle_1");
	const TCHAR* GDancePath =
		TEXT("/ElysiumBaked/Characters/Anims/_banks/character_shared_female_misc/"
			"A_dance01.A_dance01");

	const TCHAR* GStancesStem = TEXT("character_shared_female_stances");
	const TCHAR* GMiscStem = TEXT("character_shared_female_misc");
	const TCHAR* GJoyClip = TEXT("Stance_Joy_Idle_1");
	const TCHAR* GDanceClip = TEXT("dance01");
	const TCHAR* GBankFamily = TEXT("character_npc_unique_hollywood_and_and");

	struct FSubject
	{
		const TCHAR* Label;
		const TCHAR* MeshPath;
		/** Owner-declared classification going in, so a regression is visible against it. */
		const TCHAR* Standing;
	};

	const FSubject GSubjects[] = {
		{ TEXT("female_dancer_2"),
			TEXT("/ElysiumBaked/Characters/Meshes/SK_female_dancer_2.SK_female_dancer_2"),
			TEXT("broken_today") },
		{ TEXT("goth_female"),
			TEXT("/ElysiumBaked/Characters/Meshes/SK_goth_female.SK_goth_female"),
			TEXT("broken_today") },
		{ TEXT("male_dancer_2"),
			TEXT("/ElysiumBaked/Characters/Meshes/SK_male_dancer_2.SK_male_dancer_2"),
			TEXT("correct_today") },
		{ TEXT("tremere_female_armor_0"),
			TEXT("/ElysiumBaked/Characters/Meshes/SK_tremere_female_armor_0.SK_tremere_female_armor_0"),
			TEXT("correct_today") },
	};

	const FName GFocusedBones[] = {
		TEXT("Bip01 L UpperArm"),
		TEXT("Bip01 L Forearm"),
		TEXT("Bip01 L Hand"),
		TEXT("Bip01 R UpperArm"),
		TEXT("Bip01 R Forearm"),
	};

	FString VectorText(const FVector& Value)
	{
		return FString::Printf(TEXT("(%.9f,%.9f,%.9f)"), Value.X, Value.Y, Value.Z);
	}

	FString Vector3fText(const FVector3f& Value)
	{
		return FString::Printf(TEXT("(%.9f,%.9f,%.9f)"), Value.X, Value.Y, Value.Z);
	}

	FString QuatText(const FQuat& Value)
	{
		return FString::Printf(TEXT("(%.9f,%.9f,%.9f,%.9f)"), Value.X, Value.Y, Value.Z, Value.W);
	}

	double QuatAngleDegrees(const FQuat& Value)
	{
		const double W = FMath::Clamp(FMath::Abs(Value.W), 0.0, 1.0);
		return FMath::RadiansToDegrees(2.0 * FMath::Acos(W));
	}

	// --- the reseeded skeleton ------------------------------------------------------------------

	/**
	 * Build an unsaved USkeleton whose bone tree and reference pose come from one `.eskm`.
	 *
	 * The same construction the bake performs for a family skeleton, with the family reduced to a
	 * single member: modifier -> MergeAllBonesToBoneTree -> OrientAndScale everywhere.
	 */
	USkeleton* BuildTransientSkeleton(const FElysiumSkeletalSource& Source, FString& OutError)
	{
		if (Source.Bones.IsEmpty())
		{
			OutError = TEXT("container carries no bones");
			return nullptr;
		}
		USkeleton* Skeleton = NewObject<USkeleton>(GetTransientPackage(), NAME_None, RF_Transient);
		{
			FReferenceSkeletonModifier Modifier(Skeleton);
			for (const FElysiumSourceBone& Bone : Source.Bones)
			{
				Modifier.Add(FMeshBoneInfo(Bone.Name, Bone.Name.ToString(), Bone.Parent), Bone.Local);
			}
		}
		USkeletalMesh* Carrier =
			NewObject<USkeletalMesh>(GetTransientPackage(), NAME_None, RF_Transient);
		Carrier->SetRefSkeleton(Skeleton->GetReferenceSkeleton());
		if (!Skeleton->MergeAllBonesToBoneTree(Carrier))
		{
			OutError = TEXT("the bone tree refused the reference skeleton just authored onto it");
			return nullptr;
		}
		Skeleton->SetBoneTranslationRetargetingMode(0,
			EBoneTranslationRetargetingMode::OrientAndScale, /*bChildrenToo=*/true);
		return Skeleton;
	}

	/** The bake's own RegisterRetargetSource, verbatim in shape. */
	FName RegisterRetargetSource(USkeleton* Skeleton, const FName Name,
		const FElysiumSkeletalSource& Source)
	{
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

	// --- the transient sequence -----------------------------------------------------------------

	constexpr float AnimatedTranslationCm = 0.1f;
	constexpr float AnimatedRotationDeg = 0.5f;

	/** The bake's own SilentAppendixBones, verbatim in shape. */
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

	/**
	 * Build an unsaved UAnimSequence carrying one container clip's tracks, on the named skeleton.
	 *
	 * The same emission the bake performs -- a channel the clip leaves alone takes the CONTAINER's
	 * bind, which is the very thing that makes an un-translated limb eligible for the remap.
	 */
	UAnimSequence* BuildTransientSequence(const FElysiumSkeletalSource& Source,
		const FString& ClipName, USkeleton* Skeleton, const FName RetargetSource,
		int32& OutTracks, int32& OutDropped, FString& OutError)
	{
		OutTracks = 0;
		OutDropped = 0;
		const FElysiumSourceClip* Found = nullptr;
		for (const FElysiumSourceClip& Clip : Source.Clips)
		{
			if (Clip.Name == ClipName && Clip.BaseName.IsEmpty())
			{
				Found = &Clip;
				break;
			}
		}
		if (Found == nullptr)
		{
			OutError = FString::Printf(TEXT("clip %s not in container"), *ClipName);
			return nullptr;
		}
		const FElysiumSourceClip& Clip = *Found;
		const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
		const TSet<int32> Silent = Source.Vertices.IsEmpty()
			? SilentAppendixBones(Source) : TSet<int32>();

		UAnimSequence* Sequence =
			NewObject<UAnimSequence>(GetTransientPackage(), NAME_None, RF_Transient);
		Sequence->SetSkeleton(Skeleton);
		Sequence->RetargetSource = RetargetSource;

		IAnimationDataController& Controller = Sequence->GetController();
		Controller.OpenBracket(NSLOCTEXT("ElysiumProbe2", "Probe", "Probe clip"),
			/*bShouldTransact=*/false);
		Controller.InitializeModel();
		const double AuthoredRate =
			FMath::Max(static_cast<double>(Clip.FrameRate), UE_KINDA_SMALL_NUMBER);
		Controller.SetFrameRate(FFrameRate(FMath::RoundToInt(AuthoredRate * 1000.0), 1000), false);
		const int32 KeyCount = FMath::Max(Clip.FrameCount, 2);
		Controller.SetNumberOfFrames(FFrameNumber(KeyCount - 1), false);

		for (const FElysiumSourceTrack& Track : Clip.Tracks)
		{
			if (!Source.Bones.IsValidIndex(Track.Bone))
			{
				continue;
			}
			const FName BoneName = Source.Bones[Track.Bone].Name;
			if (RefSkeleton.FindBoneIndex(BoneName) == INDEX_NONE || Silent.Contains(Track.Bone))
			{
				++OutDropped;
				continue;
			}
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
			++OutTracks;
		}

		Controller.NotifyPopulated();
		Controller.CloseBracket(false);
		if (OutTracks == 0)
		{
			OutError = TEXT("no track bound");
			return nullptr;
		}
		Sequence->PostEditChange();
		Sequence->WaitOnExistingCompression();
		return Sequence;
	}

	// --- evaluation -----------------------------------------------------------------------------

	struct FBoneRow
	{
		FName Bone;
		int32 MeshIndex = INDEX_NONE;
		int32 SourceSkeletonIndex = INDEX_NONE;
		int32 TargetSkeletonIndex = INDEX_NONE;
		int32 CompactPoseIndex = INDEX_NONE;
		int32 OrientAndScaleIndex = INDEX_NONE;
		EBoneTranslationRetargetingMode::Type RetargetMode =
			EBoneTranslationRetargetingMode::Skeleton;
		FQuat Q0 = FQuat::Identity;
		double Q0AngleDeg = 0.0;
		FVector Authored = FVector::ZeroVector;
		FVector TargetBind = FVector::ZeroVector;
		FVector Remapped = FVector::ZeroVector;
		FVector OrientAndScaleOutput = FVector::ZeroVector;
		FVector Final = FVector::ZeroVector;
		bool bOrientAndScaleEntry = false;
		double FinalVersusBind = 0.0;
	};

	struct FEvalResult
	{
		bool bValid = false;
		bool bRemapValid = false;
		bool bCompressedValid = false;
		bool bRequiresRefPoseRetarget = false;
		bool bUseSourceRetargetModes = false;
		double MaxFinalVersusBind = 0.0;
		TArray<FBoneRow> Bones;
		/** Indexed by mesh bone; the mesh's own reference pose wherever the pose carried nothing. */
		TArray<FTransform> MeshLocals;
	};

	bool EvaluateOn(FAutomationTestBase& Test, USkeletalMesh* Mesh, UAnimSequence* Sequence,
		FEvalResult& Out)
	{
		if (Mesh == nullptr || Sequence == nullptr)
		{
			return false;
		}
		USkeleton* SourceSkeleton = Sequence->GetSkeleton();
		USkeleton* TargetSkeleton = Mesh->GetSkeleton();
		if (SourceSkeleton == nullptr || TargetSkeleton == nullptr)
		{
			return false;
		}
		Sequence->WaitOnExistingCompression();
		Out.bCompressedValid = Sequence->IsCompressedDataValid();

		const FReferenceSkeleton& MeshRef = Mesh->GetRefSkeleton();
		TArray<FBoneIndexType> RequiredBones;
		RequiredBones.SetNumUninitialized(MeshRef.GetNum());
		for (int32 BoneIndex = 0; BoneIndex < MeshRef.GetNum(); ++BoneIndex)
		{
			RequiredBones[BoneIndex] = static_cast<FBoneIndexType>(BoneIndex);
		}

		FMemMark Mark(FMemStack::Get());
		FBoneContainer Container;
		Container.InitializeTo(
			RequiredBones,
			UE::Anim::FCurveFilterSettings(UE::Anim::ECurveFilterMode::None),
			*Mesh);

		const FSkeletonRemapping& Remapping =
			UE::Anim::FSkeletonRemappingRegistry::Get().GetRemapping(SourceSkeleton, TargetSkeleton);
		Out.bRemapValid = Remapping.IsValid();
		Out.bRequiresRefPoseRetarget = Remapping.RequiresReferencePoseRetarget();
		const TArray<FTransform>& AuthoredRef =
			SourceSkeleton->GetRefLocalPoses(Sequence->RetargetSource);
		const FRetargetSourceCachedData& OrientAndScaleCache =
			Container.GetRetargetSourceCachedData(Sequence->RetargetSource, Remapping, AuthoredRef);

		FCompactPose Pose;
		Pose.SetBoneContainer(&Container);
		FBlendedCurve Curve;
		Curve.InitFrom(Container);
		UE::Anim::FStackAttributeContainer Attributes;
		FAnimationPoseData PoseData(Pose, Curve, Attributes);
		Sequence->GetAnimationPose(PoseData, FAnimExtractContext(0.0));

		Out.bUseSourceRetargetModes = TargetSkeleton->GetUseRetargetModesFromCompatibleSkeleton();

		Out.MeshLocals = MeshRef.GetRefBonePose();
		for (int32 MeshIndex = 0; MeshIndex < MeshRef.GetNum(); ++MeshIndex)
		{
			const FSkeletonPoseBoneIndex SkeletonPoseIndex =
				Container.GetSkeletonPoseIndexFromMeshPoseIndex(FMeshPoseBoneIndex(MeshIndex));
			const FCompactPoseBoneIndex CompactIndex =
				Container.GetCompactPoseIndexFromSkeletonPoseIndex(SkeletonPoseIndex);
			if (Pose.IsValidIndex(CompactIndex))
			{
				Out.MeshLocals[MeshIndex] = Pose[CompactIndex];
			}
		}

		for (const FName BoneName : GFocusedBones)
		{
			FBoneRow& Bone = Out.Bones.AddDefaulted_GetRef();
			Bone.Bone = BoneName;
			Bone.MeshIndex = MeshRef.FindBoneIndex(BoneName);
			if (Bone.MeshIndex == INDEX_NONE)
			{
				continue;
			}
			const FSkeletonPoseBoneIndex TargetPoseIndex =
				Container.GetSkeletonPoseIndexFromMeshPoseIndex(FMeshPoseBoneIndex(Bone.MeshIndex));
			Bone.TargetSkeletonIndex = TargetPoseIndex.GetInt();
			Bone.CompactPoseIndex =
				Container.GetCompactPoseIndexFromSkeletonPoseIndex(TargetPoseIndex).GetInt();
			Bone.SourceSkeletonIndex = Remapping.GetSourceSkeletonBoneIndex(Bone.TargetSkeletonIndex);
			if (!AuthoredRef.IsValidIndex(Bone.SourceSkeletonIndex)
				|| !Pose.IsValidIndex(FCompactPoseBoneIndex(Bone.CompactPoseIndex)))
			{
				continue;
			}
			Bone.RetargetMode = FAnimationRuntime::GetBoneTranslationRetargetingMode(
				Out.bUseSourceRetargetModes,
				Bone.SourceSkeletonIndex,
				Bone.TargetSkeletonIndex,
				SourceSkeleton,
				TargetSkeleton,
				/*bDisableRetargeting=*/false);
			Bone.Authored = AuthoredRef[Bone.SourceSkeletonIndex].GetTranslation();
			Bone.TargetBind = MeshRef.GetRefBonePose()[Bone.MeshIndex].GetTranslation();
			if (Out.bRequiresRefPoseRetarget)
			{
				Bone.Q0 = Remapping.GetRetargetingQuaternions(Bone.TargetSkeletonIndex).Get<0>();
				Bone.Q0AngleDeg = QuatAngleDegrees(Bone.Q0);
				Bone.Remapped = Remapping.RetargetBoneTranslationToTargetSkeleton(
					Bone.TargetSkeletonIndex, Bone.Authored);
			}
			else
			{
				Bone.Remapped = Bone.Authored;
			}
			Bone.Final = Pose[FCompactPoseBoneIndex(Bone.CompactPoseIndex)].GetTranslation();
			Bone.FinalVersusBind = FVector::Distance(Bone.Final, Bone.TargetBind);
			Out.MaxFinalVersusBind = FMath::Max(Out.MaxFinalVersusBind, Bone.FinalVersusBind);
			Bone.OrientAndScaleOutput = Bone.Remapped;
			if (OrientAndScaleCache.CompactPoseIndexToOrientAndScaleIndex.IsValidIndex(
				Bone.CompactPoseIndex))
			{
				Bone.OrientAndScaleIndex =
					OrientAndScaleCache.CompactPoseIndexToOrientAndScaleIndex[Bone.CompactPoseIndex];
			}
			if (OrientAndScaleCache.OrientAndScaleData.IsValidIndex(Bone.OrientAndScaleIndex))
			{
				Bone.bOrientAndScaleEntry = true;
				const FOrientAndScaleRetargetingCachedData& Cached =
					OrientAndScaleCache.OrientAndScaleData[Bone.OrientAndScaleIndex];
				Bone.OrientAndScaleOutput =
					(Bone.Remapped - Cached.SourceTranslation).IsNearlyZero(
						BONE_TRANS_RT_ORIENT_AND_SCALE_PRECISION)
					? Cached.TargetTranslation
					: Cached.TranslationDeltaOrient.RotateVector(Bone.Remapped)
						* Cached.TranslationScale;
			}
		}
		Out.bValid = true;
		return true;
	}

	void LogEval(FAutomationTestBase& Test, const TCHAR* Stage, const TCHAR* SourceLabel,
		const TCHAR* MeshLabel, const FEvalResult& Result)
	{
		Test.AddInfo(FString::Printf(
			TEXT("PROBE2|stage=%s|source=%s|mesh=%s|remap_valid=%d|requires_refpose_retarget=%d|")
			TEXT("compressed_valid=%d|use_source_retarget_modes=%d|max_final_vs_bind=%.9f"),
			Stage, SourceLabel, MeshLabel, Result.bRemapValid ? 1 : 0,
			Result.bRequiresRefPoseRetarget ? 1 : 0, Result.bCompressedValid ? 1 : 0,
			Result.bUseSourceRetargetModes ? 1 : 0, Result.MaxFinalVersusBind));
		for (const FBoneRow& Bone : Result.Bones)
		{
			Test.AddInfo(FString::Printf(
				TEXT("PROBE2|stage=%s|source=%s|mesh=%s|bone=%s|mode=%d|q0=%s|q0_deg=%.9f|")
				TEXT("authored=%s|target_bind=%s|remap_out=%s|oas_index=%d|oas_entry=%d|")
				TEXT("oas_out=%s|final=%s|final_vs_bind=%.9f"),
				Stage, SourceLabel, MeshLabel, *Bone.Bone.ToString(),
				static_cast<int32>(Bone.RetargetMode), *QuatText(Bone.Q0), Bone.Q0AngleDeg,
				*VectorText(Bone.Authored), *VectorText(Bone.TargetBind),
				*VectorText(Bone.Remapped), Bone.OrientAndScaleIndex,
				Bone.bOrientAndScaleEntry ? 1 : 0, *VectorText(Bone.OrientAndScaleOutput),
				*VectorText(Bone.Final), Bone.FinalVersusBind));
		}
	}

	// --- deformation ----------------------------------------------------------------------------

	struct FSkinMetrics
	{
		bool bValid = false;
		bool bExactValid = false;
		float ExactRefDistance = 0.f;
		float ExactSkinDistance = 0.f;
		float ExactRatio = 0.f;
		float MaxRatio = 0.f;
		float P99Ratio = 0.f;
		int32 Above2x = 0;
		int32 Above5x = 0;
		int32 Edges = 0;
	};

	FSkinMetrics SkinAndMeasure(FAutomationTestBase& Test, UWorld* World, USkeletalMesh* Mesh,
		const TArray<FTransform>& MeshLocals, bool bExactPair)
	{
		FSkinMetrics Metrics;
		FSkeletalMeshRenderData* RenderData = Mesh->GetResourceForRendering();
		FSkeletalMeshModel* ImportedModel = Mesh->GetImportedModel();
		if (RenderData == nullptr || RenderData->LODRenderData.IsEmpty()
			|| ImportedModel == nullptr || ImportedModel->LODModels.IsEmpty())
		{
			return Metrics;
		}
		FSkeletalMeshLODRenderData& LODRenderData = RenderData->LODRenderData[0];
		FSkinWeightVertexBuffer* SkinWeights = LODRenderData.GetSkinWeightVertexBuffer();
		const FSkeletalMeshLODModel& LODModel = ImportedModel->LODModels[0];
		if (SkinWeights == nullptr)
		{
			return Metrics;
		}

		UPoseableMeshComponent* Component =
			NewObject<UPoseableMeshComponent>(GetTransientPackage());
		Component->SetSkinnedAssetAndUpdate(Mesh);
		Component->RegisterComponentWithWorld(World);
		for (int32 BoneIndex = 0; BoneIndex < MeshLocals.Num(); ++BoneIndex)
		{
			if (Component->BoneSpaceTransforms.IsValidIndex(BoneIndex))
			{
				Component->BoneSpaceTransforms[BoneIndex] = MeshLocals[BoneIndex];
			}
		}
		Component->RefreshBoneTransforms();

		TArray<FMatrix44f> RefToLocals;
		Component->GetCurrentRefToLocalMatrices(RefToLocals, 0);
		TArray<FVector3f> Skinned;
		USkinnedMeshComponent::ComputeSkinnedPositions(
			Component, Skinned, RefToLocals, LODRenderData, *SkinWeights);

		const FPositionVertexBuffer& Positions =
			LODRenderData.StaticVertexBuffers.PositionVertexBuffer;
		const TArray<uint32>& Indices = LODModel.IndexBuffer;
		TArray<float> Ratios;
		Ratios.Reserve(Indices.Num());
		for (int32 Triangle = 0; Triangle + 2 < Indices.Num(); Triangle += 3)
		{
			for (int32 Edge = 0; Edge < 3; ++Edge)
			{
				const uint32 A = Indices[Triangle + Edge];
				const uint32 B = Indices[Triangle + (Edge + 1) % 3];
				if (!Skinned.IsValidIndex(static_cast<int32>(A))
					|| !Skinned.IsValidIndex(static_cast<int32>(B)))
				{
					continue;
				}
				const float RefDistance =
					FVector3f::Distance(Positions.VertexPosition(A), Positions.VertexPosition(B));
				if (RefDistance <= 0.05f)
				{
					continue;
				}
				const float Ratio = FVector3f::Distance(Skinned[A], Skinned[B]) / RefDistance;
				Ratios.Add(Ratio);
				Metrics.MaxRatio = FMath::Max(Metrics.MaxRatio, Ratio);
				Metrics.Above2x += Ratio > 2.f ? 1 : 0;
				Metrics.Above5x += Ratio > 5.f ? 1 : 0;
			}
		}
		Ratios.Sort();
		Metrics.Edges = Ratios.Num();
		if (!Ratios.IsEmpty())
		{
			Metrics.P99Ratio = Ratios[FMath::Clamp(
				FMath::FloorToInt(0.99f * Ratios.Num()), 0, Ratios.Num() - 1)];
		}

		if (bExactPair
			&& LODModel.MeshToImportVertexMap.Num() == static_cast<int32>(LODRenderData.GetNumVertices()))
		{
			int32 Render444 = INDEX_NONE;
			int32 Render445 = INDEX_NONE;
			for (int32 Vertex = 0; Vertex < LODModel.MeshToImportVertexMap.Num(); ++Vertex)
			{
				if (Render444 == INDEX_NONE && LODModel.MeshToImportVertexMap[Vertex] == 444)
				{
					Render444 = Vertex;
				}
				else if (Render445 == INDEX_NONE && LODModel.MeshToImportVertexMap[Vertex] == 445)
				{
					Render445 = Vertex;
				}
			}
			if (Skinned.IsValidIndex(Render444) && Skinned.IsValidIndex(Render445))
			{
				Metrics.bExactValid = true;
				Metrics.ExactRefDistance = FVector3f::Distance(
					Positions.VertexPosition(Render444), Positions.VertexPosition(Render445));
				Metrics.ExactSkinDistance =
					FVector3f::Distance(Skinned[Render444], Skinned[Render445]);
				Metrics.ExactRatio = Metrics.ExactRefDistance > UE_SMALL_NUMBER
					? Metrics.ExactSkinDistance / Metrics.ExactRefDistance : 0.f;
			}
		}

		Component->UnregisterComponent();
		Metrics.bValid = true;
		return Metrics;
	}

	void LogMetrics(FAutomationTestBase& Test, const TCHAR* Stage, const TCHAR* SourceLabel,
		const TCHAR* MeshLabel, const FSkinMetrics& Metrics)
	{
		Test.AddInfo(FString::Printf(
			TEXT("PROBE2|stage=%s|source=%s|mesh=%s|valid=%d|edges=%d|max_ratio=%.9f|")
			TEXT("p99_ratio=%.9f|above_2x=%d|above_5x=%d|exact_valid=%d|exact_ref=%.9f|")
			TEXT("exact_skin=%.9f|exact_ratio=%.9f|cpu_final=PROBED|gpu=NOT_PROBED"),
			Stage, SourceLabel, MeshLabel, Metrics.bValid ? 1 : 0, Metrics.Edges,
			Metrics.MaxRatio, Metrics.P99Ratio, Metrics.Above2x, Metrics.Above5x,
			Metrics.bExactValid ? 1 : 0, Metrics.ExactRefDistance, Metrics.ExactSkinDistance,
			Metrics.ExactRatio));
	}

	// --- bind grouping --------------------------------------------------------------------------

	bool BindsAgree(const FElysiumSkeletalSource& A, const FElysiumSkeletalSource& B,
		int32& OutShared)
	{
		TMap<FName, const FElysiumSourceBone*> ByName;
		ByName.Reserve(A.Bones.Num());
		for (const FElysiumSourceBone& Bone : A.Bones)
		{
			ByName.Add(Bone.Name, &Bone);
		}
		OutShared = 0;
		for (const FElysiumSourceBone& Bone : B.Bones)
		{
			const FElysiumSourceBone* const* Other = ByName.Find(Bone.Name);
			if (Other == nullptr)
			{
				continue;
			}
			++OutShared;
			const FVector Delta =
				(*Other)->Local.GetTranslation() - Bone.Local.GetTranslation();
			if (Delta.GetAbsMax() > 0.001)
			{
				return false;
			}
			if ((*Other)->Local.GetRotation().AngularDistance(Bone.Local.GetRotation()) > 0.001)
			{
				return false;
			}
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FElysiumDancerDecodeProbe2Test,
	"Elysium.Content.DancerDecodeProbe2",
	GElysiumDancerDecodeProbe2Flags)

bool FElysiumDancerDecodeProbe2Test::RunTest(const FString&)
{
	using namespace ElysiumProbe2;

	if (!FElysiumContentPaths::IsConfigured())
	{
		AddWarning(TEXT("PROBE2|stage=SKIP|reason=export_root_not_configured"));
		return true;
	}

	// --- assets ---------------------------------------------------------------------------------
	UAnimSequence* RealJoy = LoadObject<UAnimSequence>(nullptr, GJoyPath);
	UAnimSequence* RealDance = LoadObject<UAnimSequence>(nullptr, GDancePath);
	if (RealJoy == nullptr)
	{
		AddError(TEXT("PROBE2|stage=LOAD|error=joy_sequence"));
		return false;
	}
	RealJoy->AddToRoot();
	if (RealDance != nullptr)
	{
		RealDance->AddToRoot();
	}
	USkeleton* BankSkeleton = RealJoy->GetSkeleton();

	TArray<USkeletalMesh*> Meshes;
	for (const FSubject& Subject : GSubjects)
	{
		USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, Subject.MeshPath);
		if (Mesh == nullptr)
		{
			AddError(FString::Printf(TEXT("PROBE2|stage=LOAD|error=mesh|label=%s"), Subject.Label));
			return false;
		}
		Mesh->AddToRoot();
		Meshes.Add(Mesh);
	}

	// --- step 0: baseline guard -----------------------------------------------------------------
	AddInfo(FString::Printf(
		TEXT("PROBE2|stage=GUARD_SKELETON|which=bank|path=%s|bones=%d|")
		TEXT("use_retarget_modes_from_compatible=%d"),
		*BankSkeleton->GetPathName(), BankSkeleton->GetReferenceSkeleton().GetRawBoneNum(),
		BankSkeleton->GetUseRetargetModesFromCompatibleSkeleton() ? 1 : 0));
	for (const FName BoneName : GFocusedBones)
	{
		const int32 Index = BankSkeleton->GetReferenceSkeleton().FindBoneIndex(BoneName);
		AddInfo(FString::Printf(
			TEXT("PROBE2|stage=GUARD_MODE|which=bank|bone=%s|index=%d|mode=%d"),
			*BoneName.ToString(), Index,
			Index == INDEX_NONE ? -1
				: static_cast<int32>(BankSkeleton->GetBoneTranslationRetargetingMode(Index))));
	}
	for (int32 SubjectIndex = 0; SubjectIndex < Meshes.Num(); ++SubjectIndex)
	{
		USkeleton* TargetSkeleton = Meshes[SubjectIndex]->GetSkeleton();
		AddInfo(FString::Printf(
			TEXT("PROBE2|stage=GUARD_SKELETON|which=%s|standing=%s|path=%s|bones=%d|")
			TEXT("use_retarget_modes_from_compatible=%d"),
			GSubjects[SubjectIndex].Label, GSubjects[SubjectIndex].Standing,
			TargetSkeleton != nullptr ? *TargetSkeleton->GetPathName() : TEXT("NONE"),
			TargetSkeleton != nullptr ? TargetSkeleton->GetReferenceSkeleton().GetRawBoneNum() : -1,
			TargetSkeleton != nullptr
				&& TargetSkeleton->GetUseRetargetModesFromCompatibleSkeleton() ? 1 : 0));
		if (TargetSkeleton == nullptr)
		{
			continue;
		}
		for (const FName BoneName : GFocusedBones)
		{
			const int32 Index = TargetSkeleton->GetReferenceSkeleton().FindBoneIndex(BoneName);
			AddInfo(FString::Printf(
				TEXT("PROBE2|stage=GUARD_MODE|which=%s|bone=%s|index=%d|mode=%d"),
				GSubjects[SubjectIndex].Label, *BoneName.ToString(), Index,
				Index == INDEX_NONE ? -1
					: static_cast<int32>(TargetSkeleton->GetBoneTranslationRetargetingMode(Index))));
		}
	}

	UWorld* ProbeWorld = UWorld::CreateWorld(EWorldType::Game, false);
	if (ProbeWorld == nullptr)
	{
		AddError(TEXT("PROBE2|stage=WORLD|error=create_failed"));
		return false;
	}

	// Probe 1's fixed baseline, through the same evaluation and the poseable skinning route this
	// probe uses everywhere else -- so the route itself is checked against a known number.
	FEvalResult BaselineFemale;
	EvaluateOn(*this, Meshes[0], RealJoy, BaselineFemale);
	LogEval(*this, TEXT("GUARD_EVAL"), TEXT("baked_bank_skeleton"), TEXT("female_dancer_2"),
		BaselineFemale);
	const FSkinMetrics BaselineMetrics =
		SkinAndMeasure(*this, ProbeWorld, Meshes[0], BaselineFemale.MeshLocals, true);
	LogMetrics(*this, TEXT("GUARD_SEAM"), TEXT("baked_bank_skeleton"), TEXT("female_dancer_2"),
		BaselineMetrics);

	const FVector ExpectedUpperArm(10.592618944, 1.791687106, -0.436005119);
	bool bBaselineClean = false;
	if (!BaselineFemale.Bones.IsEmpty())
	{
		const double Delta = FVector::Distance(BaselineFemale.Bones[0].Final, ExpectedUpperArm);
		const float SeamDelta = FMath::Abs(BaselineMetrics.ExactRatio - 8.267468452f);
		bBaselineClean = Delta <= 0.001 && SeamDelta <= 0.01f;
		AddInfo(FString::Printf(
			TEXT("PROBE2|stage=GUARD_VERDICT|expected_upperarm=%s|observed_upperarm=%s|")
			TEXT("delta=%.9f|expected_seam=8.267468452|observed_seam=%.9f|seam_delta=%.9f|clean=%d"),
			*VectorText(ExpectedUpperArm), *VectorText(BaselineFemale.Bones[0].Final), Delta,
			BaselineMetrics.ExactRatio, SeamDelta, bBaselineClean ? 1 : 0));
	}

	// --- Part A: the reseeded skeleton ----------------------------------------------------------
	FElysiumSkeletalSource Stances;
	FString Error;
	if (!FElysiumSkeletalSource::Load(FElysiumContentPaths::NpcBankSource(GStancesStem),
		Stances, Error))
	{
		AddError(FString::Printf(TEXT("PROBE2|stage=A|error=%s"), *Error));
		ProbeWorld->DestroyWorld(false);
		return false;
	}
	USkeleton* Reseeded = BuildTransientSkeleton(Stances, Error);
	if (Reseeded == nullptr)
	{
		AddError(FString::Printf(TEXT("PROBE2|stage=A|error=%s"), *Error));
		ProbeWorld->DestroyWorld(false);
		return false;
	}
	Reseeded->AddToRoot();
	const FName StancesSource(GStancesStem);
	RegisterRetargetSource(Reseeded, StancesSource, Stances);
	AddInfo(FString::Printf(
		TEXT("PROBE2|stage=A|reseeded_bones=%d|bank_bones=%d|retarget_source=%s|clips=%d"),
		Reseeded->GetReferenceSkeleton().GetRawBoneNum(),
		BankSkeleton->GetReferenceSkeleton().GetRawBoneNum(), *StancesSource.ToString(),
		Stances.Clips.Num()));

	{
		// The reseeded reference pose against the pose the sequences already name, bone by bone.
		const TArray<FTransform>& BankAuthored = BankSkeleton->GetRefLocalPoses(StancesSource);
		const FReferenceSkeleton& ReseedRef = Reseeded->GetReferenceSkeleton();
		const FReferenceSkeleton& BankRef = BankSkeleton->GetReferenceSkeleton();
		TArray<FTransform> ReseedComponent;
		FAnimationRuntime::FillUpComponentSpaceTransforms(
			ReseedRef, ReseedRef.GetRefBonePose(), ReseedComponent);
		TArray<FTransform> BankComponent;
		FAnimationRuntime::FillUpComponentSpaceTransforms(
			BankRef, BankRef.GetRefBonePose(), BankComponent);
		double MaxDelta = 0.0;
		for (const FName BoneName : GFocusedBones)
		{
			const int32 ReseedIndex = ReseedRef.FindBoneIndex(BoneName);
			const int32 BankIndex = BankRef.FindBoneIndex(BoneName);
			if (ReseedIndex == INDEX_NONE || BankIndex == INDEX_NONE
				|| !BankAuthored.IsValidIndex(BankIndex))
			{
				continue;
			}
			const FVector ReseedBind = ReseedRef.GetRefBonePose()[ReseedIndex].GetTranslation();
			const FVector AuthoredBind = BankAuthored[BankIndex].GetTranslation();
			const double Delta = FVector::Distance(ReseedBind, AuthoredBind);
			MaxDelta = FMath::Max(MaxDelta, Delta);
			const int32 ReseedParent = ReseedRef.GetParentIndex(ReseedIndex);
			const int32 BankParent = BankRef.GetParentIndex(BankIndex);
			AddInfo(FString::Printf(
				TEXT("PROBE2|stage=A_BIND|bone=%s|reseeded_ref_translation=%s|")
				TEXT("bank_authored_translation=%s|delta=%.9f|reseeded_parent_component_rot=%s|")
				TEXT("bank_ref_parent_component_rot=%s"),
				*BoneName.ToString(), *VectorText(ReseedBind), *VectorText(AuthoredBind), Delta,
				ReseedComponent.IsValidIndex(ReseedParent)
					? *QuatText(ReseedComponent[ReseedParent].GetRotation()) : TEXT("NONE"),
				BankComponent.IsValidIndex(BankParent)
					? *QuatText(BankComponent[BankParent].GetRotation()) : TEXT("NONE")));
		}
		AddInfo(FString::Printf(
			TEXT("PROBE2|stage=A_SUMMARY|max_reseed_vs_authored_bind_delta=%.9f"), MaxDelta));
	}

	// --- Part B / C: sequences ------------------------------------------------------------------
	int32 BankTracks = 0;
	int32 BankDropped = 0;
	UAnimSequence* BankTransientJoy = BuildTransientSequence(
		Stances, GJoyClip, BankSkeleton, StancesSource, BankTracks, BankDropped, Error);
	if (BankTransientJoy != nullptr)
	{
		BankTransientJoy->AddToRoot();
	}
	else
	{
		AddWarning(FString::Printf(TEXT("PROBE2|stage=C|bank_transient=NOT_BUILT|error=%s"), *Error));
	}

	int32 ReseedTracks = 0;
	int32 ReseedDropped = 0;
	UAnimSequence* ReseedJoy = BuildTransientSequence(
		Stances, GJoyClip, Reseeded, StancesSource, ReseedTracks, ReseedDropped, Error);
	if (ReseedJoy == nullptr)
	{
		AddError(FString::Printf(TEXT("PROBE2|stage=C|error=%s"), *Error));
		ProbeWorld->DestroyWorld(false);
		return false;
	}
	ReseedJoy->AddToRoot();
	AddInfo(FString::Printf(
		TEXT("PROBE2|stage=C_BUILD|bank_transient_tracks=%d|bank_transient_dropped=%d|")
		TEXT("reseed_tracks=%d|reseed_dropped=%d|reseed_compressed_valid=%d"),
		BankTracks, BankDropped, ReseedTracks, ReseedDropped,
		ReseedJoy->IsCompressedDataValid() ? 1 : 0));

	// The transient builder checked against the baked asset it imitates, on the subject.
	if (BankTransientJoy != nullptr)
	{
		FEvalResult BuilderCheck;
		EvaluateOn(*this, Meshes[0], BankTransientJoy, BuilderCheck);
		LogEval(*this, TEXT("C_BUILDER_CHECK"), TEXT("transient_on_bank_skeleton"),
			TEXT("female_dancer_2"), BuilderCheck);
		double MaxDelta = 0.0;
		for (int32 Index = 0; Index < BuilderCheck.Bones.Num()
			&& Index < BaselineFemale.Bones.Num(); ++Index)
		{
			MaxDelta = FMath::Max(MaxDelta,
				FVector::Distance(BuilderCheck.Bones[Index].Final, BaselineFemale.Bones[Index].Final));
		}
		AddInfo(FString::Printf(
			TEXT("PROBE2|stage=C_BUILDER_CHECK|max_transient_vs_baked_final_delta=%.9f"), MaxDelta));
	}

	// Part B and Part C, per body, under the reseed and against the baked baseline.
	double WorstFinalVersusBind[UE_ARRAY_COUNT(GSubjects)] = {};
	float ReseedExactRatio = 0.f;
	float ReseedMaxRatio[UE_ARRAY_COUNT(GSubjects)] = {};
	float BaselineMaxRatio[UE_ARRAY_COUNT(GSubjects)] = {};
	for (int32 SubjectIndex = 0; SubjectIndex < Meshes.Num(); ++SubjectIndex)
	{
		const FSubject& Subject = GSubjects[SubjectIndex];

		FEvalResult Baked;
		EvaluateOn(*this, Meshes[SubjectIndex], RealJoy, Baked);
		LogEval(*this, TEXT("B_BASELINE"), TEXT("baked_bank_skeleton"), Subject.Label, Baked);
		const FSkinMetrics BakedMetrics = SkinAndMeasure(
			*this, ProbeWorld, Meshes[SubjectIndex], Baked.MeshLocals, SubjectIndex == 0);
		LogMetrics(*this, TEXT("C_BASELINE"), TEXT("baked_bank_skeleton"), Subject.Label,
			BakedMetrics);
		BaselineMaxRatio[SubjectIndex] = BakedMetrics.MaxRatio;

		FEvalResult Reseed;
		EvaluateOn(*this, Meshes[SubjectIndex], ReseedJoy, Reseed);
		LogEval(*this, TEXT("B_RESEED"), TEXT("reseeded_transient_skeleton"), Subject.Label, Reseed);
		const FSkinMetrics ReseedMetrics = SkinAndMeasure(
			*this, ProbeWorld, Meshes[SubjectIndex], Reseed.MeshLocals, SubjectIndex == 0);
		LogMetrics(*this, TEXT("C_RESEED"), TEXT("reseeded_transient_skeleton"), Subject.Label,
			ReseedMetrics);
		WorstFinalVersusBind[SubjectIndex] = Reseed.MaxFinalVersusBind;
		ReseedMaxRatio[SubjectIndex] = ReseedMetrics.MaxRatio;
		if (SubjectIndex == 0)
		{
			ReseedExactRatio = ReseedMetrics.ExactRatio;
		}
	}

	// The counterfactual floor probe 1 measured, re-measured through this probe's own route.
	{
		TArray<FTransform> Counterfactual = BaselineFemale.MeshLocals;
		const FReferenceSkeleton& MeshRef = Meshes[0]->GetRefSkeleton();
		for (const FName BoneName : {
			FName(TEXT("Bip01 L UpperArm")),
			FName(TEXT("Bip01 L Forearm")),
			FName(TEXT("Bip01 R UpperArm")),
			FName(TEXT("Bip01 R Forearm")) })
		{
			const int32 Index = MeshRef.FindBoneIndex(BoneName);
			if (Counterfactual.IsValidIndex(Index))
			{
				Counterfactual[Index].SetTranslation(
					MeshRef.GetRefBonePose()[Index].GetTranslation());
			}
		}
		const FSkinMetrics FloorMetrics =
			SkinAndMeasure(*this, ProbeWorld, Meshes[0], Counterfactual, true);
		LogMetrics(*this, TEXT("C_FLOOR"), TEXT("bind_translation_counterfactual"),
			TEXT("female_dancer_2"), FloorMetrics);
	}

	// A_dance01 on the subject, if the second container is affordable.
	{
		FElysiumSkeletalSource Misc;
		FString MiscError;
		if (FElysiumSkeletalSource::Load(FElysiumContentPaths::NpcBankSource(GMiscStem),
			Misc, MiscError))
		{
			USkeleton* ReseededMisc = BuildTransientSkeleton(Misc, MiscError);
			if (ReseededMisc != nullptr)
			{
				ReseededMisc->AddToRoot();
				const FName MiscSource(GMiscStem);
				RegisterRetargetSource(ReseededMisc, MiscSource, Misc);
				int32 Tracks = 0;
				int32 Dropped = 0;
				UAnimSequence* ReseedDance = BuildTransientSequence(
					Misc, GDanceClip, ReseededMisc, MiscSource, Tracks, Dropped, MiscError);
				if (ReseedDance != nullptr)
				{
					ReseedDance->AddToRoot();
					for (int32 SubjectIndex = 0; SubjectIndex < Meshes.Num(); ++SubjectIndex)
					{
						if (RealDance != nullptr)
						{
							FEvalResult Baked;
							EvaluateOn(*this, Meshes[SubjectIndex], RealDance, Baked);
							LogEval(*this, TEXT("B_BASELINE_DANCE"), TEXT("baked_bank_skeleton"),
								GSubjects[SubjectIndex].Label, Baked);
							const FSkinMetrics Metrics = SkinAndMeasure(*this, ProbeWorld,
								Meshes[SubjectIndex], Baked.MeshLocals, SubjectIndex == 0);
							LogMetrics(*this, TEXT("C_BASELINE_DANCE"), TEXT("baked_bank_skeleton"),
								GSubjects[SubjectIndex].Label, Metrics);
						}
						FEvalResult Reseed;
						EvaluateOn(*this, Meshes[SubjectIndex], ReseedDance, Reseed);
						LogEval(*this, TEXT("B_RESEED_DANCE"), TEXT("reseeded_transient_skeleton"),
							GSubjects[SubjectIndex].Label, Reseed);
						const FSkinMetrics Metrics = SkinAndMeasure(*this, ProbeWorld,
							Meshes[SubjectIndex], Reseed.MeshLocals, SubjectIndex == 0);
						LogMetrics(*this, TEXT("C_RESEED_DANCE"),
							TEXT("reseeded_transient_skeleton"),
							GSubjects[SubjectIndex].Label, Metrics);
					}
					ReseedDance->RemoveFromRoot();
				}
				else
				{
					AddWarning(FString::Printf(
						TEXT("PROBE2|stage=DANCE|status=NOT_PROBED|error=%s"), *MiscError));
				}
				ReseededMisc->RemoveFromRoot();
			}
		}
		else
		{
			AddWarning(FString::Printf(
				TEXT("PROBE2|stage=DANCE|status=NOT_PROBED|error=%s"), *MiscError));
		}
	}

	// --- stage E/F: where the residual Q0 comes from --------------------------------------------
	// The reseed did not make Q0 identity on the bodies whose bind translations DO equal the
	// authored ones, so the disagreement is in the rotations. Two things are asked here: which
	// pair of poses actually disagrees, and what a source skeleton seeded from the BODY -- the
	// construction that forces PS == PT -- produces.
	for (int32 SubjectIndex = 0; SubjectIndex < Meshes.Num(); ++SubjectIndex)
	{
		USkeletalMesh* Mesh = Meshes[SubjectIndex];
		const FSubject& Subject = GSubjects[SubjectIndex];
		const FReferenceSkeleton& MeshRef = Mesh->GetRefSkeleton();
		const FReferenceSkeleton& SkeletonRef = Mesh->GetSkeleton()->GetReferenceSkeleton();
		const FReferenceSkeleton& StancesRef = Reseeded->GetReferenceSkeleton();

		TArray<FTransform> MeshComponent;
		FAnimationRuntime::FillUpComponentSpaceTransforms(
			MeshRef, MeshRef.GetRefBonePose(), MeshComponent);
		TArray<FTransform> SkeletonComponent;
		FAnimationRuntime::FillUpComponentSpaceTransforms(
			SkeletonRef, SkeletonRef.GetRefBonePose(), SkeletonComponent);
		TArray<FTransform> StancesComponent;
		FAnimationRuntime::FillUpComponentSpaceTransforms(
			StancesRef, StancesRef.GetRefBonePose(), StancesComponent);

		for (const FName BoneName : GFocusedBones)
		{
			const int32 MeshIndex = MeshRef.FindBoneIndex(BoneName);
			const int32 SkeletonIndex = SkeletonRef.FindBoneIndex(BoneName);
			const int32 StancesIndex = StancesRef.FindBoneIndex(BoneName);
			if (MeshIndex == INDEX_NONE || SkeletonIndex == INDEX_NONE || StancesIndex == INDEX_NONE)
			{
				continue;
			}
			const int32 MeshParent = MeshRef.GetParentIndex(MeshIndex);
			const int32 SkeletonParent = SkeletonRef.GetParentIndex(SkeletonIndex);
			const int32 StancesParent = StancesRef.GetParentIndex(StancesIndex);
			const FQuat MeshLocal = MeshRef.GetRefBonePose()[MeshIndex].GetRotation();
			const FQuat SkeletonLocal = SkeletonRef.GetRefBonePose()[SkeletonIndex].GetRotation();
			const FQuat StancesLocal = StancesRef.GetRefBonePose()[StancesIndex].GetRotation();
			const FQuat MeshParentComponent = MeshComponent.IsValidIndex(MeshParent)
				? MeshComponent[MeshParent].GetRotation() : FQuat::Identity;
			const FQuat SkeletonParentComponent = SkeletonComponent.IsValidIndex(SkeletonParent)
				? SkeletonComponent[SkeletonParent].GetRotation() : FQuat::Identity;
			const FQuat StancesParentComponent = StancesComponent.IsValidIndex(StancesParent)
				? StancesComponent[StancesParent].GetRotation() : FQuat::Identity;
			AddInfo(FString::Printf(
				TEXT("PROBE2|stage=F_ROT|mesh=%s|bone=%s|")
				TEXT("local_mesh_vs_skeleton_deg=%.9f|local_mesh_vs_stances_deg=%.9f|")
				TEXT("parent_component_mesh_vs_skeleton_deg=%.9f|")
				TEXT("parent_component_mesh_vs_stances_deg=%.9f|")
				TEXT("parent_component_skeleton_vs_stances_deg=%.9f"),
				Subject.Label, *BoneName.ToString(),
				FMath::RadiansToDegrees(MeshLocal.AngularDistance(SkeletonLocal)),
				FMath::RadiansToDegrees(MeshLocal.AngularDistance(StancesLocal)),
				FMath::RadiansToDegrees(MeshParentComponent.AngularDistance(SkeletonParentComponent)),
				FMath::RadiansToDegrees(MeshParentComponent.AngularDistance(StancesParentComponent)),
				FMath::RadiansToDegrees(
					SkeletonParentComponent.AngularDistance(StancesParentComponent))));
		}

		// A source skeleton seeded from the BODY itself, carrying the stances bind as its named
		// retarget source. The construction that makes PS == PT if -- and only if -- the body's
		// family skeleton reference pose is the body's own.
		USkeleton* PerBody = NewObject<USkeleton>(GetTransientPackage(), NAME_None, RF_Transient);
		{
			FReferenceSkeletonModifier Modifier(PerBody);
			for (int32 BoneIndex = 0; BoneIndex < MeshRef.GetRawBoneNum(); ++BoneIndex)
			{
				const FName BoneName = MeshRef.GetBoneName(BoneIndex);
				Modifier.Add(
					FMeshBoneInfo(BoneName, BoneName.ToString(), MeshRef.GetParentIndex(BoneIndex)),
					MeshRef.GetRefBonePose()[BoneIndex]);
			}
		}
		USkeletalMesh* Carrier =
			NewObject<USkeletalMesh>(GetTransientPackage(), NAME_None, RF_Transient);
		Carrier->SetRefSkeleton(PerBody->GetReferenceSkeleton());
		if (!PerBody->MergeAllBonesToBoneTree(Carrier))
		{
			AddWarning(FString::Printf(
				TEXT("PROBE2|stage=E|mesh=%s|status=NOT_PROBED|reason=bone_tree_refused"),
				Subject.Label));
			continue;
		}
		PerBody->SetBoneTranslationRetargetingMode(0,
			EBoneTranslationRetargetingMode::OrientAndScale, /*bChildrenToo=*/true);
		PerBody->AddToRoot();
		RegisterRetargetSource(PerBody, StancesSource, Stances);

		int32 Tracks = 0;
		int32 Dropped = 0;
		FString PerBodyError;
		UAnimSequence* PerBodyJoy = BuildTransientSequence(
			Stances, GJoyClip, PerBody, StancesSource, Tracks, Dropped, PerBodyError);
		if (PerBodyJoy == nullptr)
		{
			AddWarning(FString::Printf(TEXT("PROBE2|stage=E|mesh=%s|status=NOT_PROBED|error=%s"),
				Subject.Label, *PerBodyError));
			PerBody->RemoveFromRoot();
			continue;
		}
		PerBodyJoy->AddToRoot();
		AddInfo(FString::Printf(
			TEXT("PROBE2|stage=E_BUILD|mesh=%s|per_body_bones=%d|tracks=%d|dropped=%d"),
			Subject.Label, PerBody->GetReferenceSkeleton().GetRawBoneNum(), Tracks, Dropped));
		FEvalResult PerBodyResult;
		EvaluateOn(*this, Mesh, PerBodyJoy, PerBodyResult);
		LogEval(*this, TEXT("E_PERBODY"), TEXT("per_body_transient_skeleton"), Subject.Label,
			PerBodyResult);
		const FSkinMetrics PerBodyMetrics = SkinAndMeasure(
			*this, ProbeWorld, Mesh, PerBodyResult.MeshLocals, SubjectIndex == 0);
		LogMetrics(*this, TEXT("E_PERBODY"), TEXT("per_body_transient_skeleton"), Subject.Label,
			PerBodyMetrics);
		PerBodyJoy->RemoveFromRoot();
		PerBody->RemoveFromRoot();
	}

	// --- the cost question: how many distinct binds does the family carry -----------------------
	{
		TArray<FString> Members;
		FString Json;
		const FString FamiliesPath = FElysiumContentPaths::NpcDir() / TEXT("families.json");
		if (FFileHelper::LoadFileToString(Json, *FamiliesPath))
		{
			TSharedPtr<FJsonObject> Root;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
			if (FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid())
			{
				const TSharedPtr<FJsonObject>* Banks = nullptr;
				if (Root->TryGetObjectField(TEXT("banks"), Banks) && Banks != nullptr)
				{
					const TSharedPtr<FJsonObject>* Family = nullptr;
					if ((*Banks)->TryGetObjectField(GBankFamily, Family) && Family != nullptr)
					{
						const TArray<TSharedPtr<FJsonValue>>* MemberValues = nullptr;
						if ((*Family)->TryGetArrayField(TEXT("members"), MemberValues))
						{
							for (const TSharedPtr<FJsonValue>& Value : *MemberValues)
							{
								Members.Add(Value->AsString());
							}
						}
					}
				}
			}
		}
		AddInfo(FString::Printf(TEXT("PROBE2|stage=D|family=%s|members=%d|families_json=%s"),
			GBankFamily, Members.Num(), *FamiliesPath));

		TArray<FElysiumSkeletalSource> Representatives;
		TArray<TArray<FString>> Groups;
		int32 Loaded = 0;
		for (const FString& Member : Members)
		{
			FElysiumSkeletalSource Bones;
			FString MemberError;
			if (!FElysiumSkeletalSource::LoadBones(
				FElysiumContentPaths::NpcBankSource(Member), Bones, MemberError))
			{
				AddWarning(FString::Printf(
					TEXT("PROBE2|stage=D|member=%s|error=%s"), *Member, *MemberError));
				continue;
			}
			++Loaded;
			bool bPlaced = false;
			for (int32 Group = 0; Group < Representatives.Num(); ++Group)
			{
				int32 Shared = 0;
				if (BindsAgree(Representatives[Group], Bones, Shared))
				{
					Groups[Group].Add(Member);
					bPlaced = true;
					break;
				}
			}
			if (!bPlaced)
			{
				Representatives.Add(MoveTemp(Bones));
				Groups.AddDefaulted_GetRef().Add(Member);
			}
		}
		AddInfo(FString::Printf(
			TEXT("PROBE2|stage=D_SUMMARY|members_loaded=%d|distinct_bind_groups=%d"),
			Loaded, Groups.Num()));
		for (int32 Group = 0; Group < Groups.Num(); ++Group)
		{
			AddInfo(FString::Printf(
				TEXT("PROBE2|stage=D_GROUP|group=%d|size=%d|bones=%d|members=%s"),
				Group, Groups[Group].Num(), Representatives[Group].Bones.Num(),
				*FString::Join(Groups[Group], TEXT(","))));
		}
	}

	// --- verdict inputs -------------------------------------------------------------------------
	AddInfo(FString::Printf(
		TEXT("PROBE2|stage=VERDICT_INPUTS|baseline_clean=%d|baseline_exact_seam=%.9f|")
		TEXT("reseed_exact_seam=%.9f|floor=1.081779122|")
		TEXT("female_dancer_2_max_final_vs_bind=%.9f|goth_female_max_final_vs_bind=%.9f|")
		TEXT("male_dancer_2_max_final_vs_bind=%.9f|tremere_female_armor_0_max_final_vs_bind=%.9f|")
		TEXT("reseed_max_edge=(%.6f,%.6f,%.6f,%.6f)|baseline_max_edge=(%.6f,%.6f,%.6f,%.6f)|")
		TEXT("gpu=NOT_PROBED"),
		bBaselineClean ? 1 : 0, BaselineMetrics.ExactRatio, ReseedExactRatio,
		WorstFinalVersusBind[0], WorstFinalVersusBind[1], WorstFinalVersusBind[2],
		WorstFinalVersusBind[3],
		ReseedMaxRatio[0], ReseedMaxRatio[1], ReseedMaxRatio[2], ReseedMaxRatio[3],
		BaselineMaxRatio[0], BaselineMaxRatio[1], BaselineMaxRatio[2], BaselineMaxRatio[3]));

	ReseedJoy->RemoveFromRoot();
	if (BankTransientJoy != nullptr)
	{
		BankTransientJoy->RemoveFromRoot();
	}
	Reseeded->RemoveFromRoot();
	RealJoy->RemoveFromRoot();
	if (RealDance != nullptr)
	{
		RealDance->RemoveFromRoot();
	}
	for (USkeletalMesh* Mesh : Meshes)
	{
		Mesh->RemoveFromRoot();
	}
	ProbeWorld->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
