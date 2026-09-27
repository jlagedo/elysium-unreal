#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumPlayer.h"
#include "ElysiumStub.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcPedestrian.h"
#include "Tests/ElysiumNpcTestFixture.h"

// Story 5 step 6 (spec 0019): every generated slot body stands on the port class of its retail
// owner — `FElysiumEntity`, `FElysiumAnimating`, `FElysiumAnimatingOverlay`, `FElysiumFlex`,
// `FElysiumCombatCharacter`, `FElysiumNpcBase` or `FElysiumNpc` — and a stub names that owner.
//
// These cases hold what that move made observable: the constant bodies a Troika instance never
// dispatches to (a chain class's own body under a more-derived one) still answer retail's literal,
// a chain stub fires under its owner's name with its own address, and the entity-method
// integrations write and answer the port's own words.

static constexpr EAutomationTestFlags GChainSlotsTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	struct FChainSlotsFixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumNpc* Npc = nullptr;
		FElysiumNpc* Pedestrian = nullptr;

		FChainSlotsFixture()
			: World(Build())
		{
			Npc = World.Npc(TEXT("subject"));
			Pedestrian = World.Npc(TEXT("walker"));
			FElysiumNpcWorldFixture::Quiet({ Npc, Pedestrian });
		}

		static FElysiumNpcWorldBuilder Build()
		{
			FElysiumNpcWorldBuilder Builder(TEXT("chainslots"), 20260927);
			Builder.AddNpc(TEXT("subject"), FVector(100.0, 0.0, 0.0), TEXT("npc_VHumanCombatant"));
			Builder.AddNpc(TEXT("walker"), FVector(-100.0, 0.0, 0.0), TEXT("npc_VPedestrian"));
			return Builder;
		}
	};

	const ElysiumStub::FTally* FindTally(const TArray<ElysiumStub::FTally>& Tally, const TCHAR* Surface)
	{
		return Tally.FindByPredicate([Surface](const ElysiumStub::FTally& Row)
			{
				return Row.Kind == TEXT("slot") && Row.Surface == Surface;
			});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelShadowedDefaultsTest,
	"Elysium.Substrate.NpcKernelSlots.ShadowedDefaults", GChainSlotsTestFlags)
bool FElysiumNpcKernelShadowedDefaultsTest::RunTest(const FString&)
{
	FChainSlotsFixture F;
	if (!TestNotNull(TEXT("the fixture stands the subject"), F.Npc))
	{
		return false;
	}
	// A chain class's own constant body at a slot a more-derived class refills is emitted on its
	// class but never reached by a Troika instance's virtual call. The generator probes each one
	// through a qualified call on the same instance, which runs exactly that class's body.
	TArrayView<const FElysiumNpcSlotDefault> Rows = ElysiumNpcKernelShape::ShadowedSlotDefaults();
	TestTrue(TEXT("the shadowed constant bodies have probes"), Rows.Num() > 0);
	ElysiumStub::ClearTally();
	for (const FElysiumNpcSlotDefault& Row : Rows)
	{
		if (!TestNotNull(FString::Printf(TEXT("slot %d (%s) carries a probe"), Row.Slot, Row.Address),
			Row.Invoke))
		{
			continue;
		}
		const int64 Answer = Row.Invoke(*F.Npc);
		if (!Row.bVoid)
		{
			TestEqual(FString::Printf(TEXT("slot %d (%s, %s) answers retail's %s"), Row.Slot, Row.Address,
				Row.PortMethod, Row.Retail), Answer, Row.Value);
		}
	}
	TArray<ElysiumStub::FTally> Tally;
	ElysiumStub::CollectTally(Tally);
	for (const ElysiumStub::FTally& Fired : Tally)
	{
		AddError(FString::Printf(TEXT("a shadowed default still tallies a stub: %s %s"), *Fired.Surface,
			*Fired.Address));
	}
	ElysiumStub::ClearTally();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelChainStubOwnerTest,
	"Elysium.Substrate.NpcKernelChainSlots.StubNamesItsOwner", GChainSlotsTestFlags)
bool FElysiumNpcKernelChainStubOwnerTest::RunTest(const FString&)
{
	FChainSlotsFixture F;
	if (!TestNotNull(TEXT("the fixture stands the subject"), F.Npc))
	{
		return false;
	}
	// Slot 8 `GetModelIndex` (`0x100b17f0`) is `CBaseEntity`'s body, still a stub: it stands on
	// `FElysiumEntity` and its tally names `CBaseEntity`, with the same address and receiver it had
	// when the NPC base declared it.
	ElysiumStub::ClearTally();
	(void)F.Npc->GetModelIndex();
	(void)static_cast<FElysiumEntity*>(F.Npc)->GetModelIndex();
	TArray<ElysiumStub::FTally> Tally;
	ElysiumStub::CollectTally(Tally);
	const ElysiumStub::FTally* Row = FindTally(Tally, TEXT("CBaseEntity::GetModelIndex"));
	if (TestNotNull(TEXT("slot 8 tallies under CBaseEntity"), Row))
	{
		TestEqual(TEXT("with the retail body's address"), Row->Address, FString(TEXT("0x100b17f0")));
		TestEqual(TEXT("once per call, through the NPC or the entity"), Row->Count, 2);
	}
	TestNull(TEXT("nothing tallies under the old Troika prefix"),
		FindTally(Tally, TEXT("CAI_BaseNPCTroika::GetModelIndex")));
	ElysiumStub::ClearTally();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelChainIntegrationsTest,
	"Elysium.Substrate.NpcKernelChainSlots.EntityIntegrations", GChainSlotsTestFlags)
