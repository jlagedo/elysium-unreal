// The evaluated pose against the pose retail drew.
//
// `uv run elysium research analyze_rig_pose` turns a `life_rig_pose` session into
// `pose_oracle.json`: for every sampled frame, the bone-to-world matrices
// `C_BaseAnimating::SetupBones` left in the entity's own array, beside the global sequence number
// and cycle the body was playing. This evaluates the clip that number names, at that cycle, on
// that body's own baked mesh, and compares the two poses.
//
// **The comparison never converts a matrix.** Retail's frame is Source-space and ours is
// Unreal-native, and the basis change between them contains a reflection, so a rotation compared
// across it is a sign bug waiting to read as a finding (the trap `animation_rig_resolution.md`
// records having already produced one false divergence). What is compared instead is a set of
// quantities every isometry preserves: distances between bones, and the angle a joint holds. Those
// are the pose itself, in centimetres and degrees, and they are blind only to the placement being
// divided out anyway.
//
// The clip a frame played is found by its **global sequence number**, which is retail's own
// identity for a clip and which the export now carries per row -- so this test is also what holds
// that numbering to the running game.

#include "Misc/AutomationTest.h"

#include "Tests/ElysiumNativeCharacterTestData.h"
#include "ElysiumContentPaths.h"
#include "Visual/ElysiumNpcClips.h"
#include "Visual/ElysiumNpcVisual.h"

#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "BonePose.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

