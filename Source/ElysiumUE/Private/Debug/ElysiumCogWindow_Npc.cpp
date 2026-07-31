#include "Debug/ElysiumCogWindow_Npc.h"

#if ENABLE_COG

#include "Debug/ElysiumCogStyle.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
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

// The live `npc_*` / `npc_maker` entities in the loaded map (B3): what stands where and its latch
// state. Distinct from the glTF test harness below — these are the map's own characters, driven by
// the entity substrate, not by the elysium.npc.load spike.
void FElysiumCogWindow_Npc::RenderLiveNpcs()
{
	FElysiumEntityWorld* EW = GetEntityWorld();
	ImGui::SeparatorText("Live NPCs  (map entities)");
	if (EW == nullptr)
	{
		ImGui::TextDisabled("No entity world (load a map).");
		return;
	}

	// Collect npc_*/npc_maker entities off the world's entity list.
	TArray<const FElysiumEntity*> Npcs;
	for (const TUniquePtr<FElysiumEntity>& EntPtr : EW->Entities())
	{
		const FElysiumEntity* E = EntPtr.Get();
		if (E && E->Def && E->Def->Classname.StartsWith(TEXT("npc_")))
		{
			Npcs.Add(E);
		}
	}

	ImGui::Text("%d NPC entities", Npcs.Num());
	if (Npcs.Num() == 0)
	{
		return;
	}

	const ImGuiTableFlags TableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
		ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
	if (ImGui::BeginTable("##LiveNpcs", 3, TableFlags, ImVec2(0, GetDpiScale() * 130.f)))
	{
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Name");
		ImGui::TableSetupColumn("Class");
		ImGui::TableSetupColumn("State");
		ImGui::TableHeadersRow();
		for (const FElysiumEntity* E : Npcs)
		{
			// Join the leaf's debug rows into one compact "k=v · k=v" cell — generic over both the
			// character leaf (WillTalk/UseInteresting/In dialog/...) and the maker (Enabled/NPCType/...).
			TArray<TPair<FString, FString>> Rows;
			E->GetDebugState(Rows);
			FString State;
			for (const TPair<FString, FString>& Row : Rows)
			{
				if (!State.IsEmpty()) { State += TEXT("  ·  "); }
				State += FString::Printf(TEXT("%s=%s"), *Row.Key, *Row.Value);
			}

			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextColored(E->IsInert() ? ElysiumCogStyle::ColDim : ElysiumCogStyle::ColName,
				"%s", COG_TCHAR_TO_CHAR(E->TargetName.IsEmpty() ? TEXT("(noname)") : *E->TargetName));
			ImGui::TableNextColumn(); ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*E->Def->Classname));
			ImGui::TableNextColumn(); ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*State));
		}
		ImGui::EndTable();
	}
}

void FElysiumCogWindow_Npc::RenderContent()
{
	Super::RenderContent();

	RenderLiveNpcs();

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
	// Both boxes stretch to the window, minus the Rescan button beside the first, so the two fields
	// line up at the same right edge instead of at ImGui's default 65%-of-window item width.
	ImGui::SeparatorText("Load an NPC  (.glb under out/npc)");
	const float RescanWidth = ImGui::CalcTextSize("Rescan").x + ImGui::GetStyle().FramePadding.x * 2.0f
		+ ImGui::GetStyle().ItemSpacing.x;
	const float FieldWidth = FMath::Max(GetDpiScale() * 120.0f,
		ImGui::GetContentRegionAvail().x - RescanWidth);

	ImGui::SetNextItemWidth(FieldWidth);
	FCogWidgets::InputTextWithHint("##Stem", "gangmember_male_2", PendingStem);
	ImGui::SameLine();
	if (ImGui::SmallButton("Rescan"))
	{
		bStemsDirty = true;
	}
	ImGui::SetNextItemWidth(FieldWidth);
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
		ImGui::TextColored(ElysiumCogStyle::ColError, "%s", COG_TCHAR_TO_CHAR(*LastError));
	}

	// --- Available assets (one-click Load) --------------------------------------------------
	ImGui::SeparatorText("Available");
	if (Stems.Num() == 0)
	{
		ImGui::TextDisabled("No .glb under out/npc. Export one:");
		ImGui::TextDisabled("  dev/elysium.ps1 export <map> --npc");
	}
	else
	{
		// EndChild pairs with BeginChild unconditionally: BeginChild returns false when the region is
		// fully clipped (scrolled out of view), and skipping EndChild on that frame trips ImGui's
		// begin/end balance assert.
		ImGui::BeginChild("##Stems", ImVec2(0, GetDpiScale() * 90.f), ImGuiChildFlags_Borders);
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
			if (ImGui::Selectable(COG_TCHAR_TO_CHAR(*Stems[i]), PendingStem == Stems[i]))
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
		// Wrap on the measured width rather than a fixed count per row: clip names run from "idle" to
		// "combat_knife_attack2", so four-per-row overflows for some models and wastes a third of the
		// window for others.
		const float ClipsRight = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
		for (int32 i = 0; i < Last.AnimNames.Num(); ++i)
		{
			if (i > 0)
			{
				const float NextWidth = ImGui::CalcTextSize(COG_TCHAR_TO_CHAR(*Last.AnimNames[i])).x
					+ ImGui::GetStyle().FramePadding.x * 2.0f;
				if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + NextWidth < ClipsRight)
				{
					ImGui::SameLine();
				}
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
