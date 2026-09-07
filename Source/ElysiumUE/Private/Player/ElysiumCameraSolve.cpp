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

bool ElysiumCam::SolveExposureClamp(const FElysiumShotPresentation& Presentation, float& OutMin,
	float& OutMax)
{
	if (!Presentation.bClampExposure)
	{
		return false;
	}
	// One value in both ends is what "clamped" means: the auto-exposure loop has no range left to
	// travel, so a bright emissive panel filling the frame cannot ramp the rest of the shot down.
	OutMin = Presentation.ExposureBrightness;
	OutMax = Presentation.ExposureBrightness;
	return true;
}

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
	const FElysiumShotPresentation& Shot, const FElysiumShotDrawState& State)
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

	// **The `m_bDrawPlayer` short-circuit** — `C_BasePlayer::ShouldDrawLocalPlayer` `FUN_100a7a50`,
	// read off the listing by RC11:
	//
	//     cine = GetCineCamera();
	//     if (cine) return cine->m_bDrawPlayer;                 // 0x464, FUN_10001990
	//     if (IsLocalPlayerEntity() && !CAM_IsThirdPerson()) return false;
	//     return true;
	//
	// `0x464` is read **exactly once in the whole image**, here; the test does **not** call
	// `IsActive`, so a merely *adopted* camera answers; and it short-circuits the entire third-person
	// disjunction below it rather than ANDing with it. The `IsLocalPlayer && !thirdPerson` arm above
	// therefore applies only when no cine camera is adopted.
	//
	// The alpha follows the gate rather than the band, and that is not a second opinion: the band is a
	// function of the **third-person boom** (`CInput+0x104`), and an adopted cine camera skips slot
	// 31's boom entirely (`0x100d4040`), so there is no boom for it to read. A shot that asks for the
	// body — 27 of the 51 shipped directors do, `sp_tutorial_1`'s `feedcamera` among them — gets the
	// whole body, not a body faded out by a boom that never ran.
	// Boolean, both directions. No attachment-alpha consumer is recovered, so a world weapon pops
	// rather than fading; inventing a fade here would be inventing cinematography.
	Out.bWorldWeaponEligible = Out.bThirdPerson;

	if (State.bCineAdopted)
	{
		Out.bBodyEligible = Shot.bDrawPlayerBody;
		Out.BodyAlpha = Shot.bDrawPlayerBody ? 1.0f : 0.0f;
		// **The world weapon takes the same answer, because it asks the same function.** The carried
		// weapon and the player's owned attachments are gated by `ShouldDrawLocalPlayer` too
		// (`0x100a7a50` reached from `0x100aef40`), so the short-circuit is not the body's alone: a
		// shot that draws the body draws what it is holding, and one that does not hides both.
		Out.bWorldWeaponEligible = Shot.bDrawPlayerBody;
	}

	// **The viewmodel gate is the cine camera's, not the mode predicate's, whenever one is adopted.**
	// `C_BasePlayer::ShouldHideViewModel` (slot 174, `FUN_100a7ab0`) is `cine && !ShotWantsViewmodel`,
	// and `FUN_100019a0` is
	//
	//     ShotWantsViewmodel() = (m_flSpeed <= 1.0f) && (flags & 0x40 DrawViewmodel)
	//
	// — the shot opted in **and** the camera has stopped dollying, `1.0` being exactly the tracker's
	// own speed floor (`MinTrackSpeed`), which is why `IsDollying()` is a clean read of it. So a
	// `DrawViewmodel` shot pops the hands back on only after its dolly parks, which is what the
	// shipped interaction shots (the terminals, the gym beat) are authored against.
	//
	// With no cine camera adopted the mode predicate decides, which is the other retail gate
	// (`ShouldDrawViewModel` `0x10198ea0`, `CInput` slot `+0x74`): the last frame of a third->first
	// return is the first frame every term is false, and that is the frame retail resumes the hands
	// on. An epsilon there would resume them early and break the table.
	Out.bViewmodelEligible = State.bCineAdopted
		? (Shot.bDrawViewmodel && !State.bDollying)
		: !Out.bThirdPerson;

	Out.Reticle = Out.bThirdPerson
		? EElysiumReticlePath::ThirdPerson : EElysiumReticlePath::FirstPerson;

	// **Latched, never solved** (M14). Retail issues `HideHud(0xa06d)` / `ShowHud(0xa06d)` on the
	// shot-index edge and on going inactive, and enforces nothing per frame — so this copies whatever
	// `FElysiumShotHudGate` last latched. Solving it from the live shot's keys here, as the port used
	// to, re-issued the decision every frame and could not express "replaced by another HUD-hiding
	// shot, no second call".
	Out.bShowHud = State.bHudVisible;

	return Out;
}

// The shot-change edges (`OnDataChanged` `0x100024c0`'s first two arms)

bool FElysiumShotStartEdges::OnDataChanged(const FElysiumCameraShot& Shot,
	FElysiumScriptedShotTracker& Tracker)
{
	// Arm 1 — a new `m_nClientResetFrame`. It says only "a shot started": the tracker re-seeds its
	// pose, and the aim's acquire state is left exactly as it was. A re-shot of the same record
	// therefore re-seeds without re-acquiring, which is the whole reason retail carries two fields.
	if (Shot.ResetFrame != CachedResetFrame)
	{
		CachedResetFrame = Shot.ResetFrame;
		bShotStartPending = true;
	}

	// Arm 2 — a new `m_ShotIndex`. **Either** the one-shot snap **or** the three angle-settled flags,
	// never both, and never the pose: a shot swapped under a live camera cuts its aim (or re-acquires
	// it at the tight 1-degree band) while the camera keeps dollying from where it is.
	if (Shot.ShotIndex == CachedShotIndex)
	{
		return false;
	}
	CachedShotIndex = Shot.ShotIndex;
	if (Shot.bSnapOnShotChange)
	{
		Tracker.bSnapPending = true;
	}
	else
	{
		Tracker.bPitchSettled = false;
		Tracker.bYawSettled = false;
		Tracker.bRollSettled = false;
	}
	return true;
}

