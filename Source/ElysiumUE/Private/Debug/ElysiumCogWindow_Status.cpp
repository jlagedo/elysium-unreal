#include "Debug/ElysiumCogWindow_Status.h"

#if ENABLE_COG

#include "Debug/ElysiumCogStyle.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumGameClock.h"
#include "ElysiumSessionSubsystem.h"
#include "ElysiumMapActor.h"
#include "Visual/ElysiumMapVisuals.h"
#include "Map/ElysiumMapCollision.h"

#include "CogLocalizationConfig.h"   // COG_TCHAR_TO_CHAR
#include "imgui.h"

void FElysiumCogWindow_Status::Initialize()
{
	Super::Initialize();

	// Read-only: no per-window config, so no context menu of its own.
	bHasMenu = false;
}

void FElysiumCogWindow_Status::RenderHelp()
{
	ImGui::Text(
		"Live summary of the loaded VtMB map and the Track-B entity substrate: surface, light "
		"and prop counts, the active brush/trimesh collider, entity and brush-body counts, the "
		"event-queue depth and I/O history ring-buffer fill, and the game clock. Read-only.");
}

void FElysiumCogWindow_Status::RenderContent()
{
	Super::RenderContent();

	const AElysiumMapActor* Map = GetMapActor();
	if (Map == nullptr)
	{
		ImGui::TextDisabled("No map loaded.");
		return;
	}

	// One value column for every section, so labels and values line up down the whole window. Wide
	// enough for "Brush bodies" / "Queue pending", the longest labels here.
	const float ValueColumn = GetDpiScale() * 124.0f;
	auto Row = [ValueColumn](const char* Label, const FString& Value)
	{
		ElysiumCogStyle::LabelValue(Label, COG_TCHAR_TO_CHAR(*Value), ValueColumn);
	};

	const UElysiumMapVisuals* Visuals = Map->GetVisuals();
	const UElysiumMapCollision* Collision = Map->GetCollision();

	ImGui::TextColored(ElysiumCogStyle::ColName, "%s",
		COG_TCHAR_TO_CHAR(*(Map->LoadedMap.IsEmpty() ? FString(TEXT("Loading map...")) : Map->LoadedMap)));
	ImGui::SameLine();
	ImGui::TextDisabled("%s", ElysiumMapRuntimePhaseName(Map->GetRuntimePhase()));
	ImGui::Separator();

	Row("Name", Map->LoadedMap.IsEmpty() ? TEXT("(loading)") : Map->LoadedMap);
	Row("Surfaces", FString::Printf(TEXT("%d world · %d sky"), Visuals->WorldSurfaceCount, Visuals->SkySurfaceCount));
	Row("Lights", FString::Printf(TEXT("%d"), Visuals->WorldLightCount));
	Row("Props", FString::Printf(TEXT("%d inst · %d models"), Visuals->PropInstanceCount, Visuals->PropModelCount));
	Row("Details", FString::Printf(TEXT("%d inst · %d models · %d sky"), Visuals->DetailInstanceCount, Visuals->DetailModelCount, Visuals->DetailSkyComponentCount));
	Row("Sprites", FString::Printf(TEXT("%d · %d glow · %d sky"), Visuals->SpriteCount, Visuals->SpriteGlowCount, Visuals->SpriteSkyCount));
	Row("Decals", FString::Printf(TEXT("%d"), Visuals->DecalCount));
	Row("Records", FString::Printf(TEXT("%d"), Map->EntityCount));
	Row("Brush bodies", FString::Printf(TEXT("%d"), Map->BrushBodyCount));

	if (const FElysiumEntityWorld* World = GetEntityWorld())
	{
		Row("Queue pending", FString::Printf(TEXT("%d"), World->Queue().Num()));
	}
	else
	{
		ImGui::TextDisabled("No .ents substrate on this map.");
	}

	if (const UElysiumSessionSubsystem* GameState = GetGameState())
	{
		const FElysiumGameClock& Clock = GameState->GameClock();
		Row("Now", FString::Printf(TEXT("%.2f s"), Clock.GetNow()));
		Row("Scale", FString::Printf(TEXT("%.2fx%s"), Clock.GetScale(), Clock.IsPaused() ? TEXT("  (paused)") : TEXT("")));
	}
	else
	{
		ImGui::TextDisabled("No game-state subsystem.");
	}

	if (ImGui::CollapsingHeader("Technical details"))
	{
		Row("Collider", Collision->bBrushCollision ? TEXT("brush hulls") : TEXT("render trimesh"));
		Row("Hulls", FString::Printf(TEXT("%d"), Collision->HullCount));
		Row("Disp tris", FString::Printf(TEXT("%d"), Collision->DispTriCount));
		if (const FElysiumEntityWorld* World = GetEntityWorld())
		{
			Row("I/O history", FString::Printf(TEXT("%d / %d"), World->RingBuffer().Num(), World->RingBuffer().Capacity()));
			Row("Touches", FString::Printf(TEXT("%d begin · %d end"), World->TouchBegins(), World->TouchEnds()));
			Row("Dead wires", FString::Printf(TEXT("%d target · %d input"), World->UnknownTargets(), World->UnknownInputs()));
		}
	}
}

#endif // ENABLE_COG
