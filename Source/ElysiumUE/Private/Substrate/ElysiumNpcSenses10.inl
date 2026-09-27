// Story 29d, family **Senses10** — the declarations of this family's layer 10–18 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual and this family only defines it. What lands here is
// the non-slot half — the base-class bodies beneath a Troika override, the species arms the slot
// dispatchers run, the retail helpers those bodies call, and the seams that stand for retail inputs
// this substrate has no source for.
//
// The definitions are in `Substrate/ElysiumNpcSenses10.cpp` (the Troika line) and
// `Substrate/ElysiumNpcSenses10_2.cpp` (the species line); the tests are
// `Tests/ElysiumNpcKernelSenses10Tests.cpp`. The walked prose is `docs/vtmb/npc-ai/senses.md`
// § "Story 29d, family Senses10 — …".
//
// --- What this family is --------------------------------------------------------------------------
//
// **What the NPC perceives and who its enemy is.** Twenty-four `Senses10` rows and ten
// `SpeciesSenses10` rows: slot 201 `FVisible` and the slot 594 range/concealment test under it, slot
// 467 `QueryHearSound`, slot 468 `QuerySeeEntity`, slot 469 `OnLooked` with the `CAI_BaseNPC` base
// body beneath it, slot 472 `OnSeeEntity`, slot 478 `BestEnemy`, slot 544 `UpdateEnemyMemory`, the
// weapon-LOS pair (562/573), the aim pair (538/574), slots 223/402/445, and the species arms over
// `FVisible`, `OnSeeEntity`, `BestEnemy`, `GetShootEnemyDir`, `FInViewCone` and
// `FValidateHintType`, plus the Werewolf's hint-validity and stuck bodies and the Scurrying pair.
//
// THREE STANDING FACTS OF THIS FAMILY, stated once here rather than at thirty call sites.
//
//   * **`+0x9c` is the entity's own `CBaseCombatCharacter` self-downcast cache, so `cand->+0x9c` is
//     `cand` for every combat character and null for everything else.** Every `+0x9c` test in this
//     family therefore reads "is this a combat character", and every pointer compare against a
//     `+0x9c` (`CNPC_VZombie::FVisible`, `CNPC_VFrenzyShadow::BestEnemy`) is a compare against the
//     entity itself. This is what makes `FElysiumNpcFrenzyShadow::BestEnemy` answer an entity
//     rather than a subobject, and it is why the port can use `FElysiumEntity::AsCombatCharacter()` for both.
//   * **`+0x6081` is `m_bSeenInOuterBand` and its ONE writer is slot 594.** The shape map
//     (`ElysiumNpcKernelShapeMap.cpp`) binds it to `FElysiumNpcMemory::bPlayerInOuterBand`. Slot 594
//     clears it on entry and sets it when the target is beyond `_DAT_10457f54` (**0.7**) of the
//     effective vision radius; slot 472 `OnSeeEntity` and both `OnSeeEntity` species arms read it.
//     The port's `ElysiumNpcSense::OuterBandFraction` IS that constant.
//   * **Retail's distance keys in this family are `__ftol` of the SUM OF SQUARES, not of a root.**
//     `0x10431320` is plain `__ftol` (39 bytes, no callees) and both `BestEnemy` bodies hand it the
//     unrooted sum. The checklist's walk of `CNPC_VFrenzyShadow::BestEnemy` says "`__ftol` of the
//     squared-distance root"; the listing (`1037697d`) shows no `fsqrt`. Corrected here.

// --- Slot 404 / 405: the two dispositions this family reads --------------------------------------
//
// Slot 404 `IRelationType` (`0x10299da0`) is family **Conditions10**'s row of this same story. When
// this family landed it was still the generated stub answering `0` (`D_ER`) and dispatching it would
// have told every body below that every entity is an error relation, so both helpers read the store
// the Troika body's tail reaches through `CBaseCombatCharacter::IRelationType` —
// `FElysiumRelationships` — mapped onto retail's `Disposition_t` ids.
//
// **`IRelationTypeOf` is now the promised one-line forward**: Conditions10 landed `0x10299da0` with
// all three of its forwarding arms and the four species overrides, so this reads slot 404 itself.
// `IRelationPriorityOf` does NOT forward, and that is a slot boundary rather than a gap: slot **405**
// (`0x10333700`) is a layer-0 row of story 29c's band and is still the generated stub, so its
// dispatch would answer 0 for everything. See the `.cpp` at each body.
//
// Retail's ids: `D_ER 0`, `D_HT 1`, `D_FR 2`, `D_LI 3`, `D_NU 4`. `FElysiumRelationships::Resolve`
// never answers `D_ER`, so retail's `default:` arms (the `OnLooked` `DevWarning`, `QuerySeeEntity`'s
// refusal) are unreachable through this helper and say so at each site.
int32 IRelationTypeOf(const FElysiumEntity* Candidate) const;

// --- Slot 201 `FVisible`: the blocker out-parameter ----------------------------------------------

/** SEAM. Retail's slot 201 is `bool FVisible(CBaseEntity*, int mask, CBaseEntity** ppBlocker, int)`
 *  and its refusal arms write `*ppBlocker = 0` ASYMMETRICALLY — the `npc_ignore_senses` and
 *  `npc_ignore_player` arms write it, the null-target arm does NOT, and slot 594 writes it on its
 *  range refusal and its concealment refusal but not on the far-band arm. The generated signature
 *  spells the third parameter `FElysiumEntity*` (a value, not a cell), so the write cannot be
 *  delivered to the caller; it is COUNTED here instead, with the target it was made for, so
 *  retail's asymmetry is observable rather than silently dropped. Nothing in this runtime passes a
 *  blocker yet: the Troika body itself passes `0` to slot 594, which is retail. */
