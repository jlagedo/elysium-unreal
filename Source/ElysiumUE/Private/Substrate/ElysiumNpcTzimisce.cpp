#include "Substrate/ElysiumNpcTzimisce.h"

#include "ElysiumAnimationIntent.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSessionSubsystem.h"
#include "ElysiumStub.h"
#include "ElysiumWorldServices.h"
#include "Player/ElysiumCameraShots.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumDisciplines.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumHint.h"
#include "Substrate/ElysiumInterestingPlace.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumLaw.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcDialogue.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcAnim10Shared.h"
#include "Substrate/ElysiumNpcConditions10Shared.h"
#include "Substrate/ElysiumNpcConditionsBodiesShared.h"
#include "Substrate/ElysiumNpcDamage2Shared.h"
#include "Substrate/ElysiumNpcDebug10Shared.h"
#include "Substrate/ElysiumNpcDebugShared.h"
#include "Substrate/ElysiumNpcHintsShared.h"
#include "Substrate/ElysiumNpcLifecycle2Shared.h"
#include "Substrate/ElysiumNpcLifecycle2_2Shared.h"
#include "Substrate/ElysiumNpcMotorShared.h"
#include "Substrate/ElysiumNpcPositions2Shared.h"
#include "Substrate/ElysiumNpcPrecache10Shared.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcSoundsShared.h"
#include "Substrate/ElysiumNpcSpecies2Shared.h"
#include "Substrate/ElysiumNpcState_2Shared.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumReactions.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumRetailHullTable.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumWeaponClasses.h"
#include "Visual/ElysiumActionTables.h"

// --- File-scope helpers moved with this class's bodies (story 5 step 4) ---

namespace
{
	constexpr int32 GAnim10ActIdleBody = 0xfc;           // 252, `m_bHeavyBodyTarget` NON-zero
	constexpr int32 GAnim10ActIdleBodyL = 0xfd;          // 253, `m_bHeavyBodyTarget` ZERO
	constexpr int32 GAnim10ActWalkBody = 0xfe;           // 254
	constexpr int32 GAnim10ActWalkBodyL = 0xff;          // 255
	// `CNPC_VTzimisce::GetEventName` (`0x103bdd10`), anim-event ids 2..8. The body `strcpy`s the
	// literal into the caller's buffer; ids outside the range fall through to
	// `CBaseAnimating::GetEventName`, which is what a null answer means here.
	const TCHAR* const GNpcKernelDebugTzimisceEventNames[] = {
		TEXT("START_IDLE"),      // 2, 0x1065c87c
		TEXT("START_FIDGET"),    // 3, 0x1065c86c
		TEXT("START_RUN"),       // 4, 0x1065c860
		TEXT("START_LANDHARD"),  // 5, 0x1065c84c
		TEXT("START_ATTACK"),    // 6, 0x1065c83c
		TEXT("START_ATTACKBIG"), // 7, 0x1065c828
		TEXT("START_POUNCE"),    // 8, 0x1065c818
	};
	constexpr TCHAR GDebug10FmtTzimisceBody[] = TEXT("Body - %5.1f|%5.1f|%s"); // 0x1065c904
	// `_DAT_104454c4` = 0.0, the floor every clamp in this band compares against.
	constexpr float GDebug10Zero = 0.f;
	// `_DAT_1047a3ac` = 160.0 — the distance the Tzimisce body line must exceed before it latches.
	constexpr float GDebug10TzimisceLatchUnits = 160.f;
	// `_DAT_1093d01c` and `DAT_1093cd70`: the cross-NPC latch pair `CNPC_VTzimisce#124` writes. They
	// are CLASS statics in retail — every Tzimisce in the map shares one distance and one schedule
	// name — so they are file statics here and not per-instance state. That is the recovery.
	float GDebug10TzimisceLatchDistance = 0.f;
	FString GDebug10TzimisceLatchSchedule;
	constexpr float GYawTzimisceIdle = ElysiumNpcTunables::Five;
	constexpr float GYawTzimisceDefault = ElysiumNpcTunables::YawSpeedTzimisceDefault;
	// `AngleVectors` `0x10139550` — forward, right and up for Source `[pitch yaw roll]`, each
	// returned in THIS world's axes (`bsp.source_to_unreal` negates Y). Family Facing carries the
	// forward-only form in its own anonymous namespace; `CNPC_VTzimisce`'s aim override needs all
	// three, so the full routine is written here.
	void RetailAngleVectors(const FVector& SourceAngles, FVector& OutForward, FVector& OutRight,
		FVector& OutUp)
	{
		const float Pitch = FMath::DegreesToRadians(static_cast<float>(SourceAngles.X));
		const float Yaw = FMath::DegreesToRadians(static_cast<float>(SourceAngles.Y));
		const float Roll = FMath::DegreesToRadians(static_cast<float>(SourceAngles.Z));
		const float Sp = FMath::Sin(Pitch);
		const float Cp = FMath::Cos(Pitch);
		const float Sy = FMath::Sin(Yaw);
		const float Cy = FMath::Cos(Yaw);
		const float Sr = FMath::Sin(Roll);
		const float Cr = FMath::Cos(Roll);
		// Source's own three rows, with Y negated on the way out.
		OutForward = FVector(Cp * Cy, -(Cp * Sy), -Sp);
		OutRight = FVector(-Sr * Sp * Cy + Cr * Sy, -(-Sr * Sp * Sy - Cr * Cy), -Sr * Cp);
		OutUp = FVector(Cr * Sp * Cy + Sr * Sy, -(Cr * Sp * Sy - Sr * Cy), Cr * Cp);
	}
	const TCHAR* const GSpiderchickFootsteps[] = {   // 0x106530fc, to 0x18 — six
		TEXT("character/monster/spiderchick/spi_footstep_indiv_1.wav"),
		TEXT("character/monster/spiderchick/spi_footstep_indiv_2.wav"),
		TEXT("character/monster/spiderchick/spi_footstep_indiv_3.wav"),
		TEXT("character/monster/spiderchick/spi_footstep_indiv_4.wav"),
		TEXT("character/monster/spiderchick/spi_footstep_indiv_5.wav"),
		TEXT("character/monster/spiderchick/spi_footstep_indiv_6.wav"),
	};
	const TCHAR* const GSpiderchickSwishes[] = {     // 0x10653114, to 0xc — three
		TEXT("character/monster/spiderchick/spi_attack_swish_1.wav"),
		TEXT("character/monster/spiderchick/spi_attack_swish_2.wav"),
		TEXT("character/monster/spiderchick/spi_attack_swish_3.wav"),
	};
	const TCHAR* const GTzimisceWeapon = TEXT("item_w_tzimisce_melee");
	// `_DAT_10457f60` — `CNPC_VTzimisce`'s answer for task distance sentinel -1000001. UNRECOVERED.
	constexpr float GScheduleTzimisceTaskDistance = 0.0f;
	// `CNPC_VTzimisce::vfunc487` `0x103b9f10` draws `RandomFloat(0x3f000000, 0x3f400000)` — and,
	// unlike both bodies above, writes NO squad copy.
	constexpr float GSoundsTzimisceSoundWaitMin = 0.5f;
	constexpr float GSoundsTzimisceSoundWaitMax = 0.75f;
	// `0x103be0b0`'s carry timer, `curtime + RandomFloat(7.5, 10.0)` off `0x40f00000` / `0x41200000`.
	constexpr float TzimisceBodyTimerMin = 7.5f;
	constexpr float TzimisceBodyTimerMax = 10.0f;
	// `0x103be3d0`'s initial best distance — 1025 units SQUARED, spelled as the literal
	// `1050625.0` the decompiler folded. Family Bosses' `0x10381e90` uses the same number, which is
	// what makes the two bodies "the same search over a different bone table".
	constexpr float GrabBoneRangeSqUnits = 1050625.0f;
	// `0x103be8e0`'s two grab-distance bounds and `0x103bea90`'s nudge, all off the same two cells
	// families Bosses and Damage read as the +-20 pickup cone.
	constexpr float TzimisceGrabLowerBound = -20.0f;  // _DAT_1049ae98
	constexpr float TzimisceGrabUpperBound = 20.0f;   // _DAT_1044eb0c
	// `0x103bea90`'s aim height and its impulse floor.
	constexpr float TzimisceThrowAimHeightUnits = 48.0f;   // _DAT_10447ee8
	constexpr float TzimisceThrowSpeedFloor = 1000.0f;     // _DAT_10447ee0
	constexpr float TzimisceThrowSpeedFloorImpulse = 1000.0f;   // 0x447a0000
	// `thunk_FUN_102c43b0(this, 0.75)` — the collision-ignore renewal the release ends on.
	constexpr float TzimisceReleaseIgnoreSeconds = 0.75f;
	// `0x103bef20`'s attach range gate: `distSq <= 25600` units squared, i.e. **160 units**.
	constexpr float TzimisceAttachRangeSqUnits = 25600.0f;   // _DAT_104cc51c
	// `CNPC_VTzimisce`'s slot-488 script event.
	constexpr const TCHAR* TzimisceDeathScriptEvent = TEXT("SPI_DIES");
	// `0x103be3d0` walks a NULL-TERMINATED TABLE of bone names starting at
	// `PTR_s_Bip01_L_Forearm_106530e8`, stepping one pointer at a time until the pointed-at string
	// is empty. Only the FIRST entry's text survives in the corpus as a named string; the rest of
	// the table lives past what the decompiler recovered.
	//
	// **Unrecovered:** every entry after the first. The search is written as the walk retail runs
	// and the table carries the one name that IS recovered, so the body's shape — nearest bone wins,
	// index is the table position — is exercised and a longer table needs no other edit.
	const TCHAR* const TzimisceGrabBoneTable[] = { TEXT("Bip01 L Forearm") };
	// `0x103bef20`'s carrier bone, a plain `__strcmpi` against the model's own bone table.
	constexpr const TCHAR* TzimisceAttachCarrierBone = TEXT("Bip01 R Finger1");
	// `UTIL_VecToYaw` `0x101d2c70` over a delta in THIS world's axes, whose Y is the negated Source
	// one. Private copies for the same file-ownership reason families Bosses, Facing and Positions
	// each keep one.
	float Species2VecToYaw(const FVector& PortDelta)
	{
		if (PortDelta.X == 0.0 && PortDelta.Y == 0.0)
		{
			return NpcKernelSpecies2Shared::Species2Zero;
		}
		float Yaw = FMath::RadiansToDegrees(
			static_cast<float>(FMath::Atan2(-PortDelta.Y, PortDelta.X)));
		if (Yaw < NpcKernelSpecies2Shared::Species2Zero)
		{
			Yaw += 360.0f;
		}
		return Yaw;
	}
}

