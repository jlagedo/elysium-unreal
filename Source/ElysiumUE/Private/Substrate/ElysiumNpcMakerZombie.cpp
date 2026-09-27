#include "Substrate/ElysiumNpcMakerZombie.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcPrecache10Shared.h"
#include "Substrate/ElysiumNpcZombie.h"

// `CNPCMaker_Zombie` — story 5 fold A4. Bodies read off the listing (`vtmb_asm`) and the
// decompilation; the walked prose is `docs/vtmb/npc-ai/lifecycle.md` ("The zombie maker").

namespace
{
	// `s_item_w_zombie_fists_10625644` — the classname `Precache` precaches and `MakeNPC` looks up.
	const TCHAR* const GZombieFistsItem = TEXT("item_w_zombie_fists");
	// `_DAT_10450a9c` = 0.9 — the scale `CanMakeNPC` multiplies its MANHATTAN distance by.
	constexpr double GZombieMakerDistanceScale = 0.9;
	// `1034d249 PUSH 0x41700000` — the spawn emitter's 15-second life.
	constexpr float GZombieSpawnEmitterLifetimeSeconds = 15.0f;
}

const FElysiumNpcClass* FElysiumNpcMakerZombie::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(RetailClassName);
	return Row;
}

// Slot 82: `0x1034c9f0` returns `&datamap_CNPCMaker_Zombie` (`0x106253e8`).
void* FElysiumNpcMakerZombie::GetDataDescMap()
{
	return const_cast<FElysiumClassDesc*>(FElysiumClassRegistry::Get().Find(FName(RetailClassName)));
}

// Slot 103: `0x1034cc60`.
void FElysiumNpcMakerZombie::Spawn()
{
	SpawnPrefix();
	if (!bDisabled)
	{
		// `PUSH 0x10015c4e` -> `0x1034d2d0`; `m_flNextThink = RandomFloat(1, 2) + freq + curtime` (the
		// jitter is drawn first, `1034cd2a`, then the frequency and the clock are added).
		InstalledThink = EMakerThink::Zombie;
		const double Jitter = static_cast<double>(ElysiumRng::Stream(EElysiumRngStream::NpcMaker).FRandRange(
			ZombieSpawnJitterMin, ZombieSpawnJitterMax));
		ArmThinkAt(Jitter + static_cast<double>(SpawnFrequency) + (World != nullptr ? World->NowSeconds() : 0.0));
	}
	else
	{
		// `1034cd4a PUSH 0x0` — a NULL think, not the inert one; `m_flNextThink` untouched.
		InstalledThink = EMakerThink::None;
	}
	// `1034cd51` — the two arms join, then ONE `Relink`.
	++RelinkCalls;
	// `1034cd5d` — `m_flGround = 0`, then the five police thresholds, in offset order, ON THE MAKER.
	CachedGroundZ = 0.0f;
	PlInvestigate = ZombieMakerPoliceLevel;           // +0x6348
	PlCriminalFlee = ZombieMakerPoliceLevel;          // +0x634c
	PlCriminalAttack = ZombieMakerPoliceLevel;        // +0x6350
	PlSupernaturalFlee = ZombieMakerPoliceLevel;      // +0x6354
	PlSupernaturalAttack = ZombieMakerPoliceLevel;    // +0x6358
}

// Slot 104: `0x1034cde0`.
void FElysiumNpcMakerZombie::Precache()
{
	if (!PrecacheMakerModel(/*bBadModelOverlay=*/false))
	{
		return;
	}
	// `m_altEquipment` (`+0x1a98`) then `m_spawnEquipment` (`+0x5dec`) zeroed on the MAKER, BEFORE the
	// chain — so the base chain's `UTIL_PrecacheOther(m_spawnEquipment)` arm never fires here.
	AlternateEquipment.Reset();
	AdditionalEquipment.Reset();
	// `CAI_BaseNPC::Precache` `0x1027bb50`, DIRECT.
	FElysiumNpcBase::Precache();
	// Nothing is tested after the chain: its reject arm `UTIL_Remove`s and returns, and this body
	// carries on regardless (a removal is deferred in retail).
	// The child class, unconditionally, then the fists.
	NpcKernelPrecache10Shared::Precache10Other(*this, NpcType);
	NpcKernelPrecache10Shared::Precache10Other(*this, GZombieFistsItem);
}

