// AElysiumMapActor's weather domain — the IElysiumWeather implementation: the authored
// wetness presentation, the env_particle Niagara emitters and their attachment rule, the
// follow-rain viewer volume, and the rain-timer console surface.

#include "ElysiumMapActor.h"

#include "ElysiumContentPaths.h"   // FElysiumContentPaths::BakedParticleSystem — the emitter assets
#include "ElysiumEntity.h"         // FElysiumEntity — the emitter parent attach resolve
#include "ElysiumEntityDefs.h"     // FElysiumEntityDef — an emitter parent's authored origin
#include "ElysiumEntityWorld.h"    // FindByName / EnqueueInput — the weather timer's entity I/O door
#include "ElysiumMapTransportSettings.h"   // IsMapOnV2Models -- the effects cutover
#include "Map/ElysiumMapLog.h"

#include "Engine/World.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "NiagaraComponent.h"
#include "NiagaraDataSetAccessor.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraSystemInstanceController.h"
#include "UObject/UObjectIterator.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<float> CVarRainEnhancement(
	TEXT("elysium.RainEnhancement"), 0.0f,
	TEXT("Wetness presentation tuning: 0 is the authored reference; 1 enables the enhanced branch."));
static TAutoConsoleVariable<float> CVarRainRateScale(
	TEXT("elysium.RainRateScale"), 1.0f,
	TEXT("Multiplier on the authored env_particle rate for live Niagara emitters."));
static TAutoConsoleVariable<int32> CVarRainForce(
	TEXT("elysium.RainForce"), 0,
	TEXT("1 = force the follow-rain volume on, ignoring env_particle rate."));
static TAutoConsoleVariable<float> CVarRainStreakWidth(
	TEXT("elysium.RainStreakWidth"), 1.2f, TEXT("Follow-rain streak width in cm."));
static TAutoConsoleVariable<float> CVarRainStreakLength(
	TEXT("elysium.RainStreakLength"), 55.0f, TEXT("Follow-rain streak length in cm."));
static TAutoConsoleVariable<float> CVarRainStreakAlpha(
	TEXT("elysium.RainStreakAlpha"), 0.18f, TEXT("Follow-rain streak opacity."));
static TAutoConsoleVariable<float> CVarRainWetDarken(
	TEXT("elysium.RainWetDarken"), 0.06f, TEXT("Maximum enhanced full-wet base-color darkening."));
static TAutoConsoleVariable<float> CVarRainWetRoughness(
	TEXT("elysium.RainWetRoughness"), 0.10f, TEXT("Maximum enhanced full-wet roughness reduction."));
static TAutoConsoleVariable<float> CVarRainLightResponse(
	TEXT("elysium.RainLightResponse"), 0.25f, TEXT("Translucent rain response to local lights."));
static TAutoConsoleVariable<float> CVarRainSourceRetain(
	TEXT("elysium.RainSourceRetain"), 1.0f,
	TEXT("Source cubemap weight retained at full wetness enhancement."));
static TAutoConsoleVariable<float> CVarRainWetSpecular(
	TEXT("elysium.RainWetSpecular"), 0.50f,
	TEXT("Enhanced wet-surface dielectric specular level."));
static TAutoConsoleVariable<int32> CVarRainReflectionDebug(
	TEXT("elysium.RainReflectionDebug"), 0,
	TEXT("Wet reflection view: 0 final, 1 raw mask, 2 coarse mask, 3 wet factor, "
		"4 cube sample, 5 source contribution, 6 enhanced coverage."));
static TAutoConsoleVariable<int32> CVarEnvironmentWetnessOverride(
	TEXT("elysium.EnvironmentWetnessOverride"), 0,
	TEXT("1 = present the manual environment wetness value; 0 = present authored entity state."));
static TAutoConsoleVariable<float> CVarEnvironmentWetness(
	TEXT("elysium.EnvironmentWetness"), 1.0f,
	TEXT("Manual 0..1 wetness value used while EnvironmentWetnessOverride is enabled."));
static TAutoConsoleVariable<float> CVarEnvironmentWetnessScale(
	TEXT("elysium.EnvironmentWetnessScale"), 1.0f,
	TEXT("Global multiplier over each material's authored GlobalWetness proxy scale."));

namespace
{
	TOptional<bool> GPendingWeatherTimer;

