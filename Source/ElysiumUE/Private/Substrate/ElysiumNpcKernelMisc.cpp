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
	// `.rdata` words this family's bodies compare against: shared pool constants with hundreds of
	// readers, bound to the tunables table.
	constexpr float GMiscZeroFloat = ElysiumNpcTunables::Zero;
	constexpr float GMiscOneFloat = ElysiumNpcTunables::One;
	constexpr double GMiscHalfDouble = ElysiumNpcTunables::HalfDouble;
	constexpr float GMiscYawHigh = ElysiumNpcTunables::OneTwenty;
	constexpr double GMiscZeroDouble = ElysiumNpcTunables::ZeroDouble;

	// `m_bfAINPCFlags` (+0x14b8) bit `0x20`, the carry bit `0x10381c00` toggles.
	constexpr EElysiumNpcFlag GMiscCarryingBody = EElysiumNpcFlag::CARRYING_BODY;

	// `m_bfAINPCFlags2` (+0x14bc) bit `0x1000` — `TEST AH,0x10` at `0x1028a213`. See the note in
	// `OkToDisturb` below: this IS a reader of `MADE_OBLIVIOUS`, which `ElysiumNpcFlags.h` records
	// as having none.
	constexpr EElysiumNpcFlag2 GMiscObliviousBit = EElysiumNpcFlag2::MADE_OBLIVIOUS;

	// `CNPC_VHengeyokai`'s fish-timer draw, `0x40a00000` / `0x41000000` at `0x10381c00`.
	constexpr float GMiscFishTimerMin = 5.f;
	constexpr float GMiscFishTimerMax = 8.f;

	// `CNPC_VYukie`'s two melee draws: `0x41b40000` / `0x42340000` at `0x103dd8b0` and
	// `0x40a00000` / `0x41200000` at `0x103dd9a0`.
	constexpr float GMiscYukieMustLeaveMin = 22.5f;
	constexpr float GMiscYukieMustLeaveMax = 45.f;
	constexpr float GMiscYukieCanEnterMin = 5.f;
	constexpr float GMiscYukieCanEnterMax = 10.f;

	// `CAI_StandoffBehavior::vfunc26`'s three literals, in retail's own write order.
	constexpr float GMiscStandoffAim5c = 5.f;       // 0x40a00000 -> this+0x5c
	constexpr float GMiscStandoffAim60 = 0.f;       // 0             -> this+0x60
	constexpr float GMiscStandoffAim58 = -1.f;      // 0xbf800000 -> this+0x58
	// `AimMode == 2` is the second arm's test; `> 0` is the first's.
	constexpr int32 GMiscStandoffAimModeLatch = 2;

	// `0x7f7fffff` — the `FLT_MAX` `vfunc4` stands in the owner's `m_flDistTooFar`.
	constexpr float GMiscFloatMax = 3.402823466e+38f;

	// `owner->+0x1fc = 1` (`0x102c74…`). Family **TroikaHelpers**' `StandoffVfunc5` writes 2 into the
	// same word; the identity of `+0x1fc` is unrecovered on both sides.
	constexpr int32 GMiscStandoffOwnerWord0x1fcValue = 1;

	// The two conditions slot 592 reads, by retail number.
	constexpr EElysiumNpcCond GMiscCoverEnemyOccluded = EElysiumNpcCond::EnemyOccluded;      // 0x48
	constexpr EElysiumNpcCond GMiscCoverCanRangeAttack1 = EElysiumNpcCond::CanRangeAttack1;  // 0x4f

	// `CGeneric_NPC::vfunc473`'s two answers.
	constexpr int32 GMiscSoundInterestIdle = 0x19;
	constexpr int32 GMiscSoundInterestOther = 0x17;

	// `CNPC_VBach`'s two answers per slot, and the shared refusal.
	constexpr int32 GMiscBachRange1Answer = 0x4f;   // COND_CAN_RANGE_ATTACK1
	constexpr int32 GMiscBachRange2Answer = 0x50;   // COND_CAN_RANGE_ATTACK2
	constexpr int32 GMiscBachRefusal = 0x61;        // COND_NOT_FACING_ATTACK

	// `CNPC_VSabbatLeader`'s melee-interrupt exception activity (`0x103ab400`).
	constexpr int32 GMiscSabbatLeaderMeleeActivity = 0x1141;

	// The hint type `CNPC_VChangBros::StoreArenaCenter` walks the global hint list for.
	constexpr int32 GMiscArenaCenterHintType = 0x4651;

	// The three hardcoded map entity names `0x103cade0` carries.
	const TCHAR* const GMiscWerewolfZoneName = TEXT("trigger_werewolf_zone");
	const TCHAR* const GMiscRotDoor1Name = TEXT("rotdoor1");
	const TCHAR* const GMiscRotDoor2Name = TEXT("rotdoor2");

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

void FElysiumNpc::StandoffVfunc4(FStandoffWords& Words, const FStandoffAimWords& Aim)
{
	// `0x102c7490`, arm by arm and in retail's own write order:
	//     this[0x4c] = 1;                                          // bSawNewEnemy
	//     this->+0x48 = 0;                                         // ReactionsLeft
	//     if (this->+0x44 == _DAT_104454c4)                        // ReactionDelayMax == 0.0f
	//         t = gpGlobals->curtime + this->+0x40;                //   the min alone
	//     else
	//         t = RandomFloat(this->+0x40, this->+0x44) + curtime;
	//     this->+0x3c = t;                                         // NextReactionAt
	//     this->+0x50 = owner->m_flDistTooFar;                     // +0x5de4, PARKED
	//     owner->m_flDistTooFar = 0x7f7fffff;                      // FLT_MAX
	//     if (this[0x1a]) owner->+0x1fc = 1;
	//
	// The `+0x44 == 0.0f` branch is the SAME "no range, use the min" convention family **Lifecycle**
	// recorded on `FStandoffWords::ReactionDelayMax`, so the two bodies agree by construction.
	//
	// `+0x50` is family **TroikaHelpers**' `StandoffDistTooFar`, and this is the write half of the
	// pair: `StandoffVfunc5` (`0x102c7530`) copies it BACK into `m_flDistTooFar` on the way out. A
	// standoff therefore parks the NPC's too-far distance for the length of the standoff and stands
	// `FLT_MAX` in its place, which is what makes no enemy too far while the behaviour holds.
	Words.bSawNewEnemy = true;
	Words.ReactionsLeft = 0;

	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	double Delay = static_cast<double>(Words.ReactionDelayMin);
	if (Words.ReactionDelayMax != GMiscZeroFloat)
	{
		// `(*DAT_1070b244 + 4)(min, max)` is `RandomFloat`, drawn from the one stream every NPC body
		// in this runtime draws from.
		Delay = static_cast<double>(ElysiumRng::Stream(EElysiumRngStream::NpcSchedule)
			.FRandRange(Words.ReactionDelayMin, Words.ReactionDelayMax));
	}
	Words.NextReactionAt = Now + Delay;

	StandoffDistTooFar = DistTooFar;
	DistTooFar = GMiscFloatMax;

	if (Aim.bForcesOwnerWord0x1fc)
	{
		Field_0x01fc = GMiscStandoffOwnerWord0x1fcValue;
	}
}

