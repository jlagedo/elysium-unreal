#include "UI/ElysiumTerminalScreen.h"

#include "UI/ElysiumActionButton.h"

#include "Blueprint/WidgetTree.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumTerminalUI, Log, All);

namespace
{
	const FName TerminalActions(TEXT("Terminal.Actions"));
	const FName ScreenMaterialSlot(TEXT("screen"));
	const FName SlateUIParameter(TEXT("SlateUI"));
	const FName TintParameter(TEXT("TintColorAndOpacity"));
	const FName OpacityParameter(TEXT("OpacityFromTexture"));
	constexpr float SurfaceWidth = 1024.0f;
	constexpr float SurfaceHeight = 768.0f;
	const FVector2D SurfaceDrawSize(SurfaceWidth, SurfaceHeight);
	const FLinearColor ScreenBlack(0.004f, 0.009f, 0.007f, 1.0f);
	const FLinearColor Phosphor(0.63f, 0.88f, 0.70f, 1.0f);
	const FLinearColor MutedPhosphor(0.32f, 0.52f, 0.39f, 1.0f);
	const TCHAR* ProjectionMaterialPath =
		TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Opaque.Widget3DPassThrough_Opaque");
}

UElysiumTerminalScreen::UElysiumTerminalScreen()
{
	bIsBackHandler = true;
}

int32 UElysiumTerminalScreen::FindScreenMaterialSlot(const TArray<FName>& SlotNames)
{
	return SlotNames.IndexOfByKey(ScreenMaterialSlot);
}

void UElysiumTerminalScreen::ApplyTerminal(const FElysiumTerminalView& InTerminal)
{
	const bool bNewSession = Terminal.Owner != InTerminal.Owner
		|| Terminal.SessionSerial != InTerminal.SessionSerial;
	if (!bNewSession && InTerminal.Revision < Terminal.Revision)
	{
		return;
	}
	const bool bInputModeChanged = Terminal.InputMode != InTerminal.InputMode;
	const bool bChanged = bNewSession || Terminal.Revision != InTerminal.Revision;
	Terminal = InTerminal;
	if (bNewSession || bInputModeChanged)
	{
		SetDraftText(FString());
	}
	if (bChanged && GetCachedWidget().IsValid())
	{
		RebuildTerminalSurface();
	}
}

