// Story 29d, family **SpeciesLifecycle10** — the twelve per-species spawn, touch, restore, destroy
// and think bodies of layers 11-17.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. **No body in this family is a Troika-line slot body**, so nothing
// here is declared by the generator and nothing here is `hand:` — every row is either a species
// override of a slot another family owns (an arm added to that family's method) or a class body on
// a leaf that is not `FElysiumNpc` at all (`CNPCMaker`, whose three `Spawn` bodies land on
// `FElysiumNpcMaker`).
//
// The definitions are in `Substrate/ElysiumNpcKernelSpeciesLifecycle10.cpp`, the maker's three in
// `Substrate/ElysiumNpcMaker.cpp`, and the tests in
// `Tests/ElysiumNpcKernelSpeciesLifecycle10Tests.cpp`. The walked prose is
// `docs/vtmb/npc-ai/lifecycle.md` § "Story 29d, family SpeciesLifecycle10".
//
// --- What this family is --------------------------------------------------------------------------
//
// Twelve rows, four shapes:
//
//   * **Two destructors** (`CNPC_VAndreiBlood` `0x1035cd00`, `CNPC_VWerewolf` `0x103ca7c0`). Almost
//     all of both is allocator teardown a garbage-collected runtime cannot observe; the observable
//     halves are Andrei's two owned emitter entities, which do not outlive him, and the Werewolf's
//     reset of the `werewolf_show_debug` ConVar and its global.
//   * **Two species `Restore`s** (`CNPC_VAndreiBlood` `0x1035cf80`, the three Chang brothers
//     `0x1036b170`), which are the last two arms family SaveRestore10 left routed to the base
//     `CNPC_VVampireBoss::Restore` with a comment saying "the day the owning story lands their own
//     halves, each row's `Body` moves to it". This is that story; the rows now point here.
//   * **Three maker `Spawn`s** (`CNPCMaker` `0x1034afe0`, `_Fleshpile` `0x1034c020`, `_Zombie`
//     `0x1034cc60`). One method with three arms on `FElysiumNpcMaker`, the shape that class already
//     uses for slots 104, 139, 617 and 618.
//   * **Five species arms on slots another family owns**: slot 431 `NPCThink` (`CPayphone`), slot
//     174 `StartTouch` (`CNPC_VGhoulCroucher`), slot 175 `Touch` (`CNPC_VGargoyle`) and slot 463
//     `OnStateChange` twice (`CNPC_VGuard1`, `CNPC_VHunter`).
//
// --- The corrections this family's reading made to the checklist's walks --------------------------
//
//   * **`+0x66b0` and `+0x66b8` are swapped in all three maker walks.** `vtmb_fields CNPCMaker` puts
//     `m_cLiveChildren` at **`+0x66b0`** and `m_flGround` at **`+0x66b8`**, and the listing
//     (`1034b065 MOV dword ptr [ESI + 0x66b0],0x0` *before* `1034b06f CALL dword ptr [EAX + 0x1a0]`,
//     then `1034b0bc MOV dword ptr [ESI + 0x66b8],0x0` last) agrees. The walk has the order right
//     and the two offsets the wrong way round.
//   * **`m_Collision` is `+0x270`, not `+0x17c`.** `1034b04c LEA ECX,[ESI + 0x270]` is what
//     `SetSolid(SOLID_NONE)` is called on; `+0x17c` is `m_flNextThink`, the *other* word the body
//     writes, and the walk conflated them.
//   * **`CAI_BaseNPC::FUN_101a67e0` at vtable `+0x29c` (slot 167) is `GetEnemy()`, not "a door
//     reference".** Its whole body resolves `m_hEnemy` out of the global entity table. Both
//     `OnStateChange` walks call it a door and `CNPC_VGuard1`'s calls `0x1037e2d0` "an
//     obstructing-door hook": `0x1037e2d0` sets `+0x6660` and calls `InputSetRelationship(this,
//     "player D_HT 10", 0)`. The arm is **"my enemy is the player, so hate the player at priority
//     10"**, and there is no door anywhere in either body.
//   * **`+0xa8` is `m_pPlayer` and `+0x94` is the cached `CAI_BaseNPC*`.** Both readings are already
//     recorded by families Senses and Conditions; applied here, `CNPC_VGhoulCroucher::StartTouch`'s
//     first gate reads **"the toucher is the player, or the toucher is an NPC"** rather than the
//     walk's two unnamed offsets.
//   * **`CPayphone`'s mirror copies the partner's animation CYCLE.** `+0xff0` is `m_IdealActivity`
//     (the walk says `m_Activity`) and `+0x6f8` is `m_flCycle` (the walk leaves it unnamed). With
//     both named, "mirrors its partner frame for frame" is literal: the payphone takes the partner's
//     ideal activity, its own weighted sequence for it, and then the partner's *phase*.
//   * **`CNPC_VAndreiBlood`'s two owned handles are `+0x66e0` and `+0x66e4`.** The walk calls them
//     "the two owned entity handles that sit past the class tail" because the decompiler renders
//     them as `this + 1` and `this[1].field_0x4`; the listing (`1035cd62`, `1035cdd3`) gives the
//     offsets and family Damage had already recovered both as `AndreiBloodEmitter` (`0x1035e1a0`
//     `StartBloodEmitter`) and `AndreiSummonEmitter` (`0x1035e3c0` `StartSummonEmitter`).
//   * **`CNPC_VWerewolf`'s `+0x6714` vector is the hint-data array.** `+0x6714` / `+0x6720` are the
//     `CUtlVector<WerewolfHintData_t>` `InitializeHintData` (`0x103d7710`) fills and
//     `GetHintGroundpoint` / `GetHintTargetGroundpoint` / `GetHintEndEntity` / `GetDataForHint`
//     read, which family Hints already carries as `WerewolfHintGroundpoints`. The 0x48-byte stride
//     the walk quotes is that record's size.
//   * **`CNPCMaker_Zombie`'s disabled path installs a NULL think, and Relink runs on both paths.**
//     The walk says so for the zombie and says the opposite ("with no next-think stamp at all;
//     both paths then Relink") for the base maker, where `1034b0c8 PUSH 0x1000572c` installs a
//     *non-null* inert think. Both are right; the difference is recorded because it is the whole
//     reason the zombie body is 34 bytes longer.
//
// --- The `.rdata` cells, read out of the pinned `vampire.dll` --------------------------------------
//
// File offset = address - `0x10000000` (`.rdata` is identity-mapped), against the install named by
// `ELYSIUM_VTMB_ROOT`:
//
//   `_DAT_10450aa4` = **0.01f**   — `CPayphone::NPCThink`'s mirror next-think delta (a 100 Hz tick)
//   `_DAT_1044bef8` = **0.25f**   — `CPayphone::NPCThink`'s idle next-think delta
//   `_DAT_104ada44` = **2.3f**    — `CNPC_VChangBros::Restore`'s `m_fJumpGravity`
//   `_DAT_1044ffd0` = **5.0**     — a DOUBLE (`1037c022 FADD double ptr [0x1044ffd0]`),
//                                   `CNPC_VGhoulCroucher::StartTouch`'s touch-burn re-arm interval
//
// Two more were read while checking the sibling `registry:127` rows this family does not own, and
// are recorded so the next reader does not have to: `_DAT_104a9300` = **2.0f**
// (`CNPC_VAsianVampire::Restore`'s jump gravity) and `_DAT_104c6148` = **2.0f**
// (`CNPC_VSheriffMan::Restore`'s).

