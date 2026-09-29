#include "Substrate/ElysiumNpcVampireBoss.h"

#include "ElysiumDecalSubsystem.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcCombat10Shared.h"
#include "Substrate/ElysiumNpcDamage2Shared.h"
#include "Substrate/ElysiumNpcDamageShared.h"
#include "Substrate/ElysiumNpcLifecycle2_2Shared.h"
#include "Substrate/ElysiumNpcMotor2Shared.h"
#include "Substrate/ElysiumNpcMotorShared.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcSpeciesMisc10_2Shared.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumReactions.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumRetailHullTable.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumWeaponClasses.h"

// --- File-scope helpers moved with this class's bodies (story 5 step 4) ---

namespace
{
	// `_DAT_104ce8c0` — the shared epsilon `GetCurrHealthPercent` guards its DIVISOR with.
	constexpr float GCombatEpsilon = ElysiumNpcTunables::VampireBossSegmentLengthFloor;
	// `_DAT_104454c4` — the shared float zero, and `GetCurrHealthPercent`'s refusal answer.
	constexpr float GCombatZero = ElysiumNpcTunables::Zero;
	// `CVDmg_t::Set(1, 0x40, …)` in `CausePlayerAOEDamage`: family LETHAL, `DMG_BLAST`.
	constexpr int32 AoeDamageFamily = 1;
	constexpr uint32 AoeDamageBits = 0x40u;
	// `CausePlayerAOEDamage`'s three impact sound ids, off the trace-attack result code.
	constexpr int32 AoeSoundDefault = 0x79;
	constexpr int32 AoeSoundOne = 0x7a;
	constexpr int32 AoeSoundThree = 0x7b;
	// `_DAT_104ce8bc` = **2.0** s, the transformation wait.
	constexpr double GTransformWaitSeconds = ElysiumNpcTunables::ProteanTransformWaitAtE8BC;   // `_DAT_104ce8bc` = 2.0 (the boss line's own cell, not the Hengeyokai's)
}

// Slot 420: `0x103c5840`.
void FElysiumNpcVampireBoss::NPCInit()
{
	VampireBossNPCInit();
}

// --- Moved from `ElysiumNpcCombat10.cpp` (story 5 step 4) ---

float FElysiumNpcVampireBoss::GetCurrHealthPercent() const
{
	// `103c68db`: the type-0 list, then `GetValue(0x0f)` FIRST — the numerator, the wound counter.
	const int32 Wounds = TypedStatValue(NpcKernelCombat10Shared::GStatListTypeSheet, NpcKernelCombat10Shared::GStatWounds);
	// `103c694d`: the same walk again, then `GetValue(0x11)` SECOND — the denominator, the cap.
	const int32 Cap = TypedStatValue(NpcKernelCombat10Shared::GStatListTypeSheet, NpcKernelCombat10Shared::GStatMaxHealth);
	// `103c6964`: `ABS((float)cap)` against `_DAT_104ce8c0`. The decompiler's
	// `(a < eps) == (a == eps)` idiom is true exactly when both are false, i.e. `a > eps` — so this
	// is a divide-by-zero guard on the DIVISOR and not a test on the numerator.
	if (FMath::Abs(static_cast<float>(Cap)) > GCombatEpsilon)
	{
		return static_cast<float>(Wounds) / static_cast<float>(Cap);
	}
	return GCombatZero;   // `_DAT_104454c4`
}

// --- Moved from `ElysiumNpcDamage.cpp` (story 5 step 4) ---

int32 FElysiumNpcVampireBoss::AoeTraceAttackResultCode(const FElysiumEntity* Victim) const
{
	// SEAM for the victim's own vtable `+0x50c`, whose answer picks the AOE impact sound. No body
	// here; 0 selects the default id `0x79`.
	(void)Victim;
	return 0;
}

bool FElysiumNpcVampireBoss::LastAttackTimeElapsed(float ThresholdSeconds) const
{
	// `elapsed = curtime - m_flLastAttackTime (+0x5d9c); return threshold < elapsed;`
	// STRICTLY greater — the listing's second `FCOMP` returns 0 in the low byte on equality.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	const double Elapsed = Now - LastAttackTime;
	return static_cast<double>(ThresholdSeconds) < Elapsed;
}

// --- Moved from `ElysiumNpcDamage2.cpp` (story 5 step 4) ---

void FElysiumNpcVampireBoss::ClearBodyEmitterNames()
{
	// All four, unconditionally, no loop in retail — four stores.
	for (int32 i = 0; i < 4; ++i)
	{
		BodyEmitterNames[i].Empty();
	}
}

void FElysiumNpcVampireBoss::SetBodyEmitterName(int32 Region, const FString& Name)
{
	// One store. Retail performs NO bound check on `param_1`; the port refuses out of range rather
	// than writing past a four-element array, which is the one stated divergence in this body.
	if (Region < 0 || Region >= 4)
	{
		return;
	}
	BodyEmitterNames[Region] = Name;
}

