#include "ElysiumConsole.h"

#include "ElysiumCommands.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumConsole, Log, All);

namespace
{
	// Guard against a cyclic alias (`alias a "b"` / `alias b "a"`); VtMB's console has the same
	// re-entrancy cap. 32 is far past any real alias chain in the shipped cfgs.
	constexpr int32 GMaxAliasDepth = 32;
}

void FElysiumConsole::Tokenize(const FString& Line, TArray<FString>& OutTokens)
{
	OutTokens.Reset();
	FString Cur;
	bool bInQuote = false;
	bool bHaveToken = false;
	for (int32 i = 0; i < Line.Len(); ++i)
	{
		const TCHAR C = Line[i];
		if (C == TEXT('"'))
		{
			bInQuote = !bInQuote;
			bHaveToken = true; // an empty "" is still a token
			continue;
		}
		if (!bInQuote && FChar::IsWhitespace(C))
		{
			if (bHaveToken)
			{
				OutTokens.Add(Cur);
				Cur.Reset();
				bHaveToken = false;
			}
			continue;
		}
		Cur.AppendChar(C);
		bHaveToken = true;
	}
	if (bHaveToken)
	{
		OutTokens.Add(Cur);
	}
}

void FElysiumConsole::SplitStatements(const FString& Line, TArray<FString>& OutParts)
{
	OutParts.Reset();
	FString Cur;
	bool bInQuote = false;
	for (int32 i = 0; i < Line.Len(); ++i)
	{
		const TCHAR C = Line[i];
		if (C == TEXT('"'))
		{
			bInQuote = !bInQuote;
			Cur.AppendChar(C);
			continue;
		}
		if (C == TEXT(';') && !bInQuote)
		{
			OutParts.Add(Cur);
			Cur.Reset();
			continue;
		}
		Cur.AppendChar(C);
	}
	OutParts.Add(Cur);
}

void FElysiumConsole::ParseLine(const FString& RawLine)
{
	FString Line = RawLine;
	Line.TrimStartAndEndInline();
	if (Line.IsEmpty() || Line.StartsWith(TEXT("//")))
	{
		return;
	}
	// Drop a trailing `//` comment that is not inside quotes.
	{
		bool bInQuote = false;
		for (int32 i = 0; i + 1 < Line.Len(); ++i)
		{
			if (Line[i] == TEXT('"')) { bInQuote = !bInQuote; }
			else if (!bInQuote && Line[i] == TEXT('/') && Line[i + 1] == TEXT('/'))
			{
				Line = Line.Left(i);
				Line.TrimEndInline();
				break;
			}
		}
	}
	if (Line.IsEmpty())
	{
		return;
	}

	TArray<FString> Tokens;
	Tokenize(Line, Tokens);
	if (Tokens.Num() == 0)
	{
		return;
	}

	const FString Verb = Tokens[0].ToLower();
	if (Verb == TEXT("alias") && Tokens.Num() >= 3)
	{
		Aliases.Add(Tokens[1].ToLower(), Tokens[2]);
		return;
	}
	// Keybinds and unbind directives carry no alias/cvar state we model -- the runtime input map
	// is Unreal's, not VtMB's console bindings.
	if (Verb == TEXT("bind") || Verb == TEXT("unbind") || Verb == TEXT("unbindall") ||
		Verb == TEXT("exec") || Verb == TEXT("alias"))
	{
		return;
	}
	// `<cvar> "<value>"` -- a two-token setting line.
	if (Tokens.Num() == 2)
	{
		Cvars.Add(Tokens[0].ToLower(), Tokens[1]);
	}
	// Anything else (bare commands, multi-arg engine directives) is not persistent cfg state.
}

void FElysiumConsole::ParseText(const FString& CfgText)
{
	TArray<FString> Lines;
	CfgText.ParseIntoArrayLines(Lines, /*CullEmpty*/ false);
	for (const FString& L : Lines)
	{
		ParseLine(L);
	}
}

