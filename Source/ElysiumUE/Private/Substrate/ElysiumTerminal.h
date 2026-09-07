#pragma once

#include "CoreMinimal.h"

#include "Containers/ArrayView.h"

#include "Substrate/ElysiumSkillClasses.h"
#include "Substrate/ElysiumTerminalScreenBuffer.h"

struct FElysiumTerminalView;
class UPrimitiveComponent;

// The view's reading of the session's input state, derived from retail's `m_HackFlags` and the
// pending-password target (docs/vtmb/computer-terminals.md §8.1, §9). `Raw` is `m_HackFlags 0x4`,
// and computer content DOES raise it: the mail area's open-message render (`FUN_1021c260`) ends in
// `FUN_10219240`, which is the raw-mode sender, so every open email runs in single-key mode and the
// client posts one `hackcmd %c` per press (§12). `CPropKeypad` raises the same flag; it is not
// ported.
enum class EElysiumTerminalInputMode : uint8
{
	Line,
	Password,
	Acknowledge,
	Raw,
};

struct FElysiumTerminalFunction
{
	FString Name;
	FString Description;
	FString RunText;
	FString Dependency;
	FString RunScript;
	int32 Trigger = INDEX_NONE;
};

struct FElysiumTerminalDirectory
{
	FString Name;
	FString Password;
	FString Description;
	FString Dependency;
	int32 Difficulty = 0;
	TArray<FElysiumTerminalFunction> Functions;
};

struct FElysiumTerminalEmail
{
	FString Subject;
	FString Sender;
	FString Body;
	FString Dependency;
	FString RunScript;
	bool bAutoDelete = false;
};

// The `Email` record's field widths and its three compiled-in defaults, from
// `CPropHacking::LoadFromFile` `0x1021cba0` (`docs/vtmb/computer-terminals.md` §12). Each cap is
// the `Q_strncpy` byte count minus the terminator, so `Q_strncpy(dest, src, 0x20)` keeps 31
// characters; the `dependency` / `runscript` pair share the 64-byte slot every other authored
// script string on the entity uses. The defaults are the retail `.rdata` strings at `0x105b078c`,
// `0x105b0760` and `0x105b073c`, and they are what an `Email` block missing that key DRAWS.
namespace ElysiumTerminalEmailCaps
{
	inline constexpr int32 Subject = 31;      // Q_strncpy(..., 0x20)
	inline constexpr int32 Sender = 31;       // Q_strncpy(..., 0x20)
	inline constexpr int32 Body = 511;        // Q_strncpy(..., 0x200)
	inline constexpr int32 Dependency = 63;   // Q_strncpy(..., 0x40)
	inline constexpr int32 RunScript = 63;    // Q_strncpy(..., 0x40)

	inline const TCHAR* DefaultSubject = TEXT("this email has no subject");
	inline const TCHAR* DefaultSender = TEXT("this email has no sender");
	inline const TCHAR* DefaultBody = TEXT("this email has no body");
}

// The other three halves of the same loader (`CPropHacking::LoadFromFile` `0x1021cba0`,
// `docs/vtmb/computer-terminals.md` §12): the top-level keys, the `SubDir` record and the
// `Function` record. Each cap is the `Q_strncpy` byte count minus the terminator, because
// `Q_strncpy(dst, src, n)` keeps n-1 characters — an authored value longer than that is TRUNCATED
// at load and every later measurement (the screensaver's column placement, the directory header's
// own 16-byte copy, the `%s` in a framed row) sees only the truncated copy.
//
// The defaults are the load-time `KeyValues::GetString` fallbacks, and three of them are the KEY
// literal itself — retail passes the same `.rdata` pointer as key and default — so a record that
// omits the line draws that word on the glass:
//   * `1021ceaf` / `1021cf9d`  `name`        cap 0x10, default `"name"`   (`0x1053fd80`)
//   * `1021cefc` / `1021cfe2`  `description` cap 0x20, default `"description"` (`0x105b07d8`)
//   * `1021d003`               `runtext`     cap 0x200, default `"runtext"` (`0x105b07c0`)
// `brackets` (`1021cc8a`) takes the `.rdata` default `"[]"` at `0x105b083c`; every other key
// defaults to the empty string at `0x106b8540`.
//
// The `SubDir` and `Function` records share these three widths exactly (both names are 0x10, both
// descriptions 0x20, both dependencies 0x40), so they are named once here.
namespace ElysiumTerminalCaps
{
	// Top level, into `CPropHacking`'s own fields.
	inline constexpr int32 ScreenSaver = 63;     // 1021cc64  Q_strncpy(this+0x904, ..., 0x40)
	inline constexpr int32 Brackets = 2;         // 1021cc8a  Q_strncpy(this+0x984, ..., 3)
	inline constexpr int32 EmailPassword = 31;   // 1021ccaa  Q_strncpy(this+0x944, ..., 0x20)
	inline constexpr int32 EmailUsername = 31;   // 1021ccc8  Q_strncpy(this+0x964, ..., 0x20)

