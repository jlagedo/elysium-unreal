// `AutoPositionFromTarget` and the origin selector's third arm (SC7).
//
// The one shot-record field the reader has parsed since it landed and no solver read. It is applied
// by the mode-1 think on `flags & 0x20`, disassembly `0x1006fa50`-`0x1006fb85`, and it moves the
// camera along its own axis until the lower of the two target points is framed:
//
//     if (P2.z > P1.z) swap(P1, P2)                       // P1 ends HIGH, P2 ends LOW
//     C = ClosestPointOnLine(P2, lookAt, camOrigin)       // FUN_1013ca00 -> FUN_1013c940
//     A = FieldOfView * 0.5 * DEG2RAD
//     d = |C - P2| ; h = d / sin(A) ; r = sqrt(h*h + d*d)
//     camOrigin = lookAt - normalize(lookAt - camOrigin) * r
//
// Two things about it are easy to get wrong and both are asserted here. The projection is an
// **infinite line** (RC1: `FUN_1013c940` has exactly one branch, the `1e-05f` degenerate guard, and
// no `FCOM` against `0.0` or `1.0`), so a target point that projects behind the camera or beyond the
// look-at still contributes its true perpendicular distance. And the fit is **not** `d / tan(A)`:
// retail takes the hypotenuse and then adds a second `d` in quadrature, so it always backs off
// further than an exact frame — a "correction" here would silently re-frame five shipped shots.
//
// Five shipped shots set the flag: `centerfullview`, `dialogmediumshot`, `mediumshot`,
// `dialoglowleft`, `dialoglowright`.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumCameraSolve.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "Player/ElysiumCameraShots.h"

#include "Misc/ScopeExit.h"
#include "Tests/ElysiumTestServices.h"

namespace ElysiumCameraAutoPositionTests
{
static constexpr EAutomationTestFlags GElysiumAutoPositionFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// The recovered radii for `d == 100` at the shipped `centerfullview` FOV and at a narrow one, worked
// out from the formula above by hand so the assertion is a number and not a re-run of the code:
//
//   A = 75 * 0.5 * 0.0174532924 = 0.654498465 rad, sin A = 0.60876142544
//   h = 100 / sin A = 164.26796413, r = sqrt(h^2 + 100^2) = 192.31215261
constexpr float RecoveredRadiusAt75 = 192.31215260746555f;
//   A = 40 * 0.5 * DEG2RAD = 0.349065848 rad, sin A = 0.34202014107
//   h = 292.38044194, r = 309.00861288
constexpr float RecoveredRadiusAt40 = 309.00861287508144f;
// The tight fit the formula is *not*: `d / tan(A)` at 75 degrees.
constexpr float TangentFitAt75 = 130.32253849782515f;

FElysiumEntityDefs MakeWorldDefs()
{
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__camera_autoposition_test__");
	FElysiumEntityDef World;
	World.Classname = TEXT("worldspawn");
	Defs.Defs.Add(MoveTemp(World));
	FElysiumEntityDef Subject;
	Subject.Classname = TEXT("npc_VPedestrian");
	Subject.TargetName = TEXT("subject");
	Defs.Defs.Add(MoveTemp(Subject));
	return Defs;
}

// `centerfullview.txt`'s shape, reduced to what this slice reads: an `End` anchor on the entity's
// origin and a `Target` block of `AbsMin` / `AbsMax`, which is what the shipped file authors.
const TCHAR* CenterFullViewText = TEXT(R"KV(
CameraShotTable
{
	CenterFullView
	{
		End
		{
			"Position"	"Named"
			"AttachPos"	"Origin"
			"AttachType"	"FollowEntAngles"
		}
		Target
		{
			Point1 { "Position" "Named"  "AttachPos" "AbsMin"  "AttachType" "Follow" }
			Point2 { "Position" "Named"  "AttachPos" "AbsMax"  "AttachType" "Follow" }
		}
		CameraConstraints
		{
			"FieldOfView"		"75"
			"AutoPositionFromTarget" "1"
		}
	}
}
)KV");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCameraAutoPositionTest,
	"Elysium.Substrate.CameraAutoPosition",
	ElysiumCameraAutoPositionTests::GElysiumAutoPositionFlags)

