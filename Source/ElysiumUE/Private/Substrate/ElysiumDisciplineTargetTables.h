#pragma once

#include "CoreMinimal.h"

// ================================================================================================
// 19. disciplinetgt_000..004.txt — the targeted Discipline records
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
