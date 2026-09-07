#include "ElysiumCameraSolve.h"

// The approach (`client.dll` 0x100fc000)

float ElysiumCam::Approach(float Current, float Target, float Speed, float Dt)
{
	const float Delta = Target - Current;
	if (Speed <= 0.0f || Dt <= 0.0f)
	{
		return Target;
	}
	const float Step = Speed * Dt;
	return FMath::Abs(Delta) <= Step ? Target : Current + FMath::Sign(Delta) * Step;
}

float ElysiumCam::ApproachAngle(float Current, float Target, float Speed, float Dt)
{
	// The 16-bit angle round trip VtMB does here is a storage detail; what it buys is wrap, which is
	// what NormalizeAxis gives directly.
	const float Delta = FRotator::NormalizeAxis(Target - Current);
	if (Speed <= 0.0f || Dt <= 0.0f)
	{
		return FRotator::NormalizeAxis(Target);
	}
	const float Step = Speed * Dt;
	if (FMath::Abs(Delta) <= Step)
	{
		return FRotator::NormalizeAxis(Target);
	}
	return FRotator::NormalizeAxis(Current + FMath::Sign(Delta) * Step);
}

float ElysiumCam::SolveViewRoll(const FVector& VelocityCm, const FRotator& ViewRot,
	float RollAngleDeg, float RollSpeedCm)
{
	if (RollSpeedCm <= 0.0f || RollAngleDeg == 0.0f)
	{
		return 0.0f;
	}
	// `AngleVectors`' right vector, dotted with the whole velocity — the vertical term is part of
	// the dot in the original and is kept rather than flattened.
	const FVector Right = FRotationMatrix(ViewRot).GetScaledAxis(EAxis::Y);
	const float Side = static_cast<float>(FVector::DotProduct(VelocityCm, Right));
	const float Sign = Side >= 0.0f ? 1.0f : -1.0f;
	const float Mag = FMath::Abs(Side);

	return Mag < RollSpeedCm
		? (Mag / RollSpeedCm) * RollAngleDeg * Sign
		: RollAngleDeg * Sign;
}

// The water clearance (`CViewRender::GetWaterOffset`, `cl_waterdist`)

float ElysiumCam::SolveWaterOffset(int32 WaterLevel, float ViewZ, float SurfaceZ, float WaterDistCm)
{
	// Treading: the surface sits at or just below the eye, so lift the eye to `WaterDist` above it.
	// The clamp is the band itself — a surface exactly `WaterDist` down needs no lift, and one
	// above the eye is level 3's case, not this one.
	if (WaterLevel == 2 && SurfaceZ >= ViewZ - WaterDistCm && SurfaceZ <= ViewZ)
	{
		return FMath::Clamp(SurfaceZ + WaterDistCm - ViewZ, 0.0f, WaterDistCm);
	}
	// Submerged: the surface sits at or just above the eye, so push the eye back under it.
	if (WaterLevel == 3 && SurfaceZ >= ViewZ && SurfaceZ <= ViewZ + WaterDistCm)
	{
		return FMath::Clamp(SurfaceZ - WaterDistCm - ViewZ, -WaterDistCm, 0.0f);
	}
	return 0.0f;
}

// Player-model visibility (`CAM_Think` tail, `CInput+0x104`)

float ElysiumCam::SolveModelAlpha(const FVector& SolvedOffset,
	const FElysiumCameraWeights& Weights, const FElysiumCameraCvars& Cvars)
{
	// 0 below `cam_fadeend`, 1 at/above min(`cam_idealdist`, `cam_fadestart`), SimpleSpline
	// between. The third-person weight scales the solved boom before the band is evaluated.
	//
	// **This is opacity, not eligibility** — `SolveDrawPolicy` owns the draw gate. The band reads the
	// *third-person boom*, so it answers 0 for the whole of first person no matter what else is
	// composed over the view: a scripted shot adds weight to the predicate, not length to the boom.
	// That is what makes a first-person cutscene draw an eligible but fully transparent body, which
	// is why retail needs no player-body suppression and neither do we
	// (`docs/vtmb/camera-view-modes.md` §6). In third person the real body is drawn beside any
	// `npc_VPlayerController` stand-in, exactly as retail does it.
	const float Distance = SolvedOffset.Size() * Weights.ThirdBlend();
	const float Full = FMath::Min(Cvars.IdealDist, Cvars.FadeStart);
	if (Distance >= Full)
	{
		return 1.0f;
	}
	if (Distance > Cvars.FadeEnd)
	{
		const float Span = FMath::Max(KINDA_SMALL_NUMBER, Full - Cvars.FadeEnd);
		return SimpleSpline((Distance - Cvars.FadeEnd) / Span);
	}
	return 0.0f;
}

