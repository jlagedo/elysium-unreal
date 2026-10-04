#include "Misc/AutomationTest.h"

#include "Tests/ElysiumArmTier.h"

#if WITH_DEV_AUTOMATION_TESTS && ELYSIUM_WITH_ARM_TESTS

#include "ElysiumEntityWorld.h"
#include "Substrate/ElysiumAttackCoordinator.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Tests/ElysiumNpcTestFixture.h"

#include <limits>

// Spec 0002 V11-1: retail's attack coordinator (`0x1025d880` ... `0x1025df40`), on the object alone.
// Every assertion is read off the listing of the body it names (`docs/vtmb/npc-ai/social.md` § "The
// attack coordinator object").

static constexpr EAutomationTestFlags GAttackCoordinatorTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace ElysiumAttackCoordinatorTests
{
	// Four NPCs in one headless world and a coordinator of the test's own (cap 2, as retail's three).
	struct FRig
	{
		FElysiumNpcWorldFixture F;
		FElysiumNpc* A = nullptr;
		FElysiumNpc* B = nullptr;
		FElysiumNpc* C = nullptr;
		FElysiumNpc* Foe = nullptr;
		FElysiumAttackCoordinator List;

		static FElysiumNpcWorldBuilder Build(const TCHAR* Name)
		{
			FElysiumNpcWorldBuilder Builder(Name, 0x0002b111);
			Builder.AddNpc(TEXT("a"), FVector(100.0, 0.0, 0.0));
			Builder.AddNpc(TEXT("b"), FVector(0.0, 100.0, 0.0));
			Builder.AddNpc(TEXT("c"), FVector(-100.0, 0.0, 0.0));
			Builder.AddNpc(TEXT("foe"), FVector::ZeroVector);
			return Builder;
		}

		explicit FRig(const TCHAR* Name)
			: F(Build(Name))
			, List(F.World, TEXT("Test"), 2)
		{
			A = F.Npc(TEXT("a"));
			B = F.Npc(TEXT("b"));
			C = F.Npc(TEXT("c"));
			Foe = F.Npc(TEXT("foe"));
			FElysiumNpcWorldFixture::Quiet({ A, B, C, Foe });
		}

		bool Ready(FAutomationTestBase& Test) const
		{
			if (A == nullptr || B == nullptr || C == nullptr || Foe == nullptr)
			{
				Test.AddError(TEXT("fixture did not stand the four NPCs"));
				return false;
			}
			return true;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumAttackCoordinatorAdmitTest,
	"Elysium.Arm.AttackCoordinator.Admit", GAttackCoordinatorTestFlags)
bool FElysiumAttackCoordinatorAdmitTest::RunTest(const FString&)
{
	ElysiumAttackCoordinatorTests::FRig R(TEXT("__coordinator_admit__"));
	if (!R.Ready(*this))
	{
		return false;
	}
	// 0x1025db70: append under the cap.
	TestTrue(TEXT("room: appended"), R.List.Add(R.A));
	TestEqual(TEXT("count 1"), R.List.Num(), 1);
	// Listed -> 1 with no insert.
	TestTrue(TEXT("listed: true"), R.List.Add(R.A));
	TestEqual(TEXT("and no second entry"), R.List.Num(), 1);
	TestTrue(TEXT("room: the second"), R.List.Add(R.B));
	TestEqual(TEXT("count 2 = cap"), R.List.Num(), 2);
	TestTrue(TEXT("listed while full: still true"), R.List.Add(R.B));

	// Full -> 0x1025dca0(npc, 1): only a member strictly farther than the candidate is evicted.
	R.A->ScheduleHost.EnemyDistUnits = 100.f;
	R.B->ScheduleHost.EnemyDistUnits = 300.f;
	R.C->ScheduleHost.EnemyDistUnits = 400.f;
	TestFalse(TEXT("full, and the candidate is the farthest: refused"), R.List.Add(R.C));
	TestEqual(TEXT("nothing changed"), R.List.Num(), 2);
	TestTrue(TEXT("C is absent"), R.List.IsAbsent(R.C));
	R.C->ScheduleHost.EnemyDistUnits = 200.f;
	TestTrue(TEXT("full, and a member is farther than the candidate: admitted"), R.List.Add(R.C));
	TestTrue(TEXT("the farther member B was released"), R.List.IsAbsent(R.B));
	TestFalse(TEXT("the nearer member A stays"), R.List.IsAbsent(R.A));
	TestFalse(TEXT("and C is listed"), R.List.IsAbsent(R.C));
	TestEqual(TEXT("count stays at the cap"), R.List.Num(), 2);

	// The null NPC: retail recurses (0x1025db70 <-> 0x1025dca0); the port answers false.
	TestFalse(TEXT("a null NPC is refused (crash arm not reproduced)"), R.List.Add(nullptr));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumAttackCoordinatorEvictTest,
	"Elysium.Arm.AttackCoordinator.Evict", GAttackCoordinatorTestFlags)
bool FElysiumAttackCoordinatorEvictTest::RunTest(const FString&)
{
	ElysiumAttackCoordinatorTests::FRig R(TEXT("__coordinator_evict__"));
	if (!R.Ready(*this))
	{
		return false;
	}
	// 0x1025dca0 with room is 0x1025db70.
	TestTrue(TEXT("room -> Add"),
		R.List.AddOrEvict(R.A, EElysiumCoordinatorEvict::FarthestMember));
	TestTrue(TEXT("room -> Add"),
		R.List.AddOrEvict(R.B, EElysiumCoordinatorEvict::FarthestMember));

	// useDist 0: the threshold is 0.0, so the strictly greatest member goes whatever the candidate's
	// own distance.
	R.A->ScheduleHost.EnemyDistUnits = 100.f;
	R.B->ScheduleHost.EnemyDistUnits = 300.f;
	R.C->ScheduleHost.EnemyDistUnits = 5000.f;
	TestTrue(TEXT("no distance: a far candidate still evicts"),
		R.List.AddOrEvict(R.C, EElysiumCoordinatorEvict::FarthestMember));
	TestTrue(TEXT("the member with the greatest m_flEnemyDist (B) went"), R.List.IsAbsent(R.B));
	TestFalse(TEXT("A stays"), R.List.IsAbsent(R.A));
	TestFalse(TEXT("C is in"), R.List.IsAbsent(R.C));

	// useDist 0 with every member at 0.0: `0.0 < 0.0` is false, none found -> 0, unchanged.
	R.A->ScheduleHost.EnemyDistUnits = 0.f;
	R.C->ScheduleHost.EnemyDistUnits = 0.f;
	TestFalse(TEXT("no member above the 0.0 threshold: refused"),
		R.List.AddOrEvict(R.B, EElysiumCoordinatorEvict::FarthestMember));
	TestEqual(TEXT("unchanged"), R.List.Num(), 2);
	TestTrue(TEXT("B still absent"), R.List.IsAbsent(R.B));

	// Ties keep the EARLIER member: with both at 250 the first in the list is the one evicted,
	// because the second is not strictly greater than the running best.
	R.List.Reset();
	R.List.Add(R.A);
	R.List.Add(R.B);
	R.A->ScheduleHost.EnemyDistUnits = 250.f;
	R.B->ScheduleHost.EnemyDistUnits = 250.f;
	R.C->ScheduleHost.EnemyDistUnits = 10.f;
	TestTrue(TEXT("a tie evicts"), R.List.AddOrEvict(R.C, EElysiumCoordinatorEvict::FartherThanCandidate));
	TestTrue(TEXT("the earlier of the tied members (A)"), R.List.IsAbsent(R.A));
	TestFalse(TEXT("the later one (B) stays"), R.List.IsAbsent(R.B));

	// useDist 1 at the candidate's own distance: a member AT the threshold is not strictly greater.
	R.List.Reset();
	R.List.Add(R.A);
	R.List.Add(R.B);
	R.C->ScheduleHost.EnemyDistUnits = 250.f;
	TestFalse(TEXT("members no farther than the candidate: refused"),
		R.List.AddOrEvict(R.C, EElysiumCoordinatorEvict::FartherThanCandidate));

	// A NaN distance never compares greater: the member is kept.
	R.A->ScheduleHost.EnemyDistUnits = std::numeric_limits<float>::quiet_NaN();
	R.B->ScheduleHost.EnemyDistUnits = std::numeric_limits<float>::quiet_NaN();
	TestFalse(TEXT("NaN members are never the greatest"),
		R.List.AddOrEvict(R.C, EElysiumCoordinatorEvict::FarthestMember));
	TestEqual(TEXT("unchanged"), R.List.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumAttackCoordinatorReleaseTest,
	"Elysium.Arm.AttackCoordinator.Release", GAttackCoordinatorTestFlags)
bool FElysiumAttackCoordinatorReleaseTest::RunTest(const FString&)
{
	ElysiumAttackCoordinatorTests::FRig R(TEXT("__coordinator_release__"));
	if (!R.Ready(*this))
	{
		return false;
	}
	FElysiumAttackCoordinator Three(R.F.World, TEXT("Three"), 3);
	Three.Add(R.A);
	Three.Add(R.B);
	Three.Add(R.C);
	// 0x1025ddd0: the LAST entry is copied over the found one, `count--`.
	Three.Release(R.A);
	TestEqual(TEXT("count 2"), Three.Num(), 2);
	if (Three.Num() == 2)
	{
		TestTrue(TEXT("the last entry (C) took the released slot"), Three.Handles()[0] == R.C->Handle);
		TestTrue(TEXT("and B kept its place"), Three.Handles()[1] == R.B->Handle);
	}
	// Absent and null: no-ops.
	Three.Release(R.A);
	TestEqual(TEXT("an absent NPC releases nothing"), Three.Num(), 2);
	Three.Release(nullptr);
	TestEqual(TEXT("a null NPC releases nothing"), Three.Num(), 2);
	Three.Release(R.B);
	Three.Release(R.C);
	TestEqual(TEXT("empty"), Three.Num(), 0);
	Three.Release(R.C);
	TestEqual(TEXT("a release from an empty list is a no-op"), Three.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumAttackCoordinatorQueryTest,
	"Elysium.Arm.AttackCoordinator.Query", GAttackCoordinatorTestFlags)
bool FElysiumAttackCoordinatorQueryTest::RunTest(const FString&)
{
	ElysiumAttackCoordinatorTests::FRig R(TEXT("__coordinator_query__"));
	if (!R.Ready(*this))
	{
		return false;
	}
	// 0x1025db50: `count < cap`.
	TestTrue(TEXT("empty: room"), R.List.HasRoom());
	// 0x1025de90: 1 for a null NPC and for an empty list.
	TestTrue(TEXT("a null NPC is absent"), R.List.IsAbsent(nullptr));
	TestTrue(TEXT("an empty list holds nobody"), R.List.IsAbsent(R.A));
	R.List.Add(R.A);
	TestTrue(TEXT("one of two: room"), R.List.HasRoom());
	TestFalse(TEXT("a listed NPC is not absent"), R.List.IsAbsent(R.A));
	TestTrue(TEXT("another one is"), R.List.IsAbsent(R.B));
	R.List.Add(R.B);
	TestFalse(TEXT("two of two: no room"), R.List.HasRoom());
	// 0x1025e120.
	TestEqual(TEXT("the name"), R.List.Name(), FString(TEXT("Test")));
	TestEqual(TEXT("the cap"), R.List.Cap(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumAttackCoordinatorCircleSideTest,
	"Elysium.Arm.AttackCoordinator.CircleSide", GAttackCoordinatorTestFlags)
bool FElysiumAttackCoordinatorCircleSideTest::RunTest(const FString&)
{
	ElysiumAttackCoordinatorTests::FRig R(TEXT("__coordinator_circle__"));
	if (!R.Ready(*this))
	{
		return false;
	}
	// 0x1025df40's three zero arms.
	R.List.Add(R.A);
	TestEqual(TEXT("fewer than two members: 0"), R.List.CircleSide(R.A, R.Foe), 0);
	R.List.Add(R.B);
	TestEqual(TEXT("a null NPC: 0"), R.List.CircleSide(nullptr, R.Foe), 0);
	TestEqual(TEXT("a null enemy: 0"), R.List.CircleSide(R.A, nullptr), 0);

	// The enemy at the origin. Source's Y is this world's -Y, so in SOURCE axes A stands at bearing
	// 0 and B (port +Y) at bearing -90. `AngleDiff(mine, theirs)`: for A, 0 - (-90) = +90 -> 1;
	// for B, -90 - 0 = -90 -> -1.
	R.Foe->Origin = FVector::ZeroVector;
	R.A->Origin = FVector(100.0, 0.0, 0.0);
	R.B->Origin = FVector(0.0, 100.0, 0.0);
	TestEqual(TEXT("the other member is clockwise of me: 1"), R.List.CircleSide(R.A, R.Foe), 1);
	TestEqual(TEXT("the other member is counter-clockwise of me: -1"), R.List.CircleSide(R.B, R.Foe), -1);
	// The same bearing: a difference of 0 is `<= 0`, -1.
	R.B->Origin = FVector(200.0, 0.0, 0.0);
	TestEqual(TEXT("a zero difference is -1"), R.List.CircleSide(R.A, R.Foe), -1);
	// An NPC not in the list is measured against every member.
	R.B->Origin = FVector(0.0, 100.0, 0.0);
	R.C->Origin = FVector(0.0, -100.0, 0.0);   // Source bearing +90
	// Against A (0): +90; against B (-90): 180 -> the smaller magnitude is +90 -> 1.
	TestEqual(TEXT("an outsider takes the nearest member's side"), R.List.CircleSide(R.C, R.Foe), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumAttackCoordinatorLifetimeTest,
	"Elysium.Arm.AttackCoordinator.Lifetime", GAttackCoordinatorTestFlags)
bool FElysiumAttackCoordinatorLifetimeTest::RunTest(const FString&)
{
	ElysiumAttackCoordinatorTests::FRig R(TEXT("__coordinator_lifetime__"));
	if (!R.Ready(*this))
	{
		return false;
	}
	FElysiumEntityWorld& World = R.F.World;
	// 0x1025d880: three objects, cap 2, "Normal" / "Player" / "Boss"; index 0 is retail's null.
	TestNull(TEXT("index 0 is the null coordinator"), World.AttackCoordinator(0));
	TestNull(TEXT("index 4 names nothing"), World.AttackCoordinator(4));
	static const TCHAR* const Names[] = { TEXT("Normal"), TEXT("Player"), TEXT("Boss") };
	for (int32 Index = 1; Index <= 3; ++Index)
	{
		const FElysiumAttackCoordinator* const Coordinator = World.AttackCoordinator(Index);
		if (!TestNotNull(TEXT("the world stands the coordinator"), Coordinator))
		{
			return false;
		}
		TestEqual(TEXT("its name"), Coordinator->Name(), FString(Names[Index - 1]));
		TestEqual(TEXT("cap 2"), Coordinator->Cap(), 2);
	}
	// 0x1025d940 / the next map's 0x1025d880. The port's entity world IS one map's: `Teardown` (which
	// empties the three, private, the destructor's body) ends it, and the next map's world builds
	// three empty ones. So: a list filled in this world, and a second world standing empty.
	FElysiumAttackCoordinator* const Normal = World.AttackCoordinator(1);
	Normal->Reset();
	Normal->Add(R.A);
	Normal->Add(R.B);
	TestEqual(TEXT("two members in this world's Normal"), Normal->Num(), 2);
	{
		ElysiumAttackCoordinatorTests::FRig Next(TEXT("__coordinator_lifetime_next__"));
		if (!Next.Ready(*this))
		{
			return false;
		}
		const FElysiumAttackCoordinator* const NextNormal = Next.F.World.AttackCoordinator(1);
		if (!TestNotNull(TEXT("the next world stands its own Normal"), NextNormal))
		{
			return false;
		}
		TestTrue(TEXT("a different object"), NextNormal != Normal);
		// A spawn may already have registered nobody: slots 599 / 600 run only from a selection.
		TestEqual(TEXT("the next map starts with an empty coordinator"), NextNormal->Num(), 0);
	}
	// `Reset` is what `Teardown` runs on each of the three.
	Normal->Reset();
	TestEqual(TEXT("empty after the reset"), Normal->Num(), 0);
	TestTrue(TEXT("with room"), Normal->HasRoom());
	return true;
}

#endif
