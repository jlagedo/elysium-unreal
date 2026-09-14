#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"

// Story 29d, family **SpeciesMisc10** — the Sabbat leader, the Tzimisce runner and head claw, the
// vampire boss, the Werewolf, and the Bullseye/Pedestrian spawn-side bodies. The first half is in
// `ElysiumNpcKernelSpeciesMisc10.cpp`; the declarations are in `ElysiumNpcKernelSpeciesMisc10.inl`.

namespace
{
	// --- `.rdata`, read out of the pinned `vampire.dll` at `address - 0x10000000` ---------------

	// `_DAT_104c3cc8` = **8.0** s — the idle window `0x103c67f0` is handed by
	// `CheckForJumpCondition`, measured from `m_flLastAttackTime` (`+0x5d9c`).
	constexpr float GSabbatJumpIdleSeconds = 8.f;
	// `_DAT_104c3cc4` = **0.0666667** — the health fraction lost since the mark that also allows it.
	constexpr float GSabbatJumpHealthLoss = 0.06666667f;
	// `_DAT_104c3cdc` = **0.25** s — the blood-splash repeat interval.
	constexpr double GSabbatSplashIntervalSeconds = 0.25;
	// `_DAT_104c3ce0` = **2.0** — the wound-counter rise `PlayerDamagedEnoughThisRound` requires.
	constexpr float GSabbatRoundDamageThreshold = 2.f;

	// `_DAT_104cd108` is a **DOUBLE** reading **240.0** (`103c20dc` is `FCOMP double ptr`). The 2-D
	// distance at or past which slot 332 raises condition `0x35`.
	constexpr double GHeadClawConditionDistanceUnits = 240.0;
	// `_DAT_1044fab0`, the DOUBLE 0.0 sentinel, and `0x43fa0000` = 500.0.
	constexpr double GSlowExpireSentinel = 0.0;
	constexpr float GSlowEntityMagnitude = 500.f;
	// `103c1dd5`: `PUSH 8.0` then `PUSH 5.0` — `RandomFloat(5.0, 8.0)`.
	constexpr float GHeadClawSlowSecondsMin = 5.f;
	constexpr float GHeadClawSlowSecondsMax = 8.f;

	// `_DAT_104ce8bc` = **2.0** s, the transformation wait.
	constexpr double GTransformWaitSeconds = 2.0;

	// The stat ids, in retail's numbering. `0x0f` is the accumulated WOUND counter (family
	// Combat10's `GStatWounds`), which is why `Set(0x0f, 0)` is a full heal.
	constexpr int32 GStatWounds = 0x0f;

	// The three Werewolf schedules whose failure prints the diagnostic block (`103ce7d8`…).
	constexpr int32 GWerewolfDiagnosticSchedules[3] = { 0x160, 0x161, 0x162 };

	// The zone word's bits, in the order the overlay prints them (`103d51f3`…`103d52dd`).
	struct FWerewolfZoneBit { uint32 Bit; const TCHAR* Name; };
	constexpr FWerewolfZoneBit GWerewolfZoneBits[] =
	{
		{ 0x0001u, TEXT("ZONE_NO_TALL_ANIMS") },
		{ 0x0002u, TEXT("ZONE_PLAYER_ON_BREAKABLE") },
		{ 0x0040u, TEXT("ZONE_PLAYER_INSIDE") },
		{ 0x0080u, TEXT("ZONE_PLAYER_OUTSIDE") },
		{ 0x0004u, TEXT("ZONE_PLAYER_ON_PLATFORM") },
		{ 0x0800u, TEXT("ZONE_WOLF_INSIDE") },
		{ 0x1000u, TEXT("ZONE_WOLF_OUTSIDE") },
		{ 0x0100u, TEXT("ZONE_WOLF_ON_PLATFORM") },
	};

	// The five conditions, in the order the overlay tests them (`103d52dd`…`103d535f`). Note `0x7b`
	// comes BEFORE `0x7a`.
	struct FWerewolfCondLine { int32 Cond; const TCHAR* Name; };
	constexpr FWerewolfCondLine GWerewolfCondLines[] =
	{
		{ 0x77, TEXT("COND_VWEREWOLF_CAN_TELEPORT") },
		{ 0x78, TEXT("COND_VWEREWOLF_CAN_SPECIAL_MOVE") },
		{ 0x79, TEXT("COND_VWEREWOLF_ENEMY_REACHABLE") },
		{ 0x7b, TEXT("COND_VWEREWOLF_SHOULD_BREAKHINT") },
		{ 0x7a, TEXT("COND_VWEREWOLF_DEATH_TRIGGERED") },
	};

	// `CNPC_Bullseye::Spawn`'s literals (`103567e0`…`1035693f`).
	constexpr float GBullseyeHullUnits = 16.f;
	constexpr int32 GBullseyeBloodColor = 0xf7;
	constexpr float GBullseyeFieldOfView = 0.5f;          // 0x3f000000
	// `_DAT_104493d0` is a **DOUBLE** reading **0.1** — the think delay the spawn arms.
	constexpr double GBullseyeThinkDelaySeconds = 0.1;
	constexpr int32 GBullseyeFlag = 0x2000;
	constexpr int32 GBullseyeFlag2 = 0x10;
	constexpr int32 GBullseyeSolid = 2;
	constexpr int32 GBullseyeSolidFlagBase = 0x10;
	constexpr int32 GBullseyeSolidFlagTrigger = 4;
	constexpr int32 GBullseyeEffectsNoDraw = 0x40;
	constexpr int32 GBullseyeSpawnflagBloodColor = 0x80000;
	constexpr int32 GBullseyeSpawnflagSolidFlag = 0x10000;
	constexpr int32 GBullseyeSpawnflagNoDamage = 0x20000;

