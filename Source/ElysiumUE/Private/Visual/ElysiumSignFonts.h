#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "UObject/StrongObjectPtr.h"

class UFont;

// The sign/popup typeface set. VtMB's `game_sign` panels author face names
// against client.dll's trackerscheme.res (ParagraphText, Newsprint, Trebuchet, Headline,
// Vamp_Handwriting1, MainMenu, Copperplate, Tahoma; "Default" otherwise — see
// FElysiumSignTextBlock::ResolveFont). The UI has no classic mode, so those names re-skin onto a
// small committed set of OFL vector faces under Content/Fonts instead of the 640x480 .fnt atlases:
//
//   ParagraphText / Trebuchet / Tahoma / Default -> IBM Plex Sans   (body + UI)
//   Newsprint                                     -> Zilla Slab       (editorial)
//   Headline / Copperplate                        -> Zilla Slab SemiBold
//   Vamp_Handwriting1                             -> Caveat           (handwritten notes)
//   MainMenu                                      -> Pirata One       (gothic masthead)
//
// Each TTF is loaded once into a runtime-cached UFont (kept alive here) and handed out as a
// FSlateFontInfo sized in VtMB's 768-tall virtual canvas, so type is resolution-independent (the
// charter's Slate stack). A missing file falls back to Slate's default (Roboto) at the same size.
class FElysiumSignFontLibrary
{
public:
	// Resolve an authored face name (case-insensitive) to a drawable font at the panel's current
	// vertical scale. ScreenH is the viewport height in pixels; the returned info's size is the
	// face's virtual-canvas size scaled by ElysiumSign::ScaleFor(ScreenH).
	FSlateFontInfo ResolveFace(const FString& FaceName, float ScreenH);

private:
	// Lazy-load a Content/Fonts TTF into a runtime UFont; caches success and failure (null) alike so
	// a missing file is not retried every frame.
	UFont* LoadRuntimeFont(const TCHAR* FileName);

	TMap<FString, TStrongObjectPtr<UFont>> Loaded;   // filename -> runtime font (null = load failed)
};
