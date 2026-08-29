// Content-free Substrate automation: the RPG/social reaction-score calculator — the third social
// domain (K4, `docs/architecture/gameplay-systems-architecture.md` §5.5.7). Every table here is
// built in code, fabricated to match what `system/reaction.txt` and `system/reactions000.txt`
// actually author (`docs/vtmb/npc-ai-reverse-engineering.md` → "Emotional disposition and social
// reaction are different domains"), so these cases are this runtime's statement of the contract
// rather than a reading of the export. `Substrate/ElysiumReaction.h` is the pure calculator under
// test; `Substrate/ElysiumRulebook.h` is the typed-table/parsing layer beside it.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Substrate/ElysiumReaction.h"
#include "Substrate/ElysiumReactionTables.h"

namespace ElysiumReactionTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// --- The synthetic band table — `reaction.txt`'s seven rows, exactly as shipped ---------------
	FElysiumReactionBandTable MakeBandTable()
	{
		FElysiumReactionBandTable Table;
		Table.bClamping = true;   // the shipped `General.Table`'s own `"Clamping" "1"`

		auto AddBand = [&Table](int32 LowerBoundary, const TCHAR* Label)
		{
			FElysiumReactionBand Band;
			Band.LowerBoundary = LowerBoundary;
			Band.Label = Label;
			Table.Bands.Add(MoveTemp(Band));
		};
		AddBand(0, TEXT("Want To Kill"));
		AddBand(20, TEXT("Hatred"));
		AddBand(40, TEXT("Dislike"));
		AddBand(60, TEXT("Neutral Reaction"));
		AddBand(80, TEXT("Admire"));
		AddBand(100, TEXT("Love"));
		AddBand(9999, TEXT("Obsession"));
		return Table;
	}

	// --- The synthetic modifier table — `reactions000.txt`'s six shipped groups, verbatim ---------
	FElysiumReactionModifierTable MakeModifierTable()
	{
		FElysiumReactionModifierTable Table;

		auto AddMod = [&Table](const TCHAR* Category, const TCHAR* Group, const TCHAR* RawTargets,
			const TCHAR* WhoModifies, const TCHAR* RawModifier)
		{
			FElysiumReactionModifier Mod;
			Mod.CategoryInternalName = Category;
			Mod.GroupInternalName = Group;
			Mod.Condition = ElysiumParseReactionCondition(Group);
			if (RawTargets != nullptr)
			{
				ElysiumParseReactionTargets(RawTargets, Mod.Targets);
			}
			else
			{
				Mod.Targets = FElysiumReactionTargets();
				Mod.Targets.bAuthored = false;
			}
			Mod.WhoModifies = WhoModifies;
			Mod.RawModifier = RawModifier;
			ElysiumParseReactionModifierExpr(RawModifier, Mod.Kind, Mod.ScalarValue, Mod.FormulaExpression);
			if (!Mod.IsRecognized())
			{
				Table.InertModifiers.Add(Mod.GroupInternalName);
			}
			Table.Modifiers.Add(MoveTemp(Mod));
		};

		// History
		AddMod(TEXT("History"), TEXT("Reaction (Megalomaniac)"), TEXT("ALL"), TEXT("Others"), TEXT("*2"));
		AddMod(TEXT("History"), TEXT("Reaction (Close to the Beast)"), TEXT("Kindred(!Gangrel)"), TEXT("Others"), TEXT("-5"));
		AddMod(TEXT("History"), TEXT("Reaction (Close to the Beast)"), TEXT("Kine()"), TEXT("Others"), TEXT("-20"));
		AddMod(TEXT("History"), TEXT("Reaction (Occult Nut)"), TEXT("Kindred()"), TEXT("Others"), TEXT("-5"));
		// Discipline
		AddMod(TEXT("Discipline"), TEXT("Reaction (Dementation-Passion)"), TEXT("Kindred()"), TEXT("Others"),
			TEXT("Reaction + ((Reaction - 50)*2)"));
		AddMod(TEXT("Discipline"), TEXT("Reaction (Presence-Awe)"), nullptr, TEXT("Others"), TEXT("+20"));
		AddMod(TEXT("Discipline"), TEXT("Reaction (Presence-General)"), nullptr, TEXT("Others"), TEXT("+10"));

		return Table;
	}

	using ElysiumReaction::FComputeParams;
	using ElysiumReaction::FComputeResult;
}


