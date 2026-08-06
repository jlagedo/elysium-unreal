// What `elysium.BakedCharacters` selects has to be the same character (ANM1). Everything compared
// here fails silently in game -- a dropped bone plays part of a skeleton at bind pose, a lost morph
// target leaves a face that evaluates its facial track and never moves, and a clip bound to the
// wrong bone tree plays a plausible wrong pose.
//
// THE CLIP ASSERTION IS COMPOSED, NOT PER KEY. Both sides are driven through the bone hierarchy
// and compared in component space, so an error at any bone arrives amplified at every bone below
// it -- which is what makes a dropped track, a re-parented bone or a mis-bound name land as a
// large, obvious failure instead of a subtle one. Composition is ordinary inheritance, because
// that is what the runtime applies to a baked clip and what the container's rotations are written
// for: `UE_mdl_skeletal.py` rewrites the one bone carrying VtMB's split-inheritance convention
// (StudioBone flag 0x2) at export, so no VtMB-specific rule survives into the asset.
//
// WHAT THIS TEST CANNOT ANSWER, and where that is answered instead. Both sides read the same
// `.eskm`, so this is transport and structure: it says the bake reproduced the container over the
// whole rig, not that the container is faithful to VtMB. The normalisation itself is verified
// against retail's own `.mdl` -- the only artefact still holding the un-normalised rotations --
// which is an offline check by construction, because the install cannot be committed
// (bring-your-own-game). A `_delta` clip is skipped here for the same reason it is skipped at
// export: it states a difference rather than a pose, so composing it answers nothing.
//
// THE MESH ASSERTION IS DELIBERATELY NOT "the two paths agree bone for bone". They reach Unreal
// through different bases: glTFRuntime derives every bone from the inverse-bind matrices under its
// own basis, and the bake writes the file's own locals in the repo's canonical Source-to-Unreal
// frame (`bsp.source_to_unreal`, the `UE_` exporter convention). A rig can hold one shape in many
// frames with the inverse binds absorbing the difference, so the two disagree about individual bone
// transforms -- by whole degrees and centimetres, printed at the end of the run -- while drawing the
// same body. Neither one's bone transforms predict the other's.
//
// So the rest pose is asserted against the CONTAINER, not against the loader: the `.eskm` states
// the bind pose in Unreal space with no import step to disagree about, which makes it the thing the
// bake had to reproduce. The loader is held to the bone SET and the morph SET, which are basis-free
// and are what says the two paths build the same rig. The bind-frame delta between them is
// reported rather than failed -- a property of the path being retired, kept visible while both
// remain selectable.
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
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "glTFRuntimeAsset.h"

