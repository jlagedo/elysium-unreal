#include "ElysiumGameStateSubsystem.h"

#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumState, Log, All);

void UElysiumGameStateSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// `elysium.g <name> [value]` — inspect/poke the G store by hand. No args dumps every
	// set flag; one arg reads (miss -> 0); a second arg writes (integer if it parses, else
	// string; the literal `none` deletes the key, matching G's None-deletes semantics).
	ConsoleObjects.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("elysium.g"),
		TEXT("elysium.g [name [value]] — read/write a G global flag (no args: dump all)"),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			if (Args.Num() == 0)
			{
				TArray<FString> Keys = GlobalKeys();
				Keys.Sort();
				UE_LOG(LogElysiumState, Display, TEXT("G: %d flags"), Keys.Num());
				for (const FString& Key : Keys)
				{
					UE_LOG(LogElysiumState, Display, TEXT("  %s = %s"), *Key, *GetGlobal(Key).Describe());
				}
				return;
			}

			const FString& Key = Args[0];
			if (Args.Num() == 1)
			{
				UE_LOG(LogElysiumState, Display, TEXT("G.%s = %s"), *Key, *GetGlobal(Key).Describe());
				return;
			}

			const FString& Raw = Args[1];
			if (Raw.Equals(TEXT("none"), ESearchCase::IgnoreCase))
			{
				ClearGlobal(Key);
			}
			else if (Raw.IsNumeric())
			{
				SetGlobalInt(Key, FCString::Atoi(*Raw));
			}
			else
			{
				SetGlobal(Key, FElysiumVariant::String(Raw));
			}
			UE_LOG(LogElysiumState, Display, TEXT("G.%s = %s"), *Key, *GetGlobal(Key).Describe());
		}),
		ECVF_Cheat));
}

void UElysiumGameStateSubsystem::Deinitialize()
{
	for (IConsoleObject* Obj : ConsoleObjects)
	{
		IConsoleManager::Get().UnregisterConsoleObject(Obj);
	}
	ConsoleObjects.Empty();

	Globals.Empty();
	Quests.Empty();
	Clock.Reset();

	Super::Deinitialize();
}

FElysiumVariant UElysiumGameStateSubsystem::GetGlobal(const FString& Key) const
{
	if (const FElysiumVariant* Found = Globals.Find(Key))
	{
		return *Found;
	}
	return FElysiumVariant::Int(0); // default-on-miss = 0 (confirmed, python_bridge.md)
}

void UElysiumGameStateSubsystem::SetGlobal(const FString& Key, const FElysiumVariant& Value)
{
	// Assigning None (Void here) deletes the key — tp_setattr semantics.
	if (Value.IsVoid())
	{
		Globals.Remove(Key);
		return;
	}
	Globals.Add(Key, Value);
}

bool UElysiumGameStateSubsystem::HasGlobal(const FString& Key) const
{
	return Globals.Contains(Key);
}

void UElysiumGameStateSubsystem::ClearGlobal(const FString& Key)
{
	Globals.Remove(Key);
}

void UElysiumGameStateSubsystem::ClearAllGlobals()
{
	Globals.Empty();
}

TArray<FString> UElysiumGameStateSubsystem::GlobalKeys() const
{
	TArray<FString> Keys;
	Globals.GetKeys(Keys);
	return Keys;
}

int32 UElysiumGameStateSubsystem::GetQuestState(const FString& Quest) const
{
	const int32* Found = Quests.Find(Quest);
	return Found ? *Found : 0;
}

void UElysiumGameStateSubsystem::SetQuestState(const FString& Quest, int32 State)
{
	Quests.Add(Quest, State);
}

bool UElysiumGameStateSubsystem::HasQuest(const FString& Quest) const
{
	return Quests.Contains(Quest);
}
