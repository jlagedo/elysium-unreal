#include "Substrate/ElysiumRelationships.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumSaveArchive.h"

namespace ElysiumRelationships
{
	bool Parse(const FString& Token, EElysiumRelationship& Out)
	{
		if (Token.Equals(TEXT("D_HT"), ESearchCase::IgnoreCase)) { Out = EElysiumRelationship::Hate; return true; }
		if (Token.Equals(TEXT("D_FR"), ESearchCase::IgnoreCase)) { Out = EElysiumRelationship::Fear; return true; }
		if (Token.Equals(TEXT("D_LI"), ESearchCase::IgnoreCase)) { Out = EElysiumRelationship::Like; return true; }
		if (Token.Equals(TEXT("D_NU"), ESearchCase::IgnoreCase)) { Out = EElysiumRelationship::Neutral; return true; }
		return false;
	}

	const TCHAR* LexToString(EElysiumRelationship Value)
	{
		switch (Value)
		{
		case EElysiumRelationship::Hate: return TEXT("D_HT");
		case EElysiumRelationship::Fear: return TEXT("D_FR");
		case EElysiumRelationship::Like: return TEXT("D_LI");
		case EElysiumRelationship::Neutral: return TEXT("D_NU");
		}
		return TEXT("D_NU");
	}
}

bool FElysiumRelationships::SetEntity(const FElysiumEntityHandle& Target,
	EElysiumRelationship Value, int32 Priority)
{
	if (!Target.IsSet())
	{
		return false;
	}
	if (FElysiumEntityRelationship* Existing = EntityRules.FindByPredicate(
		[&Target](const FElysiumEntityRelationship& Row) { return Row.Target == Target; }))
	{
		if (Priority < Existing->Priority)
		{
			return false;
		}
		Existing->Value = Value;
		Existing->Priority = Priority;
	}
	else
	{
		EntityRules.Add({ Target, Value, Priority });
	}
	// A stated relationship supersedes any damage-derived memory of the same target: retail's
	// relation change re-gates enemy eligibility immediately, so the lapsing row must not keep
	// answering for a target an authored or scripted decision has just spoken for.
	DerivedRules.RemoveAll(
		[&Target](const FElysiumDerivedRelationship& Row) { return Row.Target == Target; });
	return true;
}

bool FElysiumRelationships::SetClass(const FString& Classname,
	EElysiumRelationship Value, int32 Priority)
{
	FString Key = Classname;
	Key.TrimStartAndEndInline();
	Key.ToLowerInline();
	if (Key.IsEmpty())
	{
		return false;
	}
	if (FElysiumClassRelationship* Existing = ClassRules.FindByPredicate(
		[&Key](const FElysiumClassRelationship& Row) { return Row.Classname == Key; }))
	{
		if (Priority < Existing->Priority)
		{
			return false;
		}
		Existing->Value = Value;
		Existing->Priority = Priority;
		return true;
	}
	ClassRules.Add({ MoveTemp(Key), Value, Priority });
	return true;
}

bool FElysiumRelationships::SetDerivedEntity(const FElysiumEntityHandle& Target,
	EElysiumRelationship Value, int32 Priority, double ExpiresAt)
{
	if (!Target.IsSet())
	{
		return false;
	}
	// `SetEntity`'s replacement rule, applied across the two surfaces: an authored, scripted or
	// dialogue-written row about this exact target beats a derived one at a HIGHER priority, and a
	// row that can never win is not stored at all.
	if (const FElysiumEntityRelationship* Persistent = FindEntityRow(Target))
	{
		if (Priority < Persistent->Priority)
		{
			return false;
		}
	}
	if (FElysiumDerivedRelationship* Existing = DerivedRules.FindByPredicate(
		[&Target](const FElysiumDerivedRelationship& Row) { return Row.Target == Target; }))
	{
		if (Priority < Existing->Priority)
		{
			return false;
		}
		Existing->Value = Value;
		Existing->Priority = Priority;
		Existing->ExpiresAt = ExpiresAt;   // re-stamped, not extended: the newest stimulus owns it
		return true;
	}
	DerivedRules.Add({ Target, Value, Priority, ExpiresAt });
	return true;
}

int32 FElysiumRelationships::ExpireDerived(double Now)
{
	return DerivedRules.RemoveAll([Now](const FElysiumDerivedRelationship& Row)
	{
		return Now >= Row.ExpiresAt;
	});
}

const FElysiumDerivedRelationship* FElysiumRelationships::FindDerived(
	const FElysiumEntityHandle& Target) const
{
	return DerivedRules.FindByPredicate(
		[&Target](const FElysiumDerivedRelationship& Row) { return Row.Target == Target; });
}

const FElysiumEntityRelationship* FElysiumRelationships::FindEntityRow(
	const FElysiumEntityHandle& Target) const
{
	return EntityRules.FindByPredicate(
		[&Target](const FElysiumEntityRelationship& Row) { return Row.Target == Target; });
}