static constexpr EAutomationTestFlags GElysiumBakedCharacterFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// The parity slice. Two bodies, deliberately small: this test composes every bone of every
	// sampled frame of every clip it takes, so breadth costs real time and one body of each shape
	// answers what a wider slice answers. `smiling_jack` SEEDS its rig family, so its own bone tree
	// becomes the union every other male body merges into; `tremere_male_armor_0` merges INTO that
	// union, which is the case where a bone can be lost. Neither carries a cloth mesh, so neither
	// trips the cloth exclusion in `IsStemBaked`.
	const TCHAR* const GDefaultSliceStems[] = {
		TEXT("smiling_jack"),
		TEXT("tremere_male_armor_0"),
	};

	// `-ElysiumParityStems=a,b,c` widens the slice for a one-off run without touching this file.
	TArray<FString> SliceStems()
	{
		FString Raw;
		if (FParse::Value(FCommandLine::Get(), TEXT("ElysiumParityStems="), Raw) && !Raw.IsEmpty())
		{
			TArray<FString> Stems;
			Raw.ParseIntoArray(Stems, TEXT(","), /*InCullEmpty=*/true);
			for (FString& Stem : Stems)
			{
				Stem.TrimStartAndEndInline();
			}
			if (!Stems.IsEmpty())
			{
				return Stems;
			}
		}
		TArray<FString> Stems;
		for (const TCHAR* Stem : GDefaultSliceStems)
		{
			Stems.Add(Stem);
		}
		return Stems;
	}

	// How many of a body's own clips to compose. smiling_jack owns 38 dialogue clips of up to 571
	// frames, all the same shape as each other, so a handful covers what the rest would.
	constexpr int32 GMaxClipsPerModel = 6;

	// Times through a clip, as fractions of its length. The ends matter as much as the middle: a
	// mis-mapped track is most visible at a pose extreme, and a length that disagrees shows up as
	// the last sample landing somewhere else entirely.
	constexpr double GSampleFractions[] = { 0.0, 0.25, 0.5, 0.75, 1.0 };

	// Bind poses and single transforms, where nothing has accumulated yet.
	constexpr double GPositionTolerance = 0.01;   // centimetres

	// Composed component space, where a rotation error arrives multiplied by the lever arm below
	// it -- 0.05 deg at the pelvis is already ~0.09 cm out at the hand. Both sides carry the same
	// float32 keys through two serialisations, so genuine agreement sits near 1e-4; these are set
	// an order of magnitude above that and remain two orders below any real defect, which shows up
	// in whole degrees and centimetres.
	constexpr double GComposedRotationToleranceDeg = 0.05;
	constexpr double GComposedTranslationTolerance = 0.05;   // centimetres

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
	 * Compose parent-relative locals into component space by ordinary inheritance -- the rule the
	 * runtime applies to a baked clip, and the rule the container's rotations are written for.
	 *
	 * The container states every parent before its children, so a parent is always composed already.
	 */
	void ComposeComponentSpace(const TArray<FElysiumSourceBone>& Bones,
		const TArray<FTransform>& Locals, TArray<FTransform>& Out)
	{
		Out.SetNum(Bones.Num());
		for (int32 Index = 0; Index < Bones.Num(); ++Index)
		{
			const int32 Parent = Bones[Index].Parent;
			Out[Index] = Parent >= 0 && Parent < Index
				? Locals[Index] * Out[Parent]
				: Locals[Index];
		}
	}

	/**
	 * Re-express a composed pose against the rig's own root.
	 *
	 * Two rigs can hold the same world pose while distributing it differently between the root and
	 * its first child -- which is exactly what a basis difference at the root does -- and a raw
	 * comparison reports that as a large divergence on a body that renders identically.
	 */
	void RelativeToRoot(const FReferenceSkeleton& Ref, const TArray<FTransform>& Locals,
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
	// content tests. This one compares one model's baked assets against that model's own container
	// and skips per model when either side is absent, so a partial export is exactly the case it is
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
	// How far apart the two paths frame the same bone, accumulated across the slice and reported
	// once. Taken relative to each rig's own root, so it is what remains after the basis difference
	// at the root is divided out.
	double BindRotationDeg = 0.0;
	double BindTranslationCm = 0.0;
	TArray<UObject*> KeepAlive;

	for (const FString& Stem : SliceStems())
	{
		USkeletalMesh* Baked = ElysiumNpcVisual::LoadBakedMesh(Stem);
		if (Baked == nullptr)
		{
			AddInfo(FString::Printf(TEXT("skipping %s: not on the baked mount"), *Stem));
			continue;
		}
		if (!IFileManager::Get().FileExists(*FElysiumContentPaths::NpcGlb(Stem)))
		{
			AddInfo(FString::Printf(TEXT("skipping %s: no source .glb to compare against"), *Stem));
			continue;
		}

		// The container the bake read. It states the bind pose and every clip key in Unreal space
		// with no import step to disagree about, which makes it the reference the bake is held to.
		FElysiumSkeletalSource Source;
		if (!FElysiumSkeletalSource::Load(FElysiumContentPaths::NpcSource(Stem), Source, Error))
		{
			AddInfo(FString::Printf(TEXT("%s: no .eskm to check against (%s)"), *Stem, *Error));
			continue;
		}

		UglTFRuntimeAsset* Asset = nullptr;
		USkeletalMesh* Loaded = ElysiumNpcVisual::LoadMeshFromPath(
			FElysiumContentPaths::NpcGlb(Stem), Asset, Error);
		if (Loaded == nullptr)
		{
			AddError(FString::Printf(TEXT("%s: the glTFRuntime path does not load (%s)"), *Stem, *Error));
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
		if (!TestEqual(FString::Printf(TEXT("%s bone count"), *Stem),
			BakedRef.GetNum(), LoadedRef.GetNum()))
		{
			continue;
		}
		if (Source.Bones.Num() != BakedRef.GetNum())
		{
			AddError(FString::Printf(TEXT("%s: %d bones baked against %d in the container"),
				*Stem, BakedRef.GetNum(), Source.Bones.Num()));
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
				*Stem, Missing.Num(), *Missing[0].ToString(),
				Missing.Num() > 1 ? TEXT(", ...") : TEXT("")));
			continue;
		}

		// The rest pose is asserted against the CONTAINER, bone for bone. The loader is not a
		// reference for it: the two paths frame their bones differently -- glTFRuntime derives every
		// bone from the inverse-bind matrices under its own basis, the bake writes the file's own
		// Unreal-space locals -- and a rig can hold one shape in many frames, with the inverse binds
		// absorbing the difference. Both bodies render identically and neither's bone transforms
		// predict the other's, so the thing worth asserting is that the bake reproduced what it read.
		bool bRestPoseSound = true;
		for (int32 BoneIndex = 0; BoneIndex < Source.Bones.Num(); ++BoneIndex)
		{
			const FElysiumSourceBone& Bone = Source.Bones[BoneIndex];
			const int32* At = BakedIndexOf.Find(Bone.Name);
			if (At == nullptr)
			{
				AddError(FString::Printf(TEXT("%s: '%s' is in the container and not in the bake"),
					*Stem, *Bone.Name.ToString()));
				bRestPoseSound = false;
				break;
			}
			const FName BakedParent = BakedRef.GetParentIndex(*At) == INDEX_NONE
				? NAME_None : BakedRef.GetBoneName(BakedRef.GetParentIndex(*At));
			const FName SourceParent = Source.Bones.IsValidIndex(Bone.Parent)
				? Source.Bones[Bone.Parent].Name : NAME_None;
			if (BakedParent != SourceParent)
			{
				AddError(FString::Printf(TEXT("%s: '%s' hangs off '%s', the container says '%s'"),
					*Stem, *Bone.Name.ToString(), *BakedParent.ToString(), *SourceParent.ToString()));
				bRestPoseSound = false;
				break;
			}
			if (!BakedRef.GetRefBonePose()[*At].Equals(Bone.Local, GPositionTolerance))
			{
				AddError(FString::Printf(
					TEXT("%s bone '%s': the bake did not reproduce its container -- ")
					TEXT("baked t=%s q=%s against .eskm t=%s q=%s"),
					*Stem, *Bone.Name.ToString(),
					*BakedRef.GetRefBonePose()[*At].GetTranslation().ToString(),
					*BakedRef.GetRefBonePose()[*At].GetRotation().ToString(),
					*Bone.Local.GetTranslation().ToString(),
					*Bone.Local.GetRotation().ToString()));
				bRestPoseSound = false;
				break;
			}
		}

		// The frame difference itself, recorded rather than failed, so its size stays visible
		// while both paths are selectable.
		{
			TArray<FTransform> BakedPoses;
			TArray<FTransform> LoadedPoses;
			RelativeToRoot(BakedRef, BakedRef.GetRefBonePose(), BakedPoses);
			RelativeToRoot(LoadedRef, LoadedRef.GetRefBonePose(), LoadedPoses);
			for (int32 BoneIndex = 1; BoneIndex < LoadedPoses.Num(); ++BoneIndex)
			{
				const FTransform& BakedPose = BakedPoses[BakedIndexOf[LoadedRef.GetBoneName(BoneIndex)]];
				BindRotationDeg = FMath::Max(BindRotationDeg, FMath::RadiansToDegrees(
					BakedPose.GetRotation().AngularDistance(LoadedPoses[BoneIndex].GetRotation())));
				BindTranslationCm = FMath::Max(BindTranslationCm, FVector::Distance(
					BakedPose.GetTranslation(), LoadedPoses[BoneIndex].GetTranslation()));
			}
		}

		// The face is a curve interface, so its whole contract on this seam is that the same
		// targets exist and the skeleton flags them as morph-target curves.
		const TSet<FName> BakedMorphs = MorphTargetNames(Baked);
		const TSet<FName> LoadedMorphs = MorphTargetNames(Loaded);
		TestEqual(FString::Printf(TEXT("%s morph target count"), *Stem),
			BakedMorphs.Num(), LoadedMorphs.Num());
		for (const FName& Name : LoadedMorphs)
		{
			if (!BakedMorphs.Contains(Name))
			{
				AddError(FString::Printf(TEXT("%s: morph target '%s' did not survive the bake"),
					*Stem, *Name.ToString()));
				continue;
			}
			const USkeleton* Skeleton = Baked->GetSkeleton();
			const FCurveMetaData* MetaData =
				Skeleton != nullptr ? Skeleton->GetCurveMetaData(Name) : nullptr;
			if (MetaData == nullptr || !MetaData->Type.bMorphtarget)
			{
				AddError(FString::Printf(
					TEXT("%s: '%s' carries no morph-target curve metadata, so it can never drive"),
					*Stem, *Name.ToString()));
			}
		}

		// A composed pose is only meaningful over a rig the bake already reproduced, and every
		// bone would fail identically if it had not.
		if (!bRestPoseSound)
		{
			continue;
		}

		// --- the clips ----------------------------------------------------------------------
		// Where a bone is not driven by this clip, both sides hold the body's bind pose: the
		// container states it directly, and the runtime starts every evaluation from the MESH
		// reference pose and lets the sequence overwrite only the bones it carries tracks for.
		TArray<FTransform> BindLocals;
		BindLocals.Reserve(Source.Bones.Num());
		for (const FElysiumSourceBone& Bone : Source.Bones)
		{
			BindLocals.Add(BakedRef.GetRefBonePose()[BakedIndexOf[Bone.Name]]);
		}

		int32 Taken = 0;
		for (const FElysiumSourceClip& Clip : Source.Clips)
		{
			if (Taken >= GMaxClipsPerModel)
			{
				break;
			}
			if (Clip.FrameCount <= 0 || Clip.Tracks.IsEmpty())
			{
				continue;
			}
			// A `_delta` clip states a difference from a base pose, not a pose, so composing it
			// through the hierarchy answers nothing -- and it is the one family the exporter
			// leaves un-normalised, because its correction needs the runtime base.
			if ((Clip.Flags & 0x4) != 0)
			{
				continue;
			}
			UAnimSequence* BakedClip = ElysiumNpcVisual::LoadBakedClip(Baked, Stem, Clip.Name);
			if (BakedClip == nullptr)
			{
				AddInfo(FString::Printf(TEXT("%s '%s': not on the baked mount"), *Stem, *Clip.Name));
				continue;
			}
			KeepAlive.Add(BakedClip);
			++Taken;

			// Length is asserted against the container, which authored it. The bake holds a
			// single-frame pose as a two-key clip, so that case is stated rather than divided out.
			const double Rate = FMath::Max(static_cast<double>(Clip.FrameRate), 1.0);
			const double ExpectedLength = FMath::Max(Clip.FrameCount - 1, 1) / Rate;
			if (!FMath::IsNearlyEqual(BakedClip->GetPlayLength(), ExpectedLength, 1e-3))
			{
				AddError(FString::Printf(TEXT("%s '%s': %.4f s baked against %.4f s authored"),
					*Stem, *Clip.Name, BakedClip->GetPlayLength(), ExpectedLength));
				continue;
			}

			// The clip's tracks by bone, so an undriven bone is told apart from a driven one
			// rather than inferred from whether a sample happened to come back.
			TMap<int32, const FElysiumSourceTrack*> TrackOf;
			TrackOf.Reserve(Clip.Tracks.Num());
			for (const FElysiumSourceTrack& Track : Clip.Tracks)
			{
				if (Source.Bones.IsValidIndex(Track.Bone))
				{
					TrackOf.Add(Track.Bone, &Track);
				}
			}

			bool bClipSound = true;
			for (const double Fraction : GSampleFractions)
			{
				const int32 Frame = FMath::Clamp(
					FMath::RoundToInt32(Fraction * (Clip.FrameCount - 1)), 0, Clip.FrameCount - 1);
				const double Time = Frame / Rate;

				TArray<FTransform> ExpectedLocals = BindLocals;
				TArray<FTransform> ActualLocals = BindLocals;
				for (const TPair<int32, const FElysiumSourceTrack*>& Pair : TrackOf)
				{
					const FElysiumSourceTrack& Track = *Pair.Value;
					const FTransform& Bind = Source.Bones[Pair.Key].Local;
					// A channel the clip leaves alone holds the bind value from the file that
					// AUTHORED the clip, which is what the bake wrote for it.
					ExpectedLocals[Pair.Key] = FTransform(
						Track.Rotations.IsValidIndex(Frame)
							? FQuat(Track.Rotations[Frame]) : Bind.GetRotation(),
						Track.Translations.IsValidIndex(Frame)
							? FVector(Track.Translations[Frame]) : Bind.GetTranslation());

					const FName BoneName = Source.Bones[Pair.Key].Name;
					if (!SampleBone(BakedClip, BoneName, Time, ActualLocals[Pair.Key]))
					{
						AddError(FString::Printf(
							TEXT("%s '%s': bone '%s' is driven by the container and is not on the ")
							TEXT("bake's skeleton, so its track was dropped"),
							*Stem, *Clip.Name, *BoneName.ToString()));
						bClipSound = false;
						break;
					}
				}
				if (!bClipSound)
				{
					break;
				}

				TArray<FTransform> Expected;
				TArray<FTransform> Actual;
				ComposeComponentSpace(Source.Bones, ExpectedLocals, Expected);
				ComposeComponentSpace(Source.Bones, ActualLocals, Actual);

				for (int32 BoneIndex = 0; BoneIndex < Source.Bones.Num(); ++BoneIndex)
				{
					++Samples;
					const double Degrees = FMath::RadiansToDegrees(
						Actual[BoneIndex].GetRotation().AngularDistance(
							Expected[BoneIndex].GetRotation()));
					const double Centimetres = FVector::Distance(
						Actual[BoneIndex].GetTranslation(), Expected[BoneIndex].GetTranslation());
					if (Degrees > GComposedRotationToleranceDeg
						|| Centimetres > GComposedTranslationTolerance)
					{
						AddError(FString::Printf(
							TEXT("%s '%s' frame %d bone '%s': the composed pose is %.4f deg / ")
							TEXT("%.4f cm off VtMB's own"),
							*Stem, *Clip.Name, Frame, *Source.Bones[BoneIndex].Name.ToString(),
							Degrees, Centimetres));
						bClipSound = false;
						break;
					}
				}
				if (!bClipSound)
				{
					break;
				}
			}
		}
		if (Taken == 0)
		{
			AddInfo(FString::Printf(TEXT("%s: no own clip is on the baked mount"), *Stem));
		}
	}

	if (Compared == 0)
	{
		AddInfo(TEXT("skipping: no slice model is baked; run: uv run elysium export characters"));
		return true;
	}
	AddInfo(FString::Printf(TEXT("%d model(s) compared, %d composed bone samples"), Compared, Samples));
	AddInfo(FString::Printf(
		TEXT("bind-frame delta against the glTFRuntime path: %.4f deg / %.4f cm at most, after ")
		TEXT("each rig's own root is divided out -- expected, and not a defect: the two paths frame ")
		TEXT("their bones differently and their inverse binds absorb it, so both render the same ")
		TEXT("body from different rigs"),
		BindRotationDeg, BindTranslationCm));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
