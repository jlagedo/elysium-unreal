#include "ElysiumCogWindow_EventQueue.h"

#if ENABLE_COG

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
		"queue's service loop; Step releases one due event at a time for causality debugging.");
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
	ImGui::Text("now %.2f s  ·  %s%s", Now,
		bPaused ? "PAUSED" : "running",
		(bPaused && Queue.StepsPending() > 0)
			? COG_TCHAR_TO_CHAR(*FString::Printf(TEXT("  (%d step armed)"), Queue.StepsPending())) : "");

	// --- Pending events --------------------------------------------------------------------
	const TArray<FElysiumIOEvent>& Pending = Queue.Pending();
	ImGui::SeparatorText(COG_TCHAR_TO_CHAR(*FString::Printf(TEXT("Pending (%d)"), Pending.Num())));

	if (Pending.Num() == 0)
	{
		ImGui::TextDisabled("Queue empty.");
	}
	else if (ImGui::BeginTable("##Pending", 6,
		ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY |
		ImGuiTableFlags_ScrollX | ImGuiTableFlags_SizingFixedFit,
		ImVec2(0, GetDpiScale() * 180.0f)))
	{
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("In");
		ImGui::TableSetupColumn("Target");
		ImGui::TableSetupColumn("Input");
		ImGui::TableSetupColumn("Param");
		ImGui::TableSetupColumn("Caller");
		ImGui::TableSetupColumn("Py");
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
			ImGui::TextUnformatted(Ev.PythonSrc.IsEmpty() ? "" : "py");
		}
		ImGui::EndTable();
	}

	// --- I/O history ring buffer -----------------------------------------------------------
	const FElysiumRingBufferSink& Ring = World->RingBuffer();
	ImGui::SeparatorText(COG_TCHAR_TO_CHAR(*FString::Printf(
		TEXT("I/O history (%d / %d)"), Ring.Num(), Ring.Capacity())));

	TArray<FString> Lines;
	Ring.CollectOrdered(HistoryLines, Lines);
	if (Lines.Num() == 0)
	{
		ImGui::TextDisabled("No I/O delivered yet.");
	}
	else
	{
		// EndChild must always follow BeginChild, so keep the pair inside this branch.
		ImGui::BeginChild("##History", ImVec2(0, 0), ImGuiChildFlags_Borders,
			ImGuiWindowFlags_HorizontalScrollbar);
		for (const FString& L : Lines)
		{
			ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*L));
		}
		// Follow the tail as new lines land (the ring records newest-last).
		if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
		{
			ImGui::SetScrollHereY(1.0f);
		}
		ImGui::EndChild();
	}
}

#endif // ENABLE_COG
