// Content-free Substrate automation: `aiscripted_schedule` — the authored AI director's two
// recovered tables.
//
// Every number asserted here is a fact from `docs/vtmb/npc-ai/authored-control.md` ->
// "`aiscripted_schedule`": `FixScriptNPCSchedule 0x101a98c0`'s authored `forcestate` -> native state
// map and its mode set. The director's run itself is pinned by the arena record
// `script_aischedule_walk`, not here.

#include "Misc/AutomationTest.h"

#include "Tests/ElysiumArmTier.h"

#if WITH_DEV_AUTOMATION_TESTS && ELYSIUM_WITH_ARM_TESTS

#include "ElysiumNpcMindTypes.h"
#include "Substrate/ElysiumAiScriptedSchedule.h"
#include "Substrate/ElysiumScheduleId.h"
#include "Substrate/ElysiumScheduleNumbers.h"

namespace ElysiumAiScriptedScheduleTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// The two recovered tables: `forcestate`'s asymmetry and the mode set.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumAiScriptedScheduleTablesTest,
	"Elysium.Arm.AiScriptedSchedule.Tables", GElysiumTestFlags)
bool FElysiumAiScriptedScheduleTablesTest::RunTest(const FString&)
{
	// The load-bearing asymmetry. "Treating the keyvalue as the native enum would swap combat and
	// alert", which is why authored 2 and 3 are asserted in BOTH directions here.
	EElysiumNpcState State = EElysiumNpcState::Prone;
	TestFalse(TEXT("authored 0 forces no state at all"),
		ElysiumAiScriptedSchedule::ForcedState(0, State));

	TestTrue(TEXT("authored 1 forces a state"), ElysiumAiScriptedSchedule::ForcedState(1, State));
	TestEqual(TEXT("...and it is idle"), State, EElysiumNpcState::Idle);

	TestTrue(TEXT("authored 2 forces a state"), ElysiumAiScriptedSchedule::ForcedState(2, State));
	TestEqual(TEXT("...and it is ALERT, not combat"), State, EElysiumNpcState::Alert);

	TestTrue(TEXT("authored 3 forces a state"), ElysiumAiScriptedSchedule::ForcedState(3, State));
	TestEqual(TEXT("...and it is COMBAT, not alert"), State, EElysiumNpcState::Combat);

	TestFalse(TEXT("a value outside the recovered table forces nothing"),
		ElysiumAiScriptedSchedule::ForcedState(4, State));
	TestFalse(TEXT("...and is not a known force state"),
		ElysiumAiScriptedSchedule::IsKnownForceState(4));
	TestTrue(TEXT("0..3 are the known force states"),
		ElysiumAiScriptedSchedule::IsKnownForceState(0)
		&& ElysiumAiScriptedSchedule::IsKnownForceState(3));

	// The mode table: 1..5 and nothing else.
	TestFalse(TEXT("mode 0 is not a movement mode"), ElysiumAiScriptedSchedule::IsKnownMode(0));
	for (int32 Mode = 1; Mode <= 5; ++Mode)
	{
		TestTrue(FString::Printf(TEXT("mode %d is recovered"), Mode),
			ElysiumAiScriptedSchedule::IsKnownMode(Mode));
	}
	TestFalse(TEXT("mode 6 is outside the recovered table"),
		ElysiumAiScriptedSchedule::IsKnownMode(6));

	TestTrue(TEXT("modes 1 and 2 are the move-to-goal variants"),
		ElysiumAiScriptedSchedule::IsMoveToGoal(1) && ElysiumAiScriptedSchedule::IsMoveToGoal(2));
	TestTrue(TEXT("modes 4 and 5 are the follow-path variants"),
		ElysiumAiScriptedSchedule::IsFollowPath(4) && ElysiumAiScriptedSchedule::IsFollowPath(5));
	TestFalse(TEXT("mode 3 runs no program at all"),
		ElysiumAiScriptedSchedule::IsMoveToGoal(3) || ElysiumAiScriptedSchedule::IsFollowPath(3));

	TestEqual(TEXT("the move variants share one registered program"),
		ElysiumAiScriptedSchedule::ProgramFor(1), ElysiumSched::IDLE_WALK);
	TestEqual(TEXT("...both of them"),
		ElysiumAiScriptedSchedule::ProgramFor(2), ElysiumSched::IDLE_WALK);
	TestEqual(TEXT("the follow variants share the other"),
		ElysiumAiScriptedSchedule::ProgramFor(5), ElysiumSched::IDLE_WALK);
	TestEqual(TEXT("mode 3 names no program"),
		ElysiumAiScriptedSchedule::ProgramFor(3), ElysiumScheduleId::None);

	// CHOSEN, NOT RECOVERED — asserted so the choice is visible in one place and a decoded
	// discriminator changes exactly one function and this block.
	TestFalse(TEXT("CHOSEN: mode 1 walks"), ElysiumAiScriptedSchedule::IsRunVariant(1));
	TestTrue(TEXT("CHOSEN: mode 2 runs"), ElysiumAiScriptedSchedule::IsRunVariant(2));
	TestFalse(TEXT("CHOSEN: mode 4 walks"), ElysiumAiScriptedSchedule::IsRunVariant(4));
	TestTrue(TEXT("CHOSEN: mode 5 runs"), ElysiumAiScriptedSchedule::IsRunVariant(5));

	return true;
}

}   // namespace ElysiumAiScriptedScheduleTests

#endif // WITH_DEV_AUTOMATION_TESTS
