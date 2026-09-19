#include "AiInfra/ElysiumKeyfieldAccess.h"

#include "ElysiumClassRegistry.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"

namespace ElysiumKeyfieldAccess
{
	namespace
	{
		bool IsVectorProperty(const FProperty* Property)
		{
			const FStructProperty* Struct = CastField<FStructProperty>(Property);
			return Struct != nullptr && Struct->Struct == TBaseStructure<FVector>::Get();
		}

		bool IsSupported(const FProperty* Property)
		{
			return CastField<FIntProperty>(Property) != nullptr
				|| CastField<FFloatProperty>(Property) != nullptr
				|| CastField<FBoolProperty>(Property) != nullptr
				|| CastField<FStrProperty>(Property) != nullptr
				|| IsVectorProperty(Property);
		}

		// `%.9g` round-trips any float through `Atof`.
		FString FormatFloat(double Value)
		{
			return FString::Printf(TEXT("%.9g"), Value);
		}
	}

	const FProperty* FindProperty(const UScriptStruct* Struct, const FString& Key)
	{
		if (Struct == nullptr || Key.IsEmpty())
		{
			return nullptr;
		}
		const FProperty* Property = Struct->FindPropertyByName(FName(*Key));
		return Property != nullptr && IsSupported(Property) ? Property : nullptr;
	}

	const FView* FindOwner(TConstArrayView<FView> Views, const FString& Key, const FProperty*& OutProperty)
	{
		for (const FView& View : Views)
		{
			if (const FProperty* Property = FindProperty(View.Struct, Key))
			{
				OutProperty = Property;
				return &View;
			}
		}
		OutProperty = nullptr;
		return nullptr;
	}

	void Apply(const FProperty* Property, void* Data, const FString& Raw)
	{
		void* Value = Property->ContainerPtrToValuePtr<void>(Data);
		if (const FIntProperty* Int = CastField<FIntProperty>(Property))
		{
			Int->SetPropertyValue(Value, FCString::Atoi(*Raw));
		}
		else if (const FFloatProperty* Float = CastField<FFloatProperty>(Property))
		{
			Float->SetPropertyValue(Value, FCString::Atof(*Raw));
		}
		else if (const FBoolProperty* Bool = CastField<FBoolProperty>(Property))
		{
			Bool->SetPropertyValue(Value, FCString::Atoi(*Raw) != 0);
		}
		else if (const FStrProperty* Str = CastField<FStrProperty>(Property))
		{
			Str->SetPropertyValue(Value, Raw);
		}
		else if (IsVectorProperty(Property))
		{
			*static_cast<FVector*>(Value) = ElysiumParseVec3(Raw);
		}
	}

	bool Matches(const FProperty* Property, const void* Data, const FString& Raw)
	{
		const void* Value = Property->ContainerPtrToValuePtr<void>(Data);
		if (const FIntProperty* Int = CastField<FIntProperty>(Property))
		{
			return Int->GetPropertyValue(Value) == FCString::Atoi(*Raw);
		}
		if (const FFloatProperty* Float = CastField<FFloatProperty>(Property))
		{
			return Float->GetPropertyValue(Value) == FCString::Atof(*Raw);
		}
		if (const FBoolProperty* Bool = CastField<FBoolProperty>(Property))
		{
			return Bool->GetPropertyValue(Value) == (FCString::Atoi(*Raw) != 0);
		}
		if (const FStrProperty* Str = CastField<FStrProperty>(Property))
		{
			return Str->GetPropertyValue(Value).Equals(Raw, ESearchCase::CaseSensitive);
		}
		if (IsVectorProperty(Property))
		{
			return *static_cast<const FVector*>(Value) == ElysiumParseVec3(Raw);
		}
		return false;
	}

	FString Format(const FProperty* Property, const void* Data)
	{
		const void* Value = Property->ContainerPtrToValuePtr<void>(Data);
		if (const FIntProperty* Int = CastField<FIntProperty>(Property))
		{
			return FString::FromInt(Int->GetPropertyValue(Value));
		}
		if (const FFloatProperty* Float = CastField<FFloatProperty>(Property))
		{
			return FormatFloat(Float->GetPropertyValue(Value));
		}
		if (const FBoolProperty* Bool = CastField<FBoolProperty>(Property))
		{
			return Bool->GetPropertyValue(Value) ? TEXT("1") : TEXT("0");
		}
		if (const FStrProperty* Str = CastField<FStrProperty>(Property))
		{
			return Str->GetPropertyValue(Value);
		}
		if (IsVectorProperty(Property))
		{
			const FVector& V = *static_cast<const FVector*>(Value);
			return FormatFloat(V.X) + TEXT(" ") + FormatFloat(V.Y) + TEXT(" ") + FormatFloat(V.Z);
		}
		return FString();
	}

	bool IsZero(const FProperty* Property, const void* Data)
	{
		const void* Value = Property->ContainerPtrToValuePtr<void>(Data);
		if (const FIntProperty* Int = CastField<FIntProperty>(Property))
		{
			return Int->GetPropertyValue(Value) == 0;
		}
		if (const FFloatProperty* Float = CastField<FFloatProperty>(Property))
		{
			return Float->GetPropertyValue(Value) == 0.0f;
		}
		if (const FBoolProperty* Bool = CastField<FBoolProperty>(Property))
		{
			return !Bool->GetPropertyValue(Value);
		}
		if (const FStrProperty* Str = CastField<FStrProperty>(Property))
		{
			return Str->GetPropertyValue(Value).IsEmpty();
		}
		if (IsVectorProperty(Property))
		{
			return static_cast<const FVector*>(Value)->IsZero();
		}
		return true;
	}
}
