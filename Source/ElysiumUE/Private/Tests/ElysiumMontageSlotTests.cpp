// The one-shot seam on a graph-backed body: a clip handed to `PlayOneShot` reaches the frame.
//
// `UElysiumBipedAnimInstance::PlayOneShot` answers a graph-backed body over a dynamic slot montage,
// and a dynamic montage's length is `LoopCount x segment length` -- so a looping clip asking for
// LoopCount 0 builds a ZERO-LENGTH montage, which `Montage_Play` refuses without logging. The slot's
// source pose is the locomotion blend stack, and for a body that has been handed no selection that
// is the reference pose. The whole failure is therefore invisible: the call returns, the lab clock
// advances, the log stays clean, and the body stands in its bind pose.
//
// Nothing above the seam can see it. The Substrate tier never builds a montage; the Content Browser
// preview runs no anim graph; `Elysium.Content.PlayerGraphInstance` drives the graph's own blend
// stack through `PublishSelection` and never touches the slot. The evaluated bone transforms are
// the only observable, which is what this reads -- on the real generated graph, a real baked body and
// a clip the content itself flags as looping.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumAnimationIntent.h"    // the slot claim and its pure envelope
#include "ElysiumContentPaths.h"
#include "Visual/ElysiumAnimGraph.h"       // ElysiumAnimGraph::ReactionBranchTag
#include "Visual/ElysiumAnimLayerMask.h"
#include "Visual/ElysiumAnimSubsystem.h"   // FElysiumResolvedAnimation
#include "Visual/ElysiumBipedAnimInstance.h"
#include "Visual/ElysiumBlendGrids.h"
#include "Visual/ElysiumEntityBodies.h"    // ElysiumEntityAnimation::BlendedGridLengthSeconds
#include "Visual/ElysiumNpcClips.h"
#include "Visual/ElysiumNpcVisual.h"
#include "Visual/ElysiumPoseDeviation.h"

#include "AlphaBlend.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimNode_Inertialization.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimStateMachineTypes.h"     // FBakedAnimationStateMachine
#include "Animation/AnimSubsystem_Tag.h"
#include "Animation/BlendProfile.h"
#include "Animation/BlendSpace.h"
#include "Animation/Skeleton.h"
#include "AnimNodes/AnimNode_BlendListBase.h"     // EBlendListChildUpdateMode
#include "AnimNodes/AnimNode_BlendListByBool.h"
#include "AnimNodes/AnimNode_LayeredBoneBlend.h"  // the slot blend, read back by tag
#include "BlendStack/AnimNode_BlendStack.h"
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
	// two rather than the cast: the same pair `Elysium.Content.BakedCharacterParity` carries as its
	// single-rooted control. A second is listed at all so that a partial export covering only one
	// of them still runs the test.
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

namespace
{
	// Every graph-backed row below repeats the same three setup steps (the npc-domain check plus the
	// compiled player graph load, a `FindLoopingClip` pick, standing an actor/component/anim instance
	// over a mesh) and the same "body owner spawned" check, so those are factored here once. Nothing
	// is cached ACROSS rows: each is a plain function the caller re-runs every time, over the caller's
	// OWN stack-local `FTestWorldWrapper`.
	//
	// This is deliberate, not a missed optimization. A cross-row cache was tried and reverted: static
	// state keyed off "how many of the seven registered rows have reported in" is wrong the moment
	// Unreal's automation filter selects fewer than seven -- `Elysium.Content.Graph.SlotLayer` alone,
	// this project's own recommended narrow-filter workflow, would run one row, never reach the
	// count, and leave the `UWorld` standing and the mesh/clip `AddToRoot`'d for the life of the
	// process; worse, the cache held its `FTestWorldWrapper` in a function-local `static`, so on a
	// process that never re-entered this file the wrapper's destructor fired at DLL unload, after the
	// engine that owns GC and the world list has already shut down.
	//
	// It also is not the win it looks like: `LoadClass`/`LoadObject` resolve an already-resident
	// package through `StaticFindObjectFast` before touching disk (`StaticLoadObjectInternal`,
	// `UObjectGlobals.cpp`), and `LoadPackage` itself short-circuits once a package is loaded -- so a
	// second `LoadClass<UAnimInstance>` for the same graph, or a second `FindLoopingClip` over an
	// already-loaded mesh, answers from the in-memory object cache rather than re-parsing the asset.
	// Cross-row rooting only ever guarded against a GC landing between two rows, and automation does
	// not force one there. The `UWorld` is the one truly per-row cost, and it is inherently
	// non-shareable across rows that must not inherit each other's pose anyway.
	enum class ESharedSetupResult : uint8
	{
		Ready,
		Abstain,
		Failed,
	};

	// The npc-domain check plus the compiled player graph load, standing the caller's own world. The
	// caller owns `OutWorld`'s lifetime (a stack local, torn down by its own destructor when the row
	// returns) and `OutGraph` is a plain non-owning `UClass*` -- the class stays loaded by the engine's
	// own package cache for as long as anything references it, same as every other asset load here.
	ESharedSetupResult EnsureCoreSetup(FAutomationTestBase& Test, FTestWorldWrapper& OutWorld,
		UClass*& OutGraph, FString& OutAbstainMessage)
	{
		if (FElysiumContentPaths::IsIncomplete(TEXT("npc")))
		{
			OutAbstainMessage = TEXT("ELYSIUM_TEST_ABSTAIN: the npc export domain is marked incomplete");
			return ESharedSetupResult::Abstain;
		}
		OutGraph = LoadClass<UAnimInstance>(nullptr, *FElysiumContentPaths::PlayerAnimBlueprintClass());
		if (OutGraph == nullptr)
		{
			OutAbstainMessage = TEXT("ELYSIUM_TEST_ABSTAIN: the player animation graph is not generated "
				"(run: uv run elysium export bundle policy)");
			return ESharedSetupResult::Abstain;
		}
		if (!OutWorld.CreateTestWorld(EWorldType::Game) || !OutWorld.BeginPlayInTestWorld())
		{
			OutWorld.ForwardErrorMessages(&Test);
			return ESharedSetupResult::Failed;
		}
		return ESharedSetupResult::Ready;
	}

	// The looping-clip pick for the five cases that stand on an ordinary looping clip (MontageSlot,
	// OneShotArbitration, ReactionBranch, BlendStack, FirstAssetBlend). ReactionDrive and SlotLayer
	// pick their own content and never call this. On success the mesh and clip are rooted for exactly
	// the caller's own scope; the caller un-roots them (`FGraphBodyCase`'s destructor does it below),
	// the same discipline every one of these cases used before any of them shared a helper.
	ESharedSetupResult EnsureLoopingPick(FString& OutAbstainMessage, FLoopingClipPick& OutPick)
	{
		if (!FindLoopingClip(OutPick))
		{
			OutAbstainMessage =
				TEXT("ELYSIUM_TEST_ABSTAIN: no baked body in the slice carries a looping clip; "
					"run: uv run elysium export characters");
			return ESharedSetupResult::Abstain;
		}
		OutPick.Mesh->AddToRoot();
		OutPick.Clip->AddToRoot();
		return ESharedSetupResult::Ready;
	}

	// One actor, standing on the row's own world and mesh, with its own anim instance -- the per-row
	// reinitialization that keeps a case from inheriting the blend state a previous case left on a
	// shared skeleton. Both the rooted pick and the actor are released automatically when the case
	// that asked for it returns, scoped to exactly the one row, never spanning two.
	struct FGraphBodyCase
	{
		UWorld* World = nullptr;
		AActor* Owner = nullptr;
		USkeletalMeshComponent* Comp = nullptr;
		UElysiumBipedAnimInstance* Inst = nullptr;
		FLoopingClipPick Pick;
		bool bPickRooted = false;

		~FGraphBodyCase()
		{
			if (World != nullptr && Owner != nullptr)
			{
				World->DestroyActor(Owner);
			}
			if (bPickRooted)
			{
				Pick.Clip->RemoveFromRoot();
				Pick.Mesh->RemoveFromRoot();
			}
		}
	};

	ESharedSetupResult BeginGraphBodyCase(FAutomationTestBase& Test, FTestWorldWrapper& World,
		UClass* Graph, FGraphBodyCase& OutCase)
	{
		FString Message;
		const ESharedSetupResult PickResult = EnsureLoopingPick(Message, OutCase.Pick);
		if (PickResult == ESharedSetupResult::Abstain)
		{
			Test.AddInfo(Message);
			return ESharedSetupResult::Abstain;
		}
		OutCase.bPickRooted = true;

		OutCase.World = World.GetTestWorld();
		OutCase.Owner = OutCase.World->SpawnActor<AActor>();
		if (!Test.TestNotNull(TEXT("body owner spawned"), OutCase.Owner))
		{
			return ESharedSetupResult::Failed;
		}
		OutCase.Inst = StandGraphBody(OutCase.Owner, OutCase.Pick.Mesh, Graph, Test, OutCase.Comp);
		if (OutCase.Inst == nullptr)
		{
			return ESharedSetupResult::Failed;
		}
		return ESharedSetupResult::Ready;
	}
}

