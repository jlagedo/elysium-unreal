#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"

// Story 29c-1, family **Squad** — the squad and follower surface of `order.md` layers 0–9.
//
// 74 rows: the 57 slot-546 `SquadSlotName` bodies (one method plus a species table, below), the
// squad join/leave/share/vacate set, the follower pair, and the `CNPC_VChangBros` /
// `CNPC_VMingXiao` coordination helpers. The walked prose is `docs/vtmb/npc-ai/social.md`.
//
// THE STANDING FACT OF THIS FAMILY: there is no squad object here. `m_pSquad` (`+0x5da4`) is
// recorded ABSENT and `ConnectedSquad()` answers `nullptr`. Every body below is ported verbatim
// and then asks the seam declared in `ElysiumNpcKernelSquad.inl`, which answers nothing and names
// the retail call it stands for. Nothing here invents a squad to make a body "work".

namespace
{
	// `bits_CAP_SQUAD`, the `CapabilitiesGet()` bit `InitSquad` gates on (`0x10273d30`).
	constexpr int32 GNpcKernelSquadCapSquad = 0x4000000;

	// The two symbols `0x10316e80` registers into the one squad-slot namespace `DAT_10936c74`, with
	// the ids it registers them under. They are the ONLY squad-slot names in `vampire.dll`.
	constexpr int32 GNpcKernelSquadSlotAttack1 = 1000000000;  // 0x3b9aca00
	constexpr int32 GNpcKernelSquadSlotAttack2 = 1000000001;  // 0x3b9aca01

	// `CAI_LocalIdSpace`'s "this space holds no ids" sentinel, tested by name in `0x102ea2d0`.
	constexpr int32 GNpcKernelSquadEmptyIdSpace = 9999;

	// The root `CAI_ClassScheduleIdSpace` (`DAT_10920484`), the only one in the image constructed
	// with `isRoot = true` (`staticinit_10265660` → `0x102ea090('\x01')`): global base 0, local base
	// 0, local top -1. A top of -1 matches no id, so the chain walk falls off the end and answers
	// -1 for every species. Declared as a row so the walk below is the retail walk and not a
	// hard-coded refusal.
	constexpr FElysiumNpc::FSquadSlotSpecies GNpcKernelSquadRootIdSpace = {
		TEXT("CAI_BaseNPC"), TEXT(""), TEXT("0x10920484"), 0, 0, INDEX_NONE };

	// `_DAT_1044e664` — the follower-distance overlap, recovered by story 16a
	// (`docs/vtmb/npc-ai/social.md` § "`m_hFollowerBoss` — the follower controller").
	constexpr float GNpcKernelSquadFollowerOverlap = 10.0f;
}

// -------------------------------------------------------------------------------------------------
// The squad seam. Seven accessors, each answering nothing.
// -------------------------------------------------------------------------------------------------

void* FElysiumNpc::FindOrCreateSquad(const FString& InSquadName, bool bFindOnly) const
{
	// `FindCreateSquad` `0x10315800` / `FindSquad` `0x10315790`: a `strcmpi` walk of the global
	// squad list `g_pSquadList` (`0x10936c68`), then `new CAI_Squad(name)` on a miss, with a 17th
	// recruit DevMsg'd (`"Error!! Squad %s is too big!!!"`) and overwriting member 16.
	//
	// SEAM: no squad list, no `CAI_Squad`. Answers nothing for retail's `m_pSquad` (`+0x5da4`).
	(void)InSquadName;
	(void)bFindOnly;
	return nullptr;
}

void FElysiumNpc::RemoveFromSquad(void* Squad)
{
	// SEAM for `CAI_Squad::RemoveFromSquad` (`0x103158f0`): compacts `m_hMembers` and calls
	// slot 578 (an empty virtual) on each survivor.
	(void)Squad;
}

void FElysiumNpc::AddSelfToSquadMemory(void* Squad)
{
	// SEAM for `CAI_Squad::AddSelfToSquadMemory` (`0x10316720`), the arm `ReconnectToSquad`
	// (`0x1026d0c0`) takes when the disconnect count reaches zero.
	(void)Squad;
}

int32 FElysiumNpc::SquadMemberCount(const void* Squad) const
{
	// SEAM for `CAI_Squad::NumMembers` (`0x103160a0`, `squad+0x5c`).
	(void)Squad;
	return 0;
}

FElysiumEntity* FElysiumNpc::SquadMember(const void* Squad, int32 Index) const
{
	// SEAM for `CAI_Squad::GetMember(i)` (`0x103160c0`, the `m_hMembers[16]` array at `squad+0x1c`,
	// which answers NULL for every index when member 0 is disconnected).
	(void)Squad;
	(void)Index;
	return nullptr;
}

bool FElysiumNpc::IsSquadSlotOccupied(const void* Squad, int32 SquadSlot) const
{
	// SEAM for `m_squadSlotsUsed` (`CAI_Squad+0x64`, the `CVarBitVec` whose word array hangs off
	// `+0x6c`). `docs/vtmb/npc-ai/social.md`: "Strategy slots ship dead" — retail itself has no
	// `OccupyStrategySlot`, so nothing ever sets a bit.
	(void)Squad;
	(void)SquadSlot;
	return false;
}

void FElysiumNpc::ClearSquadSlotOccupied(void* Squad, int32 SquadSlot)
{
	// SEAM: the write half of the bitmap above.
	(void)Squad;
	(void)SquadSlot;
}

