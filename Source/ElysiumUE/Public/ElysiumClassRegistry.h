#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityHandle.h"
#include "ElysiumVariant.h"

#include <type_traits>

struct FElysiumEntityDef;

// Parse a space-separated "x y z" keyvalue into an FVector (zero on any other shape). Used
// by the Vector field accessor to marshal a string keyvalue (angles, velocity) at spawn.
inline FVector ElysiumParseVec3(const FString& S)
{
	TArray<FString> Parts;
	S.ParseIntoArrayWS(Parts);
	FVector V = FVector::ZeroVector;
	if (Parts.Num() >= 3)
	{
		V.X = FCString::Atof(*Parts[0]);
		V.Y = FCString::Atof(*Parts[1]);
		V.Z = FCString::Atof(*Parts[2]);
	}
	return V;
}

// An input thunk: applies one named input to an entity. A captureless registration lambda
// converts to this pointer. The base inputs call FElysiumEntity members; leaf classes
// (P1.6+) register their own.
using FElysiumInputThunk = void(*)(FElysiumEntity& Self, const FElysiumInputArgs& Args);

// R2 — one typed accessor over a live entity field. Get/Set marshal through the variant;
// `bKeyable` mirrors the VtMB datamap flags bit 0x8 (writable from a keyvalue/Python). The
// spawn pass applies map keyvalues regardless; runtime writes (Python/I/O, P1.4+) honour
// bKeyable. `Type` is the marshalling category, surfaced by the P2 inspector.
struct FElysiumFieldAccessor
{
	EElysiumVariantType Type = EElysiumVariantType::Void;
	bool bKeyable = false;
	TFunction<FElysiumVariant(const FElysiumEntity&)> Get;
	TFunction<void(FElysiumEntity&, const FElysiumVariant&)> Set;
};

// Builds one live entity of a class. The base/inert case returns a plain FElysiumEntity;
// leaf classes return their own subclass (P1.6+).
using FElysiumEntityFactory = TUniquePtr<FElysiumEntity>(*)();

// R2 — the per-classname descriptor: factory, base-class link, and the input + field tables.
// The tables hold only this class's own rows; the registry walks the base chain at lookup
// time (derived shadows base), so editing the base reaches every subclass with no flatten.
// Keys are FName, so lookup folds case (entity_io.md: fold input-name case).
struct FElysiumClassDesc
{
	FName ClassName;
	FName BaseName;                              // NAME_None at the root (CBaseEntity)
	FElysiumEntityFactory Factory = nullptr;

	TMap<FName, FElysiumInputThunk> Inputs;
	TMap<FName, FElysiumFieldAccessor> Fields;

	// --- Registration helpers (called inside a class's Build callback) -----------------
	FElysiumClassDesc& Input(FName Name, FElysiumInputThunk Thunk)
	{
		Inputs.Add(Name, Thunk);
		return *this;
	}

	// Register a data member as a typed field. The member type deduces the variant category
	// and the get/set marshalling. String keyvalues coerce at spawn (Atoi/Atof/vec-parse).
	template <typename T>
	FElysiumClassDesc& Field(FName Name, T FElysiumEntity::* Member, bool bKeyable = true)
	{
		FElysiumFieldAccessor Acc;
		Acc.bKeyable = bKeyable;
		if constexpr (std::is_same_v<T, int32>)
		{
			Acc.Type = EElysiumVariantType::Int;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::Int(E.*Member); };
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { E.*Member = V.ToInt(); };
		}
		else if constexpr (std::is_same_v<T, float>)
		{
			Acc.Type = EElysiumVariantType::Float;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::Float(E.*Member); };
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { E.*Member = V.ToFloat(); };
		}
		else if constexpr (std::is_same_v<T, bool>)
		{
			Acc.Type = EElysiumVariantType::Bool;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::Bool(E.*Member); };
			// ToInt (not ToBool): a "0" string keyvalue must read false, not "non-empty -> true".
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { E.*Member = V.ToInt() != 0; };
		}
		else if constexpr (std::is_same_v<T, FString>)
		{
			Acc.Type = EElysiumVariantType::String;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::String(E.*Member); };
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { E.*Member = V.ToString(); };
		}
		else if constexpr (std::is_same_v<T, FVector>)
		{
			Acc.Type = EElysiumVariantType::Vector;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::Vector(E.*Member); };
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V)
			{
				E.*Member = V.IsVector() ? V.ToVector() : ElysiumParseVec3(V.ToString());
			};
		}
		else
		{
			static_assert(sizeof(T) == 0, "FElysiumClassDesc::Field: unsupported member type");
		}
		Fields.Add(Name, MoveTemp(Acc));
		return *this;
	}
};

// R2 — the module-static class registry. Descriptors are registered once at module load by
// FElysiumClassRegistrar statics; lookups (case-folded, base-chain) drive I/O dispatch,
// Python attribute get/set, spawn keyvalue application, save enumeration, and the inspector.
// There is no second dispatch mechanism anywhere.
class FElysiumClassRegistry
{
public:
	static FElysiumClassRegistry& Get();

	// Insert a descriptor and return it for the registrar's Build callback to populate.
	FElysiumClassDesc& Register(FName ClassName, FName BaseName, FElysiumEntityFactory Factory);

	// The descriptor for a classname, or null if unregistered (caller falls back to base).
	const FElysiumClassDesc* Find(FName ClassName) const;

	// The base descriptor (CBaseEntity) every unregistered classname resolves to as an
	// inert record. Null only before the base registers (never at runtime).
	const FElysiumClassDesc* BaseDesc() const;

	// Chain walk (derived shadows base): resolve an input/field by name up the base chain.
	FElysiumInputThunk FindInput(const FElysiumClassDesc& Desc, FName Input) const;
	const FElysiumFieldAccessor* FindField(const FElysiumClassDesc& Desc, FName Field) const;

	// Build a live entity for a def: its leaf class if registered, else an inert base record.
	TUniquePtr<FElysiumEntity> Create(const FElysiumEntityDef& Def, FElysiumEntityHandle Handle) const;

	void ForEach(TFunctionRef<void(const FElysiumClassDesc&)> Fn) const;
	int32 Num() const { return Classes.Num(); }

private:
	TMap<FName, FElysiumClassDesc> Classes;
};

// A file-static instance registers one class at module-load time (before any Create/lookup).
// Build populates the fresh descriptor's input + field tables.
struct FElysiumClassRegistrar
{
	FElysiumClassRegistrar(FName ClassName, FName BaseName, FElysiumEntityFactory Factory,
		TFunctionRef<void(FElysiumClassDesc&)> Build)
	{
		Build(FElysiumClassRegistry::Get().Register(ClassName, BaseName, Factory));
	}
};

// The base classname every entity's chain terminates at (python_bridge.md: the datamap
// baseMap chain walks up to CBaseEntity). A function (not a global) so the FName is built at
// call time — no static-init ordering hazard against the registrar statics that use it.
inline FName ElysiumBaseClassName() { return FName(TEXT("CBaseEntity")); }
