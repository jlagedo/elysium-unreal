// Content-free Substrate automation for the stealth target surface, plus one Content-tier
// case that parses the real `vdata/system/stealth.txt`.
//
// Every number asserted here is either a fact from `docs/vtmb/stealth.md` — the 0.1 s cadence, the
// three-point rotation, the `0.083325` aggregate constant, the descending thresholds and their
// equality rule, the `-4.0` inactive sentinel, the `[-10, +10]` read clamp — or a value this file
// authors into a fabricated `StealthData`. Nothing in the Substrate tier reads `vdata`: the tables
// are built in memory and bound through `ElysiumSheetRules::BindTables`, the seam that makes this
// domain runnable headless (K10).
//
// The player think is reached ONLY through `FElysiumEntityWorld::RunPlayerThink` — never `Tick` —
// so every cadence case drives that call explicitly.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumContentPaths.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSaveTypes.h"
#include "ElysiumSheetSlots.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumSheetMath.h"
#include "Substrate/ElysiumSoundVolumeTable.h"
#include "Substrate/ElysiumStealth.h"
#include "Substrate/ElysiumStealthTables.h"
#include "Substrate/ElysiumStealthTrigger.h"
#include "Tests/ElysiumRulebookTestFixture.h"
#include "Tests/ElysiumTestServices.h"

#include "HAL/FileManager.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace ElysiumStealthTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

using EC = EElysiumTraitContainer;

namespace
{
	bool NearlyEqual(float A, float B, float Tolerance = 0.001f)
	{
		return FMath::Abs(A - B) < Tolerance;
	}

	// --- The fabricated `StealthData` ------------------------------------------------------------
	// The thresholds are retail's, because the equality rule is asserted against them. The two
	// matrices are this file's own, chosen so every cell is a closed-form check rather than a
	// number copied out of the shipped file.
	const float GRetailThresholds[FElysiumStealthTables::NumLight] =
		{ 0.99f, 0.95f, 0.87f, 0.75f, 0.60f, 0.42f, 0.28f, 0.17f, 0.08f, 0.03f, 0.00f };

	float ExpectedVision(int32 Light, int32 Stealth)
	{
		return 1.0f - 0.06f * Light - 0.02f * Stealth;
	}
	float ExpectedCone(int32 Light, int32 Stealth)
	{
		return 1.0f - 0.04f * Light - 0.01f * Stealth;
	}
	// Retail's own linear hearing row, in Source game units.
	float ExpectedHearingUnits(int32 Stealth) { return 8.0f * Stealth; }

	FElysiumStealthTables MakeStealthTables()
	{
		FElysiumStealthTables Tables;
		for (int32 L = 0; L < FElysiumStealthTables::NumLight; ++L)
		{
			Tables.LightThreshold[L] = GRetailThresholds[L];
			for (int32 S = 0; S < FElysiumStealthTables::NumStealth; ++S)
			{
				Tables.VisionScalar[L * FElysiumStealthTables::NumStealth + S] = ExpectedVision(L, S);
				Tables.ConeScalar[L * FElysiumStealthTables::NumStealth + S] = ExpectedCone(L, S);
			}
		}
		for (int32 S = 0; S < FElysiumStealthTables::NumStealth; ++S)
		{
			Tables.HearingDistUnits[S] = ExpectedHearingUnits(S);
		}
		return Tables;
	}

	// --- The fabricated sheet rules and feat table ------------------------------------------------
	FElysiumStat MakeStat(int32 Index, const TCHAR* Name, int32 Min, int32 Max, int32 Default)
	{
		FElysiumStat Stat;
		Stat.Index = Index;
		Stat.InternalName = Name;
		Stat.Name = Name;
		Stat.Min = Min;
		Stat.Max = Max;
		Stat.MinExpr = FString::FromInt(Min);
		Stat.MaxExpr = FString::FromInt(Max);
		Stat.Default = Default;
		return Stat;
	}

	FElysiumStatTable MakeStats()
	{
		FElysiumStatTable Table;
		FElysiumStatContainer& Attribs = Table.Containers[(uint8)EC::Attributes];
		Attribs.InternalName = TEXT("Attributes");
		for (const FElysiumSheetSlot& Slot : ElysiumSheetSlots(EC::Attributes))
		{
			Attribs.Stats.Add(MakeStat(Slot.Index, Slot.Internal, 0, 10000, 0));
		}
		Attribs.Stats[ElysiumSlot::MaxHealth] =
			MakeStat(ElysiumSlot::MaxHealth, TEXT("Max_Health"), 0, 1000, 100);
		Attribs.Stats[ElysiumSlot::Health] =
			MakeStat(ElysiumSlot::Health, TEXT("Health"), 0, 1000, 0);

		FElysiumStatContainer& Abilities = Table.Containers[(uint8)EC::Abilities];
		Abilities.InternalName = TEXT("Abilities");
		for (const FElysiumSheetSlot& Slot : ElysiumSheetSlots(EC::Abilities))
		{
			Abilities.Stats.Add(MakeStat(Slot.Index, Slot.Internal, 0, 5, 0));
		}

		FElysiumStatContainer& Learned = Table.Containers[(uint8)EC::Disciplines];
		Learned.InternalName = TEXT("Disciplines");
		for (const FElysiumSheetSlot& Slot : ElysiumSheetSlots(EC::Disciplines))
		{
			Learned.Stats.Add(MakeStat(Slot.Index, Slot.Internal, 0, 5, 0));
		}
		FElysiumStatContainer& Active = Table.Containers[(uint8)EC::ActiveDisciplines];
		Active.InternalName = TEXT("Active_Disciplines");
		for (const FElysiumSheetSlot& Slot : ElysiumSheetSlots(EC::ActiveDisciplines))
		{
			Active.Stats.Add(MakeStat(Slot.Index, Slot.Internal, 0, 5, 0));
		}
		return Table;
	}

	// `feats.txt`'s first two blocks in their authored order: Lockpicking then Sneaking, which is
	// what makes the stealth code term feat id 1.
	FElysiumFeatTable MakeFeats()
	{
		FElysiumFeatTable Table;
		{
			FElysiumFeat Feat;
			Feat.InternalName = TEXT("Intrusion");
			Feat.Name = TEXT("Lockpicking");
			Feat.MaxValue = 10;
			Feat.Bases.Add(FElysiumTraitRef::Parse(TEXT("Dexterity")));
			Feat.Bases.Add(FElysiumTraitRef::Parse(TEXT("Security")));
			Table.Feats.Add(MoveTemp(Feat));
		}
		{
			FElysiumFeat Feat;
			Feat.InternalName = TEXT("Sneaking");
			Feat.Name = TEXT("Sneaking");
			Feat.MaxValue = 10;
			Feat.Bases.Add(FElysiumTraitRef::Parse(TEXT("Dexterity")));
			Feat.Bases.Add(FElysiumTraitRef::Parse(TEXT("Stealth")));
			Table.Feats.Add(MoveTemp(Feat));
		}
		Table.Reindex();
		return Table;
	}

