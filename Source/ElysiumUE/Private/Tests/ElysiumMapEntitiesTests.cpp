// Content-free Substrate automation for UElysiumMapEntities::Deserialize — the R4.1 transport's
// asset-side reader. The asset is a
// transport change and nothing else, so what is asserted here is that the two reads the JSON path
// performs at parse time happen on this path too, in the same place and with the same result: the
// retail `times` 0 -> -1 (unlimited) rewrite, and the 3D-skybox placement transform.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumMapEntities.h"

static constexpr EAutomationTestFlags GElysiumMapEntitiesTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	FElysiumMapEntityOutputRow MakeOutput(const TCHAR* Name, int32 Times)
	{
		FElysiumMapEntityOutputRow Row;
		Row.Name = Name;
		Row.Target = TEXT("door_1");
		Row.Input = TEXT("Open");
		Row.Param = TEXT("");
		Row.Delay = 0.25f;
		Row.Times = Times;
		Row.Python = TEXT("");
		return Row;
	}

	// One point entity carrying keys and two outputs, and one brush entity carrying a single
	// one-vertex hull — enough shape for every branch of the row -> def copy.
	UElysiumMapEntities* BuildTable()
	{
		UElysiumMapEntities* Asset =
			NewObject<UElysiumMapEntities>(GetTransientPackage(), NAME_None, RF_Transient);
		Asset->MapName = TEXT("sp_probe");

		FElysiumMapEntityRow Point;
		Point.Classname = TEXT("logic_auto");
		Point.TargetName = TEXT("boot");
		Point.Origin = FVector(10.0, 20.0, 30.0);
		Point.Keys.Add(TEXT("origin"), TEXT("4 -8 12"));
		Point.Keys.Add(TEXT("StartHidden"), TEXT("0"));
		Point.Outputs.Add(MakeOutput(TEXT("OnMapSpawn"), 0));   // authored 0 = unlimited
		Point.Outputs.Add(MakeOutput(TEXT("OnUser1"), 3));      // a real countdown, kept
		Point.ModelMesh = TEXT("props_lamp");
		// A value with no exact binary32 form, so the stored width is asserted and not assumed:
		// the row holds the placement rotation as four doubles for exactly this reason.
		Point.ModelQuatZ = 0.707107;
		Point.ModelQuatW = 0.707107;
		Asset->Entities.Add(Point);

		FElysiumMapEntityRow Brush;
		Brush.Classname = TEXT("func_door");
		Brush.Model = 18;
		Brush.Contents = 0x1;
		Brush.bBlocksPlayer = true;
		Brush.BrushMesh = TEXT("brush_18");
		FElysiumMapEntityHullRow Hull;
		Hull.Vertices.Add(FVector(1.0, 2.0, 3.0));
		Brush.Hulls.Add(Hull);
		Asset->Entities.Add(Brush);

		return Asset;
	}
}

// The row -> def copy: order, identity, the brush fields, and the one normalisation the reader
// owns (`times` 0 -> -1, exactly as ElysiumEntityDefs.cpp's JSON path does it).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMapEntitiesDeserializeRowsTest,
	"Elysium.Substrate.MapEntities.DeserializeRows", GElysiumMapEntitiesTestFlags)
