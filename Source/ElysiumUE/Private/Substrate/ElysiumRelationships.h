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

// The state half of AddEntityRelationship/AddClassRelationship. An exact entity always outranks a
// class row. Rewriting the same target replaces it only at an equal-or-higher priority, which makes
// dialogue changes deterministic without letting a later low-priority seed erase a stronger rule.
class FElysiumRelationships
{
public:
	bool SetEntity(const FElysiumEntityHandle& Target, EElysiumRelationship Value, int32 Priority);
	bool SetClass(const FString& Classname, EElysiumRelationship Value, int32 Priority);

	EElysiumRelationship Resolve(const FElysiumEntityHandle& Target,
		const FString& Classname) const;
	bool HasEntity(const FElysiumEntityHandle& Target) const;

	int32 NumEntityRules() const { return EntityRules.Num(); }
	int32 NumClassRules() const { return ClassRules.Num(); }

	void Serialize(FElysiumSaveArchive& Ar);
	void Rebase(const class FElysiumEntityWorld& World);

private:
	TArray<FElysiumEntityRelationship> EntityRules;
	TArray<FElysiumClassRelationship> ClassRules;
};
