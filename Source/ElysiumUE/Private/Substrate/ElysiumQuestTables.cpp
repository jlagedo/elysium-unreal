#include "Substrate/ElysiumQuestTables.h"

#include "ElysiumContentPaths.h"
#include "ElysiumKeyValues.h"

#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumVdataLoad.h"

namespace
{
	using ElysiumKeyValues::FKvNode;
	using ElysiumVdata::ReadVdata;
}

// ================================================================================================
// 7. the five quests_*.txt
// ================================================================================================

// Fixed and alphabetical, because the save stores the table index: re-ordering this array would
// silently re-point every journal entry in every existing save.
const TCHAR* FElysiumQuestTables::HubNames[5] =
{
	TEXT("chinatown"), TEXT("downtown"), TEXT("hollywood"), TEXT("main"), TEXT("santamonica"),
};

const FElysiumQuestState* FElysiumQuest::StateById(int32 Id) const
{
	for (const FElysiumQuestState& State : States)
	{
		if (State.Id == Id)
		{
			return &State;
		}
	}
	return nullptr;
}

const FElysiumQuestState* FElysiumQuest::StateByOrdinal(int32 OneBased) const
{
	if (OneBased < 1 || OneBased > MaxStates || OneBased > States.Num())
	{
		return nullptr;
	}
	return &States[OneBased - 1];
}

bool FElysiumQuestTables::Load(FString& OutError)
{
	for (int32 i = 0; i < NumTables; ++i)
	{
		Quests[i].Reset();
	}
	ByTitle.Reset();

	int32 Loaded = 0;
	for (int32 t = 0; t < NumTables; ++t)
	{
		const FString Rel = FString::Printf(TEXT("system/quests_%s.txt"), HubNames[t]);
		TSharedPtr<FKvNode> Root;
		FString FileError;
		if (!ReadVdata(*Rel, Root, FileError))
		{
			OutError = FileError;
			continue;
		}
		const FKvNode* Table = Root.IsValid() ? Root->Child(TEXT("QuestTable")) : nullptr;
		if (Table == nullptr)
		{
			OutError = FString::Printf(TEXT("no QuestTable block in %s"),
				*FElysiumContentPaths::VdataFile(Rel));
			continue;
		}
		++Loaded;

		// `quests_main.txt` holds only its documentation comment — zero quests, and clean.
		for (const TPair<FString, TSharedPtr<FKvNode>>& Kid : Table->Kids)
		{
			if (Kid.Key != TEXT("quest") || !Kid.Value.IsValid())
			{
				continue;
			}
			FElysiumQuest Quest;
			Quest.TableIndex = t;
			Quest.Index = Quests[t].Num();
			// `QuestJournal::AddQuest` Q_trimspace's both, and the shipped files do author padding
			// around a Title — so the stored spelling is the trimmed one everywhere.
			Quest.Title = Kid.Value->Str(TEXT("Title"), FString()).TrimStartAndEnd();
			Quest.DisplayName = Kid.Value->Str(TEXT("DisplayName"), Quest.Title).TrimStartAndEnd();

			for (const TPair<FString, TSharedPtr<FKvNode>>& StateKid : Kid.Value->Kids)
			{
				if (StateKid.Key != TEXT("completionstate") || !StateKid.Value.IsValid())
				{
					continue;
				}
				const FKvNode& N = *StateKid.Value;
				FElysiumQuestState State;
				State.Id = N.Int(TEXT("ID"), 0);
				State.Description = N.Str(TEXT("Description"), FString());
				State.Type = N.Str(TEXT("Type"), FString());
				// A KEY into experience_table.txt, not a number — the shipped header comment is
				// wrong about this and every one of the 161 authored values is an id.
				State.AwardXp = N.Str(TEXT("AwardXP"), FString());
				State.AwardMoney = N.Int(TEXT("AwardMoney"), 0);
				State.Event = N.Str(TEXT("Event"), FString());
				Quest.States.Add(MoveTemp(State));
			}

			Quests[t].Add(MoveTemp(Quest));
		}
	}

	Reindex();

	if (Loaded == 0)
	{
		return false;
	}
	OutError.Reset();
	return true;
}

void FElysiumQuestTables::Reindex()
{
	ByTitle.Reset();
	for (int32 t = 0; t < NumTables; ++t)
	{
		for (int32 q = 0; q < Quests[t].Num(); ++q)
		{
			ByTitle.Add(ElysiumFold(Quests[t][q].Title.TrimStartAndEnd()), FElysiumQuestRef{ t, q });
		}
	}
}

const FElysiumQuest* FElysiumQuestTables::Find(const FString& Title, FElysiumQuestRef* OutRef) const
{
	const FElysiumQuestRef* Ref = ByTitle.Find(ElysiumFold(Title.TrimStartAndEnd()));
	if (Ref == nullptr)
	{
		return nullptr;
	}
	if (OutRef) { *OutRef = *Ref; }
	return At(*Ref);
}

const FElysiumQuest* FElysiumQuestTables::At(const FElysiumQuestRef& Ref) const
{
	if (Ref.Table < 0 || Ref.Table >= NumTables || !Quests[Ref.Table].IsValidIndex(Ref.Quest))
	{
		return nullptr;
	}
	return &Quests[Ref.Table][Ref.Quest];
}

int32 FElysiumQuestTables::NumQuests() const
{
	int32 N = 0;
	for (int32 i = 0; i < NumTables; ++i) { N += Quests[i].Num(); }
	return N;
}

int32 FElysiumQuestTables::NumStates() const
{
	int32 N = 0;
	for (int32 i = 0; i < NumTables; ++i)
	{
		for (const FElysiumQuest& Q : Quests[i]) { N += Q.States.Num(); }
	}
	return N;
}
