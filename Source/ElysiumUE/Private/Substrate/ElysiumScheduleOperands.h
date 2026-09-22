// The schedule parser's operand vocabulary: seventeen prefixes, four boolean words, and a number.
//
// `docs/vtmb/npc-ai/schedule-kernel.md` § "The schedule-text parser `0x1030d850`, walked" walks the
// chain at `0x1030d9ce`..`0x1030e6b7` in the order the body tests it, and this file is that chain.
// Eleven of the prefixes resolve against a fixed table compiled into the resolver body; those
// tables live here, because they are the interpreter's own vocabulary -- they were read out of the
// resolvers' `strcmpi` chains, not out of any shipped content. `FElysiumScheduleCorpus::VerifyNumbers`
// checks every row here against the `vocabulary.json` the export writes from the image, so a typo
// in this file is a load-time Error naming the pair that disagreed rather than a wrong program.
//
// The six that are not tables resolve elsewhere:
//
//   `Activity:` / `Model:` / `SOUND:`  -> `FElysiumSymbolRegistry`, which interns (see that header)
//   `NPCFlag:`                         -> `FElysiumNpcFlags::ParseName`, the `0x1030cbd0` port
//   `MiscFlag:`                        -> `ElysiumMiscFlags::ParseScheduleIndex`, the `0x1030d390` port
//   `Task:` / `Schedule:`              -> the class's id spaces, in `ElysiumScheduleText.cpp`
//
// **The data word.** A task record is two 32-bit words, id then data. A resolver's integer answer
// is stored as that number CONVERTED to float (`FILD`, signed) -- except `NPCFlag:`, `MiscFlag:`
// and `Model:`, whose 32-bit word is stored unconverted (`0x1030e06d`, `0x1030e128`, `0x1030e1e1`).
// That is why `Memory:CUSTOM1` (`0x80000000`) is spelled here as a negative `int32`: the signed
// conversion is what retail performs, and a consumer that wants the mask back casts through
// `int32`, which round-trips exactly because `2^31` is representable in a float.

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"

/** The seventeen prefixes, in the order `0x1030d850` tests them. */
enum class EElysiumOperandPrefix : uint8
{
	None = 0,
	Activity,     // 0x1025d760, over DAT_1090fbe0
	Task,         // 0x10316fd0, global task space, then class-local translation
	Schedule,     // 0x102cadb0, global schedule space, then class-local translation
	State,        // 0x1030c600
	Memory,       // 0x1030c800
	Path,         // 0x1030ca70
	Goal,         // 0x1030cb00
	HintFlags,    // 0x102d3f50 -- a SUBSTRING search, not a compare
	NpcFlag,      // 0x1030cbd0, raw word; the sign bit selects the second flag word
	MiscFlag,     // 0x1030d390, raw INDEX 0-21; an unknown name silently reads 0
	Model,        // 0x1030d3d0, raw symbol id
	Sound,        // 0x1030d400
	Expression,   // 0x1030f5f0
	Sto,          // 0x1030d480
	Dist,         // 0x1030d4f0, the negative sentinels slot 418 resolves at run time
	MxtPhase,     // 0x1030d650
	ToMode,       // 0x1030d710
};

/** One row of a resolver's compiled-in table. */
struct FElysiumOperandRow
{
	const TCHAR* Name = nullptr;
	int32 Value = 0;
};

namespace ElysiumScheduleOperands
{
	/** The prefix a token names, or `None` if it names no prefix. Case-insensitive. */
	EElysiumOperandPrefix PrefixFromName(const FString& Token);

	/** The canonical spelling, for a diagnostic and for the corpus cross-check. */
	const TCHAR* PrefixName(EElysiumOperandPrefix Prefix);

	/** The three prefixes whose word is stored unconverted. */
	bool StoresRawWord(EElysiumOperandPrefix Prefix);

	/** The resolver's address, so a published operand can name what answered it. */
	const TCHAR* ResolverAddress(EElysiumOperandPrefix Prefix);

	/** The compiled-in table for a prefix, or an empty view for the six that have none. */
	TConstArrayView<FElysiumOperandRow> Table(EElysiumOperandPrefix Prefix);

	/** A plain `strcmpi` chain over `Table(Prefix)`. False when the prefix has no table or the
	 *  name is not in it -- which for the eleven table prefixes is one of retail's failure rows. */
	bool ResolveTable(EElysiumOperandPrefix Prefix, const FString& Spelling, int32& OutValue);

	/** `HintFlags: 0x102d3f50`, which is NOT a compare: it lowercases the token and searches it for
	 *  each of `none` / `visible` / `nearest` / `random` as a SUBSTRING, ORing what it finds. A
	 *  token naming both `nearest` and `random` warns and reads as `nearest`. Never fails: an
	 *  unrecognised token reads 0. */
	int32 ResolveHintFlags(const FString& Spelling);

	/** `Flags 0x1030d7e0`: `DELAY_INTERRUPTS` -> 1 and `NONE` -> 0, silently; anything else is an
	 *  Error and also 0. `bOutKnown` distinguishes the two zeros -- the parser then reports
	 *  "Unknown schedule flag" for every 0, so an authored `Flags NONE` prints it harmlessly. */
	int32 ResolveScheduleFlag(const FString& Spelling, bool& bOutKnown);

	/** The four boolean words. True when `Token` is one of them, with `1.0` or `0.0` in `OutValue`. */
	bool BooleanWord(const FString& Token, float& OutValue);

	/** C's `atof`, which is what retail calls for any operand that is not a prefix or a boolean:
	 *  leading separators are skipped, an optional sign and digits are read, and everything from
	 *  the first character that cannot continue a number is ignored. A token that begins with no
	 *  number at all reads `0.0` and does NOT fail the text. */
	float Atof(const FString& Token);
}
