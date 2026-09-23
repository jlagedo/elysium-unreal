#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"

// Story 29c-1, family **Species** — the per-species bodies of layers 0–9. This file carries slot
// 323, the species slot table and its dispatchers, the two `CUtlVector<{EHANDLE, expiry}>` stores
// (`CNPC_VBaseBoss`'s and `CNPC_VTzimisce`'s) and the melee quartet's species replacements (slots
// 599, 600, 601, 602) plus slots 606 and 609. Everything else — `CNPC_Crow`, `CNPC_VNewscaster`,
// `CNPC_VTzimisce`'s carry chain, `CNPC_VWerewolf`, `CNPC_VZombie`, `CNPC_VMingXiaoTentacle`,
// `CNPC_VCamera`, `CScriptedTarget` and `CNPC_VAndreiBlood` — is
// `Substrate/ElysiumNpcKernelSpecies2.cpp`. The declarations and this family's three standing facts
// are `Substrate/ElysiumNpcKernelSpecies.inl`; the walked prose is `docs/vtmb/npc-ai/shape.md`.
//
// **Every `.rdata` constant in this family was READ**, not inferred: the pinned retail
// `vampire.dll`'s `.rdata` was addressed directly (image base `0x10000000`, section VA
// `0x10445000`, raw `0x445000`) and each cell's little-endian float is quoted beside its address
// below. Where a datum lives in `.data` and is filled at runtime it is a seam and says so. Nothing
// here is a guess dressed as a number.

namespace
{
	// --- Retail `.rdata`, one line per cell, each value read out of the pinned image ---------------

	// The pooled zeroes (`_DAT_104454c4`, and the double `_DAT_1044fab0`) and one.
	constexpr float SpeciesZero = ElysiumNpcTunables::Zero;
	constexpr float SpeciesOne = ElysiumNpcTunables::One;

	// Slot 323 (`0x10344dd0`). The direction's 2-D length must clear this before an angle is taken
	// at all — a hair over zero, so the too-slow arm is only a genuinely stationary direction.
	constexpr float MoveDirectionMinLength2D = 1.0e-07f;    // _DAT_1049e028

	// Slot 323's `UTIL_AngleMod`: `(360/65536) * (ftol(a * 65536/360) & 0xffff)`.
	constexpr float AngleModScale = ElysiumNpcTunables::AngleQuantum;   // 360 / 65536
	constexpr float GSpeciesDegreesPerTurn = 360.0f;                // _DAT_10450568

	// Slot 323's four band boundaries. **316, not 315** — read out of the image; the bands are 89,
	// 90, 90 and 91 degrees wide and that asymmetry is retail's.
	constexpr float MoveDirectionAheadBand = ElysiumNpcTunables::FortyFive;
	constexpr float MoveDirectionLeftBand = 135.0f;         // _DAT_1049e8a0
	constexpr float MoveDirectionBehindBand = 225.0f;       // _DAT_1049e89c
	constexpr float MoveDirectionRightBand = 316.0f;        // _DAT_1049e8a4

	// The melee timers the species slot-599 bodies arm. `CNPC_VTzimisceHeadClaw` uses the SAME pair
	// as the Troika line (`0x40f00000` / `0x41700000`); `CNPC_VTzimisce`'s carry latch uses a
	// narrower one (`0x40f00000` / `0x41200000`) and `CNPC_VYukie`'s flee window a much wider one.
	constexpr float SpeciesMeleeMustLeaveMin = 7.5f;        // 0x40f00000
	constexpr float SpeciesMeleeMustLeaveMax = 15.0f;       // 0x41700000

	// The weapon capability bits slots 600 and `CNPC_VYukie`'s flee gate require.
	constexpr uint32 SpeciesMeleeWeaponCapabilityBits = 0x18000u;

	// `CNPC_VYukie`'s flee window, `curtime + RandomFloat(22.5, 45.0)`. The immediates are
	// `0x41b40000` and `0x42340000`; 29c's walk reads them as 22.0/45.0 and the first is **22.5**.
	constexpr float YukieFleeWindowMin = 22.5f;             // 0x41b40000
	constexpr float YukieFleeWindowMax = 45.0f;             // 0x42340000

	// The two `m_NPCState` values slot 609's species gate admits: `NPC_STATE_SCRIPT` (4) and 0xc,
	// which this runtime's `EElysiumNpcState` has no member for. Both are compared as raw numbers.
	constexpr int32 ShootAtHintStateScript = 4;
	constexpr int32 ShootAtHintStateTwelve = 0xc;

	// `CNPC_VBach`'s slot-606 condition, `COND_ENEMY_OCCLUDED`.
	constexpr EElysiumNpcCond BachOccludeCondition = EElysiumNpcCond::EnemyOccluded;

	// The NPC's `m_NPCState` in RETAIL's ordinals. Family **Anim** keeps an identical private copy
	// for the same reason: neither family owns the other's file, and slot 609's gate compares raw
	// numbers so the mapping has to happen before the comparison.
	int32 SpeciesRetailNpcState(EElysiumNpcState State)
	{
		switch (State)
		{
		case EElysiumNpcState::Idle:     return 1;
		case EElysiumNpcState::Combat:   return 2;
		case EElysiumNpcState::Alert:    return 3;
		case EElysiumNpcState::Scripted: return 4;
		case EElysiumNpcState::Prone:    return 6;
		case EElysiumNpcState::Dead:     return 7;
		default:                         return 0;
		}
	}

	// `UTIL_VecToYaw` `0x101d2c70` over a delta in THIS world's axes, whose Y is the negated Source
	// one (`bsp.source_to_unreal`). Families Bosses, Facing and Positions each keep an identical
	// private copy; this is the fourth, for the same file-ownership reason.
	float SpeciesVecToYaw(const FVector& PortDelta)
	{
		if (PortDelta.X == 0.0 && PortDelta.Y == 0.0)
		{
			return SpeciesZero;
		}
		float Yaw = FMath::RadiansToDegrees(
			static_cast<float>(FMath::Atan2(-PortDelta.Y, PortDelta.X)));
		if (Yaw < SpeciesZero)
		{
			Yaw += GSpeciesDegreesPerTurn;
		}
		return Yaw;
	}

	// `UTIL_AngleMod` — the `ftol`/mask/scale triple slot 323 folds its relative yaw through.
	// Reproduced as the INTEGER pipeline retail runs, not as an `fmod`: the truncation toward zero
	// and the 16-bit mask are what make 360.0 come back as 0.0 and a negative angle come back
	// positive, and an `fmod` would answer differently at the boundaries this body compares on.
	float SpeciesAngleMod(float Degrees)
	{
		const int32 Fixed = static_cast<int32>(Degrees * (1.0f / AngleModScale));
		float Modded = static_cast<float>(static_cast<uint32>(Fixed) & 0xffffu) * AngleModScale;
		// `if (fVar1 < _DAT_104454c4) fVar1 = fVar1 + _DAT_10450568;` — dead after the mask, which
		// cannot produce a negative, and reproduced because retail carries it.
		if (Modded < SpeciesZero)
		{
			Modded += GSpeciesDegreesPerTurn;
		}
		return Modded;
	}
}

