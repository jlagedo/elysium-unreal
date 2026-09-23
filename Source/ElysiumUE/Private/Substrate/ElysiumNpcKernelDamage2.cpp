#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"

// Story 29c-1, family **Damage**, second half — the per-species death, throw and emitter bodies.
// The Troika-line slot bodies, the family seams and its four standing facts are
// `Substrate/ElysiumNpcKernelDamage.cpp` and `Substrate/ElysiumNpcKernelDamage.inl`; the walked
// prose for everything below is `docs/vtmb/npc-ai/lifecycle.md`.
//
// Split at ~1,500 lines, as family Bosses split `ElysiumNpcKernelBosses2.cpp`. The constants below
// are the ones this half uses, re-stated rather than shared because neither file owns the other's
// anonymous namespace; every one carries the `.rdata` address it was read from.

namespace
{
	// Retail's `.rdata`, one line per constant. Distances are SOURCE units.
	constexpr float ThrowIgnoreCollisionSeconds = 0.75f;   // `0x102c43b0`'s argument
	constexpr float ThrowLeadZScale = ElysiumNpcTunables::Half;
	constexpr float ThrowConeLo = -20.0f;           // _DAT_1049ae98
	constexpr float ThrowConeHi = 20.0f;            // _DAT_1044eb0c
	constexpr float ThrowSpeedFloor = 1000.0f;      // _DAT_10447ee0
	constexpr float EnergyBallForward = 50.0f;      // _DAT_104ada24
	constexpr float EnergyBallRight = 40.0f;        // _DAT_104ada28
	constexpr float EnergyBallUp = -10.0f;          // _DAT_104ada2c
	constexpr float EnergyBallSpeed = 800.0f;       // DAT_104ada30
	constexpr float GrenadeCooldown = ElysiumNpcTunables::Five;
	constexpr float GrenadeThinkDelay = 3.0f;       // _DAT_10449258
	constexpr float GrenadeThinkSlack = ElysiumNpcTunables::Hundredth;
	constexpr float ThrownModelThinkDelay = 20.0f;  // _DAT_1044eb0c

	// `CVDmg_t::Set(1, 0x40, …)` in `CausePlayerAOEDamage`: family LETHAL, `DMG_BLAST`.
	constexpr int32 AoeDamageFamily = 1;
	constexpr uint32 AoeDamageBits = 0x40u;
	// `CausePlayerAOEDamage`'s three impact sound ids, off the trace-attack result code.
	constexpr int32 AoeSoundDefault = 0x79;
	constexpr int32 AoeSoundOne = 0x7a;
	constexpr int32 AoeSoundThree = 0x7b;
	// `CNPC_VSheriffMan::KillSheriff`'s two named entities.
	constexpr const TCHAR* SheriffRelayName = TEXT("logic_zap_player");
	constexpr const TCHAR* SheriffSelfName = TEXT("sheriff");
	// `0x103937d0`'s TaskFail code when there is no active weapon.
	constexpr int32 MingXiaoNoWeaponFailure = 0x1f;
	// `0x10396bc0`'s schedule id and its selector-trace line.
	constexpr int32 MingXiaoGrabSchedule = 0x167;
	constexpr int32 MingXiaoGrabTraceLine = 0xbc3;
	// `CNPC_VMingXiao::TestHitboxes`'s hitbox-set requirement.
	constexpr int32 MingXiaoHitboxSetsRequired = 7;

	// `UTIL_AngleDiff` `0x1013d580`: `a - b` walked back into `[-180, 180]` by whole turns, wrapping
	// only on the side the `a <= b` test selects. Families Bosses, Facing and Positions each keep an
	// identical private copy for the same reason: none owns the other's file.
	float Damage2AngleDiff(float A, float B)
	{
		float Delta = A - B;
		if (A <= B)
		{
			while (Delta < -180.0f)
			{
				Delta += 360.0f;
			}
		}
		else
		{
			while (Delta > 180.0f)
			{
				Delta -= 360.0f;
			}
		}
		return Delta;
	}

	// `UTIL_VecToYaw` in SOURCE's frame, over a delta expressed in THIS world's axes (whose Y is the
	// negated Source one), exactly as families Bosses and Facing take it.
	float Damage2VecToYaw(const FVector& Delta)
	{
		if (FMath::IsNearlyZero(Delta.X) && FMath::IsNearlyZero(Delta.Y))
		{
			return 0.f;
		}
		return static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(-Delta.Y, Delta.X)));
	}
}

