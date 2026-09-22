#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSchedule.h"
#include "Player/ElysiumCameraShots.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"

// Story 29d, families **Anim10** and **SpeciesAnim10**, part two: `CAI_BaseHumanoid::
// MaintainEyeDirection` (`0x1025fa50`), the three melee selectors of slot 604 and the zombie idle
// gate of slot 509. `ElysiumNpcKernelAnim10.cpp` carries activity, sequence, pose and model.
//
// **`0x1025fa50` was read off the LISTING, not off the decompiled C.** The decompiler aliases six
// stack floats across the body's three passes — `fStack_78` is both the normalised time `t` and a
// head-vector component, `fStack_88` is both a ConVar value and the loop counter — and loses which
// vector is which in two places. Every arm below cites the instruction it came from.

namespace
{
	// Unit-prefixed: adaptive unity merges anonymous namespaces.

	// --- `0x1025fa50`'s `.rdata`, all read out of the pinned image ---------------------------------
	constexpr float GAnim10_2Zero = 0.0f;          // _DAT_104454c4
	constexpr float GAnim10_2One = 1.0f;           // _DAT_104454c0
	constexpr double GAnim10_2OneDouble = 1.0;     // _DAT_10449280, a DOUBLE
	constexpr float GAnim10_2Three = 3.0f;         // _DAT_10449258 — the cubic's 3
	constexpr float GAnim10_2LookScale = 100.0f;   // _DAT_10450564 — how far ahead the look point is
	constexpr float GAnim10_2HeadDecay = 0.8f;     // _DAT_1047049c — what the stored head keeps
	constexpr float GAnim10_2HeadBlend = 0.2f;     // _DAT_10451ab4 — ... and what the current adds
	constexpr float GAnim10_2ForwardScale = 128.0f;// _DAT_1046dcd0 — the fallback look distance
	// `_DAT_10497ca0` is a **DOUBLE** (`FCOMP double ptr [0x10497ca0]` at `1025fda5`) and reads
	// **-0.5**, recovered 2026-09-14 out of the pinned image's `.rdata`. Story 29c-1 carried it as
	// UNRECOVERED with a 0.0 stand-in; -0.5 admits a look target up to 120 degrees off the head
	// direction, which is what makes the queue usable at all.
	constexpr double GAnim10_2LookDotFloor = -0.5;

	// The blink re-arm, `RandomFloat(1.5, 4.5)` (`10260215 PUSH 0x3fc00000` / `10260210 PUSH
	// 0x40900000`), and `PickLookTarget(false, 1.5, 2.5)` (`1025fbff` / `1025fc04`).
	constexpr float GAnim10_2BlinkMin = 1.5f;
	constexpr float GAnim10_2BlinkMax = 4.5f;
	constexpr float GAnim10_2PickMin = 1.5f;
	constexpr float GAnim10_2PickMax = 2.5f;
	// The cycler-actor look importance, `PUSH 0x3f000000` at `1025fbe3`.
	constexpr float GAnim10_2CyclerImportance = 0.5f;
	// The fallback jitter, `RandomFloat(-16, 16)` on the right axis and `(-32, 32)` on the up one.
	constexpr float GAnim10_2JitterRight = 16.0f;
	constexpr float GAnim10_2JitterUp = 32.0f;

	constexpr TCHAR GAnim10_2CyclerActor[] = TEXT("cycler_actor");   // 0x105c8ee0
	constexpr TCHAR GAnim10_2CvLookMin[] = TEXT("DAT_1090fc0c");
	constexpr TCHAR GAnim10_2CvLookMax[] = TEXT("DAT_1090fc9c");
	// `m_NPCState == 4` (`NPC_STATE_SCRIPT`), the state whose arm is cycler-only.
	constexpr int32 GAnim10_2StateScript = 4;

	// --- The melee selectors' `.rdata` and schedule numbers ----------------------------------------
	//
	// `DAT_10924a1c` is the melee-range ConVar every selector thresholds on and story 29c-1 recorded
	// its name and default as UNRECOVERED; family Schedule answers 0.0, which is the arm a SET bool
	// takes. `MeleeRangeUnits()` (family TroikaHelpers) reads the same global and is called here so
	// the six bodies cannot drift.
	constexpr float GAnim10_2FarMargin = 200.0f;       // _DAT_104492b8
	constexpr float GAnim10_2HeightBand = 64.0f;       // _DAT_10451acc
	constexpr double GAnim10_2TimerUnarmed = -1.0;     // 0xbf800000
	constexpr float GAnim10_2RetryMin = 3.0f;          // RandomFloat(3.0, 4.0)
	constexpr float GAnim10_2RetryMax = 4.0f;
	constexpr int32 GAnim10_2RollFloor = 0x18;         // `CMP EAX,0x19; JL` — the roll must EXCEED 24

	// The two source-file strings the selector trace stamps into `+0x1b30`.
	constexpr TCHAR GAnim10_2FileHuman[] = TEXT("NPC_VHuman.cpp");        // 0x1063f724
	constexpr TCHAR GAnim10_2FileMingXiao[] = TEXT("NPC_VMingXiao.cpp");  // 0x10647090
	constexpr TCHAR GAnim10_2FileBach[] = TEXT("NPC_VBach.cpp");          // 0x1062eadc

	// Bach's two authored weapon classnames.
	constexpr TCHAR GAnim10_2BachRifle[] = TEXT("item_w_rem_m_700_bach");  // 0x105c1c70
	constexpr TCHAR GAnim10_2BachKatana[] = TEXT("item_w_katana");         // 0x10587668
	constexpr float GAnim10_2BachFailDelay = 15.0f;   // _DAT_10463584

	// `CNPC_VZombie::vfunc509`'s two weights and the schedule id that swaps them.
	constexpr int32 GAnim10_2ZombieIdleWeight = 999;
	constexpr int32 GAnim10_2ZombieComfortWeight = 0x14;
	constexpr int32 GAnim10_2ScheduleComfort = 0x12f;   // SCHED_TROIKA_COMFORT

	FRandomStream& Anim10_2Rng()
	{
		return ElysiumRng::Stream(EElysiumRngStream::NpcSchedule);
	}

