#include "ElysiumDlg.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumDlg, Log, All);

// Parser — 13-field, `}{`-joined, CRLF rows, Latin-1 (`docs/vtmb/game_runtime.md` §5 "Physical format").

namespace
{
	// Strip one wrapping `{ TAB ... TAB }` off a raw field segment: drop a leading `{` / trailing `}`
	// and the tabs/whitespace that pad the content. (Segments come from splitting the row on `}{`, so
	// only the very first keeps its `{` and only the very last keeps its `}`.)
	FString UnwrapField(const FString& Segment)
	{
		FString S = Segment;
		S.TrimStartAndEndInline();
		if (S.StartsWith(TEXT("{")))
		{
			S.RightChopInline(1, EAllowShrinking::No);
		}
		if (S.EndsWith(TEXT("}")))
		{
			S.LeftChopInline(1, EAllowShrinking::No);
		}
		S.TrimStartAndEndInline();   // the inner TABs that frame the content
		return S;
	}

	EElysiumDlgRole RoleFromLink(const FString& Link)
	{
		if (Link == TEXT("#"))
		{
			return EElysiumDlgRole::NpcLine;
		}
		if (Link.IsEmpty())
		{
			return EElysiumDlgRole::Padding;
		}
		return EElysiumDlgRole::PcChoice;   // a number (including "0" = END)
	}
}

FString ElysiumDlgText::StripStageDirections(const FString& Raw)
{
	if (Raw.IsEmpty() || (Raw.Find(TEXT("[")) == INDEX_NONE))
	{
		return Raw;   // fast path — nothing to strip
	}

	// Drop every `[...]` span; an unterminated `[` (no closing `]`) is kept verbatim.
	FString Out;
	Out.Reserve(Raw.Len());
	const int32 N = Raw.Len();
	for (int32 i = 0; i < N; )
	{
		if (Raw[i] == TEXT('['))
		{
			int32 Close = INDEX_NONE;
			for (int32 j = i + 1; j < N; ++j)
			{
				if (Raw[j] == TEXT(']')) { Close = j; break; }
			}
			if (Close != INDEX_NONE)
			{
				i = Close + 1;   // skip the whole `[...]`
				continue;
			}
		}
		Out.AppendChar(Raw[i++]);
	}

	// Removing a direction can leave a doubled space (e.g. "woods. [chuckle]How" -> "woods. How" is fine,
	// but "a [x] b" -> "a  b"): collapse any run of 2+ spaces to one, then trim the ends.
	while (Out.ReplaceInline(TEXT("  "), TEXT(" ")) > 0) {}
	Out.TrimStartAndEndInline();
	return Out;
}

