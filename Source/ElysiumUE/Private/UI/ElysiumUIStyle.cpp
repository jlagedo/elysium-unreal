#include "UI/ElysiumUIStyle.h"

#include "ElysiumContentPaths.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Fonts/CompositeFont.h"
#include "Styling/CoreStyle.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumUIStyle, Log, All);

namespace
{
	// The generated local face assets, by role. `pipeline/unreal/make_ui_fonts.py` imports these from
	// Content/Fonts; the names are FF_<Family>_<Weight> by construction, so the path is derivable.
	struct FRoleFaces
	{
		const TCHAR* Regular;
		const TCHAR* SemiBold;
		const TCHAR* Italic;   // null where the role ships no italic
	};

	const FRoleFaces& FacesForRole(EElysiumFontRole Role)
	{
		// Backing storage for the TCHAR* faces below: FElysiumContentPaths::UiFontFace builds
		// each path at runtime, so the pointers below must outlive this call rather than point into
		// a temporary, hence the static FString locals rather than a direct FRoleFaces initializer.
		static const FString LabelRegular = FElysiumContentPaths::UiFontFace(TEXT("FF_SpectralSC_Regular"));
		static const FString LabelSemiBold = FElysiumContentPaths::UiFontFace(TEXT("FF_SpectralSC_SemiBold"));
		static const FString BodyRegular = FElysiumContentPaths::UiFontFace(TEXT("FF_Spectral_Regular"));
		static const FString BodySemiBold = FElysiumContentPaths::UiFontFace(TEXT("FF_Spectral_SemiBold"));
		static const FString BodyItalic = FElysiumContentPaths::UiFontFace(TEXT("FF_Spectral_Italic"));
		static const FString DataRegular = FElysiumContentPaths::UiFontFace(TEXT("FF_Inter_Regular"));
		static const FString DataSemiBold = FElysiumContentPaths::UiFontFace(TEXT("FF_Inter_SemiBold"));
		// Terminus ships Regular and Bold and no italic. Bold is registered under the `SemiBold`
		// entry name because that is the library's one heavier-weight slot; the terminal draws
		// Regular and the name is an internal key, not a weight claim.
		static const FString MonoRegular = FElysiumContentPaths::UiFontFace(TEXT("FF_TerminusTTF_Regular"));
		static const FString MonoBold = FElysiumContentPaths::UiFontFace(TEXT("FF_TerminusTTF_Bold"));

		static const FRoleFaces LabelFaces{ *LabelRegular, *LabelSemiBold, nullptr };
		static const FRoleFaces BodyFaces{ *BodyRegular, *BodySemiBold, *BodyItalic };
		static const FRoleFaces DataFaces{ *DataRegular, *DataSemiBold, nullptr };
		static const FRoleFaces MonoFaces{ *MonoRegular, *MonoBold, nullptr };

		switch (Role)
		{
		case EElysiumFontRole::Label: return LabelFaces;
		case EElysiumFontRole::Data:  return DataFaces;
		case EElysiumFontRole::Mono:  return MonoFaces;
		default:                      return BodyFaces;
		}
	}

	// Slate names a composite font's weights; these are the typeface entry names we register and
	// then ask for by name in FSlateFontInfo.
	FName WeightName(EElysiumFontWeight Weight)
	{
		switch (Weight)
		{
		case EElysiumFontWeight::SemiBold: return TEXT("SemiBold");
		case EElysiumFontWeight::Italic:   return TEXT("Italic");
		default:                           return TEXT("Regular");
		}
	}

	// Append one typeface entry, or report the miss. Returns false if the asset is absent.
	bool AddFace(UFont& Font, const TCHAR* AssetPath, FName EntryName)
	{
		if (!AssetPath)
		{
			return true;   // the role legitimately has no such weight
		}
		UFontFace* Face = LoadObject<UFontFace>(nullptr, AssetPath);
		if (!Face)
		{
			UE_LOG(LogElysiumUIStyle, Warning,
				TEXT("UI font face missing: %s — run pipeline/unreal/make_ui_fonts.py in the editor"), AssetPath);
			return false;
		}
		FTypefaceEntry& Entry =
			Font.GetMutableInternalCompositeFont().DefaultTypeface.Fonts.AddDefaulted_GetRef();
		Entry.Name = EntryName;
		// FFontData(const UObject*) takes the face asset itself, so the composite's UPROPERTY graph
		// keeps it alive with the font and Slate rasterises straight out of the cooked asset.
		Entry.Font = FFontData(Face);
		return true;
	}
}

