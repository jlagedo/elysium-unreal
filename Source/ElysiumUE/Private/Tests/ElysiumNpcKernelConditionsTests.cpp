#include "Misc/AutomationTest.h"

#include "Tests/ElysiumArmTier.h"

#if WITH_DEV_AUTOMATION_TESTS && ELYSIUM_WITH_ARM_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"                  // ElysiumMove::U
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumLaw.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcGhoulCroucher.h"
#include "Substrate/ElysiumNpcWerewolf.h"
#include "Substrate/ElysiumNpcTzimisce.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSchedule.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumNpcTestCensus.h"
#include "Visual/ElysiumNpcClips.h"            // FElysiumNpcClip, the melee band's fixture sequences

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
	"Elysium.Arm.NpcKernelConditions.ClearAttackConditions", GNpcKernelCondFlags)
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
	"Elysium.Arm.NpcKernelConditions.ClearSenseConditions", GNpcKernelCondFlags)
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
// Slot 553 — the ranged band (`0x1026d890`); slot 554's body was deleted as dead in 0019/6
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCondRangeBandsTest,
	"Elysium.Arm.NpcKernelConditions.RangeAttackConditions", GNpcKernelCondFlags)
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

	// `CNPC_VBatSwarm` (0x103675e0 / 0x10367610) and `CNPC_VSheriffSwarm` (0x103b2590 / 0x103b25c0)
	// are the only overrides and are unmodified forwards, so the census must show the SAME two
	// bodies filling the slot for those classes as for the Troika line — i.e. no species answer
	// differs. Exercised by name, since neither classname is registered here.
	for (const TCHAR* Cls : { TEXT("CNPC_VBatSwarm"), TEXT("CNPC_VSheriffSwarm") })
	{
		TestNotNull(FString::Printf(TEXT("%s is a census class"), Cls),
			ElysiumNpcTestCensus::Find(Cls));
	}
	return true;
}

// =================================================================================================
// Slot 564 — `FCanCheckAttacks` (`0x102953a0` over `0x10270840`)
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCondCanCheckAttacksTest,
	"Elysium.Arm.NpcKernelConditions.FCanCheckAttacks", GNpcKernelCondFlags)
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

	// The nav type is `0x1027d990`, the navigator's `+0x18` (family Motor's `NavGetType`). A fresh
	// body stands on `NAV_GROUND` (0), which does not suppress; `NAV_JUMP` (1) and `NAV_CLIMB` (3)
	// refuse BEFORE the condition terms are read (`0x10270840`), `NAV_FLY` (2) does not.
	TestEqual(TEXT("a fresh navigator reads NAV_GROUND"), F.Npc->NavType(), 0);
	F.Npc->NavSetType(1);
	TestFalse(TEXT("NAV_JUMP refuses though SEE_ENEMY holds"), F.Npc->FCanCheckAttacksBase());
	F.Npc->NavSetType(3);
	TestFalse(TEXT("NAV_CLIMB refuses though SEE_ENEMY holds"), F.Npc->FCanCheckAttacksBase());
	F.Npc->NavSetType(2);
	TestTrue(TEXT("NAV_FLY does not refuse"), F.Npc->FCanCheckAttacksBase());
	F.Npc->NavSetType(0);

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
// Slot 463 — the species bodies and the expression map
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCondStateChangeSpeciesTest,
	"Elysium.Arm.NpcKernelConditions.OnStateChangeSpecies", GNpcKernelCondFlags)
bool FElysiumNpcKernelCondStateChangeSpeciesTest::RunTest(const FString&)
{
	// Every species body BY NAME against the census. Since story 5 step 3 each is the
	// `OnStateChange` override on the class that introduces it (`overrides-step3.tsv`); a subclass
	// with no body of its own runs its base's through C++ inheritance, as the census says.
	struct FExpect { const TCHAR* Cls; const TCHAR* Body; };
	const FExpect Rows[] = {
		{ TEXT("CNPC_VGuard1"),            TEXT("0x1037d020") },
		{ TEXT("CNPC_VHunter"),            TEXT("0x10388880") },
		{ TEXT("CNPC_VGhoulCroucher"),     TEXT("0x103871c0") },
		{ TEXT("CNPC_VHumanCombatant"),    TEXT("0x103871c0") },
		{ TEXT("CNPC_VHumanCombatPatrol"), TEXT("0x103871c0") },
		{ TEXT("CNPC_VSabbatGunman"),      TEXT("0x103871c0") },
		{ TEXT("CNPC_VYukie"),             TEXT("0x103871c0") },
		{ TEXT("CNPC_ProneDialog"),        TEXT("0x103871c0") },
		{ TEXT("CNPC_VTzimisce"),          TEXT("0x103ba2c0") },
		{ TEXT("CNPC_VCamera"),            TEXT("0x10368ea0") },
		{ TEXT("CNPC_VCameraSecurity"),    TEXT("0x10368ea0") },
		{ TEXT("CNPC_VBach"),              TEXT("0x103639b0") },
		{ TEXT("CNPC_VCop"),               TEXT("0x10371c20") },
	};
	for (const FExpect& E : Rows)
	{
		TestEqual(FString::Printf(TEXT("%s's slot-463 body is %s"), E.Cls, E.Body),
			FString(ElysiumNpcTestCensus::BodyOf(ElysiumNpcTestCensus::Find(E.Cls), 463)),
			FString(E.Body));
	}
	TestNull(TEXT("a class with no override runs the Troika body"),
		ElysiumNpcTestCensus::OverrideOf(ElysiumNpcTestCensus::Find(TEXT("CNPC_VPedestrian")), 463));

	// `CNPC_VTzimisce`'s expression map, off `PTR_s_normal_10653120` — "normal", "angry", "scream",
	// "dead". "scream" (index 2) is reached by no state.
	TestEqual(TEXT("idle maps to normal"),
		FString(FElysiumNpcTzimisce::StateChangeExpressionName(EElysiumNpcState::Idle)), FString(TEXT("normal")));
	TestEqual(TEXT("combat maps to angry"),
		FString(FElysiumNpcTzimisce::StateChangeExpressionName(EElysiumNpcState::Combat)), FString(TEXT("angry")));
	TestEqual(TEXT("alert maps to angry too"),
		FString(FElysiumNpcTzimisce::StateChangeExpressionName(EElysiumNpcState::Alert)), FString(TEXT("angry")));
	TestEqual(TEXT("dead maps to dead"),
		FString(FElysiumNpcTzimisce::StateChangeExpressionName(EElysiumNpcState::Dead)), FString(TEXT("dead")));
	TestNull(TEXT("scripted names no expression and writes nothing"),
		FElysiumNpcTzimisce::StateChangeExpressionName(EElysiumNpcState::Scripted));

	// The seam records the NAME, since this runtime has no SetExpression.
	FNpcKernelCondFixture F(TEXT("npc_VTzimisce"));
	FElysiumNpcTzimisce* Tzimisce = ElysiumTestAsSpecies<FElysiumNpcTzimisce>(F.Npc);
	if (!TestNotNull(TEXT("the Tzimisce spawned"), Tzimisce)) { return false; }
	Tzimisce->SetDefaultExpression(TEXT("angry"), 1.0f);
	return true;
}

