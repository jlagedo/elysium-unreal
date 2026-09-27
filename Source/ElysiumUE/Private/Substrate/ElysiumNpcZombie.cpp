#include "Substrate/ElysiumNpcZombie.h"

#include "ElysiumDecalSubsystem.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSchedule.h"
#include "ElysiumSessionSubsystem.h"
#include "ElysiumStub.h"
#include "ElysiumWorldServices.h"
#include "Player/ElysiumCameraShots.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumLaw.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcDialogue.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcAnim10_2Shared.h"
#include "Substrate/ElysiumNpcDamageShared.h"
#include "Substrate/ElysiumNpcDebug10Shared.h"
#include "Substrate/ElysiumNpcLifecycle19_2Shared.h"
#include "Substrate/ElysiumNpcPrecache10Shared.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcState19_2Shared.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumReactions.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumWeaponClasses.h"

// --- File-scope helpers moved with this class's bodies (story 5 step 4) ---

namespace
{
	// `CNPC_VZombie::vfunc509`'s two weights and the schedule id that swaps them.
	constexpr int32 GAnim10_2ZombieIdleWeight = 999;
	constexpr int32 GAnim10_2ZombieComfortWeight = 0x14;
	constexpr int32 GAnim10_2ScheduleComfort = 0x12f;   // SCHED_TROIKA_COMFORT
	constexpr int32 GDebug10BitZombieConds = 0x40000;  // 0x103e0e9a
	constexpr TCHAR GDebug10FmtZombieCond[] = TEXT("Cond: %s\n");        // 0x10665864
	const TCHAR* const GZombieEmitters[] = {
		TEXT("zombie_headshot_death_emitter"),
		TEXT("zombie_headshot_dmg_emitter"),
	};
	const TCHAR* const GZombieWeapon = TEXT("item_w_zombie_fists");
	// `0x103e1080`'s two literals. `0x20000` is `m_bfAINPCFlags` `SLEEPING`, which
	// `Substrate/ElysiumNpcFlags.h` names.
	constexpr int32 ZombieFloatSoundFrequency = 9;
	constexpr EElysiumNpcFlag ZombieFloatSoundBlockingFlag = EElysiumNpcFlag::SLEEPING;
	// `Float_Sound_Info` row 3, the zombie's own float-sound distance (`0x103e11b8` pushes 3), and the
	// authored value it answers (`vdata/system/rules_tables.txt`), used when no rulebook is loaded.
	constexpr int32 ZombieFloatSoundDistanceRow = 3;
	constexpr float ZombieFloatSoundDistanceUnits = 250.0f;
	// `CBaseCombatCharacter::IsUnconscious` (`0x10341aa0`) — `(m_iMiscFlags & 1) != 0`. Family
	// **Sounds** keeps an identical private copy (`SoundsIsUnconscious`) for the same
	// file-ownership reason.
	bool Species2IsUnconscious(const FElysiumNpc& Npc)
	{
		return ElysiumMiscFlags::Has(Npc.MiscFlags, 0x1u);
	}
	// `CNPC_VZombie`'s two output slots. Retail fires the SAME `COutputEvent` (`+0x66e8`) from both.
	const FName ZombieOnAttackedVictim(TEXT("OnAttackedVictim"));
}

// Slots 25 / 26: `0x103e12c0` / `0x103e12f0`, the `m_OnAttackedVictim` fire with no base forward.
void FElysiumNpcZombie::Slot25(FElysiumEntity* Victim)
{
	FUN_103e12c0(Victim);
}

void FElysiumNpcZombie::Slot26(FElysiumEntity* Victim)
{
	// `0x103e12f0`, slot 26 — byte-identical to slot 25's `0x103e12c0`, firing the SAME output from
	// a second slot. Two vtable entries, one behaviour, and it is called rather than restated.
	FUN_103e12c0(Victim);
}

// Slot 510: `0x103e1080`, which tails directly into the CAI_BaseNPC body `0x1027a530`.
/** `0x103e1080` — `CNPC_VZombie`'s slot 510, `bool ShouldPlayFloatSound()`; tails into the
 *  CAI_BaseNPC body `FElysiumNpcBase::ShouldPlayFloatSound`. */
