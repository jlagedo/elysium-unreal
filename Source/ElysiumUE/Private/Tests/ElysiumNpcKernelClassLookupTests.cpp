#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Tests/ElysiumNpcTestFixture.h"

// The species dispatcher story 29c-1 stands over 29b's census: classname -> retail class, class ->
// base chain, (class, slot) -> the body that fills it. Every family's per-species table goes through
// this reader, so the two facts that make the lookup non-trivial are asserted here once rather than
// rediscovered per family — a classname is claimed by every class in its chain, and a class with no
// body of its own at a slot inherits its base's.
//
// Content-free: the committed census is the whole input, plus one spawned NPC for the leaf's own
// `RetailClass()`.

static constexpr EAutomationTestFlags GElysiumNpcKernelClassFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelClassLookupTest,
	"Elysium.Substrate.NpcKernelClass.Lookup", GElysiumNpcKernelClassFlags)
bool FElysiumNpcKernelClassLookupTest::RunTest(const FString&)
{
	using namespace ElysiumNpcKernelClass;

	// The tree, by name.
	{
		const FElysiumNpcClass* Troika = Find(TEXT("CAI_BaseNPCTroika"));
		TestNotNull(TEXT("CAI_BaseNPCTroika is a census class"), Troika);
		TestEqual(TEXT("its direct base is CAI_BaseNPC"), FString(Troika->Base),
			FString(TEXT("CAI_BaseNPC")));
		TestEqual(TEXT("its table is 617 slots"), Troika->Slots, 617);
		TestNull(TEXT("a class outside the family is not found"), Find(TEXT("CNotAClass")));
	}

	// The chain walk.
	{
		const FElysiumNpcClass* Leader = Find(TEXT("CNPC_VSabbatLeader"));
		TestNotNull(TEXT("CNPC_VSabbatLeader is a census class"), Leader);
		TestTrue(TEXT("it is itself"), DerivesFrom(Leader, TEXT("CNPC_VSabbatLeader")));
		TestTrue(TEXT("it derives from CAI_BaseNPCTroika"),
			DerivesFrom(Leader, TEXT("CAI_BaseNPCTroika")));
		TestTrue(TEXT("and from CAI_BaseNPC"), DerivesFrom(Leader, TEXT("CAI_BaseNPC")));
		TestFalse(TEXT("but not from an unrelated species"),
			DerivesFrom(Leader, TEXT("CNPC_VWerewolf")));
		TestFalse(TEXT("and a null class derives from nothing"),
			DerivesFrom(nullptr, TEXT("CAI_BaseNPC")));
	}

	// The classname resolution, and the reason it needs the most-derived rule: the census records
	// `npc_VChangBros` on `CNPC_VChangBros` AND on its base `CNPC_VVampireBoss`, so a first-hit
	// lookup could answer either.
	{
		const FElysiumNpcClass* Chang = OfClassname(TEXT("npc_VChangBros"));
		TestNotNull(TEXT("npc_VChangBros resolves"), Chang);
		if (Chang != nullptr)
		{
			TestEqual(TEXT("to the most derived claimant, not its base"), FString(Chang->Name),
				FString(TEXT("CNPC_VChangBros")));
			TestTrue(TEXT("which still derives from CNPC_VVampireBoss"),
				DerivesFrom(Chang, TEXT("CNPC_VVampireBoss")));
		}
		const FElysiumNpcClass* Boss = OfClassname(TEXT("npc_VVampireBoss"));
		TestNotNull(TEXT("npc_VVampireBoss resolves"), Boss);
		if (Boss != nullptr)
		{
			TestEqual(TEXT("to CNPC_VVampireBoss itself"), FString(Boss->Name),
				FString(TEXT("CNPC_VVampireBoss")));
		}
		TestNull(TEXT("a classname no family class claims resolves to nothing"),
			OfClassname(TEXT("npc_not_a_classname")));
	}

	// Every classname the census records resolves, and resolves to a class that claims it.
	{
		int32 Claimed = 0;
		bool bAllResolve = true;
		for (const FElysiumNpcClass& Row : ElysiumNpcKernelShape::Classes())
		{
			for (int32 Index = 0; Index < Row.ClassnameCount; ++Index)
			{
				++Claimed;
				const FElysiumNpcClass* Resolved = OfClassname(FString(Row.Classnames[Index]));
				if (Resolved == nullptr || !DerivesFrom(Resolved, Row.Name))
				{
					bAllResolve = false;
				}
			}
		}
		TestTrue(TEXT("every census classname resolves to a class that derives from its claimant"),
			bAllResolve);
		TestTrue(TEXT("and the census claims at least the 77 recorded classnames"), Claimed >= 77);
	}

	// The slot resolution. Slot 546 `SquadSlotName` is the story's own worked example: the Troika
	// line carries a body and 57 species replace it.
	{
		const FElysiumNpcSlot* Row = SlotRow(546);
		TestNotNull(TEXT("slot 546 is a Troika-line row"), Row);
		TestNull(TEXT("a slot past the line is not"), SlotRow(700));
		TestNull(TEXT("nor is a negative index"), SlotRow(-1));
	}
	{
		// A class with no override of a slot answers its base's body, which is the vtable's own
		// rule; a class with one answers its own.
		const FElysiumNpcClass* Troika = Find(TEXT("CAI_BaseNPCTroika"));
		TestNull(TEXT("CAI_BaseNPCTroika is never an override row — its bodies ARE the line"),
			OverrideOf(Troika, 546));

		int32 Inherited = 0;
		int32 Own = 0;
		for (const FElysiumNpcClassSlot& Override : ElysiumNpcKernelShape::Overrides())
		{
			const FElysiumNpcClass* Cls = Find(Override.Class);
			if (Cls == nullptr)
			{
				continue;
			}
			const FElysiumNpcClassSlot* Nearest = OverrideOf(Cls, Override.Slot);
			if (Nearest == &Override)
			{
				++Own;
			}
			else if (Nearest != nullptr)
			{
				++Inherited;
			}
		}
		TestTrue(TEXT("every override row is the nearest answer for its own class"), Inherited == 0);
		TestEqual(TEXT("and the census's override count is reached"), Own,
			ElysiumNpcKernelShape::Overrides().Num());
	}
	{
		// The inheritance itself: a class whose own row is absent at a slot takes its nearest
		// ancestor's, which is what the vtable does, and every such answer comes from a class it
		// really derives from.
		int32 Inherited = 0;
		bool bEveryInheritedIsAnAncestor = true;
		bool bEveryInheritedBodyMatches = true;
		for (const FElysiumNpcClass& Row : ElysiumNpcKernelShape::Classes())
		{
			for (int32 Slot = 0; Slot < 617; ++Slot)
			{
				const FElysiumNpcClassSlot* Nearest = OverrideOf(&Row, Slot);
				if (Nearest == nullptr || FCString::Strcmp(Nearest->Class, Row.Name) == 0)
				{
					continue;
				}
				++Inherited;
				bEveryInheritedIsAnAncestor &= DerivesFrom(&Row, Nearest->Class);
				bEveryInheritedBodyMatches &=
					FCString::Strcmp(BodyOf(&Row, Slot), Nearest->Address) == 0;
			}
		}
		TestTrue(TEXT("some class inherits an override from its chain rather than declaring one"),
			Inherited > 0);
		TestTrue(TEXT("and every inherited override comes from a real ancestor"),
			bEveryInheritedIsAnAncestor);
		TestTrue(TEXT("and BodyOf answers that ancestor's address"), bEveryInheritedBodyMatches);
	}
	{
		// No override anywhere in the chain: the Troika-line body is the answer.
		const FElysiumNpcClass* Troika = Find(TEXT("CAI_BaseNPCTroika"));
		const FElysiumNpcSlot* Line = SlotRow(546);
		if (Line != nullptr)
		{
			TestEqual(TEXT("an unoverridden slot answers the Troika line's own body"),
				FString(BodyOf(Troika, 546)), FString(Line->Address));
		}
		TestEqual(TEXT("and a slot with no body at all answers the empty string"),
			FString(BodyOf(Troika, 700)), FString());
	}

	return true;
}