bool FElysiumNpcKernelChainIntegrationsTest::RunTest(const FString&)
{
	FChainSlotsFixture F;
	if (!TestNotNull(TEXT("the fixture stands the subject"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& Npc = *F.Npc;
	ElysiumStub::ClearTally();

	// Slots 217/219/220/221: the port has the words the retail references name, so the readers
	// answer them by reference. 220/221 equal 217/219: no local/abs split (a named modernization).
	TestTrue(TEXT("slot 217 GetAbsOrigin is the entity's Origin"), &Npc.GetAbsOrigin() == &Npc.Origin);
	TestTrue(TEXT("slot 219 GetAbsAngles is the entity's Angles"), &Npc.GetAbsAngles() == &Npc.Angles);
	TestTrue(TEXT("slot 220 GetOrigin is the entity's Origin"), &Npc.GetOrigin() == &Npc.Origin);
	TestTrue(TEXT("slot 221 GetAngles is the entity's Angles"), &Npc.GetAngles() == &Npc.Angles);
	TestTrue(TEXT("slot 194 EyeAngles answers slot 219"), &Npc.EyeAngles() == &Npc.Angles);
	TestTrue(TEXT("slot 195 LocalEyeAngles answers slot 221"), &Npc.LocalEyeAngles() == &Npc.Angles);

	// Slot 62 `SetOrigin` (`0x100b2be0`) writes only a vector that differs, through SetRuntimeOrigin.
	const FVector Moved = Npc.Origin + FVector(10.0, 20.0, 0.0);
	Npc.SetOrigin(Moved);
	TestEqual(TEXT("slot 62 writes the origin"), Npc.Origin, Moved);

	// Slot 93 `SetMoveType` (`0x100aad70`) writes both words only when the type differs.
	Npc.SetMoveType(4, 0);
	TestEqual(TEXT("slot 93 writes m_MoveType"), Npc.RetailMoveType, 4);
	TestEqual(TEXT("...and m_MoveCollide"), Npc.RetailMoveCollide, 0);
	Npc.SetMoveType(4, 7);
	TestEqual(TEXT("the same type writes nothing, not even the collide word"), Npc.RetailMoveCollide, 0);
	Npc.SetMoveType(5, 2);
	TestEqual(TEXT("a new type writes m_MoveType"), Npc.RetailMoveType, 5);
	TestEqual(TEXT("...and m_MoveCollide with it"), Npc.RetailMoveCollide, 2);

	TArray<ElysiumStub::FTally> Tally;
	ElysiumStub::CollectTally(Tally);
	for (const ElysiumStub::FTally& Fired : Tally)
	{
		AddError(FString::Printf(TEXT("an integrated slot tallied a stub: %s %s"), *Fired.Surface,
			*Fired.Address));
	}
	ElysiumStub::ClearTally();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelChainDescriptorsTest,
	"Elysium.Substrate.NpcKernelChainSlots.DescriptorChain", GChainSlotsTestFlags)
bool FElysiumNpcKernelChainDescriptorsTest::RunTest(const FString&)
{
	// A live NPC's descriptor chain walks retail's whole entity chain, and each node is the port
	// class that owns its slot bodies: ... CAI_BaseNPC < CBaseCombatCharacter < CBaseFlex <
	// CBaseAnimatingOverlay < CBaseAnimating < CBaseEntity.
	const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
	TArray<FString> Chain;
	for (const FElysiumClassDesc* D = Reg.Find(FName(TEXT("npc_VRat"))); D != nullptr;
		D = D->BaseName.IsNone() ? nullptr : Reg.Find(D->BaseName))
	{
		Chain.Add(D->ClassName.ToString());
	}
	const TArray<FString> Tail = { TEXT("CAI_BaseNPC"), TEXT("CBaseCombatCharacter"), TEXT("CBaseFlex"),
		TEXT("CBaseAnimatingOverlay"), TEXT("CBaseAnimating"), TEXT("CBaseEntity") };
	const int32 At = Chain.Find(TEXT("CAI_BaseNPC"));
	if (!TestTrue(TEXT("npc_VRat's chain reaches CAI_BaseNPC"), At != INDEX_NONE))
	{
		return false;
	}
	TestEqual(TEXT("the chain below the NPC base is retail's"),
		TArray<FString>(Chain.GetData() + At, Chain.Num() - At), Tail);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPedestrianRestoreMoveTypeTest,
	"Elysium.Substrate.NpcKernelChainSlots.PedestrianRestoreMoveType", GChainSlotsTestFlags)
bool FElysiumNpcKernelPedestrianRestoreMoveTypeTest::RunTest(const FString&)
{
	FChainSlotsFixture F;
	FElysiumNpcPedestrian* Pedestrian = F.Pedestrian != nullptr ? F.Pedestrian->AsSpecies<FElysiumNpcPedestrian>()
		: nullptr;
	if (!TestNotNull(TEXT("the fixture stands an npc_VPedestrian"), Pedestrian))
	{
		return false;
	}
	// Retail correction (story 5 step 6): `CNPC_VPedestrian::OnRestore`'s load arm calls slot 93
	// `SetMoveType(MOVETYPE_FLY = 4, MOVECOLLIDE_DEFAULT = 0)` at `103a2816`. It fired a stub and
	// wrote nothing; the slot is now the `m_MoveType`/`m_MoveCollide` seam.
	Pedestrian->PedestrianLevelResetType = 0;
	Pedestrian->RetailMoveType = 0;
	Pedestrian->RetailMoveCollide = 9;
	Pedestrian->OnRestore(true);
	TestEqual(TEXT("the restore writes MOVETYPE_FLY"), Pedestrian->RetailMoveType, 4);
	TestEqual(TEXT("...and MOVECOLLIDE_DEFAULT"), Pedestrian->RetailMoveCollide, 0);
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
