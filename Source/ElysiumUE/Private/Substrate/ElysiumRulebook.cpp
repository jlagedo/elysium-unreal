#include "Substrate/ElysiumRulebook.h"

#include "ElysiumCameraSolve.h"
#include "ElysiumContentPaths.h"
#include "ElysiumKeyValues.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	using ElysiumKeyValues::FKvNode;

	// Every loader opens its file the same way and fails with the same readable reason.
	bool ReadVdata(const TCHAR* Rel, TSharedPtr<FKvNode>& OutRoot, FString& OutError)
	{
		const FString Path = FElysiumContentPaths::VdataFile(Rel);
		FString Raw;
		if (!FFileHelper::LoadFileToString(Raw, *Path))
		{
			OutError = FString::Printf(TEXT("not found: %s"), *Path);
			return false;
		}
		OutRoot = ElysiumKeyValues::ParseText(Raw);
		if (!OutRoot.IsValid())
		{
			OutError = FString::Printf(TEXT("empty or unparseable: %s"), *Path);
			return false;
		}
		return true;
	}

	// The root block a file's whole content hangs off. Named because a wrong root is the one
	// failure that produces a silently empty table rather than an error.
	const FKvNode* RootBlock(const TSharedPtr<FKvNode>& Root, const TCHAR* Key,
		const TCHAR* Rel, FString& OutError)
	{
		const FKvNode* Block = Root.IsValid() ? Root->Child(Key) : nullptr;
		if (Block == nullptr)
		{
			OutError = FString::Printf(TEXT("no %s block in %s"), Key,
				*FElysiumContentPaths::VdataFile(Rel));
		}
		return Block;
	}

	void Index(TMap<FString, int32>& Map, const FString& Key, int32 Value)
	{
		if (!Key.IsEmpty())
		{
			Map.Add(ElysiumFold(Key), Value);
		}
	}

	// Trim the mix of spaces and tabs the tables pad with.
	FString Trim(const FString& S)
	{
		FString Out = S;
		Out.TrimStartAndEndInline();
		return Out;
	}
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

			Index(ByName, Group.InternalName, Groups.Num());
			Groups.Add(MoveTemp(Group));
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
// 7. the five quests_*.txt
// ================================================================================================

// Fixed and alphabetical, because the save stores the table index: re-ordering this array would
// silently re-point every journal entry in every existing save.
const TCHAR* FElysiumQuestTables::HubNames[5] =
{
	TEXT("chinatown"), TEXT("downtown"), TEXT("hollywood"), TEXT("main"), TEXT("santamonica"),
};

const FElysiumQuestState* FElysiumQuest::StateById(int32 Id) const
{
	for (const FElysiumQuestState& State : States)
	{
		if (State.Id == Id)
		{
			return &State;
		}
	}
	return nullptr;
}

const FElysiumQuestState* FElysiumQuest::StateByOrdinal(int32 OneBased) const
{
	if (OneBased < 1 || OneBased > MaxStates || OneBased > States.Num())
	{
		return nullptr;
	}
	return &States[OneBased - 1];
}

