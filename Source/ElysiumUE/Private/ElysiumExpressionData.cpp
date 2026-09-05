#include "ElysiumExpressionData.h"
#include "Visual/ElysiumExpressionTable.h"

#if WITH_EDITOR
#include "ElysiumContentPaths.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"
#endif

namespace
{
	const FString ExpressionPrefix = TEXT("vtmb:expression-table:");
	FString NormalizeExpressionStem(const FString& Param)
	{
		FString Stem = Param.TrimStartAndEnd().Replace(TEXT("\\"), TEXT("/")).ToLower();
		Stem.RemoveFromStart(ExpressionPrefix);
		int32 Slash;
		if (Stem.FindLastChar(TEXT('/'), Slash)) Stem.RightChopInline(Slash + 1);
		if (Stem.EndsWith(TEXT(".vfe")) || Stem.EndsWith(TEXT(".txt"))) Stem.LeftChopInline(4);
		return Stem;
	}
	const UElysiumExpressionData* Ready(const UElysiumExpressionData* Data, const FString& Id, FString& Error)
	{
		if (!Data || Data->AssetId != Id) Error = TEXT("expression reference was not prepared: ") + Id;
		else if (!Data->IsRuntimeReady()) Error = TEXT("expression has no usable compiled table: ") + Id + TEXT(" (") + Data->RuntimeStatus + TEXT(")");
		else return Data;
		return nullptr;
	}
}

int32 FElysiumNativeExpressionTable::FindRow(const FString& Name) const
{
	for (int32 I = 0; I < Rows.Num(); ++I)
		if (Rows[I].Name.TrimStartAndEnd().Equals(Name.TrimStartAndEnd(), ESearchCase::IgnoreCase)) return I;
	return INDEX_NONE;
}

int32 FElysiumNativeExpressionTable::FindRowByPhonemeCode(int32 Code) const
{
	if (Code != INDEX_NONE)
		for (int32 I = 0; I < Rows.Num(); ++I)
			if (Rows[I].PhonemeCode == Code) return I;
	return INDEX_NONE;
}

TSharedPtr<const FElysiumExpressionTable> UElysiumExpressionData::PrepareLegacyView(FString& OutError) const
{
	OutError.Reset();
	if (!Ready(this, AssetId, OutError)) return nullptr;
	auto View = MakeShared<FElysiumExpressionTable>();
	View->Stem = Stem;
	View->Keys = Table.Keys;
	View->bHasWeighting = Table.bHasWeighting;
	for (const auto& Source : Table.Rows)
	{
		if (Source.Values.Num() != Table.Keys.Num()
			|| Source.Weights.Num() != (Table.bHasWeighting ? Table.Keys.Num() : 0))
		{ OutError = TEXT("corrupt native expression row: ") + AssetId; return nullptr; }
		FElysiumExpressionRow Row;
		Row.Name = Source.Name; Row.Class = Source.Class;
		Row.PhonemeCode = Source.PhonemeCode; Row.Description = Source.Description;
		for (int32 K = 0; K < Source.Values.Num(); ++K)
		{
			const double Value = Source.Values[K];
			const double Weight = Table.bHasWeighting ? Source.Weights[K] : 1.;
			if (!FMath::IsFinite(Value) || !FMath::IsFinite(Weight)
				|| double(float(Value)) != Value || double(float(Weight)) != Weight)
			{ OutError = TEXT("native expression cannot reach float evaluator without loss: ") + AssetId; return nullptr; }
			Row.Values.Add(float(Value)); Row.Weights.Add(float(Weight));
		}
		if (Row.PhonemeCode != INDEX_NONE) View->RowByPhonemeCode.FindOrAdd(Row.PhonemeCode, View->Rows.Num());
		View->Rows.Add(MoveTemp(Row));
	}
	return View;
}

const UElysiumExpressionData* UElysiumExpressionTables::ResolveEvent(const FString& Param, const FString& Class, FString& OutError) const
{
	OutError.Reset();
	const FString Stem = NormalizeExpressionStem(Param);
	if (Stem.IsEmpty()) { OutError = TEXT("empty expression event table"); return nullptr; }
	for (const FString& Candidate : {Stem, Stem + TEXT("_") + Class.ToLower()})
	{
		const FString Id = ExpressionPrefix + Candidate;
		if (const auto* Ref = Tables.Find(Id)) return Ready(Ref->Get(), Id, OutError);
	}
	OutError = TEXT("expression event table is absent from prepared corpus: ") + Stem;
	return nullptr;
}

