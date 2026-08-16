#include "ElysiumCogSubsystem.h"

#include "CogCommon.h"
#include "Containers/Ticker.h"
#include "Logging/LogMacros.h"

#if ENABLE_COG
#include "CogImguiHelper.h"
#include "CogSubsystem.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Debug/ElysiumCogWindow_Audio.h"
#include "Debug/ElysiumCogWindow_Entities.h"
#include "Debug/ElysiumCogWindow_Environment.h"
#include "Debug/ElysiumCogWindow_EventQueue.h"
#include "Debug/ElysiumCogWindow_GreenRoom.h"
#include "Debug/ElysiumGreenRoomRun.h"
#include "Debug/ElysiumCogWindow_Camera.h"
#include "ElysiumMapSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Debug/ElysiumCogWindow_Inspector.h"
#include "Debug/ElysiumCogWindow_Lights.h"
#include "Debug/ElysiumCogWindow_Logic.h"
#include "Debug/ElysiumCogWindow_Maps.h"
#include "Debug/ElysiumCogWindow_Npc.h"
#include "Debug/ElysiumCogWindow_Scripting.h"
#include "Debug/ElysiumCogWindow_SoundScheme.h"
#include "Debug/ElysiumCogWindow_Status.h"
#include "Debug/ElysiumCogWindow_WorldViz.h"
#include "CogEngineWindow_CollisionViewer.h"
#include "CogEngineWindow_Console.h"
#include "CogEngineWindow_DebugSettings.h"
#include "CogEngineWindow_Inspector.h"
#include "CogEngineWindow_Levels.h"
#include "CogEngineWindow_LogCategories.h"
#include "CogEngineWindow_Metrics.h"
#include "CogEngineWindow_OutputLog.h"
#include "CogEngineWindow_Plots.h"
#include "CogEngineWindow_Scalability.h"
#include "CogEngineWindow_Selection.h"
#include "CogEngineWindow_Stats.h"
#include "CogEngineWindow_TimeScale.h"
#include "CogEngineWindow_Transform.h"
#endif

#if ENABLE_COG
DEFINE_LOG_CATEGORY_STATIC(LogElysiumCog, Log, All);

namespace
{
	// The armed green room, or null. Null in every ordinary session — the lab only exists under
	// `-ElysiumGreenRoom` or after `elysium.gr` has stood one up.
	FElysiumGreenRoomRun* ResolveGreenRoomLab(const UWorldSubsystem* Owner)
	{
		const UWorld* World = Owner ? Owner->GetWorld() : nullptr;
		const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		const UElysiumMapSubsystem* Maps = GI ? GI->GetSubsystem<UElysiumMapSubsystem>() : nullptr;
		return Maps ? Maps->GetGreenRoom() : nullptr;
	}
}
#endif // ENABLE_COG

bool UElysiumCogSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}

#if ENABLE_COG
	return true;
#else
	return false;
#endif
}

#if ENABLE_COG
// Whether Cog's ImGui window layout survives between runs. Off by default: a debug UI that
// restores itself decides what is on screen at boot, and a restored window that holds the mouse
// makes the game's own menu unclickable. With this off every launch starts dormant and F1 is the
// only way in. Set to 1 to keep a hand-arranged layout across runs.
static TAutoConsoleVariable<int32> CVarCogPersist(
	TEXT("elysium.CogPersist"),
	0,
	TEXT("1 = keep Cog's ImGui window layout between runs; 0 = boot dormant every time."),
	ECVF_Default);
#endif

void UElysiumCogSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

#if ENABLE_COG
	// Clear the layout before the dependency below brings Cog up: its ImGui context reads this file
	// when it first initialises, so removing it afterwards would be a race.
	if (CVarCogPersist.GetValueOnGameThread() == 0)
	{
		const FString LayoutIni = FCogImguiHelper::GetIniFilePath(TEXT("imgui"));
		if (IFileManager::Get().FileExists(*LayoutIni))
		{
			IFileManager::Get().Delete(*LayoutIni, /*RequireExists*/ false, /*EvenReadOnly*/ true);
		}
	}

	CogSubsystem = Collection.InitializeDependency<UCogSubsystem>();
#endif
}