// Weapon-class arbitration (`docs/vtmb/camera-view-modes.md` §2)

int32 ElysiumCam::ParseCameraClass(const FString& Literal)
{
	// Case-sensitive, exactly as the retail ladder is. `Ranged` is not `ranged` and falls through to
	// zero, which is a real authored hazard rather than a defect to fix here.
	if (Literal.Equals(TEXT("ranged"), ESearchCase::CaseSensitive))    { return CameraClass::Ranged; }
	if (Literal.Equals(TEXT("thrown"), ESearchCase::CaseSensitive))    { return CameraClass::Thrown; }
	if (Literal.Equals(TEXT("force_1st"), ESearchCase::CaseSensitive)) { return CameraClass::ForceFirst; }
	if (Literal.Equals(TEXT("melee"), ESearchCase::CaseSensitive))     { return CameraClass::ForceThird; }
	if (Literal.Equals(TEXT("force_3rd"), ESearchCase::CaseSensitive)) { return CameraClass::ForceThird; }
	// `noswitch`, a typo, a mis-cased literal and an absent key all land here.
	return CameraClass::None;
}

ElysiumCam::EWeaponCameraAction ElysiumCam::ApplyWeaponCameraPref(int32 Class, int32 Prefs,
	bool bWeaponSwitch)
{
	if (Class == CameraClass::None || !bWeaponSwitch)
	{
		return EWeaponCameraAction::None;
	}
	// The two forced classes short-circuit **before** the preference bitmask is consulted, which is
	// why a melee weapon cannot be brought to first person at all and why neither writes a
	// preference back.
	if (Class == CameraClass::ForceFirst)
	{
		return EWeaponCameraAction::ToFirstPerson;
	}
	if (Class == CameraClass::ForceThird)
	{
		return EWeaponCameraAction::ForceThirdOn;
	}
	// A set bit means first person. Default `camera_prefs 6` sets `ranged` and `thrown`, so a stock
	// install draws both in first person.
	return (Class & Prefs) != 0
		? EWeaponCameraAction::ToFirstPerson : EWeaponCameraAction::ToThirdPerson;
}

int32 ElysiumCam::SaveWeaponCameraPref(int32 Class, int32 Prefs, bool bThirdPerson)
{
	if (Class == CameraClass::ForceFirst || Class == CameraClass::ForceThird
		|| Class == CameraClass::None)
	{
		return Prefs;   // not user-settable
	}
	return bThirdPerson ? (Prefs & ~Class) : (Prefs | Class);
}

FElysiumCameraDrawPolicy ElysiumCam::SolveDrawPolicy(const FElysiumCameraWeights& Weights,
	const FVector& SolvedOffset, const FElysiumCameraCvars& Cvars,
	const FElysiumShotPresentation& Shot)
{
	FElysiumCameraDrawPolicy Out;

	// `ShouldDrawLocalPlayer` (`0x100a5910`) and the viewmodel gate in `ShouldDrawViewModel`
	// (`0x10198ea0`) both reach this same predicate through `CInput` slot `+0x74`, with opposite
	// senses. Reading the *smoothed weight* here instead would move both switches into the middle of
	// the blend, where retail has already switched them.
	Out.bThirdPerson = Weights.IsThirdPerson();

	// The body submits whenever the predicate holds, and the band decides how much of it is seen. On
	// the first frame of a first->third transition those disagree — eligible at alpha 0 — and that
	// disagreement is the recovered behaviour rather than a state to collapse.
	Out.bBodyEligible = Out.bThirdPerson;
	Out.BodyAlpha = SolveModelAlpha(SolvedOffset, Weights, Cvars);

	// Boolean, both directions. No attachment-alpha consumer is recovered, so a world weapon pops
	// rather than fading; inventing a fade here would be inventing cinematography.
	Out.bWorldWeaponEligible = Out.bThirdPerson;

	// **Resuming at exactly weight 0 falls out of the strict comparison inside the predicate.** The
	// last frame of a third->first return is the first frame every term is false, which is the frame
	// retail resumes the hands on. An epsilon here would resume them early and break the table.
	//
	// The second term is redundant while a named shot is live — such a shot always carries scripted
	// weight, so the predicate has already suppressed the viewmodel — and it is kept because the RE
	// records both gates. `DrawViewmodel` can only ever suppress; it never forces the hands on.
	Out.bViewmodelEligible = !Out.bThirdPerson && (!Shot.bNamed || Shot.bDrawViewmodel);

	Out.Reticle = Out.bThirdPerson
		? EElysiumReticlePath::ThirdPerson : EElysiumReticlePath::FirstPerson;

	// Named shots only. Both keys parse with a default of 0, so an authored story shot with no keys
	// hides the HUD while an interaction shot opts back in. A value shot — a `camera_track`, a VCD
	// edit — carries no such key and therefore cannot take the HUD down.
	Out.bShowHud = !Shot.bNamed || Shot.bShowHud;

	return Out;
}

