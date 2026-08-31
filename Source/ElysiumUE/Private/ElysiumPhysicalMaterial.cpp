#include "ElysiumPhysicalMaterial.h"

#include "ElysiumJsonField.h"
#include "ElysiumSurfacePropertyProvenance.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#if WITH_EDITORONLY_DATA
#include "UObject/MetaData.h"
#endif

const FName UElysiumPhysicalMaterial::TagAssetId(TEXT("ElysiumAssetId"));
const FName UElysiumPhysicalMaterial::TagGameMaterial(TEXT("ElysiumGameMaterial"));
const FName UElysiumPhysicalMaterial::TagSourceName(TEXT("ElysiumSourceName"));

namespace
{
	/**
	 * `SurfaceType7` (or `EPhysicalSurface::SurfaceType7`) as its enum value.
	 *
	 * The letter → row table itself lives on the Python side
	 * (`importers/surface_properties.GAME_MATERIAL_SURFACE_TYPES`), which is also what writes the
	 * `Config/DefaultEngine.ini` rows, so there is one table rather than two that can disagree:
	 * the sidecar names the row and this only resolves the name the reflection system already
	 * knows. An unknown or absent name is `SurfaceType_Default`, which is the same answer an
	 * entry with no `gamematerial` gets.
	 */
	EPhysicalSurface SurfaceTypeByName(const FString& Name)
	{
		if (Name.IsEmpty())
		{
			return SurfaceType_Default;
		}
		const UEnum* Enum = StaticEnum<EPhysicalSurface>();
		const int64 Value = Enum ? Enum->GetValueByNameString(Name) : INDEX_NONE;
		return Value == INDEX_NONE ? SurfaceType_Default : static_cast<EPhysicalSurface>(Value);
	}
}

void UElysiumPhysicalMaterial::FromJson(const TSharedRef<FJsonObject>& Object)
{
	using namespace ElysiumJson;

	AssetId = Str(Object, TEXT("assetId"), AssetId);
	SourceName = Str(Object, TEXT("sourceName"), SourceName);
	BaseChain = Strings(Object, TEXT("baseChain"));
	GameMaterial = Str(Object, TEXT("gameMaterial"), GameMaterial);
	SurfaceType = SurfaceTypeByName(Str(Object, TEXT("surfaceType")));

	if (const TSharedPtr<FJsonObject> Physics = Obj(Object, TEXT("physics")))
	{
		const TSharedRef<FJsonObject> Ref = Physics.ToSharedRef();
		Friction = Float(Ref, TEXT("friction"), Friction);
		Density = Float(Ref, TEXT("density"), Density);
		Thickness = Float(Ref, TEXT("thickness"), Thickness);
		// `elasticity` runs to 2 in the shipped table and the engine's Restitution is a 0-1
		// bounciness, so the asset carries both: the clamp the solver can use, and the number the
		// entry actually wrote.
		RawElasticity = Float(Ref, TEXT("elasticity"), RawElasticity);
		Restitution = FMath::Clamp(RawElasticity, 0.0f, 1.0f);
	}

	if (const TSharedPtr<FJsonObject> Movement = Obj(Object, TEXT("movement")))
	{
		const TSharedRef<FJsonObject> Ref = Movement.ToSharedRef();
		MaxSpeedFactor = Float(Ref, TEXT("maxSpeedFactor"), MaxSpeedFactor);
		JumpFactor = Float(Ref, TEXT("jumpFactor"), JumpFactor);
		bClimbable = Bool(Ref, TEXT("climbable"), bClimbable);
	}

	FootstepsLeft.Reset();
	FootstepsRight.Reset();
	if (const TSharedPtr<FJsonObject> Footsteps = Obj(Object, TEXT("footsteps")))
	{
		const TSharedRef<FJsonObject> Ref = Footsteps.ToSharedRef();
		FootstepsLeft = Strings(Ref, TEXT("left"));
		FootstepsRight = Strings(Ref, TEXT("right"));
	}

	Impacts.Reset();
	if (const TSharedPtr<FJsonObject> Matrix = Obj(Object, TEXT("impacts")))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Weapon : Matrix->Values)
		{
			const TSharedPtr<FJsonObject>* Row = nullptr;
			if (!Weapon.Value.IsValid() || !Weapon.Value->TryGetObject(Row) || !Row || !Row->IsValid())
			{
				continue;
			}
			const TSharedRef<FJsonObject> Outcomes = (*Row).ToSharedRef();
			FElysiumImpactOutcomes& Entry = Impacts.Add(Weapon.Key);
			Entry.Soak = Strings(Outcomes, TEXT("soak"));
			Entry.Norm = Strings(Outcomes, TEXT("norm"));
			Entry.Crit = Strings(Outcomes, TEXT("crit"));
		}
	}
	BulletImpactLegacy = Strings(Object, TEXT("bulletImpactLegacy"));

	SoundScriptImpact.Reset();
	SoundScriptScrape.Reset();
	if (const TSharedPtr<FJsonObject> Sounds = Obj(Object, TEXT("sounds")))
	{
		const TSharedRef<FJsonObject> Ref = Sounds.ToSharedRef();
		SoundScriptImpact = Strings(Ref, TEXT("impact"));
		SoundScriptScrape = Strings(Ref, TEXT("scrape"));
	}
}

