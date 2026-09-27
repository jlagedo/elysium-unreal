#pragma once

#include "Substrate/ElysiumNpcVampire.h"

// `CNPC_VPlayerController` (primary vtable `0x104b2d4c`), built by `npc_VPlayerController` factory
// `0x103a4470` (story 5 fold A2).
//
// The player's scene stand-in: a whole `CAI_BaseNPCTroika` under `CNPC_VVampire` that thinks every
// frame, owned by the player it stands in for (`CBasePlayer::GetControllerNPC` `0x10161a70` calls
// `SetOwnerEntity(player)` at `0x10161be1`). Its forwarding arms read slot 97 `GetOwnerEntity` — the
// `+0x184` dispatch — so they reach the OWNER, the player (`docs/vtmb/npc-ai/lifecycle.md`,
// "The player controller").
//
// Twenty-six own vtable slots (the vtable diff against `CNPC_VVampire`, not the ledger's name-prefix
// count). The deleting destructor (slot 5, `0x10376f50`) is the C++ destructor's; slot 432
// (`0x103a4870`) is an ILT jump to `CNPC_VVampire`'s own 432 `0x10385a10` and is inherited, not
// overridden, because it runs the same body. The rest are overrides below, each citing its address.
//
// `CNPC_VFrenzyShadow` and `CNPC_VWolfMorph` derive from this class and inherit every body here they
// do not replace (`ElysiumNpcFrenzyShadow.h`, `ElysiumNpcWolfMorph.h`).
class FElysiumNpcPlayerController : public FElysiumNpcVampire
{
public:
	ELYSIUM_NPC_CLASS("CNPC_VPlayerController", FElysiumNpcVampire)


	// Slot 72 `0x103751a0` — `XOR AL,AL; RET 4`: the discipline-target veto answers false, so no
	// player discipline can target the stand-in (`0x101e1a60`'s filter).
	virtual bool Slot72(int32 Discipline) override;
	// Slot 103 `0x103a4510`, shared by `CNPC_VWolfMorph`.
	virtual void Spawn() override;
	// Slot 138 `0x103a4890` — `return 2;`.
	virtual int32 Classify() override;
	// Slot 245 `0x103a49c0` / slot 246 `0x103a4a60`, shared by both children.
	virtual void AddExtraAnimationModels(FElysiumEntity* ExtraModel, const TCHAR* AttachmentA,
		const TCHAR* AttachmentB, int32 FlagsA, int32 FlagsB) override;
	virtual void RemoveExtraAnimationModels() override;
	// Slot 300 `0x103a4950`, shared by both children.
	virtual void Event_TookLife(FElysiumEntity* Victim, bool bArg2, bool bArg3) override;
	// Slots 362-365 `0x10375140` / `0x10375120` / `0x10375180` / `0x10375160` — every cone answers
	// false. `CNPC_VWolfMorph` inherits them; `CNPC_VFrenzyShadow` puts the base cones back.
	virtual bool FInViewCone(const FVector& PointCm) override;
	virtual bool FInViewCone(FElysiumEntity* Candidate) override;
	virtual bool FInAimCone(const FVector& TargetCm) override;
	virtual bool FInAimCone(FElysiumEntity* AimTarget) override;
	// Slot 404 `0x103a48b0`, shared by both children.
	virtual int32 IRelationType(FElysiumEntity* Candidate) override;
	// Slot 420 `0x103a4580`.
	virtual void NPCInit() override;
	// Slot 431 `0x103a4700`, shared by `CNPC_VWolfMorph`.
	virtual void NPCThink() override;
	// Slot 437 `0x103a46b0`, shared by both children.
	virtual int32 PreSelectSchedule() override;
	// Slots 488-497 `0x103a4730` .. `0x103a4850` (step `0x20`) — ten bare `RET`s, shared by both
	// children: the stand-in never voices a concept.
	virtual void DeathSound() override;
	virtual void AlertSound() override;
	virtual void IdleSound() override;
	virtual void PainSound() override;
	virtual void FearSound() override;
	virtual void LostEnemySound() override;
	virtual void FoundEnemySound() override;
	virtual void SurprisedSound() override;
	virtual void TargetAcquiredSound() override;
	virtual void Slot497() override;