void FElysiumNpc::RepointEnemyMemoryToSquad(void* Squad)
{
	// SEAM for the ownership move `SetSquad` (`0x1029a930`) and `SetSquadEnemies` (slot 542,
	// `0x10273dd0`) perform: delete the private `AI_Enemies` and point `m_pEnemies` (`+0x5d88`) at
	// `squad+8`, the squad's embedded memory. This runtime carries one enemy memory per NPC
	// (`FElysiumNpc::EnemyMemory`) and has no squad memory to point it at, so the ownership move is
	// recorded here and changes nothing.
	(void)Squad;
}

FElysiumEntity* FElysiumNpc::NthHintOfType(int32 HintType, int32 Ordinal) const
{
	// SEAM for the global `CAI_Hint` list walk (`DAT_10925450`, next `+0x5d8`, `m_nHintType
	// +0x5dc`). `ScheduleHost.HintNode` is a bare node index here; no store carries hint types yet.
	(void)HintType;
	(void)Ordinal;
	return nullptr;
}

// -------------------------------------------------------------------------------------------------
// Slot 546 `SquadSlotName` — one port method and a species table.
// -------------------------------------------------------------------------------------------------
//
// Every row was read from the decompiled C, not from a summary. All 56 species bodies are
// byte-identical but for their id-space global:
//
//     iVar1 = SquadSlotLocalToGlobal(&DAT_<species>, slotEN);   // 0x102ea2d0
//     return IdToSymbol(&DAT_10936c74, iVar1);                  // 0x102ea020
//
// and the Troika line (`0x101a6c00`) is the same without the first line.
//
// THE ID SPACE EACH SPECIES ANSWERS IS EMPTY, and that is a recovered fact, not a gap:
//
//   * every species space is constructed with `isRoot = false` (`0x102ea090`), which leaves
//     `m_globalBase = -1`, `m_localBase = 9999` (the "empty" sentinel) and `m_localTop = -1`;
//   * `CAI_ClassScheduleIdSpace::Init` (`0x102ea0e0`) only rewrites those three when the space has
//     already been filled (`+0x0c != -1`), which at static-init time it has not, so `Init` binds
//     the namespace and the parent and leaves the range empty;
//   * the only way a range becomes non-empty is `ADD_CUSTOM_SQUADSLOT`, which registers the name
//     into the global namespace as well — and `vampire.dll` contains exactly TWO squad-slot name
//     strings, `SQUAD_SLOT_ATTACK1` and `SQUAD_SLOT_ATTACK2`, both referenced only by
//     `0x10316e80`, the seeder of the GLOBAL namespace. No class registers one;
//   * the chain ends at the root space `DAT_10920484` (global base 0, local base 0, local top -1),
//     which matches no id either.
//
// So `SquadSlotName(n)` answers `"<<null>>"` for every `n` on every one of the 56 species, and on
// the Troika line answers `SQUAD_SLOT_ATTACK1`/`2` for ids 1000000000/1000000001, `"<<null>>"` for
// -1 and null for anything else. This agrees with `docs/vtmb/npc-ai/social.md` § "Squads, decoded
// (2026-09-08)" — "Strategy slots ship dead … every class registers zero squadslots".

