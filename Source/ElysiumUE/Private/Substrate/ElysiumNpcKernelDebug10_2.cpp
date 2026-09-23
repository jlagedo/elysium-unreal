#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"

// Story 29d, family **Debug10**, the geometry half — slot 123's four census bodies and
// `CAI_BaseNPCTroika::NPCThinkDebugPre`. `ElysiumNpcKernelDebug10.cpp` carries the ring, the trace
// messages, the seams and the slot-124 text bodies, and states this family's standing facts.
//
// `0x1029ca50` and `0x10292500` were both recovered from the LISTING. The decompiler folded every
// `NDebugOverlay` argument list into their stack frames and, for `0x10292500`, reports the body
// DAMAGED outright ("type propagation not settling") — its x87 clamp chains come out as
// `extraout_ST0` and its two witness colour ladders are lost entirely. What is ported below is the
// listing's own push order, its constants read out of the pinned image's `.rdata`, and its gates.

namespace
{
	// Unit-prefixed: adaptive unity merges anonymous namespaces.

	// --- `m_debugOverlays` bits, with the instruction that tests each ------------------------------
	constexpr int32 GDebug10_2BitText = 0x1;              // 0x10372f0b `TEST byte [+0x224],0x1`
	constexpr int32 GDebug10_2BitViewCones = 0x400000;    // 0x1029ca59
	constexpr int32 GDebug10_2BitCollisionBox = 0x1000;   // 0x1029cb39 `TEST AH,0x10`
	constexpr int32 GDebug10_2BitWeaponRings = 0x20000000;// 0x1029cd68

	// --- The ConVars this file gates on, every one shipping 0 --------------------------------------
	constexpr ElysiumNpcTunables::EConVar GDebug10_2CvEnemyBody =
		ElysiumNpcTunables::EConVar::DebugShowBodyTargets;    // DAT_10924f24
	constexpr ElysiumNpcTunables::EConVar GDebug10_2CvThinkTrace =
		ElysiumNpcTunables::EConVar::DebugVisionLine;         // DAT_1092479c
	constexpr ElysiumNpcTunables::EConVar GDebug10_2CvThinkEye =
		ElysiumNpcTunables::EConVar::DebugWeaponShootPos;     // DAT_109244c4
	constexpr ElysiumNpcTunables::EConVar GDebug10_2CvThinkExtra =
		ElysiumNpcTunables::EConVar::DebugShowNpcSkeletons;   // DAT_1092435c

	// --- Retail's `NDebugOverlay` entry points, by name --------------------------------------------
	constexpr TCHAR GDebug10_2Box[] = TEXT("NDebugOverlay::Box");
	constexpr TCHAR GDebug10_2BoxAngles[] = TEXT("NDebugOverlay::BoxAngles");
	constexpr TCHAR GDebug10_2BoxDirection[] = TEXT("NDebugOverlay::BoxDirection");
	constexpr TCHAR GDebug10_2Line[] = TEXT("NDebugOverlay::Line");
	constexpr TCHAR GDebug10_2Text[] = TEXT("NDebugOverlay::Text");
	constexpr TCHAR GDebug10_2Circle[] = TEXT("NDebugOverlay::Circle");
	constexpr TCHAR GDebug10_2ViewCone[] = TEXT("0x1029c4a0");

	// --- The census addresses slot 123 dispatches on -----------------------------------------------
	constexpr TCHAR GDebug10_2Body_Troika[] = TEXT("0x1029ca50");
	constexpr TCHAR GDebug10_2Body_VCop[] = TEXT("0x10372f00");
	constexpr TCHAR GDebug10_2Body_MingXiao[] = TEXT("0x10399d40");
	constexpr TCHAR GDebug10_2Body_Maker[] = TEXT("0x1034bd30");
	constexpr TCHAR GDebug10_2Body_ScriptedTarget[] = TEXT("0x1034e070");

	// --- Every box extent `0x1029ca50` and `0x10292500` push, in SOURCE units ----------------------
	// `0xc0400000`/`0x40400000` = -3.0/3.0; `0xc0000000`/`0x40000000` = -2.0/2.0;
	// `0xc1200000`/`0x41200000` = -10.0/10.0; `0x43480000` = 200.0; `0x42000000` = 32.0.
	const FVector GDebug10_2Box2Mins(-2.f, -2.f, -2.f);
	const FVector GDebug10_2Box2Maxs(2.f, 2.f, 2.f);
	const FVector GDebug10_2Box3Mins(-3.f, -3.f, -3.f);
	const FVector GDebug10_2Box3Maxs(3.f, 3.f, 3.f);
	const FVector GDebug10_2Box10Mins(-10.f, -10.f, -10.f);
	const FVector GDebug10_2Box10Maxs(10.f, 10.f, 10.f);
	// The hint facing box: `(0, 0, -10)` to `(200, 0, 10)`, a 200-unit ray.
	const FVector GDebug10_2FacingMins(0.f, 0.f, -10.f);
	const FVector GDebug10_2FacingMaxs(200.f, 0.f, 10.f);
	// The ring axis the weapon and MingXiao circles spin about: `(1, 0, 0)`.
	const FVector GDebug10_2RingAxis(1.f, 0.f, 0.f);

	// --- The recovered float constants ------------------------------------------------------------
	//
	// Every one read out of the pinned image's `.rdata`, by address. The overlay DURATIONS are named
	// here and are NOT carried on the captured line: `FDebugLine` has no duration column, and a
	// duration decides how long a picture stays on a screen this runtime does not have. Named,
	// visual-only (see the family's `.inl` header).
	constexpr float GDebug10_2Dur0 = 0.f;          // the ordinary "one frame" overlay
	constexpr float GDebug10_2DurHalf = 0.5f;      // 0x3f000000 — the two witness boxes and the LOS line
	constexpr float GDebug10_2DurFifth = 0.2f;     // 0x3e4ccccd — the eye/ideal pair
	constexpr float GDebug10_2DurTenth = 0.1f;     // 0x3dcccccd — the five enemy body-target boxes

