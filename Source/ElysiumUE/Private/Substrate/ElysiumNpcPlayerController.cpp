#include "Substrate/ElysiumNpcPlayerController.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumLaw.h"
#include "Substrate/ElysiumNpcLifecycle19_2Shared.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumRelationships.h"

#include "Components/SkeletalMeshComponent.h"

// `CNPC_VPlayerController` — story 5 fold A2. Every body below is the retail body at the address its
// comment names, read off the listing (`vtmb_asm`); the walked prose is
// `docs/vtmb/npc-ai/lifecycle.md`, "The player controller".

namespace
{
	// Retail's two `DevWarning` strings on the extra-model pair, quoted so the port's log line is the
	// one a reader of the binary would search for.
	const TCHAR* const GControllerWarnAddExtraModels =
		TEXT("Player Controller NPC adding extra animations, but is not attached to a player. This ")
		TEXT("is probably bad.");
	const TCHAR* const GControllerWarnRemoveExtraModels =
		TEXT("Player Controller NPC removing extra animations, but is not attached to a player. ")
		TEXT("This is probably bad.");

	// `SetName("playercontroller")` — the literal at `0x10566560` `Spawn` pushes at `0x103a452a`.
	const TCHAR* const GControllerName = TEXT("playercontroller");
	// `AddFlag2(0x10)` (`PUSH 0x10`, `0x103a4549`).
	constexpr uint32 GControllerFlags2 = 0x10u;
	// `AddClassRelationship(1, 3, 0)` (`PUSH 0 / PUSH 3 / PUSH 1`, `0x103a4519`): `Class_T` 1, the
	// player's class, liked (`D_LI` 3) at priority 0. The port keys `Class_T` 1 as the `player`
	// class row, the same mapping the Werewolf/Zombie/GhoulCroucher `NPCInit` ports use.
	const TCHAR* const GControllerPlayerClass = TEXT("player");
	constexpr int32 GControllerPlayerClassPriority = 0;
	// `PreSelectSchedule`'s selector trace: `+0x1b2c = 2` on both arms, and on the idle arm the file
	// `0x1064bc7c` and line `0xe6` into `+0x1b30` / `+0x1b34`.
	constexpr int32 GControllerPreSelectTraceId = 2;
	constexpr int32 GControllerPreSelectLine = 0xe6;
	// `mov eax,0x6b` — schedule 107, `SCHED_TROIKA_IDLE_DISPOSITION` once the controller's space
	// (`CNPC_VVampire`'s) resolves it up the parent chain.
	constexpr int32 GControllerIdleSchedule = 0x6b;
	// Retail `m_NPCState` 1, `NPC_STATE_IDLE` (`DEC EAX; JZ` at `0x103a46c0`).
	constexpr int32 GControllerRetailStateIdle = 1;
	// `InvestigateMode` / `InvestigateModeCombat` (`+0x6338` / `+0x633c`), both 6 (`Anything`).
	constexpr int32 GControllerInvestigateMode = 6;
	// Slot 404's `Disposition_t` answers.
	constexpr int32 GControllerD_ER = 0;
	constexpr int32 GControllerD_HT = 1;
	constexpr int32 GControllerD_LI = 3;
	constexpr int32 GControllerD_NU = 4;

	// `CBaseEntity::GetDebugName` — the targetname, else the classname.
	FString ControllerDebugName(const FElysiumEntity& Entity)
	{
		if (!Entity.TargetName.IsEmpty())
		{
			return Entity.TargetName;
		}
		return Entity.Class != nullptr ? Entity.Class->ClassName.ToString() : FString();
	}
}

// Slot 72: `0x103751a0`.
bool FElysiumNpcPlayerController::Slot72(int32 Discipline)
{
	// `XOR AL,AL; RET 4` — the whole body.
	(void)Discipline;
	return false;
}

