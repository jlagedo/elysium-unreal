#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "Templates/PimplPtr.h"
#include "UObject/StrongObjectPtr.h"
#include "ElysiumHUD.generated.h"

class AElysiumMapActor;
class FElysiumDlgConversation;
class FElysiumSignFontLibrary;
class IConsoleObject;
class SElysiumDialogueBox;
class UTexture2D;

// The game HUD. Always-on: the centre crosshair (or the +use context cursor), the sign/popup
// panel, and the env_fade screen fade. Player pose, FPS, and movement/skybox/light state live
// in the Cog Maps window's Player section (`docs/debug-tooling.md`), not here.
UCLASS()
class AElysiumHUD : public AHUD
{
	GENERATED_BODY()

public:
	AElysiumHUD();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void DrawHUD() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	IConsoleObject* LightsCmd = nullptr;
	IConsoleObject* PropsCmd = nullptr;

	AElysiumMapActor* ResolveMapActor() const;

	// --- Dialogue box (P9 9.1 / B4) ----------------------------------------------------------
	// The visual-novel `.dlg` panel, a native Slate widget added to the viewport while a conversation
	// is open on the entity world. Ticked (not drawn on the Canvas): each frame the HUD polls the
	// world's open conversation and rebuilds the box when the turn changes, tearing it down when the
	// conversation ends. While it is up the player controller is in UI-only input (the VN freezes the
	// world); a pick routes back through the world's PlayerDialogChoose chokepoint.
	void UpdateDialogue();
	void TeardownDialogue();
	void OnDialogueChoice(int32 VisibleIndex);   // -1 = advance a terminal line

	TSharedPtr<SElysiumDialogueBox> DialogueWidget;
	FElysiumDlgConversation* DialogueConv = nullptr;   // identity/revision compare only; owned by the world
	uint32 DialogueRev = 0;
	bool bDialogueInput = false;                       // the UI-only input mode is currently installed

	// --- +use context-icon reticle (P4.4) ----------------------------------------------------
	// The reticle swaps to VtMB's context cursor while the +use look-cursor is on a usable entity:
	// the ring frame + the entity's GetUseIcon() cell (locked_icon when locked). Atlas + per-icon
	// UVs come from the offline `out/hud/use_icons.png` + `.json` (PL3); loaded once, lazily, on the
	// first DrawHUD (BeginPlay is too early for a reliable file read on some launch paths).
	void EnsureUseIconAtlas();
	void DrawUseReticle(float CenterX, float CenterY, int32 IconIndex);

	bool bUseAtlasLoadAttempted = false;
	TStrongObjectPtr<UTexture2D> UseAtlas;
	FBox2D UseRingUV = FBox2D(ForceInit);   // context_icon_ring frame (drawn around every usable)
	TMap<int32, FBox2D> UseIconUV;          // use_icon index (1-based) -> atlas UV rect

	// --- Sign / popup window (P4.10) ---------------------------------------------------------
	// The one open game_sign panel, polled off the entity world each frame (same seam as the
	// env_fade screen state). Layout is CSignUI's 1024x768 virtual canvas stretched to the
	// viewport — see ElysiumSignData.h for the decompile this reproduces.
	void DrawSignPanel();
	// Background art by material name (e.g. "interface/pop_ups/general"), decoded from the PL5c
	// mirror on first use. A miss caches null so a missing PNG is not retried every frame.
	UTexture2D* GetSignBackground(const FString& ImageName);

	bool bSignManifestLoaded = false;
	TMap<FString, FString> SignBackgroundFiles;                  // material name -> png filename
	TMap<FString, TStrongObjectPtr<UTexture2D>> SignBackgrounds;  // material name -> texture (null = failed)

	// The panel's typeface set (Content/Fonts OFL faces keyed by VtMB's authored face names). Built
	// lazily on the first sign draw; see ElysiumSignFonts.h. TPimplPtr (not TUniquePtr) so a
	// forward-declared incomplete type works as a UCLASS member — it carries its own deleter, so
	// UHT's generated constructors don't need the complete type here.
	TPimplPtr<FElysiumSignFontLibrary> SignFonts;
};