// `CNPC_VTzimisce`'s constructor `0x103b6c60` writes both hull words at `0x103b6d32`, after the
// `CAI_BaseNPC` constructor `0x1027c300` zeroed both; the port's constructor chain runs in the same
// order. `0x103b6d27` `b8 0a 00 00 00` feeds both stores.
FElysiumNpcTzimisce::FElysiumNpcTzimisce()
{
	HullKind = 10;
	PathingHullKind = 10;
}

// Slot 482: `0x103bd270`, the standalone copy with the SCRIPT-state tail.
int32 FElysiumNpcTzimisce::CanPlaySequence(bool bDisregardState, int32 InterruptLevel)
{
	// `0x103bd270`, `CNPC_VTzimisce`'s slot 482 — the same standalone copy (callees `0x101a8ac0`
	// direct and slot 158 virtual; no base call), with the same SCRIPT-state tail.
	return CanPlaySequenceSpecies(bDisregardState, InterruptLevel);
}

// Slot 488: `0x103b92a0`, `SPI_DIES` at the script host (its slot-487 tail call is unported).
void FElysiumNpcTzimisce::DeathSound()
{
	// `0x103b92a0`, `CNPC_VTzimisce`'s slot 488 `DeathSound`. The decompiled C was damaged, so this
	// was read off the LISTING:
	//
	//     a = DAT_1093cf94->vfunc1() ? 0 : DAT_1093cf94[+0x2c];
	//     b = DAT_1093cfdc->vfunc1() ? 0 : DAT_1093cfdc[+0x2c];
	//     c = DAT_1093cebc->vfunc1() ? 0 : DAT_1093cebc[+0x28];
	//     ScriptFire(m_pScriptHost (+0x2e0), "SPI_DIES", c, b, 0, a);      // 0x10002414
	//     JMP  vtable[+0x79c];                                             // slot 487, a TAIL CALL
	//
	// Two things the listing settles that the walk does not:
	//   * the arguments are pushed in the order `a, 0, b, c` and read back by the callee as
	//     `(host, "SPI_DIES", c, b, 0, a)` — so the THIRD singleton is the first argument and the
	//     literal zero sits between the second and the first. The order is reproduced;
	//   * the last instruction is a `JMP`, not a `CALL`: the base slot 487 body runs with this
	//     frame, so slot 488's own work happens BEFORE the base's and the base's return value is
	//     what the caller sees.
	//
	// `TzimisceDeathScriptArgument` reads the three ConVars (pitch 100, attn 65, volume 1.0f's
	// dword). The event itself is the observable and it IS fired.
	int32 A = 0;
	int32 B = 0;
	int32 C = 0;
	TzimisceDeathScriptArgument(0, A);
	TzimisceDeathScriptArgument(1, B);
	TzimisceDeathScriptArgument(2, C);
	ElysiumStub::Fired(TEXT("method"),
		FString::Printf(TEXT("CNPC_VTzimisce::DeathSound 0x103b92a0 -> %s"),
			TzimisceDeathScriptEvent),
		DebugString(),
		FString::Printf(TEXT("args=%d,%d,0,%d"), C, B, A),
		TEXT("0002/29c-1: no script host for SPI_DIES"));
	// The tail call into slot 487. The generated `DeathSound` is 29c's stub for the base body
	// `0x10293ec0`; slot 487's own port method is not this family's row, so the base death sound is
	// what the caller sees and is left to the slot that already carries it.
}

// Slot 593: `0x103b9180`, the Troika body `0x1029a070` first (a direct call), then five overwrites.
/** `0x103b9180` / `0x103b92a0` — `CNPC_VTzimisce`'s slots 593 and 488. */
void FElysiumNpcTzimisce::Slot593()
{
	// `0x103b9180`:
	//     CAI_BaseNPCTroika::Base593(this);            // thunk 0x1029a070
	//     field_0x655c = 0x3c23d70a;    //  0.009999999776482582
	//     field_0x6560 = 0x3f800000;    //  1.0
	//     field_0x6564 = 0x42480000;    // 50.0
	//     field_0x6568 = 0x42480000;    // 50.0
	//     field_0x656c = 0x3c23d70a;    //  0.009999999776482582
	//
	// The base runs FIRST and then five immediates overwrite the target-lead block it just set —
	// the base's own values are therefore unobservable on a Tzimisce, which is why the body is a
	// slot override and not a registry row.
	//
	// `+0x655c`..`+0x656c` are the five `m_flTargetLead*` words 29b declared on `FElysiumNpc`
	// (`ElysiumNpcKernelShapeMap.cpp` binds all five), and family **TroikaHelpers**'
	// `ComputeTargetLeadPoint` is the reader that blends by them. So the writes are REAL here: a
	// Tzimisce's lead point is computed from a min of 0.01, a max of 1.0, equal current and
	// predicted weights of 50 and a weight scale of 0.01.
	//
	// `FElysiumNpc::Slot593` is family Closure's port of `0x1029a070`, called DIRECTLY (thunk
	// `0x10002720`) and so qualified to its Troika owner: base first, overwrite second.
	FElysiumNpc::Slot593();
	TargetLeadMin = 0.009999999776482582f;        // +0x655c, 0x3c23d70a
	TargetLeadMax = 1.0f;                         // +0x6560, 0x3f800000
	TargetLeadCurrentWeight = 50.0f;              // +0x6564, 0x42480000
	TargetLeadPredictedWeight = 50.0f;            // +0x6568, 0x42480000
	TargetLeadWeightScale = 0.009999999776482582f;// +0x656c, 0x3c23d70a
}

// Slot 420: `0x103b91d0`.
// `0x103b91d0`
void FElysiumNpcTzimisce::NPCInit()
{
	TroikaNPCInit();
	bTzimisceFirstEnemy = true;
	PathMode = 0;
	++ExpressionMapResets;                                               // 103b9f50
	TzimisceShunnedFindBody = 0;
	bTzimisceJustFoundBody = false;
	const double Now = NpcKernelLifecycle19_2Shared::Lifecycle19_2Now(*this);
	TzimiscePounceCheckTimer = Now + SpeciesShunWindowSeconds;
	TzimisceShunnedBodyTimer = Now + SpeciesShunWindowSeconds;
	PickupTarget = FElysiumEntityHandle::Invalid();
	FUN_102c43b0(0.f);
}

// Slot 422: `0x103b9270`.
// `0x103b9270`
void FElysiumNpcTzimisce::StartNPC()
{
	TroikaStartNPC();                                                    // 1029a8b0
	ThinkSet(StartNpcThinkFunction(), 0.0);                              // redundant re-arm
	++TzimisceStartNpcRearms;
}

// Slot 104: `0x103b8fa0`.
// 0x103b8fa0
void FElysiumNpcTzimisce::Precache()
{
	// `CNPC_VTzimisce::Precache` `0x103b8fa0` — the six-entry spiderchick footstep table, the
	// three-entry swish table, the melee weapon, and only then the Troika body. Base-last.
	// Oracle: `docs/vtmb/footsteps.md`.
	NpcKernelPrecache10Shared::Precache10SoundTable(*this, GSpiderchickFootsteps, UE_ARRAY_COUNT(GSpiderchickFootsteps));
	NpcKernelPrecache10Shared::Precache10SoundTable(*this, GSpiderchickSwishes, UE_ARRAY_COUNT(GSpiderchickSwishes));
	NpcKernelPrecache10Shared::Precache10Other(*this, GTzimisceWeapon);
	TroikaPrecache();
}

// Slot 375: `0x103bde40`, which calls the Troika body `0x10295590` directly.
/** `CNPC_VTzimisce::NPC_EarlyTranslateActivity` (`0x103bde40`). Under the carry-body flag bit, a
 *  ZERO `m_bHeavyBodyTarget` gives `0xfd` / `0xff` and a non-zero one `0xfc` / `0xfe`. */
