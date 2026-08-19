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
#include "ElysiumSaveArchive.h"
#include "ElysiumSaveTypes.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumSoundVolumeTable.h"
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
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World;
		FElysiumNpc* Guard = nullptr;
		FElysiumPlayer* Player = nullptr;

		explicit FSensesFixture(float VisionUnits = 4000.f, float HearingScalar = 1.0f)
			: World(nullptr, nullptr, Services.Bundle())
		{
			ElysiumRng::SeedAll(0x53454E53);

			FElysiumEntityDefs Defs;
			Defs.MapName = TEXT("__npcsenses_test__");

			FElysiumEntityDef GuardDef;
			GuardDef.Classname = TEXT("npc_VHumanCombatant");
			GuardDef.TargetName = TEXT("guard");
			GuardDef.Origin = FVector::ZeroVector;
			GuardDef.Keys.Add(TEXT("vision"), FString::SanitizeFloat(VisionUnits));
			GuardDef.Keys.Add(TEXT("hearing"), FString::SanitizeFloat(HearingScalar));
			auto Wire = [&GuardDef](const TCHAR* Output, const TCHAR* Counter)
			{
				FElysiumOutputDef Row;
				Row.Name = Output;
				Row.Target = Counter;
				Row.Input = TEXT("Add");
				Row.Param = TEXT("1");
				GuardDef.Outputs.Add(MoveTemp(Row));
			};
			Wire(TEXT("OnFoundEnemy"),    TEXT("c_foundenemy"));
			Wire(TEXT("OnFoundPlayer"),   TEXT("c_foundplayer"));
			Wire(TEXT("OnLostEnemyLOS"),  TEXT("c_lostenemylos"));
			Wire(TEXT("OnLostPlayerLOS"), TEXT("c_lostplayerlos"));
			Wire(TEXT("OnHearCombat"),    TEXT("c_hearcombat"));
			Wire(TEXT("OnHearPlayer"),    TEXT("c_hearplayer"));
			Wire(TEXT("OnHearWorld"),     TEXT("c_hearworld"));
			Defs.Defs.Add(MoveTemp(GuardDef));

			for (const TCHAR* Name : { TEXT("c_foundenemy"), TEXT("c_foundplayer"),
				TEXT("c_lostenemylos"), TEXT("c_lostplayerlos"), TEXT("c_hearcombat"),
				TEXT("c_hearplayer"), TEXT("c_hearworld") })
			{
				FElysiumEntityDef Counter;
				Counter.Classname = TEXT("math_counter");
				Counter.TargetName = Name;
				Defs.Defs.Add(MoveTemp(Counter));
			}

			World.Load(MoveTemp(Defs));
			World.SpawnPlayer();
			World.Activate(0.0);
			World.Tick(0.0);

			Guard = static_cast<FElysiumNpc*>(World.FindByName(TEXT("guard")));
			Player = World.FindPlayer();
		}

		// Let the queue deliver without letting the guard's own think re-run the senses: every
		// case here drives the pass it is asserting explicitly.
		void Flush(double Now)
		{
			if (Guard)
			{
				Guard->NextThink = ELYSIUM_NEVER_THINK;
			}
			World.Tick(Now);
		}

		float Counter(const TCHAR* Name)
		{
			const FElysiumEntity* Ent = World.FindByName(Name);
			if (Ent == nullptr)
			{
				return -1.f;
			}
			TArray<TPair<FString, FString>> Rows;
			Ent->GetDebugState(Rows);
			for (const TPair<FString, FString>& Row : Rows)
			{
				if (Row.Key == TEXT("Value"))
				{
					return FCString::Atof(*Row.Value);
				}
			}
			return -1.f;
		}
	};
}

// =====================================================================================
// `InitPerceptionDistances`: the `-1.0` sentinel, the two inspection tables, and what an
// unreachable table falls back to.
// =====================================================================================

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