static bool RunGraphMontageSlotCase(FAutomationTestBase& Test, USkeletalMeshComponent* Comp,
	UElysiumBipedAnimInstance* Inst, const FLoopingClipPick& Pick)
{
	const auto Evaluate = [Comp](int32 Frames, float DeltaSeconds, TArray<FTransform>& OutPose)
	{
		EvaluateFrames(Comp, Frames, DeltaSeconds, OutPose);
	};
	constexpr float FrameSeconds = 1.f / 30.f;

	// **Nothing is published, deliberately.** The reference is taken with the blend stack holding no
	// selection, which is the bind pose by construction and is exactly the pose a refused montage
	// leaves on screen -- so what the assertions below compare against is the defect's own output
	// rather than a computed ideal.
	TArray<FTransform> BindPose;
	Evaluate(/*Frames=*/1, FrameSeconds, BindPose);
	const int32 PosedBones = BindPose.Num() - 1;
	if (!Test.TestTrue(TEXT("the graph evaluates the whole skeleton"),
		BindPose.Num() == Pick.Mesh->GetRefSkeleton().GetNum() && PosedBones > 0))
	{
		return false;
	}

	Test.AddInfo(FString::Printf(TEXT("'%s' plays '%s'@'%s' (%.3fs, fade %.2fs) over %d bone(s)"),
		*Pick.Stem, *Pick.Label, *Pick.Owner, Pick.Clip->GetPlayLength(), Pick.FadeSeconds,
		PosedBones));

	// --- the one-shot, which is the control ---------------------------------------------------------
	//
	// A non-looping clip asks for one segment and has always built a playable montage. Measuring it
	// first separates "the slot poses nothing at all" from "the LOOPING length is wrong", which are
	// different repairs behind one identical T-pose.
	Test.TestTrue(TEXT("a one-shot clip is accepted by the slot"),
		Inst->PlayOneShot(FElysiumClipIdentity(Pick.Owner, Pick.Label), Pick.Clip, /*bLoop=*/false, Pick.FadeSeconds, Pick.FadeSeconds));
	TArray<FTransform> OneShotPose;
	Evaluate(/*Frames=*/6, FrameSeconds, OneShotPose);
	const ElysiumPose::FDeviation OneShot = ElysiumPose::Measure(BindPose, OneShotPose);
	Test.AddInfo(FString::Printf(TEXT("one-shot: %d of %d non-root bones left the bind pose (max %.1f deg)"),
		OneShot.MovedBones, PosedBones, OneShot.MaxDegrees));
	Test.TestTrue(TEXT("and it poses the body rather than leaving it in the bind pose"),
		OneShot.MovedBones > PosedBones / 4 && OneShot.MaxDegrees > 5.f);

	// --- the looping clip, which is the regression --------------------------------------------------
	//
	// The refusal this guards is the whole failure: `Montage_Play` answers a zero-length montage with
	// null and logs nothing, so the call below returning false IS the defect, with the reference pose
	// two assertions down as its only other symptom.
	Test.TestTrue(TEXT("a looping clip is accepted by the slot"),
		Inst->PlayOneShot(FElysiumClipIdentity(Pick.Owner, Pick.Label), Pick.Clip, /*bLoop=*/true, Pick.FadeSeconds, Pick.FadeSeconds));
	TArray<FTransform> LoopPose;
	Evaluate(/*Frames=*/12, FrameSeconds, LoopPose);   // 0.4 s, past the authored fade
	const ElysiumPose::FDeviation Looping = ElysiumPose::Measure(BindPose, LoopPose);
	Test.AddInfo(FString::Printf(TEXT("looping: %d of %d non-root bones left the bind pose (max %.1f deg)"),
		Looping.MovedBones, PosedBones, Looping.MaxDegrees));
	Test.TestTrue(TEXT("and it poses the body rather than leaving it in the bind pose"),
		Looping.MovedBones > PosedBones / 4 && Looping.MaxDegrees > 5.f);

	// Past the clip's own length, which is the half a plain "does it pose" check cannot reach: a
	// montage built for a single segment ends here, drops the slot's weight to zero and hands the
	// frame back to a blend stack holding nothing. A clip that loops is still posing.
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
	Test.AddInfo(FString::Printf(
		TEXT("after %.2fs (%d frames, past the clip's %.3fs): %d of %d bones off the bind pose "
		     "(max %.1f deg); %d moved since the first sample"),
		PastEndFrames * FrameSeconds, PastEndFrames, Pick.Clip->GetPlayLength(),
		Looped.MovedBones, PosedBones, Looped.MaxDegrees, Advanced.MovedBones));
	Test.TestTrue(TEXT("a looping montage is still posing the body after the clip's own length has passed"),
		Looped.MovedBones > PosedBones / 4 && Looped.MaxDegrees > 5.f);

	// The clip stops when it is asked to and not before, which is what makes the assertion above a
	// statement about the loop rather than about a montage nothing can end.
	Inst->StopOneShot(0.f);
	TArray<FTransform> StoppedPose;
	Evaluate(/*Frames=*/2, FrameSeconds, StoppedPose);
	const ElysiumPose::FDeviation Stopped = ElysiumPose::Measure(BindPose, StoppedPose);
	Test.AddInfo(FString::Printf(TEXT("stopped: %d of %d bones off the bind pose (max %.1f deg)"),
		Stopped.MovedBones, PosedBones, Stopped.MaxDegrees));
	Test.TestTrue(TEXT("and stopping it hands the frame back to the blend stack underneath"),
		Stopped.MovedBones < Looped.MovedBones);

	// --- LIFE5: two fades, stated apart, and the source pose that gates the blend IN ---------------
	//
	// Retail fades a flinch in over 0.1 and out over 0.3, and `PlaySlotAnimationAsDynamicMontage`
	// takes the two separately — so the seam takes them separately too, and both have to survive the
	// trip onto the dynamic montage rather than one being derived from the other.
	//
	// The blend IN is additionally gated by what the slot's SOURCE pose is. A one-shot over a state
	// stack that has been handed no asset has nothing to blend from — that source is the reference
	// pose — so it snaps; a one-shot over a graph holding a real selection blends, and snapping onto
	// a posed body is exactly the pop the rule exists to avoid. The predicate is therefore the
	// applied selection, not "is this the first montage on this body".
	{
		constexpr float FadeIn = 0.1f;
		constexpr float FadeOut = 0.3f;

		// Still nothing published in this test, and `StopOneShot` above left the slot empty: the
		// snapping arm of the rule.
		if (Test.TestTrue(TEXT("a one-shot with distinct fades is accepted by the slot"),
			Inst->PlayOneShot(FElysiumClipIdentity(Pick.Owner, Pick.Label), Pick.Clip, /*bLoop=*/false, FadeIn, FadeOut)))
		{
			UAnimMontage* Snapped = Inst->GetCurrentActiveMontage();
			if (Test.TestNotNull(TEXT("and the dynamic montage exists"), Snapped))
			{
				Test.TestEqual(TEXT("a clip over a graph holding no asset snaps in"),
					Snapped->BlendIn.GetBlendTime(), 0.f);
				Test.TestEqual(TEXT("while its blend OUT is the fade it asked for"),
					Snapped->BlendOut.GetBlendTime(), FadeOut);
			}
		}
		Inst->StopOneShot(0.f);
		Evaluate(/*Frames=*/2, FrameSeconds, StoppedPose);

		// Hand the graph a real selection, which is the state every body in the running game is in
		// the moment anything hits it.
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
		Evaluate(/*Frames=*/2, FrameSeconds, StoppedPose);

		if (Test.TestTrue(TEXT("the same one-shot re-arms over the applied selection"),
			Inst->PlayOneShot(FElysiumClipIdentity(Pick.Owner, Pick.Label), Pick.Clip, /*bLoop=*/false, FadeIn, FadeOut)))
		{
			UAnimMontage* Blended = Inst->GetCurrentActiveMontage();
			if (Test.TestNotNull(TEXT("and its dynamic montage exists"), Blended))
			{
				Test.TestEqual(TEXT("a clip over a graph that HAS an asset blends in rather than snapping"),
					Blended->BlendIn.GetBlendTime(), FadeIn);
				Test.TestEqual(TEXT("and the two fades reach the montage independently"),
					Blended->BlendOut.GetBlendTime(), FadeOut);
			}
		}
		Inst->StopOneShot(0.f);
	}

	return true;
}

// Who is allowed to end a clip somebody else armed: the record's arbitration verdict, obeyed here.
//
// Every body with a mover publishes a locomotion selection on every anim tick, a standing one
// included, and a stood body resolves an idle that binds an asset — so a publish that ends the
// DefaultSlot one-shot whenever it holds an asset ends every clip another owner armed on the frame
// after it started. LIFE4's channel arbitration slot decides who wins by priority in the driver
// and writes the verdict onto the record (`bBasePoseOwned`); the instance's whole job is to obey
// it. Asserted here on the real generated graph: a yielded publish leaves the clip alone whatever
// its graph state — an ambient stance holding against a standing body, a scene holding against a
// TRAVELLING one, which is the half the retired while-locomoting rule got wrong — and a publish
// that owns the base takes it. The verdict's computation is
// `Elysium.Substrate.AnimationArbitration`'s.
static bool RunGraphOneShotArbitrationCase(FAutomationTestBase& Test, USkeletalMeshComponent* Comp,
	UElysiumBipedAnimInstance* Inst, const FLoopingClipPick& Pick)
{
	constexpr float FrameSeconds = 1.f / 30.f;
	TArray<FTransform> Pose;

	// The stance clip, armed the way the ambient schedule arms one.
	if (!Test.TestTrue(TEXT("the schedule's clip is accepted by the slot"),
		Inst->PlayOneShot(FElysiumClipIdentity(Pick.Owner, Pick.Label), Pick.Clip, /*bLoop=*/false, Pick.FadeSeconds, Pick.FadeSeconds)))
	{
		return false;
	}
	EvaluateFrames(Comp, /*Frames=*/4, FrameSeconds, Pose);
	if (!Test.TestNotNull(TEXT("and it is playing"), Inst->GetCurrentActiveMontage()))
	{
		return false;
	}

	// A resolved locomotion selection with a real asset — the record a body standing still publishes
	// on every tick of its life — carrying the verdict the driver computed: the ambient claim the
	// schedule's arm submitted outranks a standing publish, so the base is yielded.
	FElysiumAnimationSelection Standing;
	Standing.GraphState = EElysiumGraphState::Idle;
	Standing.SequenceLabel = Pick.Label;
	Standing.OwnerStem = Pick.Owner;
	Standing.AnimationName = Pick.Label;
	Standing.AssetKind = EElysiumAnimAssetKind::Sequence;
	Standing.Outcome = EElysiumAnimOutcome::Resolved;
	Standing.bBasePoseOwned = false;
	Standing.BaseHold = TEXT("npc 'stance' (ambient)");
	FElysiumResolvedAnimation Assets;
	Assets.Sequence = Pick.Clip;

	Inst->PublishSelection(Standing, Assets);
	EvaluateFrames(Comp, /*Frames=*/4, FrameSeconds, Pose);
	Test.TestNotNull(TEXT("a yielded standing publish leaves the schedule's clip playing"),
		Inst->GetCurrentActiveMontage());

	// The same body, now travelling — and still yielded, which is the verdict a scene's claim
	// produces against the travel row. The retired rule ended the clip on the graph state alone;
	// the instance now obeys only the verdict.
	FElysiumAnimationSelection Travelling = Standing;
	Travelling.GraphState = EElysiumGraphState::Walk;
	Inst->PublishSelection(Travelling, Assets);
	EvaluateFrames(Comp, /*Frames=*/4, FrameSeconds, Pose);
	Test.TestNotNull(TEXT("a yielded travelling publish leaves the clip playing too"),
		Inst->GetCurrentActiveMontage());

	// The publish that won the arbitration — the claim expired, was released or was outranked —
	// takes the base back, travelling or not.
	FElysiumAnimationSelection Owned = Travelling;
	Owned.bBasePoseOwned = true;
	Owned.BaseHold.Reset();
	Inst->PublishSelection(Owned, Assets);
	EvaluateFrames(Comp, /*Frames=*/4, FrameSeconds, Pose);
	Test.TestNull(TEXT("and a publish that owns the base takes the pose back"),
		Inst->GetCurrentActiveMontage());

	// LIFE4, the expiry preempt pinned as deliberate: a one-shot's channel claim holds exactly its
	// clip's play length, so the frame the driver's expired claim hands the base to a standing
	// publish (`Elysium.Substrate.AnimationArbitration` pins that timing), the montage has already
	// completed its own blend-out — there is nothing left for the takeover to cut, which is why the
	// preempt is invisible on screen.
	if (!Test.TestTrue(TEXT("the one-shot re-arms for the expiry run"),
		Inst->PlayOneShot(FElysiumClipIdentity(Pick.Owner, Pick.Label), Pick.Clip, /*bLoop=*/false, Pick.FadeSeconds, Pick.FadeSeconds)))
	{
		return false;
	}
	const int32 LengthFrames =
		FMath::CeilToInt32(Pick.Clip->GetPlayLength() / FrameSeconds) + 2;
	EvaluateFrames(Comp, LengthFrames, FrameSeconds, Pose);
	Test.TestNull(TEXT("a non-looping one-shot has finished its own blend-out by its clip length"),
		Inst->GetCurrentActiveMontage());
	// The publish that lands on the expiry frame therefore takes over a channel whose montage is
	// already gone: a structural no-pop, not a tuned threshold.
	Inst->PublishSelection(Owned, Assets);
	EvaluateFrames(Comp, /*Frames=*/1, FrameSeconds, Pose);
	Test.TestNull(TEXT("and the expiry-frame publish has nothing to cut"),
		Inst->GetCurrentActiveMontage());

	return true;
}