bool FElysiumNpcZombie::ShouldPlayFloatSound()
{
	// `0x103e1080`, `CNPC_VZombie`'s slot 510 `ShouldPlayFloatSound` (zero stack words: the retail
	// prototype is `bool vfunc510()`, story 5 step 0 `decisions.json` `body_resolutions`), in order:
	//
	//     m_iFloatSoundFrequency (+0x10e8) = 9;                          // first, on every call
	//     if (IsInDialog()) return false;                                // 0x102c1170
	//     if (Resolve(+0x1538) && +0x153c != -1) return false;           // grapple partner and role
	//     if (IsUnconscious()) return false;
	//     if (m_bfAINPCFlags & 0x20000) return false;                    // SLEEPING
	//     player = Resolve(m_hClosestPlayer);  if (!player) return false;
	//     if (Resolve(player->+0xfe8)) return false;                     // the player's dialog partner
	//     threshold = Float_Sound_Info row 3, read once (DAT_10940495 bits 1 and 2);
	//     if (m_flPlayerDist <= threshold) return CAI_BaseNPC::ShouldPlayFloatSound();  // 0x1027a530
	//     return false;
	//
	// **No IDLE-state gates.** The accepting arm is a direct call (`0x103e11f9` -> thunk
	// `0x10005f97` -> `0x1027a530`) into the CAI_BaseNPC body, bypassing the Troika override
	// `0x10294070` and its two IDLE tests: an ALERT zombie still moans. `FElysiumNpcBase::ShouldPlayFloatSound`
	// is that base body until story 5 step 5 names it `FElysiumNpcBase::ShouldPlayFloatSound`.
	//
	// Story 5 step 3 corrected this body (`decisions-step3.json` `retail_corrections`): it had lost
	// the dialog, grapple and player-partner gates, read the distance as an unrecovered 0.0 and
	// tailed into the Troika override. The row-3 distance is recovered (`Float_Sound_Info` row 3 =
	// 250.0 Source units, `kernel_migration_audit.rulebook_fact`); retail caches it once per process,
	// this port re-reads the authored table, which answers the same.
	FloatSoundFrequency = ZombieFloatSoundFrequency;   // +0x10e8, unconditional and first

	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	if (Dialogue.bInDialog || IsTalking(Now))   // `IsInDialog` 0x102c1170, as family Sounds reads it
	{
		return false;
	}
	if (IsGrappling())   // `+0x1538` live and `+0x153c != -1`
	{
		return false;
	}
	if (Species2IsUnconscious(*this))
	{
		return false;
	}
	if (NpcFlags.Has(ZombieFloatSoundBlockingFlag))
	{
		return false;
	}
	const FElysiumEntity* ClosestPlayer = World != nullptr
		? World->Resolve(Senses.Memory.ClosestPlayer) : nullptr;
	if (ClosestPlayer == nullptr)
	{
		return false;
	}
	// The player's `+0xfe8` dialog partner. The port's stand-in is the open dialogue session, the
	// same reading family Sounds' Troika body makes (a named limitation from story 5 step 0).
	if (World->GetOpenDialogOwner().IsSet())
	{
		return false;
	}
	float ThresholdUnits = ZombieFloatSoundDistanceUnits;
	if (UElysiumSessionSubsystem* GameState = World->GetGameState())
	{
		if (UElysiumRulebookSubsystem* Rules = GameState->Rulebook())
		{
			if (const FElysiumRuleTable* Table = Rules->Rules().Table(TEXT("Float_Sound_Info")))
			{
				ThresholdUnits = Table->Lookup(ZombieFloatSoundDistanceRow, ZombieFloatSoundDistanceUnits);
			}
		}
	}
	// `m_flPlayerDist <= threshold` (equality passes), Source units.
	if (Senses.Memory.ClosestPlayerDistanceCm / ElysiumMove::U > ThresholdUnits)
	{
		return false;
	}
	return FElysiumNpcBase::ShouldPlayFloatSound();
}

