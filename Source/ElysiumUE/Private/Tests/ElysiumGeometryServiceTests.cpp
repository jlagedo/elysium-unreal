// The geometry seam's witnesses on the SHIPPED tutorial (0018 story 6, lane D).
//
// The synthetic tests prove the mask recipe on boxes. These ask `ElysiumWorldGeometry::Trace` about
// the map's own brushes: one convex of each kind the story named, chosen from the payload by
// signature (first by index, stable), a ray through its centroid on the axis that first meets THAT
// element, and one assertion per retail mask. A stub `Trace` (clear on everything) fails the own-mask
// search in `FindRay`, so it fails loudly here rather than skipping.
//
// Editor-only, like `Elysium.Content.MapCollision.Tutorial`, whose setup they reuse: the payload is
// authored through `AElysiumWorldCollisionActor::AuthorFromPayload`.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Tests/ElysiumGeometryFixture.h"

static constexpr EAutomationTestFlags GElysiumGeometryServiceFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	struct FGeometryMaskExpectation
	{
		int32 Mask;
		bool bBlocked;
	};

	FElysiumRetailTrace GeometryLine(const FVector& StartCm, const FVector& EndCm, int32 Mask)
	{
		FElysiumRetailTrace Request;
		Request.StartCm = StartCm;
		Request.EndCm = EndCm;
		Request.RetailMask = Mask;
		return Request;
	}

	bool GeometryLineBlocked(FElysiumGeometryFixture& Fixture, const FVector& StartCm,
		const FVector& EndCm, int32 Mask)
	{
		return Fixture.Trace(GeometryLine(StartCm, EndCm, Mask)).Fraction < 1.f;
	}

	FString GeometryVectorText(const FVector& Cm)
	{
		return FString::Printf(TEXT("(%.1f %.1f %.1f)"), Cm.X, Cm.Y, Cm.Z);
	}

	// The whole per-element witness: choose the convex and ray, then assert every mask.
	bool RunGeometryElement(FAutomationTestBase& Test, const TCHAR* Label, uint8 Signature,
		TConstArrayView<FGeometryMaskExpectation> Expected)
	{
		FElysiumGeometryFixture Fixture;
		if (!Fixture.Init(Test))
		{
			return false;
		}
		if (!Test.TestTrue(*FString::Printf(TEXT("%s: the payload carries convexes of the signature"),
			Label), Fixture.Convexes(Signature).Num() > 0))
		{
			return false;
		}

		// The mask this element blocks first names the ray: it must be the first thing the ray meets.
		int32 OwnMask = 0;
		for (const FGeometryMaskExpectation& Row : Expected)
		{
			if (Row.bBlocked)
			{
				OwnMask = Row.Mask;
				break;
			}
		}
		FElysiumGeometryRay Ray;
		const bool bFound = Fixture.FindRay(Signature, OwnMask,
			[&Fixture, Expected](const FVector& StartCm, const FVector& EndCm)
			{
				for (const FGeometryMaskExpectation& Row : Expected)
				{
					if (GeometryLineBlocked(Fixture, StartCm, EndCm, Row.Mask) != Row.bBlocked)
					{
						return false;
					}
				}
				return true;
			},
			Ray);
		if (!Test.TestTrue(*FString::Printf(TEXT(
			"%s: a ray through a convex's centroid meets THAT convex first under 0x%x (a stub Trace "
			"answers clear everywhere and fails here)"), Label, OwnMask), bFound))
		{
			return false;
		}
		const FElysiumGeometryConvex& Chosen = Fixture.Convexes(Signature)[Ray.Index];
		Test.AddInfo(FString::Printf(TEXT("%s: signature 0x%x index %d centroid %s extent %s, ray %s -> %s"),
			Label, Signature, Chosen.Index, *GeometryVectorText(Chosen.CentroidCm),
			*GeometryVectorText(Chosen.BoundsCm.GetExtent()), *GeometryVectorText(Ray.StartCm),
			*GeometryVectorText(Ray.EndCm)));

		for (const FGeometryMaskExpectation& Row : Expected)
		{
			const FElysiumRetailTraceResult Result =
				Fixture.Trace(GeometryLine(Ray.StartCm, Ray.EndCm, Row.Mask));
			Test.TestTrue(*FString::Printf(TEXT("%s: a collision world answers 0x%x"), Label, Row.Mask),
				Fixture.bLastAnswered);
			const bool bBlocked = Result.Fraction < 1.f;
			Test.TestEqual(*FString::Printf(TEXT("%s: mask 0x%x is %s (fraction %.3f, hit signature %d "
				"element %d)"), Label, Row.Mask, Row.bBlocked ? TEXT("blocked") : TEXT("clear"),
				Result.Fraction, Result.HitSignature, Result.ElementIndex), bBlocked, Row.bBlocked);
			if (Row.bBlocked && Row.Mask == OwnMask)
			{
				Test.TestEqual(*FString::Printf(TEXT("%s: the block is the element's own signature"),
					Label), Result.HitSignature, static_cast<int32>(Signature));
				Test.TestEqual(*FString::Printf(TEXT("%s: the block is the chosen convex"), Label),
					Result.ElementIndex, Chosen.Index);
			}
		}

		// The recording double forwards into the same geometry, so a kernel body pointed at it reads
		// what a direct trace reads.
		FElysiumRetailTraceResult ViaServices;
		const bool bViaAnswered =
			Fixture.Services.TraceRetail(GeometryLine(Ray.StartCm, Ray.EndCm, OwnMask), ViaServices);
		const FElysiumRetailTraceResult Direct =
			Fixture.Trace(GeometryLine(Ray.StartCm, Ray.EndCm, OwnMask));
		Test.TestTrue(*FString::Printf(TEXT("%s: the recording services forward to the geometry"), Label),
			bViaAnswered && FMath::IsNearlyEqual(ViaServices.Fraction, Direct.Fraction)
				&& ViaServices.HitSignature == Direct.HitSignature);
		return true;
	}
}