bool FElysiumQuestTables::Load(FString& OutError)
{
	for (int32 i = 0; i < NumTables; ++i)
	{
		Quests[i].Reset();
	}
	ByTitle.Reset();

	int32 Loaded = 0;
	for (int32 t = 0; t < NumTables; ++t)
	{
		const FString Rel = FString::Printf(TEXT("system/quests_%s.txt"), HubNames[t]);
		TSharedPtr<FKvNode> Root;
		FString FileError;
		if (!ReadVdata(*Rel, Root, FileError))
		{
			OutError = FileError;
			continue;
		}
		const FKvNode* Table = Root.IsValid() ? Root->Child(TEXT("QuestTable")) : nullptr;
		if (Table == nullptr)
		{
			OutError = FString::Printf(TEXT("no QuestTable block in %s"),
				*FElysiumContentPaths::VdataFile(Rel));
			continue;
		}
		++Loaded;

		// `quests_main.txt` holds only its documentation comment — zero quests, and clean.
		for (const TPair<FString, TSharedPtr<FKvNode>>& Kid : Table->Kids)
		{
			if (Kid.Key != TEXT("quest") || !Kid.Value.IsValid())
			{
				continue;
			}
			FElysiumQuest Quest;
			Quest.TableIndex = t;
			Quest.Index = Quests[t].Num();
			// `QuestJournal::AddQuest` Q_trimspace's both, and the shipped files do author padding
			// around a Title — so the stored spelling is the trimmed one everywhere.
			Quest.Title = Kid.Value->Str(TEXT("Title"), FString()).TrimStartAndEnd();
			Quest.DisplayName = Kid.Value->Str(TEXT("DisplayName"), Quest.Title).TrimStartAndEnd();

			for (const TPair<FString, TSharedPtr<FKvNode>>& StateKid : Kid.Value->Kids)
			{
				if (StateKid.Key != TEXT("completionstate") || !StateKid.Value.IsValid())
				{
					continue;
				}
				const FKvNode& N = *StateKid.Value;
				FElysiumQuestState State;
				State.Id = N.Int(TEXT("ID"), 0);
				State.Description = N.Str(TEXT("Description"), FString());
				State.Type = N.Str(TEXT("Type"), FString());
				// A KEY into experience_table.txt, not a number — the shipped header comment is
				// wrong about this and every one of the 161 authored values is an id.
				State.AwardXp = N.Str(TEXT("AwardXP"), FString());
				State.AwardMoney = N.Int(TEXT("AwardMoney"), 0);
				State.Event = N.Str(TEXT("Event"), FString());
				Quest.States.Add(MoveTemp(State));
			}

			Quests[t].Add(MoveTemp(Quest));
		}
	}

	Reindex();

	if (Loaded == 0)
	{
		return false;
	}
	OutError.Reset();
	return true;
}

void FElysiumQuestTables::Reindex()
{
	ByTitle.Reset();
	for (int32 t = 0; t < NumTables; ++t)
	{
		for (int32 q = 0; q < Quests[t].Num(); ++q)
		{
			ByTitle.Add(ElysiumFold(Quests[t][q].Title.TrimStartAndEnd()), FElysiumQuestRef{ t, q });
		}
	}
}

const FElysiumQuest* FElysiumQuestTables::Find(const FString& Title, FElysiumQuestRef* OutRef) const
{
	const FElysiumQuestRef* Ref = ByTitle.Find(ElysiumFold(Title.TrimStartAndEnd()));
	if (Ref == nullptr)
	{
		return nullptr;
	}
	if (OutRef) { *OutRef = *Ref; }
	return At(*Ref);
}

const FElysiumQuest* FElysiumQuestTables::At(const FElysiumQuestRef& Ref) const
{
	if (Ref.Table < 0 || Ref.Table >= NumTables || !Quests[Ref.Table].IsValidIndex(Ref.Quest))
	{
		return nullptr;
	}
	return &Quests[Ref.Table][Ref.Quest];
}

int32 FElysiumQuestTables::NumQuests() const
{
	int32 N = 0;
	for (int32 i = 0; i < NumTables; ++i) { N += Quests[i].Num(); }
	return N;
}

