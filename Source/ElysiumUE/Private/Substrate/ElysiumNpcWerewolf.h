#pragma once

#include "Substrate/ElysiumNpcBaseBoss.h"

// `CNPC_VWerewolf` (primary vtable `0x104cf4d4`), built by `npc_VWerewolf` factory `0x103c8760`.
//
// The classname's factory builds this class and it answers its own census row (story 5 step 2). Its
// slot overrides, own bodies, own datamap words and their bindings live here (steps 3-4,
// `docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`); the words a Troika body still
// reads stay on `FElysiumNpc` until step 11.
class FElysiumNpcWerewolf : public FElysiumNpcBaseBoss
{
public:
	ELYSIUM_NPC_CLASS("CNPC_VWerewolf", FElysiumNpcBaseBoss)

	// The constructor `0x103ca4b0`: its hull store (`docs/vtmb/data/class_hulls.json`).
	FElysiumNpcWerewolf();

	virtual void NPCInit() override;
	virtual void OnRestore(bool bFromLoad) override;
	virtual void Precache() override;
	virtual int32 SelectIdealStateRetail() override;
	virtual bool FVisible(FElysiumEntity* SeenTarget, int32 Mask, FElysiumEntity* Blocker, int32 Arg4) override;
	virtual void TaskFail(int32 Reason) override;
	virtual void GiveBaseFightingItems() override;
	virtual void RemoveBaseFightingItems() override;
	virtual int32 TranslateScheduleRetail(int32 ScheduleNumber) override;
	virtual float MaxYawSpeed() override;
	virtual bool ShouldIgnoreCollision(FElysiumEntity* Other) override;
	virtual bool NavIgnoreCollision(FElysiumEntity* Other) override;
	// Slot 620 (`0x103d5050`): introduced here; no Troika-line body holds the slot.
	virtual void DrawBBoxOverlay();
	virtual const TCHAR* GetShortConditionName(int32 ConditionId) override;
	virtual void DrawDebugStatOverlays() override;
	virtual void OnChangeActivity(int32 Activity) override;
	virtual int32 GetUsedHullBits() override;
	virtual bool FValidateHintType(void* Hint) override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;
	virtual void TraceAttack(void* InInfo, const FVector& DirUnits, void* InTrace) override;
	virtual void TranslateEnemyChasePosition(FElysiumEntity* Enemy, FVector& ChasePositionCm, void* Tolerance, void* SecondTolerance) override;
	virtual void OnScheduleChange(int32 NewSchedule) override;
	virtual void PainSound() override;
	virtual void ExertHvySound() override;
	virtual void GatherAttackConditions(FElysiumEntity* Enemy, float DistanceUnits) override;

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	// From `ElysiumNpcConditionsBodies.inl`.
	/** `CNPC_VWerewolf::UpdateConditionDeathTriggered` (`0x103cc890`). 29c's row targets
	 *  `FElysiumNpcConditions::UpdateConditionDeathTriggered`; that type is a bare 256-bit set with no
	 *  NPC, no activity and no outputs, so the body lands here instead.
	 *
	 *  Condition `0x7a` belongs to a Werewolf-line registrar the census has not decoded, so it is spelled
	 *  as the raw number. */
	void UpdateConditionDeathTriggered();

	// From `ElysiumNpcDebug.inl`.
	/** `NDebugOverlay::EntityBounds` (`0x10142e20`) — the whole-entity box `CBaseEntity::DrawBBoxOverlay`
	 *  draws, which `CNPC_VWerewolf#620` recolours. */
	void EmitOverlayEntityBounds(const TCHAR* RetailCall, int32 R, int32 G, int32 B, int32 A) const;
	/** `CNPC_VWerewolf::DrawDebugHullAtPoint` (`0x103d4820`) — the hull box plus one line, at a point.
	 *  `RET 0x10`: a `Vector` by value and one more dword, the overlay duration. */
	void DrawDebugHullAtPoint(const FVector& PointUnits, float Duration) const;

