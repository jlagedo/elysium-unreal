// The PLAYER half of the footstep subsystem — `docs/vtmb/footsteps.md` §2, recovered from
// `vampire.dll` and asserted here constant by constant.
//
// Seven Substrate-tier cases, no RHI, no actors, no export corpus:
//
//   * `PlayerClock`           — `UpdateStepSound 0x1011e940`: the early returns, the two speed
//                               bands, the four arms and every interval the tail lands on.
//   * `PlayerVolumes`         — the `gamematerial` pairs, the duck scale, `footstep_pc_vol`, clamp.
//   * `PlayerLanding`         — `CheckFalling 0x10125db0`'s volume ladder, the floating reduction
//                               and the clock reset that double-steps.
//   * `PlayerWater`           — the water and wade arms, moved here from `Elysium.Substrate.Water`
//                               when the clock left `AElysiumMapActor`.
//   * `PlayerHearing`         — `UpdatePlayerSound 0x1016b480`: the six categories, their radii and
//                               the 250 units/s decay, plus the producer wired to a world.
//   * `PlayerSwallows`        — 2050-2053 claimed and silent on the player leaf.
//   * `PlayerServerFootstepsOff` — `sv_footsteps 0` silences the whole clock.
//
// The rules are pure functions over value types, so most of this needs no world at all; the two
// cases that do build one drive `FElysiumPlayer::TickStepClock` / `UpdatePlayerSound` directly
// rather than through `Think`, because the think's other passes want a rulebook this tier has no
// reason to bind.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumAnimEvent.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumLocomotionSample.h"
#include "ElysiumMoveSolve.h"          // ElysiumMove::U
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSurfaceSounds.h"
#include "ElysiumWorldServices.h"      // FElysiumBodySound, EElysiumSoundChannel
#include "Substrate/ElysiumFootsteps.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumSoundVolumeTable.h"
#include "Tests/ElysiumTestServices.h"

namespace ElysiumPlayerFootstepTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

using namespace ElysiumFootsteps;

namespace
{
	// A body walking dry on the ground, at the speeds the case then overrides. `PcVol` is the
	// shipped `footstep_pc_vol`.
	FStepClockIn DryWalker(float Speed)
	{
		FStepClockIn In;
		In.Speed2D = Speed;
		In.Speed3D = Speed;
		In.bOnGround = true;
		In.PcVol = 0.5f;
		return In;
	}

	// One pass of the clock with the timer already due, so the case reads the arm rather than the
	// countdown. Returns whether a step fired.
	bool StepNow(const FStepClockIn& In, FStepClockOut& Out, int32& WadePhase, float& Clock)
	{
		Clock = 0.f;
		return AdvanceStepClock(Clock, /*DtMs*/ 0.f, WadePhase, In, Out);
	}

	// A surfaceprop row with one wav per foot, so the recorded `Rel` names the FOOT rather than a
	// variation draw — which is what the alternation assertions read.
	FElysiumSurfaceSounds OneFooted(const TCHAR* Folder, const TCHAR* GameMaterial)
	{
		FElysiumSurfaceSounds Row;
		Row.StepLeft.Add(FString::Printf(TEXT("surfaces/%s/stepleft1.wav"), Folder));
		Row.StepRight.Add(FString::Printf(TEXT("surfaces/%s/stepright1.wav"), Folder));
		Row.GameMaterial = GameMaterial;
		return Row;
	}

	// The six player rows of `sound_volume_table.txt`, at the shipped levels: LEVEL_1 is 180 units
	// and LEVEL_2 is 240 (`docs/vtmb/footsteps.md` §2.6).
	FElysiumSoundVolumeTable MakePlayerVolumeTable()
	{
		FElysiumSoundVolumeTable Table;
		FElysiumSoundLevel Quiet;
		Quiet.Level = FElysiumSoundVolumeTable::QuietLevel;
		Quiet.RadiusUnits = 180.f;
		Quiet.bOccludable = true;
		Table.Levels.Add(Quiet);
		FElysiumSoundLevel Normal;
		Normal.Level = FElysiumSoundVolumeTable::NormalLevel;
		Normal.RadiusUnits = 240.f;
		Normal.bOccludable = true;
		Table.Levels.Add(Normal);

		auto AddRow = [&Table](const FName& Category, int32 Level)
		{
			FElysiumSoundCategory Row;
			Row.Name = Category.ToString().ToLower();
			Row.Level = Level;
			Table.Categories.Add(MoveTemp(Row));
		};
		AddRow(ElysiumGameSounds::PlayerFootstepSneak(), FElysiumSoundVolumeTable::QuietLevel);
		AddRow(ElysiumGameSounds::PlayerFootstepWalk(), FElysiumSoundVolumeTable::NormalLevel);
		AddRow(ElysiumGameSounds::PlayerFootstepRun(), FElysiumSoundVolumeTable::NormalLevel);
		AddRow(ElysiumGameSounds::PlayerJump(), FElysiumSoundVolumeTable::NormalLevel);
		AddRow(ElysiumGameSounds::PlayerLandSoft(), FElysiumSoundVolumeTable::QuietLevel);
		AddRow(ElysiumGameSounds::PlayerLandHard(), FElysiumSoundVolumeTable::NormalLevel);
		Table.Reindex();
		return Table;
	}

	// A world with a player and the four surfaces the cases stand on. Nothing is ticked: every case
	// drives the one pass it is asserting.
	struct FPlayerStepFixture
	{
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World;
		FElysiumPlayer* Player = nullptr;

		FPlayerStepFixture()
			: World(nullptr, nullptr, Services.Bundle())
		{
			// Pinned, so the pitch jitter and any variation draw are the same on every machine.
			ElysiumRng::SeedAll(0x46545350);

			FElysiumEntityDefs Defs;
			Defs.MapName = TEXT("__player_footstep_test__");
			World.Load(MoveTemp(Defs));
			World.SpawnPlayer();
			World.Activate(0.0);
			Player = World.FindPlayer();

			Services.SurfaceSounds.Add(FName(TEXT("concrete")), OneFooted(TEXT("concrete"), TEXT("")));
			Services.SurfaceSounds.Add(FName(TEXT("dirt")), OneFooted(TEXT("dirt"), TEXT("D")));
			Services.SurfaceSounds.Add(FName(TEXT("ladder")), OneFooted(TEXT("ladder"), TEXT("")));
			Services.SurfaceSounds.Add(FName(TEXT("water")), OneFooted(TEXT("water"), TEXT("")));
			Services.SurfaceSounds.Add(FName(TEXT("wade")), OneFooted(TEXT("wade"), TEXT("")));
		}

