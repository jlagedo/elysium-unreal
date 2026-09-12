// Content-free Substrate automation: the NPC sensory transaction — resolved perception tuning,
// the sight cache and its cadence/grace, the committed-enemy LOS debounce and its output edges,
// hearing off the game-sound bus, and what all of it remembers across a save.
//
// Everything under test is substrate rule. The engine's only contribution is one world term —
// is the segment between two points clear — which the recording services script through
// `bLineOfSightClear`, so a case can put a wall between two entities without a collision world.
//
// The authored facts: `docs/vtmb/stealth.md` -> "Visual observer transaction" / "Auditory
// stealth", and `docs/vtmb/npc-ai-reverse-engineering.md` -> "Perception, sound, memory, and
// hostility".

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"
#include "Substrate/ElysiumRelationships.h"
#include "ElysiumKeyValues.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSaveTypes.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumStealthKillRules.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumItemTable.h"
#include "Substrate/ElysiumSheetMath.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumSoundVolumeTable.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumTestServices.h"

#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace ElysiumNpcSensesTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	float Cm(float Units) { return Units * ElysiumMove::U; }

	bool NearlyEqual(float A, float B, float Tolerance = 0.05f)
	{
		return FMath::Abs(A - B) < Tolerance;
	}

	// --- The two `Inspection` feat tables, authored exactly as `feats.txt` writes them ----------
	// Both carry `Clamping 1`, and both stop at row 10 while the feat's own `MaxValue` is 20.
	FElysiumRuleTable MakeVisionTable()
	{
		FElysiumRuleTable Table;
		Table.InternalName = ElysiumNpcSense::VisionTableName;
		Table.Name = Table.InternalName;
		Table.TraitDependency = TEXT("Inspection");
		Table.bClamping = true;
		const TCHAR* const Rows[] = { TEXT("60"), TEXT("290"), TEXT("380"), TEXT("440"),
			TEXT("480"), TEXT("510"), TEXT("540"), TEXT("590"), TEXT("660"), TEXT("750"),
			TEXT("2400") };
		for (int32 i = 0; i < UE_ARRAY_COUNT(Rows); ++i)
		{
			Table.Rows.Add(i, Rows[i]);
		}
		return Table;
	}

	FElysiumRuleTable MakeHearingTable()
	{
		FElysiumRuleTable Table;
		Table.InternalName = ElysiumNpcSense::HearingTableName;
		Table.Name = Table.InternalName;
		Table.TraitDependency = TEXT("Inspection");
		Table.bClamping = true;
		const TCHAR* const Rows[] = { TEXT("0.71"), TEXT("0.71"), TEXT("0.86"), TEXT("1.00"),
			TEXT("1.14"), TEXT("1.29"), TEXT("1.43"), TEXT("1.57"), TEXT("1.71"), TEXT("1.86"),
			TEXT("2.00") };
		for (int32 i = 0; i < UE_ARRAY_COUNT(Rows); ++i)
		{
			Table.Rows.Add(i, Rows[i]);
		}
		return Table;
	}

	// --- The sound-volume table the hearing cases emit against -----------------------------------
	// One occludable level and one that no wall stops, which is the distinction the whole
	// occlusion half of hearing turns on.
	FElysiumSoundVolumeTable MakeVolumeTable()
	{
		FElysiumSoundVolumeTable Table;

		auto AddLevel = [&Table](int32 Index, float RadiusUnits, bool bOccludable)
		{
			FElysiumSoundLevel Row;
			Row.Level = Index;
			Row.RadiusUnits = RadiusUnits;
			Row.bOccludable = bOccludable;
			Table.Levels.Add(Row);
		};
		AddLevel(FElysiumSoundVolumeTable::SilentLevel, 0.f, false);
		AddLevel(FElysiumSoundVolumeTable::QuietLevel, 180.f, true);
		AddLevel(FElysiumSoundVolumeTable::NormalLevel, 240.f, true);
		AddLevel(FElysiumSoundVolumeTable::LoudLevel, 1200.f, false);
		AddLevel(FElysiumSoundVolumeTable::PlayerStealthLevel, 120.f, true);
		AddLevel(FElysiumSoundVolumeTable::FeedingLevel, 240.f, true);

		auto AddCategory = [&Table](const TCHAR* Name, int32 Level)
		{
			FElysiumSoundCategory Row;
			Row.Name = FString(Name).ToLower();
			Row.Level = Level;
			Table.Categories.Add(MoveTemp(Row));
		};
		AddCategory(TEXT("PLAYER_GUNSHOT_BASE"), FElysiumSoundVolumeTable::LoudLevel);
		AddCategory(TEXT("NPC_TAKE_DAMAGE"), FElysiumSoundVolumeTable::NormalLevel);
		AddCategory(TEXT("DOOR_NORMAL"), FElysiumSoundVolumeTable::NormalLevel);
		AddCategory(TEXT("PLAYER_FOOTSTEP_SNEAK"), FElysiumSoundVolumeTable::QuietLevel);
		Table.Reindex();
		return Table;
	}

	// --- The fixture ------------------------------------------------------------------------------
	// One guard facing +X at the origin, three counters wired off the outputs under test, and a
	// player the caller places. `vision`/`hearing` are authored so the derive path — and its
	// headless fallback — stays out of every case that is not about it.
	struct FSensesFixture
	{
		FElysiumNpcWorldFixture Fixture;
		FElysiumRecordingServices& Services;
		FElysiumEntityWorld& World;
		FElysiumNpc* Guard = nullptr;
		FElysiumPlayer* Player = nullptr;

		static FElysiumNpcWorldBuilder BuildWorld(float VisionUnits, float HearingScalar)
		{
			FElysiumNpcWorldBuilder Builder(TEXT("__npcsenses_test__"), 0x53454E53);

			FElysiumEntityDef& GuardDef = Builder.AddNpc(TEXT("guard"));
			GuardDef.Keys.Add(TEXT("vision"), FString::SanitizeFloat(VisionUnits));
			GuardDef.Keys.Add(TEXT("hearing"), FString::SanitizeFloat(HearingScalar));
			Builder.WireOutput(TEXT("guard"), TEXT("OnFoundEnemy"),    TEXT("c_foundenemy"));
			Builder.WireOutput(TEXT("guard"), TEXT("OnFoundPlayer"),   TEXT("c_foundplayer"));
			Builder.WireOutput(TEXT("guard"), TEXT("OnLostEnemyLOS"),  TEXT("c_lostenemylos"));
			Builder.WireOutput(TEXT("guard"), TEXT("OnLostPlayerLOS"), TEXT("c_lostplayerlos"));
			Builder.WireOutput(TEXT("guard"), TEXT("OnHearCombat"),    TEXT("c_hearcombat"));
			Builder.WireOutput(TEXT("guard"), TEXT("OnHearPlayer"),    TEXT("c_hearplayer"));
			Builder.WireOutput(TEXT("guard"), TEXT("OnHearWorld"),     TEXT("c_hearworld"));

			for (const TCHAR* Name : { TEXT("c_foundenemy"), TEXT("c_foundplayer"),
				TEXT("c_lostenemylos"), TEXT("c_lostplayerlos"), TEXT("c_hearcombat"),
				TEXT("c_hearplayer"), TEXT("c_hearworld") })
			{
				Builder.AddCounter(Name);
			}
			return Builder;
		}

		explicit FSensesFixture(float VisionUnits = 4000.f, float HearingScalar = 1.0f)
			: Fixture(BuildWorld(VisionUnits, HearingScalar))
			, Services(Fixture.Services)
			, World(Fixture.World)
		{
			Guard = Fixture.Npc(TEXT("guard"));
			Player = Fixture.Player();
		}

		// Let the queue deliver without letting the guard's own think re-run the senses: every
		// case here drives the pass it is asserting explicitly.
		void Flush(double Now)
		{
			FElysiumNpcWorldFixture::Quiet({ Guard });
			World.Tick(Now);
		}

		float Counter(const TCHAR* Name)
		{
			return Fixture.Counter(Name);
		}
	};
}


// `InitPerceptionDistances`: the `-1.0` sentinel, the two inspection tables, and what an
// unreachable table falls back to.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcSensesPerceptionTest,
	"Elysium.Substrate.NpcSenses.Perception", GElysiumTestFlags)
