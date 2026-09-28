// Story 0019/8 (29e under the strict verdict), family **Misc19** -- `CAI_BaseNPCTroika`'s bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2); ported by lane L11.
//
// Declarations are in `ElysiumNpcMisc19.inl` (included inside `class FElysiumNpc`) or generated in
// `ElysiumNpcSlots.inl` for a slot body.
//
// Owns (Misc19's `rule` rows): 0x10279a50 SetEnemy (its body lives in `ElysiumNpcEnemy.cpp`),
// 0x102b4f60 CAI_BaseNPCTroika::FUN_102b4f60 (slot 596), 0x102b4fe0 CAI_BaseNPCTroika::FUN_102b4fe0
// (slot 598), 0x10365a90 FUN_10365a90 (Bach's camper pass, `ElysiumNpcMisc19Species.cpp`),
// 0x102b4cc0 CAI_BaseNPCTroika::FUN_102b4cc0 (slot 595), 0x10395ce0 FUN_10395ce0, 0x1039ea60
// FUN_1039ea60, 0x102b5c00 CAI_BaseNPCTroika::EnterGrappleState, 0x1017f4a0
// PlayerSupernaturalIncident (`ElysiumLaw.cpp`), 0x1029b290 CAI_BaseNPCTroika::HandleAnimEvent.

#include "Substrate/ElysiumNpc.h"

#include "ElysiumAnimEvent.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumSheetSlots.h"
#include "ElysiumSoundAssets.h"
#include "ElysiumWorldServices.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumNpcKernelTunables.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses10Shared.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumScheduleCorpus.h"

namespace
{
	// `DAT_10924324` / `DAT_10924328` / `DAT_1092432c`: slot 595's static half-extents, initialised
	// once behind `DAT_10923d80` bit 0 (`0x102b4cd1`-`0x102b4d01`) to 1024.0, 1024.0 and 128.0
	// SOURCE units and never written again, so they are constants here.
	constexpr float Misc19AcquireHalfExtentXUnits = 1024.0f;
	constexpr float Misc19AcquireHalfExtentYUnits = 1024.0f;
	constexpr float Misc19AcquireHalfExtentZUnits = 128.0f;
	// `0x102b4dcd`'s capacity argument: the query fills at most 32 entries.
	constexpr int32 Misc19AcquireMaxCandidates = 0x20;
	// `Disposition_t` `D_NU`, the literal slot 598 hands `AddEntityRelationship` (`0x102b4fe8`).
	constexpr EElysiumRelationship Misc19ForgetDisposition = EElysiumRelationship::Neutral;
}

// --- Slot 595 -------------------------------------------------------------------------------------

TArray<FElysiumEntity*> FElysiumNpc::AcquireTargetBoxQuery(const FVector& MinsCm, const FVector& MaxsCm,
	int32 MaxCount) const
{
	// `UTIL_EntitiesInBox(list, 0x20, mins, maxs, 0x40, 0)` (`0x101ccc80`): the engine's
	// spatial-partition enumeration (`DAT_1070b24c` `+0x30`) through a `CEdtEntitiesEnum`
	// (`0x101ccb60` / `0x101ccbf0`) that admits an element only when `m_edtDerivedType` (`+0x4c`)
	// shares a bit with the mask `0x40` — a bit the `CAI_BaseNPCTroika` constructor (`0x1028d230`)
	// ORs in (and `CNPC_VYukie`'s, `0x103dd000`, a Troika too). So the candidates are Troika-line
	// NPCs only (`AsNpc()`), and the player is never one. It stops at 32 entries.
	// SEAM (the partition): this runtime stands no spatial partition, so the Troika NPCs whose
	// ORIGIN lies in the box are listed in entity-list order. Retail tests collision-bounds overlap
	// and enumerates in partition order, both unrecovered here; only the nearest-wins scan reads the
	// list, and its strict `<` makes the first-listed of two equidistant candidates win.
	TArray<FElysiumEntity*> Out;
	if (World == nullptr)
	{
		return Out;
	}
	for (const TUniquePtr<FElysiumEntity>& Entry : World->Entities())
	{
		FElysiumEntity* const Entity = Entry.Get();
		if (Entity == nullptr || Entity->IsRecordOnly() || Entity->AsNpc() == nullptr)
		{
			continue;
		}
		const FVector& P = Entity->Origin;
		if (P.X < MinsCm.X || P.X > MaxsCm.X || P.Y < MinsCm.Y || P.Y > MaxsCm.Y
			|| P.Z < MinsCm.Z || P.Z > MaxsCm.Z)
		{
			continue;
		}
		Out.Add(Entity);
		if (Out.Num() >= MaxCount)
		{
			break;
		}
	}
	return Out;
}

