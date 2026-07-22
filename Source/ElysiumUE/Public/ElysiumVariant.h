#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntityHandle.h"

// The variant's active type. These are our own runtime categories, not VtMB's on-disk
// `fieldtype_t` codes — the field tables (P1.3) map each VtMB code to one of these when
// they marshal a value in or out (e.g. FLOAT/TIME -> Float, STRING/MODELNAME/SOUNDNAME ->
// String, VECTOR/POSITION_VECTOR -> Vector, INTEGER -> Int, BOOLEAN -> Bool, the EHANDLE
// family -> Handle; see `python_bridge.md` "Divergence").
enum class EElysiumVariantType : uint8
{
	Void,
	Bool,
	Int,
	Float,
	String,
	Vector,
	Handle,
};

// R2/R8 — the marshalling currency for the whole entity substrate. One small tagged value
// carries input parameters, keyvalue/field values, `G` store entries, and script results
// through the two chokepoints. Kept a plain C++ struct (R1): no UObject, no reflection, so
// the store, save/load, and the inspector stay in our hands.
//
// Coercions are total (never fail) — VtMB's dispatch never throws on a type mismatch, and
// the `G` store's default-on-miss is integer 0. A Void variant is the "no value" / falsy
// case (also how error-to-false surfaces: a failed script eval yields Void -> false).
struct FElysiumVariant
{
	EElysiumVariantType Type = EElysiumVariantType::Void;

	// POD payload for the scalar types (exactly one is live, per Type).
	union
	{
		bool AsBool;
		int32 AsInt = 0;
		float AsFloat;
	};
	FVector AsVector = FVector::ZeroVector;
	FElysiumEntityHandle AsHandle;
	FString AsString;

	FElysiumVariant() = default;

	// Factory constructors. Named statics avoid the bool/int/pointer overload traps.
	static FElysiumVariant Void() { return FElysiumVariant(); }
	static FElysiumVariant Bool(bool In) { FElysiumVariant V; V.Type = EElysiumVariantType::Bool; V.AsBool = In; return V; }
	static FElysiumVariant Int(int32 In) { FElysiumVariant V; V.Type = EElysiumVariantType::Int; V.AsInt = In; return V; }
	static FElysiumVariant Float(float In) { FElysiumVariant V; V.Type = EElysiumVariantType::Float; V.AsFloat = In; return V; }
	static FElysiumVariant String(const FString& In) { FElysiumVariant V; V.Type = EElysiumVariantType::String; V.AsString = In; return V; }
	static FElysiumVariant Vector(const FVector& In) { FElysiumVariant V; V.Type = EElysiumVariantType::Vector; V.AsVector = In; return V; }
	static FElysiumVariant Handle(const FElysiumEntityHandle& In) { FElysiumVariant V; V.Type = EElysiumVariantType::Handle; V.AsHandle = In; return V; }

	bool IsVoid() const { return Type == EElysiumVariantType::Void; }
	bool IsBool() const { return Type == EElysiumVariantType::Bool; }
	bool IsInt() const { return Type == EElysiumVariantType::Int; }
	bool IsFloat() const { return Type == EElysiumVariantType::Float; }
	bool IsString() const { return Type == EElysiumVariantType::String; }
	bool IsVector() const { return Type == EElysiumVariantType::Vector; }
	bool IsHandle() const { return Type == EElysiumVariantType::Handle; }

	// Python truthiness (drives logic_pythoncheck OnTrue/OnFalse; Void -> false is
	// error-to-false). Objects are truthy in Python, so a Vector is true unless zero and
	// a Handle is true unless unbound; scalars follow C truthiness.
	bool ToBool() const
	{
		switch (Type)
		{
		case EElysiumVariantType::Bool:   return AsBool;
		case EElysiumVariantType::Int:    return AsInt != 0;
		case EElysiumVariantType::Float:  return AsFloat != 0.0f;
		case EElysiumVariantType::String: return !AsString.IsEmpty();
		case EElysiumVariantType::Vector: return !AsVector.IsNearlyZero();
		case EElysiumVariantType::Handle: return AsHandle.IsSet();
		default:                          return false; // Void
		}
	}

