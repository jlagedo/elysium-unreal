#include "Substrate/ElysiumScriptedCharacter.h"

#include "ElysiumAnimationIntent.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumNpcLog.h"

namespace
{
	// One segment of the beat's montage-slot run, stated here for the travel cycle exactly as
	// `ElysiumScriptedSequence.cpp` states it for `m_iszIdle`/`m_iszPlay`/`m_iszPostIdle` — the same
	// mechanism, the same band, the same held claim. `Scripted` is what puts the cycle over a
	// TRAVELLING body: the ambient band the band-less door means is consumed by the body's own
	// locomotion publish the instant it leaves, so `m_iszCustomMove` would be replaced by an ordinary
	// gait on precisely the beats that move.
	FElysiumClipSegment TravelSegment(const FString& ClipName)
	{
		FElysiumClipSegment Segment;
		Segment.ClipName = ClipName;
		Segment.bLoop = true;
		Segment.Source = EElysiumAnimSource::Interaction;
		Segment.Priority = EElysiumAnimPriority::Scripted;
		Segment.bHoldUntilReleased = true;
		return Segment;
	}
}

FElysiumScriptedCharacter::~FElysiumScriptedCharacter()
{
	DestroyMotor();
}

bool FElysiumScriptedCharacter::BeginScriptMove(const FVector& Mark, const FVector& MarkAngles,
	EElysiumScriptGait Gait, const FString& CustomClip)
{
	if (!Motor || IsInert())
	{
		return false;
	}
	if (!ClaimScriptMove())
	{
		return false;
	}
	bScriptMoveClaimed = true;

	Motor->Stop();
	const double Now = World ? World->NowSeconds() : 0.0;
	ScriptMark = Mark;
	ScriptMarkAngles = MarkAngles;
	ScriptProgressAt = Now;
	ScriptWatchdogAt = Now + ElysiumNpcGait::ScriptWatchdogSeconds;

	if (Gait == EElysiumScriptGait::Face)
	{
		ScriptBestDistance = 0.0f;
		ScriptDeadline = Now + ElysiumNpcGait::ScriptFaceSeconds;
		ScriptPhase = EScriptPhase::Facing;
		Motor->Face(-MarkAngles.Y);
		NextThink = static_cast<float>(ScriptWatchdogAt);
		return true;
	}

	float Speed = ElysiumNpcGait::TravelSpeed(Motor, Gait == EElysiumScriptGait::Run
		? EElysiumNpcGaitKind::Run : EElysiumNpcGaitKind::Walk);
	FString ScriptWalkLabel;
	FString ScriptWalkAnim;
	if (Gait == EElysiumScriptGait::Walk)
	{
		IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
		// A scripted character is a `CAI_BaseNPC` descendant whatever it is standing in for, so the
		// chain is the cast's and the call states it.
		FElysiumActivityClipRequest Request;
		FillActivityClipRequest(Request);
		Request.Activity = TEXT("ACT_WALK");
		Request.Variant = FMath::Max(0, Handle.Index);
		// A scripted beat asked for this leg, not the body's own AI, and the record names the
		// producer rather than the body.
		Request.Source = EElysiumAnimSource::Scene;
		Request.BodyKind = EElysiumAnimBodyKind::Cast;

		FElysiumActivityClip Walk;
		if (Embodiment && Embodiment->ResolveNpcActivityClip(Request, Walk))
		{
			// The label travels even when the cell carries no motion record: it is what the travel
			// cycle below plays, and only the SPEED is conditional on the authored number.
			ScriptWalkLabel = Walk.Label;
			ScriptWalkAnim = Walk.AnimationName;
			if (FMath::IsFinite(Walk.GroundSpeedCmPerSecond) && Walk.GroundSpeedCmPerSecond > 0.f)
			{
				Speed = Walk.GroundSpeedCmPerSecond;
				UE_LOG(LogElysiumNpcEnt, Verbose,
					TEXT("%s scripted walk uses '%s' -> '%s' authored ground speed %.1fcm/s"),
					*DebugString(), *ScriptWalkLabel, *ScriptWalkAnim, Speed);
			}
		}
	}
	else if (Gait == EElysiumScriptGait::Custom)
	{
		// `CustomClip` already names the exact clip (`m_iszCustomMove`), so it is one lookup away
		// from the same authored ground speed the Walk branch above resolves through its activity
		// -- the label-route sibling of ResolveNpcActivityClip, not a second seam.
		IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
		float AuthoredSpeed = 0.f;
		FString CustomAnim;
		if (Embodiment && Embodiment->ResolveNpcSequenceClip(ModelStem(), CustomClip,
			EElysiumAnimBodyKind::Cast, CustomAnim, AuthoredSpeed)
			&& FMath::IsFinite(AuthoredSpeed) && AuthoredSpeed > 0.f)
		{
			Speed = AuthoredSpeed;
			UE_LOG(LogElysiumNpcEnt, Verbose,
				TEXT("%s scripted custom move uses '%s' -> '%s' authored ground speed %.1fcm/s"),
				*DebugString(), *CustomClip, *CustomAnim, Speed);
		}
	}
	// A scripted Run rides the run fan and nothing else, so it names it and lets the body's own
	// animation pass re-derive the cell as the leg turns. Walk and Custom above resolved a specific
	// clip's authored ground speed instead — a number the beat asked for by name — so they stay
	// untagged and are never re-derived from a fan.
	const TOptional<EElysiumNpcGaitKind> RequestedGait = Gait == EElysiumScriptGait::Run
		? TOptional<EElysiumNpcGaitKind>(EElysiumNpcGaitKind::Run)
		: TOptional<EElysiumNpcGaitKind>();
	if (!Motor->MoveTo(Mark, ElysiumNpcGait::ScriptAcceptanceCm, Speed,
		/*bAllowPartialPath=*/true, RequestedGait))
	{
		ScriptPhase = EScriptPhase::None;
		ReleaseScriptMove(TEXT("script path unavailable"));
		bScriptMoveClaimed = false;
		return false;
	}

	ScriptPhase = EScriptPhase::Travel;
	ScriptBestDistance = static_cast<float>(FVector::Dist2D(Origin, Mark));
	bool bTravelCycleStarted = false;
	if (Gait == EElysiumScriptGait::Custom)
	{
		bTravelCycleStarted = PlayAnimSegment(TravelSegment(CustomClip));
	}
	else if (Gait == EElysiumScriptGait::Walk && !ScriptWalkLabel.IsEmpty())
	{
		bTravelCycleStarted = PlayAnimSegment(TravelSegment(ScriptWalkLabel));
	}
	if (!bTravelCycleStarted)
	{
		StartScriptWalkingAnimation(Gait == EElysiumScriptGait::Run);
	}
	ScriptDeadline = Now + ElysiumNpcGait::TravelCapSeconds(ScriptBestDistance, Speed);
	NextThink = static_cast<float>(ScriptWatchdogAt);
	return true;
}

