#include "Substrate/ElysiumNpcHengeyokai.h"

#include "ElysiumAnimEvent.h"
#include "ElysiumAnimationIntent.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumStub.h"
#include "ElysiumWorldServices.h"
#include "Player/ElysiumCameraShots.h"
#include "Substrate/ElysiumDisciplines.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcAnim10Shared.h"
#include "Substrate/ElysiumNpcBossesShared.h"
#include "Substrate/ElysiumNpcConditions10Shared.h"
#include "Substrate/ElysiumNpcDebug10Shared.h"
#include "Substrate/ElysiumNpcLifecycle19_2Shared.h"
#include "Substrate/ElysiumNpcMotorShared.h"
#include "Substrate/ElysiumNpcPrecache10Shared.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcKernelTunables.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumReactions.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumRetailHullTable.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumWeaponClasses.h"
#include "Visual/ElysiumActionTables.h"

// --- File-scope helpers moved with this class's bodies (story 5 step 4) ---

namespace
{
	// The Hengeyokai's two carry activities and the Tzimisce's four body-carry variants.
	constexpr int32 GAnim10ActPickupLightIdle = 0x128;   // 296
	constexpr int32 GAnim10ActPickupLightCarry = 0x129;  // 297
	// `0x10381e90`'s grab-bone search: the initial best is 1025 units SQUARED.
	constexpr float PickupGrabBoneRangeSq = 1050625.0f;
	// `0x103822a0`'s facing cone, degrees. `_DAT_1049ae98` is the same cell the kick clamp
	// (`0x102b6890`) uses as its lower bound and `_DAT_1044eb0c` its upper — a +-20 degree cone.
	constexpr float PickupConeLo = -20.0f;         // _DAT_1049ae98
	constexpr float PickupConeHi = 20.0f;          // _DAT_1044eb0c
	// `UTIL_AngleDiff` `0x1013d580`'s two wrap bounds.
	constexpr float AngleDiffLo = -180.0f;         // _DAT_10462948
	constexpr float AngleDiffHi = ElysiumNpcTunables::OneEighty;
	constexpr float DegreesPerTurn = 360.0f;       // _DAT_10450568
	// `0x10382970`'s blacklist duration, seconds. The same 20.0 cell as the cone's upper bound;
	// `shape.md` records MingXiao blacklisting a thrown object for the same 20 s at `+0x665c`.
	constexpr float BlacklistSeconds = 20.0f;      // _DAT_1044eb0c
	// `UTIL_AngleDiff` `0x1013d580`: `a - b` walked back into `[-180, 180]` by whole turns, wrapping
	// only on the side the `a <= b` test selects. Families Facing and Positions each keep an
	// identical private copy for the same reason: neither owns the other's file.
	float BossesAngleDiff(float A, float B)
	{
		float Delta = A - B;
		if (A <= B)
		{
			while (Delta < AngleDiffLo)
			{
				Delta += DegreesPerTurn;
			}
		}
		else
		{
			while (Delta > AngleDiffHi)
			{
				Delta -= DegreesPerTurn;
			}
		}
		return Delta;
	}
	// `UTIL_VecToYaw` `0x101d2c70` over a delta in THIS world's axes, whose Y is the negated Source
	// one (`bsp.source_to_unreal`). Retail answers `0.0` for a delta whose X and Y are both zero and
	// folds a negative result up by a whole turn.
	float BossesVecToYaw(const FVector& PortDelta)
	{
		if (PortDelta.X == 0.0 && PortDelta.Y == 0.0)
		{
			return NpcKernelBossesShared::BossesZero;
		}
		float Yaw = FMath::RadiansToDegrees(
			static_cast<float>(FMath::Atan2(-PortDelta.Y, PortDelta.X)));
		if (Yaw < NpcKernelBossesShared::BossesZero)
		{
			Yaw += DegreesPerTurn;
		}
		return Yaw;
	}
	const TCHAR* const GHengeyokaiStomps[] = {   // 0x1063bd94, to 0x10
		TEXT("character/monster/hengeyokai/stomp_1.wav"),
		TEXT("character/monster/hengeyokai/stomp_2.wav"),
		TEXT("character/monster/hengeyokai/stomp_3.wav"),
		TEXT("character/monster/hengeyokai/stomp_4.wav"),
	};
	const TCHAR* const GHengeyokaiExerts[] = {   // 0x1063bda4, to 0xc
		TEXT("character/monster/hengeyokai/exert_heavy_1.wav"),
		TEXT("character/monster/hengeyokai/exert_heavy_2.wav"),
		TEXT("character/monster/hengeyokai/exert_heavy_3.wav"),
	};
	const TCHAR* const GHengeyokaiModel =
		TEXT("models/character/monster/Hengeyokai/hengeyokai.mdl");
	const TCHAR* const GHengeyokaiFreezeEmitter = TEXT("Hengeyokai_freeze_emitter");
	const TCHAR* const GHengeyokaiWeapon = TEXT("item_w_hengeyokai_fist");
}

