#include "Substrate/ElysiumChargen.h"

#include "ElysiumRng.h"
#include "ElysiumSheetSlots.h"

// --- The pools ---

namespace
{
	// Slot ranges per category, inclusive. `Attributes` runs Strength(1) .. Wits(9) in three groups
	// of three; `Abilities` runs Brawl(1) .. Academics(12) in three groups of four. Slot 0 of each
	// is the container's `*_Order` stat, and everything past the last group is the derived block.
	struct FCategoryRange
	{
		EElysiumTraitContainer Container;
		int32 First;
		int32 Last;
	};

	const FCategoryRange GCategories[(uint8)EElysiumChargenPool::Count] =
	{
		{ EElysiumTraitContainer::Attributes,   1,  3 },   // Physical
		{ EElysiumTraitContainer::Attributes,   4,  6 },   // Social
		{ EElysiumTraitContainer::Attributes,   7,  9 },   // Mental
		{ EElysiumTraitContainer::Abilities,    1,  4 },   // Talents
		{ EElysiumTraitContainer::Abilities,    5,  8 },   // Skills
		{ EElysiumTraitContainer::Abilities,    9, 12 },   // Knowledges
		{ EElysiumTraitContainer::Disciplines,  0, 12 },   // Disciplines — the whole container
	};

	// One `Subpool_*` row. A miss reads 0, which is what every shipped attribute/ability subpool
	// holds anyway — the tables are still consulted rather than assumed, because they are the knob
	// the file's own comment says clan balancing would turn.
	int32 SubpoolRow(const FElysiumRules* Rules, const TCHAR* Table, int32 Key)
	{
		if (Rules == nullptr)
		{
			return 0;
		}
		const FElysiumRuleTable* T = Rules->Table(FString(Table));
		return T ? (int32)T->Lookup(Key, 0.f) : 0;
	}

	// A table nested on a stat (`Attrib_Order`'s tier pools and order lookups all live there).
	const FElysiumRuleTable* StatTable(const FElysiumStatTable* Stats, EElysiumTraitContainer Container,
		int32 Slot, const TCHAR* TableName)
	{
		const FElysiumStat* Stat = Stats ? Stats->Container(Container).At(Slot) : nullptr;
		return Stat ? Stat->Tables.Find(ElysiumFold(TableName)) : nullptr;
	}

	// What one dot costs a chargen pool. **A chargen point buys a dot outright**: the pools are
	// counted in dots, not in the experience currency the `Costs` blocks price.
	//
	// The shipped numbers settle it. A clan gets 2/1/0 attribute points and 3/2/1 ability points,
	// while `stats.txt` prices an attribute raise at `Current_Rating * 4` and a discipline raise at
	// `Current_Rating * 5` — so a 2-point Physical pool could not buy even its first dot (the 1 -> 2
	// step costs 4) and the single discipline point could never buy anything at all. The `Costs`
	// blocks are what `AwardExperience` spends against on the level-up path; chargen spends dots.
	constexpr int32 GDotCost = 1;

	// The experience price of the step out of `Base`, for the level-up currency. `New` prices only
	// the 0 -> 1 step and **never** an Attribute — the engine's buy path passes the container kind,
	// and kind 0 takes `Raise` at every rating, which is why the attribute `New "1"` is annotated in
	// the data as a sell-back hack. `FElysiumStatCost::CannotBuy` is the refusal.
	//
	// An effect-supplied `Costs` override (Brujah's Animalism table) is not consulted: the effect
	// layer counts those rows rather than storing them.
	int32 ExperiencePrice(const FElysiumStatTable* Stats, EElysiumTraitContainer Container,
		int32 Slot, int32 Base)
	{
		if (Stats == nullptr)
		{
			return FElysiumStatCost::CannotBuy;
		}
		const FElysiumStatContainer& Table = Stats->Container(Container);
		const FElysiumStat* Stat = Table.At(Slot);
		if (Stat == nullptr)
		{
			return FElysiumStatCost::CannotBuy;
		}
		const FElysiumStatCosts& Costs = Table.CostsFor(*Stat);
		if (!Costs.bPresent)
		{
			return FElysiumStatCost::CannotBuy;
		}
		const bool bNew = Base <= 0 && Container != EElysiumTraitContainer::Attributes;
		const FElysiumStatCost& Cost = bNew ? Costs.New : Costs.Raise;
		return Cost.IsSet() ? Cost.At(Base) : FElysiumStatCost::CannotBuy;
	}