	// `SubDir` and `Function`.
	inline constexpr int32 Name = 15;            // 1021ceaf / 1021cf9d  Q_strncpy(..., 0x10)
	inline constexpr int32 Description = 31;     // 1021cefc / 1021cfe2  Q_strncpy(..., 0x20)
	inline constexpr int32 Password = 15;        // 1021cf1a  Q_strncpy(..., 0x10)
	inline constexpr int32 Dependency = 63;      // 1021cf38 / 1021d027  Q_strncpy(..., 0x40)
	inline constexpr int32 RunText = 511;        // 1021d003  Q_strncpy(..., 0x200)
	inline constexpr int32 RunScript = 63;       // 1021d048  Q_strncpy(..., 0x40)

	inline const TCHAR* DefaultName = TEXT("name");
	inline const TCHAR* DefaultDescription = TEXT("description");
	inline const TCHAR* DefaultRunText = TEXT("runtext");
	inline const TCHAR* DefaultBrackets = TEXT("[]");

	// `1021cf84`: the `Function` loop stops at twenty records per `SubDir` — the remaining blocks
	// are read by nobody, so a twenty-first function is not authored content at all.
	inline constexpr int32 FunctionsPerDirectory = 20;
}

// `CPropHacking+0x904`: a 64-byte character field, so 63 characters plus the terminator
// (`docs/vtmb/computer-terminals.md` §4.4). The parser truncates to it because retail's `Q_strncpy`
// does, and the screensaver's column placement measures the truncated string.
inline constexpr int32 ElysiumTerminalScreenSaverMax = ElysiumTerminalCaps::ScreenSaver;

// One patch-first `TerminalDefinition`. The parser preserves authored order and raw display/script
// strings; only names used for comparisons fold at the comparison site. It is deliberately plain
// C++ so malformed content and routing rules can be proven without a world, viewport, or RHI.
//
// Retail folds a `SubDir` / `Function` name at LOAD instead — `Q_strnlwr` (`vstdlib 0x10003310`,
// which ignores its count and lowers the whole string) runs on the truncated copy at `1021cef1` /
// `1021cfd7`, so the record never holds the authored case. The port keeps `Name` as authored and
// folds at every site that compares or prints it (`FElysiumPropHacking::LoweredName`), which is
// the same observable text: the two are equivalent because the cap is applied BEFORE the fold in
// retail and `ToLower` is length-preserving. The raw field is what the definition tests and the
// debug rows read back.
struct FElysiumTerminalDefinition
{
	FString ScreenSaver;
	FString Brackets;
	FString EmailPassword;
	FString EmailUsername;
	TArray<FString> LogonLines;
	TArray<FElysiumTerminalDirectory> Directories;
	TArray<FElysiumTerminalEmail> Emails;

	static bool ParseText(const FString& Text, FElysiumTerminalDefinition& Out, FString& OutError);
	static bool Load(const FString& VirtualPath, FElysiumTerminalDefinition& Out, FString& OutError);
};

inline FName ElysiumTerminalClassName()
{
	return FName(TEXT("CBaseTerminal"));
}

