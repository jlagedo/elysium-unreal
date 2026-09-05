#include "ElysiumPhysicsData.h"

#if WITH_EDITOR
#include "ElysiumContentPaths.h"
#include "JsonObjectConverter.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"

namespace
{
	using FObject = TSharedPtr<FJsonObject>;
	using FValues = TArray<TSharedPtr<FJsonValue>>;
	bool Fail(FString& Error, const FString& Reason) { Error = TEXT("physics source: ") + Reason; return false; }
	bool Parse(const FString& Text, FObject& Object)
	{
		return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object) && Object.IsValid();
	}
	bool StrictValue(const TSharedPtr<FJsonValue>& Value, FProperty* Property, const FString& Path, FString& Error);
	bool StrictObject(const FObject& Object, const UStruct* Type, const FString& Path, FString& Error)
	{
		if (!Object) return Fail(Error, Path + TEXT(": expected object"));
		int32 Count = 0;
		for (TFieldIterator<FProperty> It(Type); It; ++It)
		{
			++Count;
			if (!StrictValue(Object->TryGetField(It->GetName()), *It, Path + TEXT(".") + It->GetName(), Error)) return false;
		}
		if (Count != Object->Values.Num())
		{
			for (const auto& Pair : Object->Values)
			{
				const FString Key(Pair.Key);
				if (!FindFProperty<FProperty>(Type, *Key)) return Fail(Error, Path + TEXT(".") + Key + TEXT(": unexpected field"));
			}
			return Fail(Error, Path + TEXT(": reflected field count differs"));
		}
		return true;
	}
	bool StrictValue(const TSharedPtr<FJsonValue>& Value, FProperty* Property, const FString& Path, FString& Error)
	{
		if (!Value || Value->Type == EJson::Null) return Fail(Error, Path + TEXT(": required non-null field"));
		if (auto* Array = CastField<FArrayProperty>(Property))
		{
			const FValues* Values = nullptr;
			if (!Value->TryGetArray(Values)) return Fail(Error, Path + TEXT(": expected array"));
			for (int32 I = 0; I < Values->Num(); ++I)
				if (!StrictValue((*Values)[I], Array->Inner, FString::Printf(TEXT("%s[%d]"), *Path, I), Error)) return false;
			return true;
		}
		if (auto* Struct = CastField<FStructProperty>(Property))
		{
			const FObject* Object = nullptr;
			if (!Value->TryGetObject(Object) || !Object) return Fail(Error, Path + TEXT(": expected object"));
			return StrictObject(*Object, Struct->Struct, Path, Error);
		}
		if (CastField<FBoolProperty>(Property)) return Value->Type == EJson::Boolean || Fail(Error, Path + TEXT(": expected boolean"));
		if (CastField<FStrProperty>(Property) || CastField<FNameProperty>(Property)) return Value->Type == EJson::String || Fail(Error, Path + TEXT(": expected string"));
		if (auto* Number = CastField<FNumericProperty>(Property))
		{
			double N = 0.;
			return (Value->Type == EJson::Number && Value->TryGetNumber(N) && FMath::IsFinite(N)
				&& (!Number->IsInteger() || (N >= MIN_int32 && N <= MAX_int32 && double(int32(N)) == N)))
				|| Fail(Error, Path + (Number->IsInteger() ? TEXT(": expected finite int32 without truncation") : TEXT(": expected finite number")));
		}
		return Fail(Error, Path + TEXT(": unsupported reflected property type"));
	}
	bool NumberEquals(const FObject& Object, const FString& Key, double Expected)
	{
		const auto Value = Object->TryGetField(Key); double N = 0.;
		return Value && Value->Type == EJson::Number && Value->TryGetNumber(N) && FMath::IsFinite(N) && N == Expected;
	}
	bool StringEquals(const FObject& Object, const FString& Key, const FString& Expected, bool bOptional = false)
	{
		const auto Value = Object->TryGetField(Key); FString Text;
		return (!Value && bOptional && Expected.IsEmpty())
			|| (Value && Value->Type == EJson::String && Value->TryGetString(Text) && Text == Expected);
	}
	bool NumbersEqual(const FObject& Object, const FString& Key, const TArray<double>& Expected, bool bOptional = false)
	{
		if (!Object->HasField(Key)) return bOptional && Expected.IsEmpty();
		const FValues* Values = nullptr;
		if (!Object->TryGetArrayField(Key, Values) || Values->Num() != Expected.Num()) return false;
		for (int32 I = 0; I < Expected.Num(); ++I)
		{
			double N = 0.;
			if ((*Values)[I]->Type != EJson::Number || !(*Values)[I]->TryGetNumber(N) || !FMath::IsFinite(N) || N != Expected[I]) return false;
		}
		return true;
	}
	bool OptionalEquals(const FObject& Object, const FString& Key, const FElysiumPhysicsOptionalNumber& Expected)
	{
		return Expected.bPresent ? NumberEquals(Object, Key, Expected.Value)
			: !Object->HasField(Key) && Expected.Value == 0.;
	}
	bool ParametersEqual(const FObject& Object, const TArray<FElysiumPhysicsNamedNumber>& Parameters)
	{
		int32 Count = 0; TSet<FString> Used;
		for (const auto& Pair : Object->Values) if (Pair.Value->Type == EJson::Number) ++Count;
		for (const auto& Parameter : Parameters)
		{
			if (Used.Contains(Parameter.Name) || !NumberEquals(Object, Parameter.Name, Parameter.Value)) return false;
			Used.Add(Parameter.Name);
		}
		return Count == Parameters.Num();
	}
	FObject RowObject(const FValues& Rows, int32 I)
	{
		const FObject* Object = nullptr;
		return Rows.IsValidIndex(I) && Rows[I]->TryGetObject(Object) && Object ? *Object : nullptr;
	}
	bool Hash(const FString& Text)
	{
		if (Text.Len() != 64) return false;
		for (TCHAR C : Text) if (!((C >= '0' && C <= '9') || (C >= 'a' && C <= 'f'))) return false;
		return true;
	}
	bool Validate(const FElysiumPhysicsSourceData& Data, FString& Error)
	{
		if (Data.SchemaVersion != TEXT("1.0.0")) return Fail(Error, TEXT("schemaVersion: expected '1.0.0', got '") + Data.SchemaVersion + TEXT("'"));
		if (!Data.AssetId.StartsWith(TEXT("vtmb:model:"), ESearchCase::CaseSensitive))
			return Fail(Error, TEXT("assetId: expected vtmb:model: identity, got '") + Data.AssetId + TEXT("'"));
		// The Python projection stores a package; BakedUnit returns an object path.
		// Convert the expected resolver result only. An object path supplied as AssetPath
		// remains invalid, as do wrong roles/units and all other source validation failures.
		const FString ExpectedObject = FElysiumContentPaths::BakedUnit(Data.AssetId, TEXT("DA"), TEXT("physics"));
		if (ExpectedObject.IsEmpty()) return Fail(Error, TEXT("assetId: BakedUnit rejected '") + Data.AssetId + TEXT("'"));
		const FString ExpectedPackage = FPackageName::ObjectPathToPackageName(ExpectedObject);
		if (Data.AssetPath.IsEmpty() || Data.AssetPath != ExpectedPackage)
			return Fail(Error, FString::Printf(TEXT("assetPath: %s expected package '%s' (object '%s'), got '%s'"),
				*Data.AssetId, *ExpectedPackage, *ExpectedObject, *Data.AssetPath));
		if (!Hash(Data.SourceGlbSha256)) return Fail(Error, TEXT("sourceGlbSha256: expected 64 lowercase hex characters, got '") + Data.SourceGlbSha256 + TEXT("'"));
		if (!Hash(Data.StagedBodySha256)) return Fail(Error, TEXT("stagedBodySha256: expected 64 lowercase hex characters, got '") + Data.StagedBodySha256 + TEXT("'"));
		if (Data.GeometryFrame != TEXT("IVP metres, axis-only"))
			return Fail(Error, TEXT("geometryFrame: expected 'IVP metres, axis-only', got '") + Data.GeometryFrame + TEXT("'"));
		FObject Evidence;
		if (!Parse(Data.SourceEvidenceJson, Evidence)) return Fail(Error, TEXT("invalid cooked evidence JSON"));
		const FValues* Bones = nullptr;
		if (!Evidence->TryGetArrayField(TEXT("bones"), Bones) || Bones->Num() != Data.Bones.Num()) return Fail(Error, TEXT("bone evidence count"));
		TSet<FString> BoneNames; TSet<FName> NativeNames;
		for (int32 I = 0; I < Data.Bones.Num(); ++I)
		{
			const auto& Bone = Data.Bones[I]; const auto Source = RowObject(*Bones, I);
			if (!Source || Bone.Index != I || Bone.Parent < -1 || Bone.Parent >= I || Bone.SourceName.IsEmpty()
				|| Bone.NativeName.IsNone() || Bone.PoseToBone.Num() != 12 || BoneNames.Contains(Bone.SourceName.ToLower()) || NativeNames.Contains(Bone.NativeName)
				|| !NumberEquals(Source, TEXT("index"), I) || !NumberEquals(Source, TEXT("parent"), Bone.Parent)
				|| !StringEquals(Source, TEXT("name"), Bone.SourceName) || !NumbersEqual(Source, TEXT("poseToBone"), Bone.PoseToBone))
				return Fail(Error, FString::Printf(TEXT("bone %d disagrees with evidence"), I));
			BoneNames.Add(Bone.SourceName.ToLower()); NativeNames.Add(Bone.NativeName);
		}
		const auto PhysicsValue = Evidence->TryGetField(TEXT("physics"));
		if (!PhysicsValue) return Fail(Error, TEXT("missing explicit physics object/null"));
		if (!Data.bHasPhysics)
			return (PhysicsValue->Type == EJson::Null && Data.HeaderParameters.IsEmpty() && Data.Solids.IsEmpty() && Data.Constraints.IsEmpty()
				&& Data.KeyValues.IsEmpty() && Data.EditParams.IsEmpty() && Data.Gaps.IsEmpty()) || Fail(Error, TEXT("false physics absence"));
		const FObject *Physics = nullptr, *Header = nullptr;
		const FValues *Solids = nullptr, *Constraints = nullptr, *KV = nullptr, *Edits = nullptr, *Accessors = nullptr;
		if (!PhysicsValue->TryGetObject(Physics) || !Physics || !(*Physics)->TryGetObjectField(TEXT("header"), Header)
			|| !ParametersEqual(*Header, Data.HeaderParameters) || !StringEquals(*Physics, TEXT("coordinateSystem"), Data.GeometryFrame)
			|| !(*Physics)->TryGetArrayField(TEXT("solids"), Solids)
			|| !(*Physics)->TryGetArrayField(TEXT("constraints"), Constraints) || !(*Physics)->TryGetArrayField(TEXT("keyValues"), KV)
			|| !(*Physics)->TryGetArrayField(TEXT("editParams"), Edits) || !Evidence->TryGetArrayField(TEXT("accessors"), Accessors)
			|| Solids->Num() != Data.Solids.Num() || Constraints->Num() != Data.Constraints.Num()
			|| KV->Num() != Data.KeyValues.Num() || Edits->Num() != Data.EditParams.Num()) return Fail(Error, TEXT("physics evidence cardinality"));
		TSet<int32> UsedGaps;
		auto Gap = [&](const FString& Kind, int32 Ordinal, const FString& Field, const FString& Name)
		{
			for (int32 I = 0; I < Data.Gaps.Num(); ++I)
			{
				const auto& G = Data.Gaps[I];
				if (!UsedGaps.Contains(I) && G.Kind == Kind && G.Ordinal == Ordinal && G.Field == Field && G.SourceName == Name && !G.Reason.IsEmpty())
				{ UsedGaps.Add(I); return true; }
			}
			return false;
		};
		auto BoneIndex = [&](const FString& Name)
		{
			return Data.Bones.IndexOfByPredicate([&](const auto& Bone) { return Bone.SourceName.Equals(Name, ESearchCase::IgnoreCase); });
		};
		for (int32 I = 0; I < Data.Solids.Num(); ++I)
		{
			const auto& Solid = Data.Solids[I]; const auto Source = RowObject(*Solids, I);
			const FObject* Props = nullptr; const FValues* Hulls = nullptr;
			if (!Source || Solid.Ordinal != I || Solid.BinaryIndex < 0 || Solid.SourceOffset < 0
				|| (!Solid.Origin.IsEmpty() && Solid.Origin.Num() != 3) || (!Solid.Angles.IsEmpty() && Solid.Angles.Num() != 3)
				|| Solid.MassCenterIvp.Num() != 3 || Solid.RotationInertiaIvp.Num() != 3
				|| (Solid.AuthoredIndex.bPresent && (Solid.AuthoredIndex.Value < 0 || Solid.AuthoredIndex.Value > MAX_int32 || double(int32(Solid.AuthoredIndex.Value)) != Solid.AuthoredIndex.Value))
				|| !NumberEquals(Source, TEXT("index"), Solid.BinaryIndex)
				|| !NumberEquals(Source, TEXT("sourceOffset"), Solid.SourceOffset) || !Source->TryGetObjectField(TEXT("properties"), Props)
				|| !Source->TryGetArrayField(TEXT("hulls"), Hulls) || Hulls->Num() != Solid.Hulls.Num()
				|| !OptionalEquals(*Props, TEXT("index"), Solid.AuthoredIndex) || !ParametersEqual(*Props, Solid.Parameters)
				|| !StringEquals(*Props, TEXT("name"), Solid.SourceName, true) || !StringEquals(*Props, TEXT("parent"), Solid.SourceParent, true)
				|| !StringEquals(*Props, TEXT("surfaceprop"), Solid.SurfaceProperty, true)
				|| !NumbersEqual(*Props, TEXT("origin"), Solid.Origin, true) || !NumbersEqual(*Props, TEXT("angles"), Solid.Angles, true)
				|| !NumbersEqual(Source, TEXT("massCenter"), Solid.MassCenterIvp) || !NumbersEqual(Source, TEXT("rotationInertia"), Solid.RotationInertiaIvp))
				return Fail(Error, FString::Printf(TEXT("solid %d disagrees with evidence"), I));
			if (Solid.SourceBoneIndex != BoneIndex(Solid.SourceName) || Solid.ParentBoneIndex != BoneIndex(Solid.SourceParent)) return Fail(Error, TEXT("incorrect source bone join"));
			if (Solid.SourceBoneIndex < 0)
			{
				if (!Solid.NativeBoneName.IsNone() || !Gap(TEXT("source-bone"), I, TEXT("name"), Solid.SourceName)) return Fail(Error, TEXT("missing source-bone gap"));
			}
			else if (Solid.NativeBoneName != Data.Bones[Solid.SourceBoneIndex].NativeName) return Fail(Error, TEXT("native bone identity differs"));
			if (!Solid.SourceParent.IsEmpty() && Solid.ParentBoneIndex < 0 && !Gap(TEXT("source-bone"), I, TEXT("parent"), Solid.SourceParent)) return Fail(Error, TEXT("missing parent-bone gap"));
			for (int32 H = 0; H < Solid.Hulls.Num(); ++H)
			{
				const auto& Hull = Solid.Hulls[H]; const auto RawHull = RowObject(*Hulls, H);
				if (!RawHull || Hull.SolidOrdinal != I || Hull.LedgeOrdinal != H || Hull.Vertices.IsEmpty() || Hull.Indices.Num() % 3
					|| !NumberEquals(RawHull, TEXT("sourceOffset"), Hull.SourceOffset)
					|| !NumberEquals(RawHull, TEXT("positions"), Hull.PositionAccessor) || !NumberEquals(RawHull, TEXT("indices"), Hull.IndexAccessor)) return Fail(Error, TEXT("lost hull ownership/accessor"));
				for (int32 Index : Hull.Indices) if (!Hull.Vertices.IsValidIndex(Index)) return Fail(Error, TEXT("hull triangle index out of range"));
				for (const auto& Point : Hull.Vertices) if (!FMath::IsFinite(Point.X) || !FMath::IsFinite(Point.Y) || !FMath::IsFinite(Point.Z)) return Fail(Error, TEXT("nonfinite hull point"));
				for (bool bPoints : {true, false})
				{
					bool bFound = false;
					for (int32 A = 0; A < Accessors->Num(); ++A)
					{
						const auto Description = RowObject(*Accessors, A); const FObject* Accessor = nullptr;
						if (Description && NumberEquals(Description, TEXT("index"), bPoints ? Hull.PositionAccessor : Hull.IndexAccessor))
						{
							if (bFound || !Description->TryGetObjectField(TEXT("accessor"), Accessor)
								|| !NumberEquals(*Accessor, TEXT("count"), bPoints ? Hull.Vertices.Num() : Hull.Indices.Num())) return Fail(Error, TEXT("accessor geometry count disagrees"));
							bFound = true;
						}
					}
					if (!bFound) return Fail(Error, TEXT("missing accessor evidence"));
				}
			}
		}
		for (int32 I = 0; I < Data.Constraints.Num(); ++I)
		{
			const auto& Joint = Data.Constraints[I]; const auto Source = RowObject(*Constraints, I);
			if (!Source || Joint.Ordinal != I || Joint.Axes.Num() != 3 || !NumberEquals(Source, TEXT("parent"), Joint.ParentSolidIndex)
				|| !NumberEquals(Source, TEXT("child"), Joint.ChildSolidIndex)) return Fail(Error, TEXT("constraint topology disagrees"));
			for (bool bParent : {true, false})
			{
				const int32 Id = bParent ? Joint.ParentSolidIndex : Joint.ChildSolidIndex;
				const int32 Ordinal = bParent ? Joint.ParentSolidOrdinal : Joint.ChildSolidOrdinal;
				int32 Matches = 0, Found = INDEX_NONE;
				for (const auto& Solid : Data.Solids) if (Solid.AuthoredIndex.bPresent && Solid.AuthoredIndex.Value == Id) { ++Matches; Found = Solid.Ordinal; }
				if (Ordinal != (Matches == 1 ? Found : INDEX_NONE)) return Fail(Error, TEXT("wrong authored solid join"));
				if (Matches != 1 && !Gap(TEXT("solid-reference"), I, bParent ? TEXT("parent") : TEXT("child"), FString::FromInt(Id))) return Fail(Error, TEXT("missing solid-reference gap"));
			}
			for (int32 A = 0; A < 3; ++A)
			{
				const auto& Axis = Joint.Axes[A]; const FString Name = FString::Chr(TEXT('x') + A);
				if (Axis.Name != Name || !OptionalEquals(Source, Name + TEXT("min"), Axis.Minimum)
					|| !OptionalEquals(Source, Name + TEXT("max"), Axis.Maximum) || !OptionalEquals(Source, Name + TEXT("friction"), Axis.Friction)) return Fail(Error, TEXT("source limit/friction changed"));
			}
		}
		for (int32 I = 0; I < Data.KeyValues.Num(); ++I)
		{
			const auto Source = RowObject(*KV, I); const FValues* Pairs = nullptr; const auto& Block = Data.KeyValues[I];
			if (!Source || !StringEquals(Source, TEXT("type"), Block.BlockType) || !Source->TryGetArrayField(TEXT("pairs"), Pairs) || Pairs->Num() != Block.Pairs.Num()) return Fail(Error, TEXT("KV block order/count changed"));
			for (int32 P = 0; P < Pairs->Num(); ++P)
			{
				const auto Pair = RowObject(*Pairs, P);
				if (!Pair || !StringEquals(Pair, TEXT("key"), Block.Pairs[P].Key) || !StringEquals(Pair, TEXT("value"), Block.Pairs[P].Value)) return Fail(Error, TEXT("KV pair order/value changed"));
			}
		}
		for (int32 I = 0; I < Data.EditParams.Num(); ++I)
		{
			const auto Source = RowObject(*Edits, I);
			if (!Source || Data.EditParams[I].Ordinal != I || !ParametersEqual(Source, Data.EditParams[I].Parameters)) return Fail(Error, TEXT("editparams changed"));
		}
		return UsedGaps.Num() == Data.Gaps.Num() || Fail(Error, TEXT("extraneous gap record"));
	}
	bool Decode(const FString& Json, FElysiumPhysicsSourceData& Data, FString& Error)
	{
		FObject Object; FText Detail;
		if (!Parse(Json, Object)) return Fail(Error, TEXT("projection: invalid JSON object"));
		if (!StrictObject(Object, FElysiumPhysicsSourceData::StaticStruct(), TEXT("projection"), Error)) return false;
		if (!FJsonObjectConverter::JsonObjectToUStruct(Object.ToSharedRef(), &Data, 0, 0, true, &Detail)) return Fail(Error, Detail.ToString());
		return Validate(Data, Error);
	}
}
#endif

UElysiumPhysicsData* UElysiumPhysicsData::ApplyJson(UElysiumPhysicsData* Asset, const FString& Json, FString& OutError)
{
	OutError.Reset();
#if WITH_EDITOR
	FElysiumPhysicsSourceData Pending;
	if (!Asset) { OutError = TEXT("physics source asset is absent"); return nullptr; }
	if (!Decode(Json, Pending, OutError)) return nullptr;
	Asset->Data = MoveTemp(Pending); Asset->MarkPackageDirty(); return Asset;
#else
	OutError = TEXT("physics source authoring is editor only"); return nullptr;
#endif
}

FString UElysiumPhysicsData::Verify(UElysiumPhysicsData* Asset, const FString& Json)
{
#if WITH_EDITOR
	FElysiumPhysicsSourceData Expected; FString Error;
	if (!Asset) return TEXT("physics source asset is absent");
	if (!Decode(Json, Expected, Error)) return Error;
	return FElysiumPhysicsSourceData::StaticStruct()->CompareScriptStruct(&Asset->Data, &Expected, 0)
		? FString() : TEXT("saved physics source data differs (including cooked evidence/geometry)");
#else
	return TEXT("physics source verification is editor only");
#endif
}
