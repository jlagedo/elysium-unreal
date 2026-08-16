#pragma once

#include "CoreMinimal.h"

#include "ElysiumSheetSlots.h"

// How a *value* used as a lookup key is normalised. The KV reader lowercases block keys already, so
// this is for the other half — a `TemplateName`, a quest `Title`, an effect's `Trait`. Every layer
// that keys off one folds it the same way here, because a lookup that folded differently would
// silently miss rather than fail.
inline FString ElysiumFold(const FString& S) { return S.ToLower(); }

// VtMB's rulebook — the RPG rules layer under `vdata/system/`, mirrored verbatim by PL5b.
//
// Twelve table families, one struct pair each: a row struct and a table struct carrying
// `Load(FString& OutError)`, following `FElysiumDispositionTable`'s shape. Every `Load` is
// idempotent and cheap; the caller caches (`UElysiumRulebookSubsystem` does).
//
// Three rules hold across all of them:
//
//   * **A loader keys off the filename, not the root block key.** `rules.txt` and
//     `rules_tables.txt` are both `RuleData`; `clandoc000.txt` and all 36 `npctemplate*.txt` are
//     both `ClanDataTables`.
//   * **File order is the engine's index.** A trait is `(container, position)`, a clan is its
//     position in `clandoc000.txt`, a history is its position in `histories000.txt` — and the save
//     stores those indices, not names (`docs/vtmb/savegame_format.md`). Nothing here sorts.
//   * **An empty container is legal, not an error.** `quests_main.txt` holds no quests,
//     `npctemplate019/021.txt` hold no templates, `rules.txt`'s `Tables` block is a stub.
//
// Nothing in `vdata/` is ever saved, so these tables are session-lifetime and re-read at load —
// which is what lets a patched rulebook re-apply to an existing save.
//
// The VtMB facts these model: `docs/vtmb/game_runtime.md` §3. The per-file inventory:
// `docs/vtmb/vdata-catalog.md`. `dispositiontable.txt` is not here — it is read by
// `FElysiumDispositionTable`, which the NPC animation subsystem owns.

namespace ElysiumKeyValues { struct FKvNode; }

// ================================================================================================
// Shared value types
// ================================================================================================

// One price line out of a `Costs` block. Three authored forms, per `CVStatCost_t::Load`:
// `"Current_Rating * N"`, `"Table: a, b, c, …"`, or a bare integer.
struct FElysiumStatCost
{
	enum class EKind : uint8
	{
		None,        // no such key
		Flat,        // a bare integer
		PerRating,   // `Current_Rating * N` — engine cost type 9999
		Table,       // `Table: a, b, c, …` — engine cost type 9998
	};

	EKind Kind = EKind::None;
	int32 Amount = 0;         // Flat: the price. PerRating: the N.
	TArray<int32> Steps;      // Table: the per-rating prices in authored order.

	// The engine's "this cannot be bought" verdict. It is a runtime value, not authored data: no
	// shipped stat carries it. A derived stat is instead priced out of reach with a flat `10000`,
	// which no screen offers and no XP total reaches.
	static constexpr int32 CannotBuy = 30000;

	bool Parse(const FString& Raw);
	bool IsSet() const { return Kind != EKind::None; }

	// The price of the step out of `CurrentRating` — which is the *pre-purchase base* rating, so
	// `Buy(r) = N*r` and `Sell(r) = N*(r-1)` and a dot always refunds what it cost.
	int32 At(int32 CurrentRating) const;
	bool CanBuy(int32 CurrentRating) const { return IsSet() && At(CurrentRating) < CannotBuy; }

	FString Describe() const;
};

// A `Costs { New, Raise }` block. `New` prices only the 0 -> 1 step; if only one of the two keys is
// authored the loader copies it into the other, as `CVStatCost_t::Load` does.
struct FElysiumStatCosts
{
	FElysiumStatCost New;
	FElysiumStatCost Raise;
	bool bPresent = false;

	void Load(const ElysiumKeyValues::FKvNode& Node);
	FString Describe() const;
};

// A trait name carrying `CVStatRef`'s optional trailing modifier: `"Armor_Rating / 2"`.
// The multiplier is `1` for identity, positive N for `* N` and negative N for `/ |N|` (integer),
// which is the shape `CVStatRef::Apply` holds at `+0xC`.
struct FElysiumTraitRef
{
	FString Trait;
	int32 Mul = 1;

	static FElysiumTraitRef Parse(const FString& Raw);
	int32 Apply(int32 Value) const;
	FString Describe() const;
};

// An integer-keyed lookup table. Shared by `rules_tables.txt`'s 20 `Table` blocks and the 31
// nested ones inside `stats.txt` and `feats.txt` — one shape, authored the same way in all three.
// Values are a mix of ints and floats within one table, so the raw string is kept and converted
// on read.
struct FElysiumRuleTable
{
	FString InternalName;
	FString Name;
	FString TraitDependency;
	bool bClamping = false;
	TMap<int32, FString> Rows;

	void Load(const ElysiumKeyValues::FKvNode& Node);
	bool IsValid() const { return !Rows.IsEmpty(); }

	// The row at Key. When `bClamping`, a Key outside the authored range takes the nearest end;
	// otherwise a miss returns Def.
	float Lookup(int32 Key, float Def = 0.f) const;
	const FString* Find(int32 Key) const;
	void KeyRange(int32& OutMin, int32& OutMax) const;
};

// ================================================================================================
// 1. stats.txt — the four trait containers
// ================================================================================================

// `EElysiumTraitContainer` and `ElysiumTraitContainerName` are the compiled side's
// (`Public/ElysiumSheetSlots.h`) — one enum serves the file and the sheet it fills.

// One `Stat` block. Its position in the container is the engine's trait id, so `Index` is data.
struct FElysiumStat
{
	FString InternalName;      // the engine key — lookups are case-insensitive
	FString Name;              // display
	int32 Index = INDEX_NONE;  // position in the container, the `*_Order` block occupying 0

	int32 Min = 0;
	int32 Max = 0;
	int32 Default = 0;
	int32 MinSell = 0;         // absent -> Min  (CVStatInfo_t::PostLoad)
	int32 MaxBuy = 0;          // absent -> Max

	// `Min`/`Max` may hold a stat *name* rather than a number (`"Max" "Max_Health"`,
	// `"Max" "Animalism"`). The parsed ints above are the numeric reading; these are what was
	// authored, kept raw for the resolver that needs them.
	FString MinExpr;
	FString MaxExpr;

	FString NameMapping;       // a strings.txt display table
	FString NameFunc;          // a code-side display function (`ClanNameFunc`)
	bool bDisabled = false;

	// What the sheet's detail panel reads. `HelpText` is the trait blurb and `HelpText2` the
	// mechanical note under it; the two five-entry lists are the per-rating `LEVEL n:` pair, which
	// only the disciplines author. An unauthored rating is an empty entry, so the index is the
	// rating and never shifts.
	FString HelpText;
	FString HelpText2;
	TArray<FString> LevelHeadings;   // HelpTextH1..5
	TArray<FString> LevelDetails;    // HelpTextL1..5

	// Gates on raising the stat, e.g. `"BloodPool > 0"` and `"Health < Max_Health"`. Authored more
	// than once per block on 17 of the Active_Disciplines, which is why this is a list.
	TArray<FString> IncPredependency;

	// --- The Discipline half of a Stat block (`docs/vtmb/disciplines.md`) ---------------------
	// The four classification bytes the learned Discipline blocks author. `Is_Instant` is the
	// native-path discriminator the shared cast authority branches on; the other three are
	// authored classification the same blocks carry beside it.
	bool bIsInstant = false;
	bool bIsPassive = false;
	bool bIsRenewable = false;
	bool bIsAggressive = false;

	// `Durations { "Initial_N" "..." "Add_N" "..." }` on an `Active_*` block: the seconds a fresh
	// activation at rating N runs for, and the seconds a renewal at rating N extends by. Both are
	// 1-based on the rating; index 0 is unauthored and stays 0.
	struct FDurations
	{
		bool bAuthored = false;
		int32 Initial[6] = { 0, 0, 0, 0, 0, 0 };
		int32 Add[6] = { 0, 0, 0, 0, 0, 0 };
	};
	FDurations Durations;

	// One `Action` block on an `Active_*` stat — the file's own comment: "the Trait Effect to
	// automatically add when activating this level of the Discipline (automatically removed when
	// the Discipline de-activates)". A row with `Triggers Inc` fires ONCE on the increment (the
	// blood payment, `BloodHealFunc`); a row without one is state held for as long as the value
	// stands (the trait-effect group, the particles).
	struct FAction
	{
		bool bOnIncrement = false;   // `"Triggers" "Inc"`
		FString ValueExpr;           // `"1"`, `">0"`, `">2"` — a predicate on the active value
		FString Predependency;       // `"Clan != Toreador"`, evaluated by the owning system
		FString StatMutation;        // `"BloodPool -1"` — `<InternalName> <signed delta>`
		FString Effect;              // a `TraitEffectGroup` InternalName
		FString Function;            // a named native function (`"BloodHealFunc"`)
		bool bHasParticle = false;   // a `Particle` sub-block was authored (presentation, unread here)

		// Whether this row's `Value` predicate admits `Value`. An unauthored/unparseable expression
		// admits nothing, so a row nobody can read never fires.
		bool Admits(int32 Value) const;
	};
	TArray<FAction> Actions;

