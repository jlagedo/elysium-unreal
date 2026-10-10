// The entity data-object registry, arm tier (story L0.entity_core.data-object-registry,
// `docs/specs/layers/L0-entity/walks/L0-r019.md` § Arm tier only): the arms no arena record reaches
// because no retail entry produces an invalid type -- the `type & 31` aliasing of the mask writes
// against the signed 0..31 dispatch range, a type with no registered accessor, the create's bit
// write before validation, the destroy's clear after a skipped dispatch -- and the type-1 life of a
// touch pair through the port's touch edges.
#include "Misc/AutomationTest.h"

#include "Tests/ElysiumArmTier.h"

#if WITH_DEV_AUTOMATION_TESTS && ELYSIUM_WITH_ARM_TESTS

#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "Substrate/ElysiumDataObjects.h"
#include "Tests/ElysiumFixtureNoise.h"

namespace
{
	constexpr EAutomationTestFlags GElysiumDataObjectTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::ProductFilter;

	FElysiumEntityDefs MakeDataObjectTestDefs()
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__data_object_registry__");
		FElysiumEntityDef TriggerDef;
		TriggerDef.Classname = TEXT("trigger_multiple");
		TriggerDef.TargetName = TEXT("dobj_trigger");
		TriggerDef.Keys.Add(TEXT("spawnflags"), TEXT("1"));   // ALLOW_CLIENTS
		Defs.Defs.Add(MoveTemp(TriggerDef));
		FElysiumEntityDef RelayDef;
		RelayDef.Classname = TEXT("logic_relay");
		RelayDef.TargetName = TEXT("dobj_relay");
		Defs.Defs.Add(MoveTemp(RelayDef));
		return Defs;
	}
}

// A-1..A-5 of the walk, on an entity with every bit clear.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDataObjectRegistryArmsTest,
	"Elysium.Arm.L0DataObjectRegistry.Arms", GElysiumDataObjectTestFlags)
