#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumStub.h"
#include "Misc/ScopeExit.h"
#include "Substrate/ElysiumAiScriptedSchedule.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcAndreiBlood.h"
#include "Substrate/ElysiumNpcChangBros.h"
#include "Substrate/ElysiumNpcGargoyle.h"
#include "Substrate/ElysiumNpcWerewolf.h"
#include "Substrate/ElysiumNpcTzimisceHeadClaw.h"
#include "Substrate/ElysiumNpcTzimisceRunner.h"
#include "Substrate/ElysiumNpcYukie.h"
#include "Substrate/ElysiumNpcTzimisce.h"
#include "Substrate/ElysiumNpcCamera.h"
#include "Substrate/ElysiumNpcBach.h"
#include "Substrate/ElysiumNpcMingXiaoTentacle.h"
#include "Substrate/ElysiumNpcNewscaster.h"
#include "Substrate/ElysiumNpcAnimal.h"
#include "Substrate/ElysiumNpcZombie.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcDialogue.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcMaker.h"
#include "Substrate/ElysiumNpcMakerFleshpile.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumNpcTestCensus.h"

// Story 29c-1, family **Species**. Every assertion comes from the decompiled C of the row it names
// and from the `.rdata` cells read out of the pinned retail `vampire.dll` — the slot-323 band
// boundaries (45 / 135 / 225 / **316**), the two blacklists' swap-remove, the melee quartet's
// per-species arm order, Bach's arm-then-fire hysteresis, the slot-609 state gate, Andrei's
// two-runner budget, the Werewolf door's octagonal distance and Zombie's `ZombieAIType` reroll.
//
// Where a body can only answer "nothing" because its input is a seam — the attack coordinator, the
// ragdoll bone table, the hint node's entity, the `Float Sound Info` KeyValues block — the case says
// so: that the seam is asked and that the refusal is the recovered one.
//
// **Both tables are checked before a species answer is asserted.** Every carrier (the fleshpile maker
// included, since story 5 fold A4) is spawned by the classname its retail factory builds it from
// (`Substrate/ElysiumNpcClasses.cpp`), and the Troika side of a wiring case is a bare
// `CAI_BaseNPCTroika` (0019 story 5 step 2). `npc_VCop`'s factory answer, `CNPC_VCop`, is asserted.

static constexpr EAutomationTestFlags GElysiumNpcKernelSpeciesFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// The one fixture shape this suite uses: a world with the four spawnable species that reach
	// this family's rows, plus one cop for the census-factory case.
	struct FSpeciesFixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumNpc* Runner = nullptr;
		FElysiumNpc* Animal = nullptr;
		FElysiumNpc* Andrei = nullptr;
		FElysiumNpc* Cop = nullptr;
		FElysiumNpcZombie* Zombie = nullptr;
		FElysiumNpcNewscaster* Newscaster = nullptr;
		FElysiumNpcMingXiaoTentacle* Tentacle = nullptr;
		FElysiumNpcTzimisceRunner* TypedRunner = nullptr;
		FElysiumNpcTzimisceHeadClaw* HeadClaw = nullptr;
		FElysiumNpcTzimisce* Tzimisce = nullptr;
		FElysiumNpcWerewolf* Werewolf = nullptr;
		FElysiumNpcGargoyle* Gargoyle = nullptr;
		FElysiumNpcYukie* Yukie = nullptr;
		FElysiumNpcBach* Bach = nullptr;
		FElysiumNpcChangBros* Chang = nullptr;
		FElysiumNpcAndreiBlood* TypedAndrei = nullptr;

		FSpeciesFixture()
			: World(Build())
		{
			Runner = World.Npc(TEXT("runner"));
			Animal = World.Npc(TEXT("animal"));
			Andrei = World.Npc(TEXT("andrei"));
			Cop = World.Npc(TEXT("cop"));
			Zombie = World.NpcAs<FElysiumNpcZombie>(TEXT("zombie"));
			Newscaster = World.NpcAs<FElysiumNpcNewscaster>(TEXT("newscaster"));
			Tentacle = World.NpcAs<FElysiumNpcMingXiaoTentacle>(TEXT("tentacle"));
			TypedRunner = World.NpcAs<FElysiumNpcTzimisceRunner>(TEXT("runner"));
			HeadClaw = World.NpcAs<FElysiumNpcTzimisceHeadClaw>(TEXT("headclaw"));
			Tzimisce = World.NpcAs<FElysiumNpcTzimisce>(TEXT("tzimisce"));
			Werewolf = World.NpcAs<FElysiumNpcWerewolf>(TEXT("werewolf"));
			Gargoyle = World.NpcAs<FElysiumNpcGargoyle>(TEXT("gargoyle"));
			Yukie = World.NpcAs<FElysiumNpcYukie>(TEXT("yukie"));
			Bach = World.NpcAs<FElysiumNpcBach>(TEXT("bach"));
			Chang = World.NpcAs<FElysiumNpcChangBros>(TEXT("chang"));
			TypedAndrei = World.NpcAs<FElysiumNpcAndreiBlood>(TEXT("andrei"));
			FElysiumNpcWorldFixture::Quiet({ Runner, Animal, Andrei, Cop, Zombie, Newscaster, Tentacle,
				HeadClaw, Tzimisce, Werewolf, Gargoyle, Yukie, Bach, Chang });
		}

		static FElysiumNpcWorldBuilder Build()
		{
			FElysiumNpcWorldBuilder Builder(TEXT("species"), 29131u);
			Builder.AddNpc(TEXT("runner"), FVector::ZeroVector, TEXT("npc_VTzimisceRunner"));
			Builder.AddNpc(TEXT("animal"), FVector(200.0, 0.0, 0.0), TEXT("npc_VAnimal"));
			Builder.AddNpc(TEXT("andrei"), FVector(400.0, 0.0, 0.0), TEXT("npc_VAndreiBlood"));
			Builder.AddNpc(TEXT("cop"), FVector(600.0, 0.0, 0.0), TEXT("npc_VCop"));
			Builder.AddNpc(TEXT("zombie"), FVector(800.0, 0.0, 0.0), TEXT("npc_VZombie"));
			Builder.AddNpcOfClass(TEXT("newscaster"), FVector(1000.0, 0.0, 0.0), TEXT("CNPC_VNewscaster"));
			Builder.AddNpcOfClass(TEXT("tentacle"), FVector(1200.0, 0.0, 0.0), TEXT("CNPC_VMingXiaoTentacle"));
			Builder.AddNpcOfClass(TEXT("headclaw"), FVector(1400.0, 0.0, 0.0), TEXT("CNPC_VTzimisceHeadClaw"));
			Builder.AddNpcOfClass(TEXT("tzimisce"), FVector(1600.0, 0.0, 0.0), TEXT("CNPC_VTzimisce"));
			Builder.AddNpcOfClass(TEXT("werewolf"), FVector(1800.0, 0.0, 0.0), TEXT("CNPC_VWerewolf"));
			Builder.AddNpcOfClass(TEXT("gargoyle"), FVector(2000.0, 0.0, 0.0), TEXT("CNPC_VGargoyle"));
			Builder.AddNpcOfClass(TEXT("yukie"), FVector(2200.0, 0.0, 0.0), TEXT("CNPC_VYukie"));
			Builder.AddNpcOfClass(TEXT("bach"), FVector(2400.0, 0.0, 0.0), TEXT("CNPC_VBach"));
			Builder.AddNpcOfClass(TEXT("chang"), FVector(2600.0, 0.0, 0.0), TEXT("CNPC_VChangBros"));
			return Builder;
		}
	};

	// The WIRING cases' fixture — the two sides of one vtable dispatch.
	//
	// `Species` is spawned as the census class the case names, through the classname retail's
	// factory builds it from (`CNPC_VZombie` from `npc_VZombie`, …), so its `RetailClass()` is its
	// C++ type and nothing re-labels it. A case that names no class (`nullptr`) stands the plain
	// `npc_VHumanCombatant` it always stood and drives only `Runner` / `Animal` — the carriers of
	// `CNPC_VTzimisceRunner`'s 588/599/600/601/602 and `CNPC_VAnimal`'s 482.
	//
	// `Troika` is a bare `CAI_BaseNPCTroika` (`AddTroikaNpc`): the Troika line with no species
	// class over it, exactly "a plain Troika NPC with no species class". It was an `npc_VCop` while
	// the census gave that classname no class; the cop's factory builds `CNPC_VCop`, so a spawned
	// cop is no longer the Troika line. The cases still call their local for it `Cop`.
	struct FSpeciesWiringFixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumNpc* Species = nullptr;
		FElysiumNpc* Troika = nullptr;
		FElysiumNpc* Runner = nullptr;
		FElysiumNpc* Animal = nullptr;

		explicit FSpeciesWiringFixture(const TCHAR* RetailClassName)
			: World(Build(RetailClassName))
		{
			Species = World.Npc(TEXT("species"));
			Troika = World.Npc(TEXT("troika"));
			Runner = World.Npc(TEXT("runner"));
			Animal = World.Npc(TEXT("animal"));
			FElysiumNpcWorldFixture::Quiet({ Species, Troika, Runner, Animal });
		}

		static FElysiumNpcWorldBuilder Build(const TCHAR* RetailClassName)
		{
			FElysiumNpcWorldBuilder Builder(TEXT("species_wiring"), 29135u);
			if (RetailClassName != nullptr)
			{
				Builder.AddNpcOfClass(TEXT("species"), FVector::ZeroVector, RetailClassName);
			}
			else
			{
				Builder.AddNpc(TEXT("species"), FVector::ZeroVector, TEXT("npc_VHumanCombatant"));
			}
			Builder.AddTroikaNpc(TEXT("troika"), FVector(200.0, 0.0, 0.0));
			Builder.AddNpc(TEXT("runner"), FVector(400.0, 0.0, 0.0), TEXT("npc_VTzimisceRunner"));
			Builder.AddNpc(TEXT("animal"), FVector(600.0, 0.0, 0.0), TEXT("npc_VAnimal"));
			return Builder;
		}
	};

	// How many times the NPC asked to speak the VSound concept `Concept` since the last
	// `VSoundSpeakCalls.Reset()`. Story **29d** (family Sounds10) ported the slot 488 and 506
	// Troika-line bodies (`0x10293ec0`, `0x10294e70`), so the arm that ran is no longer told by an
	// `elysium.stubs` tally but by the concept the body actually asked for — which is the stronger
	// question, because a tally could only say that *some* stub fired.
	int32 SpeciesSpeaksConcept(const FElysiumNpc& Npc, const TCHAR* Concept)
	{
		int32 Count = 0;
		for (const FElysiumNpc::FVSoundSpeak& Row : Npc.VSoundSpeakCalls)
		{
			if (Row.Concept != nullptr && FCString::Strcmp(Row.Concept, Concept) == 0)
			{
				++Count;
			}
		}
		return Count;
	}

	// How many times a stubbed surface whose name contains `Needle` has fired since the last
	// `ElysiumStub::ClearTally()`.
	int32 SpeciesStubFires(const TCHAR* Needle)
	{
		TArray<ElysiumStub::FTally> Tally;
		ElysiumStub::CollectTally(Tally);
		int32 Count = 0;
		for (const ElysiumStub::FTally& Row : Tally)
		{
			if (Row.Surface.Contains(Needle))
			{
				Count += Row.Count;
			}
		}
		return Count;
	}
}

// -------------------------------------------------------------------------------------------------
// The species slot table — every row by name, against the census.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesSlotTableTest,
	"Elysium.Substrate.NpcKernelSpecies.SlotTable", GElysiumNpcKernelSpeciesFlags)
bool FElysiumNpcKernelSpeciesSlotTableTest::RunTest(const FString&)
{
	// The species slot table (`FElysiumNpc::SpeciesSlotRows`) is retired with story 5 fold A4: its
	// last two rows, `CNPCMaker_Fleshpile`'s 139 and 617, are that class's overrides
	// (`FElysiumNpcMakerFleshpile::DeathNotice` / `MakeNPC`). The census still names every body the
	// tree now dispatches, and names the inherited ones.
	const FElysiumNpcClass* Fleshpile = ElysiumNpcTestCensus::Find(TEXT("CNPCMaker_Fleshpile"));
	if (!TestNotNull(TEXT("CNPCMaker_Fleshpile is a census class"), Fleshpile))
	{
		return false;
	}
	TestEqual(TEXT("CNPCMaker_Fleshpile fills slot 139 with 0x1034c8e0"),
		FString(ElysiumNpcTestCensus::BodyOf(Fleshpile, 139)), FString(TEXT("0x1034c8e0")));
	TestEqual(TEXT("and slot 617 with 0x1034c2d0"),
		FString(ElysiumNpcTestCensus::BodyOf(Fleshpile, 617)), FString(TEXT("0x1034c2d0")));
	TestEqual(TEXT("CNPCMaker_Fleshpile inherits CNPCMaker's slot 618"),
		FString(ElysiumNpcTestCensus::BodyOf(Fleshpile, 618)), FString(TEXT("0x1034b580")));
	TestEqual(TEXT("CNPC_VCameraSecurity inherits CNPC_VCamera's slot 497"),
		FString(ElysiumNpcTestCensus::BodyOf(ElysiumNpcTestCensus::Find(TEXT("CNPC_VCameraSecurity")), 497)),
		FString(TEXT("0x103681d0")));
	TestEqual(TEXT("CNPC_VDog inherits CNPC_VAnimal's slot 482"),
		FString(ElysiumNpcTestCensus::BodyOf(ElysiumNpcTestCensus::Find(TEXT("CNPC_VDog")), 482)),
		FString(TEXT("0x1035fd40")));
	TestEqual(TEXT("CNPC_VFrenzyShadow's slot 599 is 0x10376b70"),
		FString(ElysiumNpcTestCensus::BodyOf(ElysiumNpcTestCensus::Find(TEXT("CNPC_VFrenzyShadow")), 599)),
		FString(TEXT("0x10376b70")));
	return true;
}

// -------------------------------------------------------------------------------------------------
// The classname -> class map, read from retail's factories.
// -------------------------------------------------------------------------------------------------
//
// Every NPC classname has exactly one `LINK_ENTITY_TO_CLASS` factory in `vampire.dll`, and the class
// it builds is the last primary-vtable write at `[this]` (docs/vtmb/npc-ai/population.md, "The
// classname → class map, read from the factories"). One class per classname, no tie-break. This
// case was `CensusFallThrough` while the port read the map off the ledger's proximity census, which
// gave `npc_VCop` no class and made the runner a "most-derived claimant" of `CNPC_VBaseBoss`'s
// over-claim; both were wrong, and the case now pins the factories' answers.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesCensusFactoriesTest,
	"Elysium.Substrate.NpcKernelSpecies.CensusFactories", GElysiumNpcKernelSpeciesFlags)
