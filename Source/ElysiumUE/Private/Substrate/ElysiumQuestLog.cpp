#include "Substrate/ElysiumQuestLog.h"

namespace ElysiumQuestLog
{
	EType ParseType(const FString& Raw)
	{
		// The engine's own order and its own operator: `Q_stristr`, first match wins, and anything
		// that matches none of the four reads as incomplete.
		if (Raw.Contains(TEXT("incomplete"), ESearchCase::IgnoreCase)) { return EType::Incomplete; }
		if (Raw.Contains(TEXT("success"),    ESearchCase::IgnoreCase)) { return EType::Success; }
		if (Raw.Contains(TEXT("failure"),    ESearchCase::IgnoreCase)) { return EType::Failure; }
		if (Raw.Contains(TEXT("botch"),      ESearchCase::IgnoreCase)) { return EType::Botch; }
		return EType::Incomplete;
	}

	const FElysiumAssignedQuest* FindRow(const TArray<FElysiumAssignedQuest>& Journal,
		const FString& Title)
	{
		const FString Key = Title.TrimStartAndEnd();
		for (const FElysiumAssignedQuest& Row : Journal)
		{
			if (Row.Title.Equals(Key, ESearchCase::IgnoreCase))
			{
				return &Row;
			}
		}
		return nullptr;
	}

	static FElysiumAssignedQuest* FindRowMutable(TArray<FElysiumAssignedQuest>& Journal,
		const FString& Title)
	{
		return const_cast<FElysiumAssignedQuest*>(FindRow(Journal, Title));
	}

	FOutcome Apply(const FElysiumQuestTables& Tables, TArray<FElysiumAssignedQuest>& Journal,
		const FString& Title, int32 NewState)
	{
		FOutcome Out;

		// 1. The catalogue. An unknown title does nothing — the engine's lookup returns -1 and the
		//    whole setter is skipped. The caller still stores the raw value in the quest map.
		FElysiumQuestRef Ref;
		const FElysiumQuest* Quest = Tables.Find(Title.TrimStartAndEnd(), &Ref);
		if (!Quest)
		{
			return Out;
		}

		// 2. The state, addressed by ORDINAL. A state the quest does not have is the same
		//    do-nothing as an unknown title (the engine's `stateIdx[N-1] == -1` guard).
		const FElysiumQuestState* State = Quest->StateByOrdinal(NewState);
		if (!State)
		{
			return Out;
		}

		Out.bResolved  = true;
		Out.Title      = Quest->Title;
		Out.DisplayName = Quest->DisplayName;
		Out.Type       = State->Type;

		// 3. The gate reads the JOURNAL row, not the catalogue.
		FElysiumAssignedQuest* Row = FindRowMutable(Journal, Quest->Title);
		if (Row)
		{
			if (Row->State == NewState)
			{
				// A repeat set. Nothing is owed and nothing is touched.
				Out.Order = Row->Order;
				return Out;
			}
			// Leaving a `botch` state is refused outright — the engine raises an Error and stops,
			// leaving the journal untouched. No shipped row authors `botch`, so this is unreachable
			// on retail data; it is here because the mechanism is real, not because the data is.
			if (const FElysiumQuestState* Leaving = Quest->StateByOrdinal(Row->State))
			{
				if (ParseType(Leaving->Type) == EType::Botch)
				{
					Out.bBotched = true;
					Out.Order = Row->Order;
					return Out;
				}
			}
		}

		// 4. `Order` is assigned once, on first assignment, as max(order) + 1 — so the first quest
		//    assigned in a run gets 1. An existing row keeps the order it was given.
		int32 Order = Row ? Row->Order : 0;
		if (!Row)
		{
			for (const FElysiumAssignedQuest& Other : Journal)
			{
				Order = FMath::Max(Order, Other.Order + 1);
			}
			Order = FMath::Max(Order, 1);
		}

		// 5. Replace in place, or append. Never two rows for one quest.
		if (!Row)
		{
			Row = &Journal.AddDefaulted_GetRef();
			Row->Title = Quest->Title;
		}
		Row->Table   = Ref.Table;
		Row->Quest   = Ref.Quest;
		Row->State   = NewState;
		Row->Order   = Order;
		Row->bUnread = true;

		Out.bChanged   = true;
		Out.Order      = Order;
		Out.AwardXpKey = State->AwardXp;
		Out.AwardMoney = State->AwardMoney;
		Out.Event      = State->Event;
		return Out;
	}
}
