#include "ElysiumSurfacePropertyProvenance.h"

#include "ElysiumJsonField.h"

void UElysiumSurfacePropertyProvenance::FromJson(const TSharedRef<FJsonObject>& Object)
{
	using namespace ElysiumJson;

	AssetId = Str(Object, TEXT("assetId"));
	Name = Str(Object, TEXT("name"));
	SourceName = Str(Object, TEXT("sourceName"));
	AssetPath = Str(Object, TEXT("assetPath"));
	UnitGlb = Str(Object, TEXT("unitGlb"));
	UnitSchemaVersion = Str(Object, TEXT("unitSchemaVersion"));
	UnitSha256 = Str(Object, TEXT("unitSha256"));
	SettingsVersion = Str(Object, TEXT("settingsVersion"));

	BaseChain = Strings(Object, TEXT("baseChain"));
	Anomalies = Strings(Object, TEXT("anomalies"));

	FieldOrigins.Reset();
	if (const TSharedPtr<FJsonObject> Origins = Obj(Object, TEXT("fieldOrigins")))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Row : Origins->Values)
		{
			FString Unit;
			if (Row.Value.IsValid() && Row.Value->TryGetString(Unit))
			{
				FieldOrigins.Add(Row.Key, Unit);
			}
		}
	}

	CoveragePercent = 0.0f;
	Unresolved.Reset();
	Unsupported.Reset();
	if (const TSharedPtr<FJsonObject> Coverage = Obj(Object, TEXT("coverage")))
	{
		const TSharedRef<FJsonObject> Ref = Coverage.ToSharedRef();
		CoveragePercent = Float(Ref, TEXT("percent"));
		Unresolved = Strings(Ref, TEXT("unresolved"));
		Unsupported = Strings(Ref, TEXT("unsupported"));
	}
}
