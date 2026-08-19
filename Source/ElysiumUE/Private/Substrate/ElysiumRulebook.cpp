#include "Substrate/ElysiumRulebook.h"

#include "ElysiumContentPaths.h"
#include "ElysiumKeyValues.h"

#include "Substrate/ElysiumVdataLoad.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	using ElysiumKeyValues::FKvNode;
	using ElysiumVdata::ReadVdata;
	using ElysiumVdata::RootBlock;
	using ElysiumVdata::Index;
	using ElysiumVdata::Trim;
}

// ================================================================================================
// Shared value types
// ================================================================================================

bool FElysiumStatCost::Parse(const FString& Raw)
{
	Kind = EKind::None;
	Amount = 0;
	Steps.Reset();

	const FString S = Trim(Raw);
	if (S.IsEmpty())
	{
		return false;
	}

	// `Table: a, b, c, …` — the per-rating price list.
	if (S.StartsWith(TEXT("Table:"), ESearchCase::IgnoreCase))
	{
		TArray<FString> Parts;
		S.Mid(6).ParseIntoArray(Parts, TEXT(","), true);
		for (const FString& P : Parts)
		{
			Steps.Add(FCString::Atoi(*Trim(P)));
		}
		if (Steps.IsEmpty())
		{
			return false;
		}
		Kind = EKind::Table;
		return true;
	}

	// `Current_Rating * N` — the ordinary per-dot price.
	if (S.StartsWith(TEXT("Current_Rating"), ESearchCase::IgnoreCase))
	{
		const int32 Star = S.Find(TEXT("*"));
		Amount = (Star != INDEX_NONE) ? FCString::Atoi(*Trim(S.Mid(Star + 1))) : 1;
		Kind = EKind::PerRating;
		return true;
	}

	Amount = FCString::Atoi(*S);
	Kind = EKind::Flat;
	return true;
}

int32 FElysiumStatCost::At(int32 CurrentRating) const
{
	switch (Kind)
	{
	case EKind::Flat:
		return Amount;
	case EKind::PerRating:
		// `Current_Rating` is the PRE-PURCHASE base rating, so buying the r -> r+1 dot costs N*r
		// and selling it back refunds exactly that.
		return Amount * FMath::Max(CurrentRating, 0);
	case EKind::Table:
		if (Steps.IsEmpty())
		{
			return CannotBuy;
		}
		return Steps[FMath::Clamp(CurrentRating, 0, Steps.Num() - 1)];
	default:
		return CannotBuy;
	}
}

FString FElysiumStatCost::Describe() const
{
	switch (Kind)
	{
	case EKind::Flat:      return FString::Printf(TEXT("%d"), Amount);
	case EKind::PerRating: return FString::Printf(TEXT("rating*%d"), Amount);
	case EKind::Table:
	{
		FString Out = TEXT("table[");
		for (int32 i = 0; i < Steps.Num(); ++i)
		{
			Out += (i ? TEXT(", ") : TEXT("")) + FString::FromInt(Steps[i]);
		}
		return Out + TEXT("]");
	}
	default: return TEXT("-");
	}
}

void FElysiumStatCosts::Load(const FKvNode& Node)
{
	const bool bNew = New.Parse(Node.Str(TEXT("New"), FString()));
	const bool bRaise = Raise.Parse(Node.Str(TEXT("Raise"), FString()));
	bPresent = bNew || bRaise;

	// `CVStatCost_t::Load` copies whichever one is authored into the other.
	if (bNew && !bRaise) { Raise = New; }
	else if (bRaise && !bNew) { New = Raise; }
}

FString FElysiumStatCosts::Describe() const
{
	return bPresent
		? FString::Printf(TEXT("new %s / raise %s"), *New.Describe(), *Raise.Describe())
		: FString(TEXT("(none)"));
}

FElysiumTraitRef FElysiumTraitRef::Parse(const FString& Raw)
{
	FElysiumTraitRef Out;
	const FString S = Trim(Raw);

	// `CVStatRef` strips a trailing ` / N` or ` * N` before resolving the name.
	int32 Op = INDEX_NONE;
	const int32 Slash = S.Find(TEXT("/"));
	const int32 Star = S.Find(TEXT("*"));
	if (Slash != INDEX_NONE && (Star == INDEX_NONE || Slash < Star)) { Op = Slash; }
	else if (Star != INDEX_NONE) { Op = Star; }

	if (Op == INDEX_NONE)
	{
		Out.Trait = S;
		return Out;
	}

	Out.Trait = Trim(S.Left(Op));
	const int32 N = FMath::Max(FCString::Atoi(*Trim(S.Mid(Op + 1))), 1);
	// The multiplier holds both operations: positive N multiplies, negative N divides.
	Out.Mul = (S[Op] == TEXT('/')) ? -N : N;
	return Out;
}

int32 FElysiumTraitRef::Apply(int32 Value) const
{
	if (Mul > 1) { return Value * Mul; }
	if (Mul < 0) { return Value / -Mul; }
	return Value;
}

FString FElysiumTraitRef::Describe() const
{
	if (Mul > 1) { return FString::Printf(TEXT("%s * %d"), *Trait, Mul); }
	if (Mul < 0) { return FString::Printf(TEXT("%s / %d"), *Trait, -Mul); }
	return Trait;
}

void FElysiumRuleTable::Load(const FKvNode& Node)
{
	InternalName = Node.Str(TEXT("InternalName"), FString());
	Name = Node.Str(TEXT("Name"), InternalName);
	TraitDependency = Node.Str(TEXT("TraitDependency"), FString());
	bClamping = Node.Bool(TEXT("Clamping"), false);

	// Every numeric key is a row; the named keys above are not.
	for (const TPair<FString, FString>& Pair : Node.Pairs)
	{
		if (Pair.Key.IsEmpty() || (!FChar::IsDigit(Pair.Key[0]) && Pair.Key[0] != TEXT('-')))
		{
			continue;
		}
		Rows.Add(FCString::Atoi(*Pair.Key), Pair.Value);
	}
}

const FString* FElysiumRuleTable::Find(int32 Key) const
{
	return Rows.Find(Key);
}

void FElysiumRuleTable::KeyRange(int32& OutMin, int32& OutMax) const
{
	OutMin = OutMax = 0;
	bool bFirst = true;
	for (const TPair<int32, FString>& Row : Rows)
	{
		if (bFirst) { OutMin = OutMax = Row.Key; bFirst = false; continue; }
		OutMin = FMath::Min(OutMin, Row.Key);
		OutMax = FMath::Max(OutMax, Row.Key);
	}
}

float FElysiumRuleTable::Lookup(int32 Key, float Def) const
{
	if (const FString* Exact = Rows.Find(Key))
	{
		return FCString::Atof(**Exact);
	}
	if (!bClamping || Rows.IsEmpty())
	{
		return Def;
	}
	int32 Lo = 0, Hi = 0;
	KeyRange(Lo, Hi);
	if (const FString* Clamped = Rows.Find(FMath::Clamp(Key, Lo, Hi)))
	{
		return FCString::Atof(**Clamped);
	}
	return Def;
}

// ================================================================================================
// 1. stats.txt
// ================================================================================================

