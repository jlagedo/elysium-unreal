#include "ElysiumCookRoot.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetBundleData.h"
#include "Engine/AssetManager.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#endif

UElysiumCookRoot::UElysiumCookRoot()
{
	bLabelAssetsInMyDirectory = false;
	bIsRuntimeLabel = false;
	bIncludeRedirectors = false;
	Rules.Priority = 1;
	Rules.ChunkId = -1;
	Rules.bApplyRecursively = true;
	Rules.CookRule = EPrimaryAssetCookRule::AlwaysCook;
}

FPrimaryAssetId UElysiumCookRoot::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(FPrimaryAssetType(TEXT("ElysiumR8CookRoot")), GetFName());
}

FString UElysiumCookRoot::PackagePath()
{
	// Python twin: asset_paths.corpus_path("model", "DA", "R8CookRoot").
	return TEXT("/ElysiumBaked/Models/_Corpus/DA_R8CookRoot");
}

#if WITH_EDITOR
namespace
{
	struct FPlan
	{
		TArray<TSoftObjectPtr<UObject>> Targets;
		FString Evidence, Inputs, Inventory;
	};
	bool Hash(const FString& Text)
	{
		if (Text.Len() != 64) return false;
		for (TCHAR C : Text) if (!((C >= '0' && C <= '9') || (C >= 'a' && C <= 'f'))) return false;
		return true;
	}
	bool Fail(FString& Error, const FString& Reason) { Error = TEXT("R8 cook root: ") + Reason; return false; }
	bool Read(const FString& Json, FPlan& Plan, FString& Error)
	{
		TSharedPtr<FJsonObject> Object;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object) || !Object) return Fail(Error, TEXT("invalid JSON"));
		FString Version, Producer, Type, Path, PhysicsScope; bool bReady = false;
		if (!Object->TryGetStringField(TEXT("schemaVersion"), Version) || Version != TEXT("1.0.0")) return Fail(Error, TEXT("schemaVersion"));
		if (!Object->TryGetStringField(TEXT("producer"), Producer) || Producer != TEXT("r8-cook-roots")) return Fail(Error, TEXT("producer"));
		if (!Object->TryGetStringField(TEXT("primaryAssetType"), Type) || Type != TEXT("ElysiumR8CookRoot")) return Fail(Error, TEXT("primaryAssetType"));
		if (!Object->TryGetStringField(TEXT("assetPath"), Path) || Path != UElysiumCookRoot::PackagePath()) return Fail(Error, TEXT("assetPath: expected canonical package path"));
		if (!Object->TryGetStringField(TEXT("physicsScope"), PhysicsScope) || PhysicsScope != TEXT("export-import-data-conservation")) return Fail(Error, TEXT("physicsScope"));
		if (!Object->TryGetBoolField(TEXT("readyToPublish"), bReady) || !bReady) return Fail(Error, TEXT("unresolved publication issues"));
		const TArray<TSharedPtr<FJsonValue>> *Issues = nullptr, *Targets = nullptr;
		if (!Object->TryGetArrayField(TEXT("issues"), Issues) || !Issues->IsEmpty()) return Fail(Error, TEXT("issues must be empty"));
		if (!Object->TryGetStringField(TEXT("sourceEvidenceJson"), Plan.Evidence) || Plan.Evidence.IsEmpty()
			|| !Object->TryGetStringField(TEXT("inputDigest"), Plan.Inputs) || !Hash(Plan.Inputs)
			|| !Object->TryGetStringField(TEXT("inventoryDigest"), Plan.Inventory) || !Hash(Plan.Inventory)) return Fail(Error, TEXT("source evidence/digests"));
		if (!Object->TryGetArrayField(TEXT("targets"), Targets)) return Fail(Error, TEXT("targets"));
		TSet<FString> Packages;
		for (const auto& Value : *Targets)
		{
			const TSharedPtr<FJsonObject>* Row = nullptr; FString Package, ObjectPath;
			if (!Value->TryGetObject(Row) || !Row || !(*Row)->TryGetStringField(TEXT("packagePath"), Package)
				|| !(*Row)->TryGetStringField(TEXT("objectPath"), ObjectPath)) return Fail(Error, TEXT("invalid target row"));
			if (!(Package.StartsWith(TEXT("/ElysiumBaked/Models/"), ESearchCase::CaseSensitive)
				|| Package.StartsWith(TEXT("/ElysiumBaked/ExpressionTables/"), ESearchCase::CaseSensitive))
				|| Package == UElysiumCookRoot::PackagePath() || !FPackageName::IsValidLongPackageName(Package)
				|| ObjectPath != Package + TEXT(".") + FPackageName::GetShortName(Package) || Packages.Contains(Package))
				return Fail(Error, TEXT("noncanonical/duplicate/self target: ") + ObjectPath);
			Packages.Add(Package);
			Plan.Targets.Add(TSoftObjectPtr<UObject>(FSoftObjectPath(ObjectPath)));
		}
		for (const TCHAR* Required : {TEXT("/ElysiumBaked/Models/_Corpus/DA_Cast"),
			TEXT("/ElysiumBaked/ExpressionTables/_Corpus/DA_ExpressionTables"),
			TEXT("/ElysiumBaked/Models/_Corpus/DA_WieldModels"), TEXT("/ElysiumBaked/Models/_Corpus/DA_PlacedModels"),
			TEXT("/ElysiumBaked/Models/_Corpus/DA_PropSkins")})
			if (!Packages.Contains(Required)) return Fail(Error, FString(TEXT("required global absent: ")) + Required);
		return true;
	}
}
#endif

