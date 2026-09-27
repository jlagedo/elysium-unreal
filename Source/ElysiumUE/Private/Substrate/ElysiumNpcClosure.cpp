#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumNpcMindTypes.h"
#include "Player/ElysiumCameraShots.h"
#include "Substrate/ElysiumCameraOverride.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumSchedule.h"
#include "Visual/ElysiumActionTables.h"
#include "Visual/ElysiumEyeRig.h"

// Story 29c-1, family **Closure** — the 41 Troika-line slots of layers 0–9 whose 29c verdict is
// `present` or `mechanism`, and the family that takes the band's stub count to zero.
//
// These are NOT the story's `rule` rows. 29c read every one of these bodies and found that the port
// already runs it somewhere (`present`) or that the mechanism behind it is the engine's rather than
// VtMB's (`mechanism`). They were still generated stubs, so the slot the kernel dispatches through
// answered a TALLY instead of the port's own answer. That is what this file closes: every one of
// the 41 now reaches the port's answer, and each definition cites the retail address and names the
// function or service it goes through. No new rule is written here. Where a body IS reproduced
// rather than forwarded, the comment says why the forward was refused.
//
// The declarations are the generator's (`Substrate/ElysiumNpcKernelSlots.inl`) except for the two
// members in `Substrate/ElysiumNpcClosure.inl`; the walked prose is
// `docs/vtmb/npc-ai/shape.md` and `docs/vtmb/npc-ai/senses.md`.
//
// THREE STANDING FACTS OF THIS FAMILY.
//
//   * **Two generated names carry two slots each, and both pairs are legal C++ overloads.** Slot
//     192 is `FVector WorldSpaceCenter()` and slot 215 is `void* WorldSpaceCenter() const`; slot
//     362 is `bool FInViewCone(const FVector&)` and slot 363 is `bool FInViewCone(FElysiumEntity*)`.
//     The `WorldSpaceCenter` pair IS one retail body compiled twice (see below); the `FInViewCone`
//     pair is two DIFFERENT bodies on two class lines and the difference is the Troika override's
//     two ConVar gates and its follower bypass. Neither needed a species arm.
//   * **A refusal is counted, not tallied.** Seven rows are Source plumbing this substrate has no
//     counterpart for. Each answers retail's answer for the zero state, names the retail call, and
//     increments `ClosureRefusals` so its case can assert the seam was asked. `ElysiumStub` is for
//     a surface with no implementation; these have one.
//   * **Distances are SOURCE UNITS in retail and CENTIMETRES here.** `ElysiumMove::U` bridges them
//     at the point of use so the recovered number stays visible.

namespace
{
	// --- Retail `.rdata`, one line per constant ---------------------------------------------------

	// `_DAT_1049adfc` = 4194304.0f = 2048², the SQUARED Source-unit radius slot 583 (`0x1028d860`)
	// compares a distance against. The comparison is the decompiler's
	// `(d < c) != (d == c)` spelling of `<=`, so a body exactly on the radius IS woken.
	constexpr double GClosureWakeRadiusUnits = 2048.0;

	// `_DAT_10452dc4` = 2.0f — the seconds `CAI_BaseNPCTroika::MaintainEyeDirection` (`0x102bff20`)
	// pushes the gaze re-scan stamp `+0x5d6c` out by on every think with a live dialogue partner.
	constexpr float GClosureDialogueReScanSeconds = 2.0f;

	// `CAI_BaseNPCTroika::NPC_TranslateActivity` (`0x10295710`): `ACT_IDLE` becomes `ACT_LAUGH_IDLE`
	// under `m_bfAINPCFlags2 & 0x80000`. The two ids are carried by the port's own
	// `ClassTranslate_Troika` row and are read off it rather than spelled again here; the bit is
	// `EElysiumNpcFlag2::D_MILDLY_CRAZY` and the static assert is what pins that reading.
	constexpr uint32 GClosureLaughIdleFlag2Bit = 0x00080000u;
	static_assert(static_cast<uint32>(EElysiumNpcFlag2::D_MILDLY_CRAZY) == GClosureLaughIdleFlag2Bit,
		"0x10295710 gates on m_bfAINPCFlags2 & 0x80000");

	// `CAI_BaseNPCTroika::Cover_Troika` (`0x10297560`) and `Reload_Troika` (`0x102954b0`), and the
	// base body `Cover_Base` (`0x10274aa0`) the first of them falls through to. Every one of these
	// is a retail `Activity` id, and they are the numbers the port's `GCoverTroikaRules` /
	// `GReloadTroikaRules` / `GCoverBaseRules` rows carry.
	constexpr int32 GClosureActIdle = 1;                   // ACT_IDLE
	constexpr int32 GClosureActCover = 6;                  // ACT_COVER
	constexpr int32 GClosureActCoverMed = 7;               // ACT_COVER_MED
	constexpr int32 GClosureActCoverLow = 8;               // ACT_COVER_LOW  (retail `'\b'`)
	constexpr int32 GClosureActReloadFast = 85;            // ACT_RELOAD_FAST (retail `0x55`)
	constexpr int32 GClosureActReloadLow = 87;             // ACT_RELOAD_LOW  (retail `0x57`)
	constexpr int32 GClosureActCrunchIdle = 0x110d;        // ACT_CRUNCH_IDLE      4365, retail `'\r'`
	constexpr int32 GClosureActMidCrunchIdle = 0x1111;     // ACT_MIDCRUNCH_IDLE   4369, retail `'\x11'`
	constexpr int32 GClosureActCornerCoverIdle = 0x1118;   // ACT_CORNER_COVER_IDLE 4376, retail `'\x18'`