// The reaction branch is on the compiled graph, and it is INERT (LIFE5 slice B1).
//
// The branch replaces the base channel rather than riding over it, so a defect in it is a body
// posing nothing at all -- and every symptom of that is identical to the ones the two tests above
// already guard. What only this test can see is that the branch was compiled in the first place: a
// tag the generator did not stamp, a blend-time pin nothing drove, or an `bActiveValue` bound to a
// property the native class no longer declares all leave a graph that loads, compiles and poses.
//
// Three claims, and no more: the tagged node exists on the compiled class, its two blend times are
// the two the instance publishes rather than the node's own 0.1 default, and with `bReactionActive`
// false the base pose still reaches the output. The reaction pose itself has no producer yet; that
// coverage belongs to the slice that gives it one.
static bool RunGraphReactionBranchCase(FAutomationTestBase& Test, USkeletalMeshComponent* Comp,
	UElysiumBipedAnimInstance* Inst, const FLoopingClipPick& Pick)
{
	constexpr float FrameSeconds = 1.f / 30.f;

	// Nothing publishes to the branch, so the off switch is a default rather than a written value.
	Test.TestFalse(TEXT("the reaction branch is inert -- nothing has activated it"),
		Inst->bReactionActive);

	// **Moved off the property default before the first frame, deliberately.** The engine's own
	// `BlendTime` default is 0.1, which is also this property's default — so an in-time pin that was
	// never wired would read back the number the test expected and pass. 0.17 is a value nothing but
	// the pin can produce.
	Inst->ReactionBlendInSeconds = 0.17f;

	// The bind pose, taken with nothing published: the pose a base channel the branch swallowed
	// would leave on screen, which is what the last assertion below is measured against.
	TArray<FTransform> BindPose;
	EvaluateFrames(Comp, /*Frames=*/1, FrameSeconds, BindPose);
	const int32 PosedBones = BindPose.Num() - 1;
	if (!Test.TestTrue(TEXT("the graph evaluates the whole skeleton"),
		BindPose.Num() == Pick.Mesh->GetRefSkeleton().GetNum() && PosedBones > 0))
	{
		return false;
	}

	// The compiled class's tag table, read the same way `ApplyUpperBodyMask` reads it.
	IAnimClassInterface* AnimClass = IAnimClassInterface::GetFromClass(Inst->GetClass());
	const FAnimSubsystem_Tag* Tags = AnimClass != nullptr
		? AnimClass->FindSubsystem<FAnimSubsystem_Tag>() : nullptr;
	if (!Test.TestNotNull(TEXT("the compiled graph carries a tag table"), Tags))
	{
		return false;
	}
	const FAnimNode_BlendListByBool* Branch = Tags->FindNodeByTag<FAnimNode_BlendListByBool>(
		FName(ElysiumAnimGraph::ReactionBranchTag), Inst);
	if (!Test.TestNotNull(TEXT("and the reaction branch is on it under its own tag"), Branch))
	{
		return false;
	}

	// Read AFTER a frame, deliberately: both blend times arrive through a driven pin, so the values
	// on the node are the instance's own only once the graph has copied its exposed inputs. Reading
	// them before the first update would assert against whatever the pin's literal happened to be.
	const TArray<float>& BlendTimes = Branch->GetBlendTimes();
	if (Test.TestEqual(TEXT("the branch has exactly the two poses it was built with"),
		BlendTimes.Num(), 2))
	{
		Test.AddInfo(FString::Printf(TEXT("reaction blend times: in %.3fs, out %.3fs"),
			BlendTimes[0], BlendTimes[1]));
		// Index 0 is the TRUE pose, so its time is the fade INTO the reaction.
		Test.TestEqual(TEXT("the fade into a reaction is the published in-time"),
			BlendTimes[0], Inst->ReactionBlendInSeconds);
		Test.TestEqual(TEXT("and the fade back to the locomotion pose is the published out-time"),
			BlendTimes[1], Inst->ReactionBlendOutSeconds);
		Test.TestTrue(TEXT("the two are stated apart rather than one value used twice"),
			!FMath::IsNearlyEqual(BlendTimes[0], BlendTimes[1]));
		Test.TestTrue(TEXT("...and the in-time is the moved value, not the node's own 0.1 default"),
			FMath::IsNearlyEqual(BlendTimes[0], 0.17f, 0.001f));
	}

	// The child update mode is the engine's `Default`, and that is a decision rather than an
	// omission. `ResetChildOnActivate` reinitializes a newly-active child only while its weight is
	// still at zero, so it never covers the overlapping retrigger it looks like the fix for — and
	// when a full-weight reaction ENDS it lands on the base child instead, restarting the blend
	// stack's gait at frame 0, wiping the inertializer's pose history and hard-cutting the very
	// release the out-fade exists for.
	//
	// It is one half of a pair: the same `ZERO_ANIMWEIGHT_THRESH` skip also makes the blend stack
	// non-relevant while a full-weight flinch stands, so `bResetOnBecomingRelevant` on the stack has
	// to be false as well or the reset arrives through that door instead.
	// `Elysium.Content.GraphBlendStack` asserts the other half.
	Test.TestEqual(TEXT("the branch reinitializes no child on activation"),
		static_cast<int32>(Branch->GetChildUpdateMode()),
		static_cast<int32>(EBlendListChildUpdateMode::Default));

	// The pose still gets through. A branch whose false pose was mis-wired -- or one whose blend
	// never settled onto the base child -- evaluates the reaction half instead, whose asset pins are
	// both null, and the body stands in its bind pose with nothing logged.
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

	TArray<FTransform> BasePose;
	EvaluateFrames(Comp, /*Frames=*/8, FrameSeconds, BasePose);
	const ElysiumPose::FDeviation Base = ElysiumPose::Measure(BindPose, BasePose);
	Test.AddInfo(FString::Printf(
		TEXT("with the branch inert: %d of %d non-root bones left the bind pose (max %.1f deg)"),
		Base.MovedBones, PosedBones, Base.MaxDegrees));
	Test.TestTrue(TEXT("an inert reaction branch passes the locomotion pose straight through"),
		Base.MovedBones > PosedBones / 4 && Base.MaxDegrees > 5.f);

	return true;
}

// S2 — the locomotion blend stack, on the compiled class, with the settings it was built with.
//
// **Almost every value asserted here is invisible in the tracked graph text**, which is the whole
// reason this test exists. A node property equal to the engine's own default is not written into
// T3D at all, and three of the settings below are exactly that: `BlendOption` IS the engine
// default, and the two engine defaults that are *wrong* for this graph (`BlendspaceUpdateMode`,
// `bResetOnBecomingRelevant`) leave no trace when written correctly either. A generator that
// stopped writing them, or an engine upgrade that renamed one so the `Wire` guard was never
// reached in the first place, produces a graph that exports clean and poses a frozen fan or
// restarts the gait on every flinch.
//
// The other half is the negative: the base channel is one node now, so a compiled class carrying a
// state machine at all means an old package is on the mount.
static bool RunGraphBlendStackCase(FAutomationTestBase& Test, USkeletalMeshComponent* Comp,
	UElysiumBipedAnimInstance* Inst, const FLoopingClipPick& Pick)
{
	constexpr float FrameSeconds = 1.f / 30.f;

	// The compiled class's tag table, read the same way the runtime reads it.
	IAnimClassInterface* AnimClass = IAnimClassInterface::GetFromClass(Inst->GetClass());
	const FAnimSubsystem_Tag* Tags = AnimClass != nullptr
		? AnimClass->FindSubsystem<FAnimSubsystem_Tag>() : nullptr;
	if (!Test.TestNotNull(TEXT("the compiled graph carries a tag table"), Tags))
	{
		return false;
	}
	const FAnimNode_BlendStack* Stack = Tags->FindNodeByTag<FAnimNode_BlendStack>(
		FName(ElysiumAnimGraph::LocomotionStackTag), Inst);
	if (!Test.TestNotNull(TEXT("and the locomotion blend stack is on it under its own tag"), Stack))
	{
		return false;
	}
	// The instance's own predicate answers the same question, and every caller that refuses a body
	// with no base channel goes through it rather than through a tag lookup of its own.
	Test.TestTrue(TEXT("the instance reports the stack it just found"),
		Inst->HasCompiledLocomotionStack());

	// **The curve, and it is load-bearing twice over.** `EAlphaBlendOption::HermiteCubic` is
	// retail's own `3t^2 - 2t^3` (`docs/vtmb/animation_and_movers.md`, the constant at
	// `0x10225158`) — and it is also the engine's default, so it is absent from the graph text and
	// nothing else can see it. `UAnimGraphNode_BlendStack::Serialize` additionally downgrades this
	// property to `Linear` on an old custom version, which is a live path rather than a hypothetical.
	Test.TestEqual(TEXT("the stack blends on retail's own Hermite-cubic curve"),
		static_cast<int32>(Stack->BlendOption),
		static_cast<int32>(EAlphaBlendOption::HermiteCubic));

	// The stack cross-fades its own players. True would make it RAISE an inertialization request
	// instead of blending, and this graph carries no node that answers one — the request would be
	// logged unserviced and the authored duration on the pin would decide nothing.
	Test.TestFalse(TEXT("the stack blends itself rather than raising an inertialization request"),
		Stack->bUseInertialBlend);

	// **And there is no inertialization node to answer one** (LIFE5). Every crossfade this graph
	// performs belongs to a node that owns it already — the stack's own `BlendTime`, the reaction
	// branch's two per-pose times, the slot montage's blend pair — so the node was placed and
	// unreached. It is asserted on the COMPILED class because that is the only place it can be:
	// the tracked graph text says what was built, and this says what the runtime actually carries.
	if (AnimClass != nullptr)
	{
		int32 Inertializers = 0;
		for (const FStructProperty* Property : AnimClass->GetAnimNodeProperties())
		{
			if (Property != nullptr && Property->Struct != nullptr
				&& Property->Struct->IsChildOf(FAnimNode_Inertialization::StaticStruct()))
			{
				++Inertializers;
			}
		}
		Test.TestEqual(TEXT("the compiled graph carries no inertialization node at all"),
			Inertializers, 0);
	}

	Test.TestEqual(TEXT("four concurrent players"), Stack->GetMaxActiveBlends(), 4);

	// **The default that would restart the gait on every flinch.** A full-weight reaction makes
	// this node non-relevant — `FAnimNode_BlendListBase` skips a child under
	// `ZERO_ANIMWEIGHT_THRESH` — and `FAnimNode_BlendStack::NeedsReset` would then `Reset()` it on
	// the frame the flinch releases, dropping the walk back to frame 0.
	Test.TestFalse(TEXT("the stack is not reset when the reaction hands the base pose back"),
		Stack->bResetOnBecomingRelevant);

	// **The default that would freeze a gait fan's steering.** `InitialOnly` samples the blend
	// space's xy once, at `BlendTo`, so `move_yaw` would stop turning the body after the transition
	// that entered the fan.
	Test.TestEqual(TEXT("a fan's steering is re-sampled every frame"),
		static_cast<int32>(Stack->BlendspaceUpdateMode),
		static_cast<int32>(EBlendStack_BlendspaceUpdateMode::UpdateActiveOnly));

	// **The default that would re-blend every frame.** `ConditionalBlendTo` compares the requested
	// blend parameters against the playing player's, and a SEQUENCE player answers the zero vector
	// — so at the engine's 0 threshold a body walking with a non-zero `move_yaw` on a plain clip
	// pushes a new player on every single update.
	Test.TestTrue(TEXT("no steering value can reach the re-blend threshold"),
		Stack->BlendParametersDeltaThreshold > 1000.f);

	// `-1` is the constructed default and is not "wherever the clip is": it is the time a new
	// player is seeded at. Every clip this graph plays starts at its head.
	Test.TestEqual(TEXT("a new player starts at the clip's head"), Stack->AnimationTime, 0.f);

	// The negative, and the one thing only a compiled class can answer: no state machine survives
	// the cutover, so nothing carries a baked `TLT_Inertialization` transition or an eight-state
	// vocabulary any more. A non-empty list here is a stale generated package on the mount.
	if (Test.TestNotNull(TEXT("the compiled class answers the anim-class interface"), AnimClass))
	{
		const TArray<FBakedAnimationStateMachine>& Machines = AnimClass->GetBakedStateMachines();
		Test.AddInfo(FString::Printf(TEXT("baked state machines on the compiled class: %d"),
			Machines.Num()));
		Test.TestEqual(TEXT("the base channel is one blend stack and no state machine at all"),
			Machines.Num(), 0);
	}

	// --- the BlendTime pin is really driven -------------------------------------------------------
	//
	// The property behind a shown pin is dead: the compiler folds the PIN, so a value the generator
	// wrote onto the node would be overwritten by the pin's literal every update. The only proof is
	// to publish a number nothing else in the graph can produce and read it back off the node.
	FElysiumAnimationSelection First;
	First.Generation = 1;
	First.GraphState = EElysiumGraphState::Idle;
	First.SequenceLabel = Pick.Label;
	First.OwnerStem = Pick.Owner;
	First.AnimationName = Pick.Label;
	First.AssetKind = EElysiumAnimAssetKind::Sequence;
	First.Outcome = EElysiumAnimOutcome::Resolved;
	First.FadeSeconds = 0.1f;
	FElysiumResolvedAnimation Assets;
	Assets.Sequence = Pick.Clip;
	Inst->PublishSelection(First, Assets);
	TArray<FTransform> Pose;
	EvaluateFrames(Comp, /*Frames=*/4, FrameSeconds, Pose);

	// 0.4271 is a value no default, no clip and no other rule in this graph produces; the combine
	// is `max`, so the outgoing 0.1 above cannot mask it.
	constexpr float Unique = 0.4271f;
	FElysiumAnimationSelection Second = First;
	Second.Generation = 2;
	Second.FadeSeconds = Unique;
	Inst->PublishSelection(Second, Assets);
	EvaluateFrames(Comp, /*Frames=*/1, FrameSeconds, Pose);

	Test.TestEqual(TEXT("the authored fade reaches the node's BlendTime pin whole"),
		Stack->BlendTime, Unique, 1e-4f);
	Test.TestEqual(TEXT("...and it is the value the instance reported asking for"),
		Inst->GetBlendReport().RequestedSeconds, Unique, 1e-4f);

	return true;
}

// ================================================================================================
// LIFE5 slice B2 — the reaction branch DRIVEN, over a real directional hit fan
// ================================================================================================

