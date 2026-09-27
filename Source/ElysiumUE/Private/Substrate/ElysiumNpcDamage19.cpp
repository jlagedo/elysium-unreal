// Story 0019/8 (29e under the strict verdict), family **Damage19** -- `CAI_BaseNPCTroika`'s bodies.
//
// Declarations are in `ElysiumNpcDamage19.inl` (included inside `class FElysiumNpc`) or generated
// in `ElysiumNpcSlots.inl` for a slot body. Every arm carries the address of the retail instruction
// it came from (`vampire.dll`, read off `vtmb_asm`); the walked prose is
// `docs/vtmb/npc-ai/story8/Damage19.md`. The `__FILE__`/`__LINE__` stamps retail writes into
// `m_SelectScheduleTrace` (`+0x1b30`/`+0x1b34`) are ABSENT shape words; as in slot 442's landed
// body the retail line is recorded through `RecordScheduleEvent`.
//
// Owns (Damage19's `rule` rows): 0x1029fa50 CAI_BaseNPCTroika::FUN_1029fa50 (slot 316), 0x1029fcf0
// CAI_BaseNPCTroika::PlayerDefenderBlockReaction, 0x102a01b0
// CAI_BaseNPCTroika::PlayerKnockbackReaction, 0x102beda0 CAI_BaseNPCTroika::OnTakeDamage_Alive
// (slot 390; the corpus label "OnTakeDamage" is a doc name, pass R second judge), 0x102bed30
// CAI_BaseNPCTroika::OnTakeDamage (slot 142, corpus-labelled `CNPC_VVampire::OnTakeDamage`).

#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumNpcFlags.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumInterestingPlace.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelTunables.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumWeaponClasses.h"

namespace
{
	// `SetSchedule` (`0x102ae750`) ids and the `AI_BaseNPCTroika.cpp` trace lines each arm stamps.
	constexpr int32 GDamage19DodgeSchedule = 0xd5;            // slot 316, `PUSH 0xd5` at 0x1029fae3
	constexpr int32 GDamage19DodgeLine = 0x28e0;
	constexpr int32 GDamage19BlockStaggerSchedule = 0xd8;     // slot 318, class 3
	constexpr int32 GDamage19BlockStaggerLine = 0x2935;
	constexpr int32 GDamage19BlockSchedule = 0xd7;            // slot 318, any other class
	constexpr int32 GDamage19BlockLine = 0x2939;
	constexpr int32 GDamage19BlockStaggerClass = 3;           // `CMP EAX,0x3` at 0x1029fd1a
	constexpr int32 GDamage19KnockbackPushSchedule = 0x14d;   // slot 320
	constexpr int32 GDamage19KnockbackSchedule = 0x14c;
	constexpr int32 GDamage19KnockbackLine = 0x29b5;
	constexpr int32 GDamage19InterestDeathSchedule = 0x104;   // slot 390
	constexpr int32 GDamage19InterestDeathLine = 0x6121;
	// `FUN_10344da0`: `0x8a < activity && activity < 0x94`.
	constexpr int32 GDamage19KnockbackPushLow = 0x8a;
	constexpr int32 GDamage19KnockbackPushHigh = 0x94;
	// Slot 318's attack re-arm: `_DAT_1049a1c0` 0.3 and `_DAT_1049a1c4` 1.5, both floats read out of
	// the pinned image. The listing is `FLD [0x1049a1c0]; FSUB [0x1049a1c4]; FADD [0x1049a1c4];
	// FADD curtime` — the folded remains of a lerp whose parameter the compiler fixed at 1, so the
	// delay is the FIRST cell (0.3), not a random draw.
	constexpr float GDamage19BlockReArmLo = 0.3f;
	constexpr float GDamage19BlockReArmHi = 1.5f;
	// `PUSH -0x1` at 0x102a01de: `SelectHeaviestSequence`'s second argument.
	constexpr int32 GDamage19NoCurrentSequence = -1;
	// `PUSH 0x0` at 0x102bee8a: `AddExpressionForEvent`'s event.
	constexpr int32 GDamage19PainExpressionEvent = 0;