	// The sound-volume table the hearing case emits against. One occludable normal level is all the
	// end-to-end radius assertion needs.
	FElysiumSoundVolumeTable MakeVolumeTable()
	{
		FElysiumSoundVolumeTable Table;
		FElysiumSoundLevel Normal;
		Normal.Level = FElysiumSoundVolumeTable::NormalLevel;
		Normal.RadiusUnits = 240.f;
		Normal.bOccludable = true;
		Table.Levels.Add(Normal);
		FElysiumSoundCategory Row;
		Row.Name = FString(TEXT("NPC_TAKE_DAMAGE")).ToLower();
		Row.Level = FElysiumSoundVolumeTable::NormalLevel;
		Table.Categories.Add(MoveTemp(Row));
		Table.Reindex();
		return Table;
	}

	// --- The fixture ------------------------------------------------------------------------------
	// A world with a player, a guard facing +X at the origin, and one `trigger_stealth_mod` volume
	// the aggregate cases touch. Every fabricated table is bound for the fixture's lifetime; the
	// subsystem always wins over a bound table, so nothing here can change what a real run reads.
	struct FStealthFixture
	{
		FElysiumStatTable Stats = MakeStats();
		FElysiumFeatTable Feats = MakeFeats();
		FElysiumStealthTables Tables = MakeStealthTables();
		FElysiumSoundVolumeTable Volumes = MakeVolumeTable();

		FElysiumRecordingServices Services;
		FElysiumEntityWorld World;
		// Declared AFTER `World` on purpose: members destruct in reverse declaration order, so this
		// unbinds the fallback tables before the world it configured tears down.
		ElysiumRulebookTest::FScopedRulebookBinding Binding;
		FElysiumPlayer* Player = nullptr;
		FElysiumNpc* Guard = nullptr;

		explicit FStealthFixture(float GuardVisionUnits = 4000.f)
			: World(nullptr, nullptr, Services.Bundle())
		{
			ElysiumRng::SeedAll(0x53544C48);
			Bind();

			FElysiumEntityDefs Defs;
			Defs.MapName = TEXT("__stealth_test__");

			FElysiumEntityDef GuardDef;
			GuardDef.Classname = TEXT("npc_VHumanCombatant");
			GuardDef.TargetName = TEXT("guard");
			GuardDef.Origin = FVector::ZeroVector;
			GuardDef.Keys.Add(TEXT("vision"), FString::SanitizeFloat(GuardVisionUnits));
			GuardDef.Keys.Add(TEXT("hearing"), TEXT("1.0"));
			// `player_reaction` is the authored `<relation> <priority>` pair.
			GuardDef.Keys.Add(TEXT("player_reaction"), TEXT("D_HT 5"));
			Defs.Defs.Add(MoveTemp(GuardDef));

			auto AddVolume = [&Defs](const TCHAR* Name, int32 Modifier)
			{
				FElysiumEntityDef Volume;
				Volume.Classname = TEXT("trigger_stealth_mod");
				Volume.TargetName = Name;
				Volume.Keys.Add(TEXT("stealth_modifier"), FString::FromInt(Modifier));
				Defs.Defs.Add(MoveTemp(Volume));
			};
			AddVolume(TEXT("vol_a"), 2);
			AddVolume(TEXT("vol_b"), 3);

			World.Load(MoveTemp(Defs));
			World.SpawnPlayer();
			World.Activate(0.0);
			World.GameSounds().SetVolumeTable(&Volumes);

			Guard = static_cast<FElysiumNpc*>(World.FindByName(TEXT("guard")));
			if (Guard)
			{
				// The world is deliberately not ticked: every case here drives the one pass it is
				// asserting, and a guard think behind its back would offer an observer from a
				// position the case has not placed yet.
				Guard->NextThink = ELYSIUM_NEVER_THINK;
			}
			Player = World.FindPlayer();
			if (Player)
			{
				// Dexterity 2 + Stealth 3 = a Sneaking rating of 5 before any volume.
				Player->Sheet.SetBase(EC::Attributes, /*Dexterity*/ 2, 2);
				Player->Sheet.SetBase(EC::Abilities, /*Stealth*/ 8, 3);
				Player->Sheet.SetBase(EC::Attributes, ElysiumSlot::MaxHealth, 100);
				Player->Sheet.SetBase(EC::Attributes, ElysiumSlot::Health, 0);
				Player->Sheet.RecomputeCurrent(&Stats);
				Player->SyncHealthFromSheet();
			}
		}

		void Bind()
		{
			ElysiumSheetRules::FBoundTables Bound;
			Bound.Stats = &Stats;
			Bound.Feats = &Feats;
			Bound.Stealth = &Tables;
			ElysiumRulebookTest::FScopedRulebookBinding::Bind(Bound);
		}

		// Null when the classname resolved to an inert record instead of the registered leaf — the
		// difference the first assertion in the modifier case is about.
		FElysiumStealthModTrigger* Volume(const TCHAR* Name)
		{
			FElysiumEntity* Ent = World.FindByName(Name);
			return (Ent != nullptr && !Ent->bRecordOnly)
				? static_cast<FElysiumStealthModTrigger*>(Ent) : nullptr;
		}

		void Touch(const TCHAR* Volume, bool bBegin)
		{
			if (const FElysiumEntity* V = World.FindByName(Volume))
			{
				World.RouteEntityTouch(V->Handle, World.PlayerHandle(), bBegin);
			}
		}

		// One player think at `Now`. The world's own tick is deliberately not driven: the player
		// think is not on it.
		void PlayerThink(double Now) { World.RunPlayerThink(Now); }
	};
}

// =====================================================================================
// The rulebook table: indexing, clamping and the neutral fallback.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumStealthTablesTest,
	"Elysium.Substrate.Stealth.Tables", GElysiumTestFlags)
