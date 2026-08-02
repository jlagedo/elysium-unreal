#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumContentPaths.h"
#include "Visual/ElysiumAnimNodes.h"
#include "Visual/ElysiumCompositionRig.h"
#include "Visual/ElysiumNpcClips.h"
#include "Visual/ElysiumNpcVisual.h"

#include "Animation/AnimInstanceProxy.h"
#include "Animation/Skeleton.h"
#include "BonePose.h"
#include "Misc/MemStack.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "ReferenceSkeleton.h"
#include "Visual/ElysiumNpcAnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Tests/AutomationCommon.h"

// CAP7.2 — the two composition stages as skeletal controls.
//
// The acceptance that matters is numerical and decoder-independent: the nodes are fed known local
// transforms and the composed result is checked, so nothing here depends on whether the shipped
// clip decoder is right. Both rules are transcribed a *second* time in this file, straight from
// the pseudocode in `docs/vtmb/animation_and_movers.md` A.4a and `docs/vtmb/procedural_bones.md`
// — including a hand-rolled slerp, so the check does not run through the same FQuat::Slerp the
// node does. An agreement is therefore between two formulations, not a function against itself.
//
// The fixture is synthetic for the reason every fixture in this repo is: nothing game-sourced is
// committed, and a capture's own bone transforms are derived from the user's install. What the
// real corpus is used for instead is the Content tier below, which reads the exported tables and
// checks them against the skeleton the same .glb produced.

static constexpr EAutomationTestFlags GElysiumCompositionTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// --- the independent transcriptions --------------------------------------------------------

	// Shortest-arc quaternion interpolation, by hand.
	FQuat SlerpByHand(const FQuat& A, FQuat B, double Alpha)
	{
		double Dot = A.X * B.X + A.Y * B.Y + A.Z * B.Z + A.W * B.W;
		if (Dot < 0.0)
		{
			B = FQuat(-B.X, -B.Y, -B.Z, -B.W);
			Dot = -Dot;
		}
		Dot = FMath::Clamp(Dot, -1.0, 1.0);

		FQuat Out;
		if (Dot > 0.9995)
		{
			Out = FQuat(
				(1.0 - Alpha) * A.X + Alpha * B.X, (1.0 - Alpha) * A.Y + Alpha * B.Y,
				(1.0 - Alpha) * A.Z + Alpha * B.Z, (1.0 - Alpha) * A.W + Alpha * B.W);
		}
		else
		{
			const double Theta = FMath::Acos(Dot);
			const double Sine = FMath::Sin(Theta);
			const double SA = FMath::Sin((1.0 - Alpha) * Theta) / Sine;
			const double SB = FMath::Sin(Alpha * Theta) / Sine;
			Out = FQuat(SA * A.X + SB * B.X, SA * A.Y + SB * B.Y, SA * A.Z + SB * B.Z,
				SA * A.W + SB * B.W);
		}
		Out.Normalize();
		return Out;
	}

	// `docs/vtmb/procedural_bones.md`'s rule, transcribed independently of FElysiumCompositionRig.
	FTransform AxisInterpByHand(const FElysiumCompositionRig& Rig, const FElysiumAxisInterpRule& Rule,
		const FQuat& ControlLocal)
	{
		const FVector W = ControlLocal.RotateVector(Rule.Axis);
		const double D0 = Rig.DriverAxes[0] | W;
		const double D1 = Rig.DriverAxes[1] | W;
		const double D2 = Rig.DriverAxes[2] | W;
		const int32 I1 = D0 >= 0.0 ? 0 : 1;
		const int32 I2 = D1 >= 0.0 ? 2 : 3;
		const int32 I3 = D2 >= 0.0 ? 4 : 5;
		const double A1 = FMath::Abs(D0);
		const double A2 = FMath::Abs(D1);
		const double A3 = FMath::Abs(D2);

		if (A1 + A2 > 0.0)
		{
			const double T = 1.0 / (A1 + A2 + A3);
			const FQuat Inner = SlerpByHand(Rule.Quat[I2], Rule.Quat[I1], A1 / (A1 + A2));
			const FQuat Q = SlerpByHand(Inner, Rule.Quat[I3], A3 * T);
			const FVector P = A1 * T * Rule.Pos[I1] + A2 * T * Rule.Pos[I2] + A3 * T * Rule.Pos[I3];
			return FTransform(Q, P);
		}
		return FTransform(Rule.Quat[I3], Rule.Pos[I3]);
	}

	// --- the synthetic skeleton ----------------------------------------------------------------

	struct FTestBone
	{
		FName Name;
		int32 Parent = INDEX_NONE;
		FTransform Bind = FTransform::Identity;
	};

	// A six-bone arm: root -> spine (the split bone) -> upperarm -> forearm -> hand, plus a
	// `bicep` helper driven by `upperarm`. That is the shipped shape — every model's split bone is
	// `Bip01 Spine1` and every driven bone hangs below it, which is what makes the stage order
	// observable at all.
	TArray<FTestBone> ArmSkeleton()
	{
		TArray<FTestBone> Bones;
		Bones.Add({ TEXT("root"), INDEX_NONE, FTransform::Identity });
		Bones.Add({ TEXT("spine"), 0, FTransform(FQuat::Identity, FVector(0.f, 0.f, 30.f)) });
		Bones.Add({ TEXT("upperarm"), 1, FTransform(FQuat::Identity, FVector(0.f, 12.f, 8.f)) });
		Bones.Add({ TEXT("bicep"), 1, FTransform(FQuat::Identity, FVector(0.f, 6.f, 8.f)) });
		Bones.Add({ TEXT("forearm"), 2, FTransform(FQuat::Identity, FVector(0.f, 0.f, -25.f)) });
		Bones.Add({ TEXT("hand"), 4, FTransform(FQuat::Identity, FVector(0.f, 0.f, -22.f)) });
		return Bones;
	}

	USkeleton* BuildSkeleton(const TArray<FTestBone>& Bones)
	{
		USkeleton* Skeleton = NewObject<USkeleton>();
		{
			// The modifier writes the skeleton's own reference skeleton and rebuilds it on scope
			// exit. The bone tree stays empty, which leaves every bone on the default `Animation`
			// retargeting mode — exactly what a fixture that supplies its own locals wants.
			FReferenceSkeletonModifier Modifier(Skeleton);
			for (const FTestBone& Bone : Bones)
			{
				Modifier.Add(FMeshBoneInfo(Bone.Name, Bone.Name.ToString(), Bone.Parent), Bone.Bind);
			}
		}
		return Skeleton;
	}

	// Component space by the plain hierarchy, so the fixture's expectations start from the same
	// place the engine's own composition does.
	TArray<FTransform> ComposeOrdinary(const TArray<FTestBone>& Bones, const TArray<FTransform>& Locals)
	{
		TArray<FTransform> Component;
		Component.SetNum(Bones.Num());
		for (int32 i = 0; i < Bones.Num(); ++i)
		{
			Component[i] = Bones[i].Parent == INDEX_NONE
				? Locals[i]
				: Locals[i] * Component[Bones[i].Parent];
		}
		return Component;
	}

	bool NearlyEqual(const FTransform& A, const FTransform& B, double Tolerance)
	{
		return A.GetTranslation().Equals(B.GetTranslation(), Tolerance)
			&& A.GetRotation().Equals(B.GetRotation(), Tolerance);
	}
}

