#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityHandle.h"
#include "ElysiumVariant.h"
#include "ElysiumSaveTypes.h" // 0x101a0a80: independent persistence metadata

#include <type_traits>

struct FElysiumEntityDef;
class FElysiumEntityWorld;

// --- The three keyvalue number parsers of vampire.dll (`walks/L0-r017.md`) ---------------------
//
// `FUN_101d0310` 0x101d0310, `void parse_floats(float* out, int count, const char* src)`: copy the
// text into a 128-byte frame (`Q_strncpy(buf, src, 0x80)`, so text past 127 characters is cut), then
// for `i = 0..count-1`: `out[i] = (float) atof(ptr)` (101d034e); NUL ends; skip bytes `<= 0x20` under
// a SIGNED compare (bytes >= 0x80 separate too; 101d0360-101d036c), NUL ends; skip bytes `> 0x20`
// (the token), NUL ends; a separator: `ptr++`, `i++` (101d0387). At the end `i = i + 1` and
// `out[i..count-1]` is zero-filled (101d038f-101d039d). So `"1 2"` with count 3 gives (1, 2, 0),
// `""` gives (0, 0, 0), `"1 2 3 4"` gives (1, 2, 3), `"1,2"` gives (1, 0, 0): the separator set is
// whitespace only, `atof` stops at the comma. Returns the number of elements the loop stored (the
// `i + 1` of the tail; 0 when `count <= 0`), which a caller's trace reports as the token count.
inline int32 ElysiumParseFloatList(float* Out, int32 Count, const FString& Text)
{
	if (Count <= 0)
	{
		return 0;                                                                   // 101d034c JLE
	}
	TCHAR Buf[128];
	FCString::Strncpy(Buf, *Text, UE_ARRAY_COUNT(Buf));                             // 101d0329 Q_strncpy(.., 0x80)
	const TCHAR* P = Buf;
	int32 I = 0;
	for (;;)
	{
		Out[I] = static_cast<float>(FCString::Atod(P));                             // 101d034e atof -> FSTP float
		if (*P == 0) break;                                                         // 101d035e
		while (*P != 0 && static_cast<int8>(*P & 0xff) <= 0x20) { ++P; }           // 101d0360 signed <= ' '
		if (*P == 0) break;
		while (*P != 0 && static_cast<int8>(*P & 0xff) > 0x20) { ++P; }            // 101d036e signed > ' '
		if (*P == 0) break;                                                         // 101d0382
		++P;                                                                        // 101d0387 one separator byte
		++I;
		if (I >= Count) { return Count; }                                           // 101d038d
	}
	const int32 Stored = I + 1;                                                     // 101d038f
	for (int32 K = Stored; K < Count; ++K) { Out[K] = 0.0f; }                      // 101d0393-101d039d zero-fill
	return Stored;
}

// `FUN_101d03e0` 0x101d03e0 (`UTIL_StringToVector`): `parse_floats(out, 3, text)`. The map's "x y z"
// spelling; a short string keeps the parsed components and zero-fills the rest. `OutTokens`, when
// given, receives the stored-element count for the caller's trace.
inline FVector ElysiumParseVec3(const FString& S, int32* OutTokens = nullptr)
{
	float V[3] = { 0.f, 0.f, 0.f };
	const int32 Tokens = ElysiumParseFloatList(V, 3, S);
	if (OutTokens != nullptr) { *OutTokens = Tokens; }
	return FVector(V[0], V[1], V[2]);
}

