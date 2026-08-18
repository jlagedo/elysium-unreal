#include "Visual/ElysiumAnimationDriver.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumItemClasses.h"   // Inventory.Active() is read for its classname

void FElysiumAnimationDriver::Reset()
{
	Latch = FElysiumJumpLatch();
	Selection = FElysiumAnimationSelection();
	Assets = FElysiumResolvedAnimation();
	LastActivity.Reset();
	LastStem.Reset();
	LastWeaponClassname.Reset();
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
	// And the reference built from them, for the same reason: a threshold left behind by the previous
	// body would classify the next one against a walk it does not author.
	Gait = FElysiumGaitReference();
}

void FElysiumAnimationDriver::SetTranslationContext(const FElysiumCombatCharacter* Char)
{
	// Written only on change. This runs once per body per frame while the answer moves only on an
	// equip, a possession or a model swap, and the discrete key below already treats a moved value
	// as a re-resolve.
	auto Adopt = [](FString& Field, const FString& Value)
	{
		if (!Field.Equals(Value, ESearchCase::CaseSensitive))
		{
			Field = Value;
		}
	};

	if (Char == nullptr)
	{
		ActorClassname.Reset();
		WeaponClassname.Reset();
		return;
	}

	// The classname a map AUTHORS, which is the key both committed ledgers are joined to. The
	// registered descriptor answers for a character no def produced, which is the player's case.
	if (Char->Def != nullptr)
	{
		Adopt(ActorClassname, Char->Def->Classname);
	}
	else if (Char->Class != nullptr)
	{
		Adopt(ActorClassname, Char->Class->ClassName.ToString());
	}
	else
	{
		ActorClassname.Reset();
	}

	const FElysiumItem* Active = Char->Inventory.Active(*Char);
	if (Active != nullptr && Active->Def != nullptr)
	{
		Adopt(WeaponClassname, Active->Def->Classname);
	}
	else
	{
		WeaponClassname.Reset();
	}
}

bool FElysiumAnimationDriver::RefreshGaitSpeeds(UElysiumAnimSubsystem* Anims,
	const FString& InWeaponClassname, const FString& InFormTag)
{
	if (Anims == nullptr)
	{
		return false;
	}
	FElysiumGaitSpeedRequest Key;
	Key.Stem = Stem;
	Key.WeaponClassname = InWeaponClassname;
	Key.FormTag = InFormTag;
	Key.Variant = Variant;
	Key.SpeedScale = SpeedScale;
	if (GaitGeneration != 0 && Key == GaitKey)
	{
		return false;
	}

	GaitKey = Key;
	Anims->ResolveGaitSpeeds(Key, GaitSpeeds);
	++GaitGeneration;
	// The classifier's reference comes off the same tables the movers command from, so the walk/run
	// threshold and the commanded speed cannot come from two different numbers. It is rebuilt here
	// rather than beside each producer's push, because there is one set of tables and both the player
	// and the cast read the threshold off it.
	Gait = ElysiumAnimIntent::GaitFrom(GaitSpeeds);
	return true;
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
		return Table->SpeedAt(MoveYawDegrees);
	}
	// Not a gait, or a body with no fan: the resolved cell's own authored speed, which is what the
	// record already carries and is zero for a label naming one clip.
	return Selection.GroundSpeedCmPerSecond;
}

void FElysiumAnimationDriver::Tick(float DeltaSeconds, const FElysiumLocomotionSample& Sample,
	UElysiumAnimSubsystem* Anims, USkeletalMesh* Mesh,
	EElysiumOneShotState OneShot)
{
	// The pose parameter is a rate, and this is the one place per body per frame — a producer's
	// sample is a getter a readout may take twice, so it seeds the value and cannot advance it.
	FElysiumLocomotionSample Body = Sample;
	Body.MoveYawPose = ElysiumLocomotion::AdvanceMoveYaw(MoveYawFilter, Sample.MoveYawVelocity,
		Sample.Speed2D(), DeltaSeconds);

	// Only the player chain commands jumps, so only the player chain has air phases to latch. A cast
	// body's mover reports itself airborne for reasons that are never a jump — a mode it has not been
	// given yet, a lift, a frame mid-teleport — and retail answers none of them with an activity: its
	// NPC surface has no ground poll at all, and the air activities that exist are requested by a
	// scripted task. Passing the producer here is what keeps that a stated rule rather than a
	// coincidence of what the movers happen to report.
	const bool bCommandsJumps = Source == EElysiumAnimSource::Player;
	Latch = ElysiumAnimIntent::AdvanceJumpLatch(Latch, Body, DeltaSeconds, Gait, OneShot,
		bCommandsJumps);

	FElysiumAnimationIntent Intent = ElysiumAnimIntent::BuildLocomotionIntent(Body, Latch, Gait,
		Source, Stem, Character, Variant);
	// The translation context, which the classifier has no business knowing: it answers what the body
	// did, and these answer which sequence set realizes it.
	Intent.ActorClassname = ActorClassname;
	Intent.WeaponClassname = WeaponClassname;
	Intent.FormTag = FormTag;

	// The body key, resolved ahead of the request so a body that changed model this frame steers by
	// its new speeds rather than by one frame of its old ones.
	RefreshGaitSpeeds(Anims, Intent.WeaponClassname, Intent.FormTag);

	// The discrete key. Everything else about the intent is continuous and does not re-select
	// anything: a body that turned, sped up or strafed is playing the same request.
	const bool bChanged = !bResolvedOnce
		|| !Intent.Activity.Equals(LastActivity, ESearchCase::IgnoreCase)
		|| !Intent.Stem.Equals(LastStem, ESearchCase::IgnoreCase)
		|| !Intent.WeaponClassname.Equals(LastWeaponClassname, ESearchCase::IgnoreCase)
		|| Intent.Route != LastRoute;

	if (bChanged)
	{
		// The generation advances on the request, not on the frame, so a completed one-shot cannot
		// cancel the request that replaced it.
		++Generation;
		LastActivity = Intent.Activity;
		LastStem = Intent.Stem;
		LastWeaponClassname = Intent.WeaponClassname;
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

	Anims->ResolveAnimation(Intent, Mesh, Selection, Assets);
	// The resolver answers with the one cell it selected; the stride the body will actually travel
	// at is the table's reading at this direction, which is the same number the mover commands.
	Selection.GroundSpeedCmPerSecond = GaitSpeedForSelection(Intent.Body.MoveYaw());
}