	AElysiumMapActor* ActiveWeatherMap()
	{
		for (TObjectIterator<AElysiumMapActor> It; It; ++It)
		{
			if (IsValid(*It) && It->IsRuntimeActive())
			{
				return *It;
			}
		}
		return nullptr;
	}

	void FireOrQueueWeatherTimer(const bool bRainOn)
	{
		if (AElysiumMapActor* Map = ActiveWeatherMap())
		{
			Map->FireWeatherTimer(bRainOn);
			return;
		}
		// -ExecCmds can run before the boot map reaches RuntimeActive. Preserve the request and
		// deliver it on the first active weather tick so automated captures still exercise the
		// authored timer and entity I/O chain.
		GPendingWeatherTimer = bRainOn;
	}
}

static FAutoConsoleCommand GElysiumRainOn(
	TEXT("elysium.weather.rain_on"),
	TEXT("Fire sm_hub_1's authored rain_on_timer through the entity I/O queue."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		FireOrQueueWeatherTimer(true);
	}));

static FAutoConsoleCommand GElysiumRainOff(
	TEXT("elysium.weather.rain_off"),
	TEXT("Fire sm_hub_1's authored rain_off_timer through the entity I/O queue."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		FireOrQueueWeatherTimer(false);
	}));

static FAutoConsoleCommand GElysiumWeatherDump(
	TEXT("elysium.weather.dump"),
	TEXT("Print the live weather presentation summary."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		if (AElysiumMapActor* Map = ActiveWeatherMap())
		{
			UE_LOG(LogElysium, Log, TEXT("weather %s"), *Map->GetWeatherDebugSummary());
			return;
		}
		UE_LOG(LogElysium, Warning, TEXT("weather dump: no active map"));
	}));

void AElysiumMapActor::WarnEmitterOnce(const FString& Key, TFunctionRef<FString()> Message)
{
	if (ReportedEmitterFailures.Contains(Key))
	{
		return;
	}
	ReportedEmitterFailures.Add(Key);
	UE_LOG(LogElysium, Warning, TEXT("%s"), *Message());
}

void AElysiumMapActor::ApplyWetness(const FElysiumWeatherTransition& Transition)
{
	WetnessTransition = Transition;
	ApplyWeatherTuning();
}

namespace
{
bool IsFollowRainDefinition(const FString& Definition)
{
	return Definition.Equals(TEXT("rain_follow_emitter"), ESearchCase::IgnoreCase);
}
}

void AElysiumMapActor::ApplyEmitter(const FElysiumWeatherEmitterState& Emitter)
{
	// On a converted map the emitter is a bake-placed actor; the viewer-box modes 10/11 are
	// weather's rain follow on either path.
	const bool bPlacedEffects = ElysiumMapTransport::IsMapOnV2Models(MapName);
	const bool bViewerBox = Emitter.AttachType == 10 || Emitter.AttachType == 11;
	// One viewer-volume system for every rain_follow_emitter. Two hub entities share it.
	if (IsFollowRainDefinition(Emitter.ParticleDefinition) || (bPlacedEffects && bViewerBox))
	{
		if (!Emitter.bActive)
		{
			RainEmitterStates.Remove(Emitter.Entity.Index);
			RefreshFollowRain();
			return;
		}
		RainEmitterStates.Add(Emitter.Entity.Index, Emitter);
		RefreshFollowRain();
		return;
	}
	if (bPlacedEffects)
	{
		ApplyEmitterToPlacedActor(Emitter);
		return;
	}
	if (!Emitter.bActive)
	{
		RemoveEmitter(Emitter.Entity);
		return;
	}

	UNiagaraComponent* Component = RainComponents.FindRef(Emitter.Entity.Index);
	if (!Component)
	{
		const FString Path = FElysiumContentPaths::BakedParticleSystem(MapName, Emitter.ParticleDefinition);
		UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *Path);
		if (!System)
		{
			WarnEmitterOnce(FString::Printf(TEXT("system|%d|%s"),
				Emitter.Entity.Index, *Emitter.ParticleDefinition.ToLower()),
				[&]
				{
					return FString::Printf(
						TEXT("particle emitter %d on map '%s' cannot load definition '%s' from '%s'"),
						Emitter.Entity.Index, *MapName, *Emitter.ParticleDefinition, *Path);
				});
			return;
		}
		Component = NewObject<UNiagaraComponent>(this);
		if (!Component)
		{
			WarnEmitterOnce(FString::Printf(TEXT("component|%d"), Emitter.Entity.Index),
				[&]
				{
					return FString::Printf(
						TEXT("particle emitter %d ('%s') could not create a Niagara component on map '%s'"),
						Emitter.Entity.Index, *Emitter.ParticleDefinition, *MapName);
				});
			return;
		}
		Component->SetAsset(System);
		Component->SetAutoActivate(false);
		Component->SetupAttachment(GetRootComponent());
		Component->RegisterComponent();
		AddInstanceComponent(Component);
		RainComponents.Add(Emitter.Entity.Index, Component);
	}

	AttachEmitter(Emitter, Component);
	Component->SetVariableFloat(TEXT("User.RateScale"), Emitter.RateScale);
	Component->Activate();
	RainEmitterStates.Add(Emitter.Entity.Index, Emitter);
}

