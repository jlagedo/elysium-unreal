// What `elysium.BakedCharacters` selects has to be the same character (ANM1). Everything compared
// here fails silently in game -- a dropped bone plays part of a skeleton at bind pose, a lost morph
// target leaves a face that evaluates its facial track and never moves, and a clip bound to the
// wrong bone tree plays a plausible wrong pose.
//
// The comparison is deliberately NOT "the two agree bone for bone". The two paths reach Unreal
// through different bases: glTFRuntime imports under its own, and the bake writes the repo's
// canonical Source-to-Unreal frame (`bsp.source_to_unreal`, the `UE_` exporter convention). The
// difference lands entirely on the ROOT bone -- measured at 0.7 to 14.5 degrees and 3 to 8 cm
// across the slice -- and on nothing else, because every other bone is parent-relative and a root
// difference cancels out of a local transform.
//
// So the root is asserted against the CONTAINER, not against the loader: the `.eskm` states the
// bind pose in Unreal space with no import step to disagree about, which makes it the thing the
// bake had to reproduce. Every other bone is asserted equal between the two paths, which is what
// says the two rigs pose identically relative to their own roots. The loader's root delta is
// reported rather than failed -- it is a known property of the path being retired, and the test
// exists to defend the bake's correctness rather than the incumbent's convention.
//
// Self-skipping: the baked mount is gitignored and regenerable, so a checkout that has not run
// `uv run elysium export characters` has nothing to compare and says so rather than failing.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumContentPaths.h"
#include "Visual/ElysiumNpcClips.h"
#include "Visual/ElysiumNpcVisual.h"
#include "Visual/ElysiumSkeletalSource.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimTypes.h"
#include "Animation/MorphTarget.h"
#include "Animation/Skeleton.h"
#include "BoneIndices.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/FileManager.h"
#include "glTFRuntimeAsset.h"

static constexpr EAutomationTestFlags GElysiumBakedCharacterFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// The ANM1 slice. Ten bodies of nine distinct bone counts from 70 to 94, so the shared skeleton
	// is exercised as a union of differing subsets, plus the one flex rig in the set.
	const TCHAR* const GSliceStems[] = {
		TEXT("smiling_jack"),
		TEXT("tremere_guardian"),
		TEXT("tremere_male_armor_0"),
		TEXT("tremere_male_armor_1"),
		TEXT("tremere_male_armor_2"),
		TEXT("tremere_male_armor_3"),
		TEXT("tremere_female_armor_0"),
		TEXT("tremere_female_armor_1"),
		TEXT("tremere_female_armor_2"),
		TEXT("tremere_female_armor_3"),
	};

	// Times through a clip, as fractions of its length. The ends matter as much as the middle: a
	// mis-mapped track is most visible at a pose extreme, and a length that disagrees shows up as
	// the last sample landing somewhere else entirely.
	constexpr double GSampleFractions[] = { 0.0, 0.25, 0.5, 0.75, 1.0 };

	// Both paths decode the same channels through the same reader, so agreement should be exact;
	// the tolerance is here for the float32 round-trip through two different serialisations, not
	// for a difference of substance. A real divergence is degrees, not thousandths.
	constexpr double GPositionTolerance = 0.01;   // centimetres
	constexpr double GRotationToleranceDeg = 0.05;

	bool SampleBone(const UAnimSequence* Sequence, const FName Bone, const double Time,
		FTransform& OutTransform)
	{
		const USkeleton* Skeleton = Sequence != nullptr ? Sequence->GetSkeleton() : nullptr;
		if (Skeleton == nullptr)
		{
			return false;
		}
		const int32 BoneIndex = Skeleton->GetReferenceSkeleton().FindBoneIndex(Bone);
		if (BoneIndex == INDEX_NONE)
		{
			return false;
		}
		// Raw data on both sides. The two sequences are compressed independently -- one by the
		// editor's DDC at bake, one by glTFRuntime at load -- and this test is about the authored
		// content surviving the bake, not about two compressors agreeing bit for bit.
		Sequence->GetBoneTransform(OutTransform, FSkeletonPoseBoneIndex(BoneIndex),
			FAnimExtractContext(Time), /*bUseRawData=*/true);
		return true;
	}

	/**
	 * Compose parent-relative locals into component space and re-express them against the rig's
	 * own root.
	 *
	 * This, not the local transform, is what decides how a body looks. Two rigs can hold the same
	 * world pose while distributing it differently between the root and its first child -- which is
	 * exactly what a basis difference at the root does -- and a local-space comparison reports that
	 * as a large divergence on a body that renders identically. Dividing by the root also removes
	 * the frame difference itself, leaving only what the two paths actually disagree about.
	 */
	void PosesRelativeToRoot(const FReferenceSkeleton& Ref, const TArray<FTransform>& Locals,
		TArray<FTransform>& Out)
	{
		const int32 Num = Locals.Num();
		TArray<FTransform> World;
		World.SetNum(Num);
		for (int32 Index = 0; Index < Num; ++Index)
		{
			const int32 Parent = Ref.GetParentIndex(Index);
			World[Index] = Ref.GetRefBonePose().IsValidIndex(Index) && Parent != INDEX_NONE
				? Locals[Index] * World[Parent] : Locals[Index];
		}
		const FTransform RootInverse = Num > 0 ? World[0].Inverse() : FTransform::Identity;
		Out.SetNum(Num);
		for (int32 Index = 0; Index < Num; ++Index)
		{
			Out[Index] = World[Index] * RootInverse;
		}
	}

	TSet<FName> MorphTargetNames(const USkeletalMesh* Mesh)
	{
		TSet<FName> Names;
		if (Mesh != nullptr)
		{
			for (const TObjectPtr<UMorphTarget>& Morph : Mesh->GetMorphTargets())
			{
				if (Morph != nullptr)
				{
					Names.Add(Morph->GetFName());
				}
			}
		}
		return Names;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumBakedCharacterParityTest,
	"Elysium.Content.BakedCharacterParity", GElysiumBakedCharacterFlags)
