#include "Visual/ElysiumAnimNodes.h"

#include "Visual/ElysiumCompositionRig.h"

#include "Animation/AnimInstanceProxy.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumHairDynamics, Log, All);

namespace
{
	// The chain lives in its animated parent's frame, so clip/grid motion moves the frame instead of
	// dragging the constraints through component space. Admit only a small, bounded share of that
	// frame's acceleration: enough follow-through to read, below the impulse that made it fly out.
	constexpr float HairAnimationMotionAlpha = 0.25f;
	constexpr float HairMaxAnimationAngularVelocity = 3.0f;
	constexpr float HairMaxAnimationAngularAcceleration = 25.0f;
	constexpr float HairComponentLinearAccelerationScale = 0.15f;
	constexpr float HairMaxComponentLinearAcceleration = 200.0f;

	// The component-space transform a bone has at this point in the pass, preferring a correction
	// this pass has already produced over what the incoming pose holds. Retail composes bone by bone
	// into one live array; this is that array, restricted to the handful of bones being rewritten.
	// By value: the search list grows as the pass runs, and the pose computes component space
	// lazily, so neither source is safe to hold a reference into.
	FTransform CurrentComponentSpace(FComponentSpacePoseContext& Output,
		const TArray<FBoneTransform>& Produced, const FCompactPoseBoneIndex BoneIndex)
	{
		for (const FBoneTransform& Transform : Produced)
		{
			if (Transform.BoneIndex == BoneIndex)
			{
				return Transform.Transform;
			}
		}
		return Output.Pose.GetComponentSpaceTransform(BoneIndex);
	}
}

void ElysiumHairDynamics::PreserveChainLocalTransforms(
	FComponentSpacePoseContext& Output, TArray<FBoneTransform>& Transforms)
{
	TArray<FBoneTransform> Corrected;
	Corrected.Reserve(Transforms.Num());

	for (const FBoneTransform& Simulated : Transforms)
	{
		const FCompactPoseBoneIndex BoneIndex = Simulated.BoneIndex;
		const FCompactPoseBoneIndex ParentIndex =
			Output.Pose.GetPose().GetParentBoneIndex(BoneIndex);
		const FTransform SourceLocal = Output.Pose.GetLocalSpaceTransform(BoneIndex);
		const FTransform SourceComponent = Output.Pose.GetComponentSpaceTransform(BoneIndex);
		const FVector SourceAxis = SourceComponent.GetRotation().GetAxisX();
		const FVector SimulatedAxis = Simulated.Transform.GetRotation().GetAxisX();
		const FQuat Swing = FQuat::FindBetweenNormals(SourceAxis, SimulatedAxis);

		FTransform CorrectedComponent = Simulated.Transform;
		CorrectedComponent.SetRotation((Swing * SourceComponent.GetRotation()).GetNormalized());
		if (ParentIndex.IsValid())
		{
			const FTransform CorrectedParent = CurrentComponentSpace(Output, Corrected, ParentIndex);
			FTransform CorrectedLocal = CorrectedComponent.GetRelativeTransform(CorrectedParent);
			CorrectedLocal.SetTranslation(SourceLocal.GetTranslation());
			CorrectedLocal.SetScale3D(SourceLocal.GetScale3D());
			CorrectedComponent = CorrectedLocal * CorrectedParent;
		}
		else
		{
			CorrectedComponent.SetTranslation(SourceLocal.GetTranslation());
			CorrectedComponent.SetScale3D(SourceLocal.GetScale3D());
		}

		Corrected.Add(FBoneTransform(BoneIndex, CorrectedComponent));
	}

	Transforms = MoveTemp(Corrected);
}

// --- stock AnimDynamics hair proof -------------------------------------------------------------