bool FElysiumNpcKernelSpeciesCensusFactoriesTest::RunTest(const FString&)
{
	FSpeciesFixture Fixture;
	TestNotNull(TEXT("npc_VTzimisceRunner is a registered spawn leaf"), Fixture.Runner);
	TestNotNull(TEXT("npc_VAnimal is a registered spawn leaf"), Fixture.Animal);
	TestNotNull(TEXT("npc_VAndreiBlood is a registered spawn leaf"), Fixture.Andrei);
	TestNotNull(TEXT("npc_VCop is a registered spawn leaf"), Fixture.Cop);
	if (Fixture.Runner == nullptr || Fixture.Cop == nullptr)
	{
		return false;
	}

	// A spawned runner is the one class its factory builds. `CNPC_VBaseBoss` claims no classname —
	// no factory builds it (population.md: "No factory builds `CAI_BaseNPC`, `CAI_BaseNPCTroika`,
	// `CNPC_VBaseBoss` or `CAI_TestHull` by classname").
	const FElysiumNpcClass* RunnerClass = Fixture.Runner->RetailClass();
	TestNotNull(TEXT("a spawned runner has a census class"), RunnerClass);
	if (RunnerClass != nullptr)
	{
		TestEqual(TEXT("and it is CNPC_VTzimisceRunner, the class its factory builds"),
			FString(RunnerClass->Name), FString(TEXT("CNPC_VTzimisceRunner")));
	}
	const FElysiumNpcClass* BaseBoss = ElysiumNpcTestCensus::Find(TEXT("CNPC_VBaseBoss"));
	if (TestNotNull(TEXT("the census carries CNPC_VBaseBoss"), BaseBoss))
	{
		TestEqual(TEXT("and CNPC_VBaseBoss claims no classname"), BaseBoss->ClassnameCount, 0);
	}

	// `npc_VCop`'s factory `0x103704f0` (ctor `0x103708e0`, size `0x6674`) builds `CNPC_VCop`, so a
	// spawned cop answers `CNPC_VCop` and every per-species lookup is `CNPC_VCop`'s own — not the
	// Troika line the proximity census used to leave it on.
	const FElysiumNpcClass* CopClass = Fixture.Cop->RetailClass();
	if (TestNotNull(TEXT("a spawned cop has a census class"), CopClass))
	{
		TestEqual(TEXT("and it is CNPC_VCop, the class factory 0x103704f0 builds"),
			FString(CopClass->Name), FString(TEXT("CNPC_VCop")));
	}
	TestTrue(TEXT("the classname query agrees: npc_VCop is CNPC_VCop"),
		ElysiumNpcTestCensus::OfClassname(TEXT("npc_VCop"))
			== ElysiumNpcTestCensus::Find(TEXT("CNPC_VCop")));
	// `CNPC_VCop`'s slot 599 is the body its chain `CNPC_VCop` -> `CNPC_VHumanCombatant` ->
	// `CNPC_VHuman` inherits (`0x10385ab0`), dispatched by C++ inheritance since the species table
	// retired (story 5 fold A4).

	// `npc_VCamera` is a registered spawn leaf now, answering `CNPC_VCamera`; its own world, so the
	// shared fixture's spawn order (and its RNG draws) stay what the other cases were written on.
	FElysiumNpcWorldBuilder CameraBuilder(TEXT("species_camera"), 29131u);
	CameraBuilder.AddNpc(TEXT("camera"), FVector::ZeroVector, TEXT("npc_VCamera"));
	FElysiumNpcWorldFixture CameraWorld(MoveTemp(CameraBuilder));
	FElysiumNpc* Camera = CameraWorld.Npc(TEXT("camera"));
	FElysiumNpcWorldFixture::Quiet({ Camera });
	if (TestNotNull(TEXT("npc_VCamera is a registered spawn leaf"), Camera))
	{
		TestEqual(TEXT("and a spawned camera is CNPC_VCamera"),
			FString(Camera->RetailClass() != nullptr ? Camera->RetailClass()->Name : TEXT("")),
			FString(TEXT("CNPC_VCamera")));
	}
	TestNotNull(TEXT("the census claims it"),
		ElysiumNpcTestCensus::OfClassname(TEXT("npc_VCamera")));
	TestEqual(TEXT("and its census slot-497 body is the camera's own 0x103681d0"),
		FString(ElysiumNpcTestCensus::BodyOf(ElysiumNpcTestCensus::Find(TEXT("CNPC_VCamera")), 497)),
		FString(TEXT("0x103681d0")));
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 323 — `0x10344dd0`'s four bands.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesSlot323Test,
	"Elysium.Substrate.NpcKernelSpecies.Slot323MoveDirection", GElysiumNpcKernelSpeciesFlags)
bool FElysiumNpcKernelSpeciesSlot323Test::RunTest(const FString&)
{
	FSpeciesFixture Fixture;
	FElysiumNpc* Npc = Fixture.Runner;
	if (Npc == nullptr)
	{
		return false;
	}
	Npc->Angles = FVector::ZeroVector;   // facing +X, Source yaw 0

	// The port's Y is the negated Source one, so a Source-relative yaw of `a` is a port delta of
	// `(cos a, -sin a)`. `Direction` builds that.
	const auto Direction = [](float SourceYawDegrees)
	{
		const float Radians = FMath::DegreesToRadians(SourceYawDegrees);
		return FVector(FMath::Cos(Radians), -FMath::Sin(Radians), 0.0) * 100.0;
	};

	// The four bands, read out of `.rdata`: `>316 or <=45 -> 2`, `<=135 -> 3`, `(135,225] -> 0`,
	// `>225 -> 1`.
	TestEqual(TEXT("straight ahead is 2"), Npc->Slot323(Direction(0.f)), 2);
	TestEqual(TEXT("44 degrees is still 2"), Npc->Slot323(Direction(44.f)), 2);
	TestEqual(TEXT("46 degrees is 3"), Npc->Slot323(Direction(46.f)), 3);
	TestEqual(TEXT("90 degrees is 3"), Npc->Slot323(Direction(90.f)), 3);
	TestEqual(TEXT("134 degrees is still 3"), Npc->Slot323(Direction(134.f)), 3);
	TestEqual(TEXT("136 degrees is 0"), Npc->Slot323(Direction(136.f)), 0);
	TestEqual(TEXT("180 degrees is 0"), Npc->Slot323(Direction(180.f)), 0);
	TestEqual(TEXT("224 degrees is still 0"), Npc->Slot323(Direction(224.f)), 0);
	TestEqual(TEXT("226 degrees is 1"), Npc->Slot323(Direction(226.f)), 1);
	TestEqual(TEXT("270 degrees is 1"), Npc->Slot323(Direction(270.f)), 1);

	// **316, not 315.** The band between them is the asymmetry retail carries and the one number a
	// clean quarter split would get wrong.
	TestEqual(TEXT("310 degrees is still 1 — the 1 band runs to 316"),
		Npc->Slot323(Direction(310.f)), 1);
	TestEqual(TEXT("318 degrees is 2"), Npc->Slot323(Direction(318.f)), 2);

	// The bands are RELATIVE to the body's own facing: turning the NPC turns every answer.
	Npc->Angles = FVector(0.0, 90.0, 0.0);   // (pitch, yaw, roll) — a SOURCE yaw of 90
	TestEqual(TEXT("with the body turned 90 degrees, world 90 is now straight ahead"),
		Npc->Slot323(Direction(90.f)), 2);
	Npc->Angles = FVector::ZeroVector;

	// The 2-D length gate — `_DAT_1049e028` = 1e-07. A purely vertical direction never clears it.
	TestEqual(TEXT("a direction with no 2-D length answers 0"),
		Npc->Slot323(FVector(0.0, 0.0, 500.0)), 0);
	TestEqual(TEXT("and so does a zero direction"), Npc->Slot323(FVector::ZeroVector), 0);
	// A direction whose 2-D length is tiny but above the epsilon still gets a real answer.
	TestEqual(TEXT("1e-03 of 2-D length clears the 1e-07 gate"),
		Npc->Slot323(FVector(1.0e-03, 0.0, 500.0)), 2);

	// Z is flattened before the yaw, so it changes nothing.
	TestEqual(TEXT("Z does not affect the band"),
		Npc->Slot323(Direction(90.f) + FVector(0.0, 0.0, 9000.0)), 3);
	return true;
}

// -------------------------------------------------------------------------------------------------
// The two `CUtlVector<{EHANDLE, expiry}>` stores.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesBlacklistsTest,
	"Elysium.Substrate.NpcKernelSpecies.Blacklists", GElysiumNpcKernelSpeciesFlags)
bool FElysiumNpcKernelSpeciesBlacklistsTest::RunTest(const FString&)
{
	FSpeciesFixture Fixture;
	FElysiumNpcTzimisce* Boss = Fixture.Tzimisce;
	FElysiumNpc* A = Fixture.Animal;
	FElysiumNpc* B = Fixture.Andrei;
	if (Boss == nullptr || A == nullptr || B == nullptr)
	{
		return false;
	}
	const double Now = Fixture.World.World.NowSeconds();

	// `0x103662d0` — the add appends with `curtime + param_2`.
	Boss->FUN_103662d0(A->Handle, 20.f);
	Boss->FUN_103662d0(B->Handle, 5.f);
	TestEqual(TEXT("two rows on CNPC_VBaseBoss's blacklist"), Boss->BossBlacklist.Num(), 2);
	TestEqual(TEXT("the first expires at curtime + 20"), Boss->BossBlacklist[0].ExpiresAt,
		Now + 20.0, 1.0e-06);
	TestEqual(TEXT("the second at curtime + 5"), Boss->BossBlacklist[1].ExpiresAt,
		Now + 5.0, 1.0e-06);

	// `0x10366490` — index-of by RESOLVED pointer, -1 for a miss.
	TestEqual(TEXT("index-of finds the first"), Boss->FUN_10366490(A), 0);
	TestEqual(TEXT("index-of finds the second"), Boss->FUN_10366490(B), 1);
	TestEqual(TEXT("index-of answers -1 for an entity that is not on it"),
		Boss->FUN_10366490(Fixture.Cop), INDEX_NONE);

	// `0x10366400` — an unexpired row answers true and is left standing; a MISS answers false.
	TestTrue(TEXT("an unexpired row still blacklists"), Boss->FUN_10366400(A));
	TestEqual(TEXT("and nothing was removed"), Boss->BossBlacklist.Num(), 2);
	TestFalse(TEXT("an entity not on the list is not blacklisted"),
		Boss->FUN_10366400(Fixture.Cop));

	// Expire the second row and take it out. The removal is a SWAP-REMOVE: the LAST row moves into
	// the hole, so the surviving row's index can change.
	Boss->BossBlacklist[1].ExpiresAt = Now - 1.0;
	TestFalse(TEXT("an expired row answers false"), Boss->FUN_10366400(B));
	TestEqual(TEXT("and is removed"), Boss->BossBlacklist.Num(), 1);
	TestEqual(TEXT("leaving the unexpired one"), Boss->FUN_10366490(A), 0);

	// Swap-remove from the FRONT with two rows behind it: the last row lands at index 0.
	Boss->BossBlacklist.Reset();
	Boss->FUN_103662d0(A->Handle, -1.f);          // already expired
	Boss->FUN_103662d0(B->Handle, 20.f);
	TestFalse(TEXT("the expired front row answers false"), Boss->FUN_10366400(A));
	TestEqual(TEXT("one row survives"), Boss->BossBlacklist.Num(), 1);
	TestEqual(TEXT("and it is the LAST row, now at index 0 — a swap-remove, not a shift"),
		Boss->FUN_10366490(B), 0);

	// `0x103bf200` / `0x103bf330` / `0x103bf3c0` — the same three bodies over `CNPC_VTzimisce`'s own
	// store at `+0x6690`, whose duration is not a parameter but the `.rdata` cell `_DAT_1044eb0c`,
	// read out of the image as **20.0**.
	Boss->FUN_103bf200(A->Handle);
	TestEqual(TEXT("one row on CNPC_VTzimisce's blacklist"), Boss->TzimisceBlacklist.Num(), 1);
	TestEqual(TEXT("expiring at curtime + 20, the hardcoded window"),
		Boss->TzimisceBlacklist[0].ExpiresAt, Now + 20.0, 1.0e-06);
	TestEqual(TEXT("index-of finds it"), Boss->FUN_103bf3c0(A), 0);
	TestTrue(TEXT("and it still blacklists"), Boss->FUN_103bf330(A));
	Boss->TzimisceBlacklist[0].ExpiresAt = Now - 1.0;
	TestFalse(TEXT("once expired it answers false"), Boss->FUN_103bf330(A));
	TestEqual(TEXT("and is removed"), Boss->TzimisceBlacklist.Num(), 0);

	// The two stores are independent — the whole point of a second offset on a second class.
	TestEqual(TEXT("the boss store is untouched by the Tzimisce one"),
		Boss->BossBlacklist.Num(), 1);
	return true;
}

// -------------------------------------------------------------------------------------------------
// The melee quartet — slots 599, 600, 601, 602 per species.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesMeleeQuartetTest,
	"Elysium.Substrate.NpcKernelSpecies.MeleeQuartet", GElysiumNpcKernelSpeciesFlags)
bool FElysiumNpcKernelSpeciesMeleeQuartetTest::RunTest(const FString&)
{
	FSpeciesFixture Fixture;
	FElysiumNpc* Npc = Fixture.Runner;
	FElysiumNpc* Enemy = Fixture.Animal;
	FElysiumNpcTzimisceRunner* Runner = Fixture.TypedRunner;
	FElysiumNpcTzimisceHeadClaw* Claw = Fixture.HeadClaw;
	if (Npc == nullptr || Enemy == nullptr || !TestNotNull(TEXT("the runner"), Runner)
		|| !TestNotNull(TEXT("the head claw"), Claw))
	{
		return false;
	}

	// --- `CNPC_VGargoyle`: every gate dropped, always true ----------------------------------------
	// Gargoyle's are overrides on `FElysiumNpcGargoyle` (story 5 step 3), byte-identical to
	// `CNPC_VFrenzyShadow`'s `0x10376b70` / `0x10376ba0`, which are `FElysiumNpcFrenzyShadow`'s since
	// fold A2 (`Elysium.Substrate.NpcKernelPlayerController.FrenzyShadowMelee`).
	TestEqual(TEXT("CNPC_VGargoyle's slot 599 is 0x10379ef0"),
		FString(ElysiumNpcTestCensus::BodyOf(ElysiumNpcTestCensus::Find(TEXT("CNPC_VGargoyle")), 599)),
		FString(TEXT("0x10379ef0")));
	Fixture.Gargoyle->bInMelee = false;
	Fixture.Gargoyle->MeleeEventFires = 0;
	TestTrue(TEXT("0x10379ef0 (CNPC_VGargoyle 599) always enters melee"), Fixture.Gargoyle->FUN_10379ef0(Enemy));
	TestTrue(TEXT("and sets m_bInMelee"), Fixture.Gargoyle->bInMelee);
	TestEqual(TEXT("having fired the global melee event once"), Fixture.Gargoyle->MeleeEventFires, 1);
	// No leave timer — the recovered difference from the Troika line.
	Fixture.Gargoyle->MeleeMustLeaveTimer = 0.0;
	Fixture.Gargoyle->FUN_10379ef0(Enemy);
	TestEqual(TEXT("and it arms no m_flMeleeMustLeaveTimer"), Fixture.Gargoyle->MeleeMustLeaveTimer, 0.0);

	Fixture.Gargoyle->bInMelee = false;
	Fixture.Gargoyle->MeleeEventFires = 0;
	TestTrue(TEXT("0x10379f20 (CNPC_VGargoyle 600) always accepts"), Fixture.Gargoyle->Slot600(Enemy));
	TestEqual(TEXT("one event"), Fixture.Gargoyle->MeleeEventFires, 1);
	// Already in melee is NOT a refusal — the Troika line's re-entry guard is gone.
	TestTrue(TEXT("and a body already in melee still accepts"), Fixture.Gargoyle->Slot600(Enemy));

	// --- `CNPC_VTzimisceHeadClaw` 599: the coordinator alone ------------------------------------
	// `MeleeCoordinatorAdmits599` is family TroikaHelpers' seam and answers false with no
	// coordinator object, so the body takes its refusal arm — which is retail's own answer for a
	// coordinator with no free slot.
	Claw->bInMelee = true;
	Claw->MeleeEventFires = 0;
	TestFalse(TEXT("0x103c19e0 refuses when the coordinator seam refuses"),
		Claw->FUN_103c19e0(Enemy));
	TestFalse(TEXT("and clears m_bInMelee on the way out"), Claw->bInMelee);
	TestEqual(TEXT("firing no melee event"), Claw->MeleeEventFires, 0);

	// --- `CNPC_VTzimisceRunner` 599: the same, plus the potential-enemy cache -------------------
	Runner->RunnerPotentialEnemy = FElysiumEntityHandle();
	Runner->bInMelee = true;
	TestFalse(TEXT("0x103c3960 refuses on the same seam"), Runner->FUN_103c3960(Enemy));
	TestFalse(TEXT("and clears m_bInMelee"), Runner->bInMelee);
	// **The cache is written on the REFUSING arm too** — the one slot-599 body in the family that
	// reads its argument at all.
	TestTrue(TEXT("but m_hPotentialEnemy was cached anyway"),
		Runner->RunnerPotentialEnemy == Enemy->Handle);
	Runner->FUN_103c3960(nullptr);
	TestFalse(TEXT("and a null argument clears it"), Runner->RunnerPotentialEnemy.IsSet());

	// --- slot 600's head-claw / runner pair: the event fires BEFORE the decision ----------------
	Claw->bInMelee = false;
	Claw->MeleeEventFires = 0;
	TestFalse(TEXT("0x103c1a60 refuses on the coordinator seam"), Claw->Slot600(Enemy));
	TestEqual(TEXT("but the global melee event fired anyway — it is unconditional and first"),
		Claw->MeleeEventFires, 1);
	// An NPC ALREADY in melee skips the request and is taken back OUT — the guard is on the way in.
	Claw->bInMelee = true;
	TestFalse(TEXT("and a body already in melee answers false"), Claw->Slot600(Enemy));
	TestFalse(TEXT("and is cleared, because the clear is outside the guard"), Claw->bInMelee);

	Runner->RunnerPotentialEnemy = FElysiumEntityHandle();
	Runner->MeleeEventFires = 0;
	Runner->bInMelee = false;
	TestFalse(TEXT("0x103c39e0 refuses the same way"), Runner->Slot600(Enemy));
	TestEqual(TEXT("with the event fired first"), Runner->MeleeEventFires, 1);
	TestTrue(TEXT("and m_hPotentialEnemy cached"), Runner->RunnerPotentialEnemy == Enemy->Handle);

	// --- `CNPC_VYukie` 600: not a melee body at all ---------------------------------------------
	// `ActiveWeaponCapabilityWord()` is family Motor's seam and answers 0, so the `0x18000` gate is
	// closed and the whole body refuses without writing anything.
	Fixture.Yukie->bInMelee = false;
	Fixture.Yukie->MeleeMustLeaveTimer = 0.0;
	Fixture.Yukie->MeleeEventFires = 0;
	TestFalse(TEXT("0x103dd900 refuses without the weapon capability bits"),
		Fixture.Yukie->Slot600(Enemy));
	TestFalse(TEXT("writing no latch"), Fixture.Yukie->bInMelee);
	TestEqual(TEXT("no flee window"), Fixture.Yukie->MeleeMustLeaveTimer, 0.0);
	TestEqual(TEXT("and no event"), Fixture.Yukie->MeleeEventFires, 0);

	// --- slot 601: the release pair --------------------------------------------------------------
	Claw->bInMelee = true;
	Claw->MeleeEventFires = 0;
	Claw->MeleeCoordinatorReleases = 0;
	Claw->Slot601(Enemy);
	TestFalse(TEXT("0x103c1ad0 leaves melee"), Claw->bInMelee);
	TestEqual(TEXT("firing the event"), Claw->MeleeEventFires, 1);
	TestEqual(TEXT("and releasing the coordinator slot UNGUARDED"),
		Claw->MeleeCoordinatorReleases, 1);

	Runner->bInMelee = true;
	Runner->RunnerPotentialEnemy = Enemy->Handle;
	Runner->MeleeCoordinatorReleases = 0;
	Runner->Slot601(Enemy);
	TestFalse(TEXT("0x103c3a70 leaves melee"), Runner->bInMelee);
	TestFalse(TEXT("and CLEARS m_hPotentialEnemy — the runner's matched set"),
		Runner->RunnerPotentialEnemy.IsSet());
	TestEqual(TEXT("still releasing the slot"), Runner->MeleeCoordinatorReleases, 1);

	// --- slot 602: the far arm only ---------------------------------------------------------------
	// `MeleeRangeUnits()` answers `debug_melee_advance_combatmove_dist`'s 100, so the doubled range
	// is 200, and `MeleeCoordinatorHasRoom()` answers false — so past 200 units the first arm wins.
	Claw->ScheduleHost.EnemyDistUnits = 500.f;
	Runner->ScheduleHost.EnemyDistUnits = 500.f;
	TestTrue(TEXT("0x103c1b10 leaves melee when out of double range and the coordinator is full"),
		Claw->FUN_103c1b10());
	TestTrue(TEXT("0x103c3ab0 is the byte-identical twin"), Runner->Slot602());
	// At zero distance the first arm's `0 < 0` fails and the body falls through to
	// `MeleeCoordinatorHoldsMe()`, which answers true for a coordinator that holds nobody.
	Claw->ScheduleHost.EnemyDistUnits = 0.f;
	TestTrue(TEXT("and at zero distance it falls through to 'the coordinator does not hold me'"),
		Claw->FUN_103c1b10());

	// A spawned runner's slots ARE its bodies: `FElysiumNpcTzimisceRunner` overrides all four.
	Runner->bInMelee = true;
	Runner->RunnerPotentialEnemy = Enemy->Handle;
	TestFalse(TEXT("a runner's slot 599 is the refusing coordinator arm"), Runner->Slot599(0));
	TestFalse(TEXT("which cleared m_bInMelee"), Runner->bInMelee);
	Runner->bInMelee = true;
	Runner->RunnerPotentialEnemy = Enemy->Handle;
	Runner->Slot601(Enemy);
	TestFalse(TEXT("a runner's slot 601 forgets m_hPotentialEnemy"), Runner->RunnerPotentialEnemy.IsSet());
	Runner->ScheduleHost.EnemyDistUnits = 0.f;
	TestTrue(TEXT("and its slot 602 falls through to 'the coordinator does not hold me'"), Runner->Slot602());
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 606 — Bach's arm-then-fire hysteresis. Slot 609 — the three state gates.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesBachGatesTest,
	"Elysium.Substrate.NpcKernelSpecies.BachGates", GElysiumNpcKernelSpeciesFlags)
