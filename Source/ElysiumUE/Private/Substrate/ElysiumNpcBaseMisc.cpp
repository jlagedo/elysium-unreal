// `CAI_BaseNPC`'s bodies of the `Misc` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseMisc.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"

// --- File-scope helpers moved with the base bodies (story 5 step 5) ---

namespace
{
	// `.rdata` words this family's bodies compare against: shared pool constants with hundreds of
	// readers, bound to the tunables table.
	constexpr float GMiscZeroFloat = ElysiumNpcTunables::Zero;
	// `m_bfAINPCFlags2` (+0x14bc) bit `0x1000` — `TEST AH,0x10` at `0x1028a213`. See the note in
	// `OkToDisturb` below: this IS a reader of `MADE_OBLIVIOUS`, which `ElysiumNpcFlags.h` records
	// as having none.
	constexpr EElysiumNpcFlag2 GMiscObliviousBit = EElysiumNpcFlag2::MADE_OBLIVIOUS;
	// `CAI_StandoffBehavior::vfunc26`'s three literals, in retail's own write order.
	constexpr float GMiscStandoffAim5c = 5.f;       // 0x40a00000 -> this+0x5c
	constexpr float GMiscStandoffAim60 = 0.f;       // 0             -> this+0x60
	constexpr float GMiscStandoffAim58 = -1.f;      // 0xbf800000 -> this+0x58
	// `AimMode == 2` is the second arm's test; `> 0` is the first's.
	constexpr int32 GMiscStandoffAimModeLatch = 2;
	// `0x7f7fffff` — the `FLT_MAX` `vfunc4` stands in the owner's `m_flDistTooFar`.
	constexpr float GMiscFloatMax = 3.402823466e+38f;
	// `owner->+0x1fc = 1` (`0x102c74…`). Family **TroikaHelpers**' `StandoffVfunc5` writes 2 into the
	// same word; the identity of `+0x1fc` is unrecovered on both sides.
	constexpr int32 GMiscStandoffOwnerWord0x1fcValue = 1;
}

// --- Moved from `ElysiumNpcMisc.cpp` (story 5 step 5) ---

void FElysiumNpcBase::StandoffVfunc4(FStandoffWords& Words, const FStandoffAimWords& Aim)
{
	// `0x102c7490`, arm by arm and in retail's own write order:
	//     this[0x4c] = 1;                                          // bSawNewEnemy
	//     this->+0x48 = 0;                                         // ReactionsLeft
	//     if (this->+0x44 == _DAT_104454c4)                        // ReactionDelayMax == 0.0f
	//         t = gpGlobals->curtime + this->+0x40;                //   the min alone
	//     else
	//         t = RandomFloat(this->+0x40, this->+0x44) + curtime;
	//     this->+0x3c = t;                                         // NextReactionAt
	//     this->+0x50 = owner->m_flDistTooFar;                     // +0x5de4, PARKED
	//     owner->m_flDistTooFar = 0x7f7fffff;                      // FLT_MAX
	//     if (this[0x1a]) owner->+0x1fc = 1;
	//
	// The `+0x44 == 0.0f` branch is the SAME "no range, use the min" convention family **Lifecycle**
	// recorded on `FStandoffWords::ReactionDelayMax`, so the two bodies agree by construction.
	//
	// `+0x50` is family **TroikaHelpers**' `StandoffDistTooFar`, and this is the write half of the
	// pair: `StandoffVfunc5` (`0x102c7530`) copies it BACK into `m_flDistTooFar` on the way out. A
	// standoff therefore parks the NPC's too-far distance for the length of the standoff and stands
	// `FLT_MAX` in its place, which is what makes no enemy too far while the behaviour holds.
	Words.bSawNewEnemy = true;
	Words.ReactionsLeft = 0;

	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	double Delay = static_cast<double>(Words.ReactionDelayMin);
	if (Words.ReactionDelayMax != GMiscZeroFloat)
	{
		// `(*DAT_1070b244 + 4)(min, max)` is `RandomFloat`, drawn from the one stream every NPC body
		// in this runtime draws from.
		Delay = static_cast<double>(ElysiumRng::Stream(EElysiumRngStream::NpcSchedule)
			.FRandRange(Words.ReactionDelayMin, Words.ReactionDelayMax));
	}
	Words.NextReactionAt = Now + Delay;

	StandoffDistTooFar = DistTooFar;
	DistTooFar = GMiscFloatMax;

	if (Aim.bForcesOwnerWord0x1fc)
	{
		Field_0x01fc = GMiscStandoffOwnerWord0x1fcValue;
	}
}