void FElysiumNpc::StandoffVfunc26(FStandoffWords& Words, FStandoffAimWords& Aim)
{
	// `0x102c7dd0`, the whole body:
	//     if (0 < this->+0x20) { this->+0x5c = 5.0f; this->+0x60 = 0.0f; this->+0x58 = -1.0f; }
	//     if (this->+0x20 == 2) this[0x4c] = 1;
	//
	// TWO independent tests, not an if/else — mode 2 takes both. The literals are immediates in the
	// body (`0x40a00000`, `0`, `0xbf800000`), not `.rdata` reads, so their values are exact.
	if (0 < Aim.AimMode)
	{
		Aim.AimWord0x5c = GMiscStandoffAim5c;
		Aim.AimWord0x60 = GMiscStandoffAim60;
		Aim.AimWord0x58 = GMiscStandoffAim58;
	}
	if (Aim.AimMode == GMiscStandoffAimModeLatch)
	{
		Words.bSawNewEnemy = true;
	}
}

bool FElysiumNpc::StandoffVfunc28()
{
	// `0x102c7ef0` — six bytes: `return DAT_10601874`.
	return StandoffSchedulesLoaded();
}

bool FElysiumNpc::StandoffSchedulesLoaded()
{
	// SEAM for `_DAT_10601874`. Its ONLY reader in the image is slot 28 above, and its only writers
	// are `CAI_StandoffBehavior`'s class initialiser `0x102c7eb0` and the loader `0x102c7f10` it
	// calls — which registers the behaviour's schedules, its two activities
	// (`ACT_RANGE_AIM_SMG1_LOW`, `ACT_RANGE_AIM_PISTOL_LOW`) and its conditions into the global id
	// space `DAT_10936b68`, and stores the LAST `0x1030d850` result back into the latch.
	//
	// This runtime has no behaviour id-space loader, so the initialiser never runs and the latch
	// keeps the zero its `.data` cell holds. Answering false is the recovered state, not a refusal
	// invented to close a branch.
	return false;
}

// -------------------------------------------------------------------------------------------------
// Slot 272 `AllocateLayer` — `0x10099470`.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpc::AllocateLayer()
{
	// `0x10099470`. The listing is the authority here, because the decompilation types the array as
	// `m_angPrevSeqAngles`: `EDX = (i + i*2) << 4` then `+ ESI + 0x748`, stride `0x30` — the gesture
	// layer table at `+0x0734` with `m_flWeight` at `+0x14` inside each 0x30-byte record. The scan
	// starts at `GetFirstGestureLayer()` (slot 267, `0x10098a40`), runs while the index is below 4,
	// and answers the first slot whose weight equals `_DAT_104454c4` (0.0f), else -1.
	//
	// Family **Anim** already carries that table and landed this exact body as
	// `AllocateGestureLayer()` beside slot 272's stub, precisely so its rows would not search a stub
	// that answers 0 for "not found". The slot now forwards to it; there is one scan, not two.
	return AllocateGestureLayer();
}

// -------------------------------------------------------------------------------------------------
// Slot 296 — `0x10348ba0`.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::Slot296(int32 Argument)
{
	// `0x10348ba0`, arm by arm:
	//     if (m_GrappleRole (+0x153c) == -1) return false;
	//     if (m_GrapplePartner (+0x1538) == INVALID_EHANDLE) return false;
	//     if (!resolve(m_GrapplePartner)) return false;           // PTR_DAT_10566458, & 0x1fff,
	//                                                             // generation >> 0xd
	//     return param_1 == 0xb;
	//
	// The three grapple terms are exactly `FElysiumGrappleState::IsPaired()` plus the world resolve
	// its comment says every consumer that can reach a world must also do.
	//
	// **Unrecovered: what `0xb` is.** Slot 296 has no dispatch site anywhere in the image (0d/0v/0c),
	// so nothing states the argument's domain; it is not a `m_GrappleType` value (those run 0..8).
	// The literal is reproduced as the literal.
	if (!Grapple.IsPaired())
	{
		return false;
	}
	if (World == nullptr || World->Resolve(Grapple.Partner) == nullptr)
	{
		return false;
	}
	return Argument == 0xb;
}

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
// Slot 513 `CapabilitiesGet` — `0x1026db30`.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpc::CapabilitiesGet() const
{
	// `0x1026db30`. Strip the scope-trace push/pop that brackets the body — retail's named debug
	// stack, which this runtime logs through its own channels — and the whole of it is:
	//     uint caps = m_afCapability;                             // +0x5cec
	//     if (GetActiveWeapon()) caps |= GetActiveWeapon()->vtable[+0x5a0]();   // slot 360
	//     return caps;
	//
	// Retail calls `GetActiveWeapon` TWICE, once for the null test and once for the dispatch; the
	// second call cannot answer differently, so one resolve carries both here.
	//
	// `ActiveWeaponCapabilityWord()` is family **Motor**'s seam for slot 360 (`0x1014f930`) and
	// answers 0 — there is no capability word on `FElysiumWeapon` — so today this is
	// `m_afCapability` verbatim, which is exactly what family Motor's own comment at `0x10278c60`
	// already assumed. Asking the seam rather than assuming it is what makes that assumption true.
	int32 Capabilities = CapabilityWord;
	const FElysiumEntity* Weapon = World != nullptr && Inventory.ActiveWeapon.IsSet()
		? World->Resolve(Inventory.ActiveWeapon)
		: nullptr;
	if (Weapon != nullptr)
	{
		Capabilities |= static_cast<int32>(ActiveWeaponCapabilityWord());
	}
	return Capabilities;
}

