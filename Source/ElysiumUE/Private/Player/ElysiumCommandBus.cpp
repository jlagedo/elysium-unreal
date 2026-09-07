#include "Player/ElysiumCommandBus.h"

#include "ElysiumBinds.h"
#include "ElysiumCameraSolve.h"
#include "ElysiumCommands.h"
#include "Debug/ElysiumConsole.h"
#include "ElysiumContentPaths.h"
#include "ElysiumFootstepTuning.h"
#include "ElysiumLookCurve.h"
#include "ElysiumMoveSolve.h"
#include "Scripting/ElysiumPythonVM.h"
#include "Substrate/ElysiumCameraCinematic.h"   // ElysiumCineCam::CvarDefs — `camera_showdebug`

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

	// The cvars the *engine* registers rather than a cfg file. Declaring them makes each a known cvar
	// with its retail default, so `cam_idealdist 50` resolves as a cvar set instead of falling through
	// to Python and a fresh install with no `out/cfg` still reads VtMB's own values. Declarations
	// survive a re-seed and are shadowed by any cfg that carries the name, which is the order the game
	// itself loads them in.
	static bool bDeclaredEngineCvars = false;
	if (!bDeclaredEngineCvars)
	{
		bDeclaredEngineCvars = true;
		for (const ElysiumCam::FCvarDef& Def : ElysiumCam::CvarDefs())
		{
			Store.DeclareCvar(Def.Name, Def.Default);
		}
		for (const ElysiumMove::FCvarDef& Def : ElysiumMove::CvarDefs())
		{
			Store.DeclareCvar(Def.Name, Def.Default);
		}
		for (const ElysiumInput::FCvarDef& Def : ElysiumInput::CvarDefs())
		{
			Store.DeclareCvar(Def.Name, Def.Default);
		}
		// A2 (footsteps): the seven `footstep_*` / `sv_footsteps` cvars, declared with the defaults
		// `vampire.dll` constructs them with (`0x1026d1b0..0x1026d3f0`, `0x1011d7e0`).
		for (const ElysiumFootstep::FCvarDef& Def : ElysiumFootstep::CvarDefs())
		{
			Store.DeclareCvar(Def.Name, Def.Default);
		}
		// SC4: `camera_showdebug`, registered by `vampire.dll` in the `CBaseCineCam` translation
		// unit itself (`0x1006d5b0`) rather than with the client's camera block, which is why it is
		// declared from the entity's own file.
		for (const ElysiumCam::FCvarDef& Def : ElysiumCineCam::CvarDefs())
		{
			Store.DeclareCvar(Def.Name, Def.Default);
		}
	}
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

bool ElysiumCommandBus::ParseTap(const FString& Line, FString& OutPressLine, FString& OutReleaseLine)
{
	OutPressLine.Reset();
	OutReleaseLine.Reset();

	FString Statement = Line;
	Statement.TrimStartAndEndInline();
	if (Statement.IsEmpty())
	{
		return false;
	}

	// The registry's own split: the first whitespace-delimited word is the verb, the rest is the
	// argument tail. `Execute` does the same thing, and doing it differently here is how a tap and a
	// typed line come to disagree about what was asked for.
	FString Word = Statement;
	FString Args;
	int32 Space = INDEX_NONE;
	if (Statement.FindChar(TEXT(' '), Space))
	{
		Word = Statement.Left(Space);
		Args = Statement.Mid(Space + 1);
		Args.TrimStartAndEndInline();
	}
	if (Word.StartsWith(TEXT("+")) || Word.StartsWith(TEXT("-")))
	{
		Word.MidInline(1);
	}

	const FName Bare = ElysiumCommands::Canonical(Word);
	const FElysiumCommandDef* Def = FElysiumCommands::Get().Find(Bare);
	if (Def == nullptr || Def->Kind != EElysiumCmdKind::ButtonPair)
	{
		// Not a failure of this function — the caller reports it with the line that was asked for.
		return false;
	}

	const FString Tail = Args.IsEmpty() ? FString() : (TEXT(" ") + Args);
	const FString Name = Bare.ToString();
	OutPressLine = TEXT("+") + Name + Tail;
	OutReleaseLine = TEXT("-") + Name + Tail;
	return true;
}

// Verbs

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

// Command-stream record / replay (S5's acceptance: a recorded stream replays identically)

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

// The debug affordance for a typed button verb. `elysium.cmd +attack` latches until `elysium.cmd
// -attack`, which is what a typed `+cmd` does in retail — a console line produces a key-DOWN and there
// is no key-up behind it (`docs/vtmb/controls.md` § "The model in one paragraph"). That stays. This
// verb is the QA driver's key-up: it presses through the same bus and releases after the frame that
// samples the press, so one call is one click. It needs the router, because the router owns the frame.
static FAutoConsoleCommandWithWorldAndArgs GElysiumCmdTap(
	TEXT("elysium.cmd.tap"),
	TEXT("Press a VtMB +/- button verb for exactly one sampled frame and release it: "
	     "`elysium.cmd.tap attack` is one click, where `elysium.cmd +attack` latches until "
	     "`elysium.cmd -attack` (which is what retail does with a typed +cmd)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() == 0)
		{
			UE_LOG(LogElysiumCmdBus, Display, TEXT("usage: elysium.cmd.tap <button verb> [args]"));
			return;
		}
		if (UElysiumInputRouter* Router = RouterFor(World))
		{
			Router->TapCommand(FString::Join(Args, TEXT(" ")));
		}
	}));

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
