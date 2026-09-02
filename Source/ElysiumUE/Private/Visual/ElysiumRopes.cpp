#include "Visual/ElysiumRopes.h"

#include "Misc/FileHelper.h"

bool FElysiumRopes::Parse(const FString& Path, TArray<FElysiumRopeDef>& Out)
{
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *Path))
	{
		return false;
	}
	ParseLines(Lines, Out);
	return true;
}

void FElysiumRopes::ParseLines(const TArray<FString>& Lines, TArray<FElysiumRopeDef>& Out)
{
	TArray<FString> Tok;
	for (const FString& Line : Lines)
	{
		Tok.Reset();
		Line.ParseIntoArray(Tok, TEXT(" "), true);
		if (Tok.Num() != 12)
		{
			// material id + 11 numbers (A3 + B3 + width + rest + nodes + texscale + flags);
			// skip malformed.
			continue;
		}
		if (!Tok[0].StartsWith(TEXT("vtmb:material:")))
		{
			// A pre-R6.5 line (a decoded texture path) names no unit; the sidecar is stale.
			continue;
		}

		auto F = [&Tok](int32 I) { return FCString::Atod(*Tok[I]); };
		FElysiumRopeDef D;
		D.MaterialId = Tok[0];
		D.A = FVector(F(1), F(2), F(3));
		D.B = FVector(F(4), F(5), F(6));
		D.WidthCm = static_cast<float>(F(7));
		D.RestCm = static_cast<float>(F(8));
		D.Nodes = FMath::Clamp(FCString::Atoi(*Tok[9]), 2, 10);   // Activate()'s [2, 10] clamp
		D.TexScale = static_cast<float>(F(10));
		D.Flags = static_cast<uint8>(FCString::Atoi(*Tok[11]));
		Out.Add(MoveTemp(D));
	}
}