static constexpr EAutomationTestFlags GElysiumRigPoseFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	//: Retail states its world in Source units; the bake states centimetres. The factor is the
	//: one `npc_export` converts every stated distance by, and T3 confirmed it against a captured
	//: matrix whose translation matched an exported bind length to five decimal places.
	constexpr double GPoseSourceUnitToCm = 2.54;

	//: A matrix3x4_t is three rows of four: a basis row, then that row's translation component.
	constexpr int32 GPoseMatrixFloats = 12;

	// How many frames one body contributes. The session holds well over a thousand per body and
	// they are dominated by held poses, so the whole set would spend minutes re-measuring one
	// answer; the cap is spread across the set rather than taken from the front, so cycle
	// coverage survives it.
	constexpr int32 GFramesPerStem = 240;

	// EVERY captured pose session, oldest first, because one session poses whichever bodies the
	// owner happened to play. A body binding a bone in retail's origin band is excluded from the
	// parity aggregates, so a run reading only the newest session can hold nothing to parity at
	// all -- and a newest-session rule would make the suite's verdict depend on which capture
	// ran last. `ELYSIUM_POSE_ORACLE` still names one report when a run wants exactly one.
	TArray<FString> FindPoseOracles()
	{
		TArray<FString> Out;
		const FString Named = FPlatformMisc::GetEnvironmentVariable(TEXT("ELYSIUM_POSE_ORACLE"));
		if (!Named.IsEmpty())
		{
			Out.Add(Named);
			return Out;
		}
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
			const FString Candidate = Frida / Session / TEXT("pose_oracle.json");
			if (IFileManager::Get().FileExists(*Candidate))
			{
				Out.Add(Candidate);
			}
		}
		return Out;
	}

	struct FPoseFrame
	{
		FString Stem;
		FString RootBone;
		int32 Sequence = INDEX_NONE;
		double Cycle = 0.0;
		int32 Contributions = 0;
		TMap<FString, FVector> BonePositions;
	};

	bool ReadPoseOracle(const FString& Path, TArray<FPoseFrame>& OutFrames, FString& OutError)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			OutError = FString::Printf(TEXT("cannot read %s"), *Path);
			return false;
		}
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutError = FString::Printf(TEXT("%s is not a JSON object"), *Path);
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>* Frames = nullptr;
		if (!Root->TryGetArrayField(TEXT("frames"), Frames))
		{
			OutError = FString::Printf(TEXT("%s carries no `frames` array"), *Path);
			return false;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Frames)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(Object))
			{
				continue;
			}
			bool bIsPlayer = false;
			(*Object)->TryGetBoolField(TEXT("is_player"), bIsPlayer);
			const TSharedPtr<FJsonObject>* State = nullptr;
			if (!bIsPlayer || !(*Object)->TryGetObjectField(TEXT("player_state"), State))
			{
				continue;   // only a body whose own sequence and cycle were read can be rebuilt
			}
			FPoseFrame Frame;
			(*Object)->TryGetStringField(TEXT("stem"), Frame.Stem);
			(*Object)->TryGetStringField(TEXT("root_bone"), Frame.RootBone);
			(*State)->TryGetNumberField(TEXT("sequence"), Frame.Sequence);
			(*State)->TryGetNumberField(TEXT("cycle"), Frame.Cycle);
			const TArray<TSharedPtr<FJsonValue>>* Contributions = nullptr;
			if ((*Object)->TryGetArrayField(TEXT("contributions"), Contributions))
			{
				Frame.Contributions = Contributions->Num();
			}
			const TSharedPtr<FJsonObject>* Bones = nullptr;
			if (!(*Object)->TryGetObjectField(TEXT("bones"), Bones))
			{
				continue;
			}
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Bone : (*Bones)->Values)
			{
				const TArray<TSharedPtr<FJsonValue>>* Row = nullptr;
				if (!Bone.Value.IsValid() || !Bone.Value->TryGetArray(Row)
					|| Row->Num() < GPoseMatrixFloats)
				{
					continue;
				}
				// Only the translation column is read. The basis rows would need the reflection
				// this comparison exists to avoid.
				Frame.BonePositions.Add(Bone.Key, FVector(
					(*Row)[3]->AsNumber(), (*Row)[7]->AsNumber(), (*Row)[11]->AsNumber()));
			}
			if (Frame.Sequence != INDEX_NONE && !Frame.BonePositions.IsEmpty()
				&& !Frame.Stem.IsEmpty() && !Frame.RootBone.IsEmpty())
			{
				OutFrames.Add(MoveTemp(Frame));
			}
		}
		return true;
	}

	// The clip a global sequence number names, in the body's own vocabulary. This is retail's own
	// identity for a clip, and the only reason it can be answered offline is that the export
	// carries the number per row.
	const FElysiumNpcClip* PoseClipForSequence(const FElysiumNpcClipSet& Set, int32 Sequence,
		FString& OutLabel)
	{
		const FElysiumNpcClip* Found = nullptr;
		Set.Clips.ForEachClip([&Found, &OutLabel, Sequence]
			(const FString& Label, const FElysiumNpcClip& Clip)
		{
			if (Found == nullptr && Clip.RawIndex == Sequence)
			{
				Found = &Clip;
				OutLabel = Label;
			}
		});
		return Found;
	}

	bool EvaluatePoseComponentSpace(const UAnimSequence* Sequence, const USkeletalMesh* Mesh,
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

		// A bone the clip does not track holds the playing mesh's own reference pose, which is what
		// the runtime shows and what retail's undriven bones hold too.
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

		// The reference skeleton states every parent before its children, so a parent is composed
		// by the time its child is reached.
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

	// A running distribution, reported rather than asserted until the numbers say what a threshold
	// should be.
	struct FPoseDistribution
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

		int32 Count() const { return Samples.Num(); }
	};
}

namespace
{
	// Retail's translate-instead-of-retarget branch fires on a bone whose own bind sits at the
	// origin, and the corpus brackets its epsilon between the largest separation retail copied
	// (0.0831 in) and the smallest it transformed (0.1267 in). Unreal's own guard is
	// `IsNearlyZero(SourceLen * TargetLen)`, three orders of magnitude tighter, so a bone whose
	// bind lands between the two is translated by one engine and retargeted by the other.
	//
	// A body carrying such a bone is measured against the recorded defect rather than against
	// parity, because the pelvis is a translation bone and every bone below and above it
	// inherits the error. The set is derived from the playing mesh's own reference skeleton, so
	// a body that acquires or loses the property is classified by its bind rather than by a
	// name this file would have to be edited to keep true.
	constexpr double GOriginBandFloorCm = 0.001;   // below this Unreal declines too, and they agree
	constexpr double GOriginBandCeilingCm = 0.254; // 0.1 in, the middle of the bracketed epsilon