void UElysiumCogSubsystem::PostInitialize()
{
	Super::PostInitialize();

#if ENABLE_COG
	UCogSubsystem* Cog = Cast<UCogSubsystem>(CogSubsystem.Get());
	if (!IsValid(Cog))
	{
		return;
	}

	// Stock CogEngine windows. No GAS/AI/Input windows (this project uses none of
	// those systems), so Cog::AddAllWindows (which pulls in CogAbility/CogAI) is
	// intentionally not used. Nor is CogEngineWindow_ImGui: its whole content is the
	// Dear ImGui / ImPlot demo, metrics, debug-log and style-editor toggles, which are
	// ImGui's own showcase, not this project's debug surface. Press F1 in PIE/standalone
	// to open the main menu.
	Cog->AddWindow<FCogEngineWindow_Inspector>("Engine.World.Actor Inspector");
	Cog->AddWindow<FCogEngineWindow_Selection>("Engine.World.Selection");
	Cog->AddWindow<FCogEngineWindow_CollisionViewer>("Engine.World.Collision");
	Cog->AddWindow<FCogEngineWindow_Levels>("Engine.World.Levels");
	Cog->AddWindow<FCogEngineWindow_Transform>("Engine.World.Transform");
	Cog->AddWindow<FCogEngineWindow_Console>("Engine.Diagnostics.Console");
	Cog->AddWindow<FCogEngineWindow_OutputLog>("Engine.Diagnostics.Output Log");
	Cog->AddWindow<FCogEngineWindow_LogCategories>("Engine.Diagnostics.Log Filters");
	Cog->AddWindow<FCogEngineWindow_Stats>("Engine.Performance.Stats");
	Cog->AddWindow<FCogEngineWindow_Metrics>("Engine.Performance.Metrics");
	Cog->AddWindow<FCogEngineWindow_Plots>("Engine.Performance.Plots");
	Cog->AddWindow<FCogEngineWindow_Scalability>("Engine.Performance.Quality");
	Cog->AddWindow<FCogEngineWindow_TimeScale>("Engine.Session.Time Control");
	Cog->AddWindow<FCogEngineWindow_DebugSettings>("Engine.Session.Debug Settings");

	// Custom Elysium windows, grouped under an "Elysium" main-menu category (the "Elysium."
	// name prefix). They read Elysium's own runtime data structures directly — the Track-B
	// entities are plain C++, invisible to Cog's UObject reflection — via FElysiumCogWindow.
	Cog->AddWindow<FElysiumCogWindow_Status>("Elysium.Session.Overview");
	Cog->AddWindow<FElysiumCogWindow_Maps>("Elysium.Session.Maps & Travel");
	Cog->AddWindow<FElysiumCogWindow_Entities>("Elysium.World.Entity Browser");
	Cog->AddWindow<FElysiumCogWindow_Inspector>("Elysium.World.Entity Inspector");
	Cog->AddWindow<FElysiumCogWindow_Logic>("Elysium.World.Logic Monitor");
	Cog->AddWindow<FElysiumCogWindow_EventQueue>("Elysium.World.Event Flow");
	Cog->AddWindow<FElysiumCogWindow_WorldViz>("Elysium.World.Overlays");
	Cog->AddWindow<FElysiumCogWindow_Lights>("Elysium.Look.Lighting");
	Cog->AddWindow<FElysiumCogWindow_Environment>("Elysium.Look.Wetness & Reflections");
	Cog->AddWindow<FElysiumCogWindow_Audio>("Elysium.Audio.Playback");
	Cog->AddWindow<FElysiumCogWindow_SoundScheme>("Elysium.Audio.Soundscape");
	Cog->AddWindow<FElysiumCogWindow_Camera>("Elysium.Characters.Camera");
	Cog->AddWindow<FElysiumCogWindow_Npc>("Elysium.Characters.Cast & AI");
	GreenRoomWindow = Cog->AddWindow<FElysiumCogWindow_GreenRoom>("Elysium.Characters.Green Room");
	Cog->AddWindow<FElysiumCogWindow_Scripting>("Elysium.Gameplay.Scripting");

	// `elysium.gr` — the whole point of which is that it works from a cold session. It arms the
	// green-room lab if this session has none, opens the window, and hands ImGui the mouse: a
	// window that is visible behind a game still holding the cursor is not usable, and finding it
	// through F1 and two menu levels is the friction this verb exists to remove.
	GreenRoomCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("elysium.gr"),
		TEXT("Open the green room: a neutral stage with one body on it, live cloth tuning, and a "
			 "camera you drive. Arms the stage if this session has none."),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this]()
		{
			UCogSubsystem* CogNow = Cast<UCogSubsystem>(CogSubsystem.Get());
			if (GreenRoomWindow == nullptr || !IsValid(CogNow))
			{
				return;
			}
			GreenRoomWindow->OpenLab();
			// **Except while the lab is driving.** With ImGui holding input, every non-gamepad key is
			// consumed before the input router sees it, so grabbing the keyboard here would silently
			// stop the body the operator is walking. F1 is the way back in.
			const FElysiumGreenRoomRun* Lab = ResolveGreenRoomLab(this);
			const bool bDriving = Lab != nullptr && Lab->IsDriving();
			if (!bDriving && !CogNow->GetContext().GetEnableInput())
			{
				CogNow->GetContext().SetEnableInput(true);
			}
		}),
		ECVF_Default);

	// Boot dormant: Cog is compiled in (non-Shipping) and F1 opens it, but nothing should be on
	// screen until then, and it must not be holding the mouse — a captured cursor makes the game's
	// own UI unclickable, which is exactly how it presents.
	//
	// Closing windows is not enough on its own. Cog restores window visibility from its ImGui ini,
	// and that restore can land *after* a fixed grace period expires, so a timed hide is a race the
	// layout sometimes wins. `elysium.CogPersist 0` (the default) removes the layout ini before Cog
	// reads it, which is what actually makes the boot state deterministic; the ticker below then
	// only has to cover the frames before the first render.
	StartupHideElapsed = 0.f;
	StartupHideTicker = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateWeakLambda(this, [this](float DeltaTime)
		{
			UCogSubsystem* CogToHide = Cast<UCogSubsystem>(CogSubsystem.Get());
			if (IsValid(CogToHide))
			{
				CogToHide->CloseAllWindows();
				// The input mode is the half that made the menu unclickable: with it enabled, ImGui
				// swallows the click before Slate sees it, and no amount of closing windows helps
				// because the main menu bar alone keeps input captured. Guarded on it already being
				// enabled — SetEnableInput touches the ImGui context, which Cog creates lazily on
				// its first tick, and this ticker can run before that.
				if (CogToHide->GetContext().GetEnableInput())
				{
					CogToHide->GetContext().SetEnableInput(false);
				}
			}

			StartupHideElapsed += DeltaTime;
			const bool bKeepTicking = StartupHideElapsed < 1.f;
			if (!bKeepTicking)
			{
				StartupHideTicker.Reset();
				// `uv run elysium gr` launches straight into the lab, so the window it exists to
				// show has to survive the dormant-boot pass above rather than be hunted for
				// afterwards. This runs once the hide window closes, which is why it is here and
				// not beside the AddWindow calls.
				if (GreenRoomWindow != nullptr && IsValid(CogToHide)
					&& FParse::Param(FCommandLine::Get(), TEXT("GreenRoomLab")))
				{
					GreenRoomWindow->OpenLab();
					// `-GreenRoomDock=left|right` parks the window against an edge of Cog's dockspace
					// instead of floating it over the stage, which is what `uv run elysium play gr`
					// asks for. Anything else leaves the window free-floating.
					FString DockSide;
					if (FParse::Value(FCommandLine::Get(), TEXT("GreenRoomDock="), DockSide)
						&& (DockSide == TEXT("left") || DockSide == TEXT("right")))
					{
						GreenRoomWindow->DockToSide(DockSide == TEXT("left"));
					}
					// A drive launch wants the window **visible and not holding the keyboard**: ImGui
					// consumes every key while it has input, so grabbing it here would fail the
					// acceptance on the first W. The window says F1 hands it over and back.
					if (!FParse::Param(FCommandLine::Get(), TEXT("GreenRoomDrive"))
						&& !CogToHide->GetContext().GetEnableInput())
					{
						CogToHide->GetContext().SetEnableInput(true);
					}
				}
			}
			return bKeepTicking;
		}));
#endif
}

void UElysiumCogSubsystem::Deinitialize()
{
#if ENABLE_COG
	if (StartupHideTicker.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(StartupHideTicker);
		StartupHideTicker.Reset();
	}
	if (GreenRoomCommand != nullptr)
	{
		IConsoleManager::Get().UnregisterConsoleObject(GreenRoomCommand);
		GreenRoomCommand = nullptr;
	}
	GreenRoomWindow = nullptr;
#endif

	Super::Deinitialize();
}
