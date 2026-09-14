// Story 29c-1, family **Misc** — the declarations of this family's layer 0–9 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual, and this family only defines it. What lands here is
// the non-slot half — the retail helpers, free functions and non-virtual methods of layers 0–9
// whose overlay row names a port method on `FElysiumNpc` that did not exist before this story.
//
// The definitions are in `Substrate/ElysiumNpcKernelMisc.cpp` and the tests in
// `Tests/ElysiumNpcKernelMiscTests.cpp`. One file per family rather than 915 declarations
// appended to an already-oversized header: the family boundary is what this story ports by.

// --- Species words this family's bodies touch -----------------------------------------------------
//
// 29b's shape map stops at `+0x665c`: above it the same offset means a different member on every
// species, so there is no shared binding. These seven are the ones this family's bodies read or
// write, declared by retail name with the species class the census (`ElysiumNpcKernelShape.cpp`)
// records the offset for. `ChangArenaCenter` (+0x66dc) and `bChangCenterStored` (+0x66e8) are
// family **Positions**' and are used, not re-declared.

float HengeyokaiFishTimer = 0.f;         // +0x666c CNPC_VHengeyokai::m_flFishTimer
bool bHengeyokaiDidFakeThrow = false;    // +0x667d CNPC_VHengeyokai::m_bDidFakeThrow
float LasombraCoverDisableOverride = 0.f;  // +0x6664 CNPC_VLasombra::m_flCoverDisableOverride
int32 SabbatLeaderRoarAttackCount = 0;   // +0x66e0 CNPC_VSabbatLeader::m_RoarAttackCount
float ManBatSlowedExpire = 0.f;          // +0x6684 CNPC_VManBat::m_flSlowedExpire
FElysiumEntityHandle WerewolfRotDoor1;   // +0x6684 CNPC_VWerewolf::m_hRotDoor1
FElysiumEntityHandle WerewolfRotDoor2;   // +0x6688 CNPC_VWerewolf::m_hRotDoor2

// --- The `CAI_StandoffBehavior` words slots 4 and 26 touch ----------------------------------------
//
// **There is no `CAI_StandoffBehavior` in this runtime and no behaviour object under the NPC at
// all.** Family **Lifecycle** landed slot 13 (`0x102c7600`) as a PURE function over a typed view of
// the behaviour's own datamap (`FStandoffWords`), and family **TroikaHelpers** put the two words
// that view does not carry — `bStandoffRangedCache` (+0x19) and `StandoffDistTooFar` (+0x50) — on
// the leaf. This family's three standoff rows follow both decisions: the words `FStandoffWords`
// already names are read and written through it, `+0x50` is the leaf's `StandoffDistTooFar`, and the
// five remaining ones are below. Nothing here stands a behaviour object to make a body "work".

/** The `CAI_StandoffBehavior` words slot 4 (`0x102c7490`) and slot 26 (`0x102c7dd0`) touch that
 *  `FStandoffWords` does not carry. `this+4` is the NPC the behaviour is attached to and is `*this`
 *  here, as it is for every other standoff row in this runtime. */
struct FStandoffAimWords
{
	// +0x001a — the byte slot 4 tests before forcing the owner's `+0x1fc` to 1. Its writer is
	// **unrecovered**: no body in the `0x102c7…` range this story read writes it.
	bool bForcesOwnerWord0x1fc = false;
	// +0x0020 — the aim mode slot 26 switches on: `> 0` resets the three aim parameters below and
	// `== 2` also latches `FStandoffWords::bSawNewEnemy`. **Unrecovered:** what sets it.
	int32 AimMode = 0;
	// +0x0058, +0x005c, +0x0060 — the three aim parameters slot 26 resets, in retail's own write
	// order (`+0x5c` first, then `+0x60`, then `+0x58`). Retail names are unrecovered; the offsets
	// are the identity.
	float AimWord0x58 = 0.f;
	float AimWord0x5c = 0.f;
	float AimWord0x60 = 0.f;
};

/** `CAI_StandoffBehavior::vfunc4` (`0x102c7490`) — the standoff's reaction-timer reset. Writes
 *  `FStandoffWords` (`bSawNewEnemy`, `ReactionsLeft`, `NextReactionAt`), parks the owner's
 *  `m_flDistTooFar` (`+0x5de4`) in the leaf's `StandoffDistTooFar` and stands `FLT_MAX` in its
 *  place, and forces the owner's `Field_0x01fc` to 1 when `Aim.bForcesOwnerWord0x1fc`. */
