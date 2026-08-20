#include "Visual/ElysiumAnimGraph.h"

namespace ElysiumAnimGraph
{
	float TransitionSeconds(const FElysiumAnimationSelection* Outgoing,
		const FElysiumAnimationSelection& Incoming)
	{
		// One gate with two operands, because retail evaluates them together: `FUN_1008de30` opens at
		// `0x1008de4d` with `if (!out || !in || (in->flags & 0x2)) return 0.0f;`, ranking a missing
		// outgoing descriptor WITH the hard cut rather than treating it as a fade of its own
		// (`docs/vtmb/animation_and_movers.md` A.4c).
		if (Incoming.bSnap || Outgoing == nullptr)
		{
			return 0.0f;
		}
		return FMath::Max(FMath::Max(0.0f, Incoming.FadeSeconds),
			FMath::Max(0.0f, Outgoing->FadeSeconds));
	}

	EElysiumGraphState StateForActivity(EElysiumAnimActivityCode Code)
	{
		switch (Code)
		{
		case EElysiumAnimActivityCode::Walk:
		case EElysiumAnimActivityCode::WalkRelaxed:
			return EElysiumGraphState::Walk;
		case EElysiumAnimActivityCode::Run:
		case EElysiumAnimActivityCode::RunRelaxed:
			return EElysiumGraphState::Run;
		case EElysiumAnimActivityCode::Sneak:
			return EElysiumGraphState::Sneak;
		case EElysiumAnimActivityCode::Crouch:
			return EElysiumGraphState::Crouch;
		case EElysiumAnimActivityCode::Leap:
			return EElysiumGraphState::Leap;
		case EElysiumAnimActivityCode::Falling:
			return EElysiumGraphState::Falling;
		case EElysiumAnimActivityCode::Land:
			return EElysiumGraphState::Land;
		// A ducked phase-8 landing. The activity resolves nothing on a validated player body — the
		// controlled corpus records the selection simply returning -1 — so the graph declares where
		// the body stands instead of the resolver inventing a clip for it. `Land` rather than
		// `Crouch`: the request is a landing, and the record still names ACT_LAND_CROUCH as what was
		// asked for.
		case EElysiumAnimActivityCode::LandCrouch:
			return EElysiumGraphState::Land;
		// The ten grounded knockback cells. They are STATED here rather than reached through the
		// default, because the answer is a decision and not a fall-through: a knockback plays on the
		// reaction branch (`ReactionBranchTag`), which sits above the locomotion pose and owns the
		// body outright for as long as its claim stands — so the locomotion state underneath is not
		// what is on screen, and the eight-state vocabulary carries no knockback state to name it
		// with. `Idle` is what a body whose locomotion is not driving it is doing, and the record
		// still names the cell that was asked for.
		//
		// The consequence to keep in view: adding a knockback GRAPH state would be a change to the
		// tracked graph text, and the reaction branch is what makes one unnecessary.
		case EElysiumAnimActivityCode::KnockbackSmallHighForward:
		case EElysiumAnimActivityCode::KnockbackSmallHighBack:
		case EElysiumAnimActivityCode::KnockbackSmallHighLeft:
		case EElysiumAnimActivityCode::KnockbackSmallHighRight:
		case EElysiumAnimActivityCode::KnockbackNormalHighForward:
		case EElysiumAnimActivityCode::KnockbackNormalHighBack:
		case EElysiumAnimActivityCode::KnockbackNormalHighLeft:
		case EElysiumAnimActivityCode::KnockbackNormalHighRight:
		case EElysiumAnimActivityCode::KnockbackSmallLowBack:
		case EElysiumAnimActivityCode::KnockbackNormalLowBack:
		// The death family shares the answer for the same structural reason — never a base-channel
		// locomotion answer. A death pose is played on the REACTION branch, over whatever the graph
		// is posing, so its own request never enters the state machine and this projection describes
		// only the state underneath it. The reaction claim holds that pose and the death handoff then
		// freezes or ragdolls the body; a ninth graph state would name a state node the authored
		// graph does not carry.
		case EElysiumAnimActivityCode::DieSimple:
		case EElysiumAnimActivityCode::DieRagdoll:
			return EElysiumGraphState::Idle;
		// Reachable, and outside the slice: the controlled corpus never witnessed either, so neither
		// has a state of its own yet. Standing is the honest answer, and the record says what was
		// really asked for.
		case EElysiumAnimActivityCode::Swim:
		case EElysiumAnimActivityCode::Treadwater:
		case EElysiumAnimActivityCode::Idle:
		case EElysiumAnimActivityCode::Unknown:
		default:
			return EElysiumGraphState::Idle;
		}
	}

	const TCHAR* StateName(EElysiumGraphState State)
	{
		switch (State)
		{
		case EElysiumGraphState::Idle:    return TEXT("Idle");
		case EElysiumGraphState::Walk:    return TEXT("Walk");
		case EElysiumGraphState::Run:     return TEXT("Run");
		case EElysiumGraphState::Sneak:   return TEXT("Sneak");
		case EElysiumGraphState::Crouch:  return TEXT("Crouch");
		case EElysiumGraphState::Leap:    return TEXT("Leap");
		case EElysiumGraphState::Falling: return TEXT("Falling");
		case EElysiumGraphState::Land:    return TEXT("Land");
		default:                          return TEXT("Idle");
		}
	}