// The leaf's own answer, through a spawned entity rather than through a name.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcRetailClassTest,
	"Elysium.Substrate.NpcKernelClass.RetailClass", GElysiumNpcKernelClassFlags)
bool FElysiumNpcRetailClassTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_class"), 5150);
	Builder.AddNpc(TEXT("combatant"), FVector::ZeroVector, TEXT("npc_VHumanCombatant"));
	Builder.AddNpc(TEXT("leader"), FVector(200.0, 0.0, 0.0), TEXT("npc_VSabbatLeader"));
	Builder.AddNpc(TEXT("rat"), FVector(400.0, 0.0, 0.0), TEXT("npc_VRat"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));

	FElysiumNpc* Combatant = Fixture.Npc(TEXT("combatant"));
	FElysiumNpc* Leader = Fixture.Npc(TEXT("leader"));
	FElysiumNpc* Rat = Fixture.Npc(TEXT("rat"));
	TestNotNull(TEXT("the combatant spawned"), Combatant);
	TestNotNull(TEXT("the Sabbat leader spawned"), Leader);
	TestNotNull(TEXT("the rat spawned"), Rat);
	if (Combatant == nullptr || Leader == nullptr || Rat == nullptr)
	{
		return false;
	}

	TestEqual(TEXT("npc_VHumanCombatant IS CNPC_VHumanCombatant"),
		FString(Combatant->RetailClass() != nullptr ? Combatant->RetailClass()->Name : TEXT("")),
		FString(TEXT("CNPC_VHumanCombatant")));
	TestEqual(TEXT("npc_VSabbatLeader IS CNPC_VSabbatLeader"),
		FString(Leader->RetailClass() != nullptr ? Leader->RetailClass()->Name : TEXT("")),
		FString(TEXT("CNPC_VSabbatLeader")));
	TestEqual(TEXT("npc_VRat IS CNPC_VRat"),
		FString(Rat->RetailClass() != nullptr ? Rat->RetailClass()->Name : TEXT("")),
		FString(TEXT("CNPC_VRat")));

	TestTrue(TEXT("the Sabbat leader is a vampire boss"),
		Leader->IsRetailClass(TEXT("CNPC_VVampireBoss")));
	TestTrue(TEXT("and a Troika NPC"), Leader->IsRetailClass(TEXT("CAI_BaseNPCTroika")));
	TestFalse(TEXT("the rat is not a vampire boss"), Rat->IsRetailClass(TEXT("CNPC_VVampireBoss")));

	// The answer is latched, and latching it does not change it.
	TestEqual(TEXT("asking twice answers the same row"),
		reinterpret_cast<UPTRINT>(Leader->RetailClass()),
		reinterpret_cast<UPTRINT>(Leader->RetailClass()));

	return true;
}

#endif