bool FElysiumDlgFile::ParseBytes(const TArray<uint8>& Bytes, FElysiumDlgFile& Out, FString* OutError)
{
	Out = FElysiumDlgFile();
	if (Bytes.Num() == 0)
	{
		if (OutError) { *OutError = TEXT("empty .dlg buffer"); }
		return false;
	}

	// Latin-1: every byte is a code point 0..255. (The corpus is Latin-1; a UTF-8 reader would mangle
	// the 0x80..0xFF punctuation VtMB authored — smart quotes, accents.)
	FString Text;
	Text.Reserve(Bytes.Num());
	for (const uint8 B : Bytes)
	{
		Text.AppendChar(static_cast<TCHAR>(B));
	}

	// Rows are CRLF-terminated; tolerate a bare LF and a missing final terminator.
	TArray<FString> Rows;
	Text.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
	Text.ParseIntoArray(Rows, TEXT("\n"), /*CullEmpty*/ false);

	for (const FString& Row : Rows)
	{
		if (Row.TrimStartAndEnd().IsEmpty())
		{
			continue;   // blank line between records / trailing newline
		}

		// Split the concatenated `{..}{..}` fields. The join is `}{`, so re-glue the braces the split ate
		// back onto each segment is unnecessary — UnwrapField only cares about the outermost pair.
		TArray<FString> Segments;
		Row.ParseIntoArray(Segments, TEXT("}{"), /*CullEmpty*/ false);
		if (Segments.Num() < 13)
		{
			// Not a 13-field record (a stray malformed line) — skip it, do not abort the file.
			UE_LOG(LogElysiumDlg, Verbose, TEXT("%s: skipping row with %d fields"), *Out.SourcePath, Segments.Num());
			continue;
		}

		// >= 13 tolerates the corpus-wide 14-field `kiki.dlg` typo: take the first 13, ignore the rest.
		FElysiumDlgLine Line;
		Line.Id = FCString::Atoi(*UnwrapField(Segments[0]));
		Line.TextMale = UnwrapField(Segments[1]);
		Line.TextFemale = UnwrapField(Segments[2]);
		Line.Link = UnwrapField(Segments[3]);
		Line.Condition = UnwrapField(Segments[4]);
		Line.Action = UnwrapField(Segments[5]);
		Line.TextMalkavian = UnwrapField(Segments[12]);
		Line.Role = RoleFromLink(Line.Link);

		const int32 Index = Out.Lines.Add(MoveTemp(Line));
		Out.IndexById.Add(Out.Lines[Index].Id, Index);   // last-wins on a duplicate id (matches a linear scan)
	}

	if (Out.Lines.Num() == 0)
	{
		if (OutError) { *OutError = TEXT("no 13-field records in .dlg buffer"); }
		return false;
	}
	return true;
}

bool FElysiumDlgFile::LoadFile(const FString& Path, FElysiumDlgFile& Out, FString* OutError)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path))
	{
		if (OutError) { *OutError = FString::Printf(TEXT("cannot read %s"), *Path); }
		return false;
	}
	const bool bOk = ParseBytes(Bytes, Out, OutError);
	Out.SourcePath = Path;
	return bOk;
}

// dlgexpr normalizer — rewrite the engine skill-check grammar into the pure-Python subset the host
// evaluates. The dlgexpr grammar (`docs/vtmb/python_bridge.md`) has no bitwise operators, so every top-level
// `&`/`|` is a logical join and every bare `IDENT [relop] INT` run is a skill-check.

// The module builds with unity on, so a bare `namespace {}` here would still collide with another
// translation unit's helpers of the same name — `ElysiumExpr.cpp` has its own `ETok`/`Tokenize`. The
// named namespace is the project's convention for that (see `ElysiumMcpTools.cpp`).
namespace ElysiumDlgExprImpl
{
	bool IsIdentStart(TCHAR C) { return FChar::IsAlpha(C) || C == TEXT('_'); }
	bool IsIdentChar(TCHAR C) { return FChar::IsAlnum(C) || C == TEXT('_'); }

	// A lightweight token stream over a dlgexpr string. Enough to find skill-checks and the top-level
	// join operators without a full parse — string literals are passed through opaque so a `&`/`|`/`;`
	// or a digit inside a quote is never mistaken for syntax. Each token records its source span so the
	// rebuilder can copy the original inter-token text verbatim (only skill-checks / joins are rewritten).
	enum class ETok : uint8 { Ident, Int, Op, String, End };
	struct FTok
	{
		ETok Kind = ETok::End;
		FString Text;   // the verbatim source slice (including quotes for a String)
		int32 Start = 0;  // source offset of the first char
		int32 End = 0;    // source offset one past the last char
	};