const UElysiumExpressionData* UElysiumExpressionTables::ResolveSelection(
	const FElysiumExpressionSelection& Selection, bool bMale, FString& OutDiagnostic) const
{
	OutDiagnostic.Reset();
	if (Selection.TableClass != TEXT("expressions") && Selection.TableClass != TEXT("phonemes"))
	{ OutDiagnostic = TEXT("invalid expression selection class"); return nullptr; }
	if (!Selection.PrimaryAssetId.IsEmpty())
	{
		const auto* Ref = Tables.Find(Selection.PrimaryAssetId);
		if (!Ref || !Ref->Get() || (*Ref)->AssetId != Selection.PrimaryAssetId)
			return Ready(Ref ? Ref->Get() : nullptr, Selection.PrimaryAssetId, OutDiagnostic);
		if ((*Ref)->RuntimeStatus != TEXT("authoring-only"))
			return Ready(Ref->Get(), Selection.PrimaryAssetId, OutDiagnostic);
		OutDiagnostic = TEXT("TXT-only selection retained as evidence; using compiled fallback: ") + Selection.PrimaryAssetId;
	}
	const FString Wanted = ExpressionPrefix + (bMale ? TEXT("phonemes_male") : TEXT("phonemes"));
	if (!Selection.FallbackAssetIds.Contains(Wanted))
	{ OutDiagnostic = TEXT("published selection does not resolve the requested fallback: ") + Wanted; return nullptr; }
	const auto* Ref = Tables.Find(Wanted);
	FString Error;
	const auto* Result = Ready(Ref ? Ref->Get() : nullptr, Wanted, Error);
	if (!Result) OutDiagnostic = Error;
	return Result;
}

