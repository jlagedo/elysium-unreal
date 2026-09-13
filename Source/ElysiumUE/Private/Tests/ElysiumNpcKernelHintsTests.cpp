#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumMoveSolve.h"
#include "Substrate/ElysiumInterestingPlace.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Tests/ElysiumNpcTestFixture.h"

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
	FElysiumNpc::FHintWords MakeHint(int32 HintType)
	{
		FElysiumNpc::FHintWords Hint;
		Hint.bValid = true;
		Hint.HintType = HintType;
		return Hint;
	}
}

// -------------------------------------------------------------------------------------------------
// Slot 566's species table.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelHintsTypeSpeciesTest,
	"Elysium.Substrate.NpcKernelHints.HintTypeSpecies", GHintsTestFlags)
bool FElysiumNpcKernelHintsTypeSpeciesTest::RunTest(const FString&)
{
	using FRow = FElysiumNpc::FHintTypeSpecies;
	auto Ask = [](const TCHAR* Cls, int32 Type)
	{
		return FElysiumNpc::FValidateHintTypeSpecies(FElysiumNpc::HintTypeSpeciesOf(Cls), Type);
	};

	// Every row, by name, with the retail address it came from.
	int32 Count = 0;
	const FRow* Rows = FElysiumNpc::HintTypeSpeciesRows(Count);
	// Ten DISTINCT bodies: five with a real rule, four `return 1` and one `return 0`.
	// `CNPC_VChangBrosBlade` and `CNPC_VChangBrosClaw` share `CNPC_VChangBros`'s body and reach it
	// by inheritance, which is why they are not rows.
	TestEqual(TEXT("the table carries the ten recovered slot-566 species bodies"), Count, 10);
	TMap<FString, FString> ByClass;
	for (int32 i = 0; i < Count; ++i)
	{
		ByClass.Add(FString(Rows[i].RetailClass), FString(Rows[i].Body));
	}
	TestEqual(TEXT("CNPC_Crow's body"), ByClass.FindRef(TEXT("CNPC_Crow")),
		FString(TEXT("0x10358c60")));
	TestEqual(TEXT("CNPC_VDog's body"), ByClass.FindRef(TEXT("CNPC_VDog")),
		FString(TEXT("0x10374aa0")));
	TestEqual(TEXT("CNPC_VSabbatLeader's body"), ByClass.FindRef(TEXT("CNPC_VSabbatLeader")),
		FString(TEXT("0x103a9340")));
	TestEqual(TEXT("CNPC_VTzimisce's body"), ByClass.FindRef(TEXT("CNPC_VTzimisce")),
		FString(TEXT("0x103ba780")));
	TestEqual(TEXT("CNPC_VWerewolf's body"), ByClass.FindRef(TEXT("CNPC_VWerewolf")),
		FString(TEXT("0x103d7ce0")));
	TestEqual(TEXT("CNPC_VAndreiBlood's body"), ByClass.FindRef(TEXT("CNPC_VAndreiBlood")),
		FString(TEXT("0x1035db00")));
	TestEqual(TEXT("CNPC_VAsianVampire's body"), ByClass.FindRef(TEXT("CNPC_VAsianVampire")),
		FString(TEXT("0x10361470")));
	TestEqual(TEXT("CNPC_VChangBros's body"), ByClass.FindRef(TEXT("CNPC_VChangBros")),
		FString(TEXT("0x1036c6a0")));
	TestEqual(TEXT("CNPC_VSheriffMan's body"), ByClass.FindRef(TEXT("CNPC_VSheriffMan")),
		FString(TEXT("0x103af810")));
	TestEqual(TEXT("CNPC_VZombie's body"), ByClass.FindRef(TEXT("CNPC_VZombie")),
		FString(TEXT("0x103e03b0")));

	// `CNPC_Crow` (`0x10358c60`): `== 700`, nothing else.
	TestTrue(TEXT("Crow accepts 700"), Ask(TEXT("CNPC_Crow"), 700));
	TestFalse(TEXT("Crow rejects 699"), Ask(TEXT("CNPC_Crow"), 699));
	TestFalse(TEXT("Crow rejects 701"), Ask(TEXT("CNPC_Crow"), 701));

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
	TestTrue(TEXT("and VTzimisce is the one body that null-checks its hint"),
		FElysiumNpc::HintTypeSpeciesOf(TEXT("CNPC_VTzimisce"))->bNullChecks);
	TestFalse(TEXT("the other four do not"),
		FElysiumNpc::HintTypeSpeciesOf(TEXT("CNPC_VWerewolf"))->bNullChecks);

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

	// The six constant bodies.
	TestTrue(TEXT("VAndreiBlood accepts anything"), Ask(TEXT("CNPC_VAndreiBlood"), -1));
	TestTrue(TEXT("VAsianVampire accepts anything"), Ask(TEXT("CNPC_VAsianVampire"), 99999));
	TestTrue(TEXT("VChangBros accepts anything"), Ask(TEXT("CNPC_VChangBros"), 0));
	TestTrue(TEXT("VSheriffMan accepts anything"), Ask(TEXT("CNPC_VSheriffMan"), 700));
	TestFalse(TEXT("VZombie accepts nothing"), Ask(TEXT("CNPC_VZombie"), 700));

	// The blade and claw brothers reach `0x1036c6a0` by inheritance, not by a row of their own.
	TestTrue(TEXT("CNPC_VChangBrosBlade inherits the ChangBros body"),
		Ask(TEXT("CNPC_VChangBrosBlade"), 42));
	TestTrue(TEXT("CNPC_VChangBrosClaw inherits it too"), Ask(TEXT("CNPC_VChangBrosClaw"), 42));

	// A class with no override and no ancestor carrying one runs the base body, which is 29d's.
	TestNull(TEXT("CAI_BaseNPCTroika carries no species row"),
		FElysiumNpc::HintTypeSpeciesOf(TEXT("CAI_BaseNPCTroika")));
	TestFalse(TEXT("and a null row cannot answer for the base body"),
		FElysiumNpc::FValidateHintTypeSpecies(nullptr, 700));

	// Slot 567's one species override, `0x10358c90`.
	TestEqual(TEXT("Crow answers activity 0x22 for hint type 700"),
		FElysiumNpc::CrowHintActivity(700, 1), 0x22);
	TestEqual(TEXT("and falls through to the base body's 1 for anything else"),
		FElysiumNpc::CrowHintActivity(701, 1), 1);
	return true;
}