// The HUD edge (`OnDataChanged` `0x100024c0`'s HUD arms, the destructor `FUN_10001920`)

bool FElysiumShotHudGate::OnDataChanged(bool bActive, int32 ShotIndex, bool bShotShowsHud)
{
	bool bIssued = false;

	// `if (m_ShotIndexCache != m_ShotIndex) { ... ShowHud / HideHud ... }` — the **only** hide site in
	// the image. A value shot carries `INDEX_NONE` and authors no `ShowHud` key, so a `camera_track`
	// can neither hide the HUD nor, by replacing a named shot, put it back: it is not a shot record
	// and does not change `m_ShotIndex`.
	if (ShotIndex != ShotIndexCache)
	{
		ShotIndexCache = ShotIndex;
		// **Issued only when the state actually changes.** Retail's arm is unconditional, and calling
		// `HideHud(0xa06d)` on an already-hidden element set does nothing at all — `SetVisible(false)`
		// on an invisible element. The port's HUD is a published boolean rather than a walk over an
		// element list, so the state edge *is* the call, and M14's contract ("a HUD-hiding shot
		// replaced by another HUD-hiding shot does not re-issue the call") is exactly this test.
		if (ShotIndex != INDEX_NONE && bHudVisible != bShotShowsHud)
		{
			bHudVisible = bShotShowsHud;
			++Issued;
			bIssued = true;
		}
	}

	// `if (!IsActive() && m_bWasActive) gHUD->ShowHud(0xa06d);` — the restore when the shot stops
	// being live at all, whatever hid it. Same rule: nothing is issued for a HUD that is already up.
	if (!bActive && bWasActive)
	{
		if (!bHudVisible)
		{
			bHudVisible = true;
			++Issued;
			bIssued = true;
		}
		// Retail's caches live on `C_BaseCineCamera`, and the camera **entity is destroyed** when the
		// shot ends (`SetCineCamera(NULL)`, `UTIL_Remove`), so the next adoption starts with a fresh
		// `-1` cache and re-issues its own hide. The port's gate outlives the shot, so the entity's
		// death is reproduced here: without this, re-running the same shot would find its index
		// already cached and leave the HUD up.
		ShotIndexCache = INDEX_NONE;
	}
	bWasActive = bActive;
	return bIssued;
}

bool FElysiumShotHudGate::OnDestroyed()
{
	// `if ((rec[0x20] & 0x200) == 0) gHUD->ShowHud(0xa06d);` — the destructor restores only what a
	// shot had taken down. Nothing is issued when the HUD was already up.
	ShotIndexCache = INDEX_NONE;
	bWasActive = false;
	if (bHudVisible)
	{
		return false;
	}
	bHudVisible = true;
	++Issued;
	return true;
}

// `AutoPositionFromTarget` (`0x1006fa50`-`0x1006fb85`) and its closest-point kernel (`FUN_1013c940`)

namespace
{
	// `_DAT_1046a5e4 = 1e-05f`, the degenerate-length guard on `dir . dir`. Retail's units are Source
	// units and the port's are cm, so the guard converts with them: it is a squared length.
	constexpr float ClosestPointLengthEpsilon = 1e-05f * ElysiumCam::U * ElysiumCam::U;

	// `_DAT_104454d0 = 0.5` and `_DAT_1044eb08 = 0.0174532924`, both read out of `vampire.dll`. The
	// half-FOV is written as retail writes it — a multiply by 0.5 and then by a stored DEG2RAD — so
	// the arithmetic is the same one the five flagged shipped shots were authored against.
	constexpr float HalfFovScale = 0.5f;
	constexpr float DegreesToRadians = 0.0174532924f;
}

float ElysiumCam::ClosestPointParameterOnLine(const FVector& P, const FVector& A, const FVector& B)
{
	const FVector Dir = B - A;
	const double LengthSquared = Dir.SizeSquared();
	if (LengthSquared < ClosestPointLengthEpsilon)
	{
		// `_DAT_104454c4 = 0.0f`. The closest point is then `A` itself, which for the one caller means
		// the look-at point.
		return 0.0f;
	}
	// **No clamp** (RC1): the listing's only branch is the guard above, and there is no `FCOM` against
	// `0.0` or `1.0` in the body. `t` outside `[0,1]` is a legal answer and the caller wants it —
	// clamping would report a strictly larger perpendicular distance for a target point that projects
	// behind the camera or beyond the look-at, and back the camera further off than retail does.
	return static_cast<float>(FVector::DotProduct(P - A, Dir) / LengthSquared);
}

FVector ElysiumCam::ClosestPointOnLine(const FVector& P, const FVector& A, const FVector& B)
{
	return A + (B - A) * ClosestPointParameterOnLine(P, A, B);
}