	// What one step costs in the state's own currency, or `CannotBuy`.
	int32 PriceOf(const FElysiumChargenState& State, const FElysiumChargenRules& Rules,
		EElysiumTraitContainer Container, int32 Slot, int32 Base)
	{
		return State.Currency == EElysiumChargenCurrency::Pools
			? GDotCost
			: ExperiencePrice(Rules.Stats, Container, Slot, Base);
	}

	// One `LevelGroup` `Dependency` — `"Attrib_Order == Physical_Mental_Social"`. The same shape as
	// a stat's `IncPredependency` except that the right-hand side may be a SYMBOLIC name out of the
	// left-hand trait's `NameMapping` group, which is how the CharGen templates select their branch.
	// Resolve that, then hand the numeric form to the shared evaluator.
	bool EvalLevelDependency(const FString& Expr, const FElysiumSheet& Sheet,
		const FElysiumChargenRules& Rules)
	{
		static const TCHAR* const Ops[] = { TEXT("<="), TEXT(">="), TEXT("=="), TEXT("!="),
			TEXT("<"), TEXT(">"), TEXT("=") };

		for (const TCHAR* Op : Ops)
		{
			const int32 At = Expr.Find(Op);
			if (At == INDEX_NONE)
			{
				continue;
			}
			const FString Lhs = Expr.Left(At).TrimStartAndEnd();
			const FString Rhs = Expr.Mid(At + FCString::Strlen(Op)).TrimStartAndEnd();
			if (Rhs.IsEmpty() || Rhs.IsNumeric() || Rules.Stats == nullptr || Rules.Strings == nullptr)
			{
				break;   // nothing symbolic to resolve — the shared evaluator handles it as authored
			}
			EElysiumTraitContainer Container = EElysiumTraitContainer::Attributes;
			const FElysiumStat* Stat = Rules.Stats->Find(Lhs, &Container);
			if (Stat == nullptr || Stat->NameMapping.IsEmpty())
			{
				break;
			}
			const int32 Index = Rules.Strings->IndexOf(Stat->NameMapping, Rhs);
			if (Index == INDEX_NONE)
			{
				// A name the strings table does not own cannot be satisfied by any value, and a gate
				// we cannot read must not silently pass: this branch is simply not the one selected.
				return false;
			}
			return ElysiumSheetRules::EvalPredependency(
				FString::Printf(TEXT("%s %s %d"), *Lhs, Op, Index), Sheet);
		}
		return ElysiumSheetRules::EvalPredependency(Expr, Sheet);
	}

	// VtMB's `giftxp 9000` + `vautolvl <clan>_CharGen`. Every group whose dependencies hold runs, in
	// file order, and each step buys the trait up to its authored value one dot at a time through
	// `IncBase` — so the stat's own ceiling and its `IncPredependency` gate the baseline exactly as
	// they gate a purchase. The XP grant is effectively unlimited, so no step is ever refused for
	// price.
	void RunChargenTemplate(FElysiumChargenState& State, const FElysiumChargenRules& Rules,
		const FElysiumClanTemplate& Template)
	{
		if (Rules.Leveling == nullptr)
		{
			return;
		}
		// The template is named by the clan, in the Attributes block rather than General — and it is
		// not a `stats.txt` slot, so it survives only as authored text.
		FString Name = Template.TraitStr(TEXT("CharGen_AutoLevel_Template"));
		if (Name.IsEmpty())
		{
			Name = Template.GeneralStr(TEXT("CharGen_AutoLevel_Template"));
		}
		const FElysiumLevelingTemplate* Level = Rules.Leveling->Find(Name);
		if (Level == nullptr)
		{
			return;
		}

		for (const FElysiumLevelGroup& Group : Level->Groups)
		{
			bool bRun = true;
			for (const FString& Dep : Group.Dependencies)
			{
				if (!EvalLevelDependency(Dep, State.Sheet, Rules))
				{
					bRun = false;
					break;
				}
			}
			if (!bRun)
			{
				continue;
			}
			for (const FElysiumLevelStep& Step : Group.Steps)
			{
				EElysiumTraitContainer Container = EElysiumTraitContainer::Attributes;
				int32 Slot = INDEX_NONE;
				if (!ElysiumFindSheetSlot(*Step.Trait, Container, Slot))
				{
					continue;
				}
				// A step is "raise TO this value", so it is a no-op when the trait already stands
				// there — which is what makes two overlapping groups idempotent rather than additive.
				while (State.Sheet.GetBase(Container, Slot) < Step.Value)
				{
					if (!State.Sheet.IncBase(Container, Slot, Rules.Stats, &State.Effects))
					{
						break;   // the ceiling or a gate refused; the template does not force past it
					}
				}
			}
		}
	}
}

