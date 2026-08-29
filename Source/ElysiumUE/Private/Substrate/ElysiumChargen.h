#pragma once

#include "CoreMinimal.h"

#include "ElysiumPlayer.h"
#include "Substrate/ElysiumChargenWizard.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumSheetMath.h"

// Character generation as a pure decision layer: the point pools a clan and a trait order produce,
// the auto-levelled baseline they sit on top of, and what a dot costs to buy or refunds when sold.
//
// No Slate, no UObject, no world — the same split `ElysiumQuestLog` and `ElysiumSheetMath` make.
// The character screen performs what this reports; the commit onto the player entity is the UI
// subsystem's.
//
// The VtMB facts: `docs/vtmb/game_runtime.md` section 3 ("The point pools", "Buying a dot"). Every rule
// below is the engine's.
//
// **Chargen spends dots, not experience.** A category point buys one dot outright; the `Costs`
// blocks in `stats.txt` price the level-up path and are not consulted here. The shipped numbers
// leave no other reading — a clan's 2-point Physical pool against an attribute raise priced
// `Current_Rating * 4` could not buy a single dot, and the one discipline point could never buy
// anything at all.

// --- The seven pools ---

// The categories chargen spends into. They are code, not data: `stats.txt` authors no category key,
// and the engine's own sheet screen hardcodes which slot range each heading covers — the same way
// `Public/ElysiumSheetSlots.h` hardcodes the slots themselves. Their DISPLAY names are data
// (`strings.txt`'s `AttributeGroup`, `AbilityGroup`, `StatCategoryTitles`).
enum class EElysiumChargenPool : uint8
{
	Physical = 0,
	Social,
	Mental,
	Talents,
	Skills,
	Knowledges,
	Disciplines,
	Count,
	None = 0xFF,   // a slot chargen does not sell — Clan, Experience, Max_Health, …
};

// The `AttributeGroup` / `AbilityGroup` index a pool answers to. Both files number their three
// categories 0..2, so the attribute pools and the ability pools share the numbering.
int32 ElysiumChargenPoolGroupIndex(EElysiumChargenPool Pool);
// The container a pool spends into. Disciplines answers `Disciplines`; the other six `Attributes`
// or `Abilities`.
EElysiumTraitContainer ElysiumChargenPoolContainer(EElysiumChargenPool Pool);

struct FElysiumChargenPools
{
	int32 Points[(uint8)EElysiumChargenPool::Count] = {};

	int32& operator[](EElysiumChargenPool Pool)       { return Points[(uint8)Pool]; }
	int32  operator[](EElysiumChargenPool Pool) const { return Points[(uint8)Pool]; }

	int32 Total() const;
	void Reset();
};

// --- The state a chargen run carries ---

// What a dot is paid with. The two spend screens are the same screen — the same rows, the same
// gates, the same buy/sell symmetry — differing only in the currency and where its floor is.
enum class EElysiumChargenCurrency : uint8
{
	Pools,        // chargen: one point out of the slot's category pool
	Experience,   // the in-game level-up sheet: the `Costs` price against the Experience slot
};

struct FElysiumChargenState
{
	EElysiumChargenCurrency Currency = EElysiumChargenCurrency::Pools;

	FString Name;
	int32 Clan = 0;                  // the 2..8 encoding `FElysiumSheet::Clan()` speaks
	bool bMale = true;
	int32 HistoryId = INDEX_NONE;    // an index into `FElysiumHistoryTable`
	bool bSkipIntro = false;

	// The auto-levelled baseline plus everything bought on top of it.
	FElysiumSheet Sheet;
	// The sheet exactly as `ApplyBaseline` left it — the floor a Sell may not cross, because a
	// baseline dot was granted rather than paid for and has nothing to refund.
	FElysiumSheet Baseline;
	// The clan's `ClanEffect` and the History's `Effect`, resolved. Every bound and every read on
	// `Sheet` goes through it, so a clan bane applies while the player is still spending.
	FElysiumSheetEffects Effects;

	FElysiumChargenPools Pools;
	FElysiumChargenPools Spent;

	// The quiz's running tally and the player's ordered trait picks. Filled by the popup runner;
	// empty on the route that goes straight to the sheet.
	TMap<FString, int32> Tally;
	TArray<FString> Ordering;

	int32 Remaining(EElysiumChargenPool Pool) const { return Pools[Pool] - Spent[Pool]; }
	int32 TotalRemaining() const { return Pools.Total() - Spent.Total(); }
	// Every pool spent out. This is what arms chargen's ACCEPT — the level-up screen has no such
	// requirement, because banked experience is allowed to stay banked.
	bool IsSpentOut() const { return TotalRemaining() <= 0; }