// Slot 103: `0x103a4510`.
void FElysiumNpcPlayerController::Spawn()
{
	// `CNPC_VPlayerController::Spawn`, in retail's order:
	//
	//   1. `CALL 0x10014876` -> `CNPC_VVampire::Spawn` `0x103c4ef0` (DIRECT). That body is
	//      `AddClassRelationship(1, 1, 0)` then `CNPC_VHuman::Spawn` `0x10384690` (which runs the
	//      Troika `Spawn` `0x10298d30`, `NPCInit` included) then `CapabilitiesAdd(0x40)`; both are
	//      ported (Spawn19, `ElysiumNpcSpawn19Species.cpp`).
	FElysiumNpcVampire::Spawn();                                           // 0x103a4514 -> 0x10014876

	//   2. `AddClassRelationship(1, 3, 0)` — the player's class, liked. It REPLACES the `(1, 1, 0)`
	//      the Vampire body wrote a moment earlier (the class row is keyed on the class), which is
	//      why the controller is friendly where a plain vampire hates.
	Relationships.AddClassRelationship(GControllerPlayerClass, EElysiumRelationship::Like,
		GControllerPlayerClassPriority);

	//   3. `MOV byte ptr [ESI+0x63f0],0x1` — `m_bForceFrequentThink`, written DIRECTLY (not through
	//      slot 416; `NPCInit` below writes it again through the slot).
	bForceFrequentThink = true;

	//   4. `SetName("playercontroller")` (`0x10003229` over the pooled string) — the stand-in's
	//      targetname. `!playercontroller` itself resolves through the player's `m_hControllerNPC`
	//      (`+0x1db0`), not through this name (`lifecycle.md`).
	if (World != nullptr)
	{
		World->RenameEntity(*this, GControllerName);                       // 0x103a4537 -> 0x10002095 SetName
	}
	else
	{
		TargetName = GControllerName;
	}

	//   5. `AddFlag2(0x10)` (`0x103a454d` -> `0x100b3840`) on `m_fFlags2`.
	AddFlag2(GControllerFlags2);

	// **Named modernization — the non-solid stand-in** (header): the motor `FElysiumNpc::Spawn`
	// built ignores other character capsules.
	SetIgnoreCharacterCollision(true);
}

// Slot 138: `0x103a4890`.
int32 FElysiumNpcPlayerController::Classify()
{
	// `MOV EAX,2; RET`.
	return 2;
}

// Slot 245: `0x103a49c0`.
void FElysiumNpcPlayerController::AddExtraAnimationModels(FElysiumEntity* ExtraModel,
	const TCHAR* AttachmentA, const TCHAR* AttachmentB, int32 FlagsA, int32 FlagsB)
{
	// `CNPC_VPlayerController::AddExtraAnimationModels`, in retail's order:
	//   1. `CBaseAnimating::AddExtraAnimationModels(...)` `0x1008e0a0`, DIRECT — the base's own list.
	//      **SEAM**: this runtime composes extra models through the character catalog, so the base
	//      half records the request on `ExtraAnimationModels` and touches nothing else.
	//   2. slot 97 `GetOwnerEntity()`; when it resolves AND its `+0xa8` (`m_pPlayer`, the self-downcast
	//      cache only `CBasePlayer`'s constructor fills) is set, forward the same five arguments to
	//      that player's slot 245 (`+0x3d4`) — a VIRTUAL dispatch on the player.
	//   3. otherwise `DevWarning` and stop.
	FExtraAnimationModelRequest Request;
	Request.Model = ExtraModel != nullptr ? ExtraModel->Handle : FElysiumEntityHandle();
	Request.AttachmentA = AttachmentA != nullptr ? FString(AttachmentA) : FString();
	Request.AttachmentB = AttachmentB != nullptr ? FString(AttachmentB) : FString();
	Request.FlagsA = FlagsA;
	Request.FlagsB = FlagsB;
	Request.bForwardedToMaster = OwnerIsThePlayer();
	const bool bForwarded = Request.bForwardedToMaster;
	ExtraAnimationModels.Add(MoveTemp(Request));

	if (!bForwarded)
	{
		UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s"), GControllerWarnAddExtraModels);
		return;
	}
	if (FElysiumPlayer* Owner = World != nullptr ? World->FindPlayer() : nullptr)
	{
		Owner->AddExtraAnimationModels(ExtraModel, AttachmentA, AttachmentB, FlagsA, FlagsB);
	}
}