// -------------------------------------------------------------------------------------------------
// `CPayphone::NPCThink` — slot 431, `0x101aabf0`.
// -------------------------------------------------------------------------------------------------

/** `m_hDialogPartner` (`+0x0fe8`), as an ENTITY HANDLE.
 *
 *  **SEAM, and it answers nothing.** This runtime carries the dialogue partner as the open session
 *  (`FElysiumNpcDialogue::bInDialog` plus the talk-end stamp), which is the reading family Anim's
 *  `HasLiveDialogPartner()` and family Sounds' `IsInDialog` both already made — a boolean, not a
 *  handle. `CPayphone::NPCThink` is the first body in the kernel that needs the partner as an
 *  entity, because it reads two of the partner's own animation words off it, so the handle is
 *  declared here at its retail offset with no writer. Nothing in this runtime assigns it, so a
 *  payphone takes retail's own no-partner arm — which is the admitting arm: a payphone standing
 *  alone idles and re-thinks on the 0.25 s clock, exactly as retail's does. */
FElysiumEntityHandle DialogPartner;   // +0x0fe8 m_hDialogPartner (SEAM: no writer)

/** The partner resolved live, or null. Retail's test is the three-part EHANDLE validity check
 *  (`index & 0x1fff`, serial `>> 0xd`, non-null record) at `101aac0b`..`101aac2c`. */
