#include "ElysiumCastData.h"
#include "ElysiumBodyData.h"
#include "ElysiumExpressionData.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"
#include "UObject/StrongObjectPtr.h"
#if WITH_EDITOR
#include "JsonObjectConverter.h"
#endif

FString UElysiumCastData::ModelIdForPreparation(const FString& Name,FString& OutError)
{
	const FString Normal=Name.TrimStartAndEnd().Replace(TEXT("\\"),TEXT("/")).ToLower();
	if (Normal.StartsWith(TEXT("vtmb:model:"))) { OutError.Reset(); return Normal; }
	static TStrongObjectPtr<UElysiumCastData> Data;
	if (!Data.IsValid()) Data=TStrongObjectPtr<UElysiumCastData>(
		LoadObject<UElysiumCastData>(nullptr,TEXT("/ElysiumBaked/Models/_Corpus/DA_Cast.DA_Cast")));
	if (!Data.IsValid()) { OutError=TEXT("native cast table is absent"); return FString(); }
	const auto* Model=Data->FindModel(Normal,OutError);
	return Model?Model->AssetId:FString();
}

const FElysiumCastModel* UElysiumCastData::FindModel(const FString& Name, FString& OutError) const
{
	FString Key = Name.TrimStartAndEnd().Replace(TEXT("\\"),TEXT("/")).ToLower();
	OutError.Reset();
	if (const auto* Model = Models.Find(Key)) return Model;
	if (const FString* Id = Aliases.Find(Key))
	{
		if (const auto* Model = Models.Find(*Id)) return Model;
		OutError = TEXT("cast alias names an absent model: ") + *Id;
	}
	else if (const auto* Candidates = AmbiguousAliases.Find(Key))
		OutError = TEXT("ambiguous model name; use one of: ") + FString::Join(Candidates->AssetIds,TEXT(", "));
	else OutError = TEXT("model is not in the cast table: ") + Name;
	return nullptr;
}

const FElysiumCinematicOwnerRef* UElysiumCastData::FindCinematic(const FString& Model, const FString& Root, FString& OutError) const
{
	const auto* Entry = FindModel(Model, OutError);
	if (!Entry) return nullptr;
	const auto* Set = Cinematics.Find(Entry->AssetId);
	if (!Set) { OutError = TEXT("model has no cinematic owner table: ") + Entry->AssetId; return nullptr; }
	for (const auto& Pair : Set->Roots)
		if (Pair.Key.Equals(Root,ESearchCase::IgnoreCase)) return &Pair.Value;
	if (Root.IsEmpty() && Set->Roots.Num()==1) return &Set->Roots.CreateConstIterator().Value();
	OutError = TEXT("cinematic root is absent or ambiguous: ") + Root;
	return nullptr;
}