	double SpeciesMisc10_2Now(const FElysiumNpc& Npc)
	{
		return Npc.World != nullptr ? Npc.World->NowSeconds() : 0.0;
	}

	bool SpeciesMisc10_2IsPlayer(const FElysiumNpc& Npc, const FElysiumEntity* Candidate)
	{
		return Candidate != nullptr && Npc.World != nullptr
			&& Candidate->Handle == Npc.World->PlayerHandle();
	}
}

// =================================================================================================
// `CNPC_Bullseye::Spawn` — `0x103567e0`, slot 103's species body.
// =================================================================================================

void FElysiumNpc::BullseyeSpawn()
{
	FBullseyeSpawnRecord& R = BullseyeSpawn_Record;
	R = FBullseyeSpawnRecord();
	R.bRan = true;
	// `103567e6`: slot 0x1a0 (416/4 is not this one — `vt+0x1a0` is slot 104, `Precache`).
	Precache();
	// `1035680d`: `SetSize(-16,-16,-16 .. 16,16,16)` through `0x101cf390`, with the MAXS built first
	// on the stack and the MINS handed as the first argument.
	R.HullMinsUnits = FVector(-GBullseyeHullUnits, -GBullseyeHullUnits, -GBullseyeHullUnits);
	R.HullMaxsUnits = FVector(GBullseyeHullUnits, GBullseyeHullUnits, GBullseyeHullUnits);
	// The port's slot 213 `SetSize` (`0x100b1890`) takes the SIZE vector — `m_vecSize` (+0x038c) —
	// which is what `0x101cf390` writes from the mins/maxs pair; the pair itself is recorded above.
	SetSize((R.HullMaxsUnits - R.HullMinsUnits) * ElysiumMove::U);
	// `10356818`: slot 0x174 (93) with (0, 0).
	// `1035681f`: `SetBloodColor(0xf7)` — the FIRST of two.
	R.BloodColorFirst = GBullseyeBloodColor;
	// `1035682a`..`10356840`: `m_fEffects = 0`, `m_flFieldOfView = 0.5`, `m_flGravity = 0`.
	R.Effects = 0;
	R.FieldOfView = GBullseyeFieldOfView;
	R.Gravity = 0.f;
	// `10356850`: `SetBloodColor` AGAIN — `0xf7` under spawnflag `0x80000` and `-1` otherwise. The
	// SECOND call is what actually decides the blood colour.
	R.BloodColorSecond = (SpawnFlags & GBullseyeSpawnflagBloodColor) != 0
		? GBullseyeBloodColor : -1;
	// `1035685f`: `AddFlag(0x2000)`.
	R.Flags = GBullseyeFlag;
	// `1035686c`: `ThinkSet(LAB_100097fa, 0.0)` then `m_flNextThink = curtime + _DAT_104493d0` — a
	// **DOUBLE** 0.1 in `.rdata`, not the 0.0 the `ThinkSet` argument carries.
	R.NextThink = SpeciesMisc10_2Now(*this) + GBullseyeThinkDelaySeconds;
	// `103568b4`: `SetSolid(2)` then `AddSolidFlags(+0x2b4 | 0x10)`.
	R.Solid = GBullseyeSolid;
	R.SolidFlags = GBullseyeSolidFlagBase;
	// `10356906`: `AddSolidFlags(| 4)` ONLY under spawnflag `0x10000`.
	if ((SpawnFlags & GBullseyeSpawnflagSolidFlag) != 0)
	{
		R.SolidFlags |= GBullseyeSolidFlagTrigger;
	}
	// `10356916`: `m_takedamage = 0` under spawnflag `0x20000`, else 2.
	R.TakeDamage = (SpawnFlags & GBullseyeSpawnflagNoDamage) != 0 ? 0 : 2;
	// `10356928`: `Relink`, then `m_fEffects |= 0x40`, then `PhysicsCheckWater`, then
	// `AddFlag2(0x10)` — in that order.
	R.bRelinked = true;
	R.Effects |= GBullseyeEffectsNoDraw;
	R.bPhysicsCheckedWater = true;
	R.Flags2 = GBullseyeFlag2;
}

// =================================================================================================
// `CNPC_VBach::GatherAttackConditions` — `0x10363db0`, slot 561's species prologue.
// =================================================================================================

