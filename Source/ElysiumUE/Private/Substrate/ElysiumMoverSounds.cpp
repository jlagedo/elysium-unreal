// The `soundgroup` resolver. The retail chain it reproduces — the five SndScheme tables, the
// "Usable\<Category>" directory template, the directory walk, the verbatim case-insensitive lookup
// and the category-root miss arm — is written out with its addresses in ElysiumMoverSounds.h.

#include "Substrate/ElysiumMoverSounds.h"

#include "ElysiumContentPaths.h"
#include "ElysiumKeyValues.h"
#include "Substrate/ElysiumMover.h"
#include "Substrate/ElysiumVdataLoad.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"

namespace
{
	using ElysiumKeyValues::FKvNode;

	// The vdata vocabulary file behind each registered category (FUN_101f66c0 @0x101f66c0 pairs the
	// table name with its directory token; FUN_101f5210 @0x101f5210 turns the table name into
	// `vdata/system/<table>.txt`). Note `SndScheme_Computer` is singular while its directory token
	// is `Computers` — the two are independent strings in retail and both are reproduced verbatim.
	const TCHAR* VocabularyFileFor(const FString& Category)
	{
		if (Category == ElysiumSoundGroups::Openable)  { return TEXT("system/sndscheme_openable.txt"); }
		if (Category == ElysiumSoundGroups::Switches)  { return TEXT("system/sndscheme_switch.txt"); }
		if (Category == ElysiumSoundGroups::Computers) { return TEXT("system/sndscheme_computer.txt"); }
		return nullptr;
	}

	// `SoundSchemeTables/SoundScheme/SoundList/Sound/Name`, in file order — the ordinal slots
	// FUN_101f5390 @0x101f5390 allocates. Order is kept because retail addresses a cue by its index
	// in this list, so a reader that ever needs the ordinal has it.
	TArray<FName> ParseVocabulary(const FString& Category)
	{
		TArray<FName> Out;
		const TCHAR* Rel = VocabularyFileFor(Category);
		if (!Rel)
		{
			return Out;
		}
		TSharedPtr<FKvNode> Root;
		FString Error;
		if (!ElysiumVdata::ReadVdata(Rel, Root, Error))
		{
			UE_LOG(LogElysiumMover, Log,
				TEXT("soundgroup: no `%s` vocabulary (%s) — every `%s` cue stays silent"),
				*Category, *Error, *Category);
			return Out;
		}
		const FKvNode* Tables = ElysiumVdata::RootBlock(Root, TEXT("SoundSchemeTables"), Rel, Error);
		const FKvNode* Scheme = Tables ? Tables->Child(TEXT("SoundScheme")) : nullptr;
		const FKvNode* List   = Scheme ? Scheme->Child(TEXT("SoundList")) : nullptr;
		if (!List)
		{
			UE_LOG(LogElysiumMover, Warning,
				TEXT("soundgroup: `%s` vocabulary has no SoundSchemeTables/SoundScheme/SoundList"),
				*Category);
			return Out;
		}
		for (const TPair<FString, TSharedPtr<FKvNode>>& Kid : List->Kids)
		{
			if (Kid.Key != TEXT("sound") || !Kid.Value.IsValid())
			{
				continue;
			}
			const FString Name = ElysiumVdata::Trim(Kid.Value->Str(TEXT("Name"), FString())).ToLower();
			if (!Name.IsEmpty())
			{
				Out.AddUnique(FName(*Name));
			}
		}
		return Out;
	}

	// `usable/<category>` — the "Usable\<Category>" of FUN_101f41b0 @0x101f41b0, where "Usable" is
	// the `Name` key all three vocabulary files author. Corpus-relative, under SoundDir().
	FString CategoryDir(const FString& Category)
	{
		return FString(TEXT("usable/")) + Category;
	}
}

namespace ElysiumSoundGroups
{
	const TArray<FString>& Categories()
	{
		static const TArray<FString> All = { Openable, Switches, Computers };
		return All;
	}

	const TArray<FName>& Subkeys(const FString& Category)
	{
		static TMap<FString, TArray<FName>> Cache;
		if (const TArray<FName>* Hit = Cache.Find(Category))
		{
			return *Hit;
		}
		return Cache.Add(Category, ParseVocabulary(Category));
	}

	const TMap<FName, FString>& Resolve(const FString& Category, const FString& InGroup)
	{
		static TMap<FString, TMap<FName, FString>> Cache;
		const FString Cat = Category.ToLower();
		// Verbatim but for the case fold: retail's FUN_101f39d0 @0x101f39d0 compares the authored
		// token with `__strcmpi` and tries no other spelling, so neither does this. The corpus is
		// deployed all-lower-case, which is the only reason the fold is needed at all.
		const FString Group = InGroup.TrimStartAndEnd().Replace(TEXT("\\"), TEXT("/")).ToLower();
		const FString Key = Cat + TEXT("/") + Group;
		if (const TMap<FName, FString>* Hit = Cache.Find(Key))
		{
			return *Hit;
		}

		TMap<FName, FString> Resolved;
		const TArray<FName>& Vocabulary = Subkeys(Cat);
		if (!Vocabulary.IsEmpty() && !Group.IsEmpty())
		{
			// The group's own directory if it is shipped; otherwise the category root, which is what
			// FUN_101f42a0 @0x101f42a0 returns on a lookup miss (the table's own base index, i.e. the
			// default `open.wav`/`close.wav`/... sitting directly under `usable/<category>/`).
			FString Dir = CategoryDir(Cat) / Group;
			const bool bGroupShipped =
				IFileManager::Get().DirectoryExists(*(FElysiumContentPaths::SoundDir() / Dir));
			if (!bGroupShipped)
			{
				Dir = CategoryDir(Cat);
			}

			TArray<FString> Missing;
			for (const FName& Sub : Vocabulary)
			{
				const FString Rel = Dir / Sub.ToString() + TEXT(".wav");
				if (FPaths::FileExists(FElysiumContentPaths::SoundDir() / Rel))
				{
					Resolved.Add(Sub, Rel);
				}
				else
				{
					Missing.Add(Sub.ToString());
				}
			}

			// One diagnostic per (category, group), emitted once because the cache is lazy. A group
			// that ships no file for a subkey is silent on that cue, exactly as retail's unfilled
			// slot is (`switches/elevator_button` has `on.wav` and no `off.wav`).
			if (!bGroupShipped)
			{
				UE_LOG(LogElysiumMover, Log,
					TEXT("soundgroup '%s/%s' is not shipped; falling back to the category root %s (%d cue(s))"),
					*Cat, *Group, *CategoryDir(Cat), Resolved.Num());
			}
			if (!Missing.IsEmpty())
			{
				UE_LOG(LogElysiumMover, Log,
					TEXT("soundgroup '%s/%s' ships no %s — those cues are silent"),
					*Cat, *Group, *FString::Join(Missing, TEXT(", ")));
			}
		}
		return Cache.Add(Key, MoveTemp(Resolved));
	}

	TArray<FString> EnumerateGroups(const FString& Category)
	{
		TArray<FString> Groups;
		IFileManager::Get().IterateDirectory(
			*(FElysiumContentPaths::SoundDir() / CategoryDir(Category.ToLower())),
			[&Groups](const TCHAR* Path, bool bIsDirectory)
			{
				if (bIsDirectory)
				{
					Groups.Add(FPaths::GetCleanFilename(FString(Path)));
				}
				return true;
			});
		Groups.Sort();
		return Groups;
	}
}
