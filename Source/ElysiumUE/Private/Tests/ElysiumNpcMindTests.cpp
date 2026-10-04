#include "Misc/AutomationTest.h"

#include "Tests/ElysiumArmTier.h"

#if WITH_DEV_AUTOMATION_TESTS && ELYSIUM_WITH_ARM_TESTS

#include "Substrate/ElysiumNpcMind.h"

static constexpr EAutomationTestFlags GElysiumNpcMindTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// The admission barrier (port-only, V6's: story V3 README Q5). It stays while the barrier does.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcMindAdmissionTest,
	"Elysium.Arm.NpcMind.Admission", GElysiumNpcMindTestFlags)

bool FElysiumNpcMindAdmissionTest::RunTest(const FString&)
{
	FElysiumNpcMind Mind;
	Mind.ArmAdmission();
	TestTrue(TEXT("first frozen think admits once"), Mind.Admit());
	TestFalse(TEXT("admission is idempotent"), Mind.Admit());
	TestEqual(TEXT("admission establishes idle"), Mind.State(), EElysiumNpcState::Idle);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
