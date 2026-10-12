#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntityHandle.h"

// Stateless retail rules: arguments are collision-group words, not Unreal channels.
namespace ElysiumCollisionRules
{
	// Immutable policies, selected on GT. L1+ owns the unrecovered virtual68 overrides.
	enum class ENativeVeto : uint8 { Base, EntityPairs, PhysicsProp, HigherLayer };
	struct FNativeBody
	{
		const void* GameDataDomain = nullptr; //Equality token only; never dereferenced on PT.
		FElysiumEntityHandle GameData, Owner, Root;
		uint16 GameFlags = 0, CallbackFlags = 0x1127, SolidFlags = 0;
		uint32 Contents = 1, SolidMask = 0x200400b, SpawnFlags = 0, Flags2 = 0;
		int32 Solid = 0, MoveType = 0, Group = 0;
		bool bEdict = false, bParent = false, bStatic = false, bMoveable = false, bTrigger = false;
		bool bCombatCharacter = false, bPlayer = false;
		ENativeVeto Veto = ENativeVeto::Base;
	};
	struct FNativeRelations
	{
		bool bEntityIgnored = false, bRawIgnored = false, bRootEntityIgnored = false, bRootRawIgnored = false;
		bool bContentsAIntersectsB = true, bContentsBIntersectsA = true;
	};
	enum class ENativeArm : uint8
	{
		NullGameData, SameGameDataFlag, SameGameDataRaw, SameGameDataAllow, BothGameFlags,
		WheelA, WheelB, OwnerA, OwnerB, SameRoot, RootEntityPair, RootRawPair,
		OtherImmovableA, OtherImmovableB, BothParentOrPush, SolidNone,
		NotSolidTriggerA, NotSolidTriggerB, NotSolid, TriggerA, TriggerB, Rules,
		ContentsA, ContentsB, EntityPair, RawPair, VetoA, VetoB, Allow
	};
	struct FNativeDecision { bool bAllow; ENativeArm Arm; uint8 HigherVetoHooks = 0; };
	//1002f1e0, evaluated only for real native candidates. No gameplay callbacks.
	ELYSIUMUE_API FNativeDecision NativeShouldCollide(const FNativeBody& A, const FNativeBody& B, const FNativeRelations& Relations);
	// CSharedGameRules::vfunc0, vampire.dll 0x1042df70.
	ELYSIUMUE_API bool SharedShouldCollide(int32 GroupA, int32 GroupB);
	// CHalfLife2 slot 77 -> CHLSharedGameRules::vfunc0, vampire.dll 0x1042c0c0.
	ELYSIUMUE_API bool ShouldCollide(int32 GroupA, int32 GroupB);
}
