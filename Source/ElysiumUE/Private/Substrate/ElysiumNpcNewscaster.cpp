#include "Substrate/ElysiumNpcNewscaster.h"

#include "ElysiumContentPaths.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumKeyValues.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSessionSubsystem.h"
#include "ElysiumStub.h"
#include "Misc/FileHelper.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcDialogue.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcLifecycle2_2Shared.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumWeaponClasses.h"

// --- File-scope helpers moved with this class's bodies (story 5 step 4) ---

namespace
{
	// The Newscaster's two files. `0x1064aadc` / `0x1064aac0` are FORMAT strings and `0x105a0f80` is
	// the one vararg, so `UTIL_VarArgs` (`0x101d3730`) builds `vdata\system\Newscaster_Main.txt`.
	const TCHAR* const GNewscasterDir = TEXT("system");
	const TCHAR* const GNewscasterMainFile = TEXT("Newscaster_Main.txt");
	const TCHAR* const GNewscasterSideFile = TEXT("Newscaster_Side.txt");
	// `0x1064aab8` (the `strstr` on the key name), `0x1064aab0` (the story-name default) and
	// `0x105a18d4` (the key it is read from).
	const TCHAR* const GNewscasterStoryKeyToken = TEXT("Story");
	const TCHAR* const GNewscasterDefaultStoryName = TEXT("STORY");
	// `0x103a08b5` — `if (versions >= 4) DevWarning("too many versions in %s! skipping %s")`.
	constexpr int32 GNewscasterMaxVersions = 4;
	// `0x103a07f0`, the per-`Story` parser. A file static because its retail argument is a
	// `KeyValues*` and the family's `.inl` is included inside `class FElysiumNpc`.
	//
	// Answers false for a record that must not be appended, exactly as retail's `XOR AL,AL` tail at
	// `103a09fd` does after it has released the record's strings.
	bool ParseNewscasterStoryKey(const FElysiumNpc& Npc, const FString& KeyName,
		const ElysiumKeyValues::FKvNode& Key, FElysiumNpc::FNewscasterStory& OutStory)
	{
		// `103a0803`: `strstr(key->GetName(), "Story")`. A key that does not carry the token warns
		// and falls through with a version count of zero, which the tail then refuses.
		if (!KeyName.Contains(GNewscasterStoryKeyToken))
		{
			UE_LOG(LogElysiumNpcEnt, Warning, TEXT("Newscaster: invalid key! (%s)"), *KeyName);
			return false;
		}
		// `103a081f`: `GetString("Name", "STORY")` — an unnamed story is literally `STORY`.
		OutStory.Name = Key.Str(TEXT("Name"), FString(GNewscasterDefaultStoryName));
		// `103a0872`: `FindKey("Version")` then `GetNextKey()` — retail walks SIBLINGS from the first
		// `Version`, so a non-`Version` sibling after it is visited too and contributes nothing
		// (no `filename` means no increment). Walking the `Version` children answers the same set for
		// every authored file in the corpus and is what the reader below expresses.
		for (const TPair<FString, TSharedPtr<ElysiumKeyValues::FKvNode>>& Child : Key.Kids)
		{
			if (Child.Key != TEXT("version") || !Child.Value.IsValid())
			{
				continue;
			}
			// `103a0893` / `103a08a5`: `GetString("dependency", 0)` and `GetString("filename", 0)`.
			const FString* Dependency = Child.Value->Value(TEXT("dependency"));
			const FString* Filename = Child.Value->Value(TEXT("filename"));
			if (OutStory.Versions.Num() >= GNewscasterMaxVersions)
			{
				UE_LOG(LogElysiumNpcEnt, Warning,
					TEXT("Newscaster: too many versions in %s! skipping %s"), *OutStory.Name,
					Filename != nullptr ? **Filename : TEXT(""));
				continue;
			}
			// `103a0916`: the version counter advances only inside the FILENAME block, so a version
			// with a dependency and no filename occupies no slot at all.
			if (Filename == nullptr || Filename->IsEmpty())
			{
				continue;
			}
			FElysiumNpc::FNewscasterStory::FVersion Version;
			Version.Dependency = Dependency != nullptr ? *Dependency : FString();
			Version.Filename = *Filename;
			OutStory.Versions.Add(MoveTemp(Version));
		}
		// `103a098d`..`103a0a0b`, the tail and the CORRECTION: the first version whose dependency is
		// absent, empty, or evaluates non-zero wins, and its INDEX is what `+0x24` holds. No version
		// qualifying answers false and the record is dropped.
		for (int32 Index = 0; Index < OutStory.Versions.Num(); ++Index)
		{
			const FString& Dependency = OutStory.Versions[Index].Dependency;
			if (Dependency.IsEmpty() || Npc.EvalNewscasterDependency(Dependency))
			{
				OutStory.SelectedVersion = Index;
				return true;
			}
		}
		return false;
	}
}

