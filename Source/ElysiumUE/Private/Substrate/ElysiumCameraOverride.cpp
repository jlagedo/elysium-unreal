#include "Substrate/ElysiumCameraOverride.h"

DEFINE_LOG_CATEGORY(LogElysiumCameraOverride);

namespace ElysiumCameraOverride
{
	float ClampFieldOfView(float Fov, const TCHAR* Site)
	{
		const float Clamped = FMath::Clamp(Fov, MinFieldOfView, MaxFieldOfView);
		if (Clamped != Fov)
		{
			UE_LOG(LogElysiumCameraOverride, Warning,
				TEXT("%s: camera override FOV %.3f is outside m_flCameraFOVOverride's SendProp "
					"range [%.1f, %.1f] (DT_Local +0x104) — clamped to %.3f"),
				Site, Fov, MinFieldOfView, MaxFieldOfView, Clamped);
		}
		return Clamped;
	}

	float ClampRoll(float RollDegrees, const TCHAR* Site)
	{
		const float Clamped = FMath::Clamp(RollDegrees, -MaxRollDegrees, MaxRollDegrees);
		if (Clamped != RollDegrees)
		{
			UE_LOG(LogElysiumCameraOverride, Warning,
				TEXT("%s: camera override roll %.3f is outside m_flCameraRollOverride's SendProp "
					"range [%.1f, %.1f] (DT_Local +0x108) — clamped to %.3f"),
				Site, RollDegrees, -MaxRollDegrees, MaxRollDegrees, Clamped);
		}
		return Clamped;
	}

	float ClampFadeSeconds(float Seconds, const TCHAR* Site)
	{
		const float Clamped = FMath::Clamp(Seconds, -MaxFadeSeconds, MaxFadeSeconds);
		if (Clamped != Seconds)
		{
			UE_LOG(LogElysiumCameraOverride, Warning,
				TEXT("%s: camera override fade %.3f s is outside "
					"m_flCameraOverrideFadeDuration's SendProp range [%.1f, %.1f] "
					"(DT_Local +0x114) — clamped to %.3f s"),
				Site, Seconds, -MaxFadeSeconds, MaxFadeSeconds, Clamped);
		}
		return Clamped;
	}
}

namespace
{
	// `handleLive()` — the EHANDLE test both retail and the port make before touching a slot.
	IElysiumCameraOverrideSource* LiveSource(const FElysiumEntityHandle& Handle,
		const IElysiumCameraOverrideResolver& Resolver)
	{
		if (!Handle.IsSet())
		{
			return nullptr;
		}
		IElysiumCameraOverrideSource* Source = Resolver.ResolveCameraOverrideSource(Handle);
		return (Source != nullptr && Source->IsCameraSourceAlive()) ? Source : nullptr;
	}
}

float FElysiumCameraOverrideChannel::ChannelFraction(double Now, const FSlot& Slot)
{
	// `0x10352234` / `0x1035234c`: a non-positive crossfade means "already there", not "divide".
	if (Slot.CrossfadeDuration <= 0.0f)
	{
		return 1.0f;
	}
	return FMath::Clamp(
		static_cast<float>((Now - Slot.SetTime) / static_cast<double>(Slot.CrossfadeDuration)),
		0.0f, 1.0f);
}

float FElysiumCameraOverrideChannel::GetWeight(double Now,
	const IElysiumCameraOverrideResolver& Resolver)
{
	// `FUN_1017d900`. The mark test is `0.0 < mark`, against a `curtime` that is always positive.
	if (Mark > 0.0
		&& (LiveSource(View.Entity, Resolver) != nullptr
			|| LiveSource(Target.Entity, Resolver) != nullptr))
	{
		if (Duration == 0.0f)
		{
			return 1.0f;   // instantaneous — the whole override is on
		}
		const float Elapsed = static_cast<float>(Now - Mark);
		if (Duration > 0.0f)
		{
			return FMath::Clamp(Elapsed / Duration, 0.0f, 1.0f);          // fade IN
		}
		return FMath::Clamp(1.0f + (Elapsed / Duration), 0.0f, 1.0f);     // fade OUT
	}

	// The getter is the reaper. Retail clears exactly these five things and nothing else — the two
	// slots keep their set times and crossfade durations, which is harmless because the handles are
	// gone and every reader gates on `handleLive`.
	Clear();
	return 0.0f;
}