// -------------------------------------------------------------------------------------------------
// Slots 424–430 — the component factories.
// -------------------------------------------------------------------------------------------------

const FElysiumNpc::FComponentFactory* FElysiumNpc::ComponentFactoryRows(int32& OutCount)
{
	// Every row was read off the decompiled C of the body it names, not off a summary. The three
	// species rows are the whole of this family's override set for the band: `CAI_BaseHumanoid`
	// replaces the motor and the navigator with their humanoid variants, and `CNPC_VRat` replaces
	// the local navigator. Nothing else in the census overrides 424–430.
	static constexpr FComponentFactory Rows[] =
	{
		// slot, class, body, bytes, constructor, vftable assigned after construction
		{ 424, TEXT("CAI_BaseNPC"), TEXT("0x1027cae0"), 0, TEXT(""), TEXT("") },
		{ 425, TEXT("CAI_BaseNPC"), TEXT("0x1027cc10"), 0x88, TEXT("0x1027f8f0"),
			TEXT("vftable_CAI_Senses") },
		{ 426, TEXT("CAI_BaseNPC"), TEXT("0x1027cef0"), 0x14, TEXT(""),
			TEXT("vftable_CAI_MoveProbe") },
		{ 427, TEXT("CAI_BaseNPC"), TEXT("0x1027cec0"), 0x6c, TEXT("0x102e0900"), TEXT("") },
		{ 427, TEXT("CAI_BaseHumanoid"), TEXT("0x10260f40"), 0x70, TEXT("0x102e0900"),
			TEXT("vftable_CAI_HumanoidMotor") },
		{ 428, TEXT("CAI_BaseNPC"), TEXT("0x1027cf60"), 0x20, TEXT("0x102ddab0"), TEXT("") },
		{ 428, TEXT("CNPC_VRat"), TEXT("0x103ad6a0"), 0x20, TEXT("0x103ad540"), TEXT("") },
		{ 429, TEXT("CAI_BaseNPC"), TEXT("0x1027cf90"), 0x68, TEXT("0x102eca50"), TEXT("") },
		{ 429, TEXT("CAI_BaseHumanoid"), TEXT("0x10262430"), 0x6c, TEXT("0x102eca50"),
			TEXT("vftable_CAI_HumanoidNavigator") },
		{ 430, TEXT("CAI_BaseNPC"), TEXT("0x1027cfc0"), 0x18, TEXT(""),
			TEXT("vftable_CAI_Pathfinder") },
	};
	OutCount = UE_ARRAY_COUNT(Rows);
	return Rows;
}

const FElysiumNpc::FComponentFactory* FElysiumNpc::ComponentFactoryFor(int32 Slot) const
{
	// The census picks the row, not a name compare: `BodyOf` walks this NPC's base chain upward and
	// answers the nearest override's address, else the Troika line's own. A classname no census
	// class claims answers null, whose `BodyOf` is the Troika line — the correct fall-through.
	const TCHAR* const SlotBody = ElysiumNpcKernelClass::BodyOf(RetailClass(), Slot);
	int32 Count = 0;
	const FComponentFactory* const Rows = ComponentFactoryRows(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		if (Rows[Index].Slot == Slot && FCString::Stricmp(Rows[Index].Body, SlotBody) == 0)
		{
			return &Rows[Index];
		}
	}
	// The census had no body for the slot (or one this table does not carry): fall back to the
	// Troika-line row so a caller still gets the allocation it would have made.
	for (int32 Index = 0; Index < Count; ++Index)
	{
		if (Rows[Index].Slot == Slot && FCString::Stricmp(Rows[Index].RetailClass,
			TEXT("CAI_BaseNPC")) == 0)
		{
			return &Rows[Index];
		}
	}
	return nullptr;
}

void* FElysiumNpc::CreateSenses()
{
	// `0x1027cc10`, slot 425. `operator new(0x88)`, then by hand:
	//     [0] = vftable_CAI_Component;  [1] = 0;  [3] = -1;
	//     [4] = 0x45400000 (3072.0f);  [5] = [6] = 0xbf800000 (-1.0f);  [7] = 0;
	//     three chained sub-objects at +0x20, +0x34, +0x48, each thunk_FUN_1027f8f0(p, 0, 0),
	//       each storing the previous one's first word in its own +0x0c;
	//     [0x1a] = -1.0f, then thunk_FUN_1027cda0(+0x68, 0.15f, true),
	//       thunk_FUN_1027cd50(+0x70, 0.25f, true), thunk_FUN_1027cd50(+0x78, 0.45f, true);
	//     [0x20] = 1 (byte);  [0x21] = 0;  [0] = vftable_CAI_Senses;
	//     [0x17..0x19] = the three sub-objects;  [1] = this (the owner);
	//     return it;  (on an allocation failure it writes `this` to absolute address 4 and returns 0)
	//
	// The three constants are the sense-refresh intervals every `CAI_Senses` starts with: 0.15 s,
	// 0.25 s, 0.45 s. They are NOT lost by answering null — this runtime's sense cadence is family
	// **Senses**' and is driven by the NPC's own think, not by a component's three timers.
	//
	// SEAM: there is no `CAI_Senses` object. `+0x5cdc` is bound to `FElysiumNpc::Senses`, a struct
	// the NPC already carries, so there is nothing to allocate and nothing to point at.
	++ComponentFactoryRefusals;
	return nullptr;
}

