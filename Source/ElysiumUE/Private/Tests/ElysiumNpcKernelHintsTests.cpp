#include "Misc/AutomationTest.h"

#include "Tests/ElysiumArmTier.h"

#if WITH_DEV_AUTOMATION_TESTS && ELYSIUM_WITH_ARM_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumHint.h"
#include "Substrate/ElysiumInterestingPlace.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumItemTable.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumRetailActivities.h"
#include "Substrate/ElysiumNpcAsianVampire.h"
#include "Substrate/ElysiumNpcSabbatLeader.h"
#include "Substrate/ElysiumNpcChangBros.h"
#include "Substrate/ElysiumNpcTzimisce.h"
#include "Substrate/ElysiumNpcWerewolf.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumPlaceSet.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumNpcTestCensus.h"

// Story 29c-1, family **Hints**. Every assertion below is read off the decompiled C of the body it
// names — the threshold, the arm order, the id, what is written — never off 29c's one-line walk.
//
// The family's standing fact is that this substrate has no hint node, so the suite is in two
// halves: the RULES, driven with a hand-built `FElysiumNpc::FHintWords` so retail's thresholds and
// switch arms are exercised exactly; and the SEAMS, each of which gets a case asserting that it is
// asked and that the refusal it answers is the recovered one.

static constexpr EAutomationTestFlags GHintsTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// A hint the seam never has to resolve: the words a `CAI_Hint` would have carried.
	FElysiumNpcBase::FHintWords MakeHint(int32 HintType)
	{
		FElysiumNpcBase::FHintWords Hint;
		Hint.bValid = true;
		Hint.HintType = HintType;
		return Hint;
	}
}

// -------------------------------------------------------------------------------------------------
// Slot 566's species bodies, each its class's own override.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelHintsTypeSpeciesTest,
	"Elysium.Arm.NpcKernelHints.HintTypeSpecies", GHintsTestFlags)
bool FElysiumNpcKernelHintsTypeSpeciesTest::RunTest(const FString&)
{
	// Ten DISTINCT slot-566 species bodies, each its class's own override (story 5 step 4): four
	// with a real rule, four `return 1`, one `return 0`, and `CNPC_VBach` (`0x10365800`), which
	// accepts 17000..17005 outright and FALLS THROUGH to the base body for everything else.
	// `CNPC_Crow`'s (`0x10358c60`) is on a class no map stands and has no C++ class. The blade and
	// claw brothers share `CNPC_VChangBros`'s body by inheriting its override.
	struct FSpecies
	{
		const TCHAR* Class;
		const TCHAR* Body;
	};
	static const FSpecies Species[] = {
		{ TEXT("CNPC_VDog"), TEXT("0x10374aa0") },
		{ TEXT("CNPC_VSabbatLeader"), TEXT("0x103a9340") },
		{ TEXT("CNPC_VTzimisce"), TEXT("0x103ba780") },
		{ TEXT("CNPC_VWerewolf"), TEXT("0x103d7ce0") },
		{ TEXT("CNPC_VAndreiBlood"), TEXT("0x1035db00") },
		{ TEXT("CNPC_VAsianVampire"), TEXT("0x10361470") },
		{ TEXT("CNPC_VChangBros"), TEXT("0x1036c6a0") },
		{ TEXT("CNPC_VSheriffMan"), TEXT("0x103af810") },
		{ TEXT("CNPC_VZombie"), TEXT("0x103e03b0") },
		{ TEXT("CNPC_VBach"), TEXT("0x10365800") },
		{ TEXT("CNPC_VChangBrosBlade"), TEXT("0x1036c6a0") },
		{ TEXT("CNPC_VChangBrosClaw"), TEXT("0x1036c6a0") },
	};
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_hints_566"), 566);
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Species); ++Index)
	{
		Builder.AddNpcOfClass(Species[Index].Class, FVector(300.0 * Index, 0.0, 0.0),
			Species[Index].Class);
	}
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	for (const FSpecies& Row : Species)
	{
		FElysiumNpc* Npc = Fixture.Npc(Row.Class);
		if (!TestNotNull(*FString::Printf(TEXT("%s stood through its factory"), Row.Class), Npc))
		{
			return false;
		}
		FElysiumNpcWorldFixture::Quiet({ Npc });
		TestEqual(*FString::Printf(TEXT("%s's slot-566 body"), Row.Class),
			FString(ElysiumNpcTestCensus::BodyOf(Npc->RetailClass(), 566)), FString(Row.Body));
	}
	// The hint's group gate is the base body's alone; every hint here passes it, so a base answer
	// is the type switch's.
	auto Ask = [&Fixture](const TCHAR* Cls, int32 Type)
	{
		FElysiumNpcBase::FHintWords Hint = MakeHint(Type);
		Hint.GroupMask = 1;
		return Fixture.Npc(Cls)->FValidateHintType(&Hint);
	};

	// `CNPC_VBach` (`0x10365800`), story 29d family Senses10.
	TestTrue(TEXT("0x10365800 accepts 17000..17005 outright"),
		Ask(TEXT("CNPC_VBach"), 17000) && Ask(TEXT("CNPC_VBach"), 17005));
	TestFalse(TEXT("...and 16999 / 17006 fall through to the base, whose switch refuses them"),
		Ask(TEXT("CNPC_VBach"), 16999) || Ask(TEXT("CNPC_VBach"), 17006));
	TestTrue(TEXT("...and a type the base accepts (0x2774) FALLS THROUGH to it and is accepted"),
		Ask(TEXT("CNPC_VBach"), 0x2774));
	TestFalse(TEXT("no other body falls through to the base"), Ask(TEXT("CNPC_VWerewolf"), 0x2774));

	// `CNPC_VDog` (`0x10374aa0`): `== 12000`.
	TestTrue(TEXT("VDog accepts 12000"), Ask(TEXT("CNPC_VDog"), 12000));
	TestFalse(TEXT("VDog rejects 12001"), Ask(TEXT("CNPC_VDog"), 12001));

	// `CNPC_VSabbatLeader` (`0x103a9340`): `15999 < t && t < 0x3e86` — 16000..16005.
	TestFalse(TEXT("SabbatLeader rejects 15999"), Ask(TEXT("CNPC_VSabbatLeader"), 15999));
	TestTrue(TEXT("SabbatLeader accepts 16000"), Ask(TEXT("CNPC_VSabbatLeader"), 16000));
	TestTrue(TEXT("SabbatLeader accepts 16005"), Ask(TEXT("CNPC_VSabbatLeader"), 16005));
	TestFalse(TEXT("SabbatLeader rejects 16006"), Ask(TEXT("CNPC_VSabbatLeader"), 16006));

	// `CNPC_VTzimisce` (`0x103ba780`): `13999 < t && t < 0x36b2` — 14000..14001 only.
	TestFalse(TEXT("VTzimisce rejects 13999"), Ask(TEXT("CNPC_VTzimisce"), 13999));
	TestTrue(TEXT("VTzimisce accepts 14000"), Ask(TEXT("CNPC_VTzimisce"), 14000));
	TestTrue(TEXT("VTzimisce accepts 14001"), Ask(TEXT("CNPC_VTzimisce"), 14001));
	TestFalse(TEXT("VTzimisce rejects 14002"), Ask(TEXT("CNPC_VTzimisce"), 14002));
	TestFalse(TEXT("and VTzimisce, the one body that null-checks its hint, refuses a null hint"),
		Fixture.Npc(TEXT("CNPC_VTzimisce"))->FValidateHintType(nullptr));

	// `CNPC_VWerewolf` (`0x103d7ce0`): `t != 0x3a9f && 14999 < t && t < 0x3aab`.
	TestFalse(TEXT("VWerewolf rejects 14999"), Ask(TEXT("CNPC_VWerewolf"), 14999));
	TestTrue(TEXT("VWerewolf accepts 15000"), Ask(TEXT("CNPC_VWerewolf"), 15000));
	TestFalse(TEXT("VWerewolf rejects the excluded 15007 (0x3a9f)"),
		Ask(TEXT("CNPC_VWerewolf"), 15007));
	TestTrue(TEXT("VWerewolf accepts 15008 either side of it"),
		Ask(TEXT("CNPC_VWerewolf"), 15008));
	TestTrue(TEXT("VWerewolf accepts the top of the range, 15018 (0x3aaa)"),
		Ask(TEXT("CNPC_VWerewolf"), 15018));
	TestFalse(TEXT("VWerewolf rejects 15019"), Ask(TEXT("CNPC_VWerewolf"), 15019));

	// The constant bodies.
	TestTrue(TEXT("VAndreiBlood accepts anything"), Ask(TEXT("CNPC_VAndreiBlood"), -1));
	TestTrue(TEXT("VAsianVampire accepts anything"), Ask(TEXT("CNPC_VAsianVampire"), 99999));
	TestTrue(TEXT("VChangBros accepts anything"), Ask(TEXT("CNPC_VChangBros"), 0));
	TestTrue(TEXT("VSheriffMan accepts anything"), Ask(TEXT("CNPC_VSheriffMan"), 700));
	TestFalse(TEXT("VZombie accepts nothing"), Ask(TEXT("CNPC_VZombie"), 700));

	// The blade and claw brothers reach `0x1036c6a0` by inheriting `FElysiumNpcChangBros`'s override.
	TestTrue(TEXT("CNPC_VChangBrosBlade inherits the ChangBros body"),
		Ask(TEXT("CNPC_VChangBrosBlade"), 42));
	TestTrue(TEXT("CNPC_VChangBrosClaw inherits it too"), Ask(TEXT("CNPC_VChangBrosClaw"), 42));
	return true;
}

// -------------------------------------------------------------------------------------------------
// The five hint-node activity lookups on the schedule host.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelHintsActivityQueryTest,
	"Elysium.Arm.NpcKernelHints.HintActivityQueries", GHintsTestFlags)