		// A published record for a body walking on `Surface` at `SpeedUnits` Source units/s.
		FElysiumLocomotionSample& Publish(FName Surface, float SpeedUnits)
		{
			FElysiumLocomotionSample Sample;
			Sample.LocalVelocity = FVector(SpeedUnits * ElysiumMove::U, 0.0, 0.0);
			Sample.bOnGround = true;
			Sample.GroundSurface = Surface;
			Services.PlayerLocomotion = Sample;
			return Services.PlayerLocomotion.GetValue();
		}
	};
}

// --- Elysium.Substrate.Footsteps.PlayerClock ----------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlayerStepClockTest,
	"Elysium.Substrate.Footsteps.PlayerClock", GElysiumTestFlags)
bool FElysiumPlayerStepClockTest::RunTest(const FString&)
{
	FStepClockOut Out;
	int32 Wade = 0;
	float Clock = 0.f;

	// --- `ReduceTimers 0x1011f520`: milliseconds, and CLAMPED AT ZERO ------------------------
	// The clamp is the whole reason the `velwalk` band gates nothing: early return #7 tests
	// `m_flStepSoundTime != 0` and the value is always exactly 0 by the time it runs.
	{
		Clock = 400.f;
		FStepClockIn In = DryWalker(150.f);
		TestFalse(TEXT("a clock that has not run out takes no step"),
			AdvanceStepClock(Clock, /*DtMs*/ 100.f, Wade, In, Out));
		TestEqual(TEXT("and is decremented by frametime * 1000"), Clock, 300.f);
		TestFalse(TEXT("still not due"), AdvanceStepClock(Clock, 290.f, Wade, In, Out));
		TestEqual(TEXT("down to 10 ms"), Clock, 10.f, 1.e-4f);
		TestTrue(TEXT("the next pass overshoots and the step fires"),
			AdvanceStepClock(Clock, 50.f, Wade, In, Out));
		TestEqual(TEXT("the clock never goes negative — it is re-armed from zero"),
			Clock, kIntervalDryWalkMs);
	}

	// --- The early returns (§2.2) --------------------------------------------------------------
	{
		FStepClockIn Frozen = DryWalker(0.f);
		TestFalse(TEXT("a body that is not moving horizontally takes no step"),
			StepNow(Frozen, Out, Wade, Clock));

		FStepClockIn Airborne = DryWalker(150.f);
		Airborne.bOnGround = false;
		TestFalse(TEXT("no ladder, no ground and no water is silent"),
			StepNow(Airborne, Out, Wade, Clock));
		Airborne.Water = EElysiumWaterLevel::Feet;
		TestTrue(TEXT("...but water alone keeps the clock alive with no ground under it"),
			StepNow(Airborne, Out, Wade, Clock));

		// The DEAD arm: 3-D speed below `velwalk` (120 dry) does NOT stop the step, because the
		// clock it is ANDed against is always zero here.
		FStepClockIn Crawling = DryWalker(kNormalVelWalk - 1.f);
		TestTrue(TEXT("the velwalk band gates nothing — the clamp killed that arm"),
			StepNow(Crawling, Out, Wade, Clock));
	}

	// --- The dry arm's three intervals ---------------------------------------------------------
	{
		FStepClockIn Walk = DryWalker(150.f);   // below velrun 220, above the 100 slow line
		TestTrue(TEXT("a dry walk steps"), StepNow(Walk, Out, Wade, Clock));
		TestEqual(TEXT("every 400 ms"), Out.NextIntervalMs, kIntervalDryWalkMs);
		TestEqual(TEXT("from the dry pool"), static_cast<int32>(Out.Pool),
			static_cast<int32>(EStepPool::Dry));

		FStepClockIn Run = DryWalker(250.f);
		TestTrue(TEXT("a dry run steps"), StepNow(Run, Out, Wade, Clock));
		TestEqual(TEXT("every 300 ms"), Out.NextIntervalMs, kIntervalDryRunMs);

		FStepClockIn Slow = DryWalker(50.f);
		TestTrue(TEXT("a dry crawl steps"), StepNow(Slow, Out, Wade, Clock));
		TestEqual(TEXT("every 800 ms, once the 2-D speed is at or below 100"),
			Out.NextIntervalMs, kIntervalSlowMs);
		FStepClockIn Boundary = DryWalker(kSlowSpeed2DUnits);
		TestTrue(TEXT("exactly 100 steps"), StepNow(Boundary, Out, Wade, Clock));
		TestEqual(TEXT("and takes the slow interval — the test is `<=`"),
			Out.NextIntervalMs, kIntervalSlowMs);
	}

	// --- `flduck`: the term the tail adds, 0 or 100 — never `velwalk` ---------------------------
	{
		FStepClockIn Ducked = DryWalker(150.f);
		Ducked.bDucked = true;
		TestTrue(TEXT("a ducked body steps"), StepNow(Ducked, Out, Wade, Clock));
		// The ducked band's velrun is 80, so 150 u/s is RUNNING while crouched.
		TestEqual(TEXT("300 ms for the ducked run, plus the 100 ms flduck"),
			Out.NextIntervalMs, kIntervalDryRunMs + kDuckedFlDuck);

		FStepClockIn DuckedSlow = DryWalker(50.f);
		DuckedSlow.bDucked = true;
		TestTrue(TEXT("a ducked crawl steps"), StepNow(DuckedSlow, Out, Wade, Clock));
		TestEqual(TEXT("800 + 100"), Out.NextIntervalMs, kIntervalSlowMs + kDuckedFlDuck);

		// The band constants themselves, so a reader can check them against `0x1011ea30`.
		TestEqual(TEXT("the ducked band is 60/80/100"), kDuckedVelWalk, 60.f);
		TestEqual(TEXT("..."), kDuckedVelRun, 80.f);
		TestEqual(TEXT("..."), kDuckedFlDuck, 100.f);
		TestEqual(TEXT("and the standing band 120/220/0"), kNormalVelWalk, 120.f);
		TestEqual(TEXT("..."), kNormalVelRun, 220.f);
		TestEqual(TEXT("...with nothing added to the interval"), kNormalFlDuck, 0.f);
	}

	// --- The ladder arm ------------------------------------------------------------------------
	{
		FStepClockIn Ladder = DryWalker(150.f);
		Ladder.bOnLadder = true;
		Ladder.bOnGround = false;   // a climbing body is not standing on anything
		TestTrue(TEXT("a climbing body steps"), StepNow(Ladder, Out, Wade, Clock));
		TestEqual(TEXT("from the ladder pool"), static_cast<int32>(Out.Pool),
			static_cast<int32>(EStepPool::Ladder));
		TestEqual(TEXT("every 350 ms plus the flduck a ladder always takes"),
			Out.NextIntervalMs, kIntervalLadderMs + kDuckedFlDuck);
		TestEqual(TEXT("at 0.35 through the tail's footstep_pc_vol"),
			Out.Volume, kVolLadder * 0.5f, 1.e-4f);
		TestEqual(TEXT("the surfaceprop is spelled `ladder`"), LadderSurface(), FName(TEXT("ladder")));
	}

	// --- Feet alternate on the entity, not the pool ---------------------------------------------
	{
		FPlayerStepFixture Fix;
		if (!TestNotNull(TEXT("the player spawned"), Fix.Player))
		{
			return false;
		}
		Fix.Publish(FName(TEXT("concrete")), 150.f);
		// Four ticks at 0.5 s each: the clock is 400 ms, so every tick lands a step.
		for (int32 Step = 0; Step < 4; ++Step)
		{
			Fix.Player->TickStepClock(Step * 0.5, 0.5f);
		}
		if (TestEqual(TEXT("four ticks past the 400 ms interval are four steps"),
			Fix.Services.BodySounds.Num(), 4))
		{
			// `1011e4a4`: `m_nStepside` starts clear, so the FIRST step is `stepright`.
			TestEqual(TEXT("the first step is the right foot"),
				Fix.Services.BodySounds[0].Rel, FString(TEXT("surfaces/concrete/stepright1.wav")));
			TestEqual(TEXT("then the left"),
				Fix.Services.BodySounds[1].Rel, FString(TEXT("surfaces/concrete/stepleft1.wav")));
			TestEqual(TEXT("then the right again"),
				Fix.Services.BodySounds[2].Rel, FString(TEXT("surfaces/concrete/stepright1.wav")));
			TestEqual(TEXT("and the left"),
				Fix.Services.BodySounds[3].Rel, FString(TEXT("surfaces/concrete/stepleft1.wav")));
			// `1011e50b` / `1011e50f`: the player's level and channel are literals, not derived.
			TestEqual(TEXT("the player's own sound level is a fixed 75"),
				Fix.Services.BodySounds[0].SoundLevelDb, kPlayerSoundLevelDb);
			TestEqual(TEXT("on CHAN_BODY"),
				static_cast<int32>(Fix.Services.BodySounds[0].Channel),
				static_cast<int32>(EElysiumSoundChannel::Body));
			for (const FElysiumBodySound& Sound : Fix.Services.BodySounds)
			{
				// `95 + RandomInt(0, 10)`, as a multiplier.
				TestTrue(TEXT("the pitch jitter stays inside 0.95..1.05"),
					Sound.Pitch >= 0.95f - 1.e-4f && Sound.Pitch <= 1.05f + 1.e-4f);
			}
		}

		// Retail's null `surfacedata_t` (`1011e448`): a body standing on nothing the table names is
		// SILENT, and the clock is still re-armed.
		const int32 Before = Fix.Services.BodySounds.Num();
		Fix.Publish(NAME_None, 150.f);
		Fix.Player->TickStepClock(10.0, 0.5f);
		TestEqual(TEXT("a step on no surface plays nothing"),
			Fix.Services.BodySounds.Num(), Before);
	}

	return true;
}