UElysiumCookRoot* UElysiumCookRoot::ApplyJson(UElysiumCookRoot* Asset, const FString& Json, FString& OutError)
{
	OutError.Reset();
#if WITH_EDITOR
	FPlan Plan;
	if (!Asset) { OutError = TEXT("R8 cook root: absent label"); return nullptr; }
	if (!Read(Json, Plan, OutError)) return nullptr;
	Asset->ExplicitAssets = MoveTemp(Plan.Targets); Asset->ExplicitBlueprints.Empty();
	Asset->AssetCollection.CollectionName = NAME_None;
	Asset->bLabelAssetsInMyDirectory = false; Asset->bIncludeRedirectors = false; Asset->bIsRuntimeLabel = false;
	Asset->Rules.Priority = 1; Asset->Rules.ChunkId = -1; Asset->Rules.bApplyRecursively = true;
	Asset->Rules.CookRule = EPrimaryAssetCookRule::AlwaysCook;
	Asset->SourceEvidenceJson = MoveTemp(Plan.Evidence); Asset->InputDigest = MoveTemp(Plan.Inputs); Asset->InventoryDigest = MoveTemp(Plan.Inventory);
	// No target UObject is loaded. Save/update creates Explicit asset-bundle metadata.
	if (Asset->GetOutermost() != GetTransientPackage()) Asset->UpdateAssetBundleData();
	Asset->MarkPackageDirty(); return Asset;
#else
	OutError = TEXT("R8 cook root authoring is editor only"); return nullptr;
#endif
}

FString UElysiumCookRoot::Verify(UElysiumCookRoot* Asset, const FString& Json)
{
#if WITH_EDITOR
	FPlan Plan; FString Error;
	if (!Asset) return TEXT("R8 cook root: absent label");
	if (!Read(Json, Plan, Error)) return Error;
	if (Asset->bLabelAssetsInMyDirectory || Asset->bIncludeRedirectors || Asset->bIsRuntimeLabel
		|| !Asset->ExplicitBlueprints.IsEmpty() || Asset->AssetCollection.CollectionName != NAME_None
		|| Asset->Rules.Priority != 1 || Asset->Rules.ChunkId != -1 || !Asset->Rules.bApplyRecursively
		|| Asset->Rules.CookRule != EPrimaryAssetCookRule::AlwaysCook) return TEXT("R8 cook root: unexpected directory/collection/runtime/cook rules");
	if (Asset->ExplicitAssets != Plan.Targets || Asset->SourceEvidenceJson != Plan.Evidence
		|| Asset->InputDigest != Plan.Inputs || Asset->InventoryDigest != Plan.Inventory) return TEXT("R8 cook root: saved inventory/evidence differs");
	return FString();
#else
	return TEXT("R8 cook root verification is editor only");
#endif
}

