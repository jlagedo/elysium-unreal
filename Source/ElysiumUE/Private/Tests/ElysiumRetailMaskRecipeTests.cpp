#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumCollisionChannels.h"
#include "ElysiumContentsSignature.h"
#include "Map/ElysiumRetailMaskRecipe.h"

// 0018 story 6: every retail mask the stand test, the fit trace, FVisible, the cover validator,
// IsAreaClear and the lateral pre-check push, turned into the channel the world answer traces on and
// the entity kinds it may meet. One row per mask; the rule is `ElysiumRetailMask::Recipe`'s.

static constexpr EAutomationTestFlags GElysiumRetailMaskRecipeFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumRetailMaskRecipeTest,
	"Elysium.Arm.Geometry.MaskRecipe", GElysiumRetailMaskRecipeFlags)
bool FElysiumRetailMaskRecipeTest::RunTest(const FString&)
{
	struct FRow
	{
		const TCHAR* Name;
		int32 Mask;
		ECollisionChannel Channel;
		bool bCharacters;
		bool bMovers;
		bool bProps;
		bool bNothing;
	};
	const ECollisionChannel Sight = ElysiumCollision::SightChannel;
	const ECollisionChannel Player = ElysiumCollision::PlayerChannel;
	const FRow Rows[] = {
		// FVisible, the cover search, the shoot node, ValidateNavGoal, the lateral pre-check.
		{ TEXT("0x2804091 sight and characters"), 0x2804091, Sight, true, true, true, false },
		// Its brush half, the signature's S mask.
		{ TEXT("0x804091 sight brushes"), static_cast<int32>(ElysiumContents::SightMask), Sight, false, true, false, false },
		{ TEXT("0x4081 opaque brushes"), 0x4081, Sight, false, true, false, false },
		// MASK_NPCSOLID: IsValidCover, IsAreaClear, CanStandAt.
		{ TEXT("0x202400b NPC-solid and characters"), 0x202400b, ECC_Pawn, true, true, true, false },
		// CanFitAtNode, the wander probe: NPCs not solid.
		{ TEXT("0x2400b NPC-solid brushes"), static_cast<int32>(ElysiumContents::NpcMask), ECC_Pawn, false, true, false, false },
		// InitLinks: no MOVEABLE, so movers do not answer.
		{ TEXT("0x2000b InitLinks"), 0x2000b, ECC_Pawn, false, false, false, false },
		// MASK_PLAYERSOLID: FindVictim, CanStartGrappleAttack.
		{ TEXT("0x201400b player-solid and characters"), 0x201400b, Player, true, true, true, false },
		{ TEXT("0x1400b player-solid brushes"), static_cast<int32>(ElysiumContents::PlayerMask), Player, false, true, false, false },
		// MASK_SHOT, the slot-68 exemption's key: no OPAQUE, PLAYERCLIP or MONSTERCLIP, so the
		// fallback channel.
		{ TEXT("0x46004003 shot"), 0x46004003, ECC_Pawn, true, true, true, false },
		// Mask 0 traces nothing.
		{ TEXT("0 nothing"), 0, ECC_Pawn, false, false, false, true },
	};
	for (const FRow& Row : Rows)
	{
		TestTrue(FString::Printf(TEXT("%s is a recovered mask"), Row.Name),
			ElysiumRetailMask::IsListed(Row.Mask));
		const FElysiumRetailMaskRecipe Recipe = ElysiumRetailMask::Recipe(Row.Mask);
		TestEqual(FString::Printf(TEXT("%s: nothing"), Row.Name), Recipe.bNothing, Row.bNothing);
		TestEqual(FString::Printf(TEXT("%s: characters"), Row.Name), Recipe.bCharacters, Row.bCharacters);
		TestEqual(FString::Printf(TEXT("%s: movers"), Row.Name), Recipe.bMovers, Row.bMovers);
		TestEqual(FString::Printf(TEXT("%s: props"), Row.Name), Recipe.bProps, Row.bProps);
		if (!Row.bNothing)
		{
			TestEqual(FString::Printf(TEXT("%s: channel"), Row.Name),
				static_cast<int32>(Recipe.Channel), static_cast<int32>(Row.Channel));
		}
	}
	// The two primitive bits are distinct, non-zero `FMaskFilter` values.
	TestTrue(TEXT("the character and mover bits differ"),
		ElysiumRetailMask::CharacterMaskBit != ElysiumRetailMask::MoverMaskBit
			&& ElysiumRetailMask::CharacterMaskBit != 0 && ElysiumRetailMask::MoverMaskBit != 0);
	return true;
}

#endif
