// Story 29d, family **SpeciesLifecycle10** — the twelve per-species spawn, touch, restore, destroy
// and think bodies of layers 11-17.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. **No body in this family is a Troika-line slot body**, so nothing
// here is declared by the generator and nothing here is `hand:` — every row is either a species
// override of a slot another family owns (an arm added to that family's method) or a maker class's
// own `Spawn` (`FElysiumNpcMaker`, `FElysiumNpcMakerFleshpile`, `FElysiumNpcMakerZombie`, story 5
// fold A4).
//
// The definitions are in `Substrate/ElysiumNpcSpeciesLifecycle10.cpp`, the makers' three in
// `Substrate/ElysiumNpcMaker*.cpp`, and the tests in
// `Tests/ElysiumNpcKernelSpeciesLifecycle10Tests.cpp`. The walked prose is
// `docs/vtmb/npc-ai/lifecycle.md` § "Story 29d, family SpeciesLifecycle10".
//
// --- What this family is --------------------------------------------------------------------------
//
// Twelve rows, four shapes:
//
//   * **Two destructors** (`CNPC_VAndreiBlood` `0x1035cd00`, `CNPC_VWerewolf`). Almost
//     all of both is allocator teardown a garbage-collected runtime cannot observe; the observable
//     halves are Andrei's two owned emitter entities, which do not outlive him, and the Werewolf's
//     reset of the `werewolf_show_debug` ConVar and its global.
//   * **Two species `Restore`s** (`CNPC_VAndreiBlood`, the three Chang brothers), which are the last two arms family SaveRestore10 left routed to the base
//     `CNPC_VVampireBoss::Restore` with a comment saying "the day the owning story lands their own
//     halves, each row's `Body` moves to it". This is that story; the rows now point here.
//   * **Three maker `Spawn`s** (`CNPCMaker` `0x1034afe0`, `_Fleshpile` `0x1034c020`, `_Zombie`
//     `0x1034cc60`). One override per maker class since story 5 fold A4.
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
// Slots 174 `StartTouch` and 175 `Touch` — the `CBaseEntity` bodies and their two species arms.
// -------------------------------------------------------------------------------------------------
//
// **Why these are not `hand:` rows.** Slot 174's and slot 175's Troika-line bodies are
// `CBaseEntity::StartTouch` (`0x100a49d0`) and `CBaseEntity::Touch` (`0x100a4af0`), which sit in
// 29c's band and carry **no verdict** — they are two of the 145 non-core `CBaseEntity` slots story
// 29c-1's report set aside as "`CBaseEntity`'s story, not this one". `gen_kernel_shape` therefore
// still emits their named stubs, and a definition of `FElysiumNpc::Touch` here would be a duplicate
// symbol. The two base bodies are ported under their own names instead — exactly as story 29c-1's
// cleanup landed `CBaseEntity`'s own slot-153 body as `FElysiumNpcBase::BaseEntityIsMoving` — and the
// species dispatch sits on top of them.
//
// **GAP, named rather than patched:** nothing dispatches slot 175 in this runtime. There is no
// per-frame entity-vs-entity touch pass here; the world's touch surface is begin/end only. Slot 174
// IS wired, through `FElysiumEntity::OnTouchStart`, which is this runtime's touch-begin
// notification and is retail's `StartTouch` by construction.

/** Slot 174 on the NPC line: the `CBaseEntity` body. `CNPC_VGhoulCroucher` overrides it
 *  (story 5 step 3). */
virtual void StartTouchSpecies(FElysiumEntity* Other);

/** Slot 175, the same shape. `CNPC_VGargoyle` overrides it. */
virtual void TouchSpecies(FElysiumEntity* Other);

/** `FElysiumEntity::OnTouchStart` — this runtime's touch-begin notification, which IS retail's
 *  slot 174. Wired so `CNPC_VGhoulCroucher`'s burn arm is reachable by a real touch rather than only
 *  by a test. */
virtual void OnTouchStart(const FElysiumEntityHandle& Activator) override;

// `CNPC_VGhoulCroucher::BurnPlayer` (`0x1037c090`) itself is family **SpeciesMisc10**'s
// `FElysiumNpcGhoulCroucher::BurnPlayer(FElysiumEntity*, float)`, landed for the slot-24 caller. It is CALLED
// here, not restated — this row's contribution is the amount and the re-arm stamp above.

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

// -------------------------------------------------------------------------------------------------
// Slot 463 `OnStateChange` — the `CNPC_VGuard1` and `CNPC_VHunter` pre-steps.
// -------------------------------------------------------------------------------------------------
//
// The FIRST halves of both bodies, which run BEFORE the holster/draw switch: Guard1's own copy of
// the switch (`ApplyStateWeaponVisibility`) and then the Troika body, Hunter's a direct call into
// `CNPC_VHumanCombatant::OnStateChange` (`0x103871c0`). Called from the classes' `OnStateChange`
// overrides (story 5 step 3).

// -------------------------------------------------------------------------------------------------
// The two destructors.
// -------------------------------------------------------------------------------------------------

/** How many `COutputEvent` lists a destructor tore down. **SEAM:** `thunk_FUN_100cd2d0` frees an
 *  output list's `CUtlVector` of `CEventAction`s. This runtime's outputs are owned by the entity
 *  def and freed with it, so the teardown is counted rather than performed — but the COUNT is the
 *  recovered fact (one for Andrei, five for the Werewolf) and is what a case asserts. */
int32 OutputListDestroys = 0;

/** `DAT_1093fac4`, the process-wide debug word `~CNPC_VWerewolf` forces back to 0 before it sets
 *  the `werewolf_show_debug` ConVar (`ElysiumNpcTunables::EConVar::WerewolfShowDebug`) to 0. */
static int32& WerewolfShowDebug();

