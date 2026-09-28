#include "Substrate/ElysiumNpcAndreiBlood.h"

#include "ElysiumDecalSubsystem.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSessionSubsystem.h"
#include "ElysiumSkeletalBasis.h"
#include "ElysiumStub.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumLaw.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcDialogue.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcDamage2Shared.h"
#include "Substrate/ElysiumNpcDamageShared.h"
#include "Substrate/ElysiumNpcFacingShared.h"
#include "Substrate/ElysiumNpcLifecycle19_2Shared.h"
#include "Substrate/ElysiumNpcPositionsShared.h"
#include "Substrate/ElysiumNpcPrecache10Shared.h"
#include "Substrate/ElysiumNpcScheduleShared.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcSpecies2Shared.h"
#include "Substrate/ElysiumNpcSpeciesLifecycle10Shared.h"
#include "Substrate/ElysiumNpcState19Shared.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumReactions.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumWeaponClasses.h"

// --- File-scope helpers moved with this class's bodies (story 5 step 4) ---

namespace
{
	// `CNPC_VAndreiBlood::SelectIdealState`'s trace tag. The base writes 1, `CNPC_VAnimal` 5,
	// `CNPC_VHengeyokai` 0x13 and `CNPC_VHunter` 0x17 (`docs/vtmb/npc-kernel/layout.md` +0x1b38).
	constexpr int32 AndreiIdealStateSelector = 4;
	// The four `PositionClearForTeleport` last-teleport floors, one per species.
	constexpr float AndreiLastTeleportFloor = 100.0f;     // DAT_104a6f7c
	constexpr int32 HintTeleport17001 = 0x4269;
	//
	// The three emitters carry a HYPHEN before `Emitter`, not the underscore the checklist's walk
	// spells: `0x1062b04c`, `0x1062b02c`, `0x1062b010`, each also named by
	// `CNPC_VAndreiBlood::StartTask`.
	const TCHAR* const GAndreiTeleportOutSound = TEXT("Character/Boss/Andrei/TeleportOut.wav");
	const TCHAR* const GAndreiTeleportInSound = TEXT("Character/Boss/Andrei/TeleportIn.wav");
	const TCHAR* const GAndreiSummonSound = TEXT("Character/Boss/Andrei/Summon.wav");
	const TCHAR* const GAndreiTeleportOutEmitter = TEXT("Andrei_Teleport_Out-Emitter");
	const TCHAR* const GAndreiTeleportInEmitter = TEXT("Andrei_Teleport_In-Emitter");
	const TCHAR* const GAndreiSummonEmitter = TEXT("Andrei_Summon-Emitter");
	// `CNPC_VAndreiBlood`'s runner cap, `_DAT_10452dc4`. TWO bodies threshold on it and both read
	// it as a float against `m_iActiveRunnerCount`: `0x1035e920` (`count < 2`) and `0x1034c2d0`
	// (`2 <= count` refuses).
	constexpr int32 AndreiMaxActiveRunners = 2;      // _DAT_10452dc4 = 2.0f
	// `0x1035e950`'s draw — `(*DAT_1070b244 + 8)(2, 4)` is `IUniformRandomStream::RandomInt`,
	// inclusive at both ends, the same object and slot family Sounds and Damage read.
	constexpr int32 AndreiHitMaxMin = 2;
	constexpr int32 AndreiHitMaxMax = 4;
}

// Slot 420: `0x1035cec0`.
// `0x1035cec0`
void FElysiumNpcAndreiBlood::NPCInit()
{
	VampireBossNPCInit();                                                // 1035cecx
	AndreiLastTeleportPosition = FVector::ZeroVector;                    // DAT_1070d1b0 origin
}

// Slot 104: `0x1035cb90`.
// 0x1035cb90
void FElysiumNpcAndreiBlood::Precache()
{
	// `CNPC_VAndreiBlood::Precache` `0x1035cb90` — the Troika body FIRST, then three sounds and
	// three preload-1 emitters. The sound-before-emitter split and the preload flag are the data.
	TroikaPrecache();
	NpcKernelPrecache10Shared::Precache10Sound(*this, GAndreiTeleportOutSound);
	NpcKernelPrecache10Shared::Precache10Sound(*this, GAndreiTeleportInSound);
	NpcKernelPrecache10Shared::Precache10Sound(*this, GAndreiSummonSound);
	NpcKernelPrecache10Shared::Precache10Particle(*this, GAndreiTeleportOutEmitter, /*Preload=*/1);
	NpcKernelPrecache10Shared::Precache10Particle(*this, GAndreiTeleportInEmitter, /*Preload=*/1);
	NpcKernelPrecache10Shared::Precache10Particle(*this, GAndreiSummonEmitter, /*Preload=*/1);
}