	int32 Anim10_2RetailNpcState(EElysiumNpcState State)
	{
		switch (State)
		{
		case EElysiumNpcState::Idle:     return 1;
		case EElysiumNpcState::Combat:   return 2;
		case EElysiumNpcState::Alert:    return 3;
		case EElysiumNpcState::Scripted: return 4;
		case EElysiumNpcState::Prone:    return 6;
		case EElysiumNpcState::Dead:     return 7;
		default:                         return 0;
		}
	}

	// The height-difference retry stamp all six slot-604 bodies run, byte for byte:
	//
	//   armed = false;
	//   if (m_flEnemyHeightDiff <= 64.0)              m_flMeleeHeightDiffTimer = -1.0f;
	//   else if (m_flMeleeHeightDiffTimer == -1.0f)   m_flMeleeHeightDiffTimer = curtime +
	//                                                     RandomFloat(3.0, 4.0);
	//   else if (m_flMeleeHeightDiffTimer <= curtime) armed = true;
	//
	// Family Schedule spells the identical helper for the five bodies it ported; it is a file-static
	// there, so the six lines are repeated here with the same citation rather than a second reading
	// being made. The `<=` on the first arm and on the expiry are both read at `10386117`
	// (`AND EAX,0x4100; JNZ`) and `10386161` (`AND EAX,0x100; JNZ`).
	bool Anim10_2TickHeightDiffTimer(FElysiumNpc& Npc, double Now)
	{
		if (Npc.ScheduleHost.EnemyHeightDiffUnits <= GAnim10_2HeightBand)
		{
			Npc.MeleeHeightDiffTimer = GAnim10_2TimerUnarmed;
			return false;
		}
		if (Npc.MeleeHeightDiffTimer == GAnim10_2TimerUnarmed)
		{
			Npc.MeleeHeightDiffTimer =
				Now + Anim10_2Rng().FRandRange(GAnim10_2RetryMin, GAnim10_2RetryMax);
			return false;
		}
		return Npc.MeleeHeightDiffTimer <= Now;
	}

	// `(**(code **)(*(int *)this + 0x4d0))()` — slot 308 `HasUsableRangedWeapon`, the split every
	// arm of every melee selector turns on. The generated slot is a stub answering false; family
	// Schedule reads the port's own catalogue answer instead and names slot 308 beside it, and so
	// does this.
	bool Anim10_2HasRangedWeapon(const FElysiumNpc& Npc)
	{
		return ElysiumNpcCond::WeaponCapability(Npc) == ElysiumNpcCond::ECapability::Ranged;
	}
}

// =================================================================================================
// `CAI_BaseHumanoid::MaintainEyeDirection` `0x1025fa50`, `CAI_BaseHumanoid#333`.
// =================================================================================================

void FElysiumNpc::ClearHeadPoseParameters()
{
	// `FUN_1025efc0`, 5 instructions of substance:
	//
	//     SetPoseParameter(this[0x17f8], 0, 0);   // +0x5fe0 head_yaw
	//     SetPoseParameter(this[0x17f9], 0, 0);   // +0x5fe4 head_pitch
	//     SetPoseParameter(this[0x17fa], 0, 0);   // +0x5fe8 head_roll
	//     CBaseAnimating::FlushBoneCache();
	//     this[0x17d3] &= 0xfffffffc;             // +0x5f4c, the two cached-direction bits
	//
	// The three indices are `HumanoidPoseParams[10..12]`, which is what ties this body to
	// `0x1025e510`'s 26-word cache. SEAM: the pose write and the bone-cache flush have no callee in
	// this substrate (family Anim recorded both); the two cache bits ARE a port member (family
	// Facing's `HumanoidHeadCacheBits`) and are cleared, which is the half a later read observes.
	HumanoidHeadCacheBits &= ~3u;
}

