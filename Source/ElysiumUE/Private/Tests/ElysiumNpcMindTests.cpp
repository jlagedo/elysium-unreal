#include "Misc/AutomationTest.h"

#include "Tests/ElysiumArmTier.h"

#if WITH_DEV_AUTOMATION_TESTS && ELYSIUM_WITH_ARM_TESTS

#include "Substrate/ElysiumNpcMind.h"

static constexpr EAutomationTestFlags GElysiumNpcMindTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcMindAdmissionTest,
	"Elysium.Arm.NpcMind.Admission", GElysiumNpcMindTestFlags)

bool FElysiumNpcMindAdmissionTest::RunTest(const FString&)
{
	FElysiumNpcMind Mind;
	FElysiumBodyOwnerToken Token;
	TestFalse(TEXT("spawn does not admit an executor"),
		Mind.Acquire(EElysiumBodyOwner::Ambient, false, Token, TEXT("pre-activation")));
	Mind.ArmAdmission();
	TestFalse(TEXT("activation arms but does not decide"),
		Mind.Acquire(EElysiumBodyOwner::Ambient, false, Token, TEXT("before first think")));
	TestTrue(TEXT("first frozen think admits once"), Mind.Admit());
	TestFalse(TEXT("admission is idempotent"), Mind.Admit());
	TestEqual(TEXT("admission establishes idle"), Mind.State(), EElysiumNpcState::Idle);
	TestEqual(TEXT("admission owns no body"), Mind.Owner(), EElysiumBodyOwner::None);
	// Alert and Combat are live transitions now that `SelectIdealState` produces them.
	TestTrue(TEXT("alert is a live transition"),
		Mind.RequestState(EElysiumNpcState::Alert, TEXT("test")));
	TestEqual(TEXT("...and takes effect"), Mind.State(), EElysiumNpcState::Alert);
	TestTrue(TEXT("combat is a live transition"),
		Mind.RequestState(EElysiumNpcState::Combat, TEXT("test")));
	TestEqual(TEXT("...and takes effect"), Mind.State(), EElysiumNpcState::Combat);
	// Prone still has no producer, so it stays a named refusal that leaves the state alone.
	TestFalse(TEXT("unimplemented prone transition is diagnostic failure"),
		Mind.RequestState(EElysiumNpcState::Prone, TEXT("test")));
	TestEqual(TEXT("failed transition preserves current state"), Mind.State(),
		EElysiumNpcState::Combat);
	TestTrue(TEXT("the mind returns to idle on request"),
		Mind.RequestState(EElysiumNpcState::Idle, TEXT("test")));

	// An ordinary body owner is not a cognitive transition: an alert NPC that takes a schedule token
	// stays alert, or the ideal-state pass and the arbiter would fight for the state every think.
	// (The patrol owner these cases used is deleted: the patrol is the path object's program since
	// story 8 wave 2 and claims no body.)
	Mind.RequestState(EElysiumNpcState::Alert, TEXT("test"));
	FElysiumBodyOwnerToken ScheduleToken;
	TestTrue(TEXT("a schedule acquires the free body"),
		Mind.Acquire(EElysiumBodyOwner::Schedule, false, ScheduleToken, TEXT("schedule")));
	TestEqual(TEXT("...without resetting the cognitive state"), Mind.State(),
		EElysiumNpcState::Alert);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcBodyOwnerTest,
	"Elysium.Arm.NpcMind.BodyOwner", GElysiumNpcMindTestFlags)