void UElysiumPhysicalMaterial::ApplyJson(UPhysicalMaterial* Material, const FString& Json, bool& bOutOk, FString& OutError)
{
	bOutOk = false;
	OutError.Reset();
	UElysiumPhysicalMaterial* Surface = Cast<UElysiumPhysicalMaterial>(Material);
	if (!Surface)
	{
		OutError = Material ? TEXT("asset is not a UElysiumPhysicalMaterial") : TEXT("no physical material");
		return;
	}
	// Deserialize the top-level value rather than an object, so a well-formed document that is
	// not an object (`[]`, `5`, `"x"`) is refused by type and not by whatever the object overload
	// happens to do with it.
	TSharedPtr<FJsonValue> Parsed;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Parsed) || !Parsed.IsValid())
	{
		OutError = FString::Printf(TEXT("surface-property sidecar does not parse as JSON: %s"), *Reader->GetErrorMessage());
		return;
	}
	const TSharedPtr<FJsonObject>* Object = nullptr;
	if (!Parsed->TryGetObject(Object) || !Object || !Object->IsValid())
	{
		OutError = TEXT("surface-property sidecar is not a JSON object");
		return;
	}
	const TSharedRef<FJsonObject> Document = (*Object).ToSharedRef();

	Surface->FromJson(Document);

	// The material is the outer, so the record serializes inside the asset's package; a flag-less
	// NewObject would land it in the transient package and the save would drop it.
	UElysiumSurfacePropertyProvenance* Record = NewObject<UElysiumSurfacePropertyProvenance>(
		Surface, NAME_None, RF_Public | RF_Transactional);
	Record->FromJson(Document);
	// AddAssetUserData removes an existing instance of the same class first, so a re-import
	// replaces rather than accumulates.
	Surface->AddAssetUserData(Record);
	Surface->MarkPackageDirty();
	bOutOk = true;
}

const UElysiumSurfacePropertyProvenance* UElysiumPhysicalMaterial::FindProvenance(const UPhysicalMaterial* Material)
{
	const UElysiumPhysicalMaterial* Surface = Cast<UElysiumPhysicalMaterial>(Material);
	if (!Surface)
	{
		return nullptr;
	}
	// GetAssetUserDataOfClass is non-const on the interface; the lookup itself mutates nothing.
	return Cast<UElysiumSurfacePropertyProvenance>(
		const_cast<UElysiumPhysicalMaterial*>(Surface)->GetAssetUserDataOfClass(
			UElysiumSurfacePropertyProvenance::StaticClass()));
}

void UElysiumPhysicalMaterial::StampRegistryTags(UPhysicalMaterial* Material, bool& bOutStamped, FString& OutError)
{
	bOutStamped = false;
	OutError.Reset();
	const UElysiumSurfacePropertyProvenance* Record = FindProvenance(Material);
	if (!Record)
	{
		OutError = Material ? TEXT("material carries no ElysiumSurfacePropertyProvenance")
			: TEXT("no physical material");
		return;
	}
#if WITH_EDITORONLY_DATA
	UPackage* Package = Material->GetPackage();
	if (!Package)
	{
		OutError = TEXT("material has no package");
		return;
	}
	const UElysiumPhysicalMaterial* Surface = CastChecked<UElysiumPhysicalMaterial>(Material);
	FMetaData& Meta = Package->GetMetaData();
	Meta.SetValue(Material, TagAssetId, *Record->AssetId);
	Meta.SetValue(Material, TagGameMaterial, *Surface->GameMaterial);
	Meta.SetValue(Material, TagSourceName, *Record->SourceName);
	Material->MarkPackageDirty();
	bOutStamped = true;
#else
	OutError = TEXT("package metadata is editor-only data");
#endif
}

// ---------------------------------------------------------------------------------------------
// IInterface_AssetUserData
// ---------------------------------------------------------------------------------------------

void UElysiumPhysicalMaterial::AddAssetUserData(UAssetUserData* InUserData)
{
	if (InUserData)
	{
		if (UAssetUserData* Existing = GetAssetUserDataOfClass(InUserData->GetClass()))
		{
			AssetUserData.Remove(Existing);
		}
		AssetUserData.Add(InUserData);
	}
}

void UElysiumPhysicalMaterial::RemoveUserDataOfClass(TSubclassOf<UAssetUserData> InUserDataClass)
{
	for (int32 Index = AssetUserData.Num() - 1; Index >= 0; --Index)
	{
		UAssetUserData* Data = AssetUserData[Index];
		if (Data && Data->IsA(InUserDataClass))
		{
			AssetUserData.RemoveAt(Index);
		}
	}
}

UAssetUserData* UElysiumPhysicalMaterial::GetAssetUserDataOfClass(TSubclassOf<UAssetUserData> InUserDataClass)
{
	for (UAssetUserData* Data : AssetUserData)
	{
		if (Data && Data->IsA(InUserDataClass))
		{
			return Data;
		}
	}
	return nullptr;
}

const TArray<UAssetUserData*>* UElysiumPhysicalMaterial::GetAssetUserDataArray() const
{
	return &ToRawPtrTArrayUnsafe(AssetUserData);
}