// Slot 420: `0x103defc0`.
// `0x103defc0`
void FElysiumNpcZombie::NPCInit()
{
	bZombieNeedsCrawlOutOfGround = true;                                 // FIRST
	TroikaNPCInit();
	++SpawnEquipRequests;                                                // item_w_zombie_fists
	NpcKernelLifecycle19_2Shared::Lifecycle19_2HatePlayerClass(*this);
	Hide();                                                              // slot 66 on self
	FElysiumEntity* Closest = nullptr;
	if (World != nullptr && Senses.Memory.ClosestPlayer.IsSet())
	{
		Closest = World->Resolve(Senses.Memory.ClosestPlayer);
	}
	ElysiumNpcEnemy::SetEnemy(*this, Closest != nullptr ? Closest->Handle : FElysiumEntityHandle::Invalid());
	SetTarget(Closest != nullptr ? Closest->Handle : FElysiumEntityHandle::Invalid());
	ZombieGrappleReadyTimer = NpcKernelLifecycle19_2Shared::Lifecycle19_2Now(*this) + TuningZombieGrappleReadyInterval();
	SetSchedule(ZombieCrawlScheduleRetailId, false);                      // 0x103df04b -> 0x102ae750
}

// Slot 104: `0x103df120`.
// 0x103df120
void FElysiumNpcZombie::Precache()
{
	// `CNPC_VZombie::Precache` `0x103df120` — the Troika body, the two headshot emitters with
	// preload **0**, and the fists. The two emitters are the assets the zombie head-damage arm
	// (`CNPC_VZombie::OnTakeDamage` `0x103e06d0`) names.
	TroikaPrecache();
	for (const TCHAR* Emitter : GZombieEmitters)
	{
		NpcKernelPrecache10Shared::Precache10Particle(*this, Emitter, /*Preload=*/0);
	}
	NpcKernelPrecache10Shared::Precache10Other(*this, GZombieWeapon);
}

// Slot 105: `0x103e0540`, the same vocalization-group body as the ghoul croucher's.
void FElysiumNpcZombie::SetModel(TCHAR* ModelName)
{
	ZombieLineSetModel(ModelName, TEXT("0x103e0540"));
}

// Slot 461: `0x103df5f0`, chaining the animal line's `0x1035fe80` directly.
int32 FElysiumNpcZombie::SelectIdealStateRetail()
{
	SelectIdealStateSelector = 0x2b;
	const int32 State = NpcStateRetail();
	if (State == 1)
	{
		if (NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::SeeFear))
		{
			NpcKernelState19_2Shared::State19_2Stamp(*this, 8, 0x1d6);
			return IdealStateRetail();
		}
		if (NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::NewEnemy))
		{
			NpcKernelState19_2Shared::State19_2Stamp(*this, 2, 0x1de);
			return IdealStateRetail();
		}
		if (NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::LightDamage)
			|| NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::HeavyDamage))
		{
			Cognition.bCondTookDamage = false;
		}
		(void)NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::Smell);
		return NpcKernelState19_2Shared::State19_2ChainAnimal(*this);
	}
	if (State == 3)
	{
		if (NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::SeeFear))
		{
			NpcKernelState19_2Shared::State19_2Stamp(*this, 8, 0x206);
			return IdealStateRetail();
		}
		if (NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::NewEnemy)
			|| NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::SeeEnemy))
		{
			NpcKernelState19_2Shared::State19_2Stamp(*this, 2, 0x20c);
			return IdealStateRetail();
		}
		(void)ShouldGoToIdleState();
		return NpcKernelState19_2Shared::State19_2ChainAnimal(*this);
	}
	return NpcKernelState19_2Shared::State19_2ChainAnimal(*this);
}

// Slot 201: `0x103e0bc0`
/** `CNPC_VZombie::FVisible` (`0x103e0bc0`): ONE arm in front of the Troika base — a candidate that
 *  IS this zombie's current enemy is answered by the obfuscate test `0x10146a80` (discipline stat 8
 *  at or above 1 AND the entity's `+0x14dc` cloak byte) rather than by sight — and the same
 *  fourth-argument clamp for everything else. */
bool FElysiumNpcZombie::FVisible(FElysiumEntity* SeenTarget, int32 Mask, FElysiumEntity* Blocker, int32 Arg4)
{
	// `103e0bef`: arm one — the current enemy's own `+0x9c` compared against the QUERIED entity. On
	// this leaf `+0x9c` IS the entity (the self-downcast cache), so the compare is "the candidate is
	// my enemy". The answer is then NOT `0x10146a80` — discipline stat 8 at or above 1 AND the
	// entity's `+0x14dc` cloak byte, which is `FElysiumCombatCharacter::IsObfuscatedForSenses`.
	FElysiumEntity* Enemy = GetEnemy();
	const FElysiumCombatCharacter* EnemyCharacter = Enemy != nullptr
		? Enemy->AsCombatCharacter() : nullptr;
	if (EnemyCharacter != nullptr && SeenTarget != nullptr && SeenTarget->Handle == Enemy->Handle)
	{
		return !EnemyCharacter->IsObfuscatedForSenses();
	}
	// `103e0c2e`: arm two — the Troika base with the fourth argument forced to 0, as Tzimisce does.
	return FElysiumNpc::FVisible(SeenTarget, Mask, Blocker, 0);
}