FElysiumEntity* ResolveDialogPartner() const;

/** SEAM for `FUN_102c1400` (`0x102c1400`), the dialogue upkeep tick both payphone arms run. It is a
 *  237-instruction body of its own — the scene-entity release, `FinishTalking` when the talk
 *  finished, the queued-line pump, the disposition switch to schedule `0xf1` and
 *  `CDialog::ShowPlayerChoices` — and it is **not one of this family's rows**. Counted here so the
 *  payphone's call ORDER (tick before the activity mirror, tick before the idle) is assertable, and
 *  named so the day it is walked the call site is already correct. */
int32 DialogUpkeepTicks = 0;
void DialogUpkeepTick();

/** How many payphone passes took the mirror arm and how many took the idle arm. */
int32 PayphoneMirrorPasses = 0;
int32 PayphoneIdlePasses = 0;

/** `CPayphone::vfunc431` (`0x101aabf0`), slot 431 — the WHOLE think for a payphone. It does not call
 *  `CAI_BaseNPCTroika::NPCThink` at any point, so a payphone runs no schedule, no senses and no
 *  motor: it is an NPC whose entire behaviour is to copy its dialogue partner's pose.
 *
 *  Retail, in the listing's order:
 *    1. slot 250 `StudioFrameAdvance(0.0)`, its float return discarded (`101aabf8` then
 *       `101aac07 FSTP ST0`).
 *    2. If `m_hDialogPartner` (`+0x0fe8`) resolves live:
 *         a. the dialogue upkeep tick `0x102c1400`, UNCONDITIONALLY;
 *         b. if the partner's `m_IdealActivity` (`+0xff0`) differs from mine:
 *              `SetIdealActivity(theirs)`; `m_nSequence` (`+0x6f0`) =
 *              `SelectWeightedSequence(theirs, -1)`; `ResetSequenceInfo()` (`0x10090950`);
 *         c. ALWAYS `m_flCycle` (`+0x6f8`) = the partner's `m_flCycle`;
 *         d. `m_flNextThink` = curtime + `_DAT_10450aa4` (**0.01 s**).
 *       and RETURNS — nothing below runs.
 *    3. Otherwise: `if (IsInDialog())` the same tick; then `SetIdealActivity(1)` (`ACT_IDLE`)
 *       UNCONDITIONALLY; then `m_flNextThink` = curtime + `_DAT_1044bef8` (**0.25 s**).
 *
 *  Note (b) versus (c): the sequence is re-selected only on an activity CHANGE, but the cycle is
 *  copied on every pass. That is what makes the mirror frame-accurate, and it is why the arm needs
 *  a 0.01 s clock.
 *
 *  Answers whether the payphone arm owned the pass, so `FElysiumNpc::Think`'s prologue can return. */