// -------------------------------------------------------------------------------------------------
// The five hint-node activity lookups on the schedule host.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelHintsActivityQueryTest,
	"Elysium.Substrate.NpcKernelHints.HintActivityQueries", GHintsTestFlags)
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
	"Elysium.Substrate.NpcKernelHints.HintRules", GHintsTestFlags)
bool FElysiumNpcKernelHintsRulesTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("kernel_hints_rules"), 29031u);
	Builder.AddNpc(TEXT("wolf"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("wolf"));
	if (!TestNotNull(TEXT("the fixture NPC spawned"), Npc))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Npc });

	// `GetHintTeleportPriority` (`0x103d3220`) — the whole map, including the two types that share
	// priority 3 and the unlisted zero.
	TestEqual(TEXT("15001 -> 2"), FElysiumNpc::GetHintTeleportPriority(0x3a99), 2);
	TestEqual(TEXT("15002 -> 3"), FElysiumNpc::GetHintTeleportPriority(0x3a9a), 3);
	TestEqual(TEXT("15003 -> 3 as well"), FElysiumNpc::GetHintTeleportPriority(0x3a9b), 3);
	TestEqual(TEXT("15000 -> 1"), FElysiumNpc::GetHintTeleportPriority(15000), 1);
	TestEqual(TEXT("15007 -> 4, the highest"), FElysiumNpc::GetHintTeleportPriority(0x3a9f), 4);
	TestEqual(TEXT("anything else -> 0"), FElysiumNpc::GetHintTeleportPriority(0x3aa0), 0);

	// `IsHintUnusable` (`0x102d14c0`) — three arms in retail's order.
	{
		FElysiumNpc::FHintWords Hint = MakeHint(0x3aa3);
		TestFalse(TEXT("a plain hint is usable"), FElysiumNpc::IsHintUnusable(Hint, 10.0, false));
		Hint.Disabled = 1;
		TestTrue(TEXT("m_iDisabled makes it unusable"),
			FElysiumNpc::IsHintUnusable(Hint, 10.0, false));
		Hint.Disabled = 0;
		Hint.NextUseTime = 20.0;
		TestTrue(TEXT("curtime before m_flNextUseTime makes it unusable"),
			FElysiumNpc::IsHintUnusable(Hint, 10.0, false));
		TestFalse(TEXT("and at the deadline exactly it is usable again — retail's test is `<`"),
			FElysiumNpc::IsHintUnusable(Hint, 20.0, false));
		Hint.NextUseTime = 0.0;
		TestTrue(TEXT("a live m_hHintOwner makes it unusable"),
			FElysiumNpc::IsHintUnusable(Hint, 10.0, true));
	}

	// `IsValidBreakHint` (`0x103d8550`) — null, unusable, then type 0x3aa3 exactly.
	{
		FElysiumNpc::FHintWords None;
		TestFalse(TEXT("a null hint is not a break hint"), Npc->IsValidBreakHint(None, 10.0));
		FElysiumNpc::FHintWords Hint = MakeHint(0x3aa3);
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
			FElysiumNpc::SelectScheduleForHint(nullptr, 0.f, 0.f), 0x163);
		FElysiumNpc::FHintWords Hint = MakeHint(0x3aa4);
		TestEqual(TEXT("15012 -> 0x15f"), FElysiumNpc::SelectScheduleForHint(&Hint, 0.f, 0.f), 0x15f);
		Hint.HintType = 0x3aa5;
		TestEqual(TEXT("15013 -> 0x160"), FElysiumNpc::SelectScheduleForHint(&Hint, 0.f, 0.f), 0x160);
		Hint.HintType = 0x3aaa;
		TestEqual(TEXT("15018 -> 0x164"), FElysiumNpc::SelectScheduleForHint(&Hint, 0.f, 0.f), 0x164);
		Hint.HintType = 0x3aa3;   // an unlisted type falls to the distance test
		TestEqual(TEXT("beyond the goal tolerance -> 0x15a"),
			FElysiumNpc::SelectScheduleForHint(&Hint, 33.0f, 32.0f), 0x15a);
		TestEqual(TEXT("at the tolerance exactly -> 0x15b, because retail's test is `>`"),
			FElysiumNpc::SelectScheduleForHint(&Hint, 32.0f, 32.0f), 0x15b);
		TestEqual(TEXT("inside it -> 0x15b"),
			FElysiumNpc::SelectScheduleForHint(&Hint, 1.0f, 32.0f), 0x15b);
	}

	// `SetHintActivity`'s switch (`0x103d6000`). Every case, with both draws.
	{
		auto Act = [](int32 Type, bool bPct, bool bFlip)
		{
			return FElysiumNpc::HintActivityForType(Type, bPct, bFlip);
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

	// Slot 568 `GetHintDelay` (`0x1026a910`): `FLD [0x104454c4] / RET 4`, the shared 0.0f.
	TestEqual(TEXT("GetHintDelay answers _DAT_104454c4, which is 0.0"), Npc->GetHintDelay(700),
		0.0f);
	TestEqual(TEXT("for every hint type, because the argument is not read"),
		Npc->GetHintDelay(12345), 0.0f);
	return true;
}

// -------------------------------------------------------------------------------------------------
// The Boss's centre-line geometry and the Chang jump path.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelHintsGeometryTest,
	"Elysium.Substrate.NpcKernelHints.Geometry", GHintsTestFlags)
