#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

/**
 * Tolerant field readers for the JSON sidecars the import lanes stage.
 *
 * A sidecar is a contract between an offline Python stage and an editor script, and it grows a
 * field at a time. So a key that is absent, null or of another type leaves the caller's default
 * standing rather than failing the record: the alternative is an import that refuses every asset
 * because one optional field was added on the Python side first. What is *not* tolerated —
 * a body that is not a JSON object at all — is the caller's check, not this header's.
 *
 * Reflection-free, header-only and inline: these are plain functions over `FJsonObject`, shared by
 * the surface-property lane's two records so the same tolerance is spelled once.
 */
namespace ElysiumJson
{
	inline FString Str(const TSharedRef<FJsonObject>& Object, const TCHAR* Key, const FString& Default = FString())
	{
		FString Out;
		return Object->TryGetStringField(Key, Out) ? Out : Default;
	}

	/** The int64 overload keeps a container offset exact past 2^53 and rejects a fractional value. */
	inline int64 Int(const TSharedRef<FJsonObject>& Object, const TCHAR* Key, int64 Default = 0)
	{
		int64 Out = 0;
		return Object->TryGetNumberField(Key, Out) ? Out : Default;
	}

	inline double Num(const TSharedRef<FJsonObject>& Object, const TCHAR* Key, double Default = 0.0)
	{
		double Out = 0.0;
		return Object->TryGetNumberField(Key, Out) ? Out : Default;
	}

	inline float Float(const TSharedRef<FJsonObject>& Object, const TCHAR* Key, float Default = 0.0f)
	{
		return static_cast<float>(Num(Object, Key, static_cast<double>(Default)));
	}

	inline bool Bool(const TSharedRef<FJsonObject>& Object, const TCHAR* Key, bool bDefault = false)
	{
		bool bOut = false;
		return Object->TryGetBoolField(Key, bOut) ? bOut : bDefault;
	}

	inline const TArray<TSharedPtr<FJsonValue>>* Arr(const TSharedRef<FJsonObject>& Object, const TCHAR* Key)
	{
		const TArray<TSharedPtr<FJsonValue>>* Out = nullptr;
		return Object->TryGetArrayField(Key, Out) ? Out : nullptr;
	}

	inline TSharedPtr<FJsonObject> Obj(const TSharedRef<FJsonObject>& Object, const TCHAR* Key)
	{
		const TSharedPtr<FJsonObject>* Out = nullptr;
		return Object->TryGetObjectField(Key, Out) && Out ? *Out : nullptr;
	}

	/** Every string element of `Key`, in order; a non-string element is skipped, not defaulted. */
	inline TArray<FString> Strings(const TSharedRef<FJsonObject>& Object, const TCHAR* Key)
	{
		TArray<FString> Out;
		if (const TArray<TSharedPtr<FJsonValue>>* Values = Arr(Object, Key))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				FString Element;
				if (Value.IsValid() && Value->TryGetString(Element))
				{
					Out.Add(Element);
				}
			}
		}
		return Out;
	}
}