	// The three `CAI_Hint::m_nHintType` (`+0x5dc`) values the two activity delegates switch on.
	constexpr int32 GClosureHintTypeCoverMed = 100;
	constexpr int32 GClosureHintTypeCoverLow = 101;      // retail `0x65`
	constexpr int32 GClosureHintTypeCoverCorner = 10200;  // retail `0x27d8`

	// `m_bfAINPCFlags & 0x200` — `EElysiumNpcFlag::COWER_PATH`, the bit `Cover_Troika` short-circuits
	// on. Named through the enum at the call site; the assert is what pins the reading.
	constexpr uint32 GClosureForcedLowCoverFlagBit = 0x00000200u;
	static_assert(static_cast<uint32>(EElysiumNpcFlag::COWER_PATH) == GClosureForcedLowCoverFlagBit,
		"0x10297560 short-circuits on m_bfAINPCFlags & 0x200");

	// `CAI_Hint*` reaching slot 569 / 570, resolved through family Hints' seam.
	//
	// The generated parameter is `void*` because retail's is a `CAI_Hint*` and THERE IS NO HINT NODE
	// IN THIS SUBSTRATE (`Substrate/ElysiumNpcHints.inl` states it in full): hints are bare
	// indices, there is no store to index into, and no object this pointer could address. What makes
	// the argument resolvable anyway is that every retail call site passes ONE thing —
	// `this->m_pHintNode` (`+0x5ddc`): `NPC_EarlyTranslateActivity` (`0x10295590`) passes it at both
	// its delegate arms and `StartTask` (`0x102a1910`) at its own. So a NULL pointer is retail's
	// `param_1 == 0` arm and a non-null one can only be this NPC's own current hint, which is
	// `FElysiumNpcScheduleHost::HintNode` and is read through `FElysiumNpcBase::HintWords`.
	//
	// That seam answers false today (no store), so the hint type is `INDEX_NONE` either way and both
	// slots take their no-hint arm — which is the honest answer for a runtime with no hint graph, not
	// a guess. `INDEX_NONE` cannot collide with a real type: retail's three are 100, 101 and 10200.
	int32 ClosureHintTypeOf(const FElysiumNpc& Npc, const void* Hint, FElysiumNpcBase::FHintWords& Out)
	{
		if (Hint == nullptr)
		{
			return INDEX_NONE;
		}
		return Npc.HintWords(Npc.BaseScheduleHost.HintNode, Out) ? Out.HintType : INDEX_NONE;
	}


	// `ClassTranslate_Troika`'s single row, looked up by the body NAME the port's committed table
	// gives it. Returning the ROW rather than copying its two ids keeps
	// `Visual/ElysiumNpcActivityTables.cpp` the one statement of the rule: a regenerated table that
	// changed either id changes the slot's answer with it.
	const ElysiumActionTables::FNpcRule* ClosureTroikaLaughIdleRule()
	{
		using namespace ElysiumActionTables;
		for (const FNpcTranslationBody& Body : NpcTranslationBodies())
		{
			if (Body.Slot == ENpcSlot::ClassTranslate && Body.Name != nullptr
				&& FCString::Stricmp(Body.Name, TEXT("ClassTranslate_Troika")) == 0)
			{
				return Body.RuleCount > 0 ? &Body.Rules[0] : nullptr;
			}
		}
		return nullptr;
	}
}

// =================================================================================================
// Slots 28, 29, 30 — the three stealth-surface accessors, `0x101aa610`, `0x101aa630`, `0x101aa650`
// =================================================================================================
//
// One instruction each: `return m_flStealthVisionScalar / m_flStealthVisionCone /
// m_flStealthHearingDist`. The three words are `+0x63c4`, `+0x63c8` and `+0x63cc` and the shape map
// binds all three to `FElysiumNpcSenses` (`ElysiumNpcSenses.h`). This is the body's OWN stealth
// surface — what an observer applies to IT — which is why `FElysiumNpcSenses::IsInViewCone` takes
// the target's cone scalar as an argument rather than reading its own.

float FElysiumNpc::GetStealthVisionScalar()
{
	// `0x101aa610` -> `FElysiumNpcSenses::StealthVisionScalar` (`+0x63c4`).
	return Senses.StealthVisionScalar;
}

float FElysiumNpc::GetStealthVisionCone()
{
	// `0x101aa630` -> `FElysiumNpcSenses::StealthVisionCone` (`+0x63c8`).
	return Senses.StealthVisionCone;
}