bool FElysiumNpcKernelHintsActivityQueryTest::RunTest(const FString&)
{
	using EQ = EElysiumHintActivityQuery;
	auto Ask = [](EQ Q, int32 Type, bool bLean)
	{
		return FElysiumNpcScheduleHost::HintNodeActivity(Q, Type, bLean);
	};

	// `0x102a13d0` — 0x1110 / 0x110c / 0x111e, lean-adjusted at 0x27d8.
	TestEqual(TEXT("0x102a13d0 type 100"), Ask(EQ::Query102a13d0, 100, false), 0x1110);
	TestEqual(TEXT("0x102a13d0 type 0x65"), Ask(EQ::Query102a13d0, 0x65, false), 0x110c);
	TestEqual(TEXT("0x102a13d0 type 0x27d8 upright"), Ask(EQ::Query102a13d0, 0x27d8, false), 0x111e);
	TestEqual(TEXT("0x102a13d0 type 0x27d8 leaning left"),
		Ask(EQ::Query102a13d0, 0x27d8, true), 0x111d);
	TestEqual(TEXT("0x102a13d0 leaning does not move the 100 answer"),
		Ask(EQ::Query102a13d0, 100, true), 0x1110);

	// `0x102a1420` — 0x1111 / 0x110d / 0x1118, and NO lean adjustment.
	TestEqual(TEXT("0x102a1420 type 100"), Ask(EQ::Query102a1420, 100, false), 0x1111);
	TestEqual(TEXT("0x102a1420 type 0x65"), Ask(EQ::Query102a1420, 0x65, false), 0x110d);
	TestEqual(TEXT("0x102a1420 type 0x27d8 upright"), Ask(EQ::Query102a1420, 0x27d8, false), 0x1118);
	TestEqual(TEXT("0x102a1420 type 0x27d8 leaning — unchanged"),
		Ask(EQ::Query102a1420, 0x27d8, true), 0x1118);

	// `0x102a1470` — 0x1112 / 0x110e / 0x111a, lean-adjusted.
	TestEqual(TEXT("0x102a1470 type 100"), Ask(EQ::Query102a1470, 100, false), 0x1112);
	TestEqual(TEXT("0x102a1470 type 0x65"), Ask(EQ::Query102a1470, 0x65, false), 0x110e);
	TestEqual(TEXT("0x102a1470 type 0x27d8 leaning"), Ask(EQ::Query102a1470, 0x27d8, true), 0x1119);

	// `0x102a14c0` — 0x1113 / 0x3f / 0x111c. Note the 0x3f: the one answer in the five tables that
	// is not in the 0x110x–0x112x band, and it is reproduced rather than corrected.
	TestEqual(TEXT("0x102a14c0 type 100"), Ask(EQ::Query102a14c0, 100, false), 0x1113);
	TestEqual(TEXT("0x102a14c0 type 0x65 answers 0x3f"), Ask(EQ::Query102a14c0, 0x65, false), 0x3f);
	TestEqual(TEXT("0x102a14c0 type 0x27d8 leaning"), Ask(EQ::Query102a14c0, 0x27d8, true), 0x111b);

	// `0x102a1510` — the same 100/0x65 pair as `0x102a1420`, a different 0x27d8, no lean.
	TestEqual(TEXT("0x102a1510 type 100 matches 0x102a1420's"),
		Ask(EQ::Query102a1510, 100, false), 0x1111);
	TestEqual(TEXT("0x102a1510 type 0x65 matches it too"),
		Ask(EQ::Query102a1510, 0x65, false), 0x110d);
	TestEqual(TEXT("but 0x102a1510 type 0x27d8 answers 0x55"),
		Ask(EQ::Query102a1510, 0x27d8, false), 0x55);
	TestEqual(TEXT("and does not lean-adjust"), Ask(EQ::Query102a1510, 0x27d8, true), 0x55);

	// Every query answers -1 for a type outside the three, which is also its no-hint answer.
	for (const EQ Q : { EQ::Query102a13d0, EQ::Query102a1420, EQ::Query102a1470, EQ::Query102a14c0,
			EQ::Query102a1510 })
	{
		TestEqual(TEXT("an unlisted hint type answers -1"), Ask(Q, 12345, false), INDEX_NONE);
		TestEqual(TEXT("and so does no hint node at all"), Ask(Q, INDEX_NONE, true), INDEX_NONE);
	}
	return true;
}

// -------------------------------------------------------------------------------------------------
// The pure hint rules.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelHintsRulesTest,
	"Elysium.Arm.NpcKernelHints.HintRules", GHintsTestFlags)
bool FElysiumNpcKernelHintsRulesTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("kernel_hints_rules"), 29031u);
	Builder.AddNpcOfClass(TEXT("wolf"), FVector::ZeroVector, TEXT("CNPC_VWerewolf"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpcWerewolf* Npc = Fixture.NpcAs<FElysiumNpcWerewolf>(TEXT("wolf"));
	if (!TestNotNull(TEXT("the fixture NPC spawned"), Npc))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Npc });

	// `GetHintTeleportPriority` (`0x103d3220`) — the whole map, including the two types that share
	// priority 3 and the unlisted zero.
	TestEqual(TEXT("15001 -> 2"), FElysiumNpcWerewolf::GetHintTeleportPriority(0x3a99), 2);
	TestEqual(TEXT("15002 -> 3"), FElysiumNpcWerewolf::GetHintTeleportPriority(0x3a9a), 3);
	TestEqual(TEXT("15003 -> 3 as well"), FElysiumNpcWerewolf::GetHintTeleportPriority(0x3a9b), 3);
	TestEqual(TEXT("15000 -> 1"), FElysiumNpcWerewolf::GetHintTeleportPriority(15000), 1);
	TestEqual(TEXT("15007 -> 4, the highest"), FElysiumNpcWerewolf::GetHintTeleportPriority(0x3a9f), 4);
	TestEqual(TEXT("anything else -> 0"), FElysiumNpcWerewolf::GetHintTeleportPriority(0x3aa0), 0);

	// `IsHintUnusable` (`0x102d14c0`) — three arms in retail's order.
	{
		FElysiumNpcBase::FHintWords Hint = MakeHint(0x3aa3);
		TestFalse(TEXT("a plain hint is usable"), FElysiumNpcBase::IsHintUnusable(Hint, 10.0, false));
		Hint.Disabled = 1;
		TestTrue(TEXT("m_iDisabled makes it unusable"),
			FElysiumNpcBase::IsHintUnusable(Hint, 10.0, false));
		Hint.Disabled = 0;
		Hint.NextUseTime = 20.0;
		TestTrue(TEXT("curtime before m_flNextUseTime makes it unusable"),
			FElysiumNpcBase::IsHintUnusable(Hint, 10.0, false));
		TestFalse(TEXT("and at the deadline exactly it is usable again — retail's test is `<`"),
			FElysiumNpcBase::IsHintUnusable(Hint, 20.0, false));
		Hint.NextUseTime = 0.0;
		TestTrue(TEXT("a live m_hHintOwner makes it unusable"),
			FElysiumNpcBase::IsHintUnusable(Hint, 10.0, true));
	}

	// `IsValidBreakHint` (`0x103d8550`) — null, unusable, then type 0x3aa3 exactly.
	{
		FElysiumNpcBase::FHintWords None;
		TestFalse(TEXT("a null hint is not a break hint"), Npc->IsValidBreakHint(None, 10.0));
		FElysiumNpcBase::FHintWords Hint = MakeHint(0x3aa3);
		TestTrue(TEXT("type 0x3aa3 is"), Npc->IsValidBreakHint(Hint, 10.0));
		Hint.HintType = 0x3aa2;
		TestFalse(TEXT("the type either side is not"), Npc->IsValidBreakHint(Hint, 10.0));
		Hint.HintType = 0x3aa3;
		Hint.Disabled = 1;
		TestFalse(TEXT("and an unusable hint is rejected before the type is even looked at"),
			Npc->IsValidBreakHint(Hint, 10.0));
	}

	// `SelectScheduleForHint` (`0x103ce9b0`) — three typed answers, then the distance test.
	{
		TestEqual(TEXT("no hint -> schedule 0x163"),
			FElysiumNpcWerewolf::SelectScheduleForHint(nullptr, 0.f, 0.f), 0x163);
		FElysiumNpcBase::FHintWords Hint = MakeHint(0x3aa4);
		TestEqual(TEXT("15012 -> 0x15f"), FElysiumNpcWerewolf::SelectScheduleForHint(&Hint, 0.f, 0.f), 0x15f);
		Hint.HintType = 0x3aa5;
		TestEqual(TEXT("15013 -> 0x160"), FElysiumNpcWerewolf::SelectScheduleForHint(&Hint, 0.f, 0.f), 0x160);
		Hint.HintType = 0x3aaa;
		TestEqual(TEXT("15018 -> 0x164"), FElysiumNpcWerewolf::SelectScheduleForHint(&Hint, 0.f, 0.f), 0x164);
		Hint.HintType = 0x3aa3;   // an unlisted type falls to the distance test
		TestEqual(TEXT("beyond the goal tolerance -> 0x15a"),
			FElysiumNpcWerewolf::SelectScheduleForHint(&Hint, 33.0f, 32.0f), 0x15a);
		TestEqual(TEXT("at the tolerance exactly -> 0x15b, because retail's test is `>`"),
			FElysiumNpcWerewolf::SelectScheduleForHint(&Hint, 32.0f, 32.0f), 0x15b);
		TestEqual(TEXT("inside it -> 0x15b"),
			FElysiumNpcWerewolf::SelectScheduleForHint(&Hint, 1.0f, 32.0f), 0x15b);
	}

	// `SetHintActivity`'s switch (`0x103d6000`). Every case, with both draws.
	{
		auto Act = [](int32 Type, bool bPct, bool bFlip)
		{
			return FElysiumNpcWerewolf::HintActivityForType(Type, bPct, bFlip);
		};
		TestEqual(TEXT("15000 -> 0x10c"), Act(15000, false, false), 0x10c);
		TestEqual(TEXT("0x3a99 -> 0x119"), Act(0x3a99, false, false), 0x119);
		TestEqual(TEXT("0x3a9a -> 0x11a"), Act(0x3a9a, false, false), 0x11a);
		TestEqual(TEXT("0x3a9b -> 0x11b"), Act(0x3a9b, false, false), 0x11b);
		TestEqual(TEXT("0x3a9c -> 0x113 on a failed percentage roll"), Act(0x3a9c, false, false),
			0x113);
		TestEqual(TEXT("0x3a9c -> 0x112 on a passed one"), Act(0x3a9c, true, false), 0x112);
		TestEqual(TEXT("0x3a9d -> 0x115 / 0x114"), Act(0x3a9d, false, false), 0x115);
		TestEqual(TEXT("0x3a9d passed"), Act(0x3a9d, true, false), 0x114);
		TestEqual(TEXT("0x3a9e -> 0x117 / 0x116"), Act(0x3a9e, false, false), 0x117);
		TestEqual(TEXT("0x3a9e passed"), Act(0x3a9e, true, false), 0x116);
		TestEqual(TEXT("0x3a9f -> 0x10d"), Act(0x3a9f, true, true), 0x10d);
		TestEqual(TEXT("0x3aa0 -> 0x10f"), Act(0x3aa0, false, false), 0x10f);
		TestEqual(TEXT("0x3aa1 -> 0x110"), Act(0x3aa1, false, false), 0x110);
		TestEqual(TEXT("0x3aa2 -> 0x10e"), Act(0x3aa2, false, false), 0x10e);
		TestEqual(TEXT("0x3aa3 -> 0x111"), Act(0x3aa3, false, false), 0x111);
		TestEqual(TEXT("0x3aa6 -> 0x122"), Act(0x3aa6, false, false), 0x122);
		TestEqual(TEXT("0x3aa7 -> 0x123"), Act(0x3aa7, false, false), 0x123);
		TestEqual(TEXT("0x3aa8 -> 0x10b"), Act(0x3aa8, false, false), 0x10b);
		TestEqual(TEXT("0x3aa9 -> 0x125 on a zero coin flip"), Act(0x3aa9, true, false), 0x125);
		TestEqual(TEXT("0x3aa9 -> 0x124 on a one — the coin, NOT the percentage"),
			Act(0x3aa9, false, true), 0x124);
		TestEqual(TEXT("0x3aaa -> 0x121"), Act(0x3aaa, false, false), 0x121);
		TestEqual(TEXT("0x3aa4 is NOT in the switch — it falls to the default refusal"),
			Act(0x3aa4, false, false), INDEX_NONE);
		TestEqual(TEXT("and so does 0x3aa5"), Act(0x3aa5, true, true), INDEX_NONE);
	}

	return true;
}