FVector ElysiumCam::AutoPositionFromTarget(const FVector& CamOrigin, const FVector& LookAt,
	const FVector& Point1, const FVector& Point2, float FieldOfViewDeg)
{
	// `if (P2.z > P1.z) swap(P1, P2)` at `0x1006f9e6` (`FCOMP` + `AND EAX,0x4100` + a JNZ that skips
	// the swap when `P2.z <= P1.z`): P1 ends **high**, P2 ends **low**, and only the low one is used
	// from here on. The high point exists solely to decide which is which, which is why feeding the
	// two points in either order gives one answer.
	const FVector Low = Point2.Z > Point1.Z ? Point1 : Point2;

	const float HalfFovRadians = FieldOfViewDeg * HalfFovScale * DegreesToRadians;
	const float SinHalfFov = FMath::Sin(HalfFovRadians);
	if (!(SinHalfFov > 0.0f))
	{
		// Unreachable through the parser — `FieldOfView` is clamped to `[20,120]`, so the half-angle is
		// `[10,60]` degrees — but a producer that builds a shot by hand can reach it, and retail's
		// unguarded divide would answer an infinity that then propagates into the origin.
		return CamOrigin;
	}

	// `C = ClosestPointOnLine(P2, lookAt, camOrigin)`, the **infinite** camera->look-at axis (RC1), so
	// `d` is the true perpendicular distance from the lower target point to it.
	const FVector Closest = ClosestPointOnLine(Low, LookAt, CamOrigin);
	const float D = static_cast<float>((Closest - Low).Size());

	// `h = d / sin(A); r = sqrt(h*h + d*d)`. **Not** the tight `d / tan(A)` fit: retail takes the
	// hypotenuse and then adds a second `d` in quadrature, so the camera always sits further back than
	// an exact frame. That over-shoot is the recovered arithmetic and is reproduced, not corrected.
	const float H = D / SinHalfFov;
	const float R = FMath::Sqrt(H * H + D * D);

	// `camOrigin = lookAt - normalize(lookAt - camOrigin) * r`: the camera slides along its own
	// existing axis to distance `r` from the look-at, keeping the direction the anchors chose.
	FVector Axis = LookAt - CamOrigin;
	if (!Axis.Normalize())
	{
		// The camera standing on its own look-at has no axis to slide along; retail's `VectorNormalize`
		// leaves a zero direction here and the result would be the look-at point itself. The origin
		// stands instead — the one guard on this path, named as such.
		return CamOrigin;
	}
	return LookAt - Axis * R;
}

// The scripted composition (`CInput::OverrideView`, `client.dll` FUN_100ffb90)

FVector ElysiumCam::ScriptedShotTargetPoint(const FVector& ShotLocation, const FRotator& ShotRotation)
{
	return ShotLocation + ShotRotation.Vector() * ViewForwardPointCm;
}

void ElysiumCam::ComposeScriptedShot(FVector& InOutLocation, FRotator& InOutRotation, float& InOutFov,
	const FVector& ShotLocation, const FVector& ShotTarget, float ShotRoll, float ShotFov,
	float Weight)
{
	// `if (0.0f < m_flScriptedWeight)` — the whole body is inside that test, which is what makes the
	// layer safe to run unconditionally.
	if (Weight <= 0.0f)
	{
		return;
	}

	// **The ease is here, not in the ramp.** `FUN_100fc900`'s tail is linear; `FUN_100ffb90` opens
	// with `e = SimpleSpline(w)`. Easing at both ends would ease twice over an authored duration.
	const float E = SimpleSpline(Weight);

	// `AngleVectors(angles, fwd)` then `viewFwdPoint = origin + fwd * 240u`: a stand-in for "what the
	// base view is looking at", so the aim can be interpolated as a **point** rather than an angle.
	const FVector ViewForwardPoint = InOutLocation + InOutRotation.Vector() * ViewForwardPointCm;

	InOutLocation = InOutLocation + (ShotLocation - InOutLocation) * E;

	const FVector Direction = (ViewForwardPoint + (ShotTarget - ViewForwardPoint) * E) - InOutLocation;
	// `VectorNormalize` then `VectorAngles`. A degenerate direction — the lerped aim point landing on
	// the lerped origin — has no angle to derive, so the previous one stands; retail's `VectorAngles`
	// answers yaw 0 there, which would snap the view sideways for one frame.
	if (!Direction.IsNearlyZero())
	{
		const FRotator Aimed = Direction.Rotation();
		InOutRotation.Pitch = Aimed.Pitch;
		InOutRotation.Yaw = Aimed.Yaw;
	}
	// `angles->roll = e * m_flOverrideRoll` — an assignment, not a lerp: the base view's roll (the
	// strafe bank, a shake) is discarded outright the instant the weight is non-zero.
	InOutRotation.Roll = E * ShotRoll;

	// `FLD [ESI+0x198] ; FSUB [EAX] ; FMUL e ; FADD [EAX] ; FSTP [EAX]` (`0x100ffcef`) — **no test**.
	// The lerp is unconditional because `m_flCameraFOVOverride` cannot be 0 in retail: the published
	// value comes from `CBaseEntity`'s slot `0xC4`, whose default is 75.0 (`0x10026830` ->
	// `_DAT_104454cc`). A producer that hands this 0 is asking for the view to be pulled to a zero
	// lens, and it gets it; seeding retail's 75 where a value shot authors no FOV is the pusher's
	// job, done at the two publish sites (`UElysiumCameraComponent::ScriptedShotView`,
	// `UElysiumCameraService::ApplyToView`).
	InOutFov = InOutFov + (ShotFov - InOutFov) * E;
}