int32 FVisibleBlockerWrites = 0;
FElysiumEntityHandle LastFVisibleBlockerTarget;
void WriteFVisibleBlocker(const FElysiumEntity* Target);

/** `CBaseEntity::FVisible`, the trace slot 201 ends at once every gate has passed: an eye-to-eye
 *  segment with retail's caller-supplied mask. The mask is this runtime's channel question and the
 *  embodiment answers a plain "is the segment clear", so the mask is carried for the record. */
bool BaseEntityFVisible(const FElysiumEntity& Target, int32 Mask) const;

// --- Slot 469 `OnLooked`: the `CAI_BaseNPC` base body beneath the Troika override ----------------

// --- Slot 472 `OnSeeEntity`: the two species arms and their class statics -------------------------

static void ResetSpeciesSuspectGlobals();

// Slot 478 `BestEnemy`: the Troika line runs `CAI_BaseNPC`'s body (`FElysiumNpcBase::BestEnemy`);
// its one species arm, `CNPC_VFrenzyShadow#478` (`0x103766d0`), is `FElysiumNpcFrenzyShadow`'s
// override, with the two words it owns (fold A2).

// --- Slot 574 `GetShootEnemyDir` and the aim point behind it -------------------------------------

// --- Slot 562 `WeaponLOSCondition`: the player-in-line-of-fire test ------------------------------

// --- Slot 445 `StartTaskOverlay`: the move-and-shoot overlay's two words -------------------------

// `m_flBurstShootPauseMin` (`+0x5bbc`) and `m_flBurstShootPauseMax` (`+0x5bc0`), the pair slot 445
// hands `0x102e8270`, are already declared on `FElysiumNpc` itself (story 29b's shape). Slot 445 is
// their first reader in the kernel closure.

// --- Slot 223 `CreateVPhysics`: the shadow the callee builds -------------------------------------

// --- Slot 402 `Event_Gibbed` ---------------------------------------------------------------------

// --- Slot 544 `UpdateEnemyMemory`: the squad gate and the eluded gate ----------------------------

// --- Slot 594 `Slot594`: the range and concealment test under `FVisible` -------------------------
//
// The slot itself is generated; what it needs and this runtime has no word for is below.

/** `m_flSeekDistInspection` (`+0x63b8`) — the effective vision distance slot 594 multiplies the
 *  target's own slot-28 scalar by. It IS `FElysiumNpcSenses::Perception.VisionDistanceCm`; named
 *  here so the body reads as retail's and the unit conversion happens in one place. */
float SeekDistInspectionCm() const;

/** The target's slot 28 (`vtable +0x70`) — its own stealth VISION scalar, the twin of slot 30's
 *  hearing reduction that `AdjustSoundDistForStealth` reads. `1.0` for a target carrying no stealth
 *  surface, which is every character except the player. */
float TargetStealthVisionScalar(const FElysiumEntity& Target) const;

// --- Slot 467 `QueryHearSound` -------------------------------------------------------------------

/** `CStealthKillRules::InDeafZone(&DAT_1072c540, player, this)` — slot 467's sound-type-4 arm.
 *  Forwards to the rulebook's own deaf-zone rule; false with no rulebook, which is retail's answer
 *  for a sound whose owner carries no player record. */
bool SoundOwnerInDeafZone(const FElysiumEntity* Owner) const;

// --- `CAI_BaseNPC`'s head probe ------------------------------------------------------------------

// --- `CNPC_VCameraSecurity` (slots 201 and 363) --------------------------------------------------

/** SEAM for `CSecCamera::CanSee` (`0x1020cd00`) and `CSecCamera::InViewCone` (`0x1020cc60`), the two
 *  tests the camera itself performs — the enabled byte `+0x7d8`, the 2-D distance against the far
 *  radius `+0x794` and the near radius `+0x790` (`0x1020cd30`), the cone dot against `+0x798`, and a
 *  `0x4091` trace whose fraction must equal `_DAT_10449280` (**1.0**). `FElysiumNpcCameraSecurity::ResolveSecCameraLink`
 *  (family Dialogue, `0x10369e70`) is the link; the camera entity carries none of those five words
 *  on this substrate, so both answer **false** — which is retail's answer for a camera that is
 *  switched off, and the arm that leaves a security NPC blind rather than omniscient. */
bool SecCameraCanSee(const FElysiumEntity* Camera, const FElysiumEntity* Target) const;
bool SecCameraInViewCone(const FElysiumEntity* Camera, const FElysiumEntity* Target) const;

// --- `CNPC_VBach` (slot 566) ---------------------------------------------------------------------
//
// `CNPC_VBach::FValidateHintType` (`0x10365800`) is `FElysiumNpcBach`'s own override (story 5
// step 4): 17000..17005 outright, every other type falling through into the Troika body.

// --- `CNPC_VScurrying` ---------------------------------------------------------------------------

/** SEAM for `CAI_BaseNPCTroika::IsAreaClear(pos, mask, 0, 0)` — the jitter arm's acceptance test.
 *  This runtime has no hull sweep; answers true, which admits the jittered point, and the march arm
 *  below is the one a test can drive end to end. */
bool IsAreaClear(const FVector& PositionCm, int32 Mask) const;

// --- `CNPC_VWerewolf` ----------------------------------------------------------------------------

// `SetHullSizeSmall(bForce)` (`0x10273180`) — the tail every `CheckStuck` exit but the teleport ends
// in — is family **Motor10**'s body and is declared in `ElysiumNpcMotor10.inl`. Dispatched
// here, not re-ported.

