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
// THE MESH ASSERTION IS AGAINST THE CONTAINER, bone for bone and morph for morph. The `.eskm`
// states the bind pose in Unreal space with no import step to disagree about, which makes it the
// thing the bake had to reproduce; the bone set and the morph set are what catch a bake that
// silently dropped part of the model.
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
#include "Engine/SkeletalMeshSocket.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

static constexpr EAutomationTestFlags GElysiumBakedCharacterFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// The parity slice, chosen by CONTAINER property rather than by which bodies happen to be
	// interesting. Seven of the nine are every stem, across all 166 exported `.eskm` containers,
	// whose VtMB skeleton carries MORE THAN ONE parentless bone -- every one of them is here, not a
	// sample of them. `brian` is the most stressing case: four strays, two of whose names differ
	// only by a `[2]` prefix. `smiling_jack` and `tremere_male_armor_0` are the single-rooted
	// control the other seven read against, one plain body and one armour variant, so a defect a
	// fork produces stays separable from one every container would show.
	const TCHAR* const GDefaultSliceStems[] = {
		TEXT("smiling_jack"),
		TEXT("tremere_male_armor_0"),
		TEXT("ash"),
		TEXT("brian"),
		TEXT("nosferatu_female_armor_1"),
		TEXT("nosferatu_female_armor_2"),
		TEXT("prophet"),
		TEXT("regular_cop"),
		TEXT("tremere_male_armor_3"),
	};

	// `-ElysiumParityStems=a,b,c` widens (or narrows) the slice for a one-off run without touching
	// this file. `bOutIsDefaultSlice` reports whether this call resolved to `GDefaultSliceStems`
	// unmodified -- true whenever no override was given, or the override named nothing -- because
	// the parity test holds a resolution miss to a stricter standard on the default slice: every
	// stem in it was put there BECAUSE it needs the coverage, where an ad-hoc stem's owner may
	// reasonably have picked one with no bank locomotion to check.
	TArray<FString> SliceStems(bool& bOutIsDefaultSlice)
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
				bOutIsDefaultSlice = false;
				return Stems;
			}
		}
		bOutIsDefaultSlice = true;
		TArray<FString> Stems;
		for (const TCHAR* Stem : GDefaultSliceStems)
		{
			Stems.Add(Stem);
		}
		return Stems;
	}

	TArray<FString> SliceStems()
	{
		bool bUnusedIsDefaultSlice = false;
		return SliceStems(bUnusedIsDefaultSlice);
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

	// A shared bank clip composed against a BODY's own baked skeleton, checked against the same
	// clip composed against the BANK's own skeleton -- the one comparison that crosses Unreal's
	// compatible-skeleton retargeting (`FSkeletonRemapping`), which every other tolerance in this
	// file is set to avoid exercising at all. Two independently built skeletons carry their own
	// `OrientAndScale` retarget sources, so a clean corpus still accumulates more slack here than
	// the sub-degree/sub-millimetre agreement a body's own clips owe their own container -- but
	// both numbers stay two orders of magnitude below the ~90 degree yaw and ~99 cm lift a clip
	// landing on the wrong bone produces -- bone 0 carrying anything other than `Bip01`, which is
	// the failure this check exists to catch. The exporter's `_single_root` puts `Bip01` at slot 0
	// in every container, so that magnitude is what the tolerances are calibrated against.
	constexpr double GBankLocomotionRotationToleranceDeg = 2.0;
	constexpr double GBankLocomotionTranslationTolerance = 5.0;   // centimetres

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
		// Raw data, because this is about the authored content surviving the bake rather than
		// about what the compressor made of it.
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
	 * compressed data was subtracted already at bake time. Both answer the delta, which is what the
	 * graph's own `FAnimNode_ApplyAdditive` composes.
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
	 * One frame of an ORDINARY (non-additive) sequence, composed against an arbitrary TARGET
	 * asset -- a `USkeleton` or a `USkeletalMesh` -- rather than always the sequence's own.
	 *
	 * Passing the sequence's own skeleton reproduces the plain evaluation every other reader in
	 * this file uses. Passing a DIFFERENT but compatible one -- a body's own baked mesh, playing a
	 * clip a shared bank owns -- is the compatible-skeleton retargeting path a live playthrough
	 * actually takes: `FBoneContainer::Initialize` resolves `RequiredBones.GetSkeletonAsset()` off
	 * whichever asset is handed in, and the compressed decode path
	 * (`UE::Anim::Decompression::GetPoseFromAnimTrackData`) looks up
	 * `FSkeletonRemappingRegistry::GetRemapping(SourceSkeleton, TargetSkeleton)` and applies it
	 * whenever the two differ -- which is exactly the mechanism a fork-carrying body's synthetic
	 * root broke, by displacing `Bip01` from bone 0 and leaving `GenerateMapping`'s
	 * always-map-bone-0-by-index rule pointing at the wrong bone. No other check in this file
	 * builds a `FBoneContainer` off anything but the sequence's own skeleton, so no other check can
	 * observe this.
	 *
	 * Bone-indexed by the TARGET asset's own reference skeleton (`Mesh->GetRefSkeleton()` for a
	 * mesh, `Skeleton->GetReferenceSkeleton()` for a bare skeleton) via the mesh<->skeleton pose
	 * remap `FBoneContainer` already carries, so the caller can compose the result through that
	 * same asset's own bone tree without a second index translation.
	 */
	bool EvaluateOrdinaryFrame(const UAnimSequence* Sequence, const UObject* TargetAsset,
		const double Time, TArray<FTransform>& OutLocals)
	{
		const USkeletalMesh* TargetMesh = Cast<USkeletalMesh>(TargetAsset);
		const USkeleton* TargetSkeleton = TargetMesh != nullptr
			? TargetMesh->GetSkeleton() : Cast<USkeleton>(TargetAsset);
		if (Sequence == nullptr || TargetAsset == nullptr || TargetSkeleton == nullptr)
		{
			return false;
		}
		const FReferenceSkeleton& Ref = TargetMesh != nullptr
			? TargetMesh->GetRefSkeleton() : TargetSkeleton->GetReferenceSkeleton();
#if WITH_EDITOR
		// PIN THE PATH, for the same reason `EvaluateAdditiveFrame` does: an unresolved compression
		// job would silently fall back to the raw data model, which never runs the compatible-
		// skeleton remap this function exists to exercise.
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
			UE::Anim::FCurveFilterSettings(UE::Anim::ECurveFilterMode::None), *TargetAsset);
		FCompactPose Pose;
		Pose.SetBoneContainer(&Container);
		FBlendedCurve Curve;
		Curve.InitFrom(Container);
		UE::Anim::FStackAttributeContainer Attributes;
		FAnimationPoseData PoseData(Pose, Curve, Attributes);
		if (!Sequence->IsCompressedDataValid())
		{
			UE_LOG(LogTemp, Warning,
				TEXT("%s: no compressed data after waiting on compression, so this reads raw and "
				     "skips compatible-skeleton remapping"),
				*Sequence->GetName());
		}
		Sequence->GetAnimationPose(PoseData, FAnimExtractContext(Time));

		// Undriven bones default to the TARGET's own reference pose, same as an ordinary sequence
		// evaluated the plain way -- this is not an additive, so there is no identity to fall back to.
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
		AddInfo(FString::Printf(TEXT("ELYSIUM_TEST_ABSTAIN: no NPC index (%s)"), *Error));
		return true;
	}

	int32 Compared = 0;
	int32 Samples = 0;
	// How far apart the two paths frame the same bone, accumulated across the slice and reported
	// once. Taken relative to each rig's own root, so it is what remains after the basis difference
	// at the root is divided out.
	TArray<UObject*> KeepAlive;
	int32 AdditiveClips = 0;
	int32 LayerClips = 0;
	int32 BlendGrids = 0;
	int32 BankLocomotionChecks = 0;

	// The `_delta` round-trip, for one container's worth of clips against one baked body.
	//
	// This is the ONE assertion here that reads the compressed data, and it has to. Unreal bakes an
	// additive sequence down by subtracting its base pose before compressing, so the raw keys the
	// bake writes are the delta composed onto the skeleton's reference pose and the compressed data
	// is the delta itself. The graph's additive node (`FAnimNode_ApplyAdditive`) reads the second, so
	// the second is what has to equal what VtMB authored -- and a raw comparison would pass without
	// the subtraction ever having run.
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
						// A bank drives bones this body has never had; that is sharing, not a
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
	// the waist down; a lost bind track leaves an owned bone on the skeleton's reference pose,
	// whose rotations are identity by construction, so the bone comes back rotation-neutral rather
	// than at the bind this clip states for it.
	auto CheckLayerMasks = [&](const USkeletalMesh* Baked, const FString& Owner,
		const FElysiumSkeletalSource& Container)
	{
		// Which appendix bones this container's clips MOVE, over the whole container -- the same
		// question the bake asks before deciding a track is a rest pose rather than animation.
		TSet<int32> AnimatedAppendix;
		for (const FElysiumSourceClip& Any : Container.Clips)
		{
			for (const FElysiumSourceTrack& Track : Any.Tracks)
			{
				bool bMoves = false;
				for (int32 Frame = 1; !bMoves && Frame < Track.Translations.Num(); ++Frame)
				{
					bMoves = (Track.Translations[Frame] - Track.Translations[0]).Size() > 0.1f;
				}
				for (int32 Frame = 1; !bMoves && Frame < Track.Rotations.Num(); ++Frame)
				{
					bMoves = FMath::RadiansToDegrees(
						Track.Rotations[0].AngularDistance(Track.Rotations[Frame])) > 0.5f;
				}
				if (bMoves)
				{
					AnimatedAppendix.Add(Track.Bone);
				}
			}
		}

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
					// A bank names bones this body has never had; they leave the mask for the
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
				// the sequence would evaluate to the skeleton's reference pose here.
				//
				// **Except in the appendix of a shared bank**, where the bake deliberately writes
				// nothing. That bind belongs to the bank's own rig, and binding it by name hands it
				// to whichever chain on the playing body happens to share the name -- so the bone is
				// left untracked on purpose and resolves to the playing mesh's own bind instead.
				// The same rule the bake applies, stated here rather than inferred from a magnitude.
				const bool bSilentAppendix = Container.Vertices.IsEmpty()
					&& !BoneName.ToString().StartsWith(TEXT("Bip01"))
					&& !AnimatedAppendix.Contains(Bone);
				if (!bOwned || Tracked.Contains(Bone) || bSilentAppendix)
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

		// Which hosts declare each grid as a layer, derived from the autolayer bindings in the same
		// sidecar. A grid whose cells ship only in derived form has one asset PER declaring host and
		// no host-free form at all, so a label alone does not name an asset — asking for the bare
		// label found nothing for 299 of the mount's 527 spaces, and this loop then skipped every
		// one of them as an ordinary absence.
		TMap<FString, TArray<FString>> HostsByTarget;
		for (const TPair<FString, FElysiumAutoLayerBinding>& Binding : Table.AutoLayers)
		{
			for (const FString& Target : Binding.Value.Clips)
			{
				HostsByTarget.FindOrAdd(Target).AddUnique(Binding.Key);
			}
		}
		for (TPair<FString, TArray<FString>>& Row : HostsByTarget)
		{
			Row.Value.Sort([](const FString& A, const FString& B) { return A < B; });
		}

		// One entry per asset the bake should have written: the grid, and the host its cells carry.
		TArray<TPair<FString, FString>> Builds;
		for (const FString& Label : Labels)
		{
			const TArray<FString>* Hosts = HostsByTarget.Find(Label);
			if (Hosts == nullptr || Hosts->IsEmpty())
			{
				Builds.Emplace(Label, FString());
				continue;
			}
			for (const FString& Host : *Hosts)
			{
				Builds.Emplace(Label, Host);
			}
		}

		int32 Taken = 0;
		for (const TPair<FString, FString>& Build : Builds)
		{
			if (Taken >= GMaxGridsPerOwner)
			{
				break;
			}
			const FString& Label = Build.Key;
			const FString& Host = Build.Value;
			const FString Suffix = Host.IsEmpty() ? FString() : TEXT("@") + Host;
			const FString Shown = Label + Suffix;
			const FElysiumBlendGrid& Grid = Table.Grids[Label];

			// How many of the grid's cells actually resolved to a clip, which is the bake's own test
			// for whether the asset exists at all: it writes nothing for a grid that resolved fewer
			// than two, because one sample is a clip rather than a blend space. Counted BEFORE the
			// load so an absence can be judged against what was askable, not merely reported.
			int32 Resolvable = 0;
			for (const FElysiumBlendCell& Cell : Grid.Cells)
			{
				if (!Cell.Clip.IsEmpty()
					&& ElysiumNpcVisual::LoadBakedClip(Baked, Owner, Cell.Clip + Suffix) != nullptr)
				{
					++Resolvable;
				}
			}

			UBlendSpace* Space = ElysiumNpcVisual::LoadBakedBlendSpace(Baked, Owner, Label, Host);
			if (Space == nullptr)
			{
				if (Resolvable < 2)
				{
					AddInfo(FString::Printf(
						TEXT("%s grid '%s': %d of %d cell(s) resolved, so the bake wrote no space"),
						*Owner, *Shown, Resolvable, Grid.Cells.Num()));
				}
				else
				{
					AddError(FString::Printf(
						TEXT("%s grid '%s': %d cell(s) are on the mount and the space is not, so every "
						     "label that names this grid falls back to one clip"),
						*Owner, *Shown, Resolvable));
				}
				continue;
			}
			KeepAlive.Add(Space);
			++Taken;
			++BlendGrids;

			// A bank blend space is authored on its shared bank skeleton and must be compatible with
			// the body that consumes it.
			const USkeleton* BodySkeleton = Baked->GetSkeleton();
			const USkeleton* SpaceSkeleton = Space->GetSkeleton();
			if (SpaceSkeleton != BodySkeleton
				&& !(BodySkeleton != nullptr && BodySkeleton->IsCompatibleForEditor(SpaceSkeleton)))
			{
				AddError(FString::Printf(
					TEXT("%s grid '%s': skeleton %s is not compatible with body skeleton %s"),
					*Owner, *Shown,
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
						*Owner, *Shown, Axis, Grid.ParamStart[Axis], Grid.ParamEnd[Axis],
						Grid.GroupSize[Axis], Parameter.Min, Parameter.Max, Parameter.GridNum));
					bAxesSound = false;
				}
			}
			if (!bAxesSound)
			{
				continue;
			}

			// The graph is the other half of a two-axis grid's contract, and it binds by POSITION:
			// the upper-body `BlendSpacePlayer` takes `AimYaw` on X and `AimPitch` on Y, so a grid
			// whose axis 0 is the pitch parameter would steer transposed while every asset check
			// above still passed. All 49 aim grids in the corpus bind `aim_yaw` first
			// (`docs/vtmb/animation_and_movers.md` A.3), and that is what makes the pins legal.
			if (Axes == 2)
			{
				const FElysiumPoseParamDesc* AxisX = Table.Param(Grid.ParamIndex[0]);
				const FElysiumPoseParamDesc* AxisY = Table.Param(Grid.ParamIndex[1]);
				++Samples;
				if (AxisX == nullptr || AxisY == nullptr
					|| AxisX->Name != TEXT("aim_yaw") || AxisY->Name != TEXT("aim_pitch"))
				{
					AddError(FString::Printf(
						TEXT("%s grid '%s': the graph steers X with `aim_yaw` and Y with `aim_pitch`, "
						     "and this grid binds X to '%s' and Y to '%s'"),
						*Owner, *Shown,
						AxisX != nullptr ? *AxisX->Name : TEXT("nothing"),
						AxisY != nullptr ? *AxisY->Name : TEXT("nothing")));
					continue;
				}
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
				const FString Wanted =
					TEXT("A_") + FElysiumContentPaths::BakedAssetName(Cell.Clip + Suffix);
				if (ElysiumNpcVisual::LoadBakedClip(Baked, Owner, Cell.Clip + Suffix) == nullptr)
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
						*Owner, *Shown, Cell.Axis[0], Cell.Axis[1], *Expected.ToString(), *Cell.Clip));
					bSamplesSound = false;
					break;
				}
				if (Found->Animation == nullptr || Found->Animation->GetName() != Wanted)
				{
					AddError(FString::Printf(
						TEXT("%s grid '%s' cell [%d,%d]: the sidecar names '%s' and the sample at %s "
						     "carries %s"),
						*Owner, *Shown, Cell.Axis[0], Cell.Axis[1], *Wanted, *Expected.ToString(),
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
					     "sample(s)"), *Owner, *Shown, Live, SampleData.Num()));
				continue;
			}
			if (Space->GetBlendSpaceData().IsEmpty())
			{
				AddError(FString::Printf(
					TEXT("%s grid '%s': %d samples and no blend data, so it evaluates to nothing"),
					*Owner, *Shown, SampleData.Num()));
				continue;
			}

			// **The neutral.** Every pose parameter rests at zero (`FElysiumPoseParams::Neutral`), and
			// a grid has to answer that with the cell its own axes place there -- cell 4 of a nine-cell
			// `move_yaw` fan, the `CC` cell of a 3x3 aim grid. Asked of the ASSET'S OWN evaluator, not
			// of the arithmetic above: that is the path the graph takes, and a sample can sit exactly
			// where the sidecar says and still be missed at rest by an axis whose range, divisions or
			// wrap disagree. A grid with no cell at zero is not a defect -- it has nothing to assert.
			{
				FVector Neutral = FVector::ZeroVector;
				const FElysiumBlendCell* Centre = nullptr;
				for (const FElysiumBlendCell& Cell : Grid.Cells)
				{
					bool bAtZero = !Cell.Clip.IsEmpty();
					for (int32 Axis = 0; Axis < Axes && bAtZero; ++Axis)
					{
						const int32 Count = Grid.GroupSize[Axis];
						const float Alpha = Count > 1
							? static_cast<float>(Cell.Axis[Axis]) / static_cast<float>(Count - 1) : 0.f;
						const float Value = Grid.ParamStart[Axis]
							+ Alpha * (Grid.ParamEnd[Axis] - Grid.ParamStart[Axis]);
						bAtZero = FMath::IsNearlyZero(Value, static_cast<float>(GBlendSampleTolerance));
					}
					if (bAtZero)
					{
						Centre = &Cell;
						break;
					}
				}
				if (Centre != nullptr
					&& ElysiumNpcVisual::LoadBakedClip(Baked, Owner, Centre->Clip + Suffix) != nullptr)
				{
					const FString Wanted =
						TEXT("A_") + FElysiumContentPaths::BakedAssetName(Centre->Clip + Suffix);
					TArray<FBlendSampleData> Picked;
					int32 Triangulation = INDEX_NONE;
					++Samples;
					const bool bPicked = Space->GetSamplesFromBlendInput(Neutral, Picked,
						Triangulation, /*bCombineAnimations=*/true);
					const FBlendSampleData* Top = nullptr;
					for (const FBlendSampleData& Data : Picked)
					{
						if (Top == nullptr || Data.GetClampedWeight() > Top->GetClampedWeight())
						{
							Top = &Data;
						}
					}
					if (!bPicked || Top == nullptr || Top->Animation == nullptr
						|| Top->Animation->GetName() != Wanted
						|| Top->GetClampedWeight() < 1.f - GBlendSampleTolerance)
					{
						AddError(FString::Printf(
							TEXT("%s grid '%s': at rest the axes must land cell [%d,%d] ('%s') whole, and "
							     "the asset answers %s at %.3f"),
							*Owner, *Shown, Centre->Axis[0], Centre->Axis[1], *Wanted,
							Top != nullptr && Top->Animation != nullptr
								? *Top->Animation->GetName() : TEXT("nothing"),
							Top != nullptr ? Top->GetClampedWeight() : 0.f));
						continue;
					}
				}
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
						*Owner, *Shown,
						Profile.IsNone() ? TEXT("none") : *Profile.ToString(),
						Named.IsNone() ? TEXT("none") : *Named.ToString()));
					break;
				}
			}
		}
	};

	// SHARED-BANK LOCOMOTION AGREEMENT: the one check that plays a bank's clip against the BODY's
	// own baked skeleton and checks the result, rather than reading the sequence back through its
	// own -- the exact combination that broke when a fork-carrying body's synthetic root displaced
	// `Bip01` from bone 0. Every other check in this file builds its `FBoneContainer` off the
	// sequence's own skeleton and would have passed unchanged with the wrong bone carrying the
	// track; this one goes through `EvaluateOrdinaryFrame`'s arbitrary-target path, which is the
	// compatible-skeleton retargeting a live playthrough actually takes, and composes the result
	// through each side's own hierarchy so a mis-landed root shows up amplified at every bone below
	// it, the same way every other composed check here does. Returns whether a qualifying clip was
	// found and checked at all, so the caller can try another bank rather than reporting a miss.
	//
	// Every body carries its own skeleton, so this runs one bank->body `FSkeletonRemapping` pair
	// per body it is handed: over the whole cast it is the proof that the remap holds for each of
	// them individually, rather than for one representative the rest are assumed to match.
	auto CheckBankLocomotionAgreement = [&](const USkeletalMesh* Baked, const FString& Stem,
		const FElysiumSkeletalSource& Source, const FString& Bank,
		const FElysiumSkeletalSource& BankSource)
	{
		// An ordinary, full-body, ground-locomotion clip: not composed as a layer or delta
		// (`Flags & 0x4`) and not owned by a partial-body mask, so its name carries the activity it
		// plays -- the class of clip that runs constantly and is exactly what stood two
		// fork-carrying characters sideways in the street.
		const FElysiumSourceClip* Clip = BankSource.Clips.FindByPredicate(
			[&BankSource](const FElysiumSourceClip& Candidate)
			{
				return (Candidate.Flags & 0x4) == 0 && !BankSource.Masks.IsValidIndex(Candidate.Mask)
					&& Candidate.FrameCount > 0 && !Candidate.Tracks.IsEmpty()
					&& (Candidate.Name.Contains(TEXT("walk"), ESearchCase::IgnoreCase)
						|| Candidate.Name.Contains(TEXT("run"), ESearchCase::IgnoreCase));
			});
		if (Clip == nullptr)
		{
			return false;
		}
		UAnimSequence* Sequence = ElysiumNpcVisual::LoadBakedClip(Baked, Bank, Clip->Name);
		if (Sequence == nullptr)
		{
			AddInfo(FString::Printf(
				TEXT("%s: locomotion clip '%s' (bank '%s') not on the baked mount"),
				*Stem, *Clip->Name, *Bank));
			return false;
		}
		KeepAlive.Add(Sequence);
		++BankLocomotionChecks;

		const USkeleton* BankSkeleton = Sequence->GetSkeleton();
		if (BankSkeleton == nullptr)
		{
			AddError(FString::Printf(TEXT("%s: '%s' (bank '%s') has no skeleton to compose"),
				*Stem, *Clip->Name, *Bank));
			return true;
		}

		TMap<FName, int32> BodyIndexOf;
		BodyIndexOf.Reserve(Source.Bones.Num());
		for (int32 Index = 0; Index < Source.Bones.Num(); ++Index)
		{
			BodyIndexOf.Add(Source.Bones[Index].Name, Index);
		}

		const double Rate = FMath::Max(static_cast<double>(Clip->FrameRate), 1.0);
		const double Length = FMath::Max(Clip->FrameCount - 1, 1) / Rate;
		bool bSound = true;
		for (const double Fraction : GSampleFractions)
		{
			const double Time = Fraction * Length;
			TArray<FTransform> BankLocals;
			TArray<FTransform> BodyLocals;
			if (!EvaluateOrdinaryFrame(Sequence, BankSkeleton, Time, BankLocals)
				|| !EvaluateOrdinaryFrame(Sequence, Baked, Time, BodyLocals))
			{
				AddError(FString::Printf(
					TEXT("%s: '%s' (bank '%s') could not be composed against both skeletons"),
					*Stem, *Clip->Name, *Bank));
				bSound = false;
				break;
			}

			// Each side composed through its OWN hierarchy -- the bank's own bone tree for the
			// bank-skeleton evaluation, the body's own for the retargeted one -- and matched by
			// bone NAME, because the fork this guards against is precisely a disagreement about
			// which bone occupies which INDEX.
			//
			// `EvaluateOrdinaryFrame` hands back locals indexed by the TARGET asset's reference
			// skeleton. For the body that order IS the container's, because a body's mesh is
			// authored from its own container. The bank-family skeleton is a union over every
			// member bank, ordered by whichever container declared a bone first -- so its indices
			// agree with THIS container's only for the family's first-declared member, and
			// composing skeleton-indexed locals with container indices reads one bone's animation
			// as another's. Reindexed by name before composing, for both sides, because the name
			// is the only join the orders share.
			const FReferenceSkeleton& BankRef = BankSkeleton->GetReferenceSkeleton();
			const FReferenceSkeleton& BodyRef = Baked->GetRefSkeleton();
			TArray<FTransform> BankByContainer;
			TArray<FTransform> BodyByContainer;
			BankByContainer.SetNum(BankSource.Bones.Num());
			BodyByContainer.SetNum(Source.Bones.Num());
			bool bIndexed = true;
			for (int32 Index = 0; Index < BankSource.Bones.Num() && bIndexed; ++Index)
			{
				const int32 At = BankRef.FindBoneIndex(BankSource.Bones[Index].Name);
				bIndexed = At != INDEX_NONE && BankLocals.IsValidIndex(At);
				if (bIndexed)
				{
					BankByContainer[Index] = BankLocals[At];
				}
			}
			for (int32 Index = 0; Index < Source.Bones.Num() && bIndexed; ++Index)
			{
				const int32 At = BodyRef.FindBoneIndex(Source.Bones[Index].Name);
				bIndexed = At != INDEX_NONE && BodyLocals.IsValidIndex(At);
				if (bIndexed)
				{
					BodyByContainer[Index] = BodyLocals[At];
				}
			}
			if (!bIndexed)
			{
				AddError(FString::Printf(
					TEXT("%s: '%s' (bank '%s') has a container bone its baked skeleton does not ")
					TEXT("carry, so the composed comparison cannot be indexed"),
					*Stem, *Clip->Name, *Bank));
				bSound = false;
				break;
			}
			TArray<FTransform> BankComposed;
			TArray<FTransform> BodyComposed;
			ComposeComponentSpace(BankSource.Bones, BankByContainer, BankComposed);
			ComposeComponentSpace(Source.Bones, BodyByContainer, BodyComposed);

			for (int32 BankIndex = 0; BankIndex < BankSource.Bones.Num(); ++BankIndex)
			{
				const int32* BodyIndex = BodyIndexOf.Find(BankSource.Bones[BankIndex].Name);
				if (BodyIndex == nullptr)
				{
					// A bank drives bones this body has never had; there is nothing on the body's
					// own hierarchy to compare that bone against.
					continue;
				}
				++Samples;
				const double Degrees = FMath::RadiansToDegrees(
					BodyComposed[*BodyIndex].GetRotation().AngularDistance(
						BankComposed[BankIndex].GetRotation()));
				const double Centimetres = FVector::Distance(
					BodyComposed[*BodyIndex].GetTranslation(),
					BankComposed[BankIndex].GetTranslation());
				if (Degrees > GBankLocomotionRotationToleranceDeg
					|| Centimetres > GBankLocomotionTranslationTolerance)
				{
					AddError(FString::Printf(
						TEXT("%s plays bank '%s' clip '%s': through the body's own baked skeleton ")
						TEXT("bone '%s' composes %.2f deg / %.2f cm away from the same clip composed ")
						TEXT("on the bank's own skeleton -- a shared-bank clip is landing on the ")
						TEXT("wrong bone"),
						*Stem, *Bank, *Clip->Name, *BankSource.Bones[BankIndex].Name.ToString(),
						Degrees, Centimetres));
					bSound = false;
					break;
				}
			}
			if (!bSound)
			{
				break;
			}
		}
		return true;
	};

	bool bIsDefaultSlice = false;
	const TArray<FString> Stems = SliceStems(bIsDefaultSlice);
	for (const FString& Stem : Stems)
	{
		USkeletalMesh* Baked = ElysiumNpcVisual::LoadBakedMesh(Stem);
		if (Baked == nullptr)
		{
			AddInfo(FString::Printf(TEXT("skipping %s: not on the baked mount"), *Stem));
			continue;
		}
		// The container the bake read. It states the bind pose and every clip key in Unreal space
		// with no import step to disagree about, which makes it the reference the bake is held to
		// -- and, since nothing else builds a character, the only one.
		FElysiumSkeletalSource Source;
		if (!FElysiumSkeletalSource::Load(FElysiumContentPaths::NpcSource(Stem), Source, Error))
		{
			AddInfo(FString::Printf(TEXT("%s: no .eskm to check against (%s)"), *Stem, *Error));
			continue;
		}
		KeepAlive.Add(Baked);
		++Compared;

		// The body's own bone set, in its own order. The mesh's reference skeleton and the
		// `USkeleton`'s are two different poses -- the mesh keeps the exact authored bind, the
		// `USkeleton` carries the rotation-neutral compatibility frame -- so this asserts the mesh
		// against the container rather than against the skeleton it binds to.
		const FReferenceSkeleton& BakedRef = Baked->GetRefSkeleton();
		if (!TestEqual(FString::Printf(TEXT("%s bone count"), *Stem),
			BakedRef.GetNum(), Source.Bones.Num()))
		{
			continue;
		}

		TMap<FName, int32> BakedIndexOf;
		BakedIndexOf.Reserve(BakedRef.GetNum());
		for (int32 BoneIndex = 0; BoneIndex < BakedRef.GetNum(); ++BoneIndex)
		{
			BakedIndexOf.Add(BakedRef.GetBoneName(BoneIndex), BoneIndex);
		}

		// The rest pose is asserted against the CONTAINER, bone for bone: the bake writes the
		// file's own Unreal-space locals, so the thing worth asserting is that it reproduced what
		// it read.
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

		// The face is a curve interface, so its whole contract on this seam is that the same
		// targets exist and the skeleton flags them as morph-target curves.
		const TSet<FName> BakedMorphs = MorphTargetNames(Baked);
		TSet<FName> SourceMorphs;
		for (const FElysiumSourceMorph& Morph : Source.Morphs)
		{
			SourceMorphs.Add(FName(*Morph.Name));
		}
		TestEqual(FString::Printf(TEXT("%s morph target count"), *Stem),
			BakedMorphs.Num(), SourceMorphs.Num());
		for (const FName& Name : SourceMorphs)
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

			// The one check that plays a shared bank's clip against THIS body's own baked skeleton
			// rather than reading it back through the bank's own -- sorted for a deterministic
			// pick, and stopping at the first bank that actually carries an ordinary walk/run clip
			// so a body playing several banks does not pay for loading all of their containers.
			TArray<FString> SortedGridOwners = GridOwners.Array();
			SortedGridOwners.Sort();
			bool bLocomotionChecked = false;
			for (const FString& Bank : SortedGridOwners)
			{
				FElysiumSkeletalSource BankSourceForPose;
				FString BankPoseError;
				if (!FElysiumSkeletalSource::Load(
					FElysiumContentPaths::NpcBankSource(Bank), BankSourceForPose, BankPoseError))
				{
					continue;
				}
				if (CheckBankLocomotionAgreement(Baked, Stem, Source, Bank, BankSourceForPose))
				{
					bLocomotionChecked = true;
					break;
				}
			}
			if (!bLocomotionChecked)
			{
				if (bIsDefaultSlice)
				{
					// A silent pass here is the exact failure mode that let the fork defect ship: a
					// stem in the DEFAULT slice is there because it is known to need this coverage,
					// so a resolution miss is a hole in the check itself and must fail loudly rather
					// than melt into an abstain the way an ad-hoc `-ElysiumParityStems=` stem may.
					AddError(FString::Printf(
						TEXT("%s: no ordinary bank locomotion clip could be resolved for this stem -- ")
						TEXT("it is in the default slice because its skeleton is known to fork, and a ")
						TEXT("miss here means this check is not covering it"), *Stem));
				}
				else
				{
					AddInfo(FString::Printf(
						TEXT("%s: no ordinary walk/run bank clip found to check pose agreement"), *Stem));
				}
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
					// authored the clip, which is what the sequence carries.
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
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no slice model is baked; run: uv run elysium export characters"));
		return true;
	}
	AddInfo(FString::Printf(TEXT("%d model(s) compared, %d bone samples, %d `_delta` clip(s) ")
		TEXT("round-tripped through the additive bake, %d masked `_layer` clip(s) checked against ")
		TEXT("their blend masks, %d blend grid(s) checked against their sidecar, %d shared-bank ")
		TEXT("locomotion clip(s) checked against the body's own baked skeleton"),
		Compared, Samples, AdditiveClips, LayerClips, BlendGrids, BankLocomotionChecks));
	// A wholesale resolution failure -- every stem's bank search coming up empty -- must never
	// present as a clean run just because nothing individually errored above.
	TestTrue(TEXT("at least one shared-bank locomotion clip was checked against a body's own ")
		TEXT("baked skeleton"), BankLocomotionChecks > 0);
	return true;
}