float FElysiumNpc::GetStealthHearingDist()
{
	// `0x101aa650` -> `FElysiumNpcSenses::StealthHearingDist` (`+0x63cc`).
	return Senses.StealthHearingDist;
}

// =================================================================================================
// Slots 48 and 49 — `CBaseEntity`'s camera defaults, `0x10026810`, `0x10026830`
// =================================================================================================
//
// `return _DAT_104454c4` (0.0) and `return _DAT_104454cc` (75.0). The port recovered both while it
// was building the camera-override interface and states them, WITH these two addresses, at
// `Substrate/ElysiumCameraOverride.h`. The slots forward there rather than repeating the literal,
// so the camera layer and the kernel cannot drift apart.

// =================================================================================================
// Slot 63 — `SetOrigin(float, float, float)`, `0x10026a10`
// =================================================================================================

// =================================================================================================
// Slots 79, 80, 82 — the three RTTI/save descriptors, `0x10321670`, `0x102c5870`, `0x1028cd10`
// =================================================================================================
//
// Each is one instruction: `return &datamap_CBaseCombatCharacter_10619d10`, `return &DAT_109248d4`
// and `return &datamap_CAI_BaseNPCTroika`. They are Source's reflection surface — the datamap a
// save/restore pass and the prediction copier walk, and the `ServerClass` the networking table is
// built from.
//
// **REFUSAL.** This substrate has neither. Its save surface is `FElysiumSaveArchive`, a visitor each
// type serializes ITSELF through (`FElysiumNpcSenses::Serialize`, `FElysiumNpcFlags`, …), so there
// is no descriptor table to hand out and no address to answer with; its entity replication surface
// is nothing at all, because it is single-player and there is no client. Answering a fabricated
// pointer would be worse than answering none: the one legitimate consumer of a datamap pointer is a
// walker, and a walker handed a lie corrupts a save.

void* FElysiumNpc::GetServerClass()
{
	// `0x102c5870` -> `&DAT_109248d4`, `CAI_BaseNPCTroika`'s `ServerClass` record. REFUSAL: this
	// runtime has no networked entity table.
	++ClosureRefusals.ServerClass;
	return nullptr;
}

void* FElysiumNpc::GetDataDescMap()
{
	// `0x1028cd10` -> `&datamap_CAI_BaseNPCTroika`. REFUSAL, as slot 79.
	//
	// The datamap ITSELF is not lost — `docs/vtmb/npc-kernel/layout.md` is the whole of it, read out
	// of the image, and `Substrate/ElysiumNpcKernelShape.cpp` carries it as census rows with
	// `ElysiumNpcKernelShapeMap.cpp` binding every offset to the member that holds it. What has no
	// counterpart is a RUNTIME pointer to a `datamap_t`.
	++ClosureRefusals.DataDescMap;
	return nullptr;
}

// =================================================================================================
// Slot 88 — the change tracker, `0x10026b50`
// =================================================================================================

// =================================================================================================
// Slot 102 — `Physics_TraceEntity`, `0x100ab450`
// =================================================================================================

// =================================================================================================
// Slot 184 — `MakeTracer`, `0x10267260`
// =================================================================================================

// =================================================================================================
// Slots 192 and 215 — `WorldSpaceCenter`, `0x10027160` and `0x100b4c30`
// =================================================================================================
//
// **ONE BODY, COMPILED TWICE.** The decompiled C of the two is identical instruction for
// instruction except for how the answer leaves: `0x10027160` (slot 192) writes three floats through
// the hidden struct-return pointer, `0x100b4c30` (slot 215) returns the pointer itself. Both:
//
//     tmp  = allocTempVector();                       // ring DAT_109f0cc0, index (i+1) & 0x7f
//     out  = allocTempVector();                       // a SECOND entry, advanced again
//     tmp  = m_Collision.mins + (maxs - mins) * 0.5;  // +0x274 / +0x280, _DAT_104454d0 = 0.5
//     if (solid && solidType != 2 && solidType != 0 && GetCollisionAngles() != vec3_angle)
//         VectorTransform(tmp, EntityToWorldTransform(), out);   // m_Collision vfunc +0x28
//     else
//         out = GetCollisionOrigin() + tmp;                      // m_Collision vfunc +0x20
//
// So it is the collision OBB's centre: the axis-aligned fast path is the origin plus the local
// midpoint, and a rotated box takes the full transform. That is the same quantity Unreal's
// `UPrimitiveComponent::Bounds` carries, which is 29c's `mechanism` verdict.
//
// The port already decided its answer for this slot and wrote it down: `ElysiumCameraShots::
// SurroundingBounds(Entity).GetCenter()`, cited as "the port's `WorldSpaceCenter`" at
// `Substrate/ElysiumBareEntityCameraSource.h`, and it is what the `Center` shot anchor, the mode-3
// follow think and the held-use maintenance all read. Its ladder — the standing skeletal body's
// bounds, else the embodiment's use box, else VtMB's own standing hull on the origin — is retail's
// collision prop as closely as a body-less state can answer it. Both slots go through it, so there
// is exactly one bounds accessor in the runtime.
//
// This is also the body `FElysiumNpc::SpeciesWorldSpaceCenter()` (family Geometry) answers for
// every class.