	// From `ElysiumNpcGeometry.inl`.
	/** `+0x66dc`/`+0x66e0`/`+0x66e4` — `CNPC_VWerewolf`'s cached fake-hull point, the world position of
	 *  the `Bip01` bone as of the previous `UpdateFakeHull`. The retail NAME is **unrecovered**: the
	 *  word is not in `CNPC_VWerewolf`'s datamap and no corpus body declares it. SOURCE units, because
	 *  the whole body is. */
	FVector WerewolfFakeHullPosUnits = FVector::ZeroVector;
	/** `+0x66f4` — the stamp `UpdateFakeHull` writes after it pushes damage, so the push repeats at
	 *  most once a second. Also not in the datamap; the retail NAME is **unrecovered**. An absolute
	 *  curtime stamp, carried as double like every other stamp on this struct. */
	double WerewolfFakeHullPushTime = 0.0;
	/** `FUN_10240250` — the axis-aligned box overlap the werewolf's fake hull is tested with, verbatim:
	 *  true iff `BMax >= AMin` and `BMin <= AMax` on all three axes, with the comparisons in retail's
	 *  order (X max, X min, Y max, Y min, Z max, Z min) and every one of them inclusive. */
	static bool BoxesOverlap(const FVector& AMin, const FVector& AMax, const FVector& BMin,
		const FVector& BMax);
	/** `UpdateFakeHull`'s activity pick: slot 323 (`vtable +0x50c`) classifies the direction from my
	 *  `WorldSpaceCenter()` to the pushed entity's origin, and its answer selects the knockback
	 *  activity handed to slot 320. 1 answers `0x7a`, 3 answers `0x7b`, anything else `0x79`. */
	static int32 FakeHullKnockbackActivity(int32 DirectionClass);
	/** `0x103d93b0`. `Now` is `gpGlobals->curtime` (`DAT_1070b228 + 0xc`), which every stamp on this
	 *  struct is measured in. */
	void UpdateFakeHull(double Now);
	/** `0x103d93b0`'s overlap arm (`103d9643`..`103d9829`), split out because it is a body of its own:
	 *  offset the pushed entity, classify the direction, pick the knockback activity, and — behind a
	 *  one-second gate that the knockback is NOT behind — push damage. `BonePosUnits` is the `Bip01`
	 *  point this call measured, in SOURCE units, and the delta against `m_vecFakeHullPos` is what
	 *  becomes the force. */
	void ApplyFakeHullPush(const FVector& BonePosUnits, double Now);
	mutable FFakeHullSeamLedger FakeHullSeams;
	/** SEAM for `CBaseEntity::GetEnemy()->+0xa8` — the entity `UpdateFakeHull` actually pushes, which
	 *  is NOT the enemy itself: the body asks slot 167 for the enemy, tests the overlap against the
	 *  ENEMY's collision box, then reads a pointer out of the enemy at `+0xa8` and offsets, classifies
	 *  and damages THAT. `+0xa8` is in no datamap in the corpus and no body in layers 0–9 writes it, so
	 *  which field it is is **unrecovered**. Answers the enemy itself, which is what a null `+0xa8`
	 *  would make retail dereference — stated rather than guessed, and the ledger records the ask. */
	FElysiumEntity* FakeHullPushTarget() const;
	bool PushedEntityKnockback(FElysiumEntity* Pushed, int32 Activity);
	/** SEAM for `CBaseEntity::TakeDamage(CTakeDamageInfo)` with the packet `UpdateFakeHull` builds:
	 *  20 damage, type `1`, sub-type `2`, `0x101c2b10(1)`, scale 1.0, a force of
	 *  `(bonePos - m_vecFakeHullPos) * 500` and a position of the nearest point. This runtime's damage
	 *  path is `ElysiumDamage::Apply`, which needs a `FElysiumDmg` descriptor and a dice context a
	 *  kernel geometry body has no source for; the force and the position are the recovered halves and
	 *  are what the ledger records. */
	void PushFakeHullDamage(FElysiumEntity* Pushed, float Damage, const FVector& ForceUnits,
		const FVector& PositionUnits);
	/** The `DAT_1093f73c` cvar `UpdateFakeHull` gates its `DrawDebugHullAtPoint` on
	 *  (`!cvar->IsCommand() && cvar->m_nValue != 0`, retail's inlined `ConVar::GetInt`):
	 *  `werewolf_show_debug`, shipped "0", which closes the draw. */
	int32 FakeHullDebugCvar() const;

