#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumOverlayStack.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"

// Story 29c-1, family **EntityChain** — the 57 unnamed bodies of the entity chain BELOW the NPC,
// `order.md` layers 0–9. The walked prose is `docs/vtmb/npc-ai/shape.md`.
//
// What the family turned out to BE, once the decompiled C was read rather than summarised: eight
// classes, not one. `CBaseEntity` (the network change-state, solidity, standability, the physics
// tick), `CBaseAnimating`/`CBaseAnimatingOverlay`/`CBaseFlex` (the gesture-layer, flinch, flex and
// scene-event slots — whose TABLES family Anim already ported, so these six slot bodies drive that
// family's members), `CBaseCombatCharacter`, **`CBasePlayer`** (half the family: law levels, the
// police response, the heightened alert, the closest-NPC cache, autoaim, the held use entity, the
// camera-override crossfade, the controller-NPC detach), `CCineNPC`, `CDialog`, `CGlobalEntityList`
// and `CPayphone`.
//
// `vtmb_fields CBasePlayer` NAMED most of what 29c's walk left unrecovered — `m_hClosestNPC`,
// `m_LevelSupernaturalAct`, `m_iCopsInPursuitCount`, `m_flSpawnResponseCopsTimer`, `m_vecAutoAim`,
// `m_fOnTarget`, `m_hControllerNPC`, `m_hCameraTargetEntity` — so those port methods carry the
// recovered name. Four rows still do not settle and keep 29c's `FUN_<address>` spelling.
//
// The `CBasePlayer` half runs over `ChainPlayer()`, the ONE player this runtime stands, and where
// that player ALREADY carries the word (`Law`, `Police`, `ScareQueue`, `Observer`) the body reads
// and writes THAT member. Nothing here stands a second copy of a rule.

namespace
{
	// ---------------------------------------------------------------------------------------------
	// The recovered `.rdata` constants this family reads, each with where its value was settled.
	// Unit-prefixed because the module builds adaptive-unity and this anonymous namespace is
	// regularly merged with others.
	// ---------------------------------------------------------------------------------------------

	// `_DAT_104454c4` — the image's shared `0.0f` (1,328 readers, no writer;
	// `docs/vtmb/npc-ai/shape.md` § slot 568).
	constexpr float GChainZero = 0.0f;
	// `_DAT_104454c0` — the image's shared `1.0f` (`docs/vtmb/animation_and_movers.md` line 659
	// reads the same word as `1.0f`; `docs/vtmb/npc-ai/shape.md` line 1029 "clamped up to
	// `_DAT_104454c0 = 1.0`").
	constexpr float GChainOne = 1.0f;
	// `_DAT_104454c8` = **80.0f** (`docs/vtmb/computer-terminals.md` line 475, `docs/vtmb/npc-ai/
	// conditions-and-states.md` line 460: "farther than 80 units (`_DAT_104454c8`)"). This is slot
	// 37's whole answer, which the ledger's `checklist-0-9.md` records as unrecovered; recovered
	// here from the same word's other readers, exactly as that row predicted it would be.
	constexpr float GChainSlot37 = 80.0f;
	// `_DAT_1045d650` = **1024.0f** (`docs/vtmb/computer-terminals.md` line 1508,
	// `docs/vtmb/npc-ai/conditions-and-states.md` line 885). Slot 550 `CoverRadius`'s answer.
	constexpr float GChainCoverRadius = 1024.0f;
	// `_DAT_10457f54` = **0.7f** (`docs/vtmb/computer-terminals.md` lines 524 and 1729,
	// `docs/vtmb/npc-ai/senses.md` line 401). The NEW-sample weight of the autoaim blend.
	constexpr float GChainAutoaimNewWeight = 0.7f;
	// `0x47c34ff3` = **100000.0f**, the "nothing is near" distance `UpdateClosestNpc` resets
	// `m_flClosestNPCDist` to. Read straight off the immediate in the decompilation.
	constexpr float GChainClosestNpcReset = 100000.0f;

	// Retail's `SolidType_t` values the two standability bodies test by number.
	constexpr int32 GChainSolidBsp = 1;       // SOLID_BSP
	constexpr int32 GChainSolidBbox = 2;      // SOLID_BBOX
	constexpr int32 GChainSolidVPhysics = 6;  // SOLID_VPHYSICS
	// `FSOLID_NOT_SOLID`, the `GetSolidFlags()` bit slot 164 refuses on.
	constexpr int32 GChainSolidNotSolid = 0x10;

	// `MoveType_t` as slot 226 switches on it. 7 takes the read-back-from-physics arm; 1 and 8 take
	// `VPhysicsUpdatePusher`. Troika's enum is NOT stock Source's here, so the numbers are carried
	// as numbers and the names are not claimed.
	constexpr int32 GChainMoveTypePhysicsRead = 7;
	constexpr int32 GChainMoveTypePusherA = 1;
	constexpr int32 GChainMoveTypePusherB = 8;

	// `FL_AIMTARGET`, the `GetFlags()` bit both autoaim bodies screen on and the only thing either
	// of them raises `m_fOnTarget` for.
	constexpr int32 GChainFlAimTarget = 0x10000;

	// `AutoaimDeflection`'s trace distance, the `0x46800000` immediate `GetAutoaimVector` pushes.
	constexpr float GChainAutoaimDistance = 16384.0f;
	// The four clamps, read as literal floats out of the decompilation (`25.0` / `-25.0` are
	// spelled as immediates; `_DAT_10450568` is `360.0` and `_DAT_1044c3a8` / `_DAT_10462948` are
	// the `180.0` / `-180.0` the wrap tests against).
	constexpr float GChainAutoaimWrap = 360.0f;
	constexpr float GChainAutoaimHalfWrap = 180.0f;
	constexpr float GChainAutoaimPitchClamp = 25.0f;
	constexpr float GChainAutoaimYawClamp = 12.0f;

	// `NPC_STATE` ids `FixScriptNpcSchedule` reads and writes (`0x1026e3e0`'s own cases).
	constexpr int32 GChainNpcStateIdle = 1;
	constexpr int32 GChainNpcStateDead = 7;
	// The `__LINE__` retail stamps into `m_SelectIdealStateTrace.m_iLine` on the way out: `0x3ca`.
	constexpr int32 GChainFixScriptScheduleLine = 970;

	// `0x101a6420`'s identity passthrough needs somewhere to point when the caller supplies null;
	// retail returns the caller's own pointer, so there is no such place and the port answers null
	// too. Declared for readability at the one site.

	// The `CPayphone#612` speech sound flags.
	constexpr int32 GChainPayphoneFlagsFinal = 0xa80;
	constexpr int32 GChainPayphoneFlagsNotFinal = 0xe80;
	// `CPayphone#35`'s capability bitmask.
	constexpr int32 GChainPayphoneUseCaps = 0x2f;
	// `0x10182a90`'s fixed "over threshold" answer.
	constexpr int32 GChainClosestNpcOverThreshold = 0x264;

	// The two record offsets `FireGlobalActsOutput` is called with, so a reader can join them back
	// to `thunk_FUN_1023dcd0`'s object.
	constexpr int32 GChainAlertEndOutput = 0x480;
	constexpr int32 GChainAlertBeginOutput = 0x498;

	// The five literals `0x1017ddd0` folds `m_LevelCriminalAct`'s stored word through.
	constexpr uint32 GChainObfAnd0 = 0x08a66e35u;
	constexpr uint32 GChainObfXor0 = 0x00793f90u;
	constexpr uint32 GChainObfAdd = 0x08b10412u;
	constexpr uint32 GChainObfAnd1 = 0x175991cau;
	constexpr uint32 GChainObfXor1 = 0x783682a9u;
}

// -------------------------------------------------------------------------------------------------
// The `CBasePlayer` half of the chain, and the seams.
// -------------------------------------------------------------------------------------------------

FElysiumPlayer* FElysiumNpc::ChainPlayer() const
{
	// Retail's `this` for every `+0x19b8`..`+0x22b0` body in this file. Null on a worldless probe
	// entity and in a headless world with no player, which every caller has retail's own null arm
	// for.
	return World != nullptr ? World->FindPlayer() : nullptr;
}

bool FElysiumNpc::PhysicsObjectPosition(const void* PhysicsObject, FVector& OutOrigin,
	FRotator& OutAngles) const
{
	// SEAM for `IPhysicsObject::GetPosition(&origin, &angles)` (the `+0x94` dispatch inside
	// `0x100b4f30`). No `IPhysicsObject` in this substrate; the generated slot signature hands the
	// pointer in as `void*` and nothing can be read off it.
	(void)PhysicsObject;
	OutOrigin = FVector::ZeroVector;
	OutAngles = FRotator::ZeroRotator;
	return false;
}

