#pragma once

#include "CoreMinimal.h"
#include "ElysiumKeyValues.h"

// The file loader of vampire.dll's second KeyValues class (the 0x1C-byte node; `ElysiumKeyValues.h`
// header note), `docs/specs/layers/L0-entity/walks/L0-r004.md`:
//
//   * `0x102482b0` wraps the tokenizer `0x10247280` over a cursor and its own shared buffer
//     `0x10754fa8` (the `VKeyValues` wrapper `0x101f2f30` uses `0x1073AA80`);
//   * `0x10248510` parses a block: `}` or an empty key closes, a child is created BEFORE its value is
//     read, `{` recurses, a quoted value is type 0, an unquoted one is typed by `strtol` against
//     `strtod` -- 2 float when strtod consumed more, 1 int when strtol consumed anything, else 0;
//   * `0x102482f0`, the whole-file text cache: CRC-32 of the file name (`0x1023f040/0c0/060`, table
//     `0x10496f58`), a linear table, a 1 MiB arena (`0x105c530c`); on a miss it opens the file itself
//     with pathID 0. `hooks.tsv:325` lists it as an L0 -> L4 hook; its body touches only the file system
//     and its own statics, so it is built here (the run's confirmer flagged the classification);
//   * `0x102480f0` loads a file into a root chain: with `cache` and `.txt` in the name the cached text
//     is used (`strstr`, `0x10546e4c`), else the file is opened `rb` (`0x105596cc`), sized, read and
//     NUL-terminated; then EVERY top-level token is a root -- the first renames the caller's node
//     (`0x10247cf0`), the rest are new nodes chained through `+0x10` -- and the token after it opens a
//     block when its FIRST BYTE is `{` (`CMP byte [EAX],0x7B`), otherwise it is dropped. Returns 0 only
//     when every open failed; 1 otherwise, empty text included.
//
// Callers (16): the sound-scheme parser `0x1022a930` (`CSoundScheme::Precache 0x1022a4b0`), the
// soundscapes, the signs, the keypads, the terminals (`CPropHacking::LoadFromFile 0x1021cba0`), the
// radios, the quest journal, the sound-volume table, the disposition table, the interesting places and
// the stealth-kill rules; all pass pathID 0 and cache 1.

struct IElysiumRetailSiteSink;

namespace ElysiumKeyValuesLoader
{
	using ElysiumKeyValues::FKvNode;
	using ElysiumKeyValues::FKvReader;

	// An open file of the engine file system (`DAT_1070b238`, written once by `CServerGameDLL::vfunc1`
	// `0x1011a0c0`): slots `+0x00 Open(name, mode, pathID)`, `+0x04 Close(h)`, `+0x08 Read(buf, n, h)`,
	// `+0x18 Size(h)`. Which engine.dll method each slot is stays UNRECOVERED (walk open question 8);
	// the shape is what the loader and the cache call.
	struct FKvFile;
	using FKvFileHandle = FKvFile*;

	struct IKvFileSystem
	{
		virtual ~IKvFileSystem() = default;
		virtual FKvFileHandle Open(const FString& Name, const ANSICHAR* Mode, int32 PathID) = 0;
		virtual void Close(FKvFileHandle Handle) = 0;
		virtual int32 Read(uint8* Out, int32 Count, FKvFileHandle Handle) = 0;
		virtual int32 Size(FKvFileHandle Handle) = 0;
	};

	// The port's engine file system: a retail-relative name resolved to a path of the deployed corpus by
	// `Resolve` (the search-path lookup the engine did), opened through the platform file. `PathID` has
	// one search path here, named modernization: every retail caller passes 0 (all paths).
	struct FKvDiskFileSystem final : public IKvFileSystem
	{
		explicit FKvDiskFileSystem(TFunction<FString(const FString& Name)> InResolve) : Resolve(MoveTemp(InResolve)) {}

