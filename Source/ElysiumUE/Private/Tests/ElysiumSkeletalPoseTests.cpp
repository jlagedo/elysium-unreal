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
		USkeletalMesh* Mesh = ElysiumNpcVisual::LoadBakedMesh(PoseCase.MeshStem);
		if (Mesh == nullptr)
		{
			AddError(FString::Printf(TEXT("%s: '%s' is not on the baked mount"),
				PoseCase.Label, PoseCase.MeshStem));
			bValid = false;
			continue;
		}
		Mesh->AddToRoot();

		UAnimSequence* Anim = ElysiumNpcVisual::LoadBakedClip(Mesh, PoseCase.BankStem,
			TEXT("entire_scene"));
		if (Anim == nullptr)
		{
			AddError(FString::Printf(TEXT("%s: '%s' carries no baked entire_scene"),
				PoseCase.Label, PoseCase.BankStem));
			Mesh->RemoveFromRoot();
			bValid = false;
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
			AddInfo(FString::Printf(TEXT("%s: reference radius %.1f cm, pose %.1f cm, "
				"root %.1f cm, longest local %.1f cm"), PoseCase.Label, ReferenceRadius,
				MaxPoseRadius, MaxRootDistance, MaxLocalSegment));
			++Evaluated;
		}

		Anim->RemoveFromRoot();
		Mesh->RemoveFromRoot();
	}

	TestEqual(TEXT("all five opening bodies were evaluated"), Evaluated,
		static_cast<int32>(UE_ARRAY_COUNT(GOpeningPoseCases)));
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

	// Both sides off the baked mount, which is what fixes the expected answer below. A clip read out
	// of a `.glb` used to arrive in glTFRuntime's basis -- `(y, x, z)` against the container's
	// `(x, -y, z)`, a 90 degree yaw carried on the `Bip01` root -- and needed a compensating
	// quarter turn at placement. Nothing loads a `.glb` any more, so the placement is the reflected
	// Source yaw and nothing else (`ElysiumSkeletalBasis`).
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
	if (TestNotNull(TEXT("placement clip has an animation model"), Model)
		&& TestTrue(TEXT("placement target skeleton has a root"), Ref.GetNum() > 0)
		&& TestTrue(TEXT("placement clip reaches 7.5 seconds"), Model->GetNumberOfFrames() >= 225))
	{
		const FVector RootLocal = Model->GetBoneTrackTransform(
			Ref.GetBoneName(0), FFrameNumber(225)).GetTranslation();
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