// ---------------------------------------------------------------------------------------------
// The rule body alone: no pose, no skeleton, no engine.
// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumAxisInterpRuleTest, "Elysium.Substrate.AxisInterpRule",
	GElysiumCompositionTestFlags)

bool FElysiumAxisInterpRuleTest::RunTest(const FString&)
{
	FElysiumCompositionRig Rig;
	Rig.DriverAxes[0] = FVector(1.f, 0.f, 0.f);
	Rig.DriverAxes[1] = FVector(0.f, 1.f, 0.f);
	Rig.DriverAxes[2] = FVector(0.f, 0.f, 1.f);

	FElysiumAxisInterpRule Rule;
	Rule.Bone = TEXT("bicep");
	Rule.Control = TEXT("upperarm");
	Rule.Axis = FVector(0.f, 0.f, 1.f);
	// Six genuinely distinct rotations and six distinct positions, so nothing degenerate hides a
	// wrong entry selection or a swapped slerp argument.
	const double Angles[6] = { 10.0, -25.0, 40.0, -15.0, 65.0, -50.0 };
	for (int32 i = 0; i < 6; ++i)
	{
		Rule.Quat[i] = FQuat(FVector(1.f, 0.f, 0.f).GetSafeNormal(), FMath::DegreesToRadians(Angles[i]));
		Rule.Pos[i] = FVector(i * 0.5f, 2.f - i, 7.f + i * 0.25f);
	}

	// Control rotations spread over all eight sign octants of the driver, so every one of the six
	// entries is selected by at least one case and both branches of the rule run.
	const FQuat Controls[] = {
		FQuat::Identity,
		FQuat(FVector(1, 0, 0), FMath::DegreesToRadians(35.0)),
		FQuat(FVector(0, 1, 0), FMath::DegreesToRadians(-70.0)),
		FQuat(FVector(1, 1, 0).GetSafeNormal(), FMath::DegreesToRadians(120.0)),
		FQuat(FVector(-1, 2, 0.5).GetSafeNormal(), FMath::DegreesToRadians(200.0)),
		FQuat(FVector(0, 1, 0), FMath::DegreesToRadians(90.0)),    // drives the a1 + a2 == 0 branch
		FQuat(FVector(1, 0, 0), FMath::DegreesToRadians(-90.0)),
	};

	int32 Degenerate = 0;
	for (int32 Case = 0; Case < UE_ARRAY_COUNT(Controls); ++Case)
	{
		const FTransform Produced = Rig.EvaluateRule(Rule, Controls[Case]);
		const FTransform Expected = AxisInterpByHand(Rig, Rule, Controls[Case]);
		TestTrue(FString::Printf(TEXT("case %d matches the independent transcription"), Case),
			NearlyEqual(Produced, Expected, 1e-5));

		const FVector W = Controls[Case].RotateVector(Rule.Axis);
		if (FMath::Abs(Rig.DriverAxes[0] | W) + FMath::Abs(Rig.DriverAxes[1] | W) <= 0.0)
		{
			++Degenerate;
		}
	}
	TestTrue(TEXT("at least one case takes the a1 + a2 == 0 branch"), Degenerate > 0);

	// The measured property of the shipped corpus: `pos[6]` is six copies of one position on the
	// median rule, and a constant table has to come back unchanged, because a1t + a2t + a3t == 1.
	FElysiumAxisInterpRule Constant = Rule;
	const FVector Bind(1.5f, -3.25f, 11.f);
	for (int32 i = 0; i < 6; ++i)
	{
		Constant.Pos[i] = Bind;
	}
	for (int32 Case = 0; Case < UE_ARRAY_COUNT(Controls); ++Case)
	{
		TestTrue(FString::Printf(TEXT("a constant pos table is returned unchanged (case %d)"), Case),
			Rig.EvaluateRule(Constant, Controls[Case]).GetTranslation().Equals(Bind, 1e-4));
	}

	// The result replaces the driven bone's local; it is not a function of it. Two different
	// animated locals for the same control rotation must produce the same answer — that is the
	// property the whole "clips carry channels something downstream overrides" arrangement rests
	// on, and it is true here by construction: the rule takes no driven-bone input at all.
	TestTrue(TEXT("the rule is a pure function of the control rotation"),
		NearlyEqual(Rig.EvaluateRule(Rule, Controls[1]), Rig.EvaluateRule(Rule, Controls[1]), 0.0));
	return true;
}

