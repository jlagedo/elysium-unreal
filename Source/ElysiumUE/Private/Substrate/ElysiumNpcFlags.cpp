#include "Substrate/ElysiumNpcFlags.h"

#include "ElysiumSaveArchive.h"

namespace
{
	struct FElysiumNpcFlagName
	{
		const TCHAR* Name;
		uint32 Bit;
	};

	// `0x1030cbd0`, word one. Transcribed in bit order rather than in the parser's `strcmpi` order:
	// the chain's order is a compiler artefact, the bit values are the data.
	const FElysiumNpcFlagName GWord1Names[] = {
		{ TEXT("D_IS_BUSY"),               0x00000001 },
		{ TEXT("DO_STARTLED"),             0x00000002 },
		{ TEXT("AT_CROSSWALK"),            0x00000004 },
		{ TEXT("PRESERVE_PATH"),           0x00000008 },
		{ TEXT("FINDING_BODY"),            0x00000010 },
		{ TEXT("CARRYING_BODY"),           0x00000020 },
		{ TEXT("NAV_IGNORE_NPC"),          0x00000040 },
		{ TEXT("IN_FLEE_SCHED"),           0x00000080 },
		{ TEXT("INITIAL_FLEE"),            0x00000100 },
		{ TEXT("COWER_PATH"),              0x00000200 },
		{ TEXT("COWERING"),                0x00000400 },
		{ TEXT("DODGING"),                 0x00000800 },
		{ TEXT("MADE_HUNT_PATH"),          0x00001000 },
		{ TEXT("AT_COVER_HINT"),           0x00002000 },
		{ TEXT("ANIM_MOVEMENT"),           0x00004000 },
		{ TEXT("DONE_EXTRAPOLATING"),      0x00008000 },
		{ TEXT("FORCE_RELAXED_ANIMS"),     0x00010000 },
		{ TEXT("SLEEPING"),                0x00020000 },
		{ TEXT("BOTCHED_ATTACK"),          0x00040000 },
		{ TEXT("NO_DIALOG"),               0x00080000 },
		{ TEXT("SKIPPED_SOUND"),           0x00100000 },
		{ TEXT("LOOKED_AT_UNKNOWN"),       0x00200000 },
		{ TEXT("IGNORE_UNKNOWN"),          0x00400000 },
		{ TEXT("ATTACK_UNKNOWN"),          0x00800000 },
		{ TEXT("MADE_INITIAL_RESPONSE"),   0x01000000 },
		{ TEXT("FINISHED_IGNORE_UNKNOWN"), 0x02000000 },
		{ TEXT("DONT_INVESTIGATE"),        0x04000000 },
		{ TEXT("PLAYING_FACE_ANIM"),       0x08000000 },
		{ TEXT("FORCED_OCCLUDE"),          0x10000000 },
		{ TEXT("INTERESTING_INTO"),        0x20000000 },
		{ TEXT("ONE_HIT_KILL"),            0x40000000 },
	};

	// `0x1030cbd0`, word two. The parser returns `0x80000000 | bit` for each of these; the bit is
	// what is carried here (see the header on why the sign bit is not a flag).
	const FElysiumNpcFlagName GWord2Names[] = {
		{ TEXT("SLEEP_BOUNDING_BOX"),       0x00000001 },
		{ TEXT("FINISH_SPECIAL_NAV"),       0x00000002 },
		{ TEXT("SCHEDULE_CHANGED"),         0x00000004 },
		{ TEXT("INTERESTING_LOST"),         0x00000008 },
		{ TEXT("TASKS_FACE_ENEMY"),         0x00000010 },
		{ TEXT("TASKS_FACE_TARGET"),        0x00000020 },
		{ TEXT("IGNORE_SQUAD_SEE_ENEMY"),   0x00000040 },
		{ TEXT("NO_UNKNOWN_ATTACK"),        0x00000080 },
		{ TEXT("COVER_VS_MELEE_MODE"),      0x00000100 },
		{ TEXT("IGNORE_DOOR_FAILURE"),      0x00000200 },
		{ TEXT("MOVE_FACE_ENEMY"),          0x00000400 },
		{ TEXT("DISALLOW_TGT_DISCIPLINE"),  0x00000800 },
		{ TEXT("MADE_OBLIVIOUS"),           0x00001000 },
		{ TEXT("SQUAD_NEW_ENEMY"),          0x00002000 },
		{ TEXT("DONT_FALL_TO_GROUND"),      0x00004000 },
		{ TEXT("DISABLE_BURST_FIRE"),       0x00008000 },
		{ TEXT("D_CALM"),                   0x00010000 },
		{ TEXT("D_INSANE"),                 0x00020000 },
		{ TEXT("D_POSSESSED"),              0x00040000 },
		{ TEXT("D_MILDLY_CRAZY"),           0x00080000 },
		{ TEXT("D_FOLLOW"),                 0x00100000 },
		{ TEXT("D_NIGHTMARE"),              0x00200000 },
		{ TEXT("D_AUTO_FEEDABLE"),          0x00400000 },
		{ TEXT("D_DISCONNECT_SQUAD"),       0x00800000 },
		{ TEXT("D_WPN_HIDDEN"),             0x01000000 },
		{ TEXT("CHOOSE_NEW_SCHEDULE"),      0x02000000 },
		{ TEXT("NO_UNKNOWN_VISION"),        0x04000000 },
		{ TEXT("NOT_FEEDABLE"),             0x08000000 },
		{ TEXT("NO_DIALOG_PERSISTENT"),     0x10000000 },
		{ TEXT("DISAPPEAR"),                0x20000000 },
		{ TEXT("ACTIVITY_COPY_PROP_CLEAN"), 0x40000000 },
	};

