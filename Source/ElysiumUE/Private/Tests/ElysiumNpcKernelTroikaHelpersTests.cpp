#include "Misc/AutomationTest.h"

#include "Tests/ElysiumArmTier.h"

#if WITH_DEV_AUTOMATION_TESTS && ELYSIUM_WITH_ARM_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "Substrate/ElysiumAttackCoordinator.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumSchedule.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumNpcTestCensus.h"

// Story 29c-1, family **TroikaHelpers**. Every assertion below is read off the decompiled C of the
// body it names — the threshold, the arm order, the id, what is written — never off 29c's one-line
// walk, which this family found wrong in four places (slots 599/600's draw bounds, slot 334's flag
// polarity, slot 610's case 2, and `CAI_Motor#17`'s two "clamps", which are both floors).
//
// Every classname builds the class retail's factory builds (story 5 step 2,
// `docs/vtmb/npc-ai/population.md`, "The classname → class map, read from the factories"):
// `npc_VCop`'s factory `0x103704f0` builds `CNPC_VCop`, so a spawned cop is NOT the Troika line. A
// case asserting the Troika-line body with no species override in the way stands a bare
// `CAI_BaseNPCTroika` (`AddTroikaNpc`), whose `RetailClass()` is null. A species row is also
// exercised through `ElysiumNpcTestCensus::Find` and the table's own lookup.

static constexpr EAutomationTestFlags GTroikaHelpersTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	constexpr float TroikaTestU = ElysiumMove::U;
}

// -------------------------------------------------------------------------------------------------
// The melee quartet's species line — slots 599/600/601/602, `0x102b5650` vs `0x10385ab0` and the
// three siblings.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelTroikaHelpersMeleeLineTest,
	"Elysium.Arm.NpcKernelTroikaHelpers.MeleeSlotLine", GTroikaHelpersTestFlags)
bool FElysiumNpcKernelTroikaHelpersMeleeLineTest::RunTest(const FString&)
{
	// The address table, checkable against `docs/vtmb/npc-kernel/slots.md` by eye.
	TestEqual(TEXT("599 Troika"),
		FString(FElysiumNpc::MeleeSlotBody(599, FElysiumNpc::EMeleeSlotLine::Troika)),
		FString(TEXT("0x102b5650")));
	TestEqual(TEXT("599 AndreiBlood"),
		FString(FElysiumNpc::MeleeSlotBody(599, FElysiumNpc::EMeleeSlotLine::AndreiBlood)),
		FString(TEXT("0x10385ab0")));
	TestEqual(TEXT("602 Troika"),
		FString(FElysiumNpc::MeleeSlotBody(602, FElysiumNpc::EMeleeSlotLine::Troika)),
		FString(TEXT("0x102b5900")));
	TestEqual(TEXT("602 AndreiBlood"),
		FString(FElysiumNpc::MeleeSlotBody(602, FElysiumNpc::EMeleeSlotLine::AndreiBlood)),
		FString(TEXT("0x10385d70")));

	// The census itself: `CNPC_VVampire` takes the `CNPC_VAndreiBlood` line at all four slots and
	// `CNPC_VRat` the Troika line. Exercised by RETAIL CLASS NAME so the claim does not depend on a
	// classname being spawnable.
	const FElysiumNpcClass* Vampire = ElysiumNpcTestCensus::Find(TEXT("CNPC_VVampire"));
	const FElysiumNpcClass* Rat = ElysiumNpcTestCensus::Find(TEXT("CNPC_VRat"));
	if (Vampire == nullptr || Rat == nullptr)
	{
		AddError(TEXT("the census carries neither CNPC_VVampire nor CNPC_VRat"));
		return false;
	}
	for (const int32 Slot : { 599, 600, 601, 602 })
	{
		TestEqual(TEXT("CNPC_VVampire is on the AndreiBlood line"),
			FString(ElysiumNpcTestCensus::BodyOf(Vampire, Slot)),
			FString(FElysiumNpc::MeleeSlotBody(Slot, FElysiumNpc::EMeleeSlotLine::AndreiBlood)));
		TestEqual(TEXT("CNPC_VRat is on the Troika line"),
			FString(ElysiumNpcTestCensus::BodyOf(Rat, Slot)),
			FString(FElysiumNpc::MeleeSlotBody(Slot, FElysiumNpc::EMeleeSlotLine::Troika)));
	}

	FElysiumNpcWorldBuilder Builder(TEXT("troika_meleeline"), 0x29c1701a);
	Builder.AddNpc(TEXT("vamp"), FVector::ZeroVector, TEXT("npc_VVampire"));
	Builder.AddNpc(TEXT("rat"), FVector(200.0, 0.0, 0.0), TEXT("npc_VRat"));
	Builder.AddNpc(TEXT("cop"), FVector(400.0, 0.0, 0.0), TEXT("npc_VCop"));
	Builder.AddNpc(TEXT("phone"), FVector(600.0, 0.0, 0.0), TEXT("npc_payphone"));
	Builder.AddTroikaNpc(TEXT("troika"), FVector(800.0, 0.0, 0.0));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Vamp = Fixture.Npc(TEXT("vamp"));
	FElysiumNpc* RatNpc = Fixture.Npc(TEXT("rat"));
	FElysiumNpc* Cop = Fixture.Npc(TEXT("cop"));
	FElysiumNpc* Troika = Fixture.Npc(TEXT("troika"));
	FElysiumNpcWorldFixture::Quiet({ Vamp, RatNpc, Cop, Troika });
	if (Vamp == nullptr || RatNpc == nullptr || Cop == nullptr || Troika == nullptr)
	{
		AddError(TEXT("fixture did not stand the three spawn leaves and the bare Troika NPC"));
		return false;
	}

	TestTrue(TEXT("npc_VVampire resolves to a census class"), Vamp->RetailClass() != nullptr);
	TestEqual(TEXT("and takes the AndreiBlood line at 602"),
		FString(Vamp->RetailClass() != nullptr
			? ElysiumNpcTestCensus::BodyOf(Vamp->RetailClass(), 602) : FElysiumNpc::MeleeSlotBody(602, FElysiumNpc::EMeleeSlotLine::Troika)),
		FString(FElysiumNpc::MeleeSlotBody(602, FElysiumNpc::EMeleeSlotLine::AndreiBlood)));
	TestEqual(TEXT("npc_VRat takes the Troika line at 602"),
		FString(RatNpc->RetailClass() != nullptr
			? ElysiumNpcTestCensus::BodyOf(RatNpc->RetailClass(), 602) : FElysiumNpc::MeleeSlotBody(602, FElysiumNpc::EMeleeSlotLine::Troika)),
		FString(FElysiumNpc::MeleeSlotBody(602, FElysiumNpc::EMeleeSlotLine::Troika)));

	// The bare Troika line: no species class, so the Troika-line body runs.
	TestNull(TEXT("the bare Troika line has no species class"), Troika->RetailClass());
	TestEqual(TEXT("so a bare Troika NPC falls through to the Troika line, which is the recovered "
		"answer"),
		FString(Troika->RetailClass() != nullptr
			? ElysiumNpcTestCensus::BodyOf(Troika->RetailClass(), 602) : FElysiumNpc::MeleeSlotBody(602, FElysiumNpc::EMeleeSlotLine::Troika)),
		FString(FElysiumNpc::MeleeSlotBody(602, FElysiumNpc::EMeleeSlotLine::Troika)));

	// story 5 step 2: npc_VCop's factory 0x103704f0 builds CNPC_VCop (population.md), and the
	// census row puts CNPC_VCop on the `CNPC_VAndreiBlood` line at 599..602 (`0x10385d70` at 602,
	// `docs/vtmb/npc-kernel/slots.md`).
	const FElysiumNpcClass* CopClass = ElysiumNpcTestCensus::Find(TEXT("CNPC_VCop"));
	TestTrue(TEXT("npc_VCop's RetailClass is CNPC_VCop — its factory builds it"),
		CopClass != nullptr && Cop->RetailClass() == CopClass);
	TestEqual(TEXT("CNPC_VCop's 602 body is the AndreiBlood line's"),
		FString(ElysiumNpcTestCensus::BodyOf(CopClass, 602)),
		FString(FElysiumNpc::MeleeSlotBody(602, FElysiumNpc::EMeleeSlotLine::AndreiBlood)));
	TestEqual(TEXT("so a cop takes the AndreiBlood line at 602"),
		FString(Cop->RetailClass() != nullptr
			? ElysiumNpcTestCensus::BodyOf(Cop->RetailClass(), 602) : FElysiumNpc::MeleeSlotBody(602, FElysiumNpc::EMeleeSlotLine::Troika)),
		FString(FElysiumNpc::MeleeSlotBody(602, FElysiumNpc::EMeleeSlotLine::AndreiBlood)));
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 599 `0x102b5650` and slot 600 `0x102b57c0`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelTroikaHelpersEnterMeleeTest,
	"Elysium.Arm.NpcKernelTroikaHelpers.EnterMelee", GTroikaHelpersTestFlags)
