#pragma once

// What every test comparing an evaluated pose against a captured retail pose needs, in one place.
//
// Two of them exist: `Elysium.Content.RigPose` scores one clip on a frame nothing layered onto,
// and `Elysium.Content.RigCompose` scores a composed overlay on a frame something did. They read
// the same capture, evaluate through the same engine path and describe their error the same way,
// so the evaluation lives here rather than once per test — two copies of a pose evaluation drift
// silently, and a divergence between them would read as a finding about the bake.

#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "BonePose.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"

namespace ElysiumPoseOracle
{
	//: Retail states its world in Source units; the bake states centimetres. The factor is the one
	//: `npc_export` converts every stated distance by, and T3 confirmed it against a captured
	//: matrix whose translation matched an exported bind length to five decimal places.
	inline constexpr double SourceUnitToCm = 2.54;

	//: A matrix3x4_t is three rows of four: a basis row, then that row's translation component.
	inline constexpr int32 MatrixFloats = 12;

	// Every captured `life_rig_pose` session directory, oldest first. One session poses whichever
	// bodies the owner happened to play, so a test reading only the newest would make its verdict
	// depend on which capture ran last.
	inline TArray<FString> FindSessions()
	{
		TArray<FString> Out;
		const FString WorkRoot = FPlatformMisc::GetEnvironmentVariable(TEXT("ELYSIUM_WORK_ROOT"));
		if (WorkRoot.IsEmpty())
		{
			return Out;
		}
		const FString Frida = WorkRoot / TEXT("research") / TEXT("frida");
		TArray<FString> Sessions;
		IFileManager::Get().FindFiles(Sessions, *(Frida / TEXT("*-life_rig_pose")), false, true);
		Sessions.Sort();
		for (const FString& Session : Sessions)
		{
			Out.Add(Frida / Session);
		}
		return Out;
	}

	// Component-space transforms from parent-relative ones. The reference skeleton states every
	// parent before its children, so a parent is composed by the time its child is reached.
	inline void LocalToComponent(const FReferenceSkeleton& Ref, const TArray<FTransform>& Locals,
		TArray<FTransform>& OutComponent)
	{
		OutComponent.SetNum(Locals.Num());
		for (int32 Index = 0; Index < Locals.Num(); ++Index)
		{
			const int32 Parent = Ref.GetParentIndex(Index);
			OutComponent[Index] = Parent == INDEX_NONE
				? Locals[Index]
				: Locals[Index] * OutComponent[Parent];
		}
	}

	// The pose a sequence evaluates to, **parent-relative**, on the playing mesh's own skeleton.
	//
	// Local rather than component because a composition is performed in local space: a masked
	// overlay replaces the bones its mask owns and leaves the rest to the base, and doing that on
	// component transforms would leave every owned bone carrying the BASE's parent chain baked into
	// it. That is the difference between "the layer composed onto this gait" and "the layer's own
	// pose relocated onto this gait", and only the first is what retail draws.
	inline bool EvaluateLocalSpace(const UAnimSequence* Sequence, const USkeletalMesh* Mesh,
		double Time, TArray<FTransform>& OutLocals)
	{
		if (Sequence == nullptr || Mesh == nullptr || Mesh->GetSkeleton() == nullptr)
		{
			return false;
		}
		const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
#if WITH_EDITOR
		const_cast<UAnimSequence*>(Sequence)->WaitOnExistingCompression();
#endif
		TArray<FBoneIndexType> RequiredBones;
		RequiredBones.SetNumUninitialized(Ref.GetNum());
		for (int32 Bone = 0; Bone < RequiredBones.Num(); ++Bone)
		{
			RequiredBones[Bone] = static_cast<FBoneIndexType>(Bone);
		}

		FMemMark Mark(FMemStack::Get());
		FBoneContainer Container;
		Container.InitializeTo(RequiredBones,
			UE::Anim::FCurveFilterSettings(UE::Anim::ECurveFilterMode::None), *Mesh);
		FCompactPose Pose;
		Pose.SetBoneContainer(&Container);
		FBlendedCurve Curve;
		Curve.InitFrom(Container);
		UE::Anim::FStackAttributeContainer Attributes;
		FAnimationPoseData PoseData(Pose, Curve, Attributes);
		Sequence->GetAnimationPose(PoseData, FAnimExtractContext(Time));

		// A bone the clip does not track holds the playing mesh's own reference pose, which is what
		// the runtime shows and what retail's undriven bones hold too.
		OutLocals = Ref.GetRefBonePose();
		for (const FCompactPoseBoneIndex BoneIndex : Pose.ForEachBoneIndex())
		{
			const FSkeletonPoseBoneIndex SkeletonIndex =
				Container.GetSkeletonPoseIndexFromCompactPoseIndex(BoneIndex);
			if (!SkeletonIndex.IsValid())
			{
				continue;
			}
			const FMeshPoseBoneIndex MeshIndex =
				Container.GetMeshPoseIndexFromSkeletonPoseIndex(SkeletonIndex);
			if (MeshIndex.IsValid() && OutLocals.IsValidIndex(MeshIndex.GetInt()))
			{
				OutLocals[MeshIndex.GetInt()] = Pose[BoneIndex];
			}
		}
		return true;
	}