int32 ElysiumChargenPoolGroupIndex(EElysiumChargenPool Pool)
{
	switch (Pool)
	{
	case EElysiumChargenPool::Physical:
	case EElysiumChargenPool::Talents:      return 0;
	case EElysiumChargenPool::Social:
	case EElysiumChargenPool::Skills:       return 1;
	case EElysiumChargenPool::Mental:
	case EElysiumChargenPool::Knowledges:   return 2;
	default:                                return INDEX_NONE;
	}
}

EElysiumTraitContainer ElysiumChargenPoolContainer(EElysiumChargenPool Pool)
{
	return (uint8)Pool < (uint8)EElysiumChargenPool::Count
		? GCategories[(uint8)Pool].Container
		: EElysiumTraitContainer::Attributes;
}

int32 FElysiumChargenPools::Total() const
{
	int32 N = 0;
	for (int32 i = 0; i < (int32)EElysiumChargenPool::Count; ++i) { N += Points[i]; }
	return N;
}

void FElysiumChargenPools::Reset()
{
	for (int32 i = 0; i < (int32)EElysiumChargenPool::Count; ++i) { Points[i] = 0; }
}

// --- The rules ---

namespace ElysiumChargen
{

EElysiumChargenPool PoolFor(EElysiumTraitContainer Container, int32 Slot)
{
	for (int32 i = 0; i < (int32)EElysiumChargenPool::Count; ++i)
	{
		const FCategoryRange& R = GCategories[i];
		if (R.Container == Container && Slot >= R.First && Slot <= R.Last)
		{
			return (EElysiumChargenPool)i;
		}
	}
	return EElysiumChargenPool::None;
}

bool ResolveClanTemplate(const FElysiumChargenRules& Rules, int32 Clan, FElysiumClanTemplate& Out)
{
	if (Rules.Clans == nullptr || !FElysiumSheet::IsValidClan(Clan))
	{
		return false;
	}
	return Rules.Clans->Resolve(
		FString::Printf(TEXT("Player_%s"), FElysiumSheet::ClanName(Clan)), Out);
}

int32 ResolveOrder(const FElysiumChargenRules& Rules, const FElysiumClanTemplate& Template,
	EElysiumTraitContainer Container, int32 Slot)
{
	const FElysiumStat* Stat = Rules.Stats ? Rules.Stats->Container(Container).At(Slot) : nullptr;
	if (Stat == nullptr)
	{
		return 0;
	}
	// The template authors the order as a NAME. `FElysiumClanTemplate::Trait` would answer 0 for it,
	// which is a different ordering entirely, so the authored text is what is read.
	const FString Authored = Template.TraitStr(Stat->InternalName);
	if (!Authored.IsEmpty() && Rules.Strings)
	{
		const int32 Index = Rules.Strings->IndexOf(Stat->NameMapping, Authored);
		if (Index != INDEX_NONE)
		{
			return Index;
		}
	}
	// An order authored as a plain number still reads; anything unresolvable takes the stat's own
	// default, which is the engine's fallback for an unmatched NameMapping.
	if (const int32* Value = Template.Trait(Stat->InternalName))
	{
		if (Authored.IsEmpty())
		{
			return *Value;
		}
	}
	return Stat->Default;
}

FElysiumChargenPools BuildPools(const FElysiumChargenRules& Rules, int32 Clan,
	int32 AttribOrder, int32 AbilityOrder, bool bKindred)
{
	FElysiumChargenPools Out;

	// The clan term. Indexed by the clandoc template index, which for the seven playable clans is
	// the same 2..8 the sheet's `Clan` slot holds.
	static const TCHAR* const ClanTables[(uint8)EElysiumChargenPool::Count] =
	{
		TEXT("Subpool_Physical"),   TEXT("Subpool_Social"),      TEXT("Subpool_Mental"),
		TEXT("Subpool_Talents"),    TEXT("Subpool_Skills"),      TEXT("Subpool_Knowledges"),
		TEXT("Subpool_Disciplines"),
	};
	for (int32 i = 0; i < (int32)EElysiumChargenPool::Count; ++i)
	{
		Out.Points[i] = SubpoolRow(Rules.Rules, ClanTables[i], Clan);
	}

	// The tier term. `<Order>_Lookups` maps the order value's three consecutive rows to category
	// indices, and the tier table prices the tier those rows sit at.
	struct FTier
	{
		int32 Slot;
		const TCHAR* Lookups;
		const TCHAR* Pool;
		const TCHAR* KinePool;
		int32 Order;
		int32 FirstCategory;   // the pool enum index category 0 maps to
	};
	const FTier Tiers[2] =
	{
		{ ElysiumSlot::AttribOrder, TEXT("Attribute_Order_Lookups"),
			TEXT("Subpool_Attribute_Primary_Secondary_Tertiary"),
			TEXT("Subpool_Attribute_Primary_Secondary_Tertiary_Kine"),
			AttribOrder,  (int32)EElysiumChargenPool::Physical },
		{ 0,                        TEXT("Ability_Order_Lookups"),
			TEXT("Subpool_Ability_Primary_Secondary_Tertiary"),
			TEXT("Subpool_Ability_Primary_Secondary_Tertiary_Kine"),
			AbilityOrder, (int32)EElysiumChargenPool::Talents },
	};
	const EElysiumTraitContainer TierContainers[2] =
	{
		EElysiumTraitContainer::Attributes, EElysiumTraitContainer::Abilities,
	};

	for (int32 t = 0; t < 2; ++t)
	{
		const FTier& Tier = Tiers[t];
		const FElysiumRuleTable* Lookups =
			StatTable(Rules.Stats, TierContainers[t], Tier.Slot, Tier.Lookups);
		const FElysiumRuleTable* Pool =
			StatTable(Rules.Stats, TierContainers[t], Tier.Slot, bKindred ? Tier.Pool : Tier.KinePool);
		if (Lookups == nullptr || Pool == nullptr)
		{
			continue;
		}
		// Three rows per ordering, in tier order: primary, secondary, tertiary.
		for (int32 Rank = 0; Rank < 3; ++Rank)
		{
			const int32 Category = (int32)Lookups->Lookup(Tier.Order * 3 + Rank, 0.f);
			if (Category < 0 || Category > 2)
			{
				continue;
			}
			Out.Points[Tier.FirstCategory + Category] += (int32)Pool->Lookup(Rank, 0.f);
		}
	}

	return Out;
}

void ApplyBaseline(FElysiumChargenState& State, const FElysiumChargenRules& Rules)
{
	State.Spent.Reset();
	State.Pools.Reset();
	State.Effects.Reset();

	if (Rules.Stats == nullptr)
	{
		State.Baseline = State.Sheet;
		return;
	}

	// 1. Every slot at its `stats.txt` default — including the -1 that hides a discipline.
	State.Sheet.SeedFrom(*Rules.Stats);
	State.Sheet.SetClan(State.Clan);
	State.Sheet.SetMale(State.bMale);

	FElysiumClanTemplate Template;
	const bool bHasClan = ResolveClanTemplate(Rules, State.Clan, Template);

	// 2. The effect layer, before the template lands: a clan bane bounds what the template writes.
	if (Rules.TraitEffects)
	{
		TArray<FString> GroupNames;
		if (bHasClan)
		{
			const FString ClanEffect = Template.GeneralStr(TEXT("ClanEffect"));
			if (!ClanEffect.IsEmpty()) { GroupNames.Add(ClanEffect); }
		}
		if (const FElysiumHistory* History =
			Rules.Histories ? Rules.Histories->At(State.HistoryId) : nullptr)
		{
			if (!History->Effect.IsEmpty()) { GroupNames.Add(History->Effect); }
		}
		State.Effects.Build(
			*Rules.TraitEffects, GroupNames, Rules.Feats, Rules.Stats, Rules.Strings,
			Rules.ExcludedEquip);
	}

	if (!bHasClan)
	{
		State.Sheet.RecomputeCurrent(Rules.Stats, &State.Effects);
		State.Baseline = State.Sheet;
		return;
	}

	// 3. The clan's authored ratings.
	State.Sheet.ApplyTemplate(Template, Rules.Stats, &State.Effects, Rules.ExcludedEquip);

	// 4. The two trait orders, which the template authors as NAMES and step 3 therefore wrote as 0.
	const int32 AttribOrder = ResolveOrder(Rules, Template,
		EElysiumTraitContainer::Attributes, ElysiumSlot::AttribOrder);
	const int32 AbilityOrder = ResolveOrder(Rules, Template, EElysiumTraitContainer::Abilities, 0);
	State.Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::AttribOrder, AttribOrder);
	State.Sheet.SetBase(EElysiumTraitContainer::Abilities, 0, AbilityOrder);
	// SetBase keeps BASE and CURRENT locally consistent, but the two symbolic order effects have to
	// replace those new base values before the auto-level dependency selects its one matching branch.
	State.Sheet.RecomputeCurrent(Rules.Stats, &State.Effects);