bool FElysiumNpcKernelTroikaHelpersEnterMeleeTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("troika_entermelee"), 0x29c1701b);
	Builder.AddNpc(TEXT("thug"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Thug = Fixture.Npc(TEXT("thug"));
	FElysiumNpcWorldFixture::Quiet({ Thug });
	if (Thug == nullptr)
	{
		AddError(TEXT("fixture did not stand the NPC"));
		return false;
	}

	// Arm 1: `m_bfNPCFrenziedFlags & 2` forces melee outright and sets `m_bInMelee` on the way.
	Thug->SetFrenziedWord(0x2);
	TestTrue(TEXT("frenzy bit 0x2 enters melee outright"), Thug->Slot599(0));
	TestTrue(TEXT("and m_bInMelee is set"), Thug->bInMelee);
	Thug->SetFrenziedWord(0);

	// Arm 2: the can-enter timer. `curtime < m_flMeleeCanEnterTimer` refuses AND clears the latch.
	Thug->bInMelee = true;
	Thug->MeleeCanEnterTimer = Thug->World->NowSeconds() + 100.0;
	TestFalse(TEXT("a live can-enter timer refuses"), Thug->Slot599(0));
	TestFalse(TEXT("and clears m_bInMelee"), Thug->bInMelee);

	// Arm 3: past the timer, the height term is decided by `ENEMY_UNREACHABLE` (the limit is
	// `_DAT_10451acc` = 64 and the enemy is 5000 units above it); the range term passes because
	// slot 308 is a stub answering false.
	Thug->MeleeCanEnterTimer = 0.0;
	Thug->ScheduleHost.EnemyHeightDiffUnits = 5000.f;
	Thug->Cognition.Conditions.Set(EElysiumNpcCond::EnemyUnreachable);
	TestFalse(TEXT("an unreachable enemy above the height limit refuses"), Thug->Slot599(0));
	Thug->Cognition.Conditions.Clear(EElysiumNpcCond::EnemyUnreachable);

	// The coordinator term with NO object (`m_pAttackCoordinator` 0, retail's fault path): refused.
	Thug->AttackCoordinator = 0;
	TestFalse(TEXT("with no coordinator object 0x1025db70 refuses"), Thug->Slot599(0));
	TestFalse(TEXT("and m_bInMelee is cleared"), Thug->bInMelee);

	// Arm 4: frenzy bit 0x1000 bypasses the coordinator entirely — and the accepting arm arms the
	// must-leave timer from `RandomFloat(7.5, 15.0)`, NOT `RandomFloat(4, 15)` as 29c's walk reads.
	const int32 EventsBefore = Thug->MeleeEventFires;
	Thug->SetFrenziedWord(0x1000);
	const double Now = Thug->World->NowSeconds();
	TestTrue(TEXT("the 0x1000 coordinator bypass enters melee"), Thug->Slot599(0));
	TestTrue(TEXT("m_bInMelee is set"), Thug->bInMelee);
	TestTrue(TEXT("the must-leave timer is at least curtime + 7.5"),
		Thug->MeleeMustLeaveTimer >= Now + 7.5 - 0.001);
	TestTrue(TEXT("and at most curtime + 15.0"), Thug->MeleeMustLeaveTimer <= Now + 15.0 + 0.001);
	TestEqual(TEXT("the global melee event fired exactly once"), Thug->MeleeEventFires,
		EventsBefore + 1);
	Thug->SetFrenziedWord(0);

	// The coordinator term WITH the object (spec 0002 V11-1): `0x1025db70` appends this NPC to
	// "Normal" and the same accepting arm runs.
	FElysiumAttackCoordinator* const Normal = Thug->World->AttackCoordinator(1);
	if (Normal == nullptr)
	{
		AddError(TEXT("the world stands no coordinator 1"));
		return false;
	}
	Normal->Reset();
	Thug->AttackCoordinator = 1;
	Thug->bInMelee = false;
	TestTrue(TEXT("an empty coordinator admits (0x1025db70)"), Thug->Slot599(0));
	TestTrue(TEXT("m_bInMelee is set"), Thug->bInMelee);
	TestFalse(TEXT("and the NPC is in the list"), Normal->IsAbsent(Thug));
	TestEqual(TEXT("once"), Normal->Num(), 1);
	TestTrue(TEXT("a second slot 599 finds it listed and answers true with no insert"), Thug->Slot599(0));
	TestEqual(TEXT("still once"), Normal->Num(), 1);
	Normal->Reset();

	// Slot 600 (`0x102b57c0`): the `m_bInMelee == 0` test is on the way IN, so a body already in
	// melee falls straight out and writes NOTHING — not even the `m_bInMelee = 0` inside the gate.
	Thug->bInMelee = true;
	const int32 EventsBeforeSlot600 = Thug->MeleeEventFires;
	TestFalse(TEXT("slot 600 refuses a body already in melee"), Thug->Slot600(nullptr));
	TestTrue(TEXT("and leaves m_bInMelee alone, because the write is INSIDE the gate"),
		Thug->bInMelee);
	TestEqual(TEXT("and fires no event"), Thug->MeleeEventFires, EventsBeforeSlot600);
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 601 `0x102b5880` / `0x10385cf0` and slot 602 `0x102b5900` / `0x10385d70`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelTroikaHelpersLeaveMeleeTest,
	"Elysium.Arm.NpcKernelTroikaHelpers.LeaveMelee", GTroikaHelpersTestFlags)
bool FElysiumNpcKernelTroikaHelpersLeaveMeleeTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("troika_leavemelee"), 0x29c1701c);
	Builder.AddNpc(TEXT("rat"), FVector::ZeroVector, TEXT("npc_VRat"));
	Builder.AddNpc(TEXT("vamp"), FVector(200.0, 0.0, 0.0), TEXT("npc_VVampire"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* TroikaNpc = Fixture.Npc(TEXT("rat"));
	FElysiumNpc* BloodNpc = Fixture.Npc(TEXT("vamp"));
	FElysiumNpcWorldFixture::Quiet({ TroikaNpc, BloodNpc });
	if (TroikaNpc == nullptr || BloodNpc == nullptr)
	{
		AddError(TEXT("fixture did not stand both leaves"));
		return false;
	}

	// Slot 601, the Troika arm: the event fires, `m_bInMelee` clears, and the coordinator release
	// is GUARDED by `m_pAttackCoordinator != 0`.
	TroikaNpc->bInMelee = true;
	TroikaNpc->AttackCoordinator = 0;
	int32 Events = TroikaNpc->MeleeEventFires;
	int32 Releases = TroikaNpc->MeleeCoordinatorReleases;
	TroikaNpc->Slot601(nullptr);
	TestFalse(TEXT("601 clears m_bInMelee"), TroikaNpc->bInMelee);
	TestEqual(TEXT("601 fires the global melee event once"), TroikaNpc->MeleeEventFires, Events + 1);
	TestEqual(TEXT("and with a null coordinator the Troika line releases nothing"),
		TroikaNpc->MeleeCoordinatorReleases, Releases);

	TroikaNpc->AttackCoordinator = 2;
	Releases = TroikaNpc->MeleeCoordinatorReleases;
	TroikaNpc->Slot601(nullptr);
	TestEqual(TEXT("with a coordinator set the Troika line does release"),
		TroikaNpc->MeleeCoordinatorReleases, Releases + 1);

	// Slot 601, the AndreiBlood arm: family Bosses' `FUN_10385cf0`, which releases WITHOUT the
	// guard. That is the whole of the difference between the two bodies; the event fires first on
	// BOTH, contrary to Bosses' note.
	BloodNpc->AttackCoordinator = 0;
	Releases = BloodNpc->MeleeCoordinatorReleases;
	Events = BloodNpc->MeleeEventFires;
	BloodNpc->Slot601(nullptr);
	TestEqual(TEXT("the AndreiBlood line releases even with a null coordinator"),
		BloodNpc->MeleeCoordinatorReleases, Releases + 1);
	TestEqual(TEXT("and fires the same single event"), BloodNpc->MeleeEventFires, Events + 1);

	// Slot 602 — the recovered divergence. The census is what states it: the two lines carry
	// DIFFERENT bodies, and `0x10385d70` drops the `m_pAttackCoordinator != 0` test.
	TestEqual(TEXT("npc_VRat is on the Troika line at 602"),
		FString(TroikaNpc->RetailClass() != nullptr
			? ElysiumNpcTestCensus::BodyOf(TroikaNpc->RetailClass(), 602) : FElysiumNpc::MeleeSlotBody(602, FElysiumNpc::EMeleeSlotLine::Troika)),
		FString(FElysiumNpc::MeleeSlotBody(602, FElysiumNpc::EMeleeSlotLine::Troika)));
	TestEqual(TEXT("npc_VVampire is on the AndreiBlood line at 602"),
		FString(BloodNpc->RetailClass() != nullptr
			? ElysiumNpcTestCensus::BodyOf(BloodNpc->RetailClass(), 602) : FElysiumNpc::MeleeSlotBody(602, FElysiumNpc::EMeleeSlotLine::Troika)),
		FString(FElysiumNpc::MeleeSlotBody(602, FElysiumNpc::EMeleeSlotLine::AndreiBlood)));

	// A null `m_pAttackCoordinator`: the Troika line's own third test refuses; the AndreiBlood copy
	// has no such test and FAULTS in retail (every coordinator entry point dereferences its `this`
	// at once), a state no spawned NPC is in (Precache binds "Normal"). The port answers false there.
	TroikaNpc->AttackCoordinator = 0;
	BloodNpc->AttackCoordinator = 0;
	TestFalse(TEXT("602 on the Troika line refuses on its own null-coordinator test"),
		TroikaNpc->Slot602());
	TestFalse(TEXT("602 on the AndreiBlood line answers false where retail faults"),
		BloodNpc->Slot602());

	// Both lines share the two gates in front of it, and both are tested BEFORE the coordinator.
	BloodNpc->AttackCoordinator = 2;
	BloodNpc->SetFrenziedWord(0x2);
	TestFalse(TEXT("frenzy bit 0x2 refuses on both lines"), BloodNpc->Slot602());
	BloodNpc->SetFrenziedWord(0);

	// With a coordinator in hand the tail runs and the two lines agree, which is retail's own
	// state. The must-leave arm is unreachable while slot 308 is a stub answering false, so the far
	// arm is what runs: `MeleeRange` is `debug_melee_advance_combatmove_dist`'s 100, so the doubled
	// range is 200.
	FElysiumAttackCoordinator* const Player = BloodNpc->World->AttackCoordinator(2);
	if (Player == nullptr)
	{
		AddError(TEXT("the world stands no coordinator 2"));
		return false;
	}
	Player->Reset();
	TroikaNpc->AttackCoordinator = 2;
	BloodNpc->ScheduleHost.EnemyDistUnits = 5000.f;
	TroikaNpc->ScheduleHost.EnemyDistUnits = 5000.f;
	// Not listed: there is room (0x1025db50), so the far arm passes and 0x1025de90 answers "absent".
	TestTrue(TEXT("an NPC the coordinator does not hold leaves (0x1025de90)"), BloodNpc->Slot602());
	TestTrue(TEXT("and the Troika line answers the same once its guard passes"),
		TroikaNpc->Slot602());
	// Listed, with room left: far or near, it stays.
	Player->Add(BloodNpc);
	TestFalse(TEXT("a held NPC stays while the coordinator has room"), BloodNpc->Slot602());
	// Listed and FULL: beyond twice the range it leaves, inside it stays.
	Player->Add(TroikaNpc);
	TestTrue(TEXT("held, full and at or beyond 2 x range: leave (0x1025db50 false)"),
		BloodNpc->Slot602());
	TestTrue(TEXT("the Troika line too"), TroikaNpc->Slot602());
	BloodNpc->ScheduleHost.EnemyDistUnits = 100.f;
	TestFalse(TEXT("held, full and inside 2 x range: stay"), BloodNpc->Slot602());

	// Slot 601 with the object behind the index: the release takes the NPC out (0x1025ddd0).
	BloodNpc->Slot601(nullptr);
	TestTrue(TEXT("601 on the AndreiBlood line released the slot"), Player->IsAbsent(BloodNpc));
	TroikaNpc->Slot601(nullptr);
	TestTrue(TEXT("601 on the Troika line released it behind its guard"), Player->IsAbsent(TroikaNpc));
	TestEqual(TEXT("the list is empty"), Player->Num(), 0);
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slots 54, 56, 322, 597.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelTroikaHelpersNotifySlotsTest,
	"Elysium.Arm.NpcKernelTroikaHelpers.NotifySlots", GTroikaHelpersTestFlags)
bool FElysiumNpcKernelTroikaHelpersNotifySlotsTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("troika_notify"), 0x29c1701d);
	Builder.AddNpc(TEXT("guard"));
	Builder.AddNpc(TEXT("foe"), FVector(300.0, 0.0, 0.0));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	FElysiumNpc* Foe = Fixture.Npc(TEXT("foe"));
	FElysiumNpcWorldFixture::Quiet({ Guard, Foe });
	if (Guard == nullptr || Foe == nullptr)
	{
		AddError(TEXT("fixture did not stand both NPCs"));
		return false;
	}

	// Slot 54 `0x102b50b0`. A null argument, a non-enemy argument and an NPC with no installed
	// program all answer TRUE; only "this IS my enemy, a program is running, and its mask does not
	// list LOST_ENEMY" answers false.
	TestTrue(TEXT("54 answers true for a null argument"), Guard->Slot54(nullptr));
	TestTrue(TEXT("54 answers true for an entity that is not my enemy"), Guard->Slot54(Foe));
	Guard->BaseMemory.Enemy = Foe->Handle;
	TestTrue(TEXT("54 answers true for my enemy while no program is installed"), Guard->Slot54(Foe));
	Guard->Schedule.Current = ElysiumScheduleGlobalId(ElysiumSched::IDLE_STAND);
	TestEqual(TEXT("54's answer is the mask test, not a condition test"), Guard->Slot54(Foe),
		ElysiumSchedule::MaskHasCondition(Guard->Schedule, *Guard, EElysiumNpcCond::LostEnemy));
	Guard->Schedule.Current = ElysiumScheduleId::None;

	// Slot 56 `0x102b5120`. The `+0x200` seam refuses, so `m_hLastEnemy` survives even when the
	// argument IS the last enemy — which is the recovered refusal and not an omission.
	Guard->BaseMemory.LastEnemy = Foe->Handle;
	Guard->Slot56(Foe, FVector::ZeroVector, FVector::ZeroVector, TEXT("x"));
	Guard->Slot56(nullptr, FVector::ZeroVector, FVector::ZeroVector, nullptr);
	TestTrue(TEXT("and a null argument returns before anything"),
		Guard->BaseMemory.LastEnemy.IsSet());

	// Slot 322 `0x102a0910` — the ally notice and then slot 600 through the vtable. Slot 600 is
	// asserted through its own observable: with no capability bits it must write nothing.
	Guard->bInMelee = true;
	const int32 Events = Guard->MeleeEventFires;
	Guard->Slot322(Foe);
	TestTrue(TEXT("322's slot-600 dispatch left m_bInMelee alone"), Guard->bInMelee);
	TestEqual(TEXT("and fired no melee event"), Guard->MeleeEventFires, Events);

	// Slot 597 `0x102b4fb0` — `AddEntityRelationship(other, D_HT, priority)`. The literal 1 is the
	// whole of what makes this more than a forward.
	Guard->Slot597(Foe, 7);
	EElysiumRelationship Value = EElysiumRelationship::Neutral;
	int32 Priority = 0;
	TestTrue(TEXT("597 wrote an exact-entity row"),
		Guard->Relationships.ResolveRow(Foe->Handle,
			Foe->Def ? Foe->Def->Classname : FString(), Value, Priority));
	TestEqual(TEXT("597's relation is D_HT"), static_cast<int32>(Value),
		static_cast<int32>(EElysiumRelationship::Hate));
	TestEqual(TEXT("597 passes the caller's priority through"), Priority, 7);
	Guard->Slot597(nullptr, 3);
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 334 `0x10330020` and slot 616 `0x102ad110`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelTroikaHelpersDisciplineTest,
	"Elysium.Arm.NpcKernelTroikaHelpers.DisciplineAndFire", GTroikaHelpersTestFlags)