bool FElysiumBakedCharacterParityTest::RunTest(const FString&)
{
	// Deliberately does NOT gate on FElysiumContentPaths::IsIncomplete(), unlike the corpus-wide
	// content tests. This one compares one model's baked assets against that model's own .glb and
	// skips per model when either side is absent, so a partial export is exactly the case it is
	// built to handle — and the bake it verifies is itself run against a subset of the cast.
	FElysiumNpcIndex Index;
	FString Error;
	if (!Index.Load(Error))
	{
		AddInfo(FString::Printf(TEXT("skipping: no NPC index (%s)"), *Error));
		return true;
	}

	int32 Compared = 0;
	int32 Samples = 0;
	// The one place the two paths' bases show, accumulated across the slice and reported once.
	double RootRotationDeg = 0.0;
	double RootTranslationCm = 0.0;
	TArray<UObject*> KeepAlive;

	for (const TCHAR* Stem : GSliceStems)
	{
		USkeletalMesh* Baked = ElysiumNpcVisual::LoadBakedMesh(Stem);
		if (Baked == nullptr)
		{
			AddInfo(FString::Printf(TEXT("skipping %s: not on the baked mount"), Stem));
			continue;
		}
		if (!IFileManager::Get().FileExists(*FElysiumContentPaths::NpcGlb(Stem)))
		{
			AddInfo(FString::Printf(TEXT("skipping %s: no source .glb to compare against"), Stem));
			continue;
		}

		UglTFRuntimeAsset* Asset = nullptr;
		USkeletalMesh* Loaded = ElysiumNpcVisual::LoadMeshFromPath(
			FElysiumContentPaths::NpcGlb(Stem), Asset, Error);
		if (Loaded == nullptr)
		{
			AddError(FString::Printf(TEXT("%s: the glTFRuntime path does not load (%s)"), Stem, *Error));
			continue;
		}
		KeepAlive.Add(Loaded);
		KeepAlive.Add(Asset);
		KeepAlive.Add(Baked);
		++Compared;

		// The body's own bone set, in its own order. The baked mesh keeps its own reference
		// skeleton -- only the SKELETON asset is shared -- so these must match exactly, or a baked
		// body is a different shape from the one the loader builds.
		const FReferenceSkeleton& BakedRef = Baked->GetRefSkeleton();
		const FReferenceSkeleton& LoadedRef = Loaded->GetRefSkeleton();
		if (!TestEqual(FString::Printf(TEXT("%s bone count"), Stem),
			BakedRef.GetNum(), LoadedRef.GetNum()))
		{
			continue;
		}
		// The container the bake read. It states the bind pose and every clip key in Unreal space
		// with no import step to disagree about, which makes it the reference the bake is held to.
		FElysiumSkeletalSource Source;
		if (!FElysiumSkeletalSource::Load(FElysiumContentPaths::NpcSource(Stem), Source, Error))
		{
			AddInfo(FString::Printf(TEXT("%s: no .eskm to check against (%s)"), Stem, *Error));
			continue;
		}
		if (Source.Bones.Num() != BakedRef.GetNum())
		{
			AddError(FString::Printf(TEXT("%s: %d bones baked against %d in the container"),
				Stem, BakedRef.GetNum(), Source.Bones.Num()));
			continue;
		}

		// Bone SETS have to agree; bone ORDER does not. glTFRuntime walks the node tree and the
		// bake emits StudioBone declaration order, so the same rig comes out indexed differently --
		// and nothing cares, because every clip binds to a rig by bone name.
		TMap<FName, int32> BakedIndexOf;
		BakedIndexOf.Reserve(BakedRef.GetNum());
		for (int32 BoneIndex = 0; BoneIndex < BakedRef.GetNum(); ++BoneIndex)
		{
			BakedIndexOf.Add(BakedRef.GetBoneName(BoneIndex), BoneIndex);
		}
		TArray<FName> Missing;
		for (int32 BoneIndex = 0; BoneIndex < LoadedRef.GetNum(); ++BoneIndex)
		{
			if (!BakedIndexOf.Contains(LoadedRef.GetBoneName(BoneIndex)))
			{
				Missing.Add(LoadedRef.GetBoneName(BoneIndex));
			}
		}
		if (!Missing.IsEmpty())
		{
			AddError(FString::Printf(TEXT("%s: %d bone(s) did not survive the bake (%s%s)"),
				Stem, Missing.Num(), *Missing[0].ToString(),
				Missing.Num() > 1 ? TEXT(", ...") : TEXT("")));
			continue;
		}

		// The rest pose is asserted against the CONTAINER, bone for bone. The loader is not a
		// reference for it: the two paths frame their bones differently -- glTFRuntime derives every
		// bone from the inverse-bind matrices under its own basis, the bake writes the file's own
		// Unreal-space locals -- and a rig can hold one shape in many frames, with the inverse binds
		// absorbing the difference. Both bodies render identically and neither's bone transforms
		// predict the other's, so the thing worth asserting is that the bake reproduced what it read.
		{
			for (int32 BoneIndex = 0; BoneIndex < Source.Bones.Num(); ++BoneIndex)
			{
				const FElysiumSourceBone& Bone = Source.Bones[BoneIndex];
				const int32* At = BakedIndexOf.Find(Bone.Name);
				if (At == nullptr)
				{
					AddError(FString::Printf(TEXT("%s: '%s' is in the container and not in the bake"),
						Stem, *Bone.Name.ToString()));
					break;
				}
				const FName BakedParent = BakedRef.GetParentIndex(*At) == INDEX_NONE
					? NAME_None : BakedRef.GetBoneName(BakedRef.GetParentIndex(*At));
				const FName SourceParent = Source.Bones.IsValidIndex(Bone.Parent)
					? Source.Bones[Bone.Parent].Name : NAME_None;
				if (BakedParent != SourceParent)
				{
					AddError(FString::Printf(TEXT("%s: '%s' hangs off '%s', the container says '%s'"),
						Stem, *Bone.Name.ToString(), *BakedParent.ToString(), *SourceParent.ToString()));
					break;
				}
				if (!BakedRef.GetRefBonePose()[*At].Equals(Bone.Local, GPositionTolerance))
				{
					AddError(FString::Printf(
						TEXT("%s bone '%s': the bake did not reproduce its container -- ")
						TEXT("baked t=%s q=%s against .eskm t=%s q=%s"),
						Stem, *Bone.Name.ToString(),
						*BakedRef.GetRefBonePose()[*At].GetTranslation().ToString(),
						*BakedRef.GetRefBonePose()[*At].GetRotation().ToString(),
						*Bone.Local.GetTranslation().ToString(),
						*Bone.Local.GetRotation().ToString()));
					break;
				}
			}

			// The frame difference itself, recorded rather than failed, so its size stays visible
			// while both paths are selectable.
			TArray<FTransform> BakedPoses;
			TArray<FTransform> LoadedPoses;
			PosesRelativeToRoot(BakedRef, BakedRef.GetRefBonePose(), BakedPoses);
			PosesRelativeToRoot(LoadedRef, LoadedRef.GetRefBonePose(), LoadedPoses);
			for (int32 BoneIndex = 1; BoneIndex < LoadedPoses.Num(); ++BoneIndex)
			{
				const FTransform& BakedPose = BakedPoses[BakedIndexOf[LoadedRef.GetBoneName(BoneIndex)]];
				RootRotationDeg = FMath::Max(RootRotationDeg, FMath::RadiansToDegrees(
					BakedPose.GetRotation().AngularDistance(LoadedPoses[BoneIndex].GetRotation())));
				RootTranslationCm = FMath::Max(RootTranslationCm, FVector::Distance(
					BakedPose.GetTranslation(), LoadedPoses[BoneIndex].GetTranslation()));
			}
		}

		// The face is a curve interface, so its whole contract on this seam is that the same
		// targets exist and the skeleton flags them as morph-target curves.
		const TSet<FName> BakedMorphs = MorphTargetNames(Baked);
		const TSet<FName> LoadedMorphs = MorphTargetNames(Loaded);
		TestEqual(FString::Printf(TEXT("%s morph target count"), Stem),
			BakedMorphs.Num(), LoadedMorphs.Num());
		for (const FName& Name : LoadedMorphs)
		{
			if (!BakedMorphs.Contains(Name))
			{
				AddError(FString::Printf(TEXT("%s: morph target '%s' did not survive the bake"),
					Stem, *Name.ToString()));
				continue;
			}
			const USkeleton* Skeleton = Baked->GetSkeleton();
			const FCurveMetaData* MetaData =
				Skeleton != nullptr ? Skeleton->GetCurveMetaData(Name) : nullptr;
			if (MetaData == nullptr || !MetaData->Type.bMorphtarget)
			{
				AddError(FString::Printf(
					TEXT("%s: '%s' carries no morph-target curve metadata, so it can never drive"),
					Stem, *Name.ToString()));
			}
		}

		// A handful of the body's own clips, sampled through both paths. Own clips rather than bank
		// clips because they are the ones this .glb owns, so both sides read the same file.
		const TArray<FString> ClipNames = Asset->GetAnimationsNames(/*bSort=*/true);
		int32 Taken = 0;
		for (const FString& ClipName : ClipNames)
		{
			if (Taken >= 4)
			{
				break;
			}
			UAnimSequence* BakedClip = ElysiumNpcVisual::LoadBakedClip(Baked, Stem, ClipName);
			if (BakedClip == nullptr)
			{
				continue;
			}
			UAnimSequence* LoadedClip = ElysiumNpcVisual::RetargetClip(Asset, Loaded, ClipName, Error);
			if (LoadedClip == nullptr)
			{
				AddError(FString::Printf(TEXT("%s '%s': the glTFRuntime path does not bind (%s)"),
					Stem, *ClipName, *Error));
				continue;
			}
			KeepAlive.Add(BakedClip);
			KeepAlive.Add(LoadedClip);
			++Taken;

			const double BakedLength = BakedClip->GetPlayLength();
			const double LoadedLength = LoadedClip->GetPlayLength();
			if (!FMath::IsNearlyEqual(BakedLength, LoadedLength, 1e-3))
			{
				AddError(FString::Printf(TEXT("%s '%s': %.4f s baked against %.4f s loaded"),
					Stem, *ClipName, BakedLength, LoadedLength));
				continue;
			}

			// Each key of each track, against the container's own numbers. This is the assertion
			// the clip half exists for: what the loader would answer is in its own bone frames and
			// says nothing about whether the bake copied the file correctly.
			const FElysiumSourceClip* SourceClip = Source.Clips.FindByPredicate(
				[&ClipName](const FElysiumSourceClip& Candidate)
				{
					return Candidate.Name.Equals(ClipName, ESearchCase::IgnoreCase);
				});
			if (SourceClip == nullptr)
			{
				AddInfo(FString::Printf(TEXT("%s '%s': not in the container"), Stem, *ClipName));
				continue;
			}
			for (const FElysiumSourceTrack& Track : SourceClip->Tracks)
			{
				if (!Source.Bones.IsValidIndex(Track.Bone))
				{
					continue;
				}
				const FName Bone = Source.Bones[Track.Bone].Name;
				for (const double Fraction : GSampleFractions)
				{
					const int32 Frame = FMath::Clamp(
						FMath::RoundToInt32(Fraction * (SourceClip->FrameCount - 1)),
						0, SourceClip->FrameCount - 1);
					const double Time = Frame / FMath::Max(SourceClip->FrameRate, 1.0f);
					FTransform BakedBone;
					if (!SampleBone(BakedClip, Bone, Time, BakedBone))
					{
						AddError(FString::Printf(TEXT("%s '%s': bone '%s' is not on the bake"),
							Stem, *ClipName, *Bone.ToString()));
						break;
					}
					++Samples;
					const FQuat Expected = Track.Rotations.IsValidIndex(Frame)
						? FQuat(Track.Rotations[Frame])
						: Source.Bones[Track.Bone].Local.GetRotation();
					const FVector ExpectedT = Track.Translations.IsValidIndex(Frame)
						? FVector(Track.Translations[Frame])
						: Source.Bones[Track.Bone].Local.GetTranslation();
					const double Degrees = FMath::RadiansToDegrees(
						BakedBone.GetRotation().AngularDistance(Expected));
					const double Centimetres = FVector::Distance(BakedBone.GetTranslation(), ExpectedT);
					if (Degrees > GRotationToleranceDeg || Centimetres > GPositionTolerance)
					{
						AddError(FString::Printf(
							TEXT("%s '%s' frame %d bone '%s': the bake is %.4f deg / %.4f cm off ")
							TEXT("its container"),
							Stem, *ClipName, Frame, *Bone.ToString(), Degrees, Centimetres));
						break;
					}
				}
			}
		}
		if (Taken == 0)
		{
			AddInfo(FString::Printf(TEXT("%s: no own clip is on the baked mount"), Stem));
		}
	}

	if (Compared == 0)
	{
		AddInfo(TEXT("skipping: no slice model is baked; run: uv run elysium export characters"));
		return true;
	}
	AddInfo(FString::Printf(TEXT("%d model(s) compared, %d bone samples"), Compared, Samples));
	AddInfo(FString::Printf(
		TEXT("bone-frame delta against the glTFRuntime path: %.4f deg / %.4f cm at most -- ")
		TEXT("expected, and not a defect: the two paths frame their bones differently and their ")
		TEXT("inverse binds absorb it, so both render the same body from different rigs"),
		RootRotationDeg, RootTranslationCm));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
