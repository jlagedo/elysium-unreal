#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumCollisionRules.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpcBase.h"
#include "Substrate/ElysiumNpcKernelShape.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCollisionRulesProductionTest,
	"Elysium.Substrate.CollisionRulesProduction",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FElysiumCollisionRulesProductionTest::RunTest(const FString&)
{
	// The complete stateless functions take group numbers as their public arguments. These
	// examples exercise retail's exclusions and adjacent accepting pairs, without manufacturing
	// a same-L0 SetCollisionGroup producer or physics object.
	struct FPair { int32 A; int32 B; bool bShared; bool bSinglePlayer; };
	const FPair Pairs[] = {
		{0, 0, true, true}, {0, 1, true, true}, {1, 1, false, false},
		{1, 2, false, false}, {2, 2, true, true}, {4, 4, false, false}, {4, 5, true, true},
		{0, 8, false, false}, {8, 20, false, false}, {8, 8, false, false},
		{13, 13, true, false}, {13, 14, true, true}, {15, 15, true, false}, {15, 16, true, true},
		{3, 17, true, false}, {6, 17, true, false}, {16, 17, true, false},
		{17, 17, true, false}, {17, 18, true, true}, {18, 18, true, false}, {18, 19, true, true},
		{2, 17, true, true}, {4, 17, true, true}, {6, 6, true, true}, {6, 18, true, true},
		{-1, 1, true, true}, {-1, 8, false, false}, {MIN_int32, MAX_int32, true, true},
		{1, MAX_int32, false, false}, {0, MAX_int32, true, true}
	};
	for (const FPair& Pair : Pairs)
	{
		const FString Label = FString::Printf(TEXT("retail groups %d/%d"), Pair.A, Pair.B);
		TestEqual(Label + TEXT(" shared"), ElysiumCollisionRules::SharedShouldCollide(Pair.A, Pair.B), Pair.bShared);
		TestEqual(Label + TEXT(" SP"), ElysiumCollisionRules::ShouldCollide(Pair.A, Pair.B), Pair.bSinglePlayer);
		TestEqual(Label + TEXT(" shared reversed"), ElysiumCollisionRules::SharedShouldCollide(Pair.B, Pair.A), Pair.bShared);
		TestEqual(Label + TEXT(" SP reversed"), ElysiumCollisionRules::ShouldCollide(Pair.B, Pair.A), Pair.bSinglePlayer);
		TestEqual(Label + TEXT(" existing caller"), FElysiumNpcBase::RetailGameRulesShouldCollide(Pair.A, Pair.B), Pair.bSinglePlayer);
	}

	// Actual registered construction/Spawn supplies the default group and inherited damage-type
	// slot on base, NPC and player receivers. No fake body, target, group write or trace result.
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__collision_rules_production__");
	FElysiumEntityDef Point;
	Point.Classname = TEXT("info_target"); Point.TargetName = TEXT("rules_point");
	Defs.Defs.Add(MoveTemp(Point));
	FElysiumEntityDef Pedestrian;
	Pedestrian.Classname = TEXT("npc_VPedestrian"); Pedestrian.TargetName = TEXT("rules_npc");
	Defs.Defs.Add(MoveTemp(Pedestrian));
	FElysiumEntityWorld World(nullptr, nullptr);
	World.Load(MoveTemp(Defs));
	World.SpawnPlayer();
	FElysiumEntity* PointEntity = World.FindByName(TEXT("rules_point"));
	FElysiumEntity* NpcEntity = World.FindByName(TEXT("rules_npc"));
	FElysiumEntity* PlayerEntity = World.FindPlayer();
	if (!TestNotNull(TEXT("registered point"), PointEntity)
		|| !TestNotNull(TEXT("registered NPC"), NpcEntity)
		|| !TestNotNull(TEXT("SpawnPlayer"), PlayerEntity)) return false;
	int32 (FElysiumEntity::* const DamageSlot)() const = &FElysiumEntity::GetDamageType;
	for (FElysiumEntity* Receiver : {PointEntity, NpcEntity, PlayerEntity})
	{
		TestEqual(TEXT("constructor produces zero group"), Receiver->CollisionGroup, 0);
		TestEqual(TEXT("virtual base damage-type selector returns zero"), (Receiver->*DamageSlot)(), 0);
		TestTrue(TEXT("production group pair reaches actual SP rules"),
			FElysiumNpcBase::RetailGameRulesShouldCollide(Receiver->CollisionGroup, PlayerEntity->CollisionGroup));
	}
	FElysiumNpcBase* NpcBase = NpcEntity->AsNpcBase();
	if (!TestNotNull(TEXT("real NPC character-filter caller"), NpcBase)) return false;
	TestTrue(TEXT("special-mask caller evaluates rules before bypassing unfinished bilateral tail"),
		NpcBase->KernelTraceKeepsCharacter(PlayerEntity->Handle, 0x46004003));
	bool bFoundDamageSlot = false;
	for (const auto& Row : ElysiumNpcKernelShape::EntitySlotRows())
	{
		if (Row.Slot != 189) continue;
		bFoundDamageSlot = true;
		TestTrue(TEXT("slot189 census names complete hand body"), Row.Body == EElysiumNpcSlotBody::Hand);
		TestTrue(TEXT("slot189 production declaration"), Row.bDeclaredHere);
		TestEqual(TEXT("slot189 retail identity"), FString(Row.Address), FString(TEXT("0x100ad850")));
	}
	TestTrue(TEXT("slot189 exists in production census"), bFoundDamageSlot);
	return true;
}

#endif
