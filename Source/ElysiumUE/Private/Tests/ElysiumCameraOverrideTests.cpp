// The `camera_track` override channel: the signed fade state machine, the lazy reap, the
// back-dated reversal, the per-entity minimum crossfades and the N-entry crossfade stack.
//
// Everything here is retail's own state machine (`docs/vtmb/camera-view-modes.md` §"The
// `camera_track` override channel"), so the assertions are on the STORED fields — the mark and the
// signed duration — rather than on an emergent weight. An equivalent-but-different mechanism (a
// separate weight variable, a blend object holding a start snapshot) fails these.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/ScopeExit.h"
#include "Debug/ElysiumLogTap.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumCameraOverride.h"

namespace ElysiumCameraOverrideTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// One entity's answers to slots 46-53, hand-built. The whole point of the source interface is that
// the channel is assertable with these and no world at all.
struct FFakeCameraSource final : public IElysiumCameraOverrideSource
{
	FVector Viewpoint = FVector::ZeroVector;
	FVector TargetPoint = FVector::ZeroVector;
	float Roll = ElysiumCameraOverride::DefaultRollDegrees;
	float Fov = ElysiumCameraOverride::DefaultFieldOfView;
	float FadeIn = 0.0f;
	float FadeOut = 0.0f;
	bool bAlive = true;

	mutable FVector LastAimFrom = FVector::ZeroVector;
	int32 ViewNotifies = 0;
	int32 TargetNotifies = 0;

	virtual FVector GetCameraViewpointPosition() const override { return Viewpoint; }
	virtual FVector GetCameraTargetPosition(const FVector& AimFrom) const override
	{
		LastAimFrom = AimFrom;
		return TargetPoint;
	}
	virtual float GetCameraRoll() const override { return Roll; }
	virtual float GetCameraFieldOfView() const override { return Fov; }
	virtual float GetCameraFadeInTime() const override { return FadeIn; }
	virtual float GetCameraFadeOutTime() const override { return FadeOut; }
	virtual void OnBecameCameraView() override { ++ViewNotifies; }
	virtual void OnBecameCameraTarget() override { ++TargetNotifies; }
	virtual bool IsCameraSourceAlive() const override { return bAlive; }
};

// The handle table and the cine-camera slot, both recorded.
class FFakeResolver final : public IElysiumCameraOverrideResolver
{
public:
	int32 CineClears = 0;

	FElysiumEntityHandle Add(FFakeCameraSource& Source)
	{
		const FElysiumEntityHandle Handle(Sources.Num(), /*Epoch*/ 7);
		Sources.Add(&Source);
		return Handle;
	}

	virtual IElysiumCameraOverrideSource* ResolveCameraOverrideSource(
		const FElysiumEntityHandle& Handle) const override
	{
		return Sources.IsValidIndex(Handle.Index) ? Sources[Handle.Index] : nullptr;
	}
	virtual void ClearCineCamera() override { ++CineClears; }

private:
	TArray<FFakeCameraSource*> Sources;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCameraOverrideTest,
	"Elysium.Substrate.CameraOverride", GElysiumTestFlags)