	// 5. The baseline dots, BOUGHT: VtMB grants 9000 XP and runs `<Clan>_CharGen`, so the starting
	//    sheet is the template's own spend order rather than a written block. The XP grant is
	//    effectively unlimited, so the run is the template's steps applied in order.
	RunChargenTemplate(State, Rules, Template);

	State.Sheet.RecomputeCurrent(Rules.Stats, &State.Effects);

	// 6. What was granted is the floor, and the pools are what the player spends on top of it. A
	//    History can replace either symbolic order through the effect layer, so read the final
	//    CURRENT values rather than the clan template's pre-History values used to seed the base.
	State.Baseline = State.Sheet;
	const int32 EffectiveAttribOrder = State.Sheet.GetCurrent(
		EElysiumTraitContainer::Attributes, ElysiumSlot::AttribOrder);
	const int32 EffectiveAbilityOrder = State.Sheet.GetCurrent(
		EElysiumTraitContainer::Abilities, 0);
	State.Pools = BuildPools(Rules, State.Clan, EffectiveAttribOrder, EffectiveAbilityOrder,
		Template.GeneralInt(TEXT("Kindred"), 1) != 0);
}

void BeginLevelUp(FElysiumChargenState& State, const FElysiumSheet& Sheet,
	const FElysiumSheetEffects& Effects)
{
	State.Currency = EElysiumChargenCurrency::Experience;
	State.Sheet = Sheet;
	State.Baseline = Sheet;
	State.Effects = Effects;
	State.Clan = Sheet.Clan();
	State.bMale = Sheet.IsMale();
	State.Pools.Reset();
	State.Spent.Reset();
}

