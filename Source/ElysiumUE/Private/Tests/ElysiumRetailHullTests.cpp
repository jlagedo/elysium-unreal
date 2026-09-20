// The two hull words, and the class table that fills them (0018 story 3, recovered 2026-09-20).
//
// Retail carries TWO hull indices on every combat character: `m_eHull` (+0x1568), which sizes the
// collision box and every trace, and an unnamed word at +0x156c, which `CAI_Navigator::SetGoal`
// caches and the A* family feeds to `CAI_Node::GetPosition`. The port's capsule follows the first
// and its NavMesh agent the second, so these tests pin the thing a wrong row would break: a body
// pathing on a mesh it does not fit.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumRetailHullTable.h"
#include "Tests/ElysiumNpcTestFixture.h"

static constexpr EAutomationTestFlags GElysiumRetailHullFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumRetailHullTableShapeTest,
	"Elysium.Substrate.RetailHull.Table", GElysiumRetailHullFlags)
bool FElysiumRetailHullTableShapeTest::RunTest(const FString&)
{
	// Most-derived first, because the resolver takes the first row this body's chain claims. A
	// base listed ahead of a species would claim every one of its descendants.
	int32 Troika = INDEX_NONE;
	int32 Human = INDEX_NONE;
	int32 Sheriff = INDEX_NONE;
	for (int32 Index = 0; Index < ElysiumRetailHulls::ClassHullCount; ++Index)
	{
		const FString Name = ElysiumRetailHulls::ClassHulls[Index].RetailClass;
		if (Name == TEXT("CAI_BaseNPCTroika")) { Troika = Index; }
		if (Name == TEXT("CNPC_VHuman"))       { Human = Index; }
		if (Name == TEXT("CNPC_VSheriffMan"))  { Sheriff = Index; }
	}
	TestTrue(TEXT("the Sheriff is listed before CNPC_VHuman"), Sheriff < Human);
	TestTrue(TEXT("CNPC_VHuman is listed before CAI_BaseNPCTroika"), Human < Troika);

	// Every hull a row names must be a row of the extent table, or the accessors -- which index
	// raw and bounds-check nothing -- would fault instead of answering. The one exception is
	// retail's own unassigned 23, which never survives construction.
	for (int32 Index = 0; Index < ElysiumRetailHulls::ClassHullCount; ++Index)
	{
		const ElysiumRetailHulls::FClassHulls& Row = ElysiumRetailHulls::ClassHulls[Index];
		if (FString(Row.RetailClass) == TEXT("CBaseCombatCharacter"))
		{
			TestNull(TEXT("the base sentinel 23 is deliberately outside the table"),
				ElysiumRetailHulls::Find(Row.Standing));
			continue;
		}
		TestNotNull(*FString::Printf(TEXT("%s's standing hull is a table row"), Row.RetailClass),
			ElysiumRetailHulls::Find(Row.Standing));
		TestNotNull(*FString::Printf(TEXT("%s's pathing hull is a table row"), Row.RetailClass),
			ElysiumRetailHulls::Find(Row.Pathing));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumRetailHullSpeciesTest,
	"Elysium.Substrate.RetailHull.Species", GElysiumRetailHullFlags)
bool FElysiumRetailHullSpeciesTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("retail_hulls"), 41011u);
	Builder.AddNpc(TEXT("guard"), FVector(0.f, 0.f, 0.f));
	Builder.AddNpc(TEXT("rat"), FVector(300.f, 0.f, 0.f), TEXT("npc_VRat"));
	Builder.AddNpc(TEXT("runner"), FVector(600.f, 0.f, 0.f), TEXT("npc_VTzimisceRunner"));
	FElysiumNpcWorldFixture World(MoveTemp(Builder));

	FElysiumNpc* Guard = World.Npc(TEXT("guard"));
	FElysiumNpc* Rat = World.Npc(TEXT("rat"));
	FElysiumNpc* Runner = World.Npc(TEXT("runner"));
	if (!TestNotNull(TEXT("the guard spawned"), Guard)
		|| !TestNotNull(TEXT("npc_VRat spawned"), Rat)
		|| !TestNotNull(TEXT("npc_VTzimisceRunner spawned"), Runner))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard, Rat, Runner });

	// A class with no row of its own holds what the nearest ancestor's constructor left, and for
	// everything under CAI_BaseNPC that is 0 -- that constructor zeroes both words before any
	// derived one runs.
	TestEqual(TEXT("a plain guard stands on HUMAN_HULL"), Guard->HullKind, 0);
	TestEqual(TEXT("and paths on it too"), Guard->PathingHullKind, 0);

	// THE witness for the whole table: CNPC_VRat has no constructor of its own. Its 19 arrives
	// through CNPC_VScurrying's, which is why the rows are ordered most-derived first.
	TestEqual(TEXT("a rat stands on RAT_HULL, inherited from CNPC_VScurrying"), Rat->HullKind, 19);
	TestEqual(TEXT("and paths on the rat mesh, not the human one"), Rat->PathingHullKind, 19);
	TestEqual(TEXT("so a rat's agent is the rat's"),
		ElysiumRetailHulls::AgentName(Rat->PathingHullKind), FName(TEXT("Rat")));

	TestEqual(TEXT("a Tzimisce runner takes its own row"), Runner->HullKind, 13);
	TestEqual(TEXT("both words alike"), Runner->PathingHullKind, 13);

	// The alternate-hull seam is no longer a seam: it answers the pathing word, so retail's
	// two-box debug arm is reachable for exactly the species retail reaches it for.
	TestEqual(TEXT("the alternate hull IS the pathing word"),
		Rat->RetailAlternateHullKind(), Rat->PathingHullKind);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumRetailHullSplitTest,
	"Elysium.Substrate.RetailHull.Split", GElysiumRetailHullFlags)