// =====================================================================================
// `SetClosestPlayer` + `SetPlayerLOS`: the 2 s cadence, the 512-unit no-trace bypass, the
// far trace, and the eight-second in-cone grace.
// =====================================================================================

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
		F.Guard->Senses.TickSight(*F.Guard, 10.0);

		TestTrue(TEXT("an in-cone target inside 512 units is seen"), F.Guard->Senses.Memory.bPlayerLos);
		TestFalse(TEXT("...without asking the engine for a trace"),
			F.Services.Saw(TEXT("QueryLineOfSight")));
		TestTrue(TEXT("the closest-player cache carries the handle"),
			F.Guard->Senses.Memory.ClosestPlayer == F.Player->Handle);
		TestTrue(TEXT("...and the distance"),
			NearlyEqual(F.Guard->Senses.Memory.ClosestPlayerDistanceCm, Cm(100.f), 1.0f));
		TestTrue(TEXT("the target is in range"), F.Guard->Senses.Memory.bPlayerInRange);
		TestFalse(TEXT("...and well inside the outer band"),
			F.Guard->Senses.Memory.bPlayerInOuterBand);

		// The cadence: moving the player and re-ticking inside 2 s does NOT recompute.
		F.Player->Origin = FVector(Cm(-100.f), 0.0, 0.0);   // directly behind the guard
		F.Guard->Senses.TickSight(*F.Guard, 11.0);
		TestTrue(TEXT("a tick inside the 2 s cadence leaves the cache alone"),
			NearlyEqual(F.Guard->Senses.Memory.ClosestPlayerDistanceCm, Cm(100.f), 1.0f)
			&& F.Guard->Senses.Memory.bPlayerLos);

		// Past the cadence it recomputes, and a target behind the observer is out of cone.
		F.Guard->Senses.TickSight(*F.Guard, 12.5);
		TestFalse(TEXT("a target behind the observer is out of cone"),
			F.Guard->Senses.Memory.bPlayerInCone);
		TestFalse(TEXT("...and therefore not seen"), F.Guard->Senses.Memory.bPlayerLos);
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
		TestTrue(TEXT("a clear far segment is sight"), F.Guard->Senses.Memory.bPlayerLos);
		TestTrue(TEXT("...and stamps the last-clear time"),
			NearlyEqual(static_cast<float>(F.Guard->Senses.Memory.PlayerLosLastClearTime), 100.0f));

		// A wall goes up. The player is still in cone, so sight is preserved for eight seconds.
		F.Services.bLineOfSightClear = false;
		F.Guard->Senses.TickSight(*F.Guard, 104.0);
		TestTrue(TEXT("blocked but in cone: sight is held inside the 8 s grace"),
			F.Guard->Senses.Memory.bPlayerLos);
		F.Guard->Senses.TickSight(*F.Guard, 108.0);
		TestTrue(TEXT("...right up to the edge of it"), F.Guard->Senses.Memory.bPlayerLos);
		F.Guard->Senses.TickSight(*F.Guard, 110.0);
		TestFalse(TEXT("...and drops once the grace has run out"),
			F.Guard->Senses.Memory.bPlayerLos);

		// The wall comes down: an ordinary clear trace restores sight and the clock.
		F.Services.bLineOfSightClear = true;
		F.Guard->Senses.TickSight(*F.Guard, 112.5);
		TestTrue(TEXT("a clear segment restores sight"), F.Guard->Senses.Memory.bPlayerLos);
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
		TestFalse(TEXT("...and is not seen"), F.Guard->Senses.Memory.bPlayerLos);
		TestFalse(TEXT("...without spending a trace on it"),
			F.Services.Saw(TEXT("QueryLineOfSight")));
	}
	return true;
}

// =====================================================================================
// `GatherEnemyConditions`: the ten-failure debounce, and the memory bit that makes the
// found/lost outputs edges rather than a per-think stream.
// =====================================================================================

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

// =====================================================================================
// Hearing: the bus cursor, radius x scalar admission, the occlusion policy, and the
// category -> output mapping.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcSensesHearingTest,
	"Elysium.Substrate.NpcSenses.Hearing", GElysiumTestFlags)