int32 FElysiumNpcTzimisce::NPC_EarlyTranslateActivity(int32 Activity)
{
	// `CNPC_VTzimisce::NPC_EarlyTranslateActivity` `0x103bde40`, 102 bytes.
	if (TzimisceCarryFormBit())
	{
		// **The polarity is the ZERO test**: `m_bHeavyBodyTarget` CLEAR takes the `_L` variants
		// (0xfd / 0xff) and SET takes the plain ones (0xfc / 0xfe). The generated table's
		// `BodySideLeft` comment read it the other way round; the listing's `CMP byte, 0` is what
		// this follows, and the generator's comment is corrected with it.
		if (!bHeavyBodyTarget)
		{
			if (Activity == NpcKernelAnim10Shared::GAnim10ActIdle)
			{
				return GAnim10ActIdleBodyL;
			}
			if (Activity == NpcKernelAnim10Shared::GAnim10ActWalk || Activity == NpcKernelAnim10Shared::GAnim10ActRun)
			{
				return GAnim10ActWalkBodyL;
			}
		}
		else
		{
			if (Activity == NpcKernelAnim10Shared::GAnim10ActIdle)
			{
				return GAnim10ActIdleBody;
			}
			if (Activity == NpcKernelAnim10Shared::GAnim10ActWalk || Activity == NpcKernelAnim10Shared::GAnim10ActRun)
			{
				return GAnim10ActWalkBody;
			}
		}
	}
	return TroikaNpcEarlyTranslateActivity(Activity);   // the direct `thunk_FUN_10295590`
}

// Slot 463: `0x103ba2c0`. On a real transition the new state picks a facial expression blended over
// 1.0 s; the direct call into the Troika body runs either way.
void FElysiumNpcTzimisce::OnStateChange(EElysiumNpcState OldState, EElysiumNpcState NewState)
{
	if (OldState != NewState)
	{
		if (const TCHAR* Name = StateChangeExpressionName(NewState))
		{
			SetDefaultExpression(Name, 1.0f);
		}
	}
	OnStateChangeTroika(OldState, NewState);
}

// Slot 461: `0x103bd690`, chaining the Troika body directly.
// `CNPC_VTzimisce::vfunc461` (`0x103bd690`) — slot 461. The base ladder rewritten around the
// Tzimisce's hunt state: idle takes SEE_UNKNOWN through slot 586 and accepts sound type **4** as
// well as the base's 1/8/0x10; combat with no enemy (or a dead one) falls to HUNT `0xb` rather
// than ALERT; and alert's hear arm answers HUNT. Everything it does not name chains Troika.
int32 FElysiumNpcTzimisce::SelectIdealStateRetail()
{
	SelectIdealStateSelector = 0x26;
	switch (NpcStateRetail())
	{
	case 1:
		// `103bd6c4`: NEW_ENEMY alone — the base pairs it with SEE_ENEMY, this body does not.
		if (NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::NewEnemy))
		{
			NpcKernelState19_2Shared::State19_2Stamp(*this, 2, 0xd42);
			break;
		}
		if (NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::LightDamage))
		{
			Cognition.bCondTookDamage = false;
			++SelectIdealStateMotorResets;
			NpcKernelState19_2Shared::State19_2Stamp(*this, 3, 0xd4c);
			break;
		}
		if (NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::HeavyDamage))
		{
			Cognition.bCondTookDamage = false;
			++SelectIdealStateMotorResets;
			NpcKernelState19_2Shared::State19_2Stamp(*this, 3, 0xd56);
			break;
		}
		if (NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::SeeUnknown))
		{
			// `103bd7ff`: slot 586 `GetBestSeeUnknown()`. A handle that does not resolve refuses
			// the arm outright — the body chains Troika rather than falling to the hear ladder.
			FElysiumEntity* const Unknown = NpcKernelState19_2Shared::State19_2Resolve(*this, GetBestSeeUnknown());
			if (Unknown == nullptr)
			{
				return NpcKernelState19_2Shared::State19_2ChainTroika(*this);
			}
			++SelectIdealStateMotorResets;
			NpcKernelState19_2Shared::State19_2Stamp(*this, 3, 0xd67);
			break;
		}
		if (NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::HearDanger)
			|| NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::HearCombat)
			|| NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::HearWorld)
			|| NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::HearPlayer)
			|| NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::HearThumper)
			|| NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::HearBulletImpact))
		{
			const FElysiumGameSoundEvent* const Sound = NpcKernelState19_2Shared::State19_2BestSound(*this);
			if (Sound == nullptr)
			{
				return NpcKernelState19_2Shared::State19_2ChainTroika(*this);
			}
			++SelectIdealStateMotorResets;
			const uint32 Type = Sound->TypeMask;
			// `103bd8ba`: 1, **4**, 8, 0x10 — the extra `4` (SOUND_PLAYER) is this class's own.
			if (Type != 1u && Type != 4u && Type != 8u && Type != 0x10u)
			{
				return NpcKernelState19_2Shared::State19_2ChainTroika(*this);
			}
			NpcKernelState19_2Shared::State19_2Stamp(*this, 3, 0xd84);
			break;
		}
		if (NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::Smell))
		{
			NpcKernelState19_2Shared::State19_2Stamp(*this, 3, 0xd8b);
			break;
		}
		return NpcKernelState19_2Shared::State19_2ChainTroika(*this);

	case 2:
		// `103bd90d`: the provocation memory bit, the same pair `CNPC_VHuman` runs.
		if ((BaseScheduleHost.MemoryBits & 0x2) != 0
			&& (NpcKernelState19_2Shared::State19_2HasCondition(*this, EElysiumNpcCond::SeeEnemy)
				|| NpcKernelState19_2Shared::State19_2HasCondition(*this, EElysiumNpcCond::NewEnemy)))
		{
			BaseScheduleHost.MemoryBits &= ~0x2u;
			Cognition.Conditions.Set(EElysiumNpcCond::ScheduleDone);
		}
		// `103bd95c`: no enemy OR `ENEMY_DEAD` — and the fallback is HUNT, not ALERT, unless the
		// Tzimisce can still see one.
		if (GetEnemy() == nullptr || NpcKernelState19_2Shared::State19_2HasCondition(*this, EElysiumNpcCond::EnemyDead))
		{
			if (NpcKernelState19_2Shared::State19_2HasCondition(*this, EElysiumNpcCond::SeeEnemy))
			{
				NpcKernelState19_2Shared::State19_2Stamp(*this, 3, 0xdcd);
			}
			else
			{
				NpcKernelState19_2Shared::State19_2Stamp(*this, 0xb, 0xdd2);
			}
			UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s ***Combat state with no enemy!"),
				*DebugString());
			return IdealStateRetail();
		}
		if (!NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::LostEnemy))
		{
			return NpcKernelState19_2Shared::State19_2ChainTroika(*this);
		}
		NpcKernelState19_2Shared::State19_2Stamp(*this, 0xb, 0xddb);
		break;

	case 3:
		if (NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::NewEnemy)
			|| NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::SeeEnemy))
		{
			NpcKernelState19_2Shared::State19_2Stamp(*this, 2, 0xd9a);
			break;
		}
		// `103bda2e`: four hear conditions, and the answer is HUNT `0xb`, not ALERT.
		if (NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::HearDanger)
			|| NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::HearCombat)
			|| NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::HearPlayer)
			|| NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::HearBulletImpact))
		{
			NpcKernelState19_2Shared::State19_2Stamp(*this, 0xb, 0xda2);
			if (NpcKernelState19_2Shared::State19_2BestSound(*this) != nullptr)
			{
				++SelectIdealStateMotorResets;
			}
			return IdealStateRetail();
		}
		if (ShouldGoToIdleState())
		{
			NpcKernelState19_2Shared::State19_2Stamp(*this, 1, 0xdaf);
			break;
		}
		return NpcKernelState19_2Shared::State19_2ChainTroika(*this);

	case 0xb:
		if (NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::NewEnemy))
		{
			NpcKernelState19_2Shared::State19_2Stamp(*this, 2, 0xdf2);
			break;
		}
		if (NpcKernelState19_2Shared::State19_2HasInterrupt(*this, EElysiumNpcCond::SeeEnemy))
		{
			NpcKernelState19_2Shared::State19_2Stamp(*this, 2, 0xdf7);
			break;
		}
		if (ShouldGoToIdleState())
		{
			NpcKernelState19_2Shared::State19_2Stamp(*this, 1, 0xdfc);
			break;
		}
		return NpcKernelState19_2Shared::State19_2ChainTroika(*this);

	default:
		return NpcKernelState19_2Shared::State19_2ChainTroika(*this);
	}
	return IdealStateRetail();
}

// Slot 201: `0x103ba290`
/** `CNPC_VTzimisce::FVisible` (`0x103ba290`), 30 bytes: the Troika base with the FOURTH argument
 *  FORCED to `0`, whatever the caller supplied. That is the whole override, and it is observable. */
bool FElysiumNpcTzimisce::FVisible(FElysiumEntity* SeenTarget, int32 Mask, FElysiumEntity* Blocker, int32 Arg4)
{
	// `103ba290`, 30 bytes: the Troika base with the FOURTH argument FORCED to `0`. The whole
	// override is that one clamp, and it is observable — every Tzimisce visibility test runs the
	// base with that word zeroed.
	return FElysiumNpc::FVisible(SeenTarget, Mask, Blocker, 0);
}

// Slot 418: `0x103b9120`, a species sentinel ahead of a direct call into the Troika body.
// `0x103b9120`
// `CNPC_VTzimisce::ResolveTaskDistance` `0x103b9120`: `if ((int)param != -1000001) return
// base(param); else return _DAT_10457f60;`, the base a direct call into `0x102bf6e0`.
float FElysiumNpcTzimisce::ResolveTaskDistance(float Distance)
{
	if (static_cast<int32>(Distance) == -1000001)
	{
		return GScheduleTzimisceTaskDistance;
	}
	return FElysiumNpc::ResolveTaskDistance(Distance);
}

// Slot 448: `0x103ba350`, its own arm and then a direct call into the Troika body `0x1029adb0`.
/** `CNPC_VTzimisce::TaskFail` (`0x103ba350`) — the Hengeyokai's arm with its own words:
 *  `m_iShunnedFindBody` (`+0x66b8`) and its own `m_FailedPickupTargets` blacklist. */