void FElysiumNpc::BaseHumanoidMaintainEyeDirection(float Interval)
{
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;

	// --- Pass 1: re-resolve the named expression scene (1025fa5c) ---------------------------------
	//
	//     if (m_iszExpressionScene) {
	//       if (!CBaseEntity::Instance(m_hExpressionSceneEnt))
	//         PlayScene(m_iszExpressionScene ? : "", &m_hExpressionSceneEnt);
	//     }
	//
	// Note the empty-string fallback: a null `string_t` reads as `DAT_106b8540`, so retail plays the
	// EMPTY scene rather than skipping. SEAM: family Anim's `PlayInstancedScene` answers -1, the
	// "unknown scene" length, so the handle at `+0x5fa0` stays dead and this arm re-fires every pass
	// — which is retail's own behaviour for a scene the cache does not hold.
	if (!ExpressionScene.IsEmpty())
	{
		const FElysiumEntity* SceneEnt =
			(World != nullptr && ExpressionSceneEnt.IsSet()) ? World->Resolve(ExpressionSceneEnt)
															 : nullptr;
		if (SceneEnt == nullptr)
		{
			(void)PlayInstancedScene(*ExpressionScene);
		}
	}

	// --- Pass 2: the three unconditional reads (1025fa98) ------------------------------------------
	ProcessSceneEvents();                      // slot 283, vtable +0x46c
	const FVector Eye = EyePosition();         // slot 193, vtable +0x304
	ClearHeadPoseParameters();                 // 0x1025efc0
	// slot 371 `HeadDirection3D` — the running head vector every later pass blends into.
	FVector Head = HeadDirection3DHumanoid();

	// --- Pass 3: prune the look queue (1025facf) ---------------------------------------------------
	//
	// Retail walks the 0x24-byte array at `+0x5f88` with the count at `+0x5f94`, dropping any entry
	// whose END STAMP (`entry + 0x18`) is in the past, or whose kind (`entry + 0x00`) is 0 with a
	// dead handle at `entry + 0x04`. The surviving entries are compacted through `0x10260820` (a
	// `memmove` of the tail) and the count decremented. `TArray::RemoveAt` is that memmove.
	//
	// The walk does NOT advance its index on a removal — `1025fb17 JMP` skips the `INC EBX` — which
	// is why two adjacent dead entries are both caught.
	for (int32 Index = 0; Index < LookTargets.Num(); )
	{
		const FLookTargetRecord& Entry = LookTargets[Index];
		const bool bExpired = static_cast<float>(Entry.EndTime) < static_cast<float>(Now);
		const bool bDeadEntity = Entry.Kind == 0
			&& (World == nullptr || World->Resolve(Entry.Target) == nullptr);
		if (bExpired || bDeadEntity)
		{
			LookTargets.RemoveAt(Index);
			continue;
		}
		++Index;
	}

	// --- Pass 4: acquire a look target when the queue is empty (1025fb25) --------------------------
	//
	// **CORRECTION to the checklist's walk**, which read `+0x928` as "is it in a scene". Slot 586 on
	// `CAI_BaseHumanoid` is `0x1025f1a0`, whose whole body is `return m_LookTargets.Count() != 0` —
	// family BaseHelpers' `HasActiveLookTargets`. So the gate is "I have NOTHING to look at", which
	// is what makes the block an acquisition.
	//
	// The classname compare happens FIRST and unconditionally (`1025fb34`), before the gate, because
	// its `SETZ BL` result is used by both arms below.
	const bool bCyclerActor = Def != nullptr
		&& Def->Classname.Equals(GAnim10_2CyclerActor, ESearchCase::IgnoreCase);
	if (!HasActiveLookTargets())
	{
		const int32 State = Anim10_2RetailNpcState(Mind.State());   // slot 464, vtable +0x740
		if (State == GAnim10_2StateScript)
		{
			// A SCRIPTED body takes the cycler arm only if it IS a cycler_actor, and otherwise does
			// NOTHING AT ALL — no `PickLookTarget`. That asymmetry is the arm's whole point: a body
			// under a scripted sequence must not acquire its own gaze.
			if (bCyclerActor)
			{
				MaintainEyeDirectionCyclerArm();
			}
		}
		else if (bCyclerActor)
		{
			MaintainEyeDirectionCyclerArm();
		}
		else
		{
			// `PickLookTarget(false, 1.5, 2.5)` — the SDK defaults, pushed as `0x3fc00000` and
			// `0x40200000`. Retail's `0x1025f1c0` ADDS the pick to the queue itself through slots
			// 535/536; family Lifecycle's port splits it into "choose" and leaves the add to the
			// caller, so the add happens here. Same record, same order; stated so the split is not
			// mistaken for an extra event.
			const FLookTargetPick Pick =
				PickLookTarget(/*bExcludePlayers*/ false, GAnim10_2PickMin, GAnim10_2PickMax);
			if (Pick.Arm == FLookTargetPick::EArm::Enemy && World != nullptr)
			{
				FElysiumEntity* Picked = World->Resolve(Pick.Target);
				if (Picked != nullptr)
				{
					AddLookTargetHumanoid(Picked, 0, Pick.MaxDuration, Pick.Importance);
				}
			}
			else if (Pick.Arm == FLookTargetPick::EArm::NavigationGoal)
			{
				AddLookTargetHumanoid(MoveGoal, 0, Pick.MaxDuration, Pick.Importance);
			}
		}
	}

	// --- Pass 5: accumulate the surviving entries into the head vector (1025fc13) -------------------
	bool bAnyAccepted = false;
	for (int32 Index = 0; Index < LookTargets.Num(); ++Index)
	{
		FLookTargetRecord& Entry = LookTargets[Index];
		// `if (this == Instance(entry.handle)) continue;` — an entry naming ME is skipped here and
		// picked up by the view-target walk below instead.
		const FElysiumEntity* Owner =
			(World != nullptr) ? World->Resolve(Entry.Target) : nullptr;
		if (Owner == this)
		{
			continue;
		}

		// `t = (curtime - entry[+0x14]) / (entry[+0x18] - entry[+0x14])` — the entry's normalised
		// age. Retail divides unguarded, so a zero-length entry yields an infinity and is then
		// rejected by the `[0, 1]` window below; reproduced.
		const float Span = static_cast<float>(Entry.EndTime) - static_cast<float>(Entry.StartTime);
		const float T = (static_cast<float>(Now) - static_cast<float>(Entry.StartTime)) / Span;

		float Weight = 0.f;
		// `1025fc71` / `1025fc82`: zero-weighted strictly BELOW 0 and strictly ABOVE 1; both bounds
		// are INCLUSIVE (the `TEST AH,0x5 / JNP` pair lets equality through on the low side and the
		// `AND EAX,0x4100 / JZ` pair on the high side).
		if (!(T < GAnim10_2Zero) && !(GAnim10_2One < T))
		{
			// The ramp, `entry + 0x1c`. Three arms, in the listing's order (`1025fc95`):
			//   t <  ramp        -> f = t / ramp
			//   1 - ramp < t     -> f = (1.0 - t) / ramp     (the 1.0 is the DOUBLE at 0x10449280)
			//   otherwise        -> f = 1.0
			const float Ramp = Entry.Rate;
			float F;
			if (T < Ramp)
			{
				F = T / Ramp;
			}
			else if (GAnim10_2One - Ramp < T)
			{
				F = static_cast<float>(GAnim10_2OneDouble - static_cast<double>(T)) / Ramp;
			}
			else
			{
				F = GAnim10_2One;
			}
			// The cubic smoothstep, `1025fcd1`..`1025fce7`: `(3f² - 2f³) * entry[+0x20]`.
			Weight = (GAnim10_2Three * F * F - (F * F * F + F * F * F))
				* static_cast<float>(Entry.Priority);
		}

		// `1025fcfa`: an ENTITY-backed entry refreshes its cached position from the entity's own
		// `EyePosition` (slot 193) before it is used. A position entry (kind 1) keeps what it stored.
		if (Entry.Kind == 0 && Owner != nullptr)
		{
			Entry.Position = const_cast<FElysiumEntity*>(Owner)->EyePosition();
		}

		// `1025fd36`: offset = entry position - MY eye, normalised in place.
		FVector Offset = Entry.Position - Eye;
		Offset.Normalize();

		// `1025fd79`: dot against slot 371 `HeadDirection3D`, re-dispatched per entry. The floor is
		// the DOUBLE at `0x10497ca0`, **-0.5**, and the test at `1025fdad` (`AND EAX,0x100; JNZ
		// skip`) accepts on `dot >= floor`.
		const FVector Aim = HeadDirection3DHumanoid();
		if (FVector::DotProduct(Offset, Aim) < GAnim10_2LookDotFloor)
		{
			continue;
		}

		// `1025fdb4`: the accepted entry blends into the running head vector. NOT re-normalised
		// inside the loop — retail normalises only on the decay arm below.
		const float Inv = GAnim10_2One - Weight;
		Head = FVector(Head.X * Inv + Offset.X * Weight,
			Head.Y * Inv + Offset.Y * Weight,
			Head.Z * Inv + Offset.Z * Weight);
		bAnyAccepted = true;
	}

	// --- Pass 6: commit the head vector (1025fe49) --------------------------------------------------
	//
	// The two arms differ in their ORDER, and that is observable: the accepted arm calls slot 537
	// `SetHeadDirection` BEFORE writing `+0x5f74`, and the decay arm writes `+0x5f74` FIRST and then
	// calls it. Read at `1025febe`/`1025fec4` and `1025ff53`/`1025ffc8`.
	if (bAnyAccepted)
	{
		FVector Point = Eye + Head * GAnim10_2LookScale;
		SetHeadDirectionHumanoid(Point, Interval);   // slot 537, vtable +0x864
		HumanoidHeadVector = Head;
	}
	else
	{
		// With NOTHING accepted — including an empty queue — the stored vector DECAYS by 0.8 and
		// takes 0.2 of the current head instead, and is then NORMALISED in place (`1025ff5b CALL
		// [0x1057966c]`, `VectorNormalize`).
		HumanoidHeadVector = FVector(
			HumanoidHeadVector.X * GAnim10_2HeadDecay + Head.X * GAnim10_2HeadBlend,
			HumanoidHeadVector.Y * GAnim10_2HeadDecay + Head.Y * GAnim10_2HeadBlend,
			HumanoidHeadVector.Z * GAnim10_2HeadDecay + Head.Z * GAnim10_2HeadBlend);
		HumanoidHeadVector.Normalize();
		FVector Point = Eye + HumanoidHeadVector * GAnim10_2LookScale;
		SetHeadDirectionHumanoid(Point, Interval);
	}

	// --- Pass 7: the view target, walked BACKWARDS (1025ffce) ---------------------------------------
	//
	// From `count - 1` down to 0. The FIRST entry that names ME takes the head point; otherwise an
	// entry whose position passes slot 587 (`0x1025e920`, family Senses' `HumanoidValidEyeTarget` —
	// the 3-D 0.5 cone) claims the view target and the walk STOPS.
	bool bViewTargetSet = false;
	for (int32 Index = LookTargets.Num() - 1; Index >= 0; --Index)
	{
		FLookTargetRecord& Entry = LookTargets[Index];
		const FElysiumEntity* Owner = (World != nullptr) ? World->Resolve(Entry.Target) : nullptr;
		if (Owner == this)
		{
			// `1026023b`: the view target is the HEAD point. Note it reads `[ESP+0x38..0x40]`, the
			// stack copy of `HeadDirection3D`, which on the DECAY arm is still the ORIGINAL head
			// direction and not the decayed one. Reproduced by using `Head`.
			SetViewtarget(Eye + Head * GAnim10_2LookScale);   // slot 277, vtable +0x454
			bViewTargetSet = true;
			break;
		}
		// The entity-backed refresh happens again on THIS pass (`10260008`), before the test.
		if (Entry.Kind == 0 && Owner != nullptr)
		{
			Entry.Position = const_cast<FElysiumEntity*>(Owner)->EyePosition();
		}
		if (HumanoidValidEyeTarget(Entry.Position))   // slot 587, vtable +0x92c
		{
			// `1026029c`: retail refreshes the position a THIRD time before handing it over. The
			// duplicate is retail's, not a port artefact.
			if (Entry.Kind == 0 && Owner != nullptr)
			{
				Entry.Position = const_cast<FElysiumEntity*>(Owner)->EyePosition();
			}
			SetViewtarget(Entry.Position);
			bViewTargetSet = true;
			break;
		}
	}

	if (!bViewTargetSet)
	{
		// `10260071`: the walk found nobody. Retail asks slot 587 about the CURRENT view target
		// (slot 278 `GetViewtarget`) and, only when that too refuses, builds a randomised point.
		//
		// **CORRECTION to the checklist's walk**, which called this "slot +0x458 plus a
		// velocity-projected point". The listing (`1026008c`..`102601db`) is
		// `VectorVectors(HeadDirection3D(), &right, &up)` and then
		//
		//     point = EyePosition() + HeadDirection3D() * 128.0
		//                           + right * RandomFloat(-16, 16)
		//                           + up    * RandomFloat(-32, 32);
		//
		// — a jittered point straight ahead, with no velocity anywhere in it.
		// slot 278 `GetViewtarget()`'s generated signature answers `void*` and cannot carry a
		// vector; `Viewtarget` (`+0x0848`, family Facing) IS the word slot 277 wrote, so it is read
		// directly and slot 278 is named here.
		if (!HumanoidValidEyeTarget(Viewtarget))
		{
			const FVector AimForward = HeadDirection3DHumanoid();
			FVector AimRight = FVector::ZeroVector;
			FVector AimUp = FVector::ZeroVector;
			AimForward.FindBestAxisVectors(AimUp, AimRight);   // `VectorVectors` 0x10138a90
			const float RightJitter =
				Anim10_2Rng().FRandRange(-GAnim10_2JitterRight, GAnim10_2JitterRight);
			const float UpJitter = Anim10_2Rng().FRandRange(-GAnim10_2JitterUp, GAnim10_2JitterUp);
			// The three vectors are scaled BEFORE the eye is fetched, and the sum is built in the
			// order forward, right, up.
			const FVector Point = EyePosition() + AimForward * GAnim10_2ForwardScale
				+ AimRight * RightJitter + AimUp * UpJitter;
			SetViewtarget(Point);
		}
	}

	// --- Pass 8: the blink toggle (102601e1) --------------------------------------------------------
	//
	//     if (m_flNextBlink < curtime) {
	//       m_nBlinkWord = (m_nBlinkWord == 0);        // a TOGGLE, not a set
	//       m_flNextBlink = curtime + RandomFloat(1.5, 4.5);
	//     }
	//
	// The compare is STRICT (`102601f2 TEST AH,0x5; JP skip`), so a deadline exactly at curtime does
	// NOT fire.
	if (static_cast<float>(HumanoidBlinkToggleTime) < static_cast<float>(Now))
	{
		FlexToggleWord = FlexToggleWord == 0 ? 1 : 0;   // +0x0854
		HumanoidBlinkToggleTime =
			Now + Anim10_2Rng().FRandRange(GAnim10_2BlinkMin, GAnim10_2BlinkMax);
	}
}

