// P4.10 — SignData parsing + the CSignUI coordinate model. See ElysiumSignData.h for the
// decompile provenance (client.dll CSignUI; $ELYSIUM_WORK_ROOT/research/ghidra/out/signui_*.txt).

#include "Substrate/ElysiumSignData.h"

#include "ElysiumContentPaths.h"
#include "ElysiumEntityHandle.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumKeyValues.h"
#include "ElysiumVariant.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumSign, Log, All);

using ElysiumKeyValues::FKvNode;

namespace
{
	// `[245, 245, 245, 200]` (or bare `245 245 245 200`) -> linear 0..1. Missing components keep
	// the supplied default, so a short list degrades instead of zeroing the colour.
	FLinearColor ParseRGBA(const FString& S, const FLinearColor& Default)
	{
		FString Body = S;
		Body.TrimStartAndEndInline();
		Body.RemoveFromStart(TEXT("["));
		Body.RemoveFromEnd(TEXT("]"));
		Body.ReplaceInline(TEXT(","), TEXT(" "));

		TArray<FString> Parts;
		Body.ParseIntoArrayWS(Parts);
		FLinearColor C = Default;
		float* const Channels[4] = { &C.R, &C.G, &C.B, &C.A };
		for (int32 i = 0; i < 4 && i < Parts.Num(); ++i)
		{
			*Channels[i] = FMath::Clamp(FCString::Atof(*Parts[i]) / 255.0f, 0.0f, 1.0f);
		}
		return C;
	}

	// A definition_file value -> the flat lowercased leaf the mirror stores.
	FString LeafName(const FString& DefinitionFile)
	{
		FString S = DefinitionFile;
		S.ReplaceInline(TEXT("\\"), TEXT("/"));
		S.TrimStartAndEndInline();
		int32 Slash = INDEX_NONE;
		if (S.FindLastChar(TEXT('/'), Slash))
		{
			S = S.Mid(Slash + 1);
		}
		if (!S.EndsWith(TEXT(".txt")))
		{
			S += TEXT(".txt");
		}
		return S.ToLower();
	}

	void ReadTextBlock(const FKvNode& B, FElysiumSignTextBlock& Out)
	{
		Out.Text = B.Str(TEXT("Text"), FString());
		Out.XPos = B.Int(TEXT("XPos"), 50);
		Out.YPos = B.Int(TEXT("YPos"), 50);
		Out.Wide = B.Int(TEXT("Wide"), 200);
		Out.Tall = B.Int(TEXT("Tall"), 200);
		Out.TextColor = ParseRGBA(B.Str(TEXT("TextRGBA"), FString()), FLinearColor(0, 0, 0, 0));
		Out.BackgroundColor = ParseRGBA(B.Str(TEXT("BackgroundRGBA"), FString()), FLinearColor(1, 1, 1, 0));
		Out.Alignment = B.Str(TEXT("Alignment"), FString()).ToLower();
		Out.FontDefault = B.Str(TEXT("Font"), FString());
		for (const int32 W : { 640, 800, 1024, 1280, 1600 })
		{
			const FString Key = FString::Printf(TEXT("Font_%d"), W);
			if (B.Has(*Key))
			{
				Out.FontByWidth.Add(W, B.Str(*Key, FString()));
			}
		}
		// CSignUI only divides when the key is present (an absent Columns leaves Wide alone).
		if (B.Has(TEXT("Columns")))
		{
			Out.Columns = FMath::Max(1, B.Int(TEXT("Columns"), 1));
			Out.Wide /= Out.Columns;
		}
	}

