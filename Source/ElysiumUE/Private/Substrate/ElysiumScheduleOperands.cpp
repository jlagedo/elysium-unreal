#include "Substrate/ElysiumScheduleOperands.h"

namespace
{
	// --- The eleven compiled-in tables ------------------------------------------------------------
	//
	// Each is one resolver's `strcmpi` chain, in the resolver's own order. The order is kept even
	// though a chain's order cannot be observed through a successful lookup, because it is what the
	// corpus cross-check diffs against and because a future reader comparing this file with the
	// decompilation should not have to sort first.

	// `0x1030c600`. 12 has no name; `0xd` and `0xe` are the two the oracle had as unnamed until the
	// parser walk read them out of this chain.
	constexpr FElysiumOperandRow GStateRows[] = {
		{ TEXT("NONE"), 0 },
		{ TEXT("IDLE"), 1 },
		{ TEXT("COMBAT"), 2 },
		{ TEXT("ALERT"), 3 },
		{ TEXT("SCRIPT"), 4 },
		{ TEXT("PLAYDEAD"), 5 },
		{ TEXT("PRONE"), 6 },
		{ TEXT("DEAD"), 7 },
		{ TEXT("FLEEING"), 8 },
		{ TEXT("RETREATING"), 9 },
		{ TEXT("COWERING"), 10 },
		{ TEXT("HUNTING"), 11 },
		{ TEXT("OBLIVIOUS"), 13 },
		{ TEXT("CRIMINAL_SUSPICION"), 14 },
	};

	// `0x1030c800`. These are mask bits, not ordinals, and the four `CUSTOM` names occupy the top
	// four. `CUSTOM1` is `0x80000000`; see the header on why it is spelled negative.
	constexpr FElysiumOperandRow GMemoryRows[] = {
		{ TEXT("PROVOKED"), 0x1 },
		{ TEXT("INCOVER"), 0x2 },
		{ TEXT("SUSPICIOUS"), 0x4 },
		{ TEXT("PATH_FAILED"), 0x20 },
		{ TEXT("FLINCHED"), 0x40 },
		{ TEXT("TOURGUIDE"), 0x100 },
		{ TEXT("LOCKED_HINT"), 0x400 },
		{ TEXT("TURNING"), 0x2000 },
		{ TEXT("TURNHACK"), 0x4000 },
		{ TEXT("HAD_ENEMY"), 0x8000 },
		{ TEXT("HAD_PLAYER"), 0x10000 },
		{ TEXT("HAD_LOS"), 0x20000 },
		{ TEXT("INVESTIGATING"), 0x08000000 },
		{ TEXT("CUSTOM4"), 0x10000000 },
		{ TEXT("CUSTOM3"), 0x20000000 },
		{ TEXT("CUSTOM2"), 0x40000000 },
		{ TEXT("CUSTOM1"), static_cast<int32>(0x80000000u) },
	};

	// `0x1030ca70`.
	constexpr FElysiumOperandRow GPathRows[] = {
		{ TEXT("TRAVEL"), 0 },
		{ TEXT("LOS"), 1 },
		{ TEXT("COVER"), 2 },
	};

	// `0x1030cb00`.
	constexpr FElysiumOperandRow GGoalRows[] = {
		{ TEXT("ENEMY"), 0 },
		{ TEXT("TARGET"), 1 },
		{ TEXT("ENEMY_LKP"), 2 },
		{ TEXT("TARGET_LKP"), 3 },
		{ TEXT("SAVED_POSITION"), 4 },
	};

	// `0x102d3f50`, searched as substrings rather than compared. Kept as a table so the corpus
	// cross-check has rows to diff; `ResolveHintFlags` is what actually reads it.
	constexpr FElysiumOperandRow GHintFlagRows[] = {
		{ TEXT("none"), 0 },
		{ TEXT("visible"), 1 },
		{ TEXT("nearest"), 2 },
		{ TEXT("random"), 4 },
	};

	// `0x1030f5f0`.
	constexpr FElysiumOperandRow GExpressionRows[] = {
		{ TEXT("FLINCH"), 0 },
		{ TEXT("KNOCKBACK"), 1 },
	};

	// `0x1030d480`.
	constexpr FElysiumOperandRow GStoRows[] = {
		{ TEXT("DEFAULT"), 0 },
		{ TEXT("SHOOT_AT_HINT"), 1 },
	};

