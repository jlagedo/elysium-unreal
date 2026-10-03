#include "Map/ElysiumRetailMaskRecipe.h"

#include "ElysiumCollisionChannels.h"
#include "ElysiumContentsSignature.h"
#include "Map/ElysiumMapLog.h"
#include "Misc/ScopeLock.h"

namespace ElysiumRetailMask
{
	bool IsListed(int32 RetailMask)
	{
		// The masks R1 and R2 recover at a retail trace or filter site, and the two 0018 story 8 adds.
		switch (static_cast<uint32>(RetailMask))
		{
		case 0u:
		case 0x2804091u:                 // FVisible, the cover search, the shoot node, ValidateNavGoal
		case ElysiumContents::SightMask: // 0x804091, its brush half
		case 0x4081u:
		case 0x202400bu:                 // MASK_NPCSOLID: IsValidCover, IsAreaClear, CanStandAt
		case ElysiumContents::NpcMask:   // 0x2400b: CanFitAtNode, the wander probe
		case 0x2000bu:                   // InitLinks
		case 0x201400bu:                 // MASK_PLAYERSOLID
		case ElysiumContents::PlayerMask: // 0x1400b
		case 0x46004003u:                // MASK_SHOT, the slot-68 exemption's key
		// 0018 story 8 (`docs/vtmb/npc-ai/shape.md` § "The claim primitives, the hint LOS check and
		// the idle gate"). By the rules below:
		//   0x46804099 -- the hint LOS check `0x102968f0`. OPAQUE `0x80` and no MONSTERCLIP: the
		//     SIGHT channel; MOVEABLE `0x4000`: movers met; no MONSTER: no characters listed and no
		//     entity props (`CTraceFilterHintLOS` refuses every combat character anyway). Its other
		//     bits (`0x40000000`, `0x4000000`, `0x10`, `0x8`, `0x1`) the recipe does not read; their
		//     meaning in VtMB's contents layout is UNRECOVERED.
		case 0x46804099u:
		//   0x2000000 -- the idle gate `0x102b5de0`, MONSTER alone. No sight or clip bit: the
		//     `ECC_Pawn` fallback; MONSTER: characters listed and entity props met; no movers. Retail
		//     meets no brush under it (the mask carries no brush bit); the channel the recipe
		//     falls back to DOES answer NPC-solid brushes, so a world hit the retail trace cannot
		//     produce is producible here (stated at `FElysiumNpc::HintIdleActivityGate`).
		case 0x2000000u:
			return true;
		default:
			return false;
		}
	}

	FElysiumRetailMaskRecipe Recipe(int32 RetailMask)
	{
		FElysiumRetailMaskRecipe Out;
		if (RetailMask == 0)
		{
			Out.bNothing = true;
			return Out;
		}
		if (!IsListed(RetailMask))
		{
			static FCriticalSection Guard;
			static TSet<int32> Logged;
			bool bFirst = false;
			{
				FScopeLock Lock(&Guard);
				Logged.Add(RetailMask, &bFirst);
				bFirst = !bFirst;
			}
			if (bFirst)
			{
				UE_LOG(LogElysium, Warning,
					TEXT("ElysiumRetailMask: mask 0x%x is outside the recovered set; answered by the "
						 "general rules"), static_cast<uint32>(RetailMask));
			}
		}
		const bool bMonsterClip = (RetailMask & MonsterClip) != 0;
		if (!bMonsterClip && (RetailMask & (Opaque | SightBrush)) != 0)
		{
			Out.Channel = ElysiumCollision::SightChannel;
		}
		else if (!bMonsterClip && (RetailMask & PlayerClip) != 0)
		{
			Out.Channel = ElysiumCollision::PlayerChannel;
		}
		else
		{
			// MONSTERCLIP, and the fallback: `ECC_Pawn` is what an NPC-solid brush blocks.
			Out.Channel = ECC_Pawn;
		}
		Out.bCharacters = (RetailMask & Monster) != 0;
		Out.bMovers = (RetailMask & Moveable) != 0;
		Out.bProps = Out.bCharacters;
		return Out;
	}
}