bool FElysiumNpcKernelSpeciesBachGatesTest::RunTest(const FString&)
{
	FSpeciesFixture Fixture;
	FElysiumNpcBach* Npc = Fixture.Bach;
	if (Npc == nullptr)
	{
		return false;
	}

	// The bodies are called directly on a runner here; `WiredSlot606` / `WiredSlot609` drive them
	// through `FElysiumNpcBach`'s overrides on a spawned `npc_VBach`.
	const FElysiumNpcClass* BachClass = ElysiumNpcTestCensus::Find(TEXT("CNPC_VBach"));
	TestEqual(TEXT("CNPC_VBach's slot 606 is 0x10364280"),
		FString(ElysiumNpcTestCensus::BodyOf(BachClass, 606)), FString(TEXT("0x10364280")));

	// --- `0x10364280`: without the condition the flag is CLEARED and 0 is answered ---------------
	Npc->bBachFireOccluded = true;
	Npc->Cognition.Conditions.Clear(EElysiumNpcCond::EnemyOccluded);
	TestEqual(TEXT("no COND_ENEMY_OCCLUDED answers 0"), Npc->Slot606(0), 0);
	TestFalse(TEXT("and clears m_bFireOccluded"), Npc->bBachFireOccluded);

	// --- with the condition, the FIRST pass only arms -------------------------------------------
	Npc->Cognition.Conditions.Set(EElysiumNpcCond::EnemyOccluded);
	TestEqual(TEXT("the first pass with the condition still answers 0"), Npc->Slot606(0), 0);
	TestTrue(TEXT("but arms m_bFireOccluded"), Npc->bBachFireOccluded);

	// --- the SECOND pass delegates to the Troika body --------------------------------------------
	// `FElysiumNpc::Slot606` is family TroikaHelpers'; the gate's job is to reach it.
	const int32 Base = Npc->FElysiumNpc::Slot606(0);
	TestEqual(TEXT("the second pass delegates to the base slot 606"), Npc->Slot606(0), Base);
	TestTrue(TEXT("and leaves the flag armed"), Npc->bBachFireOccluded);

	// --- the hysteresis is RE-PAID every time the condition drops --------------------------------
	Npc->Cognition.Conditions.Clear(EElysiumNpcCond::EnemyOccluded);
	TestEqual(TEXT("dropping the condition answers 0"), Npc->Slot606(0), 0);
	TestFalse(TEXT("and disarms"), Npc->bBachFireOccluded);
	Npc->Cognition.Conditions.Set(EElysiumNpcCond::EnemyOccluded);
	TestEqual(TEXT("so the next sighting arms again rather than firing"), Npc->Slot606(0), 0);

	// --- slot 609: the state gate ----------------------------------------------------------------
	// Only retail states 4 (`NPC_STATE_SCRIPT`) and 0xc admit the base hint search; every other
	// state ZEROES `m_pShootAtHintNode` as a side effect of asking.
	TestEqual(TEXT("CNPC_VBach's slot 609 is 0x103661f0"),
		FString(ElysiumNpcTestCensus::BodyOf(BachClass, 609)), FString(TEXT("0x103661f0")));
	// Retail's byte-identical `CNPC_VBatSwarm` / `CNPC_VSheriffSwarm` copies are on classes with no
	// instance and carry no port row (0019 story 5 step 1).
	Npc->ScheduleHost.ShootAtHintNode = 77;
	Npc->BeginScriptedSchedule(FElysiumScriptedScheduleOrder(), true, EElysiumNpcState::Combat);
	TestFalse(TEXT("a combat body does not reach the base hint search"), Npc->FUN_103661f0(false));
	TestEqual(TEXT("and its cached shoot-at hint is zeroed"), Npc->ScheduleHost.ShootAtHintNode, 0);

	Npc->ScheduleHost.ShootAtHintNode = 77;
	Npc->BeginScriptedSchedule(FElysiumScriptedScheduleOrder(), true, EElysiumNpcState::Scripted);
	TestTrue(TEXT("a scripted body (retail state 4) does reach it"), Npc->FUN_103661f0(false));
	TestEqual(TEXT("and its cached hint is left alone"), Npc->ScheduleHost.ShootAtHintNode, 77);
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 482 — four standalone species copies, one SCRIPT-state tail the base does not have.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesCanPlaySequenceTest,
	"Elysium.Substrate.NpcKernelSpecies.CanPlaySequence", GElysiumNpcKernelSpeciesFlags)
bool FElysiumNpcKernelSpeciesCanPlaySequenceTest::RunTest(const FString&)
{
	FSpeciesFixture Fixture;
	FElysiumNpcAnimal* Animal = ElysiumTestAsSpecies<FElysiumNpcAnimal>(Fixture.Animal);
	if (Animal == nullptr)
	{
		return false;
	}

	// The census agrees the classes carry their own copy. Every copy is a STANDALONE body (callees:
	// `0x101a8ac0` direct, slot 158 virtual — never the base `0x10278090`), with the base's head and
	// a different tail: a body in retail state 4 (SCRIPT) keeps its 1-or-2 where the base answers 0
	// (`0x1035fdf9` `SETNZ/DEC/AND`). Story 5 step 3 corrected the port, which called the base.
	TestEqual(TEXT("CNPC_VAnimal's slot 482 is 0x1035fd40"),
		FString(ElysiumNpcTestCensus::BodyOf(
			ElysiumNpcTestCensus::Find(TEXT("CNPC_VAnimal")), 482)),
		FString(TEXT("0x1035fd40")));
	TestEqual(TEXT("CNPC_VTzimisce's is 0x103bd270"),
		FString(ElysiumNpcTestCensus::BodyOf(
			ElysiumNpcTestCensus::Find(TEXT("CNPC_VTzimisce")), 482)),
		FString(TEXT("0x103bd270")));

	// The state gate, from the decompiled C: refuse when the caller did not disregard state, the
	// body is neither NONE (0) nor IDLE (1), its ideal is not IDLE, and it is not an ALERT (3) body
	// asked with an interrupt level of at least 1.
	Animal->BeginScriptedSchedule(FElysiumScriptedScheduleOrder(), true, EElysiumNpcState::Combat);
	TestEqual(TEXT("a combat body refuses a sequence"), Animal->CanPlaySequence(false, 0), 0);
	FElysiumNpcTzimisce* Tzim = Fixture.Tzimisce;
	if (!TestNotNull(TEXT("the Tzimisce spawned"), Tzim))
	{
		return false;
	}
	Tzim->BeginScriptedSchedule(FElysiumScriptedScheduleOrder(), true, EElysiumNpcState::Combat);
	TestEqual(TEXT("and the Tzimisce copy answers the same"), Tzim->CanPlaySequence(false, 0), 0);
	TestEqual(TEXT("and so does the base"), Animal->FElysiumNpcBase::CanPlaySequence(false, 0), 0);

	TestEqual(TEXT("disregarding state admits it"), Animal->CanPlaySequence(true, 0), 1);
	Animal->BeginScriptedSchedule(FElysiumScriptedScheduleOrder(), true, EElysiumNpcState::Idle);
	TestEqual(TEXT("an idle body admits it"), Animal->CanPlaySequence(false, 0), 1);
	Animal->BeginScriptedSchedule(FElysiumScriptedScheduleOrder(), true, EElysiumNpcState::Alert);
	TestEqual(TEXT("an alert body refuses at interrupt level 0"), Animal->CanPlaySequence(false, 0), 0);
	TestEqual(TEXT("but admits at level 1"), Animal->CanPlaySequence(false, 1), 1);

	// **The one difference: retail state 4 (SCRIPT).** The species copies keep the answer; the base
	// refuses. Through the slot, a spawned `npc_VAnimal` runs its own copy.
	Animal->BeginScriptedSchedule(FElysiumScriptedScheduleOrder(), true, EElysiumNpcState::Scripted);
	TestEqual(TEXT("a SCRIPT-state animal's copy admits a sequence"), Animal->CanPlaySequence(false, 0), 1);
	Tzim->BeginScriptedSchedule(FElysiumScriptedScheduleOrder(), true, EElysiumNpcState::Scripted);
	TestEqual(TEXT("and so does the Tzimisce copy"), Tzim->CanPlaySequence(false, 0), 1);
	TestEqual(TEXT("where the base refuses it"), Animal->FElysiumNpcBase::CanPlaySequence(false, 0), 0);
	TestEqual(TEXT("and the slot answers the animal's own copy"), Animal->CanPlaySequence(false, 0), 1);
	return true;
}

// -------------------------------------------------------------------------------------------------
// `CNPC_VTzimisce`'s carry chain.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesTzimisceCarryTest,
	"Elysium.Substrate.NpcKernelSpecies.TzimisceCarry", GElysiumNpcKernelSpeciesFlags)
bool FElysiumNpcKernelSpeciesTzimisceCarryTest::RunTest(const FString&)
{
	FSpeciesFixture Fixture;
	FElysiumNpcTzimisce* Npc = Fixture.Tzimisce;
	FElysiumNpcTzimisceHeadClaw* Claw = Fixture.HeadClaw;
	FElysiumNpc* Body = Fixture.Animal;
	if (Npc == nullptr || Body == nullptr || !TestNotNull(TEXT("the head claw"), Claw))
	{
		return false;
	}
	const double Now = Fixture.World.World.NowSeconds();

	// --- `0x103be0b0`: the CARRYING_BODY latch ---------------------------------------------------
	Npc->NpcFlags.Clear(EElysiumNpcFlag::CARRYING_BODY);
	Npc->TzimisceBodyTimer = 0.0;
	Npc->bTzimisceDidFakeThrow = true;
	Npc->FUN_103be0b0(/*bCarrying=*/true);
	TestTrue(TEXT("picking a body up raises CARRYING_BODY"),
		Npc->NpcFlags.Has(EElysiumNpcFlag::CARRYING_BODY));
	TestFalse(TEXT("and clears m_bDidFakeThrow"), Npc->bTzimisceDidFakeThrow);
	// `curtime + RandomFloat(7.5, 10.0)` — the window is the assertion, not the draw.
	TestTrue(TEXT("arming m_flBodyTimer at curtime + [7.5, 10]"),
		Npc->TzimisceBodyTimer >= Now + 7.5 && Npc->TzimisceBodyTimer <= Now + 10.0);

	// --- `0x103be150`: at-or-before, not strictly before -----------------------------------------
	TestFalse(TEXT("a freshly armed timer has not elapsed"), Npc->FUN_103be150());
	Npc->TzimisceBodyTimer = Now;
	TestTrue(TEXT("a timer armed at exactly curtime reads as already elapsed"),
		Npc->FUN_103be150());

	// --- the clearing arm writes ONLY the flag ---------------------------------------------------
	Npc->TzimisceBodyTimer = Now + 99.0;
	Npc->bTzimisceDidFakeThrow = true;
	Npc->FUN_103be0b0(/*bCarrying=*/false);
	TestFalse(TEXT("dropping a body clears CARRYING_BODY"),
		Npc->NpcFlags.Has(EElysiumNpcFlag::CARRYING_BODY));
	TestEqual(TEXT("and leaves m_flBodyTimer standing"), Npc->TzimisceBodyTimer, Now + 99.0);
	TestTrue(TEXT("and leaves m_bDidFakeThrow standing"), Npc->bTzimisceDidFakeThrow);

	// --- `0x103be3d0`: the grab-bone search ------------------------------------------------------
	// `RagdollBonePosition` is family Bosses' seam and answers false, so no bone is ever offered and
	// the body takes its "nothing found" arm — retail's own answer for a target that is not a
	// ragdoll, which is also what the missing cast arm answers.
	Npc->TzimiscePickupGrabBone = 42;
	Npc->PickupTargetPos = FVector(1.0, 2.0, 3.0);
	TestFalse(TEXT("0x103be3d0 finds no bone while the ragdoll seam refuses"),
		Npc->FUN_103be3d0(Body));
	TestEqual(TEXT("and writes neither the position"), Npc->PickupTargetPos, FVector(1.0, 2.0, 3.0));
	TestEqual(TEXT("nor the bone index"), Npc->TzimiscePickupGrabBone, 42);

	// --- `0x103be8e0`: the +-20 degree cone, NOT a distance test ---------------------------------
	// `param_1 == NULL` answers TRUE — "close enough" with nothing to aim at.
	TestTrue(TEXT("a null target answers true"), Npc->FUN_103be8e0(nullptr));

	// --- `0x103bef20`: the 160-unit attach gate --------------------------------------------------
	// The range test runs BEFORE the link is created, so a target past 160 units refuses without
	// touching the animlink seam at all.
	Npc->Origin = FVector::ZeroVector;
	Body->Origin = FVector(400.0 * ElysiumMove::U, 0.0, 0.0);   // 400 units, well past 160
	Npc->TzimiscePhysicsAnimlink = FElysiumEntityHandle();
	TestFalse(TEXT("0x103bef20 refuses a target past 160 units"), Npc->FUN_103bef20(Body, 0));
	TestFalse(TEXT("and stores no animlink"), Npc->TzimiscePhysicsAnimlink.IsSet());
	TestFalse(TEXT("and does not raise CARRYING_BODY"),
		Npc->NpcFlags.Has(EElysiumNpcFlag::CARRYING_BODY));
	// Inside 160 units it gets as far as the `phys_animlink` seam, which answers an invalid handle
	// — retail's own "CreateNoSpawn failed" arm, which also returns false without a write.
	Body->Origin = FVector(100.0 * ElysiumMove::U, 0.0, 0.0);
	TestFalse(TEXT("and inside 160 units it refuses on the phys_animlink seam instead"),
		Npc->FUN_103bef20(Body, 0));
	TestFalse(TEXT("still storing no animlink"), Npc->TzimiscePhysicsAnimlink.IsSet());
	TestFalse(TEXT("a null target refuses outright"), Npc->FUN_103bef20(nullptr, 0));

	// --- `0x103bea90`: the release clears both words and drops CARRYING_BODY ---------------------
	Npc->NpcFlags.Set(EElysiumNpcFlag::CARRYING_BODY);
	Npc->PickupTarget = Body->Handle;
	Npc->TzimiscePhysicsAnimlink = Body->Handle;
	Npc->FUN_103bea90(nullptr);
	TestFalse(TEXT("0x103bea90 clears m_hPhysicsAnimlink"), Npc->TzimiscePhysicsAnimlink.IsSet());
	TestFalse(TEXT("and m_hPickupTarget, with or without something to throw at"),
		Npc->PickupTarget.IsSet());
	TestFalse(TEXT("and clears CARRYING_BODY through 0x103be0b0(false)"),
		Npc->NpcFlags.Has(EElysiumNpcFlag::CARRYING_BODY));

	// The throw arm runs when there IS an aim target, and still ends on the same three writes.
	Npc->NpcFlags.Set(EElysiumNpcFlag::CARRYING_BODY);
	Npc->PickupTarget = Body->Handle;
	Npc->FUN_103bea90(Body);
	TestFalse(TEXT("the throw arm clears m_hPickupTarget too"), Npc->PickupTarget.IsSet());
	TestFalse(TEXT("and CARRYING_BODY"),
		Npc->NpcFlags.Has(EElysiumNpcFlag::CARRYING_BODY));

	// --- `0x103c24a0`: strictly greater, against ZERO and not against curtime --------------------
	Claw->HeadClawSlowedExpire = 0.0;
	TestFalse(TEXT("an unarmed slow window answers false"), Claw->FUN_103c24a0());
	Claw->HeadClawSlowedExpire = 1.0;
	TestTrue(TEXT("an armed one answers true"), Claw->FUN_103c24a0());
	// **And it keeps answering true long after the stamp is in the past** — the predicate is
	// `0.0 < m_flSlowedExpire`, never `curtime < m_flSlowedExpire`, so only a write back to zero
	// clears it.
	Fixture.World.Advance(Now + 100.0);
	TestTrue(TEXT("and still answers true 100 seconds past the stamp"), Claw->FUN_103c24a0());
	return true;
}

