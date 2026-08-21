#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Algo/AllOf.h"

#include "ElysiumContentPaths.h"
#include "Visual/ElysiumAnimNodes.h"
#include "Visual/ElysiumBodyAnimInstance.h"
#include "Visual/ElysiumHairDynamicsData.h"

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
	TestEqual(TEXT("diagnostic hair gravity is disabled"), Node.GravityScale, 0.0f);
	TestEqual(TEXT("diagnostic hair linear damping is maximal"),
		Node.LinearDampingOverride, 1.0f);
	TestEqual(TEXT("diagnostic hair angular damping is maximal"),
		Node.AngularDampingOverride, 1.0f);
	TestTrue(TEXT("diagnostic hair angular spring is enabled"), Node.bAngularSpring);
	TestEqual(TEXT("diagnostic hair angular spring is intentionally rigid"),
		Node.AngularSpringConstant, 1000.0f);
	TestTrue(TEXT("diagnostic hair ignores component linear acceleration"),
		Node.ComponentLinearAccScale.IsZero());
	TestTrue(TEXT("diagnostic hair has no component linear acceleration allowance"),
		Node.ComponentAppliedLinearAccClamp.IsZero());
	TestEqual(TEXT("diagnostic hair ignores simulation-space rotation"),
		Node.SimSpaceSettings.SimSpaceAngularAlpha, 0.0f);
	TestEqual(TEXT("diagnostic hair angular velocity is zero"),
		Node.SimSpaceSettings.MaxAngularVelocity, 0.0f);
	TestEqual(TEXT("diagnostic hair angular acceleration is zero"),
		Node.SimSpaceSettings.MaxAngularAcceleration, 0.0f);
	TestTrue(TEXT("diagnostic hair locks every chain cone"),
		Algo::AllOf(Node.PhysicsBodyDefinitions, [](const FAnimPhysBodyDefinition& Body)
		{
			return Body.ConstraintSetup.ConeAngle == 0.0f;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumHairDynamicsBakedScopeTest,
	"Elysium.Content.Characters.HairDynamicsBakedScope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FElysiumHairDynamicsBakedScopeTest::RunTest(const FString& Parameters)
{
	TestNull(TEXT("hair proof has no runtime feature CVar"),
		IConsoleManager::Get().FindConsoleVariable(TEXT("elysium.HairDynamics")));

	struct FSubject
	{
		const TCHAR* Stem;
		bool bPlayerMaterial;
		int32 ExpectedChains;
		const TCHAR* First;
		const TCHAR* Last;
	};
	const FSubject Subjects[] = {
		{ TEXT("malkavian_female_armor_0"), true, 1, TEXT("Bone05"), TEXT("Bone09") },
		{ TEXT("jeanette"), false, 2, TEXT("Bone01"), TEXT("Bone13") },
	};

	TArray<USkeletalMesh*> Meshes;
	for (const FSubject& Subject : Subjects)
	{
		Meshes.Add(LoadObject<USkeletalMesh>(nullptr,
			*FElysiumContentPaths::BakedCharacterMesh(Subject.Stem, Subject.bPlayerMaterial),
			nullptr, LOAD_NoWarn | LOAD_Quiet));
	}
	if (Algo::AllOf(Meshes, [](const USkeletalMesh* Mesh) { return Mesh == nullptr; }))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: neither hair proof body is baked; run the focused character export"));
		return true;
	}

	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Subjects); ++Index)
	{
		const FSubject& Subject = Subjects[Index];
		USkeletalMesh* Mesh = Meshes[Index];
		if (!TestNotNull(*FString::Printf(TEXT("%s proof mesh is baked"), Subject.Stem), Mesh))
		{
			continue;
		}
		const UElysiumHairDynamicsAssetUserData* Hair =
			Cast<UElysiumHairDynamicsAssetUserData>(Mesh->GetAssetUserDataOfClass(
				UElysiumHairDynamicsAssetUserData::StaticClass()));
		if (!TestNotNull(*FString::Printf(TEXT("%s carries hair metadata"), Subject.Stem), Hair))
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s has exact chain count"), Subject.Stem),
			Hair->Chains.Num(), Subject.ExpectedChains);
		if (!Hair->Chains.IsEmpty())
		{
			TestEqual(TEXT("first selected bound bone"), Hair->Chains[0].BoundBone,
				FName(Subject.First));
			TestEqual(TEXT("last selected chain end"), Hair->Chains.Last().ChainEnd,
				FName(Subject.Last));
			if (FStringView(Subject.Stem).Equals(TEXT("jeanette")) && Hair->Chains.Num() == 2)
			{
				TestEqual(TEXT("Jeanette first chain end"), Hair->Chains[0].ChainEnd,
					FName(TEXT("Bone07")));
				TestEqual(TEXT("Jeanette second bound bone"), Hair->Chains[1].BoundBone,
					FName(TEXT("Bone09")));
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumBreastDynamicsBakedScopeTest,
	"Elysium.Content.Characters.BreastDynamicsBakedScope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FElysiumBreastDynamicsBakedScopeTest::RunTest(const FString& Parameters)
{
	TestNull(TEXT("breast proof has no runtime feature CVar"),
		IConsoleManager::Get().FindConsoleVariable(TEXT("elysium.BreastDynamics")));

	USkeletalMesh* Jeanette = LoadObject<USkeletalMesh>(nullptr,
		*FElysiumContentPaths::BakedCharacterMesh(TEXT("jeanette")),
		nullptr, LOAD_NoWarn | LOAD_Quiet);
	USkeletalMesh* Lily = LoadObject<USkeletalMesh>(nullptr,
		*FElysiumContentPaths::BakedCharacterMesh(TEXT("lily")),
		nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (Jeanette == nullptr && Lily == nullptr)
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no breast body is baked; run a focused character export"));
		return true;
	}

	if (Jeanette != nullptr)
	{
		const UElysiumHairDynamicsAssetUserData* Hair =
			Cast<UElysiumHairDynamicsAssetUserData>(Jeanette->GetAssetUserDataOfClass(
				UElysiumHairDynamicsAssetUserData::StaticClass()));
		if (Hair == nullptr || Hair->Bodies.Num() == 0)
		{
			AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: jeanette is baked without breast bodies; re-export jeanette"));
		}
		else if (TestEqual(TEXT("jeanette has two breast bodies"), Hair->Bodies.Num(), 2))
		{
			TSet<FName> Names;
			for (const FElysiumHairDynamicsBodyConfig& Body : Hair->Bodies)
			{
				Names.Add(Body.BoundBone);
				TestTrue(TEXT("jeanette breast cone is within the bake ceiling"),
					Body.ConeAngleDegrees >= 0.0f && Body.ConeAngleDegrees <= 90.0f);
			}
			TestTrue(TEXT("jeanette right breast"), Names.Contains(FName(TEXT("right breast"))));
			TestTrue(TEXT("jeanette left breast"), Names.Contains(FName(TEXT("left breast"))));
			TestEqual(TEXT("jeanette hair chains stay a pair"), Hair->Chains.Num(), 2);
		}
	}

	if (Lily != nullptr)
	{
		const UElysiumHairDynamicsAssetUserData* Hair =
			Cast<UElysiumHairDynamicsAssetUserData>(Lily->GetAssetUserDataOfClass(
				UElysiumHairDynamicsAssetUserData::StaticClass()));
		if (Hair == nullptr || Hair->Bodies.Num() == 0)
		{
			AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: lily is baked without breast bodies; re-export lily"));
		}
		else
		{
			TestEqual(TEXT("lily has two breast bodies"), Hair->Bodies.Num(), 2);
			TestEqual(TEXT("lily carries no hair proof chains"), Hair->Chains.Num(), 0);
			TSet<FName> Names;
			for (const FElysiumHairDynamicsBodyConfig& Body : Hair->Bodies)
			{
				Names.Add(Body.BoundBone);
			}
			TestTrue(TEXT("lily BoobRight01"), Names.Contains(FName(TEXT("BoobRight01"))));
			TestTrue(TEXT("lily BoobLeft03"), Names.Contains(FName(TEXT("BoobLeft03"))));
		}
	}
	return true;
}

#endif