// Slot 246: `0x103a4a60`.
void FElysiumNpcPlayerController::RemoveExtraAnimationModels()
{
	// The exact inverse, read off the listing: clear the base's own list (`0x1008e310` through its
	// thunk `0x1000ede0`, DIRECT), ask slot 97 for the owner, and TAIL JUMP into the owner's `+0xa8`
	// object's slot 246 (`+0x3d8`). With no owner, or an owner that is not a player, the warning is
	// the whole rest of the body.
	ExtraAnimationModels.Reset();

	if (!OwnerIsThePlayer())
	{
		UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s"), GControllerWarnRemoveExtraModels);
		return;
	}
	if (FElysiumPlayer* Owner = World != nullptr ? World->FindPlayer() : nullptr)
	{
		Owner->RemoveExtraAnimationModels();
	}
}

const TCHAR* FElysiumNpcPlayerController::TookLifeReasonFormat()
{
	// `0x1064bcc0`.
	return TEXT("CNPC_VPlayerController::Event_TookLife %s");
}

// Slot 300: `0x103a4950`.
void FElysiumNpcPlayerController::Event_TookLife(FElysiumEntity* Victim, bool bArg2, bool bArg3)
{
	// **RETAIL CORRECTION (fold A2).** The port read this body as an AI-event dispatch (type 4,
	// priority -1.0) on an unported "controller object". The listing says otherwise:
	//
	//   owner = GetOwnerEntity();                           // slot 97, `CALL [EAX+0x184]`
	//   if (owner == NULL) return;
	//   player = owner->m_pPlayer;                          // `+0xa8`
	//   if (player == NULL) return;
	//   name = victim ? victim->GetDebugName() : "UNKNOWN"; // `0x1000b5cd`, literal `0x1057bd88`
	//   reason = va("CNPC_VPlayerController::Event_TookLife %s", name);   // `0x100067f3`
	//   player->SetCriminalLevel(4, -1.0f, reason);         // `0x1000fa33` -> `0x1017e150`
	//
	// `0x1017e150` is the player's criminal-level setter, ported as `ElysiumLaw::SetCriminalLevel`:
	// a kill the stand-in makes is the PLAYER's crime. The reason string is a debug tag the port's
	// setter does not carry; it is built and kept on `LastTookLifeReason` for the record.
	(void)bArg2;
	(void)bArg3;
	if (World == nullptr || !OwnerIsThePlayer())
	{
		return;
	}
	FElysiumPlayer* Player = World->FindPlayer();
	if (Player == nullptr)
	{
		return;
	}
	const FString Name = Victim != nullptr ? ControllerDebugName(*Victim) : FString(TEXT("UNKNOWN"));
	LastTookLifeReason = FString::Printf(TEXT("CNPC_VPlayerController::Event_TookLife %s"), *Name);
	ElysiumLaw::SetCriminalLevel(*Player, TookLifeCriminalLevel, ElysiumLaw::DeriveDuration);
}

// Slot 362: `0x10375140`.
bool FElysiumNpcPlayerController::FInViewCone(const FVector& PointCm)
{
	(void)PointCm;
	return false;
}

// Slot 363: `0x10375120`.
bool FElysiumNpcPlayerController::FInViewCone(FElysiumEntity* Candidate)
{
	(void)Candidate;
	return false;
}

// Slot 364: `0x10375180`.
bool FElysiumNpcPlayerController::FInAimCone(const FVector& TargetCm)
{
	(void)TargetCm;
	return false;
}

// Slot 365: `0x10375160`.
bool FElysiumNpcPlayerController::FInAimCone(FElysiumEntity* AimTarget)
{
	(void)AimTarget;
	return false;
}

// Slot 404: `0x103a48b0`.
int32 FElysiumNpcPlayerController::IRelationType(FElysiumEntity* Candidate)
{
	// The whole slot; it never chains the Troika body `0x10299da0`.
	return PlayerControllerIRelationType(Candidate);
}

