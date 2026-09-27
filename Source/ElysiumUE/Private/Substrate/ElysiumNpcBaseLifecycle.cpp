// `CAI_BaseNPC`'s bodies of the `Lifecycle` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseLifecycle.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumRng.h"
#include "ElysiumSaveArchive.h"
#include "Substrate/ElysiumInterestingPlace.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcLifecycleShared.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcScheduleHost.h"

// --- File-scope helpers moved with the base bodies (story 5 step 5) ---

namespace
{
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
	// `CAI_StandoffGoal::Spawn` (`0x102cd2d0`): `m_flNextThink = curtime + _DAT_1044e658`.
	constexpr double GStandoffGoalThinkDelaySeconds = ElysiumNpcTunables::HundredthDouble;
	// `CAI_StandoffBehavior::vfunc13` (`0x102c7600`): the elapsed-seconds threshold the reaction
	// re-roll and the blocked latch both compare against.
	constexpr double GStandoffElapsedThresholdSeconds = ElysiumNpcTunables::MinusThousandthDouble;
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
		/** Bit 0x27d8 ADDS `_DAT_1049b998` (43) to the angle range; every other row MULTIPLIES it
		 *  by `_DAT_104454d0` (0.5). */
		bool bAddsInsteadOfScales = false;
		int32 CategoryBits = 0;   // +0x474
		const TCHAR* Body = nullptr;
	};
	// The angle-range scale and the `0x27d8` bias; identity and zero stood in for them until the
	// cells were read (2026-09-21).
	constexpr float GHintAngleRangeScale = ElysiumNpcTunables::Half;
	constexpr float GHintAngleRangeBias = ElysiumNpcTunables::HintType27d8AngleBias;
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

// --- Moved from `ElysiumNpcLifecycle.cpp` (story 5 step 5) ---

// slot 0 `void SetRefEHandle(const CBaseHandle&)` — 0x10027450
void FElysiumNpcBase::SetRefEHandle(const FElysiumEntityHandle& InHandle)
{
	// Retail's whole body is `*(undefined4*)&this->field_0x448 = *param_1;` — the four-byte entity
	// handle/serial word at `+0x0448`. This runtime's counterpart is `FElysiumEntity::Handle`, which
	// the world's registry binds at `Construct`; writing it is exactly what retail does, and the
	// registry stays the authority over what the handle RESOLVES to.
	Handle = InHandle;
}

// slot 3 `IServerNetworkable* GetNetworkable()` — 0x10027630
void* FElysiumNpcBase::GetNetworkable()
{
	// Retail's whole body is `return &this->field_0x2d4;`, the address of the embedded
	// `CServerNetworkProperty` sub-object. SEAM: this runtime has no network property and no
	// networkable interface — entities are plain C++ objects with no edict behind them — so there is
	// no sub-object whose address could be answered. Answers null and names the retail word.
	return nullptr;
}

// slot 4 `CBaseEntity* GetBaseEntity()` — 0x10027650
FElysiumEntity* FElysiumNpcBase::GetBaseEntity()
{
	// `return this;`
	return this;
}

// slot 91 `bool ShouldCollide(int, int) const` — 0x100b4de0
bool FElysiumNpcBase::ShouldCollide(int32 CollisionGroupArg, int32 ContentsMask) const
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
bool FElysiumNpcBase::IsMarkedForDeletion()
{
	// `return this->m_iEFlags & 1;` — bit 0 of the entity-flags word at `+0x0268`, `EFL_KILLME`.
	// This runtime spells "killed, and the world will reap the slot" as `FElysiumEntity::bDead`,
	// which `Kill()` is the sole writer of; that IS the killme bit.
	return (IsDead() ? GEntityFlagKillMe : 0) != 0;
}

// slot 137 `CBaseAnimating* GetBaseAnimating()` — 0x1004fc50
FElysiumEntity* FElysiumNpcBase::GetBaseAnimating()
{
	// `return this;` — retail's default answers itself. This leaf IS on the animating chain
	// (`FElysiumAnimating`), so the answer is the same entity.
	return this;
}

// slot 152 `float GetDelay()` — 0x1004fc10
float FElysiumNpcBase::GetDelay()
{
	// `return (float10)*(float *)(param_1 + 0x500);` — a plain read of the `CBaseDelay` field below
	// the NPC word table. SEAM (declared, unwritten): this runtime carries an output's delay on the
	// WIRE (`FElysiumOutputDef`) rather than on the entity, so nothing writes `EntityDelay`.
	return EntityDelay;
}