bool FElysiumNpcSensesPerceptionTest::RunTest(const FString&)
{
	const FElysiumRuleTable Vision = MakeVisionTable();
	const FElysiumRuleTable Hearing = MakeHearingTable();

	// --- An authored value is copied through, and npc_perception is inert for that channel ------
	{
		FElysiumNpcPerception P;
		FString Warning;
		P.Resolve(/*npc_perception*/ 9, /*vision*/ 900.f, /*hearing*/ 1.5f,
			&Vision, &Hearing, Warning);
		TestTrue(TEXT("an authored vision distance is copied through, converted to cm"),
			NearlyEqual(P.VisionDistanceCm, Cm(900.f)));
		TestTrue(TEXT("an authored hearing scalar is copied through verbatim"),
			NearlyEqual(P.HearingScalar, 1.5f));
		TestTrue(TEXT("resolution ran"), P.bResolved);
		TestFalse(TEXT("nothing fell back"), P.bUsedFallback);
		TestTrue(TEXT("...and nothing is reported"), Warning.IsEmpty());
	}

	// --- The sentinel derives from npc_perception through the two tables -------------------------
	{
		FElysiumNpcPerception P;
		FString Warning;
		P.Resolve(5, ElysiumNpcSense::DerivedSentinel, ElysiumNpcSense::DerivedSentinel,
			&Vision, &Hearing, Warning);
		TestTrue(TEXT("perception 5 derives 510 units of vision"),
			NearlyEqual(P.VisionDistanceCm, Cm(510.f)));
		TestTrue(TEXT("...and a 1.29 hearing scalar"), NearlyEqual(P.HearingScalar, 1.29f));
		TestFalse(TEXT("a real table is not a fallback"), P.bUsedFallback);
		TestTrue(TEXT("...and is silent"), Warning.IsEmpty());
	}

	// --- One channel may derive while the other is authored --------------------------------------
	{
		FElysiumNpcPerception P;
		FString Warning;
		P.Resolve(0, ElysiumNpcSense::DerivedSentinel, /*hearing*/ 2.5f,
			&Vision, &Hearing, Warning);
		TestTrue(TEXT("the sentinel channel derives"), NearlyEqual(P.VisionDistanceCm, Cm(60.f)));
		TestTrue(TEXT("the authored channel does not"), NearlyEqual(P.HearingScalar, 2.5f));
	}

	// --- Both tables author Clamping, so a value past the last row takes it -----------------------
	{
		FElysiumNpcPerception P;
		FString Warning;
		P.Resolve(/*the feat's MaxValue, past the authored rows*/ 20,
			ElysiumNpcSense::DerivedSentinel, ElysiumNpcSense::DerivedSentinel,
			&Vision, &Hearing, Warning);
		TestTrue(TEXT("a perception past the last authored row clamps to it"),
			NearlyEqual(P.VisionDistanceCm, Cm(2400.f)));
		TestTrue(TEXT("...on both tables"), NearlyEqual(P.HearingScalar, 2.0f));
	}

	// --- An unreachable table falls back to the average-human row AND says so --------------------
	{
		FElysiumNpcPerception P;
		FString Warning;
		P.Resolve(7, ElysiumNpcSense::DerivedSentinel, ElysiumNpcSense::DerivedSentinel,
			nullptr, nullptr, Warning);
		TestTrue(TEXT("a missing table takes the average-human vision row"),
			NearlyEqual(P.VisionDistanceCm, Cm(ElysiumNpcSense::FallbackVisionUnits)));
		TestTrue(TEXT("...and its hearing row"),
			NearlyEqual(P.HearingScalar, ElysiumNpcSense::FallbackHearingScalar));
		TestTrue(TEXT("the fallback is flagged"), P.bUsedFallback);
		TestFalse(TEXT("...and reported rather than taken quietly"), Warning.IsEmpty());
		TestTrue(TEXT("the report names the table that was missing"),
			Warning.Contains(ElysiumNpcSense::VisionTableName));
	}

	// --- A missing table for a channel that does not derive is not a failure ---------------------
	{
		FElysiumNpcPerception P;
		FString Warning;
		P.Resolve(3, 600.f, 1.0f, nullptr, nullptr, Warning);
		TestFalse(TEXT("both channels authored: no table is needed"), P.bUsedFallback);
		TestTrue(TEXT("...so nothing is reported"), Warning.IsEmpty());
	}
	return true;
}


// `SetClosestPlayer` + `SetPlayerLOS`: the 2 s cadence, the 512-unit no-trace bypass, the
// far trace, and the eight-second in-cone grace.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcSensesSightTest,
	"Elysium.Substrate.NpcSenses.Sight", GElysiumTestFlags)