namespace
{
	// A baked body carrying a real reaction FAN — a hit activity whose label names a multi-cell grid
	// and whose blend space is on the mount. The table is held rather than borrowed: the grid it
	// answers points into it.
	struct FReactionFanPick
	{
		FString Stem;
		FString Label;
		FString Owner;
		USkeletalMesh* Mesh = nullptr;
		UBlendSpace* Space = nullptr;
		TSharedPtr<FElysiumBlendTable> Table;
		const FElysiumBlendGrid* Grid = nullptr;

		// The axis value cell `Index` sits at. The cells are the range's ENDPOINTS, not its buckets,
		// which is the same rule `ElysiumBlendGrids::ResolveAxis` scales by — so a fractional index is
		// exactly the parameter a blend between two cells is sampled at.
		float AxisAt(float Index) const
		{
			const int32 Count = Grid != nullptr ? Grid->GroupSize[0] : 0;
			if (Count < 2)
			{
				return 0.f;
			}
			return Grid->ParamStart[0]
				+ (Index / static_cast<float>(Count - 1)) * (Grid->ParamEnd[0] - Grid->ParamStart[0]);
		}
	};

	bool TryReactionFan(const FElysiumNpcIndex& Index, const TCHAR* Stem, const TCHAR* Activity,
		FReactionFanPick& Out)
	{
		USkeletalMesh* Mesh = ElysiumNpcVisual::LoadBakedMesh(Stem);
		FElysiumNpcClipSet Vocabulary;
		FString Error;
		if (Mesh == nullptr || !Vocabulary.Load(Stem, Error))
		{
			return false;
		}
		TArray<FString> Labels;
		Vocabulary.Clips.GetKeys(Labels);
		Labels.Sort([](const FString& A, const FString& B) { return A < B; });
		for (const FString& Label : Labels)
		{
			const FElysiumNpcClip& Clip = Vocabulary.Clips[Label];
			// By the ACTIVITY the flinch producer asks for, never by the label's spelling — which is
			// the bank's business and not a contract.
			if (!Clip.Activity.Equals(Activity, ESearchCase::IgnoreCase))
			{
				continue;
			}
			const FString Owner = Clip.IsOwnedBy(Stem) ? FString(Stem) : Clip.Owner;
			const FElysiumNpcIndexEntry* Entry = Index.Npcs.Find(Owner);
			if (Entry == nullptr)
			{
				Entry = Index.Banks.Find(Owner);
			}
			if (Entry == nullptr || Entry->Blends.IsEmpty())
			{
				continue;
			}
			TSharedPtr<FElysiumBlendTable> Table = MakeShared<FElysiumBlendTable>();
			if (!Table->Load(Entry->Blends, Error))
			{
				continue;
			}
			const FElysiumBlendGrid* Grid = Table->Find(Label);
			if (Grid == nullptr || !Grid->IsMultiCell() || Grid->GroupSize[0] < 3)
			{
				continue;
			}
			UBlendSpace* Space = ElysiumNpcVisual::LoadBakedBlendSpace(Mesh, Owner, Label);
			if (Space == nullptr)
			{
				continue;
			}
			if (Space->GetBlendSamples().IsEmpty())
			{
				continue;
			}
			Out.Stem = Stem;
			Out.Label = Label;
			Out.Owner = Owner;
			Out.Mesh = Mesh;
			Out.Space = Space;
			Out.Table = Table;
			Out.Grid = Grid;
			return true;
		}
		return false;
	}

	bool FindReactionFan(FReactionFanPick& Out)
	{
		FElysiumNpcIndex Index;
		FString Error;
		if (!Index.Load(Error) || !Index.IsValid())
		{
			return false;
		}
		static const TCHAR* const Activities[] = { TEXT("ACT_HIT_TORSO"), TEXT("ACT_HIT_HEAD") };
		for (const TCHAR* Activity : Activities)
		{
			for (const TCHAR* Stem : GBodyStems)
			{
				if (TryReactionFan(Index, Stem, Activity, Out))
				{
					return true;
				}
			}
		}
		return false;
	}

	// A looping base clip on the FAN's own body — never whichever body `FindLoopingClip` reached
	// first, because a sequence is bound to one skeleton, every body carries its own, and the two
	// picks are independent searches over the same stem list.
	UAnimSequence* FindBaseClipFor(const FReactionFanPick& Fan)
	{
		FElysiumNpcClipSet Vocabulary;
		FString Error;
		if (!Vocabulary.Load(Fan.Stem, Error))
		{
			return nullptr;
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
			const FString Owner = Clip.IsOwnedBy(Fan.Stem) ? Fan.Stem : Clip.Owner;
			UAnimSequence* Baked = ElysiumNpcVisual::LoadBakedClip(Fan.Mesh, Owner, Label);
			if (Baked != nullptr && Baked->GetPlayLength() >= GMinClipSeconds)
			{
				return Baked;
			}
		}
		return nullptr;
	}

	// One graph-backed body, standing on a resolved locomotion selection. The publish matters: the
	// reaction's blend IN is gated on the graph already posing an asset, exactly as the one-shot
	// slot's is, so a body handed nothing would snap and the fade under test would not run.
	struct FReactionStand
	{
		USkeletalMeshComponent* Comp = nullptr;
		UElysiumBipedAnimInstance* Inst = nullptr;
	};

	bool StandReactionBody(UWorld* World, UClass* Graph, const FReactionFanPick& Fan,
		UAnimSequence* BaseClip, FAutomationTestBase& Test, FReactionStand& Out)
	{
		AActor* Owner = World != nullptr ? World->SpawnActor<AActor>() : nullptr;
		if (!Test.TestNotNull(TEXT("reaction body owner spawned"), Owner))
		{
			return false;
		}
		Out.Inst = StandGraphBody(Owner, Fan.Mesh, Graph, Test, Out.Comp);
		if (Out.Inst == nullptr)
		{
			return false;
		}
		FElysiumAnimationSelection Standing;
		Standing.GraphState = EElysiumGraphState::Idle;
		Standing.SequenceLabel = TEXT("idle");
		Standing.OwnerStem = Fan.Owner;
		Standing.AnimationName = TEXT("idle");
		Standing.AssetKind = EElysiumAnimAssetKind::Sequence;
		Standing.Outcome = EElysiumAnimOutcome::Resolved;
		FElysiumResolvedAnimation Assets;
		Assets.Sequence = BaseClip;
		Out.Inst->PublishSelection(Standing, Assets);
		return true;
	}

	FElysiumReactionPlay FanPlay(UBlendSpace* Space, float AxisValue, float LengthSeconds,
		float BlendIn, float BlendOut)
	{
		FElysiumReactionPlay Play;
		Play.Space = Space;
		Play.AxisValue = AxisValue;
		Play.LengthSeconds = LengthSeconds;
		Play.BlendInSeconds = BlendIn;
		Play.BlendOutSeconds = BlendOut;
		// A hit fan IS the flinch, and the flinch's release condition is retail's own weight envelope:
		// fade in, peak, fade out, no hold between them, with the cell's own length deciding nothing.
		// Driving it any other way here would time the branch off a clip retail never advances.
		Play.Release = EElysiumReactionRelease::Envelope;
		return Play;
	}
}

