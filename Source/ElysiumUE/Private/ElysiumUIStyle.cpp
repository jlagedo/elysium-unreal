#include "ElysiumUIStyle.h"

#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Fonts/CompositeFont.h"
#include "Styling/CoreStyle.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumUIStyle, Log, All);

namespace
{
	// The committed face assets, by role. `tools/make_ui_fonts.py` imports these from
	// Content/Fonts; the names are FF_<Family>_<Weight> by construction, so the path is derivable.
	struct FRoleFaces
	{
		const TCHAR* Regular;
		const TCHAR* SemiBold;
		const TCHAR* Italic;   // null where the role ships no italic
	};

	const FRoleFaces& FacesForRole(EElysiumFontRole Role)
	{
		static const FRoleFaces LabelFaces{
			TEXT("/Game/VtMB/UI/Fonts/FF_SpectralSC_Regular.FF_SpectralSC_Regular"),
			TEXT("/Game/VtMB/UI/Fonts/FF_SpectralSC_SemiBold.FF_SpectralSC_SemiBold"),
			nullptr };
		static const FRoleFaces BodyFaces{
			TEXT("/Game/VtMB/UI/Fonts/FF_Spectral_Regular.FF_Spectral_Regular"),
			TEXT("/Game/VtMB/UI/Fonts/FF_Spectral_SemiBold.FF_Spectral_SemiBold"),
			TEXT("/Game/VtMB/UI/Fonts/FF_Spectral_Italic.FF_Spectral_Italic") };
		static const FRoleFaces DataFaces{
			TEXT("/Game/VtMB/UI/Fonts/FF_Inter_Regular.FF_Inter_Regular"),
			TEXT("/Game/VtMB/UI/Fonts/FF_Inter_SemiBold.FF_Inter_SemiBold"),
			nullptr };

		switch (Role)
		{
		case EElysiumFontRole::Label: return LabelFaces;
		case EElysiumFontRole::Data:  return DataFaces;
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
				TEXT("UI font face missing: %s — run tools/make_ui_fonts.py in the editor"), AssetPath);
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