void FElysiumNpc::MaintainEyeDirectionCyclerArm()
{
	// `1025fb7a`, the cycler-actor acquisition. Both ConVars are read `IsCommand() ? 0.0 :
	// cv->m_fValue` (`+0x28`, the FLOAT form), the MAX first and then the MIN — the order is the
	// listing's and it matters only for a test that counts reads. Then
	//
	//     AddLookTarget(UTIL_PlayerByIndex(1), 0.5, RandomFloat(min, max));
	//
	// with no fourth argument, so the ramp takes the slot's default.
	const float Max = Anim10FloatConVar(GAnim10_2CvLookMax);
	const float Min = Anim10FloatConVar(GAnim10_2CvLookMin);
	const float Duration = Anim10_2Rng().FRandRange(Min, Max);
	FElysiumEntity* Player = World != nullptr ? World->FindPlayer() : nullptr;
	// `thunk_FUN_101cd9e0(1)` — `UTIL_PlayerByIndex(1)`. Retail does NOT null-check it and
	// `AddLookTarget` stores `0xffffffff` for a null entity, which is what the port's own body does.
	AddLookTargetHumanoid(Player, 0, Duration, GAnim10_2CyclerImportance);
}

// =================================================================================================
// Family SpeciesAnim10 — slot 604's three species arms.
// =================================================================================================

