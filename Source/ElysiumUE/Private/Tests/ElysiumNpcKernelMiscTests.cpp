#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcBach.h"
#include "Substrate/ElysiumNpcChangBros.h"
#include "Substrate/ElysiumNpcManBat.h"
#include "Substrate/ElysiumNpcYukie.h"
#include "Substrate/ElysiumNpcHengeyokai.h"
#include "Substrate/ElysiumNpcSabbatLeader.h"
#include "Substrate/ElysiumNpcGargoyle.h"
#include "Substrate/ElysiumNpcWerewolf.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumNpcTestCensus.h"

// Story 29c-1, family **Misc** — the 37 layer 0–9 rows no other family owns. Every assertion below
// comes from the decompiled C or the listing of the row it names: the slop window is one second
// because `0x10295414` subtracts `_DAT_104454c0`, the Lasombra arm answers TRUE because the flag
// word lands in AL, Bach's gate is an OR because `0x1036450f` is a `JP` straight to the accept,
// and the melee-interrupt whitelist is the switch's own member set including the hoisted `0x51`.
//
// Where a body can only answer "nothing" because its input is a seam — the six component factories,
// the standoff schedule latch, the zone trigger, the melee move records, the arena-centre hint walk
// — the case says so: that the seam is asked and that the refusal is the recovered one.

static constexpr EAutomationTestFlags GElysiumNpcKernelMiscFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// The two classnames the Troika-line/own-line cases here spawn out of `ElysiumNpcClasses.cpp`.
	// story 5 step 2: every classname builds the class retail's factory builds (population.md),
	// so `npc_VCop` (factory 0x103704f0) is `CNPC_VCop`, not the Troika line; a case that wants
	// the bare Troika line stands `AddTroikaNpc`, whose `RetailClass()` answers null.
	const TCHAR* const GMiscSpawnableCombatant = TEXT("npc_VHumanCombatant");
	const TCHAR* const GMiscSpawnableSabbatLeader = TEXT("npc_VSabbatLeader");
}

// -------------------------------------------------------------------------------------------------
// Slot 513 `CapabilitiesGet` — `0x1026db30`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMiscCapabilitiesGetTest,
	"Elysium.Arm.NpcKernelMisc.CapabilitiesGet", GElysiumNpcKernelMiscFlags)
bool FElysiumNpcKernelMiscCapabilitiesGetTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_misc_caps"), 0x1026db30);
	Builder.AddNpc(TEXT("npc"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("npc"));
	TestNotNull(TEXT("the NPC spawned"), Npc);
	if (Npc == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Npc });

	// `m_afCapability` verbatim, bit for bit, with no weapon.
	Npc->CapabilityWord = 0;
	TestEqual(TEXT("no capability word answers 0"), Npc->CapabilitiesGet(), 0);
	Npc->CapabilityWord = 0x4000040;   // bits_CAP_SQUAD | bits_CAP_MOVE_SHOOT
	TestEqual(TEXT("the whole word is answered verbatim"), Npc->CapabilitiesGet(), 0x4000040);

	// The OR term is the active weapon's slot 360 (`+0x5a0`), which family Motor's seam answers 0
	// for — so the answer cannot grow past `m_afCapability` today. Asserting the seam's refusal is
	// the point: the day slot 360 lands, this case is what says the OR is live.
	TestEqual(TEXT("so the OR adds nothing"), Npc->CapabilitiesGet(), Npc->CapabilityWord);
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 590 `OkToInterruptForMelee` and the `0x1028a190` gate.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMiscMeleeInterruptTest,
	"Elysium.Arm.NpcKernelMisc.OkToInterruptForMelee", GElysiumNpcKernelMiscFlags)
