#include "Substrate/ElysiumReactionTables.h"

#include "ElysiumContentPaths.h"
#include "ElysiumKeyValues.h"

#include "Substrate/ElysiumVdataLoad.h"

namespace
{
	using ElysiumKeyValues::FKvNode;
	using ElysiumVdata::ReadVdata;
	using ElysiumVdata::RootBlock;
	using ElysiumVdata::Trim;
}

// `system/reaction.txt` + `reactions000.txt`

namespace
{
	bool IsPlainNumber(const FString& S)
	{
		if (S.IsEmpty())
		{
			return false;
		}
		int32 i = 0;
		if (S[0] == TEXT('+') || S[0] == TEXT('-')) { i = 1; }
		if (i >= S.Len())
		{
			return false;
		}
		bool bSawDigit = false;
		for (; i < S.Len(); ++i)
		{
			const TCHAR C = S[i];
			if (FChar::IsDigit(C)) { bSawDigit = true; continue; }
			if (C == TEXT('.')) { continue; }
			return false;
		}
		return bSawDigit;
	}
}

// `Kindred(!Gangrel)` -> {Kindred, ["Gangrel"]}; `Kine()` -> {Kine, []}; bare `ALL` -> {All, []}.
void ElysiumParseReactionTargets(const FString& Raw, FElysiumReactionTargets& Out)
{
	Out.Scope = EElysiumReactionTargetScope::All;
	Out.Exclusions.Reset();
	Out.bAuthored = true;

	const int32 OpenParen = Raw.Find(TEXT("("));
	const FString ScopeName = Trim(OpenParen == INDEX_NONE ? Raw : Raw.Left(OpenParen));

	if (ScopeName.Equals(TEXT("Kindred"), ESearchCase::IgnoreCase))
	{
		Out.Scope = EElysiumReactionTargetScope::Kindred;
	}
	else if (ScopeName.Equals(TEXT("Kine"), ESearchCase::IgnoreCase))
	{
		Out.Scope = EElysiumReactionTargetScope::Kine;
	}
	// Anything else — the shipped bare `ALL`, or an unrecognized future spelling — folds to `All`.

	if (OpenParen != INDEX_NONE)
	{
		const int32 CloseParen = Raw.Find(TEXT(")"), ESearchCase::IgnoreCase, ESearchDir::FromStart, OpenParen);
		if (CloseParen != INDEX_NONE && CloseParen > OpenParen + 1)
		{
			TArray<FString> Parts;
			Raw.Mid(OpenParen + 1, CloseParen - OpenParen - 1).ParseIntoArray(Parts, TEXT(","), true);
			for (FString& Part : Parts)
			{
				Part.TrimStartAndEndInline();
				if (Part.StartsWith(TEXT("!")))
				{
					Out.Exclusions.Add(FName(*Part.Mid(1)));
				}
				// A bare (non-`!`) name inside the parens never appears in the shipped corpus;
				// nothing here treats it as an inclusion filter — CHOSEN, NOT RECOVERED.
			}
		}
	}
}

// `"+20"`/`"-5"` -> Add; `"*2"` -> Multiply; anything naming `Reaction` -> Formula (evaluated by
// `ElysiumReaction::TryEvaluateFormula`); anything else -> Unrecognized.
void ElysiumParseReactionModifierExpr(const FString& Raw, EElysiumReactionModifierKind& OutKind,
	double& OutScalar, FString& OutFormula)
{
	const FString S = Trim(Raw);
	OutKind = EElysiumReactionModifierKind::Unrecognized;
	OutScalar = 0.0;
	OutFormula.Reset();

	if (S.IsEmpty())
	{
		return;
	}
	if ((S[0] == TEXT('+') || S[0] == TEXT('-')) && IsPlainNumber(S))
	{
		OutKind = EElysiumReactionModifierKind::Add;
		OutScalar = FCString::Atod(*S);
		return;
	}
	if (S[0] == TEXT('*') && IsPlainNumber(S.Mid(1)))
	{
		OutKind = EElysiumReactionModifierKind::Multiply;
		OutScalar = FCString::Atod(*S.Mid(1));
		return;
	}
	if (S.Contains(TEXT("Reaction")))
	{
		OutKind = EElysiumReactionModifierKind::Formula;
		OutFormula = S;
		return;
	}
	// An unrecognized shape — no shipped row takes this today (a bare `/`, `%`, `Max`, `Min` row,
	// per `reaction.txt`'s `ModifierNames` legend, would land here) — carried-but-inert.
}