int32 FElysiumNpc::SelectScheduleMeleeCombatHuman()
{
	// `CNPC_VHuman::SelectScheduleMeleeCombat` `0x10385e40`, 1,449 bytes, slot 604 for 34 census
	// classes. It REPLACES the Troika body `0x102b6c30` wholesale and never chains it.
	//
	// Every one of the float compares below was decoded off the LISTING, because the decompiler
	// renders three of them as `(a < b) != (a == b)` and `(a < b) == (a == b)`, which are `a <= b`
	// and `a > b` and are easy to read the wrong way round. The instruction is cited at each.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	FElysiumEntity* Enemy = GetEnemy();   // vtable +0x29c, fetched ONCE and reused
	const float Range = MeleeRangeUnits();              // DAT_10924a1c
	const float Distance = ScheduleHost.EnemyDistUnits; // +0x6268 m_flEnemyDist
	const FElysiumNpcConditions& Conds = Cognition.Conditions;

	if (!bInMelee)   // +0x6078 m_bInMelee
	{
		if (!Slot599(0))   // vtable +0x95c — "should I enter melee"
		{
			// `10385f25`: the melee failure gate `0x102b6fe0` is offered FIRST and any non-zero
			// answer returns.
			const int32 Gate = MeleeScheduleFailureGate(Enemy);
			if (Gate != 0)
			{
				return Gate;
			}
			// `10385f5b TEST AH,0x5; JP` — the roll is reached when `range + 200 >= distance`, i.e.
			// the far arm needs `range + 200 < distance` STRICTLY.
			if (Range + GAnim10_2FarMargin < Distance)
			{
				RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xe7"), GAnim10_2FileHuman, 1563));
				return 0xe7;
			}
			// `10385f99 CMP EAX,0x19; JL` — the roll must EXCEED 24, and `10385fc6 AND EAX,0x4100;
			// JZ` — the second distance test passes on `range <= distance`.
			if (Anim10_2Rng().RandRange(0, 99) > GAnim10_2RollFloor && Range <= Distance)
			{
				RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xe5"), GAnim10_2FileHuman, 1575));
				return 0xe5;
			}
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xe4"), GAnim10_2FileHuman, 1571));
			return 0xe4;
		}
	}
	else if (Slot602())   // vtable +0x968 — "should I leave melee"
	{
		Slot601(Enemy);   // vtable +0x964
		if (Anim10_2HasRangedWeapon(*this))   // vtable +0x4d0, slot 308
		{
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xe9"), GAnim10_2FileHuman, 1535));
			return 0xe9;
		}
		// `10385ee1 TEST AH,0x41; JP` — `2 * range <= distance` takes the far arm. **AT OR beyond**
		// twice the range, not strictly beyond; the checklist's walk said "exceeds".
		if (Range + Range <= Distance)
		{
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xe7"), GAnim10_2FileHuman, 1541));
			return 0xe7;
		}
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xe4"), GAnim10_2FileHuman, 1545));
		return 0xe4;
	}

	// --- The common tail (10386011) -----------------------------------------------------------------
	//
	// TWO offers, `0x102b7370` then `0x102b6fe0`, and either non-zero answer returns. The first has
	// no port body of its own; family Schedule left it as the entrenched-cover helper's sibling and
	// it is reached here through `ScheduleEntrenchedCoverOffer`, which answers 0.
	// `thunk_FUN_102b7370(this)` — `SelectDoorObstructionSchedule`, which `FElysiumNpc` already
	// carries (`ElysiumNpc.cpp`) as an `int32`; converted back to retail's number
	// because retail's `if (answer != 0) return answer` is over the raw one.
	const int32 DoorOffer =
		SelectDoorObstructionSchedule();
	if (DoorOffer != 0)
	{
		return DoorOffer;
	}
	const int32 Gate = MeleeScheduleFailureGate(Enemy);
	if (Gate != 0)
	{
		return Gate;
	}

	// The condition ladder, in strict retail order. The first two go through `0x10269d30`
	// (`HasInterruptCondition`) and the rest through `0x10269aa0` (`HasCondition`) — two different
	// questions, and the split is retail's.
	if (ElysiumSchedule::HasInterruptCondition(Schedule, *this, Conds,
			EElysiumNpcCond::ShouldDodge))
	{
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xd5"), GAnim10_2FileHuman, 1597));
		return 0xd5;
	}
	if (ElysiumSchedule::HasInterruptCondition(Schedule, *this, Conds,
			EElysiumNpcCond::ShouldBlock))
	{
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xd6"), GAnim10_2FileHuman, 1601));
		return 0xd6;
	}
	// BOTH conditions are read before either is tested (`1038608e` / `10386097`), so the second read
	// happens even when the first is set.
	const bool bKick = Conds.Has(EElysiumNpcCond::ShouldKick);
	const bool bStepback = Conds.Has(EElysiumNpcCond::ShouldStepback);
	if (bKick)
	{
		// `1038638f`: kick alone answers 0xdb; kick AND stepback flips a coin and answers 0xdb on a
		// 1 and 0xd3 on anything else.
		if (!bStepback || Anim10_2Rng().RandRange(0, 1) == 1)
		{
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xdb"), GAnim10_2FileHuman, 1611));
			return 0xdb;
		}
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xd3"), GAnim10_2FileHuman, 1615));
		return 0xd3;
	}
	if (bStepback)
	{
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xd3"), GAnim10_2FileHuman, 1615));
		return 0xd3;
	}
	if (Conds.Has(EElysiumNpcCond::CanMeleeAttack1))
	{
		if (Anim10_2HasRangedWeapon(*this))
		{
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xdc"), GAnim10_2FileHuman, 1625));
			return 0xdc;
		}
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xdd"), GAnim10_2FileHuman, 1629));
		return 0xdd;
	}

	// The height-difference retry stamp. It is TICKED here whatever the arms below decide, which is
	// why it is not folded into the test that reads it.
	const bool bRetryExpired = Anim10_2TickHeightDiffTimer(*this, Now);

	if (Anim10_2HasRangedWeapon(*this)
		&& (Conds.Has(EElysiumNpcCond::TooFarForMelee) || Conds.Has(EElysiumNpcCond::InterruptTime)
			|| Conds.Has(EElysiumNpcCond::EnemyUnreachable) || bRetryExpired))
	{
		Slot601(Enemy);
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xe9"), GAnim10_2FileHuman, 1663));
		return 0xe9;
	}
	if (Conds.Has(EElysiumNpcCond::EnemyUnreachable))
	{
		Slot601(Enemy);
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0x17"), GAnim10_2FileHuman, 1671));
		return 0x17;
	}
	if (!Conds.Has(EElysiumNpcCond::TooFarForMelee) && !Conds.Has(EElysiumNpcCond::TooFarToAttack))
	{
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 199"), GAnim10_2FileHuman, 1729));
		return 199;
	}
	if (Enemy != nullptr)
	{
		// `10386254`: the enemy's `WorldSpaceCenter` (slot 192) is read into a stack vector and
		// DISCARDED, then `0x102a11d0` decides.
		(void)ElysiumCameraShots::SurroundingBounds(*Enemy).GetCenter();   // slot 192
		if (ScheduleMeleeReachGate())
		{
			if (Anim10_2HasRangedWeapon(*this))
			{
				RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xe0"), GAnim10_2FileHuman, 1688));
				return 0xe0;
			}
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xe1"), GAnim10_2FileHuman, 1692));
			return 0xe1;
		}
	}
	FScheduleHintSearchRequest CoverRequest;
	CoverRequest.bRequest2 = true;
	CoverRequest.bRequest4 = true;   // `thunk_FUN_102b7690(this, 0, 1, 0, 1)`
	const int32 CoverOffer = SelectCoverOrKickSchedule(CoverRequest);
	if (CoverOffer != 0)
	{
		return CoverOffer;
	}
	// `102862f6 TEST AH,0x41; JNP` — the near pair needs `distance < range` STRICTLY (the checklist's
	// walk said "at or below") and the retry stamp NOT expired.
	if (Distance < Range && !bRetryExpired)
	{
		if (Anim10_2HasRangedWeapon(*this))
		{
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xd1"), GAnim10_2FileHuman, 1719));
			return 0xd1;
		}
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xd2"), GAnim10_2FileHuman, 1723));
		return 0xd2;
	}
	if (Anim10_2HasRangedWeapon(*this))
	{
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xca"), GAnim10_2FileHuman, 1708));
		return 0xca;
	}
	RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xcb"), GAnim10_2FileHuman, 1712));
	return 0xcb;
}