bool UElysiumTerminalScreen::SetProjectionTarget(UPrimitiveComponent* InTarget)
{
	if (ProjectionTarget.Get() == InTarget && HasProjection())
	{
		return true;
	}

	// Replacing a prior target intentionally leaves its last pixels/material in place. Terminal
	// screens persist as world screensavers after CommonUI releases input ownership.
	ReleaseProjectionOwnership();
	if (!InTarget)
	{
		UE_LOG(LogElysiumTerminalUI, Warning,
			TEXT("terminal projection failed for %s serial %u: no physical use visual"),
			*Terminal.Owner.ToString(), Terminal.SessionSerial);
		return false;
	}

	const TArray<FName> SlotNames = InTarget->GetMaterialSlotNames();
	const int32 ListedIndex = FindScreenMaterialSlot(SlotNames);
	const int32 MaterialIndex = InTarget->GetMaterialIndex(ScreenMaterialSlot);
	if (ListedIndex == INDEX_NONE || MaterialIndex == INDEX_NONE)
	{
		const FString Available = FString::JoinBy(SlotNames, TEXT(", "),
			[](const FName& Slot) { return Slot.ToString(); });
		UE_LOG(LogElysiumTerminalUI, Warning,
			TEXT("terminal projection failed for %s serial %u: component '%s' has no exact "
				"'screen' material slot (available: %s)"),
			*Terminal.Owner.ToString(), Terminal.SessionSerial, *InTarget->GetName(),
			Available.IsEmpty() ? TEXT("none") : *Available);
		return false;
	}

	UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(nullptr,
		ProjectionMaterialPath);
	if (!BaseMaterial)
	{
		UE_LOG(LogElysiumTerminalUI, Warning,
			TEXT("terminal projection failed for %s serial %u: material '%s' did not load"),
			*Terminal.Owner.ToString(), Terminal.SessionSerial, ProjectionMaterialPath);
		return false;
	}
	if (!FApp::CanEverRender())
	{
		UE_LOG(LogElysiumTerminalUI, Warning,
			TEXT("terminal projection failed for %s serial %u: this process has no renderer"),
			*Terminal.Owner.ToString(), Terminal.SessionSerial);
		return false;
	}

	WidgetRenderer = MakeUnique<FWidgetRenderer>(false, true);
	RenderTarget = FWidgetRenderer::CreateTargetFor(SurfaceDrawSize, TF_Bilinear, false);
	if (!RenderTarget)
	{
		UE_LOG(LogElysiumTerminalUI, Warning,
			TEXT("terminal projection failed for %s serial %u: could not allocate %dx%d target"),
			*Terminal.Owner.ToString(), Terminal.SessionSerial,
			static_cast<int32>(SurfaceWidth), static_cast<int32>(SurfaceHeight));
		ReleaseProjectionOwnership();
		return false;
	}
	RenderTarget->ClearColor = ScreenBlack;

	// The physical component, not this transient input screen, owns the persistent MID. That lets
	// the screensaver remain on the monitor after CommonUI tears down the session widget.
	ProjectionMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, InTarget);
	if (!ProjectionMaterial)
	{
		UE_LOG(LogElysiumTerminalUI, Warning,
			TEXT("terminal projection failed for %s serial %u: could not create screen material"),
			*Terminal.Owner.ToString(), Terminal.SessionSerial);
		ReleaseProjectionOwnership();
		return false;
	}
	ProjectionMaterial->SetTextureParameterValue(SlateUIParameter, RenderTarget);
	ProjectionMaterial->SetVectorParameterValue(TintParameter, FLinearColor::White);
	ProjectionMaterial->SetScalarParameterValue(OpacityParameter, 1.0f);

	ProjectionTarget = InTarget;
	ProjectionMaterialIndex = MaterialIndex;
	InTarget->SetMaterial(MaterialIndex, ProjectionMaterial);
	RenderProjection();
	return true;
}

bool UElysiumTerminalScreen::HasProjection() const
{
	return ProjectionTarget.IsValid() && ProjectionMaterialIndex != INDEX_NONE
		&& RenderTarget && ProjectionMaterial && WidgetRenderer != nullptr;
}

void UElysiumTerminalScreen::EnterScreensaver()
{
	if (!HasProjection())
	{
		ReleaseProjectionOwnership();
		return;
	}
	TerminalSurface = BuildScreensaverSurface();
	RenderProjection();
	// The mesh retains the MID and the MID retains its texture. CommonUI can now die without
	// blanking the physical monitor; a later session replaces this material with a fresh live one.
	ReleaseProjectionOwnership();
}

void UElysiumTerminalScreen::ReleaseProjectionOwnership()
{
	ProjectionTarget.Reset();
	ProjectionMaterialIndex = INDEX_NONE;
	ProjectionMaterial = nullptr;
	RenderTarget = nullptr;
	WidgetRenderer.Reset();
}

void UElysiumTerminalScreen::SetDraftText(const FString& Text)
{
	if (!CommandEntry)
	{
		return;
	}
	const int32 Limit = FMath::Max(0, Terminal.MaxInput);
	const FString Clamped = Text.Left(Limit);
	bUpdatingDraft = true;
	CommandEntry->SetText(FText::FromString(Clamped));
	bUpdatingDraft = false;
	RenderProjection();
}

FString UElysiumTerminalScreen::GetDraftText() const
{
	return CommandEntry ? CommandEntry->GetText().ToString() : FString();
}

bool UElysiumTerminalScreen::SubmitDraft()
{
	const FString Command = Terminal.InputMode == 2 ? FString() : GetDraftText();
	return SubmitCommand(Command);
}

void UElysiumTerminalScreen::HandleDraftChanged(const FText& Text)
{
	if (bUpdatingDraft)
	{
		return;
	}
	const FString Changed = Text.ToString();
	const int32 Limit = FMath::Max(0, Terminal.MaxInput);
	if (Changed.Len() > Limit)
	{
		SetDraftText(Changed.Left(Limit));
		return;
	}
	RenderProjection();
}

void UElysiumTerminalScreen::HandleDraftCommitted(const FText&, ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnEnter)
	{
		SubmitDraft();
	}
}