	const FName& Damage19OnDamaged()
	{
		static const FName Name(TEXT("OnDamaged"));
		return Name;
	}

	// The `m_SelectScheduleTrace` stamp, recorded under its retail line (`+0x1b30`/`+0x1b34` ABSENT).
	void Damage19StampSchedule(FElysiumNpc& Npc, const TCHAR* Body, int32 Line, int32 Schedule)
	{
		Npc.RecordScheduleEvent(FString::Printf(TEXT("%s AI_BaseNPCTroika.cpp:0x%x -> SetSchedule 0x%x"),
			Body, Line, Schedule));
	}

	// The same stamp where retail writes it BEFORE the arm that picks the program is decided.
	void Damage19StampLine(FElysiumNpc& Npc, const TCHAR* Body, int32 Line)
	{
		Npc.RecordScheduleEvent(FString::Printf(TEXT("%s AI_BaseNPCTroika.cpp:0x%x"), Body, Line));
	}

	// `CVDmg_t::GetDmg()` when word 0 holds a descriptor, the packet's `+0x30` float otherwise.
	float Damage19Magnitude(const FElysiumNpcBase::FElysiumTakeDamageInfo& Info)
	{
		return Info.Dmg != nullptr ? static_cast<float>(Info.Dmg->GetDmg()) : Info.Damage;
	}
}

// =================================================================================================
// The helpers the slot bodies below call.
// =================================================================================================

void FElysiumNpc::AddExpressionForEvent(int32 Event)
{
	// SEAM (header): `0x101072b0` — counted, the event kept.
	++AddExpressionForEventCalls;
	LastExpressionEvent = Event;
}

bool FElysiumNpc::InterestingPlaceHasDeathActivity(const FElysiumInterestingPlace& Place) const
{
	// `0x102daf50`: `if (place->+0x548) return 0x102dd610(type) /* type+0x19c */; return 0;`
	if (Place.ResolvedType == nullptr)
	{
		return false;                                                       // 0x102daf5a
	}
	// SEAM (header): the type row's `+0x19c` byte has no port word.
	return false;
}

FString FElysiumNpc::InterestingPlaceDeathActivityName(const FElysiumInterestingPlace& Place) const
{
	// `0x102daf20`: `if (place->+0x548) return 0x102dd580(type); return "";`. SEAM (header).
	(void)Place;
	return FString();
}

float FElysiumNpc::SequenceSwingReachUnits(const FElysiumEntity& Swinger)
{
	// SEAM (header): `GetSeqDesc(m_nSequence)+0x2d0`, no studio header in the kernel.
	(void)Swinger;
	return ElysiumNpcTunables::Zero;
}

bool FElysiumNpc::MeleeSwingInRange(const FElysiumEntity& Swinger, const FVector& PointCm)
{
	// `0x10345760`. `ESI` is the SWINGER (the entity the caller dispatched on), the argument the
	// point: slot 217 on the swinger (`0x103457ce`), the 2-D delta squared, `sqrt` through
	// `[0x10579660]` (`0x103457f4`), then `100.0 + seqdesc+0x2d0` against it.
	const FVector SwingerUnits = Swinger.GetAbsOrigin() / ElysiumMove::U;   // 0x103457ce
	const FVector PointUnits = PointCm / ElysiumMove::U;
	const float Dx = static_cast<float>(PointUnits.X - SwingerUnits.X);    // 0x103457d9
	const float Dy = static_cast<float>(PointUnits.Y - SwingerUnits.Y);    // 0x103457dd
	const float Distance = FMath::Sqrt(Dx * Dx + Dy * Dy);                 // 0x103457f4
	// `_DAT_1049e048` = 100.0f, the pad the reach is added to.
	const float Reach = ElysiumNpcTunables::Hundred + SequenceSwingReachUnits(Swinger); // 0x10345818 / 0x1034581e
	return !(Reach < Distance);                                            // 0x10345824 / 0x10345837
}