	TArray<FTok> Tokenize(const FString& S)
	{
		TArray<FTok> Toks;
		const int32 N = S.Len();
		int32 i = 0;
		while (i < N)
		{
			const TCHAR C = S[i];
			if (FChar::IsWhitespace(C)) { ++i; continue; }

			if (C == TEXT('"') || C == TEXT('\''))
			{
				const TCHAR Quote = C;
				const int32 Start = i++;
				while (i < N && S[i] != Quote) { ++i; }
				if (i < N) { ++i; }   // consume the closing quote
				Toks.Add({ ETok::String, S.Mid(Start, i - Start), Start, i });
				continue;
			}
			if (IsIdentStart(C))
			{
				const int32 Start = i;
				while (i < N && IsIdentChar(S[i])) { ++i; }
				Toks.Add({ ETok::Ident, S.Mid(Start, i - Start), Start, i });
				continue;
			}
			if (FChar::IsDigit(C))
			{
				const int32 Start = i;
				while (i < N && (FChar::IsDigit(S[i]) || S[i] == TEXT('.'))) { ++i; }
				Toks.Add({ ETok::Int, S.Mid(Start, i - Start), Start, i });
				continue;
			}
			// A relational/other operator: greedily take a 2-char relop, else one char.
			if (i + 1 < N)
			{
				const FString Two = S.Mid(i, 2);
				if (Two == TEXT("==") || Two == TEXT("!=") || Two == TEXT("<=") || Two == TEXT(">="))
				{
					Toks.Add({ ETok::Op, Two, i, i + 2 });
					i += 2;
					continue;
				}
			}
			Toks.Add({ ETok::Op, FString(1, &S[i]), i, i + 1 });
			++i;
		}
		return Toks;
	}

	bool IsRelop(const FString& Op)
	{
		return Op == TEXT("==") || Op == TEXT("!=") || Op == TEXT("<") || Op == TEXT("<=")
			|| Op == TEXT(">") || Op == TEXT(">=");
	}

	// Reserved identifiers that are NOT skills even when juxtaposed with a number — the dlgexpr keyword
	// set. (A skill-check is `<SkillName> <int>`; `not 1` / `and 2` etc. never are.)
	bool IsKeyword(const FString& Id)
	{
		return Id == TEXT("and") || Id == TEXT("or") || Id == TEXT("not")
			|| Id == TEXT("None") || Id == TEXT("True") || Id == TEXT("False");
	}

