#include "Visual/ElysiumAnimationDriver.h"

void FElysiumAnimationDriver::Reset()
{
	Latch = FElysiumJumpLatch();
	Selection = FElysiumAnimationSelection();
	Assets = FElysiumResolvedAnimation();
	LastActivity.Reset();
	LastStem.Reset();
	LastWeaponTag.Reset();
	LastRoute = EElysiumAnimRoute::Activity;
	bResolvedOnce = false;
}

void FElysiumAnimationDriver::Tick(float DeltaSeconds, const FElysiumLocomotionSample& Sample,
	UElysiumNpcAnimSubsystem* Anims, USkeletalMesh* Mesh, UglTFRuntimeAsset* OwnAsset)
{
	Latch = ElysiumAnimIntent::AdvanceJumpLatch(Latch, Sample, DeltaSeconds, Gait);

	FElysiumAnimationIntent Intent = ElysiumAnimIntent::BuildLocomotionIntent(Sample, Latch, Gait,
		Source, Stem, Character, Variant);

	// The discrete key. Everything else about the intent is continuous and does not re-select
	// anything: a body that turned, sped up or strafed is playing the same request.
	const bool bChanged = !bResolvedOnce
		|| !Intent.Activity.Equals(LastActivity, ESearchCase::IgnoreCase)
		|| !Intent.Stem.Equals(LastStem, ESearchCase::IgnoreCase)
		|| !Intent.WeaponTag.Equals(LastWeaponTag, ESearchCase::IgnoreCase)
		|| Intent.Route != LastRoute;

	if (bChanged)
	{
		// The generation advances on the request, not on the frame, so a completed one-shot cannot
		// cancel the request that replaced it.
		++Generation;
		LastActivity = Intent.Activity;
		LastStem = Intent.Stem;
		LastWeaponTag = Intent.WeaponTag;
		LastRoute = Intent.Route;
		bResolvedOnce = true;
	}
	Intent.Generation = Generation;

	if (!bChanged && Selection.Generation == Generation)
	{
		// Step 6 alone: the graph's continuous parameters, rewritten in place. The label, the owning
		// bank and the resolved assets stay exactly as they were, which is what stops the weighted
		// pick and the asset load repeating every tick.
		Selection.MoveYaw = Intent.Body.MoveYaw();
		Selection.Speed = Intent.Body.Speed2D();
		Selection.AimYaw = Intent.AimYaw;
		Selection.AimPitch = Intent.AimPitch;
		// The latch can move without the activity moving — a landing body that was already walking
		// leaves `Landing` for `Grounded` and keeps its gait — so the phase is continuous state here.
		Selection.AirPhase = Intent.AirPhase;
		for (int32 Axis = 0; Axis < Selection.Axes; ++Axis)
		{
			Selection.AxisValue[Axis] =
				ElysiumAnimResolve::PoseFrom(Intent).Get(Selection.AxisName[Axis]);
		}
		return;
	}

	if (Anims == nullptr)
	{
		// No game instance to read a catalog from. The classifier still ran, so the record says what
		// was asked for and why nothing answered.
		ElysiumAnimResolve::Resolve(Intent, FElysiumAnimationCatalog(), Selection);
		Assets = FElysiumResolvedAnimation();
		return;
	}

	Anims->ResolveAnimation(Intent, Mesh, OwnAsset, Selection, Assets);
}