// `FUN_101d0570` 0x101d0570, `void int_list(int* out, int count, const char* src)`: the same
// 128-byte copy; for `i < count`: `out[i] = atoi(ptr)`; then scan from the TOKEN START to the next
// byte equal to 0x20 ONLY (a tab is not a separator; 101d05c0), NUL ends; `ptr++`, `i++`. Because the
// scan starts on the token start and `atoi` skips leading whitespace, a leading or doubled space
// repeats the next number: `" 10 20 30"` gives (10, 10, 20, 30). The tail zero-fills `out[i+1..]`
// (101d05db-101d05ee). `atoi` is VC6's `_atol`: sign, digits, NO overflow clamp (wraps at 32 bits),
// stops at the first non-digit (`"3.9"` is 3). Called by 0x101d0630 (count 4) and `CGameText::vfunc110`.
inline int32 ElysiumParseRetailAtoi(const TCHAR* P)
{
	while (*P != 0 && FChar::IsWhitespace(*P)) { ++P; }                            // isspace skip
	bool bNegative = false;
	if (*P == TEXT('-') || *P == TEXT('+')) { bNegative = *P == TEXT('-'); ++P; }  // one optional sign
	uint32 Acc = 0;
	while (*P >= TEXT('0') && *P <= TEXT('9')) { Acc = Acc * 10u + static_cast<uint32>(*P - TEXT('0')); ++P; }   // wraps
	return static_cast<int32>(bNegative ? (0u - Acc) : Acc);
}

inline void ElysiumParseIntList(int32* Out, int32 Count, const FString& Text)
{
	TCHAR Buf[128];
	FCString::Strncpy(Buf, *Text, UE_ARRAY_COUNT(Buf));                             // 101d0589 Q_strncpy(.., 0x80)
	const TCHAR* P = Buf;
	int32 I = 0;
	if (Count > 0)
	{
		for (;;)
		{
			Out[I] = ElysiumParseRetailAtoi(P);                                     // 101d05a2 atoi
			while (*P != 0 && *P != TEXT(' ')) { ++P; }                             // 101d05b4-101d05c8: 0x20 only
			if (*P == 0) break;                                                     // 101d05cc
			++P;                                                                    // 101d05d0
			++I;
			if (I >= Count) { return; }                                             // 101d05d5
		}
	}
	for (int32 K = I + 1; K < Count; ++K) { Out[K] = 0; }                          // 101d05db-101d05ee zero-fill
}

// `FUN_101d0630` 0x101d0630, `void rgba_parse(uint8 out[4], const char* src)`: `int_list(local, 4,
// src)` (thunk 0x10009fbb), then `out[k] = (uint8) local[k]` for k = 0..3 in memory order R, G, B, A
// (101d0644-101d0660). A three-number colour leaves A = 0 (the zero-fill); `300` is 44, `-1` is 255.
// Returned packed as the `color32` dword the datamap row holds (`+0x1a0`: R | G<<8 | B<<16 | A<<24).
inline uint32 ElysiumParseRgba(const FString& Text, uint8 OutBytes[4] = nullptr)
{
	int32 L[4] = { 0, 0, 0, 0 };
	ElysiumParseIntList(L, 4, Text);
	const uint8 R = static_cast<uint8>(L[0]), G = static_cast<uint8>(L[1]), B = static_cast<uint8>(L[2]), A = static_cast<uint8>(L[3]);
	if (OutBytes != nullptr) { OutBytes[0] = R; OutBytes[1] = G; OutBytes[2] = B; OutBytes[3] = A; }
	return static_cast<uint32>(R) | (static_cast<uint32>(G) << 8) | (static_cast<uint32>(B) << 16) | (static_cast<uint32>(A) << 24);
}

// VtMB's `fieldtype_t` codes, as the datamap rows carry them (`walks/L0-r017.md` Shared facts; the
// case labels of the walker 0x101a5a80 and the variant marshal 0x100d0390): 0 VOID, 1 FLOAT, 2 STRING,
// 3 VECTOR, 4 INTEGER, 5 BOOLEAN, 6 SHORT, 7 CHARACTER, 8 COLOR32, 9 EMBEDDED, 10 CUSTOM, 11 CLASSPTR,
// 12 EHANDLE, 13 EDICT, 14 POSITION_VECTOR, 15 TIME, 16 MODELNAME, 17 SOUNDNAME, 18 INPUT, 19 FUNCTION.
namespace ElysiumRetailFieldType
{
	inline constexpr uint8 Void = 0, Float = 1, String = 2, Vector = 3, Integer = 4, Boolean = 5, Short = 6,
		Character = 7, Color32 = 8, Embedded = 9, Custom = 10, ClassPtr = 11, EHandle = 12, Edict = 13,
		PositionVector = 14, Time = 15, ModelName = 16, SoundName = 17, Input = 18, Function = 19;
}

