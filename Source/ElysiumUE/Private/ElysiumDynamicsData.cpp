#include "ElysiumDynamicsData.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"
#if WITH_EDITOR
#include "JsonObjectConverter.h"

namespace
{
	template<typename T>
	bool ReadRows(const TSharedPtr<FJsonObject>& Object,const TCHAR* Field,TArray<T>& Out,FString& Error)
	{
		const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
		if (!Object->TryGetArrayField(Field,Rows) || !Rows) { Error=FString(TEXT("missing dynamics field: "))+Field; return false; }
		for (const auto& Value : *Rows)
		{
			const TSharedPtr<FJsonObject>* Row=nullptr; T Converted;
			if (!Value->TryGetObject(Row) || !Row
				|| !FJsonObjectConverter::JsonObjectToUStruct(Row->ToSharedRef(),&Converted,0,0,true))
			{ Error=FString(TEXT("invalid dynamics row: "))+Field; return false; }
			Out.Add(MoveTemp(Converted));
		}
		return true;
	}
	template<typename T>
	bool ValidRecipe(const T& Row,float MaxCone)
	{
		return !Row.BoundBone.IsNone() && FMath::IsFinite(Row.GravityScale) && Row.GravityScale>=0.f
			&& FMath::IsFinite(Row.Damping) && Row.Damping>=.7f && Row.Damping<=1.f
			&& FMath::IsFinite(Row.AngularSpring) && Row.AngularSpring>=0.f
			&& FMath::IsFinite(Row.ConeAngleDegrees) && Row.ConeAngleDegrees>=0.f && Row.ConeAngleDegrees<=MaxCone;
	}
}
#endif

UElysiumDynamicsData* UElysiumDynamicsData::ApplyJson(UElysiumDynamicsData* Asset,const FString& Json,FString& OutError)
{
#if WITH_EDITOR
	TSharedPtr<FJsonObject> Object;
	if (!Asset || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Object) || !Object.IsValid())
	{ OutError=TEXT("invalid dynamics JSON or absent asset"); return nullptr; }
	FString Version,Id,Policy;
	int32 SourceCount=INDEX_NONE;
	if (!Object->TryGetStringField(TEXT("schemaVersion"),Version) || Version!=TEXT("1.0.0")
		|| !Object->TryGetStringField(TEXT("assetId"),Id) || !Id.StartsWith(TEXT("vtmb:model:"))
		|| !Object->TryGetStringField(TEXT("projectionPolicy"),Policy) || Policy!=TEXT("provisional-animdynamics-v1")
		|| !Object->TryGetNumberField(TEXT("sourceRecordCount"),SourceCount))
	{ OutError=TEXT("invalid dynamics schema/identity/policy/count"); return nullptr; }
	TArray<FElysiumSecondaryMotionRecord> Records;
	TArray<FElysiumHairDynamicsChainConfig> Chains;
	TArray<FElysiumHairDynamicsBodyConfig> Bodies;
	if (!ReadRows(Object,TEXT("records"),Records,OutError) || !ReadRows(Object,TEXT("chains"),Chains,OutError)
		|| !ReadRows(Object,TEXT("bodies"),Bodies,OutError)) return nullptr;
	if (Records.Num()!=SourceCount) { OutError=TEXT("dynamics projection lost a source record"); return nullptr; }
	TSet<int32> UsedChains,UsedBodies;
	for (const auto& Row : Records)
	{
		if (Row.BoneIndices.IsEmpty() || Row.BoneIndices.Num()!=Row.BoneNames.Num()
			|| Row.BoneIndices[0]!=Row.FirstBone || (Row.TerminalBone>=0 && Row.BoneIndices.Last()!=Row.TerminalBone)
			|| !FMath::IsFinite(Row.UnusedAuthoredPreset) || !FMath::IsFinite(Row.Gravity)
			|| !FMath::IsFinite(Row.Damping) || !FMath::IsFinite(Row.SpringExponent) || !FMath::IsFinite(Row.MaxAngleDegrees))
		{ OutError=TEXT("invalid source dynamics declaration or resolved walk"); return nullptr; }
		if (Row.Projection==TEXT("chain"))
		{
			if (!Chains.IsValidIndex(Row.RecipeIndex) || UsedChains.Contains(Row.RecipeIndex)
				|| Row.BoneNames.Num()<2 || Chains[Row.RecipeIndex].BoundBone!=Row.BoneNames[0]
				|| Chains[Row.RecipeIndex].ChainEnd!=Row.BoneNames.Last() || !ValidRecipe(Chains[Row.RecipeIndex],179.f))
			{ OutError=TEXT("invalid or multiply-owned dynamics chain recipe"); return nullptr; }
			UsedChains.Add(Row.RecipeIndex);
		}
		else if (Row.Projection==TEXT("breast-body"))
		{
			if (!Bodies.IsValidIndex(Row.RecipeIndex) || UsedBodies.Contains(Row.RecipeIndex)
				|| Row.BoneNames.Num()!=1 || Bodies[Row.RecipeIndex].BoundBone!=Row.BoneNames[0]
				|| !ValidRecipe(Bodies[Row.RecipeIndex],90.f))
			{ OutError=TEXT("invalid or multiply-owned dynamics body recipe"); return nullptr; }
			UsedBodies.Add(Row.RecipeIndex);
		}
		else if (Row.Projection!=TEXT("source-only") || Row.RecipeIndex!=INDEX_NONE || Row.Reason.IsEmpty())
		{ OutError=TEXT("unclassified source dynamics record"); return nullptr; }
	}
	if (UsedChains.Num()!=Chains.Num() || UsedBodies.Num()!=Bodies.Num())
	{ OutError=TEXT("dynamics recipe has no source declaration"); return nullptr; }
	Asset->AssetId=Id; Asset->ProjectionPolicy=Policy; Asset->Records=MoveTemp(Records);
	Asset->Chains=MoveTemp(Chains); Asset->Bodies=MoveTemp(Bodies);
	Asset->AuthoringEvidence=Json; Asset->MarkPackageDirty(); OutError.Reset(); return Asset;
#else
	OutError=TEXT("editor only"); return nullptr;
#endif
}

FString UElysiumDynamicsData::Verify(UElysiumDynamicsData* Asset,const FString& Json)
{
	auto* Expected=NewObject<UElysiumDynamicsData>(); FString Error;
	if (!Asset || !ApplyJson(Expected,Json,Error)) return Error.IsEmpty()?TEXT("dynamics asset is absent"):Error;
	for (const TCHAR* Name : {TEXT("AssetId"),TEXT("ProjectionPolicy"),TEXT("Records"),TEXT("Chains"),TEXT("Bodies")})
	{
		const auto* Field=FindFProperty<FProperty>(StaticClass(),Name);
		if (!Field || !Field->Identical_InContainer(Asset,Expected)) return FString(TEXT("saved dynamics differs in "))+Name;
	}
	return FString();
}