// The `Hacking_Strings` table (`docs/vtmb/computer-terminals.md` §16): the localized entry, else
// the compiled-in `STRING_*` key name, else "Unrecognized Command" — `FUN_10219400`'s three-way
// fallback. The rulebook subsystem's copy is used when a game state is in reach; a headless world
// loads the exported table once from the corpus.
namespace ElysiumHackingStrings
{
	constexpr int32 PressHackKey = 0;
	constexpr int32 AvailableMenus = 2;
	constexpr int32 AvailableCommands = 3;
	// The three entries whose obvious names collide with a member of the classes that read them
	// (`PasswordPrompt` / `InvalidCommand` are draw bodies, `Difficulty` is the skill entity's
	// authored field): the §16 index is what matters, so only the local label differs.
	constexpr int32 InvalidCommandTitle = 5;
	constexpr int32 InvalidCommandHint1 = 6;
	constexpr int32 InvalidCommandHint2 = 7;
	constexpr int32 Help1 = 8;
	constexpr int32 Help4 = 11;
	constexpr int32 LoginPrompt = 12;
	constexpr int32 PasswordNotify = 13;
	constexpr int32 PasswordRequiredTitle = 14;
	constexpr int32 InvalidPassword = 15;
	constexpr int32 ValidPassword = 16;
	constexpr int32 HomeDir = 17;
	constexpr int32 Continue = 18;
	constexpr int32 PasswordAccepted = 19;
	constexpr int32 HelpTitle = 20;
	constexpr int32 PasswordExit = 21;
	// The mail bodies (§12): the two body headers, the five hotkey words, the list title.
	constexpr int32 FromHeader = 22;
	constexpr int32 SubjectHeader = 23;
	constexpr int32 NextCmd = 24;
	constexpr int32 PrevCmd = 25;
	constexpr int32 DelCmd = 26;
	constexpr int32 MenuCmd = 27;
	constexpr int32 QuitCmd = 28;
	constexpr int32 EmailTitleBar = 29;
	constexpr int32 EmailDir = 30;
	constexpr int32 EmailCount = 31;
	constexpr int32 Quit = 33;
	constexpr int32 Help = 34;
	constexpr int32 List = 35;
	constexpr int32 Email = 36;
	constexpr int32 DifficultyLabel = 37;
	constexpr int32 SkillInsufficient = 38;
	constexpr int32 EmailPasswordAccepted = 39;
	constexpr int32 MakingHackAttempt = 40;
	constexpr int32 TypePrompt = 42;
	constexpr int32 HomeMenu = 43;
	constexpr int32 Menu = 44;
	// The mail list's three footer rows. `FUN_10219400` leaves these three slots **NULL** — they
	// are the only indices in the table with no compiled-in fallback (`local_c`/`local_8`/`local_4`
	// = 0 at `0x10219400`), so a short `Hacking_Strings` hands `Q_vsnprintf` a NULL format. They
	// resolve through `GetOrEmpty`, never `Get`.
	constexpr int32 MailListCount = 45;
	constexpr int32 MailListMore = 46;
	constexpr int32 MailListExit = 47;

	// **Test-only.** Install a `hacking_strings` table for the headless resolver, ahead of both the
	// rulebook and the exported corpus. Without it a tree with no export resolves every entry to
	// its compiled key name, and the five mail hotkeys — which are the SECOND byte of a bracketed
	// word (`"[n]ext"`) — all collapse onto `T`, so the mail cases have to abstain rather than
	// assert. `Reset` puts the ordinary resolution back; a case that installs must reset.
	void InstallForTests(TArray<FString> Entries);
	void ResetForTests();

	FString Get(const class FElysiumEntityWorld* World, int32 Index);
	// The same lookup with **no** fallback: an absent entry answers empty. This is what the three
	// footer indices take, because retail has no compiled-in label for them at all.
	FString GetOrEmpty(const class FElysiumEntityWorld* World, int32 Index);
	const TCHAR* KeyName(int32 Index);
}

// One argument of a terminal print, as `Q_vsnprintf` receives it.
struct FElysiumTerminalArg
{
	enum class EKind : uint8 { Str, Int, Chr };

	EKind Kind = EKind::Str;
	FString Text;
	int32 Number = 0;
	TCHAR Character = 0;

	FElysiumTerminalArg(const FString& In) : Text(In) {}
	FElysiumTerminalArg(const TCHAR* In) : Text(In) {}
	static FElysiumTerminalArg Num(int32 In)
	{
		FElysiumTerminalArg Arg{ FString() };
		Arg.Kind = EKind::Int;
		Arg.Number = In;
		return Arg;
	}
	static FElysiumTerminalArg Char(TCHAR In)
	{
		FElysiumTerminalArg Arg{ FString() };
		Arg.Kind = EKind::Chr;
		Arg.Character = In;
		return Arg;
	}
};

// `FUN_10217ac0`, the one printer every terminal draw body goes through: it zeroes a 512-byte
// scratch and runs `Q_vsnprintf` into it (docs/vtmb/computer-terminals.md §8.6). Every format it
// is handed is a **runtime** string — a `Hacking_Strings` entry, an authored `runtext`, the
// assembled rule/framed row — which `FString::Printf` cannot take, so this is C's formatter over
// the subset those bodies consume: `%s`, `%d`/`%i`, `%c` and `%%`.
//
// The one semantic the port depends on: a `%c` given a NUL **terminates** the output there, the
// way C does. The tutorial patch authors `brackets ""`, so its prompt prefix (`"%c%s@%s%c "`) and
// its password notify (the `%c`-bracketed format at `0x105b065c`) stop at the first bracket —
// that truncation is exactly what the retail captures show (§8.5, `04-password-failed.png`).
FString ElysiumTerminalFormat(const FString& Format,
	TArrayView<const FElysiumTerminalArg> Args = TArrayView<const FElysiumTerminalArg>());

