#include "Debug/ElysiumCogWindow_Entities.h"

#if ENABLE_COG

#include "Debug/ElysiumCogStyle.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"

#include "CogLocalizationConfig.h"   // COG_TCHAR_TO_CHAR
#include "CogWidgets.h"

namespace
{
	// live > hidden > dead ordering matches IsInert(): a record can be several of these at once,
	// so the strongest state wins the label. Record-only is orthogonal (shown by dimming).
	const char* StateLabel(const FElysiumEntity& Ent, ImVec4& OutColor)
	{
		if (Ent.IsDead())   { OutColor = ElysiumCogStyle::ColError; return "dead"; }
		if (Ent.IsHidden()) { OutColor = ElysiumCogStyle::ColWarn;  return "hidden"; }
		OutColor = ElysiumCogStyle::ColOk;
		return "live";
	}
}

void FElysiumCogWindow_Entities::Initialize()
{
	Super::Initialize();
	bHasMenu = false;
}

void FElysiumCogWindow_Entities::RenderHelp()
{
	ImGui::Text(
		"Browser over the Track-B entity substrate: every record including inert unhandled "
		"classnames. Filter by targetname/classname, toggle which dormancy states show, and read "
		"the classname histogram. Click a row to select an entity for the Entity Inspector.");
}

void FElysiumCogWindow_Entities::RenderContent()
{
	Super::RenderContent();

	FElysiumEntityWorld* World = GetEntityWorld();
	if (World == nullptr)
	{
		ImGui::TextDisabled("No .ents substrate on this map.");
		return;
	}

	const TArray<TUniquePtr<FElysiumEntity>>& Entities = World->Entities();
	if (!ImGui::BeginTabBar("##EntityViews"))
	{
		return;
	}

	if (ImGui::BeginTabItem("Browse"))
	{
		FCogWidgets::SearchBar("##EntityFilter", Filter);

		ImGui::Checkbox("Live", &bShowLive);       ImGui::SameLine();
		ImGui::Checkbox("Hidden", &bShowHidden);   ImGui::SameLine();
		ImGui::Checkbox("Dead", &bShowDead);       ImGui::SameLine();
		ImGui::Checkbox("Unhandled", &bShowRecordOnly);

	// Filtered index list.
		TArray<int32> Rows;
		Rows.Reserve(Entities.Num());
		for (int32 i = 0; i < Entities.Num(); ++i)
		{
		const FElysiumEntity* Ent = Entities[i].Get();
		if (Ent == nullptr)
		{
			continue;
		}
		if (Ent->IsDead())            { if (!bShowDead)       continue; }
		else if (Ent->IsHidden())     { if (!bShowHidden)     continue; }
		else                          { if (!bShowLive)       continue; }
		if (Ent->IsRecordOnly() && !bShowRecordOnly)
		{
			continue;
		}

		const FString Combined = FString::Printf(TEXT("%s %s"), *Ent->TargetName, *Ent->Def->Classname);
		if (!Filter.PassFilter(COG_TCHAR_TO_CHAR(*Combined)))
		{
			continue;
		}
			Rows.Add(i);
		}

	ImGui::Text("%d / %d records", Rows.Num(), Entities.Num());

	const ImGuiTableFlags TableFlags =
		ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY |
		ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;

		if (ImGui::BeginTable("##Entities", 4, TableFlags))
		{
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 44.0f);
		ImGui::TableSetupColumn("Targetname");
		ImGui::TableSetupColumn("Class");
		ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 56.0f);
		ImGui::TableHeadersRow();

		const FElysiumEntityHandle& Selected = GetSelection();

		ImGuiListClipper Clipper;
		Clipper.Begin(Rows.Num());
		while (Clipper.Step())
		{
			for (int32 Row = Clipper.DisplayStart; Row < Clipper.DisplayEnd; ++Row)
			{
				const FElysiumEntity& Ent = *Entities[Rows[Row]];
				ImGui::TableNextRow();

				ImGui::TableNextColumn();
				const bool bIsSelected = Ent.Handle == Selected;
				const FString RowId = FString::Printf(TEXT("%d##row%d"), Ent.Handle.Index, Ent.Handle.Index);
				if (ImGui::Selectable(COG_TCHAR_TO_CHAR(*RowId), bIsSelected,
					ImGuiSelectableFlags_SpanAllColumns))
				{
					SetSelection(Ent.Handle);
				}

				ImGui::TableNextColumn();
				ImGui::TextUnformatted(Ent.TargetName.IsEmpty()
					? "(none)" : COG_TCHAR_TO_CHAR(*Ent.TargetName));

				ImGui::TableNextColumn();
				if (Ent.IsRecordOnly())
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ElysiumCogStyle::ColInert);
					ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*Ent.Def->Classname));
					ImGui::PopStyleColor();
				}
				else
				{
					ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*Ent.Def->Classname));
				}

				ImGui::TableNextColumn();
				ImVec4 StateColor;
				const char* State = StateLabel(Ent, StateColor);
				ImGui::TextColored(StateColor, "%s", State);
			}
		}
			ImGui::EndTable();
		}
		ImGui::EndTabItem();
	}

	if (ImGui::BeginTabItem("Class breakdown"))
	{
		TMap<FName, int32> Counts;
		for (const TUniquePtr<FElysiumEntity>& EntPtr : Entities)
		{
			if (EntPtr)
			{
				Counts.FindOrAdd(FName(*EntPtr->Def->Classname))++;
			}
		}
		TArray<TPair<FName, int32>> Sorted;
		Sorted.Reserve(Counts.Num());
		for (const TPair<FName, int32>& KV : Counts)
		{
			Sorted.Add(KV);
		}
		Sorted.Sort([](const TPair<FName, int32>& A, const TPair<FName, int32>& B)
		{
			return A.Value > B.Value;
		});

		int32 MaxCount = 1;
		for (const TPair<FName, int32>& KV : Sorted)
		{
			MaxCount = FMath::Max(MaxCount, KV.Value);
		}

		ImGui::TextDisabled("%d classes across %d records", Sorted.Num(), Entities.Num());
		ImGui::BeginChild("##ClassBreakdown", ImVec2(0, 0), ImGuiChildFlags_Borders);
		for (const TPair<FName, int32>& KV : Sorted)
		{
			const FString Overlay = FString::Printf(TEXT("%s  (%d)"), *KV.Key.ToString(), KV.Value);
			ImGui::ProgressBar((float)KV.Value / (float)MaxCount, ImVec2(-1, 0),
				COG_TCHAR_TO_CHAR(*Overlay));
		}
		ImGui::EndChild();
		ImGui::EndTabItem();
	}
	ImGui::EndTabBar();
}

#endif // ENABLE_COG