	constexpr float GDebug10_2WitnessScale = 0.2f;    // _DAT_10451ab4
	constexpr float GDebug10_2Chan255 = 255.f;        // _DAT_1044fffc
	constexpr float GDebug10_2Chan192 = 192.f;        // _DAT_1049ae14
	constexpr float GDebug10_2Chan64 = ElysiumNpcTunables::SixtyFour;
	constexpr float GDebug10_2CriminalLiftUnits = 16.f;     // _DAT_10451ad0
	constexpr float GDebug10_2SupernaturalLiftUnits = 12.f; // _DAT_1044faa4
	constexpr float GDebug10_2BlockedLiftUnits = 20.f;      // _DAT_1044eb0c
	constexpr float GDebug10_2CopLabelLiftUnits = 8.f;      // _DAT_1045597c
	constexpr float GDebug10_2UnarmedFarUnits = 2000.f;     // 0x44fa0000
	constexpr float GDebug10_2UnarmedNearUnits = 0.f;
	constexpr float GDebug10_2RingHeightUnits = 32.f;       // 0x42000000
	constexpr float GDebug10_2MingXiaoRingHeightUnits = 8.f;// 0x41000000
	constexpr float GDebug10_2HintLeanYawDegrees = 43.f;    // 0x422c0000 / 0xc22c0000
	constexpr float GDebug10_2RelationRadiusUnits = 4096.f; // 0x45800000
	// `tr.fraction`'s "nothing was hit" value.
	constexpr double GDebug10_2TraceClearFraction = ElysiumNpcTunables::OneDouble;

	// The four MingXiao ring radii, in SOURCE units, in the order `0x10399d40` draws them. These are
	// the reason the body is worth porting at all: they are the recovered range bands.
	constexpr float GDebug10_2MingXiaoRadiiUnits[] = { 100.f, 150.f, 200.f, 300.f };

	// The three cone constants `0x1029ca50` pushes as its third argument to `0x1029c4a0`: 0.0, 2.0
	// and 4.0, as raw float bit patterns in the listing.
	constexpr float GDebug10_2Cone1Arg3 = 0.f;    // 0x0
	constexpr float GDebug10_2Cone2Arg3 = 2.f;    // 0x40000000
	constexpr float GDebug10_2Cone3Arg3 = 4.f;    // 0x40800000

	// The five hint types `0x1029ca50`'s facing arm accepts. Everything else skips the arm outright.
	constexpr int32 GDebug10_2HintTypeA = 100;      // and 0x65, 0x283c, 0x283d — the same arm
	constexpr int32 GDebug10_2HintTypeB = 0x65;
	constexpr int32 GDebug10_2HintTypeLean = 0x27d8;
	constexpr int32 GDebug10_2HintTypeC = 0x283c;
	constexpr int32 GDebug10_2HintTypeD = 0x283d;

	// `CNPC_VCop#123`'s five relationship labels, read out of `.rdata`.
	constexpr TCHAR GDebug10_2CopLabelHate[] = TEXT("D_HT");     // 0x105cc520
	constexpr TCHAR GDebug10_2CopLabelFear[] = TEXT("D_FR");     // 0x105cc518
	constexpr TCHAR GDebug10_2CopLabelLike[] = TEXT("D_LI");     // 0x105cc510
	constexpr TCHAR GDebug10_2CopLabelNeutral[] = TEXT("D_NU");  // 0x105cc508
	constexpr TCHAR GDebug10_2CopLabelError[] = TEXT("D_ER");    // 0x10636728
	constexpr TCHAR GDebug10_2CopSuspect[] = TEXT(" Suspect");   // 0x10636750
	constexpr TCHAR GDebug10_2CopAlert[] = TEXT(" Alert");       // 0x10636748
	constexpr TCHAR GDebug10_2CopCount[] = TEXT(" Count%d");     // 0x1063673c
	constexpr TCHAR GDebug10_2CopPursuit[] = TEXT(" Pursuit");   // 0x10636730
	constexpr TCHAR GDebug10_2CopTally[] = TEXT("  %d  %d");     // 0x1063671c

	constexpr TCHAR GDebug10_2BlockedBy[] = TEXT("Blocked by %s"); // 0x105d8b44
	constexpr TCHAR GDebug10_2UnknownName[] = TEXT("**UNKNOWN**"); // 0x105477a4

	// The root bone `0x1029ca50`'s `0x1000` arm boxes.
	constexpr TCHAR GDebug10_2RootBone[] = TEXT("Bip01");         // 0x105a7998

	// `(int)min(channel, channel * scale)` — the clamp `NPCThinkDebugPre`'s two witness arms apply to
	// every colour channel. The listing computes `x = (timer - curtime) * 0.2` ONCE and then folds
	// each channel through `FMUL c; FLD c; FCOMP; …; __ftol`, which is exactly this. The checklist's
	// walk read the whole thing as "the remaining-time scalar clamped to 64.0"; it is three separate
	// per-channel clamps and the channel ceilings differ between the two arms.
	int32 GDebug10_2WitnessChannel(float Ceiling, float Scale)
	{
		return static_cast<int32>(FMath::Min(Ceiling, Ceiling * Scale));
	}

	// `GetDebugName()`, as `ElysiumNpcKernelDebug10.cpp` spells it.
	FString GDebug10_2DebugName(const FElysiumEntity* Entity)
	{
		if (Entity == nullptr)
		{
			return FString();
		}
		if (!Entity->TargetName.IsEmpty())
		{
			return Entity->TargetName;
		}
		return Entity->Def != nullptr ? Entity->Def->Classname : FString();
	}
}

// -------------------------------------------------------------------------------------------------
// The geometry seams.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::EmitViewConeOverlay(float LengthUnits, float FovDot, uint32 Arg3, int32 Arg4,
	int32 Arg5, int32 Arg6, int32 Arg7, int32 Arg8) const
{
	// `0x1029c4a0` — a 400-byte helper with no verdict row of its own (it is not one of the band's
	// core functions), so its body is NOT written here. What IS this row's deliverable is the three
	// CALLS and their eight arguments, which is what is recorded: the arm, the order and the
	// constants. `Arg3` is carried as its raw float bit pattern because the listing pushes it that
	// way and one of the three is a literal `0`.
	EmitOverlayText(GDebug10_2ViewCone, Origin / ElysiumMove::U,
		FString::Printf(TEXT("len=%.1f fov=%.4f a3=%.1f rgb=(%d %d %d) a7=%d a8=%d"),
			LengthUnits, FovDot, FMath::AsFloat(Arg3), Arg4, Arg5, Arg6, Arg7, Arg8));
}