// `attach_type` 1 is `tree`: preserve the authored parent offset while following the named root.
// `attach_type` 2 is `point`: snap to a named bone (or the body root when the bone is absent).
// Parents resolve here rather than at PostSpawn because cinematic receivers may be made after map
// activation.
void AElysiumMapActor::AttachEmitter(
	const FElysiumWeatherEmitterState& Emitter, UNiagaraComponent* Component)
{
	if (!Component)
	{
		return;
	}
	auto WarnAttachmentOnce = [this, &Emitter](const TCHAR* Kind, const FString& Message)
	{
		WarnEmitterOnce(FString::Printf(TEXT("attach|%s|%d"), Kind, Emitter.Entity.Index),
			[&Message] { return Message; });
	};
	USceneComponent* ParentBody = nullptr;
	FElysiumEntity* ParentEntity = nullptr;
	const bool bWantsParent = Emitter.AttachType == 1 || Emitter.AttachType == 2;
	if (bWantsParent && !Emitter.ParentName.IsEmpty() && EntityWorld)
	{
		ParentEntity = EntityWorld->FindByName(Emitter.ParentName);
		if (ParentEntity)
		{
			ParentBody = ParentEntity->GetAttachBody();
		}
		if (!ParentBody)
		{
			WarnEmitterOnce(FString::Printf(TEXT("parent|%d|%s"),
				Emitter.Entity.Index, *Emitter.ParentName.ToLower()),
				[&]
				{
					return FString::Printf(
						TEXT("particle emitter %d ('%s') cannot attach to parent '%s' on map '%s'; using map root"),
						Emitter.Entity.Index, *Emitter.ParticleDefinition, *Emitter.ParentName, *MapName);
				});
		}
	}
	if (!ParentBody)
	{
		if (!Component->AttachToComponent(GetRootComponent(),
			FAttachmentTransformRules::KeepRelativeTransform))
		{
			WarnAttachmentOnce(TEXT("root"), FString::Printf(
				TEXT("particle emitter %d ('%s') could not attach to the map root"),
				Emitter.Entity.Index, *Emitter.ParticleDefinition));
		}
		Component->SetRelativeLocation(Emitter.LocationCm);
		return;
	}
	if (Emitter.AttachType == 1)
	{
		// Retail parents these at map setup, before the cinematic moves its actor. Components are
		// created lazily here, so reconstruct that same authored offset against the parent's live
		// position before establishing the persistent component attachment.
		FVector WorldLocation = Emitter.LocationCm;
		if (ParentEntity && ParentEntity->Def)
		{
			WorldLocation += ParentEntity->Origin - ParentEntity->Def->Origin;
		}
		if (!Component->AttachToComponent(ParentBody, FAttachmentTransformRules::KeepWorldTransform))
		{
			WarnAttachmentOnce(TEXT("tree"), FString::Printf(
				TEXT("particle emitter %d ('%s') could not tree-attach to parent '%s'"),
				Emitter.Entity.Index, *Emitter.ParticleDefinition, *Emitter.ParentName));
		}
		Component->SetWorldLocation(WorldLocation);
		return;
	}
	// VtMB bone names carry spaces (`Bip01 Neck`) and survive the glTF export unchanged, so the
	// authored name is used verbatim. A body that has not got the bone falls back to its root.
	const FName Bone(*Emitter.AttachBone);
	const bool bHasBone = !Emitter.AttachBone.IsEmpty()
		&& ParentBody->DoesSocketExist(Bone);
	if (!Component->AttachToComponent(ParentBody,
		FAttachmentTransformRules::SnapToTargetNotIncludingScale, bHasBone ? Bone : NAME_None))
	{
		WarnAttachmentOnce(TEXT("point"), FString::Printf(
			TEXT("particle emitter %d ('%s') could not point-attach to parent '%s'"),
			Emitter.Entity.Index, *Emitter.ParticleDefinition, *Emitter.ParentName));
	}
	Component->SetRelativeLocation(FVector::ZeroVector);
	if (!bHasBone && !Emitter.AttachBone.IsEmpty())
	{
		WarnEmitterOnce(FString::Printf(TEXT("bone|%d|%s|%s"), Emitter.Entity.Index,
			*Emitter.ParentName.ToLower(), *Emitter.AttachBone.ToLower()),
			[&]
			{
				return FString::Printf(
					TEXT("particle emitter %d ('%s') cannot find bone '%s' on parent '%s'; using body root"),
					Emitter.Entity.Index, *Emitter.ParticleDefinition,
					*Emitter.AttachBone, *Emitter.ParentName);
			});
	}
}

