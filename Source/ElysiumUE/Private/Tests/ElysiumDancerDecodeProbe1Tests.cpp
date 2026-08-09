// ELYSIUM PROBE — TEMPORARY — dancer-decode

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimationPoseData.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Animation/SkeletonRemapping.h"
#include "Animation/SkeletonRemappingRegistry.h"
#include "AnimationRuntime.h"
#include "BoneContainer.h"
#include "BonePose.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshRenderData.h"

static constexpr EAutomationTestFlags GElysiumDancerDecodeProbeFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	const TCHAR* GJoyPath =
		TEXT("/ElysiumBaked/Characters/Anims/_banks/character_shared_female_stances/"
			"A_Stance_Joy_Idle_1.A_Stance_Joy_Idle_1");
	const TCHAR* GFemalePath =
		TEXT("/ElysiumBaked/Characters/Meshes/SK_female_dancer_2.SK_female_dancer_2");
	const TCHAR* GMalePath =
		TEXT("/ElysiumBaked/Characters/Meshes/SK_male_dancer_2.SK_male_dancer_2");

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

	FString MatrixText(const FMatrix44f& Value)
	{
		return FString::Printf(
			TEXT("[(%.9f,%.9f,%.9f,%.9f),(%.9f,%.9f,%.9f,%.9f),")
			TEXT("(%.9f,%.9f,%.9f,%.9f),(%.9f,%.9f,%.9f,%.9f)]"),
			Value.M[0][0], Value.M[0][1], Value.M[0][2], Value.M[0][3],
			Value.M[1][0], Value.M[1][1], Value.M[1][2], Value.M[1][3],
			Value.M[2][0], Value.M[2][1], Value.M[2][2], Value.M[2][3],
			Value.M[3][0], Value.M[3][1], Value.M[3][2], Value.M[3][3]);
	}

	FString IndexListText(const TArray<int32>& Values)
	{
		FString Result;
		for (int32 Index = 0; Index < Values.Num(); ++Index)
		{
			if (Index > 0)
			{
				Result += TEXT(",");
			}
			Result += FString::FromInt(Values[Index]);
		}
		return Result;
	}

	struct FBoneProbeResult
	{
		FName Bone;
		int32 MeshIndex = INDEX_NONE;
		int32 SourceSkeletonIndex = INDEX_NONE;
		int32 TargetSkeletonIndex = INDEX_NONE;
		int32 CompactPoseIndex = INDEX_NONE;
		int32 OrientAndScaleIndex = INDEX_NONE;
		EBoneTranslationRetargetingMode::Type RetargetMode =
			EBoneTranslationRetargetingMode::Skeleton;
		FVector AuthoredTranslation = FVector::ZeroVector;
		FVector TargetBindTranslation = FVector::ZeroVector;
		FVector RemappedTranslation = FVector::ZeroVector;
		FVector OrientAndScaleOutput = FVector::ZeroVector;
		FVector FinalTranslation = FVector::ZeroVector;
		bool bOrientAndScaleEntry = false;
	};

	struct FMeshProbeResult
	{
		FString Label;
		USkeletalMesh* Mesh = nullptr;
		TArray<FBoneProbeResult> Bones;
		bool bRemapValid = false;
		bool bCompressedDataValid = false;
		bool bAllRemapEqualsFinal = true;
		bool bAllOrientAndScaleAbsentOrNoOp = true;
		float MaxAuthoredToRemappedDelta = 0.f;
		float MaxRemappedToFinalDelta = 0.f;
	};

	bool EvaluateMesh(
		FAutomationTestBase& Test,
		const TCHAR* Label,
		USkeletalMesh* Mesh,
		UAnimSequence* Sequence,
		FMeshProbeResult& Out)
	{
		Out.Label = Label;
		Out.Mesh = Mesh;
		if (Mesh == nullptr || Sequence == nullptr)
		{
			Test.AddError(FString::Printf(TEXT("PROBE1|stage=B0|mesh=%s|error=asset_load"), Label));
			return false;
		}

		USkeleton* SourceSkeleton = Sequence->GetSkeleton();
		USkeleton* TargetSkeleton = Mesh->GetSkeleton();
		if (SourceSkeleton == nullptr || TargetSkeleton == nullptr)
		{
			Test.AddError(FString::Printf(TEXT("PROBE1|stage=B0|mesh=%s|error=skeleton_load"), Label));
			return false;
		}

#if WITH_EDITOR
		Sequence->WaitOnExistingCompression();
#endif
		Out.bCompressedDataValid = Sequence->IsCompressedDataValid();

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
		const TArray<FTransform>& AuthoredRef = SourceSkeleton->GetRefLocalPoses(Sequence->RetargetSource);
		const FRetargetSourceCachedData& OrientAndScaleCache =
			Container.GetRetargetSourceCachedData(Sequence->RetargetSource, Remapping, AuthoredRef);

		FCompactPose Pose;
		Pose.SetBoneContainer(&Container);
		FBlendedCurve Curve;
		Curve.InitFrom(Container);
		UE::Anim::FStackAttributeContainer Attributes;
		FAnimationPoseData PoseData(Pose, Curve, Attributes);
		Sequence->GetAnimationPose(PoseData, FAnimExtractContext(0.0));

		Test.AddInfo(FString::Printf(
			TEXT("PROBE1|stage=B1|mesh=%s|sequence=%s|source_skeleton=%s|target_skeleton=%s|")
			TEXT("container_skeleton=%s|retarget_source=%s|remap_valid=%d|compressed_valid=%d"),
			Label,
			*Sequence->GetPathName(),
			*SourceSkeleton->GetPathName(),
			*TargetSkeleton->GetPathName(),
			*Container.GetSkeletonAsset()->GetPathName(),
			*Sequence->RetargetSource.ToString(),
			Out.bRemapValid ? 1 : 0,
			Out.bCompressedDataValid ? 1 : 0));

		if (!Out.bRemapValid)
		{
			Test.AddError(FString::Printf(TEXT("PROBE1|stage=B1|mesh=%s|error=remap_invalid"), Label));
			return false;
		}

		const bool bUseSourceRetargetModes =
			TargetSkeleton->GetUseRetargetModesFromCompatibleSkeleton();
		for (const FName BoneName : GFocusedBones)
		{
			FBoneProbeResult& Bone = Out.Bones.AddDefaulted_GetRef();
			Bone.Bone = BoneName;
			Bone.MeshIndex = MeshRef.FindBoneIndex(BoneName);
			if (Bone.MeshIndex == INDEX_NONE)
			{
				Test.AddError(FString::Printf(
					TEXT("PROBE1|stage=B2|mesh=%s|bone=%s|error=mesh_bone_absent"),
					Label, *BoneName.ToString()));
				continue;
			}

			const FSkeletonPoseBoneIndex TargetSkeletonPoseIndex =
				Container.GetSkeletonPoseIndexFromMeshPoseIndex(FMeshPoseBoneIndex(Bone.MeshIndex));
			Bone.TargetSkeletonIndex = TargetSkeletonPoseIndex.GetInt();
			Bone.CompactPoseIndex =
				Container.GetCompactPoseIndexFromSkeletonPoseIndex(TargetSkeletonPoseIndex).GetInt();
			Bone.SourceSkeletonIndex =
				Remapping.GetSourceSkeletonBoneIndex(Bone.TargetSkeletonIndex);
			if (!AuthoredRef.IsValidIndex(Bone.SourceSkeletonIndex)
				|| !Pose.IsValidIndex(FCompactPoseBoneIndex(Bone.CompactPoseIndex)))
			{
				Test.AddError(FString::Printf(
					TEXT("PROBE1|stage=B2|mesh=%s|bone=%s|error=index_map|")
					TEXT("mesh_index=%d|source_index=%d|target_index=%d|compact_index=%d"),
					Label, *BoneName.ToString(), Bone.MeshIndex, Bone.SourceSkeletonIndex,
					Bone.TargetSkeletonIndex, Bone.CompactPoseIndex));
				continue;
			}

			Bone.RetargetMode = FAnimationRuntime::GetBoneTranslationRetargetingMode(
				bUseSourceRetargetModes,
				Bone.SourceSkeletonIndex,
				Bone.TargetSkeletonIndex,
				SourceSkeleton,
				TargetSkeleton,
				/*bDisableRetargeting=*/false);
			Bone.AuthoredTranslation = AuthoredRef[Bone.SourceSkeletonIndex].GetTranslation();
			Bone.TargetBindTranslation = MeshRef.GetRefBonePose()[Bone.MeshIndex].GetTranslation();
			Bone.RemappedTranslation = Remapping.RetargetBoneTranslationToTargetSkeleton(
				Bone.TargetSkeletonIndex, Bone.AuthoredTranslation);
			Bone.FinalTranslation = Pose[FCompactPoseBoneIndex(Bone.CompactPoseIndex)].GetTranslation();
			Bone.OrientAndScaleOutput = Bone.RemappedTranslation;

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
					(Bone.RemappedTranslation - Cached.SourceTranslation).IsNearlyZero(
						BONE_TRANS_RT_ORIENT_AND_SCALE_PRECISION)
					? Cached.TargetTranslation
					: Cached.TranslationDeltaOrient.RotateVector(Bone.RemappedTranslation)
						* Cached.TranslationScale;
			}

			const float AuthoredToTargetBind =
				FVector::Distance(Bone.AuthoredTranslation, Bone.TargetBindTranslation);
			const float AuthoredToRemapped =
				FVector::Distance(Bone.AuthoredTranslation, Bone.RemappedTranslation);
			const float RemappedToFinal =
				FVector::Distance(Bone.RemappedTranslation, Bone.FinalTranslation);
			const float OrientAndScaleToInput =
				FVector::Distance(Bone.OrientAndScaleOutput, Bone.RemappedTranslation);
			Out.MaxAuthoredToRemappedDelta =
				FMath::Max(Out.MaxAuthoredToRemappedDelta, AuthoredToRemapped);
			Out.MaxRemappedToFinalDelta =
				FMath::Max(Out.MaxRemappedToFinalDelta, RemappedToFinal);
			Out.bAllRemapEqualsFinal &= RemappedToFinal <= 0.001f;
			Out.bAllOrientAndScaleAbsentOrNoOp &=
				!Bone.bOrientAndScaleEntry || OrientAndScaleToInput <= 0.001f;

			Test.AddInfo(FString::Printf(
				TEXT("PROBE1|stage=B2_B3_B4|mesh=%s|bone=%s|mesh_index=%d|source_index=%d|")
				TEXT("target_index=%d|compact_index=%d|retarget_mode=%d|authored=%s|")
				TEXT("target_bind=%s|authored_target_delta=%.9f|remapped=%s|authored_remap_delta=%.9f|")
				TEXT("oas_index=%d|oas_entry=%d|oas_output=%s|oas_input_delta=%.9f|")
				TEXT("final=%s|remap_final_delta=%.9f"),
				Label, *BoneName.ToString(), Bone.MeshIndex, Bone.SourceSkeletonIndex,
				Bone.TargetSkeletonIndex, Bone.CompactPoseIndex, static_cast<int32>(Bone.RetargetMode),
				*VectorText(Bone.AuthoredTranslation), *VectorText(Bone.TargetBindTranslation),
				AuthoredToTargetBind, *VectorText(Bone.RemappedTranslation), AuthoredToRemapped,
				Bone.OrientAndScaleIndex, Bone.bOrientAndScaleEntry ? 1 : 0,
				*VectorText(Bone.OrientAndScaleOutput), OrientAndScaleToInput,
				*VectorText(Bone.FinalTranslation), RemappedToFinal));
		}

		Test.AddInfo(FString::Printf(
			TEXT("PROBE1|stage=B_SUMMARY|mesh=%s|all_remap_equals_final=%d|")
			TEXT("all_oas_absent_or_noop=%d|max_authored_remap_delta=%.9f|")
			TEXT("max_remap_final_delta=%.9f"),
			Label, Out.bAllRemapEqualsFinal ? 1 : 0,
			Out.bAllOrientAndScaleAbsentOrNoOp ? 1 : 0,
			Out.MaxAuthoredToRemappedDelta, Out.MaxRemappedToFinalDelta));
		return true;
	}

	void ProbeEngineSkinning(
		FAutomationTestBase& Test,
		USkeletalMesh* Mesh,
		UAnimSequence* Sequence,
		const FMeshProbeResult& PoseProbe)
	{
		UWorld* ProbeWorld = UWorld::CreateWorld(EWorldType::Game, false);
		if (ProbeWorld == nullptr)
		{
			Test.AddWarning(TEXT("PROBE1|stage=C|cpu_final=NOT_PROBED|gpu=NOT_PROBED|reason=world_create_failed"));
			return;
		}
		USkeletalMeshComponent* Component = NewObject<USkeletalMeshComponent>(GetTransientPackage());
		Component->SetSkeletalMeshAsset(Mesh);
		Component->RegisterComponentWithWorld(ProbeWorld);
		Component->PlayAnimation(Sequence, /*bLooping=*/false);
		Component->SetPosition(0.f, /*bFireNotifies=*/false);
		Component->TickAnimation(0.f, /*bNeedsValidRootMotion=*/false);
		Component->RefreshBoneTransforms();
		auto Cleanup = [&]()
		{
			Component->UnregisterComponent();
			ProbeWorld->DestroyWorld(false);
		};

		const TArray<FTransform> ComponentLocals = Component->GetBoneSpaceTransforms();
		for (const FBoneProbeResult& Bone : PoseProbe.Bones)
		{
			if (ComponentLocals.IsValidIndex(Bone.MeshIndex))
			{
				const FVector ComponentLocal = ComponentLocals[Bone.MeshIndex].GetTranslation();
				Test.AddInfo(FString::Printf(
					TEXT("PROBE1|stage=C_LOCAL|bone=%s|component_local=%s|pose_local=%s|")
					TEXT("component_pose_delta=%.9f"),
					*Bone.Bone.ToString(), *VectorText(ComponentLocal),
					*VectorText(Bone.FinalTranslation),
					FVector::Distance(ComponentLocal, Bone.FinalTranslation)));
			}
		}

		FSkeletalMeshRenderData* RenderData = Mesh->GetResourceForRendering();
		FSkeletalMeshModel* ImportedModel = Mesh->GetImportedModel();
		if (RenderData == nullptr || RenderData->LODRenderData.IsEmpty()
			|| ImportedModel == nullptr || ImportedModel->LODModels.IsEmpty())
		{
			Test.AddWarning(TEXT("PROBE1|stage=C|cpu_final=NOT_PROBED|gpu=NOT_PROBED|reason=lod_data_absent"));
			Cleanup();
			return;
		}

		FSkeletalMeshLODRenderData& LODRenderData = RenderData->LODRenderData[0];
		FSkinWeightVertexBuffer* SkinWeights = LODRenderData.GetSkinWeightVertexBuffer();
		const FSkeletalMeshLODModel& LODModel = ImportedModel->LODModels[0];
		if (SkinWeights == nullptr
			|| LODModel.MeshToImportVertexMap.Num() != static_cast<int32>(LODRenderData.GetNumVertices()))
		{
			Test.AddWarning(FString::Printf(
				TEXT("PROBE1|stage=C|cpu_final=NOT_PROBED|gpu=NOT_PROBED|reason=vertex_map_mismatch|")
				TEXT("map_count=%d|render_count=%u"),
				LODModel.MeshToImportVertexMap.Num(), LODRenderData.GetNumVertices()));
			Cleanup();
			return;
		}

		TArray<FMatrix44f> RefToLocals;
		Component->GetCurrentRefToLocalMatrices(RefToLocals, 0);
		TArray<FVector3f> SkinnedPositions;
		USkeletalMeshComponent::ComputeSkinnedPositions(
			Component, SkinnedPositions, RefToLocals, LODRenderData, *SkinWeights);

		TArray<int32> Render444;
		TArray<int32> Render445;
		for (int32 RenderVertex = 0; RenderVertex < LODModel.MeshToImportVertexMap.Num(); ++RenderVertex)
		{
			if (LODModel.MeshToImportVertexMap[RenderVertex] == 444)
			{
				Render444.Add(RenderVertex);
			}
			else if (LODModel.MeshToImportVertexMap[RenderVertex] == 445)
			{
				Render445.Add(RenderVertex);
			}
		}

		Test.AddInfo(FString::Printf(
			TEXT("PROBE1|stage=C_MAP|logical_444_render=%s|logical_445_render=%s|")
			TEXT("render_vertices=%u|ref_to_local_count=%d|cpu_final=PROBED|gpu=NOT_PROBED"),
			*IndexListText(Render444), *IndexListText(Render445),
			LODRenderData.GetNumVertices(), RefToLocals.Num()));

		if (Render444.IsEmpty() || Render445.IsEmpty()
			|| !SkinnedPositions.IsValidIndex(Render444[0])
			|| !SkinnedPositions.IsValidIndex(Render445[0]))
		{
			Test.AddWarning(TEXT("PROBE1|stage=C_SEAM|cpu_final=NOT_PROBED|gpu=NOT_PROBED|reason=logical_vertex_absent"));
			Cleanup();
			return;
		}

		const int32 Index444 = Render444[0];
		const int32 Index445 = Render445[0];
		const FVector3f Ref444 =
			LODRenderData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(Index444);
		const FVector3f Ref445 =
			LODRenderData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(Index445);
		const FVector3f Skin444 = SkinnedPositions[Index444];
		const FVector3f Skin445 = SkinnedPositions[Index445];
		const float RefDistance = FVector3f::Distance(Ref444, Ref445);
		const float SkinnedDistance = FVector3f::Distance(Skin444, Skin445);
		const float Ratio = RefDistance > UE_SMALL_NUMBER ? SkinnedDistance / RefDistance : 0.f;
		Test.AddInfo(FString::Printf(
			TEXT("PROBE1|stage=C_SEAM|render_444=%d|render_445=%d|ref_444=%s|ref_445=%s|")
			TEXT("skin_444=%s|skin_445=%s|ref_distance=%.9f|skin_distance=%.9f|ratio=%.9f|")
			TEXT("cpu_final=PROBED|gpu=NOT_PROBED"),
			Index444, Index445, *Vector3fText(Ref444), *Vector3fText(Ref445),
			*Vector3fText(Skin444), *Vector3fText(Skin445), RefDistance,
			SkinnedDistance, Ratio));

		UPoseableMeshComponent* Counterfactual =
			NewObject<UPoseableMeshComponent>(GetTransientPackage());
		Counterfactual->SetSkinnedAssetAndUpdate(Mesh);
		Counterfactual->RegisterComponentWithWorld(ProbeWorld);
		Counterfactual->CopyPoseFromSkeletalComponent(Component);
		for (const FName BoneName : {
			FName(TEXT("Bip01 L UpperArm")),
			FName(TEXT("Bip01 L Forearm")),
			FName(TEXT("Bip01 R UpperArm")),
			FName(TEXT("Bip01 R Forearm"))})
		{
			const int32 BoneIndex = Mesh->GetRefSkeleton().FindBoneIndex(BoneName);
			if (Counterfactual->BoneSpaceTransforms.IsValidIndex(BoneIndex))
			{
				Counterfactual->BoneSpaceTransforms[BoneIndex].SetTranslation(
					Mesh->GetRefSkeleton().GetRefBonePose()[BoneIndex].GetTranslation());
			}
		}
		Counterfactual->RefreshBoneTransforms();
		TArray<FMatrix44f> CounterfactualRefToLocals;
		Counterfactual->GetCurrentRefToLocalMatrices(CounterfactualRefToLocals, 0);
		TArray<FVector3f> CounterfactualPositions;
		USkinnedMeshComponent::ComputeSkinnedPositions(
			Counterfactual,
			CounterfactualPositions,
			CounterfactualRefToLocals,
			LODRenderData,
			*SkinWeights);
		if (CounterfactualPositions.IsValidIndex(Index444)
			&& CounterfactualPositions.IsValidIndex(Index445))
		{
			const float CounterfactualDistance = FVector3f::Distance(
				CounterfactualPositions[Index444], CounterfactualPositions[Index445]);
			const float CounterfactualRatio = RefDistance > UE_SMALL_NUMBER
				? CounterfactualDistance / RefDistance
				: 0.f;
			Test.AddInfo(FString::Printf(
				TEXT("PROBE1|stage=C_COUNTERFACTUAL|replacement=target_bind_translation_LR_upperarm_forearm|")
				TEXT("skin_444=%s|skin_445=%s|skin_distance=%.9f|ratio=%.9f|")
				TEXT("actual_ratio=%.9f|cpu_final=PROBED|gpu=NOT_PROBED"),
				*Vector3fText(CounterfactualPositions[Index444]),
				*Vector3fText(CounterfactualPositions[Index445]),
				CounterfactualDistance, CounterfactualRatio, Ratio));
		}
		else
		{
			Test.AddWarning(TEXT("PROBE1|stage=C_COUNTERFACTUAL|cpu_final=NOT_PROBED|gpu=NOT_PROBED|reason=position_absent"));
		}
		Counterfactual->UnregisterComponent();

		for (const FBoneProbeResult& Bone : PoseProbe.Bones)
		{
			if (RefToLocals.IsValidIndex(Bone.MeshIndex))
			{
				Test.AddInfo(FString::Printf(
					TEXT("PROBE1|stage=C_MATRIX|bone=%s|mesh_index=%d|ref_to_local=%s|")
					TEXT("cpu_final=PROBED|gpu=NOT_PROBED"),
					*Bone.Bone.ToString(), Bone.MeshIndex,
					*MatrixText(RefToLocals[Bone.MeshIndex])));
			}
		}
		Cleanup();
	}

	void ProbeSecondarySkeleton(
		FAutomationTestBase& Test,
		const TCHAR* Label,
		const TCHAR* MeshPath)
	{
		USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, MeshPath);
		Test.AddInfo(FString::Printf(
			TEXT("PROBE1|stage=SECONDARY|label=%s|mesh=%s|skeleton=%s"),
			Label,
			Mesh != nullptr ? *Mesh->GetPathName() : TEXT("LOAD_FAILED"),
			Mesh != nullptr && Mesh->GetSkeleton() != nullptr
				? *Mesh->GetSkeleton()->GetPathName()
				: TEXT("LOAD_FAILED")));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FElysiumDancerDecodeProbe1Test,
	"Elysium.Content.DancerDecodeProbe1",
	GElysiumDancerDecodeProbeFlags)