	// The pose a sequence evaluates to, in component space, on the playing mesh's own skeleton.
	//
	// A bone the clip does not track holds the mesh's reference pose, which is what the runtime
	// shows and what retail's undriven bones hold too.
	inline bool EvaluateComponentSpace(const UAnimSequence* Sequence, const USkeletalMesh* Mesh,
		double Time, TArray<FTransform>& OutComponent)
	{
		if (Sequence == nullptr || Mesh == nullptr || Mesh->GetSkeleton() == nullptr)
		{
			return false;
		}
		const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
#if WITH_EDITOR
		// Pin the path: an unresolved compression job falls back to the raw data model, which never
		// runs the compatible-skeleton remap a bank clip on a body's mesh depends on.
		const_cast<UAnimSequence*>(Sequence)->WaitOnExistingCompression();
#endif
		TArray<FBoneIndexType> RequiredBones;
		RequiredBones.SetNumUninitialized(Ref.GetNum());
		for (int32 Bone = 0; Bone < RequiredBones.Num(); ++Bone)
		{
			RequiredBones[Bone] = static_cast<FBoneIndexType>(Bone);
		}

		FMemMark Mark(FMemStack::Get());
		FBoneContainer Container;
		Container.InitializeTo(RequiredBones,
			UE::Anim::FCurveFilterSettings(UE::Anim::ECurveFilterMode::None), *Mesh);
		FCompactPose Pose;
		Pose.SetBoneContainer(&Container);
		FBlendedCurve Curve;
		Curve.InitFrom(Container);
		UE::Anim::FStackAttributeContainer Attributes;
		FAnimationPoseData PoseData(Pose, Curve, Attributes);
		Sequence->GetAnimationPose(PoseData, FAnimExtractContext(Time));

		TArray<FTransform> Locals = Ref.GetRefBonePose();
		for (const FCompactPoseBoneIndex BoneIndex : Pose.ForEachBoneIndex())
		{
			const FSkeletonPoseBoneIndex SkeletonIndex =
				Container.GetSkeletonPoseIndexFromCompactPoseIndex(BoneIndex);
			if (!SkeletonIndex.IsValid())
			{
				continue;
			}
			const FMeshPoseBoneIndex MeshIndex =
				Container.GetMeshPoseIndexFromSkeletonPoseIndex(SkeletonIndex);
			if (MeshIndex.IsValid() && Locals.IsValidIndex(MeshIndex.GetInt()))
			{
				Locals[MeshIndex.GetInt()] = Pose[BoneIndex];
			}
		}

		// The reference skeleton states every parent before its children, so a parent is composed by
		// the time its child is reached.
		OutComponent.SetNum(Locals.Num());
		for (int32 Index = 0; Index < Locals.Num(); ++Index)
		{
			const int32 Parent = Ref.GetParentIndex(Index);
			OutComponent[Index] = Parent == INDEX_NONE
				? Locals[Index]
				: Locals[Index] * OutComponent[Parent];
		}
		return true;
	}

	// A running distribution, described rather than asserted until the numbers say what a threshold
	// should be.
	struct FDistribution
	{
		TArray<double> Samples;

		void Add(double Value) { Samples.Add(Value); }

		FString Describe(const TCHAR* Unit)
		{
			if (Samples.IsEmpty())
			{
				return TEXT("no samples");
			}
			Samples.Sort();
			const auto At = [this](double Fraction)
			{
				const int32 Index = FMath::Clamp(
					FMath::RoundToInt(Fraction * (Samples.Num() - 1)), 0, Samples.Num() - 1);
				return Samples[Index];
			};
			return FString::Printf(
				TEXT("n=%d  median %.3f%s  p90 %.3f%s  p99 %.3f%s  max %.3f%s"),
				Samples.Num(), At(0.5), Unit, At(0.9), Unit, At(0.99), Unit, At(1.0), Unit);
		}

		double Median()
		{
			if (Samples.IsEmpty()) { return 0.0; }
			Samples.Sort();
			return Samples[Samples.Num() / 2];
		}

		double Mean() const
		{
			if (Samples.IsEmpty()) { return 0.0; }
			double Sum = 0.0;
			for (const double Sample : Samples) { Sum += Sample; }
			return Sum / Samples.Num();
		}

		// The population deviation of the recorded set, not an estimate of a wider one it was drawn
		// from: the samples ARE the frames, so there is no population beyond them to correct for.
		double StdDev() const
		{
			if (Samples.Num() < 2) { return 0.0; }
			const double Average = Mean();
			double Sum = 0.0;
			for (const double Sample : Samples) { Sum += FMath::Square(Sample - Average); }
			return FMath::Sqrt(Sum / Samples.Num());
		}

		// The full excursion of the set. A scalar whose defect is that it SWINGS -- a limb that
		// should hold its station through a cycle and does not -- is read here and never off a
		// median, which an error constant across the cycle leaves untouched.
		double PeakToPeak() const
		{
			if (Samples.IsEmpty()) { return 0.0; }
			double Low = Samples[0];
			double High = Samples[0];
			for (const double Sample : Samples)
			{
				Low = FMath::Min(Low, Sample);
				High = FMath::Max(High, Sample);
			}
			return High - Low;
		}

		int32 Count() const { return Samples.Num(); }
	};
}