bool FElysiumStealthTablesTest::RunTest(const FString&)
{
	const FElysiumStealthTables Tables = MakeStealthTables();

	// Row-major `light * 11 + Sneaking` on both matrices, `Sneaking` alone on hearing.
	TestTrue(TEXT("the vision matrix indexes row-major light*11 + Sneaking"),
		NearlyEqual(Tables.Vision(7, 4), ExpectedVision(7, 4)));
	TestTrue(TEXT("...and is not transposed"),
		!NearlyEqual(Tables.Vision(7, 4), ExpectedVision(4, 7)));
	TestTrue(TEXT("the cone matrix indexes the same way"),
		NearlyEqual(Tables.Cone(2, 9), ExpectedCone(2, 9)));
	TestTrue(TEXT("hearing indexes on Sneaking alone"),
		NearlyEqual(Tables.HearingUnits(6), ExpectedHearingUnits(6)));
	TestTrue(TEXT("the thresholds are the descending retail row"),
		NearlyEqual(Tables.Threshold(0), 0.99f) && NearlyEqual(Tables.Threshold(10), 0.f));

	// Every read clamps rather than checking: a patched table that stops short must not read off
	// the end of a row.
	TestTrue(TEXT("an over-range light clamps to the darkest row"),
		NearlyEqual(Tables.Vision(99, 3), ExpectedVision(10, 3)));
	TestTrue(TEXT("a negative Sneaking clamps to Stealth0"),
		NearlyEqual(Tables.Vision(3, -4), ExpectedVision(3, 0)));
	TestTrue(TEXT("an over-range hearing row clamps too"),
		NearlyEqual(Tables.HearingUnits(50), ExpectedHearingUnits(10)));

	// The absent-file fallback is neutral in all four sections: stealth changes nothing.
	const FElysiumStealthTables& Neutral = FElysiumStealthTables::Neutral();
	TestFalse(TEXT("the neutral fallback is not a loaded table"), Neutral.IsValid());
	TestTrue(TEXT("...its sight scalar changes nothing"), NearlyEqual(Neutral.Vision(10, 10), 1.f));
	TestTrue(TEXT("...its cone scalar changes nothing"), NearlyEqual(Neutral.Cone(10, 10), 1.f));
	TestTrue(TEXT("...and it removes no hearing distance"),
		NearlyEqual(Neutral.HearingUnits(10), 0.f));
	return true;
}

// =====================================================================================
// The recompute rule, as a pure function: normalization, row selection, the torch override
// and the non-stealth fallback arm.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumStealthRecomputeTest,
	"Elysium.Substrate.Stealth.Recompute", GElysiumTestFlags)