// The scripted composition (`ApplyScriptedBlend`, tail of `CAM_ApplyToView`)

void ElysiumCam::ComposeScriptedShot(FVector& InOutLocation, FRotator& InOutRotation, float& InOutFov,
	const FVector& ShotLocation, const FRotator& ShotRotation, float ShotFov, float Weight)
{
	if (Weight <= 0.0f)
	{
		return;
	}
	// `FMath::Lerp` on a rotator interpolates the *normalized* delta, so a shot across the ±180
	// boundary takes the short way round. That is the behaviour the shipped apply point has, and it
	// is called rather than reimplemented so the two cannot drift.
	InOutLocation = FMath::Lerp(InOutLocation, ShotLocation, Weight);
	InOutRotation = FMath::Lerp(InOutRotation, ShotRotation, Weight);
	if (ShotFov > 0.0f)
	{
		InOutFov = FMath::Lerp(InOutFov, ShotFov, Weight);
	}
}

// The weight driver (0x100fc900)

void FElysiumCameraWeights::Advance(float DeltaSeconds, float TimeScale)
{
	const float Dt = FMath::Max(0.0f, DeltaSeconds) * FMath::Max(0.0f, TimeScale);

	// The feed and secondary weights advance FIRST, because the third-person driver below reads the
	// feed weight in its own priority test.
	Feed = FMath::Clamp(Feed + (bFeed ? ElysiumCam::FeedBlendRate
		: -ElysiumCam::FeedBlendRate) * Dt, 0.0f, 1.0f);
	Secondary = FMath::Clamp(Secondary - ElysiumCam::SecondaryDecayRate * Dt, 0.0f, 1.0f);

	// Priority: forced-third and the feed camera win, then forced-first, then the user toggle. The
	// weight ramps linearly at 2.0/s in both directions and clamps — there is no transition object, so
	// reversing mid-blend continues from where it is.
	float W = Third;
	if (bForcedThird || Feed > 0.0f)  { W += ElysiumCam::BlendRate * Dt; }
	else if (bForcedFirst)            { W -= ElysiumCam::BlendRate * Dt; }
	else if (bUserThird)              { W += ElysiumCam::BlendRate * Dt; }
	else                              { W -= ElysiumCam::BlendRate * Dt; }

	Third = FMath::Clamp(W, 0.0f, 1.0f);
}

bool FElysiumCameraWeights::IsThirdPerson() const
{
	return bForcedThird || bUserThird
		|| Third > 0.0f || Secondary > 0.0f || Feed > 0.0f || Scripted > 0.0f;
}

const TCHAR* FElysiumCameraWeights::Driver() const
{
	if (bForcedThird)   { return TEXT("forced-third"); }
	if (Feed > 0.0f)    { return TEXT("feed"); }
	if (bForcedFirst)   { return TEXT("forced-first"); }
	if (bUserThird)     { return TEXT("user"); }
	return TEXT("first");
}

FElysiumFeedCameraPose ElysiumCam::SolveOrdinaryFeedCamera(float T, float EntryYaw,
	const FElysiumCameraCvars& Cvars)
{
	const float Seconds = FMath::Max(0.0f, T);
	const float SourcePitch = FMath::Min(Cvars.FeedPitchMax,
		Cvars.FeedPitch - Cvars.FeedPitch
			* FMath::Pow(Cvars.FeedPitchPow1, Cvars.FeedPitchPow2 * Seconds));
	const FRotator Rotation(-SourcePitch, EntryYaw + Cvars.FeedYaw * Seconds, Cvars.FeedRoll);
	FElysiumFeedCameraPose Out;
	Out.Rotation = Rotation;
	Out.Offset = Rotation.Vector()
		* (Cvars.FeedForwardBase * FMath::Pow(Seconds, Cvars.FeedForwardPow));
	return Out;
}