void FElysiumCameraOverrideChannel::Clear()
{
	Mark = 0.0;
	Duration = 0.0f;
	View.Entity = FElysiumEntityHandle::Invalid();
	Target.Entity = FElysiumEntityHandle::Invalid();
	Entries.Reset();
}

void FElysiumCameraOverrideChannel::ReleaseSlot(EElysiumCameraOverrideKind Kind)
{
	FSlot& Slot = (Kind == EElysiumCameraOverrideKind::Target) ? Target : View;
	Slot.Entity = FElysiumEntityHandle::Invalid();
	if (Kind != EElysiumCameraOverrideKind::Target)
	{
		// An explicit release IS "this entity has stopped being the view entity", so the next set
		// of any entity is a real handoff and carries the cine clear again.
		ViewAdopted = FElysiumEntityHandle::Invalid();
	}
}

bool FElysiumCameraOverrideChannel::IsClear() const
{
	return Mark == 0.0 && Duration == 0.0f && !View.Entity.IsSet() && !Target.Entity.IsSet()
		&& Entries.IsEmpty();
}

void FElysiumCameraOverrideChannel::PushOutgoing(double Now, EElysiumCameraOverrideKind Kind,
	const FSlot& Slot, const IElysiumCameraOverrideResolver& Resolver)
{
	// `curtime > setTime && handleLive(handle)` — `0x1017d2fa`/`0x1017d386` for the view setter and
	// `0x1017d4b4`/`0x1017d55f` for the target one.
	if (!(Now > Slot.SetTime) || LiveSource(Slot.Entity, Resolver) == nullptr)
	{
		return;
	}
	FEntry Entry;
	// **The kind-byte defect, reproduced.** Both pushers write 0 (`0x1017d386`, `0x1017d55f`), so a
	// superseded TARGET camera folds through the VIEW arm and consumes the view channel's coverage.
	// `Kind` is carried into the entry only so the shape stays retail's; the assignment below is
	// what retail actually does. See `docs/vtmb/retail-defects.md` §7.
	(void)Kind;
	Entry.Kind = EElysiumCameraOverrideKind::View;
	Entry.Entity = Slot.Entity;
	Entry.SetTime = Slot.SetTime;
	Entry.CrossfadeDuration = Slot.CrossfadeDuration;
	Entries.Insert(MoveTemp(Entry), 0);   // push_front — the list runs newest first
}

void FElysiumCameraOverrideChannel::SetViewEntity(double Now, const FElysiumEntityHandle& Entity,
	float Crossfade, IElysiumCameraOverrideResolver& Resolver)
{
	// `FUN_1017d280`. `SetCineCamera(NULL)` runs FIRST and unconditionally — before the null-entity
	// early-out — so even a `SetViewEntity(null)` that only fades out still drops the cine shot.
	// The two channels are mutually exclusive by construction.
	Resolver.ClearCineCamera();

	AdoptViewEntity(Now, Entity, Crossfade, Resolver);
}

void FElysiumCameraOverrideChannel::ReadoptViewEntity(double Now,
	const FElysiumEntityHandle& Entity, float Crossfade,
	const IElysiumCameraOverrideResolver& Resolver)
{
	// No `ClearCineCamera` — that is the whole point of the split. See the header.
	AdoptViewEntity(Now, Entity, Crossfade, Resolver);
}

void FElysiumCameraOverrideChannel::AdoptViewEntity(double Now, const FElysiumEntityHandle& Entity,
	float Crossfade, const IElysiumCameraOverrideResolver& Resolver)
{
	if (Crossfade < 0.0f)
	{
		Crossfade = 0.0f;
	}

	IElysiumCameraOverrideSource* Incoming = LiveSource(Entity, Resolver);
	if (Incoming == nullptr)
	{
		// Retail leaves the slot alone on a dead handle and only fades out, so the remembered
		// occupant is left alone too.
		FadeOut(Now, Crossfade, Resolver);
		return;
	}

	PushOutgoing(Now, EElysiumCameraOverrideKind::View, View, Resolver);

	// Slot 0xD0. The entity can demand a MINIMUM crossfade in; the caller's argument only raises it.
	Crossfade = FMath::Max(Crossfade, Incoming->GetCameraFadeInTime());
	Arm(Now, Crossfade, Resolver);

	View.Entity = Entity;
	View.SetTime = Now;
	View.CrossfadeDuration = Crossfade;
	ViewAdopted = Entity;

	Incoming->OnBecameCameraView();   // slot 0xBC, `0x1017d3e9`
}