void AElysiumMapActor::RemoveEmitter(const FElysiumEntityHandle& Entity)
{
	if (const FElysiumWeatherEmitterState* Placed = EffectEmitterStates.Find(Entity.Index))
	{
		FElysiumWeatherEmitterState Dead = *Placed;
		Dead.bDead = true;
		ApplyEmitterToPlacedActor(Dead);
	}
	RainEmitterStates.Remove(Entity.Index);
	if (TObjectPtr<UNiagaraComponent> Component; RainComponents.RemoveAndCopyValue(Entity.Index, Component))
	{
		if (Component) { Component->DestroyComponent(); }
	}
	RefreshFollowRain();
}

void AElysiumMapActor::TickWeatherPresentation()
{
	if (GPendingWeatherTimer.IsSet())
	{
		const bool bRainOn = GPendingWeatherTimer.GetValue();
		GPendingWeatherTimer.Reset();
		FireWeatherTimer(bRainOn);
	}
	RefreshFollowRain();
	UpdateFollowRainLocation();
	ApplyWeatherTuning();
}

void AElysiumMapActor::UpdateFollowRainLocation()
{
	if (!RainFollowComponent)
	{
		return;
	}
	FVector Location;
	FRotator Rotation;
	if (GetPlayerViewPoint(Location, Rotation))
	{
		RainFollowComponent->SetWorldLocation(Location);
		RainFollowComponent->SetVariablePosition(TEXT("User.SpawnCenter"), Location);
	}
}