namespace
{
	// The 60 census classes that override slot 546 (56 distinct bodies; `CNPC_ProneDialog` shares
	// `CNPC_VHumanCombatant`'s, `CNPC_VCameraSecurity` shares `CNPC_VCamera`'s, `CNPC_VRat` shares
	// `CNPC_VScurrying`'s and `CNPC_VPlayerController` shares `CNPC_VVampire`'s), plus the Troika
	// line, which carries no id space at all. Columns: the retail class, the body that fills slot
	// 546 for it (checkable against `docs/vtmb/npc-kernel/slots.md`) and its own
	// `CAI_ClassScheduleIdSpace`. The three range fields take the struct's defaults, which ARE the
	// recovered state every one of the 56 spaces was left in by `0x102ea090(isRoot = false)`.
	constexpr FElysiumNpc::FSquadSlotSpecies GNpcKernelSquadSlotSpecies[] = {
		// The Troika line itself: `CAI_BaseNPC::SquadSlotName` looks the id up directly.
		{ TEXT("CAI_BaseNPCTroika"), TEXT("0x101a6c00"), TEXT("") },
		{ TEXT("CGenericSabbat_NPC"), TEXT("0x1035b4f0"), TEXT("0x1093a350") },
		{ TEXT("CGeneric_NPC"), TEXT("0x10359e90"), TEXT("0x1093a234") },
		{ TEXT("CGeneric_NPC_bathack"), TEXT("0x1035ace0"), TEXT("0x1093a314") },
		{ TEXT("CNPC_Crow"), TEXT("0x10359240"), TEXT("0x1093a070") },
		{ TEXT("CNPC_ProneDialog"), TEXT("0x10386c80"), TEXT("0x1093b47c") },
		{ TEXT("CNPC_VAndreiBlood"), TEXT("0x1035c460"), TEXT("0x1093a470") },
		{ TEXT("CNPC_VAnimal"), TEXT("0x1035edb0"), TEXT("0x1093a4f4") },
		{ TEXT("CNPC_VAsianVampire"), TEXT("0x10360610"), TEXT("0x1093a578") },
		{ TEXT("CNPC_VBach"), TEXT("0x10362df0"), TEXT("0x1093a608") },
		{ TEXT("CNPC_VBatSwarm"), TEXT("0x10366e10"), TEXT("0x1093a6d0") },
		{ TEXT("CNPC_VBrujah"), TEXT("0x10367a10"), TEXT("0x1093a788") },
		{ TEXT("CNPC_VCamera"), TEXT("0x10368550"), TEXT("0x1093a7bc") },
		{ TEXT("CNPC_VCameraSecurity"), TEXT("0x10368550"), TEXT("0x1093a7bc") },
		{ TEXT("CNPC_VChangBros"), TEXT("0x1036a3f0"), TEXT("0x1093aa70") },
		{ TEXT("CNPC_VChangBrosBlade"), TEXT("0x1036ecf0"), TEXT("0x1093a8d8") },
		{ TEXT("CNPC_VChangBrosClaw"), TEXT("0x1036f4f0"), TEXT("0x1093aa10") },
		{ TEXT("CNPC_VCombatman"), TEXT("0x1036fcb0"), TEXT("0x1093abb0") },
		{ TEXT("CNPC_VCop"), TEXT("0x10370ad0"), TEXT("0x1093ac40") },
		{ TEXT("CNPC_VDog"), TEXT("0x103736d0"), TEXT("0x1093ad64") },
		{ TEXT("CNPC_VFrenzyShadow"), TEXT("0x10375440"), TEXT("0x1093ae28") },
		{ TEXT("CNPC_VGangrel"), TEXT("0x10377210"), TEXT("0x1093aed8") },
		{ TEXT("CNPC_VGargoyle"), TEXT("0x10377cd0"), TEXT("0x1093b038") },
		{ TEXT("CNPC_VGhoulCroucher"), TEXT("0x1037a950"), TEXT("0x1093b0e0") },
		{ TEXT("CNPC_VGuard1"), TEXT("0x1037c800"), TEXT("0x1093b1b4") },
		{ TEXT("CNPC_VHengeyokai"), TEXT("0x1037ea60"), TEXT("0x1093b27c") },
		{ TEXT("CNPC_VHuman"), TEXT("0x10384200"), TEXT("0x1093b3c4") },
		{ TEXT("CNPC_VHumanCombatPatrol"), TEXT("0x103878b0"), TEXT("0x1093b580") },
		{ TEXT("CNPC_VHumanCombatant"), TEXT("0x10386c80"), TEXT("0x1093b47c") },
		{ TEXT("CNPC_VHunter"), TEXT("0x10388200"), TEXT("0x1093b5e8") },
		{ TEXT("CNPC_VLasombra"), TEXT("0x10388f80"), TEXT("0x1093b680") },
		{ TEXT("CNPC_VMalkavian"), TEXT("0x10389700"), TEXT("0x1093b6fc") },
		{ TEXT("CNPC_VManBat"), TEXT("0x10389f50"), TEXT("0x1093b89c") },
		{ TEXT("CNPC_VMingXiao"), TEXT("0x10391390"), TEXT("0x1093bacc") },
		{ TEXT("CNPC_VMingXiaoTentacle"), TEXT("0x1039b230"), TEXT("0x1093bd80") },
		{ TEXT("CNPC_VMoleman"), TEXT("0x1039f7a0"), TEXT("0x1093bdb8") },
		{ TEXT("CNPC_VNosferatu"), TEXT("0x103a1640"), TEXT("0x1093bf30") },
		{ TEXT("CNPC_VPedestrian"), TEXT("0x103a1fa0"), TEXT("0x1093bffc") },
		{ TEXT("CNPC_VPlaceholder"), TEXT("0x103a3c50"), TEXT("0x1093c08c") },
		{ TEXT("CNPC_VPlayerController"), TEXT("0x103c4a80"), TEXT("0x1093d2a4") },
		{ TEXT("CNPC_VRat"), TEXT("0x103abd40"), TEXT("0x1093c4e0") },
		{ TEXT("CNPC_VSabbatGunman"), TEXT("0x103a5240"), TEXT("0x1093c1d8") },
		{ TEXT("CNPC_VSabbatLeader"), TEXT("0x103a5e50"), TEXT("0x1093c3d4") },
		{ TEXT("CNPC_VScurrying"), TEXT("0x103abd40"), TEXT("0x1093c4e0") },
		{ TEXT("CNPC_VSheriffMan"), TEXT("0x103adcb0"), TEXT("0x1093c568") },
		{ TEXT("CNPC_VSheriffSwarm"), TEXT("0x103b1dc0"), TEXT("0x1093c680") },
		{ TEXT("CNPC_VStalker"), TEXT("0x103b2a50"), TEXT("0x1093c738") },
		{ TEXT("CNPC_VTaxiDriver"), TEXT("0x103b3170"), TEXT("0x1093c7f0") },
		{ TEXT("CNPC_VTest"), TEXT("0x103b3cc0"), TEXT("0x1093c8bc") },
		{ TEXT("CNPC_VToreador"), TEXT("0x103b54d0"), TEXT("0x1093c948") },
		{ TEXT("CNPC_VTremere"), TEXT("0x103b5c70"), TEXT("0x1093c9c8") },
		{ TEXT("CNPC_VTzimisce"), TEXT("0x103b70f0"), TEXT("0x1093ccc4") },
		{ TEXT("CNPC_VTzimisceHeadClaw"), TEXT("0x103c0c90"), TEXT("0x1093d1ac") },
		{ TEXT("CNPC_VTzimisceRunner"), TEXT("0x103c2a60"), TEXT("0x1093d224") },
		{ TEXT("CNPC_VVampire"), TEXT("0x103c4a80"), TEXT("0x1093d2a4") },
		{ TEXT("CNPC_VVampireBoss"), TEXT("0x103c5270"), TEXT("0x1093d30c") },
		{ TEXT("CNPC_VVentrue"), TEXT("0x103c7950"), TEXT("0x1093d394") },
		{ TEXT("CNPC_VWerewolf"), TEXT("0x103c8ed0"), TEXT("0x1093d6d4") },
		{ TEXT("CNPC_VWolfMorph"), TEXT("0x103dc950"), TEXT("0x1094028c") },
		{ TEXT("CNPC_VYukie"), TEXT("0x103dd1f0"), TEXT("0x10940314") },
		{ TEXT("CNPC_VZombie"), TEXT("0x103de4d0"), TEXT("0x109403e0") },
	};
}