	// From `ElysiumNpcHints.inl`.
	int32 TeleportHintNode = INDEX_NONE;  // +0x66b0 CNPC_VWerewolf::m_pTeleportHint (walked)
	int32 MoveHintNode = INDEX_NONE;      // +0x66bc CNPC_VWerewolf::m_pMoveHint (walked)
	bool bRandomHint = false;             // +0x66c8 CNPC_VWerewolf::m_bRandomHint (walked)
	int32 WerewolfDoorState = 0;          // +0x6680 CNPC_VWerewolf::m_DoorState (walked)
	TArray<FWerewolfHintGroundpoint> WerewolfHintGroundpoints;  // +0x6714 / +0x6720
	/** `CNPC_VWerewolf::ClearMoveHint` (`0x103d4690`). */
	void ClearMoveHint();
	/** `CNPC_VWerewolf::ClearTeleportHint` (`0x103d4760`). */
	void ClearTeleportHint();
	/** `CNPC_VWerewolf::FindHintEndEntity` (`0x103d6520`) — follow the hint's `m_strTargetName` to
	 *  another hint, then one further unchecked hop from that hint's own target name. */
	int32 FindHintEndEntity(const FHintWords& Hint) const;
	/** `CNPC_VWerewolf::GetHintGroundpoint` (`0x103d6770`) — the authored groundpoint for a hint, or
	 *  retail's `DevWarning` plus the plain `GetGroundpoint` fallback. SOURCE UNITS, because family
	 *  Motor's `GetGroundpoint` is and because the fallback can answer `vec3_invalid`, a SENTINEL that
	 *  no unit conversion may be applied to. */
	FVector GetHintGroundpoint(const FHintWords& Hint) const;
	/** Is `Value` retail's `vec3_invalid` — `DAT_10713de0/de4/de8`, which `staticinit_101371a0` fills
	 *  with `0x7f7fffff` (`FLT_MAX`)? `GetGroundpoint`'s no-hit answer, which `PositionAtHint` has to
	 *  recognise. */
	static bool IsVec3Invalid(const FVector& Value);
	/** `CNPC_VWerewolf::GetHintTeleportPriority` (`0x103d3220`) — the hint-type to priority map. */
	static int32 GetHintTeleportPriority(int32 HintType);
	/** `CNPC_VWerewolf::IsImperativeTeleportHint` (`0x103d3360`) — the authored-name ladder that says a
	 *  teleport hint must be taken. */
	bool IsImperativeTeleportHint(const FHintWords& Hint) const;
	/** SEAM for the Werewolf's vtable `+0x9a4` trace (slot 617), the last gate of the
	 *  `jump_to_platform` arm. Answers true — retail's "trace was blocked" answer, which is the arm that
	 *  does NOT call the hint imperative. */
	bool WerewolfHintTrace(const FVector& PositionCm) const;
	/** `CNPC_VWerewolf::IsValidBreakHint` (`0x103d8550`). */
	bool IsValidBreakHint(const FHintWords& Hint, double Now) const;
	/** `CNPC_VWerewolf::PositionAtHint` (`0x103d6280`) — snap to the hint's groundpoint and facing. */
	void PositionAtHint(const FHintWords& Hint);
	/** `CNPC_VWerewolf::SelectScheduleForHint` (`0x103ce9b0`) — the pure half: the three hint-type
	 *  answers plus the save-position distance test. Distances in SOURCE UNITS, as retail's are. */
	static int32 SelectScheduleForHint(const FHintWords* Hint, float DistToSavePositionUnits,
		float GoalToleranceUnits);
	/** The node-index form. */
	int32 SelectScheduleForHint(int32 HintNode) const;
	/** `CNPC_VWerewolf::SetHintActivity` (`0x103d6000`) — the pure half: the hint type to activity
	 *  switch, with the two random draws already made. */
	static int32 HintActivityForType(int32 HintType, bool bPercentRollPassed, bool bCoinFlip);
	/** The whole body: draw, switch, `PositionAtHint`, `RestartIdealActivity`. */
	bool SetHintActivity(const FHintWords& Hint);
	/** `CNPC_VWerewolf::SetMoveHint` (`0x103d44e0`). */
	void SetMoveHint(int32 HintNode, bool bRandom);
	/** `CNPC_VWerewolf::SetTeleportHint` (`0x103d45c0`). */
	void SetTeleportHint(int32 HintNode);

	// From `ElysiumNpcHints10.inl`.
	/** `CNPC_VWerewolf::GetHintEndEntity` (`0x103d6390`), 306 bytes — the cache in front of
	 *  `FindHintEndEntity` (`0x103d6520`, family Hints, already ported and CALLED here).
	 *
	 *  A linear scan of the `+0x6714` record array (count `+0x6720`, stride `0x48`) for the row whose
	 *  HINT word at `+0x04` is this hint and whose cached handle at `+0x00` resolves. **Correction to
	 *  the checklist walk**: the hit arm does not return "a second cached handle at the same slot" — it
	 *  re-reads `base[i * 0x12]`, which is the SAME word `+0x00`, and re-validates it. The redundancy is
	 *  retail's; the answer is the one cached handle, or null when the second read fails. */
	int32 GetHintEndEntity(const FHintWords& Hint) const;
	/** `CNPC_VWerewolf::GetHintEndpoint` (`0x103d6650`), 211 bytes. A null hint answers the global
	 *  `DAT_1070d1b0/b4/b8`, which `staticinit_101370b0` fills with `(0,0,0)` — `vec3_origin`. Any other
	 *  hint resolves its end entity through `GetHintEndEntity` and copies that entity's `GetAbsOrigin`
	 *  (vtable `+0x364`). CENTIMETRES, as every port position is. */
	FVector GetHintEndpoint(const FHintWords* Hint) const;
	/** The 14 hint types `GetForwardHintForHint` hands straight back, read off the switch at
	 *  `103d70dd`. **Correction to the checklist walk**: the run is NOT `0x3a9f..0x3aaa` — `0x3aa2` is
	 *  absent from the jump table, so the exempt set is `15000`, `0x3a99`, `0x3a9c`, `0x3a9f`, `0x3aa0`,
	 *  `0x3aa1` and `0x3aa3..0x3aaa`. */
	static bool IsForwardHintExemptType(int32 HintType);
	/** `CNPC_VWerewolf::GetForwardHintForHint` (`0x103d7090`), 274 bytes — the partner hint of type
	 *  `0x3a9c` that shares this hint's end entity, or null with retail's `DevWarning`. */
	int32 GetForwardHintForHint(const FHintWords& Hint) const;
	/** `CNPC_VWerewolf::IsValidTeleportHint` (`0x103d8300`), 457 bytes — seven ordered refusals and
	 *  then the endpoint's own availability. */
	bool IsValidTeleportHint(const FHintWords* Hint, double Now) const;
	/** The six hint types `IsValidTeleportHint` excludes outright, `0x3aa3..0x3aa8`, each its own `CMP`
	 *  in the listing (`103d83c8`..`103d8430`). */
	static bool IsTeleportHintExcludedType(int32 HintType);
	/** SEAM for `thunk_FUN_100b5190(endEntity)`, the LAST gate of `IsValidTeleportHint`. `0x100b5190` is
	 *  a seven-byte getter of `+0xf4`, which `docs/vtmb/npc-kernel/fields.md` names `m_bScriptHidden` —
	 *  **a correction to the checklist walk**, which called it an "entity-busy/occupied test". A hint
	 *  endpoint this substrate cannot resolve has no `m_bScriptHidden` to read, so this answers FALSE,
	 *  which is the ADMITTING value: retail returns the NEGATION, so a not-hidden endpoint is a valid
	 *  teleport hint and nothing is silently refused. */
	bool HintEndEntityScriptHidden(int32 EndEntityNode) const;