	// The stat's own price, when it overrides its container's default.
	FElysiumStatCosts Costs;

	// Nested lookup tables, keyed by `InternalName`.
	TMap<FString, FElysiumRuleTable> Tables;

	bool IsValid() const { return !InternalName.IsEmpty(); }
};

// A container is a **flat ordered list**, not a category tree: `Attributes` runs `Attrib_Order`(0),
// `Strength`(1) … `Wits`(9) and then every derived/bookkeeping stat through `Experience`(34).
struct FElysiumStatContainer
{
	FString InternalName;
	TArray<FElysiumStat> Stats;
	FElysiumStatCosts DefaultCosts;   // the container-level `Costs` every stat falls back to

	const FElysiumStat* Find(const FString& InternalName) const;
	const FElysiumStat* At(int32 Index) const;
	int32 IndexOf(const FString& InternalName) const;
	int32 Num() const { return Stats.Num(); }

	// The price that applies to a stat: its own `Costs` when it has them, the container's otherwise.
	const FElysiumStatCosts& CostsFor(const FElysiumStat& Stat) const;

	void Load(const ElysiumKeyValues::FKvNode& Node, const FString& BlockName);

private:
	TMap<FString, int32> ByName;   // lowercased InternalName -> index
};

struct FElysiumStatTable
{
	FElysiumStatContainer Containers[(uint8)EElysiumTraitContainer::Count];

	bool Load(FString& OutError);
	bool IsValid() const { return Containers[0].Num() > 0; }

	const FElysiumStatContainer& Container(EElysiumTraitContainer Which) const
	{
		return Containers[(uint8)Which];
	}

	// Resolve a trait name across all four containers, in declaration order.
	const FElysiumStat* Find(const FString& InternalName,
		EElysiumTraitContainer* OutContainer = nullptr) const;
};

// ================================================================================================
// 2. feats.txt — the derived feats
// ================================================================================================

struct FElysiumFeat
{
	FString InternalName;
	FString Name;
	int32 Index = INDEX_NONE;   // file order is the feat id
	int32 MaxValue = 10;        // the rating clamp; 10 or 20, and the file warns of code limits

	// `Base%d`, counted by probing until a key is absent — genuinely variable: three on
	// `Soak_vs_Bashing`, none on `Damage` and `Frenzy_Feat`. Each may carry a `/ N` or `* N`.
	TArray<FElysiumTraitRef> Bases;

	// `Automatic%d` — automatic successes, NOT summed into the rating. One shipped use.
	TArray<FElysiumTraitRef> Automatics;

	// The `dicerolls.txt` weighting table this feat rolls on. All 23 name `Normal`; the engine
	// falls back to index 0 (which is also `Normal`) on a miss.
	FString PcWeighting;
	FString NpcWeighting;

	TMap<FString, FElysiumRuleTable> Tables;

	bool IsValid() const { return !InternalName.IsEmpty(); }
};

struct FElysiumFeatTable
{
	TArray<FElysiumFeat> Feats;

	bool Load(FString& OutError);
	bool IsValid() const { return !Feats.IsEmpty(); }

	const FElysiumFeat* Find(const FString& InternalName) const;
	const FElysiumFeat* At(int32 Index) const;
	int32 Num() const { return Feats.Num(); }

	// `Base0`, `Base1`, … read by probing until a key is ABSENT, which is what makes the list
	// genuinely variable-length. Public because the probe rule is the piece 9.4c depends on and
	// `Elysium.Substrate.Rulebook` drives it directly.
	static void ProbeTraitRefs(const ElysiumKeyValues::FKvNode& Node, const TCHAR* Prefix,
		TArray<FElysiumTraitRef>& Out);

	// Rebuild the name index from `Feats`, and re-stamp each row's `Index` to its position — file
	// order IS the feat id, so the two cannot be allowed to disagree. `Load` does this inline; a
	// hand-built table (the tests') needs it because the lookup IS the index, not a scan — the rule
	// `FElysiumQuestTables` established and `FElysiumSoundVolumeTable::Reindex` restated.
	void Reindex();

private:
	TMap<FString, int32> ByName;
};

// ================================================================================================
// 3. rules.txt + rules_tables.txt — the constants and the shared tables
// ================================================================================================

// `rules.txt` is a bag of named blocks whose key sets differ per block and are Unofficial-Patch
// tuned, so it is held as parsed key/value rather than as a row struct: a patch that adds a key
// must not need a code change to be readable.
struct FElysiumRules
{
	TArray<FString> BlockOrder;                             // top-level blocks, lowercased, file order
	// block -> key -> raw value. A NESTED block is keyed by its dotted path
	// (`vampheal_info.vampfeedingheal_info`), so `BlockOrder` and the key set differ.
	TMap<FString, TMap<FString, FString>> Blocks;

	TArray<FElysiumRuleTable> Tables;                       // rules_tables.txt

	bool Load(FString& OutError);
	bool IsValid() const { return !Blocks.IsEmpty(); }

	bool Has(const TCHAR* Block, const TCHAR* Key) const;
	const FString* Raw(const TCHAR* Block, const TCHAR* Key) const;
	int32   Int(const TCHAR* Block, const TCHAR* Key, int32 Def = 0) const;
	float   Flt(const TCHAR* Block, const TCHAR* Key, float Def = 0.f) const;
	FString Str(const TCHAR* Block, const TCHAR* Key, const FString& Def = FString()) const;

	const FElysiumRuleTable* Table(const FString& InternalName) const;

private:
	TMap<FString, int32> TableByName;
};

// ================================================================================================
// 4. traiteffect.txt + traiteffects000.txt — the modifier vocabulary and the effects
// ================================================================================================

// The operator enum. Its values ARE the `traiteffect.txt` `ModifierNames` indices — that file is
// the vocabulary and this is the compile-time mirror of it, which `Elysium.Content.Rulebook`
// asserts still agrees.
enum class EElysiumTraitOp : uint8
{
	Add = 0,      // `+` / `-` — also the fallback when no operator name matches
	Mul,          // `*`
	Div,          // `/`
	Max,          // `Max`
	Min,          // `Min`
	Percent,      // `%`
	Value,        // `Value` — takes a named payload, not only a number
	Cost,         // `Cost` — set by a `Costs` block with no `Modifier` key
	BloodCost,
	Damage,
	Duration,
	Count,
};

const TCHAR* ElysiumTraitOpName(EElysiumTraitOp Op);

// `traiteffect.txt` — the shipped operator names, in index order.
struct FElysiumModifierNames
{
	TArray<FString> Names;

	bool Load(FString& OutError);
	bool IsValid() const { return !Names.IsEmpty(); }

	// The engine matches operator names 1..N by PREFIX, then skips the name and any spaces.
	// Index 0 (`+`) is never matched by name — it is the fallback. Returns false when nothing
	// matched, leaving OutRest as the whole input.
	bool Match(const FString& Modifier, EElysiumTraitOp& OutOp, FString& OutRest) const;
};

struct FElysiumTraitEffect
{
	FString Trait;                      // a stat, a feat, or a `TraitFxStrs` `Fx_*` flag name
	EElysiumTraitOp Op = EElysiumTraitOp::Add;
	int32 Amount = 0;
	bool bPercent = false;              // the trailing `%` of `"Duration 200%"`
	FString ValueName;                  // op Value with a named payload (`"Value Clawed_Form"`)
	FElysiumStatCosts Costs;            // op Cost
	int32 DisplayOverride = -1;
	FString RawModifier;                // exactly as authored, for the verb and for diagnosis

	bool IsValid() const { return !Trait.IsEmpty(); }
};

struct FElysiumTraitEffectGroup
{
	FString InternalName;               // `"Clan (Brujah)"`, `"History (Homo)"`
	FString Category;                   // Clan | History | Discipline | Numina | Item
	// An array, never a map keyed by trait: one group legitimately names the same trait twice
	// (Brujah's `Animalism` takes a `Costs` effect AND a `Max 3` effect).
	TArray<FElysiumTraitEffect> Effects;
};

struct FElysiumTraitEffects
{
	FElysiumModifierNames Operators;
	TArray<FString> Categories;
	TArray<FElysiumTraitEffectGroup> Groups;

	bool Load(FString& OutError);
	bool IsValid() const { return !Groups.IsEmpty(); }

	const FElysiumTraitEffectGroup* Find(const FString& InternalName) const;
	int32 Num() const { return Groups.Num(); }
	int32 NumEffects() const;

	// Append a group and index it by name — the loader's own tail, exposed so a Substrate-tier test
	// can build a fabricated table for `ElysiumSheetRules::BindTables` without a `vdata` file behind
	// it. `Find` reads the index, so an appended group is invisible without it.
	void Add(FElysiumTraitEffectGroup&& Group);

	// Parse one authored `Modifier` string against the loaded vocabulary. Public because it is the
	// piece `Elysium.Substrate.Rulebook` drives directly.
	void ParseModifier(const FString& Raw, FElysiumTraitEffect& Out) const;

private:
	TMap<FString, int32> ByName;
};

// ================================================================================================
// 5. clandoc000.txt + the 36 npctemplate*.txt — clan and NPC stat templates
// ================================================================================================

// One `ClanData` block. The trait blocks are name -> rating maps rather than fixed fields, because
// a template authors only the traits it sets: an ABSENT key means "inherit", not zero, which is
// what makes `ParentTemplateName` load-bearing.
struct FElysiumClanTemplate
{
	FString TemplateName;
	FString ParentTemplateName;         // single-parent inheritance, resolving across files
	FString Name;
	FString Description;

