#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcDamageShared.h"

#include "ElysiumDecalSubsystem.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumRng.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumReactions.h"

// Story 29c-1, family **Damage** — damage, death and the effects the two spawn. The declarations,
// the two retail packets and the family's four standing facts are
// `Substrate/ElysiumNpcDamage.inl`; the walked prose is `docs/vtmb/combat-and-damage.md`
// (the damage transaction's own concern) and `docs/vtmb/npc-ai/lifecycle.md` (spawn/death/effects).
//
// Every threshold below was read out of the pinned retail `vampire.dll`'s `.rdata` at its cited
// address (image base `0x10000000`), so the numbers are recovered facts and not estimates. Where an
// address is quoted with no number beside it, the datum lives past `.data`'s raw size and is filled
// at runtime — those are named as seams, never guessed. Where the decompiled C had lost the body
// (`0x100b4ea0`'s tail call, `0x102664c0`'s and `0x10266780`'s stack-shifted arguments) the LISTING
// was read instead and the section says so.
//
// This file carries the family's seams and its Troika-line SLOT bodies. The per-species death,
// throw and emitter bodies are `Substrate/ElysiumNpcDamage2.cpp`.

namespace
{
	constexpr float BlockedReactionShort = 0.5f;  // _DAT_1049a1b8
	constexpr float BlockedReactionLong = 1.5f;   // _DAT_1049a1bc
	constexpr float EmitterFadeSeconds = 0.1f;    // `thunk_FUN_100fbbb0`'s second argument

	// `0x102b8c40`'s schedule id and its selector-trace line.
	constexpr int32 TookDamageSchedule = 0x8a;
	constexpr int32 TookDamageTraceLine = 0x5f8b;

}

void FElysiumNpc::ResetDeathThrowImpulse()
{
	// `DAT_1070d1b0`/`b4`/`b8` — `vec3_origin`, the value `OnTakeDamage_Dead` seeds its local with
	// and leaves in place when the packet carries no attacker.
	NpcKernelDamageShared::GDeathThrowImpulse = FVector::ZeroVector;
}

// =================================================================================================
// The seams. Each answers nothing, or goes through this runtime's own service, and names the retail
// call it stands for.
// =================================================================================================

FElysiumEntityHandle FElysiumNpc::CreateNamedEntity(const TCHAR* Classname,
	const FVector& PositionUnits)
{
	// SEAM for `CBaseEntity::Create` / `CreateNoSpawn` by classname. None of the three classnames
	// this family spawns (`item_w_grenade_frag`, `prop_physics`, `item_w_chang_energy_ball`) is a
	// registered leaf here, so this records the request and answers an invalid handle — retail's own
	// "Create failed" arm.
	FCreateEntityCall Call;
	Call.Classname = Classname != nullptr ? Classname : TEXT("");
	Call.PositionUnits = PositionUnits;
	CreateEntityCalls.Add(Call);
	return FElysiumEntityHandle();
}

int32 FElysiumNpc::CreateNamedEmitter(const FString& Name, const FVector& PositionUnits,
	int32 AttachMode, const FElysiumEntityHandle& AttachEntity, const TCHAR* AttachBone)
{
	// `thunk_FUN_100fbc90(name, origin, angles)` then, when a mode is given, vtable `+0x3cc`.
	// Retail refuses an empty name before it creates anything.
	if (Name.IsEmpty())
	{
		return INDEX_NONE;
	}

	FEmitterCall Call;
	Call.Name = Name;
	Call.PositionUnits = PositionUnits;
	Call.AttachMode = AttachMode;
	Call.AttachBone = AttachBone != nullptr ? AttachBone : TEXT("");
	Call.AttachEntity = AttachEntity;

	// This runtime HAS the particle seam, so the create goes through it. Headless it answers an
	// invalid effect handle, which is the ordinary negative and not a failure.
	if (IElysiumEmbodiment* Embodiment = World != nullptr ? World->Embodiment() : nullptr)
	{
		const FElysiumEntityHandle* Parent =
			AttachEntity.IsSet() ? &AttachEntity : nullptr;
		const FElysiumEffectHandle Effect = Embodiment->SpawnParticleRoot(Name, Parent, AttachMode,
			Call.AttachBone.IsEmpty() ? NAME_None : FName(*Call.AttachBone), INDEX_NONE,
			PositionUnits * ElysiumMove::U, FRotator::ZeroRotator);
		Call.EffectId = Effect.Id;
	}

	return EmitterCalls.Add(Call);
}

