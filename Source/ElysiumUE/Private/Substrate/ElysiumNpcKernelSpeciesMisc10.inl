// Story 29d, family **SpeciesMisc10** — the declarations of this family's layer 10–18 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here: the
// generator already declares that virtual and this family only defines it.
//
// The definitions are in `Substrate/ElysiumNpcKernelSpeciesMisc10.cpp` (the species words, the
// Newscaster, the Chang brothers, the ghoul croucher, the guard and the ManBat) and
// `Substrate/ElysiumNpcKernelSpeciesMisc10_2.cpp` (the Sabbat leader, the Tzimisce pair, the vampire
// boss, the Werewolf and the Pedestrian spawn-side body); the tests are
// `Tests/ElysiumNpcKernelSpeciesMisc10Tests.cpp`. The walked prose is
// `docs/vtmb/npc-ai/social.md`, `shape.md` and `lifecycle.md`
// § "Story 29d, family SpeciesMisc10 — …".
//
// --- What this family is --------------------------------------------------------------------------
//
// **The one-off species bodies that are not slot overrides.** Forty-one rows: the Newscaster's story
// loader and its player, the Sabbat leader's blood splash and player-damage record, the ManBat's
// screech cone and its slowed-entity release, the Tzimisce runner's two form words and the head
// claw's slow/HUD chain, the Werewolf's stuck and path helpers, the Chang brothers' teleport/united
// gates and their ledge pick, the vampire boss's health record and body emitters, the transformation
// wait, and eight registry rows that are census data rather than code.
//
// FIVE STANDING FACTS OF THIS FAMILY, stated once here rather than at thirty call sites.
//
//   * **`GetCurrHealthPercent` (`0x103c6830`, family Combat10) RISES with damage.** It is
//     `wounds(stat 0x0f) / cap(stat 0x11)` and not a remaining-health fraction, so
//     `HealthPercentLostSinceRecord` (`0x103c6a20`) answers **positive** for a body that has lost
//     health. The checklist's walk of `0x103c6a20` says "a body that has lost health answers
//     negative"; it is the other way round, and every threshold that reads it
//     (`_DAT_104ad9f8` = 0.1 for the Chang brothers, `_DAT_104c3cc4` = 0.0666667 for the Sabbat
//     leader) is a *fraction of the health bar lost since the mark*. **CORRECTED.**
//   * **`+0x9c` is the entity's `CBaseCombatCharacter` self-downcast cache and `+0xa8` its
//     `CBasePlayer` one**, so `victim->+0xa8 != 0` reads "is this the player" and `victim->+0x9c`
//     is the victim itself. Family Senses10 states the first half; the second is what makes the
//     ManBat screech cone, the head claw's slot 332 and the ghoul croucher's burn
//     **player-only** bodies.
//   * **Every emitter word in this family is an `EHANDLE` to an entity this substrate does not
//     create.** Family Damage's `CreateNamedEmitter`/`StartNamedEmitter`/`KillNamedEmitter`/
//     `RemoveNamedEntity` seam is the one this family goes through, so the DECISION (which name,
//     which attach mode, which bone, in what order) is recorded and assertable while the handles
//     stay dead — which is retail's own behaviour for a dead handle.
//   * **`_DAT_1044fab0` is a DOUBLE and reads 0.0** (`103c1db6` is `FCOMP double ptr`), read out of
//     the pinned `vampire.dll` at file offset `0x44fab0`. It is the "no slow running" sentinel both
//     the ManBat and the head claw compare their expiry against.
//   * **`CNPC_VWerewolf`'s two hint words are the other way round from the checklist's walk.** The
//     datamap reads `+0x66b0 m_pTeleportHint` and `+0x66bc m_pMoveHint` (plus `+0x66b4
//     m_pLastUsedTeleportHint`, `+0x66c0 m_pLastUsedMoveHint`, `+0x66c4 m_pBreakHint`); the walk of
//     `0x103ce750` has `+0x66b0` and `+0x66bc` swapped. Family **Hints** already bound them
//     correctly (`TeleportHintNode`/`MoveHintNode`) and this family reads those. **CORRECTED.**

// --- `CAI_BaseNPC::LeaveGrappleState` `0x1026ce30` -----------------------------------------------