// -------------------------------------------------------------------------------------------------
// `CNPC_VWerewolf` — the door pair and the frame-memoised chase point.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesWerewolfTest,
	"Elysium.Substrate.NpcKernelSpecies.Werewolf", GElysiumNpcKernelSpeciesFlags)
bool FElysiumNpcKernelSpeciesWerewolfTest::RunTest(const FString&)
{
	FSpeciesFixture Fixture;
	FElysiumNpcWerewolf* Npc = Fixture.Werewolf;
	FElysiumNpc* DoorA = Fixture.Animal;
	FElysiumNpc* DoorB = Fixture.Andrei;
	if (Npc == nullptr || DoorA == nullptr || DoorB == nullptr)
	{
		return false;
	}

	// --- `0x103d1e50`: the two short circuits ----------------------------------------------------
	Npc->WerewolfDoorState = 2;
	TestTrue(TEXT("door state 2 answers true without measuring"), Npc->FUN_103d1e50());
	Npc->WerewolfDoorState = 0;
	TestFalse(TEXT("door state 0 answers false without measuring"), Npc->FUN_103d1e50());

	// --- the measurement ---------------------------------------------------------------------------
	// The distance is the octagonal approximation — largest axis plus 0.25 of the other two summed —
	// and the threshold is 135 units. The answer is the NEGATION: true means FURTHER than 135.
	Npc->WerewolfDoorState = 1;
	Npc->WerewolfRotDoor1 = DoorA->Handle;
	Npc->WerewolfRotDoor2 = DoorB->Handle;
	DoorA->Origin = FVector::ZeroVector;
	DoorB->Origin = FVector(100.0 * ElysiumMove::U, 0.0, 0.0);
	// dx=100, dy=dz=0 -> 100 + 0.25*0 = 100 <= 135 -> false
	TestFalse(TEXT("100 units apart is at or under 135"), Npc->FUN_103d1e50());
	DoorB->Origin = FVector(140.0 * ElysiumMove::U, 0.0, 0.0);
	TestTrue(TEXT("140 units apart is over it"), Npc->FUN_103d1e50());
	// The weighting is what separates this from a plain max: dx=120, dy=120, dz=0 gives
	// 120 + 0.25 * (0 + 120) = 150, over the threshold, where a Chebyshev distance would be 120.
	DoorB->Origin = FVector(120.0 * ElysiumMove::U, 120.0 * ElysiumMove::U, 0.0);
	TestTrue(TEXT("120/120/0 weighs 150 — the minor axes count for a quarter each"),
		Npc->FUN_103d1e50());
	// And an unresolvable half refuses.
	Npc->WerewolfRotDoor2 = FElysiumEntityHandle();
	TestFalse(TEXT("a door half that does not resolve answers false"), Npc->FUN_103d1e50());

	// --- `0x103d9c90`: the chase cache ------------------------------------------------------------
	// `EngineFrameNumber()` answers INDEX_NONE — the named decision — so the stamp never matches and
	// the position is recomputed on every call, which is retail's behaviour at retail's call rate.
	Npc->BaseMemory.Enemy = DoorA->Handle;
	DoorA->Origin = FVector(300.0 * ElysiumMove::U, 0.0, 0.0);
	FVector Out = FVector::ZeroVector;
	Npc->FUN_103d9c90(Out);
	TestEqual(TEXT("the chase point is the enemy's origin in SOURCE units"), Out,
		FVector(300.0, 0.0, 0.0));
	TestEqual(TEXT("and the cache holds it"), Npc->WerewolfChasePosUnits, FVector(300.0, 0.0, 0.0));
	// Moving the enemy and asking again recomputes, because the stamp never matches.
	DoorA->Origin = FVector(500.0 * ElysiumMove::U, 0.0, 0.0);
	Npc->FUN_103d9c90(Out);
	TestEqual(TEXT("moving the enemy moves the answer — the cache is always stale"), Out,
		FVector(500.0, 0.0, 0.0));
	// **With no enemy the stamp is still written and the PREVIOUS point is what is answered** — the
	// retail detail the walk leaves out.
	Npc->BaseMemory.Enemy = FElysiumEntityHandle();
	Npc->FUN_103d9c90(Out);
	TestEqual(TEXT("with no enemy the last cached point is answered, not a zero"), Out,
		FVector(500.0, 0.0, 0.0));
	return true;
}

// -------------------------------------------------------------------------------------------------
// `CNPC_VZombie` — the AI-type reroll, the two output slots and the float-sound gate.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesZombieTest,
	"Elysium.Substrate.NpcKernelSpecies.Zombie", GElysiumNpcKernelSpeciesFlags)
bool FElysiumNpcKernelSpeciesZombieTest::RunTest(const FString&)
{
	FSpeciesFixture Fixture;
	// `0x103e0980` is `CNPC_VZombie::SetZombieAIType`: asked on a real zombie (story 5 step 4; it
	// had run on a Tzimisce runner).
	FElysiumNpcZombie* Zombie = Fixture.Zombie;
	FElysiumNpc* Npc = Zombie;
	FElysiumNpc* Victim = Fixture.Animal;
	if (Npc == nullptr || Victim == nullptr || !TestNotNull(TEXT("the zombie spawned"), Zombie))
	{
		return false;
	}

	// --- `0x103e0980`: 4 is "pick one for me" and is NEVER stored --------------------------------
	for (int32 Trial = 0; Trial < 8; ++Trial)
	{
		Npc->FUN_103e0980(4);
		TestTrue(TEXT("ZombieAIType 4 is rerolled into 1..3 and never stored"),
			Npc->ZombieAiType >= 1 && Npc->ZombieAiType <= 3);
	}
	// Every other value is stored verbatim.
	for (int32 Type : { 0, 1, 2, 3, 5, 6, 7 })
	{
		Npc->FUN_103e0980(Type);
		TestEqual(*FString::Printf(TEXT("ZombieAIType %d is stored as itself"), Type),
			Npc->ZombieAiType, Type);
	}
	// The side effect fires for 2, 3, 5 and 6 — NOT a contiguous band, and 1 and 4 are the two that
	// do not. `m_bForceStateChange` is the one word of the push this substrate carries.
	for (int32 Type : { 2, 3, 5, 6 })
	{
		FSpeciesFixture Fresh;
		if (Fresh.Zombie == nullptr)
		{
			continue;
		}
		Fresh.Zombie->FUN_103e0980(Type);
		TestTrue(*FString::Printf(TEXT("ZombieAIType %d pushes a scripted order"), Type),
			Fresh.Zombie->GetMind().IsStateChangeForced());
	}
	for (int32 Type : { 0, 1, 7 })
	{
		FSpeciesFixture Fresh;
		if (Fresh.Zombie == nullptr)
		{
			continue;
		}
		Fresh.Zombie->FUN_103e0980(Type);
		TestFalse(*FString::Printf(TEXT("ZombieAIType %d does not"), Type),
			Fresh.Zombie->GetMind().IsStateChangeForced());
	}

	// --- slots 25 and 26: the same output from two vtable entries, no base forward ---------------
	const FElysiumNpcClass* ZombieClass = ElysiumNpcTestCensus::Find(TEXT("CNPC_VZombie"));
	TestEqual(TEXT("CNPC_VZombie's slot 25 is 0x103e12c0"),
		FString(ElysiumNpcTestCensus::BodyOf(ZombieClass, 25)), FString(TEXT("0x103e12c0")));
	TestEqual(TEXT("and slot 26 is 0x103e12f0"),
		FString(ElysiumNpcTestCensus::BodyOf(ZombieClass, 26)), FString(TEXT("0x103e12f0")));
	// Both fire `m_OnAttackedVictim`; the wiring is what a mapper sees, so the counter is the read.
	{
		FElysiumNpcWorldBuilder Builder(TEXT("zombie"), 29132u);
		Builder.AddNpc(TEXT("zombie"), FVector::ZeroVector, TEXT("npc_VZombie"));
		Builder.AddCounter(TEXT("victims"));
		Builder.WireOutput(TEXT("zombie"), TEXT("OnAttackedVictim"), TEXT("victims"));
		FElysiumNpcWorldFixture World(MoveTemp(Builder));
		FElysiumNpcZombie* Wired = World.NpcAs<FElysiumNpcZombie>(TEXT("zombie"));
		FElysiumNpcWorldFixture::Quiet({ Wired });
		if (TestNotNull(TEXT("the wired zombie spawned"), Wired))
		{
			Wired->FUN_103e12c0(Victim);
			World.World.Tick(World.World.NowSeconds() + 0.1);
			TestEqual(TEXT("slot 25 fires OnAttackedVictim"), World.Counter(TEXT("victims")), 1.f);
			Wired->Slot26(Victim);
			World.World.Tick(World.World.NowSeconds() + 0.1);
			TestEqual(TEXT("and slot 26 fires the SAME output"),
				World.Counter(TEXT("victims")), 2.f);
		}
	}

	// --- slot 510: the frequency write happens FIRST and on every call ---------------------------
	Zombie->FloatSoundFrequency = 0;
	ElysiumMiscFlags::Set(Zombie->MiscFlags, 0x1u);   // unconscious — the second gate
	TestFalse(TEXT("an unconscious zombie plays no float sound"), Zombie->ShouldPlayFloatSound());
	TestEqual(TEXT("but m_iFloatSoundFrequency was set to 9 before any gate ran"),
		Zombie->FloatSoundFrequency, 9);

	Zombie->FloatSoundFrequency = 0;
	ElysiumMiscFlags::Clear(Zombie->MiscFlags, 0x1u);
	Zombie->NpcFlags.Set(EElysiumNpcFlag::SLEEPING);
	TestFalse(TEXT("a SLEEPING zombie plays none either"), Zombie->ShouldPlayFloatSound());
	TestEqual(TEXT("and the frequency is still written"), Zombie->FloatSoundFrequency, 9);
	Zombie->NpcFlags.Clear(EElysiumNpcFlag::SLEEPING);

	// With no closest player it refuses; with one, past `Float_Sound_Info` row 3 (250 Source units)
	// it refuses too. The full gate order and the direct base tail are `WiredSlot510…`'s.
	Zombie->Senses.Memory.ClosestPlayer = FElysiumEntityHandle();
	TestFalse(TEXT("with no closest player it refuses"), Zombie->ShouldPlayFloatSound());
	Zombie->Senses.Memory.ClosestPlayer = Victim->Handle;
	Zombie->Senses.Memory.ClosestPlayerDistanceCm = 251.f * ElysiumMove::U;
	TestFalse(TEXT("and with one it refuses beyond the row-3 distance, 250 units"), Zombie->ShouldPlayFloatSound());
	return true;
}

// -------------------------------------------------------------------------------------------------
// `CNPC_VAndreiBlood`'s runner budget, and the fleshpile maker's two overrides.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesFleshpileTest,
	"Elysium.Substrate.NpcKernelSpecies.Fleshpile", GElysiumNpcKernelSpeciesFlags)
