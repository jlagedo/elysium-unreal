#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumClassRegistry.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumSaveTypes.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcAsianVampire.h"
#include "Substrate/ElysiumNpcBach.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcCop.h"
#include "Substrate/ElysiumNpcGargoyle.h"
#include "Substrate/ElysiumNpcHengeyokai.h"
#include "Substrate/ElysiumNpcHunter.h"
#include "Substrate/ElysiumNpcKernelBindings.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcManBat.h"
#include "Substrate/ElysiumNpcMingXiao.h"
#include "Substrate/ElysiumNpcMingXiaoTentacle.h"
#include "Substrate/ElysiumNpcSabbatLeader.h"
#include "Substrate/ElysiumNpcScurrying.h"
#include "Substrate/ElysiumNpcTzimisce.h"
#include "Tests/ElysiumNpcTestFixture.h"

// Story 5 step 4 — the species datamap rows, asserted on the classes that declare them.
//
// `NpcKernelBindings` holds the Troika table to its generated counts. This suite holds the species
// tables the same way and then asks what only a class tree can answer: a row declared on
// `CNPC_VAnimal` resolves on a rat and a dog through the descriptor chain; a row declared on one
// species is absent on its siblings; a species KEY row carries no `Key` flag while a species input is
// an input and not a field; a shadow row writes the inherited storage; every merged carrier is the
// one member its retail word binds; and every introduced class's own words survive a save. Last,
// the tutorial's rats: their eight authored keys land through `Construct` and drive `0x103acac0`
// and `0x103acba0` as retail runs them.

static constexpr EAutomationTestFlags GElysiumNpcSpeciesBindingsFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	const FElysiumFieldAccessor* SpeciesBindingsRow(const FElysiumEntity* Entity, const TCHAR* Row)
	{
		return Entity != nullptr && Entity->Class != nullptr
			? FElysiumClassRegistry::Get().FindField(*Entity->Class, FName(Row))
			: nullptr;
	}

	const FElysiumClassDesc* SpeciesBindingsDesc(const TCHAR* Classname)
	{
		return FElysiumClassRegistry::Get().Find(FName(Classname));
	}
}

// -------------------------------------------------------------------------------------------------
// Counts — each species table carries exactly the rows the generator counted.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesBindingsCountsTest,
	"Elysium.Substrate.NpcKernelSpeciesBindings.Counts", GElysiumNpcSpeciesBindingsFlags)
bool FElysiumNpcKernelSpeciesBindingsCountsTest::RunTest(const FString&)
{
	using EClass = ElysiumNpcKernelBindings::EClass;
	struct FSpecies
	{
		const TCHAR* RetailClass;
		EClass Class;
	};
	static const FSpecies Species[] = {
		{ TEXT("CNPC_VAndreiBlood"), EClass::AndreiBlood },
		{ TEXT("CNPC_VAnimal"), EClass::Animal },
		{ TEXT("CNPC_VAsianVampire"), EClass::AsianVampire },
		{ TEXT("CNPC_VBach"), EClass::Bach },
		{ TEXT("CNPC_VCameraSecurity"), EClass::CameraSecurity },
		{ TEXT("CNPC_VChangBros"), EClass::ChangBros },
		{ TEXT("CNPC_VCop"), EClass::Cop },
		{ TEXT("CNPC_VGargoyle"), EClass::Gargoyle },
		{ TEXT("CNPC_VGhoulCroucher"), EClass::GhoulCroucher },
		{ TEXT("CNPC_VGuard1"), EClass::Guard1 },
		{ TEXT("CNPC_VHengeyokai"), EClass::Hengeyokai },
		{ TEXT("CNPC_VHunter"), EClass::Hunter },
		{ TEXT("CNPC_VLasombra"), EClass::Lasombra },
		{ TEXT("CNPC_VManBat"), EClass::ManBat },
		{ TEXT("CNPC_VMingXiao"), EClass::MingXiao },
		{ TEXT("CNPC_VMingXiaoTentacle"), EClass::MingXiaoTentacle },
		{ TEXT("CNPC_VPedestrian"), EClass::Pedestrian },
		{ TEXT("CNPC_VSabbatLeader"), EClass::SabbatLeader },
		{ TEXT("CNPC_VScurrying"), EClass::Scurrying },
		{ TEXT("CNPC_VSheriffMan"), EClass::SheriffMan },
		{ TEXT("CNPC_VTaxiDriver"), EClass::TaxiDriver },
		{ TEXT("CNPC_VTzimisce"), EClass::Tzimisce },
		{ TEXT("CNPC_VTzimisceHeadClaw"), EClass::TzimisceHeadClaw },
		{ TEXT("CNPC_VTzimisceRunner"), EClass::TzimisceRunner },
		{ TEXT("CNPC_VVampireBoss"), EClass::VampireBoss },
		{ TEXT("CNPC_VWerewolf"), EClass::Werewolf },
		{ TEXT("CNPC_VZombie"), EClass::Zombie },
	};
	TestEqual(TEXT("the 27 species binding classes"), static_cast<int32>(UE_ARRAY_COUNT(Species)), 27);
	for (const FSpecies& Row : Species)
	{
		FElysiumClassDesc D;
		TestTrue(FString::Printf(TEXT("%s has a species table"), Row.RetailClass),
			ElysiumNpcKernelBindings::AddSpeciesFields(D, Row.RetailClass));
		const ElysiumNpcKernelBindings::FCounts Counts = ElysiumNpcKernelBindings::Counts(Row.Class);
		TestEqual(FString::Printf(TEXT("%s carries exactly its generated keyed and saved rows"),
			Row.RetailClass), D.Fields.Num(), Counts.Bound + Counts.Saved);
		for (const TPair<FName, FElysiumFieldAccessor>& Pair : D.Fields)
		{
			// A species row is persistence: KEY alone adds no flag (the registry applies spawn
			// keyvalues regardless), and no species datamap carries an INPUT field.
			TestTrue(FString::Printf(TEXT("%s: %s is saved"), Row.RetailClass, *Pair.Key.ToString()),
				Pair.Value.bSave);
			TestFalse(FString::Printf(TEXT("%s: %s is not an input field"), Row.RetailClass,
				*Pair.Key.ToString()), Pair.Value.bKeyable);
		}
	}
	FElysiumClassDesc None;
	TestFalse(TEXT("a class with no species datamap has no table"),
		ElysiumNpcKernelBindings::AddSpeciesFields(None, TEXT("CNPC_VHumanCombatant")));
	return true;
}