bool PayphoneThink();

/** `_DAT_10450aa4` — **0.01f**, read at file offset `0x450aa4` of the pinned `vampire.dll`. */
static constexpr double PayphoneMirrorThinkSeconds = 0.009999999776482582;
/** `_DAT_1044bef8` — **0.25f**, read at file offset `0x44bef8`. */
static constexpr double PayphoneIdleThinkSeconds = 0.25;
/** `101aac9c PUSH 0x1` — `ACT_IDLE`. */
static constexpr int32 PayphoneIdleActivity = 1;

// -------------------------------------------------------------------------------------------------
// Slots 174 `StartTouch` and 175 `Touch` — the `CBaseEntity` bodies and their two species arms.
// -------------------------------------------------------------------------------------------------
//
// **Why these are not `hand:` rows.** Slot 174's and slot 175's Troika-line bodies are
// `CBaseEntity::StartTouch` (`0x100a49d0`) and `CBaseEntity::Touch` (`0x100a4af0`), which sit in
// 29c's band and carry **no verdict** — they are two of the 145 non-core `CBaseEntity` slots story
// 29c-1's report set aside as "`CBaseEntity`'s story, not this one". `gen_kernel_shape` therefore
// still emits their named stubs, and a definition of `FElysiumNpc::Touch` here would be a duplicate
// symbol. The two base bodies are ported under their own names instead — exactly as story 29c-1's
// cleanup landed `CBaseEntity`'s own slot-153 body as `FElysiumNpc::BaseEntityIsMoving` — and the
// species dispatch sits on top of them.
//
// **GAP, named rather than patched:** nothing dispatches slot 175 in this runtime. There is no
// per-frame entity-vs-entity touch pass here; the world's touch surface is begin/end only. Slot 174
// IS wired, through `FElysiumEntity::OnTouchStart`, which is this runtime's touch-begin
// notification and is retail's `StartTouch` by construction.

/** How many times a base body forwarded the touch to `m_pParent`. This runtime's `MoveParent` IS
 *  retail's `m_pParent` (`CBaseEntity`, the move-parent handle), and the forward is a real dispatch
 *  when the parent is an NPC; when it is any other leaf the dispatch is counted and nothing is
 *  called, because no other leaf in this runtime carries slots 174/175. */
int32 ParentTouchPropagations = 0;

/** How many times `CBaseEntity::Touch` called `m_pfnTouch`. **SEAM:** this runtime has no
 *  per-entity touch think-function pointer (`+0x1ac` in retail's `CBaseEntity`), so the call is
 *  counted and nothing runs. Named because it is the FIRST thing `Touch` does, ahead of the parent
 *  forward, and dropping it silently would lose that order. */
int32 TouchFunctionCalls = 0;

/** `CBaseEntity::StartTouch` (`0x100a49d0`), slot 174's Troika-line body. The whole body is the
 *  parent forward: when `m_pParent` resolves live, dispatch ITS slot 174 (`vtable +0x2b8`) with the
 *  same toucher. Nothing else — no output, no condition, no state. */
void BaseEntityStartTouch(FElysiumEntity* Other);

/** `CBaseEntity::Touch` (`0x100a4af0`), slot 175's Troika-line body. Two steps in order:
 *  `if (m_pfnTouch) m_pfnTouch(other)`, then the same parent forward through the parent's slot 175
 *  (`vtable + 700`). */
void BaseEntityTouch(FElysiumEntity* Other);

/** Slot 174's dispatch: the species override if the census carries one for this NPC's retail class,
 *  otherwise the `CBaseEntity` body. */
void StartTouchSpecies(FElysiumEntity* Other);

/** Slot 175's dispatch, the same shape. */
void TouchSpecies(FElysiumEntity* Other);

/** `FElysiumEntity::OnTouchStart` — this runtime's touch-begin notification, which IS retail's
 *  slot 174. Wired so `CNPC_VGhoulCroucher`'s burn arm is reachable by a real touch rather than only
 *  by a test. */
