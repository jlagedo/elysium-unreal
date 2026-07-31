#pragma once

#include "CoreMinimal.h"

class FElysiumEntityWorld;

// P4.10 — the `SignData` panel a `game_sign`/`prop_sign` names through `definition_file`.
//
// Provenance: the whole draw model below is read out of the decompiled **client.dll**
// (`CSignUI`; dumps in $ELYSIUM_WORK_ROOT/research/ghidra/out/signui_*.txt). The entity and its `definition_file`
// resolution live in `vampire.dll` (`CGameSign::LoadSignData` @0x10212da0), but every pixel
// decision — the coordinate space, the defaults, the font buckets — is client-side:
//
//   * `FUN_10061520` parses the file: `HideHUD`, then `BackgroundImage`, `Rules`, and the
//     repeatable `TextBlock` / `Label` blocks in order.
//   * `FUN_10061830` (background) and `FUN_10060560` (text block) both map the authored
//     rect through a **1024 x 768 virtual canvas** onto the viewport and call SetPos/SetSize
//     (`FUN_101a9950`). The scale factors are the doubles at 0x10227ec8 = 1/1024 and
//     0x10227eb8 = 1/768; the screen dims come from `FUN_100cd100`/`FUN_100cd0f0`.
//   * **Text blocks are positioned relative to the panel, not the screen.** FUN_10061830 is a
//     CSignUI *method* — it sets the sign panel's own bounds from the BackgroundImage block —
//     and the TextBlock/Label panels are its VGUI children, so their XPos/YPos are offsets
//     inside that rect. The shipped data proves it: the patch moved
//     tutorial_popup_moving1's body text 324 -> 836 in the same edit that widened the
//     background 1024 -> 2048, and a 2048-wide centred panel starts at virtual -512 — so
//     -512 + 836 lands on the same pixel retail's 324 did against the 1024-wide panel.
//   * The stretch is **non-uniform** — X scales by width, Y by height, independently. There
//     is no aspect preservation and no letterbox. This is client.dll's global HUD convention
//     (427 references to the 1/1024 constant), not something specific to signs.
namespace ElysiumSign
{
	// The authored coordinate space. Every XPos/YPos/Wide/Tall in a SignData file is in these
	// units regardless of the player's resolution.
	constexpr float VirtualWidth  = 1024.0f;
	constexpr float VirtualHeight = 768.0f;

	// Virtual units -> pixels. One factor for both axes: the authored canvas keeps its 4:3 aspect
	// and is letterboxed horizontally rather than stretched (see RectToScreen).
	inline double ScaleFor(float ScreenH) { return double(ScreenH) / double(VirtualHeight); }

	// Map the **panel's** authored rect onto the viewport, reproducing CSignUI exactly (including
	// its truncate-toward-zero integer math). `bCentre` selects the background-only branch taken
	// when XPos and YPos are both zero/absent: the scaled rect is centred on screen rather than
	// placed at the canvas origin. Out params are pixels.
	//
	// This is the TOP-LEVEL mapping and carries the horizontal letterbox offset. Child elements
	// (TextBlock/Label) are positioned inside the panel, so they use `ScaleFor` against the
	// panel's returned position instead — applying the offset twice would double-count it.
	void RectToScreen(int32 X, int32 Y, int32 W, int32 H, float ScreenW, float ScreenH,
		bool bCentre, FVector2D& OutPos, FVector2D& OutSize);
}

// One `BackgroundImage` block.
struct FElysiumSignBackground
{
	bool    bValid = false;
	FString ImageName;             // material name, lowercased (keys out/signs/backgrounds.json)
	bool    bTiled = false;        // `Tiled` — always 0 across the shipped set
	int32   XPos = 0;              // default 0
	int32   YPos = 0;              // default 0
	int32   Wide = 0;              // absent -> screen width  (bHasWide false)
	int32   Tall = 0;              // absent -> screen height (bHasTall false)
	bool    bHasWide = false;
	bool    bHasTall = false;
	// CSignUI: `if (XPos + YPos == 0)` take the centring branch. True for all 71 tutorial
	// popups, which author `"XPos" ""` / `"YPos" ""`.
	bool    bCentre = false;
};

// One `TextBlock` or `Label` block (CSignUI treats them as the same panel kind, drawn in
// file order after the background).
struct FElysiumSignTextBlock
{
	FString Text;
	int32   XPos = 50;             // CSignUI defaults, from FUN_10061cc0
	int32   YPos = 50;
	int32   Wide = 200;
	int32   Tall = 200;
	int32   Columns = 1;           // divides Wide (`Wide /= Columns`)
	FLinearColor TextColor = FLinearColor(0, 0, 0, 0);          // TextRGBA       default [0,0,0,0]
	FLinearColor BackgroundColor = FLinearColor(1, 1, 1, 0);    // BackgroundRGBA default [255,255,255,0]
	FString Alignment;             // `center` (82 uses) / `southeast` (1); empty = default
	FString FontDefault;                    // the plain `Font` key
	TMap<int32, FString> FontByWidth;       // Font_640 / _800 / _1024 / _1280 / _1600

	// CSignUI picks by *exact* screen-width match, else the plain `Font`, else "Default".
	FString ResolveFont(int32 ScreenWidth) const;
};

// A parsed `vdata/Signs/<name>.txt`.
struct FElysiumSignData
{
	bool    bParsed = false;
	FString SourceFile;            // the resolved leaf name (after any Sign{} redirect)
	bool    bHideHUD = false;      // `HideHUD` at SignData scope

	// The `Rules` sub-block (FUN_100617b0).
	FString ClientCommand;         // capped at 128 chars by retail; not executed (out of scope)
	// The retail default is the panel's constructed value, not a literal in the parse. Only 4
	// files set the key, yet every tutorial popup instructs "left-click to continue" without
	// it — so the constructed default is true.
	bool    bCloseOnLeftClick = true;
	float   MinShowTime = 0.0f;

	FElysiumSignBackground Background;
	TArray<FElysiumSignTextBlock> Blocks;

	// Parse a definition by its `definition_file` keyvalue (e.g. "vdata/Signs/foo.txt" — the
	// directory prefix and case are ignored; the mirror is flat + lowercased under out/signs).
	// `World` is optional: when set, a `Sign { dependency; filename }` dispatch wrapper resolves
	// its first truthy `dependency` through the installed script host and re-enters that file.
	static bool Load(const FString& DefinitionFile, FElysiumSignData& Out, FElysiumEntityWorld* World);

	// Absolute path of a definition leaf name under the content mirror.
	static FString ResolvePath(const FString& DefinitionFile);
};