// `CNPC_VHengeyokai`'s pickup row (0019 story 5 commit B moved it onto the class: it was a row of a
// class-keyed table read by retail class name). The release re-arms `0x102c43b0`'s collision
// ignore at 0.75 s.
const FElysiumNpc::FPickupSpecies& FElysiumNpcHengeyokai::PickupRow()
{
	static constexpr FPickupSpecies Row = { TEXT("CNPC_VHengeyokai"), TEXT("0x10382670"), TEXT("0x10382400"),
		TEXT("Bip01 R Hand"), 0x6664, 0.75f, false };
	return Row;
}

// Slot 599: `0x10381750`, `CNPC_VHengeyokai::vfunc599`, the whole body:
//     (*DAT_10924edc)->vfunc1();      // the global melee-entered event, FIRST
//     m_bInMelee = 1;                 // +0x6078
//     return true;
// Every gate of the Troika line's body is gone, as on `CNPC_VFrenzyShadow` `0x10376b70`,
// whose order it shares; this one answers `true` explicitly.
bool FElysiumNpcHengeyokai::Slot599(int32 Arg)
{
	(void)Arg;
	++MeleeEventFires;   // `(*DAT_10924edc)->vfunc1()`, family Bosses' counter for this global
	bInMelee = true;
	return true;
}

// Slot 600: `0x10381780`, the whole body: `m_bInMelee (+0x6078) = 1; (*DAT_10924edc)->vfunc1();
// return true;` -- the write FIRST here and SECOND in slot 599, as on the frenzy shadow's `0x10376ba0`.
bool FElysiumNpcHengeyokai::Slot600(FElysiumEntity* Enemy)
{
	(void)Enemy;
	bInMelee = true;
	++MeleeEventFires;
	return true;
}

bool FElysiumNpcHengeyokai::AttachPickupAnimlink(FElysiumEntity* Carried, int32 ElementKey)
{
	// `0x10382670`.
	(void)ElementKey;
	int32 Bone = INDEX_NONE;
	const FElysiumEntityHandle Link = BeginPickupLink(PickupRow(), Carried, Bone);
	if (!Link.IsSet())
	{
		return false;
	}
	// `rag = dynamic_cast<CRagdollProp*>(param_1)`: `SetHeld(true)`, the key decoded from
	// `m_SecurePickupParam`, `SetDamage((float)key)`, then the element at that key.
	const int32 Key = HengeyokaiPickupParam;
	SetCarriedRagdollHeld(Carried->Handle, true);
	if (!FinishPickupLink(Link, Bone, Carried, Key))
	{
		return false;
	}
	HengeyokaiPhysicsAnimlink = Link;
	NpcFlags.Clear(EElysiumNpcFlag::FINDING_BODY);   // 0x10381ba0(this, false)
	CallFormBit(true);                               // 0x10381c00(this, true)
	return true;
}

void FElysiumNpcHengeyokai::ReleasePickupAnimlink(const FElysiumEntity* AimTarget)
{
	// `0x10382400`: the carried word is cleared AFTER the throw and BEFORE the collision re-arm, and
	// this arm never calls `StartIgnoringCollision`.
	ReleasePickupLink(HengeyokaiPhysicsAnimlink, HengeyokaiPickupTarget, AimTarget);
	HengeyokaiPickupTarget = FElysiumEntityHandle::Invalid();
	ArmIgnoreCollisionExpiry(PickupRow().IgnoreCollisionSeconds);
	CallFormBit(false);                              // 0x10381c00(this, false)
}