	int32 ToInt() const
	{
		switch (Type)
		{
		case EElysiumVariantType::Bool:   return AsBool ? 1 : 0;
		case EElysiumVariantType::Int:    return AsInt;
		case EElysiumVariantType::Float:  return static_cast<int32>(AsFloat);
		case EElysiumVariantType::String: return FCString::Atoi(*AsString);
		default:                          return 0; // Void/Vector/Handle
		}
	}

	float ToFloat() const
	{
		switch (Type)
		{
		case EElysiumVariantType::Bool:   return AsBool ? 1.0f : 0.0f;
		case EElysiumVariantType::Int:    return static_cast<float>(AsInt);
		case EElysiumVariantType::Float:  return AsFloat;
		case EElysiumVariantType::String: return FCString::Atof(*AsString);
		default:                          return 0.0f;
		}
	}

	FVector ToVector() const
	{
		return Type == EElysiumVariantType::Vector ? AsVector : FVector::ZeroVector;
	}

	FElysiumEntityHandle ToHandle() const
	{
		return Type == EElysiumVariantType::Handle ? AsHandle : FElysiumEntityHandle::Invalid();
	}

	// Value as a plain string (the marshalling form fed to a $string field / Python call).
	FString ToString() const
	{
		switch (Type)
		{
		case EElysiumVariantType::Bool:   return AsBool ? TEXT("1") : TEXT("0");
		case EElysiumVariantType::Int:    return FString::FromInt(AsInt);
		case EElysiumVariantType::Float:  return FString::SanitizeFloat(AsFloat);
		case EElysiumVariantType::String: return AsString;
		case EElysiumVariantType::Vector: return FString::Printf(TEXT("%g %g %g"), AsVector.X, AsVector.Y, AsVector.Z);
		case EElysiumVariantType::Handle: return AsHandle.ToString();
		default:                          return FString();
		}
	}

	// `Type(value)` — for logs and the inspector.
	FString Describe() const
	{
		switch (Type)
		{
		case EElysiumVariantType::Void:   return TEXT("Void");
		case EElysiumVariantType::Bool:   return FString::Printf(TEXT("Bool(%s)"), AsBool ? TEXT("true") : TEXT("false"));
		case EElysiumVariantType::Int:    return FString::Printf(TEXT("Int(%d)"), AsInt);
		case EElysiumVariantType::Float:  return FString::Printf(TEXT("Float(%s)"), *FString::SanitizeFloat(AsFloat));
		case EElysiumVariantType::String: return FString::Printf(TEXT("String(\"%s\")"), *AsString);
		case EElysiumVariantType::Vector: return FString::Printf(TEXT("Vector(%s)"), *ToString());
		case EElysiumVariantType::Handle: return FString::Printf(TEXT("Handle(%s)"), *AsHandle.ToString());
		default:                          return TEXT("?");
		}
	}

	bool operator==(const FElysiumVariant& Other) const
	{
		if (Type != Other.Type)
		{
			return false;
		}
		switch (Type)
		{
		case EElysiumVariantType::Bool:   return AsBool == Other.AsBool;
		case EElysiumVariantType::Int:    return AsInt == Other.AsInt;
		case EElysiumVariantType::Float:  return AsFloat == Other.AsFloat;
		case EElysiumVariantType::String: return AsString.Equals(Other.AsString, ESearchCase::CaseSensitive);
		case EElysiumVariantType::Vector: return AsVector == Other.AsVector;
		case EElysiumVariantType::Handle: return AsHandle == Other.AsHandle;
		default:                          return true; // Void == Void
		}
	}
	bool operator!=(const FElysiumVariant& Other) const { return !(*this == Other); }
};