void ElysiumCam::ComposeScriptedShot(FVector& InOutLocation, FRotator& InOutRotation, float& InOutFov,
	const FVector& ShotLocation, const FRotator& ShotRotation, float ShotFov, float Weight)
{
	ComposeScriptedShot(InOutLocation, InOutRotation, InOutFov, ShotLocation,
		ScriptedShotTargetPoint(ShotLocation, ShotRotation),
		static_cast<float>(ShotRotation.Roll), ShotFov, Weight);
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

// `RemainingTime` (`FUN_100010f0`), verbatim from the listing `0x100010f0`-`0x100011bb` — M6.
//
// The decompile is unusable, so this is the listing's three arms in order. **Both of retail's defects
// are reproduced deliberately**, because the value is a tuning constant every shipped
// `SyncRotateOnMove` shot was authored against, not a physical answer:
//
//   * the trapezoid arm's acceleration distance is `(vmax - v)^2/(2a)`, which is the correct
//     `(vmax^2 - v^2)/(2a)` only when `v == 0`;
//   * the triangle arm's radicand is `2a - 0.5*(d - R2)` — `FUN_100010e0(a, 0.5*(d - R2))`, an
//     acceleration minus a distance. It is dimensionally inconsistent and goes negative whenever
//     `d - R2 > 4a`, which NaNs `vpeak` and the turn rate that divides by it.
//
// For `jack.txt` (MoveSpeed 500, MoveAccel 250) closing 100 u from rest retail answers **0.17 s**
// where a correct kinematic solve answers 1.27 s, so the port's previous correct solve panned the
// game's most-seen camera about 7x slower than retail, across 32 more `SyncRotateOnMove` shots.
//
// The **one** divergence is the clamp on the radicand, which removes only the NaN state: the band
// needs `MoveSpeed > 2*MoveAccel` and no shipped `SyncRotateOnMove` shot has it.
//
// Units cancel, so cm and Source units both work; the port calls it in cm.
float ElysiumCam::RemainingTranslationSeconds(float Speed, float MaxSpeed, float Accel, float Distance)
{
	// `FUN_100010b0(v, a) = v*v / (2*a)`. Retail divides unguarded; the callers below are only ever
	// reached from `FUN_10001c80`, whose own `MoveAccel == 0` case never gets this far (the position
	// solve pins the speed at the floor and never leaves the decel arm). The guard is here so no
	// hardware divide-by-zero is ever executed — the same rule M7 applies to the position solve.
	if (Accel <= 0.0f)
	{
		return 0.0f;
	}
	const float TwoA = 2.0f * Accel;
	const float R1 = (MaxSpeed * MaxSpeed) / TwoA;   // distance to stop from vmax
	const float R2 = (Speed * Speed) / TwoA;         // distance to stop from the current speed

	if (2.0f * R1 < Distance)
	{
		// The trapezoid: accelerate, cruise, decelerate. `X` is retail's `(vmax - v)^2/(2a) + R1`.
		const float X = ((MaxSpeed - Speed) * (MaxSpeed - Speed)) / TwoA + R1;
		return (Distance - X) / MaxSpeed                       // cruise
			+ FMath::Abs((MaxSpeed - Speed) / Accel)           // accelerate
			+ FMath::Abs((0.0f - MaxSpeed) / Accel);           // decelerate
	}
	if (R2 >= Distance)
	{
		// Already inside the stopping distance: all that is left is the stop.
		return FMath::Abs((0.0f - Speed) / Accel);
	}
	// The triangle: the peak stays below vmax. `FUN_100010e0(a, b) = sqrt(2*a - b)` with
	// `b = 0.5*(d - R2)` — the defect. Clamped at zero so the NaN state cannot be produced.
	const float Radicand = FMath::Max(0.0f, TwoA - 0.5f * (Distance - R2));
	const float PeakSpeed = Speed + FMath::Sqrt(Radicand);
	return FMath::Abs((PeakSpeed - Speed) / Accel) + FMath::Abs((0.0f - PeakSpeed) / Accel);
}

namespace
{
	// `Approach` (`FUN_10001070`), verbatim from the listing `0x10001070`-`0x100010ae`:
	//
	//     cur > goal -> cur - rate*dt        (`FSUBR`, 0x10001081, no clamp at the goal)
	//     cur < goal -> cur + rate*dt        (`FADD`,  0x1000109d, no clamp at the goal)
	//     otherwise  -> goal
	//
	// **Nothing clamps.** A step that overshoots the goal stays overshot, and a decelerating value
	// walks straight through zero into negative territory — which is exactly what makes a zero `rate`
	// return the input unchanged on every arm, the behaviour `TurnAccel == 0` depends on.
	//
	// (Named for the address rather than `Approach`, because the module builds with unity on and
	// `UElysiumCameraService`'s own file-local `Approach` is a different function.)
	double ApproachUnclamped(double Current, double Goal, double Rate, double Dt)
	{
		if (Current > Goal) { return Current - Rate * Dt; }
		if (Current < Goal) { return Current + Rate * Dt; }
		return Goal;
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

		// The deadband is the shot's tolerance while the axis is parked and retail's flat **1.0
		// degree** once it is turning (`tol = m_bAngleSettled[i] ? rec->AngularTolerance[i] : 1.0f`,
		// the `_DAT_101e34ec` immediate): the camera comes out of the park only on a real drift, and
		// the acquire band is a whole degree wide, not a hair.
		const double Deadband = bInOutSettled
			? FMath::Max(Tolerance, 0.0f) : FElysiumScriptedShotTracker::UnsettledAngleTolerance;
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
			// **`TurnAccel == 0` leaves the rate exactly where it is** — retail, through a branch.
			// `FUN_10001c80` opens with `stopAngle = FUN_100010b0(rate, TurnAccel) = rate^2/(2*0)`,
			// a `0/0` NaN (rate 0) or an infinity (rate > 0); the NaN compare falls to the
			// `MaxTurnRate` arm and the infinity to the zero arm, and **both** arms end in
			// `FUN_10001070(rate, goal, 0, dt)` (`0x10001d0c`, `0x10001d32`), which returns the input
			// unchanged for a zero step whichever way it compares. So an axis whose shot authors no
			// `TurnAccel` starts at 0 from `Start`/`Snap` and stays 0: it never turns. Written as a
			// branch and never as a real divide, exactly as M7's `MoveAccel == 0` twin is.
			//
			// No shipped file authors `TurnAccel` 0 (the `CameraConstraints` default is 30 deg/s^2),
			// so nothing in the corpus takes this arm; the port's own producers can, and they get
			// retail's answer rather than a ceiling the port invented.
		}
		else
		{
			// `stopAngle = rate^2/(2*accel)`; `|delta| <= stopAngle` decelerates toward 0, otherwise
			// accelerate toward `MaxTurnRate[i]`. The compare is retail's `FCOMPP` + `TEST AH,0x41` +
			// `JP` at `0x10001cef`, which takes the decel arm on **less-or-equal** (both `0x01` and
			// `0x40` are odd parity, so only "greater" jumps to the ceiling arm).
			const double StoppingAngle = (InOutRate * InOutRate) / (2.0f * TurnAccel);
			// **Unclamped, and it is retail's** (`FUN_10001c80` returns `FUN_10001070`'s result raw
			// and `FUN_10001d40` stores it raw at `0x10001e4a` / `0x10001e91`): the rate overshoots
			// `MaxTurnRate` by one step on the way up and goes **negative** when it decelerates past
			// the goal, which for one frame walks the axis backwards. The port used to clamp to
			// `[0, MaxTurnRate]`, which is smoothing retail does not do.
			InOutRate = ApproachUnclamped(InOutRate,
				Error <= StoppingAngle ? 0.0 : static_cast<double>(MaxRate),
				static_cast<double>(TurnAccel), static_cast<double>(Dt));
		}

		const double Step = InOutRate * Dt;
		InOutCurrent = FRotator::NormalizeAxis(
			Step >= Error ? Desired : InOutCurrent + FMath::Sign(Delta) * Step);
	}
}