void AElysiumMapActor::RefreshFollowRain()
{
	float Rate = 0.0f;
	float Bounds = 0.0f;
	bool bAny = false;
	for (const TPair<int32, FElysiumWeatherEmitterState>& Pair : RainEmitterStates)
	{
		if (!IsFollowRainDefinition(Pair.Value.ParticleDefinition))
		{
			continue;
		}
		bAny = true;
		Rate = FMath::Max(Rate, Pair.Value.RateScale);
		Bounds = FMath::Max(Bounds, Pair.Value.BoundsCm);
	}
	if (CVarRainForce.GetValueOnGameThread() != 0)
	{
		bAny = true;
		Rate = FMath::Max(Rate, 1.0f);
	}
	const float ParticleRate = FMath::Max(0.0f, CVarRainRateScale.GetValueOnGameThread());
	Rate *= ParticleRate;
	if (!bAny || Rate <= KINDA_SMALL_NUMBER)
	{
		if (RainFollowComponent)
		{
			RainFollowComponent->Deactivate();
		}
		return;
	}
	if (!RainFollowComponent)
	{
		if (!RainSystem)
		{
			RainSystem = LoadObject<UNiagaraSystem>(nullptr,
				TEXT("/Game/ElysiumAuthored/VFX/NS_ElysiumRain.NS_ElysiumRain"));
		}
		if (!RainSystem)
		{
			WarnEmitterOnce(TEXT("follow|system"),
				[&]
				{
					return FString::Printf(
						TEXT("rain_follow_emitter on map '%s' cannot load /Game/ElysiumAuthored/VFX/NS_ElysiumRain"),
						*MapName);
				});
			return;
		}
		RainFollowComponent = NewObject<UNiagaraComponent>(this);
		if (!RainFollowComponent)
		{
			WarnEmitterOnce(TEXT("follow|component"),
				[&]
				{
					return FString::Printf(
						TEXT("rain_follow_emitter on map '%s' could not create a Niagara component"),
						*MapName);
				});
			return;
		}
		RainFollowComponent->SetAsset(RainSystem);
		RainFollowComponent->SetAutoActivate(false);
		RainFollowComponent->SetupAttachment(GetRootComponent());
		RainFollowComponent->RegisterComponent();
		AddInstanceComponent(RainFollowComponent);
		// The shared system is a tracked authored asset and is never rewritten per map; this
		// map's height-masked material instances come off its own baked Weather package and bind
		// through the system's material user parameters. A missing instance falls back to the
		// authored default material on the renderer -- rain still draws, without the map's height mask.
		const FString WeatherPkg = FElysiumContentPaths::BakedMapDir(MapName) / TEXT("Weather");
		const auto LoadRainInstance = [&](const TCHAR* Name) -> UMaterialInterface*
		{
			const FString Path = WeatherPkg / Name + TEXT(".") + Name;
			UMaterialInterface* Instance = LoadObject<UMaterialInterface>(nullptr, *Path);
			if (Instance == nullptr)
			{
				WarnEmitterOnce(FString::Printf(TEXT("follow|material|%s"), Name),
					[&]
					{
						return FString::Printf(
							TEXT("follow rain on map '%s': %s is missing -- re-bake the map's "
							     "weather package; using the authored default material"),
							*MapName, *Path);
					});
			}
			return Instance;
		};
		if (UMaterialInterface* Streaks = LoadRainInstance(TEXT("MI_ElysiumRain")))
		{
			RainFollowComponent->SetVariableMaterial(
				FName(TEXT("User.RainStreakMaterial")), Streaks);
		}
		if (UMaterialInterface* Mist = LoadRainInstance(TEXT("MI_ElysiumRainMist")))
		{
			RainFollowComponent->SetVariableMaterial(
				FName(TEXT("User.RainMistMaterial")), Mist);
		}
		UE_LOG(LogElysium, Log,
			TEXT("follow rain created on '%s' system=%s"),
			*MapName, *RainSystem->GetPathName());
	}
	if (Bounds <= KINDA_SMALL_NUMBER)
	{
		Bounds = 1200.0f;
	}
	RainFollowComponent->SetVariableFloat(TEXT("User.RateScale"), Rate);
	RainFollowComponent->SetVariableFloat(TEXT("User.BoundsCm"), Bounds);
	RainFollowComponent->SetVariableFloat(TEXT("User.LightResponse"), 1.0f);
	RainFollowComponent->SetVariableFloat(TEXT("User.StreakWidth"),
		FMath::Max(0.2f, CVarRainStreakWidth.GetValueOnGameThread()));
	RainFollowComponent->SetVariableFloat(TEXT("User.StreakLength"),
		FMath::Max(4.0f, CVarRainStreakLength.GetValueOnGameThread()));
	RainFollowComponent->SetVariableFloat(TEXT("User.StreakAlpha"),
		FMath::Clamp(CVarRainStreakAlpha.GetValueOnGameThread(), 0.0f, 1.0f));
	UpdateFollowRainLocation();
	if (!RainFollowComponent->IsActive())
	{
		RainFollowComponent->Activate();
	}
}