virtual void OnTouchStart(const FElysiumEntityHandle& Activator) override;

/** `CNPC_VGhoulCroucher::StartTouch` (`0x1037bf60`), `CNPC_VGhoulCroucher#174`. Retail, in order:
 *
 *    1. `CBaseEntity::StartTouch(other)` — the base body FIRST.
 *    2. `if (other && other->m_pPlayer (+0xa8)) OnDisturbed(other);`
 *       `else if (other->field_0x94) OnDisturbed(other);`
 *       i.e. **the toucher is the player, or the toucher is an NPC**. `+0x94` is `CBaseEntity`'s
 *       cached `CAI_BaseNPC*` (families Conditions and Damage both already read it that way).
 *       **Retail's bug, and this port's one crash guard:** the `else` branch is entered with
 *       `other == NULL` too (`1037bfd9 XOR EBX,EBX / JMP 0x1037bfe7`, and `1037bfe7 MOV EAX,[EDI +
 *       0x94]` dereferences the null `EDI`). A null toucher faults in retail. This port refuses the
 *       arm instead — no `FElysiumEntityWorld` path can produce a null toucher, so the divergence is
 *       unreachable, and it is named rather than reproduced.
 *    3. `if (m_bSpawnBurning (+0x6665) && the toucher IS the player && m_flNextTouchBurnTime
 *       (+0x666c) < curtime)`: restamp `m_flNextTouchBurnTime = curtime + _DAT_1044ffd0` (**5.0 s**)
 *       and `BurnPlayer(player, 5.0f)`.
 *       The compare is `FLD curtime / FCOMP [+0x666c] / AND EAX,0x4100 / JNZ skip`, so the arm runs
 *       only when curtime is STRICTLY greater. Note the player term reuses the `+0xa8` pointer
 *       resolved in step 2 — a toucher that entered step 2 through the `+0x94` (NPC) arm carries a
 *       null one and cannot burn.
 *
 *  The 5.0 here is against the **10.0** of this class's own `OnVictimHitByMe` (`0x1037be80`
 *  `PUSH 0x41200000`): standing in the ghoul's fire hurts half as much as being hit by it. */
void GhoulCroucherStartTouch(FElysiumEntity* Other);

/** `m_flNextTouchBurnTime` (`+0x666c`, `CNPC_VGhoulCroucher`) — the touch-burn re-arm stamp. */
double GhoulNextTouchBurnTime = 0.0;

/** `_DAT_1044ffd0` — **5.0**, a DOUBLE, read at file offset `0x44ffd0`. */
static constexpr double GhoulTouchBurnIntervalSeconds = 5.0;
/** `1037c028 PUSH 0x40a00000` — the touch burn's damage, **5.0**, against the **10.0**
 *  (`0x41200000`) this class's own `OnVictimHitByMe` (`0x1037be80`) passes to the same body. */
static constexpr float GhoulTouchBurnDamage = 5.0f;

// `CNPC_VGhoulCroucher::BurnPlayer` (`0x1037c090`) itself is family **SpeciesMisc10**'s
// `FElysiumNpc::BurnPlayer(FElysiumEntity*, float)`, landed for the slot-24 caller. It is CALLED
// here, not restated — this row's contribution is the amount and the re-arm stamp above.