// --S-: stops a sight trace and neither pawn.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumGeometryTutorialSightOnlyTest,
	"Elysium.Content.Geometry.Tutorial.SightOnly", GElysiumGeometryServiceFlags)
bool FElysiumGeometryTutorialSightOnlyTest::RunTest(const FString&)
{
	static const FGeometryMaskExpectation Expected[] =
	{
		{ ElysiumGeometryMasks::Sight, true },
		{ ElysiumGeometryMasks::Npc, false },
		{ ElysiumGeometryMasks::Player, false },
	};
	return RunGeometryElement(*this, TEXT("sight-only --S-"), ElysiumGeometrySignatures::SightOnly,
		Expected);
}

// PN--: a window or grate stops both pawns and lets sight through.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumGeometryTutorialWindowTest,
	"Elysium.Content.Geometry.Tutorial.Window", GElysiumGeometryServiceFlags)
bool FElysiumGeometryTutorialWindowTest::RunTest(const FString&)
{
	static const FGeometryMaskExpectation Expected[] =
	{
		{ ElysiumGeometryMasks::Sight, false },
		{ ElysiumGeometryMasks::Npc, true },
		{ ElysiumGeometryMasks::Player, true },
	};
	return RunGeometryElement(*this, TEXT("window PN--"), ElysiumGeometrySignatures::Window, Expected);
}

// -N--: an NPC-only clip stops an NPC and neither the player nor sight.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumGeometryTutorialNpcClipTest,
	"Elysium.Content.Geometry.Tutorial.NpcClip", GElysiumGeometryServiceFlags)
bool FElysiumGeometryTutorialNpcClipTest::RunTest(const FString&)
{
	static const FGeometryMaskExpectation Expected[] =
	{
		{ ElysiumGeometryMasks::Sight, false },
		{ ElysiumGeometryMasks::Npc, true },
		{ ElysiumGeometryMasks::Player, false },
	};
	return RunGeometryElement(*this, TEXT("NPC clip -N--"), ElysiumGeometrySignatures::NpcClip,
		Expected);
}