// ---------------------------------------------------------------------------------------------
// Composition: known locals in, composed component space out, checked against an independent
// transcription of both rules. This is the acceptance that does not depend on the clip decoder.
// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCompositionStagesTest, "Elysium.Substrate.CompositionStages",
	GElysiumCompositionTestFlags)

bool FElysiumCompositionStagesTest::RunTest(const FString&)
{
	const TArray<FTestBone> Bones = ArmSkeleton();
	USkeleton* Skeleton = BuildSkeleton(Bones);
	if (!TestNotNull(TEXT("the synthetic skeleton builds"), Skeleton))
	{
		return false;
	}

	TArray<FBoneIndexType> RequiredBones;
	for (int32 i = 0; i < Bones.Num(); ++i)
	{
		RequiredBones.Add(static_cast<FBoneIndexType>(i));
	}
	FBoneContainer Container;
	Container.InitializeTo(RequiredBones, UE::Anim::FCurveFilterSettings(), *Skeleton);
	if (!TestTrue(TEXT("the bone container initializes"), Container.IsValid()))
	{
		return false;
	}

	// Known locals — deliberately not the bind pose, and with rotation on every ancestor of the
	// driven bone, so both a wrong parent frame and a missing split branch would show.
	TArray<FTransform> Locals;
	Locals.Reserve(Bones.Num());
	for (const FTestBone& Bone : Bones)
	{
		Locals.Add(Bone.Bind);
	}
	Locals[0].SetRotation(FQuat(FVector(0, 0, 1), FMath::DegreesToRadians(20.0)));
	Locals[1].SetRotation(FQuat(FVector(1, 0, 0), FMath::DegreesToRadians(-35.0)));
	Locals[2].SetRotation(FQuat(FVector(0, 1, 0), FMath::DegreesToRadians(55.0)));
	Locals[3].SetRotation(FQuat(FVector(1, 1, 0).GetSafeNormal(), FMath::DegreesToRadians(15.0)));
	Locals[4].SetRotation(FQuat(FVector(1, 0, 0), FMath::DegreesToRadians(70.0)));
	Locals[5].SetRotation(FQuat(FVector(0, 0, 1), FMath::DegreesToRadians(-40.0)));

	TSharedRef<FElysiumCompositionRig> Rig = MakeShared<FElysiumCompositionRig>();
	Rig->Stem = TEXT("fixture");
	Rig->SplitBones.Add(TEXT("spine"));
	{
		FElysiumAxisInterpRule Rule;
		Rule.Bone = TEXT("bicep");
		Rule.Control = TEXT("upperarm");
		Rule.Axis = FVector(0.f, 0.f, 1.f);
		for (int32 i = 0; i < 6; ++i)
		{
			Rule.Quat[i] = FQuat(FVector(0, 1, 0), FMath::DegreesToRadians(12.0 * (i + 1) - 40.0));
			Rule.Pos[i] = FVector(0.f, 6.f, 8.f);   // the bind offset, as 79% of shipped rules carry
		}
		Rig->AxisRules.Add(Rule);
	}

	// Run the pose through the two nodes, in the order the design fixes.
	auto Run = [&](bool bSplitFirst)
	{
		// FCompactPose allocates off the animation mem stack, which asserts without a mark in
		// scope. The anim graph pushes one per evaluation; a fixture driving the nodes directly
		// has to push its own.
		FMemMark Mark(FMemStack::Get());
		FCompactPose Pose;
		Pose.SetBoneContainer(&Container);
		for (int32 i = 0; i < Bones.Num(); ++i)
		{
			Pose[FCompactPoseBoneIndex(i)] = Locals[i];
		}

		FAnimNode_ElysiumSplitInheritance Split;
		FAnimNode_ElysiumAxisInterp AxisInterp;
		Split.SetRig(Rig);
		AxisInterp.SetRig(Rig);
		Split.ResolveBones(Container);
		AxisInterp.ResolveBones(Container);

		FComponentSpacePoseContext Composed(nullptr);
		Composed.Pose.InitPose(MoveTemp(Pose));
		if (bSplitFirst)
		{
			Split.Apply(Composed);
			AxisInterp.Apply(Composed);
		}
		else
		{
			AxisInterp.Apply(Composed);
			Split.Apply(Composed);
		}

		TArray<FTransform> Result;
		Result.Reserve(Bones.Num());
		for (int32 i = 0; i < Bones.Num(); ++i)
		{
			Result.Add(Composed.Pose.GetComponentSpaceTransform(FCompactPoseBoneIndex(i)));
		}
		return Result;
	};

	// The expectation, transcribed straight from the two documents.
	TArray<FTransform> Expected = ComposeOrdinary(Bones, Locals);
	{
		// A.4a: a `Flags & 0x2` bone takes its rotation from the component root and its translation
		// from its parent. Then everything below it recomposes against the new frame.
		const int32 Spine = 1;
		Expected[Spine] = FTransform(Locals[Spine].GetRotation(),
			Expected[Bones[Spine].Parent].TransformPosition(Locals[Spine].GetTranslation()));
		for (int32 i = Spine + 1; i < Bones.Num(); ++i)
		{
			Expected[i] = Locals[i] * Expected[Bones[i].Parent];
		}

		// procedural_bones.md: the driven bone's local is replaced outright by the rule's output,
		// read off the control bone's local rotation in the pose the previous stage produced.
		const int32 Bicep = 3;
		const int32 Control = 2;
		const FQuat ControlLocal =
			Expected[Bones[Control].Parent].GetRotation().Inverse() * Expected[Control].GetRotation();
		Expected[Bicep] = AxisInterpByHand(*Rig, Rig->AxisRules[0], ControlLocal)
			* Expected[Bones[Bicep].Parent];
	}

	const TArray<FTransform> Produced = Run(/*bSplitFirst=*/true);
	for (int32 i = 0; i < Bones.Num(); ++i)
	{
		TestTrue(FString::Printf(TEXT("bone '%s' composes as the two rules say"),
			*Bones[i].Name.ToString()), NearlyEqual(Produced[i], Expected[i], 1e-4));
	}

	// The split bone really did decline its parent's rotation: its composed rotation is its own
	// local, not parent * local, and the two differ on this fixture.
	TestTrue(TEXT("the split bone's composed rotation is its own local rotation"),
		Produced[1].GetRotation().Equals(Locals[1].GetRotation(), 1e-4));
	TestFalse(TEXT("and that is not what the ordinary hierarchy would have produced"),
		Produced[1].GetRotation().Equals(
			(Locals[1] * Locals[0]).GetRotation(), 1e-4));
	// while its translation is exactly the ordinary one.
	TestTrue(TEXT("the split bone keeps the ordinary translation"),
		Produced[1].GetTranslation().Equals(
			ComposeOrdinary(Bones, Locals)[1].GetTranslation(), 1e-4));

	// Order. The design fixes split first, and that is what runs — but what the swap actually does
	// is worth stating rather than asserting, because it is measurable here.
	//
	// On the shipped rig shape the two stages COMMUTE. The axis rule is a pure function of the
	// control bone's LOCAL rotation, which no change to an ancestor can alter, and it returns a
	// local transform; split inheritance only rewrites an ancestor's component-space rotation and
	// leaves every descendant's local intact. So neither stage can see the other's output — unless
	// they share a bone, and over the whole export they never do: 0 of 1,535 rules name a split bone
	// as their control or as their driven bone (Elysium.Content.CompositionRigCorpus checks the
	// driven half of that on live data).
	const TArray<FTransform> Swapped = Run(/*bSplitFirst=*/false);
	TestTrue(TEXT("on a disjoint rig — the shipped shape — the two stages commute"),
		NearlyEqual(Swapped[3], Produced[3], 1e-4));

	// Where the order becomes load-bearing is the one arrangement the corpus does not contain: a
	// rule whose CONTROL is the split bone. Then the control's local rotation is exactly what split
	// inheritance rewrites, the rule reads a different input depending on which ran first, and the
	// driven bone lands somewhere else. Keeping retail's order is what makes that case right for
	// free rather than a latent divergence.
	{
		TSharedRef<FElysiumCompositionRig> Shared = MakeShared<FElysiumCompositionRig>();
		Shared->SplitBones = Rig->SplitBones;
		Shared->AxisRules = Rig->AxisRules;
		Shared->AxisRules[0].Control = TEXT("spine");
		for (int32 i = 0; i < 6; ++i)
		{
			// Distinct entries, so a different driver actually selects a different correction.
			Shared->AxisRules[0].Quat[i] =
				FQuat(FVector(0, 1, 0), FMath::DegreesToRadians(30.0 * i - 75.0));
		}

		auto RunShared = [&](bool bSplitFirst)
		{
			FMemMark Mark(FMemStack::Get());
			FCompactPose Pose;
			Pose.SetBoneContainer(&Container);
			for (int32 i = 0; i < Bones.Num(); ++i)
			{
				Pose[FCompactPoseBoneIndex(i)] = Locals[i];
			}
			FAnimNode_ElysiumSplitInheritance Split;
			FAnimNode_ElysiumAxisInterp AxisInterp;
			Split.SetRig(Shared);
			AxisInterp.SetRig(Shared);
			Split.ResolveBones(Container);
			AxisInterp.ResolveBones(Container);

			FComponentSpacePoseContext Composed(nullptr);
			Composed.Pose.InitPose(MoveTemp(Pose));
			if (bSplitFirst) { Split.Apply(Composed); AxisInterp.Apply(Composed); }
			else             { AxisInterp.Apply(Composed); Split.Apply(Composed); }
			return Composed.Pose.GetComponentSpaceTransform(FCompactPoseBoneIndex(3));
		};
		TestFalse(TEXT("when a rule's control IS the split bone, the order changes the skeleton"),
			NearlyEqual(RunShared(true), RunShared(false), 1e-4));
	}

	return true;
}