	// From `ElysiumNpcLifecycle.inl`.
	/** `+0x66a4` / `+0x66d4` / `+0x66d8` / `+0x66ec` — `CNPC_VWerewolf`'s morph-timer block, which its
	 *  own `ScriptUnhide` (`0x103d4a20`) zeroes and stamps. The first three are timers, the fourth an
	 *  absolute curtime stamp (`DAT_1070b228+0xc`), carried as `double` like every other stamp here.
	 *  NOTE `+0x66a4` is `CNPC_VMingXiao::m_flProxyReadyTimer` on a MingXiao — one offset, two species,
	 *  which is exactly why these are declared by retail class rather than by offset alone. */
	float WerewolfMorphTimerA = 0.f;    // +0x66a4 CNPC_VWerewolf (walked)
	float WerewolfMorphTimerB = 0.f;    // +0x66d4 CNPC_VWerewolf (walked)
	float WerewolfMorphTimerC = 0.f;    // +0x66d8 CNPC_VWerewolf (walked)
	double WerewolfUnhideStamp = 0.0;   // +0x66ec CNPC_VWerewolf (walked)
	/** `CNPC_VWerewolf::ScriptUnhide` (`0x103d4a20`) — the base, then `+0x66ec := curtime` and
	 *  `+0x66d8`/`+0x66d4`/`+0x66a4 := 0`, in that write order. */
	void WerewolfScriptUnhideTail(double Now);
	/** `CNPC_VWerewolf::StartSearchTimer` (`0x103d1ca0`) and `ReportSearchTimer` (`0x103d1d60`) — a
	 *  profiling pair over `rdtsc`, stamped into the STATIC pair `DAT_1093d638`/`DAT_1093d63c` and so
	 *  **shared across every instance** rather than kept per NPC. The report subtracts and leaves the
	 *  elapsed cycles in the same pair, and passes its second argument through unchanged.
	 *
	 *  Ported as file statics for exactly that reason. `ReportSearchTimer` answers its passthrough. */
	static void StartSearchTimer();
	static bool ReportSearchTimer(bool bPassThrough);

	// From `ElysiumNpcLifecycle19.inl`.
	static constexpr double WerewolfTeleportFloorSquare = 512.0; // `_DAT_104704c0`
	static constexpr float HullCentreHalf = ElysiumNpcTunables::Half;
	static constexpr double WerewolfFieldOfViewRadians = 2.0943951023931953; // `_DAT_104d0080`
	static constexpr float WerewolfSeekDistBaseUnits = 4096.f;
	static constexpr float WerewolfHearingScalarBase = 3.f;
	/** `CNPC_VWerewolf` words `0x103cac20` clears that no earlier family declared. */
	bool bWerewolfPlayFrustration = false;     // +0x66a9 `m_bPlayFrustration`
	int32 WerewolfMoveHintSearchStart = 0;     // +0x66b8 `m_pMoveHintSearchStart`, retail's NULL is 0
	int32 WerewolfWord66f8 = 0;                // +0x66f8 (retail name unrecovered)
	int32 WerewolfWord66fc = 0;                // +0x66fc (retail name unrecovered)
	void WerewolfRearm();

	// From `ElysiumNpcMisc.inl`.
	FElysiumEntityHandle WerewolfRotDoor1;   // +0x6684 CNPC_VWerewolf::m_hRotDoor1
	FElysiumEntityHandle WerewolfRotDoor2;   // +0x6688 CNPC_VWerewolf::m_hRotDoor2
	/** `0x103cade0` — `CNPC_VWerewolf`'s zone opener. Dispatches slot 251 on every entity whose
	 *  TARGETNAME is `trigger_werewolf_zone`, then caches the `rotdoor1` / `rotdoor2` entities in
	 *  `m_hRotDoor1` / `m_hRotDoor2` (`+0x6684` / `+0x6688`). Name inferred from the three hardcoded map
	 *  entity names; the retail name is unrecovered. */
	void TriggerWerewolfZone();
	int32 WerewolfZoneTriggerFires = 0;

	// From `ElysiumNpcMotor.inl`.
	/** `CBaseEntity::GetFlags2()` bit 3 — the second flag word `CNPC_VWerewolf`'s two collision-ignore
	 *  overrides test. **SEAM**: `FElysiumEntity::Flags` is the first word only; answers 0. */
	static uint32 RetailFlags2(const FElysiumEntity& Entity);
	/** `CNPC_VWerewolf::GetGroundpoint` `0x103d6a40`. */
	FVector GetGroundpoint(const FVector& PointUnits) const;

