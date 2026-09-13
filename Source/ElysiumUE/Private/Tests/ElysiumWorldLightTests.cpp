#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "ElysiumMapLightQueryData.h"
#include "ElysiumMoveSolve.h"
#include "Visual/ElysiumLightRig.h"
#include "ElysiumMapCollisionPayload.h"
#if WITH_EDITOR
#include "ElysiumContentPaths.h"
#include "Tests/ElysiumPlayerWorldFixture.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumWorldLightMathTest, "Elysium.Substrate.Stealth.WorldLightMath",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumWorldLightMathTest::RunTest(const FString&)
{
	using namespace ElysiumWorldLight;
	FElysiumWorldLight Light;
	const FVector Delta(10 * ElysiumMove::U, 0, 0);
	Light.Type = 0;
	TestEqual(TEXT("texlight InvRSquared is finite at its origin"), DistanceFalloff(Light, FVector::ZeroVector), 1.f);
	TestEqual(TEXT("texlight InvRSquared caps inside one Source unit"), DistanceFalloff(Light, FVector(0.5 * ElysiumMove::U, 0, 0)), 1.f);
	TestEqual(TEXT("texlight inverse square in Source distance"), DistanceFalloff(Light, Delta), 0.01f);
	Light.Radius = 10 * ElysiumMove::U;
	TestTrue(TEXT("radius equality remains inside"), DistanceFalloff(Light, Delta) > 0.f);
	TestEqual(TEXT("past the authored cutoff is black"), DistanceFalloff(Light, Delta * 1.01), 0.f);
	Light.Type = 1;
	Light.Constant = 2;
	Light.Linear = 3 / ElysiumMove::U;
	Light.Quadratic = 4 / (ElysiumMove::U * ElysiumMove::U);
	TestTrue(TEXT("point uses all three attenuation coefficients"), FMath::IsNearlyEqual(DistanceFalloff(Light, Delta), 1.f / 432.f));
	Light.Type = 4;
	Light.QuakeDistance = 30 * ElysiumMove::U;
	TestEqual(TEXT("quake reads linear attenuation's distance, disregards radius"), DistanceFalloff(Light, Delta * 2), 10.f);
	Light.Normal = FVector(-1, 0, 0);
	Light.Type = 0;
	TestEqual(TEXT("texlight backside is dark"), Angle(Light, FVector(-1, 0, 0)), 0.f);
	TestEqual(TEXT("texlight front is lit"), Angle(Light, FVector(1, 0, 0)), 1.f);
	Light.Type = 2;
	Light.StopDot = 0.9f;
	Light.StopDot2 = 0.5f;
	Light.Exponent = 2;
	const FVector RampDirection(0.7, FMath::Sqrt(1.0 - 0.49), 0);
	TestTrue(TEXT("spot penumbra raises cone ramp to authored exponent"), FMath::IsNearlyEqual(Angle(Light, RampDirection), 0.25f));
	Light.Exponent = 0;
	TestTrue(TEXT("zero exponent is the linear-ramp branch"), FMath::IsNearlyEqual(Angle(Light, RampDirection), 0.5f));
	TestEqual(TEXT("spot outer cone equality is excluded"), Angle(Light, FVector(0.5, FMath::Sqrt(0.75), 0)), 0.f);
	TestEqual(TEXT("spot inner cone is full"), Angle(Light, FVector(1, 0, 0)), 1.f);
	TestTrue(TEXT("raw luminance uses 0.30/0.59/0.11 without clipping"), FMath::IsNearlyEqual(Luminance(FVector(10, 20, 30)), 18.1f));
	UElysiumLightRig* Rig = NewObject<UElysiumLightRig>();
	TestEqual(TEXT("unassigned engine style is 256/264 even though rendering defaults to m"),
		Rig->GameplayStyleMultiplier(32), 256.f / 264.f);
	Rig->SetStylePattern(32, TEXT("m"));
	TestEqual(TEXT("an explicitly assigned normal pattern is exactly one"), Rig->GameplayStyleMultiplier(32), 1.f);
	Rig->SetStylePattern(32, TEXT("a"));
	TestEqual(TEXT("switched-off style is dark"), Rig->GameplayStyleMultiplier(32), 0.f);
	Rig->SetStylePattern(32, TEXT("z"));
	TestTrue(TEXT("overbright style survives"), FMath::IsNearlyEqual(Rig->GameplayStyleMultiplier(32), 25.f / 12.f));
	TestEqual(TEXT("style is held within the tenth, not interpolated"), StyleValue(TEXT("am"), 0.05), 0.f);
	TestEqual(TEXT("style advances exactly at the next tenth"), StyleValue(TEXT("am"), 0.1), 1.f);
	TestEqual(TEXT("empty engine style is 256/264"), StyleValue(TEXT(""), 0), 256.f / 264.f);
	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumLightQueryPayloadTest, "Elysium.Substrate.Stealth.LightQueryPayload",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumLightQueryPayloadTest::RunTest(const FString&)
{
	UElysiumMapLightQueryData* Data = NewObject<UElysiumMapLightQueryData>();
	const FString Error = Data->AuthorJson(TEXT(R"JSON({
		"map":"__query_test__",
		"records":{"lights":[{"type":1,"cluster":0,"constant":1,"intensity":{"x":2000,"y":30,"z":4}}],
		"nodes":[{"normal":{"x":1,"y":0,"z":0},"distance":0,"front":-1,"back":-2}],
		"leafClusters":[0,-1],"numClusters":1,"pvs":[1]},
		"hulls":[[0,0,0,100,0,0,0,100,0,0,0,100]],"sky":[],"displacements":[]
	})JSON"));
	TestTrue(TEXT("native gameplay payload authors and cooks"), Error.IsEmpty());
	if (!Error.IsEmpty()) { AddError(Error); return false; }
	TestTrue(TEXT("native payload is available with an authored empty sky"), Data->IsValidQuery());
	TestEqual(TEXT("native payload retains raw energy"), Data->Records.Lights[0].Intensity, FVector(2000,30,4));
	TestEqual(TEXT("native convex authoring survives"), Data->Occluders->WorldHullCount(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumLightQueryNativeTraceTest, "Elysium.PlayerWorld.StealthLightQuery",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumLightQueryNativeTraceTest::RunTest(const FString&)
{
	FPlayerWorldFixture Fixture;
	if (!Fixture.CreateWorld(*this)) return false;
	// `BakedUnit` rejects a leading underscore on any directory segment, so a synthetic map
	// identity must read like a retail one or the resolver hands back a short package name.
	const FString MapName = TEXT("lightquery_") + FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
	const FString ObjectPath = FElysiumContentPaths::BakedMapLightQuery(MapName);
	if (!TestFalse(TEXT("the synthetic map identity resolves to a baked object path"), ObjectPath.IsEmpty())) return false;
	const FString PackagePath = FPackageName::ObjectPathToPackageName(ObjectPath);
	UPackage* Package = CreatePackage(*PackagePath);
	if (!TestNotNull(TEXT("the transient light-query package is created"), Package)) return false;
	Package->SetFlags(RF_Transient);
	UElysiumMapLightQueryData* Data = NewObject<UElysiumMapLightQueryData>(Package,
		*FPackageName::GetLongPackageAssetName(PackagePath), RF_Public | RF_Transient);
	ON_SCOPE_EXIT { Data->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional); };
	FString Json = TEXT(R"JSON({"map":"MAP_NAME",
		"records":{"lights":[{"type":1,"cluster":0,"constant":1,
		"position":{"x":300,"y":10,"z":10},"intensity":{"x":2,"y":2,"z":2}}],
		"nodes":[{"normal":{"x":0,"y":0,"z":1},"front":-1,"back":-2}],
		"leafClusters":[0,-1],"numClusters":1,"pvs":[1]},
		"hulls":[[0,0,0,100,0,0,0,100,0,0,0,100]],"sky":[],"displacements":[]})JSON");
	Json.ReplaceInline(TEXT("MAP_NAME"), *MapName);
	const FString Error = Data->AuthorJson(Json);
	if (!Error.IsEmpty()) { AddError(Error); return false; }
	AActor* Host = Fixture.World->SpawnActor<AActor>();
	UElysiumLightRig* Rig = NewObject<UElysiumLightRig>(Host);
	Host->SetRootComponent(Rig);
	Rig->RegisterComponent();
	Rig->AdoptBaked({}, MapName);
	if (!TestTrue(TEXT("the rig adopts cooked native query geometry"), Rig->IsGameplayLightAvailable())) return false;
	TestEqual(TEXT("raw authored point luminance is unbounded by rendering"), Rig->QueryGameplayLight(FVector(200,10,10)), 2.f);
	TestEqual(TEXT("the cooked world convex blocks the source ray"), Rig->QueryGameplayLight(FVector(-10,10,10)), 0.f);

	// A runtime body blocks the ordinary scene ray but is never enumerated by the light filter.
	AActor* Dynamic = Fixture.World->SpawnActor<AActor>();
	UBoxComponent* Box = NewObject<UBoxComponent>(Dynamic);
	Dynamic->SetRootComponent(Box);
	Box->SetMobility(EComponentMobility::Movable);
	Box->InitBoxExtent(FVector(5));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Box->SetCollisionResponseToAllChannels(ECR_Block);
	Box->RegisterComponent();
	Dynamic->SetActorLocation(FVector(250,10,10));
	FHitResult Hit;
	TestTrue(TEXT("the untagged runtime body really blocks ordinary collision"),
		Fixture.World->LineTraceSingleByChannel(Hit, FVector(200,10,10), FVector(300,10,10), ECC_Visibility));
	TestEqual(TEXT("runtime entities do not cast gameplay-light shadows"), Rig->QueryGameplayLight(FVector(200,10,10)), 2.f);

	// A baked static prop uses its model's complex collision and the explicit shadow flag tag.
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("engine test cube"), Cube)) return false;
	AActor* Prop = Fixture.World->SpawnActor<AActor>();
	UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Prop);
	Prop->SetRootComponent(Mesh);
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetStaticMesh(Cube);
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Mesh->RegisterComponent();
	Prop->SetActorLocation(FVector(250,10,10));
	Prop->SetActorScale3D(FVector(0.1));
	Prop->Tags.Add(TEXT("elysium.stealth-shadow"));
	Rig->AdoptBaked({}, MapName);
	TestEqual(TEXT("an unflagged baked static prop blocks the light ray"), Rig->QueryGameplayLight(FVector(200,10,10)), 0.f);
	Prop->Tags.Remove(TEXT("elysium.stealth-shadow"));
	Rig->AdoptBaked({}, MapName);
	TestEqual(TEXT("a no-shadow static prop is not queried"), Rig->QueryGameplayLight(FVector(200,10,10)), 2.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumLightQueryBakedTutorialTest, "Elysium.Content.StealthLightQuery.Tutorial",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumLightQueryBakedTutorialTest::RunTest(const FString&)
{
	const FString MapName = TEXT("sp_tutorial_1");
	UElysiumMapLightQueryData* Data = LoadObject<UElysiumMapLightQueryData>(nullptr,
		*FElysiumContentPaths::BakedMapLightQuery(MapName));
	if (!TestNotNull(TEXT("tutorial native light-query asset exists"), Data)
		|| !TestTrue(TEXT("tutorial partition, PVS and collision payloads reload"), Data->IsValidQuery())) return false;
	TestEqual(TEXT("all authored tutorial worldlights survive the native bake"), Data->Records.Lights.Num(), 396);
	TestEqual(TEXT("the query retains its map identity"), Data->MapName, MapName);
	TestTrue(TEXT("shadow-mask world convexes survive the native bake"), Data->Occluders->WorldHullCount() > 0);
	TestEqual(TEXT("tutorial displacement shadow triangles survive the native bake"),
		Data->Occluders->DisplacementTriangleCount(), 3584);
	TestTrue(TEXT("authored sky boundaries survive the native bake"), Data->Sky->DisplacementTriangleCount() > 0);
	FPlayerWorldFixture Fixture;
	if (!Fixture.CreateWorld(*this)) return false;
	AActor* Host = Fixture.World->SpawnActor<AActor>();
	UElysiumLightRig* Rig = NewObject<UElysiumLightRig>(Host);
	Host->SetRootComponent(Rig);
	Rig->RegisterComponent();
	Rig->AdoptBaked({}, MapName);
	if (!TestTrue(TEXT("tutorial cooked query geometry becomes an available native service"),
		Rig->IsGameplayLightAvailable())) return false;
	// Sample one Source unit off each light's origin, never on it: `Engine_WorldLightDistanceFalloff`
	// (`0x200a5620`) is `1 / (constant + linear·d + quadratic·d²)` with no distance clamp, so a point
	// or spot light with `constant == 0` divides by zero at its own origin in retail as well. The
	// loader fixup (`Mod_LoadWorldlights`, all-zero attenuation -> quadratic 1) guarantees a finite
	// denominator at any d > 0.
	int32 Samples = 0;
	for (const FElysiumWorldLight& Light : Data->Records.Lights)
	{
		const FVector Sample = Light.Position + FVector(0, 0, ElysiumMove::U);
		if (Data->ClusterAt(Sample) < 0) continue;
		TestTrue(TEXT("the reloaded tutorial geometry produces finite illumination"),
			FMath::IsFinite(Rig->QueryGameplayLight(Sample)));
		if (++Samples == 16) break;
	}
	TestTrue(TEXT("the query samples actual non-solid tutorial positions"), Samples > 0);
	return true;
}
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumWorldLightQueryTest, "Elysium.Substrate.Stealth.WorldLightQuery",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumWorldLightQueryTest::RunTest(const FString&)
{
	UElysiumMapLightQueryData* Data = NewObject<UElysiumMapLightQueryData>();
	FElysiumLightQueryNode Node;
	Node.Normal = FVector(1, 0, 0); Node.Front = -1; Node.Back = -2;
	Data->Records.Nodes.Add(Node);
	Data->Records.LeafClusters = {0, -1};
	Data->Records.NumClusters = 2;
	Data->Records.Pvs = {1, 2};
	TestEqual(TEXT("partition equality takes front leaf"), Data->ClusterAt(FVector::ZeroVector), 0);
	TestEqual(TEXT("solid leaf remains invalid"), Data->ClusterAt(FVector(-1, 0, 0)), -1);
	// The same pair `SetPlayerLOS` (`0x10291610`) reaches through the engine PVS test
	// `0x101d1a90`. The rows are `{1, 2}`: cluster 0 sees only itself, cluster 1 only itself.
	TestTrue(TEXT("a cluster sees itself"), Data->ClusterVisible(0, 0));
	TestFalse(TEXT("an unset PVS bit is not visible"), Data->ClusterVisible(0, 1));
	TestTrue(TEXT("the second row is read at its own stride"), Data->ClusterVisible(1, 1));
	TestFalse(TEXT("an out-of-range cluster is not visible"), Data->ClusterVisible(0, 2));
	TestFalse(TEXT("an unresolved cluster is not visible at the data layer"),
		Data->ClusterVisible(-1, 0));
	FElysiumWorldLight Point;
	Point.Type = 1; Point.Cluster = 0; Point.Constant = 1;
	Point.Position = FVector(100, 0, 0); Point.Intensity = FVector(2, 2, 2); Point.Style = 32;
	Data->Records.Lights.Add(Point);
	FElysiumWorldLight Hidden = Point;
	Hidden.Cluster = 1; Hidden.Intensity = FVector(1000);
	Data->Records.Lights.Add(Hidden);
	FElysiumWorldLight Sun;
	Sun.Type = 3; Sun.Cluster = 1; Sun.Normal = FVector(0, 0, -1); Sun.Intensity = FVector(3);
	Data->Records.Lights.Add(Sun);
	Data->Records.Lights.Add(Sun);
	Sun.Type = 5; Data->Records.Lights.Add(Sun);
	int32 LocalTraces = 0, SunTraces = 0, StyleReads = 0;
	bool bClear = true, bSky = true;
	auto Trace = [&](const FVector&, bool bSun) { if (bSun) { ++SunTraces; return bSky; } ++LocalTraces; return bClear; };
	auto Style = [&](int32 Id) { ++StyleReads; TestEqual(TEXT("live style index reaches query"), Id, 32); return 0.5f; };
	TestEqual(TEXT("unclamped sum includes first sun outside PVS and live styled local light"),
		ElysiumWorldLight::Query(*Data, FVector::ZeroVector, Style, Trace), 4.f);
	TestEqual(TEXT("PVS rejection skips local trace"), LocalTraces, 1);
	TestEqual(TEXT("only first sun traces; skyambient skipped"), SunTraces, 1);
	TestEqual(TEXT("sun never reads style"), StyleReads, 1);
	bClear = false; bSky = false;
	TestEqual(TEXT("world/prop occlusion plus missing sky produces black"),
		ElysiumWorldLight::Query(*Data, FVector::ZeroVector, Style, Trace), 0.f);
	const int32 Before = LocalTraces + SunTraces;
	TestEqual(TEXT("solid-point early return"), ElysiumWorldLight::Query(*Data, FVector(-1, 0, 0), Style, Trace), 0.f);
	TestEqual(TEXT("solid point traces nothing"), LocalTraces + SunTraces, Before);
	return true;
}
#endif
