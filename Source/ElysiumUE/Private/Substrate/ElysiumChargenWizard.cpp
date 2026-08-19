#include "Substrate/ElysiumChargenWizard.h"

#include "ElysiumContentPaths.h"
#include "ElysiumKeyValues.h"

#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumVdataLoad.h"

namespace
{
	using ElysiumKeyValues::FKvNode;
	using ElysiumVdata::ReadVdata;
	using ElysiumVdata::RootBlock;
	using ElysiumVdata::Index;
}

// ================================================================================================
// 13. charcreatewizard.txt
// ================================================================================================

namespace
{
	FElysiumWizRegion ReadRegion(const FKvNode* Block, const TCHAR* Key)
	{
		FElysiumWizRegion R;
		const FKvNode* N = Block ? Block->Child(Key) : nullptr;
		if (N == nullptr)
		{
			return R;
		}
		R.X = N->Int(TEXT("X"), 0);
		R.Y = N->Int(TEXT("Y"), 0);
		R.Width = N->Int(TEXT("Width"), 0);
		R.Height = N->Int(TEXT("Height"), 0);
		R.bAuthored = true;
		return R;
	}

	// Every `Trait_Prereq` block directly under Block, in file order. An absent bound stays at the
	// unbounded sentinel rather than collapsing to 0, which would silently exclude a zero tally.
	void ReadPrereqs(const FKvNode* Block, TArray<FElysiumWizPrereq>& Out)
	{
		if (Block == nullptr)
		{
			return;
		}
		for (const TPair<FString, TSharedPtr<FKvNode>>& Kid : Block->Kids)
		{
			if (Kid.Key != TEXT("trait_prereq") || !Kid.Value.IsValid())
			{
				continue;
			}
			FElysiumWizPrereq P;
			P.Trait = Kid.Value->Str(TEXT("Trait"), FString());
			if (Kid.Value->Has(TEXT("MinVal"))) { P.MinVal = Kid.Value->Int(TEXT("MinVal"), 0); }
			if (Kid.Value->Has(TEXT("MaxVal"))) { P.MaxVal = Kid.Value->Int(TEXT("MaxVal"), 0); }
			Out.Add(MoveTemp(P));
		}
	}

	FElysiumWizAction ReadAction(const FKvNode& N)
	{
		FElysiumWizAction A;
		A.Text = N.Str(TEXT("Text"), FString());
		A.Next = N.Str(TEXT("Next"), FString());
		A.Trait = N.Str(TEXT("Trait"), FString());
		A.CharTemplate = N.Str(TEXT("CharTemplate"), FString());
		A.bSetGenderMale = N.Bool(TEXT("SetGenderMale"), false);
		A.bSetGenderFemale = N.Bool(TEXT("SetGenderFemale"), false);
		A.bIsCheckBox = N.Bool(TEXT("IsCheckBox"), false);
		A.bEndCharGenWiz = N.Bool(TEXT("EndCharGenWiz"), false);
		A.bProcessTraitChoices = N.Bool(TEXT("ProcessTraitChoices"), false);
		if (N.Has(TEXT("KeyLookup"))) { A.KeyLookup = N.Int(TEXT("KeyLookup"), 0); }
		ReadPrereqs(&N, A.Prereqs);
		A.Region = ReadRegion(&N, TEXT("Region"));
		return A;
	}

	FElysiumWizPopup ReadPopup(const FKvNode& N)
	{
		FElysiumWizPopup P;
		P.Text = N.Str(TEXT("Text"), FString());
		P.InternalName = N.Str(TEXT("InternalName"), FString());
		P.CharTemplate = N.Str(TEXT("CharTemplate"), FString());
		P.BkgImage = N.Str(TEXT("Bkg_Image"), FString());
		P.bOrderPrereqs = N.Bool(TEXT("Order_Prereqs"), false);
		P.bClanPrereqs = N.Bool(TEXT("Clan_Prereqs"), false);
		ReadPrereqs(&N, P.Prereqs);
		P.Region = ReadRegion(&N, TEXT("Region"));
		P.TextRegion = ReadRegion(&N, TEXT("TextRegion"));
		for (const TPair<FString, TSharedPtr<FKvNode>>& Kid : N.Kids)
		{
			if (Kid.Key == TEXT("action") && Kid.Value.IsValid())
			{
				P.Actions.Add(ReadAction(*Kid.Value));
			}
		}
		return P;
	}

