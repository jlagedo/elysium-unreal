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

// The seven clan text columns (cols 6-12), in `CDialog::clan_offset` order — the order
// `read_line_data` (`0x100e61d0`) fills them and `get_display_text` (`0x100e1ad0`) indexes them.
// Slot 6 (col-12) is the Malkavian column the whole corpus authors; slot 5 (col-11, Ventrue) is
// the only other one shipped data fills (8 rows in `prince1.dlg`).
namespace ElysiumDlgClan
{
	inline constexpr int32 Num = 7;
	inline constexpr int32 Brujah = 0;
	inline constexpr int32 Gangrel = 1;
	inline constexpr int32 Nosferatu = 2;
	inline constexpr int32 Toreador = 3;
	inline constexpr int32 Tremere = 4;
	inline constexpr int32 Ventrue = 5;
	inline constexpr int32 Malkavian = 6;
	inline constexpr int32 None = INDEX_NONE;

	// The 2..8 sheet encoding (`FElysiumSheet::Clan()`) -> the `clan_offset` the `.dlg` columns and
	// `TestSimple`'s Ventrue gate are indexed by. An unset/invalid clan answers `None`.
	int32 OffsetFromSheetClan(int32 SheetClan);
}

// One parsed `.dlg` row (13 fields: the six leading columns plus the seven clan text columns).
struct FElysiumDlgLine
{
	int32 Id = 0;               // col-0 — line id, unique in file
	FString TextMale;           // col-1 — spoken text (male-PC variant): NPC subtitle / PC choice text
	FString TextFemale;         // col-2 — spoken text (female-PC variant); empty falls back to col-1
	FString Link;               // col-3 — raw link ("#", "0", "87", "")
	FString Condition;          // col-4 — PC: dlgexpr gate (eval). NPC: an action (exec).
	FString Action;             // col-5 — action(s) run when spoken/chosen (`;`-separated)
	// cols 6-12 — the seven clan variants of this row's text, in clan_offset order. `TextClan[6]`
	// is the Malkavian column every hub authors; `TextMalkavian()` is the name the rest of the
	// runtime already uses for it.
	FString TextClan[ElysiumDlgClan::Num];
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

	// The Malkavian column (col-12). Kept as an accessor because it is the only clan column the
	// runtime named before the other six were recovered.
	const FString& TextMalkavian() const { return TextClan[ElysiumDlgClan::Malkavian]; }
	FString& TextMalkavian() { return TextClan[ElysiumDlgClan::Malkavian]; }

	// The raw text VtMB would show for this player (`get_display_text` `0x100e1ad0`): the player's
	// own clan column when that column is filled, otherwise the gendered col-1/2. Same string for
	// NPC subtitles and PC choices — VtMB re-skins both. Still stage-directions-included (raw).
	// `ClanOffset` is `ElysiumDlgClan::None` for "no clan".
	const FString& RawFor(bool bMale, int32 ClanOffset) const
	{
		if (ClanOffset >= 0 && ClanOffset < ElysiumDlgClan::Num && !TextClan[ClanOffset].IsEmpty())
		{
			return TextClan[ClanOffset];
		}
		return Text(bMale);
	}
	// The pre-D4 spelling, kept so callers that only know "is the PC Malkavian" keep working.
	const FString& RawFor(bool bMale, bool bMalk) const
	{
		return RawFor(bMale, bMalk ? ElysiumDlgClan::Malkavian : ElysiumDlgClan::None);
	}

	// The on-screen text — RawFor() with the `[...]` VO stage directions stripped, matching what VtMB
	// shows the player. Return by value (a fresh transformed string).
	FString DisplayText(bool bMale, int32 ClanOffset) const
	{
		return ElysiumDlgText::StripStageDirections(RawFor(bMale, ClanOffset));
	}
	FString DisplayText(bool bMale, bool bMalk) const
	{
		return ElysiumDlgText::StripStageDirections(RawFor(bMale, bMalk));
	}
};

namespace ElysiumDlgText
{
	// The **text-column take letter** this line resolves to for this player — the `_col_<C>` suffix
	// of `sound/character/<dlgpath>/line<id>_col_<C>.<ext>` (`0x100e15c0`, the column chooser behind
	// `generate_speech_filename` `0x100e1680`). It is the column actually displayed, never a
	// language: the clan letter when that clan column is filled, else `f` when a female PC has a
	// col-2 variant, else `e`.
	//
	// Only two clan letters are pinned by shipped audio — `m` Ventrue and `n` Malkavian — which is
	// consistent with `letter = 'h' + clan_offset`, but the other five are unattested and nothing
	// probes them: an unpinned clan column answers `e` and logs once, so a wrong guess cannot
	// silently mute a line.
	TCHAR ChosenTakeLetter(const FElysiumDlgLine& Line, bool bMale, int32 ClanOffset);
}

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