void FElysiumCameraOverrideChannel::SetTargetEntity(double Now, const FElysiumEntityHandle& Entity,
	float Crossfade, const IElysiumCameraOverrideResolver& Resolver)
{
	// `FUN_1017d460`. Note what is absent: no `SetCineCamera(NULL)`. Setting the TARGET entity does
	// not cancel a cine camera (`rc_group_bc.md` RC7) — only setting the view entity does.
	if (Crossfade < 0.0f)
	{
		Crossfade = 0.0f;
	}

	IElysiumCameraOverrideSource* Incoming = LiveSource(Entity, Resolver);
	if (Incoming == nullptr)
	{
		FadeOut(Now, Crossfade, Resolver);
		return;
	}

	PushOutgoing(Now, EElysiumCameraOverrideKind::Target, Target, Resolver);

	// Slot 0xD0 again — the target setter reads the fade-IN minimum, not the fade-out one
	// (`0x1017d570`-`0x1017d58d`).
	Crossfade = FMath::Max(Crossfade, Incoming->GetCameraFadeInTime());
	Arm(Now, Crossfade, Resolver);

	Target.Entity = Entity;
	Target.SetTime = Now;
	Target.CrossfadeDuration = Crossfade;

	Incoming->OnBecameCameraTarget();   // slot 0xB8, `0x1017d5c2` — the become-TARGET notify
}

void FElysiumCameraOverrideChannel::Arm(double Now, float NewDuration,
	const IElysiumCameraOverrideResolver& Resolver)
{
	// `FUN_1017d0b0`, read off the listing at `0x1017d0b0`-`0x1017d21d` rather than off the
	// decompile, which loses the entry clamp and inverts two comparisons.

	// `0x1017d0b0`-`0x1017d0c9`: a duration that is not strictly positive is stored as 0.
	if (!(NewDuration > 0.0f))
	{
		NewDuration = 0.0f;
	}

	// `0x1017d0e5` / `0x1017d205`: fresh. Nothing is armed, so this is a plain fade in over `dur`.
	if (!(Mark > 0.0))
	{
		Mark = Now;
		Duration = NewDuration;
		return;
	}

	// The weight BEFORE the re-time. This call can reap (both ends dead), exactly as retail's does —
	// the arm below then writes onto a channel the getter has just cleared, which is retail's own
	// behaviour and not a port hazard to be tidied away.
	const float Weight = GetWeight(Now, Resolver);

	// `0x1017d109`: reversing a fade-OUT. Back-date the mark so the weight is continuous across the
	// reversal frame — this is the whole mechanism, and it is why there is no separate weight field.
	if (Duration < 0.0f)
	{
		Duration = NewDuration;
		Mark = Now - static_cast<double>(Weight) * static_cast<double>(NewDuration);
		return;
	}

	// `0x1017d13f`-`0x1017d1aa`: already fading IN. Re-time only while the blend is unfinished and
	// only when the current fade would land EARLIER than `Now + NewDuration` — the listing compares
	// `(Mark + Duration) < (Now + NewDuration)` and falls through on "less than" (`0x1017d17d`
	// `FCOMPP` + `TEST AH,0x5` + `JP`). The plan's "a fade-in that would finish LATER is shortened"
	// had this backwards: a short fade in flight is LENGTHENED to the new request, and a long one is
	// left alone. Either way the weight is preserved by the same back-dating.
	if (Duration > 0.0f)
	{
		if (Weight < 1.0f
			&& (static_cast<double>(Duration) + Mark) < (Now + static_cast<double>(NewDuration)))
		{
			Duration = NewDuration;
			Mark = Now - static_cast<double>(Weight) * static_cast<double>(NewDuration);
		}
		return;
	}

	// `0x1017d1ad`-`0x1017d202`: the instantaneous regime (`Duration == 0`, weight pinned at 1).
	// A crossfade request upgrades it to a real fade in ONLY while the snap is still fresh —
	// `Now - 0.01 <= Mark` (`_DAT_10450aa4`, the same 10 ms dead band the client ramp uses). A snap
	// that landed longer ago than that keeps the camera hard on; the request is dropped.
	if ((Now - static_cast<double>(ElysiumCameraOverride::MarkEpsilonSeconds)) <= Mark
		&& NewDuration > 0.0f)
	{
		Mark = Now;
		Duration = NewDuration;
	}
}