const FElysiumClassRelationship* FElysiumRelationships::FindClassRow(const FString& Classname) const
{
	return ClassRules.FindByPredicate([&Classname](const FElysiumClassRelationship& Row)
	{
		return Row.Classname.Equals(Classname, ESearchCase::IgnoreCase);
	});
}

EElysiumRelationship FElysiumRelationships::Resolve(const FElysiumEntityHandle& Target,
	const FString& Classname) const
{
	EElysiumRelationship Value = EElysiumRelationship::Neutral;
	int32 Priority = 0;
	ResolveRow(Target, Classname, Value, Priority);
	return Value;
}

bool FElysiumRelationships::ResolveRow(const FElysiumEntityHandle& Target, const FString& Classname,
	EElysiumRelationship& OutValue, int32& OutPriority) const
{
	const FElysiumEntityRelationship* Exact = FindEntityRow(Target);
	if (const FElysiumDerivedRelationship* Derived = FindDerived(Target))
	{
		// The derived row stands unless a persistent exact-entity row outranks it. A persistent
		// CLASS row never does: an exact target has always outranked a class one, and a derived row
		// is about an exact target.
		if (Exact == nullptr || Derived->Priority >= Exact->Priority)
		{
			OutValue = Derived->Value;
			OutPriority = Derived->Priority;
			return true;
		}
	}
	if (Exact != nullptr)
	{
		OutValue = Exact->Value;
		OutPriority = Exact->Priority;
		return true;
	}
	if (const FElysiumClassRelationship* Class = FindClassRow(Classname))
	{
		OutValue = Class->Value;
		OutPriority = Class->Priority;
		return true;
	}
	OutValue = EElysiumRelationship::Neutral;
	OutPriority = 0;
	return false;
}

bool FElysiumRelationships::ResolvePersistentRow(const FElysiumEntityHandle& Target,
	const FString& Classname, EElysiumRelationship& OutValue, int32& OutPriority) const
{
	if (const FElysiumEntityRelationship* Exact = FindEntityRow(Target))
	{
		OutValue = Exact->Value;
		OutPriority = Exact->Priority;
		return true;
	}
	if (const FElysiumClassRelationship* Class = FindClassRow(Classname))
	{
		OutValue = Class->Value;
		OutPriority = Class->Priority;
		return true;
	}
	OutValue = EElysiumRelationship::Neutral;
	OutPriority = 0;
	return false;
}

int32 FElysiumRelationships::ResolvePriority(const FElysiumEntityHandle& Target,
	const FString& Classname) const
{
	EElysiumRelationship Value = EElysiumRelationship::Neutral;
	int32 Priority = 0;
	if (ResolveRow(Target, Classname, Value, Priority))
	{
		return Priority;   // raw, unclamped — the corpus writes 0 and 99
	}
	return Target.IsSet() ? 5 : 0;
}

bool FElysiumRelationships::HasEntity(const FElysiumEntityHandle& Target) const
{
	return FindEntityRow(Target) != nullptr;
}

void FElysiumRelationships::Serialize(FElysiumSaveArchive& Ar)
{
	// The derived rows are deliberately absent from the payload, and a load clears whatever the
	// destination store was holding: a derived row is a live stimulus with seconds left on it, and a
	// restored world starts with no stimulus at all rather than replaying one the player never
	// produced. The same rule the law-record bus and the sound cursor already carry.
	if (Ar.IsLoading())
	{
		DerivedRules.Reset();
	}

	int32 EntityCount = EntityRules.Num();
	Ar << EntityCount;
	if (Ar.IsLoading())
	{
		EntityRules.SetNum(FMath::Max(0, EntityCount));
	}
	for (FElysiumEntityRelationship& Row : EntityRules)
	{
		uint8 Value = static_cast<uint8>(Row.Value);
		Ar << Row.Target;
		Ar << Value;
		Ar << Row.Priority;
		if (Ar.IsLoading())
		{
			Row.Value = static_cast<EElysiumRelationship>(Value);
		}
	}

	int32 ClassCount = ClassRules.Num();
	Ar << ClassCount;
	if (Ar.IsLoading())
	{
		ClassRules.SetNum(FMath::Max(0, ClassCount));
	}
	for (FElysiumClassRelationship& Row : ClassRules)
	{
		uint8 Value = static_cast<uint8>(Row.Value);
		Ar << Row.Classname;
		Ar << Value;
		Ar << Row.Priority;
		if (Ar.IsLoading())
		{
			Row.Value = static_cast<EElysiumRelationship>(Value);
			Row.Classname.ToLowerInline();
		}
	}
}

void FElysiumRelationships::Rebase(const FElysiumEntityWorld& World)
{
	// Only the persistent rows: a rebase happens after a load, and no derived row crosses one.
	for (FElysiumEntityRelationship& Row : EntityRules)
	{
		Row.Target = World.RebaseSavedHandle(Row.Target);
	}
	EntityRules.RemoveAll([](const FElysiumEntityRelationship& Row) { return !Row.Target.IsSet(); });
}