// Slots 362-365: `0x1034cb50`, `0x1034cb30`, `0x1034cb90`, `0x1034cb70`.
bool FElysiumNpcMakerZombie::FInViewCone(const FVector& PointCm)
{
	(void)PointCm;
	return false;
}

bool FElysiumNpcMakerZombie::FInViewCone(FElysiumEntity* Candidate)
{
	(void)Candidate;
	return false;
}

bool FElysiumNpcMakerZombie::FInAimCone(const FVector& TargetCm)
{
	(void)TargetCm;
	return false;
}

bool FElysiumNpcMakerZombie::FInAimCone(FElysiumEntity* AimTarget)
{
	(void)AimTarget;
	return false;
}

// Slot 370: `0x1034cad0`.
FVector FElysiumNpcMakerZombie::HeadDirection2D()
{
	return BodyDirection2D();
}

// Slot 371: `0x1034cb00`.
FVector FElysiumNpcMakerZombie::HeadDirection3D()
{
	return BodyDirection3D();
}

// Slot 619: `0x1034cbb0`.
void FElysiumNpcMakerZombie::ChildPreSpawn(FElysiumNpc* Child)
{
	(void)Child;
}

// Slot 620: `0x1034cbd0`.
void FElysiumNpcMakerZombie::ChildPostSpawn(FElysiumNpc* Child)
{
	(void)Child;
}

// Slot 618: `0x1034d0a0`.
bool FElysiumNpcMakerZombie::CanMakeNPC(bool bBypass)
{
	// 1. A bypass answers yes before anything else.
	if (bBypass)
	{
		LastAttempt = EAttempt::Spawned;
		return true;
	}
	// 2. With a player: `m_flRemoveDist (+0x76d8, float) < 0.9 * (|dx| + |dy| + |dz|)` refuses — a
	//    zombie maker refuses when the player is too FAR (the base's `+0x66c8` refuses too CLOSE).
	const FElysiumPlayer* Player = World != nullptr ? World->FindPlayer() : nullptr;
	if (Player != nullptr)
	{
		const FVector Delta = Player->Origin - Origin;
		const double ManhattanUnits = (FMath::Abs(Delta.X) + FMath::Abs(Delta.Y) + FMath::Abs(Delta.Z)) / ElysiumMove::U;
		if (static_cast<double>(RemoveDistance) < ManhattanUnits * GZombieMakerDistanceScale)
		{
			LastAttempt = EAttempt::Distance;
			return false;
		}
	}
	// 3. `thunk_FUN_1034b580(this, false)` — the base, DIRECT, with a hard-coded false bypass.
	return FElysiumNpcMaker::CanMakeNPC(/*bBypass=*/false);
}