void FElysiumScriptedCharacter::SyncMovingRecord()
{
	if (Motor == nullptr || IsInert())
	{
		return;
	}
	FVector Feet = Origin;
	float Yaw = -Angles.Y;
	Motor->SampleTransform(Feet, Yaw);
	if (Feet.Equals(Origin, 0.01) && FMath::IsNearlyEqual(-Yaw, Angles.Y, 0.01f))
	{
		return;   // a standing body costs nothing
	}
	Origin = Feet;
	Angles.Y = -Yaw;
	if (World)
	{
		World->NotifyVisualChanged(*this);
	}
}

EElysiumNpcMoveStatus FElysiumScriptedCharacter::SampleMotorIntoEntity()
{
	FVector Feet = Origin;
	float Yaw = -Angles.Y;
	const EElysiumNpcMoveStatus Status = Motor->Sample(Feet, Yaw);
	Origin = Feet;
	Angles.Y = -Yaw;
	if (World)
	{
		World->NotifyVisualChanged(*this);
	}
	return Status;
}

EElysiumScriptMove FElysiumScriptedCharacter::AdvanceScriptMove()
{
	if (ScriptPhase == EScriptPhase::None)
	{
		return EElysiumScriptMove::Unsupported;
	}
	if (!Motor || IsInert())
	{
		ScriptPhase = EScriptPhase::None;
		return EElysiumScriptMove::Failed;
	}

	const double Now = World ? World->NowSeconds() : 0.0;
	ScriptWatchdogAt = Now + ElysiumNpcGait::ScriptWatchdogSeconds;
	const EElysiumNpcMoveStatus Status = SampleMotorIntoEntity();

	if (ScriptPhase == EScriptPhase::Facing)
	{
		if (Status == EElysiumNpcMoveStatus::Moving && Now < ScriptDeadline)
		{
			return EElysiumScriptMove::Moving;
		}
		ScriptPhase = EScriptPhase::None;
		return EElysiumScriptMove::Arrived;
	}

	const float Remaining = static_cast<float>(FVector::Dist2D(Origin, ScriptMark));
	if (Remaining + ElysiumNpcGait::ScriptProgressCm < ScriptBestDistance)
	{
		ScriptBestDistance = Remaining;
		ScriptProgressAt = Now;
	}
	if (Status == EElysiumNpcMoveStatus::Reached)
	{
		return BeginScriptFacing(Now);
	}

	const double Stalled = Now - ScriptProgressAt;
	const bool bNearMark = Remaining <= ElysiumNpcGait::ScriptCrowdedCm;
	if (bNearMark && Stalled >= ElysiumNpcGait::ScriptCrowdSettleSeconds)
	{
		UE_LOG(LogElysiumNpcEnt, Log,
			TEXT("%s settled %.0fcm short of %s (stopped closing for %.1fs)"),
			*DebugString(), Remaining, *ScriptMark.ToString(), Stalled);
		return BeginScriptFacing(Now);
	}
	if (Status == EElysiumNpcMoveStatus::Moving
		&& Stalled < ElysiumNpcGait::ScriptStallSeconds && Now < ScriptDeadline)
	{
		return EElysiumScriptMove::Moving;
	}
	if (bNearMark)
	{
		return BeginScriptFacing(Now);
	}

	ScriptPhase = EScriptPhase::None;
	UE_LOG(LogElysiumNpcEnt, Warning,
		TEXT("%s scripted move to %s gave up %.0fcm short (status %d)"),
		*DebugString(), *ScriptMark.ToString(), Remaining, static_cast<int32>(Status));
	return EElysiumScriptMove::Failed;
}