void StandoffVfunc4(FStandoffWords& Words, const FStandoffAimWords& Aim);

/** `CAI_StandoffBehavior::vfunc26` (`0x102c7dd0`) — the aim-parameter reset. Pure over the two
 *  views, so no behaviour store is needed to exercise either arm. */
static void StandoffVfunc26(FStandoffWords& Words, FStandoffAimWords& Aim);

/** `CAI_StandoffBehavior::vfunc28` (`0x102c7ef0`) — the whole body is `return DAT_10601874`. */
static bool StandoffVfunc28();

/** **SEAM** for `_DAT_10601874`, the one-shot latch `CAI_StandoffBehavior`'s class initialiser
 *  (`0x102c7eb0` → `0x102c7f10`) raises once its schedule, condition and activity id spaces have
 *  been registered into `DAT_10936b68`. This runtime has no behaviour id-space loader, so the
 *  namespace is never loaded and the latch keeps its zero — which is what `StandoffVfunc28`
 *  answers. Not a guess: `0x10601874` has exactly one reader (slot 28) and its only writers are
 *  that initialiser. */
static bool StandoffSchedulesLoaded();

// --- Slot 24 `OnVictimHitByMe` — one method, four retail lines ------------------------------------
//
// The Troika line (`0x1029f8d0`) plus three species bodies, resolved off the CENSUS rather than off
// a hand-typed class list so the arm is checkable against `docs/vtmb/npc-kernel/slots.md` by
// construction. A classname no census class claims — `npc_VCop` is the recovered example — answers
// `Troika`, which is the correct fall-through and not a bug.
// Story 29d, family **SpeciesMisc10** added `GhoulCroucher` (`0x1037be80`): the FOURTH species
// override of slot 24, and the only one besides the Zombie that keeps the Troika body.
enum class EVictimHitLine : uint8 { Troika, Gargoyle, SabbatLeader, Zombie, GhoulCroucher };

/** One row of the slot-24 species table: the retail class and the body that fills the slot for it. */
struct FVictimHitSpecies
{
	const TCHAR* RetailClass = nullptr;
	const TCHAR* Body = nullptr;
	EVictimHitLine Line = EVictimHitLine::Troika;
};

/** The table: the Troika line plus its three species overrides. */
static const FVictimHitSpecies* VictimHitSpeciesRows(int32& OutCount);

/** The line this NPC's class takes at slot 24, read through `ElysiumNpcKernelClass::BodyOf`. */
EVictimHitLine VictimHitLine() const;

/** `CNPC_VGargoyle::OnVictimHitByMe` (`0x1037a450`)'s classname filter, as a pure function so the
 *  two names it matches are assertable without an entity. `FClassnameIs` semantics: case-insensitive
 *  and a trailing `*` is a prefix match — neither of these two carries one. */
static bool GargoyleHitsPillar(const FString& Classname);

/** **SEAM** for `thunk_FUN_1028b160(&this->field_0x6028)` (`0x1028b160`), the whole Troika-line body
 *  of slot 24: five 12-byte `MeleeMoveRecord_t` entries at `+0x6028` zeroed in one loop. `+0x6028`
 *  is `ELYSIUM_NPC_WORD_ABSENT` in the shape map — this runtime's melee selector keeps its own
 *  retained draw — so the clear is counted and nothing is written. */
void ClearMeleeMoveRecords();
int32 MeleeMoveRecordClears = 0;

/** **SEAM** for `victim->vtable[+0x428]` (slot 266), the reaction `CNPC_VGargoyle`'s slot 24
 *  dispatches ON THE VICTIM (`MOV ECX,ESI` at `0x1037a54a`, `this` as the one argument). The victim
 *  is a `pillar` / `central_pillar` prop, not an NPC, and slot 266 is declared on `FElysiumNpc`
 *  alone here, so a non-NPC victim is counted and nothing is dispatched. An NPC victim takes the
 *  real `Slot266()`. **Unrecovered:** what a pillar's class fills slot 266 with. */
void DispatchVictimHitReaction(FElysiumEntity* Victim);
int32 VictimHitReactionDispatches = 0;

// --- Slot 590 `OkToInterruptForMelee` and slot 592 `CanSeekCover` ---------------------------------