void FElysiumScriptedShotTracker::Start(const FElysiumCameraShot& Shot,
	const FElysiumViewSetup& LiveView)
{
	// `FUN_10002210`'s arm test, verbatim: `if ((flags & 2) == 0 || (flags & 1) != 0)`.
	if (Shot.StartsOnGoal())
	{
		// The **replicated-goal** arm — no `End`, or an authored `Start`. `m_vecCurOrigin`,
		// `m_angCurAngles` and `m_vecShotStart` all come from the goal, so the shot opens on its own
		// framing and the tracker has nothing to close.
		Location = Shot.Origin;
		Rotation = Shot.bUseLookAt ? (Shot.LookAt - Shot.Origin).Rotation() : Shot.Rotation;
		Rotation.Roll = Shot.Roll;
	}
	else
	{
		// The **live-view** arm — an `End` anchor and no `Start`, which is the shipped `jack.txt` /
		// `dialogdefault.txt` / `centerfullview.txt` shape. All three fields come from
		// `CViewRender::GetViewSetup()`, so the shot dollies in from wherever the player is actually
		// looking, at the file's own `MoveSpeed` / `MoveAccel` / `TurnAccel`. The roll is the view's;
		// the shot's own roll is reached as the aim closes, exactly as its position is.
		Location = LiveView.Location;
		Rotation = LiveView.Rotation;
	}
	// Retail touches neither `m_flFOV` (`0x458`) nor `m_flCurFov` (`0x480`) at shot start; in mode 1
	// the FOV is re-established every frame from the shot record. The port's tracker holds one FOV, so
	// seeding it from the record here is that same value one frame early and never a lerp.
	Fov = Shot.FieldOfView;
	Speed = 0.0f;
	TurnRate = FVector::ZeroVector;
	// Retail marks the position settled and zeroes the rates; the axes acquire on the first frame,
	// which with the pose already at the goal is a no-op.
	bPositionSettled = true;
	bPitchSettled = false;
	bYawSettled = false;
	bRollSettled = false;
	// The tail of `FUN_10002210`: `if (flags & 0x80) m_bSnapPending = 1;`. `OnDataChanged`'s
	// shot-index arm is the *other* site that arms the same one-shot (SC5 splits the two signals at
	// the caller), and `Advance` consumes it on the very next rendered frame.
	bSnapPending = Shot.bSnapOnShotChange;
	bSeeded = true;
}

void FElysiumScriptedShotTracker::Snap(const FElysiumCameraShot& Shot)
{
	// `FUN_10002390`, in its order. The goal is copied onto the current pose; retail also re-seeds
	// `m_vecShotStart` (0x484) and `m_vecSettledOrigin` (0x4b4), which the port derives rather than
	// stores, so there is nothing to write for those two.
	Location = Shot.Origin;
	// `m_angCurAngles = m_angCamAngles` (`0x100023c0`-`0x100023f8`), copied **unconditionally** and
	// only then overwritten from the look-at on the `CamMode == 1` arm below. The net pose is the
	// same for a tracked shot, but `Snap()` is a public entry point and a copy-through shot reaching
	// it directly must land on the replicated angles rather than keep whatever it had.
	Rotation = Shot.bUseLookAt ? (Shot.LookAt - Shot.Origin).Rotation() : Shot.Rotation;
	Rotation.Roll = Shot.Roll;
	bPositionSettled = true;
	Speed = 0.0f;
	TurnRate = FVector::ZeroVector;
	// All three axes are marked **unsettled**, not settled: the next frame acquires the aim at the
	// tight 1-degree band before it parks on the shot's own tolerance.
	bPitchSettled = false;
	bYawSettled = false;
	bRollSettled = false;
	// `if (m_CamMode == 1) VectorAngles(m_vecLookAt - m_vecCurOrigin, &m_angCurAngles);` — the
	// re-derive happens **only** for a tracked shot, and it overwrites the unconditional copy above.
	// A copy-through shot keeps the replicated angles that copy just wrote, because for it those
	// angles *are* the pose.
	if (Shot.bTracked && Shot.bUseLookAt)
	{
		Rotation = (Shot.LookAt - Location).Rotation();
		Rotation.Roll = Shot.Roll;
	}
	// The one-shot's consume.
	bSnapPending = false;
}

