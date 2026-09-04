#include "ElysiumSurfaceSettings.h"

#include "ElysiumLightingSettings.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Misc/CoreDelegates.h"
#include "Misc/PackageName.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumSurfaceSettings, Log, All);

#if WITH_EDITOR
namespace
{
	/**
	 * Re-push the ini's values into `MPC_ElysiumSurfaces`'s asset defaults on every editor boot,
	 * so the cooked collection a packaged build reads never drifts from the ini between an edit
	 * and the next cook (H2: a restart previously left the collection's on-disk defaults as of
	 * whenever it was last saved, which could predate the ini's current values).
	 */
	struct FElysiumSurfaceSettingsStartupPush
	{
		FElysiumSurfaceSettingsStartupPush()
		{
			FCoreDelegates::GetOnPostEngineInit().AddLambda([]()
			{
				if (UElysiumSurfaceSettings* Settings = GetMutableDefault<UElysiumSurfaceSettings>())
				{
					Settings->PushToCollection();
				}
			});
		}
	};
	const FElysiumSurfaceSettingsStartupPush GElysiumSurfaceSettingsStartupPush;
}
#endif

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
		{FName(TEXT("ClassInfluence")), &UElysiumSurfaceSettings::ClassInfluence},
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
		{FName(TEXT("CaptureRadius")), &UElysiumSurfaceSettings::CaptureRadius},
		{FName(TEXT("DetailSwayAmplitude")), &UElysiumSurfaceSettings::DetailSwayAmplitude},
	};
	return Bindings;
}

UMaterialParameterCollection* UElysiumSurfaceSettings::LoadCollection()
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
			TEXT("%s not found -- run `uv run elysium export bundle policy` (make_surface_knobs.py) "
				 "to create it; settings edits have nowhere to go until then."),
			CollectionPath());
	}
	return Collection;
}

void UElysiumSurfaceSettings::PushToCollection() const
{
	UMaterialParameterCollection* Collection = LoadCollection();
	if (!Collection)
	{
		return;
	}
	PushToCollectionDefaults(Collection);
	PushToWorldInstances(Collection);
}

void UElysiumSurfaceSettings::PushToCollectionDefaults(UMaterialParameterCollection* Collection) const
{
#if WITH_EDITOR
	// First check without mutating anything: Modify()/PreEditChange() are not free (a transaction
	// record, a storage-size snapshot), and most pushes -- a PIE tick, a settings load with no
	// actual value drift -- change nothing at all.
	bool bAnyDiffers = false;
	for (const TPair<FName, float UElysiumSurfaceSettings::*>& Binding : ScalarBindings())
	{
		for (const FCollectionScalarParameter& Parameter : Collection->ScalarParameters)
		{
			if (Parameter.ParameterName == Binding.Key)
			{
				if (Parameter.DefaultValue != this->*Binding.Value)
				{
					bAnyDiffers = true;
				}
				break;
			}
		}
		if (bAnyDiffers)
		{
			break;
		}
	}
	if (!bAnyDiffers)
	{
		return;
	}

	// PreEditChange/PostEditChange bracketing the mutation, the engine's own idiom, is what makes
	// ParameterCollection.cpp's PostEditChangeProperty take its cheap branch: PreEditChange snapshots
	// PreviousTotalVectorStorage *before* the mutation, and PostEditChangeProperty only rebuilds the
	// deferred-parameter uniform buffers (and recompiles every referencing material) when the
	// post-edit storage size actually differs from that snapshot. This push only ever changes a
	// scalar's DefaultValue, never adds or removes a row, so the storage size never differs and the
	// cheap "just update the contents" branch runs -- but only when PreEditChange ran first with the
	// pre-mutation layout; skipping it (or calling it after mutating) leaves the snapshot stale from
	// whatever the last property-grid edit happened to set it to.
	Collection->Modify();
	Collection->PreEditChange(nullptr);
	for (const TPair<FName, float UElysiumSurfaceSettings::*>& Binding : ScalarBindings())
	{
		for (FCollectionScalarParameter& Parameter : Collection->ScalarParameters)
		{
			if (Parameter.ParameterName == Binding.Key)
			{
				Parameter.DefaultValue = this->*Binding.Value;
				break;
			}
		}
	}
	Collection->PostEditChange();
	Collection->MarkPackageDirty();
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
	// `EPropertyChangeType::Type` is a plain bitmask (UnrealType.h), not an exclusive enum: an
	// interactive drag can arrive combined with another flag, so testing equality against the
	// single `Interactive` value would miss those and push the expensive asset-defaults path on
	// every tick of the drag after all.
	if ((PropertyChangedEvent.ChangeType & EPropertyChangeType::Interactive) != 0)
	{
		// A slider mid-drag: PIE should still follow it live, but the collection's asset defaults
		// (Collection->Modify()/PreEditChange()/PostEditChange(), a transaction and a material-
		// recompile scan) are not something 60 ticks of one drag needs to pay for -- the terminal
		// ValueSet event below does that once, when the drag actually lets go.
		if (UMaterialParameterCollection* Collection = LoadCollection())
		{
			PushToWorldInstances(Collection);
		}
		return;
	}
	PushToCollection();
	// `LightSpecularScale` is read by the light rig, not by a material (R5.5): a terminal edit
	// re-derives every live rig through the lighting page's own push, which reads this object's
	// value back. Terminal only, for the same 400-light reason that page never follows a drag.
	if (PropertyChangedEvent.GetPropertyName()
		== GET_MEMBER_NAME_CHECKED(UElysiumSurfaceSettings, LightSpecularScale))
	{
		GetDefault<UElysiumLightingSettings>()->PushToWorlds();
	}
}
#endif
