#pragma once

// The warnings a headless entity world logs by construction.
//
// A test world has no `.qc` eye offsets, no `Npc_Follower_Info` / reaction-percentage rows, no squads
// and no baked character model, so spawning one NPC logs the retail DevWarnings those inputs guard,
// and every stubbed slot an NPC reaches fires the stub funnel. They were 27,000 of the suite's 28,000
// warning entries and put "passed with warnings" on 70% of the tests, which made the status carry no
// signal. Production keeps every one of them at its verbosity (they are real in the live game);
// only a test that stands a recording-services world declares them expected, and an occurrence count
// of -1 means "silently ignored", so a test that never logs one is not failed for it.
//
// Declared from `FElysiumRecordingServices`' constructor, the one object every such world is built
// on, so a fixture needs no call of its own. No test counts one of these with an `AddExpectedError`
// of its own (the framework credits the first matching entry only), so a test that starts to must
// not stand its world on `FElysiumRecordingServices`.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include <initializer_list>

namespace ElysiumFixtureNoise
{
	// `SetDefaultEyeOffset 0x10274ca0` (ElysiumNpcBaseHelpers.cpp): the port has no model-space eye
	// offset word, so the sentinel arm is the one every NPC takes.
	inline constexpr const TCHAR* EyeOffset = TEXT("has no eye offset in .qc!");
	// `SetFollowerType 0x102c4680` (ElysiumNpcSquad.cpp): no `Npc_Follower_Info` loader, so the three
	// distances are zero and the retail clamp warns on both pairs.
	inline constexpr const TCHAR* FollowerOverlap = TEXT("overlap; changing FollowerDistance");
	// The stub funnel (`ElysiumStub`, `elysium.StubWarn`): the tally, not the log, is what a test reads.
	inline constexpr const TCHAR* StubFired = TEXT("Stub fired");
	// `NPCInit`'s occluded-reaction percentages (ElysiumNpcSpawn.cpp): no rulebook row in the world.
	inline constexpr const TCHAR* OccludedReaction = TEXT("Occluded target reaction percentages");
	// `CAI_BaseNPC::Spawn`'s solo check (ElysiumNpcBaseSquad.cpp): a fixture NPC joins no squad.
	inline constexpr const TCHAR* SoloNpc = TEXT("isn't in a squad but not supposed to be solo");
	// The character-model admission (ElysiumAnimatingImpl.cpp): no baked cast in a headless world.
	inline constexpr const TCHAR* ModelAdmission = TEXT("character model admission refused: no native asset for the error model");

	// Declares the six on the running test. A construction outside a running test (a static) has no
	// current test and declares nothing.
	inline void Declare()
	{
		FAutomationTestBase* Test = FAutomationTestFramework::Get().GetCurrentTest();
		if (Test == nullptr)
		{
			return;
		}
		for (const TCHAR* Message : { EyeOffset, FollowerOverlap, StubFired, OccludedReaction, SoloNpc, ModelAdmission })
		{
			Test->AddExpectedErrorPlain(Message, EAutomationExpectedErrorFlags::Contains, -1);
		}
	}
}

#endif   // WITH_DEV_AUTOMATION_TESTS