float FElysiumNpc::ViewConeLengthForPlayer(float BaseLengthUnits, const FElysiumEntity* Player) const
{
	// SEAM for `0x1029c9f0(length, player)`. No verdict row; answers the length handed in, so the
	// second cone differs from the first only by the constants the arm pushes.
	(void)Player;
	return BaseLengthUnits;
}

float FElysiumNpc::ViewConeThirdLength(float BaseLengthUnits) const
{
	// SEAM for `0x1029ca30(length)`. Same reasoning.
	return BaseLengthUnits;
}

bool FElysiumNpc::HintOverlayWords(int32& OutHintType, float& OutHintYawDegrees,
	float& OutNodeYawDegrees, FVector& OutOriginUnits) const
{
	// SEAM for `m_pHintNode`'s four overlay words: `m_nHintType` (`+0x5dc`), the yaw at `+0x454`,
	// `0x102d12e0`'s own yaw and the node's `GetAbsOrigin()`. Family Hints records that hints are
	// node INDICES in this runtime with no type or angle store behind them.
	OutHintType = INDEX_NONE;
	OutHintYawDegrees = 0.f;
	OutNodeYawDegrees = 0.f;
	OutOriginUnits = FVector::ZeroVector;
	return false;
}

FVector FElysiumNpc::RetailWorldSpaceCenterUnits(const FElysiumEntity* Entity)
{
	// `CBaseEntity::WorldSpaceCenter()` (slot 192) is the collision OBB's centre in world space.
	// Story 29c-1's `CollisionObbExtentsUnits` records that no collision extents stand on the kernel
	// surface, so the centre IS the origin — which is what retail's own body answers for an entity
	// whose OBB is a point, and the arm this port therefore takes.
	return Entity != nullptr ? Entity->Origin / ElysiumMove::U : FVector::ZeroVector;
}

FVector FElysiumNpc::RetailBodyTargetUnits(const FElysiumEntity* Entity)
{
	// SEAM for slot 197 `BodyTarget(posSrc, bNoisy)`, whose recovered body blends `WorldSpaceCenter`
	// (192) and `EyePosition` (193) and randomises when the noisy flag is set. No blend weight stands
	// here; the eye point is the blend's own endpoint.
	//
	// Retail passes an UNINITIALISED stack vector as `posSrc` at this call site — the slot is
	// `(&out, &uninitialised, 1, 0)` and nothing ever writes that local. The read is a retail defect
	// with no observable consequence for the recovered blend, and is NOT reproduced.
	return Entity != nullptr
		? const_cast<FElysiumEntity*>(Entity)->EyePosition() / ElysiumMove::U
		: FVector::ZeroVector;
}

TArray<FElysiumNpc*> FElysiumNpc::RelationshipLineCandidates(const FVector& CentreUnits,
	float RadiusUnits) const
{
	// `0x100f8490` over `gEntList` (`0x106eb5d8`) with mask `0x820` and radius 4096.0 source units.
	// `FElysiumEntityWorld::EntityList` IS retail's entity list and its index IS the handle index
	// (story 29c-1's cleanup), so the walk is real and in the list's own ascending order.
	TArray<FElysiumNpc*> Out;
	if (World == nullptr)
	{
		return Out;
	}
	const float RadiusSqUnits = RadiusUnits * RadiusUnits;
	for (const TUniquePtr<FElysiumEntity>& Entity : World->Entities())
	{
		if (!Entity.IsValid())
		{
			continue;
		}
		// SEAM for the `0x820` content mask. The arm's own NEXT test is `ent->+0x9c` (the cached
		// combat-character downcast) and it BREAKS the whole walk on a null one, so an entity with
		// no NPC is what ends retail's iteration; asking "has an NPC" here is the same question one
		// step earlier and never ends the walk early on an unrelated prop.
		FElysiumNpc* const Npc = Entity->AsNpc();
		if (Npc == nullptr)
		{
			continue;
		}
		const FVector OtherUnits = Entity->Origin / ElysiumMove::U;
		if (FVector::DistSquared(OtherUnits, CentreUnits) > RadiusSqUnits)
		{
			continue;
		}
		Out.Add(Npc);
	}
	return Out;
}

bool FElysiumNpc::EyeToEyeBlocker(const FElysiumEntity* Player, FElysiumEntity*& OutBlocker) const
{
	// SEAM for `NPCThinkDebugPre`'s `COND_SEE_PLAYER` trace: a ray from `EyePosition()` to the
	// player's, mask -1, filtered by this NPC (`0x101d3190`), then `tr.fraction != 1.0`. There is no
	// ray cast on the kernel surface; false is retail's clear-line arm.
	(void)Player;
	OutBlocker = nullptr;
	return false;
}

FVector FElysiumNpc::RetailStandoffAnchorUnits(const FVector& EyeUnits) const
{
	// SEAM for `0x10278650(out, in, 0.0, 0.0)` — family **Motor10**'s row in this same band
	// (`FElysiumNpc::ComputeStandoffAnchorOffset`). Answers the position handed in until the two are
	// wired, so the ±2 box lands on the ±3 box and the joining line has zero length, which is what
	// retail draws when the offset is zero.
	return EyeUnits;
}

void FElysiumNpc::RunThinkDebugPreExtra(int32 Mode) const
{
	// SEAM for `0x1028e030` (mode 1) and `0x1028e060` (mode 2). Neither has a verdict row; the
	// SELECTION is what this row is observable for and it is recorded by address.
	if (Mode == 1)
	{
		EmitDevMsg(TEXT("0x1028e030"), TEXT("0x1028e030"));
		return;
	}
	if (Mode == 2)
	{
		EmitDevMsg(TEXT("0x1028e060"), TEXT("0x1028e060"));
	}
}

