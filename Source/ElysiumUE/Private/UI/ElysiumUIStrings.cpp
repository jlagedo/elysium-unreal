#include "UI/ElysiumUIStrings.h"

#include "ElysiumContentPaths.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumUIStrings, Log, All);

FElysiumUIStrings& FElysiumUIStrings::Get()
{
	static FElysiumUIStrings Instance;
	Instance.LoadOnce();
	return Instance;
}

void FElysiumUIStrings::LoadOnce()
{
	if (bTried)
	{
		return;
	}
	bTried = true;

	const FString Path = FElysiumContentPaths::UiStrings();
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		UE_LOG(LogElysiumUIStrings, Warning,
			TEXT("no UI string table at %s — falling back to built-in English. "
			     "Run: uv run elysium export bundle ui"), *Path);
		return;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogElysiumUIStrings, Warning, TEXT("UI string table is not valid JSON: %s"), *Path);
		return;
	}

	// The mirror is a flat token -> string object; the extractor already dropped the KeyValues
	// wrapper keys ("lang"/"Tokens"), so every pair here is a real string.
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Root->Values)
	{
		FString Value;
		if (Pair.Value.IsValid() && Pair.Value->TryGetString(Value))
		{
			Table.Add(Pair.Key, MoveTemp(Value));
		}
	}

	bLoaded = true;
	UE_LOG(LogElysiumUIStrings, Log, TEXT("UI string table: %d entries"), Table.Num());
}

FText FElysiumUIStrings::Resolve(const TCHAR* Token, const TCHAR* Fallback) const
{
	if (const FString* Found = Table.Find(Token))
	{
		return FText::FromString(*Found);
	}
	return FText::FromString(Fallback);
}