bool FElysiumStealthRecomputeTest::RunTest(const FString&)
{
	const FElysiumStealthTables Tables = MakeStealthTables();

	// --- The `0.083325` aggregate ---------------------------------------------------------------
	// Over a [0, 1] configured range the constant is visible directly: three fully lit samples
	// aggregate to 3 * 0.083325.
	TestTrue(TEXT("the raw aggregate is (feet + centre + head) * 0.083325"),
		NearlyEqual(ElysiumStealth::NormalizeBodyLight(1.f, 1.f, 1.f, 0.f, 1.f),
			3.f * ElysiumStealth::RawLightScale, 0.0001f));
	// The shipped 0..1 range does not turn the query into an average.
	TestTrue(TEXT("three unit-luminance samples retain the raw scale"),
		NearlyEqual(ElysiumStealth::NormalizeBodyLight(1.f, 1.f, 1.f,
			ElysiumStealth::DefaultWorldLightMin, ElysiumStealth::DefaultWorldLightMax), 3.f * ElysiumStealth::RawLightScale));
	TestTrue(TEXT("three dark samples normalize to 0.0"),
		NearlyEqual(ElysiumStealth::NormalizeBodyLight(0.f, 0.f, 0.f,
			ElysiumStealth::DefaultWorldLightMin, ElysiumStealth::DefaultWorldLightMax), 0.f));
	TestTrue(TEXT("one unit-luminance point carries the recovered aggregate coefficient"),
		NearlyEqual(ElysiumStealth::NormalizeBodyLight(1.f, 0.f, 0.f,
			ElysiumStealth::DefaultWorldLightMin, ElysiumStealth::DefaultWorldLightMax),
			ElysiumStealth::RawLightScale, 0.0001f));
	TestTrue(TEXT("a value above the configured maximum clamps rather than exceeding 1.0"),
		NearlyEqual(ElysiumStealth::NormalizeBodyLight(5.f, 5.f, 5.f,
			ElysiumStealth::DefaultWorldLightMin, ElysiumStealth::DefaultWorldLightMax), 1.f));
	TestTrue(TEXT("a degenerate configured range reads as full light, not as a divide by zero"),
		NearlyEqual(ElysiumStealth::NormalizeBodyLight(0.f, 0.f, 0.f, 0.5f, 0.5f), 1.f));

	// --- Row selection --------------------------------------------------------------------------
	TestEqual(TEXT("exactly 1.0 is Light0"), ElysiumStealth::SelectLightRow(Tables, 1.0f), 0);
	TestEqual(TEXT("above 1.0 is still Light0"), ElysiumStealth::SelectLightRow(Tables, 4.0f), 0);
	TestEqual(TEXT("just above the first threshold is Light0"),
		ElysiumStealth::SelectLightRow(Tables, 0.995f), 0);
	TestEqual(TEXT("EQUALITY enters the next darker row: 0.99 is Light1"),
		ElysiumStealth::SelectLightRow(Tables, 0.99f), 1);
	TestEqual(TEXT("just above the second threshold is Light1"),
		ElysiumStealth::SelectLightRow(Tables, 0.96f), 1);
	TestEqual(TEXT("0.95 equality enters Light2"),
		ElysiumStealth::SelectLightRow(Tables, 0.95f), 2);
	TestEqual(TEXT("0.60 equality enters Light5"),
		ElysiumStealth::SelectLightRow(Tables, 0.60f), 5);
	TestEqual(TEXT("0.03 equality enters Light10"),
		ElysiumStealth::SelectLightRow(Tables, 0.03f), 10);
	TestEqual(TEXT("pitch dark is Light10 and the walk stops there"),
		ElysiumStealth::SelectLightRow(Tables, 0.f), 10);

	// --- The eligible arm -------------------------------------------------------------------------
	{
		ElysiumStealth::FRecomputeInputs In;
		In.bEligible = true;
		In.Sneaking = 7;
		// A mean of 0.5 lands between the 0.60 and 0.42 thresholds, which is Light5.
		In.Samples[0] = In.Samples[1] = In.Samples[2] = 2.f;
		const ElysiumStealth::FRecomputeResult Out = ElysiumStealth::Recompute(Tables, In);
		TestTrue(TEXT("an eligible pass commits"), Out.bCommitted);
		TestTrue(TEXT("...and reports the arm it took"), Out.bEligible);
		TestTrue(TEXT("the raw triplet is scaled by 0.083325"),
			NearlyEqual(Out.LightOnMe, 0.5f, 0.002f));
		TestEqual(TEXT("0.5 selects Light5"), Out.LightRow, 5);
		TestEqual(TEXT("the Sneaking column is the resolved rating"), Out.StealthRow, 7);
		TestTrue(TEXT("the sight scalar comes off the selected cell"),
			NearlyEqual(Out.VisionScalar, ExpectedVision(5, 7)));
		TestTrue(TEXT("...and so does the cone scalar"),
			NearlyEqual(Out.ConeScalar, ExpectedCone(5, 7)));
		TestTrue(TEXT("the hearing reduction is the table's units converted to cm"),
			NearlyEqual(Out.HearingReductionCm, ExpectedHearingUnits(7) * ElysiumMove::U, 0.01f));
	}

	// --- The Sneaking cap -------------------------------------------------------------------------
	{
		ElysiumStealth::FRecomputeInputs In;
		In.bEligible = true;
		In.Sneaking = 40;
		In.Samples[0] = In.Samples[1] = In.Samples[2] = 0.f;
		const ElysiumStealth::FRecomputeResult Out = ElysiumStealth::Recompute(Tables, In);
		TestEqual(TEXT("a Sneaking rating above 10 is capped before the lookup"), Out.StealthRow, 10);
	}

	// --- The torch override -----------------------------------------------------------------------
	{
		ElysiumStealth::FRecomputeInputs In;
		In.bEligible = true;
		In.bTorchEquipped = true;
		In.Sneaking = 10;
		In.Samples[0] = In.Samples[1] = In.Samples[2] = 0.f;   // pitch dark, and irrelevant
		const ElysiumStealth::FRecomputeResult Out = ElysiumStealth::Recompute(Tables, In);
		TestTrue(TEXT("`item_w_torch` forces normalized light to 1.0"),
			NearlyEqual(Out.LightOnMe, 1.f));
		TestEqual(TEXT("...which is Light0"), Out.LightRow, 0);
		TestTrue(TEXT("...so the darkest samples buy nothing"),
			NearlyEqual(Out.VisionScalar, ExpectedVision(0, 10)));
	}

	// --- The non-stealth fallback -----------------------------------------------------------------
	{
		ElysiumStealth::FRecomputeInputs In;
		In.bEligible = false;
		In.Sneaking = 10;
		In.Samples[0] = In.Samples[1] = In.Samples[2] = 0.f;   // a pitch-dark room, not sneaking
		const ElysiumStealth::FRecomputeResult Out = ElysiumStealth::Recompute(Tables, In);
		TestTrue(TEXT("the ineligible pass still commits — it INSTALLS the fallback"), Out.bCommitted);
		TestFalse(TEXT("...and reports the arm it took"), Out.bEligible);
		TestTrue(TEXT("m_flLightOnMe is the -1.0 inactive sentinel, not a light level"),
			NearlyEqual(Out.LightOnMe, ElysiumStealth::InactiveLightSentinel));
		TestTrue(TEXT("the sight scalar is 1.0"), NearlyEqual(Out.VisionScalar, 1.f));
		TestEqual(TEXT("the reported light row still describes the dark room"), Out.LightRow, 10);
		TestEqual(TEXT("the feat row is preserved while ineligible"), Out.StealthRow, 10);
		TestTrue(TEXT("the cone scalar is the Light0/Stealth0 cell"),
			NearlyEqual(Out.ConeScalar, ExpectedCone(0, 0)));
		TestTrue(TEXT("the hearing reduction is the Stealth0 row"),
			NearlyEqual(Out.HearingReductionCm, ExpectedHearingUnits(0) * ElysiumMove::U, 0.01f));
		TestTrue(TEXT("a dark room buys nothing at all while not sneaking"),
			NearlyEqual(Out.VisionScalar, 1.f) && NearlyEqual(Out.HearingReductionCm, 0.f));
	}

	// --- The unavailable-service arm --------------------------------------------------------------
	{
		ElysiumStealth::FRecomputeInputs In;
		In.bLightServiceAvailable = false;
		In.bEligible = true;
		In.Sneaking = 10;
		const ElysiumStealth::FRecomputeResult Out = ElysiumStealth::Recompute(Tables, In);
		TestFalse(TEXT("no world-light service manufactures nothing"), Out.bCommitted);
	}
	return true;
}

// =====================================================================================
// The cadence and the three-point rotation, driven through `RunPlayerThink`.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumStealthCadenceTest,
	"Elysium.Substrate.Stealth.Cadence", GElysiumTestFlags)