/** `CNPC_VGargoyle::Touch` (`0x1037a270`), `CNPC_VGargoyle#175` — the gargoyle's pillar damage.
 *
 *  Retail, in order:
 *    1. `FClassnameIs(other, "pillar")`, else `FClassnameIs(other, "central_pillar")`. Both are the
 *       case-insensitive whole-name compare (`__strcmpi`), because neither literal carries the
 *       trailing `*` the compare would honour as a prefix. This is the SAME predicate this class's
 *       slot-24 body uses, and family Misc's `GargoyleHitsPillar` already carries it — it is called
 *       here, not restated.
 *    2. On a match: a `CVDmg_t` with `SetSrc(this)`, `Set(1, 0x80, 10)` — family **Lethal**,
 *       `m_bdmgTypes` **`DMG_CLUB`**, `m_iDiceAmt` **10** — and `m_iToHitSuccesses` (`+0x0c`) forced
 *       to **1**; wrapped by `0x101c26d0` into a `CTakeDamageInfo` with **inflictor = the pillar**,
 *       **attacker = the gargoyle**, damage 1.0 and ammo type -1; then slot **142** `OnTakeDamage`
 *       dispatched **ON THE PILLAR** (`1037a3c1 CALL dword ptr [EDX + 0x238]`, `ECX = ESI`).
 *    3. BOTH paths end with `CBaseEntity::Touch(other)`.
 *
 *  The inflictor being the pillar itself rather than the gargoyle is retail's, and is reproduced. */
void GargoyleTouch(FElysiumEntity* Other);

/** One slot-142 dispatch the gargoyle made at a pillar. **SEAM:** a `pillar` is not an NPC in this
 *  runtime, so there is no `OnTakeDamage` to reach on most of them; the packet is recorded whole and
 *  dispatched only when the pillar happens to carry the NPC leaf. */
struct FGargoylePillarHit
{
	FElysiumEntityHandle Pillar;          // the toucher, and retail's inflictor
	int32 Family = 0;                     // CVDmg_t word 0
	int32 DiceAmount = 0;                 // m_iDiceAmt  +0x04
	int32 ToHitSuccesses = 0;             // m_iToHitSuccesses +0x0c
	uint32 DamageTypes = 0;               // m_bdmgTypes +0x10
	float Damage = 0.f;                   // the packet's scalar
	bool bDispatched = false;             // the pillar carried a slot 142 to reach
};
TArray<FGargoylePillarHit> GargoylePillarHits;

/** `1037a38c PUSH 0x1` / `1037a385 PUSH 0x80` / `1037a383 PUSH 0xa` and `1037a3ab MOV [..],0x1`. */
static constexpr int32 GargoylePillarDamageFamily = 1;        // EElysiumDmgFamily::Lethal
static constexpr uint32 GargoylePillarDamageTypes = 0x80u;    // DMG_CLUB
static constexpr int32 GargoylePillarDiceAmount = 10;
static constexpr int32 GargoylePillarToHitSuccesses = 1;
/** `1037a3a0 PUSH 0x3f800000` — the packet's damage scalar, **1.0**. */
static constexpr float GargoylePillarDamageScale = 1.0f;

// -------------------------------------------------------------------------------------------------
// Slot 463 `OnStateChange` — the `CNPC_VGuard1` and `CNPC_VHunter` pre-steps.
// -------------------------------------------------------------------------------------------------
//
// Both classes already have a row in family Conditions' slot-463 table under
// `EStateChangeSpecies::HolsterOnState`, because the SECOND half of both bodies is the shared
// holster/draw switch `ApplyStateWeaponVisibility` carries. What neither had is its FIRST half, and
// both first halves run BEFORE the switch. The arm below is called from
// `FElysiumNpc::OnStateChange`'s `HolsterOnState` case, ahead of `ApplyStateWeaponVisibility`.

/** `CNPC_VHunter::m_hPursuitPlayer` (`+0x6664`, datamap — `ElysiumNpcKernelShape.cpp`). Distinct
 *  from `CNPC_VCop`'s word at the same offset, which family Debug10 reaches through
 *  `CopPursuitPlayer()`; the same offset means a different thing per species, which is the rule 29b
 *  recorded for everything above `+0x665c`. */
FElysiumEntityHandle HunterPursuitPlayer;