bool FElysiumNpcKernelHintsGeometryTest::RunTest(const FString&)
{
	// `DistToHintCenterLine2D_3` (`0x103c6680`) — a point-to-INFINITE-LINE distance, not a segment
	// one: `t` is never clamped, so a point behind the line start still projects onto the line.
	{
		const FVector Start(0.0, 0.0, 0.0);
		const FVector Dir(1.0, 0.0, 0.0);
		TestEqual(TEXT("a point 5 to the side is 5 from the line"),
			FElysiumNpc::DistToHintCenterLine2D_3(Start, Dir, FVector(3.0, 5.0, 0.0)), 5.0f,
			KINDA_SMALL_NUMBER);
		TestEqual(TEXT("a point ON the line is 0 from it"),
			FElysiumNpc::DistToHintCenterLine2D_3(Start, Dir, FVector(100.0, 0.0, 0.0)), 0.0f,
			KINDA_SMALL_NUMBER);
		TestEqual(TEXT("a point BEHIND the start still projects — no clamp"),
			FElysiumNpc::DistToHintCenterLine2D_3(Start, Dir, FVector(-100.0, 4.0, 0.0)), 4.0f,
			KINDA_SMALL_NUMBER);
		TestEqual(TEXT("and Z is ignored, because _DAT_104454c4 is 0.0 and the caller zeroes Dir.Z"),
			FElysiumNpc::DistToHintCenterLine2D_3(Start, Dir, FVector(3.0, 5.0, 900.0)), 5.0f,
			KINDA_SMALL_NUMBER);
	}

	// `DistToHintCenterLine2D_2` (`0x103c6570`) — the hint's own origin and facing as the line, both
	// flattened. A hint at the origin facing +X (yaw 0) is the case above.
	{
		FElysiumNpc::FHintWords Hint = MakeHint(15000);
		Hint.OriginCm = FVector(0.0, 0.0, 700.0);   // the Z is thrown away
		Hint.Angles = FVector(0.0, 0.0, 0.0);
		TestEqual(TEXT("the hint's Z does not reach the answer"),
			FElysiumNpc::DistToHintCenterLine2D(Hint, FVector(3.0, 5.0, 0.0)), 5.0f,
			KINDA_SMALL_NUMBER);
		// A hint pitched steeply still measures flat, because the forward's Z is zeroed and the
		// remainder re-normalised before the projection.
		Hint.Angles = FVector(80.0, 0.0, 0.0);
		TestEqual(TEXT("and a steep pitch does not shrink the flattened direction"),
			FElysiumNpc::DistToHintCenterLine2D(Hint, FVector(3.0, 5.0, 0.0)), 5.0f,
			KINDA_SMALL_NUMBER);
	}

	// `DistToSegment` — the clamped form `CheckJumpPathToHintNode` tests against.
	{
		const FVector A(0.0, 0.0, 0.0);
		const FVector B(10.0, 0.0, 0.0);
		TestEqual(TEXT("beside the segment"), FElysiumNpc::DistToSegment(A, B, FVector(5.0, 4.0, 0.0)),
			4.0f, KINDA_SMALL_NUMBER);
		TestEqual(TEXT("past the far end it CLAMPS, unlike the centre-line body"),
			FElysiumNpc::DistToSegment(A, B, FVector(20.0, 0.0, 0.0)), 10.0f, KINDA_SMALL_NUMBER);
		// The degenerate arm answers `_DAT_104454c4`, the shared 0.0f — NOT the distance to the
		// point. For `CheckJumpPathToHintNode`, whose test is `dist < threshold`, a zero-length
		// segment therefore always reads as "something is on the line".
		TestEqual(TEXT("a degenerate segment answers 0.0, not the distance to its point"),
			FElysiumNpc::DistToSegment(A, A, FVector(0.0, 3.0, 0.0)), 0.0f, KINDA_SMALL_NUMBER);
		TestEqual(TEXT("before the near end it CLAMPS to that end"),
			FElysiumNpc::DistToSegment(A, B, FVector(-6.0, 8.0, 0.0)), 10.0f, KINDA_SMALL_NUMBER);
		TestEqual(TEXT("and Z reaches the answer: this is a 3D distance"),
			FElysiumNpc::DistToSegment(A, B, FVector(5.0, 0.0, 12.0)), 12.0f, KINDA_SMALL_NUMBER);
	}
	return true;
}

