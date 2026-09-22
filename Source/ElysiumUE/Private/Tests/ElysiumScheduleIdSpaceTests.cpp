#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Substrate/ElysiumIdNamespace.h"
#include "Substrate/ElysiumLocalIdSpace.h"
#include "Substrate/ElysiumScheduleId.h"

// `CAI_LocalIdSpace` and the namespace it registers into, against the four retail bodies:
// `Init 0x102ea0e0`, `SetFirstLocal 0x102ea240`, `Register 0x102ea130`, `LocalToGlobal 0x102ea2d0`
// and `GlobalToLocal 0x102ea280`.
//
// The worked example every case is shaped after is the one the oracle walks: `CAI_BaseNPC`
// registers schedule ids 0x00..0x43, `CAI_BaseNPCTroika` parents on it and takes 0x44..0x155, and
// `CNPC_VBrujah` parents on `CNPC_VVampire` and takes 0x158..0x159.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScheduleIdSpaceInitTest,
	"Elysium.Substrate.ScheduleIdSpace.Init",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumScheduleIdSpaceInitTest::RunTest(const FString&)
{
	FElysiumIdNamespace Namespace;
	Namespace.Category = TEXT("schedule");
	TestEqual(TEXT("a fresh namespace hands out the seed first"),
		Namespace.NextFree(), ElysiumScheduleId::GlobalBase);

	// A ROOT space opens at local 0; a PARENTED one stays empty until its first registration.
	FElysiumLocalIdSpace Root;
	TestTrue(TEXT("a root space initialises"), Root.Init(Namespace, nullptr));
	TestEqual(TEXT("a root space opens at local 0"), Root.LocalBase, 0);
	TestEqual(TEXT("a root space takes the namespace's counter as its global base"),
		Root.GlobalBase, ElysiumScheduleId::GlobalBase);

	FElysiumLocalIdSpace Child;
	TestTrue(TEXT("a parented space initialises"), Child.Init(Namespace, &Root));
	TestTrue(TEXT("a parented space starts empty"), Child.IsEmpty());
	TestEqual(TEXT("an empty space's base is retail's 9999 sentinel"),
		Child.LocalBase, ElysiumScheduleId::EmptyLocalBase);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScheduleIdSpaceRegisterTest,
	"Elysium.Substrate.ScheduleIdSpace.Register",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumScheduleIdSpaceRegisterTest::RunTest(const FString&)
{
	FElysiumIdNamespace Namespace;
	FElysiumLocalIdSpace Base;
	Base.Init(Namespace, nullptr);

	TestTrue(TEXT("the first name registers"),
		Base.Register(TEXT("NONE"), 0x00, TEXT("schedule"), TEXT("CAI_BaseNPC")));
	TestTrue(TEXT("the last name registers"),
		Base.Register(TEXT("FAIL"), 0x43, TEXT("schedule"), TEXT("CAI_BaseNPC")));

	TestEqual(TEXT("the local top follows the highest id"), Base.LocalTop, 0x43);
	TestEqual(TEXT("the translated top is that id in global numbers"),
		Base.TranslatedTop, ElysiumScheduleId::GlobalBase + 0x43);
	TestEqual(TEXT("the namespace's counter cleared the whole range"),
		Namespace.NextFree(), ElysiumScheduleId::GlobalBase + 0x44);

	// The counter is a high-water mark: the NEXT space begins past everything the base took, which
	// is why Troika's ids start at 68 and not at zero.
	FElysiumLocalIdSpace Troika;
	Troika.Init(Namespace, &Base);
	TestEqual(TEXT("the next space begins where the previous ended"),
		Troika.GlobalBase, ElysiumScheduleId::GlobalBase + 0x44);
	TestTrue(TEXT("Troika's first schedule registers at 0x44"),
		Troika.Register(TEXT("SCHED_TROIKA_IDLE_STAND"), 0x44, TEXT("schedule"),
			TEXT("CAI_BaseNPCTroika")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScheduleIdSpaceRefusalTest,
	"Elysium.Substrate.ScheduleIdSpace.Refusals",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumScheduleIdSpaceRefusalTest::RunTest(const FString&)
{
	// Retail's three refusals. Each logs an Error and answers false.
	AddExpectedError(TEXT("uninitialized id space"), EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedError(TEXT("is not above its parent's base"),
		EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedError(TEXT("is below the base"), EAutomationExpectedErrorFlags::Contains, 0);

	FElysiumLocalIdSpace Uninitialised;
	TestFalse(TEXT("an uninitialised space refuses every name"),
		Uninitialised.Register(TEXT("X"), 1, TEXT("schedule"), TEXT("CNPC_VStub")));

	// Both remaining refusals need a PARENTED space. A root's base is 0 (`Init` seeds it there),
	// so nothing can sit below it and its first registration never goes through `SetFirstLocal` at
	// all -- which is why `CAI_BaseNPC`, a root, accepts its ids in any order.
	FElysiumIdNamespace Namespace;
	FElysiumLocalIdSpace Root;
	Root.Init(Namespace, nullptr);
	Root.Register(TEXT("ROOT_A"), 0x00, TEXT("schedule"), TEXT("CAI_BaseNPC"));

	FElysiumLocalIdSpace Middle;
	Middle.Init(Namespace, &Root);
	Middle.Register(TEXT("MIDDLE"), 0x40, TEXT("schedule"), TEXT("CAI_BaseNPCTroika"));
	TestEqual(TEXT("a parented space's base is its first registered id"), Middle.LocalBase, 0x40);

	// `0x102ea240`: a child's first id must clear its PARENT's base.
	FElysiumLocalIdSpace Child;
	Child.Init(Namespace, &Middle);
	TestFalse(TEXT("a child cannot claim a base its parent already holds"),
		Child.Register(TEXT("CLASH"), 0x10, TEXT("schedule"), TEXT("CNPC_VStub")));

	// An id below an established base is the third refusal.
	FElysiumLocalIdSpace Ordered;
	Ordered.Init(Namespace, &Root);
	TestTrue(TEXT("the first id sets the base"),
		Ordered.Register(TEXT("FIRST"), 0x20, TEXT("schedule"), TEXT("CNPC_VStub")));
	TestFalse(TEXT("an id below the base is refused"),
		Ordered.Register(TEXT("LOWER"), 0x10, TEXT("schedule"), TEXT("CNPC_VStub")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScheduleIdSpaceTranslateTest,
	"Elysium.Substrate.ScheduleIdSpace.Translate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumScheduleIdSpaceTranslateTest::RunTest(const FString&)
{
	// The Brujah chain, as the oracle walks it: base -> Troika -> Vampire -> Brujah.
	FElysiumIdNamespace Namespace;
	FElysiumLocalIdSpace Base;
	FElysiumLocalIdSpace Troika;
	FElysiumLocalIdSpace Vampire;
	FElysiumLocalIdSpace Brujah;
	Base.Init(Namespace, nullptr);
	// `NONE` at 0x00 first, as `0x102cadd0` registers it. A root space's base is 0 and `Register`
	// sets its local top to that base on the first call, so a root that registered 0x01 before
	// 0x00 would translate 0x01 to -1 and insert nothing. Retail is consistent here only because
	// the base class does register `NONE` first.
	Base.Register(TEXT("NONE"), 0x00, TEXT("schedule"), TEXT("CAI_BaseNPC"));
	Base.Register(TEXT("IDLE_STAND"), 0x01, TEXT("schedule"), TEXT("CAI_BaseNPC"));
	Base.Register(TEXT("FAIL"), 0x43, TEXT("schedule"), TEXT("CAI_BaseNPC"));
	Troika.Init(Namespace, &Base);
	Troika.Register(TEXT("SCHED_TROIKA_IDLE_STAND"), 0x44, TEXT("schedule"), TEXT("T"));
	Vampire.Init(Namespace, &Troika);
	Vampire.Register(TEXT("SCHED_VVAMPIRE_A"), 0x150, TEXT("schedule"), TEXT("V"));
	Brujah.Init(Namespace, &Vampire);
	Brujah.Register(TEXT("SCHED_VBRUJAH_WALK"), 0x158, TEXT("schedule"), TEXT("CNPC_VBrujah"));

	// A class's OWN id translates in its own space.
	const int32 WalkGlobal = Brujah.LocalToGlobal(0x158);
	TestTrue(TEXT("a class's own id translates"), WalkGlobal != INDEX_NONE);
	TestTrue(TEXT("a translated id is global"), ElysiumScheduleId::IsGlobal(WalkGlobal));
	TestEqual(TEXT("and translates back"), Brujah.GlobalToLocal(WalkGlobal), 0x158);
	TestEqual(TEXT("the namespace agrees with the space"),
		Namespace.Find(TEXT("SCHED_VBRUJAH_WALK")), WalkGlobal);

	// A BASE id resolves through the parent chain from the leaf, which is what makes a species
	// text naming `SCHEDULE:IDLE_STAND` resolve at all.
	const int32 IdleGlobal = Brujah.LocalToGlobal(0x01);
	TestEqual(TEXT("a base id resolves from the leaf through the chain"),
		IdleGlobal, Base.LocalToGlobal(0x01));
	TestEqual(TEXT("the namespace agrees"), Namespace.Find(TEXT("IDLE_STAND")), IdleGlobal);

	// The sentinel and a miss both answer -1.
	TestEqual(TEXT("-1 stays -1"), Brujah.LocalToGlobal(INDEX_NONE), INDEX_NONE);
	TestEqual(TEXT("an id no space holds answers -1"), Brujah.LocalToGlobal(0x7000), INDEX_NONE);
	TestEqual(TEXT("a global id no space holds answers -1"),
		Brujah.GlobalToLocal(ElysiumScheduleId::GlobalBase + 0x7000), INDEX_NONE);

	// Case-insensitivity: every name compare in retail is `strcmpi`.
	TestEqual(TEXT("a name resolves whatever its case"),
		Namespace.Find(TEXT("sched_vbrujah_walk")), WalkGlobal);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScheduleIdSpaceTranslatedTopTest,
	"Elysium.Substrate.ScheduleIdSpace.TranslatedTop",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumScheduleIdSpaceTranslatedTopTest::RunTest(const FString&)
{
	// `GlobalToLocal` bounds against the TRANSLATED top, not the local one. The two differ by the
	// global base, so a space whose local range is small and whose global base is large is exactly
	// where bounding against the wrong word shows: every id in the range would fail the test.
	FElysiumIdNamespace Namespace;
	FElysiumLocalIdSpace Base;
	Base.Init(Namespace, nullptr);
	Base.Register(TEXT("A"), 0, TEXT("schedule"), TEXT("Base"));
	Base.Register(TEXT("B"), 400, TEXT("schedule"), TEXT("Base"));

	FElysiumLocalIdSpace Leaf;
	Leaf.Init(Namespace, &Base);
	Leaf.Register(TEXT("LEAF"), 401, TEXT("schedule"), TEXT("Leaf"));

	const int32 Global = Leaf.LocalToGlobal(401);
	TestTrue(TEXT("the leaf's id translates"), Global != INDEX_NONE);
	TestTrue(TEXT("the translated top is above the local top"),
		Leaf.TranslatedTop > Leaf.LocalTop);
	TestEqual(TEXT("and the round trip holds, which it cannot if the bound is the local top"),
		Leaf.GlobalToLocal(Global), 401);
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