// Slot 440: `0x103df580`.
	// `0x103df580`
// `0x103df580`, `CNPC_VZombie::TranslateSchedule`, the body of `FElysiumNpcZombie::TranslateScheduleRetail`
// (story 5 step 3). Every miss is a direct call into the Troika body `0x102b12f0`.
int32 FElysiumNpcZombie::TranslateScheduleRetail(int32 ScheduleNumber)
{
	if (ScheduleNumber == 0x5b)
	{
		// `103df58e`: the console line at `0x10665640`, part of the contract a map author
		// reads — the zombie says it is ignoring the schedule while translating it.
		RecordScheduleEvent(TEXT(
			"npc_zombie: encountered schedule:[investigate unknown] ...ignoring!"));
		return 0x165;
	}
	if (ScheduleNumber == 0x156) { return 0x15e; }
	if (ScheduleNumber == 0x157) { return 0x15f; }
	return TroikaTranslateScheduleRetail(ScheduleNumber);
}

// Slot 124: `0x103e0e80`
/** `CNPC_VZombie::DrawDebugTextOverlays` (`0x103e0e80`) — the Troika body, then one `Cond: %s` line
 *  per set bit of the 0..0xbf condition bitfield at `+0x5c5c`, under `m_debugOverlays & 0x40000`. */
int32 FElysiumNpcZombie::DrawDebugTextOverlays()
{
	// `0x103e0e80`, 224 bytes. The Troika body, then — under `m_debugOverlays & 0x40000`, which is
	// NOT bit 0 — one line per set bit of the 0..0xbf bitfield at `+0x5c5c`:
	//
	//     global = (id == -1) ? -1 : id + 1000000000;
	//     local  = ConditionGlobalToLocal(GetClassScheduleIdSpace() + 0x30, global);   // 0x102ea280
	//     Q_snprintf(buf, 512, "Cond: %s\n", ConditionName(local));                    // slot 458
	//
	// The `id == -1` arm is unreachable (the loop starts at 0) and is recorded rather than written.
	// The 1e9 offset is the same script-range constant slot 458 tests against, so the pair
	// global-to-local then local-to-global is the identity for every base condition, which is what
	// `ConditionName` is handed here.
	int32 Line = TroikaDrawDebugTextOverlays();
	if ((DebugOverlays & GDebug10BitZombieConds) == 0)
	{
		return Line;
	}
	for (int32 Id = 0; Id < 0xc0; ++Id)
	{
		if (!ZombieConditionBit(Id))
		{
			continue;
		}
		const TCHAR* const Name = ConditionName(Id);
		EmitEntityText(Line, GDebug10FmtZombieCond,
			FString::Printf(GDebug10FmtZombieCond, Name != nullptr ? Name : TEXT("")));
		++Line;
	}
	return Line;
}

// Slot 24: `0x103e1280`, the Troika body `0x1029f8d0` directly FIRST, then the output.
// `0x103e1280`
void FElysiumNpcZombie::OnVictimHitByMe(FElysiumEntity* Victim)
{
	// `CNPC_VZombie::OnVictimHitByMe` `0x103e1280`, and the ORDER is the fact:
	//     CAI_BaseNPCTroika::OnVictimHitByMe(this, param_1);          // base FIRST
	//     FireOutput(&m_OnAttackedVictim (+0x66e8), param_1, this, 0);// output SECOND
	// The only species arm that keeps the base body.
	FElysiumNpc::OnVictimHitByMe(Victim);   // `0x1029f8d0`, direct
	if (Victim != nullptr)
	{
		FireOutput(FName(TEXT("OnAttackedVictim")), Victim->Handle);
	}
}

// Slot 566: `0x103e03b0`, a replacement that does not chain.
bool FElysiumNpcZombie::FValidateHintType(void* Hint)
{
	// The whole body is `return 0;`: the hint is never read.
	(void)Hint;
	return false;
}