void FElysiumScriptedCharacter::EndScriptMove()
{
	if (ScriptPhase == EScriptPhase::None && !bScriptMoveClaimed)
	{
		return;
	}
	ScriptPhase = EScriptPhase::None;
	if (Motor)
	{
		Motor->Stop();
	}
	ReleaseScriptMove(TEXT("EndScriptMove"));
	bScriptMoveClaimed = false;
	NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
}

void FElysiumScriptedCharacter::SetBodyFrozen(bool bFrozen)
{
	if (Motor)
	{
		Motor->SetFrozen(bFrozen);
	}
}

void FElysiumScriptedCharacter::SetIgnoreCharacterCollision(bool bIgnore)
{
	if (Motor)
	{
		Motor->SetIgnoreCharacterCollision(bIgnore);
	}
}

void FElysiumScriptedCharacter::OnRuntimeTransformChanged()
{
	FElysiumEntity::OnRuntimeTransformChanged();
	if (Motor)
	{
		Motor->Teleport(Origin, -Angles.Y);
	}
	else
	{
		FElysiumAnimating::OnRuntimeTransformChanged();
	}
}

void FElysiumScriptedCharacter::BuildMotor()
{
	if (Motor || !Visual)
	{
		return;
	}
	if (IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr)
	{
		Motor = Embodiment->BuildNpcMotor(Visual, Handle, Origin, -Angles.Y, ModelStem(),
			FMath::Max(0, Handle.Index));
		if (Motor)
		{
			Motor->SetEnabled(!IsInert());
		}
	}
}

void FElysiumScriptedCharacter::RebuildForModelChange(bool bBodiesEnabled)
{
	if (!bBodiesEnabled)
	{
		return;
	}
	EndScriptMove();
	DestroyMotor();
	FElysiumAnimating::OnRuntimeModelChanged();
	BuildOwnMotor();
}

void FElysiumScriptedCharacter::DestroyMotor()
{
	if (Motor && World && World->Embodiment())
	{
		World->Embodiment()->DestroyNpcMotor(Motor);
	}
	Motor = nullptr;
}

// The fallback gait, reached when the beat authored no clip for this leg — every `Run`, and a
// `Walk`/`Custom` whose own label did not resolve. It is the BODY's own gait rather than a pose the
// beat asked for, so it goes through the band-less door and yields to the locomotion publish exactly
// as it always has: a `Scripted` claim here would pin one resolved cell on the slot for the whole
// leg and stop the body's own animation pass re-deriving the fan as the leg turns, which is what
// `BeginScriptMove` states a scripted Run rides.
bool FElysiumScriptedCharacter::StartScriptWalkingAnimation(bool bRunning)
{
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (Embodiment && Visual)
	{
		FElysiumActivityClipRequest Request;
		FillActivityClipRequest(Request);
		Request.Activity = bRunning ? TEXT("ACT_RUN") : TEXT("ACT_WALK");
		Request.Variant = FMath::Max(0, Handle.Index);
		// The scripted beat is the producer here too — this is the travel cycle its own move started.
		Request.Source = EElysiumAnimSource::Scene;
		Request.BodyKind = EElysiumAnimBodyKind::Cast;

		FElysiumActivityClip Clip;
		if (Embodiment->ResolveNpcActivityClip(Request, Clip)
			&& PlayAnimClip(Clip.Label, /*bLoop=*/true))
		{
			return true;
		}
	}
	return PlayAnimClip(bRunning ? TEXT("run") : TEXT("walk"), /*bLoop=*/true);
}

EElysiumScriptMove FElysiumScriptedCharacter::BeginScriptFacing(double Now)
{
	ScriptPhase = EScriptPhase::Facing;
	ScriptDeadline = Now + ElysiumNpcGait::ScriptFaceSeconds;
	if (Motor)
	{
		Motor->Face(-ScriptMarkAngles.Y);
	}
	return EElysiumScriptMove::Moving;
}