/** How many times the acquire and release arms ran. The COUNT itself is the player's — retail's
 *  `+0x1d14` is a word on `CBasePlayer` and this runtime carries it as
 *  `FElysiumPoliceState::HuntersInPursuit`. `FUN_1017f7b0` (`0x1017f7b0`) and `FUN_1017f830`
 *  (`0x1017f830`) are already ported by family **Conditions** as the static
 *  `FElysiumNpc::OnHunterPursuitStart(FElysiumPlayer&)` / `OnHunterPursuitStop(FElysiumPlayer&)`
 *  pair, with that family's own receiver correction; this family CALLS them. These two are the
 *  Hunter arm's own tally, so a case can say which arm fired without reading the shared counter. */
int32 HunterPursuitStarts = 0;
int32 HunterPursuitStops = 0;

/** `FUN_10388c40` (`0x10388c40`), the Hunter's relationship write. Its whole body is
 *  `InputSetRelationship(this, "player D_HT 10", 0)` — the literal at `0x1063bc28`, `'player D_HT
 *  10'` with SPACES, i.e. the three-token grammar this runtime's `InputSetRelationship` already
 *  parses.
 *
 *  It is `CNPC_VGuard1`'s `0x1037e2d0` MINUS the `+0x6660` latch byte, and that byte is the only
 *  difference between the two 20-byte bodies. `0x1037e2d0` is family **SpeciesMisc10**'s
 *  `Guard1HatePlayer()` (it has six other callers in `vfunc461`), so the Guard1 arm calls that and
 *  this stands only for the Hunter's. */
void HunterHatePlayer();

/** How many times either relationship write above was made FROM this family's slot-463 pre-step. */
int32 PlayerHateRelationshipSets = 0;

/** `'player D_HT 10'` (`0x1063bc28`). */
static const TCHAR* PlayerHateRelationshipSpec();

/** The two classes' slot-463 pre-steps, dispatched on the retail class.
 *
 *  `CNPC_VGuard1::OnStateChange` (`0x1037d020`), UNCONDITIONALLY and with no reference to either
 *  state: `if (GetEnemy() && GetEnemy()->m_pPlayer) 0x1037e2d0(this)`. `GetEnemy()` is dispatched
 *  TWICE (`1037d026`, `1037d034`) and retail does not cache it; both calls are made here because a
 *  species override of slot 167 could answer differently between them.
 *
 *  `CNPC_VHunter::OnStateChange` (`0x10388880`), two independent arms in this order:
 *    1. `if (GetEnemy() && GetEnemy() && GetEnemy()->m_pPlayer && NewState == 2)` —
 *       `0x10388c40(this)` (the relationship, no latch), `OnHunterPursuitStart(player)`, then
 *       `m_hPursuitPlayer = player`'s own handle.
 *    2. `if (OldState == 2 && m_hPursuitPlayer resolves live && its m_pPlayer != 0)` —
 *       `m_hPursuitPlayer` = invalid, then `OnHunterPursuitStop(player)`, IN THAT ORDER (the clear
 *       is `1038891e`, the release after it).
 *  State 2 is `COMBAT`. Both arms can fire on the same call, and retail evaluates arm 2 against the
 *  handle arm 1 may just have written. */
void StateChangeSpeciesPreStep(EElysiumNpcState OldState, EElysiumNpcState NewState);

/** `CAI_BaseNPC::FUN_101a67e0` (`0x101a67e0`), vtable `+0x29c` = slot **167** — `GetEnemy()`. The
 *  whole body resolves `m_hEnemy` through the global entity table and answers null when the handle
 *  is stale. Declared here because this family is the first to need it by name and because the
 *  checklist's two walks call it a door reference; it is `Senses.Memory.Enemy` resolved. */
FElysiumEntity* GetEnemyEntity() const;

// -------------------------------------------------------------------------------------------------
// Slot 127 `Restore` — the two species arms family SaveRestore10 left routed to the base.
// -------------------------------------------------------------------------------------------------