// A human box at the centroid of a solid brush starts inside it (`IsAreaClear 0x102a0fb0`'s shape:
// start == end, a box, `MASK_NPCSOLID`).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumGeometryTutorialEmbeddedHullTest,
	"Elysium.Content.Geometry.Tutorial.EmbeddedHull", GElysiumGeometryServiceFlags)
bool FElysiumGeometryTutorialEmbeddedHullTest::RunTest(const FString&)
{
	FElysiumGeometryFixture Fixture;
	if (!Fixture.Init(*this))
	{
		return false;
	}
	const ElysiumRetailHulls::FRow* Row = ElysiumRetailHulls::Find(0);
	if (!TestNotNull(TEXT("the hull table has row 0"), Row)
		|| !TestEqual(TEXT("row 0 is the human hull"), FString(Row->Name), FString(TEXT("HUMAN_HULL"))))
	{
		return false;
	}
	const TArray<FElysiumGeometryConvex>& Solids =
		Fixture.Convexes(ElysiumGeometrySignatures::SolidToAll);
	if (!TestTrue(TEXT("the payload carries PNS- convexes"), Solids.Num() > 0))
	{
		return false;
	}
	const FElysiumGeometryConvex& Chosen = Solids[0];
	AddInfo(FString::Printf(TEXT("embedded hull: PNS- index %d centroid %s"), Chosen.Index,
		*GeometryVectorText(Chosen.CentroidCm)));

	FElysiumRetailTrace Request;
	Request.StartCm = Chosen.CentroidCm;
	Request.EndCm = Chosen.CentroidCm;
	Request.RetailMask = ElysiumGeometryMasks::NpcSolid;
	FElysiumGeometryFixture::HumanHullCm(Request.MinsCm, Request.MaxsCm);
	const FElysiumRetailTraceResult Result = Fixture.Trace(Request);
	TestTrue(TEXT("a collision world answers the overlap"), Fixture.bLastAnswered);
	TestTrue(TEXT("the human box embedded in a PNS- brush starts solid"), Result.bStartSolid);

	// The same box far above the map is not solid: the answer is the geometry's, not a blanket
	// "always embedded".
	FElysiumRetailTrace Away = Request;
	const FVector Lift(0.0, 0.0, 100000.0);
	Away.StartCm += Lift;
	Away.EndCm += Lift;
	TestFalse(TEXT("the same box 1 km above the map does not start solid"),
		Fixture.Trace(Away).bStartSolid);
	return true;
}

// The ledge: a downward foot-box sweep from just above a floor passes down onto it, and the same
// sweep from beside the floor's edge, over open space, finds nothing.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumGeometryTutorialLedgeTest,
	"Elysium.Content.Geometry.Tutorial.Ledge", GElysiumGeometryServiceFlags)
