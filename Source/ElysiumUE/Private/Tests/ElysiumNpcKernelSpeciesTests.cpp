#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumAiScriptedSchedule.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcDialogue.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcMaker.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Tests/ElysiumNpcTestFixture.h"

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
// **Both tables are checked before a species answer is asserted.** A classname the spawn registry
// (`Substrate/ElysiumNpcClasses.cpp`) does not stand is exercised by RETAIL CLASS NAME through
// `SpeciesSlotRowOf`; only `npc_VTzimisceRunner`, `npc_VAnimal`, `npc_VAndreiBlood` and
// `npc_maker_fleshpile` are spawned. `npc_VCop`'s null `RetailClass()` is asserted, not worked
// around.

static constexpr EAutomationTestFlags GElysiumNpcKernelSpeciesFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// The one fixture shape this suite uses: a world with the four spawnable species that reach
	// this family's rows, plus one cop for the null-census case.
	struct FSpeciesFixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumNpc* Runner = nullptr;
		FElysiumNpc* Animal = nullptr;
		FElysiumNpc* Andrei = nullptr;
		FElysiumNpc* Cop = nullptr;

		FSpeciesFixture()
			: World(Build())
		{
			Runner = World.Npc(TEXT("runner"));
			Animal = World.Npc(TEXT("animal"));
			Andrei = World.Npc(TEXT("andrei"));
			Cop = World.Npc(TEXT("cop"));
			FElysiumNpcWorldFixture::Quiet({ Runner, Animal, Andrei, Cop });
		}

		static FElysiumNpcWorldBuilder Build()
		{
			FElysiumNpcWorldBuilder Builder(TEXT("species"), 29131u);
			Builder.AddNpc(TEXT("runner"), FVector::ZeroVector, TEXT("npc_VTzimisceRunner"));
			Builder.AddNpc(TEXT("animal"), FVector(200.0, 0.0, 0.0), TEXT("npc_VAnimal"));
			Builder.AddNpc(TEXT("andrei"), FVector(400.0, 0.0, 0.0), TEXT("npc_VAndreiBlood"));
			Builder.AddNpc(TEXT("cop"), FVector(600.0, 0.0, 0.0), TEXT("npc_VCop"));
			return Builder;
		}
	};
}

// -------------------------------------------------------------------------------------------------
// The species slot table — every row by name, against the census.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesSlotTableTest,
	"Elysium.Substrate.NpcKernelSpecies.SlotTable", GElysiumNpcKernelSpeciesFlags)
bool FElysiumNpcKernelSpeciesSlotTableTest::RunTest(const FString&)
{
	int32 Count = 0;
	const FElysiumNpc::FSpeciesSlotRow* Rows = FElysiumNpc::SpeciesSlotRows(Count);
	TestEqual(TEXT("the table carries this family's 34 (class, slot) rows"), Count, 34);

	// Every row, BY NAME: the class is a census class, and the census agrees that the row's retail
	// address is the body that fills that slot for it. A row that does not match `slots.md` fails
	// here rather than being discovered by a reader.
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FElysiumNpc::FSpeciesSlotRow& Row = Rows[Index];
		const FString Name(Row.RetailClass);
		const FElysiumNpcClass* Cls = ElysiumNpcKernelClass::Find(Row.RetailClass);
		TestNotNull(*FString::Printf(TEXT("%s is a census class"), *Name), Cls);
		if (Cls == nullptr)
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s fills slot %d with %s"), *Name, Row.Slot, Row.Address),
			FString(ElysiumNpcKernelClass::BodyOf(Cls, Row.Slot)), FString(Row.Address));
	}

	// The base-chain walk is the vtable's, not a name compare: `CNPC_VCameraSecurity` has no body of
	// its own at 497 or 506 and inherits `CNPC_VCamera`'s, and `CNPC_VDog` / `CNPC_VRat` inherit
	// `CNPC_VAnimal`'s slot 482.
	const FElysiumNpc::FSpeciesSlotRow* Security =
		FElysiumNpc::SpeciesSlotRowOf(TEXT("CNPC_VCameraSecurity"), 497);
	TestNotNull(TEXT("CNPC_VCameraSecurity inherits slot 497"), Security);
	if (Security != nullptr)
	{
		TestEqual(TEXT("from CNPC_VCamera's 0x103681d0"), FString(Security->Address),
			FString(TEXT("0x103681d0")));
	}
	const FElysiumNpc::FSpeciesSlotRow* Dog =
		FElysiumNpc::SpeciesSlotRowOf(TEXT("CNPC_VDog"), 482);
	TestNotNull(TEXT("CNPC_VDog inherits slot 482"), Dog);
	if (Dog != nullptr)
	{
		TestEqual(TEXT("from CNPC_VAnimal's 0x1035fd40"), FString(Dog->Address),
			FString(TEXT("0x1035fd40")));
	}

	// And a class with no row at a slot answers null rather than the nearest row of another slot.
	TestNull(TEXT("CNPC_VAnimal has no slot 599 row"),
		FElysiumNpc::SpeciesSlotRowOf(TEXT("CNPC_VAnimal"), 599));
	TestNull(TEXT("a null class name answers null"), FElysiumNpc::SpeciesSlotRowOf(nullptr, 599));
	TestNull(TEXT("a class outside the family answers null"),
		FElysiumNpc::SpeciesSlotRowOf(TEXT("CNotAClass"), 599));
	return true;
}

// -------------------------------------------------------------------------------------------------
// The two tables that disagree — the fact four landed families lost time to.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesCensusFallThroughTest,
	"Elysium.Substrate.NpcKernelSpecies.CensusFallThrough", GElysiumNpcKernelSpeciesFlags)