// An input thunk: applies one named input to an entity. A captureless registration lambda
// converts to this pointer. The base inputs call FElysiumEntity members; leaf classes
// register their own.
using FElysiumInputThunk = void(*)(FElysiumEntity& Self, const FElysiumInputArgs& Args);

// What a registered field is *for*. VtMB's datamap flags carry the same two bits we need:
// 0x8 (FTYPEDESC_INPUT — writable at runtime from Python/an input; the generated bindings map
// retail INPUT here, while KEY 0x4 alone adds nothing because map keyvalues apply regardless) and
// 0x2 (FTYPEDESC_SAVE — walked by the save/restore pass). Persistence is the field table's fifth consumer, beside I/O, the Python
// datamap walk, keyvalue application and the inspector, so it is a
// flag on the registration rather than a second list somebody has to remember to edit.
enum class EElysiumField : uint8
{
	None   = 0,
	Key    = 1 << 0,   // retail INPUT 0x8: writable at runtime from a keyvalue / Python / an input
	Save   = 1 << 1,   // retail SAVE 0x2: enumerated by the save walk
	// Retail KEY 0x4 (`FTYPEDESC_KEY`): the row answers a map key in the datamap walker `FUN_101a5a80`
	// 0x101a5a80 (`flags & 4`, 101a5b02) and a name read in `CBaseEntity::ReadKeyField` 0x100acab0
	// (`flags & 0x14`). The generated bindings carry it for every retail KEY row (`walks/L0-r017.md`).
	MapKey = 1 << 2,
	// Retail OUTPUT 0x10 (`FTYPEDESC_OUTPUT`): an entity-output row (type 10 CUSTOM, ops
	// `CEventsSaveDataOps` 0x106e70d8). Read by `ReadKeyField`'s `& 0x14` gate beside KEY.
	Output = 1 << 3,
	// A row of an EMBEDDED datamap (type 9, `m_Collision`'s `datamap_CCollisionProperty`) flattened
	// onto the owning descriptor: the walker 0x101a5a80 reaches it through its embedded descent, but
	// `ReadKeyField` 0x100acab0 tests a row's own name only and never descends, so the read refuses it.
	Embedded = 1 << 4,
};
ENUM_CLASS_FLAGS(EElysiumField)

// The default a registration takes when it says nothing: a keyable field the save walk carries,
// and a retail KEY row (a hand-registered row under a map key's name IS one). Saving a field that
// never changes costs nothing — the freeze diffs against a fresh build of the same def and omits
// everything that matches (the zero-omission rule, generalised from "zero" to "what the rebuild
// would produce").
inline constexpr EElysiumField ElysiumFieldDefault = EElysiumField::Key | EElysiumField::Save | EElysiumField::MapKey;

// One typed accessor over a live entity field. Get/Set marshal through the variant;
// `bKeyable` mirrors the VtMB datamap flags bit 0x8, FTYPEDESC_INPUT (writable from Python). The
// spawn pass applies map keyvalues regardless; runtime writes (Python/I/O) honour
// bKeyable. `bSave` is bit 0x2 — the save walk's enumeration. `Type` is the marshalling
// category, surfaced by the inspector. `bMapKey` / `bOutput` are retail's KEY 0x4 / OUTPUT 0x10;
// `RetailType` the row's `fieldtype_t` code where the marshalling category does not imply it (TIME,
// MODELNAME, SOUNDNAME, SHORT, CHARACTER, COLOR32, POSITION_VECTOR, CUSTOM), 0 otherwise.
struct FElysiumFieldAccessor
{
	EElysiumVariantType Type = EElysiumVariantType::Void;
	bool bKeyable = false;
	bool bSave = false;
	bool bMapKey = false;
	bool bOutput = false;
	bool bEmbedded = false;
	uint8 RetailType = 0;
	EElysiumPersistenceType PersistenceType = EElysiumPersistenceType::Value; // raw FLOAT by default
	EElysiumTimePolicy TimePolicy = EElysiumTimePolicy::Ordinary; // 0x101cf250
	TFunction<FElysiumVariant(const FElysiumEntity&)> Get;
	TFunction<void(FElysiumEntity&, const FElysiumVariant&)> Set;