const FElysiumNpc::FSquadSlotSpecies* FElysiumNpc::SquadSlotSpeciesRows(int32& OutCount)
{
	OutCount = UE_ARRAY_COUNT(GNpcKernelSquadSlotSpecies);
	return GNpcKernelSquadSlotSpecies;
}

const FElysiumNpc::FSquadSlotSpecies* FElysiumNpc::SquadSlotSpeciesOf(const TCHAR* InRetailClass)
{
	if (InRetailClass == nullptr)
	{
		return nullptr;
	}
	for (const FSquadSlotSpecies& Row : GNpcKernelSquadSlotSpecies)
	{
		if (FCString::Strcmp(Row.RetailClass, InRetailClass) == 0)
		{
			return &Row;
		}
	}
	return nullptr;
}

int32 FElysiumNpc::SquadSlotLocalToGlobal(const FSquadSlotSpecies* Species, int32 LocalId)
{
	// 0x102ea2d0 `CAI_ClassScheduleIdSpace::SquadSlotLocalToGlobal`, arm for arm:
	//
	//   if (id == -1) return -1;
	//   do {
	//     if (localBase != 9999 && localBase <= id && id <= localTop)
	//       return (globalBase - localBase) + id;
	//     space = space->parent;
	//   } while (space);
	//   return -1;
	//
	// A null `Species` is the Troika line (`0x101a6c00`), which performs no translation at all and
	// hands `slotEN` straight to `IdToSymbol`.
	if (Species == nullptr || Species->IdSpace == nullptr || *Species->IdSpace == TEXT('\0'))
	{
		return LocalId;
	}
	if (LocalId == INDEX_NONE)
	{
		return INDEX_NONE;
	}
	// The chain: this class's space, then its parent's, up to the root. Retail's parent link is
	// per-class (`Init`'s third argument), and every level of every chain carries the same empty
	// range, so the walk is modelled as species → root.
	const FSquadSlotSpecies* Chain[2] = { Species, &GNpcKernelSquadRootIdSpace };
	for (const FSquadSlotSpecies* Space : Chain)
	{
		if (Space->LocalBase != GNpcKernelSquadEmptyIdSpace && Space->LocalBase <= LocalId
			&& LocalId <= Space->LocalTop)
		{
			return (Space->GlobalBase - Space->LocalBase) + LocalId;
		}
	}
	return INDEX_NONE;
}

const TCHAR* FElysiumNpc::GlobalSquadSlotName(int32 GlobalId)
{
	// 0x102ea020 `CAI_GlobalNamespace::IdToSymbol`: -1 answers the literal `"<<null>>"`, anything
	// else is looked up in the symbol table and answers NULL when it is not there
	// (`0x10249c70`). `0x10316e80` puts exactly two symbols in this table.
	if (GlobalId == INDEX_NONE)
	{
		return TEXT("<<null>>");
	}
	if (GlobalId == GNpcKernelSquadSlotAttack1)
	{
		return TEXT("SQUAD_SLOT_ATTACK1");
	}
	if (GlobalId == GNpcKernelSquadSlotAttack2)
	{
		return TEXT("SQUAD_SLOT_ATTACK2");
	}
	return nullptr;
}

// slot 546 0x101a6c00 `const char* SquadSlotName(int)` + the 56 species overrides
const TCHAR* FElysiumNpc::SquadSlotName(int32 SlotEn)
{
	const FElysiumNpcClassSlot* Override = ElysiumNpcKernelClass::OverrideOf(RetailClass(), 546);
	const FSquadSlotSpecies* Species =
		Override != nullptr ? SquadSlotSpeciesOf(Override->Class) : nullptr;
	return GlobalSquadSlotName(SquadSlotLocalToGlobal(Species, SlotEn));
}

// -------------------------------------------------------------------------------------------------
// The squad bodies.
// -------------------------------------------------------------------------------------------------

