// Story 3's headline claim, asked of the REAL payloads in a live physics world.
//
// `ElysiumContentsSignatureTests` proves each generated profile answers each retail mask, on
// synthetic boxes. That is half the claim. The other half is that the map's own brushes wear the
// right profile -- that the five NPC-only clips on `sp_tutorial_1` really do stop an NPC and not
// the player, that its seventeen sight-only brushes stop sight and neither pawn, that the hub's
// roadway stops nobody. So this authors the shipped payload into a live world exactly as the bake
// does, and asks the physics scene, brush by brush, per signature.
//
// The question is asked of THE COMPONENT, not of the point: shipped brushes overlap (a clip brush
// over a solid is ordinary), so "is this point blocked" would be answered by whichever body got
// there first. An overlap query at a convex's own centroid, filtered to that signature's
// component, asks the one body the row is about.

#include "Misc/AutomationTest.h"

// WITH_EDITOR as well: the payload is authored into the scene through
// `AElysiumWorldCollisionActor::AuthorFromPayload`, which is the bake's own path and is
// editor-only. A Game target has no such call to make, and found this the hard way.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "ElysiumCollisionChannels.h"
#include "ElysiumContentPaths.h"
#include "ElysiumContentsSignature.h"
#include "ElysiumMapCollisionPayload.h"
#include "ElysiumWorldCollisionActor.h"
#include "Map/ElysiumRetailMaskRecipe.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "PhysicsEngine/BodySetup.h"
#include "Tests/ElysiumPlayerWorldFixture.h"