// The shared exclusive terminal session (`CBaseTerminal`). The derived content leaf owns grammar
// and state; this base owns the current user, serial/revision validation, the body, the save
// block, one idempotent teardown, and the screen: the cell buffer every retail entity message
// lands in (§8.2 / §8.3), the `m_HackFlags` word, the `InfoCtrl` HUD hint (§8.4) and the four
// soundgroup cues (§14).
class FElysiumTerminal : public FElysiumSkillEntity
{
public:
	bool bStartEnabled = true;
	// The AUTHORED grid, before `Spawn` clamps it. The corpus really does author values outside the
	// clamp — la_chantry_1 `textcolumns 72`, hw_sinbin_1 42x32, la_skyline_1's
	// `largemonitor_hackable` 56x32 — and sm_bailbonds_1's `apple_monitor_screen` authors a real
	// in-range 33x23, so neither the grid nor the layout may assume 36x24.
	int32 TextColumns = 36;
	int32 TextRows = 24;
	int32 ColorScheme = 0;
	FString SoundGroup;

	// `CBaseTerminal::Spawn` `0x10217880` clamps the authored grid before anything reads it:
	// columns `if (c < 0x25) c = max(c, 4); else c = 0x24` -> `[4, 36]`, rows
	// `if (r < 0x19) r = max(r, 2); else r = 0x18` -> `[2, 24]`. Pure and static so the boundaries
	// are assertable without standing up map content, and because the whole presentation layout
	// depends on the clamp holding: the rasterizer's block is `columns*14 x rows*16` in a 512x512
	// texture (§8.3), so an unclamped 72-column grid does not fit the glass at all.
	static void ClampGrid(int32& InOutColumns, int32& InOutRows);

	FElysiumEntityHandle CurrentUser;
	uint32 SessionSerial = 0;
	uint32 ViewRevision = 0;

	// `m_HackFlags`: 0x1 acknowledge, 0x2 digits only, 0x4 raw character, 0x8 keystroke click.
	static constexpr uint8 FlagAcknowledge = 0x1;
	static constexpr uint8 FlagDigitsOnly = 0x2;
	static constexpr uint8 FlagRawCharacter = 0x4;
	static constexpr uint8 FlagKeystrokeClick = 0x8;
	uint8 HackFlags = 0;
	// `m_nMaxInput`: 0 = no limit on the local line edit; the router's 16-byte cap is separate.
	int32 MaxInput = 0;

	FElysiumTerminalScreenBuffer Screen;
	int32 HudHintType = 0;
	int32 HudHintValue = 0;

	// --- the client line editor's activation (§8.1.1, TERM20) -----------------------------------
	// Retail's line editor is not implied by the input mode: it is OPENED by entity message 3
	// (`FUN_100c82e0`) and CLOSED by every message that zeroes `+0xe88`. While it is closed,
	// `C_BaseTerminal::vfunc25` `0x100c7090` returns 1 at its third test and no key inserts, moves
	// or draws anything. The authority owns the flag here because it owns the cell buffer the
	// activation reads its origin from.
	//
	// `bLineEditArmed` is `+0xe88` as the type-3 message left it; `LineEditBreakMark` is the
	// buffer's break count at that moment, and a later break makes the arm stale, which is the
	// port's expression of the four handlers that zero the flag. `LineEditEpoch` has no retail
	// field: it is how the widget learns that `+0xed8` was cleared, since the authority cannot
	// reach into the widget's line the way retail's client reaches into its own.
	bool bLineEditArmed = false;
	uint32 LineEditBreakMark = 0;
	uint32 LineEditEpoch = 0;
	int32 LineEditOriginColumn = 0;
	int32 LineEditOriginRow = 0;
	bool IsLineEditActive() const
	{
		return bLineEditArmed && LineEditBreakMark == Screen.LineEditBreaks();
	}

	// The model's `screen` / `screen_axis` attachments in world cm, resolved off the placed body
	// (§7.2, `FUN_10218710`). One pair serves the use gate, the availability query, the use icon and
	// the `Hacking` camera shot, so "the camera sits where the cone measured from" holds by
	// construction rather than by agreement.
	FVector ScreenPointCm = FVector::ZeroVector;
	FVector ScreenAxisPointCm = FVector::ZeroVector;
	bool bScreenAttachmentsResolved = false;
	// Which part is missing, for the one named log line a refused session emits. Null while the
	// pair resolved.
	const TCHAR* AttachmentError = nullptr;

