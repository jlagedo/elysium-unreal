#include "ElysiumMaterialProvenance.h"

#include "ElysiumJsonField.h"

#include "Dom/JsonObject.h"
#include "Materials/MaterialInterface.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#if WITH_EDITORONLY_DATA
#include "UObject/MetaData.h"
#endif

using namespace ElysiumJson;

const FName UElysiumMaterialProvenance::TagAssetId(TEXT("ElysiumAssetId"));
const FName UElysiumMaterialProvenance::TagShaderProgram(TEXT("ElysiumShaderProgram"));
const FName UElysiumMaterialProvenance::TagMaster(TEXT("ElysiumMaster"));
const FName UElysiumMaterialProvenance::TagSurfaceClass(TEXT("ElysiumSurfaceClass"));

namespace
{
	void ReadParameters(const TSharedRef<FJsonObject>& O, TArray<FElysiumMaterialParameter>& Out)
	{
		Out.Reset();
		const TArray<TSharedPtr<FJsonValue>>* Rows = Arr(O, TEXT("parameters"));
		if (!Rows)
		{
			return;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Rows)
		{
			const TSharedPtr<FJsonObject>* RowPtr = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(RowPtr) || !RowPtr)
			{
				continue;
			}
			const TSharedRef<FJsonObject> Row = (*RowPtr).ToSharedRef();
			FElysiumMaterialParameter& Parameter = Out.AddDefaulted_GetRef();
			Parameter.Index = static_cast<int32>(Int(Row, TEXT("index"), Out.Num() - 1));
			Parameter.Block = Str(Row, TEXT("block"));
			Parameter.Key = Str(Row, TEXT("key"));
			Parameter.SourceKey = Str(Row, TEXT("sourceKey"));
			Parameter.Value = Str(Row, TEXT("value"));
			Parameter.ValueType = Str(Row, TEXT("valueType"));
			Parameter.Offset = static_cast<int32>(Int(Row, TEXT("offset")));
		}
	}

	void ReadBlocks(const TSharedRef<FJsonObject>& O, TArray<FElysiumMaterialBlock>& Out)
	{
		Out.Reset();
		const TArray<TSharedPtr<FJsonValue>>* Rows = Arr(O, TEXT("blocks"));
		if (!Rows)
		{
			return;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Rows)
		{
			const TSharedPtr<FJsonObject>* RowPtr = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(RowPtr) || !RowPtr)
			{
				continue;
			}
			const TSharedRef<FJsonObject> Row = (*RowPtr).ToSharedRef();
			FElysiumMaterialBlock& Block = Out.AddDefaulted_GetRef();
			Block.Name = Str(Row, TEXT("name"));
			Block.SourceName = Str(Row, TEXT("sourceName"));
			Block.Path = Str(Row, TEXT("path"));
			Block.Parent = Str(Row, TEXT("parent"));
		}
	}

	void ReadProxies(const TSharedRef<FJsonObject>& O, TArray<FElysiumMaterialProxy>& Out)
	{
		Out.Reset();
		const TArray<TSharedPtr<FJsonValue>>* Rows = Arr(O, TEXT("proxies"));
		if (!Rows)
		{
			return;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Rows)
		{
			const TSharedPtr<FJsonObject>* RowPtr = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(RowPtr) || !RowPtr)
			{
				continue;
			}
			const TSharedRef<FJsonObject> Row = (*RowPtr).ToSharedRef();
			FElysiumMaterialProxy& Proxy = Out.AddDefaulted_GetRef();
			Proxy.Kind = Str(Row, TEXT("kind"));
			Proxy.Name = Str(Row, TEXT("name"));
			Proxy.SourceName = Str(Row, TEXT("sourceName"));
			if (const TArray<TSharedPtr<FJsonValue>>* Indices = Arr(Row, TEXT("parameterIndices")))
			{
				for (const TSharedPtr<FJsonValue>& IndexValue : *Indices)
				{
					double Number = 0.0;
					if (IndexValue.IsValid() && IndexValue->TryGetNumber(Number))
					{
						Proxy.ParameterIndices.Add(static_cast<int32>(Number));
					}
				}
			}
			if (TSharedPtr<FJsonObject> Arguments = Obj(Row, TEXT("arguments")))
			{
				for (const auto& Field : Arguments->Values)
				{
					FString ArgValue;
					if (Field.Value.IsValid() && Field.Value->TryGetString(ArgValue))
					{
						Proxy.Arguments.Add(FString(Field.Key), ArgValue);
					}
				}
			}
			Proxy.Destination = Str(Row, TEXT("destination"));
		}
	}

	void ReadResolvedPrograms(const TSharedRef<FJsonObject>& O, TArray<FElysiumMaterialProgram>& Out)
	{
		Out.Reset();
		TSharedPtr<FJsonObject> Resolution = Obj(O, TEXT("shaderResolution"));
		if (!Resolution)
		{
			return;
		}
		const TArray<TSharedPtr<FJsonValue>>* Rows = Arr(Resolution.ToSharedRef(), TEXT("resolvedPrograms"));
		if (!Rows)
		{
			return;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Rows)
		{
			const TSharedPtr<FJsonObject>* RowPtr = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(RowPtr) || !RowPtr)
			{
				continue;
			}
			const TSharedRef<FJsonObject> Row = (*RowPtr).ToSharedRef();
			FElysiumMaterialProgram& Program = Out.AddDefaulted_GetRef();
			Program.PixelShader = Str(Row, TEXT("pixelShader"));
			Program.VertexShader = Str(Row, TEXT("vertexShader"));
			Program.Condition = Str(Row, TEXT("condition"), TEXT("default"));
			Program.DrawPass = Str(Row, TEXT("drawPass"));
		}
	}

	void ReadTextureBindings(const TSharedRef<FJsonObject>& O, TArray<FElysiumMaterialTextureBinding>& Out)
	{
		Out.Reset();
		const TArray<TSharedPtr<FJsonValue>>* Rows = Arr(O, TEXT("textureBindings"));
		if (!Rows)
		{
			return;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Rows)
		{
			const TSharedPtr<FJsonObject>* RowPtr = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(RowPtr) || !RowPtr)
			{
				continue;
			}
			const TSharedRef<FJsonObject> Row = (*RowPtr).ToSharedRef();
			FElysiumMaterialTextureBinding& Binding = Out.AddDefaulted_GetRef();
			Binding.Parameter = Str(Row, TEXT("parameter"));
			Binding.Value = Str(Row, TEXT("value"));
			Binding.Kind = Str(Row, TEXT("kind"));
			Binding.Asset = Str(Row, TEXT("asset"));
			Binding.Resolved = Bool(Row, TEXT("resolved"));
			Binding.UsedLinearTwin = Bool(Row, TEXT("usedLinearTwin"));
		}
	}

	void ReadDependencies(const TSharedRef<FJsonObject>& O, TArray<FElysiumMaterialDependency>& Out)
	{
		Out.Reset();
		const TArray<TSharedPtr<FJsonValue>>* Rows = Arr(O, TEXT("dependencies"));
		if (!Rows)
		{
			return;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Rows)
		{
			const TSharedPtr<FJsonObject>* RowPtr = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(RowPtr) || !RowPtr)
			{
				continue;
			}
			const TSharedRef<FJsonObject> Row = (*RowPtr).ToSharedRef();
			FElysiumMaterialDependency& Dependency = Out.AddDefaulted_GetRef();
			Dependency.Role = Str(Row, TEXT("role"));
			Dependency.Parameter = Str(Row, TEXT("parameter"));
			Dependency.Asset = Str(Row, TEXT("asset"));
			Dependency.Resolved = Bool(Row, TEXT("resolved"));
		}
	}
}

