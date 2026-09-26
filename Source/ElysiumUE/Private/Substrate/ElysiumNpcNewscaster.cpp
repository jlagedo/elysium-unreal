#include "Substrate/ElysiumNpcNewscaster.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"
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
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcDebug10Shared.h"
#include "Substrate/ElysiumNpcLifecycle19_2Shared.h"
#include "Substrate/ElysiumNpcPrecache10Shared.h"
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
	const TCHAR* const GNewscasterSoundDir = TEXT("sound/character/conversations/news/tv");
	// `CNPC_VNewscaster`'s two debug headers, verbatim from `.rdata`.
	constexpr const TCHAR* NewscasterNotPlayingText = TEXT("not playing VCD");
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

const FElysiumNpcClass* FElysiumNpcNewscaster::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(RetailClassName);
	return Row;
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
	NpcFlags.SetFrenziedWord(0);
	Senses.bCanPerformSenses = false;
	bIsBccTargetable = false;
}

// Slot 104: `0x103a03e0`.
// 0x103a03e0
void FElysiumNpcNewscaster::Precache()
{
	// `CNPC_VNewscaster::Precache` `0x103a03e0` — the Troika body, then the news/tv conversation
	// directory globbed twice: `.mp3` FIRST and `.wav` second, the reverse of the Troika body's own
	// pair. Both calls pass `bStarPrefix` CLEAR and the precache flag 0 (`PUSH 0x0 / PUSH 0x0`),
	// where the Troika body passes 1 and 0 — three call sites of one function, three argument pairs.
	TroikaPrecache();
	PrecacheDirectory(GNewscasterSoundDir, NpcKernelPrecache10Shared::GExtMp3, /*bStarPrefix=*/false, /*Flag=*/0);
	PrecacheDirectory(GNewscasterSoundDir, NpcKernelPrecache10Shared::GExtWav, /*bStarPrefix=*/false, /*Flag=*/0);
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

// Slot 124: `0x103a1250`
/** `CNPC_VNewscaster::DrawDebugTextOverlays` (`0x103a1250`) — a scope-trace push, the Troika body,
 *  then `0x103a0ff0`'s story-queue lines added to the SAME line budget under bit 0. */
int32 FElysiumNpcNewscaster::DrawDebugTextOverlays()
{
	// `0x103a1250`, 151 bytes. A scope-trace push carrying `GetDebugName()` — `"NULL ENTITY"`
	// (`0x105387dc`) for a null `this`, the empty string for a null `m_iName` — then the Troika body,
	// then `0x103a0ff0`'s lines. `0x103a0ff0` returns a COUNT and the newscaster adds it to the
	// Troika body's answer, so the two share one budget.
	UE_LOG(LogElysiumNpcEnt, VeryVerbose, TEXT("CNPC_VNewscaster::DrawDebugTextOverlays %s"),
		TargetName.IsEmpty() ? TEXT("") : *TargetName);
	const int32 Base = TroikaDrawDebugTextOverlays();
	if ((DebugOverlays & NpcKernelDebug10Shared::GDebug10BitText) == 0)
	{
		return Base;
	}
	return Base + NewscasterStoryOverlayLines(Base);
}

// --- Moved from `ElysiumNpcDebug10.cpp` (story 5 step 4) ---

int32 FElysiumNpcNewscaster::NewscasterStoryOverlayLines(int32 FirstLine)
{
	// SEAM for `0x103a0ff0`, family Species' row in band 5–9. Its answer is a COUNT of lines, which
	// the newscaster adds to the Troika body's line index.
	(void)FirstLine;
	return 0;
}

// --- Moved from `ElysiumNpcSpecies2.cpp` (story 5 step 4) ---

void FElysiumNpcNewscaster::FUN_103a0d50()
{
	// `0x103a0d50`, the story-queue TEARDOWN:
	//
	//     while (m_MainCount (+0x6668) != 0) {
	//         Release(main[0].object);                            // FUN_10430964
	//         for (o = 0; o < 0x20; o += 8) { Release(*(main + 4 + o)); Release(*(main + 8 + o)); }
	//         if (0 < m_MainCount - 1) memmove(main, main + 0x28, (m_MainCount - 1) * 0x28);
	//         m_MainCount -= 1;
	//     }
	//     ... the identical loop again over the SIDE queue (+0x6670, count +0x667c) ...
	//     field_0x6690 = 0;
	//
	// Two facts about the loops, both retail's:
	//   * the release is a **FRONT** removal, not a swap-remove: the whole remainder is memmoved
	//     down by one `0x28` record each pass, so the queue keeps its order while it drains. The two
	//     blacklists in this family's other half do the opposite, which is why this one is spelled
	//     out rather than shared.
	//   * the inner `for` releases EIGHT handles per record — `+0x04`/`+0x08` at four strides of 8 —
	//     plus the record's own object at `+0x00`, so nine per row.
	//
	// Story **29d**, family SpeciesMisc10, gave `FNewscasterStory` the four `(dependency, filename)`
	// pairs and the chosen index that `0x103a07f0` fills — so the nine handles this loop releases per
	// row now have port counterparts, and `Reset()` frees them with the row. Nothing here stands a
	// VCD ENTITY, so the observable is still the drain itself. The story-active flag is cleared last,
	// exactly as retail does.
	NewscasterMainStories.Reset();
	NewscasterSideStories.Reset();
	bNewscasterStoryActive = false;
}

int32 FElysiumNpcNewscaster::FUN_103a0ff0(int32 FirstLine, TArray<FString>& OutLines) const
{
	// `0x103a0ff0`, the debug OVERLAY:
	//
	//     if (!Resolve(m_hDialogScene (+0x6554))) {
	//         EntityText(m_pScriptHost (+0x2e0), line, "not playing VCD", 0, 255,255,255,255);
	//         return line + 1;
	//     }
	//     EntityText(m_pScriptHost, ...);  Printf("Main Stories (%d)", ...);  line += 1;
	//     for (i = 0; i < m_MainCount; ++i)
	//         line = PrintStory(this, line, &main[i], field_0x668c == 0 && field_0x6684 == i);
	//     EntityText(m_pScriptHost, ...);  Printf("Side Stories (%d)", ...);  line += 1;
	//     for (i = 0; i < m_SideCount; ++i)
	//         line = PrintStory(this, line, &side[i], field_0x668c != 0 && field_0x6688 == i);
	//     return line;
	//
	// **The highlight predicates are opposites and that is the whole meaning of `+0x668c`**: a main
	// row is highlighted when `+0x668c == 0` and a side row when `+0x668c != 0`, so the word selects
	// WHICH QUEUE is playing and `+0x6684`/`+0x6688` are the two cursors within them. The C's
	// `cVar5 = '\x01'` short-circuit reads backwards at a glance; it is the ordinary
	// "both terms or nothing" and is written that way here.
	//
	// Retail's headers are reproduced verbatim, including the `(%d)` counts. `thunk_FUN_103a0eb0` is
	// the per-row printer (`0x103a0eb0`, not this family's row); this writes one line per row with
	// the highlight flag so a reader can see which one is current.
	//
	// The overlay colour (255,255,255,255) and the `NDebugOverlay::EntityText` channel are the
	// debug tier's and reach nothing here; the LINE NUMBERING is the body's own arithmetic and is
	// what the suite reads back.
	int32 Line = FirstLine;
	const bool bScenePlaying = World != nullptr
		&& World->Resolve(Dialogue.DialogScene) != nullptr;
	if (!bScenePlaying)
	{
		OutLines.Add(NewscasterNotPlayingText);
		return Line + 1;
	}
	OutLines.Add(FString::Printf(TEXT("Main Stories (%d)"), NewscasterMainStories.Num()));
	++Line;
	for (int32 i = 0; i < NewscasterMainStories.Num(); ++i)
	{
		const bool bCurrent = NewscasterPlayingSide == 0 && NewscasterMainCursor == i;
		OutLines.Add(FString::Printf(TEXT("%s%s"), bCurrent ? TEXT("* ") : TEXT("  "),
			*NewscasterMainStories[i].Name));
		++Line;
	}
	OutLines.Add(FString::Printf(TEXT("Side Stories (%d)"), NewscasterSideStories.Num()));
	++Line;
	for (int32 i = 0; i < NewscasterSideStories.Num(); ++i)
	{
		const bool bCurrent = NewscasterPlayingSide != 0 && NewscasterSideCursor == i;
		OutLines.Add(FString::Printf(TEXT("%s%s"), bCurrent ? TEXT("* ") : TEXT("  "),
			*NewscasterSideStories[i].Name));
		++Line;
	}
	return Line;
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
	// Species gave it for the debug overlay (`0x103a0ff0` highlights a MAIN row when `+0x668c == 0`).
	// Both are retail: the overlay and this body read the same word with opposite senses, and that
	// is a retail inconsistency rather than a port error.
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