void FElysiumNpcTzimisce::TaskFail(int32 Reason)
{
	// `CNPC_VTzimisce::TaskFail` (`0x103ba350`), 130 bytes — the Hengeyokai arm with its own words.
	// `0x103be090` / `0x103be130` / `0x103be050` are byte-identical to the Hengeyokai's trio and are
	// the same two `m_bfAINPCFlags` bits.
	(void)Reason;

	if (NpcKernelConditions10Shared::Cond10RetailNpcState(Mind.State()) == NpcKernelConditions10Shared::GCond10NpcStateCombat
		&& ElysiumSchedule::HasInterruptCondition(Schedule, *this, Cognition.Conditions,
			EElysiumNpcCond::TaskFailed))
	{
		BaseScheduleHost.MemoryBits &= ~NpcKernelConditions10Shared::GCond10MemoryTopBit;                 // 103ba376
	}

	if (NpcFlags.Has(EElysiumNpcFlag::FINDING_BODY))                     // 0x103be090
	{
		BlacklistPickupTarget(PickupTarget);                      // 0x103bf200
		NpcFlags.Clear(EElysiumNpcFlag::FINDING_BODY);                   // 0x103be050(this, 0)
	}
	if (!NpcFlags.Has(EElysiumNpcFlag::CARRYING_BODY))                   // 0x103be130
	{
		SetIgnoreCollisionExpiry(NpcKernelConditions10Shared::GCond10PickupReuseDelay);
		PickupTarget = FElysiumEntityHandle::Invalid();           // m_hPickupTarget +0x6670
	}

	TzimisceShunnedFindBody = 0;                                         // m_iShunnedFindBody +0x66b8
	FElysiumNpc::TaskFail(Reason);
}

// Slot 440: `0x103bd390`.
// `0x103bd390`
// `0x103bd390`, `CNPC_VTzimisce::TranslateSchedule`, the body of `FElysiumNpcTzimisce::TranslateScheduleRetail`
// (story 5 step 3). Every miss is a direct call into the Troika body `0x102b12f0`.
int32 FElysiumNpcTzimisce::TranslateScheduleRetail(int32 ScheduleNumber)
{
	// Retail stamps its own `__FILE__`/`__LINE__` (lines `0xd00`–`0xd0f`) into `+0x1b30`/
	// `+0x1b34` on each of these fifteen rows BEFORE answering, and the default arm writes
	// neither — the pair records "this class decided". The shape map calls that pair
	// ABSENT; the mind's transition trace carries the same account.
	struct FRow { int32 From; int32 To; };
	static const FRow Rows[] = {
		{ 1, 0x157 }, { 5, 0x19b }, { 6, 0x158 },
		{ 0xf, 0x166 }, { 0x10, 0x16a }, { 0x15, 0x16c },
		{ 0x21, 0x184 }, { 0x22, 0x185 }, { 0x25, 0x16d },
		{ 0x28, 0x16e }, { 0x2f, 0x188 }, { 0x30, 0x189 },
		{ 0x31, 0x18a }, { 0x32, 0x18b }, { 0x33, 0x18c },
	};
	for (const FRow& Row : Rows)
	{
		if (Row.From == ScheduleNumber)
		{
			return Row.To;
		}
	}
	return TroikaTranslateScheduleRetail(ScheduleNumber);
}

// Slot 516: `0x103ba020`, which replaces the Troika ladder.
float FElysiumNpcTzimisce::MaxYawSpeed()
{
	// `CNPC_VTzimisce::MaxYawSpeed` `0x103ba020` — a four-arm switch and the shared turning arm,
	// with the Tzimisce's own cvar `0x1093c9fc`.
	if ((BaseScheduleHost.MemoryBits & NpcKernelMotorShared::GMemoryTurning) != 0)
	{
		return MaxYawSpeedTurningArm(TEXT("0x1093c9fc"));
	}
	switch (ActivityNumber)
	{
	case NpcKernelMotorShared::GActIdle:
	case 0xfc:
	case 0xfd:
		return GYawTzimisceIdle;      // _DAT_10454110 = 5.0
	case NpcKernelMotorShared::GActRun:
		return NpcKernelMotorShared::GYawCrouch;            // _DAT_104492a8 = 30.0
	default:
		return GYawTzimisceDefault;   // _DAT_104cc504 = 11.0
	}
}

// Slot 69: `0x103bfa00` (byte-identical across Hengeyokai, MingXiao and Tzimisce): the `0x16` derived-type
// gate, then a direct call into the Troika body `0x1029b180`.
bool FElysiumNpcTzimisce::NavIgnoreCollision(FElysiumEntity* Other)
{
	if (Other != nullptr && (RetailDerivedType(*Other) & 0x16) != 0)
	{
		return true;
	}
	return FElysiumNpc::NavIgnoreCollision(Other);
}

// Slot 124: `0x103c08d0`
/** `CNPC_VTzimisce::DrawDebugTextOverlays` (`0x103c08d0`) — the Troika body, then one `Body - …`
 *  line under bit 0 built from a cross-NPC latch pair of globals. */
int32 FElysiumNpcTzimisce::DrawDebugTextOverlays()
{
	// `0x103c08d0`, 387 bytes.
	//
	//     dist = 0.0;                                              // _DAT_104454c4
	//     if (m_hPickupTarget (+0x6670) resolves)
	//         dist = |target->GetAbsOrigin() - GetAbsOrigin()|;     // the full 3-D length
	//     if (0x103be130() && _DAT_1047a3ac < dist) {               // 160.0
	//         _DAT_1093d01c = dist;
	//         if (m_pSchedule) strcpy(DAT_1093cd70, m_pSchedule->name);
	//     }
	//     Q_snprintf(buf, 512, "Body - %5.1f|%5.1f|%s", dist, _DAT_1093d01c, DAT_1093cd70);
	//
	// The two globals are a CROSS-NPC latch — every Tzimisce in the map writes and reads the same
	// pair — so they are file statics here and not per-instance state. The checklist's walk spelled
	// the format `"Body: %5.1f %5.1f %s"`; the image says `"Body - %5.1f|%5.1f|%s"`.
	const int32 Base = TroikaDrawDebugTextOverlays();
	if ((DebugOverlays & NpcKernelDebug10Shared::GDebug10BitText) == 0)
	{
		return Base;
	}

	float Distance = GDebug10Zero;
	const FElysiumEntity* const Carried =
		World != nullptr ? World->Resolve(PickupTarget) : nullptr;
	if (Carried != nullptr)
	{
		Distance = static_cast<float>((Carried->Origin - Origin).Size() / ElysiumMove::U);
	}
	if (TzimisceCarryFormBit() && Distance > GDebug10TzimisceLatchUnits)   // 0x103be130 (bit 5 of +0x14b8)
	{
		GDebug10TzimisceLatchDistance = Distance;
		if (Schedule.IsRunning())
		{
			const TCHAR* const Name = ElysiumScheduleName(Schedule.Current);
			GDebug10TzimisceLatchSchedule = Name != nullptr ? Name : TEXT("");
		}
	}
	EmitEntityText(Base, GDebug10FmtTzimisceBody, FString::Printf(GDebug10FmtTzimisceBody,
		Distance, GDebug10TzimisceLatchDistance, *GDebug10TzimisceLatchSchedule));
	return Base + 1;
}

// Slot 337: `0x103b9160`.
int32 FElysiumNpcTzimisce::GetUsedHullBits()
{
	// The Troika body `0x1029a050` called directly, its 1 ORed with this class's bit.
	// `OR AH,0x4` in the listing; the decompiled C drops the bit.
	return FElysiumNpc::GetUsedHullBits() | 0x0400;
}

// Slot 566: `0x103ba780`, a replacement that does not chain.
bool FElysiumNpcTzimisce::FValidateHintType(void* Hint)
{
	// `param_1 != 0 && 13999 < t && t < 0x36b2`: 14000..14001, and the ONE species body in
	// the slot that null-checks the hint.
	const FHintWords* Words = static_cast<const FHintWords*>(Hint);
	return Words != nullptr && 13999 < Words->HintType && Words->HintType < 0x36b2;
}

// Slot 546: `0x103b70f0`, the class's own schedule id space.
const TCHAR* FElysiumNpcTzimisce::SquadSlotName(int32 SlotEn)
{
	// The class's `CAI_ClassScheduleIdSpace` `0x1093ccc4`, left empty by `0x102ea090(isRoot = false)`:
	// `SlotEn` translates to -1 and names `<<null>>`.
	static constexpr FSquadSlotSpecies IdSpace = {
		TEXT("CNPC_VTzimisce"), TEXT("0x103b70f0"), TEXT("0x1093ccc4") };
	return GlobalSquadSlotName(SquadSlotLocalToGlobal(&IdSpace, SlotEn));
}

// Slot 563: `0x103ba640`, the `GoalToleranceLead` shape; a replacement that does not chain.
void FElysiumNpcTzimisce::TranslateEnemyChasePosition(FElysiumEntity* Enemy, FVector& ChasePositionCm,
	void* Tolerance, void* SecondTolerance)
{
	TranslateEnemyChasePositionShaped(EChaseTranslateShape::GoalToleranceLead, Enemy, ChasePositionCm, Tolerance,
		SecondTolerance);
}

// Slot 435: `0x103bf610`, the Troika body `0x102a0940` directly, then the class's own tail.
// `0x103bf610`
void FElysiumNpcTzimisce::OnScheduleChange(int32 NewSchedule)
{
	// `CNPC_VTzimisce::OnScheduleChange` `0x103bf610`: the Troika body directly, first.
	FElysiumNpc::OnScheduleChange(NewSchedule);
	if (!NpcFlags.Has(EElysiumNpcFlag::PRESERVE_PATH)) // 0x103bf625
	{
		PathMode = 0;					 // 0x103bf630, +0x668c
		if (TzimisceShunnedFindBody > 0) // 0x103bf63a
		{
			--TzimisceShunnedFindBody; // 0x103bf63e
		}
	}
}

