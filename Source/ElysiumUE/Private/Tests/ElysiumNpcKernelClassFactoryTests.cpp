#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumClassRegistry.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcMaker.h"
#include "Substrate/ElysiumNpcMakerFleshpile.h"
#include "Substrate/ElysiumNpcMakerZombie.h"
#include "Substrate/ElysiumNpcTestHull.h"
#include "Tests/ElysiumNpcDeadClasses.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumNpcTestCensus.h"

// Story 5 step 2: the classname -> class map is retail's factories
// (`research/tooling/ghidra/driver/kernel_factories.tsv`, replayed from the 74 factories of
// `vampire.dll`; `docs/vtmb/npc-ai/population.md`, "The classname -> class map, read from the
// factories"). Every living ordinary-NPC classname builds its own C++ class, which answers its own
// census row; the retail classes above it are abstract descriptors a map cannot stand; the dead
// names stay unregistered. Commit A folded the ten deferred classes (the test hull, which has no
// classname, at fold A1; the controller line at A2; the directors at A3; the makers at A4), so no
// live retail class is deferred any more.

static constexpr EAutomationTestFlags GElysiumNpcKernelFactoryFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	struct FFactoryRow
	{
		const TCHAR* Classname;
		const TCHAR* RetailClass;
	};

	// The 45 ordinary-NPC classnames of the 44 step-2 classes (two classes carry two each).
	const FFactoryRow GStep2Factories[] =
	{
		{ TEXT("npc_payphone"), TEXT("CPayphone") },
		{ TEXT("npc_VAndreiBlood"), TEXT("CNPC_VAndreiBlood") },
		{ TEXT("npc_VAnimal"), TEXT("CNPC_VAnimal") },
		{ TEXT("npc_VAsianVampire"), TEXT("CNPC_VAsianVampire") },
		{ TEXT("npc_VBach"), TEXT("CNPC_VBach") },
		{ TEXT("npc_VBrujah"), TEXT("CNPC_VBrujah") },
		{ TEXT("npc_VCamera"), TEXT("CNPC_VCamera") },
		{ TEXT("npc_VCameraSecurity"), TEXT("CNPC_VCameraSecurity") },
		{ TEXT("npc_VChangBros"), TEXT("CNPC_VChangBros") },
		{ TEXT("npc_VChangBrosBlade"), TEXT("CNPC_VChangBrosBlade") },
		{ TEXT("npc_VChangBrosClaw"), TEXT("CNPC_VChangBrosClaw") },
		{ TEXT("npc_VCop"), TEXT("CNPC_VCop") },
		{ TEXT("npc_VDialogPedestrian"), TEXT("CNPC_VPedestrian") },
		{ TEXT("npc_VDog"), TEXT("CNPC_VDog") },
		{ TEXT("npc_VGargoyle"), TEXT("CNPC_VGargoyle") },
		{ TEXT("npc_VGhoulCroucher"), TEXT("CNPC_VGhoulCroucher") },
		{ TEXT("npc_VGuard1"), TEXT("CNPC_VGuard1") },
		{ TEXT("npc_VHengeyokai"), TEXT("CNPC_VHengeyokai") },
		{ TEXT("npc_VHuman"), TEXT("CNPC_VHuman") },
		{ TEXT("npc_VHumanCombatant"), TEXT("CNPC_VHumanCombatant") },
		{ TEXT("npc_VHumanCombatPatrol"), TEXT("CNPC_VHumanCombatPatrol") },
		{ TEXT("npc_VHunter"), TEXT("CNPC_VHunter") },
		{ TEXT("npc_VLasombra"), TEXT("CNPC_VLasombra") },
		{ TEXT("npc_VManBat"), TEXT("CNPC_VManBat") },
		{ TEXT("npc_VMercurio"), TEXT("CNPC_ProneDialog") },
		{ TEXT("npc_VMingXiao"), TEXT("CNPC_VMingXiao") },
		{ TEXT("npc_VMingXiaoTentacle"), TEXT("CNPC_VMingXiaoTentacle") },
		{ TEXT("npc_VNewscaster"), TEXT("CNPC_VNewscaster") },
		{ TEXT("npc_VPedestrian"), TEXT("CNPC_VPedestrian") },
		{ TEXT("npc_VPlaceholder"), TEXT("CNPC_VPlaceholder") },
		{ TEXT("npc_VProneDialog"), TEXT("CNPC_ProneDialog") },
		{ TEXT("npc_VRat"), TEXT("CNPC_VRat") },
		{ TEXT("npc_VSabbatGunman"), TEXT("CNPC_VSabbatGunman") },
		{ TEXT("npc_VSabbatLeader"), TEXT("CNPC_VSabbatLeader") },
		{ TEXT("npc_VScurrying"), TEXT("CNPC_VScurrying") },
		{ TEXT("npc_VSheriffMan"), TEXT("CNPC_VSheriffMan") },
		{ TEXT("npc_VTaxiDriver"), TEXT("CNPC_VTaxiDriver") },
		{ TEXT("npc_VTzimisce"), TEXT("CNPC_VTzimisce") },
		{ TEXT("npc_VTzimisceHeadClaw"), TEXT("CNPC_VTzimisceHeadClaw") },
		{ TEXT("npc_VTzimisceRunner"), TEXT("CNPC_VTzimisceRunner") },
		{ TEXT("npc_VVampire"), TEXT("CNPC_VVampire") },
		{ TEXT("npc_VVampireBoss"), TEXT("CNPC_VVampireBoss") },
		{ TEXT("npc_VWerewolf"), TEXT("CNPC_VWerewolf") },
		{ TEXT("npc_VYukie"), TEXT("CNPC_VYukie") },
		{ TEXT("npc_VZombie"), TEXT("CNPC_VZombie") },
	};

	// The controller line's three classnames (story 5 fold A2). Ordinary NPC factories, listed apart
	// because the controller's `Spawn` (`0x103a4510`, inherited by both children) renames the body
	// `playercontroller`, so a fixture finds these by class rather than by targetname.
	const FFactoryRow GControllerLineFactories[] =
	{
		{ TEXT("npc_VFrenzyShadow"), TEXT("CNPC_VFrenzyShadow") },
		{ TEXT("npc_VPlayerController"), TEXT("CNPC_VPlayerController") },
		{ TEXT("npc_VWolfMorph"), TEXT("CNPC_VWolfMorph") },
	};

	// The script directors' three classnames (story 5 fold A3): `CCineNPC` under `CAI_BaseNPC` and its
	// two twins under it. NPC-base classes, not Troika: `AsNpcBase` answers, `AsNpc` does not.
	const FFactoryRow GDirectorFactories[] =
	{
		{ TEXT("aiscripted_schedule"), TEXT("CCineAISchedule") },
		{ TEXT("aiscripted_sequence"), TEXT("CCineAI") },
		{ TEXT("scripted_sequence"), TEXT("CCineNPC") },
	};

	// Classnames whose factory builds a class with no instance in shipped content: census only.
	const FFactoryRow GDeadClassnames[] =
	{
		{ TEXT("monster_generic"), TEXT("CGenericNPC") },
		{ TEXT("npc_bullseye"), TEXT("CNPC_Bullseye") },
		{ TEXT("npc_crow"), TEXT("CNPC_Crow") },
		{ TEXT("npc_generic"), TEXT("CGeneric_NPC") },
		{ TEXT("npc_generic_bathack"), TEXT("CGeneric_NPC_bathack") },
		{ TEXT("npc_sabbat"), TEXT("CGenericSabbat_NPC") },
		{ TEXT("npc_TestBaseHumanoid"), TEXT("CAI_BaseHumanoid") },
		{ TEXT("npc_VBatSwarm"), TEXT("CNPC_VBatSwarm") },
		{ TEXT("npc_VCombatman"), TEXT("CNPC_VCombatman") },
		{ TEXT("npc_VGangrel"), TEXT("CNPC_VGangrel") },
		{ TEXT("npc_VMalkavian"), TEXT("CNPC_VMalkavian") },
		{ TEXT("npc_VMoleman"), TEXT("CNPC_VMoleman") },
		{ TEXT("npc_VNosferatu"), TEXT("CNPC_VNosferatu") },
		{ TEXT("npc_VSheriffSwarm"), TEXT("CNPC_VSheriffSwarm") },
		{ TEXT("npc_VStalker"), TEXT("CNPC_VStalker") },
		{ TEXT("npc_VTest"), TEXT("CNPC_VTest") },
		{ TEXT("npc_VToreador"), TEXT("CNPC_VToreador") },
		{ TEXT("npc_VTremere"), TEXT("CNPC_VTremere") },
		{ TEXT("npc_VVentrue"), TEXT("CNPC_VVentrue") },
		{ TEXT("scripted_target"), TEXT("CScriptedTarget") },
	};

	// The makers' three classnames (story 5 fold A4): `CNPCMaker` IS a `CAI_BaseNPCTroika`, and its
	// two variants derive from it. Troika NPCs by class, with no body (`AsNpc` answers).
	const FFactoryRow GMakerFactories[] =
	{
		{ TEXT("npc_maker"), TEXT("CNPCMaker") },
		{ TEXT("npc_maker_fleshpile"), TEXT("CNPCMaker_Fleshpile") },
		{ TEXT("npc_maker_zombie"), TEXT("CNPCMaker_Zombie") },
	};

	// The registry's base chain above `Desc`, as names.
	TArray<FString> RegistryChain(const FElysiumClassDesc& Desc)
	{
		TArray<FString> Chain;
		const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
		for (const FElysiumClassDesc* D = Reg.Find(Desc.BaseName); D != nullptr;
			D = D->BaseName.IsNone() ? nullptr : Reg.Find(D->BaseName))
		{
			Chain.Add(D->ClassName.ToString());
		}
		return Chain;
	}

	// The census chain from `RetailClass` up to `CAI_BaseNPC` (through `CAI_BaseNPCTroika`, story 5
	// step 5), then the combat character it hangs from.
	TArray<FString> ProjectedChain(const TCHAR* RetailClass)
	{
		TArray<FString> Chain;
		for (const FElysiumNpcClass* Row = ElysiumNpcTestCensus::Find(RetailClass); Row != nullptr;
			Row = ElysiumNpcTestCensus::Find(Row->Base))
		{
			Chain.Add(FString(Row->Name));
			if (FCString::Strcmp(Row->Name, TEXT("CAI_BaseNPC")) == 0)
			{
				break;
			}
		}
		Chain.Add(ElysiumCombatCharacterClassName().ToString());
		return Chain;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelClassFactoriesTest,
	"Elysium.Substrate.NpcKernelClass.Factories", GElysiumNpcKernelFactoryFlags)