bool FElysiumNpcSensesHearingTest::RunTest(const FString&)
{
	const FElysiumSoundVolumeTable Volumes = MakeVolumeTable();

	// --- Radius x the observer's hearing scalar decides admission ---------------------------------
	{
		FSensesFixture F(/*VisionUnits=*/4000.f, /*HearingScalar=*/2.0f);
		if (F.Guard == nullptr)
		{
			return false;
		}
		F.World.GameSounds().SetVolumeTable(&Volumes);
		F.Guard->Senses.StartSoundCursorAtHead(*F.Guard);

		// A 240-unit stimulus 400 units away: out of reach unheard, in reach at 2.0x.
		F.World.EmitGameSound(FVector(Cm(400.f), 0.0, 0.0), FName(TEXT("DOOR_NORMAL")), 0.f,
			FElysiumEntityHandle::Invalid());
		F.Guard->Senses.TickHearing(*F.Guard, 1.0);
		F.Flush(1.0);
		TestEqual(TEXT("the observer's hearing scalar extends the authored reach"),
			F.Counter(TEXT("c_hearworld")), 1.f);
		TestTrue(TEXT("...and the stimulus lands in memory"),
			F.Guard->Senses.Memory.LastHeardCategory == TEXT("DOOR_NORMAL"));
		TestTrue(TEXT("...with its position"),
			F.Guard->Senses.Memory.LastHeardPosition.Equals(FVector(Cm(400.f), 0.0, 0.0)));

		// The cursor has advanced: the same stimulus is not consumed twice.
		F.Guard->Senses.TickHearing(*F.Guard, 1.1);
		F.Flush(1.1);
		TestEqual(TEXT("the serial cursor stops a second consume"),
			F.Counter(TEXT("c_hearworld")), 1.f);

		// 600 units away is past 240 x 2.0.
		F.World.EmitGameSound(FVector(Cm(600.f), 0.0, 0.0), FName(TEXT("DOOR_NORMAL")), 0.f,
			FElysiumEntityHandle::Invalid());
		F.Guard->Senses.TickHearing(*F.Guard, 2.0);
		F.Flush(2.0);
		TestEqual(TEXT("a stimulus past radius x scalar is not heard"),
			F.Counter(TEXT("c_hearworld")), 1.f);
	}

	// --- Occlusion: an occludable stimulus behind a wall is dropped; a loud one is not ------------
	{
		FSensesFixture F;
		if (F.Guard == nullptr)
		{
			return false;
		}
		F.World.GameSounds().SetVolumeTable(&Volumes);
		F.Guard->Senses.StartSoundCursorAtHead(*F.Guard);
		F.Services.bLineOfSightClear = false;

		F.World.EmitGameSound(FVector(Cm(100.f), 0.0, 0.0), FName(TEXT("DOOR_NORMAL")), 0.f,
			FElysiumEntityHandle::Invalid());
		F.Guard->Senses.TickHearing(*F.Guard, 1.0);
		F.Flush(1.0);
		TestEqual(TEXT("an occludable stimulus behind a wall is dropped"),
			F.Counter(TEXT("c_hearworld")), 0.f);

		// The loud level is non-occluded in the table, so the same wall does not stop it.
		F.World.EmitGameSound(FVector(Cm(100.f), 0.0, 0.0), FName(TEXT("PLAYER_GUNSHOT_BASE")),
			0.f, FElysiumEntityHandle::Invalid());
		F.Guard->Senses.TickHearing(*F.Guard, 2.0);
		F.Flush(2.0);
		TestEqual(TEXT("a non-occluded stimulus is heard through the same wall"),
			F.Counter(TEXT("c_hearcombat")), 1.f);
	}

	// --- Own sounds are skipped, and the category mapping picks one output ------------------------
	{
		FSensesFixture F;
		if (F.Guard == nullptr || F.Player == nullptr)
		{
			return false;
		}
		F.World.GameSounds().SetVolumeTable(&Volumes);
		F.Guard->Senses.StartSoundCursorAtHead(*F.Guard);

		F.World.EmitGameSound(F.Guard->Origin, FName(TEXT("NPC_TAKE_DAMAGE")), 0.f,
			F.Guard->Handle);
		F.Guard->Senses.TickHearing(*F.Guard, 1.0);
		F.Flush(1.0);
		TestEqual(TEXT("an NPC does not hear its own noise"), F.Counter(TEXT("c_hearcombat")), 0.f);
		TestTrue(TEXT("...and it leaves no last-heard memory"),
			F.Guard->Senses.Memory.LastHeardTime < 0.0);

		// A combat category from the player takes the combat surface, not the player one.
		F.World.EmitGameSound(FVector(Cm(50.f), 0.0, 0.0), FName(TEXT("PLAYER_GUNSHOT_BASE")),
			0.f, F.Player->Handle);
		F.Guard->Senses.TickHearing(*F.Guard, 2.0);
		F.Flush(2.0);
		TestEqual(TEXT("a combat category outranks the player surface"),
			F.Counter(TEXT("c_hearcombat")), 1.f);
		TestEqual(TEXT("...so exactly one output fires"), F.Counter(TEXT("c_hearplayer")), 0.f);

		// A non-combat category the player made takes OnHearPlayer.
		F.World.EmitGameSound(FVector(Cm(50.f), 0.0, 0.0), FName(TEXT("PLAYER_FOOTSTEP_SNEAK")),
			0.f, F.Player->Handle);
		F.Guard->Senses.TickHearing(*F.Guard, 3.0);
		F.Flush(3.0);
		TestEqual(TEXT("a non-combat player stimulus takes OnHearPlayer"),
			F.Counter(TEXT("c_hearplayer")), 1.f);
		TestEqual(TEXT("...and not OnHearWorld"), F.Counter(TEXT("c_hearworld")), 0.f);
		TestTrue(TEXT("the last-heard source is the player"),
			F.Guard->Senses.Memory.LastHeardSource == F.Player->Handle);
	}
	return true;
}