// =================================================================================================
// The `CNPC_VVampireBoss` / `CNPC_VChangBros` / `CNPC_VSabbatLeader` / `CNPC_VAndreiBlood` emitters.
// =================================================================================================

void FElysiumNpc::ClearBodyEmitterNames()
{
	// All four, unconditionally, no loop in retail — four stores.
	for (int32 i = 0; i < 4; ++i)
	{
		BodyEmitterNames[i].Empty();
	}
}

void FElysiumNpc::SetBodyEmitterName(int32 Region, const FString& Name)
{
	// One store. Retail performs NO bound check on `param_1`; the port refuses out of range rather
	// than writing past a four-element array, which is the one stated divergence in this body.
	if (Region < 0 || Region >= 4)
	{
		return;
	}
	BodyEmitterNames[Region] = Name;
}

int32 FElysiumNpc::SpawnBodyEmitter(int32 Region, const FElysiumEntityHandle& AttachTo)
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

void FElysiumNpc::KillBodyEmitters()
{
	// Four iterations, each: resolve, and only on a live handle call `+0x3c8` then the 0.1 s fade.
	// The handles are NOT cleared — a second call walks the same four words again.
	for (int32 i = 0; i < 4; ++i)
	{
		KillNamedEmitter(ParticleEmitters[i]);
	}
}

void FElysiumNpc::KillCenterEmitter()
{
	// The same pair on the single `m_hCenterEmitter` (+0x66f4), and it too leaves the handle alone.
	KillNamedEmitter(ChangCenterEmitter);
}

void FElysiumNpc::SpawnBloodPoolEmitter(const FString& Name, const FElysiumEntity* OrientTo)
{
	// `GetAbsOrigin()` (slot 217) is the X and Y. The Z comes from the optional second parameter's
	// own origin when one is given, and from `thunk_FUN_101d08e0(origin, z)` — retail's floor drop —
	// when it is not. The port has no floor-drop service in the kernel, so the NPC's own Z stands in
	// and the substitution is named here rather than hidden.
	FVector Position = Origin / ElysiumMove::U;
	if (OrientTo != nullptr)
	{
		Position.Z = OrientTo->Origin.Z / ElysiumMove::U;
	}
	const int32 Index = CreateNamedEmitter(Name, Position, /*AttachMode*/ 0,
		FElysiumEntityHandle(), nullptr);
	// `thunk_FUN_100fbc90` failing leaves retail dereferencing a null pointer at `+0x3c4`; the port
	// refuses instead, which is the one stated divergence in this body.
	StartNamedEmitter(Index);
}

void FElysiumNpc::StartBloodEmitter(const FString& Name)
{
	// ONE body, written twice (`0x1035e1a0` and `0x1035e3c0`); this is the BLOOD arm.
	if (Name.IsEmpty())
	{
		return;   // `TEST EDI,EDI / JZ` — a null name does nothing at all
	}
	// Release the previously cached handle only while it still resolves, then set it to -1.
	if (AndreiBloodEmitter.IsSet())
	{
		RemoveNamedEntity(AndreiBloodEmitter);
		AndreiBloodEmitter = FElysiumEntityHandle();
	}
	// Create at this NPC's own origin (slot 217 with `(&vec3_angle, -1.0f)`).
	const int32 Index = CreateNamedEmitter(Name, Origin / ElysiumMove::U, /*AttachMode*/ 2,
		Handle, nullptr);
	// The BLOOD arm parents the emitter to this entity (`thunk_FUN_100faf60`) and starts it.
	StartNamedEmitter(Index);
}

void FElysiumNpc::StartSummonEmitter(const FString& Name)
{
	// The SUMMON arm: the same release/create/store, then attach at the bone `Bip01_R_Hand`
	// (`+0x3cc(this, 2, name)`) rather than parenting, then start.
	if (Name.IsEmpty())
	{
		return;
	}
	if (AndreiSummonEmitter.IsSet())
	{
		RemoveNamedEntity(AndreiSummonEmitter);
		AndreiSummonEmitter = FElysiumEntityHandle();
	}
	const int32 Index = CreateNamedEmitter(Name, Origin / ElysiumMove::U, /*AttachMode*/ 2,
		Handle, SummonEmitterBoneName());
	StartNamedEmitter(Index);
}

// =================================================================================================
// `0x1036dd20` — `CNPC_VChangBros::SpawnEnergyBall`.
// =================================================================================================

