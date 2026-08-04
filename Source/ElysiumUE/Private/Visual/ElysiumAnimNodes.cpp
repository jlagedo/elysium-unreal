#include "Visual/ElysiumAnimNodes.h"

#include "Visual/ElysiumClothRig.h"
#include "Visual/ElysiumCompositionRig.h"

#include "Animation/AnimInstanceProxy.h"

namespace
{
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

// --- split inheritance -------------------------------------------------------------------------

void FAnimNode_ElysiumSplitInheritance::SetRig(TSharedPtr<const FElysiumCompositionRig> InRig)
{
	Rig = MoveTemp(InRig);
	Bones.Reset();
	bResolved = false;
}

bool FAnimNode_ElysiumSplitInheritance::HasWork() const
{
	return Rig.IsValid() && !Rig->SplitBones.IsEmpty();
}

void FAnimNode_ElysiumSplitInheritance::ResolveBones(const FBoneContainer& RequiredBones)
{
	InitializeBoneReferences(RequiredBones);
}

void FAnimNode_ElysiumSplitInheritance::InitializeBoneReferences(const FBoneContainer& RequiredBones)
{
	Bones.Reset();
	bResolved = true;
	if (!HasWork())
	{
		return;
	}

	for (const FName BoneName : Rig->SplitBones)
	{
		FBoneReference Ref(BoneName);
		Ref.Initialize(RequiredBones);
		if (Ref.IsValidToEvaluate(RequiredBones))
		{
			Bones.Add(Ref);
		}
	}
	// LocalBlendCSBoneTransforms requires parents before children, and compact-pose indices are
	// already in that order.
	Bones.Sort([&RequiredBones](const FBoneReference& A, const FBoneReference& B)
	{
		return A.GetCompactPoseIndex(RequiredBones) < B.GetCompactPoseIndex(RequiredBones);
	});
}

bool FAnimNode_ElysiumSplitInheritance::IsValidToEvaluate(const USkeleton*, const FBoneContainer&)
{
	return !Bones.IsEmpty();
}

void FAnimNode_ElysiumSplitInheritance::Apply(FComponentSpacePoseContext& Output)
{
	if (!bResolved)
	{
		InitializeBoneReferences(Output.Pose.GetPose().GetBoneContainer());
	}
	// The LOD gate only applies where there is a component to have an LOD: a bare pose driven
	// straight through this node (the composition fixture) carries no proxy and always evaluates.
	if (Bones.IsEmpty()
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

void FAnimNode_ElysiumSplitInheritance::EvaluateSkeletalControl_AnyThread(
	FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms)
{
	const FBoneContainer& RequiredBones = Output.Pose.GetPose().GetBoneContainer();

	for (const FBoneReference& Ref : Bones)
	{
		const FCompactPoseBoneIndex BoneIndex = Ref.GetCompactPoseIndex(RequiredBones);
		const FCompactPoseBoneIndex ParentIndex = Output.Pose.GetPose().GetParentBoneIndex(BoneIndex);
		if (!ParentIndex.IsValid())
		{
			// A flagged root has no parent to decline: the ordinary rule already roots it in the
			// entity transform, which is what the flag asks for.
			continue;
		}

		const FTransform Composed = CurrentComponentSpace(Output, OutBoneTransforms, BoneIndex);
		const FTransform Parent = CurrentComponentSpace(Output, OutBoneTransforms, ParentIndex);

		// Local rotation, then use it as the component-space rotation: with the entity transform
		// divided out, "rooted directly in the entity transform" is "rooted in component space".
		FTransform Split = Composed;
		Split.SetRotation((Composed.GetRelativeTransform(Parent)).GetRotation());
		OutBoneTransforms.Add(FBoneTransform(BoneIndex, Split));
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

// --- the garment spike ---------------------------------------------------------------------------

void FAnimNode_ElysiumCloth::SetRig(TSharedPtr<const FElysiumClothRig> InRig)
{
	Rig = MoveTemp(InRig);
	// A new garment arrives untuned: the tuning starts as whatever this rig's own file asks for, so
	// installing a rig and never touching a slider behaves exactly as it did before tuning existed.
	Tuning = Rig.IsValid() ? FElysiumClothTuning::FromRig(*Rig) : FElysiumClothTuning();
	// The chains hold live simulation state bound to the previous rig's bone names, so they are
	// dropped rather than reconfigured. CacheBones rebuilds them against the current container.
	Chains.Reset();
	bBuilt = false;
}

void FAnimNode_ElysiumCloth::SetTuning(const FElysiumClothTuning& InTuning)
{
	// Damping and the body extents are read once, when `InitPhysics` builds the rigid bodies from
	// the definitions — so unlike everything else here, writing them changes nothing until the chain
	// is re-seated. Asking for that unconditionally would make every slider drop the garment.
	const bool bReseat = InTuning.NeedsReseat(Tuning);
	Tuning = InTuning;
	if (!bBuilt)
	{
		return;
	}
	ApplyTuning();
	if (bReseat)
	{
		for (FElysiumClothChainNode& Node : Chains)
		{
			Node.RequestInitialise(ETeleportType::ResetPhysics);
		}
	}
}

void FAnimNode_ElysiumCloth::ApplyTuning()
{
	if (!Rig.IsValid())
	{
		return;
	}
	const FElysiumClothSolver& Solver = Tuning.Solver;
	for (int32 Index = 0; Index < Chains.Num() && Index < Rig->Chains.Num(); ++Index)
	{
		FElysiumClothChainNode& Node = Chains[Index];
		const FElysiumClothChain& Source = Rig->Chains[Index];

		Node.GravityScale = Solver.GravityScale;
		Node.bOverrideLinearDamping = true;
		Node.LinearDampingOverride = Solver.LinearDamping;
		Node.bOverrideAngularDamping = true;
		Node.AngularDampingOverride = Solver.AngularDamping;
		Node.NumSolverIterationsPreUpdate = Solver.IterationsPre;
		Node.NumSolverIterationsPostUpdate = Solver.IterationsPost;
		// Both of these are zero-initialised by the engine, which leaves a body reacting only to
		// its bound bone rotating. Without them the garment ignores the character walking entirely.
		Node.ComponentLinearVelScale = FVector(Solver.ComponentLinearVelScale);
		Node.ComponentLinearAccScale = FVector(Solver.ComponentLinearAccScale);

		for (int32 LimitIndex = 0;
			LimitIndex < Node.SphericalLimits.Num() && LimitIndex < Rig->Colliders.Num();
			++LimitIndex)
		{
			Node.SphericalLimits[LimitIndex].LimitRadius =
				Rig->Colliders[LimitIndex].Radius * Tuning.ColliderRadiusScale;
		}

		for (FAnimPhysBodyDefinition& Body : Node.PhysicsBodyDefinitions)
		{
			const FElysiumClothBody* Authored = Source.Bodies.FindByPredicate(
				[&Body](const FElysiumClothBody& Candidate)
				{
					return Candidate.Bone == Body.BoundBone.BoneName;
				});
			if (Authored == nullptr)
			{
				continue;
			}
			Body.BoxExtents = FVector(FMath::Max(Authored->BoxExtent * Tuning.BoxExtentScale, 1.0f));
			// The footprint the body collides with, not just its inertia: half a lattice cell, which
			// is what makes ten columns of samples tile a panel instead of sampling it. Baked at
			// `InitPhysics` exactly like the extents it follows, so it re-seats on the same edit.
			Body.SphereCollisionRadius = Body.BoxExtents.X;
			Body.ConstraintSetup.ConeAngle =
				FMath::Clamp(Authored->ConeAngleDeg * Tuning.ConeScale, 0.f, 90.f);
		}
	}
}

bool FAnimNode_ElysiumCloth::HasWork() const
{
	return Rig.IsValid() && Rig->HasWork();
}

void FAnimNode_ElysiumCloth::PreUpdate(const UAnimInstance* Instance)
{
	for (FElysiumClothChainNode& Chain : Chains)
	{
		Chain.PreUpdate(Instance);
	}
}

void FAnimNode_ElysiumCloth::Build(const FBoneContainer& RequiredBones)
{
	Chains.Reset();
	bBuilt = true;
	if (!HasWork())
	{
		return;
	}

	TArray<FAnimPhysSphericalLimit> Limits;
	for (const FElysiumClothCollider& Collider : Rig->Colliders)
	{
		FAnimPhysSphericalLimit Limit;
		Limit.DrivingBone = FBoneReference(Collider.Bone);
		Limit.SphereLocalOffset = Collider.Offset;
		Limit.LimitRadius = Collider.Radius;
		// Outer: the garment is kept OUTSIDE the leg. Inner would trap it inside the thigh, which
		// is the same mistake as no collider at all but harder to recognise.
		Limit.LimitType = ESphericalLimitType::Outer;
		Limits.Add(Limit);
	}

	for (const FElysiumClothChain& Source : Rig->Chains)
	{
		FElysiumClothChainNode Node;
		Node.bChain = true;
		Node.BoundBone = FBoneReference(Source.Root);
		Node.ChainEnd = FBoneReference(Source.End);

		// Component space, not world: the sim origin follows the mesh component, so walking does
		// not drag the whole garment behind the character.
		Node.SimulationSpace = AnimPhysSimSpaceType::Component;

		Node.bUseSphericalLimits = !Limits.IsEmpty();
		Node.SphericalLimits = Limits;
		// On by default in the engine and useless here: a garment is bounded by the legs inside it,
		// not by a plane.
		Node.bUsePlanarLimit = false;

		// Size and name the body array through the engine's own chain walk. Authoring it by hand
		// risks a head/tail mismatch, which `InitPhysics` answers by rebuilding the array and
		// cloning one prototype over every row — the cone ramp would vanish with no error.
		Node.UpdateChainPhysicsBodyDefinitions(RequiredBones.GetReferenceSkeleton());

		for (FAnimPhysBodyDefinition& Body : Node.PhysicsBodyDefinitions)
		{
			// AnimDynamics collides a body as a dimensionless *point* by default — `CoM` is
			// documented as "only limit the center of mass from crossing planes", and
			// `FAnimPhys::ConstrainSphericalOuter` subtracts a body's own footprint only when the
			// type is something else. Left at the default a garment is forty zero-size samples on a
			// ten-column grid and a leg passes cleanly between them: the sim runs, the limits
			// install, the bodies move, nothing reports an error, and the mesh visibly intersects.
			// The radius each body gets is the half-cell `ApplyTuning` already hands it.
			Body.CollisionType = AnimPhysCollisionType::CustomSphere;

			FAnimPhysConstraintSetup& Constraint = Body.ConstraintSetup;
			// A hanging panel should swing the same amount in every direction, which is a cone
			// rather than three independent angular limits.
			Constraint.AngularConstraintType = AnimPhysAngularConstraintType::Cone;
			// Locked linear axes turn the body into a pure pendulum about its joint, which is what
			// keeps the garment attached instead of drifting off the hips.
			Constraint.LinearXLimitType = AnimPhysLinearConstraintType::Limited;
			Constraint.LinearYLimitType = AnimPhysLinearConstraintType::Limited;
			Constraint.LinearZLimitType = AnimPhysLinearConstraintType::Limited;
			Constraint.LinearAxesMin = FVector::ZeroVector;
			Constraint.LinearAxesMax = FVector::ZeroVector;
		}

		Chains.Add(MoveTemp(Node));
	}

	// Every number a slider can move, including the ones this rig authored. A rebuild therefore
	// keeps the live tuning rather than snapping the garment back to the file mid-session.
	ApplyTuning();
}

void FAnimNode_ElysiumCloth::CacheBones(const FAnimationCacheBonesContext& Context)
{
	if (Context.AnimInstanceProxy == nullptr)
	{
		return;
	}
	const FBoneContainer& RequiredBones = Context.AnimInstanceProxy->GetRequiredBones();
	if (!bBuilt)
	{
		Build(RequiredBones);
		// Initialize before the bone resolve, in that order: the base class reinitialises its alpha
		// blends here, and a chain that never sees this keeps whatever the default-constructed node
		// had. Harmless today, but it is the contract the engine's own graph follows.
		FAnimationInitializeContext InitContext(Context.AnimInstanceProxy);
		for (FElysiumClothChainNode& Chain : Chains)
		{
			Chain.Initialize_AnyThread(InitContext);
		}
	}
	for (FElysiumClothChainNode& Chain : Chains)
	{
		Chain.CacheBones_AnyThread(Context);
		// Bodies are built lazily on the next evaluate, against a pose this node cannot see from
		// here. Requesting the reset is what schedules that; without it the chains never simulate.
		Chain.RequestInitialise(ETeleportType::ResetPhysics);
	}
}

void FAnimNode_ElysiumCloth::Update(const FAnimationUpdateContext& Context)
{
	for (FElysiumClothChainNode& Chain : Chains)
	{
		Chain.Update_AnyThread(Context);
	}
}

void FElysiumClothChainNode::ApplyTo(FComponentSpacePoseContext& Output)
{
	const FBoneContainer& RequiredBones = Output.Pose.GetPose().GetBoneContainer();
	if ((Output.AnimInstanceProxy != nullptr && !IsLODEnabled(Output.AnimInstanceProxy))
		|| !IsValidToEvaluate(Output.AnimInstanceProxy->GetSkeleton(), RequiredBones))
	{
		return;
	}

	// Named for what it is, and not `BoneTransforms`: the base class keeps a scratch array of that
	// name for the graph path this node does not take.
	TArray<FBoneTransform> Simulated;
	EvaluateSkeletalControl_AnyThread(Output, Simulated);
	if (!Simulated.IsEmpty())
	{
		Output.Pose.LocalBlendCSBoneTransforms(Simulated, 1.f);
	}
}

void FAnimNode_ElysiumCloth::Apply(FComponentSpacePoseContext& Output)
{
	for (FElysiumClothChainNode& Chain : Chains)
	{
		Chain.ApplyTo(Output);
	}
}
