#pragma once

// Story 5 step 4: the file-scope locals of `ElysiumNpcPrecache10.cpp` that its staying bodies share with
// bodies moved to their species classes. Qualified at every use (`NpcKernelPrecache10Shared::`) because a unity
// build concatenates translation units and an anonymous namespace is not file-local there.

#include "Substrate/ElysiumNpc.h"
#include "ElysiumEntityDefs.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"

namespace NpcKernelPrecache10Shared
{
	// `DAT_10598a30` and `DAT_10548ed4`, the two extension patterns `0x101d0f10` globs with. Their
	// bytes are pinned by `FUN_101b1120`, which is SDK-2013's `CSoundEmitterSystem::EmitSound`
	// instruction for instruction: `Q_stristr(soundname, ".wav") || Q_stristr(soundname, ".mp3") ||
	// soundname[0] == '!'`.
	inline const TCHAR* const GExtWav = TEXT(".wav");
	inline const TCHAR* const GExtMp3 = TEXT(".mp3");
	inline void Precache10Model(FElysiumNpc& Npc, const TCHAR* Name, int32 Preload)
	{
		FElysiumNpcBase::FPrecacheOp Op;
		Op.Channel = FElysiumNpcBase::EPrecacheChannel::Model;
		Op.Name = Name;
		Op.Flag = Preload;
		Npc.IssuePrecache(Op);
	}
	inline void Precache10Sound(FElysiumNpc& Npc, const TCHAR* Name)
	{
		FElysiumNpcBase::FPrecacheOp Op;
		Op.Channel = FElysiumNpcBase::EPrecacheChannel::Sound;
		Op.Name = Name;
		Npc.IssuePrecache(Op);
	}
	inline void Precache10Particle(FElysiumNpc& Npc, const TCHAR* Name, int32 Preload)
	{
		FElysiumNpcBase::FPrecacheOp Op;
		Op.Channel = FElysiumNpcBase::EPrecacheChannel::Particle;
		Op.Name = Name;
		Op.Flag = Preload;
		Npc.IssuePrecache(Op);
	}
	inline void Precache10Other(FElysiumNpcBase& Npc, const FString& Classname)
	{
		FElysiumNpcBase::FPrecacheOp Op;
		Op.Channel = FElysiumNpcBase::EPrecacheChannel::Other;
		Op.Name = Classname;
		Npc.IssuePrecache(Op);
	}
	inline void Precache10SoundTable(FElysiumNpc& Npc, const TCHAR* const* Names, int32 Count)
	{
		// The byte loop, verbatim: `uVar = 0; do { precache(table[uVar/4]); uVar += 4; } while
		// (uVar < N)`. A do/while, so a table walked to zero bytes would still precache its first
		// entry — no body here walks one, and the bound is always a positive multiple of four.
		for (int32 Index = 0; Index < Count; ++Index)
		{
			NpcKernelPrecache10Shared::Precache10Sound(Npc, Names[Index]);
		}
	}
	// `0x10642adc` — precached TWICE in a row by BOTH `CNPC_VManBat` and `CNPC_VSheriffMan`, from
	// the same `.rdata` cell. A retail duplicate, kept.
	inline const TCHAR* const GSheriffTeleportEmitter = TEXT("sheriff_teleport_emitter");
	//
	// `DAT_105399a0` is the one-character string `"0"` — the authored "none" sentinel this runtime
	// already spells `ElysiumNpcLoadout::IsNoneSentinel`, and the thing the two-byte `REPE CMPSB`
	// in both bodies tests an equipment keyfield against.
	inline const TCHAR* const GNoneSentinel = TEXT("0");
}