bool FElysiumNpcKernelSpeciesFleshpileTest::RunTest(const FString&)
{
	FSpeciesFixture Fixture;
	FElysiumNpcAndreiBlood* Andrei = Fixture.TypedAndrei;
	FElysiumNpcTzimisceRunner* Runner = ElysiumTestAsSpecies<FElysiumNpcTzimisceRunner>(Fixture.Runner);
	if (Andrei == nullptr || Runner == nullptr)
	{
		return false;
	}

	// --- `0x1035e920`: `m_iActiveRunnerCount < 2` ------------------------------------------------
	Andrei->ActiveRunnerCount = 0;
	TestTrue(TEXT("no live runners admits another"), Andrei->FUN_1035e920());
	Andrei->ActiveRunnerCount = 1;
	TestTrue(TEXT("one live runner still admits another"), Andrei->FUN_1035e920());
	Andrei->ActiveRunnerCount = 2;
	TestFalse(TEXT("two is the cap — _DAT_10452dc4 = 2.0"), Andrei->FUN_1035e920());
	Andrei->ActiveRunnerCount = 3;
	TestFalse(TEXT("and three refuses"), Andrei->FUN_1035e920());

	// --- `0x1035e950`: `m_iHitMax = RandomInt(2, 4)`, INCLUSIVE at both ends ---------------------
	bool bSawTwo = false;
	bool bSawFour = false;
	for (int32 Trial = 0; Trial < 64; ++Trial)
	{
		Andrei->FUN_1035e950();
		TestTrue(TEXT("m_iHitMax is drawn in [2, 4]"),
			Andrei->AndreiHitMax >= 2 && Andrei->AndreiHitMax <= 4);
		bSawTwo = bSawTwo || Andrei->AndreiHitMax == 2;
		bSawFour = bSawFour || Andrei->AndreiHitMax == 4;
	}
	TestTrue(TEXT("both ends of the range are reachable — the draw is inclusive"),
		bSawTwo && bSawFour);

	// --- the fleshpile maker's two overrides (story 5 fold A4: its own class) ------------------------
	{
		FElysiumNpcWorldBuilder Builder(TEXT("fleshpile"), 29133u);
		FElysiumEntityDef& Maker = Builder.AddEntity(TEXT("npc_maker_fleshpile"), TEXT("maker"));
		// `CNPCMaker_Fleshpile::Spawn` (`0x1034c020`) dispatches slot 104 `Precache`, whose
		// missing-model arm `UTIL_Remove`s the maker, so the maker authors a model.
		Maker.Keys.Add(TEXT("model"), TEXT("models/fleshpile.mdl"));
		Maker.Keys.Add(TEXT("NPCType"), TEXT("npc_VTzimisceRunner"));
		Maker.Keys.Add(TEXT("MaxNPCCount"), TEXT("10"));
		Maker.Keys.Add(TEXT("Flag_StartDisabled"), TEXT("1"));
		Builder.AddNpc(TEXT("owner"), FVector(900.0, 0.0, 0.0), TEXT("npc_VAndreiBlood"));
		Builder.AddNpc(TEXT("child"), FVector(950.0, 0.0, 0.0), TEXT("npc_VTzimisceRunner"));
		FElysiumNpcWorldFixture World(MoveTemp(Builder));

		FElysiumEntity* MakerEnt = World.World.FindByName(TEXT("maker"));
		TestNotNull(TEXT("npc_maker_fleshpile stands"), MakerEnt);
		FElysiumNpc* MakerNpc = MakerEnt != nullptr ? MakerEnt->AsNpc() : nullptr;
		FElysiumNpcMakerFleshpile* Fleshpile = MakerNpc != nullptr
			? MakerNpc->AsSpecies<FElysiumNpcMakerFleshpile>() : nullptr;
		FElysiumNpc* Owner = World.Npc(TEXT("owner"));
		FElysiumNpc* Child = World.Npc(TEXT("child"));
		FElysiumNpcWorldFixture::Quiet({ Owner, Child });
		if (!TestNotNull(TEXT("and it is CNPCMaker_Fleshpile, an NPC of its own class"), Fleshpile)
			|| Owner == nullptr || Child == nullptr)
		{
			return false;
		}
		TestTrue(TEXT("whose singleton owner is the one npc_VAndreiBlood in the level"),
			static_cast<FElysiumNpc*>(Fleshpile->FleshpileOwner()) == Owner);

		// `0x1034c2d0`, dispatched through slot 617 on a `CNPCMaker` reference: the budget gate.
		FElysiumNpcMaker& AsMaker = *Fleshpile;
		Owner->ActiveRunnerCount = 2;
		TestNull(TEXT("0x1034c2d0 refuses once Andrei has two live runners"), AsMaker.MakeNPC(/*bBypass=*/false));
		TestEqual(TEXT("on the budget"), FString(FElysiumNpcMaker::AttemptName(Fleshpile->LastAttempt)),
			FString(TEXT("live-limit")));
		TestEqual(TEXT("and the count is untouched"), Owner->ActiveRunnerCount, 2);

		// **The bypass flag skips the WHOLE admission, blood budget included.**
		const int32 LiveBefore = Fleshpile->LiveChildren;
		const int32 TotalBefore = Fleshpile->RemainingTotal;
		FElysiumNpc* Made = AsMaker.MakeNPC(/*bBypass=*/true);
		TestNotNull(TEXT("but the bypass arm spawns anyway"), Made);
		TestEqual(TEXT("and increments the live-runner count by one"), Owner->ActiveRunnerCount, 3);
		// Retail correction (story 5 fold A4): `0x1034c2d0` writes none of the base's counters and no
		// `m_bCameFromSpawner`; the port used to run the base body behind its admission.
		TestEqual(TEXT("m_cLiveChildren is not touched by the fleshpile body"), Fleshpile->LiveChildren, LiveBefore);
		TestEqual(TEXT("nor m_iMaxNumNPCs"), Fleshpile->RemainingTotal, TotalBefore);
		if (Made != nullptr)
		{
			TestFalse(TEXT("nor the child's m_bCameFromSpawner"), Made->bCameFromSpawner);
			TestTrue(TEXT("the child is owned by the maker"), Made->GetOwnerEntity() == Fleshpile->Handle);
			TestEqual(TEXT("and takes the maker's model through SetModel"), Made->Model,
				FString(TEXT("models/fleshpile.mdl")));
			TestEqual(TEXT("with spawnflags ORed with 4"), Made->SpawnFlags & 0x4, 0x4);
		}
	}

	// With NO npc_VAndreiBlood in the level at all, the non-bypass arm refuses outright.
	{
		FElysiumNpcWorldBuilder Builder(TEXT("fleshpile_no_owner"), 29135u);
		FElysiumEntityDef& Maker = Builder.AddEntity(TEXT("npc_maker_fleshpile"), TEXT("maker"));
		Maker.Keys.Add(TEXT("model"), TEXT("models/fleshpile.mdl"));
		Maker.Keys.Add(TEXT("NPCType"), TEXT("npc_VTzimisceRunner"));
		Maker.Keys.Add(TEXT("MaxNPCCount"), TEXT("10"));
		Maker.Keys.Add(TEXT("Flag_StartDisabled"), TEXT("1"));
		FElysiumNpcWorldFixture World(MoveTemp(Builder));
		FElysiumEntity* MakerEnt = World.World.FindByName(TEXT("maker"));
		FElysiumNpc* MakerNpc = MakerEnt != nullptr ? MakerEnt->AsNpc() : nullptr;
		FElysiumNpcMakerFleshpile* Fleshpile = MakerNpc != nullptr
			? MakerNpc->AsSpecies<FElysiumNpcMakerFleshpile>() : nullptr;
		if (TestNotNull(TEXT("the ownerless fleshpile maker stands"), Fleshpile))
		{
			TestNull(TEXT("with no npc_VAndreiBlood there is no singleton"), Fleshpile->FleshpileOwner());
			TestNull(TEXT("and 0x1034c2d0 refuses outright"), Fleshpile->MakeNPC(/*bBypass=*/false));
			TestEqual(TEXT("before any other term"), FString(FElysiumNpcMaker::AttemptName(Fleshpile->LastAttempt)),
				FString(TEXT("invalid-child")));
		}
	}

	// `0x1034c8e0`: the death notice's once-only decrement, dispatched through slot 139, with the
	// runner test a TYPED test on the tree (`AsSpecies<FElysiumNpcTzimisceRunner>`).
	{
		FElysiumNpcWorldBuilder Builder(TEXT("fleshpile2"), 29134u);
		FElysiumEntityDef& Maker = Builder.AddEntity(TEXT("npc_maker_fleshpile"), TEXT("maker"));
		Maker.Keys.Add(TEXT("model"), TEXT("models/fleshpile.mdl"));
		Maker.Keys.Add(TEXT("NPCType"), TEXT("npc_VTzimisceRunner"));
		Maker.Keys.Add(TEXT("Flag_StartDisabled"), TEXT("1"));
		Builder.AddNpc(TEXT("owner"), FVector(900.0, 0.0, 0.0), TEXT("npc_VAndreiBlood"));
		Builder.AddNpc(TEXT("child"), FVector(950.0, 0.0, 0.0), TEXT("npc_VTzimisceRunner"));
		Builder.AddNpc(TEXT("bystander"), FVector(980.0, 0.0, 0.0), TEXT("npc_VAnimal"));
		FElysiumNpcWorldFixture World(MoveTemp(Builder));
		FElysiumEntity* MakerEnt = World.World.FindByName(TEXT("maker"));
		FElysiumNpc* MakerNpc = MakerEnt != nullptr ? MakerEnt->AsNpc() : nullptr;
		FElysiumNpcMakerFleshpile* Fleshpile = MakerNpc != nullptr
			? MakerNpc->AsSpecies<FElysiumNpcMakerFleshpile>() : nullptr;
		FElysiumNpc* Owner = World.Npc(TEXT("owner"));
		FElysiumNpc* Child = World.Npc(TEXT("child"));
		FElysiumNpc* Bystander = World.Npc(TEXT("bystander"));
		FElysiumNpcWorldFixture::Quiet({ Owner, Child, Bystander });
		if (Fleshpile == nullptr || Owner == nullptr || Child == nullptr || Bystander == nullptr)
		{
			return false;
		}
		// `0x1034c8e0` reads `DAT_10938040` as it stands and never fills it; the maker's own
		// `MakeNPC` / `OnRestore` are what fill it.
		TestNotNull(TEXT("the maker's lookup fills the Andrei singleton"), Fleshpile->FleshpileOwner());
		Owner->ActiveRunnerCount = 2;
		Owner->AndreiKillCount = 0;

		FElysiumEntity& AsEntity = *Fleshpile;
		AsEntity.DeathNotice(Child);
		TestEqual(TEXT("0x1034c8e0 decrements the live-runner count"), Owner->ActiveRunnerCount, 1);
		TestEqual(TEXT("and counts the kill"), Owner->AndreiKillCount, 1);
		FElysiumNpcTzimisceRunner* ChildRunner = Child->AsSpecies<FElysiumNpcTzimisceRunner>();
		TestTrue(TEXT("marking the runner so it cannot be counted twice"),
			ChildRunner != nullptr && ChildRunner->bRunnerDeathNoticeProcessed);
		// The base `0x1034bc90` ran behind it: a live child REFUNDS the total (0 -> 1).
		TestEqual(TEXT("then CNPCMaker::DeathNotice runs: a live child refunds the total"),
			Fleshpile->RemainingTotal, 1);

		// The once-only flag is what stops a corpse re-notified from being counted again.
		AsEntity.DeathNotice(Child);
		TestEqual(TEXT("a second notice for the same runner changes nothing"), Owner->ActiveRunnerCount, 1);
		TestEqual(TEXT("nor the kill count"), Owner->AndreiKillCount, 1);

		// A child that is not a `CNPC_VTzimisceRunner` fails the typed test and is not counted.
		AsEntity.DeathNotice(Bystander);
		TestEqual(TEXT("and a non-runner child is not counted at all"), Owner->ActiveRunnerCount, 1);
		TestEqual(TEXT("nor is its death a kill"), Owner->AndreiKillCount, 1);
	}
	return true;
}

// -------------------------------------------------------------------------------------------------
// `CNPC_VNewscaster`'s two story queues.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesNewscasterTest,
	"Elysium.Substrate.NpcKernelSpecies.Newscaster", GElysiumNpcKernelSpeciesFlags)
bool FElysiumNpcKernelSpeciesNewscasterTest::RunTest(const FString&)
{
	FSpeciesFixture Fixture;
	FElysiumNpcNewscaster* Npc = Fixture.Newscaster;
	FElysiumNpc* Scene = Fixture.Animal;
	if (Npc == nullptr || Scene == nullptr)
	{
		return false;
	}

	Npc->NewscasterMainStories.Add({ TEXT("main_a") });
	Npc->NewscasterMainStories.Add({ TEXT("main_b") });
	Npc->NewscasterSideStories.Add({ TEXT("side_a") });
	Npc->bNewscasterStoryActive = true;

	// --- `0x103a0ff0`: the "not playing VCD" arm is taken off the DIALOG SCENE handle -------------
	Npc->Dialogue.DialogScene = FElysiumEntityHandle();
	TArray<FString> Lines;
	TestEqual(TEXT("with no scene the listing is one line and advances the cursor by one"),
		Npc->FUN_103a0ff0(10, Lines), 11);
	TestEqual(TEXT("and it is the literal header"), Lines.Num(), 1);
	if (Lines.Num() == 1)
	{
		TestEqual(TEXT("verbatim"), Lines[0], FString(TEXT("not playing VCD")));
	}

	// --- with a scene, both queues are listed under their verbatim headers ------------------------
	Npc->Dialogue.DialogScene = Scene->Handle;
	Lines.Reset();
	const int32 End = Npc->FUN_103a0ff0(0, Lines);
	TestEqual(TEXT("two headers plus three rows is five lines"), Lines.Num(), 5);
	TestEqual(TEXT("and the cursor advanced by five"), End, 5);
	if (Lines.Num() == 5)
	{
		TestEqual(TEXT("the main header carries the count"), Lines[0],
			FString(TEXT("Main Stories (2)")));
		TestEqual(TEXT("the side header carries its own"), Lines[3],
			FString(TEXT("Side Stories (1)")));
	}

	// --- `+0x668c` selects WHICH queue is playing, and the two predicates are opposites ----------
	Npc->NewscasterPlayingSide = 0;
	Npc->NewscasterMainCursor = 1;
	Npc->NewscasterSideCursor = 0;
	Lines.Reset();
	Npc->FUN_103a0ff0(0, Lines);
	if (Lines.Num() == 5)
	{
		TestTrue(TEXT("with +0x668c == 0 the MAIN cursor row is highlighted"),
			Lines[2].StartsWith(TEXT("* ")));
		TestFalse(TEXT("and the side one is not"), Lines[4].StartsWith(TEXT("* ")));
	}
	Npc->NewscasterPlayingSide = 1;
	Lines.Reset();
	Npc->FUN_103a0ff0(0, Lines);
	if (Lines.Num() == 5)
	{
		TestFalse(TEXT("with +0x668c != 0 the main row is not highlighted"),
			Lines[2].StartsWith(TEXT("* ")));
		TestTrue(TEXT("and the SIDE cursor row is"), Lines[4].StartsWith(TEXT("* ")));
	}

	// --- `0x103a0d50`: the teardown drains both queues and clears the active flag LAST ------------
	Npc->FUN_103a0d50();
	TestEqual(TEXT("the main queue is drained"), Npc->NewscasterMainStories.Num(), 0);
	TestEqual(TEXT("the side queue too"), Npc->NewscasterSideStories.Num(), 0);
	TestFalse(TEXT("and the story-active flag is cleared"), Npc->bNewscasterStoryActive);
	return true;
}

// -------------------------------------------------------------------------------------------------
// The remaining one- and two-line species bodies.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesSmallBodiesTest,
	"Elysium.Substrate.NpcKernelSpecies.SmallBodies", GElysiumNpcKernelSpeciesFlags)
bool FElysiumNpcKernelSpeciesSmallBodiesTest::RunTest(const FString&)
{
	FSpeciesFixture Fixture;
	FElysiumNpc* Npc = Fixture.Runner;
	FElysiumNpc* Other = Fixture.Animal;
	if (Npc == nullptr || Other == nullptr)
	{
		return false;
	}

	// `CNPC_Crow`'s slot 197 (`0x103577d0`) and fly step (`0x10357be0`) are on a class no map
	// stands and carry no port body.

	// --- `0x1036c7f0`: the ChangBros setter writes family Squad's word ---------------------------
	Fixture.Chang->ChangType = 0;
	Fixture.Chang->FUN_1036c7f0(3);
	TestEqual(TEXT("0x1036c7f0 writes m_ChangType"), Fixture.Chang->ChangType, 3);
	// It is a PLAIN setter — no clamp, no validation.
	Fixture.Chang->FUN_1036c7f0(-9);
	TestEqual(TEXT("verbatim, with no clamp"), Fixture.Chang->ChangType, -9);

	FElysiumNpcMingXiaoTentacle* Tentacle = Fixture.Tentacle;
	FElysiumNpcTzimisce* Tzim = Fixture.Tzimisce;
	FElysiumNpcTzimisceRunner* Runner = Fixture.TypedRunner;
	if (!TestNotNull(TEXT("the Tzimisce spawned"), Tzim) || !TestNotNull(TEXT("the runner spawned"), Runner))
	{
		return false;
	}
	if (!TestNotNull(TEXT("the tentacle spawned"), Tentacle))
	{
		return false;
	}
	// --- `0x1039ef90`: the tentacle's cached coordinate point and condition 0x78 ------------------
	const EElysiumNpcCond Cond0x78 = static_cast<EElysiumNpcCond>(0x78);
	Tentacle->Cognition.Conditions.Clear(Cond0x78);
	Tentacle->FUN_1039ef90(FVector(7.0, 8.0, 9.0));
	TestTrue(TEXT("0x1039ef90 raises condition 0x78 — unnamed in the recovered vocabulary"),
		Tentacle->Cognition.Conditions.Has(Cond0x78));
	TestEqual(TEXT("and caches the three floats"), Tentacle->TentacleCoordinatePosUnits,
		FVector(7.0, 8.0, 9.0));

	// --- slots 21, 22 and 23: three slots, one body, and the head seam answers null ---------------
	Tentacle->TentacleHeadForwards = 0;
	Tentacle->FUN_1039e800(Other);
	Tentacle->Slot22(Other);
	Tentacle->Slot23(Other);
	TestEqual(TEXT("all three tentacle slots ask for the head"), Tentacle->TentacleHeadForwards, 3);
	TestNull(TEXT("and the seam answers null, so nothing is forwarded"),
		Tentacle->MingXiaoTentacleHead());
	const FElysiumNpcClass* TentacleClass = ElysiumNpcTestCensus::Find(TEXT("CNPC_VMingXiaoTentacle"));
	TestEqual(TEXT("CNPC_VMingXiaoTentacle's slot 21 is 0x1039e800"),
		FString(ElysiumNpcTestCensus::BodyOf(TentacleClass, 21)), FString(TEXT("0x1039e800")));
	TestEqual(TEXT("its slot 22 is 0x1039e830"),
		FString(ElysiumNpcTestCensus::BodyOf(TentacleClass, 22)), FString(TEXT("0x1039e830")));
	TestEqual(TEXT("and its slot 23 is 0x1039e860"),
		FString(ElysiumNpcTestCensus::BodyOf(TentacleClass, 23)), FString(TEXT("0x1039e860")));

	// --- `0x103b9180`, slot 593: the base first, then five literals ------------------------------
	Tzim->TargetLeadMin = 999.f;
	Tzim->TargetLeadMax = 999.f;
	Tzim->TargetLeadCurrentWeight = 999.f;
	Tzim->TargetLeadPredictedWeight = 999.f;
	Tzim->TargetLeadWeightScale = 999.f;
	Tzim->Slot593();
	TestEqual(TEXT("slot 593 writes m_flTargetLeadMin 0.01"), Tzim->TargetLeadMin, 0.01f, 1.0e-06f);
	TestEqual(TEXT("m_flTargetLeadMax 1.0"), Tzim->TargetLeadMax, 1.0f, 1.0e-06f);
	TestEqual(TEXT("m_flTargetLeadCurrentWeight 50"), Tzim->TargetLeadCurrentWeight, 50.f, 1.0e-06f);
	TestEqual(TEXT("m_flTargetLeadPredictedWeight 50"), Tzim->TargetLeadPredictedWeight, 50.f,
		1.0e-06f);
	TestEqual(TEXT("and m_flTargetLeadWeightScale 0.01"), Tzim->TargetLeadWeightScale, 0.01f,
		1.0e-06f);

	// `CScriptedTarget::Spawn` (`0x1034d6e0`) is on a class no map stands and carries no port body.

	// --- `0x103681d0` / `0x103682f0`: two EMPTY bodies, and emptiness is the point ---------------
	// The overrides exist so the base sound hooks do NOT run for a camera (`WiredSlot497` / `506`).
	const FElysiumNpcClass* CameraClass = ElysiumNpcTestCensus::Find(TEXT("CNPC_VCamera"));
	TestEqual(TEXT("CNPC_VCamera's slot 497 is 0x103681d0"),
		FString(ElysiumNpcTestCensus::BodyOf(CameraClass, 497)), FString(TEXT("0x103681d0")));
	TestEqual(TEXT("and its slot 506 is 0x103682f0"),
		FString(ElysiumNpcTestCensus::BodyOf(CameraClass, 506)), FString(TEXT("0x103682f0")));

	// --- `0x103c3fd0`, slot 588: the base's IsActivityFinished gate is GONE ----------------------
	// The restart is unconditional; `RestartIdealActivityId` is family Hints' seam and records it.
	Runner->Slot588();
	TestEqual(TEXT("a runner's slot 588 is 0x103c3fd0"),
		FString(ElysiumNpcTestCensus::BodyOf(Runner->RetailClass(), 588)), FString(TEXT("0x103c3fd0")));
	Runner->Slot588();   // `FElysiumNpcTzimisceRunner::Slot588`, the same body through the slot

	// --- `0x103b92a0`, slot 488: the three voice ConVars, then the event -------------------------
	int32 Argument = -1;
	TestTrue(TEXT("SPI_DIES argument 0 is tzimisce_voice_pitch"),
		Tzim->TzimisceDeathScriptArgument(0, Argument));
	TestEqual(TEXT("shipped 100"), Argument, 100);
	TestTrue(TEXT("argument 1 is tzimisce_voice_attn"), Tzim->TzimisceDeathScriptArgument(1, Argument));
	TestEqual(TEXT("shipped 65"), Argument, 65);
	TestTrue(TEXT("argument 2 is tzimisce_voice_volume"), Tzim->TzimisceDeathScriptArgument(2, Argument));
	TestEqual(TEXT("handed over as 1.0f's dword"), static_cast<uint32>(Argument), 0x3f800000u);
	Tzim->DeathSound();   // fires the recorded event; the tail call is slot 487's

	// --- `0x103bf560`: the motor yaw release ------------------------------------------------------
	Tzim->FUN_103bf560();   // `0x102e1e20(-1)`: RunTask19's MotorUpdateYaw
	return true;
}

