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

	// The eleventh column: the authored combo-chain block. Every row here is a shipped shape —
	// `Fists_attack_W1`'s forward-keyed entry, `fists_attack_JabLeft`'s stated `0`,
	// `knockback_flying_into_back`'s two successors with no mask, and `katana_running_attack`'s
	// window that outlives its own busy hold. `halfcolumn` is the one the parser must refuse.
	const TCHAR* const GComboSlice = TEXT(R"({
		"stem":"combo_body",
		"owners":["melee_bank"],
		"activities":["ACT_MELEE_ATTACK"],
		"fields":["owner","activity","weight","flags","frames","fps","fade","reach_cm",
		          "blocked_reaction","swings","combo"],
		"clips":{
			"nocolumn":[0,0,1,0,31,30.0,0.2,120.5,"",[
				{"start":0.2,"end":0.45,"bone":"Bip01 R Hand",
				 "a_cm":[0.0,0.0,0.0],"b_cm":[27.94,0.0,0.0],"b8":3,"ba":255}]],
			"nullcolumn":[0,0,1,0,31,30.0,0.2,120.5,"",[
				{"start":0.2,"end":0.45,"bone":"Bip01 R Hand",
				 "a_cm":[0.0,0.0,0.0],"b_cm":[27.94,0.0,0.0],"b8":3,"ba":255}],null],
			"halfcolumn":[0,0,1,0,31,30.0,0.2,120.5,"",[
				{"start":0.2,"end":0.45,"bone":"Bip01 R Hand",
				 "a_cm":[0.0,0.0,0.0],"b_cm":[27.94,0.0,0.0],"b8":3,"ba":255}],
				{"mask":8,"dodge":"","chain":"Fists_attack_W2","chain_alt":"","w_open":0.5}],
			"Fists_attack_W1":[0,0,1,0,31,30.0,0.2,282.9939,"",[],
				{"mask":8,"dodge":"","chain":"Fists_attack_W2","chain_alt":"",
				 "w_open":0.5,"w_close":0.9,"w_hold":0.91}],
			"fists_attack_JabLeft":[0,0,1,0,31,30.0,0.2,282.9939,"",[],
				{"mask":0,"dodge":"","chain":"fists_attack_longright","chain_alt":"",
				 "w_open":0.65,"w_close":0.9,"w_hold":0.91}],
			"knockback_flying_into_back":[0,0,1,0,31,30.0,0.2,null,"",[],
				{"mask":-1,"dodge":"","chain":"knockback_flying_idle",
				 "chain_alt":"knockback_flying_wall_hit","w_open":0.0,"w_close":1.0,"w_hold":1.0}],
			"katana_running_attack":[0,0,1,0,31,30.0,0.2,282.9939,"",[],
				{"mask":8,"dodge":"","chain":"katana_combo_C2","chain_alt":"",
				 "w_open":0.25,"w_close":1.0,"w_hold":0.9}]
		}
	})");

	// Columns 12 and 13 — the cast-arm selector's two inputs, and the four shapes each takes.
	//
	// `low_only` is the shipped case that makes the placeholders matter: 14 descriptors state a low
	// edge with `reach` UNSET, so every column ahead of 12 is held open and each with its own kind.
	// `zero_low` is the other trap: a stated `0.0` is a band that starts at the body, and a reader
	// testing the number instead of the null would discard it.
	const TCHAR* const GEnvelopeSlice = TEXT(R"({
		"stem":"cast_body",
		"owners":["melee_bank"],
		"activities":["ACT_MELEE_ATTACK"],
		"fields":["owner","activity","weight","flags","frames","fps","fade","reach_cm",
		          "blocked_reaction","swings","combo","low_reach_cm","envelopes"],
		"clips":{
			"nocolumn":[0,0,1,0,31,30.0,0.2,282.9939],
			"nullcolumn":[0,0,1,0,31,30.0,0.2,282.9939,"",[],null,null,null],
			"emptycolumn":[0,0,1,0,31,30.0,0.2,282.9939,"",[],null,60.0,[]],
			"low_only":[0,0,1,0,31,30.0,0.2,null,"",[],null,30.48],
			"zero_low":[0,0,1,0,31,30.0,0.2,282.9939,"",[],null,0.0],
			"banded":[0,0,1,0,31,30.0,0.2,282.9939,"",[],null,60.0,
				[{"min":[25.4,-7.62,-20.32],"max":[101.6,7.62,20.32]},
				 {"min":[30.48,-12.7,-22.86],"max":[111.76,12.7,22.86]}]],
			"halfcolumn":[0,0,1,0,31,30.0,0.2,282.9939,"",[],null,60.0,
				[{"min":[25.4,-7.62,-20.32]}]]
		}
	})");
}

// Columns 12 and 13: the reach band's near edge and the cast arm's attack envelopes.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumClipsEnvelopeColumnTest,
	"Elysium.Substrate.Clips.EnvelopeColumn", GElysiumTestFlags)