bool FElysiumMapEntitiesDeserializeRowsTest::RunTest(const FString&)
{
	FElysiumEntityDefs Defs;
	BuildTable()->Deserialize(Defs);

	TestEqual(TEXT("map name travels"), Defs.MapName, FString(TEXT("sp_probe")));
	if (!TestEqual(TEXT("one def per row, in row order"), Defs.Num(), 2))
	{
		return false;
	}

	const FElysiumEntityDef& Point = Defs.Defs[0];
	TestEqual(TEXT("classname"), Point.Classname, FString(TEXT("logic_auto")));
	TestEqual(TEXT("targetname"), Point.TargetName, FString(TEXT("boot")));
	TestEqual(TEXT("origin"), Point.Origin, FVector(10.0, 20.0, 30.0));
	TestEqual(TEXT("keys travel whole"), Point.Keys.Num(), 2);
	TestEqual(TEXT("a key's authored spelling and value"),
		Point.Keys.FindRef(TEXT("origin")), FString(TEXT("4 -8 12")));
	TestFalse(TEXT("a point entity is not a brush"), Point.IsBrush());
	TestEqual(TEXT("point entity model index"), Point.Model, (int32)INDEX_NONE);
	if (TestEqual(TEXT("both outputs travel"), Point.Outputs.Num(), 2))
	{
		TestEqual(TEXT("an authored times of 0 normalises to unlimited"),
			Point.Outputs[0].Times, -1);
		TestEqual(TEXT("a positive times is a real countdown and is kept"),
			Point.Outputs[1].Times, 3);
		TestEqual(TEXT("output name"), Point.Outputs[0].Name, FString(TEXT("OnMapSpawn")));
		TestEqual(TEXT("output delay"), Point.Outputs[0].Delay, 0.25f);
	}
	TestEqual(TEXT("model mesh stem"), Point.ModelMesh, FString(TEXT("props_lamp")));
	TestTrue(TEXT("the placement rotation keeps its authored width"),
		Point.ModelQuat.Z == 0.707107 && Point.ModelQuat.W == 0.707107);

	const FElysiumEntityDef& Brush = Defs.Defs[1];
	TestTrue(TEXT("a *N row is a brush entity"), Brush.IsBrush());
	TestEqual(TEXT("brush model index"), Brush.Model, 18);
	TestEqual(TEXT("contents"), Brush.Contents, 0x1);
	TestTrue(TEXT("blocks player"), Brush.bBlocksPlayer);
	TestEqual(TEXT("brush mesh"), Brush.BrushMesh, FString(TEXT("brush_18")));
	if (TestEqual(TEXT("one hull"), Brush.Hulls.Num(), 1))
	{
		TestEqual(TEXT("hull vertex"), Brush.Hulls[0].Vertices[0], FVector(1.0, 2.0, 3.0));
	}
	return true;
}

// The 3D-skybox placement transform: `world(v) = scale * (v - skyOrigin)` on a `sky` row, hulls
// taking the scale and not the translation, and a non-sky row left alone — the same rule
// FElysiumEntityDefs::Parse applies, applied on this path so no consumer can tell the two apart.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMapEntitiesSkyTransformTest,
	"Elysium.Substrate.MapEntities.SkyTransform", GElysiumMapEntitiesTestFlags)
bool FElysiumMapEntitiesSkyTransformTest::RunTest(const FString&)
{
	UElysiumMapEntities* Asset =
		NewObject<UElysiumMapEntities>(GetTransientPackage(), NAME_None, RF_Transient);

	FElysiumMapEntityRow Miniature;
	Miniature.Classname = TEXT("env_sprite");
	Miniature.bSky = true;
	Miniature.Origin = FVector(100.0, 200.0, 300.0);
	FElysiumMapEntityHullRow Hull;
	Hull.Vertices.Add(FVector(4.0, 8.0, 16.0));
	Miniature.Hulls.Add(Hull);
	Asset->Entities.Add(Miniature);

	FElysiumMapEntityRow Playable;
	Playable.Classname = TEXT("prop_static");
	Playable.Origin = FVector(100.0, 200.0, 300.0);
	Asset->Entities.Add(Playable);

	const float SkyScale = 16.f;
	const FVector SkyOrigin(10.0, 20.0, 30.0);
	FElysiumEntityDefs Defs;
	Asset->Deserialize(Defs, SkyScale, SkyOrigin);

	TestEqual(TEXT("the miniature scale is recorded"), Defs.SkyScale, SkyScale);
	TestEqual(TEXT("the miniature origin is recorded"), Defs.SkyOrigin, SkyOrigin);
	TestEqual(TEXT("a sky row's origin is carried through the miniature transform"),
		Defs.Defs[0].Origin, (FVector(100.0, 200.0, 300.0) - SkyOrigin) * SkyScale);
	TestEqual(TEXT("a sky row's hull takes the scale but not the translation"),
		Defs.Defs[0].Hulls[0].Vertices[0], FVector(4.0, 8.0, 16.0) * SkyScale);
	TestEqual(TEXT("a playable row is untouched"),
		Defs.Defs[1].Origin, FVector(100.0, 200.0, 300.0));

	// Identity sky (scale 1) reads the raw miniature coordinates back unchanged, which is what a
	// caller with no `.sky` values gets and what the parity tests compare on.
	FElysiumEntityDefs Raw;
	Asset->Deserialize(Raw);
	TestEqual(TEXT("scale 1 leaves a sky row raw"), Raw.Defs[0].Origin,
		FVector(100.0, 200.0, 300.0));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
