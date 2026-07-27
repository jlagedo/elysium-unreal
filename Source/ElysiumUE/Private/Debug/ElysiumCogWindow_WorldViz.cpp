#include "Debug/ElysiumCogWindow_WorldViz.h"

#if ENABLE_COG

#include "ElysiumBrushComponent.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDebugSubsystem.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "Debug/ElysiumGizmoColor.h"

#include "CogLocalizationConfig.h"   // COG_TCHAR_TO_CHAR
#include "CogWidgets.h"
#include "Engine/World.h"
#include "imgui.h"

void FElysiumCogWindow_WorldViz::Initialize()
{
	Super::Initialize();
	bHasMenu = false;
}

void FElysiumCogWindow_WorldViz::RenderHelp()
{
	ImGui::Text(
		"World-visualization layers, drawn in-world by the entity debug subsystem every frame (they "
		"stay on while you play, F1 menu closed or not). Entity gizmos: a color-keyed origin marker "
		"per entity, off / visible (walls occlude) / all (x-ray), filterable by class - the filter "
		"rows are drawn in each class's own gizmo colour, so they double as the legend. Show triggers: the wireframe hulls "
		"of every trigger brush, by class or by enabled/dormant state. I/O beams: a fading caller->"
		"target arrow on each I/O delivery. These are the same toggles as elysium.ent_gizmos / "
		"showtriggers / ent_beams. Aim at a gizmo and open the Entity Inspector to select it.");
}

void FElysiumCogWindow_WorldViz::RenderContent()
{
	Super::RenderContent();

	UWorld* GameWorld = GetWorld();
	UElysiumEntityDebugSubsystem* Dbg = GameWorld
		? GameWorld->GetSubsystem<UElysiumEntityDebugSubsystem>() : nullptr;
	if (Dbg == nullptr)
	{
		ImGui::TextDisabled("Entity debug subsystem unavailable.");
		return;
	}

	FElysiumEntityWorld* World = GetEntityWorld();
	if (World == nullptr)
	{
		ImGui::TextDisabled("No .ents substrate on this map (nothing to visualize).");
		return;
	}

	UElysiumEntityDebugSubsystem::FVizSettings& V = Dbg->Viz();
	using EGizmoMode = UElysiumEntityDebugSubsystem::EGizmoMode;

	// --- Entity gizmos ---------------------------------------------------------------------
	ImGui::SeparatorText("Entity gizmos");
	int Mode = static_cast<int>(V.GizmoMode);
	ImGui::TextUnformatted("Mode");
	ImGui::SameLine();
	ImGui::RadioButton("Off", &Mode, static_cast<int>(EGizmoMode::Off));       ImGui::SameLine();
	ImGui::RadioButton("Visible", &Mode, static_cast<int>(EGizmoMode::Visible)); ImGui::SameLine();
	ImGui::RadioButton("All (x-ray)", &Mode, static_cast<int>(EGizmoMode::All));
	V.GizmoMode = static_cast<EGizmoMode>(Mode);

	ImGui::BeginDisabled(V.GizmoMode == EGizmoMode::Off);
	ImGui::Checkbox("Labels (within 6 m)", &V.bGizmoLabels);
	ImGui::SameLine();
	FCogWidgets::HelpMarker("Boxes are a retained GPU instanced-mesh layer (built once, updated only "
		"when an entity's state changes) - no per-frame cost, so all entities show. Labels are the "
		"exception (no instanced text), so they render only for gizmos within 6 m of the camera.");

	// Filter by class. Doubles as the colour legend it replaces — each row is drawn in the colour
	// that class's gizmos are, so reading the key and choosing what to show is one control.
	if (ImGui::CollapsingHeader("Filter by type", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (ImGui::SmallButton("Select all"))
		{
			V.GizmoClassMask = ElysiumGizmoClassMaskAll;
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Clear"))
		{
			V.GizmoClassMask = 0;
		}
		ImGui::SameLine();
		ImGui::TextDisabled("(%d/%d)", FMath::CountBits(V.GizmoClassMask),
			int32(EElysiumGizmoClass::Count));

		for (uint8 i = 0; i < uint8(EElysiumGizmoClass::Count); ++i)
		{
			const EElysiumGizmoClass Cls = EElysiumGizmoClass(i);
			const uint8 Bit = ElysiumGizmoClassBit(Cls);
			bool bOn = (V.GizmoClassMask & Bit) != 0;

			const FColor C = ElysiumGizmoClassColor(Cls);
			ImGui::PushStyleColor(ImGuiCol_Text,
				ImVec4(C.R / 255.f, C.G / 255.f, C.B / 255.f, 1.f));
			ImGui::PushID(i);
			if (ImGui::Checkbox(COG_TCHAR_TO_CHAR(ElysiumGizmoClassLabel(Cls)), &bOn))
			{
				V.GizmoClassMask = bOn ? (V.GizmoClassMask | Bit) : (V.GizmoClassMask & ~Bit);
			}
			ImGui::PopID();
			ImGui::PopStyleColor();
		}
		ImGui::TextDisabled("(hidden/dormant entities draw dimmed)");
	}
	ImGui::EndDisabled();

	// --- Show triggers ---------------------------------------------------------------------
	ImGui::SeparatorText("Show triggers");
	ImGui::Checkbox("Wireframe trigger hulls", &V.bShowTriggers);
	ImGui::BeginDisabled(!V.bShowTriggers);
	ImGui::Checkbox("Color by enabled/dormant state (else by class)", &V.bTriggerColorByState);
	ImGui::EndDisabled();

	// --- I/O beams -------------------------------------------------------------------------
	ImGui::SeparatorText("I/O beams");
	ImGui::Checkbox("Fading caller->target arrows on fire", &V.bShowBeams);
	ImGui::BeginDisabled(!V.bShowBeams);
	// Stretch minus a gutter for the label ImGui draws to the slider's right (see the Lights window).
	ImGui::SetNextItemWidth(FMath::Max(GetDpiScale() * 110.0f, ImGui::GetContentRegionAvail().x
		- ImGui::CalcTextSize("Fade window").x - ImGui::GetStyle().ItemInnerSpacing.x));
	ImGui::SliderFloat("Fade window", &V.BeamSeconds, 0.5f, 15.0f, "%.1f s");
	ImGui::EndDisabled();
	ImGui::TextDisabled("Beams follow I/O delivery - hand-fire from the Entity Inspector to see one.");

	// --- Live counts (a quick sense of what the layers are drawing) ------------------------
	const TArray<TUniquePtr<FElysiumEntity>>& Entities = World->Entities();
	int32 NumLive = 0, NumTriggers = 0;
	for (const TUniquePtr<FElysiumEntity>& EntPtr : Entities)
	{
		const FElysiumEntity* E = EntPtr.Get();
		if (!E || E->IsDead() || !E->Def)
		{
			continue;
		}
		++NumLive;
		if (E->Def->IsBrush() && (E->Def->Classname.StartsWith(TEXT("trigger")) ||
			(E->Body && E->Body->GetSolidity() == EElysiumBrushSolidity::Trigger)))
		{
			++NumTriggers;
		}
	}
	ImGui::SeparatorText("Scene");
	ImGui::Text("%d entities · %d triggers", NumLive, NumTriggers);
}

#endif // ENABLE_COG
