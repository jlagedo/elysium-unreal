#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Substrate/ElysiumNpcMind.h"

static constexpr EAutomationTestFlags GElysiumNpcMindTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcMindAdmissionTest,
	"Elysium.Substrate.NpcMind.Admission", GElysiumNpcMindTestFlags)

bool FElysiumNpcMindAdmissionTest::RunTest(const FString&)
{
	FElysiumNpcMind Mind;
	FElysiumBodyOwnerToken Token;
	TestFalse(TEXT("spawn does not admit an executor"),
		Mind.Acquire(EElysiumBodyOwner::Patrol, false, Token, TEXT("pre-activation")));
	Mind.ArmAdmission();
	TestFalse(TEXT("activation arms but does not decide"),
		Mind.Acquire(EElysiumBodyOwner::Patrol, false, Token, TEXT("before first think")));
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

	// An ordinary body owner is not a cognitive transition: an alert NPC that takes a patrol token
	// stays alert, or the ideal-state pass and the arbiter would fight for the state every think.
	Mind.RequestState(EElysiumNpcState::Alert, TEXT("test"));
	FElysiumBodyOwnerToken Patrol;
	TestTrue(TEXT("patrol acquires the free body"),
		Mind.Acquire(EElysiumBodyOwner::Patrol, false, Patrol, TEXT("patrol")));
	TestEqual(TEXT("...without resetting the cognitive state"), Mind.State(),
		EElysiumNpcState::Alert);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcBodyOwnerTest,
	"Elysium.Substrate.NpcMind.BodyOwner", GElysiumNpcMindTestFlags)

bool FElysiumNpcBodyOwnerTest::RunTest(const FString&)
{
	FElysiumNpcMind Mind;
	Mind.ArmAdmission();
	Mind.Admit();

	FElysiumBodyOwnerToken Patrol;
	TestTrue(TEXT("patrol acquires a free body"),
		Mind.Acquire(EElysiumBodyOwner::Patrol, false, Patrol, TEXT("patrol")));
	FElysiumBodyOwnerToken Ambient;
	TestFalse(TEXT("ambient cannot steal patrol"),
		Mind.Acquire(EElysiumBodyOwner::Ambient, false, Ambient, TEXT("ambient")));

	FElysiumBodyOwnerToken Sequence;
	TestTrue(TEXT("sequence explicitly suspends patrol"),
		Mind.Acquire(EElysiumBodyOwner::Sequence, true, Sequence, TEXT("sequence")));
	TestEqual(TEXT("suspended owner is retained"), Mind.SuspendedOwner(), EElysiumBodyOwner::Patrol);
	TestEqual(TEXT("sequence establishes scripted state"), Mind.State(), EElysiumNpcState::Scripted);
	TestFalse(TEXT("displaced patrol token is stale"), Mind.Release(Patrol, TEXT("stale patrol")));
	TestTrue(TEXT("sequence releases its own generation"), Mind.Release(Sequence, TEXT("sequence end")));
	TestEqual(TEXT("patrol restores after sequence"), Mind.Owner(), EElysiumBodyOwner::Patrol);

	const FElysiumBodyOwnerToken RestoredPatrol = Mind.CurrentToken();
	TestFalse(TEXT("pre-suspension token cannot release restored patrol"),
		Mind.Release(Patrol, TEXT("old generation")));
	TestTrue(TEXT("restored patrol has a new valid token"),
		Mind.Release(RestoredPatrol, TEXT("clear patrol")));

	FElysiumBodyOwnerToken DialoguePatrol;
	TestTrue(TEXT("patrol can reacquire before dialogue"),
		Mind.Acquire(EElysiumBodyOwner::Patrol, false, DialoguePatrol, TEXT("patrol resume")));
	FElysiumBodyOwnerToken Dialogue;
	TestTrue(TEXT("dialogue explicitly suspends patrol"),
		Mind.Acquire(EElysiumBodyOwner::Dialogue, true, Dialogue, TEXT("dialogue")));
	TestEqual(TEXT("dialogue remembers the autonomous owner"),
		Mind.SuspendedOwner(), EElysiumBodyOwner::Patrol);
	FElysiumBodyOwnerToken Unsupported;
	TestFalse(TEXT("unimplemented follower owner is refused"),
		Mind.Acquire(EElysiumBodyOwner::Follower, false, Unsupported, TEXT("follower")));
	TestTrue(TEXT("dialogue releases its generation"), Mind.Release(Dialogue, TEXT("dialogue end")));
	TestEqual(TEXT("patrol restores after dialogue"), Mind.Owner(), EElysiumBodyOwner::Patrol);
	const FElysiumBodyOwnerToken PatrolAfterDialogue = Mind.CurrentToken();
	TestTrue(TEXT("restored patrol can be cleared"),
		Mind.Release(PatrolAfterDialogue, TEXT("ambient setup")));

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
	"Elysium.Substrate.NpcMind.Restore", GElysiumNpcMindTestFlags)

bool FElysiumNpcMindRestoreTest::RunTest(const FString&)
{
	FElysiumNpcMind Mind;
	Mind.Restore(EElysiumNpcState::Idle, EElysiumBodyOwner::Patrol);
	TestEqual(TEXT("restore admits the saved mind"), Mind.Admission(),
		FElysiumNpcMind::EAdmission::Admitted);
	TestEqual(TEXT("patrol intent is resumable"), Mind.Owner(), EElysiumBodyOwner::Patrol);
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
