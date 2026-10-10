#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumPhysicsCollision.h"

namespace ElysiumPhysicsBoxTest
{
	struct FPhysicsBoxGolden
	{
		const TCHAR* Name;
		FVector3f Mins;
		FVector3f Maxs;
		const ANSICHAR* Hex;
	};
	float RetailNaN()
	{
		const uint32 Bits = 0x7fffffff; // MSVC x86 atof("nan") in the retail probe.
		float Value; FMemory::Memcpy(&Value, &Bits, sizeof(Value)); return Value;
	}
	const float NaN = RetailNaN();
#include "ElysiumPhysicsBoxGolden.inl"
	uint8 HexNibble(ANSICHAR Character)
	{
		return Character <= '9' ? uint8(Character - '0') : uint8(Character - 'a' + 10);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPhysicsBoxConstructionTest,
	"Elysium.Substrate.PhysicsBoxConstruction",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FElysiumPhysicsBoxConstructionTest::RunTest(const FString&)
{
	using namespace ElysiumPhysicsBoxTest;
	FElysiumPhysicsCollision Service;
	// Bootstrap via an ordinary request so the internal seed can later be retrieved and freed.
	FElysiumPhysicsCollide* Ordinary = Service.BBoxToCollide(FVector3f(-3.f, 2.f, 7.f), FVector3f(19.f, 34.f, 55.f));
	if (!TestNotNull(TEXT("public service constructs actual geometry"), Ordinary)) return false;
	TArray<FElysiumPhysicsCollide*> Allocations;
	Allocations.Add(Ordinary);
	for (const FPhysicsBoxGolden& Golden : PhysicsBoxGoldens)
	{
		FElysiumPhysicsCollide* Geometry = Service.BBoxToCollide(Golden.Mins, Golden.Maxs);
		if (!TestNotNull(Golden.Name, Geometry)) continue;
		Allocations.AddUnique(Geometry);
		const TConstArrayView<uint8> Surface = Geometry->CompactSurface();
		if (!TestEqual(FString(Golden.Name) + TEXT(" complete compact surface size"), Surface.Num(), 412)) continue;
		int32 FirstMismatch = INDEX_NONE;
		for (int32 Offset = 0; Offset < Surface.Num(); ++Offset)
		{
			const uint8 Expected = uint8((HexNibble(Golden.Hex[Offset * 2]) << 4) | HexNibble(Golden.Hex[Offset * 2 + 1]));
			if (Surface[Offset] != Expected) { FirstMismatch = Offset; break; }
		}
		TestEqual(FString(Golden.Name) + TEXT(" all 412 retail bytes; first mismatching offset"), FirstMismatch, INDEX_NONE);
		// These are actual BBoxToCollide input arms, not injected body/query/cache state.
		if (!FMath::IsNaN(Golden.Mins.X))
			TestTrue(FString(Golden.Name) + TEXT(" reverse lookup returns same allocation"),
				Service.BBoxToCollide(Golden.Mins, Golden.Maxs) == Geometry);
	}
	TestNull(TEXT("only all-three-axis point degeneracy rejects"),
		Service.BBoxToCollide(FVector3f(1.f, 2.f, 3.f), FVector3f(1.f, 2.f, 3.f)));
	FElysiumPhysicsCollide* NaNFirst = Service.BBoxToCollide(FVector3f(NaN, 2.f, 7.f), FVector3f(19.f, 34.f, 55.f));
	FElysiumPhysicsCollide* NaNSecond = Service.BBoxToCollide(FVector3f(NaN, 2.f, 7.f), FVector3f(19.f, 34.f, 55.f));
	Allocations.Add(NaNFirst); Allocations.Add(NaNSecond);
	TestTrue(TEXT("NaN never hits the exact component cache"), NaNFirst != NaNSecond);
	FElysiumPhysicsCollide* Zero = Service.BBoxToCollide(FVector3f(0.f), FVector3f(1.f));
	Allocations.Add(Zero);
	TestTrue(TEXT("negative zero matches positive zero"), Service.BBoxToCollide(FVector3f(-0.f), FVector3f(1.f)) == Zero);
	for (FElysiumPhysicsCollide* Geometry : Allocations) FElysiumPhysicsCollision::DestroyCollide(Geometry);
	// Preserve retail's stale cache identity without dereferencing freed storage.
	TestTrue(TEXT("DestroyCollide does not invalidate cached allocation identity"),
		Service.BBoxToCollide(FVector3f(-3.f, 2.f, 7.f), FVector3f(19.f, 34.f, 55.f)) == Ordinary);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPhysicsBoxLifetimeTest,
	"Elysium.Substrate.PhysicsBoxLifetime",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FElysiumPhysicsBoxLifetimeTest::RunTest(const FString&)
{
	FElysiumPhysicsCollide* Geometry;
	FElysiumPhysicsCollide* Seed;
	{
		FElysiumPhysicsCollision Service;
		Geometry = Service.BBoxToCollide(FVector3f(-3.f, 2.f, 7.f), FVector3f(19.f, 34.f, 55.f));
		Seed = Service.BBoxToCollide(FVector3f(-16.f, -16.f, 0.f), FVector3f(16.f, 16.f, 72.f));
	}
	// Service cache teardown borrows geometry; allocation lifetime belongs to DestroyCollide.
	TestEqual(TEXT("real surface survives service cache destruction"), Geometry->CompactSurface().Num(), 412);
	TestEqual(TEXT("retail first packed triangle survives"), Geometry->TriangleWord(0, 0), uint32(0xb000));
	TestEqual(TEXT("seed keeps real eighth vertex"), Seed->Vertex(7), FVector3f(16.f * 0.0254f, -0.f, -16.f * 0.0254f));
	FElysiumPhysicsCollision::DestroyCollide(Geometry);
	FElysiumPhysicsCollision::DestroyCollide(Seed);
	return true;
}

#endif
