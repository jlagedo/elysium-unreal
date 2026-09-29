#include "Substrate/ElysiumNpcTestHull.h"

#include "ElysiumEntity.h"
#include "Substrate/ElysiumNpcKernelTunables.h"
#include "Substrate/ElysiumNpcUsedHullBits.h"

namespace
{
	// `CAI_TestHull::Spawn`'s constants, read out of the listing at `0x102d72f0`.
	constexpr int32 GTestHullHullCount = 22;             // `CMP EDI,0x16`
	constexpr int32 GTestHullSolidBbox = 2;              // `SOLID_BBOX`
	constexpr uint32 GTestHullSolidNotSolid = 0x4;       // `FSOLID_NOT_SOLID`
	constexpr int32 GTestHullMoveTypeFly = 4;            // `MOVETYPE_FLY`
	constexpr int32 GTestHullMoveCollideDefault = 0;     // `MOVECOLLIDE_DEFAULT`
	constexpr int32 GTestHullHealth = 0x32;              // 50
	// `0x40000` — the flag `ElysiumCameraAnimated.cpp` records beside `MakeDormant`, i.e. dormancy.
	constexpr int32 GTestHullFlagDormant = 0x40000;
}

// Slot 103: `0x102d72f0`, a replacement that does not chain to `CAI_BaseNPC::Spawn`.
void FElysiumNpcTestHull::Spawn()
{
	// 1. The hull pick, then `m_eHull` (+0x1568). The store happens at `102d7334`, before the
	//    `SetHullSizeNormal` call whose argument was already pushed.
	bool bTookFallback = false;
	HullKind = PickHull(ElysiumNpcUsedHullBits::Get(), [](int32 Hull) { return RetailHullBits(Hull); },
		bTookFallback);

	// 2. `0x10273070(this, 0)` — resize the bounds to that hull, NOT forced.
	FElysiumNpcBase::SetHullSizeNormal(false);

	// 3. `SetSolid(SOLID_BBOX = 2)` on `m_Collision` (+0x270), under a `"CBaseEntity::SetSolid"`
	//    scope-trace frame.
	RetailSolidType = GTestHullSolidBbox;
	++RetailSolidSets;

	// 4. `AddSolidFlags(word[+0x2b4] | 4)` — retail reads the CURRENT 16-bit solid-flag word,
	//    zero-extends it, ORs `FSOLID_NOT_SOLID` in and passes the WHOLE thing to `AddSolidFlags`,
	//    which ORs it again. The double-OR is retail's and is reproduced.
	const uint32 CurrentFlags = RetailSolidFlags & 0xffffu;
	RetailSolidFlags |= (CurrentFlags | GTestHullSolidNotSolid);

	// 5. `slot 93 SetMoveType(MOVETYPE_FLY = 4, MOVECOLLIDE_DEFAULT = 0)`, through the slot
	//    (`FElysiumEntity::SetMoveType` is the `m_MoveType`/`m_MoveCollide` seam).
	SetMoveType(GTestHullMoveTypeFly, GTestHullMoveCollideDefault);

	// 6. `m_iHealth (+0x210) = 0x32` — **50** — then `AddFlag(0x40000)`. The health store is at
	//    `102d7438`, after the flag's PUSH and before the call, so the health lands first.
	Health = GTestHullHealth;
	Flags |= GTestHullFlagDormant;

	// 7. `byte [+0x5f44] = 0` — this class's own first byte; what it IS stays **unrecovered**.
	bUnknown5f44 = false;

	// 8. `JMP [vtable + 0x108]` — a TAIL jump to slot 66 `Hide()`, so `Hide`'s answer is `Spawn`'s
	//    and nothing runs after it.
	Hide();
}

int32 FElysiumNpcTestHull::PickHull(int32 UsedHullBits, TFunctionRef<int32(int32)> HullBits,
	bool& bOutTookFallback)
{
	// `0x102d72f5`–`0x102d732e`, from the listing.
	bOutTookFallback = false;

	// `TEST EBX,EBX; JLE 0x102d7330` — a SIGNED test, and the target is the store with `EDI` still
	// zero. A mask of 0 OR of any value with the top bit set therefore takes hull 0 immediately and
	// never reaches the fallback call. The shipped `.data` initialiser for the mask is
	// `0xffffffff`, which is exactly such a value.
	if (UsedHullBits <= 0)
	{
		return 0;
	}

	for (int32 Index = 0; Index < GTestHullHullCount; ++Index)
	{
		if ((UsedHullBits & HullBits(Index)) != 0)
		{
			return Index;
		}
	}

	// 22 misses: `PUSH 0; CALL AddUsedHullBits; XOR EDI,EDI`. The OR of zero is a no-op and is
	// performed anyway, because the body performs it.
	ElysiumNpcUsedHullBits::Add(0);
	bOutTookFallback = true;
	return 0;
}

float FElysiumNpcTestHull::PlayActivity(const FString& Activity)
{
	// "Negative when unresolvable" (`IElysiumScheduleRunner`): a test hull has no model to play on.
	(void)Activity;
	return -1.0f;
}