UElysiumCastData* UElysiumCastData::ApplyJson(UElysiumCastData* Asset, const FString& Json, FString& OutError)
{
#if WITH_EDITOR
	TSharedPtr<FJsonObject> Object;
	if (!Asset || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Object) || !Object.IsValid())
	{ OutError = TEXT("invalid cast JSON or absent asset"); return nullptr; }
	FString Version;
	if (!Object->TryGetStringField(TEXT("schemaVersion"),Version) || Version!=TEXT("1.0.0"))
	{ OutError = TEXT("unsupported cast schema"); return nullptr; }
	TMap<FString,FElysiumCastModel> Models;
	TMap<FString,FString> Aliases;
	TMap<FString,FElysiumCastAliasCandidates> Ambiguous;
	TMap<FString,FElysiumCastCinematic> Cinematics;
	const TSharedPtr<FJsonObject>* Rows=nullptr;
	if (!Object->TryGetObjectField(TEXT("models"),Rows) || !Rows) { OutError=TEXT("no model table"); return nullptr; }
	for (const TPair<FString,TSharedPtr<FJsonValue>>& Pair : (*Rows)->Values)
	{
		FElysiumCastModel Row;
		const TSharedPtr<FJsonObject>* Record=nullptr;
		if (!Pair.Value->TryGetObject(Record) || !Record || !FJsonObjectConverter::JsonObjectToUStruct(Record->ToSharedRef(),&Row,0,0,true)
			|| Row.AssetId!=Pair.Key) { OutError=TEXT("invalid model row: ")+Pair.Key; return nullptr; }
		Models.Add(Pair.Key,MoveTemp(Row));
	}
	if (!Object->TryGetObjectField(TEXT("aliases"),Rows) || !Rows) { OutError=TEXT("no alias table"); return nullptr; }
	for (const TPair<FString,TSharedPtr<FJsonValue>>& Pair : (*Rows)->Values)
	{
		FString Id;
		if (!Pair.Value->TryGetString(Id) || !Models.Contains(Id)) { OutError=TEXT("invalid alias: ")+Pair.Key; return nullptr; }
		Aliases.Add(Pair.Key,Id);
	}
	if (!Object->TryGetObjectField(TEXT("ambiguousAliases"),Rows) || !Rows) { OutError=TEXT("no collision table"); return nullptr; }
	for (const TPair<FString,TSharedPtr<FJsonValue>>& Pair : (*Rows)->Values)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
		if (!Pair.Value->TryGetArray(Values) || !Values) { OutError=TEXT("invalid alias candidates"); return nullptr; }
		auto& Row=Ambiguous.Add(Pair.Key);
		for (const auto& Value : *Values)
		{
			FString Id;
			if (!Value->TryGetString(Id) || !Models.Contains(Id)) { OutError=TEXT("alias candidate is absent"); return nullptr; }
			Row.AssetIds.Add(Id);
		}
	}
	if (!Object->TryGetObjectField(TEXT("cinematics"),Rows) || !Rows) { OutError=TEXT("no cinematic table"); return nullptr; }
	for (const TPair<FString,TSharedPtr<FJsonValue>>& Pair : (*Rows)->Values)
	{
		FElysiumCastCinematic Row;
		const TSharedPtr<FJsonObject>* Record=nullptr;
		if (!Pair.Value->TryGetObject(Record) || !Record || !FJsonObjectConverter::JsonObjectToUStruct(Record->ToSharedRef(),&Row,0,0,true))
		{ OutError=TEXT("invalid cinematic row: ")+Pair.Key; return nullptr; }
		Cinematics.Add(Pair.Key,MoveTemp(Row));
	}
	UElysiumExpressionTables* ExpressionTables = nullptr;
	if (Object->HasField(TEXT("expressionTablesAsset")))
	{
		FString Path;
		if (!Object->TryGetStringField(TEXT("expressionTablesAsset"), Path)
			|| Path != TEXT("/ElysiumBaked/ExpressionTables/_Corpus/DA_ExpressionTables"))
		{ OutError = TEXT("invalid cast expressionTablesAsset package path"); return nullptr; }
		ExpressionTables = LoadObject<UElysiumExpressionTables>(nullptr,
			TEXT("/ElysiumBaked/ExpressionTables/_Corpus/DA_ExpressionTables.DA_ExpressionTables"));
		if (!ExpressionTables || ExpressionTables->Tables.IsEmpty())
		{ OutError = TEXT("cast expression corpus is absent or empty"); return nullptr; }
	}
	Asset->Models=MoveTemp(Models); Asset->Aliases=MoveTemp(Aliases);
	Asset->AmbiguousAliases=MoveTemp(Ambiguous); Asset->Cinematics=MoveTemp(Cinematics);
	Asset->ExpressionTables = ExpressionTables;
	Asset->AuthoringEvidence=Json; Asset->MarkPackageDirty(); OutError.Reset();
	return Asset;
#else
	OutError=TEXT("editor only"); return nullptr;
#endif
}

FString UElysiumCastData::Verify(UElysiumCastData* Asset, const FString& Json)
{
	auto* Expected=NewObject<UElysiumCastData>(); FString Error;
	if (!Asset || !ApplyJson(Expected,Json,Error)) return Error.IsEmpty()?TEXT("cast asset is absent"):Error;
	for (const TCHAR* Name : {TEXT("Models"),TEXT("Aliases"),TEXT("AmbiguousAliases"),TEXT("Cinematics"),TEXT("ExpressionTables")})
	{
		const FProperty* Field=FindFProperty<FProperty>(StaticClass(),Name);
		if (!Field || !Field->Identical_InContainer(Asset,Expected)) return FString(TEXT("saved cast differs in "))+Name;
	}
	return FString();
}