int32 FElysiumQuestTables::NumStates() const
{
	int32 N = 0;
	for (int32 i = 0; i < NumTables; ++i)
	{
		for (const FElysiumQuest& Q : Quests[i]) { N += Q.States.Num(); }
	}
	return N;
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
// 12. dicerolls.txt
// ================================================================================================

FElysiumDiceTable::FElysiumDiceTable()
{
	for (int32 i = 0; i < NumEntries; ++i)
	{
		Faces[i] = i / (NumEntries / NumFaces);
	}
}

void FElysiumDiceTable::Load(const FKvNode& Node)
{
	Name = Node.Str(TEXT("Name"), FString());
	for (int32 i = 0; i < NumEntries; ++i)
	{
		Faces[i] = Node.Int(*FString::FromInt(i), 1);
	}
}

int32 FElysiumDiceTable::Face(int32 Draw) const
{
	return Faces[FMath::Clamp(Draw, 0, NumEntries - 1)];
}

bool FElysiumDiceTable::IsUniform() const
{
	for (int32 i = 0; i < NumEntries; ++i)
	{
		if (Faces[i] != i / (NumEntries / NumFaces))
		{
			return false;
		}
	}
	return true;
}

const FElysiumDiceTable& FElysiumDiceTable::Uniform()
{
	static const FElysiumDiceTable Table;
	return Table;
}

bool FElysiumDiceTables::Load(FString& OutError)
{
	Tables.Reset();
	HealthModifiers.Reset();
	ByName.Reset();

	static const TCHAR* Rel = TEXT("system/dicerolls.txt");
	TSharedPtr<FKvNode> Root;
	if (!ReadVdata(Rel, Root, OutError))
	{
		return false;
	}
	const FKvNode* Data = RootBlock(Root, TEXT("DiceRollData"), Rel, OutError);
	const FKvNode* Rules = Data ? Data->Child(TEXT("Rules")) : nullptr;
	if (Rules == nullptr)
	{
		if (OutError.IsEmpty())
		{
			OutError = FString::Printf(TEXT("no DiceRollData/Rules block in %s"),
				*FElysiumContentPaths::VdataFile(Rel));
		}
		return false;
	}

	// Health levels are probed until one is absent, the same rule the `Base%d` list takes: the
	// authored range is data (0..7 today) and a patch may extend it.
	if (const FKvNode* Health = Rules->Child(TEXT("HealthModifiers")))
	{
		for (int32 Level = 0; ; ++Level)
		{
			const FString* Value = Health->Value(*FString::FromInt(Level));
			if (Value == nullptr)
			{
				break;
			}
			HealthModifiers.Add(FCString::Atoi(**Value));
		}
	}

	if (const FKvNode* Weightings = Rules->Child(TEXT("TableWeightings")))
	{
		// Its own `Name` leaf is a value, not a child, so the child list is exactly the tables.
		for (const TPair<FString, TSharedPtr<FKvNode>>& Kid : Weightings->Kids)
		{
			if (!Kid.Value.IsValid())
			{
				continue;
			}
			FElysiumDiceTable Table;
			Table.InternalName = Kid.Key;
			Table.Load(*Kid.Value);
			Tables.Add(MoveTemp(Table));
		}
	}
	Reindex();

	if (Tables.IsEmpty())
	{
		OutError = FString::Printf(TEXT("no TableWeightings in %s"),
			*FElysiumContentPaths::VdataFile(Rel));
		return false;
	}
	return true;
}

void FElysiumDiceTables::Reindex()
{
	ByName.Reset();
	for (int32 i = 0; i < Tables.Num(); ++i)
	{
		Index(ByName, Tables[i].InternalName, i);
	}
}

const FElysiumDiceTable& FElysiumDiceTables::At(int32 Index) const
{
	return Tables.IsValidIndex(Index) ? Tables[Index] : FElysiumDiceTable::Uniform();
}

const FElysiumDiceTable& FElysiumDiceTables::Find(const FString& InName) const
{
	const int32* Idx = ByName.Find(ElysiumFold(InName));
	return At(Idx ? *Idx : 0);
}

const FElysiumDiceTable& FElysiumDiceTables::ForFeat(const FElysiumFeat& Feat, bool bNpc) const
{
	return Find(bNpc ? Feat.NpcWeighting : Feat.PcWeighting);
}

int32 FElysiumDiceTables::HealthModifier(int32 HealthLevel) const
{
	return HealthModifiers.IsValidIndex(HealthLevel) ? HealthModifiers[HealthLevel] : 0;
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

// ================================================================================================
// 15. vdata/items/*.txt — the item definitions
// ================================================================================================

namespace
{
	// In `EElysiumItemType` order, which is `system/items.txt`'s own `ItemTypes` order.
	const TCHAR* const GItemTypeNames[] = {
		TEXT("Weapon_Melee"), TEXT("Weapon_Firearm"), TEXT("Weapon_Thrown"), TEXT("Ammo"),
		TEXT("Armor"), TEXT("Money"), TEXT("Jewelry"), TEXT("Generic"), TEXT("Powerup"),
		TEXT("Bloodpack"), TEXT("Hidden"),
	};
	static_assert(UE_ARRAY_COUNT(GItemTypeNames) == (int32)EElysiumItemType::Count,
		"the item-type mirror must match system/items.txt's ItemTypes block");
}

const TCHAR* ElysiumItemTypeName(EElysiumItemType Type)
{
	const int32 Index = (int32)Type;
	return (Index >= 0 && Index < (int32)EElysiumItemType::Count) ? GItemTypeNames[Index] : TEXT("?");
}

bool ElysiumParseItemType(const FString& Raw, EElysiumItemType& OutType, bool& OutHidden)
{
	OutHidden = false;
	TArray<FString> Tokens;
	Raw.ParseIntoArrayWS(Tokens);

	bool bFound = false;
	for (const FString& Token : Tokens)
	{
		bool bMatched = false;
		for (int32 i = 0; i < (int32)EElysiumItemType::Count; ++i)
		{
			if (!Token.Equals(GItemTypeNames[i], ESearchCase::IgnoreCase))
			{
				continue;
			}
			bMatched = true;
			if (!bFound)
			{
				OutType = (EElysiumItemType)i;
				bFound = true;
			}
			else if ((EElysiumItemType)i == EElysiumItemType::Hidden)
			{
				// A trailing `hidden` beside a real type is the file's own visibility flag —
				// `"weapon_firearm hidden"`, and `"hidden hidden"` for an item that is both.
				OutHidden = true;
			}
			break;
		}
		if (!bMatched)
		{
			// An unrecognised token is not an error: the value is authored free-form and the
			// caller keeps whatever type it already resolved.
			continue;
		}
	}
	return bFound;
}

bool FElysiumItemDef::IsWeaponType() const
{
	// `system/items.txt`'s `IsWeapon` column: the three wielded weapon families, Bloodpack (which
	// the file marks a weapon because feeding is an attack), and Hidden (the intrinsic attacks).
	switch (Type)
	{
	case EElysiumItemType::WeaponMelee:
	case EElysiumItemType::WeaponFirearm:
	case EElysiumItemType::WeaponThrown:
	case EElysiumItemType::Bloodpack:
	case EElysiumItemType::Hidden:
		return true;
	default:
		return false;
	}
}

const TCHAR* ElysiumWeaponModeTypeName(EElysiumWeaponModeType Type)
{
	switch (Type)
	{
	case EElysiumWeaponModeType::Attack:            return TEXT("Attack");
	case EElysiumWeaponModeType::SecondaryAttack:   return TEXT("Secondary_Attack");
	case EElysiumWeaponModeType::TogglePrimaryMode: return TEXT("Toggle_Primary_Mode");
	case EElysiumWeaponModeType::ZoomLoop:          return TEXT("Zoom_Out_Loop");
	case EElysiumWeaponModeType::Other:             return TEXT("Other");
	default:                                        return TEXT("None");
	}
}

const FElysiumWeaponMode* FElysiumItemDef::FindMode(const TCHAR* Tag) const
{
	for (const FElysiumWeaponMode& Mode : Modes)
	{
		if (Mode.Tag.Equals(Tag, ESearchCase::IgnoreCase))
		{
			return &Mode;
		}
	}
	return nullptr;
}

namespace
{
	EElysiumWeaponModeType ParseWeaponModeType(const FString& Raw)
	{
		if (Raw.IsEmpty())                                              { return EElysiumWeaponModeType::None; }
		if (Raw.Equals(TEXT("Attack"), ESearchCase::IgnoreCase))         { return EElysiumWeaponModeType::Attack; }
		if (Raw.Equals(TEXT("Secondary_Attack"), ESearchCase::IgnoreCase)) { return EElysiumWeaponModeType::SecondaryAttack; }
		if (Raw.Equals(TEXT("Toggle_Primary_Mode"), ESearchCase::IgnoreCase)) { return EElysiumWeaponModeType::TogglePrimaryMode; }
		if (Raw.Equals(TEXT("Zoom_Out_Loop"), ESearchCase::IgnoreCase))  { return EElysiumWeaponModeType::ZoomLoop; }
		// A consumable/throw-style mode names its own projectile record (`FragGrenade`,
		// `CrossbowBolt`). It is authored data, not a parse failure.
		return EElysiumWeaponModeType::Other;
	}

	void ParseWeaponMode(const ElysiumKeyValues::FKvNode& Block, FElysiumWeaponMode& Out)
	{
		Out.Tag = Block.Str(TEXT("Tag"), FString());
		Out.TypeName = Block.Str(TEXT("Type"), FString());
		Out.Type = ParseWeaponModeType(Out.TypeName);

		Out.Dmg = Block.Str(TEXT("Dmg"), FString());
		Out.BaseLethality = Block.Int(TEXT("BaseLethality"), 0);
		Out.SkillRequirement = Block.Int(TEXT("SkillRequirement"), 0);

		Out.AttackRate = Block.Flt(TEXT("Attack_Rate"), 0.0f);

		Out.AmmoType = Block.Str(TEXT("Ammo_Type"), FString());
		Out.AmmoCost = Block.Int(TEXT("Ammo_Cost"), 0);
		// An unauthored `Ammo_Fired` is one ray, not zero: every attack mode that omits it fires a
		// single trace, and the M37's eight is why the key exists at all.
		Out.AmmoFired = Block.Int(TEXT("Ammo_Fired"), 1);

		Out.bAllowAutofire = Block.Bool(TEXT("allow_autofire"), false);

		Out.BurstMin = Block.Int(TEXT("BurstMin"), 0);
		Out.BurstMax = Block.Int(TEXT("BurstMax"), 0);
		// The loader's own clamp — retail forces `BurstMin <= BurstMax`.
		Out.BurstMax = FMath::Max(Out.BurstMin, Out.BurstMax);

		Out.Range = Block.Flt(TEXT("Range"), 0.0f);
		Out.BotchTable = Block.Str(TEXT("Botch_Table"), FString());
	}
}

bool FElysiumItemTable::ParseText(const FString& Classname, const FString& Text,
	FElysiumItemDef& Out, FString& OutError)
{
	TSharedPtr<FKvNode> Root = ElysiumKeyValues::ParseText(Text);
	const FKvNode* Data = Root.IsValid() ? Root->Child(TEXT("WeaponData")) : nullptr;
	if (Data == nullptr)
	{
		OutError = FString::Printf(TEXT("no WeaponData block in %s"), *Classname);
		return false;
	}

	Out = FElysiumItemDef();
	Out.Classname = Classname;
	Out.PrintName = Data->Str(TEXT("printname"), FString());
	Out.Description = Data->Str(TEXT("description"), FString());

	ElysiumParseItemType(Data->Str(TEXT("item_type"), FString()), Out.Type, Out.bHidden);

	Out.bStackable = Data->Bool(TEXT("is_stackable"), false);
	// `stack_limit` only. One patch file authors `stacklimit` without the underscore, which the
	// engine's own `GetInt("stack_limit")` never reads either — a defect reproduced, not repaired.
	Out.StackLimit = Data->Int(TEXT("stack_limit"), 0);
	Out.bDroppable = Data->Bool(TEXT("is_droppable"), true);
	Out.bPermanentInventory = Data->Bool(TEXT("permanent_inventory"), false);
	Out.bWieldable = Data->Bool(TEXT("is_wieldable"), false);
	Out.bVisibleInHud = Data->Bool(TEXT("is_visible_in_hud"), true);

	Out.Worth = Data->Int(TEXT("item_worth"), 0);
	Out.PlayerSell = Data->Int(TEXT("player_sell"), 0);
	Out.Weight = Data->Int(TEXT("weight"), 0);
	Out.ItemFlags = Data->Int(TEXT("item_flags"), 0);

	Out.PlayerModel = Data->Str(TEXT("playermodel"), FString());
	Out.ViewModel = Data->Str(TEXT("viewmodel"), FString());
	Out.InfoModel = Data->Str(TEXT("infomodel"), FString());

	Out.CameraClass = ElysiumCam::ParseCameraClass(Data->Str(TEXT("camera_class"), FString()));

	Out.bReloadSingle = Data->Bool(TEXT("reload_single"), false);
	Out.bDisallowFirearmsToBashing = Data->Bool(TEXT("Disallow_FirearmsToBashing"), false);

	// The `Activation` blocks, in file order — a record may author several and the order is what
	// `PrimaryMode2`'s toggle cycles through.
	for (const TPair<FString, TSharedPtr<FKvNode>>& Kid : Data->Kids)
	{
		if (Kid.Key != TEXT("activation") || !Kid.Value.IsValid())
		{
			continue;
		}
		FElysiumWeaponMode Mode;
		ParseWeaponMode(*Kid.Value, Mode);
		Out.Modes.Add(MoveTemp(Mode));
	}

	if (const FKvNode* Magazine = Data->Child(TEXT("Magazine")))
	{
		Out.AmmoType = Magazine->Str(TEXT("Type"), FString());
		Out.MagazineSize = Magazine->Int(TEXT("Size"), 0);
		// An unauthored `Default_Size` falls back to the magazine's capacity: the file states the
		// two separately only where they differ.
		Out.DefaultAmmo = Magazine->Int(TEXT("Default_Size"), Out.MagazineSize);
		Out.DroppedAmmo = Magazine->Int(TEXT("Dropped_Ammo"), 0);
		Out.ReloadTime = Magazine->Flt(TEXT("ReloadTime"), 0.0f);
	}

	OutError.Reset();
	return true;
}

bool FElysiumItemTable::Load(FString& OutError)
{
	Items.Reset();
	ByName.Reset();

	const FString Dir = FElysiumContentPaths::VdataDir() / TEXT("items");
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(Dir / TEXT("*.txt")), true, false);
	Files.Sort();

	for (const FString& Leaf : Files)
	{
		FString Raw;
		if (!FFileHelper::LoadFileToString(Raw, *(Dir / Leaf)))
		{
			OutError = FString::Printf(TEXT("unreadable: %s"), *(Dir / Leaf));
			continue;   // one unreadable file must not cost the other 243
		}
		FElysiumItemDef Def;
		FString FileError;
		if (!ParseText(FPaths::GetBaseFilename(Leaf), Raw, Def, FileError))
		{
			OutError = FileError;
			continue;
		}
		Items.Add(MoveTemp(Def));
	}

	if (Items.IsEmpty())
	{
		OutError = FString::Printf(TEXT("no item definitions in %s"), *Dir);
		return false;
	}
	Reindex();
	OutError.Reset();
	return true;
}

void FElysiumItemTable::Reindex()
{
	ByName.Reset();
	for (int32 i = 0; i < Items.Num(); ++i)
	{
		Index(ByName, Items[i].Classname, i);
	}
}

const FElysiumItemDef* FElysiumItemTable::Find(const FString& Classname) const
{
	const int32* Idx = ByName.Find(ElysiumFold(Classname));
	return Idx ? &Items[*Idx] : nullptr;
}

const FElysiumItemDef* FElysiumItemTable::At(int32 InIndex) const
{
	return Items.IsValidIndex(InIndex) ? &Items[InIndex] : nullptr;
}

int32 FElysiumItemTable::CountOfType(EElysiumItemType Type) const
{
	int32 N = 0;
	for (const FElysiumItemDef& Def : Items)
	{
		if (Def.Type == Type) { ++N; }
	}
	return N;
}

// ================================================================================================
// 16. system/sound_volume_table.txt
// ================================================================================================

bool FElysiumSoundVolumeTable::Load(FString& OutError)
{
	Levels.Reset();
	Categories.Reset();
	Misc.Reset();
	LevelByIndex.Reset();
	CategoryByName.Reset();

	static const TCHAR* Rel = TEXT("system/sound_volume_table.txt");
	TSharedPtr<FKvNode> Root;
	if (!ReadVdata(Rel, Root, OutError))
	{
		return false;
	}
	const FKvNode* Data = RootBlock(Root, TEXT("SoundVolumeTable"), Rel, OutError);
	if (Data == nullptr)
	{
		return false;
	}

	const FKvNode* Volumes = Data->Child(TEXT("VolumeLevels"));
	if (Volumes == nullptr)
	{
		OutError = FString::Printf(TEXT("no SoundVolumeTable/VolumeLevels block in %s"),
			*FElysiumContentPaths::VdataFile(Rel));
		return false;
	}
	// The two level blocks are joined BY INDEX rather than by position: `OccludedVolumeLevels`
	// authors the same keys, so a patch that omitted one row would otherwise shift the whole join.
	const FKvNode* Occluded = Data->Child(TEXT("OccludedVolumeLevels"));
	for (const TPair<FString, FString>& Pair : Volumes->Pairs)
	{
		if (!Pair.Key.IsNumeric())
		{
			continue;   // the block holds level indices only; anything else is not a level
		}
		FElysiumSoundLevel Row;
		Row.Level = FCString::Atoi(*Pair.Key);
		Row.RadiusUnits = FCString::Atof(*Pair.Value);
		// An unauthored occlusion row reads as "cannot be occluded", which is the block's own `0`.
		Row.bOccludable = Occluded != nullptr && Occluded->Int(*Pair.Key, 0) != 0;
		Levels.Add(MoveTemp(Row));
	}

	if (const FKvNode* Types = Data->Child(TEXT("SoundTypes")))
	{
		for (const TPair<FString, FString>& Pair : Types->Pairs)
		{
			FElysiumSoundCategory Row;
			Row.Name = Pair.Key;   // already folded by the KV reader
			Row.Level = FCString::Atoi(*Pair.Value);
			Categories.Add(MoveTemp(Row));
		}
	}
	if (const FKvNode* MiscNode = Data->Child(TEXT("MiscData")))
	{
		for (const TPair<FString, FString>& Pair : MiscNode->Pairs)
		{
			Misc.Add(Pair.Key, Pair.Value);
		}
	}
	Reindex();

	if (Categories.IsEmpty())
	{
		OutError = FString::Printf(TEXT("no SoundVolumeTable/SoundTypes rows in %s"),
			*FElysiumContentPaths::VdataFile(Rel));
		return false;
	}
	return true;
}

void FElysiumSoundVolumeTable::Reindex()
{
	LevelByIndex.Reset();
	CategoryByName.Reset();
	for (int32 i = 0; i < Levels.Num(); ++i)
	{
		LevelByIndex.Add(Levels[i].Level, i);
	}
	for (int32 i = 0; i < Categories.Num(); ++i)
	{
		Index(CategoryByName, Categories[i].Name, i);
	}
}

const FElysiumSoundLevel* FElysiumSoundVolumeTable::Level(int32 InIndex) const
{
	const int32* Row = LevelByIndex.Find(InIndex);
	return Row ? &Levels[*Row] : nullptr;
}

const FElysiumSoundLevel* FElysiumSoundVolumeTable::FindCategory(const FString& Category) const
{
	const int32* Row = CategoryByName.Find(ElysiumFold(Category));
	return Row ? Level(Categories[*Row].Level) : nullptr;
}

float FElysiumSoundVolumeTable::MiscFloat(const TCHAR* Tag, float Def) const
{
	const FString* Raw = Misc.Find(ElysiumFold(FString(Tag)));
	return Raw ? FCString::Atof(**Raw) : Def;
}

const FElysiumSoundLevel& FElysiumSoundVolumeTable::NormalFallback()
{
	// LEVEL_2 verbatim: 240 game units, occludable.
	static const FElysiumSoundLevel Row{ NormalLevel, 240.f, true };
	return Row;
}