/** `CAI_BaseNPC::LeaveGrappleState` (`0x1026ce30`), 100 bytes — a DISTINCT retail function beside
 *  the Troika override `0x102b5d90` that owns slot 380, so it takes its own name and is not a slot
 *  body. Four UNCONDITIONAL steps with no grapple-type gate anywhere: fire `m_OnGrappleEnd` with the
 *  grapple partner as activator (null when the handle is stale), `CBaseCombatCharacter::
 *  LeaveGrappleState`, slot 416 `SetForceFrequentThink(false)`, then `0x10007ea0` — the saturating
 *  decrement of `m_iIsOblivious` (`+0x5bb4`) clamped at 0, and the squad reconnect `0x10009601`.
 *
 *  `FElysiumNpc::LeaveGrappleState` (`ElysiumNpc.cpp`) calls this and then slot 614. */
void BaseLeaveGrappleState();

// --- `CNPC_VGhoulCroucher` — `0x1037be80` (slot 24) and `0x1037c090` ----------------------------

/** One `BurnHitbox` request. **SEAM**: this substrate exposes no hitbox table to the kernel
 *  (family Damage's `HitboxSetCount` answers 0), so the loop makes no passes and the record is what
 *  says the body ran and took retail's own zero-hitbox arm. */
struct FBurnHitboxCall
{
	FElysiumEntityHandle Target;
	int32 HitboxIndex = INDEX_NONE;
	float Seconds = 0.f;      // `5.0`
	float Interval = 0.f;     // `0.5` (`0x3f000000`)
};

// --- `CNPC_VManBat` — `0x1038e9c0` and `0x1038f020` ---------------------------------------------

/** SEAM for `CBaseCombatCharacter::BeginSlowEntity` / `EndSlowEntity` — retail's movement-slow on
 *  another character, always with the argument **500.0** on both the ManBat (`1038eb5e`) and the
 *  Tzimisce head claw (`103c1dca`). No port system slows a character from the kernel tier, so the
 *  calls are recorded rather than applied and the record is what a case reads. */
struct FSlowEntityCall
{
	FElysiumEntityHandle Target;
	float Magnitude = 0.f;
	bool bBegin = false;
};
TArray<FSlowEntityCall> SlowEntityCalls;
void BeginSlowEntity(const FElysiumEntityHandle& Victim, float Magnitude);
void EndSlowEntity(const FElysiumEntityHandle& Victim, float Magnitude);

/** SEAM for `UTIL_ScreenShake` (`0x101cdba0`) — the ManBat's cone shakes the screen with
 *  `(2.5, 0.2, 3.0, 0.0, 0, 0)` around **its own** slot-220 origin (`vt+0x370`), not the target's.
 *  Nothing in this substrate shakes the view from the kernel; the request is recorded. */
struct FScreenShakeCall
{
	FVector CentreUnits = FVector::ZeroVector;
	float Amplitude = 0.f;
	float Frequency = 0.f;
	float Duration = 0.f;
	float Radius = 0.f;
};

/** SEAM for `0x10344f80(combatCharacter, delta, 2, 0)` — the physics push the cone applies, with
 *  `delta = target->GetAbsOrigin() - my GetAbsOrigin()` (both slot 217). Recorded. */
struct FPushEntityCall
{
	FElysiumEntityHandle Target;
	FVector DeltaUnits = FVector::ZeroVector;
	int32 Mode = 0;
};

/** SEAM for `0x1015d680(player, 0)` — the entity in the player's inventory slot 0 (the `+0x2308`
 *  handle array), which both the ManBat's HUD emitter and the head claw's gate on. This runtime's
 *  inventory is `FElysiumInventory`; this answers its active weapon entity, which IS retail's slot 0
 *  for a character carrying one, and null otherwise — retail's own refusal. */
FElysiumEntity* PlayerInventorySlot0() const;

// --- `CNPC_VNewscaster` — `0x103a0670` and `0x103a0ab0` -----------------------------------------

/** `0x103a07f0`, the per-`Story` parser, whose tail is the fact the play body depends on. It is a
 *  file static in the `.cpp` rather than a member, because its one argument is a `KeyValues*` and
 *  this header is included inside `class FElysiumNpc`.
 *
 *  **CORRECTED**: `+0x24` is NOT a version count. `103a098f`–`103a0a0b` walks the parsed versions in
 *  order and stores the index of the FIRST whose `dependency` is absent, empty, or evaluates
 *  non-zero through `0x1000134d` — `PyRun_String(src, Py_eval_input, __main__, __main__)` — and
 *  answers **true**. A record whose every dependency is false frees its strings and answers
 *  **false**, and is never appended. So a `Story` row carries FOUR `(dependency, filename)` pairs at
 *  `+0x04`/`+0x08`, `+0x0c`/`+0x10`, `+0x14`/`+0x18`, `+0x1c`/`+0x20` (a fifth is refused with
 *  `"Newscaster: too many versions in %s! skipping %s"`), the chosen index at `+0x24`, and the story
 *  name at `+0x00` — `GetString("Name", "STORY")`, so an unnamed story is literally `STORY`. A key
 *  whose name does not contain `"Story"` warns `"Newscaster: invalid key! (%s)"` and is skipped.
 *
 *  The dependency evaluation goes through `FElysiumEntityWorld::EvalCondition`, this runtime's
 *  `PyRun_String`, so the same expressions the retail files carry (`G.Story_State < 10`,
 *  `not IsPCMalk()`) are answered by the same interpreter every dlg condition uses. */