// `CNPC_VHengeyokai`'s constructor `0x1037e680` writes both hull words at `0x1037e786`, after the
// `CAI_BaseNPC` constructor `0x1027c300` zeroed both; the port's constructor chain runs in the same
// order. The inverse split: a human-sized box, HENGEYOKAI pathing (`1037e786` `MOV
// [ESI+0x1568],EDI`, `1037e78c` `MOV [ESI+0x156c],0x12`).
FElysiumNpcHengeyokai::FElysiumNpcHengeyokai()
{
	HullKind = 0;
	PathingHullKind = 18;
}

// Slot 420: `0x1037fa70`.
// `0x1037fa70`
void FElysiumNpcHengeyokai::NPCInit()
{
	TroikaNPCInit();
	HengeyokaiPathMode = 0;                                                        // +0x6680
	HengeyokaiPickupTarget = FElysiumEntityHandle::Invalid();            // +0x6664
	HengeyokaiShunnedFindFish = 0;
	bHengeyokaiJustFoundFish = false;
	bHengeyokaiInSharkForm = false;
	HengeyokaiShunnedFishTimer = NpcKernelLifecycle19_2Shared::Lifecycle19_2Now(*this) + SpeciesShunWindowSeconds;
	NodeGraphHullIndex() = HullIndexHengeyokai;
}

// Slot 104: `0x1037f960`.
// 0x1037f960
void FElysiumNpcHengeyokai::Precache()
{
	// `CNPC_VHengeyokai::Precache` `0x1037f960` — the Troika body, the 0x10-byte stomp table, the
	// 0xc-byte exert table, its model with preload 0, the freeze emitter with preload **0** rather
	// than the 1 Andrei, Chang and the ManBat use, and the fist.
	TroikaPrecache();
	NpcKernelPrecache10Shared::Precache10SoundTable(*this, GHengeyokaiStomps, UE_ARRAY_COUNT(GHengeyokaiStomps));
	NpcKernelPrecache10Shared::Precache10SoundTable(*this, GHengeyokaiExerts, UE_ARRAY_COUNT(GHengeyokaiExerts));
	NpcKernelPrecache10Shared::Precache10Model(*this, GHengeyokaiModel, /*Preload=*/0);
	NpcKernelPrecache10Shared::Precache10Particle(*this, GHengeyokaiFreezeEmitter, /*Preload=*/0);
	NpcKernelPrecache10Shared::Precache10Other(*this, GHengeyokaiWeapon);
}

// Slot 375: `0x10381b50`, which calls the human line's `0x103854f0` directly.
/** `CNPC_VHengeyokai::NPC_EarlyTranslateActivity` (`0x10381b50`). Under the carry-form bit, request
 *  1 returns `0x128` and 9 or `0x13` return `0x129`, each immediately; everything else tail-calls
 *  the HUMAN body `0x103854f0`, which itself chains the Troika one. */
int32 FElysiumNpcHengeyokai::NPC_EarlyTranslateActivity(int32 Activity)
{
	// `CNPC_VHengeyokai::NPC_EarlyTranslateActivity` `0x10381b50`, 61 bytes.
	if (HengeyokaiCarryFormBit())
	{
		if (Activity == NpcKernelAnim10Shared::GAnim10ActIdle)
		{
			return GAnim10ActPickupLightIdle;
		}
		if (Activity == NpcKernelAnim10Shared::GAnim10ActWalk || Activity == NpcKernelAnim10Shared::GAnim10ActRun)
		{
			return GAnim10ActPickupLightCarry;
		}
	}
	// Every other request, and the whole bit-clear case, tail-calls the HUMAN body — not the Troika
	// one — which itself chains the Troika pre-translate.
	return HumanNpcEarlyTranslateActivity(Activity);
}

// Slot 461: `0x10380100`, the selector tag 0x13 and then a direct call into the human line's `0x103851e0`.
int32 FElysiumNpcHengeyokai::SelectIdealStateRetail()
{
	SelectIdealStateSelector = 0x13;
	return HumanSelectIdealState();
}

