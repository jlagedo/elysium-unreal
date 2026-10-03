#include "Misc/AutomationTest.h"

#include "Tests/ElysiumArmTier.h"

#if WITH_DEV_AUTOMATION_TESTS && ELYSIUM_WITH_ARM_TESTS

#include "ElysiumEntity.h"
#include "Substrate/ElysiumNpcBase.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcTestHull.h"

// Story 5 fold A1 (spec 0019): `CAI_TestHull` is `FElysiumNpcTestHull : FElysiumNpcBase`, and its
// own bodies are overrides on the class. No classname builds one (retail builds it by code, for
// graph-build probes the port does not run), so each case constructs the C++ type directly and
// compares it against a base-only NPC through the same `FElysiumNpcBase` / `FElysiumEntity`
// reference, which is how every caller of those slots dispatches.

static constexpr EAutomationTestFlags GElysiumNpcKernelTestHullFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// A concrete `FElysiumNpcBase` with no class of its own; the interface's pure hooks answer the
	// inert value. Named for this file because the module builds adaptive-unity.
	class FTestHullBaseOnlyNpc final : public FElysiumNpcBase
	{
	public:
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelTestHullConstructionTest,
	"Elysium.Arm.NpcKernelTestHull.Construction", GElysiumNpcKernelTestHullFlags)
bool FElysiumNpcKernelTestHullConstructionTest::RunTest(const FString&)
{
	FElysiumNpcTestHull Hull;
	FElysiumNpcBase& AsBase = Hull;
	TestTrue(TEXT("+0x94 m_pBaseNPC is the hull itself"), AsBase.AsNpcBase() == &AsBase);
	TestNull(TEXT("+0x98 m_pBaseNPCTroika is null: a direct CAI_BaseNPC subclass"), AsBase.AsNpc());

	const FElysiumNpcClass* Cls = AsBase.RetailClass();
	if (TestNotNull(TEXT("the hull answers a census row"), Cls))
	{
		TestEqual(TEXT("its own, CAI_TestHull"), FString(Cls->Name), FString(TEXT("CAI_TestHull")));
		TestEqual(TEXT("which derives from CAI_BaseNPC"), FString(Cls->Base), FString(TEXT("CAI_BaseNPC")));
		TestEqual(TEXT("and no classname builds it"), Cls->ClassnameCount, 0);
		// `vtmb_vtable CAI_TestHull`: slots 5, 103, 117, 521, 522 and 523 hold its own bodies; slot 5
		// is the scalar deleting destructor, the C++ destructor's.
		TestEqual(TEXT("it fills six slots with bodies of its own"), Cls->OwnBodies, 6);
	}
	TestTrue(TEXT("AsSpecies answers the typed hull"), AsBase.AsSpecies<FElysiumNpcTestHull>() == &Hull);

	FTestHullBaseOnlyNpc Base;
	TestNull(TEXT("a base-only NPC is no test hull"), Base.AsSpecies<FElysiumNpcTestHull>());
	TestNull(TEXT("nor answers any class of the tree"), Base.RetailClass());
	return true;
}

#endif