bool FElysiumReactionModifier::IsRecognized() const
{
	return Condition != EElysiumReactionCondition::Unrecognized
		&& Kind != EElysiumReactionModifierKind::Unrecognized
		&& WhoModifies.Equals(TEXT("Others"), ESearchCase::IgnoreCase);
}

bool FElysiumReactionTargets::Matches(bool bReactorIsKindred, bool bReactorIsKine, FName ReactorClan) const
{
	switch (Scope)
	{
	case EElysiumReactionTargetScope::Kindred:
		return bReactorIsKindred && !Exclusions.Contains(ReactorClan);
	case EElysiumReactionTargetScope::Kine:
		return bReactorIsKine && !Exclusions.Contains(ReactorClan);
	case EElysiumReactionTargetScope::All:
	default:
		return true;
	}
}

EElysiumReactionCondition ElysiumParseReactionCondition(const FString& GroupInternalName)
{
	if (GroupInternalName.Equals(TEXT("Reaction (Megalomaniac)"), ESearchCase::IgnoreCase))
	{
		return EElysiumReactionCondition::Megalomaniac;
	}
	if (GroupInternalName.Equals(TEXT("Reaction (Close to the Beast)"), ESearchCase::IgnoreCase))
	{
		return EElysiumReactionCondition::CloseToTheBeast;
	}
	if (GroupInternalName.Equals(TEXT("Reaction (Occult Nut)"), ESearchCase::IgnoreCase))
	{
		return EElysiumReactionCondition::OccultNut;
	}
	if (GroupInternalName.Equals(TEXT("Reaction (Dementation-Passion)"), ESearchCase::IgnoreCase))
	{
		return EElysiumReactionCondition::DementationPassion;
	}
	if (GroupInternalName.Equals(TEXT("Reaction (Presence-Awe)"), ESearchCase::IgnoreCase))
	{
		return EElysiumReactionCondition::PresenceAwe;
	}
	if (GroupInternalName.Equals(TEXT("Reaction (Presence-General)"), ESearchCase::IgnoreCase))
	{
		return EElysiumReactionCondition::PresenceGeneral;
	}
	return EElysiumReactionCondition::Unrecognized;
}

bool FElysiumReactionBandTable::Load(FString& OutError)
{
	Bands.Reset();
	bClamping = false;

	static const TCHAR* Rel = TEXT("system/reaction.txt");
	TSharedPtr<FKvNode> Root;
	if (!ReadVdata(Rel, Root, OutError))
	{
		return false;
	}
	const FKvNode* Data = RootBlock(Root, TEXT("ReactionsData"), Rel, OutError);
	if (Data == nullptr)
	{
		return false;
	}

	const FKvNode* General = Data->Child(TEXT("General"));
	const FKvNode* Table = General ? General->Child(TEXT("Table")) : nullptr;
	if (Table == nullptr)
	{
		OutError = FString::Printf(TEXT("no ReactionsData/General/Table block in %s"),
			*FElysiumContentPaths::VdataFile(Rel));
		return false;
	}
	bClamping = Table->Bool(TEXT("Clamping"), false);

	const FKvNode* Strings = Data->Child(TEXT("Strings"));
	const FKvNode* Level = Strings ? Strings->Child(TEXT("ReactionLevel")) : nullptr;

	// The row keys ARE the band index (0..N), authored in ascending order — the value at each is
	// that band's lower boundary. This differs from `FElysiumRuleTable`, whose numeric key indexes
	// an unrelated rating dimension. `Pairs` preserves file order, which the shipped table already
	// authors ascending, so no re-sort is needed.
	for (const TPair<FString, FString>& Pair : Table->Pairs)
	{
		if (!Pair.Key.IsNumeric())
		{
			continue;
		}
		FElysiumReactionBand Band;
		Band.LowerBoundary = FCString::Atoi(*Pair.Value);
		Band.Label = Level ? Level->Str(*FString::Printf(TEXT("Name%s"), *Pair.Key), FString()) : FString();
		Bands.Add(MoveTemp(Band));
	}

	if (Bands.IsEmpty())
	{
		OutError = FString::Printf(TEXT("no ReactionsData/General/Table rows in %s"),
			*FElysiumContentPaths::VdataFile(Rel));
		return false;
	}
	return true;
}

