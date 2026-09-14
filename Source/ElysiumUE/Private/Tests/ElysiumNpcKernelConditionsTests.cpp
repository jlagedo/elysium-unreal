#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumLaw.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSchedule.h"
#include "Tests/ElysiumNpcTestFixture.h"

// Story 29c-1, family **Conditions**. Every assertion here comes off the decompiled C or off a
// constant read out of retail `vampire.dll`'s `.rdata`; the addresses are cited beside each case so
// a reader can check the number rather than the name.

static constexpr EAutomationTestFlags GNpcKernelCondFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// One NPC, one wired counter per output this family fires, and a player.
	struct FNpcKernelCondFixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumNpc* Npc = nullptr;

		explicit FNpcKernelCondFixture(const TCHAR* Classname = TEXT("npc_VHumanCombatant"))
			: World([Classname]()
				{
					FElysiumNpcWorldBuilder B(TEXT("sp_kernel_conditions"), 29031);
					B.AddNpc(TEXT("subject"), FVector(0.0, 0.0, 0.0), Classname);
					B.AddCounter(TEXT("disturbed"));
					B.AddCounter(TEXT("disturbedbyplayer"));
					B.AddCounter(TEXT("deathtriggered"));
					B.AddCounter(TEXT("copstart"));
					B.AddCounter(TEXT("copend"));
					B.AddCounter(TEXT("hunterstart"));
					B.AddCounter(TEXT("hunterend"));
					B.WireOutput(TEXT("subject"), TEXT("OnDisturbed"), TEXT("disturbed"));
					B.WireOutput(TEXT("subject"), TEXT("OnDisturbedByPlayer"),
						TEXT("disturbedbyplayer"));
					B.WireOutput(TEXT("subject"), TEXT("OnConditionDeathTriggered"),
						TEXT("deathtriggered"));
					return B;
				}())
		{
			Npc = World.Npc(TEXT("subject"));
			FElysiumNpcWorldFixture::Quiet({ Npc });
			FElysiumNpcWorldFixture::PrepareForKernelDrive(Npc);
		}
	};

	int32 CondNum(EElysiumNpcCond Cond) { return static_cast<int32>(Cond); }
}

// =================================================================================================
// Slot 560 — `ClearAttackConditions` (`0x1026dc80`)
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCondClearAttackTest,
	"Elysium.Substrate.NpcKernelConditions.ClearAttackConditions", GNpcKernelCondFlags)
bool FElysiumNpcKernelCondClearAttackTest::RunTest(const FString&)
{
	FNpcKernelCondFixture F;
	if (!TestNotNull(TEXT("the subject spawned"), F.Npc)) { return false; }

	// The eleven the body clears, by the number the binary pushes.
	const EElysiumNpcCond Cleared[] = {
		EElysiumNpcCond::CanRangeAttack1, EElysiumNpcCond::CanRangeAttack2,
		EElysiumNpcCond::CanMeleeAttack1, EElysiumNpcCond::CanMeleeAttack2,
		EElysiumNpcCond::ExtendedBlockedByFriend, EElysiumNpcCond::WaitingAttackTime,
		EElysiumNpcCond::WeaponHasLos, EElysiumNpcCond::WeaponBlockedByFriend,
		EElysiumNpcCond::WeaponPlayerInSpread, EElysiumNpcCond::WeaponPlayerNearTarget,
		EElysiumNpcCond::WeaponSightOccluded,
	};
	TestEqual(TEXT("the retail list is 0x4f,0x50,0x51,0x52,0x2e,0x2f,0x62,99,100,0x65,0x66"),
		CondNum(Cleared[6]), 0x62);
	TestEqual(TEXT("99 decimal is WEAPON_BLOCKED_BY_FRIEND"), CondNum(Cleared[7]), 99);
	TestEqual(TEXT("100 decimal is WEAPON_PLAYER_IN_SPREAD"), CondNum(Cleared[8]), 100);

	for (const EElysiumNpcCond Cond : Cleared)
	{
		F.Npc->Cognition.Conditions.Set(Cond);
	}
	// Two the body must NOT touch: one attack-adjacent, one from another family entirely.
	F.Npc->Cognition.Conditions.Set(EElysiumNpcCond::TooCloseToAttack);   // 0x5f
	F.Npc->Cognition.Conditions.Set(EElysiumNpcCond::SeeEnemy);          // 0x46

	F.Npc->ClearAttackConditions();

	for (const EElysiumNpcCond Cond : Cleared)
	{
		TestFalse(FString::Printf(TEXT("0x%02x cleared"), CondNum(Cond)),
			F.Npc->Cognition.Conditions.Has(Cond));
	}
	TestTrue(TEXT("TOO_CLOSE_TO_ATTACK is not in the list and survives"),
		F.Npc->Cognition.Conditions.Has(EElysiumNpcCond::TooCloseToAttack));
	TestTrue(TEXT("SEE_ENEMY survives"),
		F.Npc->Cognition.Conditions.Has(EElysiumNpcCond::SeeEnemy));
	return true;
}

// =================================================================================================
// Slot 477 — `ClearSenseConditions` (`0x1026e5c0`, table `0x105c97dc`)
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCondClearSenseTest,
	"Elysium.Substrate.NpcKernelConditions.ClearSenseConditions", GNpcKernelCondFlags)
bool FElysiumNpcKernelCondClearSenseTest::RunTest(const FString&)
{
	FNpcKernelCondFixture F;
	if (!TestNotNull(TEXT("the subject spawned"), F.Npc)) { return false; }

	// The fourteen dwords read out of `.rdata` at `0x105c97dc`, in the order they are stored.
	const EElysiumNpcCond Table[] = {
		EElysiumNpcCond::SeeHate, EElysiumNpcCond::SeeDislike, EElysiumNpcCond::SeeEnemy,
		EElysiumNpcCond::SeeFear, EElysiumNpcCond::SeeNemesis, EElysiumNpcCond::SeePlayer,
		EElysiumNpcCond::HearDanger, EElysiumNpcCond::HearCombat, EElysiumNpcCond::HearWorld,
		EElysiumNpcCond::HearPlayer, EElysiumNpcCond::HearThumper, EElysiumNpcCond::HearBugbait,
		EElysiumNpcCond::HearPhysicsDanger, EElysiumNpcCond::Smell,
	};
	TestEqual(TEXT("the table is 0xe entries"), static_cast<int32>(UE_ARRAY_COUNT(Table)), 14);
	const int32 Expected[] = { 0x43, 0x45, 0x46, 0x44, 0x5b, 0x5a, 0x6a, 0x6d, 0x6e, 0x6f, 0x6b,
		0x6c, 0x71, 0x5e };
	for (int32 i = 0; i < 14; ++i)
	{
		TestEqual(FString::Printf(TEXT("entry %d is 0x%02x"), i, Expected[i]),
			CondNum(Table[i]), Expected[i]);
		F.Npc->Cognition.Conditions.Set(Table[i]);
	}
	// The two the HEAR sweep owns that this table does NOT list — the load-bearing omission.
	F.Npc->Cognition.Conditions.Set(EElysiumNpcCond::HearBulletImpact);   // 0x70
	F.Npc->Cognition.Conditions.Set(EElysiumNpcCond::HearFlinch);         // 0x72

	F.Npc->ClearSenseConditions();

	for (const EElysiumNpcCond Cond : Table)
	{
		TestFalse(FString::Printf(TEXT("0x%02x cleared"), CondNum(Cond)),
			F.Npc->Cognition.Conditions.Has(Cond));
	}
	TestTrue(TEXT("HEAR_BULLET_IMPACT (0x70) survives a sense clear"),
		F.Npc->Cognition.Conditions.Has(EElysiumNpcCond::HearBulletImpact));
	TestTrue(TEXT("HEAR_FLINCH (0x72) survives a sense clear"),
		F.Npc->Cognition.Conditions.Has(EElysiumNpcCond::HearFlinch));
	return true;
}

