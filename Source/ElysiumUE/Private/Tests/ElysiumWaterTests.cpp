// R7.1 -- water volumes (rulings B/C/D). The stage emits the rows and the bake places one
// actor; the runtime's whole share is the point-in-brush classification `CheckWater` asks for, the
// tag contract the visuals bucket by, and the post-process state machine the renderer walks. Each
// is pinned here -- the classification content-free, the actor against a bare test world.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Audio/ElysiumWaterAudio.h"
#include "ElysiumBakedTags.h"
#include "ElysiumFog.h"
#include "ElysiumSurfaceParams.h"
#include "ElysiumSurfaceSettings.h"
#include "ElysiumWaterVolumes.h"

#include "Components/StaticMeshComponent.h"   // the runtime CPD stamp under test
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Tests/AutomationCommon.h"
#include "UObject/UnrealType.h"   // FindFProperty — the bake writes these names through reflection

namespace ElysiumWaterTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// One axis-aligned brush as the stage emits it: six outward half-spaces (`n·p − d <= 0` inside)
// plus the AABB its plane hull solved.
static FElysiumWaterBrush BoxBrush(const FBox& Box)
{
	FElysiumWaterBrush Brush;
	Brush.BoundsCm = Box;
	Brush.Planes = {
		FPlane(FVector(1.0, 0.0, 0.0), Box.Max.X), FPlane(FVector(-1.0, 0.0, 0.0), -Box.Min.X),
		FPlane(FVector(0.0, 1.0, 0.0), Box.Max.Y), FPlane(FVector(0.0, -1.0, 0.0), -Box.Min.Y),
		FPlane(FVector(0.0, 0.0, 1.0), Box.Max.Z), FPlane(FVector(0.0, 0.0, -1.0), -Box.Min.Z),
	};
	return Brush;
}

// The Santa Monica sewer basin, in miniature: a 2 m square pool with its surface at Z 0 and its
// floor 50 cm down, carrying `sewer_water`'s own fog tuple.
static FElysiumWaterVolume Basin()
{
	FElysiumWaterVolume Volume;
	Volume.Index = 0;
	Volume.SurfaceZCm = 0.f;
	Volume.MinZCm = -50.f;
	Volume.Material = TEXT("vtmb:material:water/sewer_water");
	Volume.bFogEnabled = true;
	Volume.FogColor = FLinearColor(5.f / 255.f, 5.f / 255.f, 0.f);
	Volume.FogStartCm = 2.54f;
	Volume.FogEndCm = 2600.96f;
	Volume.Brushes.Add(BoxBrush(FBox(FVector(-100.0, -100.0, -50.0), FVector(100.0, 100.0, 0.0))));
	return Volume;
}

