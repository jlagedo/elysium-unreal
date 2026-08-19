#include "UI/ElysiumUiArtCache.h"

#include "ElysiumContentPaths.h"
#include "UI/ElysiumUIStyle.h"
#include "UI/ElysiumUITexture.h"

#include "Engine/Texture2D.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "Widgets/Layout/SBorder.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumUiArt, Log, All);

FElysiumUiArtCache::FElysiumUiArtCache() = default;
FElysiumUiArtCache::~FElysiumUiArtCache() = default;

const FSlateBrush* FElysiumUiArtCache::Art(const TCHAR* RelPath, const FLinearColor& Tint,
                                           const FBox2f& Uv, const TCHAR* Variant)
{
	const FString Key = RelPath;
	if (ArtMissing.Contains(Key))
	{
		return nullptr;
	}
	// One texture can back several brushes (a sub-rectangle, a different tint), so the brush cache
	// is keyed by variant while the texture cache is keyed by file.
	const FString BrushKey = Variant ? Key + TEXT("#") + Variant : Key;
	if (const TSharedPtr<FSlateBrush>* Found = ArtBrushes.Find(BrushKey))
	{
		return Found->Get();
	}

	TStrongObjectPtr<UTexture2D>* Cached = ArtTextures.Find(Key);
	if (!Cached)
	{
		const FString Path = FElysiumContentPaths::UiArt(Key);
		UTexture2D* Loaded = ElysiumUI::LoadPngTexture(Path);
		if (!Loaded)
		{
			// Not fatal anywhere: every caller draws the token version instead. Verbose because a
			// clone with no export would otherwise log a dozen warnings per open.
			UE_LOG(LogElysiumUiArt, Verbose,
				TEXT("no sheet art at %s — run: uv run elysium export bundle ui"), *Path);
			ArtMissing.Add(Key);
			return nullptr;
		}
		Cached = &ArtTextures.Add(Key, TStrongObjectPtr<UTexture2D>(Loaded));
	}

	TSharedPtr<FSlateBrush> Brush = MakeShared<FSlateBrush>();
	Brush->SetResourceObject(Cached->Get());
	Brush->DrawAs = ESlateBrushDrawType::Image;
	Brush->ImageSize = FVector2D(1.0f, 1.0f);   // stretched by the box that holds it
	Brush->TintColor = FSlateColor(Tint);
	Brush->SetUVRegion(FBox2f(Uv.Min, Uv.Max));
	ArtBrushes.Add(BrushKey, Brush);
	return Brush.Get();
}

TSharedRef<SWidget> FElysiumUiArtCache::Framed(const TCHAR* RelPath, const FBox2f& Uv,
                                               const FMargin& Slice, TSharedRef<SWidget> Content)
{
	const FString Key = FString(RelPath) + TEXT("#box");
	TSharedPtr<FSlateBrush>* Found = ArtBrushes.Find(Key);
	if (!Found && !ArtMissing.Contains(FString(RelPath)))
	{
		// Reuse the plain loader for the texture, then build the box brush beside it.
		if (Art(RelPath, FLinearColor::White))
		{
			TStrongObjectPtr<UTexture2D>* Tex = ArtTextures.Find(RelPath);
			if (Tex && Tex->IsValid())
			{
				TSharedPtr<FSlateBrush> Box = MakeShared<FSlateBrush>();
				Box->SetResourceObject(Tex->Get());
				Box->DrawAs = ESlateBrushDrawType::Box;
				Box->Margin = Slice;
				Box->SetUVRegion(FBox2f(Uv.Min, Uv.Max));
				Box->ImageSize = FVector2D(1.0f, 1.0f);
				Box->TintColor = FSlateColor(FLinearColor::White);
				Found = &ArtBrushes.Add(Key, Box);
			}
		}
	}

	if (Found && Found->IsValid())
	{
		return SNew(SBorder)
			.BorderImage(Found->Get())
			.Padding(FMargin(ElysiumUI::Space::M, ElysiumUI::Space::M))
			[
				Content
			];
	}

	// No art: a hairline in the frame's own amber, which is the same line the corner scrolls sit on.
	return SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FSlateColor(ElysiumUI::Palette::Amber.CopyWithNewOpacity(0.28f)))
		.Padding(FMargin(1.0f))
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FSlateColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.35f)))
			.Padding(FMargin(ElysiumUI::Space::M))
			[
				Content
			]
		];
}

void FElysiumUiArtCache::ResetBrushes()
{
	ArtBrushes.Reset();
}