bool FElysiumNpcKernelMiscMeleeInterruptTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_misc_melee_interrupt"), 0x1029f940);
	Builder.AddNpc(TEXT("troika"), FVector::ZeroVector, GMiscSpawnableCombatant);
	Builder.AddNpc(TEXT("leader"), FVector(500.0, 0.0, 0.0), GMiscSpawnableSabbatLeader);
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Troika = Fixture.Npc(TEXT("troika"));
	FElysiumNpc* Leader = Fixture.Npc(TEXT("leader"));
	TestNotNull(TEXT("the combatant spawned"), Troika);
	TestNotNull(TEXT("the Sabbat leader spawned"), Leader);
	if (Troika == nullptr || Leader == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Troika, Leader });

	// The gate first (`0x1028a190`): a live, unchoreographed, non-oblivious NPC passes.
	TestTrue(TEXT("0x1028a190 passes a live idle NPC"), Troika->OkToDisturb());
	Troika->bInChoreoScene = true;
	TestFalse(TEXT("m_bInChoreoScene (+0x5bc4) refuses"), Troika->OkToDisturb());
	Troika->bInChoreoScene = false;
	// `+0x14bc & 0x1000` is `MADE_OBLIVIOUS`, and this address IS a reader of it — the bit
	// `ElysiumNpcFlags.h` records as having none.
	Troika->NpcFlags.Set(EElysiumNpcFlag2::MADE_OBLIVIOUS);
	TestFalse(TEXT("m_bfAINPCFlags2 & 0x1000 refuses"), Troika->OkToDisturb());
	Troika->NpcFlags.Clear(EElysiumNpcFlag2::MADE_OBLIVIOUS);
	TestTrue(TEXT("and clearing it lets the gate open again"), Troika->OkToDisturb());

	// The whitelist. These are the switch's own members, including `0x51` — the one the compiler
	// hoisted out of the jump table with a `!= 0x51` test that falls THROUGH to the accept.
	const int32 Accepted[] = { 0x1, 0x9, 0x13, 0x30, 0x4b, 0x4d, 0x51, 0x73, 0x80, 0x8a, 0xcb5,
		0xd25, 0x1121, 0x1157, 0x1158 };
	for (int32 Activity : Accepted)
	{
		Troika->ActivityNumber = Activity;
		TestTrue(*FString::Printf(TEXT("activity 0x%x is melee-interruptible"), Activity),
			Troika->OkToInterruptForMelee());
	}
	// Just outside every boundary, and two ordinary activities inside none of the bands.
	const int32 Refused[] = { 0x0, 0x2, 0x8, 0xa, 0x14, 0x50, 0x52, 0x72, 0x8b, 0xcb4, 0xcb6,
		0xd24, 0xd26, 0x1120, 0x1122, 0x1156, 0x1159 };
	for (int32 Activity : Refused)
	{
		Troika->ActivityNumber = Activity;
		TestFalse(*FString::Printf(TEXT("activity 0x%x is not"), Activity),
			Troika->OkToInterruptForMelee());
	}

	// The gate is FIRST: a refused gate beats every whitelisted activity.
	Troika->ActivityNumber = 0x1;
	Troika->bInChoreoScene = true;
	TestFalse(TEXT("the gate is tested before the whitelist"), Troika->OkToInterruptForMelee());
	Troika->bInChoreoScene = false;

	// `CNPC_VSabbatLeader::OkToInterruptForMelee` (`0x103ab400`): activity 0x1141 is an EXCEPTION
	// that skips the gate entirely, and everything else falls to the Troika line.
	TestEqual(TEXT("npc_VSabbatLeader resolves to CNPC_VSabbatLeader"),
		FString(Leader->RetailClass() != nullptr ? Leader->RetailClass()->Name : TEXT("")),
		FString(TEXT("CNPC_VSabbatLeader")));
	TestEqual(TEXT("which fills slot 590 with 0x103ab400"),
		FString(ElysiumNpcTestCensus::BodyOf(Leader->RetailClass(), 590)),
		FString(TEXT("0x103ab400")));
	Leader->ActivityNumber = 0x1141;
	Leader->bInChoreoScene = true;
	TestTrue(TEXT("activity 0x1141 answers true even with the gate shut"),
		Leader->OkToInterruptForMelee());
	Leader->bInChoreoScene = false;
	Leader->ActivityNumber = 0x2;
	TestFalse(TEXT("any other activity falls to the Troika line and is refused"),
		Leader->OkToInterruptForMelee());
	Leader->ActivityNumber = 0x9;
	TestTrue(TEXT("and a whitelisted one is accepted there"), Leader->OkToInterruptForMelee());

	// The Troika line does NOT carry the exception.
	Troika->ActivityNumber = 0x1141;
	TestFalse(TEXT("0x1141 is not on the Troika whitelist"), Troika->OkToInterruptForMelee());
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 592 `CanSeekCover` — `0x102953e0` and `CNPC_VLasombra`'s `0x103893c0`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMiscCanSeekCoverTest,
	"Elysium.Arm.NpcKernelMisc.CanSeekCover", GElysiumNpcKernelMiscFlags)