void FElysiumNpcBase::StandoffVfunc26(FStandoffWords& Words, FStandoffAimWords& Aim)
{
	// `0x102c7dd0`, the whole body:
	//     if (0 < this->+0x20) { this->+0x5c = 5.0f; this->+0x60 = 0.0f; this->+0x58 = -1.0f; }
	//     if (this->+0x20 == 2) this[0x4c] = 1;
	//
	// TWO independent tests, not an if/else — mode 2 takes both. The literals are immediates in the
	// body (`0x40a00000`, `0`, `0xbf800000`), not `.rdata` reads, so their values are exact.
	if (0 < Aim.AimMode)
	{
		Aim.AimWord0x5c = GMiscStandoffAim5c;
		Aim.AimWord0x60 = GMiscStandoffAim60;
		Aim.AimWord0x58 = GMiscStandoffAim58;
	}
	if (Aim.AimMode == GMiscStandoffAimModeLatch)
	{
		Words.bSawNewEnemy = true;
	}
}

bool FElysiumNpcBase::StandoffVfunc28()
{
	// `0x102c7ef0` — six bytes: `return DAT_10601874`.
	return StandoffSchedulesLoaded();
}

bool FElysiumNpcBase::StandoffSchedulesLoaded()
{
	// SEAM for `_DAT_10601874`. Its ONLY reader in the image is slot 28 above, and its only writers
	// are `CAI_StandoffBehavior`'s class initialiser `0x102c7eb0` and the loader `0x102c7f10` it
	// calls — which registers the behaviour's schedules, its two activities
	// (`ACT_RANGE_AIM_SMG1_LOW`, `ACT_RANGE_AIM_PISTOL_LOW`) and its conditions into the global id
	// space `DAT_10936b68`, and stores the LAST `0x1030d850` result back into the latch.
	//
	// This runtime has no behaviour id-space loader, so the initialiser never runs and the latch
	// keeps the zero its `.data` cell holds. Answering false is the recovered state, not a refusal
	// invented to close a branch.
	return false;
}

// -------------------------------------------------------------------------------------------------
// Slot 272 `AllocateLayer` — `0x10099470`.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpcBase::AllocateLayer()
{
	// `0x10099470`. The listing is the authority here, because the decompilation types the array as
	// `m_angPrevSeqAngles`: `EDX = (i + i*2) << 4` then `+ ESI + 0x748`, stride `0x30` — the gesture
	// layer table at `+0x0734` with `m_flWeight` at `+0x14` inside each 0x30-byte record. The scan
	// starts at `GetFirstGestureLayer()` (slot 267, `0x10098a40`), runs while the index is below 4,
	// and answers the first slot whose weight equals `_DAT_104454c4` (0.0f), else -1.
	//
	// Family **Anim** already carries that table and landed this exact body as
	// `AllocateGestureLayer()` beside slot 272's stub, precisely so its rows would not search a stub
	// that answers 0 for "not found". The slot now forwards to it; there is one scan, not two.
	return AllocateGestureLayer();
}

