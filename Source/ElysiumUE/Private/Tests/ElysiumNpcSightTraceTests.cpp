// Content-free Substrate automation: `ElysiumNpcSight::Visible`, the trace half of retail's
// `CBaseEntity::FVisible 0x100a6fa0`, and `VisibleTargetOrigin`, its probe table (`0x100a72e0`).
//
// The engine's contribution is one scripted `TraceRetail` answer on the recording services
// (`TraceRetailQuery`): a case states the world fraction, the hit entity and the character list, and
// asserts what retail's filter and verdict make of it. Facts: `docs/vtmb/npc-ai/senses.md` ("FVisible")
// and R2 sections 1, 2 and 5.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityWorld.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcSightTrace.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumTestServices.h"

namespace ElysiumNpcSightTraceTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// A world with the looker `guard`, a third-party `bystander` NPC and the player.
	struct FSightFixture
	{
		FElysiumNpcWorldFixture Fixture;
		FElysiumNpc* Guard = nullptr;
		FElysiumNpc* Bystander = nullptr;
		FElysiumPlayer* Player = nullptr;

		static FElysiumNpcWorldBuilder BuildWorld()
		{
			FElysiumNpcWorldBuilder Builder(TEXT("__npcsight_test__"), 0x53494748);
			Builder.AddNpc(TEXT("guard"));
			Builder.AddNpc(TEXT("bystander"), FVector(500.0, 0.0, 0.0));
			return Builder;
		}

		FSightFixture() : Fixture(BuildWorld())
		{
			Guard = Fixture.Npc(TEXT("guard"));
			Bystander = Fixture.Npc(TEXT("bystander"));
			Player = Fixture.Player();
		}

		ElysiumNpcSight::FVisibleQuery Query(const FElysiumEntityHandle& Target) const
		{
			ElysiumNpcSight::FVisibleQuery Out;
			Out.EyeCm = FVector::ZeroVector;
			Out.TargetCm = FVector(1000.0, 0.0, 0.0);
			Out.Looker = Guard->Handle;
			Out.Target = Target;
			Out.World = &Fixture.World;
			return Out;
		}

		// Script the trace: a clear world and `Characters`, nearest first.
		void Script(std::initializer_list<FElysiumRetailTraceCharacter> Characters,
			float WorldFraction = 1.f, const FElysiumEntityHandle& WorldHit = FElysiumEntityHandle::Invalid())
		{
			TArray<FElysiumRetailTraceCharacter> List(Characters);
			Fixture.Services.TraceRetailQuery =
				[List, WorldFraction, WorldHit, this](const FElysiumRetailTrace& Trace,
					FElysiumRetailTraceResult& Out)
			{
				LastTrace = Trace;
				Out.Fraction = WorldFraction;
				Out.HitEntity = WorldHit;
				for (const FElysiumRetailTraceCharacter& Character : List)
				{
					Out.Characters.Add(Character);
				}
				return true;
			};
		}

		FElysiumRetailTrace LastTrace;
	};

	FElysiumRetailTraceCharacter Body(const FElysiumEntityHandle& Entity, float Fraction)
	{
		FElysiumRetailTraceCharacter Out;
		Out.Entity = Entity;
		Out.Fraction = Fraction;
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcSightTraceTest,
	"Elysium.Substrate.Geometry.Sight.Verdict", GElysiumTestFlags)