void FElysiumNpc::VPhysicsUpdatePusher(const void* PhysicsObject)
{
	// SEAM for `CBaseEntity::VPhysicsUpdatePusher(physicsObject)`, the arm movetypes 1 and 8 take.
	(void)PhysicsObject;
	PhysicsUpdateCalls.Add(TEXT("VPhysicsUpdatePusher"));
}

FElysiumEntity* FElysiumNpc::EntityOfEdict(const void* Edict) const
{
	// SEAM for `edict->m_pNetworkable (+0x40)->GetBaseEntity() (+0x10)`. There are no edicts here.
	// Answering null is not a refusal of slot 165: retail's own null arm dispatches slot 166 with
	// 0, and so does this.
	(void)Edict;
	return nullptr;
}

bool FElysiumNpc::PhysicsObjectIsStandable(const FElysiumEntity& Entity) const
{
	// SEAM for `(*DAT_1070b250 + 0x18)(entityIndex)`, the physics-environment query `0x100b5110`
	// makes for a `SOLID_VPHYSICS` entity. False is retail's answer for an object that is awake and
	// moving, which is what an unmodelled physics world stands for.
	(void)Entity;
	return false;
}

void* FElysiumNpc::PythonInteropObject() const
{
	// SEAM for `DAT_1072b360`, slot 240's whole body. The global is the CPython interop side of the
	// entity — whatever the embedded interpreter last stored — and this runtime embeds none, so the
	// answer is the global's own pre-interpreter value.
	return nullptr;
}

bool FElysiumNpc::HeadBonePosition(const FElysiumEntity& Speaker, FVector& OutWorld) const
{
	// SEAM for `CBaseAnimating::LookupBone("bip01_head")` followed by slot 192 (`+0x300`) for the
	// resolved bone's world position. Family Anim already records that this runtime's kernel stands
	// no bone table; the same refusal under this family's name.
	(void)Speaker;
	OutWorld = FVector::ZeroVector;
	return false;
}

float FElysiumNpc::SoundDurationOf(const TCHAR* SoundName) const
{
	// SEAM for `(*DAT_1070b248 + 0x30)(name)`, the sound-length query `0x10182c40` adds to its
	// animation deadline. The kernel reaches no sound catalogue.
	(void)SoundName;
	return 0.f;
}

void FElysiumNpc::ChainEntityList(TArray<FElysiumEntity*>& Out) const
{
	// `AutoaimDeflection` walks edict indices `1 .. gpGlobals->maxEntities`, skipping the free-slot
	// byte at `edict+0x4c` and hopping each survivor's networkable to its `CBaseEntity`.
	//
	// **Named modernization**: this runtime is not an edict array. The walk is over the world's live
	// entity list instead, which is the same SET retail's walk survives to — every non-free edict
	// with a bound entity — in the world's own order rather than in edict order. Retail's order is
	// observable only through which of two EQUALLY well-aligned candidates wins, and the body's
	// comparison is a strict `<=` on the alignment score, so the first of a tie wins in both.
	Out.Reset();
	if (World == nullptr)
	{
		return;
	}
	for (const TUniquePtr<FElysiumEntity>& Entity : World->Entities())
	{
		if (Entity.IsValid() && !Entity->IsDead())
		{
			Out.Add(Entity.Get());
		}
	}
}

FElysiumEntity* FElysiumNpc::TraceAimRay(const FVector& Src, const FVector& Dir,
	float Distance) const
{
	// SEAM for `UTIL_TraceLine(src, src + dir * dist, MASK_SHOT, this, COLLISION_GROUP_NONE, &tr)`
	// and `tr.m_pEnt`. This substrate's trace surface answers world geometry, not entities, so the
	// ray hits nothing that takes damage — which is retail's own arm, and the arm that then walks
	// the entity list.
	(void)Src;
	(void)Dir;
	(void)Distance;
	return nullptr;
}

bool FElysiumNpc::GameRulesAllowAutoTargetCrosshair() const
{
	// SEAM for `(*DAT_1070ba0c + 0xa0)()`. TRUE deliberately: false is the arm that CLEARS
	// `m_fOnTarget`, and a missing rules object must not clear a flag retail only clears when the
	// rules say so.
	return true;
}

bool FElysiumNpc::GameRulesAutoAimEnabled() const
{
	// SEAM for `GetAutoAimMode() > AUTOAIM_NONE` (`(*DAT_1070ba0c + 0x58)` and the
	// `thunk_FUN_101764d0()` gate both autoaim bodies open with). FALSE is `AUTOAIM_NONE`: autoaim
	// off, which is the shipped default for a mouse-aimed single-player game.
	return false;
}

bool FElysiumNpc::GameRulesAllowsAutoAimAt(const FElysiumEntity& Candidate) const
{
	// SEAM for `(*DAT_1070ba0c + 0x80)(this, edict)`, the per-candidate admission inside the walk.
	(void)Candidate;
	return false;
}

bool FElysiumNpc::HasStudioModel(const FElysiumEntity& Candidate) const
{
	// SEAM for `CBaseAnimating::GetModelPtr()`. Family Anim's flex seam records the same fact: the
	// animating tier stands no studio header. FALSE is retail's arm for a candidate with no model,
	// and it is the arm that refuses to cache it.
	(void)Candidate;
	return false;
}

bool FElysiumNpc::ClosestNpcCandidateRefused(const FElysiumEntity& Candidate) const
{
	// SEAM for `thunk_FUN_100b5190(candidate)`, whose TRUE answer refuses an otherwise accepted
	// candidate. **Unrecovered** predicate; false is the accepting arm.
	(void)Candidate;
	return false;
}

float FElysiumNpc::ClosestNpcPerception(const FElysiumEntity& Npc) const
{
	// SEAM for `thunk_FUN_1029c970(npc)` and the two folds `thunk_FUN_1029c9f0(v, player)` /
	// `thunk_FUN_1029ca30(v)`. The port's stealth surface publishes its own COMMITTED observation
	// meter (`FElysiumPlayer::Observer.Meter`, `docs/vtmb/stealth.md`: "nothing that reads it may
	// run range, cone, trace, memory or enemy work"), so the kernel reads that rather than
	// recomputing a second perception here.
	(void)Npc;
	const FElysiumPlayer* Player = ChainPlayer();
	return Player != nullptr ? Player->Observer.Meter : 0.f;
}

int32 FElysiumNpc::ClosestNpcSenseScalar() const
{
	// SEAM for `thunk_FUN_101e9000(0x10739d08)`. NAME and DEFAULT **unrecovered** — the pointer
	// lives in uninitialised `.data` and no corpus function constructs it. 0 zeroes the scaled term.
	return 0;
}

void FElysiumNpc::FireGlobalActsOutput(int32 RecordOffset)
{
	// `thunk_FUN_100cd660(singleton + <offset>, this, this, 0)` — a `COutputEvent::FireOutput` on a
	// record hanging off the singleton `thunk_FUN_1023dcd0()` resolves.
	//
	// RECOVERED, by joining this walk to `docs/vtmb/player-entity.md` § "Law, Masquerade and world
	// response", which story 16 read from the consumer side: the two records are the cop ALERT
	// edges. `+0x498` is `OnStartCopAlertMode`, fired beside the `m_bInHeightenedAlert` /
	// `m_flHeightenedAlertExpireTimer` write; `+0x480` is `OnEndCopAlertMode`, fired beside the
	// timer clear. The singleton is the game-rules object those outputs live on.
	//
	// **SEAM, and deliberately a recording one**: this runtime ALREADY fires both edges, from
	// `Substrate/ElysiumLaw.cpp`'s own pass. Firing them a second time from the kernel body would
	// double-fire an authored output, which is a real behavioural difference — so the kernel records
	// the call and names the output rather than duplicating story 16's transport.
	UnrecoveredChainCalls.Add(FString::Printf(TEXT("0x1023dcd0+0x%x FireOutput"), RecordOffset));
}

bool FElysiumNpc::HeightenedAlertDurationCvar(float& OutSeconds) const
{
	// `DAT_10725f74` is **`debug_heightened_alert_expire_time`** — recovered by joining this walk to
	// `docs/vtmb/player-entity.md` § "Law, Masquerade and world response", which names the ConVar
	// this body's timer is scheduled from.
	//
	// **SEAM**: the kernel stands no ConVar table, so `IsCommand()` answers true and retail's own
	// `_DAT_104454c4` = 0.0 arm runs — the alert expires the instant it is armed. That is the
	// recovered refusal and not a chosen duration; the DURATION lives in `Substrate/ElysiumLaw.h`'s
	// own copy of the same cvar.
	OutSeconds = GChainZero;
	return false;
}

bool FElysiumNpc::SpawnResponseCopsDelayCvars(float& OutLow, float& OutHigh) const
{
	// SEAM for `DAT_10725894` (the high bound) and `DAT_107257bc` (the low bound). Both NAMES
	// **unrecovered**. Retail's `IsCommand()` arms write literal `0` and `_DAT_104454c4` = 0.0, so
	// the draw is over an empty range.
	OutLow = GChainZero;
	OutHigh = GChainZero;
	return false;
}