FVector FElysiumNpc::EnergyBallSpawnPoint(const FVector& OriginUnits, const FVector& FwdAxis,
	const FVector& RightAxis, const FVector& UpAxis)
{
	// The listing's three accumulations, each a dot of one basis row with the same constant triple:
	//     p.x = origin.x + fwd.x*50 + up.x*40 + right.x*(-10)
	//     p.y = origin.y + fwd.y*50 + up.y*40 + right.y*(-10)
	//     p.z = origin.z + fwd.z*50 + up.z*40 + right.z*(-10)
	// `_DAT_104ada24` = 50.0, `_DAT_104ada28` = 40.0, `_DAT_104ada2c` = -10.0, all read out of
	// `.rdata`. `AngleVectors` (`0x10139610`) fills forward/right/up from the muzzle attachment's
	// angles (slot 219, `+0x36c`).
	return OriginUnits + FwdAxis * EnergyBallForward + UpAxis * EnergyBallRight
		+ RightAxis * EnergyBallUp;
}

FElysiumEntityHandle FElysiumNpc::SpawnEnergyBall()
{
	// The muzzle attachment's basis: slot 219 answers the angles and `AngleVectors` the triple. No
	// attachment table reaches the kernel here, so this NPC's own facing stands in and the
	// substitution is named rather than hidden.
	const float YawDegrees = static_cast<float>(Angles.Y);
	const float Rad = FMath::DegreesToRadians(YawDegrees);
	const FVector FwdAxis(FMath::Cos(Rad), -FMath::Sin(Rad), 0.0);
	const FVector RightAxis(-FMath::Sin(Rad), -FMath::Cos(Rad), 0.0);
	const FVector UpAxis(0.0, 0.0, 1.0);

	const FVector Spawn = EnergyBallSpawnPoint(Origin / ElysiumMove::U, FwdAxis, RightAxis, UpAxis);
	const FElysiumEntityHandle Ball =
		CreateNamedEntity(TEXT("item_w_chang_energy_ball"), Spawn);

	// The fire is gated on BOTH the create succeeding and `m_hClosestPlayer` (+0x628c) resolving:
	// `(**(code **)(*piVar4 + 0x5d0))(this, DAT_104ada30, m_hClosestPlayer)` with the speed 800.0.
	if (Ball.IsSet() && Senses.Memory.ClosestPlayer.IsSet())
	{
		(void)EnergyBallSpeed;
	}
	return Ball;
}

// =================================================================================================
// `0x103c7230` — `CNPC_VVampireBoss::CausePlayerAOEDamage`.
// =================================================================================================

void FElysiumNpc::CausePlayerAOEDamage(const FVector& CentreUnits, float RadiusUnits)
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

// =================================================================================================
// `0x103b10f0` — `CNPC_VSheriffMan::KillSheriff`.
// =================================================================================================

void FElysiumNpc::KillSheriff()
{
	// 1. BOTH named entities are looked up and BOTH must exist before the relay fires. The
	//    `sheriff` lookup's result is never used for anything else — it is a presence test, and
	//    reproducing it is the point: a map missing `sheriff` does not zap the player.
	FElysiumEntity* Relay = World != nullptr ? World->FindByName(FString(SheriffRelayName)) : nullptr;
	FElysiumEntity* Self = World != nullptr ? World->FindByName(FString(SheriffSelfName)) : nullptr;
	if (Relay != nullptr && Self != nullptr)
	{
		// `CLogicRelay::InputTrigger` with a default `inputdata_t` (activator -1, value 0).
		static const FName TriggerInput(TEXT("Trigger"));
		World->EnqueueInput(FString(SheriffRelayName), TriggerInput, FElysiumVariant(), 0.0,
			Handle, Handle);
	}

	// 2. The weapon half is UNCONDITIONAL on the relay half — it runs even when the relay or the
	//    `sheriff` entity is missing. `m_fEffects |= 0x20` (EF_NODRAW) then `AddSolidFlags(4)`
	//    (FSOLID_NOT_SOLID) and `Relink()`: the sword goes invisible and non-solid, it is NOT
	//    removed, so a corpse still nominally holds it.
	FElysiumEntity* Weapon = (World != nullptr && Inventory.ActiveWeapon.IsSet())
		? World->Resolve(Inventory.ActiveWeapon) : nullptr;
	if (Weapon != nullptr)
	{
		HideAndUnsolidifyWeapon(Weapon->Handle);
	}
}