// =================================================================================================
// Slot 225 — `VPhysicsDestroyObject`, `0x100b5040`
// =================================================================================================

// =================================================================================================
// Slot 333 — `MaintainEyeDirection(float)`, `0x102bff20`
// =================================================================================================

void FElysiumNpc::MaintainEyeDirection(float DeltaSeconds)
{
	// `CAI_BaseNPCTroika::MaintainEyeDirection`, 182 bytes, filled by 63 classes — every shipped
	// `CNPC_V*`. Four arms, in this order:
	//
	//   1. `if (m_flPlayerDist < _DAT_10483aac && (m_blinkTimer -= dt) < 0.0)
	//          { vfunc 0x450 /* CBaseFlex::Blink */;
	//            m_blinkTimer = RandomFloat(m_flMinBlink, m_flMaxBlink); }`
	//   2. `if (m_hDialogPartner resolves live) m_flNextEyeLookTime = curtime + 2.0;`  (`+0x5d6c`)
	//   3. `thunk_FUN_102c0010(this)` — the disposition fidget driver.
	//   4. `CAI_BaseNPC::MaintainEyeDirection(dt)` (`0x1026b810`), unchanged.
	//
	// **The port runs all four, and three of them above this tier.** `AElysiumMapActor::TickGaze`
	// drives the whole eye path once per frame per DRAWN body, because retail runs it per drawn
	// model; arms 1 and 3 need the blink schedule and the fidget grid, and arm 4 needs the head
	// frame and the disposition's eye tuning, none of which a substrate NPC can reach. So:
	//
	//   * arm 1 is `FElysiumBlinkSchedule` + `ElysiumEyes::BlinkWeight` in `Visual/ElysiumEyePass.cpp`.
	//     Its gate — `m_flPlayerDist` (`+0x6264`) against the unread `_DAT_10483aac` — IS reachable
	//     here and is evaluated below, so the refusal is recorded only when retail would have blinked.
	//     `m_blinkTimer` (`+0x6570`) is an `ELYSIUM_NPC_WORD_CHAIN` onto the eye pass; `m_flMinBlink`
	//     / `m_flMaxBlink` (`+0x64d8` / `+0x64dc`) are `StanceTuning`'s blink floor and ceiling.
	//   * arm 3 is `FElysiumCombatCharacter::FidgetStep` / `NextFidgetTime`, the saccade layer
	//     underneath `TickGaze`.
	//   * arm 4 is `FElysiumCombatCharacter::TickGaze`, which cites `0x1026b810` in its own header
	//     comment and reproduces the whole selection cascade.
	//
	// Arm 2 is the one write of the WRAPPER itself, it is reachable, and it is what makes the
	// cascade's fall-through terminal for the length of a conversation, so it is performed here.
	// `TickGaze`'s dialogue arm writes the same stamp to the same value, which is idempotent — and
	// that is the retail shape too: `0x102bff20` writes it and then calls the base body, which is
	// where the stamp is read.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;

	// Arm 1's gate, retail's own order: the distance test first, and the countdown only inside it.
	// An NPC far from the player does not blink AND does not run its timer down, which is why
	// walking up to a distant body does not fire a backlog. `+0x6264` is
	// `FElysiumNpcMemory::ClosestPlayerDistanceCm`.
	if (Senses.Memory.ClosestPlayerDistanceCm < ElysiumEyes::BlinkPlayerDistance)
	{
		++ClosureRefusals.BlinkCadence;
	}

	// Arm 2 — `+0x5d6c m_flNextEyeLookTime := curtime + 2.0` while a partner is live.
	if (HasLiveDialogPartner())
	{
		NextEyeLookTime = static_cast<float>(Now) + GClosureDialogueReScanSeconds;
	}

	// Arms 3 and 4, named rather than called: their inputs are the engine tier's.
	(void)DeltaSeconds;
	++ClosureRefusals.EyeFidgetDriver;
	++ClosureRefusals.BaseEyeMaintainer;
}

// =================================================================================================
// Slot 346 — `SetPoseParameter(int, float, bool)`, `0x1032fc50`
// =================================================================================================

// =================================================================================================
// Slot 355 — the feed/grapple end output, `0x1026cf90`
// =================================================================================================