// -------------------------------------------------------------------------------------------------
// The descriptor chain — inheritance, sibling rejection, input vs key, shadow rows.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesBindingsChainTest,
	"Elysium.Substrate.NpcKernelSpeciesBindings.Chain", GElysiumNpcSpeciesBindingsFlags)
bool FElysiumNpcKernelSpeciesBindingsChainTest::RunTest(const FString&)
{
	const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();

	// `CNPC_VAnimal`'s `warn_range` (`+0x6668`) is declared once, on the abstract `CNPC_VAnimal`
	// descriptor; a rat (`CNPC_VRat` < `CNPC_VScurrying` < `CNPC_VAnimal`) and a dog reach it
	// through the chain.
	for (const TCHAR* Classname : { TEXT("npc_VRat"), TEXT("npc_VDog") })
	{
		const FElysiumClassDesc* Desc = SpeciesBindingsDesc(Classname);
		if (!TestNotNull(FString::Printf(TEXT("%s is registered"), Classname), Desc))
		{
			continue;
		}
		const FElysiumFieldAccessor* Warn = Reg.FindField(*Desc, FName(TEXT("warn_range")));
		TestNotNull(FString::Printf(TEXT("%s inherits CNPC_VAnimal's warn_range"), Classname), Warn);
		TestNull(FString::Printf(TEXT("and %s's own descriptor does not declare it"), Classname),
			Desc->Fields.Find(FName(TEXT("warn_range"))));
		if (Warn != nullptr)
		{
			TestFalse(TEXT("a species KEY row carries no Key flag"), Warn->bKeyable);
			TestTrue(TEXT("and is saved"), Warn->bSave);
		}
	}
	// Scurrying's own words reach the rat and nowhere else.
	TestNotNull(TEXT("a rat reaches CNPC_VScurrying's detection_distance"),
		Reg.FindField(*SpeciesBindingsDesc(TEXT("npc_VRat")), FName(TEXT("detection_distance"))));
	for (const TCHAR* Sibling : { TEXT("npc_VDog"), TEXT("npc_VBach"), TEXT("npc_VZombie") })
	{
		const FElysiumClassDesc* Desc = SpeciesBindingsDesc(Sibling);
		if (TestNotNull(FString::Printf(TEXT("%s is registered"), Sibling), Desc))
		{
			TestNull(FString::Printf(TEXT("%s has no detection_distance: a sibling's row is not "
				"copied"), Sibling), Reg.FindField(*Desc, FName(TEXT("detection_distance"))));
		}
	}

	// Inputs: `CNPC_VHengeyokai`'s `StartTransformation` is an INPUT, reached as one and never as a
	// field.
	const FElysiumClassDesc* Heng = SpeciesBindingsDesc(TEXT("npc_VHengeyokai"));
	if (TestNotNull(TEXT("npc_VHengeyokai is registered"), Heng))
	{
		TestTrue(TEXT("StartTransformation is an input on a Hengeyokai"),
			Reg.FindInput(*Heng, FName(TEXT("StartTransformation"))) != nullptr);
		TestNull(TEXT("and not a field"), Reg.FindField(*Heng, FName(TEXT("StartTransformation"))));
		TestTrue(TEXT("and a sibling does not answer it"),
			Reg.FindInput(*SpeciesBindingsDesc(TEXT("npc_VManBat")), FName(TEXT("StartTransformation")))
				== nullptr);
	}

	// Siblings at one offset: `+0x6664 m_hPursuitPlayer` is declared by both `CNPC_VCop` and
	// `CNPC_VHunter`, each on its own word. And the shadow: `CNPC_VHengeyokai` re-declares the Troika
	// word `+0x6458 m_flIgnoreCollisionTimer`, bound on the inherited storage.
	FElysiumNpcWorldBuilder Builder(TEXT("species_bindings_chain"), 4004);
	Builder.AddNpcOfClass(TEXT("cop"), FVector(0.0, 0.0, 0.0), TEXT("CNPC_VCop"));
	Builder.AddNpcOfClass(TEXT("hunter"), FVector(400.0, 0.0, 0.0), TEXT("CNPC_VHunter"));
	Builder.AddNpcOfClass(TEXT("heng"), FVector(800.0, 0.0, 0.0), TEXT("CNPC_VHengeyokai"));
	Builder.AddNpc(TEXT("other"), FVector(0.0, 400.0, 0.0));
	FElysiumNpcWorldFixture F(MoveTemp(Builder));
	FElysiumNpcCop* Cop = F.NpcAs<FElysiumNpcCop>(TEXT("cop"));
	FElysiumNpcHunter* Hunter = F.NpcAs<FElysiumNpcHunter>(TEXT("hunter"));
	FElysiumNpcHengeyokai* Hengeyokai = F.NpcAs<FElysiumNpcHengeyokai>(TEXT("heng"));
	FElysiumNpc* Other = F.Npc(TEXT("other"));
	if (!TestNotNull(TEXT("cop"), Cop) || !TestNotNull(TEXT("hunter"), Hunter)
		|| !TestNotNull(TEXT("hengeyokai"), Hengeyokai) || !TestNotNull(TEXT("other"), Other))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Cop, Hunter, Hengeyokai, Other });

	const FElysiumFieldAccessor* CopRow = SpeciesBindingsRow(Cop, TEXT("m_hPursuitPlayer"));
	const FElysiumFieldAccessor* HunterRow = SpeciesBindingsRow(Hunter, TEXT("m_hPursuitPlayer"));
	if (TestNotNull(TEXT("the cop's +0x6664"), CopRow) && TestNotNull(TEXT("the hunter's +0x6664"), HunterRow))
	{
		Cop->CopPursuitHandle = FElysiumEntityHandle::Invalid();
		Hunter->HunterPursuitPlayer = FElysiumEntityHandle::Invalid();
		CopRow->Set(*Cop, FElysiumVariant::Handle(Other->Handle));
		TestTrue(TEXT("the cop's row writes CopPursuitHandle"), Cop->CopPursuitHandle == Other->Handle);
		HunterRow->Set(*Hunter, FElysiumVariant::Handle(Cop->Handle));
		TestTrue(TEXT("the hunter's row writes HunterPursuitPlayer"),
			Hunter->HunterPursuitPlayer == Cop->Handle);
		TestTrue(TEXT("and neither reached the other's word"), Cop->CopPursuitHandle == Other->Handle);
		TestNull(TEXT("a Hengeyokai has no m_hPursuitPlayer"),
			SpeciesBindingsRow(Hengeyokai, TEXT("m_hPursuitPlayer")));
	}
	const FElysiumFieldAccessor* Shadow = SpeciesBindingsRow(Hengeyokai, TEXT("m_flIgnoreCollisionTimer"));
	if (TestNotNull(TEXT("the Hengeyokai's shadow row"), Shadow))
	{
		Shadow->Set(*Hengeyokai, FElysiumVariant::Float(1234.f));
		TestEqual(TEXT("the shadow row writes the inherited Troika storage"),
			static_cast<float>(Hengeyokai->IgnoreCollisionUntil), 1234.f);
	}
	return true;
}

