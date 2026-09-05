#include "ElysiumUE.h"
#include "ElysiumInputAssets.h"
#include "Editor/ElysiumAnimationDataModel.h"

#include "InputCoreTypes.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "ElysiumInputKeys"

namespace
{
	void RegisterGamepadKey(const TCHAR* Name, const FText& DisplayName)
	{
		const FKey Key(Name);
		if (!EKeys::GetKeyDetails(Key).IsValid())
		{
			EKeys::AddKey(FKeyDetails(Key, DisplayName, FKeyDetails::GamepadKey));
		}
	}
}

class FElysiumUEModule final : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		FDefaultGameModuleImpl::StartupModule();
		ElysiumAnimationDataModel::Register();
		RegisterGamepadKey(ElysiumInputAssets::DualSenseCreateKey,
			LOCTEXT("DualSenseCreate", "DualSense Create"));
		RegisterGamepadKey(ElysiumInputAssets::DualSensePSKey,
			LOCTEXT("DualSensePS", "DualSense PS"));
		RegisterGamepadKey(ElysiumInputAssets::DualSenseMuteKey,
			LOCTEXT("DualSenseMute", "DualSense Mute"));
	}
	virtual void ShutdownModule() override
	{
		ElysiumAnimationDataModel::Unregister();
		FDefaultGameModuleImpl::ShutdownModule();
	}
};

IMPLEMENT_PRIMARY_GAME_MODULE(FElysiumUEModule, ElysiumUE, "ElysiumUE");

#undef LOCTEXT_NAMESPACE