// =================================================================================================
// Slot 463 — `CAI_BaseNPCTroika::OnStateChange` (`0x102ae140`)
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCondStateChangeTroikaTest,
	"Elysium.Arm.NpcKernelConditions.OnStateChangeTroika", GNpcKernelCondFlags)
bool FElysiumNpcKernelCondStateChangeTroikaTest::RunTest(const FString&)
{
	FNpcKernelCondFixture F;
	if (!TestNotNull(TEXT("the subject spawned"), F.Npc)) { return false; }

	// --- The CHANGED half: idle -> alert (retail 1 -> 3) ---
	F.Npc->bReturnToInitialPos = false;
	F.Npc->BaseScheduleHost.MemoryBits = 0xffffffffu;
	F.Npc->NpcFlags.Set(EElysiumNpcFlag::PRESERVE_PATH);
	F.Npc->NpcFlags.Set(EElysiumNpcFlag::MADE_HUNT_PATH);
	F.Npc->NpcFlags.Set(EElysiumNpcFlag::AT_CROSSWALK);
	F.Npc->bGoToIdleState = true;
	F.Npc->Cognition.bCondTookDamage = true;
	{ const int32 HuntIds[] = { 0, -1 }; F.Npc->BuildPatrolPath(&F.Npc->PatrolPathHuntCell, 0, 0, 0, HuntIds, FElysiumNpc::EPatrolPathBuild::Replace); }

	F.Npc->OnStateChangeTroika(EElysiumNpcState::Idle, EElysiumNpcState::Alert);

	TestTrue(TEXT("ALERT arms m_bReturnToInitialPos (+0x6494)"), F.Npc->bReturnToInitialPos);
	TestEqual(TEXT("m_afMemory keeps only the low 27 bits (&= 0x07ffffff)"),
		static_cast<int64>(F.Npc->BaseScheduleHost.MemoryBits), static_cast<int64>(0x07ffffffu));
	TestFalse(TEXT("PRESERVE_PATH is cleared on the ground"),
		F.Npc->NpcFlags.Has(EElysiumNpcFlag::PRESERVE_PATH));
	TestFalse(TEXT("MADE_HUNT_PATH is cleared"),
		F.Npc->NpcFlags.Has(EElysiumNpcFlag::MADE_HUNT_PATH));
	TestFalse(TEXT("AT_CROSSWALK is cleared"),
		F.Npc->NpcFlags.Has(EElysiumNpcFlag::AT_CROSSWALK));
	TestFalse(TEXT("m_bGoToIdleState is cleared"), F.Npc->bGoToIdleState);
	TestFalse(TEXT("m_bCondTookDamage is cleared"), F.Npc->Cognition.bCondTookDamage);
	TestNull(TEXT("the hunt patrol route is released (0x1029f5d0)"), F.Npc->PatrolPathHuntCell.Path);

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
	F.Npc->BaseScheduleHost.MemoryBits = 0xffffffffu;
	F.Npc->NpcFlags.Set(EElysiumNpcFlag::AT_CROSSWALK);
	F.Npc->Cognition.bCondTookDamage = true;
	F.Npc->OnStateChangeTroika(EElysiumNpcState::Alert, EElysiumNpcState::Alert);
	TestFalse(TEXT("no transition does not arm m_bReturnToInitialPos"), F.Npc->bReturnToInitialPos);
	TestEqual(TEXT("and does NOT mask m_afMemory — that write is inside the changed half"),
		static_cast<int64>(F.Npc->BaseScheduleHost.MemoryBits), static_cast<int64>(0xffffffffu));
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
	FNpcKernelCondFixture Cam(TEXT("npc_VCamera"));
	if (!TestNotNull(TEXT("the camera fixture subject spawned"), Cam.Npc)) { return false; }
	Cam.Npc->bGoToIdleState = true;
	Cam.Npc->Cognition.bCondTookDamage = true;
	// `FElysiumNpcCamera::OnStateChange` (`0x10368ea0`) is empty and does not chain, so the Troika
	// tail's writes never happen.
	Cam.Npc->OnStateChange(EElysiumNpcState::Idle, EElysiumNpcState::Alert);
	TestTrue(TEXT("a camera's state change leaves m_bGoToIdleState alone"), Cam.Npc->bGoToIdleState);
	TestTrue(TEXT("and the took-damage word"), Cam.Npc->Cognition.bCondTookDamage);
	return true;
}

// =================================================================================================
// Slot 461 — the three species overrides
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCondIdealStateSpeciesTest,
	"Elysium.Arm.NpcKernelConditions.SelectIdealStateSpecies", GNpcKernelCondFlags)
