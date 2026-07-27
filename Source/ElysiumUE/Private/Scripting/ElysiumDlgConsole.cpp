// B4 — the scriptable echo of the visual-novel dialogue box. `elysium.dlg` dumps the open
// conversation; `elysium.dlg.choose` / `.advance` drive it exactly as clicking a choice / pressing a
// number key does (through the world's PlayerDialog* chokepoints), so a headless QA agent (or the MCP
// console_exec tool) can walk a `.dlg` beat with no UI. Non-Shipping only, like the other debug verbs.

#include "ElysiumDlg.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMapActor.h"
#include "ElysiumMapSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

#if !UE_BUILD_SHIPPING

DEFINE_LOG_CATEGORY_STATIC(LogElysiumDlgConsole, Log, All);

namespace
{
	FElysiumEntityWorld* DlgWorld(UWorld* World)
	{
		const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		const UElysiumMapSubsystem* Maps = GI ? GI->GetSubsystem<UElysiumMapSubsystem>() : nullptr;
		AElysiumMapActor* Map = Maps ? Maps->GetCurrentMap() : nullptr;
		return Map ? Map->GetEntityWorld() : nullptr;
	}
}

static FAutoConsoleCommandWithWorld GElysiumDlgDump(
	TEXT("elysium.dlg"),
	TEXT("elysium.dlg -- dump the open dialogue: speaker, the NPC line, and the numbered PC choices."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		FElysiumEntityWorld* W = DlgWorld(World);
		FElysiumDlgConversation* Conv = W ? W->GetOpenDialog() : nullptr;
		if (!Conv)
		{
			UE_LOG(LogElysiumDlgConsole, Display, TEXT("no dialogue open"));
			return;
		}
		FString Speaker;
		if (const FElysiumEntity* Owner = W->Resolve(W->GetOpenDialogOwner()))
		{
			Speaker = Owner->Def ? Owner->Def->TargetName : FString();
		}
		const bool bMale = Conv->PlayerMale();
		const bool bMalk = Conv->PlayerMalkavian();
		const FElysiumDlgLine* Line = Conv->CurrentNpcLine();
		UE_LOG(LogElysiumDlgConsole, Display, TEXT("[%s] %s"),
			Speaker.IsEmpty() ? TEXT("???") : *Speaker,
			Line ? *Line->RawFor(bMale, bMalk) : TEXT("(no line)"));
		if (Conv->IsTerminalLine())
		{
			UE_LOG(LogElysiumDlgConsole, Display, TEXT("  (terminal — elysium.dlg.advance to continue)"));
			return;
		}
		for (int32 v = 0; v < Conv->VisibleChoices().Num(); ++v)
		{
			const FElysiumDlgLine* C = Conv->VisibleChoice(v);
			UE_LOG(LogElysiumDlgConsole, Display, TEXT("  %d. %s"), v + 1,
				C ? *C->RawFor(bMale, bMalk) : TEXT("?"));
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GElysiumDlgChoose(
	TEXT("elysium.dlg.choose"),
	TEXT("elysium.dlg.choose <n> -- pick the Nth (1-based) visible PC choice in the open dialogue."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		FElysiumEntityWorld* W = DlgWorld(World);
		if (!W || !W->GetOpenDialog())
		{
			UE_LOG(LogElysiumDlgConsole, Warning, TEXT("no dialogue open"));
			return;
		}
		if (Args.Num() == 0)
		{
			UE_LOG(LogElysiumDlgConsole, Warning, TEXT("usage: elysium.dlg.choose <n>"));
			return;
		}
		const int32 N = FCString::Atoi(*Args[0]);
		W->PlayerDialogChoose(N - 1);
	}));

static FAutoConsoleCommandWithWorld GElysiumDlgAdvance(
	TEXT("elysium.dlg.advance"),
	TEXT("elysium.dlg.advance -- advance past a terminal NPC line (the box's 'continue')."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		if (FElysiumEntityWorld* W = DlgWorld(World))
		{
			W->PlayerDialogAdvance();
		}
	}));

#endif // !UE_BUILD_SHIPPING