void FElysiumNpc::AcquireNearestHatedTarget()
{
	// `CAI_BaseNPCTroika::FUN_102b4cc0` `0x102b4cc0`, slot 595 — a possessed or frenzied body turns
	// on the nearest hated, targetable, live entity in a box around it, then hands it to slot 596.
	// It writes no NPC word of its own.
	const FVector SelfOrigin = GetAbsOrigin();                                // 0x102b4d0d, slot 217
	const FVector HalfCm(Misc19AcquireHalfExtentXUnits * ElysiumMove::U,
		Misc19AcquireHalfExtentYUnits * ElysiumMove::U, Misc19AcquireHalfExtentZUnits * ElysiumMove::U);
	const TArray<FElysiumEntity*> Candidates = AcquireTargetBoxQuery(SelfOrigin - HalfCm, SelfOrigin + HalfCm,
		Misc19AcquireMaxCandidates);                                       // 0x102b4dcd
	// `0x102b4d33`: the running minimum starts at `FLT_MAX`; `0x102b4d50`: the winner at null.
	float BestDistSq = TNumericLimits<float>::Max();
	FElysiumEntity* Best = nullptr;
	for (FElysiumEntity* const Candidate : Candidates)                    // 0x102b4ddb / 0x102b4e94
	{
		if (Candidate == nullptr)                                         // 0x102b4de8
		{
			continue;
		}
		// `0x102b4dee`: the candidate's `+0x9c` combat-character sub-object; none skips it.
		FElysiumCombatCharacter* const Character = Candidate->AsCombatCharacter();
		if (Character == nullptr)                                         // 0x102b4df6
		{
			continue;
		}
		// `0x102b4dfe`/`0x102b4e05`: `GetFlags()` bit `0x8000` (`FL_NOTARGET`, the sign of AH).
		if (HasNoTargetFlag(*Character))
		{
			continue;
		}
		// `0x102b4e0b`/`0x102b4e13`: `m_bIsBCCTargetable` (`+0x1480`) clear skips it.
		if (!IsBccTargetable(*Character))
		{
			continue;
		}
		// `0x102b4e19`/`0x102b4e21`: slot 158 `IsAlive`.
		if (!Character->IsAlive())
		{
			continue;
		}
		// `0x102b4e25`/`0x102b4e2c`: `0x100b5190`, `m_bScriptHidden` (`+0xf4`) — the port's
		// `FElysiumEntity::bHidden`.
		if (Character->IsHidden())
		{
			continue;
		}
		// `0x102b4e30`: never itself.
		if (Character == this)
		{
			continue;
		}
		// `0x102b4e37`/`0x102b4e40`: slot 404 `IRelationType` must be exactly `D_HT` (1).
		if (IRelationType(Character) != NpcKernelSenses10Shared::GD_HT)
		{
			continue;
		}
		// `0x102b4e46`-`0x102b4e85`: squared distance from OUR origin; strictly less replaces.
		const float DistSq = static_cast<float>(FVector::DistSquared(SelfOrigin, Character->GetAbsOrigin()));
		if (DistSq < BestDistSq)                                          // 0x102b4e7f
		{
			BestDistSq = DistSq;
			Best = Character;
		}
	}
	if (Best != nullptr)                                                  // 0x102b4ea1
	{
		Slot596(Best);                                                    // 0x102b4ea8, slot 596
	}
}

// --- Slot 596 -------------------------------------------------------------------------------------