bool FElysiumDataObjectRegistryArmsTest::RunTest(const FString&)
{
	ElysiumFixtureNoise::Declare();
	FElysiumEntityWorld World(nullptr, nullptr);
	World.Load(MakeDataObjectTestDefs());
	World.Activate(0.0);
	FElysiumEntity* E = World.FindByName(TEXT("dobj_relay"));
	if (!TestNotNull(TEXT("the relay resolved"), E))
	{
		return false;
	}
	TestEqual(TEXT("the mask is born 0 (the ctor's initialiser is UNRECOVERED; 0 assumed)"), E->DataObjectTypes, 0u);

	// `Init` 0x1003c660 registered slots 0..3 and nothing else.
	for (int32 Type = 0; Type < FElysiumDataObjectAccessSystem::MaxTypes; ++Type)
	{
		TestEqual(FString::Printf(TEXT("slot %d is registered iff it is one of Init's four"), Type),
			World.DataObjects().Accessor(Type) != nullptr, Type < 4);
	}
	TestEqual(TEXT("slot 1 is the touch-link accessor (vtable 0x10449390)"),
		World.DataObjects().Accessor(1)->RetailVtable(), 0x10449390u);
	TestEqual(TEXT("slot 0 is the ground-link accessor (vtable 0x10449378)"),
		World.DataObjects().Accessor(0)->RetailVtable(), 0x10449378u);
	TestEqual(TEXT("slot 2 is the step-simulation accessor (vtable 0x10449360)"),
		World.DataObjects().Accessor(2)->RetailVtable(), 0x10449360u);
	TestEqual(TEXT("slot 3 is the model-width-scale accessor (vtable 0x10449348)"),
		World.DataObjects().Accessor(3)->RetailVtable(), 0x10449348u);

	// A-1: `Create(e, 32)`: `mask |= 0x1` (32 & 31 = 0) BEFORE the range check fails; returns 0; no
	// accessor is reached (slot 0 stays empty).
	TestNull(TEXT("A-1 Create(32) answers 0"), E->CreateDataObject(32));
	TestEqual(TEXT("A-1 Create(32) still set bit 0 (0x1003cfbf before 0x1003d056)"), E->DataObjectTypes, 0x1u);
	TestEqual(TEXT("A-1 no accessor was reached"), World.DataObjects().NumLive(), 0);
	TestTrue(TEXT("A-3 Has(32) reads bit 0"), E->HasDataObjectType(32));
	TestNull(TEXT("A-3 Get(32): bit set, range fails, 0"), E->GetDataObject(32));
	// A-2: `Destroy(e, 32)` with 0x1 set: arm 1 passes, arm 3 skips the call, arm 6 clears bit 0.
	E->DestroyDataObject(32);
	TestEqual(TEXT("A-2 Destroy(32) cleared bit 0 through the range skip (0x1003d28d -> 0x1003d26b)"), E->DataObjectTypes, 0u);

	// A-3: `Get(e, -1)` and `Get(e, 33)` with the mask clear: arm 1 answers 0, nothing written.
	TestNull(TEXT("A-3 Get(-1) is nobit"), E->GetDataObject(-1));
	TestNull(TEXT("A-3 Get(33) is nobit"), E->GetDataObject(33));
	TestEqual(TEXT("A-3 wrote nothing"), E->DataObjectTypes, 0u);
	// -1 & 31 = 31: `Create(e, -1)` sets bit 31 and fails the signed range check.
	TestNull(TEXT("A-3 Create(-1) answers 0"), E->CreateDataObject(-1));
	TestEqual(TEXT("A-3 Create(-1) set bit 31"), E->DataObjectTypes, 0x80000000u);
	TestNull(TEXT("A-3 Get(-1) with bit 31 set reaches the range check and answers 0"), E->GetDataObject(-1));
	E->DestroyDataObject(-1);
	TestEqual(TEXT("A-3 Destroy(-1) cleared bit 31 past the range skip"), E->DataObjectTypes, 0u);
	// 33 & 31 = 1: `Has(e, 33)` tests 0x2.
	E->AddDataObjectType(1);
	TestTrue(TEXT("A-3 Has(33) tests 0x2"), E->HasDataObjectType(33));
	TestNull(TEXT("A-3 Get(33) with 0x2 set: range fails, 0"), E->GetDataObject(33));
	E->RemoveDataObjectType(33);
	TestEqual(TEXT("A-3 Remove(33) clears 0x2"), E->DataObjectTypes, 0u);

	// A-4: `Create(e, 4)`, a valid type with no accessor: `mask |= 0x10`; returns 0.
	TestNull(TEXT("A-4 Create(4) answers 0 (null slot)"), E->CreateDataObject(4));
	TestEqual(TEXT("A-4 Create(4) set bit 4"), E->DataObjectTypes, 0x10u);
	TestNull(TEXT("A-4 Get(4): bit set, null slot, 0"), E->GetDataObject(4));
	// A-5: `Destroy(e, 4)` with 0x10 set: no call; `mask &= ~0x10`.
	E->DestroyDataObject(4);
	TestEqual(TEXT("A-5 Destroy(4) cleared bit 4 through the null-slot skip (0x1003d294 -> 0x1003d26b)"), E->DataObjectTypes, 0u);

	// Destroy with the bit clear: arm 1 leaves everything, including another type's bit.
	E->AddDataObjectType(3);
	E->DestroyDataObject(1);
	TestEqual(TEXT("Destroy(1) with bit 1 clear writes nothing (0x1003d1ad -> 0x1003d282)"), E->DataObjectTypes, 0x8u);
	E->RemoveDataObjectType(3);

	// Type 1 on the registered accessor: create is find-or-insert, get returns the block, a repeat
	// create hands back the same block untouched, destroy frees it and clears the bit.
	void* Head = E->CreateDataObject(1);
	TestNotNull(TEXT("Create(1) dispatches slot 2 and answers a block"), Head);
	TestEqual(TEXT("Create(1) set bit 1"), E->DataObjectTypes, 0x2u);
	TestEqual(TEXT("one live block"), World.DataObjects().NumLive(), 1);
	TestTrue(TEXT("Get(1) answers the same block"), E->GetDataObject(1) == Head);
	TestTrue(TEXT("a repeat Create(1) answers the existing block (0x10040970 find-or-insert)"), E->CreateDataObject(1) == Head);
	TestEqual(TEXT("still one live block"), World.DataObjects().NumLive(), 1);
	const FElysiumTouchLink* Link = static_cast<const FElysiumTouchLink*>(Head);
	TestTrue(TEXT("the block is zero-filled (0x14 bytes: handle -1, stamp 0, links 0, flags 0)"),
		!Link->EntityTouched.IsSet() && Link->TouchStamp == 0 && Link->NextLink == nullptr
		&& Link->PrevLink == nullptr && Link->Flags == 0);
	E->DestroyDataObject(1);
	TestEqual(TEXT("Destroy(1) cleared bit 1 after the dispatch"), E->DataObjectTypes, 0u);
	TestEqual(TEXT("Destroy(1) freed the block (0x10040b20)"), World.DataObjects().NumLive(), 0);
	TestNull(TEXT("Get(1) after destroy is nobit"), E->GetDataObject(1));

	// `AddDataAccessor` 0x1003c9f0 refuses an occupied or out-of-range slot.
	World.DataObjects().AddDataAccessor(1, nullptr);
	TestNotNull(TEXT("AddDataAccessor leaves an occupied slot alone"), World.DataObjects().Accessor(1));
	TestNull(TEXT("Accessor(32) is out of range"), World.DataObjects().Accessor(32));
	TestNull(TEXT("Accessor(-1) is out of range"), World.DataObjects().Accessor(-1));
	return true;
}

// The type-1 life of a touch pair through the port's touch edges: both sides get a head on the
// begin, a side keeps its head while a node remains, the last node's removal destroys it, and a
// dying brush releases its list without an `EndTouch` of its own.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDataObjectTouchLinkTest,
	"Elysium.Arm.L0DataObjectRegistry.TouchLink", GElysiumDataObjectTestFlags)
