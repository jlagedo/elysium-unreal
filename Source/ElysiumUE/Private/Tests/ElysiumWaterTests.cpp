// R7.1 -- water volumes (`docs/architecture/water-architecture.md` rulings B/C/D,
// `seam_map_map.md` -> "Import — water volumes"). The stage emits the rows and the bake places one
// actor; the runtime's whole share is the point-in-brush classification `CheckWater` asks for, the
// tag contract the visuals bucket by, and the post-process state machine the renderer walks. Each
// is pinned here -- the classification content-free, the actor against a bare test world.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumBakedTags.h"
#include "ElysiumFog.h"
#include "ElysiumSurfaceParams.h"
#include "ElysiumSurfaceSettings.h"
#include "ElysiumWaterVolumes.h"

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

	return true;
}

// The placed actor: the reflected shape the bake writes through, and the post-process state
// machine the renderer walks per view.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumWaterActorTest, "Elysium.Substrate.WaterActor", GElysiumTestFlags)
bool FElysiumWaterActorTest::RunTest(const FString&)
{
	// --- the reflected names `bake_map_v2.py::_place_water` writes by ---
	{
		// The bake calls `set_editor_property` with the Python spelling of each of these
		// (`volumes`, `surface_z_cm`, `bounds_cm`, ...), which resolves through reflection and
		// fails silently on a rename. Nothing else in the module reads them by name, so this is
		// the only place the spelling is asserted.
		const FArrayProperty* Rows =
			FindFProperty<FArrayProperty>(AElysiumWaterVolumes::StaticClass(), TEXT("Volumes"));
		TestNotNull(TEXT("Volumes is a reflected array"), Rows);

		const TCHAR* const VolumeFields[] = { TEXT("Index"), TEXT("SurfaceZCm"), TEXT("MinZCm"),
			TEXT("Material"), TEXT("bFogEnabled"), TEXT("FogColor"), TEXT("FogStartCm"),
			TEXT("FogEndCm"), TEXT("Brushes") };
		for (const TCHAR* Field : VolumeFields)
		{
			TestNotNull(*FString::Printf(TEXT("FElysiumWaterVolume::%s"), Field),
				FindFProperty<FProperty>(FElysiumWaterVolume::StaticStruct(), Field));
		}
		const TCHAR* const BrushFields[] = { TEXT("Planes"), TEXT("BoundsCm") };
		for (const TCHAR* Field : BrushFields)
		{
			TestNotNull(*FString::Printf(TEXT("FElysiumWaterBrush::%s"), Field),
				FindFProperty<FProperty>(FElysiumWaterBrush::StaticStruct(), Field));
		}
	}

	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game))
	{
		TestWorld.ForwardErrorMessages(this);
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no test world for the post-process volume"));
		return true;
	}
	UWorld* World = TestWorld.GetTestWorld();
	AElysiumWaterVolumes* Actor = World->SpawnActor<AElysiumWaterVolumes>();
	if (Actor == nullptr)
	{
		AddError(TEXT("AElysiumWaterVolumes did not spawn"));
		return false;
	}
	Actor->Volumes.Add(Basin());
	TestWorld.BeginPlayInTestWorld();

	// --- the volume's own properties: bounded, priority 1, full weight, off until a view says so ---
	{
		const FPostProcessVolumeProperties Properties = Actor->GetProperties();
		TestFalse(TEXT("the volume is bounded"), Properties.bIsUnbound);
		TestEqual(TEXT("above the map's neutral unbound PPV"), Properties.Priority, 1.f);
		TestEqual(TEXT("at full weight -- an underwater view has no soft edge"),
			Properties.BlendWeight, 1.f);
		TestFalse(TEXT("and off until a view resolves inside it"), Properties.bIsEnabled);
		TestNotNull(TEXT("its settings block is the persistent one"), Properties.Settings);
	}

	// --- the per-view gate, and the fog the MID carries while it is on ---
	{
		TestEqual(TEXT("a view under the plane resolves the volume"),
			Actor->UpdateViewPostProcess(FVector(0.0, 0.0, -25.0)), 0);
		TestTrue(TEXT("which enables the volume"), Actor->GetProperties().bIsEnabled);
		float Distance = -1.f;
		TestTrue(TEXT("and EncompassesPoint answers the gate"),
			Actor->EncompassesPoint(FVector(0.0, 0.0, -25.0), 0.f, &Distance));
		TestEqual(TEXT("at distance 0, which is what the engine actually blends on"), Distance, 0.f);

		if (UMaterialInstanceDynamic* Mid = Actor->GetUnderwaterMID())
		{
			// The same three values `ElysiumFog::Pack` writes for the scene fog and the decals.
			TArray<float> Fog;
			const FElysiumWaterVolume& Row = Actor->Volumes[0];
			ElysiumFog::Pack(Row.bFogEnabled, Row.FogColor, Row.FogStartCm, Row.FogEndCm, Fog);

			FLinearColor Colour = FLinearColor::White;
			Mid->GetVectorParameterValue(ElysiumSurfaceParamsDecal::Vectors::FogColor, Colour);
			TestEqual(TEXT("the MID carries the decoded fog colour"), Colour.R,
				Fog[ElysiumFog::SlotColor + 0], 1e-6f);
			float Value = 0.f;
			Mid->GetScalarParameterValue(ElysiumSurfaceParamsDecal::Scalars::FogStart, Value);
			TestEqual(TEXT("its start, in cm"), Value, Fog[ElysiumFog::SlotStart], 1e-3f);
			Mid->GetScalarParameterValue(ElysiumSurfaceParamsDecal::Scalars::FogInvRange, Value);
			TestEqual(TEXT("and its inverse range"), Value, Fog[ElysiumFog::SlotInvRange], 1e-9f);
		}
		else
		{
			AddInfo(TEXT("M_ElysiumUnderwater is not generated; the MID triple is not asserted "
			             "(run: uv run elysium export bundle policy)"));
		}

		TestEqual(TEXT("a view above the plane resolves nothing"),
			Actor->UpdateViewPostProcess(FVector(0.0, 0.0, 40.0)), INDEX_NONE);
		TestFalse(TEXT("which turns the volume back off"), Actor->GetProperties().bIsEnabled);
		TestFalse(TEXT("and EncompassesPoint follows it"),
			Actor->EncompassesPoint(FVector(0.0, 0.0, 40.0), 0.f, &Distance));
	}

	TestWorld.EndPlayInTestWorld();
	TestWorld.ForwardErrorMessages(this);
	return true;
}
} // namespace ElysiumWaterTests

#endif // WITH_DEV_AUTOMATION_TESTS