void FElysiumNpc::BachGatherAttackConditions(float DistanceUnits)
{
	FElysiumNpcConditions& Conds = Cognition.Conditions;
	// `10363dc6`: the WHOLE shield/teleport block is gated on `HasCondition(0x4c LIGHT_DAMAGE)` or
	// `HasCondition(0x4d HEAVY_DAMAGE)`.
	const bool bDamaged = Conds.Has(static_cast<EElysiumNpcCond>(0x4c))
		|| Conds.Has(static_cast<EElysiumNpcCond>(0x4d));
	if (bDamaged)
	{
		// `10363de3`: `m_bCondTookDamage` (+0x5b80) is cleared FIRST, inside the block.
		Cognition.bCondTookDamage = false;
		const double Now = SpeciesMisc10_2Now(*this);
		// `10363df1`: the distance at or above `DAT_1062d200[m_iBachTeleportState]`
		// (**768, 768, 384, 384**) OR `m_flNextHolyLightTime` already past takes the SHIELD arm;
		// anything else is the teleport arm.
		const int32 StateIndex = FMath::Clamp(BachTeleportState, 0, 3);
		const bool bShieldArm = DistanceUnits >= BachTeleportDistanceUnits[StateIndex]
			|| BachNextHolyLightTime <= Now;
		if (bShieldArm)
		{
			// `10363e1a`: the shield only fires when `m_flNextShieldTime` (+0x6688) is past.
			if (BachNextShieldTime < Now)
			{
				// `10363e96`: `CVStatList_t::SetBase(stat 0xd, 5)` on the type-3 (scripted) list,
				// found by the `+0x13bc`/`+0x13c0` walk for tag `+0x10 == 3` and falling back to the
				// lazily built global. Family Combat10's typed-stat seam is that walk.
				TypedStatSet(/*ListType*/ 3, /*StatId*/ 0xd, 5);
				// `10363eb3`: `m_flNextShieldTime = curtime + _DAT_1044eb0c` (**20.0**).
				BachNextShieldTime = Now + 20.0;
				// `10363ecb`: `m_bShieldActive = 1` and `m_flShieldTime = curtime + _DAT_1046bac0`
				// (**6.0**).
				bBachShieldActive = true;
				BachShieldTime = Now + 6.0;
				// `10363f3a`: the shield sound, through a `CPASAttenuationFilter` built from slot
				// 222 at attenuation 0.8, on channel 2 at volume 1.0 and pitch 100.
				EmitNamedWav(this, /*Channel*/ 2, TEXT("Character/Boss/Bach/bach_shield.wav"),
					/*Volume*/ 1.f, /*Attenuation*/ 0.8f, /*Pitch*/ 100);
			}
		}
		else
		{
			// `10363f6c`: the cvar touch, then condition `0x7b` (teleport).
			Conds.Set(static_cast<EElysiumNpcCond>(0x7b));
		}
		// `10363f81`: `+0x66a6` is cleared on EITHER branch, and inside the damage gate.
		bBachShieldFlagB = false;
	}
	// `10363f87`: INDEPENDENTLY of all of the above — when `m_flNextWeaponSwitchTime` (+0x668c) is
	// past, condition `0x79` at or above `_DAT_104704d0` (**72.0**) and `0x7a` below it.
	if (BachNextWeaponSwitchTime < SpeciesMisc10_2Now(*this))
	{
		Conds.Set(static_cast<EElysiumNpcCond>(DistanceUnits >= 72.f ? 0x79 : 0x7a));
	}
	// `10363fc5`: `CAI_BaseNPC::GatherAttackConditions` runs LAST and unmodified — the caller does
	// that, so nothing else happens here.
}

// =================================================================================================
// `CNPC_VPedestrian::CreateCorpse` — `0x103a38c0`, slot 301's species body.
// =================================================================================================

void FElysiumNpc::PedestrianCreateCorpse()
{
	// `103a38c6` / `103a38e8`: the collision OBB snapshot, BEFORE the base, because the base resizes
	// the hull. `m_Collision` vtable `+4` is the mins and `+8` the maxs, three floats each; family
	// Motor's `CollisionMinsUnits`/`CollisionMaxsUnits` is that pair.
	FVector Mins = FVector::ZeroVector;
	FVector Maxs = FVector::ZeroVector;
	RetailCollisionExtents(*this, Mins, Maxs);
	PedestrianPreDeathMinsUnits = Mins;
	PedestrianPreDeathMaxsUnits = Maxs;
	// `103a3910`: `CBaseCombatCharacter::CreateCorpse`. SEAM: this substrate stands no corpse entity
	// at the kernel tier and slot 301's Troika-line body is another row's, so the call is the record
	// below and nothing else. Named rather than hidden.
	++PedestrianCreateCorpseCalls;
	// `103a391c`: `ThinkSet(NULL, 0.0, NULL)` — the think function is CLEARED, so a pedestrian
	// corpse never thinks again. Nothing in this substrate unbinds a think function; the record is
	// what says the body asked.
	bPedestrianCorpseThinkStopped = true;
	// `103a396b`: `SetSolid(SOLID_NONE)` on the collision, under a `CBaseEntity::SetSolid`
	// scope-trace frame.
	PedestrianCorpseSolid = 0;
}

// =================================================================================================
// `CNPC_VSabbatLeader` — `0x103a9d90`, `0x103aa960`, `0x103aaa80`, `0x103aabc0`.
// =================================================================================================

bool FElysiumNpc::AttackIdleLongerThan(float Seconds) const
{
	// `0x103c67f0`: `curtime - m_flLastAttackTime (+0x5d9c) > Seconds`, STRICTLY greater.
	return (SpeciesMisc10_2Now(*this) - LastAttackTime) > static_cast<double>(Seconds);
}