bool FElysiumNpc::ActLevelOverrideCvar(int32 CvarId, int32& OutValue) const
{
	// SEAM for `DAT_10724ffc` / `DAT_1072594c` / `DAT_107250d4`. All three NAMES **unrecovered**.
	// Retail's shape is `IsCommand()` false plus a NEGATIVE `m_nValue` (`+0x2c`, word 0xb) meaning
	// "no override", which is the arm that reads the stored field. This answers that arm: the cvar
	// is not a command and its value is -1.
	(void)CvarId;
	OutValue = -1;
	return false;
}

// -------------------------------------------------------------------------------------------------
// `CBaseEntity` — the slots.
// -------------------------------------------------------------------------------------------------

float FElysiumNpc::Slot37()
{
	// 0x10026710 — the whole body is `return (float10)_DAT_104454c8;`.
	//
	// The ledger's `checklist-0-9.md` records the value as unrecovered and names where it would be
	// recovered ("UpdateEnemyPos 0x10271900 and UpdateTargetPos 0x10271b10 read the same constant").
	// It is **80.0f**: `docs/vtmb/npc-ai/conditions-and-states.md` line 460 reads the same word as
	// "farther than `_DAT_104454c8 = 80.0` units from the goal point", and
	// `docs/vtmb/computer-terminals.md` line 475 as `CPropDoorknob`'s 80-unit break-off.
	return GChainSlot37;
}

FElysiumEntity* FElysiumNpc::Slot38(FElysiumEntity* Other)
{
	// 0x10026730 — `return this;`, ignoring the argument. The declared signature really is
	// `CBaseEntity* vfunc38(CBaseEntity*)`, so this is a genuine always-answers-itself default and
	// not a decompiler artefact: what the caller passes never reaches anything.
	(void)Other;
	return this;
}

void FElysiumNpc::SetAngles(float Pitch, float Yaw, float Roll)
{
	// 0x10026a50, slot 65 — the three-scalar overload. The BODY is the packing: it lays the three
	// scalars into one stack record and dispatches slot 64 (`+0x100`) with its address. The vtable
	// hop is retail's, so a species that replaced slot 64 is reached through it, and this is the
	// same body on 501 classes.
	SetAngles(FRotator(Pitch, Yaw, Roll));
}

void FElysiumNpc::Slot89()
{
	// 0x10026b70 — tail-jumps into `0x10146790`, which clears bytes +1 and +2 of the
	// `m_NetworkChangeState` record at `+0x01b0` (`docs/vtmb/npc-kernel/layout.md`): `m_bChanged`
	// and the second flag. The interval and the countdown at +4/+6 are NOT touched, and neither is
	// byte +0, which the static prop/brush Spawns own.
	NetworkChangeState.bChanged = false;
	NetworkChangeState.bByte2 = false;
}

bool FElysiumNpc::IsStandableSolid() const
{
	// 0x100b5110, the helper slots 159 and 164 both end in.
	//
	//   GetSolid() == SOLID_BSP                          -> true
	//   GetSolid() == SOLID_VPHYSICS and the physics
	//     environment says the object is standable       -> true
	//   anything else                                    -> false  (retail returns the movetype
	//                                                       word with its low byte zeroed, which
	//                                                       IS false and nothing else)
	//
	// Retail re-dispatches slot 92 for the second test rather than caching the first answer; the
	// port keeps the two calls so a species that answered differently on the second would be read
	// differently here too.
	if (GetSolid() == GChainSolidBsp)
	{
		return true;
	}
	if (GetSolid() == GChainSolidVPhysics)
	{
		return PhysicsObjectIsStandable(*this);
	}
	return false;
}

bool FElysiumNpc::ReflectGauss()
{
	// 0x10026f20, slot 159 — `IsStandableSolid() && m_takedamage == 0`. Both terms, in retail's
	// order; `m_takedamage` is `+0x01fc`, family Damage's `TakeDamageMode`, and `0` is `DAMAGE_NO`.
	// So the answer is "a solid I could stand on that takes no damage" — world brush, not a body.
	return IsStandableSolid() && TakeDamageMode == 0;
}

bool FElysiumNpc::IsStandable()
{
	// 0x100b50a0, slot 164.
	//
	//   GetSolidFlags() & FSOLID_NOT_SOLID -> false, immediately
	//   GetSolid() is SOLID_BSP, SOLID_VPHYSICS or SOLID_BBOX -> true
	//   otherwise -> the shared helper
	//
	// The three solid tests are three SEPARATE dispatches of slot 92 in retail, in the order
	// 1, 6, 2, and the port keeps them that way. Note the asymmetry with the helper: slot 164 takes
	// `SOLID_VPHYSICS` as standable OUTRIGHT, while the helper asks the physics object — so a moving
	// physics prop is standable to slot 164 and not to `ReflectGauss`.
	if ((GetSolidFlags() & GChainSolidNotSolid) != 0)
	{
		return false;
	}
	if (GetSolid() == GChainSolidBsp)
	{
		return true;
	}
	if (GetSolid() == GChainSolidVPhysics)
	{
		return true;
	}
	if (GetSolid() == GChainSolidBbox)
	{
		return true;
	}
	return IsStandableSolid();
}

bool FElysiumNpc::CanStandOn(void* Edict)
{
	// 0x10026fb0, slot 165 — the `edict_t*` overload. It selects an ARGUMENT and dispatches slot 166
	// (`+0x298`), which is the `CBaseEntity*` overload and family Motor's body: the networkable at
	// `edict+0x40` if the edict and the networkable are both non-null, else literal 0. Retail
	// dispatches slot 166 on BOTH arms — a null edict is not a refusal, it is `CanStandOn(nullptr)`.
	if (Edict != nullptr)
	{
		return CanStandOn(EntityOfEdict(Edict));
	}
	return CanStandOn(static_cast<FElysiumEntity*>(nullptr));
}

void FElysiumNpc::VPhysicsUpdate(void* PhysicsObject)
{
	// 0x100b4f30, slot 226 — retail's per-movetype physics-tick ordering, on slot 94's answer.
	//
	//   movetype 7 : read the object's transform back (`IPhysicsObject +0x94`), warn on any
	//                component whose exponent field is all-ones (`& 0x7f800000 == 0x7f800000`,
	//                i.e. inf or NaN), then SetAbsOrigin (slot 216), SetAbsAngles (slot 218),
	//                PhysicsTouchTriggers(0) and PhysicsRelinkChildren — IN THAT ORDER.
	//   movetype 1 or 8 : `CBaseEntity::VPhysicsUpdatePusher(object)`.
	//   anything else   : nothing at all.
	//
	// The warn is `Msg("Infinite values from vphysics!...")` and it does NOT abort the arm: retail
	// prints and then writes the bad transform anyway, which is a fact a program can observe.
	const int32 MoveType = GetMoveType();
	if (MoveType == GChainMoveTypePhysicsRead)
	{
		FVector PhysOrigin = FVector::ZeroVector;
		FRotator PhysAngles = FRotator::ZeroRotator;
		if (PhysicsObjectPosition(PhysicsObject, PhysOrigin, PhysAngles))
		{
			if (!FMath::IsFinite(PhysOrigin.X) || !FMath::IsFinite(PhysOrigin.Y)
				|| !FMath::IsFinite(PhysOrigin.Z))
			{
				// `Msg(s_Infinite_values_from_vphysics__105591dc)` — and then the write anyway.
				UE_LOG(LogElysiumNpcEnt, Warning, TEXT("Infinite values from vphysics! (%s)"),
					*DebugString());
			}
			SetAbsOrigin(PhysOrigin);
			SetAbsAngles(PhysAngles);
		}
		// The two calls run on this arm whether or not the transform read answered; retail's read
		// cannot fail, and the seam's refusal must not remove them from the sequence.
		PhysicsUpdateCalls.Add(TEXT("PhysicsTouchTriggers"));
		PhysicsUpdateCalls.Add(TEXT("PhysicsRelinkChildren"));
		return;
	}
	if (MoveType == GChainMoveTypePusherA || MoveType == GChainMoveTypePusherB)
	{
		VPhysicsUpdatePusher(PhysicsObject);
	}
}

void* FElysiumNpc::Slot240()
{
	// 0x1014f8b0, slot 240 — `return DAT_1072b360;`. One global word, not a literal, which is why
	// the generator could not emit it as a `default:`.
	return PythonInteropObject();
}