bool FElysiumNpcKernelTroikaHelpersDisciplineTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("troika_discipline"), 0x29c1701e);
	Builder.AddNpc(TEXT("caster"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Caster = Fixture.Npc(TEXT("caster"));
	FElysiumNpcWorldFixture::Quiet({ Caster });
	if (Caster == nullptr)
	{
		AddError(TEXT("fixture did not stand the NPC"));
		return false;
	}

	// `m_iCurFrenzyCount > 0` refuses outright and does NOT touch the global flag — the clear is
	// the SECOND statement of the body, after that return.
	Caster->CurFrenzyCount = 1;
	TestFalse(TEXT("334 refuses while m_iCurFrenzyCount is positive"), Caster->Slot334(3, 1));
	Caster->CurFrenzyCount = 0;

	// The table seam misses, which is retail's `0xffffffff` — and a MISS answers TRUE with the
	// global flag left CLEAR. 29c's walk has that polarity inverted.
	TestTrue(TEXT("334 answers true when the discipline table does not carry the id"),
		Caster->Slot334(3, 1));
	TestFalse(TEXT("and the global ready flag stays clear on the miss arm"),
		FElysiumNpc::DisciplineReadyFlag());

	// Slot 616 — clear `COND_ON_FIRE` and stamp the immunity window.
	Caster->Cognition.Conditions.Set(EElysiumNpcCond::OnFire);
	Caster->NextBurnTime = -1.0;
	Caster->Slot616();
	TestFalse(TEXT("616 clears COND_ON_FIRE"),
		Caster->Cognition.Conditions.Has(EElysiumNpcCond::OnFire));
	TestEqual(TEXT("616 stamps m_flNextBurnTime with curtime + the 15 s _DAT_10463584 window"),
		Caster->NextBurnTime, Caster->World->NowSeconds() + 15.0, 0.001);
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 606 `0x102b8320` — the occlusion ladder.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelTroikaHelpersOccludeTest,
	"Elysium.Arm.NpcKernelTroikaHelpers.OcclusionLadder", GTroikaHelpersTestFlags)
bool FElysiumNpcKernelTroikaHelpersOccludeTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("troika_occlude"), 0x29c1701f);
	Builder.AddNpc(TEXT("shooter"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Shooter = Fixture.Npc(TEXT("shooter"));
	FElysiumNpcWorldFixture::Quiet({ Shooter });
	if (Shooter == nullptr)
	{
		AddError(TEXT("fixture did not stand the NPC"));
		return false;
	}

	// The gate: no `COND_ENEMY_OCCLUDED`, no answer.
	TestEqual(TEXT("606 answers 0 without COND_ENEMY_OCCLUDED"), Shooter->Slot606(0), 0);
	Shooter->Cognition.Conditions.Set(EElysiumNpcCond::EnemyOccluded);

	// `COND_ENEMY_UNREACHABLE` -> 0xaa, ahead of every roll.
	Shooter->Cognition.Conditions.Set(EElysiumNpcCond::EnemyUnreachable);
	TestEqual(TEXT("an unreachable enemy answers 0xaa"), Shooter->Slot606(0), 0xaa);
	Shooter->Cognition.Conditions.Clear(EElysiumNpcCond::EnemyUnreachable);

	// Frenzied bit 0x100 -> 0xb4, still ahead of every roll.
	Shooter->SetFrenziedWord(0x100);
	TestEqual(TEXT("frenzied bit 0x100 answers 0xb4"), Shooter->Slot606(0), 0xb4);
	Shooter->SetFrenziedWord(0);

	// `D_POSSESSED` rolls at 0x46 and answers one of the two; `FORCED_OCCLUDE` rolls at 0x50 and
	// CLEARS itself on the way out, which the next call proves.
	Shooter->NpcFlags.Set(EElysiumNpcFlag2::D_POSSESSED);
	const int32 Possessed = Shooter->Slot606(0);
	TestTrue(TEXT("the possessed arm answers 0xb6 or 0xb4"),
		Possessed == 0xb6 || Possessed == 0xb4);
	TestTrue(TEXT("and does NOT clear D_POSSESSED"),
		Shooter->NpcFlags.Has(EElysiumNpcFlag2::D_POSSESSED));
	Shooter->NpcFlags.Clear(EElysiumNpcFlag2::D_POSSESSED);

	Shooter->NpcFlags.Set(EElysiumNpcFlag::FORCED_OCCLUDE);
	const int32 Forced = Shooter->Slot606(0);
	TestTrue(TEXT("the forced-occlude arm answers 0xb6 or 0xb4"), Forced == 0xb6 || Forced == 0xb4);
	TestFalse(TEXT("and CLEARS FORCED_OCCLUDE, which retail does and the possessed arm does not"),
		Shooter->NpcFlags.Has(EElysiumNpcFlag::FORCED_OCCLUDE));

	// The two threshold tables. A threshold of 100 puts every `RandomInt(0, 99)` in the first
	// bucket, which pins the answer without pinning the draw.
	Shooter->PercentOccludedWait = 100;
	TestEqual(TEXT("the non-squad table's first bucket is 0xaa"), Shooter->Slot606(0), 0xaa);
	Shooter->Cognition.Conditions.Set(EElysiumNpcCond::SquadSeeEnemy);
	TestEqual(TEXT("COND_SQUAD_SEE_ENEMY selects the other table, whose first bucket is 0xab"),
		Shooter->Slot606(0), 0xab);

	// A threshold of 0 falls past every bucket to the table's own tail: 0xb2 with the squad
	// condition, 0xb4 without. The squad table's last TWO answers are the same number, which is
	// retail's and is not folded.
	Shooter->PercentOccludedWait = 0;
	Shooter->PercentOccludedCover = 0;
	Shooter->PercentOccludedWalk = 0;
	Shooter->PercentOccludedFlank = 0;
	TestEqual(TEXT("the squad table's tail is 0xb2"), Shooter->Slot606(0), 0xb2);
	Shooter->Cognition.Conditions.Clear(EElysiumNpcCond::SquadSeeEnemy);
	TestEqual(TEXT("the non-squad table's tail is 0xb4"), Shooter->Slot606(0), 0xb4);
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 607 `0x102b93c0` — the follower-distance ladder.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelTroikaHelpersFollowerTest,
	"Elysium.Arm.NpcKernelTroikaHelpers.FollowerDistance", GTroikaHelpersTestFlags)
bool FElysiumNpcKernelTroikaHelpersFollowerTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("troika_follower"), 0x29c17020);
	Builder.AddNpc(TEXT("follower"));
	Builder.AddNpc(TEXT("boss"), FVector(100.0 * TroikaTestU, 0.0, 0.0));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Follower = Fixture.Npc(TEXT("follower"));
	FElysiumNpc* Boss = Fixture.Npc(TEXT("boss"));
	FElysiumNpcWorldFixture::Quiet({ Follower, Boss });
	if (Follower == nullptr || Boss == nullptr)
	{
		AddError(TEXT("fixture did not stand both NPCs"));
		return false;
	}

	// No follower boss: 0, and nothing written.
	TestEqual(TEXT("607 answers 0 with no follower boss"), Follower->Slot607(), 0);

	Follower->FollowerBoss = Boss->Handle;
	// The boss is 100 SOURCE units away. Back-away 200 puts it inside the first ring.
	Follower->FollowerDistanceBackAway = 200.f;
	Follower->FollowerDistanceWalkTo = 20.f;
	Follower->FollowerDistanceRunTo = 10.f;
	TestEqual(TEXT("inside the back-away ring answers 0x10c"), Follower->Slot607(), 0x10c);
	TestEqual(TEXT("and stamps m_vSavePosition with the boss's own origin, in centimetres"),
		Follower->SavePosition.X, Boss->Origin.X, 0.01);

	// Outside back-away and outside run-to: 0x113, and the target is committed.
	Follower->FollowerDistanceBackAway = 10.f;
	Follower->FollowerDistanceRunTo = 50.f;
	Follower->FollowerDistanceWalkTo = 20.f;
	Follower->SetTarget(FElysiumEntityHandle());
	TestEqual(TEXT("beyond the run-to ring answers 0x113"), Follower->Slot607(), 0x113);
	TestTrue(TEXT("and SetTarget committed the boss"), Follower->GetTarget().IsSet());

	// Between run-to and walk-to. Retail tests RUN first, then WALK, so a run-to ABOVE the distance
	// and a walk-to below it lands on 0x112.
	Follower->FollowerDistanceRunTo = 500.f;
	Follower->FollowerDistanceWalkTo = 50.f;
	TestEqual(TEXT("between the two rings answers 0x112"), Follower->Slot607(), 0x112);

	// Inside both: 0x115.
	Follower->FollowerDistanceWalkTo = 500.f;
	TestEqual(TEXT("inside both rings answers 0x115"), Follower->Slot607(), 0x115);

	// The back-away arm's facing request is behind `debug_allow_move_facing`, which ships 1: the
	// request is queued on slot 517's path, with retail's 1.0 / 1.0 / 0 arguments.
	TestTrue(TEXT("debug_allow_move_facing ships on"), Follower->FacingTargetsEnabled());
	Follower->FacingTargetRequests.Reset();
	Follower->FollowerDistanceBackAway = 200.f;
	Follower->Slot607();
	TestEqual(TEXT("so the back-away arm queues one facing request"),
		Follower->FacingTargetRequests.Num(), 1);
	if (Follower->FacingTargetRequests.Num() == 1)
	{
		TestTrue(TEXT("on the boss"), Follower->FacingTargetRequests[0].Target == Boss->Handle);
		TestEqual(TEXT("for 1.0 s"), Follower->FacingTargetRequests[0].Duration, 1.0f);
	}
	// Cleared, the request is not made.
	ElysiumNpcTunables::SetConVar(ElysiumNpcTunables::EConVar::DebugAllowMoveFacing, 0.f);
	Follower->FacingTargetRequests.Reset();
	Follower->Slot607();
	TestEqual(TEXT("with the cvar cleared nothing is queued"), Follower->FacingTargetRequests.Num(), 0);
	ElysiumNpcTunables::ResetConVars();
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 608 `0x102c48b0` over the world's three coordinators (spec 0002 V11-1).
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelTroikaHelpersBindNormalTest,
	"Elysium.Arm.NpcKernelTroikaHelpers.BindNormal", GTroikaHelpersTestFlags)