// The arming path a layer actually takes, end to end, with nothing standing.
//
// `UElysiumEntityBodies::PlayNpcLayer` turns a LABEL into the two things the graph's layered blend
// needs — an overlay asset and the name of the bone mask it composes under — through four lookups
// in a row: the body's vocabulary, the declaring host in the owner's autolayer table, the derived
// `<label>@<host>` asset on the mount, and the mask profile on the PLAYING body's skeleton. Any one
// of them missing refuses the whole arm, and in game all four failures read as one thing: the
// button does nothing.
//
// So this walks the same four steps over the real mount and says which one broke. It is the only
// place that can: the runtime path needs a compiled graph, a standing body and a drive-mode pawn
// before it reaches the first lookup, and none of those are what fails.
//
// The work list is DERIVED, not named here. Every label an autolayer table binds to a host is a
// label the resolver can produce and the lab can be asked for, so the set follows whatever the
// export wrote rather than a list that drifts from it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumUpperBodyLayerArmingTest,
	"Elysium.Content.UpperBodyLayerArming", GElysiumBakedCharacterFlags)
bool FElysiumUpperBodyLayerArmingTest::RunTest(const FString&)
{
	const TArray<FString> Stems = SliceStems();
	TArray<UObject*> KeepAlive;
	int32 Bodies = 0;
	int32 Grids = 0;
	int32 Additives = 0;
	int32 MaskedClips = 0;
	// Overlays that got all the way through step 4 — the count the summary line is about.
	int32 Armable = 0;

	for (const FString& Stem : Stems)
	{
		USkeletalMesh* Mesh = ElysiumNpcVisual::LoadBakedMesh(Stem);
		FElysiumNpcClipSet Clips;
		FString ClipError;
		if (Mesh == nullptr || !Clips.Load(Stem, ClipError))
		{
			continue;
		}
		USkeleton* BodySkeleton = Mesh->GetSkeleton();
		if (BodySkeleton == nullptr)
		{
			AddError(FString::Printf(TEXT("%s: the baked body carries no skeleton"), *Stem));
			continue;
		}
		KeepAlive.Add(Mesh);
		++Bodies;

		// Every bank this body plays through, plus its own container. An autolayer table ships in
		// whichever sidecar declares the binding — the `move_and_ranged` banks carry all of them
		// today — so the set has to be gathered from the vocabulary rather than assumed.
		TSet<FString> Owners;
		Owners.Add(Stem);
		for (const TPair<FString, FElysiumNpcClip>& Entry : Clips.Clips)
		{
			Owners.Add(Entry.Value.Owner);
		}

		// Label -> the hosts declaring it, sorted. The lab's runtime host is the standing
		// sequence (table fallback only when nothing is standing); this census still walks
		// the first sorted host so a mount that ships any derived form of the label is counted.
		TMap<FString, TArray<FString>> HostsByLabel;
		TArray<FString> OwnerList = Owners.Array();
		OwnerList.Sort();
		for (const FString& Owner : OwnerList)
		{
			FElysiumBlendTable Table;
			FString TableError;
			if (!Table.Load(FString::Printf(TEXT("blends/%s.json"), *Owner), TableError))
			{
				// Most owners declare no grid and ship no sidecar at all, which is an ordinary load.
				continue;
			}
			for (const TPair<FString, FElysiumAutoLayerBinding>& Binding : Table.AutoLayers)
			{
				for (const FString& Target : Binding.Value.Clips)
				{
					HostsByLabel.FindOrAdd(Target).AddUnique(Binding.Key);
				}
			}
		}
		if (HostsByLabel.IsEmpty())
		{
			AddInfo(FString::Printf(
				TEXT("%s: no owner in its vocabulary declares an autolayer binding, so no label can "
				     "resolve as a layer"), *Stem));
			continue;
		}

		TArray<FString> Labels;
		HostsByLabel.GetKeys(Labels);
		Labels.Sort([](const FString& A, const FString& B) { return A < B; });

		for (const FString& Label : Labels)
		{
			TArray<FString>& Hosts = HostsByLabel[Label];
			Hosts.Sort([](const FString& A, const FString& B) { return A < B; });
			const FString& Host = Hosts[0];

			// Step 1 — the vocabulary. A label bound by a table the body can reach and absent from
			// the body's own clip map is not a missing asset; it is the two sidecars disagreeing,
			// and the arm refuses before it ever looks at the mount.
			const FElysiumNpcClip* LayerClip = Clips.Find(Label);
			if (LayerClip == nullptr)
			{
				AddError(FString::Printf(
					TEXT("%s: '%s' is bound as a layer of '%s' and is not in the body's vocabulary "
					     "(%d clips), so nothing can arm it"),
					*Stem, *Label, *Host, Clips.Clips.Num()));
				continue;
			}
			const FString LayerOwner = LayerClip->IsOwnedBy(Stem) ? Stem : LayerClip->Owner;

			// Step 2/3 — the asset. A grid stands as a blend space and a melee overlay as a plain
			// sequence, decided the same way the arm decides it: does the label name a grid.
			FName MaskName;
			const TCHAR* Kind = nullptr;
			// The skeleton the OVERLAY ASSET is bound to, which for a bank-owned layer is the
			// bank's rather than the body's. Carried so a missing mask can name which of the two
			// carries it — "the profile is on the wrong skeleton" and "the bake never wrote it"
			// are the same silence otherwise, and they are different repairs.
			const USkeleton* AssetSkeleton = nullptr;
			if (UBlendSpace* Space =
				ElysiumNpcVisual::LoadBakedBlendSpace(Mesh, LayerOwner, Label, Host))
			{
				KeepAlive.Add(Space);
				AssetSkeleton = Space->GetSkeleton();
				++Grids;
				Kind = TEXT("grid");
				for (const FBlendSample& Sample : Space->GetBlendSamples())
				{
					if (Sample.Animation != nullptr)
					{
						if (const UElysiumAnimLayerMask* Mask =
							Sample.Animation->FindMetaDataByClass<UElysiumAnimLayerMask>())
						{
							MaskName = Mask->Profile;
							break;
						}
					}
				}
				if (MaskName.IsNone())
				{
					AddError(FString::Printf(
						TEXT("%s: grid '%s@%s' stands and no cell of it names a blend mask, so it "
						     "would compose over the whole rig and lose the body's stance"),
						*Stem, *Label, *Host));
					continue;
				}
			}
			else
			{
				// The derived form first and the plain label second, for the reason the arm does it
				// in that order: an additive ships both ways and a mask-free overlay only plain.
				UAnimSequence* Anim = ElysiumNpcVisual::LoadBakedClip(Mesh, LayerOwner,
					FString::Printf(TEXT("%s@%s"), *Label, *Host));
				if (Anim == nullptr)
				{
					Anim = ElysiumNpcVisual::LoadBakedClip(Mesh, LayerOwner, Label);
				}
				if (Anim == nullptr)
				{
					AddError(FString::Printf(
						TEXT("%s: '%s' is owned by '%s' and neither its grid, its derived form "
						     "'%s@%s' nor its plain label is on the mount"),
						*Stem, *Label, *LayerOwner, *Label, *Host));
					continue;
				}
				KeepAlive.Add(Anim);
				AssetSkeleton = Anim->GetSkeleton();
				if (Anim->IsValidAdditive())
				{
					++Additives;
					// An additive states its own base through the additive identity and composes in
					// the other slot, which takes no mask. Nothing further to resolve.
					continue;
				}
				const UElysiumAnimLayerMask* Mask =
					Anim->FindMetaDataByClass<UElysiumAnimLayerMask>();
				if (Mask == nullptr)
				{
					AddError(FString::Printf(
						TEXT("%s: '%s' stands and is neither additive nor masked — an unmasked pose "
						     "clip cannot be a layer, it would drag every bone it does not own to "
						     "the reference pose"), *Stem, *Label));
					continue;
				}
				++MaskedClips;
				Kind = TEXT("clip");
				MaskName = Mask->Profile;
			}

			// Step 4 — the mask, resolved against the PLAYING body's skeleton rather than the one
			// the layer's own bank was baked against. Those are different assets whenever the layer
			// comes off a bank, and a profile taken from the wrong one gates a shifted set of bones
			// and logs nothing, so the name is what travels and this is where it has to land.
			const UBlendProfile* Profile = BodySkeleton->GetBlendProfile(MaskName);
			if (Profile == nullptr)
			{
				USkeleton* Bound = const_cast<USkeleton*>(AssetSkeleton);
				const bool bOnAsset = Bound != nullptr && Bound != BodySkeleton
					&& Bound->GetBlendProfile(MaskName) != nullptr;
				AddError(FString::Printf(
					TEXT("%s: %s '%s' names blend mask '%s', which %s does not carry — the layer "
					     "would compose unmasked over the whole rig (the asset is bound to %s, "
					     "which %s carry it)"),
					*Stem, Kind, *Label, *MaskName.ToString(), *BodySkeleton->GetName(),
					Bound != nullptr ? *Bound->GetName() : TEXT("no skeleton"),
					bOnAsset ? TEXT("DOES") : TEXT("does not")));
				continue;
			}
			// `BlendMask` mode is load-bearing, not cosmetic: it is the only mode whose unwritten
			// default is 0, and a profile left in any other mode reads at evaluation as owning
			// every bone — the exact opposite of the gate that was asked for.
			if (Profile->Mode != EBlendProfileMode::BlendMask)
			{
				AddError(FString::Printf(
					TEXT("%s: blend mask '%s' is in mode %d rather than BlendMask, so every bone it "
					     "does not name defaults to 1 and it gates nothing"),
					*Stem, *MaskName.ToString(), static_cast<int32>(Profile->Mode)));
				continue;
			}
			const int32 Entries = Profile->GetNumBlendEntries();
			const int32 Bones = BodySkeleton->GetReferenceSkeleton().GetNum();
			if (Entries <= 0 || Entries >= Bones)
			{
				AddError(FString::Printf(
					TEXT("%s: blend mask '%s' gates %d of %d bone(s), which is not a partial gate — "
					     "an upper-body overlay that owns none poses nothing and one that owns all "
					     "replaces the base"),
					*Stem, *MaskName.ToString(), Entries, Bones));
			}
			++Armable;
		}
	}

	if (Bodies == 0)
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no baked body on the mount; "
			"run: uv run elysium export characters"));
		return true;
	}
	AddInfo(FString::Printf(
		TEXT("%d body/bodies, %d layer(s) armable: %d grid(s), %d masked clip(s), %d additive(s)"),
		Bodies, Armable + Additives, Grids, MaskedClips, Additives));
	return true;
}


#endif // WITH_DEV_AUTOMATION_TESTS