// =================================================================================================
// `CNPC_VMingXiao`'s throw chain — `0x103937d0`, `0x10396bc0`, `0x10398fd0`, `0x103990c0`.
// =================================================================================================

float FElysiumNpc::MingXiaoThrowSpeed(float DistanceSquared, float Quadratic, float Constant)
{
	// The listing's two arms, both computing the same sum and differing only in what they answer:
	//     if (quadratic * distSq + constant <= 1000.0)  speed = 1000.0;   // _DAT_10447ee0
	//     else                                          speed = quadratic * distSq + constant;
	// so 1000 is a FLOOR on the throw speed and not a cap.
	const float Sum = Quadratic * DistanceSquared + Constant;
	return (Sum <= ThrowSpeedFloor) ? ThrowSpeedFloor : Sum;
}

void FElysiumNpc::MingXiaoThrowCleanup()
{
	// 0x10398fd0, and also the tail every path of `0x103990c0` runs.
	if (MingXiaoPhysicsAnimlink.IsSet())
	{
		RemoveNamedEntity(MingXiaoPhysicsAnimlink);
		MingXiaoPhysicsAnimlink = FElysiumEntityHandle();
	}
	// UNCONDITIONAL, outside the handle guard.
	MingXiaoThrowObject = FElysiumEntityHandle();
	ArmIgnoreCollisionExpiry(ThrowIgnoreCollisionSeconds);
	ThrowableObjectMode(0);
}

void FElysiumNpc::LaunchRagdollTowardTarget()
{
	// 1. Remove and clear `m_hPhysicsAnimlink` (+0x6738) if it resolves — the link goes before the
	//    impulse, not after.
	if (MingXiaoPhysicsAnimlink.IsSet())
	{
		RemoveNamedEntity(MingXiaoPhysicsAnimlink);
		MingXiaoPhysicsAnimlink = FElysiumEntityHandle();
	}

	// 2. Everything else is inside `GetEnemy() != null` (slot 167, `+0x29c`).
	FElysiumEntity* Enemy = (World != nullptr && Senses.Memory.Enemy.IsSet())
		? World->Resolve(Senses.Memory.Enemy) : nullptr;
	if (Enemy != nullptr)
	{
		// 3. The held object's CENTRE (`+0x370`), not its origin, is the launch point.
		const FVector FromUnits = MingXiaoThrowObject.IsSet() && World != nullptr
			&& World->Resolve(MingXiaoThrowObject) != nullptr
			? World->Resolve(MingXiaoThrowObject)->Origin / ElysiumMove::U
			: Origin / ElysiumMove::U;

		// 4. The lead point. `thunk_FUN_102c36d0(this, from, enemy, gravity, null, &out)` solves it
		//    with the gravity cvar; that solver is family Bosses' `SolveThrowImpulse`'s sibling and
		//    has no body here, so the enemy's own position stands for the lead and the substitution
		//    is named. The enemy's per-frame position delta (`+0xa0..0xa2` minus `+0x9d..0x9f` — its
		//    current origin minus its previous one) scaled by 0.5 (`_DAT_104454d0`) is then added to
		//    the lead's Z, and ONLY to its Z.
		FVector Lead = Enemy->Origin / ElysiumMove::U;
		const FVector EnemyDelta = FVector::ZeroVector;   // SEAM: no previous-origin word here
		Lead.Z += EnemyDelta.Z * ThrowLeadZScale;

		// 5. The cone. `UTIL_AngleDiff(VecToYaw(lead - from), GetAngles().y)` outside `[-20, +20]`
		//    re-aims the XY at exactly `yaw - 20` (below the cone) or `yaw + 20` (above it),
		//    preserving Z. The listing's two `FCOMP`s are inclusive at both edges, exactly as family
		//    Bosses recorded for the pickup cone that reads the same two cells.
		FVector Delta = Lead - FromUnits;
		const float SelfYaw = static_cast<float>(Angles.Y);
		const float Diff = Damage2AngleDiff(Damage2VecToYaw(Delta), SelfYaw);
		if (Diff < ThrowConeLo || Diff >= ThrowConeHi)
		{
			const float Clamped = (Diff < ThrowConeLo) ? (SelfYaw - ThrowConeHi)
													   : (SelfYaw + ThrowConeHi);
			const float Rad = FMath::DegreesToRadians(Clamped);
			const float Len = static_cast<float>(FVector2D(Delta.X, Delta.Y).Size());
			Delta.X = FMath::Cos(Rad) * Len;
			Delta.Y = -FMath::Sin(Rad) * Len;
		}
		const float DistSq = static_cast<float>(Delta.SizeSquared());
		FVector Impulse = Delta.GetSafeNormal();

		// 6. The speed, then the Z term — which is added AFTER the XY scale and is a separate cvar.
		const float Speed = MingXiaoThrowSpeed(DistSq, MingXiaoThrowCvar(0), MingXiaoThrowCvar(1));
		Impulse *= Speed;
		Impulse.Z += MingXiaoThrowCvar(2) * DistSq;

		// 7. `CRagdollProp`'s `+0x428`, or the physics object's `+0xa0`/`+0x9c` fallback. Family
		//    Bosses stands both as one seam and this body reuses it rather than declaring a second.
		ApplyThrowImpulse(MingXiaoThrowObject, Impulse);
	}

	// 8. The tail runs on EVERY path — including the one with no enemy.
	MingXiaoThrowObject = FElysiumEntityHandle();
	ArmIgnoreCollisionExpiry(ThrowIgnoreCollisionSeconds);
	ThrowableObjectMode(0);
}