void FElysiumNpc::MakerDrawDebugGeometryOverlays()
{
	// `CNPCMaker::DrawDebugGeometryOverlays` (`0x1034bd30`). SEAM: its verdict is `registry:123` in
	// band 0–4 — the maker's story, not this one. Reached here only so a spawned `npc_maker` does not
	// silently fall onto the Troika body now that this family owns the slot.
	EmitDevMsg(TEXT("0x1034bd30"), TEXT("0x1034bd30"));
}

// -------------------------------------------------------------------------------------------------
// Slot 123 — the dispatcher and the four arms.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::DrawDebugGeometryOverlays()
{
	// slot 123, dispatched on the census body for this NPC's retail class, as story 29c-1's slot-76
	// dispatcher does. A spawned `npc_VCop` answers a NULL `RetailClass()` (the census gives
	// `CNPC_VCop` no entity classname) and therefore takes the TROIKA body — the recovered answer.
	const TCHAR* const SlotBody = ElysiumNpcKernelClass::BodyOf(RetailClass(), 123);
	if (SlotBody != nullptr)
	{
		if (FCString::Strcmp(SlotBody, GDebug10_2Body_VCop) == 0)
		{
			VCopDrawDebugGeometryOverlays();
			return;
		}
		if (FCString::Strcmp(SlotBody, GDebug10_2Body_MingXiao) == 0)
		{
			MingXiaoDrawDebugGeometryOverlays();
			return;
		}
		if (FCString::Strcmp(SlotBody, GDebug10_2Body_Maker) == 0)
		{
			MakerDrawDebugGeometryOverlays();
			return;
		}
		if (FCString::Strcmp(SlotBody, GDebug10_2Body_ScriptedTarget) == 0)
		{
			// `CScriptedTarget#123` (`0x1034e070`) is story 29c-1's body, in band 0–4.
			ScriptedTargetDrawDebugGeometryOverlays();
			return;
		}
	}
	TroikaDrawDebugGeometryOverlays();
}

