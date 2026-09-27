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
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumSchedule.h"

namespace NpcKernelSoundsShared
{
	// `soundlevel_t` for every species vocalization. Retail passes `EmitSound` the pair (volume,
	// attenuation 0.8); 0.8 is Source's `ATTN_NORM`, and the level behind it is the exact inverse of
	// `0x1026d5e1`'s own `20/(L-50)`: `L = 50 + 20/0.8 = 75` (the derivation is spelled once in
	// `ElysiumFootsteps.h` → `SpeciesSoundLevelDb`, which reads the same pair off the footstep
	// vfuncs). A species vocalization is `SNDLVL_NORM`; not one of these rows reads a distance.
	inline constexpr int32 GSoundsSpeciesSoundLevelDb = 75;
	// `pitch 100` on every row, which is this seam's 1.0 (`FElysiumBodySound::Pitch` is a
	// multiplier, not Source's percentage).
	inline constexpr float GSoundsSpeciesPitch = 1.0f;

	// Source `CHAN_*`. The vocalizations emit on `CHAN_VOICE`; the Sabbat leader's footstep and
	// attack hooks emit on `CHAN_BODY`.
	inline constexpr int32 GSoundsChanVoice = 2;
	inline constexpr int32 GSoundsChanBody = 4;

	// The wav-pool arm of a species sound hook, as each body spells it: `RandomInt(0, Count - 1)`
	// (`DAT_1070b244` slot 2, inclusive at both ends) over the body's own table, then
	// `EmitSound(CPASAttenuationFilter(GetSoundEmissionOrigin(), 0.8), entindex(), channel, wav,
	// volume, 0.8, 0, 100, NULL, NULL, true, 0)`. The PAS filter is a recipient cull single-player
	// never runs (`docs/vtmb/footsteps.md` §3.5), so the port emits and builds no filter. A draw on
	// `CHAN_BODY` is a footstep-family draw (`EElysiumRngStream::Footsteps`); any other is an
	// NPC-think decision on the schedule stream.
	inline void SoundsEmitSpeciesWav(FElysiumNpcBase& Npc, const TCHAR* const* Wavs, int32 Count, float Volume,
		int32 Channel)
	{
		if (Wavs == nullptr || Count <= 0)
		{
			return;
		}
		const EElysiumRngStream Which = Channel == GSoundsChanBody
			? EElysiumRngStream::Footsteps : EElysiumRngStream::NpcSchedule;
		const int32 Index = ElysiumRng::Stream(Which).RandRange(0, Count - 1);
		IElysiumAudio* Audio = Npc.World != nullptr ? Npc.World->Audio() : nullptr;
		if (Audio == nullptr)
		{
			return;
		}
		FElysiumBodySound Sound;
		Sound.Rel = Wavs[Index];
		Sound.Volume = Volume;
		Sound.SoundLevelDb = GSoundsSpeciesSoundLevelDb;
		Sound.Pitch = GSoundsSpeciesPitch;
		Sound.Channel = static_cast<EElysiumSoundChannel>(Channel);
		Audio->PlayBodySound(Npc.Handle, Sound);
	}

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
