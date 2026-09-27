#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumClassRegistry.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcTestHull.h"
#include "Tests/ElysiumNpcTestFixture.h"

// Story 5 step 2: the classname -> class map is retail's factories
// (`docs/specs/0019-npc-kernel-rework/story-5/factories.tsv`, replayed from the 74 factories of
// `vampire.dll`; `docs/vtmb/npc-ai/population.md`, "The classname -> class map, read from the
// factories"). Every living ordinary-NPC classname builds its own C++ class, which answers its own
// census row; the retail classes above it are abstract descriptors a map cannot stand; the dead
// names stay unregistered; the deferred classes keep their pre-step-2 factories until their folds
// (commit A; the test hull, which has no classname, stands at fold A1).

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

	struct FDeferredRow
	{
		const TCHAR* Classname;
		const TCHAR* RetailClass;
		bool bRegistered;   // `factories.tsv` `current_registry` is not `unregistered-base-fallback`
		FName PriorBase;    // the base its pre-step-2 registration hangs from
	};

	// The deferred classes' classnames (steps 7-10); each keeps its pre-step-2 registration.
	const FDeferredRow GDeferredFactories[] =
	{
		{ TEXT("aiscripted_schedule"), TEXT("CCineAISchedule"), true, ElysiumBaseClassName() },
		{ TEXT("aiscripted_sequence"), TEXT("CCineAI"), true, ElysiumBaseClassName() },
		{ TEXT("npc_maker"), TEXT("CNPCMaker"), true, ElysiumBaseClassName() },
		{ TEXT("npc_maker_fleshpile"), TEXT("CNPCMaker_Fleshpile"), true, ElysiumBaseClassName() },
		{ TEXT("npc_maker_zombie"), TEXT("CNPCMaker_Zombie"), true, ElysiumBaseClassName() },
		{ TEXT("npc_VFrenzyShadow"), TEXT("CNPC_VFrenzyShadow"), false, NAME_None },
		{ TEXT("npc_VPlayerController"), TEXT("CNPC_VPlayerController"), true, ElysiumCombatCharacterClassName() },
		{ TEXT("npc_VWolfMorph"), TEXT("CNPC_VWolfMorph"), false, NAME_None },
		{ TEXT("scripted_sequence"), TEXT("CCineNPC"), true, ElysiumBaseClassName() },
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
		for (const FElysiumNpcClass* Row = ElysiumNpcKernelClass::Find(RetailClass); Row != nullptr;
			Row = ElysiumNpcKernelClass::Find(Row->Base))
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
	FElysiumNpcWorldFixture F(MoveTemp(Builder));
	for (const FFactoryRow& Row : GStep2Factories)
	{
		FElysiumEntity* Entity = F.World.FindByName(Row.Classname);
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
			ElysiumNpcKernelClass::OfClassname(Row.Classname) == Cls);
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
			const bool bBossLine = ElysiumNpcKernelClass::DerivesFrom(Cls, TEXT("CNPC_VVampireBoss"));
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
	// The 74 replayed factories partition into the 45 step-2 classnames, the 20 dead names and the
	// 9 names of the deferred folds, and the census carries exactly those, each on its own class.
	const int32 Listed = UE_ARRAY_COUNT(GStep2Factories) + UE_ARRAY_COUNT(GDeadClassnames)
		+ UE_ARRAY_COUNT(GDeferredFactories);
	TestEqual(TEXT("45 + 20 + 9 names"), Listed, 74);
	int32 CensusNames = 0;
	for (const FElysiumNpcClass& Row : ElysiumNpcKernelShape::Classes())
	{
		CensusNames += Row.ClassnameCount;
	}
	TestEqual(TEXT("the census carries exactly the factories' classnames"), CensusNames, Listed);
	auto Check = [this](const TCHAR* Classname, const TCHAR* RetailClass)
	{
		const FElysiumNpcClass* Cls = ElysiumNpcKernelClass::OfClassname(Classname);
		TestEqual(FString::Printf(TEXT("%s is %s in the census"), Classname, RetailClass),
			Cls != nullptr ? FString(Cls->Name) : FString(), FString(RetailClass));
	};
	for (const FFactoryRow& Row : GStep2Factories) { Check(Row.Classname, Row.RetailClass); }
	for (const FFactoryRow& Row : GDeadClassnames) { Check(Row.Classname, Row.RetailClass); }
	for (const FDeferredRow& Row : GDeferredFactories) { Check(Row.Classname, Row.RetailClass); }
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelClassDeadAndDeferredTest,
	"Elysium.Substrate.NpcKernelClass.DeadAndDeferred", GElysiumNpcKernelFactoryFlags)
