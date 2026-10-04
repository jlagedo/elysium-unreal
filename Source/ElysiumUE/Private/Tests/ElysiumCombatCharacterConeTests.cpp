// Content-free Substrate automation: slot 363 on the combat character,
// `CBaseCombatCharacter::FInViewCone(CBaseEntity*)` `0x10326750`, and the threshold its cone body
// `FinViewCone3dNew` `0x103264d0` compares against: the OBSERVER's `m_flFieldOfView` (`+0x1574`).
//
// This is the body the player answers `GatherEnemyConditions` (`0x1027106c`) with, which is what
// decides `ENEMY_FACING_ME 0x56` against `BEHIND_ENEMY 0x57`. The authored facts:
// `docs/vtmb/npc-ai/senses.md` -> "Cone".

#include "Misc/AutomationTest.h"

#include "Tests/ElysiumArmTier.h"

#if WITH_DEV_AUTOMATION_TESTS && ELYSIUM_WITH_ARM_TESTS

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumTestServices.h"

namespace ElysiumCombatCharacterConeTests
{
static constexpr EAutomationTestFlags GConeTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// A ground position `Units` away, `Degrees` off the +X axis an observer with zero angles faces.
	FVector Around(float Degrees, float Units)
	{
		const float Radians = FMath::DegreesToRadians(Degrees);
		return FVector(Units * ElysiumMove::U * FMath::Cos(Radians),
			Units * ElysiumMove::U * FMath::Sin(Radians), 0.0);
	}

	struct FConeFixture
	{
		FElysiumNpcWorldFixture Fixture;
		FElysiumNpc* Guard = nullptr;
		FElysiumPlayer* Player = nullptr;

		static FElysiumNpcWorldBuilder BuildWorld()
		{
			FElysiumNpcWorldBuilder Builder(TEXT("__combatcharactercone_test__"), 0x434F4E45);
			Builder.AddNpc(TEXT("guard"));
			return Builder;
		}

		FConeFixture()
			: Fixture(BuildWorld())
		{
			Guard = Fixture.Npc(TEXT("guard"));
			Player = Fixture.Player();
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCombatCharacterFInViewConeTest,
	"Elysium.Arm.CombatCharacter.FInViewCone", GConeTestFlags)
bool FElysiumCombatCharacterFInViewConeTest::RunTest(const FString&)
{
	FConeFixture F;
	if (!TestNotNull(TEXT("the guard spawned"), F.Guard)
		|| !TestNotNull(TEXT("the player exists"), F.Player))
	{
		return false;
	}
	FElysiumPlayer& Player = *F.Player;
	FElysiumNpc& Guard = *F.Guard;
	FElysiumEntity* const GuardEntity = F.Guard;
	FElysiumEntity* const PlayerEntity = F.Player;

	// --- The player observes: `m_flFieldOfView` 0.5 (`CBasePlayer::Spawn 0x1016d260`) -----------
	// Set here, not taken from the spawn: the cone is what is under test.
	Player.Origin = FVector::ZeroVector;
	Player.Angles = FVector::ZeroVector;   // facing +X in Source angles
	Player.FieldOfView = 0.5f;
	// The candidate's slot 29 word, `m_flStealthVisionCone` (`+0x63c8`), at the neutral 1.0 the
	// per-pass reset `0x1028fca5` leaves it on.
	Guard.Senses.StealthVisionCone = 1.0f;

	Guard.Origin = Around(0.f, 300.f);
	TestTrue(TEXT("0x10326750: an NPC 300 units straight ahead of the player is in its cone"),
		Player.FInViewCone(GuardEntity));

	// 75 degrees off: the cosine from the pulled-back apex is about 0.38 -- inside a 0.2 cone,
	// outside the player's 0.5. The threshold is the observer's own word.
	Guard.Origin = Around(75.f, 300.f);
	TestFalse(TEXT("0x103266a0: 75 degrees off is outside the player's 0.5"),
		Player.FInViewCone(GuardEntity));
	Player.FieldOfView = 0.2f;
	TestTrue(TEXT("0x103266a0: ...and inside the same body at 0.2 (+0x1574 is read)"),
		Player.FInViewCone(GuardEntity));
	Player.FieldOfView = 0.5f;

	Guard.Origin = Around(100.f, 300.f);
	TestFalse(TEXT("0x103265af: 100 degrees off the player's forward is out"),
		Player.FInViewCone(GuardEntity));
	Guard.Origin = Around(180.f, 300.f);
	TestFalse(TEXT("0x103265af: behind the player (dot < 0) is out"),
		Player.FInViewCone(GuardEntity));

	// The candidate's slot 29 (`0x101aa630`) multiplies the cosine (`0x1032669c`): the word is
	// read, not assumed.
	Guard.Origin = Around(0.f, 300.f);
	Guard.Senses.StealthVisionCone = 0.25f;
	TestFalse(TEXT("0x1032669c: cosine x 0.25 is under the player's 0.5"),
		Player.FInViewCone(GuardEntity));
	Guard.Senses.StealthVisionCone = 1.0f;

	// Retail dereferences the candidate; the port's guard answers false.
	TestFalse(TEXT("a null candidate answers false"),
		Player.FInViewCone(static_cast<FElysiumEntity*>(nullptr)));

	// --- An NPC observes: the Troika 0.2 (`0x10298de8`), answers unchanged ----------------------
	Guard.Origin = FVector::ZeroVector;
	Guard.Angles = FVector::ZeroVector;
	TestEqual(TEXT("0x10298de8: the Troika line's m_flFieldOfView is 0.2"), Guard.FieldOfView, 0.2f);
	const FVector Eye = Guard.EyePosition();
	TestTrue(TEXT("NPC at 0.2: a point straight ahead is in"),
		FElysiumNpcSenses::IsInViewCone(Guard, Eye + Around(0.f, 300.f)));
	TestTrue(TEXT("NPC at 0.2: 75 degrees off is in (cosine about 0.38)"),
		FElysiumNpcSenses::IsInViewCone(Guard, Eye + Around(75.f, 300.f)));
	TestTrue(TEXT("NPC at 0.2: 85 degrees off is in (cosine about 0.22)"),
		FElysiumNpcSenses::IsInViewCone(Guard, Eye + Around(85.f, 300.f)));
	TestFalse(TEXT("NPC at 0.2: 88 degrees off is out (cosine about 0.17)"),
		FElysiumNpcSenses::IsInViewCone(Guard, Eye + Around(88.f, 300.f)));
	TestFalse(TEXT("NPC at 0.2: behind is out"),
		FElysiumNpcSenses::IsInViewCone(Guard, Eye + Around(180.f, 300.f)));

	// The base body on an NPC observer with the player as candidate: slot 29 on `CHL2_Player`
	// is `0x1034f390` -> `m_flStealthVisionCone` (`+0x1c74`).
	Player.Origin = Around(75.f, 300.f);
	Player.Stealth.ConeScalar = 1.0f;
	TestTrue(TEXT("0x10326750 on an NPC: the player 75 degrees off is inside 0.2"),
		Guard.FElysiumCombatCharacter::FInViewCone(PlayerEntity));
	Player.Stealth.ConeScalar = 0.25f;
	TestFalse(TEXT("0x1034f390: the player's own cone scalar (+0x1c74) multiplies the cosine"),
		Guard.FElysiumCombatCharacter::FInViewCone(PlayerEntity));
	return true;
}
}   // namespace ElysiumCombatCharacterConeTests

#endif   // WITH_DEV_AUTOMATION_TESTS && ELYSIUM_WITH_ARM_TESTS
