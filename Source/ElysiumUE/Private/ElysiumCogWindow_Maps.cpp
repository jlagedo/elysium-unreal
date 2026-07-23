#include "ElysiumCogWindow_Maps.h"

#if ENABLE_COG

#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMapActor.h"
#include "ElysiumMapSubsystem.h"

#include "CogLocalizationConfig.h"   // COG_TCHAR_TO_CHAR
#include "CogWidgets.h"
#include "imgui.h"

void FElysiumCogWindow_Maps::Initialize()
{
	Super::Initialize();
	bHasMenu = false;
}

void FElysiumCogWindow_Maps::RenderHelp()
{
	ImGui::Text(
		"Map lifecycle. Lists every map the pipeline has exported under tools/out; click Travel to "
		"load one (the current map is highlighted). Reload re-loads the current map without a recook "
		"- the export->reload hot loop, same as elysium.reload. Below, the current map's per-phase "
		"load timings and surface/light/prop counts.");
}

void FElysiumCogWindow_Maps::RenderContent()
{
	Super::RenderContent();

	UElysiumMapSubsystem* Maps = GetMapSubsystem();
	if (Maps == nullptr)
	{
		ImGui::TextDisabled("Map subsystem unavailable.");
		return;
	}

	const FString CurrentName = Maps->GetCurrentMapName();

	// --- Reload (the hot loop) -----------------------------------------------------------------
	ImGui::BeginDisabled(CurrentName.IsEmpty());
	if (ImGui::Button("Reload current map"))
	{
		Maps->Reload();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	FCogWidgets::HelpMarker("Re-Travel the current map: edit the exporter, re-export to tools/out, "
		"then Reload here to see it - no editor recook. Same as the elysium.reload console command.");

	// --- Exported map list ---------------------------------------------------------------------
	ImGui::SeparatorText("Exported maps");
	const TArray<FString> Names = Maps->ExportedMaps();
	if (Names.Num() == 0)
	{
		ImGui::TextDisabled("No exported maps under tools/out. Run the pipeline first.");
	}
	else if (ImGui::BeginTable("##Maps", 2,
		ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
		ImVec2(0.0f, GetDpiScale() * 160.0f)))
	{
		ImGui::TableSetupColumn("Map", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 64.0f);

		for (const FString& MapName : Names)
		{
			const bool bCurrent = (MapName == CurrentName);
			ImGui::TableNextRow();

			ImGui::TableNextColumn();
			if (bCurrent)
			{
				ImGui::TextColored(ImVec4(0.40f, 0.85f, 0.40f, 1.0f), "%s", COG_TCHAR_TO_CHAR(*MapName));
				ImGui::SameLine();
				ImGui::TextDisabled("(current)");
			}
			else
			{
				ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*MapName));
			}

			ImGui::TableNextColumn();
			ImGui::PushID(COG_TCHAR_TO_CHAR(*MapName));
			ImGui::BeginDisabled(bCurrent);
			if (ImGui::SmallButton("Travel"))
			{
				Maps->Travel(MapName);
			}
			ImGui::EndDisabled();
			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	// --- Current map detail --------------------------------------------------------------------
	const AElysiumMapActor* Map = GetMapActor();
	if (Map == nullptr)
	{
		ImGui::TextDisabled("No map loaded.");
		return;
	}

	const float ValueColumn = GetDpiScale() * 120.0f;
	auto Row = [ValueColumn](const char* Label, const FString& Value)
	{
		ImGui::TextUnformatted(Label);
		ImGui::SameLine(ValueColumn);
		ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*Value));
	};

	ImGui::SeparatorText("Current map");
	Row("Name", Map->LoadedMap.IsEmpty() ? TEXT("(loading)") : Map->LoadedMap);
	Row("Surfaces", FString::Printf(TEXT("%d world · %d sky"), Map->WorldSurfaceCount, Map->SkySurfaceCount));
	Row("Collision", Map->bBrushCollision
		? FString::Printf(TEXT("brush · %d hulls · %d disp tris"), Map->HullCount, Map->DispTriCount)
		: FString(TEXT("render trimesh")));
	Row("Lights", FString::Printf(TEXT("%d"), Map->WorldLightCount));
	Row("Props", FString::Printf(TEXT("%d inst · %d models"), Map->PropInstanceCount, Map->PropModelCount));
	Row("Entities", FString::Printf(TEXT("%d rec · %d bodies"), Map->EntityCount, Map->BrushBodyCount));
	Row("Entered via", Map->EntryLandmark.IsEmpty()
		? FString(TEXT("info_player_start")) : FString::Printf(TEXT("landmark %s"), *Map->EntryLandmark));

	// --- P4.6 transitions (trigger_changelevel + info_landmark) ---------------------------------
	RenderTransitions();

	// --- Load-phase timings --------------------------------------------------------------------
	if (Map->LoadPhases.Num() > 0)
	{
		ImGui::SeparatorText("Load timings");
		if (ImGui::BeginTable("##LoadPhases", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("Phase", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 72.0f);
			for (const AElysiumMapActor::FLoadPhase& P : Map->LoadPhases)
			{
				const bool bTotal = P.Name == TEXT("Total");
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				if (bTotal)
				{
					ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.50f, 1.0f), "%s", COG_TCHAR_TO_CHAR(*P.Name));
				}
				else
				{
					ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*P.Name));
				}
				ImGui::TableNextColumn();
				ImGui::Text("%.1f", P.Milliseconds);
			}
			ImGui::EndTable();
		}
	}
}