	FString SourceFile;                 // "system/clandoc000.txt" or the npctemplate leaf
	int32 Index = INDEX_NONE;           // position within its own file — the engine's clan index

	TMap<FString, FString> General;     // the wide, template-specific key set, kept raw
	TMap<FString, int32> Attributes;
	TMap<FString, int32> Abilities;
	TMap<FString, int32> Disciplines;
	TMap<FString, int32> Numina;
	TMap<FString, int32> Resistances;
	TMap<FString, int32> Reactions;     // `Reactions { To { "Brujah" "+20" } }`
	TMap<FString, float> LoiterActivities;

	// The authored TEXT of any trait whose value is a symbolic name rather than a number —
	// `"Attrib_Order" "Physical_Mental_Social"`, `"Starting_Equipment" "Player_Kindred"`,
	// `"CharGen_AutoLevel_Template" "Brujah_CharGen"`. The trait maps above hold ints, so those
	// values would otherwise read as 0. The name resolves through the stat's `NameMapping` group in
	// `FElysiumStrings`, or against another table entirely (`CharGen_AutoLevel_Template` names a
	// leveling template and has no `stats.txt` slot at all).
	TMap<FString, FString> TraitText;

	bool IsValid() const { return !TemplateName.IsEmpty(); }
	bool IsPlayable() const { return TemplateName.StartsWith(TEXT("Player_")); }

	bool    HasGeneral(const TCHAR* Key) const;
	FString GeneralStr(const TCHAR* Key, const FString& Def = FString()) const;
	int32   GeneralInt(const TCHAR* Key, int32 Def = 0) const;

	// A trait's rating in this template alone, ignoring the parent chain.
	const int32* Trait(const FString& InternalName) const;
	// The same trait's authored text, when it was authored as a name rather than a number.
	FString TraitStr(const FString& InternalName, const FString& Def = FString()) const;
};

struct FElysiumClanTable
{
	TArray<FElysiumClanTemplate> Clans;          // clandoc000.txt — position is the clan index
	TArray<FElysiumClanTemplate> NpcTemplates;   // the 36 files, in filename order
	TArray<FString> NpcFiles;

	bool Load(FString& OutError);
	bool IsValid() const { return !Clans.IsEmpty(); }

	// Clans first, then NPC templates. Both name spaces are flat and do not collide.
	const FElysiumClanTemplate* Find(const FString& TemplateName) const;
	const FElysiumClanTemplate* Clan(int32 Index) const;

	// The template with its `ParentTemplateName` chain folded in — a child key wins, an absent one
	// takes the parent's. Cycle-guarded; returns false when the name resolves to nothing.
	bool Resolve(const FString& TemplateName, FElysiumClanTemplate& Out) const;

	// The chain a name walks, nearest first. The diagnosis half of Resolve.
	void ParentChain(const FString& TemplateName, TArray<FString>& Out) const;

	// The full authored `.mdl` path and its `npc_index.json` stem for a playable clan's body: the
	// template at `ClanIndex` resolved, then its `M_Body<slot>` / `F_Body<slot>`. Empty when the clan
	// is not playable or the slot is absent. PlayerBodyModel normalizes separators; PlayerBodyStem
	// takes its lower-cased basename for the skeletal export index.
	//
	// Only the INDEXED body keys are a player's: the un-indexed `M_Body`/`F_Body` on the human and
	// Society-of-Leopold templates name NPC models, which the map-driven seed already covers.
	FString PlayerBodyModel(int32 ClanIndex, bool bFemale, int32 ArmorSlot = 0) const;
	FString PlayerBodyStem(int32 ClanIndex, bool bFemale, int32 ArmorSlot = 0) const;

private:
	TMap<FString, int32> ClanByName;
	TMap<FString, int32> NpcByName;
};

// ================================================================================================
// 6. histories000.txt — the History background traits
// ================================================================================================

struct FElysiumHistory
{
	FString InternalName;
	FString Name;
	FString Description;                // wraps across lines in the shipped file
	FString ShortDescription;
	FString ShortPenaltyDescription;
	FString CritterScope;               // `"Kindred()"`, `"ALL()"`, `"Kindred(Gangrel)"` — raw
	FString Effect;                     // a `TraitEffectGroup` InternalName, or empty
	int32 Index = INDEX_NONE;           // the index the save's m_iVHistoryID holds

	bool IsValid() const { return !InternalName.IsEmpty(); }
};

struct FElysiumHistoryTable
{
	TArray<FElysiumHistory> Rows;

	bool Load(FString& OutError);
	bool IsValid() const { return !Rows.IsEmpty(); }

	const FElysiumHistory* Find(const FString& InternalName) const;
	const FElysiumHistory* At(int32 Index) const;
	int32 Num() const { return Rows.Num(); }

private:
	TMap<FString, int32> ByName;
};

// ================================================================================================
// 7. the five quests_*.txt — the quest catalogue
// ================================================================================================

struct FElysiumQuestState
{
	// The authored `"ID"`. DECORATIVE: `QuestJournal::AddCompletionState` never reads the key —
	// `SetQuest(title, N)` addresses the N-th state in FILE ORDER (`docs/vtmb/game_runtime.md` → "Quests").
	// Kept because every shipped row authors it equal to its own position, which is worth asserting.
	int32 Id = 0;
	FString Description;        // the journal body
	FString Type;               // success | failure | incomplete — drives the entry's colour
	FString AwardXp;            // an experience_table.txt KEY, not a number
	int32 AwardMoney = 0;       // authored in zero shipped rows; the schema is real
	FString Event;              // script data handed to the interpreter; zero shipped rows

	bool IsValid() const { return Id != 0 || !Description.IsEmpty(); }
};

struct FElysiumQuest
{
	FString Title;              // the key dialogue and scripts use: pc.SetQuest("Arthur Knox", 2)
	FString DisplayName;        // the journal heading
	int32 TableIndex = INDEX_NONE;  // which quests_* file — the save stores this
	int32 Index = INDEX_NONE;       // position within that file
	TArray<FElysiumQuestState> States;

	// State 0 is "unassigned" and is never authored, so a miss is the ordinary case.
	const FElysiumQuestState* StateById(int32 Id) const;

	// What `SetQuest(title, N)` actually addresses: the N-th state in file order, 1-based. This is
	// the engine's own lookup; `StateById` is the readable one the verb prints. VtMB caps a quest at
	// 20 states, so anything past that could not have been loaded either.
	static constexpr int32 MaxStates = 20;
	const FElysiumQuestState* StateByOrdinal(int32 OneBased) const;

	bool IsValid() const { return !Title.IsEmpty(); }
};

// A quest's address as the save holds it: which table, which quest within it.
struct FElysiumQuestRef
{
	int32 Table = INDEX_NONE;
	int32 Quest = INDEX_NONE;
	bool IsValid() const { return Table != INDEX_NONE && Quest != INDEX_NONE; }
};

struct FElysiumQuestTables
{
	// Fixed order, because the save stores the table index. Alphabetical by hub:
	// chinatown, downtown, hollywood, main, santamonica.
	static const TCHAR* HubNames[5];
	static constexpr int32 NumTables = 5;

	// `quests_main.txt` — the cross-hub table. It is not a place, so it has no tab of its own in the
	// quest log; its rows show under whichever hub is selected.
	static constexpr int32 MainTable = 3;

	// The four tables that ARE hubs, in tab order as the quest log lists them: Santa Monica,
	// Downtown, Hollywood, Chinatown. Alphabetical `HubNames` order is not tab order.
	static constexpr int32 HubTabOrder[4] = { 4, 1, 2, 0 };

	TArray<FElysiumQuest> Quests[NumTables];

	bool Load(FString& OutError);
	bool IsValid() const { return NumQuests() > 0; }

	// Rebuild the title index from `Quests`. `Load` calls it; a hand-built catalogue (the tests')
	// needs it because the lookup is the index, not a scan.
	void Reindex();

	// Case-insensitive, and trimmed on both sides — the engine `Q_trimspace`s a Title at load and
	// matches a journal row with `Q_strnicmp`.
	const FElysiumQuest* Find(const FString& Title, FElysiumQuestRef* OutRef = nullptr) const;
	const FElysiumQuest* At(const FElysiumQuestRef& Ref) const;
	int32 NumQuests() const;
	int32 NumStates() const;

private:
	TMap<FString, FElysiumQuestRef> ByTitle;
};

// ================================================================================================
// 8. experience_table.txt — the quest-reward -> XP map  (NOT KeyValues)
// ================================================================================================

// Pipe-delimited: `key | description | value`. `>` starts a comment line, a line under three
// characters is skipped, and a row splits into exactly three fields.
struct FElysiumExperienceEntry
{
	FString Key;
	FString Description;   // the only localized field of the three
	int32 Value = 0;       // stored RAW: floor(v/100) and the sub-100 residue are the awarder's job
};

struct FElysiumExperienceTable
{
	TArray<FElysiumExperienceEntry> Rows;

	bool Load(FString& OutError);
	bool IsValid() const { return !Rows.IsEmpty(); }

	// The engine compares keys with Q_strnicmp over the stored key's length — a prefix match. The
	// shipped table has no pair where one key prefixes another, so an exact case-insensitive
	// lookup reproduces it exactly; `Elysium.Content.Rulebook` asserts that premise still holds.
	const FElysiumExperienceEntry* Find(const FString& Key) const;
	int32 Num() const { return Rows.Num(); }

