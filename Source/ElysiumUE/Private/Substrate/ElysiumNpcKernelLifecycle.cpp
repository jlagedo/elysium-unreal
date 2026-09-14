#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumRng.h"
#include "ElysiumSaveArchive.h"   // story 29d: `RestoreExtendedHeader`'s `IRestore` is this archive
#include "Substrate/ElysiumInterestingPlace.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcScheduleHost.h"

// Story 29c-1, family **Lifecycle** — spawn, init, precache, save/restore, dormancy and the
// per-think clocks of `order.md` layers 0–9. 59 rows. The walked prose is
// `docs/vtmb/npc-ai/lifecycle.md`.
//
// Twelve rows fill Troika-line slots and are DEFINED here with the generated signature; the rest are
// species or variant bodies of slots a later story owns, and land under the named methods
// `ElysiumNpcKernelLifecycle.inl` declares. That file carries the family's own reading notes.

namespace
{
	// `CAI_Hint::vfunc5`'s two retail numbers. The condition the hint's destructor raises on its
	// owner (`SetCondition(owner, 0x29)`, `0x10269a20`) is above nothing this port's registrar
	// names, so it is spelled as the number — family Conditions' convention for the Werewolf's
	// `0x7a`; and the reuse delay it passes to `ClearHintNode` is a literal `0.0`, not the 5.0 s
	// every other caller passes.
	constexpr int32 GHintDestroyedCondition = 0x29;
	constexpr float GHintDestroyedReuseDelay = 0.0f;

	// `_DAT_104454c4` — the shared `0.0f` constant of `vampire.dll`, and the "unset" SENTINEL every
	// `CAI_Hint::Spawn` default test compares a hint float against. Family **Hints** recovered the
	// same global; it is repeated here rather than exported because it is one float.
	constexpr float GLifeZero = 0.0f;

	// `RandomFloat(0.2, 0.9)` — slot 471 `GetReactionDelay` (`0x1026a8a0`). `0x3e4ccccd` / `0x3f666666`.
	constexpr float GReactionDelayMin = 0.2f;
	constexpr float GReactionDelayMax = 0.9f;

	// `m_spawnflags` bits slot 423 `IsTemplate` (`0x1027e120`) and slot 552 `ShouldFadeOnDeath`
	// (`0x1027a400`) test — bit 11 and bit 9 of the same word.
	constexpr int32 GSpawnFlagTemplate = 1 << 11;      // 0x800
	constexpr int32 GSpawnFlagFadeOnDeath = 1 << 9;    // 0x200

	// `m_iEFlags` bit 0, `EFL_KILLME` — slot 116 `IsMarkedForDeletion` (`0x10027490`).
	constexpr int32 GEntityFlagKillMe = 1 << 0;

	// Slot 91 `ShouldCollide` (`0x100b4de0`): the one collision group it refuses for, and the
	// contents mask bit that overrides the refusal. `1` is Source's `COLLISION_GROUP_DEBRIS`.
	constexpr int32 GCollisionGroupDebris = 1;
	constexpr int32 GContentsDebrisOverride = 0x4000000;

	// `m_fEffects` bits the `KeyValue` cascade ORs in (`0x1009e430`).
	constexpr int32 GEffectNoShadow = 0x20;
	constexpr int32 GEffectNoReceiveShadow = 0x80;

	// `CCineNPC::Spawn` (`0x101a6f10`): the two spawnflags it reads and the two compiled constants it
	// adds to `curtime`. **Unrecovered**: `_DAT_10449280` (the auto-remove think delay) and
	// `_DAT_10449e10` (the named cine's start-time offset) have no reader in the corpus that pins
	// their value. Declared as named constants so the day they are read the change is one line.
	constexpr int32 GCineSpawnFlagAutoRemove = 0x10;
	constexpr int32 GCineSpawnFlagNotInterruptable = 0x20;
	constexpr double GCineAutoRemoveDelaySeconds = 0.0;   // _DAT_10449280 — unrecovered
	constexpr double GCineStartTimeOffsetSeconds = 0.0;   // _DAT_10449e10 — unrecovered

	// `CAI_StandoffGoal::Spawn` (`0x102cd2d0`): `m_flNextThink = curtime + _DAT_1044e658`.
	// **Unrecovered**, same situation.
	constexpr double GStandoffGoalThinkDelaySeconds = 0.0;   // _DAT_1044e658

	// `CAI_StandoffBehavior::vfunc13` (`0x102c7600`): `_DAT_10497530`, the elapsed-seconds threshold
	// the reaction re-roll and the blocked latch both compare against. **Unrecovered**: two readers,
	// both in that body, and nothing pins the value.
	constexpr double GStandoffElapsedThresholdSeconds = 0.0;   // _DAT_10497530

	// The camera's model fallback (`0x103689c0`), verbatim from `.rdata` `0x1062f790`.
	const TCHAR* const GCameraNullModel = TEXT("models/null.mdl");

	// `CGenericNPC::Precache` (`0x1034aa40`) walks `PTR_s_weapons_ar2_ar2_fire1_wav_106244c0` for
	// `0xc` bytes at stride 4 — THREE entries. Only the first is named by the pointer's own symbol;
	// the other two are the table's next two slots and are recorded by index, because the corpus
	// names the array by its head and nothing pins the remaining two strings.
	const TCHAR* const GGenericNpcWeaponSounds[] =
	{
		TEXT("weapons/ar2/ar2_fire1.wav"),
		TEXT("PTR_s_weapons_ar2_ar2_fire1_wav_106244c0[1]"),   // unrecovered
		TEXT("PTR_s_weapons_ar2_ar2_fire1_wav_106244c0[2]"),   // unrecovered
	};

	// `CAI_BaseNPC::FindNamedEntity` (`0x10279090`): the selector names, verbatim from `.rdata`, and
	// the two retired literals with their own rate-limit counters.
	const TCHAR* const GSelPlayer = TEXT("!player");                    // 0x10549184
	const TCHAR* const GSelPlayerController = TEXT("!playercontroller");// 0x1054916c
	const TCHAR* const GSelEnemy = TEXT("!enemy");                      // 0x105ccc2c
	const TCHAR* const GSelSelf = TEXT("!self");                        // 0x105ccc24
	const TCHAR* const GSelTarget1 = TEXT("!target1");                  // 0x1054914c
	const TCHAR* const GSelNearestFriend = TEXT("!nearestfriend");      // 0x105ccc10
	const TCHAR* const GSelFriend = TEXT("!friend");                    // 0x105ccc04
	const TCHAR* const GSelRetiredSelf = TEXT("self");                  // 0x105a1cb0
	const TCHAR* const GSelRetiredPlayer = TEXT("Player");              // 0x10547404

	// `DAT_10920558` / `DAT_1092055c` — the two rate-limit counters, module statics in retail and so
	// module statics here. Retail prints while the POST-INCREMENT value is under 5, which is four
	// messages, not five.
	int32 GRetiredSelfWarnings = 0;
	int32 GRetiredPlayerWarnings = 0;
	constexpr int32 GRetiredWarningLimit = 5;

	// `DAT_1093d638` / `DAT_1093d63c` — `CNPC_VWerewolf`'s search-timer pair. FILE STATICS in retail,
	// shared by every werewolf on the map, which is the recovered fact and not an accident.
	uint64 GSearchTimerCycles = 0;

	// The rdtsc stand-in. This runtime has no cycle counter seam; `FPlatformTime::Cycles64` is the
	// same shape (a monotonic tick count) and the pair is a profiling aid with no game-visible
	// consumer, so the swap changes no event order. Named modernization.
	uint64 LifecycleCycles()
	{
		return FPlatformTime::Cycles64();
	}

	// `CAI_Hint::Spawn`'s per-hint-type default block (`0x102d0b60`). One row per recovered type,
	// with the hex the body writes beside each float.
	struct FHintSpawnDefaults
	{
		int32 HintTypeLo = 0;
		int32 HintTypeHi = 0;
		float TargetAngleRange = 0.f;
		float TargetDistMin = 0.f;
		float TargetDistMax = 0.f;
		float HintRating = 0.f;
		/** Bit 0x27d8 ADDS `_DAT_1049b998` to the angle range; every other row MULTIPLIES it by
		 *  `_DAT_104454d0`. Both constants are unrecovered, so both operations are identities here
		 *  and the arm that was taken is what the test states. */
		bool bAddsInsteadOfScales = false;
		int32 CategoryBits = 0;   // +0x474
		const TCHAR* Body = nullptr;
	};

