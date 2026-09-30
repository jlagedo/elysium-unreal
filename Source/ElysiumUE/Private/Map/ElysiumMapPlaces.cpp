#include "ElysiumMapPlaces.h"

#include "Map/ElysiumMapLog.h"
#include "Substrate/ElysiumRetailHullTable.h"
#if WITH_EDITOR
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#endif

// The row carries one Z offset per retail hull: `CAI_Node +0x14 .. +0x6b`, then the yaw at `+0x6c`.
static_assert(sizeof(FElysiumPlaceRow::ZOffsetCm) / sizeof(float) == ElysiumRetailHulls::Count,
	"FElysiumPlaceRow::ZOffsetCm is one offset per retail hull");

bool UElysiumMapPlaces::IsValidPlaces() const
{
	if (PayloadVersion != SupportedPayloadVersion || NumNodes != Rows.Num())
	{
		return false;
	}
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		if (Rows[Index].NetworkIndex != Index)
		{
			return false;
		}
	}
	return true;
}

#if WITH_EDITOR
namespace
{
	bool ReadVector(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, FVector& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Object->TryGetArrayField(Key, Values) || Values->Num() != 3)
		{
			return false;
		}
		Out = FVector((*Values)[0]->AsNumber(), (*Values)[1]->AsNumber(), (*Values)[2]->AsNumber());
		return true;
	}

	// The whole payload into a fresh asset image, or the first reason it is refused. Nothing is
	// written to the asset until every row has read, so a refused payload leaves it as it was.
	FString ParsePlaces(const FString& Json, UElysiumMapPlaces& Out)
	{
		TSharedPtr<FJsonObject> Root;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root)
		{
			return TEXT("invalid places JSON");
		}
		int32 Version = 0;
		if (!Root->TryGetNumberField(TEXT("version"), Version)
			|| Version != UElysiumMapPlaces::SupportedPayloadVersion)
		{
			return FString::Printf(TEXT("unsupported places payload version %d (want %d)"),
				Version, UElysiumMapPlaces::SupportedPayloadVersion);
		}
		Out.PayloadVersion = Version;
		if (!Root->TryGetStringField(TEXT("map"), Out.MapName) || Out.MapName.IsEmpty())
		{
			return TEXT("places payload names no map");
		}
		if (!Root->TryGetNumberField(TEXT("numNodes"), Out.NumNodes)
			|| !Root->TryGetNumberField(TEXT("usedHullBits"), Out.UsedHullBits))
		{
			return TEXT("places payload lacks numNodes/usedHullBits");
		}

		const TArray<TSharedPtr<FJsonValue>>* Places = nullptr;
		if (!Root->TryGetArrayField(TEXT("places"), Places))
		{
			return TEXT("places payload lacks places");
		}
		for (const TSharedPtr<FJsonValue>& Value : *Places)
		{
			const TSharedPtr<FJsonObject>* Row = nullptr;
			if (!Value->TryGetObject(Row))
			{
				return TEXT("a place row is not an object");
			}
			FElysiumPlaceRow& Place = Out.Rows.AddDefaulted_GetRef();
			const TArray<TSharedPtr<FJsonValue>>* Offsets = nullptr;
			double Yaw = 0.0;
			if (!(*Row)->TryGetNumberField(TEXT("index"), Place.NetworkIndex)
				|| !(*Row)->TryGetNumberField(TEXT("type"), Place.Type)
				|| !(*Row)->TryGetNumberField(TEXT("flags"), Place.Flags)
				|| !ReadVector(*Row, TEXT("origin"), Place.OriginCm)
				|| !(*Row)->TryGetNumberField(TEXT("yaw"), Yaw)
				|| !(*Row)->TryGetArrayField(TEXT("zOffsets"), Offsets)
				|| !(*Row)->TryGetNumberField(TEXT("wcId"), Place.WcId)
				|| !(*Row)->TryGetNumberField(TEXT("hint"), Place.HintBspIndex))
			{
				return FString::Printf(TEXT("place row %d is missing a field"), Out.Rows.Num() - 1);
			}
			if (Place.NetworkIndex != Out.Rows.Num() - 1)
			{
				return FString::Printf(TEXT("place row %d carries index %d"), Out.Rows.Num() - 1,
					Place.NetworkIndex);
			}
			if (Offsets->Num() != ElysiumRetailHulls::Count)
			{
				return FString::Printf(TEXT("place %d carries %d hull offsets (want %d)"),
					Place.NetworkIndex, Offsets->Num(), ElysiumRetailHulls::Count);
			}
			Place.YawDeg = static_cast<float>(Yaw);
			for (int32 Hull = 0; Hull < ElysiumRetailHulls::Count; ++Hull)
			{
				Place.ZOffsetCm[Hull] = static_cast<float>((*Offsets)[Hull]->AsNumber());
			}
		}
		if (Out.NumNodes != Out.Rows.Num())
		{
			return FString::Printf(TEXT("numNodes %d but %d place rows"), Out.NumNodes, Out.Rows.Num());
		}

		const TSharedPtr<FJsonObject>* Pairing = nullptr;
		if (!Root->TryGetObjectField(TEXT("pairing"), Pairing)
			|| !(*Pairing)->TryGetNumberField(TEXT("nodeRows"), Out.PairingNodeRows))
		{
			return TEXT("places payload lacks its pairing report");
		}
		const TArray<TSharedPtr<FJsonValue>>* OutOfRange = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Standalone = nullptr;
		if (!(*Pairing)->TryGetArrayField(TEXT("outOfRange"), OutOfRange)
			|| !(*Pairing)->TryGetArrayField(TEXT("standalone"), Standalone))
		{
			return TEXT("places pairing lacks outOfRange/standalone");
		}
		for (const TSharedPtr<FJsonValue>& Value : *OutOfRange)
		{
			const TSharedPtr<FJsonObject>* Row = nullptr;
			FElysiumPlaceOutOfRange& Past = Out.PairingOutOfRange.AddDefaulted_GetRef();
			if (!Value->TryGetObject(Row) || !(*Row)->TryGetNumberField(TEXT("bspIndex"), Past.BspIndex)
				|| !(*Row)->TryGetNumberField(TEXT("counter"), Past.Counter))
			{
				return TEXT("an out-of-range pairing row is malformed");
			}
		}
		for (const TSharedPtr<FJsonValue>& Value : *Standalone)
		{
			Out.PairingStandalone.Add(static_cast<int32>(Value->AsNumber()));
		}

		const TArray<TSharedPtr<FJsonValue>>* Crosswalks = nullptr;
		if (!Root->TryGetArrayField(TEXT("crosswalkPairs"), Crosswalks))
		{
			return TEXT("places payload lacks crosswalkPairs");
		}
		for (const TSharedPtr<FJsonValue>& Value : *Crosswalks)
		{
			// `[a, b, motion]`: the two node indices and the link's hull-0 motion word. A two-entry
			// pair is a payload staged before the word rode it (re-stage the map).
			const TArray<TSharedPtr<FJsonValue>>& Pair = Value->AsArray();
			if (Pair.Num() != 3)
			{
				return TEXT("a crosswalk pair is not [node, node, hull-0 motion word]");
			}
			const FIntPoint Ends(static_cast<int32>(Pair[0]->AsNumber()), static_cast<int32>(Pair[1]->AsNumber()));
			if (!Out.Rows.IsValidIndex(Ends.X) || !Out.Rows.IsValidIndex(Ends.Y))
			{
				return FString::Printf(TEXT("crosswalk pair (%d, %d) names no node"), Ends.X, Ends.Y);
			}
			Out.CrosswalkPairs.Add(Ends);
			Out.CrosswalkPairMotions.Add(static_cast<int32>(Pair[2]->AsNumber()));
		}

		const TArray<TSharedPtr<FJsonValue>>* Caps = nullptr;
		if (!Root->TryGetArrayField(TEXT("wanderCaps"), Caps))
		{
			return TEXT("places payload lacks wanderCaps");
		}
		for (const TSharedPtr<FJsonValue>& Value : *Caps)
		{
			const TSharedPtr<FJsonObject>* Row = nullptr;
			FElysiumPlaceWanderCap& Cap = Out.WanderCaps.AddDefaulted_GetRef();
			double Units = 0.0;
			if (!Value->TryGetObject(Row) || !(*Row)->TryGetNumberField(TEXT("hull"), Cap.Hull)
				|| !(*Row)->TryGetNumberField(TEXT("capUnits"), Units)
				|| !(*Row)->TryGetBoolField(TEXT("fromHuman"), Cap.bFromHuman))
			{
				return TEXT("a wander cap row is malformed");
			}
			if (ElysiumRetailHulls::Find(Cap.Hull) == nullptr)
			{
				return FString::Printf(TEXT("a wander cap names hull %d"), Cap.Hull);
			}
			Cap.CapUnits = static_cast<float>(Units);
		}
		return FString();
	}
}

bool UElysiumMapPlaces::AuthorJson(const FString& Json)
{
	UElysiumMapPlaces* Staged = NewObject<UElysiumMapPlaces>(GetTransientPackage());
	const FString Error = ParsePlaces(Json, *Staged);
	if (!Error.IsEmpty())
	{
		UE_LOG(LogElysium, Error, TEXT("%s: places refused: %s"), *GetPathName(), *Error);
		return false;
	}
	MapName = MoveTemp(Staged->MapName);
	PayloadVersion = Staged->PayloadVersion;
	NumNodes = Staged->NumNodes;
	UsedHullBits = Staged->UsedHullBits;
	Rows = MoveTemp(Staged->Rows);
	CrosswalkPairs = MoveTemp(Staged->CrosswalkPairs);
	CrosswalkPairMotions = MoveTemp(Staged->CrosswalkPairMotions);
	WanderCaps = MoveTemp(Staged->WanderCaps);
	PairingNodeRows = Staged->PairingNodeRows;
	PairingOutOfRange = MoveTemp(Staged->PairingOutOfRange);
	PairingStandalone = MoveTemp(Staged->PairingStandalone);
	MarkPackageDirty();
	return IsValidPlaces();
}
#endif
