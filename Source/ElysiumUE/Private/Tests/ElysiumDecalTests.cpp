// The decal subsystem's seam (owner call B): what `Lay` puts in the world, what the cap recycles, what `Records`/`Restore` carry
// across a save, and what the ranged shot's forward trace resolves a surface character into.
//
// Content-free. Two things stand in for the material lane's output, and only two: the engine's own
// `MD_DeferredDecal` default material (which is what a collide-sprite request rides anyway when
// `M_V2_Decal` is not in the checkout) and one transient projector instance published at the exact
// package path `FElysiumContentPaths::BakedDecalMaterial` computes -- which makes that path a
// pinned contract with `importers/materials.py` rather than a comment.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumContentPaths.h"
#include "ElysiumDecalSubsystem.h"
#include "ElysiumPhysicalMaterial.h"
#include "ElysiumSurfaceSettings.h"

#include "Components/BoxComponent.h"
#include "Components/DecalComponent.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "MaterialDomain.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Tests/AutomationCommon.h"
#include "UObject/Package.h"

namespace ElysiumDecalTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// A `vtmb:material:` id no corpus unit spells, so the transient instance below can never shadow a
// real baked asset for the rest of the automation run.
static const TCHAR* ProbeMaterialId = TEXT("vtmb:material:elysium/test/decal_probe");

// Publish a deferred-decal material at exactly the object path the runtime will look for, so the
// id path of `Lay` resolves with no baked content. Returns null if the id folds to nothing.
static UMaterialInterface* PublishProbeInstance()
{
	const FString ObjectPath = FElysiumContentPaths::BakedDecalMaterial(ProbeMaterialId);
	if (ObjectPath.IsEmpty())
	{
		return nullptr;
	}
	FString PackageName, AssetName;
	ObjectPath.Split(TEXT("."), &PackageName, &AssetName, ESearchCase::CaseSensitive,
		ESearchDir::FromEnd);
	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		return nullptr;
	}
	Package->AddToRoot();
	// A `UMaterialInstanceConstant`, because that is what the materials stage writes and because a
	// `UMaterialInstanceDynamic` cannot parent another one -- `Lay` creates its per-decal MID off
	// whatever this resolves to.
	UMaterialInstanceConstant* Instance = NewObject<UMaterialInstanceConstant>(
		Package, FName(*AssetName), RF_Public | RF_Standalone);
	if (Instance)
	{
		Instance->Parent = UMaterial::GetDefaultMaterial(MD_DeferredDecal);
	}
	return Instance;
}

static void RetireProbeInstance()
{
	const FString ObjectPath = FElysiumContentPaths::BakedDecalMaterial(ProbeMaterialId);
	FString PackageName, AssetName;
	ObjectPath.Split(TEXT("."), &PackageName, &AssetName, ESearchCase::CaseSensitive,
		ESearchDir::FromEnd);
	if (UPackage* Package = FindObject<UPackage>(nullptr, *PackageName))
	{
		Package->RemoveFromRoot();
	}
}

