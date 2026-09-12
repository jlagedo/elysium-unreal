// The interest predicate `CAI_BaseNPCTroika::0x102b3270` (`ElysiumNpcCond::ShouldInvestigate`):
// the `investigate_mode` / `investigate_mode_combat` switch, the flag reject, and the enemy
// override, asserted directly on a headless world. Of its three retail callers the sound sweep is
// now built and drives the predicate for real in
// `Elysium.Substrate.NpcConditions.SoundSweepArms`; the see-unknown sweep (story 10b) and the
// vision producer are not, so the direct assertions here remain the only cover for the rest.
//
// `docs/vtmb/npc-ai-reverse-engineering.md` -> "The interest predicate" owns every fact here.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumRelationships.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumTestServices.h"

namespace ElysiumNpcInvestigateTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

using EMode = EElysiumInvestigateMode;

namespace
{
	// One NPC, one other NPC it can be asked about, the player. No model: the predicate reads
	// relations, flags and memory, never a body.
	struct FInvestigateFixture
	{
		FElysiumNpcWorldFixture Fixture;
		FElysiumRecordingServices& Services;
		FElysiumEntityWorld& World;
		FElysiumNpc* Npc = nullptr;
		FElysiumNpc* Other = nullptr;
		FElysiumPlayer* Player = nullptr;

		static FElysiumNpcWorldBuilder BuildWorld()
		{
			FElysiumNpcWorldBuilder Builder(TEXT("__investigate_test__"), 0x494e5647);
			for (const TCHAR* Name : { TEXT("npc"), TEXT("other") })
			{
				Builder.AddNpc(Name);
			}
			return Builder;
		}

		FInvestigateFixture()
			: Fixture(BuildWorld())
			, Services(Fixture.Services)
			, World(Fixture.World)
		{
			Npc = Fixture.Npc(TEXT("npc"));
			Other = Fixture.Npc(TEXT("other"));
			Player = Fixture.Player();
		}

		void Relate(const FElysiumEntity& Target, EElysiumRelationship Value)
		{
			Npc->Relationships.SetEntity(Target.Handle, Value, 5);
		}