bool FElysiumNpcKernelSpeciesCensusFallThroughTest::RunTest(const FString&)
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

	// The census resolves a spawned runner to its MOST DERIVED claimant — `npc_VTzimisceRunner` is
	// claimed by `CNPC_VBaseBoss` too.
	const FElysiumNpcClass* RunnerClass = Fixture.Runner->RetailClass();
	TestNotNull(TEXT("a spawned runner has a census class"), RunnerClass);
	if (RunnerClass != nullptr)
	{
		TestEqual(TEXT("and it is CNPC_VTzimisceRunner, not its CNPC_VBaseBoss claimant"),
			FString(RunnerClass->Name), FString(TEXT("CNPC_VTzimisceRunner")));
	}

	// **The recovered answer, not a bug**: no census class lists `npc_VCop`, so a spawned cop's
	// `RetailClass()` is null and every per-species lookup falls through to the Troika line.
	TestNull(TEXT("a spawned cop has NO census class"), Fixture.Cop->RetailClass());
	TestNull(TEXT("so it has no species row at slot 599"), Fixture.Cop->SpeciesSlotRow(599));
	bool Answer = false;
	TestFalse(TEXT("and the slot-599 dispatcher refuses to run a species body for it"),
		Fixture.Cop->SpeciesSlot599(nullptr, Answer));

	// `npc_VCamera` is claimed by the census but is NOT a spawn leaf — a case that spawned one would
	// silently fail its own guard, so the camera rows are exercised by retail class name instead.
	TestNull(TEXT("npc_VCamera is not a registered spawn leaf"), Fixture.World.Npc(TEXT("camera")));
	TestNotNull(TEXT("but the census claims it"),
		ElysiumNpcKernelClass::OfClassname(TEXT("npc_VCamera")));
	TestNotNull(TEXT("and its slot-497 row is reachable by class name"),
		FElysiumNpc::SpeciesSlotRowOf(TEXT("CNPC_VCamera"), 497));
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
	FElysiumNpc* Boss = Fixture.Runner;
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
	if (Npc == nullptr || Enemy == nullptr)
	{
		return false;
	}

	// --- `CNPC_VFrenzyShadow` / `CNPC_VGargoyle`: every gate dropped, always true --------------
	// Both classes are in the census and neither is a spawn leaf, so they are exercised by class
	// name through the table and their BODIES are called directly.
	for (const TCHAR* Name : { TEXT("CNPC_VFrenzyShadow"), TEXT("CNPC_VGargoyle") })
	{
		const FElysiumNpc::FSpeciesSlotRow* Row599 = FElysiumNpc::SpeciesSlotRowOf(Name, 599);
		const FElysiumNpc::FSpeciesSlotRow* Row600 = FElysiumNpc::SpeciesSlotRowOf(Name, 600);
		TestNotNull(*FString::Printf(TEXT("%s has a slot-599 row"), Name), Row599);
		TestNotNull(*FString::Printf(TEXT("%s has a slot-600 row"), Name), Row600);
	}
	Npc->bInMelee = false;
	Npc->MeleeEventFires = 0;
	TestTrue(TEXT("0x10376b70 (CNPC_VFrenzyShadow 599) always enters melee"),
		Npc->FUN_10376b70(Enemy));
	TestTrue(TEXT("and sets m_bInMelee"), Npc->bInMelee);
	TestEqual(TEXT("having fired the global melee event once"), Npc->MeleeEventFires, 1);
	TestTrue(TEXT("0x10379ef0 (CNPC_VGargoyle 599) is the same body"), Npc->FUN_10379ef0(Enemy));
	TestEqual(TEXT("so the event count is 2"), Npc->MeleeEventFires, 2);
	// Neither arms a leave timer — the recovered difference from the Troika line.
	Npc->MeleeMustLeaveTimer = 0.0;
	Npc->FUN_10376b70(Enemy);
	TestEqual(TEXT("and neither arms m_flMeleeMustLeaveTimer"), Npc->MeleeMustLeaveTimer, 0.0);

	Npc->bInMelee = false;
	Npc->MeleeEventFires = 0;
	TestTrue(TEXT("0x10376ba0 (CNPC_VFrenzyShadow 600) always accepts"), Npc->FUN_10376ba0(Enemy));
	TestTrue(TEXT("and sets m_bInMelee"), Npc->bInMelee);
	TestTrue(TEXT("0x10379f20 (CNPC_VGargoyle 600) is the same body"), Npc->FUN_10379f20(Enemy));
	TestEqual(TEXT("two events"), Npc->MeleeEventFires, 2);
	// Already in melee is NOT a refusal for these two — the Troika line's re-entry guard is gone.
	TestTrue(TEXT("and a body already in melee still accepts"), Npc->FUN_10376ba0(Enemy));

	// --- `CNPC_VTzimisceHeadClaw` 599: the coordinator alone ------------------------------------
	// `MeleeCoordinatorAdmits599` is family TroikaHelpers' seam and answers false with no
	// coordinator object, so the body takes its refusal arm — which is retail's own answer for a
	// coordinator with no free slot.
	Npc->bInMelee = true;
	Npc->MeleeEventFires = 0;
	TestFalse(TEXT("0x103c19e0 refuses when the coordinator seam refuses"),
		Npc->FUN_103c19e0(Enemy));
	TestFalse(TEXT("and clears m_bInMelee on the way out"), Npc->bInMelee);
	TestEqual(TEXT("firing no melee event"), Npc->MeleeEventFires, 0);

	// --- `CNPC_VTzimisceRunner` 599: the same, plus the potential-enemy cache -------------------
	Npc->RunnerPotentialEnemy = FElysiumEntityHandle();
	Npc->bInMelee = true;
	TestFalse(TEXT("0x103c3960 refuses on the same seam"), Npc->FUN_103c3960(Enemy));
	TestFalse(TEXT("and clears m_bInMelee"), Npc->bInMelee);
	// **The cache is written on the REFUSING arm too** — the one slot-599 body in the family that
	// reads its argument at all.
	TestTrue(TEXT("but m_hPotentialEnemy was cached anyway"),
		Npc->RunnerPotentialEnemy == Enemy->Handle);
	Npc->FUN_103c3960(nullptr);
	TestFalse(TEXT("and a null argument clears it"), Npc->RunnerPotentialEnemy.IsSet());

	// --- slot 600's head-claw / runner pair: the event fires BEFORE the decision ----------------
	Npc->bInMelee = false;
	Npc->MeleeEventFires = 0;
	TestFalse(TEXT("0x103c1a60 refuses on the coordinator seam"), Npc->FUN_103c1a60(Enemy));
	TestEqual(TEXT("but the global melee event fired anyway — it is unconditional and first"),
		Npc->MeleeEventFires, 1);
	// An NPC ALREADY in melee skips the request and is taken back OUT — the guard is on the way in.
	Npc->bInMelee = true;
	TestFalse(TEXT("and a body already in melee answers false"), Npc->FUN_103c1a60(Enemy));
	TestFalse(TEXT("and is cleared, because the clear is outside the guard"), Npc->bInMelee);

	Npc->RunnerPotentialEnemy = FElysiumEntityHandle();
	Npc->MeleeEventFires = 0;
	Npc->bInMelee = false;
	TestFalse(TEXT("0x103c39e0 refuses the same way"), Npc->FUN_103c39e0(Enemy));
	TestEqual(TEXT("with the event fired first"), Npc->MeleeEventFires, 1);
	TestTrue(TEXT("and m_hPotentialEnemy cached"), Npc->RunnerPotentialEnemy == Enemy->Handle);

	// --- `CNPC_VYukie` 600: not a melee body at all ---------------------------------------------
	// `ActiveWeaponCapabilityWord()` is family Motor's seam and answers 0, so the `0x18000` gate is
	// closed and the whole body refuses without writing anything.
	Npc->bInMelee = false;
	Npc->MeleeMustLeaveTimer = 0.0;
	Npc->MeleeEventFires = 0;
	TestFalse(TEXT("0x103dd900 refuses without the weapon capability bits"),
		Npc->FUN_103dd900(Enemy));
	TestFalse(TEXT("writing no latch"), Npc->bInMelee);
	TestEqual(TEXT("no flee window"), Npc->MeleeMustLeaveTimer, 0.0);
	TestEqual(TEXT("and no event"), Npc->MeleeEventFires, 0);

	// --- slot 601: the release pair --------------------------------------------------------------
	Npc->bInMelee = true;
	Npc->MeleeEventFires = 0;
	Npc->MeleeCoordinatorReleases = 0;
	Npc->FUN_103c1ad0(Enemy);
	TestFalse(TEXT("0x103c1ad0 leaves melee"), Npc->bInMelee);
	TestEqual(TEXT("firing the event"), Npc->MeleeEventFires, 1);
	TestEqual(TEXT("and releasing the coordinator slot UNGUARDED"),
		Npc->MeleeCoordinatorReleases, 1);

	Npc->bInMelee = true;
	Npc->RunnerPotentialEnemy = Enemy->Handle;
	Npc->MeleeCoordinatorReleases = 0;
	Npc->FUN_103c3a70(Enemy);
	TestFalse(TEXT("0x103c3a70 leaves melee"), Npc->bInMelee);
	TestFalse(TEXT("and CLEARS m_hPotentialEnemy — the runner's matched set"),
		Npc->RunnerPotentialEnemy.IsSet());
	TestEqual(TEXT("still releasing the slot"), Npc->MeleeCoordinatorReleases, 1);

	// --- slot 602: the far arm only ---------------------------------------------------------------
	// `MeleeRangeUnits()` answers 0.0 (UNRECOVERED ConVar) so the doubled range is 0, and
	// `MeleeCoordinatorHasRoom()` answers false — so any positive enemy distance takes the first arm.
	Npc->ScheduleHost.EnemyDistUnits = 500.f;
	TestTrue(TEXT("0x103c1b10 leaves melee when out of double range and the coordinator is full"),
		Npc->FUN_103c1b10());
	TestTrue(TEXT("0x103c3ab0 is the byte-identical twin"), Npc->FUN_103c3ab0());
	// At zero distance the first arm's `0 < 0` fails and the body falls through to
	// `MeleeCoordinatorHoldsMe()`, which answers true for a coordinator that holds nobody.
	Npc->ScheduleHost.EnemyDistUnits = 0.f;
	TestTrue(TEXT("and at zero distance it falls through to 'the coordinator does not hold me'"),
		Npc->FUN_103c1b10());

	// The dispatcher picks the runner's bodies for a spawned runner, and only those.
	bool Answer = false;
	Npc->bInMelee = true;
	TestTrue(TEXT("the slot-599 dispatcher runs a species body for a runner"),
		Npc->SpeciesSlot599(Enemy, Answer));
	TestFalse(TEXT("and it is the refusing coordinator arm"), Answer);
	TestTrue(TEXT("the slot-601 dispatcher runs one too"), Npc->SpeciesSlot601(Enemy));
	TestTrue(TEXT("and the slot-602 dispatcher"), Npc->SpeciesSlot602(Answer));
	// The animal has no melee row at all.
	if (Fixture.Animal != nullptr)
	{
		TestFalse(TEXT("an animal has no species slot-599 body"),
			Fixture.Animal->SpeciesSlot599(Enemy, Answer));
	}
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
	FElysiumNpc* Npc = Fixture.Runner;
	if (Npc == nullptr)
	{
		return false;
	}

	// `CNPC_VBach` is a census class and not a spawn leaf, so the rows are reached by name and the
	// bodies are called directly.
	const FElysiumNpc::FSpeciesSlotRow* Row606 =
		FElysiumNpc::SpeciesSlotRowOf(TEXT("CNPC_VBach"), 606);
	TestNotNull(TEXT("CNPC_VBach has a slot-606 row"), Row606);
	if (Row606 != nullptr)
	{
		TestEqual(TEXT("and it is 0x10364280"), FString(Row606->Address),
			FString(TEXT("0x10364280")));
	}

	// --- `0x10364280`: without the condition the flag is CLEARED and 0 is answered ---------------
	Npc->bBachFireOccluded = true;
	Npc->Cognition.Conditions.Clear(EElysiumNpcCond::EnemyOccluded);
	TestEqual(TEXT("no COND_ENEMY_OCCLUDED answers 0"), Npc->FUN_10364280(0), 0);
	TestFalse(TEXT("and clears m_bFireOccluded"), Npc->bBachFireOccluded);

	// --- with the condition, the FIRST pass only arms -------------------------------------------
	Npc->Cognition.Conditions.Set(EElysiumNpcCond::EnemyOccluded);
	TestEqual(TEXT("the first pass with the condition still answers 0"), Npc->FUN_10364280(0), 0);
	TestTrue(TEXT("but arms m_bFireOccluded"), Npc->bBachFireOccluded);

	// --- the SECOND pass delegates to the Troika body --------------------------------------------
	// `Slot606` is family TroikaHelpers'; the gate's job is to reach it, which is what is asserted.
	const int32 Base = Npc->Slot606(0);
	TestEqual(TEXT("the second pass delegates to the base slot 606"), Npc->FUN_10364280(0), Base);
	TestTrue(TEXT("and leaves the flag armed"), Npc->bBachFireOccluded);

	// --- the hysteresis is RE-PAID every time the condition drops --------------------------------
	Npc->Cognition.Conditions.Clear(EElysiumNpcCond::EnemyOccluded);
	TestEqual(TEXT("dropping the condition answers 0"), Npc->FUN_10364280(0), 0);
	TestFalse(TEXT("and disarms"), Npc->bBachFireOccluded);
	Npc->Cognition.Conditions.Set(EElysiumNpcCond::EnemyOccluded);
	TestEqual(TEXT("so the next sighting arms again rather than firing"), Npc->FUN_10364280(0), 0);

	// --- slot 609: the state gate ----------------------------------------------------------------
	// Only retail states 4 (`NPC_STATE_SCRIPT`) and 0xc admit the base hint search; every other
	// state ZEROES `m_pShootAtHintNode` as a side effect of asking.
	for (const TCHAR* Name : { TEXT("CNPC_VBach"), TEXT("CNPC_VBatSwarm"),
		TEXT("CNPC_VSheriffSwarm") })
	{
		const FElysiumNpc::FSpeciesSlotRow* Row = FElysiumNpc::SpeciesSlotRowOf(Name, 609);
		TestNotNull(*FString::Printf(TEXT("%s has a slot-609 row"), Name), Row);
	}
	Npc->ScheduleHost.ShootAtHintNode = 77;
	Npc->BeginScriptedSchedule(FElysiumScriptedScheduleOrder(), true, EElysiumNpcState::Combat);
	TestFalse(TEXT("a combat body does not reach the base hint search"), Npc->FUN_103661f0(false));
	TestEqual(TEXT("and its cached shoot-at hint is zeroed"), Npc->ScheduleHost.ShootAtHintNode, 0);

	Npc->ScheduleHost.ShootAtHintNode = 77;
	Npc->BeginScriptedSchedule(FElysiumScriptedScheduleOrder(), true, EElysiumNpcState::Scripted);
	TestTrue(TEXT("a scripted body (retail state 4) does reach it"), Npc->FUN_103661f0(false));
	TestEqual(TEXT("and its cached hint is left alone"), Npc->ScheduleHost.ShootAtHintNode, 77);

	// The other two are byte-identical, verified by driving them through the same two states.
	Npc->BeginScriptedSchedule(FElysiumScriptedScheduleOrder(), true, EElysiumNpcState::Combat);
	TestFalse(TEXT("0x10367740 (CNPC_VBatSwarm) is the same gate"), Npc->FUN_10367740(false));
	TestFalse(TEXT("0x103b26f0 (CNPC_VSheriffSwarm) is the same gate"), Npc->FUN_103b26f0(false));
	Npc->BeginScriptedSchedule(FElysiumScriptedScheduleOrder(), true, EElysiumNpcState::Scripted);
	TestTrue(TEXT("and both admit a scripted body"), Npc->FUN_10367740(false));
	TestTrue(TEXT("both of them"), Npc->FUN_103b26f0(false));
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 482 — five species, one byte-identical body.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesCanPlaySequenceTest,
	"Elysium.Substrate.NpcKernelSpecies.CanPlaySequence", GElysiumNpcKernelSpeciesFlags)