// -------------------------------------------------------------------------------------------------
// Slot 323 — `CAI_BaseNPC::FUN_10344dd0` `0x10344dd0`. 78 classes share the one body.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpc::Slot323(const FVector& DirectionCm)
{
	// `0x10344dd0`, arm by arm:
	//
	//     len2d = sqrt(dir.x*dir.x + dir.y*dir.y);          // PTR_thunk_FUN_101371d0
	//     if (_DAT_1049e028 (1e-07) <= len2d) {
	//         dir.z = 0;
	//         a = UTIL_AngleMod( VecToYaw(dir) - GetAbsAngles().y );   // 0x101d2c70, slot 219
	//         if (316.0 < a || a <= 45.0)   return 2;
	//         if (a <= 135.0)               return 3;
	//         if (135.0 < a && a <= 225.0)  return 0;
	//         if (225.0 < a)                return 1;
	//     }
	//     return 0;
	//
	// The decompiler lost the subtraction between `VecToYaw` and `GetAbsAngles` — both calls are
	// there with their results dropped on the FPU stack and a bare `__ftol()` after them, which is
	// the `UTIL_AngleMod` prologue. The reading is settled by the arithmetic that DID survive: the
	// `& 0xffff` and the `* 0.0054931640625` are `UTIL_AngleMod`'s, and a body that bucketed an
	// ABSOLUTE yaw into four fixed bands would answer the same code for the same world direction no
	// matter which way the NPC faced, which is not a direction code. **Unrecovered:** nothing else;
	// the operand order (`yaw - facing`, not `facing - yaw`) is the one that makes band 2 the body's
	// own heading, and band 2 is the band the recovered `45.0` centres on zero.
	//
	// `DirectionCm` is this runtime's axes; `SpeciesVecToYaw` negates Y for Source's, exactly as the
	// three other copies of `UTIL_VecToYaw` in the kernel do. The Z flatten is retail's and is done
	// before the yaw rather than after, which changes nothing and is kept in retail's order.
	const double Length2D = FMath::Sqrt(
		DirectionCm.X * DirectionCm.X + DirectionCm.Y * DirectionCm.Y);
	if (static_cast<float>(Length2D) < MoveDirectionMinLength2D)
	{
		return static_cast<int32>(EMoveDirectionCode::Behind);   // retail's `0`
	}
	const FVector Flat(DirectionCm.X, DirectionCm.Y, 0.0);
	const float Relative = SpeciesAngleMod(
		SpeciesVecToYaw(Flat) - static_cast<float>(Angles.Y));

	if (MoveDirectionRightBand < Relative || Relative <= MoveDirectionAheadBand)
	{
		return static_cast<int32>(EMoveDirectionCode::Ahead);
	}
	if (Relative <= MoveDirectionLeftBand)
	{
		return static_cast<int32>(EMoveDirectionCode::Left);
	}
	if (MoveDirectionLeftBand < Relative && Relative <= MoveDirectionBehindBand)
	{
		return static_cast<int32>(EMoveDirectionCode::Behind);
	}
	if (MoveDirectionBehindBand < Relative)
	{
		return static_cast<int32>(EMoveDirectionCode::Right);
	}
	// Unreachable — the four bands cover [0, 360). Retail carries the fall-through and so does this.
	return static_cast<int32>(EMoveDirectionCode::Behind);
}

// -------------------------------------------------------------------------------------------------
// The species slot table — this family's whole dispatch surface.
// -------------------------------------------------------------------------------------------------

const FElysiumNpc::FSpeciesSlotRow* FElysiumNpc::SpeciesSlotRows(int32& OutCount)
{
	// One row per (class, slot) this family ports, each carrying the RETAIL ADDRESS of the body so
	// the table is checkable against `docs/vtmb/npc-kernel/slots.md` by eye. Ordered by slot then
	// class; every row is exercised by name in `Elysium.Substrate.NpcKernelSpecies.SlotTable`.
	static constexpr FSpeciesSlotRow Rows[] =
	{
		// `CNPC_VMingXiaoTentacle` forwards three consecutive slots to its head.
		{ TEXT("CNPC_VMingXiaoTentacle"), 21, TEXT("0x1039e800") },
		{ TEXT("CNPC_VMingXiaoTentacle"), 22, TEXT("0x1039e830") },
		{ TEXT("CNPC_VMingXiaoTentacle"), 23, TEXT("0x1039e860") },
		// `CNPC_VZombie` fires `m_OnAttackedVictim` from two slots with no base forward.
		{ TEXT("CNPC_VZombie"), 25, TEXT("0x103e12c0") },
		{ TEXT("CNPC_VZombie"), 26, TEXT("0x103e12f0") },
		// Slot 103 `Spawn` on a class no map stands: `CScriptedTarget`.
		{ TEXT("CScriptedTarget"), 103, TEXT("0x1034d6e0") },
		// Slot 139 `DeathNotice` — `CNPCMaker_Fleshpile`'s, which lands on `FElysiumNpcMaker`.
		{ TEXT("CNPCMaker_Fleshpile"), 139, TEXT("0x1034c8e0") },
		// Slot 197 `BodyTarget` — `CNPC_Crow`'s same-object forward to slot 192.
		{ TEXT("CNPC_Crow"), 197, TEXT("0x103577d0") },
		// Slot 482 `CanPlaySequence`. Five classes, every body byte-identical to the base.
		{ TEXT("CNPC_VAnimal"), 482, TEXT("0x1035fd40") },
		{ TEXT("CNPC_VTzimisce"), 482, TEXT("0x103bd270") },
		// Slot 488 `DeathSound` — `CNPC_VTzimisce` fires `SPI_DIES` then tails into slot 487.
		{ TEXT("CNPC_VTzimisce"), 488, TEXT("0x103b92a0") },
		// Slots 497 and 506 — `CNPC_VCamera`'s two empty sound hooks.
		{ TEXT("CNPC_VCamera"), 497, TEXT("0x103681d0") },
		{ TEXT("CNPC_VCamera"), 506, TEXT("0x103682f0") },
		// Slot 510 `ShouldPlayFloatSound` — `CNPC_VZombie`'s moan gate.
		{ TEXT("CNPC_VZombie"), 510, TEXT("0x103e1080") },
		// Slot 588 — `CNPC_VTzimisceRunner` drops the base's `IsActivityFinished` gate.
		{ TEXT("CNPC_VTzimisceRunner"), 588, TEXT("0x103c3fd0") },
		// Slot 593 — `CNPC_VTzimisce`'s five target-lead literals.
		{ TEXT("CNPC_VTzimisce"), 593, TEXT("0x103b9180") },
		// The melee quartet, slots 599..602. These are the six classes `MeleeSlotLine` answers
		// `EMeleeSlotLine::Species` for.
		{ TEXT("CNPC_VFrenzyShadow"), 599, TEXT("0x10376b70") },
		{ TEXT("CNPC_VGargoyle"), 599, TEXT("0x10379ef0") },
		{ TEXT("CNPC_VTzimisceHeadClaw"), 599, TEXT("0x103c19e0") },
		{ TEXT("CNPC_VTzimisceRunner"), 599, TEXT("0x103c3960") },
		{ TEXT("CNPC_VFrenzyShadow"), 600, TEXT("0x10376ba0") },
		{ TEXT("CNPC_VGargoyle"), 600, TEXT("0x10379f20") },
		{ TEXT("CNPC_VTzimisceHeadClaw"), 600, TEXT("0x103c1a60") },
		{ TEXT("CNPC_VTzimisceRunner"), 600, TEXT("0x103c39e0") },
		{ TEXT("CNPC_VYukie"), 600, TEXT("0x103dd900") },
		{ TEXT("CNPC_VTzimisceHeadClaw"), 601, TEXT("0x103c1ad0") },
		{ TEXT("CNPC_VTzimisceRunner"), 601, TEXT("0x103c3a70") },
		{ TEXT("CNPC_VTzimisceHeadClaw"), 602, TEXT("0x103c1b10") },
		{ TEXT("CNPC_VTzimisceRunner"), 602, TEXT("0x103c3ab0") },
		// Slot 606 — `CNPC_VBach`'s arm-then-fire gate around `COND_ENEMY_OCCLUDED`.
		{ TEXT("CNPC_VBach"), 606, TEXT("0x10364280") },
		// Slot 609 — three byte-identical state gates in front of the base hint search.
		{ TEXT("CNPC_VBach"), 609, TEXT("0x103661f0") },
		{ TEXT("CNPC_VBatSwarm"), 609, TEXT("0x10367740") },
		{ TEXT("CNPC_VSheriffSwarm"), 609, TEXT("0x103b26f0") },
		// Slot 617 `MakeNPC` — `CNPCMaker_Fleshpile`'s, which lands on `FElysiumNpcMaker`.
		{ TEXT("CNPCMaker_Fleshpile"), 617, TEXT("0x1034c2d0") },
	};
	OutCount = UE_ARRAY_COUNT(Rows);
	return Rows;
}

