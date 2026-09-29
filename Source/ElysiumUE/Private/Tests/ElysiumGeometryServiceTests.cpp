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

#include "Substrate/ElysiumNpc.h"
#include "Tests/ElysiumGeometryFixture.h"
#include "Tests/ElysiumNpcTestFixture.h"

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
	// The first PNS- convex (by index) with a horizontal top face at least 2 m square: a floor.
	const FElysiumGeometryConvex* FindLargeFloor(const FElysiumGeometryFixture& Fixture)
	{
		for (const FElysiumGeometryConvex& Candidate : Fixture.Convexes(ElysiumGeometrySignatures::SolidToAll))
		{
			if (Candidate.TopVertexCount >= 3 && Candidate.TopHalfCm.X >= 100.0
				&& Candidate.TopHalfCm.Y >= 100.0)
			{
				return &Candidate;
			}
		}
		return nullptr;
	}

	// The first large PNS- floor top (by index) whose HUMAN_HULL volume standing on it is clear of
	// every other NPC-blocking box: nothing but the floor itself intersects the column from 0.5 cm
	// above the top (so a coplanar neighbour slab is not a blocker) up the full hull height.
	const FElysiumGeometryConvex* FindClearFloor(const FElysiumGeometryFixture& Fixture)
	{
		FVector HullMins;
		FVector HullMaxs;
		FElysiumGeometryFixture::HumanHullCm(HullMins, HullMaxs);
		for (const FElysiumGeometryConvex& Candidate : Fixture.Convexes(ElysiumGeometrySignatures::SolidToAll))
		{
			if (Candidate.TopVertexCount < 3 || Candidate.TopHalfCm.X < 100.0
				|| Candidate.TopHalfCm.Y < 100.0)
			{
				continue;
			}
			const FVector Top = Candidate.TopCentreCm;
			const FBox Volume(FVector(Top.X + HullMins.X, Top.Y + HullMins.Y, Top.Z + 0.5),
				FVector(Top.X + HullMaxs.X, Top.Y + HullMaxs.Y, Top.Z + HullMaxs.Z + 1.0));
			bool bClear = true;
			for (const FBox& Blocker : Fixture.NpcBlockerBoxes)
			{
				if (!Blocker.Equals(Candidate.BoundsCm, 0.01) && Blocker.Intersect(Volume))
				{
					bClear = false;
					break;
				}
			}
			if (bClear)
			{
				return &Candidate;
			}
		}
		return nullptr;
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

// Contact: a human box resting ON a floor top is not start-solid, and one sunk into it is. Source's
// hull trace stops `DIST_EPSILON` short of a surface it touches, so retail's "the box stands here"
// probes (`IsAreaClear 0x102a0fb0`: start == end, `MASK_NPCSOLID`) read a resting box as clear; the
// world lane's contact tolerance is what this pins, from both sides.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumGeometryTutorialContactTest,
	"Elysium.Content.Geometry.Tutorial.Contact", GElysiumGeometryServiceFlags)
bool FElysiumGeometryTutorialContactTest::RunTest(const FString&)
{
	FElysiumGeometryFixture Fixture;
	if (!Fixture.Init(*this))
	{
		return false;
	}
	const FElysiumGeometryConvex* Floor = FindClearFloor(Fixture);
	if (!TestNotNull(TEXT("the payload carries a large PNS- floor top with a clear human-hull volume above it"),
		Floor))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("contact: PNS- index %d top %s"), Floor->Index,
		*GeometryVectorText(Floor->TopCentreCm)));

	FElysiumRetailTrace Request;
	Request.RetailMask = ElysiumGeometryMasks::NpcSolid;
	FElysiumGeometryFixture::HumanHullCm(Request.MinsCm, Request.MaxsCm);   // HUMAN_HULL, full row
	const double U = ElysiumMove::U;
	struct FContactRow
	{
		const TCHAR* Label;
		double LiftUnits;
		bool bStartSolid;
	};
	const FContactRow Rows[] =
	{
		{ TEXT("bottom exactly on the floor top (+0)"), 0.0, false },
		{ TEXT("bottom 1/32 Source unit above the floor top"), 1.0 / 32.0, false },
		{ TEXT("box sunk 1 Source unit into the floor"), -1.0, true },
		{ TEXT("control: box 8 Source units above the floor top"), 8.0, false },
	};
	for (const FContactRow& Row : Rows)
	{
		Request.StartCm = Floor->TopCentreCm + FVector(0.0, 0.0, Row.LiftUnits * U);
		Request.EndCm = Request.StartCm;
		const FElysiumRetailTraceResult Result = Fixture.Trace(Request);
		TestTrue(*FString::Printf(TEXT("a collision world answers: %s"), Row.Label), Fixture.bLastAnswered);
		TestEqual(*FString::Printf(TEXT("human box %s is %sstart-solid"), Row.Label,
			Row.bStartSolid ? TEXT("") : TEXT("NOT ")), Result.bStartSolid, Row.bStartSolid);
		if (Row.LiftUnits >= 8.0)
		{
			TestTrue(TEXT("the control overlap reads fraction 1"), Result.Fraction >= 1.f);
		}
	}
	return true;
}