// =====================================================================================
// The memory is what survives losing sight, so it is what a save has to carry.
// =====================================================================================

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

// =====================================================================================
// Where the senses do NOT run: a scripted owner suppresses condition gathering, and an
// inert or dead body has no senses at all.
// =====================================================================================

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
		F.Guard->NextThink = 0.0f;
		F.World.Tick(1.0);
		F.Guard->NextThink = 0.0f;
		F.World.Tick(2.0);

		F.World.EmitGameSound(FVector(Cm(50.f), 0.0, 0.0), FName(TEXT("PLAYER_GUNSHOT_BASE")),
			0.f, FElysiumEntityHandle::Invalid());
		F.Guard->ScriptOwner = F.Guard->Handle;   // a beat owns the body
		F.Guard->NextThink = 0.0f;
		F.World.Tick(3.0);
		F.World.Tick(3.1);
		TestEqual(TEXT("a script-owned body gathers no conditions"),
			F.Counter(TEXT("c_hearcombat")), 0.f);

		// Handing the body back resumes it: the stimulus is still inside the retention window.
		F.Guard->ScriptOwner = FElysiumEntityHandle::Invalid();
		F.Guard->NextThink = 0.0f;
		F.World.Tick(4.0);
		F.World.Tick(4.1);
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
		TestFalse(TEXT("a hidden body does not see"), F.Guard->Senses.Memory.bPlayerLos);

		F.Guard->bHidden = false;
		F.Guard->bDead = true;
		F.Guard->Senses.Tick(*F.Guard, 20.0);
		TestFalse(TEXT("a dead body does not see"), F.Guard->Senses.Memory.bPlayerLos);

		// Alive and unhidden, the same pass does see — so the two above are the gate, not the setup.
		F.Guard->bDead = false;
		F.Guard->Senses.Tick(*F.Guard, 30.0);
		TestTrue(TEXT("...and a live one does"), F.Guard->Senses.Memory.bPlayerLos);
	}
	return true;
}

}   // namespace ElysiumNpcSensesTests

#endif // WITH_DEV_AUTOMATION_TESTS