	// The one row that has a prefix collision, if the data ever grows one. Empty when clean.
	void FindPrefixCollisions(TArray<TPair<FString, FString>>& Out) const;

	// Parse one line. Returns false for a comment, a blank, or a line the loader skips.
	static bool ParseRow(const FString& Line, FElysiumExperienceEntry& Out);

private:
	TMap<FString, int32> ByKey;
};

// ================================================================================================
// 9. levelingtemplate_000.txt — the ordered auto-level templates
// ================================================================================================

// One `Level { "<Trait>" "<Value>" }` — buy this trait up to this value, then move on.
struct FElysiumLevelStep
{
	FString Trait;
	int32 Value = 0;
};

// A run of steps and the gate on running them. A bare `Level` run at template scope becomes an
// ungated group, so one ordered list carries the template's whole spend order.
struct FElysiumLevelGroup
{
	bool bGated = false;
	TArray<FString> Dependencies;   // `"<Trait> <op> <Value>"`, raw
	TArray<FElysiumLevelStep> Steps;
};

struct FElysiumLevelingTemplate
{
	FString InternalName;
	FString Name;
	FString ClanDependency;
	TArray<FString> Dependencies;
	TArray<FElysiumLevelGroup> Groups;
	int32 Index = INDEX_NONE;

	bool IsCharGen() const { return InternalName.EndsWith(TEXT("_CharGen")); }
	int32 NumSteps() const;
};

struct FElysiumLevelingTemplates
{
	TArray<FElysiumLevelingTemplate> Templates;

	bool Load(FString& OutError);
	bool IsValid() const { return !Templates.IsEmpty(); }

	const FElysiumLevelingTemplate* Find(const FString& InternalName) const;
	int32 Num() const { return Templates.Num(); }
	int32 NumSteps() const;

private:
	TMap<FString, int32> ByName;
};

// ================================================================================================
// 12. dicerolls.txt — the d10 weighting tables and the wound penalty
// ================================================================================================

// One `Rules/TableWeightings` child. The engine draws `RandomInt(0, 99)` and reads `Faces[draw]`,
// so the table IS the die rather than a bias applied to one: it is the whole mapping from the raw
// draw to a face `0..9`, a physical d10 showing `1..10`.
//
// The three shipped tables (`Normal`, `Heavy`, `Light`) are each a plain uniform d10
// (`face = draw / 10`), which is what makes the uniform fallback below faithful rather than
// approximate. The mechanism is real all the same — the file's own comment invites a mod to
// reweight a die — so the resolver reads this instead of assuming uniformity.
struct FElysiumDiceTable
{
	static constexpr int32 NumEntries = 100;   // the raw draw's domain, [0, 99]
	static constexpr int32 NumFaces = 10;

	// The block key the weighting is selected by (`Normal`, `Heavy`, `Light`), lowercased by the KV
	// reader as every block key is; `Name` is the block's own authored display string.
	FString InternalName;
	FString Name;
	int32 Faces[NumEntries];

	FElysiumDiceTable();    // constructs the uniform d10

	// An ABSENT entry reads as face 1, not 0 — the loader's own `GetInt(key, 1)` default
	// (`FUN_101d90c0`), which matters only for a partially authored mod table.
	void Load(const ElysiumKeyValues::FKvNode& Node);

	// `Faces[Draw]`. The INDEX is clamped into range; the value is whatever was authored, because a
	// face outside 0..9 would be a modder's statement rather than a parse error.
	int32 Face(int32 Draw) const;
	bool IsUniform() const;

	// What a resolver rolls on when `dicerolls.txt` is absent. Failing open to uniform is faithful,
	// not a guess: every shipped table is this table.
	static const FElysiumDiceTable& Uniform();
};

struct FElysiumDiceTables
{
	// File order. Index 0 is the engine's own fallback for a weighting name that matches nothing.
	TArray<FElysiumDiceTable> Tables;

	// `Rules/HealthModifiers` — health level (0..7) -> the dice a wounded character loses off a
	// pool, which is the roll struct's `[0xe]`. Every shipped entry is 0, so the penalty is a no-op
	// today; it is loaded rather than assumed so a patch that authors one applies.
	//
	// WHICH level a character sits at is a consumer's question, so this is the lookup and not the
	// answer — `ElysiumDice::Roll` takes the resolved number. Nothing reads it yet.
	TArray<int32> HealthModifiers;

	bool Load(FString& OutError);
	bool IsValid() const { return !Tables.IsEmpty(); }

	// Rebuild the name index from `Tables`. `Load` calls it; a hand-built set (the tests') needs it
	// because the lookup IS the index, not a scan — the rule `FElysiumQuestTables` established.
	void Reindex();

	// Case-insensitive. A miss falls back to index 0, as the engine's own selection does, and to the
	// uniform table when nothing loaded at all.
	const FElysiumDiceTable& Find(const FString& InternalName) const;
	const FElysiumDiceTable& At(int32 Index) const;
	int32 Num() const { return Tables.Num(); }

	// The weighting a feat rolls on, PC or NPC side. `FElysiumFeat` stores the authored NAME and the
	// join lives here, so the two files stay independently loadable and a feat read costs nothing
	// when no roll is involved. All 23 shipped feats name `Normal` on both sides.
	const FElysiumDiceTable& ForFeat(const FElysiumFeat& Feat, bool bNpc) const;

	// The pool penalty for a health level; 0 for an unauthored level or an unloaded table.
	int32 HealthModifier(int32 HealthLevel) const;

private:
	TMap<FString, int32> ByName;
};

// ================================================================================================
// 13. charcreatewizard.txt — the chargen personality quiz and its clan scoring
// ================================================================================================
//
// The wizard is a `client.dll` panel, so nothing here is a layout: `Region`/`TextRegion` are read
// as authored *intent* (which answer sits above which, how much room the question wanted), never as
// a runtime coordinate system (`docs/project/remaster-direction.md` axis 1). What is load-bearing is the graph —
// which popup follows which, which answer increments which abstract trait, and how the tallies
// score each clan. The model: `docs/vtmb/game_runtime.md` → "Chargen — a personality quiz".

// A rectangle exactly as authored, in the file's own 1024x768 terms.
struct FElysiumWizRegion
{
	int32 X = 0, Y = 0, Width = 0, Height = 0;
	bool bAuthored = false;
};

// One `Trait_Prereq`. Either bound may be absent, and absent means unbounded — a popup that only
// authors `MaxVal` admits every tally at or below it, including a trait never yet incremented.
struct FElysiumWizPrereq
{
	FString Trait;
	int32 MinVal = MIN_int32;
	int32 MaxVal = MAX_int32;

	bool Admits(int32 Tally) const { return Tally >= MinVal && Tally <= MaxVal; }
};

// A button, and what clicking it does. At most three are shown; the rest of a block's Actions are
// `KeyLookup` alternates, selected by the wizard rather than drawn alongside.
struct FElysiumWizAction
{
	FString Text;
	FString Next;              // the InternalName of the popup to go to
	FString Trait;             // the abstract trait this answer increments, or empty
	FString CharTemplate;      // sets the player's clan/template outright
	bool bSetGenderMale = false;
	bool bSetGenderFemale = false;
	bool bIsCheckBox = false;         // only triggers if still checked when the popup closes
	bool bEndCharGenWiz = false;
	bool bProcessTraitChoices = false;
	// **Absent is not zero.** `"KeyLookup" "0"` is a real alternate distinct from a plain action, so
	// the unauthored state needs its own value.
	int32 KeyLookup = INDEX_NONE;
	TArray<FElysiumWizPrereq> Prereqs;
	FElysiumWizRegion Region;
};

struct FElysiumWizPopup
{
	FString Text;
	FString InternalName;      // NOT unique — popups share one and the wizard picks among them
	FString CharTemplate;
	FString BkgImage;
	bool bOrderPrereqs = false;
	bool bClanPrereqs = false;
	TArray<FElysiumWizPrereq> Prereqs;
	TArray<FElysiumWizAction> Actions;
	FElysiumWizRegion Region;
	FElysiumWizRegion TextRegion;

	bool IsValid() const { return !InternalName.IsEmpty(); }
};

// When a group hands off to the next one, and on what answered-count.
struct FElysiumWizNextSection
{
	FString Next;
	int32 MinCount = INDEX_NONE;
	int32 MaxCount = INDEX_NONE;
};

// One of the ten `*_Popups` blocks. Its `Defaults` is a popup-shaped block every member inherits
// from field by field — including the positional `Action` list, which concrete popups override by
// index rather than replace.
struct FElysiumWizGroup
{
	FString InternalName;
	FElysiumWizNextSection NextSection;
	FElysiumWizPopup Defaults;
	TArray<FElysiumWizPopup> Popups;
};

// How traits group for the ordering questions.
struct FElysiumWizCombination
{
	FString InternalName;
	TArray<FString> Traits;
};

struct FElysiumWizOrderingStep
{
	FString TraitCombination;
	FString Index;             // "Primary" | "Secondary" | "Tertiary"
};

struct FElysiumWizTraitOrdering
{
	FString TraitCombination;
	FString Index;
	FString Trait;
	TArray<FElysiumWizPrereq> Prereqs;
	TArray<FElysiumWizOrderingStep> Orderings;
};