/** `0x1028a190` — the shared Troika gate slot 590 opens with, and the same gate the knockback start
 *  (`0x102a01b0`, `lifecycle.md`) tests. The decompilation is DAMAGED (a jump table it could not
 *  recover); this is walked off the listing:
 *
 *      if (GetState() == 4 (NPC_STATE_SCRIPT) && m_hCine (+0x5d74) resolves)
 *          if (!CineCanInterrupt()) return false;              // 0x101a8930, on the CINE
 *      if (m_bInChoreoScene (+0x5bc4)) return false;
 *      if (m_bfAINPCFlags2 (+0x14bc) & 0x1000) return false;   // TEST AH,0x10 at 0x1028a213
 *      return IsAlive();                                       // vtable +0x278, slot 158
 *
 *  Retail name unrecovered; named for what the body answers. */
bool OkToDisturb() const;

// --- Slot 473 `GetSoundInterests` — the `CGeneric_NPC` pair ---------------------------------------
//
// Two byte-identical 20-byte bodies at two classes. Slot 473's Troika line is another family's row
// (a `value`: `return 0x81f`), so this species arm lands under its own name rather than routing
// through the slot.

/** One row of the slot-473 species table. */
struct FSoundInterestSpecies
{
	const TCHAR* RetailClass = nullptr;
	const TCHAR* Body = nullptr;
};

/** The table: `CGeneric_NPC` (`0x1035a7e0`) and `CGenericSabbat_NPC` (`0x1035be50`). */
static const FSoundInterestSpecies* SoundInterestSpeciesRows(int32& OutCount);

/** `CGeneric_NPC::vfunc473` (`0x1035a7e0`) and `CGenericSabbat_NPC::vfunc473` (`0x1035be50`) —
 *  `m_NPCState` (`+0x5cc0`) `== 1` (`NPC_STATE_IDLE`) answers `0x19`, everything else `0x17`. */
int32 GenericNpcSoundInterests() const;

// --- The three per-species threshold answers ------------------------------------------------------

/** `CNPC_Bullseye::vfunc576` (`0x10356f30`) — slot 576 `IsLightDamage`, and BYTE-IDENTICAL to the
 *  Troika line's own body (`0x10266630`, family **Damage**'s `IsLightDamage`): `damage > 0.0f`
 *  (`_DAT_104454c4`). Landed under its own name because it is its own census row; it asks the slot
 *  rather than restating the compare, so the two cannot drift. */
bool BullseyeIsLightDamage(float Damage, int32 DamageBits);

/** `CNPC_VBach::vfunc553` (`0x10364500`) and `::vfunc554` (`0x10364550`) — slots 553/554
 *  `RangeAttack1Conditions` / `RangeAttack2Conditions`. One shape, one differing answer. */
int32 BachRangeAttack1Conditions(float Dot, float DistUnits) const;
int32 BachRangeAttack2Conditions(float Dot, float DistUnits) const;

// --- The component factories, slots 424–430 -------------------------------------------------------
//
// **There is no `CAI_Senses`, `CAI_Motor`, `CAI_MoveProbe`, `CAI_LocalNavigator`, `CAI_Navigator` or
// `CAI_Pathfinder` object in this runtime.** The shape map says so twice: `+0x5cdc` is bound to
// `FElysiumNpc::Senses` (a struct on the NPC, not a component) and `+0x5d34`..`+0x5d44` are
// `ELYSIUM_NPC_WORD_CHAIN` rows pointing at `FElysiumScriptedCharacter::Motor`, the one
// `IElysiumNpcMotor` seam. So the six factories answer null and record the allocation retail would
// have made, and `CreateComponents` (slot 424) runs retail's fail-fast chain over them and answers
// false at the first refusal — which IS retail's answer when a factory returns null.

/** One factory's recovered allocation: the slot, the class whose body fills it, the body's address,
 *  the byte size `operator new` is called with and the constructor the block is handed to. Every row
 *  is checkable against `docs/vtmb/npc-kernel/slots.md`. */
struct FComponentFactory
{
	int32 Slot = 0;
	const TCHAR* RetailClass = nullptr;
	const TCHAR* Body = nullptr;
	int32 SizeBytes = 0;
	const TCHAR* Constructor = nullptr;
	// The vftable the body assigns after construction, empty when the constructor's own stands.
	const TCHAR* VTable = nullptr;
};

/** The table: the six Troika-line factories plus the three species overrides this family carries
 *  (`CAI_BaseHumanoid`'s motor and navigator, `CNPC_VRat`'s local navigator). */
static const FComponentFactory* ComponentFactoryRows(int32& OutCount);

/** The row this NPC's class takes at `Slot`, resolved through `ElysiumNpcKernelClass::BodyOf` so a
 *  species override is picked off the census. Null for a slot the table does not carry. */
const FComponentFactory* ComponentFactoryFor(int32 Slot) const;