// -------------------------------------------------------------------------------------------------
// The Boss's centre-line geometry and the Chang jump path.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelHintsGeometryTest,
	"Elysium.Arm.NpcKernelHints.Geometry", GHintsTestFlags)
bool FElysiumNpcKernelHintsGeometryTest::RunTest(const FString&)
{
	// `DistToHintCenterLine2D_3` (`0x103c6680`) — a point-to-INFINITE-LINE distance, not a segment
	// one: `t` is never clamped, so a point behind the line start still projects onto the line.
	{
		const FVector Start(0.0, 0.0, 0.0);
		const FVector Dir(1.0, 0.0, 0.0);
		TestEqual(TEXT("a point 5 to the side is 5 from the line"),
			FElysiumNpcSabbatLeader::DistToHintCenterLine2D_3(Start, Dir, FVector(3.0, 5.0, 0.0)), 5.0f,
			KINDA_SMALL_NUMBER);
		TestEqual(TEXT("a point ON the line is 0 from it"),
			FElysiumNpcSabbatLeader::DistToHintCenterLine2D_3(Start, Dir, FVector(100.0, 0.0, 0.0)), 0.0f,
			KINDA_SMALL_NUMBER);
		TestEqual(TEXT("a point BEHIND the start still projects — no clamp"),
			FElysiumNpcSabbatLeader::DistToHintCenterLine2D_3(Start, Dir, FVector(-100.0, 4.0, 0.0)), 4.0f,
			KINDA_SMALL_NUMBER);
		TestEqual(TEXT("and Z is ignored, because _DAT_104454c4 is 0.0 and the caller zeroes Dir.Z"),
			FElysiumNpcSabbatLeader::DistToHintCenterLine2D_3(Start, Dir, FVector(3.0, 5.0, 900.0)), 5.0f,
			KINDA_SMALL_NUMBER);
	}

	// `DistToHintCenterLine2D_2` (`0x103c6570`) — the hint's own origin and facing as the line, both
	// flattened. A hint at the origin facing +X (yaw 0) is the case above.
	{
		FElysiumNpcBase::FHintWords Hint = MakeHint(15000);
		Hint.OriginCm = FVector(0.0, 0.0, 700.0);   // the Z is thrown away
		Hint.Angles = FVector(0.0, 0.0, 0.0);
		TestEqual(TEXT("the hint's Z does not reach the answer"),
			FElysiumNpcSabbatLeader::DistToHintCenterLine2D(Hint, FVector(3.0, 5.0, 0.0)), 5.0f,
			KINDA_SMALL_NUMBER);
		// A hint pitched steeply still measures flat, because the forward's Z is zeroed and the
		// remainder re-normalised before the projection.
		Hint.Angles = FVector(80.0, 0.0, 0.0);
		TestEqual(TEXT("and a steep pitch does not shrink the flattened direction"),
			FElysiumNpcSabbatLeader::DistToHintCenterLine2D(Hint, FVector(3.0, 5.0, 0.0)), 5.0f,
			KINDA_SMALL_NUMBER);
	}

	// `DistToSegment` — the clamped form `CheckJumpPathToHintNode` tests against.
	{
		const FVector A(0.0, 0.0, 0.0);
		const FVector B(10.0, 0.0, 0.0);
		TestEqual(TEXT("beside the segment"), FElysiumNpcChangBros::DistToSegment(A, B, FVector(5.0, 4.0, 0.0)),
			4.0f, KINDA_SMALL_NUMBER);
		TestEqual(TEXT("past the far end it CLAMPS, unlike the centre-line body"),
			FElysiumNpcChangBros::DistToSegment(A, B, FVector(20.0, 0.0, 0.0)), 10.0f, KINDA_SMALL_NUMBER);
		// The degenerate arm answers `_DAT_104454c4`, the shared 0.0f — NOT the distance to the
		// point. For `CheckJumpPathToHintNode`, whose test is `dist < threshold`, a zero-length
		// segment therefore always reads as "something is on the line".
		TestEqual(TEXT("a degenerate segment answers 0.0, not the distance to its point"),
			FElysiumNpcChangBros::DistToSegment(A, A, FVector(0.0, 3.0, 0.0)), 0.0f, KINDA_SMALL_NUMBER);
		TestEqual(TEXT("before the near end it CLAMPS to that end"),
			FElysiumNpcChangBros::DistToSegment(A, B, FVector(-6.0, 8.0, 0.0)), 10.0f, KINDA_SMALL_NUMBER);
		TestEqual(TEXT("and Z reaches the answer: this is a 3D distance"),
			FElysiumNpcChangBros::DistToSegment(A, B, FVector(5.0, 0.0, 12.0)), 12.0f, KINDA_SMALL_NUMBER);
	}
	return true;
}

// -------------------------------------------------------------------------------------------------
// The Werewolf's hint state, and the two ring-buffer / group-key writes.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelHintsWerewolfTest,
	"Elysium.Arm.NpcKernelHints.WerewolfHints", GHintsTestFlags)
