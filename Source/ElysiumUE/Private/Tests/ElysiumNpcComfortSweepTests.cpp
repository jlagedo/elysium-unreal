// The comfort sweep, `CAI_BaseNPCTroika::FUN_102b1a20` (`ElysiumNpcCond::GatherComfort`): the clock
// and its re-arm ahead of the idle test, the nearest-at-or-within-1024-units search over the global
// comfort-target array (story 8) excluding self, the busy/installed-schedule eligibility gates, the
// per-comforter cap of three with no fallback, and the `m_hTargetEnt` write.
//
// `docs/vtmb/npc-ai-reverse-engineering.md` -> "The comfort sweep `0x102b1a20`, walked" owns every
// fact here.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumAiScriptedSchedule.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumSchedule.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumTestServices.h"

namespace ElysiumNpcComfortSweepTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

using ECond = EElysiumNpcCond;
using EFlag = EElysiumNpcFlag;

namespace
{
	// The sweep reads the mind's state, one clock, the global comfort-target array and one flag; it
	// never touches a body, so nothing here needs a model.
	struct FSweepFixture
	{
		FElysiumNpcWorldFixture Fixture;
		FElysiumEntityWorld& World;
		FElysiumNpc* Npc = nullptr;
		FElysiumNpc* Near = nullptr;   // inside ComfortRangeUnits
		FElysiumNpc* Far = nullptr;    // exactly at the boundary

		static FElysiumNpcWorldBuilder BuildWorld()
		{
			FElysiumNpcWorldBuilder Builder(TEXT("__comfort_sweep_test__"), 0x434f4d46);
			for (const TCHAR* Name : { TEXT("npc"), TEXT("near"), TEXT("far") })
			{
				Builder.AddNpc(Name);
			}
			return Builder;
		}

		FSweepFixture()
			: Fixture(BuildWorld())
			, World(Fixture.World)
		{
			Npc = Fixture.Npc(TEXT("npc"));
			Near = Fixture.Npc(TEXT("near"));
			Far = Fixture.Npc(TEXT("far"));
			if (Npc != nullptr)
			{
				Npc->Origin = FVector::ZeroVector;
				Npc->NextComfortCheckTime = 0.0;
				// A running schedule so the eligibility gate passes by default.
				ElysiumSchedule::Start(Npc->Schedule, EElysiumScheduleId::IdleDisposition, *Npc);
			}
			if (Near != nullptr)
			{
				Near->Origin = FVector(500.0, 0.0, 0.0);
			}
			if (Far != nullptr)
			{
				Far->Origin = FVector(1024.0, 0.0, 0.0);
			}
		}

		FElysiumNpcConditions Sweep(double Now) const
		{
			FElysiumNpcConditions Out;
			ElysiumNpcCond::GatherComfort(*Npc, Now, Out);
			return Out;
		}
	};
}

// --- The clock, its re-arm, and the idle test behind them ------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcComfortSweepCadenceTest,
	"Elysium.Substrate.NpcConditions.ComfortSweepCadence", GElysiumTestFlags)
bool FElysiumNpcComfortSweepCadenceTest::RunTest(const FString&)
{
	FSweepFixture F;
	if (!TestNotNull(TEXT("npc"), F.Npc) || !TestNotNull(TEXT("near"), F.Near))
	{
		return false;
	}
	F.World.AddComfortTarget(F.Near->Handle);

	F.Npc->NextComfortCheckTime = 20.0;
	TestFalse(TEXT("not due yet raises nothing"), F.Sweep(10.0).Has(ECond::Comfort));
	TestTrue(TEXT("...and a miss leaves the clock untouched"),
		FMath::IsNearlyEqual(F.Npc->NextComfortCheckTime, 20.0));

	F.Npc->NextComfortCheckTime = 10.0;
	TestTrue(TEXT("due at curtime fires"), F.Sweep(10.0).Has(ECond::Comfort));
	TestTrue(TEXT("...and re-arms to curtime + [0.2, 0.4]"),
		F.Npc->NextComfortCheckTime >= 10.2 && F.Npc->NextComfortCheckTime <= 10.4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcComfortSweepNotIdleTest,
	"Elysium.Substrate.NpcConditions.ComfortSweepNotIdle", GElysiumTestFlags)
bool FElysiumNpcComfortSweepNotIdleTest::RunTest(const FString&)
{
	FSweepFixture F;
	if (!TestNotNull(TEXT("npc"), F.Npc) || !TestNotNull(TEXT("near"), F.Near))
	{
		return false;
	}
	F.World.AddComfortTarget(F.Near->Handle);

	F.Npc->BeginScriptedSchedule(FElysiumScriptedScheduleOrder(), true, EElysiumNpcState::Alert);
	if (!TestEqual(TEXT("the forced state put the NPC in alert"),
			F.Npc->GetMind().State(), EElysiumNpcState::Alert))
	{
		return false;
	}

	F.Npc->NextComfortCheckTime = 10.0;
	TestFalse(TEXT("a non-idle NPC raises nothing"), F.Sweep(10.0).Has(ECond::Comfort));
	TestTrue(TEXT("...but the due clock re-arms ahead of the idle test"),
		F.Npc->NextComfortCheckTime >= 10.2 && F.Npc->NextComfortCheckTime <= 10.4);
	TestEqual(TEXT("...and no comforter is credited"), F.Near->ComfortingCount, 0);
	TestFalse(TEXT("...and no target is written"), F.Npc->GetTarget().IsSet());
	return true;
}

// --- The nearest search: self excluded, 1024 inclusive, a tie to the later entry -------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcComfortSweepRangeTest,
	"Elysium.Substrate.NpcConditions.ComfortSweepRange", GElysiumTestFlags)