#if WITH_EDITOR
namespace
{
	bool Invalid(FString& Error, const FString& Path, const FString& Reason)
	{
		Error = Path + TEXT(": ") + Reason;
		return false;
	}
	bool ReadObject(const FString& Json, TSharedPtr<FJsonObject>& Out, FString& Error, const FString& Path)
	{
		const auto Reader = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, Out) || !Out.IsValid())
			return Invalid(Error, Path, TEXT("invalid JSON object: ") + Reader->GetErrorMessage());
		return true;
	}
	bool ReadString(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, FString& Out,
		FString& Error, const FString& Path)
	{
		const auto Value = Object->TryGetField(Key);
		return (Value.IsValid() && Value->Type == EJson::String && Value->TryGetString(Out))
			|| Invalid(Error, Path + TEXT(".") + Key, TEXT("expected a string"));
	}
	bool ReadArray(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key,
		const TArray<TSharedPtr<FJsonValue>>*& Out, FString& Error, const FString& Path)
	{
		return Object->TryGetArrayField(Key, Out)
			|| Invalid(Error, Path + TEXT(".") + Key, TEXT("expected an array"));
	}
	bool ReadObjectField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key,
		const TSharedPtr<FJsonObject>*& Out, FString& Error, const FString& Path)
	{
		return Object->TryGetObjectField(Key, Out)
			|| Invalid(Error, Path + TEXT(".") + Key, TEXT("expected an object"));
	}
	bool ReadStrings(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, TArray<FString>& Out,
		FString& Error, const FString& Path)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!ReadArray(Object, Key, Values, Error, Path)) return false;
		for (int32 I = 0; I < Values->Num(); ++I)
		{
			FString Value;
			if ((*Values)[I]->Type != EJson::String || !(*Values)[I]->TryGetString(Value))
				return Invalid(Error, FString::Printf(TEXT("%s.%s[%d]"), *Path, Key, I), TEXT("expected a string"));
			Out.Add(MoveTemp(Value));
		}
		return true;
	}
	bool ReadInt(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, int32& Out,
		FString& Error, const FString& Path)
	{
		double Number = 0.;
		const auto Value = Object->TryGetField(Key);
		if (!Value.IsValid() || Value->Type != EJson::Number || !Value->TryGetNumber(Number) || !FMath::IsFinite(Number)
			|| Number < 0 || Number > MAX_int32 || double(int32(Number)) != Number)
			return Invalid(Error, Path + TEXT(".") + Key, TEXT("expected a nonnegative int32 JSON number"));
		Out = int32(Number); return true;
	}
	bool ReadTable(const TSharedPtr<FJsonObject>& Root, const TCHAR* Field,
		FElysiumNativeExpressionTable& Out, bool bCompiled, FString& Error)
	{
		const FString Path = FString(TEXT("document.extensions.ELYSIUM_vtmb_expression_table.")) + Field;
		const auto Value = Root->TryGetField(Field);
		if (!Value.IsValid()) return Invalid(Error, Path, TEXT("missing table or explicit null"));
		if (Value->Type == EJson::Null) return true;
		const TSharedPtr<FJsonObject>* Object = nullptr;
		if (!Value->TryGetObject(Object) || !Object) return Invalid(Error, Path, TEXT("expected an object or null"));
		Out.bPresent = true;
		if (!(*Object)->TryGetBoolField(TEXT("hasWeighting"), Out.bHasWeighting))
			return Invalid(Error, Path + TEXT(".hasWeighting"), TEXT("expected a boolean"));
		if (!ReadStrings(*Object, TEXT("keys"), Out.Keys, Error, Path)) return false;
		const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
		if (!ReadArray(*Object, TEXT("rows"), Rows, Error, Path)) return false;
		int32 Previous = INDEX_NONE;
		for (int32 I = 0; I < Rows->Num(); ++I)
		{
			const FString RowPath = FString::Printf(TEXT("%s.rows[%d]"), *Path, I);
			const TSharedPtr<FJsonObject>* RowObject = nullptr;
			FElysiumNativeExpressionRow Row;
			if (!(*Rows)[I]->TryGetObject(RowObject) || !RowObject)
				return Invalid(Error, RowPath, TEXT("expected an object"));
			if (!ReadInt(*RowObject, TEXT("index"), Row.Index, Error, RowPath)) return false;
			if (Row.Index <= Previous) return Invalid(Error, RowPath + TEXT(".index"), TEXT("source indices must increase without duplicates"));
			if (!ReadString(*RowObject, TEXT("name"), Row.Name, Error, RowPath)
				|| !ReadString(*RowObject, TEXT("class"), Row.Class, Error, RowPath)
				|| !ReadString(*RowObject, TEXT("description"), Row.Description, Error, RowPath)) return false;
			Previous = Row.Index;
			const auto Code = (*RowObject)->TryGetField(TEXT("phonemeCode"));
			if (!Code.IsValid()) return Invalid(Error, RowPath + TEXT(".phonemeCode"), TEXT("missing number or explicit null"));
			if (Code->Type != EJson::Null && !ReadInt(*RowObject, TEXT("phonemeCode"), Row.PhonemeCode, Error, RowPath)) return false;
			for (const TCHAR* Numbers : {TEXT("values"), TEXT("weights")})
			{
				const bool bValues = FCString::Strcmp(Numbers, TEXT("values")) == 0;
				const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
				if (!ReadArray(*RowObject, Numbers, Values, Error, RowPath)) return false;
				const int32 Expected = (bValues || Out.bHasWeighting) ? Out.Keys.Num() : 0;
				if (Values->Num() != Expected)
					return Invalid(Error, RowPath + TEXT(".") + Numbers,
						FString::Printf(TEXT("expected %d entries aligned with keys, got %d"), Expected, Values->Num()));
				for (int32 K = 0; K < Values->Num(); ++K)
				{
					const auto& Number = (*Values)[K];
					const FString NumberPath = FString::Printf(TEXT("%s.%s[%d]"), *RowPath, Numbers, K);
					double N = 0.;
					if (Number->Type != EJson::Number || !Number->TryGetNumber(N) || !FMath::IsFinite(N))
						return Invalid(Error, NumberPath, TEXT("expected a finite JSON number"));
					if (bCompiled && double(float(N)) != N)
						return Invalid(Error, NumberPath, FString::Printf(
							TEXT("compiled value is not exact float32: parsed %.17g, float32 %.17g; key '%s', source row %d '%s'"),
							N, double(float(N)), *Out.Keys[K], Row.Index, *Row.Name));
					(bValues ? Row.Values : Row.Weights).Add(N);
				}
			}
			Out.Rows.Add(MoveTemp(Row));
		}
		return true;
	}
	FString VerifyFields(const UObject* Asset, const UObject* Expected, UClass* Class)
	{
		for (TFieldIterator<FProperty> It(Class, EFieldIteratorFlags::ExcludeSuper); It; ++It)
			if (!It->Identical_InContainer(Asset, Expected)) return TEXT("saved expression differs in ") + It->GetName();
		return FString();
	}
}
#endif

