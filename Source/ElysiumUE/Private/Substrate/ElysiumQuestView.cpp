#include "Substrate/ElysiumQuestView.h"

namespace
{
	// A row belongs to the tab if it is that hub's, or if it is cross-hub. `main` is cross-hub by
	// design; an unresolved table is cross-hub by necessity, so the row is never invisible.
	bool ShowsUnder(const FElysiumAssignedQuest& Row, int32 Hub)
	{
		return Row.Table == Hub
			|| Row.Table == FElysiumQuestTables::MainTable
			|| Row.Table == INDEX_NONE;
	}

}

namespace ElysiumQuestView
{
	FEntry ResolveRow(const FElysiumQuestTables& Tables, const FElysiumAssignedQuest& Row)
	{
		FEntry Entry;
		Entry.Title   = Row.Title;
		Entry.Table   = Row.Table;
		Entry.State   = Row.State;
		Entry.Order   = Row.Order;
		Entry.bUnread = Row.bUnread;

		const FElysiumQuest* Quest = Tables.At({ Row.Table, Row.Quest });
		const FElysiumQuestState* State = Quest ? Quest->StateByOrdinal(Row.State) : nullptr;

		// Both halves degrade independently: a quest we know with a state we do not still shows its
		// display name, and a row we cannot place at all still shows its own title.
		Entry.DisplayName = Quest && !Quest->DisplayName.IsEmpty() ? Quest->DisplayName : Row.Title;
		Entry.bResolved   = State != nullptr;
		if (State)
		{
			Entry.Description = State->Description;
			Entry.RawType     = State->Type;
			Entry.Type        = ElysiumQuestLog::ParseType(State->Type);
		}
		return Entry;
	}

	FView Build(const FElysiumQuestTables& Tables, const TArray<FElysiumAssignedQuest>& Journal, int32 Hub)
	{
		FView View;

		for (const FElysiumAssignedQuest& Row : Journal)
		{
			const FEntry Entry = ResolveRow(Tables, Row);

			// The tab counts are over the whole journal, not over this hub's slice, because the row
			// of tabs has to report hubs the player is not currently looking at.
			if (Entry.Type == ElysiumQuestLog::EType::Incomplete
				|| Entry.Type == ElysiumQuestLog::EType::Botch)
			{
				for (int32 t = 0; t < FElysiumQuestTables::NumTables; ++t)
				{
					if (ShowsUnder(Row, t))
					{
						++View.HubActive[t];
					}
				}
			}

			if (!ShowsUnder(Row, Hub))
			{
				continue;
			}

			switch (Entry.Type)
			{
			case ElysiumQuestLog::EType::Success: View.Completed.Add(Entry); break;
			case ElysiumQuestLog::EType::Failure: View.Failed.Add(Entry);    break;
			// `incomplete` and the unauthored `botch` are both still open.
			default:                              View.Active.Add(Entry);    break;
			}
		}

		// Newest assignment first, so what changed most recently reads first. Ties fall back to the
		// title so the order is total — two rows can share an Order only in a hand-built journal, but
		// an unstable sort there would make a test flaky rather than fail honestly.
		auto ByOrderDesc = [](const FEntry& A, const FEntry& B)
		{
			return A.Order != B.Order ? A.Order > B.Order : A.Title < B.Title;
		};
		View.Active.Sort(ByOrderDesc);
		View.Completed.Sort(ByOrderDesc);
		View.Failed.Sort(ByOrderDesc);

		return View;
	}

	int32 DefaultHub(const FElysiumQuestTables& Tables, const TArray<FElysiumAssignedQuest>& Journal)
	{
		const FView Counts = Build(Tables, Journal, INDEX_NONE);

		int32 Best = FElysiumQuestTables::HubTabOrder[0];
		int32 BestCount = -1;
		// Walk in tab order, strictly greater to win, so a tie resolves to the leftmost tab.
		for (int32 Hub : FElysiumQuestTables::HubTabOrder)
		{
			if (Counts.HubActive[Hub] > BestCount)
			{
				Best = Hub;
				BestCount = Counts.HubActive[Hub];
			}
		}
		return Best;
	}
}