// =================================================================================================
// Slots 553 / 554 — the two ranged bands (`0x1026d890`, `0x1026d920`)
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCondRangeBandsTest,
	"Elysium.Substrate.NpcKernelConditions.RangeAttackConditions", GNpcKernelCondFlags)
bool FElysiumNpcKernelCondRangeBandsTest::RunTest(const FString&)
{
	FNpcKernelCondFixture F;
	if (!TestNotNull(TEXT("the subject spawned"), F.Npc)) { return false; }

	// Slot 553. Thresholds `_DAT_10450564` = 100, `_DAT_104492b8` = 200, `_DAT_1045d650` = 1024,
	// `_DAT_10449270` = 0.5 (a double). Every distance compare is STRICT; the dot carries equality.
	struct FCase { float Dot; float Dist; int32 Expect; const TCHAR* Why; };
	const FCase Range1[] = {
		{ 1.0f,  99.9f, 0x08, TEXT("inside 100 is TOO_CLOSE_FOR_RANGED") },
		{ 1.0f, 100.0f, 0x5f, TEXT("exactly 100 is not inside: the compare is strict") },
		{ 1.0f, 199.9f, 0x5f, TEXT("100..200 is TOO_CLOSE_TO_ATTACK") },
		{ 1.0f, 200.0f, 0x4f, TEXT("exactly 200 leaves the close band") },
		{ 1.0f, 1024.0f, 0x4f, TEXT("exactly 1024 is not too far: the compare is strict") },
		{ 1.0f, 1024.1f, 0x60, TEXT("past 1024 is TOO_FAR_TO_ATTACK") },
		{ 0.5f,  500.0f, 0x4f, TEXT("dot exactly 0.5 passes: the equal bit is carried") },
		{ 0.49f, 500.0f, 0x61, TEXT("below 0.5 is NOT_FACING_ATTACK") },
		// The far test runs BEFORE the dot test, so a badly-facing distant enemy reads TOO_FAR.
		{ 0.0f, 2000.0f, 0x60, TEXT("distance is resolved before facing") },
	};
	for (const FCase& C : Range1)
	{
		TestEqual(C.Why, F.Npc->RangeAttack1Conditions(C.Dot, C.Dist), C.Expect);
	}

	// Slot 554. `_DAT_10451acc` = 64, `_DAT_10483aac` = 512, and NO close-to-attack rung at all.
	const FCase Range2[] = {
		{ 1.0f,  63.9f, 0x08, TEXT("inside 64 is TOO_CLOSE_FOR_RANGED") },
		{ 1.0f,  64.0f, 0x50, TEXT("exactly 64 is already a candidate: there is no 0x5f rung") },
		{ 1.0f, 199.0f, 0x50, TEXT("the band 553 calls TOO_CLOSE_TO_ATTACK is a hit for 554") },
		{ 1.0f, 512.0f, 0x50, TEXT("exactly 512 is not too far") },
		{ 1.0f, 512.1f, 0x60, TEXT("past 512 is TOO_FAR_TO_ATTACK") },
		{ 0.5f, 300.0f, 0x50, TEXT("dot exactly 0.5 passes") },
		{ 0.49f, 300.0f, 0x61, TEXT("below 0.5 is NOT_FACING_ATTACK") },
	};
	for (const FCase& C : Range2)
	{
		TestEqual(C.Why, F.Npc->RangeAttack2Conditions(C.Dot, C.Dist), C.Expect);
	}

	// `CNPC_VBatSwarm` (0x103675e0 / 0x10367610) and `CNPC_VSheriffSwarm` (0x103b2590 / 0x103b25c0)
	// are the only overrides and are unmodified forwards, so the census must show the SAME two
	// bodies filling the slot for those classes as for the Troika line — i.e. no species answer
	// differs. Exercised by name, since neither classname is registered here.
	for (const TCHAR* Cls : { TEXT("CNPC_VBatSwarm"), TEXT("CNPC_VSheriffSwarm") })
	{
		TestNotNull(FString::Printf(TEXT("%s is a census class"), Cls),
			ElysiumNpcKernelClass::Find(Cls));
	}
	return true;
}

// =================================================================================================
// Slot 564 — `FCanCheckAttacks` (`0x102953a0` over `0x10270840`)
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCondCanCheckAttacksTest,
	"Elysium.Substrate.NpcKernelConditions.FCanCheckAttacks", GNpcKernelCondFlags)
bool FElysiumNpcKernelCondCanCheckAttacksTest::RunTest(const FString&)
{
	FNpcKernelCondFixture F;
	if (!TestNotNull(TEXT("the subject spawned"), F.Npc)) { return false; }

	// The base body's two condition terms: SEE_ENEMY (0x46) required, ENEMY_TOO_FAR (0x55) forbidden.
	TestFalse(TEXT("no SEE_ENEMY refuses"), F.Npc->FCanCheckAttacksBase());
	F.Npc->Cognition.Conditions.Set(EElysiumNpcCond::SeeEnemy);
	TestTrue(TEXT("SEE_ENEMY alone is enough on the ground"), F.Npc->FCanCheckAttacksBase());
	F.Npc->Cognition.Conditions.Set(EElysiumNpcCond::EnemyTooFar);
	TestFalse(TEXT("ENEMY_TOO_FAR refuses"), F.Npc->FCanCheckAttacksBase());
	F.Npc->Cognition.Conditions.Clear(EElysiumNpcCond::EnemyTooFar);

	// The nav-type seam. `NAV_GROUND` (0) is what it answers, and 0 is the value that does NOT
	// suppress — so the two refusals `NAV_JUMP` (1) and `NAV_CLIMB` (3) are unreachable today. The
	// seam is asked and the refusal it declines to make is the recovered one.
	TestEqual(TEXT("the nav-type seam answers NAV_GROUND"), F.Npc->NavType(), 0);

	// The Troika body: with no active weapon the capability word is 0, so
	// `bits_CAP_WEAPON_MELEE_ATTACK1` (0x8000) is clear and the suppression arm cannot fire — the
	// slot answers exactly what the base does.
	TestEqual(TEXT("with no weapon slot 564 is the base answer"),
		F.Npc->FCanCheckAttacks(), F.Npc->FCanCheckAttacksBase());
	TestTrue(TEXT("and that answer is true here"), F.Npc->FCanCheckAttacks());

	// The capability the suppression arm tests is the port's recovered melee word, 0x18000, which
	// carries 0x8000. Stated so the mask cannot drift away from `ElysiumNpcCond`'s.
	TestEqual(TEXT("the melee capability word carries CAP_WEAPON_MELEE_ATTACK1"),
		ElysiumNpcCond::MeleeCapabilityBits & 0x8000, 0x8000);
	TestEqual(TEXT("the ranged one does not"),
		ElysiumNpcCond::RangedCapabilityBits & 0x8000, 0);
	return true;
}

// =================================================================================================
// Slot 463 — the species table and the expression map
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCondStateChangeSpeciesTest,
	"Elysium.Substrate.NpcKernelConditions.OnStateChangeSpecies", GNpcKernelCondFlags)