// Slot 127: `0x1035cf80`.
/** `CNPC_VAndreiBlood::Restore` (`0x1035cf80`), `CNPC_VAndreiBlood#127`. A scope-trace push, a bare
 *  `CNPC_VVampireBoss::Restore(archive)` and a pop: **no restore-time datum of its own**. Every
 *  sibling boss writes something here and Andrei writes nothing, which is a fact rather than an
 *  unwalked body — the listing has exactly one `CALL` between the frame pushes (`1035cfd4`) and the
 *  base's `EAX` survives to the `RET 0x4` unchanged, so the answer is the base's too. */
int32 FElysiumNpcAndreiBlood::Restore(void* Archive)
{
	// `CNPC_VAndreiBlood::Restore` `0x1035cf80`. One call between the scope-frame push and pop, and
	// its `EAX` survives to the `RET 0x4`: the base's answer IS this body's answer, and there is no
	// restore-time datum of its own. An empty species row is a fact, not an unwalked body.
	return VampireBossRestore(Archive);
}

// Slot 461: `CNPC_VAndreiBlood::vfunc461`, whose typed answer is written back as retail 2 or 1.
/** The species SelectIdealState bodies, each the `SelectIdealStateRetail` override of its class. */
int32 FElysiumNpcAndreiBlood::SelectIdealStateRetail()
{
	// `CNPC_VAndreiBlood::vfunc461`'s typed answer (family Damage's `CNPC_VAndreiBlood_vfunc461`),
	// written back as retail 2 or 1 and answered: the body of `FElysiumNpcAndreiBlood`'s override.
	const EElysiumNpcState Andrei = CNPC_VAndreiBlood_vfunc461();
	Mind.WriteIdealStateRetail(Andrei == EElysiumNpcState::Alert ? 2 : 1);
	return IdealStateRetail();
}

// Slot 438: `0x1035d010`, which replaces the whole selector (the Troika selector's species hook).
// Slot 438: `0x1035d010`, the body of its class's `SpeciesSelectSchedule` override (story 5 step 3).
int32 FElysiumNpcAndreiBlood::SpeciesSelectSchedule()
{
	// CNPC_VAndreiBlood, a strict priority ladder under `field_0x1b2c = 4`.
	RecordScheduleEvent(TEXT("SelectSchedule trace 4 (CNPC_VAndreiBlood 0x1035d010)"));
	// SEAM: `m_bForceTeleport` (`+0x66d4`) is `bAndreiForceTeleport` since story 8 (Spawn19's
	// `0x1035cc20` writes it) but this ladder does not read it yet; `m_bDead` (`+0x66cd`) and
	// `m_iHitCounter` (`+0x66d8`) are `bAndreiDead` / `AndreiHitCounter` since story 8 (family
	// Damage19's `0x1035e6d0` writes both) but this ladder does not read them yet; `m_bActivated` and
	// `m_iHitMax` are `bAndreiActivated` / `AndreiHitMax` since step 4. The ladder is written out and
	// the first gate answers "activated" so the recovered order is visible; `AndreiBloodSelectGate`
	// carries the `0x1035e920` split and refuses.
	if (AndreiBloodSelectGate())
	{
		return 0x15d;
	}
	return 0x15c;
}

// Slot 566: `0x1035db00`, a replacement that does not chain.
bool FElysiumNpcAndreiBlood::FValidateHintType(void* Hint)
{
	// The whole body is `return 1;`: the hint is never read.
	(void)Hint;
	return true;
}

// Slot 546: `0x1035c460`, the class's own schedule id space.
const TCHAR* FElysiumNpcAndreiBlood::SquadSlotName(int32 SlotEn)
{
	// The class's `CAI_ClassScheduleIdSpace` `0x1093a470`, left empty by `0x102ea090(isRoot = false)`:
	// `SlotEn` translates to -1 and names `<<null>>`.
	static constexpr FSquadSlotSpecies IdSpace = {
		TEXT("CNPC_VAndreiBlood"), TEXT("0x1035c460"), TEXT("0x1093a470") };
	return GlobalSquadSlotName(SquadSlotLocalToGlobal(&IdSpace, SlotEn));
}