// Band resolution: exact boundaries and out-of-range clamping.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumReactionBandsTest,
	"Elysium.Substrate.Reaction.Bands", GElysiumTestFlags)
bool FElysiumReactionBandsTest::RunTest(const FString&)
{
	const FElysiumReactionBandTable Bands = MakeBandTable();

	TestEqual(TEXT("seven bands"), Bands.Bands.Num(), 7);
	TestTrue(TEXT("the fabricated table is clamping"), Bands.bClamping);
	TestEqual(TEXT("min boundary is 0"), Bands.MinBoundary(), 0);
	TestEqual(TEXT("max boundary is 9999"), Bands.MaxBoundary(), 9999);

	// --- Exact boundaries: each of the seven authored lower bounds resolves to its own band -------
	struct FExpected { int32 Score; const TCHAR* Label; };
	const FExpected Exact[] = {
		{ 0,    TEXT("Want To Kill") },
		{ 20,   TEXT("Hatred") },
		{ 40,   TEXT("Dislike") },
		{ 60,   TEXT("Neutral Reaction") },
		{ 80,   TEXT("Admire") },
		{ 100,  TEXT("Love") },
		{ 9999, TEXT("Obsession") },
	};
	for (const FExpected& E : Exact)
	{
		const FElysiumReactionBand* Band = Bands.Resolve(E.Score);
		if (TestNotNull(*FString::Printf(TEXT("score %d resolves to a band"), E.Score), Band))
		{
			TestEqual(*FString::Printf(TEXT("score %d is exactly '%s'"), E.Score, E.Label),
				Band->Label, FString(E.Label));
		}
	}

	// --- One point inside each band, and the boundary minus one falls in the band below -----------
	TestEqual(TEXT("19 is still Want To Kill"), Bands.Resolve(19)->Label, FString(TEXT("Want To Kill")));
	TestEqual(TEXT("21 is Hatred"), Bands.Resolve(21)->Label, FString(TEXT("Hatred")));
	TestEqual(TEXT("99 is Admire"), Bands.Resolve(99)->Label, FString(TEXT("Admire")));
	TestEqual(TEXT("10000 is still Obsession (no upper band)"), Bands.Resolve(10000)->Label,
		FString(TEXT("Obsession")));

	// --- Resolve() itself does not clamp: below the first boundary there is no matching band -------
	TestNull(TEXT("Resolve() below the table's own floor finds no band"), Bands.Resolve(-50));

	// --- Clamping is Compute()'s job, exercised against an empty modifier table ---------------------
	const FElysiumReactionModifierTable NoModifiers;
	{
		FComputeParams Params;
		Params.BaseScore = -50;
		const FComputeResult Result = ElysiumReaction::Compute(Params, Bands, NoModifiers);
		TestTrue(TEXT("a below-floor base score resolves once clamped"), Result.bResolved);
		TestEqual(TEXT("clamped to the floor"), Result.FinalScore, 0);
		TestEqual(TEXT("...which is Want To Kill"), Result.BandLabel, FString(TEXT("Want To Kill")));
	}
	{
		FComputeParams Params;
		Params.BaseScore = 50000;
		const FComputeResult Result = ElysiumReaction::Compute(Params, Bands, NoModifiers);
		TestTrue(TEXT("an above-ceiling base score resolves once clamped"), Result.bResolved);
		TestEqual(TEXT("clamped to the ceiling"), Result.FinalScore, 9999);
		TestEqual(TEXT("...which is Obsession"), Result.BandLabel, FString(TEXT("Obsession")));
	}

	// --- An unloaded/empty band table is a structured failure, not a warning-worthy crash ----------
	{
		const FElysiumReactionBandTable EmptyBands;
		FComputeParams Params;
		Params.BaseScore = 42;
		const FComputeResult Result = ElysiumReaction::Compute(Params, EmptyBands, NoModifiers);
		TestFalse(TEXT("an empty band table cannot resolve a label"), Result.bResolved);
		TestTrue(TEXT("...and reports no label"), Result.BandLabel.IsEmpty());
	}

	return true;
}