bool FElysiumNpcSensesSightTest::RunTest(const FString&)
{
	// --- Inside 512 units and in cone: seen, with no trace at all ---------------------------------
	{
		FSensesFixture F;
		if (!TestNotNull(TEXT("the guard leaf constructs"), F.Guard)
			|| !TestNotNull(TEXT("the player exists"), F.Player))
		{
			return false;
		}
		F.Player->Origin = FVector(Cm(100.f), 0.0, 0.0);   // straight ahead, well inside 512
		F.Services.Calls.Reset();
		F.Guard->Senses.SetClosestPlayer(*F.Guard, 10.0);
		F.Guard->Senses.TickSight(*F.Guard, 10.0);

		TestTrue(TEXT("an in-cone target inside 512 units is seen"), F.Guard->Senses.Memory.bPlayerVisible);
		TestTrue(TEXT("actual Look still traces inside the bookkeeping bypass"),
			F.Services.Saw(TEXT("QueryLineOfSight")));
		TestTrue(TEXT("the closest-player cache carries the handle"),
			F.Guard->Senses.Memory.ClosestPlayer == F.Player->Handle);
		TestTrue(TEXT("...and the distance"),
			NearlyEqual(F.Guard->Senses.Memory.ClosestPlayerDistanceCm, Cm(100.f), 1.0f));
		TestTrue(TEXT("the target is in range"), F.Guard->Senses.Memory.bPlayerInRange);
		TestFalse(TEXT("...and well inside the outer band"),
			F.Guard->Senses.Memory.bPlayerInOuterBand);

		// The sighting is NOT on `SetPlayerLOS`'s 2 s cache. Retail rebuilds it on every `Look`,
		// so a target that steps behind the observer is out of cone on the very next pass.
		F.Player->Origin = FVector(Cm(-100.f), 0.0, 0.0);   // directly behind the guard
		F.Guard->Senses.TickSight(*F.Guard, 11.0);
		TestFalse(TEXT("a target behind the observer is out of cone"),
			F.Guard->Senses.Memory.bPlayerInCone);
		TestFalse(TEXT("...and therefore not seen on the same pass it moved"),
			F.Guard->Senses.Memory.bPlayerVisible);
	}

	// --- `SetClosestPlayer` `0x10293a80` --------------------------------------------------------
	{
		FSensesFixture F;
		if (F.Guard == nullptr || F.Player == nullptr)
		{
			return false;
		}
		// Ungated: retail's 2 s cache is on `SetPlayerLOS` alone, so the pair is current on every
		// normal think and the three distance-driven interval laws never read a stale one.
		F.Player->Origin = FVector(Cm(100.f), 0.0, 0.0);
		F.Guard->Senses.SetClosestPlayer(*F.Guard, 1.0);
		TestTrue(TEXT("the pair is the nearest player"),
			F.Guard->Senses.Memory.ClosestPlayer == F.Player->Handle);
		F.Player->Origin = FVector(Cm(300.f), 0.0, 0.0);
		F.Guard->Senses.SetClosestPlayer(*F.Guard, 1.1);
		TestTrue(TEXT("and it is re-measured with no cadence in the way"),
			NearlyEqual(F.Guard->Senses.Memory.ClosestPlayerDistanceCm, Cm(300.f), 1.0f));

		// The `20000.0` seed is a search bound, not a clamp: past it the handle goes invalid and
		// the distance sits at the seed, which is the "no closest player" arm of every law.
		F.Player->Origin = FVector(Cm(25000.f), 0.0, 0.0);
		F.Guard->Senses.SetClosestPlayer(*F.Guard, 1.2);
		TestFalse(TEXT("a player past the search bound is not the closest player"),
			F.Guard->Senses.Memory.ClosestPlayer.IsSet());
		TestTrue(TEXT("...and the distance reads the seed"),
			NearlyEqual(F.Guard->Senses.Memory.ClosestPlayerDistanceCm,
				ElysiumNpcSense::ClosestPlayerSearchUnits * ElysiumMove::U, 1.0f));
	}

	// --- `SetPlayerLOS` `0x10291610`: no cone, a PVS term, 2 s / 512 / 8 s ----------------------
	{
		FSensesFixture F;
		if (F.Guard == nullptr || F.Player == nullptr)
		{
			return false;
		}
		// Directly BEHIND the guard, and past the near bypass so the trace decides. Retail's cached
		// LOS carries no cone term at all, which is the whole reason it is not the sighting.
		F.Player->Origin = FVector(Cm(-1000.f), 0.0, 0.0);
		F.Guard->Senses.SetClosestPlayer(*F.Guard, 1.0);
		F.Guard->Senses.SetPlayerLos(*F.Guard, 1.0);
		TestTrue(TEXT("a player behind the observer still has cached LOS"),
			F.Guard->Senses.Memory.bPlayerLos);
		TestFalse(TEXT("...while the sighting stays false"),
			F.Guard->Senses.Memory.bPlayerVisible);

		// The 2 s gate. A wall goes up but the refresh is not due, so nothing is re-asked.
		F.Services.bLineOfSightClear = false;
		F.Services.Calls.Reset();
		F.Guard->Senses.SetPlayerLos(*F.Guard, 2.0);
		TestFalse(TEXT("inside the 2 s cadence no segment is asked for"),
			F.Services.Saw(TEXT("QueryLineOfSight")));

		// Past the gate the trace runs and fails -- but the 8 s hysteresis holds LOS up, because
		// the body is still in the same PVS.
		F.Guard->Senses.SetPlayerLos(*F.Guard, 4.0);
		TestTrue(TEXT("blocked in the same PVS keeps LOS inside the 8 s grace"),
			F.Guard->Senses.Memory.bPlayerLos);
		F.Guard->Senses.SetPlayerLos(*F.Guard, 10.0);
		TestFalse(TEXT("...and drops once the grace has run out"),
			F.Guard->Senses.Memory.bPlayerLos);

		// Out of PVS: no trace, no grace. The hysteresis is explicitly PVS-gated.
		F.Services.bLineOfSightClear = true;
		F.Guard->Senses.SetPlayerLos(*F.Guard, 12.0);
		TestTrue(TEXT("a clear segment restores cached LOS"), F.Guard->Senses.Memory.bPlayerLos);
		F.Services.PvsQuery = [](const FVector&, const FVector&) { return false; };
		F.Guard->Senses.SetPlayerLos(*F.Guard, 14.0);
		TestFalse(TEXT("out of PVS is out of LOS with no grace"),
			F.Guard->Senses.Memory.bPlayerLos);
		TestFalse(TEXT("...and the PVS byte says so"), F.Guard->Senses.Memory.bPlayerInPvs);

		// Inside 512 units LOS is true with no trace at all.
		F.Services.PvsQuery = nullptr;
		F.Services.bLineOfSightClear = false;
		F.Player->Origin = FVector(Cm(-100.f), 0.0, 0.0);
		F.Guard->Senses.SetClosestPlayer(*F.Guard, 16.0);
		F.Services.Calls.Reset();
		F.Guard->Senses.SetPlayerLos(*F.Guard, 16.0);
		TestTrue(TEXT("inside 512 units LOS is true without a trace"),
			F.Guard->Senses.Memory.bPlayerLos);
		TestFalse(TEXT("...and no segment was asked for"),
			F.Services.Saw(TEXT("QueryLineOfSight")));
	}

	// --- Beyond 512 units: the far trace decides, and blocked-in-cone keeps 8 s of grace ---------
	{
		FSensesFixture F;
		if (F.Guard == nullptr || F.Player == nullptr)
		{
			return false;
		}
		F.Player->Origin = FVector(Cm(1000.f), 0.0, 0.0);   // in cone, past the near bypass
		F.Services.Calls.Reset();
		F.Guard->Senses.TickSight(*F.Guard, 100.0);
		TestTrue(TEXT("beyond 512 units the engine is asked for the segment"),
			F.Services.Saw(TEXT("QueryLineOfSight")));
		TestTrue(TEXT("a clear far segment is sight"), F.Guard->Senses.Memory.bPlayerVisible);
		TestTrue(TEXT("...and stamps the sighting's own last-clear time"),
			NearlyEqual(static_cast<float>(F.Guard->Senses.Memory.SightingLastClearTime), 100.0f));

		// A wall goes up. The player is still in cone, so sight is preserved for eight seconds.
		F.Services.bLineOfSightClear = false;
		F.Guard->Senses.TickSight(*F.Guard, 104.0);
		TestTrue(TEXT("blocked but in cone: sight is held inside the 8 s grace"),
			F.Guard->Senses.Memory.bPlayerVisible);
		F.Guard->Senses.TickSight(*F.Guard, 108.0);
		TestTrue(TEXT("...right up to the edge of it"), F.Guard->Senses.Memory.bPlayerVisible);
		F.Guard->Senses.TickSight(*F.Guard, 110.0);
		TestFalse(TEXT("...and drops once the grace has run out"),
			F.Guard->Senses.Memory.bPlayerVisible);

		// The wall comes down: an ordinary clear trace restores sight and the clock.
		F.Services.bLineOfSightClear = true;
		F.Guard->Senses.TickSight(*F.Guard, 112.5);
		TestTrue(TEXT("a clear segment restores sight"), F.Guard->Senses.Memory.bPlayerVisible);
	}

	// --- Range admission and the outer band -------------------------------------------------------
	{
		FSensesFixture F(/*VisionUnits=*/1000.f);
		if (F.Guard == nullptr || F.Player == nullptr)
		{
			return false;
		}
		// 0.8 x the effective radius: admitted, and inside the outer band.
		F.Player->Origin = FVector(Cm(800.f), 0.0, 0.0);
		F.Guard->Senses.TickSight(*F.Guard, 10.0);
		TestTrue(TEXT("a target inside the effective radius is admitted"),
			F.Guard->Senses.Memory.bPlayerInRange);
		TestTrue(TEXT("...and between 0.7x and 1.0x sets the outer band"),
			F.Guard->Senses.Memory.bPlayerInOuterBand);

		// Past the radius: rejected before cone or trace work.
		F.Player->Origin = FVector(Cm(1200.f), 0.0, 0.0);
		F.Services.Calls.Reset();
		F.Guard->Senses.TickSight(*F.Guard, 20.0);
		TestFalse(TEXT("a target past the effective radius is not admitted"),
			F.Guard->Senses.Memory.bPlayerInRange);
		TestFalse(TEXT("...and is not seen"), F.Guard->Senses.Memory.bPlayerVisible);
		TestFalse(TEXT("...without spending a trace on it"),
			F.Services.Saw(TEXT("QueryLineOfSight")));
	}

	// --- The damage override bypasses range only ---------------------------------------------------
	{
		FSensesFixture F(/*VisionUnits=*/100.f);
		if (F.Guard == nullptr || F.Player == nullptr)
		{
			return false;
		}
		F.Player->Origin = FVector(Cm(300.f), 0.0, 0.0);
		F.Guard->Senses.TickSight(*F.Guard, 10.0);
		TestFalse(TEXT("a target beyond ordinary range is not looked at"),
			F.Guard->Senses.Sighted().Contains(F.Player->Handle));
		F.Guard->Senses.Memory.StealthVisionOverrideUntil = 16.0;
		F.Guard->Senses.TickSight(*F.Guard, 11.0);
		TestTrue(TEXT("the live five-second override admits it through range"),
			F.Guard->Senses.Sighted().Contains(F.Player->Handle));
		F.Player->Origin = FVector(Cm(-300.f), 0.0, 0.0);
		F.Guard->Senses.TickSight(*F.Guard, 12.0);
		TestFalse(TEXT("the override does not bypass the 3-D cone"),
			F.Guard->Senses.Sighted().Contains(F.Player->Handle));
	}
	return true;
}