bool UElysiumTerminalScreen::SubmitCommand(const FString& Command)
{
	if (!Terminal.IsOpen())
	{
		return false;
	}
	if (!OnCommand.IsBound())
	{
		UE_LOG(LogElysiumTerminalUI, Warning,
			TEXT("terminal UI cannot submit '%s': no command route is bound for %s serial %u"),
			*Command, *Terminal.Owner.ToString(), Terminal.SessionSerial);
		return false;
	}
	const bool bAccepted = OnCommand.Execute(Terminal.Owner, Terminal.SessionSerial, Command);
	if (bAccepted)
	{
		SetDraftText(FString());
	}
	return bAccepted;
}

void UElysiumTerminalScreen::ConfigureEditor()
{
	if (!CommandEntry)
	{
		return;
	}
	FEditableTextStyle Style = FCoreStyle::Get().GetWidgetStyle<FEditableTextStyle>(
		TEXT("NormalEditableText"));
	Style.SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Mono"), 18));
	Style.SetColorAndOpacity(FSlateColor(Phosphor));
	CommandEntry->SetWidgetStyle(Style);
	CommandEntry->SetMinimumDesiredWidth(1.0f);
	CommandEntry->SetClearKeyboardFocusOnCommit(false);
	CommandEntry->SetSelectAllTextOnCommit(false);
	CommandEntry->SetRevertTextOnEscape(false);
	CommandEntry->SetIsPassword(Terminal.InputMode == 1);
	CommandEntry->SetIsReadOnly(Terminal.InputMode == 2);
	CommandEntry->SetHintText(Terminal.InputMode == 1
		? NSLOCTEXT("Elysium", "TerminalPasswordHint", "Password")
		: Terminal.InputMode == 2
			? NSLOCTEXT("Elysium", "TerminalAcknowledgeHint", "Press Enter")
			: NSLOCTEXT("Elysium", "TerminalCommandHint", "Type menu or command"));
}

FText UElysiumTerminalScreen::ScreenText() const
{
	return FText::FromString(FString::Join(Terminal.ScreenRows, TEXT("\n")));
}

FText UElysiumTerminalScreen::DraftDisplayText() const
{
	if (Terminal.InputMode == 2)
	{
		return NSLOCTEXT("Elysium", "TerminalAcknowledgePrompt", "> Press Enter _");
	}
	const FString Draft = GetDraftText();
	const FString Visible = Terminal.InputMode == 1
		? FString::ChrN(Draft.Len(), TEXT('*'))
		: Draft;
	return FText::FromString(FString::Printf(TEXT("> %s_"), *Visible));
}

TSharedRef<SWidget> UElysiumTerminalScreen::BuildActionVisual(
	UElysiumActionButton& Action, const FText& Label)
{
	const TWeakObjectPtr<UElysiumActionButton> WeakAction(&Action);
	return SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox")))
		.BorderBackgroundColor_Lambda([WeakAction]()
		{
			const UElysiumActionButton* Button = WeakAction.Get();
			if (!Button || !Button->IsExecutable())
			{
				return FSlateColor(FLinearColor(0.035f, 0.055f, 0.045f, 0.72f));
			}
			return FSlateColor(Button->IsActionSelected()
				? FLinearColor(0.16f, 0.34f, 0.22f, 0.96f)
				: FLinearColor(0.025f, 0.10f, 0.055f, 0.86f));
		})
		.Padding(FMargin(12.0f, 6.0f))
		[
			SNew(STextBlock)
			.Text(Label)
			.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Mono"), 13))
			.ColorAndOpacity(FSlateColor(Phosphor))
		];
}