		virtual FKvFileHandle Open(const FString& Name, const ANSICHAR* Mode, int32 PathID) override;
		virtual void Close(FKvFileHandle Handle) override;
		virtual int32 Read(uint8* Out, int32 Count, FKvFileHandle Handle) override;
		virtual int32 Size(FKvFileHandle Handle) override;

		TFunction<FString(const FString& Name)> Resolve;
	};

	// The bytes of a file plus the NUL (`0x102480f0` arm 4, `buf[n] = 0`), widened byte-for-byte to
	// TCHARs so the tokenizer classifies each as retail does (`ElysiumKeyValues.h` representation note).
	FString WidenBytes(const TArray<uint8>& Bytes);

	// `0x102482f0` `(name, fs) -> text | NULL`. The CRC-32 of `name` is searched in the table; a hit
	// returns the interned text (whatever file it was read from: the key is the hash alone). A miss
	// opens `name` `rb` with pathID 0; `n + 1 >= remaining` (the 1 MiB arena, `0x105c530c`) or a
	// failed open returns NULL; otherwise the text is read into the arena, NUL-terminated, recorded
	// under the hash, and the arena shrinks by `n + 1`. Process-global, like the statics it stands for.
	struct FKvFileTextCache
	{
		// A cached text: the pointer retail hands back, valid for the process.
		struct FEntry
		{
			uint32 Hash = 0;
			int32 Size = 0;   // the file's byte count (`n`)
			TSharedPtr<FString> Text;
		};

		const FEntry* Lookup(const FString& Name, IKvFileSystem& Fs, IElysiumRetailSiteSink* Sites);

		// `0x1023f040` init `0xffffffff`, `0x1023f0c0` the CRC-32 table step (`0x10496f58`), `0x1023f060`
		// the final complement: IEEE CRC-32 of the name's bytes.
		static uint32 HashName(const FString& Name);

		TArray<FEntry> Entries;              // `DAT_10753768[i]` hash, `DAT_107547a8[i]` pointer, count `DAT_10856fa8`
		int32 Remaining = 0x100000;          // `DAT_105c530c`
	};
	FKvFileTextCache& FileTextCache();

	// `0x102482b0`: `0x10247280` over `R.Pos` into `R.Token` (the buffer `0x10754fa8`), the cursor
	// written back; returns the buffer, never NULL (`""` at end of text). `R` is this class's reader --
	// a distinct instance from any `VKeyValues` reader, as the buffers are distinct.
	const FString& ReadToken(FKvReader& R, uint8* Quoted);

	// `0x10248510` `(this, cursor, fs)`: parse `Node`'s block from `R` until `}` or an empty key
	// (`fs` is forwarded to the recursion and never read).
	void ParseBlock(FKvNode& Node, FKvReader& R);

	// `0x102480f0` arms 1-5: the text source. `Cache` is the fifth argument (`char`), `PathID` the
	// fourth.
	struct FKvLoadArgs
	{
		FString FileName;
		IKvFileSystem* Fs = nullptr;
		int32 PathID = 0;
		uint8 Cache = 1;
		IElysiumRetailSiteSink* Sites = nullptr;
	};

	// `0x102480f0` `(this, filename, fs, pathID, cache)`. `Target` is ECX: the caller's node, renamed by
	// the first root (its children and chain dropped); NULL makes every root a new node. `OutRoots`
	// receives the root chain in order (retail links them through the first root's `+0x10`): `Target`
	// first when the text had a token. Returns retail's AL: 0 when no text could be opened, else 1.
	bool LoadFile(const TSharedPtr<FKvNode>& Target, const FKvLoadArgs& Args, TArray<TSharedPtr<FKvNode>>& OutRoots);

	// `0x102480f0` arm 6 over text already in hand (the shape of `FUN_10248430`, the loader's buffer
	// twin with no corpus caller): the root loop only.
	void ParseRoots(const TSharedPtr<FKvNode>& Target, FKvReader& R, TArray<TSharedPtr<FKvNode>>& OutRoots);
}
