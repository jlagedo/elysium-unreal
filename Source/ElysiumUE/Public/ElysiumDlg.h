#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"

// VtMB `.dlg` conversations. Three separable pieces, each unit-testable on its own:
//
//   * FElysiumDlgFile     — the 13-field CRLF/Latin-1 parser (physical format, docs/vtmb/game_runtime.md §5).
//   * ElysiumDlgExpr      — the `dlgexpr` front-normalizer that rewrites a raw field-4/5 string into the
//                           pure-Python subset the installed script host evaluates (skillchecks ->
//                           CalcFeat compares; the condition-level `&`/`|` -> `and`/`or`; the action-level
//                           `&` -> `;`). It is total; a string it cannot classify passes through, so the
//                           host's own error-to-false hides the line / aborts the action.
//   * FElysiumDlgConversation — the branch state machine. Pure C++/no UObject/no world: it takes a
//                           condition-eval and an action-exec callback, so it drives equally from a unit
//                           test (a fake `G`) and from the game (routed to the script host).
//
// Field roles (col-3 link): `#` = an NPC-spoken line whose col-4 AND col-5 are *actions* run when the
// line is entered (confirmed by data: jack_tutorial entry line's col-4 is `G.Story_State = -3`, an
// assignment — so NPC col-4 is exec, not a gate); a number N = a PC choice gated by its col-4 *condition*,
// jumping to NPC line N when picked (N == 0 ends the conversation); empty = editor padding, ignored.

enum class EElysiumDlgRole : uint8
{
	Padding,    // col-3 empty — editor padding, not a real turn
	NpcLine,    // col-3 "#"   — NPC-spoken line; col-4 + col-5 are actions
	PcChoice,   // col-3 = N   — player response gated by col-4; jumps to NPC line N (0 = END)
};

// Presentation transform on the spoken text. The col-1/col-2 strings carry inline `[...]` voice-director
// stage directions ("[chuckle]", "[laughing...]") meant for the VO recording, not the player; VtMB strips
// them from the on-screen subtitle. Applied only for display — the parser keeps the raw text verbatim so
// the corpus tests and any future RE compare against exactly what VtMB shipped.
namespace ElysiumDlgText
{
	// Remove every `[...]` span and collapse the whitespace the removal leaves behind. An unterminated
	// `[` (no closing `]`) is left intact.
	FString StripStageDirections(const FString& Raw);
}

// One parsed `.dlg` row (13 fields; cols 6-11 are always empty and not stored).
struct FElysiumDlgLine
{
	int32 Id = 0;               // col-0 — line id, unique in file
	FString TextMale;           // col-1 — spoken text (male-PC variant): NPC subtitle / PC choice text
	FString TextFemale;         // col-2 — spoken text (female-PC variant); empty falls back to col-1
	FString Link;               // col-3 — raw link ("#", "0", "87", "")
	FString Condition;          // col-4 — PC: dlgexpr gate (eval). NPC: an action (exec).
	FString Action;             // col-5 — action(s) run when spoken/chosen (`;`-separated)
	FString TextMalkavian;      // col-12 — the Malkavian-PC variant of this line's text; shown *instead of*
	                            // col-1/2 when the player is Malkavian (empty = no Malkavian variant)
	EElysiumDlgRole Role = EElysiumDlgRole::Padding;

	bool IsNpcLine() const { return Role == EElysiumDlgRole::NpcLine; }
	bool IsPcChoice() const { return Role == EElysiumDlgRole::PcChoice; }
	// Troika's editor emits these PC-role rows as control-flow markers. They are never player
	// responses: after the preceding NPC turn has finished, Auto-Link follows its numeric link and
	// Auto-End follows link 0. Classification is exact apart from surrounding whitespace/case so a
	// real authored sentence containing either phrase is not swallowed.
	bool IsAutoLink() const
	{
		FString Marker = TextMale;
		Marker.TrimStartAndEndInline();
		return Role == EElysiumDlgRole::PcChoice
			&& Marker.Equals(TEXT("(Auto-Link)"), ESearchCase::IgnoreCase);
	}
	bool IsAutoEnd() const
	{
		FString Marker = TextMale;
		Marker.TrimStartAndEndInline();
		return Role == EElysiumDlgRole::PcChoice
			&& Marker.Equals(TEXT("(Auto-End)"), ESearchCase::IgnoreCase);
	}
	bool IsAutomatic() const { return IsAutoLink() || IsAutoEnd(); }
	// Retail recognizes a starting-condition sentinel by a case-insensitive substring in raw col-1.
	// All three spellings occur in the engine classifier; this is independent of the row's link role.
	bool IsStartingCondition() const
	{
		return TextMale.Contains(TEXT("starting condition"), ESearchCase::IgnoreCase)
			|| TextMale.Contains(TEXT("starting-condition"), ESearchCase::IgnoreCase)
			|| TextMale.Contains(TEXT("starting_condition"), ESearchCase::IgnoreCase);
	}

