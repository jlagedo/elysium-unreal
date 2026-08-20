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

#include "ElysiumAnimationIntent.h"
#include "Visual/ElysiumAnimationResolve.h"
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

	// The tenth column: the authored swing-contact records. Two shipped shapes stand here — a
	// sequence stating a reach, a reaction and its records, and one whose reaction column is the
	// empty placeholder the exporter writes when only the records are stated. `degenerate` rides on
	// `fists_attack_heavy`'s own backwards window, one of the four in the whole install.
	const TCHAR* const GSwingSlice = TEXT(R"({
		"stem":"swing_body",
		"owners":["melee_bank"],
		"activities":["ACT_MELEE_ATTACK"],
		"fields":["owner","activity","weight","flags","frames","fps","fade","reach_cm",
		          "blocked_reaction","swings"],
		"clips":{
			"nocolumn":[0,0,1,0,31,30.0,0.2,120.5,"ACT_BLOCKED_REACTION_LEFT"],
			"nullcolumn":[0,0,1,0,31,30.0,0.2,120.5,"",null],
			"emptycolumn":[0,0,1,0,31,30.0,0.2,120.5,"",[]],
			"swing_two":[0,0,1,0,31,30.0,0.2,282.9939,"ACT_BLOCKED_REACTION_LEFT",[
				{"start":0.2,"end":0.45,"bone":"Bip01 R Hand",
				 "a_cm":[0.0,0.0,0.0],"b_cm":[27.94,-1.27,0.0],
				 "kb_names":[["ACT_KNOCKBACK_BIGHIGHRIGHT_MELEESHARED_ONEHAND"],[],[],[]],
				 "b8":3,"ba":255,"degenerate":false},
				{"start":0.302,"end":0.0,"bone":"Bip01 L Hand",
				 "a_cm":[1.0,2.0,3.0],"b_cm":[4.0,5.0,6.0],
				 "kb_names":[[],[],[],[]],
				 "b8":255,"ba":2,"degenerate":true}]]
		}
	})");
}

// The tenth column, and the compatibility rule behind it: absent, null and empty are one answer.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumClipsSwingColumnTest,
	"Elysium.Substrate.Clips.SwingColumn", GElysiumTestFlags)
bool FElysiumClipsSwingColumnTest::RunTest(const FString&)
{
	FElysiumNpcClipSet Set;
	FString Error;
	if (!TestTrue(TEXT("the synthetic slice parses"),
		Set.LoadJsonText(TEXT("swing_body"), GSwingSlice, Error)))
	{
		AddError(FString::Printf(TEXT("slice refused: %s"), *Error));
		return false;
	}

	// --- The column absent, null and empty are ONE answer: this clip has no contact --------------
	// It is the whole compatibility rule. A slice written before the column existed ends at nine, and
	// the guarded read has to leave it at "no records" rather than misread the row.
	for (const TCHAR* Label : { TEXT("nocolumn"), TEXT("nullcolumn"), TEXT("emptycolumn") })
	{
		const FElysiumNpcClip* Clip = Set.Find(Label);
		if (Clip == nullptr)
		{
			AddError(FString::Printf(TEXT("row '%s' did not parse"), Label));
			return false;
		}
		TestTrue(FString::Printf(TEXT("'%s' states no swing records"), Label),
			Clip->Swings.IsEmpty() && !Clip->HasSwings());
		// The column the swings sit behind still reads, which is what proves the row was not shifted.
		TestEqual(FString::Printf(TEXT("...with its reach still in the right column ('%s')"), Label),
			Clip->ReachCm, 120.5f);
	}

	// --- The stated column, in full ---------------------------------------------------------------
	const FElysiumNpcClip* Two = Set.Find(TEXT("swing_two"));
	if (Two == nullptr || !TestEqual(TEXT("both records parse"), Two->Swings.Num(), 2))
	{
		return false;
	}
	const FElysiumSwingRecord& First = Two->Swings[0];
	TestEqual(TEXT("the window's start"), First.Start, 0.2f);
	TestEqual(TEXT("...and its end"), First.End, 0.45f);
	TestEqual(TEXT("the bone the segment is stated in"), First.Bone, FString(TEXT("Bip01 R Hand")));
	TestTrue(TEXT("the segment's endpoints are read verbatim, in centimetres"),
		First.ACm.Equals(FVector::ZeroVector)
		&& First.BCm.Equals(FVector(27.94, -1.27, 0.0), 0.001));
	TestTrue(TEXT("the record states a sweepable segment"), First.HasSegment());
	TestEqual(TEXT("all four direction buckets are kept, in file order"),
		First.KnockbackNames.Num(), 4);
	if (First.KnockbackNames.Num() == 4)
	{
		TestEqual(TEXT("...bucket 0 naming its one candidate"), First.KnockbackNames[0].Num(), 1);
		TestTrue(TEXT("...and the three empty buckets holding their places"),
			First.KnockbackNames[1].IsEmpty() && First.KnockbackNames[2].IsEmpty()
			&& First.KnockbackNames[3].IsEmpty());
	}
	TestEqual(TEXT("the direction byte bucket 0 answers"), First.B8, 3);
	TestEqual(TEXT("...and the unconditional-knockback marker byte"), First.Ba, 255);
	TestFalse(TEXT("an ordinary window is not degenerate"), First.bDegenerate);

	// --- The degenerate record, carried verbatim ---------------------------------------------------
	// The file states it backwards and the export refuses to repair it, so the parser must not either.
	const FElysiumSwingRecord& Second = Two->Swings[1];
	TestTrue(TEXT("a backwards window is carried as written"),
		Second.End < Second.Start && FMath::IsNearlyEqual(Second.Start, 0.302f));
	TestTrue(TEXT("...and flagged rather than left to be inferred"), Second.bDegenerate);
	TestEqual(TEXT("...with its own bone"), Second.Bone, FString(TEXT("Bip01 L Hand")));
	TestEqual(TEXT("...and the marker byte the range test reads"), Second.Ba, 2);

	return true;
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

	// --- And the value reaches the seam a swing reads it off ---------------------------------------
	// `UElysiumAnimSubsystem::ResolveActivityClip` fills `FElysiumActivityClip::MaxReachCm` by asking
	// this vocabulary for the maximum over the TRANSLATED activity. Composed here rather than through
	// the subsystem, which needs a game instance: the two lines below are exactly what that adapter
	// runs, and the translated name is the resolver's answer rather than the request's — a swing that
	// asked under the pre-translation name would acquire at 0 for every weapon-translated attack.
	{
		FElysiumActivityClipRequest Request;
		Request.Stem = TEXT("reach_body");
		Request.Activity = TEXT("ACT_MELEE_ATTACK");
		Request.Source = EElysiumAnimSource::Player;
		Request.BodyKind = EElysiumAnimBodyKind::Player;

		FElysiumAnimationCatalog Catalog;
		Catalog.Clips = &Set;
		Catalog.BlendTableFor = [](const FString&) -> const FElysiumBlendTable* { return nullptr; };

		FElysiumAnimationSelection Selection;
		ElysiumAnimResolve::Resolve(
			ElysiumAnimResolve::ActivityIntentFor(Request), Catalog, Selection);
		if (TestFalse(TEXT("the melee activity resolves a clip at all"),
			Selection.AnimationName.IsEmpty()))
		{
			TestEqual(TEXT("the reach the seam carries is the resolved activity's maximum"),
				Set.MaxReachCmForActivity(Selection.ResolvedActivity), 282.9939f, 0.001f);
		}
	}

	return true;
}
}   // namespace ElysiumNpcClipsTests

#endif   // WITH_DEV_AUTOMATION_TESTS