bool FElysiumNpcKernelTroikaHelpersBindNormalTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("troika_bindnormal"), 0x29c17031);
	Builder.AddNpc(TEXT("npc"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("npc"));
	FElysiumNpcWorldFixture::Quiet({ Npc });
	if (Npc == nullptr)
	{
		AddError(TEXT("fixture did not stand the NPC"));
		return false;
	}

	// `0x1025e120` through the world's objects: the three names of `0x1025d880`, in its order.
	TestEqual(TEXT("coordinator 1 is DAT_1090fbec"), Npc->AttackCoordinatorNameOf(1), FString(TEXT("Normal")));
	TestEqual(TEXT("coordinator 2 is DAT_1090fbf0"), Npc->AttackCoordinatorNameOf(2), FString(TEXT("Player")));
	TestEqual(TEXT("coordinator 3 is DAT_1090fbf4"), Npc->AttackCoordinatorNameOf(3), FString(TEXT("Boss")));
	TestTrue(TEXT("index 0 is retail's null: no name"), Npc->AttackCoordinatorNameOf(0).IsEmpty());

	// `0x102c48b0`: the first name match writes `+0x65e8` and `+0x65ec`.
	Npc->AttackCoordinator = 0;
	Npc->AttackCoordinatorName.Reset();
	TestTrue(TEXT("608 binds \"Normal\" (Precache 0x10298ad0's literal)"), Npc->Slot608(TEXT("Normal")));
	TestEqual(TEXT("m_pAttackCoordinator names the first global"), Npc->AttackCoordinator, 1);
	TestEqual(TEXT("m_sAttackCoordinatorName is its name"), Npc->AttackCoordinatorName, FString(TEXT("Normal")));
	TestTrue(TEXT("608 binds \"Boss\" too"), Npc->Slot608(TEXT("Boss")));
	TestEqual(TEXT("the third global"), Npc->AttackCoordinator, 3);
	TestFalse(TEXT("an unknown name binds nothing"), Npc->Slot608(TEXT("melee_north")));
	TestEqual(TEXT("and leaves the last binding"), Npc->AttackCoordinator, 3);
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slots 608, 609, 610, 611, 612.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelTroikaHelpersTailSlotsTest,
	"Elysium.Arm.NpcKernelTroikaHelpers.TailSlots", GTroikaHelpersTestFlags)
