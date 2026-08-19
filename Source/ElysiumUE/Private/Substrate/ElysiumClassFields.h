#pragma once

#include "CoreMinimal.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityHandle.h"
#include "ElysiumVariant.h"

#include <type_traits>

// Register a field backed by a *subclass* member (FElysiumClassDesc::Field only reaches base
// FElysiumEntity members; leaf classes carry their own state). The accessor static_casts the
// entity to the member's class — always valid, because a class's field table is only ever walked
// for entities of that class or a subclass. One module-unique name so every registration site
// can land in one unity blob.
template <typename TClass, typename TMember>
void ElysiumAddClassField(FElysiumClassDesc& D, const TCHAR* Name, TMember TClass::* Member,
	EElysiumField Flags = ElysiumFieldDefault)
{
	static_assert(std::is_base_of_v<FElysiumEntity, TClass>, "TClass must derive from FElysiumEntity");
	FElysiumFieldAccessor Acc;
	Acc.ApplyFlags(Flags);
	if constexpr (std::is_same_v<TMember, int32>)
	{
		Acc.Type = EElysiumVariantType::Int;
		Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::Int(static_cast<const TClass&>(E).*Member); };
		Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToInt(); };
	}
	else if constexpr (std::is_same_v<TMember, bool>)
	{
		Acc.Type = EElysiumVariantType::Bool;
		Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::Bool(static_cast<const TClass&>(E).*Member); };
		// ToInt (not ToBool): a "0" keyvalue must read false, not "non-empty -> true".
		Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToInt() != 0; };
	}
	else if constexpr (std::is_same_v<TMember, float>)
	{
		Acc.Type = EElysiumVariantType::Float;
		Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::Float(static_cast<const TClass&>(E).*Member); };
		Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToFloat(); };
	}
	else if constexpr (std::is_same_v<TMember, FString>)
	{
		Acc.Type = EElysiumVariantType::String;
		Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::String(static_cast<const TClass&>(E).*Member); };
		Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToString(); };
	}
	else if constexpr (std::is_same_v<TMember, FVector>)
	{
		Acc.Type = EElysiumVariantType::Vector;
		Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::Vector(static_cast<const TClass&>(E).*Member); };
		Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToVector(); };
	}
	else if constexpr (std::is_same_v<TMember, FElysiumEntityHandle>)
	{
		// The archive drops a handle's epoch by design and ApplySnapshot re-stamps it, so a
		// Save-flagged handle field restores as a live reference or as Invalid.
		Acc.Type = EElysiumVariantType::Handle;
		Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::Handle(static_cast<const TClass&>(E).*Member); };
		Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToHandle(); };
	}
	else
	{
		static_assert(sizeof(TMember) == 0, "ElysiumAddClassField: unsupported member type");
	}
	D.Fields.Add(FName(Name), MoveTemp(Acc));
}