// -------------------------------------------------------------------------------------------------
// Slot 296 — `0x10348ba0`.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpcBase::Slot296(int32 Argument)
{
	// `0x10348ba0`, arm by arm:
	//     if (m_GrappleRole (+0x153c) == -1) return false;
	//     if (m_GrapplePartner (+0x1538) == INVALID_EHANDLE) return false;
	//     if (!resolve(m_GrapplePartner)) return false;           // PTR_DAT_10566458, & 0x1fff,
	//                                                             // generation >> 0xd
	//     return param_1 == 0xb;
	//
	// The three grapple terms are exactly `FElysiumGrappleState::IsPaired()` plus the world resolve
	// its comment says every consumer that can reach a world must also do.
	//
	// **Unrecovered: what `0xb` is.** Slot 296 has no dispatch site anywhere in the image (0d/0v/0c),
	// so nothing states the argument's domain; it is not a `m_GrappleType` value (those run 0..8).
	// The literal is reproduced as the literal.
	if (!Grapple.IsPaired())
	{
		return false;
	}
	if (World == nullptr || World->Resolve(Grapple.Partner) == nullptr)
	{
		return false;
	}
	return Argument == 0xb;
}

// -------------------------------------------------------------------------------------------------
// Slot 513 `CapabilitiesGet` — `0x1026db30`.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpcBase::CapabilitiesGet() const
{
	// `0x1026db30`. Strip the scope-trace push/pop that brackets the body — retail's named debug
	// stack, which this runtime logs through its own channels — and the whole of it is:
	//     uint caps = m_afCapability;                             // +0x5cec
	//     if (GetActiveWeapon()) caps |= GetActiveWeapon()->vtable[+0x5a0]();   // slot 360
	//     return caps;
	//
	// Retail calls `GetActiveWeapon` TWICE, once for the null test and once for the dispatch; the
	// second call cannot answer differently, so one resolve carries both here.
	//
	// `ActiveWeaponCapabilityWord()` is family **Motor**'s seam for slot 360 (`0x1014f930`) and
	// answers 0 — there is no capability word on `FElysiumWeapon` — so today this is
	// `m_afCapability` verbatim, which is exactly what family Motor's own comment at `0x10278c60`
	// already assumed. Asking the seam rather than assuming it is what makes that assumption true.
	int32 Capabilities = CapabilityWord;
	const FElysiumEntity* Weapon = World != nullptr && Inventory.ActiveWeapon.IsSet()
		? World->Resolve(Inventory.ActiveWeapon)
		: nullptr;
	if (Weapon != nullptr)
	{
		Capabilities |= static_cast<int32>(ActiveWeaponCapabilityWord());
	}
	return Capabilities;
}

void* FElysiumNpcBase::CreateSenses()
{
	// `0x1027cc10`, slot 425. `operator new(0x88)`, then by hand:
	//     [0] = vftable_CAI_Component;  [1] = 0;  [3] = -1;
	//     [4] = 0x45400000 (3072.0f);  [5] = [6] = 0xbf800000 (-1.0f);  [7] = 0;
	//     three chained sub-objects at +0x20, +0x34, +0x48, each thunk_FUN_1027f8f0(p, 0, 0),
	//       each storing the previous one's first word in its own +0x0c;
	//     [0x1a] = -1.0f, then thunk_FUN_1027cda0(+0x68, 0.15f, true),
	//       thunk_FUN_1027cd50(+0x70, 0.25f, true), thunk_FUN_1027cd50(+0x78, 0.45f, true);
	//     [0x20] = 1 (byte);  [0x21] = 0;  [0] = vftable_CAI_Senses;
	//     [0x17..0x19] = the three sub-objects;  [1] = this (the owner);
	//     return it;  (on an allocation failure it writes `this` to absolute address 4 and returns 0)
	//
	// The three constants are the sense-refresh intervals every `CAI_Senses` starts with: 0.15 s,
	// 0.25 s, 0.45 s. They are NOT lost by answering null — this runtime's sense cadence is family
	// **Senses**' and is driven by the NPC's own think, not by a component's three timers.
	//
	// SEAM: there is no `CAI_Senses` object. `+0x5cdc` is bound to `FElysiumNpc::Senses`, a struct
	// the NPC already carries, so there is nothing to allocate and nothing to point at.
	LastComponentFactoryBody = TEXT("0x1027cc10");
	++ComponentFactoryRefusals;
	return nullptr;
}