// `ElysiumTraitContainerName` lives with the slot tables in `ElysiumSheet.cpp`.

const FElysiumStat* FElysiumStatContainer::Find(const FString& InName) const
{
	const int32* Idx = ByName.Find(ElysiumFold(InName));
	return Idx ? &Stats[*Idx] : nullptr;
}

const FElysiumStat* FElysiumStatContainer::At(int32 Index) const
{
	return Stats.IsValidIndex(Index) ? &Stats[Index] : nullptr;
}

int32 FElysiumStatContainer::IndexOf(const FString& InName) const
{
	const int32* Idx = ByName.Find(ElysiumFold(InName));
	return Idx ? *Idx : INDEX_NONE;
}

const FElysiumStatCosts& FElysiumStatContainer::CostsFor(const FElysiumStat& Stat) const
{
	return Stat.Costs.bPresent ? Stat.Costs : DefaultCosts;
}

void FElysiumStatContainer::Load(const FKvNode& Node, const FString& BlockName)
{
	Stats.Reset();
	ByName.Reset();

	// The container's block name and its `InternalName` diverge on one of the four: the block is
	// `ActiveDisciplines`, the engine key is `Active_Disciplines`. The engine key wins.
	InternalName = Node.Str(TEXT("InternalName"), BlockName);

	if (const FKvNode* Costs = Node.Child(TEXT("Costs")))
	{
		DefaultCosts.Load(*Costs);
	}

	for (const TPair<FString, TSharedPtr<FKvNode>>& Kid : Node.Kids)
	{
		if (Kid.Key != TEXT("stat") || !Kid.Value.IsValid())
		{
			continue;
		}
		const FKvNode& N = *Kid.Value;

		FElysiumStat Stat;
		Stat.Index = Stats.Num();      // file position IS the engine's trait id
		Stat.InternalName = N.Str(TEXT("InternalName"), FString());
		Stat.Name = N.Str(TEXT("Name"), Stat.InternalName);

		Stat.MinExpr = N.Str(TEXT("Min"), FString());
		Stat.MaxExpr = N.Str(TEXT("Max"), FString());
		Stat.Min = N.Int(TEXT("Min"), 0);
		Stat.Max = N.Int(TEXT("Max"), 0);
		Stat.Default = N.Int(TEXT("Default"), 0);
		// CVStatInfo_t::PostLoad defaults the sell/buy bounds to the hard bounds.
		Stat.MinSell = N.Int(TEXT("MinSell"), Stat.Min);
		Stat.MaxBuy = N.Int(TEXT("MaxBuy"), Stat.Max);

		// The sheet's detail panel: the trait blurb, then the per-rating pair. `HelpTextH%d` /
		// `HelpTextL%d` are 1-based and only the disciplines author them, so both lists are probed
		// rather than sized — an absent rating is an empty entry, never a shift.
		Stat.HelpText = N.Str(TEXT("HelpText"), FString());
		Stat.HelpText2 = N.Str(TEXT("HelpText2"), FString());
		for (int32 Level = 1; Level <= 5; ++Level)
		{
			Stat.LevelHeadings.Add(N.Str(*FString::Printf(TEXT("HelpTextH%d"), Level), FString()));
			Stat.LevelDetails.Add(N.Str(*FString::Printf(TEXT("HelpTextL%d"), Level), FString()));
		}

		Stat.NameMapping = N.Str(TEXT("NameMapping"), FString());
		Stat.NameFunc = N.Str(TEXT("NameFunc"), FString());
		Stat.bDisabled = N.Bool(TEXT("Disabled"), false);
		N.ValuesFor(TEXT("IncPredependency"), Stat.IncPredependency);

		// The Discipline classification bytes (`docs/vtmb/disciplines.md`). `Is_Instant` is the one
		// the shared cast authority branches on; the other three are authored beside it.
		Stat.bIsInstant = N.Bool(TEXT("Is_Instant"), false);
		Stat.bIsPassive = N.Bool(TEXT("Is_Passive"), false);
		Stat.bIsRenewable = N.Bool(TEXT("Is_Renewable"), false);
		Stat.bIsAggressive = N.Bool(TEXT("Is_Aggressive"), false);

		if (const FKvNode* Durations = N.Child(TEXT("Durations")))
		{
			Stat.Durations.bAuthored = true;
			for (int32 Level = 1; Level <= 5; ++Level)
			{
				Stat.Durations.Initial[Level] =
					Durations->Int(*FString::Printf(TEXT("Initial_%d"), Level), 0);
				Stat.Durations.Add[Level] =
					Durations->Int(*FString::Printf(TEXT("Add_%d"), Level), 0);
			}
		}

		if (const FKvNode* Costs = N.Child(TEXT("Costs")))
		{
			Stat.Costs.Load(*Costs);
		}
		for (const TPair<FString, TSharedPtr<FKvNode>>& Sub : N.Kids)
		{
			if (Sub.Key == TEXT("table") && Sub.Value.IsValid())
			{
				FElysiumRuleTable Table;
				Table.Load(*Sub.Value);
				Stat.Tables.Add(ElysiumFold(Table.InternalName), MoveTemp(Table));
			}
			else if (Sub.Key == TEXT("action") && Sub.Value.IsValid())
			{
				const FKvNode& A = *Sub.Value;
				FElysiumStat::FAction Action;
				// `Triggers` is authored as `Inc` (and, in commented-out rows, `Inc | Enter`); a
				// row that names Inc at all fires on the increment rather than standing as state.
				Action.bOnIncrement = A.Str(TEXT("Triggers"), FString()).Contains(TEXT("Inc"));
				Action.ValueExpr = Trim(A.Str(TEXT("Value"), FString()));
				Action.Predependency = Trim(A.Str(TEXT("Predependency"), FString()));
				Action.StatMutation = Trim(A.Str(TEXT("Stat"), FString()));
				Action.Effect = Trim(A.Str(TEXT("Effect"), FString()));
				Action.Function = Trim(A.Str(TEXT("Function"), FString()));
				Action.bHasParticle = A.Child(TEXT("Particle")) != nullptr;
				Stat.Actions.Add(MoveTemp(Action));
			}
		}

		Index(ByName, Stat.InternalName, Stat.Index);
		Stats.Add(MoveTemp(Stat));
	}
}

bool FElysiumStatTable::Load(FString& OutError)
{
	for (FElysiumStatContainer& C : Containers)
	{
		C = FElysiumStatContainer();
	}

	static const TCHAR* Rel = TEXT("system/stats.txt");
	TSharedPtr<FKvNode> Root;
	if (!ReadVdata(Rel, Root, OutError))
	{
		return false;
	}
	const FKvNode* Data = RootBlock(Root, TEXT("StatData"), Rel, OutError);
	if (Data == nullptr)
	{
		return false;
	}

	// The block names, in the order the engine indexes the containers.
	static const TCHAR* Blocks[] = { TEXT("Attributes"), TEXT("Abilities"), TEXT("Disciplines"),
		TEXT("ActiveDisciplines") };
	for (int32 i = 0; i < (int32)EElysiumTraitContainer::Count; ++i)
	{
		if (const FKvNode* Block = Data->Child(Blocks[i]))
		{
			Containers[i].Load(*Block, Blocks[i]);
		}
	}

	if (!IsValid())
	{
		OutError = FString::Printf(TEXT("no stat containers in %s"),
			*FElysiumContentPaths::VdataFile(Rel));
		return false;
	}
	return true;
}

