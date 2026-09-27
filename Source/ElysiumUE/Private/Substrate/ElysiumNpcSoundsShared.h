#pragma once

// Story 5 step 4: the file-scope locals of `ElysiumNpcSounds.cpp` that its staying bodies share with
// bodies moved to their species classes. Qualified at every use (`NpcKernelSoundsShared::`) because a unity
// build concatenates translation units and an anonymous namespace is not file-local there.

#include "Substrate/ElysiumNpc.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumRng.h"
#include "ElysiumSessionSubsystem.h"
#include "ElysiumStub.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumSchedule.h"

namespace NpcKernelSoundsShared
{
	// `gpGlobals->curtime` (`DAT_1070b228 + 0xc`).
	inline double SoundsCurTime(const FElysiumNpcBase& Npc)
	{
		return Npc.World != nullptr ? Npc.World->NowSeconds() : 0.0;
	}
	// Retail's `NPC_STATE` ordinals, as the name table at `0x1027e660` orders them:
	// None(0) Idle(1) Combat(2) Alert(3) Script(4) … The port's `EElysiumNpcState` is a different
	// enum with a different order, so a body that tests a retail ordinal converts.
	inline constexpr int32 GSoundsNpcStateIdle = 1;
	inline constexpr int32 GSoundsNpcStateCombat = 2;
	inline constexpr int32 GSoundsNpcStateAlert = 3;
	// The NPC's current `m_NPCState`, in RETAIL's ordinals.
	inline int32 SoundsRetailNpcState(EElysiumNpcState State)
	{
		switch (State)
		{
		case EElysiumNpcState::Idle:     return GSoundsNpcStateIdle;
		case EElysiumNpcState::Combat:   return GSoundsNpcStateCombat;
		case EElysiumNpcState::Alert:    return GSoundsNpcStateAlert;
		case EElysiumNpcState::Scripted: return 4;
		case EElysiumNpcState::Prone:    return 6;
		case EElysiumNpcState::Dead:     return 7;
		default:                         return 0;
		}
	}
	// SEAM: `SENTENCEG_PlayRndSz(edict_t*, const char* group, float volume, soundlevel_t, int flags,
	// int pitch)` — retail `0x101aeb60`, reached from `PlaySentence`'s ordinary arm and from both
	// `CNPC_VTzimisce` sound hooks.
	//
	// It resolves a SENTENCE GROUP name out of the engine's loaded `sentences.txt`, picks one member
	// of the group, resolves the caption text, and emits the chosen sentence by index. This runtime
	// carries no sentence table at all — nothing loads `sentences.txt`, nothing maps a group name to
	// members, and `IEngineSound::EmitSentenceByIndex` has no counterpart on `IElysiumAudio` — so
	// the seam is asked and refuses with retail's own "no such sentence" answer, -1.
	//
	// UNRECOVERED beyond that: whether VtMB ships `sentences.txt` at all, and what the `SPI_*` group
	// names the Tzimisce hooks pass resolve to. Both belong to the speech story, not to this one.
	inline int32 SoundsPlaySentenceGroup(const FElysiumNpcBase& Npc, const TCHAR* Group, float Volume,
		int32 SoundLevel, int32 Flags, int32 Pitch)
	{
		static bool bReported = false;
		if (!bReported)
		{
			bReported = true;
			UE_LOG(LogElysiumNpcEnt, Verbose,
				TEXT("%s SENTENCEG_PlayRndSz(\"%s\", vol=%.2f, lvl=%d, flags=%d, pitch=%d) refused: "
					"this runtime carries no sentence table (retail 0x101aeb60)"),
				*Npc.DebugString(), Group != nullptr ? Group : TEXT(""), Volume, SoundLevel, Flags,
				Pitch);
		}
		return -1;
	}
}
