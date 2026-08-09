#include "ElysiumHUD.h"

#include "Debug/ElysiumLightProbe.h"
#include "ElysiumMapActor.h"
#include "ElysiumMapSubsystem.h"
#include "Visual/ElysiumMapVisuals.h"

#include "Engine/GameInstance.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumHUD, Log, All);

AElysiumHUD::AElysiumHUD()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AElysiumHUD::BeginPlay()
{
	Super::BeginPlay();

	LightsCmd = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("elysium.lights"),
		TEXT("elysium.lights — toggle the real-time light rig"),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this]()
		{
			if (AElysiumMapActor* Map = ResolveMapActor())
			{
				if (UElysiumMapVisuals* Visuals = Map->GetVisuals())
				{
					Visuals->ToggleLights();
				}
			}
		}),
		ECVF_Cheat);

	PropsCmd = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("elysium.props"),
		TEXT("elysium.props — toggle the static props"),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this]()
		{
			if (AElysiumMapActor* Map = ResolveMapActor())
			{
				if (UElysiumMapVisuals* Visuals = Map->GetVisuals())
				{
					Visuals->ToggleProps();
				}
			}
		}),
		ECVF_Cheat);

#if !UE_BUILD_SHIPPING
	LightProbeCmd = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("elysium.lightprobe"),
		TEXT("elysium.lightprobe [rays] — probe every light against the scene, write <map>.probe.json"),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			AElysiumMapActor* Map = ResolveMapActor();
			const int32 Rays = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 64;
			const int32 Count = ElysiumLightProbe::Run(GetWorld(), Map, Rays);
			if (Count < 0)
			{
				UE_LOG(LogElysiumHUD, Warning, TEXT("elysium.lightprobe: no light rig on this map"));
			}
		}),
		ECVF_Cheat);
#endif
}

void AElysiumHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (LightsCmd)
	{
		IConsoleManager::Get().UnregisterConsoleObject(LightsCmd);
		LightsCmd = nullptr;
	}
#if !UE_BUILD_SHIPPING
	if (LightProbeCmd)
	{
		IConsoleManager::Get().UnregisterConsoleObject(LightProbeCmd);
		LightProbeCmd = nullptr;
	}
#endif
	if (PropsCmd)
	{
		IConsoleManager::Get().UnregisterConsoleObject(PropsCmd);
		PropsCmd = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

AElysiumMapActor* AElysiumHUD::ResolveMapActor() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	const UElysiumMapSubsystem* Maps = GameInstance
		? GameInstance->GetSubsystem<UElysiumMapSubsystem>() : nullptr;
	return Maps ? Maps->GetCurrentMap() : nullptr;
}