static constexpr EAutomationTestFlags GElysiumMapCollisionContentFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	struct FExpectedBody
	{
		const TCHAR* Spelled;
		uint8 Signature;
		int32 Hulls;
		int32 CommonHulls;
	};

	/** Is `Component` among the BLOCKING overlaps of a small sphere at `Point` on `Channel`? */
	bool ComponentBlocks(UWorld* World, const UPrimitiveComponent* Component, const FVector& Point,
		ECollisionChannel Channel, FMaskFilter IgnoreMask = 0)
	{
		TArray<FOverlapResult> Overlaps;
		FCollisionQueryParams Params(FName(TEXT("ElysiumMapCollisionProbe")), false);
		Params.IgnoreMask = IgnoreMask;
		World->OverlapMultiByChannel(Overlaps, Point, FQuat::Identity, Channel,
			FCollisionShape::MakeSphere(1.0f),
			Params);
		for (const FOverlapResult& Overlap : Overlaps)
		{
			if (Overlap.GetComponent() == Component && Overlap.bBlockingHit)
			{
				return true;
			}
		}
		return false;
	}

	/** The centroid of a convex's own vertices: inside the hull, which its AABB centre need not be. */
	FVector Centroid(const FKConvexElem& Convex)
	{
		FVector Sum = FVector::ZeroVector;
		for (const FVector& Vertex : Convex.VertexData)
		{
			Sum += Vertex;
		}
		return Convex.VertexData.Num() > 0 ? Sum / Convex.VertexData.Num() : Sum;
	}

	bool RunMap(FAutomationTestBase& Test, const TCHAR* MapName,
		TConstArrayView<FExpectedBody> Expected)
	{
		FPlayerWorldFixture Fixture;
		if (!Fixture.CreateWorld(Test))
		{
			return false;
		}
		UWorld* World = Fixture.World;

		UElysiumMapCollisionPayload* Payload = LoadObject<UElysiumMapCollisionPayload>(
			nullptr, *FElysiumContentPaths::BakedMapCollision(MapName));
		if (!Test.TestNotNull(TEXT("the map's collision payload exists"), Payload)
			|| !Test.TestTrue(TEXT("its cooked bodies materialise"), Payload->CreatePhysicsMeshes()))
		{
			return false;
		}

		AElysiumWorldCollisionActor* Actor = World->SpawnActor<AElysiumWorldCollisionActor>();
		Test.TestEqual(TEXT("actual core payload carries the required fifth answer version"), Payload->PayloadVersion, 3);
		if (!Test.TestNotNull(TEXT("the world-collision actor spawns"), Actor))
		{
			return false;
		}
		Test.TestEqual(TEXT("one native body per recorded partition"),
			Actor->AuthorFromPayload(Payload), Payload->GetWorldBodies().Num());

		using ESig = EElysiumContentsSignature;
		for (const FExpectedBody& Row : Expected)
		{
			int32 SignatureHulls = 0;
			int32 CommonHulls = 0;
			TArray<UElysiumWorldCollisionComponent*> SignatureBodies;
			for (UElysiumWorldCollisionComponent* Candidate : Actor->Bodies)
			{
				if (Candidate != nullptr && Candidate->Signature == Row.Signature)
				{
					SignatureBodies.Add(Candidate);
					SignatureHulls += Candidate->Body ? Candidate->Body->AggGeom.ConvexElems.Num() : 0;
					if (Candidate->bCommonSolid) CommonHulls += Candidate->Body ? Candidate->Body->AggGeom.ConvexElems.Num() : 0;
				}
			}
			if (!Test.TestTrue(*FString::Printf(TEXT("%s has native bodies"), Row.Spelled), !SignatureBodies.IsEmpty()))
			{
				continue;
			}
			Test.TestEqual(*FString::Printf(TEXT("%s carries every staged hull exactly once"), Row.Spelled), SignatureHulls, Row.Hulls);
			Test.TestEqual(*FString::Printf(TEXT("%s common partition matches independent raw contents census"), Row.Spelled), CommonHulls, Row.CommonHulls);
			for (UElysiumWorldCollisionComponent* Component : SignatureBodies)
			{
			const TArray<FKConvexElem>& Convexes = Component->Body->AggGeom.ConvexElems;

			const ESig Signature = static_cast<ESig>(Row.Signature);
			Test.TestEqual(TEXT("real partition preserves its generated collision profile name"), Component->GetCollisionProfileName(), ElysiumContents::ProfileName(Signature));
			const bool bPlayer = EnumHasAnyFlags(Signature, ESig::Player);
			const bool bNpc = EnumHasAnyFlags(Signature, ESig::Npc);
			const bool bSight = EnumHasAnyFlags(Signature, ESig::Sight);

			// Navigation relevance is the same fact as the N answer, asserted on the real body.
			Test.TestEqual(*FString::Printf(TEXT("%s cuts the NavMesh exactly when it blocks an NPC"),
				Row.Spelled), Component->IsNavigationRelevant(), bNpc);

			// Every brush of the body, not a sample: the claim is about the map's brushes, and
			// the smallest body here is five hulls while the largest is a few thousand spheres.
			int32 WrongPlayer = 0;
			int32 WrongNpc = 0;
			int32 WrongSight = 0;
			int32 WrongCommon = 0;
			const auto BaseRecipe = ElysiumRetailMask::Recipe(0x200400b);
			const auto BaseIgnore = ElysiumRetailMask::QueryIgnoreMask(BaseRecipe, false);
			for (const FKConvexElem& Convex : Convexes)
			{
				const FVector Point = Centroid(Convex);
				WrongPlayer += ComponentBlocks(World, Component, Point,
					ElysiumCollision::PlayerChannel) != bPlayer;
				WrongNpc += ComponentBlocks(World, Component, Point, ECC_Pawn) != bNpc;
				WrongSight += ComponentBlocks(World, Component, Point,
					ElysiumCollision::SightChannel) != bSight;
				WrongCommon += ComponentBlocks(World, Component, Point, BaseRecipe.Channel, BaseIgnore) != Component->bCommonSolid;
			}
			Test.TestEqual(*FString::Printf(TEXT("%s: brushes answering the PLAYER wrongly"),
				Row.Spelled), WrongPlayer, 0);
			Test.TestEqual(*FString::Printf(TEXT("%s: brushes answering an NPC wrongly"),
				Row.Spelled), WrongNpc, 0);
			Test.TestEqual(*FString::Printf(TEXT("%s: brushes answering SIGHT wrongly"),
				Row.Spelled), WrongSight, 0);
			Test.TestEqual(*FString::Printf(TEXT("%s: actual base237 common-solid native answers"), Row.Spelled), WrongCommon, 0);
			}
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCommonSolidWorldInputTest,
	"Elysium.Content.MapCollision.CommonSolidInput", GElysiumMapCollisionContentFlags)
bool FElysiumCommonSolidWorldInputTest::RunTest(const FString&)
{
	FPlayerWorldFixture Fixture;
	if (!Fixture.CreateWorld(*this)) return false;
	UElysiumMapCollisionPayload* Payload = NewObject<UElysiumMapCollisionPayload>();
	Payload->MapName = TEXT("common_solid_native_input");
	// Window0x2 and dual-clip-only0x30000 share PN3; the extra baked answer differs.
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FElysiumCollisionHull Hull;
		for (float X : {-20.f,20.f}) for (float Y : {-20.f,20.f}) for (float Z : {-20.f,20.f})
			Hull.Vertices.Add(FVector(X+Index*200.f,Y,Z+1000.f));
		Payload->AuthorWorldBody(3, {Hull}, Index == 0);
	}
	if (!TestTrue(TEXT("actual partition cooks native convex geometry"),Payload->CreatePhysicsMeshes())) return false;
	AElysiumWorldCollisionActor* Actor = Fixture.World->SpawnActor<AElysiumWorldCollisionActor>();
	if (!Actor || !TestEqual(TEXT("same-signature inputs create two distinct native bodies"),Actor->AuthorFromPayload(Payload),2)) return false;
	TestEqual(TEXT("both native bodies retain the same established PN profile"),Actor->Bodies[0]->GetCollisionProfileName(),Actor->Bodies[1]->GetCollisionProfileName());
	const auto Recipe = ElysiumRetailMask::Recipe(0x200400b);
	FCollisionQueryParams Params(FName(TEXT("ElysiumCommonSolidNative")),false);
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const FVector Centre(Index*200.f,0,1000.f);
		Params.IgnoreMask = 0;
		if (!TestTrue(TEXT("raw native query reaches each actual cooked body"),Fixture.World->OverlapBlockingTestByChannel(Centre,FQuat::Identity,Recipe.Channel,FCollisionShape::MakeSphere(1.f),Params))) return false;
		Params.IgnoreMask = ElysiumRetailMask::QueryIgnoreMask(Recipe,false);
		TestEqual(TEXT("base237 native query includes common window and excludes dual-clip-only"),Fixture.World->OverlapBlockingTestByChannel(Centre,FQuat::Identity,Recipe.Channel,FCollisionShape::MakeSphere(1.f),Params),Index==0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMapCollisionTutorialTest,
	"Elysium.Content.MapCollision.Tutorial", GElysiumMapCollisionContentFlags)
bool FElysiumMapCollisionTutorialTest::RunTest(const FString&)
{
	// STAGED world hulls, which is what the payload carries. The spec's census pins (2,725 / 309
	// / 17 / 5) count every BRUSH including brush-entity brushes and the 3D skybox's; the world
	// body holds model 0's brushes after the sky-centroid drop and the four-vertex floor.
	static const FExpectedBody Expected[] =
	{
		{ TEXT("PNS-"), 0x7, 2128, 2128 },
		{ TEXT("PN--"), 0x3, 243, 210 }, //33 dual-clip-only hulls share PN but not base237's common answer
		{ TEXT("--S-"), 0x4, 15, 0 },    // sight-only: stops a sight trace and neither pawn
		{ TEXT("-N--"), 0x2, 5, 0 },     // the NPC-only clips: stop an NPC and not the player
	};
	return RunMap(*this, TEXT("sp_tutorial_1"), Expected);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMapCollisionHubTest,
	"Elysium.Content.MapCollision.Hub", GElysiumMapCollisionContentFlags)
bool FElysiumMapCollisionHubTest::RunTest(const FString&)
{
	static const FExpectedBody Expected[] =
	{
		{ TEXT("PNS-"), 0x7, 3662, 3662 },
		{ TEXT("PN--"), 0x3, 87, 87 },
		{ TEXT("PN-p"), 0xb, 93, 0 },
		{ TEXT("-N-p"), 0xa, 31, 0 },
		{ TEXT("---p"), 0x8, 9, 0 },     // the roadway: priced for a pedestrian, solid to nobody
	};
	return RunMap(*this, TEXT("sm_hub_1"), Expected);
}

#endif