		bool Ask(const FElysiumEntity& Candidate, bool bCombat = false) const
		{
			return ElysiumNpcCond::ShouldInvestigate(*Npc, Candidate, bCombat);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcInvestigateModesTest,
	"Elysium.Substrate.NpcConditions.InvestigateModes", GElysiumTestFlags)
bool FElysiumNpcInvestigateModesTest::RunTest(const FString&)
{
	FInvestigateFixture F;
	if (!TestNotNull(TEXT("npc"), F.Npc) || !TestNotNull(TEXT("other"), F.Other)
		|| !TestNotNull(TEXT("player"), F.Player))
	{
		return false;
	}

	// The seven modes are a product of {players, anything} x {hated, non-neutral, any}, plus never.
	// Rows: mode; columns: (neutral player, hated player, neutral NPC, hated NPC).
	struct FRow
	{
		EMode Mode;
		bool NeutralPlayer, HatedPlayer, NeutralNpc, HatedNpc;
	};
	const FRow Rows[] = {
		{ EMode::Never,             false, false, false, false },
		{ EMode::HatedPlayers,      false, true,  false, false },
		{ EMode::NonNeutralPlayers, false, true,  false, false },
		{ EMode::AnyPlayer,         true,  true,  false, false },
		{ EMode::Hated,             false, true,  false, true  },
		{ EMode::NonNeutral,        false, true,  false, true  },
		{ EMode::Anything,          true,  true,  true,  true  },
	};
	for (const FRow& Row : Rows)
	{
		F.Npc->InvestigateMode = static_cast<int32>(Row.Mode);
		const FString Name = FString::Printf(TEXT("mode %d"), static_cast<int32>(Row.Mode));

		F.Relate(*F.Player, EElysiumRelationship::Neutral);
		F.Relate(*F.Other, EElysiumRelationship::Neutral);
		TestEqual(*(Name + TEXT(": neutral player")), F.Ask(*F.Player), Row.NeutralPlayer);
		TestEqual(*(Name + TEXT(": neutral NPC")), F.Ask(*F.Other), Row.NeutralNpc);

		F.Relate(*F.Player, EElysiumRelationship::Hate);
		F.Relate(*F.Other, EElysiumRelationship::Hate);
		TestEqual(*(Name + TEXT(": hated player")), F.Ask(*F.Player), Row.HatedPlayer);
		TestEqual(*(Name + TEXT(": hated NPC")), F.Ask(*F.Other), Row.HatedNpc);
	}

	// `NonNeutral` admits fear and like as well as hate -- it is `!= D_NU`, not `== D_HT`.
	F.Npc->InvestigateMode = static_cast<int32>(EMode::NonNeutral);
	F.Relate(*F.Other, EElysiumRelationship::Fear);
	TestTrue(TEXT("non-neutral admits a feared NPC"), F.Ask(*F.Other));
	F.Npc->InvestigateMode = static_cast<int32>(EMode::Hated);
	TestFalse(TEXT("hated does not admit a feared NPC"), F.Ask(*F.Other));

	// An unrecognised mode is retail's warning and a refusal.
	AddExpectedError(TEXT("unrecognised investigate mode"), EAutomationExpectedErrorFlags::Contains, 1);
	F.Npc->InvestigateMode = 9;
	F.Relate(*F.Other, EElysiumRelationship::Hate);
	TestFalse(TEXT("mode 9 refuses"), F.Ask(*F.Other));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcInvestigateGatesTest,
	"Elysium.Substrate.NpcConditions.InvestigateGates", GElysiumTestFlags)
bool FElysiumNpcInvestigateGatesTest::RunTest(const FString&)
{
	FInvestigateFixture F;
	if (!TestNotNull(TEXT("npc"), F.Npc) || !TestNotNull(TEXT("other"), F.Other)
		|| !TestNotNull(TEXT("player"), F.Player))
	{
		return false;
	}
	F.Relate(*F.Other, EElysiumRelationship::Hate);

	// The two operands: `bCombatMode` selects `investigate_mode_combat`.
	F.Npc->InvestigateMode = static_cast<int32>(EMode::Never);
	F.Npc->InvestigateModeCombat = static_cast<int32>(EMode::Hated);
	TestFalse(TEXT("investigate_mode answers the ordinary ask"), F.Ask(*F.Other, false));
	TestTrue(TEXT("investigate_mode_combat answers the combat ask"), F.Ask(*F.Other, true));

	// The first line of the body: `DONT_INVESTIGATE | IN_FLEE_SCHED` refuses before any mode.
	F.Npc->InvestigateMode = static_cast<int32>(EMode::Anything);
	F.Npc->NpcFlags.Set(EElysiumNpcFlag::DONT_INVESTIGATE);
	TestFalse(TEXT("DONT_INVESTIGATE refuses even mode 6"), F.Ask(*F.Other));
	F.Npc->NpcFlags.Clear(EElysiumNpcFlag::DONT_INVESTIGATE);
	F.Npc->NpcFlags.Set(EElysiumNpcFlag::IN_FLEE_SCHED);
	TestFalse(TEXT("IN_FLEE_SCHED refuses even mode 6"), F.Ask(*F.Other));
	F.Npc->NpcFlags.Clear(EElysiumNpcFlag::IN_FLEE_SCHED);

	// The committed enemy is always of interest, whatever the mode says.
	F.Npc->InvestigateMode = static_cast<int32>(EMode::Never);
	F.Npc->Senses.Memory.Enemy = F.Other->Handle;
	TestTrue(TEXT("the committed enemy is of interest under mode 0"), F.Ask(*F.Other));
	// ...but the flag reject still comes first.
	F.Npc->NpcFlags.Set(EElysiumNpcFlag::DONT_INVESTIGATE);
	TestFalse(TEXT("DONT_INVESTIGATE outranks the enemy override"), F.Ask(*F.Other));
	return true;
}

}   // namespace ElysiumNpcInvestigateTests

#endif // WITH_DEV_AUTOMATION_TESTS
