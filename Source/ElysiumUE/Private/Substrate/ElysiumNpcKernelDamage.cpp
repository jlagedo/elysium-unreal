#include "Substrate/ElysiumNpc.h"

#include "ElysiumDecalSubsystem.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumRng.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumReactions.h"

// Story 29c-1, family **Damage** — damage, death and the effects the two spawn. The declarations,
// the two retail packets and the family's four standing facts are
// `Substrate/ElysiumNpcKernelDamage.inl`; the walked prose is `docs/vtmb/combat-and-damage.md`
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
// throw and emitter bodies are `Substrate/ElysiumNpcKernelDamage2.cpp`.

namespace
{
	// Retail's `.rdata`, one line per constant. Distances are SOURCE units.
	constexpr float DamageZero = ElysiumNpcTunables::Zero;
	constexpr double DamageBleedFloor = ElysiumNpcTunables::OneDouble;
	constexpr float GearDamage = ElysiumNpcTunables::Hundredth;   // SDK 2013's HITGROUP_GEAR damage
	constexpr float HeavyDamageThreshold = 20.0f; // _DAT_1044eb0c — `IsHeavyDamage`'s only number
	constexpr float BleedNoiseLowDamage = 10.0f;  // _DAT_1044e664
	constexpr float BleedNoiseHighDamage = 25.0f; // _DAT_10462994
	constexpr float BleedDirFlip = -1.0f;         // _DAT_104492dc — `vecDir * -1`
	constexpr float BleedTraceReach = -172.0f;    // _DAT_10499514 — SDK 2013's own -172
	constexpr float BlockedReactionShort = 0.5f;  // _DAT_1049a1b8
	constexpr float BlockedReactionLong = 1.5f;   // _DAT_1049a1bc
	constexpr double DeadDamageScale = ElysiumNpcTunables::TenthDouble;
	constexpr float DeadImpulseZDrop = 10.0f;     // _DAT_1044e664, reused as a Z offset
	constexpr float EmitterFadeSeconds = 0.1f;    // `thunk_FUN_100fbbb0`'s second argument

	// `CAI_BaseNPC::TraceAttack`'s own bit masks and ids.
	constexpr uint32 DmgShock = 0x100u;           // the one bit that suppresses the bleed
	constexpr int32 HitGroupGeneric = 0;
	constexpr int32 HitGroupHead = 1;
	constexpr int32 HitGroupGear = 10;
	// `CNPC_Bullseye::TraceAttack`'s two spawnflags.
	constexpr uint32 BullseyeOwnerOnlyFlag = 0x40000u;
	constexpr uint32 BullseyeDeadPreCallFlag = 0x80000u;
	// The two bits `CNPC_VSheriffMan::KillSheriff` raises on its active weapon.
	constexpr uint32 EffectNoDraw = 0x20u;        // m_fEffects |= 0x20
	constexpr uint32 SolidNotSolid = 0x4u;        // AddSolidFlags(4)
	// `CAI_BaseNPC::GiveAmmo`'s cue and its parameters.
	constexpr const TCHAR* AmmoPickupSound = TEXT("weapons/misc/ammo_pickup.wav");
	constexpr float AmmoPickupVolume = 1.0f;      // 0x3f800000
	constexpr float AmmoPickupAttenuation = 0.8f; // 0x3f4ccccd
	constexpr int32 AmmoPickupPitch = 100;        // 0x64
	constexpr int32 AmmoSlotCount = 0x20;         // the `param_2 < 0x20` bound
	// `CAI_BaseNPC::OnTakeDamage_Dead`'s bit test.
	constexpr uint32 DeadDamageBits = 0xe1u;
	// `0x102b8c40`'s schedule id and its selector-trace line.
	constexpr int32 TookDamageSchedule = 0x8a;
	constexpr int32 TookDamageTraceLine = 0x5f8b;
	// `CNPC_VAndreiBlood::SelectIdealState`'s trace tag. The base writes 1, `CNPC_VAnimal` 5,
	// `CNPC_VHengeyokai` 0x13 and `CNPC_VHunter` 0x17 (`docs/vtmb/npc-kernel/layout.md` +0x1b38).
	constexpr int32 AndreiIdealStateSelector = 4;

	// The combined `DMG_` bits every body in this family reads: the packet's own word OR'd with the
	// descriptor's `m_bdmgTypes` when there is a descriptor, and the packet's alone when there is
	// not. Retail spells this three times; it is one rule.
	uint32 CombinedDamageBits(const FElysiumNpc::FElysiumTakeDamageInfo& Info)
	{
		return Info.Dmg != nullptr ? (Info.Dmg->DmgMask | Info.DamageBits) : Info.DamageBits;
	}

	// The magnitude every body in this family reads: `CVDmg_t::GetDmg()` when there is a descriptor,
	// `m_flDamage` when there is not.
	float DamageMagnitude(const FElysiumNpc::FElysiumTakeDamageInfo& Info)
	{
		return Info.Dmg != nullptr ? static_cast<float>(Info.Dmg->GetDmg()) : Info.Damage;
	}

	// `_DAT_1070ba40`/`44`/`48`. ONE per level, as retail's file-static triple is.
	FVector GDeathThrowImpulse = FVector::ZeroVector;
}

FVector& FElysiumNpc::DeathThrowImpulse()
{
	return GDeathThrowImpulse;
}

void FElysiumNpc::ResetDeathThrowImpulse()
{
	// `DAT_1070d1b0`/`b4`/`b8` — `vec3_origin`, the value `OnTakeDamage_Dead` seeds its local with
	// and leaves in place when the packet carries no attacker.
	GDeathThrowImpulse = FVector::ZeroVector;
}

// =================================================================================================
// The seams. Each answers nothing, or goes through this runtime's own service, and names the retail
// call it stands for.
// =================================================================================================