// --- Moved from `ElysiumNpcDamage.cpp` (story 5 step 4) ---

const TCHAR* FElysiumNpcAndreiBlood::SummonEmitterBoneName()
{
	return TEXT("Bip01_R_Hand");
}

EElysiumNpcState FElysiumNpcAndreiBlood::CNPC_VAndreiBlood_vfunc461()
{
	// Twenty-five bytes: stamp the trace selector, then answer 1 or 2.
	//     this->field_0x1b38 = 4;
	//     return (m_bActivated != 0) + 1;
	// Retail's `NPC_STATE_IDLE` is 1 and `NPC_STATE_ALERT` is 2.
	SelectIdealStateSelector = AndreiIdealStateSelector;
	return bAndreiActivated ? EElysiumNpcState::Alert : EElysiumNpcState::Idle;
}

// --- Moved from `ElysiumNpcDamage2.cpp` (story 5 step 4) ---

void FElysiumNpcAndreiBlood::StartBloodEmitter(const FString& Name)
{
	// ONE body, written twice (`0x1035e1a0` and `0x1035e3c0`); this is the BLOOD arm.
	if (Name.IsEmpty())
	{
		return;   // `TEST EDI,EDI / JZ` — a null name does nothing at all
	}
	// Release the previously cached handle only while it still resolves, then set it to -1.
	if (AndreiBloodEmitter.IsSet())
	{
		RemoveNamedEntity(AndreiBloodEmitter);
		AndreiBloodEmitter = FElysiumEntityHandle();
	}
	// Create at this NPC's own origin (slot 217 with `(&vec3_angle, -1.0f)`).
	const int32 Index = CreateNamedEmitter(Name, Origin / ElysiumMove::U, /*AttachMode*/ 2,
		Handle, nullptr);
	// The BLOOD arm parents the emitter to this entity (`thunk_FUN_100faf60`) and starts it.
	StartNamedEmitter(Index);
}

void FElysiumNpcAndreiBlood::StartSummonEmitter(const FString& Name)
{
	// The SUMMON arm: the same release/create/store, then attach at the bone `Bip01_R_Hand`
	// (`+0x3cc(this, 2, name)`) rather than parenting, then start.
	if (Name.IsEmpty())
	{
		return;
	}
	if (AndreiSummonEmitter.IsSet())
	{
		RemoveNamedEntity(AndreiSummonEmitter);
		AndreiSummonEmitter = FElysiumEntityHandle();
	}
	const int32 Index = CreateNamedEmitter(Name, Origin / ElysiumMove::U, /*AttachMode*/ 2,
		Handle, SummonEmitterBoneName());
	StartNamedEmitter(Index);
}

// --- Moved from `ElysiumNpcFacing.cpp` (story 5 step 4) ---

void FElysiumNpcAndreiBlood::FacePlayerAdvance()
{
	// `CNPC_VAndreiBlood::FacePlayerAdvance` `0x1035e5f0`, 165 bytes:
	//     if (m_hClosestPlayer resolves)
	//         m_pMotor->thunk_FUN_102e20b0( player->GetAbsOrigin(), 10.0f );
	// `DAT_104a6f80 = 10.0f` is the fixed turn rate, and the call is the motor's
	// set-ideal-yaw-to-target. The whole body is the gate plus that one command.
	const FElysiumPlayer* Player =
		Senses.Memory.ClosestPlayer.IsSet() && World != nullptr ? World->FindPlayer() : nullptr;
	if (Player == nullptr || Player->IsInert() || Player->Handle != Senses.Memory.ClosestPlayer)
	{
		return;
	}
	// **SEAM**: `IElysiumNpcMotor::Face` takes a yaw, not a target, and no turn RATE crosses it, so
	// the recovered 10.0 has nowhere to land yet. The commanded yaw is retail's own — the yaw from
	// this body to the player's origin.
	if (IElysiumNpcMotor* Mover = Motor)
	{
		const float TargetYaw =
			NpcKernelFacingShared::RetailYawOf(Player->Origin - Origin, static_cast<float>(Angles.Y));
		// `Face` is in this world's yaw, which is the negated Source one.
		Mover->Face(-TargetYaw);
	}
}

// --- Moved from `ElysiumNpcLifecycle19_2.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcPositions.cpp` (story 5 step 4) ---