const FElysiumNpc::FSpeciesSlotRow* FElysiumNpc::SpeciesSlotRowOf(const TCHAR* InRetailClass,
	int32 Slot)
{
	if (InRetailClass == nullptr)
	{
		return nullptr;
	}
	int32 Count = 0;
	const FSpeciesSlotRow* Rows = SpeciesSlotRows(Count);
	// The exact class first, then the base chain — which is the vtable's own resolution, and the
	// reason it is a walk rather than a name compare: a subclass with no body of its own at a slot
	// runs its base's, and several of these classes have subclasses in the census
	// (`CNPC_VCameraSecurity` under `CNPC_VCamera`, `CNPC_VDog`/`CNPC_VRat` under `CNPC_VAnimal`).
	for (int32 i = 0; i < Count; ++i)
	{
		if (Rows[i].Slot == Slot && FCString::Strcmp(Rows[i].RetailClass, InRetailClass) == 0)
		{
			return &Rows[i];
		}
	}
	const FElysiumNpcClass* Cls = ElysiumNpcKernelClass::Find(InRetailClass);
	for (int32 i = 0; i < Count; ++i)
	{
		if (Rows[i].Slot == Slot
			&& ElysiumNpcKernelClass::DerivesFrom(Cls, Rows[i].RetailClass))
		{
			return &Rows[i];
		}
	}
	return nullptr;
}

const FElysiumNpc::FSpeciesSlotRow* FElysiumNpc::SpeciesSlotRow(int32 Slot) const
{
	// `RetailClass()` is NULL for a classname no census class claims — `npc_VCop` is the recovered
	// example, and the fall-through to "no species body, run the one you have" is that classname's
	// correct answer rather than a hole to patch.
	const FElysiumNpcClass* Cls = RetailClass();
	return SpeciesSlotRowOf(Cls != nullptr ? Cls->Name : nullptr, Slot);
}

const FElysiumNpc::FSpeciesSlotRow* FElysiumNpc::SpeciesDispatchRow(int32 Slot) const
{
	// Retail's non-virtual thunk, spelled once: a species body that calls the body it replaces is
	// calling it DIRECTLY, so slot `Slot`'s dispatcher must answer "nothing to run" while slot
	// `Slot`'s own species body is on the stack. See the note in `ElysiumNpcKernelSpecies.inl`.
	if (SpeciesDispatchingSlot == Slot)
	{
		return nullptr;
	}
	return SpeciesSlotRow(Slot);
}

FElysiumNpc::FSpeciesDispatchScope::FSpeciesDispatchScope(FElysiumNpc& InNpc, int32 Slot)
	: Npc(InNpc)
	, Previous(InNpc.SpeciesDispatchingSlot)
{
	Npc.SpeciesDispatchingSlot = Slot;
}

FElysiumNpc::FSpeciesDispatchScope::~FSpeciesDispatchScope()
{
	Npc.SpeciesDispatchingSlot = Previous;
}

#if WITH_DEV_AUTOMATION_TESTS
void FElysiumNpc::SetRetailClassForTests(const TCHAR* RetailClassName)
{
	// The same two writes `RetailClass()` makes on its first call, with the classname resolution
	// replaced by a direct census lookup. A null name stands an NPC the census claims nothing for,
	// which is `npc_VCop`'s and `CNPC_VZombie`'s own answer.
	bRetailClassResolved = true;
	RetailClassRow = ElysiumNpcKernelClass::Find(RetailClassName);
}
#endif

// -------------------------------------------------------------------------------------------------
// `CNPC_VBaseBoss::m_BlacklistedEntities` — `0x103662d0`, `0x10366400`, `0x10366490`.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::FUN_103662d0(const FElysiumEntityHandle& Entity, float Seconds)
{
	// `0x103662d0`, the ADD. Retail is a hand-inlined `CUtlVector<{EHANDLE, float}>::InsertBefore`
	// at the END of the list:
	//
	//     expiry = gpGlobals->curtime + param_2;
	//     n = m_Count (+0x6668);  cap = m_AllocCount (+0x6660);
	//     if (cap < n + 1 && m_GrowSize (+0x6664) != -1) {
	//         while (cap < n + 1) cap = (cap == 0) ? 4 : (grow == 0 ? cap * 2 : cap + grow);
	//         m_AllocCount = cap;
	//         m_Elements = m_Elements ? Plat_Realloc(m_Elements, cap * 8) : Plat_Alloc(cap * 8);
	//     }
	//     m_ElementMirror (+0x666c) = m_Elements;
	//     m_Count = n + 1;
	//     memmove(&e[n+1], &e[n], ((n + 1) - n - 1) * 8);        // always zero bytes on an append
	//     e[n] = { param_1, expiry };
	//
	// The `memmove` moves `(count + 1 - index - 1) * 8` bytes with `index == count`, i.e. **zero**,
	// so the insert is an append and the shift is dead on every call. Family Anim recorded the same
	// dead `0x10430fa0` on its own add. `TArray::Emplace` is that append; the doubling growth and
	// the 4-row first allocation are not observable through any read, so they are not reproduced
	// and this says so rather than carrying a capacity word nothing can see.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	BossBlacklist.Emplace(FBlacklistedEntity{ Entity, Now + static_cast<double>(Seconds) });
}

