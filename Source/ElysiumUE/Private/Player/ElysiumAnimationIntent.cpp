#include "ElysiumAnimationIntent.h"

namespace ElysiumAnimIntent
{

namespace
{
	struct FActivityNaming
	{
		EElysiumAnimActivityCode Code;
		const TCHAR* Name;
	};

	// The whole named vocabulary, and the only place its ACT_* literals are spelled. The knockback
	// rows are the same ten `Substrate/ElysiumReactions.h` selects between; that namespace owns which
	// cell a contact plays, and this table owns the code the record carries it as.
	const FActivityNaming GActivityNames[] =
	{
		{ EElysiumAnimActivityCode::Unknown,     TEXT("") },
		{ EElysiumAnimActivityCode::Idle,        TEXT("ACT_IDLE") },
		{ EElysiumAnimActivityCode::WalkRelaxed, TEXT("ACT_WALK_RELAXED") },
		{ EElysiumAnimActivityCode::Walk,        TEXT("ACT_WALK") },
		{ EElysiumAnimActivityCode::RunRelaxed,  TEXT("ACT_RUN_RELAXED") },
		{ EElysiumAnimActivityCode::Run,         TEXT("ACT_RUN") },
		{ EElysiumAnimActivityCode::Sneak,       TEXT("ACT_SNEAK") },
		{ EElysiumAnimActivityCode::Crouch,      TEXT("ACT_CROUCH") },
		{ EElysiumAnimActivityCode::Leap,        TEXT("ACT_LEAP") },
		{ EElysiumAnimActivityCode::Falling,     TEXT("ACT_FALLING") },
		{ EElysiumAnimActivityCode::Land,        TEXT("ACT_LAND") },
		{ EElysiumAnimActivityCode::LandCrouch,  TEXT("ACT_LAND_CROUCH") },
		{ EElysiumAnimActivityCode::Swim,        TEXT("ACT_SWIM") },
		{ EElysiumAnimActivityCode::Treadwater,  TEXT("ACT_TREADWATER") },
		{ EElysiumAnimActivityCode::DieSimple,   TEXT("ACT_DIESIMPLE") },
		{ EElysiumAnimActivityCode::DieRagdoll,  TEXT("ACT_DIERAGDOLL") },
		{ EElysiumAnimActivityCode::KnockbackSmallHighForward,
			TEXT("ACT_KNOCKBACK_SMALL_HIGH_FORWARD") },
		{ EElysiumAnimActivityCode::KnockbackSmallHighBack,
			TEXT("ACT_KNOCKBACK_SMALL_HIGH_BACK") },
		{ EElysiumAnimActivityCode::KnockbackSmallHighLeft,
			TEXT("ACT_KNOCKBACK_SMALL_HIGH_LEFT") },
		{ EElysiumAnimActivityCode::KnockbackSmallHighRight,
			TEXT("ACT_KNOCKBACK_SMALL_HIGH_RIGHT") },
		{ EElysiumAnimActivityCode::KnockbackNormalHighForward,
			TEXT("ACT_KNOCKBACK_NORMAL_HIGH_FORWARD") },
		{ EElysiumAnimActivityCode::KnockbackNormalHighBack,
			TEXT("ACT_KNOCKBACK_NORMAL_HIGH_BACK") },
		{ EElysiumAnimActivityCode::KnockbackNormalHighLeft,
			TEXT("ACT_KNOCKBACK_NORMAL_HIGH_LEFT") },
		{ EElysiumAnimActivityCode::KnockbackNormalHighRight,
			TEXT("ACT_KNOCKBACK_NORMAL_HIGH_RIGHT") },
		{ EElysiumAnimActivityCode::KnockbackSmallLowBack,
			TEXT("ACT_KNOCKBACK_SMALL_LOW_BACK") },
		{ EElysiumAnimActivityCode::KnockbackNormalLowBack,
			TEXT("ACT_KNOCKBACK_NORMAL_LOW_BACK") },
		{ EElysiumAnimActivityCode::KnockbackFlyingIntoForward,
			TEXT("ACT_KNOCKBACK_FLYING_INTO_FORWARD") },
		{ EElysiumAnimActivityCode::KnockbackFlyingIntoRight,
			TEXT("ACT_KNOCKBACK_FLYING_INTO_RIGHT") },
		{ EElysiumAnimActivityCode::KnockbackFlyingIntoLeft,
			TEXT("ACT_KNOCKBACK_FLYING_INTO_LEFT") },
		{ EElysiumAnimActivityCode::KnockbackFlyingIntoBack,
			TEXT("ACT_KNOCKBACK_FLYING_INTO_BACK") },
		{ EElysiumAnimActivityCode::KnockbackFlyingIdle,
			TEXT("ACT_KNOCKBACK_FLYING_IDLE") },
		{ EElysiumAnimActivityCode::KnockbackFlyingLand,
			TEXT("ACT_KNOCKBACK_FLYING_LAND") },
		{ EElysiumAnimActivityCode::KnockbackFlyingWallHit,
			TEXT("ACT_KNOCKBACK_FLYING_WALL_HIT") },
		{ EElysiumAnimActivityCode::KnockbackFlyingWallFall,
			TEXT("ACT_KNOCKBACK_FLYING_WALL_FALL") },
		{ EElysiumAnimActivityCode::KnockbackFlyingWallLand,
			TEXT("ACT_KNOCKBACK_FLYING_WALL_LAND") },
	};

