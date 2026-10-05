#include "Misc/AutomationTest.h"
#include "Tests/ElysiumArmTier.h"

#if WITH_DEV_AUTOMATION_TESTS && ELYSIUM_WITH_ARM_TESTS
#include "ElysiumMoveSolve.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcBase.h"

namespace ElysiumNpcAttackExtentsTests
{
static constexpr EAutomationTestFlags Flags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// 0x10090c80: descriptor fixture only. J2b's production SequenceBounds remains false.
struct FBoundsNpc final : FElysiumNpc
{
	bool bHasBounds = true;
	FVector TestBoundsMin = FVector(-30.f, -40.f, -5.f);
	FVector TestBoundsMax = FVector(25.f, 35.f, 100.f);
	mutable int32 BoundsQueries = 0;
	virtual bool SequenceBounds(int32, FVector& OutBoundsMin, FVector& OutBoundsMax) const override
	{
		++BoundsQueries;
		OutBoundsMin = TestBoundsMin;
		OutBoundsMax = TestBoundsMax;
		return bHasBounds;
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSequenceExtentsGates,
	"Elysium.Arm.NpcAttackExtents.FlagAndDescriptor", Flags)
bool FSequenceExtentsGates::RunTest(const FString&)
{
	FBoundsNpc BoundsNpc;
	BoundsNpc.SetAttackExtents(FVector(7.f, 8.f, 9.f)); // entity slot 15, 0x1009af40
	BoundsNpc.SetAttackExtentsForSequence(1);
	TestEqual(TEXT("0x10090c80 flag clear: descriptor never asked"), BoundsNpc.BoundsQueries, 0);
	TestTrue(TEXT("0x10090c80 flag clear: saved extents preserved"),
		BoundsNpc.GetAttackExtents().Equals(FVector(7.f, 8.f, 9.f)));
	BoundsNpc.AddFlag2(4u);
	BoundsNpc.bHasBounds = false;
	BoundsNpc.SetAttackExtentsForSequence(1);
	TestEqual(TEXT("0x10090c80 flag set: descriptor asked"), BoundsNpc.BoundsQueries, 1);
	TestTrue(TEXT("0x10090c80 no descriptor: no extents/partition write"),
		BoundsNpc.GetAttackExtents().Equals(FVector(7.f, 8.f, 9.f)));
	FElysiumNpc LiveSeamNpc;
	LiveSeamNpc.AddFlag2(4u);
	LiveSeamNpc.SetAttackExtents(FVector(7.f, 8.f, 9.f));
	LiveSeamNpc.SetAttackExtentsForSequence(1);
	TestTrue(TEXT("J2b: live bbox seam leaves extents intact; no arena proof"),
		LiveSeamNpc.GetAttackExtents().Equals(FVector(7.f, 8.f, 9.f)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSequenceExtentsRadial,
	"Elysium.Arm.NpcAttackExtents.RadialAndExcess", Flags)
bool FSequenceExtentsRadial::RunTest(const FString&)
{
	FBoundsNpc BoundsNpc;
	BoundsNpc.HullKind = 0;
	BoundsNpc.AddFlag2(4u);
	FVector TestCollisionMin, TestCollisionMax;
	FElysiumNpcBase::RetailCollisionExtents(BoundsNpc, TestCollisionMin, TestCollisionMax);
	BoundsNpc.SetAttackExtentsForSequence(1);
	const FVector ExpectedMargin(FMath::Max(50.0 - TestCollisionMax.X * ElysiumMove::U, 0.0),
		FMath::Max(50.0 - TestCollisionMax.Y * ElysiumMove::U, 0.0),
		FMath::Max(100.0 - TestCollisionMax.Z * ElysiumMove::U, 0.0));
	TestTrue(TEXT("0x10090c80: max(abs(min),max), 30/40 -> radial 50, then collision excess"),
		BoundsNpc.GetAttackExtents().Equals(ExpectedMargin, 1.e-4));
	BoundsNpc.TestBoundsMin = FVector(-1.f, -1.f, -1.f);
	BoundsNpc.TestBoundsMax = FVector(1.f, 1.f, 1.f);
	BoundsNpc.SetAttackExtentsForSequence(2);
	TestTrue(TEXT("0x10090c80: bounds inside collision maxs floor all excess to zero"),
		BoundsNpc.GetAttackExtents().IsNearlyZero());
	BoundsNpc.TestBoundsMin = FVector(-3.f, -4.f, -5.f);
	BoundsNpc.TestBoundsMax = FVector(90.f, 120.f, 250.f);
	BoundsNpc.SetAttackExtentsForSequence(3);
	TestTrue(TEXT("0x10090c80: larger positive maxs win before radialization (90/120 -> 150)"),
		FMath::IsNearlyEqual(BoundsNpc.GetAttackExtents().X,
			FMath::Max(150.0 - TestCollisionMax.X * ElysiumMove::U, 0.0)));
	return true;
}
}
#endif