	// From `ElysiumNpcPositions.inl`.
	// `CNPC_VWerewolf`'s teleport words. The hint itself is family Hints' `TeleportHintNode` (+0x66b0)
	// and the word `TeleportOut` clears at +0x66e8 is their `WerewolfHintFlags`.
	double WerewolfLastSeenTime = 0.0;         // +0x66ec, stamped by TeleportIn and by the can-teleport pass
	double WerewolfTimeTeleportedOut = 0.0;    // +0x66f0 m_flTimeTeleportedOut
	int32 WerewolfWord66ac = 0;                // +0x66ac, zeroed by TeleportOut; its meaning is unrecovered
	float WerewolfTeleportDistanceA = 0.f;     // +0x66cc, the first term of the teleport distance floor
	float WerewolfTeleportDistanceB = 0.f;     // +0x66d0, the second term of the same sum
	// +0x6700 / +0x6704 — `GetNearestNodeToPlayer`'s refresh clock and its cached node id.
	double NearestNodeToPlayerRefreshedAt = 0.0;
	int32 NearestNodeToPlayer = 0;
	/** `CNPC_VWerewolf::EnemyCouldSeeHull` `0x103da230` — two gates, then the base body. */
	bool EnemyCouldSeeHullWerewolf(const FVector& OriginCm, bool bSkipViewCone, bool bUseHitbox,
		const FVector& ExtentsCm);
	/** The Werewolf's own gate on slot 617 — `ConVar` `DAT_1093d694` `werewolf_disregard_player_vision`
	 *  read as `IsCommand() ? 0 : m_nValue` (+0x2c). Shipped "0", which CLOSES the gate and makes the
	 *  Werewolf's `EnemyCouldSeeHull` answer false without tracing. */
	static bool WerewolfSightConVar();
	/** The enemy predicate the same body asks second: the active enemy's own vtable `+0x278`
	 *  (slot 158). **SEAM**: answers false. */
	static bool EnemySightPredicate(const FElysiumEntity& Enemy);
	/** `CNPC_VWerewolf::TeleportOut` `0x103d4a60`. */
	void TeleportOut();
	/** `CNPC_VWerewolf::TeleportIn` `0x103d4d60`. */
	void TeleportIn();
	/** The `ConVar` gate both halves put in front of their `dev/ww_tele_*.wav` — `DAT_1093f73c`
	 *  `werewolf_show_debug`, read as `IsCommand() ? 0 : m_nValue` (+0x2c); shipped "0", the arm that
	 *  plays nothing. */
	static bool WerewolfTeleportSoundConVar();
	/** The shared sound tail of both halves — `EmitSound(CSingleUserRecipientFilter(enemy), wav, 1.0,
	 *  level 100)`. `IElysiumAudio::PlayBodySound` is this runtime's `EmitSound(..., CHAN_*, ...)`
	 *  seam; the single-user filter is a recipient cull single-player never runs. */
	void PlayTeleportSound(const TCHAR* Rel);
	/** `CNPC_VWerewolf::UpdateConditionCanTeleport` `0x103cc0d0`. */
	void UpdateConditionCanTeleport();
	/** The threshold `UpdateConditionCanTeleport` measures its stamp against — `ConVar` `DAT_1093d414`
	 *  `werewolf_teleport_out_time`, `IsCommand() ? 0.0f : m_fValue`; shipped "4.0" seconds. */
	static float WerewolfTeleportDelayConVar();
	/** `FUN_103d0bf0` — the cached nearest-graph-node-to-the-player, refreshed no more often than
	 *  `_DAT_10450aa4` and `DevWarning`ing on a miss. */
	int32 GetNearestNodeToPlayer();

	// From `ElysiumNpcSensesBodies.inl`.
	/** `DAT_1093f8ec` and `DAT_1093d574` — the two ConVar objects `CNPC_VWerewolf::ShouldPursueEnemy`
	 *  (`0x103cf5f0`) thresholds on, each read `IsCommand() ? _DAT_104454c4 (0.0) : +0x28`:
	 *  `werewolf_pursuit_unseen_time` "3.0" and `werewolf_pursuit_distance` "800". */
	static float WerewolfPursueElapsedLimitSeconds();
	static float WerewolfPursuePlayerDistLimitUnits();
	/** `0x103cb810`, `CNPC_VWerewolf#201` — `FVisible`. The gate above, then **`true`
	 *  unconditionally**: a werewolf has no range check, no cone and no line of sight. On refusal it
	 *  zeroes the blocker out-parameter, which is retail's own write. */
	bool WerewolfFVisible(const FElysiumEntity* Candidate, FElysiumEntityHandle* OutBlocker);
	/** `0x103cf5f0`, `CNPC_VWerewolf::ShouldPursueEnemy`. `m_DoorState`-adjacent flag word `+0x66e8`
	 *  bit 2 (`& 4`) skips the whole test; otherwise BOTH of two independent gates must fail before a
	 *  werewolf gives up the chase. */
	bool WerewolfShouldPursueEnemy() const;