	// The one-handed roster (`docs/vtmb/animation_and_movers.md` A.4, measured over both melee
	// banks). Every other tag — every firearm/thrown weapon, and the melee `bushhook`/
	// `sledgehammer` — takes the default two-handed mask; the two-handed melee pair is listed
	// explicitly anyway so the "not melee-versus-ranged" rule has a row a test can pin.
	struct FElysiumWeaponGripEntry
	{
		const TCHAR* WeaponTag;
		EElysiumWeaponGrip Grip;
	};

	const FElysiumWeaponGripEntry GWeaponGrips[] =
	{
		{ TEXT("baseballbat"),  EElysiumWeaponGrip::OneHanded },
		{ TEXT("katana"),       EElysiumWeaponGrip::OneHanded },
		{ TEXT("knife"),        EElysiumWeaponGrip::OneHanded },
		{ TEXT("stake"),        EElysiumWeaponGrip::OneHanded },
		{ TEXT("tireiron"),     EElysiumWeaponGrip::OneHanded },
		{ TEXT("bushhook"),     EElysiumWeaponGrip::TwoHanded },
		{ TEXT("sledgehammer"), EElysiumWeaponGrip::TwoHanded },
	};
}

const TCHAR* ActivityName(EElysiumAnimActivityCode Code)
{
	for (const FActivityNaming& Entry : GActivityNames)
	{
		if (Entry.Code == Code)
		{
			return Entry.Name;
		}
	}
	return TEXT("");
}

EElysiumAnimActivityCode ActivityCode(const FString& Name)
{
	if (Name.IsEmpty())
	{
		return EElysiumAnimActivityCode::Unknown;
	}
	for (const FActivityNaming& Entry : GActivityNames)
	{
		if (Entry.Code != EElysiumAnimActivityCode::Unknown
			&& Name.Equals(Entry.Name, ESearchCase::IgnoreCase))
		{
			return Entry.Code;
		}
	}
	// Everything the table does not name — every weapon and scripted activity, and the reaction
	// families no code has been added for — reads Unknown.
	return EElysiumAnimActivityCode::Unknown;
}

FElysiumGaitReference GaitFrom(const FElysiumGaitSpeeds& Speeds)
{
	FElysiumGaitReference Gait;
	// The forward cells, because that is what the two gaits are *called* — a body's walk speed is
	// the speed it walks forward at, and the strafe cells are the same gait pointed sideways.
	if (Speeds.Walk.IsValid())
	{
		Gait.WalkSpeedCmPerSecond = Speeds.Walk.Forward();
		// Retail's threshold: the body's own forward walk cell plus one Source unit. Deriving it from
		// the same table the mover commands from is what stops the classifier calling a body a runner
		// at a speed its run fan cannot produce.
		Gait.RunSplitAbsoluteCmPerSecond = Speeds.Walk.Forward() + ElysiumMove::U;
	}
	if (Speeds.Run.IsValid())
	{
		Gait.RunSpeedCmPerSecond = Speeds.Run.Forward();
	}
	return Gait;
}

const TCHAR* SourceName(EElysiumAnimSource Source)
{
	switch (Source)
	{
	case EElysiumAnimSource::Npc:         return TEXT("npc");
	case EElysiumAnimSource::Scene:       return TEXT("scene");
	case EElysiumAnimSource::Damage:      return TEXT("damage");
	case EElysiumAnimSource::Interaction: return TEXT("interaction");
	case EElysiumAnimSource::Debug:       return TEXT("debug");
	default:                              return TEXT("player");
	}
}

const TCHAR* BodyKindName(EElysiumAnimBodyKind BodyKind)
{
	switch (BodyKind)
	{
	case EElysiumAnimBodyKind::Cast: return TEXT("cast");
	default:                         return TEXT("player");
	}
}

const TCHAR* ChannelName(EElysiumAnimChannel Channel)
{
	switch (Channel)
	{
	case EElysiumAnimChannel::FullBody:  return TEXT("full body");
	case EElysiumAnimChannel::UpperBody: return TEXT("upper body");
	case EElysiumAnimChannel::Additive:  return TEXT("additive");
	case EElysiumAnimChannel::Gesture:   return TEXT("gesture");
	default:                             return TEXT("base");
	}
}

const TCHAR* RouteName(EElysiumAnimRoute Route)
{
	switch (Route)
	{
	case EElysiumAnimRoute::ExactLabel:   return TEXT("exact label");
	case EElysiumAnimRoute::SetAnimation: return TEXT("set animation");
	case EElysiumAnimRoute::Gesture:      return TEXT("gesture");
	default:                              return TEXT("activity");
	}
}

const TCHAR* AssetKindName(EElysiumAnimAssetKind Kind)
{
	switch (Kind)
	{
	case EElysiumAnimAssetKind::Sequence:   return TEXT("sequence");
	case EElysiumAnimAssetKind::BlendSpace: return TEXT("blend space");
	case EElysiumAnimAssetKind::Layer:      return TEXT("layer");
	default:                                return TEXT("none");
	}
}

const TCHAR* OutcomeName(EElysiumAnimOutcome Outcome)
{
	switch (Outcome)
	{
	case EElysiumAnimOutcome::Resolved:                return TEXT("resolved");
	case EElysiumAnimOutcome::TranslatedFallback:      return TEXT("translated fallback");
	case EElysiumAnimOutcome::RunToWalk:               return TEXT("run -> walk");
	case EElysiumAnimOutcome::Disposition:             return TEXT("disposition");
	case EElysiumAnimOutcome::SequenceZero:            return TEXT("sequence 0");
	case EElysiumAnimOutcome::ScriptedSequenceZero:    return TEXT("scripted sequence 0");
	case EElysiumAnimOutcome::GestureNoOp:             return TEXT("gesture no-op");
	case EElysiumAnimOutcome::MissingSequence:         return TEXT("missing sequence");
	case EElysiumAnimOutcome::MaskedRejected:          return TEXT("additive, refused");
	case EElysiumAnimOutcome::LayerMaskRejected:       return TEXT("masked layer, refused");
	case EElysiumAnimOutcome::GridStateRefused:        return TEXT("grid, sequence-only state");
	case EElysiumAnimOutcome::NoAsset:                 return TEXT("no asset");
	default:                                           return TEXT("no vocabulary");
	}
}

const TCHAR* AirPhaseName(EElysiumAirPhase Phase)
{
	switch (Phase)
	{
	case EElysiumAirPhase::Leap:    return TEXT("leap");
	case EElysiumAirPhase::Falling: return TEXT("falling");
	case EElysiumAirPhase::Landing: return TEXT("landing");
	default:                        return TEXT("grounded");
	}
}

const TCHAR* PriorityName(EElysiumAnimPriority Priority)
{
	switch (Priority)
	{
	case EElysiumAnimPriority::Ambient:          return TEXT("ambient");
	case EElysiumAnimPriority::LocomotionTravel: return TEXT("locomotion travel");
	case EElysiumAnimPriority::Scripted:         return TEXT("scripted");
	case EElysiumAnimPriority::Reaction:         return TEXT("reaction");
	case EElysiumAnimPriority::Scene:            return TEXT("scene");
	case EElysiumAnimPriority::Debug:            return TEXT("debug");
	default:                                     return TEXT("locomotion idle");
	}
}

const TCHAR* ReactionReleaseName(EElysiumReactionRelease Release)
{
	switch (Release)
	{
	case EElysiumReactionRelease::Envelope:  return TEXT("envelope");
	case EElysiumReactionRelease::Predicate: return TEXT("predicate");
	default:                                 return TEXT("clip");
	}
}

const TCHAR* HeldReactionStateName(EElysiumHeldReactionState State)
{
	switch (State)
	{
	case EElysiumHeldReactionState::Free:      return TEXT("free");
	case EElysiumHeldReactionState::Displaced: return TEXT("displaced");
	default:                                   return TEXT("held");
	}
}

EElysiumAnimPriority DefaultPriority(EElysiumAnimSource Source)
{
	switch (Source)
	{
	// The scheduled cast's stances and fidgets — hold against a standing publish, yield to travel.
	case EElysiumAnimSource::Npc:         return EElysiumAnimPriority::Ambient;
	case EElysiumAnimSource::Scene:       return EElysiumAnimPriority::Scene;
	case EElysiumAnimSource::Damage:      return EElysiumAnimPriority::Reaction;
	case EElysiumAnimSource::Interaction: return EElysiumAnimPriority::Scripted;
	case EElysiumAnimSource::Debug:       return EElysiumAnimPriority::Debug;
	// A player action request is a gameplay commitment, not an ambient decoration.
	default:                              return EElysiumAnimPriority::Scripted;
	}
}

EElysiumAnimPriority LocomotionPriority(EElysiumGraphState State)
{
	// Idle is the floor; every other projected state — a gait, a jump phase, a landing — is a body
	// actually doing something, which is exactly the line the interim rule drew and the table keeps.
	return State == EElysiumGraphState::Idle
		? EElysiumAnimPriority::LocomotionIdle
		: EElysiumAnimPriority::LocomotionTravel;
}

EElysiumWeaponGrip WeaponGrip(const FString& WeaponTag)
{
	for (const FElysiumWeaponGripEntry& Entry : GWeaponGrips)
	{
		if (WeaponTag.Equals(Entry.WeaponTag, ESearchCase::IgnoreCase))
		{
			return Entry.Grip;
		}
	}
	return EElysiumWeaponGrip::TwoHanded;
}

FElysiumJumpLatch AdvanceJumpLatch(const FElysiumJumpLatch& Prev,
	const FElysiumLocomotionSample& Sample, float DeltaSeconds, const FElysiumGaitReference& Gait,
	EElysiumOneShotState OneShot, bool bCommandsJumps)
{
	FElysiumJumpLatch Next = Prev;
	Next.PhaseSeconds = Prev.PhaseSeconds + FMath::Max(0.0f, DeltaSeconds);

	// The jump push window's RISING edge is the press. It is the only signal that separates a jump
	// from a fall, and it is the reason the latch exists at all.
	const bool bHolding = Sample.JumpHoldRemaining > 0.0f;
	const bool bPressEdge = bHolding && !Prev.bWasHolding;

	// A producer with no jump command has no air phase to latch. The transition table below is the
	// player chain's entirely — retail reads its jump phase off a `CBasePlayer` field, and the cast's
	// air activities are requested outright by scripted tasks rather than inferred from the floor —
	// so a body that only ever walks stays grounded however its mover reports itself. Held rather
	// than returned early, because the gait memory below this switch still has to advance:
	// `bLastGaitWasRun` is what `Classify` reads for walk-versus-run, and skipping it would stop the
	// whole cast ever running.
	if (!bCommandsJumps)
	{
		Next.Phase = EElysiumAirPhase::Grounded;
	}
	else switch (Prev.Phase)
	{
	case EElysiumAirPhase::Grounded:
		if (bPressEdge)
		{
			// Retail selects ACT_LEAP on the press, while the body is still nominally grounded.
			Next.Phase = EElysiumAirPhase::Leap;
		}
		else if (!Sample.bOnGround)
		{
			// Walking off a ledge is not a jump, and the ordinary chain visits ACT_LEAP only on a
			// press. This is also the whole air path for every body whose movement component has no
			// hold window, which is the whole cast.
			Next.Phase = EElysiumAirPhase::Falling;
		}
		break;

	case EElysiumAirPhase::Leap:
		if (!Sample.bOnGround)
		{
			// Airborne: hold ACT_LEAP while the push is still doing work or the body is still rising.
			if (Sample.JumpPhase() != EElysiumJumpPhase::Ascend)
			{
				Next.Phase = EElysiumAirPhase::Falling;
			}
		}
		else if (!bHolding)
		{
			// Grounded with the push finished. Which answer depends on whether the body ever left:
			// a jump that came back down lands, a jump that never lifted (a low ceiling, a lost
			// footing) simply returns to the gait.
			Next.Phase = Prev.bWasOnGround
				? EElysiumAirPhase::Grounded : EElysiumAirPhase::Landing;
		}
		break;

	case EElysiumAirPhase::Falling:
		if (Sample.bOnGround)
		{
			Next.Phase = EElysiumAirPhase::Landing;
		}
		break;

	case EElysiumAirPhase::Landing:
		if (!Sample.bOnGround)
		{
			Next.Phase = EElysiumAirPhase::Falling;
		}
		// A moving landing never waits on a clip: retail's phase 8 takes the gait outright when the
		// body is moving (`docs/vtmb/animation_and_movers.md` — "landing phase 8 while moving" ->
		// ACT_WALK_RELAXED), so this outranks anything the pose layer has to say.
		else if (Sample.Speed2D() > Gait.StillSpeed())
		{
			Next.Phase = EElysiumAirPhase::Grounded;
		}
		// Still and grounded: the landing ends when its clip does. `Playing` is an answer and it
		// keeps the body here — only the absence of an answer falls back to the stopwatch, which is
		// what a body with no pose layer gets.
		else if (OneShot == EElysiumOneShotState::Complete
			|| (OneShot == EElysiumOneShotState::Unknown
				&& Next.PhaseSeconds >= Prev.LandHoldSeconds))
		{
			Next.Phase = EElysiumAirPhase::Grounded;
		}
		break;
	}

	if (Next.Phase != Prev.Phase)
	{
		Next.PhaseSeconds = 0.0f;
	}

	// The gait, settled here so `Classify` stays a pure function of the state it is handed.
	//
	// **Both speeds, disjunctively** — retail tests the realized speed *or* the commanded one against
	// the same threshold. The commanded term dominates in practice: it is already at full value on
	// the first frame of a full input, so the run is selected immediately rather than after the body
	// has accelerated into it, which is the ramp a realized-speed-only test produces and retail does
	// not have. The realized term is what decides while coasting with the command released.
	//
	// The margin is a divergence and defaults to zero — retail holds no gait memory. It survives as a
	// dial because the commanded term is what removed the flicker it existed for, and that reasoning
	// should be falsifiable rather than assumed.
	const float Speed = FMath::Max(Sample.Speed2D(), Sample.CommandedSpeed);
	const float Split = Gait.RunSplitSpeed();
	const float Margin = Gait.HysteresisSpeed();
	Next.bLastGaitWasRun = Prev.bLastGaitWasRun
		? (Speed >= Split - Margin)
		: (Speed > Split + Margin);

	Next.bWasOnGround = Sample.bOnGround;
	Next.bWasHolding = bHolding;
	return Next;
}

// **The live selector split (LIFE4, Option A), stated where both artifacts can cite it:** this
// classifier is the live selector for the CAST and for the player's water and air phases. The
// player's grounded stand/gait is NOT selected here — the player producer walks the committed
// retail gait ladder (`ElysiumActionTables::PlayerGaitLadder()`, applied by
// `FElysiumAnimationDriver::SelectPlayerGroundActivity`) with a live combat-stance query, and
// overrides the grounded code this function returns. Two selectors, one owner each — neither is
// a reference copy of the other.
EElysiumAnimActivityCode Classify(const FElysiumLocomotionSample& Sample,
	const FElysiumJumpLatch& Latch, const FElysiumGaitReference& Gait)
{
	const float Speed = Sample.Speed2D();
	const bool bMoving = Speed > Gait.StillSpeed();

	// Water first (the ordinary selector's compact code 9). The move itself branches at Waist, so the
	// classification does too; without this branch a swimming body reads as a running one.
	if (Sample.Water >= EElysiumWaterLevel::Waist)
	{
		return bMoving ? EElysiumAnimActivityCode::Swim : EElysiumAnimActivityCode::Treadwater;
	}

	// The air phases come from the latch and never from the sample: `JumpPhase()` cannot tell a
	// descent from walking off a ledge, which is the gap this closes.
	switch (Latch.Phase)
	{
	case EElysiumAirPhase::Leap:
		return EElysiumAnimActivityCode::Leap;
	case EElysiumAirPhase::Falling:
		return EElysiumAnimActivityCode::Falling;
	case EElysiumAirPhase::Landing:
		// The recovered phase-8 three-way: a moving body takes its gait, a still one lands, and a
		// ducked one asks for the crouched land. A moving body therefore falls through to the
		// grounded branches below rather than answering here.
		if (!bMoving)
		{
			return Sample.Stance == EElysiumStance::Standing
				? EElysiumAnimActivityCode::Land : EElysiumAnimActivityCode::LandCrouch;
		}
		break;
	default:
		break;
	}

	// Ducked. All three non-standing values take one branch — the two ramps are the graph's concern,
	// not the classifier's, and a body held in `Rising` under a low ceiling is as ducked as one that
	// settled.
	//
	// **Ducked-and-moving reads as ACT_SNEAK, which is the recovered behaviour** — retail's ladder is
	// `FL_DUCKING && speed2D > 5.0 u/s` for sneak and `ACT_CROUCH` below it, a flat two-state branch
	// taken ahead of the walk/run split so a ducked body never reaches either gait or their relaxed
	// variants (`docs/vtmb/animation_and_movers.md` -> "The gait ladder runs ahead of the
	// compact-code dispatch"). Ours matches that shape: one branch, no split, no relaxed form.
	if (Sample.Stance != EElysiumStance::Standing)
	{
		return bMoving ? EElysiumAnimActivityCode::Sneak : EElysiumAnimActivityCode::Crouch;
	}

	if (!bMoving)
	{
		return EElysiumAnimActivityCode::Idle;
	}

	// The relaxed forms, which is what retail's classifier emits: translation is what turns them into
	// the activities a body actually carries sequences for.
	return Latch.bLastGaitWasRun
		? EElysiumAnimActivityCode::RunRelaxed : EElysiumAnimActivityCode::WalkRelaxed;
}

FElysiumAnimationIntent BuildLocomotionIntent(const FElysiumLocomotionSample& Sample,
	const FElysiumJumpLatch& Latch, const FElysiumGaitReference& Gait,
	EElysiumAnimSource Source, EElysiumAnimBodyKind BodyKind, const FString& Stem,
	const FElysiumEntityHandle& Character, int32 Variant)
{
	EElysiumAnimActivityCode Code = Classify(Sample, Latch, Gait);

	// **The relaxed gait forms belong to the player's own selector.** Retail's cast has no
	// locomotion classifier: a schedule task requests `ACT_WALK` or `ACT_RUN` outright, and the two
	// rows that turn a relaxed request back into a plain one are `CBasePlayer::NPC_TranslateActivity`
	// — a player virtual no cast body reaches. A cast body therefore asks for the plain activity,
	// because nothing downstream of it would ever undo the relaxed form. It is the body's chain that
	// decides this, not the producer that asked.
	if (BodyKind == EElysiumAnimBodyKind::Cast)
	{
		if (Code == EElysiumAnimActivityCode::WalkRelaxed)
		{
			Code = EElysiumAnimActivityCode::Walk;
		}
		else if (Code == EElysiumAnimActivityCode::RunRelaxed)
		{
			Code = EElysiumAnimActivityCode::Run;
		}
	}

	FElysiumAnimationIntent Out;
	Out.Character = Character;
	Out.Stem = Stem;
	Out.Source = Source;
	Out.BodyKind = BodyKind;
	Out.Channel = EElysiumAnimChannel::Base;
	Out.Activity = ActivityName(Code);
	Out.Route = EElysiumAnimRoute::Activity;
	Out.Variant = Variant;
	Out.Body = Sample;
	Out.AirPhase = Latch.Phase;
	Out.CompletionOwner = Source;

	// **`aim_pitch`, the player's half of an aim grid.** Retail's player selector takes the pitch
	// from a field and writes `aim_yaw` as a literal 0, because the body's yaw already follows the
	// view — so looking up and down is the only axis a player steers by looking, and `AimYaw` stays
	// where the cast's own producer puts it.
	//
	// **Negated, because the grid's pitch axis is positive-DOWN and Unreal's is positive-UP.** The
	// grid states it itself: the cells at the low end of its -45..45 axis are `<weapon>_aim_UC`, up
	// at the low end. That is the convention the pose parameter was authored in, and it survives
	// into the baked blend space because the sample positions are the authored parameter values.
	// Converted HERE rather than at the asset, on the same reasoning as axis interpolation: this
	// binds a LIVE view orientation to a parameter rather than reinterpreting a stored coordinate.
	//
	// **The clamp is the animation's, not the shot's.** Every shipped aim grid spans -45..45, so a
	// steeper look would saturate at a cell the grid does not have. The bullet leaves along the
	// unclamped view (`AElysiumMapActor::QueryAimTarget`), so clamping here bounds how far the body
	// leans and nothing else. A body whose producer states no view pitch — every cast body — leaves
	// this at zero, the centre column its grids are authored around.
	Out.AimPitch = FMath::Clamp(-FRotator::NormalizeAxis(Sample.ViewPitch), -45.0f, 45.0f);

	// The request's own loop intent, which is not the clip's looping flag: the leap and the two lands
	// are one-shots the body plays through, everything else is a state it holds. A held crouch holds
	// even though `crouch` is a 61-frame non-looping "into" pose — what the graph does at the end of
	// that pose is `CCC5`'s provisional answer, not this record's.
	Out.bLoop = Code != EElysiumAnimActivityCode::Leap
		&& Code != EElysiumAnimActivityCode::Land
		&& Code != EElysiumAnimActivityCode::LandCrouch;

	return Out;
}

} // namespace ElysiumAnimIntent