void FElysiumNpc::Slot596(FElysiumEntity* Entity)
{
	// `CAI_BaseNPCTroika::FUN_102b4f60` `0x102b4f60`, the one-argument enemy adopt. A null argument
	// does nothing at all.
	if (Entity == nullptr)                                                // 0x102b4f69
	{
		return;
	}
	FElysiumEntity* const Resolved = SummonerRedirect(Entity);            // 0x102b4f6e, 0x102707d0
	// The enemy is set BEFORE the memory row is written, so slot 544's gates see the new enemy.
	ElysiumNpcEnemy::SetEnemy(*this,
		Resolved != nullptr ? Resolved->Handle : FElysiumEntityHandle::Invalid()); // 0x102b4f78
	if (Resolved == nullptr)
	{
		// Crash guard, no retail arm: retail dereferences the redirect's answer (`0x102b4f7d`); it
		// is null only for a summoned body whose owner handle no longer resolves.
		return;
	}
	// `0x102b4f8a`: slot 217 `GetAbsOrigin()` on the resolved entity; `0x102b4f94`: OUR slot 544
	// `UpdateEnemyMemory(entity, origin, &entity->+0x3d4)`. The third word retail pushes is an
	// address inside the entity, not an informer; the port's slot-544 body does not read its third
	// parameter, so it is null here.
	UpdateEnemyMemory(Resolved, Resolved->GetAbsOrigin(), nullptr);
}

// --- Slot 598 -------------------------------------------------------------------------------------

void FElysiumNpc::Slot598(FElysiumEntity* Entity)
{
	// `CAI_BaseNPCTroika::FUN_102b4fe0` `0x102b4fe0`, the forget-this-entity route, in retail's
	// order.
	// 1. `CBaseCombatCharacter::AddEntityRelationship(entity, 4, 0)` — `D_NU`, priority 0, FIRST.
	if (Entity != nullptr)
	{
		// Crash guard: a null entity adds no row. (Retail's `0x10332ca0` walk matches the first row
		// whose handle no longer resolves and overwrites it, appending a `-1` row only when none
		// is stale; the port's store keeps no stale-keyed rows to match.) The write is
		// unconditional — `SetEntity`'s lower-priority refusal is not retail's.
		Relationships.AddEntityRelationship(Entity->Handle, Misc19ForgetDisposition, 0); // 0x102b4fed
	}
	// 2. Slot 167 `GetEnemy()` IS the entity -> `SetEnemy(NULL)`.
	if (static_cast<const FElysiumNpcBase&>(*this).GetEnemy() == Entity)  // 0x102b4ff6 / 0x102b4ffe
	{
		ElysiumNpcEnemy::SetEnemy(*this, FElysiumEntityHandle::Invalid()); // 0x102b5004
	}
	// 3. `m_hLastEnemy` (`+0x1a94`) resolving to the entity -> `0x10279b70(NULL)`. The resolve's
	//    0x102b5012 JZ (handle -1) and 0x102b502d JNZ (serial mismatch) both yield null, which
	//    `World->Resolve` answers the same way.
	FElysiumEntity* const Last = World != nullptr ? World->Resolve(BaseMemory.LastEnemy) : nullptr; // 0x102b5009 .. 0x102b502f
	if (Last == Entity)                                                   // 0x102b5037
	{
		SetLastEnemy(nullptr);                                            // 0x102b503d
	}
	// 4. `CAI_Enemies::ClearMemory` (`0x102dfaa0`) on slot 541 with the `"%s(%d) :"` reason
	//    (`0x101d3730`, line `0x5511`, debug text; the call 0x102b5051 formats that reason string,
	//    which `ClearMemory` only carries for its debug trace, so the port has no line for it).
	ClearEnemyMemoryRecord(Entity);                                       // 0x102b505f / 0x102b5067
}

// --- Slot 379, the Troika fill --------------------------------------------------------------------