void FElysiumNpc::StartNamedEmitter(int32 EmitterIndex)
{
	// Retail's `+0x3c4`. A root the seam never stood still records that the start ran, because the
	// ORDER (create, attach, start) is the recovered half.
	if (!EmitterCalls.IsValidIndex(EmitterIndex))
	{
		return;
	}
	EmitterCalls[EmitterIndex].bStarted = true;
}

void FElysiumNpc::KillNamedEmitter(const FElysiumEntityHandle& Emitter)
{
	// Retail's `+0x3c8` (stop) then `thunk_FUN_100fbbb0(entity, 0.1)` — the 0.1 s fade-and-remove.
	// Both are guarded on the EHANDLE resolving; nothing in this substrate creates an emitter
	// ENTITY, so every call takes the guarded arm, which is retail's own answer for a dead handle.
	++EmitterKillCalls;
	FElysiumEntity* Entity = (World != nullptr && Emitter.IsSet()) ? World->Resolve(Emitter) : nullptr;
	if (Entity == nullptr)
	{
		return;
	}
	for (FEmitterCall& Call : EmitterCalls)
	{
		if (Call.AttachEntity == Emitter)
		{
			Call.bStopped = true;
		}
	}
	if (IElysiumEmbodiment* Embodiment = World != nullptr ? World->Embodiment() : nullptr)
	{
		FElysiumEffectHandle Effect;
		for (const FEmitterCall& Call : EmitterCalls)
		{
			if (Call.AttachEntity == Emitter && Call.EffectId != INDEX_NONE)
			{
				Effect.Id = Call.EffectId;
			}
		}
		if (Effect.IsValid())
		{
			Embodiment->StopParticleRoot(Effect);
		}
	}
	// `thunk_FUN_100fbbb0(entity, 0.1)`. There is no deferred-removal service here; the removal is
	// recorded with the recovered delay named, and the entity is not killed early.
	(void)EmitterFadeSeconds;
	RemovedEntities.Add(Emitter);
}

void FElysiumNpc::RemoveNamedEntity(const FElysiumEntityHandle& Entity)
{
	// SEAM for `thunk_FUN_101cd970` / `thunk_FUN_101cd940` (`UTIL_Remove`). Recorded, and the
	// entity killed where it resolves.
	RemovedEntities.Add(Entity);
	if (World != nullptr && Entity.IsSet())
	{
		if (FElysiumEntity* Resolved = World->Resolve(Entity))
		{
			Resolved->Kill();
		}
	}
}

int32 FElysiumNpc::HitboxSetCount() const
{
	// SEAM for `CBaseAnimating::GetModelPtr()->numhitboxsets` (+0x100). No hitbox table reaches the
	// kernel here; 0 takes retail's own "fewer than 7 sets" refusal.
	return 0;
}

// =================================================================================================
// Slot 576 `0x10266630` / slot 577 `0x10266660` — `IsLightDamage` / `IsHeavyDamage`.
// =================================================================================================

// =================================================================================================
// Slot 16 `0x1009b030` — `GetAttackExtents`.
// =================================================================================================

// =================================================================================================
// Slot 154 `0x100b4ea0` — `DamageDecal(int bitsDamageType, int gameMaterial)`.
// Recovered from the LISTING: the decompiler lost the tail call's rewritten arguments.
// =================================================================================================

// =================================================================================================
// Slot 615 `0x102ad0c0` — `CanBeSetOnFire`, and `0x1037c420`, `CNPC_VGhoulCroucher`'s override.
// =================================================================================================

bool FElysiumNpc::CanBeSetOnFire()
{
	// `0x102ad0c0`, the Troika line, two arms and nothing else (`CNPC_VGhoulCroucher` overrides it,
	// story 5 step 3):
	//     if (HasCondition(0x30)) return false;                      // COND_ON_FIRE
	//     return m_flNextBurnTime (+0x65bc) < gpGlobals->curtime;
	// The condition arm returns `uVar1 & 0xffffff00`, i.e. AL = 0 — a body ALREADY on fire refuses.
	if (Cognition.Conditions.Has(EElysiumNpcCond::OnFire))
	{
		return false;
	}
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	return NextBurnTime < Now;
}

// =================================================================================================
// Slot 141 `0x10266780` — `CAI_BaseNPC::TraceAttack`, and its species prologues.
// Argument layout recovered from the LISTING (`RET 0xc`, args at `ESP+0x60/0x64/0x68`): the
// decompiler lost them to `unaff_retaddr` / `unaff_EBP`.
// =================================================================================================