bool FElysiumDataObjectTouchLinkTest::RunTest(const FString&)
{
	ElysiumFixtureNoise::Declare();
	FElysiumEntityWorld World(nullptr, nullptr);
	World.Load(MakeDataObjectTestDefs());
	const FElysiumEntityHandle Player = World.SpawnPlayer();
	World.Activate(0.0);
	FElysiumEntity* Trigger = World.FindByName(TEXT("dobj_trigger"));
	FElysiumEntity* PlayerEnt = World.Resolve(Player);
	FElysiumEntity* Relay = World.FindByName(TEXT("dobj_relay"));
	if (!TestNotNull(TEXT("the trigger resolved"), Trigger) || !TestNotNull(TEXT("the player resolved"), PlayerEnt)
		|| !TestNotNull(TEXT("the relay resolved"), Relay))
	{
		return false;
	}
	TestEqual(TEXT("untouched: no bit on the brush"), Trigger->DataObjectTypes, 0u);
	TestEqual(TEXT("untouched: no bit on the player"), PlayerEnt->DataObjectTypes, 0u);

	// Begin: `PhysicsMarkEntitiesAsTouching` marks both directions -- each side a head and a node.
	World.RouteBrushTouch(Trigger->Handle, Player, /*bBegin*/ true);
	TestEqual(TEXT("begin: the brush holds type 1"), Trigger->DataObjectTypes, 0x2u);
	TestEqual(TEXT("begin: the player holds type 1"), PlayerEnt->DataObjectTypes, 0x2u);
	TestEqual(TEXT("begin: two heads"), World.DataObjects().NumLive(), 2);
	TestEqual(TEXT("begin: two nodes (DAT_106bd9b8)"), World.TouchLinkCount(), 2);
	const FElysiumTouchLink* Head = static_cast<const FElysiumTouchLink*>(Trigger->GetDataObject(1));
	if (TestNotNull(TEXT("the brush's head"), Head))
	{
		TestTrue(TEXT("the head is circular with one node"), Head->NextLink != Head && Head->NextLink->NextLink == Head
			&& Head->PrevLink == Head->NextLink);
		TestTrue(TEXT("the node names the player"), Head->NextLink->EntityTouched == Player);
		TestEqual(TEXT("the node's flag bit 1 marks the dispatched begin"), Head->NextLink->Flags & 1, 1);
	}
	// A repeat begin is no second node and no second create.
	World.RouteBrushTouch(Trigger->Handle, Player, /*bBegin*/ true);
	TestEqual(TEXT("repeat begin: still two nodes"), World.TouchLinkCount(), 2);

	// A second partner on the player: `RouteBrushTouch` on the relay would be refused (no brush), so the
	// pass is driven directly for the player's side only -- a node remains after the trigger pair ends.
	World.TouchLinkBegin(*PlayerEnt, Relay->Handle);
	TestEqual(TEXT("second partner: three nodes"), World.TouchLinkCount(), 3);
	TestEqual(TEXT("second partner: the player's head is reused, no new block"), World.DataObjects().NumLive(), 2);

	// End of the trigger pair: the brush's list empties and its object goes; the player keeps its head.
	World.RouteBrushTouch(Trigger->Handle, Player, /*bBegin*/ false);
	TestEqual(TEXT("end: the brush's bit is cleared (0x1003d490 after-loop destroy)"), Trigger->DataObjectTypes, 0u);
	TestEqual(TEXT("end: the player keeps its bit (a node remains)"), PlayerEnt->DataObjectTypes, 0x2u);
	TestEqual(TEXT("end: one head left"), World.DataObjects().NumLive(), 1);
	TestEqual(TEXT("end: one node left"), World.TouchLinkCount(), 1);
	// The last node's removal destroys the player's object.
	World.TouchLinkEnd(*PlayerEnt, Relay->Handle);
	TestEqual(TEXT("last node gone: the player's bit is cleared"), PlayerEnt->DataObjectTypes, 0u);
	TestEqual(TEXT("last node gone: no heads"), World.DataObjects().NumLive(), 0);
	TestEqual(TEXT("last node gone: no nodes"), World.TouchLinkCount(), 0);

	// A dying brush: `~CBaseEntity` -> `PhysicsRemoveTouchedList` releases both sides.
	World.RouteBrushTouch(Trigger->Handle, Player, /*bBegin*/ true);
	TestEqual(TEXT("re-begin: two heads"), World.DataObjects().NumLive(), 2);
	World.EnqueueInput(TEXT("dobj_trigger"), FName(TEXT("Kill")), FElysiumVariant::Void(), 0.0, Player, Player);
	World.Tick(0.0);
	TestEqual(TEXT("kill: the brush's object is destroyed unconditionally"), Trigger->DataObjectTypes, 0u);
	TestEqual(TEXT("kill: the player's node for the brush is gone and its list empty"), PlayerEnt->DataObjectTypes, 0u);
	TestEqual(TEXT("kill: no heads"), World.DataObjects().NumLive(), 0);
	TestEqual(TEXT("kill: no nodes"), World.TouchLinkCount(), 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && ELYSIUM_WITH_ARM_TESTS