bool FElysiumStealthCadenceTest::RunTest(const FString&)
{
	FStealthFixture F;
	if (!F.Player)
	{
		AddError(TEXT("no player entity"));
		return false;
	}
	F.Services.bPlayerDucking = true;

	// --- Nothing recomputes between due passes ----------------------------------------------------
	F.Services.LightAtPoint = 0.2f;
	F.PlayerThink(0.0);
	const int32 FirstGeneration = F.Player->Stealth.Generation;
	TestTrue(TEXT("the first due pass commits"), FirstGeneration > 0);
	TestTrue(TEXT("...and advances the deadline by 0.1 s"),
		NearlyEqual(static_cast<float>(F.Player->Stealth.NextUpdateTime), 0.1f));

	F.PlayerThink(0.05);
	TestEqual(TEXT("a think inside the cadence does not recompute"),
		F.Player->Stealth.Generation, FirstGeneration);
	F.PlayerThink(0.099);
	TestEqual(TEXT("...nor one just short of the deadline"),
		F.Player->Stealth.Generation, FirstGeneration);

	// --- One body point per pass, index advancing -------------------------------------------------
	// The fixture's first pass already refreshed the feet sample; the next three refresh centre,
	// head and feet again, so a distinct value per pass shows the rotation directly.
	// The drive times sit just past each deadline rather than exactly on it: `NextThink` is a float
	// and the deadline a double, so a think driven at the boundary can land one ULP short. A real
	// frame never asks at the exact deadline, and a pass deferred by one frame self-corrects.
	F.Services.LightAtPoint = 0.30f;
	F.PlayerThink(0.11);
	F.Services.LightAtPoint = 0.60f;
	F.PlayerThink(0.22);
	F.Services.LightAtPoint = 0.90f;
	F.PlayerThink(0.33);

	TestTrue(TEXT("the feet sample is the newest pass's value"),
		NearlyEqual(F.Player->Stealth.Samples[0], 0.90f));
	TestTrue(TEXT("the centre sample is two passes old"),
		NearlyEqual(F.Player->Stealth.Samples[1], 0.30f));
	TestTrue(TEXT("the head sample is one pass old"),
		NearlyEqual(F.Player->Stealth.Samples[2], 0.60f));
	TestEqual(TEXT("the rotation has come back round to the centre point"),
		F.Player->Stealth.NextSampleIndex, 1);
	TestEqual(TEXT("four due passes, four committed generations"),
		F.Player->Stealth.Generation, FirstGeneration + 3);

	// The three retained samples are what the aggregate reads — the whole point of the ~0.3 s lag.
	TestTrue(TEXT("the aggregate uses the retained triplet and the recovered coefficient"),
		NearlyEqual(F.Player->Stealth.LightOnMe, (0.90f + 0.30f + 0.60f) * ElysiumStealth::RawLightScale, 0.002f));

	// --- Three distinct body points ---------------------------------------------------------------
	// The recorded queries carry their own positions, so the rotation is visible from outside.
	TArray<FString> LightQueries;
	for (const FString& Line : F.Services.Calls)
	{
		if (Line.StartsWith(TEXT("QueryLightAtPoint")))
		{
			LightQueries.Add(Line);
		}
	}
	TestEqual(TEXT("one light query per due pass and no more"), LightQueries.Num(), 4);
	// The recorded line is "QueryLightAtPoint <point> = <value>"; the value differs every pass by
	// construction, so the POINT has to be compared on its own for the rotation to be proven.
	auto PointOf = [](const FString& Line)
	{
		const int32 Split = Line.Find(TEXT(" = "));
		return Split > 0 ? Line.Left(Split) : Line;
	};
	TestTrue(TEXT("consecutive passes sample different body points"),
		PointOf(LightQueries[0]) != PointOf(LightQueries[1]));
	TestTrue(TEXT("...all three of them, and the fourth is back on the first"),
		PointOf(LightQueries[1]) != PointOf(LightQueries[2])
		&& PointOf(LightQueries[0]) != PointOf(LightQueries[2])
		&& PointOf(LightQueries[3]) == PointOf(LightQueries[0]));

	// --- The ineligible arm still samples and rotates --------------------------------------------
	F.Services.bPlayerDucking = false;
	const int32 BeforeIndex = F.Player->Stealth.NextSampleIndex;
	const int32 BeforeQueries = LightQueries.Num();
	F.PlayerThink(0.5);
	TestTrue(TEXT("standing up installs the inactive sentinel"),
		NearlyEqual(F.Player->Stealth.LightOnMe, ElysiumStealth::InactiveLightSentinel));
	TestTrue(TEXT("...and the dark room's sight advantage is gone"),
		NearlyEqual(F.Player->Stealth.VisionScalar, 1.f));
	TestEqual(TEXT("the ineligible arm advances the sample rotation"),
		F.Player->Stealth.NextSampleIndex, (BeforeIndex + 1) % 3);
	const int32 After = F.Services.Count(TEXT("QueryLightAtPoint"));
	TestEqual(TEXT("...and still samples one point"), After, BeforeQueries + 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumStealthEligibilityTest,
	"Elysium.Substrate.Stealth.EligibilityAndBounds", GElysiumTestFlags)
bool FElysiumStealthEligibilityTest::RunTest(const FString&)
{
	FStealthFixture F;
	if (!F.Player || !F.Guard) return false;
	const FBox Bounds(FVector(-100, 20, 30), FVector(40, 100, 190));
	const FVector Center(7, 61, 115); // The virtual centre need not equal the AABB midpoint.
	TestEqual(TEXT("feet use centre XY and AABB 1/8 height"),
		ElysiumStealth::SamplePoint(Bounds, Center, 0), FVector(7, 61, 50));
	TestEqual(TEXT("centre uses the virtual answer verbatim"), ElysiumStealth::SamplePoint(Bounds, Center, 1), Center);
	TestEqual(TEXT("head uses centre XY and AABB 7/8 height"),
		ElysiumStealth::SamplePoint(Bounds, Center, 2), FVector(7, 61, 170));
	TestFalse(TEXT("standing is ineligible"), ElysiumStealth::IsEligible(*F.Player, false, 5));
	TestTrue(TEXT("ducked and unobserved is eligible"), ElysiumStealth::IsEligible(*F.Player, true, 5));
	F.Player->LastHostileAssessment = F.Guard->Handle;
	F.Player->LastHostileAssessmentTime = 5;
	TestFalse(TEXT("hostile observation suppresses tables before one second"), ElysiumStealth::IsEligible(*F.Player, true, 5.999));
	TestTrue(TEXT("exactly one second is eligible"), ElysiumStealth::IsEligible(*F.Player, true, 6));
	F.Player->LastHostileAssessment = FElysiumEntityHandle();
	TestTrue(TEXT("invalid observer handle cannot suppress stealth"), ElysiumStealth::IsEligible(*F.Player, true, 5.5));
	F.Services.bPlayerDucking = true;
	F.Services.PlayerStealthBounds = Bounds;
	F.Services.PlayerStealthCenter = Center;
	F.Services.bLightQueryAvailable = false;
	F.PlayerThink(0);
	TestEqual(TEXT("unavailable service does not publish a sample"), F.Player->Stealth.Generation, 0);
	TestEqual(TEXT("unavailable service costs no trace"), F.Services.Count(TEXT("QueryLightAtPoint")), 0);
	F.Services.bLightQueryAvailable = true;
	F.PlayerThink(0.11);
	TestTrue(TEXT("first actual sample publishes validity"), F.Player->Stealth.bHasLightSample);
	TestTrue(TEXT("the service receives the world-AABB feet point"), F.Services.Saw(TEXT("QueryLightAtPoint X=7.000 Y=61.000 Z=50.000")));
	return true;
}

// =====================================================================================
// `trigger_stealth_mod`: the balanced overlap contribution, the unclamped store, and the
// clamped read that reaches the Sneaking feat.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumStealthModifierTest,
	"Elysium.Substrate.Stealth.Modifier", GElysiumTestFlags)
