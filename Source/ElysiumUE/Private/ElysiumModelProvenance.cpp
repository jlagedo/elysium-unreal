#include "ElysiumModelProvenance.h"

#include "ElysiumJsonField.h"

#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#if WITH_EDITORONLY_DATA
#include "UObject/MetaData.h"
#endif
const FName UElysiumModelProvenance::TagAssetId(TEXT("ElysiumAssetId"));
const FName UElysiumModelProvenance::TagModelShape(TEXT("ElysiumModelShape"));
const FName UElysiumModelProvenance::TagNanite(TEXT("ElysiumNanite"));

namespace
{
	using namespace ElysiumJson;

	/**
	 * A JSON field's value as text regardless of its JSON type, mirroring
	 * `ElysiumMaterialProvenance.cpp`'s own `StringifyField` -- the Coverage map stringifies
	 * whatever shape the sidecar's `coverage` object turns out to carry (C-2), the same tolerance
	 * `Anomalies[].Extra`/`Omissions[].Extra` already need. Named distinctly from the material
	 * lane's identical helper (`Model` suffix throughout this anonymous namespace) so a unity
	 * build that ever merges both `.cpp` files into one translation unit does not collide two
	 * same-named internal-linkage functions.
	 */
	FString StringifyModelField(const TSharedRef<FJsonObject>& Object, const TCHAR* Key)
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

	/** Every field of `Row` besides `SkipKey`, stringified the tolerant way (C-2's catch-all). */
	void ReadModelExtraFields(const TSharedRef<FJsonObject>& Row, const TCHAR* SkipKey, TMap<FString, FString>& Out)
	{
		for (const auto& Field : Row->Values)
		{
			const FString Key(Field.Key);
			if (Key == SkipKey)
			{
				continue;
			}
			Out.Add(Key, StringifyModelField(Row, *Key));
		}
	}

	/**
	 * The label key of an anomaly or omission row, which is not one spelling.
	 *
	 * `importers/models.py` merges two row sources into each array verbatim: this lane's own rows
	 * (`{"kind": "missingMaterialSentinel", ...}`, `{"kind": "noPhysicsSolidsBoxFallback", ...}`)
	 * and the unit's own `anomalies[]`/`omissions[]` straight out of the GLB extension, whose rows
	 * are `{"row": "degenerate-normal", ...}` and `{"row": "reserved-field", "reason": ...}`. The
	 * stage cannot rename a foreign row without rewriting it, so the reader takes the first key
	 * that is present, in the order this lane's own rows use it, and the unused one still lands in
	 * `Extra`. Pinned against the staged shape by
	 * `pipeline/tests/test_model_provenance_keys.py`.
	 */
	FString ReadRowLabel(const TSharedRef<FJsonObject>& Row, const TCHAR* Preferred,
		const TCHAR* Fallback, const TCHAR*& OutUsedKey)
	{
		FString Label;
		if (Row->TryGetStringField(Preferred, Label))
		{
			OutUsedKey = Preferred;
			return Label;
		}
		if (Row->TryGetStringField(Fallback, Label))
		{
			OutUsedKey = Fallback;
			return Label;
		}
		OutUsedKey = Preferred;
		return FString();
	}