// --- Elysium.Substrate.Footsteps.PlayerVolumes --------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlayerStepVolumeTest,
	"Elysium.Substrate.Footsteps.PlayerVolumes", GElysiumTestFlags)
bool FElysiumPlayerStepVolumeTest::RunTest(const FString&)
{
	const float Pc = 0.5f;   // `footstep_pc_vol`'s retail default

	// --- The three `gamematerial` pairs (`0x1011ed90`'s jump table) ----------------------------
	TestEqual(TEXT("`D` walking is 0.25"),
		PlayerDryVolume(TEXT("D"), true, false, 1.f), kVolDirtWalk, 1.e-4f);
	TestEqual(TEXT("`D` running is 0.55"),
		PlayerDryVolume(TEXT("D"), false, false, 1.f), kVolDirtRun, 1.e-4f);
	TestEqual(TEXT("`V` walking is 0.40"),
		PlayerDryVolume(TEXT("V"), true, false, 1.f), kVolVentWalk, 1.e-4f);
	TestEqual(TEXT("`V` running is 0.70"),
		PlayerDryVolume(TEXT("V"), false, false, 1.f), kVolVentRun, 1.e-4f);
	// Every other letter, and no letter at all, takes the default pair — 37 of the 63 shipped
	// entries declare none anywhere in their `base` chain.
	TestEqual(TEXT("`M` takes the default walking pair"),
		PlayerDryVolume(TEXT("M"), true, false, 1.f), kVolDefaultWalk, 1.e-4f);
	TestEqual(TEXT("and so does a surface with no letter"),
		PlayerDryVolume(FString(), true, false, 1.f), kVolDefaultWalk, 1.e-4f);
	TestEqual(TEXT("...running"),
		PlayerDryVolume(FString(), false, false, 1.f), kVolDefaultRun, 1.e-4f);
	// `E`..`U` sit INSIDE the jump table's range and still take the default: only two letters have
	// their own entry.
	TestEqual(TEXT("`E` is inside the table's range and still default"),
		PlayerDryVolume(TEXT("E"), false, false, 1.f), kVolDefaultRun, 1.e-4f);

	// --- The tail: duck scale, footstep_pc_vol, clamp -------------------------------------------
	TestEqual(TEXT("footstep_pc_vol halves the default walk"),
		PlayerDryVolume(FString(), true, false, Pc), kVolDefaultWalk * Pc, 1.e-4f);
	TestEqual(TEXT("ducking scales by 0.35 BEFORE footstep_pc_vol"),
		PlayerDryVolume(TEXT("V"), false, true, Pc), kVolVentRun * kVolDuckScale * Pc, 1.e-4f);
	TestEqual(TEXT("the duck scale is 0.35"), kVolDuckScale, 0.35f);
	// `0x10449280`: the clamp is at 1, and it is the last thing that happens.
	TestEqual(TEXT("a raised footstep_pc_vol clamps at 1"),
		PlayerDryVolume(TEXT("V"), false, false, 10.f), 1.f, 1.e-4f);
	TestEqual(TEXT("and the clamp constant is 1"), kVolClamp, 1.f);

	// --- The same tail on the three constant arms ------------------------------------------------
	{
		FStepClockOut Out;
		int32 Wade = 0;
		float Clock = 0.f;

		FStepClockIn Water = DryWalker(50.f);
		Water.Water = EElysiumWaterLevel::Feet;
		TestTrue(TEXT("a level-1 step fires"), StepNow(Water, Out, Wade, Clock));
		TestEqual(TEXT("water is authored at 1.0 and lands at footstep_pc_vol"),
			Out.Volume, kVolWater * Pc, 1.e-4f);

		Water.bDucked = true;
		TestTrue(TEXT("a ducked level-1 step fires"), StepNow(Water, Out, Wade, Clock));
		TestEqual(TEXT("and the duck scale reaches the water arm too — the tail is not dry-only"),
			Out.Volume, kVolWater * kVolDuckScale * Pc, 1.e-4f);
	}

	// --- The dry arm through the whole clock, on a real surface row ------------------------------
	{
		FPlayerStepFixture Fix;
		if (!TestNotNull(TEXT("the player spawned"), Fix.Player))
		{
			return false;
		}
		// `dirt` carries `gamematerial D`; 250 u/s is a dry RUN.
		Fix.Publish(FName(TEXT("dirt")), 250.f);
		Fix.Player->TickStepClock(0.0, 0.5f);
		if (TestEqual(TEXT("the run stepped"), Fix.Services.BodySounds.Num(), 1))
		{
			TestEqual(TEXT("`D` running through footstep_pc_vol is 0.275"),
				Fix.Services.BodySounds[0].Volume, kVolDirtRun * Pc, 1.e-4f);
		}
	}

	return true;
}

