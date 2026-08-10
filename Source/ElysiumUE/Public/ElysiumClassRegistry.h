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

// R2 — what a registered field is *for*. VtMB's datamap flags carry the same two bits we need:
// 0x8 (FTYPEDESC_KEY — writable from a keyvalue/Python) and 0x2 (FTYPEDESC_SAVE — walked by the
// save/restore pass). Persistence is the field table's fifth consumer, beside I/O, the Python
// datamap walk, keyvalue application and the inspector (`docs/architecture/save-architecture.md` §4), so it is a
// flag on the registration rather than a second list somebody has to remember to edit.
enum class EElysiumField : uint8
{
	None = 0,
	Key  = 1 << 0,   // writable at runtime from a keyvalue / Python / an input
	Save = 1 << 1,   // enumerated by the save walk
};
ENUM_CLASS_FLAGS(EElysiumField)

// The default a registration takes when it says nothing: a keyable field the save walk carries.
// Saving a field that never changes costs nothing — the freeze diffs against a fresh build of the
// same def and omits everything that matches (`docs/architecture/save-architecture.md` §4's zero-omission rule,
// generalised from "zero" to "what the rebuild would produce").
inline constexpr EElysiumField ElysiumFieldDefault = EElysiumField::Key | EElysiumField::Save;

// R2 — one typed accessor over a live entity field. Get/Set marshal through the variant;
// `bKeyable` mirrors the VtMB datamap flags bit 0x8 (writable from a keyvalue/Python). The
// spawn pass applies map keyvalues regardless; runtime writes (Python/I/O, P1.4+) honour
// bKeyable. `bSave` is bit 0x2 — the save walk's enumeration. `Type` is the marshalling
// category, surfaced by the P2 inspector.
struct FElysiumFieldAccessor
{
	EElysiumVariantType Type = EElysiumVariantType::Void;
	bool bKeyable = false;
	bool bSave = false;
	TFunction<FElysiumVariant(const FElysiumEntity&)> Get;
	TFunction<void(FElysiumEntity&, const FElysiumVariant&)> Set;

	void ApplyFlags(EElysiumField Flags)
	{
		bKeyable = EnumHasAnyFlags(Flags, EElysiumField::Key);
		bSave    = EnumHasAnyFlags(Flags, EElysiumField::Save);
	}
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

	// A placeholder registered by ElysiumStubClasses.cpp for a classname no leaf implements: it
	// names the inputs the shipped maps fire so they resolve and report, and holds nothing else.
	// Entities of a stub class still spawn record-only, because that is what they are. `StubOwner`
	// is the owner line its inputs report — empty on every real class.
	bool bStub = false;
	FString StubOwner;

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
	FElysiumClassDesc& Field(FName Name, T FElysiumEntity::* Member, EElysiumField Flags = ElysiumFieldDefault)
	{
		FElysiumFieldAccessor Acc;
		Acc.ApplyFlags(Flags);
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

	// Insert a stub descriptor, or return null if the name is already claimed. Registration order
	// across translation units is unspecified, so this is the half that makes a real class always
	// win: registering first, it is skipped here; registering second, its `Register` replaces the
	// stub. Either way a stub can never shadow an implementation.
	FElysiumClassDesc* RegisterStub(FName ClassName, FName BaseName);

	// The descriptor for a classname, or null if unregistered (caller falls back to base).
	const FElysiumClassDesc* Find(FName ClassName) const;

	// The base descriptor (CBaseEntity) every unregistered classname resolves to as an
	// inert record. Null only before the base registers (never at runtime).
	const FElysiumClassDesc* BaseDesc() const;

	// Chain walk (derived shadows base): resolve an input/field by name up the base chain.
	FElysiumInputThunk FindInput(const FElysiumClassDesc& Desc, FName Input) const;
	const FElysiumFieldAccessor* FindField(const FElysiumClassDesc& Desc, FName Field) const;

	// The chain-resolved `Save`-flagged field names for a class, **sorted**. Sorted rather than in
	// registration order because a class's own table is a TMap: two walks in one process agree, but
	// a save has to be reproducible across builds, and §8's byte-identical round-trip test is a
	// digest comparison. Derived shadows base, so a name appears once.
	TArray<FName> SaveFields(const FElysiumClassDesc& Desc) const;

	// Build a live entity for a def: its leaf class if registered, else an inert base record.
	TUniquePtr<FElysiumEntity> Create(const FElysiumEntityDef& Def, FElysiumEntityHandle Handle) const;

	void ForEach(TFunctionRef<void(const FElysiumClassDesc&)> Fn) const;
	int32 Num() const { return Classes.Num(); }

private:
	// Indirect because a live entity holds `Class` as a raw descriptor pointer for its whole life,
	// and registration is no longer confined to static init: the item catalogue registers one class
	// per `vdata/items` definition when it first loads (`ElysiumItems::Install`), which can happen
	// while a world is standing. A `TMap<FName, FElysiumClassDesc>` rehashes on that insert and
	// every one of those pointers dangles; a map of unique pointers moves only the table.
	TMap<FName, TUniquePtr<FElysiumClassDesc>> Classes;
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