float FElysiumNpc::Slot135(float Interval)
{
	// 0x101c10d0, slot 135 — retail's named MOVE-REBOUND easing, and the whole body.
	//
	// Five gates, all of them in retail's order and all of them strict:
	//   Interval > 0, m_flMoveDoneTime > 0, m_flMoveReboundStartTime > 0,
	//   m_flMoveReboundStartTime < m_flMoveDoneTime, m_flMoveReboundDuration > 0.
	// Then `t = (Interval + m_flLocalTime) - m_flMoveReboundStartTime`, clamped ABOVE at the
	// duration and refused at or below zero (the early `return Interval`).
	//
	// The blend is `f(t) = (t*t + 1)*t - (t/D)*(D*D + 1)*t` — a cubic minus a linear, with the
	// linear term scaled so `f(D) == 0`. It is applied to the rebound velocity to produce an
	// OFFSET from the final destination, and the velocity written is that offset over `Interval`:
	//
	//   SetLocalVelocity((m_vecFinalDest + f(t)*m_flMoveReboundVelocity - GetLocalOrigin()) / Interval)
	//
	// and the same shape again for the angular half against `m_vecFinalAngle`, slot 221 and
	// `SetLocalAngularVelocity`. Each half runs only when its rebound triple is not all zero.
	//
	// The answer is ALWAYS `Interval`, on every path.
	//
	// SEAM: `m_flMoveDoneTime`, `m_flMoveReboundStartTime`, `m_flMoveReboundDuration`,
	// `m_flMoveReboundVelocity`, `m_flMoveReboundAngVelocity`, `m_vecFinalDest` and
	// `m_vecFinalAngle` are `CBaseEntity`'s mover words and no port member claims one
	// (`docs/vtmb/npc-kernel/layout.md`); they are read through `MoveReboundState()` below, which
	// answers the resting state and makes the first gate refuse. The arithmetic is stood as a static
	// so the formula is assertable without inventing a mover.
	FMoveRebound Rebound;
	if (!MoveReboundState(Rebound))
	{
		return Interval;
	}
	if (!(Interval > GChainZero && Rebound.MoveDoneTime > GChainZero
		&& Rebound.StartTime > GChainZero && Rebound.StartTime < Rebound.MoveDoneTime
		&& Rebound.Duration > GChainZero))
	{
		return Interval;
	}
	float T = (Interval + Rebound.LocalTime) - Rebound.StartTime;
	if (T <= GChainZero)
	{
		return Interval;
	}
	if (T > Rebound.Duration)
	{
		T = Rebound.Duration;
	}
	const float Blend = MoveReboundBlend(T, Rebound.Duration);

	if (Rebound.Velocity != FVector::ZeroVector)
	{
		const FVector Destination = Rebound.FinalDest + Blend * Rebound.Velocity;
		SetLocalVelocity((Destination - Rebound.LocalOrigin) * (GChainOne / Interval));
	}
	if (Rebound.AngVelocity != FVector::ZeroVector)
	{
		const FVector Destination = Rebound.FinalAngle + Blend * Rebound.AngVelocity;
		SetLocalAngularVelocity((Destination - Rebound.LocalAngle) * (GChainOne / Interval));
	}
	return Interval;
}

float FElysiumNpc::MoveReboundBlend(float T, float Duration)
{
	// The easing inside `0x101c10d0`, on its own so the formula is assertable:
	//     (t*t + 1) * t   -   (t / D) * (D*D + 1) * t
	// `f(0) == 0` and `f(D) == 0`, and it peaks between them — a rebound that leaves and returns.
	return (T * T + GChainOne) * T - (T / Duration) * (Duration * Duration + GChainOne) * T;
}

bool FElysiumNpc::MoveReboundState(FMoveRebound& Out) const
{
	// SEAM: `CBaseEntity`'s mover words (`m_flMoveDoneTime`, `m_flMoveReboundStartTime`,
	// `m_flMoveReboundDuration`, `m_flMoveReboundVelocity`, `m_flMoveReboundAngVelocity`,
	// `m_vecFinalDest`, `m_vecFinalAngle`) have no port member — nothing in this substrate stands a
	// `CBaseToggle`-style mover on the NPC line. The resting state is all zeroes, which makes slot
	// 135's first gate refuse, and refusing is what retail does for an NPC that is not rebounding.
	Out = FMoveRebound();
	Out.LocalTime = static_cast<float>(LocalTime);
	Out.LocalOrigin = Origin;
	Out.LocalAngle = Angles;
	return false;
}

void FElysiumNpc::SetLocalVelocity(const FVector& NewVelocity)
{
	// `CBaseEntity::SetLocalVelocity`, the write slot 135's linear half ends on. The port's
	// `FElysiumEntity::Velocity` IS that word.
	Velocity = NewVelocity;
}

void FElysiumNpc::SetLocalAngularVelocity(const FVector& NewAngularVelocity)
{
	// `CBaseEntity::SetLocalAngularVelocity`, the angular half's write. `AngularVelocity` is
	// `avelocity`.
	AngularVelocity = NewAngularVelocity;
}

// -------------------------------------------------------------------------------------------------
// `CBaseAnimatingOverlay` / `CBaseFlex` — the six slots over family Anim's tables.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::Slot266()
{
	// 0x100997f0, slot 266 — clear the three flinch records at `+0x07f4` (stride 0x1c). Two writes
	// per record and no more: the SEQUENCE to -1 and the EXPIRE TIME (word 6, `+0x18`) to
	// `curtime - 1.0`. The latch, the two fades and the pose parameter survive, which is why a
	// cleared record still remembers which pose parameter it drove.
	//
	// `_DAT_104454c0` is the image's shared `1.0f`, so the stamp is one second in the PAST: already
	// expired, on purpose.
	const float Now = World != nullptr ? static_cast<float>(World->NowSeconds()) : 0.f;
	for (int32 Index = 0; Index < NumFlinchRecords; ++Index)
	{
		Flinch[Index].Sequence = -1;
		Flinch[Index].ExpireTime = Now - GChainOne;
	}
}

void FElysiumNpc::SetLayer(int32 SlotIndex, int32 Activity, int32 Sequence, bool bAutoKill)
{
	// 0x10099020, slot 268 — `CBaseAnimatingOverlay::SetLayer`. Family Anim already ported this body
	// field for field as `SetOverlayLayer` (`ElysiumNpcKernelAnim.cpp`), because its own rows
	// dispatch through it and could not do so against a stub. This is the SLOT, and it is that body:
	// one spelling, so the two cannot drift.
	SetOverlayLayer(SlotIndex, Activity, Sequence, bAutoKill);
}

int32 FElysiumNpc::FirstGestureLayerOrRefusal() const
{
	// `GetFirstGestureLayer()` (slot 267, `0x10098a40`) answers 0 for every class in the hierarchy,
	// which family Anim recovered and every scan in that family relies on. Stated here rather than
	// dispatched, because slot 267's own port body is still 29c's stub and dispatching it would
	// report a stub for a fact that is already recovered.
	return 0;
}

int32 FElysiumNpc::FindLayerByOwner(int32 Activity)
{
	// 0x100994c0, slot 271 — `CBaseAnimatingOverlay::FindGestureLayer`. The scan STARTS at
	// `GetFirstGestureLayer()` and refuses outright when that is 4 or more; each slot is tested on
	// three terms in retail's order — the weight is not zero (`_DAT_104454c4`), the owner is not
	// `ACT_INVALID` (-1), and the owner is the activity asked for. -1 on a miss.
	//
	// Family Anim carries the scan as `FindGestureLayerByOwner`; this is the slot, and it is that
	// body behind retail's own starting index.
	if (FirstGestureLayerOrRefusal() >= ElysiumOverlay::NumSlots)
	{
		return INDEX_NONE;
	}
	return FindGestureLayerByOwner(Activity);
}

void FElysiumNpc::SetFlexWeight(int32 Index, float Value)
{
	// 0x100b5ba0, slot 279 — the INDEX overload, and the normalising half of the flex pair.
	//
	//   index < 0                      -> nothing
	//   index >= GetNumFlexControllers -> nothing
	//   no studio header               -> nothing
	//   max != min                     -> store (value - min) / (max - min)
	//   max == min                     -> store value unchanged
	//
	// Note what retail does NOT do: it does not clamp. A value outside the controller's authored
	// range stores outside 0..1 and slot 281 maps it straight back out again.
	//
	// Slot 281 (`GetFlexWeight(int)`, family Anim) is the exact inverse and reads the same range
	// through the same seam, so the round trip is lossless wherever the range is non-degenerate.
	if (Index < 0 || Index >= NumFlexControllers() || Index >= NumFlexWeightSlots)
	{
		return;
	}
	float Min = 0.f;
	float Max = 0.f;
	if (!FlexControllerRange(Index, Min, Max))
	{
		// Retail's `GetModelPtr()` null arm: the write is DROPPED, not stored raw.
		return;
	}
	FlexWeight[Index] = (Max != Min) ? (Value - Min) / (Max - Min) : Value;
}

