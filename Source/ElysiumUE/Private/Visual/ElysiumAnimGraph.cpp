#include "Visual/ElysiumAnimGraph.h"

namespace ElysiumAnimGraph
{
	float TransitionSeconds(const FElysiumAnimationSelection* Outgoing,
		const FElysiumAnimationSelection& Incoming)
	{
		if (Incoming.bSnap)
		{
			return 0.0f;
		}
		const float In = FMath::Max(0.0f, Incoming.FadeSeconds);
		if (Outgoing == nullptr)
		{
			return In;
		}
		return FMath::Max(In, FMath::Max(0.0f, Outgoing->FadeSeconds));
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

	EElysiumGraphState StateFor(const FElysiumAnimationSelection& Selection)
	{
		// The LOGICAL request, not the translated one: translation changes which sequences realize a
		// request, never what the body is doing. `ACT_WALK_RELAXED` and `ACT_WALK` are the same gait
		// and the same state, which is exactly what the relaxed rows of the player's own translation
		// table say.
		return StateForActivity(ElysiumAnimIntent::ActivityCode(Selection.RequestedActivity));
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