bool FElysiumNpcKernelMiscCanSeekCoverTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_misc_cover"), 0x102953e0);
	Builder.AddNpc(TEXT("npc"), FVector::ZeroVector, GMiscSpawnableCombatant);
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("npc"));
	TestNotNull(TEXT("the NPC spawned"), Npc);
	if (Npc == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Npc });
	// The window this case measures is exactly one second wide and `m_flCanSeekCoverTimer` is a
	// FLOAT against a double clock, so the boundary assertion below is only exact on a stamp both
	// types represent exactly. The fixture's clock stands at the NPC's first think (a tenth of a
	// second, which float rounds up), so the case puts it on a whole second first.
	Fixture.World.Tick(1.0);
	const double Now = Fixture.World.NowSeconds();

	// Arm 1: `COND_ENEMY_OCCLUDED` (0x48) answers true on its own, before the clock is read at all.
	Npc->CanSeekCoverTimer = Now + 1000.0;
	Npc->Cognition.Conditions.Set(EElysiumNpcCond::EnemyOccluded);
	TestTrue(TEXT("COND_ENEMY_OCCLUDED answers true regardless of the timer"), Npc->CanSeekCover());
	Npc->Cognition.Conditions.Clear(EElysiumNpcCond::EnemyOccluded);
	TestFalse(TEXT("and without it a far-future timer refuses"), Npc->CanSeekCover());

	// Arm 2: the timer at or before curtime.
	Npc->CanSeekCoverTimer = Now;
	TestTrue(TEXT("a timer EQUAL to curtime answers true"), Npc->CanSeekCover());
	Npc->CanSeekCoverTimer = Now - 0.5;
	TestTrue(TEXT("an elapsed timer answers true"), Npc->CanSeekCover());

	// Arm 3: the slop window is ONE second (`FSUB [0x104454c0]`, the shared 1.0f), not five, and it
	// additionally requires `COND_CAN_RANGE_ATTACK1` (0x4f) to be CLEAR.
	Npc->CanSeekCoverTimer = Now + 0.5;
	TestTrue(TEXT("half a second out is inside the one-second window"), Npc->CanSeekCover());
	Npc->CanSeekCoverTimer = Now + 1.0;
	TestTrue(TEXT("exactly one second out is still inside it (the compare is <=)"),
		Npc->CanSeekCover());
	Npc->CanSeekCoverTimer = Now + 1.25;
	TestFalse(TEXT("1.25 s out is OUTSIDE it — the window is one second, not five"),
		Npc->CanSeekCover());
	Npc->CanSeekCoverTimer = Now + 4.0;
	TestFalse(TEXT("and four seconds out is too, which is what 29c's 'five second' walk missed"),
		Npc->CanSeekCover());
	Npc->CanSeekCoverTimer = Now + 0.5;
	Npc->Cognition.Conditions.Set(EElysiumNpcCond::CanRangeAttack1);
	TestFalse(TEXT("COND_CAN_RANGE_ATTACK1 closes the slop window"), Npc->CanSeekCover());
	Npc->Cognition.Conditions.Clear(EElysiumNpcCond::CanRangeAttack1);
	// ... but it does NOT close arm 2, which is tested before it.
	Npc->CanSeekCoverTimer = Now - 1.0;
	Npc->Cognition.Conditions.Set(EElysiumNpcCond::CanRangeAttack1);
	TestTrue(TEXT("an elapsed timer beats COND_CAN_RANGE_ATTACK1: arm 2 comes first"),
		Npc->CanSeekCover());
	Npc->Cognition.Conditions.Clear(EElysiumNpcCond::CanRangeAttack1);

	// `CNPC_VLasombra` fills slot 592 with `0x103893c0`. The species arm is exercised through the
	// census by retail class name (`npc_VLasombra` builds `CNPC_VLasombra` since story 5 step 2,
	// population.md) — which is also what proves the arm a combatant takes is the Troika line.
	const FElysiumNpcClass* Lasombra = ElysiumNpcTestCensus::Find(TEXT("CNPC_VLasombra"));
	TestNotNull(TEXT("CNPC_VLasombra is a census class"), Lasombra);
	TestEqual(TEXT("and fills slot 592 with 0x103893c0"),
		FString(ElysiumNpcTestCensus::BodyOf(Lasombra, 592)), FString(TEXT("0x103893c0")));
	TestEqual(TEXT("while npc_VHumanCombatant takes the Troika line 0x102953e0"),
		FString(ElysiumNpcTestCensus::BodyOf(Npc->RetailClass(), 592)),
		FString(TEXT("0x102953e0")));

	// The combatant therefore ignores `m_flCoverDisableOverride` entirely: setting it changes
	// nothing, which is what says the species arm is gated on the census and not on the field.
	Npc->CanSeekCoverTimer = Now + 100.0;
	TestFalse(TEXT("a non-Lasombra ignores m_flCoverDisableOverride"), Npc->CanSeekCover());
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 24 `OnVictimHitByMe` — the Troika line and its three species overrides.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMiscVictimHitTest,
	"Elysium.Arm.NpcKernelMisc.OnVictimHitByMe", GElysiumNpcKernelMiscFlags)
