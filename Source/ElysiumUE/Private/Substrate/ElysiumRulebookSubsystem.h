#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "Substrate/ElysiumRulebook.h"

#include "ElysiumRulebookSubsystem.generated.h"

// GameInstance-scoped owner of VtMB's rulebook — the `vdata/system/` tables the RPG layer reads.
//
// Session-lifetime and never invalidated, because **no `vdata` value is ever saved**: the sheet
// stores indices into these tables and the tables are re-read from disk at load, which is what
// lets a patched rulebook re-apply to a save made before the patch (`docs/vtmb/savegame_format.md`).
//
// Each table loads lazily on its first accessor and is cached for the session; a load failure is
// logged once and remembered, so a missing export costs one warning rather than one per read.
// `LoadAll` forces the set — what `elysium.rules` and `Elysium.Content.Rulebook` call.
//
// `dispositiontable.txt` is NOT here: it is owned by `UElysiumAnimSubsystem`, beside the
// animation data its `Animation Name` column keys.
UCLASS()
class UElysiumRulebookSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	const FElysiumStatTable&          Stats();
	const FElysiumFeatTable&          Feats();
	const FElysiumRules&              Rules();
	const FElysiumTraitEffects&       TraitEffects();
	const FElysiumClanTable&          Clans();
	const FElysiumHistoryTable&       Histories();
	const FElysiumQuestTables&        Quests();
	const FElysiumExperienceTable&    Experience();
	const FElysiumLevelingTemplates&  Leveling();
	const FElysiumWizard&             Wizard();
	const FElysiumStrings&            Strings();
	const FElysiumDiceTables&         Dice();
	// `vdata/items/*.txt`. Its first load also registers one entity class per definition
	// (`ElysiumItems::Install`), so it has to happen before a map's item entities are created —
	// `FElysiumEntityWorld::Load` touches it for exactly that reason.
	const FElysiumItemTable&          Items();

	// Force every table. Returns the number that loaded clean.
	int32 LoadAll();

	// One table's load state, for the verb and the test: name, file, row count, and the error when
	// it did not load.
	struct FStatus
	{
		FString Table;
		FString File;
		int32 Rows = 0;
		bool bLoaded = false;
		FString Error;
	};
	void GetStatus(TArray<FStatus>& Out);

private:
	void RegisterCommands();
	void ExecRules(const TArray<FString>& Args);
	void ExecRoll(const TArray<FString>& Args);

	// The lazy-load guard is set BEFORE the load, so a failure is remembered rather than retried
	// on every read — the idiom `UElysiumAnimSubsystem` established.
	template <typename T>
	const T& Get(T& Table, bool& bLoaded, const TCHAR* Name, FString& Error);

	FElysiumStatTable         StatTable;
	FElysiumFeatTable         FeatTable;
	FElysiumRules             RuleData;
	FElysiumTraitEffects      EffectData;
	FElysiumClanTable         ClanTable;
	FElysiumHistoryTable      HistoryTable;
	FElysiumQuestTables       QuestTables;
	FElysiumExperienceTable   ExperienceTable;
	FElysiumLevelingTemplates LevelingTemplates;
	FElysiumWizard            WizardData;
	FElysiumStrings           StringData;
	FElysiumDiceTables        DiceTables;
	FElysiumItemTable         ItemTable;

	bool bStatsLoaded = false;
	bool bFeatsLoaded = false;
	bool bRulesLoaded = false;
	bool bEffectsLoaded = false;
	bool bClansLoaded = false;
	bool bHistoriesLoaded = false;
	bool bQuestsLoaded = false;
	bool bExperienceLoaded = false;
	bool bLevelingLoaded = false;
	bool bWizardLoaded = false;
	bool bStringsLoaded = false;
	bool bDiceLoaded = false;
	bool bItemsLoaded = false;

	FString StatsError;
	FString FeatsError;
	FString RulesError;
	FString EffectsError;
	FString ClansError;
	FString HistoriesError;
	FString QuestsError;
	FString ExperienceError;
	FString LevelingError;
	FString WizardError;
	FString StringsError;
	FString DiceError;
	FString ItemsError;

	TArray<IConsoleObject*> ConsoleObjects;
};