	// The bones this mesh binds inside the band, by name.
	TSet<FString> OriginBandBones(const USkeletalMesh* Mesh)
	{
		TSet<FString> Out;
		if (Mesh == nullptr)
		{
			return Out;
		}
		const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
		const TArray<FTransform>& Pose = Ref.GetRefBonePose();
		for (int32 Index = 0; Index < Ref.GetNum(); ++Index)
		{
			// The root carries the actor transform rather than a bind offset, so it is not a
			// candidate however short it reads.
			if (Ref.GetParentIndex(Index) == INDEX_NONE || !Pose.IsValidIndex(Index))
			{
				continue;
			}
			const double Length = Pose[Index].GetLocation().Size();
			if (Length > GOriginBandFloorCm && Length < GOriginBandCeilingCm)
			{
				Out.Add(Ref.GetBoneName(Index).ToString());
			}
		}
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumRigPoseTest,
	"Elysium.Content.RigPose", GElysiumRigPoseFlags)
bool FElysiumRigPoseTest::RunTest(const FString&)
{
	if (!ElysiumNativeTest::HasCast())
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no native character cast (run: uv run elysium import characters)"));
		return true;
	}
	const TArray<FString> OraclePaths = FindPoseOracles();
	if (OraclePaths.IsEmpty())
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no pose_oracle.json under $ELYSIUM_WORK_ROOT/research/frida "
			"(capture with frida_probe --recipe life_rig_pose, then run analyze_rig_pose)"));
		return true;
	}
	TArray<FPoseFrame> Frames;
	FString Error;
	for (const FString& OraclePath : OraclePaths)
	{
		if (!ReadPoseOracle(OraclePath, Frames, Error))
		{
			AddError(Error);   // present but unreadable is a failure, not an abstention
			return false;
		}
	}
	const FString OraclePath = FString::Join(OraclePaths, TEXT(", "));
	if (Frames.IsEmpty())
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: the oracle carries no player frame with a sequence"));
		return true;
	}
	FElysiumNpcIndex NpcIndex;
	if (!ElysiumNativeTest::Load(NpcIndex, Error) || !NpcIndex.IsValid())
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no native cast view (run: uv run elysium import characters)"));
		return true;
	}

	// Spread the sample across each body's frames rather than taking a prefix: the session holds
	// long stretches of one held pose, and a prefix would measure the first of them repeatedly.
	TMap<FString, int32> FramesPerStem;
	for (const FPoseFrame& Frame : Frames)
	{
		FramesPerStem.FindOrAdd(Frame.Stem)++;
	}
	TMap<FString, int32> Seen;

	TMap<FString, TSharedPtr<FElysiumNpcClipSet>> ClipSets;
	TMap<FString, USkeletalMesh*> Meshes;
	TMap<FString, UAnimSequence*> Clips;

	FPoseDistribution RadiusError;         // |bone - root| against retail's, in cm
	FPoseDistribution SegmentError;        // parent-to-child bone length, in cm
	FPoseDistribution JointAngleError;     // the angle a joint holds, in degrees
	FPoseDistribution PelvisRadiusError;
	// Split on whether anything layered over the base clip that frame. A base pose is the whole
	// drawn pose only when nothing accumulated onto it; where something did, this evaluates one
	// clip against a composition and the difference is the layer rather than the clip.
	FPoseDistribution BaseOnlyRadius;
	FPoseDistribution LayeredRadius;
	FPoseDistribution BaseOnlyAngle;
	FPoseDistribution LayeredAngle;
	// The upper body is what an aim or gesture layer moves, and the lower body is what it does
	// not, so the two answer differently when a layer is the explanation.
	FPoseDistribution LowerBodyRadius;
	FPoseDistribution UpperBodyRadius;
	TMap<FString, FPoseDistribution> ByBone;
	TMap<FString, FPoseDistribution> ByStem;
	// Bodies binding a non-root bone inside retail's origin band, and their divergence. Kept
	// out of every parity aggregate and asserted against the recorded envelope instead.
	FPoseDistribution OriginBandRadius;
	TSet<FString> OriginBandStems;
	TMap<FString, TSet<FString>> OriginBandByStem;
	int32 Evaluated = 0;
	int32 NoClipMap = 0;
	int32 NoSequence = 0;
	int32 NoAsset = 0;
	int32 NoMesh = 0;
	TSet<FString> MissingAssets;

	for (const FPoseFrame& Frame : Frames)
	{
		const int32 Total = FramesPerStem[Frame.Stem];
		const int32 Stride = FMath::Max(1, Total / GFramesPerStem);
		if (Seen.FindOrAdd(Frame.Stem)++ % Stride != 0)
		{
			continue;
		}

		TSharedPtr<FElysiumNpcClipSet>& Set = ClipSets.FindOrAdd(Frame.Stem);
		if (!Set.IsValid())
		{
			Set = MakeShared<FElysiumNpcClipSet>();
			FString LoadError;
			if (!ElysiumNativeTest::Load(*Set, Frame.Stem, LoadError))
			{
				Set.Reset();
			}
		}
		if (!Set.IsValid())
		{
			++NoClipMap;
			continue;
		}
		FString Label;
		const FElysiumNpcClip* Clip = PoseClipForSequence(*Set, Frame.Sequence, Label);
		if (Clip == nullptr)
		{
			++NoSequence;
			continue;
		}

		USkeletalMesh*& Mesh = Meshes.FindOrAdd(Frame.Stem);
		if (Mesh == nullptr)
		{
			Mesh = ElysiumNpcVisual::LoadBakedMesh(Frame.Stem);
		}
		if (Mesh == nullptr)
		{
			++NoMesh;
			continue;
		}
		const FString AssetKey = Clip->Owner / Label;
		UAnimSequence*& Sequence = Clips.FindOrAdd(AssetKey);
		if (Sequence == nullptr)
		{
			Sequence = ElysiumNpcVisual::LoadBakedClip(Mesh, Clip->Owner, Label);
		}
		if (Sequence == nullptr)
		{
			++NoAsset;
			MissingAssets.Add(AssetKey);
			continue;
		}

		TArray<FTransform> Component;
		const double Time = FMath::Clamp(Frame.Cycle, 0.0, 1.0)
			* static_cast<double>(Sequence->GetPlayLength());
		if (!EvaluatePoseComponentSpace(Sequence, Mesh, Time, Component))
		{
			++NoAsset;
			continue;
		}
		++Evaluated;

		const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
		// Derived per mesh, cached per stem: whether this body binds a non-root bone inside
		// retail's origin band, which is what decides whether it is held to parity.
		const TSet<FString>* Band = OriginBandByStem.Find(Frame.Stem);
		if (Band == nullptr)
		{
			Band = &OriginBandByStem.Add(Frame.Stem, OriginBandBones(Mesh));
		}
		const bool bOriginBandBody = Band->Num() > 0;
		const int32 RootIndex = Ref.FindBoneIndex(FName(*Frame.RootBone));
		const FVector* RetailRoot = Frame.BonePositions.Find(Frame.RootBone);
		if (RootIndex == INDEX_NONE || RetailRoot == nullptr)
		{
			continue;
		}
		const FVector OurRoot = Component[RootIndex].GetLocation();

		for (const TPair<FString, FVector>& Bone : Frame.BonePositions)
		{
			const int32 BoneIndex = Ref.FindBoneIndex(FName(*Bone.Key));
			if (BoneIndex == INDEX_NONE || !Component.IsValidIndex(BoneIndex))
			{
				continue;
			}
			// Radius from the root. A distance is preserved by every isometry, so it survives the
			// basis change between the two frames without a conversion being chosen.
			const double Retail = (Bone.Value - *RetailRoot).Size() * GPoseSourceUnitToCm;
			const double Ours = (Component[BoneIndex].GetLocation() - OurRoot).Size();
			const double Delta = FMath::Abs(Retail - Ours);
			RadiusError.Add(Delta);
			ByStem.FindOrAdd(Frame.Stem).Add(Delta);
			if (bOriginBandBody)
			{
				// Recorded, not asserted for parity: this body's pelvis takes retail's translation
				// branch and our retarget scales it instead, and every bone inherits the offset.
				OriginBandRadius.Add(Delta);
				OriginBandStems.Add(Frame.Stem);
				continue;
			}
			ByBone.FindOrAdd(Bone.Key).Add(Delta);
			(Frame.Contributions == 0 ? BaseOnlyRadius : LayeredRadius).Add(Delta);
			// `Bip01 Spine` and everything under it is the half a layer reaches; the pelvis and the
			// legs hang off the root beside it.
			const bool bUpper = Bone.Key.StartsWith(TEXT("Bip01 Spine"), ESearchCase::IgnoreCase)
				|| Bone.Key.Contains(TEXT("Neck"), ESearchCase::IgnoreCase)
				|| Bone.Key.Contains(TEXT("Head"), ESearchCase::IgnoreCase)
				|| Bone.Key.Contains(TEXT("Clavicle"), ESearchCase::IgnoreCase)
				|| Bone.Key.Contains(TEXT("UpperArm"), ESearchCase::IgnoreCase)
				|| Bone.Key.Contains(TEXT("Forearm"), ESearchCase::IgnoreCase)
				|| Bone.Key.Contains(TEXT("Hand"), ESearchCase::IgnoreCase)
				|| Bone.Key.Contains(TEXT("Finger"), ESearchCase::IgnoreCase);
			const bool bLower = Bone.Key.Contains(TEXT("Thigh"), ESearchCase::IgnoreCase)
				|| Bone.Key.Contains(TEXT("Calf"), ESearchCase::IgnoreCase)
				|| Bone.Key.Contains(TEXT("Foot"), ESearchCase::IgnoreCase)
				|| Bone.Key.Contains(TEXT("Toe"), ESearchCase::IgnoreCase)
				|| Bone.Key.Equals(TEXT("Bip01 Pelvis"), ESearchCase::IgnoreCase);
			if (bUpper) { UpperBodyRadius.Add(Delta); }
			else if (bLower) { LowerBodyRadius.Add(Delta); }
			if (Bone.Key.Equals(TEXT("Bip01 Pelvis"), ESearchCase::IgnoreCase))
			{
				PelvisRadiusError.Add(Delta);
			}

			// The segment to this bone's parent, and the angle the joint holds between that
			// segment and the one continuing to the parent's own parent. Both are isometry
			// invariants, and together they are the pose.
			const int32 ParentIndex = Ref.GetParentIndex(BoneIndex);
			if (ParentIndex == INDEX_NONE)
			{
				continue;
			}
			const FName ParentName = Ref.GetBoneName(ParentIndex);
			const FVector* RetailParent = Frame.BonePositions.Find(ParentName.ToString());
			if (RetailParent == nullptr)
			{
				continue;
			}
			const FVector RetailSegment = (Bone.Value - *RetailParent) * GPoseSourceUnitToCm;
			const FVector OurSegment =
				Component[BoneIndex].GetLocation() - Component[ParentIndex].GetLocation();
			SegmentError.Add(FMath::Abs(RetailSegment.Size() - OurSegment.Size()));

			const int32 GrandIndex = Ref.GetParentIndex(ParentIndex);
			if (GrandIndex == INDEX_NONE)
			{
				continue;
			}
			const FName GrandName = Ref.GetBoneName(GrandIndex);
			const FVector* RetailGrand = Frame.BonePositions.Find(GrandName.ToString());
			if (RetailGrand == nullptr)
			{
				continue;
			}
			const FVector RetailUpper = (*RetailParent - *RetailGrand);
			const FVector OurUpper =
				Component[ParentIndex].GetLocation() - Component[GrandIndex].GetLocation();
			if (RetailSegment.IsNearlyZero() || OurSegment.IsNearlyZero()
				|| RetailUpper.IsNearlyZero() || OurUpper.IsNearlyZero())
			{
				continue;
			}
			const double RetailAngle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
				FVector::DotProduct(RetailSegment.GetSafeNormal(), RetailUpper.GetSafeNormal()),
				-1.0, 1.0)));
			const double OurAngle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
				FVector::DotProduct(OurSegment.GetSafeNormal(), OurUpper.GetSafeNormal()),
				-1.0, 1.0)));
			const double AngleDelta = FMath::Abs(RetailAngle - OurAngle);
			JointAngleError.Add(AngleDelta);
			(Frame.Contributions == 0 ? BaseOnlyAngle : LayeredAngle).Add(AngleDelta);
		}
	}

	AddInfo(FString::Printf(TEXT("oracle %s"), *OraclePath));
	AddInfo(FString::Printf(
		TEXT("frames %d evaluated; skipped: %d no clip map, %d sequence not in the vocabulary, "
		     "%d no baked asset, %d no baked mesh"),
		Evaluated, NoClipMap, NoSequence, NoAsset, NoMesh));
	if (Evaluated == 0)
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no captured frame could be evaluated against the mount"));
		return true;
	}
	AddInfo(FString::Printf(TEXT("radius from the root:   %s"), *RadiusError.Describe(TEXT(" cm"))));
	AddInfo(FString::Printf(TEXT("bone segment length:    %s"), *SegmentError.Describe(TEXT(" cm"))));
	AddInfo(FString::Printf(TEXT("joint angle:            %s"), *JointAngleError.Describe(TEXT(" deg"))));
	AddInfo(FString::Printf(TEXT("Bip01 Pelvis radius:    %s"),
		*PelvisRadiusError.Describe(TEXT(" cm"))));
	AddInfo(FString::Printf(TEXT("  base clip alone drawn: %s"),
		*BaseOnlyRadius.Describe(TEXT(" cm"))));
	AddInfo(FString::Printf(TEXT("  something layered on:  %s"),
		*LayeredRadius.Describe(TEXT(" cm"))));
	AddInfo(FString::Printf(TEXT("  base clip alone angle: %s"),
		*BaseOnlyAngle.Describe(TEXT(" deg"))));
	AddInfo(FString::Printf(TEXT("  layered angle:         %s"),
		*LayeredAngle.Describe(TEXT(" deg"))));
	AddInfo(FString::Printf(TEXT("  lower body radius:     %s"),
		*LowerBodyRadius.Describe(TEXT(" cm"))));
	AddInfo(FString::Printf(TEXT("  upper body radius:     %s"),
		*UpperBodyRadius.Describe(TEXT(" cm"))));
	for (TPair<FString, FPoseDistribution>& Stem : ByStem)
	{
		AddInfo(FString::Printf(TEXT("  %s: %s"), *Stem.Key, *Stem.Value.Describe(TEXT(" cm"))));
	}

	// The worst bones by median, which is what names a systematic divergence rather than a frame
	// that happened to be mid-transition.
	TArray<TPair<FString, double>> WorstBones;
	for (TPair<FString, FPoseDistribution>& Bone : ByBone)
	{
		WorstBones.Add({Bone.Key, Bone.Value.Median()});
	}
	WorstBones.Sort([](const TPair<FString, double>& A, const TPair<FString, double>& B)
	{
		return A.Value > B.Value;
	});
	FString Worst;
	for (int32 Index = 0; Index < FMath::Min(12, WorstBones.Num()); ++Index)
	{
		Worst += FString::Printf(TEXT("\n    %-28s median %.3f cm"),
			*WorstBones[Index].Key, WorstBones[Index].Value);
	}
	AddInfo(FString::Printf(TEXT("worst bones by median radius error:%s"), *Worst));

	// --- what this is allowed to assert -----------------------------------------------------
	//
	// A frame nothing layered onto is the whole drawn pose, and there the two agree to a median
	// of a hundredth of a millimetre: the clip the sequence number named, the cycle, the baked
	// asset, the compatible-skeleton path and the retarget are all right. A frame something
	// layered onto is a composition this evaluates one clip against, so its divergence is the
	// layer rather than a defect -- which is why the split is measured rather than the whole.
	//
	// The medians are what carry the assertion. The tails do not: `contributions` is a clock join
	// over a target the sampler does not keep, so "nothing layered on" is a floor on what layered
	// rather than a proof that nothing did, and the frames where a layer went unrecorded sit in
	// the base-only tail. A per-bone median is asserted beside them, because a whole-set median
	// would not move for a single bone going wrong -- which is the shape the known pelvis
	// divergence has.
	//
	// A body binding a bone inside retail's origin band is not held to parity at all. Its
	// pelvis is a translation bone whose error every other bone inherits, so excluding the one
	// bone would leave the body's whole pose displaced and the aggregate meaningless. It is
	// held to the recorded envelope below instead, which is a ceiling rather than a target: a
	// graph-level translation rule carrying retail's four outcomes drives it to zero downstream and
	// this direct-sequence exposure still passes.
	constexpr double MedianToleranceCm = 0.5;
	constexpr double MedianToleranceDegrees = 0.5;
	if (BaseOnlyRadius.Count() == 0 && OriginBandRadius.Count() > 0)
	{
		// Every body the session posed carries the defect, so there is nothing left to hold to
		// parity and a pass here would prove only that the exclusion works.
		AddInfo(FString::Printf(
			TEXT("ELYSIUM_TEST_ABSTAIN: every posed body binds a bone in retail's origin band (%s); ")
			TEXT("capture a body outside it to assert parity"),
			*FString::Join(OriginBandStems.Array(), TEXT(", "))));
		return true;
	}
	TestTrue(FString::Printf(
		TEXT("a pose with nothing layered on matches retail's (median %.4f cm)"),
		BaseOnlyRadius.Median()), BaseOnlyRadius.Median() <= MedianToleranceCm);
	TestTrue(FString::Printf(
		TEXT("...and holds its joints at retail's angles (median %.4f deg)"),
		BaseOnlyAngle.Median()), BaseOnlyAngle.Median() <= MedianToleranceDegrees);
	// A layer moves a bone, never the length of one, so this holds over every frame.
	TestTrue(FString::Printf(TEXT("bone lengths are retail's (median %.4f cm)"),
		SegmentError.Median()), SegmentError.Median() <= MedianToleranceCm);
	// The lower body is the half no aim or gesture layer reaches, so it is comparable whatever
	// was layered on -- and it is where the pelvis sits.
	TestTrue(FString::Printf(TEXT("the lower body matches whatever layered on (median %.4f cm)"),
		LowerBodyRadius.Median()), LowerBodyRadius.Median() <= MedianToleranceCm);

	constexpr double PerBoneToleranceCm = 1.0;
	for (TPair<FString, FPoseDistribution>& Bone : ByBone)
	{
		const bool bLowerBody = Bone.Key.Contains(TEXT("Thigh"), ESearchCase::IgnoreCase)
			|| Bone.Key.Contains(TEXT("Calf"), ESearchCase::IgnoreCase)
			|| Bone.Key.Contains(TEXT("Foot"), ESearchCase::IgnoreCase)
			|| Bone.Key.Contains(TEXT("Toe"), ESearchCase::IgnoreCase)
			|| Bone.Key.Equals(TEXT("Bip01 Pelvis"), ESearchCase::IgnoreCase);
		if (!bLowerBody)
		{
			continue;
		}
		TestTrue(FString::Printf(TEXT("'%s' sits where retail put it (median %.4f cm)"),
			*Bone.Key, Bone.Value.Median()), Bone.Value.Median() <= PerBoneToleranceCm);
	}
	// The recorded defect. `Bip01 Pelvis` binds 0.14496 cm from its parent on an affected body
	// against the male banks' 2.96802, so retail writes a constant `b - a` of 3.11281 cm where
	// `OrientAndScale` scales the bank's own translation by 0.04884 -- measured at a median of
	// 4.652 cm of pelvis displacement, 4.725 cm across the body, on `tremere_male_armor_3`.
	// The ceiling is twice that, so the known defect passes and a regression that doubles it
	// does not.
	constexpr double OriginBandCeilingCm = 10.0;
	if (OriginBandRadius.Count() > 0)
	{
		TArray<FString> Stems = OriginBandStems.Array();
		Stems.Sort();
		AddInfo(FString::Printf(
			TEXT("origin-band bodies (retail translates, we retarget): %s -- %s"),
			*FString::Join(Stems, TEXT(", ")), *OriginBandRadius.Describe(TEXT(" cm"))));
		TestTrue(FString::Printf(
			TEXT("the origin-band defect stays within its recorded envelope (median %.4f cm)"),
			OriginBandRadius.Median()), OriginBandRadius.Median() <= OriginBandCeilingCm);
	}
	if (!MissingAssets.IsEmpty())
	{
		TArray<FString> Names = MissingAssets.Array();
		Names.Sort();
		AddInfo(FString::Printf(TEXT("clips the mount does not carry (%d): %s"),
			Names.Num(), *FString::Join(Names, TEXT(", "))));
	}
	return true;
}