void FElysiumNpc::TroikaDrawDebugGeometryOverlays()
{
	// `0x1029ca50`, 2,129 bytes, arm by arm and in retail's order.

	// --- Arm 1, `0x400000` — three view cones through `0x1029c4a0` ---------------------------------
	//
	// The three calls share two registers: EDI carries the LENGTH and EBX the FIELD OF VIEW, and the
	// SECOND call overwrites both, so the third cone is drawn with the player-scaled pair whenever a
	// closest player resolved and with the plain pair otherwise. That register reuse is the arm.
	if ((DebugOverlays & GDebug10_2BitViewCones) != 0)
	{
		const double ConeNow = World != nullptr ? World->NowSeconds() : 0.0;
		float LengthUnits =
			Senses.EffectiveVisionDistanceCm(*this, ConeNow) / ElysiumMove::U;
		float FovDot = RetailFieldOfViewDot();
		EmitViewConeOverlay(LengthUnits, FovDot, 0x00000000u, 0x40, 0xc0, 0x40, 0x32, 1);

		const FElysiumEntity* const ClosestPlayer =
			World != nullptr ? World->Resolve(Senses.Memory.ClosestPlayer) : nullptr;
		if (ClosestPlayer != nullptr)
		{
			// `m_flFieldOfView / player->vtable[0x74]()` — the player's own scale word. SEAM: slot
			// `+0x74` on a player has no port body; a scale of 1.0 leaves the dot unchanged, which
			// is the value retail's own default answers.
			LengthUnits = ViewConeLengthForPlayer(LengthUnits, ClosestPlayer);
			EmitViewConeOverlay(LengthUnits, FovDot, 0x40000000u, 0xc0, 0x40, 0, 100, 0);
		}
		EmitViewConeOverlay(ViewConeThirdLength(LengthUnits), FovDot, 0x40800000u, 0xff, 0, 0x80,
			0x96, 0);
	}

	// --- Arm 2, `0x1000` — the four boxes ----------------------------------------------------------
	if ((DebugOverlays & GDebug10_2BitCollisionBox) != 0)
	{
		// 2a. The collision box, ORIENTED by `m_vecForward` (`+0x6290`), drawn only when the OBB is
		//     not degenerate. `0x10144cd0` takes the direction as its SECOND argument, before the
		//     extents.
		FVector ObbMins = FVector::ZeroVector;
		FVector ObbMaxs = FVector::ZeroVector;
		const bool bHasObb = CollisionObbExtentsUnits(ObbMins, ObbMaxs);
		if (bHasObb && (ObbMins.X != ObbMaxs.X || ObbMins.Y != ObbMaxs.Y || ObbMins.Z != ObbMaxs.Z))
		{
			EmitOverlayBoxDirection(GDebug10_2BoxDirection, Origin / ElysiumMove::U, ObbMins,
				ObbMaxs, Forward, 0x80, 0, 0, 0x28);
		}

		// 2b. A ±10 box at the `"Bip01"` bone, ANGLED to that bone (`0x10142b80`, `BoxAngles`),
		//     unconditional within the arm.
		FVector BoneUnits = FVector::ZeroVector;
		FVector BoneAngles = FVector::ZeroVector;
		if (RetailBonePosition(GDebug10_2RootBone, BoneUnits, BoneAngles))
		{
			EmitOverlayText(GDebug10_2BoxAngles, BoneUnits,
				FString::Printf(TEXT("mins=(-10.0 -10.0 -10.0) maxs=(10.0 10.0 10.0) ")
					TEXT("ang=(%.1f %.1f %.1f) rgba=(128 0 0 40)"),
					BoneAngles.X, BoneAngles.Y, BoneAngles.Z));
		}

		// 2c. The attack-extents box: the SAME collision extents, widened by `m_vecAttackExtents`
		//     (`+0x50`), drawn only when that vector is not `vec3_origin`. Colour 255/128/0 at 20.
		const FVector AttackExtentsUnits = GetAttackExtents() / ElysiumMove::U;
		if (!AttackExtentsUnits.IsZero())
		{
			EmitOverlayBox(GDebug10_2Box, Origin / ElysiumMove::U,
				ObbMins - AttackExtentsUnits, ObbMaxs + AttackExtentsUnits, 0xff, 0x80, 0, 0x14);
		}

		// 2d. The ALTERNATE hull's box, drawn only when `+0x156c` differs from `m_eHull` (`+0x1568`).
		//     Colour 255/255/64 at 10.
		if (HullKind != RetailAlternateHullKind())
		{
			FVector HullMins = FVector::ZeroVector;
			FVector HullMaxs = FVector::ZeroVector;
			RetailHullExtents(RetailAlternateHullKind(), EElysiumHullExtents::Full, HullMins, HullMaxs);
			EmitOverlayBox(GDebug10_2Box, Origin / ElysiumMove::U, HullMins, HullMaxs,
				0xff, 0xff, 0x40, 10);
		}
	}

	// --- Arm 3, `0x20000000` — the two weapon range rings ------------------------------------------
	//
	//     far  = MAX(weapon->+0x8c0, weapon->+0x8c4)
	//     near = MIN(weapon->+0x8b8, weapon->+0x8bc)
	//     unarmed -> far 2000.0, near 0.0
	//     Circle(GetOrigin(), (1,0,0), near, 32.0, 255, 255,  32, 128, 0)
	//     Circle(GetOrigin(), (1,0,0), far,  32.0, 128, 128,  16, 128, 0)
	//
	// The checklist's walk read `0x42000000` as the RADIUS; the listing's push order says it is the
	// third argument after the radius, and `CNPC_VMingXiao#123` settles it — its four values are the
	// recovered range bands 100/150/200/300 and its `0x41000000` is a constant 8.0 beside them. So
	// `0x42000000` is the ring HEIGHT and the max/min are the radii.
	if ((DebugOverlays & GDebug10_2BitWeaponRings) != 0)
	{
		float FarUnits = 0.f;
		float NearUnits = 0.f;
		if (!ActiveWeaponRangeRingsUnits(FarUnits, NearUnits))
		{
			// The `else` arm's own two literals, in retail's order: 2000.0 into the far slot and
			// 0.0 into the near one.
			FarUnits = GDebug10_2UnarmedFarUnits;
			NearUnits = GDebug10_2UnarmedNearUnits;
		}
		const FVector OriginUnits = Origin / ElysiumMove::U;
		EmitOverlayText(GDebug10_2Circle, OriginUnits,
			FString::Printf(TEXT("axis=(1.0 0.0 0.0) r=%.1f h=%.1f rgba=(255 255 32 128)"),
				NearUnits, GDebug10_2RingHeightUnits));
		EmitOverlayText(GDebug10_2Circle, OriginUnits,
			FString::Printf(TEXT("axis=(1.0 0.0 0.0) r=%.1f h=%.1f rgba=(128 128 16 128)"),
				FarUnits, GDebug10_2RingHeightUnits));
	}

	// --- Arm 4, `DAT_10924f24` — five boxes at the enemy's body target -----------------------------
	//
	// Five identical `BodyTarget(…, bNoisy = true)` calls, so retail draws five DIFFERENT points:
	// the noise is the whole reason the loop runs five times.
	if (ElysiumNpcTunables::ConVarInt(GDebug10_2CvEnemyBody) != 0)
	{
		const FElysiumEntity* const Enemy = GetEnemy();
		if (Enemy != nullptr)
		{
			for (int32 Index = 0; Index < 5; ++Index)
			{
				EmitOverlayBox(GDebug10_2Box, RetailBodyTargetUnits(Enemy), GDebug10_2Box2Mins,
					GDebug10_2Box2Maxs, 0xff, 0x20, 0x20, 1);
			}
		}
	}

	// --- Arm 5, the hint facing pair ---------------------------------------------------------------
	//
	// Two 200-unit facing boxes drawn from the hint's origin plus `m_vDefaultEyeOffset`, at two yaws
	// built from the hint's own `+0x454` and `0x102d12e0`'s node yaw:
	//
	//   types 100, 0x65, 0x283c, 0x283d     yaw1 = -hint+0x454        yaw2 = +hint+0x454
	//   type  0x27d8, leaning LEFT          yaw1 = +43.0              yaw2 = +hint+0x454
	//   type  0x27d8, not leaning left      yaw1 = -hint+0x454        yaw2 = -43.0
	//   anything else                        the whole arm is skipped
	//
	// The leaning-left arm JUMPS PAST the negation that every other arm performs, which is why its
	// first yaw is a bare +43.0 and not -43.0.
	{
		int32 HintType = INDEX_NONE;
		float HintYaw = 0.f;
		float NodeYaw = 0.f;
		FVector HintOriginUnits = FVector::ZeroVector;
		if (HintOverlayWords(HintType, HintYaw, NodeYaw, HintOriginUnits))
		{
			bool bDraw = true;
			float Yaw1 = 0.f;
			float Yaw2 = 0.f;
			switch (HintType)
			{
			case GDebug10_2HintTypeA:
			case GDebug10_2HintTypeB:
			case GDebug10_2HintTypeC:
			case GDebug10_2HintTypeD:
				Yaw1 = -HintYaw;
				Yaw2 = HintYaw;
				break;
			case GDebug10_2HintTypeLean:
				if (bLeaningLeft)
				{
					Yaw1 = GDebug10_2HintLeanYawDegrees;
					Yaw2 = HintYaw;
				}
				else
				{
					Yaw1 = -HintYaw;
					Yaw2 = -GDebug10_2HintLeanYawDegrees;
				}
				break;
			default:
				bDraw = false;
				break;
			}
			if (bDraw)
			{
				// `0x101d2f40(out, yaw)` builds a facing from a bare yaw; the pitch and roll are
				// zero, which is why only a yaw is carried.
				const FVector Dir1 = FRotator(0.f, NodeYaw + Yaw1, 0.f).Vector();
				const FVector Dir2 = FRotator(0.f, NodeYaw + Yaw2, 0.f).Vector();
				// `m_vDefaultEyeOffset` (`+0x5d60`) is an `ELYSIUM_NPC_WORD_CHAIN` row — "no
				// stored view offset; the eye point is the chain's virtual `EyePosition()`" — so
				// the offset is the zero vector here and the facing boxes sit on the hint's own
				// origin, which is what retail draws for an NPC whose offset was never set.
				const FVector FacingOriginUnits = HintOriginUnits;
				EmitOverlayBoxDirection(GDebug10_2BoxDirection, FacingOriginUnits,
					GDebug10_2FacingMins, GDebug10_2FacingMaxs, Dir1, 0xff, 0, 0, 0x32);
				EmitOverlayBoxDirection(GDebug10_2BoxDirection, FacingOriginUnits,
					GDebug10_2FacingMins, GDebug10_2FacingMaxs, Dir2, 0xff, 0, 0, 0x32);
			}
		}
	}

	// --- Arm 6, `+0x6658` — the relationship lines -------------------------------------------------
	//
	// **This arm is DEAD in retail.** `layout.md` records `+0x6658` as `m_bUnread6658`: the Troika
	// constructor (`0x1028d230`) writes a zero byte there and NO other body in the corpus touches it,
	// so the gate never opens. The arm is ported all the same because the row is a `rule` and its
	// colour ladder is the one recovered statement about what slot 404 and slot 405 mean together;
	// the port's `bUnread6658` is likewise never written non-zero.
	//
	//     for each entity within 4096.0 of WorldSpaceCenter() with mask 0x820:
	//         npc = ent->+0x9c;  if (!npc) STOP THE WHOLE WALK
	//         if (npc == this) continue
	//         switch (IRelationType(npc)) {          // slot 404
	//             1 -> (4,1,1)   2 -> (4,4,1)   3 -> (1,4,1)   4 -> (1,1,4)   default -> (4,0,0)
	//         }
	//         w = clamp((IRelationPriority(npc) + 2) * 6, 0, 0x3f)    // slot 405
	//         Line(myCentre, npc->WorldSpaceCenter(), w*r, w*g, w*b, noDepth = true, 0.0)
	//
	// The clamp is `if (v > 0x3f) v = 0x3f; else v &= (v < 0) - 1`, which is a floor at 0 spelled
	// branchlessly, so a priority below -2 contributes nothing rather than a negative channel.
	if (bUnread6658)
	{
		const FVector CentreUnits = RetailWorldSpaceCenterUnits(this);
		const TArray<FElysiumNpc*> Candidates =
			RelationshipLineCandidates(CentreUnits, GDebug10_2RelationRadiusUnits);
		for (FElysiumNpc* const Other : Candidates)
		{
			if (Other == this)
			{
				continue;
			}
			int32 WeightR = 4;
			int32 WeightG = 0;
			int32 WeightB = 0;
			switch (IRelationType(Other))
			{
			case 1: WeightR = 4; WeightG = 1; WeightB = 1; break;
			case 2: WeightR = 4; WeightG = 4; WeightB = 1; break;
			case 3: WeightR = 1; WeightG = 4; WeightB = 1; break;
			case 4: WeightR = 1; WeightG = 1; WeightB = 4; break;
			default: break;
			}
			int32 Weight = (IRelationPriority(Other) + 2) * 6;
			Weight = Weight > 0x3f ? 0x3f : (Weight < 0 ? 0 : Weight);
			EmitOverlayLine(GDebug10_2Line, CentreUnits, RetailWorldSpaceCenterUnits(Other),
				Weight * WeightR, Weight * WeightG, Weight * WeightB, true);
		}
	}

	// --- The tail: `CAI_BaseNPC::DrawDebugGeometryOverlays` (`0x10275760`), story 29c-1's body ------
	BaseDrawDebugGeometryOverlays();
}

