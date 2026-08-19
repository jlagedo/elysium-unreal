#include "Substrate/ElysiumReaction.h"

#include "Substrate/ElysiumReactionTables.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumReaction, Log, All);

namespace
{
	// Tiny recursive-descent evaluator for a `reactions000.txt` Modifier formula. The grammar
	// (`+ - * /`, parens, decimal numbers, the `Reaction` identifier) is code; every number and
	// operator token an evaluated expression actually uses still comes from the authored string —
	// this exists because `reaction.txt`'s `ModifierNames` legend names only scalar ops
	// (`+`/`*`/`/`/`Max`/`Min`/`%`), and Dementation's Passion modifier is not one of those.
	class FReactionFormulaEval
	{
	public:
		FReactionFormulaEval(const FString& InExpr, double InReactionValue)
			: Expr(InExpr), ReactionValue(InReactionValue)
		{}

		bool Evaluate(double& OutValue)
		{
			bOk = true;
			Pos = 0;
			OutValue = ParseExpr();
			SkipSpace();
			return bOk && Pos >= Expr.Len();
		}

	private:
		const FString& Expr;
		double ReactionValue;
		int32 Pos = 0;
		bool bOk = true;

		void SkipSpace() { while (Pos < Expr.Len() && FChar::IsWhitespace(Expr[Pos])) { ++Pos; } }

		double ParseExpr()   // + -, lowest precedence
		{
			double V = ParseTerm();
			for (;;)
			{
				SkipSpace();
				if (Pos < Expr.Len() && Expr[Pos] == TEXT('+')) { ++Pos; V += ParseTerm(); }
				else if (Pos < Expr.Len() && Expr[Pos] == TEXT('-')) { ++Pos; V -= ParseTerm(); }
				else { break; }
			}
			return V;
		}

		double ParseTerm()   // * /
		{
			double V = ParseFactor();
			for (;;)
			{
				SkipSpace();
				if (Pos < Expr.Len() && Expr[Pos] == TEXT('*')) { ++Pos; V *= ParseFactor(); }
				else if (Pos < Expr.Len() && Expr[Pos] == TEXT('/'))
				{
					++Pos;
					const double D = ParseFactor();
					if (FMath::IsNearlyZero(D)) { bOk = false; }
					else { V /= D; }
				}
				else { break; }
			}
			return V;
		}

		double ParseFactor()   // number | `Reaction` | ( expr ) | unary +/-
		{
			SkipSpace();
			if (Pos >= Expr.Len()) { bOk = false; return 0.0; }
			if (Expr[Pos] == TEXT('-')) { ++Pos; return -ParseFactor(); }
			if (Expr[Pos] == TEXT('+')) { ++Pos; return ParseFactor(); }
			if (Expr[Pos] == TEXT('('))
			{
				++Pos;
				const double V = ParseExpr();
				SkipSpace();
				if (Pos < Expr.Len() && Expr[Pos] == TEXT(')')) { ++Pos; }
				else { bOk = false; }
				return V;
			}
			static const FString ReactionToken = TEXT("Reaction");
			if (Expr.Mid(Pos, ReactionToken.Len()).Equals(ReactionToken, ESearchCase::IgnoreCase))
			{
				Pos += ReactionToken.Len();
				return ReactionValue;
			}
			const int32 Start = Pos;
			while (Pos < Expr.Len() && (FChar::IsDigit(Expr[Pos]) || Expr[Pos] == TEXT('.'))) { ++Pos; }
			if (Pos == Start) { bOk = false; return 0.0; }
			return FCString::Atod(*Expr.Mid(Start, Pos - Start));
		}
	};

	bool ConditionHolds(EElysiumReactionCondition Condition, const ElysiumReaction::FSubjectFacts& Subject)
	{
		switch (Condition)
		{
		case EElysiumReactionCondition::Megalomaniac:       return Subject.bHasMegalomaniac;
		case EElysiumReactionCondition::CloseToTheBeast:    return Subject.bIsCloseToTheBeast;
		case EElysiumReactionCondition::OccultNut:          return Subject.bHasOccultNut;
		case EElysiumReactionCondition::DementationPassion: return Subject.bDementationPassionActive;
		case EElysiumReactionCondition::PresenceAwe:        return Subject.bPresenceAweActive;
		case EElysiumReactionCondition::PresenceGeneral:    return Subject.bPresenceGeneralActive;
		default:                                             return false;
		}
	}
}

bool ElysiumReaction::TryEvaluateFormula(const FString& Expression, double ReactionValue, double& OutValue)
{
	OutValue = ReactionValue;
	FReactionFormulaEval Eval(Expression, ReactionValue);
	double Result = 0.0;
	if (!Eval.Evaluate(Result))
	{
		return false;
	}
	OutValue = Result;
	return true;
}

ElysiumReaction::FComputeResult ElysiumReaction::Compute(const FComputeParams& Params,
	const FElysiumReactionBandTable& Bands, const FElysiumReactionModifierTable& Modifiers)
{
	double Score = (double)Params.BaseScore;

	// File order: category (History, then Discipline), then group, then row within a group — see
	// the header's CHOSEN, NOT RECOVERED note on ordering.
	for (const FElysiumReactionModifier& Mod : Modifiers.Modifiers)
	{
		if (!Mod.IsRecognized())
		{
			continue;   // carried-but-inert; `FElysiumReactionModifierTable::Load` logged the set once
		}
		if (!ConditionHolds(Mod.Condition, Params.Subject))
		{
			continue;
		}
		if (!Mod.Targets.Matches(Params.Reactor.bIsKindred, Params.Reactor.bIsKine, Params.Reactor.Clan))
		{
			continue;
		}

		switch (Mod.Kind)
		{
		case EElysiumReactionModifierKind::Add:
			Score += Mod.ScalarValue;
			break;
		case EElysiumReactionModifierKind::Multiply:
			Score *= Mod.ScalarValue;
			break;
		case EElysiumReactionModifierKind::Formula:
			{
				double Result = Score;
				if (TryEvaluateFormula(Mod.FormulaExpression, Score, Result))
				{
					Score = Result;
				}
				else
				{
					// Should never ship: `Load` classifies `Formula` purely by the presence of the
					// word `Reaction`, so a genuinely malformed expression is a data defect. De-
					// duplicated so it warns once per distinct bad string rather than once per call.
					static TSet<FString> WarnedFormulas;
					if (!WarnedFormulas.Contains(Mod.FormulaExpression))
					{
						WarnedFormulas.Add(Mod.FormulaExpression);
						UE_LOG(LogElysiumReaction, Warning,
							TEXT("Compute: '%s' modifier formula '%s' failed to evaluate; left unapplied"),
							*Mod.GroupInternalName, *Mod.FormulaExpression);
					}
				}
			}
			break;
		default:
			break;
		}
	}

	FComputeResult Out;
	int32 FinalScoreInt = FMath::RoundToInt(Score);
	if (Bands.bClamping && Bands.IsValid())
	{
		FinalScoreInt = FMath::Clamp(FinalScoreInt, Bands.MinBoundary(), Bands.MaxBoundary());
	}
	Out.FinalScore = FinalScoreInt;

	if (const FElysiumReactionBand* Band = Bands.Resolve(FinalScoreInt))
	{
		Out.BandLabel = Band->Label;
		Out.BandLowerBoundary = Band->LowerBoundary;
		Out.bResolved = true;
	}
	return Out;
}