bool FElysiumClipsEnvelopeColumnTest::RunTest(const FString&)
{
	FElysiumNpcClipSet Set;
	FString Error;
	if (!TestTrue(TEXT("the synthetic slice parses"),
		Set.LoadJsonText(TEXT("cast_body"), GEnvelopeSlice, Error)))
	{
		AddError(FString::Printf(TEXT("slice refused: %s"), *Error));
		return false;
	}
	auto Clip = [&Set, this](const TCHAR* Label) -> const FElysiumNpcClip*
	{
		const FElysiumNpcClip* Found = Set.Find(Label);
		TestNotNull(*FString::Printf(TEXT("'%s' is in the slice"), Label), Found);
		return Found;
	};

	// --- Absent, null and empty are one answer, exactly as they are for swings and combos --------
	for (const TCHAR* Label : { TEXT("nocolumn"), TEXT("nullcolumn") })
	{
		if (const FElysiumNpcClip* Row = Clip(Label))
		{
			TestFalse(*FString::Printf(TEXT("'%s' states no near edge"), Label), Row->HasLowReach());
			TestFalse(*FString::Printf(TEXT("'%s' states no envelope"), Label), Row->HasEnvelopes());
		}
	}
	if (const FElysiumNpcClip* Row = Clip(TEXT("emptycolumn")))
	{
		TestTrue(TEXT("an empty envelope array still carries its near edge"), Row->HasLowReach());
		TestFalse(TEXT("...and reads as no envelopes"), Row->HasEnvelopes());
	}

	// --- The two shipped traps -------------------------------------------------------------------
	if (const FElysiumNpcClip* Row = Clip(TEXT("low_only")))
	{
		// 14 descriptors state a low edge with `reach` unset. The far edge's own column is held
		// open with a null, so the near edge lands in ITS column rather than sliding forward.
		TestFalse(TEXT("a low edge without a reach carries no reach"), Row->HasReach());
		TestTrue(TEXT("...and still carries the near edge"), Row->HasLowReach());
		TestEqual(TEXT("...at the value the column states"), Row->LowReachCm, 30.48f, 1e-3f);
	}
	if (const FElysiumNpcClip* Row = Clip(TEXT("zero_low")))
	{
		// `werewolf`'s `claw_attack_close` states exactly this. Zero is a band that starts at the
		// body — a real claim — so the guard is the null and never the number.
		TestTrue(TEXT("a stated zero near edge is stated"), Row->HasLowReach());
		TestEqual(TEXT("...and is zero"), Row->LowReachCm, 0.0f, 1e-6f);
	}

	// --- The records themselves, read verbatim ---------------------------------------------------
	if (const FElysiumNpcClip* Row = Clip(TEXT("banded")))
	{
		if (TestEqual(TEXT("both envelopes decode"), Row->Envelopes.Num(), 2))
		{
			// Verbatim: the exporter already stated these in the frame the cast arm's own derived
			// query is built in, and their axes are not a position.
			TestEqual(TEXT("the first envelope's near edge"),
				static_cast<float>(Row->Envelopes[0].Min.X), 25.4f, 1e-3f);
			TestEqual(TEXT("...its lateral tolerance"),
				static_cast<float>(Row->Envelopes[0].Max.Y), 7.62f, 1e-3f);
			TestEqual(TEXT("...its vertical offset"),
				static_cast<float>(Row->Envelopes[0].Min.Z), -20.32f, 1e-3f);
			// The overlap test is per-axis and inclusive, which is how the reach bit is scored.
			TestTrue(TEXT("a box inside the envelope overlaps it"),
				Row->Envelopes[0].Overlaps(FVector(50.0f, -1.0f, 0.0f),
					FVector(60.0f, 1.0f, 5.0f)));
			TestFalse(TEXT("...and one beyond its far edge does not"),
				Row->Envelopes[0].Overlaps(FVector(200.0f, -1.0f, 0.0f),
					FVector(210.0f, 1.0f, 5.0f)));
		}
	}

	// --- A malformed row is a pipeline defect, not an authored absence ---------------------------
	if (const FElysiumNpcClip* Row = Clip(TEXT("halfcolumn")))
	{
		// The column is this repository's own product, so a record missing a corner is dropped and
		// reported — the absence has its own shape, which is no column at all.
		TestEqual(TEXT("an envelope missing a corner is dropped"), Row->Envelopes.Num(), 0);
		TestTrue(TEXT("...while the row's own near edge survives"), Row->HasLowReach());
	}
	return true;
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

// The eleventh column, on the same compatibility rule as the tenth.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumClipsComboColumnTest,
	"Elysium.Substrate.Clips.ComboColumn", GElysiumTestFlags)
