// Deterministic pose envelopes for the opening embrace. The structural GLB tests prove that files
// are finite and bindable; this test evaluates the actual UAnimSequence on each target skeleton so
// a generic retargeter cannot silently turn a valid 1-3 m biped into an exploded 4-8 m pose.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumContentPaths.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumSkeletalBasis.h"
#include "Visual/ElysiumNpcVisual.h"

#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"

static constexpr EAutomationTestFlags GElysiumSkeletalPoseFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	struct FOpeningPoseCase
	{
		const TCHAR* Label;
		const TCHAR* MeshStem;
		const TCHAR* BankStem;
	};

	const FOpeningPoseCase GOpeningPoseCases[] =
	{
		{ TEXT("player/Bip01"), TEXT("brujah_male_armor_0"),
			TEXT("cinematic_santa_monica_haven_embrace_bips1__bip01") },
		{ TEXT("Sire/Bip02"), TEXT("brujah_female_armor_3"),
			TEXT("cinematic_santa_monica_haven_embrace_bips1__bip02") },
		{ TEXT("Vampire1/Bip03"), TEXT("malkavian_male_armor_0"),
			TEXT("cinematic_santa_monica_haven_embrace_bips1__bip03") },
		{ TEXT("Vampire2/Bip04"), TEXT("toreador_male_armor_0"),
			TEXT("cinematic_santa_monica_haven_embrace_bips1__bip04") },
		{ TEXT("Sheriff/Bip05"), TEXT("sheriff"),
			TEXT("cinematic_santa_monica_haven_embrace_bips2__bip05") },
	};

	float RootRelativeRadius(const TArray<FTransform>& ComponentPose)
	{
		if (ComponentPose.IsEmpty())
		{
			return 0.f;
		}
		const FTransform& Root = ComponentPose[0];
		float Radius = 0.f;
		for (const FTransform& Bone : ComponentPose)
		{
			Radius = FMath::Max(Radius,
				Root.InverseTransformPosition(Bone.GetTranslation()).Size());
		}
		return Radius;
	}

	void BuildComponentPose(const FReferenceSkeleton& Ref, IAnimationDataModel& Model,
		const TSet<FName>& AnimatedTracks, int32 Frame, TArray<FTransform>& Out)
	{
		Out = Ref.GetRefBonePose();
		for (int32 BoneIndex = 0; BoneIndex < Ref.GetNum(); ++BoneIndex)
		{
			const FName BoneName = Ref.GetBoneName(BoneIndex);
			if (AnimatedTracks.Contains(BoneName))
			{
				Out[BoneIndex] = Model.GetBoneTrackTransform(BoneName, FFrameNumber(Frame));
			}
			const int32 ParentIndex = Ref.GetParentIndex(BoneIndex);
			if (ParentIndex != INDEX_NONE)
			{
				Out[BoneIndex] *= Out[ParentIndex];
			}
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumOpeningPoseEnvelopeTest,
	"Elysium.Content.OpeningPoseEnvelope", GElysiumSkeletalPoseFlags)

bool FElysiumOpeningPoseEnvelopeTest::RunTest(const FString&)
{
	if (FElysiumContentPaths::IsIncomplete(TEXT("npc")))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: the npc export domain(s) are marked incomplete"));
		return true;
	}
	if (!IFileManager::Get().FileExists(*FElysiumContentPaths::NpcIndex()))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: NPC export is absent"));
		return true;
	}

	bool bValid = true;
	int32 Evaluated = 0;
	for (const FOpeningPoseCase& PoseCase : GOpeningPoseCases)
	{
		// Both sides off the baked mount: the body and the performance have to be in one frame for
		// a pose envelope to mean anything, and the mount is the only build of either.
		//
		// A missing mesh or clip here is the ordinary partial-bake state (`uv run elysium export
		// characters` covers the cast incrementally) rather than a defect, so it is skipped rather
		// than failed. See the tail of this test for what happens when every case skips.
		USkeletalMesh* Mesh = ElysiumNpcVisual::LoadBakedMesh(PoseCase.MeshStem);
		if (Mesh == nullptr)
		{
			AddInfo(FString::Printf(TEXT("%s: '%s' is not on the baked mount"),
				PoseCase.Label, PoseCase.MeshStem));
			continue;
		}
		Mesh->AddToRoot();

		UAnimSequence* Anim = ElysiumNpcVisual::LoadBakedClip(Mesh, PoseCase.BankStem,
			TEXT("entire_scene"));
		if (Anim == nullptr)
		{
			AddInfo(FString::Printf(TEXT("%s: '%s' carries no baked entire_scene"),
				PoseCase.Label, PoseCase.BankStem));
			Mesh->RemoveFromRoot();
			continue;
		}
		Anim->AddToRoot();

		IAnimationDataModel* Model = Anim->GetDataModel();
		const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
		if (Model == nullptr || Ref.GetNum() == 0)
		{
			AddError(FString::Printf(TEXT("%s: no animation model or target skeleton"), PoseCase.Label));
			bValid = false;
		}
		else
		{
			// Guards a Spine1-only failure where the pelvis and legs remain seated while an
			// exporter-specific quaternion transform reverses or inverts the upper body -- a class of
			// defect the per-bone NaN/rotation-normalization check below cannot see on its own,
			// because an inverted upper body is still finite and still rotation-normalized.
			//
			// Distance only. There is deliberately NO check on which way the upper body points.
			//
			// A retired standalone test (`Elysium.Content.CourtroomSeatedPose`) asserted that the
			// head stays above the pelvis, having sampled that target from one seated courtroom body
			// and promoted it to a retail oracle; it was withdrawn for exactly that reason. Both
			// obvious generalizations fail against this scene's real content: component +Z assumes a
			// world-up-aligned root, which is a property of the root bone (`Bip01` on most of the
			// cast, `Bip02` on the Sire) and not a fact about the cast; and the body's own bind pose
			// fails too, because the opening IS an embrace -- the player and the Sire go down, and a
			// reversed head-from-pelvis is the authored pose rather than a defect.
			//
			// So the Spine1-only inversion class this once guarded has no content-independent
			// geometric oracle here, and asserting one would only re-import the withdrawn claim.
			// Catching it needs a rendered retail invariant, which this test does not attempt.
			const int32 Pelvis = Ref.FindBoneIndex(FName(TEXT("Bip01 Pelvis")));
			const int32 Head = Ref.FindBoneIndex(FName(TEXT("Bip01 Head")));
			if (Pelvis == INDEX_NONE || Head == INDEX_NONE)
			{
				AddError(FString::Printf(TEXT("%s: no Bip01 Pelvis / Bip01 Head"), PoseCase.Label));
				bValid = false;
			}

			TArray<FName> TrackNames;
			Model->GetBoneTrackNames(TrackNames);
			TSet<FName> AnimatedTracks(TrackNames);
			const int32 LastFrame = Model->GetNumberOfFrames();

			TArray<FTransform> ReferenceComponent = Ref.GetRefBonePose();
			for (int32 BoneIndex = 0; BoneIndex < Ref.GetNum(); ++BoneIndex)
			{
				const int32 ParentIndex = Ref.GetParentIndex(BoneIndex);
				if (ParentIndex != INDEX_NONE)
				{
					ReferenceComponent[BoneIndex] *= ReferenceComponent[ParentIndex];
				}
			}
			const float ReferenceRadius = RootRelativeRadius(ReferenceComponent);
			const float RadiusLimit = FMath::Max(350.f, ReferenceRadius * 2.25f);

			float MaxPoseRadius = 0.f;
			float MaxRootDistance = 0.f;
			float MaxLocalSegment = 0.f;
			float MinHeadFromPelvis = TNumericLimits<float>::Max();
			float MaxHeadFromPelvis = 0.f;
			for (const int32 Frame : { 0, LastFrame / 4, LastFrame / 2,
				(LastFrame * 3) / 4, LastFrame })
			{
				TArray<FTransform> ComponentPose;
				BuildComponentPose(Ref, *Model, AnimatedTracks, Frame, ComponentPose);
				for (int32 BoneIndex = 0; BoneIndex < ComponentPose.Num(); ++BoneIndex)
				{
					if (ComponentPose[BoneIndex].ContainsNaN()
						|| !ComponentPose[BoneIndex].IsRotationNormalized())
					{
						AddError(FString::Printf(TEXT("%s: invalid bone %s at frame %d"),
							PoseCase.Label, *Ref.GetBoneName(BoneIndex).ToString(), Frame));
						bValid = false;
					}
					if (BoneIndex > 0)
					{
						const FTransform Local = AnimatedTracks.Contains(Ref.GetBoneName(BoneIndex))
							? Model->GetBoneTrackTransform(Ref.GetBoneName(BoneIndex), FFrameNumber(Frame))
							: Ref.GetRefBonePose()[BoneIndex];
						MaxLocalSegment = FMath::Max(MaxLocalSegment, Local.GetTranslation().Size());
					}
				}
				if (Pelvis != INDEX_NONE && Head != INDEX_NONE)
				{
					const FVector HeadFromPelvis =
						ComponentPose[Head].GetTranslation() - ComponentPose[Pelvis].GetTranslation();
					TestTrue(FString::Printf(TEXT("%s: frame %d keeps the upper body at human scale"),
							PoseCase.Label, Frame),
						HeadFromPelvis.Size() > 20.0f && HeadFromPelvis.Size() < 90.0f);
					MinHeadFromPelvis = FMath::Min(MinHeadFromPelvis, HeadFromPelvis.Size());
					MaxHeadFromPelvis = FMath::Max(MaxHeadFromPelvis, HeadFromPelvis.Size());
				}
				MaxPoseRadius = FMath::Max(MaxPoseRadius, RootRelativeRadius(ComponentPose));
				MaxRootDistance = FMath::Max(MaxRootDistance,
					ComponentPose[0].GetTranslation().Size());
			}

			if (MaxPoseRadius > RadiusLimit)
			{
				AddError(FString::Printf(TEXT("%s: exploded pose radius %.1f cm exceeds %.1f cm "
					"(reference %.1f cm)"), PoseCase.Label, MaxPoseRadius, RadiusLimit,
					ReferenceRadius));
				bValid = false;
			}
			if (MaxRootDistance > 900.f)
			{
				AddError(FString::Printf(TEXT("%s: authored root placement %.1f cm exceeds scene envelope"),
					PoseCase.Label, MaxRootDistance));
				bValid = false;
			}
			if (MaxLocalSegment > 200.f)
			{
				AddError(FString::Printf(TEXT("%s: non-root segment %.1f cm exceeds biped envelope"),
					PoseCase.Label, MaxLocalSegment));
				bValid = false;
			}
			if (Pelvis != INDEX_NONE && Head != INDEX_NONE)
			{
				AddInfo(FString::Printf(TEXT("%s: reference radius %.1f cm, pose %.1f cm, "
					"root %.1f cm, longest local %.1f cm, head-pelvis %.1f-%.1f cm"), PoseCase.Label,
					ReferenceRadius, MaxPoseRadius, MaxRootDistance, MaxLocalSegment,
					MinHeadFromPelvis, MaxHeadFromPelvis));
			}
			else
			{
				AddInfo(FString::Printf(TEXT("%s: reference radius %.1f cm, pose %.1f cm, "
					"root %.1f cm, longest local %.1f cm"), PoseCase.Label, ReferenceRadius,
					MaxPoseRadius, MaxRootDistance, MaxLocalSegment));
			}
			++Evaluated;
		}

		Anim->RemoveFromRoot();
		Mesh->RemoveFromRoot();
	}

	if (Evaluated == 0)
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no opening body is baked; run: uv run elysium export characters"));
		return true;
	}
	// A partial bake is the ordinary development state (`uv run elysium export characters` covers
	// the cast incrementally), so the count is reported rather than asserted against the full five;
	// only a wholesale failure -- nothing certified at all -- is a suite-level problem, handled by
	// the abstain above.
	AddInfo(FString::Printf(TEXT("%d of %d opening bodies certified"), Evaluated,
		static_cast<int32>(UE_ARRAY_COUNT(GOpeningPoseCases))));
	TestTrue(TEXT("opening poses stay inside their skeletal envelopes"), bValid);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumOpeningScenePlacementTest,
	"Elysium.Content.OpeningScenePlacement", GElysiumSkeletalPoseFlags)

