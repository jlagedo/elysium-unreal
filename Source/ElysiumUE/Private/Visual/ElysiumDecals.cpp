#include "Visual/ElysiumDecals.h"

#include "Misc/FileHelper.h"

bool FElysiumDecals::Parse(const FString& Path, TArray<FElysiumDecalDef>& Out)
{
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *Path))
	{
		return false;
	}
	ParseLines(Lines, Out);
	return true;
}

void FElysiumDecals::ParseLines(const TArray<FString>& Lines, TArray<FElysiumDecalDef>& Out)
{
	TArray<FString> Tok;
	for (const FString& Line : Lines)
	{
		Tok.Reset();
		Line.ParseIntoArray(Tok, TEXT(" "), true);
		if (Tok.Num() != 15)
		{
			continue;   // material + 14 floats (loc3 + normal3 + s3 + t3 + hw + hh); skip malformed
		}

		auto F = [&Tok](int32 I) { return FCString::Atod(*Tok[I]); };
		FElysiumDecalDef D;
		D.Mat = Tok[0];
		D.Loc = FVector(F(1), F(2), F(3));
		D.Normal = FVector(F(4), F(5), F(6));
		D.SDir = FVector(F(7), F(8), F(9));
		D.TDir = FVector(F(10), F(11), F(12));
		D.HalfW = static_cast<float>(F(13));
		D.HalfH = static_cast<float>(F(14));
		Out.Add(MoveTemp(D));
	}
}
