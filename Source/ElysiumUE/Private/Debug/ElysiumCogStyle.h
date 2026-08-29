#pragma once

#include "CogCommon.h"

#if ENABLE_COG

#include "CoreMinimal.h"
#include "imgui.h"

// The debug UI's VtMB skin: one blood-and-bone palette plus the ImGui style that carries it, shared
// by every Elysium Cog window (and, because the ImGui style is global, by the stock Cog windows and
// the F1 main menu too).
//
// Two layers:
//  - `ElysiumCogStyle::*` — the raw palette. Near-black warm-grey grounds, blood red as the single
//    accent, bone/parchment text, candle amber and absinthe green for the two data states that must
//    read at a glance. Windows use the *semantic* names below, never a literal ImVec4, so a hue can
//    be retuned in one place.
//  - `ElysiumCogStyle::EnsureApplied()` — installs the palette into ImGui's global style, and keeps
//    it installed. Cog rebuilds the style from ImGui's defaults on a DPI change, so this re-applies
//    whenever it detects the style is no longer ours (one ImVec4 compare per tick).
//
// `elysium.CogTheme 0` restores stock ImGui dark for an A/B.
namespace ElysiumCogStyle
{
	inline const ImVec4 Ink(0.047f, 0.039f, 0.043f, 0.94f);      // window ground
	inline const ImVec4 InkChild(0.071f, 0.059f, 0.063f, 0.55f); // child / scrolling region
	inline const ImVec4 InkPopup(0.086f, 0.071f, 0.075f, 0.98f); // popups, menus, tooltips
	inline const ImVec4 InkTitle(0.106f, 0.078f, 0.086f, 1.00f); // title bars, tabs
	inline const ImVec4 Frame(0.145f, 0.110f, 0.118f, 1.00f);    // input/checkbox/slider ground
	inline const ImVec4 FrameHovered(0.212f, 0.141f, 0.149f, 1.00f);
	inline const ImVec4 FrameActive(0.290f, 0.169f, 0.176f, 1.00f);
	inline const ImVec4 Border(0.259f, 0.157f, 0.169f, 1.00f);

	inline const ImVec4 Blood(0.545f, 0.098f, 0.129f, 1.00f);      // the accent
	inline const ImVec4 BloodHi(0.710f, 0.157f, 0.180f, 1.00f);    // hovered / active
	inline const ImVec4 BloodBright(0.898f, 0.243f, 0.235f, 1.00f); // ticks, grabs, overlines
	inline const ImVec4 BloodDim(0.298f, 0.071f, 0.094f, 1.00f);   // selected ground

	inline const ImVec4 Bone(0.855f, 0.824f, 0.780f, 1.00f);       // body text
	inline const ImVec4 BoneDim(0.522f, 0.490f, 0.463f, 1.00f);    // disabled text
	inline const ImVec4 Gold(0.804f, 0.690f, 0.463f, 1.00f);       // names, links, totals
	inline const ImVec4 Absinthe(0.545f, 0.784f, 0.435f, 1.00f);   // ok / live / present
	inline const ImVec4 Candle(0.929f, 0.706f, 0.298f, 1.00f);     // warning / hidden / pending
	inline const ImVec4 Wound(0.882f, 0.310f, 0.290f, 1.00f);      // error / dead / missing
	inline const ImVec4 Ash(0.478f, 0.451f, 0.435f, 1.00f);        // inert record, stubs

	inline const ImVec4& ColOk = Absinthe;        // healthy, live, loaded, current
	inline const ImVec4& ColWarn = Candle;        // hidden, pending, totals worth the eye
	inline const ImVec4& ColError = Wound;        // dead, failed, missing
	inline const ImVec4& ColName = Gold;          // an identifier being quoted back
	inline const ImVec4& ColDim = BoneDim;        // secondary prose
	inline const ImVec4& ColInert = Ash;          // a record with no registered class
	inline const ImVec4& ColSelected = Absinthe;  // the row the inspector is holding

	// Install the palette if the global ImGui style is not already carrying it (first tick, a DPI
	// rebuild, or an `elysium.CogTheme` flip). Cheap enough to call every tick.
	void EnsureApplied(float InDpiScale);

	// A label/value line whose values line up down a whole section. `InValueColumn` is a window-space
	// X in pixels; a label wider than the column pushes its value right by one item-spacing rather
	// than letting the two overlap (`ImGui::SameLine(Offset)` alone would overlap). `InValueColor`
	// tints the value only — the label stays body text.
	void LabelValue(const char* InLabel, const char* InValue, float InValueColumn,
		const ImVec4* InValueColor = nullptr);
}

#endif // ENABLE_COG