// =================================================================================================
// THE WIRING — one case per wired slot, each driving the REAL base body.
// =================================================================================================
//
// Every case below calls the slot the way the kernel calls it (`Slot21`, `CanPlaySequence`,
// `DeathSound`, …), never the dispatcher, and asserts the species side effect on the class that
// carries the override and the Troika side effect on a bare `CAI_BaseNPCTroika` (the local the
// cases call `Cop`, for the `npc_VCop` that stood there before 0019 story 5 step 2 gave that
// classname its factory's class). What the prologue does is the vtable's own resolution, so a case
// that could only assert the dispatcher's answer would be asserting nothing about the wiring.
//
// Since story 5 step 3 the species side is an override on the class's own C++ type, so "which arm
// ran" is the C++ dispatch; the cases assert the arm's own effect. Two slots have NO observable
// difference between their arms in this substrate and say so in place: 497 (both arms are empty —
// retail's camera body is one `ret` and the base's only effect is a global concept cache this
// runtime has no table for) and 588 (`RestartIdealActivityId` is family Hints' seam and reaches
// nothing, so "gated on IsActivityFinished" and "unconditional" write the same nothing). Those two
// assert the census body and drive both arms to termination.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesWiredSlot21Test,
	"Elysium.Substrate.NpcKernelSpecies.WiredSlot21", GElysiumNpcKernelSpeciesFlags)
bool FElysiumNpcKernelSpeciesWiredSlot21Test::RunTest(const FString&)
{
	FSpeciesWiringFixture Fixture(TEXT("CNPC_VMingXiaoTentacle"));
	FElysiumNpcMingXiaoTentacle* Tentacle = ElysiumTestAsSpecies<FElysiumNpcMingXiaoTentacle>(Fixture.Species);
	FElysiumNpc* Cop = Fixture.Troika;
	if (Tentacle == nullptr || Cop == nullptr)
	{
		AddError(TEXT("fixture did not stand both sides"));
		return false;
	}

	// `0x1039e800` forwards to the head and raises NOTHING of its own; `0x1029f800` raises
	// `COND_BEING_ATTACKED`. The condition is the discriminator.
	Tentacle->TentacleHeadForwards = 0;
	Tentacle->Cognition.Conditions.Clear(EElysiumNpcCond::BeingAttacked);
	Tentacle->Slot21(nullptr);
	TestEqual(TEXT("slot 21 on a tentacle asks its head, once"), Tentacle->TentacleHeadForwards, 1);
	TestFalse(TEXT("and raises no COND_BEING_ATTACKED — the base body did not run"),
		Tentacle->Cognition.Conditions.Has(EElysiumNpcCond::BeingAttacked));

	Cop->Cognition.Conditions.Clear(EElysiumNpcCond::BeingAttacked);
	Cop->Slot21(nullptr);
	TestTrue(TEXT("slot 21 on a plain Troika NPC raises COND_BEING_ATTACKED"),
		Cop->Cognition.Conditions.Has(EElysiumNpcCond::BeingAttacked));
	TestNull(TEXT("and it is no tentacle, so it asks no head"),
		Cop->AsSpecies<FElysiumNpcMingXiaoTentacle>());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesWiredSlot22Test,
	"Elysium.Substrate.NpcKernelSpecies.WiredSlot22", GElysiumNpcKernelSpeciesFlags)
bool FElysiumNpcKernelSpeciesWiredSlot22Test::RunTest(const FString&)
{
	FSpeciesWiringFixture Fixture(TEXT("CNPC_VMingXiaoTentacle"));
	FElysiumNpcMingXiaoTentacle* Tentacle = ElysiumTestAsSpecies<FElysiumNpcMingXiaoTentacle>(Fixture.Species);
	FElysiumNpc* Cop = Fixture.Troika;
	if (Tentacle == nullptr || Cop == nullptr)
	{
		AddError(TEXT("fixture did not stand both sides"));
		return false;
	}

	// `0x1039e830`, the second of the three byte-identical forwards.
	Tentacle->TentacleHeadForwards = 0;
	Tentacle->Cognition.Conditions.Clear(EElysiumNpcCond::BeingAttacked);
	Tentacle->Slot22(nullptr);
	TestEqual(TEXT("slot 22 on a tentacle asks its head"), Tentacle->TentacleHeadForwards, 1);
	TestFalse(TEXT("and the base's condition is not raised"),
		Tentacle->Cognition.Conditions.Has(EElysiumNpcCond::BeingAttacked));

	Cop->Cognition.Conditions.Clear(EElysiumNpcCond::BeingAttacked);
	Cop->Slot22(nullptr);
	TestTrue(TEXT("slot 22 on a plain Troika NPC raises COND_BEING_ATTACKED"),
		Cop->Cognition.Conditions.Has(EElysiumNpcCond::BeingAttacked));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesWiredSlot23Test,
	"Elysium.Substrate.NpcKernelSpecies.WiredSlot23", GElysiumNpcKernelSpeciesFlags)
bool FElysiumNpcKernelSpeciesWiredSlot23Test::RunTest(const FString&)
{
	FSpeciesWiringFixture Fixture(TEXT("CNPC_VMingXiaoTentacle"));
	FElysiumNpcMingXiaoTentacle* Tentacle = ElysiumTestAsSpecies<FElysiumNpcMingXiaoTentacle>(Fixture.Species);
	FElysiumNpc* Cop = Fixture.Troika;
	if (Tentacle == nullptr || Cop == nullptr)
	{
		AddError(TEXT("fixture did not stand both sides"));
		return false;
	}

	// `0x1039e860`, the third.
	Tentacle->TentacleHeadForwards = 0;
	Tentacle->Cognition.Conditions.Clear(EElysiumNpcCond::BeingAttacked);
	Tentacle->Slot23(nullptr);
	TestEqual(TEXT("slot 23 on a tentacle asks its head"), Tentacle->TentacleHeadForwards, 1);
	TestFalse(TEXT("and the base's condition is not raised"),
		Tentacle->Cognition.Conditions.Has(EElysiumNpcCond::BeingAttacked));

	Cop->Cognition.Conditions.Clear(EElysiumNpcCond::BeingAttacked);
	Cop->Slot23(nullptr);
	TestTrue(TEXT("slot 23 on a plain Troika NPC raises COND_BEING_ATTACKED"),
		Cop->Cognition.Conditions.Has(EElysiumNpcCond::BeingAttacked));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesWiredSlot25Test,
	"Elysium.Substrate.NpcKernelSpecies.WiredSlot25", GElysiumNpcKernelSpeciesFlags)
bool FElysiumNpcKernelSpeciesWiredSlot25Test::RunTest(const FString&)
{
	// `m_OnAttackedVictim` is what a mapper sees, so the counter is the read — the same wiring the
	// `0x103e12c0` body case uses, driven through the SLOT this time.
	FElysiumNpcWorldBuilder Builder(TEXT("species_wired25"), 29136u);
	Builder.AddNpc(TEXT("zombie"), FVector::ZeroVector, TEXT("npc_VZombie"));
	// The Troika side: a bare `CAI_BaseNPCTroika`, the line with no species class over it.
	Builder.AddTroikaNpc(TEXT("cop"), FVector(200.0, 0.0, 0.0));
	Builder.AddCounter(TEXT("victims"));
	Builder.WireOutput(TEXT("zombie"), TEXT("OnAttackedVictim"), TEXT("victims"));
	Builder.WireOutput(TEXT("cop"), TEXT("OnAttackedVictim"), TEXT("victims"));
	FElysiumNpcWorldFixture World(MoveTemp(Builder));
	FElysiumNpc* Zombie = World.Npc(TEXT("zombie"));
	FElysiumNpc* Cop = World.Npc(TEXT("cop"));
	FElysiumNpcWorldFixture::Quiet({ Zombie, Cop });
	if (Zombie == nullptr || Cop == nullptr)
	{
		AddError(TEXT("fixture did not stand both sides"));
		return false;
	}

	Zombie->Slot25(Cop);
	World.World.Tick(World.World.NowSeconds() + 0.1);
	TestEqual(TEXT("slot 25 on a zombie fires OnAttackedVictim"), World.Counter(TEXT("victims")),
		1.f);

	// `0x100265b0` is ONE byte, a bare `ret`: the Troika arm writes nothing and fires nothing.
	Cop->Slot25(Zombie);
	World.World.Tick(World.World.NowSeconds() + 0.1);
	TestEqual(TEXT("slot 25 on a plain Troika NPC is empty and fires nothing"),
		World.Counter(TEXT("victims")), 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesWiredSlot26Test,
	"Elysium.Substrate.NpcKernelSpecies.WiredSlot26", GElysiumNpcKernelSpeciesFlags)
bool FElysiumNpcKernelSpeciesWiredSlot26Test::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("species_wired26"), 29137u);
	Builder.AddNpc(TEXT("zombie"), FVector::ZeroVector, TEXT("npc_VZombie"));
	// The Troika side: a bare `CAI_BaseNPCTroika`, the line with no species class over it.
	Builder.AddTroikaNpc(TEXT("cop"), FVector(200.0, 0.0, 0.0));
	Builder.AddCounter(TEXT("victims"));
	Builder.WireOutput(TEXT("zombie"), TEXT("OnAttackedVictim"), TEXT("victims"));
	Builder.WireOutput(TEXT("cop"), TEXT("OnAttackedVictim"), TEXT("victims"));
	FElysiumNpcWorldFixture World(MoveTemp(Builder));
	FElysiumNpc* Zombie = World.Npc(TEXT("zombie"));
	FElysiumNpc* Cop = World.Npc(TEXT("cop"));
	FElysiumNpcWorldFixture::Quiet({ Zombie, Cop });
	if (Zombie == nullptr || Cop == nullptr)
	{
		AddError(TEXT("fixture did not stand both sides"));
		return false;
	}

	// `0x103e12f0` — the SAME output from a second vtable entry.
	Zombie->Slot26(Cop);
	World.World.Tick(World.World.NowSeconds() + 0.1);
	TestEqual(TEXT("slot 26 on a zombie fires the same OnAttackedVictim"),
		World.Counter(TEXT("victims")), 1.f);

	Cop->Slot26(Zombie);
	World.World.Tick(World.World.NowSeconds() + 0.1);
	TestEqual(TEXT("slot 26 on a plain Troika NPC is empty"), World.Counter(TEXT("victims")), 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesWiredSlot482Test,
	"Elysium.Substrate.NpcKernelSpecies.WiredSlot482CanPlaySequence",
	GElysiumNpcKernelSpeciesFlags)