	// From `ElysiumNpcSenses10.inl`.
	/** `CNPC_VWerewolf::CheckStuck(bool)` (`0x103cb920`), 1,558 bytes, read off the listing (the
	 *  decompiler aliases its stack). Gated on slot 163 `IsViewable`. Probe 1 is a full-hull trace
	 *  from the origin 2.0 up. A clear START re-probes from `WorldSpaceCenter` to the origin through
	 *  the move probe (small hull while `+0x5f2d`, maxs.z x 0.45), and only a blocked re-probe moves
	 *  the body to its end and shrinks the hull. A solid start escalates through three small-hull
	 *  traces (the up-probe, then `WorldSpaceCenter` and `EyePosition` to `GetOrigin` with maxs.z
	 *  10): a clear up-probe ends in `SetHullSizeSmall(1)`, a clear later one in
	 *  `SetAbsOrigin(endpos)` and `SetHullSizeSmall(1)`. All three solid: with `EStuckEscape::MayTeleport` and
	 *  COND `0x77` it teleports out and fails the task, otherwise it only warns; neither stuck exit
	 *  nor the clear re-probe touches the hull. Both retail callers (`TaskFail` `0x103ce750`,
	 *  `StartTask` `0x103ccda0` task `0x15b`) pass 0 (`WarnOnly`). **SEAM**: the hull traces answer CLEAR
	 *  (`KernelHullTrace`). */
	/** `CheckStuck`'s one argument, retail's bool at `[ESP+0xdc]`: whether the still-stuck exit may
	 *  teleport out (under COND `0x77`) or only warns. */
	enum class EStuckEscape : uint8
	{
		WarnOnly,     // 0 -- both retail callers
		MayTeleport,  // 1 -- no shipped caller
	};
	void WerewolfCheckStuck(EStuckEscape Escape);
	/** How many times `CheckStuck`'s teleport arm ran `TeleportOut` (a test witness). */
	int32 WerewolfTeleportOutCalls = 0;
	/** `CNPC_VWerewolf::GetHintTargetGroundpoint` (`0x103d68d0`) — the TARGET variant of family Hints'
	 *  `GetHintGroundpoint` (`0x103d6770`): a linear scan of `m_HintData` (`+0x6714`, count `+0x6720`,
	 *  stride `0x48`) comparing the ENTITY POINTER at element `+0x04`, answering the Vector at element
	 *  `+0x14` — the TARGET groundpoint, not `+0x08`'s own groundpoint — and on a miss `DevWarning`ing
	 *  and falling back to `GetGroundpoint(GetHintEndpoint(hint))`. So a miss still answers a point.
	 *  SOURCE units, as family Hints' twin is. */
	FVector GetHintTargetGroundpoint(const FHintWords& Hint) const;
	/** SEAM for `CNPC_VWerewolf::GetHintEndpoint` (the hint's END entity's origin). It resolves through
	 *  family Hints' `FindHintEndEntity` (`0x103d6520`), which is a real recovered walk over a hint
	 *  store that does not exist yet, so it answers the hint's own origin and names what it stands for.
	 *
	 *  This family's companion seam for `GetForwardHintForHint` is GONE: story 29d, family **Hints10**
	 *  landed `0x103d7090` itself (`ElysiumNpcHints10.cpp`), so `GetForwardYawForHint` now calls
	 *  the real body — which answers null when no partner hint of type `0x3a9c` shares this hint's end
	 *  entity, and the yaw is then measured from the hint's own origin, the same fallback this seam
	 *  produced. */
	FVector GetHintEndpointUnits(const FHintWords& Hint) const;
	/** `CNPC_VWerewolf::GetForwardYawForHint` (`0x103d7210`). The working direction is seeded with
	 *  `vec3_invalid` (`DAT_10713de0`…), then overwritten by `endOrigin - forwardOrigin` normalised and
	 *  converted to a yaw through `0x101d2c70`; the hint TYPE then adjusts it. **Recovered from the
	 *  listing, because the decompiler lost the `float10` return storage and both tails read alike**:
	 *  the final compare is against `_DAT_10450568` = **360.0** and it is a WRAP, not a selection — see
	 *  the definition. */
	float GetForwardYawForHint(const FHintWords& Hint) const;
	/** `CNPC_VWerewolf::InitializeHintData` (`0x103d7710`), 1,178 bytes — the one-shot build of the
	 *  `+0x6714` array, which runs only while `+0x6720` is zero. It walks the global hint chain
	 *  (`DAT_10925450`, `+0x18` next) and per hint stores the hint, its end-entity handle, its own
	 *  groundpoint and its TARGET groundpoint, writing the forward yaw back through the hint's own
	 *  angles and falling back to the raw origin / raw endpoint when a groundpoint fails the
	 *  `0x7f800000` exponent test. **SEAM**: the global hint chain is family Hints' `HintWords` seam and
	 *  resolves nothing, so the array stays empty — retail's own answer for a map with no hints — and
	 *  the per-hint rule is exercised through `InitializeHintDataRow`. */
	void InitializeHintData();
	/** One row of the build above, applied to one hint. Separated so the recovered per-hint rule is
	 *  testable while the chain that feeds it is a seam. */
	FWerewolfHintGroundpoint InitializeHintDataRow(const FHintWords& Hint) const;
	/** Retail's `(bits & 0x7f800000) == 0x7f800000` validity test on each component of a groundpoint —
	 *  an infinity or a NaN exponent, which is what `vec3_invalid` (`FLT_MAX`) is NOT, so a `FLT_MAX`
	 *  groundpoint passes this test and is stored. Recorded because it is the surprising half. */
	static bool IsGroundpointExponentValid(const FVector& PointUnits);
	/** Slot 566 `FValidateHintType` applied to a hint whose WORDS are already in hand. Retail passes the
	 *  `CAI_Hint*` itself (`vtable +0x8d8`), and both bodies below call it that way; the node-index entry
	 *  `FValidateHintTypeNode` re-resolves through the hint-store seam, which resolves nothing — so these
	 *  two bodies would refuse at the gate rather than reaching their own type ladders. This hands the
	 *  words the caller already has to the virtual slot. */
	bool ValidateHintTypeForWords(const FHintWords& Hint) const;
	/** `CNPC_VWerewolf::IsValidRandomMoveHint` (`0x103d7dc0`) and `::IsValidMoveHint` (`0x103d8060`) —
	 *  the two 520-byte twins whose type sets differ in BOTH membership and sense. `Now` is the
	 *  substrate clock; the cooldown list is family Species' `FUN_10366400`. */
	bool IsValidRandomMoveHint(const FHintWords& Hint, double Now);
	bool IsValidMoveHint(const FHintWords& Hint, double Now);
	/** `m_iRandomMoveHintNodeZone` (`+0x670c`) — the node zone the random-move arm requires the cached
	 *  nearest node's `+0x94` to equal. **SEAM**: no node graph, so `0x103d0ad0` answers "no node" and
	 *  the arm refuses, which is retail's own answer when the cache is empty. */
	int32 RandomMoveHintNodeZone = 0;
	bool CachedNearestNodeZone(int32& OutZone) const;

