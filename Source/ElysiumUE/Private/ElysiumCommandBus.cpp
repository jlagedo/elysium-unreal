#include "ElysiumCommandBus.h"

#include "ElysiumBinds.h"
#include "ElysiumCommands.h"
#include "ElysiumConsole.h"
#include "ElysiumContentPaths.h"
#include "ElysiumPythonVM.h"

#include "ElysiumInputRouter.h"
#include "ElysiumPlayerController.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumCmdBus, Log, All);

FElysiumConsole& ElysiumCommandBus::Console()
{
	FElysiumConsole& Store = FElysiumPythonVM::Get().Console();
	// The VM seeds this at EnsureStarted; a build without CPython (or a run that never starts a
	// level script) would otherwise reach the bus with empty alias/cvar tables, and half the patch's
	// default binds are aliases.
	Store.EnsureSeeded(FElysiumContentPaths::CfgDir());
	return Store;
}

void ElysiumCommandBus::Exec(const FString& Line)
{
	if (Line.IsEmpty())
	{
		return;
	}
	Console().Execute(Line);
}

// =====================================================================================
// Verbs
// =====================================================================================

static FAutoConsoleCommand GElysiumCmdExec(
	TEXT("elysium.cmd"),
	TEXT("Run a VtMB console line through the command bus: registered command -> alias -> cvar -> "
	     "Python. e.g. `elysium.cmd +use`, `elysium.cmd togglecamera`, `elysium.cmd patchtype`."),
	FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args)
	{
		if (Args.Num() == 0)
		{
			UE_LOG(LogElysiumCmdBus, Display, TEXT("usage: elysium.cmd <console line>"));
			return;
		}
		ElysiumCommandBus::Exec(FString::Join(Args, TEXT(" ")));
	}));

static FAutoConsoleCommand GElysiumCmdList(
	TEXT("elysium.commands"),
	TEXT("List the command registry: every VtMB bindable verb, its kind, whether it is implemented, "
	     "and how many times it has run. Optional filter matches the name or the group."),
	FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args)
	{
		const FString Filter = Args.Num() > 0 ? Args[0].ToLower() : FString();
		UE_LOG(LogElysiumCmdBus, Display, TEXT("%s"), *FElysiumCommands::Get().Describe(Filter));
	}));

static FAutoConsoleCommand GElysiumBindList(
	TEXT("elysium.binds"),
	TEXT("List the default bind table (VtMB keyname -> console command) the router installs."),
	FConsoleCommandDelegate::CreateStatic([]()
	{
		FString Out;
		for (const FElysiumDefaultBind& Bind : ElysiumBinds::Defaults())
		{
			Out += FString::Printf(TEXT("  %-14s %-14s %s\n"),
				*Bind.Key.ToString(), Bind.VtmbKey, Bind.Command);
		}
		UE_LOG(LogElysiumCmdBus, Display, TEXT("%d default binds\n%s"),
			ElysiumBinds::Defaults().Num(), *Out);
	}));

// =====================================================================================
// Command-stream record / replay (S5's acceptance: a recorded stream replays identically)
// =====================================================================================

namespace
{
	UElysiumInputRouter* RouterFor(UWorld* World)
	{
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		AElysiumPlayerController* Elysium = Cast<AElysiumPlayerController>(PC);
		UElysiumInputRouter* Router = Elysium ? Elysium->GetInputRouter() : nullptr;
		if (!Router)
		{
			UE_LOG(LogElysiumCmdBus, Warning, TEXT("no input router in this world"));
		}
		return Router;
	}

	FString StreamPath(const FString& Name)
	{
		const FString Stem = Name.IsEmpty() ? TEXT("stream") : Name;
		return FPaths::ProjectSavedDir() / TEXT("Elysium/Cmds") / (Stem + TEXT(".cmds"));
	}
}

static FAutoConsoleCommandWithWorldAndArgs GElysiumCmdRecord(
	TEXT("elysium.cmd.record"),
	TEXT("Start recording the player's command stream. `elysium.cmd.stop` ends it; "
	     "`elysium.cmd.save <name>` writes it to Saved/Elysium/Cmds/<name>.cmds."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* World)
	{
		if (UElysiumInputRouter* Router = RouterFor(World))
		{
			Router->StartRecording();
			UE_LOG(LogElysiumCmdBus, Display, TEXT("recording command stream"));
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GElysiumCmdStop(
	TEXT("elysium.cmd.stop"),
	TEXT("Stop recording or replaying the command stream."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* World)
	{
		if (UElysiumInputRouter* Router = RouterFor(World))
		{
			Router->StopRecording();
			Router->StopReplay();
			UE_LOG(LogElysiumCmdBus, Display, TEXT("command stream stopped (%d frames recorded)"),
				Router->Recorded().Num());
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GElysiumCmdReplay(
	TEXT("elysium.cmd.replay"),
	TEXT("Replay the recorded command stream, or `elysium.cmd.replay <name>` a saved one. The router "
	     "feeds recorded intent in place of the device, so movement and the bus cannot tell."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		UElysiumInputRouter* Router = RouterFor(World);
		if (!Router)
		{
			return;
		}
		FElysiumUserCmdStream Stream = Router->Recorded();
		if (Args.Num() > 0)
		{
			FString Text;
			const FString Path = StreamPath(Args[0]);
			if (!FFileHelper::LoadFileToString(Text, *Path) || !FElysiumUserCmdStream::FromText(Text, Stream))
			{
				UE_LOG(LogElysiumCmdBus, Warning, TEXT("could not read %s"), *Path);
				return;
			}
		}
		if (Stream.Num() == 0)
		{
			UE_LOG(LogElysiumCmdBus, Warning, TEXT("nothing to replay"));
			return;
		}
		Router->StartReplay(Stream);
		UE_LOG(LogElysiumCmdBus, Display, TEXT("replaying %d frames"), Stream.Num());
	}));

static FAutoConsoleCommandWithWorldAndArgs GElysiumCmdSave(
	TEXT("elysium.cmd.save"),
	TEXT("Write the recorded command stream to Saved/Elysium/Cmds/<name>.cmds."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		UElysiumInputRouter* Router = RouterFor(World);
		if (!Router)
		{
			return;
		}
		const FString Path = StreamPath(Args.Num() > 0 ? Args[0] : FString());
		if (FFileHelper::SaveStringToFile(Router->Recorded().ToText(), *Path))
		{
			UE_LOG(LogElysiumCmdBus, Display, TEXT("%d frames -> %s"), Router->Recorded().Num(), *Path);
		}
	}));
