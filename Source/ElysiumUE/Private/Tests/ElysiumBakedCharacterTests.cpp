// The baked cast is the only build of a character (ANM1), so what the bake writes IS the game.
// Everything compared here fails silently in game -- a dropped bone plays part of a skeleton at
// bind pose, a lost morph target leaves a face that evaluates its facial track and never moves,
// and a clip bound to the wrong bone tree plays a plausible wrong pose.
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
// THE MESH ASSERTION IS DELIBERATELY NOT "the glb and the asset agree bone for bone". Nothing
// builds a character from `.glb` at runtime any more; the file is read here as an INDEPENDENT
// ORACLE for what the model contains, because it is the same source the bake consumed and it
// arrives through completely different code. The two reach Unreal through different bases --
// glTFRuntime derives every bone from the inverse-bind matrices under its own basis, and the bake
// writes the file's own locals in the repo's canonical Source-to-Unreal frame
// (`bsp.source_to_unreal`, the `UE_` exporter convention). A rig can hold one shape in many frames
// with the inverse binds absorbing the difference, so the two disagree about individual bone
// transforms -- by whole degrees and centimetres, printed at the end of the run -- while drawing
// the same body. Neither one's bone transforms predict the other's.
//
// So the rest pose is asserted against the CONTAINER: the `.eskm` states the bind pose in Unreal
// space with no import step to disagree about, which makes it the thing the bake had to reproduce.
// The glb is held to the bone SET and the morph SET, which are basis-free and are what catch a
// bake that silently dropped part of the model. The bind-frame delta between them is reported
// rather than failed, because it is a change of basis rather than a defect.
//
// Self-skipping: the baked mount is gitignored and regenerable, so a checkout that has not run
// `uv run elysium export characters` has nothing to compare and says so rather than failing.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumContentPaths.h"
#include "Visual/ElysiumAnimLayerMask.h"
#include "Visual/ElysiumBlendGrids.h"
#include "Visual/ElysiumNpcClips.h"
#include "Visual/ElysiumNpcVisual.h"
#include "Visual/ElysiumSkeletalSource.h"

