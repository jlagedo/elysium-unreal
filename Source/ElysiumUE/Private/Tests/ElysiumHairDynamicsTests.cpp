#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Algo/AllOf.h"

#include "ElysiumContentPaths.h"
#include "Visual/ElysiumAnimNodes.h"
#include "Visual/ElysiumBodyAnimInstance.h"
#include "Visual/ElysiumHairDynamicsConfig.h"
#include "Visual/ElysiumHairDynamicsData.h"
#include "ElysiumCastData.h"

#include "Animation/AnimInstanceProxy.h"
#include "Animation/Skeleton.h"
#include "BonePose.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/IConsoleManager.h"
#include "Misc/MemStack.h"
#include "ReferenceSkeleton.h"

namespace
{
	static constexpr EAutomationTestFlags GElysiumHairDynamicsTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	TArray<FTransform> HairTestLocals()
	{
		TArray<FTransform> Locals;
		Locals.Add(FTransform::Identity);
		Locals.Add(FTransform(FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(20.0f)),
			FVector(10.0f, -2.0f, 3.0f)));
		Locals.Add(FTransform(FQuat(FVector::YAxisVector, FMath::DegreesToRadians(-15.0f)),
			FVector(3.0f, 4.0f, 0.0f), FVector(1.1f)));
		Locals.Add(FTransform(FQuat(FVector::XAxisVector, FMath::DegreesToRadians(12.0f)),
			FVector(-2.0f, 1.0f, 6.0f)));
		Locals.Add(FTransform(FQuat(FVector(1.0f, 1.0f, 0.0f).GetSafeNormal(),
			FMath::DegreesToRadians(8.0f)), FVector(1.0f, -3.0f, 2.0f)));
		return Locals;
	}

	USkeleton* BuildHairTestSkeleton(const TArray<FTransform>& Locals)
	{
		const FName Names[] = {
			TEXT("root"), TEXT("anchor"), TEXT("strand_a"), TEXT("strand_b"), TEXT("tip")
		};
		const int32 Parents[] = { INDEX_NONE, 0, 1, 2, 3 };
		USkeleton* Skeleton = NewObject<USkeleton>();
		{
			FReferenceSkeletonModifier Modifier(Skeleton);
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(Names); ++Index)
			{
				Modifier.Add(FMeshBoneInfo(Names[Index], Names[Index].ToString(), Parents[Index]),
					Locals[Index]);
			}
		}
		return Skeleton;
	}

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumHairDynamicsSegmentLengthTest,
	"Elysium.Substrate.HairDynamics.SegmentLengthInvariant", GElysiumHairDynamicsTestFlags)

