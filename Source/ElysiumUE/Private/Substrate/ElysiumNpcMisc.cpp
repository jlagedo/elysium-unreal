#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"

// Story 29c-1, family **Misc** — the 37 layer 0–9 rows that belong to no other family's concern.
//
// Fourteen of them fill Troika-line vtable slots (24, 272, 296, 347, 424–430, 513, 590, 592) and the
// rest are retail helpers, free functions and per-species overrides. What the rows share is only
// that no other family owns them; each one is read off the decompiled C and cited by address.
//
// THE STANDING FACTS OF THIS FAMILY, all three already recorded elsewhere in the shape map and
// re-stated once here so no body below has to argue them again:
//
//   * There is NO `CAI_StandoffBehavior` object (slots 4/26/28 below are pure functions over typed
//     views, exactly as family **Lifecycle** landed slot 13).
//   * There is NO `CAI_Senses` / `CAI_Motor` / `CAI_MoveProbe` / `CAI_LocalNavigator` /
//     `CAI_Navigator` / `CAI_Pathfinder` component (slots 424–430). `+0x5cdc` is bound to
//     `FElysiumNpc::Senses`, a struct on the NPC; `+0x5d34`..`+0x5d44` are `_CHAIN` rows pointing at
//     the one `IElysiumNpcMotor` seam.
//   * There is NO `MeleeMoveRecord_t` array (`+0x6028`, `ELYSIUM_NPC_WORD_ABSENT`), which is the
//     whole of slot 24's Troika-line body.
//
// The walked prose is `docs/vtmb/npc-ai/shape.md` and `docs/vtmb/npc-ai/conditions-and-states.md`.

namespace
{
	constexpr float GMiscOneFloat = ElysiumNpcTunables::One;

	// `m_bfAINPCFlags` (+0x14b8) bit `0x20`, the carry bit `0x10381c00` toggles.
	constexpr EElysiumNpcFlag GMiscCarryingBody = EElysiumNpcFlag::CARRYING_BODY;

	// `CNPC_VHengeyokai`'s fish-timer draw, `0x40a00000` / `0x41000000` at `0x10381c00`.
	constexpr float GMiscFishTimerMin = 5.f;
	constexpr float GMiscFishTimerMax = 8.f;

	// The two conditions slot 592 reads, by retail number.
	constexpr EElysiumNpcCond GMiscCoverEnemyOccluded = EElysiumNpcCond::EnemyOccluded;      // 0x48
	constexpr EElysiumNpcCond GMiscCoverCanRangeAttack1 = EElysiumNpcCond::CanRangeAttack1;  // 0x4f

	// `s_Knockback_1056bdcc`, the one expression-event name in the image.
	const TCHAR* const GMiscKnockbackEventName = TEXT("Knockback");

	// `CAI_BaseNPCTroika::OkToInterruptForMelee`'s activity whitelist (`0x1029f940`), transcribed
	// from the switch and the three range tests. Singles first, then the three closed ranges.
	constexpr int32 GMiscMeleeInterruptSingles[] =
	{
		0x1, 0x9, 0x13, 0x30, 0x4b, 0x4d, 0x51, 0xcb5, 0xd25, 0x1121,
	};
	struct FMiscActivityRange { int32 Low; int32 High; };
	constexpr FMiscActivityRange GMiscMeleeInterruptRanges[] =
	{
		{ 0x73, 0x8a }, { 0x1157, 0x1158 },
	};
}

// -------------------------------------------------------------------------------------------------
// `CAI_StandoffBehavior` — slots 4, 26 and 28 of the BEHAVIOUR's own vtable.
// -------------------------------------------------------------------------------------------------