// `GatherEnemyConditions`: the ten-failure debounce, and the memory bit that makes the
// found/lost outputs edges rather than a per-think stream.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcSensesEnemyLosTest,
	"Elysium.Substrate.NpcSenses.EnemyLos", GElysiumTestFlags)
bool FElysiumNpcSensesEnemyLosTest::RunTest(const FString&)
{
	FSensesFixture F;
	if (!TestNotNull(TEXT("the guard leaf constructs"), F.Guard)
		|| !TestNotNull(TEXT("the player exists"), F.Player))
	{
		return false;
	}

	// Nothing has been committed, so the debounce is inert and no output has an occasion to fire.
	F.Guard->Senses.GatherEnemyLos(*F.Guard, 0.0);
	F.Flush(0.0);
	TestEqual(TEXT("no committed enemy means no found edge"), F.Counter(TEXT("c_foundenemy")), 0.f);

	// Cycle 5 owns the writer; a test injects the handle directly, which is the whole point of
	// keeping the debounce testable before enemy selection exists.
	F.Guard->Senses.Memory.Enemy = F.Player->Handle;

	// --- The first admitted LOS is one edge, and it fires both surfaces for the player ----------
	F.Guard->Senses.GatherEnemyLos(*F.Guard, 1.0);
	F.Flush(1.0);
	TestEqual(TEXT("the first admitted committed-enemy LOS fires OnFoundEnemy"),
		F.Counter(TEXT("c_foundenemy")), 1.f);
	TestEqual(TEXT("...and OnFoundPlayer, because the enemy IS the player"),
		F.Counter(TEXT("c_foundplayer")), 1.f);

	// The bit is retained: staying visible is not a stream of found edges.
	for (int32 i = 0; i < 5; ++i)
	{
		F.Guard->Senses.GatherEnemyLos(*F.Guard, 2.0 + i);
	}
	F.Flush(8.0);
	TestEqual(TEXT("continued sight does not re-fire the found edge"),
		F.Counter(TEXT("c_foundenemy")), 1.f);

	// --- Nine failures retain HAVE_ENEMY_LOS; the tenth is the loss edge -------------------------
	F.Services.bLineOfSightClear = false;
	for (int32 i = 0; i < ElysiumNpcSense::EnemyLosFailureLimit - 1; ++i)
	{
		F.Guard->Senses.GatherEnemyLos(*F.Guard, 10.0 + i);
	}
	F.Flush(20.0);
	TestEqual(TEXT("nine consecutive failures still count"),
		F.Guard->Senses.Memory.EnemyLosFailures, ElysiumNpcSense::EnemyLosFailureLimit - 1);
	TestFalse(TEXT("...but the enemy is not occluded yet"),
		F.Guard->Senses.Memory.bEnemyOccluded);
	TestEqual(TEXT("...and nothing has been lost"), F.Counter(TEXT("c_lostenemylos")), 0.f);

	F.Guard->Senses.GatherEnemyLos(*F.Guard, 30.0);
	F.Flush(30.0);
	TestTrue(TEXT("the tenth failure flips to ENEMY_OCCLUDED"),
		F.Guard->Senses.Memory.bEnemyOccluded);
	TestEqual(TEXT("...and fires OnLostEnemyLOS"), F.Counter(TEXT("c_lostenemylos")), 1.f);
	TestEqual(TEXT("...plus OnLostPlayerLOS for the player"),
		F.Counter(TEXT("c_lostplayerlos")), 1.f);

	// The eleventh failure is not a second edge.
	for (int32 i = 0; i < 5; ++i)
	{
		F.Guard->Senses.GatherEnemyLos(*F.Guard, 31.0 + i);
	}
	F.Flush(40.0);
	TestEqual(TEXT("further failures do not re-fire the loss edge"),
		F.Counter(TEXT("c_lostenemylos")), 1.f);
	TestTrue(TEXT("losing sight does NOT clear the enemy"),
		F.Guard->Senses.Memory.Enemy == F.Player->Handle);

	// --- Regaining sight is a new acquisition episode --------------------------------------------
	F.Services.bLineOfSightClear = true;
	F.Guard->Senses.GatherEnemyLos(*F.Guard, 50.0);
	F.Flush(50.0);
	TestEqual(TEXT("the debounce resets on a clear check"),
		F.Guard->Senses.Memory.EnemyLosFailures, 0);
	TestEqual(TEXT("...and a re-acquisition is a fresh found edge"),
		F.Counter(TEXT("c_foundenemy")), 2.f);

	// --- A non-player enemy fires only the enemy half --------------------------------------------
	{
		FSensesFixture G;
		if (G.Guard == nullptr)
		{
			return false;
		}
		FElysiumEntity* Other = G.World.FindByName(TEXT("c_hearworld"));   // any live non-player
		if (!TestNotNull(TEXT("a non-player entity exists to be the enemy"), Other))
		{
			return false;
		}
		G.Guard->Senses.Memory.Enemy = Other->Handle;
		G.Guard->Senses.GatherEnemyLos(*G.Guard, 1.0);
		G.Flush(1.0);
		TestEqual(TEXT("a non-player enemy fires OnFoundEnemy"),
			G.Counter(TEXT("c_foundenemy")), 1.f);
		TestEqual(TEXT("...and NOT OnFoundPlayer"), G.Counter(TEXT("c_foundplayer")), 0.f);
	}
	return true;
}


// Hearing: the bus cursor, radius x scalar admission, the occlusion policy, and the
// category -> output mapping.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcSensesHearingTest,
	"Elysium.Substrate.NpcSenses.Hearing", GElysiumTestFlags)
bool FElysiumNpcSensesHearingTest::RunTest(const FString&)
{
	const FElysiumSoundVolumeTable Volumes = MakeVolumeTable();
	FSensesFixture F(4000.f, 2.f);
	if (!F.Guard || !F.Player) return false;
	F.World.GameSounds().SetVolumeTable(&Volumes);
	F.Guard->Senses.StartSoundCursorAtHead(*F.Guard);
	FElysiumGameSoundRequest Sound;
	Sound.Category = FName(TEXT("DOOR_NORMAL"));
	Sound.TypeMask = ElysiumGameSounds::Player;
	Sound.Source = F.Player->Handle;
	Sound.Position = FVector(Cm(400.f), 0.0, F.Guard->EyePosition().Z);
	F.World.GameSounds().Emit(Sound, 1.0);
	F.Guard->Senses.TickHearing(*F.Guard, 1.0);
	F.Flush(1.0);
	TestEqual(TEXT("Listen queues the output, not just its condition"), F.Counter(TEXT("c_hearplayer")), 0.f);
	TestTrue(TEXT("the sound snapshot is immediate"), F.Guard->Senses.Memory.LastSoundPlayer.Serial != 0);
	TestTrue(TEXT("hearing never acquires enemy memory"), F.Guard->EnemyMemory.Num() == 0);
	F.Guard->Senses.TickHearing(*F.Guard, 1.199);
	TestFalse(TEXT("no player condition before minimum delay"), F.Guard->Senses.HeardConditions.Has(EElysiumNpcCond::HearPlayer));
	F.Guard->Senses.TickHearing(*F.Guard, 1.9);
	F.Flush(1.9);
	TestTrue(TEXT("player condition promoted by maximum delay"), F.Guard->Senses.HeardConditions.Has(EElysiumNpcCond::HearPlayer));
	TestEqual(TEXT("raw PLAYER publishes OnHearPlayer"), F.Counter(TEXT("c_hearplayer")), 1.f);
	F.Guard->Senses.TickHearing(*F.Guard, 2.0);
	F.Flush(2.0);
	TestEqual(TEXT("the serial cursor does not replay"), F.Counter(TEXT("c_hearplayer")), 1.f);
	TestTrue(TEXT("heard conditions last one Listen"), F.Guard->Senses.HeardConditions.IsEmpty());

	Sound.TypeMask = ElysiumGameSounds::World;
	Sound.Position.X = Cm(600.f);
	F.World.GameSounds().Emit(Sound, 3.0);
	F.Guard->Senses.TickHearing(*F.Guard, 3.0);
	F.Guard->Senses.TickHearing(*F.Guard, 3.9);
	F.Flush(3.9);
	TestEqual(TEXT("outside radius times hearing is rejected"), F.Counter(TEXT("c_hearworld")), 0.f);

	Sound.Position.X = Cm(100.f);
	F.Services.bLineOfSightClear = false;
	F.World.GameSounds().Emit(Sound, 4.0);
	F.Guard->Senses.TickHearing(*F.Guard, 4.0);
	F.Guard->Senses.TickHearing(*F.Guard, 4.9);
	F.Flush(4.9);
	TestEqual(TEXT("occlusion blocks an occludable world sound"), F.Counter(TEXT("c_hearworld")), 0.f);
	Sound.Category = FName(TEXT("PLAYER_GUNSHOT_BASE"));
	Sound.TypeMask = ElysiumGameSounds::Combat;
	F.World.GameSounds().Emit(Sound, 5.0);
	F.Guard->Senses.TickHearing(*F.Guard, 5.0);
	F.Guard->Senses.TickHearing(*F.Guard, 5.9);
	F.Flush(5.9);
	TestEqual(TEXT("nonoccludable combat reaches through that wall"), F.Counter(TEXT("c_hearcombat")), 1.f);
	F.Guard->Senses.CommitBestSound(F.Guard->Senses.HeardConditions);
	TestTrue(TEXT("commit uses this pass's promoted combat condition"), F.Guard->Senses.Memory.BestSound.TypeMask == ElysiumGameSounds::Combat);
	F.Services.bLineOfSightClear = true;
	Sound.Category = FName(TEXT("DOOR_NORMAL"));
	Sound.TypeMask = ElysiumGameSounds::Player;
	F.World.GameSounds().Emit(Sound, 6.0);
	F.Guard->Senses.TickHearing(*F.Guard, 6.0);
	F.Guard->Senses.TickHearing(*F.Guard, 6.9);
	F.Guard->Senses.CommitBestSound(F.Guard->Senses.HeardConditions);
	TestTrue(TEXT("an old combat snapshot does not outrank a newly heard player"), F.Guard->Senses.Memory.BestSound.TypeMask == ElysiumGameSounds::Player);
	Sound.Source = F.Guard->Handle;
	F.World.GameSounds().Emit(Sound, 7.0);
	F.Guard->Senses.TickHearing(*F.Guard, 7.0);
	F.Guard->Senses.TickHearing(*F.Guard, 7.9);
	TestTrue(TEXT("the observer ignores itself"), F.Guard->Senses.HeardConditions.IsEmpty());
	return true;
}