	// A PC choice whose link is the literal 0 — picking it ends the conversation.
	bool IsEnd() const { return Role == EElysiumDlgRole::PcChoice && Link == TEXT("0"); }
	// Parsed integer link for a PC choice (0 = END); INDEX_NONE for a non-PC row.
	int32 LinkTarget() const { return Role == EElysiumDlgRole::PcChoice ? FCString::Atoi(*Link) : INDEX_NONE; }

	// The raw subtitle/choice text for the player's gender (col-2 when female and present, else col-1),
	// verbatim from the file (stage directions included) — for tests / RE compares. Ignores clan.
	const FString& Text(bool bMale) const { return (bMale || TextFemale.IsEmpty()) ? TextMale : TextFemale; }

	// The raw text VtMB would show for this player: the Malkavian variant (col-12) when the player is
	// Malkavian and it exists, otherwise the gendered col-1/2. Same string for NPC subtitles and PC
	// choices — VtMB re-skins both for a Malkavian PC. Still stage-directions-included (raw).
	const FString& RawFor(bool bMale, bool bMalk) const
	{
		return (bMalk && !TextMalkavian.IsEmpty()) ? TextMalkavian : Text(bMale);
	}

	// The on-screen text — RawFor() with the `[...]` VO stage directions stripped, matching what VtMB
	// shows the player. Return by value (a fresh transformed string).
	FString DisplayText(bool bMale, bool bMalk) const { return ElysiumDlgText::StripStageDirections(RawFor(bMale, bMalk)); }
};

// A whole parsed `.dlg` file: rows in file order plus an id -> index map for link resolution.
struct FElysiumDlgFile
{
	TArray<FElysiumDlgLine> Lines;
	TMap<int32, int32> IndexById;   // line id -> index into Lines
	FString SourcePath;             // for logs (empty for an in-memory parse)

	// Parse a raw `.dlg` byte buffer (Latin-1, CRLF rows, `}{`-joined 13-field records). Tolerates a
	// 14-field row (the corpus-wide `kiki.dlg` typo) by ignoring the extras. Never fails on content;
	// returns false only on an empty buffer. OutError, when given, carries a note.
	static bool ParseBytes(const TArray<uint8>& Bytes, FElysiumDlgFile& Out, FString* OutError = nullptr);
	// Load + parse a file off disk. False (with OutError) when the file cannot be read.
	static bool LoadFile(const FString& Path, FElysiumDlgFile& Out, FString* OutError = nullptr);

	bool IsValid() const { return Lines.Num() > 0; }
	int32 IndexOfId(int32 Id) const { const int32* P = IndexById.Find(Id); return P ? *P : INDEX_NONE; }
	const FElysiumDlgLine* FindById(int32 Id) const { const int32 I = IndexOfId(Id); return I != INDEX_NONE ? &Lines[I] : nullptr; }
};

// The dlgexpr front-normalizer (raw field text -> pure-Python source for the script host). Pure string
// transforms — no evaluation — so they unit-test directly.
namespace ElysiumDlgExpr
{
	// A field-4 condition. Skillchecks (`Seduction 7`, `Humanity >= 5`) -> `pc.CalcFeat("Seduction") >= 7`;
	// an `M_`/`F_`-prefixed one carries the engine dependency's sex gate (`M_Persuasion 3` ->
	// `(pc.IsMale() and pc.CalcFeat("Persuasion") >= 3)`);
	// the top-level dlgexpr joins `&` -> `and`, `|` -> `or`. Empty -> empty (the caller treats empty as
	// an open gate). A shape it cannot classify is returned unchanged (host error-to-false).
	FString ConditionToPython(const FString& Raw);

	// A field-5 action list. The statement separator `;` is kept; the rare action-level `&` -> `;`.
	// Skillchecks do not occur in actions, but a stray one is rewritten for safety. Empty -> empty.
	FString ActionToPython(const FString& Raw);
}