void FElysiumCameraOverrideChannel::FadeOut(double Now, float NewDuration,
	const IElysiumCameraOverrideResolver& Resolver)
{
	// `FUN_1017d6d0`.
	const float Weight = GetWeight(Now, Resolver);
	if (!(Weight > 0.0f))
	{
		return;   // nothing is up; there is nothing to fade out of
	}

	// Slot 0xD4 on each LIVE end. Each can demand its own minimum crossfade out, and the answer is
	// the maximum of the three. A dead end contributes 0.0 (`0x1017d767` / `0x1017d7e6`).
	if (const IElysiumCameraOverrideSource* ViewSource = LiveSource(View.Entity, Resolver))
	{
		NewDuration = FMath::Max(NewDuration, ViewSource->GetCameraFadeOutTime());
	}
	if (const IElysiumCameraOverrideSource* TargetSource = LiveSource(Target.Entity, Resolver))
	{
		NewDuration = FMath::Max(NewDuration, TargetSource->GetCameraFadeOutTime());
	}

	// `0x1017d87e`: a non-positive duration hard-clears the MARK ONLY. The handles and the entry
	// list survive; the next `GetWeight` reaps them. This is the instant snap back to the player.
	if (!(NewDuration > 0.0f))
	{
		Mark = 0.0;
		return;
	}

	// `0x1017d825`-`0x1017d871`: apply when the channel is not already fading out, or when the
	// already-running fade-out would end LATER than this one would (`Mark - Duration > Now + dur`,
	// with `Duration` negative, so `Mark - Duration` is that fade's end time).
	if (Duration >= 0.0f
		|| (Mark - static_cast<double>(Duration)) > (Now + static_cast<double>(NewDuration)))
	{
		Duration = -NewDuration;
		Mark = Now - static_cast<double>(1.0f - Weight) * static_cast<double>(NewDuration);
	}
}