	void ReadBackground(const FKvNode& B, FElysiumSignBackground& Out)
	{
		Out.ImageName = B.Str(TEXT("Name"), FString()).Replace(TEXT("\\"), TEXT("/")).ToLower();
		if (Out.ImageName.IsEmpty())
		{
			return;   // CSignUI bails on an empty Name — no background panel at all
		}
		Out.bValid = true;
		Out.bTiled = B.Bool(TEXT("Tiled"), false);
		Out.XPos = B.Int(TEXT("XPos"), 0);
		Out.YPos = B.Int(TEXT("YPos"), 0);
		Out.bHasWide = B.Has(TEXT("Wide"));
		Out.bHasTall = B.Has(TEXT("Tall"));
		Out.Wide = Out.bHasWide ? B.Int(TEXT("Wide"), 0) : 0;
		Out.Tall = Out.bHasTall ? B.Int(TEXT("Tall"), 0) : 0;
		Out.bCentre = (Out.XPos + Out.YPos) == 0;
	}

	// Fill `Out` from one already-parsed SignData block.
	void ReadSignData(const FKvNode& Root, FElysiumSignData& Out)
	{
		Out.bHideHUD = Root.Bool(TEXT("HideHUD"), false);

		if (const FKvNode* Bg = Root.Child(TEXT("BackgroundImage")))
		{
			ReadBackground(*Bg, Out.Background);
		}
		if (const FKvNode* Rules = Root.Child(TEXT("Rules")))
		{
			Out.ClientCommand = Rules->Str(TEXT("ClientCommand"), FString()).Left(128);
			Out.bCloseOnLeftClick = Rules->Bool(TEXT("CloseOnLeftClick"), Out.bCloseOnLeftClick);
			Out.MinShowTime = Rules->Flt(TEXT("MinShowTime"), 0.0f);
		}
		// TextBlock and Label are the same panel kind to CSignUI, drawn in file order.
		for (const TPair<FString, TSharedPtr<FKvNode>>& Kid : Root.Kids)
		{
			if (!Kid.Value.IsValid() || (Kid.Key != TEXT("textblock") && Kid.Key != TEXT("label")))
			{
				continue;
			}
			FElysiumSignTextBlock Block;
			ReadTextBlock(*Kid.Value, Block);
			Out.Blocks.Add(MoveTemp(Block));
		}
	}
}

// ============================================================================================
// ElysiumSign::RectToScreen — the CSignUI coordinate model
// ============================================================================================

void ElysiumSign::RectToScreen(int32 X, int32 Y, int32 W, int32 H, float ScreenW, float ScreenH,
	bool bCentre, FVector2D& OutPos, FVector2D& OutSize)
{
	// ONE scale for both axes, driven by height. `FUN_100cd100` — which the width math multiplies
	// by 1/1024 — is not the raw backbuffer width but a 4:3-proportional width (ScreenH * 4/3), so
	// `Wide * A / 1024` collapses to `Wide * ScreenH / 768`, the same factor the height uses. The
	// authored 1024x768 canvas therefore keeps its aspect and is letterboxed horizontally rather
	// than stretched. Verified against the retail game: on a 16:9 screen the measured panel width
	// is 1.513x the screen width, where a stretch model predicts 2.0x and this one predicts 1.50x,
	// and the panel/text/dark-box edges then land within a few pixels across five features.
	const double S = double(ScreenH) / double(VirtualHeight);
	const double EffectiveWidth = double(ScreenH) * 4.0 / 3.0;
	const double OffsetX = (double(ScreenW) - EffectiveWidth) * 0.5;

	// __ftol truncates toward zero; mirror it so a rect lands on the same pixel retail picks.
	auto Trunc = [](double V) { return double(int32(V)); };

	OutSize = FVector2D(Trunc(S * W), Trunc(S * H));

	if (bCentre)
	{
		// FUN_10061830's zero-position branch: SAR-halve both the canvas dim and the authored
		// extent (integer, toward zero) before the scale, then subtract. Adding the letterbox
		// offset back makes the X half collapse to the true screen centre.
		OutPos = FVector2D(Trunc(double(int32(ScreenW) / 2) - S * double(W / 2)),
			Trunc(double(int32(ScreenH) / 2) - S * double(H / 2)));
	}
	else
	{
		OutPos = FVector2D(Trunc(OffsetX + S * X), Trunc(S * Y));
	}
}