bool CanBuy(const FElysiumChargenState& State, const FElysiumChargenRules& Rules,
	EElysiumTraitContainer Container, int32 Slot, int32& OutCost)
{
	OutCost = 0;

	const EElysiumChargenPool Pool = PoolFor(Container, Slot);
	if (Pool == EElysiumChargenPool::None)
	{
		return false;
	}
	if (!IsRowVisible(Rules, State.Sheet, Container, Slot))
	{
		return false;
	}

	const FElysiumStat* Stat = Rules.Stats ? Rules.Stats->Container(Container).At(Slot) : nullptr;
	if (Stat == nullptr || Stat->bDisabled)
	{
		return false;
	}

	const int32 Base = State.Sheet.GetBase(Container, Slot);
	int32 Min = 0, Max = 0;
	State.Sheet.BoundsFor(Container, Slot, Rules.Stats, &State.Effects, Min, Max);
	if (Max >= Min && Base >= Max)
	{
		return false;
	}
	for (const FString& Expr : Stat->IncPredependency)
	{
		if (!ElysiumSheetRules::EvalPredependency(Expr, State.Sheet))
		{
			return false;
		}
	}

	const int32 Price = PriceOf(State, Rules, Container, Slot, Base);
	if (Price >= FElysiumStatCost::CannotBuy)
	{
		return false;
	}
	OutCost = Price;
	return State.Available(Pool) >= OutCost;
}