void FElysiumNpc::ClearSceneEvents(void* Scene)
{
	// 0x100b5d80, slot 285. TWO bodies in one:
	//
	//   Scene == nullptr : the count at `+0x0a64` is set to 0 and NOTHING ELSE HAPPENS — no release,
	//                      no zeroing, no compaction. The records are still there; retail simply
	//                      stops counting them.
	//   Scene != nullptr : walk the array, and for every record whose SCENE (word 1) is this scene,
	//                      release the event (`0x10075b70`), zero words 0, 1 and the byte at 2,
	//                      `memmove` the tail down one stride and decrement both the count and the
	//                      cursor — so the compacted-in record is re-tested, which is what lets one
	//                      pass remove every event of a scene.
	if (Scene == nullptr)
	{
		// Retail's clear-all. The port's `TArray` has no separate count word, so emptying it is the
		// same observable state; `SceneEventsAllocated` is deliberately left alone, exactly as
		// retail leaves `+0x0a5c` alone.
		SceneEvents.Reset();
		return;
	}
	for (int32 Index = 0; Index < SceneEvents.Num(); )
	{
		if (SceneEvents[Index].Scene == static_cast<const FElysiumSceneData*>(Scene))
		{
			ReleaseSceneEvent(SceneEvents[Index]);
			SceneEvents.RemoveAt(Index);
			continue;   // retail's `iVar2 + -1` then `+1`: the cursor does not advance
		}
		++Index;
	}
}

void FElysiumNpc::RemoveSceneEvent(void* Event)
{
	// 0x100b6180, slot 287 — the same array keyed by POINTER EQUALITY on word 0 (the event), and
	// unlike slot 285 it stops at the FIRST match: the search loop returns, releases, compacts and
	// falls out. A second record carrying the same event would survive, which is a fact a program
	// can observe.
	//
	// A null `Event` is not special-cased here the way a null scene is in slot 285: it searches for
	// a record whose event pointer is null and removes the first one it finds.
	for (int32 Index = 0; Index < SceneEvents.Num(); ++Index)
	{
		if (SceneEvents[Index].Event == static_cast<const FElysiumSceneEvent*>(Event))
		{
			ReleaseSceneEvent(SceneEvents[Index]);
			SceneEvents.RemoveAt(Index);
			return;
		}
	}
}

void FElysiumNpc::ReleaseSceneEvent(const FSceneEventRecord& Record)
{
	// SEAM for `thunk_FUN_10075b70(record.event)`, the release both removers call before they
	// compact. The port's scene events are parsed data owned by the scene asset and are not
	// reference-counted, so nothing is released; recorded so the CALL is assertable.
	(void)Record;
	++SceneEventReleases;
}

// -------------------------------------------------------------------------------------------------
// The `CAI_BaseNPC` line's own unnamed slots.
// -------------------------------------------------------------------------------------------------

const FVector* FElysiumNpc::TranslateNavGoalPosition(const FVector* GoalPosition)
{
	// 0x101a6420, slot 410 — `return param_1;`. An IDENTITY PASSTHROUGH, not a fixed literal: the
	// base answer is whatever the caller supplied, so a caller that passes null gets null back. A
	// species that wants to move the goal overrides the slot.
	return GoalPosition;
}

int32 FElysiumNpc::GetLocalScheduleId(int32 GlobalId)
{
	// 0x101a6620, slot 447 — `0x102ea280(GetClassScheduleIdSpace(), id)`, the GLOBAL-to-LOCAL
	// direction of the range translation family Schedule ports the other half of. The walk is:
	// -1 stays -1; otherwise follow the chain at `+0x10`, and for the first space whose local base
	// is not the 9999 sentinel and whose `[globalBase, localTop]` range holds the id, answer
	// `(localBase - globalBase) + id`.
	return GlobalToLocalId(ClassScheduleIdSpace(), GlobalId);
}

int32 FElysiumNpc::GetLocalTaskId(int32 GlobalId)
{
	// 0x101a6640, slot 450 — the same call with the space pointer advanced by `+0x18`, which is the
	// TASK sub-space of the same `CAI_ClassScheduleIdSpace` (the schedule space is at +0x00, tasks
	// at +0x18, conditions at +0x30 and squad slots at +0x48; family Squad reaches +0x48 the same
	// way).
	//
	// SEAM: family Schedule's row carries the SCHEDULE sub-space only, and no row in this runtime
	// carries the task one — the port parses no schedule text, so all four sub-spaces are the empty
	// state the static constructor left. The translation therefore answers -1 for every id, which is
	// what it would answer over the real task space too.
	return GlobalToLocalId(nullptr, GlobalId);
}

int32 FElysiumNpc::GlobalToLocalId(const FScheduleIdSpace* Space, int32 GlobalId)
{
	// `0x102ea280`. A null space is retail's end-of-chain, which answers -1.
	if (GlobalId == INDEX_NONE || Space == nullptr)
	{
		return INDEX_NONE;
	}
	// `m_localBase != 9999 && m_globalBase <= id && id <= m_localTop`.
	if (Space->LocalBase != 9999 && Space->GlobalBase <= GlobalId && GlobalId <= Space->LocalTop)
	{
		return (Space->LocalBase - Space->GlobalBase) + GlobalId;
	}
	// SEAM: retail walks `space->m_pParent` at `+0x10`. `FScheduleIdSpace` carries no parent link —
	// family Schedule's rows are the static state and every one of them is the empty sentinel — so
	// the walk is one step long and falls off the end here.
	return INDEX_NONE;
}

bool FElysiumNpc::CanPlaySentence(bool bDisregardState)
{
	// 0x101a6840, slot 483 — forwards to slot 158 `IsAlive` (`+0x278`) AND DROPS ITS OWN ARGUMENT on
	// the way. `bDisregardState` never reaches the callee; a caller that passed true to mean "ask me
	// anyway" is answered exactly as one that passed false. That is a fact a program can observe and
	// it is ported as such, not tidied.
	(void)bDisregardState;
	return IsAlive();
}

float FElysiumNpc::CoverRadius()
{
	// 0x101a6c20, slot 550 — `return (float10)_DAT_1045d650;` = **1024.0f**
	// (`docs/vtmb/computer-terminals.md` line 1508 reads the same word as `1024.0`;
	// `docs/vtmb/npc-ai/conditions-and-states.md` line 885 repeats it). `CNPC_VPedestrian` and
	// `CNPC_VTzimisce` override it for real elsewhere; this is the line's own answer.
	return GChainCoverRadius;
}

bool FElysiumNpc::Slot579(int32 Argument)
{
	// 0x101a6ce0, slot 579 — `return slot158() == 0;`, i.e. the NEGATION of `IsAlive`, with its own
	// integer argument dropped exactly as slot 483 drops its bool. So slot 579 is "is this thing
	// dead", spelled as a wrapper rather than as a field read.
	(void)Argument;
	return !IsAlive();
}

void* FElysiumNpc::GetClassScheduleIdSpace()
{
	// 0x101aa790, slot 580 — `CAI_BaseNPCTroika`'s override, `return &DAT_10924248`.
	//
	// Family Schedule already stands the table of every class's id space and the chain walk that
	// picks this NPC's row (`ClassScheduleIdSpace()`), and its `.inl` records that slot 580's own
	// body "is still a generated stub ... not this story's to define". That stub is gone: this
	// family owns `0x101aa790`, so the slot is defined here and it IS that walk — the nearest
	// species override, else the Troika line's own row, which is the `&DAT_10924248` retail returns.
	return const_cast<FScheduleIdSpace*>(ClassScheduleIdSpace());
}

const FElysiumNpc::FScheduleIdSpace* FElysiumNpc::BaseClassScheduleIdSpace() const
{
	// 0x101a6d00 — slot 580's BASE body, `return &DAT_1090ff08`. It is a DIFFERENT id space from the
	// Troika line's `&DAT_10924248`, and family Schedule's table has no row for it, so the row is
	// stood here as a static.
	//
	// 49 dispatch sites reach slot 580 and every species subclass overrides it; the base is reached
	// only by the classes between `CAI_BaseNPC` and `CAI_BaseNPCTroika`, none of which is an entity
	// classname this runtime spawns. The row's ranges are the empty state the static constructor
	// left (`0x102ea090(isRoot = false)`), because nothing in the image registers a schedule into
	// `DAT_1090ff08`.
	static const FScheduleIdSpace GBaseRow = {
		TEXT("CAI_BaseNPC"), TEXT("0x101a6d00"), TEXT("0x1090ff08"), INDEX_NONE, 9999, INDEX_NONE };
	return &GBaseRow;
}

