#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumClassRegistry.h"
#include "ElysiumEntityDefs.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcChangBros.h"
#include "Substrate/ElysiumNpcChangBrosBlade.h"
#include "Substrate/ElysiumNpcChangBrosClaw.h"
#include "Substrate/ElysiumNpcHumanCombatant.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcRat.h"
#include "Substrate/ElysiumNpcSabbatLeader.h"
#include "Substrate/ElysiumNpcVampireBoss.h"
#include "Tests/ElysiumNpcDeadClasses.h"
#include "Tests/ElysiumNpcTestCensus.h"
#include "Tests/ElysiumNpcTestFixture.h"

// The class tree answers retail's type questions (0019 story 5 commit B).
//
// Retail asks "is this a `CNPC_VChangBros`" with `__RTDynamicCast`, which walks the RTTI class
// hierarchy of the object's complete class. The port asks the same with `AsSpecies<T>()`, which
// each class of the tree answers through `IsNpcClass`: its own census row, then its C++ base's
// answer. These cases hold that chain to the census's base column for every classname the registry
// builds, and pin the casts retail's bodies perform. The census row a class answers is its identity
// (logs, the census and factory tests), never a dispatch key; nothing in the runtime walks the
// census by name.

static constexpr EAutomationTestFlags GElysiumNpcKernelClassTreeFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelClassTreeChainTest,
	"Elysium.Substrate.NpcKernelClass.TreeMatchesCensus", GElysiumNpcKernelClassTreeFlags)
bool FElysiumNpcKernelClassTreeChainTest::RunTest(const FString&)
{
	// Every constructible NPC-line classname in the registry, built through its factory: the typed
	// test admits exactly the census rows on its class's base chain below the two base lines, and
	// refuses every other census row.
	const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
	TArray<FName> Classnames;
	Reg.ForEach([&Classnames](const FElysiumClassDesc& Desc)
	{
		if (Desc.bAbstract)
		{
			return;
		}
		for (const FElysiumNpcClass& Row : ElysiumNpcKernelShape::Classes())
		{
			for (int32 Index = 0; Index < Row.ClassnameCount; ++Index)
			{
				if (Desc.ClassName == FName(Row.Classnames[Index]) && !ElysiumNpcDeadClasses::Contains(Row.Name))
				{
					Classnames.AddUnique(Desc.ClassName);
				}
			}
		}
	});
	TestEqual(TEXT("the registry builds the 54 live factory classnames"), Classnames.Num(), 54);

	int32 Checked = 0;
	for (const FName& Classname : Classnames)
	{
		FElysiumEntityDef Def;
		Def.Classname = Classname.ToString();
		TUniquePtr<FElysiumEntity> Entity = Reg.Create(Def, FElysiumEntityHandle(0, 1));
		const FElysiumNpcBase* Npc = Entity.IsValid() ? Entity->AsNpcBase() : nullptr;
		if (!TestNotNull(FString::Printf(TEXT("%s builds an NPC"), *Def.Classname), Npc))
		{
			continue;
		}
		const FElysiumNpcClass* Own = Npc->RetailClass();
		if (!TestNotNull(FString::Printf(TEXT("%s answers a census row"), *Def.Classname), Own))
		{
			continue;
		}
		TestTrue(FString::Printf(TEXT("%s's census row names it"), *Def.Classname),
			ElysiumNpcTestCensus::OfClassname(Def.Classname) == Own);
		for (const FElysiumNpcClass& Row : ElysiumNpcKernelShape::Classes())
		{
			const bool bLine = FCString::Strcmp(Row.Name, TEXT("CAI_BaseNPC")) == 0
				|| FCString::Strcmp(Row.Name, TEXT("CAI_BaseNPCTroika")) == 0;
			const bool bExpected = !bLine && ElysiumNpcTestCensus::DerivesFrom(Own, Row.Name);
			TestEqual(FString::Printf(TEXT("%s (%s) IS a %s"), *Def.Classname, Own->Name, Row.Name),
				Npc->IsNpcClass(&Row), bExpected);
		}
		++Checked;
	}
	TestEqual(TEXT("every live classname was checked against the census chain"), Checked, 54);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelClassTreeCastsTest,
	"Elysium.Substrate.NpcKernelClass.TypedCasts", GElysiumNpcKernelClassTreeFlags)
