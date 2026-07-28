#include "Debug/ElysiumCogWindow_Maps.h"

#if ENABLE_COG

#include "Debug/ElysiumCogStyle.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMapActor.h"
#include "Visual/ElysiumMapVisuals.h"
#include "Map/ElysiumMapCollision.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumPlayerBody.h"

#include "CogLocalizationConfig.h"   // COG_TCHAR_TO_CHAR
#include "CogWidgets.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
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
				ImGui::TextColored(ElysiumCogStyle::ColOk, "%s", COG_TCHAR_TO_CHAR(*MapName));
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

	const float ValueColumn = GetDpiScale() * 96.0f;
	auto Row = [ValueColumn](const char* Label, const FString& Value)
	{
		ElysiumCogStyle::LabelValue(Label, COG_TCHAR_TO_CHAR(*Value), ValueColumn);
	};

	const UElysiumMapVisuals* Visuals = Map->GetVisuals();
	const UElysiumMapCollision* Collision = Map->GetCollision();

	ImGui::SeparatorText("Current map");
	Row("Name", Map->LoadedMap.IsEmpty() ? TEXT("(loading)") : Map->LoadedMap);
	Row("Runtime", FString::Printf(TEXT("%s · %.2fs"),
		ElysiumMapRuntimePhaseName(Map->GetRuntimePhase()), Map->GetRuntimeWaitSeconds()));
	const FString Missing = Map->GetMissingRuntimePrerequisites();
	if (!Missing.IsEmpty())
	{
		Row("Waiting for", Missing);
	}
	if (!Map->GetRuntimeFailureReason().IsEmpty())
	{
		Row("Failure", Map->GetRuntimeFailureReason());
	}
	Row("Surfaces", FString::Printf(TEXT("%d world · %d sky"), Visuals->WorldSurfaceCount, Visuals->SkySurfaceCount));
	Row("Collision", FString::Printf(TEXT("%s · %d hulls · %d disp tris"),
		ElysiumCollisionBuildStateName(Collision->GetBuildState()),
		Collision->HullCount, Collision->DispTriCount));
	Row("Lights", FString::Printf(TEXT("%d"), Visuals->WorldLightCount));
	Row("Props", FString::Printf(TEXT("%d inst · %d models"), Visuals->PropInstanceCount, Visuals->PropModelCount));
	Row("Decals", FString::Printf(TEXT("%d"), Visuals->DecalCount));
	Row("Entities", FString::Printf(TEXT("%d rec · %d bodies"), Map->EntityCount, Map->BrushBodyCount));
	Row("Entered via", Map->EntryLandmark.IsEmpty()
		? FString(TEXT("info_player_start")) : FString::Printf(TEXT("landmark %s"), *Map->EntryLandmark));

	// --- Player pose + FPS (the removed elysium.debug overlay's readout) ------------------------
	RenderPlayer();

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
					ImGui::TextColored(ElysiumCogStyle::ColWarn, "%s", COG_TCHAR_TO_CHAR(*P.Name));
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