// -------------------------------------------------------------------------------------------------
// Merged and split carriers — each retail word binds exactly one member.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesBindingsCarriersTest,
	"Elysium.Substrate.NpcKernelSpeciesBindings.MergedAndSplitCarriers", GElysiumNpcSpeciesBindingsFlags)
bool FElysiumNpcKernelSpeciesBindingsCarriersTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("species_bindings_carriers"), 4005);
	Builder.AddNpcOfClass(TEXT("asian"), FVector(0.0, 0.0, 0.0), TEXT("CNPC_VAsianVampire"));
	Builder.AddNpcOfClass(TEXT("bach"), FVector(400.0, 0.0, 0.0), TEXT("CNPC_VBach"));
	Builder.AddNpcOfClass(TEXT("manbat"), FVector(800.0, 0.0, 0.0), TEXT("CNPC_VManBat"));
	Builder.AddNpcOfClass(TEXT("xiao"), FVector(1200.0, 0.0, 0.0), TEXT("CNPC_VMingXiao"));
	Builder.AddNpcOfClass(TEXT("tentacle"), FVector(1600.0, 0.0, 0.0), TEXT("CNPC_VMingXiaoTentacle"));
	Builder.AddNpcOfClass(TEXT("sabbat"), FVector(2000.0, 0.0, 0.0), TEXT("CNPC_VSabbatLeader"));
	Builder.AddNpcOfClass(TEXT("heng"), FVector(2400.0, 0.0, 0.0), TEXT("CNPC_VHengeyokai"));
	Builder.AddNpcOfClass(TEXT("tzimisce"), FVector(2800.0, 0.0, 0.0), TEXT("CNPC_VTzimisce"));
	Builder.AddNpcOfClass(TEXT("gargoyle"), FVector(3200.0, 0.0, 0.0), TEXT("CNPC_VGargoyle"));
	Builder.AddNpc(TEXT("other"), FVector(0.0, 400.0, 0.0));
	FElysiumNpcWorldFixture F(MoveTemp(Builder));
	FElysiumNpcAsianVampire* Asian = F.NpcAs<FElysiumNpcAsianVampire>(TEXT("asian"));
	FElysiumNpcBach* Bach = F.NpcAs<FElysiumNpcBach>(TEXT("bach"));
	FElysiumNpcManBat* ManBat = F.NpcAs<FElysiumNpcManBat>(TEXT("manbat"));
	FElysiumNpcMingXiao* Xiao = F.NpcAs<FElysiumNpcMingXiao>(TEXT("xiao"));
	FElysiumNpcMingXiaoTentacle* Tentacle = F.NpcAs<FElysiumNpcMingXiaoTentacle>(TEXT("tentacle"));
	FElysiumNpcSabbatLeader* Sabbat = F.NpcAs<FElysiumNpcSabbatLeader>(TEXT("sabbat"));
	FElysiumNpcHengeyokai* Heng = F.NpcAs<FElysiumNpcHengeyokai>(TEXT("heng"));
	FElysiumNpcTzimisce* Tzimisce = F.NpcAs<FElysiumNpcTzimisce>(TEXT("tzimisce"));
	FElysiumNpcGargoyle* Gargoyle = F.NpcAs<FElysiumNpcGargoyle>(TEXT("gargoyle"));
	FElysiumNpc* Other = F.Npc(TEXT("other"));
	if (Asian == nullptr || Bach == nullptr || ManBat == nullptr || Xiao == nullptr
		|| Tentacle == nullptr || Sabbat == nullptr || Heng == nullptr || Tzimisce == nullptr
		|| Gargoyle == nullptr || Other == nullptr)
	{
		AddError(TEXT("the species world did not stand"));
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Asian, Bach, ManBat, Xiao, Tentacle, Sabbat, Heng, Tzimisce,
		Gargoyle, Other });

	// Write the retail word through its binding, then read the survivor member directly: a merged
	// loser that had come back, or a binding pointed elsewhere, reads the stale value.
	auto Write = [this](FElysiumNpc* Npc, const TCHAR* Row, const FElysiumVariant& Value) -> bool
	{
		const FElysiumFieldAccessor* Acc = SpeciesBindingsRow(Npc, Row);
		if (!TestNotNull(FString::Printf(TEXT("%s is bound"), Row), Acc))
		{
			return false;
		}
		Acc->Set(*Npc, Value);
		return true;
	};

	// The eleven merges (4c): each retail word had two or three port members; the survivor is the
	// one the datamap binds.
	if (Write(Asian, TEXT("m_bPathBlocked"), FElysiumVariant::Bool(true)))
	{
		TestTrue(TEXT("AsianVampire +0x66d4: bSpeciesPathBlocked merged into bAsianVampirePathBlocked"),
			Asian->bAsianVampirePathBlocked);
	}
	if (Write(Asian, TEXT("m_bSuppressRanged"), FElysiumVariant::Bool(true)))
	{
		TestTrue(TEXT("AsianVampire +0x66e8: bAsianVampireSuppressRanged merged into bSuppressRanged"),
			Asian->bSuppressRanged);
	}
	if (Write(Bach, TEXT("m_flNextHolyLightTime"), FElysiumVariant::Float(321.f)))
	{
		TestEqual(TEXT("Bach +0x6690: BachFailStamp and BachRepositionTimer merged into "
			"BachNextHolyLightTime"), static_cast<float>(Bach->BachNextHolyLightTime), 321.f);
	}
	if (Write(ManBat, TEXT("m_iMoveGoalNodeID"), FElysiumVariant::Int(77)))
	{
		TestEqual(TEXT("ManBat +0x6674: MoveGoalNodeId merged into ManBatMoveGoalNodeId"),
			ManBat->ManBatMoveGoalNodeId, 77);
	}
	if (Write(Xiao, TEXT("m_rflAttackTimers[4]"), FElysiumVariant::Float(44.f))
		&& Write(Xiao, TEXT("m_rflAttackTimers[5]"), FElysiumVariant::Float(55.f)))
	{
		TestEqual(TEXT("MingXiao +0x66c4[4]: MingXiaoPickupCooldownA merged into MingXiaoAttackTimers[4]"),
			static_cast<float>(Xiao->MingXiaoAttackTimers[4]), 44.f);
		TestEqual(TEXT("MingXiao +0x66c4[5]: MingXiaoPickupCooldownB merged into MingXiaoAttackTimers[5]"),
			static_cast<float>(Xiao->MingXiaoAttackTimers[5]), 55.f);
	}
	if (Write(Xiao, TEXT("m_iSeveredTentacleMask"), FElysiumVariant::Int(0x15)))
	{
		TestEqual(TEXT("MingXiao +0x6710: SeveredTentacleMask merged into MingXiaoSeveredTentacleMask"),
			Xiao->MingXiaoSeveredTentacleMask, 0x15u);
	}
	if (Write(Xiao, TEXT("m_hThrowObject"), FElysiumVariant::Handle(Other->Handle)))
	{
		TestTrue(TEXT("MingXiao +0x6718: SpeciesThrowObject merged into MingXiaoThrowObject"),
			Xiao->MingXiaoThrowObject == Other->Handle);
	}
	if (Write(Sabbat, TEXT("m_fLastSplashTime"), FElysiumVariant::Float(12.f)))
	{
		TestEqual(TEXT("SabbatLeader +0x66c8: SabbatLeaderLastSplashTime merged into SabbatLastSplashTime"),
			static_cast<float>(Sabbat->SabbatLastSplashTime), 12.f);
	}
	if (Write(Sabbat, TEXT("m_bDiving"), FElysiumVariant::Bool(true)))
	{
		TestTrue(TEXT("SabbatLeader +0x66d5: bSabbatLeaderDiving merged into bSabbatDiving"),
			Sabbat->bSabbatDiving);
	}

	// The splits (4g): one port member stood for several classes' words; each class now has its own.
	if (Write(Heng, TEXT("m_hPickupTarget"), FElysiumVariant::Handle(Other->Handle))
		&& Write(Tzimisce, TEXT("m_hPickupTarget"), FElysiumVariant::Handle(Heng->Handle)))
	{
		TestTrue(TEXT("Hengeyokai +0x6664 m_hPickupTarget is HengeyokaiPickupTarget"),
			Heng->HengeyokaiPickupTarget == Other->Handle);
		TestTrue(TEXT("Tzimisce +0x6670 m_hPickupTarget is its own PickupTarget"),
			Tzimisce->PickupTarget == Heng->Handle);
	}
	Gargoyle->GargoyleShunnedFindPillar = 3;
	Heng->HengeyokaiShunnedFindFish = 5;
	Tzimisce->TzimisceShunnedFindBody = 7;
	TestEqual(TEXT("the Gargoyle's shunned-pillar count is its own word"),
		Gargoyle->GargoyleShunnedFindPillar, 3);
	TestEqual(TEXT("the Hengeyokai's shunned-fish count is its own word"),
		Heng->HengeyokaiShunnedFindFish, 5);
	TestEqual(TEXT("the Tzimisce's shunned-body count is its own word"),
		Tzimisce->TzimisceShunnedFindBody, 7);
	if (Write(Tentacle, TEXT("m_iTentacleID"), FElysiumVariant::Int(4))
		&& Write(Xiao, TEXT("m_iTentacleID"), FElysiumVariant::Int(-1)))
	{
		TestEqual(TEXT("the tentacle's +0x6660 m_iTentacleID is its own TentacleId"), Tentacle->TentacleId, 4);
		TestEqual(TEXT("and the head's +0x6674 is MingXiaoTentacleId"), Xiao->MingXiaoTentacleId, -1);
	}
	return true;
}

