// Story 0019/8 (29e under the strict verdict), family **Damage19** -- `CAI_BaseNPC`'s bodies.
//
// Declarations are in `ElysiumNpcBaseDamage19.inl` (included inside `class FElysiumNpcBase`) or
// generated in `ElysiumNpcBaseSlots.inl` for a slot body. Every arm carries the address of the
// retail instruction it came from (`vampire.dll`, image base `0x10000000`, read off `vtmb_asm`); the
// walked prose is `docs/vtmb/npc-ai/story8/Damage19.md`. The debug ring, the scope-trace stack and
// the `__FILE__`/`__LINE__` stamps stay absent, as in every landed story-8 family.
//
// Owns (Damage19's `rule` rows): 0x10265ed0 CAI_BaseNPC::OnTakeDamage_Alive, 0x10265e90
// CAI_BaseNPC::OnTakeDamage.

#include "Substrate/ElysiumNpcBase.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "Substrate/ElysiumNpcKernelTunables.h"
#include "Substrate/ElysiumStealth.h"

namespace
{
	// `m_afMemory` bit `0x2` (`bits_MEMORY_INCOVER`), cleared at `0x10265ee7` with
	// `AND EDI,0xfffffffd`. The same bit `TASK_FORGET_COVER` clears (`ElysiumNpc.cpp`).
	constexpr uint32 GDamage19BaseMemoryInCover = 0x2u;
	// `CBaseEntity::GetFlags` bits: `FL_NPC` (`TEST AH,0x20` at `0x10265f56`) and `FL_CLIENT |
	// FL_NPC` (`TEST EAX,0x2080` at `0x10265f6f`).
	constexpr uint32 GDamage19BaseFlClient = 0x80u;
	constexpr uint32 GDamage19BaseFlNpc = 0x2000u;
	constexpr uint32 GDamage19BaseFlClientOrNpc = 0x2080u;
	// `FVisible`'s trace mask, `PUSH 0x2804091` at `0x10265f9a`.
	constexpr int32 GDamage19BaseVisibleMask = 0x2804091;
	// `_DAT_1047b868`, a DOUBLE: the repeated-damage fraction of `m_iMaxHealth` (`FMUL double ptr`
	// at `0x10266307`). Recovered fact 0019/4: repeated damage is `> 30 %`.
	constexpr double GDamage19BaseRepeatedFraction = 0.3;
	// `CSoundEnt::InsertSound(1, origin, DAT_1072bc84, 0.2, DAT_1072bcc1, this)`: `PUSH 0x1`
	// (`SOUND_COMBAT`, `0x10266359`) and `PUSH 0x3e4ccccd` (0.2 s, `0x1026634a`).
	constexpr double GDamage19BaseSoundDuration = 0.2;

	// `CBaseEntity::GetFlags() & Mask` for the two bits this body reads. `FL_NPC` is set by the
	// `CAI_BaseNPC` constructor (`0x1027c300`) on every NPC-base instance, which is `AsNpcBase()`;
	// `FL_CLIENT` is the player, which is `+0xa8`'s self-downcast and this world's player handle.
	uint32 Damage19BaseRetailFlags(const FElysiumEntity& Entity)
	{
		uint32 Flags = 0;
		if (Entity.AsNpcBase() != nullptr)
		{
			Flags |= GDamage19BaseFlNpc;
		}
		if (Entity.World != nullptr && Entity.Handle == Entity.World->PlayerHandle())
		{
			Flags |= GDamage19BaseFlClient;
		}
		return Flags;
	}

	// The combined `DMG_` bits every damage body reads: the descriptor's `m_bdmgTypes` (`+0x10`)
	// OR'd onto the packet's `+0x38` when word 0 holds a `CVDmg_t`, the packet's word alone when not
	// (`0x102661e5..0x102661f5`).
	uint32 Damage19BaseCombinedBits(const FElysiumNpcBase::FElysiumTakeDamageInfo& Info)
	{
		// Word-0 test `0x102661e9` (slot 576's bits) / `0x10266242` (slot 577's).
		return Info.Dmg != nullptr ? (Info.Dmg->DmgMask | Info.DamageBits) : Info.DamageBits;
	}