// `CheckWater`'s three queries and the point-in-brush test under them, plus the contract the bake
// and the settings page share with this class. No actor, no world, no trace.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumWaterTest, "Elysium.Substrate.Water", GElysiumTestFlags)
bool FElysiumWaterTest::RunTest(const FString&)
{
	TArray<FElysiumWaterVolume> Volumes;
	Volumes.Add(Basin());

	// The feet sit one Source unit off the floor, exactly as `CheckWater` places them.
	const FVector Feet(0.0, 0.0, -48.0);
	const FVector Dry(0.0, 0.0, 40.0);

	// --- the three levels, in the order `CheckWater` asks them ---
	// The level is compared as the number it publishes -- `TestEqual` has no scoped-enum overload,
	// and that number is what the camera's push model and the movement channel carry anyway.
	{
		int32 Volume = INDEX_NONE;
		TestEqual(TEXT("feet in, waist and eyes above the plane is Feet"),
			static_cast<int32>(ElysiumWater::ClassifyBody(Volumes, Feet, Dry, Dry, &Volume)),
			static_cast<int32>(EElysiumWaterLevel::Feet));
		TestEqual(TEXT("and it names the volume the feet are in"), Volume, 0);

		TestEqual(TEXT("waist in, eyes above is Waist"),
			static_cast<int32>(ElysiumWater::ClassifyBody(Volumes, Feet,
				FVector(0.0, 0.0, -25.0), Dry, &Volume)),
			static_cast<int32>(EElysiumWaterLevel::Waist));
		TestEqual(TEXT("still the same volume"), Volume, 0);

		TestEqual(TEXT("eyes under the plane is Eyes"),
			static_cast<int32>(ElysiumWater::ClassifyBody(Volumes, Feet, FVector(0.0, 0.0, -25.0),
				FVector(0.0, 0.0, -5.0), &Volume)),
			static_cast<int32>(EElysiumWaterLevel::Eyes));
		TestEqual(TEXT("and still the same volume"), Volume, 0);
	}

	// --- dry feet end the query, whatever the rest of the body is doing ---
	{
		int32 Volume = 7;
		// Standing on the bank beside the pool.
		TestEqual(TEXT("a body beside the volume is dry"),
			static_cast<int32>(ElysiumWater::ClassifyBody(Volumes, FVector(500.0, 0.0, -48.0),
				FVector(500.0, 0.0, -25.0), Dry, &Volume)),
			static_cast<int32>(EElysiumWaterLevel::None));
		TestEqual(TEXT("and no volume is named"), Volume, INDEX_NONE);

		// Below the floor: a basement under the basin is not in the water above it. This is the
		// `minZ` half of the brush, which the planes carry rather than the row's own float.
		TestEqual(TEXT("a body below the brush's floor is dry"),
			static_cast<int32>(ElysiumWater::ClassifyBody(Volumes, FVector(0.0, 0.0, -60.0),
				FVector(0.0, 0.0, -55.0), FVector(0.0, 0.0, -52.0), nullptr)),
			static_cast<int32>(EElysiumWaterLevel::None));
	}

	// --- more than one volume: the query names which ---
	{
		FElysiumWaterVolume Canal = Basin();
		Canal.Index = 3;
		Canal.SurfaceZCm = -200.f;
		Canal.MinZCm = -400.f;
		Canal.Brushes.Reset();
		Canal.Brushes.Add(BoxBrush(FBox(FVector(400.0, -100.0, -400.0), FVector(600.0, 100.0, -200.0))));
		Volumes.Add(Canal);

		TestEqual(TEXT("a point in the first volume answers 0"),
			ElysiumWater::FindVolumeAt(Volumes, FVector(0.0, 0.0, -25.0)), 0);
		TestEqual(TEXT("a point in the second answers 1"),
			ElysiumWater::FindVolumeAt(Volumes, FVector(500.0, 0.0, -300.0)), 1);
		TestEqual(TEXT("and a point in neither answers INDEX_NONE"),
			ElysiumWater::FindVolumeAt(Volumes, FVector(300.0, 0.0, 0.0)), INDEX_NONE);
		Volumes.Pop();
	}

	// --- the planes decide, not the box: a sloped brush rejects its own bounding corner ---
	{
		FElysiumWaterVolume Sloped = Basin();
		// The same hull with one diagonal cut through the +X/+Z corner, which is what a sloped
		// canal bank stages as. The AABB is unchanged, so a bounds-only test would say yes.
		Sloped.Brushes[0].Planes.Add(FPlane(FVector(1.0, 0.0, 1.0).GetSafeNormal(), 0.0));
		const TArray<FElysiumWaterVolume> Cut = { Sloped };

		TestEqual(TEXT("a point past the cut is outside, though inside the bounds"),
			ElysiumWater::FindVolumeAt(Cut, FVector(90.0, 0.0, -5.0)), INDEX_NONE);
		TestEqual(TEXT("and a point on the deep side is still inside"),
			ElysiumWater::FindVolumeAt(Cut, FVector(-90.0, 0.0, -25.0)), 0);
	}

	// --- the contract the bake, the visuals and the settings page share ---
	{
		TestEqual(TEXT("elysium.water"), ElysiumBakedTags::Water, FName(TEXT("elysium.water")));

		// The level is what the mover and the animation intent already branch on, so its numbers
		// are the ones `CheckWater` publishes and the camera's `int32` state carries.
		TestEqual(TEXT("None is 0"), static_cast<int32>(EElysiumWaterLevel::None), 0);
		TestEqual(TEXT("Feet is 1"), static_cast<int32>(EElysiumWaterLevel::Feet), 1);
		TestEqual(TEXT("Waist is 2"), static_cast<int32>(EElysiumWaterLevel::Waist), 2);
		TestEqual(TEXT("Eyes is 3"), static_cast<int32>(EElysiumWaterLevel::Eyes), 3);

		// The one translation knob (§4.2): the value at which SLW's exponential extinction and
		// VtMB's linear fog agree at the half-fog distance.
		const UElysiumSurfaceSettings* Surfaces = GetDefault<UElysiumSurfaceSettings>();
		TestEqual(TEXT("WaterFogScale defaults to 2 ln 2"), Surfaces->WaterFogScale,
			2.0f * FMath::Loge(2.0f), 1e-6f);
		bool bBound = false;
		for (const TPair<FName, float UElysiumSurfaceSettings::*>& Binding :
			UElysiumSurfaceSettings::ScalarBindings())
		{
			if (Binding.Key == FName(TEXT("WaterFogScale")))
			{
				bBound = true;
				TestEqual(TEXT("the binding reaches the field"), Surfaces->*Binding.Value,
					Surfaces->WaterFogScale);
			}
		}
		TestTrue(TEXT("WaterFogScale is a collection binding"), bBound);
	}

	// --- R7.4: the compiler's carve outranks the authored brush (G18) ---
	{
		// The same basin with a channel cut out of its middle: vbsp decomposes the water solid into
		// two convex pieces that leave the centre empty, and the authored brush knows nothing about
		// it. A point in the gap is dry, though every brush plane says otherwise.
		FElysiumWaterVolume Carved = Basin();
		FElysiumWaterBrush West;
		West.Planes = {
			FPlane(FVector(1.0, 0.0, 0.0), -40.0), FPlane(FVector(-1.0, 0.0, 0.0), 100.0),
			FPlane(FVector(0.0, 1.0, 0.0), 100.0), FPlane(FVector(0.0, -1.0, 0.0), 100.0),
			FPlane(FVector(0.0, 0.0, 1.0), 0.0), FPlane(FVector(0.0, 0.0, -1.0), 50.0),
		};
		FElysiumWaterBrush East = West;
		East.Planes[0] = FPlane(FVector(1.0, 0.0, 0.0), 100.0);
		East.Planes[1] = FPlane(FVector(-1.0, 0.0, 0.0), -40.0);
		Carved.Pieces = { West, East };
		const TArray<FElysiumWaterVolume> Pieces = { Carved };

		TestEqual(TEXT("a point in a compiler piece is in the volume"),
			ElysiumWater::FindVolumeAt(Pieces, FVector(-80.0, 0.0, -25.0)), 0);
		TestEqual(TEXT("and one in the other piece too"),
			ElysiumWater::FindVolumeAt(Pieces, FVector(80.0, 0.0, -25.0)), 0);
		TestEqual(TEXT("a point in the carve is dry, though the authored brush contains it"),
			ElysiumWater::FindVolumeAt(Pieces, FVector(0.0, 0.0, -25.0)), INDEX_NONE);
		// The pieces carry planes alone -- vbsp publishes no bounds for them -- so the plane test
		// has to run without a box, which is the one thing the brush path refuses to do.
		TestFalse(TEXT("the pieces carry no bounds of their own"),
			Pieces[0].Pieces[0].BoundsCm.IsValid != 0);
		// And with no pieces the brush is still the whole answer.
		TestEqual(TEXT("the same point is wet with no pieces staged"),
			ElysiumWater::FindVolumeAt(Volumes, FVector(0.0, 0.0, -25.0)), 0);
	}

	// --- R7.4: near water is the PVS box set, not the carve (G9/G23) ---
	{
		FElysiumWaterVolume Near = Basin();
		// The room the basin stands in: everything that can see the water.
		Near.NearBoxesCm.Add(FBox(FVector(-600.0, -600.0, -50.0), FVector(600.0, 600.0, 400.0)));
		const TArray<FElysiumWaterVolume> WithPvs = { Near };

		TestTrue(TEXT("a point in the water is near it"),
			ElysiumWater::IsNearWater(WithPvs, FVector(0.0, 0.0, -25.0)));
		TestTrue(TEXT("so is one on the bank, well outside the brush"),
			ElysiumWater::IsNearWater(WithPvs, FVector(500.0, 0.0, 200.0)));
		TestFalse(TEXT("a point outside the water's PVS is not"),
			ElysiumWater::IsNearWater(WithPvs, FVector(5000.0, 0.0, 200.0)));
		// A level baked before the near set existed degrades to the brush, never to "never".
		TestTrue(TEXT("with no near set the volume's own bounds answer"),
			ElysiumWater::IsNearWater(Volumes, FVector(0.0, 0.0, -25.0)));
		TestFalse(TEXT("and a point outside them is not near"),
			ElysiumWater::IsNearWater(Volumes, FVector(500.0, 0.0, 200.0)));
	}

	// --- R7.4: the fluid row's unset state (G7 / verdict B5) ---
	{
		const FElysiumWaterFluid Unset;
		TestFalse(TEXT("a volume with no authored fluid says so"), Unset.bHasFluid);
		TestEqual(TEXT("its index is 0 -- below VtMB's own > 0 creation guard"), Unset.Index, 0);
		TestEqual(TEXT("no density"), Unset.Density, 0.f);
		TestEqual(TEXT("no damping"), Unset.Damping, 0.f);
		TestTrue(TEXT("and no current to push a body with"), Unset.CurrentVelocityCm.IsNearlyZero());
	}

	// --- R7.4: the splash, with VtMB's own numbers (verdict D1/D2) ---
	{
		// The thresholds, converted from the Source units VtMB authors them in.
		TestEqual(TEXT("the entry needs -200 in/s of fall"),
			ElysiumWater::BigEntryVelocityZCmPerSec, -508.f, 1e-3f);
		TestEqual(TEXT("the wade needs 50 in/s across"),
			ElysiumWater::WadeSpeedCmPerSec, 127.f, 1e-3f);
		TestEqual(TEXT("and the spawn leads the body by 35 ms"),
			ElysiumWater::SplashLeadSeconds, 0.035f);
		TestEqual(TEXT("watersplash_emitter is the wade root"),
			FString(ElysiumWater::WadeSplashRoot), FString(TEXT("watersplash_emitter")));
		TestEqual(TEXT("waterbigsplash_emitter is the entry root"),
			FString(ElysiumWater::BigSplashRoot), FString(TEXT("waterbigsplash_emitter")));

		ElysiumWater::FSplashInput In;
		In.SurfaceZCm = 0.f;
		In.OriginCm = FVector(0.0, 0.0, -10.0);
		In.NowSeconds = 100.0;

		// Falling in fast enough: the entry splash, at the plane, led by the horizontal velocity.
		In.PreviousLevel = 0;
		In.Level = 1;
		In.VelocityCmPerSec = FVector(200.0, 0.0, -600.0);
		ElysiumWater::FSplashDecision Out = ElysiumWater::DecideSplash(In);
		TestEqual(TEXT("a fast entry is the big splash"),
			static_cast<int32>(Out.Kind), static_cast<int32>(ElysiumWater::ESplash::Big));
		TestEqual(TEXT("led by vel.xy * 0.035"), Out.LocationCm.X, -7.0, 1e-6);
		TestEqual(TEXT("and snapped to the surface plane"), Out.LocationCm.Z, 0.0, 1e-6);
		TestEqual(TEXT("which stamps the entity's splash clock"), Out.LastSplashSeconds, 100.0);

		// The same transition, walked into rather than fallen into: no entry splash, but the body
		// is now wading, which is the other rule.
		In.VelocityCmPerSec = FVector(200.0, 0.0, -100.0);
		Out = ElysiumWater::DecideSplash(In);
		TestEqual(TEXT("walking in at -39 in/s is the wade splash, not the big one"),
			static_cast<int32>(Out.Kind), static_cast<int32>(ElysiumWater::ESplash::Wade));

		// Wading: in the water but not under it, moving, cooldown expired.
		In.PreviousLevel = 1;
		In.Level = 2;
		In.VelocityCmPerSec = FVector(200.0, 0.0, 0.0);
		In.WadeJitterUnits = 4;
		Out = ElysiumWater::DecideSplash(In);
		TestEqual(TEXT("wading raises the wade splash"),
			static_cast<int32>(Out.Kind), static_cast<int32>(ElysiumWater::ESplash::Wade));
		TestEqual(TEXT("jittered up by RandomInt(0,8) Source units"),
			Out.LocationCm.Z, 4.0 * ElysiumMove::U, 1e-6);
		TestEqual(TEXT("and it arms the cooldown: 5.0 - horiz(in/s) * 7.8e-5"),
			Out.NextWadeSeconds, 100.0 + (5.0 - (200.0 / ElysiumMove::U) * 7.8e-5), 1e-4);

		// Submerged is not wading.
		In.Level = 3;
		In.NextWadeSeconds = ElysiumWater::NeverSeconds;
		In.LastSplashSeconds = ElysiumWater::NeverSeconds;
		TestEqual(TEXT("a submerged body does not wade"),
			static_cast<int32>(ElysiumWater::DecideSplash(In).Kind),
			static_cast<int32>(ElysiumWater::ESplash::None));

		// Too slow to wade.
		In.Level = 2;
		In.VelocityCmPerSec = FVector(100.0, 0.0, 0.0);   // 39 in/s, under the 50 in/s gate
		TestEqual(TEXT("below 50 in/s across, nothing splashes"),
			static_cast<int32>(ElysiumWater::DecideSplash(In).Kind),
			static_cast<int32>(ElysiumWater::ESplash::None));

		// D2's half-second per-entity limit, over both rules.
		In.PreviousLevel = 0;
		In.Level = 1;
		In.VelocityCmPerSec = FVector(0.0, 0.0, -600.0);
		In.LastSplashSeconds = 99.8;
		TestEqual(TEXT("a second splash 0.2 s after the last is refused"),
			static_cast<int32>(ElysiumWater::DecideSplash(In).Kind),
			static_cast<int32>(ElysiumWater::ESplash::None));
		In.LastSplashSeconds = 99.4;
		TestEqual(TEXT("0.6 s after it, allowed"),
			static_cast<int32>(ElysiumWater::DecideSplash(In).Kind),
			static_cast<int32>(ElysiumWater::ESplash::Big));

		// The wade cooldown itself, as a function of speed.
		TestEqual(TEXT("the cooldown at 50 in/s"),
			ElysiumWater::WadeCooldownSeconds(ElysiumWater::WadeSpeedCmPerSec),
			5.f - 50.f * 7.8e-5f, 1e-6f);
		TestTrue(TEXT("and it shortens as the body runs"),
			ElysiumWater::WadeCooldownSeconds(400.f) < ElysiumWater::WadeCooldownSeconds(200.f));
	}

	// --- R7.4: buoyancy, the named modernization "vphysics buoyancy" ---
	{
		// A 1 m cube whose top sits at the plane.
		const FBox Cube(FVector(-50.0, -50.0, -100.0), FVector(50.0, 50.0, 0.0));
		TestEqual(TEXT("a body under the plane is wholly submerged"),
			ElysiumWater::SubmergedFraction(Cube, 0.f), 1.f, 1e-6f);
		TestEqual(TEXT("half under is half"),
			ElysiumWater::SubmergedFraction(Cube, -50.f), 0.5f, 1e-6f);
		TestEqual(TEXT("above the plane is nothing"),
			ElysiumWater::SubmergedFraction(Cube, -100.f), 0.f, 1e-6f);
		TestEqual(TEXT("a 1 m cube displaces 1 m3 when it is all under"),
			ElysiumWater::DisplacedVolumeM3(Cube, 1.f), 1.f, 1e-6f);
		TestEqual(TEXT("and half a m3 when half of it is"),
			ElysiumWater::DisplacedVolumeM3(Cube, 0.5f), 0.5f, 1e-6f);
		// rho * V * |g|, in Unreal's own kg*cm/s2 -- a cubic metre of water at 980 cm/s2.
		TestEqual(TEXT("Archimedes on a cubic metre of water"),
			ElysiumWater::BuoyantForceZ(ElysiumWater::DefaultFluidDensityKgPerM3, 1.f, -980.f),
			980000.f, 1e-1f);
		TestEqual(TEXT("water's own density is VtMB's 1000 kg/m3"),
			ElysiumWater::DefaultFluidDensityKgPerM3, 1000.f);
	}

	// --- R7.4: the lightstyle CPD slot, and the water sound rules (G6, verdict D3/D4) ---
	{
		// One shared block: the fog owns floats 0-5 and the style owns 6, and the bake, the material
		// graph and the rig all read it by number.
		TestEqual(TEXT("the fog block is six floats"), ElysiumFog::NumFloats, 6);
		TestEqual(TEXT("the lightstyle brightness is slot 6"), ElysiumLightStyle::SlotBrightness, 6);
		TestEqual(TEXT("an unstyled primitive reads 1.0, not 0"), ElysiumLightStyle::Unstyled, 1.f);
		TestEqual(TEXT("and the masters declare it by name"),
			ElysiumLightStyle::ParameterName, FName(TEXT("LightStyleBrightness")));
		// Two mirrors of one string: the runtime owns the slot and its name, the material stage
		// owns the parameter table it writes instances through. They are pinned equal here so a
		// rename on either side is a failed test rather than a term that silently stops moving.
		TestEqual(TEXT("the Lit table agrees"),
			ElysiumSurfaceParamsLit::Scalars::LightStyleBrightness, ElysiumLightStyle::ParameterName);
		TestEqual(TEXT("and so does the Water table"),
			ElysiumSurfaceParamsWater::Scalars::LightStyleBrightness,
			ElysiumLightStyle::ParameterName);
		TestEqual(TEXT("the style tag is the one the bake stamps on a styled chunk"),
			ElysiumBakedTags::LightStyle(1), FName(TEXT("elysium.style=1")));

		// A CPD-driven parameter always reads its slot -- the material's default value is editor
		// preview and never a runtime fallback -- so the neutral 1.0 has to be WRITTEN. A component
		// built at runtime starts with no custom primitive data at all, which reads 0 on slot 6 and
		// would multiply the lit base colour and the emissive to black.
		{
			UStaticMeshComponent* Fresh = NewObject<UStaticMeshComponent>();
			TestEqual(TEXT("a fresh component carries no custom primitive data"),
				Fresh->GetCustomPrimitiveData().Data.Num(), 0);
			ElysiumLightStyle::StampUnstyled(Fresh);
			const TArray<float>& Data = Fresh->GetCustomPrimitiveData().Data;
			TestEqual(TEXT("the stamp reaches slot 6"), Data.Num(),
				ElysiumLightStyle::NumFloats);
			TestEqual(TEXT("and leaves it at full brightness"),
				Data[ElysiumLightStyle::SlotBrightness], ElysiumLightStyle::Unstyled);
			// It never resizes the block from the front: the fog's own six stay where they are.
			for (int32 Slot = 0; Slot < ElysiumFog::NumFloats; ++Slot)
			{
				TestEqual(TEXT("the fog slots underneath are untouched"), Data[Slot], 0.f);
			}
			ElysiumLightStyle::StampUnstyled(nullptr);   // and a null component is a no-op
		}

		// G6's second carrier. A brush entity's mesh is never placed, so it has no chunk actor to
		// tag: the style rides the mesh's material slot names, which the bake writes as
		// `safe_name(<group key>)` -- and a styled group's key ends in `#style<n>`, which folds to
		// `_style<n>`. `sm_pier_1`'s 17 `objects/surf` foam bodies arrive exactly this way.
		{
			auto One = [](const TCHAR* Name)
			{
				const TArray<FName> Slots { FName(Name) };
				return ElysiumLightStyle::StyleFromSlotNames(Slots);
			};
			auto Two = [](const TCHAR* First, const TCHAR* Second)
			{
				const TArray<FName> Slots { FName(First), FName(Second) };
				return ElysiumLightStyle::StyleFromSlotNames(Slots);
			};
			TestEqual(TEXT("a folded styled group key names its style"),
				One(TEXT("objects_surf_style1")), 1);
			TestEqual(TEXT("two-digit styles too"), One(TEXT("objects_surf_style32")), 32);
			TestEqual(TEXT("every section must agree"),
				Two(TEXT("a_style1"), TEXT("b_style1")), 1);
			TestEqual(TEXT("a mesh whose sections disagree animates on none"),
				Two(TEXT("a_style1"), TEXT("b_style32")), 0);
			TestEqual(TEXT("and so does one that mixes styled with unstyled"),
				Two(TEXT("a_style1"), TEXT("objects_surf")), 0);
			TestEqual(TEXT("an unstyled mesh names no style"), One(TEXT("objects_surf")), 0);
			TestEqual(TEXT("a bare marker is not a style"), One(TEXT("style1")), 0);
			TestEqual(TEXT("nor is a marker with no index"), One(TEXT("a_style")), 0);
			TestEqual(TEXT("nor one whose index is not a number"), One(TEXT("a_style1x")), 0);
			TestEqual(TEXT("style 0 is the always-on base, never animated"),
				One(TEXT("a_style0")), 0);
			TestEqual(TEXT("a mesh with no slots at all names none"),
				ElysiumLightStyle::StyleFromSlotNames(TArray<FName>()), 0);
		}

		// **The step clock moved.** `UpdateStepSound`'s water and wade arms are two arms of one
		// player step clock, not a water feature; the intervals, the one-in-four wade silence, the
		// pool selection and the foot alternation are asserted in
		// `Elysium.Substrate.Footsteps.PlayerWater` against `ElysiumFootsteps::AdvanceStepClock`.
		// What is left in this lane is the impact, the scrape and the exit.

		// The exit cue names the literal `CBaseEntity::PhysicsCheckWaterTransition` pushes, and
		// resolves to that name or to nothing -- never to a substitute. No measured VtMB install
		// ships `player/pl_wade2.wav` (stock Source naming a Half-Life 2 asset Troika never
		// packed: 0 hits across 67,469 packed entries and every loose override), so leaving the
		// water is silent here exactly as it was in 2004. Asserted install-independently, because
		// the substrate tier must answer the same on a checkout whose sound export has not run.
		TestEqual(TEXT("the exit sound names the literal vampire.dll 1003f4d0 pushes"),
			FString(ElysiumWaterAudio::ExitSound), FString(TEXT("player/pl_wade2.wav")));
		const FString Exit = ElysiumWaterAudio::Resolve(ElysiumWaterAudio::ECue::Exit, 0);
		TestTrue(TEXT("and resolves to that name or to nothing, never to a substitute"),
			Exit.IsEmpty() || Exit == FString(ElysiumWaterAudio::ExitSound));
	}

	return true;
}

} // namespace ElysiumWaterTests

#endif // WITH_DEV_AUTOMATION_TESTS
