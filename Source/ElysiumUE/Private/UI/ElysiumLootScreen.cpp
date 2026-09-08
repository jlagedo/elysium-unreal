#include "UI/ElysiumLootScreen.h"

#include "UI/ElysiumActionButton.h"
#include "UI/ElysiumUIStyle.h"

#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumLootUI, Log, All);

namespace
{
	const FName ContainerGroup(TEXT("Loot.Container"));
	const FName PlayerGroup(TEXT("Loot.Player"));
	const FName FooterGroup(TEXT("Loot.Footer"));
	const FName CloseAction(TEXT("Loot.Close"));
	constexpr float PanelWidth = 1080.0f;
	constexpr float ListWidth = 470.0f;
}

UElysiumLootScreen::UElysiumLootScreen()
{
	bIsBackHandler = true;
}

void UElysiumLootScreen::ApplyLoot(const FElysiumLootView& InLoot)
{
	const bool bChanged = Loot.Owner != InLoot.Owner || Loot.Revision != InLoot.Revision;
	Loot = InLoot;
	if (bChanged && PanelHost.IsValid())
	{
		PanelHost->SetContent(BuildPanel());
		FinalizeNavigationBuild();
	}
}

float UElysiumLootScreen::VirtualScale() const
{
	return FMath::Clamp(ElysiumUI::PaintHeight(*this) / 1080.0f, 0.75f, 2.0f);
}

TSharedRef<SWidget> UElysiumLootScreen::BuildActionVisual(UElysiumActionButton& Action,
	const FElysiumLootEntryView& Entry, bool bTake)
{
	const TWeakObjectPtr<UElysiumActionButton> WeakAction(&Action);
	const FString Quantity = Entry.Quantity > 1
		? FString::Printf(TEXT("  x%d"), Entry.Quantity) : FString();
	return SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
		.BorderBackgroundColor_Lambda([WeakAction]()
		{
			const UElysiumActionButton* Button = WeakAction.Get();
			return FSlateColor(Button && Button->IsActionSelected()
				? FLinearColor(0.48f, 0.055f, 0.07f, 0.98f)
				: FLinearColor(0.09f, 0.075f, 0.075f, 0.94f));
		})
		.Padding(FMargin(16.0f, 11.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Entry.Label + Quantity))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 16))
				.ColorAndOpacity(FSlateColor(FLinearColor::White))
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(STextBlock)
				.Text(bTake
					? NSLOCTEXT("Elysium", "LootTake", "Take")
					: NSLOCTEXT("Elysium", "LootStore", "Store"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.82f, 0.64f, 0.38f, 1.0f)))
			]
		];
}

TSharedRef<SWidget> UElysiumLootScreen::BuildList(
	const TArray<FElysiumLootEntryView>& Entries, bool bTake, FName Group)
{
	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	if (Entries.IsEmpty())
	{
		Rows->AddSlot().AutoHeight().Padding(8.0f)
		[
			SNew(STextBlock)
			.Text(NSLOCTEXT("Elysium", "LootEmpty", "Empty"))
			.Font(FCoreStyle::GetDefaultFontStyle("Italic", 15))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.52f, 0.50f, 1.0f)))
		];
	}
	for (const FElysiumLootEntryView& Entry : Entries)
	{
		const FName ActionId(*FString::Printf(TEXT("Loot.%s.%d"),
			bTake ? TEXT("Take") : TEXT("Give"), Entry.Slot));
		UElysiumActionButton* Action = CreateActionButton(ActionId, Group,
			FText::FromString(Entry.Label), true,
			[this, bTake, Slot = Entry.Slot]() { OnTransfer.ExecuteIfBound(bTake, Slot); });
		if (!Action)
		{
			UE_LOG(LogElysiumLootUI, Warning,
				TEXT("loot screen failed to create %s action for slot %d (%s)"),
				bTake ? TEXT("take") : TEXT("store"), Entry.Slot, *Entry.Classname);
			continue;
		}
		Action->SetSlateContent(BuildActionVisual(*Action, Entry, bTake));
		Rows->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			Action->TakeWidget()
		];
	}
	return SNew(SScrollBox)
		+ SScrollBox::Slot()
		[
			Rows
		];
}