	// The unspent experience on the sheet, which is what `Currency::Experience` draws down.
	int32 Experience() const
	{
		return Sheet.GetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::Experience);
	}
	void SetExperience(int32 Value)
	{
		Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::Experience, Value);
	}

	// The currency left for this slot's purchase — a pool in chargen, the XP total at level-up.
	int32 Available(EElysiumChargenPool Pool) const
	{
		return Currency == EElysiumChargenCurrency::Pools ? Remaining(Pool) : Experience();
	}
};

// The rulebook a chargen call needs, gathered once rather than threaded through nine parameters.
// Every member may be null and every entry point survives it — a missing table costs the rule it
// carries, not the run.
struct FElysiumChargenRules
{
	const FElysiumStatTable* Stats = nullptr;
	const FElysiumRules* Rules = nullptr;
	const FElysiumClanTable* Clans = nullptr;
	const FElysiumHistoryTable* Histories = nullptr;
	const FElysiumLevelingTemplates* Leveling = nullptr;
	const FElysiumTraitEffects* TraitEffects = nullptr;
	const FElysiumFeatTable* Feats = nullptr;
	const FElysiumStrings* Strings = nullptr;

	bool IsValid() const { return Stats != nullptr && Clans != nullptr; }
};

// --- The rules ---

namespace ElysiumChargen
{
	// The pool a slot spends from, or `None` when chargen does not sell it. Attributes 1-3 / 4-6 /
	// 7-9 are Physical / Social / Mental and Abilities 1-4 / 5-8 / 9-12 are Talents / Skills /
	// Knowledges; every other Attribute slot (Clan, Gender, BloodPool, the derived block) is `None`,
	// and so is the whole `Active_Disciplines` container.
	EElysiumChargenPool PoolFor(EElysiumTraitContainer Container, int32 Slot);

	// The clan's `Player_<name>` template, resolved. Null when the clan is out of range or the
	// table did not load.
	bool ResolveClanTemplate(const FElysiumChargenRules& Rules, int32 Clan,
		FElysiumClanTemplate& Out);

	// `Attrib_Order` / `Ability_Order` as the clan template authors them — a symbolic name
	// (`"Physical_Mental_Social"`) resolved through the stat's `NameMapping` group. Returns the
	// stat's `Default` when the name does not resolve, which is the engine's own fallback.
	int32 ResolveOrder(const FElysiumChargenRules& Rules, const FElysiumClanTemplate& Template,
		EElysiumTraitContainer Container, int32 Slot);

	// The seven pools: a CLAN term plus a TIER term.
	//
	//   * The clan term is `rules_tables.txt`'s `Subpool_<category>` indexed by the clandoc template
	//     index. Every attribute/ability subpool ships all-zero (Troika balanced them out and left
	//     the tables in place); `Subpool_Disciplines` ships 1 for every playable clan.
	//   * The tier term is the `Subpool_*_Primary_Secondary_Tertiary` table nested on the
	//     `Attrib_Order` / `Ability_Order` stat — 2/1/0 for attributes, 3/2/1 for abilities — read
	//     at the tier the ORDER gives that category. The `_Kine` variants (1/1/1) apply when the
	//     template's `Kindred` is 0.
	//   * Disciplines take the clan term alone: there is no discipline order and no tier table.
	FElysiumChargenPools BuildPools(const FElysiumChargenRules& Rules, int32 Clan,
		int32 AttribOrder, int32 AbilityOrder, bool bKindred);

	// Seed the sheet, overlay the clan template, resolve the effect layer, then BUY the baseline by
	// running the clan's `CharGen_AutoLevel_Template` — VtMB's own `giftxp 9000` + `vautolvl
	// <clan>_CharGen`, which is why the starting dots are bought rather than written. Snapshots
	// `Baseline`, rebuilds `Pools`, and clears `Spent`.
	//
	// Runs BEFORE any point-buy, and again whenever the clan or the History changes — both move the
	// baseline, so both invalidate what was spent on top of it.
	void ApplyBaseline(FElysiumChargenState& State, const FElysiumChargenRules& Rules);

	// The level-up screen's equivalent: a scratch over a live character. The sheet as it stands is
	// both the working copy and the sell floor, so a dot bought this session can be sold back and
	// one earned before it cannot, and the currency is the Experience slot the sheet already holds.
	// Nothing reaches the character until the caller commits the scratch.
	void BeginLevelUp(FElysiumChargenState& State, const FElysiumSheet& Sheet,
		const FElysiumSheetEffects& Effects);

