#include "ElysiumCogWindow_Npc.h"

#if ENABLE_COG

#include "ElysiumNpcSubsystem.h"

#include "CogLocalizationConfig.h"   // COG_TCHAR_TO_CHAR
#include "CogWidgets.h"
#include "imgui.h"

void FElysiumCogWindow_Npc::Initialize()
{
	Super::Initialize();
	bHasMenu = false;
}

void FElysiumCogWindow_Npc::RenderHelp()
{
	ImGui::Text(
		"glTFRuntime skeletal-path spike (P8 8.2). Pick a VtMB NPC exported to out/npc/<stem>.glb "
		"(mdl_gltf.py: mesh + StudioBone skeleton + one RLE animation, a standard glTF 2.0 file) and "
		"Load it: UElysiumNpcSubsystem runs it through glTFRuntime into a runtime USkeletalMesh + "
		"UAnimSequence and spawns a skeletal-mesh actor in front of the player, playing the clip on a "
		"single-node anim instance. The table shows what each load produced -- bone count, the "
		"animations present in the glb, the applied clip, load time, spawn location. Per-clip buttons "
		"re-play any animation; Clear destroys the spawned NPCs. Same path the elysium.npc.* verbs drive.");
}

void FElysiumCogWindow_Npc::RenderContent()
{
	Super::RenderContent();

	UElysiumNpcSubsystem* Npc = GetNpcSubsystem();
	if (Npc == nullptr)
	{
		ImGui::TextDisabled("NPC subsystem unavailable.");
		return;
	}

	if (bStemsDirty)
	{
		Stems = UElysiumNpcSubsystem::AvailableGlbStems();
		bStemsDirty = false;
		if (PendingStem.IsEmpty() && Stems.Num() > 0)
		{
			PendingStem = Stems[0];
		}
	}

	// --- Load an NPC ------------------------------------------------------------------------
	ImGui::SeparatorText("Load an NPC  (.glb under out/npc)");
	FCogWidgets::InputTextWithHint("##Stem", "gangmember_male_2", PendingStem);
	ImGui::SameLine();
	if (ImGui::SmallButton("Rescan"))
	{
		bStemsDirty = true;
	}
	FCogWidgets::InputTextWithHint("##Anim", "(first animation)", PendingAnim);

	ImGui::BeginDisabled(PendingStem.IsEmpty());
	if (ImGui::Button("Load"))
	{
		LastError.Reset();
		if (Npc->LoadTestNpc(PendingStem, PendingAnim, LastError) != nullptr)
		{
			LastError.Reset();
		}
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Clear all"))
	{
		Npc->ClearNpcs();
	}
	if (!LastError.IsEmpty())
	{
		ImGui::TextColored(ImVec4(1.f, 0.4f, 0.35f, 1.f), "%s", COG_TCHAR_TO_CHAR(*LastError));
	}

	// --- Available assets (one-click Load) --------------------------------------------------
	ImGui::SeparatorText("Available");
	if (Stems.Num() == 0)
	{
		ImGui::TextDisabled("No .glb under out/npc. Export one:");
		ImGui::TextDisabled("  python tools/mdl_gltf.py <model.mdl> <anim> out/npc");
	}
	else if (ImGui::BeginChild("##Stems", ImVec2(0, GetDpiScale() * 90.f), ImGuiChildFlags_Borders))
	{
		for (int32 i = 0; i < Stems.Num(); ++i)
		{
			ImGui::PushID(i);
			if (ImGui::SmallButton("Load"))
			{
				PendingStem = Stems[i];
				LastError.Reset();
				Npc->LoadTestNpc(Stems[i], PendingAnim, LastError);
			}
			ImGui::SameLine();
			if (ImGui::Selectable(COG_TCHAR_TO_CHAR(*Stems[i])))
			{
				PendingStem = Stems[i];
			}
			ImGui::PopID();
		}
		ImGui::EndChild();
	}

	// --- Loaded (what glTFRuntime produced) -------------------------------------------------
	const TArray<FElysiumLoadedNpc>& Live = Npc->GetLoaded();
	ImGui::SeparatorText("Loaded");
	ImGui::Text("%d spawned", Live.Num());
	if (Live.Num() == 0)
	{
		return;
	}

	const ImGuiTableFlags TableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
		ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
	if (ImGui::BeginTable("##Npcs", 6, TableFlags, ImVec2(0, GetDpiScale() * 130.f)))
	{
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Stem");
		ImGui::TableSetupColumn("Bones", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 46.f);
		ImGui::TableSetupColumn("Clip");
		ImGui::TableSetupColumn("Anims", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 44.f);
		ImGui::TableSetupColumn("Load ms", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 58.f);
		ImGui::TableSetupColumn("Location");
		ImGui::TableHeadersRow();
		for (const FElysiumLoadedNpc& Record : Live)
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn(); ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*Record.Stem));
			ImGui::TableNextColumn(); ImGui::Text("%d", Record.NumBones);
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(Record.AnimName.IsEmpty() ? "(ref pose)" : COG_TCHAR_TO_CHAR(*Record.AnimName));
			ImGui::TableNextColumn(); ImGui::Text("%d", Record.NumAnims);
			ImGui::TableNextColumn(); ImGui::Text("%.1f", Record.LoadMilliseconds);
			ImGui::TableNextColumn(); ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*Record.Location.ToCompactString()));
		}
		ImGui::EndTable();
	}

	// Per-clip re-play for the most-recent load: audition any animation the glb carries.
	const FElysiumLoadedNpc& Last = Live.Last();
	if (Last.AnimNames.Num() > 0)
	{
		ImGui::SeparatorText(COG_TCHAR_TO_CHAR(*FString::Printf(TEXT("Clips in %s  (re-load with clip)"), *Last.Stem)));
		for (int32 i = 0; i < Last.AnimNames.Num(); ++i)
		{
			if (i % 4 != 0)
			{
				ImGui::SameLine();
			}
			ImGui::PushID(i);
			if (ImGui::SmallButton(COG_TCHAR_TO_CHAR(*Last.AnimNames[i])))
			{
				PendingAnim = Last.AnimNames[i];
				LastError.Reset();
				Npc->LoadTestNpc(Last.Stem, Last.AnimNames[i], LastError);
			}
			ImGui::PopID();
		}
	}
}

#endif // ENABLE_COG