// Slot 546: `0x103de4d0`, the class's own schedule id space.
const TCHAR* FElysiumNpcZombie::SquadSlotName(int32 SlotEn)
{
	// The class's `CAI_ClassScheduleIdSpace` `0x109403e0`, left empty by `0x102ea090(isRoot = false)`:
	// `SlotEn` translates to -1 and names `<<null>>`.
	static constexpr FSquadSlotSpecies IdSpace = {
		TEXT("CNPC_VZombie"), TEXT("0x103de4d0"), TEXT("0x109403e0") };
	return GlobalSquadSlotName(SquadSlotLocalToGlobal(&IdSpace, SlotEn));
}

// Slot 509: `0x103e0fa0`, a replacement that does not chain.
/** `CNPC_VZombie::vfunc509` (`0x103e0fa0`), slot 509's zombie arm. It REPLACES the Troika body
 *  `0x10294040` wholesale — no dialog refusal, no state test, no `SF_NPC_GAG` — and its own arms are
 *  the targetable byte, a live dialog partner, `IsBusyWithDiscipline`, then a weight of 999 that
 *  drops to 20 (a 1-in-21 roll) when the running schedule's local id is `0x12f`, in which case the
 *  float-sound arm is SKIPPED. */
bool FElysiumNpcZombie::ShouldPlayIdleSound()
{
	// `CNPC_VZombie::vfunc509` `0x103e0fa0`, 166 bytes, `CNPC_VZombie#509` only. It REPLACES the
	// Troika body `0x10294040` wholesale: there is no `IsInDialog` refusal, no `m_NPCState` test and
	// no `SF_NPC_GAG` test in it at all, which is why a zombie vocalises in states where a human
	// would not.
	//
	// Every refusal returns false through the same `return (uint)piVar3 & 0xffffff00` low-byte clear.

	// 1. `m_bIsBCCTargetable` clear -> false. SEAM: family Sounds10 recorded that this byte
	//    (`+0x7ec` on `CBaseCombatCharacter`) has no port member, so the arm is not tested; stated
	//    here rather than silently dropped.

	// 2. A LIVE `m_hDialogPartner` (+0x0fe8) -> false. The port stands for it with "this character
	//    owns the open dialogue session", the same reading family Sounds10 made.
	if (World != nullptr)
	{
		const FElysiumEntityHandle DialogOwner = World->GetOpenDialogOwner();
		if (DialogOwner.IsSet() && DialogOwner.Index == Handle.Index)
		{
			return false;
		}
	}

	// 3. `IsBusyWithDiscipline()` -> false.
	if (IsBusyWithDiscipline())
	{
		return false;
	}

	// 4. The weight. 999 by default; a RUNNING schedule whose local id (slot 447
	//    `GetLocalScheduleId`) is `0x12f SCHED_TROIKA_COMFORT` drops it to 20 — a 1-in-21 roll — AND
	//    skips the float-sound arm entirely. `GetLocalScheduleId` answers -1 for every id today
	//    (family Sounds10's note: no schedule text is parsed), so the comfort branch is unreachable
	//    until story 10i registers the schedule.
	int32 Weight = GAnim10_2ZombieIdleWeight;
	bool bComforting = false;
	if (Schedule.IsRunning())
	{
		if (GetLocalScheduleId(Schedule.Current) == GAnim10_2ScheduleComfort)
		{
			Weight = GAnim10_2ZombieComfortWeight;
			bComforting = true;
		}
	}

	// 5. Otherwise slot 510 `ShouldPlayFloatSound` decides: true plays slot 507 `FloatSound` and
	//    returns FALSE. The float sound is played INSTEAD of an idle sound, not beside it.
	if (!bComforting)
	{
		if (ShouldPlayFloatSound())
		{
			FloatSound();
			return false;
		}
	}

	// 6. True only when the roll is EXACTLY 0.
	return NpcKernelAnim10_2Shared::Anim10_2Rng().RandRange(0, Weight) == 0;
}