	// `_DAT_104454d0` (the angle-range scale) and `_DAT_1049b998` (the `0x27d8` bias) have no reader
	// in the corpus that pins them. Identity and zero keep the recovered ARMS observable without
	// claiming a value.
	constexpr float GHintAngleRangeScale = 1.0f;   // _DAT_104454d0 — unrecovered
	constexpr float GHintAngleRangeBias = 0.0f;    // _DAT_1049b998 — unrecovered
	// `_DAT_1044eb08` — the degrees-to-radians factor the `fcos` is taken in. Recovered by its use.
	const float GHintDegToRad = PI / 180.f;

	constexpr FHintSpawnDefaults GHintSpawnRows[] =
	{
		// 100..0x65 (100..101) — the cover band. 0x42700000 = 60, 0x43800000 = 256, 0x7f7fffff =
		// FLT_MAX, 0x40400000 = 3.
		{ 100, 0x65, 60.f, 256.f, MAX_FLT, 3.f, false, 1, TEXT("0x102d0b60") },
		// 0x27d8 (10200) — the one row that ADDS its bias. 0x41880000 = 17.
		{ 0x27d8, 0x27d8, 17.f, 256.f, MAX_FLT, 3.f, true, 1, TEXT("0x102d0b60") },
		// 0x283c (10300). 0x42700000 = 60.
		{ 0x283c, 0x283c, 60.f, 256.f, MAX_FLT, 3.f, false, 4, TEXT("0x102d0b60") },
		// 0x283d (10301). 0x41200000 = 10, 0x42800000 = 64, 0x43800000 = 256.
		{ 0x283d, 0x283d, 10.f, 64.f, 256.f, 3.f, false, 8, TEXT("0x102d0b60") },
		// 0x28a0 (10400).
		{ 0x28a0, 0x28a0, 10.f, 64.f, MAX_FLT, 3.f, false, 0x10, TEXT("0x102d0b60") },
	};

	// `RandomFloat` through the one stream every NPC body here draws from — the same stream families
	// Hints and Conditions use, so the NPC's draws stay one reproducible sequence.
	float LifecycleRandomFloat(float Min, float Max)
	{
		return ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).FRandRange(Min, Max);
	}

	int32 LifecycleRandomInt(int32 Min, int32 Max)
	{
		return ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(Min, Max);
	}
}

// -------------------------------------------------------------------------------------------------
// The twelve Troika-line slots this family fills. Each carries the generated signature exactly.
// -------------------------------------------------------------------------------------------------

// slot 0 `void SetRefEHandle(const CBaseHandle&)` — 0x10027450
void FElysiumNpc::SetRefEHandle(const FElysiumEntityHandle& InHandle)
{
	// Retail's whole body is `*(undefined4*)&this->field_0x448 = *param_1;` — the four-byte entity
	// handle/serial word at `+0x0448`. This runtime's counterpart is `FElysiumEntity::Handle`, which
	// the world's registry binds at `Construct`; writing it is exactly what retail does, and the
	// registry stays the authority over what the handle RESOLVES to.
	Handle = InHandle;
}

// slot 3 `IServerNetworkable* GetNetworkable()` — 0x10027630
void* FElysiumNpc::GetNetworkable()
{
	// Retail's whole body is `return &this->field_0x2d4;`, the address of the embedded
	// `CServerNetworkProperty` sub-object. SEAM: this runtime has no network property and no
	// networkable interface — entities are plain C++ objects with no edict behind them — so there is
	// no sub-object whose address could be answered. Answers null and names the retail word.
	return nullptr;
}

// slot 4 `CBaseEntity* GetBaseEntity()` — 0x10027650
FElysiumEntity* FElysiumNpc::GetBaseEntity()
{
	// `return this;`
	return this;
}

// slot 91 `bool ShouldCollide(int, int) const` — 0x100b4de0
bool FElysiumNpc::ShouldCollide(int32 CollisionGroupArg, int32 ContentsMask) const
{
	// Verbatim: the answer is true unless `m_CollisionGroup == 1` (`+0x0368`) AND bit `0x4000000` of
	// the contents mask is CLEAR. Retail ignores its first argument entirely, and so does this.
	(void)CollisionGroupArg;
	if (CollisionGroup == GCollisionGroupDebris && (ContentsMask & GContentsDebrisOverride) == 0)
	{
		return false;
	}
	return true;
}

// slot 116 `bool IsMarkedForDeletion()` — 0x10027490
bool FElysiumNpc::IsMarkedForDeletion()
{
	// `return this->m_iEFlags & 1;` — bit 0 of the entity-flags word at `+0x0268`, `EFL_KILLME`.
	// This runtime spells "killed, and the world will reap the slot" as `FElysiumEntity::bDead`,
	// which `Kill()` is the sole writer of; that IS the killme bit.
	return (IsDead() ? GEntityFlagKillMe : 0) != 0;
}

// slot 137 `CBaseAnimating* GetBaseAnimating()` — 0x1004fc50
FElysiumEntity* FElysiumNpc::GetBaseAnimating()
{
	// `return this;` — retail's default answers itself. This leaf IS on the animating chain
	// (`FElysiumAnimating`), so the answer is the same entity.
	return this;
}

// slot 152 `float GetDelay()` — 0x1004fc10
float FElysiumNpc::GetDelay()
{
	// `return (float10)*(float *)(param_1 + 0x500);` — a plain read of the `CBaseDelay` field below
	// the NPC word table. SEAM (declared, unwritten): this runtime carries an output's delay on the
	// WIRE (`FElysiumOutputDef`) rather than on the entity, so nothing writes `EntityDelay`.
	return EntityDelay;
}

// slot 158 `bool IsAlive()` — 0x100b4dc0
bool FElysiumNpc::IsAlive()
{
	// `return this->m_lifeState == 0;` — `LIFE_ALIVE`, the int at `+0x0200`.
	//
	// This runtime has no `m_lifeState` word: it spells the same fact as two latches, and a body is
	// alive when NEITHER stands. `bDeathReported` is the death transaction's own one-shot
	// (`OnKilled` ran; the mind is dead and the death schedule is running), and `bDead` is `Kill`'s
	// terminal flag. `UpdateEnemyDistances` already reads `m_lifeState` through `IsDead()` for its
	// own arm, which is why the second term is spelled the same way here.
	return !HasReportedDeath() && !IsDead();
}

// slot 423 `bool IsTemplate()` — 0x1027e120
bool FElysiumNpc::IsTemplate()
{
	// `return (uint)this->m_spawnflags >> 0xb & 1;`
	return (SpawnFlags & GSpawnFlagTemplate) != 0;
}

// slot 471 `float GetReactionDelay()` — 0x1026a8a0
float FElysiumNpc::GetReactionDelay()
{
	// The whole body is `RandomFloat(0.2, 0.9)` (`0x3e4ccccd`, `0x3f666666`). It is the delay
	// `OnListened` (`0x1026a5e0`) queues a heard condition on `m_DelayedConditionList` with —
	// `docs/vtmb/npc-ai/senses.md` -> "OnListened". `FElysiumNpcSenses::TickHearing` dispatches it,
	// beside the `HEAR_FLINCH` variant `RandomFloat(0, 0.5)` that is NOT this slot.
	return LifecycleRandomFloat(GReactionDelayMin, GReactionDelayMax);
}

// slot 552 `bool ShouldFadeOnDeath()` — 0x1027a400
bool FElysiumNpc::ShouldFadeOnDeath()
{
	// `return (uint)this->m_spawnflags >> 9 & 1;`
	return (SpawnFlags & GSpawnFlagFadeOnDeath) != 0;
}