void* FElysiumNpcBase::CreateMoveProbe()
{
	// `0x1027cef0`, slot 426. `operator new(0x14)`, then `p[1] = this` (the owner), `p[3] = -1` (the
	// same sentinel every component in this band seeds) and `p[0] = vftable_CAI_MoveProbe`. No
	// constructor call at all — the whole object is four stores.
	//
	// SEAM: `+0x5d40` is an `ELYSIUM_NPC_WORD_CHAIN` row on the motor; there is no move probe.
	LastComponentFactoryBody = TEXT("0x1027cef0");
	++ComponentFactoryRefusals;
	return nullptr;
}

void* FElysiumNpcBase::CreateMotor()
{
	// `0x1027cec0`, slot 427: `operator new(0x6c)` then `thunk_FUN_102e0900(p, this)` — the base
	// `CAI_Motor`, whose own constructor installs its vftable.
	//
	// SEAM: `+0x5d34` is an `ELYSIUM_NPC_WORD_CHAIN` row on `FElysiumScriptedCharacter::Motor`, the
	// one `IElysiumNpcMotor` seam, which the services provide rather than the NPC allocating.
	// `LastComponentFactoryBody` still says WHICH motor this class would have been given.
	LastComponentFactoryBody = TEXT("0x1027cec0");
	++ComponentFactoryRefusals;
	return nullptr;
}

void* FElysiumNpcBase::CreateLocalNavigator()
{
	// `0x1027cf60`, slot 428: `operator new(0x20)` then `thunk_FUN_102ddab0(p, this)`.
	// `CNPC_VRat::vfunc428` (`0x103ad6a0`) overrides it on `FElysiumNpcRat` (story 5 step 3).
	//
	// SEAM: `+0x5d38` is folded into the same motor seam.
	LastComponentFactoryBody = TEXT("0x1027cf60");
	++ComponentFactoryRefusals;
	return nullptr;
}

void* FElysiumNpcBase::CreateNavigator()
{
	// `0x1027cf90`, slot 429: `operator new(0x68)` then `thunk_FUN_102eca50(p, this)`.
	//
	// SEAM: `+0x5d34`'s chain row already says the navigator is the motor seam's concern here.
	LastComponentFactoryBody = TEXT("0x1027cf90");
	++ComponentFactoryRefusals;
	return nullptr;
}

void* FElysiumNpcBase::CreatePathfinder()
{
	// `0x1027cfc0`, slot 430: `operator new(0x18)`, `p[1] = this`, `p[3] = -1`, `p[4] = p[5] = 0`,
	// `p[0] = vftable_CAI_Pathfinder`. Like the move probe, no constructor call.
	//
	// SEAM: `+0x5d3c` is an `ELYSIUM_NPC_WORD_CHAIN` row — the motor owns path generation.
	LastComponentFactoryBody = TEXT("0x1027cfc0");
	++ComponentFactoryRefusals;
	return nullptr;
}