float FElysiumNpc::HitGroupDamageScaleCvar(int32 HitGroup) const
{
	// SEAM for `DAT_109201ac` / `DAT_1090fe74` / `DAT_109204f4` / `DAT_1090fdc4` / `DAT_1092023c`
	// — SDK 2013's `sk_npc_head` / `_chest` / `_stomach` / `_arm` / `_leg`. All five pointer cells
	// live past `.data`'s raw size and no corpus function constructs them, so their names and
	// defaults are UNRECOVERED. Retail's inlined read is
	// `if (cvar->IsCommand()) 0.0f else cvar->m_flValue`; an unconstructed cvar answers 0.0f.
	(void)HitGroup;
	return DamageZero;
}

bool FElysiumNpc::TraceAttackEvadeCheck(const FElysiumDmg* Dmg) const
{
	// SEAM for `CVDmg_t::EvadeCheck` (`0x10012783`). This is not an absence: `combat-and-damage.md`
	// § "`CVDmg_t`: the 17-word damage descriptor" records that **the installed generic EvadeCheck
	// callback returns zero** and that real defense lives in the ranged/melee attack code. False is
	// the recovered answer.
	(void)Dmg;
	return false;
}

void FElysiumNpc::SpawnBlood(const FVector& PositionUnits, int32 BloodColor, float Damage)
{
	// SEAM for `thunk_FUN_102699e0` — `SpawnBlood(ptr->endpos, BloodColor(), damage)`. Recorded;
	// there is no authored blood spray root to hand the particle seam yet.
	FSpawnBloodCall Call;
	Call.PositionUnits = PositionUnits;
	Call.BloodColor = BloodColor;
	Call.Damage = Damage;
	SpawnBloodCalls.Add(Call);
}

void FElysiumNpc::AddMultiDamage(const FElysiumTakeDamageInfo& SubInfo)
{
	// SEAM for `thunk_FUN_101c2d20(subInfo, this)` — SDK 2013's `AddMultiDamage`. This runtime
	// commits through `FElysiumCombatCharacter::TakeDamage`/`CommitDamage` rather than a per-frame
	// accumulator, so the packet is recorded and nothing is spent here.
	MultiDamageAccumulator.Add(SubInfo);
}

int32 FElysiumNpc::AmmoMaxCarry(int32 AmmoIndex) const
{
	// SEAM for `thunk_FUN_10427620` — `GetAmmoDef()->MaxCarry(index)`. UNRECOVERED: this runtime's
	// reserve is keyed by the authored ammo TYPE NAME and no table joins retail's 0..31 index to it.
	(void)AmmoIndex;
	return 0;
}

FString FElysiumNpc::AmmoTypeNameForIndex(int32 AmmoIndex) const
{
	// SEAM for the same `CAmmoDef` index order. UNRECOVERED; answers the empty name, which is what
	// closes `GiveAmmo`'s clamp.
	(void)AmmoIndex;
	return FString();
}

bool FElysiumNpc::GameRulesAllowsAmmo(int32 AmmoIndex) const
{
	// SEAM for `(*g_pGameRules)->vtable+0xd4`, `GiveAmmo`'s first gate. No rules object here carries
	// it; TRUE is the permissive arm, so the capacity clamp below is what decides.
	(void)AmmoIndex;
	return true;
}

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

bool FElysiumNpc::TestOneHitbox(int32 HitboxSetIndex, const FVector& RayStartUnits,
	const FVector& RayEndUnits, uint32 Mask) const
{
	// SEAM for `thunk_FUN_10399ef0(ray, mask, trace, studiohdr, hitboxset, bonecache)`.
	(void)HitboxSetIndex;
	(void)RayStartUnits;
	(void)RayEndUnits;
	(void)Mask;
	return false;
}

bool FElysiumNpc::CandidateIsStandable(const FElysiumEntity* Candidate) const
{
	// SEAM for slot 164 `IsStandable` on ANOTHER entity. `CBaseEntity::IsStandable`
	// (`0x100b50a0`) is four lines and every one of them reads a word this substrate does not
	// carry:
	//     if (GetSolidFlags() & 0x10) return false;                  // FSOLID_NOT_STANDABLE
	//     int s = GetSolid();                                        // +0x170
	//     if (s == 1 || s == 6 || s == 2) return true;               // BSP, VPHYSICS, BBOX
	//     return IsBSPModel();                                       // thunk_FUN_100b5110
	// There is no solid TYPE and no solid FLAG word on `FElysiumEntity`, so this answers TRUE —
	// the arm a solid body takes, and the permissive one for the single caller below. It does NOT
	// dispatch slot 164, which is another family's generated stub: asking a stub would make this
	// body's answer a function of when that story lands rather than of what retail reads.
	(void)Candidate;
	return true;
}

float FElysiumNpc::MingXiaoThrowCvar(int32 Which) const
{
	// SEAM for `DAT_1093bbcc` (0, the quadratic term), `DAT_1093bc14` (1, the constant term) and
	// `DAT_1093b9fc` (2, the Z term) of `0x103990c0`'s throw speed. All three pointer cells live
	// past `.data`'s raw size and no corpus function constructs them: UNRECOVERED, all answer 0.0f.
	(void)Which;
	return DamageZero;
}

int32 FElysiumNpc::ZombieGibAmmoTypeCvar(int32 Which) const
{
	// SEAM for `DAT_10940404` (0) and `DAT_1094044c` (1) — `CNPC_VZombie::TraceAttack`'s two
	// cvar-backed forced ammo types. UNRECOVERED; both answer 0, an unconstructed cvar's own answer.
	(void)Which;
	return 0;
}

bool FElysiumNpc::HasPlayerControllerObject() const
{
	// SEAM for `this->vtable+0x184` — `CNPC_VPlayerController`'s stored controller object. No word
	// of this substrate stands for it; null takes the guarded arm of all four species bodies.
	return false;
}