bool FElysiumNpcKernelTroikaHelpersTailSlotsTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("troika_tail"), 0x29c17021);
	Builder.AddNpc(TEXT("npc"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("npc"));
	FElysiumNpcWorldFixture::Quiet({ Npc });
	if (Npc == nullptr)
	{
		AddError(TEXT("fixture did not stand the NPC"));
		return false;
	}

	// Slot 608 `0x102c48b0`. The two guards first, then the walk over the three globals; a name
	// none of them carries binds nothing (`BindNormal` below asserts the three that match).
	TestFalse(TEXT("608 refuses a null name"), Npc->Slot608(nullptr));
	TestFalse(TEXT("608 refuses an empty name"), Npc->Slot608(TEXT("")));
	Npc->AttackCoordinator = 0;
	TestFalse(TEXT("608 refuses a name no global answers to"), Npc->Slot608(TEXT("melee_north")));
	TestEqual(TEXT("and leaves m_pAttackCoordinator alone"), Npc->AttackCoordinator, 0);
	int32 Count = 0;
	const int32* Indices = FElysiumNpc::AttackCoordinatorIndices(Count);
	TestEqual(TEXT("retail walks exactly three coordinator globals"), Count, 3);
	TestEqual(TEXT("and the first is index 1"), Indices[0], 1);

	// Slot 609 `0x102b6b50`. The cached-hint arm, then the wait, then the re-roll and the search.
	Npc->ScheduleHost.ShootAtHintNode = 0;
	Npc->ScheduleHost.NextShootAtHintSearchTime = Npc->World->NowSeconds() + 100.0;
	TestEqual(TEXT("609 waits while the search deadline has not passed"),
		Npc->FindShootAtHintNode(false), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("and does not re-roll the deadline on the wait arm"),
		Npc->ScheduleHost.NextShootAtHintSearchTime, Npc->World->NowSeconds() + 100.0, 0.001);

	const double Now = Npc->World->NowSeconds();
	TestEqual(TEXT("609 with bForce skips the wait and still finds nothing (no hint store)"),
		Npc->FindShootAtHintNode(true), static_cast<int32>(INDEX_NONE));
	TestTrue(TEXT("and re-rolls the deadline into curtime + 2.0..2.5"),
		Npc->ScheduleHost.NextShootAtHintSearchTime >= Now + 2.0 - 0.001
			&& Npc->ScheduleHost.NextShootAtHintSearchTime <= Now + 2.5 + 0.001);
	TestNull(TEXT("the slot itself answers null — a hint is an index here, not an object"),
		Npc->Slot609(true));

	// Slot 610 `0x102adfe0`. Retail's COMBAT (2) and ALERT (3) arms share the two Anger names and
	// differ ONLY in the blend weight — 1.0 and 0.5. That is the arm 29c's walk folds away.
	Npc->Slot610(EElysiumNpcState::Combat);
	TestEqual(TEXT("610 COMBAT resolves Anger"), Npc->DefExpression, FString(TEXT("Anger")));
	TestEqual(TEXT("610 COMBAT resolves Anger_No_Deform"), Npc->NoDeformExpression,
		FString(TEXT("Anger_No_Deform")));
	TestEqual(TEXT("610 COMBAT blends at 1.0"), Npc->ExpressionBlendWeight, 1.0f, 0.0001f);
	Npc->Slot610(EElysiumNpcState::Alert);
	TestEqual(TEXT("610 ALERT resolves the same pair"), Npc->DefExpression, FString(TEXT("Anger")));
	TestEqual(TEXT("610 ALERT blends at 0.5, which is the whole difference"),
		Npc->ExpressionBlendWeight, 0.5f, 0.0001f);
	// The default arm's table is a seam, so the three words survive it untouched.
	Npc->Slot610(EElysiumNpcState::Idle);

	// Slot 611 `0x102c12a0` — the stance selector, which the port already carries. The answer is
	// retail's own `-1` fallback, `m_nSequence`.
	Npc->SequenceNumber = 17;
	TestEqual(TEXT("611 answers m_nSequence, retail's fallback for an unresolved sequence"),
		Npc->Slot611(), 17);

	// Slot 612 `0x102c04b0` — with no live dialog partner the answer is 0x80 whatever
	// `m_bDialogQueIsFinal` says, because the flag is only read INSIDE the partner arm.
	TestEqual(TEXT("612 answers 0x80 with no dialog partner"), Npc->Slot612(), 0x80);
	Npc->Dialogue.bDialogQueIsFinal = true;
	TestEqual(TEXT("and still 0x80 — the final flag is read inside the partner arm"),
		Npc->Slot612(), 0x80);
	return true;
}