bool FElysiumNpcKernelClassFactoriesTest::RunTest(const FString&)
{
	TestEqual(TEXT("the step-2 classnames"), static_cast<int32>(UE_ARRAY_COUNT(GStep2Factories)), 45);

	// Every classname stands in one world through the map-load path: a live NPC of its own class,
	// not a record, whose descriptor derives exactly as retail's class does.
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_factories"), 5150);
	Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(GStep2Factories); ++Index)
	{
		Builder.AddNpc(GStep2Factories[Index].Classname, FVector(400.f * Index, 3000.f, 0.f),
			GStep2Factories[Index].Classname);
	}
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(GControllerLineFactories); ++Index)
	{
		Builder.AddNpc(GControllerLineFactories[Index].Classname,
			FVector(400.f * Index, 6000.f, 0.f), GControllerLineFactories[Index].Classname);
	}
	FElysiumNpcWorldFixture F(MoveTemp(Builder));
	TArray<FFactoryRow> Rows(GStep2Factories, UE_ARRAY_COUNT(GStep2Factories));
	Rows.Append(GControllerLineFactories, UE_ARRAY_COUNT(GControllerLineFactories));
	for (const FFactoryRow& Row : Rows)
	{
		// A controller-line body renames itself `playercontroller` in its `Spawn`, so it is found
		// by its class.
		FElysiumEntity* Entity = F.World.FindByName(Row.Classname);
		if (Entity == nullptr)
		{
			Entity = F.NpcOfClass(Row.RetailClass);
		}
		if (!TestNotNull(FString::Printf(TEXT("%s stands"), Row.Classname), Entity))
		{
			continue;
		}
		const FElysiumNpc* Npc = Entity->AsNpc();
		if (!TestNotNull(FString::Printf(TEXT("%s is an NPC"), Row.Classname), Npc))
		{
			continue;
		}
		TestFalse(FString::Printf(TEXT("%s is not a record"), Row.Classname), Entity->IsRecordOnly());
		const FElysiumNpcClass* Cls = Npc->RetailClass();
		TestEqual(FString::Printf(TEXT("%s answers its factory's class"), Row.Classname),
			Cls != nullptr ? FString(Cls->Name) : FString(), FString(Row.RetailClass));
		TestTrue(FString::Printf(TEXT("%s's census row names the classname"), Row.Classname),
			ElysiumNpcTestCensus::OfClassname(Row.Classname) == Cls);
		if (TestNotNull(FString::Printf(TEXT("%s has a descriptor"), Row.Classname), Entity->Class))
		{
			const TArray<FString> Chain = RegistryChain(*Entity->Class);
			const TArray<FString> Expected = ProjectedChain(Row.RetailClass);
			const bool bPrefix = Chain.Num() >= Expected.Num()
				&& TArray<FString>(Chain.GetData(), Expected.Num()) == Expected;
			TestTrue(FString::Printf(TEXT("%s's descriptor chain [%s] begins [%s]"), Row.Classname,
				*FString::Join(Chain, TEXT(" ")), *FString::Join(Expected, TEXT(" "))), bPrefix);

			// The NPC surface resolves through the chain on every classname: a `CAI_BaseNPCTroika`
			// input and field, the `TweakParam` input (`0x1029ea40`), and the `CBaseAnimating`
			// `SpawnTempParticle` seam.
			const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
			for (const TCHAR* Input : { TEXT("StartPlayerDialog"), TEXT("TweakParam"),
				TEXT("SpawnTempParticle") })
			{
				TestTrue(FString::Printf(TEXT("%s resolves input %s"), Row.Classname, Input),
					Reg.FindInput(*Entity->Class, FName(Input)) != nullptr);
			}
			TestNotNull(FString::Printf(TEXT("%s resolves field stattemplate"), Row.Classname),
				Reg.FindField(*Entity->Class, FName(TEXT("stattemplate"))));
			// `TransformModel` is a `CNPC_VVampireBoss` datamap INPUT: every class below it inherits
			// it (retail's datamap chain), no other class has it.
			const bool bBossLine = ElysiumNpcTestCensus::DerivesFrom(Cls, TEXT("CNPC_VVampireBoss"));
			TestEqual(FString::Printf(TEXT("%s has TransformModel only on the vampire-boss line"),
				Row.Classname), Reg.FindInput(*Entity->Class, FName(TEXT("TransformModel"))) != nullptr,
				bBossLine);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelClassPartitionTest,
	"Elysium.Substrate.NpcKernelClass.FactoryPartition", GElysiumNpcKernelFactoryFlags)
bool FElysiumNpcKernelClassPartitionTest::RunTest(const FString&)
{
	// The 74 replayed factories partition into the 45 step-2 classnames, the 3 of the controller
	// line (fold A2), the 3 directors (fold A3), the 3 makers (fold A4) and the 20 dead names, and
	// the census carries exactly those, each on its own class.
	const int32 Listed = UE_ARRAY_COUNT(GStep2Factories) + UE_ARRAY_COUNT(GControllerLineFactories)
		+ UE_ARRAY_COUNT(GDirectorFactories) + UE_ARRAY_COUNT(GMakerFactories)
		+ UE_ARRAY_COUNT(GDeadClassnames);
	TestEqual(TEXT("45 + 3 + 3 + 20 + 3 names"), Listed, 74);
	int32 CensusNames = 0;
	for (const FElysiumNpcClass& Row : ElysiumNpcKernelShape::Classes())
	{
		CensusNames += Row.ClassnameCount;
	}
	TestEqual(TEXT("the census carries exactly the factories' classnames"), CensusNames, Listed);
	auto Check = [this](const TCHAR* Classname, const TCHAR* RetailClass)
	{
		const FElysiumNpcClass* Cls = ElysiumNpcTestCensus::OfClassname(Classname);
		TestEqual(FString::Printf(TEXT("%s is %s in the census"), Classname, RetailClass),
			Cls != nullptr ? FString(Cls->Name) : FString(), FString(RetailClass));
	};
	for (const FFactoryRow& Row : GStep2Factories) { Check(Row.Classname, Row.RetailClass); }
	for (const FFactoryRow& Row : GControllerLineFactories) { Check(Row.Classname, Row.RetailClass); }
	for (const FFactoryRow& Row : GDirectorFactories) { Check(Row.Classname, Row.RetailClass); }
	for (const FFactoryRow& Row : GDeadClassnames) { Check(Row.Classname, Row.RetailClass); }
	for (const FFactoryRow& Row : GMakerFactories) { Check(Row.Classname, Row.RetailClass); }
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelClassAbstractTest,
	"Elysium.Substrate.NpcKernelClass.AbstractRefusal", GElysiumNpcKernelFactoryFlags)
bool FElysiumNpcKernelClassAbstractTest::RunTest(const FString&)
{
	const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
	// The retail classes no classname builds, plus every class a classname descriptor derives
	// from: a def naming one is refused, never stood as a record or an NPC.
	TArray<FString> Abstract = { TEXT("CAI_BaseNPC"), TEXT("CAI_BaseNPCTroika"), TEXT("CNPC_VBaseBoss"),
		TEXT("CAI_TestHull") };
	for (const FFactoryRow& Row : GStep2Factories)
	{
		Abstract.AddUnique(Row.RetailClass);
	}
	for (const FFactoryRow& Row : GControllerLineFactories)
	{
		Abstract.AddUnique(Row.RetailClass);
	}
	for (const FFactoryRow& Row : GMakerFactories)
	{
		Abstract.AddUnique(Row.RetailClass);
	}
	// One refusal per name below, the map row and the runtime creation in the world case.
	AddExpectedError(TEXT("refused -- an abstract retail class"), EAutomationExpectedErrorFlags::Contains,
		Abstract.Num() + 2);
	for (const FString& Name : Abstract)
	{
		const FElysiumClassDesc* Desc = Reg.Find(FName(*Name));
		if (!TestNotNull(FString::Printf(TEXT("%s has a descriptor"), *Name), Desc))
		{
			continue;
		}
		TestTrue(FString::Printf(TEXT("%s is abstract"), *Name), Desc->bAbstract);
		FElysiumEntityDef Def;
		Def.Classname = Name;
		TestFalse(FString::Printf(TEXT("%s is refused"), *Name),
			Reg.Create(Def, FElysiumEntityHandle(0, 1)).IsValid());
	}

	// Internal construction builds the class against the abstract descriptor: retail's own
	// construction by code, which is how the bare Troika line and `CAI_TestHull` (fold A1) stand.
	{
		FElysiumEntityDef Def;
		Def.Classname = TEXT("CAI_TestHull");
		Def.InternalFactory = []() -> TUniquePtr<FElysiumEntity> { return MakeUnique<FElysiumNpcTestHull>(); };
		TUniquePtr<FElysiumEntity> Entity = Reg.Create(Def, FElysiumEntityHandle(0, 1));
		FElysiumNpcBase* Hull = Entity.IsValid() ? Entity->AsNpcBase() : nullptr;
		if (TestNotNull(TEXT("an internally built test hull stands"), Hull))
		{
			TestNull(TEXT("on the CAI_BaseNPC line, not the Troika's"), Entity->AsNpc());
			TestNotNull(TEXT("as its own C++ class"), Hull->AsSpecies<FElysiumNpcTestHull>());
			const FElysiumNpcClass* Cls = Hull->RetailClass();
			TestEqual(TEXT("answering CAI_TestHull"), Cls != nullptr ? FString(Cls->Name) : FString(),
				FString(TEXT("CAI_TestHull")));
			TestTrue(TEXT("against the abstract CAI_TestHull descriptor"),
				Entity->Class != nullptr && Entity->Class->ClassName == FName(TEXT("CAI_TestHull")));
		}
	}
	{
		FElysiumEntityDef Def;
		Def.Classname = TEXT("CAI_BaseNPCTroika");
		Def.InternalFactory = []() -> TUniquePtr<FElysiumEntity> { return MakeUnique<FElysiumNpc>(); };
		TUniquePtr<FElysiumEntity> Entity = Reg.Create(Def, FElysiumEntityHandle(0, 1));
		const FElysiumNpc* Npc = Entity.IsValid() ? Entity->AsNpc() : nullptr;
		if (TestNotNull(TEXT("an internally built Troika NPC stands"), Npc))
		{
			TestTrue(TEXT("and answers the Troika line"), Npc->RetailClass() == nullptr);
		}
	}

	// A map row naming an abstract class leaves its slot empty; the next row still stands.
	{
		FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_abstract"), 7);
		Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
		Builder.AddEntity(TEXT("CNPC_VBaseBoss"), TEXT("boss"));
		Builder.AddNpc(TEXT("cop"), FVector(200.f, 0.f, 0.f), TEXT("npc_VCop"));
		FElysiumNpcWorldFixture F(MoveTemp(Builder));
		TestNull(TEXT("the abstract row stands nothing"), F.World.FindByName(TEXT("boss")));
		TestNotNull(TEXT("the row after it still stands"), F.Npc(TEXT("cop")));
		// The runtime creation path (script `CreateEntityNoSpawn`, a maker child) refuses the same.
		FElysiumEntityDef Runtime;
		Runtime.Classname = TEXT("CNPC_VVampireBoss");
		TestFalse(TEXT("a runtime creation of an abstract class answers an invalid handle"),
			F.World.CreateRuntimeEntityNoSpawn(MoveTemp(Runtime)).IsSet());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelClassDeadClassnamesTest,
	"Elysium.Substrate.NpcKernelClass.DeadClassnames", GElysiumNpcKernelFactoryFlags)
bool FElysiumNpcKernelClassDeadClassnamesTest::RunTest(const FString&)
{
	const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
	// A dead class's classname is not registered: a def naming one is an inert record.
	for (const FFactoryRow& Row : GDeadClassnames)
	{
		const TCHAR* Name = Row.Classname;
		TestNull(FString::Printf(TEXT("%s is unregistered"), Name), Reg.Find(FName(Name)));
		FElysiumEntityDef Def;
		Def.Classname = Name;
		TUniquePtr<FElysiumEntity> Entity = Reg.Create(Def, FElysiumEntityHandle(0, 1));
		TestTrue(FString::Printf(TEXT("%s stands as a record"), Name),
			Entity.IsValid() && Entity->IsRecordOnly() && Entity->AsNpc() == nullptr);
	}
	// Nothing is deferred (story 5 commit A): the three maker classnames, the last factory rows to
	// stand, build their own NPC classes under their retail class, which hangs from
	// `CAI_BaseNPCTroika`. A bare def still stands a live entity (its missing model removes it at
	// `Spawn`, not here) and it is an `FElysiumNpc`.
	for (const FFactoryRow& Row : GMakerFactories)
	{
		const FElysiumClassDesc* Desc = Reg.Find(FName(Row.Classname));
		if (!TestNotNull(FString::Printf(TEXT("%s is registered"), Row.Classname), Desc))
		{
			continue;
		}
		TestFalse(FString::Printf(TEXT("%s is not abstract"), Row.Classname), Desc->bAbstract);
		TestEqual(FString::Printf(TEXT("%s hangs from its retail class"), Row.Classname),
			Desc->BaseName, FName(Row.RetailClass));
		FElysiumEntityDef Def;
		Def.Classname = Row.Classname;
		TUniquePtr<FElysiumEntity> Entity = Reg.Create(Def, FElysiumEntityHandle(0, 1));
		FElysiumNpc* Npc = Entity.IsValid() ? Entity->AsNpc() : nullptr;
		if (TestNotNull(FString::Printf(TEXT("%s (%s) is an FElysiumNpc"), Row.Classname, Row.RetailClass), Npc))
		{
			const FElysiumNpcClass* Cls = Npc->RetailClass();
			TestEqual(FString::Printf(TEXT("%s answers %s"), Row.Classname, Row.RetailClass),
				Cls != nullptr ? FString(Cls->Name) : FString(), FString(Row.RetailClass));
			TestNotNull(FString::Printf(TEXT("%s is a CNPCMaker by type"), Row.Classname),
				Npc->AsSpecies<FElysiumNpcMaker>());
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelClassMakerFactoriesTest,
	"Elysium.Substrate.NpcKernelClass.MakerFactories", GElysiumNpcKernelFactoryFlags)
bool FElysiumNpcKernelClassMakerFactoriesTest::RunTest(const FString&)
{
	// The makers stand through the map-load path: a live NPC of its own class, not a record, whose
	// descriptor derives exactly as retail's class does, carrying the Troika surface it inherits (a
	// maker IS a `CAI_BaseNPCTroika`) and its own datamap inputs.
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_maker_factories"), 5151);
	Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(GMakerFactories); ++Index)
	{
		FElysiumEntityDef& Def = Builder.AddEntity(GMakerFactories[Index].Classname,
			GMakerFactories[Index].Classname, FVector(400.f * Index, 9000.f, 0.f));
		Def.Keys.Add(TEXT("model"), TEXT("models/maker.mdl"));
		Def.Keys.Add(TEXT("NPCType"), TEXT("npc_VHuman"));
		Def.Keys.Add(TEXT("Flag_StartDisabled"), TEXT("1"));
	}
	FElysiumNpcWorldFixture F(MoveTemp(Builder));
	const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
	for (const FFactoryRow& Row : GMakerFactories)
	{
		FElysiumEntity* Entity = F.World.FindByName(Row.Classname);
		if (!TestNotNull(FString::Printf(TEXT("%s stands"), Row.Classname), Entity))
		{
			continue;
		}
		FElysiumNpc* Npc = Entity->AsNpc();
		if (!TestNotNull(FString::Printf(TEXT("%s is an NPC"), Row.Classname), Npc))
		{
			continue;
		}
		TestNotNull(FString::Printf(TEXT("%s is an NPC base"), Row.Classname), Entity->AsNpcBase());
		TestNotNull(FString::Printf(TEXT("%s is a combat character"), Row.Classname), Entity->AsCombatCharacter());
		const FElysiumNpcClass* Cls = Npc->RetailClass();
		TestEqual(FString::Printf(TEXT("%s answers its factory's class"), Row.Classname),
			Cls != nullptr ? FString(Cls->Name) : FString(), FString(Row.RetailClass));
		if (!TestNotNull(FString::Printf(TEXT("%s has a descriptor"), Row.Classname), Entity->Class))
		{
			continue;
		}
		const TArray<FString> Chain = RegistryChain(*Entity->Class);
		const TArray<FString> Expected = ProjectedChain(Row.RetailClass);
		const bool bPrefix = Chain.Num() >= Expected.Num()
			&& TArray<FString>(Chain.GetData(), Expected.Num()) == Expected;
		TestTrue(FString::Printf(TEXT("%s's descriptor chain [%s] begins [%s]"), Row.Classname,
			*FString::Join(Chain, TEXT(" ")), *FString::Join(Expected, TEXT(" "))), bPrefix);
		// Its own inputs, on `CNPCMaker`'s descriptor, and the Troika's `DisableThink` inherited.
		for (const TCHAR* Input : { TEXT("Spawn"), TEXT("Enable"), TEXT("Disable"), TEXT("Toggle"),
			TEXT("DisableThink") })
		{
			TestTrue(FString::Printf(TEXT("%s resolves input %s"), Row.Classname, Input),
				Reg.FindInput(*Entity->Class, FName(Input)) != nullptr);
		}
		// Its own words, generated per class: the zombie's three only on the zombie.
		TestNotNull(FString::Printf(TEXT("%s resolves field NPCType"), Row.Classname),
			Reg.FindField(*Entity->Class, FName(TEXT("NPCType"))));
		TestNotNull(FString::Printf(TEXT("%s resolves the saved m_sRefMapDataBuffer"), Row.Classname),
			Reg.FindField(*Entity->Class, FName(TEXT("m_sRefMapDataBuffer"))));
		TestEqual(FString::Printf(TEXT("%s has remove_distance only on the zombie maker"), Row.Classname),
			Reg.FindField(*Entity->Class, FName(TEXT("remove_distance"))) != nullptr,
			FCString::Strcmp(Row.RetailClass, TEXT("CNPCMaker_Zombie")) == 0);
		// And the Troika's keyed words, which a maker authors on itself (`vision`, `pl_investigate`).
		TestNotNull(FString::Printf(TEXT("%s resolves the Troika's vision"), Row.Classname),
			Reg.FindField(*Entity->Class, FName(TEXT("vision"))));
	}
	TestNotNull(TEXT("npc_maker_fleshpile builds FElysiumNpcMakerFleshpile"),
		F.World.FindByName(TEXT("npc_maker_fleshpile")) != nullptr
			? F.World.FindByName(TEXT("npc_maker_fleshpile"))->AsNpc()->AsSpecies<FElysiumNpcMakerFleshpile>()
			: nullptr);
	TestNotNull(TEXT("npc_maker_zombie builds FElysiumNpcMakerZombie"),
		F.World.FindByName(TEXT("npc_maker_zombie")) != nullptr
			? F.World.FindByName(TEXT("npc_maker_zombie"))->AsNpc()->AsSpecies<FElysiumNpcMakerZombie>()
			: nullptr);
	return true;
}

// The factory map in its closing form (0019 story 5 commit B).
//
// The registry's NPC line is exactly the factory map: every constructible descriptor under
// `CAI_BaseNPC` is one of the 45 step-2 classnames, the controller line's 3 (fold A2), the
// directors' 3 (fold A3) or the makers' 3 (fold A4) hanging from its own retail class, every
// abstract one is a live census class, and every live census class stands on the line — no class
// is deferred (the test hull, fold A1, stands as an abstract live census class).
namespace
{
	// Whether a descriptor is `CAI_BaseNPC` or derives from it through the registry chain.
	bool OnNpcLine(const FElysiumClassDesc& Desc)
	{
		const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
		for (const FElysiumClassDesc* D = &Desc; D != nullptr;
			D = D->BaseName.IsNone() ? nullptr : Reg.Find(D->BaseName))
		{
			if (D->ClassName == FName(TEXT("CAI_BaseNPC")))
			{
				return true;
			}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelClassRegistryTest,
	"Elysium.Substrate.NpcKernelClass.RegistryMatchesFactories", GElysiumNpcKernelFactoryFlags)
bool FElysiumNpcKernelClassRegistryTest::RunTest(const FString&)
{
	const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
	TSet<FString> Constructible;
	TSet<FString> Abstract;
	Reg.ForEach([this, &Constructible, &Abstract](const FElysiumClassDesc& Desc)
	{
		if (!OnNpcLine(Desc))
		{
			return;
		}
		const FString Name = Desc.ClassName.ToString();
		if (Desc.bAbstract)
		{
			Abstract.Add(Name);
			const bool bLine = Name == TEXT("CAI_BaseNPC") || Name == TEXT("CAI_BaseNPCTroika");
			TestTrue(FString::Printf(TEXT("abstract %s is a live census class"), *Name),
				bLine || (ElysiumNpcTestCensus::Find(*Name) != nullptr
					&& !ElysiumNpcDeadClasses::Contains(*Name)));
			return;
		}
		Constructible.Add(Name);
		const FFactoryRow* Row = nullptr;
		for (const FFactoryRow& Candidate : GStep2Factories)
		{
			if (Name == Candidate.Classname)
			{
				Row = &Candidate;
			}
		}
		for (const FFactoryRow& Candidate : GControllerLineFactories)
		{
			if (Name == Candidate.Classname)
			{
				Row = &Candidate;
			}
		}
		for (const FFactoryRow& Candidate : GDirectorFactories)
		{
			if (Name == Candidate.Classname)
			{
				Row = &Candidate;
			}
		}
		for (const FFactoryRow& Candidate : GMakerFactories)
		{
			if (Name == Candidate.Classname)
			{
				Row = &Candidate;
			}
		}
		if (TestNotNull(FString::Printf(TEXT("constructible %s is a factory-map classname"), *Name), Row))
		{
			TestEqual(FString::Printf(TEXT("%s hangs from its retail class"), *Name),
				Desc.BaseName, FName(Row->RetailClass));
		}
	});
	TestEqual(TEXT("the registry's NPC line builds exactly the 45 step-2, 3 controller-line, 3 "
		"director and 3 maker classnames"), Constructible.Num(),
		static_cast<int32>(UE_ARRAY_COUNT(GStep2Factories) + UE_ARRAY_COUNT(GControllerLineFactories)
			+ UE_ARRAY_COUNT(GDirectorFactories) + UE_ARRAY_COUNT(GMakerFactories)));
	TArray<FFactoryRow> NpcFactories(GStep2Factories, UE_ARRAY_COUNT(GStep2Factories));
	NpcFactories.Append(GControllerLineFactories, UE_ARRAY_COUNT(GControllerLineFactories));
	NpcFactories.Append(GDirectorFactories, UE_ARRAY_COUNT(GDirectorFactories));
	NpcFactories.Append(GMakerFactories, UE_ARRAY_COUNT(GMakerFactories));
	for (const FFactoryRow& Row : NpcFactories)
	{
		TestTrue(FString::Printf(TEXT("%s is registered on the NPC line"), Row.Classname),
			Constructible.Contains(Row.Classname));
		TestTrue(FString::Printf(TEXT("its retail class %s has an abstract descriptor"), Row.RetailClass),
			Abstract.Contains(Row.RetailClass));
	}

	// No deferred entry: EVERY live census class (the 56 of plan Appendix A; the 21 dead ones keep
	// their census rows and no port class) has a descriptor on the NPC line.
	int32 Live = 0;
	for (const FElysiumNpcClass& Row : ElysiumNpcKernelShape::Classes())
	{
		if (ElysiumNpcDeadClasses::Contains(Row.Name))
		{
			continue;
		}
		++Live;
		const FElysiumClassDesc* Desc = Reg.Find(FName(Row.Name));
		TestTrue(FString::Printf(TEXT("live census class %s stands on the NPC line"), Row.Name),
			Desc != nullptr && OnNpcLine(*Desc));
	}
	TestEqual(TEXT("56 live census classes, none deferred"), Live, 56);
	return true;
}

#endif
