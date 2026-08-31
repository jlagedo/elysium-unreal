#include "ElysiumSurfaceSettings.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Misc/PackageName.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumSurfaceSettings, Log, All);

UElysiumSurfaceSettings::UElysiumSurfaceSettings()
{
	CategoryName = TEXT("Elysium");
	SectionName = TEXT("Surfaces");
}

const TCHAR* UElysiumSurfaceSettings::CollectionPath()
{
	return TEXT("/Game/ElysiumGenerated/Materials/V2/MPC_ElysiumSurfaces");
}

const TArray<TPair<FName, float UElysiumSurfaceSettings::*>>& UElysiumSurfaceSettings::ScalarBindings()
{
	static const TArray<TPair<FName, float UElysiumSurfaceSettings::*>> Bindings = {
		{FName(TEXT("DefaultSpecular")), &UElysiumSurfaceSettings::DefaultSpecular},
		{FName(TEXT("DefaultRoughness")), &UElysiumSurfaceSettings::DefaultRoughness},
		{FName(TEXT("DefaultMetallic")), &UElysiumSurfaceSettings::DefaultMetallic},
		{FName(TEXT("LightSpecularScale")), &UElysiumSurfaceSettings::LightSpecularScale},
		{FName(TEXT("Overbright")), &UElysiumSurfaceSettings::Overbright},
		{FName(TEXT("MaskRoughnessMin")), &UElysiumSurfaceSettings::MaskRoughnessMin},
		{FName(TEXT("MaskRoughnessMax")), &UElysiumSurfaceSettings::MaskRoughnessMax},
		{FName(TEXT("MaskSpecularScale")), &UElysiumSurfaceSettings::MaskSpecularScale},
		{FName(TEXT("MaskMetallicMax")), &UElysiumSurfaceSettings::MaskMetallicMax},
		{FName(TEXT("EnvTintScale")), &UElysiumSurfaceSettings::EnvTintScale},
		{FName(TEXT("FixedCubeStrength")), &UElysiumSurfaceSettings::FixedCubeStrength},
		{FName(TEXT("ChromaticTintStrength")), &UElysiumSurfaceSettings::ChromaticTintStrength},
		{FName(TEXT("ChromaThreshold")), &UElysiumSurfaceSettings::ChromaThreshold},
		{FName(TEXT("DecalDepthOffset")), &UElysiumSurfaceSettings::DecalDepthOffset},
		{FName(TEXT("CaptureRadius")), &UElysiumSurfaceSettings::CaptureRadius},
	};
	return Bindings;
}

void UElysiumSurfaceSettings::PushToCollection() const
{
	// CollectionPath() is a package path; LoadObject with a null outer wants the full
	// "Package.Object" form, and a top-level asset's object name equals its package's short name.
	const FString PackagePath = CollectionPath();
	const FString ObjectPath = PackagePath + TEXT(".") + FPackageName::GetShortName(PackagePath);
	UMaterialParameterCollection* Collection =
		LoadObject<UMaterialParameterCollection>(nullptr, *ObjectPath);
	if (!Collection)
	{
		UE_LOG(LogElysiumSurfaceSettings, Warning,
			TEXT("PushToCollection: %s not found -- run `uv run elysium export bundle policy` "
				 "(make_surface_knobs.py) to create it; settings edits have nowhere to go until then."),
			CollectionPath());
		return;
	}
	PushToCollectionDefaults(Collection);
	PushToWorldInstances(Collection);
}

void UElysiumSurfaceSettings::PushToCollectionDefaults(UMaterialParameterCollection* Collection) const
{
#if WITH_EDITOR
	bool bChangedAny = false;
	for (const TPair<FName, float UElysiumSurfaceSettings::*>& Binding : ScalarBindings())
	{
		for (FCollectionScalarParameter& Parameter : Collection->ScalarParameters)
		{
			if (Parameter.ParameterName == Binding.Key)
			{
				const float Value = this->*Binding.Value;
				if (Parameter.DefaultValue != Value)
				{
					Parameter.DefaultValue = Value;
					bChangedAny = true;
				}
				break;
			}
		}
	}
	if (bChangedAny)
	{
		// One PostEditChange for the whole batch: ParameterCollection.cpp refreshes every world
		// instance and the default resource off this single call.
		Collection->PostEditChange();
	}
#endif
}

void UElysiumSurfaceSettings::PushToWorldInstances(UMaterialParameterCollection* Collection) const
{
	if (!GEngine)
	{
		return;
	}
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		UWorld* World = Context.World();
		if (!World)
		{
			continue;
		}
		UMaterialParameterCollectionInstance* Instance = World->GetParameterCollectionInstance(Collection);
		if (!Instance)
		{
			continue;
		}
		for (const TPair<FName, float UElysiumSurfaceSettings::*>& Binding : ScalarBindings())
		{
			Instance->SetScalarParameterValue(Binding.Key, this->*Binding.Value);
		}
	}
}

#if WITH_EDITOR
void UElysiumSurfaceSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	PushToCollection();
}
#endif