namespace
{
	void ApplyBodyBox(FAnimPhysBodyDefinition& Body, const FReferenceSkeleton& ReferenceSkeleton,
		float ConeAngleDegrees)
	{
		const int32 BoneIndex = ReferenceSkeleton.FindBoneIndex(Body.BoundBone.BoneName);
		FVector Segment = FVector::ZeroVector;
		if (BoneIndex != INDEX_NONE)
		{
			Segment = ReferenceSkeleton.GetRefBonePose()[BoneIndex].GetTranslation();
		}
		const float Length = FMath::Max(static_cast<float>(Segment.Size()), 2.0f);
		Body.BoxExtents = FVector(Length * 0.5f, 1.5f, 1.5f);
		Body.LocalJointOffset = FVector(Length * 0.5f, 0.0f, 0.0f);
		Body.ConstraintSetup.bLinearFullyLocked = true;
		Body.ConstraintSetup.LinearAxesMin = FVector::ZeroVector;
		Body.ConstraintSetup.LinearAxesMax = FVector::ZeroVector;
		Body.ConstraintSetup.AngularConstraintType = AnimPhysAngularConstraintType::Cone;
		Body.ConstraintSetup.TwistAxis = AnimPhysTwistAxis::AxisX;
		Body.ConstraintSetup.AngularTargetAxis = AnimPhysTwistAxis::AxisX;
		Body.ConstraintSetup.AngularTarget = FVector::XAxisVector;
		Body.ConstraintSetup.ConeAngle = ConeAngleDegrees;
	}
}

void FAnimNode_ElysiumHairDynamics::Configure(
	const FElysiumHairDynamicsChainConfig& Config, const FReferenceSkeleton& ReferenceSkeleton)
{
	BoundBone.BoneName = Config.BoundBone;
	ChainEnd.BoneName = Config.ChainEnd;
	bChain = true;
	const int32 BoundIndex = ReferenceSkeleton.FindBoneIndex(Config.BoundBone);
	const int32 RelativeIndex = BoundIndex != INDEX_NONE
		? ReferenceSkeleton.GetParentIndex(BoundIndex)
		: INDEX_NONE;
	if (RelativeIndex == INDEX_NONE)
	{
		UE_LOG(LogElysiumHairDynamics, Warning,
			TEXT("Hair AnimDynamics chain %s -> %s has no valid parent simulation bone; disabling it"),
			*Config.BoundBone.ToString(), *Config.ChainEnd.ToString());
		BoundBone.BoneName = NAME_None;
		ChainEnd.BoneName = NAME_None;
		bChain = false;
		PhysicsBodyDefinitions.Reset();
		RequestInitialise(ETeleportType::ResetPhysics);
		return;
	}
	SimulationSpace = AnimPhysSimSpaceType::BoneRelative;
	RelativeSpaceBone.BoneName = ReferenceSkeleton.GetBoneName(RelativeIndex);
	GravityScale = Config.GravityScale;
	bOverrideLinearDamping = true;
	bOverrideAngularDamping = true;
	LinearDampingOverride = Config.Damping;
	AngularDampingOverride = Config.Damping;
	bAngularSpring = Config.AngularSpring > 0.0f;
	AngularSpringConstant = Config.AngularSpring;
	NumSolverIterationsPreUpdate = 8;
	NumSolverIterationsPostUpdate = 2;
	ComponentLinearAccScale = FVector(HairComponentLinearAccelerationScale);
	ComponentLinearVelScale = FVector::ZeroVector;
	ComponentAppliedLinearAccClamp = FVector(HairMaxComponentLinearAcceleration);
	SimSpaceSettings.SimSpaceAngularAlpha = HairAnimationMotionAlpha;
	SimSpaceSettings.MaxAngularVelocity = HairMaxAnimationAngularVelocity;
	SimSpaceSettings.MaxAngularAcceleration = HairMaxAnimationAngularAcceleration;
	bUsePlanarLimit = false;
	bUseSphericalLimits = false;
	bEnableWind = false;
	LODThreshold = 2;

	// Start with one prototype, let the stock node resolve the inclusive chain, then size each
	// rigid body from the actual native reference segment instead of a character-wide constant.
	PhysicsBodyDefinitions.Reset();
	PhysicsBodyDefinitions.AddDefaulted();
	UpdateChainPhysicsBodyDefinitions(ReferenceSkeleton);
	for (int32 Index = 0; Index < PhysicsBodyDefinitions.Num(); ++Index)
	{
		FAnimPhysBodyDefinition& Body = PhysicsBodyDefinitions[Index];
		const int32 BoneIndex = ReferenceSkeleton.FindBoneIndex(Body.BoundBone.BoneName);
		FVector Segment = FVector::ZeroVector;
		if (PhysicsBodyDefinitions.IsValidIndex(Index + 1))
		{
			const int32 ChildIndex = ReferenceSkeleton.FindBoneIndex(
				PhysicsBodyDefinitions[Index + 1].BoundBone.BoneName);
			if (ChildIndex != INDEX_NONE)
			{
				Segment = ReferenceSkeleton.GetRefBonePose()[ChildIndex].GetTranslation();
			}
		}
		if (Segment.IsNearlyZero() && BoneIndex != INDEX_NONE)
		{
			Segment = ReferenceSkeleton.GetRefBonePose()[BoneIndex].GetTranslation();
		}
		const float Length = FMath::Max(static_cast<float>(Segment.Size()), 2.0f);
		Body.BoxExtents = FVector(Length * 0.5f, 1.5f, 1.5f);
		Body.LocalJointOffset = FVector(Length * 0.5f, 0.0f, 0.0f);
		Body.ConstraintSetup.bLinearFullyLocked = true;
		Body.ConstraintSetup.LinearAxesMin = FVector::ZeroVector;
		Body.ConstraintSetup.LinearAxesMax = FVector::ZeroVector;
		Body.ConstraintSetup.AngularConstraintType = AnimPhysAngularConstraintType::Cone;
		Body.ConstraintSetup.TwistAxis = AnimPhysTwistAxis::AxisX;
		Body.ConstraintSetup.AngularTargetAxis = AnimPhysTwistAxis::AxisX;
		Body.ConstraintSetup.AngularTarget = FVector::XAxisVector;
		Body.ConstraintSetup.ConeAngle = Config.ConeAngleDegrees;
	}
	RequestInitialise(ETeleportType::ResetPhysics);
}