void FElysiumCogWindow_Maps::RenderPlayer()
{
	UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (PC == nullptr)
	{
		return;
	}

	FVector ViewLoc = FVector::ZeroVector;
	FRotator ViewRot = FRotator::ZeroRotator;
	PC->GetPlayerViewPoint(ViewLoc, ViewRot);
	const FVector Met = ViewLoc / 100.0;
	// Unreal (cm, left-handed) -> Source (inches, right-handed): the inverse of the
	// (sx,-sy,sz)*2.54 load transform.
	const FVector Src(ViewLoc.X / 2.54, -ViewLoc.Y / 2.54, ViewLoc.Z / 2.54);

	const float Dt = World->GetDeltaSeconds();
	if (Dt > 0.f)
	{
		SmoothedFPS = FMath::FInterpTo(SmoothedFPS, 1.f / Dt, Dt, 4.f);
	}

	const IElysiumPlayerBody* Body = Cast<IElysiumPlayerBody>(PC->GetPawn());
	const bool bNoclip = Body && Body->IsNoclip();
	const AElysiumMapActor* Map = GetMapActor();
	const UElysiumMapVisuals* MapVisuals = Map ? Map->GetVisuals() : nullptr;
	const bool bSky = MapVisuals && MapVisuals->IsSkyboxVisible();
	const bool bLights = MapVisuals && MapVisuals->AreLightsVisible();

	ImGui::SeparatorText("Player");
	const float ValueColumn = GetDpiScale() * 96.0f;
	auto Row = [ValueColumn](const char* Label, const FString& Value, const ImVec4* Color = nullptr)
	{
		ElysiumCogStyle::LabelValue(Label, COG_TCHAR_TO_CHAR(*Value), ValueColumn, Color);
	};

	Row("Position", FString::Printf(TEXT("(%.1f, %.1f, %.1f) m   yaw %.0f°"),
		Met.X, Met.Y, Met.Z, ViewRot.Yaw));
	Row("Source units", FString::Printf(TEXT("(%.0f, %.0f, %.0f)"), Src.X, Src.Y, Src.Z));
	Row("Mode", bNoclip ? FString(TEXT("NOCLIP")) : FString(TEXT("WALK")),
		bNoclip ? &ElysiumCogStyle::ColOk : nullptr);
	Row("Sky / Lights", FString::Printf(TEXT("%s / %s"),
		bSky ? TEXT("ON") : TEXT("OFF"), bLights ? TEXT("ON") : TEXT("OFF")));
	Row("FPS", FString::Printf(TEXT("%.0f"), SmoothedFPS), &ElysiumCogStyle::ColOk);
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
		ImGui::TextColored(ElysiumCogStyle::ColWarn, "PENDING: %s",
			COG_TCHAR_TO_CHAR(*Maps->PendingTravelDesc()));
	}

	// Buttons lead, text follows: a destination is "<map> @ <landmark>" and a landmark origin is
	// three floats, both long enough to push a trailing button off the right edge of a narrow window.
	// With the fixed-width widgets first it is the least critical text that truncates instead.
	int32 RowId = 0;
	for (FElysiumEntity* Ent : Changes)
	{
		ImGui::PushID(RowId++);
		const FString RowName = Ent->TargetName.IsEmpty() ? TEXT("(unnamed)") : Ent->TargetName;
		const FString Dest = FString::Printf(TEXT("-> %s @ %s"),
			*Ent->Def->Keys.FindRef(TEXT("map")), *Ent->Def->Keys.FindRef(TEXT("landmark")));

		if (ImGui::SmallButton("Inspect"))
		{
			SetSelection(Ent->Handle);
		}
		ImGui::SameLine();
		// Fires the ChangeLevel input (the scripted path), so a transition is testable without
		// walking into the volume.
		if (ImGui::SmallButton("Change now"))
		{
			World->EnqueueInput(TEXT("!self"), FName(TEXT("ChangeLevel")), FElysiumVariant::Void(), 0.0,
				FElysiumEntityHandle::Invalid(), Ent->Handle);
		}
		ImGui::SameLine();
		if (Ent->IsInert())
		{
			ImGui::TextColored(ElysiumCogStyle::ColInert, "%s", COG_TCHAR_TO_CHAR(*RowName));
		}
		else
		{
			ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*RowName));
		}
		ImGui::SameLine();
		ImGui::TextDisabled("%s", COG_TCHAR_TO_CHAR(*Dest));
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
			if (ImGui::SmallButton("Inspect"))
			{
				SetSelection(Ent->Handle);
			}
			ImGui::SameLine();
			ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*RowName));
			ImGui::SameLine();
			ImGui::TextDisabled("%s", COG_TCHAR_TO_CHAR(*Ent->Def->Origin.ToCompactString()));
			ImGui::PopID();
		}
	}
}

#endif // ENABLE_COG
