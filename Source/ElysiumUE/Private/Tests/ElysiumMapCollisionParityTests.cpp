// Content-tier parity for the collision transport: the baked `UElysiumMapCollisionPayload` and the `<map>.hulls` / `<map>.dispcol` /
// `<map>.ents` documents it replaces must describe the same solid world.
//
// This is the parity that matters, and it is deliberately not the stage's. The stage compares
// numbers it just read against the files it read them from; these tests load the real cooked asset
// and the real sidecars and compare the geometry each **C++** path ends up with -- the payload's
// `AggGeom` against the sidecar readers' own parse, and the payload's per-entity bodies against the
// defs `ElysiumEntityDefSource::Load` hands the entity world (which is where the 3D-skybox scale
// the stage applies is proved, since the deserializer applies it on the other side).
//
// Ground truth for "which maps" is the staged manifest this machine's last `uv run elysium import
// map-collision` wrote under `$ELYSIUM_WORK_ROOT/import/map_collision/`, the same oracle the model
// and entity lanes read -- not a re-derivation of the selection here, which could drift from the
// real one and still agree with a bug in it.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumContentPaths.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEnvironment.h"
#include "ElysiumMapCollisionPayload.h"
#include "ElysiumMapEntities.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "PhysicsEngine/BodySetup.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

static constexpr EAutomationTestFlags GElysiumMapCollisionParityTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// `$ELYSIUM_WORK_ROOT/import/map_collision/manifest.json`, or empty when the root is unset.
	FString ManifestPath()
	{
		const FString WorkRoot = FPlatformMisc::GetEnvironmentVariable(TEXT("ELYSIUM_WORK_ROOT"));
		if (WorkRoot.IsEmpty())
		{
			return FString();
		}
		return WorkRoot / TEXT("import") / TEXT("map_collision") / TEXT("manifest.json");
	}

	// The map stems the last stage run named, or an abstention already recorded and false.
	bool StagedMaps(FAutomationTestBase& Test, TArray<FString>& OutMaps)
	{
		const FString Path = ManifestPath();
		if (Path.IsEmpty())
		{
			Test.AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: ELYSIUM_WORK_ROOT is not configured"));
			return false;
		}
		if (!IFileManager::Get().FileExists(*Path))
		{
			Test.AddInfo(FString::Printf(
				TEXT("ELYSIUM_TEST_ABSTAIN: no staged map-collision manifest at %s (run: uv run ")
				TEXT("elysium import map-collision --maps sp_tutorial_1 --maps sm_pawnshop_1 ")
				TEXT("--maps sm_hub_1)"), *Path));
			return false;
		}

		FString Text;
		if (!Test.TestTrue(TEXT("staged map-collision manifest reads"),
			FFileHelper::LoadFileToString(Text, *Path)))
		{
			return false;
		}
		TSharedPtr<FJsonObject> Manifest;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!Test.TestTrue(TEXT("staged map-collision manifest parses"),
			FJsonSerializer::Deserialize(Reader, Manifest) && Manifest.IsValid()))
		{
			return false;
		}
		for (const TSharedPtr<FJsonValue>& Row : Manifest->GetArrayField(TEXT("maps")))
		{
			OutMaps.Add(Row->AsObject()->GetStringField(TEXT("map")));
		}
		if (OutMaps.Num() == 0)
		{
			Test.AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: the staged manifest names no map"));
			return false;
		}
		return true;
	}

	const UElysiumMapCollisionPayload* LoadPayload(const FString& Map)
	{
		return LoadObject<UElysiumMapCollisionPayload>(
			nullptr, *FElysiumContentPaths::BakedMapCollision(Map), nullptr,
			LOAD_NoWarn | LOAD_Quiet);
	}

	// The sidecar reader's own rule, restated: at least four whole vertex triples per line.
	// (`UElysiumMapCollision::LoadHulls` -- the numbers matter, so this is not a loose count.)
	TArray<TArray<double>> ReadHullRows(const FString& Path)
	{
		TArray<TArray<double>> Rows;
		TArray<FString> Lines;
		if (!FFileHelper::LoadFileToStringArray(Lines, *Path))
		{
			return Rows;
		}
		for (const FString& Line : Lines)
		{
			TArray<FString> Tokens;
			Line.ParseIntoArray(Tokens, TEXT(" "), true);
			if (Tokens.Num() < 12 || Tokens.Num() % 3 != 0)
			{
				continue;
			}
			TArray<double>& Row = Rows.AddDefaulted_GetRef();
			Row.Reserve(Tokens.Num());
			for (const FString& Token : Tokens)
			{
				Row.Add(FCString::Atod(*Token));
			}
		}
		return Rows;
	}

	int32 CountDispRows(const FString& Path)
	{
		TArray<FString> Lines;
		if (!FFileHelper::LoadFileToStringArray(Lines, *Path))
		{
			return 0;
		}
		int32 Count = 0;
		for (const FString& Line : Lines)
		{
			TArray<FString> Tokens;
			Line.ParseIntoArray(Tokens, TEXT(" "), true);
			if (Tokens.Num() == 9)
			{
				++Count;
			}
		}
		return Count;
	}
}