bool FElysiumCameraOverrideTest::RunTest(const FString&)
{
	// --- The three weight arms (`FUN_1017d900`) ------------------------------------------------
	// Retail's mark test is `0.0 < mark` against a `curtime` that is always positive, so every case
	// below arms at a positive substrate time. That is a real precondition, not test hygiene.
	{
		FFakeCameraSource A;
		FFakeResolver R;
		const FElysiumEntityHandle HA = R.Add(A);

		FElysiumCameraOverrideChannel Ch;
		Ch.SetViewEntity(1.0, HA, /*Crossfade*/ 0.0f, R);
		TestEqual(TEXT("a zero duration is stored as zero, not as a tiny fade"),
			Ch.SignedDuration(), 0.0f);
		TestEqual(TEXT("dur == 0 is the instantaneous arm: weight 1"), Ch.GetWeight(1.0, R), 1.0f);
		TestEqual(TEXT("...and it stays 1 however much time passes"), Ch.GetWeight(9.0, R), 1.0f);
		TestEqual(TEXT("the entity was notified it is the VIEW camera"), A.ViewNotifies, 1);
		TestEqual(TEXT("...and never that it is the target"), A.TargetNotifies, 0);
	}
	{
		FFakeCameraSource A;
		FFakeResolver R;
		const FElysiumEntityHandle HA = R.Add(A);

		FElysiumCameraOverrideChannel Ch;
		Ch.SetViewEntity(1.0, HA, /*Crossfade*/ 2.0f, R);
		TestEqual(TEXT("a positive duration is the fade IN"), Ch.SignedDuration(), 2.0f);
		TestEqual(TEXT("the ramp in is linear from the mark"), Ch.GetWeight(2.0, R), 0.5f);
		TestEqual(TEXT("...clamped at the bottom"), Ch.GetWeight(0.5, R), 0.0f);
		TestEqual(TEXT("...and at the top"), Ch.GetWeight(99.0, R), 1.0f);

		// `FUN_1017d6d0`. The sign IS the state: nothing else records the direction.
		Ch.FadeOut(2.0, /*Duration*/ 1.0f, R);
		TestEqual(TEXT("a release writes a NEGATIVE duration"), Ch.SignedDuration(), -1.0f);
		TestEqual(TEXT("...back-dated so the weight is continuous across the release frame"),
			Ch.GetWeight(2.0, R), 0.5f);
		TestEqual(TEXT("...and the ramp out reaches zero on time"), Ch.GetWeight(2.5, R), 0.0f);
	}

	// --- The lazy reap: the GETTER is the reaper ----------------------------------------------
	{
		FFakeCameraSource A;
		FFakeResolver R;
		const FElysiumEntityHandle HA = R.Add(A);

		FElysiumCameraOverrideChannel Ch;
		Ch.SetViewEntity(1.0, HA, /*Crossfade*/ 4.0f, R);
		TestFalse(TEXT("an armed channel is not clear"), Ch.IsClear());

		A.bAlive = false;   // both ends now dead — there is no target entity at all
		TestEqual(TEXT("with both ends dead the weight is 0"), Ch.GetWeight(2.0, R), 0.0f);
		TestTrue(TEXT("...and the same query reaped the channel: mark, duration, both handles, "
			"the entry list"), Ch.IsClear());

		// The reap is observable precisely here: the next push must behave as FRESH, not as a
		// mid-blend reversal of the fade that was in flight when the entities died.
		A.bAlive = true;
		Ch.SetViewEntity(5.0, HA, /*Crossfade*/ 3.0f, R);
		TestEqual(TEXT("the next push arms fresh at the push time"), Ch.MarkTime(), 5.0);
		TestEqual(TEXT("...over the requested duration"), Ch.SignedDuration(), 3.0f);
		TestEqual(TEXT("...so it starts from zero rather than from the stale weight"),
			Ch.GetWeight(5.0, R), 0.0f);
	}

	// --- The back-dated reversal (`FUN_1017d0b0`) ----------------------------------------------
	// Reverse a fade-out at w = 0.4. Retail moves the START to `t - w*dur`; there is no separate
	// weight variable anywhere on this path, which is why the assertion is on `MarkTime()`.
	{
		FFakeCameraSource A;
		FFakeResolver R;
		const FElysiumEntityHandle HA = R.Add(A);

		FElysiumCameraOverrideChannel Ch;
		Ch.SetViewEntity(1.0, HA, /*Crossfade*/ 0.0f, R);   // instantaneous: weight pinned at 1
		Ch.FadeOut(1.0, /*Duration*/ 2.0f, R);
		TestEqual(TEXT("the release is armed as a 2 s fade out"), Ch.SignedDuration(), -2.0f);
		const float MidWeight = Ch.GetWeight(2.2, R);
		TestEqual(TEXT("at t = 2.2 the fade out is at 0.4"), MidWeight, 0.4f);

		Ch.Arm(2.2, /*Duration*/ 3.0f, R);
		TestEqual(TEXT("the reversal stores the new duration with a POSITIVE sign"),
			Ch.SignedDuration(), 3.0f);
		TestEqual(TEXT("...and back-dates the mark to t - w*dur"),
			Ch.MarkTime(), 2.2 - 0.4 * 3.0, 1.e-4);
		TestEqual(TEXT("the weight is continuous across the reversal frame"),
			Ch.GetWeight(2.2, R), MidWeight, 1.e-4f);
		TestTrue(TEXT("...and has not yet arrived just before the remaining 0.6 * dur"),
			Ch.GetWeight(2.2 + 0.6 * 3.0 - 0.01, R) < 1.0f);
		TestEqual(TEXT("...reaching 1 in exactly 0.6 * dur"),
			Ch.GetWeight(2.2 + 0.6 * 3.0, R), 1.0f);
	}

	// --- The re-time arms the plan had backwards ----------------------------------------------
	// `0x1017d17d`'s `FCOMPP` + `TEST AH,0x5` + `JP` falls through on LESS THAN, so a fade-in in
	// flight is re-timed when it would land EARLIER than `t + dur` — it is LENGTHENED, not
	// shortened. A longer fade already in flight is left alone. Corrected by `rc_group_bc.md`.
	{
		FFakeCameraSource A;
		FFakeResolver R;
		const FElysiumEntityHandle HA = R.Add(A);

		FElysiumCameraOverrideChannel Ch;
		Ch.SetViewEntity(10.0, HA, /*Crossfade*/ 2.0f, R);   // ends at t = 12
		Ch.Arm(11.0, /*Duration*/ 6.0f, R);                  // would end at t = 17: later, so it takes
		TestEqual(TEXT("a short fade in flight is lengthened to the new request"),
			Ch.SignedDuration(), 6.0f);
		TestEqual(TEXT("...back-dated by the same w*dur, so the weight does not jump"),
			Ch.MarkTime(), 11.0 - 0.5 * 6.0, 1.e-4);
		TestEqual(TEXT("...which is exactly what makes the weight continuous"),
			Ch.GetWeight(11.0, R), 0.5f, 1.e-4f);

		FElysiumCameraOverrideChannel Long;
		Long.SetViewEntity(10.0, HA, /*Crossfade*/ 10.0f, R);   // ends at t = 20
		Long.Arm(11.0, /*Duration*/ 1.0f, R);                   // would end at t = 12: earlier, refused
		TestEqual(TEXT("a longer fade already in flight is left alone"),
			Long.SignedDuration(), 10.0f);
		TestEqual(TEXT("...mark untouched"), Long.MarkTime(), 10.0);
	}

	// --- The instantaneous regime's 10 ms dead band -------------------------------------------
	// `_DAT_10450aa4` = 0.00999999977. A crossfade request upgrades a `dur == 0` snap to a real
	// fade only while the snap is still within 10 ms; an older snap keeps the camera hard on.
	{
		FFakeCameraSource A;
		FFakeResolver R;
		const FElysiumEntityHandle HA = R.Add(A);

		FElysiumCameraOverrideChannel Fresh;
		Fresh.SetViewEntity(1.0, HA, 0.0f, R);
		Fresh.Arm(1.005, /*Duration*/ 2.0f, R);
		TestEqual(TEXT("a snap 5 ms old upgrades to a fade in"), Fresh.SignedDuration(), 2.0f);
		TestEqual(TEXT("...armed from now"), Fresh.MarkTime(), 1.005);

		FElysiumCameraOverrideChannel Stale;
		Stale.SetViewEntity(1.0, HA, 0.0f, R);
		Stale.Arm(3.0, /*Duration*/ 2.0f, R);
		TestEqual(TEXT("a snap that landed long ago keeps the camera hard on"),
			Stale.SignedDuration(), 0.0f);
		TestEqual(TEXT("...and the weight stays 1"), Stale.GetWeight(3.0, R), 1.0f);
	}

	// --- The per-entity minimum crossfades (slots 0xD0 / 0xD4) --------------------------------
	{
		FFakeCameraSource A;
		A.FadeIn = 1.0f;
		A.FadeOut = 1.0f;
		FFakeResolver R;
		const FElysiumEntityHandle HA = R.Add(A);

		FElysiumCameraOverrideChannel Ch;
		Ch.SetViewEntity(1.0, HA, /*Crossfade*/ 0.2f, R);
		TestEqual(TEXT("an entity demanding 1.0 s raises a 0.2 s push"),
			Ch.ViewSlot().CrossfadeDuration, 1.0f);
		TestEqual(TEXT("...and the armed fade is that duration"), Ch.SignedDuration(), 1.0f);

		Ch.FadeOut(2.5, /*Duration*/ 0.2f, R);
		TestEqual(TEXT("...and it raises the release the same way"), Ch.SignedDuration(), -1.0f);

		// `dur <= 0` hard-clears the mark. Nothing else is touched — the handles and the stack
		// survive and are collected by the next weight query.
		FFakeCameraSource B;
		FFakeResolver R2;
		const FElysiumEntityHandle HB = R2.Add(B);
		FElysiumCameraOverrideChannel Snap;
		Snap.SetViewEntity(1.0, HB, 0.0f, R2);
		Snap.FadeOut(2.0, /*Duration*/ 0.0f, R2);
		TestEqual(TEXT("a zero release hard-clears the mark"), Snap.MarkTime(), 0.0);
		TestEqual(TEXT("...so the override is off on the very next query"),
			Snap.GetWeight(2.0, R2), 0.0f);
		TestTrue(TEXT("...and that query reaps the rest"), Snap.IsClear());
	}

	// --- The crossfade stack: three outgoing view cameras --------------------------------------
	// Coverage accumulates as `1 - PROD(1 - f_i)` over the live channel's fraction and every entry's
	// own, and EVERY published value is pulled back by the CHANNEL's accumulated weight — never by
	// the entry's own `f`. `rc_group_bc.md` RC8: `f` appears in the listing exactly once, in the
	// coverage accumulator at `0x10352633`.
	{
		FFakeCameraSource A, B, C, D;
		A.Viewpoint = FVector(300.0, 0.0, 0.0);
		B.Viewpoint = FVector(200.0, 0.0, 0.0);
		C.Viewpoint = FVector(100.0, 0.0, 0.0);
		D.Viewpoint = FVector::ZeroVector;
		FFakeResolver R;
		const FElysiumEntityHandle HA = R.Add(A);
		const FElysiumEntityHandle HB = R.Add(B);
		const FElysiumEntityHandle HC = R.Add(C);
		const FElysiumEntityHandle HD = R.Add(D);

		FElysiumCameraOverrideChannel Ch;
		Ch.SetViewEntity(1.0, HA, 10.0f, R);
		Ch.SetViewEntity(2.0, HB, 10.0f, R);
		Ch.SetViewEntity(3.0, HC, 10.0f, R);
		Ch.SetViewEntity(4.0, HD, 10.0f, R);

		TestEqual(TEXT("three outgoing cameras are queued"), Ch.OutgoingEntries().Num(), 3);
		TestEqual(TEXT("...newest first: C, then B, then A"),
			Ch.OutgoingEntries()[0].Entity.Index, HC.Index);
		TestEqual(TEXT("...B second"), Ch.OutgoingEntries()[1].Entity.Index, HB.Index);
		TestEqual(TEXT("...A last"), Ch.OutgoingEntries()[2].Entity.Index, HA.Index);

		const FElysiumCameraOverrideChannel::FPublished& P =
			Ch.Publish(5.0, R, /*EyePosition*/ FVector::ZeroVector);
		TestTrue(TEXT("the channel is publishing"), P.bActive);

		// f: live D 0.1, C 0.2, B 0.3, A 0.4 -> 1 - 0.9*0.8*0.7*0.6.
		TestEqual(TEXT("coverage is 1 - PROD(1 - f_i)"), P.ViewCoverage, 0.6976f, 1.e-4f);

		// Folded by the accumulated weight at each step: 0.1, then 0.28, then 0.496.
		TestEqual(TEXT("every value is pulled back by the CHANNEL weight"),
			P.ViewOrigin.X, 235.1232, 1.e-3);
		// The same fold done with the entry's own fraction would answer 245.6 — the number the plan
		// (and both earlier reports) would have produced. Asserting it is NOT that is the point.
		TestTrue(TEXT("...and emphatically not by the entry's own fraction"),
			!FMath::IsNearlyEqual(P.ViewOrigin.X, 245.6, 1.e-2));
	}

	// --- An entry whose entity dies mid-fold is dropped ---------------------------------------
	{
		FFakeCameraSource A, B, C, D;
		A.Viewpoint = FVector(300.0, 0.0, 0.0);
		B.Viewpoint = FVector(200.0, 0.0, 0.0);
		C.Viewpoint = FVector(100.0, 0.0, 0.0);
		FFakeResolver R;
		const FElysiumEntityHandle HA = R.Add(A);
		const FElysiumEntityHandle HB = R.Add(B);
		const FElysiumEntityHandle HC = R.Add(C);
		const FElysiumEntityHandle HD = R.Add(D);

		FElysiumCameraOverrideChannel Ch;
		Ch.SetViewEntity(1.0, HA, 10.0f, R);
		Ch.SetViewEntity(2.0, HB, 10.0f, R);
		Ch.SetViewEntity(3.0, HC, 10.0f, R);
		Ch.SetViewEntity(4.0, HD, 10.0f, R);

		B.bAlive = false;
		const FElysiumCameraOverrideChannel::FPublished& P =
			Ch.Publish(5.0, R, FVector::ZeroVector);
		TestEqual(TEXT("the dead entry is compacted out"), Ch.OutgoingEntries().Num(), 2);
		TestEqual(TEXT("...leaving the others in order"),
			Ch.OutgoingEntries()[0].Entity.Index, HC.Index);
		TestEqual(TEXT("...and it contributed no coverage"), P.ViewCoverage, 0.568f, 1.e-4f);
		TestEqual(TEXT("...so the fold is C then A alone"), P.ViewOrigin.X, 241.2, 1.e-3);
	}

	// --- The kind-byte defect: a superseded TARGET folds through the VIEW arm ------------------
	// `0x1017d386` and `0x1017d55f` both write 0, so the fold's kind-1 branch is unreachable in a
	// shipped run. `docs/vtmb/retail-defects.md` §7.
	{
		FFakeCameraSource V, T1, T2;
		V.Viewpoint = FVector::ZeroVector;
		V.Fov = 30.0f;
		V.Roll = 0.0f;
		// T1 is an NPC target: `CBaseCombatCharacter` overrides neither slot 48 nor 49, so it
		// answers the `CBaseEntity` defaults, and the defect drags the published FOV toward 75.
		T1.Viewpoint = FVector(500.0, 0.0, 0.0);
		T1.TargetPoint = FVector(0.0, 500.0, 0.0);
		T2.TargetPoint = FVector(0.0, 900.0, 0.0);
		FFakeResolver R;
		const FElysiumEntityHandle HV = R.Add(V);
		const FElysiumEntityHandle H1 = R.Add(T1);
		const FElysiumEntityHandle H2 = R.Add(T2);

		FElysiumCameraOverrideChannel Ch;
		Ch.SetViewEntity(1.0, HV, 10.0f, R);
		Ch.SetTargetEntity(1.0, H1, 10.0f, R);
		Ch.SetTargetEntity(2.0, H2, 10.0f, R);

		TestEqual(TEXT("the superseded target queued one entry"), Ch.OutgoingEntries().Num(), 1);
		TestEqual(TEXT("...and it carries the kind byte VIEW, because both pushers write 0"),
			static_cast<int32>(Ch.OutgoingEntries()[0].Kind),
			static_cast<int32>(EElysiumCameraOverrideKind::View));

		const FElysiumCameraOverrideChannel::FPublished& P =
			Ch.Publish(3.0, R, FVector::ZeroVector);
		// wView = 0.2, so the published view origin is pulled back to within a fifth of the outgoing
		// TARGET's own viewpoint: `lerp(500, 0, 0.2) = 400`. That is the defect made visible — a
		// camera that was only ever asked to be the aim POINT is now moving the lens.
		TestEqual(TEXT("the outgoing target drags the published VIEW origin"),
			P.ViewOrigin.X, 400.0, 1.e-3);
		TestEqual(TEXT("...and the published FOV toward the CBaseEntity default 75"),
			P.FieldOfView, 66.0f, 1.e-3f);
		TestEqual(TEXT("...while the target channel's own coverage is untouched"),
			P.TargetCoverage, 0.1f, 1.e-4f);
		TestTrue(TEXT("...and the target point simply snaps to the new target"),
			P.TargetPoint.Equals(T2.TargetPoint, 1.e-3));
	}

	// --- Only the VIEW setter cancels a cine camera --------------------------------------------
	{
		FFakeCameraSource A;
		FFakeResolver R;
		const FElysiumEntityHandle HA = R.Add(A);

		FElysiumCameraOverrideChannel Ch;
		Ch.SetTargetEntity(1.0, HA, 0.0f, R);
		TestEqual(TEXT("SetTargetEntity leaves a live cine shot alone"), R.CineClears, 0);
		TestEqual(TEXT("...and notifies through the become-TARGET slot"), A.TargetNotifies, 1);
		TestEqual(TEXT("...never the become-view one"), A.ViewNotifies, 0);

		Ch.SetViewEntity(2.0, HA, 0.0f, R);
		TestEqual(TEXT("SetViewEntity drops it, exactly as SetCineCamera(NULL) does"),
			R.CineClears, 1);
		TestEqual(TEXT("...and notifies through the become-VIEW slot"), A.ViewNotifies, 1);
	}

	// --- M4: the encoder clamps are contract, and they warn ------------------------------------
	{
		FElysiumLogTap Logs(256);
		Logs.Install();
		ON_SCOPE_EXIT { Logs.Remove(); };
		const uint64 Cursor = Logs.Cursor();

		FFakeCameraSource A;
		A.Fov = 250.0f;       // outside m_flCameraFOVOverride's [0, 180]
		A.Roll = -400.0f;     // outside m_flCameraRollOverride's [-180, 180]
		FFakeResolver R;
		const FElysiumEntityHandle HA = R.Add(A);

		FElysiumCameraOverrideChannel Ch;
		Ch.SetViewEntity(1.0, HA, /*Crossfade*/ 25.0f, R);   // outside the [-10, +10] fade range
		const FElysiumCameraOverrideChannel::FPublished& P =
			Ch.Publish(2.0, R, FVector::ZeroVector);
		TestEqual(TEXT("FOV is clamped to the SendProp maximum"), P.FieldOfView, 180.0f);
		TestEqual(TEXT("roll is clamped to the SendProp minimum"), P.Roll, -180.0f);
		TestEqual(TEXT("the replicated fade duration is clamped to +-10 s"), P.FadeDuration, 10.0f);
		TestEqual(TEXT("...while the UNREPLICATED duration keeps what was armed"),
			Ch.SignedDuration(), 25.0f);

		TArray<FElysiumLogTap::FLine> Lines;
		Logs.CollectSince(Cursor, Lines);
		auto SawWarning = [&Lines](const TCHAR* Needle)
		{
			return Lines.ContainsByPredicate([Needle](const FElysiumLogTap::FLine& Line)
			{
				return Line.Verbosity.Equals(TEXT("Warning"), ESearchCase::IgnoreCase)
					&& Line.Text.Contains(Needle);
			});
		};
		TestTrue(TEXT("the FOV clamp names its SendProp"), SawWarning(TEXT("m_flCameraFOVOverride")));
		TestTrue(TEXT("the roll clamp names its SendProp"), SawWarning(TEXT("m_flCameraRollOverride")));
		TestTrue(TEXT("the fade clamp names its SendProp"),
			SawWarning(TEXT("m_flCameraOverrideFadeDuration")));
	}

	// --- With no live view entity every queued entry folds all the way back --------------------
	// `WeightView` starts at 0 and is raised only inside the live-view block, so `lerp(old, pub, 0)`
	// is `old`. A named consequence in the recovery, and the reason the port cannot special-case it.
	{
		FFakeCameraSource A, B, Keeper;
		A.Viewpoint = FVector(400.0, 0.0, 0.0);
		Keeper.TargetPoint = FVector(0.0, 10.0, 0.0);
		FFakeResolver R;
		const FElysiumEntityHandle HA = R.Add(A);
		const FElysiumEntityHandle HB = R.Add(B);
		const FElysiumEntityHandle HK = R.Add(Keeper);

		FElysiumCameraOverrideChannel Ch;
		Ch.SetTargetEntity(1.0, HK, 4.0f, R);   // keeps the channel alive when the view dies
		Ch.SetViewEntity(1.0, HA, 4.0f, R);
		Ch.SetViewEntity(2.0, HB, 4.0f, R);     // A becomes an outgoing entry
		B.bAlive = false;                       // the live view entity dies; the target survives

		const FElysiumCameraOverrideChannel::FPublished& P =
			Ch.Publish(3.0, R, FVector::ZeroVector);
		TestTrue(TEXT("the channel still publishes while the target end is live"), P.bActive);
		TestEqual(TEXT("the published view folds all the way back to the outgoing camera"),
			P.ViewOrigin.X, 400.0, 1.e-3);
	}

	return true;
}