// --- Elysium.Substrate.Footsteps.PlayerLanding --------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlayerLandingTest,
	"Elysium.Substrate.Footsteps.PlayerLanding", GElysiumTestFlags)
bool FElysiumPlayerLandingTest::RunTest(const FString&)
{
	// --- The safe-fall speed `CheckFalling` recomputes every call (`0x10125e04`) ----------------
	{
		// `sv_jump_boost` 25, `sv_gravity` 800, `rules.txt Jumping/BaseJumpVelocity` 185, the feat-1
		// scalars 1.0 and 0.2 s, floored at `fall_threshold` 200.
		const float Computed = MaxSafeFallSpeedUnits(25.f, 800.f, 185.f, 1.f, 0.2f, 200.f);
		TestEqual(TEXT("the shipped rules put the hard-landing line at 412.75"),
			Computed, kMaxSafeFallSpeed, 1.e-2f);
		TestEqual(TEXT("and the floor wins when the jump is worth nothing"),
			MaxSafeFallSpeedUnits(25.f, 800.f, 0.f, 0.f, 0.f, 200.f), 200.f, 1.e-3f);
		// The two rules-derived speeds, `sqrt(2 * 800 * dist)`.
		TestEqual(TEXT("SafeFallDist 240 is 619.68 u/s"),
			kSafeFallSpeed, FMath::Sqrt(2.f * 800.f * 240.f), 1.e-2f);
		TestEqual(TEXT("SupernaturalFallDist 500 is 894.43 u/s — read and discarded"),
			kSupernaturalFallSpeed, FMath::Sqrt(2.f * 800.f * 500.f), 1.e-2f);
	}

	// --- The volume ladder ----------------------------------------------------------------------
	{
		float Fall = 0.f;
		TestEqual(TEXT("a body that did not fall forces no step"),
			LandingStepVolume(Fall, false, false), 0.f);

		Fall = 300.f;
		TestEqual(TEXT("below the hard line the landing is soft at 0.25"),
			LandingStepVolume(Fall, false, false), kLandVolSoft);
		TestEqual(TEXT("and the fall speed is left alone"), Fall, 300.f);

		Fall = 500.f;   // >= 412.75, < 619.68, > 309.84
		TestEqual(TEXT("a hard dry landing is 0.85"),
			LandingStepVolume(Fall, false, false), kLandVolHard);

		Fall = 700.f;   // > SafeFallSpeed
		TestEqual(TEXT("past the safe-fall speed it is 1.0"),
			LandingStepVolume(Fall, false, false), kLandVolFatal);

		Fall = 500.f;
		TestEqual(TEXT("landing in water is the ratio against the safe-fall speed"),
			LandingStepVolume(Fall, /*bInWater*/ true, false), 500.f / kSafeFallSpeed, 1.e-4f);
		Fall = 900.f;
		TestEqual(TEXT("...clamped at 1"),
			LandingStepVolume(Fall, /*bInWater*/ true, false), 1.f, 1.e-4f);
	}

	// --- The `IsFloating` reduction: a real write, and the only door to the 0.5 arm --------------
	{
		float Fall = 450.f;
		TestEqual(TEXT("standing on a floating entity drops the landing to 0.5"),
			LandingStepVolume(Fall, false, /*bGroundIsFloating*/ true), kLandVolMid);
		TestEqual(TEXT("and the 173 is subtracted from m_flFallVelocity itself"),
			Fall, 450.f - kFloatingFallReduction, 1.e-3f);

		// **0.65 is unreachable with the shipped rules**, even floating: the hard band starts at
		// 412.75 and 412.75 - 173 is 239.75, which is still at or above the 200 threshold. Proven
		// by lowering the recomputed line, which is what a `rules.txt` with no jump would do.
		float Low = 250.f;
		TestEqual(TEXT("with a 200 u/s hard line a floating landing reaches the 0.65 arm"),
			LandingStepVolume(Low, false, true, /*MaxSafeFallSpeed*/ 200.f), kLandVolLow);
		float Shipped = kMaxSafeFallSpeed;
		TestEqual(TEXT("but at the shipped line the same body is still 0.5, never 0.65"),
			LandingStepVolume(Shipped, false, true), kLandVolMid);
		// The pair is INVERTED as shipped: the faster fall is the quieter one.
		TestTrue(TEXT("and the faster fall is the QUIETER of the two"), kLandVolMid < kLandVolLow);
	}

	// --- The clock reset, the double step, and the forced step's own volume ----------------------
	{
		FPlayerStepFixture Fix;
		if (!TestNotNull(TEXT("the player spawned"), Fix.Player))
		{
			return false;
		}
		FElysiumLocomotionSample& Sample = Fix.Publish(FName(TEXT("concrete")), 150.f);
		// Arm the clock so the ordinary pass cannot fire, then land.
		Fix.Player->StepSoundMs = 400.f;
		Sample.FallSpeedAtLanding = 300.f * ElysiumMove::U;   // soft
		Fix.Player->TickStepClock(0.0, 0.001f);

		// `1012617a`: the clock is zeroed, `UpdateStepSound` runs again and fires because its only
		// remaining gate is "moving at all", and the forced step follows on the other foot. **VtMB
		// double-steps on a landing.**
		if (TestEqual(TEXT("a landing plays TWO steps — retail's own double"),
			Fix.Services.BodySounds.Num(), 2))
		{
			TestEqual(TEXT("the ordinary one takes the concrete default at footstep_pc_vol"),
				Fix.Services.BodySounds[0].Volume, kVolDefaultWalk * 0.5f, 1.e-4f);
			TestEqual(TEXT("the forced one takes CheckFalling's ladder UNSCALED"),
				Fix.Services.BodySounds[1].Volume, kLandVolSoft, 1.e-4f);
			TestTrue(TEXT("and they are on opposite feet"),
				Fix.Services.BodySounds[0].Rel != Fix.Services.BodySounds[1].Rel);
		}
		TestEqual(TEXT("the clock was re-armed by the second pass, not left at zero"),
			Fix.Player->StepSoundMs, kIntervalDryWalkMs);

		// The clock is usually DUE on the landing frame — it drained through the jump while early
		// return #5 refused every airborne pass. Retail still plays two: `PlayerMove 0x101274a0`
		// runs the ordinary pass at the TOP of the move, over the previous move's null ground
		// entity, so on the landing frame that pass returns and only `CheckFalling`'s pair sounds.
		// A port that ran the ordinary pass over the post-move grounded sample would play three
		// and flip the foot parity.
		Fix.Services.BodySounds.Reset();
		Fix.Player->StepSoundMs = 0.f;
		Sample.FallSpeedAtLanding = 300.f * ElysiumMove::U;
		Fix.Player->TickStepClock(0.25, 0.25f);
		TestEqual(TEXT("a landing with the clock already due is STILL two steps, not three"),
			Fix.Services.BodySounds.Num(), 2);

		// A landing signal is a ONE-FRAME fact: the next tick with the same published record must
		// not force a second landing step.
		Sample.FallSpeedAtLanding = 0.f;
		const int32 After = Fix.Services.BodySounds.Num();
		Fix.Player->TickStepClock(0.5, 0.5f);
		TestEqual(TEXT("the next frame is one ordinary step and no landing"),
			Fix.Services.BodySounds.Num(), After + 1);
	}

	return true;
}

