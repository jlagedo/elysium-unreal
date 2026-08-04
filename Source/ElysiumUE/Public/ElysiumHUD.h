#pragma once

#include "CoreMinimal.h"
#include "ElysiumInputScope.h"
#include "ElysiumViewState.h"
#include "GameFramework/HUD.h"
#include "Templates/PimplPtr.h"
#include "UObject/StrongObjectPtr.h"
#include "ElysiumHUD.generated.h"

class AElysiumMapActor;
class FElysiumSignFontLibrary;
class IConsoleObject;
class UElysiumDialogueScreen;
class UElysiumPresentationSubsystem;
class UTexture2D;

// The legacy world-HUD bridge. The local-player UElysiumPlayerUISubsystem owns the always-on reticle,
// vitals and fade; this actor keeps the faithful Canvas sign panel, the retained dialogue bridge,
// and map-scoped developer commands until those modal surfaces move into the unified root.
//
// **It reads FElysiumViewState and nothing else** (11.8): no map-actor walk, no FElysiumEntityWorld,
// no per-draw-path IsMenuUp() check. `UElysiumPresentationSubsystem` publishes the state in step 9
// of the frame and calls OnViewPublished right after, which is where the retained surfaces — the
// dialogue box and the sign's input scope — reconcile; DrawHUD draws only the sign panel on Canvas.
// The local-player HUD subsystem consumes the same publication for its Slate surface. The map-actor
// handle that survives is the dev console verbs' (`elysium.lights` and friends), which are not
// presentation.
UCLASS()
class AElysiumHUD : public AHUD
{
	GENERATED_BODY()

public:
	AElysiumHUD();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void DrawHUD() override;

private:
	IConsoleObject* LightsCmd = nullptr;
	IConsoleObject* PropsCmd = nullptr;
	IConsoleObject* LightProbeCmd = nullptr;

	// For the dev console verbs above only — never for anything drawn.
	AElysiumMapActor* ResolveMapActor() const;

	UElysiumPresentationSubsystem* Presentation() const;
	// This frame's published state, or a default (nothing on screen) when there is no publisher.
	const FElysiumViewState& View() const;

	// Called from the publisher, once per frame, right after the state is rebuilt. The retained
	// surfaces reconcile here rather than in an actor tick: the publish runs in TG_PostUpdateWork
	// and a HUD tick would be a frame behind it, which is one frame of a dialogue box drawing over
	// a pause menu that just opened.
	void OnViewPublished(const FElysiumViewState& NewView);
	FDelegateHandle ViewPublishedHandle;

	// --- Dialogue box (P9 9.1 / B4) ----------------------------------------------------------
	// The visual-novel `.dlg` panel, a CommonUI screen wrapping the native Slate body while published
	// state carries an open conversation. `ElysiumView::ReconcileDialogue` decides build / rebuild /
	// teardown against what is already up; while the box is up it holds a UI-only input scope (the
	// VN freezes the world), and a pick routes back through the presenter's DialogueChoose.
	void RebuildDialogue(const FElysiumDialogueView& Dialogue);
	void TeardownDialogue();
	void OnDialogueChoice(int32 VisibleIndex);   // -1 = advance a terminal line

	UPROPERTY(Transient)
	TObjectPtr<UElysiumDialogueScreen> DialogueScreen;
	// Identity + turn of the conversation the box currently shows. The pointer is compared and never
	// dereferenced — the conversation is the world's, and the map epoch it lives in can end.
	const FElysiumDlgConversation* ShownConv = nullptr;
	uint32 ShownRev = 0;
	// --- Sign / popup window (P4.10) ---------------------------------------------------------
	// The one open game_sign panel, taken off the published state each frame with its fade-in ramp
	// already resolved. Layout is CSignUI's 1024x768 virtual canvas stretched to the viewport — see
	// ElysiumSignData.h for the decompile this reproduces.
	void DrawSignPanel(const FElysiumViewState& V);
	// Background art by material name (e.g. "interface/pop_ups/general"), decoded from the PL5c
	// mirror on first use. A miss caches null so a missing PNG is not retried every frame.
	UTexture2D* GetSignBackground(const FString& ImageName);

	// Hold an input scope for as long as the published state carries an open sign (11.5). The panel
	// deliberately keeps game input — VtMB's popups say "left-click to continue" and that click is
	// the player controller's, not a widget's — so the scope changes no mode; what it does is put the
	// panel in the arbitration order, so a menu opening over it restores exactly the sign's state on
	// close.
	void UpdateSignScope(bool bSignOpen);

	FElysiumInputScopeHandle SignScope;
	bool bSignManifestLoaded = false;
	TMap<FString, FString> SignBackgroundFiles;                  // material name -> png filename
	TMap<FString, TStrongObjectPtr<UTexture2D>> SignBackgrounds;  // material name -> texture (null = failed)

	// The panel's typeface set (Content/Fonts OFL faces keyed by VtMB's authored face names). Built
	// lazily on the first sign draw; see ElysiumSignFonts.h. TPimplPtr (not TUniquePtr) so a
	// forward-declared incomplete type works as a UCLASS member — it carries its own deleter, so
	// UHT's generated constructors don't need the complete type here.
	TPimplPtr<FElysiumSignFontLibrary> SignFonts;
};