int32 FElysiumNpc::SelectScheduleMeleeCombatMingXiao()
{
	// `CNPC_VMingXiao::SelectScheduleMeleeCombat` `0x10396050`, 1,522 bytes. The same skeleton as
	// `0x10385e40` with FOUR stated differences, each marked below.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	const float Range = MeleeRangeUnits();
	const float Distance = ScheduleHost.EnemyDistUnits;
	const FElysiumNpcConditions& Conds = Cognition.Conditions;

	if (!bInMelee)
	{
		// **DIFFERENCE 1**: MingXiao re-fetches `GetEnemy()` at every use rather than caching it, and
		// its not-engaged arm does NOT offer the melee failure gate `0x102b6fe0` at all.
		if (!Slot599(0))
		{
			// **DIFFERENCE 2**: the distance is tested BEFORE the roll, where the human body offers
			// the gate first. `10396095` is the same `range + 200 < distance` strict compare.
			if (Range + GAnim10_2FarMargin < Distance)
			{
				RecordScheduleEvent(
					FString::Printf(TEXT("%s:%d -> 0xe7"), GAnim10_2FileMingXiao, 2644));
				return 0xe7;
			}
			if (Anim10_2Rng().RandRange(0, 99) > GAnim10_2RollFloor && Range <= Distance)
			{
				RecordScheduleEvent(
					FString::Printf(TEXT("%s:%d -> 0xe5"), GAnim10_2FileMingXiao, 2656));
				return 0xe5;
			}
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xe4"), GAnim10_2FileMingXiao, 2652));
			return 0xe4;
		}
	}
	else if (Slot602())
	{
		Slot601(GetEnemy());
		if (Anim10_2HasRangedWeapon(*this))
		{
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xe9"), GAnim10_2FileMingXiao, 2623));
			return 0xe9;
		}
		if (Range + Range <= Distance)
		{
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xe7"), GAnim10_2FileMingXiao, 2629));
			return 0xe7;
		}
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xe4"), GAnim10_2FileMingXiao, 2633));
		return 0xe4;
	}

	// **DIFFERENCE 3**: the common tail offers ONLY `0x102b7370`; there is no second
	// `0x102b6fe0` offer.
	// `thunk_FUN_102b7370(this)` — `SelectDoorObstructionSchedule`, which `FElysiumNpc` already
	// carries (`ElysiumNpc.cpp`) as an `int32`; converted back to retail's number
	// because retail's `if (answer != 0) return answer` is over the raw one.
	const int32 DoorOffer =
		SelectDoorObstructionSchedule();
	if (DoorOffer != 0)
	{
		return DoorOffer;
	}

	// **DIFFERENCE 4a**: an extra arm the human body lacks, and it OPENS the ladder — `COND 0x48
	// ENEMY_OCCLUDED`, read through `HasCondition` and not through the interrupt form.
	if (Conds.Has(EElysiumNpcCond::EnemyOccluded))
	{
		if (Anim10_2HasRangedWeapon(*this))
		{
			Slot601(GetEnemy());
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xe9"), GAnim10_2FileMingXiao, 2675));
			return 0xe9;
		}
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xcd"), GAnim10_2FileMingXiao, 2685));
		return 0xcd;
	}
	if (ElysiumSchedule::HasInterruptCondition(Schedule, *this, Conds,
			EElysiumNpcCond::ShouldDodge))
	{
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xd5"), GAnim10_2FileMingXiao, 2692));
		return 0xd5;
	}
	// **DIFFERENCE 4b**: there is NO `COND 0x0d SHOULD_BLOCK` arm at all.
	const bool bKick = Conds.Has(EElysiumNpcCond::ShouldKick);
	const bool bStepback = Conds.Has(EElysiumNpcCond::ShouldStepback);
	if (bKick)
	{
		if (!bStepback || Anim10_2Rng().RandRange(0, 1) == 1)
		{
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xdb"), GAnim10_2FileMingXiao, 2702));
			return 0xdb;
		}
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xd3"), GAnim10_2FileMingXiao, 2706));
		return 0xd3;
	}
	if (bStepback)
	{
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xd3"), GAnim10_2FileMingXiao, 2706));
		return 0xd3;
	}
	if (Conds.Has(EElysiumNpcCond::CanMeleeAttack1))
	{
		if (Anim10_2HasRangedWeapon(*this))
		{
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xdc"), GAnim10_2FileMingXiao, 2716));
			return 0xdc;
		}
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xdd"), GAnim10_2FileMingXiao, 2720));
		return 0xdd;
	}

	const bool bRetryExpired = Anim10_2TickHeightDiffTimer(*this, Now);
	if (Anim10_2HasRangedWeapon(*this)
		&& (Conds.Has(EElysiumNpcCond::TooFarForMelee) || Conds.Has(EElysiumNpcCond::InterruptTime)
			|| Conds.Has(EElysiumNpcCond::EnemyUnreachable) || bRetryExpired))
	{
		Slot601(GetEnemy());
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xe9"), GAnim10_2FileMingXiao, 2754));
		return 0xe9;
	}
	if (Conds.Has(EElysiumNpcCond::EnemyUnreachable))
	{
		Slot601(GetEnemy());
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0x17"), GAnim10_2FileMingXiao, 2762));
		return 0x17;
	}
	if (!Conds.Has(EElysiumNpcCond::TooFarForMelee) && !Conds.Has(EElysiumNpcCond::TooFarToAttack))
	{
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 199"), GAnim10_2FileMingXiao, 2820));
		return 199;
	}
	FElysiumEntity* Enemy = GetEnemy();
	if (Enemy != nullptr)
	{
		(void)ElysiumCameraShots::SurroundingBounds(*Enemy).GetCenter();   // slot 192
		if (ScheduleMeleeReachGate())
		{
			if (Anim10_2HasRangedWeapon(*this))
			{
				RecordScheduleEvent(
					FString::Printf(TEXT("%s:%d -> 0xe0"), GAnim10_2FileMingXiao, 2779));
				return 0xe0;
			}
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xe1"), GAnim10_2FileMingXiao, 2783));
			return 0xe1;
		}
	}
	FScheduleHintSearchRequest CoverRequest;
	CoverRequest.bRequest2 = true;
	CoverRequest.bRequest4 = true;   // `thunk_FUN_102b7690(this, 0, 1, 0, 1)`
	const int32 CoverOffer = SelectCoverOrKickSchedule(CoverRequest);
	if (CoverOffer != 0)
	{
		return CoverOffer;
	}
	if (Distance < Range && !bRetryExpired)
	{
		if (Anim10_2HasRangedWeapon(*this))
		{
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xd1"), GAnim10_2FileMingXiao, 2810));
			return 0xd1;
		}
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xd2"), GAnim10_2FileMingXiao, 2814));
		return 0xd2;
	}
	if (Anim10_2HasRangedWeapon(*this))
	{
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xca"), GAnim10_2FileMingXiao, 2799));
		return 0xca;
	}
	RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xcb"), GAnim10_2FileMingXiao, 2803));
	return 0xcb;
}