void AElysiumMapActor::ApplyWeatherTuning()
{
	const float Enhancement = FMath::Clamp(CVarRainEnhancement.GetValueOnGameThread(), 0.0f, 1.0f);
	bEnvironmentWetnessOverride = CVarEnvironmentWetnessOverride.GetValueOnGameThread() != 0;
	PresentedWetness = bEnvironmentWetnessOverride
		? FMath::Clamp(CVarEnvironmentWetness.GetValueOnGameThread(), 0.0f, 1.0f)
		: FMath::Clamp(WetnessTransition.CurrentWetness, 0.0f, 1.0f);
	PresentedWetnessScale = FMath::Clamp(
		CVarEnvironmentWetnessScale.GetValueOnGameThread(), 0.0f, 4.0f);
	if (!EnvironmentParameters)
	{
		EnvironmentParameters = LoadObject<UMaterialParameterCollection>(
			nullptr, *FElysiumContentPaths::Material(TEXT("MPC_ElysiumEnvironment")));
	}
	if (EnvironmentParameters && GetWorld())
	{
		UKismetMaterialLibrary::SetScalarParameterValue(GetWorld(), EnvironmentParameters,
			TEXT("GlobalWetness"), PresentedWetness);
		UKismetMaterialLibrary::SetScalarParameterValue(GetWorld(), EnvironmentParameters,
			TEXT("WetnessOutputScale"), PresentedWetnessScale);
		UKismetMaterialLibrary::SetScalarParameterValue(GetWorld(), EnvironmentParameters,
			TEXT("RainEnhancement"), Enhancement);
		UKismetMaterialLibrary::SetScalarParameterValue(GetWorld(), EnvironmentParameters,
			TEXT("RainWetDarken"), FMath::Max(0.0f, CVarRainWetDarken.GetValueOnGameThread()));
		UKismetMaterialLibrary::SetScalarParameterValue(GetWorld(), EnvironmentParameters,
			TEXT("RainWetRoughness"), FMath::Max(0.0f, CVarRainWetRoughness.GetValueOnGameThread()));
		UKismetMaterialLibrary::SetScalarParameterValue(GetWorld(), EnvironmentParameters,
			TEXT("RainLightResponse"), FMath::Clamp(
				CVarRainLightResponse.GetValueOnGameThread(), 0.0f, 1.0f));
		UKismetMaterialLibrary::SetScalarParameterValue(GetWorld(), EnvironmentParameters,
			TEXT("RainSourceRetain"), FMath::Clamp(
				CVarRainSourceRetain.GetValueOnGameThread(), 0.0f, 1.0f));
		UKismetMaterialLibrary::SetScalarParameterValue(GetWorld(), EnvironmentParameters,
			TEXT("RainWetSpecular"), FMath::Clamp(
				CVarRainWetSpecular.GetValueOnGameThread(), 0.0f, 1.0f));
		UKismetMaterialLibrary::SetScalarParameterValue(GetWorld(), EnvironmentParameters,
			TEXT("RainReflectionDebug"), static_cast<float>(FMath::Clamp(
				CVarRainReflectionDebug.GetValueOnGameThread(), 0, 6)));
	}
	const float ParticleRate = FMath::Max(0.0f, CVarRainRateScale.GetValueOnGameThread());
	const float LightResponse = FMath::Clamp(
		CVarRainLightResponse.GetValueOnGameThread() * 4.0f, 0.0f, 2.0f);
	for (const TPair<int32, FElysiumWeatherEmitterState>& Pair : RainEmitterStates)
	{
		if (UNiagaraComponent* Component = RainComponents.FindRef(Pair.Key))
		{
			Component->SetVariableFloat(TEXT("User.RateScale"), Pair.Value.RateScale * ParticleRate);
			Component->SetVariableFloat(TEXT("User.LightResponse"), LightResponse);
		}
	}
}

bool AElysiumMapActor::IsFollowRainActive() const
{
	return RainFollowComponent && RainFollowComponent->IsActive();
}

FVector AElysiumMapActor::GetFollowRainLocation() const
{
	return RainFollowComponent ? RainFollowComponent->GetComponentLocation() : FVector::ZeroVector;
}