bool FElysiumDancerDecodeProbe1Test::RunTest(const FString&)
{
	UAnimSequence* Sequence = LoadObject<UAnimSequence>(nullptr, GJoyPath);
	USkeletalMesh* Female = LoadObject<USkeletalMesh>(nullptr, GFemalePath);
	USkeletalMesh* Male = LoadObject<USkeletalMesh>(nullptr, GMalePath);
	if (Sequence == nullptr || Female == nullptr || Male == nullptr)
	{
		AddError(FString::Printf(
			TEXT("PROBE1|stage=LOAD|sequence=%s|female=%s|male=%s"),
			Sequence != nullptr ? TEXT("OK") : TEXT("FAILED"),
			Female != nullptr ? TEXT("OK") : TEXT("FAILED"),
			Male != nullptr ? TEXT("OK") : TEXT("FAILED")));
		return false;
	}

	Sequence->AddToRoot();
	Female->AddToRoot();
	Male->AddToRoot();

	FMeshProbeResult FemaleProbe;
	FMeshProbeResult MaleProbe;
	const bool bFemaleRan = EvaluateMesh(*this, TEXT("female_dancer_2"), Female, Sequence, FemaleProbe);
	const bool bMaleRan = EvaluateMesh(*this, TEXT("male_dancer_2"), Male, Sequence, MaleProbe);
	if (bFemaleRan)
	{
		ProbeEngineSkinning(*this, Female, Sequence, FemaleProbe);
	}

	ProbeSecondarySkeleton(
		*this,
		TEXT("female_dancer_2"),
		TEXT("/ElysiumBaked/Characters/Meshes/SK_female_dancer_2.SK_female_dancer_2"));
	ProbeSecondarySkeleton(
		*this,
		TEXT("tremere_female_armor_0"),
		TEXT("/ElysiumBaked/Characters/Meshes/SK_tremere_female_armor_0.SK_tremere_female_armor_0"));
	ProbeSecondarySkeleton(
		*this,
		TEXT("goth_female"),
		TEXT("/ElysiumBaked/Characters/Meshes/SK_goth_female.SK_goth_female"));

	const bool bFemaleCriteria = bFemaleRan
		&& FemaleProbe.bAllRemapEqualsFinal
		&& FemaleProbe.bAllOrientAndScaleAbsentOrNoOp;
	const bool bMalePassThrough = bMaleRan && MaleProbe.MaxAuthoredToRemappedDelta <= 0.001f;
	AddInfo(FString::Printf(
		TEXT("PROBE1|stage=VERDICT_INPUTS|female_remap_equals_final=%d|")
		TEXT("female_oas_absent_or_noop=%d|male_pass_through=%d|")
		TEXT("male_max_authored_remap_delta=%.9f|gpu=NOT_PROBED"),
		FemaleProbe.bAllRemapEqualsFinal ? 1 : 0,
		FemaleProbe.bAllOrientAndScaleAbsentOrNoOp ? 1 : 0,
		bMalePassThrough ? 1 : 0,
		MaleProbe.MaxAuthoredToRemappedDelta));

	Sequence->RemoveFromRoot();
	Female->RemoveFromRoot();
	Male->RemoveFromRoot();

	TestTrue(TEXT("female and male probe paths executed"), bFemaleRan && bMaleRan);
	TestTrue(TEXT("female prime-stage criteria were observed"), bFemaleCriteria);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