bool FElysiumNpcComfortSweepRangeTest::RunTest(const FString&)
{
	FSweepFixture F;
	if (!TestNotNull(TEXT("npc"), F.Npc) || !TestNotNull(TEXT("near"), F.Near)
		|| !TestNotNull(TEXT("far"), F.Far))
	{
		return false;
	}

	F.World.AddComfortTarget(F.Npc->Handle);
	TestFalse(TEXT("self is excluded"), F.Sweep(0.0).Has(ECond::Comfort));
	F.World.RemoveComfortTarget(F.Npc->Handle);
	F.Npc->NextComfortCheckTime = 0.0;

	F.Far->Origin = FVector(1024.001, 0.0, 0.0);
	F.World.AddComfortTarget(F.Far->Handle);
	TestFalse(TEXT("beyond 1024 units is never chosen"), F.Sweep(0.0).Has(ECond::Comfort));
	F.Npc->NextComfortCheckTime = 0.0;

	F.Far->Origin = FVector(1024.0, 0.0, 0.0);
	TestTrue(TEXT("exactly 1024 units qualifies"), F.Sweep(0.0).Has(ECond::Comfort));
	TestEqual(TEXT("...and is credited"), F.Far->ComfortingCount, 1);
	TestTrue(TEXT("...and becomes the target"), F.Npc->GetTarget() == F.Far->Handle);
	F.Npc->NextComfortCheckTime = 0.0;

	F.Near->Origin = FVector(0.0, 1024.0, 0.0);
	F.World.AddComfortTarget(F.Near->Handle);
	TestTrue(TEXT("a tie fires"), F.Sweep(0.0).Has(ECond::Comfort));
	TestEqual(TEXT("...crediting the later array entry"), F.Near->ComfortingCount, 1);
	TestEqual(TEXT("...not the earlier one"), F.Far->ComfortingCount, 1);
	TestTrue(TEXT("...which becomes the target"), F.Npc->GetTarget() == F.Near->Handle);
	return true;
}

// --- Eligibility: busy with a discipline, and no installed schedule --------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcComfortSweepEligibilityTest,
	"Elysium.Substrate.NpcConditions.ComfortSweepEligibility", GElysiumTestFlags)
bool FElysiumNpcComfortSweepEligibilityTest::RunTest(const FString&)
{
	FSweepFixture F;
	if (!TestNotNull(TEXT("npc"), F.Npc) || !TestNotNull(TEXT("near"), F.Near))
	{
		return false;
	}
	F.World.AddComfortTarget(F.Near->Handle);

	F.Npc->NpcFlags.Set(EFlag::D_IS_BUSY);
	TestFalse(TEXT("busy with a discipline is ineligible"), F.Sweep(0.0).Has(ECond::Comfort));
	F.Npc->NpcFlags.Clear(EFlag::D_IS_BUSY);
	F.Npc->NextComfortCheckTime = 0.0;

	F.Npc->Schedule.Clear();
	TestFalse(TEXT("no installed schedule is ineligible"), F.Sweep(0.0).Has(ECond::Comfort));
	F.Npc->NextComfortCheckTime = 0.0;

	ElysiumSchedule::Start(F.Npc->Schedule, EElysiumScheduleId::IdleDisposition, *F.Npc);
	TestTrue(TEXT("eligible once both gates are clear"), F.Sweep(0.0).Has(ECond::Comfort));
	return true;
}

// --- The per-comforter cap of three, with no fallback to the next-nearest ---------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcComfortSweepCapTest,
	"Elysium.Substrate.NpcConditions.ComfortSweepCap", GElysiumTestFlags)
bool FElysiumNpcComfortSweepCapTest::RunTest(const FString&)
{
	FSweepFixture F;
	if (!TestNotNull(TEXT("npc"), F.Npc) || !TestNotNull(TEXT("near"), F.Near)
		|| !TestNotNull(TEXT("far"), F.Far))
	{
		return false;
	}
	F.Far->Origin = FVector(600.0, 0.0, 0.0);
	F.World.AddComfortTarget(F.Near->Handle);
	F.World.AddComfortTarget(F.Far->Handle);
	F.Near->ComfortingCount = 3;

	TestFalse(TEXT("a nearest comforter already at the cap is skipped outright"),
		F.Sweep(0.0).Has(ECond::Comfort));
	TestEqual(TEXT("...with no fallback to the next-nearest"), F.Far->ComfortingCount, 0);
	TestEqual(TEXT("...and the capped comforter's count is untouched"), F.Near->ComfortingCount, 3);
	TestFalse(TEXT("...and no target is written"), F.Npc->GetTarget().IsSet());
	return true;
}

}   // namespace ElysiumNpcComfortSweepTests

#endif // WITH_DEV_AUTOMATION_TESTS