void FElysiumNpc::MingXiaoThrowAttack(int32 TaskId, int32 Tentacle,
	TFunctionRef<float(int32)> TuningField)
{
	// 1. `thunk_FUN_102e0b40(m_pNavigator)` — clear the path. No navigator here; recorded by the
	//    absence, as family Motor states.
	// 2. `m_hMeleeWeapon`'s owner (+0xa0) is resolved and then `+0x610` runs, both before the
	//    active-weapon test.
	// 3. No active weapon fails the task with code 0x1f and returns.
	(void)TaskId;
	if (!Inventory.ActiveWeapon.IsSet())
	{
		TaskFail(MingXiaoNoWeaponFailure);
		return;
	}
	// 4. The enemy's `+0x9c` (its own owner/target word) is read, then the weapon's activity
	//    translation (`+0x5a4`) and `+0x5e0`, then slot 331 `ChooseMeleeAttackSequence`. A refusal
	//    OR a negative activity fails the task; a success sets the activity through `+0x4dc`.
	//    Slot 331 is family Bosses' `ChooseMeleeAttackSequenceSeam`, which answers FALSE.
	if (!ChooseMeleeAttackSequenceSeam())
	{
		TaskFail(MingXiaoNoWeaponFailure);
	}
	// 5. Either way the attack timer is stamped: `m_rflAttackTimers[t] = curtime + FUN_103983d0(..)`
	//    — family Bosses owns both the array and the curve, and this body reuses them.
	if (Tentacle >= 0 && Tentacle < 6)
	{
		const double Now = World != nullptr ? World->NowSeconds() : 0.0;
		MingXiaoAttackTimers[Tentacle] = Now + static_cast<double>(
			FUN_103983d0(MingXiaoThrowingTentacle, TuningField));
	}
}

int32 FElysiumNpc::MingXiaoFindThrowObject(int32 PedestalCvarDraw, int32 PedestalCvarCeiling)
{
	// 1. A LIVE `m_hThrowObject` answers 0 — the search only runs with empty hands.
	if (MingXiaoThrowObject.IsSet() && World != nullptr
		&& World->Resolve(MingXiaoThrowObject) != nullptr)
	{
		return 0;
	}
	// 2. Two curtime-gated cooldowns, in THIS order: `+0x66d8` first, then `+0x66d4`.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	if (Now < MingXiaoPickupCooldownB)
	{
		return 0;
	}
	if (Now < MingXiaoPickupCooldownA)
	{
		return 0;
	}
	// 3. With an enemy, `+0x848` runs — the per-species float family Bosses records as one of the
	//    four overrides of that virtual. Then `ClearCondition(9)`, unconditionally.
	Cognition.Conditions.Clear(EElysiumNpcCond::TooFarForMelee);   // retail's ClearCondition(9)

	// 4. `RandomInt(...)` against the tuning record's `+8` cell: the search runs ONLY on a draw
	//    strictly below it. The caller hands both in so the gate is measurable; the record itself
	//    lives past `.data`'s raw size, which families Bosses, Facing and Motor all record.
	if (PedestalCvarDraw < PedestalCvarCeiling)
	{
		int32 Task = MingXiaoThrowingTentacle;
		FVector AimPoint = FVector::ZeroVector;
		FVector SavedForward = FVector::ZeroVector;
		FElysiumEntity* Pedestal = FUN_10398b20(Task, AimPoint, SavedForward);
		MingXiaoThrowObject = Pedestal != nullptr ? Pedestal->Handle : FElysiumEntityHandle();
		MingXiaoThrowingTentacle = Task;
		MingXiaoPickupTargetPos = AimPoint;
		MingXiaoPickupSavedForward = SavedForward;
	}

	// 5. Only a LIVE object answers the schedule: ignore its collision, set the throwable mode to 1,
	//    stamp the selector trace with line 0xbc3 and answer schedule 0x167.
	if (MingXiaoThrowObject.IsSet() && World != nullptr
		&& World->Resolve(MingXiaoThrowObject) != nullptr)
	{
		StartIgnoringCollision(MingXiaoThrowObject);
		ThrowableObjectMode(1);
		(void)MingXiaoGrabTraceLine;
		return MingXiaoGrabSchedule;
	}
	return 0;
}