const TCHAR* FElysiumNpc::TookLifeEventSource()
{
	return TEXT("CNPC_VPlayerController::Event_TookLife");
}

const TCHAR* FElysiumNpc::SummonEmitterBoneName()
{
	return TEXT("Bip01_R_Hand");
}

void FElysiumNpc::HideAndUnsolidifyWeapon(const FElysiumEntityHandle& Weapon)
{
	// SEAM for `m_fEffects |= 0x20` (EF_NODRAW), `AddSolidFlags(4)` (FSOLID_NOT_SOLID) and
	// `CBaseEntity::Relink` on the weapon. No `FElysiumEntity` word stands for either mask.
	FHideAndUnsolidifyCall Call;
	Call.Entity = Weapon;
	Call.EffectBits = EffectNoDraw;
	Call.SolidBits = SolidNotSolid;
	HideAndUnsolidifyCalls.Add(Call);
}

int32 FElysiumNpc::AoeTraceAttackResultCode(const FElysiumEntity* Victim) const
{
	// SEAM for the victim's own vtable `+0x50c`, whose answer picks the AOE impact sound. No body
	// here; 0 selects the default id `0x79`.
	(void)Victim;
	return 0;
}

// =================================================================================================
// Slot 576 `0x10266630` / slot 577 `0x10266660` — `IsLightDamage` / `IsHeavyDamage`.
// =================================================================================================

bool FElysiumNpc::IsLightDamage(float Damage, int32 DamageBits)
{
	// 0x10266630, the whole body: `return 0.0f < damage`. STRICTLY greater — a zero-damage hit is
	// not light damage. The `int` second argument (retail's `bitsDamageType`) is not read.
	(void)DamageBits;
	return DamageZero < Damage;
}

bool FElysiumNpc::IsHeavyDamage(float Damage, int32 DamageBits)
{
	// 0x10266660, the whole body: `return 20.0f < damage` (`_DAT_1044eb0c`). STRICTLY greater, so a
	// hit measuring exactly 20 is light and not heavy. SDK 2013's `CAI_BaseNPC::IsHeavyDamage`
	// returns a flat false; this fork's threshold is the divergence and it is the recovered fact.
	(void)DamageBits;
	return HeavyDamageThreshold < Damage;
}

// =================================================================================================
// Slot 16 `0x1009b030` — `GetAttackExtents`.
// =================================================================================================

FVector FElysiumNpc::GetAttackExtents()
{
	// The whole body: copy `m_vecAttackExtents` (+0x50/+0x54/+0x58) into the out-parameter. The
	// setter's other half — `CBaseEntity::SetAttackExtents` (`0x1009af40`), which also pushes the
	// margin at the collision partition — is already `FElysiumNpc::SetAttackExtents`, so this reads
	// the word that one writes. This runtime carries the margin in CENTIMETRES.
	return ScheduleHost.AttackExtentsCm;
}

// =================================================================================================
// Slot 154 `0x100b4ea0` — `DamageDecal(int bitsDamageType, int gameMaterial)`.
// Recovered from the LISTING: the decompiler lost the tail call's rewritten arguments.
// =================================================================================================

int32 FElysiumNpc::DamageDecal(int32 DamageBits, int32 GameMaterial)
{
	// Arm 1: `m_nRenderMode == kRenderTransAlpha (4)` answers -1 — no decal at all.
	if (RenderMode == 4)
	{
		return INDEX_NONE;
	}
	// Arm 2: any other non-normal render mode, on glass (`0x47` = `'G'`), answers the fixed decal
	// index `0x34`.
	if (RenderMode != 0 && GameMaterial == 0x47)
	{
		return 0x34;
	}
	// Arm 3 is a TAIL CALL that rewrites its own two stack arguments to `(0, 4)` before jumping —
	//     100b4eca  MOV dword ptr [ESP + 0x8],0x4
	//     100b4ed2  MOV dword ptr [ESP + 0x4],0x0
	//     100b4edc  JMP dword ptr [EAX + 0x8]
	// through `*DAT_1070b244` slot 2, which is `IUniformRandomStream::RandomInt`: the same object
	// whose slot 1 (`+0x4`) `TraceBleed` draws its float spread from. So the ordinary answer is a
	// uniform decal index in `[0, 4]` and neither argument reaches it.
	(void)DamageBits;
	return ElysiumRng::Stream(EElysiumRngStream::Effects).RandRange(0, 4);
}

// =================================================================================================
// Slot 615 `0x102ad0c0` — `CanBeSetOnFire`, and `0x1037c420`, `CNPC_VGhoulCroucher`'s override.
// =================================================================================================

const FElysiumNpc::FCanBeSetOnFireSpecies* FElysiumNpc::CanBeSetOnFireSpeciesRows(int32& OutCount)
{
	// `CNPC_VGhoulCroucher` is the ONLY class in the family tree that replaces the Troika body:
	// `vtmb_slot 615` lists `CAI_BaseNPCTroika#615` plus 55 inheritors on `0x102ad0c0` and this one
	// on `0x1037c420`.
	static const FCanBeSetOnFireSpecies Rows[] = {
		{ TEXT("CNPC_VGhoulCroucher"), TEXT("0x1037c420"), /*bRefusesWhileSpawnBurning*/ true },
	};
	OutCount = UE_ARRAY_COUNT(Rows);
	return Rows;
}

const FElysiumNpc::FCanBeSetOnFireSpecies* FElysiumNpc::CanBeSetOnFireSpeciesOf(
	const TCHAR* InRetailClass)
{
	int32 Count = 0;
	const FCanBeSetOnFireSpecies* Rows = CanBeSetOnFireSpeciesRows(Count);
	const FElysiumNpcClass* Cls = ElysiumNpcKernelClass::Find(InRetailClass);
	for (int32 i = 0; i < Count; ++i)
	{
		if (ElysiumNpcKernelClass::DerivesFrom(Cls, Rows[i].RetailClass))
		{
			return &Rows[i];
		}
	}
	return nullptr;
}