TSharedRef<SWidget> UElysiumTerminalScreen::BuildTerminalSurface()
{
	TSharedRef<SWrapBox> Actions = SNew(SWrapBox).UseAllottedSize(true);
	for (const FElysiumTerminalActionView& ActionView : Terminal.Actions)
	{
		const FName ActionId(*ActionView.Id);
		if (ActionId.IsNone())
		{
			UE_LOG(LogElysiumTerminalUI, Warning,
				TEXT("terminal UI ignored an action with an empty id for %s serial %u"),
				*Terminal.Owner.ToString(), Terminal.SessionSerial);
			continue;
		}
		const FText Label = FText::FromString(ActionView.Label);
		UElysiumActionButton* Action = CreateActionButton(
			ActionId, TerminalActions, Label, ActionView.bEnabled,
			[this, Command = ActionView.Command]() { SubmitCommand(Command); }, {},
			FText::FromString(ActionView.Explanation));
		if (!Action)
		{
			UE_LOG(LogElysiumTerminalUI, Warning,
				TEXT("terminal UI failed to create action '%s' for %s serial %u"),
				*ActionView.Id, *Terminal.Owner.ToString(), Terminal.SessionSerial);
			continue;
		}
		if (DefaultActionId.IsNone())
		{
			DefaultActionId = ActionId;
		}
		Action->SetSlateContent(BuildActionVisual(*Action, Label));
		Actions->AddSlot().Padding(FMargin(0.0f, 0.0f, 7.0f, 7.0f))
		[
			Action->TakeWidget()
		];
	}

	return SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox")))
		.BorderBackgroundColor(FSlateColor(ScreenBlack))
		.Padding(FMargin(28.0f, 24.0f))
		.Clipping(EWidgetClipping::ClipToBoundsAlways)
		[
			SNew(SScaleBox)
			.Stretch(EStretch::ScaleToFit)
			.StretchDirection(EStretchDirection::Both)
			[
				SNew(SBox)
				.WidthOverride(SurfaceWidth - 56.0f)
				.HeightOverride(SurfaceHeight - 48.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().FillHeight(1.0f)
					[
						SNew(STextBlock)
						.Text(this, &UElysiumTerminalScreen::ScreenText)
						.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Mono"), 18))
						.ColorAndOpacity(FSlateColor(Phosphor))
						.Clipping(EWidgetClipping::ClipToBoundsAlways)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f)
					[
						SNew(SBorder)
						.BorderImage(FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox")))
						.BorderBackgroundColor(FSlateColor(FLinearColor(0.018f, 0.05f, 0.029f, 0.92f)))
						.Padding(FMargin(10.0f, 7.0f))
						[
							SNew(STextBlock)
							.Text(this, &UElysiumTerminalScreen::DraftDisplayText)
							.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Mono"), 18))
							.ColorAndOpacity(FSlateColor(Phosphor))
						]
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						Actions
					]
				]
			]
		];
}

TSharedRef<SWidget> UElysiumTerminalScreen::BuildScreensaverSurface() const
{
	const FText Label = Terminal.ScreenSaverLabel.IsEmpty()
		? FText::GetEmpty()
		: FText::FromString(Terminal.ScreenSaverLabel);
	return SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox")))
		.BorderBackgroundColor(FSlateColor(ScreenBlack))
		.Padding(FMargin(28.0f, 24.0f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().FillHeight(1.0f)
			[
				SNullWidget::NullWidget
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Text(Label)
				.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Mono"), 15))
				.ColorAndOpacity(FSlateColor(MutedPhosphor))
			]
		];
}

void UElysiumTerminalScreen::RebuildTerminalSurface()
{
	BeginNavigationBuild();
	SetNavigationGroup(TerminalActions, true, true, true, true);
	DefaultActionId = NAME_None;
	ConfigureEditor();
	TerminalSurface = BuildTerminalSurface();
	FinalizeNavigationBuild(DefaultActionId);
	if (IsActivated() && CommandEntry && GetOwningPlayer())
	{
		CommandEntry->SetUserFocus(GetOwningPlayer());
	}
	RenderProjection();
}

void UElysiumTerminalScreen::RenderProjection()
{
	if (!HasProjection() || !TerminalSurface.IsValid())
	{
		return;
	}
	WidgetRenderer->DrawWidget(RenderTarget, TerminalSurface.ToSharedRef(), SurfaceDrawSize,
		0.0f, false);
}

TSharedRef<SWidget> UElysiumTerminalScreen::RebuildWidget()
{
	(void)Super::RebuildWidget();
	if (!WidgetTree)
	{
		UE_LOG(LogElysiumTerminalUI, Warning,
			TEXT("terminal UI rebuild has no WidgetTree for %s serial %u"),
			*Terminal.Owner.ToString(), Terminal.SessionSerial);
	}
	else if (!CommandEntry)
	{
		CommandEntry = WidgetTree->ConstructWidget<UEditableText>(
			UEditableText::StaticClass(), TEXT("TerminalCommandEntry"));
		if (CommandEntry)
		{
			CommandEntry->OnTextChanged.AddDynamic(this,
				&UElysiumTerminalScreen::HandleDraftChanged);
			CommandEntry->OnTextCommitted.AddDynamic(this,
				&UElysiumTerminalScreen::HandleDraftCommitted);
		}
		else
		{
			UE_LOG(LogElysiumTerminalUI, Warning,
				TEXT("terminal UI failed to create its command editor for %s serial %u"),
				*Terminal.Owner.ToString(), Terminal.SessionSerial);
		}
	}

	RebuildTerminalSurface();
	// This one-pixel transparent input shell is the only viewport widget. The visible console is
	// rendered exclusively through the monitor material above.
	return SNew(SBox)
		.WidthOverride(1.0f)
		.HeightOverride(1.0f)
		.RenderOpacity(0.0f)
		[
			CommandEntry ? CommandEntry->TakeWidget() : SNullWidget::NullWidget
		];
}

UWidget* UElysiumTerminalScreen::NativeGetDesiredFocusTarget() const
{
	return CommandEntry ? CommandEntry : Super::NativeGetDesiredFocusTarget();
}

bool UElysiumTerminalScreen::HandleNavigation(EElysiumNavigationDirection Direction)
{
	const int32 Delta = Direction == EElysiumNavigationDirection::Up
		|| Direction == EElysiumNavigationDirection::Left ? -1 : 1;
	// Action visuals live in the render-target Slate tree, outside the viewport focus path. Keep
	// keyboard focus on the invisible editor while updating the durable semantic selection.
	return SelectAdjacentInGroup(TerminalActions, Delta, true, false);
}

void UElysiumTerminalScreen::HandleSelectedActionChanged(FName, FName)
{
	RenderProjection();
}

FReply UElysiumTerminalScreen::NativeOnPreviewKeyDown(
	const FGeometry& Geometry, const FKeyEvent& KeyEvent)
{
	if (KeyEvent.GetKey() == EKeys::Escape)
	{
		if (!KeyEvent.IsRepeat())
		{
			SubmitCommand(TEXT("quit"));
		}
		return FReply::Handled();
	}
	if (KeyEvent.GetKey() == EKeys::C && KeyEvent.IsControlDown())
	{
		if (!KeyEvent.IsRepeat())
		{
			SubmitCommand(TEXT("break"));
		}
		return FReply::Handled();
	}
	if (KeyEvent.GetKey() == EKeys::Gamepad_FaceButton_Bottom)
	{
		if (!KeyEvent.IsRepeat())
		{
			if (!ExecuteSelectedAction())
			{
				SubmitDraft();
			}
		}
		return FReply::Handled();
	}

	const FKey Key = KeyEvent.GetKey();
	const bool bGamepadNavigation = Key == EKeys::Gamepad_DPad_Up
		|| Key == EKeys::Gamepad_DPad_Down || Key == EKeys::Gamepad_DPad_Left
		|| Key == EKeys::Gamepad_DPad_Right || Key == EKeys::Gamepad_LeftStick_Up
		|| Key == EKeys::Gamepad_LeftStick_Down || Key == EKeys::Gamepad_LeftStick_Left
		|| Key == EKeys::Gamepad_LeftStick_Right;
	if (CommandEntry && CommandEntry->HasKeyboardFocus() && !bGamepadNavigation)
	{
		// Do not let the navigable-screen W/A/S/D aliases consume ordinary terminal text entry.
		return UElysiumActivatableScreen::NativeOnPreviewKeyDown(Geometry, KeyEvent);
	}
	return Super::NativeOnPreviewKeyDown(Geometry, KeyEvent);
}

bool UElysiumTerminalScreen::NativeOnHandleBackAction()
{
	SubmitCommand(TEXT("quit"));
	return true;
}

void UElysiumTerminalScreen::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	ReleaseProjectionOwnership();
	TerminalSurface.Reset();
	CommandEntry = nullptr;
}