// The reaction branch, DRIVEN. What only this test can see is that the branch is a fan rather than a
// clip: the pose at an angle BETWEEN two authored reactions differs measurably from both of them,
// which is exactly the pose a snap-to-nearest resolver could never strike and the whole reason a
// directional flinch replaces the base channel instead of riding the one-shot slot.
//
// It runs on the REAL cell length, which is the other thing only this test can see: every hit cell
// VtMB ships bakes to two frames, shorter than the out-fade alone. What keeps a flinch on screen at
// all is therefore NOT the cell — it is retail's own weight envelope, which the play states as its
// release condition (`EElysiumReactionRelease::Envelope`): fade in over 0.1, peak, fade out over
// 0.3, no hold between them. A branch timed off the cell instead is dropped by the update that
// armed it.
//
// Everything else here is the handover: the branch fades in rather than snapping, the phase clock
// drops `bReactionActive` one out-fade before the branch's end so the fade completes ON that end,
// and a Scene-band clip takes the branch back with nothing left holding it.
static bool RunGraphReactionDriveCase(FAutomationTestBase& Test)
{
	FTestWorldWrapper TestWorld;
	UClass* Graph = nullptr;
	FString CoreMessage;
	switch (EnsureCoreSetup(Test, TestWorld, Graph, CoreMessage))
	{
	case ESharedSetupResult::Abstain:
		Test.AddInfo(CoreMessage);
		return true;
	case ESharedSetupResult::Failed:
		return false;
	default:
		break;
	}
	UWorld* World = TestWorld.GetTestWorld();

	FReactionFanPick Fan;
	if (!FindReactionFan(Fan))
	{
		Test.AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no baked body in the slice carries a directional hit fan; "
			"run: uv run elysium export characters"));
		return true;
	}
	UAnimSequence* BaseClip = FindBaseClipFor(Fan);
	if (BaseClip == nullptr)
	{
		Test.AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: the fan's body carries no looping base clip to stand on"));
		return true;
	}
	Fan.Mesh->AddToRoot();
	Fan.Space->AddToRoot();
	BaseClip->AddToRoot();
	ON_SCOPE_EXIT
	{
		BaseClip->RemoveFromRoot();
		Fan.Space->RemoveFromRoot();
		Fan.Mesh->RemoveFromRoot();
	};

	// Two adjacent cells that are genuinely different clips, and the parameter halfway between them.
	int32 LowCell = INDEX_NONE;
	for (int32 Cell = 0; Cell + 1 < Fan.Grid->GroupSize[0]; ++Cell)
	{
		const FElysiumBlendCell* A = Fan.Grid->CellAt(Cell, 0);
		const FElysiumBlendCell* B = Fan.Grid->CellAt(Cell + 1, 0);
		if (A != nullptr && B != nullptr && !A->Clip.IsEmpty() && !B->Clip.IsEmpty()
			&& !A->Clip.Equals(B->Clip, ESearchCase::IgnoreCase))
		{
			LowCell = Cell;
			break;
		}
	}
	if (LowCell == INDEX_NONE)
	{
		Test.AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: the fan carries no two adjacent distinct cells"));
		return true;
	}
	const float AxisLow = Fan.AxisAt(static_cast<float>(LowCell));
	const float AxisMid = Fan.AxisAt(static_cast<float>(LowCell) + 0.5f);
	const float AxisHigh = Fan.AxisAt(static_cast<float>(LowCell) + 1.0f);
	const float ClipSeconds = ElysiumEntityAnimation::BlendedGridLengthSeconds(Fan.Space, AxisMid);
	Test.AddInfo(FString::Printf(
		TEXT("'%s' plays fan '%s'@'%s': cells %d/%d at %.1f / %.1f, midpoint %.1f, clip %.3fs"),
		*Fan.Stem, *Fan.Label, *Fan.Owner, LowCell, LowCell + 1, AxisLow, AxisHigh, AxisMid,
		ClipSeconds));
	if (!Test.TestTrue(TEXT("the fan reports a blended length"), ClipSeconds > 0.f))
	{
		return false;
	}
	// **The cell's own length, driven as it ships.** `PlayReaction`'s contract is that the caller
	// states the length — the branch has no clip of its own to ask — and the number the producer
	// states is exactly this one (`Elysium.Content.ReactionGridLength` asserts it against the assets).
	// On a two-frame cell that is shorter than the out-fade, so the branch stands for its minimum
	// hold instead; a stand-in length here would prove the mechanism on a clip the game does not have.
	const float LengthSeconds = ClipSeconds;
	// The asset's own axis and samples, reported rather than assumed: a fan whose baked axis range
	// disagrees with the sidecar's would be steered outside its samples and evaluate one cell at every
	// angle, which reads on screen as a snap and reads here as three identical poses.
	{
		const FBlendParameter& Parameter = Fan.Space->GetBlendParameter(0);
		FString Line = FString::Printf(TEXT("%s axis [%.1f, %.1f]: "), *Fan.Space->GetName(),
			Parameter.Min, Parameter.Max);
		for (const FBlendSample& Sample : Fan.Space->GetBlendSamples())
		{
			Line += FString::Printf(TEXT("%.1f=%s(%.3fs) "), Sample.SampleValue.X,
				*GetNameSafe(Sample.Animation),
				Sample.Animation != nullptr ? Sample.Animation->GetPlayLength() : 0.f);
		}
		Test.AddInfo(Line);
	}

	// Four bodies, ticked in lockstep so their base clips share a phase: the two cells the midpoint
	// sits between, the midpoint itself, and a CONTROL that is never hit. The control is what the
	// return to the base pose is measured against — a body that keeps playing its idle for the
	// reaction's whole length is not standing where it stood when it was hit, so the pre-hit pose
	// answers a question about the base clip's own motion rather than about the branch.
	constexpr float FrameSeconds = 1.f / 30.f;
	constexpr float BlendIn = 0.1f;
	constexpr float BlendOut = 0.3f;
	// The branch's own hold, restated so the frame counts below read against it. It is
	// `FElysiumReactionPlay::ActiveSeconds` for an `Envelope` play — the recovered `DamageFlinch`
	// triangle, which peaks at the blend-in and carries no hold after it, whatever the cell's own
	// length is.
	const float HoldSeconds = BlendIn;
	// At least three, because the envelope's peak is the blend-in and the blend-in is three frames.
	const int32 HoldFrames = FMath::CeilToInt32(HoldSeconds / FrameSeconds);
	Test.AddInfo(FString::Printf(TEXT("clip %.4fs, in %.2f, out %.2f -> hold %.4fs (%d frames)"),
		LengthSeconds, BlendIn, BlendOut, HoldSeconds, HoldFrames));
	if (!Test.TestTrue(TEXT("the hold outlasts the two measurement frames"), HoldFrames >= 3))
	{
		return false;
	}
	FReactionStand Low, Mid, High, Control;
	if (!StandReactionBody(World, Graph, Fan, BaseClip, Test, Low)
		|| !StandReactionBody(World, Graph, Fan, BaseClip, Test, Mid)
		|| !StandReactionBody(World, Graph, Fan, BaseClip, Test, High)
		|| !StandReactionBody(World, Graph, Fan, BaseClip, Test, Control))
	{
		return false;
	}
	FReactionStand* const Stands[] = { &Low, &Mid, &High, &Control };
	ON_SCOPE_EXIT
	{
		for (FReactionStand* Stand : Stands)
		{
			if (Stand->Comp != nullptr && Stand->Comp->GetOwner() != nullptr)
			{
				World->DestroyActor(Stand->Comp->GetOwner());
			}
		}
	};
	auto TickAll = [&Stands](int32 Frames)
	{
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			for (FReactionStand* Stand : Stands)
			{
				Stand->Comp->TickAnimation(FrameSeconds, /*bNeedsValidRootMotion=*/false);
				Stand->Comp->RefreshBoneTransforms(/*TickFunction=*/nullptr);
			}
		}
	};

	// The locomotion pose, with the branch still inert. Also what arms the blend IN: the reaction's
	// in-fade is gated on the graph already posing an asset, exactly as the montage slot's is.
	TickAll(3);
	const TArray<FTransform> BasePose = Mid.Comp->GetComponentSpaceTransforms();
	const int32 PosedBones = BasePose.Num() - 1;
	if (!Test.TestTrue(TEXT("the graph evaluates the whole skeleton"), PosedBones > 0))
	{
		return false;
	}

	Test.TestTrue(TEXT("the compiled graph carries the reaction branch"),
		Mid.Inst->HasCompiledReactionBranch());
	Test.TestTrue(TEXT("the low cell's reaction is accepted"),
		Low.Inst->PlayReaction(FanPlay(Fan.Space, AxisLow, LengthSeconds, BlendIn, BlendOut)));
	Test.TestTrue(TEXT("the midpoint's reaction is accepted"),
		Mid.Inst->PlayReaction(FanPlay(Fan.Space, AxisMid, LengthSeconds, BlendIn, BlendOut)));
	Test.TestTrue(TEXT("the high cell's reaction is accepted"),
		High.Inst->PlayReaction(FanPlay(Fan.Space, AxisHigh, LengthSeconds, BlendIn, BlendOut)));
	Test.TestTrue(TEXT("the branch reports itself active"), Mid.Inst->bReactionActive);
	Test.TestEqual(TEXT("...steered at the angle it was handed"), Mid.Inst->ReactionAxis0, AxisMid);
	Test.TestTrue(TEXT("...as a fan rather than a clip"), Mid.Inst->bReactionHasBlendSpace);

	// **The blocker this seed exists for.** A two-frame cell less the out-fade is a negative number:
	// a branch timed off the CELL seeds its clock at zero and the very first update drops it, so a
	// real flinch never reaches the frame at all. The envelope is what it is timed off instead.
	TickAll(1);
	const ElysiumPose::FDeviation FirstFrame =
		ElysiumPose::Measure(BasePose, Mid.Comp->GetComponentSpaceTransforms());
	Test.TestTrue(TEXT("the branch survives the update that armed it"), Mid.Inst->bReactionActive);

	TickAll(1);
	const TArray<FTransform> LowPose = Low.Comp->GetComponentSpaceTransforms();
	const TArray<FTransform> MidPose = Mid.Comp->GetComponentSpaceTransforms();
	const TArray<FTransform> HighPose = High.Comp->GetComponentSpaceTransforms();
	Test.TestTrue(TEXT("...and is still holding two frames in"), Mid.Inst->bReactionActive);

	const ElysiumPose::FDeviation TookTheFrame = ElysiumPose::Measure(BasePose, MidPose);
	Test.AddInfo(FString::Printf(TEXT("reaction vs locomotion: %d of %d bones moved (max %.1f deg)"),
		TookTheFrame.MovedBones, PosedBones, TookTheFrame.MaxDegrees));
	Test.TestTrue(TEXT("the reaction replaces the locomotion pose"),
		TookTheFrame.MovedBones > PosedBones / 4 && TookTheFrame.MaxDegrees > 5.f);

	// **It FADES in rather than snapping**, which is the whole of what `BlendTime_0` being driven off
	// `ReactionBlendInSeconds` buys: a branch handed a zero in-time — an unwired pin, a pin folded to
	// the node's own default — would be at full weight on the first frame and identical on the
	// second. The out-fade's own shape is asserted by the return to base below.
	Test.AddInfo(FString::Printf(TEXT("in-fade: %.1f deg after one frame, %.1f deg after two"),
		FirstFrame.MaxDegrees, TookTheFrame.MaxDegrees));
	Test.TestTrue(TEXT("the branch fades in over its own blend time rather than snapping"),
		TookTheFrame.MaxDegrees > FirstFrame.MaxDegrees);

	// **The two-cell mix, which is the whole point.** The midpoint is an even blend of two authored
	// reactions, so it is a pose neither of them strikes. A resolver that quantized the angle to its
	// nearest cell — or a branch that played one sequence instead of the fan — would make one of
	// these two deviations vanish.
	const ElysiumPose::FDeviation FromLow = ElysiumPose::Measure(LowPose, MidPose);
	const ElysiumPose::FDeviation FromHigh = ElysiumPose::Measure(HighPose, MidPose);
	Test.AddInfo(FString::Printf(
		TEXT("midpoint vs cell %d: max %.1f deg (%d bones); vs cell %d: max %.1f deg (%d bones)"),
		LowCell, FromLow.MaxDegrees, FromLow.MovedBones,
		LowCell + 1, FromHigh.MaxDegrees, FromHigh.MovedBones));
	Test.TestTrue(TEXT("the mid-cell pose differs from the cell below it"), FromLow.MaxDegrees > 1.f);
	Test.TestTrue(TEXT("...and from the cell above it"), FromHigh.MaxDegrees > 1.f);

	// **The de-snap.** The phase clock drops `bReactionActive` one out-fade before the branch's end,
	// so the fade back completes ON that end — the same instant the Reaction claim's `TotalSeconds`
	// expires and the locomotion publish resumes. A branch that stayed active until the end would
	// still be fading 0.3s after the base had been handed back.
	if (HoldFrames - 1 > 2)
	{
		TickAll(HoldFrames - 1 - 2);
	}
	Test.TestTrue(TEXT("the branch is still active a frame short of the hold"),
		Mid.Inst->bReactionActive);
	TickAll(2);
	Test.TestFalse(TEXT("and drops at the hold"), Mid.Inst->bReactionActive);

	// Past the branch's whole life: the out-fade has run and the locomotion pose owns the frame
	// again. Measured against the CONTROL rather than against the pre-hit pose, because the base
	// child keeps advancing under the fades — a body that has been reacting for 0.4s is standing
	// where the idle put it, not where it was hit.
	TickAll(FMath::CeilToInt32(BlendOut / FrameSeconds) + 2);
	const TArray<FTransform> BackPose = Mid.Comp->GetComponentSpaceTransforms();
	const TArray<FTransform> ControlPose = Control.Comp->GetComponentSpaceTransforms();
	const ElysiumPose::FDeviation Returned = ElysiumPose::Measure(ControlPose, BackPose);
	const ElysiumPose::FDeviation Drift = ElysiumPose::Measure(BasePose, ControlPose);
	Test.AddInfo(FString::Printf(
		TEXT("after %.3fs: %d bones off the un-hit control (max %.1f deg); the idle itself moved "
		     "%.1f deg over the same span, against %.1f deg at the height of the reaction"),
		HoldSeconds + BlendOut, Returned.MovedBones, Returned.MaxDegrees, Drift.MaxDegrees,
		TookTheFrame.MaxDegrees));
	Test.TestTrue(TEXT("the reacting body is back on the locomotion pose the control holds"),
		Returned.MaxDegrees < TookTheFrame.MaxDegrees * 0.25f);

	// A Scene-band clip owns the body outright, and it takes the branch with it: a reaction left
	// active behind a standing clip would pin its own assets and count down a phase nothing is
	// showing.
	Test.TestTrue(TEXT("the low body's reaction is re-armed for the scene case"),
		Low.Inst->PlayReaction(FanPlay(Fan.Space, AxisLow, LengthSeconds, BlendIn, BlendOut)));
	TickAll(2);
	Test.TestTrue(TEXT("...and is running"), Low.Inst->bReactionActive);
	Low.Inst->PlayClip(FElysiumClipIdentity(), BaseClip, /*bLoop=*/true);
	Test.TestFalse(TEXT("a scene clip stops the reaction outright"), Low.Inst->bReactionActive);

	return true;
}

// The length a reaction is timed by is the ENGINE's, not the base cell's.
//
// A hit fan's cells are separate authored reactions that do not share a length, and the graph plays
// a blend of the two the angle sits between — so the only honest duration at a mid-cell parameter is
// the one the blend space itself reports. A caller timing off the label's own clip ends the reaction
// early or late by exactly the difference between two cells.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumReactionGridLengthTest,
	"Elysium.Content.ReactionGridLength", GElysiumMontageSlotFlags)
bool FElysiumReactionGridLengthTest::RunTest(const FString&)
{
	if (FElysiumContentPaths::IsIncomplete(TEXT("npc")))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: the npc export domain is marked incomplete"));
		return true;
	}
	FReactionFanPick Fan;
	if (!FindReactionFan(Fan))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no baked body in the slice carries a directional hit fan; "
			"run: uv run elysium export characters"));
		return true;
	}
	Fan.Mesh->AddToRoot();
	Fan.Space->AddToRoot();
	ON_SCOPE_EXIT
	{
		Fan.Space->RemoveFromRoot();
		Fan.Mesh->RemoveFromRoot();
	};

	TestTrue(TEXT("a null space reports no length rather than an instant clip"),
		ElysiumEntityAnimation::BlendedGridLengthSeconds(nullptr, 0.f) <= 0.f);

	int32 Checked = 0;
	for (int32 Cell = 0; Cell + 1 < Fan.Grid->GroupSize[0]; ++Cell)
	{
		const FElysiumBlendCell* Low = Fan.Grid->CellAt(Cell, 0);
		const FElysiumBlendCell* High = Fan.Grid->CellAt(Cell + 1, 0);
		if (Low == nullptr || High == nullptr || Low->Clip.IsEmpty() || High->Clip.IsEmpty())
		{
			continue;
		}
		UAnimSequence* LowClip = ElysiumNpcVisual::LoadBakedClip(Fan.Mesh, Fan.Owner, Low->Clip);
		UAnimSequence* HighClip = ElysiumNpcVisual::LoadBakedClip(Fan.Mesh, Fan.Owner, High->Clip);
		if (LowClip == nullptr || HighClip == nullptr)
		{
			continue;
		}
		++Checked;

		// At a cell, the engine's answer IS that cell's clip. Asserted first because it is what makes
		// the bracket below a statement about interpolation rather than about arithmetic.
		const float AtLow = ElysiumEntityAnimation::BlendedGridLengthSeconds(
			Fan.Space, Fan.AxisAt(static_cast<float>(Cell)));
		TestTrue(*FString::Printf(TEXT("cell %d reports its own clip's length (%.4f vs %.4f)"),
			Cell, AtLow, LowClip->GetPlayLength()),
			FMath::IsNearlyEqual(AtLow, LowClip->GetPlayLength(), 0.01f));

		// Between them it is bracketed by the pair, which is what "blended length" means.
		const float Mid = ElysiumEntityAnimation::BlendedGridLengthSeconds(
			Fan.Space, Fan.AxisAt(static_cast<float>(Cell) + 0.5f));
		const float Lower = FMath::Min(LowClip->GetPlayLength(), HighClip->GetPlayLength());
		const float Upper = FMath::Max(LowClip->GetPlayLength(), HighClip->GetPlayLength());
		TestTrue(*FString::Printf(
			TEXT("the midpoint of cells %d/%d is bracketed by them (%.4f in [%.4f, %.4f])"),
			Cell, Cell + 1, Mid, Lower, Upper),
			Mid >= Lower - 0.01f && Mid <= Upper + 0.01f);
		TestTrue(*FString::Printf(TEXT("...and is a positive duration (%.4f)"), Mid), Mid > 0.f);
	}
	AddInfo(FString::Printf(TEXT("fan '%s'@'%s': %d adjacent cell pair(s) checked"),
		*Fan.Label, *Fan.Owner, Checked));
	TestTrue(TEXT("at least one adjacent pair was checked"), Checked > 0);

	return true;
}