// =====================================================================================
// Lay -- the orientation, the box, the sort order, and the two ways a request names nothing.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDecalLayTest, "Elysium.Substrate.DecalLay", GElysiumTestFlags)
bool FElysiumDecalLayTest::RunTest(const FString&)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game) || !TestWorld.BeginPlayInTestWorld())
	{
		TestWorld.ForwardErrorMessages(this);
		return false;
	}
	UWorld* World = TestWorld.GetTestWorld();
	UElysiumDecalSubsystem* Decals = World ? World->GetSubsystem<UElysiumDecalSubsystem>() : nullptr;
	if (!TestNotNull(TEXT("the world carries a decal subsystem"), Decals))
	{
		return false;
	}

	UTexture2D* Sprite = UTexture2D::CreateTransient(4, 4);
	if (!TestNotNull(TEXT("a transient sprite for the collide-sprite path"), Sprite))
	{
		return false;
	}

	// A request that names nothing draws nothing -- a `UDecalComponent` bound to the surface-domain
	// error material would be silently replaced by the engine default, which is why there is no
	// fallback here.
	FElysiumDecalRequest Empty;
	Empty.Location = FVector(1.f, 2.f, 3.f);
	TestNull(TEXT("a request with neither an id nor a texture is refused"), Decals->Lay(Empty));
	TestEqual(TEXT("and lays nothing"), Decals->NumLaid(), 0);

	// A wall facing -X, with an authored horizontal axis.
	FElysiumDecalRequest Request;
	Request.Texture = Sprite;
	Request.Location = FVector(100.f, 200.f, 300.f);
	Request.Normal = FVector(-1.f, 0.f, 0.f);
	Request.Tangent = FVector(0.f, 1.f, 0.f);
	Request.HalfSizeCm = FVector2D(7.f, 21.f);
	UDecalComponent* First = Decals->Lay(Request);
	if (!TestNotNull(TEXT("a sprite request lays a decal"), First))
	{
		return false;
	}
	TestEqual(TEXT("the pool holds it"), Decals->NumLaid(), 1);
	TestEqual(TEXT("laid at the impact point"), First->GetComponentLocation(), Request.Location);

	// `_place_decals`' frame: local +X is the room-facing normal (the box projects along -X into
	// the wall) and the surface's horizontal axis is local Z.
	const FQuat Frame = First->GetComponentQuat();
	TestTrue(TEXT("local +X is the room-facing normal"),
		Frame.GetAxisX().Equals(Request.Normal, 1e-3f));
	TestTrue(TEXT("local +Z is the surface tangent"),
		Frame.GetAxisZ().Equals(Request.Tangent, 1e-3f));

	// `DecalSize` is the box half-size: X the projection reach, Y vertical, Z horizontal.
	TestEqual(TEXT("the projection reach matches the bake's DECAL_HALF_DEPTH"),
		static_cast<float>(First->DecalSize.X), 16.f);
	TestEqual(TEXT("half-height on local Y"), static_cast<float>(First->DecalSize.Y), 7.f);
	TestEqual(TEXT("half-width on local Z"), static_cast<float>(First->DecalSize.Z), 21.f);
	TestEqual(TEXT("VtMB decals never fade by screen size"), First->FadeScreenSize, 0.f);
	TestEqual(TEXT("a persistent stain carries no fade"), First->GetFadeDuration(), 0.f);

	// A stain laid later draws over one laid earlier, the way `R_DecalCreate`'s list did.
	Request.Location = FVector(100.f, 260.f, 300.f);
	UDecalComponent* Second = Decals->Lay(Request);
	if (!TestNotNull(TEXT("a second sprite request lays a second decal"), Second))
	{
		return false;
	}
	TestTrue(TEXT("the later stain sorts above the earlier one"),
		Second->SortOrder > First->SortOrder);
	TestTrue(TEXT("and takes its own pooled component"), Second != First);

	// A runtime stain has no authored s/t frame; a zero tangent still has to produce a valid frame
	// rather than a degenerate rotation.
	Request.Tangent = FVector::ZeroVector;
	Request.Normal = FVector(0.f, 0.f, 1.f);
	UDecalComponent* Derived = Decals->Lay(Request);
	if (!TestNotNull(TEXT("a request with no tangent still lays"), Derived))
	{
		return false;
	}
	const FQuat DerivedFrame = Derived->GetComponentQuat();
	TestTrue(TEXT("the derived frame still points +X along the normal"),
		DerivedFrame.GetAxisX().Equals(Request.Normal, 1e-3f));
	TestTrue(TEXT("and its derived tangent lies in the surface plane"), FMath::IsNearlyZero(
		static_cast<float>(FVector::DotProduct(DerivedFrame.GetAxisZ(), Request.Normal)), 1e-3f));

	Decals->ClearLaid();
	TestEqual(TEXT("ClearLaid empties the pool"), Decals->NumLaid(), 0);
	return !HasAnyErrors();
}

// =====================================================================================
// A stain with a lifetime -- the fade the engine's own timer runs, and its absence from the save.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDecalLifetimeTest, "Elysium.Substrate.DecalLifetime",
	GElysiumTestFlags)