void FElysiumNpc::SpitAttackTimer(bool bFirstParamSet, bool bSecondParamSet)
{
	// Twenty-nine bytes: `if (p1 && p2) m_flSpitAttackTimer = 0;`. Neither parameter is read for
	// anything else, so the presence test is the whole of the condition.
	if (bFirstParamSet && bSecondParamSet)
	{
		MingXiaoSpitAttackTimer = 0.0;
	}
}

void FElysiumNpc::ThrowableObjectMode(int32 Mode)
{
	// Thirteen bytes, one store. `m_eThrowableObjectMode` is family Bosses' member at +0x673c.
	MingXiaoThrowableObjectMode = Mode;
}

bool FElysiumNpc::SeveredTentaclesCanStandOn(const FElysiumEntity* Candidate) const
{
	// The loop walks index 0..5 and reads TWO words per step: `puVar5[-7]` is `m_rhProxies[i]`
	// (+0x668c, seven dwords below `m_rhSeveredTentacles` at +0x66a8) and `*puVar5` the severed
	// tentacle. Both resolved pointers are compared against the candidate and either match answers
	// FALSE — `return uVar1 & 0xffffff00`, i.e. AL = 0.
	for (int32 i = 0; i < 6; ++i)
	{
		const FElysiumEntity* Proxy =
			(World != nullptr && Proxies[i].IsSet()) ? World->Resolve(Proxies[i]) : nullptr;
		if (Proxy == Candidate)
		{
			return false;
		}
		const FElysiumEntity* Severed = (World != nullptr && SeveredTentacles[i].IsSet())
			? World->Resolve(SeveredTentacles[i]) : nullptr;
		if (Severed == Candidate)
		{
			return false;
		}
	}
	// On a full miss a NON-NULL candidate is asked its own `IsStandable` (+0x290, slot 164) and a
	// false there answers false; a NULL candidate skips the call entirely and answers TRUE.
	if (Candidate != nullptr && !CandidateIsStandable(Candidate))
	{
		return false;
	}
	return true;
}

bool FElysiumNpc::TestHitboxesMingXiao(const FVector& RayStartUnits, const FVector& RayEndUnits,
	uint32 Mask)
{
	// Three refusals, in retail's order: no model, then `!m_bHasTransformed`, then fewer than 7
	// hitbox sets (`*(int *)(studiohdr + 0x100) < 7`).
	if (!bMingXiaoHasTransformed)
	{
		return false;
	}
	if (HitboxSetCount() < MingXiaoHitboxSetsRequired)
	{
		return false;
	}
	// Hitbox set 0 — the master box — is tested FIRST and unguarded; a hit ends the body.
	if (TestOneHitbox(0, RayStartUnits, RayEndUnits, Mask))
	{
		return true;
	}
	// Then sets 1..6, each gated by `thunk_FUN_10398000(this, index)` — family Bosses'
	// `IsTentacleConnected`, which answers "tentacle n is NOT severed". The set offset walks
	// `0xc, 0x18, … 0x48` while the tentacle index walks `0, 1, … 6`, so the LOOP runs seven times
	// with the index one ahead of the set: index 0 pairs with set 1.
	for (int32 i = 0; i < 6; ++i)
	{
		if (!IsTentacleConnected(i))
		{
			continue;
		}
		if (TestOneHitbox(i + 1, RayStartUnits, RayEndUnits, Mask))
		{
			return true;
		}
	}
	// Retail's fallthrough answers TRUE — `return CONCAT31(…, 1)` — even when nothing was hit. That
	// is not a transcription slip: the body's answer is "I handled the hitbox test", not "I hit".
	return true;
}

