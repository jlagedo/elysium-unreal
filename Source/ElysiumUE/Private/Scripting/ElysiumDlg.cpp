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

int32 ElysiumDlgClan::OffsetFromSheetClan(int32 SheetClan)
{
	// `FElysiumSheet` stores VtMB's own 2..8 clan encoding (Brujah 2, Gangrel 3, Malkavian 4,
	// Nosferatu 5, Toreador 6, Tremere 7, Ventrue 8). `CDialog::clan_offset` is a DIFFERENT order —
	// the one the seven `.dlg` clan columns are written in — so the two are joined here rather than
	// anywhere a caller might assume they coincide.
	switch (SheetClan)
	{
	case 2: return Brujah;
	case 3: return Gangrel;
	case 4: return Malkavian;
	case 5: return Nosferatu;
	case 6: return Toreador;
	case 7: return Tremere;
	case 8: return Ventrue;
	default: return None;
	}
}

TCHAR ElysiumDlgText::ChosenTakeLetter(const FElysiumDlgLine& Line, bool bMale, int32 ClanOffset)
{
	if (ClanOffset >= 0 && ClanOffset < ElysiumDlgClan::Num && !Line.TextClan[ClanOffset].IsEmpty())
	{
		if (ClanOffset == ElysiumDlgClan::Ventrue)
		{
			return TEXT('m');
		}
		if (ClanOffset == ElysiumDlgClan::Malkavian)
		{
			return TEXT('n');
		}
		// The other five letters are unattested: no shipped `_col_*` file exists for them, so the
		// only honest probe is the one the displayed column would otherwise have had. (`'h' +
		// clan_offset` reproduces both pinned letters and is the likely rule, but nothing in the
		// shipped audio confirms it, so it is not probed.)
		static bool bLoggedUnpinnedClanTake = false;
		if (!bLoggedUnpinnedClanTake)
		{
			bLoggedUnpinnedClanTake = true;
			UE_LOG(LogElysiumDlg, Log,
				TEXT("line %d has a filled clan column %d whose take letter is unpinned by shipped "
					 "audio; probing the gendered take instead"), Line.Id, ClanOffset);
		}
	}
	// `f` only when a female PC has a col-2 that DIFFERS from col-1: `FUN_100e15c0` calls
	// `FUN_100df030(row)`, which is `col2 != NULL && strcmpi(col1, col2) != 0`. Troika's editor fills
	// col-2 with a copy of col-1 on most rows (17,566 filled, 211 distinct female takes shipped), so
	// an "is col-2 non-empty" test asks for a `_col_f` take that does not exist and silences the
	// line — every one of Jack's tutorial rows is such a copy. `e` is the male/default column.
	const bool bFemaleVariant = !Line.TextFemale.IsEmpty()
		&& !Line.TextFemale.Equals(Line.TextMale, ESearchCase::IgnoreCase);
	return (!bMale && bFemaleVariant) ? TEXT('f') : TEXT('e');
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
		// cols 6-12 — the seven clan text columns, in clan_offset order (`read_line_data`
		// `0x100e61d0` reads exactly seven and warns "Missing clan field %c" when a row is short).
		for (int32 Clan = 0; Clan < ElysiumDlgClan::Num; ++Clan)
		{
			Line.TextClan[Clan] = UnwrapField(Segments[6 + Clan]);
		}
		Line.Role = RoleFromLink(Line.Link);

		// `read_line_data`'s own acceptance test, applied after the whole row is read: a row is
		// stored only when its id is non-negative AND its male text is at least two characters.
		// 30,828 padding rows and 635 text-less `#` rows corpus-wide never enter retail's table, so
		// a link to one of those ids does not resolve there either.
		if (Line.Id < 0 || Line.TextMale.Len() < 2)
		{
			continue;
		}

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

FString ElysiumDlgExpr::ConditionToPython(const FString& Raw)
{
	// Condition joins: `&` -> logical `and`, `|` -> logical `or`.
	return ElysiumDlgExprImpl::Normalize(Raw, TEXT("and"), TEXT("or"));
}

FString ElysiumDlgExpr::ActionToPython(const FString& Raw)
{
	// Action joins: `&` -> statement separator `;` (7 rows corpus-wide use `&` between statements).
	// `|` never separates actions; leave it (a stray one falls to host error-to-false).
	return ElysiumDlgExprImpl::Normalize(Raw, TEXT(";"), TEXT("|"));
}

// The col-4 dependency — `CDialogDependency::Parse` / `ParseDep` / `TestSimple` / `Test`.

const TCHAR* ElysiumDlgTraitClassName(EElysiumDlgTraitClass Class)
{
	switch (Class)
	{
	case EElysiumDlgTraitClass::Attribute:  return TEXT("attribute");
	case EElysiumDlgTraitClass::Ability:    return TEXT("ability");
	case EElysiumDlgTraitClass::Discipline: return TEXT("discipline");
	case EElysiumDlgTraitClass::Feat:       return TEXT("feat");
	default:                                return TEXT("unknown");
	}
}

namespace ElysiumDlgDepImpl
{
	// `TestSimple`'s two hard-coded constants. Discipline slot 6 is `Dominate` and clan_offset 5 is
	// Ventrue in the shipped tables; the resolver's `DisciplineName` is used only to notice a
	// rulebook that has moved the slot out from under the constant.
	constexpr int32 DominateDisciplineId = 6;
	constexpr int32 FrenzyFeatId = ElysiumDlgFeat::Frenzy;   // `feats.txt` order 22 = `Frenzy_Feat`

	bool IsIdentBody(const FString& S)
	{
		if (S.IsEmpty() || !(FChar::IsAlpha(S[0]) || S[0] == TEXT('_')))
		{
			return false;
		}
		for (const TCHAR C : S)
		{
			if (!FChar::IsAlnum(C) && C != TEXT('_'))
			{
				return false;
			}
		}
		return true;
	}

	bool IsIntegerLiteral(const FString& S)
	{
		if (S.IsEmpty())
		{
			return false;
		}
		for (const TCHAR C : S)
		{
			if (!FChar::IsDigit(C))
			{
				return false;
			}
		}
		return true;
	}

	// `FUN_102047d0` — the statref half of ParseDep. Strips the `M_`/`F_` sex gate, folds a `-`
	// before the threshold into the inversion flag (retail overwrites the character with a space,
	// so the number itself parses positive), then splits on whitespace: token 0 is the trait name,
	// token 1 is `atoi`'d as the threshold. There is no relop in this grammar at all — every
	// shipped skill front is the implicit `>=`, which is why the 5 "explicit relop" conditions the
	// corpus survey reported are Python halves, not checks.
	void ParseStatRef(const FString& Half, FString& OutName, int32& OutThreshold,
		bool& bOutInverted, EElysiumDlgSexGate& OutGate)
	{
		FString S = Half;
		S.TrimStartAndEndInline();
		OutGate = EElysiumDlgSexGate::None;
		if (S.StartsWith(TEXT("F_"), ESearchCase::IgnoreCase))
		{
			OutGate = EElysiumDlgSexGate::Female;
			S.RightChopInline(2, EAllowShrinking::No);
		}
		else if (S.StartsWith(TEXT("M_"), ESearchCase::IgnoreCase))
		{
			OutGate = EElysiumDlgSexGate::Male;
			S.RightChopInline(2, EAllowShrinking::No);
		}

		bOutInverted = false;
		int32 Dash = INDEX_NONE;
		if (S.FindChar(TEXT('-'), Dash))
		{
			bOutInverted = true;
			S[Dash] = TEXT(' ');
		}

		TArray<FString> Tokens;
		S.ParseIntoArrayWS(Tokens);
		OutName = Tokens.Num() > 0 ? Tokens[0] : FString();
		OutThreshold = Tokens.Num() > 1 ? FCString::Atoi(*Tokens[1]) : 0;
	}
}

FElysiumDlgDependency FElysiumDlgDependency::Parse(const FString& RawCondition,
	const IElysiumDlgTraitResolver* Resolver)
{
	using namespace ElysiumDlgDepImpl;

	FElysiumDlgDependency Dep;
	const FString Trimmed = RawCondition.TrimStartAndEnd();
	if (Trimmed.IsEmpty())
	{
		return Dep;   // bEmpty — the caller's open gate (retail never Parses an absent field)
	}
	Dep.bEmpty = false;

	// `Parse`: the FIRST `|` wins over the first `&`, and there are at most two halves — a second
	// separator stays inside the right half.
	TArray<FString> Halves;
	int32 Sep = INDEX_NONE;
	if (Trimmed.FindChar(TEXT('|'), Sep))
	{
		Dep.Compound = EElysiumDlgCompound::Or;
	}
	else if (Trimmed.FindChar(TEXT('&'), Sep))
	{
		Dep.Compound = EElysiumDlgCompound::And;
	}
	if (Sep != INDEX_NONE)
	{
		Halves.Add(Trimmed.Left(Sep));
		Halves.Add(Trimmed.Mid(Sep + 1));
	}
	else
	{
		Halves.Add(Trimmed);
	}

	bool bPrecedenceFixed = false;
	for (const FString& Half : Halves)
	{
		const FString HalfTrimmed = Half.TrimStartAndEnd();
		if (HalfTrimmed.IsEmpty())
		{
			continue;   // retail's strlen guard: an empty half writes nothing
		}

		bool bClaimed = false;
		if (!Dep.bHasSkill)
		{
			FString Name;
			int32 Threshold = 0;
			bool bInverted = false;
			EElysiumDlgSexGate Gate = EElysiumDlgSexGate::None;
			ParseStatRef(HalfTrimmed, Name, Threshold, bInverted, Gate);

			EElysiumDlgTraitClass Class = EElysiumDlgTraitClass::Unknown;
			int32 Id = INDEX_NONE;
			if (!Name.IsEmpty() && Resolver && Resolver->ResolveTrait(Name, Class, Id)
				&& Class != EElysiumDlgTraitClass::Unknown)
			{
				Dep.bHasSkill = true;
				Dep.Trait = Name;
				Dep.Class = Class;
				Dep.TraitId = Id;
				Dep.Threshold = Threshold;
				Dep.bInverted = bInverted;
				Dep.SexGate = Gate;
				// `ParseDep`: the blood price is the threshold for a discipline and zero for every
				// other class. It is recorded even for an inverted check, exactly as retail does;
				// only the wire flag that drives the dot glyphs is suppressed there.
				Dep.BloodCost = (Class == EElysiumDlgTraitClass::Discipline) ? Threshold : 0;
				bClaimed = true;
				if (Class == EElysiumDlgTraitClass::Discipline && Id == DominateDisciplineId)
				{
					// TestSimple gates discipline id 6 on clan_offset 5. Name the slot once so a
					// re-ordered `stats.txt` shows up here rather than silently moving the Ventrue
					// refusal onto a different power.
					static bool bLoggedSlotDrift = false;
					const FString Named = Resolver->DisciplineName(Id);
					if (!bLoggedSlotDrift && !Named.IsEmpty()
						&& !Named.Equals(TEXT("Dominate"), ESearchCase::IgnoreCase))
					{
						bLoggedSlotDrift = true;
						UE_LOG(LogElysiumDlg, Warning,
							TEXT("the clan-gated discipline slot 6 resolves to '%s', not Dominate — "
								 "TestSimple's Ventrue constant may no longer name the power it gated"),
							*Named);
					}
				}
				if (!bPrecedenceFixed)
				{
					Dep.Precedence = EElysiumDlgPrecedence::SkillFirst;
					bPrecedenceFixed = true;
				}
			}
			else
			{
				// A corpus diagnostic: the half is shaped exactly like a check but names nothing.
				TArray<FString> Tokens;
				FString Shape = HalfTrimmed;
				Shape.TrimStartAndEndInline();
				Shape.ReplaceInline(TEXT("-"), TEXT(" "));
				Shape.ParseIntoArrayWS(Tokens);
				if (Tokens.Num() == 2 && IsIdentBody(Name) && IsIntegerLiteral(Tokens[1]))
				{
					Dep.bUnresolvedSkillFront = true;
				}
			}
		}

		if (!bClaimed)
		{
			// Retail holds ONE Python buffer, so a second Python half overwrites the first. Exactly
			// one shipped condition does that (`IsClan(pc,"Ventrue") & G.Patch_Plus == 1`), and it
			// is dead in retail: the compound then tests a simple dependency that was never set,
			// which falls to `TestSimple`'s `Unhandled dialog dependency` arm and reads false. That
			// is reproduced rather than repaired — it is an authoring defect, not a port gap.
			Dep.Python = HalfTrimmed;
			Dep.bHasPython = true;
			if (!bPrecedenceFixed)
			{
				Dep.Precedence = EElysiumDlgPrecedence::PythonFirst;
				bPrecedenceFixed = true;
			}
		}
	}

	if (Sep == INDEX_NONE)
	{
		Dep.Compound = Dep.bHasSkill ? EElysiumDlgCompound::SkillOnly : EElysiumDlgCompound::PythonOnly;
	}
	return Dep;
}

namespace ElysiumDlgDepTest
{
	// `TestSimple` (`0x100e9760`) for one dependency, over the injected sheet. `bForcePass` is
	// M-DISABLED's counterfactual: the same walk with the rating/blood comparison forced true, so
	// the sex gate and the Ventrue gate still refuse.
	bool TestSimpleImpl(const FElysiumDlgDependency& Dep, const IElysiumDlgSheet* Sheet,
		bool bForcePass, int32& OutHave)
	{
		OutHave = 0;
		if (Sheet == nullptr)
		{
			return false;   // no sheet: fail closed, as an unresolved gate does
		}
		// The sex gate is the first thing TestSimple reads, before the class switch.
		if (Dep.SexGate == EElysiumDlgSexGate::Female && Sheet->IsMale())
		{
			return false;
		}
		if (Dep.SexGate == EElysiumDlgSexGate::Male && !Sheet->IsMale())
		{
			return false;
		}

		switch (Dep.Class)
		{
		case EElysiumDlgTraitClass::Attribute:
		case EElysiumDlgTraitClass::Ability:
		{
			OutHave = Sheet->Stat(Dep.Class, Dep.TraitId);
			if (bForcePass)
			{
				return true;
			}
			// The inverted arm is `value < threshold` AND `value >= 0` — retail re-reads the value
			// and returns true only for a non-negative one, so a slot reading -1 (absent) fails
			// both ways round.
			return Dep.bInverted ? (OutHave < Dep.Threshold && OutHave >= 0)
			                     : (OutHave >= Dep.Threshold);
		}
		case EElysiumDlgTraitClass::Discipline:
		{
			if (Dep.TraitId == ElysiumDlgDepImpl::DominateDisciplineId
				&& Sheet->ClanOffset() != ElysiumDlgClan::Ventrue)
			{
				// Hard-coded in TestSimple: discipline id 6 is refused unless clan_offset is 5.
				// Parse() names the slot through the rulebook and warns when it is not Dominate, so
				// a re-ordered `stats.txt` is visible rather than silently moving the refusal.
				return false;
			}
			const int32 Rating = Sheet->Discipline(Dep.TraitId);
			OutHave = Rating;
			if (Dep.bInverted)
			{
				// The inverted arm reads the rating only: retail does NOT test the blood pool for
				// an authored failure route. The CHARGE is a separate question and the answer is
				// the other way round -- `pc_charge_dependency` (`0x100e8b90`) has no inversion
				// test at all, only `cost != 0`, so an inverted discipline row that is picked is
				// still charged its threshold in blood. `ParseDep` records `BloodCost` for it for
				// exactly that reason.
				return bForcePass || (Rating < Dep.Threshold && Rating >= 0);
			}
			if (bForcePass)
			{
				return true;
			}
			// Both halves must meet the threshold: the rating AND attributes slot 0xc.
			return Rating >= Dep.Threshold && Sheet->BloodPool() >= Dep.Threshold;
		}
		case EElysiumDlgTraitClass::Feat:
		{
			if (Dep.TraitId == ElysiumDlgDepImpl::FrenzyFeatId)
			{
				// `CBaseCombatCharacter::FrenzyComparison(threshold)` — the frenzy state compare,
				// inverted by the same flag. Nothing in the port answers it yet.
				// TODO(dialogue-plan): CBaseCombatCharacter::FrenzyComparison — the frenzy
				// dependency arm (feat id 0x16) has no source in the substrate; it answers false
				// (and its inverted form true) until the frenzy state lands.
				static bool bLoggedFrenzySeam = false;
				if (!bLoggedFrenzySeam)
				{
					bLoggedFrenzySeam = true;
					UE_LOG(LogElysiumDlg, Warning,
						TEXT("dialogue dependency '%s %d' routes to FrenzyComparison, which is not "
							 "recovered; the check answers %s"), *Dep.Trait, Dep.Threshold,
						Dep.bInverted ? TEXT("true") : TEXT("false"));
				}
				return bForcePass || Dep.bInverted;
			}
			OutHave = Sheet->CalcFeat(Dep.Trait);
			if (bForcePass)
			{
				return true;
			}
			return Dep.bInverted ? (OutHave < Dep.Threshold && OutHave >= 0)
			                     : (OutHave >= Dep.Threshold);
		}
		default:
			// Retail's `DevMsg("Unhandled dialog dependency: %s")` arm — a compound whose simple
			// slot was never claimed reads false, whichever way the counterfactual is asked.
			return false;
		}
	}
}

FElysiumDlgGateResult FElysiumDlgDependency::Explain(const IElysiumDlgSheet* Sheet,
	FPythonFn PythonFn) const
{
	FElysiumDlgGateResult Result;
	if (bEmpty)
	{
		Result.bPasses = true;
		return Result;
	}

	// One Python evaluation, memoised, so the counterfactual costs no second script call even
	// where retail's short-circuit would have skipped the first.
	bool bPythonEvaluated = false;
	bool bPythonValue = false;
	auto EvalPython = [&]() -> bool
	{
		if (!bPythonEvaluated)
		{
			bPythonEvaluated = true;
			bPythonValue = bHasPython ? PythonFn(Python) : false;
		}
		return bPythonValue;
	};

	int32 Have = 0;
	const bool bSimple = ElysiumDlgDepTest::TestSimpleImpl(*this, Sheet, /*bForcePass*/ false, Have);
	int32 ForcedHave = 0;
	const bool bSimpleForced = ElysiumDlgDepTest::TestSimpleImpl(*this, Sheet, /*bForcePass*/ true, ForcedHave);

	auto Combine = [&](bool bSimpleAnswer) -> bool
	{
		switch (Compound)
		{
		case EElysiumDlgCompound::SkillOnly:
			return bSimpleAnswer;
		case EElysiumDlgCompound::PythonOnly:
			return EvalPython();
		case EElysiumDlgCompound::And:
			return Precedence == EElysiumDlgPrecedence::SkillFirst
				? (bSimpleAnswer && EvalPython())
				: (EvalPython() && bSimpleAnswer);
		case EElysiumDlgCompound::Or:
			return Precedence == EElysiumDlgPrecedence::SkillFirst
				? (bSimpleAnswer || EvalPython())
				: (EvalPython() || bSimpleAnswer);
		}
		return false;
	};

	Result.bPasses = Combine(bSimple);
	// M-DISABLED, precisely: a labellable (non-inverted, resolved) skill component that fails, in a
	// gate the same evaluation would pass with that component forced true. An inverted check is an
	// authored failure route and is never shown disabled.
	Result.bSkillFailedOnly = !Result.bPasses && HasLabelledSkillFront() && !bSimple
		&& Combine(bSimpleForced);

	if (HasLabelledSkillFront())
	{
		Result.Label.bValid = true;
		Result.Label.Trait = Trait;
		Result.Label.Kind = Class;
		Result.Label.Required = Threshold;
		Result.Label.Have = Have;
		Result.Label.BloodCost = BloodCost;
		Result.Label.Pool = Sheet != nullptr ? Sheet->BloodPool() : 0;
		// `TestSimple`'s discipline arm is two comparisons against the same threshold; when the
		// rating half passes, the pool is the one that refused. M-REQ has to say so, or a greyed
		// `[ DOMINATE 3/2 ]` row reads as a contradiction.
		Result.Label.bBloodShort = Class == EElysiumDlgTraitClass::Discipline
			&& BloodCost > 0 && Have >= Threshold && Result.Label.Pool < BloodCost;
	}
	return Result;
}

bool FElysiumDlgDependency::Test(const IElysiumDlgSheet* Sheet, FPythonFn PythonFn) const
{
	// Retail short-circuits, and a col-4 Python half is a pure eval, so Test never needs the
	// counterfactual. Kept separate from Explain for exactly that reason.
	if (bEmpty)
	{
		return true;
	}
	int32 Have = 0;
	switch (Compound)
	{
	case EElysiumDlgCompound::SkillOnly:
		return ElysiumDlgDepTest::TestSimpleImpl(*this, Sheet, false, Have);
	case EElysiumDlgCompound::PythonOnly:
		return bHasPython && PythonFn(Python);
	case EElysiumDlgCompound::And:
		if (Precedence == EElysiumDlgPrecedence::SkillFirst)
		{
			return ElysiumDlgDepTest::TestSimpleImpl(*this, Sheet, false, Have) && PythonFn(Python);
		}
		return PythonFn(Python) && ElysiumDlgDepTest::TestSimpleImpl(*this, Sheet, false, Have);
	case EElysiumDlgCompound::Or:
		if (Precedence == EElysiumDlgPrecedence::SkillFirst)
		{
			return ElysiumDlgDepTest::TestSimpleImpl(*this, Sheet, false, Have) || PythonFn(Python);
		}
		return PythonFn(Python) || ElysiumDlgDepTest::TestSimpleImpl(*this, Sheet, false, Have);
	}
	return false;
}

void FElysiumDlgDependency::Charge(IElysiumDlgSheet* Sheet) const
{
	if (Sheet == nullptr || BloodCost == 0)
	{
		return;   // `pc_charge_dependency` does nothing without a cost
	}
	Sheet->SpendBlood(BloodCost);
	if (Class == EElysiumDlgTraitClass::Discipline)
	{
		// The level retail passes is the blood cost, which is the same number as the threshold.
		Sheet->AddFakedDisciplineEffect(Trait, TraitId, BloodCost);
	}
}

// Branch machine (`docs/vtmb/game_runtime.md` §5 "Runtime / branching").

FElysiumDlgConversation::FElysiumDlgConversation(TSharedRef<const FElysiumDlgFile> InFile,
	bool bInPlayerMale, bool bInPlayerMalkavian, FCondFn InCond, FActFn InAct,
	FStartFallbackFn InStartFallback)
	: DlgFile(InFile)
	, bMale(bInPlayerMale)
	, ClanOffset(bInPlayerMalkavian ? ElysiumDlgClan::Malkavian : ElysiumDlgClan::None)
	, CondFn(MoveTemp(InCond))
	, ActFn(MoveTemp(InAct))
	, StartFallbackFn(MoveTemp(InStartFallback))
{
}

void FElysiumDlgConversation::SetGateContext(TSharedPtr<IElysiumDlgSheet> InSheet,
	TSharedPtr<const IElysiumDlgTraitResolver> InResolver, int32 InClanOffset)
{
	Sheet = MoveTemp(InSheet);
	TraitResolver = MoveTemp(InResolver);
	ClanOffset = InClanOffset;
}

FElysiumDlgGateResult FElysiumDlgConversation::ExplainGate(const FString& RawCondition) const
{
	FElysiumDlgGateResult Open;
	if (RawCondition.IsEmpty())
	{
		Open.bPasses = true;
		return Open;   // no gate — always offered (retail's caller never Parses an absent field)
	}
	const FElysiumDlgDependency Dep = FElysiumDlgDependency::Parse(RawCondition, TraitResolver.Get());
	if (Dep.bHasSkill && !Sheet.IsValid() && !bLoggedMissingSheet)
	{
		bLoggedMissingSheet = true;
		UE_LOG(LogElysiumDlg, Warning,
			TEXT("%s: '%s' carries a skill check but no character sheet is bound; every skill front "
				 "in this conversation fails closed"), *DlgFile->SourcePath, *RawCondition);
	}
	return Dep.Explain(Sheet.Get(),
		[this](const FString& PythonSource) { return CondFn ? CondFn(PythonSource) : false; });
}

bool FElysiumDlgConversation::PassesGate(const FString& RawCondition) const
{
	return ExplainGate(RawCondition).bPasses;
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
	if (bStarted)
	{
		// The opener's col-4 is an action, not a gate; running it twice would double its `G`
		// writes. The world starts the conversation itself so its session exists first, so a
		// caller that already started it lands here.
		return;
	}
	bStarted = true;
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
	if (bOver)
	{
		// A col-5 flushed on the way into this turn may have closed the conversation
		// (`CDialog::Release`). Retail's `process_npc_line` is unreachable once the dialog object
		// is released; entering a line here would resurrect a closed session with a live NPC row.
		return;
	}
	CurrentIndex = LineIndex;
	VisibleChoiceEntries.Reset();
	PendingAutomaticIndex = INDEX_NONE;
	PendingNpcAction.Reset();
	bNoValidReply = false;

	const FElysiumDlgLine& Npc = DlgFile->Lines[LineIndex];
	// `process_npc_line` (`0x100e8100`): col-4 runs NOW (it is exec on an NPC row, not a gate — see
	// the header note), and col-5 is PARKED at `+0x30ea` for `CallPendingNPCEventScript`. The world
	// flushes it when the line finishes speaking, when the player skips it, when a pick cuts it, and
	// on close. Nothing else may run it.
	if (ActFn && !Npc.Condition.IsEmpty())
	{
		ActFn(Npc.Condition);
		if (bOver)
		{
			// That action released the dialog (`EndDialog`). Retail's `process_npc_line` has no
			// dialog object left to park a script on or fill a response band for.
			return;
		}
	}
	PendingNpcAction = Npc.Action;

	// Gather the contiguous run of rows after this NPC line, up to the next NPC line. A passing
	// Auto-Link/Auto-End row is authored control flow, not a response: it suppresses the response band
	// and remains pending until the spoken NPC turn completes. Physical row order decides between
	// overlapping automatic gates, just as it does for starting sentinels.
	int32 CandidateRows = 0;
	for (int32 j = LineIndex + 1; j < DlgFile->Lines.Num(); ++j)
	{
		const FElysiumDlgLine& L = DlgFile->Lines[j];
		if (L.IsNpcLine())
		{
			break;
		}
		if (!L.IsPcChoice() || L.IsStartingCondition())
		{
			continue;
		}
		++CandidateRows;

		const FElysiumDlgGateResult Gate = ExplainGate(L.Condition);
		if (Gate.bPasses)
		{
			if (L.IsAutomatic())
			{
				VisibleChoiceEntries.Reset();
				PendingAutomaticIndex = j;
				break;
			}
			VisibleChoiceEntries.Add({ j, /*bEnabled*/ true, Gate });
			continue;
		}
		// M-DISABLED — a row that fails only on its skill front stays visible, greyed, carrying its
		// requirement label, so the player learns what they lack. An automatic control row never
		// becomes one: it is not a response.
		if (Gate.bSkillFailedOnly && !L.IsAutomatic())
		{
			VisibleChoiceEntries.Add({ j, /*bEnabled*/ false, Gate });
		}
	}

	// M-DISABLED's dedup: 48 corpus pairs author the pass and fail routes with the same sentence,
	// and the enabled fail route already carries it. Drop the disabled twin rather than print the
	// line twice.
	if (PendingAutomaticIndex == INDEX_NONE)
	{
		TSet<FString> EnabledText;
		for (const FElysiumDlgVisibleChoice& Entry : VisibleChoiceEntries)
		{
			if (Entry.bEnabled)
			{
				EnabledText.Add(DlgFile->Lines[Entry.LineIndex].DisplayText(bMale, ClanOffset));
			}
		}
		if (!EnabledText.IsEmpty())
		{
			VisibleChoiceEntries.RemoveAll([this, &EnabledText](const FElysiumDlgVisibleChoice& Entry)
			{
				return !Entry.bEnabled
					&& EnabledText.Contains(DlgFile->Lines[Entry.LineIndex].DisplayText(bMale, ClanOffset));
			});
		}
	}

	// M-CAP — retail stops at four passing rows because the wire packet holds four dependency slots.
	// The port shows every one; a band that overflows is an authoring finding, logged once.
	const int32 Enabled = NumEnabledChoices();
	if (Enabled > 4)
	{
		UE_LOG(LogElysiumDlg, Log,
			TEXT("%s: line %d offers %d enabled responses; retail's wire packet caps the band at 4 "
				 "(M-CAP: the port shows them all)"), *DlgFile->SourcePath, Npc.Id, Enabled);
	}

	// Retail's "no valid reply": `get_pc_responses` returned nothing and no Auto-End set the
	// auto-terminate flag, so the engine replaces the NPC's own text and shows one dummy response.
	// A band that authored no PC rows at all is terminal by design and keeps the authored line.
	bNoValidReply = Enabled == 0 && PendingAutomaticIndex == INDEX_NONE && CandidateRows > 0;

	++Rev;
	// No passing choices and no pending automatic -> terminal line; the UI offers a close.
}

void FElysiumDlgConversation::FlushPendingNpcAction()
{
	if (PendingNpcAction.IsEmpty())
	{
		return;
	}
	// Cleared BEFORE the call: `CallPendingNPCEventScript` zeroes the buffer first, so a script that
	// re-enters dialogue cannot run this row's col-5 a second time.
	const FString Action = MoveTemp(PendingNpcAction);
	PendingNpcAction.Reset();
	if (ActFn)
	{
		ActFn(Action);
	}
}

const FElysiumDlgLine* FElysiumDlgConversation::CurrentNpcLine() const
{
	return (CurrentIndex != INDEX_NONE && !bOver) ? &DlgFile->Lines[CurrentIndex] : nullptr;
}

const FElysiumDlgLine* FElysiumDlgConversation::VisibleChoice(int32 VisibleIndex) const
{
	if (!VisibleChoiceEntries.IsValidIndex(VisibleIndex))
	{
		return nullptr;
	}
	return &DlgFile->Lines[VisibleChoiceEntries[VisibleIndex].LineIndex];
}

int32 FElysiumDlgConversation::VisibleChoiceLineId(int32 VisibleIndex) const
{
	const FElysiumDlgLine* Line = VisibleChoice(VisibleIndex);
	return Line != nullptr ? Line->Id : INDEX_NONE;
}

bool FElysiumDlgConversation::IsChoiceEnabled(int32 VisibleIndex) const
{
	return VisibleChoiceEntries.IsValidIndex(VisibleIndex)
		&& VisibleChoiceEntries[VisibleIndex].bEnabled;
}

int32 FElysiumDlgConversation::NumEnabledChoices() const
{
	int32 Count = 0;
	for (const FElysiumDlgVisibleChoice& Entry : VisibleChoiceEntries)
	{
		Count += Entry.bEnabled ? 1 : 0;
	}
	return Count;
}

const FElysiumDlgLine* FElysiumDlgConversation::PendingAutomatic() const
{
	return (PendingAutomaticIndex != INDEX_NONE && !bOver)
		? &DlgFile->Lines[PendingAutomaticIndex] : nullptr;
}

void FElysiumDlgConversation::Choose(int32 VisibleIndex)
{
	if (bOver || PendingAutomaticIndex != INDEX_NONE
		|| !VisibleChoiceEntries.IsValidIndex(VisibleIndex))
	{
		return;
	}
	if (!VisibleChoiceEntries[VisibleIndex].bEnabled)
	{
		return;   // M-DISABLED: a greyed row's number key does nothing
	}
	// BY VALUE, before the flush. `FlushPendingNpcAction` runs authored script: the parked col-5
	// can `EndDialog` (which `Close()`s this conversation and RESETS `VisibleChoiceEntries`) or
	// open another one. A reference into the band would dangle across that call.
	const int32 LineIndex = VisibleChoiceEntries[VisibleIndex].LineIndex;

	// `CDialog::Pick` (`0x100e4bd0`) order, with M-REVEAL's cut folded in where retail's
	// `NPCNotifyDoneTalking` already had to have run: the NPC's parked col-5 first, then the
	// dependency charge, then the row's own col-5, then the link.
	FlushPendingNpcAction();
	if (bOver)
	{
		// The parked col-5 released the dialog. Retail's `Pick` is unreachable past a `Release`,
		// so nothing further of this pick happens: no blood is spent, no faked effect is raised
		// and no line is entered.
		return;
	}
	const FElysiumDlgLine& Line = DlgFile->Lines[LineIndex];
	if (Sheet.IsValid())
	{
		const FElysiumDlgDependency Dep =
			FElysiumDlgDependency::Parse(Line.Condition, TraitResolver.Get());
		Dep.Charge(Sheet.Get());
		if (VisibleChoiceEntries.IsValidIndex(VisibleIndex)
			&& VisibleChoiceEntries[VisibleIndex].LineIndex == LineIndex)
		{
			VisibleChoiceEntries[VisibleIndex].Gate.Label.bCharged = Dep.BloodCost > 0;
		}
	}
	FollowPcLine(Line);
}

void FElysiumDlgConversation::ResolveAutomatic()
{
	if (bOver || PendingAutomaticIndex == INDEX_NONE)
	{
		return;
	}
	const int32 AutomaticIndex = PendingAutomaticIndex;
	PendingAutomaticIndex = INDEX_NONE;
	// An automatic row travels the same Pick path, so the parked col-5 goes first here too — and
	// with it the same rule: a col-5 that released the dialog ends the turn here.
	FlushPendingNpcAction();
	if (bOver)
	{
		return;
	}
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
	// `CDialog::Release` (`0x100e5240`) flushes the pending NPC event script before it clears the
	// live dialogue state, so a line cut short by a close still runs the col-5 it parked.
	FlushPendingNpcAction();
	bOver = true;
	VisibleChoiceEntries.Reset();
	PendingAutomaticIndex = INDEX_NONE;
	bNoValidReply = false;
	++Rev;
}