float FElysiumNpc::BaseHearingSensitivity() const
{
	// 0x101a67c0 — slot 476 `HearingSensitivity`'s base body, `return (float10)_DAT_104454c0;`.
	// `_DAT_104454c0` is the image's shared **1.0f**, so the base sensitivity is UNITY and
	// `CanHearSound`'s `volume * sensitivity` (`docs/vtmb/npc-ai/senses.md` § "radius =
	// HearingSensitivity") is the bare volume. Troika's own override (`0x101aa5f0`) reads `+0x63c0`
	// and is the body every spawned NPC actually gets; this is what the line under it answers.
	return GChainOne;
}

float FElysiumNpc::LastAiThink() const
{
	// 0x101a64a0 — `CBaseEntity::GetLastThink` for the FOURTH think channel, beside family
	// Lifecycle's `LastUpdateThink` / `LastNormalThink` / `LastMoveThink`. This runtime carries the
	// word as `FElysiumNpcScheduleHost::LastAI`.
	return static_cast<float>(ScheduleHost.LastAI);
}

// -------------------------------------------------------------------------------------------------
// `CPayphone` — three species bodies on a class this runtime's NPC leaf does not stand.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::PayphoneAddSceneEvent(const void* Scene, const void* Event)
{
	// 0x101aad90, `CPayphone#286` — the body is `return;` with both parameters ignored. Only
	// `CPayphone` fills slot 286 with it, so `default:void` does not formally apply and it lands as
	// a body: a payphone swallows every choreographed-scene event instead of queueing it the way
	// `CBaseFlex::AddSceneEvent` (family Anim's `AddSceneEventBase`) would.
	(void)Scene;
	(void)Event;
}

int32 FElysiumNpc::PayphoneSpeechSoundFlags() const
{
	// 0x101aadb0, `CPayphone#612`. Slot 612 is the speech sound FLAGS the line emitter
	// (`0x102c0520`) passes to `EmitSound` (`docs/vtmb/npc-kernel/signatures.md` slot 612).
	//
	//   bDialogQueIsFinal set   -> 0xa80
	//   bDialogQueIsFinal clear -> 0xe80
	//
	// Note the sign against the Troika line's own body (`0x102c04b0`), which returns `0x680` when
	// the flag is CLEAR and the partner is live: the payphone's two answers differ by `0x400` in the
	// opposite direction, so a payphone's LAST line is the quiet one and the Troika line's is not.
	return Dialogue.bDialogQueIsFinal ? GChainPayphoneFlagsFinal : GChainPayphoneFlagsNotFinal;
}

int32 FElysiumNpc::PayphoneUseCaps(FElysiumEntity* Other)
{
	// 0x101aa950, `CPayphone#35` — `return CanTalk(other) ? 0x2f : 0;`. The whole mask behind one
	// virtual: slot 295 (`+0x49c`), which is family 29d's `CanTalk`. `-(c != 0) & 0x2f` is the
	// compiler's branchless spelling of that conditional and nothing more.
	return CanTalk(Other) ? GChainPayphoneUseCaps : 0;
}

// -------------------------------------------------------------------------------------------------
// `CDialog` — two bodies of the dialogue file object.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::DialogLineHeadPosition(int32 LineIndex, FVector& OutWorld) const
{
	// 0x100e49b0. Four gates before anything happens, in retail's order:
	//   LineIndex >= 0, LineIndex < the line count (`+0x2834`), the dialog's SPEAKER handle
	//   (word 0 of the object) resolves live, and the line's flag byte (`+0x2810 + i`) has bit 0.
	// Then the line TYPE (`+0x2820 + i*4`) selects:
	//   5 and 6 -> `LookupBone("bip01_head")` on the speaker, then slot 192 for the bone's world
	//              position, then `thunk_FUN_101d26b0()`.
	//   anything else -> return, having done nothing.
	//
	// The two arms are BYTE-IDENTICAL in the decompilation — retail duplicated the block rather than
	// falling through — and both re-resolve the speaker handle a second time between the bone lookup
	// and the position fetch, which is why a speaker that died inside `LookupBone` reaches slot 192
	// through a NULL pointer. Ported as one arm because the two are the same instructions; the
	// re-resolve is reproduced, and here it cannot crash.
	OutWorld = FVector::ZeroVector;
	if (LineIndex < 0 || LineIndex >= DialogPcLines.Num())
	{
		return false;
	}
	FElysiumEntity* Speaker = World != nullptr ? World->Resolve(DialogSpeaker) : nullptr;
	if (Speaker == nullptr)
	{
		return false;
	}
	if ((DialogPcLines[LineIndex].Flags & 1) == 0)
	{
		return false;
	}
	const int32 Type = DialogPcLines[LineIndex].Type;
	if (Type != 5 && Type != 6)
	{
		return false;
	}
	// The second resolve, retail's own.
	Speaker = World->Resolve(DialogSpeaker);
	if (Speaker == nullptr)
	{
		return false;
	}
	return HeadBonePosition(*Speaker, OutWorld);
}

void FElysiumNpc::CallPendingDialogEventScript()
{
	// 0x100e4ef0. Run the 0x100-byte buffer at `CDialog+0x31ea` through `CDialog::CallEventScript`
	// if its first byte is non-zero, then zero-fill ALL 0x100 bytes — the clear is OUTSIDE the `if`,
	// so an empty buffer is still wiped.
	//
	// The buffer is filled by `CDialog::process_pc_line` (`0x100e8520`,
	// `Q_strncpy(this+0x31ea, this + line*0x228 + 0x2964, 0x100)`). It is a DIFFERENT field from the
	// one the port's `FElysiumDlgConversation::FlushPendingNpcAction` drains, which is the col-5 NPC
	// action; this one is the line's own event script.
	if (!PendingDialogEventScript.IsEmpty())
	{
		DialogEventScriptCalls.Add(PendingDialogEventScript);
	}
	PendingDialogEventScript.Empty();
}

// -------------------------------------------------------------------------------------------------
// `CGlobalEntityList` — the listener registry.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::AddListenerEntity(void* Listener)
{
	// 0x100f6d80 — `CGlobalEntityList::AddListenerEntity`. Called only ever on `&DAT_106eb5d8`, the
	// global entity list, by `CNavPropertyDatabase`, `CPhysSaveRestoreBlockHandler`,
	// `CEntityListSystem` and the aim-target manager (`0x102cd650`); `0x100f8700` walks the result
	// BACKWARDS calling each element's slot 1, which is what makes the elements listener objects.
	//
	// Three steps, in retail's order: a linear dedupe scan that RETURNS on a hit; a grow when the
	// count would exceed `+0x1804c`; and an insert at the end — retail computes a `memmove` for the
	// tail after the insertion point and then finds the length is zero, because the insertion point
	// IS the end. The mirror at `+0x18058` is rewritten with the block pointer on every append and
	// nothing reads it back.
	if (EntityListeners.Listeners.Contains(Listener))
	{
		return;
	}
	if (EntityListeners.Listeners.Num() + 1 > EntityListeners.Allocated)
	{
		EntityListeners.Allocated = EntityListeners.Listeners.Num() + 1;
	}
	EntityListeners.Listeners.Add(Listener);
	EntityListeners.DebugBlock = EntityListeners.Listeners.GetData();
}

void FElysiumNpc::RemoveListenerEntity(void* Listener)
{
	// 0x100f6e40 — the counterpart: a linear search by VALUE, a `memmove` compaction of the tail and
	// a decrement. A value the list does not hold is a silent no-op, and the capacity word is NOT
	// reduced.
	const int32 Index = EntityListeners.Listeners.Find(Listener);
	if (Index == INDEX_NONE)
	{
		return;
	}
	EntityListeners.Listeners.RemoveAt(Index);
}

// -------------------------------------------------------------------------------------------------
// `CCineNPC` — the scripted-sequence trio.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::CineIsTimeToStart() const
{
	// 0x101a7540 — `m_iDelay` (+0x5f70) below 1 **AND** `m_startTime` (+0x5f74) at or before
	// curtime.
	//
	// The SDK's `CAI_ScriptedSequence::IsTimeToStart` is `(m_iDelay <= 0 || curtime >= m_startTime)`
	// — an **OR**. Retail's is an AND, in the decompiled C, which means a beat with a delay never
	// starts by its start time alone and one with no delay still waits for it. A shipped divergence
	// from the SDK, ported as it stands.
	//
	// SEAM: `CCineNPC`'s own datamap words are not on this leaf — the port's scripted sequence is
	// `FElysiumScriptedSequence` and its beat clock is that entity's, not the NPC's — so both words
	// are read through `CineDelayState()` below, which answers the resting (0, 0) pair. Both terms
	// then hold, and the answer is TRUE: a beat with no authored delay is ready, which is retail's
	// own answer for `m_iDelay == 0, m_startTime == 0`.
	int32 Delay = 0;
	float StartTime = 0.f;
	CineDelayState(Delay, StartTime);
	const float Now = World != nullptr ? static_cast<float>(World->NowSeconds()) : 0.f;
	return Delay < 1 && StartTime <= Now;
}