// The scripted-shot channel

// The client shot tracker (`C_BaseCineCamera`)

namespace
{
	// How long the remaining translation still takes, given the current speed, the shot's ceiling and
	// its acceleration. `SyncRotateOnMove` divides the angular error by this so the pan lands with the
	// dolly (`FUN_10001c80`). With no acceleration the camera runs at `MoveSpeed` outright.
	float RemainingTranslationSeconds(float Speed, float MaxSpeed, float Accel, float Distance)
	{
		const float Ceiling = FMath::Max(MaxSpeed, KINDA_SMALL_NUMBER);
		if (Accel <= 0.0f)
		{
			return Distance / Ceiling;
		}
		const float RampSeconds = FMath::Max(0.0f, Ceiling - Speed) / Accel;
		const float RampDistance = 0.5f * (Speed + Ceiling) * RampSeconds;
		if (RampDistance >= Distance)
		{
			// v*t + a*t^2/2 = d.
			return (-Speed + FMath::Sqrt(Speed * Speed + 2.0f * Accel * Distance)) / Accel;
		}
		return RampSeconds + (Distance - RampDistance) / Ceiling;
	}

	// One axis of `FUN_10001d40` plus its rate solve `FUN_10001c80`. `InOutRate` is the axis' entry in
	// the tracker's `0x4a8[3]`; `bInOutSettled` its entry in `0x4c1[3]`.
	// `FRotator`/`FVector` components are `double` in UE5, so the axis and its rate come in by
	// reference at that width; the shot's own limits stay the `float` the file parsed them as.
	void AdvanceAngleAxis(double& InOutCurrent, double Desired, double& InOutRate, bool& bInOutSettled,
		float Tolerance, float MaxRate, float TurnAccel, float SyncSeconds, float Dt)
	{
		const double Delta = FRotator::NormalizeAxis(Desired - InOutCurrent);
		const double Error = FMath::Abs(Delta);

		// The deadband is the shot's tolerance while the axis is parked and a hair once it is turning:
		// the camera comes out of the park only on a real drift, and then goes all the way onto the
		// target instead of stopping at the edge of the band.
		const double Deadband = bInOutSettled
			? FMath::Max(Tolerance, 0.0f) : FElysiumScriptedShotTracker::SettleAngle;
		if (Error <= Deadband)
		{
			bInOutSettled = true;
			InOutRate = 0.0f;
			return;
		}
		bInOutSettled = false;

		if (SyncSeconds > 0.0f)
		{
			// `SyncRotateOnMove` with the position still moving: size the rate to the remaining
			// translation time. `MaxTurnRate` is deliberately bypassed here — retail lands the pan with
			// the dolly rather than clamping it.
			InOutRate = Error / SyncSeconds;
		}
		else if (TurnAccel <= 0.0f)
		{
			// A value shot (a `camera_track` edit, a grammar profile) authors no `TurnAccel`; it runs
			// at the rate ceiling directly, which is the rate-limited behaviour this channel had
			// before the tracker landed.
			InOutRate = FMath::Max(MaxRate, 0.0f);
		}
		else
		{
			const double StoppingAngle = (InOutRate * InOutRate) / (2.0f * TurnAccel);
			InOutRate += (Error <= StoppingAngle ? -TurnAccel : TurnAccel) * Dt;
			InOutRate = FMath::Clamp(InOutRate, 0.0f, FMath::Max(MaxRate, 0.0f));
		}

		const double Step = InOutRate * Dt;
		InOutCurrent = FRotator::NormalizeAxis(
			Step >= Error ? Desired : InOutCurrent + FMath::Sign(Delta) * Step);
	}
}

void FElysiumScriptedShotTracker::Start(const FElysiumCameraShot& Shot)
{
	Location = Shot.Origin;
	Rotation = Shot.bUseLookAt ? (Shot.LookAt - Shot.Origin).Rotation() : Shot.Rotation;
	Rotation.Roll = Shot.Roll;
	Speed = 0.0f;
	TurnRate = FVector::ZeroVector;
	// Retail marks the position settled and zeroes the rates; the axes acquire on the first frame,
	// which with the pose already at the goal is a no-op.
	bPositionSettled = true;
	bPitchSettled = false;
	bYawSettled = false;
	bRollSettled = false;
	bSeeded = true;
}