	/** `anomalies[]`: `kind` (this lane's rows) or `row` (the unit's); the rest lands in `Extra`. */
	void ReadModelAnomalies(const TSharedRef<FJsonObject>& O, TArray<FElysiumProvenanceAnomaly>& Out)
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
			const TCHAR* UsedKey = nullptr;
			Anomaly.Kind = ReadRowLabel(Row, TEXT("kind"), TEXT("row"), UsedKey);
			ReadModelExtraFields(Row, UsedKey, Anomaly.Extra);
		}
	}

	/** `omissions[]`: `reason` (the unit's rows) or `kind` (this lane's); the rest lands in `Extra`. */
	void ReadModelOmissions(const TSharedRef<FJsonObject>& O, TArray<FElysiumProvenanceNote>& Out)
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
			const TCHAR* UsedKey = nullptr;
			Note.Reason = ReadRowLabel(Row, TEXT("reason"), TEXT("kind"), UsedKey);
			ReadModelExtraFields(Row, UsedKey, Note.Extra);
		}
	}

	/**
	 * `sourceSha256[]`: one `{"role", "sha256"}` row per source member ("Source closure" -- the
	 * MDL, both VTX variants, and the PHY when the model ships one), flattened to `role:sha256`.
	 *
	 * The role is kept rather than dropped because a model unit is assembled from several
	 * independently-resolved members and "which member" is the whole reason this field is an array
	 * here and a single string on the material and texture lanes. A plain string element is taken
	 * as-is, so a future stage that flattens the rows itself still reads.
	 */
	void ReadSourceSha256(const TSharedRef<FJsonObject>& O, TArray<FString>& Out)
	{
		Out.Reset();
		const TArray<TSharedPtr<FJsonValue>>* Rows = Arr(O, TEXT("sourceSha256"));
		if (!Rows)
		{
			return;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Rows)
		{
			if (!Value.IsValid())
			{
				continue;
			}
			FString Plain;
			if (Value->TryGetString(Plain))
			{
				Out.Add(Plain);
				continue;
			}
			const TSharedPtr<FJsonObject>* RowPtr = nullptr;
			if (!Value->TryGetObject(RowPtr) || !RowPtr)
			{
				continue;
			}
			const TSharedRef<FJsonObject> Row = (*RowPtr).ToSharedRef();
			const FString Role = Str(Row, TEXT("role"));
			const FString Digest = Str(Row, TEXT("sha256"));
			Out.Add(Role.IsEmpty() ? Digest : Role + TEXT(":") + Digest);
		}
	}

	/** `slots[]`: `materialBindings.slots` plus this lane's own resolution ("Material binding"). */
	void ReadSlots(const TSharedRef<FJsonObject>& O, TArray<FElysiumModelSlotProvenance>& Out)
	{
		Out.Reset();
		const TArray<TSharedPtr<FJsonValue>>* Rows = Arr(O, TEXT("slots"));
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
			FElysiumModelSlotProvenance& Slot = Out.AddDefaulted_GetRef();
			Slot.Index = static_cast<int32>(Int(Row, TEXT("index"), Out.Num() - 1));
			Slot.SlotName = Str(Row, TEXT("slotName"));
			Slot.SourceName = Str(Row, TEXT("sourceName"));
			Slot.SourcePath = Str(Row, TEXT("sourcePath"));
			// `materialId` is the key the stage writes ("Material binding": a `vtmb:material:` id
			// or a `vtmb:missing-material:<slot>:<name>` sentinel); the field it fills is named
			// MaterialAssetId to say what kind of id it is.
			Slot.MaterialAssetId = Str(Row, TEXT("materialId"));
			Slot.MaterialAsset = Str(Row, TEXT("materialAsset"));
			Slot.Resolved = Bool(Row, TEXT("resolved"));
			Slot.IsSentinel = Bool(Row, TEXT("isSentinel"));
		}
	}

	/**
	 * `skinFamilies[]`: `materialBindings.skinFamilies`, resolved to `MI_` paths by the stage.
	 *
	 * Each staged row is `{"family": n, "slots": [name, ...], "materials": [path, ...],
	 * "materialIds": [...]}` -- two parallel arrays over *every* column of the model's skin table,
	 * drawn by LOD 0 or not, because the whole table is what `DA_ElysiumPropSkins` is written from
	 * (R1.5). The pair is zipped into `Overrides` here, so the record states what each family
	 * paints at each slot; the diff-against-family-0 fold that makes the *table's* rows short is
	 * the table's own, not this record's, and `Slots[]` above already states family 0 on its own.
	 */
	void ReadSkinFamilies(const TSharedRef<FJsonObject>& O, TArray<FElysiumModelSkinFamilyProvenance>& Out)
	{
		Out.Reset();
		const TArray<TSharedPtr<FJsonValue>>* Rows = Arr(O, TEXT("skinFamilies"));
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
			FElysiumModelSkinFamilyProvenance& Family = Out.AddDefaulted_GetRef();
			Family.Family = static_cast<int32>(Int(Row, TEXT("family"), Out.Num() - 1));
			const TArray<FString> SlotNames = Strings(Row, TEXT("slots"));
			const TArray<FString> Materials = Strings(Row, TEXT("materials"));
			const int32 Count = FMath::Min(SlotNames.Num(), Materials.Num());
			for (int32 Index = 0; Index < Count; ++Index)
			{
				FElysiumModelSkinOverride& Override = Family.Overrides.AddDefaulted_GetRef();
				Override.SlotName = SlotNames[Index];
				Override.MaterialAsset = Materials[Index];
			}
		}
	}

	/** `lods[]`: `vtx.lods` plus the `switchPoints` -> `ScreenSize` mapping ("Geometry"). */
	void ReadLods(const TSharedRef<FJsonObject>& O, TArray<FElysiumModelLodProvenance>& Out)
	{
		Out.Reset();
		const TArray<TSharedPtr<FJsonValue>>* Rows = Arr(O, TEXT("lods"));
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
			FElysiumModelLodProvenance& Lod = Out.AddDefaulted_GetRef();
			Lod.Index = static_cast<int32>(Int(Row, TEXT("index"), Out.Num() - 1));
			Lod.SwitchPoint = Float(Row, TEXT("switchPoint"));
			Lod.ScreenSize = Float(Row, TEXT("screenSize"));
			Lod.Sections = static_cast<int32>(Int(Row, TEXT("sections")));
			Lod.Triangles = static_cast<int32>(Int(Row, TEXT("triangles")));
			Lod.Dropped = Bool(Row, TEXT("dropped"));
		}
	}

	/** `hullBounds`: `{min: [x,y,z], max: [x,y,z]}`, the box the bbox collision rule reads. */
	void ReadHullBounds(const TSharedRef<FJsonObject>& O, FElysiumModelHullBounds& Out)
	{
		Out = FElysiumModelHullBounds();
		TSharedPtr<FJsonObject> Bounds = Obj(O, TEXT("hullBounds"));
		if (!Bounds.IsValid())
		{
			return;
		}
		const TSharedRef<FJsonObject> BoundsRef = Bounds.ToSharedRef();
		auto ReadVector = [&BoundsRef](const TCHAR* Key) -> FVector
		{
			const TArray<TSharedPtr<FJsonValue>>* Components = Arr(BoundsRef, Key);
			if (!Components || Components->Num() < 3)
			{
				return FVector::ZeroVector;
			}
			double X = 0.0, Y = 0.0, Z = 0.0;
			if ((*Components)[0].IsValid()) { (*Components)[0]->TryGetNumber(X); }
			if ((*Components)[1].IsValid()) { (*Components)[1]->TryGetNumber(Y); }
			if ((*Components)[2].IsValid()) { (*Components)[2]->TryGetNumber(Z); }
			return FVector(X, Y, Z);
		};
		Out.Min = ReadVector(TEXT("min"));
		Out.Max = ReadVector(TEXT("max"));
	}
}