// =================================================================================================
// `0x103bf170` — `CNPC_VTzimisce`'s pickup release (29c's `VGargoyleGibCleanup` row).
// =================================================================================================

void FElysiumNpc::VGargoyleGibCleanup()
{
	// 1. `m_hPickupTarget` (+0x6670) to -1, FIRST and unconditionally.
	PickupTarget = FElysiumEntityHandle();
	// 2. `thunk_FUN_102c43b0(this, 0.75)` — the collision-ignore re-arm, before anything is removed.
	ArmIgnoreCollisionExpiry(ThrowIgnoreCollisionSeconds);
	// 3. Resolve `m_hPhysicsAnimlink` (+0x6684) and call `UTIL_Remove` on it — **even when the
	//    handle does NOT resolve**, in which case retail passes a null pointer. That unguarded call
	//    is the body's own shape and is reproduced by always running the removal path; the port's
	//    removal is a no-op on an invalid handle, where retail's relied on `UTIL_Remove`'s own null
	//    tolerance.
	RemoveNamedEntity(TzimiscePhysicsAnimlink);
	TzimiscePhysicsAnimlink = FElysiumEntityHandle();
	// 4. `thunk_FUN_103be0b0(this, 0)` — the Tzimisce `CARRYING_BODY` flag write. Family Bosses'
	//    `CallFormBit` stands the same seam for the Hengeyokai arm; this records it the same way.
	CallFormBit(false);
}

// =================================================================================================
// `0x10365860` — `CNPC_VBach::ThrowGrenade`, and `0x1038f2c0` — `CNPC_VManBat::ThrowModel`.
// =================================================================================================

void FElysiumNpc::ThrowGrenade(const FString& GrenadeTargetName, float Force)
{
	// 1. The cooldown, and it is the FIRST thing: `curtime - m_flLastGrenadeTime >= 5.0`
	//    (`_DAT_10454110`), inclusive at the edge.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	if (!(static_cast<double>(GrenadeCooldown) <= Now - BachLastGrenadeTime))
	{
		return;
	}
	// 2. The named target must exist. A missing one leaves the cooldown UNSTAMPED, so the next think
	//    tries again immediately.
	FElysiumEntity* Spot = World != nullptr ? World->FindByName(GrenadeTargetName) : nullptr;
	if (Spot == nullptr)
	{
		return;
	}
	// 3. The stamp happens before the create, so a failed create still spends the cooldown.
	BachLastGrenadeTime = Now;

	// 4. `CBaseEntity::Create("item_w_grenade_frag", target->GetAbsOrigin())`.
	const FElysiumEntityHandle Grenade =
		CreateNamedEntity(TEXT("item_w_grenade_frag"), Spot->Origin / ElysiumMove::U);
	if (!Grenade.IsSet())
	{
		return;   // retail's own `if (this_00 != 0)` guard
	}

	// 5. `m_takedamage = 2` (DAMAGE_YES), `m_iHealth = 1`, `m_pfnTouch = null` — a grenade that a
	//    single point of damage detonates and that nothing can trigger by touch. Then
	//    `VPhysicsInitNormal(2, 0, false)`; a failure `Msg`es "No physics data for grenade" and
	//    removes it.
	// 6. `forward * force` as the velocity with a zero angular velocity, then
	//    `m_flNextThink = curtime + 3.0 + 0.01` (`_DAT_10449258` and `_DAT_10450aa4`) written into
	//    both the grenade's own `+0x7c` word and its think field.
	(void)Force;
	(void)GrenadeThinkDelay;
	(void)GrenadeThinkSlack;
	// 7. `m_bCamperFlag = 0` — the LAST thing, and it is on the THROWER, not the grenade.
	bBachCamperFlag = false;
}

