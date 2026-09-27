// Story 0019/8 (29e under the strict verdict), family **Misc19** -- `CAI_BaseNPC`'s bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2); ported by lane L11.
//
// Declarations are in `ElysiumNpcBaseMisc19.inl` (included inside `class FElysiumNpcBase`) or
// generated in `ElysiumNpcBaseSlots.inl` for a slot body.
//
// Owns (Misc19's `rule` rows): 0x10279dd0 CAI_BaseNPC::ChooseEnemy (its body lives in
// `ElysiumNpcEnemy.cpp` with `SetEnemy`), 0x1026cdc0 CAI_BaseNPC::EnterGrappleState, 0x1026cec0
// CAI_BaseNPC::FUN_1026cec0 (slot 354), 0x10274e30 CAI_BaseNPC::HandleAnimEvent.

#include "Substrate/ElysiumNpcBase.h"

#include "ElysiumAnimEvent.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumHint.h"
#include "Substrate/ElysiumInterestingPlace.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcMaker.h"
#include "Substrate/ElysiumNpcSoundsShared.h"
#include "Substrate/ElysiumScriptedSequence.h"
#include "Substrate/ElysiumWeaponClasses.h"

// --- Slot 379's base body -------------------------------------------------------------------------

bool FElysiumNpcBase::EnterGrappleState(const FElysiumEntityHandle& Partner, EElysiumGrappleRole Role,
	EElysiumGrappleType Type, int32 Position, bool bHolster)
{
	// `0x1026cdc0`, three statements, none gated:
	// 1. `0x1026d130` — `SetEnemy(NULL)`, `DisconnectFromSquad` (`0x1026d050`), `++m_iIsOblivious`
	//    (`+0x5bb4`), no bookkeeping bit and no output. The port's body of it is the director's
	//    `MakeNpcOblivious`, which is that function whole.
	FElysiumScriptedSequence::MakeNpcOblivious(*this);                    // 0x1026cdc4
	// 2. `m_OnGrappleBegin` (`+0x5bd8`) with the partner as activator (`0x100cd660(&out, partner,
	//    this, 0)`).
	static const FName OnGrappleBegin(TEXT("OnGrappleBegin"));
	FireOutput(OnGrappleBegin, Partner);                                  // 0x1026cdd7
	// 3. `CBaseCombatCharacter::EnterGrappleState` with every argument; its `AL` is this body's
	//    answer (the payphone's `SETNZ` and the Troika's `TEST AL,AL` read it).
	return FElysiumCombatCharacter::EnterGrappleState(Partner, Role, Type, Position, bHolster); // 0x1026cdfd
}

// --- Slot 354 -------------------------------------------------------------------------------------

void FElysiumNpcBase::Slot354()
{
	// `CAI_BaseNPC::FUN_1026cec0` `0x1026cec0` (slot 354, `CAI_BaseNPC#354` and
	// `CAI_BaseNPCTroika#354`): the victim's feed-begin callback. The incoming argument is unused.
	// 1. `0x1026d130` — the make-oblivious triple.
	FElysiumScriptedSequence::MakeNpcOblivious(*this);                    // 0x1026cec3
	static const FName OnFedUponBegin(TEXT("OnFedUponBegin"));           // +0x5c08

	// 2. The zombie-feed gate: a LIVE partner (`+0x1538` not -1, serial matching, table entry
	//    non-null), a set role (`+0x153c != -1`) and grapple type 8 (`+0x1540`,
	//    `BeFedOnByZombie`) return having fired nothing.
	const FElysiumEntity* const LivePartner =
		World != nullptr ? World->Resolve(Grapple.Partner) : nullptr;       // 0x1026cec8 .. 0x1026cef8 (serial 0x1026cef3)
	if (LivePartner != nullptr)
	{
		if (Grapple.Role == EElysiumGrappleRole::None)                    // 0x1026cf01
		{
			FireOutput(OnFedUponBegin, FElysiumEntityHandle::Invalid());  // 0x1026cf4a / 0x1026cf55
			return;
		}
		if (Grapple.Type == EElysiumGrappleType::ZombieFeedsPlayer)       // 0x1026cf0a
		{
			return;                                                       // 0x1026cf5a
		}
	}
	// 3. `0x1026cf0c`: no role -> fired with a null activator; otherwise the partner handle is
	//    re-resolved (`-1` or a stale serial fire null) and fired with it.
	if (Grapple.Role == EElysiumGrappleRole::None)                        // 0x1026cf13
	{
		FireOutput(OnFedUponBegin, FElysiumEntityHandle::Invalid());      // 0x1026cf55
		return;
	}
	const FElysiumEntity* const Partner =
		World != nullptr ? World->Resolve(Grapple.Partner) : nullptr;       // 0x1026cf15 .. 0x1026cf35 (-1 0x1026cf1e)
	FireOutput(OnFedUponBegin,
		Partner != nullptr ? Partner->Handle : FElysiumEntityHandle::Invalid()); // 0x1026cf43 / 0x1026cf55
}