void FAnimNode_ElysiumHairDynamics::ConfigureBody(
	const FElysiumHairDynamicsBodyConfig& Config, const FReferenceSkeleton& ReferenceSkeleton)
{
	BoundBone.BoneName = Config.BoundBone;
	ChainEnd.BoneName = Config.BoundBone;
	bChain = false;
	SimulationSpace = AnimPhysSimSpaceType::Component;
	GravityScale = Config.GravityScale;
	bOverrideLinearDamping = true;
	bOverrideAngularDamping = true;
	LinearDampingOverride = Config.Damping;
	AngularDampingOverride = Config.Damping;
	bAngularSpring = Config.AngularSpring > 0.0f;
	AngularSpringConstant = Config.AngularSpring;
	NumSolverIterationsPreUpdate = 8;
	NumSolverIterationsPostUpdate = 2;
	ComponentLinearAccScale = FVector::OneVector;
	ComponentLinearVelScale = FVector::ZeroVector;
	ComponentAppliedLinearAccClamp = FVector(2500.0);
	SimSpaceSettings.SimSpaceAngularAlpha = 1.0f;
	SimSpaceSettings.MaxAngularVelocity = 10.0f;
	SimSpaceSettings.MaxAngularAcceleration = 100.0f;
	bUsePlanarLimit = false;
	bUseSphericalLimits = false;
	bEnableWind = false;
	LODThreshold = 2;

	PhysicsBodyDefinitions.Reset();
	FAnimPhysBodyDefinition& Body = PhysicsBodyDefinitions.AddDefaulted_GetRef();
	Body.BoundBone.BoneName = Config.BoundBone;
	ApplyBodyBox(Body, ReferenceSkeleton, Config.ConeAngleDegrees);
	RequestInitialise(ETeleportType::ResetPhysics);
}

void FAnimNode_ElysiumHairDynamics::Apply(FComponentSpacePoseContext& Output)
{
	const FBoneContainer& RequiredBones = Output.Pose.GetPose().GetBoneContainer();
	if (!FAnimWeight::IsRelevant(ActualAlpha)
		|| !IsValidToEvaluate(nullptr, RequiredBones))
	{
		return;
	}
	TArray<FBoneTransform> Transforms;
	EvaluateSkeletalControl_AnyThread(Output, Transforms);
	if (!Transforms.IsEmpty())
	{
		if (bChain)
		{
			ElysiumHairDynamics::PreserveChainLocalTransforms(Output, Transforms);
		}
		Output.Pose.LocalBlendCSBoneTransforms(Transforms, ActualAlpha);
	}
}

// --- axis interpolation ------------------------------------------------------------------------

void FAnimNode_ElysiumAxisInterp::SetRig(TSharedPtr<const FElysiumCompositionRig> InRig)
{
	Rig = MoveTemp(InRig);
	Resolved.Reset();
	bResolved = false;
}

bool FAnimNode_ElysiumAxisInterp::HasWork() const
{
	return Rig.IsValid() && !Rig->AxisRules.IsEmpty();
}

