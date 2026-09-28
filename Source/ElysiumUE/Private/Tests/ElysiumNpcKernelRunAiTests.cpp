// Story 0019/8 (29e under the strict verdict), family **RunAi19** -- the family's tests.
//
// Test names carry `Elysium.Substrate.NpcKernelRunAi19.` and the retail address. Every assertion is
// read off the listing (`families-19-29/RunAi19-READING.md`, `vtmb_asm`). Each case drives the
// body directly on an NPC stood as the retail class the row belongs to, with its AI quieted so no
// think competes with the pass the case runs.
//
// Owns (RunAi19's `rule` rows): 0x1028fd80 CAI_BaseNPCTroika::RunAlternateAI, 0x1026f110
// CAI_BaseNPC::RunAI, 0x1028fcc0 CAI_BaseNPCTroika::RunAI, 0x1039e3d0
// CNPC_VMingXiaoTentacle::RunAI, 0x103bdef0 CNPC_VTzimisce::vfunc432, 0x103c1d20
// CNPC_VTzimisceHeadClaw::vfunc432, 0x1035e980 CNPC_VAndreiBlood::vfunc432, 0x10361110
// CNPC_VAsianVampire::RunAI, 0x10363b60 CNPC_VBach::vfunc432, 0x103747e0 CNPC_VDog::vfunc432,
// 0x10378b80 CNPC_VGargoyle::vfunc432, 0x10380120 CNPC_VHengeyokai::vfunc432, 0x1038e990
// CNPC_VManBat::vfunc432, 0x103a3670 CNPC_VPedestrian::vfunc432, 0x103a75c0
// CNPC_VSabbatLeader::RunAI, 0x103aebd0 CNPC_VSheriffMan::RunAI, 0x103df850 CNPC_VZombie::vfunc432.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcMaker.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumSchedule.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumNpcTestCensus.h"
#include "Substrate/ElysiumNpcAndreiBlood.h"
#include "Substrate/ElysiumNpcAsianVampire.h"
#include "Substrate/ElysiumNpcBach.h"
#include "Substrate/ElysiumNpcDog.h"
#include "Substrate/ElysiumNpcGargoyle.h"
#include "Substrate/ElysiumNpcHengeyokai.h"
#include "Substrate/ElysiumNpcManBat.h"
#include "Substrate/ElysiumNpcMingXiaoTentacle.h"
#include "Substrate/ElysiumNpcPedestrian.h"
#include "Substrate/ElysiumNpcSabbatLeader.h"
#include "Substrate/ElysiumNpcSheriffMan.h"
#include "Substrate/ElysiumNpcTzimisce.h"
#include "Substrate/ElysiumNpcTzimisceHeadClaw.h"
#include "Substrate/ElysiumNpcZombie.h"

static constexpr EAutomationTestFlags GRunAi19Flags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// `IDLE_STAND` with an EMPTY interrupt mask for as long as a case runs, so a `MaintainSchedule`
	// the body under test reaches keeps the program and never gets to `SetSchedule`'s clear of the
	// condition word. The scope is read live by the mask test, so it lives with the fixture.
	struct FRunAi19QuietMask
	{
		FElysiumNpcConditions NoInterrupts;
		ElysiumSchedule::FInterruptMaskScope Scope{ ElysiumSched::IDLE_STAND, NoInterrupts };
	};

	struct FRunAi19Fixture
	{
		FElysiumNpcWorldFixture World;
		FRunAi19QuietMask QuietMask;
		FElysiumNpc* Npc = nullptr;
		FElysiumNpc* Other = nullptr;
		FElysiumPlayer* Player = nullptr;

		// `SubjectClass` is the retail class the subject is built as; `CAI_BaseNPCTroika` stands the
		// bare Troika line.
		explicit FRunAi19Fixture(const TCHAR* SubjectClass = TEXT("CAI_BaseNPCTroika"))
			: World([SubjectClass]
				{
					FElysiumNpcWorldBuilder Builder(TEXT("runai19_kernel"), 432);
					Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
					Builder.AddNpcOfClass(TEXT("subject"), FVector::ZeroVector, SubjectClass);
					Builder.AddNpc(TEXT("other"), FVector(400.f, 0.f, 0.f), TEXT("npc_VHumanCombatant"));
					return Builder;
				}())
		{
			Npc = World.Npc(TEXT("subject"));
			Other = World.Npc(TEXT("other"));
			Player = World.Player();
			FElysiumNpcWorldFixture::Quiet({ Npc, Other });
		}

		double Now() const { return World.World.NowSeconds(); }
	};

	// Installs `IDLE_STAND`, whose interrupts the fixture's `QuietMask` has emptied.
	void RunAi19InstallQuietProgram(FElysiumNpc& N)
	{
		N.Schedule.Clear();
		ElysiumSchedule::Start(N.Schedule, ElysiumSched::IDLE_STAND, N);
	}

	void RunAi19SetOnePassConditions(FElysiumNpc& N)
	{
		N.Cognition.Conditions.Set(EElysiumNpcCond::LightDamage);
		N.Cognition.Conditions.Set(EElysiumNpcCond::HeavyDamage);
		N.Cognition.Conditions.Set(EElysiumNpcCond::WasBumped);
	}
}

// =================================================================================================
// 0x1026f110 CAI_BaseNPC::RunAI
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunAi19BaseRunAiFullPassTest,
	"Elysium.Substrate.NpcKernelRunAi19.BaseRunAI.FullPass", GRunAi19Flags)
