#include "Debug/ElysiumCogWindow_Logic.h"

#if ENABLE_COG

#include "Debug/ElysiumCogStyle.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"

#include "CogLocalizationConfig.h"   // COG_TCHAR_TO_CHAR
#include "imgui.h"

void FElysiumCogWindow_Logic::Initialize()
{
	Super::Initialize();
	bHasMenu = false;
}

void FElysiumCogWindow_Logic::RenderHelp()
{
	ImGui::Text(
		"Live board of the P4.5 tutorial-logic entities: math_counter values, logic_timer state, "
		"logic_case selection, env_fade, func_brush solidity, point_teleport, and the trigger family "
		"(hurt/look/autosave). Each row shows its runtime state, an Inspect button (hands it to the "
		"Entity Inspector), and a quick-fire of its primary input. The top panel shows the env_fade "
		"screen fade the HUD is currently drawing.");
}

void FElysiumCogWindow_Logic::RenderContent()
{
	Super::RenderContent();

	FElysiumEntityWorld* World = GetEntityWorld();
	if (World == nullptr)
	{
		ImGui::TextDisabled("No .ents substrate on this map.");
		return;
	}

	// --- env_fade screen fade (what the HUD draws this frame) -------------------------------
	ImGui::SeparatorText("Screen fade (env_fade)");
	FLinearColor FadeColor;
	if (World->GetScreenFade(FadeColor))
	{
		// Swatch first, then the numbers: the rgb/alpha string is the part that may run past a narrow
		// window edge, and it is the part you can read off the swatch anyway.
		const ImVec4 Swatch(FadeColor.R, FadeColor.G, FadeColor.B, 1.0f);
		ImGui::ColorButton("##fadecol", Swatch, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
			ImVec2(ImGui::GetTextLineHeight(), ImGui::GetTextLineHeight()));
		ImGui::SameLine();
		ImGui::TextColored(ElysiumCogStyle::ColOk, "ACTIVE");
		ImGui::SameLine();
		ImGui::Text("rgb (%.0f %.0f %.0f)  alpha %.2f",
			FadeColor.R * 255.f, FadeColor.G * 255.f, FadeColor.B * 255.f, FadeColor.A);
	}
	else
	{
		ImGui::TextDisabled("idle");
	}

	// --- The logic/point/brush classes ------------------------------------------------------
	RenderClassSection("math_counter", TEXT("math_counter"), TEXT("Add"), TEXT("1"));
	RenderClassSection("logic_timer", TEXT("logic_timer"), TEXT("Toggle"), TEXT(""));
	RenderClassSection("logic_case_toggle", TEXT("logic_case_toggle"), TEXT("InValue"), TEXT("1"));
	RenderClassSection("logic_case", TEXT("logic_case"), TEXT("PickRandom"), TEXT(""));
	RenderClassSection("env_fade", TEXT("env_fade"), TEXT("Fade"), TEXT(""));
	RenderClassSection("func_brush", TEXT("func_brush"), TEXT("Toggle"), TEXT(""));
	RenderClassSection("point_teleport", TEXT("point_teleport"), TEXT("Teleport"), TEXT(""));
	RenderClassSection("trigger_hurt", TEXT("trigger_hurt"), TEXT("Toggle"), TEXT(""));
	RenderClassSection("trigger_look", TEXT("trigger_look"), TEXT("Enable"), TEXT(""));
	RenderClassSection("trigger_autosave", TEXT("trigger_autosave"), TEXT("Enable"), TEXT(""));
}

void FElysiumCogWindow_Logic::RenderClassSection(const char* Label, const TCHAR* Classname,
	const TCHAR* QuickInput, const TCHAR* QuickParam)
{
	FElysiumEntityWorld* World = GetEntityWorld();
	if (World == nullptr)
	{
		return;
	}

	// Collect the live entities of this class first, so the header can show the count.
	TArray<FElysiumEntity*> Matches;
	for (const TUniquePtr<FElysiumEntity>& EntPtr : World->Entities())
	{
		FElysiumEntity* Ent = EntPtr.Get();
		if (Ent && Ent->Def && Ent->Def->Classname == Classname)
		{
			Matches.Add(Ent);
		}
	}
	if (Matches.Num() == 0)
	{
		return;   // don't clutter the board with classes this map doesn't use
	}

	const FString Header = FString::Printf(TEXT("%hs  (%d)"), Label, Matches.Num());
	if (!ImGui::CollapsingHeader(COG_TCHAR_TO_CHAR(*Header), ImGuiTreeNodeFlags_DefaultOpen))
	{
		return;
	}

	int32 RowId = 0;
	for (FElysiumEntity* Ent : Matches)
	{
		ImGui::PushID(RowId++);

		const bool bSelected = (GetSelection() == Ent->Handle);
		const FString RowName = Ent->TargetName.IsEmpty() ? TEXT("(unnamed)") : Ent->TargetName;

		// Fixed-width buttons lead so a long targetname truncates against the window edge instead of
		// pushing them out of reach.
		if (ImGui::SmallButton("Inspect"))
		{
			SetSelection(Ent->Handle);
		}
		if (QuickInput && *QuickInput)
		{
			ImGui::SameLine();
			const FString BtnLabel = FString::Printf(TEXT("%s%s"), QuickInput,
				(QuickParam && *QuickParam) ? *FString::Printf(TEXT(" %s"), QuickParam) : TEXT(""));
			if (ImGui::SmallButton(COG_TCHAR_TO_CHAR(*BtnLabel)))
			{
				World->EnqueueInput(TEXT("!self"), FName(QuickInput),
					FElysiumVariant::String(QuickParam ? QuickParam : TEXT("")), 0.0,
					FElysiumEntityHandle::Invalid(), Ent->Handle);
			}
		}

		// Name (coloured while inert so a hidden/dead logic node is obvious, and while selected so
		// the row the Entity Inspector is holding is findable in a long board).
		ImGui::SameLine();
		if (Ent->IsInert())
		{
			ImGui::TextColored(ElysiumCogStyle::ColInert, "%s", COG_TCHAR_TO_CHAR(*RowName));
		}
		else if (bSelected)
		{
			ImGui::TextColored(ElysiumCogStyle::ColSelected, "%s", COG_TCHAR_TO_CHAR(*RowName));
		}
		else
		{
			ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*RowName));
		}

		// Live state (GetDebugState), indented under the row on one value column so a section of
		// several entities reads as a table rather than as ragged prose.
		TArray<TPair<FString, FString>> State;
		Ent->GetDebugState(State);
		ImGui::Indent();
		const float ValueColumn = ImGui::GetCursorPosX() + GetDpiScale() * 104.0f;
		for (const TPair<FString, FString>& KV : State)
		{
			ImGui::TextDisabled("%s:", COG_TCHAR_TO_CHAR(*KV.Key));
			ImGui::SameLine(0.0f, 0.0f);
			ImGui::SetCursorPosX(FMath::Max(
				ImGui::GetCursorPosX() + ImGui::GetStyle().ItemSpacing.x, ValueColumn));
			ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*KV.Value));
		}
		ImGui::Unindent();

		ImGui::PopID();
	}
}

#endif // ENABLE_COG