	// Rebuild the source, copying the original text between tokens verbatim so pass-through spacing is
	// preserved, and substituting only two things: a skill-check run `IDENT [relop] INT` (IDENT not
	// followed by `.`/`(`, not a keyword, not preceded by `.`) -> `pc.CalcFeat("IDENT") relop INT`
	// (implicit `>=`, with an `M_`/`F_` prefix becoming the sex gate the engine's dependency carries),
	// and the join operators `&` -> AndRepl / `|` -> OrRepl (which differ between condition and
	// action). `Src` is the trimmed source the token spans index into.
	FString Rebuild(const FString& Src, const TArray<FTok>& Toks, const TCHAR* AndRepl, const TCHAR* OrRepl)
	{
		FString Out;
		Out.Reserve(Src.Len() + 32);
		int32 Cursor = 0;   // source offset copied up to

		const int32 N = Toks.Num();
		for (int32 i = 0; i < N; ++i)
		{
			const FTok& T = Toks[i];
			// Copy the untouched source (whitespace) between the previous token and this one.
			Out.Append(Src.Mid(Cursor, T.Start - Cursor));

			// Join operators map to a keyword / statement separator.
			if (T.Kind == ETok::Op && T.Text == TEXT("&")) { Out.Append(AndRepl); Cursor = T.End; continue; }
			if (T.Kind == ETok::Op && T.Text == TEXT("|")) { Out.Append(OrRepl); Cursor = T.End; continue; }

			const bool bPrevDot = (i > 0 && Toks[i - 1].Kind == ETok::Op && Toks[i - 1].Text == TEXT("."));
			const bool bNextCallOrMember = (i + 1 < N && Toks[i + 1].Kind == ETok::Op
				&& (Toks[i + 1].Text == TEXT("(") || Toks[i + 1].Text == TEXT(".")));

			if (T.Kind == ETok::Ident && !IsKeyword(T.Text) && !bPrevDot && !bNextCallOrMember)
			{
				int32 RelAt = INDEX_NONE;
				int32 IntAt = INDEX_NONE;
				if (i + 1 < N && Toks[i + 1].Kind == ETok::Int)
				{
					IntAt = i + 1;
				}
				else if (i + 2 < N && Toks[i + 1].Kind == ETok::Op && IsRelop(Toks[i + 1].Text)
					&& Toks[i + 2].Kind == ETok::Int)
				{
					RelAt = i + 1;
					IntAt = i + 2;
				}

				if (IntAt != INDEX_NONE)
				{
					const FString Relop = (RelAt != INDEX_NONE) ? Toks[RelAt].Text : TEXT(">=");
					// `M_`/`F_` is the check's SEX GATE, not part of the trait name:
					// `CDialogDependency::TestSimple` carries a required-gender field beside the check
					// and rejects the line outright when it does not match the character's
					// `CBaseCombatCharacter::IsMale` (`docs/vtmb/game_runtime.md` section 3). 36 corpus
					// conditions use it — `M_Persuasion 3` and `F_Persuasion 3` on the same beat, with
					// different lines.
					FString Feat = T.Text;
					const TCHAR* SexGate = nullptr;
					if (Feat.StartsWith(TEXT("M_"), ESearchCase::CaseSensitive))
					{
						SexGate = TEXT("pc.IsMale()");
						Feat = Feat.RightChop(2);
					}
					else if (Feat.StartsWith(TEXT("F_"), ESearchCase::CaseSensitive))
					{
						SexGate = TEXT("not pc.IsMale()");
						Feat = Feat.RightChop(2);
					}
					// The receiver is explicit: `CalcFeat` is a Character method, not a module global,
					// so a bare call resolves to nothing in either host. The check is the PC's, which
					// is who the engine's dependency evaluates it against.
					if (SexGate)
					{
						Out.Append(FString::Printf(TEXT("(%s and pc.CalcFeat(\"%s\") %s %s)"),
							SexGate, *Feat, *Relop, *Toks[IntAt].Text));
					}
					else
					{
						Out.Append(FString::Printf(TEXT("pc.CalcFeat(\"%s\") %s %s"),
							*Feat, *Relop, *Toks[IntAt].Text));
					}
					Cursor = Toks[IntAt].End;   // consumed the whole run (the interior spaces are dropped)
					i = IntAt;
					continue;
				}
			}

			// Ordinary token — emit its own source slice verbatim.
			Out.Append(Src.Mid(T.Start, T.End - T.Start));
			Cursor = T.End;
		}
		return Out.TrimStartAndEnd();
	}

	// Shared driver: tokenize the trimmed source, then rebuild with the joins/skill-checks rewritten.
	FString Normalize(const FString& Raw, const TCHAR* AndRepl, const TCHAR* OrRepl)
	{
		const FString Trimmed = Raw.TrimStartAndEnd();
		if (Trimmed.IsEmpty())
		{
			return FString();
		}
		return Rebuild(Trimmed, Tokenize(Trimmed), AndRepl, OrRepl);
	}
}

using namespace ElysiumDlgExprImpl;

FString ElysiumDlgExpr::ConditionToPython(const FString& Raw)
{
	// Condition joins: `&` -> logical `and`, `|` -> logical `or`.
	return Normalize(Raw, TEXT("and"), TEXT("or"));
}

FString ElysiumDlgExpr::ActionToPython(const FString& Raw)
{
	// Action joins: `&` -> statement separator `;` (7 rows corpus-wide use `&` between statements).
	// `|` never separates actions; leave it (a stray one falls to host error-to-false).
	return Normalize(Raw, TEXT(";"), TEXT("|"));
}

// Branch machine (`docs/vtmb/game_runtime.md` §5 "Runtime / branching").

FElysiumDlgConversation::FElysiumDlgConversation(TSharedRef<const FElysiumDlgFile> InFile,
	bool bInPlayerMale, bool bInPlayerMalkavian, FCondFn InCond, FActFn InAct,
	FStartFallbackFn InStartFallback)
	: DlgFile(InFile)
	, bMale(bInPlayerMale)
	, bMalk(bInPlayerMalkavian)
	, CondFn(MoveTemp(InCond))
	, ActFn(MoveTemp(InAct))
	, StartFallbackFn(MoveTemp(InStartFallback))
{
}