bool FElysiumNpc::CanBeSetOnFire()
{
	// `0x1037c420` first: the species arm runs BEFORE the base body and refuses outright while the
	// authored `on_fire` keyfield (`m_bSpawnBurning`, +0x6665) is set — a croucher that spawned
	// burning cannot be set on fire again. Anything else falls into `thunk_FUN_102ad0c0`.
	const FCanBeSetOnFireSpecies* Row = CanBeSetOnFireSpeciesOf(
		RetailClass() != nullptr ? RetailClass()->Name : nullptr);
	if (Row != nullptr && Row->bRefusesWhileSpawnBurning && bGhoulSpawnBurning)
	{
		return false;
	}

	// `0x102ad0c0`, the Troika line, two arms and nothing else:
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
// Slot 141 `0x10266780` — `CAI_BaseNPC::TraceAttack`, and its three species prologues.
// Argument layout recovered from the LISTING (`RET 0xc`, args at `ESP+0x60/0x64/0x68`): the
// decompiler lost them to `unaff_retaddr` / `unaff_EBP`.
// =================================================================================================

const FElysiumNpc::FTraceAttackSpecies* FElysiumNpc::TraceAttackSpeciesRows(int32& OutCount)
{
	static const FTraceAttackSpecies Rows[] = {
		{ TEXT("CNPC_Bullseye"), TEXT("0x10356f60"), ETraceAttackPrologue::BullseyeGate },
		{ TEXT("CNPC_VWerewolf"), TEXT("0x103ccbf0"), ETraceAttackPrologue::ZeroAmmoType },
		{ TEXT("CNPC_VZombie"), TEXT("0x103e0430"), ETraceAttackPrologue::ZombieGib },
	};
	OutCount = UE_ARRAY_COUNT(Rows);
	return Rows;
}

const FElysiumNpc::FTraceAttackSpecies* FElysiumNpc::TraceAttackSpeciesOf(
	const TCHAR* InRetailClass)
{
	int32 Count = 0;
	const FTraceAttackSpecies* Rows = TraceAttackSpeciesRows(Count);
	const FElysiumNpcClass* Cls = ElysiumNpcKernelClass::Find(InRetailClass);
	for (int32 i = 0; i < Count; ++i)
	{
		if (ElysiumNpcKernelClass::DerivesFrom(Cls, Rows[i].RetailClass))
		{
			return &Rows[i];
		}
	}
	return nullptr;
}

const FElysiumNpc::FTraceAttackSpecies* FElysiumNpc::TraceAttackSpecies() const
{
	const FElysiumNpcClass* Cls = RetailClass();
	return Cls != nullptr ? TraceAttackSpeciesOf(Cls->Name) : nullptr;
}

bool FElysiumNpc::ZombieTraceAttackPrologue(int32 HitGroup, bool bAttackerWeaponIsMelee,
	int32 FirstCvarAmmoType, int32 SecondCvarAmmoType, bool& OutShouldGib, int32& OutAmmoType)
{
	// 0x103e0430, verbatim. Both cvars are read FIRST, unconditionally, before either arm:
	//     first  = DAT_10940404->IsCommand() ? 0 : DAT_10940404->m_nValue;
	//     second = DAT_1094044c->IsCommand() ? 0 : DAT_1094044c->m_nValue;
	// then
	//     if (trace->hitgroup == 1) { shouldGib = 1; forced = second; }
	//     else { shouldGib = 0;
	//            if (!attacker || !attacker->activeWeapon) return;      // no force at all
	//            forced = first;                                        // the swap is in the test
	//            if ((activeWeapon->GetCapabilities() & 0x18000) == 0) return; }
	//     SetDamageType(info, forced);
	// Note the ORDER of the else arm: `iStack_4 = iStack_8` (second := first) is executed as part of
	// the capability test's own expression, so the FIRST cvar is what a qualifying melee hit forces
	// and the SECOND is what a head hit forces.
	OutShouldGib = (HitGroup == HitGroupHead);
	if (OutShouldGib)
	{
		OutAmmoType = SecondCvarAmmoType;
		return true;
	}
	if (!bAttackerWeaponIsMelee)
	{
		return false;
	}
	OutAmmoType = FirstCvarAmmoType;
	return true;
}

void FElysiumNpc::TraceAttack(void* InInfo, const FVector& DirUnits, void* InTrace)
{
	FElysiumTakeDamageInfo* Info = static_cast<FElysiumTakeDamageInfo*>(InInfo);
	FElysiumTraceHit* Trace = static_cast<FElysiumTraceHit*>(InTrace);
	if (Info == nullptr || Trace == nullptr)
	{
		return;   // the port's one refusal: retail would have dereferenced both
	}

	// --- The species prologues, ahead of the Troika body -----------------------------------------
	const FTraceAttackSpecies* Species = TraceAttackSpecies();
	if (Species != nullptr)
	{
		switch (Species->Prologue)
		{
		case ETraceAttackPrologue::BullseyeGate:
		{
			// 0x10356f60. Spawnflag 0x40000: the hit is only taken when the vtable `+0x94`/`+0x29c`
			// chain off `info[0xb]` (`m_hInflictor`'s owner) reports THIS entity as its owner —
			// `param_1[0xb]+0x94` null returns immediately. Neither word has a source in this
			// substrate (`m_hInflictor`'s owner chain), so the gate takes its REFUSAL arm and the
			// hit is dropped, which is the conservative half of retail's own two answers.
			if ((SpawnFlags & BullseyeOwnerOnlyFlag) != 0)
			{
				return;
			}
			// Spawnflag 0x80000 with `m_takedamage == 0` runs slot 146 (`TraceBleed`) on the
			// packet's own descriptor FIRST, and then falls through into the base body anyway.
			if ((SpawnFlags & BullseyeDeadPreCallFlag) != 0 && TakeDamageMode == 0)
			{
				TraceBleed(Info->Dmg, DirUnits, Trace);
			}
			break;
		}
		case ETraceAttackPrologue::ZeroAmmoType:
			// 0x103ccbf0: `thunk_FUN_101c2a50(info, 0)` — zero the packet's damage-type word — then
			// the base body. Nothing else at all.
			Info->DamageBits = 0;
			break;
		case ETraceAttackPrologue::ZombieGib:
		{
			// 0x103e0430. `m_hAttacker`'s active weapon's capability mask is the melee test.
			const FElysiumEntity* Attacker =
				(World != nullptr && Info->Attacker.IsSet()) ? World->Resolve(Info->Attacker) : nullptr;
			const FElysiumCombatCharacter* AttackerChar =
				Attacker != nullptr ? Attacker->AsCombatCharacter() : nullptr;
			// SEAM: the weapon capability mask (`+0x5a0`) is story 29d's. Retail's test is
			// `(weapon->GetCapabilities() & 0x18000) != 0` — the same melee-block capability the
			// player block resolver uses (`docs/vtmb/combat-and-damage.md` -> "Weapon and input
			// surface"). Without that accessor the melee arm cannot open, so a non-head hit forces
			// no ammo type, which is retail's own `goto LAB_103e04dc`.
			const bool bMelee = AttackerChar != nullptr && false;
			bool bShouldGib = false;
			int32 Forced = 0;
			if (ZombieTraceAttackPrologue(Trace->HitGroup, bMelee, ZombieGibAmmoTypeCvar(0),
					ZombieGibAmmoTypeCvar(1), bShouldGib, Forced))
			{
				Info->DamageBits = static_cast<uint32>(Forced);
			}
			bZombieShouldGib = bShouldGib;
			break;
		}
		case ETraceAttackPrologue::None:
		default:
			break;
		}
	}

	// --- `0x10266780`, arm by arm ----------------------------------------------------------------

	// 1. `m_fNoDamageDecal = false` runs BEFORE the `m_takedamage` test, so a body that takes no
	//    damage still has the flag cleared.
	bNoDamageDecal = false;
	if (TakeDamageMode == 0)
	{
		return;
	}

	// 2. `CVDmg_t::EvadeCheck(info->m_pDmg, engine->IndexOfEdict(m_pPev))`. A true answer returns
	//    with nothing written.
	if (TraceAttackEvadeCheck(Info->Dmg))
	{
		return;
	}

	// 3. The 0x4c-byte sub-packet copy — `CTakeDamageInfo subInfo = info` — `REP MOVSD` of 0x13
	//    dwords. Everything after this reads the COPY, and the original is what the flinch gets.
	FElysiumTakeDamageInfo SubInfo = *Info;

	// 4. `SetLastHitGroup(ptr->hitgroup)` and `m_nForceBone = (short)ptr->physicsbone`, in that
	//    order. The bone is sign-extended from a `short` (`MOVSX ECX, word ptr [ESI + 0x48]`).
	LastHitGroup = Trace->HitGroup;
	ForceBone = static_cast<int32>(static_cast<int16>(Trace->PhysicsBone));

	// 5. `CVDmg_t::Apply(info->m_pDmg, victimIndex, info.GetAttacker())` — the ONE damage
	//    resolution, whose integer answer seeds the local magnitude. It runs unconditionally, before
	//    the hitgroup switch, and it is the number the health commit later spends.
	float Magnitude = 0.f;
	if (Info->Dmg != nullptr)
	{
		FElysiumCombatCharacter* Attacker = nullptr;
		if (World != nullptr && Info->Attacker.IsSet())
		{
			if (FElysiumEntity* Ent = World->Resolve(Info->Attacker))
			{
				Attacker = Ent->AsCombatCharacter();
			}
		}
		ElysiumDamage::Apply(*Info->Dmg, Attacker, *this,
			FElysiumDamageContext::FromCharacter(*this));
		Magnitude = static_cast<float>(Info->Dmg->CommittedDamage());
	}

	// 6. The hitgroup switch. **It scales the LOCAL float only** — nothing here is written back into
	//    either packet, which is the family's first standing fact. The jump table at `0x10266a24`
	//    is indexed by `hitgroup - 1` over ten entries:
	//        1 head, 2 chest, 3 stomach, 4/5 arms, 6/7 legs -> cvar * apply
	//        10 gear -> the flat 0.01 constant, AND `ptr->hitgroup = 0`
	//        anything else -> the apply result unchanged
	switch (Trace->HitGroup)
	{
	case 1:
	case 2:
	case 3:
	case 4:
	case 5:
	case 6:
	case 7:
		Magnitude *= HitGroupDamageScaleCvar(Trace->HitGroup);
		break;
	case HitGroupGear:
		// `FSTP ST0` discards the apply result entirely before loading the constant.
		Magnitude = GearDamage;
		Trace->HitGroup = HitGroupGeneric;
		break;
	default:
		break;
	}

	// 7. From here the value tested is the SUB-PACKET's own `GetDmg()` when it carries a descriptor,
	//    and the scaled local float only when it does not — `MOV ECX,[ESP+0x10]; TEST ECX,ECX`.
	const bool bHasDescriptor = SubInfo.Dmg != nullptr;
	const double Tested = bHasDescriptor ? static_cast<double>(SubInfo.Dmg->GetDmg())
										 : static_cast<double>(Magnitude);

	// 8. `subInfo.GetDamage() >= 1.0` (a DOUBLE at `0x10449280`) and no `DMG_SHOCK`. Failing either
	//    skips straight to the flinch — and, crucially, leaves `m_fNoDamageDecal` FALSE, because
	//    retail's raise of that flag is on the SURVIVED-HEADSHOT arm and on nothing else.
	if (Tested >= DamageBleedFloor && (CombinedDamageBits(SubInfo) & DmgShock) == 0)
	{
		// 9. The survived-headshot arm: a head hit the victim lives through raises
		//    `m_fNoDamageDecal` and skips the blood, the bleed AND the flinch is still run after.
		//    The comparison is `m_iHealth - damage > 0` read off `TEST AH,0x41 / JNP`.
		bool bSkipBleed = false;
		if (Trace->HitGroup == HitGroupHead)
		{
			const double Survived = static_cast<double>(Health) - Tested;
			if (Survived > 0.0)
			{
				bNoDamageDecal = true;
				bSkipBleed = true;
			}
		}

		if (!bSkipBleed)
		{
			// 10. `SpawnBlood(ptr->endpos, BloodColor(), damage)` then
			//     `TraceBleed(subInfo.m_pDmg, vecDir, ptr)`. The colour is slot 145 on THIS body.
			const float Amount = static_cast<float>(Tested);
			SpawnBlood(Trace->EndPosUnits, BloodColor(), Amount);
			TraceBleed(SubInfo.Dmg, DirUnits, Trace);
		}
	}

	// 11. `DamageFlinch(info, vecDir, ptr)` — slot 292, and it takes the ORIGINAL packet, not the
	//     sub-packet. It runs on EVERY path past the `m_takedamage`/evade gates, including the
	//     under-1.0 one and the survived-headshot one.
	DamageFlinch(Info, DirUnits, Trace);

	// 12. The tail: `subInfo.SetAmmoType(ptr->+0x50)`, `subInfo.+0x28 = info.+0x28`, then
	//     `AddMultiDamage(subInfo, this)`.
	SubInfo.AmmoType = Trace->AmmoType;
	SubInfo.Attacker = Info->Attacker;
	AddMultiDamage(SubInfo);
}

// =================================================================================================
// Slot 146 `0x10268ef0` — `TraceBleed(CVDmg_t*, const Vector&, trace_t*)`.
// SDK 2013's `CBaseEntity::TraceBleed` with this fork's own thresholds; every constant below was
// read out of `.rdata` and matches Valve's published body number for number.
// =================================================================================================

void FElysiumNpc::TraceBleed(void* InDmg, const FVector& DirUnits, void* InTrace)
{
	const FElysiumDmg* Dmg = static_cast<const FElysiumDmg*>(InDmg);
	const FElysiumTraceHit* Trace = static_cast<const FElysiumTraceHit*>(InTrace);
	if (Dmg == nullptr || Trace == nullptr)
	{
		return;
	}

	// 1. `BloodColor()` (slot 145) gates the whole body: `DONT_BLEED` (-1) and `BLOOD_COLOR_MECH`
	//    (0x14) both refuse. Retail asks it TWICE — once per comparison — and again at the decal.
	const int32 Color = BloodColor();
	if (Color == INDEX_NONE || Color == 0x14)
	{
		return;
	}

	// 2. The descriptor's word 1 — its AUTHORED base damage, not the applied result — is what the
	//    noise table is keyed on (`(float)*(int *)(param_1 + 4)`), and a zero refuses.
	const float BaseDamage = static_cast<float>(Dmg->BaseDamage);
	if (BaseDamage == DamageZero)
	{
		return;
	}

	// 3. `(m_bdmgTypes & 0xc7) != 0` — DMG_CRUSH|DMG_BULLET|DMG_SLASH|DMG_BLAST|DMG_CLUB. Only the
	//    LOW BYTE is tested (`*(byte *)(param_1 + 0x10) & 199`), so `DMG_AIRBOAT` and every bit
	//    above 8 is excluded, exactly as SDK 2013's mask is.
	if ((Dmg->DmgMask & 0xc7u) == 0)
	{
		return;
	}

	// 4. This fork's own addition to the SDK body: a per-entity decal BUDGET. Slot 158 answering
	//    false makes the body spend one unit of `+0x208` (`m_iMaxHealth`'s offset in Ghidra's
	//    struct) and refuse outright once it is exhausted. No word of this substrate stands for that
	//    counter and slot 158's meaning is UNRECOVERED; the seam is the slot answering TRUE, which
	//    is the arm that spends nothing.
	// (nothing to do on the true arm — recorded here so the budget is not rediscovered)

	// 5. The noise/count table, read off `_DAT_1044e664` = 10.0 and `_DAT_10462994` = 25.0 and the
	//    three denormal float bit patterns the decompiler printed for the integer counts (1, 2, 4):
	//        damage <  10 -> noise 0.1, 1 trace
	//        damage <  25 -> noise 0.2, 2 traces
	//        else         -> noise 0.3, 4 traces
	float Noise = 0.1f;
	int32 Count = 1;
	if (BaseDamage >= BleedNoiseLowDamage)
	{
		if (BaseDamage >= BleedNoiseHighDamage)
		{
			Noise = 0.3f;
			Count = 4;
		}
		else
		{
			Noise = 0.2f;
			Count = 2;
		}
	}

	FTraceBleedPass Pass;
	Pass.Noise = Noise;
	Pass.TraceCount = Count;

	FRandomStream& Rng = ElysiumRng::Stream(EElysiumRngStream::Effects);
	IElysiumEmbodiment* Embodiment = World != nullptr ? World->Embodiment() : nullptr;
	for (int32 i = 0; i < Count; ++i)
	{
		// 6. `vecTraceDir = vecDir * -1` (`_DAT_104492dc`) jittered by three independent
		//    `RandomFloat(-noise, +noise)` draws — three draws per trace, in X, Y, Z order.
		FVector TraceDir = DirUnits * BleedDirFlip;
		TraceDir.X += Rng.FRandRange(-Noise, Noise);
		TraceDir.Y += Rng.FRandRange(-Noise, Noise);
		TraceDir.Z += Rng.FRandRange(-Noise, Noise);

		// 7. The trace runs from `ptr->endpos` to `endpos + traceDir * -172` (`_DAT_10499514`), mask
		//    `0x400b` — `MASK_SOLID_BRUSHONLY` with `CONTENTS_GRATE` cleared, which is what keeps
		//    blood off a grate. The NEGATIVE reach with the already-flipped direction is Valve's own
		//    double negation and lands the trace behind the victim.
		const FVector StartUnits = Trace->EndPosUnits;
		const FVector EndUnits = StartUnits + TraceDir * BleedTraceReach;
		Pass.LastStartUnits = StartUnits;
		Pass.LastEndUnits = EndUnits;

		// 8. A trace whose fraction is not 1.0 paints `UTIL_BloodDecalTrace(&tr, BloodColor())`.
		//    The port's decal seam takes a direction and a range rather than a hit, so the same
		//    segment is handed to it; headless it answers false, which is a miss.
		if (Embodiment != nullptr)
		{
			const FVector Direction = (EndUnits - StartUnits).GetSafeNormal();
			const float RangeCm = static_cast<float>((EndUnits - StartUnits).Size()) * ElysiumMove::U;
			Embodiment->LayShotImpactDecal(StartUnits * ElysiumMove::U, Direction, RangeCm,
				ElysiumRng::Stream(EElysiumRngStream::Effects).RandRange(
					1, ElysiumImpactDecals::PoolSize));
		}
	}
	TraceBleedPasses.Add(Pass);
}

// =================================================================================================
// Slot 392 `0x102664c0` — `CAI_BaseNPC::OnTakeDamage_Dead`.
// Recovered from the LISTING; the decompiled C lost both the vector algebra and the `__ftol`.
// =================================================================================================

int32 FElysiumNpc::OnTakeDamage_Dead(void* InInfo)
{
	FElysiumTakeDamageInfo* Info = static_cast<FElysiumTakeDamageInfo*>(InInfo);
	if (Info == nullptr)
	{
		return 1;   // retail answers a flat 1 on every path
	}

	// 1. The global death-throw impulse. Seeded from `vec3_origin` (`DAT_1070d1b0..b8`) and only
	//    overwritten when `info.m_hAttacker` (+0x28) is live:
	//        Vector a = attacker->WorldSpaceCenter();   // slot 192, +0x300
	//        a.z -= 10.0;                               // _DAT_1044e664
	//        Vector b = this->WorldSpaceCenter();
	//        impulse = a - b;  VectorNormalize(impulse);   // 0x1057966c, length discarded
	//        _DAT_1070ba40/44/48 = impulse;
	//    It is a FILE-STATIC triple, so there is ONE death-throw impulse per level shared by every
	//    body, not one per NPC — the same shape family Bosses recorded for the ManBat watchdog. The
	//    port carries it as such.
	const FElysiumEntity* Attacker =
		(World != nullptr && Info->Attacker.IsSet()) ? World->Resolve(Info->Attacker) : nullptr;
	if (Attacker != nullptr)
	{
		FVector A = Attacker->Origin / ElysiumMove::U;
		A.Z -= DeadImpulseZDrop;
		const FVector B = Origin / ElysiumMove::U;
		DeathThrowImpulse() = (A - B).GetSafeNormal();
	}

	// 2. `(m_bdmgTypes | m_bitsDamageType) & 0xe1` — the low-byte test: DMG_CRUSH|DMG_BURN|
	//    DMG_CLUB|DMG_SLASH's high pair, i.e. `1 | 0x20 | 0x40 | 0x80`. `TEST AL,0xe1` is a BYTE
	//    test, so nothing above bit 7 can open this arm.
	if ((CombinedDamageBits(*Info) & DeadDamageBits) == 0)
	{
		return 1;
	}
	// 3. `m_takedamage != 1` — a body set to DAMAGE_EVENTS_ONLY takes nothing here.
	if (TakeDamageMode == 1)
	{
		return 1;
	}

	// 4. `m_iHealth = (int)(m_iHealth - GetDmg() * 0.1)` — `_DAT_104493d0`, a DOUBLE 0.1. A corpse
	//    takes a TENTH of the incoming damage into its engine-space health, which is what later
	//    drives the gib threshold; the sheet's own damage counter is untouched.
	const double Amount = static_cast<double>(DamageMagnitude(*Info));
	Health = static_cast<int32>(static_cast<double>(Health) - Amount * DeadDamageScale);
	return 1;
}

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

int32 FElysiumNpc::GiveAmmo(int32 Count, int32 AmmoIndex, bool bSuppressSound)
{
	// Five gates, in retail's order, each answering 0:
	//   count <= 0;  !g_pGameRules->+0xd4(this, index);  index < 0;  index >= 0x20;
	//   and finally the clamp itself.
	if (Count <= 0)
	{
		return 0;
	}
	if (!GameRulesAllowsAmmo(AmmoIndex))
	{
		return 0;
	}
	if (AmmoIndex < 0 || AmmoIndex >= AmmoSlotCount)
	{
		return 0;
	}

	// `room = GetAmmoDef()->MaxCarry(index) - m_iAmmo[index]`, then `add = min(count, room)`, and
	// `add < 1` answers 0 BEFORE the sound. So a full pool is silent as well as fruitless.
	const FString AmmoName = AmmoTypeNameForIndex(AmmoIndex);
	const int32 Held = AmmoName.IsEmpty() ? 0 : Inventory.Reserve(AmmoName);
	const int32 Room = AmmoMaxCarry(AmmoIndex) - Held;
	const int32 Add = FMath::Min(Count, Room);
	if (Add < 1)
	{
		return 0;
	}

	// The cue, only when the third argument is clear. `CPASAttenuationFilter` at this body's ear
	// position, channel 3 (`CHAN_ITEM`), volume 1.0, attenuation 0.8, flags 0, pitch 100.
	if (!bSuppressSound)
	{
		if (IElysiumAudio* Audio = World != nullptr ? World->Audio() : nullptr)
		{
			FElysiumBodySound Sound;
			Sound.Rel = AmmoPickupSound;
			Sound.Volume = AmmoPickupVolume;
			Sound.Pitch = 1.0f;   // retail's 100 is the engine's "unmodified" pitch
			Sound.Channel = EElysiumSoundChannel::Item;
			Audio->PlayBodySound(Handle, Sound);
		}
		(void)AmmoPickupAttenuation;
		(void)AmmoPickupPitch;
	}

	// `m_iAmmo[index] += add; return add;`
	if (!AmmoName.IsEmpty())
	{
		Inventory.AddReserve(AmmoName, Add);
	}
	return Add;
}

// =================================================================================================
// Slot 292's species gate — `0x10378cb0` (`CNPC_VGargoyle`) and `0x103802a0`
// (`CNPC_VHengeyokai`), byte-identical bodies.
// =================================================================================================

const FElysiumNpc::FDamageFlinchSpecies* FElysiumNpc::DamageFlinchSpeciesRows(int32& OutCount)
{
	// `vtmb_slot 292` lists 254 classes; exactly two of them replace
	// `CBaseCombatCharacter::DamageFlinch` with a gate, and both use the same mask.
	static const FDamageFlinchSpecies Rows[] = {
		{ TEXT("CNPC_VGargoyle"), TEXT("0x10378cb0"), ElysiumDamage::FirearmMask },
		{ TEXT("CNPC_VHengeyokai"), TEXT("0x103802a0"), ElysiumDamage::FirearmMask },
	};
	OutCount = UE_ARRAY_COUNT(Rows);
	return Rows;
}

const FElysiumNpc::FDamageFlinchSpecies* FElysiumNpc::DamageFlinchSpeciesOf(
	const TCHAR* InRetailClass)
{
	int32 Count = 0;
	const FDamageFlinchSpecies* Rows = DamageFlinchSpeciesRows(Count);
	const FElysiumNpcClass* Cls = ElysiumNpcKernelClass::Find(InRetailClass);
	for (int32 i = 0; i < Count; ++i)
	{
		if (ElysiumNpcKernelClass::DerivesFrom(Cls, Rows[i].RetailClass))
		{
			return &Rows[i];
		}
	}
	return nullptr;
}

bool FElysiumNpc::DamageFlinchSuppressed(uint32 CombinedBits, float Magnitude, uint32 SuppressMask)
{
	// The two bodies, verbatim:
	//     bits = dmg ? (dmg->m_bdmgTypes | info[0xe]) : info[0xe];
	//     if ((bits & 0x4000002) != 0) return;                      // no flinch at all
	//     mag = dmg ? dmg->GetDmg() : (float)info[0xc];
	//     if (mag != 0.0f) CBaseCombatCharacter::DamageFlinch(...);
	// `0x4000002` is `DMG_BULLET | DMG_BUCKSHOT`, which is exactly `ElysiumDamage::FirearmMask`:
	// a Gargoyle and a Hengeyokai do not flinch from gunfire. The magnitude test is an EXACT
	// inequality against 0.0, not a threshold.
	if ((CombinedBits & SuppressMask) != 0)
	{
		return true;
	}
	return Magnitude == DamageZero;
}

bool FElysiumNpc::SuppressesDamageFlinch(const FElysiumDmg& Dmg) const
{
	const FElysiumNpcClass* Cls = RetailClass();
	const FDamageFlinchSpecies* Row = Cls != nullptr ? DamageFlinchSpeciesOf(Cls->Name) : nullptr;
	if (Row == nullptr)
	{
		return false;   // the Troika line flinches on every hit, as it always has
	}
	// The port's commit path hands the flinch a resolved descriptor rather than a packet, so the
	// combined bits are the descriptor's own and the magnitude is the committed damage — which is
	// what `CVDmg_t::GetDmg` answers once `Apply` has run.
	return DamageFlinchSuppressed(Dmg.DmgMask, static_cast<float>(Dmg.GetDmg()), Row->SuppressMask);
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
	SavePosition = Senses.Memory.LastDamageAttackPosition;
	// 4. The selector trace (`+0x1b30` file, `+0x1b34` line 0x5f8b) is ABSENT in this runtime's
	//    shape map; the mind transition trace carries the same account.
	(void)TookDamageTraceLine;
	return TookDamageSchedule;
}

// =================================================================================================
// `0x1035d150` — `CNPC_VAndreiBlood::SelectIdealState` (slot 461).
// =================================================================================================

EElysiumNpcState FElysiumNpc::CNPC_VAndreiBlood_vfunc461()
{
	// Twenty-five bytes: stamp the trace selector, then answer 1 or 2.
	//     this->field_0x1b38 = 4;
	//     return (m_bActivated != 0) + 1;
	// Retail's `NPC_STATE_IDLE` is 1 and `NPC_STATE_ALERT` is 2.
	SelectIdealStateSelector = AndreiIdealStateSelector;
	return bAndreiActivated ? EElysiumNpcState::Alert : EElysiumNpcState::Idle;
}

// =================================================================================================
// `0x103c67f0` — `CNPC_VVampireBoss`'s attack-recency test.
// =================================================================================================

bool FElysiumNpc::LastAttackTimeElapsed(float ThresholdSeconds) const
{
	// `elapsed = curtime - m_flLastAttackTime (+0x5d9c); return threshold < elapsed;`
	// STRICTLY greater — the listing's second `FCOMP` returns 0 in the low byte on equality.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	const double Elapsed = Now - LastAttackTime;
	return static_cast<double>(ThresholdSeconds) < Elapsed;
}
