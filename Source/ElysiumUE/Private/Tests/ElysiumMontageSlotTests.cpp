// The one-shot seam on a graph-backed body: a clip handed to `PlayOneShot` reaches the frame.
//
// `UElysiumBipedAnimInstance::PlayOneShot` answers a graph-backed body over a dynamic slot montage,
// and a dynamic montage's length is `LoopCount x segment length` -- so a looping clip asking for
// LoopCount 0 builds a ZERO-LENGTH montage, which `Montage_Play` refuses without logging. The slot's
// source pose is the locomotion state machine, and for a body that has been handed no selection that
// is the reference pose. The whole failure is therefore invisible: the call returns, the lab clock
// advances, the log stays clean, and the body stands in its bind pose.
//
// Nothing above the seam can see it. The Substrate tier never builds a montage; the Content Browser
// preview runs no anim graph; `Elysium.Content.PlayerGraphInstance` drives the graph's own state
// machine through `PublishSelection` and never touches the slot. The evaluated bone transforms are
// the only observable, which is what this reads -- on the real generated graph, a real baked body and
// a clip the content itself flags as looping.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumContentPaths.h"
#include "Visual/ElysiumAnimSubsystem.h"   // FElysiumResolvedAnimation
#include "Visual/ElysiumBipedAnimInstance.h"
#include "Visual/ElysiumNpcClips.h"
#include "Visual/ElysiumNpcVisual.h"
#include "Visual/ElysiumPoseDeviation.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"
#include "Tests/AutomationCommon.h"

static constexpr EAutomationTestFlags GElysiumMontageSlotFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// Any biped body whose vocabulary carries a looping clip answers this question, so the slice is
	// two rather than the cast: one seeds its rig family and one merges into it, which is the same
	// pair `Elysium.Content.BakedCharacterParity` uses and the reason a second is listed at all --
	// a partial export that covers only one of them still runs the test.
	const TCHAR* const GBodyStems[] = {
		TEXT("smiling_jack"),
		TEXT("tremere_male_armor_0"),
	};

	// Long enough that the one-shot pass below is measured mid-clip rather than after it ended, and
	// short enough that ticking a whole loop cycle at 1/30 stays a fraction of a second of work.
	constexpr float GMinClipSeconds = 0.5f;
	constexpr float GMaxClipSeconds = 8.0f;

	struct FLoopingClipPick
	{
		FString Stem;
		FString Label;
		FString Owner;
		USkeletalMesh* Mesh = nullptr;
		UAnimSequence* Clip = nullptr;
		float FadeSeconds = UElysiumBodyAnimInstance::DefaultBlendSeconds;
	};

	// The first baked body carrying a clip the CONTENT flags as looping (STUDIO_LOOPING, bit 0) that
	// is one animation on the mount. A label naming a blend grid resolves to no single sequence and is
	// skipped rather than guessed at -- the grid pin is `Elysium.Content.PlayerGraphInstance`'s.
	bool FindLoopingClip(FLoopingClipPick& Out)
	{
		for (const TCHAR* Stem : GBodyStems)
		{
			USkeletalMesh* Mesh = ElysiumNpcVisual::LoadBakedMesh(Stem);
			FElysiumNpcClipSet Vocabulary;
			FString Error;
			if (Mesh == nullptr || !Vocabulary.Load(Stem, Error))
			{
				continue;
			}

			TArray<FString> Labels;
			Vocabulary.Clips.GetKeys(Labels);
			Labels.Sort([](const FString& A, const FString& B) { return A < B; });
			for (const FString& Label : Labels)
			{
				const FElysiumNpcClip& Clip = Vocabulary.Clips[Label];
				if ((Clip.Flags & 0x1) == 0 || Clip.IsAdditive()
					|| Clip.Seconds() < GMinClipSeconds || Clip.Seconds() > GMaxClipSeconds)
				{
					continue;
				}
				const FString Owner = Clip.IsOwnedBy(Stem) ? FString(Stem) : Clip.Owner;
				UAnimSequence* Baked = ElysiumNpcVisual::LoadBakedClip(Mesh, Owner, Label);
				if (Baked == nullptr || Baked->GetPlayLength() < GMinClipSeconds)
				{
					continue;
				}
				Out.Stem = Stem;
				Out.Label = Label;
				Out.Owner = Owner;
				Out.Mesh = Mesh;
				Out.Clip = Baked;
				Out.FadeSeconds = Clip.FadeSeconds();
				return true;
			}
		}
		return false;
	}
}