const FElysiumStat* FElysiumStatTable::Find(const FString& InName,
	EElysiumTraitContainer* OutContainer) const
{
	for (int32 i = 0; i < (int32)EElysiumTraitContainer::Count; ++i)
	{
		if (const FElysiumStat* Stat = Containers[i].Find(InName))
		{
			if (OutContainer) { *OutContainer = (EElysiumTraitContainer)i; }
			return Stat;
		}
	}
	return nullptr;
}

// ================================================================================================
// FElysiumStat::FAction — the `Value` predicate
// ================================================================================================

bool FElysiumStat::FAction::Admits(int32 Value) const
{
	const FString S = Trim(ValueExpr);
	if (S.IsEmpty())
	{
		return false;   // an unauthored predicate admits nothing rather than everything
	}
	if (S.StartsWith(TEXT(">=")))
	{
		return Value >= FCString::Atoi(*Trim(S.Mid(2)));
	}
	if (S.StartsWith(TEXT("<=")))
	{
		return Value <= FCString::Atoi(*Trim(S.Mid(2)));
	}
	if (S.StartsWith(TEXT("!=")))
	{
		return Value != FCString::Atoi(*Trim(S.Mid(2)));
	}
	if (S.StartsWith(TEXT("==")))
	{
		return Value == FCString::Atoi(*Trim(S.Mid(2)));
	}
	if (S.StartsWith(TEXT(">")))
	{
		return Value > FCString::Atoi(*Trim(S.Mid(1)));
	}
	if (S.StartsWith(TEXT("<")))
	{
		return Value < FCString::Atoi(*Trim(S.Mid(1)));
	}
	if (!S.IsNumeric())
	{
		return false;   // a predicate we cannot read admits nothing
	}
	return Value == FCString::Atoi(*S);
}

// ================================================================================================
// 2. feats.txt
// ================================================================================================

// Probing until a key is ABSENT is the rule, not scanning a fixed range: `feats.txt` comments out
// a `Base2` while leaving `Base0`/`Base1` live, so a range scan would resurrect it the moment the
// authors parked another one at a lower index.
void FElysiumFeatTable::ProbeTraitRefs(const FKvNode& Node, const TCHAR* Prefix,
	TArray<FElysiumTraitRef>& Out)
{
	for (int32 i = 0; ; ++i)
	{
		const FString Key = FString::Printf(TEXT("%s%d"), Prefix, i);
		const FString* Value = Node.Value(*Key);
		if (Value == nullptr)
		{
			return;
		}
		Out.Add(FElysiumTraitRef::Parse(*Value));
	}
}

bool FElysiumFeatTable::Load(FString& OutError)
{
	Feats.Reset();
	ByName.Reset();

	static const TCHAR* Rel = TEXT("system/feats.txt");
	TSharedPtr<FKvNode> Root;
	if (!ReadVdata(Rel, Root, OutError))
	{
		return false;
	}
	const FKvNode* Data = RootBlock(Root, TEXT("FeatData"), Rel, OutError);
	const FKvNode* List = Data ? Data->Child(TEXT("Feats")) : nullptr;
	if (List == nullptr)
	{
		if (OutError.IsEmpty())
		{
			OutError = FString::Printf(TEXT("no FeatData/Feats block in %s"),
				*FElysiumContentPaths::VdataFile(Rel));
		}
		return false;
	}

	for (const TPair<FString, TSharedPtr<FKvNode>>& Kid : List->Kids)
	{
		if (Kid.Key != TEXT("feat") || !Kid.Value.IsValid())
		{
			continue;
		}
		const FKvNode& N = *Kid.Value;

		FElysiumFeat Feat;
		Feat.Index = Feats.Num();
		Feat.InternalName = N.Str(TEXT("InternalName"), FString());
		Feat.Name = N.Str(TEXT("Name"), Feat.InternalName);
		Feat.MaxValue = N.Int(TEXT("MaxValue"), 10);
		Feat.PcWeighting = N.Str(TEXT("PCWeighting"), FString());
		Feat.NpcWeighting = N.Str(TEXT("NPCWeighting"), FString());
		ProbeTraitRefs(N, TEXT("Base"), Feat.Bases);
		ProbeTraitRefs(N, TEXT("Automatic"), Feat.Automatics);
		// `Display2nd%d` is a dead key — no shipped feat sets one — so it is deliberately not read.

		for (const TPair<FString, TSharedPtr<FKvNode>>& Sub : N.Kids)
		{
			if (Sub.Key == TEXT("table") && Sub.Value.IsValid())
			{
				FElysiumRuleTable Table;
				Table.Load(*Sub.Value);
				Feat.Tables.Add(ElysiumFold(Table.InternalName), MoveTemp(Table));
			}
		}

		Index(ByName, Feat.InternalName, Feat.Index);
		Feats.Add(MoveTemp(Feat));
	}

	if (Feats.IsEmpty())
	{
		OutError = FString::Printf(TEXT("no feats in %s"), *FElysiumContentPaths::VdataFile(Rel));
		return false;
	}
	return true;
}

void FElysiumFeatTable::Reindex()
{
	ByName.Reset();
	for (int32 i = 0; i < Feats.Num(); ++i)
	{
		Feats[i].Index = i;
		Index(ByName, Feats[i].InternalName, i);
	}
}

const FElysiumFeat* FElysiumFeatTable::Find(const FString& InName) const
{
	const int32* Idx = ByName.Find(ElysiumFold(InName));
	return Idx ? &Feats[*Idx] : nullptr;
}

const FElysiumFeat* FElysiumFeatTable::At(int32 Index) const
{
	return Feats.IsValidIndex(Index) ? &Feats[Index] : nullptr;
}

// ================================================================================================
// 3. rules.txt + rules_tables.txt
// ================================================================================================