	// The `Hacking` shot's director handle while a session is live, 0 otherwise. Retail stores its
	// `camera_cinematic` on the player (`player+0x1ec4`), not in the script-camera slot, so this is
	// its own stacked handle and a `SetCamera` cutscene cannot be clobbered by a terminal.
	int32 CameraShot = 0;

	// `CBaseEntity`'s default held-use reach, slot 37 (`CAISound::FUN_10026710` -> `_DAT_104454c8`
	// = **80.0** Source units, read from the module's `.rdata`). `CBasePlayer::PlayerUse`'s
	// maintenance arm (`FUN_10167e00` 10167eb1) dispatches the PIN (slot 43) while the manhattan XY
	// distance from the player's eye to the nearest point on the held entity's collision bounds is
	// at or beyond it, and the view snap (slot 41) inside it. A terminal overrides neither 36 nor
	// 37, so this is the value it runs on.
	static constexpr float HoldReachCm = 80.0f * 2.54f;

	virtual void Spawn() override;
	virtual void Activate() override;
	virtual bool IsUsable() const override { return bStartEnabled; }
	virtual bool CanPlayerFocus(const FElysiumUseContext& Context) const override;
	// Slot 34 `0x10218690` — availability. `m_bEnabled`, then reject if `this+0x8c` resolves to
	// **any** live entity (not "any entity other than the requester"), then the cone. It never looks
	// at the requester's player component.
	virtual bool CanBeUsed(const FElysiumUseContext& Context) const override;
	// Slot 35 `CBaseTerminal::ObjectCaps` `0x10218660` — `return -((char)cone != 0) & 2`. The cone
	// alone: no enable test, no user test.
	virtual bool HasUseIconCaps(const FElysiumUseContext& Context) const override;
	virtual FElysiumUseBeginResult BeginPlayerUse(const FElysiumUseContext& Context) override;
	virtual void EndPlayerUse(const FElysiumUseContext& Context, EElysiumUseEndReason Reason) override;
	virtual void Serialize(FElysiumSaveArchive& Ar) override;
	virtual void OnDormancyChanged() override;
	virtual void OnRuntimeTransformChanged() override;
	virtual void OnRuntimeModelChanged() override;
	virtual void TickPlayerUse(const FElysiumUseContext& Context) override;
	// Slot 44 (`+0xb0`) is NOT overridden: `CBaseTerminal` inherits `CAISound::FUN_100267b0` =
	// `return 1`, which is the base's own answer here (correction C11).
	virtual bool GetBodyAttachmentPoint(FName Attachment, FVector& OutWorld) const override;
	virtual UPrimitiveComponent* GetAttachBody() const override;
	virtual const TCHAR* SaveBlockReason() const override;
	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override;
	virtual FElysiumTerminal* AsTerminal() override { return this; }

	void InputEnable();
	void InputDisable();
	bool Submit(uint32 ExpectedSerial, const FString& Command);
	bool BeginHack(uint32 ExpectedSerial);
	void BuildView(FElysiumTerminalView& Out) const;
	// The same projection with no session bound: owner, serial 0, the grid and the revision. This is
	// what the world publishes for a terminal nobody is using, so the screensaver the authority is
	// drawing reaches the glass without anyone standing at it.
	void BuildIdleView(FElysiumTerminalView& Out) const;
	EElysiumTerminalInputMode InputMode() const;

	// The authored `screen saver` label the idle glass draws. On the base there is none; the content
	// leaf answers with its own. Published on BOTH the idle and the live view, because the label is
	// the only piece of content state an idle monitor needs and asking for it must not drag the
	// session's action list — and its dependency evaluation — in with it.
	virtual const FString& ScreenSaverLabel() const;

	// Re-read `screen` / `screen_axis` off the standing body. Called from `Spawn`, and again from
	// every hook that can move or replace that body.
	void ResolveScreenAttachments();
	// One named warning naming entity, model and the missing part — raised at spawn and after a
	// model change, never per use press.
	void ReportMissingAttachments() const;
	// `FUN_10218710` itself: the plan-view screen cone, with no enable and no user test. The three
	// gate bodies differ only in what they ask BEFORE this.
	bool FacesScreen(const FElysiumUseContext& Context) const;
	// One named warning, once per entity: a live terminal with no body has nowhere to put its glass,
	// so the screensaver its think is drawing reaches nobody. Reported where the publication would
	// otherwise have skipped it silently.
	void ReportBodilessGlass() const;