int32 FElysiumNpc::FUN_10366490(const FElysiumEntity* Candidate) const
{
	// `0x10366490`, the INDEX-OF. Walks `m_Count` rows, resolves each row's `EHANDLE` through the
	// global entity table (`PTR_DAT_10566458`, sentinel `0xffffffff`, serial in the top 19 bits)
	// and answers the index whose RESOLVED POINTER equals the argument, else -1.
	//
	// Two retail details that are kept: a row whose handle is dead resolves to **0**, so a NULL
	// argument would match the first dead row — and this port reproduces that, because the callers
	// below pass `0x10366400`'s argument straight through and one of them can be null.
	for (int32 i = 0; i < BossBlacklist.Num(); ++i)
	{
		const FElysiumEntity* Resolved = World != nullptr
			? World->Resolve(BossBlacklist[i].Entity) : nullptr;
		if (Resolved == Candidate)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

bool FElysiumNpc::FUN_10366400(const FElysiumEntity* Candidate)
{
	// `0x10366400`, the TEST-AND-EXPIRE:
	//
	//     i = IndexOf(param_1);                      // thunk 0x10366490
	//     if (i == -1) return false;                 // <- the low byte of -1 is 0xff, see below
	//     if (curtime < e[i].expiry) return true;    // still blacklisted
	//     if (0 < m_Count) { memmove(&e[i], &e[m_Count - 1], 8); m_Count -= 1; }
	//     return false;
	//
	// **The `-1` arm is a retail quirk and it is reproduced.** The body returns `uVar4 & 0xffffff00`
	// on the miss path, where `uVar4` is the index — so for `i == -1` the returned byte is
	// `0xffffffff & 0xffffff00`'s low byte, i.e. **0**, and the caller reads false. For any other
	// miss (expired) the low byte is likewise cleared. So every non-"still live" path answers false
	// and the only true is the unexpired one; the sign of the index never leaks out.
	//
	// The removal is a SWAP-REMOVE — the LAST row is memmoved over the one being dropped and the
	// count decremented — so the list's order is not preserved and a later index-of can answer a
	// different number for the same entity. `RemoveAtSwap` is exactly that.
	const int32 Index = FUN_10366490(Candidate);
	if (Index == INDEX_NONE)
	{
		return false;
	}
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	if (Now < BossBlacklist[Index].ExpiresAt)
	{
		return true;
	}
	if (BossBlacklist.Num() > 0)
	{
		BossBlacklist.RemoveAtSwap(Index);
	}
	return false;
}

// -------------------------------------------------------------------------------------------------
// `CNPC_VTzimisce`'s own blacklist — `0x103bf200`, `0x103bf330`, `0x103bf3c0`.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::FUN_103bf200(const FElysiumEntityHandle& Entity)
{
	// `0x103bf200`. The SAME hand-inlined `CUtlVector` append as `0x103662d0`, at `+0x6690` instead
	// of `+0x665c`, with one difference: the duration is not a parameter. It is the `.rdata` cell
	// `_DAT_1044eb0c`, read out of the pinned image as **20.0** — the same twenty seconds
	// `CNPC_VHengeyokai`'s blacklist (family Bosses' `BlacklistSeconds`) and MingXiao's thrown-object
	// skip use, off the same cell.
	constexpr float TzimisceBlacklistSeconds = 20.0f;   // _DAT_1044eb0c
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	TzimisceBlacklist.Emplace(
		FBlacklistedEntity{ Entity, Now + static_cast<double>(TzimisceBlacklistSeconds) });
}

int32 FElysiumNpc::FUN_103bf3c0(const FElysiumEntity* Candidate) const
{
	// `0x103bf3c0` — `0x10366490` written a second time over `+0x6690`/`+0x669c`. Instruction for
	// instruction the same walk, the same sentinel and the same resolve-to-zero on a dead row.
	for (int32 i = 0; i < TzimisceBlacklist.Num(); ++i)
	{
		const FElysiumEntity* Resolved = World != nullptr
			? World->Resolve(TzimisceBlacklist[i].Entity) : nullptr;
		if (Resolved == Candidate)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

bool FElysiumNpc::FUN_103bf330(const FElysiumEntity* Candidate)
{
	// `0x103bf330` — `0x10366400` written a second time, including the `& 0xffffff00` miss arm and
	// the swap-remove. Two classes, two offsets, one behaviour.
	const int32 Index = FUN_103bf3c0(Candidate);
	if (Index == INDEX_NONE)
	{
		return false;
	}
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	if (Now < TzimisceBlacklist[Index].ExpiresAt)
	{
		return true;
	}
	if (TzimisceBlacklist.Num() > 0)
	{
		TzimisceBlacklist.RemoveAtSwap(Index);
	}
	return false;
}

// -------------------------------------------------------------------------------------------------
// Slot 599 — the four species replacements of `CAI_BaseNPCTroika::FUN_102b5650`.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::FUN_10376b70(FElysiumEntity* Enemy)
{
	// `0x10376b70`, `CNPC_VFrenzyShadow`'s slot 599, the WHOLE body:
	//     (*DAT_10924edc)->vfunc1();      // the global melee-entered event, FIRST
	//     m_bInMelee = 1;
	//     return <whatever was in EAX>;
	//
	// Twenty-six bytes. It drops EVERY gate the Troika line has — the frenzied bits, the melee-enter
	// timer, the range and height terms and the attack coordinator — so a frenzy shadow enters melee
	// unconditionally and can never be refused. The declared return is `void`; the caller reads the
	// register the event call left behind, which is not a decision this body makes. Slot 599's port
	// signature answers `bool`, and **true** is the only honest reading: `m_bInMelee` was set, which
	// is what "entered melee" means to every caller of slot 599.
	//
	// `Enemy` is read by nothing in the body — retail takes it and ignores it, exactly as the Troika
	// line does.
	(void)Enemy;
	++MeleeEventFires;   // `(*DAT_10924edc)->vfunc1()`, family Bosses' counter for this global
	bInMelee = true;
	return true;
}

bool FElysiumNpc::FUN_10379ef0(FElysiumEntity* Enemy)
{
	// `0x10379ef0`, `CNPC_VGargoyle`'s slot 599 — byte-identical to `CNPC_VFrenzyShadow`'s
	// `0x10376b70` above, verified against the decompiled C of both. Two classes, one body, and it
	// is CALLED rather than restated so the two cannot drift.
	return FUN_10376b70(Enemy);
}

bool FElysiumNpc::FUN_103c19e0(FElysiumEntity* Enemy)
{
	// `0x103c19e0`, `CNPC_VTzimisceHeadClaw`'s slot 599:
	//
	//     if (RequestMeleeSlot(m_pAttackCoordinator, this)) {     // 0x1025db70
	//         (*DAT_10924edc)->vfunc1();
	//         m_bInMelee = 1;
	//         m_flMeleeMustLeaveTimer = curtime + RandomFloat(7.5, 15.0);
	//         return true;
	//     }
	//     m_bInMelee = 0;
	//     return false;
	//
	// It replaces the Troika line's five-term ladder with the COORDINATOR ALONE: no frenzied bits,
	// no follower boss, no melee-enter timer, no range or height test. The timer it arms afterwards
	// is the same `RandomFloat(7.5, 15.0)` (`0x40f00000` / `0x41700000`) the Troika line uses, and
	// the event fires BEFORE the two writes, which is the Troika line's order too.
	//
	// `MeleeCoordinatorAdmits599()` is family TroikaHelpers' seam over `0x1025db70` and answers
	// false with no coordinator object here — so this body takes its refusal arm, clears
	// `m_bInMelee` and answers false. That IS retail's behaviour for a coordinator with no free
	// slot; what this substrate cannot yet produce is the accepting arm.
	(void)Enemy;
	if (MeleeCoordinatorAdmits599())
	{
		++MeleeEventFires;
		bInMelee = true;
		const double Now = World != nullptr ? World->NowSeconds() : 0.0;
		MeleeMustLeaveTimer = Now + static_cast<double>(
			ElysiumRng::Stream(EElysiumRngStream::NpcSchedule)
				.FRandRange(SpeciesMeleeMustLeaveMin, SpeciesMeleeMustLeaveMax));
		return true;
	}
	bInMelee = false;
	return false;
}

bool FElysiumNpc::FUN_103c3960(FElysiumEntity* Enemy)
{
	// `0x103c3960`, `CNPC_VTzimisceRunner`'s slot 599:
	//
	//     m_hPotentialEnemy = param_1 ? param_1->GetRefEHandle() : INVALID;    // +0x6678
	//     if (RequestMeleeSlot(m_pAttackCoordinator, this)) {                  // 0x1025db70
	//         (*DAT_10924edc)->vfunc1();
	//         m_bInMelee = 1;
	//         return true;
	//     }
	//     m_bInMelee = 0;
	//     return false;
	//
	// TWO differences from `CNPC_VTzimisceHeadClaw`'s, and both are load-bearing:
	//   * the argument is CACHED first — this is the one slot-599 body in the family that reads its
	//     parameter at all, and it writes `m_hPotentialEnemy` on BOTH arms, refusal included;
	//   * the melee-must-leave timer is **not armed**. A runner that enters melee therefore has no
	//     deadline to leave it, which is a real divergence from the Troika line and from the head
	//     claw beside it, not a transcription slip — the immediates for `RandomFloat` are simply not
	//     in the body.
	RunnerPotentialEnemy = Enemy != nullptr ? Enemy->Handle : FElysiumEntityHandle();
	if (MeleeCoordinatorAdmits599())
	{
		++MeleeEventFires;
		bInMelee = true;
		return true;
	}
	bInMelee = false;
	return false;
}

bool FElysiumNpc::SpeciesSlot599(FElysiumEntity* Enemy, bool& OutAnswer)
{
	const FSpeciesSlotRow* Row = SpeciesDispatchRow(599);
	if (Row == nullptr)
	{
		return false;   // no species body: run the Troika / `CNPC_VAndreiBlood` line you have
	}
	const FSpeciesDispatchScope Scope(*this, 599);
	if (FCString::Strcmp(Row->Address, TEXT("0x10376b70")) == 0)
	{
		OutAnswer = FUN_10376b70(Enemy);
		return true;
	}
	if (FCString::Strcmp(Row->Address, TEXT("0x10379ef0")) == 0)
	{
		OutAnswer = FUN_10379ef0(Enemy);
		return true;
	}
	if (FCString::Strcmp(Row->Address, TEXT("0x103c19e0")) == 0)
	{
		OutAnswer = FUN_103c19e0(Enemy);
		return true;
	}
	if (FCString::Strcmp(Row->Address, TEXT("0x103c3960")) == 0)
	{
		OutAnswer = FUN_103c3960(Enemy);
		return true;
	}
	return false;
}

// -------------------------------------------------------------------------------------------------
// Slot 600 — the five species replacements of `FUN_102b57c0`.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::FUN_10376ba0(FElysiumEntity* Enemy)
{
	// `0x10376ba0`, `CNPC_VFrenzyShadow`'s slot 600, twenty-three bytes:
	//     m_bInMelee = 1;
	//     (*DAT_10924edc)->vfunc1();
	//     return true;
	//
	// **The write comes FIRST here and SECOND in slot 599** (`0x10376b70`). Nothing observes the
	// order — the event reaches no NPC word — but it is the bodies' own order and is kept.
	//
	// As with 599, every gate is gone: no weapon capability test, no `m_bInMelee` re-entry guard,
	// no coordinator. A frenzy shadow always accepts.
	(void)Enemy;
	bInMelee = true;
	++MeleeEventFires;
	return true;
}

bool FElysiumNpc::FUN_10379f20(FElysiumEntity* Enemy)
{
	// `0x10379f20`, `CNPC_VGargoyle`'s slot 600 — byte-identical to `0x10376ba0`.
	return FUN_10376ba0(Enemy);
}

bool FElysiumNpc::FUN_103c1a60(FElysiumEntity* Enemy)
{
	// `0x103c1a60`, `CNPC_VTzimisceHeadClaw`'s slot 600:
	//
	//     (*DAT_10924edc)->vfunc1();                                 // FIRST, unconditionally
	//     if (m_bInMelee == 0) {
	//         if (RequestMeleeSlotNoForce(m_pAttackCoordinator, this, false)) {   // 0x1025dca0
	//             m_bInMelee = 1;
	//             return true;
	//         }
	//     }
	//     m_bInMelee = 0;
	//     return false;
	//
	// Three differences from the Troika line (`0x102b57c0`):
	//   * **the event fires before anything is decided**, not inside the accepting arm — so a head
	//     claw that is already in melee still fires it;
	//   * the weapon-capability gate (`weapon->slot360() & 0x18000`) is gone entirely;
	//   * `m_flMeleeMustLeaveTimer` is NOT re-armed on the accepting arm. 29c's walk calls that "a
	//     real divergence from the base body" and it is: the head claw enters melee with no leave
	//     deadline.
	//
	// The falling-out `m_bInMelee = 0` runs for an NPC that was ALREADY in melee too, because the
	// guard is on the way in and not around the write — so calling this on a body already in melee
	// takes it back out. That is retail's, and it is exactly the shape the Troika line avoids by
	// keeping its clear inside the capability arm.
	(void)Enemy;
	++MeleeEventFires;
	if (!bInMelee)
	{
		if (MeleeCoordinatorAdmits600())
		{
			bInMelee = true;
			return true;
		}
	}
	bInMelee = false;
	return false;
}

bool FElysiumNpc::FUN_103c39e0(FElysiumEntity* Enemy)
{
	// `0x103c39e0`, `CNPC_VTzimisceRunner`'s slot 600 — `0x103c1a60` with the `m_hPotentialEnemy`
	// cache in front of it, written on both arms exactly as its slot 599 does:
	//
	//     (*DAT_10924edc)->vfunc1();
	//     m_hPotentialEnemy = param_1 ? param_1->GetRefEHandle() : INVALID;
	//     ... the head claw's body, verbatim ...
	//
	// Note the ORDER: the event fires BEFORE the cache here, where slot 599 caches first and fires
	// the event only on the accepting arm.
	++MeleeEventFires;
	RunnerPotentialEnemy = Enemy != nullptr ? Enemy->Handle : FElysiumEntityHandle();
	if (!bInMelee)
	{
		if (MeleeCoordinatorAdmits600())
		{
			bInMelee = true;
			return true;
		}
	}
	bInMelee = false;
	return false;
}

bool FElysiumNpc::FUN_103dd900(FElysiumEntity* Enemy)
{
	// `0x103dd900`, `CNPC_VYukie`'s slot 600 — and it is not a melee-entry body at all. It reuses
	// the slot for a ONE-SHOT FLEE:
	//
	//     caps = GetActiveWeapon() ? weapon->slot360() : 0;        // +0x5a0
	//     if ((caps & 0x18000) != 0 && m_bInMelee == 0) {
	//         m_bInMelee = 1;
	//         m_flMeleeMustLeaveTimer = curtime + RandomFloat(22.5, 45.0);   // +0x6074
	//         (*DAT_10924edc)->vfunc1();
	//         return true;
	//     }
	//     return false;
	//
	// It keeps the Troika line's weapon-capability gate and its `m_bInMelee` latch and DROPS the
	// coordinator entirely, then arms `+0x6074` with a window an order of magnitude longer than the
	// line's `RandomFloat(7.5, 15.0)` — 22.5 to 45 seconds, off immediates `0x41b40000` and
	// `0x42340000`. 29c's walk reads the first as 22.0; the immediate says **22.5**.
	//
	// The latch is what makes it one-shot: `m_bInMelee` is never cleared by this body, so a second
	// call before something else clears it answers false and writes nothing.
	//
	// `ActiveWeaponCapabilityWord()` is family Motor's seam over the weapon's `+0x5a0` (slot 360)
	// and answers 0, so the gate is closed today and the whole body refuses. The refusal is the
	// recovered one — retail refuses for a weapon with neither capability bit — and the arm that
	// this substrate cannot yet reach is the accepting one.
	(void)Enemy;
	const FElysiumEntity* Weapon = (World != nullptr && Inventory.ActiveWeapon.IsSet())
		? World->Resolve(Inventory.ActiveWeapon)
		: nullptr;
	const uint32 Capability = Weapon != nullptr ? ActiveWeaponCapabilityWord() : 0u;
	if ((Capability & SpeciesMeleeWeaponCapabilityBits) != 0 && !bInMelee)
	{
		bInMelee = true;
		const double Now = World != nullptr ? World->NowSeconds() : 0.0;
		MeleeMustLeaveTimer = Now + static_cast<double>(
			ElysiumRng::Stream(EElysiumRngStream::NpcSchedule)
				.FRandRange(YukieFleeWindowMin, YukieFleeWindowMax));
		++MeleeEventFires;
		return true;
	}
	return false;
}

bool FElysiumNpc::SpeciesSlot600(FElysiumEntity* Enemy, bool& OutAnswer)
{
	const FSpeciesSlotRow* Row = SpeciesDispatchRow(600);
	if (Row == nullptr)
	{
		return false;
	}
	const FSpeciesDispatchScope Scope(*this, 600);
	if (FCString::Strcmp(Row->Address, TEXT("0x10376ba0")) == 0)
	{
		OutAnswer = FUN_10376ba0(Enemy);
		return true;
	}
	if (FCString::Strcmp(Row->Address, TEXT("0x10379f20")) == 0)
	{
		OutAnswer = FUN_10379f20(Enemy);
		return true;
	}
	if (FCString::Strcmp(Row->Address, TEXT("0x103c1a60")) == 0)
	{
		OutAnswer = FUN_103c1a60(Enemy);
		return true;
	}
	if (FCString::Strcmp(Row->Address, TEXT("0x103c39e0")) == 0)
	{
		OutAnswer = FUN_103c39e0(Enemy);
		return true;
	}
	if (FCString::Strcmp(Row->Address, TEXT("0x103dd900")) == 0)
	{
		OutAnswer = FUN_103dd900(Enemy);
		return true;
	}
	return false;
}

// -------------------------------------------------------------------------------------------------
// Slot 601 — the two species replacements of `FUN_102b5880`.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::FUN_103c1ad0(FElysiumEntity* Enemy)
{
	// `0x103c1ad0`, `CNPC_VTzimisceHeadClaw`'s slot 601:
	//     (*DAT_10924edc)->vfunc1();
	//     m_bInMelee = 0;
	//     ReleaseMeleeSlot(m_pAttackCoordinator, this);      // 0x1025ddd0, UNGUARDED
	//
	// It drops the Troika line's `HasUsableRangedWeapon()` test and the `m_flMeleeCanEnterTimer`
	// re-arm that hangs off it, so a head claw that leaves melee may re-enter on the very next
	// pass. It also drops the `m_pAttackCoordinator != 0` guard the Troika line puts in front of the
	// release — the same guard family TroikaHelpers found the `CNPC_VAndreiBlood` line dropping,
	// and for the same reason: the release faults on a null coordinator, so a body that reaches it
	// already has one.
	(void)Enemy;
	++MeleeEventFires;
	bInMelee = false;
	++MeleeCoordinatorReleases;   // `thunk_FUN_1025ddd0(m_pAttackCoordinator, this)`
}

void FElysiumNpc::FUN_103c3a70(FElysiumEntity* Enemy)
{
	// `0x103c3a70`, `CNPC_VTzimisceRunner`'s slot 601 — `0x103c1ad0` with one extra write, and it
	// is not the potential-enemy CACHE the runner's 599 and 600 do but its CLEAR:
	//     (*DAT_10924edc)->vfunc1();
	//     m_hPotentialEnemy = 0xffffffff;                    // +0x6678, INVALID_EHANDLE
	//     m_bInMelee = 0;
	//     ReleaseMeleeSlot(m_pAttackCoordinator, this);
	//
	// So the runner's three melee slots are a matched set: 599 and 600 latch whoever was offered,
	// 601 forgets them. The argument is ignored on this arm, unlike the other two.
	(void)Enemy;
	++MeleeEventFires;
	RunnerPotentialEnemy = FElysiumEntityHandle();
	bInMelee = false;
	++MeleeCoordinatorReleases;
}

bool FElysiumNpc::SpeciesSlot601(FElysiumEntity* Enemy)
{
	const FSpeciesSlotRow* Row = SpeciesDispatchRow(601);
	if (Row == nullptr)
	{
		return false;
	}
	const FSpeciesDispatchScope Scope(*this, 601);
	if (FCString::Strcmp(Row->Address, TEXT("0x103c1ad0")) == 0)
	{
		FUN_103c1ad0(Enemy);
		return true;
	}
	if (FCString::Strcmp(Row->Address, TEXT("0x103c3a70")) == 0)
	{
		FUN_103c3a70(Enemy);
		return true;
	}
	return false;
}

// -------------------------------------------------------------------------------------------------
// Slot 602 — the two species replacements of `FUN_102b5900`.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::FUN_103c1b10()
{
	// `0x103c1b10`, `CNPC_VTzimisceHeadClaw`'s slot 602:
	//
	//     range = MeleeRangeConVar();                    // (*DAT_10924a1c + 4)() ? 0.0 : +0x28
	//     if (range + range < m_flEnemyDist && !CoordinatorHasRoom(m_pAttackCoordinator))  // 0x1025db50
	//         return true;
	//     return CoordinatorDoesNotHoldMe(m_pAttackCoordinator, this);                     // 0x1025de90
	//
	// Eighty-three bytes against the Troika line's far larger body, and it keeps only the FAR arm:
	// the frenzied-bit gate, the follower-boss gate, the null-coordinator gate, the
	// `HasUsableRangedWeapon()` split and the `m_flMeleeMustLeaveTimer` deadline are all gone. A
	// head claw leaves melee when it is out of double melee range and the coordinator is full, or
	// when the coordinator is not holding it — and never because a timer ran out.
	//
	// `range + range` is the DOUBLED melee range, the same doubling the Troika line uses.
	// `MeleeRangeUnits()` (family TroikaHelpers) reads the same ConVar,
	// `debug_melee_advance_combatmove_dist` "100", so the doubled range is 200 units;
	// `MeleeCoordinatorHasRoom()` answers false, so past 200 units that arm returns true.
	//
	// The last line negates `MeleeCoordinatorHoldsMe()` because that seam is spelled "is this NPC IN
	// the array" while `0x1025de90` answers "is it NOT" — family TroikaHelpers' own `Slot602` writes
	// the same `!`, and this body calls the seam the same way so the two cannot disagree. With an
	// absent coordinator the seam answers false, so this answers TRUE: nothing holds this NPC, so
	// nothing stops it leaving melee.
	const float DoubledRange = MeleeRangeUnits() + MeleeRangeUnits();
	if (DoubledRange < ScheduleHost.EnemyDistUnits && !MeleeCoordinatorHasRoom())
	{
		return true;
	}
	return !MeleeCoordinatorHoldsMe();
}

bool FElysiumNpc::FUN_103c3ab0()
{
	// `0x103c3ab0`, `CNPC_VTzimisceRunner`'s slot 602 — byte-identical to `0x103c1b10`, verified
	// against the decompiled C of both. Called rather than restated.
	return FUN_103c1b10();
}

bool FElysiumNpc::SpeciesSlot602(bool& OutAnswer)
{
	const FSpeciesSlotRow* Row = SpeciesDispatchRow(602);
	if (Row == nullptr)
	{
		return false;
	}
	const FSpeciesDispatchScope Scope(*this, 602);
	if (FCString::Strcmp(Row->Address, TEXT("0x103c1b10")) == 0)
	{
		OutAnswer = FUN_103c1b10();
		return true;
	}
	if (FCString::Strcmp(Row->Address, TEXT("0x103c3ab0")) == 0)
	{
		OutAnswer = FUN_103c3ab0();
		return true;
	}
	return false;
}

// -------------------------------------------------------------------------------------------------
// Slot 606 — `CNPC_VBach::FUN_10364280` `0x10364280`.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpc::FUN_10364280(int32 Arg)
{
	// `0x10364280`, the whole body:
	//
	//     if (!HasCondition(COND_ENEMY_OCCLUDED 0x48)) { m_bFireOccluded = 0; return 0; }
	//     if (m_bFireOccluded)  return Base606(this, param_1);      // thunk 0x102b8320
	//     m_bFireOccluded = 1;
	//     return 0;
	//
	// An ARM-THEN-FIRE gate: the first pass on which Bach sees the condition only raises its own
	// flag and refuses; the second and later passes delegate to the Troika body. The flag is cleared
	// the moment the condition drops, so the delay is re-paid every time the enemy goes behind
	// cover — it is a one-pass hysteresis, not a one-shot.
	//
	// The base `0x102b8320` is family **TroikaHelpers**' `Slot606`. It is CALLED, so Bach's gate
	// wraps the real rule rather than replacing it. Retail's answers are the Troika body's numbers;
	// `0` is its "no opinion".
	if (!Cognition.Conditions.Has(BachOccludeCondition))
	{
		bBachFireOccluded = false;
		return 0;
	}
	if (bBachFireOccluded)
	{
		return Slot606(Arg);
	}
	bBachFireOccluded = true;
	return 0;
}

bool FElysiumNpc::SpeciesSlot606(int32 Arg, int32& OutAnswer)
{
	const FSpeciesSlotRow* Row = SpeciesDispatchRow(606);
	if (Row == nullptr)
	{
		return false;
	}
	// The scope is what makes `FUN_10364280`'s own `Slot606(Arg)` retail's `thunk_FUN_102b8320` —
	// a direct call into the Troika body and not a second trip through this dispatcher.
	const FSpeciesDispatchScope Scope(*this, 606);
	if (FCString::Strcmp(Row->Address, TEXT("0x10364280")) == 0)
	{
		OutAnswer = FUN_10364280(Arg);
		return true;
	}
	return false;
}

// -------------------------------------------------------------------------------------------------
// Slot 609 — the three byte-identical state gates of `CNPC_VBach`, `CNPC_VBatSwarm` and
// `CNPC_VSheriffSwarm`.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::FUN_103661f0(bool bArg)
{
	// `0x103661f0`, forty-four bytes:
	//
	//     if (m_NPCState != 4 && m_NPCState != 0xc) { m_pShootAtHintNode = 0; return NULL; }
	//     return Base609(this, param_1);                          // thunk 0x102b6b50
	//
	// A pure STATE GATE in front of the Troika hint search: only a scripted body (4) or a body in
	// retail state 0xc may look for a shoot-at hint, and any other state has its cached hint
	// CLEARED as a side effect of asking. `EElysiumNpcState` carries no member for retail 0xc, so
	// that half of the test can never pass here and is written as the raw number with this note —
	// exactly what family Conditions did for retail state 8.
	//
	// `bArg` is the base body's own argument and is not read by the gate.
	(void)bArg;
	const int32 State = SpeciesRetailNpcState(Mind.State());
	if (State != ShootAtHintStateScript && State != ShootAtHintStateTwelve)
	{
		// `m_pShootAtHintNode = 0` (+0x6444). Zero, not `INDEX_NONE`: retail writes a NULL POINTER
		// and this runtime carries the word as a node index whose "none" is 0 — family
		// TroikaHelpers' `FindShootAtHintNode` reads it back with `!= 0` on the same convention.
		ScheduleHost.ShootAtHintNode = 0;
		return false;
	}
	return true;
}

bool FElysiumNpc::FUN_10367740(bool bArg)
{
	// `0x10367740`, `CNPC_VBatSwarm`'s — byte-identical to `0x103661f0`.
	return FUN_103661f0(bArg);
}

bool FElysiumNpc::FUN_103b26f0(bool bArg)
{
	// `0x103b26f0`, `CNPC_VSheriffSwarm`'s — byte-identical to `0x103661f0`.
	return FUN_103661f0(bArg);
}

bool FElysiumNpc::SpeciesSlot609(bool bArg, bool& OutRunBase)
{
	const FSpeciesSlotRow* Row = SpeciesDispatchRow(609);
	if (Row == nullptr)
	{
		return false;
	}
	const FSpeciesDispatchScope Scope(*this, 609);
	if (FCString::Strcmp(Row->Address, TEXT("0x103661f0")) == 0)
	{
		OutRunBase = FUN_103661f0(bArg);
		return true;
	}
	if (FCString::Strcmp(Row->Address, TEXT("0x10367740")) == 0)
	{
		OutRunBase = FUN_10367740(bArg);
		return true;
	}
	if (FCString::Strcmp(Row->Address, TEXT("0x103b26f0")) == 0)
	{
		OutRunBase = FUN_103b26f0(bArg);
		return true;
	}
	return false;
}

// -------------------------------------------------------------------------------------------------
// Slot 482 — `CanPlaySequence`'s five species copies.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpc::FUN_1035fd40(bool bDisregardState, int32 InterruptLevel)
{
	// `0x1035fd40`, `CNPC_VAnimal`'s slot 482 (and `CNPC_VDog`'s, `CNPC_VRat`'s and two more, all
	// on the one body). **Byte-identical to the base `CAI_BaseNPC::CanPlaySequence` `0x10278090`**,
	// which family **Anim** has already ported as `FElysiumNpc::CanPlaySequence`: the cine-handle
	// resolve through the global table, the `0x101a8ac0` interruptibility upgrade from 1 to 2, the
	// `IsAlive()` gate at vtable `+0x278` and the four-term state refusal are the same instructions
	// in the same order.
	//
	// So this is not a second behaviour and is not written twice. The base is CALLED, and the row
	// exists to say — with the address — that five species carry their own copy of it and that the
	// copy answers the same thing. The census check is in the suite.
	return CanPlaySequence(bDisregardState, InterruptLevel);
}

int32 FElysiumNpc::FUN_103bd270(bool bDisregardState, int32 InterruptLevel)
{
	// `0x103bd270`, `CNPC_VTzimisce`'s slot 482 — the same byte-identical copy again.
	return CanPlaySequence(bDisregardState, InterruptLevel);
}

bool FElysiumNpc::SpeciesCanPlaySequence(bool bDisregardState, int32 InterruptLevel,
	int32& OutAnswer)
{
	const FSpeciesSlotRow* Row = SpeciesDispatchRow(482);
	if (Row == nullptr)
	{
		return false;
	}
	// Both bodies below CALL `CanPlaySequence`, which is the base they are byte-identical to; the
	// scope is what stops that call coming back here, exactly as retail's direct `0x10278090` does.
	const FSpeciesDispatchScope Scope(*this, 482);
	if (FCString::Strcmp(Row->Address, TEXT("0x1035fd40")) == 0)
	{
		OutAnswer = FUN_1035fd40(bDisregardState, InterruptLevel);
		return true;
	}
	if (FCString::Strcmp(Row->Address, TEXT("0x103bd270")) == 0)
	{
		OutAnswer = FUN_103bd270(bDisregardState, InterruptLevel);
		return true;
	}
	return false;
}

// -------------------------------------------------------------------------------------------------
// Slot 588 — `CNPC_VTzimisceRunner::vfunc588` `0x103c3fd0`.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::FUN_103c3fd0()
{
	// `0x103c3fd0`, eight bytes: `RestartIdealActivity(1)` and nothing else.
	//
	// The base override (`0x10293e50`) puts `IsActivityFinished()` in front of the same call. This
	// one does not, so a runner restarts its ideal activity on EVERY call of slot 588 — mid-clip
	// included — where every other class waits for the clip to end. That is the whole of the
	// species difference.
	//
	// `RestartIdealActivityId` is family **Hints**' seam: this runtime resolves activities by name
	// and carries no retail-id table, so the id is recorded and reaches nothing. The DECISION —
	// that the restart is unconditional — is this body's and is what the suite asserts.
	RestartIdealActivityId(1);
}

bool FElysiumNpc::SpeciesSlot588()
{
	const FSpeciesSlotRow* Row = SpeciesDispatchRow(588);
	if (Row == nullptr || FCString::Strcmp(Row->Address, TEXT("0x103c3fd0")) != 0)
	{
		return false;
	}
	const FSpeciesDispatchScope Scope(*this, 588);
	FUN_103c3fd0();
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 593 — `CNPC_VTzimisce::vfunc593` `0x103b9180`.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::FUN_103b9180()
{
	// `0x103b9180`:
	//     CAI_BaseNPCTroika::Base593(this);            // thunk 0x1029a070
	//     field_0x655c = 0x3c23d70a;    //  0.009999999776482582
	//     field_0x6560 = 0x3f800000;    //  1.0
	//     field_0x6564 = 0x42480000;    // 50.0
	//     field_0x6568 = 0x42480000;    // 50.0
	//     field_0x656c = 0x3c23d70a;    //  0.009999999776482582
	//
	// The base runs FIRST and then five immediates overwrite the target-lead block it just set —
	// the base's own values are therefore unobservable on a Tzimisce, which is why the body is a
	// slot override and not a registry row.
	//
	// `+0x655c`..`+0x656c` are the five `m_flTargetLead*` words 29b declared on `FElysiumNpc`
	// (`ElysiumNpcKernelShapeMap.cpp` binds all five), and family **TroikaHelpers**'
	// `ComputeTargetLeadPoint` is the reader that blends by them. So the writes are REAL here: a
	// Tzimisce's lead point is computed from a min of 0.01, a max of 1.0, equal current and
	// predicted weights of 50 and a weight scale of 0.01.
	//
	// `Slot593()` is the generated slot, whose body is still 29c's stub for `0x1029a070`. Calling it
	// keeps the order — base first, overwrite second — so when the base lands nothing here moves.
	Slot593();
	TargetLeadMin = 0.009999999776482582f;        // +0x655c, 0x3c23d70a
	TargetLeadMax = 1.0f;                         // +0x6560, 0x3f800000
	TargetLeadCurrentWeight = 50.0f;              // +0x6564, 0x42480000
	TargetLeadPredictedWeight = 50.0f;            // +0x6568, 0x42480000
	TargetLeadWeightScale = 0.009999999776482582f;// +0x656c, 0x3c23d70a
}

bool FElysiumNpc::SpeciesSlot593()
{
	const FSpeciesSlotRow* Row = SpeciesDispatchRow(593);
	if (Row == nullptr || FCString::Strcmp(Row->Address, TEXT("0x103b9180")) != 0)
	{
		return false;
	}
	// `FUN_103b9180` runs the base FIRST (`thunk_FUN_1029a070`) and then overwrites all five words;
	// the scope makes its `Slot593()` that direct call rather than a second dispatch.
	const FSpeciesDispatchScope Scope(*this, 593);
	FUN_103b9180();
	return true;
}