bool FElysiumNpcKernelHintsWerewolfTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("kernel_hints_werewolf"), 29031u);
	Builder.AddNpcOfClass(TEXT("wolf"), FVector::ZeroVector, TEXT("CNPC_VWerewolf"));
	Builder.AddNpcOfClass(TEXT("asian"), FVector(0.0, 800.0, 0.0), TEXT("CNPC_VAsianVampire"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpcWerewolf* Npc = Fixture.NpcAs<FElysiumNpcWerewolf>(TEXT("wolf"));
	FElysiumNpcAsianVampire* Asian = Fixture.NpcAs<FElysiumNpcAsianVampire>(TEXT("asian"));
	if (!TestNotNull(TEXT("the AsianVampire spawned"), Asian))
	{
		return false;
	}
	if (!TestNotNull(TEXT("the fixture NPC spawned"), Npc))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Npc });
	const EElysiumNpcCond MoveHintCond = static_cast<EElysiumNpcCond>(0x78);

	// `SetMoveHint` (`0x103d44e0`) / `ClearMoveHint` (`0x103d4690`).
	Npc->SetMoveHint(7, /*bRandom=*/true);
	TestEqual(TEXT("SetMoveHint installs the hint"), Npc->MoveHintNode, 7);
	TestTrue(TEXT("and writes m_bRandomHint"), Npc->bRandomHint);
	TestTrue(TEXT("and raises condition 0x78"), Npc->Cognition.Conditions.Has(MoveHintCond));

	Npc->SetMoveHint(9, /*bRandom=*/false);
	TestEqual(TEXT("a second SetMoveHint replaces the first"), Npc->MoveHintNode, 9);
	TestFalse(TEXT("and overwrites m_bRandomHint"), Npc->bRandomHint);
	TestTrue(TEXT("0x78 still stands: ClearMoveHint drops it and SetMoveHint raises it again"),
		Npc->Cognition.Conditions.Has(MoveHintCond));

	Npc->ClearMoveHint();
	TestEqual(TEXT("ClearMoveHint releases the hint"), Npc->MoveHintNode, int32(INDEX_NONE));
	TestFalse(TEXT("and clears 0x78"), Npc->Cognition.Conditions.Has(MoveHintCond));
	Npc->ClearMoveHint();
	TestFalse(TEXT("the 0x78 clear is unconditional — it runs with no hint installed too"),
		Npc->Cognition.Conditions.Has(MoveHintCond));

	// `SetTeleportHint` (`0x103d45c0`) / `ClearTeleportHint` (`0x103d4760`). The two hint kinds
	// SHARE `m_bRandomHint`, which is why setting a teleport hint un-randomises a move hint.
	Npc->SetMoveHint(3, /*bRandom=*/true);
	Npc->SetTeleportHint(11);
	TestEqual(TEXT("SetTeleportHint installs"), Npc->TeleportHintNode, 11);
	TestFalse(TEXT("and clears the shared m_bRandomHint"), Npc->bRandomHint);
	TestEqual(TEXT("without touching the move hint"), Npc->MoveHintNode, 3);
	TestTrue(TEXT("and without touching condition 0x78"),
		Npc->Cognition.Conditions.Has(MoveHintCond));
	Npc->ClearTeleportHint();
	TestEqual(TEXT("ClearTeleportHint releases"), Npc->TeleportHintNode, int32(INDEX_NONE));
	TestTrue(TEXT("and still leaves 0x78 alone"), Npc->Cognition.Conditions.Has(MoveHintCond));

	// `AddHintToStoredJumpPositions` (`0x10361990`) — a two-slot ring that wraps at index > 1. The
	// ring itself is family Motor's `LastJumpPosition` pair, in SOURCE UNITS, so the origins below
	// are whole-unit multiples of the 2.54 cm scale.
	{
		Asian->LastJumpPositionIdx = 0;
		FElysiumNpcBase::FHintWords A = MakeHint(0);
		A.OriginCm = FVector(100.0 * ElysiumMove::U, 0.0, 0.0);
		FElysiumNpcBase::FHintWords B = MakeHint(0);
		B.OriginCm = FVector(200.0 * ElysiumMove::U, 0.0, 0.0);
		FElysiumNpcBase::FHintWords C = MakeHint(0);
		C.OriginCm = FVector(300.0 * ElysiumMove::U, 0.0, 0.0);
		Asian->AddHintToStoredJumpPositions(A);
		TestEqual(TEXT("slot 0 takes the first"), Asian->LastJumpPosition[0].X, 100.0, 0.01);
		TestEqual(TEXT("and the index advances"), Asian->LastJumpPositionIdx, 1);
		Asian->AddHintToStoredJumpPositions(B);
		TestEqual(TEXT("slot 1 takes the second"), Asian->LastJumpPosition[1].X, 200.0, 0.01);
		TestEqual(TEXT("and the index wraps at 2"), Asian->LastJumpPositionIdx, 0);
		Asian->AddHintToStoredJumpPositions(C);
		TestEqual(TEXT("the third overwrites the first"), Asian->LastJumpPosition[0].X, 300.0, 0.01);
		FElysiumNpcBase::FHintWords None;
		Asian->AddHintToStoredJumpPositions(None);
		TestEqual(TEXT("and a null hint writes nothing and does not advance"),
			Asian->LastJumpPositionIdx, 1);
	}

	// `IsImperativeTeleportHint` (`0x103d3360`) — every gate, by flag and by authored name.
	{
		FElysiumNpcBase::FHintWords Hint = MakeHint(0x3aa9);
		Hint.Name = TEXT("cheater_outside_hint_2");
		Npc->WerewolfHintFlags = 0;
		TestFalse(TEXT("with no flag set, nothing is imperative"),
			Npc->IsImperativeTeleportHint(Hint));
		Npc->WerewolfHintFlags = 0x200;
		TestTrue(TEXT("flag 0x200 admits cheater_outside_hint_2 at type 0x3aa9"),
			Npc->IsImperativeTeleportHint(Hint));
		Hint.Name = TEXT("cheater_outside_hint_3");
		TestFalse(TEXT("but not cheater_outside_hint_3, which is flag 0x400's"),
			Npc->IsImperativeTeleportHint(Hint));
		Npc->WerewolfHintFlags = 0x400;
		TestTrue(TEXT("flag 0x400 admits it"), Npc->IsImperativeTeleportHint(Hint));

		Npc->WerewolfHintFlags = 0x200;
		Hint.HintType = 15000;
		Hint.Name = TEXT("shard_hint_3");
		TestTrue(TEXT("flag 0x200's second arm is shard_hint_3 at type 15000"),
			Npc->IsImperativeTeleportHint(Hint));
		Hint.HintType = 0x3aa9;
		TestFalse(TEXT("and the type is part of the gate, not decoration"),
			Npc->IsImperativeTeleportHint(Hint));

		// Flag 0x4 splits on the door state.
		Npc->WerewolfHintFlags = 0x4;
		Npc->WerewolfDoorState = 2;
		Hint.HintType = 0x3aa9;
		Hint.Name = TEXT("cheater_outside_hint_1");
		TestTrue(TEXT("door state 2 admits cheater_outside_hint_1"),
			Npc->IsImperativeTeleportHint(Hint));
		Npc->WerewolfDoorState = 3;
		TestTrue(TEXT("door state 3 does too"), Npc->IsImperativeTeleportHint(Hint));
		Npc->WerewolfDoorState = 0;
		TestFalse(TEXT("any other door state takes the OTHER pair of arms"),
			Npc->IsImperativeTeleportHint(Hint));
		Hint.HintType = 15000;
		Hint.Name = TEXT("skylight_2_teleports");
		TestTrue(TEXT("which admits skylight_2_teleports at 15000"),
			Npc->IsImperativeTeleportHint(Hint));
		Hint.HintType = 0x3a99;
		Hint.Name = TEXT("fulldoor_a_3_breakthrough_f");
		TestTrue(TEXT("and fulldoor_a_3_breakthrough_f at 0x3a99"),
			Npc->IsImperativeTeleportHint(Hint));

		// The one arm with a trace behind it: slot 617 (`0x103da230`) at the hint's origin
		// (`0x103d38a7`), and `0x103d38ad TEST AL,AL / JNZ 0x103d38c3` refuses only when the enemy
		// COULD see it. Story 8 lane L12 integration: the slot has its body, and a Werewolf with no
		// enemy falls through to the boss body, which refuses — so the arm is imperative. (The test
		// used to pin the seam's "blocked" answer.)
		Npc->WerewolfDoorState = 2;
		Hint.HintType = 0x3aa4;
		Hint.Name = TEXT("jump_to_platform_hint_1");
		TestTrue(TEXT("0x103d38af the jump_to_platform arm: slot 617 answers false with no enemy, imperative"),
			Npc->IsImperativeTeleportHint(Hint));
		TestFalse(TEXT("0x103da33a slot 617 with no enemy: the boss body refuses"),
			Npc->WerewolfHintTrace(FVector::ZeroVector));
	}

	// `GetHintGroundpoint` (`0x103d6770`) — the authored table, then retail's warn-and-fall-back.
	{
		FElysiumNpcBase::FHintWords Hint = MakeHint(15000);
		Hint.HintIndex = 42;
		Hint.OriginCm = FVector(10.0, 20.0, 30.0);
		Npc->WerewolfHintGroundpoints.Reset();
		// Story 29d, family Hints10 corrected the record: `+0x00` is the cached end entity and the
		// hint is at `+0x04` (`103d6779`). This probe keys on the hint, as the body does.
		Npc->WerewolfHintGroundpoints.Add({ INDEX_NONE, 42, FVector(1.0, 2.0, 3.0) });
		TestEqual(TEXT("a matching row answers its groundpoint, in source units"),
			Npc->GetHintGroundpoint(Hint).X, 1.0, 0.001);
		Npc->WerewolfHintGroundpoints.Reset();
		AddExpectedError(TEXT("Werewolf did not find the hint"),
			EAutomationExpectedErrorFlags::Contains, 0);
		// The miss arm falls into family Motor's `GetGroundpoint` (`0x103d6a40`), whose no-hit answer
		// is `vec3_invalid` — `DAT_10713de0/de4/de8`, which `staticinit_101371a0` fills with
		// `0x7f7fffff`. NOT the hint's own origin, and not zero.
		const FVector Fallback = Npc->GetHintGroundpoint(Hint);
		TestTrue(TEXT("an empty table warns and falls back to GetGroundpoint, which answers "
			"vec3_invalid because no trace ran"), FElysiumNpcWerewolf::IsVec3Invalid(Fallback));
		TestFalse(TEXT("and vec3_invalid is not mistaken for an ordinary point"),
			FElysiumNpcWerewolf::IsVec3Invalid(FVector(1.0, 2.0, 3.0)));

		// `PositionAtHint` therefore declines the move — the port's one named divergence here.
		const FVector Before = Npc->Origin;
		Npc->PositionAtHint(Hint);
		TestTrue(TEXT("PositionAtHint declines a vec3_invalid groundpoint rather than writing "
			"FLT_MAX into the origin"), Npc->Origin.Equals(Before, 0.001));

		// An authored row moves it for real, scaled out of source units.
		Npc->WerewolfHintGroundpoints.Add({ INDEX_NONE, 42, FVector(100.0, 0.0, 0.0) });
		Npc->PositionAtHint(Hint);
		TestEqual(TEXT("an authored groundpoint places the body at it"), Npc->Origin.X,
			100.0 * ElysiumMove::U, 0.01);
		Npc->WerewolfHintGroundpoints.Reset();
	}

	// `SetHintGroup` (`0x102781e0`) — the write always lands; the slot-551 dispatch only on a change.
	Npc->SetHintGroup(TEXT("alpha"));
	TestEqual(TEXT("SetHintGroup writes m_strHintGroup"), Npc->BaseScheduleHost.HintGroup,
		FString(TEXT("alpha")));
	Npc->SetHintGroup(TEXT("alpha"));
	TestEqual(TEXT("re-setting the same group leaves it"), Npc->BaseScheduleHost.HintGroup,
		FString(TEXT("alpha")));
	Npc->SetHintGroup(TEXT("beta"));
	TestEqual(TEXT("and a change writes the new one"), Npc->BaseScheduleHost.HintGroup,
		FString(TEXT("beta")));
	return true;
}

// -------------------------------------------------------------------------------------------------
// The hint-idle activity and the interest loop.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelHintsInterestTest,
	"Elysium.Arm.NpcKernelHints.Interest", GHintsTestFlags)
bool FElysiumNpcKernelHintsInterestTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("kernel_hints_interest"), 29031u);
	Builder.AddNpc(TEXT("loiterer"));
	Builder.AddEntity(TEXT("intersting_place"), TEXT("spot"), FVector(100.0, 0.0, 0.0));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("loiterer"));
	if (!TestNotNull(TEXT("the fixture NPC spawned"), Npc))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Npc });
	FElysiumEntity* SpotEntity = Fixture.World.FindByName(TEXT("spot"));
	FElysiumInterestingPlace* Spot = static_cast<FElysiumInterestingPlace*>(SpotEntity);
	if (!TestNotNull(TEXT("the interesting place spawned"), Spot))
	{
		return false;
	}

	// `PlayHintIdleActivity` (`0x102aaa60`). The stamp is written first and on every arm.
	Npc->LastAttackTime = 0.0;
	Npc->Cognition.Conditions.Clear(static_cast<EElysiumNpcCond>(99));
	TestTrue(TEXT("with no hint and no condition 99, the fallback restarts ACT 0x19"),
		Npc->PlayHintIdleActivity(12.5));
	TestEqual(TEXT("and m_flLastAttackTime (+0x5d9c) was stamped with curtime"),
		Npc->LastAttackTime, 12.5, 0.001);
	Npc->Cognition.Conditions.Set(static_cast<EElysiumNpcCond>(99));
	TestFalse(TEXT("with condition 99 standing, nothing is restarted"),
		Npc->PlayHintIdleActivity(20.0));
	TestEqual(TEXT("but the stamp still landed"), Npc->LastAttackTime, 20.0, 0.001);

	// `ClaimInterestingPlace` (`0x102a9f40`). The place's type is not in the shipped table on a bare
	// test world, so both activity names are empty — which is retail's "no INTO activity" arm, and
	// its DevWarning fallback beneath it.
	// Only the two interest bodies' own DevWarnings are expected here. The type table's one-shot
	// "unavailable" warning is NOT: it fires at most once per process and only when the export
	// corpus is missing, so expecting it makes the case depend on which suite ran first.
	AddExpectedError(TEXT("Can not find interest"), EAutomationExpectedErrorFlags::Contains, 0);
	Spot->MinTime = 4.0f;
	Spot->MarkersAllocated = 1; Spot->Markers.SetNum(1); Spot->MarkersUsed = 1;
	Spot->Markers[0].Occupant = Npc->Handle; // already-reserved 0x102da860 prerequisite
	Spot->MaxTime = 4.0f;   // a degenerate range pins the deadline exactly
	Npc->ClaimInterestingPlace(Spot, /*bClaimSecondary=*/false, 100.0);
	TestEqual(TEXT("the wait deadline is curtime + RandomFloat(min_time, max_time)"),
		Npc->BaseScheduleHost.WaitFinished, 104.0, 0.001);
	TestTrue(TEXT("the place is claimed"), Spot->IsEnabledFor(Npc->Handle));
	TestFalse(TEXT("no INTO activity means INTERESTING_INTO is NOT raised"),
		Npc->NpcFlags.Has(EElysiumNpcFlag::INTERESTING_INTO));
	TestEqual(TEXT("and the phase is the idle dwell (retail +0x6304 == 2)"),
		Npc->GetAmbientPhaseForDebug(), 2);

	// `RunInterestingPlaceLoop` (`0x102aa210`). With no INTO flag standing, reaching the deadline
	// finishes the task outright rather than entering an OUTOF phase.
	TestFalse(TEXT("before the deadline the loop does not finish"),
		Npc->RunInterestingPlaceLoop(Spot, 103.0));
	TestTrue(TEXT("at the deadline, and with no INTERESTING_INTO, the task is finished"),
		Npc->RunInterestingPlaceLoop(Spot, 104.0));

	// With INTERESTING_INTO standing, the same deadline enters the OUTOF phase instead.
	Npc->NpcFlags.Set(EElysiumNpcFlag::INTERESTING_INTO);
	TestFalse(TEXT("with INTERESTING_INTO the deadline does NOT finish the task"),
		Npc->RunInterestingPlaceLoop(Spot, 110.0));
	TestEqual(TEXT("it moves to the OUTOF phase (retail +0x6304 == 3)"),
		Npc->GetAmbientPhaseForDebug(), 3);
	// The OUTOF clip ends on the live `m_bSequenceFinished` (+0x65c, `0x102aa25b`) of a non-looping
	// sequence (+0x65d clear, `0x102aa235`): the INTO flag is released and the wait ends now.
	Npc->bSequenceLoopedOnce = false;
	Npc->bSequenceFinished = false;
	Npc->RunInterestingPlaceLoop(Spot, 111.0);
	TestTrue(TEXT("0x102aa25b: an OUTOF still playing keeps INTERESTING_INTO"),
		Npc->NpcFlags.Has(EElysiumNpcFlag::INTERESTING_INTO));
	Npc->bSequenceFinished = true;
	Npc->RunInterestingPlaceLoop(Spot, 112.0);
	TestFalse(TEXT("0x102aa25b: a finished OUTOF releases INTERESTING_INTO"),
		Npc->NpcFlags.Has(EElysiumNpcFlag::INTERESTING_INTO));

	// `ResolvePatrolInterestPlace` (`0x1029f780`) — the cache at `+0x6300`, and the seam under it.
	Npc->ScheduleHost.Unknown6300 = 0;
	TestEqual(TEXT("with no patrol-node record the resolve caches nothing"),
		Npc->ResolvePatrolInterestPlace(0), 0);
	Npc->ScheduleHost.Unknown6300 = 77;
	TestEqual(TEXT("and a filled cache is returned without re-resolving"),
		Npc->ResolvePatrolInterestPlace(0), 77);
	return true;
}