// -------------------------------------------------------------------------------------------------
// `0x102b8980`, `0x102bf560`, `0x102bf770`, `0x102c0360`, `0x102aa9e0` (`0x102a0b90` is family Dialogue's).
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelTroikaHelpersFreeBodiesTest,
	"Elysium.Arm.NpcKernelTroikaHelpers.FreeBodies", GTroikaHelpersTestFlags)
bool FElysiumNpcKernelTroikaHelpersFreeBodiesTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("troika_free"), 0x29c17022);
	Builder.AddNpc(TEXT("npc"));
	Builder.AddNpc(TEXT("attacker"), FVector(300.0, 0.0, 0.0));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("npc"));
	FElysiumNpc* Attacker = Fixture.Npc(TEXT("attacker"));
	FElysiumNpcWorldFixture::Quiet({ Npc, Attacker });
	if (Npc == nullptr || Attacker == nullptr)
	{
		AddError(TEXT("fixture did not stand both NPCs"));
		return false;
	}

	// `0x102b8980` — the alert rung ladder: each rung's schedule id (0x4c, 0x4d, 0x51/0x52).
	Npc->FullInvestigate = 0;
	Npc->AlertLevel = 0;
	Npc->BaseScheduleHost.MemoryBits = 0;
	TestEqual(TEXT("level 0 advances to 1 and grades L"),
		Npc->AdvanceAlertLevelGrade(), 0x4c);
	TestEqual(TEXT("and wrote m_eAlertLevel 1"), Npc->AlertLevel, 1);
	TestEqual(TEXT("level 1 advances to 2 and grades M"),
		Npc->AdvanceAlertLevelGrade(), 0x4d);
	TestEqual(TEXT("and wrote m_eAlertLevel 2"), Npc->AlertLevel, 2);
	TestEqual(TEXT("level 2 pins at 3 and grades Q"),
		Npc->AdvanceAlertLevelGrade(), 0x51);
	TestEqual(TEXT("and wrote m_eAlertLevel 3"), Npc->AlertLevel, 3);
	Npc->BaseScheduleHost.MemoryBits = 0x8000000u;
	TestEqual(TEXT("the +0x5d8c bit 0x8000000 turns Q into R"),
		Npc->AdvanceAlertLevelGrade(), 0x52);
	// `m_bFullInvestigate` writes the level BEFORE the switch reads it, so it always lands on 'Q'.
	Npc->BaseScheduleHost.MemoryBits = 0;
	Npc->AlertLevel = 0;
	Npc->FullInvestigate = 1;
	TestEqual(TEXT("full_investigate forces rung 3 ahead of the switch"),
		Npc->AdvanceAlertLevelGrade(), 0x51);
	Npc->FullInvestigate = 0;

	// `0x102bf560` — the detected-attack notice. `m_bIgnoreDetectedAttack` refuses everything;
	// otherwise BOTH arms stamp the time, including the null one.
	Npc->bIgnoreDetectedAttack = true;
	Npc->Senses.Memory.DetectedAttackTime = -1.0;
	Npc->RecordDetectedAttack(Attacker);
	TestEqual(TEXT("m_bIgnoreDetectedAttack refuses the whole body"),
		Npc->Senses.Memory.DetectedAttackTime, -1.0, 0.0001);
	Npc->bIgnoreDetectedAttack = false;
	Npc->RecordDetectedAttack(Attacker);
	TestTrue(TEXT("the attacker is recorded"), Npc->Senses.Memory.DetectedAttackAttacker.IsSet());
	TestEqual(TEXT("and the window is stamped"), Npc->Senses.Memory.DetectedAttackTime,
		Npc->World->NowSeconds(), 0.001);
	Npc->Senses.Memory.DetectedAttackTime = -1.0;
	Npc->RecordDetectedAttack(nullptr);
	TestFalse(TEXT("a null attacker clears the handle"),
		Npc->Senses.Memory.DetectedAttackAttacker.IsSet());
	TestEqual(TEXT("and STILL stamps the window — retail writes it on both arms"),
		Npc->Senses.Memory.DetectedAttackTime, Npc->World->NowSeconds(), 0.001);

	// `0x102bf770` — the move stop. The three unconditional writes run whatever the activity does.
	Npc->BaseScheduleHost.bShouldMove = true;
	Npc->ScheduleHost.DesiredMoveYaw = 42.f;
	Npc->Navigator.bPaused = false;
	Npc->StopScheduledMove();
	TestFalse(TEXT("m_bShouldMove is cleared"), Npc->BaseScheduleHost.bShouldMove);
	TestEqual(TEXT("m_flDesiredMoveYaw is zeroed"), Npc->ScheduleHost.DesiredMoveYaw, 0.f, 0.0001f);
	TestTrue(TEXT("and the path is PAUSED (0x102ee2a0 -> 0x1030be80, path+0x10 = 1), not reset"),
		Npc->NavigatorIsPaused());

	// `0x102c0360` — with no live dialog partner the whole guarded block is skipped.
	const int32 ClearsBefore = Npc->DialogPartnerClears;
	Npc->bCutsceneForceLOD = true;
	Npc->OnDialogRelease();
	TestTrue(TEXT("no dialog partner leaves m_bCutsceneForceLOD alone"), Npc->bCutsceneForceLOD);
	TestEqual(TEXT("and clears no partner"), Npc->DialogPartnerClears, ClearsBefore);
	// With a live `m_hDialogPartner` (+0xfe8; any live entity resolves): `+0x1590 = 0`, the output,
	// then `SetDialogPartner(this, NULL)` — so a second call finds no partner and does nothing.
	Npc->SetDialogPartner(Npc->Handle);
	Npc->OnDialogRelease();
	TestFalse(TEXT("a live partner clears m_bCutsceneForceLOD"), Npc->bCutsceneForceLOD);
	TestFalse(TEXT("...and the partner (0x10107050(this, NULL))"), Npc->GetDialogPartner().IsSet());
	TestEqual(TEXT("...once"), Npc->DialogPartnerClears, ClearsBefore + 1);
	Npc->OnDialogRelease();
	TestEqual(TEXT("a second call finds no partner"), Npc->DialogPartnerClears, ClearsBefore + 1);

	// `0x102aa9e0` — the fail arm. A null cell (`0x102aa9e8`) and a cell with no path
	// (`0x102aa9f1`) both raise `TaskFail(0x1d)` through slot 448 (`0x102aaa1c` / `0x102aaa34`); the
	// live arms are `NextPatrolPoint_0x102aa9e0` below.
	Npc->BaseScheduleHost.FailureReason = 0;
	Npc->Cognition.Conditions.Clear(EElysiumNpcCond::TaskFailed);
	Npc->FUN_102aa9e0(nullptr);
	TestEqual(TEXT("0x102aa9e8 a null cell raises reason 0x1d through TaskFail (slot 448)"),
		Npc->BaseScheduleHost.FailureReason, 0x1d);
	TestTrue(TEXT("and TaskFail raised COND_TASK_FAILED with it"),
		Npc->Cognition.Conditions.Has(EElysiumNpcCond::TaskFailed));
	Npc->BaseScheduleHost.FailureReason = 0;
	Npc->Cognition.Conditions.Clear(EElysiumNpcCond::TaskFailed);
	FElysiumNpc::FPatrolPathCell EmptyCell;
	Npc->FUN_102aa9e0(&EmptyCell);
	TestEqual(TEXT("0x102aa9f1 a cell with no path raises reason 0x1d too"),
		Npc->BaseScheduleHost.FailureReason, 0x1d);
	return true;
}

// -------------------------------------------------------------------------------------------------
// `0x102aa9e0` (TASK_NEXT_PATROL_POINT's body) over `NextPoint` `0x10307b80`, the release
// `0x1029f5d0` and the interest draw `0x1029f650`. The step table `DAT_1049df2c` is +1/-1/+1/-1 and
// the next-type table `DAT_1049df30` is 0/1/3/2 for types 0..3 (read out of `vampire.dll`).
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelTroikaHelpersNextPatrolPointTest,
	"Elysium.Arm.NpcKernelTroikaHelpers.NextPatrolPoint_0x102aa9e0", GTroikaHelpersTestFlags)