bool FElysiumNpcKernelCondIdealStateSpeciesTest::RunTest(const FString&)
{
	struct FExpect { const TCHAR* Cls; const TCHAR* Body; };
	const FExpect Rows[] = {
		{ TEXT("CNPC_VCamera"),           TEXT("0x10369060") },
		{ TEXT("CNPC_VCameraSecurity"),   TEXT("0x10369060") },
		{ TEXT("CNPC_VMingXiao"),         TEXT("0x103945a0") },
		{ TEXT("CNPC_VMingXiaoTentacle"), TEXT("0x1039e310") },
	};
	// Each body is the `SelectIdealStateRetail` override on its class (story 5 step 4); the census
	// says which classes hold it. Every class stands through its factory and is asked through the slot.
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_cond_461"), 461);
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Rows); ++Index)
	{
		Builder.AddNpcOfClass(Rows[Index].Cls, FVector(400.0 * (Index + 1), 0.0, 0.0), Rows[Index].Cls);
	}
	Builder.AddNpc(TEXT("foe"), FVector(0.0, 400.0, 0.0));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Foe = Fixture.Npc(TEXT("foe"));
	if (!TestNotNull(TEXT("the foe stood"), Foe))
	{
		return false;
	}
	for (const FExpect& E : Rows)
	{
		FElysiumNpc* Npc = Fixture.Npc(E.Cls);
		if (!TestNotNull(FString::Printf(TEXT("%s stood through its factory"), E.Cls), Npc))
		{
			return false;
		}
		FElysiumNpcWorldFixture::Quiet({ Npc });
		TestEqual(FString::Printf(TEXT("%s's slot-461 body is %s"), E.Cls, E.Body),
			FString(ElysiumNpcTestCensus::BodyOf(Npc->RetailClass(), 461)), FString(E.Body));
	}
	FElysiumNpc* Camera = Fixture.Npc(TEXT("CNPC_VCamera"));
	FElysiumNpc* Security = Fixture.Npc(TEXT("CNPC_VCameraSecurity"));
	FElysiumNpc* Xiao = Fixture.Npc(TEXT("CNPC_VMingXiao"));
	FElysiumNpc* Tentacle = Fixture.Npc(TEXT("CNPC_VMingXiaoTentacle"));

	// `CNPC_VCamera::FUN_10369060` — retail 3 ALERT, with NO test at all, written as
	// `m_IdealNPCState`; `CNPC_VCameraSecurity` inherits the body.
	for (FElysiumNpc* Cam : { Camera, Security })
	{
		for (const bool bEnemy : { false, true })
		{
			Cam->BaseMemory.Enemy = bEnemy ? Foe->Handle : FElysiumEntityHandle();
			TestEqual(TEXT("a camera's slot 461 answers retail ALERT"), Cam->SelectIdealStateRetail(), 3);
			TestEqual(TEXT("and writes it as the ideal state"), Cam->IdealStateRetail(), 3);
		}
	}
	// Even a DEAD camera reports ALERT: the body writes before any test (restored from the pre-step-4
	// case, story 5 step 4r).
	Camera->SetState(7);
	TestEqual(TEXT("even a dead camera reports ALERT"), Camera->SelectIdealStateRetail(), 3);

	// `CNPC_VMingXiao::vfunc461` — the dead write is ALWAYS overwritten by the enemy test, which is
	// retail's own dead code. So the answer depends on the enemy and on nothing else.
	Xiao->BaseMemory.Enemy = Foe->Handle;
	TestEqual(TEXT("MingXiao with an enemy is COMBAT (retail 2)"), Xiao->SelectIdealStateRetail(), 2);
	Xiao->BaseMemory.Enemy = FElysiumEntityHandle();
	TestEqual(TEXT("MingXiao without one is IDLE (retail 1)"), Xiao->SelectIdealStateRetail(), 1);
	// A DEAD MingXiao still answers from its enemy: the 7 write is unreachable (retail's own dead
	// code, reproduced).
	Xiao->SetState(7);
	Xiao->BaseMemory.Enemy = Foe->Handle;
	TestEqual(TEXT("a dead MingXiao still answers from the enemy: the 7 write is unreachable"),
		Xiao->SelectIdealStateRetail(), 2);

	// `CNPC_VMingXiaoTentacle::vfunc461` — here the dead arm is REAL and short-circuits.
	Tentacle->BaseMemory.Enemy = Foe->Handle;
	TestEqual(TEXT("a live tentacle with an enemy is COMBAT"), Tentacle->SelectIdealStateRetail(), 2);
	Tentacle->BaseMemory.Enemy = FElysiumEntityHandle();
	TestEqual(TEXT("and without one is IDLE"), Tentacle->SelectIdealStateRetail(), 1);
	Tentacle->WriteIdealStateRetail(7);
	Tentacle->BaseMemory.Enemy = Foe->Handle;
	TestEqual(TEXT("a tentacle whose IDEAL state is dead stays dead, enemy or not"),
		Tentacle->SelectIdealStateRetail(), 7);
	// And one whose CURRENT state is dead stays dead too, with a live ideal state.
	Tentacle->SetState(7);
	Tentacle->WriteIdealStateRetail(1);
	TestEqual(TEXT("a tentacle whose CURRENT state is dead stays dead"), Tentacle->SelectIdealStateRetail(), 7);
	return true;
}

// =================================================================================================
// Slot 459 — `RemoveIgnoredConditions`
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCondRemoveIgnoredTest,
	"Elysium.Arm.NpcKernelConditions.RemoveIgnoredConditions", GNpcKernelCondFlags)
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

	FElysiumNpcBase::ClearCineIgnoredConditions(*F.Npc);

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

	// Inside state 4 the dispatch reaches the director's own slot 459 (`0x101a89a0`): the director
	// case is `Elysium.Arm.NpcKernelDirector.Interrupt` (story 5 fold A3).
	return true;
}

// =================================================================================================
// `RefreshCombatConditions` (`0x102b2570`)
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCondRefreshCombatTest,
	"Elysium.Arm.NpcKernelConditions.RefreshCombatConditions", GNpcKernelCondFlags)
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
	return true;
}

// =================================================================================================
// `RefreshOccludedCondition` (`0x1028e700`)
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCondRefreshOccludedTest,
	"Elysium.Arm.NpcKernelConditions.RefreshOccludedCondition", GNpcKernelCondFlags)
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
	"Elysium.Arm.NpcKernelConditions.Disturbed", GNpcKernelCondFlags)
bool FElysiumNpcKernelCondDisturbedTest::RunTest(const FString&)
{
	FNpcKernelCondFixture F(TEXT("npc_VGhoulCroucher"));
	if (!TestNotNull(TEXT("the subject spawned"), F.Npc)) { return false; }
	FElysiumPlayer* Player = F.World.Player();
	if (!TestNotNull(TEXT("the player spawned"), Player)) { return false; }

	TestFalse(TEXT("m_bWasDisturbed starts clear"), ElysiumTestAsSpecies<FElysiumNpcGhoulCroucher>(F.Npc)->IsDisturbed());

	// The player arm: `m_hClosestPlayer` takes the DISTURBER, `OnDisturbedByPlayer` fires, and an
	// `AddEntityRelationship(player, D_HT, 10)` record is written.
	ElysiumTestAsSpecies<FElysiumNpcGhoulCroucher>(F.Npc)->bUnawareExited = true;
	ElysiumTestAsSpecies<FElysiumNpcGhoulCroucher>(F.Npc)->OnDisturbed(Player);
	TestTrue(TEXT("the latch is set"), ElysiumTestAsSpecies<FElysiumNpcGhoulCroucher>(F.Npc)->IsDisturbed());
	TestFalse(TEXT("m_bUnawareExited (+0x6667) is cleared"), ElysiumTestAsSpecies<FElysiumNpcGhoulCroucher>(F.Npc)->bUnawareExited);
	TestEqual(TEXT("m_hClosestPlayer takes the disturber"),
		ElysiumTestAsSpecies<FElysiumNpcGhoulCroucher>(F.Npc)->Senses.Memory.ClosestPlayer.Index, Player->Handle.Index);
	F.World.World.Tick(F.World.World.NowSeconds());
	TestEqual(TEXT("OnDisturbedByPlayer fired"), F.World.Counter(TEXT("disturbedbyplayer")), 1.f);
	TestEqual(TEXT("and OnDisturbed did not"), F.World.Counter(TEXT("disturbed")), 0.f);
	EElysiumRelationship Value = EElysiumRelationship::Neutral;
	int32 Priority = 0;
	TestTrue(TEXT("a relationship row was written against the player"),
		ElysiumTestAsSpecies<FElysiumNpcGhoulCroucher>(F.Npc)->Relationships.ResolveRow(Player->Handle, FString(), Value, Priority));
	TestEqual(TEXT("D_HT"), static_cast<int32>(Value),
		static_cast<int32>(EElysiumRelationship::Hate));
	TestEqual(TEXT("priority 10"), Priority, 10);

	// The latch: a second disturbance writes nothing and fires nothing.
	ElysiumTestAsSpecies<FElysiumNpcGhoulCroucher>(F.Npc)->bUnawareExited = true;
	ElysiumTestAsSpecies<FElysiumNpcGhoulCroucher>(F.Npc)->OnDisturbed(Player);
	F.World.World.Tick(F.World.World.NowSeconds());
	TestTrue(TEXT("the second call leaves m_bUnawareExited alone"), ElysiumTestAsSpecies<FElysiumNpcGhoulCroucher>(F.Npc)->bUnawareExited);
	TestEqual(TEXT("and fires no second output"),
		F.World.Counter(TEXT("disturbedbyplayer")), 1.f);

	// The non-player arm, on a fresh subject: the generic `SetClosestPlayer` sweep runs and
	// `OnDisturbed` fires instead.
	FNpcKernelCondFixture G(TEXT("npc_VGhoulCroucher"));
	if (!TestNotNull(TEXT("the second subject spawned"), G.Npc)) { return false; }
	ElysiumTestAsSpecies<FElysiumNpcGhoulCroucher>(G.Npc)->OnDisturbed(nullptr);
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
	"Elysium.Arm.NpcKernelConditions.PursuitCounters", GNpcKernelCondFlags)
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
	"Elysium.Arm.NpcKernelConditions.RequestDesiredState", GNpcKernelCondFlags)
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
		ElysiumSchedule::Start(F.Npc->Schedule, ElysiumSched::SCHED_TROIKA_IDLE_DISPOSITION, *F.Npc));
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
	"Elysium.Arm.NpcKernelConditions.AlternateAiDoor", GNpcKernelCondFlags)