void FElysiumNpc::VCopDrawDebugGeometryOverlays()
{
	// `0x10372f00`, 1,020 bytes. Debug-only, but the arms are the recovered statement of what a cop
	// knows about the player: the shared timed grudge, the heightened-alert window, the pursuit
	// count and its own pursuit target, all rendered as one label above its head.
	//
	// Gates, in order: `m_debugOverlays & 1`, then `m_hClosestPlayer` must RESOLVE, then the
	// collision OBB must not be degenerate. Each failure jumps straight to the Troika tail.
	if ((DebugOverlays & GDebug10_2BitText) != 0)
	{
		const FElysiumEntity* const Player =
			World != nullptr ? World->Resolve(Senses.Memory.ClosestPlayer) : nullptr;
		FVector ObbMins = FVector::ZeroVector;
		FVector ObbMaxs = FVector::ZeroVector;
		const bool bHasObb = CollisionObbExtentsUnits(ObbMins, ObbMaxs);
		const bool bDegenerate = !bHasObb
			|| (ObbMins.X == ObbMaxs.X && ObbMins.Y == ObbMaxs.Y && ObbMins.Z == ObbMaxs.Z);
		if (Player != nullptr && !bDegenerate)
		{
			// The label sits `(maxs.z - mins.z) + 8.0` above `GetAbsOrigin()`.
			const float LiftUnits = (ObbMaxs.Z - ObbMins.Z) + GDebug10_2CopLabelLiftUnits;
			FVector LabelUnits = Origin / ElysiumMove::U;
			LabelUnits.Z += LiftUnits;

			FString Label;
			switch (IRelationType(const_cast<FElysiumEntity*>(Player)))
			{
			case 1:
				// The `D_HT` arm re-runs the TROIKA `IRelationType` (`0x10299da0`) first and throws
				// the answer away — an artefact of the species override calling its own base, and
				// reproduced only as this comment because it writes nothing.
				Label = GDebug10_2CopLabelHate;
				if (CopSuspectIs(Player))
				{
					Label += GDebug10_2CopSuspect;
				}
				if (PlayerHeightenedAlert(Player))
				{
					Label += GDebug10_2CopAlert;
				}
				{
					const int32 Pursuers = PlayerCopsInPursuitCount(Player);
					if (Pursuers > 0)
					{
						// Retail asks `0x1017f770` a SECOND time for the printed number, after the
						// `> 0` test; one read is the same answer.
						Label += FString::Printf(GDebug10_2CopCount, Pursuers);
					}
				}
				if (CopPursuitPlayer() == Player)
				{
					Label += GDebug10_2CopPursuit;
				}
				break;
			case 2: Label = GDebug10_2CopLabelFear; break;
			case 3: Label = GDebug10_2CopLabelLike; break;
			case 4: Label = GDebug10_2CopLabelNeutral; break;
			default: Label = GDebug10_2CopLabelError; break;
			}

			// `"  %d  %d"` with two further cop-class statics, `DAT_1093acac` and `DAT_1093acb0`.
			// SEAM: neither is stood here and neither has a writer in this band; both read 0.
			Label += FString::Printf(GDebug10_2CopTally, 0, 0);
			EmitOverlayText(GDebug10_2Text, LabelUnits, Label);
		}
	}

	// The Troika body ALWAYS runs, whichever gate turned the label off.
	TroikaDrawDebugGeometryOverlays();
}