bool FElysiumRetailHullSplitTest::RunTest(const FString&)
{
	// The three species the two-word design EXISTS for. Every other class has both words alike,
	// so a table that silently collapsed to one word would pass every other test in this file.
	struct FSplit { const TCHAR* Class; int32 Standing; int32 Pathing; };
	static const FSplit Splits[] =
	{
		{ TEXT("CNPC_VSheriffMan"), 21, 0  },   // stands SHERIFF, routes on the human mesh
		{ TEXT("CNPC_VHengeyokai"),  0, 18 },   // the inverse
		{ TEXT("CNPC_VMingXiao"),   15, 16 },   // what MING_XIAO_PATHING_HULL exists for
	};
	for (const FSplit& Split : Splits)
	{
		const ElysiumRetailHulls::FClassHulls* Row = nullptr;
		for (int32 Index = 0; Index < ElysiumRetailHulls::ClassHullCount; ++Index)
		{
			if (FString(ElysiumRetailHulls::ClassHulls[Index].RetailClass) == Split.Class)
			{
				Row = &ElysiumRetailHulls::ClassHulls[Index];
				break;
			}
		}
		if (!TestNotNull(*FString::Printf(TEXT("%s has a row"), Split.Class), Row))
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s stands on %d"), Split.Class, Split.Standing),
			Row->Standing, Split.Standing);
		TestEqual(*FString::Printf(TEXT("%s paths on %d"), Split.Class, Split.Pathing),
			Row->Pathing, Split.Pathing);
		TestNotEqual(*FString::Printf(TEXT("%s's two words differ"), Split.Class),
			Row->Standing, Row->Pathing);
	}

	// And the agent that follows from the split: the Sheriff needs no agent of his own, because
	// he routes on the human's mesh however large the box he stands in.
	TestEqual(TEXT("the Sheriff's agent is the human's"),
		ElysiumRetailHulls::AgentName(0), FName(TEXT("Human")));
	return true;
}

#endif