bool FElysiumHairDynamicsSegmentLengthTest::RunTest(const FString& Parameters)
{
	const TArray<FTransform> SourceLocals = HairTestLocals();
	USkeleton* Skeleton = BuildHairTestSkeleton(SourceLocals);
	FMemMark Mark(FMemStack::Get());

	auto RunCase = [this, Skeleton, &SourceLocals](float Alpha, int32 FirstBone)
	{
		TArray<FBoneIndexType> RequiredBones;
		RequiredBones.SetNumUninitialized(SourceLocals.Num());
		for (int32 Index = 0; Index < RequiredBones.Num(); ++Index)
		{
			RequiredBones[Index] = static_cast<FBoneIndexType>(Index);
		}

		FBoneContainer Container;
		Container.InitializeTo(RequiredBones,
			UE::Anim::FCurveFilterSettings(UE::Anim::ECurveFilterMode::None), *Skeleton);
		FCompactPose LocalPose;
		LocalPose.SetBoneContainer(&Container);
		for (int32 Index = 0; Index < SourceLocals.Num(); ++Index)
		{
			LocalPose[FCompactPoseBoneIndex(Index)] = SourceLocals[Index];
		}

		FAnimInstanceProxy Proxy;
		FComponentSpacePoseContext Output(&Proxy);
		Output.Pose.InitPose(LocalPose);

		TArray<FBoneTransform> Simulated;
		TArray<FQuat> ExpectedSwingOnlyRotations;
		for (int32 Index = FirstBone; Index < SourceLocals.Num(); ++Index)
		{
			const FVector Axis = FVector(1.0f, static_cast<float>(Index), 0.5f).GetSafeNormal();
			const FTransform Unconverged(
				FQuat(Axis, FMath::DegreesToRadians(70.0f + 11.0f * Index)),
				FVector(1000.0f * Index, -750.0f * Index, 500.0f * Index),
				FVector(4.0f, 3.0f, 2.0f));
			Simulated.Add(FBoneTransform(FCompactPoseBoneIndex(Index), Unconverged));
			const FTransform SourceComponent =
				Output.Pose.GetComponentSpaceTransform(FCompactPoseBoneIndex(Index));
			ExpectedSwingOnlyRotations.Add((FQuat::FindBetweenNormals(
				SourceComponent.GetRotation().GetAxisX(), Unconverged.GetRotation().GetAxisX())
				* SourceComponent.GetRotation()).GetNormalized());
		}

		ElysiumHairDynamics::PreserveChainLocalTransforms(Output, Simulated);
		for (int32 ResultIndex = 0; ResultIndex < Simulated.Num(); ++ResultIndex)
		{
			TestTrue(*FString::Printf(TEXT("bone %d removes free axial roll"),
				FirstBone + ResultIndex), Simulated[ResultIndex].Transform.GetRotation().Equals(
					ExpectedSwingOnlyRotations[ResultIndex], 0.001));
		}
		Output.Pose.LocalBlendCSBoneTransforms(Simulated, Alpha);

		bool bRotationChanged = false;
		for (int32 Index = FirstBone; Index < SourceLocals.Num(); ++Index)
		{
			const FTransform Result = Output.Pose.GetLocalSpaceTransform(FCompactPoseBoneIndex(Index));
			TestTrue(*FString::Printf(TEXT("alpha %.2f bone %d keeps local translation"), Alpha, Index),
				Result.GetTranslation().Equals(SourceLocals[Index].GetTranslation(), 0.001));
			TestTrue(*FString::Printf(TEXT("alpha %.2f bone %d keeps segment length"), Alpha, Index),
				FMath::IsNearlyEqual(Result.GetTranslation().Size(),
					SourceLocals[Index].GetTranslation().Size(), 0.001));
			TestTrue(*FString::Printf(TEXT("alpha %.2f bone %d keeps local scale"), Alpha, Index),
				Result.GetScale3D().Equals(SourceLocals[Index].GetScale3D(), 0.001));
			bRotationChanged |= !Result.GetRotation().Equals(
				SourceLocals[Index].GetRotation(), 0.001);
		}
		TestTrue(*FString::Printf(TEXT("alpha %.2f still applies simulated rotation"), Alpha),
			bRotationChanged);
	};

	RunCase(1.0f, 1);
	RunCase(0.35f, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumHairDynamicsPresentationTuningTest,
	"Elysium.Substrate.HairDynamics.PresentationTuning", GElysiumHairDynamicsTestFlags)

bool FElysiumHairDynamicsPresentationTuningTest::RunTest(const FString& Parameters)
{
	const TArray<FTransform> Locals = HairTestLocals();
	USkeleton* Skeleton = BuildHairTestSkeleton(Locals);
	FElysiumHairDynamicsChainConfig Chain;
	Chain.BoundBone = TEXT("anchor");
	Chain.ChainEnd = TEXT("tip");
	Chain.GravityScale = 1.0f;
	Chain.Damping = 0.9f;
	Chain.ConeAngleDegrees = 45.0f;

	FAnimNode_ElysiumHairDynamics Node;
	Node.Configure(Chain, Skeleton->GetReferenceSkeleton());
	TestEqual(TEXT("hair uses the bound root parent as its simulation frame"),
		Node.SimulationSpace, AnimPhysSimSpaceType::BoneRelative);
	TestEqual(TEXT("hair simulation frame is the anchor parent"),
		Node.RelativeSpaceBone.BoneName, FName(TEXT("root")));
	TestEqual(TEXT("hair keeps authored gravity"), Node.GravityScale, Chain.GravityScale);
	TestEqual(TEXT("hair keeps authored linear damping"),
		Node.LinearDampingOverride, Chain.Damping);
	TestEqual(TEXT("hair keeps authored angular damping"),
		Node.AngularDampingOverride, Chain.Damping);
	TestFalse(TEXT("zero authored spring stays disabled"), Node.bAngularSpring);
	TestEqual(TEXT("hair keeps the authored angular spring"),
		Node.AngularSpringConstant, Chain.AngularSpring);
	TestTrue(TEXT("hair admits a bounded component linear acceleration share"),
		Node.ComponentLinearAccScale.Equals(FVector(0.15f)));
	TestTrue(TEXT("hair clamps component linear acceleration"),
		Node.ComponentAppliedLinearAccClamp.Equals(FVector(200.0f)));
	TestEqual(TEXT("hair admits a bounded animation-frame angular share"),
		Node.SimSpaceSettings.SimSpaceAngularAlpha, 0.25f);
	TestEqual(TEXT("hair clamps animation angular velocity"),
		Node.SimSpaceSettings.MaxAngularVelocity, 3.0f);
	TestEqual(TEXT("hair clamps animation angular acceleration"),
		Node.SimSpaceSettings.MaxAngularAcceleration, 25.0f);
	TestTrue(TEXT("hair keeps the authored cone on every chain body"),
		Algo::AllOf(Node.PhysicsBodyDefinitions, [&Chain](const FAnimPhysBodyDefinition& Body)
		{
			return Body.ConstraintSetup.ConeAngle == Chain.ConeAngleDegrees;
		}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumHairDynamicsResetRoutingTest,
	"Elysium.Substrate.HairDynamics.ResetRouting", GElysiumHairDynamicsTestFlags)

bool FElysiumHairDynamicsResetRoutingTest::RunTest(const FString& Parameters)
{
	const TArray<FTransform> Locals = HairTestLocals();
	USkeleton* Skeleton = BuildHairTestSkeleton(Locals);
	const FReferenceSkeleton& ReferenceSkeleton = Skeleton->GetReferenceSkeleton();

	FElysiumHairDynamicsChainConfig Chain;
	Chain.BoundBone = TEXT("anchor");
	Chain.ChainEnd = TEXT("tip");
	Chain.ConeAngleDegrees = 45.0f;
	FElysiumHairDynamicsBodyConfig Body;
	Body.BoundBone = TEXT("strand_a");
	Body.ConeAngleDegrees = 20.0f;

	FElysiumBodyAnimProxy Proxy;
	Proxy.SetHairDynamics({ Chain }, { Body }, ReferenceSkeleton);
	TArray<FAnimNode_Base*> CustomNodes;
	Proxy.GetCustomNodes(CustomNodes);
	if (TestEqual(TEXT("one stable reset bridge is registered as a custom node"),
		CustomNodes.Num(), 1)
		&& TestTrue(TEXT("the bridge participates in engine dynamics reset"),
			CustomNodes[0]->NeedsDynamicReset()))
	{
		Proxy.ClearHairDynamicResetRequestsForTest();
		CustomNodes[0]->ResetDynamics(ETeleportType::TeleportPhysics);
		TestTrue(TEXT("teleport reset reaches the current chain and body"),
			Proxy.HairDynamicsRequestedResetForTest(ETeleportType::TeleportPhysics));
	}

	Proxy.SetHairDynamics({ Chain }, {}, ReferenceSkeleton);
	Proxy.ClearHairDynamicResetRequestsForTest();
	CustomNodes[0]->ResetDynamics(ETeleportType::ResetPhysics);
	TestTrue(TEXT("reconfiguration routes reset only to the replacement chain"),
		Proxy.HairDynamicsRequestedResetForTest(ETeleportType::ResetPhysics));

	Proxy.SetHairDynamics({}, {}, ReferenceSkeleton);
	CustomNodes[0]->ResetDynamics(ETeleportType::ResetPhysics);
	TestEqual(TEXT("clearing dynamics leaves no simulated nodes"),
		Proxy.NumHairDynamicsChains() + Proxy.NumHairDynamicsBodies(), 0);
	return true;
}

#endif
