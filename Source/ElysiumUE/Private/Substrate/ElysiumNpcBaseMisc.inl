// `CAI_BaseNPC`'s declarations of the `Misc` family (story 5 step 5),
// moved from `ElysiumNpcMisc*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseMisc.cpp`.

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

/** `0x1028a190` — the shared Troika gate slot 590 opens with, and the same gate the knockback start
 *  (`0x102a01b0`, `lifecycle.md`) tests. The decompilation is DAMAGED (a jump table it could not
 *  recover); this is walked off the listing:
 *
 *      if (GetState() == 4 (NPC_STATE_SCRIPT) && m_hCine (+0x5d74) resolves)
 *          if (!cine->CanInterrupt()) return false;            // 0x101a8930, on the CINE
 *      if (m_bInChoreoScene (+0x5bc4)) return false;
 *      if (m_bfAINPCFlags2 (+0x14bc) & 0x1000) return false;   // TEST AH,0x10 at 0x1028a213
 *      return IsAlive();                                       // vtable +0x278, slot 158
 *
 *  Retail name unrecovered; named for what the body answers. */
bool OkToDisturb() const;


/** The retail body of the factory asked last — the row of `ComponentFactoryRows` it names. */
const TCHAR* LastComponentFactoryBody = nullptr;

/** How many times each factory was asked and refused — the read side of the seam. */
int32 ComponentFactoryRefusals = 0;

/** The slot of the factory that refused first inside `CreateComponents`, or `INDEX_NONE`. */
int32 FirstRefusedComponentSlot = INDEX_NONE;