bool FElysiumNpc::CheckForJumpCondition()
{
	// `103a9dda`: `0x103c67f0(this, DAT_104c3cc8)` — **8.0** s since the last attack.
	if (AttackIdleLongerThan(GSabbatJumpIdleSeconds))
	{
		return true;
	}
	// `103a9df6`: the health delta at or above `_DAT_104c3cc4` (**0.0666667**). The percent rises
	// with damage, so this is "lost a fifteenth of the bar since the mark".
	if (HealthPercentLostSinceRecord() >= GSabbatJumpHealthLoss)
	{
		return true;
	}
	// `103a9e15`: otherwise the answer IS `PlayerDamagedEnoughThisRound`'s.
	return PlayerDamagedEnoughThisRound();
}

void FElysiumNpc::SabbatLeaderUpdateBloodSplash()
{
	// `103aa9ad`: `m_bDiving` set does NOTHING AT ALL — not even the level copy, which is what makes
	// leaving a dive re-fire the big splash.
	if (bSabbatDiving)
	{
		return;
	}
	const int32 CurrentWaterLevel = WaterLevel;
	// `103aa9b9`: the current level above 0 AND (the previous level was 0 OR the interval has
	// lapsed). The interval test is `m_fLastSplashTime + 0.25 < curtime`, strictly.
	const bool bIntervalPast =
		SabbatLastSplashTime + GSabbatSplashIntervalSeconds < SpeciesMisc10_2Now(*this);
	if (CurrentWaterLevel > 0 && (SabbatLastWaterLevel == 0 || bIntervalPast))
	{
		// `103aa9e5`: the ordinary splash always...
		SpawnBloodPoolEmitter(TEXT("bloodsplash_emitter"), nullptr);
		// `103aaa01`: ...and the big one ONLY on the dry-to-wet edge.
		if (SabbatLastWaterLevel == 0)
		{
			SpawnBloodPoolEmitter(TEXT("bloodbigsplash_emitter"), nullptr);
		}
		// `103aaa1a`: the stamp, after both spawns.
		SabbatLastSplashTime = SpeciesMisc10_2Now(*this);
	}
	// `103aaa27`: the previous level is copied on EVERY non-diving pass, inside the diving guard and
	// outside the splash one.
	SabbatLastWaterLevel = CurrentWaterLevel;
}

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

void FElysiumNpc::RecordPlayerHealth()
{
	// `103aaad6`: nothing at all without a LIVE `m_hClosestPlayer` — the mark keeps its previous
	// value, which is retail's own behaviour and not a reset.
	const FElysiumEntity* Player = World != nullptr
		? World->Resolve(Senses.Memory.ClosestPlayer) : nullptr;
	if (Player == nullptr)
	{
		return;
	}
	// `103aab5f`: stat `0x0f` off the player's type-0 list. That is the accumulated WOUND counter —
	// `CBaseCombatCharacter::HealthToPercent` (`0x1032fe60`) computes
	// `((stat0x11 - stat0x0f) * m_iMaxHealth) / stat0x11` — so this snapshots the player's DAMAGE
	// TOTAL at the start of a round, not its health.
	SabbatLastPlayerHealth = TypedStatValueOf(Player, GStatWounds);
}