// Slot 448: `0x10380510`, its own arm and then a direct call into the Troika body `0x1029adb0`.
/** `CNPC_VHengeyokai::TaskFail` (`0x10380510`) — the memory-bit clear, then the blacklist-and-drop
 *  pair on `FINDING_BODY`, then the ignore-collision re-arm on `!CARRYING_BODY`, then
 *  `m_iShunnedFindFish` (`+0x6678`) = 0. */
void FElysiumNpcHengeyokai::TaskFail(int32 Reason)
{
	// `CNPC_VHengeyokai::TaskFail` (`0x10380510`), 130 bytes.
	(void)Reason;

	if (NpcKernelConditions10Shared::Cond10RetailNpcState(Mind.State()) == NpcKernelConditions10Shared::GCond10NpcStateCombat
		&& ElysiumSchedule::HasInterruptCondition(Schedule, *this, Cognition.Conditions,
			EElysiumNpcCond::TaskFailed))
	{
		BaseScheduleHost.MemoryBits &= ~NpcKernelConditions10Shared::GCond10MemoryTopBit;                 // 10380536
	}

	// `10380548`: `0x10381be0` is `(m_bfAINPCFlags >> 4) & 1` — `FINDING_BODY`, NOT a species word.
	if (NpcFlags.Has(EElysiumNpcFlag::FINDING_BODY))
	{
		BlacklistPickupTarget(HengeyokaiPickupTarget);                      // 0x10382970
		NpcFlags.Clear(EElysiumNpcFlag::FINDING_BODY);                   // 0x10381ba0(this, 0)
	}

	// `1038057c`: `0x10381c80` is `(m_bfAINPCFlags >> 5) & 1` — `CARRYING_BODY`. The arm runs when
	// it is CLEAR.
	if (!NpcFlags.Has(EElysiumNpcFlag::CARRYING_BODY))
	{
		SetIgnoreCollisionExpiry(NpcKernelConditions10Shared::GCond10PickupReuseDelay);               // 0x102c43b0(this, 0.75)
		HengeyokaiPickupTarget = FElysiumEntityHandle::Invalid();           // m_hPickupTarget = -1
	}

	HengeyokaiShunnedFindFish = 0;                                         // m_iShunnedFindFish +0x6678
	FElysiumNpc::TaskFail(Reason);
}

// Slot 440: `0x1037ffa0`.
// `0x1037ffa0`
// `0x1037ffa0`, `CNPC_VHengeyokai::TranslateSchedule`, the body of `FElysiumNpcHengeyokai::TranslateScheduleRetail`
// (story 5 step 3). Every miss is a direct call into the Troika body `0x102b12f0`.
int32 FElysiumNpcHengeyokai::TranslateScheduleRetail(int32 ScheduleNumber)
{
	if (ScheduleNumber != 0x16e && HengeyokaiSkin == 1)
	{
		++HengeyokaiThawCalls;
	}
	if (ScheduleNumber == 0x5b) { return 0x15a; }
	if (ScheduleNumber == 0xdc) { return 0x15c; }
	if (ScheduleNumber == 0xdd) { return 0x15d; }
	if (ScheduleNumber == 0xea) { return 0x15b; }
	return TroikaTranslateScheduleRetail(ScheduleNumber);
}

// Slot 69: `0x10380f90` (byte-identical across Hengeyokai, MingXiao and Tzimisce): the `0x16` derived-type
// gate, then a direct call into the Troika body `0x1029b180`.
bool FElysiumNpcHengeyokai::NavIgnoreCollision(FElysiumEntity* Other)
{
	if (Other != nullptr && (RetailDerivedType(*Other) & 0x16) != 0)
	{
		return true;
	}
	return FElysiumNpc::NavIgnoreCollision(Other);
}

// Slot 124: `0x10383560`
/** `CNPC_VHengeyokai::DrawDebugTextOverlays` (`0x10383560`) — the Troika body, then slot 9's string
 *  on one further line under bit 0. */
int32 FElysiumNpcHengeyokai::DrawDebugTextOverlays()
{
	// `0x10383560`, 106 bytes: the Troika body, then slot 9's string on one further line under bit 0.
	// The string is taken from slot 9's returned object's first word and the empty string
	// (`DAT_106b8540`) substitutes for a null one; retail prints it with NO format string at all.
	// The `+1` IS the contract: it is the budget every later overlay consumes.
	const int32 Base = TroikaDrawDebugTextOverlays();
	if ((DebugOverlays & NpcKernelDebug10Shared::GDebug10BitText) == 0)
	{
		return Base;
	}
	const FString Slot9 = HengeyokaiSlot9String();
	EmitEntityText(Base, TEXT("0x10383560"), Slot9);
	return Base + 1;
}

