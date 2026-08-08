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

	// The slice's whole vocabulary, and the only place its ACT_* literals are spelled.
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
	};

	// The actor/form table. Both rows are `required`: the relaxed forms are what the classifier
	// emits and a player body carries no sequence for either, so a miss on the override is a broken
	// catalog rather than a reason to fall back.
	const FElysiumActivityTranslation GActorTranslations[] =
	{
		{ TEXT("ACT_WALK_RELAXED"), TEXT("ACT_WALK"), true },
		{ TEXT("ACT_RUN_RELAXED"),  TEXT("ACT_RUN"),  true },
	};

	const FElysiumActivityTranslation* FindRow(TArrayView<const FElysiumActivityTranslation> Table,
		const FString& From)
	{
		for (const FElysiumActivityTranslation& Row : Table)
		{
			if (From.Equals(Row.From, ESearchCase::IgnoreCase))
			{
				return &Row;
			}
		}
		return nullptr;
	}
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
	// Everything outside the slice — every reaction, weapon and scripted activity — reads Unknown.
	return EElysiumAnimActivityCode::Unknown;
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
	case EElysiumAnimOutcome::RequiredOverrideMissing: return TEXT("required override missing");
	case EElysiumAnimOutcome::MaskedRejected:          return TEXT("masked, refused");
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

TArrayView<const FElysiumActivityTranslation> ActorTranslations()
{
	return MakeArrayView(GActorTranslations);
}

TArrayView<const FElysiumActivityTranslation> WeaponTranslations(const FString& WeaponTag)
{
	// No weapon exists to translate through yet, and an unarmed body's table is empty in retail too.
	// The pass below still runs over this view and terminates on its own stop condition, which is why
	// the seam is exercised rather than skipped.
	(void)WeaponTag;
	return TArrayView<const FElysiumActivityTranslation>();
}

FElysiumTranslationResult TranslateActivity(const FString& Activity, const FString& WeaponTag,
	const FString& FormTag)
{
	// The form half of the actor table has no recovered rows; the tag rides through so the seam takes
	// it rather than growing a second parameter later.
	(void)FormTag;

	FElysiumTranslationResult Out;
	Out.Incoming = Activity;
	FString Current = Activity;

	// Weapon first — `Weapon_TranslateActivity` at virtual +0x5f4, the pinned player order.
	if (const FElysiumActivityTranslation* Row = FindRow(WeaponTranslations(WeaponTag), Current))
	{
		Out.Incoming = Current;
		Current = Row->To;
		Out.bRequired = Row->bRequired;
		++Out.Iterations;
	}
	// Retained whether or not a row applied: retail keeps the first weapon answer separately from the
	// last, and with an empty table the two agree by construction rather than by accident.
	Out.FirstWeaponActivity = Current;
	Out.WeaponActivity = Current;

	// Then the actor/form table — `CBasePlayer::NPC_TranslateActivity` at virtual +0x5e0.
	if (const FElysiumActivityTranslation* Row = FindRow(ActorTranslations(), Current))
	{
		Out.Incoming = Current;
		Current = Row->To;
		Out.bRequired = Row->bRequired;
		++Out.Iterations;
	}

	Out.Resolved = Current;
	return Out;
}

FElysiumJumpLatch AdvanceJumpLatch(const FElysiumJumpLatch& Prev,
	const FElysiumLocomotionSample& Sample, float DeltaSeconds, const FElysiumGaitReference& Gait)
{
	FElysiumJumpLatch Next = Prev;
	Next.PhaseSeconds = Prev.PhaseSeconds + FMath::Max(0.0f, DeltaSeconds);

	// The jump push window's RISING edge is the press. It is the only signal that separates a jump
	// from a fall, and it is the reason the latch exists at all.
	const bool bHolding = Sample.JumpHoldRemaining > 0.0f;
	const bool bPressEdge = bHolding && !Prev.bWasHolding;

	switch (Prev.Phase)
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
			// hold window — the cast, and the `elysium.SourceMovement 0` capsule.
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
		else if (Sample.Speed2D() > Gait.StillSpeed()
			|| Next.PhaseSeconds >= Prev.LandHoldSeconds)
		{
			Next.Phase = EElysiumAirPhase::Grounded;
		}
		break;
	}

	if (Next.Phase != Prev.Phase)
	{
		Next.PhaseSeconds = 0.0f;
	}

	// The gait memory, settled here so `Classify` stays a pure function of the state it is handed.
	// Crossing the split by the margin is what stops a decelerating body flickering between the two
	// gaits and advancing the request generation on every frame.
	const float Speed = Sample.Speed2D();
	const float Split = Gait.RunSplitSpeed();
	const float Margin = Gait.HysteresisSpeed();
	Next.bLastGaitWasRun = Prev.bLastGaitWasRun
		? (Speed >= Split - Margin)
		: (Speed > Split + Margin);

	Next.bWasOnGround = Sample.bOnGround;
	Next.bWasHolding = bHolding;
	return Next;
}

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
	// **Ducked-and-moving reads as ACT_SNEAK, and that is a reconstruction rather than a recovered
	// fact.** The ordinary selector reaches sneak inside compact code 1 ("walk, run, sneak or their
	// relaxed variants, selected from realized speed, flags and weapon state") and those flags are
	// undecoded; there is no sneak button in `FElysiumUserCmd`. What supports it is the stride: the
	// authored `sneak` cells run 69.7-79.3 cm/s and a ducked gait here is a third of the base speed,
	// the same band. Recorded as a divergence in `docs/architecture/input-architecture.md` terms —
	// an owner call, not an accident.
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
	EElysiumAnimSource Source, const FString& Stem, const FElysiumEntityHandle& Character,
	int32 Variant)
{
	const EElysiumAnimActivityCode Code = Classify(Sample, Latch, Gait);

	FElysiumAnimationIntent Out;
	Out.Character = Character;
	Out.Stem = Stem;
	Out.Source = Source;
	Out.Channel = EElysiumAnimChannel::Base;
	Out.Activity = ActivityName(Code);
	Out.Route = EElysiumAnimRoute::Activity;
	Out.Variant = Variant;
	Out.Body = Sample;
	Out.AirPhase = Latch.Phase;
	Out.CompletionOwner = Source;

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