namespace
{
	// The recipe BuildNpcVisual uses for a graph-backed body, minus the placement. Null on any
	// refusal, with the refusal already reported.
	UElysiumBipedAnimInstance* StandGraphBody(AActor* Owner, USkeletalMesh* Mesh, UClass* Graph,
		FAutomationTestBase& Test, USkeletalMeshComponent*& OutComp)
	{
		USkeletalMeshComponent* Comp = NewObject<USkeletalMeshComponent>(Owner);
		Comp->SetMobility(EComponentMobility::Movable);
		Comp->SetSkeletalMeshAsset(Mesh);
		Comp->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		Comp->SetAnimInstanceClass(Graph);
		Owner->SetRootComponent(Comp);
		Comp->RegisterComponent();
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		OutComp = Comp;

		UElysiumBipedAnimInstance* Inst = Cast<UElysiumBipedAnimInstance>(Comp->GetAnimInstance());
		if (!Test.TestNotNull(TEXT("the generated graph installs the biped host"), Inst))
		{
			return nullptr;
		}
		// Without this the seam under test is never reached: a host reporting no compiled graph routes
		// the one-shot to the proxy's clip player, which poses correctly and proves nothing about the
		// slot.
		if (!Test.TestTrue(
			TEXT("and the body reports a compiled graph, so the one-shot takes the slot montage"),
			Inst->HasCompiledGraph()))
		{
			return nullptr;
		}
		return Inst;
	}

	// A null tick function keeps evaluation on this thread, so the transforms are readable the moment
	// RefreshBoneTransforms returns rather than a frame later.
	void EvaluateFrames(USkeletalMeshComponent* Comp, int32 Frames, float DeltaSeconds,
		TArray<FTransform>& OutPose)
	{
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			Comp->TickAnimation(DeltaSeconds, /*bNeedsValidRootMotion=*/false);
			Comp->RefreshBoneTransforms(/*TickFunction=*/nullptr);
		}
		OutPose = Comp->GetComponentSpaceTransforms();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumGraphMontageSlotTest,
	"Elysium.Content.GraphMontageSlot", GElysiumMontageSlotFlags)
