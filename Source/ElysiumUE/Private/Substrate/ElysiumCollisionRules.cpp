#include "ElysiumCollisionRules.h"

ElysiumCollisionRules::FNativeDecision ElysiumCollisionRules::NativeShouldCollide(
	const FNativeBody& A, const FNativeBody& B, const FNativeRelations& Relations)
{
	//1002f1e0: reads are immutable values; each branch preserves retail precedence.
	if (!A.GameData.IsSet() || !B.GameData.IsSet()) return {true, ENativeArm::NullGameData};
	const bool bSameDomain = A.GameDataDomain == B.GameDataDomain;
	if (bSameDomain && A.GameData == B.GameData)
	{
		if ((A.GameFlags | B.GameFlags) & 0x8000) return {false, ENativeArm::SameGameDataFlag};
		if (Relations.bRawIgnored) return {false, ENativeArm::SameGameDataRaw};
		return {true, ENativeArm::SameGameDataAllow};
	}
	if ((A.GameFlags & B.GameFlags) & 2) return {false, ENativeArm::BothGameFlags};
	//1002f820: repeated94 then native1. Its group2 compare has no effect on AL.
	if ((A.CallbackFlags & 0x40) && B.MoveType != 8 && B.MoveType != 7 && !B.bStatic) return {false, ENativeArm::WheelA};
	if ((B.CallbackFlags & 0x40) && A.MoveType != 8 && A.MoveType != 7 && !A.bStatic) return {false, ENativeArm::WheelB};
	//224: every installed retail table dispatches100273b0, the constant false body.
	if (bSameDomain && A.bEdict && B.bEdict)
	{
		if (A.Owner == B.GameData) return {false, ENativeArm::OwnerA};
		if (B.Owner == A.GameData) return {false, ENativeArm::OwnerB};
	}
	if (A.bParent || B.bParent)
	{
		if (bSameDomain && A.Root == B.Root) return {false, ENativeArm::SameRoot};
		if (Relations.bRootEntityIgnored) return {false, ENativeArm::RootEntityPair};
		if (Relations.bRootRawIgnored) return {false, ENativeArm::RootRawPair};
	}
	const bool bPushOrParentA = A.MoveType == 8 || A.bParent;
	const bool bPushOrParentB = B.MoveType == 8 || B.bParent;
	if (bPushOrParentA && !B.bMoveable) return {false, ENativeArm::OtherImmovableA};
	if (bPushOrParentB && !A.bMoveable) return {false, ENativeArm::OtherImmovableB};
	if (bPushOrParentA && bPushOrParentB) return {false, ENativeArm::BothParentOrPush};
	if (!A.Solid || !B.Solid) return {false, ENativeArm::SolidNone};
	if ((A.SolidFlags | B.SolidFlags) & 4)
	{
		if (A.bTrigger && !(B.SolidFlags & 4)) return {true, ENativeArm::NotSolidTriggerA};
		if (B.bTrigger && !(A.SolidFlags & 4)) return {true, ENativeArm::NotSolidTriggerB};
		return {false, ENativeArm::NotSolid};
	}
	if ((A.SolidFlags & 8) && B.Solid != 6 && B.Solid != 1 && B.MoveType != 7) return {false, ENativeArm::TriggerA};
	if ((B.SolidFlags & 8) && A.Solid != 6 && A.Solid != 1 && A.MoveType != 7) return {false, ENativeArm::TriggerB};
	if (!ShouldCollide(A.Group, B.Group)) return {false, ENativeArm::Rules};
	if (!Relations.bContentsAIntersectsB) return {false, ENativeArm::ContentsA};
	if (!Relations.bContentsBIntersectsA) return {false, ENativeArm::ContentsB};
	if (Relations.bEntityIgnored) return {false, ENativeArm::EntityPair};
	if (Relations.bRawIgnored) return {false, ENativeArm::RawPair};
	auto Veto = [](const FNativeBody& Self, const FNativeBody& Other)
	{
		//101914a0. All L0 pair-only68 fallbacks were already tested at the entity-pair arm.
		return Self.Veto == ENativeVeto::PhysicsProp &&
			((Other.bCombatCharacter && (Self.SpawnFlags & 4)) || (Other.bPlayer && (Self.Flags2 & 1)));
	};
	if (Veto(A,B)) return {false, ENativeArm::VetoA};
	const uint8 FirstHook = A.Veto == ENativeVeto::HigherLayer ? 1 : 0;
	if (Veto(B,A)) return {false, ENativeArm::VetoB, FirstHook};
	//Explicit owned upward hooks; callback-bearing higher semantics await their layer.
	return {true, ENativeArm::Allow, static_cast<uint8>(FirstHook | (B.Veto == ENativeVeto::HigherLayer ? 2 : 0))};
}

bool ElysiumCollisionRules::SharedShouldCollide(int32 GroupA, int32 GroupB)
{
	// vampire.dll 0x1042df70: signed ordering, including negative/unrecognised groups.
	if (GroupB < GroupA)
	{
		Swap(GroupA, GroupB);
	}
	if (GroupA == 8 || GroupB == 8) return false;
	if (GroupA == 1) return false;
	if (GroupA == 4 && GroupB == 4) return false;
	return true;
}

bool ElysiumCollisionRules::ShouldCollide(int32 GroupA, int32 GroupB)
{
	// Actual single-player dispatch: CWorld::Precache 0x1023c020 -> InstallGameRules
	// 0x10352f70 -> ctor 0x10355740, CHalfLife2 table 0x104a308c slot 77 0x10356230
	// -> getter 0x1042c0a0 -> singleton 0x109eed4c -> CHLSharedGameRules 0x1042c0c0.
	// The function reads no singleton state. Remap each argument BEFORE signed ordering.
	if (GroupA == 6) GroupA = 3;
	if (GroupB == 6) GroupB = 3;
	if (GroupB < GroupA)
	{
		Swap(GroupA, GroupB);
	}
	if (GroupA == 15)
	{
		if (GroupB == 15) return false;
	}
	else if (GroupA == 13 && GroupB == 13) return false;
	if (GroupB == 17 && (GroupA == 3 || GroupA == 16 || GroupA == 17)) return false;
	if (GroupA == 18 && GroupB == 18) return false;
	return SharedShouldCollide(GroupA, GroupB); // 0x1042df70, inlined by retail 0x1042c0c0
}