void UElysiumModelProvenance::FromJson(const TSharedRef<FJsonObject>& O)
{
	using namespace ElysiumJson;

	// --- identity ---
	AssetId = Str(O, TEXT("assetId"));
	ModelPath = Str(O, TEXT("modelPath"));
	Stem = Str(O, TEXT("stem"));
	UnitSchemaVersion = Str(O, TEXT("unitSchemaVersion"));
	UnitSha256 = Str(O, TEXT("unitSha256"));
	ReadSourceSha256(O, SourceSha256);
	SettingsVersion = Str(O, TEXT("settingsVersion"));

	// --- identity classification ---
	Shape = Str(O, TEXT("shape"));
	Family = Str(O, TEXT("family"));
	Roles = Strings(O, TEXT("roles"));

	// --- material binding ---
	ReadSlots(O, Slots);
	ReadSkinFamilies(O, SkinFamilies);

	// --- geometry ---
	ReadLods(O, Lods);
	// `bNanite` is the key the stage writes -- the field's own name, Unreal's boolean prefix and
	// all, not a `nanite` transliteration of it.
	bNanite = Bool(O, TEXT("bNanite"));
	NaniteVetoSlot = Str(O, TEXT("naniteVetoSlot"));
	NaniteVetoMaterial = Str(O, TEXT("naniteVetoMaterial"));

	// --- collision ---
	CollisionMode = Str(O, TEXT("collisionMode"));
	HullCount = static_cast<int32>(Int(O, TEXT("hullCount")));
	ShapeCount = static_cast<int32>(Int(O, TEXT("shapeCount")));
	MassKg = Float(O, TEXT("massKg"));
	ReadHullBounds(O, HullBounds);

	// --- surface property ---
	SurfaceProperty = Str(O, TEXT("surfaceProperty"));
	SurfacePropertySource = Str(O, TEXT("surfacePropertySource"));
	PhysMaterial = Str(O, TEXT("physMaterial"));

	// --- content ---
	ReadModelAnomalies(O, Anomalies);
	ReadModelOmissions(O, Omissions);
	Coverage.Reset();
	if (TSharedPtr<FJsonObject> CoverageObject = Obj(O, TEXT("coverage")))
	{
		ReadModelExtraFields(CoverageObject.ToSharedRef(), TEXT(""), Coverage);
	}
}

