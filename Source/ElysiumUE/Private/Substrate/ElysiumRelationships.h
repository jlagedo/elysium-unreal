#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntityHandle.h"

struct FElysiumSaveArchive;

// CAI_BaseNPC's combat relationship value. This is intentionally not disposition-table emotion
// and not the RPG reaction score: K4 gives each domain its own store and writer.
enum class EElysiumRelationship : uint8
{
	Neutral,
	Hate,
	Fear,
	Like,
};

namespace ElysiumRelationships
{
	bool Parse(const FString& Token, EElysiumRelationship& Out);
	const TCHAR* LexToString(EElysiumRelationship Value);
}

struct FElysiumEntityRelationship
{
	FElysiumEntityHandle Target;
	EElysiumRelationship Value = EElysiumRelationship::Neutral;
	int32 Priority = 0;
};

struct FElysiumClassRelationship
{
	FString Classname;
	EElysiumRelationship Value = EElysiumRelationship::Neutral;
	int32 Priority = 0;
};

// A row this runtime DERIVED from a live stimulus rather than reading it off authored content, a
// script or dialogue — today only the damage memory (`ElysiumNpcEnemy::RememberAttacker`).
//
// Two properties separate it from the rows beside it, and both come from the stimulus rather than
// from the store: it carries an absolute expiry on the substrate clock, and it never enters a save.
// A derived row is a fact about the last few seconds of a fight; restoring one would hand a loaded
// game a hostility whose stimulus the player never produced.
struct FElysiumDerivedRelationship
{
	FElysiumEntityHandle Target;
	EElysiumRelationship Value = EElysiumRelationship::Neutral;
	int32 Priority = 0;
	// Absolute, on `FElysiumEntityWorld::NowSeconds()`. Expired when `Now >= ExpiresAt`, which is
	// the same comparison the law-record bus uses for its own expiring store.
	double ExpiresAt = 0.0;
};

// The state half of AddEntityRelationship/AddClassRelationship. An exact entity always outranks a
// class row. Rewriting the same target replaces it only at an equal-or-higher priority, which makes
// dialogue changes deterministic without letting a later low-priority seed erase a stronger rule.
//
// The store carries two surfaces, and the split is what keeps a lifetime-bound stimulus out of the
// save file. The PERSISTENT rows — the entity and class tables — are the authored, scripted and
// dialogue-written ones; they never expire, they are what `Serialize` carries, and their behaviour
// is untouched by anything below. The DERIVED rows are the expiring stand-ins described on
// `FElysiumDerivedRelationship`. Every resolve consults both, on the same equal-or-higher-priority
// rule `SetEntity` already replaces a row by: a derived row stands unless a persistent exact-entity
// row outranks it.
class FElysiumRelationships
{
public:
	bool SetEntity(const FElysiumEntityHandle& Target, EElysiumRelationship Value, int32 Priority);
	bool SetClass(const FString& Classname, EElysiumRelationship Value, int32 Priority);

	// Install or refresh the derived row toward `Target`, expiring at the absolute `ExpiresAt`.
	//
	// Refused — false, and nothing stored — when a persistent exact-entity row outranks it, which is
	// `SetEntity`'s own replacement rule applied across the two surfaces: an authored
	// `player_reaction D_LI 10` keeps its character friendly through a punch. A row already present
	// for the same target at the same or a lower priority is RE-STAMPED, so a repeated stimulus
	// renews the window rather than being swallowed.
	bool SetDerivedEntity(const FElysiumEntityHandle& Target, EElysiumRelationship Value,
		int32 Priority, double ExpiresAt);

	// Drop every derived row whose window closed at or before `Now`, and report how many went. The
	// store has no think of its own, so its owner runs this once per decision pass — expiry is a
	// clock fact and the substrate clock is the only clock.
	int32 ExpireDerived(double Now);

	// The live derived row toward `Target`, or null. Expiry is NOT applied here: this answers what
	// the store holds, and `ExpireDerived` is what makes the store agree with the clock.
	const FElysiumDerivedRelationship* FindDerived(const FElysiumEntityHandle& Target) const;

	EElysiumRelationship Resolve(const FElysiumEntityHandle& Target,
		const FString& Classname) const;

	// `IRelationPriority` (`0x10333700`) — the arbitration weight beside the relation.
	//
	// The exact entity row is consulted before the class row and its RAW integer is returned:
	// the diagnostic text describes a 1-10 range but the parser never clamps, and the installed
	// corpus writes zero and 99, so out-of-range values stay ordered rather than folded. A target
	// with no row at all answers 5 for a live actor; a null target answers 0.
	int32 ResolvePriority(const FElysiumEntityHandle& Target, const FString& Classname) const;

	// Both halves of one lookup, so an enemy arbitration pass does not walk the rows twice.
	// Returns false when no surface carries the target (the neutral / priority-5 default).
	bool ResolveRow(const FElysiumEntityHandle& Target, const FString& Classname,
		EElysiumRelationship& OutValue, int32& OutPriority) const;

	// The same lookup over the PERSISTENT rows alone — what the store held before any stimulus, and
	// what it will hold again once every window has closed. A writer of a derived row asks this to
	// tell an authored decision from its own earlier one.
	bool ResolvePersistentRow(const FElysiumEntityHandle& Target, const FString& Classname,
		EElysiumRelationship& OutValue, int32& OutPriority) const;

	// Persistent rows only: this answers "did an author, a script or dialogue already decide about
	// this target", which is a question about the saved table and not about a live stimulus.
	bool HasEntity(const FElysiumEntityHandle& Target) const;

	int32 NumEntityRules() const { return EntityRules.Num(); }
	int32 NumClassRules() const { return ClassRules.Num(); }
	int32 NumDerivedRules() const { return DerivedRules.Num(); }

	void Serialize(FElysiumSaveArchive& Ar);
	void Rebase(const class FElysiumEntityWorld& World);

private:
	const FElysiumEntityRelationship* FindEntityRow(const FElysiumEntityHandle& Target) const;
	const FElysiumClassRelationship* FindClassRow(const FString& Classname) const;

	TArray<FElysiumEntityRelationship> EntityRules;
	TArray<FElysiumClassRelationship> ClassRules;
	TArray<FElysiumDerivedRelationship> DerivedRules;
};