void FElysiumConsole::LoadFromCfgDir(const FString& CfgDir)
{
	Aliases.Reset();
	Cvars.Reset();

	// Engine boot order: defaults, then the saved config, then autoexec, then user overrides. Later
	// files shadow earlier ones -- user.cfg wins, which is where the patch's `patchtype` alias lives.
	static const TCHAR* const Order[] = { TEXT("default.cfg"), TEXT("config.cfg"),
										   TEXT("autoexec.cfg"), TEXT("user.cfg") };
	int32 FilesRead = 0;
	for (const TCHAR* Name : Order)
	{
		const FString Path = CfgDir / Name;
		if (!IFileManager::Get().FileExists(*Path))
		{
			continue;
		}
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			UE_LOG(LogElysiumConsole, Warning, TEXT("could not read %s"), *Path);
			continue;
		}
		ParseText(Text);
		++FilesRead;
	}
	bSeeded = true;
	UE_LOG(LogElysiumConsole, Display, TEXT("console cfg: %d file(s) from %s -> %d aliases, %d cvars"),
		FilesRead, *CfgDir, Aliases.Num(), Cvars.Num());
}

void FElysiumConsole::EnsureSeeded(const FString& CfgDir)
{
	if (!bSeeded)
	{
		LoadFromCfgDir(CfgDir);
	}
}

FString FElysiumConsole::GetCvar(const FString& Name) const
{
	const FString* V = Cvars.Find(Name.ToLower());
	return V ? *V : FString();
}

void FElysiumConsole::SetCvar(const FString& Name, const FString& Value)
{
	Cvars.Add(Name.ToLower(), Value);
}

void FElysiumConsole::Execute(const FString& CommandLine)
{
	TArray<FString> Parts;
	SplitStatements(CommandLine, Parts);
	for (const FString& Part : Parts)
	{
		ExecuteStatement(Part, /*Depth*/ 0);
	}
}

void FElysiumConsole::ExecuteStatement(const FString& Statement, int32 Depth)
{
	FString S = Statement;
	S.TrimStartAndEndInline();
	if (S.IsEmpty())
	{
		return;
	}
	if (Depth >= GMaxAliasDepth)
	{
		UE_LOG(LogElysiumConsole, Warning, TEXT("alias recursion too deep at '%s'"), *S);
		return;
	}

	// First whitespace word (outside quotes) is the command; the remainder is its argument string.
	TArray<FString> Tokens;
	Tokenize(S, Tokens);
	if (Tokens.Num() == 0)
	{
		return;
	}
	const FString Name = Tokens[0].ToLower();

	// 1) a registered command (the VtMB bindable-verb inventory, S7). Commands outrank aliases the
	// way Source's own Cmd_ExecuteString does, so nothing a player writes into user.cfg can shadow
	// `+forward`. Execute() reports false for a word that names no verb, which is the cue to go on.
	if (FElysiumCommands::Get().Execute(S))
	{
		return;
	}

	// 2) alias -> expand and run (each expansion may itself be `;`-separated / nest aliases).
	if (const FString* Expansion = Aliases.Find(Name))
	{
		TArray<FString> Parts;
		SplitStatements(*Expansion, Parts);
		for (const FString& Part : Parts)
		{
			ExecuteStatement(Part, Depth + 1);
		}
		return;
	}

	// The argument string = everything after the first token (recover it from the raw statement so
	// quoting/spacing is preserved for a cvar value).
	FString Args;
	{
		int32 FirstTokEnd = 0;
		while (FirstTokEnd < S.Len() && !FChar::IsWhitespace(S[FirstTokEnd])) { ++FirstTokEnd; }
		Args = S.Mid(FirstTokEnd);
		Args.TrimStartAndEndInline();
		Args = Args.TrimQuotes();
	}

	// 3) known cvar -> a value sets it; no value is a read (retail prints it; we no-op).
	if (Cvars.Contains(Name))
	{
		if (!Args.IsEmpty())
		{
			Cvars.Add(Name, Args);
		}
		return;
	}

	// 4) fall through to Python. If the sink reports it was NOT Python (NameError/SyntaxError), the
	// word is an engine cvar/command we do not model -- drop it with a Verbose note, matching the
	// retail effect (nothing happens) without a scary traceback.
	if (PythonSink && PythonSink(S))
	{
		return;
	}
	UE_LOG(LogElysiumConsole, Verbose, TEXT("unhandled console command '%s' (engine cvar/command not modelled)"), *S);
}