bool FElysiumNpcKernelCondStateChangeSpeciesTest::RunTest(const FString&)
{
	using ESpecies = FElysiumNpc::EStateChangeSpecies;

	// Every row exercised BY NAME, with the shape 29c recovered for it.
	struct FExpect { const TCHAR* Cls; const TCHAR* Body; ESpecies Shape; };
	const FExpect Rows[] = {
		{ TEXT("CNPC_VGuard1"),            TEXT("0x1037d020"), ESpecies::HolsterOnState },
		{ TEXT("CNPC_VHunter"),            TEXT("0x10388880"), ESpecies::HolsterOnState },
		{ TEXT("CNPC_VGhoulCroucher"),     TEXT("0x103871c0"), ESpecies::HolsterOnState },
		{ TEXT("CNPC_VHumanCombatant"),    TEXT("0x103871c0"), ESpecies::HolsterOnState },
		{ TEXT("CNPC_VHumanCombatPatrol"), TEXT("0x103871c0"), ESpecies::HolsterOnState },
		{ TEXT("CNPC_VSabbatGunman"),      TEXT("0x103871c0"), ESpecies::HolsterOnState },
		{ TEXT("CNPC_VStalker"),           TEXT("0x103871c0"), ESpecies::HolsterOnState },
		{ TEXT("CNPC_VYukie"),             TEXT("0x103871c0"), ESpecies::HolsterOnState },
		{ TEXT("CNPC_ProneDialog"),        TEXT("0x103871c0"), ESpecies::HolsterOnState },
		{ TEXT("CNPC_VTzimisce"),          TEXT("0x103ba2c0"), ESpecies::FacialExpression },
		{ TEXT("CNPC_VCamera"),            TEXT("0x10368ea0"), ESpecies::Suppressed },
		{ TEXT("CNPC_VCameraSecurity"),    TEXT("0x10368ea0"), ESpecies::Suppressed },
		{ TEXT("CAI_BaseHumanoid"),        TEXT("0x10260630"), ESpecies::HumanoidPreStep },
		{ TEXT("CNPC_VBach"),              TEXT("0x103639b0"), ESpecies::BachSnapBack },
		{ TEXT("CNPC_VCop"),               TEXT("0x10371c20"), ESpecies::Cop },
	};
	int32 Count = 0;
	FElysiumNpc::StateChangeSpeciesRows(Count);
	TestEqual(TEXT("the table is fifteen rows"), Count, static_cast<int32>(UE_ARRAY_COUNT(Rows)));
	for (const FExpect& E : Rows)
	{
		const FElysiumNpc::FStateChangeSpecies* Row = FElysiumNpc::StateChangeSpeciesOf(E.Cls);
		if (!TestNotNull(FString::Printf(TEXT("%s has a slot-463 row"), E.Cls), Row))
		{
			continue;
		}
		TestEqual(FString::Printf(TEXT("%s's body is %s"), E.Cls, E.Body), FString(Row->Body),
			FString(E.Body));
		TestEqual(FString::Printf(TEXT("%s's shape"), E.Cls), static_cast<int32>(Row->Shape),
			static_cast<int32>(E.Shape));
	}
	TestNull(TEXT("a class with no override runs the Troika body"),
		FElysiumNpc::StateChangeSpeciesOf(TEXT("CNPC_VPedestrian")));

	// `CNPC_VTzimisce`'s expression map, off `PTR_s_normal_10653120` — "normal", "angry", "scream",
	// "dead". "scream" (index 2) is reached by no state.
	TestEqual(TEXT("idle maps to normal"),
		FString(FElysiumNpc::StateChangeExpressionName(EElysiumNpcState::Idle)), FString(TEXT("normal")));
	TestEqual(TEXT("combat maps to angry"),
		FString(FElysiumNpc::StateChangeExpressionName(EElysiumNpcState::Combat)), FString(TEXT("angry")));
	TestEqual(TEXT("alert maps to angry too"),
		FString(FElysiumNpc::StateChangeExpressionName(EElysiumNpcState::Alert)), FString(TEXT("angry")));
	TestEqual(TEXT("dead maps to dead"),
		FString(FElysiumNpc::StateChangeExpressionName(EElysiumNpcState::Dead)), FString(TEXT("dead")));
	TestNull(TEXT("scripted names no expression and writes nothing"),
		FElysiumNpc::StateChangeExpressionName(EElysiumNpcState::Scripted));

	// The seam records the NAME, since this runtime has no SetExpression.
	FNpcKernelCondFixture F;
	if (!TestNotNull(TEXT("the subject spawned"), F.Npc)) { return false; }
	F.Npc->SetDefaultExpression(TEXT("angry"), 1.0f);
	TestEqual(TEXT("the expression seam stores the name"), F.Npc->DefExpression, FString(TEXT("angry")));
	return true;
}

// =================================================================================================
// Slot 463 — `CAI_BaseNPCTroika::OnStateChange` (`0x102ae140`)
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCondStateChangeTroikaTest,
	"Elysium.Substrate.NpcKernelConditions.OnStateChangeTroika", GNpcKernelCondFlags)