	bool TryParseState(const FString& Name, EElysiumGraphState& OutState)
	{
		for (int32 i = 0; i < NumGraphStates; ++i)
		{
			const EElysiumGraphState State = static_cast<EElysiumGraphState>(i);
			if (Name.Equals(StateName(State), ESearchCase::IgnoreCase))
			{
				OutState = State;
				return true;
			}
		}
		return false;
	}

	const TCHAR* ActivityForState(EElysiumGraphState State)
	{
		switch (State)
		{
		case EElysiumGraphState::Walk:    return TEXT("ACT_WALK");
		case EElysiumGraphState::Run:     return TEXT("ACT_RUN");
		case EElysiumGraphState::Sneak:   return TEXT("ACT_SNEAK");
		case EElysiumGraphState::Crouch:  return TEXT("ACT_CROUCH");
		case EElysiumGraphState::Leap:    return TEXT("ACT_LEAP");
		case EElysiumGraphState::Falling: return TEXT("ACT_FALLING");
		case EElysiumGraphState::Land:    return TEXT("ACT_LAND");
		default:                          return TEXT("ACT_IDLE");
		}
	}

	bool StateCanPlayBlendSpace(EElysiumGraphState State)
	{
		switch (State)
		{
		case EElysiumGraphState::Idle:
		case EElysiumGraphState::Walk:
		case EElysiumGraphState::Run:
		case EElysiumGraphState::Sneak:
		case EElysiumGraphState::Crouch:
		case EElysiumGraphState::Leap:
		case EElysiumGraphState::Falling:
		case EElysiumGraphState::Land:
			return true;
		default:
			return false;
		}
	}

	bool StateCanPlay(EElysiumGraphState State, EElysiumAnimAssetKind Kind)
	{
		switch (Kind)
		{
		// Nothing to play. The graph answers a miss by holding the pose it has, which every state
		// can do, so a request that resolved no asset is never a coverage hole.
		case EElysiumAnimAssetKind::None:
			return true;
		// Every state carries a sequence player: the authored pair is sequence-or-blend-space on all
		// eight, and `bHasBlendSpace` is what picks between them.
		case EElysiumAnimAssetKind::Sequence:
			return true;
		case EElysiumAnimAssetKind::BlendSpace:
			return StateCanPlayBlendSpace(State);
		// A masked overlay is never a base pose — it rides the layered blend, which is the same rule
		// that refuses an additive from the base channel.
		case EElysiumAnimAssetKind::Layer:
		default:
			return false;
		}
	}

	bool RefuseUnplayableGrid(FElysiumAnimationSelection& Selection)
	{
		if (Selection.AssetKind != EElysiumAnimAssetKind::BlendSpace)
		{
			return false;
		}
		const EElysiumGraphState State = Selection.GraphState;
		if (StateCanPlay(State, Selection.AssetKind))
		{
			return false;
		}
		Selection.AssetKind = EElysiumAnimAssetKind::None;
		Selection.Outcome = EElysiumAnimOutcome::GridStateRefused;
		Selection.Detail = FString::Printf(
			TEXT("'%s' is a blend space; state %s cannot play a grid (owner '%s')"),
			*Selection.SequenceLabel, StateName(State),
			Selection.OwnerStem.IsEmpty() ? TEXT("?") : *Selection.OwnerStem);
		return true;
	}

	bool IsOneShotState(EElysiumGraphState State)
	{
		// Read off the authored data rather than chosen: `leap` and `land` are non-looping clips in
		// the `misc` bank, and `crouch` is a 61-frame non-looping into-pose carrying flags 0x0. The
		// other five loop, so nothing has to end them.
		return State == EElysiumGraphState::Leap
			|| State == EElysiumGraphState::Land
			|| State == EElysiumGraphState::Crouch;
	}

	bool ShouldRepeatClip(EElysiumGraphState State, bool bAuthoredLooping)
	{
		// `Crouch` alone, and by name rather than by "is a one-shot": the other two one-shots are
		// events that finish, and looping either would stop it ever reporting complete.
		return bAuthoredLooping || State == EElysiumGraphState::Crouch;
	}

	bool IsPlayableRemaining(float RemainingSeconds, float ClipLengthSeconds)
	{
		// A clip with no length answers nothing rather than answering "already finished": a zero
		// length is a missing asset, not an instant one.
		return ClipLengthSeconds > 0.0f
			&& RemainingSeconds >= 0.0f
			&& RemainingSeconds <= ClipLengthSeconds + UE_KINDA_SMALL_NUMBER;
	}

	bool NeedsForcedReblend(bool bSameAsset, bool bLoopChanged)
	{
		// Only on the same asset: a different asset re-blends on its own, and forcing one there would
		// ask the stack to push a second player for the transition it is already performing.
		return bSameAsset && bLoopChanged;
	}

	bool ShouldHoldPose(bool bHasAppliedOnce, bool bHasSequence, bool bHasBlendSpace)
	{
		return bHasAppliedOnce && !bHasSequence && !bHasBlendSpace;
	}

	EElysiumOneShotState OneShotStateFor(bool bHasAsset, bool bGenerationMatches,
		bool bInOneShotState, bool bComplete)
	{
		// Checked first, and deliberately without consulting the report: the report describes a clip,
		// and there is no clip. Nothing to wait for is finished, not unknown.
		if (!bHasAsset)
		{
			return EElysiumOneShotState::Complete;
		}
		if (!bGenerationMatches || !bInOneShotState)
		{
			return EElysiumOneShotState::Unknown;
		}
		return bComplete ? EElysiumOneShotState::Complete : EElysiumOneShotState::Playing;
	}
}