// =================================================================================================
// Slots 362 and 363 — `FInViewCone`, `0x10326a20` and `0x102b4540`
// =================================================================================================
//
// **Two different bodies on two class lines, not one body twice.**
//
// Slot 362 (`CAI_BaseNPC`, `0x10326a20`, 134 bytes, 71 classes) is a scope-trace push, a call to
// `thunk_FUN_103268e0(this, point, m_flFieldOfView)` and a pop. `0x103268e0` ConVar-gates between
// `FinViewCone2d` (`0x103261f0`) and `FinViewCone3dNew` (`0x103264d0`); the 3-D branch is the exact
// address `FElysiumNpcSenses::IsInViewCone`'s header comment cites, so the formula is already
// ported. The 2-D branch is a ConVar this runtime does not carry and is **unrecovered**.
//
// Slot 363 (`CAI_BaseNPCTroika`, `0x102b4540`) is the Troika override and is a different shape: a
// null guard, the two sense-off ConVars (`DAT_10924fba` `npc_ignore_senses`, `DAT_10924fb9`
// `npc_ignore_player` against the target's `+0xa8` player flag), then the follower bypass — if
// `GetFollowerBoss()` (slot 293, `vtable +0x494`) resolves AND equals `m_hClosestPlayer` AND the
// target's `+0x98` object carries a set byte at `+0x6279`, answer TRUE at any angle — and only then
// the base body at the target's eye. The port reproduces the two gates and the fallthrough and
// names the bypass a seam in its own comment.

bool FElysiumNpc::FInViewCone(FElysiumEntity* Candidate)
{
	// `0x102b4540` -> `FElysiumNpcSenses::IsInViewCone(Npc, target)`, the Troika override, which is
	// where the two ConVar gates and the follower bypass live. The null guard is retail's own first
	// line (`if (param_1 == NULL) return false`) and is kept here so the forward cannot be reached
	// with a null reference.
	return Candidate != nullptr && FElysiumNpcSenses::IsInViewCone(*this, *Candidate);
}

// =================================================================================================
// Slot 376 — `NPC_TranslateActivity`, `0x10295710`
// =================================================================================================

int32 FElysiumNpc::NPC_TranslateActivity(int32 Activity)
{
	// `CAI_BaseNPCTroika::NPC_TranslateActivity`, and the whole of it:
	//
	//     if (act == ACT_IDLE && (m_bfAINPCFlags2 & 0x80000) == 0x80000) act = 0x105d;
	//     return act;
	//
	// `0x105d` is 4189, `ACT_LAUGH_IDLE`; `0x80000` is `EElysiumNpcFlag2::D_MILDLY_CRAZY`.
	//
	// The rule is already committed, as the one row of `ClassTranslate_Troika` in
	// `Visual/ElysiumNpcActivityTables.cpp` (cited there by this exact address), and 19 of the
	// census's classes inherit that body. The slot reads the row's own `FromId` and `ToId` rather
	// than spelling 1 and 4189 again, so the table stays the single statement of the rule and a
	// regeneration that changed either id changes the slot with it.
	//
	// **Why the row is read rather than `ElysiumActionTables::NpcTranslate` called:** that walker is
	// string-keyed (it resolves `ACT_*` NAMES, because the port's clip vocabulary is names) while
	// this slot's retail ABI is an activity NUMBER in and an activity NUMBER out. Round-tripping
	// number -> name -> number would add two lookups and a failure mode for a one-row body.
	const ElysiumActionTables::FNpcRule* Row = ClosureTroikaLaughIdleRule();
	if (Row != nullptr && Activity == Row->FromId
		&& NpcFlags.Has(EElysiumNpcFlag2::D_MILDLY_CRAZY))
	{
		return Row->ToId;
	}
	return Activity;
}

// =================================================================================================
// Slot 406 — `GetStateName(NPC_STATE)`, `0x1027e740`
// =================================================================================================

// =================================================================================================
// Slots 412–415 — the four think-clock `Last` mirrors, `0x101aa6d0`..`0x101aa730`
// =================================================================================================
//
// One instruction each: `return m_flLastUpdateThink / m_flLastNormalThink / m_flLastMoveThink /
// m_flLastAIThink`. `+0x6254`, `+0x6258`, `+0x625c`, `+0x6260`, all bound to
// `FElysiumNpcScheduleHost` by the shape map.
//
// The port carries them as DOUBLES, as it carries every absolute stamp, and retail's slot is a
// `float`. The narrowing is at the slot boundary and not in the store, which is the right place for
// it: the interval a `Calc*` reports is computed in double and only the reported answer rounds.

float FElysiumNpc::GetLastUpdateThink()
{
	// `0x101aa6d0` -> `FElysiumNpcScheduleHost::LastUpdate` (`+0x6254`).
	return static_cast<float>(ScheduleHost.LastUpdate);
}

float FElysiumNpc::GetLastNormalThink()
{
	// `0x101aa6f0` -> `FElysiumNpcScheduleHost::LastNormal` (`+0x6258`).
	return static_cast<float>(ScheduleHost.LastNormal);
}

float FElysiumNpc::GetLastMoveThink()
{
	// `0x101aa710` -> `FElysiumNpcScheduleHost::LastMove` (`+0x625c`).
	return static_cast<float>(ScheduleHost.LastMove);
}

float FElysiumNpc::GetLastAIThink()
{
	// `0x101aa730` -> `FElysiumNpcScheduleHost::LastAI` (`+0x6260`).
	return static_cast<float>(ScheduleHost.LastAI);
}

// =================================================================================================
// Slots 416 and 417 — `m_bForceFrequentThink`, `0x101aa750` and `0x101aa770`
// =================================================================================================