void FElysiumCogWindow_Maps::RenderTransitions()
{
	FElysiumEntityWorld* World = GetEntityWorld();
	if (World == nullptr)
	{
		return;
	}

	// Collect this map's trigger_changelevel + info_landmark entities.
	TArray<FElysiumEntity*> Changes;
	TArray<FElysiumEntity*> Landmarks;
	for (const TUniquePtr<FElysiumEntity>& EntPtr : World->Entities())
	{
		FElysiumEntity* Ent = EntPtr.Get();
		if (!Ent || !Ent->Def)
		{
			continue;
		}
		if (Ent->Def->Classname == TEXT("trigger_changelevel")) { Changes.Add(Ent); }
		else if (Ent->Def->Classname == TEXT("info_landmark"))  { Landmarks.Add(Ent); }
	}
	if (Changes.Num() == 0 && Landmarks.Num() == 0)
	{
		return;
	}

	const FString Header = FString::Printf(TEXT("Transitions  (%d changelevel · %d landmark)"),
		Changes.Num(), Landmarks.Num());
	if (!ImGui::CollapsingHeader(COG_TCHAR_TO_CHAR(*Header), ImGuiTreeNodeFlags_DefaultOpen))
	{
		return;
	}

	// Pending deferred travel (a trigger fired / ChangeMap scheduled, swap on the next tick).
	if (UElysiumMapSubsystem* Maps = GetMapSubsystem(); Maps && Maps->HasPendingTravel())
	{
		ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.50f, 1.0f), "PENDING: %s",
			COG_TCHAR_TO_CHAR(*Maps->PendingTravelDesc()));
	}

	// trigger_changelevel rows: destination + a button that fires the ChangeLevel input (the scripted
	// path), so a transition is testable without walking into the volume.
	int32 RowId = 0;
	for (FElysiumEntity* Ent : Changes)
	{
		ImGui::PushID(RowId++);
		const FString RowName = Ent->TargetName.IsEmpty() ? TEXT("(unnamed)") : Ent->TargetName;
		const FString Dest = FString::Printf(TEXT("-> %s @ %s"),
			*Ent->Def->Keys.FindRef(TEXT("map")), *Ent->Def->Keys.FindRef(TEXT("landmark")));

		if (Ent->IsInert())
		{
			ImGui::TextColored(ImVec4(0.7f, 0.5f, 0.4f, 1.0f), "%s", COG_TCHAR_TO_CHAR(*RowName));
		}
		else
		{
			ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*RowName));
		}
		ImGui::SameLine();
		ImGui::TextDisabled("%s", COG_TCHAR_TO_CHAR(*Dest));

		ImGui::SameLine();
		if (ImGui::SmallButton("Inspect"))
		{
			SetSelection(Ent->Handle);
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Change now"))
		{
			World->EnqueueInput(TEXT("!self"), FName(TEXT("ChangeLevel")), FElysiumVariant::Void(), 0.0,
				FElysiumEntityHandle::Invalid(), Ent->Handle);
		}
		ImGui::PopID();
	}

	// info_landmark rows: name + origin (the anchors the offset math resolves against).
	if (Landmarks.Num() > 0)
	{
		ImGui::Spacing();
		for (FElysiumEntity* Ent : Landmarks)
		{
			ImGui::PushID(RowId++);
			const FString RowName = Ent->TargetName.IsEmpty() ? TEXT("(unnamed)") : Ent->TargetName;
			ImGui::BulletText("%s", COG_TCHAR_TO_CHAR(*RowName));
			ImGui::SameLine();
			ImGui::TextDisabled("%s", COG_TCHAR_TO_CHAR(*Ent->Def->Origin.ToString()));
			ImGui::SameLine();
			if (ImGui::SmallButton("Inspect"))
			{
				SetSelection(Ent->Handle);
			}
			ImGui::PopID();
		}
	}
}

#endif // ENABLE_COG