void FElysiumNpc::MingXiaoDrawDebugGeometryOverlays()
{
	// `0x10399d40`, 328 bytes. Gated on `m_debugOverlays & 0x20000000` — the WEAPON-RING bit, not
	// bit 0 like its siblings — so MingXiao's bands and the Troika body's two weapon rings appear
	// together and are meant to be read against each other.
	//
	// Four rings about `(1, 0, 0)` at 100, 150, 200 and 300 source units, height 8.0, colour
	// (255, 32, 32) at alpha 128, no depth test, duration 0. The four radii are the recovered range
	// bands and are the reason to port the body at all.
	if ((DebugOverlays & GDebug10_2BitWeaponRings) != 0)
	{
		const FVector OriginUnits = Origin / ElysiumMove::U;
		for (const float RadiusUnits : GDebug10_2MingXiaoRadiiUnits)
		{
			EmitOverlayText(GDebug10_2Circle, OriginUnits,
				FString::Printf(TEXT("axis=(1.0 0.0 0.0) r=%.1f h=%.1f rgba=(255 32 32 128)"),
					RadiusUnits, GDebug10_2MingXiaoRingHeightUnits));
		}
	}
	TroikaDrawDebugGeometryOverlays();
}

// -------------------------------------------------------------------------------------------------
// `CAI_BaseNPCTroika::NPCThinkDebugPre` — `0x10292500`.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::TroikaNPCThinkDebugPre()
{
	// `0x10292500`, 1,782 bytes, read from the LISTING (the decompiler reports the body DAMAGED).
	// One direct caller, `NPCThink` (`0x10292de0`), at the top of the pass.
	//
	// The ONLY state this body touches is the `+0x5b55` dump-request byte in its tail. Everything
	// else is a draw.

	// The scope-trace push: `{ "CAI_BaseNPCTroika::NPCThinkDebugPre", m_iName ? m_iName : "", "" }`,
	// with `"NULL ENTITY"` for a null `this`. The port has no scope-trace stack; the push is recorded
	// so the arm is visible and the entity it names is the one retail names.
	UE_LOG(LogElysiumNpcEnt, VeryVerbose, TEXT("CAI_BaseNPCTroika::NPCThinkDebugPre %s"),
		TargetName.IsEmpty() ? TEXT("") : *TargetName);

	const double Now = World != nullptr ? World->NowSeconds() : 0.0;

	// --- Arm 1, the criminal witness box ------------------------------------------------------------
	//
	//     if (curtime <= m_flCriminalWitnessedTimer) {
	//         x = (m_flCriminalWitnessedTimer - curtime) * 0.2;          // _DAT_10451ab4
	//         Box(GetOrigin() with z += OBBMaxs().z + 16.0,              // _DAT_10451ad0
	//             (-3,-3,-3), (3,3,3),
	//             (int)MIN(255.0, 255.0*x), (int)MIN(64.0, 64.0*x), (int)MIN(64.0, 64.0*x),
	//             1, 0.5);
	//     }
	//
	// The comparison is `FLD curtime; FCOMP timer; TEST AH,0x41; JP` — taken for BOTH "less" and
	// "equal", so a timer standing exactly at curtime still draws. Three separate per-channel clamps,
	// not one scalar clamped to 64: the checklist's walk had it as the latter.
	if (Now <= Witness.CriminalWitnessedTime)
	{
		const float Scale =
			static_cast<float>(Witness.CriminalWitnessedTime - Now) * GDebug10_2WitnessScale;
		FVector BoxUnits = Origin / ElysiumMove::U;
		FVector ObbMins = FVector::ZeroVector;
		FVector ObbMaxs = FVector::ZeroVector;
		CollisionObbExtentsUnits(ObbMins, ObbMaxs);
		BoxUnits.Z += ObbMaxs.Z + GDebug10_2CriminalLiftUnits;
		EmitOverlayBox(GDebug10_2Box, BoxUnits, GDebug10_2Box3Mins, GDebug10_2Box3Maxs,
			GDebug10_2WitnessChannel(GDebug10_2Chan255, Scale),
			GDebug10_2WitnessChannel(GDebug10_2Chan64, Scale),
			GDebug10_2WitnessChannel(GDebug10_2Chan64, Scale), 1);
	}

	// --- Arm 2, the supernatural witness box --------------------------------------------------------
	//
	// The same shape with a DIFFERENT lift (12.0, `_DAT_1044faa4`) and a different colour ladder:
	// the red channel's ceiling is 192.0 (`_DAT_1049ae14`) and the green's is 255.0, where the
	// criminal box has 255/64/64. The checklist called this "the same box with the opposite corner
	// order"; the corner order is identical — only the two STACK SLOTS the extents live in are
	// swapped, which nothing can observe.
	if (Now <= Witness.SupernaturalWitnessedTime)
	{
		const float Scale =
			static_cast<float>(Witness.SupernaturalWitnessedTime - Now) * GDebug10_2WitnessScale;
		FVector BoxUnits = Origin / ElysiumMove::U;
		FVector ObbMins = FVector::ZeroVector;
		FVector ObbMaxs = FVector::ZeroVector;
		CollisionObbExtentsUnits(ObbMins, ObbMaxs);
		BoxUnits.Z += ObbMaxs.Z + GDebug10_2SupernaturalLiftUnits;
		EmitOverlayBox(GDebug10_2Box, BoxUnits, GDebug10_2Box3Mins, GDebug10_2Box3Maxs,
			GDebug10_2WitnessChannel(GDebug10_2Chan192, Scale),
			GDebug10_2WitnessChannel(GDebug10_2Chan255, Scale),
			GDebug10_2WitnessChannel(GDebug10_2Chan64, Scale), 1);
	}

	// --- Arm 3, `DAT_1092479c` — the line of sight and the blocker ----------------------------------
	if (ElysiumNpcTunables::ConVarInt(GDebug10_2CvThinkTrace) != 0)
	{
		// 3a. `COND_SEE_PLAYER` (0x5a) plus a resolving `m_hClosestPlayer`: trace eye to eye, draw
		//     the blocker's whole-entity box when it is neither the player nor null, and draw the
		//     line — RED (255, 0, 0) when something blocked it, GREEN (0, 255, 0) when it reached
		//     the player or hit the player itself. The checklist's walk has the `DrawBBoxOverlays`
		//     call and omits the line, which is the arm's actual output.
		const FElysiumEntity* const Player =
			World != nullptr ? World->Resolve(Senses.Memory.ClosestPlayer) : nullptr;
		if (Cognition.Conditions.Has(EElysiumNpcCond::SeePlayer) && Player != nullptr)
		{
			const FVector StartUnits = EyePosition() / ElysiumMove::U;
			const FVector EndUnits = const_cast<FElysiumEntity*>(Player)->EyePosition()
				/ ElysiumMove::U;
			int32 R = 0;
			int32 G = 0xff;
			FElysiumEntity* Blocker = nullptr;
			if (EyeToEyeBlocker(Player, Blocker) && Blocker != Player)
			{
				// `CBaseEntity::DrawBBoxOverlay(hit)` — only for a non-null blocker that is not the
				// player. The whole-entity box is story 29c-1's `EmitOverlayEntityBounds`, which
				// draws THIS entity's; the blocker is another one, so the call is recorded at its
				// position with the entity it names.
				if (Blocker != nullptr)
				{
					EmitOverlayText(TEXT("CBaseEntity::DrawBBoxOverlay"),
						Blocker->Origin / ElysiumMove::U, Blocker->DebugString());
				}
				R = 0xff;
				G = 0;
			}
			EmitOverlayLine(GDebug10_2Line, StartUnits, EndUnits, R, G, 0, true);
		}

		// 3b. `COND_ENEMY_OCCLUDED` (0x48): a `"Blocked by %s"` label 20.0 units above the eye,
		//     naming `m_hEnemyOccluder` (`+0x5d90`) through `GetDebugName()` — or the literal
		//     `"**UNKNOWN**"` (`0x105477a4`) when that handle does not resolve, which the
		//     checklist's walk omits.
		if (Cognition.Conditions.Has(EElysiumNpcCond::EnemyOccluded))
		{
			FVector LabelUnits = EyePosition() / ElysiumMove::U;
			LabelUnits.Z += GDebug10_2BlockedLiftUnits;
			const FElysiumEntity* const Occluder =
				World != nullptr ? World->Resolve(Senses.Memory.EnemyOccluder) : nullptr;
			const FString Name = Occluder != nullptr
				? GDebug10_2DebugName(Occluder) : FString(GDebug10_2UnknownName);
			EmitOverlayText(GDebug10_2Text, LabelUnits,
				FString::Printf(GDebug10_2BlockedBy, *Name));
		}
	}

	// --- Arm 4, `DAT_109244c4` — the eye/ideal pair and the line between them ------------------------
	//
	//     origin = GetAbsOrigin();
	//     eye    = Weapon_ShootPosition(origin);             // slot 389, vtable +0x614
	//     ideal  = 0x10278650(eye, 0.0, 0.0);                // family Motor10's row
	//     Box(eye,   (-3,-3,-3), (3,3,3), 255,  64,  64, 1, 0.2);
	//     Box(ideal, (-2,-2,-2), (2,2,2),  64, 255,  64, 1, 0.2);
	//     Line(eye, ideal, 64, 255, 64, noDepth = true, 0.2);
	//
	// The checklist's walk has the two box sizes and the line; the colours, the alphas and the
	// durations are the listing's.
	if (ElysiumNpcTunables::ConVarInt(GDebug10_2CvThinkEye) != 0)
	{
		// Slot 389 `Weapon_ShootPosition(GetAbsOrigin())` — story 29c's body, in layer 1.
		const FVector EyeUnits = Weapon_ShootPosition(Origin) / ElysiumMove::U;
		const FVector IdealUnits = RetailStandoffAnchorUnits(EyeUnits);
		EmitOverlayBox(GDebug10_2Box, EyeUnits, GDebug10_2Box3Mins, GDebug10_2Box3Maxs,
			0xff, 0x40, 0x40, 1);
		EmitOverlayBox(GDebug10_2Box, IdealUnits, GDebug10_2Box2Mins, GDebug10_2Box2Maxs,
			0x40, 0xff, 0x40, 1);
		EmitOverlayLine(GDebug10_2Line, EyeUnits, IdealUnits, 0x40, 0xff, 0x40, true);
	}

	// --- Arm 5, `DAT_1092435c` — the mode selector --------------------------------------------------
	//
	//     v = enabled ? cvar->+0x2c : 0;
	//     if (v == 1) 0x1028e030(this); else if (v == 2) 0x1028e060(this);
	//
	// The value is an INT and not a flag: 1 and 2 select two different bodies and every other value
	// selects neither.
	RunThinkDebugPreExtra(ElysiumNpcTunables::ConVarInt(GDebug10_2CvThinkExtra));

	// --- The tail: the ring dump request -------------------------------------------------------------
	//
	//     if (this->+0x5b55) { this->+0x5b55 = 0; 0x1027efb0(this); }
	//
	// The clear happens ONLY when the byte was set — the checklist's walk has the tail clearing it
	// unconditionally. `+0x5b55` is ABSENT (story 29b), so the request never stands and the dump is
	// never asked for; `DumpDebugLogRing` records the arm.
	if (DebugRingDumpRequested())
	{
		ClearDebugRingDumpRequest();
		DumpDebugLogRing();
	}
}

bool FElysiumNpc::DebugRingDumpRequested() const
{
	// `+0x5b55`, ABSENT (story 29b: "the dump request for that debug ring"). Nothing raises it here,
	// so the tail never dumps — which is also the answer for every retail NPC until a developer asks.
	return false;
}

void FElysiumNpc::ClearDebugRingDumpRequest()
{
	// The ONE state write in the whole of `0x10292500`, and the byte it writes is absent. Recorded
	// rather than made; there is nothing to clear.
}