// ---------------------------------------------------------------------------------------------
// A model carrying one stage, the other, or neither.
// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCompositionStagesIndependentTest,
	"Elysium.Substrate.CompositionStagesIndependent", GElysiumCompositionTestFlags)

bool FElysiumCompositionStagesIndependentTest::RunTest(const FString&)
{
	const TArray<FTestBone> Bones = ArmSkeleton();
	USkeleton* Skeleton = BuildSkeleton(Bones);
	TArray<FBoneIndexType> RequiredBones;
	for (int32 i = 0; i < Bones.Num(); ++i)
	{
		RequiredBones.Add(static_cast<FBoneIndexType>(i));
	}
	FBoneContainer Container;
	Container.InitializeTo(RequiredBones, UE::Anim::FCurveFilterSettings(), *Skeleton);

	TArray<FTransform> Locals;
	for (const FTestBone& Bone : Bones)
	{
		Locals.Add(Bone.Bind);
	}
	// The root must carry a rotation of its own, or split inheritance has nothing to decline and
	// the stage is invisible: with an unrotated root, "rooted in the component" and "rooted in the
	// parent" are the same answer.
	Locals[0].SetRotation(FQuat(FVector(0, 0, 1), FMath::DegreesToRadians(20.0)));
	Locals[1].SetRotation(FQuat(FVector(1, 0, 0), FMath::DegreesToRadians(-35.0)));
	Locals[2].SetRotation(FQuat(FVector(0, 1, 0), FMath::DegreesToRadians(55.0)));
	const TArray<FTransform> Ordinary = ComposeOrdinary(Bones, Locals);

	auto RunWith = [&](TSharedPtr<const FElysiumCompositionRig> Rig)
	{
		// FCompactPose allocates off the animation mem stack, which asserts without a mark in
		// scope. The anim graph pushes one per evaluation; a fixture driving the nodes directly
		// has to push its own.
		FMemMark Mark(FMemStack::Get());
		FCompactPose Pose;
		Pose.SetBoneContainer(&Container);
		for (int32 i = 0; i < Bones.Num(); ++i)
		{
			Pose[FCompactPoseBoneIndex(i)] = Locals[i];
		}
		FAnimNode_ElysiumSplitInheritance Split;
		FAnimNode_ElysiumAxisInterp AxisInterp;
		Split.SetRig(Rig);
		AxisInterp.SetRig(Rig);
		Split.ResolveBones(Container);
		AxisInterp.ResolveBones(Container);

		FComponentSpacePoseContext Composed(nullptr);
		Composed.Pose.InitPose(MoveTemp(Pose));
		Split.Apply(Composed);
		AxisInterp.Apply(Composed);

		TArray<FTransform> Result;
		for (int32 i = 0; i < Bones.Num(); ++i)
		{
			Result.Add(Composed.Pose.GetComponentSpaceTransform(FCompactPoseBoneIndex(i)));
		}
		return Result;
	};

	// No rig at all — the overwhelmingly common case (animals, crowd bodies, every player body's
	// procedural half, most props). The pose must come out exactly as the plain hierarchy.
	{
		const TArray<FTransform> Result = RunWith(nullptr);
		bool bIdentical = true;
		for (int32 i = 0; i < Bones.Num(); ++i)
		{
			bIdentical &= NearlyEqual(Result[i], Ordinary[i], 1e-6);
		}
		TestTrue(TEXT("a body with no composition rig poses under the ordinary hierarchy"), bIdentical);
	}

	// Split bones and no rules.
	{
		TSharedRef<FElysiumCompositionRig> Rig = MakeShared<FElysiumCompositionRig>();
		Rig->SplitBones.Add(TEXT("spine"));
		TestTrue(TEXT("a split-only rig has work"), Rig->HasWork());
		const TArray<FTransform> Result = RunWith(Rig);
		TestFalse(TEXT("a split-only rig moves the split bone"),
			Result[1].GetRotation().Equals(Ordinary[1].GetRotation(), 1e-6));
		TestTrue(TEXT("a split-only rig leaves the driven bone on the ordinary composition"),
			Result[3].GetRotation().Equals(
				(Locals[3] * Result[1]).GetRotation(), 1e-4));
	}

	// Rules and no split bones.
	{
		TSharedRef<FElysiumCompositionRig> Rig = MakeShared<FElysiumCompositionRig>();
		FElysiumAxisInterpRule Rule;
		Rule.Bone = TEXT("bicep");
		Rule.Control = TEXT("upperarm");
		Rule.Axis = FVector(0.f, 0.f, 1.f);
		for (int32 i = 0; i < 6; ++i)
		{
			Rule.Quat[i] = FQuat(FVector(0, 1, 0), FMath::DegreesToRadians(9.0 * (i + 1)));
			Rule.Pos[i] = FVector(0.f, 6.f, 8.f);
		}
		Rig->AxisRules.Add(Rule);
		TestTrue(TEXT("a rules-only rig has work"), Rig->HasWork());

		const TArray<FTransform> Result = RunWith(Rig);
		TestTrue(TEXT("a rules-only rig leaves the split bone on the ordinary composition"),
			NearlyEqual(Result[1], Ordinary[1], 1e-6));
		TestFalse(TEXT("a rules-only rig moves the driven bone"),
			Result[3].GetRotation().Equals(Ordinary[3].GetRotation(), 1e-6));
		// Nothing but the driven bone moves: the correction is local to it.
		TestTrue(TEXT("the control bone is untouched"), NearlyEqual(Result[2], Ordinary[2], 1e-6));
		TestTrue(TEXT("the hand below the control is untouched"),
			NearlyEqual(Result[5], Ordinary[5], 1e-6));
	}

	// A rule naming bones this skeleton does not have resolves to nothing and poses normally.
	{
		TSharedRef<FElysiumCompositionRig> Rig = MakeShared<FElysiumCompositionRig>();
		FElysiumAxisInterpRule Rule;
		Rule.Bone = TEXT("Bip01 L Bicep");
		Rule.Control = TEXT("Bip01 L UpperArm");
		Rule.Axis = FVector(0.f, 0.f, 1.f);
		Rig->AxisRules.Add(Rule);
		Rig->SplitBones.Add(TEXT("Bip01 Spine1"));

		const TArray<FTransform> Result = RunWith(Rig);
		bool bIdentical = true;
		for (int32 i = 0; i < Bones.Num(); ++i)
		{
			bIdentical &= NearlyEqual(Result[i], Ordinary[i], 1e-6);
		}
		TestTrue(TEXT("a rig whose bones this skeleton lacks changes nothing"), bIdentical);
	}
	return true;
}