// -------------------------------------------------------------------------------------------------
// The Werewolf's hint state, and the two ring-buffer / group-key writes.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelHintsWerewolfTest,
	"Elysium.Substrate.NpcKernelHints.WerewolfHints", GHintsTestFlags)
bool FElysiumNpcKernelHintsWerewolfTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("kernel_hints_werewolf"), 29031u);
	Builder.AddNpc(TEXT("wolf"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("wolf"));
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
		Npc->LastJumpPositionIdx = 0;
		FElysiumNpc::FHintWords A = MakeHint(0);
		A.OriginCm = FVector(100.0 * ElysiumMove::U, 0.0, 0.0);
		FElysiumNpc::FHintWords B = MakeHint(0);
		B.OriginCm = FVector(200.0 * ElysiumMove::U, 0.0, 0.0);
		FElysiumNpc::FHintWords C = MakeHint(0);
		C.OriginCm = FVector(300.0 * ElysiumMove::U, 0.0, 0.0);
		Npc->AddHintToStoredJumpPositions(A);
		TestEqual(TEXT("slot 0 takes the first"), Npc->LastJumpPosition[0].X, 100.0, 0.01);
		TestEqual(TEXT("and the index advances"), Npc->LastJumpPositionIdx, 1);
		Npc->AddHintToStoredJumpPositions(B);
		TestEqual(TEXT("slot 1 takes the second"), Npc->LastJumpPosition[1].X, 200.0, 0.01);
		TestEqual(TEXT("and the index wraps at 2"), Npc->LastJumpPositionIdx, 0);
		Npc->AddHintToStoredJumpPositions(C);
		TestEqual(TEXT("the third overwrites the first"), Npc->LastJumpPosition[0].X, 300.0, 0.01);
		FElysiumNpc::FHintWords None;
		Npc->AddHintToStoredJumpPositions(None);
		TestEqual(TEXT("and a null hint writes nothing and does not advance"),
			Npc->LastJumpPositionIdx, 1);
	}

	// `IsImperativeTeleportHint` (`0x103d3360`) — every gate, by flag and by authored name.
	{
		FElysiumNpc::FHintWords Hint = MakeHint(0x3aa9);
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

		// The one arm with a trace behind it. The trace seam answers "blocked", so the arm refuses —
		// and that refusal IS the recovered answer for a trace that never ran.
		Npc->WerewolfDoorState = 2;
		Hint.HintType = 0x3aa4;
		Hint.Name = TEXT("jump_to_platform_hint_1");
		TestFalse(TEXT("the jump_to_platform arm asks the +0x9a4 trace seam, which blocks"),
			Npc->IsImperativeTeleportHint(Hint));
		TestTrue(TEXT("and the seam it asks is the blocked side"),
			Npc->WerewolfHintTrace(FVector::ZeroVector));
	}

	// `GetHintGroundpoint` (`0x103d6770`) — the authored table, then retail's warn-and-fall-back.
	{
		FElysiumNpc::FHintWords Hint = MakeHint(15000);
		Hint.NodeId = 42;
		Hint.OriginCm = FVector(10.0, 20.0, 30.0);
		Npc->WerewolfHintGroundpoints.Reset();
		Npc->WerewolfHintGroundpoints.Add({ 42, FVector(1.0, 2.0, 3.0) });
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
			"vec3_invalid because no trace ran"), FElysiumNpc::IsVec3Invalid(Fallback));
		TestFalse(TEXT("and vec3_invalid is not mistaken for an ordinary point"),
			FElysiumNpc::IsVec3Invalid(FVector(1.0, 2.0, 3.0)));

		// `PositionAtHint` therefore declines the move — the port's one named divergence here.
		const FVector Before = Npc->Origin;
		Npc->PositionAtHint(Hint);
		TestTrue(TEXT("PositionAtHint declines a vec3_invalid groundpoint rather than writing "
			"FLT_MAX into the origin"), Npc->Origin.Equals(Before, 0.001));

		// An authored row moves it for real, scaled out of source units.
		Npc->WerewolfHintGroundpoints.Add({ 42, FVector(100.0, 0.0, 0.0) });
		Npc->PositionAtHint(Hint);
		TestEqual(TEXT("an authored groundpoint places the body at it"), Npc->Origin.X,
			100.0 * ElysiumMove::U, 0.01);
		Npc->WerewolfHintGroundpoints.Reset();
	}

	// `SetHintGroup` (`0x102781e0`) — the write always lands; the slot-551 dispatch only on a change.
	Npc->SetHintGroup(TEXT("alpha"));
	TestEqual(TEXT("SetHintGroup writes m_strHintGroup"), Npc->ScheduleHost.HintGroup,
		FString(TEXT("alpha")));
	Npc->SetHintGroup(TEXT("alpha"));
	TestEqual(TEXT("re-setting the same group leaves it"), Npc->ScheduleHost.HintGroup,
		FString(TEXT("alpha")));
	Npc->SetHintGroup(TEXT("beta"));
	TestEqual(TEXT("and a change writes the new one"), Npc->ScheduleHost.HintGroup,
		FString(TEXT("beta")));
	return true;
}