// The memory is what survives losing sight, so it is what a save has to carry.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcSensesMemorySaveTest,
	"Elysium.Substrate.NpcSenses.MemorySave", GElysiumTestFlags)
bool FElysiumNpcSensesMemorySaveTest::RunTest(const FString&)
{
	FSensesFixture F;
	if (!TestNotNull(TEXT("the guard leaf constructs"), F.Guard)
		|| !TestNotNull(TEXT("the player exists"), F.Player))
	{
		return false;
	}

	FElysiumNpcMemory& Mem = F.Guard->Senses.Memory;
	Mem.Enemy = F.Player->Handle;
	Mem.LastEnemy = F.Player->Handle;
	Mem.LastSeen[static_cast<int32>(FElysiumNpcMemory::ESeen::Hate)] = F.Player->Handle;
	Mem.LastSeenTime[static_cast<int32>(FElysiumNpcMemory::ESeen::Hate)] = 12.5;
	Mem.LastHeardSource = F.Player->Handle;
	Mem.LastHeardPosition = FVector(11.0, 22.0, 33.0);
	Mem.LastHeardCategory = TEXT("PLAYER_GUNSHOT_BASE");
	Mem.LastHeardTime = 7.25;
	Mem.LastDamageAttacker = F.Player->Handle;
	Mem.LastDamageTime = 3.5;
	Mem.LastDamageAmount = 9;
	Mem.EnemyLosFailures = 4;
	Mem.bEnemyLosLatched = true;
	Mem.PlayerLosLastClearTime = 6.0;

	TArray<uint8> Payload;
	{
		FMemoryWriter Writer(Payload, /*bIsPersistent*/ true);
		FElysiumSaveArchive Ar(Writer, FElysiumSaveVersion::Latest);
		F.Guard->Serialize(Ar);
	}

	// A second world of the same shape, restored from that payload.
	FSensesFixture G;
	if (G.Guard == nullptr || G.Player == nullptr)
	{
		return false;
	}
	{
		FMemoryReader Reader(Payload, /*bIsPersistent*/ true);
		FElysiumSaveArchive Ar(Reader, FElysiumSaveVersion::Latest);
		G.Guard->Serialize(Ar);
	}

	const FElysiumNpcMemory& Restored = G.Guard->Senses.Memory;
	TestTrue(TEXT("the committed enemy survives the round trip"),
		Restored.Enemy == G.Player->Handle);
	TestTrue(TEXT("...and the last enemy beside it"), Restored.LastEnemy == G.Player->Handle);
	TestTrue(TEXT("the last-seen hate slot survives"),
		Restored.Seen(FElysiumNpcMemory::ESeen::Hate) == G.Player->Handle);
	TestTrue(TEXT("...with its timestamp"), NearlyEqual(
		static_cast<float>(Restored.LastSeenTime[static_cast<int32>(FElysiumNpcMemory::ESeen::Hate)]),
		12.5f));
	TestEqual(TEXT("the last-heard category survives"), Restored.LastHeardCategory,
		FString(TEXT("PLAYER_GUNSHOT_BASE")));
	TestTrue(TEXT("...with its position"),
		Restored.LastHeardPosition.Equals(FVector(11.0, 22.0, 33.0)));
	TestEqual(TEXT("the last damage amount survives"), Restored.LastDamageAmount, 9);
	TestTrue(TEXT("...and its attacker"), Restored.LastDamageAttacker == G.Player->Handle);
	TestEqual(TEXT("the debounce counter survives"), Restored.EnemyLosFailures, 4);
	TestTrue(TEXT("...and the edge latch, so a restore does not re-fire OnFoundEnemy"),
		Restored.bEnemyLosLatched);

	// A payload written before the senses block restores without them rather than half-read.
	{
		TArray<uint8> Legacy;
		{
			FMemoryWriter Writer(Legacy, /*bIsPersistent*/ true);
			FElysiumSaveArchive Ar(Writer, FElysiumSaveVersion::Activation);
			F.Guard->Serialize(Ar);
		}
		FSensesFixture H;
		if (H.Guard == nullptr)
		{
			return false;
		}
		FMemoryReader Reader(Legacy, /*bIsPersistent*/ true);
		FElysiumSaveArchive Ar(Reader, FElysiumSaveVersion::Activation);
		H.Guard->Serialize(Ar);
		TestFalse(TEXT("a pre-senses payload restores an NPC with no remembered enemy"),
			H.Guard->Senses.Memory.Enemy.IsSet());
	}
	return true;
}


// Where the senses do NOT run: a scripted owner suppresses condition gathering, and an
// inert or dead body has no senses at all.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcSensesSuppressionTest,
	"Elysium.Substrate.NpcSenses.Suppression", GElysiumTestFlags)