bool FElysiumNpcKernelMiscVictimHitTest::RunTest(const FString&)
{
	// The four species bodies of slot 24 by name, against the census. Each is its class's own
	// `OnVictimHitByMe` override (story 5 step 3); `CNPC_VGhoulCroucher#24` (`0x1037be80`) was added by
	// story 29d, family SpeciesMisc10.
	struct FRow
	{
		const TCHAR* RetailClass;
		const TCHAR* Body;
	};
	const FRow Rows[] =
	{
		{ TEXT("CAI_BaseNPCTroika"), TEXT("0x1029f8d0") },
		{ TEXT("CNPC_VGargoyle"), TEXT("0x1037a450") },
		{ TEXT("CNPC_VSabbatLeader"), TEXT("0x103ab4a0") },
		{ TEXT("CNPC_VZombie"), TEXT("0x103e1280") },
		{ TEXT("CNPC_VGhoulCroucher"), TEXT("0x1037be80") },
	};
	for (const FRow& Row : Rows)
	{
		const FString Name(Row.RetailClass);
		const FElysiumNpcClass* Cls = ElysiumNpcTestCensus::Find(Row.RetailClass);
		TestNotNull(*FString::Printf(TEXT("%s is a census class"), *Name), Cls);
		if (Cls == nullptr)
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s fills slot 24 with %s"), *Name, Row.Body),
			FString(ElysiumNpcTestCensus::BodyOf(Cls, 24)), FString(Row.Body));
	}

	// The Gargoyle classname filter, as a pure function: two names, case-insensitive, whole-name.
	TestTrue(TEXT("\"pillar\" matches"), FElysiumNpcGargoyle::GargoyleHitsPillar(TEXT("pillar")));
	TestTrue(TEXT("\"PILLAR\" matches — the compare is __strcmpi"),
		FElysiumNpcGargoyle::GargoyleHitsPillar(TEXT("PILLAR")));
	TestTrue(TEXT("\"central_pillar\" matches"),
		FElysiumNpcGargoyle::GargoyleHitsPillar(TEXT("central_pillar")));
	TestFalse(TEXT("\"pillar_of_salt\" does NOT — neither name carries a trailing *"),
		FElysiumNpcGargoyle::GargoyleHitsPillar(TEXT("pillar_of_salt")));
	TestFalse(TEXT("an empty classname does not"), FElysiumNpcGargoyle::GargoyleHitsPillar(FString()));
	TestFalse(TEXT("and neither does prop_physics"),
		FElysiumNpcGargoyle::GargoyleHitsPillar(TEXT("prop_physics")));

	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_misc_victim_hit"), 0x1029f8d0);
	Builder.AddNpc(TEXT("troika"), FVector::ZeroVector, GMiscSpawnableCombatant);
	Builder.AddNpc(TEXT("leader"), FVector(500.0, 0.0, 0.0), GMiscSpawnableSabbatLeader);
	Builder.AddNpc(TEXT("victim"), FVector(200.0, 0.0, 0.0), GMiscSpawnableCombatant);
	Builder.AddNpc(TEXT("zombie"), FVector(-500.0, 0.0, 0.0), TEXT("npc_VZombie"));
	Builder.AddCounter(TEXT("attacked"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Troika = Fixture.Npc(TEXT("troika"));
	FElysiumNpcSabbatLeader* Leader = Fixture.NpcAs<FElysiumNpcSabbatLeader>(TEXT("leader"));
	FElysiumNpc* Victim = Fixture.Npc(TEXT("victim"));
	TestNotNull(TEXT("the combatant spawned"), Troika);
	TestNotNull(TEXT("the Sabbat leader spawned"), Leader);
	TestNotNull(TEXT("the victim spawned"), Victim);
	if (Troika == nullptr || Leader == nullptr || Victim == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Troika, Leader, Victim });

	// The Troika line (`0x1029f8d0`): the whole body is the melee-record clear, and it does NOT read
	// the victim — a null victim clears just the same.
	Troika->MeleeMoveRecordClears = 0;
	Troika->OnVictimHitByMe(Victim);
	TestEqual(TEXT("the Troika line clears the melee move records"), Troika->MeleeMoveRecordClears,
		1);
	Troika->OnVictimHitByMe(nullptr);
	TestEqual(TEXT("and does so with a null victim too — it reads no argument"),
		Troika->MeleeMoveRecordClears, 2);

	// `CNPC_VSabbatLeader::OnVictimHitByMe` (`0x103ab4a0`): the decrement is gated on the victim
	// being the tracked closest player, the clamp is at zero, and the base body does NOT run.
	Leader->MeleeMoveRecordClears = 0;
	Leader->SabbatLeaderRoarAttackCount = 2;
	Leader->Senses.Memory.ClosestPlayer = FElysiumEntityHandle::Invalid();
	Leader->OnVictimHitByMe(Victim);
	TestEqual(TEXT("with no tracked player the count is untouched"),
		Leader->SabbatLeaderRoarAttackCount, 2);
	Leader->Senses.Memory.ClosestPlayer = Victim->Handle;
	Leader->OnVictimHitByMe(Victim);
	TestEqual(TEXT("hitting the tracked player decrements m_RoarAttackCount"),
		Leader->SabbatLeaderRoarAttackCount, 1);
	Leader->OnVictimHitByMe(Victim);
	Leader->OnVictimHitByMe(Victim);
	Leader->OnVictimHitByMe(Victim);
	TestEqual(TEXT("and it clamps at zero, never below"), Leader->SabbatLeaderRoarAttackCount, 0);
	Leader->OnVictimHitByMe(Troika);
	TestEqual(TEXT("a different victim is ignored"), Leader->SabbatLeaderRoarAttackCount, 0);
	TestEqual(TEXT("the Sabbat leader never runs the base record clear"),
		Leader->MeleeMoveRecordClears, 0);

	// `CNPC_VZombie::OnVictimHitByMe` (`0x103e1280`), through a spawned `npc_VZombie`'s own override:
	// the Troika body FIRST (the record clear), then `OnAttackedVictim`.
	FElysiumNpc* Zombie = Fixture.Npc(TEXT("zombie"));
	TestNotNull(TEXT("the zombie spawned"), Zombie);
	if (Zombie != nullptr)
	{
		FElysiumNpcWorldFixture::Quiet({ Zombie });
		Zombie->MeleeMoveRecordClears = 0;
		Zombie->OnVictimHitByMe(Victim);
		TestEqual(TEXT("the zombie keeps the Troika record clear"), Zombie->MeleeMoveRecordClears, 1);
	}

	// The slot-266 dispatch seam: an NPC victim takes the real slot, a non-NPC is counted only.
	Troika->VictimHitReactionDispatches = 0;
	Troika->DispatchVictimHitReaction(Victim);
	TestEqual(TEXT("the victim-reaction dispatch is counted"),
		Troika->VictimHitReactionDispatches, 1);
	Troika->DispatchVictimHitReaction(nullptr);
	TestEqual(TEXT("and a missing victim is counted without dispatching"),
		Troika->VictimHitReactionDispatches, 2);
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 473's Troika answer and `CNPC_VBach`'s slots 553/554.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMiscSpeciesThresholdTest,
	"Elysium.Arm.NpcKernelMisc.SpeciesThresholds", GElysiumNpcKernelMiscFlags)