// A clan's ranked traits. **A rank repeats** — Gangrel authors two `Primary`s — so each is a list.
struct FElysiumWizClanNode
{
	FString CharTemplate;
	TArray<FString> Primary;
	TArray<FString> Secondary;
	TArray<FString> Tertiary;

	// The rank this clan gives a trait: 0 Primary, 1 Secondary, 2 Tertiary, INDEX_NONE if unranked.
	int32 RankOf(const FString& Trait) const;
};

struct FElysiumWizard
{
	TArray<FString> Traits;                     // the 8 abstract traits, in file order
	TArray<FElysiumWizCombination> Combinations;
	TArray<FElysiumWizTraitOrdering> Orderings;
	TArray<FElysiumWizClanNode> ClanNodes;
	// `ConnectionScores`: [selection 0..2][clan rank 0..2]. The payoff for the player's n-th chosen
	// trait meeting a clan that ranks it n-th.
	int32 ConnectionScores[3][3] = {};
	TArray<FElysiumWizGroup> Groups;
	TArray<FString> GroupNames;                 // Strings.PopUpGroups, the authored group order

	bool Load(FString& OutError);
	bool IsValid() const { return !Groups.IsEmpty(); }

	const FElysiumWizGroup* Group(const FString& InternalName) const;
	// Every popup with this InternalName, across all groups — the set the wizard picks from.
	void PopupsNamed(const FString& InternalName, TArray<const FElysiumWizPopup*>& Out) const;
	const FElysiumWizClanNode* ClanNode(const FString& CharTemplate) const;

	int32 NumPopups() const;

private:
	TMap<FString, int32> GroupByName;
};

// ================================================================================================
// 14. strings.txt + strings_internal.txt — the named string lists
// ================================================================================================

// `StringData.Strings` — a flat set of named groups, each a sparse `Name<N>` list. The file's own
// note says they "are referenced by the `NameMapping` fields in stats", which is what makes this a
// rulebook table and not UI text: a stat's value is DISPLAYED through its group, and a clan
// template's symbolic trait value (`"Attrib_Order" "Physical_Mental_Social"`) is READ back through
// the same group. `AttributeOrder`/`AbilityOrder` are what turn one into an index.
//
// Both files hang off the same root key and their group name spaces overlap by design — `ClanStrs`
// is authored in one and `ClanStrs_NO_LOCALIZE` in the other, and `strings.txt` itself authors
// `UIOccultStrs` twice. Groups therefore MERGE by index rather than replacing, so a second block
// extends the first and only overwrites the entries it names.
struct FElysiumStrings
{
	// Lowercased group name -> the dense `Name<N>` list. A gap in the authored indices (a commented
	// out `Name4`) is an empty entry, not a shortened list: the index is the value, so it must not
	// shift.
	TMap<FString, TArray<FString>> Groups;

	bool Load(FString& OutError);
	bool IsValid() const { return !Groups.IsEmpty(); }

	const TArray<FString>* Group(const FString& Name) const;
	// `Name<Index>` in the group, or Def when the group or the index is absent.
	FString At(const FString& Group, int32 Index, const FString& Def = FString()) const;
	// The index whose entry equals Value, case-insensitively, or INDEX_NONE. This is the direction
	// the clan templates and the leveling dependencies read.
	int32 IndexOf(const FString& Group, const FString& Value) const;

	int32 Num() const { return Groups.Num(); }
	int32 NumEntries() const;
};

// ================================================================================================
// 15. vdata/items/*.txt — the item definitions
// ================================================================================================
//
// The one authority on item POLICY (`docs/vtmb/inventory.md` §4). A filename prefix is a
// convention and not a type system, so nothing anywhere reads `item_w_`/`item_k_` to decide
// whether a thing stacks, drops or is a weapon — it is decided here or not at all.
//
// Unlike the twelve `system/` tables, this one is a whole DIRECTORY: one file per item, its
// basename being the entity classname the maps, the dialogue and the scripts all name.

// The eleven semantic item types `system/items.txt` declares, in that file's own order — the order
// is the engine's item-type enum (the file's own comment points at `vamp_data.h`). This is the
// compile-time mirror of the file, the way `EElysiumTraitOp` mirrors `traiteffect.txt`.
enum class EElysiumItemType : uint8
{
	WeaponMelee = 0,
	WeaponFirearm,
	WeaponThrown,
	Ammo,
	Armor,
	Money,
	Jewelry,
	Generic,
	Powerup,
	Bloodpack,
	Hidden,
	Count,
};

const TCHAR* ElysiumItemTypeName(EElysiumItemType Type);

// `item_type` is a SPACE-SEPARATED SET, not one word: `"weapon_firearm hidden"` and
// `"hidden hidden"` are both authored. The first token that names a type wins; a `hidden` token
// beyond that one sets OutHidden. Returns false when no token named a type.
bool ElysiumParseItemType(const FString& Raw, EElysiumItemType& OutType, bool& OutHidden);

// The authored `Type` of one weapon mode — the value `CWeaponRanged::ModeDispatch` branches on
// (`docs/vtmb/combat-and-damage.md` § "Input and firing modes"). This is a FIRE-MODE state machine,
// not a combo system: the record decides what a press does, and nothing derives it from a classname.
// An authored spelling outside this set stays readable as `TypeName` and resolves to `Other`.
enum class EElysiumWeaponModeType : uint8
{
	None = 0,
	Attack,              // `Attack` — the ordinary attack modes
	SecondaryAttack,     // `Secondary_Attack`
	TogglePrimaryMode,   // `Toggle_Primary_Mode` — swap primary modes 0/1
	ZoomLoop,            // `Zoom_Out_Loop` — cycle the scope range/state
	Other,               // a consumable/throw-style or otherwise unrecovered mode
};

const TCHAR* ElysiumWeaponModeTypeName(EElysiumWeaponModeType Type);

// One `Activation` block — a weapon MODE. A weapon record carries one per authored block, in file
// order, and the `Tag` (`Primary`, `PrimaryMode2`, `Secondary`) is how retail names them.
//
// `Dmg` is kept as the authored string here: the rulebook is the data layer, and turning the
// grammar into a descriptor is `ElysiumDamage::ParseDmg`'s job, which the weapon controller does
// once when the entity spawns.
struct FElysiumWeaponMode
{
	FString Tag;                        // `Tag`
	FString TypeName;                   // `Type`, verbatim
	EElysiumWeaponModeType Type = EElysiumWeaponModeType::None;

	FString Dmg;                        // `Dmg` — the authored damage grammar
	// `BaseLethality` and `SkillRequirement` are the two adjacent integers
	// (`combat-and-damage.md` § "Authored weapon inputs"). The second is loaded beside the first and
	// its runtime consumer is not recovered, so it is stored and inert.
	int32 BaseLethality = 0;
	int32 SkillRequirement = 0;

	// `Attack_Rate` — the ranged next-shot interval, in seconds. Melee recovery does NOT use it
	// (it is the selected clip's duration over its playback rate), but a melee record authors it and
	// the dry-fire path advances by it, so it is loaded for every mode.
	float AttackRate = 0.0f;

	FString AmmoType;                   // `Ammo_Type`
	// The two counts retail keeps DISTINCT: rounds spent per scheduled shot, and rays/pellets placed
	// in the fire packet for each of those shots. The M37 spends one shell and emits eight rays.
	int32 AmmoCost = 0;                 // `Ammo_Cost`
	int32 AmmoFired = 1;                // `Ammo_Fired`; unauthored means one ray

	// `allow_autofire` — clear means held attack intent is lost after the press edge.
	bool bAllowAutofire = false;

	// `BurstMin`/`BurstMax`, loaded with the loader's own `BurstMin <= BurstMax` clamp. No player or
	// NPC consumer is recovered, so they are stored and inert.
	int32 BurstMin = 0;
	int32 BurstMax = 0;

	float Range = 0.0f;                 // `Range`
	FString BotchTable;                 // `Botch_Table`

	bool IsAttack() const
	{
		return Type == EElysiumWeaponModeType::Attack || Type == EElysiumWeaponModeType::SecondaryAttack;
	}
};

// One `vdata/items/<classname>.txt` — the `WeaponData` block every one of them hangs off, reduced
// to what the inventory runtime and the economy read. The file carries far more (crosshair bloom,
// muzzle particles, botch tables, sprite atlases); those belong to the systems that own them and
// are deliberately not mirrored here.
struct FElysiumItemDef
{
	FString Classname;          // the file's basename — the entity classname
	FString PrintName;          // `printname` — the display name
	FString Description;

	EElysiumItemType Type = EElysiumItemType::Generic;
	bool bHidden = false;       // the second `hidden` token beside the type

	// --- Policy. Three DISTINCT keys; none is a synonym for another (`inventory.md` §4) --------
	bool  bStackable = false;           // `is_stackable` — selects quantity behaviour / m_iItemCount
	int32 StackLimit = 0;               // `stack_limit`; 0 = the file authored none
	// `is_droppable` — read by the ordinary player drop predicate. 78 of the 244 files author it;
	// the engine's own default for the other 166 was not recovered, so `true` is this runtime's
	// choice and is stated here rather than implied.
	bool  bDroppable = true;
	bool  bPermanentInventory = false;  // `permanent_inventory` — longer-lived storage policy

	bool  bWieldable = false;           // `is_wieldable`
	bool  bVisibleInHud = true;         // `is_visible_in_hud`