// The world collider: the payload's convex set and the `<map>.hulls` the sidecar path parses are
// the same set, hull for hull and vertex for vertex, and the displacement soup is the same triangle
// count. This is the count parity the roadmap's proof asks for, plus the values behind it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMapCollisionWorldParityTest,
	"Elysium.Content.MapCollision.WorldParity", GElysiumMapCollisionParityTestFlags)
bool FElysiumMapCollisionWorldParityTest::RunTest(const FString&)
{
	TArray<FString> Maps;
	if (!StagedMaps(*this, Maps))
	{
		return true;
	}

	int32 Compared = 0;
	TArray<FString> Unreadable;
	for (const FString& Map : Maps)
	{
		const UElysiumMapCollisionPayload* Payload = LoadPayload(Map);
		if (Payload == nullptr)
		{
			Unreadable.Add(FString::Printf(TEXT("%s: no baked payload at %s"),
				*Map, *FElysiumContentPaths::BakedMapCollision(Map)));
			continue;
		}
		const TArray<TArray<double>> Rows = ReadHullRows(FElysiumContentPaths::MapHulls(Map));
		if (Rows.Num() == 0)
		{
			Unreadable.Add(FString::Printf(TEXT("%s: no readable %s"),
				*Map, *FElysiumContentPaths::MapHulls(Map)));
			continue;
		}
		++Compared;

		TestEqual(FString::Printf(TEXT("%s: world convex count, payload vs .hulls"), *Map),
			Payload->WorldHullCount(), Rows.Num());
		TestEqual(FString::Printf(TEXT("%s: displacement triangles, payload vs .dispcol"), *Map),
			Payload->DisplacementTriangleCount(),
			CountDispRows(FElysiumContentPaths::MapDispCol(Map)));

		const UBodySetup* Setup = Payload->GetWorldHulls();
		if (Setup == nullptr)
		{
			AddError(FString::Printf(TEXT("%s: the payload carries no world body setup"), *Map));
			continue;
		}
		const int32 Common = FMath::Min(Setup->AggGeom.ConvexElems.Num(), Rows.Num());
		int32 Differing = 0;
		for (int32 Index = 0; Index < Common; ++Index)
		{
			const TArray<FVector>& Verts = Setup->AggGeom.ConvexElems[Index].VertexData;
			const TArray<double>& Row = Rows[Index];
			if (Verts.Num() * 3 != Row.Num())
			{
				++Differing;
				continue;
			}
			for (int32 V = 0; V < Verts.Num(); ++V)
			{
				if (Verts[V].X != Row[V * 3] || Verts[V].Y != Row[V * 3 + 1]
					|| Verts[V].Z != Row[V * 3 + 2])
				{
					++Differing;
					break;
				}
			}
		}
		TestEqual(FString::Printf(TEXT("%s: world hulls differing from .hulls"), *Map),
			Differing, 0);
		AddInfo(FString::Printf(
			TEXT("%s: %d world convex hull(s), %d displacement triangle(s), %d brush body(ies)"),
			*Map, Payload->WorldHullCount(), Payload->DisplacementTriangleCount(),
			Payload->BrushBodies.Num()));
	}

	if (Compared == 0)
	{
		AddInfo(FString::Printf(
			TEXT("ELYSIUM_TEST_ABSTAIN: no staged map could be read both ways (%s)"),
			*FString::Join(Unreadable, TEXT("; "))));
		return true;
	}
	for (const FString& Row : Unreadable)
	{
		AddError(FString::Printf(TEXT("%s (other maps in the same run resolved)"), *Row));
	}
	return true;
}

// The brush-entity bodies: every brush entity the running game builds a body for has one in the
// payload, at its own lump ordinal, carrying exactly the convex geometry its def carries -- which
// for a `sky` entity means the deserializer's scaled hulls, the one transform the stage applies.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMapCollisionBrushParityTest,
	"Elysium.Content.MapCollision.BrushParity", GElysiumMapCollisionParityTestFlags)