UElysiumExpressionData* UElysiumExpressionData::ApplyJson(UElysiumExpressionData* Asset, const FString& Json, FString& OutError)
{
#if WITH_EDITOR
	OutError.Reset();
	TSharedPtr<FJsonObject> Stage, Document;
	FString Version, Id, Path, Evidence, Digest, Status;
	auto Fail = [&OutError](const FString& Field, const FString& Reason) -> UElysiumExpressionData*
	{ Invalid(OutError, Field, Reason); return nullptr; };
	if (!Asset) return Fail(TEXT("stage.asset"), TEXT("native target is absent"));
	if (!ReadObject(Json, Stage, OutError, TEXT("stage"))) return nullptr;
	if (!ReadString(Stage, TEXT("schemaVersion"), Version, OutError, TEXT("stage"))) return nullptr;
	if (Version != TEXT("1.0.0")) return Fail(TEXT("stage.schemaVersion"), TEXT("expected 1.0.0, got ") + Version);
	if (!ReadString(Stage, TEXT("assetId"), Id, OutError, TEXT("stage"))) return nullptr;
	if (!Id.StartsWith(ExpressionPrefix)) return Fail(TEXT("stage.assetId"), TEXT("expected expression-table identity, got ") + Id);
	if (!ReadString(Stage, TEXT("assetPath"), Path, OutError, TEXT("stage"))) return nullptr;
	// Offline baked_unit returns a package; BakedUnit returns its object path.
	// Compare package to package, then use the canonical object path for any load.
	const FString ExpectedObject = FElysiumContentPaths::BakedUnit(Id, TEXT("DA"));
	const FString ExpectedPackage = FPackageName::ObjectPathToPackageName(ExpectedObject);
	if (ExpectedObject.IsEmpty() || Path != ExpectedPackage)
		return Fail(TEXT("stage.assetPath"), FString::Printf(TEXT("%s: expected package '%s' (object '%s'), got '%s'"),
			*Id, *ExpectedPackage, *ExpectedObject, *Path));
	if (!ReadString(Stage, TEXT("sourceDocumentJson"), Evidence, OutError, TEXT("stage"))
		|| !ReadObject(Evidence, Document, OutError, TEXT("stage.sourceDocumentJson"))
		|| !ReadString(Stage, TEXT("sourceGlbSha256"), Digest, OutError, TEXT("stage"))) return nullptr;
	if (Digest.Len() != 64) return Fail(TEXT("stage.sourceGlbSha256"), TEXT("expected 64 hexadecimal digits"));
	for (TCHAR C : Digest) if (!FChar::IsHexDigit(C)) return Fail(TEXT("stage.sourceGlbSha256"), TEXT("non-hexadecimal character"));
	if (!ReadString(Stage, TEXT("runtimeStatus"), Status, OutError, TEXT("stage"))) return nullptr;
	const FString RootPath = TEXT("document.extensions.ELYSIUM_vtmb_expression_table");
	const TSharedPtr<FJsonObject> *Extensions = nullptr, *Root = nullptr, *Identity = nullptr, *Coverage = nullptr;
	if (!ReadObjectField(Document, TEXT("extensions"), Extensions, OutError, TEXT("document"))
		|| !ReadObjectField(*Extensions, TEXT("ELYSIUM_vtmb_expression_table"), Root, OutError, TEXT("document.extensions"))
		|| !ReadString(*Root, TEXT("schemaVersion"), Version, OutError, RootPath)) return nullptr;
	if (Version != TEXT("1.0.0")) return Fail(RootPath + TEXT(".schemaVersion"), TEXT("expected 1.0.0, got ") + Version);
	if (!ReadObjectField(*Root, TEXT("identity"), Identity, OutError, RootPath)
		|| !ReadObjectField(*Root, TEXT("coverage"), Coverage, OutError, RootPath)) return nullptr;
	FString SourceId, Stem, Kind;
	bool bLoadable = false;
	const FString IdentityPath = RootPath + TEXT(".identity");
	if (!ReadString(*Identity, TEXT("asset"), SourceId, OutError, IdentityPath)) return nullptr;
	if (SourceId != Id) return Fail(IdentityPath + TEXT(".asset"), TEXT("differs from stage.assetId: ") + Id);
	if (!ReadString(*Identity, TEXT("stem"), Stem, OutError, IdentityPath)) return nullptr;
	if (Id != ExpressionPrefix + Stem) return Fail(IdentityPath + TEXT(".stem"), TEXT("differs from stage.assetId: ") + Id);
	if (!ReadString(*Identity, TEXT("sourceKind"), Kind, OutError, IdentityPath)) return nullptr;
	if (Kind != TEXT("vfe+txt") && Kind != TEXT("vfe-only") && Kind != TEXT("txt-only"))
		return Fail(IdentityPath + TEXT(".sourceKind"), TEXT("unknown source kind: ") + Kind);
	if (!(*Identity)->TryGetBoolField(TEXT("runtimeLoadable"), bLoadable) || bLoadable != (Kind != TEXT("txt-only")))
		return Fail(IdentityPath + TEXT(".runtimeLoadable"), TEXT("expected boolean consistent with sourceKind: ") + Kind);
	FElysiumNativeExpressionTable Table, Authoring;
	if (!ReadTable(*Root, TEXT("table"), Table, true, OutError)
		|| !ReadTable(*Root, TEXT("authoring"), Authoring, false, OutError)) return nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Unsupported = nullptr;
	if (!ReadArray(*Coverage, TEXT("unsupported"), Unsupported, OutError, RootPath + TEXT(".coverage"))) return nullptr;
	const FString ExpectedStatus = !bLoadable ? TEXT("authoring-only") : !Table.bPresent ? TEXT("undecoded-vfe")
		: !Unsupported->IsEmpty() ? TEXT("unsupported-vfe") : TEXT("ready");
	if (Status != ExpectedStatus) return Fail(TEXT("stage.runtimeStatus"), TEXT("expected ") + ExpectedStatus + TEXT(", got ") + Status);
	if (!bLoadable && Table.bPresent) return Fail(RootPath + TEXT(".table"), TEXT("TXT-only unit must publish null compiled table"));
	if (Status == TEXT("ready"))
	{
		const FString VfePath = RootPath + TEXT(".vfe");
		const TSharedPtr<FJsonObject>* Vfe = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Settings = nullptr;
		TArray<FString> Keys; int32 Count = INDEX_NONE;
		if (!ReadObjectField(*Root, TEXT("vfe"), Vfe, OutError, RootPath)
			|| !ReadInt(*Vfe, TEXT("numFlexSettings"), Count, OutError, VfePath)) return nullptr;
		if (Count != Table.Rows.Num()) return Fail(VfePath + TEXT(".numFlexSettings"),
			FString::Printf(TEXT("declares %d, table has %d rows"), Count, Table.Rows.Num()));
		if (!ReadArray(*Vfe, TEXT("settings"), Settings, OutError, VfePath)) return nullptr;
		if (Settings->Num() != Count) return Fail(VfePath + TEXT(".settings"), TEXT("count differs from numFlexSettings"));
		if (!ReadStrings(*Vfe, TEXT("keys"), Keys, OutError, VfePath)) return nullptr;
		if (Keys != Table.Keys) return Fail(VfePath + TEXT(".keys"), TEXT("controller names/order differ from table.keys"));
		for (int32 I = 0; I < Count; ++I)
		{
			const FString SettingPath = FString::Printf(TEXT("%s.settings[%d]"), *VfePath, I);
			const TSharedPtr<FJsonObject>* Setting = nullptr; int32 Index = INDEX_NONE; FString Type;
			if (!(*Settings)[I]->TryGetObject(Setting)) return Fail(SettingPath, TEXT("expected an object"));
			if (!ReadInt(*Setting, TEXT("index"), Index, OutError, SettingPath)) return nullptr;
			if (Index != Table.Rows[I].Index) return Fail(SettingPath + TEXT(".index"), TEXT("differs from corresponding table row"));
			if (!ReadString(*Setting, TEXT("type"), Type, OutError, SettingPath)) return nullptr;
			if (Type != TEXT("normal")) return Fail(SettingPath + TEXT(".type"), TEXT("ready table contains non-normal setting: ") + Type);
		}
	}
	// No mutation before complete validation. Evidence remains present in cooked builds.
	Asset->AssetId = Id; Asset->Stem = Stem; Asset->SourceKind = Kind;
	Asset->SourceGlbSha256 = Digest; Asset->SourceDocumentJson = Evidence;
	Asset->RuntimeStatus = Status; Asset->Table = MoveTemp(Table); Asset->Authoring = MoveTemp(Authoring);
	Asset->MarkPackageDirty(); OutError.Reset(); return Asset;
#else
	OutError = TEXT("expression import is editor only"); return nullptr;
#endif
}