	// `CVDmg_t::GetDmg()` (`0x101faa70`) converted by `FILD` when word 0 holds a descriptor, the
	// packet's float `+0x30` when it does not (`0x102661fa..0x10266212`).
	float Damage19BaseMagnitude(const FElysiumNpcBase::FElysiumTakeDamageInfo& Info)
	{
		// Slot 576's amount: test `0x102661f8`, `GetDmg` `0x102661fc`; slot 577's: test `0x10266253`,
		// `GetDmg` `0x10266255`; the sum's: tests `0x102662b7` / `0x102662db`, `GetDmg` `0x102662b9` /
		// `0x102662dd`.
		return Info.Dmg != nullptr ? static_cast<float>(Info.Dmg->GetDmg()) : Info.Damage;
	}

	// `+0x28`, the packet's inflictor. The port's packet has no `+0x28` word; the descriptor's own
	// `Inflictor` handle is the packet `m_hInflictor` seam (`ElysiumDamage.h`), and an absent one
	// stays absent — which takes retail's null-inflictor arm rather than guessing the attacker.
	FElysiumEntity* Damage19BaseInflictor(const FElysiumNpcBase& Npc,
		const FElysiumNpcBase::FElysiumTakeDamageInfo& Info)
	{
		if (Npc.World == nullptr || Info.Dmg == nullptr || !Info.Dmg->Inflictor.IsSet())
		{
			return nullptr;
		}
		return Npc.World->Resolve(Info.Dmg->Inflictor);
	}

	// `UTIL_VecToYaw` over a delta in this world's axes (Source yaw is `atan2(y, x)` with Unreal's
	// Y negated), the yaw `0x102e2020` hands `0x102e0a80`.
	float Damage19BaseYawTo(const FVector& DeltaCm)
	{
		return FMath::RadiansToDegrees(static_cast<float>(FMath::Atan2(-DeltaCm.Y, DeltaCm.X)));
	}

	const FName& Damage19BaseOnDamaged()
	{
		static const FName Name(TEXT("OnDamaged"));
		return Name;
	}

	const FName& Damage19BaseOnHalfHealth()
	{
		static const FName Name(TEXT("OnHalfHealth"));
		return Name;
	}
}

int32 FElysiumNpcBase::CombatCharacterOnTakeDamageAlive(void* Info)
{
	// SEAM (header). The generated 29e slot body is dispatched for its tally; its `{}` is NOT the
	// answer, because `0x103302e0`'s only exit (`0x10330abe`) returns 1 on every path.
	(void)FElysiumCombatCharacter::OnTakeDamage_Alive(Info);
	return 1;
}

// =================================================================================================
// Slot 142 — `CAI_BaseNPC::OnTakeDamage` `0x10265e90`, 33 bytes.
// =================================================================================================

int32 FElysiumNpcBase::OnTakeDamage(void* Info)
{
	// `CBaseCombatCharacter::OnTakeDamage(info)` FIRST, DIRECT (`0x10265e99 CALL 0x1000e854` ->
	// `0x1032ef60`); its answer is saved and is what this body returns.
	const int32 Result = FElysiumCombatCharacter::OnTakeDamage(Info);      // 0x10265e99
	// Then, UNCONDITIONALLY and whatever that answered, slot 459 `RemoveIgnoredConditions`
	// (`+0x72c`) — after the damage transaction has raised LIGHT/HEAVY_DAMAGE, so a condition the
	// running program had suppressed becomes visible on the same pass.
	RemoveIgnoredConditions();                                              // 0x10265ea4
	return Result;                                                          // 0x10265eae
}

// =================================================================================================
// Slot 390 — `CAI_BaseNPC::OnTakeDamage_Alive` `0x10265ed0`, 1184 bytes.
// =================================================================================================