	// From `ElysiumNpcSpecies.inl`.
	// `CNPC_VWerewolf`'s frame-memoised chase cache. `m_DoorState` (+0x6680) is family **Hints**'
	// `WerewolfDoorState` and is read through it. No datamap names these; they are walked off
	// `0x103d9c90`.
	int32 WerewolfChaseFrame = INDEX_NONE;                        // +0x6670 (walked)
	FVector WerewolfChasePosUnits = FVector::ZeroVector;          // +0x6674/+0x6678/+0x667c (walked)
	/** SEAM for `(**(code **)(*DAT_1070b22c + 0x1e0))()` — the ENGINE FRAME NUMBER `0x103d9c90`
	 *  memoises its chase position on. This runtime's clock is `FElysiumEntityWorld::NowSeconds()` and
	 *  carries no frame counter at all.
	 *
	 *  **NAMED DECISION**: this answers `INDEX_NONE`, and the body reads that as "the stamp can never
	 *  match", so the cache is ALWAYS STALE and the position is recomputed on every call. Retail calls
	 *  the body at most once per frame per NPC, so recomputing per call is retail's own answer at
	 *  retail's own call rate; answering a CONSTANT frame instead would freeze the cache after its
	 *  first fill and hand every later caller a stale point, which is the one behaviour retail never
	 *  has. The word `+0x6670` is still written, so a frame counter arriving later needs no other edit. */
	int32 EngineFrameNumber() const;
	/** `0x103d1e50` — `CNPC_VWerewolf`: are the two door halves near enough to count as shut? */
	bool FUN_103d1e50() const;
	/** `0x103d9c90` — `CNPC_VWerewolf`'s frame-memoised chase position. SOURCE units out. */
	void FUN_103d9c90(FVector& OutPositionUnits);

	// From `ElysiumNpcSpeciesLifecycle10.inl`.
	/** `CNPC_VWerewolf::~CNPC_VWerewolf` (`0x103ca7c0`). Retail, in order:
	 *    1. The two vftable restores (not portable, not observable).
	 *    2. Under the scope frame: `DAT_1093fac4 = 0`, then `werewolf_show_debug`'s ConVar slot 4
	 *       (`SetValue`) with 0. The ONE thing outside this object the body touches.
	 *    3. Destroy five outputs in this order: `m_OnTeleportIn`, `m_OnTeleportOut`,
	 *       `m_OnFinishCrushAnimation`, `m_OnBeginCrushAnimation`, `m_OnConditionDeathTriggered`.
	 *    4. Walk the hint-data vector (`+0x6714`, count `+0x6720`, stride **0x48**) BACKWARDS from
	 *       `count - 1`, destroying each record with `0x103dc5b0`; zero the count; then `0x103dc220`
	 *       over the vector.
	 *    5. The `CUtlMemory` teardowns of the `+0x6714`, `+0x668c` and `+0x665c` blocks, each under a
	 *       "grow size is not -1" test (allocator only).
	 *    6. `~CAI_BaseNPCTroika`.
	 *  `+0x6714`/`+0x6720` is the array family Hints carries as `WerewolfHintGroundpoints`, so step 4 is
	 *  a real clear here and the backwards walk is the recovered order rather than a `Reset()`. */
	void DestroyWerewolf();
	/** How many hint-data records the destructor above tore down, and in what order they were visited.
	 *  Retail walks descending; the list records the index of each visit so the ORDER is assertable. */
	TArray<int32> WerewolfHintTeardownOrder;