// Slot 487: `0x103b9f10`, a replacement that does not chain.
// `CNPC_VTzimisce::vfunc487` (`0x103b9f10`) — the body of `FElysiumNpcTzimisce::JustMadeSound`.
// `CAI_BaseNPCTroika::FUN_102b4c40` (`0x102b4c40`), slot 487 on the Troika line: `m_flSoundWaitTime
// = curtime + RandomFloat(0.25, 0.75)`, and — when the squad is connected — a SECOND, INDEPENDENT
// draw written to the squad's own copy at `+0x60`. Retail draws twice; it does not reuse the first
// number, which is why the two clocks drift apart.
//
// `CNPC_VTzimisce::vfunc487` (`0x103b9f10`) is the one species override of this slot: the same
// formula with a 0.5–0.75 draw and NO squad half at all, `FElysiumNpcTzimisce`'s override (story 5
// step 3).
void FElysiumNpcTzimisce::JustMadeSound()
{
	// `CNPC_VTzimisce::vfunc487` `0x103b9f10`, the body of `FElysiumNpcTzimisce::JustMadeSound`: 41
	// bytes that end at the write. No squad copy, and no call into the Troika body.
	FRandomStream& Stream = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule);
	BaseMemory.SoundWaitTime = NpcKernelSoundsShared::SoundsCurTime(*this)
		+ Stream.FRandRange(GSoundsTzimisceSoundWaitMin, GSoundsTzimisceSoundWaitMax);
}

// Slot 490: `0x103b9380`: `if (FOkToMakeSound())`, the Tzimisce sound family's three ConVars
// (`tzimisce_voice_pitch` "100" `0x1093cf94`, `tzimisce_voice_attn` "65" `0x1093cfdc`,
// `tzimisce_voice_volume` "1" `0x1093cebc`), then `SENTENCEG_PlayRndSz(edict, "SPI_IDLE", volume,
// attn, 0, pitch)`; it does not chain.
void FElysiumNpcTzimisce::IdleSound()
{
	if (FOkToMakeSound())
	{
		NpcKernelSoundsShared::SoundsPlaySentenceGroup(*this, TEXT("SPI_IDLE"),
			ElysiumNpcTunables::ConVarFloat(ElysiumNpcTunables::EConVar::TzimisceVoiceVolume),
			ElysiumNpcTunables::ConVarInt(ElysiumNpcTunables::EConVar::TzimisceVoiceAttn), 0,
			ElysiumNpcTunables::ConVarInt(ElysiumNpcTunables::EConVar::TzimisceVoicePitch));
	}
}

// Slot 491: `0x103b9500`:
//
//     if (FOkToMakeSound()) {                                  // vtable +0x798
//         <the three ConVars>; SENTENCEG_PlayRndSz(edict, "SPI_TAKE_DAMAGE", …);
//         JustMadeSound();                                     // vtable +0x79c, 103b9592
//     }
//     0x103b9f90(this, 1, 1.0);                                // UNCONDITIONAL
//
// The only vocalization in the family that re-arms the sound clock itself; it does not chain. The
// tail is `SetExpression(ExpressionTable[1] = "angry", …, 1.0)`, the pain facial expression, and it
// runs whether the gate opened or not: `SetDefaultExpression` is its seam (it records the name).
void FElysiumNpcTzimisce::PainSound()
{
	if (FOkToMakeSound())
	{
		NpcKernelSoundsShared::SoundsPlaySentenceGroup(*this, TEXT("SPI_TAKE_DAMAGE"),
			ElysiumNpcTunables::ConVarFloat(ElysiumNpcTunables::EConVar::TzimisceVoiceVolume),
			ElysiumNpcTunables::ConVarInt(ElysiumNpcTunables::EConVar::TzimisceVoiceAttn), 0,
			ElysiumNpcTunables::ConVarInt(ElysiumNpcTunables::EConVar::TzimisceVoicePitch));
		JustMadeSound();
	}
	SetDefaultExpression(TEXT("angry"), 1.0f);   // 0x103b9f90(this, 1, 1.0)
}

// --- Moved from `ElysiumNpcAnim10.cpp` (story 5 step 4) ---

bool FElysiumNpcTzimisce::TzimisceCarryFormBit() const
{
	// `thunk_FUN_103be130(this)` — the SAME `+0x14b8` bit 5 on `CNPC_VTzimisce`.
	return NpcFlags.Has(EElysiumNpcFlag::CARRYING_BODY);
}

// --- Moved from `ElysiumNpcConditionsBodies.cpp` (story 5 step 4) ---

const TCHAR* FElysiumNpcTzimisce::StateChangeExpressionName(EElysiumNpcState NewState)
{
	// `CNPC_VTzimisce::vfunc463` (`0x103ba2c0`) maps the NEW state to an index into
	// `PTR_s_normal_10653120`, whose four entries were read out of `.rdata`:
	//   0 "normal", 1 "angry", 2 "scream", 3 "dead".
	// The switch names three of them — idle -> 0, alert/combat/hunt -> 1, dead -> 3 — and "scream"
	// is reached by no state. Every other state falls through writing nothing.
	switch (NpcKernelConditionsShared::CondRetailStateId(NewState))
	{
	case 1:               return TEXT("normal");   // index 0
	case 2: case 3:       return TEXT("angry");    // index 1; retail also lists 0xb HUNT here
	case 7:               return TEXT("dead");     // index 3
	default:              return nullptr;
	}
}

void FElysiumNpcTzimisce::SetDefaultExpression(const TCHAR* ExpressionName, float BlendSeconds)
{
	// SEAM for `CBaseCombatCharacter::LookupExpressionIndex` + `0x103b9f90(this, index, 1.0)`. There
	// is no `SetExpression` in this runtime — the script API lists it as a stub and family Sounds
	// already recorded the same gap for `CNPC_VTzimisce::PainSound`. The NAME is stored because
	// this runtime names expressions (`NoDeformExpression`, +0x64d0, is the same shape); the blend
	// is dropped and named here rather than faked.
	(void)BlendSeconds;
	DefExpression = ExpressionName != nullptr ? FString(ExpressionName) : FString();
}

// --- Moved from `ElysiumNpcConditions10.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcDamage2.cpp` (story 5 step 4) ---

void FElysiumNpcTzimisce::VGargoyleGibCleanup()
{
	// 1. `m_hPickupTarget` (+0x6670) to -1, FIRST and unconditionally.
	PickupTarget = FElysiumEntityHandle();
	// 2. `thunk_FUN_102c43b0(this, 0.75)` — the collision-ignore re-arm, before anything is removed.
	ArmIgnoreCollisionExpiry(NpcKernelDamage2Shared::ThrowIgnoreCollisionSeconds);
	// 3. Resolve `m_hPhysicsAnimlink` (+0x6684) and call `UTIL_Remove` on it — **even when the
	//    handle does NOT resolve**, in which case retail passes a null pointer. That unguarded call
	//    is the body's own shape and is reproduced by always running the removal path; the port's
	//    removal is a no-op on an invalid handle, where retail's relied on `UTIL_Remove`'s own null
	//    tolerance.
	RemoveNamedEntity(TzimiscePhysicsAnimlink);
	TzimiscePhysicsAnimlink = FElysiumEntityHandle();
	// 4. `thunk_FUN_103be0b0(this, 0)` — the Tzimisce `CARRYING_BODY` flag write. Family Bosses'
	//    `CallFormBit` stands the same seam for the Hengeyokai arm; this records it the same way.
	CallFormBit(false);
}

// --- Moved from `ElysiumNpcDebug.cpp` (story 5 step 4) ---

const TCHAR* FElysiumNpcTzimisce::TzimisceEventName(int32 EventId)
{
	// `CNPC_VTzimisce::GetEventName` `0x103bdd10`, slot 241's only species override. Retail's
	// signature is `void GetEventName(char* out, animevent_t* event)` and each arm `strcpy`s its
	// literal into `out`; ids outside 2..8 tail into `CBaseAnimating::GetEventName`.
	//
	// Named `TzimisceEventName` and NOT `GetEventName`: slot 241's Troika-line body (`0x1008c170`)
	// is still a generated stub owned by a later story, and it is what holds the `GetEventName`
	// name. When that body lands it dispatches here for `CNPC_VTzimisce`.
	const int32 Offset = EventId - 2;
	if (Offset >= 0 && Offset < UE_ARRAY_COUNT(GNpcKernelDebugTzimisceEventNames))
	{
		return GNpcKernelDebugTzimisceEventNames[Offset];
	}
	return nullptr;
}

// --- Moved from `ElysiumNpcHints.cpp` (story 5 step 4) ---

bool FElysiumNpcTzimisce::IsTzimisceHintUsable(int32 HintNode, const FElysiumEntity* Anchor) const
{
	// SEAM for `0x103bfc20`.
	(void)HintNode;
	(void)Anchor;
	return false;
}

