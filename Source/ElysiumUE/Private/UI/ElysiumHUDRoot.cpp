#include "UI/ElysiumHUDRoot.h"

#include "ElysiumHUDModel.h"
#include "UI/ElysiumHUDWidget.h"

#include "Components/Widget.h"
#include "GameFramework/PlayerController.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SNullWidget.h"

TSharedRef<SWidget> UElysiumHUDRoot::RebuildWidget()
{
	APlayerController* PC = GetOwningPlayer();
	HUDWidget = PC ? CreateWidget<UElysiumHUDWidget>(PC, UElysiumHUDWidget::StaticClass()) : nullptr;
	if (HUDWidget)
	{
		HUDWidget->SetModel(Model);
	}

	return SNew(SOverlay)
		+ SOverlay::Slot()
		[
			HUDWidget ? HUDWidget->TakeWidget() : SNullWidget::NullWidget
		]
		+ SOverlay::Slot()
		[
			SAssignNew(TransientHost, SBox)
		]
		+ SOverlay::Slot()
		[
			SAssignNew(GameModalHost, SBox)
		]
		+ SOverlay::Slot()
		[
			SAssignNew(SystemModalHost, SBox)
		];
}

void UElysiumHUDRoot::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	TransientHost.Reset();
	GameModalHost.Reset();
	SystemModalHost.Reset();
	HUDWidget = nullptr;
}