	// --- The bodies, callable by name ------------------------------------------------------------

	/** `0x103a48b0` — slot 404's body, a `const` reading of the candidate. Retail `Disposition_t`:
	 *  0 `D_ER`, 1 `D_HT`, 3 `D_LI`, 4 `D_NU`. Moved here from family Squad's species arm with the
	 *  fold. */
	int32 PlayerControllerIRelationType(const FElysiumEntity* Candidate) const;

	/** `CNPC_VPlayerController::Event_TookLife`'s tag, the retail format string at `0x1064bcc0`
	 *  (`"CNPC_VPlayerController::Event_TookLife %s"`) that `0x1017e150` receives as its reason. */
	static const TCHAR* TookLifeReasonFormat();

	/** The retail level `Event_TookLife` raises the owner's criminal level to (`PUSH 0x4`,
	 *  `0x103a4991`), with `-1.0` for "derive the duration" (`PUSH 0xbf800000`, `0x103a498c`). */
	static constexpr int32 TookLifeCriminalLevel = 4;
	/** The reason `Event_TookLife` last formatted (`va(TookLifeReasonFormat(), victim)`). Retail
	 *  hands it to `0x1017e150` as a debug tag; `ElysiumLaw::SetCriminalLevel` carries no reason, so
	 *  it is kept here for the record and read by the test alone. */
	FString LastTookLifeReason;

	/** SEAM for `AddFlag2(0x10)` (`0x100027bb` at `0x103a454d`) — `CBaseEntity::m_fFlags2`
	 *  (`+0x438`). `FElysiumEntity::Flags` is the first word (`+0x434`) only, and no port member
	 *  stands for the second; the bits the controller's `Spawn` ORs in are carried here, under the
	 *  class that writes them, until the word has an owner. The meaning of bit `0x10` is
	 *  unrecovered. */
	uint32 Flags2Added = 0;

	/** The `+0x6360` / `+0x6361` bytes and the `+0x6364` word `NPCInit` writes after the five law
	 *  thresholds: story 1's dead `SecureType` scrambler (`0x10009408` over 0, XOR/AND-mixed into
	 *  `+0x6364`). The port stores the PLAIN value, 0, through `SeedCriminalLevelWitnessed` — the
	 *  same store the Troika body makes — and keeps no scrambled copy. */

	// --- The port's own presentation (named modernization) ---------------------------------------
	//
	// **Named modernization — the non-solid stand-in.** No step of retail's `Spawn` `0x103a4510`,
	// `NPCInit` `0x103a4580` or `GetControllerNPC` `0x10161a70` changes the stand-in's solidity (brief
	// open question 6: `CNPC_VHuman::Spawn`'s own `SetSolid` is unwalked). `CBasePlayer::PostThink`
	// (`0x1016c510`..`0x1016c672`) pins the pawn onto the stand-in every frame
	// (`FElysiumEntityWorld::UpdatePlayerFromController`), so its Unreal motor ignores other character
	// capsules — the pawn's first among them — rather than walking into the body standing inside it.
	// No retail state, and no event order, reads the Unreal capsule. The stand-in itself is never drawn
	// (`EF_NODRAW`, `FElysiumNpc::IsTransmitted`); its body stays live as the pose the pawn draws.
	virtual void SetIgnoreCharacterCollision(bool bIgnore) override;
	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override;
	// Port presentation, no retail counterpart: the stand-in's removal kills it and then destroys its
	// skeletal body (`FElysiumEntityWorld::RemovePlayerControllerEntity`), so a dead stand-in drops
	// its Unreal motor first — the engine-side path follower must not hold the dying attachment.
	virtual void OnDormancyChanged() override;

protected:
	virtual void BuildOwnMotor() override;
};