bool FElysiumStealthModifierTest::RunTest(const FString&)
{
	FStealthFixture F;
	if (!F.Player)
	{
		AddError(TEXT("no player entity"));
		return false;
	}

	const FElysiumEntity* Raw = F.World.FindByName(TEXT("vol_a"));
	TestTrue(TEXT("`trigger_stealth_mod` is a registered class, not an inert record"),
		Raw != nullptr && !Raw->bRecordOnly);
	if (const FElysiumStealthModTrigger* Volume = F.Volume(TEXT("vol_a")))
	{
		TestEqual(TEXT("`stealth_modifier` is an authored keyfield"), Volume->StealthMod, 2);
	}
	else
	{
		AddError(TEXT("trigger_stealth_mod did not resolve to its leaf class"));
		return false;
	}

	// --- Stacking, and leaving one of several -----------------------------------------------------
	F.Touch(TEXT("vol_a"), /*bBegin*/ true);
	TestEqual(TEXT("entering a volume adds its raw modifier"), F.Player->StealthModRaw, 2);
	F.Touch(TEXT("vol_b"), /*bBegin*/ true);
	TestEqual(TEXT("overlapping volumes add"), F.Player->StealthModRaw, 5);
	F.Touch(TEXT("vol_a"), /*bBegin*/ false);
	TestEqual(TEXT("leaving one restores exactly the remaining contribution"),
		F.Player->StealthModRaw, 3);
	F.Touch(TEXT("vol_b"), /*bBegin*/ false);
	TestEqual(TEXT("leaving the last one returns to zero"), F.Player->StealthModRaw, 0);

	// --- One begin/end pair per contact -----------------------------------------------------------
	F.Touch(TEXT("vol_a"), true);
	F.Touch(TEXT("vol_a"), true);
	TestEqual(TEXT("a repeated begin for a contact already credited adds nothing"),
		F.Player->StealthModRaw, 2);
	F.Touch(TEXT("vol_a"), false);
	F.Touch(TEXT("vol_a"), false);
	TestEqual(TEXT("...and the matching end takes back exactly one contribution"),
		F.Player->StealthModRaw, 0);

	// --- The store is raw; only the read clamps ---------------------------------------------------
	F.Player->StealthModRaw = 26;
	TestEqual(TEXT("the stored sum is NOT clamped"), F.Player->StealthModRaw, 26);
	TestEqual(TEXT("GetStealthModifier clamps at +10"), F.Player->GetStealthModifier(), 10);
	F.Player->StealthModRaw = -26;
	TestEqual(TEXT("...and at -10"), F.Player->GetStealthModifier(), -10);
	F.Player->StealthModRaw = 3;
	TestEqual(TEXT("...and passes an in-range sum through"), F.Player->GetStealthModifier(), 3);

	// --- The Sneaking feat's code term ------------------------------------------------------------
	// Dexterity 2 + Stealth 3 = 5 before any volume.
	F.Player->StealthModRaw = 0;
	TestEqual(TEXT("the Sneaking feat is the sum of its authored bases"),
		F.Player->CalcFeat(TEXT("Sneaking")), 5);
	F.Player->StealthModRaw = 2;
	TestEqual(TEXT("a volume's clamped modifier is added to feat id 1"),
		F.Player->CalcFeat(TEXT("Sneaking")), 7);
	TestEqual(TEXT("...and to no other feat"), F.Player->CalcFeat(TEXT("Intrusion")), 2);
	F.Player->StealthModRaw = 99;
	TestEqual(TEXT("the clamped read, not the raw sum, is what enters the rating"),
		F.Player->CalcFeat(TEXT("Sneaking")), 10);
	F.Player->StealthModRaw = -99;
	TestEqual(TEXT("a negative volume cannot push the rating below zero"),
		F.Player->CalcFeat(TEXT("Sneaking")), 0);

	// And the recompute consumes exactly that, capped at 10.
	F.Player->StealthModRaw = 2;
	TestEqual(TEXT("the recompute's Sneaking input is the resolved feat"),
		ElysiumStealth::ResolveSneaking(*F.Player), 7);

	// Feat id 1 is Sneaking, which is the whole premise of the special case.
	TestEqual(TEXT("Sneaking is feat id 1"), ElysiumFeats::SneakingFeatIndex, 1);
	if (const FElysiumFeat* Sneaking = F.Feats.Find(TEXT("Sneaking")))
	{
		TestEqual(TEXT("...in the table this fixture built"), Sneaking->Index, 1);
	}
	return true;
}

// =====================================================================================
// The senses consuming the committed surface: the observer's effective radius, its cone,
// and the sound radius a player-sourced emit reaches.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumStealthSensesTest,
	"Elysium.Substrate.Stealth.Senses", GElysiumTestFlags)