// slot 545 0x10273d30 `bool InitSquad()`, with CNPC_VCamera / CNPC_VCameraSecurity's 0x10369bd0
bool FElysiumNpc::InitSquad()
{
	if (ConnectedSquad() == nullptr)
	{
		// `CapabilitiesGet()` is slot 513 (`0x1026db30`) and is still a generated stub, so
		// `bits_CAP_SQUAD` never reads set and this body stops here. That refusal IS the seam:
		// asking the capability is retail's first gate and the port asks it.
		if ((CapabilitiesGet() & GNpcKernelSquadCapSquad) != 0)
		{
			if (SquadName.IsEmpty())
			{
				UE_LOG(LogElysiumNpcEnt, Warning,
					TEXT("WARNING: Found %s that isn't in a squad but not supposed to be solo"),
					*DebugString());
				return ConnectedSquad() != nullptr;
			}
			// The species split, read off the census rather than off a name: `CNPC_VCamera` and
			// `CNPC_VCameraSecurity` fill slot 545 with `0x10369bd0`, everything else inherits the
			// Troika line's `0x10273d30`.
			const TCHAR* SlotBody = ElysiumNpcKernelClass::BodyOf(RetailClass(), 545);
			const bool bCameraArm =
				SlotBody != nullptr && FCString::Strcmp(SlotBody, TEXT("0x10369bd0")) == 0;
			if (bCameraArm)
			{
				// 0x10369bd0: join an EXISTING squad by name; on a miss create one and then
				// immediately remove yourself from it — the camera wants the squad object for the
				// shared memory, not membership. Verbatim, odd as it reads.
				void* Squad = FindOrCreateSquad(SquadName, /*bFindOnly=*/true);
				if (Squad == nullptr)
				{
					Squad = FindOrCreateSquad(SquadName, /*bFindOnly=*/false);
					if (Squad != nullptr)
					{
						RemoveFromSquad(Squad);
					}
				}
			}
			else
			{
				// 0x10273d30: find-or-create and join.
				FindOrCreateSquad(SquadName, /*bFindOnly=*/false);
			}
			// Slot 542 `SetSquadEnemies` (`0x10273dd0`) — delete the private memory and point
			// `m_pEnemies` at the squad's. Called on both arms.
			Slot542();
		}
	}
	return ConnectedSquad() != nullptr;
}

void FElysiumNpc::SetSquad(const FString& NewSquadName)
{
	// 0x1029a930 `CAI_BaseNPCTroika::SetSquad`, the `SQUAD` tweak param's move:
	//
	//   1. already squadded  -> RemoveFromSquad(m_pSquad, this); m_pEnemies = NULL
	//      not squadded      -> delete the private AI_Enemies (0x102e0730 + operator delete)
	//   2. squad = FindCreateSquad(this, name[0] ? name : NULL)
	//      hit  -> m_pSquad = squad; m_pEnemies = squad + 8   (the squad's embedded AI_Enemies)
	//      miss -> m_pSquad = NULL;  m_pEnemies = new AI_Enemies (0x102e06f0)
	//   3. if (m_iSquadDisconnected > 0 && m_pSquad) LeaveSquad(this)   -- `0x10316700` is RET 4,
	//      an empty stub, so this arm has no effect in retail either.
	void* Squad = const_cast<void*>(ConnectedSquad());
	if (Squad != nullptr)
	{
		RemoveFromSquad(Squad);
	}
	// Step 2. The seam never finds and never creates, so the enemy memory keeps its private
	// ownership, which is what the "miss" arm does anyway (`new AI_Enemies`).
	//
	// `SetSquad` does NOT write `m_SquadName` (`+0x5da8`) — the keyfield and the squad object are
	// separate words in retail and this body only moves the object.
	void* NewSquad = FindOrCreateSquad(NewSquadName, /*bFindOnly=*/false);
	RepointEnemyMemoryToSquad(NewSquad);
	if (ScheduleHost.SquadDisconnected > 0 && ConnectedSquad() != nullptr)
	{
		// `LeaveSquad` `0x10316700` is `RET 4`.
	}
}

bool FElysiumNpc::SharesSquadWith(const FElysiumNpc* Other) const
{
	// 0x102781a0: null other -> false; my squad null -> false (even when the other's is also
	// null); else `m_pSquad == other->m_pSquad`.
	if (Other == nullptr)
	{
		return false;
	}
	const void* MySquad = ConnectedSquad();
	if (MySquad == nullptr)
	{
		return false;
	}
	return MySquad == Other->ConnectedSquad();
}

void FElysiumNpc::VacateSquadSlot()
{
	// 0x1028ae60. Gates: `m_iMySquadSlot != -1`, `m_iSquadDisconnected < 1`, `m_pSquad != 0`.
	// Then it reads the squad's `m_squadSlotsUsed` word for the slot and DevMsgs
	// `"ERROR: Vacating an empty slot!"` when the bit is already clear, re-reads the squad under
	// the same disconnect test (retail tests it twice; the second read yields NULL when
	// disconnected, which would fault — it cannot be reached because the first gate already
	// required `< 1`), clears the bit and writes `m_iMySquadSlot = -1`.
	//
	// NOTE: this runtime declares `MySquadSlot = 0`, not retail's -1 sentinel. Nothing writes the
	// field — `docs/vtmb/npc-ai/social.md`: "zero code readers, save-only" — and the squad gate
	// below refuses first, so the difference is unobservable today. Reported so 29b's default can
	// be corrected with the shape map.
	if (MySquadSlot == INDEX_NONE || ScheduleHost.SquadDisconnected >= 1)
	{
		return;
	}
	void* Squad = const_cast<void*>(ConnectedSquad());
	if (Squad == nullptr)
	{
		return;
	}
	if (!IsSquadSlotOccupied(Squad, MySquadSlot))
	{
		UE_LOG(LogElysiumNpcEnt, Warning, TEXT("ERROR: Vacating an empty slot!"));
	}
	ClearSquadSlotOccupied(Squad, MySquadSlot);
	MySquadSlot = INDEX_NONE;
}