// --- Elysium.Substrate.Footsteps.PlayerWater ----------------------------------------------------
// The assertions that used to live in `Elysium.Substrate.Water`, against the substrate clock the
// water and wade arms moved into. **Two numbers changed with the move and both are recoveries**:
// the term added to the interval is `flduck` (100) and not `velwalk` (60) — the decompiled C at
// `0x1011ec9c` mislabels the stack slot — and the water band's 60 u/s minimum gates nothing,
// because the arm that reads it is dead.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlayerWaterStepTest,
	"Elysium.Substrate.Footsteps.PlayerWater", GElysiumTestFlags)
bool FElysiumPlayerWaterStepTest::RunTest(const FString&)
{
	FStepClockOut Out;
	int32 Wade = 0;
	float Clock = 0.f;

	// --- Level 1: the `water` pool, every step sounds --------------------------------------------
	{
		FStepClockIn Walk = DryWalker(50.f);
		Walk.Water = EElysiumWaterLevel::Feet;
		TestTrue(TEXT("a level-1 walk steps"), StepNow(Walk, Out, Wade, Clock));
		TestEqual(TEXT("from the water pool"), static_cast<int32>(Out.Pool),
			static_cast<int32>(EStepPool::Water));
		TestEqual(TEXT("every 400 + 100 ms"),
			Out.NextIntervalMs, kIntervalWaterWalkMs + kDuckedFlDuck);
		TestFalse(TEXT("and level 1 has no silent phase"), Out.bSilentPhase);

		// The water band's velrun is 80, so anything at or above it is running.
		FStepClockIn Run = DryWalker(kDuckedVelRun);
		Run.Water = EElysiumWaterLevel::Feet;
		TestTrue(TEXT("a level-1 run steps"), StepNow(Run, Out, Wade, Clock));
		TestEqual(TEXT("every 300 + 100 ms"),
			Out.NextIntervalMs, kIntervalWaterRunMs + kDuckedFlDuck);

		// The DEAD 60 u/s minimum: a body creeping through a puddle still steps.
		FStepClockIn Creep = DryWalker(5.f);
		Creep.Water = EElysiumWaterLevel::Feet;
		TestTrue(TEXT("the water band's 60 u/s minimum gates nothing — that arm is dead"),
			StepNow(Creep, Out, Wade, Clock));
		TestEqual(TEXT("and the water arm has no slow interval of its own"),
			Out.NextIntervalMs, kIntervalWaterWalkMs + kDuckedFlDuck);

		TestEqual(TEXT("the surfaceprop is spelled `water`"),
			WaterSurface(), FName(TEXT("water")));
	}

	// --- Level >= 2: the `wade` pool and its four-phase counter -----------------------------------
	{
		FStepClockIn Wading = DryWalker(150.f);
		Wading.Water = EElysiumWaterLevel::Waist;
		Wade = 0;

		// Phase 0 is SILENT and — the load-bearing half — does not re-arm the clock, so the next
		// move retries immediately rather than waiting out an interval.
		Clock = 0.f;
		TestFalse(TEXT("the first wading step is the silent one"),
			AdvanceStepClock(Clock, 0.f, Wade, Wading, Out));
		TestTrue(TEXT("and it says so"), Out.bSilentPhase);
		TestEqual(TEXT("the clock is NOT re-armed by the silent pass"), Clock, 0.f);
		TestEqual(TEXT("the counter advanced to 1"), Wade, 1);

		for (int32 Phase = 1; Phase <= 3; ++Phase)
		{
			Clock = 0.f;
			TestTrue(TEXT("the other three phases sound"),
				AdvanceStepClock(Clock, 0.f, Wade, Wading, Out));
			TestEqual(TEXT("from the wade pool"), static_cast<int32>(Out.Pool),
				static_cast<int32>(EStepPool::Wade));
			TestEqual(TEXT("every 600 + 100 ms at any speed"),
				Out.NextIntervalMs, kIntervalWadeMs + kDuckedFlDuck);
		}
		TestEqual(TEXT("the counter wrapped back to 0"), Wade, 0);
		Clock = 0.f;
		TestFalse(TEXT("so the cycle is silent, sound, sound, sound"),
			AdvanceStepClock(Clock, 0.f, Wade, Wading, Out));
		TestEqual(TEXT("four phases in the cycle"), kWadePhases, 4);

		TestEqual(TEXT("the surfaceprop is spelled `wade`"), WadeSurface(), FName(TEXT("wade")));
		// Level 3 wades too — the arm is `>= 2`.
		FStepClockIn Deep = DryWalker(150.f);
		Deep.Water = EElysiumWaterLevel::Eyes;
		Wade = 1;
		TestTrue(TEXT("level 3 takes the same arm"), StepNow(Deep, Out, Wade, Clock));
		TestEqual(TEXT("..."), static_cast<int32>(Out.Pool), static_cast<int32>(EStepPool::Wade));
	}

	// --- The pools come off the same baked surfaceprops, through the one seam --------------------
	{
		FPlayerStepFixture Fix;
		if (!TestNotNull(TEXT("the player spawned"), Fix.Player))
		{
			return false;
		}
		// D3/D4: the pool is keyed off the classified LEVEL and never off the material under the
		// foot — the pier's foam cards bind `PM_default`, and a material-keyed rule would give the
		// waterline dry footsteps.
		FElysiumLocomotionSample& Sample = Fix.Publish(FName(TEXT("concrete")), 150.f);
		Sample.Water = EElysiumWaterLevel::Feet;
		Fix.Player->TickStepClock(0.0, 1.0f);
		if (TestEqual(TEXT("a level-1 step drew from PM_water"), Fix.Services.BodySounds.Num(), 1))
		{
			TestEqual(TEXT("...even though the foot is on concrete"),
				Fix.Services.BodySounds[0].Rel, FString(TEXT("surfaces/water/stepright1.wav")));
			TestEqual(TEXT("at 1.0 through footstep_pc_vol"),
				Fix.Services.BodySounds[0].Volume, kVolWater * 0.5f, 1.e-4f);
		}

		Sample.Water = EElysiumWaterLevel::Waist;
		Fix.Player->WadeStepPhase = 1;   // past the silent phase
		Fix.Player->TickStepClock(1.0, 1.0f);
		if (TestEqual(TEXT("a wading step drew from PM_wade"), Fix.Services.BodySounds.Num(), 2))
		{
			TestEqual(TEXT("...on the other foot"),
				Fix.Services.BodySounds[1].Rel, FString(TEXT("surfaces/wade/stepleft1.wav")));
		}
	}

	return true;
}