void AElysiumMapActor::FireWeatherTimer(bool bRainOn)
{
	if (!EntityWorld)
	{
		return;
	}
	EntityWorld->EnqueueInput(bRainOn ? TEXT("rain_on_timer") : TEXT("rain_off_timer"),
		FName(TEXT("FireTimer")), FElysiumVariant::Void(), 0.0,
		FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
}

FString AElysiumMapActor::GetWeatherDebugSummary() const
{
	FString Result = FString::Printf(
		TEXT("wet authored %.3f->%.3f presented %.3f x%.2f override=%d components %d follow=%d force=%d"),
		WetnessTransition.CurrentWetness, WetnessTransition.TargetWetness,
		PresentedWetness, PresentedWetnessScale, bEnvironmentWetnessOverride ? 1 : 0,
		RainComponents.Num(),
		RainFollowComponent && RainFollowComponent->IsActive() ? 1 : 0,
		CVarRainForce.GetValueOnGameThread());
	if (RainFollowComponent)
	{
		Result += FString::Printf(TEXT(" follow_loc=%s"),
			*RainFollowComponent->GetComponentLocation().ToCompactString());
	}
	if (RainSystem)
	{
		for (const FNiagaraEmitterHandle& Handle : RainSystem->GetEmitterHandles())
		{
			const FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
			if (!Data)
			{
				continue;
			}
			for (const UNiagaraRendererProperties* Renderer : Data->GetRenderers())
			{
				const UNiagaraSpriteRendererProperties* Sprite =
					Cast<UNiagaraSpriteRendererProperties>(Renderer);
				Result += Sprite ? FString::Printf(
					TEXT(" | renderer=%s enabled=%d source=%d material=%s position=%s "
						"color=%s size=%s visibility=%s:%u camera_cull=%d:%.1f..%.1f"),
					*Handle.GetName().ToString(), Sprite->GetIsEnabled() ? 1 : 0,
					static_cast<int32>(Sprite->SourceMode), *GetNameSafe(Sprite->Material),
					*Sprite->PositionBinding.GetParamMapBindableVariable().GetName().ToString(),
					*Sprite->ColorBinding.GetParamMapBindableVariable().GetName().ToString(),
					*Sprite->SpriteSizeBinding.GetParamMapBindableVariable().GetName().ToString(),
					*Sprite->RendererVisibilityTagBinding.GetParamMapBindableVariable().GetName().ToString(),
					Sprite->RendererVisibility, Sprite->bEnableCameraDistanceCulling ? 1 : 0,
					Sprite->MinCameraDistance, Sprite->MaxCameraDistance)
					: FString::Printf(TEXT(" | renderer=%s non-sprite"), *Handle.GetName().ToString());
			}
		}
	}
	for (const TPair<int32, FElysiumWeatherEmitterState>& Pair : RainEmitterStates)
	{
		const UNiagaraComponent* Component = RainComponents.FindRef(Pair.Key);
		FString Materials;
		if (Component)
		{
			TArray<UMaterialInterface*> UsedMaterials;
			Component->GetUsedMaterials(UsedMaterials, false);
			for (const UMaterialInterface* Material : UsedMaterials)
			{
				Materials += Materials.IsEmpty() ? TEXT("") : TEXT(",");
				Materials += GetNameSafe(Material);
			}
			if (const FNiagaraSystemInstanceControllerConstPtr Controller =
					Component->GetSystemInstanceController())
			{
				if (const FNiagaraSystemInstance* Instance = Controller->GetSystemInstance_Unsafe())
				{
					for (const FNiagaraEmitterInstanceRef& EmitterRef : Instance->GetEmitters())
					{
						const FNiagaraEmitterInstance& Emitter = EmitterRef.Get();
						const FNiagaraDataSet& Data = Emitter.GetParticleData();
						const FNiagaraDataSetAccessor<FNiagaraPosition> Accessor(
							Data, FName(TEXT("Position")));
						const FNiagaraDataSetReaderFloat<FNiagaraPosition> Reader =
							Accessor.GetReader(Data);
						const FNiagaraDataSetAccessor<int32> VisibilityAccessor(
							Data, FName(TEXT("VisibilityTag")));
						const FNiagaraDataSetReaderInt32<int32> VisibilityReader =
							VisibilityAccessor.GetReader(Data);
						FNiagaraPosition Min(ForceInit), Max(ForceInit);
						if (Reader.IsValid() && Emitter.GetNumParticles() > 0)
						{
							Reader.GetMinMax(Min, Max);
						}
						Materials += FString::Printf(TEXT(";%s:n=%d,p=%s..%s,visibility=%d"),
							*Emitter.GetEmitterHandle().GetName().ToString(), Emitter.GetNumParticles(),
							*FVector3f(Min).ToString(), *FVector3f(Max).ToString(),
							VisibilityReader.IsValid() && Emitter.GetNumParticles() > 0
								? VisibilityReader.Get(0) : INDEX_NONE);
					}
				}
			}
		}
		Result += FString::Printf(
			TEXT(" | #%d active=%d component=%d visible=%d render=%d rate=%.3f "
				"loc=%s world_bounds=%s materials=%s"),
			Pair.Key, Pair.Value.bActive ? 1 : 0,
			Component && Component->IsActive() ? 1 : 0,
			Component && Component->IsVisible() ? 1 : 0,
			Component && Component->IsRenderStateCreated() ? 1 : 0,
			Pair.Value.RateScale,
			Component ? *Component->GetComponentLocation().ToCompactString() : TEXT("<none>"),
			Component ? *Component->Bounds.GetBox().ToString() : TEXT("<none>"),
			Materials.IsEmpty() ? TEXT("<none>") : *Materials);
	}
	return Result;
}
