#pragma once

#include "CoreMinimal.h"

#include "Containers/ArrayView.h"

#include "Substrate/ElysiumSkillClasses.h"
#include "Substrate/ElysiumTerminalScreenBuffer.h"

struct FElysiumTerminalView;
class UPrimitiveComponent;

// The view's reading of the session's input state, derived from retail's `m_HackFlags` and the
// pending-password target (docs/vtmb/computer-terminals.md §8.1, §9). `Raw` is `m_HackFlags 0x4`,
// a mode the substrate carries even though no computer content raises it (the keypad does).
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

// One patch-first `TerminalDefinition`. The parser preserves authored order and raw display/script
// strings; only names used for comparisons fold at the comparison site. It is deliberately plain
// C++ so malformed content and routing rules can be proven without a world, viewport, or RHI.
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

	FString Get(const class FElysiumEntityWorld* World, int32 Index);
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
	int32 TextColumns = 36;
	int32 TextRows = 24;
	int32 ColorScheme = 0;
	FString SoundGroup;

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
	virtual bool IsUsable() const override { return bStartEnabled; }
	virtual bool CanPlayerFocus(const FElysiumUseContext& Context) const override;
	virtual FElysiumUseBeginResult BeginPlayerUse(const FElysiumUseContext& Context) override;
	virtual void EndPlayerUse(const FElysiumUseContext& Context, EElysiumUseEndReason Reason) override;
	virtual void Serialize(FElysiumSaveArchive& Ar) override;
	virtual void OnDormancyChanged() override;
	virtual void OnRuntimeTransformChanged() override;
	virtual void OnRuntimeModelChanged() override;
	virtual void TickPlayerUse(const FElysiumUseContext& Context) override;
	// Slot 44 (`+0xb0`): `CBaseTerminal` inherits `CAISound::FUN_100267b0` = `return 1`, so a second
	// `+use` press while the session is held **always** releases it (correction C11).
	virtual bool ReleasesOnSecondUse() const override { return true; }
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

	// Re-read `screen` / `screen_axis` off the standing body. Called from `Spawn`, and again from
	// every hook that can move or replace that body.
	void ResolveScreenAttachments();
	// One named warning naming entity, model and the missing part — raised at spawn and after a
	// model change, never per use press.
	void ReportMissingAttachments() const;

	// --- the retail entity messages, as writes into `Screen` (§8.2) ---
	void ScreenSetCursor(int32 Column, int32 Row) { Screen.SetCursor(Column, Row); }
	void ScreenPrint(const FString& Text) { Screen.Print(Text); }
	void ScreenClear() { Screen.Clear(); }
	void ScreenStyleDefault() { Screen.SetStyleDefault(); }
	void ScreenStyleAlternate() { Screen.SetStyleAlternate(); }
	void ScreenSetMargins(int32 Left, int32 Right) { Screen.SetMargins(Left, Right); }
	void ScreenEcho(TCHAR Character) { Screen.Echo(Character); }
	// `FUN_10219120` (leave acknowledge/raw), `FUN_10219240` (raw), `FUN_10219270` (acknowledge).
	void EnterLineEdit() { HackFlags &= ~(FlagAcknowledge | FlagRawCharacter); }
	void EnterRawCharacter() { EnterLineEdit(); HackFlags |= FlagRawCharacter; }
	void EnterAcknowledge() { EnterLineEdit(); HackFlags |= FlagAcknowledge; }
	// `FUN_10218820` — the `InfoCtrl` usermessage (§8.4).
	void SetHudHint(int32 Type, int32 Value);
	// `FUN_101f5950` on the entity's soundgroup: `access`, `accept`, `error`, `typing` (§14).
	void PlayCue(const TCHAR* Cue);

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

	UPrimitiveComponent* WorldBody = nullptr;
	FString VisualStem;
	TMap<FName, FString> CueRels;
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
	bool bEmailUnlocked = false;
	int32 EmailAttempts = 0;

	virtual void Serialize(FElysiumSaveArchive& Ar) override;
	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override;
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

	// The router (AcceptCmd 0x1021a830) and its arms.
	bool Builtins(const FString& Line);                    // FUN_1021aaa0
	bool MatchNames(const FString& Line);                  // FUN_1021b750
	void TypedSubmit(bool bSkill, const FString& Line);    // FUN_10217f50
	void PasswordSucceeded();                              // 0x1021c4d0
	void PasswordFailed();                                 // 0x1021c560
	void BeginInput();                                     // 0x10217b30
	static bool SameToken(const FString& Table, const FString& Line, int32 Count = 16);
};
