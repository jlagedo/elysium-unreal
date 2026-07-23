#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "UObject/StrongObjectPtr.h"
#include "ElysiumHUD.generated.h"

class AElysiumMapActor;
class IConsoleObject;
class UTexture2D;

// Debug overlay, toggled by the `elysium.debug` console command (' or ` opens the console)
// or F1. Hidden on a clean load. Top-left panel: map + surface counts, the view point in
// metres and Source units, yaw, movement/skybox state, and a crosshair pick reporting the
// mesh under the aim. A big FPS meter sits top-right. The centre crosshair is always drawn
// (independent of the overlay toggle and any Cog window), so the aim reticle never flickers.
UCLASS()
class AElysiumHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void DrawHUD() override;

	void ToggleDebug() { bShowDebug = !bShowDebug; }
	bool IsDebugVisible() const { return bShowDebug; }

private:
	bool bShowDebug = false;
	float SmoothedFPS = 0.f;
	IConsoleObject* DebugCmd = nullptr;
	IConsoleObject* LightsCmd = nullptr;
	IConsoleObject* PropsCmd = nullptr;

	AElysiumMapActor* ResolveMapActor() const;

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
};