bool FElysiumNpcBase::CreateComponents()
{
	// `0x1027cae0`, slot 424 — retail's fail-fast chain, in its own order, which is NOT the slot
	// order:
	//     m_pSenses  = vt[+0x6a4]();  if (!m_pSenses)  return false;   // slot 425
	//     m_pMotor   = vt[+0x6ac]();  if (!m_pMotor)   return false;   // slot 427
	//     +0x5d38    = vt[+0x6b0]();  if (!+0x5d38)    return false;   // slot 428 local navigator
	//     +0x5d40    = vt[+0x6a8]();  if (!+0x5d40)    return false;   // slot 426 move probe
	//     m_pNavigator  = vt[+0x6b4](); if (!it) return false;         // slot 429
	//     m_pPathfinder = vt[+0x6b8](); if (!it) return false;         // slot 430
	//     +0x5cf8 = this;                                              // the pathfinder's owner
	//     thunk_FUN_102e0ba0(m_pMotor, +0x5d38 ? +0x5d38 + 0x10 : 0);  // motor <- local navigator
	//     thunk_FUN_102ddc00(+0x5d38, m_pNavigator ? m_pNavigator + 0x10 : 0);
	//     m_pNavigator->vt[+0xc](DAT_1093407c);
	//     return true;
	//
	// Note the order: **the local navigator is built before the move probe** even though its slot
	// number is higher, and the two `+ 0x10` adjustments are second-base pointers, not offsets into
	// a field. Both null guards before the thunks are dead — the chain has already returned false on
	// either being null — and are reproduced as written.
	//
	// SEAM CONSEQUENCE, stated rather than papered over: all six factories answer null here, so this
	// answers FALSE at the first one (slot 425), which is retail's own answer when a factory fails.
	// Nothing in this runtime calls `CreateComponents` — spawn does not route through it — so the
	// false is observable only to a test. `FirstRefusedComponentSlot` records which factory refused
	// so the chain's ORDER is measurable even while every arm refuses.
	FirstRefusedComponentSlot = INDEX_NONE;
	const int32 ChainOrder[] = { 425, 427, 428, 426, 429, 430 };
	for (int32 Slot : ChainOrder)
	{
		void* Component = nullptr;
		switch (Slot)
		{
		case 425: Component = CreateSenses(); break;
		case 427: Component = CreateMotor(); break;
		case 428: Component = CreateLocalNavigator(); break;
		case 426: Component = CreateMoveProbe(); break;
		case 429: Component = CreateNavigator(); break;
		default:  Component = CreatePathfinder(); break;
		}
		if (Component == nullptr)
		{
			FirstRefusedComponentSlot = Slot;
			return false;
		}
	}
	// Unreachable while the six seams refuse. The tail retail runs here — the pathfinder-owner
	// write (`+0x5cf8`), the two cross-wires and the navigator's `vtable+0xc` call with
	// `DAT_1093407c` (the global node graph) — has no counterpart to write to and is recorded in
	// the comment above rather than stood as six more seams.
	return true;
}

bool FElysiumNpcBase::OkToDisturb() const
{
	// `0x1028a190`, walked off the LISTING because the decompilation is damaged (`Could not recover
	// jumptable at 0x1028a21d`). The listing is linear and the whole body is:
	//
	//     1028a195  CALL [EAX+0x740]        ; slot 464 GetState()
	//     1028a19b  CMP EAX,0x4             ; NPC_STATE_SCRIPT
	//     1028a1a0  MOV EAX,[ESI+0x5d74]    ; m_hCine
	//     ... resolve it, and if it resolves:
	//     1028a1fa  CALL 0x1000c6da         ; -> 0x101a8930 CCineNPC::CanInterrupt, ON THE CINE
	//     1028a201  JZ  0x1028a223          ; false -> return false
	//     1028a203  MOV AL,[ESI+0x5bc4]     ; m_bInChoreoScene -> non-zero returns false
	//     1028a20d  MOV EAX,[ESI+0x14bc]
	//     1028a213  TEST AH,0x10            ; m_bfAINPCFlags2 & 0x1000 -> set returns false
	//     1028a21d  JMP [EDX+0x278]         ; tail-call slot 158 IsAlive()
	//
	// **`+0x14bc & 0x1000` is `MADE_OBLIVIOUS`, and this is a reader of it.** `ElysiumNpcFlags.h`
	// records that bit as having "ZERO readers anywhere in retail"; that is wrong, and this address
	// is the counter-example. The header is another story's file and is left alone; the correction
	// is recorded here and in `docs/vtmb/npc-ai/conditions-and-states.md`.
	//
	// The cine arm asks `CineCanInterrupt()` (family **EntityChain**'s port of `0x101a8930`) on
	// THIS NPC rather than on the resolved cine: this runtime stands one leaf and the cine entity
	// is not an `FElysiumNpc`. That body's own `m_interruptable` term is a seam answering false, so
	// the arm refuses either way — which is the recovered refusal and is stated here, not hidden.
	//
	// Retail name unrecovered.
	if (Mind.State() == EElysiumNpcState::Scripted && ScriptOwner.IsSet()
		&& World != nullptr && World->Resolve(ScriptOwner) != nullptr)
	{
		if (!CineCanInterrupt())
		{
			return false;
		}
	}
	if (bInChoreoScene)
	{
		return false;
	}
	if (NpcFlags.Has(GMiscObliviousBit))
	{
		return false;
	}
	return const_cast<FElysiumNpcBase*>(this)->IsAlive();
}