bool Buy(FElysiumChargenState& State, const FElysiumChargenRules& Rules,
	EElysiumTraitContainer Container, int32 Slot)
{
	int32 Cost = 0;
	if (!CanBuy(State, Rules, Container, Slot, Cost))
	{
		return false;
	}
	if (!State.Sheet.IncBase(Container, Slot, Rules.Stats, &State.Effects))
	{
		return false;   // the sheet's own gate refused — charge nothing
	}
	if (State.Currency == EElysiumChargenCurrency::Pools)
	{
		State.Spent[PoolFor(Container, Slot)] += Cost;
	}
	else
	{
		State.SetExperience(State.Experience() - Cost);
		State.Sheet.RecomputeCurrent(Rules.Stats, &State.Effects);
	}
	return true;
}

bool CanSell(const FElysiumChargenState& State, const FElysiumChargenRules& Rules,
	EElysiumTraitContainer Container, int32 Slot, int32& OutRefund)
{
	OutRefund = 0;

	const EElysiumChargenPool Pool = PoolFor(Container, Slot);
	if (Pool == EElysiumChargenPool::None)
	{
		return false;
	}

	const int32 Base = State.Sheet.GetBase(Container, Slot);
	// The baseline is the floor: a granted dot cost nothing, so there is nothing to refund.
	if (Base <= State.Baseline.GetBase(Container, Slot))
	{
		return false;
	}

	const FElysiumStat* Stat = Rules.Stats ? Rules.Stats->Container(Container).At(Slot) : nullptr;
	if (Stat == nullptr || Base <= Stat->MinSell)
	{
		return false;
	}

	// A dot refunds exactly what it cost — `Sell(r)` is priced as `Buy(r-1)`, which is the buy/sell
	// symmetry the engine's own cost model holds to.
	const int32 Refund = PriceOf(State, Rules, Container, Slot, Base - 1);
	if (Refund >= FElysiumStatCost::CannotBuy)
	{
		return false;
	}
	OutRefund = Refund;
	return true;
}

bool Sell(FElysiumChargenState& State, const FElysiumChargenRules& Rules,
	EElysiumTraitContainer Container, int32 Slot)
{
	int32 Refund = 0;
	if (!CanSell(State, Rules, Container, Slot, Refund))
	{
		return false;
	}
	State.Sheet.SetBase(Container, Slot, State.Sheet.GetBase(Container, Slot) - 1);

	if (State.Currency == EElysiumChargenCurrency::Pools)
	{
		const EElysiumChargenPool Pool = PoolFor(Container, Slot);
		State.Spent[Pool] = FMath::Max(0, State.Spent[Pool] - Refund);
	}
	else
	{
		State.SetExperience(State.Experience() + Refund);
	}
	State.Sheet.RecomputeCurrent(Rules.Stats, &State.Effects);
	return true;
}

bool IsRowVisible(const FElysiumChargenRules& Rules, const FElysiumSheet& Sheet,
	EElysiumTraitContainer Container, int32 Slot)
{
	if (PoolFor(Container, Slot) == EElysiumChargenPool::None)
	{
		return false;
	}
	const FElysiumStat* Stat = Rules.Stats ? Rules.Stats->Container(Container).At(Slot) : nullptr;
	if (Stat == nullptr || Stat->bDisabled)
	{
		return false;
	}
	const int32 Value = Sheet.GetCurrent(Container, Slot);
	return Value >= 0 && Value < 6;
}