// slot 158 `bool IsAlive()` — 0x100b4dc0
bool FElysiumNpcBase::IsAlive()
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
bool FElysiumNpcBase::IsTemplate()
{
	// `return (uint)this->m_spawnflags >> 0xb & 1;`
	return (SpawnFlags & GSpawnFlagTemplate) != 0;
}

// slot 471 `float GetReactionDelay()` — 0x1026a8a0
float FElysiumNpcBase::GetReactionDelay()
{
	// The whole body is `RandomFloat(0.2, 0.9)` (`0x3e4ccccd`, `0x3f666666`). It is the delay
	// `OnListened` (`0x1026a5e0`) queues a heard condition on `m_DelayedConditionList` with —
	// `docs/vtmb/npc-ai/senses.md` -> "OnListened". `FElysiumNpcSenses::TickHearing` dispatches it,
	// beside the `HEAR_FLINCH` variant `RandomFloat(0, 0.5)` that is NOT this slot.
	return LifecycleRandomFloat(GReactionDelayMin, GReactionDelayMax);
}

// slot 552 `bool ShouldFadeOnDeath()` — 0x1027a400
bool FElysiumNpcBase::ShouldFadeOnDeath()
{
	// `return (uint)this->m_spawnflags >> 9 & 1;`
	return (SpawnFlags & GSpawnFlagFadeOnDeath) != 0;
}

// slot 559 `CBaseEntity* FindNamedEntity(const char*)` — 0x10279090
FElysiumEntity* FElysiumNpcBase::FindNamedEntity(const TCHAR* Name)
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
			(World && BaseMemory.Enemy.IsSet()) ? World->Resolve(BaseMemory.Enemy) : nullptr;
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

void FElysiumNpcBase::HintScriptHide(FHintWords& Hint)
{
	// 0x102d0860. The base `CBaseEntity::ScriptHide` half belongs to the ENTITY and is
	// `FElysiumEntity::ScriptHide`; there is no `CAI_Hint` entity here, so this body is the hint's
	// own half and nothing else.
	Hint.Disabled = 1;
}

void FElysiumNpcBase::HintScriptUnhide(FHintWords& Hint)
{
	// 0x102d0890 — the exact inverse.
	Hint.Disabled = 0;
}

void FElysiumNpcBase::HintKill(FHintWords& Hint)
{
	// 0x102d08c0, whose whole body is `MOV EAX,[ECX] / JMP [EAX + 0x134]`. `+0x134` is slot 77, so
	// Kill on a hint node IS ScriptHide. Not "like" it: the same address is reached.
	HintScriptHide(Hint);
}

double FElysiumNpcBase::StandoffGoalSpawnNextThink(double Now)
{
	// 0x102cd2d0 — `ThinkSet(&LAB_10006672, 0, NULL)` and then
	// `m_flNextThink = curtime + _DAT_1044e658`. The think function is a stub label with no body in
	// the corpus, so what is recovered is the CLOCK and nothing else.
	return Now + GStandoffGoalThinkDelaySeconds;
}

void FElysiumNpcBase::HintSpawn(FHintWords& Hint)
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

FElysiumNpcBase::EKeyValueArm FElysiumNpcBase::ClassifyKeyValue(const FString& Key, FString& OutKey)
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

FString FElysiumNpcBase::RewriteAngleKey(float AngleValue, const FVector& CurrentAngles)
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

int32 FElysiumNpcBase::RestoreExtendedHeader(void* Archive)
{
	// 0x1027c160, slot 127 on the `CAI_BaseNPC` line, read off the LISTING — see the `.inl` for what
	// story 29d corrected here. In retail's order:
	//
	//   1. `1027c178 CALL [IRestore + 0x8]` reads `AIExtendedSaveHeader_t` (datamap `0x105cabd0`)
	//      into `field_0x19b4`. This runtime's header is `FElysiumNpcBase::FAiExtendedSaveHeader` and
	//      `LastSavedExtendedHeader` is `+0x19b4`; the four words come back through the same seam
	//      `FElysiumNpcBase::Save` wrote them through (`ElysiumNpcSaveRestore10.cpp`).
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
	SaveStampDecode(BaseScheduleHost.WaitFinished, ESaveStampMode::Zero);          // +0x5db4, mode 3

	if (Motor != nullptr)
	{
		++MotorRestoreFixups;
	}
	++MoveAndShootRestoreFixups;
	return ChainResult;
}

bool FElysiumNpcBase::OnRestoreForwardsCheckUntouch(bool bCallerValue)
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

