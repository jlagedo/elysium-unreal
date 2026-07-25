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
		if (Tok.Num() != 11)
		{
			continue;   // tex + 10 numbers (A3 + B3 + width + slack + subdiv + texscale); skip malformed
		}

		auto F = [&Tok](int32 I) { return FCString::Atod(*Tok[I]); };
		FElysiumRopeDef D;
		D.Tex = Tok[0];   // "-" means no decoded texture
		D.A = FVector(F(1), F(2), F(3));
		D.B = FVector(F(4), F(5), F(6));
		D.WidthCm = static_cast<float>(F(7));
		D.SlackCm = static_cast<float>(F(8));
		D.Subdiv = FCString::Atoi(*Tok[9]);
		D.TexScale = static_cast<float>(F(10));
		Out.Add(MoveTemp(D));
	}
}
