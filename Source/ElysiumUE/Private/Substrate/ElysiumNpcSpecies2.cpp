#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcSpecies2Shared.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumRng.h"
#include "ElysiumSessionSubsystem.h"
#include "ElysiumStub.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcDialogue.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"

// Story 29c-1, family **Species**, the second half — `CNPC_VAndreiBlood`, `CNPC_VChangBros`,
// `CNPC_VMingXiaoTentacle`, `CNPC_VNewscaster`, `CNPC_VTzimisce`'s carry chain,
// `CNPC_VTzimisceHeadClaw`'s slow read, `CNPC_VWerewolf`, `CNPC_VZombie` and `CNPC_VCamera`.
// Slot 323, the species slot table, its dispatchers and the melee quartet are
// `Substrate/ElysiumNpcSpecies.cpp`; the declarations are
// `Substrate/ElysiumNpcSpecies.inl`.
//
// As in the first half, every `.rdata` constant below was READ out of the pinned retail
// `vampire.dll` and the value is quoted beside its cell address. A `.data` cell filled at runtime is
// a seam and says so.

namespace
{
	// --- Retail `.rdata`, read out of the pinned image --------------------------------------------

	// `0x103e0980`'s reroll: `if (type == 4) type = RandomInt(1, 3)`.
	constexpr int32 ZombieAiTypeRerolled = 4;
	constexpr int32 ZombieAiTypeRerollMin = 1;
	constexpr int32 ZombieAiTypeRerollMax = 3;

	constexpr const TCHAR* NewscasterMainHeader = TEXT("Main Stories (%d)");
	constexpr const TCHAR* NewscasterSideHeader = TEXT("Side Stories (%d)");

}

// -------------------------------------------------------------------------------------------------
// `CNPC_VTzimisce`'s carry chain — `0x103be0b0`, `0x103be150`, `0x103be3d0`, `0x103be8e0`,
// `0x103bea90`, `0x103bef20`, `0x103bf560`.
// -------------------------------------------------------------------------------------------------

// -------------------------------------------------------------------------------------------------
// `CNPC_VZombie` — `0x103e0980`, `0x103e1080`, `0x103e12c0`, `0x103e12f0`.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::FUN_103e0980(int32 InZombieAiType)
{
	// `0x103e0980`, `CNPC_VZombie::SetZombieAIType` — the writer behind the `ZombieAIType` mapper
	// keyvalue (`+0x6678`, datamap `FIELD_INTEGER`, key `ZombieAIType`):
	//
	//     if (param_1 == 4) param_1 = RandomInt(1, 3);
	//     m_iZombieAIType = param_1;
	//     switch (param_1) { case 2: case 3: case 5: case 6: PushOrder(this, 1, false); }   // 0x102ae840
	//
	// **4 is the "pick one for me" value** and is never stored: a mapper who writes 4 gets 1, 2 or 3
	// at spawn. The reroll is `RandomInt(1, 3)`, inclusive at both ends, so 4 can produce 2 or 3 and
	// therefore reach the side effect it could not have reached directly.
	//
	// The four values that push an order are 2, 3, 5 and 6 — NOT a contiguous band, and 1 and 4 are
	// the two that do not. `0x102ae840` is family **Schedule**'s scripted-order push, ported as
	// `PushScriptedScheduleOrder`; the arguments are the literal order id 1 and a false flag.
	int32 Type = InZombieAiType;
	if (Type == ZombieAiTypeRerolled)
	{
		Type = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule)
			.RandRange(ZombieAiTypeRerollMin, ZombieAiTypeRerollMax);
	}
	ZombieAiType = Type;
	switch (Type)
	{
	case 2:
	case 3:
	case 5:
	case 6:
		// `thunk_FUN_102ae840(this, 1, '\0')`.
		Mind.ForceStateChange();
		ElysiumStub::Fired(TEXT("method"),
			TEXT("CNPC_VZombie::SetZombieAIType 0x103e0980 order push 0x102ae840"),
			DebugString(), FString::Printf(TEXT("type=%d order=1"), Type),
			TEXT("0002/29c-1: family Schedule owns the scripted-order push"));
		break;
	default:
		break;
	}
}