bool FElysiumRules::Load(FString& OutError)
{
	BlockOrder.Reset();
	Blocks.Reset();
	Tables.Reset();
	TableByName.Reset();

	static const TCHAR* RulesRel = TEXT("system/rules.txt");
	TSharedPtr<FKvNode> Root;
	if (!ReadVdata(RulesRel, Root, OutError))
	{
		return false;
	}
	const FKvNode* Data = RootBlock(Root, TEXT("RuleData"), RulesRel, OutError);
	if (Data == nullptr)
	{
		return false;
	}

	// Each block's key set differs and the Unofficial Patch tunes them, so the whole block is kept
	// as authored rather than mapped onto fields a patch could outgrow. Blocks nest — the blood/
	// health ratio is `VampHeal_Info { VampFeedingHeal_Info { … } }` — so a nested one is stored
	// under its dotted PATH and only the top-level names go into `BlockOrder`.
	TFunction<void(const FKvNode&, const FString&)> ReadBlock =
		[this, &ReadBlock](const FKvNode& Node, const FString& Path)
	{
		TMap<FString, FString>& Block = Blocks.FindOrAdd(Path);
		for (const TPair<FString, FString>& Pair : Node.Pairs)
		{
			Block.Add(Pair.Key, Pair.Value);
		}
		for (const TPair<FString, TSharedPtr<FKvNode>>& Sub : Node.Kids)
		{
			if (Sub.Value.IsValid())
			{
				ReadBlock(*Sub.Value, FString::Printf(TEXT("%s.%s"), *Path, *Sub.Key));
			}
		}
	};
	for (const TPair<FString, TSharedPtr<FKvNode>>& Kid : Data->Kids)
	{
		if (!Kid.Value.IsValid())
		{
			continue;
		}
		ReadBlock(*Kid.Value, Kid.Key);
		BlockOrder.AddUnique(Kid.Key);
	}
	if (Blocks.IsEmpty())
	{
		OutError = FString::Printf(TEXT("no rule blocks in %s"),
			*FElysiumContentPaths::VdataFile(RulesRel));
		return false;
	}

	// The tables live in the second file, under the same root key.
	static const TCHAR* TablesRel = TEXT("system/rules_tables.txt");
	TSharedPtr<FKvNode> TableRoot;
	FString TableError;
	if (ReadVdata(TablesRel, TableRoot, TableError))
	{
		const FKvNode* TableData = TableRoot.IsValid() ? TableRoot->Child(TEXT("RuleData")) : nullptr;
		const FKvNode* List = TableData ? TableData->Child(TEXT("Tables")) : nullptr;
		if (List != nullptr)
		{
			for (const TPair<FString, TSharedPtr<FKvNode>>& Kid : List->Kids)
			{
				if (Kid.Key != TEXT("table") || !Kid.Value.IsValid())
				{
					continue;
				}
				FElysiumRuleTable Table;
				Table.Load(*Kid.Value);
				Index(TableByName, Table.InternalName, Tables.Num());
				Tables.Add(MoveTemp(Table));
			}
		}
	}
	if (Tables.IsEmpty())
	{
		// The constants loaded; name the half that did not rather than the whole rulebook.
		OutError = TableError.IsEmpty()
			? FString::Printf(TEXT("no RuleData/Tables rows in %s"),
				*FElysiumContentPaths::VdataFile(TablesRel))
			: TableError;
		return false;
	}
	return true;
}

const FString* FElysiumRules::Raw(const TCHAR* Block, const TCHAR* Key) const
{
	const TMap<FString, FString>* B = Blocks.Find(ElysiumFold(Block));
	return B ? B->Find(ElysiumFold(Key)) : nullptr;
}

bool FElysiumRules::Has(const TCHAR* Block, const TCHAR* Key) const
{
	return Raw(Block, Key) != nullptr;
}

int32 FElysiumRules::Int(const TCHAR* Block, const TCHAR* Key, int32 Def) const
{
	const FString* V = Raw(Block, Key);
	return V ? FCString::Atoi(**V) : Def;
}

float FElysiumRules::Flt(const TCHAR* Block, const TCHAR* Key, float Def) const
{
	const FString* V = Raw(Block, Key);
	return V ? FCString::Atof(**V) : Def;
}

FString FElysiumRules::Str(const TCHAR* Block, const TCHAR* Key, const FString& Def) const
{
	const FString* V = Raw(Block, Key);
	return V ? *V : Def;
}

const FElysiumRuleTable* FElysiumRules::Table(const FString& InName) const
{
	const int32* Idx = TableByName.Find(ElysiumFold(InName));
	return Idx ? &Tables[*Idx] : nullptr;
}

// ================================================================================================
// 4. traiteffect.txt + traiteffects000.txt
// ================================================================================================

const TCHAR* ElysiumTraitOpName(EElysiumTraitOp Op)
{
	switch (Op)
	{
	case EElysiumTraitOp::Add:       return TEXT("+");
	case EElysiumTraitOp::Mul:       return TEXT("*");
	case EElysiumTraitOp::Div:       return TEXT("/");
	case EElysiumTraitOp::Max:       return TEXT("Max");
	case EElysiumTraitOp::Min:       return TEXT("Min");
	case EElysiumTraitOp::Percent:   return TEXT("%");
	case EElysiumTraitOp::Value:     return TEXT("Value");
	case EElysiumTraitOp::Cost:      return TEXT("Cost");
	case EElysiumTraitOp::BloodCost: return TEXT("BloodCost");
	case EElysiumTraitOp::Damage:    return TEXT("Damage");
	case EElysiumTraitOp::Duration:  return TEXT("Duration");
	default:                         return TEXT("?");
	}
}

bool FElysiumModifierNames::Load(FString& OutError)
{
	Names.Reset();

	static const TCHAR* Rel = TEXT("system/traiteffect.txt");
	TSharedPtr<FKvNode> Root;
	if (!ReadVdata(Rel, Root, OutError))
	{
		return false;
	}
	const FKvNode* Data = RootBlock(Root, TEXT("TraitEffectsData"), Rel, OutError);
	const FKvNode* General = Data ? Data->Child(TEXT("General")) : nullptr;
	const FKvNode* Ops = General ? General->Child(TEXT("ModifierNames")) : nullptr;
	if (Ops == nullptr)
	{
		if (OutError.IsEmpty())
		{
			OutError = FString::Printf(TEXT("no General/ModifierNames block in %s"),
				*FElysiumContentPaths::VdataFile(Rel));
		}
		return false;
	}

	// Keys are the operator indices, so the array is filled by index rather than by order.
	for (const TPair<FString, FString>& Pair : Ops->Pairs)
	{
		const int32 Idx = FCString::Atoi(*Pair.Key);
		if (Idx < 0 || Idx > 64)
		{
			continue;
		}
		if (Names.Num() <= Idx)
		{
			Names.SetNum(Idx + 1);
		}
		Names[Idx] = Pair.Value;
	}

	if (Names.IsEmpty())
	{
		OutError = FString::Printf(TEXT("ModifierNames in %s has no rows"),
			*FElysiumContentPaths::VdataFile(Rel));
		return false;
	}
	return true;
}

bool FElysiumModifierNames::Match(const FString& Modifier, EElysiumTraitOp& OutOp,
	FString& OutRest) const
{
	OutRest = Trim(Modifier);
	// Index 0 is `+`, which is never matched by name — an unrecognised modifier falls to it with a
	// signed integer, and that is what `"+1"` and `"-2"` take. So the scan starts at 1.
	for (int32 i = 1; i < Names.Num(); ++i)
	{
		if (Names[i].IsEmpty() || !OutRest.StartsWith(Names[i], ESearchCase::IgnoreCase))
		{
			continue;
		}
		OutOp = (EElysiumTraitOp)i;
		OutRest = Trim(OutRest.Mid(Names[i].Len()));
		return true;
	}
	OutOp = EElysiumTraitOp::Add;
	return false;
}