bool FElysiumNpc::PlayerDamagedEnoughThisRound() const
{
	// `103aabf9`: no live closest player answers false.
	const FElysiumEntity* Player = World != nullptr
		? World->Resolve(Senses.Memory.ClosestPlayer) : nullptr;
	if (Player == nullptr)
	{
		return false;
	}
	// `103aaca0`: the same stat re-derived, then `_DAT_104c3ce0 <= (float)(stat0x0f - mark)` — the
	// wound counter having risen by at least **2.0** since `RecordPlayerHealth`, which is what makes
	// the retail name literal.
	const float Risen = static_cast<float>(TypedStatValueOf(Player, GStatWounds)
		- SabbatLastPlayerHealth);
	return GSabbatRoundDamageThreshold <= Risen;
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

void FElysiumNpc::Slot332(FElysiumEntity* SlowTarget)
{
	// `CAI_BaseNPC#332` / `CAI_BaseNPCTroika#332` (`0x1014f890`) — the whole Troika-line body is
	// `return;`. It moved out of the generated file only so this dispatcher has a prologue, exactly
	// as story 29c-1's cleanup moved slots 488 and 506 for the same reason; the base contributes
	// nothing and the ONE species override is `CNPC_VTzimisceHeadClaw`'s `0x103c1d80`.
	const TCHAR* const SlotBody = ElysiumNpcKernelClass::BodyOf(RetailClass(), 332);
	if (SlotBody != nullptr && FCString::Strcmp(SlotBody, TEXT("0x103c1d80")) == 0)
	{
		HeadClawSlot332(SlowTarget);
	}
}

void FElysiumNpc::HeadClawSlot332(FElysiumEntity* SlowTarget)
{
	// `103c1d8a`..`103c1daa`: a null target, a target with no `+0xa8` player record, or one with no
	// `+0x9c` combat view does nothing at all. So slot 332 is a PLAYER-ONLY body.
	if (SlowTarget == nullptr || !SpeciesMisc10_2IsPlayer(*this, SlowTarget))
	{
		return;
	}
	FElysiumCombatCharacter* Victim = SlowTarget->AsCombatCharacter();
	if (Victim == nullptr)
	{
		return;
	}
	// 1. `103c1db0`: `BeginSlowEntity(victim, 500.0)` only while `m_flSlowedExpire` (**+0x6678**,
	//    not the `+0x6684` the walk names) still equals the 0.0 sentinel.
	if (HeadClawSlowedExpire == GSlowExpireSentinel)
	{
		BeginSlowEntity(SlowTarget->Handle, GSlowEntityMagnitude);
	}
	// 2. `103c1dd5`: `m_flSlowedExpire = RandomFloat(5.0, 8.0) + curtime`, unconditionally.
	HeadClawSlowedExpire = SpeciesMisc10_2Now(*this)
		+ ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).FRandRange(GHeadClawSlowSecondsMin,
			GHeadClawSlowSecondsMax);
	// 3. `103c1e02`: `m_hSlowedEntity` (**+0x6674**) = the victim's handle.
	HeadClawSlowedEntity = SlowTarget->Handle;
	// 4. `103c1e08`: with the particle handle (**+0x667c**) dead, spawn `Tzim2_player_emitter` at the
	//    VICTIM's slot-217 origin, store it, attach at `Bip01 Spine` mode 1, start it.
	if (World == nullptr || World->Resolve(HeadClawPlayerEmitter) == nullptr)
	{
		const int32 Index = CreateNamedEmitter(TEXT("Tzim2_player_emitter"),
			SlowTarget->Origin / ElysiumMove::U, /*AttachMode*/ 1, SlowTarget->Handle, TEXT("Bip01 Spine"));
		StartNamedEmitter(Index);
	}
	// 5. `103c1f13` / `103c1f5c`: the two Slug sounds, from a `CPASAttenuationFilter` built on the
	//    VICTIM's slot-222 emission origin at attenuation 0.8, volume 1.0 and pitch 100 — the HIT on
	//    channel **4** and the AFFECTED on channel **3**, in that order.
	EmitNamedWav(SlowTarget, /*Channel*/ 4, TEXT("Character/Monster/TC_FatGuy/Sluge_Hit.wav"),
		1.f, 0.8f, 100);
	EmitNamedWav(SlowTarget, /*Channel*/ 3, TEXT("Character/Monster/TC_FatGuy/Sluge_Affected.wav"),
		1.f, 0.8f, 100);
	// 6. `103c1f87`: with the player's inventory slot 0 holding an entity and the HUD handle
	//    (**+0x6680**) dead, spawn `HUD_Tzim2_emitter` at `vec3_origin`, attach it to THAT ITEM with
	//    mode `0xe` and an EMPTY bone name, and start it.
	const FElysiumEntity* Item = PlayerInventorySlot0();
	if (Item != nullptr && (World == nullptr || World->Resolve(HeadClawHudEmitter) == nullptr))
	{
		const int32 Index = CreateNamedEmitter(TEXT("HUD_Tzim2_emitter"), FVector::ZeroVector,
			/*AttachMode*/ 0xe, Item->Handle, TEXT(""));
		StartNamedEmitter(Index);
	}
	// 7. `103c20ba`: the **2-D** distance (`sqrt(dx*dx + dy*dy)`, no Z) between MY slot-217 origin
	//    and the victim's; at or past `_DAT_104cd108` — a DOUBLE reading **240.0** — the cvar is
	//    touched and `SetCondition(0x35)` is raised.
	const FVector Delta = (SlowTarget->Origin - Origin) / ElysiumMove::U;
	const double Flat = FMath::Sqrt(Delta.X * Delta.X + Delta.Y * Delta.Y);
	if (GHeadClawConditionDistanceUnits <= Flat)
	{
		Cognition.Conditions.Set(static_cast<EElysiumNpcCond>(0x35));
	}
}

bool FElysiumNpc::HeadClawSlowRunning() const
{
	// `0x103c24a0`: `m_flSlowedExpire` (+0x6678) strictly above 0.0.
	return HeadClawSlowedExpire > GSlowExpireSentinel;
}

void FElysiumNpc::TzimisceHeadClawEndSlow(bool bForce)
{
	// `103c223c`: the gate, then forced-or-lapsed.
	if (!HeadClawSlowRunning())
	{
		return;
	}
	if (!bForce && !(HeadClawSlowedExpire <= SpeciesMisc10_2Now(*this)))
	{
		return;
	}
	// `103c2265`: zero the expiry first.
	HeadClawSlowedExpire = 0.0;
	// `103c2277`: ONLY while the slowed entity resolves AND carries a combat view — the handle
	// clear, the `EndSlowEntity` and the sound are ALL inside that guard, so a stale handle leaves
	// `m_hSlowedEntity` standing.
	FElysiumEntity* Victim = World != nullptr ? World->Resolve(HeadClawSlowedEntity) : nullptr;
	if (Victim != nullptr && Victim->AsCombatCharacter() != nullptr)
	{
		EndSlowEntity(HeadClawSlowedEntity, GSlowEntityMagnitude);
		HeadClawSlowedEntity = FElysiumEntityHandle::Invalid();
		// `103c2319`: `…/Sluge_Affected.wav` on channel 3 from the VICTIM's slot-222 origin at
		// attenuation 0.8 — the same wav slot 332's second emit uses.
		EmitNamedWav(Victim, /*Channel*/ 3,
			TEXT("Character/Monster/TC_FatGuy/Sluge_Affected.wav"), 1.f, 0.8f, 100);
	}
	// `103c2360` / `103c23e0`: `UTIL_Remove` the two owned effects at **+0x667c** and **+0x6680**,
	// each only while its handle's serial matches its slot, and set both to -1 either way.
	if (World != nullptr && World->Resolve(HeadClawPlayerEmitter) != nullptr)
	{
		RemoveNamedEntity(HeadClawPlayerEmitter);
		HeadClawPlayerEmitter = FElysiumEntityHandle::Invalid();
	}
	if (World != nullptr && World->Resolve(HeadClawHudEmitter) != nullptr)
	{
		RemoveNamedEntity(HeadClawHudEmitter);
		HeadClawHudEmitter = FElysiumEntityHandle::Invalid();
	}
}