// -------------------------------------------------------------------------------------------------
// Save round trip — every introduced class's own words, through Freeze and ApplySnapshot.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesBindingsSaveRoundTripTest,
	"Elysium.Substrate.NpcKernelSpeciesBindings.SaveRoundTrip", GElysiumNpcSpeciesBindingsFlags)
bool FElysiumNpcKernelSpeciesBindingsSaveRoundTripTest::RunTest(const FString&)
{
	const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();

	// What retail's own bodies rewrite on the way back, each with the body that does it. The restore
	// restarts the program (`NpcKernelBindings.SaveRoundTrip`'s restart divergence), and a restart
	// runs slot 435; a Pedestrian's slot 130 re-inits it on every load.
	struct FDerived { const TCHAR* Classname; const TCHAR* Row; const TCHAR* Why; };
	static const FDerived Derived[] =
	{
		{ TEXT("npc_VGargoyle"), TEXT("m_iShunnedFindPillar"),
		  TEXT("decremented by the Gargoyle's slot 435 `0x10378fe4` on the restart") },
		{ TEXT("npc_VHengeyokai"), TEXT("m_iShunnedFindFish"),
		  TEXT("decremented by the Hengeyokai's slot 435 `0x103830be` on the restart") },
		{ TEXT("npc_VTzimisce"), TEXT("m_iShunnedFindBody"),
		  TEXT("decremented by the Tzimisce's slot 435 `0x103bf63e` on the restart") },
		{ TEXT("npc_VTzimisce"), TEXT("m_ePathMode"),
		  TEXT("cleared by the Tzimisce's slot 435 `0x103bf630` on the restart") },
		{ TEXT("npc_VDialogPedestrian"), TEXT("m_bFirstThink"),
		  TEXT("`CNPC_VPedestrian::OnRestore` `0x103a25a0` re-runs `NPCInit` `0x103a2570` on a load") },
	};
	auto IsDerived = [](const TCHAR* Classname, FName Row)
	{
		for (const FDerived& D : Derived)
		{
			if (FCString::Strcmp(D.Classname, Classname) == 0 && Row == FName(D.Row))
			{
				return true;
			}
		}
		return false;
	};

	// One NPC per species class a classname builds, plus a live witness every saved handle can
	// name. The controller line folds in step 7 and has no species table.
	TArray<const TCHAR*> Classes;
	for (const FElysiumNpcClass& Row : ElysiumNpcKernelShape::Classes())
	{
		if (Row.ClassnameCount > 0 && FString(Row.Name).StartsWith(TEXT("CNPC_V"))
			&& FCString::Strcmp(Row.Name, TEXT("CNPC_VPlayerController")) != 0
			&& Reg.Find(FName(Row.Classnames[0])) != nullptr)
		{
			Classes.Add(Row.Classnames[0]);
		}
	}
	auto Build = [&Classes]()
	{
		FElysiumNpcWorldBuilder B(TEXT("species_bindings_roundtrip"), 4006);
		for (int32 Index = 0; Index < Classes.Num(); ++Index)
		{
			B.AddNpc(*FString::Printf(TEXT("s%d"), Index), FVector(400.0 * (Index + 1), 0.0, 0.0),
				Classes[Index]);
		}
		B.AddNpc(TEXT("other"), FVector(0.0, 400.0, 0.0));
		B.AddTroikaNpc(TEXT("troika"), FVector(0.0, 800.0, 0.0));
		return B;
	};
	FElysiumNpcWorldFixture F(Build());
	FElysiumNpc* Other = F.Npc(TEXT("other"));
	FElysiumNpc* Troika = F.Npc(TEXT("troika"));
	if (!TestNotNull(TEXT("the witness stands"), Other) || !TestNotNull(TEXT("the Troika NPC stands"), Troika)
		|| !TestNotNull(TEXT("the Troika NPC has a descriptor"), Troika->Class))
	{
		return false;
	}
	// The Troika line's own persistence is `NpcKernelBindings.SaveRoundTrip`'s; this case stamps only
	// the rows a species descriptor adds.
	const TSet<FName> Base(Reg.SaveFields(*Troika->Class));

	struct FWritten
	{
		int32 Npc;
		FName Row;
		FElysiumVariant Value;
	};
	TArray<FWritten> Written;
	TSet<FString> StampedClasses;
	int32 Salt = 0;
	for (int32 Index = 0; Index < Classes.Num(); ++Index)
	{
		FElysiumNpc* Npc = F.Npc(*FString::Printf(TEXT("s%d"), Index));
		if (!TestNotNull(FString::Printf(TEXT("%s stands"), Classes[Index]), Npc) || Npc->Class == nullptr)
		{
			continue;
		}
		for (const FName& Row : Reg.SaveFields(*Npc->Class))
		{
			const FElysiumFieldAccessor* Acc = Reg.FindField(*Npc->Class, Row);
			if (Base.Contains(Row) || Acc == nullptr || !Acc->Set || !Acc->Get)
			{
				continue;
			}
			++Salt;
			FElysiumVariant Value;
			switch (Acc->Type)
			{
			case EElysiumVariantType::Bool:   Value = FElysiumVariant::Bool(true); break;
			case EElysiumVariantType::Int:    Value = FElysiumVariant::Int(100 + (Salt % 100)); break;
			case EElysiumVariantType::Float:  Value = FElysiumVariant::Float(2000.f + Salt); break;
			case EElysiumVariantType::String:
				Value = FElysiumVariant::String(FString::Printf(TEXT("species_%d"), Salt)); break;
			case EElysiumVariantType::Vector:
				Value = FElysiumVariant::Vector(FVector(Salt, Salt + 1, Salt + 2)); break;
			case EElysiumVariantType::Handle: Value = FElysiumVariant::Handle(Other->Handle); break;
			default: continue;
			}
			Acc->Set(*Npc, Value);
			Written.Add({ Index, Row, Value });
			StampedClasses.Add(Classes[Index]);
		}
	}
	// Every one of the 27 species tables reaches at least one standing class.
	if (!TestTrue(TEXT("species rows were stamped on many classes"), StampedClasses.Num() >= 20))
	{
		return false;
	}

	FElysiumMapSnapshot Snapshot;
	F.World.Freeze(Snapshot);
	FElysiumNpcWorldFixture G(Build());
	TestTrue(TEXT("the snapshot applies"), G.World.ApplySnapshot(Snapshot) > 0);
	FElysiumNpc* RestoredOther = G.Npc(TEXT("other"));
	if (!TestNotNull(TEXT("the witness restores"), RestoredOther))
	{
		return false;
	}

	int32 Survived = 0;
	for (const FWritten& W : Written)
	{
		FElysiumNpc* Npc = G.Npc(*FString::Printf(TEXT("s%d"), W.Npc));
		const FElysiumFieldAccessor* Acc = Npc != nullptr && Npc->Class != nullptr
			? Reg.FindField(*Npc->Class, W.Row) : nullptr;
		if (Acc == nullptr)
		{
			AddError(FString::Printf(TEXT("%s: %s no longer resolves"), Classes[W.Npc], *W.Row.ToString()));
			continue;
		}
		const FElysiumVariant Back = Acc->Get(*Npc);
		bool bSame = false;
		switch (W.Value.Type)
		{
		case EElysiumVariantType::Bool:   bSame = Back.AsBool == W.Value.AsBool; break;
		case EElysiumVariantType::Int:    bSame = Back.AsInt == W.Value.AsInt; break;
		case EElysiumVariantType::Float:  bSame = FMath::IsNearlyEqual(Back.AsFloat, W.Value.AsFloat, 0.01f); break;
		case EElysiumVariantType::String: bSame = Back.AsString == W.Value.AsString; break;
		case EElysiumVariantType::Vector: bSame = Back.AsVector.Equals(W.Value.AsVector, 0.01); break;
		case EElysiumVariantType::Handle: bSame = Back.AsHandle.Index == RestoredOther->Handle.Index; break;
		default: break;
		}
		if (IsDerived(Classes[W.Npc], W.Row))
		{
			++Survived;
		}
		else if (bSame)
		{
			++Survived;
		}
		else
		{
			AddError(FString::Printf(TEXT("%s: the species save row '%s' did not survive the record"),
				Classes[W.Npc], *W.Row.ToString()));
		}
	}
	TestEqual(TEXT("every species save row survived the record or is rewritten by a named retail body"),
		Survived, Written.Num());
	return true;
}