bool FElysiumNpcKernelClassDeadAndDeferredTest::RunTest(const FString&)
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
	// The deferred classes keep the registration they had before step 2 until their fold: a live
	// entity that is not yet an `FElysiumNpc`, or (FrenzyShadow, WolfMorph) still no registration.
	for (const FDeferredRow& Row : GDeferredFactories)
	{
		const FElysiumClassDesc* Desc = Reg.Find(FName(Row.Classname));
		TestEqual(FString::Printf(TEXT("%s keeps its registration"), Row.Classname), Desc != nullptr,
			Row.bRegistered);
		TestFalse(FString::Printf(TEXT("%s is not abstract"), Row.Classname),
			Desc != nullptr && Desc->bAbstract);
		if (Desc != nullptr)
		{
			TestEqual(FString::Printf(TEXT("%s keeps its pre-step-2 base"), Row.Classname),
				Desc->BaseName, Row.PriorBase);
		}
		FElysiumEntityDef Def;
		Def.Classname = Row.Classname;
		TUniquePtr<FElysiumEntity> Entity = Reg.Create(Def, FElysiumEntityHandle(0, 1));
		TestEqual(FString::Printf(TEXT("%s builds a live entity only if registered"), Row.Classname),
			Entity.IsValid() && !Entity->IsRecordOnly(), Row.bRegistered);
		TestTrue(FString::Printf(TEXT("%s (%s) is not yet an FElysiumNpc"), Row.Classname,
			Row.RetailClass), Entity.IsValid() && Entity->AsNpc() == nullptr);
	}
	return true;
}

// The factory map at the step-6 boundary (0019 story 5 step 6, plan § 5 step 6: "extend the census
// tests for ... current factories; all ten deferred classes remain explicitly listed").
//
// The registry's NPC line is exactly the factory map: every constructible descriptor under
// `CAI_BaseNPC` is one of the 45 step-2 classnames hanging from its own retail class, every abstract
// one is a live census class, and none of the nine still-deferred classes has an NPC descriptor yet
// (the test hull, fold A1, stands as an abstract live census class).
namespace
{
	// `manifest.json` `deferred_classes` (git at `a00cd11b`), less the folds commit A has landed:
	// A1 the test hull; A2 the controller line, A3 the directors and A4 the makers remain.
	const TCHAR* const GDeferredClasses[] =
	{
		TEXT("CCineNPC"),
		TEXT("CCineAI"),
		TEXT("CCineAISchedule"),
		TEXT("CNPCMaker"),
		TEXT("CNPCMaker_Fleshpile"),
		TEXT("CNPCMaker_Zombie"),
		TEXT("CNPC_VPlayerController"),
		TEXT("CNPC_VFrenzyShadow"),
		TEXT("CNPC_VWolfMorph"),
	};

	bool IsDeferredClass(const FString& RetailClass)
	{
		for (const TCHAR* Name : GDeferredClasses)
		{
			if (RetailClass == Name)
			{
				return true;
			}
		}
		return false;
	}

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
				bLine || (ElysiumNpcKernelClass::Find(*Name) != nullptr && !IsDeferredClass(Name)));
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
		if (TestNotNull(FString::Printf(TEXT("constructible %s is a factory-map classname"), *Name), Row))
		{
			TestEqual(FString::Printf(TEXT("%s hangs from its retail class"), *Name),
				Desc.BaseName, FName(Row->RetailClass));
		}
	});
	TestEqual(TEXT("the registry's NPC line builds exactly the 45 step-2 classnames"), Constructible.Num(),
		static_cast<int32>(UE_ARRAY_COUNT(GStep2Factories)));
	for (const FFactoryRow& Row : GStep2Factories)
	{
		TestTrue(FString::Printf(TEXT("%s is registered on the NPC line"), Row.Classname),
			Constructible.Contains(Row.Classname));
		TestTrue(FString::Printf(TEXT("its retail class %s has an abstract descriptor"), Row.RetailClass),
			Abstract.Contains(Row.RetailClass));
	}

	// The nine deferred classes, explicitly: each is a census class, none is on the NPC line yet, and
	// their classnames are exactly the nine deferred factory rows.
	TestEqual(TEXT("nine classes are deferred"), static_cast<int32>(UE_ARRAY_COUNT(GDeferredClasses)), 9);
	int32 DeferredNames = 0;
	for (const TCHAR* Name : GDeferredClasses)
	{
		const FElysiumNpcClass* Cls = ElysiumNpcKernelClass::Find(Name);
		if (!TestNotNull(FString::Printf(TEXT("deferred %s is a census class"), Name), Cls))
		{
			continue;
		}
		DeferredNames += Cls->ClassnameCount;
		const FElysiumClassDesc* Desc = Reg.Find(FName(Name));
		TestFalse(FString::Printf(TEXT("deferred %s has no NPC descriptor before its fold"), Name),
			Desc != nullptr && OnNpcLine(*Desc));
	}
	TestEqual(TEXT("the deferred classes carry the nine deferred classnames"), DeferredNames,
		static_cast<int32>(UE_ARRAY_COUNT(GDeferredFactories)));
	for (const FDeferredRow& Row : GDeferredFactories)
	{
		TestTrue(FString::Printf(TEXT("%s belongs to a deferred class"), Row.Classname),
			IsDeferredClass(Row.RetailClass));
		const FElysiumClassDesc* Desc = Reg.Find(FName(Row.Classname));
		TestFalse(FString::Printf(TEXT("%s is not on the NPC line"), Row.Classname),
			Desc != nullptr && OnNpcLine(*Desc));
	}
	return true;
}

#endif