void FElysiumNpc::CineDelayState(int32& OutDelay, float& OutStartTime) const
{
	// SEAM for `CCineNPC::m_iDelay` (+0x5f70) and `m_startTime` (+0x5f74). The port's beat lives on
	// the `scripted_sequence` entity and the kernel holds no pointer to it; (0, 0) is the resting
	// state retail's constructor leaves.
	OutDelay = 0;
	OutStartTime = 0.f;
}

bool FElysiumNpc::CineCanInterrupt() const
{
	// 0x101a8930 — `m_interruptable` (+0x5f90) set AND the resolved `m_hTargetEnt` (+0x5ce4)
	// answering slot 158 `IsAlive`. The three terms are sequential and short-circuit in retail's own
	// order: the flag, the handle resolving to a non-null entity, and only then the virtual. A
	// missing target answers FALSE, not true — an interruptable beat whose actor has gone cannot be
	// interrupted, it is already over.
	//
	// `+0x5ce4` is bound to `FElysiumNpc::TargetEnt` in the shape map, which is the word this reads.
	// SEAM: `m_interruptable` is `CCineNPC`'s and has no port member; `CineIsInterruptable()` below
	// answers false, so this answers false and says which term refused.
	if (!CineIsInterruptable())
	{
		return false;
	}
	if (World == nullptr || !TargetEnt.IsSet())
	{
		return false;
	}
	FElysiumEntity* Resolved = World->Resolve(TargetEnt);
	if (Resolved == nullptr)
	{
		return false;
	}
	FElysiumNpc* TargetNpc = Resolved->AsNpc();
	return TargetNpc != nullptr ? TargetNpc->IsAlive() : false;
}

bool FElysiumNpc::CineIsInterruptable() const
{
	// SEAM for `CCineNPC::m_interruptable` (+0x5f90, `FIELD_BOOLEAN`). No port member; the port's
	// scripted sequence carries its own interruption policy on the sequence entity
	// (`FElysiumEntity::bScriptOwnerLocked` is the nearest thing and is a DIFFERENT rule — it is
	// spawnflag 512's queue lock, not `m_interruptable`). Answers false.
	return false;
}

void FElysiumNpc::FixScriptNpcSchedule(FElysiumNpc& Npc)
{
	// 0x101a8840 — `CCineNPC::FixScriptNPCSchedule(npc)`, the body every scripted-sequence teardown
	// ends on, and one of the SDK's own named bodies:
	//
	//     if ( pNPC->GetIdealState() != NPC_STATE_DEAD )
	//         pNPC->SetIdealState( NPC_STATE_IDLE );
	//     pNPC->ClearSchedule( ... );
	//
	// Retail's `m_IdealNPCState` (+0x5cc4) is compared against 7 and stored as 1, with the
	// `m_SelectIdealStateTrace` pair stamped first: `+0x1b3c` takes the
	// `e:\vampire\main\dlls\scripted_cp...` path string and `+0x1b40` the `__LINE__` 0x3ca = 970.
	//
	// 29c's walk read that pair as "a one-shot assertion/error-state tripwire". It is NOT:
	// `docs/vtmb/npc-kernel/layout.md` names `+0x1b3c`/`+0x1b40` the ideal-state selector trace, and
	// every writer of `m_IdealNPCState` in the image — `Event_Killed` `0x10265ad0`, `NPCInit`
	// `0x10273390`, `CineCleanup` `0x1027d170` — stamps it the same way. The shape map records both
	// words ABSENT because this runtime traces selections through the mind's transition trace, which
	// is where the reason lands instead.
	//
	// `ClearSchedule` runs on EVERY path, including the dead one.
	if (Npc.Mind.DesiredRetailState() != GChainNpcStateDead)
	{
		Npc.Mind.RequestDesiredState(GChainNpcStateIdle, GChainFixScriptScheduleLine);
	}
	Npc.ClearSchedule();
}

// -------------------------------------------------------------------------------------------------
// `CBasePlayer` — the law and police half.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpc::LevelSupernaturalAct() const
{
	// 0x1017dd80 — `m_LevelSupernaturalAct` (+0x1ccc) behind `DAT_10724ffc`. The shape, in retail's
	// order:
	//   !IsCommand() and m_nValue < 0  -> the stored field
	//   IsCommand()                    -> 0
	//   otherwise                      -> m_nValue
	// Retail dispatches `IsCommand()` TWICE rather than caching it, which the port keeps.
	int32 Override = 0;
	if (!ActLevelOverrideCvar(ActCvarSupernatural, Override) && Override < 0)
	{
		const FElysiumPlayer* Player = ChainPlayer();
		return Player != nullptr ? Player->Law.Supernatural : 0;
	}
	if (ActLevelOverrideCvar(ActCvarSupernatural, Override))
	{
		return 0;
	}
	return Override;
}

int32 FElysiumNpc::LevelInvestigateAct() const
{
	// 0x1017de60 — the same three arms over `m_LevelInvestigateAct` (+0x1cdc) and `DAT_107250d4`.
	int32 Override = 0;
	if (!ActLevelOverrideCvar(ActCvarInvestigate, Override) && Override < 0)
	{
		const FElysiumPlayer* Player = ChainPlayer();
		return Player != nullptr ? Player->Law.Investigate : 0;
	}
	if (ActLevelOverrideCvar(ActCvarInvestigate, Override))
	{
		return 0;
	}
	return Override;
}

uint32 FElysiumNpc::ObfuscateActLevel(uint32 Stored)
{
	// The arithmetic inside `0x1017ddd0`, verbatim:
	//     (((stored & 0x8a66e35) ^ 0x793f90) + 0x8b10412) & 0x175991ca ^ stored ^ 0x783682a9
	// then through `thunk_FUN_1042fd40`.
	//
	// **Unrecovered:** `thunk_FUN_1042fd40` itself. The corpus carries no body for it, so what it
	// does to the folded word is not known and the fold is returned unchanged. That is why the port
	// stores the criminal level as PLAINTEXT (`FElysiumLawState::Criminal`, documented `+0x1cd8`)
	// and reproduces the fold here as an assertable function rather than as storage — an
	// obfuscation whose final transform is unknown cannot be a round trip.
	const uint32 Folded = ((((Stored & GChainObfAnd0) ^ GChainObfXor0) + GChainObfAdd)
		& GChainObfAnd1) ^ Stored ^ GChainObfXor1;
	return Folded;
}

int32 FElysiumNpc::LevelCriminalAct() const
{
	// 0x1017ddd0 — the criminal level, and the odd one of the three: its stored word at `+0x1cd8` is
	// OBFUSCATED, which is why `CBasePlayer`'s datamap types `+0x1cd0 m_LevelCriminalAct` as a
	// record rather than as an int. The cvar arms are identical to the other two.
	int32 Override = 0;
	if (!ActLevelOverrideCvar(ActCvarCriminal, Override) && Override < 0)
	{
		const FElysiumPlayer* Player = ChainPlayer();
		const int32 Stored = Player != nullptr ? Player->Law.Criminal : 0;
		// The port stores plaintext; the fold is what retail applies to its own storage, and it is
		// exercised so the five literals are asserted rather than dropped.
		(void)ObfuscateActLevel(static_cast<uint32>(Stored));
		return Stored;
	}
	if (ActLevelOverrideCvar(ActCvarCriminal, Override))
	{
		return 0;
	}
	return Override;
}

int32 FElysiumNpc::CriminalActCount() const
{
	// 0x1017e720 — a bare getter of `m_iCriminalActCount` (+0x1ce8). The port's player already
	// carries the word and already publishes the read (`FElysiumPlayer::CriminalActCount()`, which
	// the NPC conditions 31-34 lane reads); this row IS that call and stands nothing beside it.
	const FElysiumPlayer* Player = ChainPlayer();
	return Player != nullptr ? Player->CriminalActCount() : 0;
}

int32 FElysiumNpc::SupernaturalActCount() const
{
	// 0x1017e740 — the same over `m_iSupernaturalActCount` (+0x1cec).
	const FElysiumPlayer* Player = ChainPlayer();
	return Player != nullptr ? Player->SupernaturalActCount() : 0;
}

int32 FElysiumNpc::FUN_10178120() const
{
	// 0x10178120 — a bare getter of `+0x1d24`. The datamap names `m_flSetOnHeadTimer` at +0x1d20 and
	// nothing again until `m_iVFlags` (+0x1d60), so the field's NAME and CONCERN are
	// **unrecovered** and the port method keeps 29c's `FUN_` spelling.
	//
	// What IS recovered: three kernel callers and five outside read it through this getter, and
	// NOTHING in `vampire.dll` writes it — a `vtmb_grep` for the offset finds only this body and its
	// thunk. So the shipped answer is whatever the constructor left, and the constructor zeroes it.
	return Field_0x1d24;
}

