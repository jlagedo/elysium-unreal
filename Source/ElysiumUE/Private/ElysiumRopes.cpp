#include "ElysiumRopes.h"

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
		if (Tok.Num() != 14)
		{
			// tex + 11 numbers (A3 + B3 + width + rest + nodes + texscale + flags) + bump + matflags;
			// skip malformed.
			continue;
		}

		auto F = [&Tok](int32 I) { return FCString::Atod(*Tok[I]); };
		FElysiumRopeDef D;
		D.Tex = Tok[0];   // "-" means no decoded texture
		D.A = FVector(F(1), F(2), F(3));
		D.B = FVector(F(4), F(5), F(6));
		D.WidthCm = static_cast<float>(F(7));
		D.RestCm = static_cast<float>(F(8));
		D.Nodes = FMath::Clamp(FCString::Atoi(*Tok[9]), 2, 10);   // Activate()'s [2, 10] clamp
		D.TexScale = static_cast<float>(F(10));
		D.Flags = static_cast<uint8>(FCString::Atoi(*Tok[11]));
		D.Bump = Tok[12];   // "-" means no normal map
		D.MatFlags = static_cast<uint8>(FCString::Atoi(*Tok[13]));
		Out.Add(MoveTemp(D));
	}
}