bool FElysiumMapCollisionBrushParityTest::RunTest(const FString&)
{
	TArray<FString> Maps;
	if (!StagedMaps(*this, Maps))
	{
		return true;
	}

	//: How many differing entities one map may name before the rest are rolled up.
	constexpr int32 ReportLimit = 8;

	int32 Compared = 0, TotalBodies = 0;
	for (const FString& Map : Maps)
	{
		const UElysiumMapCollisionPayload* Payload = LoadPayload(Map);
		if (Payload == nullptr)
		{
			continue;   // the world test above owns the verdict on a missing payload
		}

		// The defs exactly as the map load takes them, sky transform included: that is the geometry
		// the runtime would have cooked, and therefore what the payload must already carry.
		FElysiumSkyDef Sky;
		FElysiumSkyDef::Parse(FElysiumContentPaths::MapSky(Map), Sky);
		FElysiumEntityDefs Defs;
		if (ElysiumEntityDefSource::Load(Map, Defs, Sky.Scale, Sky.OriginCm)
			== EElysiumEntityDefSource::None)
		{
			AddError(FString::Printf(TEXT("%s: no entity defs from either transport"), *Map));
			continue;
		}
		++Compared;

		int32 Expected = 0, Differing = 0;
		for (int32 Index = 0; Index < Defs.Num(); ++Index)
		{
			const FElysiumEntityDef& Def = Defs.Defs[Index];
			// The def's hulls minus the ones a body cannot hold (a convex needs a tetrahedron); the
			// stage drops the same ones, so an entity whose hulls are all degenerate has no row.
			TArray<const FElysiumConvexHull*> Usable;
			for (const FElysiumConvexHull& Hull : Def.Hulls)
			{
				if (Hull.Vertices.Num() >= 4)
				{
					Usable.Add(&Hull);
				}
			}
			if (Usable.Num() == 0)
			{
				continue;
			}
			++Expected;
			const UBodySetup* Body = Payload->FindBrushBody(Index);
			if (Body == nullptr)
			{
				++Differing;
				if (Differing <= ReportLimit)
				{
					AddError(FString::Printf(TEXT("%s[%d] (%s): no cooked body for a brush entity"),
						*Map, Index, *Def.Classname));
				}
				continue;
			}

			bool bEqual = Body->AggGeom.ConvexElems.Num() == Usable.Num();
			for (int32 H = 0; bEqual && H < Usable.Num(); ++H)
			{
				bEqual = Body->AggGeom.ConvexElems[H].VertexData == Usable[H]->Vertices;
			}
			if (!bEqual)
			{
				++Differing;
				if (Differing <= ReportLimit)
				{
					AddError(FString::Printf(
						TEXT("%s[%d] (%s%s): cooked body and def hulls differ (%d vs %d convex)"),
						*Map, Index, *Def.Classname, Def.bSky ? TEXT(", sky") : TEXT(""),
						Body->AggGeom.ConvexElems.Num(), Usable.Num()));
				}
			}
		}
		if (Differing > ReportLimit)
		{
			AddError(FString::Printf(TEXT("%s: %d of %d brush entities differ (first %d named)"),
				*Map, Differing, Expected, ReportLimit));
		}
		TotalBodies += Expected;
		TestEqual(FString::Printf(TEXT("%s: cooked brush bodies vs brush entities"), *Map),
			Payload->BrushBodies.Num(), Expected);
		AddInfo(FString::Printf(TEXT("%s: %d brush entity body(ies) compared, %d differing"),
			*Map, Expected, Differing));
	}

	if (Compared == 0)
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no staged map carried a baked collision payload"));
		return true;
	}
	AddInfo(FString::Printf(TEXT("%d map(s), %d brush body(ies) compared convex for convex"),
		Compared, TotalBodies));
	return true;
}

// The cooked payload is usable geometry, not just numbers: every setup it carries creates its
// Chaos meshes, which is the state `UElysiumMapCollision::Build` requires before it registers a
// component and the collision-ready barrier polls.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMapCollisionCookedTest,
	"Elysium.Content.MapCollision.PhysicsMeshes", GElysiumMapCollisionParityTestFlags)
bool FElysiumMapCollisionCookedTest::RunTest(const FString&)
{
	TArray<FString> Maps;
	if (!StagedMaps(*this, Maps))
	{
		return true;
	}

	int32 Compared = 0;
	for (const FString& Map : Maps)
	{
		// Const is dropped deliberately: creating the physics meshes is exactly what the runtime
		// does to this asset at map load, and this test asserts that it succeeds.
		UElysiumMapCollisionPayload* Payload = const_cast<UElysiumMapCollisionPayload*>(
			LoadPayload(Map));
		if (Payload == nullptr)
		{
			continue;   // the world test above owns the verdict on a missing payload
		}
		++Compared;
		TestTrue(FString::Printf(TEXT("%s: every cooked body creates its physics meshes"), *Map),
			Payload->CreatePhysicsMeshes());
		if (const UBodySetup* Setup = Payload->GetWorldHulls())
		{
			TestTrue(FString::Printf(TEXT("%s: the world set is live geometry"), *Map),
				Setup->bCreatedPhysicsMeshes && !Setup->bFailedToCreatePhysicsMeshes);
		}
	}

	if (Compared == 0)
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no staged map carried a baked collision payload"));
		return true;
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