// --- The col-4 dependency (`CDialogDependency`) -------------------------------------------------
//
// Retail does NOT rewrite col-4 into script text: `CDialogDependency::Parse` (`0x100e8fc0`) splits
// it into at most two halves on the FIRST `|` (compound OR) or `&` (compound AND), and hands each
// half to `ParseDep` (`0x100e9290`). A half whose first whitespace token resolves in the four
// `stats.txt` containers or in `feats.txt` (`0x102047d0` -> `0x10204570`) becomes the **simple
// dependency** — a trait class, a trait id, a threshold, an inversion flag and a sex gate; every
// other half is the **Python dependency**, evaluated with the integer-only truth rule. Whichever
// half claimed the simple slot first fixes the evaluation precedence.
//
// This is why the string rewrite could not work: the threshold's `-` is a MARKER, not a relop
// (`0x102047d0` replaces it with a space and raises the inversion flag), so the 459 corpus
// conditions spelled `Humanity -8` — every authored low-humanity and failure route — were left
// unrecognised and never passed.

enum class EElysiumDlgTraitClass : uint8
{
	Unknown,      // the half resolved no trait: it is the Python dependency (`CVStatRef` class -1)
	Attribute,    // CVStatRef class 0 — `stats.txt` Attributes (Humanity, Appearance, Wits, …)
	Ability,      // class 1 — Abilities (Brawl, Firearms, …)
	Discipline,   // class 2 — Disciplines (Dominate, Dementation, Presence, Thaumaturgy)
	Feat,         // class 4 — `feats.txt` (Persuasion, Seduction, Intimidate, Haggle, Research)
};

const TCHAR* ElysiumDlgTraitClassName(EElysiumDlgTraitClass Class);

namespace ElysiumDlgFeat
{
	// `feats.txt` file order 22 (`Frenzy_Feat`). `TestSimple` (`0x100e9760`) does NOT compare it
	// like every other feat: class-4 id `0x16` routes to `CBaseCombatCharacter::FrenzyComparison`,
	// which reads the frenzy state machine. Named in the header because presentation has to know
	// this front is not a rating it can label.
	inline constexpr int32 Frenzy = 0x16;
}

// `CDialogDependency+0x224`: the `M_`/`F_` prefix on the check name, which is a required-gender
// field on the dependency and not part of the trait name. 0 = requires female, 1 = requires male.
enum class EElysiumDlgSexGate : uint8 { None, Male, Female };

// `m_CompoundType` (`+0x21c`). Retail spells it 1 "one only" / 2 AND / 3 OR; which of the two
// halves the "one" is, is `Precedence`.
enum class EElysiumDlgCompound : uint8 { SkillOnly, PythonOnly, And, Or };

// `m_CompoundPrecedence` (`+0x220`), fixed by which half claimed the simple slot first: 1 the
// skill check is evaluated first, 2 the Python half is.
enum class EElysiumDlgPrecedence : uint8 { SkillFirst, PythonFirst };

// Name -> (class, id), the `CVStatRef` resolver walk (`0x10204570`): the four `stats.txt`
// containers in declaration order, then `feats.txt`. Injected so the parser stays UObject-free —
// the game binds the rulebook, a test binds a fake.
class IElysiumDlgTraitResolver
{
public:
	virtual ~IElysiumDlgTraitResolver() = default;

	// Case-insensitive, on the name with the `M_`/`F_` gate prefix already stripped. False means
	// "no container and no feat owns this name", which is retail's signal that the half is Python.
	virtual bool ResolveTrait(const FString& Name, EElysiumDlgTraitClass& OutClass,
		int32& OutId) const = 0;

	// The discipline slot's authored name, for the diagnostic that keeps the hard-coded
	// "discipline id 6 requires Ventrue" arm honest against a re-ordered `stats.txt`. Empty when
	// the resolver cannot name it.
	virtual FString DisciplineName(int32 Id) const { return FString(); }
};

// The character sheet as a dependency reads it — `CBaseCombatCharacter`'s stat lists and
// `Feats::FeatValue`, plus the two writes `pc_charge_dependency` (`0x100e8b90`) makes. Injected for
// the same reason the resolver is.
class IElysiumDlgSheet
{
public:
	virtual ~IElysiumDlgSheet() = default;