// -------------------------------------------------------------------------------------------------
// Slot 347 `GetExpressionEventParams` — `0x102c1c10`, over the base `0x10014ba0`.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::CombatCharacterExpressionEventParams(int32 Event, TCHAR* OutName, float* OutA,
	float* OutB, float* OutC, float* OutD) const
{
	// `CBaseCombatCharacter::GetExpressionEventParams` `0x10014ba0`, the base every class but
	// `CAI_BaseNPCTroika` answers slot 347 with. Two arms and one dead default:
	//     if (0 <= e && e < 2) {
	//         if (e == 0) { "Knockback", 1.0f, 0.1f, 1.0f, 0.2f;  return true; }
	//         if (e == 1) { "Knockback", 1.0f, 0.25f, 2.0f, 0.5f; return true; }
	//         DevWarning("Unhandled Expression Event: %s (%d)", e);
	//     }
	//     return false;
	//
	// The `DevWarning` is UNREACHABLE — the range test admits only 0 and 1 and both are handled
	// above it. Reproduced as unreachable rather than tidied away; it is the compiler's switch
	// default and its presence is what says the enum once had more members.
	if (Event != 0 && Event != 1)
	{
		return false;
	}
	if (OutName != nullptr)
	{
		FCString::Strncpy(OutName, GMiscKnockbackEventName, ExpressionEventNameChars);
	}
	if (OutA != nullptr) { *OutA = 1.f; }                          // 0x3f800000, both arms
	if (OutB != nullptr) { *OutB = Event == 0 ? 0.1f : 0.25f; }    // 0x3dcccccd / 0x3e800000
	if (OutC != nullptr) { *OutC = Event == 0 ? 1.f : 2.f; }       // 0x3f800000 / 0x40000000
	if (OutD != nullptr) { *OutD = Event == 0 ? 0.2f : 0.5f; }     // 0x3e4ccccd / 0x3f000000
	return true;
}

