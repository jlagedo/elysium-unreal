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
		return true;
	}
	EntityRules.Add({ Target, Value, Priority });
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

EElysiumRelationship FElysiumRelationships::Resolve(const FElysiumEntityHandle& Target,
	const FString& Classname) const
{
	if (const FElysiumEntityRelationship* Exact = EntityRules.FindByPredicate(
		[&Target](const FElysiumEntityRelationship& Row) { return Row.Target == Target; }))
	{
		return Exact->Value;
	}
	if (const FElysiumClassRelationship* Class = ClassRules.FindByPredicate(
		[&Classname](const FElysiumClassRelationship& Row)
		{
			return Row.Classname.Equals(Classname, ESearchCase::IgnoreCase);
		}))
	{
		return Class->Value;
	}
	return EElysiumRelationship::Neutral;
}

bool FElysiumRelationships::HasEntity(const FElysiumEntityHandle& Target) const
{
	return EntityRules.ContainsByPredicate(
		[&Target](const FElysiumEntityRelationship& Row) { return Row.Target == Target; });
}

void FElysiumRelationships::Serialize(FElysiumSaveArchive& Ar)
{
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
	for (FElysiumEntityRelationship& Row : EntityRules)
	{
		Row.Target = World.RebaseSavedHandle(Row.Target);
	}
	EntityRules.RemoveAll([](const FElysiumEntityRelationship& Row) { return !Row.Target.IsSet(); });
}