#include "Animation/AnimSequence.h"
#include "Animation/BlendProfile.h"
#include "Animation/BlendSpace.h"
#include "Animation/AnimTypes.h"
#include "Animation/AnimationPoseData.h"
#include "BonePose.h"
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
	// union, which is the case where a bone can be lost.
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

	// How many `_delta` clips per model to round-trip. Only two shipped models carry any, and
	// their 118 between them are all the same shape.
	constexpr int32 GMaxAdditiveClipsPerModel = 4;

	// How many masked `*_layer` clips per model to check. The whole install states four distinct
	// masks over 209 clips, so a handful covers every shape a wider slice would reach.
	constexpr int32 GMaxLayerClipsPerModel = 4;

	// How many blend grids per clip owner to check (ANM3). `move_and_ranged` alone declares 253, and
	// they are two shapes -- a 9x1 `move_yaw` fan and a 3x3 aim grid -- so a handful reaches both.
	// Sorted by label before the cap is applied, or which shapes it covers would drift with the map
	// iteration order.
	constexpr int32 GMaxGridsPerOwner = 6;

	// Where a baked sample is allowed to sit against the arithmetic the sidecar states. Both sides
	// are float32 over a range of 360, so agreement is exact to within representation; this is set
	// far below the smallest gap between two cells (45 degrees on a nine-cell fan).
	constexpr double GBlendSampleTolerance = 1e-3;

	// The additive delta is read back through the COMPRESSED data, unlike everything else here,
	// because that is the only form the runtime applier ever sees and the only one the additive
	// bake-out has run over. So these are a compressor's tolerance rather than a serialiser's --
	// and still three orders below the failure they exist to catch, which is a bone arriving
	// rotated by its own bind (tens of degrees, centimetres of offset).
	constexpr double GAdditiveRotationToleranceDeg = 0.5;
	constexpr double GAdditiveTranslationTolerance = 0.5;   // centimetres

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
	 * One frame of an additive sequence as the DELTA it states, indexed by skeleton bone.
	 *
	 * `GetBoneTransform` is the wrong door for this and quietly answers the wrong question: it is
	 * a plain track read, so on a raw evaluation it hands back the keys as written and never
	 * performs the additive conversion at all. The keys of a baked `_delta` are the delta already
	 * composed onto the reference pose -- Unreal's own transport, because its compressor subtracts
	 * that pose back out -- so a track read reports the reference pose and calls a correct asset
	 * broken.
	 *
	 * `GetAnimationPose` is the door the runtime uses. It resolves the two evaluation paths
	 * itself: raw data goes through `GetBonePose_Additive`, which subtracts the base pose, and
	 * compressed data was subtracted already at bake time. Both answer the delta, which is what
	 * `FElysiumNpcAnimProxy::EvaluateAdditives` accumulates.
	 *
	 * A bone the sequence carries no track for comes back as the ADDITIVE identity rather than as
	 * the reference pose, which is the property that makes an unmasked layer safe to accumulate
	 * over every bone.
	 */
	bool EvaluateAdditiveFrame(const UAnimSequence* Sequence, const double Time,
		TArray<FTransform>& OutLocals)
	{
		const USkeleton* Skeleton = Sequence != nullptr ? Sequence->GetSkeleton() : nullptr;
		if (Skeleton == nullptr)
		{
			return false;
		}
#if WITH_EDITOR
		// PIN THE PATH, or this asserts whichever one the process happened to be ready to serve.
		// `GetAnimationPose` falls back to the raw data model whenever the compressed data for the
		// current platform is not resident yet, and compression runs asynchronously after a bake --
		// so the same call answered out of two different representations depending on how much work
		// happened earlier in the same run, which made this test's result depend on how many
		// sequences the checks above it had loaded first. Waiting makes it always the compressed
		// data, which is the only form a cooked build ships and the only one the additive bake-out
		// has run over.
		const_cast<UAnimSequence*>(Sequence)->WaitOnExistingCompression();
#endif
		const FReferenceSkeleton& Ref = Skeleton->GetReferenceSkeleton();
		TArray<FBoneIndexType> RequiredBones;
		RequiredBones.SetNumUninitialized(Ref.GetNum());
		for (int32 Bone = 0; Bone < RequiredBones.Num(); ++Bone)
		{
			RequiredBones[Bone] = static_cast<FBoneIndexType>(Bone);
		}

		FMemMark Mark(FMemStack::Get());
		FBoneContainer Container;
		Container.InitializeTo(RequiredBones,
			UE::Anim::FCurveFilterSettings(UE::Anim::ECurveFilterMode::None), *Skeleton);
		FCompactPose Pose;
		Pose.SetBoneContainer(&Container);
		FBlendedCurve Curve;
		Curve.InitFrom(Container);
		UE::Anim::FStackAttributeContainer Attributes;
		FAnimationPoseData PoseData(Pose, Curve, Attributes);
		if (!Sequence->IsCompressedDataValid())
		{
			// Reported rather than worked around: after the wait above this should not happen, and
			// if it does the reading below came out of the raw data model and the compressed data --
			// the only form a cooked build ships -- was never checked at all.
			UE_LOG(LogTemp, Warning,
				TEXT("%s: no compressed data after waiting on compression, so this reads raw"),
				*Sequence->GetName());
		}
		Sequence->GetAnimationPose(PoseData, FAnimExtractContext(Time));

		OutLocals.SetNum(Ref.GetNum());
		for (const FCompactPoseBoneIndex BoneIndex : Pose.ForEachBoneIndex())
		{
			const FSkeletonPoseBoneIndex Skeletal =
				Container.GetSkeletonPoseIndexFromCompactPoseIndex(BoneIndex);
			if (Skeletal.IsValid() && OutLocals.IsValidIndex(Skeletal.GetInt()))
			{
				OutLocals[Skeletal.GetInt()] = Pose[BoneIndex];
			}
		}
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
	int32 AdditiveClips = 0;
	int32 LayerClips = 0;
	int32 BlendGrids = 0;

	// The `_delta` round-trip, for one container's worth of clips against one baked body.
	//
	// This is the ONE assertion here that reads the compressed data, and it has to. Unreal bakes an
	// additive sequence down by subtracting its base pose before compressing, so the raw keys the
	// bake writes are the delta composed onto the skeleton's reference pose and the compressed data
	// is the delta itself. The runtime accumulator (`FElysiumNpcAnimProxy::EvaluateAdditives`) reads
	// the second, so the second is what has to equal what VtMB authored -- and a raw comparison
	// would pass without the subtraction ever having run.
	//
	// Un-composed, unlike everything else here: a delta has no hierarchy to inherit through. It is
	// accumulated onto a bone's own local rotation, so a per-bone comparison IS the contract.
	auto CheckAdditives = [&](const USkeletalMesh* Baked, const FString& Owner,
		const FElysiumSkeletalSource& Container)
	{
		int32 Taken = 0;
		for (const FElysiumSourceClip& Clip : Container.Clips)
		{
			if (Taken >= GMaxAdditiveClipsPerModel)
			{
				break;
			}
			if ((Clip.Flags & 0x4) == 0 || Clip.FrameCount <= 0 || Clip.Tracks.IsEmpty())
			{
				continue;
			}
			if (Clip.BaseName.IsEmpty())
			{
				// A delta no host declares has no base to be a difference from, so no asset is
				// built for it. Finding one means the mount is carrying a package from a previous
				// export -- and one that is WRONG, because it was written against a different base
				// and is still resolvable by the label the model references.
				if (ElysiumNpcVisual::LoadBakedClip(Baked, Owner, Clip.Name) != nullptr)
				{
					AddError(FString::Printf(
						TEXT("%s '%s': an additive with no declared base is on the mount, so a ")
						TEXT("stale package survived a re-export -- clean the mount"),
						*Owner, *Clip.Name));
				}
				continue;
			}
			const FElysiumSourceClip* BaseClip = Container.Clips.FindByPredicate(
				[&Clip](const FElysiumSourceClip& Candidate)
				{ return Candidate.Name == Clip.BaseName; });
			if (BaseClip == nullptr)
			{
				AddError(FString::Printf(TEXT("%s '%s': names base '%s', absent from the container"),
					*Owner, *Clip.Name, *Clip.BaseName));
				continue;
			}
			UAnimSequence* BakedDelta = ElysiumNpcVisual::LoadBakedClip(Baked, Owner, Clip.Name);
			if (BakedDelta == nullptr)
			{
				AddInfo(FString::Printf(TEXT("%s '%s': not on the baked mount"), *Owner, *Clip.Name));
				continue;
			}
			KeepAlive.Add(BakedDelta);
			++Taken;
			++AdditiveClips;

			// Without the stamp the sequence evaluates to a POSE, and every bone the layer does not
			// touch comes back as the reference pose rather than as no change -- which the
			// accumulator would then post-multiply into the body, one bind rotation per bone.
			if (!BakedDelta->IsValidAdditive())
			{
				AddError(FString::Printf(
					TEXT("%s '%s': STUDIO_DELTA in the container and not additive on the mount, so ")
					TEXT("no delta can be read out of it"), *Owner, *Clip.Name));
				continue;
			}

			const FReferenceSkeleton& BakedRefSkeleton =
				BakedDelta->GetSkeleton()->GetReferenceSkeleton();
			bool bSound = true;
			for (const double Fraction : GSampleFractions)
			{
				const int32 Frame = FMath::Clamp(
					FMath::RoundToInt32(Fraction * (Clip.FrameCount - 1)), 0, Clip.FrameCount - 1);
				const double Time = Frame / FMath::Max(static_cast<double>(Clip.FrameRate), 1.0);
				TArray<FTransform> Deltas;
				if (!EvaluateAdditiveFrame(BakedDelta, Time, Deltas))
				{
					break;
				}
				for (const FElysiumSourceTrack& Track : Clip.Tracks)
				{
					if (!Container.Bones.IsValidIndex(Track.Bone))
					{
						continue;
					}
					const FName BoneName = Container.Bones[Track.Bone].Name;
					const int32 SkeletonBone = BakedRefSkeleton.FindBoneIndex(BoneName);
					if (!Deltas.IsValidIndex(SkeletonBone))
					{
						// A bank drives bones this rig family has never had; that is sharing, not a
						// defect, and the composed pass below is what asserts an own body's rig.
						continue;
					}
					// The container states the COMPOSED pose -- the base with the delta already on
					// it -- so what the asset must hand back is whatever pre-multiplies onto the
					// base to reproduce that. Re-applying it here is what makes this assertion
					// cover the whole chain at once: the exporter's composition, the bake naming
					// `ABPT_AnimFrame`, and the compressor's subtraction. Comparing the read-back
					// delta against the container's track directly would assert none of them, and
					// would pass just as happily against a delta conjugated by the wrong base.
					const FElysiumSourceTrack* BaseBone = BaseClip->Tracks.FindByPredicate(
						[&Track](const FElysiumSourceTrack& Candidate)
						{ return Candidate.Bone == Track.Bone; });
					const FQuat BaseRotation = (BaseBone != nullptr
						&& BaseBone->Rotations.IsValidIndex(0))
						? FQuat(BaseBone->Rotations[0])
						: Container.Bones[Track.Bone].Local.GetRotation();
					const FVector BasePosition = (BaseBone != nullptr
						&& BaseBone->Translations.IsValidIndex(0))
						? FVector(BaseBone->Translations[0])
						: Container.Bones[Track.Bone].Local.GetTranslation();

					const FQuat Expected = Track.Rotations.IsValidIndex(Frame)
						? FQuat(Track.Rotations[Frame]) : BaseRotation;
					const FVector ExpectedPos = Track.Translations.IsValidIndex(Frame)
						? FVector(Track.Translations[Frame]) : BasePosition;
					FTransform Actual = Deltas[SkeletonBone];
					Actual.SetRotation((Actual.GetRotation() * BaseRotation).GetNormalized());
					Actual.SetTranslation(Actual.GetTranslation() + BasePosition);

					++Samples;
					const double Degrees = FMath::RadiansToDegrees(
						Actual.GetRotation().AngularDistance(Expected));
					const double Centimetres =
						FVector::Distance(Actual.GetTranslation(), ExpectedPos);
					if (Degrees > GAdditiveRotationToleranceDeg
						|| Centimetres > GAdditiveTranslationTolerance)
					{
						AddError(FString::Printf(
							TEXT("%s '%s' frame %d bone '%s': the baked delta is %.4f deg / %.4f cm ")
							TEXT("off VtMB's own"),
							*Owner, *Clip.Name, Frame, *BoneName.ToString(), Degrees, Centimetres));
						bSound = false;
						break;
					}
				}
				if (!bSound)
				{
					break;
				}
			}
		}
	};

	// The masked partial-body overlays, the other kind of autolayer. Where an additive is verified
	// by its numbers, one of these is verified by its GATE: a `*_layer` sequence is composed by a
	// complementary-weight blend that replaces the bones it owns, so what has to survive the bake
	// is which bones those are and what the clip poses them at where it animates nothing.
	//
	// Both halves fail silently and neither is visible in the sequence's own tracks. A lost mask
	// composes the overlay over the whole rig at full weight, which erases the body's stance from
	// the waist down; a lost bind track leaves an owned bone on the shared skeleton's reference
	// pose, which is another body of the family's bind rather than this clip's own.
	auto CheckLayerMasks = [&](const USkeletalMesh* Baked, const FString& Owner,
		const FElysiumSkeletalSource& Container)
	{
		int32 Taken = 0;
		for (const FElysiumSourceClip& Clip : Container.Clips)
		{
			if (Taken >= GMaxLayerClipsPerModel)
			{
				break;
			}
			// An additive states its own mask through the additive identity and is given no mask
			// asset; a clip with no mask at all owns the whole rig and needs none.
			if ((Clip.Flags & 0x4) != 0 || !Container.Masks.IsValidIndex(Clip.Mask)
				|| Clip.FrameCount <= 0 || Clip.Tracks.IsEmpty())
			{
				continue;
			}
			UAnimSequence* BakedLayer = ElysiumNpcVisual::LoadBakedClip(Baked, Owner, Clip.Name);
			if (BakedLayer == nullptr)
			{
				AddInfo(FString::Printf(TEXT("%s '%s': not on the baked mount"), *Owner, *Clip.Name));
				continue;
			}
			KeepAlive.Add(BakedLayer);
			++Taken;
			++LayerClips;

			const UElysiumAnimLayerMask* Meta =
				BakedLayer->FindMetaDataByClass<UElysiumAnimLayerMask>();
			USkeleton* Skeleton = BakedLayer->GetSkeleton();
			const UBlendProfile* Profile = Meta != nullptr && Skeleton != nullptr
				? Skeleton->GetBlendProfile(Meta->Profile) : nullptr;
			if (Profile == nullptr)
			{
				int32 Owned = 0;
				for (const uint8 In : Container.Masks[Clip.Mask].Bones)
				{
					Owned += In != 0 ? 1 : 0;
				}
				AddError(FString::Printf(
					TEXT("%s '%s': the container has it owning %d of %d bones and the baked sequence "
					     "names %s, so it would compose over the whole rig"),
					*Owner, *Clip.Name, Owned, Container.Bones.Num(),
					Meta == nullptr ? TEXT("no blend mask") : *Meta->Profile.ToString()));
				continue;
			}

			const FReferenceSkeleton& Ref = Skeleton->GetReferenceSkeleton();
			const FElysiumSourceMask& Mask = Container.Masks[Clip.Mask];
			TSet<int32> Tracked;
			for (const FElysiumSourceTrack& Track : Clip.Tracks)
			{
				Tracked.Add(Track.Bone);
			}

			bool bMaskSound = true;
			for (int32 Bone = 0; Bone < Container.Bones.Num() && bMaskSound; ++Bone)
			{
				const FName BoneName = Container.Bones[Bone].Name;
				if (Ref.FindBoneIndex(BoneName) == INDEX_NONE)
				{
					// A bank names bones this family has never had; they leave the mask for the
					// same reason their tracks are dropped.
					continue;
				}
				const bool bOwned = Mask.Bones[Bone] != 0;
				++Samples;
				if ((Profile->GetBoneBlendScale(BoneName) > 0.0f) != bOwned)
				{
					AddError(FString::Printf(
						TEXT("%s '%s' bone '%s': the container %s it and the blend mask %s"),
						*Owner, *Clip.Name, *BoneName.ToString(),
						bOwned ? TEXT("owns") : TEXT("masks out"),
						bOwned ? TEXT("does not") : TEXT("owns it")));
					bMaskSound = false;
					break;
				}
				// An owned bone the clip animates nothing on holds its BIND pose, which is a pose
				// the overlay states rather than an absence. The bake writes it out; without that
				// the sequence would evaluate to the shared skeleton's reference pose here.
				if (!bOwned || Tracked.Contains(Bone))
				{
					continue;
				}
				FTransform Held;
				if (!SampleBone(BakedLayer, BoneName, 0.0, Held))
				{
					continue;
				}
				const FTransform& Bind = Container.Bones[Bone].Local;
				const double Degrees = FMath::RadiansToDegrees(
					Held.GetRotation().AngularDistance(Bind.GetRotation()));
				const double Centimetres =
					FVector::Distance(Held.GetTranslation(), Bind.GetTranslation());
				++Samples;
				if (Degrees > GComposedRotationToleranceDeg || Centimetres > GPositionTolerance)
				{
					AddError(FString::Printf(
						TEXT("%s '%s' bone '%s': owned by the mask and unanimated, so it holds its "
						     "bind pose -- the bake has it %.4f deg / %.4f cm away"),
						*Owner, *Clip.Name, *BoneName.ToString(), Degrees, Centimetres));
					bMaskSound = false;
					break;
				}
			}
		}
	};

	// The blend grids (ANM3), against the sidecar that declares them.
	//
	// A blend space fails silently in three distinct ways and none of them is visible in a list that
	// counts samples. Its axis range can be widened past what the grid states, which puts every cell
	// somewhere the sequence never declared; a sample can be dropped -- `UBlendSpace::AddSample`
	// reports failure only through its return value -- which leaves a hole the evaluator interpolates
	// straight across; and `ResampleData` can be skipped, which leaves the samples in place with no
	// triangulation to blend them, so the asset poses nothing at all.
	//
	// The fourth is why the aim grids are in scope. A grid of masked `*_layer` overlays composes as
	// ONE layer under ONE bone mask, because no Unreal blend node masks per sample -- so if the cells
	// of a grid disagree about their mask, that grid cannot be layered and the graph the mask assets
	// were baked for cannot be built over it. The bake refuses to write one; this says the same thing
	// about what actually landed.
	auto CheckBlendSpaces = [&](const USkeletalMesh* Baked, const FString& Owner)
	{
		FElysiumBlendTable Table;
		FString TableError;
		if (!Table.Load(FString::Printf(TEXT("blends/%s.json"), *Owner), TableError))
		{
			// Most owners declare no grid and ship no sidecar at all, which is an ordinary load.
			return;
		}

		TArray<FString> Labels;
		Table.Grids.GetKeys(Labels);
		Labels.Sort([](const FString& A, const FString& B) { return A < B; });

		int32 Taken = 0;
		for (const FString& Label : Labels)
		{
			if (Taken >= GMaxGridsPerOwner)
			{
				break;
			}
			const FElysiumBlendGrid& Grid = Table.Grids[Label];
			UBlendSpace* Space = ElysiumNpcVisual::LoadBakedBlendSpace(Baked, Owner, Label);
			if (Space == nullptr)
			{
				AddInfo(FString::Printf(TEXT("%s grid '%s': not on the baked mount"), *Owner, *Label));
				continue;
			}
			KeepAlive.Add(Space);
			++Taken;
			++BlendGrids;

			// The body's own skeleton, or one it has DECLARED compatible. A bank is baked once
			// against a skeleton of its own -- rebuilding it per rig family was 11x the assets for
			// the same animation -- so equality is no longer the rule that makes a grid playable.
			// The declaration is: it is what builds the name-keyed bone map the evaluator remaps
			// through, and without it the samples really would not evaluate on this rig.
			const USkeleton* BodySkeleton = Baked->GetSkeleton();
			const USkeleton* SpaceSkeleton = Space->GetSkeleton();
			if (SpaceSkeleton != BodySkeleton
				&& !(BodySkeleton != nullptr && BodySkeleton->IsCompatibleForEditor(SpaceSkeleton)))
			{
				AddError(FString::Printf(
					TEXT("%s grid '%s': bound to skeleton %s, which %s neither is nor declares "
					     "compatible, so no sample of it can evaluate on this rig"),
					*Owner, *Label,
					SpaceSkeleton != nullptr ? *SpaceSkeleton->GetName() : TEXT("none"),
					BodySkeleton != nullptr ? *BodySkeleton->GetName() : TEXT("none")));
				continue;
			}

			const int32 Axes = (Grid.GroupSize[1] > 1 && Grid.ParamIndex[1] != INDEX_NONE) ? 2 : 1;
			bool bAxesSound = true;
			for (int32 Axis = 0; Axis < Axes; ++Axis)
			{
				const FBlendParameter& Parameter = Space->GetBlendParameter(Axis);
				++Samples;
				if (!FMath::IsNearlyEqual(Parameter.Min, Grid.ParamStart[Axis], 1e-3f)
					|| !FMath::IsNearlyEqual(Parameter.Max, Grid.ParamEnd[Axis], 1e-3f)
					|| Parameter.GridNum != FMath::Max(1, Grid.GroupSize[Axis] - 1))
				{
					AddError(FString::Printf(
						TEXT("%s grid '%s' axis %d: the sidecar states %.3f..%.3f over %d cells and the "
						     "asset carries %.3f..%.3f over %d divisions"),
						*Owner, *Label, Axis, Grid.ParamStart[Axis], Grid.ParamEnd[Axis],
						Grid.GroupSize[Axis], Parameter.Min, Parameter.Max, Parameter.GridNum));
					bAxesSound = false;
				}
			}
			if (!bAxesSound)
			{
				continue;
			}

			// The sample placement, cell by cell. Derived here rather than shared with the bake: an
			// assertion that calls the code it is asserting cannot fail.
			const TArray<FBlendSample>& SampleData = Space->GetBlendSamples();
			int32 Live = 0;
			bool bSamplesSound = true;
			for (const FElysiumBlendCell& Cell : Grid.Cells)
			{
				if (Cell.Clip.IsEmpty())
				{
					continue;
				}
				const FString Wanted = TEXT("A_") + FElysiumContentPaths::BakedAssetName(Cell.Clip);
				if (ElysiumNpcVisual::LoadBakedClip(Baked, Owner, Cell.Clip) == nullptr)
				{
					// The bake skips a cell whose sequence is absent, so the asset is right to be
					// short one and the count below must not expect it.
					continue;
				}
				++Live;

				FVector Expected = FVector::ZeroVector;
				for (int32 Axis = 0; Axis < Axes; ++Axis)
				{
					const int32 Count = Grid.GroupSize[Axis];
					const float Alpha = Count > 1
						? static_cast<float>(Cell.Axis[Axis]) / static_cast<float>(Count - 1) : 0.f;
					Expected[Axis] = Grid.ParamStart[Axis]
						+ Alpha * (Grid.ParamEnd[Axis] - Grid.ParamStart[Axis]);
				}

				const FBlendSample* Found = SampleData.FindByPredicate(
					[&](const FBlendSample& Candidate)
					{
						for (int32 Axis = 0; Axis < Axes; ++Axis)
						{
							if (FMath::Abs(Candidate.SampleValue[Axis] - Expected[Axis])
								> GBlendSampleTolerance)
							{
								return false;
							}
						}
						return true;
					});
				++Samples;
				if (Found == nullptr)
				{
					AddError(FString::Printf(
						TEXT("%s grid '%s' cell [%d,%d]: nothing is sampled at %s, so '%s' is a hole "
						     "the blend interpolates straight across"),
						*Owner, *Label, Cell.Axis[0], Cell.Axis[1], *Expected.ToString(), *Cell.Clip));
					bSamplesSound = false;
					break;
				}
				if (Found->Animation == nullptr || Found->Animation->GetName() != Wanted)
				{
					AddError(FString::Printf(
						TEXT("%s grid '%s' cell [%d,%d]: the sidecar names '%s' and the sample at %s "
						     "carries %s"),
						*Owner, *Label, Cell.Axis[0], Cell.Axis[1], *Wanted, *Expected.ToString(),
						Found->Animation != nullptr ? *Found->Animation->GetName() : TEXT("nothing")));
					bSamplesSound = false;
					break;
				}
			}
			if (!bSamplesSound)
			{
				continue;
			}
			if (SampleData.Num() != Live)
			{
				AddError(FString::Printf(
					TEXT("%s grid '%s': %d of the sidecar's cells baked and the asset carries %d "
					     "sample(s)"), *Owner, *Label, Live, SampleData.Num()));
				continue;
			}
			if (Space->GetBlendSpaceData().IsEmpty())
			{
				AddError(FString::Printf(
					TEXT("%s grid '%s': %d samples and no blend data, so it evaluates to nothing"),
					*Owner, *Label, SampleData.Num()));
				continue;
			}

			// The measured half of the aim-grid question. Reported as an error rather than info: a
			// grid whose cells disagree cannot be composed as a layer at all.
			FName Profile = NAME_None;
			bool bFirst = true;
			for (const FBlendSample& Sample : SampleData)
			{
				const UElysiumAnimLayerMask* Mask = Sample.Animation != nullptr
					? Sample.Animation->FindMetaDataByClass<UElysiumAnimLayerMask>() : nullptr;
				const FName Named = Mask != nullptr ? Mask->Profile : NAME_None;
				if (bFirst)
				{
					Profile = Named;
					bFirst = false;
					continue;
				}
				++Samples;
				if (Named != Profile)
				{
					AddError(FString::Printf(
						TEXT("%s grid '%s': its cells name different bone masks (%s and %s), so it "
						     "cannot compose as one layer under one mask"),
						*Owner, *Label,
						Profile.IsNone() ? TEXT("none") : *Profile.ToString(),
						Named.IsNone() ? TEXT("none") : *Named.ToString()));
					break;
				}
			}
		}
	};

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

		// The `_delta` family, which lives in the BANKS rather than in a body's own container --
		// `move_and_ranged` carries all 60 of the male ones. Resolved through the body's own
		// vocabulary rather than named here, so the pass follows whatever the export actually
		// wrote. Runs before the composed-pose loop below because it answers a different question
		// and shares none of its machinery.
		CheckAdditives(Baked, Stem, Source);
		CheckLayerMasks(Baked, Stem, Source);
		CheckBlendSpaces(Baked, Stem);
		{
			FElysiumNpcClipSet Vocabulary;
			FString VocabularyError;
			TSet<FString> AdditiveOwners;
			// Every bank this body can play, which is a wider set than the additive owners below: a
			// grid is declared wherever the animations live, and a bank owning grids need not own a
			// `_delta`. Cheap to widen because a blend space is checked against its own sidecar and
			// the mount, with no container to load.
			TSet<FString> GridOwners;
			if (Vocabulary.Load(Stem, VocabularyError))
			{
				for (const TPair<FString, FElysiumNpcClip>& Entry : Vocabulary.Clips)
				{
					if (Entry.Value.IsOwnedBy(Stem))
					{
						continue;
					}
					GridOwners.Add(Entry.Value.Owner);
					if (Entry.Value.IsAdditive())
					{
						AdditiveOwners.Add(Entry.Value.Owner);
					}
				}
			}
			for (const FString& Bank : GridOwners)
			{
				CheckBlendSpaces(Baked, Bank);
			}
			for (const FString& Bank : AdditiveOwners)
			{
				FElysiumSkeletalSource BankSource;
				FString BankError;
				if (!FElysiumSkeletalSource::Load(
					FElysiumContentPaths::NpcBankSource(Bank), BankSource, BankError))
				{
					AddInfo(FString::Printf(TEXT("%s: bank '%s' has no container (%s)"),
						*Stem, *Bank, *BankError));
					continue;
				}
				CheckAdditives(Baked, Bank, BankSource);
				// The same bank, and not a coincidence: the two kinds of autolayer are declared
				// together on `move_and_ranged`, one masked `*_layer` and one `_delta` per host
				// sequence (`docs/vtmb/animation_and_movers.md` A.3).
				CheckLayerMasks(Baked, Bank, BankSource);
			}
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
			// leaves un-normalised, because its correction needs the runtime base. The pass above
			// asserts it in the only form that means anything for one.
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
	AddInfo(FString::Printf(TEXT("%d model(s) compared, %d bone samples, %d `_delta` clip(s) ")
		TEXT("round-tripped through the additive bake, %d masked `_layer` clip(s) checked against ")
		TEXT("their blend masks, %d blend grid(s) checked against their sidecar"),
		Compared, Samples, AdditiveClips, LayerClips, BlendGrids));
	AddInfo(FString::Printf(
		TEXT("bind-frame delta against the glTFRuntime path: %.4f deg / %.4f cm at most, after ")
		TEXT("each rig's own root is divided out -- expected, and not a defect: the two paths frame ")
		TEXT("their bones differently and their inverse binds absorb it, so both render the same ")
		TEXT("body from different rigs"),
		BindRotationDeg, BindTranslationCm));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
