// Story 29c-1, family **Misc** — the declarations of this family's layer 0–9 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual, and this family only defines it. What lands here is
// the non-slot half — the retail helpers, free functions and non-virtual methods of layers 0–9
// whose overlay row names a port method on `FElysiumNpc` that did not exist before this story.
//
// The definitions are in `Substrate/ElysiumNpcMisc.cpp` and the tests in
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

// --- The `CAI_StandoffBehavior` words slots 4 and 26 touch ----------------------------------------
//
// **There is no `CAI_StandoffBehavior` in this runtime and no behaviour object under the NPC at
// all.** Family **Lifecycle** landed slot 13 (`0x102c7600`) as a PURE function over a typed view of
// the behaviour's own datamap (`FStandoffWords`), and family **TroikaHelpers** put the two words
// that view does not carry — `bStandoffRangedCache` (+0x19) and `StandoffDistTooFar` (+0x50) — on
// the leaf. This family's three standoff rows follow both decisions: the words `FStandoffWords`
// already names are read and written through it, `+0x50` is the leaf's `StandoffDistTooFar`, and the
// five remaining ones are below. Nothing here stands a behaviour object to make a body "work".

// --- Slot 24 `OnVictimHitByMe` — the Troika line and four species bodies ------------------------

/** **SEAM** for `thunk_FUN_1028b160(&this->field_0x6028)` (`0x1028b160`), the whole Troika-line body
 *  of slot 24: five 12-byte `MeleeMoveRecord_t` entries at `+0x6028` zeroed in one loop. `+0x6028`
 *  is `ELYSIUM_NPC_WORD_ABSENT` in the shape map — this runtime's melee selector keeps its own
 *  retained draw — so the clear is counted and nothing is written. */
void ClearMeleeMoveRecords();
int32 MeleeMoveRecordClears = 0;

// --- Slot 590 `OkToInterruptForMelee` and slot 592 `CanSeekCover` ---------------------------------

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

/** The table: the six Troika-line factories plus the one live species override this family carries
 *  (`CNPC_VRat`'s local navigator). */
static const FComponentFactory* ComponentFactoryRows(int32& OutCount);

// --- The remaining non-slot bodies ----------------------------------------------------------------

/** `0x10381c00` — the `CNPC_VHengeyokai` carry form bit. Sets or clears `m_bfAINPCFlags`
 *  `CARRYING_BODY` (`+0x14b8` bit `0x20`) and, on the TRUE arm only, stamps `m_flFishTimer`
 *  (`+0x666c`) with `curtime + RandomFloat(5.0, 8.0)` and clears `m_bDidFakeThrow` (`+0x667d`).
 *  Family **Bosses**' `CallFormBit` seam stood for this and now forwards to it. */
void FormBit(bool bSet);

/** `CBaseCombatCharacter::GetExpressionEventParams` (`0x10014ba0`) — the base slot 347 body the
 *  Troika override forwards every event but `1` to. Fully recovered, so it is ported rather than
 *  seamed. */
bool CombatCharacterExpressionEventParams(int32 Event, TCHAR* OutName, float* OutA, float* OutB,
	float* OutC, float* OutD) const;

/** The `Q_strncpy(buffer, "Knockback", 0x40)` both slot-347 bodies open their event-1 arm with —
 *  retail's fixed 64-character buffer, so the port's copy is bounded the same way. */
static constexpr int32 ExpressionEventNameChars = 0x40;

/** **SEAM** for `victim->vtable[+0x428]` (slot 266), the reaction `CNPC_VGargoyle`'s slot 24
 *  dispatches ON THE VICTIM (`MOV ECX,ESI` at `0x1037a54a`, `this` as the one argument). The victim
 *  is a `pillar` / `central_pillar` prop, not an NPC, and slot 266 is declared on `FElysiumNpc`
 *  alone here, so a non-NPC victim is counted and nothing is dispatched. An NPC victim takes the
 *  real `Slot266()`. **Unrecovered:** what a pillar's class fills slot 266 with. */
void DispatchVictimHitReaction(FElysiumEntity* Victim);
int32 VictimHitReactionDispatches = 0;