// ---------------------------------------------------------------------------------------------
// The real exported corpus. Content tier — self-skips when the export root is not configured.
// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCompositionRigCorpusTest,
	"Elysium.Content.CompositionRigCorpus", GElysiumCompositionTestFlags)

bool FElysiumCompositionRigCorpusTest::RunTest(const FString&)
{
	if (!FElysiumContentPaths::IsConfigured())
	{
		AddInfo(TEXT("no export root configured; skipping"));
		return true;
	}
	FElysiumNpcIndex Index;
	FString Error;
	if (!Index.Load(Error))
	{
		AddInfo(FString::Printf(TEXT("no npc index (%s); skipping"), *Error));
		return true;
	}

	int32 Declared = 0;
	int32 Loaded = 0;
	int32 Rules = 0;
	int32 SplitModels = 0;
	for (const TPair<FString, FElysiumNpcIndexEntry>& Pair : Index.Npcs)
	{
		if (!Pair.Value.SplitRotationBones.IsEmpty())
		{
			++SplitModels;
		}
		if (Pair.Value.Procedural.IsEmpty())
		{
			continue;
		}
		++Declared;

		FElysiumCompositionRig Rig;
		FString RigError;
		if (!Rig.LoadAxisRules(Pair.Value.Procedural, RigError))
		{
			AddError(FString::Printf(TEXT("procedural '%s': %s"), *Pair.Key, *RigError));
			continue;
		}
		++Loaded;
		Rules += Rig.AxisRules.Num();

		if (Rig.AxisRules.Num() != Pair.Value.ProceduralBones)
		{
			AddError(FString::Printf(TEXT("'%s': index says %d driven bones, the table carries %d"),
				*Pair.Key, Pair.Value.ProceduralBones, Rig.AxisRules.Num()));
		}
		// The two stages are disjoint over the whole corpus — 295 of 295 on the measured capture —
		// which is what lets each be confirmed against the other's bones without contamination.
		for (const FElysiumAxisInterpRule& Rule : Rig.AxisRules)
		{
			if (Pair.Value.SplitRotationBones.Contains(Rule.Bone.ToString()))
			{
				AddError(FString::Printf(TEXT("'%s': %s is both split and driven"),
					*Pair.Key, *Rule.Bone.ToString()));
			}
			if (Rule.Bone == Rule.Control)
			{
				AddError(FString::Printf(TEXT("'%s': %s drives itself"), *Pair.Key,
					*Rule.Bone.ToString()));
			}
		}
	}

	if (Declared == 0)
	{
		AddInfo(TEXT("no model in this export declares a procedural rule table; skipping"));
		return true;
	}
	TestEqual(TEXT("every declared rule table parses"), Loaded, Declared);
	AddInfo(FString::Printf(
		TEXT("%d model(s) with a rule table, %d driven bones, %d model(s) with a split bone"),
		Loaded, Rules, SplitModels));
	TestTrue(TEXT("the corpus carries split bones"), SplitModels > 0);
	return true;
}