bool FElysiumDlgConversation::PassesGate(const FString& RawCondition) const
{
	if (RawCondition.IsEmpty())
	{
		return true;   // no gate — always offered
	}
	return CondFn ? CondFn(RawCondition) : false;
}

int32 FElysiumDlgConversation::SelectStartingLineIndex() const
{
	// CDialog::GetStartingLine walks the physical row array and stops at the first passing sentinel
	// whose link resolves. A passing dangling link warns and does not prevent a later row from winning.
	for (const FElysiumDlgLine& Line : DlgFile->Lines)
	{
		if (!Line.IsStartingCondition() || !PassesGate(Line.Condition))
		{
			continue;
		}

		if (!Line.Link.IsNumeric())
		{
			UE_LOG(LogElysiumDlg, Warning, TEXT("%s: starting condition row %d has non-numeric link '%s'"),
				*DlgFile->SourcePath, Line.Id, *Line.Link);
			continue;
		}

		const int32 TargetId = FCString::Atoi(*Line.Link);
		const int32 TargetIndex = DlgFile->IndexOfId(TargetId);
		if (TargetIndex != INDEX_NONE && DlgFile->Lines[TargetIndex].IsNpcLine())
		{
			UE_LOG(LogElysiumDlg, Verbose, TEXT("%s: starting condition row %d selected NPC line %d"),
				*DlgFile->SourcePath, Line.Id, TargetId);
			return TargetIndex;
		}

		UE_LOG(LogElysiumDlg, Warning, TEXT("%s: starting condition row %d links to missing/invalid NPC line %d"),
			*DlgFile->SourcePath, Line.Id, TargetId);
	}

	// With no winning sentinel, retail executes a non-empty `usescript`. No script means line 1.
	// A script error/non-int returns 0, which deliberately fails resolution below and reaches Acquire's
	// first-stored-line fallback rather than being silently rewritten to line 1.
	const TOptional<int32> ScriptLine = StartFallbackFn ? StartFallbackFn() : TOptional<int32>();
	const int32 SelectedId = ScriptLine.IsSet() ? ScriptLine.GetValue() : 1;
	const int32 SelectedIndex = DlgFile->IndexOfId(SelectedId);
	if (SelectedIndex != INDEX_NONE && DlgFile->Lines[SelectedIndex].IsNpcLine())
	{
		return SelectedIndex;
	}

	// CDialog::Acquire validates GetStartingLine's result and substitutes the first stored line id.
	if (!DlgFile->Lines.IsEmpty() && DlgFile->Lines[0].IsNpcLine())
	{
		UE_LOG(LogElysiumDlg, Verbose, TEXT("%s: invalid starting line %d; falling back to first stored line %d"),
			*DlgFile->SourcePath, SelectedId, DlgFile->Lines[0].Id);
		return 0;
	}

	UE_LOG(LogElysiumDlg, Warning, TEXT("%s: no valid NPC starting line"), *DlgFile->SourcePath);
	return INDEX_NONE;
}

void FElysiumDlgConversation::Start()
{
	const int32 StartingIndex = SelectStartingLineIndex();
	if (StartingIndex != INDEX_NONE)
	{
		EnterNpcLine(StartingIndex);
		return;
	}
	bOver = true;
	++Rev;
}