	int32 WerewolfBreakHintNode = INDEX_NONE;       // +0x66c4 `m_pBreakHint`
	int32 WerewolfLastUsedTeleportHint = INDEX_NONE; // +0x66b4 `m_pLastUsedTeleportHint`
	int32 WerewolfLastUsedMoveHint = INDEX_NONE;     // +0x66c0 `m_pLastUsedMoveHint`
	bool bWerewolfTaskFailed = false;                // +0x66a1 (walked; retail name unrecovered)
	int32 WerewolfHintNodeCacheA = INDEX_NONE;       // +0x6708 (walked; retail name unrecovered)
	/** `CNPC_VWerewolf::HasPath` (`0x103d0db0`). 120 of its 138 bytes are the scope-trace frame; the
	 *  body is one call, `0x102ee380(m_pNavigator, start, end)`, which stamps the navigator cache twice
	 *  — `+8` from the NPC's own `+0x156c` hull and `+0xc` from the global frame word, then the same
	 *  pair on the path object `0x102ecc00` — before forwarding both Vectors to `0x102fdcc0`.
	 *
	 *  CORRECTION: the stamping is inside `0x102ee380`, not in this body, and the body itself neither
	 *  reads nor writes anything of its own. `0x102fdcc0` is the seam: this substrate stands no path
	 *  object, so it answers **false** — retail's own answer for a navigator with no path. */
	bool WerewolfHasPath(const FVector& StartUnits, const FVector& EndUnits) const;
	mutable TArray<FHasPathQuery> HasPathQueries;
	/** `CNPC_VWerewolf::DrawDebugStatOverlays` (`0x103d5130`), slot 76's species arm. PREPENDS
	 *  `Not Seen Time` and `Player Distance`, CHAINS `CNPC_VMingXiao`'s arm (`0x10366290`), then APPENDS
	 *  the zone word bit by bit, five conditions, the door state, a cvar-selected hint dump and the last
	 *  five rows of the schedule stack.
	 *
	 *  `Not Seen Time` is `max(curtime - +0x66ec, 0.0)` — the clamp at `103d51aa` is against
	 *  `_DAT_104454c4` = 0.0 and the decompiler drops it — and `Player Distance` is `+0x6264`
	 *  (`m_flPlayerDist`), NOT a computed range. */
	void WerewolfDrawDebugStatOverlays(TArray<FString>& OutLines) const;
	/** `+0x668c` (the schedule-stack array) and `+0x6698` (its count) — the last five rows the overlay
	 *  prints, a null row printing `INVALID SCHEDULE`. Carried as the names the overlay reads because
	 *  that is every observable the body produces. */
	TArray<FString> WerewolfScheduleStack;
	/** `CNPC_VWerewolf::SnapToAnimationPoint` (`0x103d9f90`), six writes and two calls in order:
	 *  `MatchOriginAnglesToAnimation("Bip01", 1, 1)` snaps origin AND angles onto the animation's root
	 *  bone; `SetHullSizeSmall(force = 0)`, which therefore does nothing when the body is already small;
	 *  the cached fake-hull point `+0x66dc`/`+0x66e0`/`+0x66e4` cleared from `vec3_origin`; `+0x66a8` =
	 *  0; and both hint-node handles `+0x6708` / `+0x670c` = -1, so the snap also drops whatever hint the
	 *  Werewolf was heading for. */
	void SnapToAnimationPoint();
	int32 WerewolfSnapWordA = 0;   // +0x66a8 (walked; retail name unrecovered)
	TArray<FMatchOriginAnglesCall> MatchOriginAnglesCalls;

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	// From `ElysiumNpcGeometry.inl`.
	/** SEAM for `CollisionProperty::CalcNearestPoint` (`0x100dd000`) — the nearest point on an entity's
	 *  OBB to a world point, which becomes the damage POSITION. No collision property here; answers the
	 *  point unchanged, which is `CalcNearestPoint`'s own answer for a point already inside the box. */
	FVector NearestPointOnEntity(const FElysiumEntity* Entity, const FVector& PointCm) const;
	/** SEAM for slot 323 (`0x10344dd0`, `int vfunc323(const Vector&)`) and slot 320
	 *  (`PlayerKnockbackReaction(CBaseCombatCharacter*, Activity)`) dispatched on the PUSHED entity.
	 *  Both slots exist on this leaf and are 29e's stubs; when the pushed entity is an NPC they are
	 *  called for real and when it is not they record and answer 0 / false. */
	int32 PushedEntityDirectionClass(FElysiumEntity* Pushed, const FVector& DeltaCm) const;

	// From `ElysiumNpcMisc.inl`.
	/** **SEAM** for `zone->vtable[+0x3ec]` (slot 251) on a `trigger_werewolf_zone` entity. On the
	 *  `CAI_BaseNPC` line slot 251 is `IsActivityFinished`, but a trigger is a different hierarchy
	 *  sharing the index and the census does not carry its table — so what this fires is
	 *  **unrecovered**. Counted, and nothing is dispatched. */
	void FireWerewolfZoneTrigger(FElysiumEntity& Zone);

	// --- 0019/8 shape: forwarding overrides (replace the body, keep the declaration) ---
	virtual void Spawn() override;
	virtual int32 OnTakeDamage(void* Arg0) override;
	virtual bool HandleAnimEvent(const FElysiumAnimEvent& Event) override;
	virtual void NPCThink() override;
	virtual void GatherConditions() override;
	virtual int32 SpeciesSelectSchedule() override;
	virtual int32 StartTaskSlot442(void* Arg0) override;
	virtual int32 RunTaskSlot444(void* Arg0) override;
};
