#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpcKernelShape.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumEntityPhysicsMaskProductionTest,
	"Elysium.Substrate.PhysicsMaskProduction",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FElysiumEntityPhysicsMaskProductionTest::RunTest(const FString&)
{
	// Real registered construction/Spawn supplies the type caches and NPC flag; no body or
	// trace fixture stands in for either input read by vampire.dll 0x1003e7a0.
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__physics_mask_production__");
	FElysiumEntityDef Point;
	Point.Classname = TEXT("info_target");
	Point.TargetName = TEXT("mask_point");
	Defs.Defs.Add(MoveTemp(Point));
	FElysiumEntityDef Pedestrian;
	Pedestrian.Classname = TEXT("npc_VPedestrian");
	Pedestrian.TargetName = TEXT("mask_npc");
	Defs.Defs.Add(MoveTemp(Pedestrian));
	FElysiumEntityWorld World(nullptr, nullptr);
	World.Load(MoveTemp(Defs));
	World.SpawnPlayer();
	FElysiumEntity* PointEntity = World.FindByName(TEXT("mask_point"));
	FElysiumEntity* NpcEntity = World.FindByName(TEXT("mask_npc"));
	FElysiumEntity* PlayerEntity = World.FindPlayer();
	if (!TestNotNull(TEXT("registered point construction"), PointEntity)
		|| !TestNotNull(TEXT("registered NPC construction"), NpcEntity)
		|| !TestNotNull(TEXT("production SpawnPlayer"), PlayerEntity)) return false;

	bool bFoundSlot237 = false;
	for (const auto& Row : ElysiumNpcKernelShape::EntitySlotRows())
	{
		if (Row.Slot != 237) continue;
		bFoundSlot237 = true;
		TestTrue(TEXT("slot 237 census identifies the hand body"), Row.Body == EElysiumNpcSlotBody::Hand);
		TestTrue(TEXT("slot 237 is declared on the actual base receiver"), Row.bDeclaredHere);
		TestEqual(TEXT("slot 237 retail identity"), FString(Row.Address), FString(TEXT("0x1003e7a0")));
	}
	TestTrue(TEXT("the slot 237 production declaration is present"), bFoundSlot237);
	int32 (FElysiumEntity::* const Slot237)() const = &FElysiumEntity::PhysicsSolidMaskForEntity;

	TestNull(TEXT("base construction has no player self-pointer"), PointEntity->AsPlayer());
	TestNull(TEXT("NPC construction has no player self-pointer"), NpcEntity->AsPlayer());
	TestTrue(TEXT("player construction supplies its actual self-pointer"),
		PlayerEntity->AsPlayer() == World.FindPlayer());
	TestEqual(TEXT("point default mask"), PointEntity->PhysicsSolidMaskForEntity(), 0x200400b);
	TestTrue(TEXT("NPCInit production writes FL_NPC"), (NpcEntity->Flags & 0x2000) != 0);
	TestEqual(TEXT("spawned NPC mask"), NpcEntity->PhysicsSolidMaskForEntity(), 0x202400b);
	TestEqual(TEXT("spawned player mask"), PlayerEntity->PhysicsSolidMaskForEntity(), 0x201400b);
	TestEqual(TEXT("slot 237 virtual member dispatch on the player base receiver"),
		(PlayerEntity->*Slot237)(), 0x201400b);

	// Runtime flag writes must affect the selector; type/FL_CLIENT cannot replace the first arm.
	PointEntity->Flags |= 0x80;
	TestEqual(TEXT("FL_CLIENT alone does not create a player self-pointer"),
		PointEntity->PhysicsSolidMaskForEntity(), 0x200400b);
	PointEntity->Flags |= 0x2000;
	TestEqual(TEXT("base entity with FL_NPC gets the NPC mask"),
		PointEntity->PhysicsSolidMaskForEntity(), 0x202400b);
	NpcEntity->Flags &= ~0x2000;
	TestEqual(TEXT("NPC type with FL_NPC removed falls back to base"),
		NpcEntity->PhysicsSolidMaskForEntity(), 0x200400b);
	PlayerEntity->Flags |= 0x2000;
	TestEqual(TEXT("FL_NPC precedes the nonnull player self-pointer"),
		PlayerEntity->PhysicsSolidMaskForEntity(), 0x202400b);
	PlayerEntity->Flags = 0;
	const FElysiumEntity& ConstPlayer = *PlayerEntity;
	TestTrue(TEXT("const self-downcast preserves the production identity"),
		ConstPlayer.AsPlayer() == World.FindPlayer());
	TestEqual(TEXT("player pointer still selects player mask with flags clear"),
		ConstPlayer.PhysicsSolidMaskForEntity(), 0x201400b);

	// Self-downcast caches are construction identity, not serialized pointers. Reset the real
	// world and rebuild its player through the production factory; don't retain old receivers.
	FElysiumEntityDefs ReloadDefs;
	ReloadDefs.MapName = TEXT("__physics_mask_recreated__");
	FElysiumEntityDef ReloadPoint;
	ReloadPoint.Classname = TEXT("info_target");
	ReloadPoint.TargetName = TEXT("mask_recreated_point");
	ReloadDefs.Defs.Add(MoveTemp(ReloadPoint));
	World.Teardown();
	FElysiumEntityWorld RecreatedWorld(nullptr, nullptr);
	RecreatedWorld.Load(MoveTemp(ReloadDefs));
	RecreatedWorld.SpawnPlayer();
	FElysiumEntity* RecreatedPlayer = RecreatedWorld.FindPlayer();
	FElysiumEntity* RecreatedPoint = RecreatedWorld.FindByName(TEXT("mask_recreated_point"));
	if (!TestNotNull(TEXT("player recreated after world reset"), RecreatedPlayer)
		|| !TestNotNull(TEXT("base entity recreated after world reset"), RecreatedPoint)) return false;
	TestTrue(TEXT("recreated player's cache names its current self"),
		RecreatedPlayer->AsPlayer() == RecreatedWorld.FindPlayer());
	TestNull(TEXT("base entity did not inherit the departed player's identity"), RecreatedPoint->AsPlayer());
	TestEqual(TEXT("recreated player still routes through slot 237"), (RecreatedPlayer->*Slot237)(), 0x201400b);
	TestEqual(TEXT("recreated base still routes through slot 237"), (RecreatedPoint->*Slot237)(), 0x200400b);
	return true;
}

#endif