// Slot 420: `0x103a0420`.
// `0x103a0420`
void FElysiumNpcNewscaster::NPCInit()
{
	TroikaNPCInit();
	Senses.Memory.NextFleeSoundTime = 0.0;
	NpcKernelLifecycle19_2Shared::Lifecycle19_2LawNever(*this);
	SeedCriminalLevelWitnessed();
	InvestigateMode = 0;
	InvestigateModeCombat = 0;
	SetFrenziedWord(0);
	Senses.bCanPerformSenses = false;
	bIsBccTargetable = false;
}

// Slot 180: `0x103a03a0`, which ends in `TroikaUpdateOnRemove`.
/** `CNPC_VNewscaster::UpdateOnRemove` (`0x103a03a0`) — family Species' `FUN_103a0d50` (the two
 *  story-queue teardowns and the active-story byte), then the Troika body. */
void FElysiumNpcNewscaster::UpdateOnRemove()
{
	// `CNPC_VNewscaster::vfunc180` `0x103a03a0` — `thunk_FUN_103a0d50(this)` then the Troika body.
	// `0x103a0d50` is family Species' `FUN_103a0d50`: the two story queues torn down row by row and
	// the story-active byte cleared.
	FUN_103a0d50();
	TroikaUpdateOnRemove();
}

// Slot 404: `0x103a01b0`, eight bytes, `return 4;`. It never looks at the candidate, never reaches
// the table and never reaches the Troika body, so a newscaster is `D_NU` toward everything, itself
// and null included.
int32 FElysiumNpcNewscaster::IRelationType(FElysiumEntity* Candidate)
{
	(void)Candidate;
	return 4;   // D_NU
}

// --- Moved from `ElysiumNpcDebug10.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcSpecies2.cpp` (story 5 step 4) ---

void FElysiumNpcNewscaster::FUN_103a0d50()
{
	// `0x103a0d50`, the story-queue teardown: retail drains the main queue (+0x665c, count +0x6668)
	// then the side queue (+0x6670, count +0x667c) front-first, releasing each 0x28 record's nine
	// handles, and clears the stories-loaded byte (+0x6690) LAST so the next think reloads. The
	// drain is `TArray::Reset` (the rows own their strings); the byte is the rule kept.
	NewscasterMainStories.Reset();
	NewscasterSideStories.Reset();
	bNewscasterStoryActive = false;
}

// --- Moved from `ElysiumNpcSpeciesMisc10.cpp` (story 5 step 4) ---

void FElysiumNpcNewscaster::LoadNewscasterStories()
{
	// `103a0abd`: tear BOTH queues down first — family Species' `FUN_103a0d50`.
	FUN_103a0d50();

	// `103a0ac2` / `103a0bb8`: `UTIL_VarArgs("%sNewscaster_Main.txt", "vdata\system\")`. CORRECTION:
	// the strings are FORMATS with a `%s`, not `\s`-prefixed paths.
	const FString Dir = FElysiumContentPaths::VdataDir() / GNewscasterDir;
	auto LoadQueue = [&Dir](const TCHAR* File, TArray<FElysiumNpc::FNewscasterStory>& OutQueue,
		const FElysiumNpc& Npc)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *(Dir / File)))
		{
			// CRASH GUARD, named: retail's `103a0ba7` reads `[EDI+4]` with `EDI == 0` when the
			// KeyValues load fails, so a missing file faults. Nothing of retail's behaviour is
			// reachable past that point, so refusing the queue changes no observable order.
			UE_LOG(LogElysiumNpcEnt, Warning,
				TEXT("Newscaster: %s is missing (retail faults here)"), File);
			return;
		}
		const TSharedPtr<ElysiumKeyValues::FKvNode> Root = ElysiumKeyValues::ParseText(Text);
		if (!Root.IsValid())
		{
			return;
		}
		// `103a0aef`: `root->GetFirstSubKey()` then `GetNextKey()` — every child of the file's one
		// top-level block (`NewsData`), in authored order.
		const ElysiumKeyValues::FKvNode* Data = Root->Kids.Num() == 1 && Root->Kids[0].Value.IsValid()
			? Root->Kids[0].Value.Get() : Root.Get();
		for (const TPair<FString, TSharedPtr<ElysiumKeyValues::FKvNode>>& Child : Data->Kids)
		{
			if (!Child.Value.IsValid())
			{
				continue;
			}
			// `103a0b00`: the ten-word scratch row is zeroed per key, so nothing leaks between rows.
			FElysiumNpc::FNewscasterStory Story;
			// `103a0b15`: appended ONLY when the parser answers true.
			if (ParseNewscasterStoryKey(Npc, Child.Key, *Child.Value, Story))
			{
				OutQueue.Add(MoveTemp(Story));
			}
		}
	};
	LoadQueue(GNewscasterMainFile, NewscasterMainStories, *this);
	LoadQueue(GNewscasterSideFile, NewscasterSideStories, *this);
	// `103a0cae`: the loaded flag last.
	bNewscasterStoryActive = true;
}