int32 FElysiumNpcPlayerController::PlayerControllerIRelationType(const FElysiumEntity* Candidate) const
{
	// `0x103a48b0` (`docs/vtmb/npc-ai/social.md`):
	//
	//   target == NULL                                   -> 0   D_ER
	//   target == m_hFriendPlayer (+0x60ac, resolved)    -> 3   D_LI
	//   target->m_pCombatCharacter (+0x9c) != NULL
	//     && cc->m_bIsBCCTargetable (+0x1480)
	//     && !cc->m_bScriptHidden   (+0x00f4, 0x100b5190) -> 1  D_HT
	//   otherwise                                        -> 4   D_NU
	if (Candidate == nullptr)
	{
		return GControllerD_ER;
	}
	if (World != nullptr && FriendPlayer.IsSet() && World->Resolve(FriendPlayer) == Candidate)
	{
		return GControllerD_LI;
	}
	const FElysiumCombatCharacter* Combatant = Candidate->AsCombatCharacter();
	if (Combatant != nullptr)
	{
		// `m_bIsBCCTargetable` has no port field on a non-NPC combat character and no recovered
		// clearer, so it reads true — the same reading `ElysiumNpcConditions.cpp` takes for `+0x1480`.
		const bool bTargetable = true;
		if (bTargetable && !Candidate->IsHidden())
		{
			return GControllerD_HT;
		}
	}
	return GControllerD_NU;
}

// Slot 420: `0x103a4580`.
void FElysiumNpcPlayerController::NPCInit()
{
	// `CNPC_VPlayerController::NPCInit`, in retail's order:
	//   1. `CALL 0x1000c531` -> `CAI_BaseNPCTroika::NPCInit` `0x1029a0b0`, DIRECT.
	TroikaNPCInit();
	//   2. `+0x641c = 0` — `m_flNextFleeSoundTime`.
	Senses.Memory.NextFleeSoundTime = 0.0;
	//   3. `+0x6348..+0x6358 = 999999` — the five law thresholds.
	NpcKernelLifecycle19_2Shared::Lifecycle19_2LawNever(*this);
	//   4. `+0x6360` / `+0x6361` / `+0x6364` — story 1's dead `SecureType` scrambler over 0, stored
	//      PLAIN (header); then 5. `+0x6368 = 0`, the witnessed supernatural channel.
	SeedCriminalLevelWitnessed();
	//   6. `+0x6338` and `+0x633c = 6`.
	InvestigateMode = GControllerInvestigateMode;
	InvestigateModeCombat = GControllerInvestigateMode;
	//   7. slot 416 `SetForceFrequentThink(1)` through `vt+0x680` — VIRTUAL.
	SetForceFrequentThink(true);
	//   8. `m_hFriendPlayer (+0x60ac)` = slot 97's owner handle (`owner->vt+4` GetRefEHandle), or -1.
	const FElysiumEntityHandle Owner = GetOwnerEntity();
	FriendPlayer = (World != nullptr && World->Resolve(Owner) != nullptr)
		? Owner : FElysiumEntityHandle::Invalid();
	//   9. `+0x5b84 = 0` (the frenzied word), `m_pSenses+0x80 = 0`, `+0x1480 = 0`.
	SetFrenziedWord(0);
	Senses.bCanPerformSenses = false;
	bIsBccTargetable = false;
}

// Slot 431: `0x103a4700`, shared by `CNPC_VWolfMorph`.
void FElysiumNpcPlayerController::NPCThink()
{
	// The decompilation is damaged; the listing is two instructions of substance:
	//   `CALL 0x10002a7c`        -> `CAI_BaseNPCTroika::NPCThink` `0x10292de0`, DIRECT;
	//   `JMP dword ptr [EAX+0x998]` -> slot 614 `ResetThinkTimers`, VIRTUAL, as a tail call.
	// Every class in the image fills slot 614 with `0x102c23f0`; the port's one body is the virtual
	// `FElysiumNpc::ResetThinkTimers`.
	FElysiumNpc::NPCThink();                                                // 0x103a4703 CALL 0x10002a7c
	ResetThinkTimers(World != nullptr ? World->NowSeconds() : 0.0);         // 0x103a470d JMP [EAX+0x998] slot 614
}