	// Whether the dot can be bought, and what it costs — always one point out of the slot's own
	// category pool. Refused when the row is hidden, at the stat's effective max, on a failed
	// `IncPredependency`, and when the pool is spent out.
	bool CanBuy(const FElysiumChargenState& State, const FElysiumChargenRules& Rules,
		EElysiumTraitContainer Container, int32 Slot, int32& OutCost);
	bool Buy(FElysiumChargenState& State, const FElysiumChargenRules& Rules,
		EElysiumTraitContainer Container, int32 Slot);

	// The mirror: the point comes back. Refused at the stat's `MinSell` and at the baseline — a
	// granted dot was never paid for.
	bool CanSell(const FElysiumChargenState& State, const FElysiumChargenRules& Rules,
		EElysiumTraitContainer Container, int32 Slot, int32& OutRefund);
	bool Sell(FElysiumChargenState& State, const FElysiumChargenRules& Rules,
		EElysiumTraitContainer Container, int32 Slot);

	// Whether the sheet draws a row for this slot at all. The rule is the value, not the price: a
	// row shows only while its CURRENT value is in [0, 6). This is what hides a non-clan discipline
	// — every discipline stat is authored `Min -1` / `Default -1`, and a clan template writes 1 only
	// for the ones that clan has, so the rest stay at -1. The cost layer is never asked: the `-1` is
	// a row filter, not a price.
	bool IsRowVisible(const FElysiumChargenRules& Rules, const FElysiumSheet& Sheet,
		EElysiumTraitContainer Container, int32 Slot);

	// SuggestClan is declared with the quiz below, which is what produces the ordering it scores.
}

// --- The quiz ---

// One run of `charcreatewizard.txt`'s popup chain: the entry popup, the personality questions, and
// the clan suggestion they produce. Pure — it holds pointers into the loaded wizard and mutates the
// chargen state's tally; the widget only draws what `Choices` says and reports which one was
// clicked.
//
// **Route 3, the Society of Leopold hunter campaign, is omitted** — a marked, reversible divergence
// recorded in `docs/vtmb/game_runtime.md`. The action that leads to it is filtered out of the entry
// popup rather than left to open a path that dead-ends.
struct FElysiumWizRun
{
	const FElysiumWizard* Wizard = nullptr;
	// The popup on screen and the surviving actions of it, in authored order — what the widget draws.
	const FElysiumWizPopup* Popup = nullptr;
	TArray<const FElysiumWizAction*> Choices;

	FString GroupName;      // the group whose `NextSection` governs the advance
	int32 Answered = 0;     // popups answered inside that group
	bool bFinished = false; // the chain ended — the sheet is what comes next

	bool IsActive() const { return !bFinished && Popup != nullptr; }
};

namespace ElysiumChargen
{
	// The wizard's own entry point: `Help_Popup0`, the three-route question. Route 1 walks the quiz,
	// route 2 ends the run immediately and lands on the sheet with nothing preselected.
	extern const TCHAR* const WizEntryPopup;

	// Open the run at a named popup. `bFinished` comes back true when the name resolves to nothing,
	// which is what a caller that wants no quiz at all can rely on.
	void WizBegin(FElysiumWizRun& Run, const FElysiumWizard& Wizard,
		const FElysiumChargenState& State, const FString& PopupName);

	// Take the choice at `Index`: apply its trait to the tally, its gender and its clan to the
	// state, then follow `Next` — or advance the group through `NextSection` when the popup names
	// none. Returns whether the run is still active.
	bool WizChoose(FElysiumWizRun& Run, FElysiumChargenState& State, int32 Index);

	// The player's three highest-tallied traits, most-picked first — the ordering `SuggestClan`
	// scores. Ties break on the wizard's own trait order, so the result is deterministic.
	void WizOrdering(const FElysiumChargenState& State, const FElysiumWizard& Wizard,
		TArray<FString>& Out);

	// The clan the quiz suggests: the 3x3 `ConnectionScores` summed over the player's ordered trait
	// picks against each clan node's ranking of them. Ties go to the earlier clan node, as file
	// order decides. Returns the 2..8 clan encoding, or 0 when nothing scored.
	int32 SuggestClan(const FElysiumWizard& Wizard, const FElysiumClanTable& Clans,
		const TArray<FString>& Ordering);
}
