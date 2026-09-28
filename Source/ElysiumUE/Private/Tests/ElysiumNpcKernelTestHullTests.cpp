#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

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
	"Elysium.Substrate.NpcKernelTestHull.Construction", GElysiumNpcKernelTestHullFlags)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelTestHullBodiesTest,
	"Elysium.Substrate.NpcKernelTestHull.Bodies", GElysiumNpcKernelTestHullFlags)
bool FElysiumNpcKernelTestHullBodiesTest::RunTest(const FString&)
{
	FElysiumNpcTestHull HullObject;
	FTestHullBaseOnlyNpc BaseObject;
	const FElysiumNpcBase& Hull = HullObject;
	const FElysiumNpcBase& Base = BaseObject;

	// Slot 522: `0x102d72b0` returns `_DAT_10462950` = 40.0; the base's `0x101a6b40` = 18.0.
	TestEqual(TEXT("slot 522 StepHeight: the hull answers 40"), Hull.StepHeight(), 40.0f);
	TestEqual(TEXT("...and a base-only NPC the base's 18"), Base.StepHeight(), 18.0f);

	// Slot 523: `0x102d72d0` reads the SAME 40.0 cell.
	TestEqual(TEXT("slot 523 GetMaxJumpSpeed: the hull answers 40"), Hull.GetMaxJumpSpeed(), 40.0f);
	// The base's own `0x101a6b60` returns `_DAT_10453b94` — the step height's 18.0 cell. A retail
	// correction of fold A1: the port's generated stub answered 0 here.
	TestEqual(TEXT("...and a base-only NPC the base's 18, the step height's cell"), Base.GetMaxJumpSpeed(),
		18.0f);

	// Slot 521: `0x102d7760` hands `FUN_10280790` 1024 / 1024 / 1024; the base's `0x10280880` hands it
	// 80 / 250 / 160. Each point set below separates one bound.
	auto Legal = [](const FElysiumNpcBase& Npc, const FVector& Start, const FVector& Apex, const FVector& End)
	{
		FVector S = Start;
		FVector A = Apex;
		FVector E = End;
		return Npc.IsJumpLegal(S, A, E);
	};
	// A rise of 300: over the base's 80, under the hull's 1024.
	const FVector Zero = FVector::ZeroVector;
	TestTrue(TEXT("slot 521: the hull admits a 300-unit rise"), Legal(Hull, Zero, Zero, FVector(0.0, 0.0, 300.0)));
	TestFalse(TEXT("...the base refuses it"), Legal(Base, Zero, Zero, FVector(0.0, 0.0, 300.0)));
	// The rise bound with its 0.1 slack: 1024.1 legal, 1024.2 not.
	TestTrue(TEXT("the hull's rise bound is 1024 + 0.1"), Legal(Hull, Zero, Zero, FVector(0.0, 0.0, 1024.1)));
	TestFalse(TEXT("...and 1024.2 is over it"), Legal(Hull, Zero, Zero, FVector(0.0, 0.0, 1024.2)));
	// The drop bound (the apex at the start height clears the apex arm): 1024.1 legal, 1024.2 not.
	TestTrue(TEXT("the hull's drop bound is 1024 + 0.1"),
		Legal(Hull, FVector(0.0, 0.0, 1024.1), FVector(0.0, 0.0, 1024.1), Zero));
	TestFalse(TEXT("...and 1024.2 is over it"),
		Legal(Hull, FVector(0.0, 0.0, 1024.2), FVector(0.0, 0.0, 1024.2), Zero));
	// The distance bound: a flat 1024.1 legal, 1024.2 not; the base's is 160.
	TestTrue(TEXT("the hull's distance bound is 1024 + 0.1"), Legal(Hull, Zero, Zero, FVector(1024.1, 0.0, 0.0)));
	TestFalse(TEXT("...and 1024.2 is over it"), Legal(Hull, Zero, Zero, FVector(1024.2, 0.0, 0.0)));
	TestFalse(TEXT("...where the base stops at 160"), Legal(Base, Zero, Zero, FVector(300.0, 0.0, 0.0)));

	// Slot 117: `0x102d7290` = `CBaseEntity::ObjectCaps` `& 0xfffffffd` — never carried across a level
	// change. The base line does not override slot 117 and keeps the transition bit.
	const FElysiumEntity& HullEntity = HullObject;
	const FElysiumEntity& BaseEntity = BaseObject;
	TestEqual(TEXT("slot 117 ObjectCaps: the hull clears FCAP_ACROSS_TRANSITION"), HullEntity.ObjectCaps(), 0);
	TestEqual(TEXT("...and a base-only NPC keeps it"), BaseEntity.ObjectCaps(),
		ElysiumEntityCaps::AcrossTransition);
	return true;
}

#endif
