#include "ElysiumObjModel.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	// OBJ face vertex token "a/b/c" (or "a") -> zero-based position index.
	int32 FaceIndex(const FString& Token)
	{
		int32 Slash;
		const FString Num = Token.FindChar('/', Slash) ? Token.Left(Slash) : Token;
		return FCString::Atoi(*Num) - 1;
	}
}

bool FElysiumObjModel::Parse(const FString& ObjPath, FElysiumObjModel& Out)
{
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *ObjPath))
	{
		return false;
	}

	Out.Dir = FPaths::GetPath(ObjPath);
	FString CurMat;
	TArray<FString> Tok;

	for (const FString& Line : Lines)
	{
		if (Line.StartsWith(TEXT("v "), ESearchCase::CaseSensitive))
		{
			Tok.Reset();
			Line.ParseIntoArray(Tok, TEXT(" "), true);
			if (Tok.Num() >= 4)
			{
				const double GX = FCString::Atod(*Tok[1]);
				const double GY = FCString::Atod(*Tok[2]);
				const double GZ = FCString::Atod(*Tok[3]);
				// Godot (metres) -> Unreal (cm): swap Y/Z, scale 100.
				Out.Positions.Add(FVector(GX, GZ, GY) * 100.0);
			}
		}
		else if (Line.StartsWith(TEXT("vt "), ESearchCase::CaseSensitive))
		{
			Tok.Reset();
			Line.ParseIntoArray(Tok, TEXT(" "), true);
			if (Tok.Num() >= 3)
			{
				Out.Uvs.Add(FVector2D(FCString::Atod(*Tok[1]), FCString::Atod(*Tok[2])));
			}
		}
		else if (Line.StartsWith(TEXT("usemtl "), ESearchCase::CaseSensitive))
		{
			CurMat = Line.Mid(7).TrimStartAndEnd();
			Out.Groups.FindOrAdd(CurMat);
		}
		else if (Line.StartsWith(TEXT("f "), ESearchCase::CaseSensitive) && !CurMat.IsEmpty())
		{
			Tok.Reset();
			Line.ParseIntoArray(Tok, TEXT(" "), true);
			if (Tok.Num() >= 4)
			{
				TArray<int32>& Group = Out.Groups.FindChecked(CurMat);
				const int32 I0 = FaceIndex(Tok[1]);
				for (int32 K = 2; K < Tok.Num() - 1; ++K)
				{
					// Reverse winding (i0, k+1, k) to keep faces front-facing after the
					// handedness flip from the Y/Z swap.
					Group.Add(I0);
					Group.Add(FaceIndex(Tok[K + 1]));
					Group.Add(FaceIndex(Tok[K]));
				}
			}
		}
		else if (Line.StartsWith(TEXT("mtllib "), ESearchCase::CaseSensitive))
		{
			Out.MtlName = Line.Mid(7).TrimStartAndEnd();
			ParseMtl(Out.Dir / Out.MtlName, Out.Materials);
		}
	}

	return Out.Positions.Num() > 0;
}

void FElysiumObjModel::ParseMtl(const FString& Path, TMap<FString, FElysiumMaterialDef>& Mats)
{
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *Path))
	{
		return;
	}

	FElysiumMaterialDef* Cur = nullptr;
	TArray<FString> Tok;

	for (const FString& Line : Lines)
	{
		Tok.Reset();
		Line.ParseIntoArray(Tok, TEXT(" "), true);
		if (Tok.Num() == 0)
		{
			continue;
		}
		const FString& Key = Tok[0];

		if (Key == TEXT("newmtl") && Tok.Num() >= 2)
		{
			Cur = &Mats.Add(Tok[1]);
			Cur->Name = Tok[1];
		}
		else if (!Cur)
		{
			continue;
		}
		else if (Key == TEXT("map_Kd") && Tok.Num() >= 2)
		{
			Cur->Albedo = Tok[1];
		}
		else if (Key == TEXT("illum") && Tok.Num() >= 2 && Tok[1] == TEXT("4"))
		{
			Cur->bScissor = true;
		}
		else if (Key == TEXT("blend") && Tok.Num() >= 2 && Tok[1] == TEXT("1"))
		{
			Cur->bBlend = true;
		}
		else if (Key == TEXT("Kd") && Tok.Num() >= 4)
		{
			Cur->Color = FLinearColor(FCString::Atof(*Tok[1]), FCString::Atof(*Tok[2]), FCString::Atof(*Tok[3]));
		}
	}
}