bool EvalNewscasterDependency(const FString& Source) const;

/** SEAM for `0x101cd9e0(1)` — `UTIL_PlayerByIndex(1)`, the gate that stops the loader running before
 *  there is a player. Answers the world's player entity. */
bool NewscasterPlayerPresent() const;

// --- `CNPC_VSabbatLeader` — `0x103a9d90`, `0x103aa960`, `0x103aaa80`, `0x103aabc0` --------------

/** SEAM: another character's type-0 `CVStatList_t` value. Family **Combat10** built
 *  `TypedStatValue` for THIS NPC's sheet; these two bodies read the PLAYER's, so the walk
 *  (`+0x13bc` count, `+0x13c0` table, tag `+0x10 == 0`, else the lazily built global
 *  `DAT_109f0b40`) is applied to another entity here. A candidate with no sheet answers 0, which is
 *  the empty global's own answer. */
static int32 TypedStatValueOf(const FElysiumEntity* Candidate, int32 StatId);

// --- `CNPC_VTzimisceHeadClaw` — `0x103c1d80` (slot 332) and `0x103c2230` ------------------------
//
// **OFFSETS CORRECTED.** The listing reads `m_flSlowedExpire` at `+0x6678` (`103c1db0`),
// `m_hSlowedEntity` at `+0x6674` (`103c1e02`), the player emitter at `+0x667c` (`103c1dfc`) and the
// HUD emitter at `+0x6680` (`103c1ff2`) — the checklist's walk has the first two swapped with each
// other and the last two shifted. `0x103c2230` reads the same four, which is the cross-check.

/** SEAM for `IEngineSound::EmitSound(edict, channel, wav, volume, attenuation, 0, pitch, …)` on a
 *  NAMED wav — the path the Bach shield and both Slug sounds take, which is NOT the VSound concept
 *  table family Sounds10 seams. Forwarded to `IElysiumAudio::PlayBodySound` where there is one, and
 *  recorded either way because retail's channel/volume/attenuation triple is the recovered half. */
struct FNamedWavEmit
{
	FString Wav;
	int32 Channel = 0;
	float Volume = 0.f;
	float Attenuation = 0.f;
	int32 Pitch = 0;
	FElysiumEntityHandle Emitter;   // whose `+0x2e0` edict retail passes
};
TArray<FNamedWavEmit> NamedWavEmits;
void EmitNamedWav(const FElysiumEntity* Emitter, int32 Channel, const TCHAR* Wav, float Volume,
	float Attenuation, int32 Pitch);

// --- `CNPC_VTzimisceRunner` — `0x103c3cd0` (slot 335) and `0x103c3d10` (slot 336) ---------------
//
// **NOT WIRED TO SLOTS 335/336.** Both Troika-line bodies (`0x103415f0`, `0x10341680`) are layer-0
// rows of band 0–9 with no verdict, so `gen_kernel_shape` emits their stubs and `merge_verdicts
// --band 10-18` refuses a row for an address outside this band. Each body lands under its recovered
// species name, is driven by the suite, and the gap is named in the story's answer.

// --- `CNPC_VVampireBoss` — `0x103c63c0`, `0x103c6a00`, `0x103c6a20`, `0x103c6f40` ---------------

// --- `CNPC_VWerewolf` — `0x103ce750` (slot 448), `0x103d0db0`, `0x103d5130` (slot 76), `0x103d9f90`

/** Each `WerewolfHasPath` ask, with both endpoints, so a case can read that the forward happened
 *  while the seam refuses. */
struct FHasPathQuery
{
	FVector StartUnits = FVector::ZeroVector;
	FVector EndUnits = FVector::ZeroVector;
};

/** SEAM for `CBaseAnimating::MatchOriginAnglesToAnimation(bone, bOrigin, bAngles)` — the snap onto
 *  the animation's root bone. This substrate has no bone-space sampler at the kernel tier, so the
 *  request is recorded and the transform is left alone; every other write of the body still lands. */
struct FMatchOriginAnglesCall
{
	FString Bone;
	bool bOrigin = false;
	bool bAngles = false;
};