bool FElysiumNpcKernelSpeciesWiredSlot482Test::RunTest(const FString&)
{
	FSpeciesWiringFixture Fixture(nullptr);
	FElysiumNpc* Animal = Fixture.Animal;   // `npc_VAnimal`, a REAL spawn leaf carrying 0x1035fd40
	FElysiumNpc* Cop = Fixture.Troika;
	if (Animal == nullptr || Cop == nullptr)
	{
		AddError(TEXT("fixture did not stand both sides"));
		return false;
	}

	// The carrier arrives through the registry: its classname's factory builds its class.
	TestEqual(TEXT("a spawned npc_VAnimal is CNPC_VAnimal"),
		FString(Animal->RetailClass() != nullptr ? Animal->RetailClass()->Name : TEXT("")),
		FString(TEXT("CNPC_VAnimal")));

	// `0x1035fd40` shares the base's head and every state arm but one: retail state 4 (SCRIPT),
	// which the species copy admits and the base refuses. Every arm is driven through the REAL slot.
	Animal->BeginScriptedSchedule(FElysiumScriptedScheduleOrder(), true, EElysiumNpcState::Combat);
	Cop->BeginScriptedSchedule(FElysiumScriptedScheduleOrder(), true, EElysiumNpcState::Combat);
	TestEqual(TEXT("a combat animal refuses a sequence through the slot"),
		Animal->CanPlaySequence(false, 0), 0);
	TestEqual(TEXT("and so does a bare Troika NPC in combat, on the Troika arm"),
		Cop->CanPlaySequence(false, 0), 0);
	TestEqual(TEXT("disregarding state admits it on the species arm"),
		Animal->CanPlaySequence(true, 0), 1);
	TestEqual(TEXT("and on the Troika arm"), Cop->CanPlaySequence(true, 0), 1);

	Animal->BeginScriptedSchedule(FElysiumScriptedScheduleOrder(), true, EElysiumNpcState::Alert);
	TestEqual(TEXT("an alert animal refuses at interrupt level 0"),
		Animal->CanPlaySequence(false, 0), 0);
	TestEqual(TEXT("and admits at level 1"), Animal->CanPlaySequence(false, 1), 1);

	// The discriminator: a SCRIPT-state body. Animal, Zombie (Animal's copy, inherited), a human-line
	// class (`0x103850a0`) and Tzimisce (`0x103bd270`) keep the answer; a Tzimisce runner inherits
	// the BASE (it has no copy of its own) and so does the bare Troika line.
	FElysiumNpcWorldBuilder Builder(TEXT("species_wired482"), 29138u);
	Builder.AddNpc(TEXT("zombie"), FVector::ZeroVector, TEXT("npc_VZombie"));
	Builder.AddNpc(TEXT("human"), FVector(200.0, 0.0, 0.0), TEXT("npc_VHuman"));
	Builder.AddNpc(TEXT("tzimisce"), FVector(400.0, 0.0, 0.0), TEXT("npc_VTzimisce"));
	Builder.AddNpc(TEXT("headclaw"), FVector(600.0, 0.0, 0.0), TEXT("npc_VTzimisceHeadClaw"));
	FElysiumNpcWorldFixture Script(MoveTemp(Builder));
	FElysiumNpcWorldFixture::Quiet({ Script.Npc(TEXT("zombie")), Script.Npc(TEXT("human")),
		Script.Npc(TEXT("tzimisce")), Script.Npc(TEXT("headclaw")) });
	for (FElysiumNpc* Npc : { Animal, Cop, Fixture.Runner, Script.Npc(TEXT("zombie")),
			 Script.Npc(TEXT("human")), Script.Npc(TEXT("tzimisce")), Script.Npc(TEXT("headclaw")) })
	{
		if (Npc != nullptr)
		{
			Npc->BeginScriptedSchedule(FElysiumScriptedScheduleOrder(), true, EElysiumNpcState::Scripted);
		}
	}
	TestEqual(TEXT("a SCRIPT-state animal admits a sequence (0x1035fd40)"), Animal->CanPlaySequence(false, 0), 1);
	for (const TCHAR* Name : { TEXT("zombie"), TEXT("human"), TEXT("tzimisce") })
	{
		FElysiumNpc* Npc = Script.Npc(Name);
		TestTrue(*FString::Printf(TEXT("%s stood"), Name), Npc != nullptr);
		if (Npc != nullptr)
		{
			TestEqual(*FString::Printf(TEXT("a SCRIPT-state %s admits it through its own copy"), Name),
				Npc->CanPlaySequence(false, 0), 1);
		}
	}
	if (FElysiumNpc* HeadClaw = Script.Npc(TEXT("headclaw")))
	{
		TestEqual(TEXT("a SCRIPT-state head claw runs the base and refuses"), HeadClaw->CanPlaySequence(false, 0), 0);
	}
	if (Fixture.Runner != nullptr)
	{
		TestEqual(TEXT("and so does a runner"), Fixture.Runner->CanPlaySequence(false, 0), 0);
	}
	TestEqual(TEXT("and the bare Troika line"), Cop->CanPlaySequence(false, 0), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesWiredSlot488Test,
	"Elysium.Substrate.NpcKernelSpecies.WiredSlot488DeathSound", GElysiumNpcKernelSpeciesFlags)
bool FElysiumNpcKernelSpeciesWiredSlot488Test::RunTest(const FString&)
{
	FSpeciesWiringFixture Fixture(TEXT("CNPC_VTzimisce"));
	FElysiumNpcTzimisce* Tzimisce = ElysiumTestAsSpecies<FElysiumNpcTzimisce>(Fixture.Species);
	FElysiumNpc* Cop = Fixture.Troika;
	if (Tzimisce == nullptr || Cop == nullptr)
	{
		AddError(TEXT("fixture did not stand both sides"));
		return false;
	}

	// The two arms are told apart by what they report: `0x103b92a0` fires `SPI_DIES` at the script
	// host (three singleton seams, all substituting 0 — retail's own arm), and the Troika-line body
	// speaks the VSound concept `"Death"`.
	//
	// This case was written when slot 488's Troika arm was still a generated stub, so its probe for
	// that arm was the `elysium.stubs` tally. Story **29d** (family Sounds10) ported `0x10293ec0`,
	// so the probe is now the body's own output. Same question, answered off the real body: a
	// tally proved only that *a* stub fired, where the speak record proves *which concept* the
	// NPC actually asked for.
	ElysiumStub::ClearTally();
	ON_SCOPE_EXIT { ElysiumStub::ClearTally(); };
	Tzimisce->VSoundSpeakCalls.Reset();
	Cop->VSoundSpeakCalls.Reset();

	Tzimisce->DeathSound();
	TestEqual(TEXT("slot 488 on a Tzimisce fires SPI_DIES"), SpeciesStubFires(TEXT("SPI_DIES")), 1);
	TestEqual(TEXT("and never reaches the base death sound"),
		SpeciesSpeaksConcept(*Tzimisce, TEXT("Death")), 0);

	Cop->DeathSound();
	TestEqual(TEXT("slot 488 on a plain Troika NPC reaches the base, which speaks Death"),
		SpeciesSpeaksConcept(*Cop, TEXT("Death")), 1);
	TestEqual(TEXT("and fires no SPI_DIES"), SpeciesStubFires(TEXT("SPI_DIES")), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesWiredSlot497Test,
	"Elysium.Substrate.NpcKernelSpecies.WiredSlot497", GElysiumNpcKernelSpeciesFlags)
bool FElysiumNpcKernelSpeciesWiredSlot497Test::RunTest(const FString&)
{
	FSpeciesWiringFixture Fixture(TEXT("CNPC_VCamera"));
	FElysiumNpcCamera* Camera = ElysiumTestAsSpecies<FElysiumNpcCamera>(Fixture.Species);
	FElysiumNpc* Cop = Fixture.Troika;
	if (Camera == nullptr || Cop == nullptr)
	{
		AddError(TEXT("fixture did not stand both sides"));
		return false;
	}

	// **Both arms are observationally empty here and that is the row's own fact**: `0x103681d0` is
	// one byte of `ret`, and the Troika body's only effect is a once-only scan of retail's global
	// response-concept table, which this runtime has no table for (the seam writes -1 into a global
	// this substrate does not carry). So the assertion is the census body and that both terminate.
	TestEqual(TEXT("a camera's slot 497 is 0x103681d0"),
		FString(ElysiumNpcTestCensus::BodyOf(Camera->RetailClass(), 497)), FString(TEXT("0x103681d0")));
	TestEqual(TEXT("and CNPC_VCameraSecurity inherits the same body"),
		FString(ElysiumNpcTestCensus::BodyOf(ElysiumNpcTestCensus::Find(TEXT("CNPC_VCameraSecurity")), 497)),
		FString(TEXT("0x103681d0")));
	Camera->Slot497();   // the empty species arm
	Cop->Slot497();      // the once-only base arm
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesWiredSlot506Test,
	"Elysium.Substrate.NpcKernelSpecies.WiredSlot506", GElysiumNpcKernelSpeciesFlags)
bool FElysiumNpcKernelSpeciesWiredSlot506Test::RunTest(const FString&)
{
	FSpeciesWiringFixture Fixture(TEXT("CNPC_VCamera"));
	FElysiumNpcCamera* Camera = ElysiumTestAsSpecies<FElysiumNpcCamera>(Fixture.Species);
	FElysiumNpc* Cop = Fixture.Troika;
	if (Camera == nullptr || Cop == nullptr)
	{
		AddError(TEXT("fixture did not stand both sides"));
		return false;
	}

	// The EMPTINESS of the camera's `0x103682f0` is observable through what the base would have
	// said: `0x10294e70` speaks the concept `"Target_Reacquired"`, so a camera speaks nothing and
	// a cop speaks once.
	//
	// The probe was the `elysium.stubs` tally when this case was written and slot 506's Troika arm
	// was still generated; story **29d** (family Sounds10) ported `0x10294e70`, so it now reads the
	// real body's output instead.
	ElysiumStub::ClearTally();
	ON_SCOPE_EXIT { ElysiumStub::ClearTally(); };
	Camera->VSoundSpeakCalls.Reset();
	Cop->VSoundSpeakCalls.Reset();

	Camera->Slot506();
	TestEqual(TEXT("slot 506 on a camera is empty — the 29d base never runs"),
		SpeciesSpeaksConcept(*Camera, TEXT("Target_Reacquired")), 0);

	Cop->Slot506();
	TestEqual(TEXT("slot 506 on a plain Troika NPC reaches the base"),
		SpeciesSpeaksConcept(*Cop, TEXT("Target_Reacquired")), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesWiredSlot510Test,
	"Elysium.Substrate.NpcKernelSpecies.WiredSlot510ShouldPlayFloatSound",
	GElysiumNpcKernelSpeciesFlags)
bool FElysiumNpcKernelSpeciesWiredSlot510Test::RunTest(const FString&)
{
	FSpeciesWiringFixture Fixture(TEXT("CNPC_VZombie"));
	FElysiumNpcZombie* Zombie = ElysiumTestAsSpecies<FElysiumNpcZombie>(Fixture.Species);
	FElysiumNpc* Cop = Fixture.Troika;
	if (Zombie == nullptr || Cop == nullptr)
	{
		AddError(TEXT("fixture did not stand both sides"));
		return false;
	}

	// `0x103e1080` writes `m_iFloatSoundFrequency = 9` FIRST and on every call, before any gate;
	// the Troika-line body (`0x10294070`) never touches the word at all. `SLEEPING` is a gate BOTH
	// bodies refuse on, so the answers agree and the word is the whole discriminator.
	Zombie->FloatSoundFrequency = 0;
	Zombie->NpcFlags.Set(EElysiumNpcFlag::SLEEPING);
	TestFalse(TEXT("a SLEEPING zombie's slot 510 refuses"), Zombie->ShouldPlayFloatSound());
	TestEqual(TEXT("but wrote m_iFloatSoundFrequency = 9 before that gate ran"),
		Zombie->FloatSoundFrequency, 9);

	Cop->FloatSoundFrequency = 0;
	Cop->NpcFlags.Set(EElysiumNpcFlag::SLEEPING);
	TestFalse(TEXT("a plain Troika NPC's slot 510 refuses on the same gate"),
		Cop->ShouldPlayFloatSound());
	TestEqual(TEXT("and leaves m_iFloatSoundFrequency alone — the species write never happened"),
		Cop->FloatSoundFrequency, 0);
	Zombie->NpcFlags.Clear(EElysiumNpcFlag::SLEEPING);

	// **The direct tail (story 5 step 3 correction).** `0x103e1080` has NO state gate and its
	// accepting arm is a direct call (`0x103e11f9` -> `0x10005f97` -> `0x1027a530`) into the
	// CAI_BaseNPC body, never the Troika override's two IDLE tests. An ALERT zombie inside row 3's
	// 250 units therefore reaches the base roll — ONE draw of the schedule stream — where the
	// Troika body refuses the same ALERT body without drawing.
	FElysiumEntity* Player = Cop;   // any live entity stands for `m_hClosestPlayer`
	Zombie->BeginScriptedSchedule(FElysiumScriptedScheduleOrder(), true, EElysiumNpcState::Alert);
	Zombie->Senses.Memory.ClosestPlayer = Player->Handle;
	Zombie->NextFloatSoundTime = 0.0;
	FRandomStream& Rng = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule);

	Zombie->Senses.Memory.ClosestPlayerDistanceCm = 250.f * ElysiumMove::U;   // inclusive
	int32 Seed = Rng.GetCurrentSeed();
	Zombie->ShouldPlayFloatSound();
	TestNotEqual(TEXT("an ALERT zombie at exactly 250 units reaches the base roll (one draw)"),
		Rng.GetCurrentSeed(), Seed);
	TestEqual(TEXT("with m_iFloatSoundFrequency 9, its own write"), Zombie->FloatSoundFrequency, 9);

	Zombie->Senses.Memory.ClosestPlayerDistanceCm = 250.5f * ElysiumMove::U;
	Seed = Rng.GetCurrentSeed();
	TestFalse(TEXT("past 250 units it refuses"), Zombie->ShouldPlayFloatSound());
	TestEqual(TEXT("without drawing"), Rng.GetCurrentSeed(), Seed);

	// The gates run in retail's order and every refusal is draw-free: the dialog gate first.
	Zombie->Senses.Memory.ClosestPlayerDistanceCm = 100.f * ElysiumMove::U;
	Zombie->Dialogue.bInDialog = true;
	Seed = Rng.GetCurrentSeed();
	Zombie->FloatSoundFrequency = 0;
	TestFalse(TEXT("a zombie in dialog refuses (IsInDialog 0x102c1170)"), Zombie->ShouldPlayFloatSound());
	TestEqual(TEXT("after writing the frequency"), Zombie->FloatSoundFrequency, 9);
	TestEqual(TEXT("and without drawing"), Rng.GetCurrentSeed(), Seed);
	Zombie->Dialogue.bInDialog = false;

	// The Troika body the zombie bypasses refuses the same ALERT body on its IDLE test.
	Seed = Rng.GetCurrentSeed();
	TestFalse(TEXT("the Troika override refuses an ALERT body"), Zombie->FElysiumNpc::ShouldPlayFloatSound());
	TestEqual(TEXT("without drawing"), Rng.GetCurrentSeed(), Seed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesWiredSlot588Test,
	"Elysium.Substrate.NpcKernelSpecies.WiredSlot588", GElysiumNpcKernelSpeciesFlags)
bool FElysiumNpcKernelSpeciesWiredSlot588Test::RunTest(const FString&)
{
	FSpeciesWiringFixture Fixture(nullptr);
	FElysiumNpcTzimisceRunner* Runner = ElysiumTestAsSpecies<FElysiumNpcTzimisceRunner>(Fixture.Runner);   // a REAL `npc_VTzimisceRunner`
	FElysiumNpc* Cop = Fixture.Troika;
	if (Runner == nullptr || Cop == nullptr)
	{
		AddError(TEXT("fixture did not stand both sides"));
		return false;
	}

	// **No observable difference yet, and the reason is named**: both arms end in
	// `RestartIdealActivityId`, family Hints' seam over `0x10289ee0`, which records the id and
	// reaches nothing — so "gated on `IsActivityFinished()`" and `0x103c3fd0`'s "unconditional"
	// write the same nothing. The DECISION is what is asserted, with the census behind it.
	TestEqual(TEXT("a spawned runner is CNPC_VTzimisceRunner"),
		FString(Runner->RetailClass() != nullptr ? Runner->RetailClass()->Name : TEXT("")),
		FString(TEXT("CNPC_VTzimisceRunner")));
	TestEqual(TEXT("and its slot-588 body is 0x103c3fd0, not the base's 0x10293e50"),
		FString(ElysiumNpcTestCensus::BodyOf(Runner->RetailClass(), 588)),
		FString(TEXT("0x103c3fd0")));
	Runner->Slot588();   // the species arm, through the slot
	Cop->Slot588();      // the base arm
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesWiredSlot593Test,
	"Elysium.Substrate.NpcKernelSpecies.WiredSlot593", GElysiumNpcKernelSpeciesFlags)
bool FElysiumNpcKernelSpeciesWiredSlot593Test::RunTest(const FString&)
{
	FSpeciesWiringFixture Fixture(TEXT("CNPC_VTzimisce"));
	FElysiumNpcTzimisce* Tzimisce = ElysiumTestAsSpecies<FElysiumNpcTzimisce>(Fixture.Species);
	FElysiumNpc* Cop = Fixture.Troika;
	if (Tzimisce == nullptr || Cop == nullptr)
	{
		AddError(TEXT("fixture did not stand both sides"));
		return false;
	}

	// `0x103b9180` runs the base (`thunk_FUN_1029a070`) and then OVERWRITES all five words, so
	// `m_flTargetLeadMin` is 0.01 on a Tzimisce and the base's 0.1 on everything else. That the
	// base ran at all is what the direct call is for, and the number proves the order.
	Tzimisce->TargetLeadMin = 999.f;
	Tzimisce->Slot593();
	TestEqual(TEXT("slot 593 on a Tzimisce leaves m_flTargetLeadMin at 0.01"),
		Tzimisce->TargetLeadMin, 0.01f, 1.0e-06f);
	TestEqual(TEXT("with the base's own weights standing under it"),
		Tzimisce->TargetLeadCurrentWeight, 50.f, 1.0e-06f);

	Cop->TargetLeadMin = 999.f;
	Cop->Slot593();
	TestEqual(TEXT("slot 593 on a plain Troika NPC writes the base's 0.1"), Cop->TargetLeadMin,
		0.1f, 1.0e-06f);

	// Destination and order: the Tzimisce body's direct call reaches `FElysiumNpc::Slot593` (the
	// Troika `0x1029a070`) and the five overwrites come AFTER it. The qualified base alone writes
	// the Troika 0.1; the Tzimisce's own slot, run after, leaves its 0.01 — and running the base
	// again afterwards would leave 0.1, so the final 0.01 is only possible with base-then-overwrite.
	Tzimisce->FElysiumNpc::Slot593();
	TestEqual(TEXT("the qualified base on a Tzimisce writes the Troika 0.1"), Tzimisce->TargetLeadMin,
		0.1f, 1.0e-06f);
	Tzimisce->Slot593();
	TestEqual(TEXT("and the Tzimisce's slot overwrites it after calling the base: 0.01"),
		Tzimisce->TargetLeadMin, 0.01f, 1.0e-06f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesWiredSlot599Test,
	"Elysium.Substrate.NpcKernelSpecies.WiredSlot599", GElysiumNpcKernelSpeciesFlags)
bool FElysiumNpcKernelSpeciesWiredSlot599Test::RunTest(const FString&)
{
	FSpeciesWiringFixture Fixture(nullptr);
	FElysiumNpcTzimisceRunner* Runner = ElysiumTestAsSpecies<FElysiumNpcTzimisceRunner>(Fixture.Runner);
	FElysiumNpc* Cop = Fixture.Troika;
	if (Runner == nullptr || Cop == nullptr)
	{
		AddError(TEXT("fixture did not stand both sides"));
		return false;
	}

	// `0x103c3960` drops EVERY gate the Troika body has — frenzy, the can-enter timer, the range and
	// height terms — and goes straight to the coordinator, which refuses. The frenzy bit is the
	// discriminator: the Troika arm enters melee outright on it, the runner's arm never reads it.
	Runner->SetFrenziedWord(0x2);
	Runner->bInMelee = true;
	TestFalse(TEXT("a runner's slot 599 refuses on the coordinator, frenzy bit and all"),
		Runner->Slot599(0));
	TestFalse(TEXT("and clears m_bInMelee, which the Troika arm's first gate would not have"),
		Runner->bInMelee);
	Runner->SetFrenziedWord(0);

	Cop->SetFrenziedWord(0x2);
	Cop->bInMelee = false;
	TestTrue(TEXT("a plain Troika NPC's slot 599 enters melee outright on frenzy bit 0x2"),
		Cop->Slot599(0));
	TestTrue(TEXT("and sets m_bInMelee"), Cop->bInMelee);
	Cop->SetFrenziedWord(0);

	// The ARGUMENT. `signatures.md` types slot 599 `int` because the base ignores it, but the
	// runner's copy caches `m_hPotentialEnemy` from it — and every recovered dispatch site pushes
	// `GetEnemy()`. The prologue hands the species arm this NPC's own enemy, so a live enemy is
	// what lands in the word and a cleared one is what lands when there is none.
	Runner->BaseMemory.Enemy = Cop->Handle;
	Runner->RunnerPotentialEnemy = FElysiumEntityHandle();
	Runner->Slot599(0);
	TestEqual(TEXT("slot 599 hands the species arm GetEnemy(), which it caches"),
		Runner->RunnerPotentialEnemy, Cop->Handle);
	Runner->BaseMemory.Enemy = FElysiumEntityHandle();
	Runner->Slot599(0);
	TestFalse(TEXT("and with no enemy the cache is cleared, retail's null arm"),
		Runner->RunnerPotentialEnemy.IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesWiredSlot600Test,
	"Elysium.Substrate.NpcKernelSpecies.WiredSlot600", GElysiumNpcKernelSpeciesFlags)
bool FElysiumNpcKernelSpeciesWiredSlot600Test::RunTest(const FString&)
{
	FSpeciesWiringFixture Fixture(nullptr);
	FElysiumNpcTzimisceRunner* Runner = ElysiumTestAsSpecies<FElysiumNpcTzimisceRunner>(Fixture.Runner);
	FElysiumNpc* Cop = Fixture.Troika;
	if (Runner == nullptr || Cop == nullptr)
	{
		AddError(TEXT("fixture did not stand both sides"));
		return false;
	}

	// `0x103c39e0` fires the global melee event FIRST and unconditionally; the Troika body fires it
	// only inside the accepting arm, which the weapon-capability seam (answering 0) never reaches.
	const int32 RunnerEvents = Runner->MeleeEventFires;
	Runner->RunnerPotentialEnemy = FElysiumEntityHandle();
	TestFalse(TEXT("a runner's slot 600 still refuses on the coordinator"), Runner->Slot600(Cop));
	TestEqual(TEXT("but the event fired anyway — before anything was decided"),
		Runner->MeleeEventFires, RunnerEvents + 1);
	TestEqual(TEXT("and the argument was cached into m_hPotentialEnemy"),
		Runner->RunnerPotentialEnemy, Cop->Handle);

	const int32 CopEvents = Cop->MeleeEventFires;
	TestFalse(TEXT("a plain Troika NPC's slot 600 refuses with no capability bits"),
		Cop->Slot600(Runner));
	TestEqual(TEXT("and fires no event, because the fire is INSIDE the accepting arm"),
		Cop->MeleeEventFires, CopEvents);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesWiredSlot601Test,
	"Elysium.Substrate.NpcKernelSpecies.WiredSlot601", GElysiumNpcKernelSpeciesFlags)
bool FElysiumNpcKernelSpeciesWiredSlot601Test::RunTest(const FString&)
{
	FSpeciesWiringFixture Fixture(nullptr);
	FElysiumNpcTzimisceRunner* Runner = ElysiumTestAsSpecies<FElysiumNpcTzimisceRunner>(Fixture.Runner);
	FElysiumNpc* Cop = Fixture.Troika;
	if (Runner == nullptr || Cop == nullptr)
	{
		AddError(TEXT("fixture did not stand both sides"));
		return false;
	}

	// `0x103c3a70` releases the coordinator slot UNGUARDED and clears `m_hPotentialEnemy`; the
	// Troika body guards the release on `m_pAttackCoordinator != 0`. With no coordinator object in
	// this substrate that guard is closed, so the release count is the discriminator.
	Runner->AttackCoordinator = 0;
	Runner->bInMelee = true;
	Runner->RunnerPotentialEnemy = Cop->Handle;
	const int32 RunnerReleases = Runner->MeleeCoordinatorReleases;
	Runner->Slot601(Cop);
	TestFalse(TEXT("a runner's slot 601 leaves melee"), Runner->bInMelee);
	TestEqual(TEXT("releasing the coordinator slot with no guard at all"),
		Runner->MeleeCoordinatorReleases, RunnerReleases + 1);
	TestFalse(TEXT("and FORGETS m_hPotentialEnemy — the runner's matched set"),
		Runner->RunnerPotentialEnemy.IsSet());

	Cop->AttackCoordinator = 0;
	Cop->bInMelee = true;
	const int32 CopReleases = Cop->MeleeCoordinatorReleases;
	Cop->Slot601(Runner);
	TestFalse(TEXT("a plain Troika NPC's slot 601 leaves melee too"), Cop->bInMelee);
	TestEqual(TEXT("but releases nothing, because its release is GUARDED"),
		Cop->MeleeCoordinatorReleases, CopReleases);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesWiredSlot602Test,
	"Elysium.Substrate.NpcKernelSpecies.WiredSlot602", GElysiumNpcKernelSpeciesFlags)
bool FElysiumNpcKernelSpeciesWiredSlot602Test::RunTest(const FString&)
{
	FSpeciesWiringFixture Fixture(nullptr);
	FElysiumNpcTzimisceRunner* Runner = ElysiumTestAsSpecies<FElysiumNpcTzimisceRunner>(Fixture.Runner);
	FElysiumNpc* Cop = Fixture.Troika;
	if (Runner == nullptr || Cop == nullptr)
	{
		AddError(TEXT("fixture did not stand both sides"));
		return false;
	}

	// `0x103c3ab0` keeps only the FAR arm: out of double melee range with a full coordinator. The
	// Troika body refuses first on its own null-coordinator test, which is the state this substrate
	// is always in (the named divergence family TroikaHelpers records).
	Runner->AttackCoordinator = 0;
	Runner->ScheduleHost.EnemyDistUnits = 500.f;
	TestTrue(TEXT("a runner's slot 602 leaves melee on the far arm"), Runner->Slot602());

	Cop->AttackCoordinator = 0;
	Cop->ScheduleHost.EnemyDistUnits = 500.f;
	TestFalse(TEXT("a plain Troika NPC's slot 602 refuses on its null-coordinator test"),
		Cop->Slot602());
	return true;
}

// Story 5 step 3 correction (`21bb56cf`; `docs/vtmb/npc-ai/shape.md` § "The melee quartet's species
// replacements"): a spawned `npc_VYukie`
// reaches its own melee quartet through the vtable. Before the step the four bodies were ported but
// no dispatch reached them, so a Yukie ran the Troika line's.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesWiredYukieMeleeTest,
	"Elysium.Substrate.NpcKernelSpecies.WiredYukieMelee", GElysiumNpcKernelSpeciesFlags)
bool FElysiumNpcKernelSpeciesWiredYukieMeleeTest::RunTest(const FString&)
{
	FSpeciesWiringFixture Fixture(TEXT("CNPC_VYukie"));
	FElysiumNpcYukie* Yukie = ElysiumTestAsSpecies<FElysiumNpcYukie>(Fixture.Species);
	FElysiumNpc* Cop = Fixture.Troika;
	if (Yukie == nullptr || Cop == nullptr)
	{
		AddError(TEXT("fixture did not stand both sides"));
		return false;
	}
	const double Now = Fixture.World.World.NowSeconds();

	// 599 `0x103dd8b0`: no gates, and a must-leave window of 22.5-45 s (the Troika line's is 7.5-15).
	Yukie->bInMelee = false;
	Yukie->MeleeEventFires = 0;
	Yukie->AttackCoordinator = 0;
	TestTrue(TEXT("a Yukie's slot 599 enters melee with no coordinator"), Yukie->Slot599(0));
	TestTrue(TEXT("latching m_bInMelee"), Yukie->bInMelee);
	TestEqual(TEXT("firing the melee event once"), Yukie->MeleeEventFires, 1);
	TestTrue(TEXT("with a must-leave window of at least 22.5 s"),
		Yukie->MeleeMustLeaveTimer >= Now + 22.5 && Yukie->MeleeMustLeaveTimer <= Now + 45.0);

	// 600 `0x103dd900`: the weapon-capability seam answers 0, so the body refuses and writes nothing.
	Yukie->bInMelee = false;
	Yukie->MeleeMustLeaveTimer = 0.0;
	Yukie->MeleeEventFires = 0;
	TestFalse(TEXT("a Yukie's slot 600 refuses without the weapon capability bits"), Yukie->Slot600(Cop));
	TestFalse(TEXT("writing no latch"), Yukie->bInMelee);
	TestEqual(TEXT("and firing no event"), Yukie->MeleeEventFires, 0);

	// 601 `0x103dd9a0`: the event and the clear, and no coordinator release.
	Yukie->bInMelee = true;
	Yukie->MeleeCoordinatorReleases = 0;
	Yukie->Slot601(Cop);
	TestFalse(TEXT("a Yukie's slot 601 leaves melee"), Yukie->bInMelee);
	TestEqual(TEXT("firing the event"), Yukie->MeleeEventFires, 1);
	TestEqual(TEXT("and releasing no coordinator slot"), Yukie->MeleeCoordinatorReleases, 0);

	// 602 `0x103dda10`: distance alone without a ranged weapon; the Troika body refuses first on its
	// null-coordinator test.
	Yukie->ScheduleHost.EnemyDistUnits = 500.f;
	Cop->AttackCoordinator = 0;
	Cop->ScheduleHost.EnemyDistUnits = 500.f;
	TestTrue(TEXT("a Yukie's slot 602 leaves melee on distance"), Yukie->Slot602());
	TestFalse(TEXT("where a plain Troika NPC's refuses"), Cop->Slot602());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesWiredSlot606Test,
	"Elysium.Substrate.NpcKernelSpecies.WiredSlot606", GElysiumNpcKernelSpeciesFlags)
bool FElysiumNpcKernelSpeciesWiredSlot606Test::RunTest(const FString&)
{
	FSpeciesWiringFixture Fixture(TEXT("CNPC_VBach"));
	FElysiumNpcBach* Bach = ElysiumTestAsSpecies<FElysiumNpcBach>(Fixture.Species);
	FElysiumNpc* Cop = Fixture.Troika;
	if (Bach == nullptr || Cop == nullptr)
	{
		AddError(TEXT("fixture did not stand both sides"));
		return false;
	}

	// With `COND_ENEMY_OCCLUDED` and `COND_ENEMY_UNREACHABLE` both up, the Troika body answers
	// `0xaa` at once. Bach's `0x10364280` arms `m_bFireOccluded` and answers 0 on the FIRST pass and
	// only then delegates — so the two answers differ on pass one and agree on pass two, which is
	// what proves the delegation is a DIRECT call into the base and not a second dispatch.
	for (FElysiumNpc* Npc : std::initializer_list<FElysiumNpc*>{ Bach, Cop })
	{
		Npc->Cognition.Conditions.Set(EElysiumNpcCond::EnemyOccluded);
		Npc->Cognition.Conditions.Set(EElysiumNpcCond::EnemyUnreachable);
	}
	Bach->bBachFireOccluded = false;

	TestEqual(TEXT("slot 606 on a Bach answers 0 on the first pass"), Bach->Slot606(0), 0);
	TestTrue(TEXT("having armed m_bFireOccluded"), Bach->bBachFireOccluded);
	TestEqual(TEXT("slot 606 on a plain Troika NPC answers 0xaa at once"), Cop->Slot606(0), 0xaa);
	TestEqual(TEXT("and Bach's SECOND pass delegates into that same base body"), Bach->Slot606(0),
		0xaa);
	TestTrue(TEXT("leaving the flag armed"), Bach->bBachFireOccluded);

	// The base's own side effect proves the destination: the Troika body's `FORCED_OCCLUDE` arm
	// CLEARS the flag. Bach's first pass (the HasCondition gate, then the flag write) returns before
	// the base, so the flag survives it; the second pass reaches the base, which clears it.
	for (FElysiumNpc* Npc : std::initializer_list<FElysiumNpc*>{ Bach, Cop })
	{
		Npc->Cognition.Conditions.Clear(EElysiumNpcCond::EnemyUnreachable);
		Npc->NpcFlags.Set(EElysiumNpcFlag::FORCED_OCCLUDE);
	}
	Bach->bBachFireOccluded = false;
	Bach->Slot606(0);
	TestTrue(TEXT("Bach's first pass arms m_bFireOccluded"), Bach->bBachFireOccluded);
	TestTrue(TEXT("and never reaches the base, so FORCED_OCCLUDE stands"),
		Bach->NpcFlags.Has(EElysiumNpcFlag::FORCED_OCCLUDE));
	Bach->Slot606(0);
	TestFalse(TEXT("its second pass reaches the Troika body, which clears FORCED_OCCLUDE"),
		Bach->NpcFlags.Has(EElysiumNpcFlag::FORCED_OCCLUDE));
	Cop->Slot606(0);
	TestFalse(TEXT("as a plain Troika NPC's first pass does"), Cop->NpcFlags.Has(EElysiumNpcFlag::FORCED_OCCLUDE));
	// Without the condition Bach answers 0 at once and disarms, and the base is not reached.
	Bach->Cognition.Conditions.Clear(EElysiumNpcCond::EnemyOccluded);
	Bach->NpcFlags.Set(EElysiumNpcFlag::FORCED_OCCLUDE);
	TestEqual(TEXT("with no COND_ENEMY_OCCLUDED Bach answers 0"), Bach->Slot606(0), 0);
	TestFalse(TEXT("and disarms"), Bach->bBachFireOccluded);
	TestTrue(TEXT("without reaching the base"), Bach->NpcFlags.Has(EElysiumNpcFlag::FORCED_OCCLUDE));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesWiredSlot609Test,
	"Elysium.Substrate.NpcKernelSpecies.WiredSlot609", GElysiumNpcKernelSpeciesFlags)
bool FElysiumNpcKernelSpeciesWiredSlot609Test::RunTest(const FString&)
{
	FSpeciesWiringFixture Fixture(TEXT("CNPC_VBach"));
	FElysiumNpcBach* Bach = ElysiumTestAsSpecies<FElysiumNpcBach>(Fixture.Species);
	FElysiumNpc* Cop = Fixture.Troika;
	if (Bach == nullptr || Cop == nullptr)
	{
		AddError(TEXT("fixture did not stand both sides"));
		return false;
	}

	// `0x103661f0` admits only retail `m_NPCState` 4 or 0xc; every other state has its cached
	// `m_pShootAtHintNode` ZEROED as a side effect of asking, and the base search never runs.
	Bach->BeginScriptedSchedule(FElysiumScriptedScheduleOrder(), true, EElysiumNpcState::Combat);
	Bach->ScheduleHost.ShootAtHintNode = 77;
	TestNull(TEXT("slot 609 on a combat Bach answers no hint"), Bach->Slot609(false));
	TestEqual(TEXT("and zeroes m_pShootAtHintNode as a side effect of asking"),
		Bach->ScheduleHost.ShootAtHintNode, 0);

	// A scripted body (retail state 4) passes the gate and the base search runs, leaving the word
	// where it is — the same place a plain Troika NPC leaves it, because there is no gate at all.
	Bach->BeginScriptedSchedule(FElysiumScriptedScheduleOrder(), true, EElysiumNpcState::Scripted);
	Bach->ScheduleHost.ShootAtHintNode = 77;
	Bach->Slot609(false);
	TestEqual(TEXT("a scripted Bach reaches the base search and keeps its cached hint"),
		Bach->ScheduleHost.ShootAtHintNode, 77);

	Cop->BeginScriptedSchedule(FElysiumScriptedScheduleOrder(), true, EElysiumNpcState::Combat);
	Cop->ScheduleHost.ShootAtHintNode = 77;
	Cop->Slot609(false);
	TestEqual(TEXT("a plain Troika NPC has no gate: its cached hint survives combat"),
		Cop->ScheduleHost.ShootAtHintNode, 77);
	return true;
}

#endif   // WITH_DEV_AUTOMATION_TESTS