bool FElysiumNpcKernelCondStateChangeTroikaTest::RunTest(const FString&)
{
	FNpcKernelCondFixture F;
	if (!TestNotNull(TEXT("the subject spawned"), F.Npc)) { return false; }

	// --- The CHANGED half: idle -> alert (retail 1 -> 3) ---
	F.Npc->bReturnToInitialPos = false;
	F.Npc->ScheduleHost.MemoryBits = 0xffffffffu;
	F.Npc->NpcFlags.Set(EElysiumNpcFlag::PRESERVE_PATH);
	F.Npc->NpcFlags.Set(EElysiumNpcFlag::MADE_HUNT_PATH);
	F.Npc->NpcFlags.Set(EElysiumNpcFlag::AT_CROSSWALK);
	F.Npc->bGoToIdleState = true;
	F.Npc->Cognition.bCondTookDamage = true;
	F.Npc->HuntPatrolPoints = { FVector::ZeroVector, FVector::OneVector };

	F.Npc->OnStateChangeTroika(EElysiumNpcState::Idle, EElysiumNpcState::Alert);

	TestTrue(TEXT("ALERT arms m_bReturnToInitialPos (+0x6494)"), F.Npc->bReturnToInitialPos);
	TestEqual(TEXT("m_afMemory keeps only the low 27 bits (&= 0x07ffffff)"),
		static_cast<int64>(F.Npc->ScheduleHost.MemoryBits), static_cast<int64>(0x07ffffffu));
	TestFalse(TEXT("PRESERVE_PATH is cleared on the ground"),
		F.Npc->NpcFlags.Has(EElysiumNpcFlag::PRESERVE_PATH));
	TestFalse(TEXT("MADE_HUNT_PATH is cleared"),
		F.Npc->NpcFlags.Has(EElysiumNpcFlag::MADE_HUNT_PATH));
	TestFalse(TEXT("AT_CROSSWALK is cleared"),
		F.Npc->NpcFlags.Has(EElysiumNpcFlag::AT_CROSSWALK));
	TestFalse(TEXT("m_bGoToIdleState is cleared"), F.Npc->bGoToIdleState);
	TestFalse(TEXT("m_bCondTookDamage is cleared"), F.Npc->Cognition.bCondTookDamage);
	TestEqual(TEXT("the hunt patrol route is released"), F.Npc->HuntPatrolPoints.Num(), 0);

	// --- COMBAT arms it too; every other reachable state does NOT ---
	F.Npc->bReturnToInitialPos = false;
	F.Npc->OnStateChangeTroika(EElysiumNpcState::Idle, EElysiumNpcState::Combat);
	TestTrue(TEXT("COMBAT arms m_bReturnToInitialPos"), F.Npc->bReturnToInitialPos);

	F.Npc->bReturnToInitialPos = false;
	F.Npc->OnStateChangeTroika(EElysiumNpcState::Alert, EElysiumNpcState::Idle);
	TestFalse(TEXT("IDLE takes the default arm and does NOT arm it"), F.Npc->bReturnToInitialPos);

	// --- DEAD (retail 7) releases the patrol route and SKIPS the flag ---
	F.Npc->bReturnToInitialPos = false;
	F.Npc->OnStateChangeTroika(EElysiumNpcState::Alert, EElysiumNpcState::Dead);
	TestFalse(TEXT("DEAD does not arm m_bReturnToInitialPos"), F.Npc->bReturnToInitialPos);

	// --- The UNCHANGED half: old == new still runs the whole tail ---
	F.Npc->bReturnToInitialPos = false;
	F.Npc->ScheduleHost.MemoryBits = 0xffffffffu;
	F.Npc->NpcFlags.Set(EElysiumNpcFlag::AT_CROSSWALK);
	F.Npc->Cognition.bCondTookDamage = true;
	F.Npc->OnStateChangeTroika(EElysiumNpcState::Alert, EElysiumNpcState::Alert);
	TestFalse(TEXT("no transition does not arm m_bReturnToInitialPos"), F.Npc->bReturnToInitialPos);
	TestEqual(TEXT("and does NOT mask m_afMemory — that write is inside the changed half"),
		static_cast<int64>(F.Npc->ScheduleHost.MemoryBits), static_cast<int64>(0xffffffffu));
	TestFalse(TEXT("but the tail still clears AT_CROSSWALK"),
		F.Npc->NpcFlags.Has(EElysiumNpcFlag::AT_CROSSWALK));
	TestFalse(TEXT("and still clears m_bCondTookDamage"), F.Npc->Cognition.bCondTookDamage);

	// --- The base body `0x1026e3e0` under it, which this runtime derives ---
	TestEqual(TEXT("retail 1 IDLE -> 0x31"),
		static_cast<int32>(FElysiumNpcFlags::NpcStateFlagsForRetailState(1)), 0x31);
	TestEqual(TEXT("retail 2 COMBAT -> 0x8f"),
		static_cast<int32>(FElysiumNpcFlags::NpcStateFlagsForRetailState(2)), 0x8f);
	TestEqual(TEXT("retail 3 ALERT -> 0x39"),
		static_cast<int32>(FElysiumNpcFlags::NpcStateFlagsForRetailState(3)), 0x39);
	TestEqual(TEXT("retail 8 FLEE -> 0x85"),
		static_cast<int32>(FElysiumNpcFlags::NpcStateFlagsForRetailState(8)), 0x85);

	// --- `CNPC_VCamera`'s total suppression: the slot writes NOTHING, not even the tail ---
	FNpcKernelCondFixture Cam;
	if (!TestNotNull(TEXT("the camera fixture subject spawned"), Cam.Npc)) { return false; }
	Cam.Npc->bGoToIdleState = true;
	Cam.Npc->Cognition.bCondTookDamage = true;
	const FElysiumNpc::FStateChangeSpecies* CamRow =
		FElysiumNpc::StateChangeSpeciesOf(TEXT("CNPC_VCamera"));
	if (TestNotNull(TEXT("the camera row exists"), CamRow))
	{
		TestEqual(TEXT("and it is the Suppressed shape"), static_cast<int32>(CamRow->Shape),
			static_cast<int32>(FElysiumNpc::EStateChangeSpecies::Suppressed));
	}
	return true;
}

// =================================================================================================
// Slot 461 — the three species overrides
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCondIdealStateSpeciesTest,
	"Elysium.Substrate.NpcKernelConditions.SelectIdealStateSpecies", GNpcKernelCondFlags)
bool FElysiumNpcKernelCondIdealStateSpeciesTest::RunTest(const FString&)
{
	using ERule = FElysiumNpc::EIdealStateSpecies;

	struct FExpect { const TCHAR* Cls; const TCHAR* Body; ERule Rule; };
	const FExpect Rows[] = {
		{ TEXT("CNPC_VCamera"),           TEXT("0x10369060"), ERule::AlwaysAlert },
		{ TEXT("CNPC_VCameraSecurity"),   TEXT("0x10369060"), ERule::AlwaysAlert },
		{ TEXT("CNPC_VMingXiao"),         TEXT("0x103945a0"), ERule::MingXiao },
		{ TEXT("CNPC_VMingXiaoTentacle"), TEXT("0x1039e310"), ERule::MingXiaoTentacle },
	};
	int32 Count = 0;
	FElysiumNpc::IdealStateSpeciesRows(Count);
	TestEqual(TEXT("the table is four rows"), Count, static_cast<int32>(UE_ARRAY_COUNT(Rows)));
	for (const FExpect& E : Rows)
	{
		const FElysiumNpc::FIdealStateSpecies* Row = FElysiumNpc::IdealStateSpeciesOf(E.Cls);
		if (!TestNotNull(FString::Printf(TEXT("%s has a slot-461 row"), E.Cls), Row))
		{
			continue;
		}
		TestEqual(FString::Printf(TEXT("%s's body is %s"), E.Cls, E.Body), FString(Row->Body),
			FString(E.Body));
		TestEqual(FString::Printf(TEXT("%s's rule"), E.Cls), static_cast<int32>(Row->Rule),
			static_cast<int32>(E.Rule));
	}
	TestNull(TEXT("an ordinary class has no slot-461 override"),
		FElysiumNpc::IdealStateSpeciesOf(TEXT("CNPC_VHumanCombatant")));

	// `CNPC_VCamera::FUN_10369060` — retail 3 ALERT, with NO test at all.
	for (const bool bEnemy : { false, true })
	{
		TestEqual(TEXT("a camera's ideal state is hardcoded ALERT"),
			static_cast<int32>(FElysiumNpc::SelectIdealStateSpecies(ERule::AlwaysAlert, true,
				EElysiumNpcState::Idle, EElysiumNpcState::Idle, bEnemy)),
			static_cast<int32>(EElysiumNpcState::Alert));
	}
	TestEqual(TEXT("even a dead camera reports ALERT: the body writes before any test"),
		static_cast<int32>(FElysiumNpc::SelectIdealStateSpecies(ERule::AlwaysAlert, false,
			EElysiumNpcState::Dead, EElysiumNpcState::Dead, false)),
		static_cast<int32>(EElysiumNpcState::Alert));

	// `CNPC_VMingXiao::vfunc461` — the dead write is ALWAYS overwritten by the enemy test, which is
	// retail's own dead code. So the answer depends on the enemy and on nothing else.
	TestEqual(TEXT("MingXiao with an enemy is COMBAT"),
		static_cast<int32>(FElysiumNpc::SelectIdealStateSpecies(ERule::MingXiao, true,
			EElysiumNpcState::Idle, EElysiumNpcState::Idle, true)),
		static_cast<int32>(EElysiumNpcState::Combat));
	TestEqual(TEXT("MingXiao without one is IDLE"),
		static_cast<int32>(FElysiumNpc::SelectIdealStateSpecies(ERule::MingXiao, true,
			EElysiumNpcState::Idle, EElysiumNpcState::Idle, false)),
		static_cast<int32>(EElysiumNpcState::Idle));
	TestEqual(TEXT("and a DEAD MingXiao still answers from the enemy: the 7 write is unreachable"),
		static_cast<int32>(FElysiumNpc::SelectIdealStateSpecies(ERule::MingXiao, false,
			EElysiumNpcState::Dead, EElysiumNpcState::Dead, true)),
		static_cast<int32>(EElysiumNpcState::Combat));

	// `CNPC_VMingXiaoTentacle::vfunc461` — here the dead arm is REAL and short-circuits.
	TestEqual(TEXT("a tentacle whose CURRENT state is dead stays dead"),
		static_cast<int32>(FElysiumNpc::SelectIdealStateSpecies(ERule::MingXiaoTentacle, true,
			EElysiumNpcState::Dead, EElysiumNpcState::Idle, true)),
		static_cast<int32>(EElysiumNpcState::Dead));
	TestEqual(TEXT("and one whose IDEAL state is dead stays dead too"),
		static_cast<int32>(FElysiumNpc::SelectIdealStateSpecies(ERule::MingXiaoTentacle, true,
			EElysiumNpcState::Idle, EElysiumNpcState::Dead, true)),
		static_cast<int32>(EElysiumNpcState::Dead));
	TestEqual(TEXT("a live tentacle with an enemy is COMBAT"),
		static_cast<int32>(FElysiumNpc::SelectIdealStateSpecies(ERule::MingXiaoTentacle, true,
			EElysiumNpcState::Alert, EElysiumNpcState::Alert, true)),
		static_cast<int32>(EElysiumNpcState::Combat));
	TestEqual(TEXT("and without one is IDLE"),
		static_cast<int32>(FElysiumNpc::SelectIdealStateSpecies(ERule::MingXiaoTentacle, true,
			EElysiumNpcState::Alert, EElysiumNpcState::Alert, false)),
		static_cast<int32>(EElysiumNpcState::Idle));

	// No registered classname reaches any of the three, so the live dispatch declines — which is the
	// arm that lets the two-layer rule run.
	FNpcKernelCondFixture F;
	if (!TestNotNull(TEXT("the subject spawned"), F.Npc)) { return false; }
	EElysiumNpcState Ideal = EElysiumNpcState::Idle;
	TestFalse(TEXT("npc_VHumanCombatant has no species ideal-state override"),
		F.Npc->SelectIdealStateForSpecies(Ideal));
	return true;
}

