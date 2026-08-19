#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "Substrate/ElysiumDisposition.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumSkillClasses.h"

#include "ElysiumRulebookSubsystem.generated.h"

// One lazily loaded rulebook table: the parsed data, the load-attempted guard, and the remembered
// load error. The guard is set BEFORE the load, so a failure is remembered rather than retried on
// every read — the idiom `UElysiumAnimSubsystem` established.
template <typename T>
struct TElysiumLazyTable
{
	T Table;
	bool bLoaded = false;
	FString Error;
};

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
	// `vdata/system/sound_volume_table.txt` — the radius and occlusion policy of every named game
	// sound. Read by the substrate sound-event bus when a producer emits without an explicit radius.
	const FElysiumSoundVolumeTable&   SoundVolumes();
	// `vdata/system/stealth.txt` — the four `StealthData` tables the player's target surface reads:
	// the sight-range and cone scalar matrices, the hearing-distance reduction and the descending
	// light-row thresholds. Read by `Substrate/ElysiumStealth.h`'s recompute, never by the senses.
	const FElysiumStealthTables&      Stealth();
	// `vdata/system/dispositiontable.txt`. Both halves of a row are read from here: the
	// `Animation Name` column the standing-idle vocabulary is keyed on, and the fidget/stance-change
	// pacing the disposition stance machine rolls against.
	const FElysiumDispositionTable&   Dispositions();
	// `vdata/system/reaction.txt` + `reactions000.txt`. The RPG/social reaction score band table
	// and its modifier catalogue — the third social domain (K4), read by the pure calculator in
	// `Substrate/ElysiumReaction.h`, never by combat targeting.
	const FElysiumReactionCatalogue&  Reactions();
	// `vdata/system/disciplinetgt_000..004.txt` — the targeted Discipline records the cast
	// transaction interprets (`docs/vtmb/disciplines.md` § "Targeted `DisciplineTgt` path").
	const FElysiumDisciplineTargets&  DisciplineTargets();
	// `vdata/items/*.txt`. Its first load also registers one entity class per definition
	// (`ElysiumItems::Install`), so it has to happen before a map's item entities are created —
	// `FElysiumEntityWorld::Load` touches it for exactly that reason.
	const FElysiumItemTable&          Items();
	// Patch-first `vdata/hackterminals/*`, keyed by normalized virtual path. Unlike the fixed
	// tables above this catalogue is demand-driven by map entities, but each named definition is
	// still parsed once for the session and shared by every placement that references it.
	const FElysiumTerminalDefinition* TerminalDefinition(const FString& VirtualPath,
		FString& OutError);

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

	template <typename T>
	const T& Get(TElysiumLazyTable<T>& Slot, const TCHAR* Name);

	TElysiumLazyTable<FElysiumStatTable>         StatTable;
	TElysiumLazyTable<FElysiumFeatTable>         FeatTable;
	TElysiumLazyTable<FElysiumRules>             RuleData;
	TElysiumLazyTable<FElysiumTraitEffects>      EffectData;
	TElysiumLazyTable<FElysiumClanTable>         ClanTable;
	TElysiumLazyTable<FElysiumHistoryTable>      HistoryTable;
	TElysiumLazyTable<FElysiumQuestTables>       QuestTables;
	TElysiumLazyTable<FElysiumExperienceTable>   ExperienceTable;
	TElysiumLazyTable<FElysiumLevelingTemplates> LevelingTemplates;
	TElysiumLazyTable<FElysiumWizard>            WizardData;
	TElysiumLazyTable<FElysiumStrings>           StringData;
	TElysiumLazyTable<FElysiumDiceTables>        DiceTables;
	TElysiumLazyTable<FElysiumSoundVolumeTable>  SoundVolumeTable;
	TElysiumLazyTable<FElysiumStealthTables>     StealthTableSet;
	TElysiumLazyTable<FElysiumItemTable>         ItemTable;
	TElysiumLazyTable<FElysiumDispositionTable>  DispositionTable;
	TElysiumLazyTable<FElysiumReactionCatalogue> ReactionCatalogue;
	TElysiumLazyTable<FElysiumDisciplineTargets> DisciplineTargetTable;
	TMap<FString, TSharedPtr<FElysiumTerminalDefinition>> TerminalDefinitions;
	TMap<FString, FString> TerminalDefinitionErrors;

	TArray<IConsoleObject*> ConsoleObjects;
};