int32 SuggestClan(const FElysiumWizard& Wizard, const FElysiumClanTable& Clans,
	const TArray<FString>& Ordering)
{
	int32 BestClan = 0;
	int32 BestScore = 0;

	for (const FElysiumWizClanNode& Node : Wizard.ClanNodes)
	{
		int32 Score = 0;
		bool bRanked = false;
		for (int32 Pick = 0; Pick < Ordering.Num() && Pick < 3; ++Pick)
		{
			const int32 Rank = Node.RankOf(Ordering[Pick]);
			if (Rank >= 0 && Rank < 3)
			{
				Score += Wizard.ConnectionScores[Pick][Rank];
				bRanked = true;
			}
		}
		// A clan that ranks none of the picks is not a suggestion — which is also what makes an
		// empty ordering (the route that skips the quiz) suggest nothing at all.
		if (!bRanked || (BestClan != 0 && Score <= BestScore))
		{
			continue;   // ties go to the earlier node, which is file order
		}
		// The node names a clandoc template; its position in `clandoc000.txt` is the clan index the
		// sheet holds.
		const FElysiumClanTemplate* Template = Clans.Find(Node.CharTemplate);
		if (Template == nullptr || !FElysiumSheet::IsValidClan(Template->Index))
		{
			continue;
		}
		BestClan = Template->Index;
		BestScore = Score;
	}
	return BestClan;
}

}   // namespace ElysiumChargen

// --- The quiz ---

namespace ElysiumChargen
{

const TCHAR* const WizEntryPopup = TEXT("Help_Popup0");

namespace
{
	// The Society of Leopold hunter campaign. **Omitted** — a marked, reversible divergence
	// (`docs/vtmb/game_runtime.md`): its templates are the multiplayer clans 9..11, which the sheet's
	// clan encoding, the clan sigils and the body lookup all stop short of. The entry popup's third
	// action is dropped rather than left to open a path that dead-ends.
	const TCHAR* const GOmittedPopup = TEXT("Hunter_Selection");

	int32 TallyOf(const FElysiumChargenState& State, const FString& Trait)
	{
		const int32* Found = State.Tally.Find(ElysiumFold(Trait));
		return Found ? *Found : 0;
	}

	bool PrereqsHold(const TArray<FElysiumWizPrereq>& Prereqs, const FElysiumChargenState& State)
	{
		for (const FElysiumWizPrereq& P : Prereqs)
		{
			if (!P.Admits(TallyOf(State, P.Trait)))
			{
				return false;
			}
		}
		return true;
	}

	// A popup's own gates: its trait prerequisites, and — when it names one — the character template
	// it belongs to, which is how the seven per-clan phrasings of one help popup pick themselves.
	bool PopupAdmits(const FElysiumWizPopup& Popup, const FElysiumChargenState& State,
		const FElysiumClanTable* Clans)
	{
		if (!PrereqsHold(Popup.Prereqs, State))
		{
			return false;
		}
		if (Popup.CharTemplate.IsEmpty())
		{
			return true;
		}
		const FElysiumClanTemplate* Row = Clans ? Clans->Find(Popup.CharTemplate) : nullptr;
		return Row != nullptr && Row->Index == State.Clan;
	}

	void FillChoices(FElysiumWizRun& Run, const FElysiumChargenState& State)
	{
		Run.Choices.Reset();
		if (Run.Popup == nullptr)
		{
			return;
		}
		for (const FElysiumWizAction& Action : Run.Popup->Actions)
		{
			// An action the group's `Defaults` supplied but the popup never filled in has no text and
			// is not a choice — which is also what drops the unused `KeyLookup` alternates.
			if (Action.Text.IsEmpty() || !PrereqsHold(Action.Prereqs, State))
			{
				continue;
			}
			if (Action.Next.Equals(GOmittedPopup, ESearchCase::IgnoreCase))
			{
				continue;
			}
			Run.Choices.Add(&Action);
		}
	}