// Slot 337: `0x1037fb20`.
int32 FElysiumNpcHengeyokai::GetUsedHullBits()
{
	// A bare `return 0x40001`: no call up the chain, so the Troika line's bit 0 is absent.
	return 0x40001;
}

// Slot 546: `0x1037ea60`, the class's own schedule id space.
const TCHAR* FElysiumNpcHengeyokai::SquadSlotName(int32 SlotEn)
{
	// The class's `CAI_ClassScheduleIdSpace` `0x1093b27c`, left empty by `0x102ea090(isRoot = false)`:
	// `SlotEn` translates to -1 and names `<<null>>`.
	static constexpr FSquadSlotSpecies IdSpace = {
		TEXT("CNPC_VHengeyokai"), TEXT("0x1037ea60"), TEXT("0x1093b27c") };
	return GlobalSquadSlotName(SquadSlotLocalToGlobal(&IdSpace, SlotEn));
}

// Slot 259: `0x1037fb60`, the footstep body of `docs/vtmb/footsteps.md` §1.7; an id it does not
// claim is a direct call into the base body.
bool FElysiumNpcHengeyokai::HandleAnimEvent(const FElysiumAnimEvent& Event)
{
	return SpeciesFootstepAnimEvent(TEXT("npc_VHengeyokai"), Event);
}

// Slot 292: `0x103802a0` — no flinch from gunfire or a zero-magnitude hit; otherwise the base
// `CBaseCombatCharacter::DamageFlinch`. The port's flinch runs through `StartDamageFlinch`, whose
// species hook this is.
bool FElysiumNpcHengeyokai::SuppressesDamageFlinch(const FElysiumDmg& Dmg) const
{
	// `0x103802a0`, byte-identical with its sibling's: the port's commit path hands the flinch a
	// resolved descriptor rather than a packet, so the combined bits are the descriptor's own and the
	// magnitude is the committed damage — which is what `CVDmg_t::GetDmg` answers once `Apply` has
	// run. `0x4000002` is `DMG_BULLET | DMG_BUCKSHOT`, exactly `ElysiumDamage::FirearmMask`.
	return DamageFlinchSuppressed(Dmg.DmgMask, static_cast<float>(Dmg.GetDmg()),
		ElysiumDamage::FirearmMask);
}

// Slot 435: `0x10383090`, the Troika body `0x102a0940` directly, then the class's own tail.
// `0x10383090`
void FElysiumNpcHengeyokai::OnScheduleChange(int32 NewSchedule)
{
	// `CNPC_VHengeyokai::OnScheduleChange` `0x10383090`: the Troika body directly, first.
	FElysiumNpc::OnScheduleChange(NewSchedule);
	if (!NpcFlags.Has(EElysiumNpcFlag::PRESERVE_PATH)) // 0x103830a5
	{
		HengeyokaiPathMode = 0;					 // 0x103830b0, +0x6680
		if (HengeyokaiShunnedFindFish > 0) // 0x103830ba
		{
			--HengeyokaiShunnedFindFish; // 0x103830be
		}
	}
}

// --- Moved from `ElysiumNpcAnim10.cpp` (story 5 step 4) ---

bool FElysiumNpcHengeyokai::HengeyokaiCarryFormBit() const
{
	// `thunk_FUN_10381c80(this)` — `m_bfAINPCFlags` (`+0x14b8`) bit 5, `0x20 CARRYING_BODY`.
	return NpcFlags.Has(EElysiumNpcFlag::CARRYING_BODY);
}

// --- Moved from `ElysiumNpcBosses.cpp` (story 5 step 4) ---

void FElysiumNpcHengeyokai::SetCarriedRagdollHeld(const FElysiumEntityHandle& Carried, bool bHeld)
{
	// SEAM for `thunk_FUN_10157890(ragdoll, b)`, which `0x10382670` sets before it reads the element.
	(void)Carried;
	(void)bHeld;
}

