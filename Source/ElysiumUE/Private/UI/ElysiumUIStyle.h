#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "UObject/StrongObjectPtr.h"

class UFont;

// The UI design-token layer (roadmap 8.6). One place that owns the palette, the type ramp and
// the metric scale, so every screen reads the same values instead of re-deriving them.
//
// **The virtual canvas is VtMB's own.** `client.dll` authors its UI in a 1024x768 space and
// scales it by `screenW/1024` / `screenH/768` (`docs/vtmb-ui.md` §2, recovered from
// `CVMainMenu::PerformLayout`) — the same canvas `CSignUI` uses. Every size below is in that
// space and is multiplied by `ElysiumUI::ScaleFor(ScreenH)` at draw time, so the layout law the
// RE recovered *is* the layout code and the UI is resolution-independent by construction.
// Scaling is applied once, by an `SDPIScaler` at the root of each screen, rather than through
// the engine's `UIScaleCurve` — the curve would have to restate the same ratio in an ini and
// could then drift from the canvas the panels are authored against.
namespace ElysiumUI
{
	// VtMB's authored UI canvas. Height is the divisor because the UI is vertically anchored;
	// width varies with aspect and the layout reflows into it (real ultrawide, no letterbox).
	inline constexpr float VirtualW = 1024.0f;
	inline constexpr float VirtualH = 768.0f;

	// Virtual units -> pixels. Mirrors ElysiumSign::ScaleFor so signs, menu and HUD share one law.
	inline float ScaleFor(float ScreenH) { return (ScreenH > 0.0f) ? (ScreenH / VirtualH) : 1.0f; }

	// --- Palette -------------------------------------------------------------------------------
	// Read from the install's own `VampireScheme.res` (the scheme client.dll loads) and confirmed
	// against reference captures. The chrome is gold; blood red is an accent reserved for the menu
	// column, the pips and critical states — not the ground.
	namespace Palette
	{
		// Chrome / labels — `VUnselectedText` rising to `VDesHeaderText`.
		inline const FLinearColor Gold      = FLinearColor::FromSRGBColor(FColor(171, 140,  95));
		inline const FLinearColor GoldLit   = FLinearColor::FromSRGBColor(FColor(255, 240, 191));
		// Body copy — `BaseText` / `ControlText`.
		inline const FLinearColor Bone      = FLinearColor::FromSRGBColor(FColor(216, 222, 211));
		inline const FLinearColor BoneDim   = FLinearColor::FromSRGBColor(FColor(150, 159, 142));
		// Selection / disabled — `VSelectedText`, `VDisabledText`.
		inline const FLinearColor White     = FLinearColor::FromSRGBColor(FColor(255, 255, 255));
		inline const FLinearColor Disabled  = FLinearColor::FromSRGBColor(FColor(128, 128, 128));
		// The active tab — `BrightControlText`.
		inline const FLinearColor Cyan      = FLinearColor::FromSRGBColor(FColor(109, 207, 246));
		// The ART's gold, sampled off `cm_divider` and the sheet panel frames — warmer than the
		// scheme's text gold, which is a TEXT colour. A drawn rule continuing a bitmap rule has to
		// use this or the two read as two different lines meeting.
		inline const FLinearColor Amber     = FLinearColor::FromSRGBColor(FColor(192, 136,  72));
		// The menu column. Hardcoded in CVMainMenu as 0xc00000a8 = RGBA(168,0,0,192); the armed
		// colour is ours (retail swaps to the same value at full alpha, which reads as no change
		// on a modern display).
		inline const FLinearColor Blood     = FLinearColor::FromSRGBColor(FColor(168,   0,   0));
		inline const FLinearColor BloodLit  = FLinearColor::FromSRGBColor(FColor(210,  20,  20));
		// Grounds. Every background colour in VampireScheme is fully transparent — the UI floats
		// on the scene — so these are ours, for the few surfaces that must occlude.
		inline const FLinearColor Ink       = FLinearColor(0.008f, 0.007f, 0.006f, 1.0f);
		inline const FLinearColor Scrim     = FLinearColor(0.0f, 0.0f, 0.0f, 0.55f);
	}

	// --- Type ramp (virtual px) ------------------------------------------------------------------
	// Sizes, not faces: the role/weight picks the face. Tuned against the reference captures rather
	// than against VtMB's per-resolution `.fnt` tiers, which do not survive vector type.
	namespace Type
	{
		inline constexpr float Display  = 46.0f;   // the one big statement
		inline constexpr float Title    = 32.0f;   // screen titles
		inline constexpr float MenuItem = 30.0f;   // the main-menu column
		inline constexpr float Heading  = 22.0f;   // section headers
		inline constexpr float Body     = 16.0f;   // running copy
		inline constexpr float Label    = 14.0f;   // sheet rows, HUD labels
		inline constexpr float Caption  = 12.0f;

		// Small caps want air; body copy does not. Slate takes tracking in points at the drawn
		// size, so these are multiplied by the final pixel size, not by the virtual one.
		inline constexpr float TrackLabel = 0.10f;
		inline constexpr float TrackBody  = 0.0f;
	}

	// --- Spacing (virtual px) --------------------------------------------------------------------
	namespace Space
	{
		inline constexpr float XS = 4.0f;
		inline constexpr float S  = 8.0f;
		inline constexpr float M  = 16.0f;
		inline constexpr float L  = 28.0f;
		inline constexpr float XL = 48.0f;
	}
}

// Which of the three Nocturne families a piece of text belongs to (docs/ui-architecture.md).
enum class EElysiumFontRole : uint8
{
	Label,   // Spectral SC — small caps: menu items, sheet rows, HUD labels, headers
	Body,    // Spectral    — running copy: signs, subtitles, descriptions
	Data,    // Inter       — numerals and dense data, tabular figures
};

enum class EElysiumFontWeight : uint8
{
	Regular,
	SemiBold,
	Italic,   // Body only; falls back to Regular on the other two roles
};

// Resolves role+weight to a drawable face, composing the committed `UFontFace` assets under
// `/Game/VtMB/UI/Fonts` into runtime `UFont`s. The faces are real cooked assets (built by
// `tools/make_ui_fonts.py`), not loose TTFs read at draw time — so they stream and cook like any
// other content. One `UFont` is built per role, with the weights as named typeface entries.
class FElysiumUIFontLibrary
{
public:
	// VirtualSize is in the 768-tall canvas; Scale is ElysiumUI::ScaleFor(ScreenH).
	FSlateFontInfo Font(EElysiumFontRole Role, EElysiumFontWeight Weight,
	                    float VirtualSize, float Scale);

	// True once every face resolved — a missing asset falls back to Slate's default face at the
	// same size, so layout still works but the type is visibly wrong.
	bool IsComplete() const { return bComplete; }

private:
	UFont* FontForRole(EElysiumFontRole Role);

	TMap<uint8, TStrongObjectPtr<UFont>> Fonts;   // role -> composed runtime UFont (null = failed)
	bool bComplete = true;
};

// The module's one font library, shared by every screen. Composing a role's `UFont` is not free and
// the faces are identical wherever they are drawn, so this is a single instance rather than one per
// screen. The composed `UFont`s are transient UObjects held by `TStrongObjectPtr` inside, so they
// survive GC for the session.
FElysiumUIFontLibrary& ElysiumUIFonts();