// -------------------------------------------------------------------------------------------------
// The seams. Each is asked, and the refusal it answers is the recovered one.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelHintsSeamTest,
	"Elysium.Arm.NpcKernelHints.Seams", GHintsTestFlags)
bool FElysiumNpcKernelHintsSeamTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("kernel_hints_seams"), 29031u);
	Builder.AddNpc(TEXT("sabbat"), FVector::ZeroVector, TEXT("npc_VSabbatLeader"));
	Builder.AddNpcOfClass(TEXT("tzimisce"), FVector(400.0, 0.0, 0.0), TEXT("CNPC_VTzimisce"));
	Builder.AddNpcOfClass(TEXT("wolf"), FVector(800.0, 0.0, 0.0), TEXT("CNPC_VWerewolf"));
	Builder.AddNpcOfClass(TEXT("chang"), FVector(0.0, 800.0, 0.0), TEXT("CNPC_VChangBros"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("sabbat"));
	FElysiumNpcTzimisce* Tzim = Fixture.NpcAs<FElysiumNpcTzimisce>(TEXT("tzimisce"));
	FElysiumNpcWerewolf* Wolf = Fixture.NpcAs<FElysiumNpcWerewolf>(TEXT("wolf"));
	FElysiumNpcChangBros* Chang = Fixture.NpcAs<FElysiumNpcChangBros>(TEXT("chang"));
	if (!TestNotNull(TEXT("the ChangBros spawned"), Chang))
	{
		return false;
	}
	if (!TestNotNull(TEXT("the Tzimisce spawned"), Tzim) || !TestNotNull(TEXT("the werewolf spawned"), Wolf))
	{
		return false;
	}
	if (!TestNotNull(TEXT("the fixture NPC spawned"), Npc))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Npc });
	// `SetHintActivity` below runs `PositionAtHint`, whose groundpoint lookup finds an empty
	// authored table and takes retail's warn-and-fall-back arm.
	AddExpectedError(TEXT("Werewolf did not find the hint"),
		EAutomationExpectedErrorFlags::Contains, 0);

	// The typed hint query — the seam everything else in the family goes through.
	FElysiumNpcBase::FHintWords Words;
	TestFalse(TEXT("HintWords cannot resolve a hint index: there is no hint store"),
		Npc->HintWords(0, Words));
	TestFalse(TEXT("and it leaves the view invalid"), Words.bValid);

	// The searches.
	TestEqual(TEXT("FindHintNear (0x102d1af0) finds nothing"),
		Npc->FindHintNear(15000, 0, 5000.0f), int32(INDEX_NONE));
	TestEqual(TEXT("FindHintOfTypeNear (0x102d24b0) finds nothing"),
		Npc->FindHintOfTypeNear(Npc, 14000, 0, 200.0f), int32(INDEX_NONE));
	TestEqual(TEXT("FindHintByName (the CAI_Hint RTTI cast) finds nothing"),
		Npc->FindHintByName(TEXT("shard_hint_3")), int32(INDEX_NONE));

	// An unresolvable hint is unusable, which is what every caller of `0x102d14c0` needs.

	// Slot 566's species entry point: this NPC IS a `CNPC_VSabbatLeader`, whose own override
	// (`0x103a9340`) fills the slot — and the refusal below is the seam's, not the body's.
	TestEqual(TEXT("CNPC_VSabbatLeader fills slot 566 with its own body"),
		FString(ElysiumNpcTestCensus::BodyOf(Npc->RetailClass(), 566)), FString(TEXT("0x103a9340")));
	TestFalse(TEXT("but slot 566 on a hint node refuses, because the hint cannot be read"),
		Npc->FValidateHintTypeNode(0));

	// `FindHintNode` (`0x10365780`) — the miss arm: `m_pHintNode` is written on BOTH paths, then
	// `TaskFail(4)`.
	AddExpectedError(TEXT("TaskFail 0x4"), EAutomationExpectedErrorFlags::Contains, 0);
	Npc->BaseScheduleHost.HintNode = 5;
	Npc->BaseScheduleHost.FailureReason = 0;
	TestFalse(TEXT("FindHintNode misses"), Npc->FindHintNode(15000, 0));
	TestEqual(TEXT("and still clears m_pHintNode, as retail's unconditional store does"),
		Npc->BaseScheduleHost.HintNode, int32(INDEX_NONE));
	TestEqual(TEXT("and fails the task with retail's reason 4"),
		Npc->BaseScheduleHost.FailureReason, 4);

	// `SelectTzimisceHintNode` (`0x103bfa50`) — the null-target arm zeroes the hint, and a
	// double miss returns 0 without touching it.
	Tzim->BaseScheduleHost.HintNode = 5;
	TestEqual(TEXT("a null target answers 0"), Tzim->SelectTzimisceHintNode(nullptr), 0);
	TestEqual(TEXT("and zeroes m_pHintNode"), Tzim->BaseScheduleHost.HintNode, int32(INDEX_NONE));
	Tzim->BaseScheduleHost.HintNode = 5;
	TestEqual(TEXT("two missed searches also answer 0"), Tzim->SelectTzimisceHintNode(Tzim), 0);
	TestEqual(TEXT("but leave m_pHintNode alone — only the null-target arm clears it"),
		Tzim->BaseScheduleHost.HintNode, 5);

	// The two cover forwards. `0x10297430` refuses before calling the validator when the handle is
	// unset; `0x102974f0` has no such pre-check but still cannot resolve a hint.
	Npc->ScheduleHost.HintCoverObject = FElysiumEntityHandle();
	TestFalse(TEXT("IsHintCoverValid refuses with no m_hHintCoverObject"),
		Npc->IsHintCoverValid(0));
	TestFalse(TEXT("IsHintCoverValidLoose refuses too, at the hint rather than the handle"),
		Npc->IsHintCoverValidLoose(0));
	// The validator behind both (`0x10296c40`), the hint LOS check (`0x102968f0`) and the idle gate
	// (`0x102b5de0`) are bodies now, asserted arm by arm in `AttackValidator`, `HintLos` and
	// `IdleGate` below.

	// `FindHintEndEntity` (`0x103d6520`) — with both lookups refusing, retail's fallback is the
	// hint itself.
	{
		FElysiumNpcBase::FHintWords Hint = MakeHint(15000);
		Hint.HintIndex = 13;
		Hint.TargetName = TEXT("end_of_the_line");
		TestEqual(TEXT("a hint whose target name resolves to nothing falls back to itself"),
			Wolf->FindHintEndEntity(Hint), 13);
	}

	// `CheckJumpPathToHintNode` (`0x1036df50`) — the gates, in retail's order.
	{
		FElysiumNpcBase::FHintWords Hint = MakeHint(15000);
		Hint.OriginCm = FVector(500.0, 0.0, 0.0);

		const FElysiumEntityHandle CachedPlayer = Chang->Senses.Memory.ClosestPlayer;
		Chang->Senses.Memory.ClosestPlayer = FElysiumEntityHandle::Invalid();
		TestFalse(TEXT("no cached m_hClosestPlayer blocks the jump before anything else is asked"),
			Chang->CheckJumpPathToHintNode(Hint));
		Chang->Senses.Memory.ClosestPlayer = CachedPlayer;

		FElysiumNpcBase::FHintWords None;

		// With a player cached, the segment arms test against `_DAT_104ada34` (100 units); whether
		// or not the fixture player stands within it, the sector gate below refuses.
		TestEqual(TEXT("the sector seam answers 4, the value that CLOSES the gate"),
			Chang->JumpPathSector(FVector::ZeroVector), 4);
		TestTrue(TEXT("the fixture cached a closest player"),
			Chang->Senses.Memory.ClosestPlayer.IsSet());
		TestFalse(TEXT("so the jump is refused on the sector gate, not on the segment distance"),
			Chang->CheckJumpPathToHintNode(Hint));
	}

	// `SetHintActivity` (`0x103d6000`) — a null hint refuses without drawing.
	{
		FElysiumNpcBase::FHintWords None;
		TestFalse(TEXT("SetHintActivity refuses a null hint"), Wolf->SetHintActivity(None));
		FElysiumNpcBase::FHintWords Hint = MakeHint(0x3aa4);   // a type the switch does not carry
		TestFalse(TEXT("and a type outside the switch"), Wolf->SetHintActivity(Hint));
		Hint.HintType = 0x3aaa;
		// A type the switch carries still answers true: `PositionAtHint` declining a `vec3_invalid`
		// groundpoint does not change `SetHintActivity`'s answer, which retail never reads back.
		TestTrue(TEXT("but positions at and plays a type it does"), Wolf->SetHintActivity(Hint));
	}

	// `CAI_Hint::OnRestore` (slot 130, `0x102d3ec0`), handed over from family Sounds: the node lookup
	// `0x102d3e60` over the saved `m_nNodeID`, then either the "incorrect origin" arm or the relink.
	{
		FElysiumPlaceSet Places;
		FElysiumPlaceRow Row;
		Row.OriginCm = FVector(10.0, 20.0, 30.0);
		Row.Type = 2;
		Places.AdoptRows({ Row, Row });
		Places.BeginMapSpawn();
		const int32 Misses = ElysiumAiNetwork::NodeMissCounter();                  // DAT_106c994c, never reset
		const FElysiumEntityHandle HintHandle(7, 3);

		FElysiumNpcBase::FHintWords Standalone = MakeHint(15000);   // m_nNodeID -1
		const FElysiumNpcBase::FHintRestoreResult None =
			FElysiumNpcBase::HintOnRestore(Standalone, HintHandle, Places);
		TestFalse(TEXT("a hint with no node id finds no node"), None.bNodeFound);
		TestFalse(TEXT("so no node was claimed"), None.bClaimedNode);
		TestTrue(TEXT("and nothing was teleported"), None.NodeOriginCm.IsNearlyZero());
		TestEqual(TEXT("...and -1 is not counted out (0x102d3e60)"), ElysiumAiNetwork::NodeMissCounter() - Misses, 0);

		FElysiumNpcBase::FHintWords Past = MakeHint(15000);
		Past.NodeId = 2;
		TestFalse(TEXT("an id past the network finds no node"),
			FElysiumNpcBase::HintOnRestore(Past, HintHandle, Places).bNodeFound);
		TestEqual(TEXT("...and counts out (DAT_106c994c)"), ElysiumAiNetwork::NodeMissCounter() - Misses, 1);

		FElysiumNpcBase::FHintWords Bound = MakeHint(15000);
		Bound.NodeId = 1;
		const FElysiumNpcBase::FHintRestoreResult Relinked =
			FElysiumNpcBase::HintOnRestore(Bound, HintHandle, Places);
		TestTrue(TEXT("a bound hint finds its node"), Relinked.bNodeFound && Relinked.NodeIndex == 1);
		TestTrue(TEXT("...claims it (node +0xa0 = this)"), Relinked.bClaimedNode
			&& Places.AttachedHint(1) == HintHandle);
		TestEqual(TEXT("...and stands at its raw origin"), Relinked.NodeOriginCm, Row.OriginCm);
		TestFalse(TEXT("the other node is untouched"), Places.AttachedHint(0).IsSet());
	}

	// The remaining seams, each asked once so a future implementer sees the call site in a trace.
	// `ActivityList_IndexForName` (`0x10412520`) is real since story 8 wave 2: retail's enum
	// (`0x104126e0`'s first registration is ACT_IDLE = 1), case-folded, -1 for a name it lacks.
	TestEqual(TEXT("ActivityIdForName: ACT_IDLE is 1"), Npc->ActivityIdForName(TEXT("ACT_IDLE")), 1);
	TestEqual(TEXT("...case-folded (the table comparator LAB_10006636, installed by 0x1024a230, is not in the listing: the fold is the port's reading, unproven)"), Npc->ActivityIdForName(TEXT("act_idle")), 1);
	TestEqual(TEXT("...and retail's own -1 for a name it never registered"),
		Npc->ActivityIdForName(TEXT("ACT_NOT_A_REGISTERED_NAME")), int32(INDEX_NONE));
	// The 29 grapple registrations (`0x10412590`) store their value only; the name table insert
	// `0x10412260` is reached from `0x104123a0` / `0x104124b0` alone, so `0x10412420` answers -1 for
	// a grapple name, while the grapple list still holds the value.
	TestEqual(TEXT("0x10412590: a grapple name is not in the name table"),
		Npc->ActivityIdForName(TEXT("ACT_FINISHING_MOVE")), int32(INDEX_NONE));
	TestTrue(TEXT("0x104126a0: ...but its value is a grapple activity"), ElysiumRetailActivities::IsGrapple(148));
	TestNull(TEXT("0x10412550: ...and names nothing"), ElysiumRetailActivities::NameOf(148));
	TestEqual(TEXT("the bridge resolves it by id (the registration's own name)"),
		FString(ElysiumRetailActivities::RegistrationNameOf(148)), FString(TEXT("ACT_FINISHING_MOVE")));
	// (`IsHintSequenceFinished` / `DoesHintSequenceLoop` are gone: `0x102aa210` reads the live words
	// `m_bSequenceFinished` +0x65c (`0x102aa25b`) and `m_bSequenceLoops` +0x65d (`0x102aa235`,
	// `0x102aa45c`), which the sequence bridge carries.)
	TestNull(TEXT("InterestingPlaceMarkerOccupant answers nothing"),
		Npc->InterestingPlaceMarkerOccupant(nullptr));
	TestEqual(TEXT("CurrentRetailActivityId answers -1"), Npc->CurrentRetailActivityId(),
		int32(INDEX_NONE));
	{
		FString Unused;
		TestFalse(TEXT("PatrolNodeInterestRecordName answers nothing for a record that is no hint"),
			Npc->PatrolNodeInterestRecordName(0, Unused));
	}
	TestFalse(TEXT("IsTzimisceHintUsable answers false"), Tzim->IsTzimisceHintUsable(0, Tzim));
	return true;
}

