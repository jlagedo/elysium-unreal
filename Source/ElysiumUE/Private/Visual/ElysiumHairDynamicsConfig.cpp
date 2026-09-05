#include "Visual/ElysiumHairDynamicsConfig.h"
#include "ElysiumCastData.h"

#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumHairDynamicsConfig, Log, All);

namespace
{
	// The tracked authored table. Not derived from the user's install and not on the baked mount --
	// it is repository content, so a miss is a missing tracked asset rather than a missing export.
	const TCHAR* GHairDynamicsConfigPath =
		TEXT("/Game/ElysiumAuthored/Hair/DA_HairDynamics.DA_HairDynamics");
}

const UElysiumHairDynamicsConfig* UElysiumHairDynamicsConfig::Load()
{
	// Held outside any UObject, so a plain TStrongObjectPtr roots it (`Source/ElysiumUE/CLAUDE.md`
	// C++ coding policy) -- this is a static accessor with no owning object to hang a UPROPERTY off,
	// and the table outlives any one map epoch. The attempt is made ONCE per process, success or
	// failure: this is reached per body stood, and a missing package would otherwise re-run a
	// failing LoadObject and re-emit the same warning for every character on every map load ("log
	// once where the failure is owned"). An asset added mid-session is picked up on the next launch.
	static TStrongObjectPtr<UElysiumHairDynamicsConfig> Config;
	static bool bTried = false;
	if (!bTried)
	{
		bTried = true;
		Config = TStrongObjectPtr<UElysiumHairDynamicsConfig>(
			LoadObject<UElysiumHairDynamicsConfig>(nullptr, GHairDynamicsConfigPath));
		if (!Config.IsValid())
		{
			UE_LOG(LogElysiumHairDynamicsConfig, Warning,
				TEXT("hair dynamics: '%s' did not load -- the tracked authored asset is missing from "
				     "Content/ElysiumAuthored. No character simulates hair this session."),
				GHairDynamicsConfigPath);
		}
	}
	return Config.Get();
}

const FElysiumHairDynamicsStem* UElysiumHairDynamicsConfig::FindModel(const FString& Model)
{
	const UElysiumHairDynamicsConfig* const Config = Load();
	if (Config == nullptr || Model.IsEmpty())
	{
		return nullptr;
	}
	FString Error;
	const FString Id=UElysiumCastData::ModelIdForPreparation(Model,Error);
	if (Id.IsEmpty())
	{
		UE_LOG(LogElysiumHairDynamicsConfig,Warning,TEXT("hair tuning model '%s': %s"),*Model,*Error);
		return nullptr;
	}
	return Config->Stems.Find(FName(*Id));
}