// =================================================================================================
// Slot 459 — `RemoveIgnoredConditions`
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCondRemoveIgnoredTest,
	"Elysium.Substrate.NpcKernelConditions.RemoveIgnoredConditions", GNpcKernelCondFlags)
bool FElysiumNpcKernelCondRemoveIgnoredTest::RunTest(const FString&)
{
	FNpcKernelCondFixture F;
	if (!TestNotNull(TEXT("the subject spawned"), F.Npc)) { return false; }

	// The cine body `0x101a89a0`, applied directly: fourteen conditions in retail's order plus the
	// `m_bCondTookDamage` byte between the two groups.
	const EElysiumNpcCond Cleared[] = {
		EElysiumNpcCond::LightDamage, EElysiumNpcCond::HeavyDamage, EElysiumNpcCond::RepeatedDamage,
		EElysiumNpcCond::InvestigateLevel, EElysiumNpcCond::CriminalFleeLevel,
		EElysiumNpcCond::SupernaturalFleeLevel, EElysiumNpcCond::HearFlinch,
		EElysiumNpcCond::CriminalAttackLevel, EElysiumNpcCond::SupernaturalAttackLevel,
		EElysiumNpcCond::InvestigateSound, EElysiumNpcCond::InvestigateSight,
		EElysiumNpcCond::Comfort, EElysiumNpcCond::BeingAttacked,
	};
	TestEqual(TEXT("the last entry is decimal 10 = BEING_ATTACKED"),
		CondNum(EElysiumNpcCond::BeingAttacked), 10);
	for (const EElysiumNpcCond Cond : Cleared)
	{
		F.Npc->Cognition.Conditions.Set(Cond);
	}
	F.Npc->Cognition.Conditions.Set(EElysiumNpcCond::SeeEnemy);   // not in the list
	F.Npc->Cognition.bCondTookDamage = true;

	FElysiumNpc::ClearCineIgnoredConditions(*F.Npc);

	for (const EElysiumNpcCond Cond : Cleared)
	{
		TestFalse(FString::Printf(TEXT("0x%02x cleared on the partner"), CondNum(Cond)),
			F.Npc->Cognition.Conditions.Has(Cond));
	}
	TestFalse(TEXT("m_bCondTookDamage (+0x5b80) cleared"), F.Npc->Cognition.bCondTookDamage);
	TestTrue(TEXT("SEE_ENEMY is not in the list"),
		F.Npc->Cognition.Conditions.Has(EElysiumNpcCond::SeeEnemy));

	// The NPC's own slot 459 (`0x1026d7f0`): it only acts in state 4 (SCRIPT) with a live `m_hCine`.
	F.Npc->Cognition.Conditions.Set(EElysiumNpcCond::LightDamage);
	F.Npc->RemoveIgnoredConditions();
	TestTrue(TEXT("outside state 4 slot 459 writes nothing"),
		F.Npc->Cognition.Conditions.Has(EElysiumNpcCond::LightDamage));

	// The partner seam. It answers null — this runtime's scripted scenes carry no cine target — so
	// even inside state 4 the dispatch has nobody to clear. The refusal IS the recovered arm.
	TestNull(TEXT("the cine-partner seam answers nothing"), F.Npc->CineIgnoredConditionsPartner());
	return true;
}

// =================================================================================================
// `RefreshCombatConditions` (`0x102b2570`)
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCondRefreshCombatTest,
	"Elysium.Substrate.NpcKernelConditions.RefreshCombatConditions", GNpcKernelCondFlags)