void FElysiumNpc::SetForceFrequentThink(bool bEnabled)
{
	// `0x101aa750`: `m_bForceFrequentThink = param_1` (`+0x63f0`), and nothing else. It is the
	// word's ONLY writer in the image, which is what `ElysiumNpc.h` already records beside the
	// member.
	bForceFrequentThink = bEnabled;
}

bool FElysiumNpc::GetForceFrequentThink()
{
	// `0x101aa770`: `return m_bForceFrequentThink` (`+0x63f0`).
	return bForceFrequentThink;
}

// =================================================================================================
// Slot 439 — `SelectFailSchedule(int, int, AI_TaskFailureCode_t)`, `0x1028abe0`
// =================================================================================================

// =================================================================================================
// Slot 446 — `GetScheduleOfType(int)`, `0x102cc260`
// =================================================================================================

// =================================================================================================
// Slot 462 — `ShouldGoToIdleState()`, `0x101aa6b0`
// =================================================================================================

bool FElysiumNpc::ShouldGoToIdleState()
{
	// `0x101aa6b0`: `return m_bGoToIdleState` (`+0x63fc`), one instruction.
	return bGoToIdleState;
}

// =================================================================================================
// Slot 464 — `GetState()`, `0x101a6720`
// =================================================================================================

// =================================================================================================
// Slot 476 — `HearingSensitivity()`, `0x101aa5f0`
// =================================================================================================

float FElysiumNpc::HearingSensitivity()
{
	// `0x101aa5f0`: `return m_flHearingSensitivity` (`+0x63c0`). The shape map binds it to
	// `FElysiumNpcPerception::HearingScalar` — the RESOLVED channel, which
	// `InitPerceptionDistances` (`0x1028fb70`) fills once at Activate from the authored key and the
	// rulebook, not the raw keyfield.
	return Senses.Perception.HearingScalar;
}

// =================================================================================================
// Slot 480 — `ShouldChooseNewEnemy()`, `0x10279d00`
// =================================================================================================

// =================================================================================================
// Slot 561 — `GatherAttackConditions(CBaseEntity*, float)`, `0x1026dd10`
// =================================================================================================

// =================================================================================================
// Slots 569 and 570 — the cover and reload activity delegates, `0x10297560`, `0x102954b0`
// =================================================================================================
//
// **These two are REPRODUCED rather than forwarded, and the reason is a divergence in the target.**
//
// 29c's targets are the port's `GCoverTroikaRules` and `GReloadTroikaRules`
// (`Visual/ElysiumNpcActivityTables.cpp`), and for `Reload_Troika` the table's four rows ARE the
// retail body. For `Cover_Troika` they are not, in one arm: retail's first line is
//
//     if ((m_bfAINPCFlags & 0x200) == 0x200) return ACT_COVER_LOW;   // unconditional, no probe
//
// while the committed table models it as `{ ForcedLowCover, 101, ForceCoverContext }` — a row that
// sets the cover CONTEXT to 101 and then lets the following rows run, so the answer becomes
// `ACT_CRUNCH_IDLE` when the body authors it and falls through `Cover_Base` to `ACT_IDLE` when it
// does not. That is a different answer for a set flag, so forwarding would have wired the slot to
// something retail does not do. The table is not this family's file; the divergence is REPORTED
// rather than edited, and the bodies below answer retail.
//
// Both bodies probe the model with `SelectWeightedSequence` — `thunk_FUN_10295460` (the Troika
// line's stat-filtered twin) and `CBaseAnimating::SelectWeightedSequence` — and take the first
// candidate the body can play. The port's probe is `FElysiumAnimatingOverlay::SelectWeightedSequenceForActivity`
// (family Facing), which is a SEAM answering -1: this substrate resolves activities by name and
// stands no sequence index at the kernel tier. Every probe therefore misses today, which means
// slot 569 answers `ACT_COVER_LOW` under the flag and `ACT_IDLE` otherwise, and slot 570 answers
// `ACT_RELOAD_FAST`. Those are retail's own answers for a body that authors no cover or reload
// clips; when the seam grows a sequence table the arms above it start firing without another edit.
//
// The hint argument is retail's `CAI_Hint*`. THERE IS NO HINT NODE IN THIS SUBSTRATE (family Hints
// states this in full): hints are bare indices and there is no store to index into, so the generated
// `void*` has no object to point at and the only value this runtime can hand these slots is null —
// which takes retail's `param_1 == 0` arm. The cover context is read through
// `FElysiumNpcBase::HintWords`, family Hints' seam for exactly this, so the day the store exists both
// slots read it without another edit.