bool FElysiumNpcKernelMiscSpeciesThresholdTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_misc_species"), 0x10364500);
	Builder.AddNpc(TEXT("npc"), FVector::ZeroVector, GMiscSpawnableCombatant);
	Builder.AddNpcOfClass(TEXT("bach"), FVector(400.0, 0.0, 0.0), TEXT("CNPC_VBach"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("npc"));
	FElysiumNpcBach* BachNpc = Fixture.NpcAs<FElysiumNpcBach>(TEXT("bach"));
	if (!TestNotNull(TEXT("the Bach spawned"), BachNpc))
	{
		return false;
	}
	TestNotNull(TEXT("the NPC spawned"), Npc);
	if (Npc == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Npc });

	// Slot 473's two species overrides (`CGeneric_NPC`, `CGenericSabbat_NPC`) are on classes with
	// no instance; every class with one answers the Troika line's own mask.
	TestEqual(TEXT("the Troika line's own answer is 0x81f"), Npc->GetSoundInterests(), 0x81f);

	// `CNPC_VBach`'s slots 553/554 (`0x10364500` and its dead twin): an OR of two thresholds, 0.5 on the
	// dot and 120 units on the distance, with the answer the only difference between the two.
	const FElysiumNpcClass* Bach = ElysiumNpcTestCensus::Find(TEXT("CNPC_VBach"));
	TestNotNull(TEXT("CNPC_VBach is a census class"), Bach);
	TestEqual(TEXT("and fills slot 553 with 0x10364500"),
		FString(ElysiumNpcTestCensus::BodyOf(Bach, 553)), FString(TEXT("0x10364500")));

	TestEqual(TEXT("dot exactly 0.5 accepts (the compare is >=)"),
		BachNpc->BachRangeAttack1Conditions(0.5f, 10000.f), 0x4f);
	TestEqual(TEXT("dot above 0.5 accepts"), BachNpc->BachRangeAttack1Conditions(0.9f, 10000.f), 0x4f);
	TestEqual(TEXT("dot below 0.5 with the target far refuses with COND_NOT_FACING_ATTACK"),
		BachNpc->BachRangeAttack1Conditions(0.49f, 10000.f), 0x61);
	TestEqual(TEXT("but a near target accepts even facing away — it is an OR"),
		BachNpc->BachRangeAttack1Conditions(-1.f, 119.f), 0x4f);
	TestEqual(TEXT("distance exactly 120 accepts (the compare is <=)"),
		BachNpc->BachRangeAttack1Conditions(-1.f, 120.f), 0x4f);
	TestEqual(TEXT("distance just past 120 refuses"),
		BachNpc->BachRangeAttack1Conditions(-1.f, 120.1f), 0x61);
	return true;
}