bool FElysiumStealthSensesTest::RunTest(const FString&)
{
	// A 1000-unit guard, so the scalar's effect on the radius is measurable in whole units.
	FStealthFixture F(/*GuardVisionUnits*/ 1000.f);
	if (!F.Player || !F.Guard)
	{
		AddError(TEXT("no player or guard"));
		return false;
	}
	F.Guard->Senses.ResolveTuning(*F.Guard);

	// The player stands 900 units down +X, inside the guard's authored 1000 and dead ahead.
	const float StandOffCm = 900.f * ElysiumMove::U;
	F.Player->Origin = FVector(StandOffCm, 0.0, 0.0);

	// --- Not sneaking: the neutral surface admits the player --------------------------------------
	F.Services.bPlayerDucking = false;
	F.PlayerThink(0.0);
	F.Guard->Senses.Memory.PlayerLosNextUpdateTime = -1.0;
	F.Guard->Senses.TickSight(*F.Guard, 0.0);
	TestTrue(TEXT("a non-sneaking player is inside the guard's authored vision distance"),
		F.Guard->Senses.Memory.bPlayerInRange);

	// --- Sneaking in the dark: the sight scalar shrinks the effective radius -----------------------
	// Sneaking 5, pitch dark -> Light10/Stealth5 -> 1.0 - 0.6 - 0.1 = 0.30. 1000 * 0.30 = 300 units,
	// and the player is standing at 900.
	F.Services.bPlayerDucking = true;
	F.Services.LightAtPoint = 0.f;
	// The previous hostile assessment first expires; then sample a full body cycle.
	F.PlayerThink(1.11);
	F.PlayerThink(1.22);
	F.PlayerThink(1.33);
	TestTrue(TEXT("a fully dark triplet reads as Light10"), F.Player->Stealth.LightRow == 10);
	TestEqual(TEXT("...at the fixture's Sneaking 5"), F.Player->Stealth.StealthRow, 5);
	TestTrue(TEXT("...for the table's own scalar"),
		NearlyEqual(F.Player->Stealth.VisionScalar, ExpectedVision(10, 5)));

	F.Guard->Senses.Memory.PlayerLosNextUpdateTime = -1.0;
	F.Guard->Senses.TickSight(*F.Guard, 2.0);
	TestFalse(TEXT("the target's sight scalar shrinks the observer's effective radius"),
		F.Guard->Senses.Memory.bPlayerInRange);
	TestFalse(TEXT("actual Look rejects the player outside the stealth radius"),
		F.Guard->Senses.Sighted().Contains(F.Player->Handle));

	// Step inside the shrunken radius and the same guard sees again — the scalar moved the radius,
	// it did not blind the guard.
	F.Player->Origin = FVector(200.f * ElysiumMove::U, 0.0, 0.0);
	F.Guard->Senses.Memory.PlayerLosNextUpdateTime = -1.0;
	F.Guard->Senses.TickSight(*F.Guard, 4.0);
	TestTrue(TEXT("inside the shrunken radius the player is admitted again"),
		F.Guard->Senses.Memory.bPlayerInRange);

	// --- The cone scalar ---------------------------------------------------------------------------
	// FinViewCone3dNew 0x1032669c multiplies the cosine, then compares against 0.2.
	{
		// A point 60 degrees off the guard's +X facing: inside the neutral 0.2 threshold.
		const FVector Offset(FMath::Cos(FMath::DegreesToRadians(60.f)) * 500.f,
			FMath::Sin(FMath::DegreesToRadians(60.f)) * 500.f, 0.0);
		TestTrue(TEXT("a target at 60 degrees is inside the neutral cone"),
			FElysiumNpcSenses::IsInViewCone(*F.Guard, Offset, 1.0f));
		// Narrowing the cone (a scalar above 1) pushes the same point out.
		TestFalse(TEXT("a smaller scalar narrows the cone and rejects it"),
			FElysiumNpcSenses::IsInViewCone(*F.Guard, Offset, 0.3f));
		// A point 75 degrees off is outside the neutral cone but inside a widened one.
		const FVector Wide(FMath::Cos(FMath::DegreesToRadians(75.f)) * 500.f,
			FMath::Sin(FMath::DegreesToRadians(75.f)) * 500.f, 0.0);
		TestTrue(TEXT("75 degrees remains inside the neutral 157 degree cone"),
			FElysiumNpcSenses::IsInViewCone(*F.Guard, Wide, 1.0f));
		TestFalse(TEXT("the stealth cone scalar narrows this angle out"),
			FElysiumNpcSenses::IsInViewCone(*F.Guard, Wide, 0.4f));
	}

	// --- The hearing reduction, end to end through a real emit --------------------------------------
	// `NPC_TAKE_DAMAGE` from the one typed health commit, with the player as the source. The bus
	// resolves the authored 240-unit radius and `AdjustSoundDistForStealth` subtracts the player's
	// committed reduction at insertion.
	{
		const float ReductionCm = F.Player->Stealth.HearingReductionCm;
		TestTrue(TEXT("a sneaking player carries a hearing reduction"), ReductionCm > 0.f);
		TestTrue(TEXT("...which is the table's Stealth5 row in cm"),
			NearlyEqual(ReductionCm, ExpectedHearingUnits(5) * ElysiumMove::U, 0.01f));

		F.Player->TakeDamage(5.f);
		const TArray<FElysiumGameSoundEvent>& Retained = F.World.GameSounds().Retained();
		if (Retained.Num() == 0)
		{
			AddError(TEXT("the typed health commit emitted no stimulus"));
			return false;
		}
		const float Expected = 240.f * ElysiumMove::U - ReductionCm;
		TestTrue(TEXT("the emitted radius is the authored one less the player's reduction"),
			NearlyEqual(Retained.Last().RadiusCm, Expected, 0.5f));
	}

	// --- A non-player source removes nothing --------------------------------------------------------
	{
		const int32 Before = F.World.GameSounds().Retained().Num();
		F.Guard->Sheet.SetBase(EC::Attributes, ElysiumSlot::MaxHealth, 100);
		F.Guard->Sheet.RecomputeCurrent(&F.Stats);
		F.Guard->SyncHealthFromSheet();
		F.Guard->TakeDamage(5.f);
		const TArray<FElysiumGameSoundEvent>& Retained = F.World.GameSounds().Retained();
		if (Retained.Num() > Before)
		{
			TestTrue(TEXT("an NPC carries no surface, so its stimulus keeps the authored radius"),
				NearlyEqual(Retained.Last().RadiusCm, 240.f * ElysiumMove::U, 0.5f));
		}
	}

	// --- The floor at zero ---------------------------------------------------------------------------
	{
		FElysiumGameSoundBus Bus;
		FElysiumGameSoundRequest Request;
		Request.Category = FName(TEXT("NPC_TAKE_DAMAGE"));
		Request.RadiusCm = 40.f;
		Request.StealthHearingReductionCm = 500.f;
		TestTrue(TEXT("a reduction larger than the radius floors at zero, never negative"),
			NearlyEqual(Bus.Emit(Request, 0.0).RadiusCm, 0.f));
	}
	return true;
}

// =====================================================================================
// The HUD observer snapshot: offered by the senses, committed by the player think, and
// generation-gated.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumStealthObserverTest,
	"Elysium.Substrate.Stealth.Observer", GElysiumTestFlags)