UElysiumModelProvenance* UElysiumModelProvenance::ApplyJson(
	UStaticMesh* Mesh, const FString& Json, FString& OutError)
{
	OutError.Reset();
	if (!Mesh)
	{
		OutError = TEXT("no mesh");
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

	// The mesh is the outer, so the record serializes inside the asset's package; a flag-less
	// NewObject would land it in the transient package and the save would drop it.
	UElysiumModelProvenance* Record = NewObject<UElysiumModelProvenance>(Mesh, NAME_None, RF_Public | RF_Transactional);
	Record->FromJson(Object->ToSharedRef());
	// AddAssetUserData removes an existing instance of the same class first, so a re-import
	// replaces rather than accumulates.
	Mesh->AddAssetUserData(Record);
	Mesh->MarkPackageDirty();
	return Record;
}

const UElysiumModelProvenance* UElysiumModelProvenance::Find(const UStaticMesh* Mesh)
{
	if (!Mesh)
	{
		return nullptr;
	}
	// GetAssetUserDataOfClass is non-const on the interface; the lookup itself mutates nothing.
	return Cast<UElysiumModelProvenance>(
		const_cast<UStaticMesh*>(Mesh)->GetAssetUserDataOfClass(UElysiumModelProvenance::StaticClass()));
}

void UElysiumModelProvenance::StampRegistryTags(UStaticMesh* Mesh, bool& bOutStamped, FString& OutError)
{
	bOutStamped = false;
	OutError.Reset();
	const UElysiumModelProvenance* Record = Find(Mesh);
	if (!Record)
	{
		OutError = TEXT("mesh carries no ElysiumModelProvenance");
		return;
	}
#if WITH_EDITORONLY_DATA
	UPackage* Package = Mesh->GetPackage();
	if (!Package)
	{
		OutError = TEXT("mesh has no package");
		return;
	}
	FMetaData& Meta = Package->GetMetaData();
	Meta.SetValue(Mesh, TagAssetId, *Record->AssetId);
	Meta.SetValue(Mesh, TagModelShape, *Record->Shape);
	Meta.SetValue(Mesh, TagNanite, Record->bNanite ? TEXT("true") : TEXT("false"));
	Mesh->MarkPackageDirty();
	bOutStamped = true;
#else
	OutError = TEXT("package metadata is editor-only data");
#endif
}