void FElysiumDlgConversation::EnterNpcLine(int32 LineIndex)
{
	CurrentIndex = LineIndex;
	VisibleChoiceIndices.Reset();
	PendingAutomaticIndex = INDEX_NONE;

	const FElysiumDlgLine& Npc = DlgFile->Lines[LineIndex];
	// NPC col-4 and col-5 are both actions run when the line is spoken (col-4 is exec, not a gate —
	// see the header note).
	if (ActFn)
	{
		if (!Npc.Condition.IsEmpty()) { ActFn(Npc.Condition); }
		if (!Npc.Action.IsEmpty()) { ActFn(Npc.Action); }
	}

	// Gather the contiguous run of rows after this NPC line, up to the next NPC line. A passing
	// Auto-Link/Auto-End row is authored control flow, not a response: it suppresses the response band
	// and remains pending until the spoken NPC turn completes. Physical row order decides between
	// overlapping automatic gates, just as it does for starting sentinels.
	for (int32 j = LineIndex + 1; j < DlgFile->Lines.Num(); ++j)
	{
		const FElysiumDlgLine& L = DlgFile->Lines[j];
		if (L.IsNpcLine())
		{
			break;
		}
		if (L.IsPcChoice() && !L.IsStartingCondition() && PassesGate(L.Condition))
		{
			if (L.IsAutomatic())
			{
				VisibleChoiceIndices.Reset();
				PendingAutomaticIndex = j;
				break;
			}
			VisibleChoiceIndices.Add(j);
		}
	}

	++Rev;
	// No passing choices and no pending automatic -> terminal line; the UI offers a close.
}

const FElysiumDlgLine* FElysiumDlgConversation::CurrentNpcLine() const
{
	return (CurrentIndex != INDEX_NONE && !bOver) ? &DlgFile->Lines[CurrentIndex] : nullptr;
}

const FElysiumDlgLine* FElysiumDlgConversation::VisibleChoice(int32 VisibleIndex) const
{
	if (!VisibleChoiceIndices.IsValidIndex(VisibleIndex))
	{
		return nullptr;
	}
	return &DlgFile->Lines[VisibleChoiceIndices[VisibleIndex]];
}

const FElysiumDlgLine* FElysiumDlgConversation::PendingAutomatic() const
{
	return (PendingAutomaticIndex != INDEX_NONE && !bOver)
		? &DlgFile->Lines[PendingAutomaticIndex] : nullptr;
}

void FElysiumDlgConversation::Choose(int32 VisibleIndex)
{
	if (bOver || PendingAutomaticIndex != INDEX_NONE
		|| !VisibleChoiceIndices.IsValidIndex(VisibleIndex))
	{
		return;
	}
	FollowPcLine(DlgFile->Lines[VisibleChoiceIndices[VisibleIndex]]);
}

void FElysiumDlgConversation::ResolveAutomatic()
{
	if (bOver || PendingAutomaticIndex == INDEX_NONE)
	{
		return;
	}
	const int32 AutomaticIndex = PendingAutomaticIndex;
	PendingAutomaticIndex = INDEX_NONE;
	FollowPcLine(DlgFile->Lines[AutomaticIndex]);
}

void FElysiumDlgConversation::FollowPcLine(const FElysiumDlgLine& Line)
{
	// A visible pick and an automatic transition share the same action-before-link contract.
	if (ActFn && !Line.Action.IsEmpty())
	{
		ActFn(Line.Action);
	}

	const int32 Target = Line.LinkTarget();
	if (Target == 0)
	{
		Close();
		return;
	}
	const int32 NextIndex = DlgFile->IndexOfId(Target);
	if (NextIndex == INDEX_NONE || !DlgFile->Lines[NextIndex].IsNpcLine())
	{
		// Dangling / non-NPC link — end rather than jump somewhere undefined.
		UE_LOG(LogElysiumDlg, Warning, TEXT("%s: PC row %d links to missing/invalid NPC line %d"),
			*DlgFile->SourcePath, Line.Id, Target);
		Close();
		return;
	}
	EnterNpcLine(NextIndex);
}

void FElysiumDlgConversation::AdvanceTerminal()
{
	if (bOver || PendingAutomaticIndex != INDEX_NONE)
	{
		return;
	}
	Close();
}

void FElysiumDlgConversation::Close()
{
	if (bOver)
	{
		return;
	}
	bOver = true;
	VisibleChoiceIndices.Reset();
	PendingAutomaticIndex = INDEX_NONE;
	++Rev;
}