int32 FElysiumNpcAndreiBlood::SelectTeleportNodeAndreiRule(TArrayView<const FHintWords> Nodes,
	const FVector& PlayerCm, bool bPickFarthest, TFunctionRef<bool(const FVector&, float)> Clear)
{
	// Types 17000 and `0x4269`, the plain 3-D distance to the player, the clearance gate at
	// `DAT_104a6f7c = 100.0`, and then a COIN FLIP that is drawn ONCE before the walk and decides
	// whether the nearest or the FARTHEST candidate wins. The seed of the running best follows the
	// flip — `FLT_MAX` for nearest, `0.0` for farthest — so the farthest arm accepts any candidate
	// at a positive distance.
	int32 Best = INDEX_NONE;
	float BestDistance = bPickFarthest ? 0.f : TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		const int32 Type = Nodes[Index].HintType;
		if (Type != NpcKernelPositionsShared::HintTeleport17000 && Type != HintTeleport17001)
		{
			continue;
		}
		const FVector& NodeCm = Nodes[Index].OriginCm;
		const float Distance = static_cast<float>(FVector::Dist(PlayerCm, NodeCm));
		if (Distance < AndreiLastTeleportFloor * NpcKernelPositionsShared::U)
		{
			continue;
		}
		if (!Clear(NodeCm, AndreiLastTeleportFloor * NpcKernelPositionsShared::U))
		{
			continue;
		}
		if (bPickFarthest ? BestDistance < Distance : Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = Index;
		}
	}
	return Best;
}

int32 FElysiumNpcAndreiBlood::SelectTeleportNodeAndrei()
{
	const FElysiumPlayer* Player =
		Senses.Memory.ClosestPlayer.IsSet() && World != nullptr ? World->FindPlayer() : nullptr;
	if (Player == nullptr || Player->Handle != Senses.Memory.ClosestPlayer)
	{
		return INDEX_NONE;   // the ONE arm that returns before the draw, so no stream position moves
	}
	// `(**(code **)(*DAT_1070b244 + 8))(0, 1)` — `VEngineRandom001::RandomInt(0, 1)`. Named
	// decision: the draw goes on `EElysiumRngStream::NpcSchedule`, this runtime's NPC decision
	// stream, because it is one decision per selection and belongs beside the schedule picks.
	const bool bPickFarthest =
		ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(0, 1) != 0;
	TArray<FHintWords> Nodes;
	TArray<int32> NodeIds;
	GatherHintNodes(Nodes, NodeIds);
	const int32 Pick = SelectTeleportNodeAndreiRule(Nodes, Player->Origin, bPickFarthest,
		[this](const FVector& PositionCm, float ClearanceCm)
		{
			return PositionClearForTeleportAndrei(PositionCm, ClearanceCm);
		});
	if (Pick == INDEX_NONE)
	{
		// Retail faults here for the same reason the Sheriff's does, and takes the same named
		// divergence. Note that Andrei stamps NO clock beside the position — unlike the Sheriff and
		// the Chang brothers, he has no `m_fLastTeleportTime` at all.
		return INDEX_NONE;
	}
	AndreiLastTeleportPosition = Nodes[Pick].OriginCm;
	return NodeIds[Pick];
}

bool FElysiumNpcAndreiBlood::PositionClearForTeleportAndrei(const FVector& PositionCm, float ClearanceCm) const
{
	// `0x1035e030`, the whole body:
	//     if (|GetAbsOrigin() - pos| <  clearance)      return false;
	//     if (|m_vLastTeleportPosition - pos| < 100.0)  return false;   // DAT_104a6f7c
	//     return true;
	// Both distances are 3-D and rooted. There is no player term and no store term: Andrei only
	// asks that the candidate be far enough from where he is and from where he last went.
	if (static_cast<float>(FVector::Dist(Origin, PositionCm)) < ClearanceCm)
	{
		return false;
	}
	if (static_cast<float>(FVector::Dist(AndreiLastTeleportPosition, PositionCm))
		< AndreiLastTeleportFloor * NpcKernelPositionsShared::U)
	{
		return false;
	}
	return true;
}

// --- Moved from `ElysiumNpcPrecache10.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcSchedule.cpp` (story 5 step 4) ---

bool FElysiumNpcAndreiBlood::AndreiBloodSelectGate() const
{
	// NO LONGER A SEAM. `thunk_FUN_1035e920` reads `+0x66b8`, which family **Species** recovered as
	// `CNPC_VAndreiBlood::m_iActiveRunnerCount` and declared, and landed the body as `FUN_1035e920`
	// in `ElysiumNpcSpecies2.cpp`. False is still the arm that answers `0x15c`; it is now
	// false because the runner budget says so rather than because nothing answered.
	return FUN_1035e920();
}

