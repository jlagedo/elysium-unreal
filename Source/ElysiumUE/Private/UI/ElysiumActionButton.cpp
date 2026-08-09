#include "UI/ElysiumActionButton.h"

#include "Blueprint/WidgetTree.h"
#include "InputCoreTypes.h"
#include "Widgets/Layout/SBox.h"

void UElysiumSlateHost::SetContent(TSharedRef<SWidget> InContent)
{
	Content = InContent;
	if (ContentBox.IsValid())
	{
		ContentBox->SetContent(InContent);
	}
}

TSharedRef<SWidget> UElysiumSlateHost::RebuildWidget()
{
	return SAssignNew(ContentBox, SBox)
	[
		Content.IsValid() ? Content.ToSharedRef() : SNullWidget::NullWidget
	];
}

void UElysiumSlateHost::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	ContentBox.Reset();
	Content.Reset();
}

UElysiumActionButtonStyle::UElysiumActionButtonStyle()
{
	auto MakeTransparent = []()
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::NoDrawType;
		return Brush;
	};

	NormalBase = MakeTransparent();
	NormalHovered = MakeTransparent();
	NormalPressed = MakeTransparent();
	SelectedBase = MakeTransparent();
	SelectedHovered = MakeTransparent();
	SelectedPressed = MakeTransparent();
	Disabled = MakeTransparent();
	ButtonPadding = FMargin(0.0f);
	CustomPadding = FMargin(0.0f);
}

UElysiumActionButton::UElysiumActionButton(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetStyle(UElysiumActionButtonStyle::StaticClass());
	SetIsSelectable(true);
	SetShouldSelectUponReceivingFocus(true);
	SetIsFocusable(true);
	SetClickMethod(EButtonClickMethod::MouseDown);
}

bool UElysiumActionButton::Initialize()
{
	// UCommonButtonBase::Initialize expects a presentation root to exist before it wraps that root
	// in its internal UButton. Native user widgets normally do not receive their WidgetTree until
	// UUserWidget::Initialize (inside Super), so create the tree here first. Without this ordering
	// the CommonButton remains focusable/clickable but has no visual child: menu labels, dialogue
	// responses and every other retained-Slate action are invisible at runtime.
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"), RF_Transient);
	}

	if (!SlateHost)
	{
		SlateHost = Cast<UElysiumSlateHost>(WidgetTree->FindWidget(TEXT("SlateContent")));
	}
	if (!SlateHost && !WidgetTree->RootWidget)
	{
		SlateHost = WidgetTree->ConstructWidget<UElysiumSlateHost>(
			UElysiumSlateHost::StaticClass(), TEXT("SlateContent"));
		WidgetTree->RootWidget = SlateHost;
	}

	return Super::Initialize();
}

void UElysiumActionButton::Configure(FName InActionId, const FText& InLabel,
	const FText& InCaption, bool bInExecutable)
{
	ActionId = InActionId;
	Label = InLabel;
	Caption = InCaption;
	bExecutable = bInExecutable;
	SetToolTipText(Caption);
}

void UElysiumActionButton::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	if (TSharedPtr<SWidget> Widget = GetCachedWidget())
	{
		const FText AccessibleLabel = Caption.IsEmpty()
			? Label
			: FText::Format(NSLOCTEXT("Elysium", "ActionAccessibility", "{0}. {1}"),
				Label, Caption);
		Widget->SetAccessibleBehavior(EAccessibleBehavior::Custom, AccessibleLabel);
	}
}

void UElysiumActionButton::SetSlateContent(TSharedRef<SWidget> InContent)
{
	if (SlateHost)
	{
		SlateHost->SetContent(InContent);
	}
}

FReply UElysiumActionButton::NativeOnFocusReceived(const FGeometry& InGeometry,
	const FFocusEvent& InFocusEvent)
{
	FReply Reply = Super::NativeOnFocusReceived(InGeometry, InFocusEvent);
	HighlightedEvent.Broadcast(*this);
	return Reply;
}

void UElysiumActionButton::NativeOnHovered()
{
	Super::NativeOnHovered();
	HighlightedEvent.Broadcast(*this);
}

void UElysiumActionButton::NativeOnClicked()
{
	Super::NativeOnClicked();
	if (bExecutable)
	{
		ActionActivatedEvent.Broadcast(*this);
	}
}

FReply UElysiumActionButton::NativeOnMouseButtonDown(const FGeometry& InGeometry,
	const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		HighlightedEvent.Broadcast(*this);
		if (bExecutable)
		{
			SecondaryActivatedEvent.Broadcast(*this);
		}
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}