int32 FElysiumNpc::SelectScheduleMeleeCombatBach()
{
	// `CNPC_VBach::SelectScheduleMeleeCombat` `0x10364080`, 395 bytes, `CNPC_VBach#604` only.
	//
	// A weapon-DISCIPLINE prologue — Bach must be holding the right gun or the right sword for the
	// condition he is in — and then the HUMAN body, with a `+0x6444` clear and a forced `0x159` when
	// that answered zero. All three conditions (0x7b, 0x7a, 0x79) are read through `0x10269aa0`,
	// `HasCondition`; none of them has a producer in this runtime, which is stated at the call.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	const FElysiumNpcConditions& Conds = Cognition.Conditions;

	// `0x7b`: the fail arm. It stamps a curtime deadline at `+0x6690` and returns 0x15a.
	if (Conds.Has(static_cast<EElysiumNpcCond>(0x7b)))
	{
		BachFailStamp = Now + GAnim10_2BachFailDelay;   // curtime + _DAT_10463584 (15.0f)
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0x15a"), GAnim10_2FileBach, 622));
		return 0x15a;
	}

	// `CBaseCombatCharacter::GetActiveWeapon()` is fetched ONCE here and the `0x7a` condition read
	// immediately after it, before either is used.
	const FElysiumEntity* Weapon = ActiveWeaponEntity();
	const bool bKatanaCondition = Conds.Has(static_cast<EElysiumNpcCond>(0x7a));
	if (Weapon == nullptr)
	{
		if (bKatanaCondition)
		{
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0x158"), GAnim10_2FileBach, 652));
			return 0x158;
		}
		if (Conds.Has(static_cast<EElysiumNpcCond>(0x79)))
		{
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0x159"), GAnim10_2FileBach, 656));
			return 0x159;
		}
	}
	else if (!bKatanaCondition)
	{
		if (Conds.Has(static_cast<EElysiumNpcCond>(0x79)))
		{
			// Armed, rifle condition: the weapon must BE the rifle, or the body refuses with 0x159.
			// A match falls through to slot 605 `SelectScheduleRangedCombat` (vtable +0x974) and
			// returns ITS answer — the one arm of this body that leaves the melee family entirely.
			if (Weapon->Def == nullptr
				|| !Weapon->Def->Classname.Equals(GAnim10_2BachRifle, ESearchCase::IgnoreCase))
			{
				RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0x159"), GAnim10_2FileBach, 640));
				return 0x159;
			}
			return SelectScheduleRangedCombat(0);
		}
	}
	else
	{
		// Armed, katana condition: the weapon must BE the katana, or 0x158.
		if (Weapon->Def == nullptr
			|| !Weapon->Def->Classname.Equals(GAnim10_2BachKatana, ESearchCase::IgnoreCase))
		{
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0x158"), GAnim10_2FileBach, 633));
			return 0x158;
		}
	}

	// The fall-through: `thunk_FUN_10385e40(this, param_1)` — the HUMAN body, called DIRECTLY and
	// not through the vtable, so a class that overrode slot 604 does not re-enter here.
	const int32 Answer = SelectScheduleMeleeCombatHuman();
	// `+0x6444` is cleared unless `m_NPCState` is 4 or 0xc. It is cleared on EVERY path out of the
	// human body, including the ones that answered non-zero.
	const int32 State = Anim10_2RetailNpcState(Mind.State());
	if (State != 4 && State != 0xc)
	{
		BachClearWord = 0;
	}
	if (Answer == 0)
	{
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0x159"), GAnim10_2FileBach, 669));
		return 0x159;
	}
	return Answer;
}