// slot 559 `CBaseEntity* FindNamedEntity(const char*)` — 0x10279090
FElysiumEntity* FElysiumNpc::FindNamedEntity(const TCHAR* Name)
{
	// The target-selector string dispatch, arm by arm in retail's order. EVERY arm that does not
	// resolve falls to the same tail, `return this` — a selector this NPC cannot answer resolves to
	// the NPC itself, which is what keeps a mistyped `.vcd` actor name pointing at a live entity.
	if (Name == nullptr)
	{
		return this;
	}
	FElysiumEntity* Player = World ? World->Resolve(World->PlayerHandle()) : nullptr;

	// 1. `!player` — `thunk_FUN_101cd9e0(1)`, `UTIL_PlayerByIndex(1)`.
	if (FCString::Stricmp(Name, GSelPlayer) == 0)
	{
		return Player;
	}
	// 2. `!playercontroller` — the player, then `thunk_FUN_101618a0` on it. A NULL player skips the
	//    whole arm and falls to the tail.
	if (FCString::Stricmp(Name, GSelPlayerController) == 0)
	{
		if (Player != nullptr)
		{
			return PlayerControllerOf(Player);
		}
		return this;
	}
	// 3. `!enemy` — vtable `+0x29c` (slot 167 `GetEnemy`), TWICE in retail: once as a null test and
	//    once for the answer. A null enemy falls to the tail.
	if (FCString::Stricmp(Name, GSelEnemy) == 0)
	{
		FElysiumEntity* Enemy =
			(World && Senses.Memory.Enemy.IsSet()) ? World->Resolve(Senses.Memory.Enemy) : nullptr;
		return Enemy != nullptr ? Enemy : static_cast<FElysiumEntity*>(this);
	}
	// 4. `!self` and `!target1` — matched, and then deliberately NOT handled: retail's `if` body is
	//    skipped and control reaches the tail, which is `this`. Named here because "the arm exists
	//    and answers the tail" is a different fact from "no arm matched".
	if (FCString::Stricmp(Name, GSelSelf) == 0 || FCString::Stricmp(Name, GSelTarget1) == 0)
	{
		return this;
	}
	// 5. `!nearestfriend` / `!friend` — both resolve the PLAYER. There is no friend search.
	if (FCString::Stricmp(Name, GSelNearestFriend) == 0 || FCString::Stricmp(Name, GSelFriend) == 0)
	{
		return Player;
	}
	// 6. The retired bare `self` — rate-limited to four `DevMsg`s (retail tests the POST-increment
	//    against 5), then the tail either way.
	if (FCString::Stricmp(Name, GSelRetiredSelf) == 0)
	{
		if (++GRetiredSelfWarnings < GRetiredWarningLimit)
		{
			UE_LOG(LogElysiumNpcEnt, Warning,
				TEXT("ERROR: \"self\" is no longer used, use \"!self\" in vcd instead!"));
		}
		return this;
	}
	// 7. The retired bare `Player` — the same rate limit on its own counter, and then the PLAYER.
	if (FCString::Stricmp(Name, GSelRetiredPlayer) == 0)
	{
		if (++GRetiredPlayerWarnings < GRetiredWarningLimit)
		{
			UE_LOG(LogElysiumNpcEnt, Warning,
				TEXT("ERROR: \"player\" is no longer used, use \"!player\" in vcd instead!"));
		}
		return Player;
	}
	// 8. Anything else is a plain name lookup (`CGlobalEntityList::FindEntityByName`), and a miss
	//    falls to the tail.
	if (World != nullptr)
	{
		if (FElysiumEntity* Found = World->FindByName(FString(Name)))
		{
			return Found;
		}
	}
	return this;
}

FElysiumEntity* FElysiumNpc::PlayerControllerOf(FElysiumEntity* Player) const
{
	// SEAM for `thunk_FUN_101618a0(player)`, the `!playercontroller` resolve. Retail hands back the
	// player's own scene stand-in; this runtime stands that as the `npc_VPlayerController` leaf
	// (`FElysiumPlayerControllerNpc`), which a map places by name and which the player holds no
	// pointer to. Answers the player itself, which is the arm that keeps the selector resolving to a
	// live entity, and names what would settle it: a back-pointer from the player to its duplicate.
	return Player;
}

// -------------------------------------------------------------------------------------------------
// Slots 412/413/414 — the three think stamps this runtime already carries.
// -------------------------------------------------------------------------------------------------

float FElysiumNpc::LastUpdateThink() const
{
	// 0x101a6440 — `CBaseEntity::GetLastThink` for the update channel.
	return static_cast<float>(ScheduleHost.LastUpdate);
}

float FElysiumNpc::LastNormalThink() const
{
	// 0x101a6460 — the normal channel.
	return static_cast<float>(ScheduleHost.LastNormal);
}

float FElysiumNpc::LastMoveThink() const
{
	// 0x101a6480 — the move channel.
	return static_cast<float>(ScheduleHost.LastMove);
}

// -------------------------------------------------------------------------------------------------
// Slots 77 / 78 / 119 — dormancy, by species.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::HintScriptHide(FHintWords& Hint)
{
	// 0x102d0860. The base `CBaseEntity::ScriptHide` half belongs to the ENTITY and is
	// `FElysiumEntity::ScriptHide`; there is no `CAI_Hint` entity here, so this body is the hint's
	// own half and nothing else.
	Hint.Disabled = 1;
}

void FElysiumNpc::HintScriptUnhide(FHintWords& Hint)
{
	// 0x102d0890 — the exact inverse.
	Hint.Disabled = 0;
}

void FElysiumNpc::HintKill(FHintWords& Hint)
{
	// 0x102d08c0, whose whole body is `MOV EAX,[ECX] / JMP [EAX + 0x134]`. `+0x134` is slot 77, so
	// Kill on a hint node IS ScriptHide. Not "like" it: the same address is reached.
	HintScriptHide(Hint);
}

void FElysiumNpc::HintDeletingDestructor()
{
	// 0x102d2f00 -> 0x102d3040. `this` is the node's OWNER — retail resolves it out of the hint's
	// `m_hOwner` and takes the `m_pNPC` (+0x98) off it; here the owner is the receiver, so the two
	// resolves are the caller's and what is left is the pair of writes, in retail's order.
	//
	//     SetCondition(owner, 0x29);                 // 0x10269a20, FIRST
	//     CAI_BaseNPCTroika::ClearHintNode(owner, 0.0);
	//
	// The condition is raised BEFORE the reference is dropped, and the reuse delay is ZERO: the node
	// is being destroyed, so there is nothing to hold a cooldown against. Both are retail's.
	Cognition.Conditions.Set(static_cast<EElysiumNpcCond>(GHintDestroyedCondition));
	ClearScheduleHint(GHintDestroyedReuseDelay);

	// SEAM: the rest of `0x102d3040` is the engine's global hint-list unlink and the node's own
	// storage teardown. This substrate carries hint nodes as rows, not as engine objects on a list,
	// so there is no list here and no kernel-observable state in that half.
}

FElysiumNpc::FCineUnhideRecord FElysiumNpc::TroikaScriptUnhideTail()
{
	FCineUnhideRecord Record;

	// 1. Slot 614 `ResetThinkTimers`, dispatched right after `CBaseEntity::ScriptUnhide`
	//    (vtable `+0x998`). ALREADY CARRIED: `FElysiumNpc::OnDormancyChanged` runs it on the waking
	//    edge and cites this exact address. Not repeated, so the stamps are not written twice.

	// 2. The active weapon's own slot 78 (`weapon->vtable+0x138`) — retail UNHIDES the weapon, it
	//    does not holster it. `GetActiveWeapon()` is called twice in the listing, once as the null
	//    test and once for the dispatch.
	if (World != nullptr && Inventory.ActiveWeapon.IsSet())
	{
		if (FElysiumEntity* Weapon = World->Resolve(Inventory.ActiveWeapon))
		{
			Weapon->ScriptUnhide();
		}
	}

	// 3. The cine hand-back: when `m_bCineScriptHidden` (`+0x5d78`) stands and `m_hCine`
	//    (`+0x5d74`) resolves, six words of THIS body's state are written onto the cine at
	//    `+0x5f78`..`+0x5f8c`, in this order — slot 94 `GetMoveType`, slot 95 `GetMoveCollide`,
	//    slot 92 `GetSolid`, slot 211 `GetSolidFlags`, `m_fEffects`, `m_bfAINPCFlags`.
	//
	//    `+0x5d78` is its OWN byte (story 29e rebound it as `FElysiumNpc::bCineScriptHidden`, which
	//    `NPCInit` clears at `102735xx` and this body clears on both arms), distinct from
	//    `m_bScriptHidden` (`+0x0f4`) and from `m_fEffects`. Its SETTER — retail's `ScriptHide` —
	//    is not ported yet, so the latch would never stand; until it is, the condition is read from
	//    the entity's own hidden flag, which is this runtime's only live spelling of "hidden by a
	//    script". The beat (`FElysiumScriptedSequence`) carries no `+0x5f78` block, so the record is
	//    RETURNED rather than written.
	const bool bCineLatchStands = bHidden;
	FElysiumEntity* Cine = (World && ScriptOwner.IsSet()) ? World->Resolve(ScriptOwner) : nullptr;
	if (bCineLatchStands && Cine != nullptr)
	{
		Record.bWroteToCine = true;
		Record.MoveType = GetMoveType();
		Record.MoveCollide = GetMoveCollide();
		Record.Solid = GetSolid();
		Record.SolidFlags = GetSolidFlags();
		// `m_fEffects` and `m_bfAINPCFlags` both stay 0 here: this runtime carries no `m_fEffects`
		// word at all (rendering effects are the body's, not the entity's), and `FElysiumNpcFlags`
		// keeps its two raw words private — no ported reader wants the whole word, only named bits.
		// Recorded as the two words retail writes so the day a consumer exists it fills them.
		Record.Effects = 0;
		Record.NpcFlagWord = 0;
	}
	// 4. `m_bCineScriptHidden = 0` on BOTH arms — the latch is cleared whether or not a cine took
	//    the record (`102c1fxx` and the tail).
	bCineScriptHidden = false;
	//
	// **Unrecovered:** the four vtable dispatches ahead of `CBaseEntity::ScriptUnhide` in the
	// listing (the same slots 94/95/92/211, results discarded by the decompiler). They are the same
	// sequence in the same order as the recorded one, so either the compiler hoisted the reads or
	// retail restores them from the cine first; nothing in the corpus settles which, and this body
	// reproduces only the sequence whose destination is stated.
	return Record;
}