bool FElysiumGraphMontageSlotTest::RunTest(const FString&)
{
	if (FElysiumContentPaths::IsIncomplete(TEXT("npc")))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: the npc export domain is marked incomplete"));
		return true;
	}

	UClass* Graph = LoadClass<UAnimInstance>(nullptr,
		*FElysiumContentPaths::PlayerAnimBlueprintClass());
	if (Graph == nullptr)
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: the player animation graph is not generated "
			"(run: uv run elysium export bundle policy)"));
		return true;
	}

	FLoopingClipPick Pick;
	if (!FindLoopingClip(Pick))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no baked body in the slice carries a looping clip; "
			"run: uv run elysium export characters"));
		return true;
	}
	Pick.Mesh->AddToRoot();
	Pick.Clip->AddToRoot();
	ON_SCOPE_EXIT
	{
		Pick.Clip->RemoveFromRoot();
		Pick.Mesh->RemoveFromRoot();
	};

	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game) || !TestWorld.BeginPlayInTestWorld())
	{
		TestWorld.ForwardErrorMessages(this);
		return false;
	}
	UWorld* World = TestWorld.GetTestWorld();
	AActor* Owner = World ? World->SpawnActor<AActor>() : nullptr;
	if (!TestNotNull(TEXT("body owner spawned"), Owner))
	{
		return false;
	}

	USkeletalMeshComponent* Comp = nullptr;
	UElysiumBipedAnimInstance* Inst = StandGraphBody(Owner, Pick.Mesh, Graph, *this, Comp);
	if (Inst == nullptr)
	{
		return false;
	}

	const auto Evaluate = [Comp](int32 Frames, float DeltaSeconds, TArray<FTransform>& OutPose)
	{
		EvaluateFrames(Comp, Frames, DeltaSeconds, OutPose);
	};
	constexpr float FrameSeconds = 1.f / 30.f;

	// **Nothing is published, deliberately.** The reference is taken with the state machine holding no
	// selection, which is the bind pose by construction and is exactly the pose a refused montage
	// leaves on screen -- so what the assertions below compare against is the defect's own output
	// rather than a computed ideal.
	TArray<FTransform> BindPose;
	Evaluate(/*Frames=*/1, FrameSeconds, BindPose);
	const int32 PosedBones = BindPose.Num() - 1;
	if (!TestTrue(TEXT("the graph evaluates the whole skeleton"),
		BindPose.Num() == Pick.Mesh->GetRefSkeleton().GetNum() && PosedBones > 0))
	{
		return false;
	}

	AddInfo(FString::Printf(TEXT("'%s' plays '%s'@'%s' (%.3fs, fade %.2fs) over %d bone(s)"),
		*Pick.Stem, *Pick.Label, *Pick.Owner, Pick.Clip->GetPlayLength(), Pick.FadeSeconds,
		PosedBones));

	// --- the one-shot, which is the control ---------------------------------------------------------
	//
	// A non-looping clip asks for one segment and has always built a playable montage. Measuring it
	// first separates "the slot poses nothing at all" from "the LOOPING length is wrong", which are
	// different repairs behind one identical T-pose.
	TestTrue(TEXT("a one-shot clip is accepted by the slot"),
		Inst->PlayOneShot(Pick.Clip, /*bLoop=*/false, Pick.FadeSeconds));
	TArray<FTransform> OneShotPose;
	Evaluate(/*Frames=*/6, FrameSeconds, OneShotPose);
	const ElysiumPose::FDeviation OneShot = ElysiumPose::Measure(BindPose, OneShotPose);
	AddInfo(FString::Printf(TEXT("one-shot: %d of %d non-root bones left the bind pose (max %.1f deg)"),
		OneShot.MovedBones, PosedBones, OneShot.MaxDegrees));
	TestTrue(TEXT("and it poses the body rather than leaving it in the bind pose"),
		OneShot.MovedBones > PosedBones / 4 && OneShot.MaxDegrees > 5.f);

	// --- the looping clip, which is the regression --------------------------------------------------
	//
	// The refusal this guards is the whole failure: `Montage_Play` answers a zero-length montage with
	// null and logs nothing, so the call below returning false IS the defect, with the reference pose
	// two assertions down as its only other symptom.
	TestTrue(TEXT("a looping clip is accepted by the slot"),
		Inst->PlayOneShot(Pick.Clip, /*bLoop=*/true, Pick.FadeSeconds));
	TArray<FTransform> LoopPose;
	Evaluate(/*Frames=*/12, FrameSeconds, LoopPose);   // 0.4 s, past the authored fade
	const ElysiumPose::FDeviation Looping = ElysiumPose::Measure(BindPose, LoopPose);
	AddInfo(FString::Printf(TEXT("looping: %d of %d non-root bones left the bind pose (max %.1f deg)"),
		Looping.MovedBones, PosedBones, Looping.MaxDegrees));
	TestTrue(TEXT("and it poses the body rather than leaving it in the bind pose"),
		Looping.MovedBones > PosedBones / 4 && Looping.MaxDegrees > 5.f);

	// Past the clip's own length, which is the half a plain "does it pose" check cannot reach: a
	// montage built for a single segment ends here, drops the slot's weight to zero and hands the
	// frame back to a state machine holding nothing. A clip that loops is still posing.
	//
	// **A HALF cycle past it, not a whole one.** Advancing by a multiple of the clip's own length
	// lands the loop back on the phase the first sample already read, so the two poses agree for a
	// reason that has nothing to do with whether the montage survived — and the `Advanced` reading
	// below is then a tautology rather than an observation. Offsetting by half makes the two samples
	// independent: they differ on any clip that is not a still pose. That is reported and not
	// asserted, because a looping clip whose every frame is one pose is legal content.
	const int32 PastEndFrames =
		FMath::CeilToInt(Pick.Clip->GetPlayLength() * 1.5f / FrameSeconds);
	TArray<FTransform> LoopedPose;
	Evaluate(PastEndFrames, FrameSeconds, LoopedPose);
	const ElysiumPose::FDeviation Looped = ElysiumPose::Measure(BindPose, LoopedPose);
	const ElysiumPose::FDeviation Advanced = ElysiumPose::Measure(LoopPose, LoopedPose);
	AddInfo(FString::Printf(
		TEXT("after %.2fs (%d frames, past the clip's %.3fs): %d of %d bones off the bind pose "
		     "(max %.1f deg); %d moved since the first sample"),
		PastEndFrames * FrameSeconds, PastEndFrames, Pick.Clip->GetPlayLength(),
		Looped.MovedBones, PosedBones, Looped.MaxDegrees, Advanced.MovedBones));
	TestTrue(TEXT("a looping montage is still posing the body after the clip's own length has passed"),
		Looped.MovedBones > PosedBones / 4 && Looped.MaxDegrees > 5.f);

	// The clip stops when it is asked to and not before, which is what makes the assertion above a
	// statement about the loop rather than about a montage nothing can end.
	Inst->StopOneShot(0.f);
	TArray<FTransform> StoppedPose;
	Evaluate(/*Frames=*/2, FrameSeconds, StoppedPose);
	const ElysiumPose::FDeviation Stopped = ElysiumPose::Measure(BindPose, StoppedPose);
	AddInfo(FString::Printf(TEXT("stopped: %d of %d bones off the bind pose (max %.1f deg)"),
		Stopped.MovedBones, PosedBones, Stopped.MaxDegrees));
	TestTrue(TEXT("and stopping it hands the frame back to the state machine underneath"),
		Stopped.MovedBones < Looped.MovedBones);

	return true;
}