FElysiumNpc* FElysiumNpc::GetOtherBrother() const
{
	// 0x1036e2f0 `CNPC_VChangBros::GetOtherBrother`: with `m_iSquadDisconnected < 1` and a live
	// `m_pSquad`, walk `0..NumMembers()` re-reading `NumMembers()` every iteration, `RTDynamicCast`
	// each member to `CNPC_VChangBros` and answer the first one that is not me. Else 0.
	if (ScheduleHost.SquadDisconnected >= 1)
	{
		return nullptr;
	}
	const void* Squad = ConnectedSquad();
	if (Squad == nullptr)
	{
		return nullptr;
	}
	for (int32 Index = 0; Index < SquadMemberCount(Squad); ++Index)
	{
		FElysiumEntity* Member = SquadMember(Squad, Index);
		if (Member == nullptr)
		{
			continue;
		}
		FElysiumNpc* Brother = Member->AsNpc();
		// The RTTI cast, as the chain walk this runtime dispatches species by.
		if (Brother != nullptr && Brother != this
			&& Brother->IsRetailClass(TEXT("CNPC_VChangBros")))
		{
			return Brother;
		}
	}
	return nullptr;
}

bool FElysiumNpc::ReadyForUnited() const
{
	// 0x1036e820 `CNPC_VChangBros::ReadyForUnited`: `GetCurSchedule()` (`0x1028a150`) and its
	// schedule id (`CAI_Schedule+0x00`) against 0x15a or 0x15b, false when there is no schedule.
	//
	// The port's schedule set does not carry the two ChangBros `UNITED` programs yet — the
	// comparison is retail's and nothing in `int32` numbers 0x15a/0x15b — so this
	// answers false until they are registered. Not a stub: the two numbers ARE the rule.
	if (!Schedule.IsRunning())
	{
		return false;
	}
	const int32 Number =
		IdSpace(EElysiumIdCategory::Schedule)->GlobalToLocal(Schedule.Current);
	return Number == 0x15a || Number == 0x15b;
}

FElysiumEntity* FElysiumNpc::SelectUnitedNode() const
{
	// 0x1036d100 `CNPC_VChangBros::SelectUnitedNode`: walk the global hint list from
	// `DAT_10925450` following `+0x5d8`, count the nodes whose `m_nHintType` (`+0x5dc`) is 18000,
	// and return the FIRST for `m_ChangType == 0`, the SECOND for `m_ChangType == 1` (the arm is
	// `type == 1 && count > 0`, so it is the match after the first). Any other `m_ChangType` walks
	// the whole list and answers 0.
	if (ChangType == 0)
	{
		return NthHintOfType(18000, 0);
	}
	if (ChangType == 1)
	{
		return NthHintOfType(18000, 1);
	}
	return nullptr;
}

void FElysiumNpc::CoordinateTroops()
{
	// 0x10399610 `CNPC_VMingXiao::CoordinateTroops`, one index per call:
	//
	//   id = m_iCoordinateTentacleID
	//   if (m_rhSeveredTentacles[id] resolves) 0x103998d0(this, tentacle)
	//   if (m_rhProxies[id]          resolves) 0x103999f0(this, proxy)
	//   if (++m_iCoordinateTentacleID > 5) m_iCoordinateTentacleID = 0
	//
	// The index advance and the wrap at 5 are this body; the two per-troop arms are rows of their
	// own and are NOT ported here:
	//   * `0x103998d0` — the severed tentacle's re-aim: skip when its schedule is 0x163/0x165 or
	//     its state is not 2, then a distance and a 2-D dot against `+0x6290/+0x6294` before
	//     `0x1039ef90(tentacle, myOrigin)`;
	//   * `0x103999f0` — the proxy pair's swap: `0x1039aaf0(x, 0)` on both, then two
	//     distance/dot tests against the enemy that re-arm one of them with `0x1039aaf0(x, 1)`.
	// Both are asked here and answer nothing, as the handle seam does.
	const int32 Id = CoordinateTentacleId;
	if (Id >= 0 && Id < static_cast<int32>(UE_ARRAY_COUNT(SeveredTentacles)) && World != nullptr)
	{
		if (World->Resolve(SeveredTentacles[Id]) != nullptr)
		{
			// 0x103998d0, unported.
		}
		if (World->Resolve(Proxies[Id]) != nullptr)
		{
			// 0x103999f0, unported.
		}
	}
	CoordinateTentacleId = Id + 1;
	if (CoordinateTentacleId > 5)
	{
		CoordinateTentacleId = 0;
	}
}