// `Targets` string parsing and scope matching.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumReactionTargetsTest,
	"Elysium.Substrate.Reaction.Targets", GElysiumTestFlags)
bool FElysiumReactionTargetsTest::RunTest(const FString&)
{
	// --- Kindred(!Gangrel) ---------------------------------------------------------------------
	{
		FElysiumReactionTargets Targets;
		ElysiumParseReactionTargets(TEXT("Kindred(!Gangrel)"), Targets);
		TestTrue(TEXT("Kindred(!Gangrel) is authored"), Targets.bAuthored);
		TestEqual(TEXT("scope is Kindred"), Targets.Scope, EElysiumReactionTargetScope::Kindred);
		TestEqual(TEXT("one exclusion"), Targets.Exclusions.Num(), 1);
		TestTrue(TEXT("Gangrel is excluded"), Targets.Exclusions.Contains(FName(TEXT("Gangrel"))));

		TestFalse(TEXT("a Gangrel reactor does not match"),
			Targets.Matches(/*Kindred*/ true, /*Kine*/ false, FName(TEXT("Gangrel"))));
		TestTrue(TEXT("a non-Gangrel Kindred reactor matches"),
			Targets.Matches(true, false, FName(TEXT("Brujah"))));
		TestFalse(TEXT("a Kine reactor does not match a Kindred scope"),
			Targets.Matches(false, true, NAME_None));
	}

	// --- Kine() ----------------------------------------------------------------------------------
	{
		FElysiumReactionTargets Targets;
		ElysiumParseReactionTargets(TEXT("Kine()"), Targets);
		TestEqual(TEXT("scope is Kine"), Targets.Scope, EElysiumReactionTargetScope::Kine);
		TestEqual(TEXT("no exclusions"), Targets.Exclusions.Num(), 0);
		TestTrue(TEXT("a Kine reactor matches"), Targets.Matches(false, true, NAME_None));
		TestFalse(TEXT("a Kindred reactor does not match a Kine scope"),
			Targets.Matches(true, false, FName(TEXT("Toreador"))));
	}

	// --- Kindred() (no exclusions) -----------------------------------------------------------------
	{
		FElysiumReactionTargets Targets;
		ElysiumParseReactionTargets(TEXT("Kindred()"), Targets);
		TestEqual(TEXT("scope is Kindred"), Targets.Scope, EElysiumReactionTargetScope::Kindred);
		TestTrue(TEXT("any Kindred reactor matches, Gangrel included"),
			Targets.Matches(true, false, FName(TEXT("Gangrel"))));
	}

	// --- bare ALL --------------------------------------------------------------------------------
	{
		FElysiumReactionTargets Targets;
		ElysiumParseReactionTargets(TEXT("ALL"), Targets);
		TestEqual(TEXT("scope is All"), Targets.Scope, EElysiumReactionTargetScope::All);
		TestTrue(TEXT("a Kindred reactor matches ALL"), Targets.Matches(true, false, FName(TEXT("Malkavian"))));
		TestTrue(TEXT("a Kine reactor matches ALL"), Targets.Matches(false, true, NAME_None));
	}

	return true;
}


// `Modifier` string parsing: scalar Add/Multiply, the free-form Formula shape, and the
// unrecognized carried-but-inert case.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumReactionModifierExprTest,
	"Elysium.Substrate.Reaction.ModifierExpr", GElysiumTestFlags)