	void ApplyFlags(EElysiumField Flags)
	{
		bKeyable = EnumHasAnyFlags(Flags, EElysiumField::Key);
		bSave    = EnumHasAnyFlags(Flags, EElysiumField::Save);
		bMapKey  = EnumHasAnyFlags(Flags, EElysiumField::MapKey);
		bOutput  = EnumHasAnyFlags(Flags, EElysiumField::Output);
		bEmbedded = EnumHasAnyFlags(Flags, EElysiumField::Embedded);
	}

	// The row's retail `fieldtype_t`: the stated code, else the one the marshalling category implies
	// (Int -> INTEGER 4, Float -> FLOAT 1, Bool -> BOOLEAN 5, String -> STRING 2, Vector -> VECTOR 3,
	// Handle -> EHANDLE 12, Void -> 0). What the walker 0x101a5a80 switches on and what
	// `FUN_100d0390` stores at `variant+0x10`.
	uint8 RetailFieldType() const
	{
		if (RetailType != 0) return RetailType;
		switch (Type)
		{
		case EElysiumVariantType::Bool:   return ElysiumRetailFieldType::Boolean;
		case EElysiumVariantType::Int:    return ElysiumRetailFieldType::Integer;
		case EElysiumVariantType::Float:  return ElysiumRetailFieldType::Float;
		case EElysiumVariantType::String: return ElysiumRetailFieldType::String;
		case EElysiumVariantType::Vector: return ElysiumRetailFieldType::Vector;
		case EElysiumVariantType::Handle: return ElysiumRetailFieldType::EHandle;
		default:                          return ElysiumRetailFieldType::Void;
		}
	}
};

// Builds one live entity of a class. The base/inert case returns a plain FElysiumEntity;
// leaf classes return their own subclass.
using FElysiumEntityFactory = TUniquePtr<FElysiumEntity>(*)();