bool FElysiumNpcKernelTroikaHelpersNextPatrolPointTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("troika_next_patrol"), 0x29c1702b);
	Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
	Builder.AddTroikaNpc(TEXT("npc"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("npc"));
	if (!TestNotNull(TEXT("the subject constructs"), Npc))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Npc });
	FElysiumNpc::ResetPatrolPathPool();
	using EBuild = FElysiumNpc::EPatrolPathBuild;
	const int32 Five[] = { 10, 11, 12, 13, 14, -1 };
	const int32 Three[] = { 20, 21, 22, -1 };

	// Starts a live task, builds a path and runs `0x102aa9e0` over the patrol cell.
	auto Next = [Npc](int32 Repeat, int32 Type, const int32* Ids, int32 Current)
	{
		Npc->Cognition.Conditions.Clear(EElysiumNpcCond::TaskFailed);
		Npc->BaseScheduleHost.FailureReason = 0;
		Npc->BuildPatrolPath(&Npc->PatrolPathCell, Repeat, Type, 0, Ids, EBuild::Replace);
		if (Npc->PatrolPathCell.Path != nullptr)
		{
			Npc->PatrolPathCell.Path->Current = Current;
		}
		Npc->Schedule.TaskStatus = EElysiumTaskStatus::Running;
		Npc->FUN_102aa9e0(&Npc->PatrolPathCell);
	};

	// Type 1, count 5, cur 4: step -1 (`0x10307b8e` `DAT_1049df2c[1]`), still in range
	// (`0x10307b9b` / `0x10307ba0`) -> false; no release; TaskComplete (`0x102aaa10`).
	Next(999, 1, Five, 4);
	if (!TestNotNull(TEXT("type 1 keeps its path"), Npc->PatrolPathCell.Path))
	{
		return false;
	}
	TestEqual(TEXT("0x10307b96 type 1 steps 4 -> 3"), Npc->PatrolPathCell.Path->Current, 3);
	TestEqual(TEXT("in range: repeat untouched"), Npc->PatrolPathCell.Path->Repeat, 999);
	TestEqual(TEXT("in range: type untouched"), Npc->PatrolPathCell.Path->Type, 1);
	TestEqual(TEXT("0x102aaa10 TaskComplete(false)"), Npc->Schedule.TaskStatus, EElysiumTaskStatus::Complete);
	TestFalse(TEXT("no TaskFail"), Npc->Cognition.Conditions.Has(EElysiumNpcCond::TaskFailed));

	// Type 1, cur 0, repeat 999: -1 is off the front (`0x10307b9b JS`); repeat > 0 (`0x10307ba7`)
	// -> repeat 998 (`0x10307ba9`), type next[1] = 1 (`0x10307bb5`), cur = 0x10307c20 = min(4, 0x7fff) = 4.
	Next(999, 1, Five, 0);
	TestNotNull(TEXT("a wrapped type-1 path is kept"), Npc->PatrolPathCell.Path);
	if (Npc->PatrolPathCell.Path != nullptr)
	{
		TestEqual(TEXT("0x10307bbc type 1 wraps to the last node"), Npc->PatrolPathCell.Path->Current, 4);
		TestEqual(TEXT("0x10307ba9 repeat 999 -> 998"), Npc->PatrolPathCell.Path->Repeat, 998);
		TestEqual(TEXT("0x10307bb5 next[1] = 1: it loops, it does not reverse"), Npc->PatrolPathCell.Path->Type, 1);
	}

	// Type 2, count 3, cur 2, repeat 999: 3 is off the end (`0x10307ba0`) -> repeat 998, type
	// next[2] = 3, cur = min(2, 0x7fff) = 2.
	Next(999, 2, Three, 2);
	TestNotNull(TEXT("a reversed type-2 path is kept"), Npc->PatrolPathCell.Path);
	if (Npc->PatrolPathCell.Path != nullptr)
	{
		TestEqual(TEXT("0x10307bb5 next[2] = 3: it reverses"), Npc->PatrolPathCell.Path->Type, 3);
		TestEqual(TEXT("0x10307bbc cur = start[3] clamped to the last node"), Npc->PatrolPathCell.Path->Current, 2);
		TestEqual(TEXT("0x10307ba9 repeat 998"), Npc->PatrolPathCell.Path->Repeat, 998);
	}

	// Type 3, cur 0: -1 -> type next[3] = 2, cur = start[2] = 0.
	Next(999, 3, Three, 0);
	TestNotNull(TEXT("a reversed type-3 path is kept"), Npc->PatrolPathCell.Path);
	if (Npc->PatrolPathCell.Path != nullptr)
	{
		TestEqual(TEXT("0x10307bb5 next[3] = 2"), Npc->PatrolPathCell.Path->Type, 2);
		TestEqual(TEXT("0x10307bbc cur = start[2] = 0"), Npc->PatrolPathCell.Path->Current, 0);
		TestEqual(TEXT("0x10307ba9 repeat 998"), Npc->PatrolPathCell.Path->Repeat, 998);
	}

	// Repeat 0, off the end: `0x10307ba7 JLE` -> true (`0x10307bc3`); `0x102aa9ff` releases the path
	// (`0x1029f5d0`: owned byte cleared, pointer nulled); `0x102aaa07` draws on the now-empty cell,
	// which clears `m_bPatrolPathUseHint` (`0x1029f659`) and rolls nothing; TaskComplete.
	Npc->ScheduleHost.bPatrolPathUseHint = true;
	Next(0, 0, Three, 2);
	TestNull(TEXT("0x1029f5e8 an exhausted path is released"), Npc->PatrolPathCell.Path);
	TestFalse(TEXT("0x1029f5dd and its owned byte is cleared"), Npc->PatrolPathCell.bOwned);
	TestFalse(TEXT("0x1029f659 the draw clears m_bPatrolPathUseHint"), Npc->ScheduleHost.bPatrolPathUseHint);
	TestEqual(TEXT("0x102aaa10 the exhausted pass still completes"), Npc->Schedule.TaskStatus,
		EElysiumTaskStatus::Complete);
	TestFalse(TEXT("and does not fail"), Npc->Cognition.Conditions.Has(EElysiumNpcCond::TaskFailed));

	// Null path: `0x102aa9f1` -> `TaskFail(0x1d)` (`0x102aaa34`), no completion.
	Npc->Cognition.Conditions.Clear(EElysiumNpcCond::TaskFailed);
	Npc->BaseScheduleHost.FailureReason = 0;
	Npc->Schedule.TaskStatus = EElysiumTaskStatus::Running;
	Npc->FUN_102aa9e0(&Npc->PatrolPathCell);
	TestEqual(TEXT("0x102aaa34 a null path fails 0x1d"), Npc->BaseScheduleHost.FailureReason, 0x1d);
	TestNotEqual(TEXT("and does not complete"), Npc->Schedule.TaskStatus, EElysiumTaskStatus::Complete);

	FElysiumNpc::ResetPatrolPathPool();
	return true;
}

// -------------------------------------------------------------------------------------------------
// `0x102c36d0`, the ignore-collision triple and `0x102c4ad0`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelTroikaHelpersGeometryTest,
	"Elysium.Arm.NpcKernelTroikaHelpers.LeadJumpAndCollision", GTroikaHelpersTestFlags)