bool FElysiumReactionModifierExprTest::RunTest(const FString&)
{
	EElysiumReactionModifierKind Kind;
	double Scalar;
	FString Formula;

	ElysiumParseReactionModifierExpr(TEXT("+20"), Kind, Scalar, Formula);
	TestEqual(TEXT("'+20' is Add"), Kind, EElysiumReactionModifierKind::Add);
	TestEqual(TEXT("'+20' scalar is 20"), Scalar, 20.0);

	ElysiumParseReactionModifierExpr(TEXT("-5"), Kind, Scalar, Formula);
	TestEqual(TEXT("'-5' is Add"), Kind, EElysiumReactionModifierKind::Add);
	TestEqual(TEXT("'-5' scalar is -5"), Scalar, -5.0);

	ElysiumParseReactionModifierExpr(TEXT("*2"), Kind, Scalar, Formula);
	TestEqual(TEXT("'*2' is Multiply"), Kind, EElysiumReactionModifierKind::Multiply);
	TestEqual(TEXT("'*2' scalar is 2"), Scalar, 2.0);

	ElysiumParseReactionModifierExpr(TEXT("Reaction + ((Reaction - 50)*2)"), Kind, Scalar, Formula);
	TestEqual(TEXT("the Dementation-Passion string is Formula"), Kind,
		EElysiumReactionModifierKind::Formula);
	TestEqual(TEXT("the formula text is kept verbatim"), Formula,
		FString(TEXT("Reaction + ((Reaction - 50)*2)")));

	// --- Carried-but-inert: no shipped row takes this shape today ----------------------------------
	ElysiumParseReactionModifierExpr(TEXT("Max 5"), Kind, Scalar, Formula);
	TestEqual(TEXT("'Max 5' is Unrecognized"), Kind, EElysiumReactionModifierKind::Unrecognized);

	ElysiumParseReactionModifierExpr(TEXT("%10"), Kind, Scalar, Formula);
	TestEqual(TEXT("'%10' is Unrecognized"), Kind, EElysiumReactionModifierKind::Unrecognized);

	return true;
}


// `ReactionGroup` InternalName -> condition classification.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumReactionConditionTest,
	"Elysium.Substrate.Reaction.Condition", GElysiumTestFlags)
bool FElysiumReactionConditionTest::RunTest(const FString&)
{
	using EC = EElysiumReactionCondition;
	TestEqual(TEXT("Megalomaniac"), ElysiumParseReactionCondition(TEXT("Reaction (Megalomaniac)")),
		EC::Megalomaniac);
	TestEqual(TEXT("Close to the Beast"),
		ElysiumParseReactionCondition(TEXT("Reaction (Close to the Beast)")),
		EC::CloseToTheBeast);
	TestEqual(TEXT("Occult Nut"), ElysiumParseReactionCondition(TEXT("Reaction (Occult Nut)")),
		EC::OccultNut);
	TestEqual(TEXT("Dementation-Passion"),
		ElysiumParseReactionCondition(TEXT("Reaction (Dementation-Passion)")),
		EC::DementationPassion);
	TestEqual(TEXT("Presence-Awe"), ElysiumParseReactionCondition(TEXT("Reaction (Presence-Awe)")),
		EC::PresenceAwe);
	TestEqual(TEXT("Presence-General"),
		ElysiumParseReactionCondition(TEXT("Reaction (Presence-General)")),
		EC::PresenceGeneral);
	TestEqual(TEXT("an unknown group name is Unrecognized"),
		ElysiumParseReactionCondition(TEXT("Reaction (Something New)")), EC::Unrecognized);

	return true;
}


// The Dementation-Passion formula, evaluated directly.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumReactionFormulaEvalTest,
	"Elysium.Substrate.Reaction.FormulaEval", GElysiumTestFlags)
bool FElysiumReactionFormulaEvalTest::RunTest(const FString&)
{
	static const TCHAR* Passion = TEXT("Reaction + ((Reaction - 50)*2)");
	double Out = 0.0;

	TestTrue(TEXT("Reaction=70 evaluates"), ElysiumReaction::TryEvaluateFormula(Passion, 70.0, Out));
	TestEqual(TEXT("70 + ((70-50)*2) == 110"), Out, 110.0);

	TestTrue(TEXT("Reaction=30 evaluates"), ElysiumReaction::TryEvaluateFormula(Passion, 30.0, Out));
	TestEqual(TEXT("30 + ((30-50)*2) == -10"), Out, -10.0);

	TestTrue(TEXT("Reaction=50 is the formula's own fixed point"),
		ElysiumReaction::TryEvaluateFormula(Passion, 50.0, Out));
	TestEqual(TEXT("50 + ((50-50)*2) == 50"), Out, 50.0);

	// --- Malformed expressions fail closed, leaving OutValue at the input ---------------------------
	Out = 12345.0;
	TestFalse(TEXT("an unbalanced paren fails"), ElysiumReaction::TryEvaluateFormula(TEXT("Reaction + )"), 5.0, Out));
	TestEqual(TEXT("...leaving OutValue at ReactionValue"), Out, 5.0);

	Out = 12345.0;
	TestFalse(TEXT("division by zero fails"), ElysiumReaction::TryEvaluateFormula(TEXT("Reaction / 0"), 5.0, Out));
	TestEqual(TEXT("...leaving OutValue at ReactionValue"), Out, 5.0);

	return true;
}


