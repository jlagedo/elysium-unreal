#include "ElysiumCogWindow_Status.h"

#if ENABLE_COG

#include "ElysiumEntityWorld.h"
#include "ElysiumGameClock.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumMapActor.h"

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

	// One value column for every section, so labels and values line up down the whole window.
	const float ValueColumn = GetDpiScale() * 120.0f;
	auto Row = [ValueColumn](const char* Label, const FString& Value)
	{
		ImGui::TextUnformatted(Label);
		ImGui::SameLine(ValueColumn);
		ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*Value));
	};

	ImGui::SeparatorText("Map");
	Row("Name", Map->LoadedMap.IsEmpty() ? TEXT("(loading)") : Map->LoadedMap);
	Row("Surfaces", FString::Printf(TEXT("%d world · %d sky"), Map->WorldSurfaceCount, Map->SkySurfaceCount));
	Row("Lights", FString::Printf(TEXT("%d"), Map->WorldLightCount));
	Row("Props", FString::Printf(TEXT("%d inst · %d models"), Map->PropInstanceCount, Map->PropModelCount));

	ImGui::SeparatorText("Collision");
	Row("Collider", Map->bBrushCollision ? TEXT("brush hulls") : TEXT("render trimesh"));
	Row("Hulls", FString::Printf(TEXT("%d"), Map->HullCount));
	Row("Disp tris", FString::Printf(TEXT("%d"), Map->DispTriCount));

	ImGui::SeparatorText("Entities");
	Row("Records", FString::Printf(TEXT("%d"), Map->EntityCount));
	Row("Brush bodies", FString::Printf(TEXT("%d"), Map->BrushBodyCount));

	if (const FElysiumEntityWorld* World = GetEntityWorld())
	{
		Row("Queue pending", FString::Printf(TEXT("%d"), World->Queue().Num()));
		Row("I/O history", FString::Printf(TEXT("%d / %d"), World->RingBuffer().Num(), World->RingBuffer().Capacity()));
		Row("Touches", FString::Printf(TEXT("%d begin · %d end"), World->TouchBegins(), World->TouchEnds()));
		Row("Dead wires", FString::Printf(TEXT("%d target · %d input"), World->UnknownTargets(), World->UnknownInputs()));
	}
	else
	{
		ImGui::TextDisabled("No .ents substrate on this map.");
	}

	ImGui::SeparatorText("Clock");
	if (const UElysiumGameStateSubsystem* GameState = GetGameState())
	{
		const FElysiumGameClock& Clock = GameState->GameClock();
		Row("Now", FString::Printf(TEXT("%.2f s"), Clock.GetNow()));
		Row("Scale", FString::Printf(TEXT("%.2fx%s"), Clock.GetScale(), Clock.IsPaused() ? TEXT("  (paused)") : TEXT("")));
	}
	else
	{
		ImGui::TextDisabled("No game-state subsystem.");
	}
}

#endif // ENABLE_COG