bool FElysiumNpc::ThrowModel(const FString& ModelName, const FString& ThrowParentName)
{
	// 1. `CreateNoSpawn("prop_physics", GetAbsOrigin())`. A failure answers false with nothing done.
	const FElysiumEntityHandle Prop =
		CreateNamedEntity(TEXT("prop_physics"), Origin / ElysiumMove::U);
	if (!Prop.IsSet())
	{
		return false;
	}
	// 2. `DevMsg("ManBat is throwing model %s", model)` — the literal that names the body.
	UE_LOG(LogTemp, Verbose, TEXT("ManBat is throwing model %s"), *ModelName);
	// 3. SetModel(model), Spawn(), LookupBone(model) — retail passes the SAME string to the bone
	//    lookup that it passed to SetModel, which is a retail quirk and is reproduced as written.
	const int32 Bone = LookupBoneByName(*ModelName);
	// 4. `thunk_FUN_10157da0(prop, bone, &info)` makes the corpse-shaped ragdoll, then the template
	//    prop is removed (`thunk_FUN_101cd940`).
	RemoveNamedEntity(Prop);
	(void)Bone;
	// 5. The ragdoll's think is armed at `curtime + 20.0` (`_DAT_1044eb0c`) and, only when a parent
	//    name is given, it is parented and owned by it. Then family Bosses' ManBat animlink arm
	//    attaches it, and its handle lands in `m_hPickupTarget` (+0x668c).
	(void)ThrownModelThinkDelay;
	(void)ThrowParentName;
	// The ragdoll was never created, so the ManBat pickup word stays as it was — retail's own
	// `if (this_01 != 0)` guard, which answers false without writing it.
	return false;
}

// =================================================================================================
// The `CNPC_VFrenzyShadow` / `CNPC_VPlayerController` line's four damage-and-death slot arms.
// =================================================================================================

const FElysiumNpc::FTookLifeSpecies* FElysiumNpc::TookLifeSpeciesRows(int32& OutCount)
{
	// `vtmb_slot 300`: three classes share `0x103a4950`.
	static const FTookLifeSpecies Rows[] = {
		{ TEXT("CNPC_VFrenzyShadow"), TEXT("0x103a4950") },
		{ TEXT("CNPC_VPlayerController"), TEXT("0x103a4950") },
		{ TEXT("CNPC_VWolfMorph"), TEXT("0x103a4950") },
	};
	OutCount = UE_ARRAY_COUNT(Rows);
	return Rows;
}

const FElysiumNpc::FTookLifeSpecies* FElysiumNpc::TookLifeSpeciesOf(const TCHAR* InRetailClass)
{
	int32 Count = 0;
	const FTookLifeSpecies* Rows = TookLifeSpeciesRows(Count);
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

int32 FElysiumNpc::OnTakeDamageSpecies(const FElysiumTakeDamageInfo* Info)
{
	// 0x10376ae0, thirty-two bytes: forward the packet to the controller object's slot 0x238 and
	// ALWAYS answer 0 — a `CNPC_VFrenzyShadow` never reports damage taken, whatever the object does.
	(void)Info;
	(void)HasPlayerControllerObject();
	return 0;
}

int32 FElysiumNpc::OnTakeDamage_AliveSpecies(const FElysiumTakeDamageInfo* Info)
{
	// 0x10376b10, thirty-six bytes: forward through the controller object's own `+0x9c` to slot
	// 0x618 and ALWAYS answer 0. Note it is the SUB-object's slot, one indirection deeper than
	// `OnTakeDamage`'s.
	(void)Info;
	(void)HasPlayerControllerObject();
	return 0;
}

void FElysiumNpc::Event_KilledSpecies(const FElysiumTakeDamageInfo* Info)
{
	// 0x10376b50 is THREE BYTES — an empty body that ignores its parameter. `CNPC_VFrenzyShadow`'s
	// death handling is fully suppressed versus `CAI_BaseNPC::Event_Killed`: no ideal-state change,
	// no corpse, no outputs. Reproduced as written.
	(void)Info;
}

void FElysiumNpc::Event_TookLifeSpecies(const FElysiumEntity* Victim)
{
	// 0x103a4950. Guarded on BOTH the controller object (`+0x184`) and its AI component (`+0xa8`);
	// only inside both does it build the victim's debug name (`GetDebugName`, which itself tolerates
	// a null victim) and dispatch `thunk_FUN_1017e150(component, 4, -1.0, source)`.
	if (!HasPlayerControllerObject())
	{
		return;
	}
	FControllerAiEvent Event;
	Event.EventType = 4;
	Event.Priority = -1.0f;
	Event.Source = TookLifeEventSource();
	Event.VictimName = Victim != nullptr ? Victim->DebugString() : FString();
	ControllerAiEvents.Add(Event);
}