// --- `SetEnemy`'s helpers -------------------------------------------------------------------------

void FElysiumNpcBase::SetLastEnemy(FElysiumEntity* Entity)
{
	// `0x10279b70`: slot 1 `GetRefEHandle()` on the entity, its handle into `m_hLastEnemy`; null
	// writes `0xffffffff`.
	BaseMemory.LastEnemy = Entity != nullptr ? Entity->Handle : FElysiumEntityHandle::Invalid();
}

FElysiumEntity* FElysiumNpcBase::SummonerRedirect(FElysiumEntity* Entity)
{
	// `0x102707d0`: `param_1 != 0 && param_1->+0x98 != 0 && (+0x98)->vtable[0x228]() == 3` ->
	// `(+0x98)->vtable[0x184]()`, else `param_1`.
	if (Entity == nullptr)
	{
		return Entity;
	}
	FElysiumNpc* const Troika = Entity->AsNpc();                          // +0x98 m_pBaseNPCTroika
	if (Troika == nullptr || Troika->Classify() != 3)                     // slot 138
	{
		return Entity;
	}
	// Slot 97 `GetOwnerEntity()` — the owner handle, resolved (null when it no longer resolves,
	// which is retail's own answer for a dead owner handle).
	return Troika->World != nullptr ? Troika->World->Resolve(Troika->GetOwnerEntity()) : nullptr;
}

void FElysiumNpcBase::ClearEnemyMemoryRecord(FElysiumEntity* Entity)
{
	// `CAI_Enemies::ClearMemory` (`0x102dfaa0`) on slot 541 `GetEnemies()`: the entity's first
	// record is unlinked (`FElysiumNpcEnemyMemory::ClearMemory`); a null entity removes nothing.
	FElysiumNpcEnemyMemory* const Enemies = static_cast<FElysiumNpcEnemyMemory*>(GetEnemies());
	++ClearEnemyMemoryRecordCalls;
	LastClearedEnemyMemoryRecord =
		Entity != nullptr ? Entity->Handle : FElysiumEntityHandle::Invalid();
	if (Enemies != nullptr && Entity != nullptr)
	{
		Enemies->ClearMemory(Entity->Handle);
	}
}

void FElysiumNpcBase::TaskFailText(const TCHAR* Text)
{
	// Slot 448 through the vtable (the Troika fill `0x1029adb0` runs its own resets first), with
	// retail's string-pointer code standing as `TextTaskFailCode`.
	LastTaskFailText = Text;
	TaskFail(TextTaskFailCode);
}

// --- Slot 259's base body -------------------------------------------------------------------------

namespace
{
	// `0x10275260`/`0x10275270`: `FindEntityGenericNearest(option, GetAbsOrigin(), 256.0, this, NULL)`.
	constexpr float Misc19PickupSearchRadiusUnits = 256.0f;
	// The `+0x1b44`/`+0x1b48` TaskFail trace lines `0x7f8` stamps (`AI_BaseNPC.cpp`).
	constexpr int32 Misc19PickupInUseLine = 0x1f06;
	constexpr int32 Misc19PickupCantUseLine = 0x1f0e;
	constexpr int32 Misc19PickupStolenLine = 0x1f19;
	// `SENTENCEG_PlayRndSz(edict, options, 1.0, 0x50, 0, 0x64)` (`0x10274f03`-`0x10274f16`).
	constexpr int32 Misc19SentenceSoundLevel = 0x50;
	constexpr int32 Misc19SentencePitch = 0x64;
	// `m_fEffects |= 0x10` (`EF_NOINTERP`), `0x102751e1`.
	constexpr uint32 Misc19EffectNoInterp = 0x10u;
	// `m_afMemory` bit `0x2000`, the turning bit `0x7e4` clears.
	constexpr uint32 Misc19MemoryTurning = 0x2000u;