	// `0x1030d4f0`. These are exactly the sentinels slot 418 `ResolveTaskDistance` turns into
	// distances at run time: the name table and the run-time enum are one vocabulary.
	constexpr FElysiumOperandRow GDistRows[] = {
		{ TEXT("ACCUM"), -1000000 },
		{ TEXT("TZIMISCE_CLAW"), -1000001 },
		{ TEXT("DIALOG"), -1000002 },
		{ TEXT("COMBATMOVE"), -1000003 },
		{ TEXT("MINGXIAO_IDEAL_RANGE"), -1000004 },
		{ TEXT("FOLLOWER_DISTANCE_BACKAWAY"), -1000005 },
		{ TEXT("FOLLOWER_DISTANCE_WALKTO"), -1000006 },
		{ TEXT("FOLLOWER_DISTANCE_RUNTO"), -1000007 },
		{ TEXT("FOLLOWER_DISTANCE_OVERLAP"), -1000008 },
	};

	// `0x1030d650`.
	constexpr FElysiumOperandRow GMxtPhaseRows[] = {
		{ TEXT("TENTACLE"), 0 },
		{ TEXT("TENTACLE_TO_GRUB"), 1 },
		{ TEXT("GRUB"), 2 },
		{ TEXT("GRUB_TO_PROXY"), 3 },
	};

	// `0x1030d710`.
	constexpr FElysiumOperandRow GToModeRows[] = {
		{ TEXT("NONE"), 0 },
		{ TEXT("PATHING"), 1 },
		{ TEXT("GRABBING"), 2 },
		{ TEXT("CARRYING"), 3 },
		{ TEXT("THROWING"), 4 },
	};

	struct FPrefixSpelling
	{
		const TCHAR* Name;
		EElysiumOperandPrefix Prefix;
		const TCHAR* Resolver;
	};

	// The spellings as the texts author them. The compare is case-insensitive, so `SOUND:` and
	// `Sound:` are the same prefix -- retail's own texts spell four of these in upper case and the
	// rest in mixed case, and nothing depends on which.
	constexpr FPrefixSpelling GPrefixes[] = {
		{ TEXT("Activity"),   EElysiumOperandPrefix::Activity,   TEXT("0x1025d760") },
		{ TEXT("Task"),       EElysiumOperandPrefix::Task,       TEXT("0x10316fd0") },
		{ TEXT("Schedule"),   EElysiumOperandPrefix::Schedule,   TEXT("0x102cadb0") },
		{ TEXT("State"),      EElysiumOperandPrefix::State,      TEXT("0x1030c600") },
		{ TEXT("Memory"),     EElysiumOperandPrefix::Memory,     TEXT("0x1030c800") },
		{ TEXT("Path"),       EElysiumOperandPrefix::Path,       TEXT("0x1030ca70") },
		{ TEXT("Goal"),       EElysiumOperandPrefix::Goal,       TEXT("0x1030cb00") },
		{ TEXT("HintFlags"),  EElysiumOperandPrefix::HintFlags,  TEXT("0x102d3f50") },
		{ TEXT("NPCFlag"),    EElysiumOperandPrefix::NpcFlag,    TEXT("0x1030cbd0") },
		{ TEXT("MiscFlag"),   EElysiumOperandPrefix::MiscFlag,   TEXT("0x1030d390") },
		{ TEXT("Model"),      EElysiumOperandPrefix::Model,      TEXT("0x1030d3d0") },
		{ TEXT("SOUND"),      EElysiumOperandPrefix::Sound,      TEXT("0x1030d400") },
		{ TEXT("EXPRESSION"), EElysiumOperandPrefix::Expression, TEXT("0x1030f5f0") },
		{ TEXT("STO"),        EElysiumOperandPrefix::Sto,        TEXT("0x1030d480") },
		{ TEXT("DIST"),       EElysiumOperandPrefix::Dist,       TEXT("0x1030d4f0") },
		{ TEXT("MXTPHASE"),   EElysiumOperandPrefix::MxtPhase,   TEXT("0x1030d650") },
		{ TEXT("TOMODE"),     EElysiumOperandPrefix::ToMode,     TEXT("0x1030d710") },
	};
}

EElysiumOperandPrefix ElysiumScheduleOperands::PrefixFromName(const FString& Token)
{
	for (const FPrefixSpelling& Row : GPrefixes)
	{
		if (Token.Equals(Row.Name, ESearchCase::IgnoreCase))
		{
			return Row.Prefix;
		}
	}
	return EElysiumOperandPrefix::None;
}

const TCHAR* ElysiumScheduleOperands::PrefixName(EElysiumOperandPrefix Prefix)
{
	for (const FPrefixSpelling& Row : GPrefixes)
	{
		if (Row.Prefix == Prefix)
		{
			return Row.Name;
		}
	}
	return TEXT("?");
}