// Compute(): each modifier, its Targets gate, and the History-before-Discipline order.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumReactionComputeTest,
	"Elysium.Substrate.Reaction.Compute", GElysiumTestFlags)
bool FElysiumReactionComputeTest::RunTest(const FString&)
{
	const FElysiumReactionBandTable Bands = MakeBandTable();
	const FElysiumReactionModifierTable Modifiers = MakeModifierTable();
	TestEqual(TEXT("seven fabricated modifier rows"), Modifiers.Modifiers.Num(), 7);
	TestTrue(TEXT("every fabricated row is recognized"), Modifiers.InertModifiers.IsEmpty());

	// --- Megalomaniac: doubles toward ANY reactor -----------------------------------------------
	{
		FComputeParams Params;
		Params.BaseScore = 50;
		Params.Subject.bHasMegalomaniac = true;
		Params.Reactor.bIsKindred = true;
		Params.Reactor.Clan = FName(TEXT("Tremere"));
		const FComputeResult Result = ElysiumReaction::Compute(Params, Bands, Modifiers);
		TestEqual(TEXT("50 * 2 == 100"), Result.FinalScore, 100);
		TestEqual(TEXT("100 is Love"), Result.BandLabel, FString(TEXT("Love")));
	}

	// --- Close to the Beast: Kindred(!Gangrel) penalty, Gangrel exempt, Kine penalized worse -------
	{
		FComputeParams Params;
		Params.BaseScore = 60;
		Params.Subject.bIsCloseToTheBeast = true;
		Params.Reactor.bIsKindred = true;
		Params.Reactor.Clan = FName(TEXT("Brujah"));
		const FComputeResult Result = ElysiumReaction::Compute(Params, Bands, Modifiers);
		TestEqual(TEXT("a non-Gangrel Kindred takes -5: 60 - 5 == 55"), Result.FinalScore, 55);
		TestEqual(TEXT("55 is Dislike"), Result.BandLabel, FString(TEXT("Dislike")));
	}
	{
		FComputeParams Params;
		Params.BaseScore = 60;
		Params.Subject.bIsCloseToTheBeast = true;
		Params.Reactor.bIsKindred = true;
		Params.Reactor.Clan = FName(TEXT("Gangrel"));
		const FComputeResult Result = ElysiumReaction::Compute(Params, Bands, Modifiers);
		TestEqual(TEXT("a Gangrel reactor is exempt: unchanged at 60"), Result.FinalScore, 60);
		TestEqual(TEXT("60 is Neutral Reaction"), Result.BandLabel, FString(TEXT("Neutral Reaction")));
	}
	{
		FComputeParams Params;
		Params.BaseScore = 60;
		Params.Subject.bIsCloseToTheBeast = true;
		Params.Reactor.bIsKine = true;
		const FComputeResult Result = ElysiumReaction::Compute(Params, Bands, Modifiers);
		TestEqual(TEXT("Kine take the harsher -20: 60 - 20 == 40"), Result.FinalScore, 40);
		TestEqual(TEXT("40 is exactly the Dislike boundary"), Result.BandLabel, FString(TEXT("Dislike")));
	}

	// --- Occult Nut: Kindred penalty only ----------------------------------------------------------
	{
		FComputeParams Params;
		Params.BaseScore = 45;
		Params.Subject.bHasOccultNut = true;
		Params.Reactor.bIsKindred = true;
		Params.Reactor.Clan = FName(TEXT("Nosferatu"));
		const FComputeResult Result = ElysiumReaction::Compute(Params, Bands, Modifiers);
		TestEqual(TEXT("45 - 5 == 40"), Result.FinalScore, 40);
	}
	{
		FComputeParams Params;
		Params.BaseScore = 45;
		Params.Subject.bHasOccultNut = true;
		Params.Reactor.bIsKine = true;
		const FComputeResult Result = ElysiumReaction::Compute(Params, Bands, Modifiers);
		TestEqual(TEXT("a Kine reactor is outside Occult Nut's Kindred() scope"), Result.FinalScore, 45);
	}

	// --- Dementation-Passion: the formula, gated to Kindred reactors --------------------------------
	{
		FComputeParams Params;
		Params.BaseScore = 70;
		Params.Subject.bDementationPassionActive = true;
		Params.Reactor.bIsKindred = true;
		Params.Reactor.Clan = FName(TEXT("Ventrue"));
		const FComputeResult Result = ElysiumReaction::Compute(Params, Bands, Modifiers);
		TestEqual(TEXT("70 + ((70-50)*2) == 110"), Result.FinalScore, 110);
		TestEqual(TEXT("110 is Love"), Result.BandLabel, FString(TEXT("Love")));
	}
	{
		FComputeParams Params;
		Params.BaseScore = 30;
		Params.Subject.bDementationPassionActive = true;
		Params.Reactor.bIsKindred = true;
		Params.Reactor.Clan = FName(TEXT("Ventrue"));
		const FComputeResult Result = ElysiumReaction::Compute(Params, Bands, Modifiers);
		TestEqual(TEXT("30 + ((30-50)*2) == -10, clamped to 0"), Result.FinalScore, 0);
		TestEqual(TEXT("0 is Want To Kill"), Result.BandLabel, FString(TEXT("Want To Kill")));
	}
	{
		FComputeParams Params;
		Params.BaseScore = 70;
		Params.Subject.bDementationPassionActive = true;
		Params.Reactor.bIsKine = true;   // outside the Kindred() scope
		const FComputeResult Result = ElysiumReaction::Compute(Params, Bands, Modifiers);
		TestEqual(TEXT("a Kine reactor is untouched by Dementation's Passion"), Result.FinalScore, 70);
	}

	// --- Presence-Awe / Presence-General: unconditional, both apply regardless of reactor -----------
	{
		FComputeParams Params;
		Params.BaseScore = 60;
		Params.Subject.bPresenceAweActive = true;
		Params.Subject.bPresenceGeneralActive = true;
		const FComputeResult Result = ElysiumReaction::Compute(Params, Bands, Modifiers);
		TestEqual(TEXT("60 + 20 + 10 == 90"), Result.FinalScore, 90);
		TestEqual(TEXT("90 is Admire"), Result.BandLabel, FString(TEXT("Admire")));
	}

	// --- Order: History (Megalomaniac's *2) runs before Discipline (Presence-General's +10) ---------
	// (40 * 2) + 10 == 90, NOT (40 + 10) * 2 == 100 — pins the CHOSEN, NOT RECOVERED ordering rule.
	{
		FComputeParams Params;
		Params.BaseScore = 40;
		Params.Subject.bHasMegalomaniac = true;
		Params.Subject.bPresenceGeneralActive = true;
		const FComputeResult Result = ElysiumReaction::Compute(Params, Bands, Modifiers);
		TestEqual(TEXT("History runs before Discipline: (40*2)+10 == 90"), Result.FinalScore, 90);
	}

	// --- No applicable facts: the base score passes through unmodified ------------------------------
	{
		FComputeParams Params;
		Params.BaseScore = 65;
		const FComputeResult Result = ElysiumReaction::Compute(Params, Bands, Modifiers);
		TestEqual(TEXT("no active subject facts leaves the base score untouched"), Result.FinalScore, 65);
		TestEqual(TEXT("65 is Neutral Reaction"), Result.BandLabel, FString(TEXT("Neutral Reaction")));
	}

	return true;
}