	int32 Worth = 0;                    // `item_worth`
	int32 PlayerSell = 0;               // `player_sell` — the vendor half is 9.10's
	int32 Weight = 0;                   // `weight`
	int32 ItemFlags = 0;                // `item_flags`

	// `camera_class`, parsed to its bits. The equipped item's value is what decides whether drawing
	// this weapon changes the view, and it lives on the item record rather than on the player
	// (`docs/vtmb/camera-view-modes.md` §2). 0 for `noswitch`, an unrecognized literal and an absent
	// key alike — all three reach the same early-out.
	int32 CameraClass = 0;

	// --- The `Magazine` child block — a firearm's ammunition -----------------------------------
	FString AmmoType;                   // `Type`, e.g. `ThirtyeightRound`; empty = carries no magazine
	int32 MagazineSize = 0;             // `Size` — the loaded-magazine capacity
	int32 DefaultAmmo = 0;              // `Default_Size` — what a fresh item spawns loaded with
	int32 DroppedAmmo = 0;              // `Dropped_Ammo`
	float ReloadTime = 0.0f;            // `ReloadTime`

	// `reload_single` — a WeaponData-level key (the patch-first M37, .38 and flaming crossbow author
	// it). One round per reload cycle, re-entering until interrupted, full or out of reserve.
	bool bReloadSingle = false;

	// `Disallow_FirearmsToBashing` — the record read by the Kindred lethal->bashing conversion in
	// `ElysiumDamage::Apply` step 2. No shipped `vdata/items` record authors it; the one shipped
	// author is an NPC template, so this join is the one `combat-and-damage.md` states (the
	// attacker's active weapon record) and the observed authoring disagrees with it. Reading the
	// key here keeps the documented join and changes no shipped behaviour.
	bool bDisallowFirearmsToBashing = false;

	// --- The `Activation` blocks — the weapon modes ---------------------------------------------
	TArray<FElysiumWeaponMode> Modes;

	// --- Models --------------------------------------------------------------------------------
	FString PlayerModel;                // `playermodel` — the loose world (ground) model
	FString ViewModel;
	FString InfoModel;

	bool IsValid() const { return !Classname.IsEmpty(); }
	// The three wielded weapon families — the ones `FElysiumWeapon` controls. Policy from the parsed
	// record, never from the classname prefix.
	bool IsControllableWeapon() const
	{
		return Type == EElysiumItemType::WeaponMelee
			|| Type == EElysiumItemType::WeaponFirearm
			|| Type == EElysiumItemType::WeaponThrown;
	}
	// The first mode carrying `Tag`, case-insensitively, or null.
	const FElysiumWeaponMode* FindMode(const TCHAR* Tag) const;
	// The `system/items.txt` `IsWeapon` column, mirrored: the wielded families plus Bloodpack.
	bool IsWeaponType() const;
	// Whether an ordinary stack may still take one more. A `StackLimit` of 0 is unauthored, which
	// is not a limit of zero.
	bool StackHasRoom(int32 Count) const { return StackLimit <= 0 || Count < StackLimit; }
};

struct FElysiumItemTable
{
	TArray<FElysiumItemDef> Items;   // filename order

	bool Load(FString& OutError);
	bool IsValid() const { return !Items.IsEmpty(); }

	// Rebuild the classname index from `Items`. `Load` calls it; a hand-built table (the tests')
	// needs it because the lookup IS the index, not a scan — the rule `FElysiumQuestTables` set.
	void Reindex();

	// Case-insensitive, as every inventory lookup in the game is.
	const FElysiumItemDef* Find(const FString& Classname) const;
	const FElysiumItemDef* At(int32 Index) const;
	int32 Num() const { return Items.Num(); }
	int32 CountOfType(EElysiumItemType Type) const;

	// Parse one file's text into a definition. Public because it is the piece the tests drive
	// directly, and because the directory walk is the only other thing `Load` does.
	static bool ParseText(const FString& Classname, const FString& Text, FElysiumItemDef& Out,
		FString& OutError);

private:
	TMap<FString, int32> ByName;
};

// ================================================================================================
// 16. system/sound_volume_table.txt — the game-sound levels and their occlusion policy
// ================================================================================================
//
// The authored half of NPC hearing (`docs/vtmb/npc-ai-reverse-engineering.md` → "Visual and
// auditory input"). Four blocks, read as one table:
//
//   * `VolumeLevels`         — level index → the radius an average human hears it at.
//   * `OccludedVolumeLevels` — the same indices → 1 when world geometry can occlude the level.
//   * `SoundTypes`           — the named category a producer emits (`PLAYER_GUNSHOT_PISTOL`,
//                              `NPC_TAKE_DAMAGE`, `DOOR_NORMAL`) → the level it carries.
//   * `MiscData`             — loose tuning tags; the shipped file authors `PLAYER_RUN_SPEED` and
//                              comments the other two out, so an absent tag is the ordinary case.
//
// **Radii stay in the authored Source game units.** This is the data layer; the one conversion to
// centimetres happens at the consumer edge through `ElysiumMove::U`, like every other recovered
// distance in the runtime.

// One `VolumeLevels` row joined to its `OccludedVolumeLevels` answer.
struct FElysiumSoundLevel
{
	int32 Level = INDEX_NONE;    // the authored level index — the value a `SoundTypes` row names
	float RadiusUnits = 0.f;     // Source game units
	bool  bOccludable = false;   // 0 = world geometry cannot stop it (the loud/level-3 family)

	bool IsValid() const { return Level != INDEX_NONE; }
};

// One `SoundTypes` row. `Name` is the FOLDED spelling: the KV reader lowercases every block key,
// which is also how every lookup here is keyed.
struct FElysiumSoundCategory
{
	FString Name;
	int32 Level = INDEX_NONE;
};

struct FElysiumSoundVolumeTable
{
	// The semantic levels the file's own comments name. A producer emits a CATEGORY and never a
	// level; these exist for the two places that need the level itself — the unknown-category
	// default and the absent-file fallback.
	static constexpr int32 SilentLevel        = 0;
	static constexpr int32 QuietLevel         = 1;
	static constexpr int32 NormalLevel        = 2;
	static constexpr int32 LoudLevel          = 3;
	static constexpr int32 PlayerStealthLevel = 4;
	static constexpr int32 FeedingLevel       = 5;

	TArray<FElysiumSoundLevel> Levels;      // file order, which is also level order
	TArray<FElysiumSoundCategory> Categories;   // file order
	TMap<FString, FString> Misc;            // `MiscData`, folded tag -> raw value

	bool Load(FString& OutError);
	bool IsValid() const { return !Levels.IsEmpty() && !Categories.IsEmpty(); }

	// Rebuild both indices from `Levels`/`Categories`. `Load` calls it; a hand-built table (the
	// tests') needs it because the lookup IS the index, not a scan — the rule `FElysiumQuestTables`
	// established.
	void Reindex();

	const FElysiumSoundLevel* Level(int32 Index) const;
	// The level a named category carries, or null when the table names no such category — which is
	// the caller's cue to fall back and say so. Case-insensitive, as every KV lookup in the game is.
	const FElysiumSoundLevel* FindCategory(const FString& Category) const;
	int32 NumLevels() const { return Levels.Num(); }
	int32 NumCategories() const { return Categories.Num(); }

	float MiscFloat(const TCHAR* Tag, float Def) const;

	// What a resolver uses when `sound_volume_table.txt` is absent entirely: LEVEL_2 exactly as the
	// shipped file authors it. Failing open to the normal level is faithful rather than a guess —
	// it is the same level an unknown category already resolves to — and it is the shape
	// `FElysiumDiceTable::Uniform()` established for a missing export.
	static const FElysiumSoundLevel& NormalFallback();

private:
	TMap<int32, int32> LevelByIndex;
	TMap<FString, int32> CategoryByName;
};

// ================================================================================================
// 17. system/reaction.txt + reactions000.txt — the RPG/social reaction score and its modifiers
// ================================================================================================
//
// The third social domain (`docs/architecture/gameplay-systems-architecture.md` §5.5.7, K4): a
// score separate from the native combat-relationship table (`D_HT`/`D_FR`/`D_LI`/`D_NU`, read
// through `FElysiumNpc`'s relationship rows) and from `DispositionTable.txt`'s emotional/
// presentation state (`FElysiumDispositionTable`, above). Nothing here derives from, or is derived
// by, either of the other two — a band label like `Hatred` names a point on THIS scale, not a
// `D_HT` row (`docs/vtmb/npc-ai-reverse-engineering.md` → "Emotional disposition and social
// reaction are different domains").
//
// `reaction.txt`'s `General.Table` ("Reaction Ranges") is the seven-row band-boundary list, joined
// BY INDEX to `Strings.ReactionLevel`'s `Name<N>` labels — row N's boundary is band N's lower bound
// (`"0" "0"`, `"1" "20"`, … `"6" "9999"` -> Want To Kill, Hatred, Dislike, Neutral Reaction, Admire,
// Love, Obsession). The table's own `"Clamping" "1"` is read verbatim: the calculator in
// `Substrate/ElysiumReaction.h` clamps the running score into
// `[Bands[0].LowerBoundary, Bands.Last().LowerBoundary]` before resolving it, the natural reading
// of a range table that declares itself clamped.
//
// `reactions000.txt`'s `ReactionCategory -> ReactionGroup -> Reaction` nesting is the same shape
// `FElysiumTraitEffects` already reads for `traiteffects000.txt` (Category/Group/Effect). Each
// `Reaction` row carries an optional `Targets` scope (`Kindred(!Gangrel)`, `Kine()`, bare `ALL`, or
// no key at all), a `WhoModifies` token (every shipped row authors `Others`), and a `Modifier`
// string that is either a scalar op (`+20`, `-5`, `*2`) or a free-form expression naming the
// running score (`"Reaction + ((Reaction - 50)*2)"`, Dementation's Passion). The calculator in
// `Substrate/ElysiumReaction.{h,cpp}` is the sole consumer of both tables; this layer only parses
// what the two files carry.