bool FElysiumNpcKernelRunAi19BaseRunAiFullPassTest::RunTest(const FString&)
{
	// 0x1026f110: clear the gathered byte, gather (full pass, no partner), latch, head probe,
	// PrescheduleThink, MaintainSchedule, then the three end-of-pass clears and m_bRanAI.
	FRunAi19Fixture F;
	if (!TestNotNull(TEXT("subject"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	RunAi19InstallQuietProgram(N);
	RunAi19SetOnePassConditions(N);
	N.Cognition.GatheredAt = -5.0;
	N.BaseScheduleHost.bRanAi = false;
	N.FElysiumNpcBase::RunAI(false);
	TestEqual(TEXT("1026f237 slot 433 ran on a full pass with no partner: gathered this pass"),
		N.Cognition.GatheredAt, F.Now());
	TestFalse(TEXT("1026f311 LIGHT_DAMAGE 0x4c cleared"),
		N.Cognition.Conditions.Has(EElysiumNpcCond::LightDamage));
	TestFalse(TEXT("1026f31a HEAVY_DAMAGE 0x4d cleared"),
		N.Cognition.Conditions.Has(EElysiumNpcCond::HeavyDamage));
	TestFalse(TEXT("1026f323 WAS_BUMPED 0x38 cleared"),
		N.Cognition.Conditions.Has(EElysiumNpcCond::WasBumped));
	TestTrue(TEXT("1026f32c m_bRanAI = 1"), N.BaseScheduleHost.bRanAi);
	TestEqual(TEXT("1026f1c3 developer 0: no overlay"), N.RunAi19OverlayCalls, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunAi19BaseRunAiReducedTest,
	"Elysium.Substrate.NpcKernelRunAi19.BaseRunAI.Reduced", GRunAi19Flags)
bool FElysiumNpcKernelRunAi19BaseRunAiReducedTest::RunTest(const FString&)
{
	// 0x1026f1ea: a reduced pass skips the gather; 0x1026f30b: and the three clears. m_bRanAI is
	// written on every pass.
	FRunAi19Fixture F;
	if (!TestNotNull(TEXT("subject"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	RunAi19InstallQuietProgram(N);
	RunAi19SetOnePassConditions(N);
	N.Cognition.GatheredAt = F.Now();
	N.BaseScheduleHost.bRanAi = false;
	N.FElysiumNpcBase::RunAI(true);
	TestTrue(TEXT("1026f1ad the byte is cleared and a reduced pass does not gather"),
		N.Cognition.GatheredAt < 0.0);
	TestTrue(TEXT("1026f30b reduced: LIGHT_DAMAGE stands"),
		N.Cognition.Conditions.Has(EElysiumNpcCond::LightDamage));
	TestTrue(TEXT("1026f30b reduced: HEAVY_DAMAGE stands"),
		N.Cognition.Conditions.Has(EElysiumNpcCond::HeavyDamage));
	TestTrue(TEXT("1026f30b reduced: WAS_BUMPED stands"),
		N.Cognition.Conditions.Has(EElysiumNpcCond::WasBumped));
	TestTrue(TEXT("1026f32c m_bRanAI = 1 on a reduced pass too"), N.BaseScheduleHost.bRanAi);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunAi19BaseRunAiDialogPartnerTest,
	"Elysium.Substrate.NpcKernelRunAi19.BaseRunAI.DialogPartner", GRunAi19Flags)
bool FElysiumNpcKernelRunAi19BaseRunAiDialogPartnerTest::RunTest(const FString&)
{
	// 0x1026f1f9..0x1026f219: a live m_hDialogPartner skips the gather on a full pass; the end-of-pass
	// clears are gated on bReduced alone (0x1026f30b), so they still run.
	FRunAi19Fixture F;
	if (!TestNotNull(TEXT("subject"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	RunAi19InstallQuietProgram(N);
	RunAi19SetOnePassConditions(N);
	N.Dialogue.bInDialog = true;
	TestTrue(TEXT("the partner reads live"), N.RunAi19DialogPartnerLive());
	N.FElysiumNpcBase::RunAI(false);
	TestTrue(TEXT("1026f219 live partner: no gather, the byte stays clear"),
		N.Cognition.GatheredAt < 0.0);
	TestFalse(TEXT("1026f311 the clears still run"),
		N.Cognition.Conditions.Has(EElysiumNpcCond::LightDamage));
	TestFalse(TEXT("1026f323 WAS_BUMPED cleared"),
		N.Cognition.Conditions.Has(EElysiumNpcCond::WasBumped));
	TestTrue(TEXT("1026f32c m_bRanAI"), N.BaseScheduleHost.bRanAi);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunAi19BaseRunAiOverlayTest,
	"Elysium.Substrate.NpcKernelRunAi19.BaseRunAI.Overlay", GRunAi19Flags)
bool FElysiumNpcKernelRunAi19BaseRunAiOverlayTest::RunTest(const FString&)
{
	// 0x1026f1be..0x1026f1e1: the overlay needs developer != 0 AND m_pNavigator->m_bNotOnNetwork.
	// The navigator byte is a seam answering false (`Conditions19NavNotOnNetwork`), so the arm stays
	// shut even with developer raised; the seam itself records retail's literal and duration.
	FRunAi19Fixture F;
	if (!TestNotNull(TEXT("subject"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	RunAi19InstallQuietProgram(N);
	FElysiumNpcMaker::DeveloperCvarLevel = 1;
	N.FElysiumNpcBase::RunAI(true);
	FElysiumNpcMaker::DeveloperCvarLevel = 0;
	TestEqual(TEXT("1026f1d6 on-network navigator: no overlay"), N.RunAi19OverlayCalls, 0);
	N.RunAi19AddTimedOverlay(TEXT("NPC w/no reachable nodes!"), 5.0f);
	TestEqual(TEXT("1026f1da the literal"), N.RunAi19LastOverlayText,
		FString(TEXT("NPC w/no reachable nodes!")));
	TestEqual(TEXT("1026f1d8 PUSH 0x5"), N.RunAi19LastOverlaySeconds, 5.0f);
	return true;
}

// =================================================================================================
// 0x1028fcc0 CAI_BaseNPCTroika::RunAI
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunAi19TroikaRunAiTest,
	"Elysium.Substrate.NpcKernelRunAi19.TroikaRunAI", GRunAi19Flags)
bool FElysiumNpcKernelRunAi19TroikaRunAiTest::RunTest(const FString&)
{
	// 0x1028fcc0: 0x1028fc90 (the stealth reset), UpdatePedestrianInfo, then the base pass with the
	// bool passed through.
	FRunAi19Fixture F;
	if (!TestNotNull(TEXT("subject"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	RunAi19InstallQuietProgram(N);
	RunAi19SetOnePassConditions(N);
	N.Senses.StealthHearingDist = 300.f;
	N.Senses.StealthVisionScalar = 0.25f;
	N.Senses.StealthVisionCone = 0.5f;
	N.BaseScheduleHost.bRanAi = false;
	N.RunAI(true);
	TestEqual(TEXT("1028fc95 m_flStealthHearingDist = 0"), N.Senses.StealthHearingDist, 0.f);
	TestEqual(TEXT("1028fc9f m_flStealthVisionScalar = 1.0"), N.Senses.StealthVisionScalar, 1.f);
	TestEqual(TEXT("1028fca5 m_flStealthVisionCone = 1.0"), N.Senses.StealthVisionCone, 1.f);
	TestTrue(TEXT("1028fd3d the base pass ran (m_bRanAI)"), N.BaseScheduleHost.bRanAi);
	TestTrue(TEXT("1028fd3d with bReduced passed through: no gather"), N.Cognition.GatheredAt < 0.0);
	TestTrue(TEXT("1028fd3d with bReduced passed through: no end-of-pass clear"),
		N.Cognition.Conditions.Has(EElysiumNpcCond::LightDamage));

	N.RunAI(false);
	TestEqual(TEXT("1028fd3d a full pass gathers"), N.Cognition.GatheredAt, F.Now());
	TestFalse(TEXT("1028fd3d a full pass clears"),
		N.Cognition.Conditions.Has(EElysiumNpcCond::LightDamage));
	return true;
}

// =================================================================================================
// 0x1028fd80 CAI_BaseNPCTroika::RunAlternateAI
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunAi19RunAlternateAiGrappleTest,
	"Elysium.Substrate.NpcKernelRunAi19.RunAlternateAI.Grapple", GRunAi19Flags)
bool FElysiumNpcKernelRunAi19RunAlternateAiGrappleTest::RunTest(const FString&)
{
	// 0x1028fdf2..0x1028fe52: a live partner with role 1 answers TRUE whether or not the ideal
	// activity is one of the nine AutoMovement entries, and never reaches the mode switch.
	FRunAi19Fixture F;
	if (!TestNotNull(TEXT("subject"), F.Npc) || !TestNotNull(TEXT("other"), F.Other))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	N.Grapple.Partner = F.Other->Handle;
	N.Grapple.Role = EElysiumGrappleRole::Victim;
	N.AlternateAi = 3;
	N.AlternateAiExpireTime = F.Now() - 1.0;
	N.IdealActivityNumber = 0xf88;
	TestTrue(TEXT("1028fe42 an AutoMovement activity answers true"), N.RunAlternateAI(false));
	N.IdealActivityNumber = 0xf89;
	TestTrue(TEXT("1028fe47 an activity outside the table answers true too"), N.RunAlternateAI(false));
	N.IdealActivityNumber = 0x1039;
	TestTrue(TEXT("1028fe42 0x1039 is the last entry"), N.RunAlternateAI(true));
	TestEqual(TEXT("1028fe52 the mode switch was never reached"), N.AlternateAi, 3);

	// 0x1028fe1f: role 0 (the attacker) falls through to the mode switch.
	N.Grapple.Role = EElysiumGrappleRole::Attacker;
	TestTrue(TEXT("1028fe9a role 0 reaches mode 3"), N.RunAlternateAI(false));
	TestEqual(TEXT("10290305 mode 3 expired: mode cleared"), N.AlternateAi, 0);

	// 0x1028fdf2: no partner at all falls through as well.
	N.Grapple.Partner = FElysiumEntityHandle();
	N.Grapple.Role = EElysiumGrappleRole::Victim;
	TestFalse(TEXT("1028fec1 mode 0 answers false"), N.RunAlternateAI(false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunAi19RunAlternateAiModesTest,
	"Elysium.Substrate.NpcKernelRunAi19.RunAlternateAI.Modes", GRunAi19Flags)
bool FElysiumNpcKernelRunAi19RunAlternateAiModesTest::RunTest(const FString&)
{
	// 0x1028fe57..0x1028fecc: m_eAlternateAI 1..4 dispatch to their arms, 0 and > 4 answer false.
	FRunAi19Fixture F;
	if (!TestNotNull(TEXT("subject"), F.Npc) || !TestNotNull(TEXT("other"), F.Other))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	const double Now = F.Now();

	N.AlternateAi = 0;
	TestFalse(TEXT("1028fec1 mode 0: false"), N.RunAlternateAI(false));
	N.AlternateAi = 5;
	TestFalse(TEXT("1028fe60 JA: mode 5 answers false"), N.RunAlternateAI(false));
	TestEqual(TEXT("1028fe60 and writes nothing"), N.AlternateAi, 5);

	// Mode 1 (0x10290040): no door resolves -> mode reset and false.
	N.AlternateAi = 1;
	N.OpeningDoor = FElysiumEntityHandle();
	TestFalse(TEXT("1028fe6e mode 1 with a dead door answers false"), N.RunAlternateAI(false));
	TestEqual(TEXT("10290040 dead door resets the mode"), N.AlternateAi, 0);

	// Mode 4 (0x10290350): always true.
	N.AlternateAi = 4;
	N.AlternateAiExpireTime = Now + 100.0;
	TestTrue(TEXT("1028feb0 mode 4 answers true"), N.RunAlternateAI(false));
	TestEqual(TEXT("10290350 unexpired mode 4 keeps its mode"), N.AlternateAi, 4);

	// Mode 3 (0x102902e0), unexpired: MaintainActivity and nothing else.
	N.AlternateAi = 3;
	N.OpeningDoor = F.Other->Handle;
	N.bOpeningDoorWait = true;
	N.AlternateAiExpireTime = Now + 1.0;
	const int32 MaintainsBefore = N.MaintainActivityCalls;
	TestTrue(TEXT("1028fe9a mode 3 answers true"), N.RunAlternateAI(false));
	TestEqual(TEXT("102902e3 MaintainActivity"), N.MaintainActivityCalls, MaintainsBefore + 1);
	TestEqual(TEXT("102902fd unexpired keeps the mode"), N.AlternateAi, 3);
	TestTrue(TEXT("102902fd unexpired keeps the door"), N.OpeningDoor.IsSet());

	// Mode 3, expiry EQUAL to curtime: the else arm fires.
	N.AlternateAiExpireTime = Now;
	N.Cognition.Conditions.Clear(EElysiumNpcCond::TaskFailed);
	TestTrue(TEXT("10290323 mode 3 expired answers true"), N.RunAlternateAI(false));
	TestTrue(TEXT("10290305 TaskFail sets TASK_FAILED"),
		N.Cognition.Conditions.Has(EElysiumNpcCond::TaskFailed));
	TestEqual(TEXT("10290305 with 0xe"), N.BaseScheduleHost.FailureReason, 0xe);
	TestFalse(TEXT("1029030d m_hOpeningDoor = -1"), N.OpeningDoor.IsSet());
	TestFalse(TEXT("10290317 m_bOpeningDoorWait = 0"), N.bOpeningDoorWait);
	TestEqual(TEXT("1029031d m_eAlternateAI = 0"), N.AlternateAi, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunAi19RunAlternateAiMode2Test,
	"Elysium.Substrate.NpcKernelRunAi19.RunAlternateAI.Mode2", GRunAi19Flags)
bool FElysiumNpcKernelRunAi19RunAlternateAiMode2Test::RunTest(const FString&)
{
	// 0x10290200, reached through 0x1028fe84.
	FRunAi19Fixture F;
	if (!TestNotNull(TEXT("subject"), F.Npc) || !TestNotNull(TEXT("other"), F.Other))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	const double Now = F.Now();

	// Unexpired: MaintainActivity, then nothing.
	N.AlternateAi = 2;
	N.OpeningDoor = F.Other->Handle;
	N.bOpeningDoorWait = true;
	N.AlternateAiExpireTime = Now + 1.0;
	const int32 MaintainsBefore = N.MaintainActivityCalls;
	TestTrue(TEXT("1028fe84 mode 2 answers true"), N.RunAlternateAI(false));
	TestEqual(TEXT("10290203 MaintainActivity"), N.MaintainActivityCalls, MaintainsBefore + 1);
	TestEqual(TEXT("1029021d unexpired keeps the mode"), N.AlternateAi, 2);

	// Expired with the door still live: TaskFail(0xe) and the three clears.
	N.AlternateAiExpireTime = Now;
	N.Cognition.Conditions.Clear(EElysiumNpcCond::TaskFailed);
	TestTrue(TEXT("10290277 answers true"), N.RunAlternateAI(false));
	TestTrue(TEXT("10290256 slot 448"), N.Cognition.Conditions.Has(EElysiumNpcCond::TaskFailed));
	TestEqual(TEXT("10290252 reason 0xe"), N.BaseScheduleHost.FailureReason, 0xe);
	TestFalse(TEXT("1029025c door -1"), N.OpeningDoor.IsSet());
	TestFalse(TEXT("10290266 wait cleared"), N.bOpeningDoorWait);
	TestEqual(TEXT("1029026d mode 0"), N.AlternateAi, 0);

	// Expired with no door: no TaskFail, the re-plan arm, then mode 0 -- the wait flag is untouched.
	N.AlternateAi = 2;
	N.bOpeningDoorWait = true;
	N.BaseScheduleHost.FailureReason = 0;
	N.Cognition.Conditions.Clear(EElysiumNpcCond::TaskFailed);
	TestTrue(TEXT("102902a7 answers true"), N.RunAlternateAI(false));
	TestFalse(TEXT("1029022c a dead door does not fail the task"),
		N.Cognition.Conditions.Has(EElysiumNpcCond::TaskFailed));
	TestEqual(TEXT("1029029d mode 0"), N.AlternateAi, 0);
	TestTrue(TEXT("1029029d the wait flag is not this arm's"), N.bOpeningDoorWait);
	return true;
}

namespace
{
	// Installs the quiet program and runs two full `MaintainSchedule` passes at the same clock, so its
	// opening tasks (the activity set among them) are done and a later reduced pass only continues
	// the wait the program parks on: what a species body writes before or after the base pass is not
	// overwritten by a task starting.
	void RunAi19Settle(FElysiumNpc& N, double Now)
	{
		RunAi19InstallQuietProgram(N);
		N.MaintainSchedule(Now, false);
		N.MaintainSchedule(Now, false);
	}

	// Advances the NPC schedule stream until its NEXT `RandRange(Min, Max)` answers `Wanted` (or, with
	// `bEqual` false, anything but `Wanted`). A copy of the stream is the peek.
	bool RunAi19PrimeRoll(int32 Min, int32 Max, int32 Wanted, bool bEqual)
	{
		FRandomStream& Stream = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule);
		for (int32 Attempt = 0; Attempt < 100000; ++Attempt)
		{
			FRandomStream Peek = Stream;
			if ((Peek.RandRange(Min, Max) == Wanted) == bEqual)
			{
				return true;
			}
			Stream.RandRange(Min, Max);
		}
		return false;
	}
}

// =================================================================================================
// 0x1035e980 CNPC_VAndreiBlood
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunAi19AndreiBloodTest,
	"Elysium.Substrate.NpcKernelRunAi19.AndreiBlood", GRunAi19Flags)
bool FElysiumNpcKernelRunAi19AndreiBloodTest::RunTest(const FString&)
{
	FRunAi19Fixture F(TEXT("CNPC_VAndreiBlood"));
	FElysiumNpcAndreiBlood* N = ElysiumTestAsSpecies<FElysiumNpcAndreiBlood>(F.Npc);
	if (!TestNotNull(TEXT("CNPC_VAndreiBlood"), N) || !TestNotNull(TEXT("player"), F.Player))
	{
		return false;
	}
	RunAi19Settle(*N, F.Now());

	// Not activated: forced IDLE on both words, the player relationship to D_NU.
	N->bAndreiActivated = false;
	N->WriteNpcStateRetail(2);
	N->WriteIdealStateRetail(2);
	N->Senses.Memory.ClosestPlayer = F.Player->Handle;
	N->RunAI(true);
	TestEqual(TEXT("1035e98d m_NPCState = 1"), N->NpcStateRetail(), 1);
	TestEqual(TEXT("1035e997 m_IdealNPCState = 1"), N->IdealStateRetail(), 1);
	TestEqual(TEXT("1035e9e7 AddClassRelationship(player, D_NU, 10): slot 404 now answers 4"),
		N->IRelationType(F.Player), 4);
	TestTrue(TEXT("1035ea01 the base pass ran"), N->BaseScheduleHost.bRanAi);

	// Activated: both words COMBAT.
	N->bAndreiActivated = true;
	N->BaseScheduleHost.bRanAi = false;
	N->RunAI(true);
	TestEqual(TEXT("1035ea16 m_NPCState = 2"), N->NpcStateRetail(), 2);
	TestEqual(TEXT("1035ea1c m_IdealNPCState = 2"), N->IdealStateRetail(), 2);
	TestTrue(TEXT("1035ea22 the base pass ran"), N->BaseScheduleHost.bRanAi);
	return true;
}

// =================================================================================================
// 0x10361110 CNPC_VAsianVampire
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunAi19AsianVampireTest,
	"Elysium.Substrate.NpcKernelRunAi19.AsianVampire", GRunAi19Flags)
bool FElysiumNpcKernelRunAi19AsianVampireTest::RunTest(const FString&)
{
	FRunAi19Fixture F(TEXT("CNPC_VAsianVampire"));
	FElysiumNpcAsianVampire* N = ElysiumTestAsSpecies<FElysiumNpcAsianVampire>(F.Npc);
	if (!TestNotNull(TEXT("CNPC_VAsianVampire"), N))
	{
		return false;
	}
	RunAi19Settle(*N, F.Now());
	N->MovedPosition = FVector(5000.0, 5000.0, 5000.0);
	N->MovedTimeStamp = -9.0;
	N->BaseScheduleHost.bRanAi = false;
	N->RunAI(true);
	TestEqual(TEXT("10361163 UpdateMovedTimeStamp stamped curtime (moved > 40)"), N->MovedTimeStamp,
		F.Now());
	TestFalse(TEXT("10361163 and re-took the position"),
		N->MovedPosition.Equals(FVector(5000.0, 5000.0, 5000.0)));
	TestTrue(TEXT("1036116f the base pass ran"), N->BaseScheduleHost.bRanAi);
	return true;
}

// =================================================================================================
// 0x10363b60 CNPC_VBach
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunAi19BachTest,
	"Elysium.Substrate.NpcKernelRunAi19.Bach", GRunAi19Flags)
bool FElysiumNpcKernelRunAi19BachTest::RunTest(const FString&)
{
	FRunAi19Fixture F(TEXT("CNPC_VBach"));
	FElysiumNpcBach* N = ElysiumTestAsSpecies<FElysiumNpcBach>(F.Npc);
	if (!TestNotNull(TEXT("CNPC_VBach"), N) || !TestNotNull(TEXT("other"), F.Other))
	{
		return false;
	}
	RunAi19Settle(*N, F.Now());
	const double Now = F.Now();

	// m_bCanFightYet clear: SetEnemy(NULL) every pass.
	N->bCanFightYet = false;
	ElysiumNpcEnemy::SetEnemy(*N, F.Other->Handle);
	TestNotNull(TEXT("the enemy stands before the pass"), N->GetEnemy());
	N->RunAI(true);
	TestNull(TEXT("10363b6f SetEnemy(NULL) while m_bCanFightYet is 0"), N->GetEnemy());

	// m_bCanFightYet set: the enemy is kept.
	N->bCanFightYet = true;
	ElysiumNpcEnemy::SetEnemy(*N, F.Other->Handle);
	N->RunAI(true);
	TestNotNull(TEXT("10363b6b m_bCanFightYet set keeps the enemy"), N->GetEnemy());

	// The shield: an equal stamp has not expired (strictly before curtime only).
	N->bBachShieldActive = true;
	N->BachShieldTime = Now;
	N->TypedStatWrites.Reset();
	N->RunAI(true);
	TestTrue(TEXT("10363b97 m_flShieldTime == curtime: still active"), N->bBachShieldActive);
	TestEqual(TEXT("10363b97 and no stat write"), N->TypedStatWrites.Num(), 0);

	N->BachShieldTime = Now - 1.0;
	N->RunAI(true);
	TestFalse(TEXT("10363bfd m_bShieldActive = 0"), N->bBachShieldActive);
	if (TestEqual(TEXT("10363bf7 one SetBase"), N->TypedStatWrites.Num(), 1))
	{
		TestEqual(TEXT("10363bb1 on the type-3 list"), N->TypedStatWrites[0].ListType, 3);
		TestEqual(TEXT("10363bf0 stat 0xd"), N->TypedStatWrites[0].StatId, 0xd);
		TestEqual(TEXT("10363bf7 to 0"), N->TypedStatWrites[0].Value, 0);
	}

	// Inactive shield: nothing.
	N->TypedStatWrites.Reset();
	N->RunAI(true);
	TestEqual(TEXT("10363b7c inactive shield: no write"), N->TypedStatWrites.Num(), 0);
	return true;
}

// =================================================================================================
// 0x103747e0 CNPC_VDog
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunAi19DogTest,
	"Elysium.Substrate.NpcKernelRunAi19.Dog", GRunAi19Flags)
bool FElysiumNpcKernelRunAi19DogTest::RunTest(const FString&)
{
	FRunAi19Fixture F(TEXT("CNPC_VDog"));
	FElysiumNpcDog* N = ElysiumTestAsSpecies<FElysiumNpcDog>(F.Npc);
	if (!TestNotNull(TEXT("CNPC_VDog"), N))
	{
		return false;
	}
	RunAi19Settle(*N, F.Now());

	// ACT_SNARL with the sequence finished: SetActivity(ACT_IDLE).
	N->ActivityNumber = 0x6e;
	N->IdealActivityNumber = 0x6e;
	N->bSequenceFinished = true;
	N->RunAI(true);
	TestEqual(TEXT("10374843 ACT_SNARL finished -> SetActivity(ACT_IDLE)"), N->IdealActivityNumber, 1);

	// ACT_SIT unfinished: nothing.
	N->ActivityNumber = 0x40;
	N->IdealActivityNumber = 0x40;
	N->bSequenceFinished = false;
	N->RunAI(true);
	TestEqual(TEXT("10374866 ACT_SIT unfinished: no SetActivity"), N->IdealActivityNumber, 0x40);

	// ACT_FIDGET finished: SetActivity(ACT_IDLE).
	N->ActivityNumber = 3;
	N->IdealActivityNumber = 3;
	N->bSequenceFinished = true;
	N->RunAI(true);
	TestEqual(TEXT("1037486e ACT_FIDGET finished -> SetActivity(ACT_IDLE)"), N->IdealActivityNumber, 1);

	// ACT_IDLE in NPC_STATE_IDLE: the exact roll 0x80 fidgets.
	N->WriteNpcStateRetail(1);
	N->ActivityNumber = 1;
	N->IdealActivityNumber = 1;
	N->bSequenceFinished = false;
	TestTrue(TEXT("a 0x80 roll is primed"), RunAi19PrimeRoll(0, 0xff, 0x80, true));
	N->RunAI(true);
	TestEqual(TEXT("10374813 RandomInt(0,255) == 0x80 -> SetActivity(ACT_FIDGET)"),
		N->IdealActivityNumber, 3);

	// Any other roll does not.
	N->ActivityNumber = 1;
	N->IdealActivityNumber = 1;
	TestTrue(TEXT("a non-0x80 roll is primed"), RunAi19PrimeRoll(0, 0xff, 0x80, false));
	N->RunAI(true);
	TestEqual(TEXT("1037480b any other roll: no fidget (equality, not a threshold)"),
		N->IdealActivityNumber, 1);

	// ACT_IDLE outside NPC_STATE_IDLE: no roll arm at all, even with 0x80 next.
	N->WriteNpcStateRetail(2);
	N->ActivityNumber = 1;
	N->IdealActivityNumber = 1;
	TestTrue(TEXT("a 0x80 roll is primed again"), RunAi19PrimeRoll(0, 0xff, 0x80, true));
	N->RunAI(true);
	TestEqual(TEXT("103747f4 not IDLE: no fidget"), N->IdealActivityNumber, 1);
	return true;
}

// =================================================================================================
// 0x10378b80 CNPC_VGargoyle
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunAi19GargoyleTest,
	"Elysium.Substrate.NpcKernelRunAi19.Gargoyle", GRunAi19Flags)
bool FElysiumNpcKernelRunAi19GargoyleTest::RunTest(const FString&)
{
	FRunAi19Fixture F(TEXT("CNPC_VGargoyle"));
	FElysiumNpcGargoyle* N = ElysiumTestAsSpecies<FElysiumNpcGargoyle>(F.Npc);
	if (!TestNotNull(TEXT("CNPC_VGargoyle"), N) || !TestNotNull(TEXT("other"), F.Other))
	{
		return false;
	}
	RunAi19Settle(*N, F.Now());

	// The pass: the sweep asks the hull trace (the family Motor seam answers clear), so no blocker,
	// no reaction, and the base runs.
	const int32 TracesBefore = N->MotorSeams.HullTraces;
	N->BaseScheduleHost.bRanAi = false;
	N->RunAI(true);
	TestTrue(TEXT("10378b89 0x103796a0 traced"), N->MotorSeams.HullTraces > TracesBefore);
	TestEqual(TEXT("10378b90 a clear sweep reacts to nothing"), N->RunAi19BlockerPushes, 0);
	TestTrue(TEXT("10378bc6 the base pass ran"), N->BaseScheduleHost.bRanAi);

	// The reaction arms, driven with a real blocker.
	N->RunAi19GargoyleReact(nullptr, 0);
	TestEqual(TEXT("10378b90 null blocker: nothing"), N->RunAi19BlockerPushes, 0);

	const FVector Before = N->Origin;
	N->RunAi19GargoyleReact(F.Other, 1);
	TestTrue(TEXT("10378ba1 kind 1: 0x10379e80 raises the origin by StepHeight (18 units)"),
		FMath::IsNearlyEqual(N->Origin.Z - Before.Z, 18.0 * ElysiumMove::U, 0.01));
	N->SetOrigin(Before);

	F.Other->bHasPhysicsObject = false;
	N->RunAi19GargoyleReact(F.Other, 0);
	TestEqual(TEXT("10379be6 no m_pPhysicsObject: no push"), N->RunAi19BlockerPushes, 0);
	TestEqual(TEXT("10379bd4 not a prop_dynamic: no break"), N->RunAi19GargoylePropBreaks, 0);

	F.Other->bHasPhysicsObject = true;
	N->RunAi19GargoyleReact(F.Other, 0);
	TestEqual(TEXT("10379db5 AddVelocity on the blocker"), N->RunAi19BlockerPushes, 1);
	TestTrue(TEXT("10379db5 zero own velocity: (0,0,gargoyle_obstruction_z 75)"),
		N->RunAi19LastBlockerPushUnits.Equals(FVector(0.0, 0.0, 75.0)));
	return true;
}

// =================================================================================================
// 0x10380120 CNPC_VHengeyokai
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunAi19HengeyokaiTest,
	"Elysium.Substrate.NpcKernelRunAi19.Hengeyokai", GRunAi19Flags)
bool FElysiumNpcKernelRunAi19HengeyokaiTest::RunTest(const FString&)
{
	FRunAi19Fixture F(TEXT("CNPC_VHengeyokai"));
	FElysiumNpcHengeyokai* N = ElysiumTestAsSpecies<FElysiumNpcHengeyokai>(F.Npc);
	if (!TestNotNull(TEXT("CNPC_VHengeyokai"), N) || !TestNotNull(TEXT("other"), F.Other))
	{
		return false;
	}
	RunAi19Settle(*N, F.Now());

	// The stun ConVar off: no morph.
	FElysiumNpcHengeyokai::HengeyokaiStunConVar() = 0;
	N->Skin = 0;
	const int32 TracesBefore = N->MotorSeams.HullTraces;
	N->RunAI(true);
	TestTrue(TEXT("10380129 0x10380fc0 traced"), N->MotorSeams.HullTraces > TracesBefore);
	TestEqual(TEXT("10380169 hengeyokai_stun 0: no morph"), N->Skin, 0);

	// On: the morph, then the ConVar back to 0 -- one shot.
	FElysiumNpcHengeyokai::HengeyokaiStunConVar() = 1;
	N->RunAI(true);
	TestEqual(TEXT("10380179 0x103830e0 FadeToSkin(1)"), N->Skin, 1);
	TestEqual(TEXT("10380185 ConVar::SetValue(0)"), FElysiumNpcHengeyokai::HengeyokaiStunConVar(), 0);

	// The reaction arms.
	const FVector Before = N->Origin;
	N->RunAi19HengeyokaiReact(F.Other, 1);
	TestTrue(TEXT("10380141 kind 1: 0x103816e0 steps up 18 units"),
		FMath::IsNearlyEqual(N->Origin.Z - Before.Z, 18.0 * ElysiumMove::U, 0.01));
	N->SetOrigin(Before);
	N->RunAi19HengeyokaiReact(nullptr, 1);
	TestTrue(TEXT("10380130 null blocker: nothing"), N->Origin.Equals(Before));
	F.Other->bHasPhysicsObject = true;
	N->RunAi19HengeyokaiReact(F.Other, 0);
	TestEqual(TEXT("1038014b kind 0: 0x10381460 pushes"), N->RunAi19BlockerPushes, 1);
	TestTrue(TEXT("10381644 (0,0,hengeyokai_obstruction_z 75)"),
		N->RunAi19LastBlockerPushUnits.Equals(FVector(0.0, 0.0, 75.0)));
	return true;
}

// =================================================================================================
// 0x1038e990 CNPC_VManBat
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunAi19ManBatTest,
	"Elysium.Substrate.NpcKernelRunAi19.ManBat", GRunAi19Flags)
bool FElysiumNpcKernelRunAi19ManBatTest::RunTest(const FString&)
{
	FRunAi19Fixture F(TEXT("CNPC_VManBat"));
	FElysiumNpcManBat* N = ElysiumTestAsSpecies<FElysiumNpcManBat>(F.Npc);
	if (!TestNotNull(TEXT("CNPC_VManBat"), N))
	{
		return false;
	}
	RunAi19Settle(*N, F.Now());
	F.World.Advance(1.0);
	const float Now = static_cast<float>(F.Now());
	// A slow still in the future is left alone (force false).
	N->ManBatSlowedExpire = Now + 5.f;
	N->RunAI(true);
	TestEqual(TEXT("1038e995 force 0: an unexpired slow stands"), N->ManBatSlowedExpire, Now + 5.f);
	// An expired one (above 0.0, at or before curtime) is released.
	N->ManBatSlowedExpire = Now - 0.5f;
	N->BaseScheduleHost.bRanAi = false;
	N->RunAI(true);
	TestEqual(TEXT("1038e995 an expired slow is released: m_flSlowedExpire = 0"),
		N->ManBatSlowedExpire, 0.f);
	TestTrue(TEXT("1038e9a1 the base pass ran"), N->BaseScheduleHost.bRanAi);
	return true;
}

// =================================================================================================
// 0x1039e3d0 CNPC_VMingXiaoTentacle
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunAi19MingXiaoTentacleTest,
	"Elysium.Substrate.NpcKernelRunAi19.MingXiaoTentacle", GRunAi19Flags)
bool FElysiumNpcKernelRunAi19MingXiaoTentacleTest::RunTest(const FString&)
{
	FRunAi19Fixture F(TEXT("CNPC_VMingXiaoTentacle"));
	FElysiumNpcMingXiaoTentacle* N = ElysiumTestAsSpecies<FElysiumNpcMingXiaoTentacle>(F.Npc);
	if (!TestNotNull(TEXT("CNPC_VMingXiaoTentacle"), N))
	{
		return false;
	}
	RunAi19Settle(*N, F.Now());
	const double Now = F.Now();

	// (1) The evade timer: an equal stamp does not run; a past one re-arms to curtime + [1, 2].
	N->TentacleUpdateEvadeTimer = Now;
	N->bIgnoreCollisionSpecies = false;
	N->bTentacleHitGroundSound = true;
	N->RunAI(true);
	TestEqual(TEXT("1039e4a6 timer == curtime: not re-armed"), N->TentacleUpdateEvadeTimer, Now);
	N->TentacleUpdateEvadeTimer = Now - 0.5;
	N->Cognition.Conditions.Clear(EElysiumNpcCond::TaskFailed);
	N->RunAI(true);
	TestTrue(TEXT("1039e4cf re-armed to curtime + RandomFloat(1, 2)"),
		N->TentacleUpdateEvadeTimer >= Now + 1.0 && N->TentacleUpdateEvadeTimer <= Now + 2.0);
	// The navigator's goal-type word is a seam answering -1, not GOALTYPE_LOCATION: no task fails.
	TestFalse(TEXT("1039e4f0 goal type != 4: the evade arm stops, no TaskFail"),
		N->Cognition.Conditions.Has(EElysiumNpcCond::TaskFailed));

	// (2) The collision release: an unexpired timer keeps the flag.
	N->bIgnoreCollisionSpecies = true;
	N->TentacleIgnoreCollisionTimer = Now + 1.0;
	N->RunAI(true);
	TestTrue(TEXT("1039e5bc curtime < timer: still ignoring"), N->bIgnoreCollisionSpecies);
	// Expired (equal): cleared, and IsAreaClear (the hull-trace seam answers clear) keeps it cleared.
	N->TentacleIgnoreCollisionTimer = Now;
	N->RunAI(true);
	TestFalse(TEXT("1039e5cb cleared; 1039e5e2 a clear area leaves it cleared"),
		N->bIgnoreCollisionSpecies);

	// (3) The landing sounds: once, only with a ground entity.
	N->bTentacleHitGroundSound = false;
	const bool bGrounded = N->GetGroundEntity() != nullptr;
	N->RunAI(true);
	TestEqual(TEXT("1039e615 / 1039e625 m_bHitGroundSound follows GetGroundEntity"),
		N->bTentacleHitGroundSound, bGrounded);
	N->bTentacleHitGroundSound = true;
	N->BaseScheduleHost.bRanAi = false;
	N->RunAI(true);
	TestTrue(TEXT("1039e607 already played: stays set"), N->bTentacleHitGroundSound);
	TestTrue(TEXT("1039e633 the Troika base ran"), N->BaseScheduleHost.bRanAi);
	return true;
}

// =================================================================================================
// 0x103a3670 CNPC_VPedestrian
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunAi19PedestrianTest,
	"Elysium.Substrate.NpcKernelRunAi19.Pedestrian", GRunAi19Flags)
bool FElysiumNpcKernelRunAi19PedestrianTest::RunTest(const FString&)
{
	FRunAi19Fixture F(TEXT("CNPC_VPedestrian"));
	FElysiumNpcPedestrian* N = ElysiumTestAsSpecies<FElysiumNpcPedestrian>(F.Npc);
	if (!TestNotNull(TEXT("CNPC_VPedestrian"), N))
	{
		return false;
	}
	RunAi19Settle(*N, F.Now());
	const double Now = F.Now();

	// Not fleeing: nothing.
	N->NpcFlags.Clear(EElysiumNpcFlag::IN_FLEE_SCHED);
	N->Senses.Memory.NextFleeSoundTime = Now - 1.0;
	N->RunAI(true);
	TestEqual(TEXT("103a368f no 0x80 flag: the stamp is untouched"), N->Senses.Memory.NextFleeSoundTime,
		Now - 1.0);

	// Fleeing but not due.
	N->NpcFlags.Set(EElysiumNpcFlag::IN_FLEE_SCHED);
	N->Senses.Memory.NextFleeSoundTime = Now + 1.0;
	N->RunAI(true);
	TestEqual(TEXT("103a36ab curtime < m_flNextFleeSoundTime: untouched"),
		N->Senses.Memory.NextFleeSoundTime, Now + 1.0);

	// Fleeing and due (equal passes): re-stamped curtime + 10.
	N->Senses.Memory.NextFleeSoundTime = Now;
	N->BaseScheduleHost.bRanAi = false;
	N->RunAI(true);
	TestEqual(TEXT("103a36c7 m_flNextFleeSoundTime = curtime + 10.0"), N->Senses.Memory.NextFleeSoundTime,
		Now + 10.0);
	TestTrue(TEXT("103a367b the base pass ran first"), N->BaseScheduleHost.bRanAi);
	return true;
}

// =================================================================================================
// 0x103a75c0 CNPC_VSabbatLeader
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunAi19SabbatLeaderTest,
	"Elysium.Substrate.NpcKernelRunAi19.SabbatLeader", GRunAi19Flags)
bool FElysiumNpcKernelRunAi19SabbatLeaderTest::RunTest(const FString&)
{
	FRunAi19Fixture F(TEXT("CNPC_VSabbatLeader"));
	FElysiumNpcSabbatLeader* N = ElysiumTestAsSpecies<FElysiumNpcSabbatLeader>(F.Npc);
	if (!TestNotNull(TEXT("CNPC_VSabbatLeader"), N))
	{
		return false;
	}
	RunAi19Settle(*N, F.Now());
	N->bSabbatDiving = false;
	N->WaterLevel = 2;
	N->SabbatLastWaterLevel = 0;
	N->SabbatLastSplashTime = -100.0;
	N->BaseScheduleHost.bRanAi = false;
	N->RunAI(true);
	TestTrue(TEXT("103a7618 the base pass ran"), N->BaseScheduleHost.bRanAi);
	TestEqual(TEXT("103a761f UpdateBloodSplash stamped the splash"), N->SabbatLastSplashTime, F.Now());
	TestEqual(TEXT("103a761f and copied the level"), N->SabbatLastWaterLevel, 2);
	return true;
}

// =================================================================================================
// 0x103aebd0 CNPC_VSheriffMan
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunAi19SheriffManTest,
	"Elysium.Substrate.NpcKernelRunAi19.SheriffMan", GRunAi19Flags)
bool FElysiumNpcKernelRunAi19SheriffManTest::RunTest(const FString&)
{
	FRunAi19Fixture F(TEXT("CNPC_VSheriffMan"));
	FElysiumNpcSheriffMan* N = ElysiumTestAsSpecies<FElysiumNpcSheriffMan>(F.Npc);
	if (!TestNotNull(TEXT("CNPC_VSheriffMan"), N))
	{
		return false;
	}
	RunAi19Settle(*N, F.Now());
	// Not cached: CacheFloorHeights runs; the fixture authors no centre node, so it stores nothing.
	N->bSheriffLedgeHeightStored = false;
	N->SheriffCenterFloorZ = 123.f;
	N->RunAI(true);
	TestFalse(TEXT("103aec2d no centre node: the latch stays clear"), N->bSheriffLedgeHeightStored);
	TestEqual(TEXT("103aec2d and no centre height"), N->SheriffCenterFloorZ, 123.f);
	// Cached: skipped, the latch stands.
	N->bSheriffLedgeHeightStored = true;
	N->BaseScheduleHost.bRanAi = false;
	N->RunAI(true);
	TestTrue(TEXT("103aec29 cached: skipped"), N->bSheriffLedgeHeightStored);
	TestTrue(TEXT("103aec39 the base pass ran"), N->BaseScheduleHost.bRanAi);
	return true;
}

// =================================================================================================
// 0x103bdef0 CNPC_VTzimisce
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunAi19TzimisceTest,
	"Elysium.Substrate.NpcKernelRunAi19.Tzimisce", GRunAi19Flags)
bool FElysiumNpcKernelRunAi19TzimisceTest::RunTest(const FString&)
{
	FRunAi19Fixture F(TEXT("CNPC_VTzimisce"));
	FElysiumNpcTzimisce* N = ElysiumTestAsSpecies<FElysiumNpcTzimisce>(F.Npc);
	if (!TestNotNull(TEXT("CNPC_VTzimisce"), N) || !TestNotNull(TEXT("other"), F.Other))
	{
		return false;
	}
	RunAi19Settle(*N, F.Now());

	const int32 TracesBefore = N->MotorSeams.HullTraces;
	N->BaseScheduleHost.bRanAi = false;
	N->RunAI(true);
	TestTrue(TEXT("103bdef9 0x103c0160 traced"), N->MotorSeams.HullTraces > TracesBefore);
	TestEqual(TEXT("103bdf00 a clear sweep: no reaction"), N->RunAi19BlockerPushes, 0);
	TestTrue(TEXT("103bdf36 the base pass ran"), N->BaseScheduleHost.bRanAi);

	// The sweep alone: a clear trace answers null and leaves the out-word as it was.
	int32 Kind = 7;
	TestNull(TEXT("103c0475 fraction 1.0: no blocker"),
		N->RunAi19ObstructionSweep(24.f, false, Kind));
	TestEqual(TEXT("103c0475 the out-word is not written"), Kind, 7);

	// The reaction arms.
	F.Other->bHasPhysicsObject = true;
	N->RunAi19TzimisceReact(nullptr, 0);
	TestEqual(TEXT("103bdf00 null blocker: nothing"), N->RunAi19BlockerPushes, 0);
	N->RunAi19TzimisceReact(F.Other, 0);
	TestEqual(TEXT("103bdf2a kind 0: 0x103c05e0 pushes"), N->RunAi19BlockerPushes, 1);
	TestTrue(TEXT("103c07c4 (0,0,tzimisce_obstruction_z 75)"),
		N->RunAi19LastBlockerPushUnits.Equals(FVector(0.0, 0.0, 75.0)));
	const FVector Before = N->Origin;
	N->RunAi19TzimisceReact(F.Other, 1);
	TestTrue(TEXT("103bdf11 kind 1 (dead in retail): 0x103c0860 steps up by StepHeight"),
		N->Origin.Z > Before.Z);
	return true;
}

// =================================================================================================
// 0x103c1d20 CNPC_VTzimisceHeadClaw
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunAi19TzimisceHeadClawTest,
	"Elysium.Substrate.NpcKernelRunAi19.TzimisceHeadClaw", GRunAi19Flags)
bool FElysiumNpcKernelRunAi19TzimisceHeadClawTest::RunTest(const FString&)
{
	FRunAi19Fixture F(TEXT("CNPC_VTzimisceHeadClaw"));
	FElysiumNpcTzimisceHeadClaw* N = ElysiumTestAsSpecies<FElysiumNpcTzimisceHeadClaw>(F.Npc);
	if (!TestNotNull(TEXT("CNPC_VTzimisceHeadClaw"), N))
	{
		return false;
	}
	RunAi19Settle(*N, F.Now());
	F.World.Advance(1.0);
	const double Now = F.Now();
	// Unexpired, force 0: stands.
	N->HeadClawSlowedExpire = Now + 5.0;
	N->RunAI(true);
	TestEqual(TEXT("103c1d25 force 0: an unexpired slow stands"), N->HeadClawSlowedExpire, Now + 5.0);
	// Expired: released.
	N->HeadClawSlowedExpire = Now - 0.5;
	N->BaseScheduleHost.bRanAi = false;
	N->RunAI(true);
	TestEqual(TEXT("103c1d25 an expired slow is released: m_flSlowedExpire = 0"),
		N->HeadClawSlowedExpire, 0.0);
	TestTrue(TEXT("103c1d31 the Troika base ran"), N->BaseScheduleHost.bRanAi);
	return true;
}

// =================================================================================================
// 0x103df850 CNPC_VZombie
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunAi19ZombieDespawnTest,
	"Elysium.Substrate.NpcKernelRunAi19.Zombie.Despawn", GRunAi19Flags)
bool FElysiumNpcKernelRunAi19ZombieDespawnTest::RunTest(const FString&)
{
	FRunAi19Fixture F(TEXT("CNPC_VZombie"));
	FElysiumNpcZombie* N = ElysiumTestAsSpecies<FElysiumNpcZombie>(F.Npc);
	if (!TestNotNull(TEXT("CNPC_VZombie"), N))
	{
		return false;
	}
	RunAi19Settle(*N, F.Now());
	N->ZombieRemoveDistUnits = 100.f;

	// Type 0: no despawn at any distance.
	N->ZombieAiType = 0;
	N->Senses.Memory.ClosestPlayerDistanceCm = 500.f * ElysiumMove::U;
	N->bGroundSpeedFromIntervalMovement = false;
	N->RunAI(true);
	TestFalse(TEXT("103df85a type 0: alive"), N->IsDead());
	TestTrue(TEXT("103df9b1 +0x05ac = 1 on every exit"), N->bGroundSpeedFromIntervalMovement);

	// Type 1, at the remove distance exactly: kept (strictly beyond only).
	N->ZombieAiType = 1;
	N->Senses.Memory.ClosestPlayerDistanceCm = 100.f * ElysiumMove::U;
	N->RunAI(true);
	TestFalse(TEXT("103df86f dist == remove distance: alive"), N->IsDead());

	// Type 1, beyond it: DevMsg and Kill, then the pass continues.
	N->Senses.Memory.ClosestPlayerDistanceCm = 101.f * ElysiumMove::U;
	N->bGroundSpeedFromIntervalMovement = false;
	N->RunAI(true);
	TestTrue(TEXT("103df89f slot 119 Kill"), N->IsDead());
	TestTrue(TEXT("103df9b1 the tail still ran after Kill"), N->bGroundSpeedFromIntervalMovement);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunAi19ZombieGrappleTest,
	"Elysium.Substrate.NpcKernelRunAi19.Zombie.Grapple", GRunAi19Flags)
bool FElysiumNpcKernelRunAi19ZombieGrappleTest::RunTest(const FString&)
{
	FRunAi19Fixture F(TEXT("CNPC_VZombie"));
	FElysiumNpcZombie* N = ElysiumTestAsSpecies<FElysiumNpcZombie>(F.Npc);
	if (!TestNotNull(TEXT("CNPC_VZombie"), N) || !TestNotNull(TEXT("player"), F.Player))
	{
		return false;
	}
	RunAi19Settle(*N, F.Now());
	const double Now = F.Now();
	// Every gate open except the roll; the tuning record answers its image defaults (98 / 160 / 2 /
	// 20) in a headless world.
	auto Open = [N, &F, Now]()
	{
		N->ZombieAiType = 0;
		N->ZombieRemoveDistUnits = 1.0e6f;
		N->ActivityNumber = 1;
		N->Cognition.Conditions.Set(EElysiumNpcCond::SeeEnemy);
		N->Senses.Memory.ClosestPlayer = F.Player->Handle;
		N->Senses.Memory.ClosestPlayerDistanceCm = 98.f * ElysiumMove::U;   // min is inclusive
		N->ZombieGrappleReadyTimer = Now;                                  // an equal stamp is due
	};

	// Each closed gate leaves the timer alone.
	Open();
	N->ZombieAiType = 2;
	N->RunAI(true);
	TestEqual(TEXT("103df8be type 2: no roll"), N->ZombieGrappleReadyTimer, Now);
	Open();
	N->ActivityNumber = 0x4b;
	N->RunAI(true);
	TestEqual(TEXT("103df8cb activity 0x4b: no roll"), N->ZombieGrappleReadyTimer, Now);
	Open();
	N->Cognition.Conditions.Clear(EElysiumNpcCond::SeeEnemy);
	N->RunAI(true);
	TestEqual(TEXT("103df8dc no COND 0x46: no roll"), N->ZombieGrappleReadyTimer, Now);
	Open();
	N->Senses.Memory.ClosestPlayerDistanceCm = 97.f * ElysiumMove::U;
	N->RunAI(true);
	TestEqual(TEXT("103df8f7 below LungeDistanceMin: no roll"), N->ZombieGrappleReadyTimer, Now);
	Open();
	N->Senses.Memory.ClosestPlayerDistanceCm = 161.f * ElysiumMove::U;
	N->RunAI(true);
	TestEqual(TEXT("103df914 above LungeDistanceMax: no roll"), N->ZombieGrappleReadyTimer, Now);
	Open();
	N->ZombieGrappleReadyTimer = Now + 1.0;
	N->RunAI(true);
	TestEqual(TEXT("103df930 curtime < m_flGrappleReadyTimer: no roll"), N->ZombieGrappleReadyTimer,
		Now + 1.0);
	Open();
	N->Senses.Memory.ClosestPlayer = F.Npc->Handle;   // resolves, but is not the player
	N->RunAI(true);
	TestEqual(TEXT("103df967 the closest is not a player (+0xa8): no roll"), N->ZombieGrappleReadyTimer,
		Now);

	// The roll: RandomInt(0, 99) strictly below Percent (2). The stream decides each pass, so the
	// pass is repeated with every gate re-opened until one roll lands; the stamp is the witness.
	bool bRolled = false;
	for (int32 Pass = 0; Pass < 2000 && !bRolled; ++Pass)
	{
		Open();
		N->RunAI(true);
		bRolled = N->ZombieGrappleReadyTimer != Now;
	}
	TestTrue(TEXT("103df988 a roll below Percent fired within 2000 passes"), bRolled);
	TestEqual(TEXT("103df9ab m_flGrappleReadyTimer = curtime + DelayBetween (20)"),
		N->ZombieGrappleReadyTimer, Now + 20.0);
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