// =================================================================================================
// `CNPC_VTzimisceRunner` — `0x103c3cd0` (slot 335) and `0x103c3d10` (slot 336).
// =================================================================================================

float FElysiumNpc::RunnerHullEngineToken() const
{
	// SEAM for `DAT_1070b22c` vtable `+0x1dc` called with 0. The retail quantity is unrecovered; the
	// ONE property both bodies read is that it changes between frames and not within one, so the
	// substrate clock stands in and the substitution is named here.
	return static_cast<float>(SpeciesMisc10_2Now(*this));
}

void FElysiumNpc::TzimisceRunnerNotifyChangeSizeSmall()
{
	// `103c3cd3`: `SetHullSizeSmall(force = 1)` — family Motor10's `0x10273180`.
	SetHullSizeSmall(/*bForce=*/true);
	// `103c3cdb`: the form byte `+0x6672`, which family Anim10's `PreTranslate_TzimisceRunner`
	// (`0x103c3e10`) selects its four TZ activity variants on.
	bTzimisceRunnerForm = true;
	// `103c3ce2`: `m_bWantsLargeHull` (+0x5f2c) = 0.
	bWantsLargeHull = false;
	// `103c3cf6`: `+0x6674` = the engine token, the value slot 336 reads back.
	RunnerHullToken = RunnerHullEngineToken();
}

void FElysiumNpc::TzimisceRunnerNotifyChangeSizeNormal()
{
	// `103c3d1e`: re-read the engine token and compare against the one slot 335 cached. ONLY when
	// the two DIFFER does the restore happen — an unchanged token leaves the runner small and the
	// form byte set, so the restore is edge-triggered on the engine value, not on a request.
	if (RunnerHullEngineToken() == RunnerHullToken)
	{
		return;
	}
	// `103c3d4a`: `SetHullSizeNormal(force = 1)`, clear the form byte, set `m_bWantsLargeHull`.
	SetHullSizeNormal(/*bForce=*/true);
	bTzimisceRunnerForm = false;
	bWantsLargeHull = true;
}

// =================================================================================================
// `CNPC_VVampireBoss` — `0x103c63c0`, `0x103c6a00`, `0x103c6a20`, `0x103c6f40`.
// =================================================================================================

void FElysiumNpc::WaitForTransformation()
{
	// `103c6407`: nothing until `curtime` passes `m_flProteanTransformStartTime + _DAT_104ce8bc`
	// (**2.0** s), strictly.
	if (!(ProteanTransformStartTime + GTransformWaitSeconds < SpeciesMisc10_2Now(*this)))
	{
		return;
	}
	// `103c6490`: `CVStatList_t::Set(stat 0x0f, 0)` on the type-0 (Attributes) list — a FULL HEAL,
	// because `0x0f` is the wound counter and not current health.
	TypedStatSet(/*ListType*/ 0, GStatWounds, 0);
	// `103c64c4`: resolve `m_hTransformPartner`, `RTDynamicCast` it, and fire the partner's `+0x6664`
	// output with THIS as both activator and caller. The cast refuses anything that is not on the
	// boss line, which this runtime expresses as a `RetailClass` chain test.
	if (FElysiumEntity* Partner = World != nullptr ? World->Resolve(TransformPartner) : nullptr)
	{
		if (FElysiumNpc* PartnerNpc = Partner->AsNpc())
		{
			if (ElysiumNpcKernelClass::DerivesFrom(PartnerNpc->RetailClass(),
				TEXT("CNPC_VVampireBoss")))
			{
				PartnerNpc->FireOutput(TEXT("OnTransformComplete"), Handle);
			}
		}
	}
	// `103c6515`: `TaskComplete(0)` — `0x10273e80` returns untouched while `COND 0x5c TASK_FAILED`
	// stands, so a failed task is not completed by this.
	TaskComplete(/*bIgnoreTaskFailed=*/false);
}

void FElysiumNpc::RecordHealthPercent()
{
	// `0x103c6a00`: one write.
	BossHealthPercentRecord = GetCurrHealthPercent();
}

float FElysiumNpc::HealthPercentLostSinceRecord() const
{
	// `0x103c6a20`: `GetCurrHealthPercent() - m_HealthPercentRecord`, in that order. The percent is
	// `wounds / cap` and RISES with damage, so a body that has lost health answers POSITIVE —
	// the checklist's walk says negative and is wrong (standing fact one).
	return GetCurrHealthPercent() - BossHealthPercentRecord;
}

const TCHAR* FElysiumNpc::VampireBossEmitterAttachment(int32 Region)
{
	// `PTR_s_Bip01_L_Hand_1065e6d0`, the four pointers read out of the pinned image at `0x65e6d0`.
	// Regions 2 and 3 are the SAME string.
	switch (Region)
	{
	case 0: return TEXT("Bip01 L Hand");
	case 1: return TEXT("Bip01 R Hand");
	case 2: return TEXT("Bip01 Spine");
	case 3: return TEXT("Bip01 Spine");
	default: return TEXT("");
	}
}