bool FElysiumNpcSightTraceTest::RunTest(const FString&)
{
	FSightFixture F;
	if (!TestNotNull(TEXT("the guard constructs"), F.Guard)
		|| !TestNotNull(TEXT("the bystander constructs"), F.Bystander)
		|| !TestNotNull(TEXT("the player exists"), F.Player))
	{
		return false;
	}
	const IElysiumEmbodiment& Embodiment = F.Fixture.Services;
	TestTrue(TEXT("NPCInit set m_bNPCTransparent on every NPC (0x10273394)"), F.Bystander->bNpcTransparent);
	TestFalse(TEXT("the player is not NPC-transparent"), F.Player->bNpcTransparent);

	// --- an NPC between the eyes: FVisible never sees it ------------------------------------------
	{
		F.Script({ Body(F.Bystander->Handle, 0.4f) });
		FElysiumEntityHandle Blocker = FElysiumEntityHandle::Invalid();
		TestTrue(TEXT("an NPC between looker and target does not block (CTraceFilterFVisible)"),
			ElysiumNpcSight::Visible(Embodiment, F.Query(F.Player->Handle), &Blocker));
		TestFalse(TEXT("...and there is no blocker"), Blocker.IsSet());
		TestTrue(TEXT("the looker is the filter's pass entity"),
			F.LastTrace.Ignore.Contains(F.Guard->Handle));
	}

	// --- the same NPC under the lateral pre-check's TwoEnt filter: it blocks ----------------------
	{
		F.Script({ Body(F.Bystander->Handle, 0.4f) });
		ElysiumNpcSight::FVisibleQuery Query = F.Query(F.Player->Handle);
		Query.bNpcsBlock = true;
		FElysiumEntityHandle Blocker;
		TestFalse(TEXT("bNpcsBlock: a third-party NPC blocks (CTraceFilterSimpleTwoEnt)"),
			ElysiumNpcSight::Visible(Embodiment, Query, &Blocker));
		TestTrue(TEXT("...and it is the blocker"), Blocker == F.Bystander->Handle);
	}

	// --- the player between: blocked, the player is the blocker -----------------------------------
	{
		F.Script({ Body(F.Player->Handle, 0.4f), Body(F.Bystander->Handle, 0.8f) });
		FElysiumEntityHandle Blocker;
		TestFalse(TEXT("the player between looker and a point behind it blocks"),
			ElysiumNpcSight::Visible(Embodiment, F.Query(F.Bystander->Handle), &Blocker));
		TestTrue(TEXT("...and the player is the blocker"), Blocker == F.Player->Handle);
	}

	// --- a hit on the target is clear -------------------------------------------------------------
	{
		F.Script({ Body(F.Player->Handle, 0.9f) });
		FElysiumEntityHandle Blocker;
		TestTrue(TEXT("the ray reaching the target's own body is visible (tr.m_pEnt == target)"),
			ElysiumNpcSight::Visible(Embodiment, F.Query(F.Player->Handle), &Blocker));
		TestFalse(TEXT("...with no blocker"), Blocker.IsSet());
	}

	// --- the nearer of the world hit and the first kept character decides -------------------------
	{
		F.Script({ Body(F.Player->Handle, 0.6f) }, 0.3f);
		FElysiumEntityHandle Blocker = F.Player->Handle;
		TestFalse(TEXT("a wall nearer than the player blocks"),
			ElysiumNpcSight::Visible(Embodiment, F.Query(F.Bystander->Handle), &Blocker));
		TestFalse(TEXT("...and the static world is the blocker (Invalid)"), Blocker.IsSet());

		F.Script({ Body(F.Player->Handle, 0.2f) }, 0.3f);
		TestFalse(TEXT("the player nearer than a wall blocks"),
			ElysiumNpcSight::Visible(Embodiment, F.Query(F.Bystander->Handle), &Blocker));
		TestTrue(TEXT("...and it is the blocker"), Blocker == F.Player->Handle);

		F.Script({}, 0.5f, F.Bystander->Handle);
		TestTrue(TEXT("a world hit on the target itself is visible"),
			ElysiumNpcSight::Visible(Embodiment, F.Query(F.Bystander->Handle), nullptr));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcSightTraceHeadlessTest,
	"Elysium.Substrate.Geometry.Sight.Headless", GElysiumTestFlags)
bool FElysiumNpcSightTraceHeadlessTest::RunTest(const FString&)
{
	FSightFixture F;
	if (!TestNotNull(TEXT("the guard constructs"), F.Guard))
	{
		return false;
	}
	const IElysiumEmbodiment& Embodiment = F.Fixture.Services;
	// `TraceRetailQuery` is unset: no collision world, so the brush-only verdict answers.
	F.Fixture.Services.bLineOfSightClear = false;
	FElysiumEntityHandle Blocker = F.Player->Handle;
	TestFalse(TEXT("headless: a blocked brush-only segment is not visible"),
		ElysiumNpcSight::Visible(Embodiment, F.Query(F.Player->Handle), &Blocker));
	TestFalse(TEXT("...and the block is the world's"), Blocker.IsSet());
	F.Fixture.Services.bLineOfSightClear = true;
	TestTrue(TEXT("headless: a clear brush-only segment is visible"),
		ElysiumNpcSight::Visible(Embodiment, F.Query(F.Player->Handle), nullptr));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcSightProbeTest,
	"Elysium.Substrate.Geometry.Sight.Probe", GElysiumTestFlags)
bool FElysiumNpcSightProbeTest::RunTest(const FString&)
{
	// `FVisibleTargetOrigin 0x100a72e0`: a box 20 x 40 x 180 with its feet on the origin.
	const FVector Eye(100.0, 200.0, 170.0);
	const FVector Origin(100.0, 200.0, 50.0);
	const FVector Mins(-10.0, -20.0, 0.0);
	const FVector Maxs(10.0, 20.0, 180.0);
	const FVector Centre(100.0, 200.0, 140.0);
	auto At = [&](int32 Probe)
	{
		return ElysiumNpcSight::VisibleTargetOrigin(Probe, Eye, Origin, Mins, Maxs);
	};

	TestTrue(TEXT("probe 0 aims at the target's eye"), At(0).Equals(Eye));
	TestTrue(TEXT("probe 10 aims at the target's eye"), At(10).Equals(Eye));
	TestTrue(TEXT("a probe above 10 aims at the target's eye"), At(11).Equals(Eye));
	TestTrue(TEXT("probe 1 aims at the OBB centre"), At(1).Equals(Centre));

	// Bottom corner mm (probe 6): x, y at the mins, z pulled to cz + 0.9 * (corner.z - cz).
	const double BottomZ = Centre.Z + 0.9 * (Origin.Z + Mins.Z - Centre.Z);
	TestTrue(TEXT("probe 6 aims at the bottom mm corner, pulled 0.9 toward the centre"),
		At(6).Equals(FVector(90.0, 180.0, BottomZ), 1e-6));
	TestTrue(TEXT("probe 9 is the bottom MM corner"),
		At(9).Equals(FVector(110.0, 220.0, BottomZ), 1e-6));
	const double TopZ = Centre.Z + 0.9 * (Origin.Z + Maxs.Z - Centre.Z);
	TestTrue(TEXT("probe 2 is the top mm corner"), At(2).Equals(FVector(90.0, 180.0, TopZ), 1e-6));
	TestTrue(TEXT("probe 3 is the top mM corner"), At(3).Equals(FVector(90.0, 220.0, TopZ), 1e-6));
	TestTrue(TEXT("probe 4 is the top Mm corner"), At(4).Equals(FVector(110.0, 180.0, TopZ), 1e-6));
	TestTrue(TEXT("probe 5 is the top MM corner"), At(5).Equals(FVector(110.0, 220.0, TopZ), 1e-6));
	TestTrue(TEXT("probe 7 is the bottom mM corner"), At(7).Equals(FVector(90.0, 220.0, BottomZ), 1e-6));
	TestTrue(TEXT("probe 8 is the bottom Mm corner"), At(8).Equals(FVector(110.0, 180.0, BottomZ), 1e-6));
	return true;
}
}

#endif  // WITH_DEV_AUTOMATION_TESTS