bool FElysiumNpc::EnterGrappleState(const FElysiumEntityHandle& Partner, EElysiumGrappleRole Role,
	EElysiumGrappleType Type, int32 Position, bool bHolster)
{
	// `CAI_BaseNPCTroika::EnterGrappleState` (`0x102b5c00`), slot 379, whole and in order. No
	// grapple-type gate anywhere. Moved here from `ElysiumNpc.cpp` (story 25a's body) by lane L11,
	// which corrected three divergences: step 2 inlined the base body instead of calling it, step 1
	// wrote a COPY of each queued record, and step 3 read `Dialogue.bInDialog` where retail asks
	// `IsInDialog` (`0x102c1170`, which also counts a talking NPC).

	// 1. A NON-EMPTY queued-burn list (`m_QueuedBurnDamage` `+0x65a8`, count `+0x65b4`, records of
	//    `0x4c` bytes) is DISCHARGED INTO THE PARTNER and the grapple is REFUSED. The list is left
	//    standing; the next attempt discharges it again.
	if (QueuedBurnDamage.Num() > 0)                                       // 0x102b5c0f
	{
		FElysiumEntity* const PartnerEntity = World != nullptr ? World->Resolve(Partner) : nullptr;
		FElysiumCombatCharacter* const PartnerCharacter =
			PartnerEntity != nullptr ? PartnerEntity->AsCombatCharacter() : nullptr;
		for (FElysiumDmg& Queued : QueuedBurnDamage)                      // 0x102b5c14 .. 0x102b5c56
		{
			// The record is written IN PLACE, as retail's is: the writes persist in the queue.
			Queued.Inflictor = Handle;                                    // 0x102b5c20 rec+0x2c
			Queued.Source = Handle;                                       // 0x102b5c23 rec+0x28
			const FElysiumDmg& Burn = Queued;
			// `RandomInt(0, 1)` (`0x102b5c26`); ZERO takes 5 (`0x102b5c33`), then `0x101c2a10(rec,
			// hitbox)` (`0x102b5c3b`).
			LastGrappleBurnHitbox =
				ElysiumRng::Stream(EElysiumRngStream::Reaction).RandRange(0, 1) == 0 ? 5 : 4;
			// SEAM: this runtime's damage packet carries no hit group, so the hitbox is recorded
			// rather than written into the descriptor.
			++GrappleBurnDischarges;
			if (PartnerCharacter != nullptr)
			{
				PartnerCharacter->TakeDamage(Burn, this);                 // 0x102b5c45
			}
		}
		return false;                                                     // 0x102b5c5b -> 0x102b5d29
	}

	// 2. `CAI_BaseNPC::EnterGrappleState` (`0x1026cdc0`); its `AL` gates the rest.
	if (!FElysiumNpcBase::EnterGrappleState(Partner, Role, Type, Position, bHolster)) // 0x102b5c86
	{
		return false;                                                     // 0x102b5c8d -> 0x102b5d29
	}
	// 3. `IsInDialog` (`0x102c1170`) -> the dialogue stop (`0x102c0bb0`).
	if (IsInDialog())                                                     // 0x102b5c95 / 0x102b5c9c
	{
		StopDialogOnRemove();                                             // 0x102b5ca0
	}
	// 4. A LIVE `m_hCine` (`+0x5d74`) -> `0x101a8c30` on it, then `SetState(m_IdealNPCState)`
	//    (`0x1026e340`) when the current state is not already it.
	// Liveness: 0x102b5cae JZ (handle -1), 0x102b5cce JNZ (serial mismatch), 0x102b5cd3 JZ (null
	// table entry). The re-resolve at 0x102b5cd5 (its 0x102b5cde JZ -1 and 0x102b5cf5 JNZ serial
	// tests, both to a null `this`) cannot fail after that check; the `Resolve` below stands for it.
	if (ScriptOwnerIsLive() && World != nullptr)                          // 0x102b5cae .. 0x102b5cd3
	{
		if (FElysiumEntity* const Owner = World->Resolve(ScriptOwner))
		{
			Owner->CancelScriptedSequenceForDialogue(Handle);             // 0x102b5cfd
		}
		if (NpcStateRetail() != IdealStateRetail())                       // 0x102b5d10
		{
			SetState(IdealStateRetail());                                 // 0x102b5d15
		}
	}
	// 5. `ClearSchedule` (`0x10280d30`), then TRUE.
	ClearSchedule();                                                      // 0x102b5d1c
	return true;                                                          // 0x102b5d26
}

// --- Slot 259, the Troika fill --------------------------------------------------------------------