bool FElysiumOpeningScenePlacementTest::RunTest(const FString&)
{
	if (FElysiumContentPaths::IsIncomplete(TEXT("maps"))
		|| FElysiumContentPaths::IsIncomplete(TEXT("npc")))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: the maps and npc export domain(s) are marked incomplete"));
		return true;
	}
	const FString EntsPath = FElysiumContentPaths::MapEnts(TEXT("sp_theatre"));
	if (!IFileManager::Get().FileExists(*EntsPath)
		|| !IFileManager::Get().FileExists(*FElysiumContentPaths::NpcIndex()))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: theatre or NPC export is absent"));
		return true;
	}

	FElysiumEntityDefs Defs;
	if (!TestTrue(TEXT("sp_theatre entities parse"), FElysiumEntityDefs::Parse(EntsPath, Defs)))
	{
		return true;
	}
	const FElysiumEntityDef* Scene = nullptr;
	const FElysiumEntityDef* Target = nullptr;
	for (const FElysiumEntityDef& Def : Defs.Defs)
	{
		if (Def.TargetName.Equals(TEXT("embrace_o_matic"), ESearchCase::IgnoreCase))
		{
			Scene = &Def;
		}
		else if (Def.TargetName.Equals(TEXT("embrace_target_5"), ESearchCase::IgnoreCase))
		{
			Target = &Def;
		}
	}
	if (!TestNotNull(TEXT("embrace scene entity exists"), Scene)
		|| !TestNotNull(TEXT("the post-7.5s target key exists"), Target))
	{
		return true;
	}

	FVector SourceAngles = FVector::ZeroVector;
	TArray<FString> AngleParts;
	Scene->Keys.FindRef(TEXT("angles")).ParseIntoArrayWS(AngleParts);
	if (AngleParts.Num() >= 3)
	{
		SourceAngles = FVector(FCString::Atof(*AngleParts[0]), FCString::Atof(*AngleParts[1]),
			FCString::Atof(*AngleParts[2]));
	}
	TestTrue(TEXT("the theatre scene keeps its authored 270 degree yaw"),
		FMath::IsNearlyEqual(SourceAngles.Y, 270.0f));

	// Both sides off the baked mount. A clip read out of a `.glb` arrives in glTFRuntime's basis --
	// `(y, x, z)` against the container's `(x, -y, z)`, a 90 degree yaw carried on the `Bip01` root
	// -- and needs a compensating quarter turn at placement. Nothing loads a `.glb`; the placement
	// is the reflected Source yaw and nothing else (`ElysiumSkeletalBasis`).
	USkeletalMesh* Mesh = ElysiumNpcVisual::LoadBakedMesh(TEXT("brujah_male_armor_0"));
	UAnimSequence* Anim = Mesh != nullptr
		? ElysiumNpcVisual::LoadBakedClip(Mesh,
			TEXT("cinematic_santa_monica_haven_embrace_bips1__bip01"), TEXT("entire_scene"))
		: nullptr;
	if (!TestNotNull(TEXT("player baked mesh loads for placement probe"), Mesh)
		|| !TestNotNull(TEXT("entire_scene is on the mount for placement probe"), Anim))
	{
		return true;
	}
	Mesh->AddToRoot();
	Anim->AddToRoot();

	IAnimationDataModel* Model = Anim->GetDataModel();
	const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
	// Resolved by NAME, not index 0: a skeleton whose VtMB tree forks carries a non-`Bip01` bone
	// at index 0, and reading that as the root would measure the wrong bone
	// (`ElysiumCogWindow_GreenRoom::ScanRootMotion` resolves the same way).
	static const FName RootBone(TEXT("Bip01"));
	if (TestNotNull(TEXT("placement clip has an animation model"), Model)
		&& TestTrue(TEXT("placement target skeleton carries a 'Bip01' root"),
			Ref.FindBoneIndex(RootBone) != INDEX_NONE)
		&& TestTrue(TEXT("placement clip reaches 7.5 seconds"), Model->GetNumberOfFrames() >= 225))
	{
		const FVector RootLocal = Model->GetBoneTrackTransform(
			RootBone, FFrameNumber(225)).GetTranslation();
		// The baked basis: a character's authored forward is its own component +X, so the placement
		// is the reflected Source yaw and nothing else.
		const FVector CorrectWorld = Scene->Origin
			+ ElysiumSkeletalBasis::FromSourceAngles(SourceAngles).RotateVector(RootLocal);
		// The same placement with the retired glb correction still on it -- the quarter turn this
		// test exists to catch, stated here rather than through a helper because nothing in the
		// runtime carries one any more.
		const FRotator QuarterTurned(0.0f, -90.0f - SourceAngles.Y, 0.0f);
		const FVector QuarterTurnedWorld = Scene->Origin + QuarterTurned.RotateVector(RootLocal);
		const float CorrectDistance = FVector::Distance(CorrectWorld, Target->Origin);
		const float QuarterTurnedDistance = FVector::Distance(QuarterTurnedWorld, Target->Origin);
		TestTrue(TEXT("the baked player root lands inside the authored 7.5s shot envelope"),
			CorrectDistance < 150.0f);
		TestTrue(TEXT("adding the glb quarter-turn misses that shot by over three metres"),
			QuarterTurnedDistance > 300.0f);
		AddInfo(FString::Printf(TEXT("7.5s placement: baked %.1f cm, quarter-turned %.1f cm"),
			CorrectDistance, QuarterTurnedDistance));
	}

	Anim->RemoveFromRoot();
	Mesh->RemoveFromRoot();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