bool FElysiumNpc::HoldingMeleeWeapon(const FElysiumCombatCharacter& Character)
{
	// `0x10345d00`: `GetActiveWeapon()` null answers false; otherwise `weapon->slot360() & 0x18000`.
	return ElysiumNpcCond::WeaponCapability(Character) == ElysiumNpcCond::ECapability::Melee;
}

int32 FElysiumNpc::DefenderBlockReactionClass(const FElysiumMeleeRoll& Roll) const
{
	// `0x103498b0`: `__ftol(word1 - word3 - word2)` (`0x103498b1..0x103498c6`) against the four
	// `Melee_Reactions` cells. `ClassifyDefender` walks the same four `<=` bands in the same order.
	const FElysiumWeaponContext Context = FElysiumWeaponContext::FromCharacter(*this);
	const EElysiumMeleeDefenderReaction Reaction =
		ElysiumWeapons::ClassifyDefender(Context.Margins, Roll.Margin());
	if (Reaction == EElysiumMeleeDefenderReaction::Unclassified)
	{
		return INDEX_NONE;   // no `rules.txt` table: not a retail band, takes the not-3 arm
	}
	return static_cast<int32>(Reaction) - 1;   // DodgeAttack..HitKnockback -> retail 0..4
}

bool FElysiumNpc::IsKnockbackPushActivity(int32 Activity)
{
	// `0x10344da0`: `if (0x8a < a && a < 0x94) return 1; return 0;`
	return GDamage19KnockbackPushLow < Activity && Activity < GDamage19KnockbackPushHigh;
}

void FElysiumNpc::DismissPresenceEffect()
{
	// SEAM (header): `0x101e3ff0(&DAT_10739a4c, this)` — counted, removes nothing.
	++DismissPresenceEffectCalls;
}

// =================================================================================================
// Slot 142 — `CAI_BaseNPCTroika::OnTakeDamage` `0x102bed30` (corpus `CNPC_VVampire::OnTakeDamage`).
// =================================================================================================

int32 FElysiumNpc::OnTakeDamage(void* Info)
{
	// `m_bInvincible` (+0x63d8) clear: tail-jump to `CAI_BaseNPC::OnTakeDamage`, DIRECT.
	if (!bInvincible)                                                       // 0x102bed38
	{
		return FElysiumNpcBase::OnTakeDamage(Info);                         // 0x102bed6d
	}
	// Invincible: the packet is refused outright, but FIRST — only when `m_flLastDamageTime`
	// differs from curtime (`JNP` at 0x102bed4e skips on ordered-equal only) — the stamp is written
	// and `m_OnDamaged` fires with this as activator and caller, delay 0. At most once a tick.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	if (static_cast<float>(BaseMemory.RepeatedDamageWindowStart) != static_cast<float>(Now)) // 0x102bed4e
	{
		BaseMemory.RepeatedDamageWindowStart = Now;                         // 0x102bed56
		FireOutput(Damage19OnDamaged(), Handle);                            // 0x102bed63
	}
	return 0;                                                               // 0x102bed68
}

// =================================================================================================
// Slot 316 — `CAI_BaseNPCTroika::FUN_1029fa50` `0x1029fa50`, the melee-reaction hook.
// =================================================================================================