/** `CNPC_VAndreiBlood::Restore` (`0x1035cf80`), `CNPC_VAndreiBlood#127`. A scope-trace push, a bare
 *  `CNPC_VVampireBoss::Restore(archive)` and a pop: **no restore-time datum of its own**. Every
 *  sibling boss writes something here and Andrei writes nothing, which is a fact rather than an
 *  unwalked body — the listing has exactly one `CALL` between the frame pushes (`1035cfd4`) and the
 *  base's `EAX` survives to the `RET 0x4` unchanged, so the answer is the base's too. */
int32 AndreiBloodRestore(void* Archive);

/** `CNPC_VChangBros::Restore` (`0x1036b170`), shared by `CNPC_VChangBros`, `CNPC_VChangBrosBlade`
 *  and `CNPC_VChangBrosClaw`. `CNPC_VVampireBoss::Restore` first and ITS answer is kept
 *  (`1036b1e3 MOV EDI,EAX` … `1036b210 MOV EAX,EDI`), then four writes in the listing's order:
 *    `m_fJumpGravity` (`+0x64b8`) = `_DAT_104ada44` (**2.3f**);
 *    `SetBodyEmitterName(0, "chang_powerup_emitter")`;
 *    `SetBodyEmitterName(1, "chang_powerup_emitter")`;
 *    `SetBodyEmitterName(2, "chang_spine_emitter")`.
 *  The gravity store is issued between the `FLD` and the first `SetBodyEmitterName` call
 *  (`1036b1ce FLD` / `1036b1db FSTP float ptr [ESI + 0x64b8]` / `1036b1e5 CALL`), so it lands
 *  first. */
int32 ChangBrosRestore(void* Archive);

/** `_DAT_104ada44` — **2.3f**, read at file offset `0x4ada44`. */
static constexpr float ChangBrosJumpGravity = 2.3f;
/** `0x10630e54` and `0x10630e3c`, the two emitter-name literals. */
static const TCHAR* ChangPowerupEmitterName();
static const TCHAR* ChangSpineEmitterName();

// -------------------------------------------------------------------------------------------------
// The two destructors.
// -------------------------------------------------------------------------------------------------

/** How many `COutputEvent` lists a destructor tore down. **SEAM:** `thunk_FUN_100cd2d0` frees an
 *  output list's `CUtlVector` of `CEventAction`s. This runtime's outputs are owned by the entity
 *  def and freed with it, so the teardown is counted rather than performed — but the COUNT is the
 *  recovered fact (one for Andrei, five for the Werewolf) and is what a case asserts. */
int32 OutputListDestroys = 0;

/** `CNPC_VAndreiBlood::Destructor` (`0x1035cd00`). Retail, in order:
 *    1. Restore its own vftable and the secondary one at `+0x19b0` (a C++ artifact; nothing a
 *       program can observe, and nothing this port has).
 *    2. Under a `"CNPC_VAndreiBlood::Destructor"` scope frame: `UTIL_Remove` (`0x101cd940`) the
 *       blood emitter at **`+0x66e0`** and then the summon emitter at **`+0x66e4`**, each only when
 *       the handle resolves live, and write `0xffffffff` back to each after its removal.
 *       **Retail writes the invalid handle only on the arm it removed on**, not unconditionally.
 *    3. Destroy `m_OnTransformComplete` (`+0x6664`).
 *    4. Tail-jump to `~CAI_BaseNPCTroika` (`0x1028d610`).
 *  Step 2 is the whole observable body: Andrei's blood pieces do not outlive him. */
void DestroyAndreiBlood();

/** `DAT_1093fac4` and the `werewolf_show_debug` ConVar, which `~CNPC_VWerewolf` forces back to 0.
 *  **SEAM:** this runtime stands no such console variable. The pair is carried as one process-wide
 *  int (retail's global IS process-wide, and the ConVar is the same state under a name) so the reset
 *  is a real write with a real reader. */
static int32& WerewolfShowDebug();

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