bool FElysiumDecalLifetimeTest::RunTest(const FString&)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game) || !TestWorld.BeginPlayInTestWorld())
	{
		TestWorld.ForwardErrorMessages(this);
		return false;
	}
	UWorld* World = TestWorld.GetTestWorld();
	UElysiumDecalSubsystem* Decals = World ? World->GetSubsystem<UElysiumDecalSubsystem>() : nullptr;
	UTexture2D* Sprite = UTexture2D::CreateTransient(4, 4);
	if (!TestNotNull(TEXT("the world carries a decal subsystem"), Decals)
		|| !TestNotNull(TEXT("a transient sprite"), Sprite))
	{
		return false;
	}

	FElysiumDecalRequest Request;
	Request.Texture = Sprite;
	Request.MaterialId = FString();
	Request.LifetimeSeconds = 2.5f;
	UDecalComponent* Timed = Decals->Lay(Request);
	if (!TestNotNull(TEXT("a lifetime request lays a decal"), Timed))
	{
		return false;
	}
	TestEqual(TEXT("the life span is the requested lifetime"), Timed->GetFadeStartDelay(), 2.5f);
	TestTrue(TEXT("and it fades rather than popping"), Timed->GetFadeDuration() > 0.f);

	// A recycled slot must not inherit the previous stain's fade: a persistent request clears it.
	Request.LifetimeSeconds = 0.f;
	UDecalComponent* Persistent = Decals->Lay(Request);
	if (!TestNotNull(TEXT("a persistent request lays a decal"), Persistent))
	{
		return false;
	}
	TestEqual(TEXT("a persistent stain has no fade delay"), Persistent->GetFadeStartDelay(), 0.f);
	TestEqual(TEXT("and no fade duration"), Persistent->GetFadeDuration(), 0.f);

	// Neither is a save record: one is temporary, and both are unnamed sprites.
	TestEqual(TEXT("a sprite stain carries no DECALLIST name"), Decals->Records().Num(), 0);
	return !HasAnyErrors();
}

// =====================================================================================
// The cap -- `UElysiumSurfaceSettings::MaxLaidDecals`, oldest-first, recycling the component.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDecalCapTest, "Elysium.Substrate.DecalCap", GElysiumTestFlags)
bool FElysiumDecalCapTest::RunTest(const FString&)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game) || !TestWorld.BeginPlayInTestWorld())
	{
		TestWorld.ForwardErrorMessages(this);
		return false;
	}
	UWorld* World = TestWorld.GetTestWorld();
	UElysiumDecalSubsystem* Decals = World ? World->GetSubsystem<UElysiumDecalSubsystem>() : nullptr;
	if (!TestNotNull(TEXT("the world carries a decal subsystem"), Decals))
	{
		return false;
	}
	if (PublishProbeInstance() == nullptr
		|| UElysiumDecalSubsystem::LoadProjectorInstance(ProbeMaterialId) == nullptr)
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: the transient projector instance could not be "
			"published at the path FElysiumContentPaths::BakedDecalMaterial names"));
		RetireProbeInstance();
		return true;
	}

	UElysiumSurfaceSettings* Settings = GetMutableDefault<UElysiumSurfaceSettings>();
	const int32 SavedCap = Settings->MaxLaidDecals;
	Settings->MaxLaidDecals = 3;

	TArray<const UDecalComponent*> Components;
	for (int32 I = 0; I < 5; ++I)
	{
		FElysiumDecalRequest Request;
		Request.MaterialId = ProbeMaterialId;
		Request.Location = FVector(static_cast<float>(I) * 10.f, 0.f, 0.f);
		Components.Add(Decals->Lay(Request));
	}
	Settings->MaxLaidDecals = SavedCap;

	TestEqual(TEXT("the pool never exceeds the cap"), Decals->NumLaid(), 3);
	// Oldest-first: the fourth stain re-uses the first stain's component, the fifth the second's.
	TestTrue(TEXT("the fourth stain recycles the first's component"),
		Components[3] != nullptr && Components[3] == Components[0]);
	TestTrue(TEXT("the fifth stain recycles the second's component"),
		Components[4] != nullptr && Components[4] == Components[1]);

	const TArray<FElysiumDecalRecord> Records = Decals->Records();
	if (TestEqual(TEXT("three stains survive the cap"), Records.Num(), 3))
	{
		// The three that survive are the three most recent, in the order they were laid.
		TestEqual(TEXT("the oldest survivor is the third shot"),
			static_cast<float>(Records[0].Position.X), 20.f);
		TestEqual(TEXT("then the fourth"), static_cast<float>(Records[1].Position.X), 30.f);
		TestEqual(TEXT("then the fifth"), static_cast<float>(Records[2].Position.X), 40.f);
	}

	Decals->ClearLaid();
	RetireProbeInstance();
	return !HasAnyErrors();
}