	// `Feats::FeatValue` for a class-4 dependency. By NAME, because that is the accessor the
	// runtime already owns; file order is the feat id and the two agree.
	virtual int32 CalcFeat(const FString& FeatName) const = 0;
	// The CURRENT value of an Attributes/Abilities slot (`CVStatList_t::GetCurrent`).
	virtual int32 Stat(EElysiumDlgTraitClass Class, int32 Id) const = 0;
	// The CURRENT value of a Disciplines slot.
	virtual int32 Discipline(int32 Id) const = 0;
	// Attributes slot `0xc` — the second half of every discipline check.
	virtual int32 BloodPool() const = 0;
	virtual bool IsMale() const = 0;
	// `CDialog::clan_offset` — 0..6 in `ElysiumDlgClan` order, or `ElysiumDlgClan::None`.
	virtual int32 ClanOffset() const = 0;

	// The pick-time charge. Default no-ops so a read-only fake needs neither.
	virtual void SpendBlood(int32 Points) {}
	virtual void AddFakedDisciplineEffect(const FString& Trait, int32 DisciplineId, int32 Level) {}
};

// What `Explain()` reports so presentation never evaluates anything (M-REQ / M-DISABLED).
struct FElysiumDlgGateLabel
{
	FString Trait;          // the authored spelling, gate prefix stripped ("Persuasion")
	EElysiumDlgTraitClass Kind = EElysiumDlgTraitClass::Unknown;
	int32 Required = 0;     // the authored threshold
	int32 Have = 0;         // the player's rating for that trait
	int32 BloodCost = 0;    // the discipline's blood price (= Required), 0 otherwise
	// The player's CURRENT blood pool (attributes slot `0xc`, `TestSimple`'s second discipline
	// half). Carried on the label because M-REQ's disabled row has to say WHICH half is short: a
	// `[ DOMINATE 3/2 ]` row whose rating passes is greyed for the pool, and printing only the
	// price would read as a contradiction.
	int32 Pool = 0;
	// The discipline half that failed is the pool, not the rating: `Have >= Required` but
	// `Pool < BloodCost`. Presentation prints `· <Pool>/<BloodCost> BLOOD` rather than
	// `· <BloodCost> BLOOD` when this is set.
	bool bBloodShort = false;
	bool bValid = false;    // there is a labellable (non-inverted) skill front
	bool bCharged = false;  // this row's charge has been taken (set by Charge())
};

struct FElysiumDlgGateResult
{
	bool bPasses = false;
	// The gate fails ONLY on the skill front: the same evaluation with the skill component forced
	// true would pass. This is the whole of M-DISABLED's rule and is computed in one pass here so
	// presentation never re-evaluates a Python half.
	bool bSkillFailedOnly = false;
	FElysiumDlgGateLabel Label;
};

struct FElysiumDlgDependency
{
	// The simple (skill-check) half.
	FString Trait;                                        // authored spelling, gate prefix stripped
	EElysiumDlgTraitClass Class = EElysiumDlgTraitClass::Unknown;
	int32 TraitId = INDEX_NONE;                           // `+0x08`
	int32 Threshold = 0;                                  // `+0x14`
	bool bInverted = false;                               // `+0x0c` — a `-` before the number: `<`
	EElysiumDlgSexGate SexGate = EElysiumDlgSexGate::None; // `+0x224`
	int32 BloodCost = 0;                                  // `+0x18` — Threshold for a discipline

	// The Python half, normalised for the installed host exactly as before. Retail holds ONE
	// buffer (`+0x1c`), so when both halves are Python the second overwrites the first — see
	// `Parse`'s note.
	FString Python;

	EElysiumDlgCompound Compound = EElysiumDlgCompound::PythonOnly;
	EElysiumDlgPrecedence Precedence = EElysiumDlgPrecedence::PythonFirst;

	bool bEmpty = true;      // col-4 was blank — the caller's open gate, never evaluated
	bool bHasSkill = false;
	bool bHasPython = false;
	// A corpus diagnostic, not a retail field: a half shaped exactly `[gate]IDENT [-]INT` whose
	// name no container and no feat owns. Zero across the shipped 147 files.
	bool bUnresolvedSkillFront = false;

	// `CDialogDependency::Parse` + `ParseDep`. A null resolver leaves every half Python, which is
	// what a world with no rulebook can honestly answer.
	static FElysiumDlgDependency Parse(const FString& RawCondition,
		const IElysiumDlgTraitResolver* Resolver);