bool FElysiumNpcKernelCondRefreshCombatTest::RunTest(const FString&)
{
	FNpcKernelCondFixture F;
	if (!TestNotNull(TEXT("the subject spawned"), F.Npc)) { return false; }

	// 1. Both products are cleared before any gate, so a body that is NOT in melee ends the pass
	//    with neither standing however they got there.
	F.Npc->bInMelee = false;
	F.Npc->Cognition.Conditions.Set(EElysiumNpcCond::ShouldStepback);
	F.Npc->Cognition.Conditions.Set(EElysiumNpcCond::ShouldKick);
	F.Npc->RefreshCombatConditions();
	TestFalse(TEXT("SHOULD_STEPBACK is cleared unconditionally"),
		F.Npc->Cognition.Conditions.Has(EElysiumNpcCond::ShouldStepback));
	TestFalse(TEXT("SHOULD_KICK is cleared unconditionally"),
		F.Npc->Cognition.Conditions.Has(EElysiumNpcCond::ShouldKick));

	// 2. In melee with ENEMY_TOO_FAR (0x55): the ONE write is TOO_FAR_FOR_MELEE (0x09) and the body
	//    returns — no stepback draw is taken at all.
	F.Npc->bInMelee = true;
	F.Npc->Cognition.Conditions.Set(EElysiumNpcCond::EnemyTooFar);
	F.Npc->Cognition.Conditions.Set(EElysiumNpcCond::TooCloseToAttack);
	F.Npc->RefreshCombatConditions();
	TestTrue(TEXT("ENEMY_TOO_FAR raises TOO_FAR_FOR_MELEE"),
		F.Npc->Cognition.Conditions.Has(EElysiumNpcCond::TooFarForMelee));
	TestFalse(TEXT("and nothing else: the body returned"),
		F.Npc->Cognition.Conditions.Has(EElysiumNpcCond::ShouldStepback));
	F.Npc->Cognition.Conditions.Clear(EElysiumNpcCond::EnemyTooFar);
	F.Npc->Cognition.Conditions.Clear(EElysiumNpcCond::TooFarForMelee);

	// 3. The stepback window. `m_flLastMeleeStepbackTime < m_flLastAttackTime` opens it on its own,
	//    and with it CLOSED (stepped back after the last attack, and the 3.0 / speed-scale period
	//    not yet elapsed) neither arm is evaluated. Arm A is `TOO_CLOSE_TO_ATTACK && RandomInt(0,99)
	//    < 20`, so with the condition clear and the rare arm's terms blocked nothing is raised.
	const double Now = F.World.World.NowSeconds();
	F.Npc->LastAttackTime = Now - 100.0;
	F.Npc->LastMeleeStepbackTime = Now;   // after the last attack, and the period has not elapsed
	F.Npc->Cognition.Conditions.Set(EElysiumNpcCond::TooCloseToAttack);
	F.Npc->RefreshCombatConditions();
	TestFalse(TEXT("a closed window takes no draw and raises nothing"),
		F.Npc->Cognition.Conditions.Has(EElysiumNpcCond::ShouldStepback));

	// Open it the other way — the period elapsed (3.0 seconds at speed scale 1.0) — and drive both
	// arms by making the rare arm impossible so only arm A's 20-in-100 can fire. Over enough
	// passes the draw must produce BOTH answers; a body that always or never raised would fail.
	F.Npc->LastAttackTime = Now - 100.0;
	F.Npc->LastMeleeStepbackTime = Now - 10.0;
	F.Npc->Cognition.Conditions.Set(EElysiumNpcCond::CanMeleeAttack1);   // blocks the rare arm
	int32 Raised = 0;
	for (int32 i = 0; i < 200; ++i)
	{
		F.Npc->Cognition.Conditions.Set(EElysiumNpcCond::TooCloseToAttack);
		F.Npc->RefreshCombatConditions();
		Raised += F.Npc->Cognition.Conditions.Has(EElysiumNpcCond::ShouldStepback) ? 1 : 0;
	}
	TestTrue(TEXT("arm A's 20-in-100 draw raises SHOULD_STEPBACK sometimes"), Raised > 0);
	TestTrue(TEXT("and not always"), Raised < 200);

	// 4. The kick arm. Its FIRST term is the running program's MASK — `SHOULD_KICK` is not one of
	//    the conditions `BuildScheduleTestBits` adds, and with no schedule installed there is no
	//    mask at all, so the arm refuses before it reaches the weapon.
	F.Npc->Schedule.Clear();
	F.Npc->NextAttackTime = Now - 1.0;
	F.Npc->Cognition.Conditions.Set(EElysiumNpcCond::TooCloseToAttack);
	F.Npc->RefreshCombatConditions();
	TestFalse(TEXT("with no installed program the kick arm cannot fire"),
		F.Npc->Cognition.Conditions.Has(EElysiumNpcCond::ShouldKick));

	// The weapon-flag seam is the arm's last term and answers false, so SHOULD_KICK is never raised
	// in this runtime whatever the mask says. The seam is asked, and the refusal is the recovered
	// one: no `FElysiumWeapon` carries retail's `+0x5a0` flag word.
	TestFalse(TEXT("the weapon-flag seam answers nothing"), F.Npc->WeaponFlagBlocksAttack());
	return true;
}

// =================================================================================================
// `RefreshOccludedCondition` (`0x1028e700`)
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCondRefreshOccludedTest,
	"Elysium.Substrate.NpcKernelConditions.RefreshOccludedCondition", GNpcKernelCondFlags)
bool FElysiumNpcKernelCondRefreshOccludedTest::RunTest(const FString&)
{
	FNpcKernelCondFixture F;
	if (!TestNotNull(TEXT("the subject spawned"), F.Npc)) { return false; }

	F.Npc->OccludedDelay = 2.0f;   // +0x62c8
	double Stamp = 0.0;            // the sentinel, `_DAT_104454c4` = 0.0f

	// Not standing: the sentinel is reset and nothing else happens.
	Stamp = 123.0;
	F.Npc->RefreshOccludedCondition(EElysiumNpcCond::EnemyOccluded, Stamp, 10.0);
	TestEqual(TEXT("a dropped condition resets the sentinel to 0"), Stamp, 0.0);

	// First pass with it standing: arm `curtime + m_flOccludedDelay` and SUPPRESS.
	F.Npc->Cognition.Conditions.Set(EElysiumNpcCond::EnemyOccluded);
	F.Npc->RefreshOccludedCondition(EElysiumNpcCond::EnemyOccluded, Stamp, 10.0);
	TestEqual(TEXT("the sentinel is armed to curtime + OccludedDelay"), Stamp, 12.0);
	TestFalse(TEXT("and the condition is taken away until the delay elapses"),
		F.Npc->Cognition.Conditions.Has(EElysiumNpcCond::EnemyOccluded));

	// Still inside the window: still suppressed, and the stamp is NOT re-armed.
	F.Npc->Cognition.Conditions.Set(EElysiumNpcCond::EnemyOccluded);
	F.Npc->RefreshOccludedCondition(EElysiumNpcCond::EnemyOccluded, Stamp, 11.9);
	TestEqual(TEXT("the stamp is not re-armed mid-window"), Stamp, 12.0);
	TestFalse(TEXT("still suppressed at 11.9"),
		F.Npc->Cognition.Conditions.Has(EElysiumNpcCond::EnemyOccluded));

	// At the deadline the compare is `curtime < stamp`, so EXACTLY 12.0 is already through.
	F.Npc->Cognition.Conditions.Set(EElysiumNpcCond::EnemyOccluded);
	F.Npc->RefreshOccludedCondition(EElysiumNpcCond::EnemyOccluded, Stamp, 12.0);
	TestTrue(TEXT("at exactly the deadline the condition stands"),
		F.Npc->Cognition.Conditions.Has(EElysiumNpcCond::EnemyOccluded));
	return true;
}

// =================================================================================================
// The disturbed latch (`0x1037bb20`, `0x1037b6e0`)
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCondDisturbedTest,
	"Elysium.Substrate.NpcKernelConditions.Disturbed", GNpcKernelCondFlags)
