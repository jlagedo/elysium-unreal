#include "Visual/ElysiumSignFonts.h"

#include "ElysiumContentPaths.h"
#include "Substrate/ElysiumSignData.h"

#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Fonts/CompositeFont.h"
#include "Misc/FileHelper.h"
#include "Styling/CoreStyle.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumFont, Log, All);

namespace
{
	// One committed OFL face per authored VtMB name. Size is in the 768-tall virtual canvas and is
	// scaled to pixels by ElysiumSign::ScaleFor(ScreenH) at draw time (resolution-independent). These
	// mirror trackerscheme.res's intent — body vs. editorial vs. display — not its exact pixel sizes;
	// they are the presentation layer and tunable by owner call.
	struct FFaceSpec
	{
		const TCHAR* File;
		float        VirtualSize;
	};

	// Case-folded authored face name -> spec. VtMB's live sign set uses exactly the eight names below
	// (plus the "Default" fallback); an unknown name resolves to the body face.
	FFaceSpec SpecForFace(const FString& FaceName)
	{
		static const TCHAR* IBMPlexSans = TEXT("IBMPlexSans-VF.ttf");        // body + UI sans
		static const TCHAR* ZillaSlab   = TEXT("ZillaSlab-Regular.ttf");     // editorial slab
		static const TCHAR* ZillaSemi   = TEXT("ZillaSlab-SemiBold.ttf");    // headlines
		static const TCHAR* Caveat      = TEXT("Caveat-Regular.ttf");        // handwriting
		static const TCHAR* Pirata      = TEXT("PirataOne-Regular.ttf");     // gothic masthead

		const FString Face = FaceName.ToLower();
		if (Face == TEXT("newsprint"))          { return { ZillaSlab, 15.f }; }
		if (Face == TEXT("headline"))           { return { ZillaSemi, 30.f }; }
		if (Face == TEXT("copperplate"))        { return { ZillaSemi, 20.f }; }
		if (Face == TEXT("vamp_handwriting1"))  { return { Caveat,    22.f }; }
		if (Face == TEXT("mainmenu"))           { return { Pirata,    34.f }; }
		if (Face == TEXT("tahoma"))             { return { IBMPlexSans, 13.f }; }
		// ParagraphText, Trebuchet, Default, and any unrecognised face -> the body sans.
		return { IBMPlexSans, 15.f };
	}
}

UFont* FElysiumSignFontLibrary::LoadRuntimeFont(const TCHAR* FileName)
{
	if (const TStrongObjectPtr<UFont>* Found = Loaded.Find(FileName))
	{
		return Found->Get();   // cached hit — may be null (a prior load failure)
	}
	// Reserve the slot up front so a missing file caches as a failure and is not retried each frame.
	TStrongObjectPtr<UFont>& Slot = Loaded.Add(FileName);

	const FString Path = FElysiumContentPaths::FontFile(FileName);
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path))
	{
		UE_LOG(LogElysiumFont, Warning, TEXT("sign font missing, falling back to Roboto: %s"), *Path);
		return nullptr;
	}

	// Embed the TTF payload in a runtime font face (Inline = no file-IO once loaded), then wrap it in
	// a runtime-cached UFont whose single-entry composite Slate can rasterise through FreeType.
	UFontFace* Face = NewObject<UFontFace>(GetTransientPackage());
	Face->LoadingPolicy = EFontLoadingPolicy::Inline;
	Face->FontFaceData = FFontFaceData::MakeFontFaceData(MoveTemp(Bytes));

	UFont* Font = NewObject<UFont>(GetTransientPackage());
	Font->FontCacheType = EFontCacheType::Runtime;
	FTypefaceEntry& Entry = Font->CompositeFont.DefaultTypeface.Fonts.AddDefaulted_GetRef();
	Entry.Name = TEXT("Regular");
	Entry.Font = FFontData(Face);   // the composite's UPROPERTY graph keeps the face alive with the font

	Slot.Reset(Font);
	return Font;
}

FSlateFontInfo FElysiumSignFontLibrary::ResolveFace(const FString& FaceName, float ScreenH)
{
	const FFaceSpec Spec = SpecForFace(FaceName);
	const float Size = FMath::Max(1.f, float(Spec.VirtualSize * ElysiumSign::ScaleFor(ScreenH)));

	if (UFont* Font = LoadRuntimeFont(Spec.File))
	{
		return FSlateFontInfo(Font, Size);
	}
	// The TTF was absent: keep the layout working with the engine's default Slate face at the same
	// size (this still measures/draws through the same FreeType path, unlike an offline UFont).
	return FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), Size);
}