	// The Python half's verdict. The caller normalises + routes to the script host and applies the
	// integer-only truth rule (`FElysiumVariant::IsPythonCheckTrue`), or fakes it in a test.
	using FPythonFn = TFunctionRef<bool(const FString& PythonSource)>;

	// `CDialogDependency::Test` (`0x100e94e0`) over `TestSimple` (`0x100e9760`) + `TestPython`.
	bool Test(const IElysiumDlgSheet* Sheet, FPythonFn Python) const;

	// Test() plus the M-DISABLED counterfactual and the M-REQ label, in ONE evaluation. The Python
	// half is evaluated at most once even where retail's short-circuit would have skipped it —
	// that single extra eval is what buys the disabled row (a named consequence of M-DISABLED).
	FElysiumDlgGateResult Explain(const IElysiumDlgSheet* Sheet, FPythonFn Python) const;

	// The skill front presentation labels and M-DISABLED tests: present, resolved and not an
	// authored failure route.
	bool HasLabelledSkillFront() const
	{
		return bHasSkill && !bInverted && Class != EElysiumDlgTraitClass::Unknown
			&& !IsUnrecoveredFrenzyFront();
	}

	// The frenzy seam (`TestSimple`'s class-4 id `0x16` arm -> `FrenzyComparison`). Nothing in the
	// port answers it yet, so a non-inverted frenzy check reads false for a reason that is the
	// PORT's, not the player's: showing it as a disabled `[ FRENZY 0/1 ]` row would advertise a
	// requirement retail never shows and would put a row on screen retail hides. It stays out of
	// the labelled-skill-front set — hidden, exactly as a failing Python gate is — until
	// `CBaseCombatCharacter::FrenzyComparison` is recovered.
	// TODO(dialogue-plan): recover FrenzyComparison, then drop this exclusion.
	bool IsUnrecoveredFrenzyFront() const
	{
		return Class == EElysiumDlgTraitClass::Feat && TraitId == ElysiumDlgFeat::Frenzy;
	}

	// `pc_charge_dependency` (`0x100e8b90`): subtract the blood cost from the pool and, for a
	// discipline, fire the faked-effect seam on the conversation partner. No-op for every other
	// dependency. Called by `FElysiumDlgConversation::Choose`.
	void Charge(IElysiumDlgSheet* Sheet) const;
};

// One row of the response band, enabled or disabled. Author order, numbered 1..N across both
// (M-DISABLED: the numbering is stable whether or not the player has the skill).
struct FElysiumDlgVisibleChoice
{
	int32 LineIndex = INDEX_NONE;   // index into File().Lines
	bool bEnabled = true;           // false = shown greyed, unpickable, carrying its label
	FElysiumDlgGateResult Gate;
};