// The ledge, asked of the KERNEL on real geometry: `MoveProbeCheckStandPosition`
// (`CAI_MoveProbe::CheckStandPosition 0x102e7270`: the spot lifted 0.1, dropped slot 523 -- 36 Source
// units on the Troika line -- under the 0.75/0.25 foot box, `MASK_NPCSOLID`) over the tutorial's
// payload. A kernel NPC stands in its own headless entity world; its recording services' `TraceRetail`
// forwards into the fixture, so the probe's one hull trace meets the shipped brushes. Over a large
// PNS- floor top the probe finds ground; past the floor's edge over open space it does not.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumGeometryTutorialLedgeTest,
	"Elysium.Content.Geometry.Tutorial.Ledge", GElysiumGeometryServiceFlags)
bool FElysiumGeometryTutorialLedgeTest::RunTest(const FString&)
{
	FElysiumGeometryFixture Fixture;
	if (!Fixture.Init(*this))
	{
		return false;
	}
	FElysiumNpcWorldBuilder Builder(TEXT("geometry_ledge_stand"), 6001);
	Builder.AddNpc(TEXT("guard"));
	FElysiumNpcWorldFixture Npcs(MoveTemp(Builder));
	FElysiumNpc* Guard = Npcs.Npc(TEXT("guard"));
	if (!TestNotNull(TEXT("the kernel NPC"), Guard))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });
	// Both fixtures live on this stack frame and the knob is cleared before it unwinds, so the
	// reference capture cannot dangle.
	Npcs.Services.TraceRetailQuery = [&Fixture](const FElysiumRetailTrace& Request,
		FElysiumRetailTraceResult& Out)
	{
		Out = Fixture.Trace(Request);
		return Fixture.bLastAnswered;
	};

	const FElysiumGeometryConvex* Floor = FindLargeFloor(Fixture);
	if (!TestNotNull(TEXT("the payload carries a PNS- convex with a large horizontal top"), Floor))
	{
		Npcs.Services.TraceRetailQuery = nullptr;
		return false;
	}
	const double U = ElysiumMove::U;
	AddInfo(FString::Printf(TEXT("ledge: PNS- index %d top %s half %.1f x %.1f"), Floor->Index,
		*GeometryVectorText(Floor->TopCentreCm), Floor->TopHalfCm.X, Floor->TopHalfCm.Y));

	// Stands: the feet on the floor top's centre.
	TestTrue(TEXT("the stand probe finds ground on a PNS- floor top"),
		Guard->MoveProbeCheckStandPosition(Floor->TopCentreCm / U, ElysiumGeometryMasks::NpcSolid,
			nullptr, nullptr));
	TestTrue(TEXT("and it asked the geometry through the forwarder"),
		Npcs.Services.Saw(TEXT("TraceRetail")));

	// Falls: past the floor's edge where the payload holds nothing that stops an NPC across the probe's
	// column (0.1 above the spot down the slot-523 drop, 36 Source units). The point is chosen from
	// the payload's boxes, never from a trace; the footprint used is wider than the foot box.
	const double Above = 0.1 * U;
	const double Drop = 36.0 * U;
	const double Footprint = 13.0 * U;
	const FVector Directions[] = { FVector(1, 0, 0), FVector(-1, 0, 0), FVector(0, 1, 0), FVector(0, -1, 0) };
	bool bFallTested = false;
	for (const FElysiumGeometryConvex& Candidate : Fixture.Convexes(ElysiumGeometrySignatures::SolidToAll))
	{
		if (Candidate.TopVertexCount < 3 || Candidate.TopHalfCm.X < 100.0 || Candidate.TopHalfCm.Y < 100.0)
		{
			continue;
		}
		for (const FVector& Direction : Directions)
		{
			const double Reach = (Direction.X != 0.0 ? Candidate.TopHalfCm.X : Candidate.TopHalfCm.Y)
				+ Footprint + 5.0;
			const FVector Point = Candidate.TopCentreCm + Direction * Reach;
			const FBox Column(FVector(Point.X - Footprint, Point.Y - Footprint, Point.Z - Drop - 1.0),
				FVector(Point.X + Footprint, Point.Y + Footprint, Point.Z + Above + 1.0));
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
			AddInfo(FString::Printf(TEXT("ledge: open column past PNS- index %d at %s"), Candidate.Index,
				*GeometryVectorText(Point)));
			TestFalse(TEXT("the stand probe finds no ground past the floor's edge over open space"),
				Guard->MoveProbeCheckStandPosition(Point / U, ElysiumGeometryMasks::NpcSolid, nullptr,
					nullptr));
			bFallTested = true;
			break;
		}
		if (bFallTested)
		{
			break;
		}
	}
	TestTrue(TEXT("the payload has open space beside a large PNS- top to probe over"), bFallTested);
	Npcs.Services.TraceRetailQuery = nullptr;
	return true;
}

#endif