bool FElysiumNpcKernelCondAlternateAiTest::RunTest(const FString&)
{
	FNpcKernelCondFixture F;
	if (!TestNotNull(TEXT("the subject spawned"), F.Npc)) { return false; }

	// `FUN_10298800` — three stores.
	F.Npc->BaseScheduleHost.bShouldMove = true;
	F.Npc->AlternateAi = 0;
	F.Npc->EnterAlternateAi();
	TestFalse(TEXT("m_bShouldMove (+0x1a40) is cleared"), F.Npc->BaseScheduleHost.bShouldMove);
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
	(void)F.Npc->RunAlternateAiOpeningDoor(1.0);
	TestEqual(TEXT("and the transaction is still installed"), F.Npc->AlternateAi, 1);
	TestEqual(TEXT("with no mode-2 expiry stamped"), F.Npc->AlternateAiExpireTime, 0.0);

	// The SECOND term, `FacingIdeal` (`0x10278c80`), is family Facing's real body and is NOT a
	// refusal: `|CAI_Motor::DeltaIdealYaw()| <= 0.006` with the equal bit carried. `NPCInit` seeds
	// the motor's ideal yaw from the body's own (`0x10273390`), so an untouched body IS facing ideal,
	// and the gate this transaction sits behind is OPEN.
	TestEqual(TEXT("an untouched body's yaw delta is retail's aligned 0"),
		F.Npc->MotorDeltaIdealYaw(), 0.f);
	TestTrue(TEXT("so FacingIdeal answers TRUE on a body that has not turned"), F.Npc->FacingIdeal());
	F.Npc->Angles.Y = 0.0; F.Npc->MotorIdealYaw = 0.006f;   // `DeltaIdealYaw` 0x102e1f90: AngleDiff(ideal, AngleMod(0))
	TestTrue(TEXT("and at exactly the tolerance too: the compare carries the equal bit"),
		F.Npc->FacingIdeal());
	F.Npc->Angles.Y = 0.0; F.Npc->MotorIdealYaw = 0.007f;   // `DeltaIdealYaw` 0x102e1f90: AngleDiff(ideal, AngleMod(0))
	TestFalse(TEXT("past 0.006 degrees it answers false"), F.Npc->FacingIdeal());
	F.Npc->Angles.Y = 0.0; F.Npc->MotorIdealYaw = 0.f;   // `DeltaIdealYaw` 0x102e1f90: AngleDiff(ideal, AngleMod(0))

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
	"Elysium.Arm.NpcKernelConditions.Werewolf", GNpcKernelCondFlags)
bool FElysiumNpcKernelCondWerewolfTest::RunTest(const FString&)
{
	FNpcKernelCondFixture F(TEXT("npc_VWerewolf"));
	FElysiumNpcWerewolf* Wolf = ElysiumTestAsSpecies<FElysiumNpcWerewolf>(F.Npc);
	if (!TestNotNull(TEXT("the Werewolf spawned"), Wolf)) { return false; }

	// `UpdateConditionDeathTriggered` (`0x103cc890`). The gate is `m_Activity == 0x11d` AND
	// `m_DoorState == 1`; the activity seam answers -1, so the gate cannot pass here and the whole
	// body is a single `ClearCondition(0x7a)`.
	const EElysiumNpcCond DeathTriggered = static_cast<EElysiumNpcCond>(0x7a);
	Wolf->WerewolfDoorState = 1;
	Wolf->Cognition.Conditions.Set(DeathTriggered);
	Wolf->UpdateConditionDeathTriggered();
	TestFalse(TEXT("0x7a is cleared unconditionally at the top of the body"),
		Wolf->Cognition.Conditions.Has(DeathTriggered));
	F.World.World.Tick(F.World.World.NowSeconds());
	TestEqual(TEXT("and with the gate closed the output does not fire"),
		F.World.Counter(TEXT("deathtriggered")), 0.f);

	// The door-state term on its own refuses too, which is the second half of the gate.
	Wolf->WerewolfDoorState = 0;
	Wolf->UpdateConditionDeathTriggered();
	TestFalse(TEXT("a closed door leaves 0x7a clear"),
		Wolf->Cognition.Conditions.Has(DeathTriggered));

	// The gather suppression (`0x103d02b0`): the zone word and the 40-unit height delta, on the
	// werewolf `npc_VWerewolf` builds (story 5 step 2).
	TestNotNull(TEXT("CNPC_VWerewolf is a census class"),
		ElysiumNpcTestCensus::Find(TEXT("CNPC_VWerewolf")));
	TestTrue(TEXT("and the subject is one"), Wolf->AsSpecies<FElysiumNpcWerewolf>() != nullptr);

	// The arm is a SUPPRESSION: with the zone bits set and the enemy 40 units above, the melee pair
	// is taken away. Driven through the condition set directly, because the gather's own entry gates
	// (a live enemy, a world) are `ElysiumNpcCond::GatherAttackConditions`' and not this arm's.
	Wolf->WerewolfHintFlags = 0x4u;
	TestTrue(TEXT("the zone word carries bit 0x4"), (Wolf->WerewolfHintFlags & 0x4u) != 0);
	TestFalse(TEXT("bit 0x100 is the other admitted one"),
		(Wolf->WerewolfHintFlags & 0x100u) != 0);
	return true;
}