int32 FElysiumNpcVampireBoss::SpawnBodyEmitter(int32 Region, const FElysiumEntityHandle& AttachTo)
{
	// Two refusals first, in retail's order: a null attach entity, then an unset name.
	if (!AttachTo.IsSet())
	{
		return INDEX_NONE;
	}
	if (Region < 0 || Region >= 4 || BodyEmitterNames[Region].IsEmpty())
	{
		return INDEX_NONE;
	}
	// `+0x3cc(this, 1)` for region 3 — a ONE-SHOT on the boss itself — and `+0x3cc(this, 2, attach)`
	// for every other region. **Retail never calls `+0x3c4` here**, unlike every other emitter body
	// in this family, so the root is created and attached but not started.
	const bool bOneShot = (Region == 3);
	return CreateNamedEmitter(BodyEmitterNames[Region], Origin / ElysiumMove::U,
		bOneShot ? 1 : 2, bOneShot ? FElysiumEntityHandle() : AttachTo, nullptr);
}

void FElysiumNpcVampireBoss::KillBodyEmitters()
{
	// Four iterations, each: resolve, and only on a live handle call `+0x3c8` then the 0.1 s fade.
	// The handles are NOT cleared — a second call walks the same four words again.
	for (int32 i = 0; i < 4; ++i)
	{
		KillNamedEmitter(ParticleEmitters[i]);
	}
}

void FElysiumNpcVampireBoss::CausePlayerAOEDamage(const FVector& CentreUnits, float RadiusUnits)
{
	// 1. `m_hClosestPlayer` must resolve. Everything else is inside that guard.
	FElysiumEntity* Player = (World != nullptr && Senses.Memory.ClosestPlayer.IsSet())
		? World->Resolve(Senses.Memory.ClosestPlayer) : nullptr;
	if (Player == nullptr)
	{
		return;
	}

	// 2. `delta = player->GetAbsOrigin() - centre`, and its LENGTH — not its square — must be
	//    STRICTLY less than the radius (`thunk_FUN_101371d0` is `VectorLength`).
	const FVector Delta = Player->Origin / ElysiumMove::U - CentreUnits;
	const float Distance = static_cast<float>(Delta.Size());
	if (!(Distance < RadiusUnits))
	{
		return;
	}

	// 3. A world-only trace from the centre to the player (`CTraceFilterWorldOnly`, mask 1). Its
	//    RESULT is handed to `DispatchTraceAttack` as the hit record; retail never tests it.
	FElysiumEntityHandle Blocker;
	if (IElysiumEmbodiment* Embodiment = World != nullptr ? World->Embodiment() : nullptr)
	{
		Embodiment->TracePlayerSolid(CentreUnits * ElysiumMove::U, Player->Origin, Handle, Blocker);
	}

	// 4. `CVDmg_t::Set(1, 0x40, (int)|delta|^2)` — family LETHAL, `DMG_BLAST`, and the damage input
	//    is the SQUARED distance truncated to an integer. That is retail's own arithmetic: the
	//    further the player is inside the radius, the HARDER the blast hits. Ported verbatim.
	FElysiumDmg Dmg;
	Dmg.Family = static_cast<EElysiumDmgFamily>(AoeDamageFamily);
	Dmg.DmgMask = AoeDamageBits;
	Dmg.BaseDamage = static_cast<int32>(Delta.SizeSquared());
	Dmg.Source = Handle;

	// 5. `CBaseEntity::DispatchTraceAttack(player, &info, &delta, &trace)`.
	if (FElysiumCombatCharacter* Victim = Player->AsCombatCharacter())
	{
		Victim->TakeDamage(Dmg, this);
	}

	// 6. The impact sound, chosen off the player's own `+0x50c` answer and played through `+0x500`:
	//    0x79 by default, 0x7a on 1 and 0x7b on 3. Neither slot has a body in this substrate, so the
	//    id is computed and recorded — the CHOICE is the recovered half.
	int32 SoundId = AoeSoundDefault;
	const int32 ResultCode = AoeTraceAttackResultCode(Player);
	if (ResultCode == 1)
	{
		SoundId = AoeSoundOne;
	}
	else if (ResultCode == 3)
	{
		SoundId = AoeSoundThree;
	}
	AoeImpactSounds.Add(SoundId);
}

// --- Moved from `ElysiumNpcLifecycle19_2.cpp` (story 5 step 4) ---

void FElysiumNpcVampireBoss::VampireBossNPCInit()
{
	TroikaNPCInit();
	VampireBossMonsterModelName.Reset();
	ClearBodyEmitterNames();
	VampireBossMonsterClassname = TEXT("npc_VVampireBoss");
	bJumping = false;
	RecordHealthPercent();                                               // 103c6a00
	JumpGravity = 1.f;
}

// --- Moved from `ElysiumNpcMotor.cpp` (story 5 step 4) ---

void FElysiumNpcVampireBoss::CommitSetupJump()
{
	// `thunk_FUN_102c4e80(this)` — the commit every `SetupJump`/`SetupSuperJump` ends on, which
	// takes the three jump words just written and starts the leap. It is RunTask19's `JumpCommit`
	// (one seam for the one call; story 8 L05 integration): recorded, starts nothing.
	JumpCommit();
}