// The conversation branch state machine (docs/vtmb/game_runtime.md §5 "Runtime / branching"), driven by
// injected callbacks so it is host-agnostic and unit-testable:
//   * CondFn(rawCondition) -> bool : evaluate a PC choice's col-4 (the caller normalizes + routes to the
//                                    script host, or fakes it in a test). Also evaluates starting sentinels.
//   * ActFn(rawAction)            : execute an NPC line's col-4/col-5 or a chosen PC row's col-5. Never
//                                    called for an empty string.
//   * StartFallbackFn()           : run the owner NPC's non-empty `usescript`; unset means there is no
//                                    usescript, while a set value (including 0) is its integer result.
class FElysiumDlgConversation
{
public:
	using FCondFn = TFunction<bool(const FString& RawCondition)>;
	using FActFn = TFunction<void(const FString& RawAction)>;
	using FStartFallbackFn = TFunction<TOptional<int32>()>;

	FElysiumDlgConversation(TSharedRef<const FElysiumDlgFile> InFile, bool bInPlayerMale,
		bool bInPlayerMalkavian, FCondFn InCond, FActFn InAct,
		FStartFallbackFn InStartFallback = FStartFallbackFn());

	// Recompute the retail state-based opener, then run that NPC line's col-4 + col-5 actions and gather
	// its passing PC choices. Physical row order is authored control flow: first passing valid sentinel wins.
	void Start();

	// The NPC line currently being spoken, or null before Start()/after the conversation closes.
	const FElysiumDlgLine* CurrentNpcLine() const;
	// Indices (into File->Lines) of the visible PC choices for the current NPC line, in author order.
	const TArray<int32>& VisibleChoices() const { return VisibleChoiceIndices; }
	// Convenience: resolve the Nth visible choice to its line, or null if out of range.
	const FElysiumDlgLine* VisibleChoice(int32 VisibleIndex) const;

	// A passing editor-generated automatic row belongs to the current NPC turn but is not displayed.
	// The world resolves it only after that turn's voice finishes.
	bool IsAwaitingAutomatic() const { return PendingAutomaticIndex != INDEX_NONE && !bOver; }
	const FElysiumDlgLine* PendingAutomatic() const;
	// The current NPC line has neither passing choices nor an automatic continuation — it is the last
	// thing said; the next explicit advance ends it.
	bool IsTerminalLine() const
	{
		return CurrentIndex != INDEX_NONE && VisibleChoiceIndices.Num() == 0
			&& PendingAutomaticIndex == INDEX_NONE && !bOver;
	}
	// The conversation has ended (a link-0 pick, an unresolved link, or a closed terminal line).
	bool IsOver() const { return bOver; }

	// Player picks the Nth visible choice: run its col-5 action, then follow its link (0 -> end).
	void Choose(int32 VisibleIndex);
	// Run the pending Auto-Link/Auto-End row's action once and follow its link. No-op unless the
	// current NPC turn selected an automatic row.
	void ResolveAutomatic();
	// Advance past a terminal NPC line (the "continue" affordance) — ends the conversation.
	void AdvanceTerminal();
	// End the conversation immediately (e.g. the owner NPC dies). Idempotent.
	void Close();

	// Bumped on every state change (Start / Choose / AdvanceTerminal / Close) so the UI refreshes only
	// when the turn actually changed rather than every frame.
	uint32 Revision() const { return Rev; }

	bool PlayerMale() const { return bMale; }
	// Whether the player is Malkavian — selects col-12 for on-screen text (FElysiumDlgLine::DisplayText).
	bool PlayerMalkavian() const { return bMalk; }
	const FElysiumDlgFile& File() const { return *DlgFile; }

private:
	int32 SelectStartingLineIndex() const;
	void EnterNpcLine(int32 LineIndex);   // exec its actions, gather passing choices, mark terminal if none
	void FollowPcLine(const FElysiumDlgLine& Line); // exec action, follow numeric link (0 -> close)
	bool PassesGate(const FString& RawCondition) const;  // empty -> true; else CondFn

	TSharedRef<const FElysiumDlgFile> DlgFile;
	bool bMale = true;
	bool bMalk = false;
	FCondFn CondFn;
	FActFn ActFn;
	FStartFallbackFn StartFallbackFn;

	int32 CurrentIndex = INDEX_NONE;      // index into Lines of the current NPC line
	TArray<int32> VisibleChoiceIndices;
	int32 PendingAutomaticIndex = INDEX_NONE;
	bool bOver = false;
	uint32 Rev = 0;
};