// --- Elysium.Substrate.Footsteps.PlayerHearing --------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlayerHearingTest,
	"Elysium.Substrate.Footsteps.PlayerHearing", GElysiumTestFlags)
bool FElysiumPlayerHearingTest::RunTest(const FString&)
{
	// --- The priority order (`0x1016b480`) -------------------------------------------------------
	{
		FHearingIn In;
		In.bOnGround = true;
		In.Speed3D = 200.f;
		In.bJumpHeld = true;
		TestEqual(TEXT("IN_JUMP wins over everything, ground or not"),
			HearingCategory(In), ElysiumGameSounds::PlayerJump());

		In.bJumpHeld = false;
		In.bOnGround = false;
		TestEqual(TEXT("in the air and not jumping is SILENT"), HearingCategory(In), FName(NAME_None));

		In.bOnGround = true;
		In.bLandSoft = true;
		TestEqual(TEXT("anim state 8 is the soft landing"),
			HearingCategory(In), ElysiumGameSounds::PlayerLandSoft());
		In.bLandSoft = false;
		In.bLandHard = true;
		TestEqual(TEXT("10 and 11 are the hard one"),
			HearingCategory(In), ElysiumGameSounds::PlayerLandHard());
		In.bLandHard = false;

		In.Speed3D = 0.f;
		TestEqual(TEXT("standing still on the ground is silent"),
			HearingCategory(In), FName(NAME_None));

		In.Speed3D = 60.f;
		In.bDucked = true;
		TestEqual(TEXT("ducking is the WHOLE of sneaking on this path"),
			HearingCategory(In), ElysiumGameSounds::PlayerFootstepSneak());
		In.bDucked = false;
		TestEqual(TEXT("below PLAYER_RUN_SPEED is the walk row"),
			HearingCategory(In), ElysiumGameSounds::PlayerFootstepWalk());
		In.Speed3D = kPlayerRunSpeedUnits;
		TestEqual(TEXT("exactly 128 is still walking — the test is a strict `>`"),
			HearingCategory(In), ElysiumGameSounds::PlayerFootstepWalk());
		In.Speed3D = kPlayerRunSpeedUnits + 1.f;
		TestEqual(TEXT("above it is the run row"),
			HearingCategory(In), ElysiumGameSounds::PlayerFootstepRun());
		TestEqual(TEXT("PLAYER_RUN_SPEED is 128"), kPlayerRunSpeedUnits, 128.f);

		// The water level is NEVER consulted: wading emits footsteps and swimming emits nothing,
		// because losing ground contact is what silences it.
		In.Speed3D = 60.f;
		TestEqual(TEXT("a wading body still emits the walk row"),
			HearingCategory(In), ElysiumGameSounds::PlayerFootstepWalk());
	}

	// --- The shipped radii and the 250 u/s decay --------------------------------------------------
	{
		TestEqual(TEXT("sneak carries 180 units"),
			HearingRadiusUnits(ElysiumGameSounds::PlayerFootstepSneak()), 180.f);
		TestEqual(TEXT("walk 240"),
			HearingRadiusUnits(ElysiumGameSounds::PlayerFootstepWalk()), 240.f);
		TestEqual(TEXT("run the SAME 240 — the split changes the name and nothing else"),
			HearingRadiusUnits(ElysiumGameSounds::PlayerFootstepRun()), 240.f);
		TestEqual(TEXT("jump 240"), HearingRadiusUnits(ElysiumGameSounds::PlayerJump()), 240.f);
		TestEqual(TEXT("the soft landing 180"),
			HearingRadiusUnits(ElysiumGameSounds::PlayerLandSoft()), 180.f);
		TestEqual(TEXT("the hard one 240"),
			HearingRadiusUnits(ElysiumGameSounds::PlayerLandHard()), 240.f);
		TestEqual(TEXT("and a name that is not one of the six carries nothing"),
			HearingRadiusUnits(FName(TEXT("DOOR_NORMAL"))), 0.f);

		TestEqual(TEXT("the volume rises INSTANTLY"),
			DecayHearingRadiusUnits(0.f, 240.f, 0.001f), 240.f);
		TestEqual(TEXT("and falls at 250 units per second"),
			DecayHearingRadiusUnits(240.f, 0.f, 0.1f), 215.f, 1.e-3f);
		TestEqual(TEXT("never past the target"),
			DecayHearingRadiusUnits(240.f, 0.f, 10.f), 0.f);
		TestEqual(TEXT("a sprint heard once keeps ringing for about a second"),
			DecayHearingRadiusUnits(240.f, 0.f, 1.f), 0.f, 1.e-3f);
		TestEqual(TEXT("the decay rate is 250"), kHearingDecayUnitsPerSecond, 250.f);
	}

	// --- The producer: ONE stimulus, rewritten, never a queue of them -----------------------------
	{
		// Declared BEFORE the fixture on purpose: members and locals destruct in reverse order, and
		// the bus holds a raw pointer to this table for the world's lifetime.
		FElysiumSoundVolumeTable Volumes = MakePlayerVolumeTable();
		FPlayerStepFixture Fix;
		if (!TestNotNull(TEXT("the player spawned"), Fix.Player))
		{
			return false;
		}
		Fix.World.GameSounds().SetVolumeTable(&Volumes);

		FElysiumLocomotionSample& Sample = Fix.Publish(FName(TEXT("concrete")), 200.f);
		Fix.Player->UpdatePlayerSound(0.0);
		TestEqual(TEXT("a running body stamps one stimulus"), Fix.World.GameSounds().NumRetained(), 1);
		if (Fix.World.GameSounds().NumRetained() == 1)
		{
			const FElysiumGameSoundEvent& Event = Fix.World.GameSounds().Retained()[0];
			TestEqual(TEXT("in the run category"), Event.Category,
				ElysiumGameSounds::PlayerFootstepRun());
			TestEqual(TEXT("at the table's 240 units, in centimetres"),
				Event.RadiusCm, 240.f * ElysiumMove::U, 1.e-2f);
			TestTrue(TEXT("owned by the player"), Event.Source == Fix.Player->Handle);
		}

		// The second think REWRITES the same slot rather than adding to the window: retail owns one
		// permanently reserved `CSound` and never inserts.
		Sample.LocalVelocity = FVector(60.0 * ElysiumMove::U, 0.0, 0.0);
		Fix.Player->UpdatePlayerSound(0.1);
		TestEqual(TEXT("a hundred thinks later there is still exactly one"),
			Fix.World.GameSounds().NumRetained(), 1);
		if (Fix.World.GameSounds().NumRetained() == 1)
		{
			const FElysiumGameSoundEvent& Event = Fix.World.GameSounds().Retained()[0];
			TestEqual(TEXT("now the walk row"), Event.Category,
				ElysiumGameSounds::PlayerFootstepWalk());
			// Walk and run resolve to the same level, so nothing decays here.
			TestEqual(TEXT("still 240 units"), Event.RadiusCm, 240.f * ElysiumMove::U, 1.e-2f);
		}
		const uint64 AfterTwo = Fix.World.GameSounds().LastSerial();
		TestTrue(TEXT("...and it re-serialises, so a consumer's cursor sees it again"), AfterTwo > 1);
		TestEqual(TEXT("a replaced slot is not an eviction"),
			Fix.World.GameSounds().NumEvicted(), 0);

		// Ducking is sneaking: the target drops to 180 and the slot DECAYS towards it rather than
		// snapping.
		Sample.Stance = EElysiumStance::Ducked;
		Fix.Player->UpdatePlayerSound(0.2);
		if (Fix.World.GameSounds().NumRetained() == 1)
		{
			const FElysiumGameSoundEvent& Event = Fix.World.GameSounds().Retained()[0];
			TestEqual(TEXT("the category becomes sneak at once"), Event.Category,
				ElysiumGameSounds::PlayerFootstepSneak());
			TestEqual(TEXT("but the radius only falls 25 units in that 0.1 s"),
				Event.RadiusCm, 215.f * ElysiumMove::U, 1.e-2f);
		}

		// Standing still targets zero, and once it gets there the slot is retired — retail's
		// volume 0, which every listener ignores.
		Sample.LocalVelocity = FVector::ZeroVector;
		Fix.Player->UpdatePlayerSound(10.0);
		TestEqual(TEXT("a body that stopped long enough leaves no stimulus at all"),
			Fix.World.GameSounds().NumRetained(), 0);

		// `FL_NOTARGET` (`1016b4b8`) and `m_fNoPlayerSound` (`1016b610`): the record is written at
		// volume 0 whatever the body does, and comes back the think after the switch clears.
		Sample.LocalVelocity = FVector(200.0 * ElysiumMove::U, 0.0, 0.0);
		Fix.Player->bNoPlayerSound = true;
		Fix.Player->UpdatePlayerSound(10.1);
		TestEqual(TEXT("with the player sound killed a sprint raises nothing"),
			Fix.World.GameSounds().NumRetained(), 0);
		Fix.Player->bNoPlayerSound = false;
		Fix.Player->UpdatePlayerSound(10.2);
		TestEqual(TEXT("and it comes back the think after"),
			Fix.World.GameSounds().NumRetained(), 1);
	}

	return true;
}