bool FElysiumNpcKernelTroikaHelpersGeometryTest::RunTest(const FString&)
{
	// The two pure halves of `0x102c36d0` first — they are the only parts the pathfinder seam does
	// not stand in front of.
	TestEqual(TEXT("the ratio clamp's lower bound"),
		FElysiumNpc::ClampTargetLeadRatio(0.25f, 0.5f, 2.0f), 0.5f, 0.0001f);
	TestEqual(TEXT("its upper bound"),
		FElysiumNpc::ClampTargetLeadRatio(5.0f, 0.5f, 2.0f), 2.0f, 0.0001f);
	TestEqual(TEXT("and the pass-through between them"),
		FElysiumNpc::ClampTargetLeadRatio(1.25f, 0.5f, 2.0f), 1.25f, 0.0001f);

	const FVector Blend = FElysiumNpc::BlendTargetLeadPoint(FVector(10.0, 0.0, 0.0),
		FVector(0.0, 20.0, 0.0), 0.5f, 0.25f, 2.0f);
	TestEqual(TEXT("the blend scales the SUM, not each term"), static_cast<float>(Blend.X), 10.f,
		0.0001f);
	TestEqual(TEXT("on every axis"), static_cast<float>(Blend.Y), 10.f, 0.0001f);

	FElysiumNpcWorldBuilder Builder(TEXT("troika_geometry"), 0x29c17023);
	Builder.AddNpc(TEXT("npc"));
	Builder.AddNpc(TEXT("goal"), FVector(100.0 * TroikaTestU, 0.0, 50.0 * TroikaTestU));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("npc"));
	FElysiumNpc* Goal = Fixture.Npc(TEXT("goal"));
	FElysiumNpcWorldFixture::Quiet({ Npc, Goal });
	if (Npc == nullptr || Goal == nullptr)
	{
		AddError(TEXT("fixture did not stand both NPCs"));
		return false;
	}

	// `0x102c36d0`'s two reachable arms. A null lead with a fallback copies it through; a null lead
	// with no fallback leaves the point alone; a live lead with the pathfinder seam refusing lands
	// on the lead's plain origin.
	FVector Point(1.0, 2.0, 3.0);
	const FVector Fallback(7.0, 8.0, 9.0);
	Npc->ComputeTargetLeadPoint(FVector::ZeroVector, nullptr, 1.0f, &Fallback, Point);
	TestEqual(TEXT("a null lead copies the fallback through"), Point.X, 7.0, 0.0001);
	Point = FVector(1.0, 2.0, 3.0);
	Npc->ComputeTargetLeadPoint(FVector::ZeroVector, nullptr, 1.0f, nullptr, Point);
	TestEqual(TEXT("a null lead with no fallback leaves the point alone"), Point.X, 1.0, 0.0001);
	Npc->ComputeTargetLeadPoint(FVector::ZeroVector, Goal, 1.0f, nullptr, Point);
	TestEqual(TEXT("the refused pathfinder query lands on the lead's own origin, SOURCE units"),
		Point.X, 100.0, 0.001);
	TestEqual(TEXT("and its height with it"), Point.Z, 50.0, 0.001);

	// `0x102c4380` / `0x102c43b0` / `0x102c43f0`.
	Npc->IgnoreCollisionEntity = FElysiumEntityHandle();
	Npc->IgnoreCollisionUntil = 0.0;
	Npc->FUN_102c43b0(5.0f);
	TestEqual(TEXT("the renewal refuses while nothing is being ignored"), Npc->IgnoreCollisionUntil,
		0.0, 0.0001);

	Npc->FUN_102c4380(Goal);
	TestTrue(TEXT("StartIgnoringCollision records the partner"), Npc->IgnoreCollisionEntity.IsSet());
	TestTrue(TEXT("and pins the timer to the FLT_MAX never sentinel"),
		Npc->IgnoreCollisionUntil > 1.0e37);

	Npc->FUN_102c43b0(100.0f);
	TestEqual(TEXT("the renewal now arms curtime + 100"), Npc->IgnoreCollisionUntil,
		Npc->World->NowSeconds() + 100.0, 0.001);
	TestTrue(TEXT("and the immediate expiry check did not fire"),
		Npc->IgnoreCollisionEntity.IsSet());

	Npc->FUN_102c43b0(0.0f);
	TestFalse(TEXT("a zero duration expires on the same call, because 43b0 runs 43f0 after it"),
		Npc->IgnoreCollisionEntity.IsSet());
	TestTrue(TEXT("and the timer is pinned back to FLT_MAX so it never refires"),
		Npc->IgnoreCollisionUntil > 1.0e37);

	// `0x102c4ad0`. The goal is 100 units out on X and 50 up.
	Npc->SetJumpOriginAndTarget(Goal, 3.5f, 200.0f);
	TestEqual(TEXT("the jump height is the caller's"), Npc->JumpHeight, 3.5f, 0.0001f);
	TestEqual(TEXT("inside the backoff distance the target IS the goal's origin"), Npc->JumpTarget.X,
		100.0, 0.001);
	TestEqual(TEXT("and the origin is mine"), Npc->JumpOrigin.X, 0.0, 0.001);

	Npc->SetJumpOriginAndTarget(Goal, 1.0f, 0.25f);
	TestEqual(TEXT("beyond it the planar target is pulled back by the same number as a FRACTION"),
		Npc->JumpTarget.X, 75.0, 0.001);
	TestEqual(TEXT("and the height is the goal's, untouched — retail's `- backoff * 0.0`"),
		Npc->JumpTarget.Z, 50.0, 0.001);
	return true;
}

// -------------------------------------------------------------------------------------------------
// `CAI_Motor` and `CAI_Navigator`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelTroikaHelpersMotorTest,
	"Elysium.Arm.NpcKernelTroikaHelpers.MotorAndNavigator", GTroikaHelpersTestFlags)
bool FElysiumNpcKernelTroikaHelpersMotorTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("troika_motor"), 0x29c17024);
	Builder.AddNpc(TEXT("npc"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("npc"));
	FElysiumNpcWorldFixture::Quiet({ Npc });
	if (Npc == nullptr)
	{
		AddError(TEXT("fixture did not stand the NPC"));
		return false;
	}

	// `CAI_Motor#8` `0x102e1270` — the full stop: zero velocity, activity 0x30, no reissue.
	const int32 ReissuesBefore = Npc->TroikaMotor.MoveReissues;
	Npc->FUN_102e1270();
	TestEqual(TEXT("#8 forces activity 0x30"), Npc->TroikaMotor.LastForcedActivity, 0x30);
	TestEqual(TEXT("#8 zeroes the velocity"),
		static_cast<float>(Npc->TroikaMotor.LastVelocityUnits.Size()), 0.f, 0.0001f);
	TestEqual(TEXT("and reissues no move"), Npc->TroikaMotor.MoveReissues, ReissuesBefore);

	// `CAI_Navigator#7` `0x102eea70`.
	Npc->Navigator.bNavFailed = false;
	Npc->NavigatorWord0x54 = 0;
	const int32 Clears = Npc->NavigatorRouteClears;
	Npc->FUN_102eea70();
	TestTrue(TEXT("#7 sets the navigator's dirty latch"), Npc->Navigator.bNavFailed);
	TestEqual(TEXT("#7 resets +0x54 to -1"), Npc->NavigatorWord0x54, -1);
	TestEqual(TEXT("#7 clears the route once"), Npc->NavigatorRouteClears, Clears + 1);
	return true;
}

// -------------------------------------------------------------------------------------------------
// The two hint bodies.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelTroikaHelpersStandoffTest,
	"Elysium.Arm.NpcKernelTroikaHelpers.StandoffAndHints", GTroikaHelpersTestFlags)
bool FElysiumNpcKernelTroikaHelpersStandoffTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("troika_standoff"), 0x29c17025);
	Builder.AddNpc(TEXT("npc"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("npc"));
	FElysiumNpcWorldFixture::Quiet({ Npc });
	if (Npc == nullptr)
	{
		AddError(TEXT("fixture did not stand the NPC"));
		return false;
	}

	// `0x102b6120` — no hint node ZEROES the caller's point and answers false. That is the arm, not
	// a refusal to write.
	Npc->BaseScheduleHost.HintNode = INDEX_NONE;
	FVector Point(5.0, 6.0, 7.0);
	TestFalse(TEXT("the lean offset answers false with no hint node"),
		Npc->ApplyHintLeanOffset(Point, true));
	TestEqual(TEXT("and zeroes the point, which is what retail writes on that arm"),
		static_cast<float>(Point.Size()),
		0.f, 0.0001f);

	// `0x102b7110` — the search brackets the first attempt with `m_bForceCoverLOSCheck` and leaves
	// it CLEAR afterwards; with no hint store it finds nothing and answers false.
	Npc->ScheduleHost.bForceCoverLosCheck = false;
	Npc->bStayEntrenched = true;
	TestFalse(TEXT("the tactical hint search finds nothing (no hint store)"),
		Npc->FindTacticalHintNode(0));
	TestFalse(TEXT("and leaves m_bForceCoverLOSCheck clear — the bracket is around the FIRST "
			  "search only, and the entrenched retry runs outside it"),
		Npc->ScheduleHost.bForceCoverLosCheck);
	TestEqual(TEXT("and no hint is installed"), Npc->BaseScheduleHost.HintNode,
		static_cast<int32>(INDEX_NONE));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