namespace
{
	// `0x1029b58f`-`0x1029b5a7`: `"%s %f %f %f"` into (name, total, fade-in, fade-out) with the
	// defaults 1.0 / 0.2 / 0.2; answers `sscanf`'s count (-1 for an input with no token at all).
	int32 Misc19ScanExpression(const FString& Options, FString& OutName, float& InOutTotal,
		float& InOutIn, float& InOutOut)
	{
		const TCHAR* Cursor = *Options;
		while (*Cursor != 0 && FChar::IsWhitespace(*Cursor))
		{
			++Cursor;
		}
		if (*Cursor == 0)
		{
			return -1;                                                     // EOF before the first field
		}
		const TCHAR* Start = Cursor;
		while (*Cursor != 0 && !FChar::IsWhitespace(*Cursor))
		{
			++Cursor;
		}
		OutName = FString::ConstructFromPtrSize(Start, static_cast<int32>(Cursor - Start));
		int32 Count = 1;
		float* const Fields[] = { &InOutTotal, &InOutIn, &InOutOut };
		for (float* const Field : Fields)
		{
			// `%f`: skip whitespace, then [sign] digits [. digits] [e [sign] digits]; no digit fails
			// the conversion and stops the count.
			const TCHAR* P = Cursor;
			while (*P != 0 && FChar::IsWhitespace(*P))
			{
				++P;
			}
			const TCHAR* const FieldStart = P;
			if (*P == TEXT('+') || *P == TEXT('-'))
			{
				++P;
			}
			bool bDigits = false;
			while (FChar::IsDigit(*P))
			{
				++P;
				bDigits = true;
			}
			if (*P == TEXT('.'))
			{
				++P;
				while (FChar::IsDigit(*P))
				{
					++P;
					bDigits = true;
				}
			}
			if (!bDigits)
			{
				break;                                                     // `%f` fails: the count stops
			}
			if (*P == TEXT('e') || *P == TEXT('E'))
			{
				const TCHAR* Exponent = P + 1;
				if (*Exponent == TEXT('+') || *Exponent == TEXT('-'))
				{
					++Exponent;
				}
				if (FChar::IsDigit(*Exponent))
				{
					P = Exponent;
					while (FChar::IsDigit(*P))
					{
						++P;
					}
				}
			}
			const FString Token = FString::ConstructFromPtrSize(FieldStart, static_cast<int32>(P - FieldStart));
			*Field = static_cast<float>(FCString::Atod(*Token));
			Cursor = P;
			++Count;
		}
		return Count;
	}

	// `0x1029b58f`-`0x1029b5a7`: the `sscanf` defaults.
	constexpr float Misc19ExpressionDefaultTotal = 1.0f;
	constexpr float Misc19ExpressionDefaultFade = 0.2f;
	// `TASK_DO_INTEREST_ACTIVITY`, the class-local task id `0x1029b70d` / `0x1029b817` compare.
	constexpr int32 Misc19TaskDoInterestActivity = 0xb4;
	// The PAS `EmitSound` soundlevel every sound arm here passes (`0x42`, 66 dB).
	constexpr int32 Misc19AnimSoundLevel = 0x42;
}