// -------------------------------------------------------------------------------------------------
// 0018 story 8: `0x10296c40` (the attack-position validator), `0x102968f0` (the hint LOS check) and
// `0x102b5de0` (the idle gate), as rules over a world that stands a live hint.
//
// Geometry, in Source units along +X (centimetres = units x `ElysiumMove::U`): the NPC at the origin,
// the enemy NPC at 300, a standalone hint at 100 facing yaw 0, a plain `info_target` crate off-axis.
// So the hint-to-enemy distance is 200 and the facing projection is ~1.0.
// -------------------------------------------------------------------------------------------------

namespace ElysiumNpcKernelHintsValidatorTests
{
	constexpr double CmPerUnit = ElysiumMove::U;
	constexpr int32 HintLosMask = 0x46804099;
	constexpr int32 IdleGateMask = 0x2000000;

	FVector AtUnits(double X, double Y = 0.0, double Z = 0.0)
	{
		return FVector(X, Y, Z) * CmPerUnit;
	}

	FElysiumNpcWorldBuilder BuildValidatorWorld(const TCHAR* Map)
	{
		FElysiumNpcWorldBuilder Builder(Map, 18);
		Builder.AddNpcOfClass(TEXT("npc"), FVector::ZeroVector, nullptr);
		Builder.AddNpcOfClass(TEXT("enemy"), AtUnits(300.0), nullptr);
		Builder.AddEntity(TEXT("info_target"), TEXT("crate"), AtUnits(200.0, 200.0));
		FElysiumEntityDef& Def = Builder.AddEntity(TEXT("info_node_hint"), TEXT("h"), AtUnits(100.0));
		Def.Keys.Add(TEXT("hinttype"), TEXT("100"));
		return Builder;
	}

	struct FValidatorRig
	{
		FElysiumNpcWorldFixture F;
		FElysiumNpc* Npc = nullptr;
		FElysiumNpc* Enemy = nullptr;
		FElysiumEntity* Crate = nullptr;
		FElysiumHint* Hint = nullptr;
		FElysiumItemTable Items;
		bool bItemsInstalled = false;

		explicit FValidatorRig(const TCHAR* Map)
			: F(BuildValidatorWorld(Map))
		{
			Npc = F.Npc(TEXT("npc"));
			Enemy = F.Npc(TEXT("enemy"));
			Crate = F.World.FindByName(TEXT("crate"));
			Hint = FElysiumHint::Cast(F.World.FindByName(TEXT("h")));
			FElysiumNpcWorldFixture::Quiet({ Npc, Enemy });
			if (Hint != nullptr)
			{
				Hint->NodeId = INDEX_NONE;             // standalone: `0x102d1180` is its own origin
				Hint->Angles = FVector::ZeroVector;    // facing +X
				Hint->Disabled = 0;
			}
		}

		~FValidatorRig()
		{
			F.Services.TraceRetailQuery = nullptr;
			if (bItemsInstalled)
			{
				ElysiumItems::Uninstall(Items);
			}
		}

		bool Ready(FAutomationTestBase& Test) const
		{
			return Test.TestNotNull(TEXT("the NPC stood"), Npc)
				&& Test.TestNotNull(TEXT("the enemy stood"), Enemy)
				&& Test.TestNotNull(TEXT("the crate stood"), Crate)
				&& Test.TestNotNull(TEXT("the hint stood"), Hint);
		}

		// The live hint's words with the band opened wide and the facing at yaw 0; each case then
		// moves the one word its arm reads.
		FElysiumNpcBase::FHintWords Words() const
		{
			FElysiumNpcBase::FHintWords Out;
			Npc->HintWords(Hint->Handle.Index, Out);
			Out.TargetDistMin = 0.f;
			Out.TargetDistMax = 1000.f;
			Out.NodeId = INDEX_NONE;
			Out.Angles = FVector::ZeroVector;
			Out.Disabled = 0;
			return Out;
		}

		// `GiveNamedItem` of a case-local firearm, then slot 388 when it did not become active (the
		// pattern `ElysiumNpcKernelStartTaskTests_4.cpp` stands). A firearm is a `CWeaponRanged`,
		// whose `+0x8c0` is 1024 (`0x10238070`, 0018 story 8) — the word arm 5 reads.
		FElysiumEntity* GiveWeapon()
		{
			if (!bItemsInstalled)
			{
				bItemsInstalled = true;
				FElysiumItemDef Gun;
				Gun.Classname = TEXT("item_w_hints18_gun");
				Gun.PrintName = Gun.Classname;
				Gun.Type = EElysiumItemType::WeaponFirearm;
				Gun.bWieldable = true;
				Items.Items.Add(MoveTemp(Gun));
				Items.Reindex();
				ElysiumItems::Install(Items);
			}
			const FElysiumEntityHandle Handle =
				Npc->Inventory.GiveNamedItem(*Npc, FString(TEXT("item_w_hints18_gun")));
			FElysiumEntity* Item = F.World.Resolve(Handle);
			if (Item != nullptr && Npc->ActiveWeaponEntity() != Item)
			{
				Npc->Weapon_Switch(Item, 0);
			}
			return Npc->ActiveWeaponEntity();
		}
	};

	// A trace double that records the last request it was asked and answers `Answer`.
	struct FTraceDouble
	{
		FElysiumRetailTrace Seen;
		FElysiumRetailTraceResult Answer;
		int32 Calls = 0;