bool FElysiumNpcBodyOwnerTest::RunTest(const FString&)
{
	FElysiumNpcMind Mind;
	Mind.ArmAdmission();
	Mind.Admit();

	FElysiumBodyOwnerToken Held;
	TestTrue(TEXT("schedule acquires a free body"),
		Mind.Acquire(EElysiumBodyOwner::Schedule, false, Held, TEXT("schedule")));
	FElysiumBodyOwnerToken Ambient;
	TestFalse(TEXT("ambient cannot steal a schedule"),
		Mind.Acquire(EElysiumBodyOwner::Ambient, false, Ambient, TEXT("ambient")));

	FElysiumBodyOwnerToken Sequence;
	TestTrue(TEXT("sequence explicitly suspends the schedule"),
		Mind.Acquire(EElysiumBodyOwner::Sequence, true, Sequence, TEXT("sequence")));
	TestEqual(TEXT("suspended owner is retained"), Mind.SuspendedOwner(), EElysiumBodyOwner::Schedule);
	TestEqual(TEXT("sequence establishes scripted state"), Mind.State(), EElysiumNpcState::Scripted);
	TestFalse(TEXT("displaced schedule token is stale"), Mind.Release(Held, TEXT("stale schedule")));
	TestTrue(TEXT("sequence releases its own generation"), Mind.Release(Sequence, TEXT("sequence end")));
	TestEqual(TEXT("schedule restores after sequence"), Mind.Owner(), EElysiumBodyOwner::Schedule);

	const FElysiumBodyOwnerToken RestoredHeld = Mind.CurrentToken();
	TestFalse(TEXT("pre-suspension token cannot release restored schedule"),
		Mind.Release(Held, TEXT("old generation")));
	TestTrue(TEXT("restored schedule has a new valid token"),
		Mind.Release(RestoredHeld, TEXT("clear schedule")));

	FElysiumBodyOwnerToken DialogueHeld;
	TestTrue(TEXT("schedule can reacquire before dialogue"),
		Mind.Acquire(EElysiumBodyOwner::Schedule, false, DialogueHeld, TEXT("schedule resume")));
	FElysiumBodyOwnerToken Dialogue;
	TestTrue(TEXT("dialogue explicitly suspends the schedule"),
		Mind.Acquire(EElysiumBodyOwner::Dialogue, true, Dialogue, TEXT("dialogue")));
	TestEqual(TEXT("dialogue remembers the autonomous owner"),
		Mind.SuspendedOwner(), EElysiumBodyOwner::Schedule);
	FElysiumBodyOwnerToken Unsupported;
	TestFalse(TEXT("unimplemented follower owner is refused"),
		Mind.Acquire(EElysiumBodyOwner::Follower, false, Unsupported, TEXT("follower")));
	TestTrue(TEXT("dialogue releases its generation"), Mind.Release(Dialogue, TEXT("dialogue end")));
	TestEqual(TEXT("schedule restores after dialogue"), Mind.Owner(), EElysiumBodyOwner::Schedule);
	const FElysiumBodyOwnerToken HeldAfterDialogue = Mind.CurrentToken();
	TestTrue(TEXT("restored schedule can be cleared"),
		Mind.Release(HeldAfterDialogue, TEXT("ambient setup")));

	FElysiumBodyOwnerToken AmbientOwner;
	TestTrue(TEXT("ambient acquires only a free body"),
		Mind.Acquire(EElysiumBodyOwner::Ambient, false, AmbientOwner, TEXT("ambient claim")));
	FElysiumBodyOwnerToken DialogueFromAmbient;
	TestFalse(TEXT("dialogue cannot steal an unreleased ambient claim"),
		Mind.Acquire(EElysiumBodyOwner::Dialogue, true, DialogueFromAmbient, TEXT("dialogue")));
	FElysiumBodyOwnerToken SequenceFromAmbient;
	TestTrue(TEXT("sequence can explicitly suspend ambient intent"),
		Mind.Acquire(EElysiumBodyOwner::Sequence, true, SequenceFromAmbient, TEXT("sequence")));
	TestTrue(TEXT("sequence restores ambient intent"),
		Mind.Release(SequenceFromAmbient, TEXT("sequence end")));
	TestEqual(TEXT("ambient is restored with a fresh generation"),
		Mind.Owner(), EElysiumBodyOwner::Ambient);
	const FElysiumBodyOwnerToken AmbientAfterSequence = Mind.CurrentToken();
	Mind.Invalidate(TEXT("death"), true);
	TestEqual(TEXT("death invalidates body owner"), Mind.Owner(), EElysiumBodyOwner::None);
	TestEqual(TEXT("death establishes terminal state"), Mind.State(), EElysiumNpcState::Dead);
	TestFalse(TEXT("death invalidates the ambient token"),
		Mind.Release(AmbientAfterSequence, TEXT("late release")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcMindRestoreTest,
	"Elysium.Arm.NpcMind.Restore", GElysiumNpcMindTestFlags)

bool FElysiumNpcMindRestoreTest::RunTest(const FString&)
{
	FElysiumNpcMind Mind;
	Mind.Restore(EElysiumNpcState::Idle, EElysiumBodyOwner::Ambient);
	TestEqual(TEXT("restore admits the saved mind"), Mind.Admission(),
		FElysiumNpcMind::EAdmission::Admitted);
	TestEqual(TEXT("ambient intent is resumable"), Mind.Owner(), EElysiumBodyOwner::Ambient);
	TestTrue(TEXT("restored intent receives a fresh transient token"), Mind.CurrentToken().IsSet());

	Mind.Restore(EElysiumNpcState::Scripted, EElysiumBodyOwner::Dialogue);
	TestEqual(TEXT("scripted session state is not resumed"), Mind.State(), EElysiumNpcState::Idle);
	TestEqual(TEXT("dialogue ownership is not resumed"), Mind.Owner(), EElysiumBodyOwner::None);

	Mind.Restore(EElysiumNpcState::Dead, EElysiumBodyOwner::Ambient);
	TestEqual(TEXT("dead state restores"), Mind.State(), EElysiumNpcState::Dead);
	TestEqual(TEXT("dead state never restores an executor"), Mind.Owner(), EElysiumBodyOwner::None);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