const TCHAR* const* FElysiumNpcHengeyokai::PickupGrabBoneNames()
{
	// `PTR_s_Bone01_1063bd88`, read from the image: two `char*` rows followed by the shared empty
	// string `DAT_106b8540`, which is what stops retail's walk (its condition is the FIRST CHARACTER
	// of the next row, not a null pointer).
	static const TCHAR* const Names[] = { TEXT("Bone01"), TEXT("Bone04"), nullptr };
	return Names;
}

bool FElysiumNpcHengeyokai::FindPickupTargetGrabBone(const FElysiumEntity* InTarget)
{
	// `0x10381e90`, arm for arm.
	//
	//     bool found = false;  float best = 1050625.0;  int index = 0;
	//     CRagdollProp* rag = dynamic_cast<...>(target);
	//     if (rag == NULL) {
	//         m_vecPickupTargetPos = target->GetOrigin();     // slot 220 (+0x370)
	//         m_iPickupTargetGrabBone = 0;
	//         return true;
	//     }
	//     for (name : { "Bone01", "Bone04" }) {
	//         element = rag->GetElement(name);                 // +0x424
	//         if (!element) { ++index; continue; }             // the index still advances
	//         element->GetPosition(&pos, &angles);             // +0x94
	//         d2 = (GetOrigin() - pos).LengthSqr();            // slot 220, re-read per bone
	//         if (d2 < best) { m_vecPickupTargetPos = pos; found = true;
	//                          m_iPickupTargetGrabBone = index; best = d2; }
	//         ++index;
	//     }
	//     return found;
	//
	// Retail does NOT clamp the best distance when the cast fails — the fallback arm writes bone 0
	// and answers true unconditionally, and that is the arm this substrate always takes because
	// `RagdollBonePosition` is a seam. Both arms are ported; only one is reachable today.
	if (InTarget == nullptr)
	{
		// Retail dereferences `param_1` through `__RTDynamicCast`, which answers NULL for a null
		// pointer, so a null target lands on the fallback arm and then dereferences the target's
		// vtable. This port refuses instead — the one divergence, and it is a crash retail would
		// take, not a behaviour a shipped program could have been tuned against.
		return false;
	}

	bool bFound = false;
	float Best = PickupGrabBoneRangeSq;
	int32 Index = 0;
	FVector BonePositionUnits = FVector::ZeroVector;
	const TCHAR* const* Names = PickupGrabBoneNames();
	// The RTTI cast: `RagdollBonePosition` answers false for every bone when the target is not a
	// ragdoll, which is retail's null-cast arm — but retail decides ONCE, before the walk, so the
	// fallback is taken only when the cast itself failed.
	bool bAnyElement = false;
	for (int32 i = 0; Names[i] != nullptr; ++i)
	{
		if (RagdollBonePosition(InTarget, Names[i], BonePositionUnits))
		{
			bAnyElement = true;
			const FVector MineUnits = Origin / ElysiumMove::U;
			const float DistSq = static_cast<float>((MineUnits - BonePositionUnits).SizeSquared());
			if (DistSq < Best)
			{
				HengeyokaiPickupTargetPos = BonePositionUnits;
				bFound = true;
				HengeyokaiPickupTargetGrabBone = Index;
				Best = DistSq;
			}
		}
		++Index;
	}
	if (bAnyElement)
	{
		return bFound;
	}
	// The cast failed: the target's own origin, bone 0, and true.
	HengeyokaiPickupTargetPos = InTarget->Origin / ElysiumMove::U;
	HengeyokaiPickupTargetGrabBone = 0;
	return true;
}

bool FElysiumNpcHengeyokai::WithinPickupFacingCone(const FVector& Delta, float YawDegrees)
{
	// `0x103822a0`'s tail, read from the listing because the decompiler turned the two `FCOMP`s into
	// status-word arithmetic:
	//     1038237b  FLD  [0x1049ae98] ; FCOMP ; JP  -> AL = 0     (const  >  diff)
	//     1038238d  FCOMP [0x1044eb0c] ; JP        -> AL = 0     (diff   >  const)
	//     1038239a  MOV AL, 1
	// so the answer is `-20.0 <= AngleDiff(VecToYaw(delta), yaw) <= 20.0`, inclusive on both edges.
	const float Diff = BossesAngleDiff(BossesVecToYaw(Delta), YawDegrees);
	if (PickupConeLo > Diff)
	{
		return false;
	}
	return !(Diff > PickupConeHi);
}