// -------------------------------------------------------------------------------------------------
// The hint-idle activity and the interest loop.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelHintsInterestTest,
	"Elysium.Substrate.NpcKernelHints.Interest", GHintsTestFlags)
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
	Spot->MaxTime = 4.0f;   // a degenerate range pins the deadline exactly
	Npc->ClaimInterestingPlace(Spot, /*bClaimSecondary=*/false, 100.0);
	TestEqual(TEXT("the wait deadline is curtime + RandomFloat(min_time, max_time)"),
		Npc->ScheduleHost.WaitFinished, 104.0, 0.001);
	TestTrue(TEXT("the place is claimed"), Spot->IsEnabledFor(Npc->Handle));
	TestFalse(TEXT("no INTO activity means INTERESTING_INTO is NOT raised"),
		Npc->NpcFlags.Has(EElysiumNpcFlag::INTERESTING_INTO));
	TestEqual(TEXT("and the phase is the idle dwell (retail +0x6304 == 2)"),
		Npc->GetAmbientPhaseForDebug(), 3);

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
		Npc->GetAmbientPhaseForDebug(), 4);

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
	"Elysium.Substrate.NpcKernelHints.Seams", GHintsTestFlags)
bool FElysiumNpcKernelHintsSeamTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("kernel_hints_seams"), 29031u);
	Builder.AddNpc(TEXT("sabbat"), FVector::ZeroVector, TEXT("npc_VSabbatLeader"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("sabbat"));
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
	FElysiumNpc::FHintWords Words;
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
	TestTrue(TEXT("a hint index the seam cannot resolve is unusable"),
		Npc->IsHintUnusable(0, 10.0));

	// Slot 566's species entry point: this NPC IS a `CNPC_VSabbatLeader`, so a row exists — and the
	// refusal below is the seam's, not the table's.
	TestNotNull(TEXT("CNPC_VSabbatLeader carries a slot-566 row"),
		FElysiumNpc::HintTypeSpeciesOf(TEXT("CNPC_VSabbatLeader")));
	TestFalse(TEXT("but FValidateHintTypeForSpecies refuses, because the hint cannot be read"),
		Npc->FValidateHintTypeForSpecies(0));

	// `FindHintNode` (`0x10365780`) — the miss arm: `m_pHintNode` is written on BOTH paths, then
	// `TaskFail(4)`.
	AddExpectedError(TEXT("TaskFail 0x4"), EAutomationExpectedErrorFlags::Contains, 0);
	Npc->ScheduleHost.HintNode = 5;
	Npc->ScheduleHost.FailureReason = 0;
	TestFalse(TEXT("FindHintNode misses"), Npc->FindHintNode(15000, 0));
	TestEqual(TEXT("and still clears m_pHintNode, as retail's unconditional store does"),
		Npc->ScheduleHost.HintNode, int32(INDEX_NONE));
	TestEqual(TEXT("and fails the task with retail's reason 4"),
		Npc->ScheduleHost.FailureReason, 4);

	// `SelectTzimisceHintNode` (`0x103bfa50`) — the null-target arm zeroes the hint, and a
	// double miss returns 0 without touching it.
	Npc->ScheduleHost.HintNode = 5;
	TestEqual(TEXT("a null target answers 0"), Npc->SelectTzimisceHintNode(nullptr), 0);
	TestEqual(TEXT("and zeroes m_pHintNode"), Npc->ScheduleHost.HintNode, int32(INDEX_NONE));
	Npc->ScheduleHost.HintNode = 5;
	TestEqual(TEXT("two missed searches also answer 0"), Npc->SelectTzimisceHintNode(Npc), 0);
	TestEqual(TEXT("but leave m_pHintNode alone — only the null-target arm clears it"),
		Npc->ScheduleHost.HintNode, 5);

	// The two cover forwards. `0x10297430` refuses before calling the validator when the handle is
	// unset; `0x102974f0` has no such pre-check but still cannot resolve a hint.
	Npc->ScheduleHost.HintCoverObject = FElysiumEntityHandle();
	TestFalse(TEXT("IsHintCoverValid refuses with no m_hHintCoverObject"),
		Npc->IsHintCoverValid(0));
	TestFalse(TEXT("IsHintCoverValidLoose refuses too, at the hint rather than the handle"),
		Npc->IsHintCoverValidLoose(0));
	{
		FElysiumNpc::FHintWords Hint = MakeHint(0x27d8);
		TestFalse(TEXT("and the 0x10296c40 validator seam itself answers false"),
			Npc->ValidateHintCoverRange(Hint, nullptr, 0.0f, 0.731f));
	}

	// `FindHintEndEntity` (`0x103d6520`) — with both lookups refusing, retail's fallback is the
	// hint itself.
	{
		FElysiumNpc::FHintWords Hint = MakeHint(15000);
		Hint.NodeId = 13;
		Hint.TargetName = TEXT("end_of_the_line");
		TestEqual(TEXT("a hint whose target name resolves to nothing falls back to itself"),
			Npc->FindHintEndEntity(Hint), 13);
	}

	// `CheckJumpPathToHintNode` (`0x1036df50`) — the gates, in retail's order.
	{
		FElysiumNpc::FHintWords Hint = MakeHint(15000);
		Hint.OriginCm = FVector(500.0, 0.0, 0.0);

		const FElysiumEntityHandle CachedPlayer = Npc->Senses.Memory.ClosestPlayer;
		Npc->Senses.Memory.ClosestPlayer = FElysiumEntityHandle::Invalid();
		TestFalse(TEXT("no cached m_hClosestPlayer blocks the jump before anything else is asked"),
			Npc->CheckJumpPathToHintNode(Hint));
		Npc->Senses.Memory.ClosestPlayer = CachedPlayer;

		FElysiumNpc::FHintWords None;
		TestFalse(TEXT("and so does a hint the seam could not resolve"),
			Npc->CheckJumpPathToHintNode(None));

		// With a player cached the two segment arms pass — `_DAT_104ada34` is unrecovered and
		// stands at zero, so `dist < threshold` is never true — and the sector gate is what refuses.
		TestEqual(TEXT("the sector seam answers 4, the value that CLOSES the gate"),
			Npc->JumpPathSector(FVector::ZeroVector), 4);
		TestTrue(TEXT("the fixture cached a closest player"),
			Npc->Senses.Memory.ClosestPlayer.IsSet());
		TestFalse(TEXT("so the jump is refused on the sector gate, not on the segment distance"),
			Npc->CheckJumpPathToHintNode(Hint));
	}

	// `SetHintActivity` (`0x103d6000`) — a null hint refuses without drawing.
	{
		FElysiumNpc::FHintWords None;
		TestFalse(TEXT("SetHintActivity refuses a null hint"), Npc->SetHintActivity(None));
		FElysiumNpc::FHintWords Hint = MakeHint(0x3aa4);   // a type the switch does not carry
		TestFalse(TEXT("and a type outside the switch"), Npc->SetHintActivity(Hint));
		Hint.HintType = 0x3aaa;
		// A type the switch carries still answers true: `PositionAtHint` declining a `vec3_invalid`
		// groundpoint does not change `SetHintActivity`'s answer, which retail never reads back.
		TestTrue(TEXT("but positions at and plays a type it does"), Npc->SetHintActivity(Hint));
	}

	// `CAI_Hint::OnRestore` (slot 130, `0x102d3ec0`), handed over from family Sounds. With no AI
	// network, retail's "incorrect origin" arm is the only reachable one — and it IS an arm.
	AddExpectedError(TEXT("AI hint has incorrect origin"),
		EAutomationExpectedErrorFlags::Contains, 0);
	{
		const FElysiumNpc::FHintRestoreResult Restore =
			FElysiumNpc::HintOnRestore(MakeHint(15000));
		TestFalse(TEXT("no node was found"), Restore.bNodeFound);
		TestFalse(TEXT("so no node was claimed"), Restore.bClaimedNode);
		TestTrue(TEXT("and nothing was teleported"), Restore.NodeOriginCm.IsNearlyZero());
	}

	// The remaining seams, each asked once so a future implementer sees the call site in a trace.
	TestEqual(TEXT("ActivityIdForName answers retail's own -1"),
		Npc->ActivityIdForName(TEXT("ACT_IDLE")), int32(INDEX_NONE));
	TestFalse(TEXT("IsHintSequenceFinished answers false"), Npc->IsHintSequenceFinished());
	TestFalse(TEXT("DoesHintSequenceLoop answers false"), Npc->DoesHintSequenceLoop());
	TestNull(TEXT("InterestingPlaceMarkerOccupant answers nothing"),
		Npc->InterestingPlaceMarkerOccupant(nullptr));
	TestEqual(TEXT("CurrentRetailActivityId answers -1"), Npc->CurrentRetailActivityId(),
		int32(INDEX_NONE));
	TestTrue(TEXT("HintIdleActivityGate answers the passing side"), Npc->HintIdleActivityGate());
	{
		FString Unused;
		TestFalse(TEXT("PatrolNodeInterestRecordName answers nothing"),
			Npc->PatrolNodeInterestRecordName(0, Unused));
	}
	TestFalse(TEXT("IsTzimisceHintUsable answers false"), Npc->IsTzimisceHintUsable(0, Npc));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