// The combat character's half of the interface: one runtime field answering both directions, and
// `SetAsCameraTarget`'s broadcast with a crossfade argument of exactly 0.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCameraOverrideCharacterTest,
	"Elysium.Substrate.CameraOverrideCharacter", GElysiumTestFlags)
bool FElysiumCameraOverrideCharacterTest::RunTest(const FString&)
{
	FElysiumEntityWorld World(nullptr, nullptr);
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__camera_override_test__");
		FElysiumEntityDef Npc;
		Npc.Classname = TEXT("npc_VVampire");
		Npc.TargetName = TEXT("subject");
		Npc.Origin = FVector(100.f, 0.f, 0.f);
		Npc.Keys.Add(TEXT("model"), TEXT("models/character/npc/unique/jack/Jack.mdl"));
		Defs.Defs.Add(MoveTemp(Npc));
		World.Load(MoveTemp(Defs));
		World.SpawnPlayer();
		World.Activate(0.0);
	}
	// Retail's `curtime` is never zero; the channel's `mark > 0` test is read literally, so the
	// world clock has to have advanced before anything arms.
	World.Tick(1.0);

	FElysiumEntity* Raw = World.FindByName(TEXT("subject"));
	FElysiumCombatCharacter* Subject = Raw ? Raw->AsCombatCharacter() : nullptr;
	if (!TestNotNull(TEXT("the map has a combat character"), Subject))
	{
		return false;
	}

	TestEqual(TEXT("a fresh character demands no crossfade in"), Subject->GetCameraFadeInTime(), 0.f);
	TestEqual(TEXT("...nor out"), Subject->GetCameraFadeOutTime(), 0.f);

	// `InputSetBodyAsCameraTarget` -> `SetAsCameraTarget(0, 0)`.
	Subject->InputSetBodyAsCameraTarget(FElysiumInputArgs());
	TestFalse(TEXT("SetBody picks the body, not the head"), Subject->bCameraTargetIsHead);
	TestEqual(TEXT("the channel took the character as its TARGET"),
		World.CameraOverrideChannel().TargetSlot().Entity.Index, Subject->Handle.Index);
	TestEqual(TEXT("...with the broadcast's own crossfade of 0"),
		World.CameraOverrideChannel().TargetSlot().CrossfadeDuration, 0.f);
	TestEqual(TEXT("...and the view slot is untouched: a target does not take the view"),
		World.CameraOverrideChannel().ViewSlot().Entity.Index, INDEX_NONE);

	// `InputFadeHeadAsCameraTarget` -> `SetAsCameraTarget(1, value)`. The ONE field answers both
	// directions, unclamped — unlike a `camera_track`'s two authored keyvalues clamped at zero.
	World.Tick(2.0);
	FElysiumInputArgs Fade;
	Fade.Param = FElysiumVariant::Float(0.75f);
	Subject->InputFadeHeadAsCameraTarget(Fade);
	TestTrue(TEXT("FadeHead picks the head"), Subject->bCameraTargetIsHead);
	TestEqual(TEXT("the single fade field answers the IN direction"),
		Subject->GetCameraFadeInTime(), 0.75f);
	TestEqual(TEXT("...and the OUT direction, from the same field"),
		Subject->GetCameraFadeOutTime(), 0.75f);
	TestEqual(TEXT("the broadcast still passes 0, so the entity's own minimum is what bites"),
		World.CameraOverrideChannel().TargetSlot().CrossfadeDuration, 0.75f);

	// Head -> the look point; body -> the world-space centre. Headless, the latter has no collision
	// bounds and falls back to the standing-offset midpoint, which is still not the eye point.
	TestTrue(TEXT("the head arm aims at the look point"),
		Subject->GetCameraTargetPosition(FVector::ZeroVector).Equals(Subject->EyePosition(), 0.1));
	Subject->bCameraTargetIsHead = false;
	TestFalse(TEXT("...and the body arm does not"),
		Subject->GetCameraTargetPosition(FVector::ZeroVector).Equals(Subject->EyePosition(), 0.1));
	TestTrue(TEXT("the viewpoint getter is the look point in both cases"),
		Subject->GetCameraViewpointPosition().Equals(Subject->EyePosition(), 0.1));

	// Slots 48 and 49 are NOT overridden by CBaseCombatCharacter: it keeps the CBaseEntity bodies.
	TestEqual(TEXT("a character answers the CBaseEntity default roll"),
		Subject->GetCameraRoll(), ElysiumCameraOverride::DefaultRollDegrees);
	TestEqual(TEXT("...and the CBaseEntity default FOV of 75"),
		Subject->GetCameraFieldOfView(), ElysiumCameraOverride::DefaultFieldOfView);

	return true;
}

} // namespace ElysiumCameraOverrideTests

#endif // WITH_DEV_AUTOMATION_TESTS