const FElysiumReactionBand* FElysiumReactionBandTable::Resolve(int32 Score) const
{
	const FElysiumReactionBand* Best = nullptr;
	for (const FElysiumReactionBand& Band : Bands)
	{
		if (Band.LowerBoundary <= Score && (Best == nullptr || Band.LowerBoundary > Best->LowerBoundary))
		{
			Best = &Band;
		}
	}
	return Best;
}

bool FElysiumReactionModifierTable::Load(FString& OutError)
{
	Modifiers.Reset();
	InertModifiers.Reset();

	static const TCHAR* Rel = TEXT("system/reactions000.txt");
	TSharedPtr<FKvNode> Root;
	if (!ReadVdata(Rel, Root, OutError))
	{
		return false;
	}
	const FKvNode* Data = RootBlock(Root, TEXT("ReactionsData"), Rel, OutError);
	if (Data == nullptr)
	{
		return false;
	}

	for (const TPair<FString, TSharedPtr<FKvNode>>& CatKid : Data->Kids)
	{
		if (CatKid.Key != TEXT("reactioncategory") || !CatKid.Value.IsValid())
		{
			continue;
		}
		const FString CategoryName = CatKid.Value->Str(TEXT("InternalName"), FString());

		for (const TPair<FString, TSharedPtr<FKvNode>>& GrpKid : CatKid.Value->Kids)
		{
			if (GrpKid.Key != TEXT("reactiongroup") || !GrpKid.Value.IsValid())
			{
				continue;
			}
			const FString GroupName = GrpKid.Value->Str(TEXT("InternalName"), FString());
			const EElysiumReactionCondition Condition = ElysiumParseReactionCondition(GroupName);

			for (const TPair<FString, TSharedPtr<FKvNode>>& RxKid : GrpKid.Value->Kids)
			{
				if (RxKid.Key != TEXT("reaction") || !RxKid.Value.IsValid())
				{
					continue;
				}
				const FKvNode& N = *RxKid.Value;

				FElysiumReactionModifier Mod;
				Mod.CategoryInternalName = CategoryName;
				Mod.GroupInternalName = GroupName;
				Mod.Condition = Condition;
				Mod.WhoModifies = N.Str(TEXT("WhoModifies"), FString());
				Mod.RawModifier = N.Str(TEXT("Modifier"), FString());

				if (const FString* Targets = N.Value(TEXT("Targets")))
				{
					ElysiumParseReactionTargets(*Targets, Mod.Targets);
				}
				else
				{
					// No `Targets` key at all — `Presence-Awe`/`Presence-General` ship this way. See
					// `FElysiumReactionTargets::bAuthored`'s comment in the header.
					Mod.Targets = FElysiumReactionTargets();
					Mod.Targets.bAuthored = false;
				}

				ElysiumParseReactionModifierExpr(Mod.RawModifier, Mod.Kind, Mod.ScalarValue, Mod.FormulaExpression);

				if (!Mod.IsRecognized())
				{
					InertModifiers.Add(FString::Printf(
						TEXT("%s (Condition=%s Kind=%s WhoModifies='%s' Modifier='%s')"),
						*Mod.GroupInternalName,
						Mod.Condition == EElysiumReactionCondition::Unrecognized ? TEXT("unrecognized") : TEXT("ok"),
						Mod.Kind == EElysiumReactionModifierKind::Unrecognized ? TEXT("unrecognized") : TEXT("ok"),
						*Mod.WhoModifies, *Mod.RawModifier));
				}

				Modifiers.Add(MoveTemp(Mod));
			}
		}
	}

	if (Modifiers.IsEmpty())
	{
		OutError = FString::Printf(TEXT("no ReactionsData/ReactionCategory rows in %s"),
			*FElysiumContentPaths::VdataFile(Rel));
		return false;
	}
	return true;
}

bool FElysiumReactionCatalogue::Load(FString& OutError)
{
	FString BandsError, ModifiersError;
	const bool bBandsOk = Bands.Load(BandsError);
	const bool bModifiersOk = Modifiers.Load(ModifiersError);

	if (!bBandsOk || !bModifiersOk)
	{
		TArray<FString> Errors;
		if (!bBandsOk) { Errors.Add(BandsError); }
		if (!bModifiersOk) { Errors.Add(ModifiersError); }
		OutError = FString::Join(Errors, TEXT(" | "));
	}
	// Bands are load-bearing (nothing to resolve a score to without them); Modifiers are
	// supplementary (an unmodified base score still resolves to a band), so the catalogue is usable
	// once Bands alone loaded — the `FElysiumSoundVolumeTable` fail-open shape.
	return bBandsOk;
}