		// The rig clears the query in its destructor and every case clears it before this dies.
		void Install(FValidatorRig& Rig)
		{
			Rig.F.Services.TraceRetailQuery = [this](const FElysiumRetailTrace& Request,
				FElysiumRetailTraceResult& Out)
			{
				Seen = Request;
				++Calls;
				Out = Answer;
				return true;
			};
		}

		void AnswerClear()
		{
			Answer = FElysiumRetailTraceResult();
		}

		void AnswerBlocked(const FElysiumEntityHandle& Hit)
		{
			Answer = FElysiumRetailTraceResult();
			Answer.Fraction = 0.5f;
			Answer.HitEntity = Hit;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelHintsAttackValidatorTest,
	"Elysium.Arm.NpcKernelHints.AttackValidator", GHintsTestFlags)
bool FElysiumNpcKernelHintsAttackValidatorTest::RunTest(const FString&)
{
	using namespace ElysiumNpcKernelHintsValidatorTests;
	FValidatorRig R(TEXT("__hints_attack_validator__"));
	if (!R.Ready(*this))
	{
		return false;
	}
	FElysiumNpc* Npc = R.Npc;
	const int32 HintId = R.Hint->Handle.Index;
	constexpr float Good = 0.5f;
	constexpr float Bad = 1.1f;
	Npc->bStayEntrenched = false;
	Npc->BaseScheduleHost.HintNode = INDEX_NONE;
	Npc->ScheduleHost.bForceCoverLosCheck = false;

	// Arm 0 (`10296c4f` / `10296c61`): a null or disabled hint fails, before the entrenched pass.
	{
		FElysiumNpcBase::FHintWords None;
		TestFalse(TEXT("0x10296c40: a null hint fails"),
			Npc->ValidateHintCoverRange(None, R.Enemy, Good, Bad));
		FElysiumNpcBase::FHintWords Disabled = R.Words();
		Disabled.Disabled = 1;
		Npc->bStayEntrenched = true;
		Npc->BaseScheduleHost.HintNode = HintId;
		TestFalse(TEXT("0x10296c40: a disabled hint fails even as my entrenched hint"),
			Npc->ValidateHintCoverRange(Disabled, R.Enemy, Good, Bad));
		Npc->bStayEntrenched = false;
		Npc->BaseScheduleHost.HintNode = INDEX_NONE;
	}

	// Arms 1 and 2, with no weapon yet: the entrenched / null-enemy PASS comes before the weapon test.
	if (!TestNull(TEXT("precondition: the NPC holds no active weapon"), Npc->ActiveWeaponEntity()))
	{
		return false;
	}
	TestTrue(TEXT("0x10296c40 arm 1: a null enemy PASSES, weapon or not"),
		Npc->ValidateHintCoverRange(R.Words(), nullptr, Good, Bad));
	Npc->bStayEntrenched = true;
	Npc->BaseScheduleHost.HintNode = HintId;
	TestTrue(TEXT("0x10296c40 arm 1: my own hint while m_bStayEntrenched PASSES before any test"),
		Npc->ValidateHintCoverRange(R.Words(), R.Enemy, Good, Bad));
	Npc->BaseScheduleHost.HintNode = INDEX_NONE;
	TestFalse(TEXT("0x10296c40 arm 2: entrenched on a hint that is not mine is no pass: no weapon fails"),
		Npc->ValidateHintCoverRange(R.Words(), R.Enemy, Good, Bad));
	Npc->bStayEntrenched = false;
	TestFalse(TEXT("0x10296c40 arm 2: no active weapon fails"),
		Npc->ValidateHintCoverRange(R.Words(), R.Enemy, Good, Bad));

	if (!TestNotNull(TEXT("the NPC now holds a weapon"), R.GiveWeapon()))
	{
		return false;
	}
	TestTrue(TEXT("0x10296c40: the base layout passes every arm"),
		Npc->ValidateHintCoverRange(R.Words(), R.Enemy, Good, Bad));

	// Arm 3: |hint.z - me.z| over 64 (`_DAT_1049ae28`, a double) fails.
	{
		FElysiumNpcBase::FHintWords W = R.Words();
		W.OriginCm.Z = 65.0 * CmPerUnit;
		TestFalse(TEXT("0x10296c40 arm 3: a hint 65 units above me fails"),
			Npc->ValidateHintCoverRange(W, R.Enemy, Good, Bad));
		W.OriginCm.Z = -65.0 * CmPerUnit;
		TestFalse(TEXT("0x10296c40 arm 3: ...and 65 below (the difference is absolute)"),
			Npc->ValidateHintCoverRange(W, R.Enemy, Good, Bad));
		W.OriginCm.Z = 63.0 * CmPerUnit;
		TestTrue(TEXT("0x10296c40 arm 3: 63 units passes"),
			Npc->ValidateHintCoverRange(W, R.Enemy, Good, Bad));
	}

	// Arm 4: the hint-to-enemy 2-D distance (200) under m_flTargetDistMin fails.
	{
		FElysiumNpcBase::FHintWords W = R.Words();
		W.TargetDistMin = 201.f;
		TestFalse(TEXT("0x10296c40 arm 4: distance 200 < m_flTargetDistMin 201 fails"),
			Npc->ValidateHintCoverRange(W, R.Enemy, Good, Bad));
		W.TargetDistMin = 199.f;
		TestTrue(TEXT("0x10296c40 arm 4: ...and passes against 199"),
			Npc->ValidateHintCoverRange(W, R.Enemy, Good, Bad));
	}

	// Arm 5: unless entrenched, over m_flTargetDistMax OR the weapon's `+0x8c0` (1024) fails.
	{
		FElysiumNpcBase::FHintWords W = R.Words();
		W.TargetDistMax = 199.f;
		TestFalse(TEXT("0x10296c40 arm 5: distance 200 > m_flTargetDistMax 199 fails"),
			Npc->ValidateHintCoverRange(W, R.Enemy, Good, Bad));
		Npc->bStayEntrenched = true;
		TestTrue(TEXT("0x10296c40 arm 5: m_bStayEntrenched skips the upper bound entirely"),
			Npc->ValidateHintCoverRange(W, R.Enemy, Good, Bad));
		Npc->bStayEntrenched = false;

		const FVector EnemyAt = R.Enemy->Origin;
		W.TargetDistMax = 5000.f;
		R.Enemy->Origin = AtUnits(1200.0);   // distance 1100
		TestFalse(TEXT("0x10296c40 arm 5: distance 1100 is past the firearm's +0x8c0 (1024)"),
			Npc->ValidateHintCoverRange(W, R.Enemy, Good, Bad));
		Npc->bStayEntrenched = true;
		TestTrue(TEXT("0x10296c40 arm 5: ...which entrenchment skips too"),
			Npc->ValidateHintCoverRange(W, R.Enemy, Good, Bad));
		Npc->bStayEntrenched = false;
		R.Enemy->Origin = AtUnits(1100.0);   // distance 1000
		TestTrue(TEXT("0x10296c40 arm 5: distance 1000 is inside it"),
			Npc->ValidateHintCoverRange(W, R.Enemy, Good, Bad));
		R.Enemy->Origin = EnemyAt;
	}

	// Arm 6: a hint that is not mine needs dot(norm(hint - enemy), norm(me - enemy)) >= 0.2.
	{
		const FVector MeAt = Npc->Origin;
		Npc->Origin = AtUnits(600.0);   // past the enemy: the hint is on the other side of it
		TestFalse(TEXT("0x10296c40 arm 6: a hint across the enemy from me fails the 0.2 projection"),
			Npc->ValidateHintCoverRange(R.Words(), R.Enemy, Good, Bad));
		Npc->BaseScheduleHost.HintNode = HintId;
		TestTrue(TEXT("0x10296c40 arm 6: my own hint skips the projection"),
			Npc->ValidateHintCoverRange(R.Words(), R.Enemy, Good, Bad));
		Npc->BaseScheduleHost.HintNode = INDEX_NONE;
		Npc->Origin = MeAt;
	}

	// Arm 7: the facing projection against two bounds, and they are not symmetric.
	{
		FElysiumNpcBase::FHintWords W = R.Words();
		TestTrue(TEXT("0x10296c40 arm 7: dead ahead (~1.0) sits between good 0.99 and bad 1.1"),
			Npc->ValidateHintCoverRange(W, R.Enemy, 0.99f, Bad));
		TestFalse(TEXT("0x10296c40 arm 7: `<= flGoodRange` fails (good 1.0)"),
			Npc->ValidateHintCoverRange(W, R.Enemy, 1.0f, Bad));
		TestFalse(TEXT("0x10296c40 arm 7: `>= flBadRange` fails (bad 0.731, IsHintCoverValid's)"),
			Npc->ValidateHintCoverRange(W, R.Enemy, Good, 0.731f));
		W.Angles = FVector(0.0, 180.0, 0.0);
		TestFalse(TEXT("0x10296c40 arm 7: a hint facing away (~-1.0) is outside good 0.0"),
			Npc->ValidateHintCoverRange(W, R.Enemy, 0.0f, Bad));

		// The facing is a SOURCE yaw (`0x102d12e0`) and the port's world has Y negated: yaw 90 faces
		// Unreal -Y.
		const FVector EnemyAt = R.Enemy->Origin;
		W.Angles = FVector(0.0, 90.0, 0.0);
		R.Enemy->Origin = AtUnits(100.0, -200.0);
		TestTrue(TEXT("0x10296c40 arm 7: Source yaw 90 faces an enemy at Unreal -Y"),
			Npc->ValidateHintCoverRange(W, R.Enemy, Good, Bad));
		R.Enemy->Origin = AtUnits(100.0, 200.0);
		TestFalse(TEXT("0x10296c40 arm 7: ...and turns its back on one at Unreal +Y"),
			Npc->ValidateHintCoverRange(W, R.Enemy, Good, Bad));
		R.Enemy->Origin = EnemyAt;
	}

	// The two forwards differ only in the bad bound: 0.731 (`0x10297430`) and 1.1 (`0x102974f0`).
	{
		R.Hint->TargetDistMin = 0.f;
		R.Hint->TargetDistMax = 1000.f;
		R.Hint->TargetAngleRangeDot = Good;
		Npc->ScheduleHost.HintCoverObject = R.Enemy->Handle;
		TestFalse(TEXT("IsHintCoverValid: an enemy dead ahead is `inside of bad range` 0.731"),
			Npc->IsHintCoverValid(HintId));
		TestTrue(TEXT("IsHintCoverValidLoose: ...and inside 1.1"), Npc->IsHintCoverValidLoose(HintId));
		Npc->ScheduleHost.HintCoverObject = FElysiumEntityHandle();
	}

	// Arm 8: only under m_bForceCoverLOSCheck, `0x102968f0(hint, enemy)` must pass.
	{
		FTraceDouble Trace;
		Trace.Install(R);
		Trace.AnswerBlocked(FElysiumEntityHandle::Invalid());
		TestTrue(TEXT("0x10296c40 arm 8: without m_bForceCoverLOSCheck no LOS is asked"),
			Npc->ValidateHintCoverRange(R.Words(), R.Enemy, Good, Bad));
		TestEqual(TEXT("0x10296c40 arm 8: ...and no trace was cast"), Trace.Calls, 0);
		Npc->ScheduleHost.bForceCoverLosCheck = true;
		TestFalse(TEXT("0x10296c40 arm 8: forced, a blocked hint LOS fails"),
			Npc->ValidateHintCoverRange(R.Words(), R.Enemy, Good, Bad));
		TestEqual(TEXT("0x10296c40 arm 8: ...under the LOS mask"), Trace.Seen.RetailMask, HintLosMask);
		Trace.AnswerClear();
		TestTrue(TEXT("0x10296c40 arm 8: forced, a clear hint LOS passes"),
			Npc->ValidateHintCoverRange(R.Words(), R.Enemy, Good, Bad));
		Npc->ScheduleHost.bForceCoverLosCheck = false;
		R.F.Services.TraceRetailQuery = nullptr;
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelHintsHintLosTest,
	"Elysium.Arm.NpcKernelHints.HintLos", GHintsTestFlags)
bool FElysiumNpcKernelHintsHintLosTest::RunTest(const FString&)
{
	using namespace ElysiumNpcKernelHintsValidatorTests;
	FValidatorRig R(TEXT("__hints_hint_los__"));
	if (!R.Ready(*this))
	{
		return false;
	}
	FElysiumNpc* Npc = R.Npc;
	const int32 HintId = R.Hint->Handle.Index;

	TestFalse(TEXT("0x102968f0: a null target fails"), Npc->HintLosCheck(HintId, nullptr));
	TestFalse(TEXT("0x102968f0: a null hint fails"), Npc->HintLosCheck(INDEX_NONE, R.Enemy));
	TestTrue(TEXT("0x102968f0: no collision world answers the PASS arm"),
		Npc->HintLosCheck(HintId, R.Enemy));

	FTraceDouble Trace;
	Trace.Install(R);
	Trace.AnswerClear();
	TestTrue(TEXT("0x102968f0: a clear line passes"), Npc->HintLosCheck(HintId, R.Enemy));
	FVector MinsUnits = FVector::ZeroVector;
	FVector MaxsUnits = FVector::ZeroVector;
	FElysiumNpcBase::RetailCollisionExtents(*Npc, MinsUnits, MaxsUnits);
	const FVector ExpectedStart = R.Hint->Origin + FVector(0.0, 0.0, MaxsUnits.Z * CmPerUnit);
	TestTrue(TEXT("0x102968f0: the line starts at the HINT, raised by the NPC's collision maxs z"),
		Trace.Seen.StartCm.Equals(ExpectedStart, 0.01));
	TestTrue(TEXT("0x102968f0: ...and ends at the target's eye (slot 193)"),
		Trace.Seen.EndCm.Equals(R.Enemy->EyePosition(), 0.01));
	TestEqual(TEXT("0x102968f0: mask 0x46804099"), Trace.Seen.RetailMask, HintLosMask);
	TestEqual(TEXT("0x102968f0: CTraceFilterHintLOS has no pass entity"), Trace.Seen.Ignore.Num(), 0);

	Trace.AnswerBlocked(FElysiumEntityHandle::Invalid());
	TestFalse(TEXT("0x102968f0: fraction < 1.0 fails"), Npc->HintLosCheck(HintId, R.Enemy));
	Trace.AnswerClear();
	Trace.Answer.bStartSolid = true;
	TestFalse(TEXT("0x102968f0: startsolid fails at fraction 1.0"), Npc->HintLosCheck(HintId, R.Enemy));
	Trace.AnswerClear();
	Trace.Answer.bAllSolid = true;
	TestFalse(TEXT("0x102968f0: allsolid fails at fraction 1.0"), Npc->HintLosCheck(HintId, R.Enemy));

	// The filter refuses every combat character: a character on the line blocks nothing.
	Trace.AnswerClear();
	FElysiumRetailTraceCharacter Body;
	Body.Entity = R.Enemy->Handle;
	Body.Fraction = 0.25f;
	Trace.Answer.Characters.Add(Body);
	TestTrue(TEXT("0x102968f0: a character on the line is not a blocker"),
		Npc->HintLosCheck(HintId, R.Enemy));
	R.F.Services.TraceRetailQuery = nullptr;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelHintsIdleGateTest,
	"Elysium.Arm.NpcKernelHints.IdleGate", GHintsTestFlags)
bool FElysiumNpcKernelHintsIdleGateTest::RunTest(const FString&)
{
	using namespace ElysiumNpcKernelHintsValidatorTests;
	FValidatorRig R(TEXT("__hints_idle_gate__"));
	if (!R.Ready(*this))
	{
		return false;
	}
	FElysiumNpc* Npc = R.Npc;
	Npc->ShootTargetOverride = FElysiumEntityHandle::Invalid();
	Npc->Cognition.Conditions.Clear(EElysiumNpcCond::EnemyOccluded);
	Npc->OccludedReportTimeE = 0.0;
	if (!TestNull(TEXT("precondition: no enemy"), Npc->GetEnemy()))
	{
		return false;
	}

	// Arm 2's head, no trace needed.
	TestTrue(TEXT("0x102b5de0 arm 2: no override, not occluded, no enemy passes"),
		Npc->HintIdleActivityGate());
	Npc->Cognition.Conditions.Set(EElysiumNpcCond::EnemyOccluded);
	TestFalse(TEXT("0x102b5de0 arm 2: HasCondition(0x48 ENEMY_OCCLUDED) fails"),
		Npc->HintIdleActivityGate());
	Npc->Cognition.Conditions.Clear(EElysiumNpcCond::EnemyOccluded);

	// `0x1028e870`: `m_flOccludedDelay + curtime - m_flOccludedReportTimeE`, time since occluded.
	const double Now = R.F.World.NowSeconds();
	Npc->OccludedDelay = 1.0f;
	Npc->OccludedReportTimeE = Now - 3.0;   // 1 + 3 = 4 s
	TestFalse(TEXT("0x102b5de0 arm 2: more than 3.0 s since occluded fails"),
		Npc->HintIdleActivityGate());
	Npc->OccludedReportTimeE = Now - 2.0;   // 1 + 2 = 3 s
	TestTrue(TEXT("0x102b5de0 arm 2: exactly 3.0 s continues (only ABOVE 3.0 fails)"),
		Npc->HintIdleActivityGate());
	Npc->OccludedReportTimeE = 0.0;
	Npc->OccludedDelay = 0.0f;

	// The enemy trace: WorldSpaceCenter -> the enemy's BodyTarget(vec3_origin, false, false).
	ElysiumNpcEnemy::SetEnemy(*Npc, R.Enemy->Handle);
	if (!TestTrue(TEXT("precondition: the enemy is set"), Npc->GetEnemy() == R.Enemy))
	{
		return false;
	}
	FTraceDouble Trace;
	Trace.Install(R);
	Trace.AnswerClear();
	TestTrue(TEXT("0x102b5de0 arm 2: a clear line to the enemy passes"), Npc->HintIdleActivityGate());
	TestEqual(TEXT("0x102b5de0: mask 0x2000000"), Trace.Seen.RetailMask, IdleGateMask);
	TestTrue(TEXT("0x102b5de0: from WorldSpaceCenter (slot 192)"),
		Trace.Seen.StartCm.Equals(Npc->WorldSpaceCenter(), 0.01));
	TestTrue(TEXT("0x102b5de0: to the enemy's BodyTarget (slot 197)"),
		Trace.Seen.EndCm.Equals(R.Enemy->BodyTarget(FVector::ZeroVector, false, false), 0.01));
	TestTrue(TEXT("0x102b5de0: CTraceFilterSimple(this) passes the NPC itself"),
		Trace.Seen.Ignore.Contains(Npc->Handle));

	Trace.AnswerBlocked(FElysiumEntityHandle::Invalid());
	TestTrue(TEXT("0x102b5de0 arm 2: a block with no entity the port names passes"),
		Npc->HintIdleActivityGate());
	Trace.AnswerBlocked(R.Crate->Handle);
	Npc->Relationships.SetEntity(R.Crate->Handle, EElysiumRelationship::Like, 5);
	TestFalse(TEXT("0x102b5de0 arm 2: something I like (D_LI 3) in the way fails"),
		Npc->HintIdleActivityGate());
	Npc->Relationships.SetEntity(R.Crate->Handle, EElysiumRelationship::Neutral, 5);
	TestFalse(TEXT("0x102b5de0 arm 2: something I ignore (D_NU 4) in the way fails"),
		Npc->HintIdleActivityGate());
	Npc->Relationships.SetEntity(R.Crate->Handle, EElysiumRelationship::Hate, 5);
	TestTrue(TEXT("0x102b5de0 arm 2: something I hate in the way passes"), Npc->HintIdleActivityGate());
	Npc->Relationships.SetEntity(R.Crate->Handle, EElysiumRelationship::Fear, 5);
	TestTrue(TEXT("0x102b5de0 arm 2: something I fear in the way passes"), Npc->HintIdleActivityGate());

	// Arm 1: a live m_hShootTargetOverride takes the whole gate; ENEMY_OCCLUDED is not read.
	Npc->ShootTargetOverride = R.Crate->Handle;
	Npc->Cognition.Conditions.Set(EElysiumNpcCond::EnemyOccluded);
	Trace.AnswerClear();
	TestTrue(TEXT("0x102b5de0 arm 1: a clear line to the override passes, occluded or not"),
		Npc->HintIdleActivityGate());
	TestTrue(TEXT("0x102b5de0 arm 1: the line ends at the override's origin"),
		Trace.Seen.EndCm.Equals(R.Crate->Origin, 0.01));
	TestEqual(TEXT("0x102b5de0 arm 1: mask 0x2000000"), Trace.Seen.RetailMask, IdleGateMask);
	Trace.AnswerBlocked(FElysiumEntityHandle::Invalid());
	TestFalse(TEXT("0x102b5de0 arm 1: blocked by the world fails"), Npc->HintIdleActivityGate());
	Trace.AnswerClear();
	Trace.Answer.bStartSolid = true;
	TestFalse(TEXT("0x102b5de0 arm 1: startsolid in the world fails too"), Npc->HintIdleActivityGate());
	Trace.AnswerBlocked(R.Crate->Handle);
	TestTrue(TEXT("0x102b5de0 arm 1: blocked by any entity that is not the world passes"),
		Npc->HintIdleActivityGate());
	Npc->ShootTargetOverride = FElysiumEntityHandle::Invalid();
	Npc->Cognition.Conditions.Clear(EElysiumNpcCond::EnemyOccluded);
	R.F.Services.TraceRetailQuery = nullptr;
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