void* FElysiumNpc::CreateMoveProbe()
{
	// `0x1027cef0`, slot 426. `operator new(0x14)`, then `p[1] = this` (the owner), `p[3] = -1` (the
	// same sentinel every component in this band seeds) and `p[0] = vftable_CAI_MoveProbe`. No
	// constructor call at all — the whole object is four stores.
	//
	// SEAM: `+0x5d40` is an `ELYSIUM_NPC_WORD_CHAIN` row on the motor; there is no move probe.
	++ComponentFactoryRefusals;
	return nullptr;
}

void* FElysiumNpc::CreateMotor()
{
	// `0x1027cec0`, slot 427: `operator new(0x6c)` then `thunk_FUN_102e0900(p, this)` — the base
	// `CAI_Motor`, whose own constructor installs its vftable.
	//
	// `CAI_BaseHumanoid::vfunc427` (`0x10260f40`) is the species override: `operator new(0x70)` —
	// FOUR bytes larger — the same `0x102e0900` base constructor, then `vftable_CAI_HumanoidMotor`
	// at `[0]` and `vftable_CAI_HumanoidMotor_at16` at `[4]` (the second base's adjustor thunk
	// table), and `p[0x1b] = -1`. So the humanoid motor is the base plus one word, and the retail
	// choice slot 427 exists to make is which of the two a class gets.
	//
	// SEAM: `+0x5d34` is an `ELYSIUM_NPC_WORD_CHAIN` row on `FElysiumScriptedCharacter::Motor`, the
	// one `IElysiumNpcMotor` seam, which the services provide rather than the NPC allocating.
	// `ComponentFactoryFor(427)` still says WHICH motor this class would have been given.
	++ComponentFactoryRefusals;
	return nullptr;
}

void* FElysiumNpc::CreateLocalNavigator()
{
	// `0x1027cf60`, slot 428: `operator new(0x20)` then `thunk_FUN_102ddab0(p, this)`.
	// `CNPC_VRat::vfunc428` (`0x103ad6a0`) is the species override and is the SAME SIZE — `0x20` —
	// with a different constructor (`0x103ad540`). A rat's local navigator is a different type of
	// the same shape, which is why the size alone does not identify the row.
	//
	// SEAM: `+0x5d38` is folded into the same motor seam.
	++ComponentFactoryRefusals;
	return nullptr;
}

void* FElysiumNpc::CreateNavigator()
{
	// `0x1027cf90`, slot 429: `operator new(0x68)` then `thunk_FUN_102eca50(p, this)`.
	// `CAI_BaseHumanoid::vfunc429` (`0x10262430`) allocates `0x6c` — four bytes larger, the same
	// relationship the motor pair has — calls the SAME `0x102eca50` constructor, then installs
	// `vftable_CAI_HumanoidNavigator` at `[0]`, its `_at16` adjustor table at `[4]`, and clears the
	// byte at `+0x68`.
	//
	// SEAM: `+0x5d34`'s chain row already says the navigator is the motor seam's concern here.
	++ComponentFactoryRefusals;
	return nullptr;
}

void* FElysiumNpc::CreatePathfinder()
{
	// `0x1027cfc0`, slot 430: `operator new(0x18)`, `p[1] = this`, `p[3] = -1`, `p[4] = p[5] = 0`,
	// `p[0] = vftable_CAI_Pathfinder`. Like the move probe, no constructor call.
	//
	// SEAM: `+0x5d3c` is an `ELYSIUM_NPC_WORD_CHAIN` row — the motor owns path generation.
	++ComponentFactoryRefusals;
	return nullptr;
}