bool FElysiumCameraAutoPositionTest::RunTest(const FString&)
{
	using namespace ElysiumCameraAutoPositionTests;

	// A geometry chosen so every quantity is exact: the look-at sits on the world origin, the camera
	// 500 along +X, and the two target points 100 above and below the look-at. The lower point's
	// perpendicular distance to the camera axis is therefore exactly 100.
	const FVector LookAt(0.0f, 0.0f, 0.0f);
	const FVector CamOrigin(500.0f, 0.0f, 0.0f);
	const FVector High(0.0f, 0.0f, 100.0f);
	const FVector Low(0.0f, 0.0f, -100.0f);

	// --- the recovered radius, at the shipped FOV -------------------------------------------------
	{
		const FVector Moved = ElysiumCam::AutoPositionFromTarget(CamOrigin, LookAt, High, Low, 75.0f);
		TestTrue(TEXT("the camera stays on its own axis"),
			Moved.Equals(FVector(RecoveredRadiusAt75, 0.0f, 0.0f), 0.01f));
		TestEqual(TEXT("and sits at the recovered radius from the look-at"),
			static_cast<float>((Moved - LookAt).Size()), RecoveredRadiusAt75, 0.01f);

		// The swap is the only thing the higher point is for, so the two orders are one answer.
		const FVector Swapped = ElysiumCam::AutoPositionFromTarget(CamOrigin, LookAt, Low, High, 75.0f);
		TestTrue(TEXT("feeding the points in either order gives one answer"),
			Swapped.Equals(Moved, 0.001f));

		// A narrower lens has to back further off to fit the same subject.
		const FVector Narrow = ElysiumCam::AutoPositionFromTarget(CamOrigin, LookAt, High, Low, 40.0f);
		TestEqual(TEXT("a narrower FieldOfView backs the camera further off"),
			static_cast<float>((Narrow - LookAt).Size()), RecoveredRadiusAt40, 0.01f);
		TestTrue(TEXT("which is further than the 75 degree answer"),
			(Narrow - LookAt).Size() > (Moved - LookAt).Size());

		// **Not** the tight fit. 192.31 against 130.32 is 62 cm of framing, which no epsilon hides.
		TestTrue(TEXT("the result differs measurably from the d/tan(A) fit"),
			FMath::Abs(static_cast<float>((Moved - LookAt).Size()) - TangentFitAt75) > 50.0f);
	}

	// --- the projection is unclamped: an infinite line, not a segment ----------------------------
	{
		// (a) the low point projects **behind the camera** (`t > 1`). The midpoint of the two target
		// points is still the look-at, so only the projection changes.
		const FVector BehindLow(700.0f, 0.0f, -100.0f);
		const FVector BehindHigh(-700.0f, 0.0f, 100.0f);
		TestTrue(TEXT("the low point projects past the camera"),
			ElysiumCam::ClosestPointParameterOnLine(BehindLow, LookAt, CamOrigin) > 1.0f);
		const FVector Behind =
			ElysiumCam::AutoPositionFromTarget(CamOrigin, LookAt, BehindHigh, BehindLow, 75.0f);
		TestEqual(TEXT("its perpendicular distance is still 100, so the radius is unchanged"),
			static_cast<float>((Behind - LookAt).Size()), RecoveredRadiusAt75, 0.01f);

		// (b) the low point projects **beyond the look-at** (`t < 0`).
		const FVector BeyondLow(-700.0f, 0.0f, -100.0f);
		const FVector BeyondHigh(700.0f, 0.0f, 100.0f);
		TestTrue(TEXT("the low point projects behind the look-at"),
			ElysiumCam::ClosestPointParameterOnLine(BeyondLow, LookAt, CamOrigin) < 0.0f);
		const FVector Beyond =
			ElysiumCam::AutoPositionFromTarget(CamOrigin, LookAt, BeyondHigh, BeyondLow, 75.0f);
		TestEqual(TEXT("and the radius is unchanged there too"),
			static_cast<float>((Beyond - LookAt).Size()), RecoveredRadiusAt75, 0.01f);

		// A segment-clamped implementation would report 223.6 and 707.1 for those two distances and
		// therefore two much larger radii; this is the assertion that would catch it.
		TestTrue(TEXT("a clamped projection would have moved the camera"),
			FMath::Abs((Behind - LookAt).Size() - (Beyond - LookAt).Size()) < 0.01);

		// The degenerate guard: `len2 < 1e-05f` returns t = 0, so the closest point is the look-at.
		TestEqual(TEXT("a zero-length axis answers t = 0"),
			ElysiumCam::ClosestPointParameterOnLine(High, LookAt, LookAt), 0.0f);
		TestTrue(TEXT("so the closest point is the look-at itself"),
			ElysiumCam::ClosestPointOnLine(High, LookAt, LookAt).Equals(LookAt, 0.001f));
	}

	// --- through `Resolve`: the flag, and the origin selector's third arm ------------------------
	{
		ElysiumCameraShots::FlushCache();
		ON_SCOPE_EXIT { ElysiumCameraShots::FlushCache(); };

		FElysiumCameraShotDef Def;
		if (!TestTrue(TEXT("the centerfullview-shaped shot parses"),
			ElysiumCameraShots::ParseText(CenterFullViewText, Def)))
		{
			return false;
		}
		TestTrue(TEXT("AutoPositionFromTarget parses"), Def.Constraints.bAutoPositionFromTarget);

		// A body whose bounds are known, standing away from the world origin so the framing is a real
		// move rather than a no-op.
		FElysiumRecordingServices Services;
		Services.bHasPlayer = true;
		const FVector Origin(300.0f, 40.0f, 0.0f);
		const FBox Box(FVector(280.0f, 10.0f, 5.0f), FVector(340.0f, 70.0f, 185.0f));
		Services.UseBodyBounds = Box;
		Services.bHasUseBodyBounds = true;

		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MakeWorldDefs());
		FElysiumEntity* Subject = World.FindByName(TEXT("subject"));
		if (!TestNotNull(TEXT("the framed entity"), Subject))
		{
			return false;
		}
		Subject->Origin = Origin;

		// The same definition with the flag cleared is the control: it is what the anchors alone say.
		FElysiumCameraShotDef Unflagged = Def;
		Unflagged.Constraints.bAutoPositionFromTarget = false;

		FElysiumCameraShot Plain;
		FElysiumCameraShot Framed;
		if (TestTrue(TEXT("the unflagged shot resolves"),
				FElysiumCameraDirector::Resolve(&World, Unflagged, Subject->Handle, Plain))
			&& TestTrue(TEXT("and the flagged one does"),
				FElysiumCameraDirector::Resolve(&World, Def, Subject->Handle, Framed)))
		{
			TestTrue(TEXT("the flag clear leaves the origin exactly where the anchors put it"),
				Plain.Origin.Equals(Origin, 0.01f));
			TestTrue(TEXT("both aim at the same point — the solve moves the origin, never the look-at"),
				Framed.LookAt.Equals(Plain.LookAt, 0.01f));
			TestFalse(TEXT("and the flagged shot has moved off the anchor"),
				Framed.Origin.Equals(Plain.Origin, 1.0f));

			// It is exactly the kernel, applied to the anchors this resolve produced: `Point1` is the
			// `AbsMin` slot and `Point2` the `AbsMax` slot, so the swap picks the mins as the low point.
			const FVector Expected = ElysiumCam::AutoPositionFromTarget(Plain.Origin, Plain.LookAt,
				Box.Min, Box.Max, Def.Constraints.FieldOfView);
			TestTrue(TEXT("Resolve applies the recovered formula to the resolved anchors"),
				Framed.Origin.Equals(Expected, 0.01f));

			// The camera slides along the axis it already had, so the direction survives.
			const FVector Before = (Plain.Origin - Plain.LookAt).GetSafeNormal();
			const FVector After = (Framed.Origin - Framed.LookAt).GetSafeNormal();
			TestTrue(TEXT("and only along the axis the anchors chose"), Before.Equals(After, 0.001f));
		}

		// `+0x594 == 2`: the whole `if (sel != 2)` body is skipped, `AutoPositionFromTarget` with it.
		FElysiumCameraShot Suppressed;
		if (TestTrue(TEXT("the flagged shot resolves under the third origin arm"),
			FElysiumCameraDirector::Resolve(&World, Def, Subject->Handle, Suppressed, nullptr,
				EElysiumShotResolvePass::ShotStart, EElysiumShotOriginSelector::Entity)))
		{
			TestTrue(TEXT("the third origin-selector arm suppresses the solve"),
				Suppressed.Origin.Equals(Plain.Origin, 0.01f));
			TestTrue(TEXT("and the shot carries the selector it was resolved under"),
				Suppressed.OriginSelector == EElysiumShotOriginSelector::Entity);
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