void FElysiumTraitEffects::ParseModifier(const FString& Raw, FElysiumTraitEffect& Out) const
{
	Out.RawModifier = Raw;
	Out.Op = EElysiumTraitOp::Add;
	Out.Amount = 0;
	Out.bPercent = false;
	Out.ValueName.Reset();

	FString Rest;
	Operators.Match(Raw, Out.Op, Rest);

	if (Out.Op == EElysiumTraitOp::Value)
	{
		// `Value` takes a NAMED payload as readily as a number (`"Value Clawed_Form"`).
		if (!Rest.IsEmpty() && !FChar::IsDigit(Rest[0]) && Rest[0] != TEXT('-') && Rest[0] != TEXT('+'))
		{
			Out.ValueName = Rest;
			return;
		}
	}

	if (Rest.EndsWith(TEXT("%")))
	{
		Out.bPercent = true;
		Rest = Trim(Rest.LeftChop(1));
	}
	Out.Amount = FCString::Atoi(*Rest);
}

bool FElysiumTraitEffects::Load(FString& OutError)
{
	Categories.Reset();
	Groups.Reset();
	ByName.Reset();

	// The vocabulary first: without it a `Modifier` string cannot be read.
	if (!Operators.Load(OutError))
	{
		return false;
	}

	static const TCHAR* Rel = TEXT("system/traiteffects000.txt");
	TSharedPtr<FKvNode> Root;
	if (!ReadVdata(Rel, Root, OutError))
	{
		return false;
	}
	const FKvNode* Data = RootBlock(Root, TEXT("TraitEffectsData"), Rel, OutError);
	if (Data == nullptr)
	{
		return false;
	}

	for (const TPair<FString, TSharedPtr<FKvNode>>& CatKid : Data->Kids)
	{
		if (CatKid.Key != TEXT("traiteffectcategory") || !CatKid.Value.IsValid())
		{
			continue;
		}
		const FString Category = CatKid.Value->Str(TEXT("InternalName"), FString());
		Categories.Add(Category);

		for (const TPair<FString, TSharedPtr<FKvNode>>& GrpKid : CatKid.Value->Kids)
		{
			if (GrpKid.Key != TEXT("traiteffectgroup") || !GrpKid.Value.IsValid())
			{
				continue;
			}
			FElysiumTraitEffectGroup Group;
			Group.Category = Category;
			Group.InternalName = GrpKid.Value->Str(TEXT("InternalName"), FString());

			for (const TPair<FString, TSharedPtr<FKvNode>>& FxKid : GrpKid.Value->Kids)
			{
				// `UNUSED_TraitEffect` is the authors' own way of parking one — skipped, not loaded.
				if (FxKid.Key != TEXT("traiteffect") || !FxKid.Value.IsValid())
				{
					continue;
				}
				const FKvNode& N = *FxKid.Value;

				FElysiumTraitEffect Fx;
				Fx.Trait = N.Str(TEXT("Trait"), FString());
				Fx.DisplayOverride = N.Int(TEXT("DisplayOverride"), -1);

				// The key the code reads is `Modifier`. The file's own header comment says
				// `Modify` and is stale.
				const FString* Modifier = N.Value(TEXT("Modifier"));
				const FKvNode* Costs = N.Child(TEXT("Costs"));
				if (Modifier != nullptr)
				{
					ParseModifier(*Modifier, Fx);
				}
				else if (Costs != nullptr)
				{
					// A `Costs` block with no `Modifier` IS the operator: it replaces the trait's
					// price for this character.
					Fx.Op = EElysiumTraitOp::Cost;
				}
				if (Costs != nullptr)
				{
					Fx.Costs.Load(*Costs);
				}

				Group.Effects.Add(MoveTemp(Fx));
			}

			Add(MoveTemp(Group));
		}
	}

	if (Groups.IsEmpty())
	{
		OutError = FString::Printf(TEXT("no trait-effect groups in %s"),
			*FElysiumContentPaths::VdataFile(Rel));
		return false;
	}
	return true;
}

void FElysiumTraitEffects::Add(FElysiumTraitEffectGroup&& Group)
{
	Index(ByName, Group.InternalName, Groups.Num());
	Groups.Add(MoveTemp(Group));
}

const FElysiumTraitEffectGroup* FElysiumTraitEffects::Find(const FString& InName) const
{
	const int32* Idx = ByName.Find(ElysiumFold(InName));
	return Idx ? &Groups[*Idx] : nullptr;
}

int32 FElysiumTraitEffects::NumEffects() const
{
	int32 N = 0;
	for (const FElysiumTraitEffectGroup& G : Groups) { N += G.Effects.Num(); }
	return N;
}

// ================================================================================================
// 5. clandoc000.txt + npctemplate*.txt
// ================================================================================================

namespace
{
	void LoadTraitBlock(const FKvNode* Node, TMap<FString, int32>& Out,
		TMap<FString, FString>* OutText = nullptr)
	{
		if (Node == nullptr)
		{
			return;
		}
		// Keys are trait InternalNames; a key that is absent means "inherit", not zero, which is
		// why an authored zero and a missing key must stay distinguishable.
		for (const TPair<FString, FString>& Pair : Node->Pairs)
		{
			Out.Add(Pair.Key, FCString::Atoi(*Pair.Value));
			// A symbolic value would otherwise vanish into a 0 — the Attributes block carries six of
			// them, including the two trait ORDERS that decide the chargen point pools.
			if (OutText && !Pair.Value.IsNumeric())
			{
				OutText->Add(Pair.Key, Pair.Value);
			}
		}
	}

	void LoadClanBlock(const FKvNode& N, const FString& SourceFile, int32 Idx,
		FElysiumClanTemplate& Out)
	{
		Out.SourceFile = SourceFile;
		Out.Index = Idx;

		if (const FKvNode* Text = N.Child(TEXT("Text")))
		{
			Out.TemplateName = Text->Str(TEXT("TemplateName"), FString());
			Out.ParentTemplateName = Text->Str(TEXT("ParentTemplateName"), FString());
			Out.Name = Text->Str(TEXT("Name"), FString());
			Out.Description = Text->Str(TEXT("Description"), FString());
		}
		if (const FKvNode* General = N.Child(TEXT("General")))
		{
			for (const TPair<FString, FString>& Pair : General->Pairs)
			{
				Out.General.Add(Pair.Key, Pair.Value);
			}
		}
		LoadTraitBlock(N.Child(TEXT("Attributes")), Out.Attributes, &Out.TraitText);
		LoadTraitBlock(N.Child(TEXT("Abilities")), Out.Abilities, &Out.TraitText);
		LoadTraitBlock(N.Child(TEXT("Disciplines")), Out.Disciplines, &Out.TraitText);
		LoadTraitBlock(N.Child(TEXT("Numina")), Out.Numina, &Out.TraitText);
		LoadTraitBlock(N.Child(TEXT("Resistances")), Out.Resistances, &Out.TraitText);

		if (const FKvNode* Reactions = N.Child(TEXT("Reactions")))
		{
			// `To` is the clan -> signed-modifier map; `From` is authored empty everywhere.
			LoadTraitBlock(Reactions->Child(TEXT("To")), Out.Reactions);
		}
		if (const FKvNode* Loiter = N.Child(TEXT("LoiterActivities")))
		{
			for (const TPair<FString, FString>& Pair : Loiter->Pairs)
			{
				Out.LoiterActivities.Add(Pair.Key, FCString::Atof(*Pair.Value));
			}
		}
	}