bool FElysiumNpcKernelCondDisturbedTest::RunTest(const FString&)
{
	FNpcKernelCondFixture F;
	if (!TestNotNull(TEXT("the subject spawned"), F.Npc)) { return false; }
	FElysiumPlayer* Player = F.World.Player();
	if (!TestNotNull(TEXT("the player spawned"), Player)) { return false; }

	TestFalse(TEXT("m_bWasDisturbed starts clear"), F.Npc->IsDisturbed());

	// The player arm: `m_hClosestPlayer` takes the DISTURBER, `OnDisturbedByPlayer` fires, and an
	// `AddEntityRelationship(player, D_HT, 10)` record is written.
	F.Npc->bUnawareExited = true;
	F.Npc->OnDisturbed(Player);
	TestTrue(TEXT("the latch is set"), F.Npc->IsDisturbed());
	TestFalse(TEXT("m_bUnawareExited (+0x6667) is cleared"), F.Npc->bUnawareExited);
	TestEqual(TEXT("m_hClosestPlayer takes the disturber"),
		F.Npc->Senses.Memory.ClosestPlayer.Index, Player->Handle.Index);
	F.World.World.Tick(F.World.World.NowSeconds());
	TestEqual(TEXT("OnDisturbedByPlayer fired"), F.World.Counter(TEXT("disturbedbyplayer")), 1.f);
	TestEqual(TEXT("and OnDisturbed did not"), F.World.Counter(TEXT("disturbed")), 0.f);
	EElysiumRelationship Value = EElysiumRelationship::Neutral;
	int32 Priority = 0;
	TestTrue(TEXT("a relationship row was written against the player"),
		F.Npc->Relationships.ResolveRow(Player->Handle, FString(), Value, Priority));
	TestEqual(TEXT("D_HT"), static_cast<int32>(Value),
		static_cast<int32>(EElysiumRelationship::Hate));
	TestEqual(TEXT("priority 10"), Priority, 10);

	// The latch: a second disturbance writes nothing and fires nothing.
	F.Npc->bUnawareExited = true;
	F.Npc->OnDisturbed(Player);
	F.World.World.Tick(F.World.World.NowSeconds());
	TestTrue(TEXT("the second call leaves m_bUnawareExited alone"), F.Npc->bUnawareExited);
	TestEqual(TEXT("and fires no second output"),
		F.World.Counter(TEXT("disturbedbyplayer")), 1.f);

	// The non-player arm, on a fresh subject: the generic `SetClosestPlayer` sweep runs and
	// `OnDisturbed` fires instead.
	FNpcKernelCondFixture G;
	if (!TestNotNull(TEXT("the second subject spawned"), G.Npc)) { return false; }
	G.Npc->OnDisturbed(nullptr);
	G.World.World.Tick(G.World.World.NowSeconds());
	TestEqual(TEXT("a non-player disturber fires OnDisturbed"),
		G.World.Counter(TEXT("disturbed")), 1.f);
	TestEqual(TEXT("and not OnDisturbedByPlayer"),
		G.World.Counter(TEXT("disturbedbyplayer")), 0.f);
	return true;
}

// =================================================================================================
// The cop / hunter pursuit counters (`0x1017f650`, `0x1017f7b0`, `0x1017f830`)
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCondPursuitTest,
	"Elysium.Substrate.NpcKernelConditions.PursuitCounters", GNpcKernelCondFlags)
bool FElysiumNpcKernelCondPursuitTest::RunTest(const FString&)
{
	FNpcKernelCondFixture F;
	FElysiumPlayer* Player = F.World.Player();
	if (!TestNotNull(TEXT("the player spawned"), Player)) { return false; }

	// The counters are the PLAYER's `+0x1d10` / `+0x1d14`, not the NPC's.
	TestEqual(TEXT("cops start at zero"), Player->Police.CopsInPursuit, 0);
	FElysiumNpc::OnCopPursuitStart(*Player);
	TestEqual(TEXT("the first cop increments to one"), Player->Police.CopsInPursuit, 1);
	FElysiumNpc::OnCopPursuitStart(*Player);
	TestEqual(TEXT("a second cop increments again"), Player->Police.CopsInPursuit, 2);

	// Hunters: increment, increment, decrement, decrement — and the STOP body decrements FIRST,
	// which is why the second stop is the one that reaches zero.
	TestEqual(TEXT("hunters start at zero"), Player->Police.HuntersInPursuit, 0);
	FElysiumNpc::OnHunterPursuitStart(*Player);
	TestEqual(TEXT("one hunter"), Player->Police.HuntersInPursuit, 1);
	FElysiumNpc::OnHunterPursuitStart(*Player);
	TestEqual(TEXT("two hunters"), Player->Police.HuntersInPursuit, 2);
	FElysiumNpc::OnHunterPursuitStop(*Player);
	TestEqual(TEXT("one hunter left"), Player->Police.HuntersInPursuit, 1);
	FElysiumNpc::OnHunterPursuitStop(*Player);
	TestEqual(TEXT("and none"), Player->Police.HuntersInPursuit, 0);

	// The criminal-window sweep hook. This runtime's `EElysiumNpcState` has no member for retail
	// state 14, so the sweep's predicate is unsatisfiable and it visits nobody — the seam is asked
	// and its "nothing" is the recovered answer.
	FElysiumNpc::PromoteCriminalWindowNpcs(F.World.World);
	TestEqual(TEXT("the state-14 sweep changes no count"), Player->Police.CopsInPursuit, 2);
	return true;
}

// =================================================================================================
// `RequestDesiredState` — the two flee arms (`0x102ad260`, `0x102ad2d0`)
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCondRequestFleeTest,
	"Elysium.Substrate.NpcKernelConditions.RequestDesiredState", GNpcKernelCondFlags)
bool FElysiumNpcKernelCondRequestFleeTest::RunTest(const FString&)
{
	FNpcKernelCondFixture F;
	if (!TestNotNull(TEXT("the subject spawned"), F.Npc)) { return false; }

	// The gate is `HasInterruptCondition` — the condition standing AND the running program's mask
	// listing it. With no program installed there is no mask, so a standing condition is refused.
	F.Npc->Schedule.Clear();
	F.Npc->Cognition.Conditions.Set(EElysiumNpcCond::SupernaturalFleeLevel);   // 0x21
	TestEqual(TEXT("with no installed program the flee request is refused"),
		F.Npc->RequestFleeDesiredState(EElysiumNpcCond::SupernaturalFleeLevel, 0x4468), 0);
	TestEqual(TEXT("and nothing was written"), F.Npc->GetMind().DesiredRetailState(), 0);
	TestFalse(TEXT("INITIAL_FLEE is not armed"),
		F.Npc->NpcFlags.Has(EElysiumNpcFlag::INITIAL_FLEE));

	// Install a program. `BuildScheduleTestBits` (`0x102ad140`) adds the three flee/investigate law
	// conditions to every non-busy, non-investigating NPC's mask, so the gate now stands.
	TestTrue(TEXT("a program installs"),
		ElysiumSchedule::Start(F.Npc->Schedule, EElysiumScheduleId::IdleDisposition, *F.Npc));
	TestTrue(TEXT("and its effective mask lists SUPERNATURAL_FLEE_LEVEL"),
		ElysiumSchedule::MaskHasCondition(F.Npc->Schedule, *F.Npc,
			EElysiumNpcCond::SupernaturalFleeLevel));

	F.Npc->Cognition.Conditions.Set(EElysiumNpcCond::SupernaturalFleeLevel);
	TestEqual(TEXT("0x102ad260 answers retail state 8"),
		F.Npc->RequestFleeDesiredState(EElysiumNpcCond::SupernaturalFleeLevel, 0x4468), 8);
	TestTrue(TEXT("INITIAL_FLEE (+0x14b8 bit 0x100) is armed"),
		F.Npc->NpcFlags.Has(EElysiumNpcFlag::INITIAL_FLEE));
	TestEqual(TEXT("m_IdealNPCState takes the raw retail 8"), F.Npc->GetMind().DesiredRetailState(), 8);
	TestEqual(TEXT("and the typed ideal state is LEFT ALONE: no port state is retail 8"),
		static_cast<int32>(F.Npc->GetMind().IdealState()),
		static_cast<int32>(EElysiumNpcState::Idle));

	// The second body differs only in the condition and the source line.
	F.Npc->NpcFlags.Clear(EElysiumNpcFlag::INITIAL_FLEE);
	F.Npc->Cognition.Conditions.Clear(EElysiumNpcCond::SupernaturalFleeLevel);
	TestEqual(TEXT("0x102ad2d0 refuses without CRIMINAL_FLEE_LEVEL"),
		F.Npc->RequestFleeDesiredState(EElysiumNpcCond::CriminalFleeLevel, 0x447d), 0);
	F.Npc->Cognition.Conditions.Set(EElysiumNpcCond::CriminalFleeLevel);       // 0x1f
	TestEqual(TEXT("and answers 8 with it"),
		F.Npc->RequestFleeDesiredState(EElysiumNpcCond::CriminalFleeLevel, 0x447d), 8);
	return true;
}

