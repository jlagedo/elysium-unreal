#pragma once

#include "CoreMinimal.h"

#include "Substrate/ElysiumChargen.h"
#include "UI/ElysiumActivatableScreen.h"

#include "ElysiumChargenPopup.generated.h"

class UTexture2D;
struct FSlateBrush;

// One `charcreatewizard.txt` popup, full screen: the framed page art with the question at its top
// left and the surviving answers as numbered lines below it. The entry popup and every quiz question
// are the same widget — they differ only in the `FElysiumWizPopup` handed to it.
//
// The same C++ Slate / `SDPIScaler` idiom as the character screen (`docs/architecture/ui-architecture.md`), and
// the same guard on every image: `out/ui/art` is gitignored, so a missing page degrades to the
// screen's own scrim rather than leaving a hole.
//
// The run is the caller's — this widget draws `Run.Popup`, reports the index that was clicked and
// asks to be redrawn. Everything that decides what happens next is `ElysiumChargen::WizChoose`.
UCLASS()
class UElysiumChargenPopup : public UElysiumActivatableScreen
{
	GENERATED_BODY()

public:
	UElysiumChargenPopup();

	// The run to draw. Held by the host, so the widget never owns the quiz's progress.
	void SetRun(TSharedPtr<FElysiumWizRun> InRun);
	void Refresh();

	// A numbered answer was clicked. The host applies it and either refreshes or takes the popup
	// down — the widget does neither.
	DECLARE_DELEGATE_OneParam(FOnChargenAnswer, int32);
	FOnChargenAnswer OnAnswer;

	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	virtual bool NativeOnHandleBackAction() override;

private:
	float VirtualScale() const;
	const FSlateBrush* Art(const FString& RelPath);
	TSharedRef<SWidget> BuildPage();

	TSharedPtr<FElysiumWizRun> Run;

	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UTexture2D>> ArtTextures;

	TMap<FString, TSharedPtr<FSlateBrush>> ArtBrushes;
	TSet<FString> ArtMissing;

	TSharedPtr<class SBox> PageHost;
};