	// Move to the popup named, picking among same-named phrasings from the owned stream so a seeded
	// run replays identically. Leaves the run finished when the name is empty or nothing admits.
	void GoTo(FElysiumWizRun& Run, const FElysiumChargenState& State, const FString& Name,
		const FElysiumClanTable* Clans)
	{
		Run.Popup = nullptr;
		if (Name.IsEmpty() || Run.Wizard == nullptr)
		{
			Run.bFinished = true;
			return;
		}

		TArray<const FElysiumWizPopup*> Candidates;
		Run.Wizard->PopupsNamed(Name, Candidates);
		Candidates.RemoveAll([&State, Clans](const FElysiumWizPopup* P)
		{
			return P == nullptr || !PopupAdmits(*P, State, Clans);
		});
		if (Candidates.IsEmpty())
		{
			Run.bFinished = true;
			return;
		}

		const int32 Pick = Candidates.Num() == 1
			? 0
			: ElysiumRng::Stream(EElysiumRngStream::Chargen).RandRange(0, Candidates.Num() - 1);
		Run.Popup = Candidates[Pick];

		// The group a popup belongs to decides when the section advances, so it is re-read here
		// rather than assumed to be the one the run started in.
		for (const FElysiumWizGroup& Group : Run.Wizard->Groups)
		{
			if (Group.Popups.ContainsByPredicate(
				[Chosen = Run.Popup](const FElysiumWizPopup& P) { return &P == Chosen; }))
			{
				if (Group.InternalName != Run.GroupName)
				{
					Run.GroupName = Group.InternalName;
					Run.Answered = 0;
				}
				break;
			}
		}

		FillChoices(Run, State);
		if (Run.Choices.IsEmpty())
		{
			// A popup with nothing to click cannot be answered; ending the run is better than
			// standing on it.
			Run.Popup = nullptr;
			Run.bFinished = true;
		}
	}
}

void WizBegin(FElysiumWizRun& Run, const FElysiumWizard& Wizard, const FElysiumChargenState& State,
	const FString& PopupName)
{
	Run = FElysiumWizRun();
	Run.Wizard = &Wizard;
	GoTo(Run, State, PopupName, nullptr);
}

bool WizChoose(FElysiumWizRun& Run, FElysiumChargenState& State, int32 Index)
{
	if (!Run.IsActive() || !Run.Choices.IsValidIndex(Index))
	{
		return Run.IsActive();
	}
	const FElysiumWizAction& Action = *Run.Choices[Index];

	// What an answer does: it tallies a trait, and it may set the sex or name a clan outright.
	if (!Action.Trait.IsEmpty())
	{
		State.Tally.FindOrAdd(ElysiumFold(Action.Trait)) += 1;
	}
	if (Action.bSetGenderMale)   { State.bMale = true; }
	if (Action.bSetGenderFemale) { State.bMale = false; }

	++Run.Answered;

	// A checkbox is a preference, not an answer: it must not consume the question.
	if (Action.bIsCheckBox)
	{
		--Run.Answered;
		return true;
	}

	if (Action.bEndCharGenWiz)
	{
		Run.Popup = nullptr;
		Run.Choices.Reset();
		Run.bFinished = true;
		return false;
	}

	FString Next = Action.Next;
	if (Next.IsEmpty())
	{
		// The group's own advance: once enough of its popups have been answered, the section moves
		// on. `Trait_Popups` runs 6 to 8 questions before it hands off to the winner section.
		if (const FElysiumWizGroup* Group = Run.Wizard->Group(Run.GroupName))
		{
			if (Run.Answered >= Group->NextSection.MinCount)
			{
				Next = Group->NextSection.Next;
			}
		}
	}

	GoTo(Run, State, Next, nullptr);
	return Run.IsActive();
}

void WizOrdering(const FElysiumChargenState& State, const FElysiumWizard& Wizard,
	TArray<FString>& Out)
{
	Out.Reset();
	// Sorted by tally, ties broken by the wizard's own trait order — so two traits picked the same
	// number of times always order the same way, and a seeded run scores the same clan twice.
	TArray<FString> Traits = Wizard.Traits;
	Traits.StableSort([&State](const FString& A, const FString& B)
	{
		return TallyOf(State, A) > TallyOf(State, B);
	});
	for (const FString& Trait : Traits)
	{
		if (TallyOf(State, Trait) <= 0 || Out.Num() >= 3)
		{
			continue;
		}
		Out.Add(Trait);
	}
}

}   // namespace ElysiumChargen