TSharedRef<SWidget> UElysiumLootScreen::BuildPanel()
{
	BeginNavigationBuild();
	SetNavigationGroup(ContainerGroup, false, true, false, true);
	SetNavigationGroup(PlayerGroup, false, true, false, true);
	SetNavigationGroup(FooterGroup, true, false, false, false);

	UElysiumActionButton* Close = CreateActionButton(CloseAction, FooterGroup,
		NSLOCTEXT("Elysium", "LootClose", "Close"), true,
		[this]() { OnClose.ExecuteIfBound(); });
	check(Close);
	Close->SetSlateContent(
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
		.BorderBackgroundColor(FSlateColor(FLinearColor(0.26f, 0.12f, 0.11f, 1.0f)))
		.Padding(FMargin(28.0f, 10.0f))
		[
			SNew(STextBlock)
			.Text(NSLOCTEXT("Elysium", "LootClose", "Close"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 15))
			.ColorAndOpacity(FSlateColor(FLinearColor::White))
		]);

	TSharedRef<SWidget> ContainerList = BuildList(Loot.ContainerItems, true, ContainerGroup);
	TSharedRef<SWidget> PlayerList = BuildList(Loot.PlayerItems, false, PlayerGroup);
	const TArray<FName> ContainerActions = GetActionsInGroup(ContainerGroup);
	const TArray<FName> PlayerActions = GetActionsInGroup(PlayerGroup);
	for (int32 Index = 0; Index < ContainerActions.Num(); ++Index)
	{
		if (!PlayerActions.IsEmpty())
		{
			SetActionNeighbor(ContainerActions[Index], EElysiumNavigationDirection::Right,
				PlayerActions[FMath::Min(Index, PlayerActions.Num() - 1)]);
		}
	}
	for (int32 Index = 0; Index < PlayerActions.Num(); ++Index)
	{
		if (!ContainerActions.IsEmpty())
		{
			SetActionNeighbor(PlayerActions[Index], EElysiumNavigationDirection::Left,
				ContainerActions[FMath::Min(Index, ContainerActions.Num() - 1)]);
		}
	}
	if (!ContainerActions.IsEmpty())
	{
		SetActionNeighbor(ContainerActions.Last(), EElysiumNavigationDirection::Down, CloseAction);
	}
	if (!PlayerActions.IsEmpty())
	{
		SetActionNeighbor(PlayerActions.Last(), EElysiumNavigationDirection::Down, CloseAction);
	}
	const FName CloseUp = !ContainerActions.IsEmpty() ? ContainerActions.Last()
		: (!PlayerActions.IsEmpty() ? PlayerActions.Last() : NAME_None);
	if (!CloseUp.IsNone())
	{
		SetActionNeighbor(CloseAction, EElysiumNavigationDirection::Up, CloseUp);
	}

	return SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
		.BorderBackgroundColor(FSlateColor(FLinearColor(0.025f, 0.020f, 0.022f, 0.98f)))
		.Padding(FMargin(30.0f))
		[
			SNew(SBox).WidthOverride(PanelWidth).HeightOverride(700.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 22.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Loot.Title.IsEmpty() ? TEXT("Container") : Loot.Title))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 24))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.86f, 0.70f, 0.43f, 1.0f)))
				]
				+ SVerticalBox::Slot().FillHeight(1.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 12.0f, 0.0f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)
						[
							SNew(STextBlock)
							.Text(NSLOCTEXT("Elysium", "LootContents", "Contents"))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 17))
						]
						+ SVerticalBox::Slot().FillHeight(1.0f)
						[
							SNew(SBox).WidthOverride(ListWidth)
							[ ContainerList ]
						]
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(12.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)
						[
							SNew(STextBlock)
							.Text(NSLOCTEXT("Elysium", "LootInventory", "Inventory"))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 17))
						]
						+ SVerticalBox::Slot().FillHeight(1.0f)
						[
							SNew(SBox).WidthOverride(ListWidth)
							[ PlayerList ]
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				.Padding(0.0f, 20.0f, 0.0f, 0.0f)
				[ Close->TakeWidget() ]
			]
		];
}

TSharedRef<SWidget> UElysiumLootScreen::RebuildWidget()
{
	(void)Super::RebuildWidget();
	TSharedRef<SWidget> Panel = BuildPanel();
	TSharedRef<SWidget> Result = SNew(SDPIScaler)
		.DPIScale_Lambda([this]() { return VirtualScale(); })
		[
			SAssignNew(PanelHost, SBox)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Padding(FMargin(48.0f))
			[ Panel ]
		];
	FinalizeNavigationBuild();
	return Result;
}

bool UElysiumLootScreen::NativeOnHandleBackAction()
{
	OnClose.ExecuteIfBound();
	return true;
}

void UElysiumLootScreen::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	PanelHost.Reset();
}