void FElysiumNpc::Slot316(FElysiumEntity* Target, bool bCheckDodge, bool bCheckBlock)
{
	// The ConVar at `DAT_10924a6c` is polled (slot 1, result discarded) before every SetCondition
	// in this body (`0x1029fa5c`, `0x1029fab3`, `0x1029fb18`); it decides nothing.
	// 1. BEING_ATTACKED, UNCONDITIONALLY and before the null-target test.
	Cognition.Conditions.Set(EElysiumNpcCond::BeingAttacked);               // 0x1029fa63
	if (Target == nullptr)                                                  // 0x1029fa6e
	{
		return;
	}
	// 2. The dodge arm: bCheckDodge, the TARGET's swing in range of this body's origin, this body
	//    holding a melee weapon, slot 325 — all four, in that order.
	if (bCheckDodge                                                         // 0x1029fa7a
		&& MeleeSwingInRange(*Target, GetAbsOrigin())                       // 0x1029fa80 / 0x1029fa89 / 0x1029fa90
		&& HoldingMeleeWeapon(*this)                                        // 0x1029fa94 / 0x1029fa9b
		&& Slot325())                                                       // 0x1029faa1 / 0x1029faa9
	{
		Cognition.Conditions.Set(EElysiumNpcCond::ShouldDodge);             // 0x1029faba
		// Only when slot 590 also agrees is the dodge program installed.
		if (OkToInterruptForMelee())                                        // 0x1029fac3 / 0x1029facb
		{
			Damage19StampSchedule(*this, TEXT("Slot316"), GDamage19DodgeLine,
				GDamage19DodgeSchedule);                                    // 0x1029fad6 / 0x1029fae0
			SetSchedule(GDamage19DodgeSchedule, false);                     // 0x1029faea
		}
	}
	// 3. The block arm: bCheckBlock, a melee weapon, slot 324 — NO range test, NO program.
	if (bCheckBlock                                                         // 0x1029faf5
		&& HoldingMeleeWeapon(*this)                                        // 0x1029faf9 / 0x1029fb00
		&& Slot324())                                                       // 0x1029fb06 / 0x1029fb0e
	{
		Cognition.Conditions.Set(EElysiumNpcCond::ShouldBlock);             // 0x1029fb1f
	}
}

// =================================================================================================
// Slot 318 — `CAI_BaseNPCTroika::PlayerDefenderBlockReaction` `0x1029fcf0`.
// =================================================================================================

bool FElysiumNpc::PlayerDefenderBlockReaction(FElysiumEntity* Attacker, void* InRoll, void* InDmg)
{
	(void)Attacker;
	(void)InDmg;
	// 1. No `melee_dice_roll_result` answers false.
	if (InRoll == nullptr)                                                  // 0x1029fcfa
	{
		return false;                                                       // 0x1029fd00
	}
	// 2. `0x1028a190` (`OkToDisturb`) refusing answers ITS false byte.
	if (!OkToDisturb())                                                     // 0x1029fd05 / 0x1029fd0c
	{
		return false;                                                       // 0x1029fd10
	}
	// 3. The defender class of the roll: 3 (block-stagger) takes 0xd8, anything else 0xd7.
	const int32 Class =
		DefenderBlockReactionClass(*static_cast<const FElysiumMeleeRoll*>(InRoll)); // 0x1029fd15
	if (Class == GDamage19BlockStaggerClass)                                // 0x1029fd1a / 0x1029fd2b
	{
		Damage19StampSchedule(*this, TEXT("PlayerDefenderBlockReaction"), GDamage19BlockStaggerLine,
			GDamage19BlockStaggerSchedule);                                 // 0x1029fd21 / 0x1029fd3e
		SetSchedule(GDamage19BlockStaggerSchedule, false);                  // 0x1029fd48 / 0x1029fd4d
	}
	else
	{
		Damage19StampSchedule(*this, TEXT("PlayerDefenderBlockReaction"), GDamage19BlockLine,
			GDamage19BlockSchedule);                                        // 0x1029fd21 / 0x1029fd2d
		SetSchedule(GDamage19BlockSchedule, false);                         // 0x1029fd37 / 0x1029fd4d
	}
	// 4. `m_flNextAttack = (0.3 - 1.5) + 1.5 + curtime`, in retail's float order.
	const float Delay = (GDamage19BlockReArmLo - GDamage19BlockReArmHi) + GDamage19BlockReArmHi; // 0x1029fd52..0x1029fd64
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	NextAttackTime = static_cast<double>(static_cast<float>(Now) + Delay);  // 0x1029fd6a / 0x1029fd6f
	return true;                                                            // 0x1029fd6d
}