void FElysiumNpc::WerewolfScriptUnhideTail(double Now)
{
	// 0x103d4a20 — the base, then four writes in this order: the stamp first, then the three timers
	// high-to-low. Retail's order is preserved because a reader between them would see it.
	WerewolfUnhideStamp = Now;   // +0x66ec := curtime (DAT_1070b228+0xc)
	WerewolfMorphTimerC = 0.f;   // +0x66d8
	WerewolfMorphTimerB = 0.f;   // +0x66d4
	WerewolfMorphTimerA = 0.f;   // +0x66a4
}

void FElysiumNpc::GhoulCroucherScriptUnhideTail()
{
	// 0x1037c2f0 — the base, then `m_hBurningParticle` (`+0x6670`) resolved through the handle table
	// and its vtable `+0x138` (slot 78) dispatched when it resolves to a live entity. Retail does
	// NOT clear the handle, so neither does this.
	if (World == nullptr || !BurningParticle.IsSet())
	{
		return;
	}
	if (FElysiumEntity* Particle = World->Resolve(BurningParticle))
	{
		Particle->ScriptUnhide();
	}
}

FElysiumNpc::FCineUnhideRecord FElysiumNpc::ScriptUnhideSpecies(double Now)
{
	// The Troika tail is slot 78 for 62 classes and runs first on every one of them, because both
	// species overrides call it before their own tail.
	const FCineUnhideRecord Record = TroikaScriptUnhideTail();
	if (IsRetailClass(TEXT("CNPC_VWerewolf")))
	{
		WerewolfScriptUnhideTail(Now);
	}
	else if (IsRetailClass(TEXT("CNPC_VGhoulCroucher")))
	{
		GhoulCroucherScriptUnhideTail();
	}
	return Record;
}

// -------------------------------------------------------------------------------------------------
// Slot 103 `Spawn` — the species bodies.
// -------------------------------------------------------------------------------------------------

FElysiumNpc::FCineSpawnState FElysiumNpc::CineSpawn(int32 InSpawnFlags, bool bNamed, double Now)
{
	FCineSpawnState State;
	// 0x101a6f10, in retail's own order.
	State.Solid = 0;                 // SetSolid(SOLID_NONE)
	State.AddedSolidFlags = 4;       // AddSolidFlags(flags | 4) — FSOLID_NOT_SOLID
	State.bTargetable = false;       // m_bIsBCCTargetable = 0
	State.bAlive = false;            // m_bIsAlive = 0
	// The auto-remove think: an UNNAMED cine, or spawnflag 0x10 on a named one.
	if (!bNamed || (InSpawnFlags & GCineSpawnFlagAutoRemove) != 0)
	{
		State.bAutoRemoveThink = true;
		State.NextThink = Now + GCineAutoRemoveDelaySeconds;
		// Only a NAMED cine that armed the think also takes a start time.
		if (bNamed)
		{
			State.bHasStartTime = true;
			State.StartTime = Now + GCineStartTimeOffsetSeconds;
		}
	}
	// Spawnflag 0x20 CLEARS interruptable; its absence sets it.
	State.bInterruptable = (InSpawnFlags & GCineSpawnFlagNotInterruptable) == 0;
	State.SequenceStarted = 0;
	State.bNextCineSet = false;      // m_hNextCine = 0xffffffff
	State.AddedFlags2 = 0x10;        // AddFlag2(0x10)
	return State;
}

double FElysiumNpc::StandoffGoalSpawnNextThink(double Now)
{
	// 0x102cd2d0 — `ThinkSet(&LAB_10006672, 0, NULL)` and then
	// `m_flNextThink = curtime + _DAT_1044e658`. The think function is a stub label with no body in
	// the corpus, so what is recovered is the CLOCK and nothing else.
	return Now + GStandoffGoalThinkDelaySeconds;
}

void FElysiumNpc::HintSpawn(FHintWords& Hint)
{
	// 0x102d0b60, per `docs/vtmb/npc-ai/authored-control.md` -> "The navigation and reaction
	// keyfields". Retail's own order:
	// SetSolid(0) and Relink first (the entity half, which this runtime's hint has no body for),
	// then the per-type default block, then the group fold.
	const FHintSpawnDefaults* Row = nullptr;
	for (const FHintSpawnDefaults& Candidate : GHintSpawnRows)
	{
		if (Hint.HintType >= Candidate.HintTypeLo && Hint.HintType <= Candidate.HintTypeHi)
		{
			Row = &Candidate;
			break;
		}
	}
	if (Row != nullptr)
	{
		// Each of the four floats is filled ONLY when it still carries the `-1.0` unset sentinel —
		// an authored value always wins. (`_DAT_104454c4` is the shared 0.0f the comparison is
		// against; the sentinel the keyfields leave is what family Hints' `FHintWords` defaults to,
		// so the test is "still at the default".)
		if (Hint.TargetAngleRange == GLifeZero) { Hint.TargetAngleRange = Row->TargetAngleRange; }
		if (Hint.TargetDistMin == GLifeZero) { Hint.TargetDistMin = Row->TargetDistMin; }
		if (Hint.TargetDistMax == GLifeZero) { Hint.TargetDistMax = Row->TargetDistMax; }
		if (Hint.HintRating == GLifeZero) { Hint.HintRating = Row->HintRating; }
		// The 100..0x65 row carries retail's one duplicated test: a STILL-unset `m_flTargetDistMin`
		// re-writes `m_flHintRating` (not the distance) with 3.0. Transcribed because it is what
		// shipped, and because it is unreachable after the fill above — which is the point.
		if (Row->HintTypeLo == 100 && Hint.TargetDistMin == GLifeZero)
		{
			Hint.HintRating = Row->HintRating;
		}
		// The angle range becomes its own dot-product test: scale (or, for 0x27d8, bias) then cos.
		Hint.TargetAngleRange = Row->bAddsInsteadOfScales
			? Hint.TargetAngleRange + GHintAngleRangeBias
			: Hint.TargetAngleRange * GHintAngleRangeScale;
		Hint.TargetAngleRangeDot = FMath::Cos(Hint.TargetAngleRange * GHintDegToRad);
		// `+0x474`'s category bitmask, per type. `FHintWords` carries no such word — it is read by
		// no ported body — so the bits are recorded here and nowhere else.
		(void)Row->CategoryBits;
	}
	// The group fold, which runs for EVERY hint including one with no default row: 1..32 becomes a
	// single bit, anything else becomes -1 (every group). Note that `CAI_InterestingPlace::Spawn`
	// makes the same fold answer 1 instead of -1 for an out-of-range id; the two are different.
	Hint.GroupMask = (Hint.GroupMask > 0 && Hint.GroupMask < 0x21)
		? (1 << (Hint.GroupMask - 1))
		: -1;
}

