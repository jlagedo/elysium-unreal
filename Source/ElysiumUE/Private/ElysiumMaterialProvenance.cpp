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
	/**
	 * A JSON field's value as text regardless of its JSON type: `pipeline/importers/materials.py`'s
	 * `parameters[].value` carries the VMT's own scalar/string value straight through
	 * (`str(row.get("value") or "")` on the Python stage side becomes whatever JSON type the
	 * original value parsed as -- a bare number for `"0.5"`, a string for `"brick/floora"`), and
	 * `Value` here is a provenance record, not a typed binding: it stores the text either way.
	 */
	FString StringifyField(const TSharedRef<FJsonObject>& Object, const TCHAR* Key)
	{
		const TSharedPtr<FJsonValue> Value = Object->TryGetField(Key);
		if (!Value.IsValid())
		{
			return FString();
		}
		FString Out;
		if (Value->TryGetString(Out))
		{
			return Out;
		}
		double Number = 0.0;
		if (Value->TryGetNumber(Number))
		{
			return FString::SanitizeFloat(Number);
		}
		bool bBool = false;
		if (Value->TryGetBool(bBool))
		{
			return bBool ? TEXT("true") : TEXT("false");
		}
		return FString();
	}

	/** Every row of `parameters[]`: `index`, `block`, `key`, `sourceKey`, `value`, `valueType`,
	 * `offset`, in source order, exactly as `stage_unit` writes them (`offset` wired through
	 * H-3; absent on a row the unit's own exporter did not carry one for, reading as 0). */
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
			Parameter.Value = StringifyField(Row, TEXT("value"));
			Parameter.ValueType = Str(Row, TEXT("valueType"));
			Parameter.Offset = static_cast<int32>(Int(Row, TEXT("offset")));
		}
	}

	/** `proxies[]`: `{index, kind, sourceName, arguments, destination}`, exactly as `_apply_proxies`
	 * writes them. `name` and `parameterIndices` are not part of this stage's sidecar. */
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
			Proxy.SourceName = Str(Row, TEXT("sourceName"));
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

	/** `textureBindings[]`: `{parameter, asset}`, exactly as `stage_unit` writes them. */
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
			Binding.Asset = Str(Row, TEXT("asset"));
		}
	}

	/** `materialReferences[]`: `{parameter, asset}`, exactly as `stage_unit` writes them -- this
	 * stage's sidecar carries no `dependencies` key at all. */
	void ReadMaterialReferences(const TSharedRef<FJsonObject>& O, TArray<FElysiumMaterialDependency>& Out)
	{
		Out.Reset();
		const TArray<TSharedPtr<FJsonValue>>* Rows = Arr(O, TEXT("materialReferences"));
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
			Dependency.Parameter = Str(Row, TEXT("parameter"));
			Dependency.Asset = Str(Row, TEXT("asset"));
		}
	}

	/** Every field of `Row` besides `SkipKey`, stringified the same tolerant way `Value` fields
	 * are (`StringifyField`) -- C-2's catch-all for an omission/anomaly row shape this reader has
	 * not been taught a dedicated field for yet. */
	void ReadExtraFields(const TSharedRef<FJsonObject>& Row, const TCHAR* SkipKey, TMap<FString, FString>& Out)
	{
		for (const auto& Field : Row->Values)
		{
			const FString Key(Field.Key);
			if (Key == SkipKey)
			{
				continue;
			}
			Out.Add(Key, StringifyField(Row, *Key));
		}
	}

	/** `omissions[]`: every row carries `reason`; every other field (`role`, `key`, `kind`, ...)
	 * lands in `Extra` (C-2). */
	void ReadOmissions(const TSharedRef<FJsonObject>& O, TArray<FElysiumProvenanceNote>& Out)
	{
		Out.Reset();
		const TArray<TSharedPtr<FJsonValue>>* Rows = Arr(O, TEXT("omissions"));
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
			FElysiumProvenanceNote& Note = Out.AddDefaulted_GetRef();
			Note.Reason = Str(Row, TEXT("reason"));
			ReadExtraFields(Row, TEXT("reason"), Note.Extra);
		}
	}

	/** `anomalies[]`: every row carries `kind`; every other field (`value`, `proxy`, `switch`,
	 * `target`, ...) lands in `Extra` (C-2). */
	void ReadAnomalies(const TSharedRef<FJsonObject>& O, TArray<FElysiumProvenanceAnomaly>& Out)
	{
		Out.Reset();
		const TArray<TSharedPtr<FJsonValue>>* Rows = Arr(O, TEXT("anomalies"));
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
			FElysiumProvenanceAnomaly& Anomaly = Out.AddDefaulted_GetRef();
			Anomaly.Kind = Str(Row, TEXT("kind"));
			ReadExtraFields(Row, TEXT("kind"), Anomaly.Extra);
		}
	}

	/** `comments[]`: `{offset, text}`, exactly as `stage_unit` writes them. */
	void ReadComments(const TSharedRef<FJsonObject>& O, TArray<FElysiumProvenanceComment>& Out)
	{
		Out.Reset();
		const TArray<TSharedPtr<FJsonValue>>* Rows = Arr(O, TEXT("comments"));
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
			FElysiumProvenanceComment& Comment = Out.AddDefaulted_GetRef();
			Comment.Offset = static_cast<int32>(Int(Row, TEXT("offset")));
			Comment.Text = Str(Row, TEXT("text"));
		}
	}
}

