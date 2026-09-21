#pragma once

#include "CoreMinimal.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityHandle.h"
#include "ElysiumVariant.h"

#include <type_traits>

// Register a field backed by a member the entity reaches through a *path* — a component struct,
// a struct nested inside one, or an element of an array on either. `Resolve` is a captureless
// generic lambda taking `auto&` and returning a reference to the word:
//
//     ElysiumAddClassFieldVia<FElysiumNpc>(D, TEXT("hintgroup"),
//         [](auto& N) -> auto& { return N.ScheduleHost.HintGroup; });
//
// Retail reaches a component word the same way every other word is reached — by offset off the
// entity, because `CAI_BaseNPCTroika` is one flat object and `CAI_Senses` and kin are embedded in
// it. This port splits that object into component structs, so a datamap row whose word lives in one
// needs a seam that reaches it by path. That is all this adds: the path is compiled, so a rename or
// a re-home is a build error, and nothing about the marshalling or the registry changes.
//
// The member type is deduced from the resolver's return, which is what makes one form serve a
// `bool`, an `FString`, an `FVector` and an array element alike. A generic lambda is required (not
// `[](FElysiumNpc& N)`) because the getter needs the same path through a `const` entity.
template <typename TClass, typename TResolve>
void ElysiumAddClassFieldVia(FElysiumClassDesc& D, const TCHAR* Name, TResolve Resolve,
	EElysiumField Flags = ElysiumFieldDefault)
{
	static_assert(std::is_base_of_v<FElysiumEntity, TClass>, "TClass must derive from FElysiumEntity");
	using TMember = std::remove_cv_t<std::remove_reference_t<
		decltype(Resolve(std::declval<TClass&>()))>>;
	static_assert(std::is_lvalue_reference_v<decltype(Resolve(std::declval<TClass&>()))>,
		"ElysiumAddClassFieldVia: the resolver must return a reference to the word");
	FElysiumFieldAccessor Acc;
	Acc.ApplyFlags(Flags);
	if constexpr (std::is_same_v<TMember, bool>)
	{
		Acc.Type = EElysiumVariantType::Bool;
		Acc.Get = [Resolve](const FElysiumEntity& E) { return FElysiumVariant::Bool(Resolve(static_cast<const TClass&>(E))); };
		// ToInt (not ToBool): a "0" keyvalue must read false, not "non-empty -> true".
		Acc.Set = [Resolve](FElysiumEntity& E, const FElysiumVariant& V) { Resolve(static_cast<TClass&>(E)) = V.ToInt() != 0; };
	}
	else if constexpr (std::is_integral_v<TMember>)
	{
		// Every retail `FIELD_INTEGER` row, whatever width the port chose for it. The datamap's own
		// type is `int`; a port member that widened to `uint32` (a bit word) or narrowed to `uint8`
		// is the same number, and the variant category a keyvalue and the inspector speak is Int.
		Acc.Type = EElysiumVariantType::Int;
		Acc.Get = [Resolve](const FElysiumEntity& E) { return FElysiumVariant::Int(static_cast<int32>(Resolve(static_cast<const TClass&>(E)))); };
		Acc.Set = [Resolve](FElysiumEntity& E, const FElysiumVariant& V) { Resolve(static_cast<TClass&>(E)) = static_cast<TMember>(V.ToInt()); };
	}
	else if constexpr (std::is_enum_v<TMember>)
	{
		// The port states several retail `int` words as a typed enum. The stored number is retail's,
		// so it marshals as Int; the cast back is unchecked for the same reason retail's is — the
		// datamap writes the word and the reader is what validates it.
		Acc.Type = EElysiumVariantType::Int;
		Acc.Get = [Resolve](const FElysiumEntity& E) { return FElysiumVariant::Int(static_cast<int32>(Resolve(static_cast<const TClass&>(E)))); };
		Acc.Set = [Resolve](FElysiumEntity& E, const FElysiumVariant& V) { Resolve(static_cast<TClass&>(E)) = static_cast<TMember>(V.ToInt()); };
	}
	else if constexpr (std::is_same_v<TMember, float>)
	{
		Acc.Type = EElysiumVariantType::Float;
		Acc.Get = [Resolve](const FElysiumEntity& E) { return FElysiumVariant::Float(Resolve(static_cast<const TClass&>(E))); };
		Acc.Set = [Resolve](FElysiumEntity& E, const FElysiumVariant& V) { Resolve(static_cast<TClass&>(E)) = V.ToFloat(); };
	}
	else if constexpr (std::is_same_v<TMember, double>)
	{
		// The port widens retail's `time` and several of its floats to double (the substrate clock
		// is a double), so a datamap row can land on one. The variant stays Float: the marshalling
		// category is what a keyvalue and the inspector speak, not the storage width.
		Acc.Type = EElysiumVariantType::Float;
		Acc.Get = [Resolve](const FElysiumEntity& E) { return FElysiumVariant::Float(static_cast<float>(Resolve(static_cast<const TClass&>(E)))); };
		Acc.Set = [Resolve](FElysiumEntity& E, const FElysiumVariant& V) { Resolve(static_cast<TClass&>(E)) = V.ToFloat(); };
	}
	else if constexpr (std::is_same_v<TMember, FString>)
	{
		Acc.Type = EElysiumVariantType::String;
		Acc.Get = [Resolve](const FElysiumEntity& E) { return FElysiumVariant::String(Resolve(static_cast<const TClass&>(E))); };
		Acc.Set = [Resolve](FElysiumEntity& E, const FElysiumVariant& V) { Resolve(static_cast<TClass&>(E)) = V.ToString(); };
	}
	else if constexpr (std::is_same_v<TMember, FVector>)
	{
		Acc.Type = EElysiumVariantType::Vector;
		// A keyvalue arrives as a string variant, which ToVector() reads as zero; parse it the way
		// the base Field does so a vector keyfield spawns with its authored value.
		Acc.Get = [Resolve](const FElysiumEntity& E) { return FElysiumVariant::Vector(Resolve(static_cast<const TClass&>(E))); };
		Acc.Set = [Resolve](FElysiumEntity& E, const FElysiumVariant& V)
		{
			Resolve(static_cast<TClass&>(E)) = V.IsVector() ? V.ToVector() : ElysiumParseVec3(V.ToString());
		};
	}
	else if constexpr (std::is_same_v<TMember, FElysiumEntityHandle>)
	{
		// The archive drops a handle's epoch by design and ApplySnapshot re-stamps it, so a
		// Save-flagged handle field restores as a live reference or as Invalid.
		Acc.Type = EElysiumVariantType::Handle;
		Acc.Get = [Resolve](const FElysiumEntity& E) { return FElysiumVariant::Handle(Resolve(static_cast<const TClass&>(E))); };
		Acc.Set = [Resolve](FElysiumEntity& E, const FElysiumVariant& V) { Resolve(static_cast<TClass&>(E)) = V.ToHandle(); };
	}
	else
	{
		static_assert(sizeof(TMember) == 0, "ElysiumAddClassFieldVia: unsupported member type");
	}
	D.Fields.Add(FName(Name), MoveTemp(Acc));
}

// Register a field backed by a *subclass* member (FElysiumClassDesc::Field only reaches base
// FElysiumEntity members; leaf classes carry their own state). The accessor static_casts the
// entity to the member's class — always valid, because a class's field table is only ever walked
// for entities of that class or a subclass. One module-unique name so every registration site
// can land in one unity blob.
//
// A pointer-to-member is the common case of the path form above, so it delegates: one marshalling
// chain, one place a new member type is taught.
template <typename TClass, typename TMember>
void ElysiumAddClassField(FElysiumClassDesc& D, const TCHAR* Name, TMember TClass::* Member,
	EElysiumField Flags = ElysiumFieldDefault)
{
	ElysiumAddClassFieldVia<TClass>(D, Name,
		[Member](auto& E) -> auto& { return E.*Member; }, Flags);
}
