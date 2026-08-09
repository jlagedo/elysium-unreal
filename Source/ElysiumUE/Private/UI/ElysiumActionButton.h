#pragma once

#include "CommonButtonBase.h"
#include "Components/Widget.h"

#include "ElysiumActionButton.generated.h"

class SBox;
class SWidget;

// A UMG-owned bridge for the retained Slate visuals used by Elysium's code-only UI. Keeping the
// visual inside a UWidget lets UCommonButtonBase remain the real focus and activation target.
UCLASS()
class UElysiumSlateHost final : public UWidget
{
	GENERATED_BODY()

public:
	void SetContent(TSharedRef<SWidget> InContent);
	bool HasContent() const { return Content.IsValid(); }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
	TSharedPtr<SWidget> Content;
	TSharedPtr<SBox> ContentBox;
};

// Transparent native style: every screen supplies its own retained Slate presentation, while the
// CommonButton internal widget contributes only input, focus, accessibility and activation.
UCLASS()
class UElysiumActionButtonStyle final : public UCommonButtonStyle
{
	GENERATED_BODY()

public:
	UElysiumActionButtonStyle();
};

class UElysiumActionButton;
DECLARE_MULTICAST_DELEGATE_OneParam(FElysiumActionButtonEvent, UElysiumActionButton&);

// The one actionable control used by code-authored screens. It deliberately stays focusable when
// an action cannot execute so a menu can select the row and explain why it is unavailable.
UCLASS()
class UElysiumActionButton final : public UCommonButtonBase
{
	GENERATED_BODY()

public:
	UElysiumActionButton(const FObjectInitializer& ObjectInitializer);

	virtual bool Initialize() override;

	void Configure(FName InActionId, const FText& InLabel, const FText& InCaption,
		bool bInExecutable);
	void SetSlateContent(TSharedRef<SWidget> InContent);
	void SetExecutable(bool bInExecutable) { bExecutable = bInExecutable; }

	FName GetActionId() const { return ActionId; }
	const FText& GetActionLabel() const { return Label; }
	const FText& GetActionCaption() const { return Caption; }
	bool IsExecutable() const { return bExecutable; }
	bool IsActionSelected() const { return GetSelected(); }
	bool HasSlateContent() const { return SlateHost && SlateHost->HasContent(); }

	FElysiumActionButtonEvent& OnActionActivated() { return ActionActivatedEvent; }
	FElysiumActionButtonEvent& OnSecondaryActivated() { return SecondaryActivatedEvent; }
	FElysiumActionButtonEvent& OnHighlighted() { return HighlightedEvent; }

protected:
	virtual void SynchronizeProperties() override;
	virtual FReply NativeOnFocusReceived(const FGeometry& InGeometry,
		const FFocusEvent& InFocusEvent) override;
	virtual void NativeOnHovered() override;
	virtual void NativeOnClicked() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry,
		const FPointerEvent& InMouseEvent) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UElysiumSlateHost> SlateHost;

	FName ActionId;
	FText Label;
	FText Caption;
	bool bExecutable = true;

	FElysiumActionButtonEvent ActionActivatedEvent;
	FElysiumActionButtonEvent SecondaryActivatedEvent;
	FElysiumActionButtonEvent HighlightedEvent;
};