int32 FElysiumNpcTzimisce::SelectTzimisceHintNode(const FElysiumEntity* Anchor)
{
	// `0x103bfa50`. Two competing hint groups within 200 units of `Target`, the nearer wins, the
	// loser is released with a 0.5 s reuse delay, and the answer is a schedule id chosen by whether
	// the winner is usable.
	if (Anchor == nullptr)
	{
		BaseScheduleHost.HintNode = INDEX_NONE;   // retail zeroes `m_pHintNode`
		return 0;
	}

	const int32 A = FindHintOfTypeNear(Anchor, 14000, 0, 200.0f);
	const int32 B = FindHintOfTypeNear(Anchor, 0x36b1, 0, 200.0f);

	auto Install = [this, Anchor](int32 Node, int32 BaseSchedule) -> int32
	{
		BaseScheduleHost.HintNode = Node;
		// `(-(uint)usable & 0xfffffff6) + Base` — usable subtracts 10 from the base id.
		return IsTzimisceHintUsable(Node, Anchor) ? BaseSchedule - 10 : BaseSchedule;
	};

	if (A != INDEX_NONE)
	{
		if (B == INDEX_NONE)
		{
			return Install(A, 0x36ba);
		}
		// The compare is on the vtable `+0x370` position accessor of the target and of each hint,
		// squared, with no tie-break: an exact tie takes the B branch.
		const FVector AnchorPos = HintComparePosition(Anchor);
		FHintWords WordsA;
		FHintWords WordsB;
		HintWords(A, WordsA);
		HintWords(B, WordsB);
		const double DistA = FVector::DistSquared(WordsA.OriginCm, AnchorPos);
		const double DistB = FVector::DistSquared(WordsB.OriginCm, AnchorPos);
		if (DistA < DistB)
		{
			ReleaseHintNode(B, 0.5f);
			return Install(A, 0x36ba);
		}
		ReleaseHintNode(A, 0.5f);
	}
	if (B != INDEX_NONE)
	{
		return Install(B, 0x36bb);
	}
	// Both searches missed. Retail leaves `m_pHintNode` untouched on this path — only the null
	// `Anchor` arm zeroes it — and returns 0.
	return 0;
}

// --- Moved from `ElysiumNpcLifecycle2.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcLifecycle19_2.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcMaintain.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcMotor.cpp` (story 5 step 4) ---

bool FElysiumNpcTzimisce::TranslateNavGoalPositionTzimisce(const FVector& GoalUnits,
	FVector& OutGoalUnits) const
{
	// `CNPC_VTzimisce::vfunc410` `0x103bf580`, the species branch of slot 410:
	//     if (m_ePathMode == 1) { if (GetEnemy()) return GetEnemy()->vtable[0x370/4 = 220](); }
	//     else if (m_ePathMode != 2) return goal;
	//     if (m_hPickupTarget resolves) return m_vecPickupTargetPos;
	//     return goal;
	// Note the fall-through: path mode 1 with no enemy does NOT return the goal, it drops into the
	// pickup arm. That is retail's own control flow and it is kept.
	if (PathMode == 1)
	{
		FElysiumEntity* Enemy = World != nullptr && BaseMemory.Enemy.IsSet()
			? World->Resolve(BaseMemory.Enemy)
			: nullptr;
		if (Enemy != nullptr)
		{
			OutGoalUnits = NpcKernelMotorShared::SourceOf(Enemy->Origin);
			return true;
		}
	}
	else if (PathMode != 2)
	{
		OutGoalUnits = GoalUnits;
		return false;
	}
	const bool bPickupLive = PickupTarget.IsSet() && World != nullptr
		&& World->Resolve(PickupTarget) != nullptr;
	if (bPickupLive)
	{
		OutGoalUnits = PickupTargetPos;
		return true;
	}
	OutGoalUnits = GoalUnits;
	return false;
}

// --- Moved from `ElysiumNpcPositions2.cpp` (story 5 step 4) ---

float FElysiumNpcTzimisce::TzimisceAimConVar(int32 Which)
{
	// 0 `DAT_1093cbac` (the UP term), 1 `DAT_1093cbf4` (RIGHT), 2 `DAT_1093cc3c` (FORWARD), each
	// read as `IsCommand() ? 0.0f : m_fValue (+0x28)`: `tzimisce_claw_left_z` "40",
	// `tzimisce_claw_left_y` "25" and `tzimisce_claw_left_x` "0". Any other index answers 0.
	using ElysiumNpcTunables::EConVar;
	switch (Which)
	{
	case 0: return ElysiumNpcTunables::ConVarFloat(EConVar::TzimisceClawLeftZ);
	case 1: return ElysiumNpcTunables::ConVarFloat(EConVar::TzimisceClawLeftY);
	case 2: return ElysiumNpcTunables::ConVarFloat(EConVar::TzimisceClawLeftX);
	default: return 0.f;
	}
}

FVector FElysiumNpcTzimisce::TzimisceAimOffset(const FVector& SrcCm, const FVector& Forward,
	const FVector& Right, const FVector& Up, float ForwardScale, float RightScale, float UpScale,
	bool bAddRight)
{
	// The two arms of `0x103bfd80`, which differ in ONE sign and nothing else:
	//
	//     m_Activity == 0x106:  out = src + forward*F - right*R + up*Up
	//     m_Activity == 0x107:  out = src + forward*F + right*R + up*Up
	//
	// Retail spells the second arm as two statements — the sum without the up term, then
	// `Vector::operator+` (`FUN_1011e060`) with it — and the first as one expression; the result is
	// the same and the split is the compiler's.
	const FVector RightTerm = Right * RightScale;
	return SrcCm + Forward * ForwardScale + (bAddRight ? RightTerm : -RightTerm) + Up * UpScale;
}

bool FElysiumNpcTzimisce::WeaponShootPositionTzimisce(const FVector& SrcCm, FVector& OutCm) const
{
	// `CNPC_VTzimisce::vfunc389` `0x103bfd80` — the species branch of slot 389. Two activities get
	// an offset aim origin and everything else falls through to
	// `CBaseCombatCharacter::Weapon_ShootPosition`, which is what `false` says here.
	//
	//     AngleVectors( GetAbsAngles(), &forward, &right, &up );   // vtable +0x374, slot 221
	if (ActivityNumber != 0x106 && ActivityNumber != 0x107)
	{
		return false;
	}
	FVector Fwd = FVector::ZeroVector;
	FVector Rgt = FVector::ZeroVector;
	FVector Upv = FVector::ZeroVector;
	RetailAngleVectors(Angles, Fwd, Rgt, Upv);
	// `0x103bfd80` adds Source-unit distances to its source point. Here the point is in
	// centimetres and the basis is dimensionless, so convert all three ConVar distances once.
	OutCm = TzimisceAimOffset(SrcCm, Fwd, Rgt, Upv, TzimisceAimConVar(2) * NpcKernelPositions2Shared::GPositionsTailU,
		TzimisceAimConVar(1) * NpcKernelPositions2Shared::GPositionsTailU, TzimisceAimConVar(0) * NpcKernelPositions2Shared::GPositionsTailU,
		/*bAddRight*/ ActivityNumber == 0x107);
	return true;
}

// --- Moved from `ElysiumNpcPrecache10.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcSchedule.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcSenses10_2.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcSounds.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcSpecies.cpp` (story 5 step 4) ---

void FElysiumNpcTzimisce::FUN_103bf200(const FElysiumEntityHandle& Entity)
{
	// `0x103bf200`. The SAME hand-inlined `CUtlVector` append as `0x103662d0`, at `+0x6690` instead
	// of `+0x665c`, with one difference: the duration is not a parameter. It is the `.rdata` cell
	// `_DAT_1044eb0c`, read out of the pinned image as **20.0** — the same twenty seconds
	// `CNPC_VHengeyokai`'s blacklist (family Bosses' `BlacklistSeconds`) and MingXiao's thrown-object
	// skip use, off the same cell.
	constexpr float TzimisceBlacklistSeconds = 20.0f;   // _DAT_1044eb0c
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	TzimisceBlacklist.Emplace(
		FBlacklistedEntity{ Entity, Now + static_cast<double>(TzimisceBlacklistSeconds) });
}