	// Fold a group's `Defaults` into one of its popups: the popup wins wherever it authored
	// something, the defaults fill the rest. Actions inherit **by position**, because the defaults
	// block carries the geometry and the shared flags for slot 0, 1, 2 and the alternates, and a
	// concrete popup authors only the text and wiring for the slots it uses.
	void ApplyDefaults(const FElysiumWizPopup& Def, FElysiumWizPopup& P)
	{
		if (P.Text.IsEmpty())         { P.Text = Def.Text; }
		if (P.InternalName.IsEmpty()) { P.InternalName = Def.InternalName; }
		if (P.CharTemplate.IsEmpty()) { P.CharTemplate = Def.CharTemplate; }
		if (P.BkgImage.IsEmpty())     { P.BkgImage = Def.BkgImage; }
		if (!P.Region.bAuthored)      { P.Region = Def.Region; }
		if (!P.TextRegion.bAuthored)  { P.TextRegion = Def.TextRegion; }

		for (int32 i = 0; i < P.Actions.Num(); ++i)
		{
			if (!Def.Actions.IsValidIndex(i))
			{
				continue;
			}
			const FElysiumWizAction& D = Def.Actions[i];
			FElysiumWizAction& A = P.Actions[i];
			if (A.Text.IsEmpty())         { A.Text = D.Text; }
			if (A.Next.IsEmpty())         { A.Next = D.Next; }
			if (A.Trait.IsEmpty())        { A.Trait = D.Trait; }
			if (A.CharTemplate.IsEmpty()) { A.CharTemplate = D.CharTemplate; }
			if (!A.Region.bAuthored)      { A.Region = D.Region; }
			A.bSetGenderMale       = A.bSetGenderMale       || D.bSetGenderMale;
			A.bSetGenderFemale     = A.bSetGenderFemale     || D.bSetGenderFemale;
			A.bIsCheckBox          = A.bIsCheckBox          || D.bIsCheckBox;
			A.bEndCharGenWiz       = A.bEndCharGenWiz       || D.bEndCharGenWiz;
			A.bProcessTraitChoices = A.bProcessTraitChoices || D.bProcessTraitChoices;
			if (A.KeyLookup == INDEX_NONE) { A.KeyLookup = D.KeyLookup; }
		}
	}
}

int32 FElysiumWizClanNode::RankOf(const FString& Trait) const
{
	const FString F = ElysiumFold(Trait);
	for (const FString& T : Primary)   { if (ElysiumFold(T) == F) { return 0; } }
	for (const FString& T : Secondary) { if (ElysiumFold(T) == F) { return 1; } }
	for (const FString& T : Tertiary)  { if (ElysiumFold(T) == F) { return 2; } }
	return INDEX_NONE;
}