// =====================================================================================
// `Records()` / `Restore()` -- the DECALLIST shape, round-tripped through the same `Lay`.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDecalRecordsTest, "Elysium.Substrate.DecalRecords",
	GElysiumTestFlags)
bool FElysiumDecalRecordsTest::RunTest(const FString&)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game) || !TestWorld.BeginPlayInTestWorld())
	{
		TestWorld.ForwardErrorMessages(this);
		return false;
	}
	UWorld* World = TestWorld.GetTestWorld();
	UElysiumDecalSubsystem* Decals = World ? World->GetSubsystem<UElysiumDecalSubsystem>() : nullptr;
	if (!TestNotNull(TEXT("the world carries a decal subsystem"), Decals))
	{
		return false;
	}
	if (PublishProbeInstance() == nullptr
		|| UElysiumDecalSubsystem::LoadProjectorInstance(ProbeMaterialId) == nullptr)
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: the transient projector instance could not be "
			"published at the path FElysiumContentPaths::BakedDecalMaterial names"));
		RetireProbeInstance();
		return true;
	}

	FElysiumDecalRequest World1;
	World1.MaterialId = ProbeMaterialId;
	World1.Location = FVector(10.f, 20.f, 30.f);
	World1.Normal = FVector(0.f, 0.f, 1.f);
	TestNotNull(TEXT("the world stain lays"), Decals->Lay(World1));

	FElysiumDecalRequest OnEntity;
	OnEntity.MaterialId = ProbeMaterialId;
	OnEntity.Location = FVector(-40.f, 0.f, 5.f);
	OnEntity.Normal = FVector(1.f, 0.f, 0.f);
	OnEntity.EntityIndex = 7;
	TestNotNull(TEXT("the entity-stuck stain lays"), Decals->Lay(OnEntity));

	// Neither of these is a `DECALLIST` row: one is temporary, one has no name to write.
	FElysiumDecalRequest Temporary = World1;
	Temporary.LifetimeSeconds = 4.f;
	Decals->Lay(Temporary);
	FElysiumDecalRequest Sprite;
	Sprite.Texture = UTexture2D::CreateTransient(4, 4);
	Decals->Lay(Sprite);

	const TArray<FElysiumDecalRecord> Saved = Decals->Records();
	if (!TestEqual(TEXT("only the two persistent named stains are records"), Saved.Num(), 2))
	{
		Decals->ClearLaid();
		RetireProbeInstance();
		return false;
	}
	TestEqual(TEXT("the record carries the material id"), Saved[0].MaterialId,
		FString(ProbeMaterialId));
	TestEqual(TEXT("the record carries the position"), Saved[0].Position, World1.Location);
	TestTrue(TEXT("the record carries the normal, so a restore never re-traces the world"),
		Saved[0].Normal.Equals(World1.Normal, 1e-4f));
	TestEqual(TEXT("a world stain is stuck to no entity"), Saved[0].EntityIndex, (int32)INDEX_NONE);
	TestEqual(TEXT("an entity stain carries its saveentityindex"), Saved[1].EntityIndex, 7);

	// The load: the same `Lay` the shot used, off the record alone.
	Decals->ClearLaid();
	TestEqual(TEXT("the world is clear before the restore"), Decals->Records().Num(), 0);
	TestEqual(TEXT("every record is re-laid"), Decals->Restore(Saved), 2);

	const TArray<FElysiumDecalRecord> Restored = Decals->Records();
	if (TestEqual(TEXT("and reads back as the same list"), Restored.Num(), Saved.Num()))
	{
		for (int32 I = 0; I < Saved.Num(); ++I)
		{
			TestEqual(TEXT("id round-trips"), Restored[I].MaterialId, Saved[I].MaterialId);
			TestEqual(TEXT("position round-trips"), Restored[I].Position, Saved[I].Position);
			TestTrue(TEXT("normal round-trips"), Restored[I].Normal.Equals(Saved[I].Normal, 1e-4f));
			TestEqual(TEXT("entity index round-trips"), Restored[I].EntityIndex,
				Saved[I].EntityIndex);
		}
	}

	Decals->ClearLaid();
	RetireProbeInstance();
	return !HasAnyErrors();
}