void UElysiumMaterialProvenance::FromJson(const TSharedRef<FJsonObject>& O)
{
	// --- identity --- AssetPath, UnitGlb, SourceSha256 and (further below) SurfacePropertyAsset
	// are not part of the provenance sidecar `stage_unit` writes -- they live on the manifest
	// entry, and `pipeline/unreal/import_materials.py` merges them into this same JSON object as
	// top-level keys before calling ApplyJson, so they read exactly like every other field here.
	AssetId = Str(O, TEXT("assetId"));
	MaterialPath = Str(O, TEXT("materialPath"));
	AssetPath = Str(O, TEXT("assetPath"));
	UnitGlb = Str(O, TEXT("unitGlb"));
	UnitSchemaVersion = Str(O, TEXT("unitSchemaVersion"));
	UnitSha256 = Str(O, TEXT("unitSha256"));
	// Review finding 9: the manifest entry key is `sourceMembersSha256` now (renamed from
	// `sourceSha256` -- a digest over every `sourceResolution.members[]` row, not one file), read
	// under its new name; `SourceSha256` (the `FString` member) keeps its existing name, since
	// only the JSON key changed here.
	SourceSha256 = Str(O, TEXT("sourceMembersSha256"));
	SettingsVersion = Str(O, TEXT("settingsVersion"));

	// --- shader --- top-level `shaderFamily`/`shaderResolved`, not a nested `shaderResolution`
	// object (this stage's sidecar carries no `resolvedPrograms`/`resolutionInputs`/`resolutionReason`).
	Shader = Str(O, TEXT("shader"));
	SourceShader = Str(O, TEXT("sourceShader"));
	ResolvedFamily = Str(O, TEXT("shaderFamily"));
	ShaderResolved = Bool(O, TEXT("shaderResolved"));

	// --- build decisions ---
	Master = Str(O, TEXT("master"));
	SkinnedAsset = Str(O, TEXT("skinnedAsset"));
	SkinnedMaster = Str(O, TEXT("skinnedMaster"));
	SkeletalConsumers.Reset(); StaticConsumers.Reset(); MapConsumers.Reset();
	if (const TSharedPtr<FJsonObject> Consumers = Obj(O, TEXT("consumers")))
	{
		SkeletalConsumers = Strings(Consumers.ToSharedRef(), TEXT("skeletal"));
		StaticConsumers = Strings(Consumers.ToSharedRef(), TEXT("static"));
		MapConsumers = Strings(Consumers.ToSharedRef(), TEXT("map"));
	}
	BlendMode = Str(O, TEXT("blendMode"));
	TwoSided = Bool(O, TEXT("twoSided"));
	SurfaceClass = FName(*Str(O, TEXT("surfaceClass")));
	SurfaceClassIndex = static_cast<int32>(Int(O, TEXT("surfaceClassIndex")));
	SurfaceClassSource = Str(O, TEXT("surfaceClassSource"));
	PhysMaterialFallback = Bool(O, TEXT("physMaterialFallback"));
	if (TSharedPtr<FJsonObject> Environment = Obj(O, TEXT("environment")))
	{
		const TSharedRef<FJsonObject> EnvironmentRef = Environment.ToSharedRef();
		EnvMapSymbol = Str(EnvironmentRef, TEXT("envMapSymbol"));
		EnvMapAssetId = Str(EnvironmentRef, TEXT("envMapAssetId"));
		EnvMapProbePath = FSoftObjectPath(Str(EnvironmentRef, TEXT("envMapProbePath")));
		// H-2: `envMapAsset` (an authored-fixed-cube instance's own bound EnvMap asset path) is a
		// different key from `envMapAssetId` (a patched unit's un-bound probe id) -- both live
		// under `environment`, never together on the same unit.
		EnvMapAsset = Str(EnvironmentRef, TEXT("envMapAsset"));
		bEnvMapTintChromatic = Bool(EnvironmentRef, TEXT("envMapTintChromatic"));
		bPatchedProbe = Bool(EnvironmentRef, TEXT("patchedProbe"));
	}
	SurfacePropertyAsset = Str(O, TEXT("physMaterial"));
	// `patchBase` (the base's own `vtmb:material:` id) is what PatchOf has always documented
	// itself as; `patchOf` in this stage's sidecar is a different thing entirely (a map-patched
	// copy's `{x, y, z}` probe-origin coordinate), so PatchOf reads patchBase, not patchOf.
	PatchOf = Str(O, TEXT("patchBase"));
	{
		TArray<FString> Operations = Strings(O, TEXT("patchKind"));
		PatchKind = FString::Join(Operations, TEXT(","));
	}

	// --- placement / map / runtime-factory ---
	IsDecalSurface = Bool(O, TEXT("isDecalSurface"));
	IgnoreZ = Bool(O, TEXT("ignoreZ"));
	if (const TArray<TSharedPtr<FJsonValue>>* Origin = Arr(O, TEXT("spriteOrigin")))
	{
		double X = 0.0, Y = 0.0;
		if (Origin->IsValidIndex(0) && (*Origin)[0].IsValid())
		{
			(*Origin)[0]->TryGetNumber(X);
		}
		if (Origin->IsValidIndex(1) && (*Origin)[1].IsValid())
		{
			(*Origin)[1]->TryGetNumber(Y);
		}
		SpriteOrigin = FVector2D(X, Y);
	}
	SpriteOrientation = Float(O, TEXT("spriteOrientation"));
	MinLight = Float(O, TEXT("minLight"));
	MaxLight = Float(O, TEXT("maxLight"));
	WetnessScale = Float(O, TEXT("wetnessScale"));
	SubdivSize = Float(O, TEXT("subdivSize"));
	Curve = Float(O, TEXT("curve"));

	// --- content --- `Blocks` is not part of this stage's sidecar; it stays empty.
	ReadParameters(O, Parameters);
	ReadProxies(O, Proxies);
	ReadTextureBindings(O, TextureBindings);
	ReadMaterialReferences(O, Dependencies);
	// C-2: `omissions`/`anomalies`/`comments` are arrays of objects (`{reason, role}`,
	// `{kind, value}`, `{offset, text}`, ...), not arrays of strings -- `ElysiumJson::Strings`
	// silently skips every non-string element, so the old `TArray<FString>` fields always read
	// empty against a real sidecar. `ReadOmissions`/`ReadAnomalies`/`ReadComments` above parse the
	// row shapes these stages actually write.
	ReadAnomalies(O, Anomalies);
	ReadOmissions(O, Omissions);
	ReadComments(O, Comments);
	if (TSharedPtr<FJsonObject> Coverage = Obj(O, TEXT("coverage")))
	{
		const TSharedRef<FJsonObject> CoverageRef = Coverage.ToSharedRef();
		CoverageTotalKeys = static_cast<int32>(Int(CoverageRef, TEXT("totalKeys")));
		CoverageUnmappedKeys = Strings(CoverageRef, TEXT("unmappedKeys"));
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
	// The default-condition pixel program's name, when the stage published one, or the resolved
	// family otherwise -- today's sidecar carries no `resolvedPrograms`, so this always falls
	// through to ResolvedFamily, exactly as the "shader did not resolve to a concrete pair" case
	// always did.
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