// One `General.Table` row joined to its `Strings.ReactionLevel` label.
struct FElysiumReactionBand
{
	int32 LowerBoundary = 0;
	FString Label;

	bool IsValid() const { return !Label.IsEmpty(); }
};

struct FElysiumReactionBandTable
{
	TArray<FElysiumReactionBand> Bands;   // ascending, `General.Table`'s own row order (0..6)
	bool bClamping = false;               // `General.Table`'s `"Clamping"` key

	bool Load(FString& OutError);
	bool IsValid() const { return !Bands.IsEmpty(); }

	// The band whose LowerBoundary is the greatest one <= Score. Null only when the table itself is
	// empty — a loaded table always covers every Score at or above its first (0) boundary.
	const FElysiumReactionBand* Resolve(int32 Score) const;

	int32 MinBoundary() const { return Bands.IsEmpty() ? 0 : Bands[0].LowerBoundary; }
	int32 MaxBoundary() const { return Bands.IsEmpty() ? 0 : Bands.Last().LowerBoundary; }
};

// `Targets`'s scope token. `reaction.txt`'s own (fully commented-out) `General.CritterScopes`
// legend names the same three values.
enum class EElysiumReactionTargetScope : uint8
{
	Kindred,
	Kine,
	All,      // bare `ALL`, or the row authored no `Targets` key at all
};

// A parsed `Targets` string: `Kindred(!Gangrel)` -> {Kindred, ["Gangrel"]}; `Kine()` -> {Kine, []};
// bare `ALL` -> {All, []}.
struct FElysiumReactionTargets
{
	EElysiumReactionTargetScope Scope = EElysiumReactionTargetScope::All;
	TArray<FName> Exclusions;   // the `(!X,!Y)` names, e.g. `Kindred(!Gangrel)` -> ["Gangrel"]

	// False when the row authored no `Targets` key at all (`Presence-Awe`/`Presence-General` ship
	// this way). CHOSEN, NOT RECOVERED: read as unconditional — same as Scope::All — rather than as
	// "never applies", since a bonus that could never fire would make the authored row meaningless.
	// See `ElysiumReaction.cpp`.
	bool bAuthored = false;

	// Whether a reacting character with these facts falls inside this scope.
	bool Matches(bool bReactorIsKindred, bool bReactorIsKine, FName ReactorClan) const;
};

// Parses a `Targets` string. Public because it is the piece `Elysium.Substrate.Reaction` drives
// directly, the rule `FElysiumTraitEffects::ParseModifier` established.
void ElysiumParseReactionTargets(const FString& Raw, FElysiumReactionTargets& Out);

// What decides whether a modifier applies — the character/history/Discipline fact its owning
// `ReactionGroup`'s `InternalName` names. The six below are every group `reactions000.txt` ships;
// an InternalName outside this set parses as `Unrecognized` and the row is carried-but-inert.
enum class EElysiumReactionCondition : uint8
{
	Megalomaniac,          // "Reaction (Megalomaniac)"
	CloseToTheBeast,       // "Reaction (Close to the Beast)"
	OccultNut,             // "Reaction (Occult Nut)"
	DementationPassion,    // "Reaction (Dementation-Passion)"
	PresenceAwe,           // "Reaction (Presence-Awe)"
	PresenceGeneral,       // "Reaction (Presence-General)"
	Unrecognized,
};

EElysiumReactionCondition ElysiumParseReactionCondition(const FString& GroupInternalName);

// The parsed shape of a `Modifier` string.
enum class EElysiumReactionModifierKind : uint8
{
	Add,            // leading `+`/`-` and a plain number: `"+20"`, `"-5"`
	Multiply,       // leading `*` and a plain number: `"*2"`
	Formula,        // a free-form expression naming `Reaction`: `"Reaction + ((Reaction - 50)*2)"`
	Unrecognized,   // carried-but-inert — no shipped row takes this today
};

// Parses a `Modifier` string. Public for the same reason `ElysiumParseReactionTargets` is.
void ElysiumParseReactionModifierExpr(const FString& Raw, EElysiumReactionModifierKind& OutKind,
	double& OutScalar, FString& OutFormula);

// One `reactions000.txt` `Reaction` row.
struct FElysiumReactionModifier
{
	FString CategoryInternalName;   // the owning `ReactionCategory`'s `InternalName` ("History", "Discipline")
	FString GroupInternalName;      // the owning `ReactionGroup`'s `InternalName`
	EElysiumReactionCondition Condition = EElysiumReactionCondition::Unrecognized;

	FElysiumReactionTargets Targets;
	FString WhoModifies;             // raw token; every shipped row authors `Others`

	FString RawModifier;             // exactly as authored, for diagnosis
	EElysiumReactionModifierKind Kind = EElysiumReactionModifierKind::Unrecognized;
	double ScalarValue = 0.0;        // Add: the signed delta. Multiply: the factor.
	FString FormulaExpression;       // Kind == Formula: the raw expression text, evaluated by
	                                  // `ElysiumReaction::TryEvaluateFormula`

	// Recognized and applicable in principle: a named condition, a parseable Modifier shape, and the
	// one shipped `WhoModifies` value. `ElysiumReaction::Compute` skips a row this returns false for;
	// `FElysiumReactionModifierTable::Load` logs the inert set once.
	bool IsRecognized() const;
};

struct FElysiumReactionModifierTable
{
	TArray<FElysiumReactionModifier> Modifiers;   // file order: category, then group, then row

	// Diagnostic only — `GroupInternalName` plus a short reason, one entry per row `IsRecognized()`
	// returns false for. `Load` fills it; the subsystem's `Reactions()` accessor logs it once on the
	// first load.
	TArray<FString> InertModifiers;

	bool Load(FString& OutError);
	bool IsValid() const { return !Modifiers.IsEmpty(); }
	int32 Num() const { return Modifiers.Num(); }
};

// The pair, bundled together — `ElysiumReaction::Compute` needs both. One lazy `Reactions()`
// accessor exposes them, the shape `SoundVolumes()` established for a single-file table and
// `TraitEffects()` established for a two-file one (`traiteffect.txt` + `traiteffects000.txt`).
struct FElysiumReactionCatalogue
{
	FElysiumReactionBandTable Bands;          // reaction.txt
	FElysiumReactionModifierTable Modifiers;  // reactions000.txt

	// Bands are load-bearing — there is nothing to resolve a score to without them. Modifiers are
	// supplementary: an unmodified base score still resolves to a band. The catalogue counts as
	// loaded once Bands alone loads, mirroring how `FElysiumSoundVolumeTable` fails open on a
	// missing file.
	bool Load(FString& OutError);
	bool IsValid() const { return Bands.IsValid(); }
};

// ================================================================================================
// 14. disciplinetgt_000..004.txt — the targeted Discipline records
// ================================================================================================
//
// `docs/vtmb/disciplines.md` → "Targeted `DisciplineTgt` path" owns the behaviour. The five files
// are Animalism, Dementation, Dominate, Presence and Thaumaturgy in that order; each holds one
// `DisciplineTgtList` of `DisciplineTgt` blocks. A block's named sub-blocks that are not one of the
// fixed keys below are **hit tables** — the names are authored freely (`Hit_Human`,
// `Hit_Strata_2_Berserk`, …) and an `Affects_Table` mapping names one of them by `HitTable`.

// One `Affects_Filters` row. The vocabulary is the comment block at the head of
// `disciplinetgt_000.txt` plus the two data-driven rows (`CharTemplate`, `DisciplineStrata`) and
// the chance roll the mapping walk uses.
enum class EElysiumDiscFilter : uint8
{
	Unknown = 0,
	Critter,
	Human,
	Supernatural,
	NoSupernatural,
	Boss,
	NoBoss,
	Self,
	NoSelf,
	NoFriends,
	Invulnerable,
	NoInvulnerable,
	Player,
	PrimaryTarget,
	Combatant,
	NonCombatant,
	CharTemplate,        // matches the target's resolved `stattemplate` name
	DisciplineStrata,    // matches the template's authored `DisciplineStrata`
	Chance,              // a percentage roll, not a property of the target
};

const TCHAR* ElysiumDiscFilterName(EElysiumDiscFilter Filter);

struct FElysiumDiscFilterRow
{
	EElysiumDiscFilter Kind = EElysiumDiscFilter::Unknown;
	FString RawKey;          // exactly as authored, for diagnosis
	FString Text;            // `CharTemplate`'s template name
	int32 Number = 0;        // `DisciplineStrata`'s strata, `Chance`'s percent, else the 0/1 flag
	bool bEnabled = true;    // the authored `"1"`/`"0"` value; a `"0"` row asserts nothing
};

struct FElysiumDiscFilterSet
{
	TArray<FElysiumDiscFilterRow> Rows;
	bool IsEmpty() const { return Rows.IsEmpty(); }
};

