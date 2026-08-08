#include "Debug/ElysiumChannelRecorder.h"

#if !UE_BUILD_SHIPPING

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	// A value's text is a deterministic function of the value and the channel's own precision —
	// which is what a committed baseline needs. The one thing floating point can do to break that
	// is hand back a negative zero, which prints with a sign and compares equal to the one without;
	// a value that rounds to all-zeros loses its sign here rather than in the diff.
	FString FormatValue(double Value, int8 Precision)
	{
		if (!FMath::IsFinite(Value))
		{
			return TEXT("nan");
		}
		FString Text = FString::Printf(TEXT("%.*f"), static_cast<int32>(Precision), Value);
		if (Text.StartsWith(TEXT("-")))
		{
			bool bAllZero = true;
			for (TCHAR C : Text)
			{
				bAllZero &= !(C >= TEXT('1') && C <= TEXT('9'));
			}
			if (bAllZero)
			{
				Text.RemoveAt(0);
			}
		}
		return Text;
	}

	FString JsonEscape(const FString& In)
	{
		FString Out;
		Out.Reserve(In.Len() + 8);
		for (TCHAR C : In)
		{
			switch (C)
			{
			case TEXT('"'):  Out += TEXT("\\\""); break;
			case TEXT('\\'): Out += TEXT("\\\\"); break;
			case TEXT('\n'): Out += TEXT("\\n");  break;
			case TEXT('\r'): Out += TEXT("\\r");  break;
			case TEXT('\t'): Out += TEXT("\\t");  break;
			default:         Out.AppendChar(C);   break;
			}
		}
		return Out;
	}

	void AppendNumberObject(FString& Out, const TCHAR* Key,
		const TArray<TPair<FString, double>>& Rows, const TCHAR* Indent)
	{
		Out += FString::Printf(TEXT("%s\"%s\": {"), Indent, Key);
		for (int32 i = 0; i < Rows.Num(); ++i)
		{
			Out += FString::Printf(TEXT("%s\n%s  \"%s\": %s"),
				i > 0 ? TEXT(",") : TEXT(""), Indent,
				*JsonEscape(Rows[i].Key), *FormatValue(Rows[i].Value, 6));
		}
		Out += Rows.Num() > 0 ? FString::Printf(TEXT("\n%s}"), Indent) : FString(TEXT("}"));
	}
}

void FElysiumChannelRecorder::Reset()
{
	Columns.Reset();
	Values.Reset();
	Written.Reset();
	FrameCount_ = 0;
	bFrameOpen = false;
	RunValues.Reset();
	MetaStrings.Reset();
	MetaNumbers.Reset();
	Constants.Reset();
	Overrides.Reset();
	DeferredErrors.Reset();
}

bool FElysiumChannelRecorder::Open(TArrayView<const TCHAR* const> FrameChannels, FString& OutError)
{
	Reset();

	for (const TCHAR* Name : FrameChannels)
	{
		const ElysiumChannels::FChannelDef* Def = ElysiumChannels::Find(Name);
		if (!Def)
		{
			OutError = FString::Printf(
				TEXT("channel '%s' is not in ElysiumChannels::Defs() — declare it there, with its ")
				TEXT("comparison rule, before writing it"), Name ? Name : TEXT("<null>"));
			Columns.Reset();
			return false;
		}
		if (Def->Scope != ElysiumChannels::EScope::Frame)
		{
			OutError = FString::Printf(TEXT("channel '%s' is run-scoped; it cannot be a CSV column"),
				Name);
			Columns.Reset();
			return false;
		}
		if (Columns.ContainsByPredicate(
			[Def](const ElysiumChannels::FChannelDef* C) { return C == Def; }))
		{
			OutError = FString::Printf(TEXT("channel '%s' is declared twice"), Name);
			Columns.Reset();
			return false;
		}
		Columns.Add(Def);
	}

	if (Columns.Num() == 0)
	{
		OutError = TEXT("a run must declare at least one frame channel");
		return false;
	}
	Written.Init(false, Columns.Num());
	return true;
}