void FElysiumNpc::VampireBossSpawnBodyEmitters()
{
	// `103c6f7c`: `KillBodyEmitters` FIRST. It has to be first because a spawn that answers null
	// leaves that slot's PREVIOUS handle standing rather than clearing it.
	KillBodyEmitters();
	// `103c6f8a`: four fixed iterations, `SpawnBodyEmitter(i, name)`, storing the spawned entity's
	// handle into `m_hParticleEmitters[i]` (+0x66a0) ONLY when the spawn answered something.
	for (int32 Region = 0; Region < 4; ++Region)
	{
		// The attachment name is retail's argument; family Damage's `SpawnBodyEmitter` takes the
		// attach ENTITY, and the name lives in `BodyEmitterNames[Region]` — which is the same four
		// words a species Restore stamps. The bone the table supplies is recorded beside the call.
		BodyEmitterAttachments[Region] = VampireBossEmitterAttachment(Region);
		const int32 Index = SpawnBodyEmitter(Region, Handle);
		if (Index != INDEX_NONE)
		{
			ParticleEmitters[Region] = Handle;
		}
	}
}

// =================================================================================================
// `CNPC_VWerewolf` — `0x103ce750` (slot 448), `0x103d0db0`, `0x103d5130` (slot 76), `0x103d9f90`.
// =================================================================================================

void FElysiumNpc::WerewolfTaskFail(int32 Reason)
{
	// `103ce7d8`: the DIAGNOSTIC half runs only when the running schedule (`+0x5c38`) equals one of
	// `0x160`, `0x161` or `0x162` resolved through `0x102cc1f0`.
	// `ElysiumScheduleNumber` is this runtime's retail schedule number. The three ids `0x160`,
	// `0x161` and `0x162` are `CNPC_VWerewolf`'s own programs and have no row in
	// `EElysiumScheduleId` yet, so the comparison is made on the NUMBER and simply never matches
	// today — which is retail's own arm for a Werewolf running anything else. Named, not stubbed.
	const int32 Running = ElysiumScheduleNumber(Schedule.Current);
	bool bDiagnostic = false;
	for (const int32 Id : GWerewolfDiagnosticSchedules)
	{
		bDiagnostic = bDiagnostic || (Running != 0 && Running == Id);
	}
	if (bDiagnostic)
	{
		// `103ce838`: `DevWarning("Werewolf failed task '%32s' schedule...")` with the task name
		// from slot `0x704` over the current task, the schedule name at `schedule+0x40` and the
		// failure text from `0x10316fa0`. `INVALID TASK` when there is no current task.
		UE_LOG(LogElysiumNpcEnt, Warning,
			TEXT("Werewolf failed task '%d' schedule %d, reason %d"),
			Schedule.TaskIndex, Running, Reason);
		// `103ce87d` / `103ce89a` / `103ce8b7`: the three hint lines. The BREAK line is GATED on
		// `m_pBreakHint` but PRINTS `m_pTeleportHint` — a retail copy-paste bug, reproduced.
		if (MoveHintNode != INDEX_NONE)
		{
			UE_LOG(LogElysiumNpcEnt, Warning, TEXT("Current MOVE Hint: %d"), MoveHintNode);
		}
		if (TeleportHintNode != INDEX_NONE)
		{
			UE_LOG(LogElysiumNpcEnt, Warning, TEXT("Current TELEPORT Hint: %d"), TeleportHintNode);
		}
		if (WerewolfBreakHintNode != INDEX_NONE)
		{
			UE_LOG(LogElysiumNpcEnt, Warning, TEXT("Current BREAK Hint: %d"), TeleportHintNode);
		}
	}
	// `103ce8c6`: everything below is UNCONDITIONAL, including on the non-diagnostic path.
	SetHullSizeSmall(/*bForce=*/true);
	ClearMoveHint();
	ClearTeleportHint();
	WerewolfBreakHintNode = INDEX_NONE;
	// `103ce8e4`: `+0x66a4 = 0` — family Lifecycle's `WerewolfMorphTimerA`.
	WerewolfMorphTimerA = 0.f;
	// `103ce8eb`: `+0x66a1 = 1`.
	bWerewolfTaskFailed = true;
	// `103ce8f7`: the Troika base `0x1029adb0` — the caller runs it, so nothing is done here.
	// `103ce902`: `CheckStuck(NULL)`.
	CheckStuck();
	// `103ce90a`: the zone word cleared, then both hint-node caches to -1.
	WerewolfHintFlags = 0;
	WerewolfHintNodeCacheA = INDEX_NONE;
	RandomMoveHintNodeZone = INDEX_NONE;
}

bool FElysiumNpc::WerewolfHasPath(const FVector& StartUnits, const FVector& EndUnits) const
{
	// `103d0e2b`: the whole body is `0x102ee380(m_pNavigator, &start, &end)`. CORRECTION: the two
	// navigator-cache stamps the walk attributes to this body are inside `0x102ee380`, which writes
	// `nav+8` from the NPC's `+0x156c` hull and `nav+0xc` from the global frame word, repeats the
	// pair on the path object `0x102ecc00`, and only then forwards both Vectors to `0x102fdcc0`.
	//
	// SEAM: this substrate stands no `CAI_Path` object, so `0x102fdcc0` answers **false** — retail's
	// own answer for a navigator with no path — and the ask is recorded so a case can read that the
	// forward happened.
	HasPathQueries.Add(FHasPathQuery{ StartUnits, EndUnits });
	return false;
}