bool FElysiumWizard::Load(FString& OutError)
{
	Traits.Reset();
	Combinations.Reset();
	Orderings.Reset();
	ClanNodes.Reset();
	Groups.Reset();
	GroupNames.Reset();
	GroupByName.Reset();
	FMemory::Memzero(ConnectionScores);

	static const TCHAR* Rel = TEXT("system/charcreatewizard.txt");
	TSharedPtr<FKvNode> Root;
	if (!ReadVdata(Rel, Root, OutError))
	{
		return false;
	}
	const FKvNode* Wiz = RootBlock(Root, TEXT("CharCreateWizard"), Rel, OutError);
	if (Wiz == nullptr)
	{
		return false;
	}

	// The authored group order. It names the ten `*_Popups` blocks; those blocks are siblings at
	// wizard scope, so this list is what tells them apart from `Strings`/`Traits`/`Clan_Tables`.
	if (const FKvNode* Strings = Wiz->Child(TEXT("Strings")))
	{
		if (const FKvNode* Names = Strings->Child(TEXT("PopUpGroups")))
		{
			for (const TPair<FString, FString>& P : Names->Pairs) { GroupNames.Add(P.Value); }
		}
	}

	if (const FKvNode* TraitsBlock = Wiz->Child(TEXT("Traits")))
	{
		for (const TPair<FString, TSharedPtr<FKvNode>>& Kid : TraitsBlock->Kids)
		{
			if (Kid.Key == TEXT("trait") && Kid.Value.IsValid())
			{
				Traits.Add(Kid.Value->Str(TEXT("InternalName"), FString()));
			}
		}
		if (const FKvNode* Combos = TraitsBlock->Child(TEXT("TraitCombinations")))
		{
			for (const TPair<FString, TSharedPtr<FKvNode>>& Kid : Combos->Kids)
			{
				if (Kid.Key == TEXT("traitcombination") && Kid.Value.IsValid())
				{
					FElysiumWizCombination C;
					C.InternalName = Kid.Value->Str(TEXT("InternalName"), FString());
					Kid.Value->ValuesFor(TEXT("Trait"), C.Traits);   // repeats, and means them
					Combinations.Add(MoveTemp(C));
				}
			}
			if (const FKvNode* Ords = Combos->Child(TEXT("TraitOrderings")))
			{
				for (const TPair<FString, TSharedPtr<FKvNode>>& Kid : Ords->Kids)
				{
					if (Kid.Key != TEXT("traitordering") || !Kid.Value.IsValid())
					{
						continue;
					}
					FElysiumWizTraitOrdering O;
					O.TraitCombination = Kid.Value->Str(TEXT("TraitCombination"), FString());
					O.Index = Kid.Value->Str(TEXT("Index"), FString());
					O.Trait = Kid.Value->Str(TEXT("Trait"), FString());
					ReadPrereqs(Kid.Value.Get(), O.Prereqs);
					for (const TPair<FString, TSharedPtr<FKvNode>>& Step : Kid.Value->Kids)
					{
						if (Step.Key == TEXT("ordering") && Step.Value.IsValid())
						{
							FElysiumWizOrderingStep S;
							S.TraitCombination = Step.Value->Str(TEXT("TraitCombination"), FString());
							S.Index = Step.Value->Str(TEXT("Index"), FString());
							O.Orderings.Add(MoveTemp(S));
						}
					}
					Orderings.Add(MoveTemp(O));
				}
			}
		}
	}

	if (const FKvNode* Tables = Wiz->Child(TEXT("Clan_Tables")))
	{
		for (const TPair<FString, TSharedPtr<FKvNode>>& Kid : Tables->Kids)
		{
			if (Kid.Key != TEXT("clannode") || !Kid.Value.IsValid())
			{
				continue;
			}
			FElysiumWizClanNode C;
			C.CharTemplate = Kid.Value->Str(TEXT("CharTemplate"), FString());
			// A rank repeats -- Gangrel authors two Primaries -- so every value is kept, not the last.
			Kid.Value->ValuesFor(TEXT("Primary"), C.Primary);
			Kid.Value->ValuesFor(TEXT("Secondary"), C.Secondary);
			Kid.Value->ValuesFor(TEXT("Tertiary"), C.Tertiary);
			ClanNodes.Add(MoveTemp(C));
		}
		if (const FKvNode* Scores = Tables->Child(TEXT("ConnectionScores")))
		{
			int32 Sel = 0;
			for (const TPair<FString, TSharedPtr<FKvNode>>& Kid : Scores->Kids)
			{
				if (Kid.Key != TEXT("selection") || !Kid.Value.IsValid() || Sel >= 3)
				{
					continue;
				}
				ConnectionScores[Sel][0] = Kid.Value->Int(TEXT("Primary"), 0);
				ConnectionScores[Sel][1] = Kid.Value->Int(TEXT("Secondary"), 0);
				ConnectionScores[Sel][2] = Kid.Value->Int(TEXT("Tertiary"), 0);
				++Sel;
			}
		}
	}

	// The popup groups. Their block keys are the names `Strings.PopUpGroups` listed, so everything
	// else at wizard scope is skipped without needing a special case per sibling.
	TSet<FString> WantedGroups;
	for (const FString& N : GroupNames) { WantedGroups.Add(ElysiumFold(N)); }

	for (const TPair<FString, TSharedPtr<FKvNode>>& Kid : Wiz->Kids)
	{
		if (!Kid.Value.IsValid() || !WantedGroups.Contains(Kid.Key))
		{
			continue;
		}
		FElysiumWizGroup G;
		G.InternalName = Kid.Value->Str(TEXT("InternalName"), FString());
		if (const FKvNode* Next = Kid.Value->Child(TEXT("NextSection")))
		{
			G.NextSection.Next = Next->Str(TEXT("Next"), FString());
			if (Next->Has(TEXT("MinCount"))) { G.NextSection.MinCount = Next->Int(TEXT("MinCount"), 0); }
			if (Next->Has(TEXT("MaxCount"))) { G.NextSection.MaxCount = Next->Int(TEXT("MaxCount"), 0); }
		}
		if (const FKvNode* Def = Kid.Value->Child(TEXT("Defaults")))
		{
			G.Defaults = ReadPopup(*Def);
		}
		for (const TPair<FString, TSharedPtr<FKvNode>>& Sub : Kid.Value->Kids)
		{
			if (Sub.Key != TEXT("popup") || !Sub.Value.IsValid())
			{
				continue;
			}
			FElysiumWizPopup P = ReadPopup(*Sub.Value);
			ApplyDefaults(G.Defaults, P);
			G.Popups.Add(MoveTemp(P));
		}
		Index(GroupByName, G.InternalName.IsEmpty() ? Kid.Key : G.InternalName, Groups.Num());
		Groups.Add(MoveTemp(G));
	}

	if (Groups.IsEmpty())
	{
		OutError = FString::Printf(TEXT("no popup groups in %s"), *FElysiumContentPaths::VdataFile(Rel));
		return false;
	}
	return true;
}

const FElysiumWizGroup* FElysiumWizard::Group(const FString& InternalName) const
{
	const int32* Idx = GroupByName.Find(ElysiumFold(InternalName));
	return Idx ? &Groups[*Idx] : nullptr;
}

void FElysiumWizard::PopupsNamed(const FString& InternalName, TArray<const FElysiumWizPopup*>& Out) const
{
	const FString F = ElysiumFold(InternalName);
	for (const FElysiumWizGroup& G : Groups)
	{
		for (const FElysiumWizPopup& P : G.Popups)
		{
			if (ElysiumFold(P.InternalName) == F) { Out.Add(&P); }
		}
	}
}

const FElysiumWizClanNode* FElysiumWizard::ClanNode(const FString& CharTemplate) const
{
	const FString F = ElysiumFold(CharTemplate);
	for (const FElysiumWizClanNode& C : ClanNodes)
	{
		if (ElysiumFold(C.CharTemplate) == F) { return &C; }
	}
	return nullptr;
}

int32 FElysiumWizard::NumPopups() const
{
	int32 N = 0;
	for (const FElysiumWizGroup& G : Groups) { N += G.Popups.Num(); }
	return N;
}