void FElysiumNpc::AlertNearbyAlly(FElysiumEntity* Attacker)
{
	// 0x102bf5d0. The caller walks the NPCs near a victim; `this` is the ally being told and
	// `Attacker` is who did it. Arm for arm:
	//
	//   attacker != NULL
	//   && m_NPCState (+0x5cc0) is 1 (IDLE), 3 (COMBAT) or 0xb (the Troika hunt state)
	//   && attacker->m_pCombatCharacter (+0x9c) != NULL
	//   && IRelationType(attacker) (slot 404, vt+0x650) != 3 (D_LI)
	//   && ( dist2(attacker->GetAbsOrigin(), GetAbsOrigin()) < _DAT_1049aea0
	//        || (FVisible(attacker, 0x2804091, 0, 0) && FInViewCone(attacker)) )
	//   -> 0x102bf560: unless m_bIgnoreDetectedAttack (+0x65f5), record the attacker in
	//      m_hDetectedAttacker (+0x65c0) and set m_flDetectedAttackExpireTime (+0x65c4) to
	//      curtime + _DAT_10454110.
	//
	// 29c's one-line walk read `vt+0x650` as a not-dead test; it is slot 404 `IRelationType`, and
	// the constant compared is `D_LI`. Corrected here and in `social.md`.
	if (Attacker == nullptr || World == nullptr)
	{
		return;
	}
	const EElysiumNpcState State = Mind.State();
	// Retail's third admitted state is the custom `0xb`, which this runtime's state set does not
	// carry (`EElysiumNpcState` stops at `Dead`); UNRECOVERED here, so only 1 and 3 are tested.
	if (State != EElysiumNpcState::Idle && State != EElysiumNpcState::Combat)
	{
		return;
	}
	// `+0x9c m_pCombatCharacter` is CBaseEntity's self-downcast cache: non-null exactly for a
	// `CBaseCombatCharacter`.
	if (Attacker->AsCombatCharacter() == nullptr)
	{
		return;
	}
	if (SpeciesIRelationType(Attacker) == 3)
	{
		return;
	}
	// `_DAT_1049aea0` is the squared radius. Its literal is not in the corpus; the same chain is
	// recovered in `docs/vtmb/combat-and-damage.md` as 150 Source units, which is the constant
	// `ElysiumNpcCond::MeleeNoticeAcceptanceUnits` already carries.
	const double RadiusCm =
		static_cast<double>(ElysiumNpcCond::MeleeNoticeAcceptanceUnits) * ElysiumMove::U;
	const double Dist2 = FVector::DistSquared(Origin, Attacker->Origin);
	const double Now = World->NowSeconds();
	const bool bNear = Dist2 < RadiusCm * RadiusCm;
	if (!bNear
		&& !(FElysiumNpcSenses::IsVisible(*this, *Attacker, Now)
			&& FElysiumNpcSenses::IsInViewCone(*this, *Attacker)))
	{
		return;
	}
	// 0x102bf560, a row of its own; its two writes land on the words the shape map binds here.
	if (!bIgnoreDetectedAttack)
	{
		Senses.Memory.DetectedAttackAttacker = Attacker->Handle;
		// Retail stores an EXPIRY, `curtime + _DAT_10454110`; this runtime stores the stamp and
		// compares it against `ElysiumNpcCond::DetectedAttackRetentionSeconds`. The retail delta's
		// literal is UNRECOVERED.
		Senses.Memory.DetectedAttackTime = Now;
	}
}

int32 FElysiumNpc::SpeciesIRelationType(const FElysiumEntity* Candidate) const
{
	// 0x103a48b0, slot 404's body for CNPC_VFrenzyShadow, CNPC_VPlayerController and
	// CNPC_VWolfMorph:
	//
	//   target == NULL                                   -> 0   D_ER
	//   target == m_hFriendPlayer (+0x60ac, resolved)    -> 3   D_LI
	//   target->m_pCombatCharacter (+0x9c) != NULL
	//     && cc->m_bIsBCCTargetable (+0x1480)
	//     && !cc->m_bScriptHidden   (+0x00f4, 0x100b5190) -> 1  D_HT
	//   otherwise                                        -> 4   D_NU
	if (Candidate == nullptr)
	{
		return 0;
	}
	if (World != nullptr && FriendPlayer.IsSet() && World->Resolve(FriendPlayer) == Candidate)
	{
		return 3;
	}
	const FElysiumCombatCharacter* Combatant = Candidate->AsCombatCharacter();
	if (Combatant != nullptr)
	{
		// `m_bIsBCCTargetable` has no port field and no recovered clearer, so it reads true — the
		// same reading `ElysiumNpcConditions.cpp` already takes for the `+0x1480` term.
		const bool bTargetable = true;
		if (bTargetable && !Candidate->IsHidden())
		{
			return 1;
		}
	}
	return 4;
}

// -------------------------------------------------------------------------------------------------
// The follower pair.
// -------------------------------------------------------------------------------------------------

// slot 293 0x102c5470 `CBaseEntity* GetFollowerBoss()`
FElysiumEntity* FElysiumNpc::GetFollowerBoss()
{
	// 0x102c5470: resolve `m_hFollowerBoss` (`+0x647c`) through the handle table and answer
	// `boss + 0x9c`, the cached `CBaseCombatCharacter*` — so a live boss that is not a combat
	// character answers NULL, exactly as a dead handle does.
	if (World == nullptr || !FollowerBoss.IsSet())
	{
		return nullptr;
	}
	FElysiumEntity* Boss = World->Resolve(FollowerBoss);
	return Boss != nullptr ? Boss->AsCombatCharacter() : nullptr;
}