// =================================================================================================
// Slot 320 — `CAI_BaseNPCTroika::PlayerKnockbackReaction` `0x102a01b0`.
// =================================================================================================

bool FElysiumNpc::PlayerKnockbackReaction(FElysiumEntity* Attacker, int32 Activity)
{
	// 1. `0x1028a190` must admit and `IsInDialog` must refuse; either failure answers false with
	//    nothing written.
	if (!OkToDisturb())                                                     // 0x102a01b4 / 0x102a01bb
	{
		return false;                                                       // 0x102a0253
	}
	if (IsInDialog())                                                       // 0x102a01c3 / 0x102a01ca
	{
		return false;                                                       // 0x102a0253
	}
	// 2. `TranslateActivity(activity, 0)` then `SelectHeaviestSequence(translated, -1)`; a negative
	//    sequence answers false with nothing written.
	int32 WeaponActivity = 0;
	const int32 Translated = TranslateActivityNumber(Activity, WeaponActivity); // 0x102a01d9
	if (SelectHeaviestSequence(Translated, GDamage19NoCurrentSequence) < 0) // 0x102a01e3 / 0x102a01ea
	{
		return false;                                                       // 0x102a0253
	}
	// 3. `m_knockbackType = activity`, slot 614 `ResetThinkTimers`, the trace stamp (one line for
	//    both programs, written before the push test).
	KnockbackType = Activity;                                               // 0x102a01f0
	ResetThinkTimers(World != nullptr ? World->NowSeconds() : 0.0);         // 0x102a01f6
	Damage19StampLine(*this, TEXT("PlayerKnockbackReaction"), GDamage19KnockbackLine); // 0x102a01ff / 0x102a0209
	// 4. An activity in the push window builds the knockback velocity (`0x102a0290`) and takes
	//    0x14d; any other takes 0x14c with no push. True either way.
	if (IsKnockbackPushActivity(Activity))                                  // 0x102a0213 / 0x102a021a
	{
		ComputeKnockbackVelocity(Attacker);                                 // 0x102a0224
		SetSchedule(GDamage19KnockbackPushSchedule, false);                 // 0x102a0232
		return true;                                                        // 0x102a0238
	}
	SetSchedule(GDamage19KnockbackSchedule, false);                         // 0x102a0247
	return true;                                                            // 0x102a024d
}

// =================================================================================================
// Slot 390 — `CAI_BaseNPCTroika::OnTakeDamage_Alive` `0x102beda0`, 535 bytes.
// =================================================================================================