// Slot 141: `0x103e0430`, a prologue ahead of a direct call into `CAI_BaseNPC::TraceAttack` (`0x10266780`).
void FElysiumNpcZombie::TraceAttack(void* InInfo, const FVector& DirUnits, void* InTrace)
{
	// `0x103e0430`, the body of `FElysiumNpcZombie::TraceAttack`: the gib prologue, then the base
	// body `0x10266780` directly.
	FElysiumTakeDamageInfo* Info = static_cast<FElysiumTakeDamageInfo*>(InInfo);
	FElysiumTraceHit* Trace = static_cast<FElysiumTraceHit*>(InTrace);
	if (Info == nullptr || Trace == nullptr)
	{
		return;   // the port's one refusal: retail would have dereferenced both
	}
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
	bool bHeadHit = false;
	int32 Forced = 0;
	if (ZombieTraceAttackPrologue(Trace->HitGroup, bMelee, ZombieGibAmmoTypeCvar(0),
			ZombieGibAmmoTypeCvar(1), bHeadHit, Forced))
	{
		Info->DamageBits = static_cast<uint32>(Forced);
	}
	bZombieHeadHit = bHeadHit;                                         // +0x66e1
	FElysiumNpcBase::TraceAttack(InInfo, DirUnits, InTrace);
}

// --- Moved from `ElysiumNpcDamage.cpp` (story 5 step 4) ---

int32 FElysiumNpcZombie::ZombieGibAmmoTypeCvar(int32 Which) const
{
	// SEAM for `DAT_10940404` (0) and `DAT_1094044c` (1) — `CNPC_VZombie::TraceAttack`'s two
	// cvar-backed forced ammo types. UNRECOVERED; both answer 0, an unconstructed cvar's own answer.
	(void)Which;
	return 0;
}

bool FElysiumNpcZombie::ZombieTraceAttackPrologue(int32 HitGroup, bool bAttackerWeaponIsMelee,
	int32 FirstCvarAmmoType, int32 SecondCvarAmmoType, bool& OutHeadHit, int32& OutAmmoType)
{
	// 0x103e0430, verbatim. Both cvars are read FIRST, unconditionally, before either arm:
	//     first  = DAT_10940404->IsCommand() ? 0 : DAT_10940404->m_nValue;
	//     second = DAT_1094044c->IsCommand() ? 0 : DAT_1094044c->m_nValue;
	// then
	//     if (trace->hitgroup == 1) { headHit(+0x66e1) = 1; forced = second; }
	//     else { headHit(+0x66e1) = 0;
	//            if (!attacker || !attacker->activeWeapon) return;      // no force at all
	//            forced = first;                                        // the swap is in the test
	//            if ((activeWeapon->GetCapabilities() & 0x18000) == 0) return; }
	//     SetDamageType(info, forced);
	// Note the ORDER of the else arm: `iStack_4 = iStack_8` (second := first) is executed as part of
	// the capability test's own expression, so the FIRST cvar is what a qualifying melee hit forces
	// and the SECOND is what a head hit forces.
	OutHeadHit = (HitGroup == NpcKernelDamageShared::HitGroupHead);
	if (OutHeadHit)
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

// --- Moved from `ElysiumNpcDebug10.cpp` (story 5 step 4) ---

bool FElysiumNpcZombie::ZombieConditionBit(int32 ConditionId) const
{
	// SEAM for `CNPC_VZombie`'s bitfield at `+0x5c5c`, walked 0..0xbf. It is one of the schedule
	// block's six words and no port member carries it.
	(void)ConditionId;
	return false;
}

// --- Moved from `ElysiumNpcSpecies2.cpp` (story 5 step 4) ---

void FElysiumNpcZombie::FUN_103e12c0(FElysiumEntity* Victim)
{
	// `0x103e12c0`, `CNPC_VZombie`'s slot 25, twenty-two bytes:
	//     FireOutput(&m_OnAttackedVictim (+0x66e8), param_1, this, 0);   // thunk 0x100cd660
	//
	// **No base forward.** Slot 25's base is unnamed (`Slot25` in the generated surface) and this
	// override replaces it outright, so whatever the base did for every other class does not happen
	// for a zombie — the only effect is the mapper-visible output.
	//
	// `m_OnAttackedVictim` is a datamap `FIELD_OUTPUT` with the mapper key `OnAttackedVictim`; the
	// ACTIVATOR is `param_1` (the victim) and the CALLER is this NPC, which is the ordinary
	// `COutputEvent::FireOutput` argument order and is preserved.
	FireOutput(ZombieOnAttackedVictim,
		Victim != nullptr ? Victim->Handle : FElysiumEntityHandle());
}