void FAnimNode_ElysiumAxisInterp::ResolveBones(const FBoneContainer& RequiredBones)
{
	InitializeBoneReferences(RequiredBones);
}

void FAnimNode_ElysiumAxisInterp::InitializeBoneReferences(const FBoneContainer& RequiredBones)
{
	Resolved.Reset();
	bResolved = true;
	if (!HasWork())
	{
		return;
	}

	Resolved.Reserve(Rig->AxisRules.Num());
	for (int32 RuleIndex = 0; RuleIndex < Rig->AxisRules.Num(); ++RuleIndex)
	{
		const FElysiumAxisInterpRule& Rule = Rig->AxisRules[RuleIndex];
		FResolvedRule Entry;
		Entry.RuleIndex = RuleIndex;
		Entry.Bone = FBoneReference(Rule.Bone);
		Entry.Control = FBoneReference(Rule.Control);
		Entry.Bone.Initialize(RequiredBones);
		Entry.Control.Initialize(RequiredBones);
		// A rule whose driven bone or control is not in this LOD's bone container simply does not
		// run — the bone it would correct is not being posed either.
		if (Entry.Bone.IsValidToEvaluate(RequiredBones)
			&& Entry.Control.IsValidToEvaluate(RequiredBones))
		{
			Resolved.Add(MoveTemp(Entry));
		}
	}
	Resolved.Sort([&RequiredBones](const FResolvedRule& A, const FResolvedRule& B)
	{
		return A.Bone.GetCompactPoseIndex(RequiredBones) < B.Bone.GetCompactPoseIndex(RequiredBones);
	});
}

bool FAnimNode_ElysiumAxisInterp::IsValidToEvaluate(const USkeleton*, const FBoneContainer&)
{
	return !Resolved.IsEmpty();
}

void FAnimNode_ElysiumAxisInterp::Apply(FComponentSpacePoseContext& Output)
{
	if (!bResolved)
	{
		InitializeBoneReferences(Output.Pose.GetPose().GetBoneContainer());
	}
	if (Resolved.IsEmpty()
		|| (Output.AnimInstanceProxy != nullptr && !IsLODEnabled(Output.AnimInstanceProxy)))
	{
		return;
	}

	// Named for what it is, and not `BoneTransforms`: the base class keeps a scratch array of
	// that name for the graph path this node does not take.
	TArray<FBoneTransform> Corrections;
	EvaluateSkeletalControl_AnyThread(Output, Corrections);
	if (!Corrections.IsEmpty())
	{
		Output.Pose.LocalBlendCSBoneTransforms(Corrections, 1.f);
	}
}

void FAnimNode_ElysiumAxisInterp::EvaluateSkeletalControl_AnyThread(
	FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms)
{
	const FBoneContainer& RequiredBones = Output.Pose.GetPose().GetBoneContainer();

	for (const FResolvedRule& Entry : Resolved)
	{
		const FElysiumAxisInterpRule& Rule = Rig->AxisRules[Entry.RuleIndex];

		const FCompactPoseBoneIndex ControlIndex = Entry.Control.GetCompactPoseIndex(RequiredBones);
		const FCompactPoseBoneIndex ControlParent =
			Output.Pose.GetPose().GetParentBoneIndex(ControlIndex);

		const FQuat ControlComponent =
			CurrentComponentSpace(Output, OutBoneTransforms, ControlIndex).GetRotation();
		const FQuat ControlLocal = ControlParent.IsValid()
			? CurrentComponentSpace(Output, OutBoneTransforms, ControlParent)
				.GetRotation().Inverse() * ControlComponent
			: ControlComponent;

		// The rule produces the driven bone's local transform outright — the animated one is
		// discarded, not adjusted.
		const FTransform NewLocal = Rig->EvaluateRule(Rule, ControlLocal);

		const FCompactPoseBoneIndex BoneIndex = Entry.Bone.GetCompactPoseIndex(RequiredBones);
		const FCompactPoseBoneIndex ParentIndex = Output.Pose.GetPose().GetParentBoneIndex(BoneIndex);
		const FTransform NewComponent = ParentIndex.IsValid()
			? NewLocal * CurrentComponentSpace(Output, OutBoneTransforms, ParentIndex)
			: NewLocal;

		OutBoneTransforms.Add(FBoneTransform(BoneIndex, NewComponent));
	}
}
