#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumMoveSolve.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumSchedule.h"
#include "Tests/ElysiumNpcTestFixture.h"

// Story 29c-1, family **TroikaHelpers**. Every assertion below is read off the decompiled C of the
// body it names — the threshold, the arm order, the id, what is written — never off 29c's one-line
// walk, which this family found wrong in four places (slots 599/600's draw bounds, slot 334's flag
// polarity, slot 610's case 2, and `CAI_Motor#17`'s two "clamps", which are both floors).
//
// **The two tables disagree, and this suite honours both.** `npc_VCop` is a REGISTERED spawn leaf
// whose census classname list is null, so `RetailClass()` answers null and every per-species lookup
// falls through to the Troika line — asserted rather than worked around. A species row whose
// classname no fixture can spawn is exercised through `ElysiumNpcKernelClass::Find` and the table's
// own lookup.

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
	"Elysium.Substrate.NpcKernelTroikaHelpers.MeleeSlotLine", GTroikaHelpersTestFlags)
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
	const FElysiumNpcClass* Vampire = ElysiumNpcKernelClass::Find(TEXT("CNPC_VVampire"));
	const FElysiumNpcClass* Rat = ElysiumNpcKernelClass::Find(TEXT("CNPC_VRat"));
	if (Vampire == nullptr || Rat == nullptr)
	{
		AddError(TEXT("the census carries neither CNPC_VVampire nor CNPC_VRat"));
		return false;
	}
	for (const int32 Slot : { 599, 600, 601, 602 })
	{
		TestEqual(TEXT("CNPC_VVampire is on the AndreiBlood line"),
			FString(ElysiumNpcKernelClass::BodyOf(Vampire, Slot)),
			FString(FElysiumNpc::MeleeSlotBody(Slot, FElysiumNpc::EMeleeSlotLine::AndreiBlood)));
		TestEqual(TEXT("CNPC_VRat is on the Troika line"),
			FString(ElysiumNpcKernelClass::BodyOf(Rat, Slot)),
			FString(FElysiumNpc::MeleeSlotBody(Slot, FElysiumNpc::EMeleeSlotLine::Troika)));
	}

	FElysiumNpcWorldBuilder Builder(TEXT("troika_meleeline"), 0x29c1701a);
	Builder.AddNpc(TEXT("vamp"), FVector::ZeroVector, TEXT("npc_VVampire"));
	Builder.AddNpc(TEXT("rat"), FVector(200.0, 0.0, 0.0), TEXT("npc_VRat"));
	Builder.AddNpc(TEXT("cop"), FVector(400.0, 0.0, 0.0), TEXT("npc_VCop"));
	Builder.AddNpc(TEXT("phone"), FVector(600.0, 0.0, 0.0), TEXT("npc_payphone"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Vamp = Fixture.Npc(TEXT("vamp"));
	FElysiumNpc* RatNpc = Fixture.Npc(TEXT("rat"));
	FElysiumNpc* Cop = Fixture.Npc(TEXT("cop"));
	FElysiumNpcWorldFixture::Quiet({ Vamp, RatNpc, Cop });
	if (Vamp == nullptr || RatNpc == nullptr || Cop == nullptr)
	{
		AddError(TEXT("fixture did not stand the three spawn leaves"));
		return false;
	}

	TestTrue(TEXT("npc_VVampire resolves to a census class"), Vamp->RetailClass() != nullptr);
	TestEqual(TEXT("and takes the AndreiBlood line at 602"),
		static_cast<int32>(Vamp->MeleeSlotLine(602)),
		static_cast<int32>(FElysiumNpc::EMeleeSlotLine::AndreiBlood));
	TestEqual(TEXT("npc_VRat takes the Troika line at 602"),
		static_cast<int32>(RatNpc->MeleeSlotLine(602)),
		static_cast<int32>(FElysiumNpc::EMeleeSlotLine::Troika));

	// The recovered two-table disagreement: no census class claims `npc_VCop`.
	TestNull(TEXT("npc_VCop's RetailClass is null — the census claims no classname for it"),
		Cop->RetailClass());
	TestEqual(TEXT("so a cop falls through to the Troika line, which is the recovered answer"),
		static_cast<int32>(Cop->MeleeSlotLine(602)),
		static_cast<int32>(FElysiumNpc::EMeleeSlotLine::Troika));
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 599 `0x102b5650` and slot 600 `0x102b57c0`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelTroikaHelpersEnterMeleeTest,
	"Elysium.Substrate.NpcKernelTroikaHelpers.EnterMelee", GTroikaHelpersTestFlags)
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
	Thug->NpcFlags.SetFrenziedWord(0x2);
	TestTrue(TEXT("frenzy bit 0x2 enters melee outright"), Thug->Slot599(0));
	TestTrue(TEXT("and m_bInMelee is set"), Thug->bInMelee);
	Thug->NpcFlags.SetFrenziedWord(0);

	// Arm 2: the can-enter timer. `curtime < m_flMeleeCanEnterTimer` refuses AND clears the latch.
	Thug->bInMelee = true;
	Thug->MeleeCanEnterTimer = Thug->World->NowSeconds() + 100.0;
	TestFalse(TEXT("a live can-enter timer refuses"), Thug->Slot599(0));
	TestFalse(TEXT("and clears m_bInMelee"), Thug->bInMelee);

	// Arm 3: past the timer, with the coordinator seam refusing and the frenzy bypass clear, the
	// whole gate closes on the coordinator. The height term passes (the limit is the unrecovered
	// 0.0 and `EnemyHeightDiffUnits` defaults to 5000, so `ENEMY_UNREACHABLE` decides) and the
	// range term passes because slot 308 is a stub answering false.
	Thug->MeleeCanEnterTimer = 0.0;
	Thug->ScheduleHost.EnemyHeightDiffUnits = 5000.f;
	Thug->Cognition.Conditions.Set(EElysiumNpcCond::EnemyUnreachable);
	TestFalse(TEXT("an unreachable enemy above the height limit refuses"), Thug->Slot599(0));
	Thug->Cognition.Conditions.Clear(EElysiumNpcCond::EnemyUnreachable);
	TestFalse(TEXT("and with the enemy reachable the coordinator seam is what refuses"),
		Thug->Slot599(0));

	// Arm 4: frenzy bit 0x1000 bypasses the coordinator entirely, which is the one way this
	// substrate can reach the entry write — and it arms the must-leave timer from
	// `RandomFloat(7.5, 15.0)`, NOT `RandomFloat(4, 15)` as 29c's walk reads.
	const int32 EventsBefore = Thug->MeleeEventFires;
	Thug->NpcFlags.SetFrenziedWord(0x1000);
	const double Now = Thug->World->NowSeconds();
	TestTrue(TEXT("the 0x1000 coordinator bypass enters melee"), Thug->Slot599(0));
	TestTrue(TEXT("m_bInMelee is set"), Thug->bInMelee);
	TestTrue(TEXT("the must-leave timer is at least curtime + 7.5"),
		Thug->MeleeMustLeaveTimer >= Now + 7.5 - 0.001);
	TestTrue(TEXT("and at most curtime + 15.0"), Thug->MeleeMustLeaveTimer <= Now + 15.0 + 0.001);
	TestEqual(TEXT("the global melee event fired exactly once"), Thug->MeleeEventFires,
		EventsBefore + 1);
	Thug->NpcFlags.SetFrenziedWord(0);

	// Slot 600: the weapon capability word is family Motor's seam answering 0, so `0x18000` is
	// never present and the body writes NOTHING — not even the `m_bInMelee = 0` inside the gate.
	Thug->bInMelee = true;
	const int32 EventsBeforeSlot600 = Thug->MeleeEventFires;
	TestFalse(TEXT("slot 600 refuses with no capability bits"), Thug->Slot600(nullptr));
	TestTrue(TEXT("and leaves m_bInMelee alone, because the write is INSIDE the gate"),
		Thug->bInMelee);
	TestEqual(TEXT("and fires no event"), Thug->MeleeEventFires, EventsBeforeSlot600);
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 601 `0x102b5880` / `0x10385cf0` and slot 602 `0x102b5900` / `0x10385d70`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelTroikaHelpersLeaveMeleeTest,
	"Elysium.Substrate.NpcKernelTroikaHelpers.LeaveMelee", GTroikaHelpersTestFlags)
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
		static_cast<int32>(TroikaNpc->MeleeSlotLine(602)),
		static_cast<int32>(FElysiumNpc::EMeleeSlotLine::Troika));
	TestEqual(TEXT("npc_VVampire is on the AndreiBlood line at 602"),
		static_cast<int32>(BloodNpc->MeleeSlotLine(602)),
		static_cast<int32>(FElysiumNpc::EMeleeSlotLine::AndreiBlood));

	// **NAMED DIVERGENCE**, and the reachability argument behind it: every coordinator entry point
	// dereferences its `this` at once, so a null `m_pAttackCoordinator` faults in retail — and the
	// only paths that set `m_bInMelee` without touching the coordinator are exactly the two gates
	// this body refuses on. So retail cannot reach the tail with a null coordinator, and BOTH lines
	// refuse here rather than one of them answering on a state no shipped program ever saw.
	TroikaNpc->AttackCoordinator = 0;
	BloodNpc->AttackCoordinator = 0;
	TestFalse(TEXT("602 on the Troika line refuses on its own null-coordinator test"),
		TroikaNpc->Slot602());
	TestFalse(TEXT("602 on the AndreiBlood line refuses too — the named divergence, because retail "
			  "would have faulted rather than answered"),
		BloodNpc->Slot602());

	// Both lines share the two gates in front of it, and both are tested BEFORE the coordinator.
	BloodNpc->AttackCoordinator = 2;
	BloodNpc->NpcFlags.SetFrenziedWord(0x2);
	TestFalse(TEXT("frenzy bit 0x2 refuses on both lines"), BloodNpc->Slot602());
	BloodNpc->NpcFlags.SetFrenziedWord(0);

	// With a coordinator index in hand the tail runs and the two lines agree, which is retail's own
	// state. The must-leave arm is unreachable while slot 308 is a stub answering false, so the far
	// arm is what runs: `MeleeRange` is unrecovered at 0.0 and `EnemyDistUnits` sits at its
	// no-enemy 5000, `2 * 0 <= 5000` holds, and the "has room" seam answers false.
	BloodNpc->ScheduleHost.EnemyDistUnits = 5000.f;
	TestTrue(TEXT("the far arm answers true: no room in the coordinator"), BloodNpc->Slot602());
	TroikaNpc->AttackCoordinator = 2;
	TroikaNpc->ScheduleHost.EnemyDistUnits = 5000.f;
	TestTrue(TEXT("and the Troika line answers the same once its guard passes"),
		TroikaNpc->Slot602());
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slots 54, 56, 322, 597.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelTroikaHelpersNotifySlotsTest,
	"Elysium.Substrate.NpcKernelTroikaHelpers.NotifySlots", GTroikaHelpersTestFlags)
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
	Guard->Senses.Memory.Enemy = Foe->Handle;
	TestTrue(TEXT("54 answers true for my enemy while no program is installed"), Guard->Slot54(Foe));
	Guard->Schedule.Current = EElysiumScheduleId::IdleStand;
	TestEqual(TEXT("54's answer is the mask test, not a condition test"), Guard->Slot54(Foe),
		ElysiumSchedule::MaskHasCondition(Guard->Schedule, *Guard, EElysiumNpcCond::LostEnemy));
	Guard->Schedule.Current = EElysiumScheduleId::None;

	// Slot 56 `0x102b5120`. The `+0x200` seam refuses, so `m_hLastEnemy` survives even when the
	// argument IS the last enemy — which is the recovered refusal and not an omission.
	Guard->Senses.Memory.LastEnemy = Foe->Handle;
	Guard->Slot56(Foe, FVector::ZeroVector, FVector::ZeroVector, TEXT("x"));
	TestTrue(TEXT("56 leaves m_hLastEnemy set while the +0x200 seam answers false"),
		Guard->Senses.Memory.LastEnemy.IsSet());
	Guard->Slot56(nullptr, FVector::ZeroVector, FVector::ZeroVector, nullptr);
	TestTrue(TEXT("and a null argument returns before anything"),
		Guard->Senses.Memory.LastEnemy.IsSet());

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
	"Elysium.Substrate.NpcKernelTroikaHelpers.DisciplineAndFire", GTroikaHelpersTestFlags)
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
	TestEqual(TEXT("the table seam answers INDEX_NONE"), Caster->DisciplineTableFind(3, 1),
		static_cast<int32>(INDEX_NONE));

	// Slot 616 — clear `COND_ON_FIRE` and stamp the immunity window.
	Caster->Cognition.Conditions.Set(EElysiumNpcCond::OnFire);
	Caster->NextBurnTime = -1.0;
	Caster->Slot616();
	TestFalse(TEXT("616 clears COND_ON_FIRE"),
		Caster->Cognition.Conditions.Has(EElysiumNpcCond::OnFire));
	TestEqual(TEXT("616 stamps m_flNextBurnTime with curtime + the (unrecovered) window"),
		Caster->NextBurnTime, Caster->World->NowSeconds(), 0.001);
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 606 `0x102b8320` — the occlusion ladder.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelTroikaHelpersOccludeTest,
	"Elysium.Substrate.NpcKernelTroikaHelpers.OcclusionLadder", GTroikaHelpersTestFlags)
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
	Shooter->NpcFlags.SetFrenziedWord(0x100);
	TestEqual(TEXT("frenzied bit 0x100 answers 0xb4"), Shooter->Slot606(0), 0xb4);
	Shooter->NpcFlags.SetFrenziedWord(0);

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
	"Elysium.Substrate.NpcKernelTroikaHelpers.FollowerDistance", GTroikaHelpersTestFlags)
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

	// The facing request is behind family Facing's cvar seam, which answers false.
	TestFalse(TEXT("the queued facing request was not made — the cvar seam refuses"),
		Follower->FacingTargetsEnabled());
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slots 608, 609, 610, 611, 612.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelTroikaHelpersTailSlotsTest,
	"Elysium.Substrate.NpcKernelTroikaHelpers.TailSlots", GTroikaHelpersTestFlags)
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

	// Slot 608 `0x102c48b0`. The two guards first, then the walk over the three globals — whose
	// names are a seam, so nothing can match.
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
	TestEqual(TEXT("610's default arm changes nothing while the disposition table is a seam"),
		Npc->ExpressionBlendWeight, 0.5f, 0.0001f);
	TestEqual(TEXT("the expression-index seam answers INDEX_NONE — this runtime names expressions"),
		Npc->LookupExpressionIndex(TEXT("Anger")), static_cast<int32>(INDEX_NONE));

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
// `0x102b8980`, `0x102bf560`, `0x102bf770`, `0x102c0360`, `0x102a0b90`, `0x102aa9e0`, `0x10312cd0`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelTroikaHelpersFreeBodiesTest,
	"Elysium.Substrate.NpcKernelTroikaHelpers.FreeBodies", GTroikaHelpersTestFlags)
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

	// `0x102b8980` — the alert rung ladder, letter by letter.
	Npc->FullInvestigate = 0;
	Npc->AlertLevel = 0;
	Npc->ScheduleHost.MemoryBits = 0;
	TestEqual(TEXT("level 0 advances to 1 and grades L"),
		static_cast<int32>(Npc->AdvanceAlertLevelGrade()), static_cast<int32>(TEXT('L')));
	TestEqual(TEXT("and wrote m_eAlertLevel 1"), Npc->AlertLevel, 1);
	TestEqual(TEXT("level 1 advances to 2 and grades M"),
		static_cast<int32>(Npc->AdvanceAlertLevelGrade()), static_cast<int32>(TEXT('M')));
	TestEqual(TEXT("and wrote m_eAlertLevel 2"), Npc->AlertLevel, 2);
	TestEqual(TEXT("level 2 pins at 3 and grades Q"),
		static_cast<int32>(Npc->AdvanceAlertLevelGrade()), static_cast<int32>(TEXT('Q')));
	TestEqual(TEXT("and wrote m_eAlertLevel 3"), Npc->AlertLevel, 3);
	Npc->ScheduleHost.MemoryBits = 0x8000000u;
	TestEqual(TEXT("the +0x5d8c bit 0x8000000 turns Q into R"),
		static_cast<int32>(Npc->AdvanceAlertLevelGrade()),
		static_cast<int32>(TEXT('R')));
	// `m_bFullInvestigate` writes the level BEFORE the switch reads it, so it always lands on 'Q'.
	Npc->ScheduleHost.MemoryBits = 0;
	Npc->AlertLevel = 0;
	Npc->FullInvestigate = 1;
	TestEqual(TEXT("full_investigate forces rung 3 ahead of the switch"),
		static_cast<int32>(Npc->AdvanceAlertLevelGrade()), static_cast<int32>(TEXT('Q')));
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
	Npc->ScheduleHost.bShouldMove = true;
	Npc->ScheduleHost.DesiredMoveYaw = 42.f;
	const int32 NavResetsBefore = Npc->NavResets;
	Npc->StopScheduledMove();
	TestFalse(TEXT("m_bShouldMove is cleared"), Npc->ScheduleHost.bShouldMove);
	TestEqual(TEXT("m_flDesiredMoveYaw is zeroed"), Npc->ScheduleHost.DesiredMoveYaw, 0.f, 0.0001f);
	TestEqual(TEXT("and the navigator reset was asked exactly once"), Npc->NavResets,
		NavResetsBefore + 1);

	// `0x102c0360` — with no live dialog partner the whole guarded block is skipped.
	const int32 ClearsBefore = Npc->DialogPartnerClears;
	Npc->bCutsceneForceLOD = true;
	Npc->OnDialogRelease();
	TestTrue(TEXT("no dialog partner leaves m_bCutsceneForceLOD alone"), Npc->bCutsceneForceLOD);
	TestEqual(TEXT("and clears no partner"), Npc->DialogPartnerClears, ClearsBefore);

	// `0x102a0b90` — the crosswalk stamp, named by the `AT_CROSSWALK` bit beside it.
	TestFalse(TEXT("AT_CROSSWALK starts clear"), Npc->NpcFlags.Has(EElysiumNpcFlag::AT_CROSSWALK));
	Npc->SetAtCrosswalk(9);
	TestTrue(TEXT("SetAtCrosswalk raises AT_CROSSWALK"),
		Npc->NpcFlags.Has(EElysiumNpcFlag::AT_CROSSWALK));
	TestEqual(TEXT("and stores the node at +0x630c"), Npc->AtCrosswalkNode, 9);

	// `0x102aa9e0` — the branch itself. A null argument raises `TaskFail(0x1d)`; a live one
	// forwards and completes the task.
	Npc->ScheduleHost.FailureReason = 0;
	Npc->Cognition.Conditions.Clear(EElysiumNpcCond::TaskFailed);
	Npc->FUN_102aa9e0(nullptr);
	TestEqual(TEXT("a null argument raises assert reason 0x1d through TaskFail (slot 448)"),
		Npc->ScheduleHost.FailureReason, 0x1d);
	TestTrue(TEXT("and TaskFail raised COND_TASK_FAILED with it"),
		Npc->Cognition.Conditions.Has(EElysiumNpcCond::TaskFailed));
	const int32 ForwardsBefore = Npc->TaskArgumentForwards;
	Npc->Schedule.bTaskCompletedExternally = false;
	int32 Dummy = 0;
	Npc->FUN_102aa9e0(&Dummy);
	TestEqual(TEXT("a live argument forwards once"), Npc->TaskArgumentForwards, ForwardsBefore + 1);
	TestTrue(TEXT("and completes the task"), Npc->Schedule.bTaskCompletedExternally);

	// `0x10312cd0` — the expresser factory. The base gate is `IsAlive`, and only a live body
	// reaches the factory at all.
	const int32 FactoryBefore = Npc->ExpresserFactoryCalls;
	TestFalse(TEXT("CreateExpresser answers false — there is no expresser substrate"),
		Npc->CreateExpresser());
	TestEqual(TEXT("but the factory WAS asked, because the base gate opened"),
		Npc->ExpresserFactoryCalls, FactoryBefore + 1);
	return true;
}

