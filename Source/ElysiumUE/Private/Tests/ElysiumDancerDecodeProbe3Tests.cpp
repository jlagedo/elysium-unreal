// ELYSIUM PROBE — TEMPORARY — dancer-decode-probe3

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/BlendProfile.h"
#include "Animation/Skeleton.h"
#include "Animation/SkeletonRemapping.h"
#include "Animation/SkeletonRemappingRegistry.h"
#include "AnimationRuntime.h"
#include "BoneContainer.h"
#include "BonePose.h"
#include "Components/PoseableMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Modules/ModuleManager.h"
#include "ReferenceSkeleton.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "UObject/Package.h"

#include "ElysiumContentPaths.h"
#include "Visual/ElysiumCompositionRig.h"
#include "Visual/ElysiumSkeletalSource.h"

static constexpr EAutomationTestFlags GElysiumDancerDecodeProbe3Flags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace ElysiumProbe3
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

	const TCHAR* GSkeletonPath = TEXT("/ElysiumBaked/Characters/Skeletons");
	const TCHAR* GAnimPath = TEXT("/ElysiumBaked/Characters/Anims");

	struct FSubject
	{
		const TCHAR* Label;
		const TCHAR* MeshPath;
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

	// The first five are the chain the retarget experiment reads; index 0 is `Bip01 L UpperArm`,
	// which the baseline guard below names by position. The rest are the limb HELPERS -- every one
	// of them a `ProcType == 1` driven bone on the two bodies that fold -- so the per-bone remap
	// rows cover the bones the seam attribution actually blames rather than only their controls.
	const FName GFocusedBones[] = {
		TEXT("Bip01 L UpperArm"),
		TEXT("Bip01 L Forearm"),
		TEXT("Bip01 L Hand"),
		TEXT("Bip01 R UpperArm"),
		TEXT("Bip01 R Forearm"),
		TEXT("Bip01 L Shoulder"),
		TEXT("Bip01 R Shoulder"),
		TEXT("Bip01 L Elbow"),
		TEXT("Bip01 R Elbow"),
		TEXT("Bip01 L Bicep"),
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

	// --- transient skeletons --------------------------------------------------------------------

	/**
	 * A transient USkeleton carrying the SAME bone tree as `Original` -- same names, same parents,
	 * same order -- with each bone's local transform decided by `PoseFor`.
	 *
	 * Everything else the original carries that a reference-pose experiment must not disturb is
	 * copied verbatim: the named retarget sources (indexed by bone, so the preserved order is what
	 * makes the copy legal), the per-bone translation retarget MODE, and
	 * `bUseRetargetModesFromCompatibleSkeleton`.
	 *
	 * The mode copy is load-bearing and was not obvious. `EBoneTranslationRetargetingMode::Skeleton`
	 * makes `DecompressPose` skip the compatible-remap translation call outright, so a clone that
	 * silently promoted such a bone to `OrientAndScale` would move it for a reason that has nothing
	 * to do with the reference pose under test.
	 */
	USkeleton* CloneSkeletonWithPose(
		const USkeleton* Original,
		TFunctionRef<FTransform(int32 /*BoneIndex*/, FName /*BoneName*/, const FTransform& /*Local*/)> PoseFor,
		int32& OutOrderMismatches,
		FString& OutError)
	{
		OutOrderMismatches = 0;
		if (Original == nullptr)
		{
			OutError = TEXT("no original skeleton");
			return nullptr;
		}
		const FReferenceSkeleton& SourceRef = Original->GetReferenceSkeleton();
		USkeleton* Clone = NewObject<USkeleton>(GetTransientPackage(), NAME_None, RF_Transient);
		{
			FReferenceSkeletonModifier Modifier(Clone);
			for (int32 BoneIndex = 0; BoneIndex < SourceRef.GetRawBoneNum(); ++BoneIndex)
			{
				const FName BoneName = SourceRef.GetBoneName(BoneIndex);
				Modifier.Add(
					FMeshBoneInfo(BoneName, BoneName.ToString(), SourceRef.GetParentIndex(BoneIndex)),
					PoseFor(BoneIndex, BoneName, SourceRef.GetRefBonePose()[BoneIndex]));
			}
		}
		USkeletalMesh* Carrier =
			NewObject<USkeletalMesh>(GetTransientPackage(), NAME_None, RF_Transient);
		Carrier->SetRefSkeleton(Clone->GetReferenceSkeleton());
		if (!Clone->MergeAllBonesToBoneTree(Carrier))
		{
			OutError = TEXT("the bone tree refused the reference skeleton just authored onto it");
			return nullptr;
		}
		Clone->SetBoneTranslationRetargetingMode(0,
			EBoneTranslationRetargetingMode::OrientAndScale, /*bChildrenToo=*/true);

		// The copies below are only legal if index i still names the same bone on both sides.
		const FReferenceSkeleton& CloneRef = Clone->GetReferenceSkeleton();
		for (int32 BoneIndex = 0; BoneIndex < SourceRef.GetRawBoneNum(); ++BoneIndex)
		{
			if (!CloneRef.GetRawRefBoneInfo().IsValidIndex(BoneIndex)
				|| CloneRef.GetBoneName(BoneIndex) != SourceRef.GetBoneName(BoneIndex))
			{
				++OutOrderMismatches;
				continue;
			}
			Clone->SetBoneTranslationRetargetingMode(BoneIndex,
				Original->GetBoneTranslationRetargetingMode(BoneIndex), /*bChildrenToo=*/false);
		}
		Clone->SetUseRetargetModesFromCompatibleSkeleton(
			Original->GetUseRetargetModesFromCompatibleSkeleton());
		Clone->AnimRetargetSources = Original->AnimRetargetSources;
		return Clone;
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

	/** Swap the skeleton a loaded mesh names, and put it back. Nothing is saved. */
	struct FScopedSkeletonSwap
	{
		USkeletalMesh* Mesh = nullptr;
		USkeleton* Previous = nullptr;

		FScopedSkeletonSwap(USkeletalMesh* InMesh, USkeleton* InSkeleton)
			: Mesh(InMesh)
			, Previous(InMesh != nullptr ? InMesh->GetSkeleton() : nullptr)
		{
			if (Mesh != nullptr && InSkeleton != nullptr)
			{
				Mesh->SetSkeleton(InSkeleton);
			}
		}

		~FScopedSkeletonSwap()
		{
			if (Mesh != nullptr)
			{
				Mesh->SetSkeleton(Previous);
			}
		}
	};

	// --- the transient sequence (probe 2's builder, unchanged) ----------------------------------

	constexpr float AnimatedTranslationCm = 0.1f;
	constexpr float AnimatedRotationDeg = 0.5f;

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
		Controller.OpenBracket(NSLOCTEXT("ElysiumProbe3", "Probe", "Probe clip"),
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
		FQuat Q1 = FQuat::Identity;
		double Q0AngleDeg = 0.0;
		double Q1AngleDeg = 0.0;
		FVector Authored = FVector::ZeroVector;
		FVector TargetBind = FVector::ZeroVector;
		FVector Remapped = FVector::ZeroVector;
		FVector Final = FVector::ZeroVector;
		FQuat FinalRotation = FQuat::Identity;
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
		double MaxQ0AngleDeg = 0.0;
		double MaxQ1AngleDeg = 0.0;
		/** Over EVERY mapped bone of the target skeleton, not just the focused five. */
		int32 MappedBones = 0;
		int32 BonesWithQ0 = 0;
		int32 BonesWithQ1 = 0;
		double WorstQ0AngleDeg = 0.0;
		FName WorstQ0Bone;
		TArray<FBoneRow> Bones;
		/** Indexed by mesh bone; the mesh's own reference pose wherever the pose carried nothing. */
		TArray<FTransform> MeshLocals;
	};

	bool EvaluateOn(USkeletalMesh* Mesh, UAnimSequence* Sequence, FEvalResult& Out)
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

		// The whole retargeting table, so "the reference poses agree" is a count over every bone
		// rather than a claim extrapolated from five.
		if (Out.bRequiresRefPoseRetarget)
		{
			const FReferenceSkeleton& TargetRef = TargetSkeleton->GetReferenceSkeleton();
			for (int32 BoneIndex = 0; BoneIndex < TargetRef.GetRawBoneNum(); ++BoneIndex)
			{
				if (Remapping.GetSourceSkeletonBoneIndex(BoneIndex) == INDEX_NONE)
				{
					continue;
				}
				++Out.MappedBones;
				const TTuple<FQuat, FQuat>& QQ = Remapping.GetRetargetingQuaternions(BoneIndex);
				const double Q0Deg = QuatAngleDegrees(QQ.Get<0>());
				const double Q1Deg = QuatAngleDegrees(QQ.Get<1>());
				Out.BonesWithQ0 += Q0Deg > 0.001 ? 1 : 0;
				Out.BonesWithQ1 += Q1Deg > 0.001 ? 1 : 0;
				if (Q0Deg > Out.WorstQ0AngleDeg)
				{
					Out.WorstQ0AngleDeg = Q0Deg;
					Out.WorstQ0Bone = TargetRef.GetBoneName(BoneIndex);
				}
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
				const TTuple<FQuat, FQuat>& QQ =
					Remapping.GetRetargetingQuaternions(Bone.TargetSkeletonIndex);
				Bone.Q0 = QQ.Get<0>();
				Bone.Q1 = QQ.Get<1>();
				Bone.Q0AngleDeg = QuatAngleDegrees(Bone.Q0);
				Bone.Q1AngleDeg = QuatAngleDegrees(Bone.Q1);
				Bone.Remapped = Remapping.RetargetBoneTranslationToTargetSkeleton(
					Bone.TargetSkeletonIndex, Bone.Authored);
			}
			else
			{
				Bone.Remapped = Bone.Authored;
			}
			Out.MaxQ0AngleDeg = FMath::Max(Out.MaxQ0AngleDeg, Bone.Q0AngleDeg);
			Out.MaxQ1AngleDeg = FMath::Max(Out.MaxQ1AngleDeg, Bone.Q1AngleDeg);
			Bone.Final = Pose[FCompactPoseBoneIndex(Bone.CompactPoseIndex)].GetTranslation();
			Bone.FinalRotation = Pose[FCompactPoseBoneIndex(Bone.CompactPoseIndex)].GetRotation();
			Bone.FinalVersusBind = FVector::Distance(Bone.Final, Bone.TargetBind);
			Out.MaxFinalVersusBind = FMath::Max(Out.MaxFinalVersusBind, Bone.FinalVersusBind);
			if (OrientAndScaleCache.CompactPoseIndexToOrientAndScaleIndex.IsValidIndex(
				Bone.CompactPoseIndex))
			{
				Bone.OrientAndScaleIndex =
					OrientAndScaleCache.CompactPoseIndexToOrientAndScaleIndex[Bone.CompactPoseIndex];
			}
			Bone.bOrientAndScaleEntry =
				OrientAndScaleCache.OrientAndScaleData.IsValidIndex(Bone.OrientAndScaleIndex);
		}
		Out.bValid = true;
		return true;
	}

	void LogEval(FAutomationTestBase& Test, const TCHAR* Stage, const TCHAR* Variant,
		const TCHAR* MeshLabel, const TCHAR* ClipLabel, const FEvalResult& Result,
		const FEvalResult* Baseline)
	{
		Test.AddInfo(FString::Printf(
			TEXT("PROBE3|stage=%s|variant=%s|mesh=%s|clip=%s|remap_valid=%d|")
			TEXT("requires_refpose_retarget=%d|compressed_valid=%d|use_source_retarget_modes=%d|")
			TEXT("max_q0_deg=%.9f|max_q1_deg=%.9f|max_final_vs_bind=%.9f|mapped_bones=%d|")
			TEXT("bones_with_nonidentity_q0=%d|bones_with_nonidentity_q1=%d|worst_q0_bone=%s|")
			TEXT("worst_q0_deg=%.9f"),
			Stage, Variant, MeshLabel, ClipLabel, Result.bRemapValid ? 1 : 0,
			Result.bRequiresRefPoseRetarget ? 1 : 0, Result.bCompressedValid ? 1 : 0,
			Result.bUseSourceRetargetModes ? 1 : 0, Result.MaxQ0AngleDeg, Result.MaxQ1AngleDeg,
			Result.MaxFinalVersusBind, Result.MappedBones, Result.BonesWithQ0, Result.BonesWithQ1,
			Result.WorstQ0Bone.IsNone() ? TEXT("NONE") : *Result.WorstQ0Bone.ToString(),
			Result.WorstQ0AngleDeg));
		for (int32 Index = 0; Index < Result.Bones.Num(); ++Index)
		{
			const FBoneRow& Bone = Result.Bones[Index];
			double RotationDelta = 0.0;
			double TranslationDelta = 0.0;
			if (Baseline != nullptr && Baseline->Bones.IsValidIndex(Index))
			{
				RotationDelta = FMath::RadiansToDegrees(
					Bone.FinalRotation.AngularDistance(Baseline->Bones[Index].FinalRotation));
				TranslationDelta = FVector::Distance(Bone.Final, Baseline->Bones[Index].Final);
			}
			Test.AddInfo(FString::Printf(
				TEXT("PROBE3|stage=%s|variant=%s|mesh=%s|clip=%s|bone=%s|mode=%d|")
				TEXT("q0_deg=%.9f|q1_deg=%.9f|authored=%s|target_bind=%s|remap_out=%s|")
				TEXT("oas_index=%d|oas_entry=%d|final=%s|final_vs_bind=%.9f|final_rot=%s|")
				TEXT("rot_vs_baseline_deg=%.9f|trans_vs_baseline=%.9f"),
				Stage, Variant, MeshLabel, ClipLabel, *Bone.Bone.ToString(),
				static_cast<int32>(Bone.RetargetMode), Bone.Q0AngleDeg, Bone.Q1AngleDeg,
				*VectorText(Bone.Authored), *VectorText(Bone.TargetBind),
				*VectorText(Bone.Remapped), Bone.OrientAndScaleIndex,
				Bone.bOrientAndScaleEntry ? 1 : 0, *VectorText(Bone.Final), Bone.FinalVersusBind,
				*QuatText(Bone.FinalRotation), RotationDelta, TranslationDelta));
		}
	}

	// --- deformation ----------------------------------------------------------------------------

	/** One influence slot of one render vertex, weight re-normalised over the slots it carries. */
	struct FVertexInfluence
	{
		FName Bone;
		float Weight = 0.f;
	};

	struct FStretchEdge
	{
		float Ratio = 0.f;
		float RefDistance = 0.f;
		uint32 A = 0;
		uint32 B = 0;
		TArray<FVertexInfluence> Influences;
	};

	/**
	 * Which BONES the stretched edges belong to.
	 *
	 * An edge ratio names a pair of vertices, and a vertex is only ever moved by the bones weighted
	 * into it -- so the bones carrying a seam are recoverable from the skin weights, exactly, with
	 * no guess about which part of the arm the number came from.
	 */
	struct FStretchAttribution
	{
		TArray<FStretchEdge> Worst;
		/** Summed influence weight over every edge above 2x, per bone. */
		TMap<FName, float> WeightAbove2x;
		int32 EdgesAbove2x = 0;
	};

	/** Render-vertex influences, resolved through the section bone map to mesh bone names. */
	void InfluencesOf(const FSkeletalMeshLODRenderData& LODRenderData,
		const FSkinWeightVertexBuffer& SkinWeights, const FReferenceSkeleton& MeshRef,
		const uint32 Vertex, TArray<FVertexInfluence>& Out)
	{
		const FSkelMeshRenderSection* Section = nullptr;
		for (const FSkelMeshRenderSection& Candidate : LODRenderData.RenderSections)
		{
			if (Vertex >= Candidate.BaseVertexIndex
				&& Vertex < Candidate.BaseVertexIndex + Candidate.NumVertices)
			{
				Section = &Candidate;
				break;
			}
		}
		if (Section == nullptr)
		{
			return;
		}
		// The raw weights are fixed point and their scale differs by skin-weight buffer format, so
		// they are normalised over the slots rather than divided by an assumed maximum.
		TArray<FVertexInfluence> Slots;
		float Total = 0.f;
		const uint32 MaxInfluences = SkinWeights.GetMaxBoneInfluences();
		for (uint32 Influence = 0; Influence < MaxInfluences; ++Influence)
		{
			const float Raw = static_cast<float>(SkinWeights.GetBoneWeight(Vertex, Influence));
			if (Raw <= 0.f)
			{
				continue;
			}
			const int32 SectionBone = static_cast<int32>(SkinWeights.GetBoneIndex(Vertex, Influence));
			if (!Section->BoneMap.IsValidIndex(SectionBone))
			{
				continue;
			}
			FVertexInfluence Slot;
			Slot.Bone = MeshRef.GetBoneName(Section->BoneMap[SectionBone]);
			Slot.Weight = Raw;
			Total += Raw;
			Slots.Add(MoveTemp(Slot));
		}
		for (FVertexInfluence& Slot : Slots)
		{
			Slot.Weight = Total > 0.f ? Slot.Weight / Total : 0.f;
			Out.Add(MoveTemp(Slot));
		}
	}

	struct FSkinMetrics
	{
		bool bValid = false;
		bool bExactValid = false;
		float ExactRefDistance = 0.f;
		float ExactSkinDistance = 0.f;
		float ExactRatio = 0.f;
		float MaxRatio = 0.f;
		/**
		 * The same maximum over edges at least half a centimetre long.
		 *
		 * The ratio divides by the reference edge, and this cast's meshes carry edges under two
		 * millimetres -- so the unrestricted maximum can be produced by a sub-millimetre edge moving
		 * a centimetre, which is not a seam anybody can see. Reported beside the raw maximum rather
		 * than in place of it, because which one moved is itself the diagnosis.
		 */
		float MaxRatioCoarse = 0.f;
		float P99Ratio = 0.f;
		int32 Above2x = 0;
		int32 Above5x = 0;
		int32 Edges = 0;
	};

	/** Edges shorter than this are excluded from `MaxRatioCoarse`, in centimetres. */
	constexpr float CoarseEdgeCm = 0.5f;

	FSkinMetrics SkinAndMeasure(UWorld* World, USkeletalMesh* Mesh,
		const TArray<FTransform>& MeshLocals, bool bExactPair,
		FStretchAttribution* Attribution = nullptr)
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
				if (RefDistance >= CoarseEdgeCm)
				{
					Metrics.MaxRatioCoarse = FMath::Max(Metrics.MaxRatioCoarse, Ratio);
				}
				Metrics.Above2x += Ratio > 2.f ? 1 : 0;
				Metrics.Above5x += Ratio > 5.f ? 1 : 0;
				if (Attribution != nullptr && Ratio > 2.f)
				{
					FStretchEdge Hot;
					Hot.Ratio = Ratio;
					Hot.RefDistance = RefDistance;
					Hot.A = A;
					Hot.B = B;
					Attribution->Worst.Add(MoveTemp(Hot));
				}
			}
		}

		if (Attribution != nullptr)
		{
			Attribution->EdgesAbove2x = Attribution->Worst.Num();
			Attribution->Worst.Sort([](const FStretchEdge& A, const FStretchEdge& B)
				{
					return A.Ratio > B.Ratio;
				});
			// Every >2x edge is tallied; only the worst few keep their influence list, because the
			// list is what a reader inspects and the tally is what a reader counts.
			constexpr int32 WorstKept = 8;
			for (int32 Index = 0; Index < Attribution->Worst.Num(); ++Index)
			{
				FStretchEdge& Hot = Attribution->Worst[Index];
				TArray<FVertexInfluence> Both;
				InfluencesOf(LODRenderData, *SkinWeights, Mesh->GetRefSkeleton(), Hot.A, Both);
				InfluencesOf(LODRenderData, *SkinWeights, Mesh->GetRefSkeleton(), Hot.B, Both);
				for (const FVertexInfluence& Slot : Both)
				{
					Attribution->WeightAbove2x.FindOrAdd(Slot.Bone) += Slot.Weight;
				}
				if (Index < WorstKept)
				{
					Hot.Influences = MoveTemp(Both);
				}
			}
			if (Attribution->Worst.Num() > WorstKept)
			{
				Attribution->Worst.SetNum(WorstKept);
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

	void LogMetrics(FAutomationTestBase& Test, const TCHAR* Stage, const TCHAR* Variant,
		const TCHAR* MeshLabel, const TCHAR* ClipLabel, const FSkinMetrics& Metrics)
	{
		Test.AddInfo(FString::Printf(
			TEXT("PROBE3|stage=%s|variant=%s|mesh=%s|clip=%s|valid=%d|edges=%d|max_ratio=%.9f|")
			TEXT("max_ratio_edges_over_%.1fcm=%.9f|")
			TEXT("p99_ratio=%.9f|above_2x=%d|above_5x=%d|exact_valid=%d|exact_ref=%.9f|")
			TEXT("exact_skin=%.9f|exact_ratio=%.9f|cpu_final=PROBED|gpu=NOT_PROBED"),
			Stage, Variant, MeshLabel, ClipLabel, Metrics.bValid ? 1 : 0, Metrics.Edges,
			Metrics.MaxRatio, CoarseEdgeCm, Metrics.MaxRatioCoarse,
			Metrics.P99Ratio, Metrics.Above2x, Metrics.Above5x,
			Metrics.bExactValid ? 1 : 0, Metrics.ExactRefDistance, Metrics.ExactSkinDistance,
			Metrics.ExactRatio));
	}

	// --- the seam, attributed to bones --------------------------------------------------------

	/**
	 * Replace every driven bone's local with the axis-interpolation rule's answer, exactly as
	 * `FAnimNode_ElysiumAxisInterp` does when a body is posed by the graph.
	 *
	 * The node reads the control's local out of component space and writes the driven bone back into
	 * it; here the pose already IS a local array, so the same rule reads and writes it directly.
	 * Rules run in mesh bone order for the same reason the node sorts by compact pose index: a
	 * driven bone may itself control another.
	 */
	int32 ApplyCompositionRig(const FElysiumCompositionRig& Rig, const FReferenceSkeleton& MeshRef,
		TArray<FTransform>& MeshLocals, int32& OutUnresolved)
	{
		OutUnresolved = 0;
		/** (driven mesh bone, control mesh bone, rule index), for the rules this mesh can run. */
		TArray<TTuple<int32, int32, int32>> Ordered;
		Ordered.Reserve(Rig.AxisRules.Num());
		for (int32 RuleIndex = 0; RuleIndex < Rig.AxisRules.Num(); ++RuleIndex)
		{
			const FElysiumAxisInterpRule& Rule = Rig.AxisRules[RuleIndex];
			const int32 Bone = MeshRef.FindBoneIndex(Rule.Bone);
			const int32 Control = MeshRef.FindBoneIndex(Rule.Control);
			if (Bone == INDEX_NONE || Control == INDEX_NONE || !MeshLocals.IsValidIndex(Bone)
				|| !MeshLocals.IsValidIndex(Control))
			{
				++OutUnresolved;
				continue;
			}
			Ordered.Add(MakeTuple(Bone, Control, RuleIndex));
		}
		Ordered.Sort([](const TTuple<int32, int32, int32>& A, const TTuple<int32, int32, int32>& B)
			{
				return A.Get<0>() < B.Get<0>();
			});
		for (const TTuple<int32, int32, int32>& Entry : Ordered)
		{
			MeshLocals[Entry.Get<0>()] = Rig.EvaluateRule(Rig.AxisRules[Entry.Get<2>()],
				MeshLocals[Entry.Get<1>()].GetRotation());
		}
		return Ordered.Num();
	}

	FString InfluenceText(const TArray<FVertexInfluence>& Influences)
	{
		TArray<FString> Parts;
		for (const FVertexInfluence& Slot : Influences)
		{
			Parts.Add(FString::Printf(TEXT("%s=%.2f"), *Slot.Bone.ToString(), Slot.Weight));
		}
		return Parts.IsEmpty() ? TEXT("NONE") : FString::Join(Parts, TEXT(","));
	}

	void LogAttribution(FAutomationTestBase& Test, const TCHAR* Stage, const TCHAR* Variant,
		const TCHAR* MeshLabel, const TCHAR* ClipLabel, const FStretchAttribution& Attribution,
		const TArray<FTransform>& Posed, const FReferenceSkeleton& MeshRef,
		const FElysiumCompositionRig& Rig)
	{
		for (const FStretchEdge& Hot : Attribution.Worst)
		{
			Test.AddInfo(FString::Printf(
				TEXT("PROBE3|stage=%s_EDGE|variant=%s|mesh=%s|clip=%s|ratio=%.6f|ref_cm=%.6f|")
				TEXT("vertices=%u/%u|influences=%s"),
				Stage, Variant, MeshLabel, ClipLabel, Hot.Ratio, Hot.RefDistance, Hot.A, Hot.B,
				*InfluenceText(Hot.Influences)));
		}

		// Sorted by blamed weight, so the first rows are the bones the seam is made of.
		TArray<TPair<FName, float>> Blame;
		for (const TPair<FName, float>& Entry : Attribution.WeightAbove2x)
		{
			Blame.Add(Entry);
		}
		Blame.Sort([](const TPair<FName, float>& A, const TPair<FName, float>& B)
			{
				return A.Value > B.Value;
			});
		int32 Row = 0;
		for (const TPair<FName, float>& Blamed : Blame)
		{
			if (Row++ >= 12)
			{
				break;
			}
			const int32 Bone = MeshRef.FindBoneIndex(Blamed.Key);
			const bool bDriven = Rig.FindRule(Blamed.Key) != nullptr;
			const FTransform& Bind = MeshRef.GetRefBonePose().IsValidIndex(Bone)
				? MeshRef.GetRefBonePose()[Bone] : FTransform::Identity;
			const FTransform& Local = Posed.IsValidIndex(Bone) ? Posed[Bone] : FTransform::Identity;
			Test.AddInfo(FString::Printf(
				TEXT("PROBE3|stage=%s_BONE|variant=%s|mesh=%s|clip=%s|bone=%s|blamed_weight=%.3f|")
				TEXT("procedurally_driven=%d|rotation_vs_mesh_bind_deg=%.6f|")
				TEXT("translation_vs_mesh_bind_cm=%.6f"),
				Stage, Variant, MeshLabel, ClipLabel, *Blamed.Key.ToString(), Blamed.Value,
				bDriven ? 1 : 0,
				FMath::RadiansToDegrees(Local.GetRotation().AngularDistance(Bind.GetRotation())),
				FVector::Distance(Local.GetTranslation(), Bind.GetTranslation())));
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FElysiumDancerDecodeProbe3Test,
	"Instrument.Elysium.DancerDecodeProbe3",
	GElysiumDancerDecodeProbe3Flags)

bool FElysiumDancerDecodeProbe3Test::RunTest(const FString&)
{
	using namespace ElysiumProbe3;

	if (!FElysiumContentPaths::IsConfigured())
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: PROBE3|stage=SKIP|reason=export_root_not_configured"));
		return true;
	}

	UAnimSequence* RealJoy = LoadObject<UAnimSequence>(nullptr, GJoyPath);
	UAnimSequence* RealDance = LoadObject<UAnimSequence>(nullptr, GDancePath);
	if (RealJoy == nullptr)
	{
		AddError(TEXT("PROBE3|stage=LOAD|error=joy_sequence"));
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
			AddError(FString::Printf(TEXT("PROBE3|stage=LOAD|error=mesh|label=%s"), Subject.Label));
			return false;
		}
		Mesh->AddToRoot();
		Meshes.Add(Mesh);
	}

	UWorld* ProbeWorld = UWorld::CreateWorld(EWorldType::Game, false);
	if (ProbeWorld == nullptr)
	{
		AddError(TEXT("PROBE3|stage=WORLD|error=create_failed"));
		return false;
	}
	auto Finish = [&]()
	{
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
	};

	// =============================================================================================
	// STEP 2 -- baseline guard (run first so every later number has a known anchor)
	// =============================================================================================

	AddInfo(FString::Printf(
		TEXT("PROBE3|stage=GUARD_SKELETON|which=bank|path=%s|bones=%d|")
		TEXT("use_retarget_modes_from_compatible=%d|virtual_bones=%d|sockets=%d|blend_profiles=%d|")
		TEXT("retarget_sources=%d"),
		*BankSkeleton->GetPathName(), BankSkeleton->GetReferenceSkeleton().GetRawBoneNum(),
		BankSkeleton->GetUseRetargetModesFromCompatibleSkeleton() ? 1 : 0,
		BankSkeleton->GetReferenceSkeleton().GetVirtualBoneRefData().Num(),
		BankSkeleton->Sockets.Num(), BankSkeleton->BlendProfiles.Num(),
		BankSkeleton->AnimRetargetSources.Num()));
	for (const FName BoneName : GFocusedBones)
	{
		const int32 Index = BankSkeleton->GetReferenceSkeleton().FindBoneIndex(BoneName);
		AddInfo(FString::Printf(
			TEXT("PROBE3|stage=GUARD_MODE|which=bank|bone=%s|index=%d|mode=%d"),
			*BoneName.ToString(), Index,
			Index == INDEX_NONE ? -1
				: static_cast<int32>(BankSkeleton->GetBoneTranslationRetargetingMode(Index))));
	}
	for (int32 SubjectIndex = 0; SubjectIndex < Meshes.Num(); ++SubjectIndex)
	{
		USkeleton* TargetSkeleton = Meshes[SubjectIndex]->GetSkeleton();
		AddInfo(FString::Printf(
			TEXT("PROBE3|stage=GUARD_SKELETON|which=%s|standing=%s|path=%s|bones=%d|")
			TEXT("use_retarget_modes_from_compatible=%d|virtual_bones=%d|sockets=%d|")
			TEXT("blend_profiles=%d|retarget_sources=%d"),
			GSubjects[SubjectIndex].Label, GSubjects[SubjectIndex].Standing,
			TargetSkeleton != nullptr ? *TargetSkeleton->GetPathName() : TEXT("NONE"),
			TargetSkeleton != nullptr ? TargetSkeleton->GetReferenceSkeleton().GetRawBoneNum() : -1,
			TargetSkeleton != nullptr
				&& TargetSkeleton->GetUseRetargetModesFromCompatibleSkeleton() ? 1 : 0,
			TargetSkeleton != nullptr
				? TargetSkeleton->GetReferenceSkeleton().GetVirtualBoneRefData().Num() : -1,
			TargetSkeleton != nullptr ? TargetSkeleton->Sockets.Num() : -1,
			TargetSkeleton != nullptr ? TargetSkeleton->BlendProfiles.Num() : -1,
			TargetSkeleton != nullptr ? TargetSkeleton->AnimRetargetSources.Num() : -1));
		if (TargetSkeleton == nullptr)
		{
			continue;
		}
		for (const FName BoneName : GFocusedBones)
		{
			const int32 Index = TargetSkeleton->GetReferenceSkeleton().FindBoneIndex(BoneName);
			AddInfo(FString::Printf(
				TEXT("PROBE3|stage=GUARD_MODE|which=%s|bone=%s|index=%d|mode=%d"),
				GSubjects[SubjectIndex].Label, *BoneName.ToString(), Index,
				Index == INDEX_NONE ? -1
					: static_cast<int32>(TargetSkeleton->GetBoneTranslationRetargetingMode(Index))));
		}
	}

	// The whole per-bone mode table, not just the focused five. A bone in
	// `EBoneTranslationRetargetingMode::Skeleton` never reaches the compatible-remap translation
	// call at all, so any residue of the reverted per-bone experiment changes what a clone must
	// carry -- and it is the thing probe 2 could only read for five bones.
	{
		TArray<const USkeleton*> ModeSkeletons;
		ModeSkeletons.Add(BankSkeleton);
		for (USkeletalMesh* Mesh : Meshes)
		{
			ModeSkeletons.AddUnique(Mesh->GetSkeleton());
		}
		for (const USkeleton* Skeleton : ModeSkeletons)
		{
			int32 Counts[8] = {};
			TArray<FString> NonOrientAndScale;
			const FReferenceSkeleton& Ref = Skeleton->GetReferenceSkeleton();
			for (int32 BoneIndex = 0; BoneIndex < Ref.GetRawBoneNum(); ++BoneIndex)
			{
				const int32 Mode =
					static_cast<int32>(Skeleton->GetBoneTranslationRetargetingMode(BoneIndex));
				if (Mode >= 0 && Mode < 8)
				{
					++Counts[Mode];
				}
				if (Mode != static_cast<int32>(EBoneTranslationRetargetingMode::OrientAndScale)
					&& NonOrientAndScale.Num() < 12)
				{
					NonOrientAndScale.Add(FString::Printf(TEXT("%s=%d"),
						*Ref.GetBoneName(BoneIndex).ToString(), Mode));
				}
			}
			AddInfo(FString::Printf(
				TEXT("PROBE3|stage=GUARD_MODE_CENSUS|skeleton=%s|bones=%d|animation=%d|")
				TEXT("skeleton_mode=%d|animation_scaled=%d|animation_relative=%d|")
				TEXT("orient_and_scale=%d|non_orient_and_scale_sample=%s"),
				*Skeleton->GetName(), Ref.GetRawBoneNum(), Counts[0], Counts[1], Counts[2],
				Counts[3], Counts[4],
				NonOrientAndScale.IsEmpty() ? TEXT("NONE") : *FString::Join(NonOrientAndScale, TEXT(","))));
		}
	}

	// Today's numbers, per body and per clip, kept for the rotation comparison later.
	TArray<FEvalResult> BaselineJoy;
	TArray<FEvalResult> BaselineDance;
	TArray<FSkinMetrics> BaselineJoyMetrics;
	TArray<FSkinMetrics> BaselineDanceMetrics;
	BaselineJoy.SetNum(Meshes.Num());
	BaselineDance.SetNum(Meshes.Num());
	BaselineJoyMetrics.SetNum(Meshes.Num());
	BaselineDanceMetrics.SetNum(Meshes.Num());
	for (int32 SubjectIndex = 0; SubjectIndex < Meshes.Num(); ++SubjectIndex)
	{
		EvaluateOn(Meshes[SubjectIndex], RealJoy, BaselineJoy[SubjectIndex]);
		LogEval(*this, TEXT("BASELINE"), TEXT("baked_assets"), GSubjects[SubjectIndex].Label,
			TEXT("Joy"), BaselineJoy[SubjectIndex], nullptr);
		BaselineJoyMetrics[SubjectIndex] = SkinAndMeasure(ProbeWorld, Meshes[SubjectIndex],
			BaselineJoy[SubjectIndex].MeshLocals, SubjectIndex == 0);
		LogMetrics(*this, TEXT("BASELINE"), TEXT("baked_assets"), GSubjects[SubjectIndex].Label,
			TEXT("Joy"), BaselineJoyMetrics[SubjectIndex]);
		if (RealDance != nullptr)
		{
			EvaluateOn(Meshes[SubjectIndex], RealDance, BaselineDance[SubjectIndex]);
			LogEval(*this, TEXT("BASELINE"), TEXT("baked_assets"), GSubjects[SubjectIndex].Label,
				TEXT("Dance"), BaselineDance[SubjectIndex], nullptr);
			BaselineDanceMetrics[SubjectIndex] = SkinAndMeasure(ProbeWorld, Meshes[SubjectIndex],
				BaselineDance[SubjectIndex].MeshLocals, SubjectIndex == 0);
			LogMetrics(*this, TEXT("BASELINE"), TEXT("baked_assets"), GSubjects[SubjectIndex].Label,
				TEXT("Dance"), BaselineDanceMetrics[SubjectIndex]);
		}
	}

	const FVector ExpectedUpperArm(10.592618944, 1.791687106, -0.436005119);
	bool bBaselineClean = false;
	if (!BaselineJoy[0].Bones.IsEmpty())
	{
		const double Delta = FVector::Distance(BaselineJoy[0].Bones[0].Final, ExpectedUpperArm);
		const float SeamDelta = FMath::Abs(BaselineJoyMetrics[0].ExactRatio - 8.267464638f);
		bBaselineClean = Delta <= 0.001 && SeamDelta <= 0.01f;
		AddInfo(FString::Printf(
			TEXT("PROBE3|stage=GUARD_VERDICT|expected_upperarm=%s|observed_upperarm=%s|delta=%.9f|")
			TEXT("expected_seam=8.267464638|observed_seam=%.9f|seam_delta=%.9f|")
			TEXT("expected_ref_edge=1.075366497|observed_ref_edge=%.9f|clean=%d"),
			*VectorText(ExpectedUpperArm), *VectorText(BaselineJoy[0].Bones[0].Final), Delta,
			BaselineJoyMetrics[0].ExactRatio, SeamDelta, BaselineJoyMetrics[0].ExactRefDistance,
			bBaselineClean ? 1 : 0));
	}
	if (!bBaselineClean)
	{
		AddWarning(TEXT("PROBE3|stage=GUARD_VERDICT|status=BASELINE_DID_NOT_REPRODUCE"));
	}

	// =============================================================================================
	// SEAM -- which bones carry the stretch, and what a posed body does about them
	// =============================================================================================
	//
	// Two questions, both answered as numbers rather than as an argument.
	//
	// WHICH BONES: an edge ratio names two vertices, and a vertex moves only by the bones weighted
	// into it, so the skin weights attribute every stretched edge exactly.
	//
	// AGAINST WHAT: everything above skins through a `UPoseableMeshComponent`, which runs no
	// animation graph -- so it never applies the axis-interpolation stage a posed body runs
	// (`FAnimNode_ElysiumAxisInterp`, `docs/architecture/animation-architecture.md` section 5), and
	// every limb helper holds its BIND while its control swings. That is the same condition as the
	// Content Browser preview, which `Source/ElysiumUE/CLAUDE.md` already records as not a bake
	// defect. Measuring the same pose both ways is what separates the two readings.

	for (int32 SubjectIndex = 0; SubjectIndex < Meshes.Num(); ++SubjectIndex)
	{
		USkeletalMesh* Mesh = Meshes[SubjectIndex];
		const TCHAR* Label = GSubjects[SubjectIndex].Label;
		const FReferenceSkeleton& MeshRef = Mesh->GetRefSkeleton();

		FElysiumCompositionRig Rig;
		Rig.Stem = Label;
		FString RigError;
		const bool bRigLoaded =
			Rig.LoadAxisRules(FString::Printf(TEXT("procedural/%s.json"), Label), RigError);
		AddInfo(FString::Printf(
			TEXT("PROBE3|stage=SEAM_RIG|mesh=%s|loaded=%d|axis_rules=%d|error=%s"),
			Label, bRigLoaded ? 1 : 0, Rig.AxisRules.Num(),
			bRigLoaded ? TEXT("NONE") : *RigError));

		struct FSeamClip
		{
			const TCHAR* Label;
			const FEvalResult* Result;
		};
		const FSeamClip SeamClips[] = {
			{ TEXT("Joy"), &BaselineJoy[SubjectIndex] },
			{ TEXT("Dance"), &BaselineDance[SubjectIndex] },
		};
		for (const FSeamClip& Clip : SeamClips)
		{
			if (Clip.Result == nullptr || !Clip.Result->bValid)
			{
				AddWarning(FString::Printf(
					TEXT("PROBE3|stage=SEAM|mesh=%s|clip=%s|status=NOT_PROBED|reason=no_pose"),
					Label, Clip.Label));
				continue;
			}

			FStretchAttribution Bare;
			const FSkinMetrics BareMetrics = SkinAndMeasure(ProbeWorld, Mesh,
				Clip.Result->MeshLocals, SubjectIndex == 0, &Bare);
			LogMetrics(*this, TEXT("SEAM"), TEXT("no_composition"), Label, Clip.Label, BareMetrics);
			LogAttribution(*this, TEXT("SEAM"), TEXT("no_composition"), Label, Clip.Label, Bare,
				Clip.Result->MeshLocals, MeshRef, Rig);

			if (Rig.AxisRules.IsEmpty())
			{
				continue;
			}

			TArray<FTransform> Driven = Clip.Result->MeshLocals;
			int32 Unresolved = 0;
			const int32 Applied = ApplyCompositionRig(Rig, MeshRef, Driven, Unresolved);
			AddInfo(FString::Printf(
				TEXT("PROBE3|stage=SEAM_APPLY|mesh=%s|clip=%s|rules_applied=%d|rules_unresolved=%d"),
				Label, Clip.Label, Applied, Unresolved));

			FStretchAttribution DrivenBlame;
			const FSkinMetrics DrivenMetrics =
				SkinAndMeasure(ProbeWorld, Mesh, Driven, SubjectIndex == 0, &DrivenBlame);
			LogMetrics(*this, TEXT("SEAM"), TEXT("composition_applied"), Label, Clip.Label,
				DrivenMetrics);
			LogAttribution(*this, TEXT("SEAM"), TEXT("composition_applied"), Label, Clip.Label,
				DrivenBlame, Driven, MeshRef, Rig);

			// How far the rule moved each driven bone, beside how far its control travelled. A
			// correction near zero on a control that swung is the helper holding its bind.
			for (const FElysiumAxisInterpRule& Rule : Rig.AxisRules)
			{
				const int32 Bone = MeshRef.FindBoneIndex(Rule.Bone);
				const int32 Control = MeshRef.FindBoneIndex(Rule.Control);
				if (!Driven.IsValidIndex(Bone) || !Clip.Result->MeshLocals.IsValidIndex(Bone)
					|| !Clip.Result->MeshLocals.IsValidIndex(Control))
				{
					continue;
				}
				const FQuat Undriven = Clip.Result->MeshLocals[Bone].GetRotation();
				const FQuat Corrected = Driven[Bone].GetRotation();
				const FQuat BoneBind = MeshRef.GetRefBonePose()[Bone].GetRotation();
				const FQuat ControlBind = MeshRef.GetRefBonePose()[Control].GetRotation();
				const FQuat ControlLocal = Clip.Result->MeshLocals[Control].GetRotation();
				AddInfo(FString::Printf(
					TEXT("PROBE3|stage=SEAM_DRIVEN|mesh=%s|clip=%s|bone=%s|control=%s|")
					TEXT("undriven_vs_bind_deg=%.6f|driven_vs_bind_deg=%.6f|correction_deg=%.6f|")
					TEXT("control_vs_bind_deg=%.6f"),
					Label, Clip.Label, *Rule.Bone.ToString(), *Rule.Control.ToString(),
					FMath::RadiansToDegrees(Undriven.AngularDistance(BoneBind)),
					FMath::RadiansToDegrees(Corrected.AngularDistance(BoneBind)),
					FMath::RadiansToDegrees(Corrected.AngularDistance(Undriven)),
					FMath::RadiansToDegrees(ControlLocal.AngularDistance(ControlBind))));
			}
		}
	}

	// =============================================================================================
	// SEAM_SURVEY -- does the 5x flag pick A_dance01 out of its own bank?
	// =============================================================================================
	//
	// A threshold that fires on one clip is a finding; a threshold that fires on half the corpus is
	// an instrument. The same measurement runs over a stride sample of the bank A_dance01 ships in,
	// on one of the two bodies the seam was reported on, and reports where A_dance01 lands among
	// them. Additives are skipped: a delta is not a pose, and skinning one measures nothing.
	{
		const int32 SurveySubject = 1;		// goth_female
		USkeletalMesh* SurveyMesh = Meshes[SurveySubject];
		IAssetRegistry& SurveyRegistry =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		const FString BankPath = FString(GAnimPath) / TEXT("_banks") / GMiscStem;
		SurveyRegistry.ScanPathsSynchronous({ BankPath }, /*bForceRescan=*/false);

		FARFilter Filter;
		Filter.PackagePaths.Add(FName(*BankPath));
		Filter.bRecursivePaths = true;
		Filter.ClassPaths.Add(UAnimSequence::StaticClass()->GetClassPathName());
		TArray<FAssetData> Assets;
		SurveyRegistry.GetAssets(Filter, Assets);
		Assets.Sort([](const FAssetData& A, const FAssetData& B)
			{
				return A.GetObjectPathString() < B.GetObjectPathString();
			});

		const int32 Budget = 48;
		const int32 Stride = FMath::Max(1, Assets.Num() / FMath::Max(1, Budget));
		TArray<TPair<float, FString>> Measured;
		float DanceRatio = 0.f;
		for (int32 Index = 0; Index < Assets.Num(); ++Index)
		{
			const bool bIsDance = Assets[Index].AssetName == FName(TEXT("A_dance01"));
			if (Index % Stride != 0 && !bIsDance)
			{
				continue;
			}
			UAnimSequence* Sequence = Cast<UAnimSequence>(Assets[Index].GetAsset());
			if (Sequence == nullptr || Sequence->IsValidAdditive())
			{
				continue;
			}
			FEvalResult Result;
			if (!EvaluateOn(SurveyMesh, Sequence, Result))
			{
				continue;
			}
			const FSkinMetrics Metrics =
				SkinAndMeasure(ProbeWorld, SurveyMesh, Result.MeshLocals, false);
			Measured.Add(TPair<float, FString>(Metrics.MaxRatio, Assets[Index].AssetName.ToString()));
			if (bIsDance)
			{
				DanceRatio = Metrics.MaxRatio;
			}
			AddInfo(FString::Printf(
				TEXT("PROBE3|stage=SEAM_SURVEY_CLIP|mesh=%s|clip=%s|max_ratio=%.6f|")
				TEXT("max_ratio_edges_over_%.1fcm=%.6f|above_5x=%d"),
				GSubjects[SurveySubject].Label, *Assets[Index].AssetName.ToString(),
				Metrics.MaxRatio, CoarseEdgeCm, Metrics.MaxRatioCoarse, Metrics.Above5x));
		}

		TArray<float> Ratios;
		for (const TPair<float, FString>& Row : Measured)
		{
			Ratios.Add(Row.Key);
		}
		Ratios.Sort();
		int32 Above5x = 0;
		int32 WorseThanDance = 0;
		for (const float Ratio : Ratios)
		{
			Above5x += Ratio > 5.f ? 1 : 0;
			WorseThanDance += Ratio > DanceRatio ? 1 : 0;
		}
		AddInfo(FString::Printf(
			TEXT("PROBE3|stage=SEAM_SURVEY|mesh=%s|bank=%s|clips_in_bank=%d|sampled=%d|stride=%d|")
			TEXT("median_max_ratio=%.6f|clips_above_5x=%d|dance01_max_ratio=%.6f|")
			TEXT("clips_stretching_more_than_dance01=%d"),
			GSubjects[SurveySubject].Label, GMiscStem, Assets.Num(), Ratios.Num(), Stride,
			Ratios.IsEmpty() ? 0.f : Ratios[Ratios.Num() / 2], Above5x, DanceRatio,
			WorseThanDance));
	}

	// =============================================================================================
	// STEP 1 -- the falsifier: where does an UNTRACKED bone come from?
	// =============================================================================================
	//
	// The discriminator is built so that only one thing changes. Displacing a bone's TRANSLATION on
	// the target skeleton leaves every entry of the remapping table alone (Q0 and Q1 are built from
	// ROTATIONS only), so the pose value for that bone can move for exactly one reason: the
	// skeleton's reference pose being what an untracked bone falls back to. A second clone displaces
	// the same bone's ROTATION instead -- that one MUST move something if the clone is being read at
	// all, which is the positive control that the swap took effect.

	bool bResolvesToMesh = true;
	bool bStep1Ran = false;
	{
		TArray<FName> TrackedNames;
		if (IAnimationDataModel* Model = RealJoy->GetDataModel())
		{
			Model->GetBoneTrackNames(TrackedNames);
		}
		const TSet<FName> Tracked(TrackedNames);
		AddInfo(FString::Printf(TEXT("PROBE3|stage=S1_TRACKS|sequence=%s|track_names=%d"),
			*RealJoy->GetName(), TrackedNames.Num()));

		const int32 Step1Subjects[] = { 0, 2 };
		for (const int32 SubjectIndex : Step1Subjects)
		{
			USkeletalMesh* Mesh = Meshes[SubjectIndex];
			USkeleton* Original = Mesh->GetSkeleton();
			const FReferenceSkeleton& MeshRef = Mesh->GetRefSkeleton();

			TArray<FName> Untracked;
			for (int32 BoneIndex = 1; BoneIndex < MeshRef.GetRawBoneNum()
				&& Untracked.Num() < 3; ++BoneIndex)
			{
				const FName BoneName = MeshRef.GetBoneName(BoneIndex);
				if (Tracked.Contains(BoneName))
				{
					continue;
				}
				if (MeshRef.GetRefBonePose()[BoneIndex].GetTranslation().Size() < 1.0)
				{
					continue;
				}
				Untracked.Add(BoneName);
			}
			if (Untracked.IsEmpty())
			{
				AddWarning(FString::Printf(
					TEXT("PROBE3|stage=S1|mesh=%s|status=NOT_PROBED|reason=no_untracked_bone"),
					GSubjects[SubjectIndex].Label));
				continue;
			}

			// Baseline pose, so the untracked bone's value today is on the record.
			FEvalResult Before;
			EvaluateOn(Mesh, RealJoy, Before);

			const TSet<FName> Displaced(Untracked);
			const FVector Offset(0.0, 500.0, 0.0);
			const FQuat Twist(FVector::UpVector, FMath::DegreesToRadians(90.0));

			int32 Mismatches = 0;
			FString CloneError;
			USkeleton* IdenticalClone = CloneSkeletonWithPose(Original,
				[](int32, FName, const FTransform& Local)
				{
					return Local;
				}, Mismatches, CloneError);
			USkeleton* TranslationClone = CloneSkeletonWithPose(Original,
				[&Displaced, &Offset](int32, FName Name, const FTransform& Local)
				{
					if (!Displaced.Contains(Name))
					{
						return Local;
					}
					FTransform Moved = Local;
					Moved.SetTranslation(Local.GetTranslation() + Offset);
					return Moved;
				}, Mismatches, CloneError);
			USkeleton* RotationClone = CloneSkeletonWithPose(Original,
				[&Displaced, &Twist](int32, FName Name, const FTransform& Local)
				{
					if (!Displaced.Contains(Name))
					{
						return Local;
					}
					FTransform Turned = Local;
					Turned.SetRotation(Twist * Local.GetRotation());
					return Turned;
				}, Mismatches, CloneError);
			if (IdenticalClone == nullptr || TranslationClone == nullptr || RotationClone == nullptr)
			{
				AddWarning(FString::Printf(
					TEXT("PROBE3|stage=S1|mesh=%s|status=NOT_PROBED|error=%s"),
					GSubjects[SubjectIndex].Label, *CloneError));
				continue;
			}
			IdenticalClone->AddToRoot();
			TranslationClone->AddToRoot();
			RotationClone->AddToRoot();

			// The three runs differ in exactly one thing each, and the CONTROL is the identical
			// clone -- not the saved skeleton -- so "being a transient clone" is held fixed and only
			// the reference pose varies between control and test.
			TArray<FTransform> IdenticalLocals;
			TArray<FTransform> TranslationLocals;
			TArray<FTransform> RotationLocals;
			{
				FEvalResult Result;
				FScopedSkeletonSwap Swap(Mesh, IdenticalClone);
				EvaluateOn(Mesh, RealJoy, Result);
				IdenticalLocals = Result.MeshLocals;
			}
			{
				FEvalResult Result;
				FScopedSkeletonSwap Swap(Mesh, TranslationClone);
				EvaluateOn(Mesh, RealJoy, Result);
				TranslationLocals = Result.MeshLocals;
				AddInfo(FString::Printf(
					TEXT("PROBE3|stage=S1_SWAP|mesh=%s|clone=translation|")
					TEXT("mesh_names_skeleton=%s|bone_order_mismatches=%d"),
					GSubjects[SubjectIndex].Label, *Mesh->GetSkeleton()->GetName(), Mismatches));
			}
			{
				FEvalResult Result;
				FScopedSkeletonSwap Swap(Mesh, RotationClone);
				EvaluateOn(Mesh, RealJoy, Result);
				RotationLocals = Result.MeshLocals;
			}

			for (const FName BoneName : Untracked)
			{
				const int32 MeshIndex = MeshRef.FindBoneIndex(BoneName);
				const int32 SkeletonIndex =
					Original->GetReferenceSkeleton().FindBoneIndex(BoneName);
				if (MeshIndex == INDEX_NONE || SkeletonIndex == INDEX_NONE
					|| !Before.MeshLocals.IsValidIndex(MeshIndex)
					|| !TranslationLocals.IsValidIndex(MeshIndex)
					|| !RotationLocals.IsValidIndex(MeshIndex))
				{
					continue;
				}
				const FVector MeshBind = MeshRef.GetRefBonePose()[MeshIndex].GetTranslation();
				const FVector SkeletonPose =
					Original->GetReferenceSkeleton().GetRefBonePose()[SkeletonIndex].GetTranslation();
				const FVector DisplacedSkeletonPose = SkeletonPose + Offset;
				const FVector Today = Before.MeshLocals[MeshIndex].GetTranslation();
				const FVector Control = IdenticalLocals[MeshIndex].GetTranslation();
				const FVector Observed = TranslationLocals[MeshIndex].GetTranslation();
				const double VersusControl = FVector::Distance(Observed, Control);
				const double VersusDisplacedSkeleton =
					FVector::Distance(Observed, DisplacedSkeletonPose);
				const double CloneVersusSaved = FVector::Distance(Control, Today);
				const double RotationMoved = FMath::RadiansToDegrees(
					RotationLocals[MeshIndex].GetRotation().AngularDistance(
						IdenticalLocals[MeshIndex].GetRotation()));
				bResolvesToMesh &= VersusControl <= 0.001;
				bStep1Ran = true;
				AddInfo(FString::Printf(
					TEXT("PROBE3|stage=S1_RESULT|mesh=%s|bone=%s|tracked=0|")
					TEXT("candidate_mesh_bind=%s|candidate_skeleton_pose=%s|")
					TEXT("candidate_skeleton_pose_displaced=%s|saved_skeleton_evaluated=%s|")
					TEXT("identical_clone_evaluated=%s|clone_vs_saved=%.9f|")
					TEXT("observed_with_displaced_skeleton=%s|delta_vs_control=%.9f|")
					TEXT("delta_vs_displaced_skeleton=%.9f|")
					TEXT("positive_control_rotation_clone_moved_deg=%.9f"),
					GSubjects[SubjectIndex].Label, *BoneName.ToString(),
					*VectorText(MeshBind), *VectorText(SkeletonPose),
					*VectorText(DisplacedSkeletonPose), *VectorText(Today),
					*VectorText(Control), CloneVersusSaved,
					*VectorText(Observed), VersusControl, VersusDisplacedSkeleton, RotationMoved));
			}

			IdenticalClone->RemoveFromRoot();
			TranslationClone->RemoveFromRoot();
			RotationClone->RemoveFromRoot();
		}
	}

	AddInfo(FString::Printf(
		TEXT("PROBE3|stage=S1_VERDICT|ran=%d|untracked_resolves_to=%s"),
		bStep1Ran ? 1 : 0,
		bStep1Ran ? (bResolvesToMesh ? TEXT("MESH_REFERENCE_POSE") : TEXT("SKELETON_REFERENCE_POSE"))
			: TEXT("UNKNOWN")));
	if (!bStep1Ran || !bResolvesToMesh)
	{
		AddInfo(TEXT("PROBE3|stage=VERDICT|result=KILLED_AT_STEP1|reason=untracked_bone_follows_skeleton"));
		Finish();
		return true;
	}

	// =============================================================================================
	// STEP 3 -- the canonical reference pose
	// =============================================================================================

	IAssetRegistry& Registry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	Registry.ScanPathsSynchronous({ FString(GSkeletonPath) }, /*bForceRescan=*/false);

	TArray<USkeleton*> AllSkeletons;
	{
		FARFilter Filter;
		Filter.PackagePaths.Add(FName(GSkeletonPath));
		Filter.bRecursivePaths = true;
		Filter.ClassPaths.Add(USkeleton::StaticClass()->GetClassPathName());
		TArray<FAssetData> Assets;
		Registry.GetAssets(Filter, Assets);
		Assets.Sort([](const FAssetData& A, const FAssetData& B)
			{
				return A.GetObjectPathString() < B.GetObjectPathString();
			});
		for (const FAssetData& Asset : Assets)
		{
			if (USkeleton* Loaded = Cast<USkeleton>(Asset.GetAsset()))
			{
				Loaded->AddToRoot();
				AllSkeletons.Add(Loaded);
			}
		}
	}

	// The canonical map. Deterministic by construction: skeletons in sorted object-path order, and
	// the first one to declare a bone NAME owns its canonical local rotation.
	TMap<FName, FQuat> CanonicalRotation;
	int32 CanonicalDisagreements = 0;
	for (const USkeleton* Skeleton : AllSkeletons)
	{
		const FReferenceSkeleton& Ref = Skeleton->GetReferenceSkeleton();
		for (int32 BoneIndex = 0; BoneIndex < Ref.GetRawBoneNum(); ++BoneIndex)
		{
			const FName BoneName = Ref.GetBoneName(BoneIndex);
			const FQuat Local = Ref.GetRefBonePose()[BoneIndex].GetRotation();
			if (const FQuat* Existing = CanonicalRotation.Find(BoneName))
			{
				if (FMath::RadiansToDegrees(Existing->AngularDistance(Local)) > 0.001)
				{
					++CanonicalDisagreements;
				}
			}
			else
			{
				CanonicalRotation.Add(BoneName, Local);
			}
		}
	}
	AddInfo(FString::Printf(
		TEXT("PROBE3|stage=S3_CANON|skeletons_loaded=%d|distinct_bone_names=%d|")
		TEXT("name_rotation_disagreements=%d"),
		AllSkeletons.Num(), CanonicalRotation.Num(), CanonicalDisagreements));

	enum class ECanonicalMode : uint8
	{
		Identical,		// the clone control: same pose, so only "being a clone" is varied
		FlatIdentity,	// every bone's local rotation is identity
		Seeded,			// every bone name takes one shared rotation from the map above
	};

	auto MakeClone = [&](const USkeleton* Original, ECanonicalMode Mode, FString& OutError) -> USkeleton*
	{
		int32 Mismatches = 0;
		USkeleton* Clone = CloneSkeletonWithPose(Original,
			[Mode, &CanonicalRotation](int32, FName Name, const FTransform& Local)
			{
				FTransform Result = Local;
				if (Mode == ECanonicalMode::FlatIdentity)
				{
					Result.SetRotation(FQuat::Identity);
				}
				else if (Mode == ECanonicalMode::Seeded)
				{
					if (const FQuat* Canon = CanonicalRotation.Find(Name))
					{
						Result.SetRotation(*Canon);
					}
				}
				return Result;
			}, Mismatches, OutError);
		if (Clone != nullptr)
		{
			Clone->AddToRoot();
			if (Mismatches > 0)
			{
				AddWarning(FString::Printf(
					TEXT("PROBE3|stage=S3_CLONE|skeleton=%s|bone_order_mismatches=%d"),
					*Original->GetName(), Mismatches));
			}
		}
		return Clone;
	};

	struct FVariant
	{
		const TCHAR* Label;
		ECanonicalMode Mode;
	};
	const FVariant GVariants[] = {
		{ TEXT("clone_identical"), ECanonicalMode::Identical },
		{ TEXT("canonical_flat_identity"), ECanonicalMode::FlatIdentity },
		{ TEXT("canonical_seeded"), ECanonicalMode::Seeded },
	};

	// One clone per variant of the bank skeleton, plus one per variant of every distinct body
	// skeleton in the subject set.
	TMap<const USkeleton*, TArray<USkeleton*>> Clones;
	{
		TArray<const USkeleton*> Originals;
		Originals.Add(BankSkeleton);
		for (USkeletalMesh* Mesh : Meshes)
		{
			Originals.AddUnique(Mesh->GetSkeleton());
		}
		for (const USkeleton* Original : Originals)
		{
			TArray<USkeleton*>& Row = Clones.Add(Original);
			for (const FVariant& Variant : GVariants)
			{
				FString CloneError;
				USkeleton* Clone = MakeClone(Original, Variant.Mode, CloneError);
				if (Clone == nullptr)
				{
					AddError(FString::Printf(
						TEXT("PROBE3|stage=S3_CLONE|skeleton=%s|variant=%s|error=%s"),
						*Original->GetName(), Variant.Label, *CloneError));
					Finish();
					return false;
				}
				Row.Add(Clone);
			}
			AddInfo(FString::Printf(
				TEXT("PROBE3|stage=S3_CLONE|skeleton=%s|bones=%d|variants=%d|")
				TEXT("retarget_sources_copied=%d"),
				*Original->GetName(), Original->GetReferenceSkeleton().GetRawBoneNum(),
				Row.Num(), Row[0]->AnimRetargetSources.Num()));
		}
	}

	// --- the sequences the canonical skeletons need ---------------------------------------------
	FElysiumSkeletalSource Stances;
	FElysiumSkeletalSource Misc;
	FString SourceError;
	if (!FElysiumSkeletalSource::Load(FElysiumContentPaths::NpcBankSource(GStancesStem),
		Stances, SourceError))
	{
		AddError(FString::Printf(TEXT("PROBE3|stage=S3_SRC|error=%s"), *SourceError));
		Finish();
		return false;
	}
	const bool bMiscLoaded = FElysiumSkeletalSource::Load(
		FElysiumContentPaths::NpcBankSource(GMiscStem), Misc, SourceError);
	if (!bMiscLoaded)
	{
		AddWarning(FString::Printf(TEXT("PROBE3|stage=S3_SRC|misc=NOT_PROBED|error=%s"),
			*SourceError));
	}

	const FName StancesSource(GStancesStem);
	const FName MiscSource(GMiscStem);

	// The builder control: the same emission on the REAL bank skeleton must reproduce the baked
	// asset, or nothing measured through a transient sequence means anything.
	{
		int32 Tracks = 0;
		int32 Dropped = 0;
		FString Error;
		UAnimSequence* Control = BuildTransientSequence(Stances, GJoyClip, BankSkeleton,
			StancesSource, Tracks, Dropped, Error);
		if (Control != nullptr)
		{
			Control->AddToRoot();
			FEvalResult ControlResult;
			EvaluateOn(Meshes[0], Control, ControlResult);
			double MaxDelta = 0.0;
			for (int32 Index = 0; Index < ControlResult.Bones.Num()
				&& Index < BaselineJoy[0].Bones.Num(); ++Index)
			{
				MaxDelta = FMath::Max(MaxDelta, FVector::Distance(
					ControlResult.Bones[Index].Final, BaselineJoy[0].Bones[Index].Final));
			}
			AddInfo(FString::Printf(
				TEXT("PROBE3|stage=S3_BUILDER_CONTROL|tracks=%d|dropped=%d|compressed_valid=%d|")
				TEXT("max_transient_vs_baked_final_delta=%.9f"),
				Tracks, Dropped, Control->IsCompressedDataValid() ? 1 : 0, MaxDelta));
			Control->RemoveFromRoot();
		}
		else
		{
			AddWarning(FString::Printf(
				TEXT("PROBE3|stage=S3_BUILDER_CONTROL|status=NOT_PROBED|error=%s"), *Error));
		}
	}

	// =============================================================================================
	// STEP 4 -- end-to-end pose, rotation and deformation, per variant
	// =============================================================================================

	float VariantExactSeam[UE_ARRAY_COUNT(GVariants)] = {};
	int32 VariantAbove5x[UE_ARRAY_COUNT(GVariants)][UE_ARRAY_COUNT(GSubjects)] = {};
	double VariantMaxFinalVersusBind[UE_ARRAY_COUNT(GVariants)][UE_ARRAY_COUNT(GSubjects)] = {};
	double VariantMaxRotationDelta[UE_ARRAY_COUNT(GVariants)][UE_ARRAY_COUNT(GSubjects)] = {};

	for (int32 VariantIndex = 0; VariantIndex < UE_ARRAY_COUNT(GVariants); ++VariantIndex)
	{
		const FVariant& Variant = GVariants[VariantIndex];
		USkeleton* BankClone = Clones[BankSkeleton][VariantIndex];

		int32 Tracks = 0;
		int32 Dropped = 0;
		FString Error;
		UAnimSequence* Joy = BuildTransientSequence(Stances, GJoyClip, BankClone, StancesSource,
			Tracks, Dropped, Error);
		if (Joy == nullptr)
		{
			AddError(FString::Printf(TEXT("PROBE3|stage=S4|variant=%s|error=%s"),
				Variant.Label, *Error));
			continue;
		}
		Joy->AddToRoot();
		AddInfo(FString::Printf(
			TEXT("PROBE3|stage=S4_BUILD|variant=%s|clip=Joy|tracks=%d|dropped=%d|")
			TEXT("compressed_valid=%d"),
			Variant.Label, Tracks, Dropped, Joy->IsCompressedDataValid() ? 1 : 0));

		// Both containers hang off the SAME bank family skeleton, so the misc clip is built on the
		// same clone. The clone already carries the family's registered retarget sources verbatim
		// (`CloneSkeletonWithPose` copies `AnimRetargetSources`); registration below is the fallback
		// for a family that never had one, so the clip is never left naming nothing.
		UAnimSequence* Dance = nullptr;
		if (bMiscLoaded)
		{
			if (BankClone->AnimRetargetSources.Find(MiscSource) == nullptr)
			{
				RegisterRetargetSource(BankClone, MiscSource, Misc);
				AddWarning(FString::Printf(
					TEXT("PROBE3|stage=S4_BUILD|variant=%s|clip=Dance|")
					TEXT("note=misc retarget source was absent and had to be registered"),
					Variant.Label));
			}
			FString MiscError;
			int32 MiscTracks = 0;
			int32 MiscDropped = 0;
			Dance = BuildTransientSequence(Misc, GDanceClip, BankClone, MiscSource,
				MiscTracks, MiscDropped, MiscError);
			if (Dance != nullptr)
			{
				Dance->AddToRoot();
				AddInfo(FString::Printf(
					TEXT("PROBE3|stage=S4_BUILD|variant=%s|clip=Dance|tracks=%d|dropped=%d|")
					TEXT("compressed_valid=%d"),
					Variant.Label, MiscTracks, MiscDropped,
					Dance->IsCompressedDataValid() ? 1 : 0));
			}
			else
			{
				AddWarning(FString::Printf(
					TEXT("PROBE3|stage=S4|variant=%s|clip=Dance|status=NOT_PROBED|error=%s"),
					Variant.Label, *MiscError));
			}
		}

		for (int32 SubjectIndex = 0; SubjectIndex < Meshes.Num(); ++SubjectIndex)
		{
			USkeletalMesh* Mesh = Meshes[SubjectIndex];
			USkeleton* BodyClone = Clones[Mesh->GetSkeleton()][VariantIndex];
			FScopedSkeletonSwap Swap(Mesh, BodyClone);

			FEvalResult JoyResult;
			EvaluateOn(Mesh, Joy, JoyResult);
			LogEval(*this, TEXT("S4"), Variant.Label, GSubjects[SubjectIndex].Label, TEXT("Joy"),
				JoyResult, &BaselineJoy[SubjectIndex]);
			const FSkinMetrics JoyMetrics = SkinAndMeasure(ProbeWorld, Mesh, JoyResult.MeshLocals,
				SubjectIndex == 0);
			LogMetrics(*this, TEXT("S4"), Variant.Label, GSubjects[SubjectIndex].Label, TEXT("Joy"),
				JoyMetrics);
			if (SubjectIndex == 0)
			{
				VariantExactSeam[VariantIndex] = JoyMetrics.ExactRatio;
			}
			VariantAbove5x[VariantIndex][SubjectIndex] = JoyMetrics.Above5x;
			VariantMaxFinalVersusBind[VariantIndex][SubjectIndex] = JoyResult.MaxFinalVersusBind;
			for (int32 Index = 0; Index < JoyResult.Bones.Num()
				&& Index < BaselineJoy[SubjectIndex].Bones.Num(); ++Index)
			{
				VariantMaxRotationDelta[VariantIndex][SubjectIndex] = FMath::Max(
					VariantMaxRotationDelta[VariantIndex][SubjectIndex],
					static_cast<double>(FMath::RadiansToDegrees(
						JoyResult.Bones[Index].FinalRotation.AngularDistance(
							BaselineJoy[SubjectIndex].Bones[Index].FinalRotation))));
			}

			if (Dance != nullptr)
			{
				FEvalResult DanceResult;
				EvaluateOn(Mesh, Dance, DanceResult);
				LogEval(*this, TEXT("S4"), Variant.Label, GSubjects[SubjectIndex].Label,
					TEXT("Dance"), DanceResult, &BaselineDance[SubjectIndex]);
				const FSkinMetrics DanceMetrics = SkinAndMeasure(ProbeWorld, Mesh,
					DanceResult.MeshLocals, SubjectIndex == 0);
				LogMetrics(*this, TEXT("S4"), Variant.Label, GSubjects[SubjectIndex].Label,
					TEXT("Dance"), DanceMetrics);
			}
		}

		Joy->RemoveFromRoot();
		if (Dance != nullptr)
		{
			Dance->RemoveFromRoot();
		}
	}

	// The counterfactual floor, re-measured through this probe's own route.
	{
		TArray<FTransform> Counterfactual = BaselineJoy[0].MeshLocals;
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
			SkinAndMeasure(ProbeWorld, Meshes[0], Counterfactual, true);
		LogMetrics(*this, TEXT("S4_FLOOR"), TEXT("bind_translation_counterfactual"),
			TEXT("female_dancer_2"), TEXT("Joy"), FloorMetrics);
	}

	// =============================================================================================
	// STEP 5 -- what else reads a skeleton's reference pose
	// =============================================================================================

	// (a) sequences whose RetargetSource is None, which fall back to the skeleton's own pose.
	{
		Registry.ScanPathsSynchronous({ FString(GAnimPath) }, /*bForceRescan=*/false);
		FARFilter Filter;
		Filter.PackagePaths.Add(FName(GAnimPath));
		Filter.bRecursivePaths = true;
		Filter.ClassPaths.Add(UAnimSequence::StaticClass()->GetClassPathName());
		TArray<FAssetData> Assets;
		Registry.GetAssets(Filter, Assets);
		Assets.Sort([](const FAssetData& A, const FAssetData& B)
			{
				return A.GetObjectPathString() < B.GetObjectPathString();
			});
		const int32 Total = Assets.Num();
		const int32 Budget = 300;
		const int32 Stride = FMath::Max(1, Total / FMath::Max(1, Budget));
		int32 Sampled = 0;
		int32 NoneSource = 0;
		int32 UnregisteredSource = 0;
		int32 Additives = 0;
		int32 AdditiveRefPoseTypeRefPose = 0;
		int32 AdditiveRefPoseTypeSequence = 0;
		FString FirstNone;
		for (int32 Index = 0; Index < Total; Index += Stride)
		{
			UAnimSequence* Sequence = Cast<UAnimSequence>(Assets[Index].GetAsset());
			if (Sequence == nullptr)
			{
				continue;
			}
			++Sampled;
			const USkeleton* Skeleton = Sequence->GetSkeleton();
			if (Sequence->RetargetSource.IsNone())
			{
				if (NoneSource++ == 0)
				{
					FirstNone = Sequence->GetPathName();
				}
			}
			else if (Skeleton == nullptr
				|| Skeleton->AnimRetargetSources.Find(Sequence->RetargetSource) == nullptr)
			{
				++UnregisteredSource;
			}
			if (Sequence->IsValidAdditive())
			{
				++Additives;
#if WITH_EDITORONLY_DATA
				if (Sequence->RefPoseType == ABPT_RefPose)
				{
					++AdditiveRefPoseTypeRefPose;
				}
				else if (Sequence->RefPoseType == ABPT_AnimScaled
					|| Sequence->RefPoseType == ABPT_AnimFrame
					|| Sequence->RefPoseType == ABPT_LocalAnimFrame)
				{
					++AdditiveRefPoseTypeSequence;
				}
#endif
			}
		}
		AddInfo(FString::Printf(
			TEXT("PROBE3|stage=S5_RETARGET_SOURCE|anim_sequences_total=%d|sampled=%d|stride=%d|")
			TEXT("retarget_source_none=%d|retarget_source_unregistered=%d|first_none=%s|")
			TEXT("additives=%d|additive_base_is_skeleton_refpose=%d|additive_base_is_sequence=%d"),
			Total, Sampled, Stride, NoneSource, UnregisteredSource,
			FirstNone.IsEmpty() ? TEXT("NONE") : *FirstNone,
			Additives, AdditiveRefPoseTypeRefPose, AdditiveRefPoseTypeSequence));
	}

	// (b) an additive evaluated under the canonical pose against today.
	{
		FARFilter Filter;
		Filter.PackagePaths.Add(FName(GAnimPath));
		Filter.bRecursivePaths = true;
		Filter.ClassPaths.Add(UAnimSequence::StaticClass()->GetClassPathName());
		TArray<FAssetData> Assets;
		Registry.GetAssets(Filter, Assets);
		Assets.Sort([](const FAssetData& A, const FAssetData& B)
			{
				return A.GetObjectPathString() < B.GetObjectPathString();
			});
		UAnimSequence* Additive = nullptr;
		for (const FAssetData& Asset : Assets)
		{
			if (!Asset.AssetName.ToString().EndsWith(TEXT("_delta")))
			{
				continue;
			}
			UAnimSequence* Candidate = Cast<UAnimSequence>(Asset.GetAsset());
			if (Candidate != nullptr && Candidate->IsValidAdditive()
				&& (Candidate->GetSkeleton() == BankSkeleton
					|| Candidate->GetSkeleton() == Meshes[0]->GetSkeleton()))
			{
				Additive = Candidate;
				break;
			}
		}
		if (Additive == nullptr)
		{
			AddWarning(TEXT("PROBE3|stage=S5_ADDITIVE|status=NOT_PROBED|reason=no_additive_on_the_subject_pair"));
		}
		else
		{
			Additive->AddToRoot();
			FEvalResult Today;
			EvaluateOn(Meshes[0], Additive, Today);
			auto Against = [&](int32 VariantIndex, const TCHAR* Label)
			{
				FEvalResult Swapped;
				{
					FScopedSkeletonSwap Swap(Meshes[0],
						Clones[Meshes[0]->GetSkeleton()][VariantIndex]);
					EvaluateOn(Meshes[0], Additive, Swapped);
				}
				double MaxTranslation = 0.0;
				double MaxRotation = 0.0;
				for (int32 Index = 0; Index < Today.MeshLocals.Num()
					&& Index < Swapped.MeshLocals.Num(); ++Index)
				{
					MaxTranslation = FMath::Max(MaxTranslation, FVector::Distance(
						Today.MeshLocals[Index].GetTranslation(),
						Swapped.MeshLocals[Index].GetTranslation()));
					MaxRotation = FMath::Max(MaxRotation,
						static_cast<double>(FMath::RadiansToDegrees(
							Today.MeshLocals[Index].GetRotation().AngularDistance(
								Swapped.MeshLocals[Index].GetRotation()))));
				}
				AddInfo(FString::Printf(
					TEXT("PROBE3|stage=S5_ADDITIVE|sequence=%s|additive_type=%d|target_variant=%s|")
					TEXT("note=the SEQUENCE still names the real bank skeleton, so this varies the ")
					TEXT("TARGET end alone|max_translation_delta=%.9f|max_rotation_delta_deg=%.9f"),
					*Additive->GetPathName(), static_cast<int32>(Additive->AdditiveAnimType),
					Label, MaxTranslation, MaxRotation));
			};
			Against(0, TEXT("clone_identical"));
			Against(1, TEXT("canonical_flat_identity"));
			Additive->RemoveFromRoot();
		}
	}

	// (c) blend profiles / layer masks, virtual bones, sockets, curve metadata, and load-time
	//     mesh compatibility -- read across every baked skeleton rather than the four subjects.
	{
		int32 TotalProfiles = 0;
		int32 TotalProfileEntries = 0;
		int32 TotalVirtualBones = 0;
		int32 TotalSockets = 0;
		int32 TotalCurves = 0;
		for (const USkeleton* Skeleton : AllSkeletons)
		{
			TotalVirtualBones += Skeleton->GetReferenceSkeleton().GetVirtualBoneRefData().Num();
			TotalSockets += Skeleton->Sockets.Num();
			TotalProfiles += Skeleton->BlendProfiles.Num();
			for (const TObjectPtr<UBlendProfile>& Profile : Skeleton->BlendProfiles)
			{
				if (Profile != nullptr)
				{
					TotalProfileEntries += Profile->GetNumBlendEntries();
				}
			}
			int32 Curves = 0;
			Skeleton->ForEachCurveMetaData([&Curves](FName, const FCurveMetaData&)
				{
					++Curves;
				});
			TotalCurves += Curves;
		}
		AddInfo(FString::Printf(
			TEXT("PROBE3|stage=S5_CONSUMERS|skeletons=%d|virtual_bones=%d|sockets=%d|")
			TEXT("blend_profiles=%d|blend_profile_entries=%d|curve_metadata=%d"),
			AllSkeletons.Num(), TotalVirtualBones, TotalSockets, TotalProfiles,
			TotalProfileEntries, TotalCurves));

		for (int32 SubjectIndex = 0; SubjectIndex < Meshes.Num(); ++SubjectIndex)
		{
			USkeletalMesh* Mesh = Meshes[SubjectIndex];
			const USkeleton* Flat = Clones[Mesh->GetSkeleton()][1];
			const USkeleton* Seeded = Clones[Mesh->GetSkeleton()][2];
			AddInfo(FString::Printf(
				TEXT("PROBE3|stage=S5_COMPAT|mesh=%s|original_is_compatible=%d|")
				TEXT("flat_identity_is_compatible=%d|seeded_is_compatible=%d"),
				GSubjects[SubjectIndex].Label,
				Mesh->GetSkeleton()->IsCompatibleMesh(Mesh, true) ? 1 : 0,
				Flat->IsCompatibleMesh(Mesh, true) ? 1 : 0,
				Seeded->IsCompatibleMesh(Mesh, true) ? 1 : 0));
		}
	}

	// (d) the bake's own RegisterRetargetSource seeds undeclared bones from the skeleton's pose.
	{
		const FReferenceSkeleton& BankRef = BankSkeleton->GetReferenceSkeleton();
		TSet<FName> Declared;
		for (const FElysiumSourceBone& Bone : Stances.Bones)
		{
			Declared.Add(Bone.Name);
		}
		TSet<FName> TrackedByAnyClip;
		for (const FElysiumSourceClip& Clip : Stances.Clips)
		{
			for (const FElysiumSourceTrack& Track : Clip.Tracks)
			{
				if (Stances.Bones.IsValidIndex(Track.Bone))
				{
					TrackedByAnyClip.Add(Stances.Bones[Track.Bone].Name);
				}
			}
		}
		int32 Undeclared = 0;
		int32 UndeclaredAndTracked = 0;
		for (int32 BoneIndex = 0; BoneIndex < BankRef.GetRawBoneNum(); ++BoneIndex)
		{
			const FName BoneName = BankRef.GetBoneName(BoneIndex);
			if (!Declared.Contains(BoneName))
			{
				++Undeclared;
				if (TrackedByAnyClip.Contains(BoneName))
				{
					++UndeclaredAndTracked;
				}
			}
		}
		AddInfo(FString::Printf(
			TEXT("PROBE3|stage=S5_RETARGET_SEED|bank_bones=%d|container_bones=%d|")
			TEXT("bones_seeded_from_skeleton_pose=%d|of_those_tracked_by_a_clip=%d"),
			BankRef.GetRawBoneNum(), Stances.Bones.Num(), Undeclared, UndeclaredAndTracked));
	}

	// =============================================================================================
	// verdict inputs
	// =============================================================================================

	AddInfo(FString::Printf(
		TEXT("PROBE3|stage=VERDICT_INPUTS|baseline_clean=%d|baseline_exact_seam=%.9f|floor=1.081774116|")
		TEXT("clone_identical_seam=%.9f|flat_identity_seam=%.9f|seeded_seam=%.9f|")
		TEXT("above_5x_baseline=(%d,%d,%d,%d)|above_5x_flat=(%d,%d,%d,%d)|")
		TEXT("above_5x_seeded=(%d,%d,%d,%d)|")
		TEXT("max_final_vs_bind_flat=(%.9f,%.9f,%.9f,%.9f)|")
		TEXT("max_rotation_delta_flat_deg=(%.6f,%.6f,%.6f,%.6f)|")
		TEXT("max_rotation_delta_seeded_deg=(%.6f,%.6f,%.6f,%.6f)|gpu=NOT_PROBED"),
		bBaselineClean ? 1 : 0, BaselineJoyMetrics[0].ExactRatio,
		VariantExactSeam[0], VariantExactSeam[1], VariantExactSeam[2],
		BaselineJoyMetrics[0].Above5x, BaselineJoyMetrics[1].Above5x,
		BaselineJoyMetrics[2].Above5x, BaselineJoyMetrics[3].Above5x,
		VariantAbove5x[1][0], VariantAbove5x[1][1], VariantAbove5x[1][2], VariantAbove5x[1][3],
		VariantAbove5x[2][0], VariantAbove5x[2][1], VariantAbove5x[2][2], VariantAbove5x[2][3],
		VariantMaxFinalVersusBind[1][0], VariantMaxFinalVersusBind[1][1],
		VariantMaxFinalVersusBind[1][2], VariantMaxFinalVersusBind[1][3],
		VariantMaxRotationDelta[1][0], VariantMaxRotationDelta[1][1],
		VariantMaxRotationDelta[1][2], VariantMaxRotationDelta[1][3],
		VariantMaxRotationDelta[2][0], VariantMaxRotationDelta[2][1],
		VariantMaxRotationDelta[2][2], VariantMaxRotationDelta[2][3]));

	for (const TPair<const USkeleton*, TArray<USkeleton*>>& Row : Clones)
	{
		for (USkeleton* Clone : Row.Value)
		{
			Clone->RemoveFromRoot();
		}
	}
	for (USkeleton* Skeleton : AllSkeletons)
	{
		Skeleton->RemoveFromRoot();
	}
	Finish();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