// Who is allowed to end a clip somebody else armed.
//
// Every body with a mover publishes a locomotion selection on every anim tick, a standing one
// included, and a stood body resolves an idle that binds an asset — so a publish that ends the
// DefaultSlot one-shot whenever it holds an asset ends every clip another owner armed on the frame
// after it started. What that deletes is the ambient cast's whole stance vocabulary: the schedule
// arms an idle, a fidget or a stance transition, the next tick kills it with a zero blend, and the
// schedule re-arms it one clip length later for the life of the map. The body stands still, and the
// only thing that ever reaches the frame is the single forced evaluation the arm itself performs.
//
// The floor asserted here is that a body which is not travelling does not make that claim. It is not
// the whole answer — a travelling body still takes the pose back from a one-shot, and the answer to
// that is the channel arbitration slot.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumGraphIdlePublishTest,
	"Elysium.Content.GraphOneShotSurvivesIdlePublish", GElysiumMontageSlotFlags)
bool FElysiumGraphIdlePublishTest::RunTest(const FString&)
{
	if (FElysiumContentPaths::IsIncomplete(TEXT("npc")))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: the npc export domain is marked incomplete"));
		return true;
	}
	UClass* Graph = LoadClass<UAnimInstance>(nullptr,
		*FElysiumContentPaths::PlayerAnimBlueprintClass());
	if (Graph == nullptr)
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: the player animation graph is not generated "
			"(run: uv run elysium export bundle policy)"));
		return true;
	}
	FLoopingClipPick Pick;
	if (!FindLoopingClip(Pick))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no baked body in the slice carries a looping clip; "
			"run: uv run elysium export characters"));
		return true;
	}
	Pick.Mesh->AddToRoot();
	Pick.Clip->AddToRoot();
	ON_SCOPE_EXIT
	{
		Pick.Clip->RemoveFromRoot();
		Pick.Mesh->RemoveFromRoot();
	};

	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game) || !TestWorld.BeginPlayInTestWorld())
	{
		TestWorld.ForwardErrorMessages(this);
		return false;
	}
	UWorld* World = TestWorld.GetTestWorld();
	AActor* Owner = World ? World->SpawnActor<AActor>() : nullptr;
	if (!TestNotNull(TEXT("body owner spawned"), Owner))
	{
		return false;
	}
	USkeletalMeshComponent* Comp = nullptr;
	UElysiumBipedAnimInstance* Inst = StandGraphBody(Owner, Pick.Mesh, Graph, *this, Comp);
	if (Inst == nullptr)
	{
		return false;
	}
	constexpr float FrameSeconds = 1.f / 30.f;
	TArray<FTransform> Pose;

	// The stance clip, armed the way the ambient schedule arms one.
	if (!TestTrue(TEXT("the schedule's clip is accepted by the slot"),
		Inst->PlayOneShot(Pick.Clip, /*bLoop=*/false, Pick.FadeSeconds)))
	{
		return false;
	}
	EvaluateFrames(Comp, /*Frames=*/4, FrameSeconds, Pose);
	if (!TestNotNull(TEXT("and it is playing"), Inst->GetCurrentActiveMontage()))
	{
		return false;
	}

	// A resolved locomotion selection with a real asset — the record a body standing still publishes
	// on every tick of its life.
	FElysiumAnimationSelection Standing;
	Standing.GraphState = EElysiumGraphState::Idle;
	Standing.SequenceLabel = Pick.Label;
	Standing.OwnerStem = Pick.Owner;
	Standing.AnimationName = Pick.Label;
	Standing.AssetKind = EElysiumAnimAssetKind::Sequence;
	Standing.Outcome = EElysiumAnimOutcome::Resolved;
	FElysiumResolvedAnimation Assets;
	Assets.Sequence = Pick.Clip;

	Inst->PublishSelection(Standing, Assets);
	EvaluateFrames(Comp, /*Frames=*/4, FrameSeconds, Pose);
	TestNotNull(TEXT("a standing body's locomotion publish leaves the schedule's clip playing"),
		Inst->GetCurrentActiveMontage());

	// The same body, now travelling. Here the publish IS the claim: a walk fan underneath a live
	// one-shot never reaches the frame, so the body would keep playing its idle while it moves.
	FElysiumAnimationSelection Travelling = Standing;
	Travelling.GraphState = EElysiumGraphState::Walk;
	Inst->PublishSelection(Travelling, Assets);
	EvaluateFrames(Comp, /*Frames=*/4, FrameSeconds, Pose);
	TestNull(TEXT("and a travelling body's publish takes the base pose back"),
		Inst->GetCurrentActiveMontage());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