	// Both files hang their rows off the same root key, so one reader serves both.
	bool LoadClanFile(const FString& Rel, TArray<FElysiumClanTemplate>& Out, FString& OutError)
	{
		TSharedPtr<FKvNode> Root;
		if (!ReadVdata(*Rel, Root, OutError))
		{
			return false;
		}
		const FKvNode* Tables = Root.IsValid() ? Root->Child(TEXT("ClanDataTables")) : nullptr;
		if (Tables == nullptr)
		{
			OutError = FString::Printf(TEXT("no ClanDataTables block in %s"),
				*FElysiumContentPaths::VdataFile(Rel));
			return false;
		}
		// An empty ClanDataTables is legal — npctemplate019 and 021 ship exactly that.
		int32 Idx = 0;
		for (const TPair<FString, TSharedPtr<FKvNode>>& Kid : Tables->Kids)
		{
			if (Kid.Key != TEXT("clandata") || !Kid.Value.IsValid())
			{
				continue;
			}
			FElysiumClanTemplate Row;
			LoadClanBlock(*Kid.Value, Rel, Idx++, Row);
			Out.Add(MoveTemp(Row));
		}
		return true;
	}
}

bool FElysiumClanTemplate::HasGeneral(const TCHAR* Key) const
{
	return General.Contains(ElysiumFold(Key));
}

FString FElysiumClanTemplate::GeneralStr(const TCHAR* Key, const FString& Def) const
{
	const FString* V = General.Find(ElysiumFold(Key));
	return V ? *V : Def;
}

int32 FElysiumClanTemplate::GeneralInt(const TCHAR* Key, int32 Def) const
{
	const FString* V = General.Find(ElysiumFold(Key));
	return V ? FCString::Atoi(**V) : Def;
}

const int32* FElysiumClanTemplate::Trait(const FString& InName) const
{
	const FString Key = ElysiumFold(InName);
	if (const int32* V = Attributes.Find(Key)) { return V; }
	if (const int32* V = Abilities.Find(Key)) { return V; }
	if (const int32* V = Disciplines.Find(Key)) { return V; }
	if (const int32* V = Numina.Find(Key)) { return V; }
	return Resistances.Find(Key);
}

FString FElysiumClanTemplate::TraitStr(const FString& InName, const FString& Def) const
{
	const FString* V = TraitText.Find(ElysiumFold(InName));
	return V ? *V : Def;
}

bool FElysiumClanTable::Load(FString& OutError)
{
	Clans.Reset();
	NpcTemplates.Reset();
	NpcFiles.Reset();
	ClanByName.Reset();
	NpcByName.Reset();

	if (!LoadClanFile(TEXT("system/clandoc000.txt"), Clans, OutError))
	{
		return false;
	}
	for (int32 i = 0; i < Clans.Num(); ++i)
	{
		Index(ClanByName, Clans[i].TemplateName, i);
	}

	// The NPC templates are a whole directory, so they are discovered rather than listed: the
	// Unofficial Patch adds named ones (`npctemplate_cdc`) beside the numbered set.
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(FElysiumContentPaths::VdataDir() / TEXT("system")
		/ TEXT("npctemplate*.txt")), true, false);
	Files.Sort();
	for (const FString& Leaf : Files)
	{
		const FString Rel = TEXT("system/") + Leaf;
		FString FileError;
		if (!LoadClanFile(Rel, NpcTemplates, FileError))
		{
			// One unreadable template file must not cost the other 35.
			OutError = FileError;
			continue;
		}
		NpcFiles.Add(Leaf);
	}
	for (int32 i = 0; i < NpcTemplates.Num(); ++i)
	{
		Index(NpcByName, NpcTemplates[i].TemplateName, i);
	}

	if (Clans.IsEmpty())
	{
		OutError = TEXT("no clan templates in system/clandoc000.txt");
		return false;
	}
	OutError.Reset();
	return true;
}

const FElysiumClanTemplate* FElysiumClanTable::Find(const FString& TemplateName) const
{
	const FString Key = ElysiumFold(TemplateName);
	if (const int32* Idx = ClanByName.Find(Key)) { return &Clans[*Idx]; }
	if (const int32* Idx = NpcByName.Find(Key)) { return &NpcTemplates[*Idx]; }
	return nullptr;
}

const FElysiumClanTemplate* FElysiumClanTable::Clan(int32 Index) const
{
	return Clans.IsValidIndex(Index) ? &Clans[Index] : nullptr;
}

void FElysiumClanTable::ParentChain(const FString& TemplateName, TArray<FString>& Out) const
{
	TSet<FString> Seen;
	const FElysiumClanTemplate* Node = Find(TemplateName);
	while (Node != nullptr && !Seen.Contains(ElysiumFold(Node->TemplateName)))
	{
		Seen.Add(ElysiumFold(Node->TemplateName));
		Out.Add(Node->TemplateName);
		if (Node->ParentTemplateName.IsEmpty())
		{
			return;
		}
		Node = Find(Node->ParentTemplateName);
	}
}

FString FElysiumClanTable::PlayerBodyModel(int32 ClanIndex, bool bFemale, int32 ArmorSlot) const
{
	// Addressed by INDEX rather than by a `Player_<name>` string: the clandoc's own template order
	// is the engine's clan index, so the table already holds the mapping and nothing here has to
	// know how a clan is spelled.
	const FElysiumClanTemplate* Row = Clan(ClanIndex);
	if (Row == nullptr || !Row->IsPlayable() || ArmorSlot < 0 || ArmorSlot > 5)
	{
		return FString();
	}
	FElysiumClanTemplate Template;
	if (!Resolve(Row->TemplateName, Template))
	{
		return FString();
	}
	const FString Key = FString::Printf(TEXT("%s_Body%d"), bFemale ? TEXT("F") : TEXT("M"), ArmorSlot);
	FString Model = Template.GeneralStr(*Key);
	Model.ReplaceInline(TEXT("\\"), TEXT("/"));
	return Model;
}

FString FElysiumClanTable::PlayerBodyStem(int32 ClanIndex, bool bFemale, int32 ArmorSlot) const
{
	const FString Model = PlayerBodyModel(ClanIndex, bFemale, ArmorSlot);
	return Model.IsEmpty() ? FString() : FPaths::GetBaseFilename(Model).ToLower();
}