bool FElysiumNpc::CreateComponents()
{
	// `0x1027cae0`, slot 424 — retail's fail-fast chain, in its own order, which is NOT the slot
	// order:
	//     m_pSenses  = vt[+0x6a4]();  if (!m_pSenses)  return false;   // slot 425
	//     m_pMotor   = vt[+0x6ac]();  if (!m_pMotor)   return false;   // slot 427
	//     +0x5d38    = vt[+0x6b0]();  if (!+0x5d38)    return false;   // slot 428 local navigator
	//     +0x5d40    = vt[+0x6a8]();  if (!+0x5d40)    return false;   // slot 426 move probe
	//     m_pNavigator  = vt[+0x6b4](); if (!it) return false;         // slot 429
	//     m_pPathfinder = vt[+0x6b8](); if (!it) return false;         // slot 430
	//     +0x5cf8 = this;                                              // the pathfinder's owner
	//     thunk_FUN_102e0ba0(m_pMotor, +0x5d38 ? +0x5d38 + 0x10 : 0);  // motor <- local navigator
	//     thunk_FUN_102ddc00(+0x5d38, m_pNavigator ? m_pNavigator + 0x10 : 0);
	//     m_pNavigator->vt[+0xc](DAT_1093407c);
	//     return true;
	//
	// Note the order: **the local navigator is built before the move probe** even though its slot
	// number is higher, and the two `+ 0x10` adjustments are second-base pointers, not offsets into
	// a field. Both null guards before the thunks are dead — the chain has already returned false on
	// either being null — and are reproduced as written.
	//
	// SEAM CONSEQUENCE, stated rather than papered over: all six factories answer null here, so this
	// answers FALSE at the first one (slot 425), which is retail's own answer when a factory fails.
	// Nothing in this runtime calls `CreateComponents` — spawn does not route through it — so the
	// false is observable only to a test. `FirstRefusedComponentSlot` records which factory refused
	// so the chain's ORDER is measurable even while every arm refuses.
	FirstRefusedComponentSlot = INDEX_NONE;
	const int32 ChainOrder[] = { 425, 427, 428, 426, 429, 430 };
	for (int32 Slot : ChainOrder)
	{
		void* Component = nullptr;
		switch (Slot)
		{
		case 425: Component = CreateSenses(); break;
		case 427: Component = CreateMotor(); break;
		case 428: Component = CreateLocalNavigator(); break;
		case 426: Component = CreateMoveProbe(); break;
		case 429: Component = CreateNavigator(); break;
		default:  Component = CreatePathfinder(); break;
		}
		if (Component == nullptr)
		{
			FirstRefusedComponentSlot = Slot;
			return false;
		}
	}
	// Unreachable while the six seams refuse. The tail retail runs here — the pathfinder-owner
	// write (`+0x5cf8`), the two cross-wires and the navigator's `vtable+0xc` call with
	// `DAT_1093407c` (the global node graph) — has no counterpart to write to and is recorded in
	// the comment above rather than stood as six more seams.
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 590 `OkToInterruptForMelee` — `0x1029f940`, plus `CNPC_VSabbatLeader`'s `0x103ab400`.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::OkToDisturb() const
{
	// `0x1028a190`, walked off the LISTING because the decompilation is damaged (`Could not recover
	// jumptable at 0x1028a21d`). The listing is linear and the whole body is:
	//
	//     1028a195  CALL [EAX+0x740]        ; slot 464 GetState()
	//     1028a19b  CMP EAX,0x4             ; NPC_STATE_SCRIPT
	//     1028a1a0  MOV EAX,[ESI+0x5d74]    ; m_hCine
	//     ... resolve it, and if it resolves:
	//     1028a1fa  CALL 0x1000c6da         ; -> 0x101a8930 CCineNPC::CanInterrupt, ON THE CINE
	//     1028a201  JZ  0x1028a223          ; false -> return false
	//     1028a203  MOV AL,[ESI+0x5bc4]     ; m_bInChoreoScene -> non-zero returns false
	//     1028a20d  MOV EAX,[ESI+0x14bc]
	//     1028a213  TEST AH,0x10            ; m_bfAINPCFlags2 & 0x1000 -> set returns false
	//     1028a21d  JMP [EDX+0x278]         ; tail-call slot 158 IsAlive()
	//
	// **`+0x14bc & 0x1000` is `MADE_OBLIVIOUS`, and this is a reader of it.** `ElysiumNpcFlags.h`
	// records that bit as having "ZERO readers anywhere in retail"; that is wrong, and this address
	// is the counter-example. The header is another story's file and is left alone; the correction
	// is recorded here and in `docs/vtmb/npc-ai/conditions-and-states.md`.
	//
	// The cine arm asks `CineCanInterrupt()` (family **EntityChain**'s port of `0x101a8930`) on
	// THIS NPC rather than on the resolved cine: this runtime stands one leaf and the cine entity
	// is not an `FElysiumNpc`. That body's own `m_interruptable` term is a seam answering false, so
	// the arm refuses either way — which is the recovered refusal and is stated here, not hidden.
	//
	// Retail name unrecovered.
	if (Mind.State() == EElysiumNpcState::Scripted && ScriptOwner.IsSet()
		&& World != nullptr && World->Resolve(ScriptOwner) != nullptr)
	{
		if (!CineCanInterrupt())
		{
			return false;
		}
	}
	if (bInChoreoScene)
	{
		return false;
	}
	if (NpcFlags.Has(GMiscObliviousBit))
	{
		return false;
	}
	return const_cast<FElysiumNpc*>(this)->IsAlive();
}

bool FElysiumNpc::OkToInterruptForMelee()
{
	// `CNPC_VSabbatLeader::OkToInterruptForMelee` (`0x103ab400`) first — the census says whether
	// this NPC takes it. The body, scope trace stripped:
	//     if (m_Activity (+0xfec) != 0x1141) return CAI_BaseNPCTroika::OkToInterruptForMelee();
	//     return true;
	// An exception, not a replacement: activity `0x1141` is always interruptible for the Sabbat
	// leader and everything else falls straight through to the Troika line.
	if (FCString::Stricmp(ElysiumNpcKernelClass::BodyOf(RetailClass(), 590),
		TEXT("0x103ab400")) == 0
		&& ActivityNumber == GMiscSabbatLeaderMeleeActivity)
	{
		return true;
	}

	// `CAI_BaseNPCTroika::OkToInterruptForMelee` `0x1029f940`:
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

	// `CNPC_VLasombra::vfunc592` (`0x103893c0`) first:
	//     if (curtime < m_flCoverDisableOverride (+0x6664)) return true;   // NOT false
	//     return CAI_BaseNPCTroika::CanSeekCover();
	//
	// **29c's walk has the polarity backwards** ("returns false while curtime is before
	// m_flCoverDisableOverride"). The body returns the FPU flag word for `curtime < override` on
	// that arm, i.e. TRUE — `CONCAT22(..., (curtime<ovr)<<8 | ...)` puts the comparison result in
	// AL. The field name reads as a disable, but the arm it guards is the permissive one: while the
	// override stands, a Lasombra may always seek cover without consulting the Troika rule.
	// Corrected here and in the walked paragraph.
	if (FCString::Stricmp(ElysiumNpcKernelClass::BodyOf(RetailClass(), 592),
		TEXT("0x103893c0")) == 0
		&& Now < static_cast<double>(LasombraCoverDisableOverride))
	{
		return true;
	}

	// `CAI_BaseNPCTroika::FUN_102953e0` `0x102953e0`, three arms in order:
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
// Slot 24 `OnVictimHitByMe` — `0x1029f8d0` and its three species overrides.
// -------------------------------------------------------------------------------------------------

const FElysiumNpc::FVictimHitSpecies* FElysiumNpc::VictimHitSpeciesRows(int32& OutCount)
{
	static constexpr FVictimHitSpecies Rows[] =
	{
		{ TEXT("CAI_BaseNPCTroika"), TEXT("0x1029f8d0"), EVictimHitLine::Troika },
		{ TEXT("CNPC_VGargoyle"), TEXT("0x1037a450"), EVictimHitLine::Gargoyle },
		{ TEXT("CNPC_VSabbatLeader"), TEXT("0x103ab4a0"), EVictimHitLine::SabbatLeader },
		{ TEXT("CNPC_VZombie"), TEXT("0x103e1280"), EVictimHitLine::Zombie },
		// Story 29d, family **SpeciesMisc10**: `CNPC_VGhoulCroucher#24` (`0x1037be80`). Without this
		// row a burning croucher fell to the bare Troika arm and never burned the player.
		{ TEXT("CNPC_VGhoulCroucher"), TEXT("0x1037be80"), EVictimHitLine::GhoulCroucher },
	};
	OutCount = UE_ARRAY_COUNT(Rows);
	return Rows;
}

FElysiumNpc::EVictimHitLine FElysiumNpc::VictimHitLine() const
{
	const TCHAR* const SlotBody = ElysiumNpcKernelClass::BodyOf(RetailClass(), 24);
	int32 Count = 0;
	const FVictimHitSpecies* const Rows = VictimHitSpeciesRows(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		if (FCString::Stricmp(Rows[Index].Body, SlotBody) == 0)
		{
			return Rows[Index].Line;
		}
	}
	return EVictimHitLine::Troika;
}

bool FElysiumNpc::GargoyleHitsPillar(const FString& Classname)
{
	// The two `FClassnameIs` compares of `0x1037a450`, which are `__strcmpi` (case-insensitive) and
	// accept a trailing `*` as a prefix match — neither `"pillar"` nor `"central_pillar"` carries
	// one, so both are whole-name compares.
	return Classname.Equals(TEXT("pillar"), ESearchCase::IgnoreCase)
		|| Classname.Equals(TEXT("central_pillar"), ESearchCase::IgnoreCase);
}

void FElysiumNpc::ClearMeleeMoveRecords()
{
	// SEAM for `thunk_FUN_1028b160(&this->field_0x6028)` (`0x1028b160`): five iterations of three
	// dword stores from `+0x6028`, i.e. `MeleeMoveRecord_t m_MeleeMoveRecords[5]` zeroed whole. The
	// offset is `ELYSIUM_NPC_WORD_ABSENT` in the shape map, so the clear is counted and no word is
	// written. This is the ENTIRE Troika-line body of slot 24.
	++MeleeMoveRecordClears;
}

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

void FElysiumNpc::OnVictimHitByMe(FElysiumEntity* Victim)
{
	switch (VictimHitLine())
	{
	case EVictimHitLine::Gargoyle:
	{
		// `CNPC_VGargoyle::OnVictimHitByMe` `0x1037a450`. The control flow inverts twice, so read
		// the listing's jump targets rather than the nesting: the two early `JZ 0x1037a547` on a
		// pointer-identity hit and the two `SETZ` tails all land on the DISPATCH, and the only path
		// that reaches `0x1037a552` (the return) is "neither name matched". So:
		//
		//     if (classname is "pillar" or "central_pillar") victim->vtable[+0x428](this);
		//     // and NOTHING otherwise — not even the base body.
		//
		// **29c's walk has this inverted** ("skips the base hit reaction when the victim's classname
		// is pillar ... otherwise calls it"). Corrected here and in the walked paragraph.
		//
		// Note what this override does NOT do: it never calls the Troika line, so a Gargoyle's melee
		// move records are never cleared. That is retail's, not an omission here.
		const FString Classname = Victim != nullptr && Victim->Def != nullptr
			? Victim->Def->Classname : FString();
		if (GargoyleHitsPillar(Classname))
		{
			DispatchVictimHitReaction(Victim);
		}
		return;
	}
	case EVictimHitLine::SabbatLeader:
	{
		// `CNPC_VSabbatLeader::OnVictimHitByMe` `0x103ab4a0`, scope trace stripped:
		//     ent = resolve(m_hClosestPlayer);                 // +0x628c, index & 0x1fff,
		//                                                      // generation >> 0xd
		//     if (ent == param_1 && --m_RoarAttackCount < 0) m_RoarAttackCount = 0;
		//
		// The decrement is INSIDE the condition's second term, so it happens only when the victim is
		// the tracked player; the clamp is a separate test on the decremented value. It also does
		// NOT call the Troika line — the base's record clear does not run for a Sabbat leader.
		const FElysiumEntity* Closest = World != nullptr && Senses.Memory.ClosestPlayer.IsSet()
			? World->Resolve(Senses.Memory.ClosestPlayer)
			: nullptr;
		if (Closest != nullptr && Closest == Victim)
		{
			--SabbatLeaderRoarAttackCount;
			if (SabbatLeaderRoarAttackCount < 0)
			{
				SabbatLeaderRoarAttackCount = 0;
			}
		}
		return;
	}
	case EVictimHitLine::Zombie:
		// `CNPC_VZombie::OnVictimHitByMe` `0x103e1280`, and the ORDER is the fact:
		//     CAI_BaseNPCTroika::OnVictimHitByMe(this, param_1);          // base FIRST
		//     FireOutput(&m_OnAttackedVictim (+0x66e8), param_1, this, 0);// output SECOND
		// The only species arm that keeps the base body.
		ClearMeleeMoveRecords();
		if (Victim != nullptr)
		{
			FireOutput(FName(TEXT("OnAttackedVictim")), Victim->Handle);
		}
		return;

	case EVictimHitLine::GhoulCroucher:
		// `CNPC_VGhoulCroucher::OnVictimHitByMe` `0x1037be80`, story 29d family SpeciesMisc10:
		//     player = param_1 ? param_1->+0xa8 : 0;                  // the PLAYER downcast cache
		//     if (m_bSpawnBurning (+0x6665) && player) BurnPlayer(player, 10.0);
		//     CAI_BaseNPCTroika::OnVictimHitByMe(this, param_1);      // ALWAYS, unlike the Gargoyle
		//                                                             // and SabbatLeader arms
		// `1037bf3c` pushes `0x41200000` = **10.0** as the burn damage.
		if (bGhoulSpawnBurning && Victim != nullptr && World != nullptr
			&& Victim->Handle == World->PlayerHandle())
		{
			BurnPlayer(Victim, 10.f);
		}
		ClearMeleeMoveRecords();
		return;

	case EVictimHitLine::Troika:
	default:
		// `CAI_BaseNPCTroika::OnVictimHitByMe` `0x1029f8d0` — fourteen bytes whose whole body is
		// `thunk_FUN_1028b160(&this->field_0x6028)`. The victim argument is READ BY NOTHING.
		ClearMeleeMoveRecords();
		return;
	}
}

// -------------------------------------------------------------------------------------------------
// Slot 473 `GetSoundInterests` — the `CGeneric_NPC` pair.
// -------------------------------------------------------------------------------------------------

const FElysiumNpc::FSoundInterestSpecies* FElysiumNpc::SoundInterestSpeciesRows(int32& OutCount)
{
	static constexpr FSoundInterestSpecies Rows[] =
	{
		{ TEXT("CGeneric_NPC"), TEXT("0x1035a7e0") },
		{ TEXT("CGenericSabbat_NPC"), TEXT("0x1035be50") },
	};
	OutCount = UE_ARRAY_COUNT(Rows);
	return Rows;
}

int32 FElysiumNpc::GenericNpcSoundInterests() const
{
	// `0x1035a7e0` and `0x1035be50` are byte-identical 20-byte bodies:
	//     return (m_NPCState (+0x5cc0) == 1) ? 0x19 : 0x17;
	//
	// `1` is retail's `NPC_STATE_IDLE`. This runtime's `EElysiumNpcState` starts at `Idle = 0` and
	// carries no `NPC_STATE_NONE`, so the retail number 1 is `EElysiumNpcState::Idle` here — the
	// same mapping family **Squad** used at `0x102bf5d0`.
	//
	// The two answers are sound-interest MASKS, not schedule ids: `0x19` is `0x17 | 0x8`, so an idle
	// generic NPC listens to one extra sound class. The Troika line's own answer is `0x81f`.
	return Mind.State() == EElysiumNpcState::Idle ? GMiscSoundInterestIdle : GMiscSoundInterestOther;
}

// -------------------------------------------------------------------------------------------------
// The three per-species threshold answers.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::BullseyeIsLightDamage(float Damage, int32 DamageBits)
{
	// `CNPC_Bullseye::vfunc576` `0x10356f30`: `return _DAT_104454c4 < param_1`, i.e. `damage > 0.0f`.
	// That is BYTE-IDENTICAL to the Troika line's own slot-576 body (`0x10266630`), which family
	// **Damage** landed as `IsLightDamage`, so this asks the slot instead of restating the compare.
	// It exists as its own method because it is its own census row (`CNPC_Bullseye#576`) and the
	// story's acceptance is per row.
	return IsLightDamage(Damage, DamageBits);
}

int32 FElysiumNpc::BachRangeAttack1Conditions(float Dot, float DistUnits) const
{
	// `CNPC_VBach::vfunc553` `0x10364500`, slot 553 `RangeAttack1Conditions(flDot, flDist)`:
	//     if (flDot >= (float)_DAT_10449270) return 0x4f;          // 0.5, a DOUBLE in .rdata
	//     if (flDist <= _DAT_1044f00c) return 0x4f;                // 120.0f
	//     return 0x61;
	//
	// **It is an OR, not an AND**, and that is the shipped oddity: Bach can range-attack whenever he
	// is roughly facing the target OR the target is inside 120 units, where the ordinary shape would
	// require both. `0x4f` is `COND_CAN_RANGE_ATTACK1` and `0x61` is `COND_NOT_FACING_ATTACK`; note
	// that a target that is merely too far answers "not facing", which is the wrong refusal and is
	// retail's.
	//
	// The listing settles both polarities: `FCOMP double [0x10449270]` with `TEST AH,0x5 / JP` is
	// `>=` (equal takes the accept), and `AND EAX,0x4100 / JZ` on the second is `>` (equal takes the
	// accept there too).
	return (static_cast<double>(Dot) >= GMiscHalfDouble || DistUnits <= GMiscYawHigh)
		? GMiscBachRange1Answer : GMiscBachRefusal;
}

int32 FElysiumNpc::BachRangeAttack2Conditions(float Dot, float DistUnits) const
{
	// `CNPC_VBach::vfunc554` `0x10364550`, slot 554 `RangeAttack2Conditions(flDot, flDist)`. The
	// SAME two thresholds in the same order; only the accepted answer differs — `0x50`
	// (`COND_CAN_RANGE_ATTACK2`) instead of `0x4f`. One behaviour written twice, which is why the
	// gate is not restated.
	return BachRangeAttack1Conditions(Dot, DistUnits) == GMiscBachRange1Answer
		? GMiscBachRange2Answer : GMiscBachRefusal;
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

bool FElysiumNpc::FormBitTimerExpired() const
{
	// `0x10381ca0` — the whole body is `return m_flFishTimer (+0x666c) <= gpGlobals->curtime`. The
	// read half of the pair above; `0x10381c00` is the only writer of `+0x666c` this story found.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	return static_cast<double>(HengeyokaiFishTimer) <= Now;
}

// -------------------------------------------------------------------------------------------------
// `CNPC_VYukie`'s melee pair — slots 599 and 601.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::YukieEnterMelee()
{
	// `CNPC_VYukie::vfunc599` `0x103dd8b0`, the whole body:
	//     (*DAT_10924edc)->vfunc1();                                    // the global melee event
	//     m_bInMelee = 1;                                               // +0x6078
	//     m_flMeleeMustLeaveTimer = RandomFloat(22.5f, 45.0f) + curtime; // +0x6074
	//     return true;
	//
	// Against family **TroikaHelpers**' `Slot599` (the Troika line, `0x102b5650`) this is the whole
	// species difference: **Yukie has no gates at all**. No frenzy bit, no follower boss, no
	// can-enter timer, no range term, no height term, no attack coordinator — she always enters
	// melee, and her must-leave window (22.5–45 s) is three times the Troika line's (7.5–15 s).
	//
	// `(*DAT_10924edc)->vfunc1()` is the same global event object families Bosses and TroikaHelpers
	// already count through `MeleeEventFires`; the same counter is incremented rather than a second
	// one stood beside it.
	++MeleeEventFires;
	bInMelee = true;
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	MeleeMustLeaveTimer = Now + static_cast<double>(
		ElysiumRng::Stream(EElysiumRngStream::NpcSchedule)
			.FRandRange(GMiscYukieMustLeaveMin, GMiscYukieMustLeaveMax));
	return true;
}

void FElysiumNpc::YukieLeaveMelee()
{
	// `0x103dd9a0`, `CNPC_VYukie#601`, the whole body:
	//     (*DAT_10924edc)->vfunc1();                                     // the global melee event
	//     m_bInMelee = 0;                                                // +0x6078
	//     if (HasUsableRangedWeapon())                                   // slot 308 (+0x4d0)
	//         m_flMeleeCanEnterTimer = RandomFloat(5.0f, 10.0f) + curtime;  // +0x6070
	//
	// The Troika line's `0x102b5880` with its LAST line dropped: there is no attack-coordinator
	// release. Everything before it — the event, the clear, the gated re-arm and both draw bounds —
	// is identical. So Yukie enters melee unconditionally and, on the way out, never gives a
	// coordinator slot back, because she never took one.
	//
	// Slot 308 `HasUsableRangedWeapon` (`0x10336d70`) is still a generated stub answering false, so
	// the timer arm is not reached today; it is wired, not inlined.
	++MeleeEventFires;
	bInMelee = false;
	if (HasUsableRangedWeapon())
	{
		const double Now = World != nullptr ? World->NowSeconds() : 0.0;
		MeleeCanEnterTimer = Now + static_cast<double>(
			ElysiumRng::Stream(EElysiumRngStream::NpcSchedule)
				.FRandRange(GMiscYukieCanEnterMin, GMiscYukieCanEnterMax));
	}
}

// -------------------------------------------------------------------------------------------------
// The three remaining species bodies.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::SlowedExpire() const
{
	// `0x1038f290` — the whole body is `return (float)_DAT_1044fab0 < m_flSlowedExpire (+0x6684)`.
	//
	// `_DAT_1044fab0` is the shared `0.0` DOUBLE (`docs/vtmb/footsteps.md` § the 2-D speed gate), so
	// this is a compare against ZERO and NOT against curtime: `m_flSlowedExpire` is read here as a
	// FLAG spelled as a float, not as a deadline. A body that read it as a deadline would answer the
	// opposite for every ManBat whose slow has not been armed.
	return static_cast<double>(ManBatSlowedExpire) > GMiscZeroDouble;
}

void FElysiumNpc::StoreArenaCenter()
{
	// `CNPC_VChangBros::StoreArenaCenter` `0x1036e400` (name recovered by `NameFromStrings` at
	// `0x106313a0`), scope trace stripped:
	//     node = DAT_10925450;                                   // the global CAI_Hint list head
	//     if (!node) return;
	//     while (node->m_nHintType (+0x5dc) != 0x4651) {
	//         node = node->next (+0x5d8);
	//         if (!node) return;                                 // no write at all
	//     }
	//     pos = node->vtable[+0x364]();                          // GetAbsOrigin
	//     m_vArenaCenter = pos;                                  // +0x66dc, three floats
	//     m_bCenterStored = 1;                                   // +0x66e8
	//
	// Both writes are inside the found arm: a map with no `0x4651` hint leaves `m_bCenterStored`
	// false and the centre at whatever it was, which is what the Chang fight's own guard reads.
	//
	// The walk is exactly family **Squad**'s `NthHintOfType(type, 0)` seam — the global hint list,
	// the same next link and the same type word — so that accessor is asked rather than a second
	// walk stood beside it. It answers null (no hint store carries hint types here), so this takes
	// retail's "fell off the end" arm and writes nothing.
	const FElysiumEntity* Node = NthHintOfType(GMiscArenaCenterHintType, 0);
	if (Node == nullptr)
	{
		return;
	}
	ChangArenaCenter = Node->Origin;
	bChangCenterStored = true;
}

void FElysiumNpc::FireWerewolfZoneTrigger(FElysiumEntity& Zone)
{
	// SEAM for `zone->vtable[+0x3ec]()` — slot 251. On the `CAI_BaseNPC` line slot 251 is
	// `IsActivityFinished` (`0x10272900`), but a `trigger_werewolf_zone` is a different hierarchy
	// sharing the index and the census carries no table for it, so what this fires is
	// **unrecovered**. Counted; nothing is dispatched.
	(void)Zone;
	++WerewolfZoneTriggerFires;
}

void FElysiumNpc::TriggerWerewolfZone()
{
	// `0x103cade0`, in order:
	//     for (e = FindEntityByName(NULL, "trigger_werewolf_zone"); e;
	//          e = FindEntityByName(e, "trigger_werewolf_zone"))
	//         e->vtable[+0x3ec]();
	//     d1 = dynamic_cast<CFuncMoveLinear*>(FindEntityByName(NULL, "rotdoor1", this, this));
	//     m_hRotDoor1 = d1 ? d1->GetRefEHandle() : INVALID_EHANDLE;    // +0x6684
	//     d2 = dynamic_cast<CFuncMoveLinear*>(FindEntityByName(NULL, "rotdoor2", this, this));
	//     m_hRotDoor2 = d2 ? d2->GetRefEHandle() : INVALID_EHANDLE;    // +0x6688
	//
	// `0x100f7380` compares `entity+0x11c` (`m_iName`), not `+0x26c` (`m_iClassname`), so the zone
	// sweep is by TARGETNAME — three hardcoded map names, which is why this body has no keyfield and
	// no parameter. The two door lookups go through the five-argument form (`0x100f7770`, the one
	// that understands `!self` / `!activator`), passing `this` as both the searching entity and the
	// activator.
	//
	// The RTTI cast is load-bearing: an entity named `rotdoor1` that is NOT a `CFuncMoveLinear`
	// stores the INVALID handle rather than itself, so a mis-typed map disarms the door instead of
	// crashing later. Reproduced as a name lookup plus a refusal, with the class test itself
	// **unrecovered** — this runtime has no `CFuncMoveLinear` leaf to test against, so any entity
	// carrying the name is accepted and the comment says so.
	//
	// Retail name unrecovered; named from the entity names it carries.
	if (World == nullptr)
	{
		return;
	}
	World->ForEachNamed(GMiscWerewolfZoneName, [this](FElysiumEntity& Zone)
	{
		FireWerewolfZoneTrigger(Zone);
	});
	const FElysiumEntity* Door1 = World->FindByName(GMiscRotDoor1Name);
	WerewolfRotDoor1 = Door1 != nullptr ? Door1->Handle : FElysiumEntityHandle::Invalid();
	const FElysiumEntity* Door2 = World->FindByName(GMiscRotDoor2Name);
	WerewolfRotDoor2 = Door2 != nullptr ? Door2->Handle : FElysiumEntityHandle::Invalid();
}