// -------------------------------------------------------------------------------------------------
// The tutorial's rats — eight authored keys, `0x103acac0` and `0x103acba0`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesBindingsTutorialRatTest,
	"Elysium.Substrate.NpcKernelSpeciesBindings.TutorialRat", GElysiumNpcSpeciesBindingsFlags)
bool FElysiumNpcKernelSpeciesBindingsTutorialRatTest::RunTest(const FString&)
{
	// `sp_tutorial_1`'s three `npc_VRat` entities author these eight keys each. Before step 4 no
	// species row was bound, so every one was dropped and `0x103acac0` refused every target
	// (`m_flDetectionDistance` 0, strict `<`).
	FElysiumNpcWorldBuilder Builder(TEXT("species_bindings_rat"), 4007);
	FElysiumEntityDef& RatDef = Builder.AddNpc(TEXT("rat"), FVector::ZeroVector, TEXT("npc_VRat"));
	RatDef.Keys.Add(TEXT("friendship_level"), TEXT("1"));
	RatDef.Keys.Add(TEXT("warn_range"), TEXT("200"));
	RatDef.Keys.Add(TEXT("conflict_range"), TEXT("100"));
	RatDef.Keys.Add(TEXT("detection_distance"), TEXT("256"));
	RatDef.Keys.Add(TEXT("ignore_nosferatu"), TEXT("1"));
	RatDef.Keys.Add(TEXT("must_detect"), TEXT("1"));
	RatDef.Keys.Add(TEXT("fright_distance"), TEXT("128"));
	RatDef.Keys.Add(TEXT("fright_duration"), TEXT("5"));
	Builder.AddNpc(TEXT("other"), FVector(0.0, 400.0, 0.0));
	FElysiumNpcWorldFixture F(MoveTemp(Builder));
	FElysiumNpcScurrying* Rat = F.NpcAs<FElysiumNpcScurrying>(TEXT("rat"));
	FElysiumNpc* Other = F.Npc(TEXT("other"));
	FElysiumPlayer* Player = F.Player();
	if (!TestNotNull(TEXT("the rat is a CNPC_VScurrying"), Rat) || !TestNotNull(TEXT("other"), Other)
		|| !TestNotNull(TEXT("player"), Player))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Rat, Other });
	TestTrue(TEXT("npc_VRat builds CNPC_VRat"), Rat->IsRetailClass(TEXT("CNPC_VRat")));

	// The eight keys, on the words their datamap rows name.
	TestEqual(TEXT("friendship_level -> CNPC_VAnimal +0x6664"), Rat->AnimalFriendshipLevel, 1);
	TestEqual(TEXT("warn_range -> CNPC_VAnimal +0x6668"), Rat->AnimalWarnRangeUnits, 200.f);
	TestEqual(TEXT("conflict_range -> CNPC_VAnimal +0x666c"), Rat->AnimalConflictRangeUnits, 100.f);
	TestEqual(TEXT("detection_distance -> CNPC_VScurrying +0x6690"), Rat->ScurryingDetectionDistanceUnits, 256.f);
	TestTrue(TEXT("ignore_nosferatu -> CNPC_VScurrying +0x6694"), Rat->bScurryingIgnoreNosferatu);
	TestTrue(TEXT("must_detect -> CNPC_VScurrying +0x6695"), Rat->bScurryingMustDetect);
	TestEqual(TEXT("fright_distance -> CNPC_VScurrying +0x6698"), Rat->ScurryingFrightDistanceUnits, 128.f);
	TestEqual(TEXT("fright_duration -> CNPC_VScurrying +0x669c"), Rat->ScurryingFrightDurationSeconds, 5.f);

	// `0x103acac0` on the authored words, arm by arm (`vtmb_asm 0x103acac0`).
	const double U = ElysiumMove::U;
	// `103acb15 FCOMP [ESI+0x6690]` / `TEST AH,0x5 / JP`: strictly inside 256 units.
	Other->Origin = Rat->Origin + FVector(255.0 * U, 0.0, 0.0);
	TestTrue(TEXT("0x103acb15 a non-player 255 units away is detected"), Rat->ScurryingShouldDetect(Other));
	Other->Origin = Rat->Origin + FVector(256.0 * U, 0.0, 0.0);
	TestFalse(TEXT("...and at exactly 256 it is not: the compare is strict"), Rat->ScurryingShouldDetect(Other));

	// `103acb26`: `ignore_nosferatu` rejects a `Player_Nosferatu` target BEFORE the must-detect arm,
	// so a Nosferatu player the rat plainly sees is still refused.
	Player->Origin = Rat->Origin + FVector(100.0 * U, 0.0, 0.0);
	Rat->Cognition.Conditions.Set(static_cast<EElysiumNpcCond>(0x5a));
	Player->Sheet.SetClan(FElysiumSheet::ClanFromName(TEXT("Nosferatu")));
	TestFalse(TEXT("0x103acb26 a Nosferatu player is ignored even under COND_SEE_PLAYER"),
		Rat->ScurryingShouldDetect(Player));
	// `103acb3d`: `must_detect` — any other clan passes only under COND 0x5a or 0x6f.
	Player->Sheet.SetClan(FElysiumSheet::ClanFromName(TEXT("Brujah")));
	TestTrue(TEXT("0x103acb3d a Brujah player under COND_SEE_PLAYER is detected"),
		Rat->ScurryingShouldDetect(Player));
	Rat->Cognition.Conditions.Clear(static_cast<EElysiumNpcCond>(0x5a));
	Rat->Cognition.Conditions.Clear(static_cast<EElysiumNpcCond>(0x6f));
	TestFalse(TEXT("0x103ad0a0 and refused when neither condition stands"), Rat->ScurryingShouldDetect(Player));
	Other->Origin = Rat->Origin + FVector(100.0 * U, 0.0, 0.0);
	TestTrue(TEXT("0x103ad0a0 a non-player needs no condition"), Rat->ScurryingShouldDetect(Other));

	// `0x103acba0` with the authored `fright_distance` its caller passes (`0x103ac740`, story 8):
	// no AI network, so the node search fails and the march runs (`103acd6c`). The start is the
	// origin raised by half the step height; x and y march 128 units along the 3-D-normalised
	// direction AWAY from the threat and z is the start's (`103ace05`), so a threat 50 units below
	// shortens the horizontal march.
	const FVector Threat = Rat->Origin + FVector(-100.0 * U, 0.0, -50.0 * U);
	FVector Destination = FVector::ZeroVector;
	TestTrue(TEXT("0x103acba0 answers a destination on a clear trace"),
		Rat->ScurryingFindFleeDestination(Threat, Rat->ScurryingFrightDistanceUnits, &Destination));
	const FVector Away = (Rat->Origin - Threat).GetSafeNormal();
	const double StartZ = Rat->Origin.Z + static_cast<double>(Rat->StepHeight()) * 0.5 * U;
	TestTrue(TEXT("x marches 128 units times the normalised x"),
		FMath::IsNearlyEqual(Destination.X, Rat->Origin.X + Away.X * 128.0 * U, 0.01));
	TestTrue(TEXT("y does not move for a threat straight behind"),
		FMath::IsNearlyEqual(Destination.Y, Rat->Origin.Y, 0.01));
	TestTrue(TEXT("z is the raised start's, not marched"), FMath::IsNearlyEqual(Destination.Z, StartZ, 0.01));
	TestTrue(TEXT("and the horizontal march is shorter than 128 units"),
		(Destination.X - Rat->Origin.X) / U < 128.0);
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