FString UElysiumExpressionData::Verify(UElysiumExpressionData* Asset, const FString& Json)
{
#if WITH_EDITOR
	auto* Expected = NewObject<UElysiumExpressionData>(); FString Error;
	if (!Asset || !ApplyJson(Expected, Json, Error)) return Error.IsEmpty() ? TEXT("expression asset is absent") : Error;
	return VerifyFields(Asset, Expected, StaticClass());
#else
	return TEXT("expression verification is editor only");
#endif
}

UElysiumExpressionTables* UElysiumExpressionTables::ApplyJson(UElysiumExpressionTables* Asset, const FString& Json, FString& OutError)
{
#if WITH_EDITOR
	OutError.Reset();
	TSharedPtr<FJsonObject> Object; const TSharedPtr<FJsonObject>* Rows = nullptr;
	FString Version, Path;
	auto Fail = [&OutError](const FString& Field, const FString& Reason) -> UElysiumExpressionTables*
	{ Invalid(OutError, Field, Reason); return nullptr; };
	if (!Asset) return Fail(TEXT("corpus.asset"), TEXT("native target is absent"));
	if (!ReadObject(Json, Object, OutError, TEXT("corpus"))
		|| !ReadString(Object, TEXT("schemaVersion"), Version, OutError, TEXT("corpus"))) return nullptr;
	if (Version != TEXT("1.0.0")) return Fail(TEXT("corpus.schemaVersion"), TEXT("expected 1.0.0, got ") + Version);
	if (!ReadString(Object, TEXT("assetPath"), Path, OutError, TEXT("corpus"))) return nullptr;
	if (Path != TEXT("/ElysiumBaked/ExpressionTables/_Corpus/DA_ExpressionTables"))
		return Fail(TEXT("corpus.assetPath"), TEXT("expected /ElysiumBaked/ExpressionTables/_Corpus/DA_ExpressionTables, got ") + Path);
	if (!ReadObjectField(Object, TEXT("tables"), Rows, OutError, TEXT("corpus"))) return nullptr;
	if ((*Rows)->Values.IsEmpty()) return Fail(TEXT("corpus.tables"), TEXT("empty table inventory"));
	TMap<FString, TObjectPtr<UElysiumExpressionData>> Tables;
	for (const auto& Pair : (*Rows)->Values)
	{
		FString TablePath;
		const FString Id(Pair.Key);
		const FString Field = TEXT("corpus.tables[") + Id + TEXT("]");
		if (!Id.StartsWith(ExpressionPrefix)) return Fail(Field, TEXT("expected expression-table identity"));
		if (Pair.Value->Type != EJson::String || !Pair.Value->TryGetString(TablePath)) return Fail(Field, TEXT("expected package path string"));
		const FString ObjectPath = FElysiumContentPaths::BakedUnit(Id, TEXT("DA"));
		const FString PackagePath = FPackageName::ObjectPathToPackageName(ObjectPath);
		if (ObjectPath.IsEmpty() || TablePath != PackagePath)
			return Fail(Field, FString::Printf(TEXT("expected package '%s' (object '%s'), got '%s'"), *PackagePath, *ObjectPath, *TablePath));
		// Editor authoring only: cooked hard references are resident before runtime resolution.
		auto* Table = LoadObject<UElysiumExpressionData>(nullptr, *ObjectPath);
		if (!Table) return Fail(Field, TEXT("native table is absent: ") + ObjectPath);
		if (Table->AssetId != Id) return Fail(Field, TEXT("loaded asset identity differs: ") + Table->AssetId);
		Tables.Add(Id, Table);
	}
	Asset->Tables = MoveTemp(Tables); Asset->MarkPackageDirty(); OutError.Reset(); return Asset;
#else
	OutError = TEXT("expression import is editor only"); return nullptr;
#endif
}

FString UElysiumExpressionTables::Verify(UElysiumExpressionTables* Asset, const FString& Json)
{
#if WITH_EDITOR
	auto* Expected = NewObject<UElysiumExpressionTables>(); FString Error;
	if (!Asset || !ApplyJson(Expected, Json, Error)) return Error.IsEmpty() ? TEXT("expression corpus is absent") : Error;
	return VerifyFields(Asset, Expected, StaticClass());
#else
	return TEXT("expression verification is editor only");
#endif
}