float FElysiumScriptedShotTracker::TrackFov(const FElysiumCameraShot& Shot, float CameraFovCvar)
{
	// The `camera_fov` guard, ahead of everything: `if (!cvar.IsCommand() && 10.0f < cvar.GetFloat())
	// return cvar.GetFloat();` (`_DAT_101e34f4` = 10.0, RC9). It returns **without writing
	// `m_flCurFov`**, so the rendered FOV freezes at its previous value instead of following the
	// cvar. That freeze is retail's behaviour and is reproduced, not smoothed over (M12). The
	// shipped default is `-1`, so nothing in a stock run takes this arm.
	if (FovOverrideThreshold < CameraFovCvar)
	{
		return CameraFovCvar;
	}
	// A straight copy from the **shot record**, every frame — never a lerp, and never the replicated
	// `m_flFOV`, which `CamMode == 1` does not consult.
	Fov = Shot.FieldOfView;
	return Fov;
}

void FElysiumScriptedShotTracker::Advance(const FElysiumCameraShot& Shot, float DeltaSeconds,
	float CameraFovCvar)
{
	if (!bSeeded)
	{
		Start(Shot);
		return;
	}
	// The frame-delta guards, `FUN_10001a20`'s own, applied to the **parameter** — the tracker never
	// reads a clock. Retail's `dt < 1/255 => 0.01` arm is also its zero-and-negative handling, which
	// is why there is no early-out: a stalled or a paused frame still advances the tracker by a flat
	// 10 ms, exactly as a retail frame with an unmoved `curtime` does.
	float Dt = DeltaSeconds;
	if (Dt > FrameDeltaCeiling)
	{
		Dt = FrameDeltaCeiling;
	}
	else if (Dt < FrameDeltaFloor)
	{
		Dt = FrameDeltaFloor;
	}

	// `if (m_bSnapPending) FUN_10002390();` — consumed at the top of the frame, ahead of the CamMode
	// dispatch, so the snap lands for a copy-through shot too.
	if (bSnapPending)
	{
		Snap(Shot);
	}

	// `CamMode != 1` (`FUN_10001a20`'s fall-through) and the `camera_track` override (`FUN_100ffb90`):
	// the pose *is* the shot, re-derived every frame. No deadband, no rate, no settle state — the
	// tracker's three functions are never entered, so a `camera_track` dolly re-aims at its authored
	// target every frame instead of freezing on the seed. Retail's copy-through writes `m_flCurFov`
	// from the replicated `m_flFOV`; the port's value shot carries the same number on the record.
	if (!Shot.bTracked)
	{
		const bool bWasSnapPending = bSnapPending;
		Start(Shot);
		bSnapPending = bWasSnapPending;
		bPitchSettled = bYawSettled = bRollSettled = true;
		return;
	}

	// Position, `FUN_10001fe0`. **`m_flDistRemaining` (`0x4c4`) is stored before anything moves**
	// (`0x10002054`, an unconditional `FST` ahead of both the settled and the unsettled arm), and it
	// is the value `SyncRotateOnMove` divides by later in this same frame.
	const FVector ToGoal = Shot.Origin - Location;
	const float Distance = static_cast<float>(ToGoal.Size());
	DistRemaining = Distance;

	// **A port seam, not retail.** Retail's parser always hands `FUN_10001fe0` a `MoveSpeed` — the
	// `CameraConstraints` default is 150 u/s and no shipped file writes 0 — so retail's position
	// solve has no zero-speed arm at all. The port has producers that push a tracked shot with no
	// constraints (the dialogue profiles stand in for `dialogdefault.txt`; SC9 gives them the file's
	// numbers), and for those "0 = the camera is not rate-limited" is the port's own grammar.
	if (Shot.MoveSpeed <= 0.0f)
	{
		Location = Shot.Origin;
		Speed = 0.0f;
		bPositionSettled = true;
	}
	else
	{
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
				// **M7 — retail, reproduced through a branch.** With `MoveAccel == 0` retail's
				// `stopDist = FUN_100010b0(v, 0) = v*v/0` makes the `stopDist < dist` compare false
				// (a `0/0` NaN compares false; an infinity is not less than a finite distance), so
				// control takes the **decel** arm: `Approach(v, 0, 0, dt)` returns `v` unchanged
				// because a zero rate moves nothing, and the clamp below pins it at the 1 u/s floor.
				// The camera crawls. Written as a branch and never as a real divide, so no NaN or
				// infinity is ever produced on this path.
			}
			else
			{
				const float StoppingDistance = (Speed * Speed) / (2.0f * Shot.MoveAccel);
				Speed += (Distance <= StoppingDistance ? -Shot.MoveAccel : Shot.MoveAccel) * Dt;
			}
			// `speed = clamp(speed, 1.0f, MoveSpeed)` (`0x10002137`-`0x1000215a`). The **floor** is
			// retail's, and it is what pins the `MoveAccel == 0` crawl at 2.54 cm/s.
			Speed = FMath::Clamp(Speed, MinTrackSpeed, Shot.MoveSpeed);
			const float Step = Speed * Dt;
			Location = Step >= Distance ? Shot.Origin : Location + (ToGoal / Distance) * Step;
		}
	}

	// Angles, `FUN_10001d40`. The desired angle is solved against the origin the position step just
	// produced, exactly as retail does — which is why a dolly re-aims as it travels.
	FRotator Desired = Shot.bUseLookAt ? (Shot.LookAt - Location).Rotation() : Shot.Rotation;
	Desired.Roll = Shot.Roll;

	// `SyncRotateOnMove` (`FUN_10001c80`): only while the position is actually travelling, and then
	// `MaxTurnRate` is bypassed so the pan lands with the dolly. The distance handed to the timing
	// solve is `m_flDistRemaining` — the **pre-move** distance the position step just stored
	// (`0x10001cb7` reads `this+0x4c4`), not the distance left after this frame's step. The floor at
	// `Dt` is the port's, and it stands where retail's unguarded `delta / T` would divide by a zero
	// `T`.
	const float SyncSeconds = (Shot.bSyncRotateOnMove && !bPositionSettled)
		? FMath::Max(ElysiumCam::RemainingTranslationSeconds(Speed, Shot.MoveSpeed, Shot.MoveAccel,
			DistRemaining), Dt)
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

	// Third and last of `FUN_10001fa0`'s steps, after position and angles.
	TrackFov(Shot, CameraFovCvar);
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