int32 FElysiumChannelRecorder::IndexOf(const TCHAR* Name) const
{
	for (int32 i = 0; i < Columns.Num(); ++i)
	{
		if (FCString::Strcmp(Columns[i]->Name, Name) == 0)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

void FElysiumChannelRecorder::BeginFrame()
{
	if (bFrameOpen)
	{
		// A frame opened and never closed is a frame that failed; drop its half-written row rather
		// than letting the next one write past it.
		Values.SetNum(FrameCount_ * Columns.Num());
	}
	Written.Init(false, Columns.Num());
	Values.AddZeroed(Columns.Num());
	bFrameOpen = true;
}

void FElysiumChannelRecorder::Set(const TCHAR* Name, double Value)
{
	if (!bFrameOpen)
	{
		return;
	}
	const int32 Column = IndexOf(Name);
	if (Column == INDEX_NONE)
	{
		DeferredErrors.AddUnique(FString::Printf(
			TEXT("frame channel '%s' was written but not declared"), Name ? Name : TEXT("<null>")));
		return;
	}
	Values[FrameCount_ * Columns.Num() + Column] = Value;
	Written[Column] = true;
}

bool FElysiumChannelRecorder::EndFrame(FString& OutError)
{
	if (!bFrameOpen)
	{
		OutError = TEXT("EndFrame without BeginFrame");
		return false;
	}
	for (int32 i = 0; i < Columns.Num(); ++i)
	{
		if (!Written[i])
		{
			// Left as a zero this would be indistinguishable from a measured zero once written.
			OutError = FString::Printf(TEXT("frame %d never wrote channel '%s'"),
				FrameCount_, Columns[i]->Name);
			Values.SetNum(FrameCount_ * Columns.Num());
			bFrameOpen = false;
			return false;
		}
	}
	++FrameCount_;
	bFrameOpen = false;
	return true;
}

void FElysiumChannelRecorder::SetRun(const TCHAR* Name, double Value)
{
	const ElysiumChannels::FChannelDef* Def = ElysiumChannels::Find(Name);
	if (!Def || Def->Scope != ElysiumChannels::EScope::Run)
	{
		DeferredErrors.AddUnique(FString::Printf(
			TEXT("run channel '%s' is not declared as run-scoped in ElysiumChannels::Defs()"),
			Name ? Name : TEXT("<null>")));
		return;
	}
	for (TPair<const ElysiumChannels::FChannelDef*, double>& Row : RunValues)
	{
		if (Row.Key == Def)
		{
			Row.Value = Value;
			return;
		}
	}
	RunValues.Add({ Def, Value });
}

void FElysiumChannelRecorder::SetMeta(const TCHAR* Key, const FString& Value)
{
	MetaStrings.Add({ Key, Value });
}

void FElysiumChannelRecorder::SetMetaNumber(const TCHAR* Key, double Value)
{
	MetaNumbers.Add({ Key, Value });
}

void FElysiumChannelRecorder::SetConstant(const TCHAR* Name, double Value)
{
	Constants.Add({ Name, Value });
}

void FElysiumChannelRecorder::SetOverride(const TCHAR* Name, double Value)
{
	Overrides.Add({ Name, Value });
}

void FElysiumChannelRecorder::Serialize(FString& OutCsv, FString& OutManifest) const
{
	// --- The CSV ------------------------------------------------------------------------------
	{
		TArray<FString> Names;
		Names.Reserve(Columns.Num());
		for (const ElysiumChannels::FChannelDef* Def : Columns)
		{
			Names.Add(Def->Name);
		}
		TArray<FString> Lines;
		Lines.Reserve(FrameCount_ + 1);
		Lines.Add(FString::Join(Names, TEXT(",")));

		TArray<FString> Cells;
		Cells.SetNum(Columns.Num());
		for (int32 Row = 0; Row < FrameCount_; ++Row)
		{
			for (int32 Col = 0; Col < Columns.Num(); ++Col)
			{
				Cells[Col] = FormatValue(Values[Row * Columns.Num() + Col], Columns[Col]->Precision);
			}
			Lines.Add(FString::Join(Cells, TEXT(",")));
		}
		OutCsv = FString::Join(Lines, TEXT("\n")) + TEXT("\n");
	}

	// --- The manifest -------------------------------------------------------------------------
	FString J;
	J += TEXT("{\n");
	J += TEXT("  \"schema\": 1,\n");

	J += TEXT("  \"run\": {");
	{
		bool bFirst = true;
		for (const TPair<FString, FString>& Row : MetaStrings)
		{
			J += FString::Printf(TEXT("%s\n    \"%s\": \"%s\""), bFirst ? TEXT("") : TEXT(","),
				*JsonEscape(Row.Key), *JsonEscape(Row.Value));
			bFirst = false;
		}
		for (const TPair<FString, double>& Row : MetaNumbers)
		{
			J += FString::Printf(TEXT("%s\n    \"%s\": %s"), bFirst ? TEXT("") : TEXT(","),
				*JsonEscape(Row.Key), *FormatValue(Row.Value, 6));
			bFirst = false;
		}
		J += bFirst ? TEXT("") : TEXT(",\n");
		if (bFirst)
		{
			J += TEXT("\n");
		}
		AppendNumberObject(J, TEXT("constants"), Constants, TEXT("    "));
		J += TEXT(",\n");
		AppendNumberObject(J, TEXT("tuningOverrides"), Overrides, TEXT("    "));
		J += TEXT("\n  },\n");
	}

	// Every declared channel, with the rule the differ must apply to it. Frame channels first, in
	// column order, then the run channels with their measured values.
	J += TEXT("  \"channels\": [");
	{
		auto AppendChannel = [&J](const ElysiumChannels::FChannelDef* Def, bool bFirst,
			bool bHasValue, double Value)
		{
			J += FString::Printf(TEXT("%s\n    { \"name\": \"%s\", \"producer\": \"%s\", ")
				TEXT("\"scope\": \"%s\", \"kind\": \"%s\", \"tolerance\": %s, ")
				TEXT("\"precision\": %d, \"unit\": \"%s\", \"speedDependent\": %s, ")
				TEXT("\"help\": \"%s\""),
				bFirst ? TEXT("") : TEXT(","),
				Def->Name, Def->Producer,
				ElysiumChannels::ScopeName(Def->Scope), ElysiumChannels::KindName(Def->Kind),
				*FormatValue(Def->Tolerance, 6), static_cast<int32>(Def->Precision), Def->Unit,
				Def->bSpeedDependent ? TEXT("true") : TEXT("false"),
				*JsonEscape(Def->Help));
			if (bHasValue)
			{
				J += FString::Printf(TEXT(", \"value\": %s"), *FormatValue(Value, Def->Precision));
			}
			J += TEXT(" }");
		};

		bool bFirst = true;
		for (const ElysiumChannels::FChannelDef* Def : Columns)
		{
			AppendChannel(Def, bFirst, /*bHasValue*/ false, 0.0);
			bFirst = false;
		}
		for (const TPair<const ElysiumChannels::FChannelDef*, double>& Row : RunValues)
		{
			AppendChannel(Row.Key, bFirst, /*bHasValue*/ true, Row.Value);
			bFirst = false;
		}
		J += bFirst ? TEXT("]") : TEXT("\n  ]");
	}

	if (DeferredErrors.Num() > 0)
	{
		J += TEXT(",\n  \"errors\": [");
		for (int32 i = 0; i < DeferredErrors.Num(); ++i)
		{
			J += FString::Printf(TEXT("%s\n    \"%s\""), i > 0 ? TEXT(",") : TEXT(""),
				*JsonEscape(DeferredErrors[i]));
		}
		J += TEXT("\n  ]");
	}
	J += TEXT("\n}\n");
	OutManifest = J;
}

bool FElysiumChannelRecorder::Write(const FString& Dir, const FString& Stem, FString& OutError) const
{
	FString Csv;
	FString Manifest;
	Serialize(Csv, Manifest);

	const FString CsvPath = FPaths::Combine(Dir, Stem + TEXT(".csv"));
	const FString ManifestPath = FPaths::Combine(Dir, Stem + TEXT(".channels.json"));
	if (!FFileHelper::SaveStringToFile(Csv, *CsvPath)
		|| !FFileHelper::SaveStringToFile(Manifest, *ManifestPath))
	{
		OutError = FString::Printf(TEXT("could not write '%s'"), *CsvPath);
		return false;
	}
	return true;
}

#endif // !UE_BUILD_SHIPPING