bool FElysiumClanTable::Resolve(const FString& TemplateName, FElysiumClanTemplate& Out) const
{
	TArray<FString> Chain;
	ParentChain(TemplateName, Chain);
	if (Chain.IsEmpty())
	{
		return false;
	}

	// Walk the chain from the ROOT down so a nearer template's key overwrites its parent's, then
	// restore the identity of the template that was actually asked for.
	auto Merge = [](const TMap<FString, int32>& From, TMap<FString, int32>& To)
	{
		for (const TPair<FString, int32>& Pair : From) { To.Add(Pair.Key, Pair.Value); }
	};

	Out = FElysiumClanTemplate();
	for (int32 i = Chain.Num() - 1; i >= 0; --i)
	{
		const FElysiumClanTemplate* Node = Find(Chain[i]);
		if (Node == nullptr)
		{
			continue;
		}
		for (const TPair<FString, FString>& Pair : Node->General) { Out.General.Add(Pair.Key, Pair.Value); }
		Merge(Node->Attributes, Out.Attributes);
		Merge(Node->Abilities, Out.Abilities);
		Merge(Node->Disciplines, Out.Disciplines);
		Merge(Node->Numina, Out.Numina);
		Merge(Node->Resistances, Out.Resistances);
		Merge(Node->Reactions, Out.Reactions);
		for (const TPair<FString, float>& Pair : Node->LoiterActivities)
		{
			Out.LoiterActivities.Add(Pair.Key, Pair.Value);
		}
		for (const TPair<FString, FString>& Pair : Node->TraitText)
		{
			Out.TraitText.Add(Pair.Key, Pair.Value);
		}
		if (!Node->Name.IsEmpty()) { Out.Name = Node->Name; }
		if (!Node->Description.IsEmpty()) { Out.Description = Node->Description; }
	}

	const FElysiumClanTemplate* Self = Find(Chain[0]);
	if (Self == nullptr)
	{
		return false;
	}
	Out.TemplateName = Self->TemplateName;
	Out.ParentTemplateName = Self->ParentTemplateName;
	Out.SourceFile = Self->SourceFile;
	Out.Index = Self->Index;
	return true;
}

// ================================================================================================
// 6. histories000.txt
// ================================================================================================

bool FElysiumHistoryTable::Load(FString& OutError)
{
	Rows.Reset();
	ByName.Reset();

	static const TCHAR* Rel = TEXT("system/histories000.txt");
	TSharedPtr<FKvNode> Root;
	if (!ReadVdata(Rel, Root, OutError))
	{
		return false;
	}
	const FKvNode* Tables = RootBlock(Root, TEXT("HistoryDataTables"), Rel, OutError);
	if (Tables == nullptr)
	{
		return false;
	}

	for (const TPair<FString, TSharedPtr<FKvNode>>& Kid : Tables->Kids)
	{
		if (Kid.Key != TEXT("historydata") || !Kid.Value.IsValid())
		{
			continue;
		}
		FElysiumHistory Row;
		Row.Index = Rows.Num();     // the index the save's m_iVHistoryID holds
		if (const FKvNode* Text = Kid.Value->Child(TEXT("Text")))
		{
			Row.Name = Text->Str(TEXT("Name"), FString());
			Row.Description = Text->Str(TEXT("Description"), FString());
			Row.ShortDescription = Text->Str(TEXT("ShortDescription"), FString());
			Row.ShortPenaltyDescription = Text->Str(TEXT("ShortPenaltyDescription"), FString());
		}
		if (const FKvNode* General = Kid.Value->Child(TEXT("General")))
		{
			Row.InternalName = General->Str(TEXT("InternalName"), FString());
			Row.CritterScope = General->Str(TEXT("CritterScope"), FString());
			Row.Effect = General->Str(TEXT("Effect"), FString());
		}
		Index(ByName, Row.InternalName, Row.Index);
		Rows.Add(MoveTemp(Row));
	}

	if (Rows.IsEmpty())
	{
		OutError = FString::Printf(TEXT("no histories in %s"), *FElysiumContentPaths::VdataFile(Rel));
		return false;
	}
	return true;
}

const FElysiumHistory* FElysiumHistoryTable::Find(const FString& InName) const
{
	const int32* Idx = ByName.Find(ElysiumFold(InName));
	return Idx ? &Rows[*Idx] : nullptr;
}

const FElysiumHistory* FElysiumHistoryTable::At(int32 Index) const
{
	return Rows.IsValidIndex(Index) ? &Rows[Index] : nullptr;
}

// ================================================================================================
// 8. experience_table.txt — pipe-delimited, not KeyValues
// ================================================================================================

bool FElysiumExperienceTable::ParseRow(const FString& Line, FElysiumExperienceEntry& Out)
{
	// `>` is the comment marker and a line under three characters is skipped — both are the
	// loader's own rules (`ExperienceTable::LoadFile`), not general whitespace handling.
	const FString Trimmed = Trim(Line);
	if (Trimmed.Len() < 3 || Trimmed.StartsWith(TEXT(">")))
	{
		return false;
	}

	TArray<FString> Fields;
	Trimmed.ParseIntoArray(Fields, TEXT("|"), false);
	if (Fields.Num() != 3)
	{
		return false;
	}

	Out.Key = Trim(Fields[0]);
	Out.Description = Trim(Fields[1]);
	// Stored RAW. The trailing `01` is not an encoding to strip here: the award path takes
	// floor(v/100) and carries the sub-100 remainder, and give-once is the player's own ledger.
	Out.Value = FCString::Atoi(*Trim(Fields[2]));
	return !Out.Key.IsEmpty();
}

bool FElysiumExperienceTable::Load(FString& OutError)
{
	Rows.Reset();
	ByKey.Reset();

	const FString Path = FElysiumContentPaths::VdataFile(TEXT("system/experience_table.txt"));
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *Path))
	{
		OutError = FString::Printf(TEXT("not found: %s"), *Path);
		return false;
	}

	for (const FString& Line : Lines)
	{
		FElysiumExperienceEntry Entry;
		if (ParseRow(Line, Entry))
		{
			ByKey.Add(ElysiumFold(Entry.Key), Rows.Num());
			Rows.Add(MoveTemp(Entry));
		}
	}

	if (Rows.IsEmpty())
	{
		OutError = FString::Printf(TEXT("no rows in %s"), *Path);
		return false;
	}
	return true;
}

const FElysiumExperienceEntry* FElysiumExperienceTable::Find(const FString& Key) const
{
	const int32* Idx = ByKey.Find(ElysiumFold(Key));
	return Idx ? &Rows[*Idx] : nullptr;
}

void FElysiumExperienceTable::FindPrefixCollisions(TArray<TPair<FString, FString>>& Out) const
{
	// The engine's lookup is a Q_strnicmp over the STORED key's length, so a stored key that
	// prefixes a longer incoming one would swallow it. An exact match reproduces the engine only
	// while this comes back empty.
	for (const FElysiumExperienceEntry& A : Rows)
	{
		for (const FElysiumExperienceEntry& B : Rows)
		{
			if (&A != &B && B.Key.Len() > A.Key.Len() && B.Key.StartsWith(A.Key, ESearchCase::IgnoreCase))
			{
				Out.Emplace(A.Key, B.Key);
			}
		}
	}
}

// ================================================================================================
// 9. levelingtemplate_000.txt
// ================================================================================================

int32 FElysiumLevelingTemplate::NumSteps() const
{
	int32 N = 0;
	for (const FElysiumLevelGroup& G : Groups) { N += G.Steps.Num(); }
	return N;
}