const FElysiumCameraShot* FElysiumCameraShotStack::TopCine() const
{
	for (int32 i = Shots.Num() - 1; i >= 0; --i)
	{
		if (Shots[i].Shot.bCine)
		{
			return &Shots[i].Shot;
		}
	}
	return nullptr;
}

int32 FElysiumCameraShotStack::TopCineId() const
{
	for (int32 i = Shots.Num() - 1; i >= 0; --i)
	{
		if (Shots[i].Shot.bCine)
		{
			return Shots[i].Id;
		}
	}
	return 0;
}

const FElysiumCameraShot* FElysiumCameraShotStack::TopTrack() const
{
	for (int32 i = Shots.Num() - 1; i >= 0; --i)
	{
		if (!Shots[i].Shot.bCine)
		{
			return &Shots[i].Shot;
		}
	}
	return nullptr;
}

int32 FElysiumCameraShotStack::TopTrackId() const
{
	for (int32 i = Shots.Num() - 1; i >= 0; --i)
	{
		if (!Shots[i].Shot.bCine)
		{
			return Shots[i].Id;
		}
	}
	return 0;
}

void FElysiumCameraShotStack::ArmRampIn(float Seconds)
{
	const float Duration = FMath::Max(0.0f, Seconds);
	// Inside the dead band there is no ramp to arm: `|duration| <= 0.01` reads as weight 1 and stays.
	// A zero-duration arrival is exactly that — a hard cut in.
	if (Duration <= RampDeadBandSeconds)
	{
		RampDuration = 0.0f;
		RampStartTime = RampNow;
		return;
	}
	// The back-date (`vampire.dll` `FUN_1017d0b0` re-times by moving the start, never by writing the
	// weight): solve `start` so the regime answers exactly the weight in force right now, which is
	// what makes a reversal resume instead of restarting.
	const float Current = GetTrackWeight();
	RampDuration = Duration;
	RampStartTime = RampNow - Current * Duration;
}

void FElysiumCameraShotStack::ArmRampOut(float Seconds)
{
	const float Duration = FMath::Max(0.0f, Seconds);
	// **Only `dur <= 0` clears the mark.** `FUN_1017d6d0`'s `0x1017d802` branch reaches `0x1017d87e`,
	// which zeroes `m_flCameraOverrideFadeMarkTime` and nothing else, for a non-positive duration
	// alone. Anything above zero is stored as `duration = -dur` with a live mark, and `FUN_100fc900`'s
	// tail then reads `|duration| <= 0.01` as **weight 1 that stays** (`0x100fcbf7`'s
	// `FCOMP [_DAT_10235278]` falls through only on a strict `<`). So a 5 ms release leaves retail's
	// override hard **on** — a cut in, not a cut out — and the port reproduces that rather than
	// treating a sub-dead-band release as "off".
	//
	// Corpus: all 168 shipped `camera_track` chains return with `ToPlayerTime 0`, which is the
	// `dur <= 0` arm both readings agree on, so nothing shipped changes either way.
	if (Duration <= 0.0f)
	{
		RampDuration = 0.0f;
		RampStartTime = 0.0f;
		return;
	}
	const float Current = GetTrackWeight();
	RampDuration = -Duration;
	// Inside the dead band the weight is 1 whatever the mark is, so the back-date has nothing to
	// preserve; the mark still has to be strictly positive or `startTime <= 0` would read as "off".
	RampStartTime = Duration <= RampDeadBandSeconds
		? RampNow : RampNow - (Current - 1.0f) * RampDuration;
}

int32 FElysiumCameraShotStack::Push(const FElysiumCameraShot& Shot)
{
	FEntry& Entry = Shots.AddDefaulted_GetRef();
	Entry.Id = NextId++;
	Entry.Shot = Shot;
	// A push **is** a shot start: `SetShot` stamps `m_nClientResetFrame` and the shot start
	// `FUN_1006e8e0` stamps it again, and the client's only test on it is "does it differ from the
	// cached one". The stamp is the channel's, not the pusher's, so a caller cannot forget it.
	Entry.Shot.ResetFrame = NextResetFrame++;
	// **A cine shot has no ramp.** It is adopted at full weight the instant `m_iCameraOverrideIdx`
	// changes and the client hard-writes its pose from the next frame; nothing on the channel is
	// timed, so it must not disturb the track channel's ramp either.
	if (!Shot.bCine)
	{
		ArmRampIn(Shot.BlendSeconds);
	}
	return Entry.Id;
}

bool FElysiumCameraShotStack::Update(int32 Id, const FElysiumCameraShot& Shot)
{
	for (FEntry& Entry : Shots)
	{
		if (Entry.Id == Id)
		{
			// The ramp already in flight belongs to the push, not to the refresh: a `Follow` shot
			// re-resolving its origin every frame must not restart its own blend. Which channel the
			// shot is on is settled at the push for the same reason.
			//
			// `ResetFrame` is preserved for the same reason and a stronger one: this is retail's
			// **think**, which re-publishes `m_vecCamOrigin` / `m_angCamAngles` / `m_flFOV` every tick
			// and never touches `m_nClientResetFrame`. Re-stamping it here would re-seed the tracker
			// every frame and no shot would ever dolly.
			const float Blend = Entry.Shot.BlendSeconds;
			const bool bCine = Entry.Shot.bCine;
			const int32 ResetFrame = Entry.Shot.ResetFrame;
			Entry.Shot = Shot;
			Entry.Shot.BlendSeconds = Blend;
			Entry.Shot.bCine = bCine;
			Entry.Shot.ResetFrame = ResetFrame;
			return true;
		}
	}
	return false;
}