void FElysiumNpcNewscaster::PlayNextNewscasterStory()
{
	// `103a0678`: with the loaded flag clear, the player gate decides — and a missing player returns
	// WITHOUT loading, so the next call tries again.
	if (!bNewscasterStoryActive)
	{
		if (!NewscasterPlayerPresent())
		{
			return;
		}
		LoadNewscasterStories();
		// `103a069b`: seed BOTH cursors with `RandomInt(0, count - 1)`. An empty queue hands
		// `RandomInt(0, -1)`, which retail answers with 0.
		FRandomStream& Rng = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule);
		NewscasterMainCursor = NewscasterMainStories.Num() > 0
			? Rng.RandRange(0, NewscasterMainStories.Num() - 1) : 0;
		NewscasterSideCursor = NewscasterSideStories.Num() > 0
			? Rng.RandRange(0, NewscasterSideStories.Num() - 1) : 0;
	}
	// `103a06c8`: `IsInDialog` (`0x102c1170`) refuses the whole rest of the body.
	if (Dialogue.bInDialog)
	{
		return;
	}
	const int32 MainCount = NewscasterMainStories.Num();
	const int32 SideCount = NewscasterSideStories.Num();
	// `103a06dd`: both queues empty does nothing.
	if (MainCount + SideCount == 0)
	{
		return;
	}
	// `103a06ef`: `RandomInt(0, count0 + count1)` — an INCLUSIVE upper bound, so the roll can equal
	// the sum and land on the side queue once more often than the counts alone would say. Retail's
	// arithmetic, reproduced.
	const int32 Roll = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(0, MainCount + SideCount);
	NewscasterPlayingSide = (Roll < MainCount) ? 1 : 0;

	// `103a0719`: a zero `+0x668c` OR an empty main queue takes the SIDE cursor. Note the polarity —
	// `+0x668c` non-zero selects the MAIN queue here, which is the opposite of the name family
	// Species gave it for the dead debug overlay (0019/6), which highlighted a MAIN row when
	// `+0x668c == 0`. Retail read the same word with opposite senses there; that is a retail
	// inconsistency rather than a port error.
	const TArray<FNewscasterStory>* Queue = nullptr;
	int32 Index = 0;
	if (NewscasterPlayingSide == 0 || MainCount == 0)
	{
		// `103a0730`: an empty SIDE queue returns early, without advancing anything.
		if (SideCount == 0)
		{
			return;
		}
		NewscasterSideCursor = (NewscasterSideCursor + 1 >= SideCount)
			? 0 : NewscasterSideCursor + 1;
		Index = NewscasterSideCursor;
		Queue = &NewscasterSideStories;
	}
	else
	{
		// `103a0762`: the MAIN cursor, advanced modulo its count the same way.
		NewscasterMainCursor = (NewscasterMainCursor + 1 >= MainCount)
			? 0 : NewscasterMainCursor + 1;
		Index = NewscasterMainCursor;
		Queue = &NewscasterMainStories;
	}
	// `103a0789`: `record + 8 + selected * 8` — the SELECTED VERSION's filename, and nothing when it
	// is null.
	if (!Queue->IsValidIndex(Index))
	{
		return;
	}
	const FNewscasterStory& Story = (*Queue)[Index];
	if (!Story.Versions.IsValidIndex(Story.SelectedVersion))
	{
		return;
	}
	const FString& File = Story.Versions[Story.SelectedVersion].Filename;
	if (File.IsEmpty())
	{
		return;
	}
	// `103a07a3`: `0x102c0520(this, filename, 0, 0)` — `OnDialogFilePlayed`.
	NewscasterPlayedFiles.Add(File);
	// `0x102c0520` is the spoken-line player, and the port carries its TALKING half only
	// (`OnDialogFilePlayed`, `ElysiumNpc.cpp:2177`): the `scripted_scene` create and the `sound\`
	// prefix strip have no counterpart here. The duration retail passes from this call site is 0.
	OnDialogFilePlayed(0.0);
}