void FElysiumNpc::SetFollowerBossName(const FElysiumEntity* Boss)
{
	// 0x102c4470:
	//   name = boss->m_pPlayer (+0xa8) ? "!player" : (boss->m_iName (+0x26c) ?: "")
	//   SetFollowerBoss(name)                       -- 0x102c44e0, a row of its own
	//   m_sFollowerBoss (+0x6478) = name[0] ? name : NULL
	//
	// Retail dereferences `boss` without a null test; a null argument faults there and is refused
	// here (NAMED DIVERGENCE: a crash is not a behaviour the port reproduces).
	if (Boss == nullptr)
	{
		return;
	}
	const bool bIsPlayer = World != nullptr && Boss->Handle == World->PlayerHandle();
	const FString Name = bIsPlayer ? FString(TEXT("!player")) : Boss->TargetName;
	// `SetFollowerBoss(const char*)` (`0x102c44e0`) resolves the name through slot 559, refuses
	// `this`, `Error`s on a squad member and sets `m_bfNPCFrenziedFlags |= 0x3008`. It is a row of
	// its own and is NOT ported here; `m_hFollowerBoss` (`+0x647c`) therefore stays unwritten and
	// `GetFollowerBoss()` keeps answering nothing.
	FollowerBossName = Name.IsEmpty() ? FString() : Name;
}

void FElysiumNpc::SetFollowerType(const FString& NewFollowerType)
{
	// 0x102c4640: `0x102c4680(type)`, then `m_sFollowerType (+0x6480) = type[0] ? type : NULL`.
	//
	// 0x102c4680, the whole of it:
	//   0x101e8c90(&DAT_10739d08, type, &backAway +0x6484, &walkTo +0x6488, &runTo +0x648c)
	//       -- the `Npc_Follower_Info` row of Rules.txt (`docs/vtmb/npc-ai/social.md`)
	//   if (walkTo < backAway + 10.0) { DevMsg x4; walkTo = backAway + 10.0 }
	//   if (runTo  < walkTo   + 10.0) { DevMsg x4; runTo  = walkTo   + 10.0 }
	//
	// SEAM: this runtime has no `Npc_Follower_Info` loader, so the row read answers nothing and the
	// three distances keep whatever value they already carry. The CLAMP is the recovered rule and
	// runs either way — it is what keeps the three follower bands from overlapping.
	const float Overlap = GNpcKernelSquadFollowerOverlap;
	if (FollowerDistanceWalkTo < FollowerDistanceBackAway + Overlap)
	{
		UE_LOG(LogElysiumNpcEnt, Warning,
			TEXT("WARNING: FollowerDistanceBackAway (%f) and FollowerDistanceWalkTo (%f) overlap; ")
			TEXT("changing FollowerDistanceWalkTo to %f. NPC is using Follower Type '%s'"),
			FollowerDistanceBackAway, FollowerDistanceWalkTo,
			FollowerDistanceBackAway + Overlap, *NewFollowerType);
		FollowerDistanceWalkTo = FollowerDistanceBackAway + Overlap;
	}
	if (FollowerDistanceRunTo < FollowerDistanceWalkTo + Overlap)
	{
		UE_LOG(LogElysiumNpcEnt, Warning,
			TEXT("WARNING: FollowerDistanceWalkTo (%f) and FollowerDistanceRunTo (%f) overlap; ")
			TEXT("changing FollowerDistanceRunTo to %f. NPC is using Follower Type '%s'"),
			FollowerDistanceWalkTo, FollowerDistanceRunTo,
			FollowerDistanceWalkTo + Overlap, *NewFollowerType);
		FollowerDistanceRunTo = FollowerDistanceWalkTo + Overlap;
	}
	FollowerType = NewFollowerType.IsEmpty() ? FString() : NewFollowerType;
}

FElysiumEntity* FElysiumNpc::ResolveNamedMaster() const
{
	// 0x101a8130, disassembled because the decompiled C folds the RTTI call:
	//
	//   name = *(char**)(this + 0x5f5c); if (!name) return 0;
	//   ent  = gEntList.FindEntityByName(NULL, name, 0, 0);   -- 0x100f7770 over DAT_106eb5d8
	//   if (!ent || !ent->m_pBaseNPC (+0x94)) return 0;
	//   return RTDynamicCast(ent, 0, CBaseEntity_typeinfo 0x10538764, 0x105947c8, 0);
	//
	// LARGELY UNRECOVERED, and stated rather than papered over:
	//   * the key word. `+0x5f5c` is the second `COutputEvent` of the NPC's eight-output block in
	//     this runtime's shape (`ElysiumNpcKernelShapeMap.cpp`, `_IMPLICIT`), so no port member
	//     holds a name there and the read is a local empty key;
	//   * the target type. RTTI type descriptor `0x105947c8` has exactly ONE referrer in the whole
	//     image — this body — so the corpus does not name the class the cast admits;
	//   * the caller. The corpus records no call site, so the receiver class is unconfirmed.
	//
	// What IS recovered and ported: the name gate, the by-name entity lookup and the `+0x94`
	// `m_pBaseNPC` gate (the entity must be an NPC). The final type gate has no recovered answer,
	// so this refuses rather than admitting an NPC retail might reject.
	const FString NamedMasterKey;  // +0x5f5c, UNRECOVERED — see above
	if (NamedMasterKey.IsEmpty() || World == nullptr)
	{
		return nullptr;
	}
	FElysiumEntity* Found = World->FindByName(NamedMasterKey);
	if (Found == nullptr || Found->AsNpc() == nullptr)
	{
		return nullptr;
	}
	return nullptr;  // UNRECOVERED: the RTTI class `0x105947c8` admits
}
