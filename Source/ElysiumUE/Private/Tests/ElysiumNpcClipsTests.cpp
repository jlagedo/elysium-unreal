// Content-free Substrate automation: the NPC clip-slice column contract.
//
// The slice's row is positional and TRUNCATED at its last stated column
// (`pipeline/src/elysium_pipeline/exporters/npc_export.py`), so a row's length is data: a seven-column
// row states no reach and no blocked reaction, and a nine-column row whose reach is a JSON null
// states a reaction and no reach. Nothing here reads an export — the slice is a string on the stack,
// which is the only way the short, long and null shapes can all be asserted in one place.
//
// The two melee columns are `mstudioseqdesc_t`+0x2D0 and +0x2E0
// (`docs/vtmb/combat-and-damage.md` § "Target acquisition, sequence commit and recovery" and
// § "Block and stagger reactions").
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Visual/ElysiumNpcClips.h"

namespace ElysiumNpcClipsTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// One synthetic stem carrying the five row shapes the schema admits, plus one row the parser must
	// refuse. `andrei_rAttack`'s real numbers stand in the nine-column case so the fixture is a shipped
	// row rather than an invented one.
	const TCHAR* const GSlice = TEXT(R"({
		"stem":"reach_body",
		"owners":["melee_bank"],
		"activities":["ACT_MELEE_ATTACK","ACT_IDLE"],
		"fields":["owner","activity","weight","flags","frames","fps","fade","reach_cm",
		          "blocked_reaction"],
		"clips":{
			"swing_short":[0,0,1,0,31,30.0,0.2],
			"swing_mid":[0,0,1,0,31,30.0,0.2,120.5],
			"swing_long":[0,0,1,0,31,30.0,0.2,282.9939,"ACT_BLOCKED_REACTION_LEFT"],
			"swing_noreach":[0,0,1,0,31,30.0,0.2,null,"ACT_BLOCKED_REACTION_RIGHT"],
			"idle01":[0,1,30,1,30,30.0,0.2],
			"truncated":[0,0]
		}
	})");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumClipsMeleeColumnsTest,
	"Elysium.Substrate.Clips.MeleeColumns", GElysiumTestFlags)
bool FElysiumClipsMeleeColumnsTest::RunTest(const FString&)
{
	// The six-column minimum is what the parser refuses below, and it says so once per slice with a
	// count rather than once per row.
	AddExpectedError(TEXT("npc clips 'reach_body': 1 malformed row"),
		EAutomationExpectedErrorFlags::Contains, 1);

	FElysiumNpcClipSet Set;
	FString Error;
	if (!TestTrue(TEXT("the synthetic slice parses"),
		Set.LoadJsonText(TEXT("reach_body"), GSlice, Error)))
	{
		AddError(FString::Printf(TEXT("slice refused: %s"), *Error));
		return false;
	}
	TestEqual(TEXT("the row shorter than the six mandatory columns is dropped, not defaulted"),
		Set.Clips.Num(), 5);

	// --- The short row: the columns simply are not there ------------------------------------------
	const FElysiumNpcClip* Short = Set.Find(TEXT("swing_short"));
	if (Short == nullptr)
	{
		AddError(TEXT("the seven-column row did not parse"));
		return false;
	}
	TestEqual(TEXT("a row stating no reach reads as no claim"), Short->ReachCm, 0.0f);
	TestFalse(TEXT("and answers HasReach false"), Short->HasReach());
	TestTrue(TEXT("and names no blocked reaction"), Short->BlockedReaction.IsEmpty());
	TestEqual(TEXT("while the columns before it still read"), Short->Fade, 0.2f);

	// --- The eight-column row: a reach with no reaction behind it ---------------------------------
	const FElysiumNpcClip* Mid = Set.Find(TEXT("swing_mid"));
	if (Mid == nullptr)
	{
		AddError(TEXT("the eight-column row did not parse"));
		return false;
	}
	TestEqual(TEXT("the reach column reads"), Mid->ReachCm, 120.5f);
	TestTrue(TEXT("and a stated reach answers HasReach"), Mid->HasReach());
	TestTrue(TEXT("with the reaction column still absent"), Mid->BlockedReaction.IsEmpty());

	// --- The nine-column row: both, off a shipped sequence -----------------------------------------
	const FElysiumNpcClip* Long = Set.Find(TEXT("swing_long"));
	if (Long == nullptr)
	{
		AddError(TEXT("the nine-column row did not parse"));
		return false;
	}
	TestEqual(TEXT("andrei_rAttack's authored reach"), Long->ReachCm, 282.9939f, 0.001f);
	TestEqual(TEXT("and the reaction its descriptor stores"), Long->BlockedReaction,
		FString(TEXT("ACT_BLOCKED_REACTION_LEFT")));

	// --- The null reach: the reaction column's placeholder ------------------------------------------
	// `reach_cm` holds the slot `blocked_reaction` sits behind, so a sequence naming a reaction and no
	// reach writes a JSON null there. Reading it as a number would answer 0 either way; the point is
	// that it must not be read as a number at all.
	const FElysiumNpcClip* NoReach = Set.Find(TEXT("swing_noreach"));
	if (NoReach == nullptr)
	{
		AddError(TEXT("the null-reach row did not parse"));
		return false;
	}
	TestEqual(TEXT("a null reach is no claim"), NoReach->ReachCm, 0.0f);
	TestFalse(TEXT("and answers HasReach false"), NoReach->HasReach());
	TestEqual(TEXT("while its reaction still lands in the right column"), NoReach->BlockedReaction,
		FString(TEXT("ACT_BLOCKED_REACTION_RIGHT")));

	// --- The maximum over the answering sequences --------------------------------------------------
	// Retail queries `FindEntityFOV` at the largest reach the translated activity returns, so the long
	// variant sets the distance even when the weighted pick would have landed on the short one.
	TestEqual(TEXT("the activity's reach is the maximum over its four answering clips"),
		Set.MaxReachCmForActivity(TEXT("ACT_MELEE_ATTACK")), 282.9939f, 0.001f);
	TestEqual(TEXT("asked case-insensitively, as every other activity query is"),
		Set.MaxReachCmForActivity(TEXT("act_melee_attack")), 282.9939f, 0.001f);
	TestEqual(TEXT("an activity whose clips state no reach answers 0"),
		Set.MaxReachCmForActivity(TEXT("ACT_IDLE")), 0.0f);
	TestEqual(TEXT("and so does an activity this vocabulary cannot answer at all"),
		Set.MaxReachCmForActivity(TEXT("ACT_KICK")), 0.0f);
	TestEqual(TEXT("an empty activity is not a query"),
		Set.MaxReachCmForActivity(FString()), 0.0f);

	return true;
}
}   // namespace ElysiumNpcClipsTests

#endif   // WITH_DEV_AUTOMATION_TESTS