// =================================================================================================
// Slot 146 `0x10268ef0` — `TraceBleed(CVDmg_t*, const Vector&, trace_t*)`.
// SDK 2013's `CBaseEntity::TraceBleed` with this fork's own thresholds; every constant below was
// read out of `.rdata` and matches Valve's published body number for number.
// =================================================================================================

// =================================================================================================
// Slot 392 `0x102664c0` — `CAI_BaseNPC::OnTakeDamage_Dead`.
// Recovered from the LISTING; the decompiled C lost both the vector algebra and the `__ftol`.
// =================================================================================================

// =================================================================================================
// Slot 319 `0x1029fdb0` — `CAI_BaseNPCTroika::PlayerAttackerBlockedReaction`.
// =================================================================================================

bool FElysiumNpc::PlayerAttackerBlockedReaction(FElysiumEntity* Defender, void* InRoll, void* InDmg)
{
	(void)InDmg;
	// 1. `SetIdealActivity(GetBlockReactionActivity(defender))`. The activity is recorded rather
	//    than played — see the seam's note.
	(void)Defender;
	LastBlockedReactionActivity = ElysiumReactions::DefaultBlockedReaction;
	++BlockedReactionActivityCalls;

	// 2. `m_flNextAttack = curtime + lerp(0.5, 1.5, t)` where `t` is 1.0 only when the roll record
	//    exists AND `thunk_FUN_10349830(roll)` answers 2 — retail's own
	//    `(_DAT_1049a1bc - _DAT_1049a1b8) * t + _DAT_1049a1b8`, with `_DAT_1049a1b8` = 0.5 and
	//    `_DAT_1049a1bc` = 1.5 read out of `.rdata`. So an ordinary block re-arms the attacker in
	//    half a second and a dice-roll-type-2 block in one and a half.
	const FElysiumMeleeDiceRollResult* Roll = static_cast<const FElysiumMeleeDiceRollResult*>(InRoll);
	const float T = (Roll != nullptr && Roll->RollType == 2) ? 1.0f : 0.0f;
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	NextAttackTime = Now + static_cast<double>(
		(BlockedReactionLong - BlockedReactionShort) * T + BlockedReactionShort);

	// 3. Retail returns a flat 1.
	return true;
}

// =================================================================================================
// Slot 374 `0x10334180` — `CAI_BaseNPC::GiveAmmo(int count, int ammoIndex, bool suppressSound)`.
// Argument roles recovered from the LISTING (`MOV AL, byte ptr [ESP + 0x54]` is the third).
// =================================================================================================

// =================================================================================================
// Slot 292's species gate — `0x10378cb0` (`CNPC_VGargoyle`) and `0x103802a0`
// (`CNPC_VHengeyokai`), byte-identical bodies.
// =================================================================================================

bool FElysiumNpc::SuppressesDamageFlinch(const FElysiumDmg& Dmg) const
{
	// The Troika line flinches on every hit, as it always has. `CNPC_VGargoyle` (`0x10378cb0`) and
	// `CNPC_VHengeyokai` (`0x103802a0`) override this hook on their C++ classes (story 5 step 3):
	// it is the port's seam for their slot-292 `DamageFlinch` bodies.
	(void)Dmg;
	return false;
}

// =================================================================================================
// `0x102b8c40` — the took-damage arm of `CAI_BaseNPCTroika::SelectSchedule`.
// =================================================================================================

int32 FElysiumNpc::CacheDamagePosition()
{
	// 1. Two conditions, OR'd, and NOTHING happens without one of them.
	if (!Cognition.Conditions.Has(EElysiumNpcCond::LightDamage)
		&& !Cognition.Conditions.Has(EElysiumNpcCond::HeavyDamage))
	{
		return 0;
	}
	// 2. `m_bCondTookDamage = 0` — the latch is spent here, not where the damage landed.
	Cognition.bCondTookDamage = false;
	// 3. `m_vSavePosition = m_vecLastDamageAttackPos`, three words copied straight across.
	SavePosition = BaseMemory.LastDamageAttackPosition;
	// 4. The selector trace (`+0x1b30` file, `+0x1b34` line 0x5f8b) is ABSENT in this runtime's
	//    shape map; the mind transition trace carries the same account.
	(void)TookDamageTraceLine;
	return TookDamageSchedule;
}