// The shipped hit cells carry no layer mask — a design assumption, turned into a check.
//
// The reaction branch replaces the WHOLE base pose. A cell that shipped as a partial-body overlay
// (a `UElysiumAnimLayerMask` naming the bones it owns) would compose correctly only through the
// layered blend, and standing it as a base pose would hold every bone outside its gate at bind — a
// body reacting with a frozen lower half. Nothing in the branch reads a mask, so this is what says
// the content never needed one.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumReactionCellMasksTest,
	"Elysium.Content.ReactionCellMasks", GElysiumMontageSlotFlags)
bool FElysiumReactionCellMasksTest::RunTest(const FString&)
{
	if (FElysiumContentPaths::IsIncomplete(TEXT("npc")))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: the npc export domain is marked incomplete"));
		return true;
	}
	FReactionFanPick Fan;
	if (!FindReactionFan(Fan))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no baked body in the slice carries a directional hit fan; "
			"run: uv run elysium export characters"));
		return true;
	}
	Fan.Mesh->AddToRoot();
	Fan.Space->AddToRoot();
	ON_SCOPE_EXIT
	{
		Fan.Space->RemoveFromRoot();
		Fan.Mesh->RemoveFromRoot();
	};

	// The blend space's own samples rather than the sidecar's cells: what the branch evaluates is the
	// asset, so the asset is what is asked.
	int32 Samples = 0;
	int32 Masked = 0;
	int32 Additive = 0;
	for (const FBlendSample& Sample : Fan.Space->GetBlendSamples())
	{
		if (Sample.Animation == nullptr)
		{
			continue;
		}
		++Samples;
		if (const UElysiumAnimLayerMask* Mask =
			Sample.Animation->FindMetaDataByClass<UElysiumAnimLayerMask>())
		{
			++Masked;
			AddError(FString::Printf(
				TEXT("reaction cell '%s' carries the layer mask '%s' (%d bones); the reaction branch "
				     "poses the whole body and would hold every bone outside it at bind"),
				*Sample.Animation->GetName(), *Mask->Profile.ToString(), Mask->OwnedBones));
		}
		if (Sample.Animation->IsValidAdditive())
		{
			++Additive;
			AddError(FString::Printf(
				TEXT("reaction cell '%s' is an additive; the branch stands it as a base pose, which "
				     "folds the skeleton rather than animating it"),
				*Sample.Animation->GetName()));
		}
	}
	AddInfo(FString::Printf(TEXT("fan '%s'@'%s': %d sample(s), %d masked, %d additive"),
		*Fan.Label, *Fan.Owner, Samples, Masked, Additive));
	TestTrue(TEXT("the fan carries samples to check"), Samples > 0);

	return true;
}

// The residual half of the null-outgoing refusal: a publish that resolved NOTHING is not an operand.
//
// `ElysiumAnimGraph::TransitionSeconds` refuses a null outgoing descriptor because a body that has
// published nothing poses the skeleton's bind pose, and fading up out of that is a T-pose on screen
// for the whole blend. `bHasApplied` is not the fact that answers it: it goes true on the FIRST
// update of any body, asset or no asset, so a first publish whose record resolved no clip presents a
// non-null descriptor to the generation after it — while the stack underneath is still evaluating
// an un-published pin. The same T-pose, through the front door.
//
// It is asserted on the real generated graph because the rule it guards is about what the graph is
// EVALUATING, which the pure function cannot see. `GetBlendReport` is the observable: the decision is
// recorded where it is made, because the fade it produces is indistinguishable after the fact from a
// correct hard cut.
static bool RunGraphFirstAssetBlendCase(FAutomationTestBase& Test, USkeletalMeshComponent* Comp,
	UElysiumBipedAnimInstance* Inst, const FLoopingClipPick& Pick)
{
	constexpr float FrameSeconds = 1.f / 30.f;
	TArray<FTransform> Pose;

	// **The body arrives in the defect's own state without being put there.** A registered instance
	// updates whether or not anything has published, so its first update applies the default record
	// — no sequence, no blend space — and `bHasApplied` goes true on it. That is generation 0, and
	// from here on every resolved-nothing publish is HELD rather than applied, which is the correct
	// behaviour and the reason the state persists: the applied record goes on naming no asset while
	// the stack goes on posing the bind pose.
	EvaluateFrames(Comp, /*Frames=*/2, FrameSeconds, Pose);
	Test.TestTrue(TEXT("a body that has published nothing is posing the bind pose"),
		Inst->GetAppliedSelection().AssetKind == EElysiumAnimAssetKind::None);

	// A request that resolved nothing, which is not a contrived record: the controlled corpus records
	// exactly one on a validated player body, a ducked phase-8 landing asking for ACT_LAND_CROUCH
	// whose selection returns -1 (`docs/vtmb/animation_and_movers.md`). It is held, and holding is
	// what keeps the applied record describing a body that poses nothing.
	FElysiumAnimationSelection Missed;
	Missed.Generation = 1;
	Missed.GraphState = EElysiumGraphState::Idle;
	Missed.SequenceLabel = Pick.Label;
	Missed.OwnerStem = Pick.Owner;
	Missed.AssetKind = EElysiumAnimAssetKind::None;
	Missed.Outcome = EElysiumAnimOutcome::MissingSequence;
	Missed.FadeSeconds = UElysiumBodyAnimInstance::DefaultBlendSeconds;
	Inst->PublishSelection(Missed, FElysiumResolvedAnimation());
	EvaluateFrames(Comp, /*Frames=*/2, FrameSeconds, Pose);
	Test.TestTrue(TEXT("a publish that resolved no asset holds the pose it had"), Inst->IsHoldingPose());

	// The first record that carries a real clip. Its outgoing operand exists and names a non-zero
	// authored fade, so a gate asking only "has anything been published" hands the inertializer that
	// whole duration of blending up out of the bind pose the stack is still evaluating. The fade is
	// stated on the record rather than taken off the clip, because a clip that authors a hard cut
	// answers zero for a different reason and the two must not be confused here.
	FElysiumAnimationSelection Resolved = Missed;
	Resolved.Generation = 2;
	Resolved.AnimationName = Pick.Label;
	Resolved.AssetKind = EElysiumAnimAssetKind::Sequence;
	Resolved.Outcome = EElysiumAnimOutcome::Resolved;
	Resolved.bSnap = false;
	Resolved.FadeSeconds = UElysiumBodyAnimInstance::DefaultBlendSeconds;
	FElysiumResolvedAnimation Assets;
	Assets.Sequence = Pick.Clip;
	Inst->PublishSelection(Resolved, Assets);
	EvaluateFrames(Comp, /*Frames=*/2, FrameSeconds, Pose);

	const FElysiumBlendReport& Blend = Inst->GetBlendReport();
	Test.AddInfo(FString::Printf(
		TEXT("first real clip over a body that had posed nothing: %.3fs requested (snap=%d, "
		     "nothing-to-fade-from=%d)"),
		Blend.RequestedSeconds, Blend.bSnap ? 1 : 0, Blend.bFirstPublish ? 1 : 0));
	Test.TestEqual(TEXT("the real clip's own generation is the one reported"), Blend.Generation, 2u);
	Test.TestEqual(TEXT("and it snaps in rather than inertializing up out of the bind pose"),
		Blend.RequestedSeconds, 0.f);
	Test.TestTrue(TEXT("...named as having no outgoing clip to fade from, not as an authored hard cut"),
		Blend.bFirstPublish && !Blend.bSnap);

	// The control, and it is what makes the assertion above about the POSED-AN-ASSET verdict rather
	// than about first publishes: the next generation follows a record that DID pose a clip, so it
	// fades over the authored duration.
	//
	// It also re-publishes the SAME clip, which the blend stack refuses to re-blend onto — retail's
	// own reselect-without-reset, reached here by the node's asset comparison rather than by a rule
	// of ours. What is asserted is the duration the instance asked for; the pose does not restart.
	FElysiumAnimationSelection Again = Resolved;
	Again.Generation = 3;
	Inst->PublishSelection(Again, Assets);
	EvaluateFrames(Comp, /*Frames=*/2, FrameSeconds, Pose);
	Test.TestEqual(TEXT("the generation after a posed record fades over the authored duration"),
		Inst->GetBlendReport().RequestedSeconds, UElysiumBodyAnimInstance::DefaultBlendSeconds);
	Test.TestFalse(TEXT("...and is not reported as having nothing to fade from"),
		Inst->GetBlendReport().bFirstPublish);

	return true;
}

namespace
{
	// A baked partial-body LAYER clip on a body whose own skeleton carries the mask it names, plus an
	// ordinary base clip from the same body to compose it over.
	//
	// **Both halves of the mask have to be found, and neither can be assumed.** The bake writes
	// `UElysiumAnimLayerMask` onto a sequence whenever a bone on the emitted skeleton is unowned,
	// while the `UBlendProfile` that name resolves to belongs to the skeleton being PLAYED — and a
	// bank owns essentially every shipped layer, so those are routinely different assets. A pick that
	// checked only the metadata would stand a layer the graph then refuses, and prove nothing about
	// the projection.
	//
	// The base clip comes off the SAME body: a sequence is bound to one skeleton, and the two searches
	// walk one stem at a time for that reason.
	struct FSlotLayerPick
	{
		FString Stem;
		FString Label;
		FString Owner;
		USkeletalMesh* Mesh = nullptr;
		UAnimSequence* Layer = nullptr;
		UAnimSequence* BaseClip = nullptr;
		FName MaskName;
	};

	bool FindSlotLayer(FSlotLayerPick& Out)
	{
		for (const TCHAR* Stem : GBodyStems)
		{
			USkeletalMesh* Mesh = ElysiumNpcVisual::LoadBakedMesh(Stem);
			USkeleton* Skeleton = Mesh != nullptr ? Mesh->GetSkeleton() : nullptr;
			FElysiumNpcClipSet Vocabulary;
			FString Error;
			if (Skeleton == nullptr || !Vocabulary.Load(Stem, Error))
			{
				continue;
			}
			TArray<FString> Labels;
			Vocabulary.Clips.GetKeys(Labels);
			Labels.Sort([](const FString& A, const FString& B) { return A < B; });

			FSlotLayerPick Found;
			Found.Stem = Stem;
			Found.Mesh = Mesh;
			for (const FString& Label : Labels)
			{
				const FElysiumNpcClip& Clip = Vocabulary.Clips[Label];
				// An additive is the OTHER kind of partial pose and composes through its own node; a
				// clip outside the length band is either a two-frame cell or a whole performance,
				// neither of which reads as a phase.
				if (Clip.IsAdditive() || Clip.Seconds() < GMinClipSeconds
					|| Clip.Seconds() > GMaxClipSeconds)
				{
					continue;
				}
				const FString Owner = Clip.IsOwnedBy(Stem) ? FString(Stem) : Clip.Owner;
				UAnimSequence* Baked = ElysiumNpcVisual::LoadBakedClip(Mesh, Owner, Label);
				if (Baked == nullptr || Baked->GetPlayLength() < GMinClipSeconds)
				{
					continue;
				}
				const UElysiumAnimLayerMask* Mask =
					Baked->FindMetaDataByClass<UElysiumAnimLayerMask>();
				if (Mask == nullptr)
				{
					if (Found.BaseClip == nullptr && (Clip.Flags & 0x1) != 0)
					{
						Found.BaseClip = Baked;
					}
					continue;
				}
				if (Found.Layer != nullptr)
				{
					continue;
				}
				const UBlendProfile* Profile = Skeleton->GetBlendProfile(Mask->Profile);
				if (Profile == nullptr || Profile->Mode != EBlendProfileMode::BlendMask)
				{
					continue;
				}
				Found.Layer = Baked;
				Found.Label = Label;
				Found.Owner = Owner;
				Found.MaskName = Mask->Profile;
			}
			if (Found.Layer != nullptr && Found.BaseClip != nullptr)
			{
				Out = Found;
				return true;
			}
		}
		return false;
	}