bool FElysiumNpcKernelSpeciesCanPlaySequenceTest::RunTest(const FString&)
{
	FSpeciesFixture Fixture;
	FElysiumNpc* Animal = Fixture.Animal;
	if (Animal == nullptr)
	{
		return false;
	}

	// The census agrees the five classes carry their own copy, and every copy is byte-identical to
	// the base `CAI_BaseNPC::CanPlaySequence` `0x10278090` — so the species answer IS the base's.
	TestEqual(TEXT("CNPC_VAnimal's slot 482 is 0x1035fd40"),
		FString(ElysiumNpcKernelClass::BodyOf(
			ElysiumNpcKernelClass::Find(TEXT("CNPC_VAnimal")), 482)),
		FString(TEXT("0x1035fd40")));
	TestEqual(TEXT("CNPC_VTzimisce's is 0x103bd270"),
		FString(ElysiumNpcKernelClass::BodyOf(
			ElysiumNpcKernelClass::Find(TEXT("CNPC_VTzimisce")), 482)),
		FString(TEXT("0x103bd270")));

	// The state gate, from the decompiled C: refuse when the caller did not disregard state, the
	// body is neither NONE (0) nor IDLE (1), its ideal is not IDLE, and it is not an ALERT (3) body
	// asked with an interrupt level of at least 1.
	Animal->BeginScriptedSchedule(FElysiumScriptedScheduleOrder(), true, EElysiumNpcState::Combat);
	TestEqual(TEXT("a combat body refuses a sequence"), Animal->FUN_1035fd40(false, 0), 0);
	TestEqual(TEXT("and the Tzimisce copy answers the same"), Animal->FUN_103bd270(false, 0), 0);
	TestEqual(TEXT("and so does the base it is a copy of"), Animal->CanPlaySequence(false, 0), 0);

	TestEqual(TEXT("disregarding state admits it"), Animal->FUN_1035fd40(true, 0), 1);
	Animal->BeginScriptedSchedule(FElysiumScriptedScheduleOrder(), true, EElysiumNpcState::Idle);
	TestEqual(TEXT("an idle body admits it"), Animal->FUN_1035fd40(false, 0), 1);
	Animal->BeginScriptedSchedule(FElysiumScriptedScheduleOrder(), true, EElysiumNpcState::Alert);
	TestEqual(TEXT("an alert body refuses at interrupt level 0"), Animal->FUN_1035fd40(false, 0), 0);
	TestEqual(TEXT("but admits at level 1"), Animal->FUN_1035fd40(false, 1), 1);

	// The dispatcher reaches it for a spawned `npc_VAnimal`, and not for the runner's class, whose
	// slot-482 body is `CNPC_VTzimisce`'s and not `CNPC_VTzimisceRunner`'s own.
	int32 Answer = -1;
	TestTrue(TEXT("the dispatcher runs a species body for an animal"),
		Animal->SpeciesCanPlaySequence(true, 0, Answer));
	TestEqual(TEXT("answering the base's 1"), Answer, 1);
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
	FElysiumNpc* Npc = Fixture.Runner;
	FElysiumNpc* Body = Fixture.Animal;
	if (Npc == nullptr || Body == nullptr)
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
	Npc->HeadClawSlowedExpire = 0.0;
	TestFalse(TEXT("an unarmed slow window answers false"), Npc->FUN_103c24a0());
	Npc->HeadClawSlowedExpire = 1.0;
	TestTrue(TEXT("an armed one answers true"), Npc->FUN_103c24a0());
	// **And it keeps answering true long after the stamp is in the past** — the predicate is
	// `0.0 < m_flSlowedExpire`, never `curtime < m_flSlowedExpire`, so only a write back to zero
	// clears it.
	Fixture.World.Advance(Now + 100.0);
	TestTrue(TEXT("and still answers true 100 seconds past the stamp"), Npc->FUN_103c24a0());
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
	FElysiumNpc* Npc = Fixture.Runner;
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
	Npc->Senses.Memory.Enemy = DoorA->Handle;
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
	Npc->Senses.Memory.Enemy = FElysiumEntityHandle();
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
	FElysiumNpc* Npc = Fixture.Runner;
	FElysiumNpc* Victim = Fixture.Animal;
	if (Npc == nullptr || Victim == nullptr)
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
		if (Fresh.Runner == nullptr)
		{
			continue;
		}
		Fresh.Runner->FUN_103e0980(Type);
		TestTrue(*FString::Printf(TEXT("ZombieAIType %d pushes a scripted order"), Type),
			Fresh.Runner->GetMind().IsStateChangeForced());
	}
	for (int32 Type : { 0, 1, 7 })
	{
		FSpeciesFixture Fresh;
		if (Fresh.Runner == nullptr)
		{
			continue;
		}
		Fresh.Runner->FUN_103e0980(Type);
		TestFalse(*FString::Printf(TEXT("ZombieAIType %d does not"), Type),
			Fresh.Runner->GetMind().IsStateChangeForced());
	}

	// --- slots 25 and 26: the same output from two vtable entries, no base forward ---------------
	const FElysiumNpc::FSpeciesSlotRow* Row25 =
		FElysiumNpc::SpeciesSlotRowOf(TEXT("CNPC_VZombie"), 25);
	const FElysiumNpc::FSpeciesSlotRow* Row26 =
		FElysiumNpc::SpeciesSlotRowOf(TEXT("CNPC_VZombie"), 26);
	TestNotNull(TEXT("CNPC_VZombie has a slot-25 row"), Row25);
	TestNotNull(TEXT("and a slot-26 row"), Row26);
	if (Row25 != nullptr && Row26 != nullptr)
	{
		TestEqual(TEXT("slot 25 is 0x103e12c0"), FString(Row25->Address),
			FString(TEXT("0x103e12c0")));
		TestEqual(TEXT("slot 26 is 0x103e12f0"), FString(Row26->Address),
			FString(TEXT("0x103e12f0")));
	}
	// Both fire `m_OnAttackedVictim`; the wiring is what a mapper sees, so the counter is the read.
	{
		FElysiumNpcWorldBuilder Builder(TEXT("zombie"), 29132u);
		Builder.AddNpc(TEXT("zombie"), FVector::ZeroVector, TEXT("npc_VTzimisceRunner"));
		Builder.AddCounter(TEXT("victims"));
		Builder.WireOutput(TEXT("zombie"), TEXT("OnAttackedVictim"), TEXT("victims"));
		FElysiumNpcWorldFixture World(MoveTemp(Builder));
		FElysiumNpc* Zombie = World.Npc(TEXT("zombie"));
		FElysiumNpcWorldFixture::Quiet({ Zombie });
		if (Zombie != nullptr)
		{
			Zombie->FUN_103e12c0(Victim);
			World.World.Tick(World.World.NowSeconds() + 0.1);
			TestEqual(TEXT("slot 25 fires OnAttackedVictim"), World.Counter(TEXT("victims")), 1.f);
			Zombie->FUN_103e12f0(Victim);
			World.World.Tick(World.World.NowSeconds() + 0.1);
			TestEqual(TEXT("and slot 26 fires the SAME output"),
				World.Counter(TEXT("victims")), 2.f);
		}
	}

	// --- slot 510: the frequency write happens FIRST and on every call ---------------------------
	Npc->FloatSoundFrequency = 0;
	ElysiumMiscFlags::Set(Npc->MiscFlags, 0x1u);   // unconscious — the second gate
	TestFalse(TEXT("an unconscious zombie plays no float sound"), Npc->FUN_103e1080(false));
	TestEqual(TEXT("but m_iFloatSoundFrequency was set to 9 before any gate ran"),
		Npc->FloatSoundFrequency, 9);

	Npc->FloatSoundFrequency = 0;
	ElysiumMiscFlags::Clear(Npc->MiscFlags, 0x1u);
	Npc->NpcFlags.Set(EElysiumNpcFlag::SLEEPING);
	TestFalse(TEXT("a SLEEPING zombie plays none either"), Npc->FUN_103e1080(false));
	TestEqual(TEXT("and the frequency is still written"), Npc->FloatSoundFrequency, 9);
	Npc->NpcFlags.Clear(EElysiumNpcFlag::SLEEPING);

	// With no closest player it refuses; with one it reaches the UNRECOVERED distance threshold,
	// whose named decision (0.0) makes the body refuse for any positive player distance.
	Npc->Senses.Memory.ClosestPlayer = FElysiumEntityHandle();
	TestFalse(TEXT("with no closest player it refuses"), Npc->FUN_103e1080(false));
	Npc->Senses.Memory.ClosestPlayer = Victim->Handle;
	Npc->Senses.Memory.ClosestPlayerDistanceCm = 500.f;
	TestFalse(TEXT("and with one it refuses on the unrecovered 'Float Sound Info' threshold"),
		Npc->FUN_103e1080(false));
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
	FElysiumNpc* Andrei = Fixture.Andrei;
	FElysiumNpc* Runner = Fixture.Runner;
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

	// --- the maker's two overrides ---------------------------------------------------------------
	// `npc_maker_fleshpile` IS a registered spawn leaf and shares `FElysiumNpcMaker` with
	// `npc_maker`, so the fleshpile arms are a species branch on that class and not a subclass.
	{
		FElysiumNpcWorldBuilder Builder(TEXT("fleshpile"), 29133u);
		FElysiumEntityDef& Maker = Builder.AddEntity(TEXT("npc_maker_fleshpile"), TEXT("maker"));
		Maker.Keys.Add(TEXT("NPCType"), TEXT("npc_VTzimisceRunner"));
		Maker.Keys.Add(TEXT("MaxNPCCount"), TEXT("10"));
		Maker.Keys.Add(TEXT("Flag_StartDisabled"), TEXT("1"));
		Builder.AddNpc(TEXT("owner"), FVector(900.0, 0.0, 0.0), TEXT("npc_VAndreiBlood"));
		Builder.AddNpc(TEXT("child"), FVector(950.0, 0.0, 0.0), TEXT("npc_VTzimisceRunner"));
		FElysiumNpcWorldFixture World(MoveTemp(Builder));

		FElysiumEntity* MakerEnt = World.World.FindByName(TEXT("maker"));
		TestNotNull(TEXT("npc_maker_fleshpile stands"), MakerEnt);
		FElysiumNpcMaker* Fleshpile = MakerEnt != nullptr
			? static_cast<FElysiumNpcMaker*>(MakerEnt) : nullptr;
		FElysiumNpc* Owner = World.Npc(TEXT("owner"));
		FElysiumNpc* Child = World.Npc(TEXT("child"));
		FElysiumNpcWorldFixture::Quiet({ Owner, Child });
		if (Fleshpile == nullptr || Owner == nullptr || Child == nullptr)
		{
			return false;
		}
		TestTrue(TEXT("and it is the fleshpile variant"), Fleshpile->IsFleshpileMaker());
		TestEqual(TEXT("whose singleton owner is the one npc_VAndreiBlood in the level"),
			Fleshpile->FleshpileOwner(), Owner);

		// `0x1034c2d0`: the budget gate. With the cap reached, MakeNPC refuses — a term the base
		// `CNPCMaker::MakeNPC` does not have at all.
		Owner->ActiveRunnerCount = 2;
		TestEqual(TEXT("0x1034c2d0 refuses once Andrei has two live runners"),
			Fleshpile->FUN_1034c2d0(/*bBypass=*/false), FElysiumNpcMaker::EAttempt::LiveLimit);
		TestEqual(TEXT("and the count is untouched"), Owner->ActiveRunnerCount, 2);

		// **The bypass flag skips the WHOLE admission, blood budget included.**
		const FElysiumNpcMaker::EAttempt Bypassed = Fleshpile->FUN_1034c2d0(/*bBypass=*/true);
		TestEqual(TEXT("but the bypass arm spawns anyway"), Bypassed,
			FElysiumNpcMaker::EAttempt::Spawned);
		TestEqual(TEXT("and increments the live-runner count by one"),
			Owner->ActiveRunnerCount, 3);

	}

	// With NO npc_VAndreiBlood in the level at all, the non-bypass arm refuses outright — the first
	// term of the fleshpile's admission and one the base maker does not have.
	{
		FElysiumNpcWorldBuilder Builder(TEXT("fleshpile_no_owner"), 29135u);
		FElysiumEntityDef& Maker = Builder.AddEntity(TEXT("npc_maker_fleshpile"), TEXT("maker"));
		Maker.Keys.Add(TEXT("NPCType"), TEXT("npc_VTzimisceRunner"));
		Maker.Keys.Add(TEXT("MaxNPCCount"), TEXT("10"));
		Maker.Keys.Add(TEXT("Flag_StartDisabled"), TEXT("1"));
		FElysiumNpcWorldFixture World(MoveTemp(Builder));
		FElysiumEntity* MakerEnt = World.World.FindByName(TEXT("maker"));
		FElysiumNpcMaker* Fleshpile = MakerEnt != nullptr
			? static_cast<FElysiumNpcMaker*>(MakerEnt) : nullptr;
		if (Fleshpile != nullptr)
		{
			TestNull(TEXT("with no npc_VAndreiBlood there is no singleton"),
				Fleshpile->FleshpileOwner());
			TestEqual(TEXT("and 0x1034c2d0 refuses outright"),
				Fleshpile->FUN_1034c2d0(/*bBypass=*/false),
				FElysiumNpcMaker::EAttempt::InvalidChild);
		}
	}

	// `0x1034c8e0`: the death notice's once-only decrement, over a fresh world so the singleton is
	// live again.
	{
		FElysiumNpcWorldBuilder Builder(TEXT("fleshpile2"), 29134u);
		FElysiumEntityDef& Maker = Builder.AddEntity(TEXT("npc_maker_fleshpile"), TEXT("maker"));
		Maker.Keys.Add(TEXT("NPCType"), TEXT("npc_VTzimisceRunner"));
		Maker.Keys.Add(TEXT("Flag_StartDisabled"), TEXT("1"));
		Builder.AddNpc(TEXT("owner"), FVector(900.0, 0.0, 0.0), TEXT("npc_VAndreiBlood"));
		Builder.AddNpc(TEXT("child"), FVector(950.0, 0.0, 0.0), TEXT("npc_VTzimisceRunner"));
		Builder.AddNpc(TEXT("bystander"), FVector(980.0, 0.0, 0.0), TEXT("npc_VAnimal"));
		FElysiumNpcWorldFixture World(MoveTemp(Builder));
		FElysiumEntity* MakerEnt = World.World.FindByName(TEXT("maker"));
		FElysiumNpcMaker* Fleshpile = MakerEnt != nullptr
			? static_cast<FElysiumNpcMaker*>(MakerEnt) : nullptr;
		FElysiumNpc* Owner = World.Npc(TEXT("owner"));
		FElysiumNpc* Child = World.Npc(TEXT("child"));
		FElysiumNpc* Bystander = World.Npc(TEXT("bystander"));
		FElysiumNpcWorldFixture::Quiet({ Owner, Child, Bystander });
		if (Fleshpile == nullptr || Owner == nullptr || Child == nullptr || Bystander == nullptr)
		{
			return false;
		}
		Owner->ActiveRunnerCount = 2;
		Owner->AndreiKillCount = 0;

		Fleshpile->FUN_1034c8e0(Child);
		TestEqual(TEXT("0x1034c8e0 decrements the live-runner count"),
			Owner->ActiveRunnerCount, 1);
		TestEqual(TEXT("and counts the kill"), Owner->AndreiKillCount, 1);
		TestTrue(TEXT("marking the runner so it cannot be counted twice"),
			Child->bRunnerDeathNoticeProcessed);

		// The once-only flag is what stops a corpse re-notified from being counted again.
		Fleshpile->FUN_1034c8e0(Child);
		TestEqual(TEXT("a second notice for the same runner changes nothing"),
			Owner->ActiveRunnerCount, 1);
		TestEqual(TEXT("nor the kill count"), Owner->AndreiKillCount, 1);

		// A child that is not a `CNPC_VTzimisceRunner` fails the cast and is not counted.
		Fleshpile->FUN_1034c8e0(Bystander);
		TestEqual(TEXT("and a non-runner child is not counted at all"),
			Owner->ActiveRunnerCount, 1);
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
	FElysiumNpc* Npc = Fixture.Runner;
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

	// --- `0x103577d0`, slot 197: the forward answers its own ARGUMENT back ------------------------
	// Every other class's slot 197 answers the body's own centre; a crow's answers the caller's
	// point, unchanged.
	const FVector Asked(11.0, 22.0, 33.0);
	TestEqual(TEXT("CNPC_Crow's slot 197 answers the point it was asked about"),
		Npc->FUN_103577d0(Asked), Asked);
	TestNotEqual(TEXT("which is not this body's own world-space centre"),
		Npc->WorldSpaceCenter(), Asked);

	// --- `0x10357be0`: the scale clamp is a FLATTEN -----------------------------------------------
	// `0.0 < scale` is true for every positive argument, so the caller's number is replaced by 1.0.
	// `CrowFlyStep` is the pure half and takes the already-clamped scale.
	{
		// 100 units away, arriving threshold `scale * 170`: at scale 1 the body has arrived.
		const FElysiumNpc::FCrowFlyStep Near =
			FElysiumNpc::CrowFlyStep(1.f, FVector(100.0, 0.0, 0.0), FVector::ZeroVector);
		TestTrue(TEXT("100 units is inside the 170-unit arrival radius"), Near.bArrived);
		// The velocity is the RAW offset scaled by 170 — NOT normalised first.
		TestEqual(TEXT("and the velocity is the raw offset times 170"), Near.VelocityUnits,
			FVector(100.0 * 170.0, 0.0, 0.0));
		TestEqual(TEXT("with the motor yaw pointing along it"), Near.MotorYaw, 0.f, 1.0e-03f);

		const FElysiumNpc::FCrowFlyStep Far =
			FElysiumNpc::CrowFlyStep(1.f, FVector(400.0, 0.0, 0.0), FVector::ZeroVector);
		TestFalse(TEXT("400 units is outside it"), Far.bArrived);
		const FElysiumNpc::FCrowFlyStep Left =
			FElysiumNpc::CrowFlyStep(1.f, FVector(0.0, -400.0, 0.0), FVector::ZeroVector);
		TestEqual(TEXT("a Source +Y offset is yaw 90"), Left.MotorYaw, 90.f, 1.0e-03f);
		const FElysiumNpc::FCrowFlyStep Up =
			FElysiumNpc::CrowFlyStep(1.f, FVector(0.0, 0.0, 400.0), FVector::ZeroVector);
		TestEqual(TEXT("and a straight-up offset is pitch -90"), Up.Pitch, -90.f, 1.0e-03f);
	}

	// --- `0x1036c7f0`: the ChangBros setter writes family Squad's word ---------------------------
	Npc->ChangType = 0;
	Npc->FUN_1036c7f0(3);
	TestEqual(TEXT("0x1036c7f0 writes m_ChangType"), Npc->ChangType, 3);
	// It is a PLAIN setter — no clamp, no validation.
	Npc->FUN_1036c7f0(-9);
	TestEqual(TEXT("verbatim, with no clamp"), Npc->ChangType, -9);

	// --- `0x1039ef90`: the tentacle's cached coordinate point and condition 0x78 ------------------
	const EElysiumNpcCond Cond0x78 = static_cast<EElysiumNpcCond>(0x78);
	Npc->Cognition.Conditions.Clear(Cond0x78);
	Npc->FUN_1039ef90(FVector(7.0, 8.0, 9.0));
	TestTrue(TEXT("0x1039ef90 raises condition 0x78 — unnamed in the recovered vocabulary"),
		Npc->Cognition.Conditions.Has(Cond0x78));
	TestEqual(TEXT("and caches the three floats"), Npc->TentacleCoordinatePosUnits,
		FVector(7.0, 8.0, 9.0));

	// --- slots 21, 22 and 23: three slots, one body, and the head seam answers null ---------------
	Npc->TentacleHeadForwards = 0;
	Npc->FUN_1039e800(Other);
	Npc->FUN_1039e830(Other);
	Npc->FUN_1039e860(Other);
	TestEqual(TEXT("all three tentacle slots ask for the head"), Npc->TentacleHeadForwards, 3);
	TestNull(TEXT("and the seam answers null, so nothing is forwarded"),
		Npc->MingXiaoTentacleHead());
	for (int32 Slot : { 21, 22, 23 })
	{
		TestNotNull(*FString::Printf(TEXT("CNPC_VMingXiaoTentacle has a slot-%d row"), Slot),
			FElysiumNpc::SpeciesSlotRowOf(TEXT("CNPC_VMingXiaoTentacle"), Slot));
	}

	// --- `0x103b9180`, slot 593: the base first, then five literals ------------------------------
	Npc->TargetLeadMin = 999.f;
	Npc->TargetLeadMax = 999.f;
	Npc->TargetLeadCurrentWeight = 999.f;
	Npc->TargetLeadPredictedWeight = 999.f;
	Npc->TargetLeadWeightScale = 999.f;
	Npc->FUN_103b9180();
	TestEqual(TEXT("slot 593 writes m_flTargetLeadMin 0.01"), Npc->TargetLeadMin, 0.01f, 1.0e-06f);
	TestEqual(TEXT("m_flTargetLeadMax 1.0"), Npc->TargetLeadMax, 1.0f, 1.0e-06f);
	TestEqual(TEXT("m_flTargetLeadCurrentWeight 50"), Npc->TargetLeadCurrentWeight, 50.f, 1.0e-06f);
	TestEqual(TEXT("m_flTargetLeadPredictedWeight 50"), Npc->TargetLeadPredictedWeight, 50.f,
		1.0e-06f);
	TestEqual(TEXT("and m_flTargetLeadWeightScale 0.01"), Npc->TargetLeadWeightScale, 0.01f,
		1.0e-06f);

	// --- `0x1034d6e0`: `CScriptedTarget::Spawn` caches the origin --------------------------------
	Npc->Origin = FVector(254.0, 0.0, 0.0);   // 100 SOURCE units
	Npc->ScriptedTargetLastPositionUnits = FVector::ZeroVector;
	Npc->FUN_1034d6e0();
	TestEqual(TEXT("CScriptedTarget::Spawn caches m_vLastPosition in SOURCE units"),
		Npc->ScriptedTargetLastPositionUnits, FVector(100.0, 0.0, 0.0));
	TestNull(TEXT("and CScriptedTarget is a census class with no entity classname"),
		ElysiumNpcKernelClass::OfClassname(TEXT("scripted_target")));
	TestNotNull(TEXT("though the class itself is in the census"),
		ElysiumNpcKernelClass::Find(TEXT("CScriptedTarget")));

	// --- `0x103681d0` / `0x103682f0`: two EMPTY bodies, and emptiness is the point ---------------
	// The rows exist so the base sound hooks do NOT run for a camera. Nothing to assert but that
	// the dispatcher reaches them for the right class and no other.
	TestNotNull(TEXT("CNPC_VCamera has a slot-497 row"),
		FElysiumNpc::SpeciesSlotRowOf(TEXT("CNPC_VCamera"), 497));
	TestNotNull(TEXT("and a slot-506 row"),
		FElysiumNpc::SpeciesSlotRowOf(TEXT("CNPC_VCamera"), 506));
	TestFalse(TEXT("a runner does not take the camera's empty slot 497"), Npc->SpeciesSlot497());
	TestFalse(TEXT("nor its empty slot 506"), Npc->SpeciesSlot506());

	// --- `0x103c3fd0`, slot 588: the base's IsActivityFinished gate is GONE ----------------------
	// The restart is unconditional; `RestartIdealActivityId` is family Hints' seam and records it.
	Npc->FUN_103c3fd0();
	TestTrue(TEXT("a runner has a slot-588 row"),
		FElysiumNpc::SpeciesSlotRowOf(TEXT("CNPC_VTzimisceRunner"), 588) != nullptr);
	TestTrue(TEXT("and the dispatcher runs it"), Npc->SpeciesSlot588());

	// --- `0x103b92a0`, slot 488: three singleton seams, all refusing, event still fired ----------
	int32 Argument = -1;
	TestFalse(TEXT("the SPI_DIES singleton seam refuses"),
		Npc->TzimisceDeathScriptArgument(0, Argument));
	TestEqual(TEXT("substituting zero, which is retail's own arm"), Argument, 0);
	Npc->FUN_103b92a0();   // fires the recorded event; the tail call is slot 487's

	// --- `0x103bf560`: the motor yaw release ------------------------------------------------------
	Npc->FUN_103bf560();   // reaches family Hints' ReleaseMotorHintYaw seam
	return true;
}

#endif   // WITH_DEV_AUTOMATION_TESTS