	// --- the retail entity messages, as writes into `Screen` (§8.2) ---
	void ScreenSetCursor(int32 Column, int32 Row) { Screen.SetCursor(Column, Row); }
	void ScreenPrint(const FString& Text) { Screen.Print(Text); }
	void ScreenClear() { Screen.Clear(); }
	void ScreenStyleDefault() { Screen.SetStyleDefault(); }
	void ScreenStyleAlternate() { Screen.SetStyleAlternate(); }
	void ScreenSetMargins(int32 Left, int32 Right) { Screen.SetMargins(Left, Right); }
	void ScreenEcho(TCHAR Character) { Screen.Echo(Character); }
	// `FUN_10219120` (leave acknowledge/raw), `FUN_10219240` (raw), `FUN_10219270` (acknowledge).
	// All three send entity message **type 3** first, because the two mode senders open with a call
	// to `FUN_10219120` — so opening the client's line editor is not a line-mode-only act, and a
	// raw-character prompt (the open mail message, `FUN_1021c260`) is editable in exactly the same
	// sense: the client's `+0xe88` has to be set or `0x100c7090` eats the key with no send.
	void EnterLineEdit();
	void EnterRawCharacter() { EnterLineEdit(); HackFlags |= FlagRawCharacter; }
	void EnterAcknowledge() { EnterLineEdit(); HackFlags |= FlagAcknowledge; }
	// `FUN_10218820` — the `InfoCtrl` usermessage (§8.4).
	void SetHudHint(int32 Type, int32 Value);
	// The hint as the client's `InfoCtrl` handler resolves it (§8.4), so the HUD draws a line rather
	// than re-deriving one from a type byte. Empty = hidden.
	FString HudHintLine() const;
	// `FUN_101f5950` on the entity's soundgroup: `access`, `accept`, `error`, `typing` (§14).
	void PlayCue(const TCHAR* Cue);
	// The stable audio-owner id every cue is submitted under, and the one the exit's stop-by-owner
	// names. One accessor, because those two have to agree for the stop to reach the cues.
	FString CueOwnerId() const;
	// Whether this entity's `soundgroup` resolved the named cue. A world with no exported
	// `usable/soundgroups.json` resolves none, which is a seam a case names rather than a failure.
	bool HasCue(const TCHAR* Cue) const
	{
		const FString* Rel = CueRels.Find(FName(Cue));
		return Rel != nullptr && !Rel->IsEmpty();
	}

	// --- the shared draw helpers (§8.5 / §8.6) ---
	// `FUN_1021b140`: margins (1,1), clear, and the framed box around `Title` — or around every
	// `LogonScreen` line when `Title` is null.
	void TitleBox(const FString* Title, const TArray<FString>& LogonLines);
	void RuleRow();
	void FramedRow(const FString& Text, int32 Margin);

protected:
	virtual bool OpenContent(FString& OutError) { return true; }
	virtual void BeginContentSession() {}
	virtual void EndContentSession() {}
	virtual bool SubmitContent(const FString& Command) { return false; }
	virtual bool BeginContentHack() { return false; }
	virtual void BuildContentView(FElysiumTerminalView& Out) const {}
	virtual EElysiumTerminalInputMode ContentInputMode() const;
	// The shared body of `BuildView` / `BuildIdleView`; `Serial` is the only difference.
	void FillView(FElysiumTerminalView& Out, uint32 Serial) const;

	// The body build and its teardown, split out of `Spawn` so a runtime `SetModel` can replay the
	// whole thing the way `FElysiumProp::OnRuntimeModelChanged` does — a re-registered anchor on a
	// STALE body would leave the glass bound to a component the model change replaced.
	void BuildBody();
	void DestroyBody();

	UPrimitiveComponent* WorldBody = nullptr;
	FString VisualStem;
	TMap<FName, FString> CueRels;
	// Latch for `ReportBodilessGlass`: the defect is per entity, not per frame.
	mutable bool bReportedBodilessGlass = false;
};

// `prop_hacking` (`CPropHacking`): ordered TerminalDefinition content, the directory / password
// state, the retail router (`AcceptCmd` 0x1021a830), every draw body on the cell screen, the
// deterministic hacking bypass with its visible cracking stepper, the Function transaction, and
// the persistent unlock / attempt state.
class FElysiumPropHacking final : public FElysiumTerminal
{
public:
	FString HackFile;
	bool bGlobalEmail = false;
	float ScreenSaverDelay = 1.5f;
	float ScreenSaverStart = 5.0f;