bool FElysiumNpcSensesSuppressionTest::RunTest(const FString&)
{
	const FElysiumSoundVolumeTable Volumes = MakeVolumeTable();

	// --- A scripted owner suppresses the whole pass, driven through the real Think ---------------
	{
		FSensesFixture F;
		if (!TestNotNull(TEXT("the guard leaf constructs"), F.Guard))
		{
			return false;
		}
		F.World.GameSounds().SetVolumeTable(&Volumes);
		F.Guard->Senses.StartSoundCursorAtHead(*F.Guard);

		// Admission first: the ordinary think path has to be past its barrier for this to be a
		// statement about suppression rather than about admission.
		FElysiumNpcWorldFixture::Wake({ F.Guard }, 1.0);
		F.World.Tick(1.0);
		FElysiumNpcWorldFixture::Wake({ F.Guard }, 2.0);
		F.World.Tick(2.0);

		// The sound must be newer than the last Listen, whose timestamp is exactly 2.0.
		F.Flush(2.1);
		F.World.EmitGameSound(FVector(Cm(50.f), 0.0, 0.0), FName(TEXT("PLAYER_GUNSHOT_BASE")),
			0.f, FElysiumEntityHandle::Invalid(), 0.f, ElysiumGameSounds::Combat);
		F.Guard->ScriptOwner = F.Guard->Handle;   // a beat owns the body
		FElysiumNpcWorldFixture::Wake({ F.Guard }, 3.0);
		F.World.Tick(3.0);
		F.World.Tick(3.1);
		TestEqual(TEXT("a script-owned body gathers no conditions"),
			F.Counter(TEXT("c_hearcombat")), 0.f);

		// Handing the body back resumes it: the stimulus is still inside the retention window.
		F.Guard->ScriptOwner = FElysiumEntityHandle::Invalid();
		FElysiumNpcWorldFixture::Wake({ F.Guard }, 4.0);
		F.World.Tick(4.0);
		F.World.Tick(4.1);
		FElysiumNpcWorldFixture::Wake({ F.Guard }, 4.91);
		F.World.Tick(4.91);
		F.World.Tick(4.92);
		TestEqual(TEXT("...and hearing resumes once the beat releases it"),
			F.Counter(TEXT("c_hearcombat")), 1.f);
	}

	// --- A hidden or dead body has no senses ------------------------------------------------------
	{
		FSensesFixture F;
		if (F.Guard == nullptr || F.Player == nullptr)
		{
			return false;
		}
		F.Player->Origin = FVector(Cm(100.f), 0.0, 0.0);
		F.Guard->bHidden = true;
		F.Guard->Senses.Tick(*F.Guard, 10.0);
		TestFalse(TEXT("a hidden body does not see"), F.Guard->Senses.Memory.bPlayerVisible);

		F.Guard->bHidden = false;
		F.Guard->bDead = true;
		F.Guard->Senses.Tick(*F.Guard, 20.0);
		TestFalse(TEXT("a dead body does not see"), F.Guard->Senses.Memory.bPlayerVisible);

		// Alive and unhidden, the same pass does see — so the two above are the gate, not the setup.
		F.Guard->bDead = false;
		F.Guard->Senses.Tick(*F.Guard, 30.0);
		TestTrue(TEXT("...and a live one does"), F.Guard->Senses.Memory.bPlayerVisible);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcSensesAdmissionTest,
	"Elysium.Substrate.NpcSenses.AdmissionDetails", GElysiumTestFlags)
bool FElysiumNpcSensesAdmissionTest::RunTest(const FString&)
{
	FSensesFixture F(540.f);
	if (!F.Guard || !F.Player) return false;
	F.Player->Origin = FVector(Cm(100.f), 0, 0);
	F.Guard->Relationships.SetEntity(F.Player->Handle, EElysiumRelationship::Hate, 11);
	F.Guard->Senses.TickSight(*F.Guard, 1.0);
	FElysiumNpcConditions Conditions;
	ElysiumNpcCond::GatherSight(*F.Guard, 1.0, Conditions);
	TestTrue(TEXT("priority 11 is NEMESIS"), Conditions.Has(EElysiumNpcCond::SeeNemesis));
	TestTrue(TEXT("SEE_PLAYER uses its actual registered identity"), Conditions.Has(EElysiumNpcCond::SeePlayer));
	TestTrue(TEXT("NEMESIS still writes the D_HT enemy memory"), F.Guard->EnemyMemory.Find(F.Player->Handle) != nullptr);
	F.Player->Origin.X = Cm(2000.f);
	F.Guard->Senses.TickSight(*F.Guard, 1.1);
	TestTrue(TEXT("player Look retains the preceding list until 0.15 s"), F.Guard->Senses.Sighted().Contains(F.Player->Handle));
	F.Guard->Senses.TickSight(*F.Guard, 1.151);
	TestFalse(TEXT("the next player Look rejects beyond its authored radius"), F.Guard->Senses.Sighted().Contains(F.Player->Handle));
	F.Player->Origin.X = Cm(100.f);
	F.Player->Sheet.SetBase(EElysiumTraitContainer::ActiveDisciplines, 8, 1);
	F.Player->Sheet.RecomputeCurrent(nullptr);
	F.Player->Disciplines.bObfuscateCloaked = true;
	F.Guard->Senses.TickSight(*F.Guard, 2.0);
	TestFalse(TEXT("an active cloak is hidden without observer detection"), F.Guard->Senses.Sighted().Contains(F.Player->Handle));
	F.Guard->Disciplines.bObfuscateDetectionReady = true;
	F.Guard->Disciplines.ObfuscateDetectionRadiusUnits = 100.f;
	F.Guard->Senses.TickSight(*F.Guard, 3.0);
	TestTrue(TEXT("the recovered observer radius includes equality"), F.Guard->Senses.Sighted().Contains(F.Player->Handle));
	FElysiumActiveDisciplineEffect BrainWipe;
	BrainWipe.Record = TEXT("Dominate_BrainWipe");
	F.Guard->Disciplines.TargetEffects.Add(BrainWipe);
	F.Guard->Senses.TickSight(*F.Guard, 4.0);
	TestFalse(TEXT("BrainWipe belongs to the observer"), F.Guard->Senses.Sighted().Contains(F.Player->Handle));
	F.Guard->Disciplines.TargetEffects.Reset();
	F.Player->Disciplines.TargetEffects.Add(BrainWipe);
	F.Guard->Senses.TickSight(*F.Guard, 5.0);
	TestTrue(TEXT("a brain-wiped target is still visible"), F.Guard->Senses.Sighted().Contains(F.Player->Handle));
	F.Guard->Senses.ViewConeBodyOffsetCm = Cm(1000.f);
	TestFalse(TEXT("the front-plane test runs before the apex shift"),
		FElysiumNpcSenses::IsInViewCone(*F.Guard, F.Guard->EyePosition() + FVector(-1, 0, 0)));
	TestTrue(TEXT("the pulled-back apex admits a front-side target"),
		FElysiumNpcSenses::IsInViewCone(*F.Guard, F.Guard->EyePosition() + FVector(1, 100, 0)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcSensesTroikaConeTest,
	"Elysium.Substrate.NpcSenses.TroikaCone", GElysiumTestFlags)
bool FElysiumNpcSensesTroikaConeTest::RunTest(const FString&)
{
	IConsoleVariable* IgnorePlayerCVar =
		IConsoleManager::Get().FindConsoleVariable(TEXT("npc_ignore_player"));
	IConsoleVariable* IgnoreSensesCVar =
		IConsoleManager::Get().FindConsoleVariable(TEXT("npc_ignore_senses"));
	if (!TestNotNull(TEXT("npc_ignore_player is registered"), IgnorePlayerCVar)
		|| !TestNotNull(TEXT("npc_ignore_senses is registered"), IgnoreSensesCVar))
	{
		return false;
	}
	const int32 WasIgnorePlayer = IgnorePlayerCVar->GetInt();
	const int32 WasIgnoreSenses = IgnoreSensesCVar->GetInt();
	ON_SCOPE_EXIT
	{
		IgnorePlayerCVar->Set(WasIgnorePlayer, ECVF_SetByCode);
		IgnoreSensesCVar->Set(WasIgnoreSenses, ECVF_SetByCode);
	};
	IgnorePlayerCVar->Set(0, ECVF_SetByCode);
	IgnoreSensesCVar->Set(0, ECVF_SetByCode);

	FSensesFixture F;
	if (!TestNotNull(TEXT("the guard leaf constructs"), F.Guard)
		|| !TestNotNull(TEXT("the player exists"), F.Player))
	{
		return false;
	}
	FElysiumEntity* Other = F.World.FindByName(TEXT("c_hearworld"));
	if (!TestNotNull(TEXT("a non-player entity exists"), Other))
	{
		return false;
	}
	F.Player->Origin = FVector(Cm(100.f), 0.0, 0.0);
	Other->Origin = FVector(Cm(100.f), 0.0, 0.0);
	F.Guard->Relationships.SetEntity(F.Player->Handle, EElysiumRelationship::Hate, 5);
	F.Guard->Relationships.SetEntity(Other->Handle, EElysiumRelationship::Hate, 5);

	TestTrue(TEXT("default off: a player ahead is in the entity cone"),
		FElysiumNpcSenses::IsInViewCone(*F.Guard, *F.Player));
	TestTrue(TEXT("default off: a hated non-player ahead is in the entity cone"),
		FElysiumNpcSenses::IsInViewCone(*F.Guard, *Other));
	F.Guard->Senses.TickSight(*F.Guard, 1.0);
	TestTrue(TEXT("default off: Look admits the player"),
		F.Guard->Senses.Sighted().Contains(F.Player->Handle));

	IgnorePlayerCVar->Set(1, ECVF_SetByCode);
	TestFalse(TEXT("npc_ignore_player rejects the player entity cone"),
		FElysiumNpcSenses::IsInViewCone(*F.Guard, *F.Player));
	TestTrue(TEXT("...and still uses the base cone on a hated non-player"),
		FElysiumNpcSenses::IsInViewCone(*F.Guard, *Other));
	TestTrue(TEXT("the point body is not the Troika override"),
		FElysiumNpcSenses::IsInViewCone(*F.Guard, F.Player->EyePosition()));
	F.Guard->Senses.Memory.PlayerLosNextUpdateTime = -1.0;
	F.Guard->Senses.TickSight(*F.Guard, 2.0);
	TestFalse(TEXT("Look does not admit the ignored player"),
		F.Guard->Senses.Sighted().Contains(F.Player->Handle));
	TestTrue(TEXT("...and still admits a hated non-player ahead"),
		F.Guard->Senses.Sighted().Contains(Other->Handle));
	TestTrue(TEXT("the closest-player cache still fills bPlayerInCone from the base body"),
		F.Guard->Senses.Memory.bPlayerInCone);
	TestFalse(TEXT("IsVisible also refuses the ignored player"),
		FElysiumNpcSenses::IsVisible(*F.Guard, *F.Player, 2.0));

	F.Player->Origin = FVector(Cm(-100.f), 0.0, 0.0);
	TestFalse(TEXT("arm 4 does not accept a player behind the observer"),
		FElysiumNpcSenses::IsInViewCone(*F.Guard, *F.Player));
	F.Player->Origin = FVector(Cm(100.f), 0.0, 0.0);

	const FElysiumSoundVolumeTable Volumes = MakeVolumeTable();
	F.World.GameSounds().SetVolumeTable(&Volumes);
	F.Guard->Senses.StartSoundCursorAtHead(*F.Guard);
	FElysiumGameSoundRequest Sound;
	Sound.Category = FName(TEXT("DOOR_NORMAL"));
	Sound.TypeMask = ElysiumGameSounds::Player;
	Sound.Source = F.Player->Handle;
	Sound.Position = FVector(Cm(50.f), 0.0, F.Guard->EyePosition().Z);
	F.World.GameSounds().Emit(Sound, 3.0);
	F.Guard->Senses.TickHearing(*F.Guard, 3.0);
	TestTrue(TEXT("npc_ignore_player does not snapshot a player sound"),
		F.Guard->Senses.Memory.LastSoundPlayer.Serial == 0);
	Sound.TypeMask = ElysiumGameSounds::World;
	Sound.Source = FElysiumEntityHandle::Invalid();
	F.World.GameSounds().Emit(Sound, 3.1);
	F.Guard->Senses.TickHearing(*F.Guard, 3.1);
	TestTrue(TEXT("...and still snapshots a non-player sound"),
		F.Guard->Senses.Memory.LastSoundWorld.Serial != 0);

	IgnorePlayerCVar->Set(0, ECVF_SetByCode);
	IgnoreSensesCVar->Set(1, ECVF_SetByCode);
	TestFalse(TEXT("npc_ignore_senses rejects the player entity cone"),
		FElysiumNpcSenses::IsInViewCone(*F.Guard, *F.Player));
	TestFalse(TEXT("...and a hated non-player"),
		FElysiumNpcSenses::IsInViewCone(*F.Guard, *Other));
	TestTrue(TEXT("the point body still answers the base cone"),
		FElysiumNpcSenses::IsInViewCone(*F.Guard, F.Player->EyePosition()));
	TestFalse(TEXT("IsVisible refuses everyone"),
		FElysiumNpcSenses::IsVisible(*F.Guard, *F.Player, 4.0)
		|| FElysiumNpcSenses::IsVisible(*F.Guard, *Other, 4.0));
	F.Guard->Senses.TickSight(*F.Guard, 4.0);
	TestFalse(TEXT("Look admits nobody"),
		F.Guard->Senses.Sighted().Contains(F.Player->Handle)
		|| F.Guard->Senses.Sighted().Contains(Other->Handle));
	F.Guard->Senses.Memory.LastSoundPlayer = FElysiumGameSoundEvent();
	Sound.TypeMask = ElysiumGameSounds::Player;
	Sound.Source = F.Player->Handle;
	F.World.GameSounds().Emit(Sound, 4.1);
	F.Guard->Senses.TickHearing(*F.Guard, 4.1);
	TestTrue(TEXT("npc_ignore_senses does not snapshot a player sound"),
		F.Guard->Senses.Memory.LastSoundPlayer.Serial == 0);

	IgnoreSensesCVar->Set(0, ECVF_SetByCode);
	TestTrue(TEXT("restoring the switches returns the entity cone"),
		FElysiumNpcSenses::IsInViewCone(*F.Guard, *F.Player));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcSensesDeafRulesTest,
	"Elysium.Substrate.NpcSenses.DeafZoneRules", GElysiumTestFlags)
bool FElysiumNpcSensesDeafRulesTest::RunTest(const FString&)
{
	const auto Root = ElysiumKeyValues::ParseText(TEXT("StealthKillRules { DeafZoneArc { 1 60 2 80 } StatInfo { StealthKillDistMax 95 HearingScalarMax 2.5 } }"));
	FElysiumStealthKillRules Rules;
	FString Error;
	if (!Root || !TestTrue(TEXT("the authored rule shape parses"), Rules.Parse(*Root, Error))) return false;
	TestEqual(TEXT("authored reach overrides the 70-unit fallback"), Rules.DistanceMaxUnits, 95.f);
	TestEqual(TEXT("a short degree table extends its last entry"), Rules.DeafArcDegrees[19], 80.f);
	TestEqual(TEXT("maximum sneaking and zero hearing give zero depth"), Rules.MinDepthUnits(10, 0), 0.f);
	TestEqual(TEXT("minimum sneaking and maximum hearing give twice the reach"), Rules.MinDepthUnits(1, 2.5f), 190.f);
	FSensesFixture F;
	if (!F.Guard || !F.Player) return false;
	F.Player->Origin = FVector(-Cm(500.f), 0, 0);
	F.Services.bPlayerDucking = true;
	TestEqual(TEXT("an invalid combat feat closes the arc"), Rules.ArcDot(0), 1.f);
	TestEqual(TEXT("an out-of-table combat feat is not clamped"), Rules.ArcDot(21), 1.f);
	TestFalse(TEXT("an unequipped player has no rear deaf arc"), Rules.InDeafZone(*F.Player, *F.Guard));
	F.Player->Origin.X = -0.01;
	TestFalse(TEXT("inside minimum depth the victim hears the approach"), Rules.InDeafZone(*F.Player, *F.Guard));
	F.Player->Origin.X = Cm(500.f);
	TestFalse(TEXT("a front approach has no deaf zone"), Rules.InDeafZone(*F.Player, *F.Guard));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcSensesCombatArcTest,
	"Elysium.Substrate.NpcSenses.CombatArc", GElysiumTestFlags)
bool FElysiumNpcSensesCombatArcTest::RunTest(const FString&)
{
	FElysiumFeatTable Feats;
	for (int32 Index = 0; Index <= 10; ++Index)
	{
		FElysiumFeat Feat;
		Feat.InternalName = FString::Printf(TEXT("sense_fixture_%d"), Index);
		Feat.MaxValue = 20;
		if (Index == 1) { Feat.InternalName = TEXT("Sneaking"); Feat.Bases.Add(FElysiumTraitRef::Parse(TEXT("Dexterity"))); }
		if (Index == 9) { Feat.InternalName = TEXT("Close_Combat_Brawl"); Feat.Bases.Add(FElysiumTraitRef::Parse(TEXT("Strength"))); }
		if (Index == 10) { Feat.InternalName = TEXT("Close_Combat_Melee"); Feat.Bases.Add(FElysiumTraitRef::Parse(TEXT("Dexterity"))); }
		Feats.Feats.Add(Feat);
	}
	Feats.Reindex();
	const auto Previous = ElysiumSheetRules::BoundTables();
	auto Bound = Previous;
	Bound.Feats = &Feats;
	ElysiumSheetRules::BindTables(Bound);
	ON_SCOPE_EXIT { ElysiumSheetRules::BindTables(Previous); };
	FElysiumItemTable Items;
	for (const TCHAR* Name : { TEXT("item_w_sense_arc_brawl"), TEXT("item_w_sense_arc_melee") })
	{
		FElysiumItemDef Item;
		Item.Classname = Name;
		Item.Type = EElysiumItemType::WeaponMelee;
		FElysiumWeaponMode Mode;
		Mode.Tag = TEXT("Primary");
		Mode.Type = EElysiumWeaponModeType::Attack;
		Mode.Dmg = FString(Name).EndsWith(TEXT("melee"))
			? TEXT("2 Lethal Close_Combat_Melee DMG_SLASH") : TEXT("2 Bashing Close_Combat_Brawl DMG_FIST");
		Item.Modes.Add(Mode);
		Items.Items.Add(Item);
	}
	Items.Reindex();
	ElysiumItems::Install(Items);
	ON_SCOPE_EXIT { ElysiumItems::Uninstall(Items); };
	FSensesFixture F;
	if (!F.Guard || !F.Player) return false;
	F.Player->Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::Strength, 10);
	F.Player->Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::Dexterity, 1);
	F.Player->Sheet.RecomputeCurrent(nullptr);
	FElysiumStealthKillRules Rules;
	Rules.DeafArcDegrees[0] = 60.f;
	for (int32 Index = 1; Index < 20; ++Index) Rules.DeafArcDegrees[Index] = 80.f;
	const float Angle = FMath::DegreesToRadians(35.f);
	F.Player->Origin = FVector(-FMath::Cos(Angle) * Cm(500.f), FMath::Sin(Angle) * Cm(500.f), 0);
	F.Services.bPlayerDucking = true;
	for (const TCHAR* Name : { TEXT("item_w_sense_arc_brawl"), TEXT("item_w_sense_arc_melee") })
	{
		const auto Handle = F.Player->Inventory.GiveNamedItem(*F.Player, Name);
		FElysiumEntity* Entity = F.World.Resolve(Handle);
		FElysiumItem* Item = Entity ? Entity->AsItem() : nullptr;
		if (!TestNotNull(TEXT("the active-weapon fixture was granted"), Item)) return false;
		TestTrue(TEXT("the weapon becomes active"), F.Player->Inventory.SetActiveWeapon(*F.Player, *Item));
		const bool Brawl = FString(Name).EndsWith(TEXT("brawl"));
		TestEqual(TEXT("arc follows active combat feat, not unchanged Sneaking"), Rules.InDeafArc(*F.Player, *F.Guard), Brawl);
		TestEqual(TEXT("rear hearing uses the same active-weapon arc"), Rules.InDeafZone(*F.Player, *F.Guard), Brawl);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcSensesUnknownAuthorityTest,
	"Elysium.Substrate.NpcSenses.UnknownAuthority", GElysiumTestFlags)
bool FElysiumNpcSensesUnknownAuthorityTest::RunTest(const FString&)
{
	FSensesFixture F(540.f);
	if (!F.Guard || !F.Player) return false;
	F.Guard->InvestigateMode = 4; // thug_1's authored hated-target investigation policy
	F.Services.bPlayerDucking = true;
	F.Player->Origin = FVector(Cm(500.f), 0, 0);
	F.Guard->Relationships.SetEntity(F.Player->Handle, EElysiumRelationship::Hate, 5);
	F.Guard->Senses.TickSight(*F.Guard, 1.0);
	FElysiumNpcConditions Conditions;
	ElysiumNpcCond::GatherSight(*F.Guard, 1.0, Conditions);
	TestTrue(TEXT("outer-band approach is unknown"), Conditions.Has(EElysiumNpcCond::SeeUnknown));
	TestFalse(TEXT("unknown is skipped before the hated observation"), Conditions.Has(EElysiumNpcCond::SeeHate));
	TestFalse(TEXT("unknown creates no enemy record"), F.Guard->EnemyMemory.Find(F.Player->Handle) != nullptr);
	TestEqual(TEXT("a new unknown increments the retail sighting counter"), F.Guard->EnemySightings, 1);
	F.Player->Observer.bDetected = true; // a stale/different nearest-observer presentation sample
	F.Guard->Senses.TickSight(*F.Guard, 1.2);
	Conditions.Reset();
	ElysiumNpcCond::GatherSight(*F.Guard, 1.2, Conditions);
	TestTrue(TEXT("HUD detection cannot turn unknown contact into acquisition"), Conditions.Has(EElysiumNpcCond::SeeUnknown));
	TestFalse(TEXT("presentation cannot author hostile-observation history"), F.Player->LastHostileAssessment.IsSet());
	TestEqual(TEXT("the same unknown is not a repeat encounter every look"), F.Guard->EnemySightings, 1);
	F.Player->Origin.X = Cm(200.f);
	F.Guard->Senses.TickSight(*F.Guard, 2.0);
	Conditions.Reset();
	ElysiumNpcCond::GatherSight(*F.Guard, 2.0, Conditions);
	TestTrue(TEXT("leaving the band admits real hate observation"), Conditions.Has(EElysiumNpcCond::SeeHate));
	TestTrue(TEXT("real admitted observation supplies the one-second predicate"), F.Player->WasRecentlyObservedByHostile(2.999));
	TestFalse(TEXT("exactly one second later the predicate expires"), F.Player->WasRecentlyObservedByHostile(3.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcSensesOverrideDebounceTest,
	"Elysium.Substrate.NpcSenses.OverrideAndDebounce", GElysiumTestFlags)
bool FElysiumNpcSensesOverrideDebounceTest::RunTest(const FString&)
{
	FSensesFixture F(540.f);
	if (!F.Guard || !F.Player) return false;
	F.Player->Origin = FVector(Cm(800.f), 0, 0);
	FElysiumGameSoundRequest Shot;
	Shot.Category = ElysiumGameSounds::Gunshot();
	Shot.TypeMask = ElysiumGameSounds::Combat;
	Shot.RadiusCm = Cm(1200.f);
	Shot.Position = F.Player->EyePosition();
	Shot.Source = F.Player->Handle;
	Shot.DurationSeconds = .2;
	F.World.GameSounds().Emit(Shot, 10.0);
	F.Guard->Senses.TickHearing(*F.Guard, 10.0);
	F.Guard->Senses.TickHearing(*F.Guard, 10.91);
	F.Guard->Senses.TickSight(*F.Guard, 11.0);
	TestTrue(TEXT("hearing with no committed enemy opens one second of range override"),
		F.Guard->Senses.Sighted().Contains(F.Player->Handle));
	F.Guard->Senses.TickSight(*F.Guard, 11.91);
	TestFalse(TEXT("override expires at equality"), F.Guard->Senses.Sighted().Contains(F.Player->Handle));
	F.Player->Origin.X = Cm(100.f);
	F.Guard->Senses.Memory.Enemy = F.Player->Handle;
	F.Guard->Senses.GatherEnemyLos(*F.Guard, 12.0);
	FElysiumActiveDisciplineEffect BrainWipe;
	BrainWipe.Record = TEXT("Dominate_BrainWipe");
	F.Guard->Disciplines.TargetEffects.Add(BrainWipe);
	for (int32 Count = 0; Count < 9; ++Count) F.Guard->Senses.GatherEnemyLos(*F.Guard, 13.0 + Count * .1);
	TestFalse(TEXT("BrainWipe retains LOS below ten failed samples"), F.Guard->Senses.Memory.bEnemyOccluded);
	F.Guard->Senses.GatherEnemyLos(*F.Guard, 14.0);
	TestTrue(TEXT("the tenth BrainWipe sample becomes occluded despite clear geometry"), F.Guard->Senses.Memory.bEnemyOccluded);
	F.Guard->Disciplines.TargetEffects.Reset();
	F.Guard->Senses.GatherEnemyLos(*F.Guard, 14.1);
	TestFalse(TEXT("removing the effect restores actual visibility"), F.Guard->Senses.Memory.bEnemyOccluded);
	return true;
}

}   // namespace ElysiumNpcSensesTests

#endif // WITH_DEV_AUTOMATION_TESTS