bool FElysiumNpc::ConversationPlaceSpawnPrecachesLast()
{
	// 0x102dbc80: `CALL 0x10001f28` (its own field init) then `JMP [[this]+0x1a0]` — `+0x1a0` is
	// slot 104. The spawn's LAST act is its precache, which is the reverse of every other class.
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 104 `Precache` — the species bodies.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::ConversationPlacePrecache(const FString& SoundLoop, const FString& SoundOnce,
	TArray<FPrecacheRequest>& Out)
{
	// 0x102dbcb0. `m_iszSoundLoop` (`+0x0454`) is warned about when it is empty and is then NOT
	// precached; `m_iszSoundOnce` (`+0x0458`) is precached unconditionally and never warned. The
	// asymmetry is retail's and is the recovered fact.
	if (SoundLoop.IsEmpty())
	{
		FPrecacheRequest Warn;
		Warn.Name = SoundLoop;
		Warn.bWarnedInvalid = true;
		Out.Add(MoveTemp(Warn));
	}
	else
	{
		Out.Add(FPrecacheRequest{ SoundLoop, /*bModel=*/false, /*bWarnedInvalid=*/false });
	}
	Out.Add(FPrecacheRequest{ SoundOnce, /*bModel=*/false, /*bWarnedInvalid=*/false });
}

const TCHAR* const* FElysiumNpc::GenericNpcWeaponSounds(int32& OutCount)
{
	OutCount = UE_ARRAY_COUNT(GGenericNpcWeaponSounds);
	return GGenericNpcWeaponSounds;
}

void FElysiumNpc::GenericNpcPrecache(const FString& InModel, TArray<FPrecacheRequest>& Out)
{
	// 0x1034aa40: the three-entry weapon-sound table first, in table order, then this entity's own
	// model through the MODEL precacher.
	int32 Count = 0;
	const TCHAR* const* Sounds = GenericNpcWeaponSounds(Count);
	for (int32 i = 0; i < Count; ++i)
	{
		Out.Add(FPrecacheRequest{ FString(Sounds[i]), /*bModel=*/false, /*bWarnedInvalid=*/false });
	}
	Out.Add(FPrecacheRequest{ InModel, /*bModel=*/true, /*bWarnedInvalid=*/false });
}

FString FElysiumNpc::CameraPrecacheModel(const FString& AuthoredModel)
{
	// 0x103689c0, shared with `CNPC_VCameraSecurity`: the model key falls back to `models/null.mdl`
	// through vtable `+0x350` (`SetModelName`) when it is unset OR empty — retail reads the key
	// three times to decide, which is one question.
	//
	// **Not ported here:** the tail. After `PrecacheModel` the body dispatches slot 452 (`+0x710`),
	// rejects the spawn outright (`Msg("ERROR: Rejecting spawn of %s as e...")` plus
	// `thunk_FUN_101cd940`) when it answers false, zeroes `m_iInterestingPlaceGroups` (`+0x62dc`),
	// and then runs the AI-node link-table integrity check (`0x102f9970` / `0x102f9920` /
	// `0x102f9950`) that `DevMsg`s "is being spawned after links have been...". **Unrecovered here:**
	// this substrate has no AI node graph and no link table, so there is nothing to check; the
	// rejection arm needs slot 452, which is a later story's.
	return AuthoredModel.IsEmpty() ? FString(GCameraNullModel) : AuthoredModel;
}

// -------------------------------------------------------------------------------------------------
// Slot 110 `KeyValue` — the AI-helper-entity cascade.
// -------------------------------------------------------------------------------------------------

FElysiumNpc::EKeyValueArm FElysiumNpc::ClassifyKeyValue(const FString& Key, FString& OutKey)
{
	// 0x1009e430, arm by arm. The FIRST thing the body does is truncate the key at a `#`
	// (`FUN_10431f30(param_1, '#')` then `*p = 0`), so every comparison below sees the truncated
	// name — and `"origin#2"` is `"origin"`.
	int32 Hash = INDEX_NONE;
	OutKey = Key.FindChar(TEXT('#'), Hash) ? Key.Left(Hash) : Key;

	// Retail guards the first two with `if (*param_1 == 'r')`, which is an optimisation and not a
	// rule: a key that reaches them starts with `r` by definition.
	if (OutKey.Equals(TEXT("rendercolor"), ESearchCase::IgnoreCase)
		|| OutKey.Equals(TEXT("rendercolor32"), ESearchCase::IgnoreCase))
	{
		return EKeyValueArm::RenderColor;
	}
	if (OutKey.Equals(TEXT("renderamt"), ESearchCase::IgnoreCase))
	{
		return EKeyValueArm::RenderAmt;
	}
	if (OutKey.Equals(TEXT("disableshadows"), ESearchCase::IgnoreCase))
	{
		return EKeyValueArm::DisableShadows;
	}
	if (OutKey.Equals(TEXT("mins"), ESearchCase::IgnoreCase))
	{
		return EKeyValueArm::Mins;
	}
	if (OutKey.Equals(TEXT("maxs"), ESearchCase::IgnoreCase))
	{
		return EKeyValueArm::Maxs;
	}
	if (OutKey.Equals(TEXT("disablereceiveshadows"), ESearchCase::IgnoreCase))
	{
		return EKeyValueArm::DisableReceiveShadows;
	}
	if (OutKey.Equals(TEXT("angle"), ESearchCase::IgnoreCase))
	{
		return EKeyValueArm::Angle;
	}
	if (OutKey.Equals(TEXT("angles"), ESearchCase::IgnoreCase))
	{
		return EKeyValueArm::Angles;
	}
	if (OutKey.Equals(TEXT("origin"), ESearchCase::IgnoreCase))
	{
		return EKeyValueArm::Origin;
	}
	// Nothing matched: retail walks the datamap chain (`vtable +0x148 GetDataDescMap`, following
	// `baseMap` at `+0xc`) and offers the key to each level's `ParseKeyvalue`. THIS RUNTIME ALREADY
	// DOES THAT — `FElysiumEntity::Construct` applies each raw keyvalue through the class-chain
	// field table (`FElysiumClassRegistry::FindField`), which is the same walk over the same data.
	// So this arm names the port's own path rather than standing a second one.
	//
	// **Not ported:** the `ent_debugkeys` cvar arm (`DAT_106cf424`), which `Msg`s every matched and
	// unmatched key for one classname. It is a console diagnostic with no game-visible effect.
	return EKeyValueArm::DataMap;
}

FString FElysiumNpc::RewriteAngleKey(float AngleValue, const FVector& CurrentAngles)
{
	// 0x1009e430's `angle` arm: `atof` the value, and then — this is the arm order, not a tidy-up —
	// a value BELOW `_DAT_104454c4` (0.0f) takes the `__ftol` + `Q_strncpy` literal path, while any
	// other value composes `"%f %f %f"` from `GetAbsAngles()[0]`, the VALUE, and `GetAbsAngles()[2]`.
	// The yaw alone is replaced. Retail then re-enters the cascade with the key `angles`.
	//
	// **Unrecovered:** the literal the negative arm copies. `Q_strncpy`'s source is folded away in
	// the decompilation and the `.rdata` it would name is not pinned; Source's own convention is
	// that `angle -1` means "up" and `angle -2` "down", which this does NOT claim. The negative arm
	// therefore answers the same composition, and says so.
	return FString::Printf(TEXT("%f %f %f"),
		static_cast<float>(CurrentAngles.X), AngleValue, static_cast<float>(CurrentAngles.Z));
}

// -------------------------------------------------------------------------------------------------
// Slot 113 `Activate`.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::ClassifyIsNonZero() const
{
	// SEAM for slot 138 `Classify()`. Retail's gate is `Classify() != 0`; the classes that answer 0
	// are the inert helpers that share the `CAI_BaseNPC` vtable (`CAI_Hint`, `CAI_TestHull`,
	// `CScriptedTarget`), none of which this runtime stands on this leaf. Every entity that reaches
	// `FElysiumNpc::Activate` is a living NPC, so the gate passes.
	return true;
}

void FElysiumNpc::ApplyDefaultDispositionOnActivate()
{
	// 0x1028e310, the whole Troika `Activate` past `CBaseEntity::Activate`: when `Classify()` is
	// non-zero, apply `m_sDefaultDisposition` (`+0x6558`) — or the EMPTY string when the keyfield is
	// unset, which retail substitutes explicitly — through `SetDisposition(name, 1)`.
	//
	// The level is retail's literal `1`, which is what `default_disposition starts at level 1` on
	// `FElysiumAnimating::DispositionLevel` already records.
	if (!ClassifyIsNonZero())
	{
		return;
	}
	SetDisposition(Disposition, 1);
}

TArray<FElysiumEntityHandle> FElysiumNpc::ConversationPlaceActivate(const FString& PlacesName,
	bool bEnabled, TArray<FString>* OutRejected) const
{
	// 0x102dbde0. `m_iszInterestingPlaces` (`+0x0450`) names a TARGETNAME, and retail walks every
	// entity carrying it (`FindEntityByName` from the previous hit, not from the head) — so one name
	// admits many places. A hit is admitted when it RTTI-casts to the conversation interesting-place
	// type AND its `field[0x161]` is 1; everything else is `DevWarning`ed and dropped.
	//
	// `+0x04fc` (the admitted count) is zeroed FIRST, before the walk.
	TArray<FElysiumEntityHandle> Admitted;
	if (World == nullptr || PlacesName.IsEmpty())
	{
		// Retail substitutes the empty string for an unset name and looks THAT up, which finds
		// nothing; the arm below (`m_bEnabled`) still runs, which is why this does not return early
		// past it.
		(void)bEnabled;
		return Admitted;
	}
	FElysiumEntityWorld& MutableWorld = *const_cast<FElysiumEntityWorld*>(World);
	if (FElysiumEntity* Hit = MutableWorld.FindByName(PlacesName))
	{
		// The `___RTDynamicCast` to the conversation interesting-place type: this runtime recognises
		// an interesting place by its REGISTERED CLASSNAME, `intersting_place` (retail's own
		// misspelling), because entities here carry no RTTI. The `field[0x161] == 1` term beside it
		// is the leaf's own enabled byte, which `FElysiumInterestingPlace::bEnabled` carries.
		const bool bIsPlace =
			Hit->Def != nullptr && Hit->Def->Classname == TEXT("intersting_place");
		const FElysiumInterestingPlace* Place =
			bIsPlace ? static_cast<const FElysiumInterestingPlace*>(Hit) : nullptr;
		if (Place != nullptr && Place->bEnabled)
		{
			Admitted.Add(Hit->Handle);
		}
		else if (OutRejected != nullptr)
		{
			// `DevWarning("%s %s is not a valid target for ...")`.
			OutRejected->Add(Hit->DebugString());
		}
	}
	// The tail: `m_bEnabled` either re-arms the conversation think (`thunk_FUN_102dc3e0`) or turns
	// the think off outright (`ThinkSet(NULL, 0, NULL)`). SEAM — this runtime has no conversation
	// interesting place, so there is no think to arm; the admitted list is the whole answer.
	return Admitted;
}

// -------------------------------------------------------------------------------------------------
// Slots 127 / 130 — save and restore.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpc::RestoreExtendedHeader(void* Archive)
{
	// 0x1027c160, slot 127 on the `CAI_BaseNPC` line, read off the LISTING — see the `.inl` for what
	// story 29d corrected here. In retail's order:
	//
	//   1. `1027c178 CALL [IRestore + 0x8]` reads `AIExtendedSaveHeader_t` (datamap `0x105cabd0`)
	//      into `field_0x19b4`. This runtime's header is `FElysiumNpc::FAiExtendedSaveHeader` and
	//      `LastSavedExtendedHeader` is `+0x19b4`; the four words come back through the same seam
	//      `BaseSave` wrote them through (`ElysiumNpcKernelSaveRestore10.cpp`).
	//   2. `1027c17e CALL CBaseCombatCharacter::Restore` — and EDI keeps its answer, which is this
	//      body's return value.
	//   3. `1027c189 PUSH 0x4 / LEA EDX,[ESI+0x5b8c]` and `1027c199 PUSH 0x3 / LEA EAX,[ESI+0x5db4]`
	//      — TWO sentinel decodes, `m_flExtendedBlockedByFriendTimer` at mode 4 and
	//      `m_flWaitFinished` at mode 3. Not a clock re-base and not seven fields.
	//   4. `thunk_FUN_102e0b80(m_pMotor)` when a motor stands, and `thunk_FUN_102e8ac0` on the
	//      move-and-shoot overlay — both re-link a saved pointer. This runtime rebuilds the motor
	//      from the def on load and binds no overlay at all
	//      (`ElysiumNpcKernelShapeMap.cpp` `+0x5cf4` ABSENT), so neither has a pointer to re-link;
	//      family SaveRestore10 counts both.
	FAiExtendedSaveHeader Header;
	if (FElysiumSaveArchive* Ar = static_cast<FElysiumSaveArchive*>(Archive))
	{
		*Ar << Header.Version;
		*Ar << Header.Flags;
		*Ar << Header.ScheduleName;
		*Ar << Header.ScheduleCrc;
		LastSavedExtendedHeader = Header;
	}
	SaveArchiveLog.Add({ FSaveArchiveOp::EKind::Fields, TEXT("AIExtendedSaveHeader_t"),
		static_cast<int32>(Header.Flags) });

	const int32 ChainResult = 1;   // `CBaseCombatCharacter::Restore`; see `GChainRestoreResult`.

	SaveStampDecode(ExtendedBlockedByFriendTimer, ESaveStampMode::FloatMax);   // +0x5b8c, mode 4
	SaveStampDecode(ScheduleHost.WaitFinished, ESaveStampMode::Zero);          // +0x5db4, mode 3

	if (Motor != nullptr)
	{
		++MotorRestoreFixups;
	}
	++MoveAndShootRestoreFixups;
	return ChainResult;
}

bool FElysiumNpc::OnRestoreForwardsCheckUntouch(bool bCallerValue)
{
	// 0x100aa5a0. The whole body, from the listing:
	//
	//     MOV EAX, [ECX]
	//     MOV dword ptr [ESP + 0x4], 0x0
	//     JMP [EAX + 0x18]
	//
	// `+0x18` is slot 6 `SetCheckUntouch(bool)`. The argument on the stack is OVERWRITTEN with 0
	// before the jump, so a restore always lands as `SetCheckUntouch(false)` however it was called.
	(void)bCallerValue;
	return false;
}

// -------------------------------------------------------------------------------------------------
// Slot 175 `Touch`, slot 180 `UpdateOnRemove`.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::CineTouch(FElysiumEntity* Other)
{
	// 0x101a75a0, shared by `CCineAI`, `CCineAISchedule` and `CCineNPC`: `return;`, parameter
	// ignored, no chain to the base. Nothing happens when something touches a cine actor, and
	// nothing else gets a chance to.
	(void)Other;
	return true;
}

void FElysiumNpc::BaseNpcUpdateOnRemove()
{
	// 0x1027ca30 — slot 180 for the NON-Troika branch (`CAI_BaseNPC`, `CScriptedTarget`,
	// `CNPC_Bullseye`, `CNPC_Crow`, the test hulls). The Troika override `0x1028d6e0` the port's
	// slot table cites is a DIFFERENT body; this is the one below it, in retail's order:
	//
	//   1. The squad unlink (`+0x5da4`): `thunk_FUN_10315ec0` asks whether this NPC is in the squad
	//      and `thunk_FUN_103158f0` removes it. SEAM — `ConnectedSquad()` answers nothing on this
	//      substrate (no squad object, 0002/17), so there is no list to leave.
	//   2. The hint release (`+0x5ddc`): `thunk_FUN_102d1420(hint, 0.0)` with a ZERO reuse delay,
	//      then `m_pHintNode = 0`. `ClearScheduleHint` is that pair, and 0.0 is the delay.
	//   3. This class's own vtable `+0x7fc` (slot 511).
	//   4. `CBaseCombatCharacter::UpdateOnRemove` — the chain's, which this runtime runs from
	//      `FElysiumEntity::Kill`.
	if (ConnectedSquad() != nullptr)
	{
		// Unreachable today by construction; stated so the squad layer replaces the body and not the
		// call site.
	}
	ClearScheduleHint(0.0f);
	// Slot 511 (`+0x7fc`) is a later story's; nothing routes to it yet.
}

// -------------------------------------------------------------------------------------------------
// Slot 434 `PrescheduleThink`, slot 512 `GetExpresser`, slot 585.
// -------------------------------------------------------------------------------------------------

FElysiumNpc::EPrescheduleSpecies FElysiumNpc::PrescheduleSpecies() const
{
	// 0x10369100 (`CNPC_VCamera`, shared with `CNPC_VCameraSecurity`) is an EMPTY body — a camera
	// does no preschedule work. 0x103a7650 (`CNPC_VSabbatLeader`) is the scope trace and an
	// unconditional forward to `CNPC_VAndreiBlood::PrescheduleThink` (`0x10385a30`), family Bosses'.
	if (IsRetailClass(TEXT("CNPC_VCamera")))
	{
		return EPrescheduleSpecies::Empty;
	}
	if (IsRetailClass(TEXT("CNPC_VSabbatLeader")))
	{
		return EPrescheduleSpecies::ForwardToAndreiBlood;
	}
	return EPrescheduleSpecies::Base;
}

void* FElysiumNpc::ExpressiveNpcExpresser() const
{
	// 0x10260da0 — `return *(undefined4 *)((int)this + 0x5f48);`. SEAM: `+0x5f48` is unbound in the
	// shape map and this runtime stands no expression substrate, so there is no expresser object to
	// point at. Null is also what the BASE body (`0x101a6b20`) answers, so a caller that dispatches
	// slot 512 sees the base's answer rather than an invented one.
	return nullptr;
}

bool FElysiumNpc::ValidHeadTarget(const FVector& EyePointCm) const
{
	// SEAM for `ValidHeadTarget` (vtable `+0x930`), the predicate every `PickLookTarget` arm applies
	// before accepting a candidate. Answers TRUE — retail's "accept" — so the arm ORDER stays
	// observable; a false here would empty every arm and make the body untestable.
	(void)EyePointCm;
	return true;
}

FElysiumNpc::FLookTargetPick FElysiumNpc::PickLookTarget(bool bExcludePlayers, float MinTime,
	float MaxTime) const
{
	// 0x1025f1c0, the body that occupies `ProcessTweakParam`'s physical slot in the base branch:
	// `CAI_BaseActor::PickLookTarget(bExcludePlayers, minTime, maxTime)`. `MaintainEyeDirection`
	// dispatches it with `(false, 1.5, 2.5)`.
	//
	// Arm order, read off the decompiled C:
	//
	//   1. THE ENEMY. `GetEnemy()` (vtable `+0x29c`). When it stands, and either `FVisible` (mask
	//      `0x2804091`) passes or `RandomInt(0,3) == 0`, and `ValidHeadTarget(enemy->EyePosition())`
	//      accepts, the pick is the enemy with importance `RandomFloat(0.7, 1.0)` and the CALLER's
	//      durations. Otherwise the arms below run with the durations re-drawn to
	//      `RandomFloat(0.5, 0.8)` / `0.2`.
	//   2. THE NAVIGATION GOAL. A navigator with a path, `RandomInt(1,10) < 4`, and a goal further
	//      than `_DAT_10497cb0` away: the pick is the goal POINT (importance `RandomFloat(0.2,0.4)`
	//      when `RandomInt(1,10) < 6`, else `RandomFloat(1.0, 2.0)`).
	//   3. THE SCAN. Skipped entirely when slot 464 `GetState()` is 2 (COMBAT) and `RandomInt(1,10)`
	//      is 8 or under. Otherwise a 1024-unit entity query around `GetAbsOrigin()`, rejecting
	//      self, rejecting a player when `bExcludePlayers`, preferring a character
	//      (`GetFlags() & 0x80`) that passes `FVisible` and `ValidHeadTarget`, and otherwise scoring
	//      each candidate against `RandomInt(1,100)` scaled by 10 for a live entity and 100 for one
	//      that answers slot 152.
	//
	// SEAMS, named: there is no entity-in-radius query on this substrate's NPC leaf, no navigator
	// goal other than `MoveGoal` (the feet destination of the move in flight), and no head-target
	// cone. What lands is arms 1 and 2, which have their inputs here; arm 3 answers nothing and says
	// so. `_DAT_10497cb0`, the goal-distance floor, is **unrecovered** (two readers, both in this
	// body) and is 0 here, which is the arm that ACCEPTS any goal.
	FLookTargetPick Pick;
	Pick.MinDuration = MinTime;
	Pick.MaxDuration = MaxTime;

	FElysiumEntity* Enemy =
		(World && Senses.Memory.Enemy.IsSet())
			? const_cast<FElysiumEntityWorld*>(World)->Resolve(Senses.Memory.Enemy)
			: nullptr;
	if (Enemy != nullptr)
	{
		const bool bVisible = FElysiumNpcSenses::IsVisible(*this, *Enemy,
			World ? World->NowSeconds() : 0.0);
		if ((bVisible || LifecycleRandomInt(0, 3) == 0) && ValidHeadTarget(Enemy->EyePosition()))
		{
			Pick.Target = Enemy->Handle;
			Pick.Importance = LifecycleRandomFloat(0.7f, 1.0f);
			Pick.Arm = FLookTargetPick::EArm::Enemy;
			return Pick;
		}
		// Retail re-draws the durations on the way past the enemy arm, and the later arms use THOSE
		// rather than the caller's.
		Pick.MinDuration = LifecycleRandomFloat(0.5f, 0.8f);
		Pick.MaxDuration = 0.2f;
	}

	// Arm 2 — the navigator's goal. `bMoveIssued`'s destination is the port's `MoveGoal`.
	if (bMoveIssued && LifecycleRandomInt(1, 10) < 4)
	{
		Pick.Importance = LifecycleRandomInt(1, 10) < 6
			? LifecycleRandomFloat(0.2f, 0.4f)
			: LifecycleRandomFloat(1.0f, 2.0f);
		Pick.Arm = FLookTargetPick::EArm::NavigationGoal;
		return Pick;
	}

	// Arm 3 — the scan. SEAM: no radius query here. `bExcludePlayers` is carried so the day the
	// query lands the filter is already stated.
	(void)bExcludePlayers;
	return Pick;
}

// -------------------------------------------------------------------------------------------------
// The free functions and the unnamed tables.
// -------------------------------------------------------------------------------------------------

float FElysiumNpc::ScaleField_0x1ddc() const
{
	// `FUN_10160680` — `(float10)_DAT_10725c9c * (float10)*(float *)(param_1 + 0x1ddc)`.
	// **Unrecovered:** `_DAT_10725c9c`'s value, the field's retail name and the class that owns the
	// offset. 1.0 keeps the read observable without claiming a scale.
	constexpr float Scale = 1.0f;   // _DAT_10725c9c — unrecovered
	return Field_0x1ddc * Scale;
}

bool FElysiumNpc::TestField_0x2830() const
{
	// `FUN_100e58b0`, arm order preserved: the byte at `+0x30e9` short-circuits TRUE, and only then
	// is the int at `+0x2830` compared against 0.
	if (bField_0x30e9)
	{
		return true;
	}
	return Field_0x2830 == 0;
}

bool FElysiumNpc::HasNonDefaultVelocity() const
{
	// `CAISound::FUN_10026e70`, slot 153 for the five AI-helper classes: compare `m_vecVelocity`
	// (`+0x03d4`) component-wise against `DAT_1070d1b0/b4/b8` and answer 1 when ANY component
	// differs. That global triple is the always-zero vector `GetGroundVelocityToApply` (slot 210,
	// `0x10027370`) answers, so the comparison is against the zero vector and this is "am I moving".
	// Retail compares exactly, with no epsilon, and so does this.
	return Velocity.X != 0.0 || Velocity.Y != 0.0 || Velocity.Z != 0.0;
}

int32 FElysiumNpc::UnawareTableEntry(const TCHAR* RetailTable, int32 Index)
{
	// SEAM for `DAT_1063abcc` and `DAT_1063abdc`. **Unrecovered:** neither table's contents nor its
	// purpose (message, sound or activity selection) is settled anywhere in the corpus. The indexing
	// is the recovered body and is above; this is the row it would read.
	(void)RetailTable;
	(void)Index;
	return 0;
}

int32 FElysiumNpc::UnawareTableA() const
{
	// `FUN_1037b870` — `*(undefined4 *)(&DAT_1063abcc + *(int *)(this + 0x6668) * 4)`.
	return UnawareTableEntry(TEXT("DAT_1063abcc"), UnawareType);
}

int32 FElysiumNpc::UnawareTableB() const
{
	// `FUN_1037b890` — the same shape over `DAT_1063abdc`, same index.
	return UnawareTableEntry(TEXT("DAT_1063abdc"), UnawareType);
}

int32 FElysiumNpc::ProxySlotIndexOf(const FElysiumEntity* Proxy) const
{
	// SEAM for `proxy->+0x6660`. `CNPC_VMingXiao`'s blood-proxy subsystem has no producer here, so
	// this answers `INDEX_NONE` and `ProxyReadyTimer` takes its "the slot did not resolve" arm.
	(void)Proxy;
	return INDEX_NONE;
}

bool FElysiumNpc::ProxyReadyTimer(const FElysiumEntity* Proxy, double Now)
{
	// `FUN_10397b40`, arm by arm.
	//
	// 1. A null argument answers false.
	if (Proxy == nullptr)
	{
		return false;
	}
	// 2. `curtime < m_flProxyReadyTimer` (`+0x66a4`) answers false — the cooldown is not up. The
	//    word is family **Bosses**' `MingXiaoProxyReadyTimer`, read through its owner.
	if (Now < MingXiaoProxyReadyTimer)
	{
		return false;
	}
	// 3. The argument's own slot index (`proxy+0x6660`) must index back to the argument through
	//    `this+0x66a8 + slot*4`. Anything else answers false.
	const int32 Slot = ProxySlotIndexOf(Proxy);
	if (Slot < 0 || Slot >= MingXiaoProxySlots)
	{
		return false;
	}
	const FElysiumEntity* Registered =
		(World && Proxies[Slot].IsSet()) ? World->Resolve(Proxies[Slot]) : nullptr;
	if (Registered != Proxy)
	{
		return false;
	}
	// 4. An already-registered slot answers TRUE at once, before the census below.
	if (bProxyRegistered[Slot])
	{
		return true;
	}
	// 5. Count the six slots that are either a live handle OR already registered. Only a count of
	//    ZERO registers this slot and answers true — one proxy at a time.
	int32 Taken = 0;
	for (int32 i = 0; i < MingXiaoProxySlots; ++i)
	{
		const bool bLive =
			World && Proxies[i].IsSet() && World->Resolve(Proxies[i]) != nullptr;
		if (bLive || bProxyRegistered[i])
		{
			++Taken;
		}
	}
	if (Taken < 1)
	{
		bProxyRegistered[Slot] = true;
		return true;
	}
	return false;
}

void FElysiumNpc::StartSearchTimer()
{
	// `CNPC_VWerewolf::StartSearchTimer` (`0x103d1ca0`): `rdtsc` into the STATIC pair
	// `DAT_1093d638`/`DAT_1093d63c`, shared by every werewolf on the map rather than kept per NPC.
	// That is the recovered fact and is why this is a file static here too.
	GSearchTimerCycles = LifecycleCycles();
}

bool FElysiumNpc::ReportSearchTimer(bool bPassThrough)
{
	// `CNPC_VWerewolf::ReportSearchTimer` (`0x103d1d60`): `rdtsc` again, SUBTRACT the stored pair in
	// place so the statics now hold the elapsed cycles, and pass the second argument through
	// unchanged. Retail reports nothing else — the pair is the report.
	GSearchTimerCycles = LifecycleCycles() - GSearchTimerCycles;
	return bPassThrough;
}

uint64 FElysiumNpc::SearchTimerElapsedCycles()
{
	return GSearchTimerCycles;
}

void FElysiumNpc::MotorResetToDefault()
{
	// `CAI_Motor::FUN_102e1110` (`0x102e1110`), in retail's order:
	//
	//   1. Resolve the navigator (`thunk_FUN_102e2610`) and its move type (`thunk_FUN_102ee3f0`),
	//      then push that move type onto the OUTER NPC through vtable `+0x4d8` (slot 310).
	//   2. Reset the motor's own state (`thunk_FUN_102e2840`).
	//   3. Zero the facing/move vector (`thunk_FUN_102e2690` against `DAT_1070d1b0`, the same
	//      always-zero global `HasNonDefaultVelocity` above compares to).
	//   4. `npc+0x3ec = 0x3f800000` — gravity back to 1.0.
	//
	// **The target `FElysiumNpcMotor::ResetToDefault` names a struct this runtime does not stand:**
	// the motor here is `FElysiumScriptedCharacter::Motor`, a movement solver with no retail-shaped
	// state object, and the shape map already records `+0x5d34`..`+0x5d44` as `CHAIN` rows into it.
	// So the body lands on the leaf that owns the motor, and the two steps whose inputs exist here
	// are the ones that run.
	if (Motor != nullptr)
	{
		// Steps 1–3 are the motor's own: this runtime's motor carries no retail move-type word and
		// no separate facing vector, so `StopMoving` is the whole of what it can be told. The
		// navigator move-type push (slot 310) has no receiver here.
		StopMoving();
	}
	Gravity = 1.0f;   // step 4, verbatim
}

// -------------------------------------------------------------------------------------------------
// `CAI_StandoffBehavior::vfunc13` (`0x102c7600`).
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpc::StandoffSelect(FStandoffWords& Words, const FStandoffConditions& Conditions,
	bool bInCombatState, bool bHasEnemy, FHintWords* Hint, double Now)
{
	// The selector, arm by arm. Every `return` below is a retail code: `0x17`, `0x25`, `0x21`,
	// `0x29`/`0x28`, or `INDEX_NONE` for the fall-through to `CAI_Behavior::vfunc13`.

	// 0. `m_NPCState != 2` (`+0x5cc0`) falls through immediately. A standoff that is not in COMBAT
	//    selects nothing of its own.
	if (!bInCombatState)
	{
		return INDEX_NONE;
	}
	// 1. Condition 0x40 or condition 0x3f: the answer is `0x29` minus the `+0x24` byte — so `0x29`
	//    with the byte clear and `0x28` with it set. Both gates share the one answer.
	if (Conditions.bCond0x40 || Conditions.bCond0x3f)
	{
		return 0x29 - (Words.bCoverDirty ? 1 : 0);
	}
	// 2. The `+0x4c` latch, consumed on read: a set latch is cleared, and with an enemy standing the
	//    answer is `0x17` at once.
	if (Words.bSawNewEnemy)
	{
		Words.bSawNewEnemy = false;
		if (bHasEnemy)
		{
			return 0x17;
		}
	}
	// 3. Condition 0x4c, gated on `RandomInt(0, 99) <= m_iChanceThreshold` (`+0x38`) — note `<=`,
	//    not `<`. With an enemy standing, the claimed hint's `+0x9c` timer is MIN-ed down to curtime
	//    and the reaction counter becomes `m_iReactionsLeft > 1 ? 1 : 0`.
	if (Conditions.bCond0x4c && LifecycleRandomInt(0, 99) <= Words.ChanceThreshold && bHasEnemy)
	{
		if (Hint != nullptr && Hint->bValid && Now < Hint->NextUseTime)
		{
			Hint->NextUseTime = Now;
		}
		Words.ReactionsLeft = Words.ReactionsLeft > 1 ? 1 : 0;
	}
	// 4. The re-roll: an exhausted counter that has been idle longer than `_DAT_10497530` re-draws
	//    `RandomInt(0, max - min) + min` from the `+0x30`/`+0x34` pair.
	if (Words.ReactionsLeft == 0
		&& (Now - Words.NextReactionAt) > GStandoffElapsedThresholdSeconds)
	{
		Words.ReactionsLeft = LifecycleRandomInt(0, Words.ReactionChanceMax - Words.ReactionChanceMin)
			+ Words.ReactionChanceMin;
	}
	// 5. A counter of exactly ONE re-stamps the next-reaction clock: `+0x44 == 0.0` means "no
	//    range", and the delay is then `+0x40` alone; otherwise it is `RandomFloat(+0x40, +0x44)`.
	if (Words.ReactionsLeft == 1)
	{
		Words.NextReactionAt = Words.ReactionDelayMax == GLifeZero
			? Now + Words.ReactionDelayMin
			: Now + LifecycleRandomFloat(Words.ReactionDelayMin, Words.ReactionDelayMax);
	}
	// 6. A counter at or below ZERO answers `0x17`, and on the way out writes the posture from the
	//    hint's type (`+0x5dc == 0x65` -> posture 2, else 0) and, on a `RandomInt(0,99) < 0x50`
	//    roll, min-s the hint's `+0x9c` timer down to curtime.
	if (Words.ReactionsLeft < 1)
	{
		if (Hint != nullptr && Hint->bValid)
		{
			Words.Posture = Hint->HintType == 0x65 ? 2 : 0;
			if (LifecycleRandomInt(0, 99) < 0x50 && Now < Hint->NextUseTime)
			{
				Hint->NextUseTime = Now;
			}
		}
		return 0x17;
	}
	// 7. Condition 0x48: posture 2 is promoted to 3 and answers `0x25`; otherwise a blocked-since
	//    stamp older than `_DAT_10497530` sets the `+0x54` byte.
	if (Conditions.bCond0x48)
	{
		if (Words.Posture == 2)
		{
			Words.Posture = 3;
			return 0x25;
		}
		if ((Now - Words.BlockedSince) > GStandoffElapsedThresholdSeconds)
		{
			Words.bBlockedLongEnough = true;
		}
	}
	// 8. Conditions 0x4f and 0x51 both SUPPRESS the 0x60 arm below — retail's nesting is
	//    `if (!0x4f) { if (!0x51) { if (0x60) { ... } } }`.
	if (!Conditions.bCond0x4f && !Conditions.bCond0x51 && Conditions.bCond0x60)
	{
		// With 0x48 also standing, a `RandomInt(0,99) > 0x31` falls through to the base instead.
		if (Conditions.bCond0x48 && LifecycleRandomInt(0, 99) > 0x31)
		{
			return INDEX_NONE;
		}
		return 0x21;
	}
	// 9. Everything else: `CAI_Behavior::vfunc13`, the base this runtime does not carry.
	return INDEX_NONE;
}