	// `_DAT_10449400`, a double `2.0` written back as the float `2.0f` (`0x40000000`). Activate is the
	// only place it is applied, and only AFTER the first tick has already been scheduled; neither the
	// exit re-arm nor `ss_start` is ever floored (correction C15, `docs/vtmb/computer-terminals.md`
	// §13).
	static constexpr float ScreenSaverDelayFloor = 2.0f;

	FElysiumTerminalDefinition Definition;
	TArray<uint8> DirectoryUnlocked;
	TArray<int32> DirectoryAttempts;
	// `+0x9dc`: -1 root, -2 the mail area, else a directory index.
	int32 CurrentDirectory = INDEX_NONE;
	// `+0x9e0`: -1 none, -2 mail, else the directory awaiting its password.
	int32 PendingDirectory = INDEX_NONE;
	static constexpr int32 MailArea = -2;
	// `m_bEmailUnlocked` (`+0xc2c`) and `m_nEmailAttempts` (`+0xc28`), both saved. The attempt
	// counter is **write-only** in retail — `FUN_1021ca90` zeroes it, the password failure arm
	// increments it, and nothing ever reads it; it is serialized and inert.
	bool bEmailUnlocked = false;
	int32 EmailAttempts = 0;

	// --- the mail state (`docs/vtmb/computer-terminals.md` §12) ---
	// `m_EmailFlags` (`+0xa28`): `DEFINE_ARRAY(FIELD_INTEGER, 128)`, bit `0x1` read, bit `0x2`
	// deleted. There is no third bit and no clear accessor anywhere in the module — a mail cannot
	// be un-read or un-deleted. The array is 128 while the record vector is unbounded, and the
	// accessors answer FALSE out of range rather than clamping, so email #129 and up are
	// permanently unread and never deleted.
	static constexpr int32 EmailFlagCount = 128;
	static constexpr int32 EmailFlagRead = 0x1;
	static constexpr int32 EmailFlagDeleted = 0x2;
	TArray<int32> EmailFlags;

	// The transient half, none of which is in retail's datamap: the selected visible row
	// (`+0x9f0`), the open mail's REAL record index (`+0x9f4`, `-1` = the list state), the
	// visible-index table (`+0x9f8`, rebuilt on every list and directory draw) and the page
	// (`+0xa0c`, ten rows to a page). OnUseEnd resets none of them, so they survive a session.
	int32 MailSelectedRow = 0;
	int32 MailOpenIndex = INDEX_NONE;
	TArray<int32> MailVisible;
	int32 MailPage = 0;

	FElysiumPropHacking() { EmailFlags.SetNumZeroed(EmailFlagCount); }

	// `FUN_1021a4b0` / `FUN_1021a4f0` / `FUN_1021a530` / `FUN_1021a560`, bounds-checked at 128 and
	// set-only, exactly as retail's four accessors are.
	bool IsEmailRead(int32 Index) const;
	bool IsEmailDeleted(int32 Index) const;
	void SetEmailRead(int32 Index);
	void SetEmailDeleted(int32 Index);
	// `FUN_1021bbf0`, the module's only deletion path (`autodelete` is read by nothing). It carries
	// its OWN bound — the record count — in front of the accessor's 128, so the two are different
	// guards and a record between the two is refused by the second and not the first. Public
	// beside the accessors it wraps: the router can only ever hand it a valid open index, so the
	// record bound is unreachable from the grammar and is proven by calling it.
	void MailDelete(int32 RealIndex);                      // FUN_1021bbf0
	// `FUN_1021bd80`: visible = not deleted AND its `dependency` (record `+0x240`, mode `0x102`)
	// passes, in record order. Rebuilt from scratch on every draw, so numbering is not stable
	// across a state change — and it never re-clamps the page.
	void RebuildMailIndex();
	// `FUN_1021c040`, the inbox. Public because it is also the miss path of the mail router.
	void MailListDraw();

	virtual void Serialize(FElysiumSaveArchive& Ar) override;
	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override;
	virtual const FString& ScreenSaverLabel() const override { return Definition.ScreenSaver; }
	virtual void Think() override;
	// `CPropHacking::vfunc113` `0x1021a270`: the idle logon box on the glass, the FIRST screensaver
	// tick at `RandomFloat(0,1) + curtime`, and only then the `ss_delay` floor.
	virtual void Activate() override;