	// The overlay slot's own layered blend on a standing instance, found the way the instance finds
	// it: by tag, off the compiled class. Null on a graph generated before the slot node existed.
	FAnimNode_LayeredBoneBlend* FindSlotBlend(UElysiumBipedAnimInstance* Inst)
	{
		IAnimClassInterface* AnimClass = Inst != nullptr
			? IAnimClassInterface::GetFromClass(Inst->GetClass()) : nullptr;
		const FAnimSubsystem_Tag* Tags = AnimClass != nullptr
			? AnimClass->FindSubsystem<FAnimSubsystem_Tag>() : nullptr;
		return Tags != nullptr
			? Tags->FindNodeByTag<FAnimNode_LayeredBoneBlend>(
				FName(ElysiumAnimGraph::SlotLayerTag), Inst)
			: nullptr;
	}
}

// The overlay SLOT, from the seam a producer arms to the pins the graph evaluates.
//
// Retail's `CBaseAnimatingOverlay` slot 0 — the masked partial-body layer every ranged fire, reload
// and dry-fire composes through. Everything above the record is asserted content-free
// (`Elysium.Substrate.AnimationArbitration` owns the envelope and the phase,
// `Elysium.Substrate.AnimationGraph` owns the two pin functions); what only a real body can answer
// is whether the arm seam, the projection and the mask write agree with them — and whether the blend
// node the mask is written on exists at all, which is a property of the GENERATED graph.
//
// Five things live here and nowhere else:
//
//  * `PlaySlotLayer`'s three refusals — no sequence, no baked bone mask, and a claim with no clip
//    length. The last is the one that reads as working: a layer with no phase is one still frame of
//    its clip, pinned on the evaluator at a fixed weight for as long as the producer holds the
//    channel, which on screen is an arm that stopped rather than an arm that never moved.
//  * the projection, both ways — a published record drives the three pins, and a record that has
//    stopped naming a layer takes them back down. `HasSlotLayer()` is a pointer test, so a weight or
//    a playhead left behind by a claim that has gone describes a layer the record no longer names.
//  * `StopSlotLayer` taking the pose down WITHOUT another update, which is the whole point of it:
//    its callers are a run's stop path and the death transaction, and a frozen corpse never runs
//    another update to clear the staging with.
//  * `ApplySlotMask`'s refusal, said ONCE. The refused name never reaches `SetBlendMask`, so the
//    applied name cannot latch it, and a per-frame line for the life of a standing layer buries
//    every other thing in the log.
//  * the channel's PHASE, which is what makes a ranged commit land on its own clip: the shot clips
//    carry the 3030-3044 commit ids and they compose here, `FElysiumWeapon` resolves one and asks
//    whether a polled channel is standing on it in the same statement pair, and the answer has to be
//    a record separate from the base pose's — the layer and whatever owns the base are standing on
//    their own clips at the same time.
static bool RunGraphSlotLayerCase(FAutomationTestBase& Test)
{
	FTestWorldWrapper TestWorld;
	UClass* Graph = nullptr;
	FString CoreMessage;
	switch (EnsureCoreSetup(Test, TestWorld, Graph, CoreMessage))
	{
	case ESharedSetupResult::Abstain:
		Test.AddInfo(CoreMessage);
		return true;
	case ESharedSetupResult::Failed:
		return false;
	default:
		break;
	}
	UWorld* World = TestWorld.GetTestWorld();

	FSlotLayerPick Pick;
	if (!FindSlotLayer(Pick))
	{
		Test.AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no baked body in the slice carries a masked layer clip "
			"whose blend profile is on its own skeleton; run: uv run elysium export characters"));
		return true;
	}
	Pick.Mesh->AddToRoot();
	Pick.Layer->AddToRoot();
	Pick.BaseClip->AddToRoot();
	ON_SCOPE_EXIT
	{
		Pick.BaseClip->RemoveFromRoot();
		Pick.Layer->RemoveFromRoot();
		Pick.Mesh->RemoveFromRoot();
	};
	const float LayerSeconds = Pick.Layer->GetPlayLength();
	Test.AddInfo(FString::Printf(TEXT("'%s' layers '%s'@'%s' (%.3fs) through mask '%s', over base '%s'"),
		*Pick.Stem, *Pick.Label, *Pick.Owner, LayerSeconds, *Pick.MaskName.ToString(),
		*GetNameSafe(Pick.BaseClip)));

	AActor* Owner = World != nullptr ? World->SpawnActor<AActor>() : nullptr;
	if (!Test.TestNotNull(TEXT("slot body owner spawned"), Owner))
	{
		return false;
	}
	ON_SCOPE_EXIT
	{
		if (World != nullptr && Owner != nullptr)
		{
			World->DestroyActor(Owner);
		}
	};
	USkeletalMeshComponent* Comp = nullptr;
	UElysiumBipedAnimInstance* Inst = StandGraphBody(Owner, Pick.Mesh, Graph, Test, Comp);
	if (Inst == nullptr)
	{
		return false;
	}
	FAnimNode_LayeredBoneBlend* SlotBlend = FindSlotBlend(Inst);
	if (SlotBlend == nullptr)
	{
		Test.AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: the generated graph carries no overlay-slot blend under "
			"the ElysiumSlotLayer tag (run: uv run elysium export bundle policy)"));
		return true;
	}

	constexpr float FrameSeconds = 1.f / 30.f;
	TArray<FTransform> Pose;

	// The base pose the layer composes over. It matters that one is standing: the slot is deliberately
	// NOT gated on who owns the base, and a body posing nothing would let a broken gate pass.
	FElysiumAnimationSelection Standing;
	Standing.GraphState = EElysiumGraphState::Idle;
	Standing.SequenceLabel = TEXT("idle");
	Standing.OwnerStem = Pick.Stem;
	Standing.AnimationName = TEXT("idle");
	Standing.AssetKind = EElysiumAnimAssetKind::Sequence;
	Standing.Outcome = EElysiumAnimOutcome::Resolved;
	FElysiumResolvedAnimation BaseOnly;
	BaseOnly.Sequence = Pick.BaseClip;
	Inst->PublishSelection(Standing, BaseOnly);
	EvaluateFrames(Comp, /*Frames=*/1, FrameSeconds, Pose);
	Test.TestTrue(TEXT("the body poses a base clip before anything is layered over it"),
		Inst->RequestedSlotSequence == nullptr && Inst->SlotLayerWeight == 0.f);

	// The claim a weapon will arm the layer with: one shot, on the UpperBody channel, carrying the
	// forced layer activity that names its envelope family.
	FElysiumClipSegment Shot;
	Shot.ClipName = Pick.Label;
	Shot.Source = EElysiumAnimSource::Player;
	Shot.Priority = EElysiumAnimPriority::Scripted;
	Shot.Channel = EElysiumAnimChannel::UpperBody;
	Shot.Activity = TEXT("ACT_RANGE_ATTACK1_LAYER");
	const FElysiumAnimationRequest Claim = ElysiumAnimIntent::ClaimForSegment(Shot, LayerSeconds);
	// The (bank, label) pair the layer's event timeline is addressed by — the same pair
	// `UElysiumEntityBodies::PlayNpcClip` builds for the base channel, because the ranged families
	// carry the 3030-3044 commit ids on the clips that compose HERE.
	const FElysiumClipIdentity LayerIdentity(Pick.Owner, Pick.Label);

	// --- the three refusals, each said out loud ---------------------------------------------------
	Test.AddExpectedError(TEXT("the overlay slot was armed for"),
		EAutomationExpectedErrorFlags::Contains, 1);
	Test.TestFalse(TEXT("the slot refuses a claim with no sequence behind it"),
		Inst->PlaySlotLayer(LayerIdentity, nullptr, Pick.MaskName, Claim));

	Test.AddExpectedError(TEXT("it carries no baked bone mask"),
		EAutomationExpectedErrorFlags::Contains, 1);
	Test.TestFalse(TEXT("...and a layer that carries no baked bone mask, which would own the whole rig"),
		Inst->PlaySlotLayer(LayerIdentity, Pick.Layer, NAME_None, Claim));

	// The refusal that reads as working. A claim with no length has no phase, so the evaluator would
	// stand on one frame of the clip at a fixed weight for as long as the producer held the channel.
	FElysiumAnimationRequest Lengthless = Claim;
	Lengthless.HoldSeconds = 0.f;
	Lengthless.ClipLengthSeconds = 0.f;
	Test.AddExpectedError(TEXT("its claim carries no clip length"),
		EAutomationExpectedErrorFlags::Contains, 1);
	Test.TestFalse(TEXT("...and a claim carrying no clip length, which would freeze on one frame"),
		Inst->PlaySlotLayer(LayerIdentity, Pick.Layer, Pick.MaskName, Lengthless));
	Test.TestTrue(TEXT("none of the three left anything staged on the slot"),
		Inst->RequestedSlotSequence == nullptr);

	// --- the arm frame reaches the pins -----------------------------------------------------------
	Test.TestTrue(TEXT("a masked layer with a length behind it is taken"),
		Inst->PlaySlotLayer(LayerIdentity, Pick.Layer, Pick.MaskName, Claim));
	EvaluateFrames(Comp, /*Frames=*/1, FrameSeconds, Pose);
	Test.TestTrue(TEXT("the layer reaches the graph's own slot pin"),
		Inst->RequestedSlotSequence == Pick.Layer);
	Test.TestEqual(TEXT("...carrying the mask its own metadata names"), Inst->RequestedSlotMaskName,
		Pick.MaskName);
	// Age zero on the attack family is the ceiling — the snap. A reload would be at the foot of its
	// ramp, which is the whole reason the seam reads the envelope rather than writing 1.0.
	Test.TestEqual(TEXT("...at the envelope's own answer for the frame it was armed on"),
		Inst->SlotLayerWeight, ElysiumAnimIntent::SlotWeightMax);
	Test.TestEqual(TEXT("...with the evaluator seated at the clip's head"), Inst->SlotExplicitTime, 0.f);
	// And the mask really was written on the node, which is the one thing no pin can show: the
	// layered blend's mask is edit-time state, so it is set through `SetBlendMask` rather than copied.
	Test.TestTrue(TEXT("...and the slot's blend node was given a profile, not left unmasked"),
		SlotBlend->BlendMasks.IsValidIndex(0) && SlotBlend->BlendMasks[0] != nullptr);

	// --- and the channel PUBLISHES A PHASE, at the instant the arm was accepted --------------------
	//
	// This is what makes a ranged commit land on its own clip. The shot clips carry the 3030-3044
	// commit ids and they compose HERE; `FElysiumWeapon` resolves a clip and asks
	// `CommitArrivesFromAnimEvent` about it in the same statement pair, so a phase that only appeared
	// on the next update would answer for the previous play — and every player shot would silently
	// fall back to the `ContactEventCycle` estimate with nothing but a Verbose line to say so.
	FElysiumClipPhase LayerPhase;
	Test.TestTrue(TEXT("the overlay slot answers the phase seam for its own channel"),
		Inst->GetClipPhase(EElysiumAnimChannel::UpperBody, LayerPhase));
	Test.TestEqual(TEXT("...naming the label the claim armed"), LayerPhase.Label, Pick.Label);
	Test.TestEqual(TEXT("...over the bank the include DAG resolved it out of"),
		LayerPhase.OwnerStem, Pick.Owner);
	Test.TestEqual(TEXT("...on the channel it composes on"), LayerPhase.Channel,
		EElysiumAnimChannel::UpperBody);
	Test.TestEqual(TEXT("...at the head of its clip, which is where a fresh claim starts"),
		LayerPhase.Cycle, 0.f);
	Test.TestEqual(TEXT("...carrying the sequence's own authored length"), LayerPhase.Length,
		LayerSeconds, UE_KINDA_SMALL_NUMBER);
	// The rate the claim is riding the clip at, read back out of the one duration the envelope, the
	// expiry and this phase all share: `authored / SlotPhaseLength`. It is what a consumer sampling a
	// window forward from the cycle multiplies by, so a claim whose hold disagreed with its clip
	// would hand every such reader a window measured in the wrong seconds. This claim states the
	// authored 1.0, so the two lengths are the same number and the rate is exactly 1.
	Test.TestEqual(TEXT("...at the rate the claim rides it, which one authored playback rate makes 1"),
		LayerPhase.PlayRate, 1.0f, UE_KINDA_SMALL_NUMBER);
	Test.TestTrue(TEXT("...and a play id, without which a repeated shot is a lap rather than a new play"),
		LayerPhase.PlayId != 0);
	// The two records are separate, which is the whole reason the slot is a second published phase
	// rather than a fifth base arm: the layer and whatever owns the base are standing on their own
	// clips at the same time, and asking for one must never hand back the other.
	//
	// The base is STOOD ON a clip of its own for the comparison. Asking an empty channel would put a
	// default-constructed record on both sides of the test, which passes however the two records are
	// wired together — including the way that matters, one record answering for both channels.
	const FElysiumClipIdentity BaseIdentity(Pick.Owner, TEXT("elysium_base_under_the_layer"));
	Inst->PlayClip(BaseIdentity, Pick.BaseClip, /*bLoop=*/true, /*bRestart=*/true, /*PlayRate=*/1.f);
	FElysiumClipPhase BaseAsked;
	Test.TestTrue(TEXT("the base channel answers for the clip IT is standing on"),
		Inst->GetClipPhase(EElysiumAnimChannel::Base, BaseAsked));
	Test.TestEqual(TEXT("...naming that clip"), BaseAsked.Label, BaseIdentity.Label);
	Test.TestNotEqual(TEXT("...and never the layer's, which is a second record beside it"),
		BaseAsked.Label, Pick.Label);
	Test.TestTrue(TEXT("...while the overlay slot goes on answering for its own"),
		Inst->GetClipPhase(EElysiumAnimChannel::UpperBody, LayerPhase));
	Test.TestEqual(TEXT("...still the layer's label"), LayerPhase.Label, Pick.Label);
	// And the base's own play ends without touching the layer, which is the same independence read
	// from the other side. The cinematic clip is taken back down before the next evaluation, because
	// a standing one replaces the graph's output outright.
	Inst->StopClip();
	Test.TestFalse(TEXT("a base play that ends stops answering for the base"),
		Inst->GetClipPhase(EElysiumAnimChannel::Base, BaseAsked));
	Test.TestTrue(TEXT("...and takes nothing from the layer"),
		Inst->GetClipPhase(EElysiumAnimChannel::UpperBody, LayerPhase));

	// --- the driver's republish walks it, and the record is what the pins follow -------------------
	FElysiumAnimationSelection Layered = Standing;
	Layered.SlotLabel = Pick.Label;
	Layered.SlotOwnerStem = Pick.Owner;
	Layered.SlotWeight = 0.5f;
	Layered.SlotCycle = 0.5f;
	FElysiumResolvedAnimation Composed = BaseOnly;
	Composed.SlotSequence = Pick.Layer;
	Composed.SlotMaskName = Pick.MaskName;
	Inst->PublishSelection(Layered, Composed);
	EvaluateFrames(Comp, /*Frames=*/1, FrameSeconds, Pose);
	Test.TestEqual(TEXT("a published record drives the slot's weight"), Inst->SlotLayerWeight, 0.5f);
	Test.TestEqual(TEXT("...and pins the evaluator at that phase of the layer's own seconds"),
		Inst->SlotExplicitTime, 0.5f * LayerSeconds, UE_KINDA_SMALL_NUMBER);
	Test.TestTrue(TEXT("...over the same layer, which is not re-seated at its head mid-motion"),
		Inst->RequestedSlotSequence == Pick.Layer);
	// The phase walks with the pose, off the same one number. A timeline advanced against any other
	// clock would dispatch a shot's commit id at an instant the layer never reaches.
	Test.TestTrue(TEXT("the published phase moves with the record"),
		Inst->GetClipPhase(EElysiumAnimChannel::UpperBody, LayerPhase));
	Test.TestEqual(TEXT("...to the record's own cycle"), LayerPhase.Cycle, 0.5f);
	Test.TestEqual(TEXT("...anchored where the dispatcher was last left, so the interval between the two "
		"fires each record exactly once"), LayerPhase.AnchorCycle, 0.f);

	// --- and a record that stops naming a layer takes every one of them back down ------------------
	Inst->PublishSelection(Standing, BaseOnly);
	EvaluateFrames(Comp, /*Frames=*/1, FrameSeconds, Pose);
	Test.TestTrue(TEXT("a cleared record takes the layer off the pin"),
		Inst->RequestedSlotSequence == nullptr);
	Test.TestEqual(TEXT("...its weight with it"), Inst->SlotLayerWeight, 0.f);
	Test.TestEqual(TEXT("...and its playhead, rather than leaving the last shot's phase standing"),
		Inst->SlotExplicitTime, 0.f);
	// The timeline goes with it. A phase left standing names a clip nothing composes, and the event
	// pass would keep walking its records against a frozen cycle for the life of the body.
	Test.TestFalse(TEXT("...and the channel stops answering the phase seam"),
		Inst->GetClipPhase(EElysiumAnimChannel::UpperBody, LayerPhase));

	// --- the stop seam takes the pose down NOW, without waiting for another update -----------------
	//
	// Its callers are a run's stop path and the death transaction. A corpse frozen at its final pose
	// never runs another update, so a stop that only cleared the staging would leave the shot it died
	// mid-way through composing at its last weight and phase for the life of the body.
	Test.TestTrue(TEXT("the layer is armed again"),
		Inst->PlaySlotLayer(LayerIdentity, Pick.Layer, Pick.MaskName, Claim));
	EvaluateFrames(Comp, /*Frames=*/1, FrameSeconds, Pose);
	Test.TestTrue(TEXT("...and is standing on the pin"), Inst->RequestedSlotSequence == Pick.Layer);
	Inst->StopSlotLayer();
	Test.TestTrue(TEXT("StopSlotLayer clears the pin without another update"),
		Inst->RequestedSlotSequence == nullptr);
	Test.TestEqual(TEXT("...and the weight the graph evaluates"), Inst->SlotLayerWeight, 0.f);
	Test.TestFalse(TEXT("...and the phase it published, which no later update would come back to clear"),
		Inst->GetClipPhase(EElysiumAnimChannel::UpperBody, LayerPhase));

	// The same call as every release path actually reaches it: a run's stop path, an NPC motor giving
	// back every claim at once and the map actor doing the same for the player all hold a MESH rather
	// than a host, and all three go through this one door. A second spelling of the take-down is a
	// place it can be forgotten, and a body killed mid-fire then freezes holding the overlay's last
	// weight.
	Test.TestTrue(TEXT("the layer is armed a third time"),
		Inst->PlaySlotLayer(LayerIdentity, Pick.Layer, Pick.MaskName, Claim));
	EvaluateFrames(Comp, /*Frames=*/1, FrameSeconds, Pose);
	Test.TestTrue(TEXT("...and is standing on the pin again"),
		Inst->RequestedSlotSequence == Pick.Layer);
	UElysiumBipedAnimInstance::StopSlotLayerOn(Comp);
	Test.TestTrue(TEXT("the body-addressed stop reaches the same pin"),
		Inst->RequestedSlotSequence == nullptr);
	Test.TestEqual(TEXT("...and the same weight"), Inst->SlotLayerWeight, 0.f);
	// A body with no biped graph at all carries no slot to stop, which is an ordinary absence rather
	// than a failure — every green-room stand and preview body is one.
	UElysiumBipedAnimInstance::StopSlotLayerOn(nullptr);

	// --- a mask this body's skeleton does not carry is refused, and said ONCE ----------------------
	//
	// Three frames, one line. The refused name never reaches `SetBlendMask`, so the applied name
	// cannot latch it — and the request does not change frame to frame, so an unguarded warning
	// repeats for every frame the layer stands.
	Test.AddExpectedError(TEXT("is absent from this body's skeleton or is not a blend mask"),
		EAutomationExpectedErrorFlags::Contains, 1);
	FElysiumResolvedAnimation BadMask = Composed;
	BadMask.SlotMaskName = FName(TEXT("elysium_no_such_blend_mask"));
	Inst->PublishSelection(Layered, BadMask);
	EvaluateFrames(Comp, /*Frames=*/3, FrameSeconds, Pose);

	return true;
}

