#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcPedestrian.h"
#include "Tests/ElysiumNpcTestFixture.h"

// Story 5 step 6 (spec 0019): every generated slot body stands on the port class of its retail
// owner — `FElysiumEntity`, `FElysiumAnimating`, `FElysiumAnimatingOverlay`, `FElysiumFlex`,
// `FElysiumCombatCharacter`, `FElysiumNpcBase` or `FElysiumNpc` — and a stub names that owner.
//
// These cases hold what that move made observable: the entity-method integrations write and answer
// the port's own words. Every class's constant bodies are probed on a receiver of that class by
// `NpcKernelSlots.Defaults`, and the per-class slot tables are held to the chain by
// `NpcKernelShape.SlotOwners`.

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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelChainIntegrationsTest,
	"Elysium.Arm.NpcKernelChainSlots.EntityIntegrations", GChainSlotsTestFlags)
bool FElysiumNpcKernelChainIntegrationsTest::RunTest(const FString&)
{
	FChainSlotsFixture F;
	if (!TestNotNull(TEXT("the fixture stands the subject"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& Npc = *F.Npc;

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