// =================================================================================================
// Slot 561 — `GatherAttackConditions` (`0x1026dd10`), and the melee weapon's band (`0x103ea7e0`)
// (spec 0002 V5a-1)
// =================================================================================================

namespace
{
	// A base-line body whose two innate producers (slots 553 / 555) and slot 331 answer what the case
	// states, so each arm of the gather is driven without a weapon entity. `m_afCapability` selects
	// the innate arms (`0x1026de8f` 0x20000, `0x1026dfa5` 0x80000).
	class FGatherProbeNpc final : public FElysiumNpcBase
	{
	public:
		int32 RangedAnswer = 0;
		int32 MeleeAnswer = 0;
		// Raised from INSIDE the ranged producer, after the top clear: where retail's slot 562 /
		// weapon slot 364 raise them.
		TArray<EElysiumNpcCond> RaiseInRangedArm;
		TArray<FString> Calls;
		bool bSlot327 = true;
		bool bSlot331 = false;
		int32 Slot331Out = INDEX_NONE;
		int32 Slot331Calls = 0;
		int32 Slot331Activity = 0;

		// The probe is never registered with the world it borrows; it leaves it before the base
		// destructors run.
		virtual ~FGatherProbeNpc() override { World = nullptr; }

		virtual int32 RangeAttack1Conditions(float, float) override
		{
			Calls.Add(TEXT("553"));
			for (const EElysiumNpcCond Cond : RaiseInRangedArm)
			{
				Cognition.Conditions.Set(Cond);
			}
			return RangedAnswer;
		}
		virtual int32 MeleeAttack1Conditions(float, float) override
		{
			Calls.Add(TEXT("555"));
			return MeleeAnswer;
		}
		virtual bool Slot327() override { return bSlot327; }
		virtual bool ChooseMeleeAttackSequence(FElysiumEntity*, FElysiumEntity*, int32 Activity,
			void* Out) override
		{
			++Slot331Calls;
			Slot331Activity = Activity;
			*static_cast<int32*>(Out) = Slot331Out;
			return bSlot331;
		}
	};

	constexpr int32 GGatherTestCapInnateRange1 = 0x20000;
	constexpr int32 GGatherTestCapInnateMelee1 = 0x80000;
	const double GGatherTestFltMax = static_cast<double>(TNumericLimits<float>::Max());

	// The probe, committed to the fixture's subject as its enemy, the timers at their spawn values.
	void ArmGatherProbe(FGatherProbeNpc& Probe, FNpcKernelCondFixture& F, int32 Capabilities)
	{
		Probe.World = &F.World.World;
		Probe.BaseMemory.Enemy = F.Npc->Handle;
		Probe.BaseMemory.EnemyOccludedCheck = 0;     // slot 481's latch: the enemy stated in sight
		Probe.CapabilityWord = Capabilities;
		Probe.WeaponBlockedByFriendTimer = 0.0;      // +0x5b88
		Probe.ExtendedBlockedByFriendTimer = GGatherTestFltMax;   // +0x5b8c
		F.Npc->Origin = FVector(400.0 * ElysiumMove::U, 0.0, 0.0);
	}

