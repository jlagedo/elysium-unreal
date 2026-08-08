// Focused retail-pose oracle for the seated courtroom cast. The broad skeletal tests prove that
// a pose is finite; this one catches the Spine1-only failure where the pelvis and legs remain
// seated while an exporter-specific quaternion transform reverses or inverts the upper body.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumContentPaths.h"
#include "Visual/ElysiumNpcVisual.h"

#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "glTFRuntimeAsset.h"

static constexpr EAutomationTestFlags GElysiumCourtroomPoseFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	void BuildCourtroomComponentPose(const FReferenceSkeleton& Ref, IAnimationDataModel& Model,
		const TSet<FName>& AnimatedTracks, int32 Frame, TArray<FTransform>& Out)
	{
		Out = Ref.GetRefBonePose();
		for (int32 BoneIndex = 0; BoneIndex < Ref.GetNum(); ++BoneIndex)
		{
			const FName BoneName = Ref.GetBoneName(BoneIndex);
			if (AnimatedTracks.Contains(BoneName))
			{
				Out[BoneIndex] = Model.GetBoneTrackTransform(
					BoneName, FFrameNumber(Frame));
			}
			const int32 ParentIndex = Ref.GetParentIndex(BoneIndex);
			if (ParentIndex != INDEX_NONE)
			{
				Out[BoneIndex] *= Out[ParentIndex];
			}
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCourtroomSeatedPoseTest,
	"Elysium.Content.CourtroomSeatedPose", GElysiumCourtroomPoseFlags)

bool FElysiumCourtroomSeatedPoseTest::RunTest(const FString&)
{
	if (FElysiumContentPaths::IsIncomplete(TEXT("npc")))
	{
		AddWarning(TEXT("skipping: the npc export domain(s) are marked incomplete"));
		return true;
	}
	if (!IFileManager::Get().FileExists(*FElysiumContentPaths::NpcIndex()))
	{
		AddInfo(TEXT("skipping: NPC export is absent"));
		return true;
	}

	FString Error;
	// The mount is the only build of a character; the out-asset is null on that path by design.
	UglTFRuntimeAsset* Unused = nullptr;
	USkeletalMesh* Mesh = ElysiumNpcVisual::LoadMesh(
		TEXT("ventrue_female_armor_1"), Unused, Error);
	const FString BankPath = FElysiumContentPaths::NpcBankGlb(
		TEXT("banks/cinematic_santa_monica_courtroom_courtroom_bip5__bip01.glb"));
	UglTFRuntimeAsset* BankAsset = ElysiumNpcVisual::LoadAssetFromPath(BankPath, Error);
	UAnimSequence* Anim = Mesh && BankAsset
		? ElysiumNpcVisual::RetargetClip(BankAsset, Mesh, TEXT("entire_scene"), Error)
		: nullptr;
	if (!TestNotNull(TEXT("Vampire4 mesh loads"), Mesh)
		|| !TestNotNull(TEXT("courtroom Bip01 bank loads"), BankAsset)
		|| !TestNotNull(TEXT("courtroom entire_scene binds"), Anim))
	{
		AddError(Error);
		return true;
	}
	Mesh->AddToRoot();
	BankAsset->AddToRoot();
	Anim->AddToRoot();

	IAnimationDataModel* Model = Anim->GetDataModel();
	const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
	const int32 Pelvis = Ref.FindBoneIndex(FName(TEXT("Bip01 Pelvis")));
	const int32 Head = Ref.FindBoneIndex(FName(TEXT("Bip01 Head")));
	if (TestNotNull(TEXT("courtroom clip has an animation model"), Model)
		&& TestTrue(TEXT("Vampire4 has Bip01 Pelvis"), Pelvis != INDEX_NONE)
		&& TestTrue(TEXT("Vampire4 has Bip01 Head"), Head != INDEX_NONE))
	{
		TArray<FName> TrackNames;
		Model->GetBoneTrackNames(TrackNames);
		const TSet<FName> AnimatedTracks(TrackNames);
		const int32 LastFrame = Model->GetNumberOfFrames();

		// This is deliberately only a geometry-sanity test. A prior coordinate target was
		// sampled from the implementation under test and falsely promoted to a retail oracle.
		// Facing/lean acceptance requires a rendered retail invariant.
		for (const int32 Frame : { 0, LastFrame / 4, LastFrame / 2,
			(LastFrame * 3) / 4, LastFrame })
		{
			TArray<FTransform> Pose;
			BuildCourtroomComponentPose(Ref, *Model, AnimatedTracks, Frame, Pose);
			const FVector HeadFromPelvis =
				Pose[Head].GetTranslation() - Pose[Pelvis].GetTranslation();
			TestTrue(FString::Printf(
				TEXT("frame %d produces finite head placement"), Frame),
				!HeadFromPelvis.ContainsNaN());
			TestTrue(FString::Printf(
				TEXT("frame %d keeps the upper body at human scale"), Frame),
				HeadFromPelvis.Size() > 20.0f && HeadFromPelvis.Size() < 90.0f);
			TestTrue(FString::Printf(
				TEXT("frame %d keeps the head above the pelvis"), Frame),
				HeadFromPelvis.Z > 10.0f);
			AddInfo(FString::Printf(TEXT("frame %d: head-from-pelvis %s"),
				Frame, *HeadFromPelvis.ToCompactString()));
		}
	}

	Anim->RemoveFromRoot();
	BankAsset->RemoveFromRoot();
	Mesh->RemoveFromRoot();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