// The conversation branch state machine (docs/vtmb/game_runtime.md §5 "Runtime / branching"), driven by
// injected callbacks so it is host-agnostic and unit-testable:
//   * CondFn(pythonHalf) -> bool  : evaluate the PYTHON half of a col-4 dependency (the caller
//                                    normalizes + routes to the script host, or fakes it in a test).
//                                    The skill-check half never reaches it — that is the sheet's.
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
	//
	// IDEMPOTENT. The opening line's col-4 is an ACTION (`process_npc_line` `0x100e8100`), and 40
	// shipped openers assign a `G` flag or call a level-script function, so running it twice is a
	// double side effect. The world calls Start() itself so its session exists before that action
	// runs (a col-4 that calls `EndDialog`/`OpenDialog` must act on THIS conversation); a caller
	// that started the conversation first therefore finds the second call a no-op.
	void Start();
	// Whether Start() has already run. False before the opener's col-4 has executed.
	bool HasStarted() const { return bStarted; }

	// Bind the gate context. Must be set before Start() — the opener's own sentinels are dependencies
	// too. A conversation with no sheet still runs: a row with a skill front then fails closed with
	// one log, which is the honest answer for a world that has no character sheet.
	void SetGateContext(TSharedPtr<IElysiumDlgSheet> InSheet,
		TSharedPtr<const IElysiumDlgTraitResolver> InResolver, int32 InClanOffset);

	// The NPC line currently being spoken, or null before Start()/after the conversation closes.
	const FElysiumDlgLine* CurrentNpcLine() const;
	// The response band for the current NPC line, in author order: every enabled row plus the
	// disabled skill-failure rows M-DISABLED keeps visible.
	const TArray<FElysiumDlgVisibleChoice>& VisibleChoices() const { return VisibleChoiceEntries; }
	// Convenience: resolve the Nth visible choice to its line, or null if out of range.
	const FElysiumDlgLine* VisibleChoice(int32 VisibleIndex) const;
	// The `.dlg` line id of the Nth visible row, or INDEX_NONE when the index is out of range.
	// Presentation submits it back with the pick so a band that changed between the frame the
	// player saw and the frame the click arrives cannot silently pick a different sentence.
	int32 VisibleChoiceLineId(int32 VisibleIndex) const;
	// Whether the Nth visible row can be picked. A disabled row's number key does nothing.
	bool IsChoiceEnabled(int32 VisibleIndex) const;
	int32 NumEnabledChoices() const;

	// The band offered rows but gated every one of them out, with no automatic continuation — the
	// authoring gap retail covers by replacing the NPC's own text (`NoValidReplyText`) and showing
	// one dummy response. Presentation substitutes the subtitle; the Continue closes.
	bool NoValidReply() const { return bNoValidReply; }
	static const TCHAR* NoValidReplyText() { return TEXT("I do not have a valid reply."); }

	// A passing editor-generated automatic row belongs to the current NPC turn but is not displayed.
	// The world resolves it only after that turn's voice finishes.
	bool IsAwaitingAutomatic() const { return PendingAutomaticIndex != INDEX_NONE && !bOver; }
	const FElysiumDlgLine* PendingAutomatic() const;
	// The current NPC line has neither passing choices nor an automatic continuation — it is the last
	// thing said; the next explicit advance ends it.
	bool IsTerminalLine() const
	{
		return CurrentIndex != INDEX_NONE && NumEnabledChoices() == 0
			&& PendingAutomaticIndex == INDEX_NONE && !bOver;
	}
	// The conversation has ended (a link-0 pick, an unresolved link, or a closed terminal line).
	bool IsOver() const { return bOver; }

	// The current NPC turn's parked col-5. `process_npc_line` (`0x100e8100`) runs the row's col-4
	// immediately and stashes col-5 at `+0x30ea`; `CallPendingNPCEventScript` (`0x100e5c70`) runs it
	// once, when the NPC finishes talking, on `NPCNotifyDoneTalking`, or on `Release`. The world
	// drives all four edges (voice completion, skip, pick, close).
	bool HasPendingNpcAction() const { return !PendingNpcAction.IsEmpty(); }
	void FlushPendingNpcAction();

	// Player picks the Nth visible choice: flush the NPC's parked col-5, charge the dependency,
	// run the row's own col-5, then follow its link (0 -> end). Refuses a disabled row.
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
	// The player's `clan_offset` — selects the clan text column and answers `TestSimple`'s Ventrue
	// gate. `ElysiumDlgClan::None` when the character has no clan.
	int32 PlayerClanOffset() const { return ClanOffset; }
	// Whether the player is Malkavian — the pre-D4 spelling of "clan offset 6".
	bool PlayerMalkavian() const { return ClanOffset == ElysiumDlgClan::Malkavian; }
	const FElysiumDlgFile& File() const { return *DlgFile; }

private:
	int32 SelectStartingLineIndex() const;
	void EnterNpcLine(int32 LineIndex);   // exec col-4, park col-5, gather the response band
	void FollowPcLine(const FElysiumDlgLine& Line); // exec action, follow numeric link (0 -> close)
	bool PassesGate(const FString& RawCondition) const;  // empty -> true; else the dependency
	FElysiumDlgGateResult ExplainGate(const FString& RawCondition) const;

	TSharedRef<const FElysiumDlgFile> DlgFile;
	bool bMale = true;
	int32 ClanOffset = ElysiumDlgClan::None;
	TSharedPtr<IElysiumDlgSheet> Sheet;
	TSharedPtr<const IElysiumDlgTraitResolver> TraitResolver;
	FCondFn CondFn;
	FActFn ActFn;
	FStartFallbackFn StartFallbackFn;

	int32 CurrentIndex = INDEX_NONE;      // index into Lines of the current NPC line
	TArray<FElysiumDlgVisibleChoice> VisibleChoiceEntries;
	int32 PendingAutomaticIndex = INDEX_NONE;
	FString PendingNpcAction;             // the current NPC row's parked col-5
	bool bNoValidReply = false;
	bool bStarted = false;
	bool bOver = false;
	uint32 Rev = 0;
	mutable bool bLoggedMissingSheet = false;
};