void FElysiumNpcBase::BaseNpcUpdateOnRemove()
{
	// 0x1027ca30 — slot 180 for the NON-Troika branch (`CAI_BaseNPC`, `CScriptedTarget`,
	// `CNPC_Bullseye`, `CNPC_Crow`, the test hulls). The Troika override `0x1028d6e0` the port's
	// slot table cites is a DIFFERENT body; this is the one below it, in retail's order:
	//
	//   1. The squad unlink (`+0x5da4`): `thunk_FUN_10315ec0` asks whether this NPC is in the squad
	//      and `thunk_FUN_103158f0` removes it. SEAM — `ConnectedSquad()` answers nothing on this
	//      substrate (no squad object, 0002/17), so there is no list to leave.
	//   2. The hint release (`+0x5ddc`), INLINE and guarded only on a hint being held (`1027ca53`):
	//      `thunk_FUN_102d1420(hint, 0.0)` unlocks it (hint `+0x5e0 = -1`) and stamps its reuse
	//      time (hint `+0x5ec = now + 0.0`), then `m_pHintNode = 0`. No owner test and none of the
	//      Troika `ClearHintNode` `0x10295ab0`'s cover/flag/extents writes (story 5 step 5
	//      correction: the port called that one).
	//   3. This class's own vtable `+0x7fc` (slot 511).
	//   4. `CBaseCombatCharacter::UpdateOnRemove` — the chain's, which this runtime runs from
	//      `FElysiumEntity::Kill`.
	if (ConnectedSquad() != nullptr)
	{
		// Unreachable today by construction; stated so the squad layer replaces the body and not the
		// call site.
	}
	if (BaseScheduleHost.HintNode != INDEX_NONE)
	{
		BaseScheduleHost.bOwnsHint = false;                                   // hint +0x5e0 = -1
		BaseScheduleHost.HintReusableAt = World != nullptr ? World->NowSeconds() : 0.0;  // +0x5ec
		BaseScheduleHost.HintNode = INDEX_NONE;                               // +0x5ddc = 0
	}
	// Slot 511 (`+0x7fc`) is a later story's; nothing routes to it yet.
}

float FElysiumNpcBase::ScaleField_0x1ddc() const
{
	// `FUN_10160680` — `(float10)_DAT_10725c9c * (float10)*(float *)(param_1 + 0x1ddc)`.
	// **Unrecovered:** `_DAT_10725c9c`'s value, the field's retail name and the class that owns the
	// offset. 1.0 keeps the read observable without claiming a scale.
	constexpr float Scale = 1.0f;   // _DAT_10725c9c — unrecovered
	return Field_0x1ddc * Scale;
}

bool FElysiumNpcBase::HasNonDefaultVelocity() const
{
	// `CAISound::FUN_10026e70`, slot 153 for the five AI-helper classes: compare `m_vecVelocity`
	// (`+0x03d4`) component-wise against `DAT_1070d1b0/b4/b8` and answer 1 when ANY component
	// differs. That global triple is the always-zero vector `GetGroundVelocityToApply` (slot 210,
	// `0x10027370`) answers, so the comparison is against the zero vector and this is "am I moving".
	// Retail compares exactly, with no epsilon, and so does this.
	return Velocity.X != 0.0 || Velocity.Y != 0.0 || Velocity.Z != 0.0;
}

void FElysiumNpcBase::MotorResetToDefault()
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

int32 FElysiumNpcBase::StandoffSelect(FStandoffWords& Words, const FStandoffConditions& Conditions,
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
		// SEAM: this writes the caller's `FHintWords` VIEW. The live `ai_hint` takes the write only
		// when its owner writes the view back (`FElysiumHint::FromWords`); the claim path that does
		// so is 0018 story 8's, so for now nothing reaches the entity's `m_flNextUseTime`.
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

// --- Moved from `ElysiumNpcLifecycle.cpp` (story 5 step 5) ---

// -------------------------------------------------------------------------------------------------
// The twelve Troika-line slots this family fills. Each carries the generated signature exactly.
// -------------------------------------------------------------------------------------------------

FElysiumEntity* FElysiumNpcBase::PlayerControllerOf(FElysiumEntity* Player) const
{
	// SEAM for `thunk_FUN_101618a0(player)`, the `!playercontroller` resolve. Retail hands back the
	// player's own scene stand-in; this runtime stands that as the `npc_VPlayerController` leaf
	// (`FElysiumPlayerControllerNpc`), which a map places by name and which the player holds no
	// pointer to. Answers the player itself, which is the arm that keeps the selector resolving to a
	// live entity, and names what would settle it: a back-pointer from the player to its duplicate.
	return Player;
}