int32 FElysiumNpc::GetCoverActivity(void* Hint)
{
	// `CAI_BaseNPCTroika::Cover_Troika` (`0x10297560`), 161 bytes, 64 classes, slot 569.
	//
	//     if (m_bfAINPCFlags & 0x200) return ACT_COVER_LOW;                        // COWER_PATH
	//     if (hint) switch (hint->m_nHintType) {                                   // +0x5dc
	//       case 100:   if (SelectWeightedSequence(ACT_MIDCRUNCH_IDLE)    != -1) return it;
	//       case 101:   if (SelectWeightedSequence(ACT_CRUNCH_IDLE)       != -1) return it;
	//       case 10200: if (SelectWeightedSequence(ACT_CORNER_COVER_IDLE) != -1) return it; }
	//     return Cover_Base(hint);                                                 // 0x10274aa0
	if (NpcFlags.Has(EElysiumNpcFlag::COWER_PATH))
	{
		return GClosureActCoverLow;
	}

	FHintWords Words;
	const int32 HintType = ClosureHintTypeOf(*this, Hint, Words);
	if (HintType == GClosureHintTypeCoverMed
		&& SelectWeightedSequenceForActivity(GClosureActMidCrunchIdle) != INDEX_NONE)
	{
		return GClosureActMidCrunchIdle;
	}
	if (HintType == GClosureHintTypeCoverLow
		&& SelectWeightedSequenceForActivity(GClosureActCrunchIdle) != INDEX_NONE)
	{
		return GClosureActCrunchIdle;
	}
	if (HintType == GClosureHintTypeCoverCorner
		&& SelectWeightedSequenceForActivity(GClosureActCornerCoverIdle) != INDEX_NONE)
	{
		return GClosureActCornerCoverIdle;
	}

	// `CAI_BaseNPC::Cover_Base` (`0x10274aa0`), the body the Troika line falls through to. It is not
	// a slot of its own — 13 classes carry it AT slot 569 and the Troika override calls it
	// statically — so it is inlined here rather than given a second symbol. The port's
	// `GCoverBaseRules` encodes the identical four rows.
	if (HintType == GClosureHintTypeCoverMed
		&& SelectWeightedSequenceForActivity(GClosureActCoverMed) != INDEX_NONE)
	{
		return GClosureActCoverMed;
	}
	if (HintType == GClosureHintTypeCoverLow
		&& SelectWeightedSequenceForActivity(GClosureActCoverLow) != INDEX_NONE)
	{
		return GClosureActCoverLow;
	}
	if (SelectWeightedSequenceForActivity(GClosureActCover) != INDEX_NONE)
	{
		return GClosureActCover;
	}
	return GClosureActIdle;
}

int32 FElysiumNpc::GetReloadActivity(void* Hint)
{
	// `CAI_BaseNPCTroika::Reload_Troika` (`0x102954b0`), 141 bytes, 64 classes, slot 570.
	//
	//     if (hint) switch (hint->m_nHintType) {                                   // +0x5dc
	//       case 100: if (SelectWeightedSequence(ACT_RELOAD_LOW) != -1) return ACT_RELOAD_LOW;
	//                 if (SelectWeightedSequence(ACT_MIDCRUNCH_IDLE) != -1) return it;
	//       case 101: if (SelectWeightedSequence(ACT_RELOAD_LOW) != -1) return ACT_RELOAD_LOW;
	//                 if (SelectWeightedSequence(ACT_CRUNCH_IDLE)    != -1) return it; }
	//     return ACT_RELOAD_FAST;                                                  // 0x55
	//
	// Note what it does NOT do: there is no `Reload_Base` fall-through and no `ACT_RELOAD` (84)
	// anywhere in the body. The Troika line answers `ACT_RELOAD_FAST` for every miss, where the base
	// body (`0x10274820`, 13 classes) answers `ACT_RELOAD`. The port's table row agrees.
	//
	// The crunch-idle answers are the rows the port's table marks `RewriteThroughTranslator`: retail
	// returns the raw id and the CALLER re-enters the whole translator with it. That re-entry is the
	// caller's, not this slot's, and this slot returns the id retail returns.
	FHintWords Words;
	const int32 HintType = ClosureHintTypeOf(*this, Hint, Words);
	if (HintType == GClosureHintTypeCoverMed || HintType == GClosureHintTypeCoverLow)
	{
		if (SelectWeightedSequenceForActivity(GClosureActReloadLow) != INDEX_NONE)
		{
			return GClosureActReloadLow;
		}
		const int32 Crunch = HintType == GClosureHintTypeCoverMed ? GClosureActMidCrunchIdle : GClosureActCrunchIdle;
		if (SelectWeightedSequenceForActivity(Crunch) != INDEX_NONE)
		{
			return Crunch;
		}
	}
	return GClosureActReloadFast;
}

// =================================================================================================
// Slot 583 — the proximity wake, `0x1028d860`
// =================================================================================================