// ---------------------------------------------------------------------------------------------
// The import transform, checked against the skeleton the same .glb produced.
//
// This is the one thing about the rule table that cannot be checked without real data: the sidecar
// states its entries in the glb's own basis and metres, and the skeleton glTFRuntime builds from
// that same file is conjugated and scaled. `docs/vtmb/procedural_bones.md` measured that `pos[6]`
// holds six copies of one position at the median and that the position IS the driven bone's bind
// position on 79.3% of rules — so after the import, a rule's position must land on that bone's
// reference-skeleton translation. A wrong basis or a missing metres-to-centimetres would miss by
// a limb length or by a factor of a hundred.
// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCompositionImportTest,
	"Elysium.Content.CompositionRigImport", GElysiumCompositionTestFlags)

bool FElysiumCompositionImportTest::RunTest(const FString&)
{
	if (!FElysiumContentPaths::IsConfigured())
	{
		AddInfo(TEXT("no export root configured; skipping"));
		return true;
	}
	FElysiumNpcIndex Index;
	FString Error;
	if (!Index.Load(Error))
	{
		AddInfo(FString::Printf(TEXT("no npc index (%s); skipping"), *Error));
		return true;
	}

	// A handful of ordinary bipeds rather than the whole cast: each one loads a multi-megabyte glb,
	// and the rule table is a shared rig template, so the corpus adds coverage of the *export* (the
	// test above) rather than of the basis.
	const TCHAR* Stems[] = { TEXT("lacroix"), TEXT("skelter"), TEXT("isaac") };
	int32 Checked = 0;
	int32 OnBind = 0;
	int32 Total = 0;

	for (const TCHAR* Stem : Stems)
	{
		const FElysiumNpcIndexEntry* Entry = Index.Npcs.Find(Stem);
		if (Entry == nullptr || Entry->Procedural.IsEmpty()
			|| !FPaths::FileExists(FElysiumContentPaths::NpcGlb(Stem)))
		{
			continue;
		}

		FElysiumCompositionRig Rig;
		FString RigError;
		if (!Rig.LoadAxisRules(Entry->Procedural, RigError))
		{
			AddError(FString::Printf(TEXT("procedural '%s': %s"), Stem, *RigError));
			continue;
		}

		UglTFRuntimeAsset* Asset = nullptr;
		FString MeshError;
		USkeletalMesh* Mesh = ElysiumNpcVisual::LoadMesh(Stem, Asset, MeshError);
		if (Mesh == nullptr)
		{
			AddError(FString::Printf(TEXT("mesh '%s': %s"), Stem, *MeshError));
			continue;
		}
		++Checked;

		// The three Source axes come back as unit directions, which is the cheapest signal that the
		// import applied a basis rather than an arbitrary matrix.
		for (int32 k = 0; k < 3; ++k)
		{
			TestTrue(FString::Printf(TEXT("'%s' driver axis %d is a unit direction"), Stem, k),
				FMath::IsNearlyEqual(Rig.DriverAxes[k].Size(), 1.0, 1e-4));
		}

		const FReferenceSkeleton& Reference = Mesh->GetRefSkeleton();
		const TArray<FTransform>& Bind = Reference.GetRefBonePose();
		for (const FElysiumAxisInterpRule& Rule : Rig.AxisRules)
		{
			const int32 BoneIndex = Reference.FindBoneIndex(Rule.Bone);
			const int32 ControlIndex = Reference.FindBoneIndex(Rule.Control);
			TestTrue(FString::Printf(TEXT("'%s' names a real driven bone (%s)"), Stem,
				*Rule.Bone.ToString()), BoneIndex != INDEX_NONE);
			TestTrue(FString::Printf(TEXT("'%s' names a real control bone (%s)"), Stem,
				*Rule.Control.ToString()), ControlIndex != INDEX_NONE);
			if (BoneIndex == INDEX_NONE || !Bind.IsValidIndex(BoneIndex))
			{
				continue;
			}
			++Total;

			// The axis and every driver axis survive the import as unit directions.
			TestTrue(FString::Printf(TEXT("'%s' rule axis is a unit direction"), Stem),
				FMath::IsNearlyEqual(Rule.Axis.Size(), 1.0, 1e-4));

			// Six copies of one position is the median shape; take entry 0 as the position.
			bool bConstant = true;
			for (int32 i = 1; i < 6; ++i)
			{
				bConstant &= Rule.Pos[i].Equals(Rule.Pos[0], 1e-2);
			}
			if (bConstant && Rule.Pos[0].Equals(Bind[BoneIndex].GetTranslation(), 0.05))
			{
				++OnBind;
			}
		}
	}

	if (Checked == 0)
	{
		AddInfo(TEXT("none of the sampled models is exported here; skipping"));
		return true;
	}
	AddInfo(FString::Printf(TEXT("%d model(s), %d rules, %d on the driven bone's bind position"),
		Checked, Total, OnBind));
	// procedural_bones.md measures 79.3% over the whole corpus. Requiring a clear majority is what
	// separates "the import transform is right" from "the import transform is off by a basis or a
	// factor of a hundred", without pinning the test to a corpus statistic.
	// procedural_bones.md measures 79.3% over the whole corpus. Requiring a clear majority is what
	// separates "the import transform is right" from "the import transform is off by a basis or a
	// factor of a hundred", without pinning the test to a corpus statistic.
	TestTrue(TEXT("most rules pin the driven bone at its own bind position, in centimetres"),
		Total > 0 && OnBind * 2 > Total);
	return true;
}

