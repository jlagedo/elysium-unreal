#pragma once

#include "CoreMinimal.h"

#include "UObject/StrongObjectPtr.h"

class SWidget;
class UTexture2D;
struct FMargin;
struct FSlateBrush;

// A cache over the decoded UI art under `out/ui/art/`: one cache rather than a member per image,
// because a screen draws a dozen pieces and they all want the same load-once-guard-everywhere
// treatment. Textures are keyed by file and held for the cache's lifetime; brushes are keyed by
// variant, so one texture can back several (a sub-rectangle, a different tint), and are dropped
// per widget tree through `ResetBrushes`. `out/ui/art/` is gitignored, so every image degrades to
// a caller-drawn token equivalent rather than leaving a hole.
class FElysiumUiArtCache
{
public:
	FElysiumUiArtCache();
	~FElysiumUiArtCache();
	FElysiumUiArtCache(const FElysiumUiArtCache&) = delete;
	FElysiumUiArtCache& operator=(const FElysiumUiArtCache&) = delete;

	// Returns null when the file is absent, which every caller handles by drawing the token
	// version instead. `Uv` selects a sub-rectangle of the page — every one of these textures is a
	// power-of-two page with the art in one corner, and several carry two usable pieces (a
	// divider's two curled ends). The whole page is the default.
	const FSlateBrush* Art(const TCHAR* RelPath, const FLinearColor& Tint,
	                       const FBox2f& Uv = FBox2f(FVector2f::ZeroVector, FVector2f::UnitVector),
	                       const TCHAR* Variant = nullptr);

	// A framed panel out of one of the decoded window textures, 9-sliced so the corner scrolls do
	// not stretch. Falls back to a hairline border.
	TSharedRef<SWidget> Framed(const TCHAR* RelPath, const FBox2f& Uv, const FMargin& Slice,
	                           TSharedRef<SWidget> Content);

	// Drops the brushes; the textures and the miss cache stay. Called per widget tree rebuild and
	// when the owning widget releases its Slate resources.
	void ResetBrushes();

private:
	// Keyed by file. The strong pointer holds each loaded texture against GC, since the cache
	// lives outside any `UPROPERTY`.
	TMap<FString, TStrongObjectPtr<UTexture2D>> ArtTextures;

	TMap<FString, TSharedPtr<FSlateBrush>> ArtBrushes;
	// A miss is cached so a rebuild does not re-hit the disk for a file that is not there.
	TSet<FString> ArtMissing;
};