	// `0x100f7fe0` (`FindEntityGenericNearest`): `FindEntityByNameNearest` (`0x100f7b20`) — the live
	// entity whose targetname is `Name`, squared distance strictly below the radius squared,
	// first-listed on a tie — and, failing that, `FindEntityByClassnameNearest` (`0x100f7d50`).
	FElysiumEntity* Misc19FindEntityGenericNearest(FElysiumEntityWorld& World, const FString& Name,
		const FVector& PointCm, float RadiusUnits)
	{
		const double RadiusCm = static_cast<double>(RadiusUnits) * ElysiumMove::U;
		double BestSq = RadiusCm * RadiusCm;
		FElysiumEntity* Best = nullptr;
		for (const TUniquePtr<FElysiumEntity>& Entry : World.Entities())
		{
			FElysiumEntity* const Entity = Entry.Get();
			if (Entity == nullptr || Entity->IsRecordOnly() || Entity->IsDead()
				|| !Entity->TargetName.Equals(Name, ESearchCase::IgnoreCase))
			{
				continue;
			}
			const double DistSq = FVector::DistSquared(Entity->Origin, PointCm);
			if (DistSq < BestSq)
			{
				BestSq = DistSq;
				Best = Entity;
			}
		}
		if (Best != nullptr)
		{
			return Best;
		}
		return FElysiumNpcMaker::FindNearestByClassname(World, *Name, PointCm, RadiusUnits);
	}
}

bool FElysiumNpcBase::WeaponHandleAnimEventMisc19(const FElysiumAnimEvent& Event)
{
	// `0x1032e210`: a live `m_hActiveWeapon` -> `weapon->Operator_HandleAnimEvent(event, this)`.
	FElysiumItem* const Held = Inventory.Active(*this);
	FElysiumWeapon* const Weapon = Held != nullptr ? Held->AsWeapon() : nullptr;
	return Weapon != nullptr && Weapon->OperatorHandleAnimEvent(*this, Event);
}

void FElysiumNpcBase::EmitSoundScriptMisc19(const FString& SoundScript)
{
	// SEAM for `0x101b0c10` — see the declaration.
	EmittedSoundScripts.Add(SoundScript);
}