// ---------------------------------------------------------------------------------------------
// Under a blend — where a per-clip bake would fail and this stage earns its place.
//
// The correction is non-linear: sign-selected among six entries, two slerps, and a 1/(a1+a2+a3)
// normalisation. So evaluating it per clip and blending the results is NOT the same as blending
// first and evaluating once. This checks both halves of that on a real rigged body — that the
// runtime does the second (through a live crossfade the driven bone is the rule applied to the
// blended control rotation), and that the first would have given a measurably different answer.
//
// The body is chosen to be posed from clips its OWN .glb owns, so bone-name matching is
// unambiguous. A cinematic bank holding several complete bipeds on one skeleton is a different
// problem and not this one.
// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCompositionUnderBlendTest,
	"Elysium.Content.CompositionUnderBlend", GElysiumCompositionTestFlags)

bool FElysiumCompositionUnderBlendTest::RunTest(const FString&)
{
	if (!FElysiumContentPaths::IsConfigured())
	{
		AddInfo(TEXT("no export root configured; skipping"));
		return true;
	}
	FElysiumNpcIndex Index;
	FString Error;
	if (!Index.Load(Error))
	{
		AddInfo(FString::Printf(TEXT("no npc index (%s); skipping"), *Error));
		return true;
	}

	// A model with a rule table, a split bone, and at least two clips of its own, so both stages
	// run and the crossfade is between two sequences the same file owns.
	FString Stem;
	TArray<FString> Own;
	for (const TPair<FString, FElysiumNpcIndexEntry>& Pair : Index.Npcs)
	{
		if (Pair.Value.Procedural.IsEmpty() || Pair.Value.SplitRotationBones.IsEmpty()
			|| !FPaths::FileExists(FElysiumContentPaths::NpcGlb(Pair.Key))
			|| (!Stem.IsEmpty() && Pair.Key >= Stem))
		{
			continue;
		}
		FElysiumNpcClipSet Candidate;
		FString SetError;
		if (!Candidate.Load(Pair.Key, SetError))
		{
			continue;
		}
		TArray<FString> Labels;
		for (const TPair<FString, FElysiumNpcClip>& Clip : Candidate.Clips)
		{
			if (Clip.Value.IsOwnedBy(Pair.Key) && Clip.Value.Frames > 1)
			{
				Labels.Add(Clip.Key);
			}
		}
		if (Labels.Num() >= 2)
		{
			Labels.Sort();
			Stem = Pair.Key;
			Own = MoveTemp(Labels);
		}
	}
	if (Stem.IsEmpty())
	{
		AddInfo(TEXT("no exported model carries both stages and two own clips; skipping"));
		return true;
	}

	TSharedRef<FElysiumCompositionRig> Rig = MakeShared<FElysiumCompositionRig>();
	Rig->Stem = Stem;
	for (const FString& BoneName : Index.Npcs[Stem].SplitRotationBones)
	{
		Rig->SplitBones.Add(FName(*BoneName));
	}
	if (!Rig->LoadAxisRules(Index.Npcs[Stem].Procedural, Error))
	{
		AddError(Error);
		return false;
	}

	UglTFRuntimeAsset* Asset = nullptr;
	USkeletalMesh* Mesh = ElysiumNpcVisual::LoadMesh(Stem, Asset, Error);
	if (!TestNotNull(TEXT("the rigged mesh loads"), Mesh))
	{
		AddError(Error);
		return false;
	}
	UAnimSequence* ClipA = ElysiumNpcVisual::RetargetClip(Asset, Mesh, Own[0], Error);
	UAnimSequence* ClipB = ElysiumNpcVisual::RetargetClip(Asset, Mesh, Own[1], Error);
	if (!TestNotNull(TEXT("the first own clip retargets"), ClipA)
		|| !TestNotNull(TEXT("the second own clip retargets"), ClipB))
	{
		AddError(Error);
		return false;
	}

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

	USkeletalMeshComponent* Comp = NewObject<USkeletalMeshComponent>(Owner);
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetSkeletalMeshAsset(Mesh);
	Comp->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	Comp->SetAnimInstanceClass(UElysiumNpcAnimInstance::StaticClass());
	Owner->SetRootComponent(Comp);
	Comp->RegisterComponent();
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	UElysiumNpcAnimInstance* Inst = Cast<UElysiumNpcAnimInstance>(Comp->GetAnimInstance());
	if (!TestNotNull(TEXT("the Elysium animation host is installed"), Inst))
	{
		return false;
	}
	Inst->SetCompositionRig(Rig);

	auto Advance = [Comp](float Delta)
	{
		Comp->TickAnimation(Delta, /*bNeedsValidRootMotion=*/false);
		Comp->RefreshBoneTransforms(/*TickFunction=*/nullptr);
	};
	const FReferenceSkeleton& Reference = Mesh->GetRefSkeleton();
	// One bone's local transform, read back off what the component actually posed.
	auto LocalOf = [&Reference, Comp](const FName BoneName) -> FTransform
	{
		const int32 Index = Reference.FindBoneIndex(BoneName);
		const TArray<FTransform>& Component = Comp->GetComponentSpaceTransforms();
		if (Index == INDEX_NONE || !Component.IsValidIndex(Index))
		{
			return FTransform::Identity;
		}
		const int32 Parent = Reference.GetParentIndex(Index);
		return Component.IsValidIndex(Parent)
			? Component[Index].GetRelativeTransform(Component[Parent])
			: Component[Index];
	};

	Advance(0.f);
	if (!TestTrue(TEXT("the rule table resolved against this skeleton"),
		Inst->GetResolvedAxisInterpRules() > 0))
	{
		return false;
	}

	// Each clip on its own: a driven bone carries the rule's output, not its own decoded channel.
	TArray<FQuat> ControlA, ControlB;
	auto SettleOn = [&](UAnimSequence* Clip, TArray<FQuat>& OutControls, const TCHAR* Which)
	{
		Inst->PlayClip(Clip, /*bLoop=*/true, /*BlendSeconds=*/0.f);
		Advance(1.f / 30.f);
		OutControls.Reset();
		for (const FElysiumAxisInterpRule& Rule : Rig->AxisRules)
		{
			const FQuat ControlLocal = LocalOf(Rule.Control).GetRotation();
			OutControls.Add(ControlLocal);
			TestTrue(FString::Printf(TEXT("%s: '%s' carries the rule output, not its own channel"),
				Which, *Rule.Bone.ToString()),
				LocalOf(Rule.Bone).GetRotation().Equals(
					Rig->EvaluateRule(Rule, ControlLocal).GetRotation(), 1e-3));
		}
	};
	SettleOn(ClipA, ControlA, TEXT("clip A"));
	SettleOn(ClipB, ControlB, TEXT("clip B"));

	// Mid-crossfade. The graph blends the two locals, the hierarchy composes, and only then does
	// the rule run — so a driven bone still equals the rule applied to the LIVE control rotation,
	// even though that rotation belongs to neither clip.
	Inst->PlayClip(ClipA, /*bLoop=*/true, /*BlendSeconds=*/0.f);
	Advance(1.f / 30.f);
	Inst->PlayClip(ClipB, /*bLoop=*/true, /*BlendSeconds=*/1.f);
	Advance(0.5f);
	TestTrue(TEXT("the crossfade is running"), Inst->GetPlayingClip() == ClipB);

	int32 Blended = 0;
	for (const FElysiumAxisInterpRule& Rule : Rig->AxisRules)
	{
		const FQuat ControlLocal = LocalOf(Rule.Control).GetRotation();
		if (TestTrue(FString::Printf(
			TEXT("mid-blend: '%s' is the rule applied to the blended control rotation"),
			*Rule.Bone.ToString()),
			LocalOf(Rule.Bone).GetRotation().Equals(
				Rig->EvaluateRule(Rule, ControlLocal).GetRotation(), 1e-3)))
		{
			++Blended;
		}
	}
	TestTrue(TEXT("at least one driven bone was checked mid-blend"), Blended > 0);

	// And the other order — bake each clip, then blend the bakes — is measurably different. That
	// difference is the whole reason the correction cannot be pre-baked into the clips.
	double WorstDegrees = 0.0;
	for (int32 i = 0; i < Rig->AxisRules.Num(); ++i)
	{
		const FElysiumAxisInterpRule& Rule = Rig->AxisRules[i];
		const FQuat BlendThenEvaluate =
			Rig->EvaluateRule(Rule, FQuat::Slerp(ControlA[i], ControlB[i], 0.5f)).GetRotation();
		const FQuat EvaluateThenBlend = FQuat::Slerp(
			Rig->EvaluateRule(Rule, ControlA[i]).GetRotation(),
			Rig->EvaluateRule(Rule, ControlB[i]).GetRotation(), 0.5f);
		WorstDegrees = FMath::Max(WorstDegrees,
			FMath::RadiansToDegrees(BlendThenEvaluate.AngularDistance(EvaluateThenBlend)));
	}
	AddInfo(FString::Printf(
		TEXT("'%s': %d rules; blend-then-evaluate and evaluate-then-blend differ by up to %.3f deg ")
		TEXT("at a 50/50 blend of '%s' and '%s'"),
		*Stem, Rig->AxisRules.Num(), WorstDegrees, *Own[0], *Own[1]));
	TestTrue(TEXT("a per-clip bake would not have reproduced the blended pose"),
		WorstDegrees > 1e-3);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