void FElysiumNpc::SetSpawnResponseCops(int32 Level, FElysiumEntity* Source, const FVector& At)
{
	// 0x1017ed00 — record a delayed police response.
	//
	// Gate: `0x1017f8b0` reads `m_iHuntersInPursuitCount` (+0x1d14) and the body refuses when it is
	// 1 or more. A hunter on the street suppresses a cop response outright.
	//
	// Arm one, the FLT_MAX sentinel at `+0x1cf8` ("nothing pending"):
	//   store the level, the source's handle (or -1 when the source is null), the three location
	//   floats, and then `timer = curtime + RandomFloat(lo, hi)` — the bounds are two ConVars.
	// Arm two, a record already armed:
	//   ONLY when `Level` strictly exceeds the stored level, overwrite the level, the handle and the
	//   location. **The timer is not rescheduled**, which is what makes a burst of incidents one
	//   response landing at the first one's deadline.
	FElysiumPlayer* Player = ChainPlayer();
	const int32 HuntersInPursuit = Player != nullptr ? Player->Police.HuntersInPursuit : 0;
	if (HuntersInPursuit >= 1)
	{
		return;
	}

	const bool bUnused = SpawnResponseCopsTimer == FLT_MAX;
	if (!bUnused && SpawnResponseCopsLevel >= Level)
	{
		return;
	}

	SpawnResponseCopsLevel = Level;
	SpawnResponseCopsNpc = Source != nullptr ? Source->Handle : FElysiumEntityHandle::Invalid();
	SpawnResponseCopsLocation = At;

	if (bUnused)
	{
		float Low = 0.f;
		float High = 0.f;
		SpawnResponseCopsDelayCvars(Low, High);
		// `(*DAT_1070b244 + 4)(lo, hi)` is `RandomFloat`. **Named decision**: the port draws only
		// when the range is non-empty. Retail draws unconditionally, and a zero-width draw still
		// advances its stream; this runtime's streams are SHARED across systems, so a draw that
		// cannot change the answer but does move another system's sequence would be a real
		// behavioural difference.
		const float Delay = (High > Low)
			? ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).FRandRange(Low, High)
			: GChainZero;
		const float Now = World != nullptr ? static_cast<float>(World->NowSeconds()) : 0.f;
		SpawnResponseCopsTimer = Delay + Now;
	}

	// The port's `FElysiumPoliceState` carries the CONSUMER's view of the same record (story 16,
	// recovered from the spawn side). Mirrored here so the two cannot disagree; the producer's four
	// words above are the retail ones and this is the port's own read side, not a second rule.
	if (Player != nullptr)
	{
		Player->Police.bResponsePending = true;
		Player->Police.ResponseSeverity = SpawnResponseCopsLevel;
		Player->Police.ResponseWitness = SpawnResponseCopsNpc;
		Player->Police.ResponsePosition = SpawnResponseCopsLocation;
		Player->Police.ResponseDeadline = SpawnResponseCopsTimer;
	}
}

void FElysiumNpc::RemoveCopInPursuit()
{
	// 0x1017f6e0 — one cop leaves the pursuit.
	//
	// The decrement is UNCONDITIONAL and unclamped: `m_iCopsInPursuitCount` can go negative, and
	// the zero test is `== 0`, not `<= 0`, so a count driven below zero never fires the alert again.
	// On exactly zero: the unrecovered `0x10370630`, then `BeginHeightenedAlert`.
	//
	// The trailing `DevMsg` reads `"CSActs:    %6.1f - OnCopPursuitStart - %d in pursuit"` — retail
	// prints "Start" from the REMOVE path, sharing the format string verbatim with `0x1017f650`,
	// which is the add. A shipped copy-paste, recorded rather than corrected.
	FElysiumPlayer* Player = ChainPlayer();
	if (Player == nullptr)
	{
		return;
	}
	Player->Police.CopsInPursuit -= 1;
	if (Player->Police.CopsInPursuit == 0)
	{
		UnrecoveredChainCalls.Add(TEXT("0x10370630"));
		BeginHeightenedAlert();
	}
	UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("CSActs: OnCopPursuitStart - %d in pursuit"),
		Player->Police.CopsInPursuit);
}

bool FElysiumNpc::IsHeightenedAlertActive() const
{
	// 0x1017f8d0 — `curtime < m_flHeightenedAlertExpireTimer` (+0x1d1c), and nothing else. It does
	// NOT read `m_bInHeightenedAlert` (+0x1d18), which is why `EndHeightenedAlert`'s zeroing of the
	// timer alone is enough to answer false while the flag still stands.
	const FElysiumPlayer* Player = ChainPlayer();
	if (Player == nullptr)
	{
		return false;
	}
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	return Now < Player->Police.HeightenedAlertExpiry;
}

void FElysiumNpc::BeginHeightenedAlert()
{
	// 0x1017f9c0 — arm it. Four steps in retail's order: fire the `+0x498` output
	// (`OnStartCopAlertMode`) off the `0x1023dcd0` singleton; read the duration ConVar
	// `DAT_10725f74` (`debug_heightened_alert_expire_time`); set `m_bInHeightenedAlert` and
	// `m_flHeightenedAlertExpireTimer = <duration> + curtime`; call the unrecovered `0x1017f900`.
	//
	// Note the order of the last two: retail reads curtime BEFORE it raises the flag, and the flag
	// is raised before the timer is written. Nothing observes the gap, and it is kept anyway.
	FireGlobalActsOutput(GChainAlertBeginOutput);

	float Duration = 0.f;
	HeightenedAlertDurationCvar(Duration);
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;

	FElysiumPlayer* Player = ChainPlayer();
	if (Player != nullptr)
	{
		Player->Police.bHeightenedAlert = true;
		Player->Police.HeightenedAlertExpiry = static_cast<double>(Duration) + Now;
	}
	UnrecoveredChainCalls.Add(TEXT("0x1017f900"));
}

void FElysiumNpc::EndHeightenedAlert()
{
	// 0x1017f980 — end it. Fire the `+0x480` output (`OnEndCopAlertMode`) off the same singleton,
	// then zero `m_flHeightenedAlertExpireTimer`.
	//
	// It does **not** clear `m_bInHeightenedAlert` (+0x1d18). The flag and the timer are written by
	// different bodies and only the timer is read by the predicate, so after this the flag is stale
	// and nothing notices — which is exactly what the port reproduces rather than tidying.
	FireGlobalActsOutput(GChainAlertEndOutput);
	FElysiumPlayer* Player = ChainPlayer();
	if (Player != nullptr)
	{
		Player->Police.HeightenedAlertExpiry = 0.0;
	}
}

void FElysiumNpc::RememberScaredNpc(int32 Priority, FElysiumEntity* Npc)
{
	// 0x1017fd60 — the 16-byte scare records at `+0x1d90`.
	//
	// The key is the NPC's ENTITY INDEX, fetched through the engine (`(*DAT_1070b22c + 0x8c)` on the
	// NPC's edict at `npc+0x2e0`), and it is compared against WORD 2 of each record. On a hit:
	//   word 3 (the priority) takes the GREATER of the two — retail writes only when the new one is
	//   larger — and word 0 (the timestamp) is ALWAYS refreshed to curtime, hit or not.
	// On a miss: grow, append `{ <word0 from EBX>, curtime, index, <word3 from the return slot> }`,
	// and log `"Scared NPC: %d (dist:%.2f)"` with the index and the NPC's own `+0x6264`.
	//
	// The append's words 0 and 3 come out of registers the decompilation cannot attribute
	// (`unaff_EBX`, `unaff_retaddr`), so the LAYOUT of the appended record is **unrecovered** beyond
	// the timestamp and the index. The port's `FElysiumScareRecord` is story 16's recovery of the
	// same 16 bytes from the CONSUMER side — `{ Npc, Severity, Time }` with one word unidentified —
	// and this body writes THAT, which is the reading that already exists rather than a second one.
	if (Npc == nullptr)
	{
		return;
	}
	FElysiumPlayer* Player = ChainPlayer();
	if (Player == nullptr)
	{
		return;
	}
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;

	for (FElysiumScareRecord& Record : Player->ScareQueue)
	{
		if (Record.Npc.Index == Npc->Handle.Index)
		{
			if (Record.Severity < Priority)
			{
				Record.Severity = Priority;
			}
			Record.Time = Now;
			return;
		}
	}

	FElysiumScareRecord Fresh;
	Fresh.Npc = Npc->Handle;
	Fresh.Severity = Priority;
	Fresh.Time = Now;
	Player->ScareQueue.Add(Fresh);

	// `Msg(s_Scared_NPC___d__dist___2f__105888bc, index, npc[0x1899])`. `npc + 0x6264` is a float on
	// the NPC the message calls a distance; no port member claims the offset, so the message names
	// the NPC instead and says so.
	UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("Scared NPC: %d (dist: unrecovered +0x6264)"),
		Npc->Handle.Index);
}