bool ElysiumScheduleOperands::StoresRawWord(EElysiumOperandPrefix Prefix)
{
	return Prefix == EElysiumOperandPrefix::NpcFlag
		|| Prefix == EElysiumOperandPrefix::MiscFlag
		|| Prefix == EElysiumOperandPrefix::Model;
}

const TCHAR* ElysiumScheduleOperands::ResolverAddress(EElysiumOperandPrefix Prefix)
{
	for (const FPrefixSpelling& Row : GPrefixes)
	{
		if (Row.Prefix == Prefix)
		{
			return Row.Resolver;
		}
	}
	return TEXT("");
}

TConstArrayView<FElysiumOperandRow> ElysiumScheduleOperands::Table(EElysiumOperandPrefix Prefix)
{
	switch (Prefix)
	{
	case EElysiumOperandPrefix::State:      return GStateRows;
	case EElysiumOperandPrefix::Memory:     return GMemoryRows;
	case EElysiumOperandPrefix::Path:       return GPathRows;
	case EElysiumOperandPrefix::Goal:       return GGoalRows;
	case EElysiumOperandPrefix::HintFlags:  return GHintFlagRows;
	case EElysiumOperandPrefix::Expression: return GExpressionRows;
	case EElysiumOperandPrefix::Sto:        return GStoRows;
	case EElysiumOperandPrefix::Dist:       return GDistRows;
	case EElysiumOperandPrefix::MxtPhase:   return GMxtPhaseRows;
	case EElysiumOperandPrefix::ToMode:     return GToModeRows;
	default:                                return TConstArrayView<FElysiumOperandRow>();
	}
}

bool ElysiumScheduleOperands::ResolveTable(
	EElysiumOperandPrefix Prefix, const FString& Spelling, int32& OutValue)
{
	for (const FElysiumOperandRow& Row : Table(Prefix))
	{
		if (Spelling.Equals(Row.Name, ESearchCase::IgnoreCase))
		{
			OutValue = Row.Value;
			return true;
		}
	}
	return false;
}

int32 ElysiumScheduleOperands::ResolveHintFlags(const FString& Spelling)
{
	const FString Lowered = Spelling.ToLower();

	int32 Flags = 0;
	for (const FElysiumOperandRow& Row : GHintFlagRows)
	{
		if (Lowered.Contains(Row.Name, ESearchCase::CaseSensitive))
		{
			Flags |= Row.Value;
		}
	}

	// Retail's own arm: a token naming both warns and keeps `nearest`. The two are alternative
	// pickers over the same hint list, so the OR would be meaningless rather than merely odd.
	constexpr int32 Nearest = 2;
	constexpr int32 Random = 4;
	if ((Flags & Nearest) != 0 && (Flags & Random) != 0)
	{
		Flags &= ~Random;
	}
	return Flags;
}

int32 ElysiumScheduleOperands::ResolveScheduleFlag(const FString& Spelling, bool& bOutKnown)
{
	if (Spelling.Equals(TEXT("DELAY_INTERRUPTS"), ESearchCase::IgnoreCase))
	{
		bOutKnown = true;
		return 1;
	}
	if (Spelling.Equals(TEXT("NONE"), ESearchCase::IgnoreCase))
	{
		bOutKnown = true;
		return 0;
	}
	bOutKnown = false;
	return 0;
}

bool ElysiumScheduleOperands::BooleanWord(const FString& Token, float& OutValue)
{
	if (Token.Equals(TEXT("TRUE"), ESearchCase::IgnoreCase)
		|| Token.Equals(TEXT("ON"), ESearchCase::IgnoreCase))
	{
		OutValue = 1.0f;
		return true;
	}
	if (Token.Equals(TEXT("FALSE"), ESearchCase::IgnoreCase)
		|| Token.Equals(TEXT("OFF"), ESearchCase::IgnoreCase))
	{
		OutValue = 0.0f;
		return true;
	}
	return false;
}

float ElysiumScheduleOperands::Atof(const FString& Token)
{
	// `FCString::Atof` is `atof`, and `atof` is what the parser calls. Both read as far as they can
	// and answer 0 for a token that starts with no number -- which is the behaviour that lets a
	// bare `TASK_FOO SOMETHING_UNSPELLED` load as operand 0 rather than failing the text. The
	// shipped corpus never relies on it: all 691 texts parse with no non-numeric bare operand.
	return FCString::Atof(*Token);
}