void FElysiumScriptedShotTracker::Advance(const FElysiumCameraShot& Shot, float DeltaSeconds)
{
	if (!bSeeded)
	{
		Start(Shot);
		return;
	}
	const float Dt = FMath::Max(0.0f, DeltaSeconds);
	if (Dt <= 0.0f)
	{
		return;
	}

	// `SnapOnShotChange` is a hard copy, and a shot with no `MoveSpeed` snaps by the same grammar
	// ("0 = the camera is not rate-limited"), so both skip the approach entirely.
	if (Shot.bSnapOnShotChange || Shot.MoveSpeed <= 0.0f)
	{
		Location = Shot.Origin;
		Speed = 0.0f;
		bPositionSettled = true;
	}
	else
	{
		// Position, `FUN_10001fe0`.
		const FVector ToGoal = Shot.Origin - Location;
		const float Distance = static_cast<float>(ToGoal.Size());
		const float Threshold = bPositionSettled
			? FMath::Max(Shot.DistanceTolerance, 0.0f) : SettleDistance;
		if (Distance < Threshold)
		{
			bPositionSettled = true;
			Speed = 0.0f;
		}
		else
		{
			bPositionSettled = false;
			if (Shot.MoveAccel <= 0.0f)
			{
				Speed = Shot.MoveSpeed;
			}
			else
			{
				const float StoppingDistance = (Speed * Speed) / (2.0f * Shot.MoveAccel);
				Speed += (Distance <= StoppingDistance ? -Shot.MoveAccel : Shot.MoveAccel) * Dt;
				Speed = FMath::Clamp(Speed, 0.0f, Shot.MoveSpeed);
			}
			const float Step = Speed * Dt;
			Location = Step >= Distance ? Shot.Origin : Location + (ToGoal / Distance) * Step;
		}
	}

	// Angles, `FUN_10001d40`. The desired angle is solved against the origin the position step just
	// produced, exactly as retail does — which is why a dolly re-aims as it travels.
	FRotator Desired = Shot.bUseLookAt ? (Shot.LookAt - Location).Rotation() : Shot.Rotation;
	Desired.Roll = Shot.Roll;

	if (Shot.bSnapOnShotChange)
	{
		Rotation = Desired;
		TurnRate = FVector::ZeroVector;
		bPitchSettled = bYawSettled = bRollSettled = true;
		return;
	}

	// `SyncRotateOnMove`: only while the position is actually travelling.
	const float SyncSeconds = (Shot.bSyncRotateOnMove && !bPositionSettled)
		? FMath::Max(RemainingTranslationSeconds(Speed, Shot.MoveSpeed, Shot.MoveAccel,
			static_cast<float>(FVector::Distance(Shot.Origin, Location))), Dt)
		: 0.0f;

	AdvanceAngleAxis(Rotation.Pitch, Desired.Pitch, TurnRate.X, bPitchSettled,
		static_cast<float>(Shot.AngularTolerance.X), static_cast<float>(Shot.MaxTurnRate.X),
		Shot.TurnAccel, SyncSeconds, Dt);
	AdvanceAngleAxis(Rotation.Yaw, Desired.Yaw, TurnRate.Y, bYawSettled,
		static_cast<float>(Shot.AngularTolerance.Y), static_cast<float>(Shot.MaxTurnRate.Y),
		Shot.TurnAccel, SyncSeconds, Dt);
	AdvanceAngleAxis(Rotation.Roll, Desired.Roll, TurnRate.Z, bRollSettled,
		static_cast<float>(Shot.AngularTolerance.Z), static_cast<float>(Shot.MaxTurnRate.Z),
		Shot.TurnAccel, SyncSeconds, Dt);
}

float ElysiumCam::WidenSourceFov(float SourceFovDegrees, float AspectRatio)
{
	if (SourceFovDegrees <= 0.0f || AspectRatio <= 0.0f
		|| FMath::IsNearlyEqual(AspectRatio, SourceFovAspect, 0.001f))
	{
		return SourceFovDegrees;
	}
	const float HalfTan = FMath::Tan(FMath::DegreesToRadians(
		FMath::Clamp(SourceFovDegrees, 1.0f, 179.0f) * 0.5f));
	const float Widened = 2.0f * FMath::RadiansToDegrees(
		FMath::Atan(HalfTan * AspectRatio / SourceFovAspect));
	return FMath::Clamp(Widened, 1.0f, 170.0f);
}