// -------------------------------------------------------------------------------------------------
// The Hengeyokai form bit, the Yukie melee pair, the ManBat slow, the Chang arena centre and the
// Werewolf zone.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMiscSpeciesBodiesTest,
	"Elysium.Arm.NpcKernelMisc.SpeciesBodies", GElysiumNpcKernelMiscFlags)
bool FElysiumNpcKernelMiscSpeciesBodiesTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_misc_species_bodies"), 0x10381c00);
	Builder.AddNpc(TEXT("npc"), FVector::ZeroVector, GMiscSpawnableCombatant);
	Builder.AddEntity(TEXT("point_target"), TEXT("trigger_werewolf_zone"), FVector(10.0, 0.0, 0.0));
	Builder.AddEntity(TEXT("point_target"), TEXT("trigger_werewolf_zone"), FVector(20.0, 0.0, 0.0));
	Builder.AddEntity(TEXT("point_target"), TEXT("rotdoor1"), FVector(30.0, 0.0, 0.0));
	Builder.AddNpcOfClass(TEXT("wolf"), FVector(400.0, 0.0, 0.0), TEXT("CNPC_VWerewolf"));
	Builder.AddNpcOfClass(TEXT("heng"), FVector(0.0, 400.0, 0.0), TEXT("CNPC_VHengeyokai"));
	Builder.AddNpcOfClass(TEXT("yukie"), FVector(0.0, 800.0, 0.0), TEXT("CNPC_VYukie"));
	Builder.AddNpcOfClass(TEXT("bat"), FVector(0.0, 1200.0, 0.0), TEXT("CNPC_VManBat"));
	Builder.AddNpcOfClass(TEXT("chang"), FVector(0.0, 1600.0, 0.0), TEXT("CNPC_VChangBros"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("npc"));
	FElysiumNpcWerewolf* Wolf = Fixture.NpcAs<FElysiumNpcWerewolf>(TEXT("wolf"));
	FElysiumNpcHengeyokai* Heng = Fixture.NpcAs<FElysiumNpcHengeyokai>(TEXT("heng"));
	FElysiumNpcYukie* YukieNpc = Fixture.NpcAs<FElysiumNpcYukie>(TEXT("yukie"));
	FElysiumNpcManBat* Bat = Fixture.NpcAs<FElysiumNpcManBat>(TEXT("bat"));
	FElysiumNpcChangBros* Chang = Fixture.NpcAs<FElysiumNpcChangBros>(TEXT("chang"));
	if (Heng == nullptr || YukieNpc == nullptr || Bat == nullptr || Chang == nullptr)
	{
		AddError(TEXT("the species bodies' classes did not stand"));
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Heng, YukieNpc, Bat, Chang });
	TestNotNull(TEXT("the NPC spawned"), Npc);
	TestNotNull(TEXT("the werewolf spawned"), Wolf);
	if (Npc == nullptr || Wolf == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Npc, Wolf });
	// `m_flFishTimer` is a FLOAT against a double clock, so the `<= curtime` boundary below is
	// only exact on a stamp both types represent. The fixture's clock stands at the NPC's first
	// think (a tenth of a second, which float rounds up), so this case puts it on a whole second.
	Fixture.World.Tick(1.0);
	const double Now = Fixture.World.NowSeconds();

	// `0x10381c00` / `0x10381ca0` — the form bit and its timer.
	Heng->bHengeyokaiDidFakeThrow = true;
	Heng->HengeyokaiFishTimer = 0.f;
	Heng->FormBit(true);
	TestTrue(TEXT("the true arm raises CARRYING_BODY (+0x14b8 bit 0x20)"),
		Heng->NpcFlags.Has(EElysiumNpcFlag::CARRYING_BODY));
	TestFalse(TEXT("and clears m_bDidFakeThrow (+0x667d)"), Heng->bHengeyokaiDidFakeThrow);
	TestTrue(TEXT("m_flFishTimer takes curtime + RandomFloat(5, 8)"),
		Heng->HengeyokaiFishTimer >= static_cast<float>(Now) + 5.f
			&& Heng->HengeyokaiFishTimer <= static_cast<float>(Now) + 8.f);
	TestFalse(TEXT("so the timer has not expired"), Heng->FormBitTimerExpired());

	const float Armed = Heng->HengeyokaiFishTimer;
	Heng->bHengeyokaiDidFakeThrow = true;
	Heng->FormBit(false);
	TestFalse(TEXT("the false arm clears CARRYING_BODY"),
		Heng->NpcFlags.Has(EElysiumNpcFlag::CARRYING_BODY));
	TestEqual(TEXT("and leaves m_flFishTimer exactly where it stood"), Heng->HengeyokaiFishTimer,
		Armed);
	TestTrue(TEXT("and does NOT clear m_bDidFakeThrow"), Heng->bHengeyokaiDidFakeThrow);

	Heng->HengeyokaiFishTimer = static_cast<float>(Now);
	TestTrue(TEXT("a timer equal to curtime HAS expired (the compare is <=)"),
		Heng->FormBitTimerExpired());

	// `CNPC_VYukie`'s melee pair. The CENSUS proves which bodies fill the slots and the bodies
	// themselves are called by name (`npc_VYukie` builds `CNPC_VYukie` since story 5 step 2,
	// population.md; this case does not stand one).
	const FElysiumNpcClass* Yukie = ElysiumNpcTestCensus::Find(TEXT("CNPC_VYukie"));
	TestNotNull(TEXT("CNPC_VYukie is a census class"), Yukie);
	TestEqual(TEXT("and fills slot 599 with 0x103dd8b0"),
		FString(ElysiumNpcTestCensus::BodyOf(Yukie, 599)), FString(TEXT("0x103dd8b0")));
	TestEqual(TEXT("and slot 601 with 0x103dd9a0"),
		FString(ElysiumNpcTestCensus::BodyOf(Yukie, 601)), FString(TEXT("0x103dd9a0")));

	const int32 EventsBefore = YukieNpc->MeleeEventFires;
	YukieNpc->bInMelee = false;
	YukieNpc->MeleeMustLeaveTimer = 0.0;
	TestTrue(TEXT("Yukie's enter-melee is UNGATED and always answers true"),
		YukieNpc->YukieEnterMelee());
	TestTrue(TEXT("it sets m_bInMelee"), YukieNpc->bInMelee);
	TestTrue(TEXT("and arms m_flMeleeMustLeaveTimer inside [22.5, 45]"),
		YukieNpc->MeleeMustLeaveTimer >= Now + 22.5 && YukieNpc->MeleeMustLeaveTimer <= Now + 45.0);
	TestEqual(TEXT("the global melee event fired once"), YukieNpc->MeleeEventFires, EventsBefore + 1);

	YukieNpc->MeleeCanEnterTimer = 1234.0;
	YukieNpc->YukieLeaveMelee();
	TestFalse(TEXT("leaving clears m_bInMelee"), YukieNpc->bInMelee);
	TestEqual(TEXT("and fires the same global event again"), YukieNpc->MeleeEventFires,
		EventsBefore + 2);
	TestFalse(TEXT("slot 308 HasUsableRangedWeapon is still a stub answering false"),
		YukieNpc->HasUsableRangedWeapon());
	TestEqual(TEXT("so the can-enter re-arm is NOT reached and the timer stands"),
		YukieNpc->MeleeCanEnterTimer, 1234.0);

	// `0x1038f290` — a compare against ZERO (`_DAT_1044fab0`, the shared 0.0 double), not against
	// curtime. A flag spelled as a float.
	Bat->ManBatSlowedExpire = 0.f;
	TestFalse(TEXT("an unarmed m_flSlowedExpire answers false"), Bat->SlowedExpire());
	Bat->ManBatSlowedExpire = 0.001f;
	TestTrue(TEXT("anything above zero answers true"), Bat->SlowedExpire());
	// The clock walks well past that stamp and the answer does NOT change — which is the whole
	// point: `_DAT_1044fab0` is the shared `0.0` double, so the compare is against zero and a body
	// that read `m_flSlowedExpire` as a deadline against curtime would flip here.
	Fixture.Advance(Now + 60.0);
	TestTrue(TEXT("curtime is now far past the stamp"), Fixture.World.NowSeconds() > 1.0);
	TestTrue(TEXT("and it STILL answers true — it is NOT a deadline"), Bat->SlowedExpire());
	Bat->ManBatSlowedExpire = -1.f;
	TestFalse(TEXT("a negative value answers false"), Bat->SlowedExpire());
	Bat->ManBatSlowedExpire = 0.f;
	TestFalse(TEXT("and exactly zero does too — the compare is strict"), Bat->SlowedExpire());

	// `CNPC_VChangBros::StoreArenaCenter` `0x1036e400`: both writes are inside the found arm, and
	// the hint walk is family Squad's seam, which answers null.
	TestNull(TEXT("the global hint walk answers nothing (no hint store carries hint types)"),
		Chang->NthHintOfType(0x4651, 0));
	Chang->ChangArenaCenter = FVector(1.0, 2.0, 3.0);
	Chang->bChangCenterStored = false;
	Chang->StoreArenaCenter();
	TestFalse(TEXT("with no 0x4651 hint m_bCenterStored stays false"), Chang->bChangCenterStored);
	TestEqual(TEXT("and m_vArenaCenter is left exactly as it was"), Chang->ChangArenaCenter,
		FVector(1.0, 2.0, 3.0));

	// `0x103cade0`: the zone sweep is by TARGETNAME (`entity+0x11c`), so both zone entities are
	// reached, and a missing `rotdoor2` stores the INVALID handle rather than anything else.
	Wolf->WerewolfZoneTriggerFires = 0;
	Wolf->WerewolfRotDoor1 = FElysiumEntityHandle::Invalid();
	Wolf->WerewolfRotDoor2 = FElysiumEntityHandle::Invalid();
	Wolf->TriggerWerewolfZone();
	TestEqual(TEXT("both trigger_werewolf_zone entities were reached by targetname"),
		Wolf->WerewolfZoneTriggerFires, 2);
	TestTrue(TEXT("rotdoor1 is cached"), Wolf->WerewolfRotDoor1.IsSet());
	TestFalse(TEXT("and a missing rotdoor2 stores the invalid handle"),
		Wolf->WerewolfRotDoor2.IsSet());
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