// --- Moved from `ElysiumNpcSpecies2.cpp` (story 5 step 4) ---

bool FElysiumNpcAndreiBlood::FUN_1035e920() const
{
	// `0x1035e920`, twenty-five bytes: `return m_iActiveRunnerCount (+0x66b8) < 2.0` — the cap read
	// out of `.rdata` at `_DAT_10452dc4` as **2.0f**.
	//
	// The datamap types `+0x66b8` `FIELD_INTEGER` (`m_iActiveRunnerCount`) while every body that
	// touches it uses x87 float instructions on it. It is a COUNT either way, and the three other
	// bodies that read and write it — `CNPCMaker_Fleshpile::MakeNPC` (`0x1034c2d0`, the `2 <= count`
	// refusal and the `+1` on success) and its `DeathNotice` (`0x1034c8e0`, the `-1`) — are the rest
	// of the same budget. So Andrei's fleshpile may have at most TWO runners alive at once, and this
	// is the predicate its schedule selector asks.
	//
	// Family **Schedule** stands a seam for exactly this body — `AndreiBloodSelectGate`
	// (`ElysiumNpcSchedule.cpp:1354`) — which answers false because `+0x66b8` had no port
	// member. It does now. The one-line forward is that family's file and is reported rather than
	// edited here.
	return ActiveRunnerCount < AndreiMaxActiveRunners;
}

void FElysiumNpcAndreiBlood::FUN_1035e950()
{
	// `0x1035e950`, twenty-six bytes: `m_iHitMax (+0x66dc) = RandomInt(2, 4)`.
	//
	// `(*DAT_1070b244 + 8)` is `IUniformRandomStream::RandomInt`, INCLUSIVE at both ends — the same
	// object and slot families Sounds, Damage and Positions read. So Andrei's hit budget for a phase
	// is 2, 3 or 4, drawn once. `m_iHitCounter` is `+0x66d8` (the datamap replay); `+0x66e0` is the
	// walked blood-emitter handle `AndreiBloodEmitter`, not this row's.
	//
	// The draw IS made rather than skipped: the RNG stream's position is observable, and a body that
	// refused to draw would walk every later draw on the same stream off by one.
	AndreiHitMax = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule)
		.RandRange(AndreiHitMaxMin, AndreiHitMaxMax);
}

// --- Moved from `ElysiumNpcSpeciesLifecycle10.cpp` (story 5 step 4) ---

// -------------------------------------------------------------------------------------------------
// The two destructors.
// -------------------------------------------------------------------------------------------------

void FElysiumNpcAndreiBlood::DestroyAndreiBlood()
{
	// `CNPC_VAndreiBlood::Destructor` `0x1035cd00`. The two vftable restores above the scope frame
	// are a C++ artifact with no port and nothing a program can observe.
	//
	// `1035cd62` and `1035cdd3`: the blood emitter (`+0x66e0`) and then the summon emitter
	// (`+0x66e4`). Each is `UTIL_Remove`d only when its handle resolves live, and the invalid handle
	// is written back ONLY on that arm — a stale handle is left as it stands.
	if (World != nullptr && AndreiBloodEmitter.IsSet())
	{
		if (FElysiumEntity* Emitter = World->Resolve(AndreiBloodEmitter))
		{
			Emitter->Kill();                             // `UTIL_Remove` `0x101cd940`
			AndreiBloodEmitter = FElysiumEntityHandle();  // `1035cdc0 MOV [ESI+0x66e0],0xffffffff`
		}
	}
	if (World != nullptr && AndreiSummonEmitter.IsSet())
	{
		if (FElysiumEntity* Emitter = World->Resolve(AndreiSummonEmitter))
		{
			Emitter->Kill();
			AndreiSummonEmitter = FElysiumEntityHandle();  // `1035ce2e MOV [ESI+0x66e4],0xffffffff`
		}
	}

	// `1035ce42 LEA ECX,[ESI + 0x6664] / CALL 0x10015708` — `m_OnTransformComplete`'s list, torn down
	// AFTER the scope frame pops. Counted; see the `.inl`.
	++OutputListDestroys;

	// `1035ce51 JMP 0x100123af` — `~CAI_BaseNPCTroika`. This runtime's teardown is the world's reap.
}

// --- Moved from `ElysiumNpcState19.cpp` (story 5 step 4) ---