int32 FElysiumCameraShotStack::Push(const FElysiumCameraShot& Shot)
{
	FEntry& Entry = Shots.AddDefaulted_GetRef();
	Entry.Id = NextId++;
	Entry.Shot = Shot;
	RampSeconds = Shot.BlendSeconds;
	return Entry.Id;
}

bool FElysiumCameraShotStack::Update(int32 Id, const FElysiumCameraShot& Shot)
{
	for (FEntry& Entry : Shots)
	{
		if (Entry.Id == Id)
		{
			// The ramp already in flight belongs to the push, not to the refresh: a `Follow` shot
			// re-resolving its origin every frame must not restart its own blend.
			const float Blend = Entry.Shot.BlendSeconds;
			Entry.Shot = Shot;
			Entry.Shot.BlendSeconds = Blend;
			return true;
		}
	}
	return false;
}

bool FElysiumCameraShotStack::Pop(int32 Id, float BlendOutSeconds)
{
	const int32 Index = Shots.IndexOfByPredicate([Id](const FEntry& E) { return E.Id == Id; });
	if (Index == INDEX_NONE)
	{
		return false;
	}
	const bool bWasTop = Index == Shots.Num() - 1;
	const float Blend = BlendOutSeconds >= 0.0f
		? BlendOutSeconds
		: Shots[Index].Shot.BlendSeconds;
	Shots.RemoveAt(Index);
	if (bWasTop)
	{
		// Fading out (or handing over to whatever is underneath) takes as long as arriving did.
		RampSeconds = Blend;
	}
	return true;
}

void FElysiumCameraShotStack::Clear()
{
	Shots.Reset();
	Weight = 0.0f;
	RampSeconds = 0.5f;
}

const FElysiumCameraShot* FElysiumCameraShotStack::Find(int32 Id) const
{
	const FEntry* Entry = Shots.FindByPredicate([Id](const FEntry& E) { return E.Id == Id; });
	return Entry ? &Entry->Shot : nullptr;
}

void FElysiumCameraShotStack::Advance(float DeltaSeconds)
{
	const float Dt = FMath::Max(0.0f, DeltaSeconds);
	const float Target = Shots.Num() > 0 ? 1.0f : 0.0f;

	// A *timed* ramp, not the toggle's fixed rate: a scripted camera is given a duration, which is
	// what makes a cutscene's cut land on the beat it was authored for.
	if (RampSeconds <= 0.0f)
	{
		Weight = Target;
		return;
	}
	const float Rate = 1.0f / RampSeconds;
	Weight = Target > Weight
		? FMath::Min(Target, Weight + Rate * Dt)
		: FMath::Max(Target, Weight - Rate * Dt);
}

FString FElysiumCameraShotStack::Describe() const
{
	if (Shots.Num() == 0)
	{
		return FString::Printf(TEXT("no shot (weight %.2f)"), Weight);
	}
	FString Out = FString::Printf(TEXT("weight %.2f, %d shot(s):"), Weight, Shots.Num());
	for (int32 i = Shots.Num() - 1; i >= 0; --i)
	{
		Out += FString::Printf(TEXT("\n  %s#%d %s  origin %s  fov %.1f"),
			i == Shots.Num() - 1 ? TEXT("* ") : TEXT("  "),
			Shots[i].Id,
			Shots[i].Shot.DebugName.IsEmpty() ? TEXT("(unnamed)") : *Shots[i].Shot.DebugName,
			*Shots[i].Shot.Origin.ToCompactString(),
			Shots[i].Shot.FieldOfView);
	}
	return Out;
}

// The cvar surface

