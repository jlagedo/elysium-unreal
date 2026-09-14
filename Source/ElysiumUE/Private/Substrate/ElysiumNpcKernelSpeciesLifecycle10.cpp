#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"

// Story 29d, family **SpeciesLifecycle10** — `CPayphone`'s slot 431, the slot 174/175 base bodies
// and their two species arms, the `CNPC_VGuard1` / `CNPC_VHunter` slot-463 pre-steps, the two
// `CNPC_VVampireBoss`-line `Restore` arms and the two destructors. The three maker `Spawn` bodies
// land on `FElysiumNpcMaker` and are in `Substrate/ElysiumNpcMaker.cpp`, beside that class's other
// slot bodies.
//
// `ElysiumNpcKernelSpeciesLifecycle10.inl` carries the family's reading notes, the corrections it
// made to the checklist's walks and the four `.rdata` cells it read out of the pinned image. The
// walked prose is `docs/vtmb/npc-ai/lifecycle.md`.

namespace
{
	// The retail classes this family's arms key on, spelled once.
	const TCHAR* const GClassPayphone = TEXT("CPayphone");
	const TCHAR* const GClassGargoyle = TEXT("CNPC_VGargoyle");
	const TCHAR* const GClassGhoulCroucher = TEXT("CNPC_VGhoulCroucher");
	const TCHAR* const GClassGuard1 = TEXT("CNPC_VGuard1");
	const TCHAR* const GClassHunter = TEXT("CNPC_VHunter");

	// The two slots this family dispatches, spelled as retail's indices.
	constexpr int32 GStartTouchSlot = 174;   // vtable +0x2b8
	constexpr int32 GTouchSlot = 175;        // vtable +0x2bc, the `+ 700` of `CBaseEntity::Touch`
	// `1037a3c1 CALL dword ptr [EDX + 0x238]` — slot 142, `OnTakeDamage`, dispatched on the PILLAR.
	constexpr int32 GOnTakeDamageSlot = 142;

	// `CBaseEntity::m_pPlayer` (`+0xa8`), the self-downcast cache that is non-null on exactly the
	// player. This runtime has no such cache: "is the player" is `Handle == World->PlayerHandle()`,
	// which is the reading families Senses and Debug10 already made (`IsPlayerRecord`,
	// `CameraSecurityQuerySeeEntity`) and is repeated as a function rather than as a third reading.
	bool SpeciesLifecycle10IsPlayer(const FElysiumNpc& Npc, const FElysiumEntity* Candidate)
	{
		return Candidate != nullptr && Npc.World != nullptr
			&& Candidate->Handle == Npc.World->PlayerHandle();
	}

	// `CBaseEntity::field_0x94`, the cached `CAI_BaseNPC*` — non-null on exactly an NPC. Family
	// Conditions reads it the same way for the comforter test (`ElysiumNpcConditions.cpp:683`).
	bool SpeciesLifecycle10IsNpc(const FElysiumEntity* Candidate)
	{
		return Candidate != nullptr && Candidate->AsNpc() != nullptr;
	}

	// `DAT_1093fac4` / the `werewolf_show_debug` ConVar, as one process-wide word. Retail's IS
	// process-wide — the same shape family SaveRestore10 gave `DAT_1093acac`/`DAT_1093acb0`.
	int32 GWerewolfShowDebug = 0;
}

int32& FElysiumNpc::WerewolfShowDebug()
{
	return GWerewolfShowDebug;
}

// -------------------------------------------------------------------------------------------------
// `CPayphone::NPCThink` — slot 431, `0x101aabf0`.
// -------------------------------------------------------------------------------------------------

FElysiumEntity* FElysiumNpc::ResolveDialogPartner() const
{
	// `101aac0b`..`101aac2c`: the EHANDLE validity triple — index `& 0x1fff`, serial `>> 0xd` against
	// the slot's, and a non-null record. `FElysiumEntityWorld::Resolve` is that triple.
	if (!DialogPartner.IsSet() || World == nullptr)
	{
		return nullptr;
	}
	return const_cast<FElysiumEntityWorld*>(World)->Resolve(DialogPartner);
}

void FElysiumNpc::DialogUpkeepTick()
{
	// SEAM for `FUN_102c1400`. Counted; see the `.inl` for what the real body does and why it is not
	// one of this family's rows.
	++DialogUpkeepTicks;
}