// -------------------------------------------------------------------------------------------------
// `0x102c36d0`, the ignore-collision triple and `0x102c4ad0`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelTroikaHelpersGeometryTest,
	"Elysium.Substrate.NpcKernelTroikaHelpers.LeadJumpAndCollision", GTroikaHelpersTestFlags)
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
	"Elysium.Substrate.NpcKernelTroikaHelpers.MotorAndNavigator", GTroikaHelpersTestFlags)
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

	// `CAI_Motor#3` `0x102e0ea0` — the activity dispatch is FIRST, before the state reset.
	Npc->FUN_102e0ea0();
	TestEqual(TEXT("#3 forces activity 0x33"), Npc->TroikaMotor.LastForcedActivity, 0x33);
	TestEqual(TEXT("#3 resets the motor state once"), Npc->TroikaMotor.StateResets, 1);
	TestEqual(TEXT("#3 sets solid once"), Npc->TroikaMotor.SolidSets, 1);
	TestEqual(TEXT("#3 dispatches the owner's +0x340 once"), Npc->TroikaMotor.OwnerSlot208Dispatches,
		1);

	// `CAI_Motor#8` `0x102e1270` — the full stop: zero velocity, activity 0x30, no reissue.
	const int32 ReissuesBefore = Npc->TroikaMotor.MoveReissues;
	Npc->FUN_102e1270();
	TestEqual(TEXT("#8 forces activity 0x30"), Npc->TroikaMotor.LastForcedActivity, 0x30);
	TestEqual(TEXT("#8 zeroes the velocity"),
		static_cast<float>(Npc->TroikaMotor.LastVelocityUnits.Size()), 0.f, 0.0001f);
	TestEqual(TEXT("and reissues no move"), Npc->TroikaMotor.MoveReissues, ReissuesBefore);

	// `CAI_Motor#6` `0x102e1180` — the goal vector goes straight into SetAbsVelocity, activity
	// 0x2c, and the move is reissued at the goal's own yaw and speed -1.
	Npc->FUN_102e1180(FVector(0.0, 10.0, 0.0));
	TestEqual(TEXT("#6 hands the goal itself to SetAbsVelocity"),
		static_cast<float>(Npc->TroikaMotor.LastVelocityUnits.Y), 10.f, 0.0001f);
	TestEqual(TEXT("#6 forces activity 0x2c"), Npc->TroikaMotor.LastForcedActivity, 0x2c);
	TestEqual(TEXT("#6 reissues at the goal's yaw"), Npc->TroikaMotor.LastReissueYaw, 90.0f, 0.01f);
	TestEqual(TEXT("and at speed -1"), Npc->TroikaMotor.LastReissueSpeed, -1.0f, 0.0001f);

	// `CAI_Motor#4` `0x102e0f90` — with the deceleration scale unrecovered at 0.0 the interval
	// bound is 0, so the FAR arm is the one that runs: the interval is zeroed and the move
	// reissued. The velocity is written on BOTH arms, before the branch.
	Npc->TroikaMotor.MoveInterval = 5.f;
	const int32 Reissues = Npc->TroikaMotor.MoveReissues;
	TestFalse(TEXT("#4 takes the restart arm"), Npc->FUN_102e0f90(FVector(100.0, 0.0, 0.0), 45.0f));
	TestEqual(TEXT("#4 zeroes the move interval on that arm"), Npc->TroikaMotor.MoveInterval, 0.f,
		0.0001f);
	TestEqual(TEXT("#4 reissues once"), Npc->TroikaMotor.MoveReissues, Reissues + 1);
	TestEqual(TEXT("at the caller's yaw"), Npc->TroikaMotor.LastReissueYaw, 45.0f, 0.0001f);

	// `CAI_Motor#17` `0x102e2580` — BOTH bounds are floors, so a hull floor above the base speed
	// wins. 29c's walk reads the first as an upper bound; it is not.
	Npc->TroikaMotor.SpeedCeilingUnits = 0.f;
	Npc->TroikaMotor.HullSpeedFloorUnits = 37.f;
	TestEqual(TEXT("#17 floors against the hull table"), Npc->FUN_102e2580(), 37.f, 0.0001f);
	Npc->TroikaMotor.SpeedCeilingUnits = 90.f;
	TestEqual(TEXT("and against the motor's own +0x40, which RAISES rather than clamps"),
		Npc->FUN_102e2580(), 90.f, 0.0001f);
	Npc->TroikaMotor.SpeedCeilingUnits = 0.f;
	Npc->TroikaMotor.HullSpeedFloorUnits = 0.f;

	// `CAI_Motor#15` `0x102e2180`'s blend, as a pure function. One entry at full weight is the
	// normalised delta; a zero-weight entry contributes nothing.
	const FElysiumNpc::FFacingQueueEntry Entries[] = {
		{ FVector(10.0, 0.0, 0.0), 1.0f },
	};
	const FVector Blend = FElysiumNpc::BlendFacingQueue(Entries, FVector::ZeroVector);
	TestEqual(TEXT("#15 normalises the accumulator after every entry"),
		static_cast<float>(Blend.X), 1.f, 0.0001f);
	int32 Survivors = -1;
	const FVector Empty = Npc->FUN_102e2180(Survivors);
	TestEqual(TEXT("with no facing queue #15 answers the zero vector"),
		static_cast<float>(Empty.Size()), 0.f, 0.0001f);
	TestEqual(TEXT("and zero survivors"), Survivors, 0);

	// `CAI_Motor#18` `0x102e19e0` — the clip test is a hard refusal, and without the `move_yaw`
	// pose parameter the pseudo-yaw arm reissues and writes no desired yaw.
	Npc->TroikaMotor.bSteerClipped = true;
	const int32 PoseWrites = Npc->TroikaMotor.PoseParamWrites;
	Npc->FUN_102e19e0(FVector(0.0, 10.0, 0.0));
	TestEqual(TEXT("#18 refuses outright when the owner's clip test holds"),
		Npc->TroikaMotor.PoseParamWrites, PoseWrites);
	Npc->TroikaMotor.bSteerClipped = false;
	Npc->TroikaMotor.bHasMoveYawPoseParam = false;
	const int32 Before18 = Npc->TroikaMotor.MoveReissues;
	Npc->FUN_102e19e0(FVector(0.0, 10.0, 0.0));
	TestEqual(TEXT("#18 without the pose parameter reissues the move"),
		Npc->TroikaMotor.MoveReissues, Before18 + 1);
	TestEqual(TEXT("and writes no pose parameter"), Npc->TroikaMotor.PoseParamWrites, PoseWrites);
	Npc->TroikaMotor.bHasMoveYawPoseParam = true;
	Npc->FUN_102e19e0(FVector(0.0, 10.0, 0.0));
	TestEqual(TEXT("#18 with it writes the desired yaw once"), Npc->TroikaMotor.PoseParamWrites,
		PoseWrites + 1);

	// `CAI_Navigator#7` and `#11` — byte-identical bodies at two distinct slots.
	Npc->Navigator.bNavFailed = false;
	Npc->NavigatorWord0x54 = 0;
	int32 Clears = Npc->NavigatorRouteClears;
	Npc->FUN_102eea70();
	TestTrue(TEXT("#7 sets the navigator's dirty latch"), Npc->Navigator.bNavFailed);
	TestEqual(TEXT("#7 resets +0x54 to -1"), Npc->NavigatorWord0x54, -1);
	TestEqual(TEXT("#7 clears the route once"), Npc->NavigatorRouteClears, Clears + 1);
	Npc->Navigator.bNavFailed = false;
	Npc->NavigatorWord0x54 = 0;
	Clears = Npc->NavigatorRouteClears;
	Npc->FUN_102eeac0();
	TestTrue(TEXT("#11 is the same behaviour at a different slot"), Npc->Navigator.bNavFailed);
	TestEqual(TEXT("#11 resets +0x54 to -1 too"), Npc->NavigatorWord0x54, -1);
	TestEqual(TEXT("#11 clears the route once"), Npc->NavigatorRouteClears, Clears + 1);

	// `CAI_Navigator#17` — the path is a seam, so the block comes back with `bHasPath` clear.
	const FElysiumNpc::FNavMoveInfo Info = Npc->FUN_102eee40();
	TestFalse(TEXT("#17 answers no path while the path object is a seam"), Info.bHasPath);
	TestEqual(TEXT("but it still reports the nav type it read first"), Info.NavType,
		Npc->NavGetType());
	return true;
}