TArrayView<const ElysiumCam::FCvarDef> ElysiumCam::CvarDefs()
{
	// Defaults are the values `client.dll` registers, typed exactly as a `config.cfg` carries them.
	// `camera_prefs` / `camera_weaponswitch` are FCVAR_ARCHIVE and only become meaningful once weapons
	// exist; they are declared so an archived value survives round-tripping a user's cfg.
	static const FCvarDef Defs[] =
	{
		{ TEXT("cam_idealdist"),          TEXT("85"),  TEXT("desired boom length, Source units") },
		{ TEXT("cam_targetangle"),        TEXT("15"),  TEXT("camera pitch offset above the eye line") },
		{ TEXT("cam_yaw"),                TEXT("0"),   TEXT("yaw offset from the view direction") },
		{ TEXT("camfeed_yaw"),            TEXT("50"),  TEXT("ordinary feed orbit yaw, degrees per second") },
		{ TEXT("camfeed_yaw_end"),        TEXT("2"),   TEXT("registered; unused by the ordinary feed solve") },
		{ TEXT("camfeed_pitch"),          TEXT("80"),  TEXT("ordinary feed pitch curve amplitude") },
		{ TEXT("camfeed_pitch_min"),      TEXT("0"),   TEXT("registered; unused by the ordinary feed solve") },
		{ TEXT("camfeed_pitch_max"),      TEXT("60"),  TEXT("ordinary feed pitch clamp") },
		{ TEXT("camfeed_pitch_pow1"),     TEXT("2"),   TEXT("ordinary feed pitch exponential base") },
		{ TEXT("camfeed_pitch_pow2"),     TEXT("-0.35"), TEXT("ordinary feed pitch exponential time coefficient") },
		{ TEXT("camfeed_roll"),           TEXT("0"),   TEXT("ordinary feed view roll") },
		{ TEXT("camfeed_forward_base"),   TEXT("-50"), TEXT("ordinary feed dolly base, Source units") },
		{ TEXT("camfeed_forward_pow"),    TEXT("0.5"), TEXT("ordinary feed dolly time exponent") },
		{ TEXT("cam_idealyaw"),           TEXT("0"),   TEXT("registered; read in no recovered path") },
		{ TEXT("cam_idealpitch"),         TEXT("0"),   TEXT("registered; read in no recovered path") },
		{ TEXT("cam_snapto"),             TEXT("0"),   TEXT("registered; role unverified") },
		{ TEXT("cam_collide"),            TEXT("1"),   TEXT("run the boom collision trace") },
		{ TEXT("cam_trace_radius"),       TEXT("9"),   TEXT("half-extent of the boom hull trace") },
		{ TEXT("cam_fadestart"),          TEXT("32"),  TEXT("player model fully visible at/above this distance") },
		{ TEXT("cam_fadeend"),            TEXT("18"),  TEXT("player model fully hidden at/below this distance") },
		{ TEXT("cl_rollangle"),           TEXT("2"),   TEXT("strafe view bank, degrees at full speed") },
		{ TEXT("cl_rollspeed"),           TEXT("200"), TEXT("sideways speed at which the bank reaches cl_rollangle") },
		{ TEXT("cl_waterdist"),           TEXT("4"),   TEXT("clearance the view keeps from the water plane, Source units") },
		{ TEXT("default_fov"),            TEXT("75"),  TEXT("player horizontal field of view at the 4:3 reference (Hor+)") },
		{ TEXT("viewmodel_fov"),          TEXT("54"),  TEXT("first-person viewmodel field of view at the 4:3 reference") },
		{ TEXT("c_mindistance"),          TEXT("30"),  TEXT("boom length clamp, minimum") },
		{ TEXT("c_maxdistance"),          TEXT("200"), TEXT("boom length clamp, maximum") },
		{ TEXT("c_minpitch"),             TEXT("0"),   TEXT("orbit pitch clamp, minimum") },
		{ TEXT("c_maxpitch"),             TEXT("90"),  TEXT("orbit pitch clamp, maximum") },
		{ TEXT("c_minyaw"),               TEXT("-135"),TEXT("orbit yaw clamp, minimum") },
		{ TEXT("c_maxyaw"),               TEXT("135"), TEXT("orbit yaw clamp, maximum") },
		{ TEXT("cdamp_on"),               TEXT("1"),   TEXT("enable the spring damper on the final position") },
		{ TEXT("cdamp_hookesconstant"),   TEXT("4.0"), TEXT("spring constant, free camera") },
		{ TEXT("cdamp_hookesconstantwall"),TEXT("15.0"),TEXT("spring constant while wall-clipped (stiffer)") },
		{ TEXT("cdamp_springlength"),     TEXT("0.1"), TEXT("spring rest length") },
		{ TEXT("cdamp_maxdist"),          TEXT("50.0"),TEXT("damper clamp") },
		{ TEXT("cam_command"),            TEXT("0"),   TEXT("one-shot mode request (1 = third, 2 = first)") },
		{ TEXT("camera_weaponswitch"),    TEXT("1"),   TEXT("archive -- auto-switch camera on weapon change (4.9)") },
		{ TEXT("camera_prefs"),           TEXT("6"),   TEXT("archive -- per-weapon-class bitmask, set bit = first person (4.9)") },
	};
	return MakeArrayView(Defs);
}