bool FElysiumNpcBase::HandleAnimEvent(const FElysiumAnimEvent& Event)
{
	// `CAI_BaseNPC::HandleAnimEvent` `0x10274e30`, slot 259. Every id the two switches name returns
	// at `0x102754d1` without the base; only the default label forwards. So every named arm answers
	// claimed, its guard failures included.
	const int32 Id = Event.Event;
	// Dispatch: `0x10274e45` JG (id > 0x3fe -> the second band at `0x1027512c`); `0x10274e5e` JA
	// (id - 0x3e8 > 10 -> default) and the table jump `0x10274e64` (`0x102754dc`); in the second band
	// `0x10275134` JA (id - 0x7d1 > 0x34 -> default) and the table jump `0x10275142` through the byte
	// table `0x1027553c` into `0x10275508`. The C++ switch is those three jumps.
	switch (Id)
	{
	case 0x3fc:
	case 0x3fd:
	case 0x3fe:                                                           // 0x10274e4b / 0x10274e50
		UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("Bodygroup!"));            // 0x10275115 / 0x1027511a
		return true;
	case 0x3e8:                                                           // SCRIPT_EVENT_DEAD, 0x10274e6b
		if (NpcStateRetail() != 4)                                        // 0x10274e72
		{
			return true;
		}
		AnimEventLifeStateWord = 1;                                       // 0x10274e78 m_lifeState := LIFE_DYING
		Health = 0;                                                       // 0x10274e82 m_iHealth
		return true;
	case 0x3e9:
	case 0x3ea:                                                           // 0x10275004 / 0x1027508c
		// A live `m_hCine` (`+0x5d74`: -1 `0x1027500d`/`0x10275095`, serial `0x10275031`/`0x102750b9`,
		// null entry `0x1027503b`/`0x102750c3`) -> `AllowInterrupt(0 / 1)` (`0x101a8890`). The
		// re-resolve (`0x1027504a`/`0x10275063`, `0x102750d2`/`0x102750eb`) of the handle just
		// validated cannot miss; its null arm (`0x1027507e`/`0x10275107`) is unreachable.
		if (FElysiumScriptedSequence* const Cine = ResolveCine())
		{
			Cine->AllowInterrupt(Id == 0x3ea);                            // 0x1027506b push 0 / 0x102750f3 push 1
		}
		return true;
	case 0x3eb:                                                           // 0x10274f27
	{
		// The Troika's interesting place (`+0x98`'s `+0x62ec`) is read FIRST (`0x10274f31` /
		// `0x10274f33`); then `atoi(options)` goes to the live cine's `0x101a7230` (`m_hCine`
		// checks `0x10274f42`/`0x10274f64`/`0x10274f69`, re-resolve `0x10274f74`/`0x10274f8d`), else
		// to `m_pHintNode`'s `0x102d09b0` (`0x10274fba`), else to the place's `0x102db3e0`
		// (`0x10274fe0`). Each fires its `OnScriptEvent0<n>` / `OnAnimEvent<n>` only for 1..8.
		FElysiumNpc* const Troika = AsNpc();                              // 0x10274f27 +0x98
		FElysiumInterestingPlace* const Place =
			Troika != nullptr ? Troika->CurrentAmbientSpot() : nullptr;   // 0x10274f33 +0x62ec
		if (FElysiumScriptedSequence* const Cine = ResolveCine())
		{
			// `0x101a7230(n)`: `+0x5fb4 + 0x18n` for n in 1..8, i.e. the port's zero-based index.
			const int32 N = FCString::Atoi(*Event.Options);               // 0x10274f99
			if (N >= 1 && N <= 8)
			{
				Cine->FireScriptEvent(N - 1);                             // 0x10274fa4
			}
			return true;
		}
		if (BaseScheduleHost.HintNode != INDEX_NONE)                      // 0x10274fba
		{
			FireHintAnimEvent(BaseScheduleHost.HintNode,
				FCString::Atoi(*Event.Options));                          // 0x10274fc0 / 0x10274fd0
			return true;
		}
		if (Place == nullptr)                                             // 0x10274fe0
		{
			return true;
		}
		// `0x102db3e0(this, n)`: `(n*3 + 0x8d)*8` = `OnAnimEvent<n>` (`+0x480` for n = 1), n in 1..8,
		// this NPC as activator.
		const int32 N = FCString::Atoi(*Event.Options);                   // 0x10274fea
		if (N >= 1 && N <= 8)
		{
			Place->FireOutput(FName(*FString::Printf(TEXT("OnAnimEvent%d"), N)), Handle); // 0x10274ff6
		}
		return true;
	}
	case 0x3ec:
	case 0x3f0:                                                           // 0x10274ec1 / 0x10274ed5
		EmitSoundScriptMisc19(Event.Options);                             // 0x10274ec7 / 0x10274edb
		return true;
	case 0x3f1:                                                           // 0x10274ee9
		// `random->RandomInt(0, 2)`: zero returns; non-zero falls THROUGH into 0x3ed — the
		// sentence plays two times in three.
		if (ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(0, 2) == 0) // 0x10274ef5 / 0x10274efa
		{
			return true;
		}
		[[fallthrough]];
	case 0x3ed:                                                           // 0x10274f00
		NpcKernelSoundsShared::SoundsPlaySentenceGroup(*this, *Event.Options, 1.f,
			Misc19SentenceSoundLevel, 0, Misc19SentencePitch);            // 0x10274f16
		return true;
	case 0x3f2:                                                           // NOT_DEAD, 0x10274e95
		if (NpcStateRetail() != 4)                                        // 0x10274e9c
		{
			return true;
		}
		AnimEventLifeStateWord = 0;                                       // 0x10274ea8 m_lifeState := LIFE_ALIVE
		Health = MaxHealth;                                               // 0x10274ea2 / 0x10274eb2
		return true;
	case 0x7d1:                                                           // 0x1027516d
		if ((Flags & 1) != 0)                                             // 0x1027516f / 0x10275176 FL_ONGROUND
		{
			EmitSoundScriptMisc19(TEXT("AI_BaseNPC.BodyDrop_Light"));     // 0x10275183
		}
		return true;
	case 0x7d2:                                                           // 0x10275149
		if ((Flags & 1) != 0)                                             // 0x1027514b / 0x10275152
		{
			EmitSoundScriptMisc19(TEXT("AI_BaseNPC.BodyDrop_Heavy"));     // 0x1027515f
		}
		return true;
	case 0x7da:                                                           // 0x10275191
		EmitSoundScriptMisc19(TEXT("AI_BaseNPC.SwishSound"));             // 0x10275198
		return true;
	case 0x7e4:                                                           // 0x102751a6, the 180 turn
		SetIdealActivity(1);                                              // 0x102751aa ACT_IDLE
		// `0x102751c5` slot 221 `GetAngles` (`vt+0x374`) feeds `0x102751d3` (`0x1000a7c7`,
		// `SetBoneController(0, yaw)`), which the port has no bone controller for.
		BaseScheduleHost.MemoryBits &= ~Misc19MemoryTurning;              // 0x102751bf
		// `0x10095cb0(0, GetAngles().y)` — `SetBoneController(0, yaw)`; the port drives no bone
		// controller, the facing is the entity's own angles (the identity the facing family
		// states).
		if (FElysiumNpc* const Troika = AsNpc())
		{
			Troika->EffectsWord |= Misc19EffectNoInterp;                  // 0x102751e1 m_fEffects |= EF_NOINTERP
		}
		return true;
	case 0x7e6:                                                           // 0x102751f1
		// `0x102751f4`/`0x102751f6` test the options POINTER only; a model event's options are
		// never null, so an empty string reaches `atoi("") = 0` (`0x102751fd`).
		// `0x102e0b40` then `0x102e1c10(motor, GetAbsAngles().y + atoi(options), -1.0)`.
		// `0x10275218`: slot 219 `GetAbsAngles` (`vt+0x36c`), its `.y` plus the `atoi`.
		SetAlternateAiIdealYaw(static_cast<float>(Angles.Y)
			+ static_cast<float>(FCString::Atoi(*Event.Options)));        // 0x1027520f / 0x10275234
		return true;
	case 0x7f8:                                                           // 0x10275242, NPC_PICKUP
	{
		FElysiumEntity* Found = nullptr;
		if (!Event.Options.IsEmpty())                                     // 0x10275247 / 0x10275253
		{
			// A non-empty option whose search misses is "stolen" — no fallback to the target.
			Found = World != nullptr ? Misc19FindEntityGenericNearest(*World, Event.Options,
				GetAbsOrigin(), Misc19PickupSearchRadiusUnits) : nullptr; // 0x10275260 / 0x10275270
		}
		else
		{
			// `m_hTargetEnt` -1 (`0x1027528e`) or a stale serial (`0x102752af`) -> the stolen arm.
			Found = World != nullptr ? World->Resolve(GetTarget()) : nullptr; // 0x10275285 .. 0x102752b9
		}
		// `+0xa0` (`m_pCombatWeapon`, the weapon self-cast): every VtMB item is one.
		FElysiumItem* const Item = Found != nullptr ? Found->AsItem() : nullptr; // 0x1027527d / 0x102752bf
		if (Item == nullptr)                                              // 0x10275277 / 0x102752c7
		{
			RecordScheduleEvent(FString::Printf(TEXT("TaskFail trace AI_BaseNPC.cpp:%d"),
				Misc19PickupStolenLine));                                 // 0x10275370 / 0x1027537a
			TaskFailText(TEXT("Weapon stolen by someone else"));          // 0x10275384
			return true;
		}
		// `0x102521f0`: the weapon's owner (`+0x88c`) resolves to a combat character.
		const FElysiumEntity* const Owner = World->Resolve(Item->Owner);
		if (Owner != nullptr && Owner->AsCombatCharacter() != nullptr)   // 0x102752cf / 0x102752d8
		{
			RecordScheduleEvent(FString::Printf(TEXT("TaskFail trace AI_BaseNPC.cpp:%d"),
				Misc19PickupInUseLine));                                  // 0x102752e1 / 0x102752eb
			TaskFailText(TEXT("Weapon in use by someone else"));          // 0x102752f5
			return true;
		}
		if (!Weapon_CanUse(Item))                                         // 0x10275307 / 0x1027530f
		{
			RecordScheduleEvent(FString::Printf(TEXT("TaskFail trace AI_BaseNPC.cpp:%d"),
				Misc19PickupCantUseLine));                                // 0x1027531a / 0x10275324
			TaskFailText(TEXT("Can't use this weapon type"));             // 0x1027532e
			return true;
		}
		++WeaponOnPickedUpCalls;                                          // 0x10275342 weapon slot 341
		Weapon_Equip(Item, false);                                        // 0x1027534f slot 383
		TaskComplete(false);                                              // 0x10275359
		return true;
	}
	case 0x7f9:                                                           // 0x10275436
		// The optional `FindEntityGeneric(NULL, options, this, NULL)` and its slot 192
		// (`WorldSpaceCenter`) are computed and DISCARDED (`0x10275448`/`0x1027545a`); the drop is
		// unconditional and takes no target (retail's divergence from the SDK's throw, kept).
		// `0x1027543b` JZ (null options skip the lookup), `0x1027544f` JZ (a miss skips slot 192).
		Weapon_Drop(ActiveWeaponEntity(), nullptr, false);                // 0x10275468 / 0x10275470 slot 385
		return true;
	case 0x7fa:
	case 0x7fb:                                                           // 0x102753ba / 0x10275393
		// Active weapon null or no option -> return; `LookupSequence(option)` / `atoi(option)` on
		// the weapon model, -1 -> return, else `0x10260a50(weapon, seq)`. SEAM: no weapon model.
		// The options test (`0x102753a9` / `0x102753d0`) is the pointer's, never null here.
		if (ActiveWeaponEntity() != nullptr)                              // 0x10275395 / 0x1027539e / 0x102753bc / 0x102753c5
		{
			// A `-1` sequence returns (`0x102753e1`); otherwise `0x102753ea` (`0x1000d0b2`,
			// `ResetSequence` on the weapon model).
			++WeaponModelSequenceRequests;                                // 0x102753d9 / 0x102753b0
		}
		return true;
	case 0x7fc:                                                           // 0x102753f8
		// `LookupActivity(option)` on the active weapon; -1 -> return; else `Weapon_SetActivity(act,
		// 0)`. SEAM: the weapon lookup answers -1.
		// The options test (`0x1027540c`) is the pointer's, never null here.
		if (ActiveWeaponEntity() != nullptr)                              // 0x102753fa / 0x10275401
		{
			// A found activity reaches `0x10275428` (`0x100101ea`, `Weapon_SetActivity(act, 0)`).
			++WeaponModelSequenceRequests;                                // 0x10275415; 0x1027541d -1 -> return
		}
		return true;
	case 0x802:
	case 0x803:                                                           // 0x1027547f
	case 0x804:
	case 0x805:                                                           // 0x10275491
		// `0x1026d460(this, 0)` "normal" for 0x802/0x803, mode 1 "heavy" for 0x804/0x805. The chain
		// is the Troika's `NpcStep` (it reads the Troika's footstep template); a base-only NPC has
		// none to play.
		if (FElysiumNpc* const Troika = AsNpc())
		{
			Troika->NpcStep(Id, Id >= 0x804);                             // 0x10275483 / 0x10275495
		}
		return true;
	default:
		break;
	}
	// `0x102754a3`: `pSource == this` (the port dispatches only this character's own timelines)
	// and the id outside 3000..0xfa2 -> `CBaseCombatCharacter::HandleAnimEvent` (`0x102754bb`);
	// otherwise `Weapon_HandleAnimEvent` (`0x102754cc`).
	// `0x102754a6` JNZ: an event whose `pSource` is not this character goes to the weapon route too.
	if (Id >= 3000 && Id <= 0xfa2)                                        // 0x102754ae / 0x102754b6
	{
		return WeaponHandleAnimEventMisc19(Event);
	}
	return FElysiumCombatCharacter::HandleAnimEvent(Event);               // 0x102754bb direct call
}

bool FElysiumNpcBase::FireHintAnimEvent(int32 HintNode, int32 N)
{
	// `0x102d09b0(hint, activator, n)`: `0 < n < 9` fires `+0x4e4 + 0x18n` — `OnAnimEvent<n>`
	// (`CAI_Hint +0x4fc` for n = 1) — with the calling NPC as activator and the hint as caller.
	if (N < 1 || N > 8 || World == nullptr || !World->Entities().IsValidIndex(HintNode))
	{
		return false;
	}
	FElysiumHint* const Hint = FElysiumHint::Cast(World->Entities()[HintNode].Get());
	if (Hint == nullptr)
	{
		return false;
	}
	Hint->FireOutput(FName(*FString::Printf(TEXT("OnAnimEvent%d"), N)), Handle);
	return true;
}