// Slot 437: `0x103a46b0`, shared by both children.
int32 FElysiumNpcPlayerController::PreSelectSchedule()
{
	// `MOV EAX,[ECX+0x5cc0]` (`m_NPCState`), `MOV [ECX+0x1b2c],2` on BOTH arms, then:
	//   state != IDLE -> `JMP 0x1000df2b` -> `CAI_BaseNPCTroika::PreSelectSchedule` `0x102ae920`,
	//                    DIRECT (a tail jump);
	//   state == IDLE -> trace `NPC_VPlayerController.cpp:0xe6` and answer `0x6b` (107).
	const int32 State = NpcStateRetail();                         // 0x103a46b0
	SelectScheduleSelector = GControllerPreSelectTraceId;         // 0x103a46b6 MOV [ECX+0x1b2c],2
	if (State != GControllerRetailStateIdle)                       // 0x103a46c0 DEC / 0x103a46c1 JZ
	{
		return FElysiumNpc::PreSelectSchedule();                   // 0x103a46c3 JMP 0x1000df2b
	}
	RecordScheduleEvent(FString::Printf(
		TEXT("PreSelectSchedule trace NPC_VPlayerController.cpp:%d -> 0x%x"),
		GControllerPreSelectLine, GControllerIdleSchedule));
	return GControllerIdleSchedule;
}

// Slots 488-497: `0x103a4730` .. `0x103a4850`, each a bare `RET`.
void FElysiumNpcPlayerController::DeathSound() {}           // 488 `0x103a4730`
void FElysiumNpcPlayerController::AlertSound() {}           // 489 `0x103a4750`
void FElysiumNpcPlayerController::IdleSound() {}            // 490 `0x103a4770`
void FElysiumNpcPlayerController::PainSound() {}            // 491 `0x103a4790`
void FElysiumNpcPlayerController::FearSound() {}            // 492 `0x103a47b0`
void FElysiumNpcPlayerController::LostEnemySound() {}       // 493 `0x103a47d0`
void FElysiumNpcPlayerController::FoundEnemySound() {}      // 494 `0x103a47f0`
void FElysiumNpcPlayerController::SurprisedSound() {}       // 495 `0x103a4810`
void FElysiumNpcPlayerController::TargetAcquiredSound() {}  // 496 `0x103a4830`
void FElysiumNpcPlayerController::Slot497() {}              // 497 `0x103a4850`

// --- The port's own presentation (named modernization) ---------------------------------------------

void FElysiumNpcPlayerController::SetIgnoreCharacterCollision(bool bIgnore)
{
	// **Named modernization — the non-solid stand-in** (header): whatever a caller asks, the
	// stand-in's Unreal motor ignores other character capsules.
	(void)bIgnore;
	FElysiumNpcVampire::SetIgnoreCharacterCollision(true);
}

void FElysiumNpcPlayerController::BuildOwnMotor()
{
	// The shared motor build, made non-solid (the modernization above). Reached by the prepared-visual
	// completion and the model-swap rebuild.
	BuildMotor();
	SetIgnoreCharacterCollision(true);
}

void FElysiumNpcPlayerController::OnDormancyChanged()
{
	FElysiumNpcVampire::OnDormancyChanged();
	if (bDead)
	{
		DestroyMotor();
	}
}

void FElysiumNpcPlayerController::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	FElysiumNpcVampire::GetDebugState(Out);
	Out.Emplace(TEXT("Role"), TEXT("player controller (the player's scene stand-in; non-solid)"));
	Out.Emplace(TEXT("Owner"), OwnerIsThePlayer() ? TEXT("the player") : TEXT("(none)"));
	// `GetControllerNPC` `0x10161a70`'s `m_fEffects |= 0x60` and what it means for the body.
	Out.Emplace(TEXT("Effects"), FString::Printf(TEXT("0x%x%s"), EffectsWord,
		IsTransmitted() ? TEXT("") : TEXT(" (EF_NODRAW: never transmitted, ShouldTransmit 0x100ab020)")));
	Out.Emplace(TEXT("Body drawn"), Visual == nullptr ? TEXT("(no body)")
		: Visual->IsVisible() ? TEXT("yes") : TEXT("no (animation host; the pawn draws its pose)"));
}