int32 FElysiumNpcTzimisce::FUN_103bf3c0(const FElysiumEntity* Candidate) const
{
	// `0x103bf3c0` — `0x10366490` written a second time over `+0x6690`/`+0x669c`. Instruction for
	// instruction the same walk, the same sentinel and the same resolve-to-zero on a dead row.
	for (int32 i = 0; i < TzimisceBlacklist.Num(); ++i)
	{
		const FElysiumEntity* Resolved = World != nullptr
			? World->Resolve(TzimisceBlacklist[i].Entity) : nullptr;
		if (Resolved == Candidate)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

bool FElysiumNpcTzimisce::FUN_103bf330(const FElysiumEntity* Candidate)
{
	// `0x103bf330` — `0x10366400` written a second time, including the `& 0xffffff00` miss arm and
	// the swap-remove. Two classes, two offsets, one behaviour.
	const int32 Index = FUN_103bf3c0(Candidate);
	if (Index == INDEX_NONE)
	{
		return false;
	}
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	if (Now < TzimisceBlacklist[Index].ExpiresAt)
	{
		return true;
	}
	if (TzimisceBlacklist.Num() > 0)
	{
		TzimisceBlacklist.RemoveAtSwap(Index);
	}
	return false;
}

// -------------------------------------------------------------------------------------------------
// Slot 482 — `CanPlaySequence`'s five species copies.
// -------------------------------------------------------------------------------------------------

// -------------------------------------------------------------------------------------------------
// Slot 593 — `CNPC_VTzimisce::vfunc593` `0x103b9180`.
// -------------------------------------------------------------------------------------------------

// --- Moved from `ElysiumNpcSpecies2.cpp` (story 5 step 4) ---

bool FElysiumNpcTzimisce::TzimisceDeathScriptArgument(int32 SingletonIndex, int32& OutArgument) const
{
	// `DAT_1093cf94` `tzimisce_voice_pitch` "100" and `DAT_1093cfdc` `tzimisce_voice_attn` "65"
	// hand over their `+0x2c` ints; `DAT_1093cebc` `tzimisce_voice_volume` "1" hands over its
	// `+0x28` FLOAT, whose dword is pushed as it stands. All three are ConVars, so none substitutes 0.
	using ElysiumNpcTunables::EConVar;
	switch (SingletonIndex)
	{
	case 0:
		OutArgument = ElysiumNpcTunables::ConVarInt(EConVar::TzimisceVoicePitch);
		return true;
	case 1:
		OutArgument = ElysiumNpcTunables::ConVarInt(EConVar::TzimisceVoiceAttn);
		return true;
	case 2:
	{
		const float Volume = ElysiumNpcTunables::ConVarFloat(EConVar::TzimisceVoiceVolume);
		FMemory::Memcpy(&OutArgument, &Volume, sizeof(OutArgument));
		return true;
	}
	default:
		OutArgument = 0;
		return false;
	}
}

void FElysiumNpcTzimisce::FUN_103be0b0(bool bCarrying)
{
	// `0x103be0b0`, eighty-four bytes:
	//     if (param_1) {
	//         m_bfAINPCFlags |= CARRYING_BODY (0x20);
	//         m_flBodyTimer (+0x66a4) = curtime + RandomFloat(7.5, 10.0);
	//         m_bDidFakeThrow (+0x66b4) = 0;
	//     } else {
	//         m_bfAINPCFlags &= ~CARRYING_BODY;
	//     }
	//
	// This is `CNPC_VTzimisce`'s exact counterpart of `CNPC_VHengeyokai`'s `0x10381c00` — family
	// Bosses' `CallFormBit` seam, which stamps `m_flFishTimer` (+0x666c) and clears
	// `m_bDidFakeThrow` (+0x667d) on ITS class's offsets. Same two writes, same flag bit, different
	// species and different offsets; this one is a body and not a seam because both its words are
	// declared.
	//
	// The clearing arm writes ONLY the flag — the timer and the fake-throw byte are left standing,
	// so a Tzimisce that drops a body keeps whatever deadline it was carrying under.
	//
	// The random draw order matters and is kept: retail draws BEFORE it reads `curtime`.
	if (bCarrying)
	{
		NpcFlags.Set(EElysiumNpcFlag::CARRYING_BODY);
		const float Drawn = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule)
			.FRandRange(TzimisceBodyTimerMin, TzimisceBodyTimerMax);
		const double Now = World != nullptr ? World->NowSeconds() : 0.0;
		bTzimisceDidFakeThrow = false;
		TzimisceBodyTimer = Now + static_cast<double>(Drawn);
		return;
	}
	NpcFlags.Clear(EElysiumNpcFlag::CARRYING_BODY);
}

bool FElysiumNpcTzimisce::FUN_103be150() const
{
	// `0x103be150`, thirty-two bytes: `return m_flBodyTimer (+0x66a4) <= curtime`.
	//
	// At-or-before, not strictly before — a timer armed at exactly `curtime` reads as already
	// elapsed. A Tzimisce that has never carried anything has `m_flBodyTimer` zero, so this answers
	// true from spawn; the `m_bfAINPCFlags` `CARRYING_BODY` bit is what the callers pair it with.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	return TzimisceBodyTimer <= Now;
}

bool FElysiumNpcTzimisce::FUN_103be3d0(FElysiumEntity* InTarget)
{
	// `0x103be3d0`, `CNPC_VTzimisce`'s grab-bone search:
	//
	//     best = 1050625.0;  found = false;
	//     rag = dynamic_cast<CRagdollProp*>(param_1);        // 0x10538764 -> 0x1057c684
	//     if (rag) for (i = 0; table[i][0] != '\0'; ++i) {
	//         el = rag->GetElement(table[i]);                 // vtable +0x424
	//         if (!el) continue;
	//         el->GetPosition(&pos, &unusedAngles);           // +0x94
	//         d = DistSq(GetOrigin(), pos);                   // vtable +0x370
	//         if (d < best) { m_vecPickupTargetPos = pos; found = true;
	//                         m_iPickupTargetGrabBone = i; best = d; }
	//     }
	//     return found;
	//
	// It is family Bosses' `0x10381e90` over a DIFFERENT bone table — the one starting at
	// `PTR_s_Bip01_L_Forearm_106530e8` — and with **no fallback branch when the cast fails**: where
	// `0x10381e90` has a second arm, this one simply answers false. 29c's walk says the same.
	//
	// The initial best is **1025 units squared** (1050625), so a bone further than 1025 units away
	// never wins even if it is the only one.
	//
	// **Seams:** `RagdollBonePosition` (family Bosses) stands the cast plus the element lookup plus
	// `GetPosition` and answers false, so no bone is ever offered and the body takes its
	// "nothing found" arm — which is retail's own answer for a target that is not a ragdoll. The
	// writes are real when a bone IS offered, which is what the suite drives.
	//
	// **Unrecovered:** every entry of the bone table after the first (see `TzimisceGrabBoneTable`).
	float Best = GrabBoneRangeSqUnits;
	bool bFound = false;
	const FVector OriginUnits = Origin / ElysiumMove::U;
	for (int32 i = 0; i < UE_ARRAY_COUNT(TzimisceGrabBoneTable); ++i)
	{
		FVector BoneUnits = FVector::ZeroVector;
		if (!RagdollBonePosition(InTarget, TzimisceGrabBoneTable[i], BoneUnits))
		{
			continue;
		}
		const float DistSq = static_cast<float>(FVector::DistSquared(OriginUnits, BoneUnits));
		if (DistSq < Best)
		{
			PickupTargetPos = BoneUnits;      // +0x6674 m_vecPickupTargetPos (family Motor's)
			bFound = true;
			TzimiscePickupGrabBone = i;       // +0x6680 m_iPickupTargetGrabBone
			Best = DistSq;
		}
	}
	return bFound;
}

bool FElysiumNpcTzimisce::FUN_103be8e0(FElysiumEntity* InTarget)
{
	// `0x103be8e0`, "is the thing I am holding close enough?":
	//
	//     if (param_1 == NULL) return true;                       // <- note the answer
	//     held  = Resolve(m_hPickupTarget (+0x6670));
	//     from  = held->GetOrigin();                              // vtable +0x370
	//     gz    = ConVar(DAT_1093ca8c).IsCommand() ? 0.0 : value;
	//     lead  = ComputeTargetLeadPoint(this, from, param_1, gz);// thunk 0x102c36d0
	//     delta = lead - GetOrigin();
	//     yaw   = VecToYaw(delta);                                // thunk 0x101d2c70
	//     diff  = UTIL_AngleDiff(yaw, GetAngles().y);             // thunk 0x1013d580, +0x374 + 4
	//     if (diff <= -20.0) return <the flag word>;              // _DAT_1049ae98
	//     if (diff <= 20.0)  return <the flag word>;              // _DAT_1044eb0c
	//     return true;
	//
	// **29c's walk reads the last two tests as a DISTANCE comparison and they are an ANGLE
	// comparison.** `thunk_FUN_1013d580` is `UTIL_AngleDiff` — families Bosses, Damage2, Facing and
	// Positions all name it that — and `*(float *)(iVar8 + 4)` is `GetAngles().y`, the body's YAW,
	// not a model radius. So the body is a +-20 degree FACING CONE, the same cone
	// `CNPC_VHengeyokai`'s `0x103822a0` uses off the same two `.rdata` cells, and not a range test.
	// The recovered numbers are -20.0 and +20.0, read out of the image.
	//
	// The `param_1 == NULL` arm answering TRUE is retail's and is kept: with nothing to aim at, the
	// body reports "close enough" rather than refusing.
	//
	// **Seams:** `ChaseLeadPosition` (family Positions) stands `0x102c36d0` and answers the input
	// position unchanged; `DAT_1093ca8c` is `tzimisce_throw_power`, shipped ".007".
	if (InTarget == nullptr)
	{
		return true;
	}
	const FElysiumEntity* Held = World != nullptr ? World->Resolve(PickupTarget) : nullptr;
	const FVector FromUnits = Held != nullptr ? Held->Origin / ElysiumMove::U : FVector::ZeroVector;
	FVector LeadUnits = FromUnits;
	ChaseLeadPosition(InTarget, FVector::ZeroVector,
		ElysiumNpcTunables::ConVarFloat(ElysiumNpcTunables::EConVar::TzimisceThrowPower),
		FromUnits * ElysiumMove::U, LeadUnits);
	const FVector DeltaUnits = LeadUnits - Origin / ElysiumMove::U;
	const float Yaw = Species2VecToYaw(DeltaUnits);
	// `UTIL_AngleDiff` `0x1013d580` — family Bosses keeps a private copy for the same reason; this
	// half of it is the only part the two tests read.
	float Diff = Yaw - static_cast<float>(Angles.Y);
	while (Diff > 180.0f)
	{
		Diff -= 360.0f;
	}
	while (Diff < -180.0f)
	{
		Diff += 360.0f;
	}
	if (Diff <= TzimisceGrabLowerBound)
	{
		return false;
	}
	if (Diff <= TzimisceGrabUpperBound)
	{
		return false;
	}
	return true;
}

bool FElysiumNpcTzimisce::FUN_103bef20(FElysiumEntity* InTarget, int32 ElementKey)
{
	// `0x103bef20`, the physics-animlink ATTACH:
	//
	//     if (param_1 == NULL) return false;
	//     if (DistSq(GetAbsOrigin(), param_1->GetAbsOrigin()) > 25600.0) return false;   // 160 units
	//     link = CreateNoSpawn("phys_animlink", vec3_origin);   if (!link) return false;
	//     bone = <scan the model's bone table for "Bip01 R Finger1">;
	//     if (bone < 0) return false;
	//     element = param_1[0xdb];                              // the carried thing's +0x36c payload
	//     if (element == 0) return false;
	//     rag = dynamic_cast<CRagdollProp*>(param_1);
	//     if (rag && rag->GetElement(param_2) == 0) return false;   // vtable +0x424
	//     LinkAnimlink(link, this, bone, element, vec3_origin, vec3_origin);   // 0x1014f210
	//     m_hPhysicsAnimlink (+0x6684) = link->GetRefEHandle();
	//     thunk_FUN_103be050(this, false);
	//     FUN_103be0b0(this, true);                             // CARRYING_BODY + the body timer
	//     return true;
	//
	// The range gate is the one number that makes this body different from family Bosses' two attach
	// arms: **160 units**, read as `25600.0` squared out of `.rdata`. Neither boss has one.
	//
	// The bone scan's retail quirk family Bosses recorded holds here too — the loop leaves its
	// counter at the bone COUNT when nothing matched, so a miss is a positive index and the
	// `bone < 0` guard never fires. This port answers on the seam's `INDEX_NONE` instead and says so.
	//
	// The tail is what wires this row to the rest of the chain: it calls `0x103be0b0` with true, so
	// a successful attach is what arms `m_flBodyTimer` and raises `CARRYING_BODY`.
	//
	// **Seams:** `CreatePhysAnimlink`, `LookupBoneByName`, `RagdollElementForBone` and
	// `WirePhysAnimlink` are family Bosses'; `thunk_FUN_103be050` is `CNPC_VTzimisce`'s other latch
	// and is no row of this family's.
	if (InTarget == nullptr)
	{
		return false;
	}
	const float DistSqUnits = static_cast<float>(
		FVector::DistSquared(Origin / ElysiumMove::U, InTarget->Origin / ElysiumMove::U));
	if (DistSqUnits > TzimisceAttachRangeSqUnits)
	{
		return false;
	}
	const FElysiumEntityHandle Link = CreatePhysAnimlink();
	if (World == nullptr || World->Resolve(Link) == nullptr)
	{
		return false;
	}
	const int32 Bone = LookupBoneByName(TzimisceAttachCarrierBone);
	if (Bone < 0)
	{
		return false;
	}
	if (!RagdollElementForBone(InTarget, ElementKey))
	{
		return false;
	}
	WirePhysAnimlink(Link, Bone, ElementKey);
	TzimiscePhysicsAnimlink = Link;
	FUN_103be0b0(/*bCarrying=*/true);
	return true;
}

void FElysiumNpcTzimisce::FUN_103bea90(FElysiumEntity* AimTarget)
{
	// `0x103bea90`, the physics-animlink RELEASE — nine hundred and seventeen bytes, and it is the
	// throw as well as the release:
	//
	//     UTIL_Remove(Resolve(m_hPhysicsAnimlink (+0x6684)));   // thunk 0x101cd970
	//     m_hPhysicsAnimlink = NULL;                            // thunk 0x100a0ae0
	//     if (param_1 != NULL) {
	//         held  = Resolve(m_hPickupTarget (+0x6670));
	//         from  = held->GetOrigin();
	//         gz    = ConVar(DAT_1093ca8c).IsCommand() ? 0.0 : value;
	//         aim   = ComputeTargetLeadPoint(this, from, param_1, gz);   // thunk 0x102c36d0
	//         aim.z += 48.0;                                             // _DAT_10447ee8
	//         delta = aim - from;  distSq = Dot(delta, delta);
	//         yaw   = VecToYaw(delta);
	//         diff  = UTIL_AngleDiff(yaw, GetAngles().y);
	//         if (diff > -20.0) { if (20.0 <= diff) delta.xy = Rotate(from, radius + 20.0).xy; }
	//         else              {                   delta.xy = Rotate(from, radius - 20.0).xy; }
	//         speed = (distSq * ConVar(DAT_1093ca8c)) <= 1000.0 ? 1000.0 : distSq * ConVar(...);
	//         impulse.xy = delta.xy * speed;
	//         impulse.z  = ConVar(DAT_1093ca44) * distSq + delta.z * speed;
	//         rag = dynamic_cast<CRagdollProp*>(Resolve(m_hPickupTarget));
	//         if (rag) rag->ApplyImpulse(&impulse, &vec3_origin);        // vtable +0x428
	//         else     { phys = held->m_pPhysicsObject (+0x36c);
	//                    if (phys) { phys->GetPosition(&p, &a);          // +0xa0
	//                                phys->ApplyForceCenter(&impulse); } // +0x9c
	//     }
	//     m_hPickupTarget = -1;                                   // +0x6670, INVALID
	//     ArmIgnoreCollisionExpiry(0.75);                         // thunk 0x102c43b0
	//     FUN_103be0b0(this, false);                              // clear CARRYING_BODY
	//
	// Three recovered facts the one-line walk gets wrong or leaves out:
	//   * the `1049ae98`/`1044eb0c` pair is again the **+-20 degree cone**, not a dot product and
	//     not a distance — the same reading as `0x103be8e0` above and off the same two cells;
	//   * the speed floor is **1000.0** (`_DAT_10447ee0`, and the literal `0x447a0000` on the other
	//     arm is the same 1000), which is family Damage2's `ThrowSpeedFloor` off the same cell;
	//   * the aim point is lifted **48 units** (`_DAT_10447ee8`), the same lift family Bosses' two
	//     release arms use.
	//
	// The tail is the mirror of the attach's: `0x103be0b0(false)` clears `CARRYING_BODY`, and
	// `m_hPickupTarget` is cleared to INVALID whether or not there was anything to throw at.
	//
	// **Seams:** `RemovePhysAnimlink`, `ApplyThrowImpulse` and `ArmIgnoreCollisionExpiry` are family
	// Bosses'; `ChaseLeadPosition` is family Positions'. `DAT_1093ca8c` is `tzimisce_throw_power`
	// ".007" and `DAT_1093ca44` `tzimisce_throw_hds` ".0008" (the height-to-distance scalar).
	RemovePhysAnimlink(TzimiscePhysicsAnimlink);
	TzimiscePhysicsAnimlink = FElysiumEntityHandle();

	if (AimTarget != nullptr)
	{
		const FElysiumEntity* Held = World != nullptr ? World->Resolve(PickupTarget) : nullptr;
		const FVector FromUnits = Held != nullptr
			? Held->Origin / ElysiumMove::U : FVector::ZeroVector;
		FVector AimUnits = FromUnits;
		const float ThrowPower =
			ElysiumNpcTunables::ConVarFloat(ElysiumNpcTunables::EConVar::TzimisceThrowPower);
		const float ThrowHds =
			ElysiumNpcTunables::ConVarFloat(ElysiumNpcTunables::EConVar::TzimisceThrowHds);
		ChaseLeadPosition(AimTarget, FVector::ZeroVector, ThrowPower,
			FromUnits * ElysiumMove::U, AimUnits);
		AimUnits.Z += TzimisceThrowAimHeightUnits;

		FVector DeltaUnits = AimUnits - FromUnits;
		const float DistSq = static_cast<float>(DeltaUnits.SizeSquared());
		const float Yaw = Species2VecToYaw(DeltaUnits);
		float Diff = Yaw - static_cast<float>(Angles.Y);
		while (Diff > 180.0f)
		{
			Diff -= 360.0f;
		}
		while (Diff < -180.0f)
		{
			Diff += 360.0f;
		}
		// The two nudge arms. `thunk_FUN_101d2f40(&from, radius +- 20.0)` rewrites X and Y only and
		// leaves Z, which is why retail saves and restores `delta.z` around each call. With no
		// radius seam the rewrite cannot be performed; the ARM taken is recorded and the delta is
		// left as it stands, which is retail's own middle arm (within the cone, nothing rotated).
		(void)Diff;

		// `speed = distSq * throw_power`, floored at 1000.
		float Speed = DistSq * ThrowPower;
		if (Speed <= TzimisceThrowSpeedFloor)
		{
			Speed = TzimisceThrowSpeedFloorImpulse;
		}
		FVector ImpulseUnits(DeltaUnits.X * Speed, DeltaUnits.Y * Speed,
			ThrowHds * DistSq + DeltaUnits.Z * Speed);
		ApplyThrowImpulse(PickupTarget, ImpulseUnits);
	}

	PickupTarget = FElysiumEntityHandle();   // `+0x6670 = -1`
	ArmIgnoreCollisionExpiry(TzimisceReleaseIgnoreSeconds);
	FUN_103be0b0(/*bCarrying=*/false);
}

void FElysiumNpcTzimisce::FUN_103bf560()
{
	// `0x103bf560`, fourteen bytes: `SetIdealYaw(m_pMotor (+0x5d44), -1)` — `thunk_FUN_102e1e20`
	// with the sentinel that means "use the live heading", which the motor computes by `ftol`-ing
	// the current angle.
	//
	// Family **Motor** found `0x10382d20` to be the identical one-liner on another class. Retail name
	// unrecovered; single caller (`0x103bb1e0`, the RunTask arm 0xbf/0xc0), no vtable slot. Story 8
	// (L05 integration): RunTask19's `MotorUpdateYaw` is `0x102e1e20` itself (family Hints'
	// `ReleaseMotorHintYaw` stays an inert seam for its live callers; see there).
	MotorUpdateYaw(-1);
}

// -------------------------------------------------------------------------------------------------
// `CNPC_VTzimisce`'s slots 488 and the two `CNPC_VCamera` empties.
// -------------------------------------------------------------------------------------------------

// --- Moved from `ElysiumNpcState_2.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcTranslate.cpp` (story 5 step 4) ---