int32 FElysiumNpcBase::OnTakeDamage_Alive(void* InInfo)
{
	FElysiumTakeDamageInfo* Info = static_cast<FElysiumTakeDamageInfo*>(InInfo);

	// 1. Slot 491 `PainSound()` BEFORE anything else, then forget cover.
	PainSound();                                                            // 0x10265ed9
	BaseScheduleHost.MemoryBits &= ~GDamage19BaseMemoryInCover;             // 0x10265eea

	// Crash guard (named divergence): retail hands a null packet straight to `0x103302e0`, which
	// dereferences it.
	if (Info == nullptr)
	{
		return 0;
	}

	// 2. `CBaseCombatCharacter::OnTakeDamage_Alive(info)`; a zero answer returns 0 with nothing
	//    below run.
	if (CombatCharacterOnTakeDamageAlive(Info) == 0)                        // 0x10265ef5
	{
		return 0;                                                           // 0x10265efc -> 0x10265f03
	}

	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	const float CurTime = static_cast<float>(Now);                          // [0x1070b228]+0xc

	// 3. `m_OnDamaged` unless `m_flLastDamageTime` already equals curtime — `TEST AH,0x44 / JNP`
	//    skips on ordered-equal only, so an unordered compare fires too. The stamp is written far
	//    below (step 11), so this is the once-per-tick gate of the SAME body.
	if (static_cast<float>(BaseMemory.RepeatedDamageWindowStart) != CurTime) // 0x10265f1a
	{
		FireOutput(Damage19BaseOnDamaged(), Handle);                        // 0x10265f26 (this, this, 0)
	}

	// 4. `m_OnHalfHealth` when `m_iHealth <= m_iMaxHealth / 2`, the halving by `CDQ/SUB/SAR`, which
	//    is C's truncating division.
	if (!(Health > MaxHealth / 2))                                          // 0x10265f3e
	{
		FireOutput(Damage19BaseOnHalfHealth(), Handle);                     // 0x10265f4a
	}

	// 5. `FL_NPC` clear on this body answers 1 with nothing more.
	if ((Damage19BaseRetailFlags(*this) & GDamage19BaseFlNpc) == 0)         // 0x10265f51 / 0x10265f59
	{
		return 1;                                                           // -> 0x10266363
	}

	// 6. No attacker at `info+0x2c` answers 1 — no sound either.
	FElysiumEntity* Attacker =
		(World != nullptr && Info->Attacker.IsSet()) ? World->Resolve(Info->Attacker) : nullptr;
	if (Attacker == nullptr)                                                // 0x10265f64
	{
		return 1;                                                           // -> 0x10266363
	}

	// 7. An attacker with neither `FL_CLIENT` nor `FL_NPC` skips straight to the sound tail: no
	//    attack position, no memory, no last-damage record, no conditions and no sum.
	if ((Damage19BaseRetailFlags(*Attacker) & GDamage19BaseFlClientOrNpc) != 0) // 0x10265f6a / 0x10265f74
	{
		// 8. Seen = slot 363 `FInViewCone(attacker)` AND slot 201 `FVisible(attacker, 0x2804091,
		//    0, 0)`, the second asked only when the first answered true.
		const bool bSeen = FInViewCone(Attacker)                            // 0x10265f83 / 0x10265f8b
			&& FVisible(Attacker, GDamage19BaseVisibleMask, nullptr, 0);    // 0x10265fa2 / 0x10265faa

		// 9. `m_vecLastDamageAttackPos`: the inflictor's (`+0x28`) slot-217 origin, or — with no
		//    inflictor — this body's origin plus the file-static death-throw direction
		//    (`_DAT_1070ba40..48`) times 64.0 (`_DAT_10451acc`). Both arms of the seen split compute
		//    it the same way (`0x10265fb5` / `0x10266052`).
		FVector AttackPositionCm = FVector::ZeroVector;
		if (FElysiumEntity* Inflictor = Damage19BaseInflictor(*this, *Info))
		{
			AttackPositionCm = Inflictor->GetAbsOrigin();                   // 0x10265fb9 / 0x10266056
		}
		else
		{
			AttackPositionCm = GetAbsOrigin()                               // 0x10266012 / 0x102660a2
				+ DeathThrowImpulse() * (ElysiumNpcTunables::SixtyFour * ElysiumMove::U); // 0x10265fe4 / 0x10266074
		}
		BaseMemory.LastDamageAttackPosition = AttackPositionCm;             // 0x10265fc1 / 0x102660e0

		if (!bSeen)
		{
			// 10. The unseen split. `0x102dfa20(GetEnemies(), attacker)` is "does slot 541's memory
			//     carry a record for him" (`FElysiumNpcEnemyMemory::Find`).
			const FElysiumNpcEnemyMemory* Memory =
				static_cast<const FElysiumNpcEnemyMemory*>(GetEnemies());   // 0x10266100
			const bool bAttackerKnown =
				Memory != nullptr && Memory->Find(Attacker->Handle) != nullptr; // 0x10266108
			// Retail passes `&DAT_1070d1b0` as slot 544's third (informer) argument on both calls
			// (`PUSH 0x1070d1b0`, `0x10266124` / `0x10266156`); the port's body does not read it.
			if (GetEnemy() != nullptr                                       // 0x102660ee / 0x102660f6
				&& !bAttackerKnown                                          // 0x1026610f
				&& !Cognition.Conditions.Has(EElysiumNpcCond::SeeEnemy))    // 0x10266115 / 0x1026611c
			{
				// The CURRENT ENEMY's memory is refreshed with the attack position.
				UpdateEnemyMemory(GetEnemy(), AttackPositionCm, nullptr);   // 0x1026612c / 0x10266135
			}
			else
			{
				// The membership is asked AGAIN (`0x1026614d`), and the attacker is passed only when
				// he already has a record; otherwise NULL, which updates the position alone.
				const FElysiumNpcEnemyMemory* Again =
					static_cast<const FElysiumNpcEnemyMemory*>(GetEnemies()); // 0x10266145
				const bool bKnownAgain = Again != nullptr && Again->Find(Attacker->Handle) != nullptr;
				UpdateEnemyMemory(bKnownAgain ? Attacker : nullptr,         // 0x10266162
					AttackPositionCm, nullptr);                             // 0x1026616c
			}
		}

		// 11. With a live enemy, on BOTH paths: `0x102e0b40(m_pMotor)`, then the enemy's last known
		//     position (`0x102dfed0` off `GetEnemies()`), then `0x102e2020(m_pMotor, &lkp, 0)`.
		if (FElysiumEntity* Enemy = GetEnemy())                             // 0x10266176 / 0x1026617e
		{
			++OnTakeDamageAliveMotorResets;                                 // 0x10266186
			(void)Enemy;                                                    // 0x1026618f slot 167 again
			FVector LastKnownCm = FVector::ZeroVector;
			EnemyLastKnownPosition(LastKnownCm);                            // 0x1026619f / 0x102661a7
			++OnTakeDamageAliveMotorFaceCalls;
			OnTakeDamageAliveMotorFaceTargetCm = LastKnownCm;
			SetMotorHintYaw(Damage19BaseYawTo(LastKnownCm - GetAbsOrigin())); // 0x102661b9
		}

		// 12. `m_hLastDamageEnt` = the attacker's handle, or `0xffffffff` for none (unreachable
		//     here: step 6 refused a null attacker, but retail tests it again at `0x102661c3`).
		BaseMemory.LastDamageAttacker = Attacker != nullptr
			? Attacker->Handle : FElysiumEntityHandle::Invalid();           // 0x102661c7 / 0x102661cc / 0x102661d4
		// 13. `m_bCondTookDamage = 1`.
		Cognition.bCondTookDamage = true;                                   // 0x102661de

		// 14. Slot 576 `IsLightDamage(amount, bits)` sets 0x4c; slot 577 `IsHeavyDamage` sets
		//     0x4d. Retail recomputes bits and amount before each (`0x102661e5`, `0x1026623e`), and
		//     polls the ConVar at `DAT_10924a6c` (slot 1, result discarded) before each SetCondition
		//     (`0x10266232`, `0x1026628c`, and `0x10266327` before 0x4e) - it decides nothing.
		if (IsLightDamage(Damage19BaseMagnitude(*Info),
			static_cast<int32>(Damage19BaseCombinedBits(*Info))))           // 0x10266220 / 0x10266228
		{
			Cognition.Conditions.Set(EElysiumNpcCond::LightDamage);         // 0x10266239
		}
		if (IsHeavyDamage(Damage19BaseMagnitude(*Info),
			static_cast<int32>(Damage19BaseCombinedBits(*Info))))           // 0x10266279 / 0x10266282
		{
			Cognition.Conditions.Set(EElysiumNpcCond::HeavyDamage);         // 0x10266293
		}

		// 15. `m_flSumDamage`: RESET to this hit when `curtime - m_flLastDamageTime >= 1.0`
		//     (`_DAT_10449280`, a double; `TEST AH,0x5 / JP` also takes the unordered compare),
		//     otherwise ACCUMULATE. Then `m_flLastDamageTime = curtime`.
		const double Elapsed = static_cast<double>(CurTime)
			- static_cast<double>(static_cast<float>(BaseMemory.RepeatedDamageWindowStart)); // 0x1026629f / 0x102662a2
		const bool bAccumulate = Elapsed < ElysiumNpcTunables::OneDouble;  // 0x102662a8 / 0x102662b3
		const float Sum = bAccumulate
			? Damage19BaseMagnitude(*Info)
				+ static_cast<float>(BaseMemory.RepeatedDamageAccumulated)  // 0x102662c6 / 0x102662d1
			: Damage19BaseMagnitude(*Info);                                 // 0x102662e6 / 0x102662ec
		// `m_flSumDamage` is a float in retail and an int on the port's word (`+0x5d94`,
		// `FElysiumNpcBaseMemory::RepeatedDamageAccumulated`); the stored sum truncates, the compare
		// below reads the float exactly as retail's `FSTP`/`FLD` round trip does.
		BaseMemory.RepeatedDamageAccumulated = static_cast<int32>(Sum);    // 0x102662ef
		BaseMemory.RepeatedDamageWindowStart = Now;                        // 0x10266310

		// 16. REPEATED_DAMAGE (0x4e) when `m_iMaxHealth * 0.3 < m_flSumDamage` (strict; `FCOMPP` +
		//     `TEST AH,0x5 / JP` skips on >= and on unordered).
		if (static_cast<double>(MaxHealth) * GDamage19BaseRepeatedFraction
			< static_cast<double>(Sum))                                     // 0x10266307 / 0x1026631d
		{
			Cognition.Conditions.Set(EElysiumNpcCond::RepeatedDamage);      // 0x1026632e
		}
	}

	// 17. The tail, reached from the 0x2080 skip too: `CSoundEnt::InsertSound(SOUND_COMBAT,
	//     GetAbsOrigin(), DAT_1072bc84, 0.2, DAT_1072bcc1, this)`. The volume and occlusion cells
	//     are filled at runtime by the sound-volume table (no corpus writer); the port's bus resolves
	//     the same answer from `NPC_TAKE_DAMAGE`'s row.
	if (World != nullptr)
	{
		World->EmitGameSound(GetAbsOrigin(), ElysiumGameSounds::NpcTakeDamage(), -1.f, Handle,
			ElysiumStealth::HearingReductionCmFor(this), ElysiumGameSounds::Combat,
			GDamage19BaseSoundDuration);                                    // 0x10266352 / 0x1026635b
	}
	return 1;                                                               // 0x10266364
}
