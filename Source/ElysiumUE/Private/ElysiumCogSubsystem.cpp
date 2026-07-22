#include "ElysiumCogSubsystem.h"

#include "CogCommon.h"

#if ENABLE_COG
#include "CogSubsystem.h"
#include "CogEngineWindow_CollisionViewer.h"
#include "CogEngineWindow_Console.h"
#include "CogEngineWindow_DebugSettings.h"
#include "CogEngineWindow_ImGui.h"
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

void UElysiumCogSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

#if ENABLE_COG
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
	// intentionally not used. Press F1 in PIE/standalone to open the main menu.
	Cog->AddWindow<FCogEngineWindow_Inspector>("Engine.Inspector");
	Cog->AddWindow<FCogEngineWindow_Selection>("Engine.Selection");
	Cog->AddWindow<FCogEngineWindow_CollisionViewer>("Engine.Collision Viewer");
	Cog->AddWindow<FCogEngineWindow_Levels>("Engine.Levels");
	Cog->AddWindow<FCogEngineWindow_Transform>("Engine.Transform");
	Cog->AddWindow<FCogEngineWindow_Console>("Engine.Console");
	Cog->AddWindow<FCogEngineWindow_OutputLog>("Engine.Output Log");
	Cog->AddWindow<FCogEngineWindow_LogCategories>("Engine.Log Categories");
	Cog->AddWindow<FCogEngineWindow_Stats>("Engine.Stats");
	Cog->AddWindow<FCogEngineWindow_Metrics>("Engine.Metrics");
	Cog->AddWindow<FCogEngineWindow_Plots>("Engine.Plots");
	Cog->AddWindow<FCogEngineWindow_TimeScale>("Engine.Time Scale");
	Cog->AddWindow<FCogEngineWindow_Scalability>("Engine.Scalability");
	Cog->AddWindow<FCogEngineWindow_DebugSettings>("Engine.Debug Settings");
	Cog->AddWindow<FCogEngineWindow_ImGui>("Engine.ImGui");
#endif
}