// Slot 617: `0x1034d140`.
FElysiumNpc* FElysiumNpcMakerZombie::MakeNPC(bool bBypass)
{
	// 1. `thunk_FUN_1034b7b0(this, bypass)` — the base `MakeNPC`, DIRECT (its slot-618 call is still
	//    virtual, so this class's `CanMakeNPC` admits). A null child returns null.
	FElysiumNpc* Child = FElysiumNpcMaker::MakeNPC(bBypass);
	if (Child == nullptr)
	{
		return nullptr;
	}
	// 2. `Weapon_OwnsThisType(this, "item_w_zombie_fists", 0)` — asked of the MAKER (`MOV ECX,EDI`).
	//    **Named divergence:** on the true arm retail dereferences a null item (`XOR ESI,ESI`, the
	//    lookup skipped, `1034d1a3 MOV EBP,[ESI]`); the port returns the child instead of faulting.
	//    Unreachable either way: the seam answers false, as a maker owns no weapon.
	if (WeaponOwnsThisType(GZombieFistsItem))
	{
		return Child;
	}
	// 3. `thunk_FUN_10136580("item_w_zombie_fists")`: a missing definition removes the child
	//    (`UTIL_Remove`) and answers null.
	if (!ZombieFistsItemExists())
	{
		Child->Kill();
		return nullptr;
	}
	// 4-5. `item->SetOrigin(this->EyePosition())` (slot 62 / slot 193), `item+0x204 |= 0x40000000`,
	//    `DispatchSpawn(item)`, and when the item's `+0x1d0` check answers false,
	//    `child->Weapon_Equip(item, false)` (slot 383). SEAM: recorded.
	LastZombieFistsItem = GZombieFistsItem;
	++ZombieFistsEquips;
	// 6. `___RTDynamicCast(child, 0, CBaseEntity, CNPC_VZombie 0x1062567c, 0)` (the descriptor's name at
	//    `+8`, `0x10625684`, is `.?AVCNPC_VZombie@@`) and, on a hit, `CNPC_VZombie::SetZombieAIType`
	//    `0x103e0980(child, m_iZombieAISpawnType)`.
	if (Child->AsSpecies<FElysiumNpcZombie>() != nullptr)
	{
		Child->FUN_103e0980(ZombieAiSpawnType);
	}
	// 7. `"Zombies_spawning_emitter"` at `GetAbsOrigin()` / `GetAbsAngles(-1.0)`, 15 s. SEAM: recorded.
	FZombieSpawnEmitter Emitter;
	Emitter.Origin = Origin;
	Emitter.Angles = FVector(Angles.X, Angles.Y, Angles.Z);
	Emitter.LifetimeSeconds = GZombieSpawnEmitterLifetimeSeconds;
	ZombieSpawnEmitters.Add(Emitter);
	// 8. `child->Unhide()` (slot 67, dispatched) and `SetDisableAI(child, false)`, which overrides the
	//    copy the base made from the maker's own `m_bDisableAI`.
	Child->Unhide();
	Child->SetDisableAi(false);
	return Child;
}

void FElysiumNpcMakerZombie::Think()
{
	if (InstalledThink == EMakerThink::Zombie)
	{
		ZombieMakerThink();
		return;
	}
	FElysiumNpcMaker::Think();
}

// `0x1034d2d0`.
void FElysiumNpcMakerZombie::ZombieMakerThink()
{
	// `CALL [EAX+0x9a4](0)`, `JNZ` on the child, the live-ceiling pair, the `RandomFloat(1, 2)` retry:
	// `MakerThink`'s instructions, and its slot-617 call reaches this class's `MakeNPC`.
	MakerThink();
}

void FElysiumNpcMakerZombie::SetZombieAIType(int32 Type, bool bPropagate)
{
	// `0x1034cf20`.
	ZombieAiSpawnType = Type;
	if (!bPropagate || World == nullptr)
	{
		return;
	}
	// `for (e = FindEntityByName(NULL, m_ChildTargetName); e; e = FindEntityByName(e, ...))`: an entity
	// with a Troika pointer whose `+0x254` handle resolves to this maker. `+0x254` is `m_pParent` by the
	// datamap layout, which is the port's resolved `MoveParent`.
	for (const TUniquePtr<FElysiumEntity>& Candidate : World->Entities())
	{
		if (!Candidate.IsValid() || Candidate->IsDead()
			|| !Candidate->TargetName.Equals(ChildTargetName, ESearchCase::IgnoreCase))
		{
			continue;
		}
		FElysiumNpc* Npc = Candidate->AsNpc();
		if (Npc != nullptr && Npc->MoveParent == Handle)
		{
			Npc->FUN_103e0980(Type);
		}
	}
}

bool FElysiumNpcMakerZombie::ZombieFistsItemExists() const
{
#if WITH_DEV_AUTOMATION_TESTS
	if (ZombieFistsItemForTests.IsSet())
	{
		return ZombieFistsItemForTests.GetValue();
	}
#endif
	return FElysiumClassRegistry::Get().Find(FName(GZombieFistsItem)) != nullptr;
}
