#pragma once

#include "CommonActivatableWidget.h"
#include "ElysiumWorldServices.h"

#include "ElysiumNotificationScreen.generated.h"

class UElysiumNotificationScreen;
class SWidget;

DECLARE_DELEGATE_OneParam(FOnElysiumNotificationFinished, UElysiumNotificationScreen*);

// Pure notification presentation rules. Exposed beside the screen so automation can assert the
// complete lifetime without sleeping or constructing a viewport.
namespace ElysiumNotificationUI
{
	inline constexpr float EnterSeconds = 0.20f;
	inline constexpr float HoldSeconds = 2.40f;
	inline constexpr float ExitSeconds = 0.25f;
	inline constexpr float TotalSeconds = EnterSeconds + HoldSeconds + ExitSeconds;
	inline constexpr float CardWidth = 520.0f;
	inline constexpr float TopMargin = 48.0f;

	FText CategoryLabel(EElysiumNotificationKind Kind);
	FText SubjectLabel(const FElysiumNotification& Notification);
	float OpacityAt(float ElapsedSeconds);
	float OffsetYAt(float ElapsedSeconds);
}

// One passive entry in the root's CommonUI notification queue. It owns no input scope and asks the
// local-player owner to remove it when its presentation lifetime completes.
UCLASS()
class UElysiumNotificationScreen final : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	UElysiumNotificationScreen();

	void ApplyNotification(const FElysiumNotification& InNotification);
	void SetSuspended(bool bInSuspended) { bSuspended = bInSuspended; }
	const FElysiumNotification& GetNotification() const { return Notification; }
	bool IsSuspended() const { return bSuspended; }
	float ElapsedSeconds() const { return Elapsed; }

	FOnElysiumNotificationFinished OnFinished;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeOnActivated() override;
	virtual void NativeOnDeactivated() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
	float VirtualScale() const;
	void ApplyAnimationState();

	FElysiumNotification Notification;
	TSharedPtr<SWidget> CardHost;
	float Elapsed = 0.0f;
	bool bSuspended = false;
	bool bFinishRequested = false;
};