// The per-classname descriptor: factory, base-class link, and the input + field tables.
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

	// An abstract retail class (story 5 step 2): `CAI_BaseNPCTroika`, `CNPC_VBaseBoss`, and each
	// retail class a classname descriptor derives through. It carries the rows its subclasses
	// inherit, but no classname factory builds it, so `Create` refuses it rather than standing an
	// inert record -- an authored row naming one is a defect, not an unported class.
	bool bAbstract = false;

	TMap<FName, FElysiumInputThunk> Inputs;
	TMap<FName, FElysiumFieldAccessor> Fields;
	// The datamap row's declared parameter type for the inputs that state one (`TypedInput`).
	// `CBaseEntity::AcceptInput` `0x100abc90` runs a row's function only when the variant is of the
	// row's type or `variant_t::Convert` (`0x100d05d0`) makes it so; otherwise it refuses the input. An
	// input registered with no declared type is retail's FIELD_VOID row, which `Convert` always admits.
	TMap<FName, EElysiumVariantType> InputTypes;

	// --- Registration helpers (called inside a class's Build callback) ---
	FElysiumClassDesc& Input(FName Name, FElysiumInputThunk Thunk)
	{
		Inputs.Add(Name, Thunk);
		return *this;
	}

	// An input whose datamap row declares a parameter type (`CLight`'s `SetPattern` / `FadeToPattern`
	// are FIELD_STRING, rows 6 / 7 of `0x105754f8`). The thunk sees the variant AFTER `Convert`.
	FElysiumClassDesc& TypedInput(FName Name, EElysiumVariantType DeclaredType, FElysiumInputThunk Thunk)
	{
		Inputs.Add(Name, Thunk);
		InputTypes.Add(Name, DeclaredType);
		return *this;
	}

	// Annotate a row this descriptor owns; does not change Python marshalling (0x101a0a80).
	FElysiumClassDesc& TimeField(FName Name, EElysiumTimePolicy Policy = EElysiumTimePolicy::Ordinary)
	{
		FElysiumFieldAccessor& Accessor = Fields.FindChecked(Name); // only the row's existing writer
		Accessor.PersistenceType = EElysiumPersistenceType::Time; // raw datamap TIME, not FLOAT
		Accessor.TimePolicy = Policy; // 0x101cf250/0x101cf2f0
		return *this;
	}

	// State a row's retail `fieldtype_t` where the member's marshalling category does not imply it:
	// the datamap walker `FUN_101a5a80` 0x101a5a80 switches on the ROW's code (COLOR32 8 parses four
	// bytes into a word a plain `int` would read as one number; SHORT 6 / CHARACTER 7 truncate), and
	// `ReadKeyField` 0x100acab0's variant keeps it (`FUN_100d0390` answers VOID for TIME 15, MODELNAME
	// 16, SOUNDNAME 17 and CUSTOM 10). Emitted by `gen_kernel_bindings` for every such generated row.
	FElysiumClassDesc& RetailType(FName Name, uint8 Code)
	{
		Fields.FindChecked(Name).RetailType = Code;
		return *this;
	}

	// An entity-output row: retail's `DEFINE_OUTPUT` (type 10 CUSTOM, flags SAVE|KEY|OUTPUT 0x16, ops
	// `CEventsSaveDataOps` 0x106e70d8). A map key of this name reaches the walker's custom branch
	// (`ops->vtbl[4]` 0x100cdb20 -> `FUN_100cd6d0` 0x100cd6d0), which PREPENDS the parsed action
	// (`FUN_100ccf90`) to the output's list -- `FElysiumEntity::AddOutputAction`. `ReadKeyField`
	// answers VOID 0, true for it. Reads nothing: the list is on the entity, not a member word.
	FElysiumClassDesc& OutputRow(FName Name)
	{
		FElysiumFieldAccessor Acc;
		Acc.ApplyFlags(EElysiumField::Save | EElysiumField::MapKey | EElysiumField::Output);
		Acc.Type = EElysiumVariantType::Void;
		Acc.RetailType = ElysiumRetailFieldType::Custom;
		Acc.Get = [](const FElysiumEntity&) { return FElysiumVariant::Void(); };
		Acc.Set = [Name](FElysiumEntity& E, const FElysiumVariant& V) { E.AddOutputAction(Name, V.ToString()); };
		Fields.Add(Name, MoveTemp(Acc));
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

// The module-static class registry. Descriptors are registered once at module load by
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

	// Insert an abstract descriptor (no factory; `Create` refuses it) for a retail class that only
	// its subclasses' classnames build.
	FElysiumClassDesc& RegisterAbstract(FName ClassName, FName BaseName);

	// The descriptor for a classname, or null if unregistered (caller falls back to base).
	const FElysiumClassDesc* Find(FName ClassName) const;

	// The base descriptor (CBaseEntity) every unregistered classname resolves to as an
	// inert record. Null only before the base registers (never at runtime).
	const FElysiumClassDesc* BaseDesc() const;

	// Chain walk (derived shadows base): resolve an input/field by name up the base chain.
	FElysiumInputThunk FindInput(const FElysiumClassDesc& Desc, FName Input) const;
	// The declared parameter type of the row `FindInput` resolves to (the same descriptor's), or null
	// for a row that declares none.
	const EElysiumVariantType* FindInputType(const FElysiumClassDesc& Desc, FName Input) const;
	const FElysiumFieldAccessor* FindField(const FElysiumClassDesc& Desc, FName Field) const;

	// The chain-resolved `Save`-flagged field names for a class, **sorted**. Sorted rather than in
	// registration order because a class's own table is a TMap: two walks in one process agree, but
	// a save has to be reproducible across builds, and §8's byte-identical round-trip test is a
	// digest comparison. Derived shadows base, so a name appears once.
	TArray<FName> SaveFields(const FElysiumClassDesc& Desc) const;

	// Build a live entity for a def: its leaf class if registered, else an inert base record. Null
	// (refused, logged) for an abstract class's descriptor, unless the def carries an internal
	// factory -- retail's construction by code, which no classname reaches. `World` is bound before
	// `Construct` runs, as `gpGlobals` is live for the base constructor `0x1009d980` (it reads
	// `curtime` into `m_flLastThink`) and the engine is for the edict attach that precedes the
	// keyvalues; null for a worldless probe entity.
	TUniquePtr<FElysiumEntity> Create(const FElysiumEntityDef& Def, FElysiumEntityHandle Handle,
		FElysiumEntityWorld* World = nullptr) const;

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