void FElysiumCameraCvars::LoadFrom(TFunctionRef<FString(const TCHAR*)> Lookup)
{
	const auto Num = [&Lookup](const TCHAR* Name, float Default) -> float
	{
		const FString V = Lookup(Name);
		return V.IsEmpty() ? Default : FCString::Atof(*V);
	};
	const auto Flag = [&Lookup](const TCHAR* Name, bool Default) -> bool
	{
		const FString V = Lookup(Name);
		return V.IsEmpty() ? Default : FCString::Atoi(*V) != 0;
	};

	IdealDist   = Num(TEXT("cam_idealdist"), 85.0f) * ElysiumCam::U;
	MinDistance = Num(TEXT("c_mindistance"), 30.0f) * ElysiumCam::U;
	MaxDistance = Num(TEXT("c_maxdistance"), 200.0f) * ElysiumCam::U;
	TargetAngle = Num(TEXT("cam_targetangle"), 15.0f);
	Yaw         = Num(TEXT("cam_yaw"), 0.0f);

	MinPitch = Num(TEXT("c_minpitch"), 0.0f);
	MaxPitch = Num(TEXT("c_maxpitch"), 90.0f);
	MinYaw   = Num(TEXT("c_minyaw"), -135.0f);
	MaxYaw   = Num(TEXT("c_maxyaw"), 135.0f);

	bCollide    = Flag(TEXT("cam_collide"), true);
	TraceRadius = Num(TEXT("cam_trace_radius"), 9.0f) * ElysiumCam::U;

	FadeStart = Num(TEXT("cam_fadestart"), 32.0f) * ElysiumCam::U;
	FadeEnd   = Num(TEXT("cam_fadeend"), 18.0f) * ElysiumCam::U;

	RollAngle = Num(TEXT("cl_rollangle"), 2.0f);
	RollSpeed = Num(TEXT("cl_rollspeed"), 200.0f) * ElysiumCam::U;
	WaterDist = Num(TEXT("cl_waterdist"), 4.0f) * ElysiumCam::U;

	// Degrees, unconverted, and clamped the way Source clamps a `fov` write. They stay 4:3-referenced
	// here; `ElysiumCam::WidenSourceFov` widens them to the window at the point of use.
	DefaultFov   = FMath::Clamp(Num(TEXT("default_fov"), 75.0f), 20.0f, 120.0f);
	ViewmodelFov = FMath::Clamp(Num(TEXT("viewmodel_fov"), 54.0f), 20.0f, 120.0f);

	bDampOn            = Flag(TEXT("cdamp_on"), true);
	HookesConstant     = Num(TEXT("cdamp_hookesconstant"), 4.0f);
	HookesConstantWall = Num(TEXT("cdamp_hookesconstantwall"), 15.0f);
	SpringLength       = Num(TEXT("cdamp_springlength"), 0.1f) * ElysiumCam::U;
	DampMaxDist        = Num(TEXT("cdamp_maxdist"), 50.0f) * ElysiumCam::U;

	FeedYaw         = Num(TEXT("camfeed_yaw"), 50.0f);
	FeedYawEnd      = Num(TEXT("camfeed_yaw_end"), 2.0f);
	FeedPitch       = Num(TEXT("camfeed_pitch"), 80.0f);
	FeedPitchMin    = Num(TEXT("camfeed_pitch_min"), 0.0f);
	FeedPitchMax    = Num(TEXT("camfeed_pitch_max"), 60.0f);
	FeedPitchPow1   = FMath::Max(UE_SMALL_NUMBER, Num(TEXT("camfeed_pitch_pow1"), 2.0f));
	FeedPitchPow2   = Num(TEXT("camfeed_pitch_pow2"), -0.35f);
	FeedRoll        = Num(TEXT("camfeed_roll"), 0.0f);
	FeedForwardBase = Num(TEXT("camfeed_forward_base"), -50.0f) * ElysiumCam::U;
	FeedForwardPow  = Num(TEXT("camfeed_forward_pow"), 0.5f);

	// The boom length clamp is the authored one; an ideal outside it is the clamp's business, not a
	// silent correction of the user's cvar.
	MinDistance = FMath::Max(0.0f, MinDistance);
	MaxDistance = FMath::Max(MinDistance, MaxDistance);
}