bool FElysiumNpc::HandleAnimEvent(const FElysiumAnimEvent& Event)
{
	// `CAI_BaseNPCTroika::HandleAnimEvent` `0x1029b290`, slot 259 (the Troika line and 51 classes).
	// Retail returns void and every claimed arm returns without calling the base, the early-outs
	// included, so every claimed arm here answers `true`; only an unclaimed id chains.

	// `0x1029b29c`: `GetCurTask()` (`0x1028a150`) first — `m_pSchedule->tasks + m_iTaskIndex`, or
	// null. The port's step carries the GLOBAL task id; the class-local number is what retail's
	// record holds.
	const FElysiumScheduleProgram* const Program = ElysiumScheduleFor(Schedule.Current);
	const FElysiumScheduleStep* const CurTask =
		(Program != nullptr && Program->Tasks.IsValidIndex(Schedule.TaskIndex))
		? &Program->Tasks[Schedule.TaskIndex] : nullptr;
	const bool bDoInterestActivity = CurTask != nullptr
		&& GlobalToLocalId(IdSpace(EElysiumIdCategory::Task), CurTask->TaskId)
			== Misc19TaskDoInterestActivity;
	IElysiumAudio* const Audio = World != nullptr ? World->Audio() : nullptr;

	// Dispatch: `0x1029b2b0` JG (> 0x80c -> `0x1029b668`, where `0x1029b66e` JG sends > 0x1039 to
	// `0x1029b7f1` (`0x1029b803` JG past 0x103b -> base) and `0x1029b68e` JLE sends <= 0x1035 to the
	// base); `0x1029b2c5` JA (id - 0x7d5 > 0x23 -> base) and the table jump `0x1029b2d3` through the
	// byte table `0x1029b990` into `0x1029b97c`. Every sound arm below builds a
	// `CPASAttenuationFilter` around slot 222 `EyePosition` (`vt+0x378`) and tears it down after the
	// emit; the port's sound request carries the sample, volume, level and channel, the filter being
	// the Source server's recipient set: `0x7d5` `0x1029b3d2` / `0x1029b3de` / `0x1029b3f0` /
	// `0x1029b40b` / `0x1029b416`, edict `0x1029b42a`, origin `0x1029b44b`; the stop arm `0x1029b729` /
	// `0x1029b735` / `0x1029b747` / `0x1029b762` / `0x1029b76d`, edict `0x1029b781`, origin
	// `0x1029b7a2`, teardown (shared with `0x7d5`) `0x1029b7c5` / `0x1029b7ce` / `0x1029b7df`; the
	// interesting-place arm `0x1029b8a7` / `0x1029b8b3` / `0x1029b8c5` / `0x1029b8e0` / `0x1029b8eb`,
	// edict `0x1029b8ff`, origin `0x1029b921`, teardown `0x1029b94f` / `0x1029b958` / `0x1029b969`.
	switch (Event.Event)
	{
	case 0x80c:                                                           // 0x1029b2b6
	{
		// EVENT_EXPRESSION: `sscanf(opt, "%s %f %f %f")`.
		FString Name;
		float Total = Misc19ExpressionDefaultTotal;
		float In = Misc19ExpressionDefaultFade;
		float Out = Misc19ExpressionDefaultFade;
		const int32 Count = Misc19ScanExpression(Event.Options, Name, Total, In, Out); // 0x1029b5b7
		if (Count <= 1)                                                   // 0x1029b5c2
		{
			return true;
		}
		// `0x1029b5cc FCOMP float ptr [0x104454c4]` — the image's 0.0 cell.
		if (!(Total > ElysiumNpcTunables::Zero))                          // 0x1029b5d9 (<= 0 or unordered)
		{
			return true;
		}
		// The x87 keeps the sum and the ratio at extended precision and rounds only at each store
		// (`0x1029b5fe` / `0x1029b606` / `0x1029b629`); double stands for the 80-bit register.
		const double Sum = static_cast<double>(Out) + static_cast<double>(In); // 0x1029b5df / 0x1029b5e3
		if (Sum > static_cast<double>(Total))                             // 0x1029b5e7 / 0x1029b5f2
		{
			const double Ratio = static_cast<double>(Total) / Sum;        // 0x1029b5f4 FDIVR
			In = static_cast<float>(static_cast<double>(In) * Ratio);     // 0x1029b5fc / 0x1029b5fe
			Out = static_cast<float>(static_cast<double>(Out) * Ratio);   // 0x1029b602 / 0x1029b606
		}
		const double HoldWide = static_cast<double>(Total)
			- (static_cast<double>(Out) + static_cast<double>(In));       // 0x1029b60e .. 0x1029b616
		// `0x1029b61a FCOM float ptr [0x104454c4]`.
		const float Hold = HoldWide > static_cast<double>(ElysiumNpcTunables::Zero)
			? static_cast<float>(HoldWide) : ElysiumNpcTunables::Zero;    // 0x1029b627 / 0x1029b629 / 0x1029b631
		SetExpressionMisc19(Name, 0.f, In, Hold, Out, 1.f);               // 0x1029b656
		return true;
	}
	case 0x7d5:                                                           // 0x1029b3c6
	{
		// `EmitSound(PAS filter, entindex, CHAN_AUTO, option-as-sample, 1.0, 0x42, 0, 100)`.
		if (Audio != nullptr)
		{
			FElysiumBodySound Sound;
			Sound.Rel = Event.Options;
			Sound.Volume = 1.f;
			Sound.SoundLevelDb = Misc19AnimSoundLevel;
			Sound.Pitch = 1.f;
			Sound.Channel = EElysiumSoundChannel::Auto;
			Audio->PlayBodySound(Handle, Sound);                          // 0x1029b46c
		}
		return true;
	}
	case 0x7d6:                                                           // 0x1029b474
	{
		// The scripted self-kill — an inlined `CBaseCombatCharacter::Die(attacker, 1, 0)`
		// (`0x103392c0`) WITHOUT its `m_lifeState != 2` guard.
		FElysiumDmg Dmg;                                                  // 0x1029b478 CVDmg_t
		Dmg.Source = Handle;                                              // 0x1029b482 SetSrc(this)
		Dmg.BaseDamage = 1;                                               // 0x1029b495 m_iDiceAmt
		Dmg.ExtraInput = 1;                                               // 0x1029b499 m_iToHitSuccesses
		FElysiumEntity* const Closest =
			World != nullptr ? World->Resolve(Senses.Memory.ClosestPlayer) : nullptr; // 0x1029b487 .. 0x1029b4bc (-1 0x1029b49d, serial 0x1029b4ba)
		FElysiumTakeDamageInfo Info;                                      // 0x1029b4dc 0x101c26d0
		Info.Dmg = &Dmg;
		Info.Attacker = Closest != nullptr ? Closest->Handle : FElysiumEntityHandle::Invalid();
		Info.Damage = 1.f;
		Info.DamageBits = 0;
		Info.AmmoType = INDEX_NONE;
		// `0x1029b4e9` `0x101c2a90(1)`: `CTakeDamageInfo +0x48 := 1`. SEAM: the packet has no such
		// field; the flag's meaning is unrecovered.
		// `0x1029b4ee`-`0x1029b546`: on the first stat list whose `+0x10` is 0 (the sheet; the walk
		// `0x1029b4f8` / `0x1029b508` / `0x1029b510`, with the static empty list's one-time
		// construction `0x1029b519` / `0x1029b529` / `0x1029b533` as its fallback),
		// `SetBaseToStatValue(0xf, 0x11)` — wounds taken := max health.
		Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::Health,
			TypedStatValue(0, ElysiumSlot::MaxHealth));
		RecomputeSheet();
		Event_Killed(&Info);                                              // 0x1029b557, slot 144
		Event_Dying();                                                    // 0x1029b561, slot 403
		return true;                                                      // 0x1029b56b the CVDmg_t destructor
	}
	case 0x7e5:                                                           // 0x1029b2da
		// `0x1029b2dc` -> `0x10293d70`: `m_afMemory &= ~0x2000` (the turn-finished bit).
		BaseScheduleHost.MemoryBits &= ~0x2000u;
		return true;
	case 0x7f8:                                                           // 0x1029b2ee
	{
		// NPC_PICKUP: `m_hTargetEnt` (`+0x5ce4`) as a `CBaseCombatWeapon` (every VtMB item is one).
		// -1 (`0x1029b2f7`) or a stale serial (`0x1029b314`) resolves null.
		FElysiumEntity* const PickupTarget = World != nullptr ? World->Resolve(GetTarget()) : nullptr;
		FElysiumItem* const Item = PickupTarget != nullptr ? PickupTarget->AsItem() : nullptr; // 0x1029b32b
		if (Item == nullptr)                                              // 0x1029b337
		{
			TaskFailText(TEXT("Weapon stolen by someone else"));          // 0x1029b3b3
			return true;
		}
		// `0x102521f0`: the weapon's owner (`+0x88c`) resolves to a combat character — this NPC
		// included.
		const FElysiumEntity* const Owner = World->Resolve(Item->Owner);
		if (Owner != nullptr && Owner->AsCombatCharacter() != nullptr)   // 0x1029b33b / 0x1029b344
		{
			TaskFailText(TEXT("Weapon in use by someone else"));          // 0x1029b34d
			return true;
		}
		if (!Weapon_CanUse(Item))                                         // 0x1029b363 / 0x1029b36d
		{
			TaskFailText(TEXT("Can't use this weapon type"));             // 0x1029b376
			return true;
		}
		Weapon_Equip(Item, false);                                        // 0x1029b38e
		TaskComplete(false);                                              // 0x1029b398
		return true;
	}
	case 0x80d:                                                           // 0x1029b686
	{
		if (Event.Options.IsEmpty())                                      // 0x1029b6b6 / 0x1029b6bb
		{
			return true;
		}
		// `SequenceDuration(m_nSequence) * m_flCycle + 0.1` — the time ELAPSED in the sequence.
		// `0x1029b6db FADD double ptr [0x104493d0]` — the image's 0.1 DOUBLE, added at x87 width.
		const float Hold = static_cast<float>(static_cast<double>(SequenceDurationOf(SequenceNumber))
			* static_cast<double>(SequenceCycle) + ElysiumNpcTunables::TenthDouble); // 0x1029b6c6 .. 0x1029b6e1
		SetExpressionMisc19(Event.Options, 0.f, 0.f, Hold, 0.f, 1.f);     // 0x1029b6f1
		return true;
	}
	case 0x1036:
	case 0x1037:
	case 0x103a:
	case 0x103b:                                                          // 0x1029b696 / 0x1029b7f7..803
	{
		if (!bDoInterestActivity)                                         // 0x1029b80b / 0x1029b817
		{
			return true;
		}
		// `IsMale` (`0x10336920`): the sheet's gender stat `== 1` picks the directory (`0x1029b82b`).
		const TCHAR* const Gender =
			TypedStatValue(0, ElysiumSlot::Gender) == 1 ? TEXT("male") : TEXT("female"); // 0x1029b81f
		// The filesystem test (`DAT_1070b238` slot 9) on `sound/Interesting_places/%s/%s.wav`
		// picks the gendered path, else the flat one.
		const FString Gendered = FString::Printf(TEXT("Interesting_places/%s/%s.wav"), Gender,
			*Event.Options);                                              // 0x1029b845 / 0x1029b865
		FElysiumBodySound Sound;
		Sound.Rel = ElysiumSoundAssets::Exists(Gendered) ? Gendered       // 0x1029b854 / 0x1029b859
			: FString::Printf(TEXT("Interesting_places/%s.wav"), *Event.Options); // 0x1029b878
		Sound.Channel = (Event.Event == 0x1036 || Event.Event == 0x103a)  // 0x1029b88a / 0x1029b897
			? EElysiumSoundChannel::Body : EElysiumSoundChannel::Voice;
		Sound.Volume = 1.f;
		Sound.SoundLevelDb = Misc19AnimSoundLevel;
		Sound.Pitch = 1.f;
		// Retail's flags `0x180` have no field on the body-sound request.
		if (Audio != nullptr)
		{
			Audio->PlayBodySound(Handle, Sound);                          // 0x1029b948
		}
		return true;
	}
	case 0x1038:
	case 0x1039:                                                          // 0x1029b67a
	{
		if (!bDoInterestActivity)                                         // 0x1029b705 / 0x1029b70d
		{
			return true;
		}
		// Engine sound slot 11 with an empty sample and `SND_STOP | SND_STOP_LOOPING` (0x24): stop
		// channel 4 (`0x1038`) or 2 (`0x1039`). `StopEntitySounds` is the port's stop-by-entity verb.
		if (Audio != nullptr)
		{
			Audio->StopEntitySounds(Handle,
				Event.Event == 0x1038 ? static_cast<int32>(EElysiumSoundChannel::Body)
					: static_cast<int32>(EElysiumSoundChannel::Voice));   // 0x1029b7be
		}
		return true;
	}
	default:
		break;
	}
	// `0x1029b69c` / `0x1029b69f`: every id this layer does not claim — `CAI_BaseNPC::HandleAnimEvent`
	// `0x10274e30`.
	return FElysiumNpcBase::HandleAnimEvent(Event);
}