namespace ElysiumUI
{
	const FElysiumTerminalPalette& TerminalPalette(int32 ColorScheme)
	{
		// One record per authored `colorscheme`, in the order retail's four records sit in at
		// `0x10233378`. Built once: the terminal draw asks for a palette per painted frame per
		// monitor, and these are literals with no dependency on anything.
		static const FElysiumTerminalPalette Schemes[4] =
		{
			// 0 — amber. The default `colorscheme 0`, and the one `tuthack` runs on.
			FElysiumTerminalPalette{
				FLinearColor::FromSRGBColor(FColor(10, 6, 2)),
				FLinearColor::FromSRGBColor(FColor(255, 176, 60)),
				FLinearColor::FromSRGBColor(FColor(255, 214, 150)),
				FLinearColor::FromSRGBColor(FColor(255, 176, 60)),
				FLinearColor::FromSRGBColor(FColor(10, 6, 2)) },
			// 1 — green (P1 phosphor).
			FElysiumTerminalPalette{
				FLinearColor::FromSRGBColor(FColor(2, 10, 4)),
				FLinearColor::FromSRGBColor(FColor(96, 255, 128)),
				FLinearColor::FromSRGBColor(FColor(190, 255, 205)),
				FLinearColor::FromSRGBColor(FColor(96, 255, 128)),
				FLinearColor::FromSRGBColor(FColor(2, 10, 4)) },
			// 2 — cold white.
			FElysiumTerminalPalette{
				FLinearColor::FromSRGBColor(FColor(6, 7, 9)),
				FLinearColor::FromSRGBColor(FColor(222, 230, 236)),
				FLinearColor::FromSRGBColor(FColor(255, 255, 255)),
				FLinearColor::FromSRGBColor(FColor(222, 230, 236)),
				FLinearColor::FromSRGBColor(FColor(6, 7, 9)) },
			// 3 — cyan.
			FElysiumTerminalPalette{
				FLinearColor::FromSRGBColor(FColor(2, 9, 12)),
				FLinearColor::FromSRGBColor(FColor(86, 229, 255)),
				FLinearColor::FromSRGBColor(FColor(190, 244, 255)),
				FLinearColor::FromSRGBColor(FColor(86, 229, 255)),
				FLinearColor::FromSRGBColor(FColor(2, 9, 12)) },
		};
		return Schemes[FMath::Clamp(ColorScheme, 0, 3)];
	}
}

UFont* FElysiumUIFontLibrary::FontForRole(EElysiumFontRole Role)
{
	const uint8 Key = static_cast<uint8>(Role);
	if (const TStrongObjectPtr<UFont>* Found = Fonts.Find(Key))
	{
		return Found->Get();   // cached; may be null (a prior failure, not retried)
	}
	// Reserve up front so a missing asset caches as a failure rather than reloading every frame.
	TStrongObjectPtr<UFont>& Slot = Fonts.Add(Key);

	const FRoleFaces& Faces = FacesForRole(Role);
	UFont* Font = NewObject<UFont>(GetTransientPackage());
	Font->FontCacheType = EFontCacheType::Runtime;

	bool bOk = AddFace(*Font, Faces.Regular, TEXT("Regular"));
	bOk &= AddFace(*Font, Faces.SemiBold, TEXT("SemiBold"));
	bOk &= AddFace(*Font, Faces.Italic, TEXT("Italic"));

	if (!bOk || Font->GetCompositeFont()->DefaultTypeface.Fonts.Num() == 0)
	{
		bComplete = false;
		return nullptr;   // Slot stays null: the caller falls back to the default Slate face
	}

	Slot.Reset(Font);
	return Font;
}

FSlateFontInfo FElysiumUIFontLibrary::Font(EElysiumFontRole Role, EElysiumFontWeight Weight,
                                          float VirtualSize, float Scale)
{
	const float Size = FMath::Max(1.0f, VirtualSize * Scale);

	if (UFont* RoleFont = FontForRole(Role))
	{
		FName Name = WeightName(Weight);
		// A role that ships no italic (Spectral SC, Inter) resolves italic to its regular weight
		// rather than letting Slate fall through to a synthetic face.
		const TArray<FTypefaceEntry>& Entries = RoleFont->GetCompositeFont()->DefaultTypeface.Fonts;
		const bool bHas = Entries.ContainsByPredicate(
			[&Name](const FTypefaceEntry& E) { return E.Name == Name; });
		if (!bHas)
		{
			Name = TEXT("Regular");
		}
		return FSlateFontInfo(RoleFont, Size, Name);
	}

	// The assets are absent: keep the layout measurable with the engine's default face at the same
	// size. Visibly wrong type is the intended signal — it says "run the font generator".
	return FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), Size);
}

FElysiumUIFontLibrary& ElysiumUIFonts()
{
	static FElysiumUIFontLibrary Library;
	return Library;
}
