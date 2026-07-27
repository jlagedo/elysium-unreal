#pragma once

#include "CoreMinimal.h"

#include "ElysiumSheetSlots.h"

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
//     stores those indices, not names (`savegame_format.md`). Nothing here sorts.
//   * **An empty container is legal, not an error.** `quests_main.txt` holds no quests,
//     `npctemplate019/021.txt` hold no templates, `rules.txt`'s `Tables` block is a stub.
//
// Nothing in `vdata/` is ever saved, so these tables are session-lifetime and re-read at load —
// which is what lets a patched rulebook re-apply to an existing save.
//
// The VtMB facts these model: `docs/game_runtime.md` §3. The per-file inventory:
// `docs/vdata-catalog.md`. `dispositiontable.txt` is not here — it is read by
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

	// Gates on raising the stat, e.g. `"BloodPool > 0"` and `"Health < Max_Health"`. Authored more
	// than once per block on 17 of the Active_Disciplines, which is why this is a list.
	TArray<FString> IncPredependency;

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

	bool IsValid() const { return !TemplateName.IsEmpty(); }
	bool IsPlayable() const { return TemplateName.StartsWith(TEXT("Player_")); }

	bool    HasGeneral(const TCHAR* Key) const;
	FString GeneralStr(const TCHAR* Key, const FString& Def = FString()) const;
	int32   GeneralInt(const TCHAR* Key, int32 Def = 0) const;

	// A trait's rating in this template alone, ignoring the parent chain.
	const int32* Trait(const FString& InternalName) const;
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
	// `SetQuest(title, N)` addresses the N-th state in FILE ORDER (`game_runtime.md` → "Quests").
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