bool FElysiumNpc::GetExpressionEventParams(int32 Event, TCHAR* OutName, void* OutA, void* OutB,
	void* OutC, void* OutD)
{
	// `CAI_BaseNPCTroika::GetExpressionEventParams` `0x102c1c10`:
	//     if (param_1 != 1) return CBaseCombatCharacter::GetExpressionEventParams(...);
	//     Q_strncpy(name, "Knockback", 0x40);
	//     *a = 1.0f; *b = 0.25f; *c = 0.8f; *d = 0.5f;
	//     seq = SelectHeaviestSequence(this, m_knockbackType, -1);     // 0x10004ec1
	//     if (seq >= 0) *c += SequenceDuration(this, seq);             // 0x10009813
	//     return true;
	//
	// The Troika line KEEPS the base's event-1 name and its first, second and fourth floats and
	// replaces only the third: `2.0` becomes `0.8` plus the knockback clip's own length. So the
	// third float is "how long the expression holds" and the override makes it track the animation
	// instead of a fixed two seconds. Event 0 is NOT special-cased and falls to the base.
	//
	// `SelectHeaviestSequence` is family **TroikaHelpers**' seam (answering `INDEX_NONE`) and
	// `SequenceDurationOf` is family **Anim**'s (answering 0), so the extension term is not taken
	// today and the answer is the flat `0.8`. Both are asked rather than skipped, so the day either
	// lands the arm opens without a change here.
	float* const A = static_cast<float*>(OutA);
	float* const B = static_cast<float*>(OutB);
	float* const C = static_cast<float*>(OutC);
	float* const D = static_cast<float*>(OutD);
	if (Event != 1)
	{
		return CombatCharacterExpressionEventParams(Event, OutName, A, B, C, D);
	}
	if (OutName != nullptr)
	{
		FCString::Strncpy(OutName, GMiscKnockbackEventName, ExpressionEventNameChars);
	}
	if (A != nullptr) { *A = 1.f; }      // 0x3f800000
	if (B != nullptr) { *B = 0.25f; }    // 0x3e800000
	if (C != nullptr) { *C = 0.8f; }
	if (D != nullptr) { *D = 0.5f; }     // 0x3f000000
	const int32 Sequence = SelectHeaviestSequence(KnockbackType, INDEX_NONE);
	if (Sequence >= 0 && C != nullptr)
	{
		*C += SequenceDurationOf(Sequence);
	}
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slots 424–430 — the component factories.
// -------------------------------------------------------------------------------------------------

const FElysiumNpc::FComponentFactory* FElysiumNpc::ComponentFactoryRows(int32& OutCount)
{
	// Every row was read off the decompiled C of the body it names, not off a summary. The species
	// row is the whole of this family's live override set for the band: `CNPC_VRat` replaces the
	// local navigator. The only other overrides of 424–430 are `CAI_ExpressiveNPC`'s expresser
	// factory at 424 (`0x10312cd0`) and `CAI_BaseHumanoid`'s humanoid motor (`0x10260f40`) and
	// navigator (`0x10262430`); neither class has an instance, and 0019 story 5 step 1 removed
	// their rows (census only).
	static constexpr FComponentFactory Rows[] =
	{
		// slot, class, body, bytes, constructor, vftable assigned after construction
		{ 424, TEXT("CAI_BaseNPC"), TEXT("0x1027cae0"), 0, TEXT(""), TEXT("") },
		{ 425, TEXT("CAI_BaseNPC"), TEXT("0x1027cc10"), 0x88, TEXT("0x1027f8f0"),
			TEXT("vftable_CAI_Senses") },
		{ 426, TEXT("CAI_BaseNPC"), TEXT("0x1027cef0"), 0x14, TEXT(""),
			TEXT("vftable_CAI_MoveProbe") },
		{ 427, TEXT("CAI_BaseNPC"), TEXT("0x1027cec0"), 0x6c, TEXT("0x102e0900"), TEXT("") },
		{ 428, TEXT("CAI_BaseNPC"), TEXT("0x1027cf60"), 0x20, TEXT("0x102ddab0"), TEXT("") },
		{ 428, TEXT("CNPC_VRat"), TEXT("0x103ad6a0"), 0x20, TEXT("0x103ad540"), TEXT("") },
		{ 429, TEXT("CAI_BaseNPC"), TEXT("0x1027cf90"), 0x68, TEXT("0x102eca50"), TEXT("") },
		{ 430, TEXT("CAI_BaseNPC"), TEXT("0x1027cfc0"), 0x18, TEXT(""),
			TEXT("vftable_CAI_Pathfinder") },
	};
	OutCount = UE_ARRAY_COUNT(Rows);
	return Rows;
}

// -------------------------------------------------------------------------------------------------
// Slot 590 `OkToInterruptForMelee` — `0x1029f940`, plus `CNPC_VSabbatLeader`'s `0x103ab400`.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::OkToInterruptForMelee()
{
	// `CAI_BaseNPCTroika::OkToInterruptForMelee` `0x1029f940` (`CNPC_VSabbatLeader` overrides it,
	// story 5 step 3):
	//     if (!OkToDisturb()) return false;                           // 0x1028a190
	//     switch (m_Activity) { 1, 9, 0x13, 0x30, 0x4b, 0x4d, 0x51,
	//                           0x73..0x8a, 0xcb5, 0xd25, 0x1121, 0x1157..0x1158: return true; }
	//     return false;
	//
	// The compiler split the whitelist three ways — a jump table below `0x52`, then a `< 0xd26`
	// band, then the tail — and `0x51` is the one member hoisted out of the switch (`if (uVar1 !=
	// 0x51)`) because it sits at the table's edge. It IS a member: the `!= 0x51` test falls THROUGH
	// to the accept, it does not reject. 29c's walk lists the set without it; corrected here.
	if (!OkToDisturb())
	{
		return false;
	}
	for (int32 Single : GMiscMeleeInterruptSingles)
	{
		if (ActivityNumber == Single)
		{
			return true;
		}
	}
	for (const FMiscActivityRange& Range : GMiscMeleeInterruptRanges)
	{
		if (Range.Low <= ActivityNumber && ActivityNumber <= Range.High)
		{
			return true;
		}
	}
	return false;
}

// -------------------------------------------------------------------------------------------------
// Slot 592 `CanSeekCover` — `0x102953e0`, plus `CNPC_VLasombra`'s `0x103893c0`.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::CanSeekCover()
{
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;

	// `CAI_BaseNPCTroika::FUN_102953e0` `0x102953e0` (`CNPC_VLasombra` overrides it, story 5
	// step 3), three arms in order:
	//     if (HasCondition(COND_ENEMY_OCCLUDED 0x48)) return true;
	//     if (m_flCanSeekCoverTimer (+0x607c) <= curtime) return true;
	//     if (m_flCanSeekCoverTimer - 1.0f <= curtime && !HasCondition(COND_CAN_RANGE_ATTACK1 0x4f))
	//         return true;
	//     return false;
	//
	// **The slop window is ONE second, not five.** `0x10295414` is `FSUB [0x104454c0]` and
	// `_DAT_104454c0` is the image's shared `1.0f`, read out of the listing at
	// `docs/vtmb/animation_and_movers.md:659` (`FLD dword ptr [0x104454c0] ; 1.0f`). 29c's walk says
	// five; corrected here and in the walked paragraph.
	//
	// The last arm's `(a < b) != (a == b)` is the FPU flag pair for `a <= b`, the same spelling
	// family TroikaHelpers recorded on slot 602.
	if (Cognition.Conditions.Has(GMiscCoverEnemyOccluded))
	{
		return true;
	}
	if (CanSeekCoverTimer <= Now)
	{
		return true;
	}
	if (CanSeekCoverTimer - static_cast<double>(GMiscOneFloat) <= Now
		&& !Cognition.Conditions.Has(GMiscCoverCanRangeAttack1))
	{
		return true;
	}
	return false;
}

// -------------------------------------------------------------------------------------------------
// Slot 24 `OnVictimHitByMe` — `0x1029f8d0` and its four species bodies.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::ClearMeleeMoveRecords()
{
	// SEAM for `thunk_FUN_1028b160(&this->field_0x6028)` (`0x1028b160`): five iterations of three
	// dword stores from `+0x6028`, i.e. `MeleeMoveRecord_t m_MeleeMoveRecords[5]` zeroed whole. The
	// offset is `ELYSIUM_NPC_WORD_ABSENT` in the shape map, so the clear is counted and no word is
	// written. This is the ENTIRE Troika-line body of slot 24.
	++MeleeMoveRecordClears;
}

void FElysiumNpc::OnVictimHitByMe(FElysiumEntity* Victim)
{
	// `CAI_BaseNPCTroika::OnVictimHitByMe` `0x1029f8d0` — fourteen bytes whose whole body is
	// `thunk_FUN_1028b160(&this->field_0x6028)`. The victim argument is READ BY NOTHING.
	// `CNPC_VGargoyle`, `CNPC_VSabbatLeader`, `CNPC_VZombie` and `CNPC_VGhoulCroucher` override this
	// method on their C++ classes (story 5 step 3).
	(void)Victim;
	ClearMeleeMoveRecords();
}

// -------------------------------------------------------------------------------------------------
// `CNPC_VHengeyokai`'s form bit — `0x10381c00` and `0x10381ca0`.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::FormBit(bool bSet)
{
	// `0x10381c00`, both arms:
	//     if (param_1) {
	//         m_bfAINPCFlags |= 0x20;                                  // +0x14b8 CARRYING_BODY
	//         m_flFishTimer = RandomFloat(5.0f, 8.0f) + curtime;       // +0x666c
	//         m_bDidFakeThrow = 0;                                     // +0x667d
	//     } else {
	//         m_bfAINPCFlags &= ~0x20;
	//     }
	//
	// The order inside the true arm is retail's: the flag, then the draw, then the byte clear is
	// written BEFORE the timer store (`0x10381c2b` stores `+0x667d` at `0x10381c32`, the timer at
	// `0x10381c38`). Nothing observes the difference, but the draw happens before both stores and
	// that ordering is what a stream position depends on.
	//
	// The false arm does NOT touch the timer or the throw byte — a cleared carry leaves the fish
	// timer standing wherever the last pickup left it.
	if (!bSet)
	{
		NpcFlags.Clear(GMiscCarryingBody);
		return;
	}
	NpcFlags.Set(GMiscCarryingBody);
	const float Draw = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule)
		.FRandRange(GMiscFishTimerMin, GMiscFishTimerMax);
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	bHengeyokaiDidFakeThrow = false;
	HengeyokaiFishTimer = static_cast<float>(Now) + Draw;
}

// -------------------------------------------------------------------------------------------------
// The three remaining species bodies.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::DispatchVictimHitReaction(FElysiumEntity* Victim)
{
	// SEAM for `victim->vtable[+0x428](this)` — slot 266 dispatched ON THE VICTIM (`MOV ECX,ESI` at
	// `0x1037a54a`, the Gargoyle as the one pushed argument). On the `CAI_BaseNPC` line slot 266 is
	// the flinch-record clear (`0x100997f0`, family **EntityChain**'s `Slot266`), but the victim
	// here is a `pillar` prop from a different hierarchy that shares the index, and the census does
	// not carry its table — so what a pillar does with it is **unrecovered**.
	++VictimHitReactionDispatches;
	if (Victim != nullptr)
	{
		if (FElysiumNpc* VictimNpc = Victim->AsNpc())
		{
			VictimNpc->Slot266();
		}
	}
}