void FElysiumNpc::WerewolfDrawDebugStatOverlays(TArray<FString>& OutLines) const
{
	// `103d51a1`: `Not Seen Time  : %3.1f` with `max(curtime - +0x66ec, 0.0)` — the clamp against
	// `_DAT_104454c4` (0.0) at `103d51aa` is folded away by the decompiler and is reproduced here.
	const double NotSeen = FMath::Max(SpeciesMisc10_2Now(*this) - WerewolfLastSeenTime, 0.0);
	OutLines.Add(FString::Printf(TEXT("Not Seen Time  : %3.1f"), NotSeen));
	// `103d51d3`: `Player Distance: %3.1f` reads `+0x6264 m_flPlayerDist` — the CACHED distance, not
	// a computed range.
	OutLines.Add(FString::Printf(TEXT("Player Distance: %3.1f"),
		Senses.Memory.ClosestPlayerDistanceCm / ElysiumMove::U));
	// `103d51ee`: the tail into `CNPC_VMingXiao`'s arm `0x10366290` — chained, not replaced. The
	// port's slot-76 dispatcher owns that arm, so the caller runs it around this body.
	// `103d51f3`: the zone word, bit by bit, in retail's order.
	for (const FWerewolfZoneBit& Zone : GWerewolfZoneBits)
	{
		if ((WerewolfHintFlags & Zone.Bit) == Zone.Bit)
		{
			OutLines.Add(Zone.Name);
		}
	}
	// `103d52dd`: the five conditions, `0x7b` BEFORE `0x7a`.
	for (const FWerewolfCondLine& Line : GWerewolfCondLines)
	{
		if (Cognition.Conditions.Has(static_cast<EElysiumNpcCond>(Line.Cond)))
		{
			OutLines.Add(Line.Name);
		}
	}
	// `103d535f`: the door state. States 0..3 print; anything else prints NOTHING at all — retail
	// has no `default:`.
	switch (WerewolfDoorState)
	{
	case 0: OutLines.Add(FString::Printf(TEXT("door state: (%d)closed"), 0)); break;
	case 1: OutLines.Add(FString::Printf(TEXT("door state: (%d)closing"), 1)); break;
	case 2: OutLines.Add(FString::Printf(TEXT("door state: (%d)open"), 2)); break;
	case 3: OutLines.Add(FString::Printf(TEXT("door state: (%d)opening"), 3)); break;
	default: break;
	}
	// `103d539e`: the cvar-selected PLAYER hint dump — `UTIL_PlayerByIndex(1)` through
	// `0x10172710` and an RTTI cast to `CAI_Hint`. SEAM: no hint store, no cvar; the block is
	// recorded as unreachable rather than guessed at.
	// `103d53ea`: the NPC's own hint, `m_pMoveHint` (+0x66bc) FIRST, then `m_pTeleportHint`
	// (+0x66b0), then `m_pLastUsedTeleportHint` (+0x66b4) or `m_pLastUsedMoveHint` (+0x66c0) by two
	// more cvars. The OFFSETS the checklist's walk gives for the first two are swapped; the NAMES
	// are right.
	int32 DumpHint = MoveHintNode;
	if (DumpHint == INDEX_NONE)
	{
		DumpHint = TeleportHintNode;
	}
	if (DumpHint != INDEX_NONE)
	{
		OutLines.Add(FString::Printf(TEXT("hint %d"), DumpHint));
	}
	// `103d5450`: the LAST FIVE rows of the schedule stack (`+0x668c`, count `+0x6698`), a null row
	// printing `INVALID SCHEDULE`. The start index is `max(count - 5, 0)` — the decompiler's
	// `(count - 5) & ((count - 5 < 0) - 1)` is that clamp.
	const int32 Count = WerewolfScheduleStack.Num();
	const int32 First = FMath::Max(Count - 5, 0);
	const int32 Last = FMath::Min(First + 5, Count);
	for (int32 Index = First; Index < Last; ++Index)
	{
		const FString& Name = WerewolfScheduleStack[Index];
		OutLines.Add(Name.IsEmpty() ? FString(TEXT("INVALID SCHEDULE")) : Name);
	}
}

void FElysiumNpc::SnapToAnimationPoint()
{
	// `103d9fdc`: `MatchOriginAnglesToAnimation("Bip01", 1, 1)` — origin AND angles onto the
	// animation's root bone. SEAM: recorded, because this substrate samples no bone at this tier.
	MatchOriginAnglesCalls.Add(FMatchOriginAnglesCall{ TEXT("Bip01"), true, true });
	// `103d9fe9`: `SetHullSizeSmall(force = 0)`, which therefore does NOTHING when `+0x5f2d` already
	// says small.
	SetHullSizeSmall(/*bForce=*/false);
	// `103d9ff7`: the cached fake-hull triple cleared from `vec3_origin` (family Geometry's word).
	WerewolfFakeHullPosUnits = FVector::ZeroVector;
	// `103da00e`: `+0x66a8 = 0`.
	WerewolfSnapWordA = 0;
	// `103da015`: both hint-node caches to -1 — the snap also drops whatever hint the Werewolf was
	// heading for.
	WerewolfHintNodeCacheA = INDEX_NONE;
	RandomMoveHintNodeZone = INDEX_NONE;
}