// One complex test replacing the seven near-identical `IMPLEMENT_SIMPLE_AUTOMATION_TEST` graph
// cases above, each now a row here. The win is not a cross-row cache -- see the note above
// `EnsureCoreSetup` for why that was tried and reverted -- it is that the seven rows' identical
// setup boilerplate (the npc-domain check, the graph `LoadClass`, `FindLoopingClip`, standing an
// actor/component/anim instance, "body owner spawned") is stated once, in `EnsureCoreSetup`/
// `EnsureLoopingPick`/`BeginGraphBodyCase`, and every row -- run alone under a narrow filter or
// as part of the whole tier -- still runs its own `FTestWorldWrapper` from first principles.
// `ReactionGridLength` and `ReactionCellMasks` are untouched: neither builds a world or an anim
// instance, so neither shares any of this.
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FElysiumGraphCasesTest,
	"Elysium.Content.Graph", GElysiumMontageSlotFlags)

void FElysiumGraphCasesTest::GetTests(TArray<FString>& OutBeautifiedNames,
	TArray<FString>& OutTestCommands) const
{
	static const TCHAR* const Cases[] = {
		TEXT("MontageSlot"),
		TEXT("OneShotArbitration"),
		TEXT("ReactionBranch"),
		TEXT("BlendStack"),
		TEXT("ReactionDrive"),
		TEXT("FirstAssetBlend"),
		TEXT("SlotLayer"),
	};
	for (const TCHAR* Case : Cases)
	{
		OutBeautifiedNames.Add(Case);
		OutTestCommands.Add(Case);
	}
}

bool FElysiumGraphCasesTest::RunTest(const FString& Parameters)
{
	// ReactionDrive and SlotLayer pick their own content and stand their own world entirely inside
	// their own function, so they are dispatched directly.
	if (Parameters == TEXT("ReactionDrive"))
	{
		return RunGraphReactionDriveCase(*this);
	}
	if (Parameters == TEXT("SlotLayer"))
	{
		return RunGraphSlotLayerCase(*this);
	}

	// The five ordinary-looping-clip cases share one shape: this row's own world, this row's own
	// graph load, this row's own pick, this row's own body -- nothing here outlives this call.
	FTestWorldWrapper TestWorld;
	UClass* Graph = nullptr;
	FString CoreMessage;
	switch (EnsureCoreSetup(*this, TestWorld, Graph, CoreMessage))
	{
	case ESharedSetupResult::Abstain:
		AddInfo(CoreMessage);
		return true;
	case ESharedSetupResult::Failed:
		return false;
	default:
		break;
	}

	FGraphBodyCase Case;
	switch (BeginGraphBodyCase(*this, TestWorld, Graph, Case))
	{
	case ESharedSetupResult::Abstain: return true;
	case ESharedSetupResult::Failed: return false;
	default: break;
	}

	if (Parameters == TEXT("MontageSlot"))
	{
		return RunGraphMontageSlotCase(*this, Case.Comp, Case.Inst, Case.Pick);
	}
	if (Parameters == TEXT("OneShotArbitration"))
	{
		return RunGraphOneShotArbitrationCase(*this, Case.Comp, Case.Inst, Case.Pick);
	}
	if (Parameters == TEXT("ReactionBranch"))
	{
		return RunGraphReactionBranchCase(*this, Case.Comp, Case.Inst, Case.Pick);
	}
	if (Parameters == TEXT("BlendStack"))
	{
		return RunGraphBlendStackCase(*this, Case.Comp, Case.Inst, Case.Pick);
	}
	if (Parameters == TEXT("FirstAssetBlend"))
	{
		return RunGraphFirstAssetBlendCase(*this, Case.Comp, Case.Inst, Case.Pick);
	}

	AddError(FString::Printf(TEXT("unknown Elysium.Content.Graph case '%s'"), *Parameters));
	return false;
}

#endif // WITH_DEV_AUTOMATION_TESTS