bool FElysiumLevelingTemplates::Load(FString& OutError)
{
	Templates.Reset();
	ByName.Reset();

	static const TCHAR* Rel = TEXT("system/levelingtemplate_000.txt");
	TSharedPtr<FKvNode> Root;
	if (!ReadVdata(Rel, Root, OutError))
	{
		return false;
	}
	const FKvNode* List = RootBlock(Root, TEXT("LevelingTemplateList"), Rel, OutError);
	if (List == nullptr)
	{
		return false;
	}

	// A `Level { "<Trait>" "<Value>" }` block carries exactly one key/value pair, and the key IS
	// the trait name — so the pair is read positionally rather than by name.
	auto ReadStep = [](const FKvNode& N, FElysiumLevelStep& Out)
	{
		if (N.Pairs.IsEmpty())
		{
			return false;
		}
		Out.Trait = N.Pairs[0].Key;
		Out.Value = FCString::Atoi(*N.Pairs[0].Value);
		return true;
	};

	for (const TPair<FString, TSharedPtr<FKvNode>>& Kid : List->Kids)
	{
		if (Kid.Key != TEXT("levelingtemplate") || !Kid.Value.IsValid())
		{
			continue;
		}
		const FKvNode& N = *Kid.Value;

		FElysiumLevelingTemplate Template;
		Template.Index = Templates.Num();
		Template.InternalName = N.Str(TEXT("InternalName"), FString());
		Template.Name = N.Str(TEXT("Name"), Template.InternalName);
		Template.ClanDependency = N.Str(TEXT("ClanDependency"), FString());
		N.ValuesFor(TEXT("Dependency"), Template.Dependencies);

		// Order is the whole semantics — the buyer walks the list top to bottom — so a bare `Level`
		// run at template scope becomes an ungated group in place rather than being hoisted.
		for (const TPair<FString, TSharedPtr<FKvNode>>& Child : N.Kids)
		{
			if (!Child.Value.IsValid())
			{
				continue;
			}
			if (Child.Key == TEXT("levelgroup"))
			{
				FElysiumLevelGroup Group;
				Group.bGated = true;
				Child.Value->ValuesFor(TEXT("Dependency"), Group.Dependencies);
				for (const TPair<FString, TSharedPtr<FKvNode>>& StepKid : Child.Value->Kids)
				{
					FElysiumLevelStep Step;
					if (StepKid.Key == TEXT("level") && StepKid.Value.IsValid() &&
						ReadStep(*StepKid.Value, Step))
					{
						Group.Steps.Add(MoveTemp(Step));
					}
				}
				Template.Groups.Add(MoveTemp(Group));
			}
			else if (Child.Key == TEXT("level"))
			{
				FElysiumLevelStep Step;
				if (!ReadStep(*Child.Value, Step))
				{
					continue;
				}
				if (Template.Groups.IsEmpty() || Template.Groups.Last().bGated)
				{
					Template.Groups.Add(FElysiumLevelGroup());
				}
				Template.Groups.Last().Steps.Add(MoveTemp(Step));
			}
		}

		Index(ByName, Template.InternalName, Template.Index);
		Templates.Add(MoveTemp(Template));
	}

	if (Templates.IsEmpty())
	{
		OutError = FString::Printf(TEXT("no leveling templates in %s"),
			*FElysiumContentPaths::VdataFile(Rel));
		return false;
	}
	return true;
}

const FElysiumLevelingTemplate* FElysiumLevelingTemplates::Find(const FString& InName) const
{
	const int32* Idx = ByName.Find(ElysiumFold(InName));
	return Idx ? &Templates[*Idx] : nullptr;
}

int32 FElysiumLevelingTemplates::NumSteps() const
{
	int32 N = 0;
	for (const FElysiumLevelingTemplate& T : Templates) { N += T.NumSteps(); }
	return N;
}

// ================================================================================================
// 14. strings.txt + strings_internal.txt
// ================================================================================================

namespace
{
	// One `StringData.Strings` block: every child is a group of sparse `Name<N>` leaves. Merges into
	// whatever the group already holds, because a group is authored more than once across (and
	// within) the two files.
	void LoadStringGroups(const FKvNode& Strings, TMap<FString, TArray<FString>>& Out)
	{
		for (const TPair<FString, TSharedPtr<FKvNode>>& Kid : Strings.Kids)
		{
			if (!Kid.Value.IsValid())
			{
				continue;
			}
			TArray<FString>& Names = Out.FindOrAdd(Kid.Key);
			for (const TPair<FString, FString>& Pair : Kid.Value->Pairs)
			{
				if (!Pair.Key.StartsWith(TEXT("name")))
				{
					continue;
				}
				const FString Tail = Pair.Key.Mid(4);
				if (Tail.IsEmpty() || !Tail.IsNumeric())
				{
					continue;   // a bare `Name`, which several groups carry as a title
				}
				const int32 Index = FCString::Atoi(*Tail);
				if (Index < 0 || Index > 4096)
				{
					continue;
				}
				// Grow to the authored index rather than appending: the index IS the stat value, so
				// a gap left by a commented-out entry must stay a gap.
				while (Names.Num() <= Index)
				{
					Names.Emplace();
				}
				Names[Index] = Pair.Value;
			}
		}
	}

	bool LoadStringFile(const TCHAR* Rel, TMap<FString, TArray<FString>>& Out, FString& OutError)
	{
		TSharedPtr<FKvNode> Root;
		if (!ReadVdata(Rel, Root, OutError))
		{
			return false;
		}
		const FKvNode* Data = RootBlock(Root, TEXT("StringData"), Rel, OutError);
		if (Data == nullptr)
		{
			return false;
		}
		// One `Strings` child in both files, but iterate rather than `Child` so a patch that splits
		// it does not silently drop half the groups.
		for (const TPair<FString, TSharedPtr<FKvNode>>& Kid : Data->Kids)
		{
			if (Kid.Key == TEXT("strings") && Kid.Value.IsValid())
			{
				LoadStringGroups(*Kid.Value, Out);
			}
		}
		return true;
	}
}

bool FElysiumStrings::Load(FString& OutError)
{
	Groups.Reset();

	if (!LoadStringFile(TEXT("system/strings.txt"), Groups, OutError))
	{
		return false;
	}
	// The internal file loads second so its groups extend the localized ones. An unreadable internal
	// file costs the `AttributeOrder`/`AbilityOrder` lookups, not the whole table, so it is recorded
	// and survived.
	FString InternalError;
	if (!LoadStringFile(TEXT("system/strings_internal.txt"), Groups, InternalError))
	{
		OutError = InternalError;
	}
	return !Groups.IsEmpty();
}

const TArray<FString>* FElysiumStrings::Group(const FString& Name) const
{
	return Groups.Find(ElysiumFold(Name));
}

FString FElysiumStrings::At(const FString& InGroup, int32 Index, const FString& Def) const
{
	const TArray<FString>* Names = Group(InGroup);
	return (Names && Names->IsValidIndex(Index) && !(*Names)[Index].IsEmpty()) ? (*Names)[Index] : Def;
}

int32 FElysiumStrings::IndexOf(const FString& InGroup, const FString& Value) const
{
	const TArray<FString>* Names = Group(InGroup);
	if (Names == nullptr || Value.IsEmpty())
	{
		return INDEX_NONE;
	}
	for (int32 i = 0; i < Names->Num(); ++i)
	{
		if ((*Names)[i].Equals(Value, ESearchCase::IgnoreCase))
		{
			return i;
		}
	}
	return INDEX_NONE;
}

int32 FElysiumStrings::NumEntries() const
{
	int32 N = 0;
	for (const TPair<FString, TArray<FString>>& Pair : Groups) { N += Pair.Value.Num(); }
	return N;
}