// =================================================================================================
// Family SpeciesAnim10 — slot 509's zombie arm.
// =================================================================================================

bool FElysiumNpc::ShouldPlayIdleSoundZombieArm() const
{
	// `CNPC_VZombie#509` is `0x103e0fa0` and is the ONLY override of slot 509 in the census; every
	// other class inherits the Troika body `0x10294040`. Keyed on the row's retail ADDRESS, the
	// convention family Precache10 set.
	const FElysiumNpcClassSlot* Override = ElysiumNpcKernelClass::OverrideOf(RetailClass(), 509);
	return Override != nullptr
		&& FCString::Strcmp(Override->Address, TEXT("0x103e0fa0")) == 0;
}

bool FElysiumNpc::ShouldPlayIdleSoundZombie()
{
	// `CNPC_VZombie::vfunc509` `0x103e0fa0`, 166 bytes, `CNPC_VZombie#509` only. It REPLACES the
	// Troika body `0x10294040` wholesale: there is no `IsInDialog` refusal, no `m_NPCState` test and
	// no `SF_NPC_GAG` test in it at all, which is why a zombie vocalises in states where a human
	// would not.
	//
	// Every refusal returns false through the same `return (uint)piVar3 & 0xffffff00` low-byte clear.

	// 1. `m_bIsBCCTargetable` clear -> false. SEAM: family Sounds10 recorded that this byte
	//    (`+0x7ec` on `CBaseCombatCharacter`) has no port member, so the arm is not tested; stated
	//    here rather than silently dropped.

	// 2. A LIVE `m_hDialogPartner` (+0x0fe8) -> false. The port stands for it with "this character
	//    owns the open dialogue session", the same reading family Sounds10 made.
	if (World != nullptr)
	{
		const FElysiumEntityHandle DialogOwner = World->GetOpenDialogOwner();
		if (DialogOwner.IsSet() && DialogOwner.Index == Handle.Index)
		{
			return false;
		}
	}

	// 3. `IsBusyWithDiscipline()` -> false.
	if (IsBusyWithDiscipline())
	{
		return false;
	}

	// 4. The weight. 999 by default; a RUNNING schedule whose local id (slot 447
	//    `GetLocalScheduleId`) is `0x12f SCHED_TROIKA_COMFORT` drops it to 20 — a 1-in-21 roll — AND
	//    skips the float-sound arm entirely. `GetLocalScheduleId` answers -1 for every id today
	//    (family Sounds10's note: no schedule text is parsed), so the comfort branch is unreachable
	//    until story 10i registers the schedule.
	int32 Weight = GAnim10_2ZombieIdleWeight;
	bool bComforting = false;
	if (Schedule.IsRunning())
	{
		if (GetLocalScheduleId(Schedule.Current) == GAnim10_2ScheduleComfort)
		{
			Weight = GAnim10_2ZombieComfortWeight;
			bComforting = true;
		}
	}

	// 5. Otherwise slot 510 `ShouldPlayFloatSound` decides: true plays slot 507 `FloatSound` and
	//    returns FALSE. The float sound is played INSTEAD of an idle sound, not beside it.
	if (!bComforting)
	{
		if (ShouldPlayFloatSound())
		{
			FloatSound();
			return false;
		}
	}

	// 6. True only when the roll is EXACTLY 0.
	return Anim10_2Rng().RandRange(0, Weight) == 0;
}