// =================================================================================================
// The alternate-AI door transaction (`0x10298800`, `0x10290040`)
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCondAlternateAiTest,
	"Elysium.Substrate.NpcKernelConditions.AlternateAiDoor", GNpcKernelCondFlags)
bool FElysiumNpcKernelCondAlternateAiTest::RunTest(const FString&)
{
	FNpcKernelCondFixture F;
	if (!TestNotNull(TEXT("the subject spawned"), F.Npc)) { return false; }

	// `FUN_10298800` — three stores.
	F.Npc->ScheduleHost.bShouldMove = true;
	F.Npc->AlternateAi = 0;
	F.Npc->EnterAlternateAi();
	TestFalse(TEXT("m_bShouldMove (+0x1a40) is cleared"), F.Npc->ScheduleHost.bShouldMove);
	TestEqual(TEXT("m_eAlternateAI (+0x644c) is armed to mode 1"), F.Npc->AlternateAi, 1);

	// `FUN_10290040` with a dead `m_hOpeningDoor`: the ONE arm that resets the mode and lets go.
	F.Npc->OpeningDoor = FElysiumEntityHandle::Invalid();
	TestFalse(TEXT("a stale door ends the transaction"), F.Npc->RunAlternateAiOpeningDoor(1.0));
	TestEqual(TEXT("and resets the mode to 0"), F.Npc->AlternateAi, 0);

	// With a LIVE door the transaction stays installed, and the door's own facing-point seam is what
	// refuses. Retail's refusal arm (`iStack_10 == -1`) returns false WITHOUT clearing the mode.
	F.Npc->EnterAlternateAi();
	F.Npc->OpeningDoor = F.Npc->Handle;   // any live entity: the body only tests that it resolves
	F.Npc->AlternateAiExpireTime = 0.0;
	TestFalse(TEXT("the door-point seam refuses"), F.Npc->RunAlternateAiOpeningDoor(1.0));
	TestEqual(TEXT("and the transaction is still installed"), F.Npc->AlternateAi, 1);
	TestEqual(TEXT("with no mode-2 expiry stamped"), F.Npc->AlternateAiExpireTime, 0.0);

	// The door NPC-open-point seam is the one that answers nothing, and it is the FIRST of the two
	// terms the advance arms need.
	FVector Point = FVector::ZeroVector;
	TestFalse(TEXT("the door NPC-open-point seam answers false"),
		F.Npc->OpeningDoorFacingPoint(*F.Npc, false, Point));

	// The SECOND term, `FacingIdeal` (`0x10278c80`), is family Facing's real body and is NOT a
	// refusal: `|CAI_Motor::DeltaIdealYaw()| <= 0.006` with the equal bit carried, over a motor seam
	// that stands at retail's "already facing the ideal" answer of 0. So an untouched body IS facing
	// ideal, and the gate this transaction sits behind is OPEN.
	TestEqual(TEXT("the motor's yaw-delta seam stands at retail's aligned 0"),
		F.Npc->MotorIdealYawDelta, 0.f);
	TestTrue(TEXT("so FacingIdeal answers TRUE on a body that has not turned"), F.Npc->FacingIdeal());
	F.Npc->MotorIdealYawDelta = 0.006f;
	TestTrue(TEXT("and at exactly the tolerance too: the compare carries the equal bit"),
		F.Npc->FacingIdeal());
	F.Npc->MotorIdealYawDelta = 0.007f;
	TestFalse(TEXT("past 0.006 degrees it answers false"), F.Npc->FacingIdeal());
	F.Npc->MotorIdealYawDelta = 0.f;

	// RECOVERED FACT, asserted rather than worked around: the advance is unreachable anyway. Even
	// facing ideal, and even on the `m_bOpeningDoorWait` arm — which in retail needs nothing more
	// than a point and a facing to stamp mode 2 with a 1.0 s expiry — the body returns at the door
	// query, because the door query comes first. **This arm cannot be reached until a door carries
	// an NPC-open point.**
	F.Npc->bOpeningDoorWait = true;
	F.Npc->AlternateAiExpireTime = 0.0;
	TestFalse(TEXT("the wait arm still refuses: the door query precedes the facing gate"),
		F.Npc->RunAlternateAiOpeningDoor(1.0));
	TestEqual(TEXT("mode 2 is never armed"), F.Npc->AlternateAi, 1);
	TestEqual(TEXT("and no expiry is stamped"), F.Npc->AlternateAiExpireTime, 0.0);
	return true;
}

// =================================================================================================
// `CNPC_VWerewolf` — the gather suppression and the death-triggered latch
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCondWerewolfTest,
	"Elysium.Substrate.NpcKernelConditions.Werewolf", GNpcKernelCondFlags)
bool FElysiumNpcKernelCondWerewolfTest::RunTest(const FString&)
{
	FNpcKernelCondFixture F;
	if (!TestNotNull(TEXT("the subject spawned"), F.Npc)) { return false; }

	// `UpdateConditionDeathTriggered` (`0x103cc890`). The gate is `m_Activity == 0x11d` AND
	// `m_DoorState == 1`; the activity seam answers -1, so the gate cannot pass here and the whole
	// body is a single `ClearCondition(0x7a)`.
	const EElysiumNpcCond DeathTriggered = static_cast<EElysiumNpcCond>(0x7a);
	TestEqual(TEXT("the activity seam answers -1 (this runtime names activities)"),
		F.Npc->CurrentRetailActivityId(), -1);
	F.Npc->WerewolfDoorState = 1;
	F.Npc->Cognition.Conditions.Set(DeathTriggered);
	F.Npc->UpdateConditionDeathTriggered();
	TestFalse(TEXT("0x7a is cleared unconditionally at the top of the body"),
		F.Npc->Cognition.Conditions.Has(DeathTriggered));
	F.World.World.Tick(F.World.World.NowSeconds());
	TestEqual(TEXT("and with the gate closed the output does not fire"),
		F.World.Counter(TEXT("deathtriggered")), 0.f);

	// The door-state term on its own refuses too, which is the second half of the gate.
	F.Npc->WerewolfDoorState = 0;
	F.Npc->UpdateConditionDeathTriggered();
	TestFalse(TEXT("a closed door leaves 0x7a clear"),
		F.Npc->Cognition.Conditions.Has(DeathTriggered));

	// The gather suppression (`0x103d02b0`): the zone word and the 40-unit height delta. No
	// registered classname is a werewolf, so the census row is what is exercised by name.
	TestNotNull(TEXT("CNPC_VWerewolf is a census class"),
		ElysiumNpcKernelClass::Find(TEXT("CNPC_VWerewolf")));
	TestFalse(TEXT("and npc_VHumanCombatant is not one, so the arm is skipped for it"),
		F.Npc->IsRetailClass(TEXT("CNPC_VWerewolf")));

	// The arm is a SUPPRESSION: with the zone bits set and the enemy 40 units above, the melee pair
	// is taken away. Driven through the condition set directly, because the gather's own entry gates
	// (a live enemy, a world) are `ElysiumNpcCond::GatherAttackConditions`' and not this arm's.
	F.Npc->WerewolfHintFlags = 0x4u;
	TestTrue(TEXT("the zone word carries bit 0x4"), (F.Npc->WerewolfHintFlags & 0x4u) != 0);
	TestFalse(TEXT("bit 0x100 is the other admitted one"),
		(F.Npc->WerewolfHintFlags & 0x100u) != 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