bool FElysiumNpcHengeyokai::FUN_103822a0(const FElysiumEntity* InTarget) const
{
	// `0x103822a0`'s head, in retail's order. Every early-out answers TRUE (`MOV AL, 1`):
	//     if (param_1 == NULL) return true;
	//     if (!m_hPickupTarget.IsValid()) return true;          // +0x6664, the EHANDLE serial test
	//     delta = param_1->GetOrigin() - GetOrigin();            // slot 220, BOTH sides
	//     return -20 <= AngleDiff(VecToYaw(delta), GetAngles().y) <= 20;   // slot 221's yaw
	if (InTarget == nullptr)
	{
		return true;
	}
	if (World == nullptr || World->Resolve(HengeyokaiPickupTarget) == nullptr)
	{
		return true;
	}
	const FVector Delta = InTarget->Origin - Origin;
	return WithinPickupFacingCone(Delta, static_cast<float>(Angles.Y));
}

void FElysiumNpcHengeyokai::AddBlacklistedEntity(const FElysiumEntity* Entity)
{
	// `0x10382970`. The observable half, in retail's order:
	//     expiry = gpGlobals->curtime + 20.0;               // _DAT_1044eb0c
	//     <CUtlVector grow: 4 on an empty store, else double, else + m_nGrowSize>
	//     m_pElements = m_pMemory;  ++m_Size;
	//     memmove(base + (old+1)*8, base + old*8, ((m_Size - old) - 1) * 8);   // always 0 bytes
	//     base[old] = { param_1, expiry };
	// The `memmove` length is `(old + 1 - old) - 1`, which is zero on every path, so the append is
	// the whole of it. The grow arithmetic and `m_pElements` are `CUtlMemory` bookkeeping with no
	// observable effect on a `TArray` and are deliberately not reproduced.
	//
	// Retail stores the EHANDLE, not the pointer, and stamps the expiry whether or not the entity is
	// already in the store — a second add is a second row, not a refresh.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	FBlacklistedEntity Row;
	Row.Entity = Entity != nullptr ? Entity->Handle : FElysiumEntityHandle::Invalid();
	Row.ExpiresAt = Now + BlacklistSeconds;
	HengeyokaiBlacklist.Add(Row);
}