/** How many times each factory was asked and refused — the read side of the seam. */
int32 ComponentFactoryRefusals = 0;
/** The slot of the factory that refused first inside `CreateComponents`, or `INDEX_NONE`. */
int32 FirstRefusedComponentSlot = INDEX_NONE;

// --- The remaining non-slot bodies ----------------------------------------------------------------

/** `0x10381c00` — the `CNPC_VHengeyokai` carry form bit. Sets or clears `m_bfAINPCFlags`
 *  `CARRYING_BODY` (`+0x14b8` bit `0x20`) and, on the TRUE arm only, stamps `m_flFishTimer`
 *  (`+0x666c`) with `curtime + RandomFloat(5.0, 8.0)` and clears `m_bDidFakeThrow` (`+0x667d`).
 *  Family **Bosses**' `CallFormBit` seam stood for this and now forwards to it. */
void FormBit(bool bSet);

/** `0x10381ca0` — the read half of the pair above: has `m_flFishTimer` (`+0x666c`) reached curtime?
 *  NAMED `FormBitTimerExpired`, not 29c's `FormBit`: that name is the setter's, and one method
 *  cannot be both a `void(bool)` and a `bool()`. */
bool FormBitTimerExpired() const;

/** `CNPC_VYukie::vfunc599` (`0x103dd8b0`) — the species body at slot 599, `EnterMelee`. Unlike the
 *  Troika line (family **TroikaHelpers**' `Slot599`) it has NO gates at all: the global melee event
 *  fires, `m_bInMelee` goes true and `m_flMeleeMustLeaveTimer` takes
 *  `curtime + RandomFloat(22.5, 45.0)`. 29c's target named the data member `MeleeMustLeaveTimer`;
 *  renamed here because that member is 29b's and this is the body that writes it. */
bool YukieEnterMelee();

/** `0x103dd9a0` — the species body at slot 601 for `CNPC_VYukie`, `LeaveMelee`. The Troika line's
 *  `0x102b5880` without the attack-coordinator release. Renamed from 29c's `MeleeCanEnterTimer` for
 *  the same reason as above. */
void YukieLeaveMelee();

/** `0x1038f290` — is `CNPC_VManBat`'s slow effect still running? `m_flSlowedExpire` (`+0x6684`)
 *  strictly above `_DAT_1044fab0`, the shared `0.0` DOUBLE (`docs/vtmb/footsteps.md:319`, the
 *  2-D speed gate). A flag test spelled as a float compare, not a deadline against curtime. */
bool SlowedExpire() const;

/** `CNPC_VChangBros::StoreArenaCenter` (`0x1036e400`) — walk the global `CAI_Hint` list for the
 *  first node of type `0x4651`, take its origin into `m_vArenaCenter` (`+0x66dc`, family
 *  **Positions**' `ChangArenaCenter`) and raise `m_bCenterStored` (`+0x66e8`). */
void StoreArenaCenter();

/** `0x103cade0` — `CNPC_VWerewolf`'s zone opener. Dispatches slot 251 on every entity whose
 *  TARGETNAME is `trigger_werewolf_zone`, then caches the `rotdoor1` / `rotdoor2` entities in
 *  `m_hRotDoor1` / `m_hRotDoor2` (`+0x6684` / `+0x6688`). Name inferred from the three hardcoded map
 *  entity names; the retail name is unrecovered. */
void TriggerWerewolfZone();

/** **SEAM** for `zone->vtable[+0x3ec]` (slot 251) on a `trigger_werewolf_zone` entity. On the
 *  `CAI_BaseNPC` line slot 251 is `IsActivityFinished`, but a trigger is a different hierarchy
 *  sharing the index and the census does not carry its table — so what this fires is
 *  **unrecovered**. Counted, and nothing is dispatched. */
void FireWerewolfZoneTrigger(FElysiumEntity& Zone);
int32 WerewolfZoneTriggerFires = 0;

/** `CBaseCombatCharacter::GetExpressionEventParams` (`0x10014ba0`) — the base slot 347 body the
 *  Troika override forwards every event but `1` to. Fully recovered, so it is ported rather than
 *  seamed. */
bool CombatCharacterExpressionEventParams(int32 Event, TCHAR* OutName, float* OutA, float* OutB,
	float* OutC, float* OutD) const;

/** The `Q_strncpy(buffer, "Knockback", 0x40)` both slot-347 bodies open their event-1 arm with —
 *  retail's fixed 64-character buffer, so the port's copy is bounded the same way. */
static constexpr int32 ExpressionEventNameChars = 0x40;