void FElysiumNpc::SetExpressionMisc19(const FString& Name, float Delay, float FadeIn, float Duration,
	float FadeOut, float Scale)
{
	// SEAM for `CBaseCombatCharacter::SetExpression` (`0x10106580`): `FadeoutExpressions`, the
	// dialog-partner scale adjustment (constants `_DAT_1044fab0` / `_DAT_10449280` /
	// `_DAT_10450aa4` unrecovered), then `AddScriptedExpression(name, fadeIn, fadeOut, scale, delay,
	// duration)`. The recorder the port has for the last call is `AddScriptedExpression`; the fade-out
	// of the running expressions and the partner scale are not built.
	++SetExpressionMisc19Calls;
	AddScriptedExpression(Name, FadeIn, FadeOut, Scale, Delay, Duration);
}

void FElysiumNpc::RecordAnimEventShake(const FVector& CentreUnits, float Amplitude, float Frequency,
	float Duration, float Radius, bool bAirShake)
{
	// SEAM for `UTIL_ScreenShake` (`0x101cdba0`) — see the declaration.
	FAnimEventShakeCall Call;
	Call.CentreUnits = CentreUnits;
	Call.Amplitude = Amplitude;
	Call.Frequency = Frequency;
	Call.Duration = Duration;
	Call.Radius = Radius;
	Call.bAirShake = bAirShake;
	AnimEventShakeCalls.Add(Call);
}