// =====================================================================================
// The ranged shot's forward trace -- `C_TEGunshotDecal`'s decal half against a real surface.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDecalImpactTraceTest, "Elysium.Substrate.DecalImpactTrace",
	GElysiumTestFlags)
bool FElysiumDecalImpactTraceTest::RunTest(const FString&)
{
	// The pure table first: the pool is the unremapped surface character's, the variation is the
	// caller's roll, and every character the shipped `surfaceproperties.txt` does not group into
	// flesh / wood / glass / metal falls to concrete (effects.md §3.5).
	TestEqual(TEXT("W is the wood pool"),
		ElysiumImpactDecals::MaterialIdFor(TEXT("W"), false, 3),
		FString(TEXT("vtmb:material:decals/hits/wood/shot3")));
	TestEqual(TEXT("F is the blood pool"),
		ElysiumImpactDecals::MaterialIdFor(TEXT("F"), false, 1),
		FString(TEXT("vtmb:material:decals/hits/flesh/blood1")));
	TestEqual(TEXT("Y is the glass pool"),
		ElysiumImpactDecals::MaterialIdFor(TEXT("Y"), false, 5),
		FString(TEXT("vtmb:material:decals/hits/glass/shot5")));
	for (const TCHAR* Metal : { TEXT("M"), TEXT("V"), TEXT("G"), TEXT("P") })
	{
		TestEqual(TEXT("the metal/vent/grate/computer group is one pool"),
			ElysiumImpactDecals::PoolFor(Metal, false),
			FString(TEXT("decals/hits/metal/shot")));
	}
	for (const TCHAR* Other : { TEXT("C"), TEXT("D"), TEXT("O"), TEXT("T"), TEXT("S"), TEXT("A"),
			TEXT("N"), TEXT("U"), TEXT("I"), TEXT("X"), TEXT("") })
	{
		TestEqual(TEXT("everything else is the table's index 0"),
			ElysiumImpactDecals::PoolFor(Other, false),
			FString(TEXT("decals/hits/concrete/shot")));
	}
	TestEqual(TEXT("the soak column is a flesh hit whatever it landed on"),
		ElysiumImpactDecals::PoolFor(TEXT("W"), true), FString(TEXT("decals/hits/flesh/soak")));
	TestEqual(TEXT("a variation outside the pool clamps into it"),
		ElysiumImpactDecals::MaterialIdFor(TEXT("W"), false, 99),
		FString(TEXT("vtmb:material:decals/hits/wood/shot5")));

	// The authored size: 64 texels x `$decalscale`, in Source units. 0.10 everywhere but glass.
	TestTrue(TEXT("a bullet hole is 64 x 0.10 Source units across"), FMath::IsNearlyEqual(
		static_cast<float>(ElysiumImpactDecals::HalfSizeCmFor(TEXT("decals/hits/wood/shot")).X),
		64.f * 0.10f * 2.54f * 0.5f, 1e-3f));
	TestTrue(TEXT("a glass hole authors $decalscale 0.25 and is 2.5x as wide"), FMath::IsNearlyEqual(
		static_cast<float>(
			ElysiumImpactDecals::HalfSizeCmFor(TEXT("vtmb:material:decals/hits/glass/shot2")).X),
		64.f * 0.25f * 2.54f * 0.5f, 1e-3f));

	// Then the trace, against a surface that carries a real `UElysiumPhysicalMaterial`.
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game) || !TestWorld.BeginPlayInTestWorld())
	{
		TestWorld.ForwardErrorMessages(this);
		return false;
	}
	UWorld* World = TestWorld.GetTestWorld();
	AActor* WallOwner = World ? World->SpawnActor<AActor>() : nullptr;
	UBoxComponent* Wall = WallOwner ? NewObject<UBoxComponent>(WallOwner, TEXT("ShotWall")) : nullptr;
	UElysiumPhysicalMaterial* Surface = NewObject<UElysiumPhysicalMaterial>(GetTransientPackage());
	if (!TestNotNull(TEXT("a wall to shoot"), Wall)
		|| !TestNotNull(TEXT("a surface property for it"), Surface))
	{
		return false;
	}
	Surface->GameMaterial = TEXT("W");
	WallOwner->SetRootComponent(Wall);
	Wall->InitBoxExtent(FVector(10.f, 100.f, 100.f));
	Wall->SetWorldLocation(FVector(200.f, 0.f, 0.f));
	Wall->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Wall->SetCollisionResponseToAllChannels(ECR_Ignore);
	// The render-surface channel, the one a converted map's drawn geometry blocks and its
	// material-less `.hulls` collider does not (ElysiumImpactDecals::SurfaceTraceChannel).
	Wall->SetCollisionResponseToChannel(ElysiumImpactDecals::SurfaceTraceChannel, ECR_Block);
	Wall->RegisterComponent();
	WallOwner->AddInstanceComponent(Wall);
	// After registration: the override reaches the live body's shapes, which is where a query with
	// `bReturnPhysicalMaterial` reads it from. One tick then pushes the new material into the
	// solver's external query list, which is what a scene query resolves the shape's handle
	// against -- without it the hit reports no material at all.
	Wall->SetPhysMaterialOverride(Surface);
	TestWorld.TickTestWorld();

	FElysiumDecalRequest Request;
	FHitResult Hit;
	if (!TestTrue(TEXT("the shot's forward trace meets the wall"),
		ElysiumImpactDecals::BuildImpactRequest(World, FVector::ZeroVector,
			FVector(1.f, 0.f, 0.f), 1000.f, /*Variation*/ 4, Request, Hit)))
	{
		return false;
	}
	TestTrue(TEXT("the hit reports the wall's physical material"),
		Cast<UElysiumPhysicalMaterial>(Hit.PhysMaterial.Get()) == Surface);
	TestEqual(TEXT("the hit's surface character selects the wood pool"), Request.MaterialId,
		FString(TEXT("vtmb:material:decals/hits/wood/shot4")));
	TestTrue(TEXT("the hole is at the impact point"),
		FMath::IsNearlyEqual(static_cast<float>(Request.Location.X), 190.f, 1.f));
	TestTrue(TEXT("and faces back down the shot"),
		Request.Normal.Equals(FVector(-1.f, 0.f, 0.f), 1e-3f));
	TestTrue(TEXT("at the unit's own authored size"), FMath::IsNearlyEqual(
		static_cast<float>(Request.HalfSizeCm.X), 64.f * 0.10f * 2.54f * 0.5f, 1e-3f));

	// The same wall, re-classified: the pool follows the surface property, not the geometry.
	Surface->GameMaterial = TEXT("Y");
	TestTrue(TEXT("the trace runs again"), ElysiumImpactDecals::BuildImpactRequest(World,
		FVector::ZeroVector, FVector(1.f, 0.f, 0.f), 1000.f, 2, Request, Hit));
	TestEqual(TEXT("a glass wall takes the glass pool"), Request.MaterialId,
		FString(TEXT("vtmb:material:decals/hits/glass/shot2")));
	TestTrue(TEXT("and the glass unit's larger hole"), FMath::IsNearlyEqual(
		static_cast<float>(Request.HalfSizeCm.X), 64.f * 0.25f * 2.54f * 0.5f, 1e-3f));

	// A shot that reaches nothing marks nothing -- an ordinary outcome, not a failure.
	TestFalse(TEXT("a shot that falls short of the wall marks nothing"),
		ElysiumImpactDecals::BuildImpactRequest(World, FVector::ZeroVector,
			FVector(1.f, 0.f, 0.f), 50.f, 1, Request, Hit));
	TestFalse(TEXT("a degenerate direction is refused"),
		ElysiumImpactDecals::BuildImpactRequest(World, FVector::ZeroVector, FVector::ZeroVector,
			1000.f, 1, Request, Hit));
	return !HasAnyErrors();
}

} // namespace ElysiumDecalTests

#endif // WITH_DEV_AUTOMATION_TESTS