bool FElysiumCameraShotStack::Restart(int32 Id)
{
	for (FEntry& Entry : Shots)
	{
		if (Entry.Id == Id)
		{
			// `SetShot` on a camera that already has one: the mode is cleared and re-written, the
			// anchors are re-bound and `m_nClientResetFrame` is stamped again — but the entity, and so
			// the port's id, is the same. The consumer arms shot start off the new stamp; whether the
			// aim also re-acquires is `m_ShotIndex`'s business, not this one's.
			Entry.Shot.ResetFrame = NextResetFrame++;
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
	const bool bWasCine = Shots[Index].Shot.bCine;
	const float Blend = BlendOutSeconds >= 0.0f
		? BlendOutSeconds
		: Shots[Index].Shot.BlendSeconds;
	Shots.RemoveAt(Index);

	// **M1, ruled: the release of a cine shot is a cut.** Retail returns control on the same tick the
	// camera dies — `SetImmobilized(false)`, the weapon restore and the HUD restore all land on that
	// frame — and there is no blend field anywhere on `C_BaseCineCamera` to soften it with. So
	// `BlendOutSeconds` is not consulted, and no cvar exists that could reintroduce one.
	if (bWasCine)
	{
		return true;
	}
	// The track channel: only emptying it starts the blend out. A shot popped from under another
	// track shot leaves the ramp exactly where it is, because the channel is still owned.
	if (!TopTrack())
	{
		ArmRampOut(Blend);
	}
	return true;
}

void FElysiumCameraShotStack::Clear()
{
	Shots.Reset();
	// `startTime <= 0` is retail's "the override is off", which is what a teardown wants: no residual
	// weight, and no fade left running over a map that is gone.
	RampStartTime = 0.0f;
	RampDuration = 0.0f;
	// The clock restarts with the map. Retail's `curtime` is a float too and resets on a level change;
	// letting a single-precision accumulator run for hours would coarsen a 0.35 s ramp to its own
	// resolution, and there is nothing live across a teardown for the reset to disturb.
	RampNow = 1.0f;
}

const FElysiumCameraShot* FElysiumCameraShotStack::Find(int32 Id) const
{
	const FEntry* Entry = Shots.FindByPredicate([Id](const FEntry& E) { return E.Id == Id; });
	return Entry ? &Entry->Shot : nullptr;
}

void FElysiumCameraShotStack::Advance(float DeltaSeconds)
{
	// The clock, and nothing else. `FUN_100fc900` recomputes the weight from `engine->GetCurTime()`
	// and the two stored fields every frame rather than integrating it, which is why re-timing a fade
	// is a write to `startTime` and never a write to the weight.
	RampNow += FMath::Max(0.0f, DeltaSeconds);
}

float FElysiumCameraShotStack::GetTrackWeight() const
{
	// `FUN_100fc900`'s tail, in its own order.
	if (RampStartTime <= 0.0f)
	{
		return 0.0f;
	}
	float W = 1.0f;
	if (RampDuration > RampDeadBandSeconds)
	{
		W = (RampNow - RampStartTime) / RampDuration;
	}
	else if (RampDuration < -RampDeadBandSeconds)
	{
		W = 1.0f + (RampNow - RampStartTime) / RampDuration;
	}
	// Otherwise `|duration| <= 0.01`: the weight stays at the 1 written above — a hard cut in.
	const float Clamped = FMath::Clamp(W, 0.0f, 1.0f);

	// The clock is accumulated from frame deltas rather than read, so a ramp that has just finished
	// can land a float epsilon short of its endpoint. Snapping that last epsilon is what keeps
	// "the channel is idle" and "the shot is fully arrived" boolean rather than thresholds — a
	// residual 6e-8 of weight is a dead camera still composed over a live player.
	if (Clamped <= UE_KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}
	return Clamped >= 1.0f - UE_KINDA_SMALL_NUMBER ? 1.0f : Clamped;
}

FString FElysiumCameraShotStack::Describe() const
{
	const float Weight = GetWeight();
	if (Shots.Num() == 0)
	{
		return FString::Printf(TEXT("no shot (weight %.2f)"), Weight);
	}
	FString Out = FString::Printf(TEXT("weight %.2f (cine %d, track %.2f), %d shot(s):"),
		Weight, TopCine() != nullptr ? 1 : 0, GetTrackWeight(), Shots.Num());
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
		{ TEXT("camera_fov"),             TEXT("-1"),  TEXT("dev: above 10, the scripted-shot FOV FREEZES rather than following it (M12)") },
		{ TEXT("c_orthowidth"),           TEXT("100"), TEXT("archive -- camortho view width, Source units") },
		{ TEXT("c_orthoheight"),          TEXT("100"), TEXT("archive -- camortho view height, Source units") },
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

	// `camera_fov` is **not** clamped like the two above: retail reads it raw and compares it against
	// 10, and its own default `-1` is outside any sane FOV range on purpose — it is the "unset" value.
	CameraFov = Num(TEXT("camera_fov"), -1.0f);

	// The `camortho` rect, Source units in, cm out.
	OrthoWidth  = FMath::Max(0.0f, Num(TEXT("c_orthowidth"), 100.0f) * ElysiumCam::U);
	OrthoHeight = FMath::Max(0.0f, Num(TEXT("c_orthoheight"), 100.0f) * ElysiumCam::U);

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