	// The two masks `OnScheduleChange` applies inside its `PRESERVE_PATH` guard, retail's own
	// constants. `~0xbbf4b97e` is `0x440b4681`.
	constexpr uint32 GScheduleChangeKeep1 = 0xbbf4b97eu;
	constexpr uint32 GScheduleChangeKeep2 = 0x77fff14fu;
	// The two unconditional tail clears, outside the guard.
	constexpr uint32 GScheduleChangeTailKeep1 = 0xd7ffffffu;
	constexpr uint32 GScheduleChangeTailKeep2 = 0x3fffffffu;
}

void FElysiumNpcFlags::AddOblivious()
{
	Set(EElysiumNpcFlag2::MADE_OBLIVIOUS);
	++ObliviousCount;
}

void FElysiumNpcFlags::RemoveOblivious()
{
	// Retail clamps at zero (`0x1026d160`) rather than trusting the pairing, and so does this: the
	// binary itself has a path that drops the bookkeeping bit without decrementing, so the counter is
	// not provably balanced even in retail.
	ObliviousCount = FMath::Max(0, ObliviousCount - 1);
	Clear(EElysiumNpcFlag2::MADE_OBLIVIOUS);
}

bool FElysiumNpcFlags::OnScheduleChange()
{
	Set(EElysiumNpcFlag2::SCHEDULE_CHANGED);

	bool bReleasedOblivious = false;
	if (!Has(EElysiumNpcFlag::PRESERVE_PATH))
	{
		Word1 &= GScheduleChangeKeep1;
		Word2 &= GScheduleChangeKeep2;
		if (Has(EElysiumNpcFlag2::MADE_OBLIVIOUS))
		{
			RemoveOblivious();
			bReleasedOblivious = true;
		}
	}
	Word1 &= GScheduleChangeTailKeep1;
	Word2 &= GScheduleChangeTailKeep2;
	return bReleasedOblivious;
}

void FElysiumNpcFlags::Serialize(FElysiumSaveArchive& Ar)
{
	Ar << Word1;
	Ar << Word2;
	Ar << ObliviousCount;
}

FString FElysiumNpcFlags::Describe() const
{
	TArray<FString> Parts;
	for (const FElysiumNpcFlagName& Row : GWord1Names)
	{
		if ((Word1 & Row.Bit) != 0)
		{
			Parts.Add(Row.Name);
		}
	}
	for (const FElysiumNpcFlagName& Row : GWord2Names)
	{
		if ((Word2 & Row.Bit) != 0)
		{
			Parts.Add(Row.Name);
		}
	}
	if (ObliviousCount > 0)
	{
		Parts.Add(FString::Printf(TEXT("oblivious=%d"), ObliviousCount));
	}
	return Parts.IsEmpty() ? TEXT("-") : FString::Join(Parts, TEXT("|"));
}

bool FElysiumNpcFlags::ParseName(const FString& Name, EElysiumNpcFlag& OutWord1,
	EElysiumNpcFlag2& OutWord2)
{
	OutWord1 = EElysiumNpcFlag::None;
	OutWord2 = EElysiumNpcFlag2::None;
	// Retail's operand is spelled `NPCFlag:<name>` and the compiler hands the parser what follows the
	// colon; accepting the whole operand here keeps a caller from having to split it.
	FString Bare = Name;
	int32 Colon = INDEX_NONE;
	if (Bare.FindChar(TEXT(':'), Colon))
	{
		Bare = Bare.Mid(Colon + 1);
	}
	Bare.TrimStartAndEndInline();
	if (Bare.IsEmpty())
	{
		return false;
	}
	for (const FElysiumNpcFlagName& Row : GWord1Names)
	{
		if (Bare.Equals(Row.Name, ESearchCase::IgnoreCase))
		{
			OutWord1 = static_cast<EElysiumNpcFlag>(Row.Bit);
			return true;
		}
	}
	for (const FElysiumNpcFlagName& Row : GWord2Names)
	{
		if (Bare.Equals(Row.Name, ESearchCase::IgnoreCase))
		{
			OutWord2 = static_cast<EElysiumNpcFlag2>(Row.Bit);
			return true;
		}
	}
	return false;
}

const TCHAR* FElysiumNpcFlags::LexToString(EElysiumNpcFlag Flag)
{
	const uint32 Bit = static_cast<uint32>(Flag);
	for (const FElysiumNpcFlagName& Row : GWord1Names)
	{
		if (Row.Bit == Bit)
		{
			return Row.Name;
		}
	}
	return TEXT("NPCFlag:?");
}