void FElysiumNpc::Slot583(const FVector& PointCm)
{
	// `CAI_BaseNPCTroika::FUN_1028d860`, the per-NPC half of the world's wake broadcast:
	//
	//     Vector o = GetAbsOrigin();                       // vtable +0x364, slot 217
	//     float d2 = (p - o).LengthSqr();
	//     if (d2 <= _DAT_1049adfc /* 2048*2048 */) ResetThinkTimers();   // vtable +0x998, slot 614
	//
	// The comparison is the decompiler's `(d2 < c) != (d2 == c)` spelling of `<=`, so a body EXACTLY
	// on the radius is woken. The broadcast that drives it is `0x1028d820`, which walks every entity
	// with a Troika pointer and dispatches this slot.
	//
	// -> `FElysiumEntityWorld::WakeNpcsNear` is 29c's target and is the BROADCAST half: it already
	// carries this test, inline, citing this same address. It is a loop over every NPC, so it cannot
	// be forwarded to from one NPC without waking the others; what lands here is its loop body, over
	// the same radius, ending in the same `ResetThinkTimers`. The two are asserted equal by name in
	// `Elysium.Substrate.NpcKernelClosure.Slot583ProximityWake`.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	const double RadiusCm = GClosureWakeRadiusUnits * ElysiumMove::U;
	if (FVector::DistSquared(Origin, PointCm) <= RadiusCm * RadiusCm)
	{
		ResetThinkTimers(Now);
	}
}

// =================================================================================================
// Slot 584 — the full think-stamp reset, `0x1028d910`
// =================================================================================================

void FElysiumNpc::Slot584(int32 Unused)
{
	// `CAI_BaseHumanoid#584` and `CAI_ExpressiveNPC#584` fill this slot with `FUN_10260dc0` instead
	// (a tail jump through the Expresser at `+0x5f48`). Neither class has an instance, so 0019
	// story 5 step 1 removed that species prologue; the body below is the Troika line's.
	//
	//     ResetThinkTimers();                              // vtable +0x998, slot 614
	//     m_flLastThink = m_flLastUpdateThink = m_flLastNormalThink
	//                   = m_flLastMoveThink = m_flLastAIThink = gpGlobals->curtime;
	//
	// -> `FElysiumNpc::ResetAllThinkStamps(Now)`, which `ElysiumNpc.h` already names slot 584 and
	// this address for: slot 614 first (the four `Next` stamps and `m_flNextThink` to now), then
	// every `Last` mirror to now. Its two live callers are `SetAIEnabled(true)`'s broadcast and
	// `TASK_WAIT_PVS`'s completion.
	//
	// **`m_flLastThink` (`+0x0178`) is the one word of the five with no port member.** It is a
	// `CBaseEntity` word below 29b's band and `ElysiumNpcKernelShapeMap.cpp` carries no row for it;
	// nothing in layers 0–9 reads it, so the reset has nothing to write. Stated here rather than
	// invented.
	//
	// The `int` argument reaches no instruction in the retail body.
	(void)Unused;
	ResetAllThinkStamps(World != nullptr ? World->NowSeconds() : 0.0);
}

// =================================================================================================
// Slot 586 — `GetBestSeeUnknown()`, `0x101aa5d0`
// =================================================================================================

FElysiumEntityHandle FElysiumNpc::GetBestSeeUnknown()
{
	// `0x101aa5d0`: `*param_1 = m_hBestSeeUnknown` (`+0x6088`), the EHANDLE written out through the
	// struct-return pointer. -> `FElysiumNpcMemory::BestSeeUnknown`, the shape map's binding, which
	// the unknown-sighting arm of the sense pass writes.
	return Senses.Memory.BestSeeUnknown;
}

// =================================================================================================
// Slot 593 — the target-lead defaults, `0x1029a070`
// =================================================================================================

void FElysiumNpc::Slot593()
{
	// `CNPC_VTzimisce` overrides this slot (`FElysiumNpcTzimisce`, `0x103b9180`): it calls THIS body
	// directly (`thunk_FUN_1029a070`, spelled `FElysiumNpc::Slot593()`) and then overwrites all five
	// words, so a Tzimisce's lead defaults are never the ones below.
	// `CAI_BaseNPCTroika::FUN_1029a070`, 48 bytes, five immediate stores and nothing else:
	//
	//     +0x655c = 0x3dcccccd = 0.1f     m_flTargetLeadMin
	//     +0x6560 = 0x3f800000 = 1.0f     m_flTargetLeadMax
	//     +0x6564 = 0x42480000 = 50.0f    m_flTargetLeadCurrentWeight
	//     +0x6568 = 0x42480000 = 50.0f    m_flTargetLeadPredictedWeight
	//     +0x656c = 0x3c23d70a = 0.01f    m_flTargetLeadWeightScale
	//
	// The values are IMMEDIATES, not `.rdata` reads, so they are transcribed from the instruction
	// stream. All five words are bound to `FElysiumNpc` by the shape map. Six classes dispatch this
	// slot; none overrides it. Its `int` argument reaches no instruction.
	//
	// The two weights are a 50/50 split between the target's current position and its predicted one,
	// and the scale is what turns them into a fraction — which is why they are 50 and 0.01 rather
	// than 0.5: retail divides by 100 at the point of use, not here.
	TargetLeadMin = 0.1f;
	TargetLeadMax = 1.0f;
	TargetLeadCurrentWeight = 50.0f;
	TargetLeadPredictedWeight = 50.0f;
	TargetLeadWeightScale = 0.01f;
}
