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
	// A teleport is not a turn: slewing the stride across one would rotate the body's gait over a
	// quarter second of standing somewhere else entirely.
	MoveYawFilter = FElysiumMoveYawFilter();

	// The tables go too. A reset is a teleport or a map epoch, and a driver reused across a model
	// swap would otherwise steer the mover with the previous body's authored speeds — which is the
	// one failure mode of pushing rather than pulling, and it costs one re-resolve to close.
	GaitKey = FElysiumGaitSpeedRequest();
	GaitSpeeds = FElysiumGaitSpeeds();
	GaitGeneration = 0;
}

float FElysiumAnimationDriver::GaitSpeedForSelection(float MoveYawDegrees) const
{
	const FElysiumGaitSpeedTable* Table = nullptr;
	switch (ElysiumAnimIntent::ActivityCode(Selection.ResolvedActivity))
	{
	case EElysiumAnimActivityCode::Walk:  Table = &GaitSpeeds.Walk;  break;
	case EElysiumAnimActivityCode::Run:   Table = &GaitSpeeds.Run;   break;
	case EElysiumAnimActivityCode::Sneak: Table = &GaitSpeeds.Sneak; break;
	default: break;
	}
	if (Table != nullptr && Table->IsValid())
	{
		return Table->SpeedAt(MoveYawDegrees, bInterpolateGaitSpeed);
	}
	// Not a gait, or a body with no fan: the resolved cell's own authored speed, which is what the
	// record already carries and is zero for a label naming one clip.
	return Selection.GroundSpeedCmPerSecond;
}

void FElysiumAnimationDriver::Tick(float DeltaSeconds, const FElysiumLocomotionSample& Sample,
	UElysiumNpcAnimSubsystem* Anims, USkeletalMesh* Mesh, UglTFRuntimeAsset* OwnAsset,
	EElysiumOneShotState OneShot)
{
	// The pose parameter is a rate, and this is the one place per body per frame — a producer's
	// sample is a getter a readout may take twice, so it seeds the value and cannot advance it.
	FElysiumLocomotionSample Body = Sample;
	Body.MoveYawPose = ElysiumLocomotion::AdvanceMoveYaw(MoveYawFilter, Sample.MoveYawVelocity,
		Sample.Speed2D(), DeltaSeconds);

	Latch = ElysiumAnimIntent::AdvanceJumpLatch(Latch, Body, DeltaSeconds, Gait, OneShot);

	FElysiumAnimationIntent Intent = ElysiumAnimIntent::BuildLocomotionIntent(Body, Latch, Gait,
		Source, Stem, Character, Variant);

	// The body key, resolved ahead of the request so a body that changed model this frame steers by
	// its new speeds rather than by one frame of its old ones.
	if (Anims != nullptr)
	{
		FElysiumGaitSpeedRequest Key;
		Key.Stem = Intent.Stem;
		Key.WeaponTag = Intent.WeaponTag;
		Key.FormTag = Intent.FormTag;
		Key.Variant = Variant;
		Key.SpeedScale = SpeedScale;
		if (GaitGeneration == 0 || Key != GaitKey)
		{
			GaitKey = Key;
			Anims->ResolveGaitSpeeds(Key, GaitSpeeds);
			++GaitGeneration;
		}
	}

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
		// The stride, refreshed with the direction. Left alone it reports the speed of whichever way
		// the body happened to be going when the activity last changed, so a body that turned while
		// walking would keep quoting its old cell — and `act_stride` is this rung's readback.
		Selection.GroundSpeedCmPerSecond = GaitSpeedForSelection(Intent.Body.MoveYaw());
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
	// The resolver answers with the one cell it selected; the stride the body will actually travel
	// at is the table's reading at this direction, which is the same number the mover commands.
	Selection.GroundSpeedCmPerSecond = GaitSpeedForSelection(Intent.Body.MoveYaw());
}