// --- Elysium.Substrate.Footsteps.PlayerSwallows -------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlayerSwallowsTest,
	"Elysium.Substrate.Footsteps.PlayerSwallows", GElysiumTestFlags)
bool FElysiumPlayerSwallowsTest::RunTest(const FString&)
{
	TestFalse(TEXT("2049 is not a footfall"), PlayerSwallows(2049));
	TestTrue(TEXT("2050 is"), PlayerSwallows(2050));
	TestTrue(TEXT("2051"), PlayerSwallows(2051));
	TestTrue(TEXT("2052"), PlayerSwallows(2052));
	TestTrue(TEXT("2053"), PlayerSwallows(2053));
	TestFalse(TEXT("2054 is not"), PlayerSwallows(2054));

	FPlayerStepFixture Fix;
	if (!TestNotNull(TEXT("the player spawned"), Fix.Player))
	{
		return false;
	}
	Fix.Publish(FName(TEXT("concrete")), 150.f);

	for (int32 Id = 2050; Id <= 2053; ++Id)
	{
		FElysiumAnimEvent Event;
		Event.Event = Id;
		TestTrue(TEXT("the player CLAIMS every footfall record its clips carry"),
			Fix.Player->HandleAnimEvent(Event));
	}
	// Claimed and SILENT: the player's steps come off the clock, so a record that played one would
	// double every step.
	TestEqual(TEXT("and plays nothing for any of them"), Fix.Services.BodySounds.Num(), 0);

	// An id outside the band still falls through to the combat-character chain, which owns nothing
	// here and answers false — an ordinary negative, not a failure.
	FElysiumAnimEvent Other;
	Other.Event = 2054;
	TestFalse(TEXT("an id outside the band falls to the base handler"),
		Fix.Player->HandleAnimEvent(Other));

	return true;
}