int32 FElysiumNpc::OnTakeDamage_Alive(void* InInfo)
{
	FElysiumTakeDamageInfo* Info = static_cast<FElysiumTakeDamageInfo*>(InInfo);
	// Crash guard (named divergence): retail copies the packet out of a null pointer.
	if (Info == nullptr)
	{
		return 0;
	}

	// 1. The whole incoming packet is cached verbatim on the NPC FIRST.
	LastTakeDamageInfo = *Info;                                             // 0x102bedab..0x102bee48

	// 2. `CAI_BaseNPC::OnTakeDamage_Alive`, DIRECT; its answer is what every exit returns, and a
	//    zero answer returns at once.
	const int32 Result = FElysiumNpcBase::OnTakeDamage_Alive(Info);         // 0x102bee50
	if (Result == 0)                                                        // 0x102bee59
	{
		return Result;                                                      // 0x102befb4
	}

	FElysiumEntity* Attacker =
		(World != nullptr && Info->Attacker.IsSet()) ? World->Resolve(Info->Attacker) : nullptr; // +0x2c
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;

	// 3. The committed amount against 0.0 (`_DAT_104454c4`); `AND EAX,0x4100 / JNZ` takes <= and
	//    unordered to the zero-damage arm.
	const float Amount = Damage19Magnitude(*Info);                          // 0x102bee63 / 0x102bee65 / 0x102bee74
	if (Amount > ElysiumNpcTunables::Zero)                                  // 0x102bee77 / 0x102bee84
	{
		// 4. Positive damage: the pain expression.
		AddExpressionForEvent(GDamage19PainExpressionEvent);                // 0x102bee8e
		// 5. `m_bfAINPCFlags & 0x40000000` (ONE_HIT_KILL): slot 144 `Event_Killed(info)` and return.
		if (NpcFlags.Has(EElysiumNpcFlag::ONE_HIT_KILL))                    // 0x102bee93 / 0x102bee9d
		{
			Event_Killed(Info);                                             // 0x102beea4
			return Result;                                                  // 0x102beeaf
		}
		// 6. An interesting place with a death activity: resolve the activity ONCE, and return
		//    WITHOUT the tail on every sub-arm.
		const FElysiumInterestingPlace* Place = CurrentAmbientSpot();       // +0x62ec
		if (Place != nullptr                                                // 0x102beeba
			&& InterestingPlaceHasDeathActivity(*Place))                    // 0x102beec0 / 0x102beec7
		{
			if (InterestingDeathActivity == INDEX_NONE)                     // 0x102beed4 / 0x102beed7
			{
				const FString Name = InterestingPlaceDeathActivityName(*Place); // 0x102beedf
				InterestingDeathActivity = ActivityIdForName(Name);         // 0x102beee7 / 0x102beef2
				if (InterestingDeathActivity == INDEX_NONE)                 // 0x102beeef / 0x102beef8
				{
					// `0x10411f90` (result discarded) then DevWarning through `[0x109f364c]`.
					UE_LOG(LogElysiumNpcEnt, Warning,
						TEXT("Can not find interest death activity '%s' in the activity list.  Dying immediately.\n"),
						*Name);                                             // 0x102bef00 / 0x102bef05
					Event_Killed(Info);                                     // 0x102bef13
					return Result;                                          // 0x102bef1f
				}
				Damage19StampSchedule(*this, TEXT("OnTakeDamage_Alive"), GDamage19InterestDeathLine,
					GDamage19InterestDeathSchedule);                        // 0x102bef2b / 0x102bef35
				SetSchedule(GDamage19InterestDeathSchedule, false);         // 0x102bef3f
			}
			// An activity ALREADY resolved (`JNZ 0x102bef44`) returns here too, with no program
			// installed and no tail — reproduced.
			return Result;                                                  // 0x102bef4a
		}
		// Neither arm: fall to the tail (`JZ 0x102bef88`).
	}
	else
	{
		// 7. Zero (or unordered) damage still counts as damage for the AI: LIGHT_DAMAGE, the
		//    last-damage attacker and `m_bCondTookDamage` — the arm the base cannot raise.
		Cognition.Conditions.Set(EElysiumNpcCond::LightDamage);             // 0x102bef4d / 0x102bef5c
		BaseMemory.LastDamageAttacker = Attacker != nullptr
			? Attacker->Handle : FElysiumEntityHandle::Invalid();           // 0x102bef66 / 0x102bef6f / 0x102bef77
		Cognition.bCondTookDamage = true;                                   // 0x102bef81
	}

	// 8. The tail: a live attacker extends the FVisible range override by 5.0 s (`0x1028e8b0`,
	//    `PUSH 0x40a00000`), then slot 600 with slot 167 `GetEnemy()`.
	if (Attacker != nullptr)                                                // 0x102bef8b / 0x102bef8d
	{
		Senses.ExtendVisionOverride(*this, Attacker->Handle, Now,
			static_cast<double>(ElysiumNpcTunables::Five));                 // 0x102bef97
	}
	Slot600(GetEnemy());                                                    // 0x102befa0 / 0x102befa9
	return Result;                                                          // 0x102befb4
}