// -------------------------------------------------------------------------------------------------
// `CAI_StandoffBehavior` and the two hint bodies.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelTroikaHelpersStandoffTest,
	"Elysium.Substrate.NpcKernelTroikaHelpers.StandoffAndHints", GTroikaHelpersTestFlags)
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

	// `0x102c7410`. A clear `+0x19` answers false WITHOUT touching anything else; a set one runs
	// into the capability gate, which the seam closes — and THAT arm clears `+0x19`.
	Npc->bStandoffRangedCache = false;
	TestFalse(TEXT("vfunc3 refuses on a clear +0x19"), Npc->StandoffVfunc3());
	Npc->bStandoffRangedCache = true;
	TestFalse(TEXT("and refuses on the closed capability gate"), Npc->StandoffVfunc3());
	TestFalse(TEXT("which is one of the two arms that CLEAR +0x19"), Npc->bStandoffRangedCache);
	TestEqual(TEXT("the capability seam answers 0"),
		static_cast<int32>(Npc->StandoffOwnerCapabilityWord()), 0);
	TestEqual(TEXT("and the sequence seam INDEX_NONE"), Npc->SelectHeaviestSequence(8, INDEX_NONE),
		static_cast<int32>(INDEX_NONE));

	// `0x102c7530`. The hint is cleared UNCONDITIONALLY, the behaviour's `+0x50` lands in
	// `m_flDistTooFar`, and `+0x1fc` is forced to 2.
	Npc->ScheduleHost.HintNode = 4;
	Npc->StandoffDistTooFar = 512.f;
	Npc->DistTooFar = 0.f;
	Npc->Field_0x01fc = 0;
	Npc->StandoffVfunc5();
	TestEqual(TEXT("vfunc5 clears the hint node unconditionally"), Npc->ScheduleHost.HintNode,
		static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("vfunc5 copies the behaviour's +0x50 into m_flDistTooFar"), Npc->DistTooFar,
		512.f, 0.0001f);
	TestEqual(TEXT("and forces the owner's +0x1fc to 2"), Npc->Field_0x01fc, 2);

	// `0x102c7960` / `0x102c79a0` — byte-identical bodies. With no program the first test refuses;
	// with one, the behaviour-local schedule seam answers None and the compare fails.
	Npc->Cognition.Conditions.Set(EElysiumNpcCond::NewEnemy);
	Npc->Schedule.Current = EElysiumScheduleId::None;
	Npc->StandoffVfunc20();
	TestTrue(TEXT("vfunc20 clears nothing with no program installed"),
		Npc->Cognition.Conditions.Has(EElysiumNpcCond::NewEnemy));
	Npc->Schedule.Current = EElysiumScheduleId::IdleStand;
	Npc->StandoffVfunc21();
	TestTrue(TEXT("vfunc21 clears nothing while the behaviour-local id space is a seam"),
		Npc->Cognition.Conditions.Has(EElysiumNpcCond::NewEnemy));
	TestEqual(TEXT("and the seam is what refuses"),
		static_cast<int32>(Npc->StandoffScheduleForLocalId(0x17)),
		static_cast<int32>(EElysiumScheduleId::None));
	Npc->Schedule.Current = EElysiumScheduleId::None;

	// `0x102b6120` — no hint node ZEROES the caller's point and answers false. That is the arm, not
	// a refusal to write.
	Npc->ScheduleHost.HintNode = INDEX_NONE;
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
	TestEqual(TEXT("and no hint is installed"), Npc->ScheduleHost.HintNode,
		static_cast<int32>(INDEX_NONE));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