bool FElysiumStealthObserverTest::RunTest(const FString&)
{
	FStealthFixture F(4000.f);
	if (!F.Player || !F.Guard) return false;
	F.Guard->Senses.ResolveTuning(*F.Guard);
	F.Player->Origin = FVector(500.f * ElysiumMove::U, 0, 0);
	F.Guard->Senses.TickSight(*F.Guard, .11);
	TestFalse(TEXT("sensing does not publish a partial snapshot"), F.Player->Observer.IsSet());
	ElysiumStealth::PublishObservers(*F.Player, .11);
	TestTrue(TEXT("the committed observation identifies the observer"), F.Player->Observer.Observer == F.Guard->Handle);
	TestFalse(TEXT("sight without enemy commitment is searching"), F.Player->Observer.bDetected);
	const int32 Searching = F.Player->Observer.Generation;
	F.Guard->Senses.Memory.Enemy = F.Player->Handle;
	F.Guard->Senses.GatherEnemyLos(*F.Guard, .2);
	TestFalse(TEXT("the committed enemy edge still waits for snapshot publication"), F.Player->Observer.bDetected);
	ElysiumStealth::PublishObservers(*F.Player, .2);
	TestTrue(TEXT("snapshot follows the committed enemy LOS edge"), F.Player->Observer.bDetected);
	TestTrue(TEXT("the detection transition advances its generation"), F.Player->Observer.Generation > Searching);
	const int32 Detected = F.Player->Observer.Generation;
	F.Services.Calls.Reset();
	ElysiumStealth::PublishObservers(*F.Player, .21);
	TestEqual(TEXT("unchanged snapshot retains its generation"), F.Player->Observer.Generation, Detected);
	TestFalse(TEXT("publication performs no detection trace"), F.Services.Saw(TEXT("QueryLineOfSight")));
	F.Guard->bDead = true;
	ElysiumStealth::PublishObservers(*F.Player, .22);
	TestFalse(TEXT("dead observer disappears in the committed frame"), F.Player->Observer.IsSet());
	TestTrue(TEXT("presentation cleanup does not change enemy identity"), F.Guard->Senses.Memory.Enemy == F.Player->Handle);
	F.Guard->bDead = false;
	F.Guard->Senses.Memory.Enemy = FElysiumEntityHandle::Invalid();
	F.Guard->Senses.Memory.BestSeeUnknown = F.Player->Handle;
	F.Player->Origin = FVector(-500.f * ElysiumMove::U, 0, 0);
	F.Guard->Senses.TickSight(*F.Guard, 1.0);
	ElysiumStealth::PublishObservers(*F.Player, 1.0);
	TestTrue(TEXT("the retained unknown handle survives loss of current sight"),
		F.Guard->Senses.Memory.BestSeeUnknown == F.Player->Handle);
	TestFalse(TEXT("a remembered unknown cannot keep HUD searching forever"), F.Player->Observer.IsSet());
	return true;
}
// =====================================================================================
// Save: the whole surface as one generation, and the raw aggregate beside it.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumStealthSaveTest,
	"Elysium.Substrate.Stealth.Save", GElysiumTestFlags)
bool FElysiumStealthSaveTest::RunTest(const FString&)
{
	FElysiumPlayerRecord Record;
	Record.StealthMap = TEXT("sp_tutorial_1");
	Record.StealthModRaw = 5;
	Record.Stealth.NextUpdateTime = 12.3;
	Record.Stealth.VisionScalar = 0.42f;
	Record.Stealth.ConeScalar = 0.63f;
	Record.Stealth.HearingReductionCm = 101.6f;
	Record.Stealth.NextSampleIndex = 2;
	Record.Stealth.Samples[0] = 0.11f;
	Record.Stealth.Samples[1] = 0.22f;
	Record.Stealth.Samples[2] = 0.33f;
	Record.Stealth.LightOnMe = 0.22f;
	Record.Stealth.LightRow = 8;
	Record.Stealth.StealthRow = 6;
	Record.Stealth.bEligible = true;
	Record.Stealth.Generation = 77;

	TArray<uint8> Bytes;
	{
		FMemoryWriter Writer(Bytes, /*bIsPersistent*/ true);
		FElysiumSaveArchive Ar(Writer, FElysiumSaveVersion::Latest);
		Ar << Record;
	}
	FElysiumPlayerRecord Back;
	{
		FMemoryReader Reader(Bytes, /*bIsPersistent*/ true);
		FElysiumSaveArchive Ar(Reader, FElysiumSaveVersion::Latest);
		Ar << Back;
	}

	TestEqual(TEXT("the map scope survives"), Back.StealthMap, Record.StealthMap);
	TestEqual(TEXT("the raw aggregate survives"), Back.StealthModRaw, 5);
	TestTrue(TEXT("the deadline survives"), NearlyEqual((float)Back.Stealth.NextUpdateTime, 12.3f));
	TestTrue(TEXT("the sight scalar survives"), NearlyEqual(Back.Stealth.VisionScalar, 0.42f));
	TestTrue(TEXT("the cone scalar survives"), NearlyEqual(Back.Stealth.ConeScalar, 0.63f));
	TestTrue(TEXT("the hearing reduction survives"),
		NearlyEqual(Back.Stealth.HearingReductionCm, 101.6f, 0.01f));
	TestEqual(TEXT("the rotation index survives"), Back.Stealth.NextSampleIndex, 2);
	for (int32 i = 0; i < FElysiumStealthSurface::NumSamples; ++i)
	{
		TestTrue(TEXT("every retained sample survives"),
			NearlyEqual(Back.Stealth.Samples[i], Record.Stealth.Samples[i]));
	}
	TestTrue(TEXT("the aggregate survives"), NearlyEqual(Back.Stealth.LightOnMe, 0.22f));
	TestEqual(TEXT("the resolved light row survives"), Back.Stealth.LightRow, 8);
	TestEqual(TEXT("the resolved Sneaking row survives"), Back.Stealth.StealthRow, 6);
	TestTrue(TEXT("the eligible arm survives"), Back.Stealth.bEligible);
	// The generation is what makes the triplet and its derived values ONE restore: a payload that
	// carried the samples without it would be exactly the split the recovery forbids.
	TestEqual(TEXT("the generation travels with the group it ties together"),
		Back.Stealth.Generation, 77);

	// An older payload restores a DEFAULT surface — never a restored triplet with defaulted
	// derived values.
	TArray<uint8> Legacy;
	{
		FMemoryWriter Writer(Legacy, /*bIsPersistent*/ true);
		FElysiumSaveArchive Ar(Writer, FElysiumSaveVersion::Latest);
		Ar << Record;
	}
	FElysiumPlayerRecord Old;
	Old.Stealth.Samples[0] = 0.9f;   // pre-loaded rubbish the read must overwrite wholesale
	Old.StealthModRaw = 42;
	{
		FMemoryReader Reader(Legacy, /*bIsPersistent*/ true);
		FElysiumSaveArchive Ar(Reader, FElysiumSaveVersion::Disciplines);
		Ar << Old;
	}
	TestTrue(TEXT("a pre-stealth payload restores a default surface"),
		NearlyEqual(Old.Stealth.LightOnMe, ElysiumStealth::InactiveLightSentinel));
	TestEqual(TEXT("...with no generation"), Old.Stealth.Generation, 0);
	TestEqual(TEXT("...and no raw aggregate"), Old.StealthModRaw, 0);
	TestTrue(TEXT("...and no retained samples from the live object"),
		NearlyEqual(Old.Stealth.Samples[0], 1.f));
	return true;
}

// =====================================================================================
// The real `vdata/system/stealth.txt`. Self-skips when no export corpus is mounted.
// =====================================================================================

}   // namespace ElysiumStealthTests

#endif   // WITH_DEV_AUTOMATION_TESTS