// Carried-but-inert rows: an unrecognized group, an unrecognized WhoModifies, and an
// unrecognized Modifier shape are all parsed and kept, never applied.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumReactionInertModifierTest,
	"Elysium.Substrate.Reaction.InertModifier", GElysiumTestFlags)
bool FElysiumReactionInertModifierTest::RunTest(const FString&)
{
	const FElysiumReactionBandTable Bands = MakeBandTable();

	// An unrecognized ReactionGroup name.
	{
		FElysiumReactionModifierTable Table;
		FElysiumReactionModifier Mod;
		Mod.CategoryInternalName = TEXT("History");
		Mod.GroupInternalName = TEXT("Reaction (Something Nobody Shipped)");
		Mod.Condition = ElysiumParseReactionCondition(Mod.GroupInternalName);
		ElysiumParseReactionTargets(TEXT("ALL"), Mod.Targets);
		Mod.WhoModifies = TEXT("Others");
		Mod.RawModifier = TEXT("+9999");
		ElysiumParseReactionModifierExpr(Mod.RawModifier, Mod.Kind, Mod.ScalarValue, Mod.FormulaExpression);
		Table.Modifiers.Add(Mod);

		TestFalse(TEXT("an unrecognized group is not recognized"), Mod.IsRecognized());

		FComputeParams Params;
		Params.BaseScore = 50;
		// Every subject flag left false — nothing here can enable a condition Compute() never named.
		const FComputeResult Result = ElysiumReaction::Compute(Params, Bands, Table);
		TestEqual(TEXT("an unrecognized-group row never applies"), Result.FinalScore, 50);
	}

	// A `WhoModifies` value outside the one shipped literal.
	{
		FElysiumReactionModifierTable Table;
		FElysiumReactionModifier Mod;
		Mod.CategoryInternalName = TEXT("History");
		Mod.GroupInternalName = TEXT("Reaction (Megalomaniac)");
		Mod.Condition = ElysiumParseReactionCondition(Mod.GroupInternalName);
		ElysiumParseReactionTargets(TEXT("ALL"), Mod.Targets);
		Mod.WhoModifies = TEXT("Self");   // never shipped
		Mod.RawModifier = TEXT("*2");
		ElysiumParseReactionModifierExpr(Mod.RawModifier, Mod.Kind, Mod.ScalarValue, Mod.FormulaExpression);
		Table.Modifiers.Add(Mod);

		TestFalse(TEXT("a 'Self' WhoModifies is not recognized"), Mod.IsRecognized());

		FComputeParams Params;
		Params.BaseScore = 50;
		Params.Subject.bHasMegalomaniac = true;   // the condition holds; WhoModifies still blocks it
		const FComputeResult Result = ElysiumReaction::Compute(Params, Bands, Table);
		TestEqual(TEXT("a 'Self' row never applies even when its condition holds"), Result.FinalScore, 50);
	}

	// An unrecognized Modifier shape.
	{
		FElysiumReactionModifierTable Table;
		FElysiumReactionModifier Mod;
		Mod.CategoryInternalName = TEXT("History");
		Mod.GroupInternalName = TEXT("Reaction (Megalomaniac)");
		Mod.Condition = ElysiumParseReactionCondition(Mod.GroupInternalName);
		ElysiumParseReactionTargets(TEXT("ALL"), Mod.Targets);
		Mod.WhoModifies = TEXT("Others");
		Mod.RawModifier = TEXT("Max 5");   // no shipped row takes this shape
		ElysiumParseReactionModifierExpr(Mod.RawModifier, Mod.Kind, Mod.ScalarValue, Mod.FormulaExpression);
		Table.Modifiers.Add(Mod);

		TestFalse(TEXT("an unrecognized Modifier shape is not recognized"), Mod.IsRecognized());

		FComputeParams Params;
		Params.BaseScore = 50;
		Params.Subject.bHasMegalomaniac = true;
		const FComputeResult Result = ElysiumReaction::Compute(Params, Bands, Table);
		TestEqual(TEXT("an unrecognized Modifier shape never applies"), Result.FinalScore, 50);
	}

	return true;
}

}   // namespace ElysiumReactionTests

#endif // WITH_DEV_AUTOMATION_TESTS
