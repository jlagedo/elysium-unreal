#pragma once

#include "CoreMinimal.h"

#include "Substrate/ElysiumSkillClasses.h"

struct FElysiumTerminalView;
class UPrimitiveComponent;

enum class EElysiumTerminalInputMode : uint8
{
	Line,
	Password,
	Acknowledge,
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

// The shared exclusive terminal session. The derived content leaf owns grammar/state; this base
// owns the current user, serial/revision validation, body, save block, and one idempotent teardown.
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

	virtual void Spawn() override;
	virtual bool IsUsable() const override { return bStartEnabled; }
	virtual bool CanPlayerFocus(const FElysiumUseContext& Context) const override;
	virtual FElysiumUseBeginResult BeginPlayerUse(const FElysiumUseContext& Context) override;
	virtual void EndPlayerUse(const FElysiumUseContext& Context, EElysiumUseEndReason Reason) override;
	virtual void Serialize(FElysiumSaveArchive& Ar) override;
	virtual void OnDormancyChanged() override;
	virtual void OnRuntimeTransformChanged() override;
	virtual UPrimitiveComponent* GetAttachBody() const override;
	virtual const TCHAR* SaveBlockReason() const override;
	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override;
	virtual FElysiumTerminal* AsTerminal() override { return this; }

	void InputEnable();
	void InputDisable();
	bool Submit(uint32 ExpectedSerial, const FString& Command);
	bool BeginHack(uint32 ExpectedSerial);
	void BuildView(FElysiumTerminalView& Out) const;

protected:
	virtual bool OpenContent(FString& OutError) { return true; }
	virtual void BeginContentSession() {}
	virtual void EndContentSession() {}
	virtual bool SubmitContent(const FString& Command) { return false; }
	virtual bool BeginContentHack() { return false; }
	virtual void BuildContentView(FElysiumTerminalView& Out) const {}

	UPrimitiveComponent* WorldBody = nullptr;
	FString VisualStem;
};

// `prop_hacking`: ordered TerminalDefinition content, directory/password state, deterministic
// hacking bypass, Function output/script transaction, and persistent unlock/attempt state.
class FElysiumPropHacking final : public FElysiumTerminal
{
public:
	FString HackFile;
	bool bGlobalEmail = false;
	float ScreenSaverDelay = 1.5f;
	float ScreenSaverStart = 5.0f;

	FElysiumTerminalDefinition Definition;
	TArray<uint8> DirectoryUnlocked;
	TArray<int32> DirectoryAttempts;
	int32 CurrentDirectory = INDEX_NONE;
	int32 PendingDirectory = INDEX_NONE;
	EElysiumTerminalInputMode InputMode = EElysiumTerminalInputMode::Line;

	virtual void Serialize(FElysiumSaveArchive& Ar) override;
	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override;

	// Also used by the content-independent tests and, later, a cached rulebook reader. Replaces the
	// parsed immutable definition and re-sizes only its derived persistent arrays.
	void InstallDefinition(FElysiumTerminalDefinition InDefinition);

protected:
	virtual bool OpenContent(FString& OutError) override;
	virtual void BeginContentSession() override;
	virtual void EndContentSession() override;
	virtual bool SubmitContent(const FString& Command) override;
	virtual bool BeginContentHack() override;
	virtual void BuildContentView(FElysiumTerminalView& Out) const override;
	virtual int32 AttemptDifficulty() const override;
	virtual void OnSkillSucceeded(FElysiumCombatCharacter& User) override;
	virtual void OnSkillFailed(FElysiumCombatCharacter& User) override;
	virtual void OnAttemptStopped(FElysiumCombatCharacter& User, EElysiumUseEndReason Reason) override;

private:
	TArray<FString> ScreenLines;
	TArray<int32> VisibleDirectories;
	TArray<int32> VisibleFunctions;
	FString LastCommand;
	FString LastRejection;

	bool DependencyPasses(const FString& Source) const;
	void AppendLine(const FString& Line);
	void RenderRoot();
	void RenderDirectory();
	void EnterDirectory(int32 Index);
	bool RouteNormalCommand(const FString& Command);
	bool RoutePassword(const FString& Command);
	bool AcceptPassword(const FString& Command);
	bool ExecuteFunction(const FElysiumTerminalFunction& Function);
	void ReturnToRoot();
};