bool FElysiumNpc::PayphoneThink()
{
	// `CPayphone::vfunc431` `0x101aabf0`. The whole think for a payphone; the Troika body never runs.
	if (!IsRetailClass(GClassPayphone))
	{
		return false;
	}

	// **What this body replaces is `NPCThink`, and NOT `NPCInit`.** Retail runs admission, the
	// combat loadout and a director's parked order at SPAWN, inside `NPCInit`; this runtime cannot
	// (creating an item entity inside the world's spawn pass invalidates the array being iterated,
	// and admission would wipe a forced state a director applied ahead of it), so it defers all
	// three onto the first normal-due think — see `FElysiumNpc::Think`, which calls them "the
	// port's own one-shot lifecycle". They are bookkeeping this runtime owes every NPC, not
	// statements of `0x10292de0`, so a species body that replaces the think must still run them or
	// the entity is never admitted and can never own its own body.
	//
	// It is not theoretical: without this, `FElysiumNpcMind::IsAcquisitionAllowed` refuses every
	// claim on an unadmitted mind, so `BeginDialogueBodySession` refuses and **a payphone cannot be
	// talked to at all** — which is what `Elysium.Substrate.DialogueCamera.RetailChain` caught.
	// All three are guarded one-shots, so calling them on every payphone pass is free.
	RunAdmissionBarrier();
	ResolveLoadout();
	ReplayDeferredScriptedOrder();

	// 1. `101aabf4 PUSH 0x0` / `101aabf8 CALL dword ptr [EAX + 0x3e8]` — slot 250
	//    `StudioFrameAdvance(0.0)`, and `101aac07 FSTP ST0` throws the returned interval away.
	(void)StudioFrameAdvance(0.0f);

	const double Now = World != nullptr ? World->NowSeconds() : 0.0;

	// 2. `101aabfe MOV EAX,dword ptr [ESI + 0xfe8]` — the partner arm.
	if (const FElysiumEntity* PartnerEntity = ResolveDialogPartner())
	{
		// `101aac31 CALL 0x100125bc` — the dialogue upkeep tick, unconditional on this arm.
		DialogUpkeepTick();

		// `101aac36 MOV EDI,[EBX + 0xff0]` — the PARTNER's `m_IdealActivity`. Only an NPC carries
		// the kernel's activity words in this runtime; a partner that is any other leaf answers the
		// activity it has, which is zero, and zero is exactly what retail reads out of a
		// `CBaseEntity` whose `+0xff0` was never written.
		const FElysiumNpc* PartnerNpc = PartnerEntity->AsNpc();
		const int32 PartnerIdealActivity = PartnerNpc != nullptr ? PartnerNpc->IdealActivityNumber : 0;

		// `101aac42 CMP EDI,EAX / JZ` — the sequence is re-selected ONLY on an activity change.
		if (PartnerIdealActivity != IdealActivityNumber)
		{
			SetIdealActivity(PartnerIdealActivity);                                 // 0x10272650
			SequenceNumber = SelectWeightedSequenceForActivity(PartnerIdealActivity);  // (act, -1)
			ResetSequenceInfo();                                                    // 0x10090950
		}

		// `101aac65 MOV EAX,[EBX + 0x6f8] / 101aac6c MOV [ESI + 0x6f8],EAX` — the partner's
		// `m_flCycle`, copied on EVERY pass. This is the frame-accurate half of the mirror.
		SequenceCycle = PartnerNpc != nullptr ? PartnerNpc->SequenceCycle : 0.f;

		// `101aac7b FADD float ptr [0x10450aa4]` — 0.01 s.
		NextThink = static_cast<float>(Now + PayphoneMirrorThinkSeconds);
		++PayphoneMirrorPasses;
		return true;
	}

	// 3. `101aac8a` — the no-partner arm. `IsInDialog()` gates only the tick.
	if (IsInDialog())
	{
		DialogUpkeepTick();
	}
	// `101aac9c PUSH 0x1` — `ACT_IDLE`, UNCONDITIONALLY, outside the `IsInDialog` test.
	SetIdealActivity(PayphoneIdleActivity);
	// `101aacae FADD float ptr [0x1044bef8]` — 0.25 s.
	NextThink = static_cast<float>(Now + PayphoneIdleThinkSeconds);
	++PayphoneIdlePasses;
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slots 174 `StartTouch` and 175 `Touch`.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::BaseEntityStartTouch(FElysiumEntity* Other)
{
	// `CBaseEntity::StartTouch` `0x100a49d0`. The entire body past the scope frame is the parent
	// forward: `if (m_pParent resolves live) parent->vtable[+0x2b8](other)`.
	if (!MoveParent.IsSet() || World == nullptr)
	{
		return;
	}
	FElysiumEntity* Parent = World->Resolve(MoveParent);
	if (Parent == nullptr)
	{
		return;
	}
	++ParentTouchPropagations;
	if (FElysiumNpc* ParentNpc = Parent->AsNpc())
	{
		// Retail dispatches slot 174 virtually, so a parent with a species override takes it.
		ParentNpc->StartTouchSpecies(Other);
	}
	// A parent that is not an NPC carries no slot 174 in this runtime; the dispatch is counted above
	// and nothing runs, which is stated rather than approximated.
}

void FElysiumNpc::BaseEntityTouch(FElysiumEntity* Other)
{
	// `CBaseEntity::Touch` `0x100a4af0`. Two steps, and the ORDER is the fact: the touch think
	// function FIRST, the parent forward second.
	//
	// SEAM: `m_pfnTouch` (`+0x1ac`) has no port member — this runtime has no per-entity touch
	// callback pointer — so the call is counted and nothing runs.
	++TouchFunctionCalls;

	if (!MoveParent.IsSet() || World == nullptr)
	{
		return;
	}
	FElysiumEntity* Parent = World->Resolve(MoveParent);
	if (Parent == nullptr)
	{
		return;
	}
	++ParentTouchPropagations;
	if (FElysiumNpc* ParentNpc = Parent->AsNpc())
	{
		ParentNpc->TouchSpecies(Other);   // the parent's slot 175 (`vtable + 700`)
	}
}

void FElysiumNpc::StartTouchSpecies(FElysiumEntity* Other)
{
	// Slot 174's vtable dispatch, as a table lookup. `CNPC_VGhoulCroucher` is the census's only
	// override of the slot on the NPC line.
	if (IsRetailClass(GClassGhoulCroucher)
		&& ElysiumNpcKernelClass::OverrideOf(RetailClass(), GStartTouchSlot) != nullptr)
	{
		GhoulCroucherStartTouch(Other);
		return;
	}
	BaseEntityStartTouch(Other);
}

void FElysiumNpc::TouchSpecies(FElysiumEntity* Other)
{
	// Slot 175's dispatch. `CNPC_VGargoyle` is the census's only override on the NPC line.
	if (IsRetailClass(GClassGargoyle)
		&& ElysiumNpcKernelClass::OverrideOf(RetailClass(), GTouchSlot) != nullptr)
	{
		GargoyleTouch(Other);
		return;
	}
	BaseEntityTouch(Other);
}

void FElysiumNpc::OnTouchStart(const FElysiumEntityHandle& Activator)
{
	// This runtime's touch-begin notification IS retail's slot 174, so the whole slot-174 chain hangs
	// off it. `FElysiumEntity::OnTouchStart` is empty, so there is no base behaviour to keep.
	FElysiumEntity* Other = World != nullptr ? World->Resolve(Activator) : nullptr;
	StartTouchSpecies(Other);
}

void FElysiumNpc::GhoulCroucherStartTouch(FElysiumEntity* Other)
{
	// `CNPC_VGhoulCroucher::StartTouch` `0x1037bf60`.

	// 1. `1037bfd0 CALL 0x10009caf` — `CBaseEntity::StartTouch(other)`, FIRST.
	BaseEntityStartTouch(Other);

	// 2. `1037bfd5`..`1037bff4`. The player pointer resolved here is reused by step 3, which is why
	//    it is a local and not re-derived.
	//
	//    DIVERGENCE, named: retail reads `[EDI + 0x94]` with `EDI == 0` when the toucher is null
	//    (`1037bfd9 XOR EBX,EBX / JMP 0x1037bfe7`) and faults. This port refuses the arm. No
	//    `FElysiumEntityWorld` touch path can hand a null toucher in, so the guard is unreachable.
	const bool bTouchIsPlayer = SpeciesLifecycle10IsPlayer(*this, Other);
	if (Other != nullptr && (bTouchIsPlayer || SpeciesLifecycle10IsNpc(Other)))
	{
		OnDisturbed(Other);
	}

	// 3. `1037bff9`..`1037c036`. `m_bSpawnBurning` (+0x6665), the toucher being the PLAYER (the
	//    `+0xa8` pointer from step 2 — an NPC toucher carries a null one and cannot burn), and
	//    `m_flNextTouchBurnTime` (+0x666c) STRICTLY behind curtime.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	if (bGhoulSpawnBurning && bTouchIsPlayer && GhoulNextTouchBurnTime < Now)
	{
		// `1037c022 FADD double ptr [0x1044ffd0]` — 5.0 s, and the restamp happens BEFORE the burn
		// (`1037c030 FSTP` precedes `1037c036 CALL`). `BurnPlayer` is family SpeciesMisc10's body
		// for `0x1037c090`; this row supplies the 5.0 against `OnVictimHitByMe`'s 10.0.
		GhoulNextTouchBurnTime = Now + GhoulTouchBurnIntervalSeconds;
		BurnPlayer(Other, GhoulTouchBurnDamage);
	}
}

void FElysiumNpc::GargoyleTouch(FElysiumEntity* Other)
{
	// `CNPC_VGargoyle::Touch` `0x1037a270`.
	const FString Classname = Other != nullptr && Other->Def != nullptr
		? Other->Def->Classname : FString();

	// 1. The two `FClassnameIs` compares, in retail's order. Family Misc already carries them for
	//    this class's slot-24 body; called, not restated.
	if (GargoyleHitsPillar(Classname))
	{
		// 2. `CVDmg_t`: `SetSrc(this)`, `Set(1, 0x80, 10)`, `m_iToHitSuccesses = 1`.
		FGargoylePillarHit Hit;
		Hit.Pillar = Other->Handle;                              // retail's inflictor, and the victim
		Hit.Family = GargoylePillarDamageFamily;                 // 1 = EElysiumDmgFamily::Lethal
		Hit.DiceAmount = GargoylePillarDiceAmount;               // m_iDiceAmt = 10
		Hit.ToHitSuccesses = GargoylePillarToHitSuccesses;       // m_iToHitSuccesses = 1
		Hit.DamageTypes = GargoylePillarDamageTypes;             // m_bdmgTypes = DMG_CLUB
		Hit.Damage = GargoylePillarDamageScale;                  // the packet's scalar, 1.0

		FElysiumDmg Dmg;
		Dmg.Family = static_cast<EElysiumDmgFamily>(GargoylePillarDamageFamily);
		Dmg.BaseDamage = GargoylePillarDiceAmount;               // word 1, `m_iDiceAmt`
		Dmg.ExtraInput = GargoylePillarToHitSuccesses;           // word 3, `m_iToHitSuccesses`
		Dmg.DmgMask = GargoylePillarDamageTypes;                 // word 4, `m_bdmgTypes`
		Dmg.Source = Handle;                                     // `SetSrc(this)`, word 13

		FElysiumTakeDamageInfo Info;
		Info.Dmg = &Dmg;
		// `1037a3a5 PUSH EBP` — the ATTACKER is the gargoyle. `1037a3a7 PUSH ESI`, pushed first and
		// therefore argument 0, makes the PILLAR the inflictor; this runtime's packet carries no
		// inflictor word, so it is recorded on the hit above instead of being dropped.
		Info.Attacker = Handle;
		Info.Damage = GargoylePillarDamageScale;
		Info.DamageBits = 0;                                     // `1037a39c PUSH 0x0`
		Info.AmmoType = INDEX_NONE;                              // `1037a399 PUSH -0x1`

		// 3. Slot 142 `OnTakeDamage` dispatched ON THE PILLAR.
		if (FElysiumNpc* PillarNpc = Other->AsNpc())
		{
			(void)GOnTakeDamageSlot;
			PillarNpc->OnTakeDamage(&Info);
			Hit.bDispatched = true;
		}
		GargoylePillarHits.Add(MoveTemp(Hit));
	}

	// 4. `1037a3d0` — BOTH paths end in `CBaseEntity::Touch(other)`.
	BaseEntityTouch(Other);
}

// -------------------------------------------------------------------------------------------------
// Slot 463 `OnStateChange` — the `CNPC_VGuard1` and `CNPC_VHunter` pre-steps.
// -------------------------------------------------------------------------------------------------

FElysiumEntity* FElysiumNpc::GetEnemyEntity() const
{
	// `CAI_BaseNPC::FUN_101a67e0` `0x101a67e0`, vtable `+0x29c` (slot 167): `m_hEnemy` resolved
	// through the global entity table, null when the handle is stale.
	if (!Senses.Memory.Enemy.IsSet() || World == nullptr)
	{
		return nullptr;
	}
	return const_cast<FElysiumEntityWorld*>(World)->Resolve(Senses.Memory.Enemy);
}

const TCHAR* FElysiumNpc::PlayerHateRelationshipSpec()
{
	// `s_player_D_HT_10_1063bc28` — `'player D_HT 10'`, with spaces, read off the listing at
	// `1037e2d8` / `10388c48`. Lower-case `player`, unlike `CNPC_VCop`'s `"Player D_HT 10"`
	// (`0x106366f4`); family SpeciesMisc10 recorded the same difference for `0x1037e2d0`.
	return TEXT("player D_HT 10");
}

void FElysiumNpc::HunterHatePlayer()
{
	// `FUN_10388c40` `0x10388c40` — `CNPC_VGuard1`'s `0x1037e2d0` (family SpeciesMisc10's
	// `Guard1HatePlayer`) minus the `+0x6660` latch byte, and the byte is the only difference.
	//
	// `thunk_FUN_10273790(this, "player D_HT 10", 0)` — `InputSetRelationship`, whose port is
	// `FElysiumNpc::InputSetRelationship` and whose parser already takes the three-token grammar.
	FElysiumInputArgs Args;
	Args.Param = FElysiumVariant::String(PlayerHateRelationshipSpec());
	Args.Activator = Handle;
	Args.Caller = Handle;
	Args.Input = FName(TEXT("SetRelationship"));
	InputSetRelationship(Args);
	++PlayerHateRelationshipSets;
}

void FElysiumNpc::StateChangeSpeciesPreStep(EElysiumNpcState OldState, EElysiumNpcState NewState)
{
	if (IsRetailClass(GClassGuard1))
	{
		// `CNPC_VGuard1::OnStateChange` `0x1037d020`, arm (1). Unconditional on the states: the only
		// question is whether my enemy is the player. `GetEnemy()` is dispatched TWICE (`1037d026`,
		// `1037d034`) and retail caches neither call, so both are made.
		if (GetEnemyEntity() != nullptr)
		{
			const FElysiumEntity* Enemy = GetEnemyEntity();
			if (SpeciesLifecycle10IsPlayer(*this, Enemy))
			{
				// `0x1037e2d0` is family SpeciesMisc10's `Guard1HatePlayer()` — six other
				// callers in `vfunc461` reach it too. Called, not restated.
				Guard1HatePlayer();
				++PlayerHateRelationshipSets;
			}
		}
		return;
	}

	if (IsRetailClass(GClassHunter))
	{
		// `CNPC_VHunter::OnStateChange` `0x10388880`, two independent arms in retail's order.
		// Retail's states are `m_NPCState` ids; 2 is COMBAT.
		if (GetEnemyEntity() != nullptr && GetEnemyEntity() != nullptr)
		{
			FElysiumEntity* Enemy = GetEnemyEntity();
			if (SpeciesLifecycle10IsPlayer(*this, Enemy) && NewState == EElysiumNpcState::Combat)
			{
				HunterHatePlayer();                                 // 0x10388c40
				// `0x1017f7b0`, ported by family Conditions with its receiver correction: the
				// refcount is the PLAYER's `+0x1d14`, this runtime's
				// `FElysiumPoliceState::HuntersInPursuit`. Called, not restated.
				if (FElysiumPlayer* PlayerRecord = World != nullptr ? World->FindPlayer() : nullptr)
				{
					OnHunterPursuitStart(*PlayerRecord);
				}
				++HunterPursuitStarts;
				// `1038890a MOV EAX,[EAX]` off the player's `GetRefEHandle()` — the player's own
				// handle, cached on the hunter.
				HunterPursuitPlayer = Enemy->Handle;
			}
		}
		if (OldState == EElysiumNpcState::Combat && HunterPursuitPlayer.IsSet())
		{
			FElysiumEntity* Pursued = World != nullptr ? World->Resolve(HunterPursuitPlayer) : nullptr;
			if (SpeciesLifecycle10IsPlayer(*this, Pursued))
			{
				// The CLEAR comes first (`1038891e`), the release second.
				HunterPursuitPlayer = FElysiumEntityHandle();
				if (FElysiumPlayer* PlayerRecord = World != nullptr ? World->FindPlayer() : nullptr)
				{
					OnHunterPursuitStop(*PlayerRecord);             // 0x1017f830
				}
				++HunterPursuitStops;
			}
		}
		return;
	}
}

// -------------------------------------------------------------------------------------------------
// Slot 127 `Restore` — the two `CNPC_VVampireBoss`-line species arms.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpc::AndreiBloodRestore(void* Archive)
{
	// `CNPC_VAndreiBlood::Restore` `0x1035cf80`. One call between the scope-frame push and pop, and
	// its `EAX` survives to the `RET 0x4`: the base's answer IS this body's answer, and there is no
	// restore-time datum of its own. An empty species row is a fact, not an unwalked body.
	return VampireBossRestore(Archive);
}

const TCHAR* FElysiumNpc::ChangPowerupEmitterName()
{
	return TEXT("chang_powerup_emitter");   // `0x10630e54`
}

const TCHAR* FElysiumNpc::ChangSpineEmitterName()
{
	return TEXT("chang_spine_emitter");     // `0x10630e3c`
}

int32 FElysiumNpc::ChangBrosRestore(void* Archive)
{
	// `CNPC_VChangBros::Restore` `0x1036b170`, shared by `CNPC_VChangBros`, `CNPC_VChangBrosBlade`
	// and `CNPC_VChangBrosClaw`.
	const int32 Result = VampireBossRestore(Archive);   // `1036b1c9`, and `EDI` keeps its answer
	// `1036b1ce FLD float ptr [0x104ada44]` / `1036b1db FSTP float ptr [ESI + 0x64b8]` — the store
	// is issued before the first emitter call.
	JumpGravity = ChangBrosJumpGravity;                 // +0x64b8, 2.3f
	SetBodyEmitterName(0, ChangPowerupEmitterName());   // `1036b1d9 PUSH 0x0`
	SetBodyEmitterName(1, ChangPowerupEmitterName());   // `1036b1ef PUSH 0x1`
	SetBodyEmitterName(2, ChangSpineEmitterName());     // `1036b1fd PUSH 0x2`
	return Result;                                      // `1036b210 MOV EAX,EDI`
}

// -------------------------------------------------------------------------------------------------
// The two destructors.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::DestroyAndreiBlood()
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

void FElysiumNpc::DestroyWerewolf()
{
	// `CNPC_VWerewolf::~CNPC_VWerewolf` `0x103ca7c0`.

	// The one thing outside this object the body touches: the debug global and the ConVar behind it.
	GWerewolfShowDebug = 0;

	// The five outputs, in the listing's order: `m_OnTeleportIn`, `m_OnTeleportOut`,
	// `m_OnFinishCrushAnimation`, `m_OnBeginCrushAnimation`, `m_OnConditionDeathTriggered`.
	OutputListDestroys += 5;

	// The hint-data vector (`+0x6714`, count `+0x6720`, stride 0x48), walked BACKWARDS from
	// `count - 1` with `0x103dc5b0` on each record, then the count zeroed and `0x103dc220` run over
	// the vector. The backwards walk is recorded because it is the recovered order.
	WerewolfHintTeardownOrder.Reset();
	for (int32 Index = WerewolfHintGroundpoints.Num() - 1; Index >= 0; --Index)
	{
		WerewolfHintTeardownOrder.Add(Index);
	}
	WerewolfHintGroundpoints.Reset();   // `*(undefined4 *)&this->field_0x6720 = 0`

	// The three `CUtlMemory` teardowns under the "grow size is not -1" tests are allocator work with
	// nothing a program can observe, and `~CAI_BaseNPCTroika` is the world's reap here.
}
