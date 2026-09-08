#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "UObject/StrongObjectPtr.h"

class UFont;
class UWidget;

// The UI design-token layer. One place that owns the palette, the type ramp and the metric scale,
// so every screen reads the same values instead of re-deriving them.
//
// **The virtual canvas is VtMB's own.** `client.dll` authors its UI in a 1024x768 space and
// scales it by `screenW/1024` / `screenH/768` (`docs/vtmb/vtmb-ui.md` §2, recovered from
// `CVMainMenu::PerformLayout`) — the same canvas `CSignUI` uses. Every size below is in that
// space and is multiplied by `ElysiumUI::ScaleFor(ScreenH)` at draw time, so the layout law the
// RE recovered *is* the layout code and the UI is resolution-independent by construction.
// Scaling is applied once, by an `SDPIScaler` at the root of each screen, rather than through
// the engine's `UIScaleCurve`. The project pins that curve to 1 in `DefaultEngine.ini` so
// `SGameLayerManager` is identity; `ScaleFor` is the only scale, in PIE and in `-game` alike.
namespace ElysiumUI
{
	// VtMB's authored UI canvas. Height is the divisor because the UI is vertically anchored;
	// width varies with aspect and the layout reflows into it (real ultrawide, no letterbox).
	inline constexpr float VirtualW = 1024.0f;
	inline constexpr float VirtualH = 768.0f;

	// Height of `Widget` in the space an `SDPIScaler` inside it paints. Prefers cached paint
	// geometry so editor DPI cannot inflate the canvas; falls back to the game viewport in
	// Slate units (pixels / window DPI) before the first layout.
	float PaintHeight(const UWidget& Widget);

	// Virtual units -> paint units. Mirrors ElysiumSign::ScaleFor so signs, menu and HUD share one law.
	inline float ScaleFor(float ScreenH) { return (ScreenH > 0.0f) ? (ScreenH / VirtualH) : 1.0f; }
	inline float ScaleFor(const UWidget& Widget) { return ScaleFor(PaintHeight(Widget)); }

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

	// One computer terminal's glass, by `colorscheme`.
	//
	// `m_nColorScheme` (`DT_BaseTerminal+0x818`) is the index the retail rasterizer reads its four
	// palette records at `0x10233378` with, clamped to `[0, 3]` at spawn. The INDEX is retail; the
	// colours are **not** — the four records there are 2004 CRT values chosen against a CRT, and
	// `docs/vtmb/computer-terminals.md` §6.4 records them without copying them. These four are the
	// project's own, a named modernization, and every authored `colorscheme` still selects one of
	// exactly four the way retail's does.
	//
	// The style bit (`0x80`) is the cell's own inverse-video switch, and retail's rasterizer XORs
	// the foreground/background pair on it — so `Alternate*` is that pair swapped rather than a
	// fifth and sixth colour. Bit SET is the resting style (`FElysiumTerminalScreenBuffer::
	// DefaultStyle`), so a cleared bit is the reverse-video block.
	struct FElysiumTerminalPalette
	{
		FLinearColor Background = FLinearColor::Black;
		FLinearColor Foreground = FLinearColor::White;
		// The block cursor. Its own token because a cursor that is exactly the foreground reads as a
		// glyph on a phosphor screen; each palette lifts it.
		FLinearColor Cursor = FLinearColor::White;
		FLinearColor AlternateBackground = FLinearColor::White;
		FLinearColor AlternateForeground = FLinearColor::Black;
	};

	// `colorscheme`, clamped to `[0, 3]`: 0 amber, 1 green, 2 cold white, 3 cyan. Out-of-range
	// indices clamp rather than assert — the authority already clamps at spawn, and a projection
	// carrying a stale view must still draw something.
	const FElysiumTerminalPalette& TerminalPalette(int32 ColorScheme);

	// Type ramp in virtual px. Sizes, not faces: the role/weight picks the face. Tuned against the
	// reference captures rather than against VtMB's per-resolution `.fnt` tiers, which do not
	// survive vector type.
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

	// Spacing in virtual px.
	namespace Space
	{
		inline constexpr float XS = 4.0f;
		inline constexpr float S  = 8.0f;
		inline constexpr float M  = 16.0f;
		inline constexpr float L  = 28.0f;
		inline constexpr float XL = 48.0f;
	}
}

// Which of the Nocturne families a piece of text belongs to.
enum class EElysiumFontRole : uint8
{
	Label,   // Spectral SC — small caps: menu items, sheet rows, HUD labels, headers
	Body,    // Spectral    — running copy: signs, subtitles, descriptions
	Data,    // Inter       — numerals and dense data, tabular figures
	// Terminus — the character-cell grid on a computer terminal's glass, and nothing else. It is a
	// bitmap-derived face with square outlines, so it is the only role whose faces are authored with
	// the distance-field ppem raised (`pipeline/unreal/make_ui_fonts.py`); at the default ppem the
	// MSDF rounds its corners and the 36x24 grid reads as a blur.
	Mono,
};

enum class EElysiumFontWeight : uint8
{
	Regular,
	SemiBold,
	Italic,   // Body only; falls back to Regular on the other two roles
};

// Resolves role+weight to a drawable face, composing the generated local `UFontFace` assets under
// `/Game/ElysiumGenerated/UI/Fonts` into runtime `UFont`s. The faces are real cooked assets (built by
// `pipeline/unreal/make_ui_fonts.py`), not loose TTFs read at draw time — so they stream and cook like any
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