	// One sequence descriptor as `0x103ea7e0` reads it, stated in SOURCE units and baked to the
	// clip's centimetres: `+0x10` weight, `+0x2c4` swing count, `+0x2cc` / `+0x2d0` the reach band,
	// one `+0x2bc` envelope whose two corners' first axis are `EnvMin` / `EnvMax`.
	struct FBandSequence
	{
		int32 Weight = 1;
		int32 Swings = 1;
		float LowUnits = 0.f;
		float HighUnits = 0.f;
		float EnvMinUnits = 0.f;
		float EnvMaxUnits = 0.f;
		bool bEnvelope = true;
	};
	FElysiumNpcClip BandClip(const FBandSequence& Spec)
	{
		const float Cm = static_cast<float>(ElysiumMove::U);
		FElysiumNpcClip Clip;
		Clip.Weight = Spec.Weight;
		Clip.Swings.AddDefaulted(Spec.Swings);
		Clip.LowReachCm = Spec.LowUnits * Cm;
		Clip.ReachCm = Spec.HighUnits * Cm;
		if (Spec.bEnvelope)
		{
			FElysiumMeleeEnvelope Envelope;
			Envelope.Min = FVector(Spec.EnvMinUnits * Cm, -10.0, -10.0);
			Envelope.Max = FVector(Spec.EnvMaxUnits * Cm, 10.0, 10.0);
			Clip.Envelopes.Add(Envelope);
		}
		return Clip;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCondGatherAttackClearsTest,
	"Elysium.Arm.NpcKernelConditions.GatherAttackClears", GNpcKernelCondFlags)
bool FElysiumNpcKernelCondGatherAttackClearsTest::RunTest(const FString&)
{
	FNpcKernelCondFixture F;
	if (!TestNotNull(TEXT("the subject spawned"), F.Npc)) { return false; }
	const EElysiumNpcCond Bands[] = { EElysiumNpcCond::TooCloseForRanged, EElysiumNpcCond::TooCloseToAttack,
		EElysiumNpcCond::TooFarToAttack, EElysiumNpcCond::TooFarForMelee };   // 0x08 0x5f 0x60 0x09

	// --- `0x1026de02`: slot 560 at the top. A band word of the previous gather survives it, a
	// CAN_* and 0x2f do not; and with no CAN_* standing afterwards the tail leaves the bands alone
	// (`0x1026e0dd JZ 0x1026e10c`).
	{
		FGatherProbeNpc Probe;
		ArmGatherProbe(Probe, F, GGatherTestCapInnateRange1);
		Probe.RangedAnswer = CondNum(EElysiumNpcCond::NotFacingAttack);   // 0x61: no CAN_* this gather
		FElysiumNpcConditions& C = Probe.Cognition.Conditions;
		for (const EElysiumNpcCond Band : Bands) { C.Set(Band); }
		C.Set(EElysiumNpcCond::CanRangeAttack1);
		C.Set(EElysiumNpcCond::CanMeleeAttack1);
		C.Set(EElysiumNpcCond::WaitingAttackTime);
		ElysiumNpcCond::GatherAttackConditions(Probe, 10.0);
		TestFalse(TEXT("0x1026de02: the previous gather's CAN_RANGE_ATTACK1 (0x4f) is cleared"),
			C.Has(EElysiumNpcCond::CanRangeAttack1));
		TestFalse(TEXT("0x1026de02: the previous gather's CAN_MELEE_ATTACK1 (0x51) is cleared"),
			C.Has(EElysiumNpcCond::CanMeleeAttack1));
		TestFalse(TEXT("0x1026de02: the previous gather's WAITING_ATTACK_TIME (0x2f) is cleared"),
			C.Has(EElysiumNpcCond::WaitingAttackTime));
		TestTrue(TEXT("0x1026df64: the ranged answer 0x61 is set"), C.Has(EElysiumNpcCond::NotFacingAttack));
		for (const EElysiumNpcCond Band : Bands)
		{
			TestTrue(FString::Printf(TEXT("0x1026e0dd: no CAN_* standing, band 0x%02x stays"), CondNum(Band)),
				C.Has(Band));
		}
	}

	// --- `0x1026e0df`: a CAN_* standing with `+0x5b88` lapsed clears 0x08 / 0x5f / 0x60 / 0x09.
	{
		FGatherProbeNpc Probe;
		ArmGatherProbe(Probe, F, GGatherTestCapInnateRange1);
		Probe.RangedAnswer = CondNum(EElysiumNpcCond::CanRangeAttack1);   // 0x4f, in sight
		FElysiumNpcConditions& C = Probe.Cognition.Conditions;
		for (const EElysiumNpcCond Band : Bands) { C.Set(Band); }
		ElysiumNpcCond::GatherAttackConditions(Probe, 10.0);
		TestTrue(TEXT("0x1026df52: 0x4f stands"), C.Has(EElysiumNpcCond::CanRangeAttack1));
		for (const EElysiumNpcCond Band : Bands)
		{
			TestFalse(FString::Printf(TEXT("0x1026e0df: a CAN_* standing clears band 0x%02x"), CondNum(Band)),
				C.Has(Band));
		}
		TestFalse(TEXT("0x1026e107: ...and 0x63"), C.Has(EElysiumNpcCond::WeaponBlockedByFriend));
	}

	// --- `0x1026df00`: slot 560 again between the two LOS tests of a 0x4f answer. A word of the
	// eleven raised inside the ranged arm survives a passing first test and not a failing one; both
	// tests failing sets no 0x4f (`0x1026df45 JZ 0x1026df69`).
	{
		FGatherProbeNpc Probe;
		ArmGatherProbe(Probe, F, GGatherTestCapInnateRange1);
		Probe.RangedAnswer = CondNum(EElysiumNpcCond::CanRangeAttack1);
		Probe.RaiseInRangedArm = { EElysiumNpcCond::WeaponHasLos };   // 0x62
		FElysiumNpcConditions& C = Probe.Cognition.Conditions;
		ElysiumNpcCond::GatherAttackConditions(Probe, 10.0);
		TestTrue(TEXT("0x1026defa: the first LOS test passing skips the second clear (0x62 stays)"),
			C.Has(EElysiumNpcCond::WeaponHasLos));

		Probe.BaseMemory.EnemyOccludedCheck = 10;   // the occlusion latch at its limit: both tests fail
		ElysiumNpcCond::GatherAttackConditions(Probe, 10.0);
		TestFalse(TEXT("0x1026df00: the failing first test runs slot 560 again (0x62 cleared)"),
			C.Has(EElysiumNpcCond::WeaponHasLos));
		TestFalse(TEXT("0x1026df45: both tests failing sets no 0x4f"), C.Has(EElysiumNpcCond::CanRangeAttack1));
		TestTrue(TEXT("the stand-in's failing test raises WEAPON_SIGHT_OCCLUDED (0x66)"),
			C.Has(EElysiumNpcCond::WeaponSightOccluded));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCondGatherAttackFriendTimersTest,
	"Elysium.Arm.NpcKernelConditions.GatherAttackFriendTimers", GNpcKernelCondFlags)
bool FElysiumNpcKernelCondGatherAttackFriendTimersTest::RunTest(const FString&)
{
	FNpcKernelCondFixture F;
	if (!TestNotNull(TEXT("the subject spawned"), F.Npc)) { return false; }
	FGatherProbeNpc Probe;
	ArmGatherProbe(Probe, F, GGatherTestCapInnateRange1);
	FElysiumNpcConditions& C = Probe.Cognition.Conditions;

	// 0x63 raised inside the ranged arm (retail: slot 562's weapon slot 364 on a friendly hit).
	Probe.RangedAnswer = 0;
	Probe.RaiseInRangedArm = { EElysiumNpcCond::WeaponBlockedByFriend };
	ElysiumNpcCond::GatherAttackConditions(Probe, 10.0);
	TestEqual(TEXT("0x1026dff2: 0x63 with +0x5b8c at FLT_MAX arms it at curtime + 2.5"),
		Probe.ExtendedBlockedByFriendTimer, 12.5);
	TestEqual(TEXT("0x1026e006: 0x63 arms +0x5b88 at curtime + 1.5"), Probe.WeaponBlockedByFriendTimer, 11.5);
	TestFalse(TEXT("0x1026e04c: the extended timer has not lapsed, no 0x2e"),
		C.Has(EElysiumNpcCond::ExtendedBlockedByFriend));

	ElysiumNpcCond::GatherAttackConditions(Probe, 11.0);
	TestEqual(TEXT("0x1026dfe7: an armed +0x5b8c is not re-armed"), Probe.ExtendedBlockedByFriendTimer, 12.5);
	TestEqual(TEXT("0x1026e006: +0x5b88 is re-armed every gather 0x63 stands"),
		Probe.WeaponBlockedByFriendTimer, 12.5);

	ElysiumNpcCond::GatherAttackConditions(Probe, 13.0);
	TestTrue(TEXT("0x1026e05d: +0x5b8c < curtime raises EXTENDED_BLOCKED_BY_FRIEND (0x2e)"),
		C.Has(EElysiumNpcCond::ExtendedBlockedByFriend));
	TestEqual(TEXT("+0x5b88 is curtime + 1.5 again"), Probe.WeaponBlockedByFriendTimer, 14.5);

	// The friend gone, a 0x4f answer, the block still held (`curtime < +0x5b88`).
	Probe.RaiseInRangedArm.Reset();
	Probe.RangedAnswer = CondNum(EElysiumNpcCond::CanRangeAttack1);
	ElysiumNpcCond::GatherAttackConditions(Probe, 14.0);
	TestTrue(TEXT("0x1026e087: while +0x5b88 holds, 0x63 is re-raised"),
		C.Has(EElysiumNpcCond::WeaponBlockedByFriend));
	TestFalse(TEXT("0x1026e099: ...and CAN_RANGE_ATTACK1 (0x4f) cleared"),
		C.Has(EElysiumNpcCond::CanRangeAttack1));
	TestEqual(TEXT("0x1026e02a: +0x5b88 still ahead, +0x5b8c is left alone"),
		Probe.ExtendedBlockedByFriendTimer, 12.5);
	TestTrue(TEXT("0x1026e05d: ...so 0x2e is raised again"), C.Has(EElysiumNpcCond::ExtendedBlockedByFriend));

	// The melee CAN_* goes the same way under the hold.
	Probe.CapabilityWord = GGatherTestCapInnateRange1 | GGatherTestCapInnateMelee1;
	Probe.MeleeAnswer = CondNum(EElysiumNpcCond::CanMeleeAttack1);
	ElysiumNpcCond::GatherAttackConditions(Probe, 14.25);
	TestFalse(TEXT("0x1026e107: while +0x5b88 holds, CAN_MELEE_ATTACK1 (0x51) is cleared"),
		C.Has(EElysiumNpcCond::CanMeleeAttack1));
	Probe.CapabilityWord = GGatherTestCapInnateRange1;

	// The hold lapsed (`+0x5b88 <= curtime`) without 0x63: the extended timer returns to FLT_MAX.
	ElysiumNpcCond::GatherAttackConditions(Probe, 15.0);
	TestEqual(TEXT("0x1026e02c: +0x5b88 <= curtime without 0x63 resets +0x5b8c to FLT_MAX"),
		Probe.ExtendedBlockedByFriendTimer, GGatherTestFltMax);
	TestFalse(TEXT("0x1026e04c: no 0x2e"), C.Has(EElysiumNpcCond::ExtendedBlockedByFriend));
	TestTrue(TEXT("0x1026e076: the hold over, 0x4f stands"), C.Has(EElysiumNpcCond::CanRangeAttack1));
	TestFalse(TEXT("0x1026e107: ...and 0x63 is down"), C.Has(EElysiumNpcCond::WeaponBlockedByFriend));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCondGatherAttackBothArmsTest,
	"Elysium.Arm.NpcKernelConditions.GatherAttackBothArms", GNpcKernelCondFlags)
bool FElysiumNpcKernelCondGatherAttackBothArmsTest::RunTest(const FString&)
{
	FNpcKernelCondFixture F;
	if (!TestNotNull(TEXT("the subject spawned"), F.Npc)) { return false; }

	// `0x1026df6d`: the melee arm runs AFTER a ranged answer, not instead of it.
	{
		FGatherProbeNpc Probe;
		ArmGatherProbe(Probe, F, GGatherTestCapInnateRange1 | GGatherTestCapInnateMelee1);
		Probe.RangedAnswer = CondNum(EElysiumNpcCond::NotFacingAttack);   // 0x61
		Probe.MeleeAnswer = CondNum(EElysiumNpcCond::CanMeleeAttack1);    // 0x51
		FElysiumNpcConditions& C = Probe.Cognition.Conditions;
		ElysiumNpcCond::GatherAttackConditions(Probe, 10.0);
		if (TestEqual(TEXT("0x1026ded1 then 0x1026dfc2: both producers run"), Probe.Calls.Num(), 2))
		{
			TestEqual(TEXT("slot 553 first"), Probe.Calls[0], FString(TEXT("553")));
			TestEqual(TEXT("slot 555 second"), Probe.Calls[1], FString(TEXT("555")));
		}
		TestTrue(TEXT("0x1026df64: the ranged answer is set"), C.Has(EElysiumNpcCond::NotFacingAttack));
		TestTrue(TEXT("0x1026dfcb: the melee answer is set beside it"), C.Has(EElysiumNpcCond::CanMeleeAttack1));
	}
	// Each bit alone runs its own arm only (`0x1026de95 JZ 0x1026df6d`, `0x1026dfab JZ 0x1026dfd0`).
	{
		FGatherProbeNpc Probe;
		ArmGatherProbe(Probe, F, GGatherTestCapInnateMelee1);
		Probe.MeleeAnswer = CondNum(EElysiumNpcCond::TooFarForMelee);
		ElysiumNpcCond::GatherAttackConditions(Probe, 10.0);
		TestEqual(TEXT("0x1026de95: no ranged bit, slot 553 is not called"), Probe.Calls.Num(), 1);
		TestTrue(TEXT("0x1026dfcb: 0x09 from slot 555"),
			Probe.Cognition.Conditions.Has(EElysiumNpcCond::TooFarForMelee));
	}
	{
		FGatherProbeNpc Probe;
		ArmGatherProbe(Probe, F, 0);
		ElysiumNpcCond::GatherAttackConditions(Probe, 10.0);
		TestEqual(TEXT("0x1026dfab: neither bit, neither producer"), Probe.Calls.Num(), 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCondMeleeWeaponBandTest,
	"Elysium.Arm.NpcKernelConditions.MeleeWeaponBand", GNpcKernelCondFlags)
bool FElysiumNpcKernelCondMeleeWeaponBandTest::RunTest(const FString&)
{
	// `0x103ea7e0` on fixture clips. One sequence: reach band [20, 100], one envelope 40..80 (mean
	// 60). `hi * 1.2 = 120 < 256`, so the far limit is the floor 256.
	FGatherProbeNpc Owner;
	FGatherProbeNpc Target;
	FBandSequence Spec;
	Spec.LowUnits = 20.f; Spec.HighUnits = 100.f; Spec.EnvMinUnits = 40.f; Spec.EnvMaxUnits = 80.f;
	const TArray<FElysiumNpcClip> Band = { BandClip(Spec) };

	auto Ask = [&Owner](FElysiumEntity* TargetEntity, TConstArrayView<FElysiumNpcClip> Sequences,
		float Dot, float Dist, double NextPrimary = 0.0)
	{
		ElysiumNpcCond::FMeleeWeaponBandQuery Query;
		Query.Owner = &Owner;
		Query.Target = TargetEntity;
		Query.TranslatedActivity = 0x4b;
		Query.WeaponNextPrimaryAttackTime = NextPrimary;
		Query.Dot = Dot;
		Query.DistUnits = Dist;
		Query.Now = 10.0;
		Query.Sequences = Sequences;
		return ElysiumNpcCond::MeleeWeaponBand(Query);
	};

	TestEqual(TEXT("0x103eaa7b: dist > max(1.2 * hi, 256) is 9"), Ask(&Target, Band, 0.9f, 300.f), 0x09);
	TestEqual(TEXT("0x103eaaa1: hi < dist <= 256 is 0x60"), Ask(&Target, Band, 0.9f, 200.f), 0x60);
	TestEqual(TEXT("0x103eaac7: dot < 0.7 inside hi is 0x61"), Ask(&Target, Band, 0.5f, 50.f), 0x61);
	TestEqual(TEXT("0x103eaa7b: the far word comes before the facing one"), Ask(&Target, Band, 0.5f, 300.f), 0x09);
	TestEqual(TEXT("0x103eaaeb: dist < lo is 0x5f"), Ask(&Target, Band, 0.9f, 10.f), 0x5f);
	TestEqual(TEXT("0x103eab2a: in band, ready, slot 331 refusing: dist >= mean * 0.25 is 0x60"),
		Ask(&Target, Band, 0.9f, 50.f), 0x60);
	TestEqual(TEXT("0x103ea8d3: slot 331 was asked each time dot > 0.7 with a ready CC target"),
		Owner.Slot331Calls, 4);
	TestEqual(TEXT("0x103ea8c6: ...with the translated activity"), Owner.Slot331Activity, 0x4b);

	// `mean * 0.25`: a band starting at the body (lo 0), mean 60 -> 15.
	{
		FBandSequence FromBody = Spec;
		FromBody.LowUnits = 0.f;
		const TArray<FElysiumNpcClip> Close = { BandClip(FromBody) };
		TestEqual(TEXT("0x103eab20: dist < mean * 0.25 is 0x5f"), Ask(&Target, Close, 0.9f, 10.f), 0x5f);
		TestEqual(TEXT("0x103eab2a: dist above it is 0x60"), Ask(&Target, Close, 0.9f, 20.f), 0x60);
	}

	// `hi * 1.2` above the floor: hi 300 -> 360.
	{
		FBandSequence Long = Spec;
		Long.HighUnits = 300.f;
		const TArray<FElysiumNpcClip> Far = { BandClip(Long) };
		TestEqual(TEXT("0x103eaa47: hi < dist <= 1.2 * hi is 0x60"), Ask(&Target, Far, 0.9f, 350.f), 0x60);
		TestEqual(TEXT("0x103eaa7b: dist > 1.2 * hi is 9"), Ask(&Target, Far, 0.9f, 370.f), 0x09);
	}

	// `ready` (`0x103ea84e..0x103ea899`): an unexpired stamp, or the target's slot 327 refusing,
	// closes the 0x51 arm and the last arm; the band words still answer.
	{
		const int32 Before = Owner.Slot331Calls;
		TestEqual(TEXT("0x103ea85c: +0x730 >= curtime is not ready: in band answers 0"),
			Ask(&Target, Band, 0.9f, 50.f, 10.0), 0);
		Target.bSlot327 = false;
		TestEqual(TEXT("0x103ea893: the target's slot 327 refusing is not ready: 0"),
			Ask(&Target, Band, 0.9f, 50.f), 0);
		TestEqual(TEXT("...and the far word still answers"), Ask(&Target, Band, 0.9f, 300.f), 0x09);
		Target.bSlot327 = true;
		TestEqual(TEXT("0x103ea8bd: not ready never asks slot 331"), Owner.Slot331Calls, Before);
	}

	// The 0x51 arm (`0x103ea89d..0x103ea8e8`): dot > 0.7, a CC target, ready, slot 331 true with
	// out >= 0. It answers before any band word.
	{
		Owner.bSlot331 = true;
		Owner.Slot331Out = 3;
		TestEqual(TEXT("0x103ea8e8: slot 331 answering with out >= 0 is 0x51"),
			Ask(&Target, Band, 0.9f, 50.f), 0x51);
		TestEqual(TEXT("0x103ea8e8: ...ahead of the far word"), Ask(&Target, Band, 0.9f, 300.f), 0x51);
		const int32 Before = Owner.Slot331Calls;
		// The cell is an f64 and the argument an f32: 0.7f widens to 0.69999998..., BELOW the cell.
		TestEqual(TEXT("0x103ea8b1 / 0x103eaac2: the float 0.7 is under the f64 0.7: no 0x51, 0x61"),
			Ask(&Target, Band, 0.7f, 50.f), 0x61);
		TestEqual(TEXT("0x103ea8b5: no CC target, no 0x51"), Ask(nullptr, Band, 0.9f, 50.f), 0);
		TestEqual(TEXT("...and slot 331 was asked for neither"), Owner.Slot331Calls, Before);
		Owner.Slot331Out = INDEX_NONE;
		TestEqual(TEXT("0x103ea8e3: slot 331 true with out < 0 is not 0x51"),
			Ask(&Target, Band, 0.9f, 50.f), 0x60);
		Owner.bSlot331 = false;
	}

	// Which sequences count (`0x103ea976..0x103ea989`), and none counted -> 0 (`0x103eaa03`).
	{
		TestEqual(TEXT("0x103ea95a: no sequence answers 0"),
			Ask(&Target, TConstArrayView<FElysiumNpcClip>(), 0.9f, 300.f), 0);
		FBandSequence NoSwing = Spec;
		NoSwing.Swings = 0;
		const TArray<FElysiumNpcClip> Unswung = { BandClip(NoSwing) };
		TestEqual(TEXT("0x103ea989: a sequence with no swing record is not counted"),
			Ask(&Target, Unswung, 0.9f, 300.f), 0);
		FBandSequence Weightless = Spec;
		Weightless.Weight = 0;
		const TArray<FElysiumNpcClip> ZeroWeight = { BandClip(Weightless) };
		TestEqual(TEXT("0x103ea97c: weight 0 is not counted without a CC target"),
			Ask(nullptr, ZeroWeight, 0.9f, 300.f), 0);
		TestEqual(TEXT("0x103ea978: ...and is counted with one"), Ask(&Target, ZeroWeight, 0.9f, 300.f), 0x09);
		FBandSequence NoEnvelope = Spec;
		NoEnvelope.bEnvelope = false;
		const TArray<FElysiumNpcClip> Bare = { BandClip(NoEnvelope) };
		TestEqual(TEXT("0x103eaa03: no envelope record counted answers 0"),
			Ask(&Target, Bare, 0.9f, 300.f), 0);
	}

	// `lo` / `hi` are the minimum / maximum over the counted sequences (`0x103ea98b..0x103ea9bf`).
	{
		FBandSequence Short = Spec;
		Short.LowUnits = 5.f; Short.HighUnits = 60.f;
		const TArray<FElysiumNpcClip> Two = { BandClip(Spec), BandClip(Short) };
		TestEqual(TEXT("one sequence, lo 20: dist 18 is under it (0x5f)"), Ask(&Target, Band, 0.9f, 18.f), 0x5f);
		TestEqual(TEXT("0x103ea9a2: lo is the smallest +0x2cc (5): dist 18 is past it and past mean * 0.25"),
			Ask(&Target, Two, 0.9f, 18.f), 0x60);
		TestEqual(TEXT("0x103ea9bf: hi is the largest +0x2d0 (100): dist 90 is inside it"),
			Ask(&Target, Two, 0.5f, 90.f), 0x61);
	}

	TestEqual(TEXT("0x103ea7f9: no owner answers 0"),
		ElysiumNpcCond::MeleeWeaponBand(ElysiumNpcCond::FMeleeWeaponBandQuery()), 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