bool FElysiumGeometryTutorialLedgeTest::RunTest(const FString&)
{
	FElysiumGeometryFixture Fixture;
	if (!Fixture.Init(*this))
	{
		return false;
	}
	FVector HullMins;
	FVector HullMaxs;
	FElysiumGeometryFixture::HumanHullCm(HullMins, HullMaxs);
	// The foot-box: the human footprint, one Source unit thick. The sweep only has to find the floor
	// under the feet, and a full-height box would meet every ceiling and wall the room has.
	const FVector FootMins(HullMins.X, HullMins.Y, 0.0);
	const FVector FootMaxs(HullMaxs.X, HullMaxs.Y, ElysiumMove::U);
	constexpr double Above = 0.1;
	constexpr double Drop = 10.0;

	const TArray<FElysiumGeometryConvex>& Solids =
		Fixture.Convexes(ElysiumGeometrySignatures::SolidToAll);
	if (!TestTrue(TEXT("the payload carries PNS- convexes"), Solids.Num() > 0))
	{
		return false;
	}

	// The floor: the first PNS- convex (by index) with a horizontal top face at least 2 m square.
	const FElysiumGeometryConvex* Floor = nullptr;
	for (const FElysiumGeometryConvex& Candidate : Solids)
	{
		if (Candidate.TopVertexCount >= 3 && Candidate.TopHalfCm.X >= 100.0
			&& Candidate.TopHalfCm.Y >= 100.0)
		{
			Floor = &Candidate;
			break;
		}
	}
	if (!TestNotNull(TEXT("the payload carries a PNS- convex with a large horizontal top"), Floor))
	{
		return false;
	}
	const FVector FaceCentreCm = Floor->TopCentreCm;
	AddInfo(FString::Printf(TEXT("ledge: PNS- index %d top %s half %.1f x %.1f"), Floor->Index,
		*GeometryVectorText(FaceCentreCm), Floor->TopHalfCm.X, Floor->TopHalfCm.Y));

	// Passes: from 0.1 above the top's centre, swept down, the feet meet the floor.
	{
		FElysiumRetailTrace Down;
		Down.StartCm = FaceCentreCm + FVector(0.0, 0.0, Above);
		Down.EndCm = Down.StartCm - FVector(0.0, 0.0, Drop);
		Down.MinsCm = FootMins;
		Down.MaxsCm = FootMaxs;
		Down.RetailMask = ElysiumGeometryMasks::NpcSolid;
		const FElysiumRetailTraceResult Result = Fixture.Trace(Down);
		TestTrue(TEXT("a collision world answers the sweep"), Fixture.bLastAnswered);
		TestTrue(*FString::Printf(TEXT("the foot-box sweep from 0.1 above the floor top meets it "
			"(fraction %.3f)"), Result.Fraction), Result.Fraction != 1.f);
	}

	// Misses: past the floor's edge, where the payload itself holds nothing that stops an NPC across
	// the swept column. The point is chosen from the payload's boxes, never from a trace.
	const FVector Directions[] = { FVector(1, 0, 0), FVector(-1, 0, 0), FVector(0, 1, 0),
		FVector(0, -1, 0) };
	const double Clear = 5.0;
	bool bMissTested = false;
	for (const FElysiumGeometryConvex& Candidate : Solids)
	{
		const FVector Centre = Candidate.TopCentreCm;
		const FVector2D Half = Candidate.TopHalfCm;
		if (Candidate.TopVertexCount < 3 || Half.X < 100.0 || Half.Y < 100.0)
		{
			continue;
		}
		for (const FVector& Direction : Directions)
		{
			const double Reach = (Direction.X != 0.0 ? Half.X : Half.Y) + FootMaxs.X + Clear;
			const FVector Point = Centre + Direction * Reach;
			const FBox Column(FVector(Point.X - FootMaxs.X, Point.Y - FootMaxs.Y, Centre.Z - Drop - 1.0),
				FVector(Point.X + FootMaxs.X, Point.Y + FootMaxs.Y, Centre.Z + Above + FootMaxs.Z + 1.0));
			bool bOpen = true;
			for (const FBox& Blocker : Fixture.NpcBlockerBoxes)
			{
				if (Blocker.Intersect(Column))
				{
					bOpen = false;
					break;
				}
			}
			if (!bOpen)
			{
				continue;
			}
			FElysiumRetailTrace Down;
			Down.StartCm = FVector(Point.X, Point.Y, Centre.Z + Above);
			Down.EndCm = Down.StartCm - FVector(0.0, 0.0, Drop);
			Down.MinsCm = FootMins;
			Down.MaxsCm = FootMaxs;
			Down.RetailMask = ElysiumGeometryMasks::NpcSolid;
			const FElysiumRetailTraceResult Result = Fixture.Trace(Down);
			AddInfo(FString::Printf(TEXT("ledge: open column past PNS- index %d at %s"), Candidate.Index,
				*GeometryVectorText(Point)));
			TestTrue(TEXT("a collision world answers the open-space sweep"), Fixture.bLastAnswered);
			TestTrue(*FString::Printf(TEXT("the foot-box sweep past the floor's edge finds nothing "
				"(fraction %.3f)"), Result.Fraction), Result.Fraction == 1.f);
			bMissTested = true;
			break;
		}
		if (bMissTested)
		{
			break;
		}
	}
	TestTrue(TEXT("the payload has open space beside a large PNS- top to sweep over"), bMissTested);
	return true;
}

#endif