// ============================================================================================
// FElysiumSignTextBlock::ResolveFont
// ============================================================================================

FString FElysiumSignTextBlock::ResolveFont(int32 ScreenWidth) const
{
	// FUN_10061c40: an *exact* width match on 640/800/1024/1280/1600 selects Font_<w>; anything
	// else (1600x900, 2560x1440, ...) falls through to the plain `Font`, then to "Default".
	if (const FString* Bucket = FontByWidth.Find(ScreenWidth))
	{
		if (!Bucket->IsEmpty())
		{
			return *Bucket;
		}
	}
	return FontDefault.IsEmpty() ? TEXT("Default") : FontDefault;
}

// ============================================================================================
// FElysiumSignData
// ============================================================================================

FString FElysiumSignData::ResolvePath(const FString& DefinitionFile)
{
	return FElysiumContentPaths::SignFile(LeafName(DefinitionFile));
}

bool FElysiumSignData::Load(const FString& DefinitionFile, FElysiumSignData& Out, FElysiumEntityWorld* World)
{
	Out = FElysiumSignData();

	// A `Sign { dependency; filename }` wrapper redirects to another file; retail evaluates each
	// dependency with Py_eval_input and takes the first truthy one (CGameSign::LoadSignData
	// @0x10212da0). Bounded so a cyclic/self-referential wrapper can't spin.
	FString Current = DefinitionFile;
	TSet<FString> Seen;
	for (int32 Hop = 0; Hop < 8; ++Hop)
	{
		const FString Leaf = LeafName(Current);
		if (Seen.Contains(Leaf))
		{
			UE_LOG(LogElysiumSign, Warning, TEXT("sign '%s': dependency redirect cycle at '%s'"),
				*DefinitionFile, *Leaf);
			return false;
		}
		Seen.Add(Leaf);

		const FString AbsPath = FElysiumContentPaths::SignFile(Leaf);
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *AbsPath))
		{
			UE_LOG(LogElysiumSign, Warning, TEXT("sign definition missing: %s"), *AbsPath);
			return false;
		}

		const TSharedPtr<FKvNode> Root = ElysiumKeyValues::ParseText(Text);
		if (!Root.IsValid())
		{
			return false;
		}

		// A dispatch wrapper is a file of `Sign` blocks (24 of the 278 ship this way,
		// newspaper_all.txt being the pattern). Take the first whose dependency evaluates true.
		bool bRedirected = false;
		for (const TPair<FString, TSharedPtr<FKvNode>>& Kid : Root->Kids)
		{
			if (Kid.Key != TEXT("sign") || !Kid.Value.IsValid())
			{
				continue;
			}
			const FString Dep = Kid.Value->Str(TEXT("dependency"), FString());
			const FString File = Kid.Value->Str(TEXT("filename"), FString());
			if (Dep.IsEmpty() || File.IsEmpty())
			{
				continue;
			}
			// EvalCondition goes through the installed script host, so it is Void (falsy) when
			// scripting is off — matching retail's error-to-false on a failed eval.
			const bool bTrue = World && World->EvalCondition(Dep, FElysiumEntityHandle(),
				FElysiumEntityHandle()).ToBool();
			if (bTrue)
			{
				Current = File;
				bRedirected = true;
				break;
			}
		}
		if (bRedirected)
		{
			continue;
		}

		const FKvNode* Data = Root->Child(TEXT("SignData"));
		if (!Data)
		{
			// Some files (the NewspaperData set) root under another name; nothing to draw here yet.
			UE_LOG(LogElysiumSign, Warning, TEXT("sign '%s': no SignData block"), *Leaf);
			return false;
		}

		Out.SourceFile = Leaf;
		ReadSignData(*Data, Out);
		Out.bParsed = true;
		return true;
	}

	UE_LOG(LogElysiumSign, Warning, TEXT("sign '%s': redirect depth exceeded"), *DefinitionFile);
	return false;
}