int32 FElysiumNpcHengeyokai::FindBlacklistedEntity(const FElysiumEntity* Entity) const
{
	// `0x10382b30`: walk the store, resolve each row's EHANDLE and compare the POINTER against the
	// candidate. A row whose handle no longer resolves compares as NULL, so a null candidate matches
	// the first dead row — retail's behaviour, and reproduced.
	if (World == nullptr)
	{
		return INDEX_NONE;
	}
	for (int32 i = 0; i < HengeyokaiBlacklist.Num(); ++i)
	{
		const FElysiumEntity* Stored = World->Resolve(HengeyokaiBlacklist[i].Entity);
		if (Stored == Entity)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

bool FElysiumNpcHengeyokai::BlacklistTestAndExpire(TArray<FBlacklistedEntity>& Store, int32 Index, double Now)
{
	// `0x10382aa0`'s tail, and `0x10366400`'s — the two are byte-for-byte the same body over two
	// different stores:
	//     if (index == -1) return false;
	//     if (curtime < store[index].expiry) return true;
	//     if (m_Size > 0) { memmove(&store[index], &store[m_Size - 1], 8); --m_Size; }
	//     return false;
	// The expire arm is a SWAP-REMOVE with the last row, not an ordered erase, and the `m_Size > 0`
	// guard is dead (the index came from a walk of that same size).
	if (Index == INDEX_NONE)
	{
		return false;
	}
	if (Now < Store[Index].ExpiresAt)
	{
		return true;
	}
	if (Store.Num() > 0)
	{
		Store[Index] = Store[Store.Num() - 1];
		Store.RemoveAt(Store.Num() - 1, EAllowShrinking::No);
	}
	return false;
}

bool FElysiumNpcHengeyokai::IsEntityBlacklisted(const FElysiumEntity* Entity)
{
	// `0x10382aa0`.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	return BlacklistTestAndExpire(HengeyokaiBlacklist, FindBlacklistedEntity(Entity), Now);
}

// --- Moved from `ElysiumNpcConditions10.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcDebug10.cpp` (story 5 step 4) ---

// -------------------------------------------------------------------------------------------------
// The species arms of slot 124.
// -------------------------------------------------------------------------------------------------

FString FElysiumNpcHengeyokai::HengeyokaiSlot9String() const
{
	// SEAM for slot 9's string, which `CNPC_VHengeyokai#124` prints verbatim. Slot 9 is a generated
	// stub owned by another story; the empty string is retail's null arm (`DAT_106b8540`).
	return FString();
}

// --- Moved from `ElysiumNpcLifecycle19_2.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcMaintain19.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcMisc.cpp` (story 5 step 4) ---

bool FElysiumNpcHengeyokai::FormBitTimerExpired() const
{
	// `0x10381ca0` — the whole body is `return m_flFishTimer (+0x666c) <= gpGlobals->curtime`. The
	// read half of the pair above; `0x10381c00` is the only writer of `+0x666c` this story found.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	return static_cast<double>(HengeyokaiFishTimer) <= Now;
}

// --- Moved from `ElysiumNpcMotor.cpp` (story 5 step 4) ---

void FElysiumNpcHengeyokai::MotorCancelLinkFacing()
{
	// `thunk_FUN_102e1e20(m_pMotor, -1)` — `FUN_10382d20`'s whole body. **SEAM**: the Facing family
	// established that this mover keeps no facing queue (`m_facingQueue`, motor+0x54), so the cancel
	// is recorded and cancels nothing.
	++MotorSeams.LinkFacingCancels;
}

void FElysiumNpcHengeyokai::ClearLinkActivity()
{
	// `FUN_10382d20` `0x10382d20`: `thunk_FUN_102e1e20(m_pMotor, -1)`.
	MotorCancelLinkFacing();
}

// --- Moved from `ElysiumNpcPrecache10.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcTranslate19.cpp` (story 5 step 4) ---


// --- Story 8, lane L12: Boss19 `0x103830e0` ----------------------------------------------------

void FElysiumNpcHengeyokai::HengeyokaiEnterMorph()
{
	// `0x103830e0` (`FElysiumNpc::HengeyokaiEnterMorph` in the checklist), 55 bytes, no branch. The
	// order is the rule: the morph program is installed BEFORE the skin flips, and the fade time is
	// written BEFORE the target skin, so both land on the same think and the swap is instant.
	// `+0x1b30`/`+0x1b34` := `NPC_VHengeyokai.cpp`, 0x985 — absent in the shape map; recorded.
	RecordScheduleEvent(TEXT("EnterMorph trace NPC_VHengeyokai.cpp:2437"));  // 0x103830ea / 0x103830f4
	// `0x16e`, the morph program — the same class-local id this class's `TranslateScheduleRetail`
	// gates its skin test on above.
	constexpr int32 MorphSchedule = 0x16e;
	SetSchedule(MorphSchedule, false);                                       // 0x103830e5 / 0x103830fe 0x102ae750
	// `CBaseAnimating::SetSkinFadeTime(0.0)` (`0x1008d5f0`): a time at or below `_DAT_1044fab0`
	// (0.01, a DOUBLE) stores the floor, so the fade time becomes 0.01, not 0.
	constexpr float SkinFadeTime = 0.0f;
	HengeyokaiSkinCrossfadeTime = static_cast<float>(ElysiumNpcTunables::HundredthDouble) < SkinFadeTime
		? SkinFadeTime : static_cast<float>(ElysiumNpcTunables::HundredthDouble);   // 0x10383107
	// `CBaseAnimating::FadeToSkin(1)` (`0x1008d6d0`): only on a real change, `DevMsg`, then the old
	// skin into `m_nSkinCrossfade` and the new into `m_nSkin`.
	constexpr int32 MorphSkin = 1;
	if (HengeyokaiSkin != MorphSkin)                                         // 0x10383110
	{
		UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("Fading to skin %d over %.2f"), MorphSkin,
			HengeyokaiSkinCrossfadeTime);
		HengeyokaiSkinCrossfade = HengeyokaiSkin;
		HengeyokaiSkin = MorphSkin;
	}
}