	// `CPropHackingSS_Think` `0x1021a740`. Carries no in-use guard of its own — retail's entry
	// cancels the think outright and that is the whole of the protection (correction C16).
	void ScreenSaverThink();
	// `ThinkSet(CPropHackingSS_Think, 0, NULL)` + `m_flNextThink = Delay + curtime`. The one place a
	// screensaver schedule is written, so the three retail moments differ only in their argument.
	void ArmScreenSaver(float DelaySeconds);
	// Entry's `ThinkSet(NULL)` (`0x1021a5d6`).
	void CancelScreenSaver() { NextThink = ELYSIUM_NEVER_THINK; }

	// Also used by the content-independent tests. Replaces the parsed immutable definition and
	// re-sizes only its derived persistent arrays.
	void InstallDefinition(FElysiumTerminalDefinition InDefinition);

	// The live cracking / typed buffer (`m_szHackPWD`). While non-empty, every `hackcmd` line is
	// dropped and the stepper redraws the last row each think.
	const FString& HackBuffer() const { return CrackBuffer; }
	bool ReprintPromptPending() const { return bReprintPrompt; }

protected:
	virtual bool OpenContent(FString& OutError) override;
	virtual void BeginContentSession() override;
	virtual void EndContentSession() override;
	virtual bool SubmitContent(const FString& Command) override;
	virtual bool BeginContentHack() override;
	virtual void BuildContentView(FElysiumTerminalView& Out) const override;
	virtual EElysiumTerminalInputMode ContentInputMode() const override;
	virtual int32 AttemptDifficulty() const override;
	virtual void OnSkillSucceeded(FElysiumCombatCharacter& User) override;
	virtual void OnSkillFailed(FElysiumCombatCharacter& User) override;
	virtual void OnAttemptStopped(FElysiumCombatCharacter& User, EElysiumUseEndReason Reason) override;

private:
	FString CrackBuffer;          // `+0x82c` m_szHackPWD
	bool bReprintPrompt = false;  // `+0x9ec`
	bool bSkillSubmit = false;    // `+0x83c`
	int32 CrackRating = 0;
	float CrackTotalSeconds = 0.0f;
	FString LastCommand;

	bool DependencyPasses(const FString& Source) const;
	FString HackingString(int32 Index) const;
	// Loader-lowercased names (`Q_strnlwr`, §6): what the listings print and compare.
	static FString LoweredName(const FString& Name) { return Name.ToLower(); }
	// Unread among the CURRENT visible table — the directory draw rebuilds it first.
	int32 UnreadEmailCount() const;

	// The draw bodies, each named for its retail address in the .cpp.
	void DirectoryDraw();                                  // FUN_1021aca0
	void Prompt();                                         // FUN_1021b410
	void PasswordPrompt(int32 PendingTarget, bool bRetry);  // FUN_1021b5e0 (+ FUN_1021c390)
	void HelpDraw();                                       // FUN_1021b030
	void InvalidCommand(const FString& Line);              // FUN_1021c3c0
	bool EnterDirectory(int32 TargetIndex);                // FUN_1021c890
	bool ExecuteFunction(int32 DirectoryIndex, int32 FunctionIndex);   // FUN_1021c6d0
	void CrackingStep();                                   // FUN_10217d60
	void CrackingRow(const FString& Shown);                // CPropHacking::vfunc278

	// The mail bodies.
	void MailRender();                                     // FUN_1021c260
	void MailOpen(int32 VisibleRow);                       // FUN_1021bc90
	void MailNextPage();                                   // FUN_1021c000
	void MailPrevPage();                                   // FUN_1021bfd0
	void MailNextMessage();                                // FUN_1021bc20
	void MailPrevMessage();                                // FUN_1021bc60
	bool MailHotkeys(const FString& Line);                 // FUN_1021b9c0
	// The two `global_email` wrappers. Both early-out unless `m_bHasGlobalEmail` is set, and both
	// key on the entity's own `targetname` (`""` when unnamed).
	void LoadGlobalEmailState();                           // CPropHacking::LoadGlobalEmailState
	void SaveGlobalEmailState();                           // CPropHacking::SaveGlobalEmailState

	// The router (AcceptCmd 0x1021a830) and its arms.
	bool Builtins(const FString& Line);                    // FUN_1021aaa0
	bool MatchNames(const FString& Line);                  // FUN_1021b750
	void TypedSubmit(bool bSkill, const FString& Line);    // FUN_10217f50
	void PasswordSucceeded();                              // 0x1021c4d0
	void PasswordFailed();                                 // 0x1021c560
	void BeginInput();                                     // 0x10217b30
	static bool SameToken(const FString& Table, const FString& Line, int32 Count = 16);
};