void UElysiumMaterialProvenance::FromJson(const TSharedRef<FJsonObject>& O)
{
	// --- identity ---
	AssetId = Str(O, TEXT("assetId"));
	MaterialPath = Str(O, TEXT("materialPath"));
	AssetPath = Str(O, TEXT("assetPath"));
	UnitGlb = Str(O, TEXT("unitGlb"));
	UnitSchemaVersion = Str(O, TEXT("unitSchemaVersion"));
	UnitSha256 = Str(O, TEXT("unitSha256"));
	SourceSha256 = Str(O, TEXT("sourceSha256"));
	SettingsVersion = Str(O, TEXT("settingsVersion"));

	// --- shader ---
	Shader = Str(O, TEXT("shader"));
	SourceShader = Str(O, TEXT("sourceShader"));
	if (TSharedPtr<FJsonObject> Resolution = Obj(O, TEXT("shaderResolution")))
	{
		const TSharedRef<FJsonObject> ResolutionRef = Resolution.ToSharedRef();
		ResolvedFamily = Str(ResolutionRef, TEXT("family"));
		ResolutionInputs = Strings(ResolutionRef, TEXT("resolutionInputs"));
		ResolutionReason = Str(ResolutionRef, TEXT("resolutionReason"));
	}
	ReadResolvedPrograms(O, ResolvedPrograms);

	// --- build decisions ---
	Master = Str(O, TEXT("master"));
	BlendMode = Str(O, TEXT("blendMode"));
	TwoSided = Bool(O, TEXT("twoSided"));
	SurfaceClass = FName(*Str(O, TEXT("surfaceClass")));
	SurfaceClassIndex = static_cast<int32>(Int(O, TEXT("surfaceClassIndex")));
	EnvMapSymbol = Str(O, TEXT("envMapSymbol"));
	EnvMapAssetId = Str(O, TEXT("envMapAssetId"));
	EnvMapProbePath = FSoftObjectPath(Str(O, TEXT("envMapProbePath")));
	SurfacePropertyAsset = Str(O, TEXT("surfacePropertyAsset"));
	if (TSharedPtr<FJsonObject> Patch = Obj(O, TEXT("patch")))
	{
		const TSharedRef<FJsonObject> PatchRef = Patch.ToSharedRef();
		PatchOf = Str(PatchRef, TEXT("asset"));
		PatchKind = Str(PatchRef, TEXT("kind"));
	}

	// --- content ---
	ReadParameters(O, Parameters);
	ReadBlocks(O, Blocks);
	ReadProxies(O, Proxies);
	ReadTextureBindings(O, TextureBindings);
	ReadDependencies(O, Dependencies);
	Anomalies = Strings(O, TEXT("anomalies"));
	Omissions = Strings(O, TEXT("omissions"));
	Comments = Strings(O, TEXT("comments"));
	if (TSharedPtr<FJsonObject> Coverage = Obj(O, TEXT("coverage")))
	{
		CoveragePercent = Float(Coverage.ToSharedRef(), TEXT("percent"));
	}
}