bool FElysiumNpcKernelClassTreeCastsTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_class_tree"), 5150);
	Builder.AddNpc(TEXT("combatant"), FVector::ZeroVector, TEXT("npc_VHumanCombatant"));
	Builder.AddNpc(TEXT("leader"), FVector(200.0, 0.0, 0.0), TEXT("npc_VSabbatLeader"));
	Builder.AddNpc(TEXT("rat"), FVector(400.0, 0.0, 0.0), TEXT("npc_VRat"));
	Builder.AddNpc(TEXT("cop"), FVector(600.0, 0.0, 0.0), TEXT("npc_VCop"));
	Builder.AddNpc(TEXT("chang"), FVector(800.0, 0.0, 0.0), TEXT("npc_VChangBros"));
	Builder.AddNpc(TEXT("blade"), FVector(1000.0, 0.0, 0.0), TEXT("npc_VChangBrosBlade"));
	Builder.AddNpc(TEXT("claw"), FVector(1200.0, 0.0, 0.0), TEXT("npc_VChangBrosClaw"));
	Builder.AddTroikaNpc(TEXT("troika"), FVector(1400.0, 0.0, 0.0));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));

	FElysiumNpc* Combatant = Fixture.Npc(TEXT("combatant"));
	FElysiumNpc* Leader = Fixture.Npc(TEXT("leader"));
	FElysiumNpc* Rat = Fixture.Npc(TEXT("rat"));
	FElysiumNpc* Cop = Fixture.Npc(TEXT("cop"));
	FElysiumNpc* Chang = Fixture.Npc(TEXT("chang"));
	FElysiumNpc* Blade = Fixture.Npc(TEXT("blade"));
	FElysiumNpc* Claw = Fixture.Npc(TEXT("claw"));
	FElysiumNpc* Troika = Fixture.Npc(TEXT("troika"));
	for (const FElysiumNpc* Npc : { Combatant, Leader, Rat, Cop, Chang, Blade, Claw, Troika })
	{
		if (!TestNotNull(TEXT("the fixture stood every NPC"), Npc))
		{
			return false;
		}
	}

	// The identity: the census row of the class the factory built, the same row on every call.
	TestTrue(TEXT("npc_VSabbatLeader IS CNPC_VSabbatLeader"),
		Leader->RetailClass() == ElysiumNpcKernelShape::ClassNamed(TEXT("CNPC_VSabbatLeader")));
	TestTrue(TEXT("and its class's static row is that row"),
		Leader->RetailClass() == FElysiumNpcSabbatLeader::StaticRetailClass());
	// `npc_VCop`'s factory `0x103704f0` builds `CNPC_VCop`; it used to resolve to no class.
	TestEqual(TEXT("npc_VCop IS CNPC_VCop"),
		FString(Cop->RetailClass() != nullptr ? Cop->RetailClass()->Name : TEXT("")),
		FString(TEXT("CNPC_VCop")));
	TestNull(TEXT("the bare Troika line is no class of the tree"), Troika->RetailClass());

	// The typed tests, down the tree.
	TestNotNull(TEXT("the Sabbat leader is a vampire boss"), Leader->AsSpecies<FElysiumNpcVampireBoss>());
	TestNull(TEXT("the rat is not a vampire boss"), Rat->AsSpecies<FElysiumNpcVampireBoss>());
	TestNotNull(TEXT("the rat is a CNPC_VRat"), Rat->AsSpecies<FElysiumNpcRat>());
	TestNotNull(TEXT("the cop is a human combatant"), Cop->AsSpecies<FElysiumNpcHumanCombatant>());
	TestNull(TEXT("but not a vampire boss"), Cop->AsSpecies<FElysiumNpcVampireBoss>());
	TestNull(TEXT("a combatant is not a cop's subclass"), Combatant->AsSpecies<FElysiumNpcSabbatLeader>());
	TestNull(TEXT("the bare Troika line answers no class"), Troika->AsSpecies<FElysiumNpcHumanCombatant>());

	// `___RTDynamicCast(member, 0, CNPC_VChangBros)` (`CNPC_VChangBros::GetOtherBrother` `0x1036e2f0`,
	// `CanJump`, `IsValidTeleportPosition`): the Blade and the Claw ARE `CNPC_VChangBros`, and the
	// cast answers the same object.
	TestTrue(TEXT("a ChangBros is a ChangBros"), Chang->AsSpecies<FElysiumNpcChangBros>() == Chang);
	TestTrue(TEXT("the Blade is a ChangBros"),
		static_cast<FElysiumNpc*>(Blade->AsSpecies<FElysiumNpcChangBros>()) == Blade);
	TestTrue(TEXT("the Claw is a ChangBros"),
		static_cast<FElysiumNpc*>(Claw->AsSpecies<FElysiumNpcChangBros>()) == Claw);
	TestNull(TEXT("but the base ChangBros is no Blade"), Chang->AsSpecies<FElysiumNpcChangBrosBlade>());
	TestNull(TEXT("and the Blade is no Claw"), Blade->AsSpecies<FElysiumNpcChangBrosClaw>());
	TestNull(TEXT("the Sabbat leader, a vampire boss too, is no ChangBros"),
		Leader->AsSpecies<FElysiumNpcChangBros>());
	return true;
}

#endif