// --- Moved from `ElysiumNpcKernelMotor2.cpp` (story 5 step 4) ---

void FElysiumNpcVampireBoss::SetupJumpRise(float Enabled, float Rise)
{
	// `CNPC_VAsianVampire::SetupJump` `0x10361a70` and `CNPC_VSheriffMan::SetupJump` `0x103b1300`
	// are the same 284 bytes apart from their rise constant. Neither is a vtable slot: each class's
	// own task code calls its own (story 5 step 3 split the port's class test into the two names):
	//
	//     if (param_1 == 0.0) return;
	//     m_vJumpOrigin = GetAbsOrigin();
	//     m_vJumpTarget = m_pHintNode->GetAbsOrigin();
	//     float top = max(m_pHintNode->GetAbsOrigin().z, GetAbsOrigin().z);
	//     m_fJumpHeight = rise + (top - GetAbsOrigin().z);
	//     thunk_FUN_102c4e80(this);
	//
	// Unlike `SetupSuperJump` there is no `ABS`/split arm and no condition clear.
	if (Enabled == 0.0f)
	{
		return;
	}
	const FVector SelfUnits = NpcKernelMotor2Shared::MotorTailSourceOf(Origin);
	FVector HintUnits = FVector::ZeroVector;
	if (!NavHintNodeOrigin(BaseScheduleHost.HintNode, HintUnits))
	{
		// **SEAM**, as in `SetupSuperJump`: no hint origins.
		return;
	}
	JumpOrigin = SelfUnits;
	JumpTarget = HintUnits;
	float Top = static_cast<float>(HintUnits.Z);
	if (Top < static_cast<float>(SelfUnits.Z))
	{
		Top = static_cast<float>(SelfUnits.Z);
	}
	JumpHeight = Rise + (Top - static_cast<float>(SelfUnits.Z));
	CommitSetupJump();
}

// --- Moved from `ElysiumNpcSpeciesMisc10_2.cpp` (story 5 step 4) ---

bool FElysiumNpcVampireBoss::AttackIdleLongerThan(float Seconds) const
{
	// `0x103c67f0`: `curtime - m_flLastAttackTime (+0x5d9c) > Seconds`, STRICTLY greater.
	return (NpcKernelSpeciesMisc10_2Shared::SpeciesMisc10_2Now(*this) - LastAttackTime) > static_cast<double>(Seconds);
}

void FElysiumNpcVampireBoss::WaitForTransformation()
{
	// `103c6407`: nothing until `curtime` passes `m_flProteanTransformStartTime + _DAT_104ce8bc`
	// (**2.0** s), strictly.
	if (!(ProteanTransformStartTime + GTransformWaitSeconds < NpcKernelSpeciesMisc10_2Shared::SpeciesMisc10_2Now(*this)))
	{
		return;
	}
	// `103c6490`: `CVStatList_t::Set(stat 0x0f, 0)` on the type-0 (Attributes) list — a FULL HEAL,
	// because `0x0f` is the wound counter and not current health.
	TypedStatSet(/*ListType*/ 0, NpcKernelSpeciesMisc10_2Shared::GStatWounds, 0);
	// `103c64c4`: resolve `m_hTransformPartner`, `RTDynamicCast` it, and fire the partner's `+0x6664`
	// output with THIS as both activator and caller. The cast refuses anything that is not on the
	// boss line: the tree's typed test.
	if (FElysiumEntity* Partner = World != nullptr ? World->Resolve(TransformPartner) : nullptr)
	{
		FElysiumNpc* PartnerNpc = Partner->AsNpc();
		if (FElysiumNpcVampireBoss* PartnerBoss =
			PartnerNpc != nullptr ? PartnerNpc->AsSpecies<FElysiumNpcVampireBoss>() : nullptr)
		{
			PartnerBoss->FireOutput(TEXT("OnTransformComplete"), Handle);
		}
	}
	// `103c6515`: `TaskComplete(0)` — `0x10273e80` returns untouched while `COND 0x5c TASK_FAILED`
	// stands, so a failed task is not completed by this.
	TaskComplete(/*bIgnoreTaskFailed=*/false);
}

void FElysiumNpcVampireBoss::RecordHealthPercent()
{
	// `0x103c6a00`: one write.
	BossHealthPercentRecord = GetCurrHealthPercent();
}

float FElysiumNpcVampireBoss::HealthPercentLostSinceRecord() const
{
	// `0x103c6a20`: `GetCurrHealthPercent() - m_HealthPercentRecord`, in that order. The percent is
	// `wounds / cap` and RISES with damage, so a body that has lost health answers POSITIVE —
	// the checklist's walk says negative and is wrong (standing fact one).
	return GetCurrHealthPercent() - BossHealthPercentRecord;
}

const TCHAR* FElysiumNpcVampireBoss::VampireBossEmitterAttachment(int32 Region)
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

void FElysiumNpcVampireBoss::VampireBossSpawnBodyEmitters()
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