UElysiumMaterialProvenance* UElysiumMaterialProvenance::ApplyJson(
	UMaterialInterface* Material, const FString& Json, FString& OutError)
{
	OutError.Reset();
	if (!Material)
	{
		OutError = TEXT("no material");
		return nullptr;
	}
	// Deserialize the top-level value rather than an object, so a well-formed document that is not
	// an object is refused by type rather than by whatever the object overload happens to do.
	TSharedPtr<FJsonValue> Parsed;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Parsed) || !Parsed.IsValid())
	{
		OutError = FString::Printf(TEXT("provenance sidecar does not parse as JSON: %s"), *Reader->GetErrorMessage());
		return nullptr;
	}
	const TSharedPtr<FJsonObject>* Object = nullptr;
	if (!Parsed->TryGetObject(Object) || !Object || !Object->IsValid())
	{
		OutError = TEXT("provenance sidecar is not a JSON object");
		return nullptr;
	}

	// The material is the outer, so the record serializes inside the asset's package; a flag-less
	// NewObject would land it in the transient package and the save would drop it.
	UElysiumMaterialProvenance* Record = NewObject<UElysiumMaterialProvenance>(Material, NAME_None, RF_Public | RF_Transactional);
	Record->FromJson(Object->ToSharedRef());
	// AddAssetUserData removes an existing instance of the same class first, so a re-import
	// replaces rather than accumulates.
	Material->AddAssetUserData(Record);
	Material->MarkPackageDirty();
	return Record;
}

const UElysiumMaterialProvenance* UElysiumMaterialProvenance::Find(const UMaterialInterface* Material)
{
	if (!Material)
	{
		return nullptr;
	}
	// GetAssetUserDataOfClass is non-const on the interface; the lookup itself mutates nothing.
	return Cast<UElysiumMaterialProvenance>(
		const_cast<UMaterialInterface*>(Material)->GetAssetUserDataOfClass(UElysiumMaterialProvenance::StaticClass()));
}

void UElysiumMaterialProvenance::StampRegistryTags(UMaterialInterface* Material, bool& bOutStamped, FString& OutError)
{
	bOutStamped = false;
	OutError.Reset();
	const UElysiumMaterialProvenance* Record = Find(Material);
	if (!Record)
	{
		OutError = TEXT("material carries no ElysiumMaterialProvenance");
		return;
	}
#if WITH_EDITORONLY_DATA
	UPackage* Package = Material->GetPackage();
	if (!Package)
	{
		OutError = TEXT("material has no package");
		return;
	}
	// The default-condition pixel program's name, or the resolved family when the shader did not
	// resolve to a concrete program pair (docs/architecture/seam_map_material.md -> "Provenance").
	FString ShaderProgram = Record->ResolvedFamily;
	for (const FElysiumMaterialProgram& Program : Record->ResolvedPrograms)
	{
		if (Program.Condition == TEXT("default"))
		{
			ShaderProgram = Program.PixelShader;
			break;
		}
	}
	if (ShaderProgram.IsEmpty() && Record->ResolvedPrograms.Num() > 0)
	{
		ShaderProgram = Record->ResolvedPrograms[0].PixelShader;
	}

	FMetaData& Meta = Package->GetMetaData();
	Meta.SetValue(Material, TagAssetId, *Record->AssetId);
	Meta.SetValue(Material, TagShaderProgram, *ShaderProgram);
	Meta.SetValue(Material, TagMaster, *Record->Master);
	Meta.SetValue(Material, TagSurfaceClass, *Record->SurfaceClass.ToString());
	Material->MarkPackageDirty();
	bOutStamped = true;
#else
	OutError = TEXT("package metadata is editor-only data");
#endif
}
