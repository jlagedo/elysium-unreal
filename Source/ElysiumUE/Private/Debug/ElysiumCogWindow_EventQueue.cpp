#include "Debug/ElysiumCogWindow_EventQueue.h"

#if ENABLE_COG

#include "Debug/ElysiumCogStyle.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumEventQueue.h"
#include "ElysiumIOSink.h"

#include "CogLocalizationConfig.h"   // COG_TCHAR_TO_CHAR
#include "CogWidgets.h"

void FElysiumCogWindow_EventQueue::Initialize()
{
	Super::Initialize();
	bHasMenu = false;
}

void FElysiumCogWindow_EventQueue::RenderHelp()
{
	ImGui::Text(
		"The Track-B event queue: the pending time-sorted I/O deliveries with their fire times, the "
		"always-on I/O history ring buffer, and pause / single-step controls. Pausing holds the "
		"queue's service loop; Step releases one due event at a time for causality debugging.\n\n"
		"A row with Target '(python)' is a field-6 payload or a ScheduleTask deferred source (5.4) - it "
		"carries no I/O input, just the Python string in the last column, evaluated at its fire time.");
}

void FElysiumCogWindow_EventQueue::RenderContent()
{
	Super::RenderContent();

	FElysiumEntityWorld* World = GetEntityWorld();
	if (World == nullptr)
	{
		ImGui::TextDisabled("No .ents substrate on this map.");
		return;
	}

	FElysiumEventQueue& Queue = World->Queue();
	const double Now = World->NowSeconds();

	// --- Pause / step controls -------------------------------------------------------------
	const bool bPaused = Queue.IsPaused();
	if (ImGui::Button(bPaused ? "Resume" : "Pause"))
	{
		if (bPaused) { Queue.Resume(); } else { Queue.Pause(); }
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(!bPaused);
	if (ImGui::Button("Step"))
	{
		Queue.RequestSteps(1);
	}
	ImGui::SameLine();
	if (ImGui::Button("Step 10"))
	{
		Queue.RequestSteps(10);
	}
	ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::TextDisabled("%.2f s", Now);
	ImGui::SameLine();
	ImGui::TextColored(bPaused ? ElysiumCogStyle::ColWarn : ElysiumCogStyle::ColOk, "%s",
		bPaused ? "paused" : "running");
	if (bPaused && Queue.StepsPending() > 0)
	{
		ImGui::SameLine();
		ImGui::TextDisabled("(%d step armed)", Queue.StepsPending());
	}

	const TArray<FElysiumIOEvent>& Pending = Queue.Pending();
	const FElysiumRingBufferSink& Ring = World->RingBuffer();
	if (!ImGui::BeginTabBar("##EventFlowViews"))
	{
		return;
	}

	const FString PendingLabel = FString::Printf(TEXT("Pending  %d###Pending"), Pending.Num());
	if (ImGui::BeginTabItem(COG_TCHAR_TO_CHAR(*PendingLabel)))
	{
		if (Pending.Num() == 0)
		{
			ImGui::TextDisabled("Nothing is waiting to fire.");
		}
		else if (ImGui::BeginTable("##Pending", 6,
		ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY |
		ImGuiTableFlags_ScrollX | ImGuiTableFlags_SizingFixedFit,
		ImVec2(0, 0)))
		{
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("In");
		ImGui::TableSetupColumn("Target");
		ImGui::TableSetupColumn("Input");
		ImGui::TableSetupColumn("Param");
		ImGui::TableSetupColumn("Caller");
		ImGui::TableSetupColumn("Python (field-6 / ScheduleTask)");
		ImGui::TableHeadersRow();

		for (const FElysiumIOEvent& Ev : Pending)
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::Text("%+.2f s", Ev.FireTime - Now);
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(Ev.Target.IsEmpty() ? "(python)" : COG_TCHAR_TO_CHAR(*Ev.Target));
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*Ev.Input.ToString()));
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*Ev.Param.ToString()));
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*World->DescribeHandle(Ev.Caller)));
			ImGui::TableNextColumn();
			// The deferred field-6 / ScheduleTask source, verbatim (a python-only event has no Target,
			// so it shows "(python)" in the Target column and its source here).
			if (!Ev.PythonSrc.IsEmpty())
			{
				ImGui::TextColored(ElysiumCogStyle::ColName, "%s", COG_TCHAR_TO_CHAR(*Ev.PythonSrc));
			}
		}
			ImGui::EndTable();
		}
		ImGui::EndTabItem();
	}

	const FString HistoryLabel = FString::Printf(TEXT("History  %d###History"), Ring.Num());
	if (ImGui::BeginTabItem(COG_TCHAR_TO_CHAR(*HistoryLabel)))
	{
		ImGui::TextDisabled("Showing the latest %d of %d deliveries", HistoryLines, Ring.Capacity());
		TArray<FString> Lines;
		Ring.CollectOrdered(HistoryLines, Lines);
		if (Lines.Num() == 0)
		{
			ImGui::TextDisabled("No I/O has been delivered yet.");
		}
		else
		{
			ImGui::BeginChild("##History", ImVec2(0, 0), ImGuiChildFlags_Borders,
				ImGuiWindowFlags_HorizontalScrollbar);
			for (const FString& L : Lines)
			{
				ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*L));
			}
			if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
			{
				ImGui::SetScrollHereY(1.0f);
			}
			ImGui::EndChild();
		}
		ImGui::EndTabItem();
	}
	ImGui::EndTabBar();
}

#endif // ENABLE_COG
