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
// rediscovered per family — a classname is claimed by exactly the one class retail's factory builds
// for it, and a class with no body of its own at a slot inherits its base's.
//
// Content-free: the committed census is the whole input, plus spawned NPCs for the leaf's own
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

	// The classname resolution. The census classnames are read off retail's factories: every NPC
	// classname has exactly one `LINK_ENTITY_TO_CLASS` factory, and the class it builds is the last
	// primary-vtable write at `[this]` (`docs/vtmb/npc-ai/population.md`, "The classname → class
	// map, read from the factories" and "Step 0 factory boundary replay": 74 factories replayed on
	// the pinned `vampire.dll`). So a classname has one claimant and needs no tie-break; the old
	// "most-derived claimant" rule only undid a proximity survey's over-claims and is gone.
	{
		struct FFactoryRow
		{
			const TCHAR* Classname;
			const TCHAR* Builds;
		};
		static const FFactoryRow FactoryRows[] =
		{
			// Only `CNPC_VChangBros` claims it; its base `CNPC_VVampireBoss` does not.
			{ TEXT("npc_VChangBros"), TEXT("CNPC_VChangBros") },
			// Factory `0x103c4fa0` (inline); the proximity census resolved it to nothing.
			{ TEXT("npc_VVampireBoss"), TEXT("CNPC_VVampireBoss") },
			// Factory `0x103704f0` (ctor `0x103708e0`); the proximity census resolved it to nothing.
			{ TEXT("npc_VCop"), TEXT("CNPC_VCop") },
			// Factory `0x103ddd70`; the proximity census resolved it to nothing.
			{ TEXT("npc_VZombie"), TEXT("CNPC_VZombie") },
			// Aliases keep distinct factories over one class.
			{ TEXT("npc_VMercurio"), TEXT("CNPC_ProneDialog") },
			{ TEXT("npc_VProneDialog"), TEXT("CNPC_ProneDialog") },
			{ TEXT("npc_VDialogPedestrian"), TEXT("CNPC_VPedestrian") },
		};
		for (const FFactoryRow& Row : FactoryRows)
		{
			const FElysiumNpcClass* Resolved = OfClassname(Row.Classname);
			TestEqual(*FString::Printf(TEXT("%s resolves to its factory's class %s"), Row.Classname,
					Row.Builds),
				FString(Resolved != nullptr ? Resolved->Name : TEXT("")), FString(Row.Builds));
		}
		TestTrue(TEXT("CNPC_VChangBros still derives from CNPC_VVampireBoss"),
			DerivesFrom(OfClassname(TEXT("npc_VChangBros")), TEXT("CNPC_VVampireBoss")));
		const FElysiumNpcClass* BaseBoss = Find(TEXT("CNPC_VBaseBoss"));
		TestNotNull(TEXT("CNPC_VBaseBoss is a census class"), BaseBoss);
		if (BaseBoss != nullptr)
		{
			TestEqual(TEXT("but it is abstract: no factory builds it, so it claims no classname"),
				BaseBoss->ClassnameCount, 0);
		}
		TestNull(TEXT("a classname no family class claims resolves to nothing"),
			OfClassname(TEXT("npc_not_a_classname")));
	}

	// Every classname the census records has exactly one claimant, and resolves to it.
	{
		int32 Claimed = 0;
		TMap<FString, int32> Claimants;
		bool bAllResolve = true;
		for (const FElysiumNpcClass& Row : ElysiumNpcKernelShape::Classes())
		{
			for (int32 Index = 0; Index < Row.ClassnameCount; ++Index)
			{
				++Claimed;
				++Claimants.FindOrAdd(FString(Row.Classnames[Index]));
				if (OfClassname(FString(Row.Classnames[Index])) != &Row)
				{
					bAllResolve = false;
				}
			}
		}
		bool bOneClaimantEach = true;
		for (const TPair<FString, int32>& Pair : Claimants)
		{
			if (Pair.Value != 1)
			{
				bOneClaimantEach = false;
				AddError(FString::Printf(TEXT("%s has %d claimants"), *Pair.Key, Pair.Value));
			}
		}
		TestTrue(TEXT("every census classname has exactly one claimant"), bOneClaimantEach);
		TestTrue(TEXT("and resolves to that claimant"), bAllResolve);
		// The 74 retail NPC-vtable factories: 68 NPC classnames, the three makers and the three
		// directors (`scripted_sequence`, `aiscripted_sequence`, `aiscripted_schedule`).
		TestEqual(TEXT("the census claims the 74 factory classnames"), Claimed, 74);
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
	Builder.AddNpc(TEXT("cop"), FVector(600.0, 0.0, 0.0), TEXT("npc_VCop"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));

	FElysiumNpc* Combatant = Fixture.Npc(TEXT("combatant"));
	FElysiumNpc* Leader = Fixture.Npc(TEXT("leader"));
	FElysiumNpc* Rat = Fixture.Npc(TEXT("rat"));
	FElysiumNpc* Cop = Fixture.Npc(TEXT("cop"));
	TestNotNull(TEXT("the combatant spawned"), Combatant);
	TestNotNull(TEXT("the Sabbat leader spawned"), Leader);
	TestNotNull(TEXT("the rat spawned"), Rat);
	TestNotNull(TEXT("the cop spawned"), Cop);
	if (Combatant == nullptr || Leader == nullptr || Rat == nullptr || Cop == nullptr)
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
	// `npc_VCop`'s factory `0x103704f0` builds `CNPC_VCop` (population.md, "The classname → class
	// map, read from the factories"); it used to resolve to no class and run as the bare Troika line.
	TestEqual(TEXT("npc_VCop IS CNPC_VCop"),
		FString(Cop->RetailClass() != nullptr ? Cop->RetailClass()->Name : TEXT("")),
		FString(TEXT("CNPC_VCop")));

	TestTrue(TEXT("the Sabbat leader is a vampire boss"),
		Leader->IsRetailClass(TEXT("CNPC_VVampireBoss")));
	TestTrue(TEXT("and a Troika NPC"), Leader->IsRetailClass(TEXT("CAI_BaseNPCTroika")));
	TestFalse(TEXT("the rat is not a vampire boss"), Rat->IsRetailClass(TEXT("CNPC_VVampireBoss")));
	TestTrue(TEXT("the cop is a human combatant"), Cop->IsRetailClass(TEXT("CNPC_VHumanCombatant")));
	TestTrue(TEXT("and a Troika NPC"), Cop->IsRetailClass(TEXT("CAI_BaseNPCTroika")));
	TestFalse(TEXT("but not a vampire boss"), Cop->IsRetailClass(TEXT("CNPC_VVampireBoss")));

	// The answer is the C++ class's own (`OwnRetailClass`, story 5 step 2), not a latch: the census
	// row of the class the classname built, the same row on every call.
	TestEqual(TEXT("the C++ class answers its own census row"),
		reinterpret_cast<UPTRINT>(Leader->RetailClass()),
		reinterpret_cast<UPTRINT>(ElysiumNpcKernelClass::Find(TEXT("CNPC_VSabbatLeader"))));
	TestEqual(TEXT("asking twice answers the same row"),
		reinterpret_cast<UPTRINT>(Leader->RetailClass()),
		reinterpret_cast<UPTRINT>(Leader->RetailClass()));

	return true;
}

#endif