const FElysiumCameraOverrideChannel::FPublished& FElysiumCameraOverrideChannel::Publish(
	double Now, const IElysiumCameraOverrideResolver& Resolver, const FVector& EyePosition)
{
	// `CHL2_Player::SetupVisibility` `0x10352120`. The replicated pair is copied unconditionally,
	// before the weight is even consulted (`0x10352150`-`0x10352170`).
	Published.FadeStartTime = Mark;
	Published.FadeDuration = ElysiumCameraOverride::ClampFadeSeconds(
		Duration, TEXT("camera override publish"));

	const float Weight = GetWeight(Now, Resolver);
	Published.Weight = Weight;
	if (!(Weight > 0.0f))
	{
		Published.bActive = false;
		Published.bHasView = false;
		Published.bHasTarget = false;
		Published.ViewCoverage = 0.0f;
		Published.TargetCoverage = 0.0f;
		return Published;
	}
	Published.bActive = true;
	Published.Timestamp = Now;

	// Both channel weights start at 0 (`0x10352179` / `0x1035217d`) and are raised only inside their
	// own "entity is live" block. With no live view entity `WeightView` stays 0, and every queued
	// view entry then folds the published value ALL the way back to its own camera.
	float WeightView = 0.0f;
	float WeightTarget = 0.0f;

	Published.bHasView = false;
	Published.bHasTarget = false;

	if (const IElysiumCameraOverrideSource* ViewSource = LiveSource(View.Entity, Resolver))
	{
		Published.ViewOrigin = ViewSource->GetCameraViewpointPosition();   // slot 0xC8
		Published.FieldOfView = ViewSource->GetCameraFieldOfView();        // slot 0xC4
		Published.Roll = ViewSource->GetCameraRoll();                      // slot 0xC0
		Published.bHasView = true;
		WeightView = ChannelFraction(Now, View);
	}

	if (const IElysiumCameraOverrideSource* TargetSource = LiveSource(Target.Entity, Resolver))
	{
		// The aim-from source: the published view origin, or `EyePosition()` when there is no view
		// entity. Dead in every shipped implementation — carried because retail computes it.
		const FVector AimFrom = Published.bHasView ? Published.ViewOrigin : EyePosition;
		Published.TargetPoint = TargetSource->GetCameraTargetPosition(AimFrom);   // slot 0xCC
		Published.bHasTarget = true;
		WeightTarget = ChannelFraction(Now, Target);
	}

	// The fold, newest first. Read `rc_group_bc.md` RC8 before touching this: **every value is
	// pulled back by the CHANNEL's accumulated weight**, not by the entry's own fraction. `f`
	// appears in exactly one place in the listing — the coverage accumulator at the bottom
	// (`0x10352633` is its only `FMUL`/`FSUB` site) — and nowhere else.
	for (int32 Index = 0; Index < Entries.Num(); )
	{
		const FEntry& Entry = Entries[Index];
		const bool bViewArm = Entry.Kind == EElysiumCameraOverrideKind::View;
		float& Coverage = bViewArm ? WeightView : WeightTarget;

		const IElysiumCameraOverrideSource* Source = LiveSource(Entry.Entity, Resolver);
		if (Coverage >= 1.0f || Source == nullptr)
		{
			// `0x103523d7`-`0x10352422` then `0x10352653`-`0x1035267f`: the drop is a compaction,
			// so the surviving entries keep their newest-first order.
			Entries.RemoveAt(Index);
			continue;
		}

		const float Fraction = (Entry.CrossfadeDuration > 0.0f)
			? FMath::Clamp(static_cast<float>(
				(Now - Entry.SetTime) / static_cast<double>(Entry.CrossfadeDuration)), 0.0f, 1.0f)
			: 1.0f;

		if (bViewArm)
		{
			Published.ViewOrigin = FMath::Lerp(
				Source->GetCameraViewpointPosition(), Published.ViewOrigin,
				static_cast<double>(WeightView));
			Published.Roll = FMath::Lerp(Source->GetCameraRoll(), Published.Roll, WeightView);
			Published.FieldOfView = FMath::Lerp(
				Source->GetCameraFieldOfView(), Published.FieldOfView, WeightView);
			Published.bHasView = true;
		}
		else
		{
			// Dead but present. No shipped path writes the kind byte as 1 (both pushers write 0),
			// so this arm is unreachable in a faithful run — it is kept because the fold's shape is
			// retail's, and because a future fix of the defect would light it up unchanged.
			Published.TargetPoint = FMath::Lerp(
				Source->GetCameraTargetPosition(Published.ViewOrigin), Published.TargetPoint,
				static_cast<double>(WeightTarget));
			Published.bHasTarget = true;
		}

		// `w[kind] = 1 - (1 - f) * (1 - w[kind])` — `0x1035261f`-`0x1035264f`. The entry's own
		// fraction advances coverage and does nothing else.
		Coverage = 1.0f - (1.0f - Fraction) * (1.0f - Coverage);
		++Index;
	}

	Published.ViewCoverage = WeightView;
	Published.TargetCoverage = WeightTarget;

	// M4, ruled 2026-09-07. The encoder clamps sit exactly where retail's SendProps do — on the
	// composed value, once, at the point of publication — so the fold above works in unclamped
	// floats and only the published pair is bounded. The bit widths are NOT reproduced.
	Published.FieldOfView = ElysiumCameraOverride::ClampFieldOfView(
		Published.FieldOfView, TEXT("camera override publish"));
	Published.Roll = ElysiumCameraOverride::ClampRoll(
		Published.Roll, TEXT("camera override publish"));

	// M13, ruled 2026-09-07: retail follows this with `AddOriginToPVS(m_vecCameraViewOverride)` and,
	// for a live cine camera, a `ResetPVS` that skips the ordinary visibility pass entirely. Unreal
	// culls from the actual view, which is what the hand-moved PVS origin was approximating, so no
	// PVS half is built here. The behavioural consequence is retail's artefact and is deliberately
	// NOT reproduced: in retail, entities near the player but far from the lens stop receiving
	// updates for the duration of a shot.

	return Published;
}