FString UElysiumCookRoot::VerifyCookRules(const FString& Json)
{
#if WITH_EDITOR
	FPlan Plan; FString Error;
	if (!Read(Json, Plan, Error)) return Error;
	if (!UAssetManager::IsInitialized()) return TEXT("R8 cook root: AssetManager is not initialized");
	auto& Manager = UAssetManager::Get(); FPrimaryAssetTypeInfo Info;
	const FPrimaryAssetId Id(FPrimaryAssetType(TEXT("ElysiumR8CookRoot")), FName(*FPackageName::GetShortName(PackagePath())));
	if (!Manager.GetPrimaryAssetTypeInfo(Id.PrimaryAssetType, Info) || Info.bIsEditorOnly
		|| Info.Rules.CookRule != EPrimaryAssetCookRule::AlwaysCook || !Info.Rules.bApplyRecursively)
		return TEXT("R8 cook root: register the dedicated type with bIsEditorOnly=False, AlwaysCook and recursive rules");
	const FSoftObjectPath ObjectPath(PackagePath() + TEXT(".") + FPackageName::GetShortName(PackagePath()));
	// The manager's primary-asset map was built from the config at editor start; a label saved
	// by this same process (the first publication, or a re-authored one) is not in it until the
	// configured specific asset is rescanned. Synchronous, so the check below reads the saved file.
	Manager.ScanPathsForPrimaryAssets(Id.PrimaryAssetType, { PackagePath() }, UElysiumCookRoot::StaticClass(),
		/*bHasBlueprintClasses*/ false, /*bIsEditorOnly*/ false, /*bForceSynchronousScan*/ true);
	if (Manager.GetPrimaryAssetIdForPath(ObjectPath) != Id) return TEXT("R8 cook root: canonical label is not scanned");
	// This must also pass in a fresh editor BEFORE loading the label: custom primary
	// types use saved bundle metadata and type rules; only stock PrimaryAssetLabel
	// types are automatically loaded by ApplyPrimaryAssetLabels during cooking.
	TArray<FAssetBundleEntry> Bundles;
	if (!Manager.GetAssetBundleEntries(Id, Bundles)) return TEXT("R8 cook root: no saved AssetBundles metadata");
	TSet<FTopLevelAssetPath> Actual, Expected;
	for (const auto& Ref : Plan.Targets) Expected.Add(Ref.ToSoftObjectPath().GetAssetPath());
	for (const auto& Bundle : Bundles)
	{
		if (Bundle.BundleName != FName(TEXT("Explicit")) && !Bundle.AssetPaths.IsEmpty()) return TEXT("R8 cook root: unexpected saved bundle");
		for (const auto& Path : Bundle.AssetPaths) Actual.Add(Path);
	}
	if (Actual.Num() != Expected.Num()) return TEXT("R8 cook root: saved bundle count differs");
	for (const auto& Path : Expected) if (!Actual.Contains(Path)) return TEXT("R8 cook root: saved bundle lost ") + Path.ToString();
	Manager.UpdateManagementDatabase(EUpdateManagementDatabaseFlags::BuildChunkMap | EUpdateManagementDatabaseFlags::ForceRefresh);
	for (const auto& Ref : Plan.Targets)
	{
		const FName Package(*Ref.ToSoftObjectPath().GetLongPackageName());
		if (Manager.GetPackageCookRule(Package) != EPrimaryAssetCookRule::AlwaysCook)
			return TEXT("R8 cook root: target is not AlwaysCook: ") + Package.ToString();
	}
	return FString();
#else
	return TEXT("R8 cook rule verification is editor only");
#endif
}
