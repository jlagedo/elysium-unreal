#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcSpeciesMisc10_2Shared.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"

// Story 29d, family **SpeciesMisc10** — the Sabbat leader, the Tzimisce runner and head claw, the
// vampire boss, the Werewolf, and the Pedestrian spawn-side body. The first half is in
// `ElysiumNpcSpeciesMisc10.cpp`; the declarations are in `ElysiumNpcSpeciesMisc10.inl`.
// =================================================================================================
// `CNPC_VSabbatLeader` — `0x103a9d90`, `0x103aa960`, `0x103aaa80`, `0x103aabc0`.
// =================================================================================================

int32 FElysiumNpc::TypedStatValueOf(const FElysiumEntity* Candidate, int32 StatId)
{
	// The `+0x13bc` count / `+0x13c0` table walk for tag `+0x10 == 0` applied to ANOTHER entity —
	// family Combat10's `TypedStatValue` is the same walk on this NPC. A candidate that stands no
	// sheet answers 0, which is the lazily built global `DAT_109f0b40`'s own answer.
	const FElysiumCombatCharacter* Character =
		Candidate != nullptr ? Candidate->AsCombatCharacter() : nullptr;
	if (Character == nullptr)
	{
		return 0;
	}
	return Character->Sheet.GetCurrent(EElysiumTraitContainer::Attributes, StatId);
}

// =================================================================================================
// `CNPC_VTzimisceHeadClaw` — `0x103c1d80` (slot 332) and `0x103c2230`.
// =================================================================================================

void FElysiumNpc::EmitNamedWav(const FElysiumEntity* Emitter, int32 Channel, const TCHAR* Wav,
	float Volume, float Attenuation, int32 Pitch)
{
	// SEAM for `IEngineSound::EmitSound(edict, channel, wav, volume, attenuation, 0, pitch, 0, 0,
	// 1, 0)` on a NAMED wav. Recorded either way, because the channel/volume/attenuation triple IS
	// the recovered half; forwarded where the world stands an audio service.
	NamedWavEmits.Add(FNamedWavEmit{ FString(Wav), Channel, Volume, Attenuation, Pitch,
		Emitter != nullptr ? Emitter->Handle : FElysiumEntityHandle::Invalid() });
	if (IElysiumAudio* Audio = World != nullptr ? World->Audio() : nullptr)
	{
		FElysiumBodySound Sound;
		Sound.Rel = FString(Wav);
		Sound.Volume = Volume;
		Sound.Pitch = Pitch / 100.f;
		Sound.Channel = EElysiumSoundChannel::Body;
		Audio->PlayBodySound(Emitter != nullptr ? Emitter->Handle : Handle, Sound);
	}
}