// --- Elysium.Substrate.Footsteps.PlayerServerFootstepsOff ---------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlayerServerFootstepsOffTest,
	"Elysium.Substrate.Footsteps.PlayerServerFootstepsOff", GElysiumTestFlags)
bool FElysiumPlayerServerFootstepsOffTest::RunTest(const FString&)
{
	// `sv_footsteps` (`0x109ef090`) gates `UpdateStepSound` UNCONDITIONALLY — the top of the
	// function, regardless of `maxClients`, unlike `PlayStepSound`'s own copy of the test. It does
	// NOT gate the NPC animation-event path, which `0x1026d460` runs without consulting it.
	{
		FStepClockOut Out;
		int32 Wade = 0;
		float Clock = 250.f;
		FStepClockIn In = DryWalker(150.f);
		In.bServerFootsteps = false;

		TestFalse(TEXT("sv_footsteps 0 takes no step"),
			AdvanceStepClock(Clock, 300.f, Wade, In, Out));
		// `ReduceTimers` is a different function and runs from `PlayerMove` regardless, so the clock
		// still drains — turning footsteps back on does not owe the player a stored-up step.
		TestEqual(TEXT("but the clock still drains, because ReduceTimers is not gated"),
			Clock, 0.f);

		In.bServerFootsteps = true;
		TestTrue(TEXT("and turning it back on steps on the very next pass"),
			AdvanceStepClock(Clock, 0.f, Wade, In, Out));
	}

	// Through the whole producer, off the world's own cvar surface.
	{
		FPlayerStepFixture Fix;
		if (!TestNotNull(TEXT("the player spawned"), Fix.Player))
		{
			return false;
		}
		TestTrue(TEXT("sv_footsteps defaults to 1"), Fix.World.FootstepTuning().bServerFootsteps);
		Fix.World.FootstepTuning().bServerFootsteps = false;
		Fix.Publish(FName(TEXT("concrete")), 150.f);
		Fix.Player->TickStepClock(0.0, 0.5f);
		TestEqual(TEXT("a walking player makes no sound with sv_footsteps 0"),
			Fix.Services.BodySounds.Num(), 0);

		Fix.World.FootstepTuning().bServerFootsteps = true;
		Fix.Player->TickStepClock(0.5, 0.5f);
		TestEqual(TEXT("and steps again the moment it is back"),
			Fix.Services.BodySounds.Num(), 1);
	}

	return true;
}

} // namespace ElysiumPlayerFootstepTests

#endif // WITH_DEV_AUTOMATION_TESTS