bool FElysiumClipsComboColumnTest::RunTest(const FString&)
{
	// The one record the parser refuses: a stated column missing a window bound. The exporter writes
	// the block WHOLE, so a half-record is a pipeline defect and is counted rather than half-read.
	AddExpectedError(TEXT("1 combo-chain block"), EAutomationExpectedErrorFlags::Contains, 1);

	FElysiumNpcClipSet Set;
	FString Error;
	if (!TestTrue(TEXT("the synthetic slice parses"),
		Set.LoadJsonText(TEXT("combo_body"), GComboSlice, Error)))
	{
		AddError(FString::Printf(TEXT("slice refused: %s"), *Error));
		return false;
	}

	// --- Absent, null and a dropped record are ONE answer: this attack chains nothing --------------
	for (const TCHAR* Label : { TEXT("nocolumn"), TEXT("nullcolumn"), TEXT("halfcolumn") })
	{
		const FElysiumNpcClip* Clip = Set.Find(Label);
		if (Clip == nullptr)
		{
			AddError(FString::Printf(TEXT("row '%s' did not parse"), Label));
			return false;
		}
		TestFalse(FString::Printf(TEXT("'%s' states no combo block"), Label), Clip->HasCombo());
		TestFalse(FString::Printf(TEXT("...so it hands off to nothing ('%s')"), Label),
			Clip->Combo.HasChain());
		TestFalse(FString::Printf(TEXT("...and is invisible to direction-keyed selection ('%s')"),
			Label), Clip->Combo.HasStateMask());
		// The column the block sits behind still reads, which is what proves the row was not shifted.
		TestEqual(FString::Printf(TEXT("...with its swing records still in the right column ('%s')"),
			Label), Clip->Swings.Num(), 1);
	}

	// --- The stated block, in full ----------------------------------------------------------------
	// `Fists_attack_W1`'s own shape: a forward-keyed entry chaining to its successor over a window
	// that closes before the busy hold releases.
	const FElysiumNpcClip* W1 = Set.Find(TEXT("Fists_attack_W1"));
	if (W1 == nullptr || !TestTrue(TEXT("the stated block parses"), W1->HasCombo()))
	{
		return false;
	}
	TestEqual(TEXT("the authored mask is carried RAW, in the file's own IN_ bits"),
		W1->Combo.Mask, ElysiumCombo::InForward);
	TestTrue(TEXT("...so the sequence is a selection candidate"), W1->Combo.HasStateMask());
	TestEqual(TEXT("the successor is a sequence label, not an activity"),
		W1->Combo.Chain, FString(TEXT("Fists_attack_W2")));
	TestTrue(TEXT("...and it can hand off"), W1->Combo.HasChain());
	TestEqual(TEXT("the window opens where the file says"), W1->Combo.WindowOpen, 0.5f);
	TestEqual(TEXT("...closes where the file says"), W1->Combo.WindowClose, 0.9f);
	TestEqual(TEXT("...and the busy hold is its own value"), W1->Combo.HoldCycle, 0.91f);
	TestTrue(TEXT("the window is closed at BOTH ends"),
		W1->Combo.IsWindowOpen(0.5f) && W1->Combo.IsWindowOpen(0.9f));
	TestFalse(TEXT("...and shut on either side of it"),
		W1->Combo.IsWindowOpen(0.49f) || W1->Combo.IsWindowOpen(0.91f));

	// --- `mask == 0` is a STATED mask, not an absence ---------------------------------------------
	// 36 shipped descriptors carry it — the attack a neutral press selects — and a reader treating a
	// falsy mask as unset would drop every one of them.
	const FElysiumNpcClip* Jab = Set.Find(TEXT("fists_attack_JabLeft"));
	if (Jab != nullptr)
	{
		TestTrue(TEXT("the neutral mask is stated"), Jab->Combo.HasStateMask());
		TestEqual(TEXT("...and its value is zero"), Jab->Combo.Mask, 0);
		TestEqual(TEXT("...and it chains like any other entry"),
			Jab->Combo.Chain, FString(TEXT("fists_attack_longright")));
	}

	// --- `-1` is the marker, and the row is still a real block -------------------------------------
	// `knockback_flying_into_back` authors no mask and no window and chains twice, which is why the
	// marker is what gates selection rather than the block's presence.
	const FElysiumNpcClip* Flying = Set.Find(TEXT("knockback_flying_into_back"));
	if (Flying != nullptr)
	{
		TestTrue(TEXT("the block is stated"), Flying->HasCombo());
		TestFalse(TEXT("...but the sequence is not a selection candidate"),
			Flying->Combo.HasStateMask());
		TestEqual(TEXT("...while its alternate successor is carried"),
			Flying->Combo.ChainAlt, FString(TEXT("knockback_flying_wall_hit")));
	}

	// --- `w_hold` BELOW `w_close`, carried verbatim -----------------------------------------------
	// `katana_running_attack` authors 0.25/1.0/0.9. A consumer deriving the hold from the close would
	// disagree with the file on all four descriptors that do this.
	const FElysiumNpcClip* Running = Set.Find(TEXT("katana_running_attack"));
	if (Running != nullptr)
	{
		TestEqual(TEXT("the window closes at the end of the clip"), Running->Combo.WindowClose, 1.0f);
		TestEqual(TEXT("...while the busy hold releases before it"), Running->Combo.HoldCycle, 0.9f);
		TestTrue(TEXT("...and nothing reorders the pair"),
			Running->Combo.HoldCycle < Running->Combo.WindowClose);
	}
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