// One `Mapping` / `Default_Mapping` row inside an `Affects_Table`.
struct FElysiumDiscMapping
{
	int32 ChancePercent = INDEX_NONE;   // absent = unconditional
	int32 Count = INDEX_NONE;           // how many of the resolved set this mapping claims
	FString HitTable;
};

struct FElysiumDiscAffectsTable
{
	FElysiumDiscFilterSet Filters;
	TArray<FElysiumDiscMapping> Mappings;          // ordered, chance-gated
	TArray<FElysiumDiscMapping> DefaultMappings;   // the remainder catcher
};

// `AoE.Affects` — `Self` / `Target` / `Radius` / `Cone`.
enum class EElysiumDiscShape : uint8 { Self, Target, Radius, Cone };

struct FElysiumDiscAoE
{
	float Range = 0.f;                   // Source units
	bool bSourceIsTarget = false;        // `"Source" "Target"` centres the shape on the aim target
	EElysiumDiscShape Shape = EElysiumDiscShape::Target;
	float MinRadius = 0.f;
	float MaxRadius = 0.f;
	FElysiumDiscFilterSet Filters;       // admission, before any table is consulted
	TArray<FElysiumDiscAffectsTable> Tables;   // ordered; the first whose filters pass decides
};

// An authored numeric payload: `"30%"`, `"6-8%"`, `"7-10"`, `"3"`, `"-1"`.
struct FElysiumDiscAmount
{
	bool bAuthored = false;
	bool bPercent = false;
	int32 Min = 0;
	int32 Max = 0;

	bool Parse(const FString& Raw);
	// The literal value when the row is a single number; `Min` when it is a range.
	int32 Low() const { return Min; }
	bool IsRange() const { return Max != Min; }
};

// One `Trigger_Casting` — a nested cast into another record.
struct FElysiumDiscTriggerCast
{
	FString DisciplineFx;   // the nested record's InternalName
	FString Source;         // `Self` | `Target`
	FString Affects;        // `Self` | `Target`
};

// One `HitInfo` — the independent channels `docs/vtmb/disciplines.md` § "Hit execution" lists.
// Every authored channel is parsed and carried; which ones this runtime executes is
// `Substrate/ElysiumDisciplines.cpp`'s decision, stated there channel by channel.
struct FElysiumDiscHit
{
	FString Name;
	FString InheritFrom;

	FElysiumDiscAmount DmgHealth;        // `Dmg_Health` — percent of Max_Health, or a flat count
	FElysiumDiscAmount HealBlood;        // `Heal_Blood` — blood points onto the target
	FElysiumDiscAmount BloodSuck;        // `Blood_Suck`
	FElysiumDiscAmount HealthBuffer;     // `Health_Buffer` — `-1` clears it
	int32 HealthBufferBlockPercent = INDEX_NONE;
	FElysiumDiscAmount Duration;         // seconds; `-1` = infinite
	int32 ChanceEffectivePercent = 100;

	TArray<FString> TraitEffects;        // `TraitEffect { "Effect" "..." }`, repeatable

	FString AiSchedule;                  // `AI_Schedule`
	FString AiNpcFlag;                   // `AI_NPCFlag`
	FString Expression;
	FString GestureAnim;
	float GestureDuration = 0.f;
	FString PlayerAnim;                  // the compact player action
	FString MiscFlag;
	int32 FlinchPercent = INDEX_NONE;
	int32 KnockbackPercent = INDEX_NONE;
	int32 AddToComfort = INDEX_NONE;
	bool bDoFrenzy = false;
	bool bDoPossession = false;
	bool bGibOnDeath = false;
	bool bClearCopyProp = false;

	TArray<FElysiumDiscTriggerCast> TriggerCasting;

	// `OnEnd` / `OnInterrupt` are hit blocks in their own right — the channels that run when the
	// timed effect finishes or is broken. Held by value in a single-element array so the struct
	// stays self-contained without a forward-declared pointer.
	TArray<FElysiumDiscHit> OnEnd;
	TArray<FElysiumDiscHit> OnInterrupt;

	// Fold `Base` in under this row: a key this row does not author takes the base's.
	void InheritFromRow(const FElysiumDiscHit& Base);
};

struct FElysiumDisciplineTgt
{
	FString Name;
	FString InternalName;
	FString Discipline;                  // the learned Discipline's InternalName
	int32 Level = 0;

	int32 BloodCost = 0;
	bool bOvert = false;
	bool bTriggerAISound = false;
	int32 SupernaturalLvl = 0;
	float RecoveryTime = 0.f;

	// The three independent interruption flags.
	bool bRemoveOnTakeDamage = false;
	bool bRemoveOnHearCombat = false;
	bool bRemoveOnWasBumped = false;

	FString ViewModel;
	FString TargetHighlightParticle;

	bool bHasProjectile = false;
	float ProjectileSpeed = 0.f;
	FString ProjectileModel;
	FString ProjectileParticle;
	bool bProjectileDiesOnHit = false;

	FElysiumDiscAoE AoE;

	// Hit tables by folded name, in file order.
	TArray<FElysiumDiscHit> Hits;

	// A hit table by name, with its `InheritFrom` chain already folded in. Returns false when the
	// name is not a table on this record — which is an authored defect, not a silent miss.
	bool ResolveHit(const FString& HitTable, FElysiumDiscHit& Out) const;

	// A helper record: zero blood and zero recovery. `Trigger_Casting` nests into these, and they
	// are not powers the player selects.
	bool IsHelper() const { return BloodCost == 0 && RecoveryTime == 0.f; }
};

struct FElysiumDisciplineTargets
{
	// File order across `disciplinetgt_000..004.txt`, which is also the order the shared authority
	// searches: within one Discipline the main level records precede the helper records, so the
	// first match on (Discipline, Level) is the power the player cast.
	TArray<FElysiumDisciplineTgt> Records;

	bool Load(FString& OutError);
	bool IsValid() const { return !Records.IsEmpty(); }
	int32 Num() const { return Records.Num(); }

	const FElysiumDisciplineTgt* Find(const FString& InternalName) const;
	const FElysiumDisciplineTgt* FindFor(const FString& Discipline, int32 Level) const;

	// Append a record and index it by InternalName. `Load`'s own tail, exposed for the same reason
	// `FElysiumTraitEffects::Add` is: `Find` (and therefore `Trigger_Casting`) reads the index.
	void Add(FElysiumDisciplineTgt&& Record);

private:
	TMap<FString, int32> ByName;
};

// ================================================================================================
// 18. system/stealth.txt — the four StealthData tables
// ================================================================================================
//
// `docs/vtmb/stealth.md` -> "The rulebook" owns the behaviour. Four named sections under one
// `StealthData` root:
//
//   * `StealthVisionScalarTable`     — Light0..10 x Stealth0..10, multiplier on an observer's
//                                      effective visual range;
//   * `StealthVisionConeScalarTable` — the same shape, multiplier applied in the cone test;
//   * `StealthHearingDistTable`      — Stealth0..10, distance REMOVED from an eligible player
//                                      sound radius, in Source game units;
//   * `StealthLightRangeTable`       — Light0..10, the DESCENDING normalized-light thresholds the
//                                      light row is chosen against.
//
// Both matrices index row-major `light * 11 + Sneaking`; hearing indexes on `Sneaking` alone.
// A missing section or row is a developer diagnostic and the individual read retains the table's
// existing default, which is what the neutral construction below is for. Radii stay in the
// authored Source game units: the one conversion to centimetres happens at the consumer edge,
// like every other recovered distance in the runtime.

struct FElysiumStealthTables
{
	static constexpr int32 NumLight = 11;     // Light0..Light10
	static constexpr int32 NumStealth = 11;   // Stealth0..Stealth10

	// The neutral defaults an unauthored cell keeps: a scalar of 1.0 changes nothing, a hearing
	// reduction of 0 removes nothing, and a threshold of 0 puts every lit value in `Light0`.
	float VisionScalar[NumLight * NumStealth];
	float ConeScalar[NumLight * NumStealth];
	float HearingDistUnits[NumStealth];
	float LightThreshold[NumLight];

	FElysiumStealthTables();

	bool Load(FString& OutError);
	// Every section present, with all 11 rows and all 11 columns authored. A partial parse still
	// answers — each unauthored cell keeps its neutral default — but it is not a valid table.
	bool IsValid() const
	{
		return bVisionLoaded && bConeLoaded && bHearingLoaded && bThresholdsLoaded;
	}

	// Counted in authored VALUES rather than sections: a section that silently lost its tail is the
	// regression that matters, and four is not a number a status row can regress on.
	int32 NumAuthoredValues() const { return AuthoredValues; }

	// The four reads. Every index is clamped rather than checked: the row selector and the feat cap
	// already bound their inputs, and a clamp keeps a patched table that stops short from reading
	// off the end.
	float Vision(int32 Light, int32 Stealth) const;
	float Cone(int32 Light, int32 Stealth) const;
	float HearingUnits(int32 Stealth) const;
	float Threshold(int32 Light) const;

	// What a resolver uses when `stealth.txt` is absent entirely: the neutral table above. Failing
	// open to "stealth changes nothing" is the same posture `FElysiumSoundVolumeTable::
	// NormalFallback()` takes, and it is the correct answer for a character the tables cannot
	// describe.
	static const FElysiumStealthTables& Neutral();

private:
	bool bVisionLoaded = false;
	bool bConeLoaded = false;
	bool bHearingLoaded = false;
	bool bThresholdsLoaded = false;
	int32 AuthoredValues = 0;
};
