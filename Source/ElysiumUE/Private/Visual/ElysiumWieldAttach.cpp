#include "ElysiumWieldAttach.h"

#include "ReferenceSkeleton.h"

namespace ElysiumWieldAttach
{
	FTransform RefPoseComponentSpace(const FReferenceSkeleton& Ref, int32 BoneIndex)
	{
		if (!Ref.GetRefBonePose().IsValidIndex(BoneIndex))
		{
			return FTransform::Identity;
		}
		FTransform Out = Ref.GetRefBonePose()[BoneIndex];
		for (int32 Index = Ref.GetParentIndex(BoneIndex); Index != INDEX_NONE;
			Index = Ref.GetParentIndex(Index))
		{
			Out = Out * Ref.GetRefBonePose()[Index];
		}
		return Out;
	}

	FName NearestSharedAncestor(const FReferenceSkeleton& WieldRef,
		const FReferenceSkeleton& WearerRef, FName BoneName)
	{
		int32 Index = WieldRef.FindBoneIndex(BoneName);
		while (Index != INDEX_NONE)
		{
			const FName Name = WieldRef.GetBoneName(Index);
			if (WearerRef.FindBoneIndex(Name) != INDEX_NONE)
			{
				return Name;
			}
			Index = WieldRef.GetParentIndex(Index);
		}
		return NAME_None;
	}

	namespace
	{
		// One socket rule, twice. Both bindings differ only in which bone they anchor on, and the
		// arithmetic is the same: bring the model's own copy of that bone onto the wearer's.
		FPlan Socket(const FReferenceSkeleton& WieldRef, const FReferenceSkeleton& WearerRef,
			FName Anchor, const TCHAR* Which)
		{
			FPlan Plan;
			if (Anchor.IsNone())
			{
				Plan.Reason = FString::Printf(
					TEXT("a %s binding names no %s bone, so there is nothing to anchor on"),
					Which, Which);
				return Plan;
			}
			const int32 OnModel = WieldRef.FindBoneIndex(Anchor);
			if (OnModel == INDEX_NONE)
			{
				Plan.Reason = FString::Printf(
					TEXT("the model carries no '%s' to anchor by"), *Anchor.ToString());
				return Plan;
			}
			if (WearerRef.FindBoneIndex(Anchor) == INDEX_NONE)
			{
				// The wearer is the other half of the coincidence; without the bone there is no
				// transform to attach to, and attaching to the component root instead would put the
				// weapon at the wearer's feet.
				Plan.Reason = FString::Printf(
					TEXT("the wearer carries no '%s' to anchor on"), *Anchor.ToString());
				return Plan;
			}
			Plan.Placement = EPlacement::RigidToBone;
			Plan.AnchorBone = Anchor;
			Plan.Relative = RefPoseComponentSpace(WieldRef, OnModel).Inverse();
			return Plan;
		}
	}

	FPlan Resolve(EElysiumWieldBinding Binding, const FReferenceSkeleton& WieldRef,
		const FReferenceSkeleton& WearerRef, FName MountBone, FName HandBone)
	{
		FPlan Plan;
		if (WieldRef.GetNum() == 0 || WearerRef.GetNum() == 0)
		{
			Plan.Reason = TEXT("one of the two skeletons is empty");
			return Plan;
		}

		switch (Binding)
		{
		case EElysiumWieldBinding::SocketHand:
			// The hand is the shared bone for a carry whose mount is the model's own grip box, which
			// no wearer carries. Anchoring on the mount would refuse; anchoring on the hand places
			// the mount exactly where the model's own bind put it relative to that hand.
			return Socket(WieldRef, WearerRef, HandBone, TEXT("hand"));

		case EElysiumWieldBinding::SocketProp:
		{
			// A prop carry names a bone the wearer declares for it -- `handle`, `Bat`,
			// `Sledgehammer` -- so the mount IS the shared bone.
			FPlan Prop = Socket(WieldRef, WearerRef, MountBone, TEXT("prop"));
			if (Prop.IsWorn())
			{
				return Prop;
			}
			// **Not every wearer declares every prop bone.** The female player skeleton carries no
			// `bush hook`, so its row has no prop bone to anchor on -- and the manifest already
			// answers what to do, because `hand_bone` is exactly "the nearest matched ancestor an
			// unmatched mount runs FK off". Anchoring there places the model's own prop bone where
			// the model itself put it relative to that hand, which is the same arithmetic and the
			// same rigidity.
			FPlan Hand = Socket(WieldRef, WearerRef, HandBone, TEXT("hand"));
			if (!Hand.IsWorn())
			{
				// Both routes gone. The prop reason is the one that names what the manifest asked
				// for, so it is the one reported.
				return Prop;
			}
			Hand.bAnchoredOnHandInstead = true;
			Hand.Reason = FString::Printf(
				TEXT("the wearer carries no '%s', so the carry anchors on '%s' instead"),
				*MountBone.ToString(), *HandBone.ToString());
			return Hand;
		}

		case EElysiumWieldBinding::LeaderPose:
		case EElysiumWieldBinding::CopyPose:
			// Two names for one mechanism: the wearer's matched bones drive the model's, and the
			// model's unmatched bones run FK off their nearest matched ancestor. Kept apart in the
			// enum because the manifest distinguishes them and a later rung may too.
			Plan.Placement = EPlacement::LeaderPose;
			return Plan;

		case EElysiumWieldBinding::Projectile:
			Plan.Reason = TEXT("a projectile is a free-standing actor and is never worn");
			return Plan;

		case EElysiumWieldBinding::None:
		default:
			Plan.Reason = TEXT("the row carries no geometry");
			return Plan;
		}
	}
}
