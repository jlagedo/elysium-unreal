// Story 29c-1, family **Facing** — the declarations of this family's layer 0–9 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual, and this family only defines it. What lands here is
// the non-slot half — the retail helpers, free functions and non-virtual methods of layers 0–9
// whose overlay row names a port method on `FElysiumNpc` that did not exist before this story.
//
// The definitions are in `Substrate/ElysiumNpcKernelFacing.cpp` and the tests in
// `Tests/ElysiumNpcKernelFacingTests.cpp`. One file per family rather than 915 declarations
// appended to an already-oversized header: the family boundary is what this story ports by.
//
// The family is the facing-target queue and the turn-activity ladder. Two of its rows fill slots
// the GENERATOR already defines (`HeadDirection2D`/`HeadDirection3D` at 370/371, `OnChangeActivity`
// at 465, `AddLookTarget` at 535/536): 29c's verdict for those is the TROIKA-LINE body, and the
// rows this family carries are other branches' fills of the same slot — `CAI_BaseHumanoid`'s real
// implementations and the species overrides. Those land as named methods here, because a second
// definition of a generated slot is a duplicate symbol, and each says in its comment which slot it
// is the branch answer for.

// --- Words this family needed that 29b did not declare ------------------------------------------
//
// All four sit outside the hand-written shape map's band (`ElysiumNpcKernelShapeMap.cpp` binds
// `0x1a40`..`0x665a`, the words `CAI_BaseNPC` itself carries): two are `CBaseAnimating`-tier and
// two are a species leaf's own.

// +0x0848 m_viewtarget — the world point `SetViewtarget` (slot 277) copies in. Retail networks it
// to the client, which is the hop this runtime makes with `FElysiumCombatCharacter::CurEyeTarget`;
// this is retail's own word, written by the kernel and read by nothing in this substrate yet.
FVector Viewtarget = FVector::ZeroVector;

// +0x0ff0 m_IdealActivity — the logical activity `SetIdealActivity` (`0x10272650`) stores before it
// re-translates the ideal triple beside it. This runtime names activities rather than numbering
// them (`FElysiumIdealActivityState::Activity`), so the kernel's own copy is carried as retail's
// registered number, which is what its bodies compare and write.
int32 IdealActivityNumber = 0;

// +0x1484 m_nMotionTrail — the motion-trail id `CNPC_VSabbatGunman::OnChangeActivity` picks per
// activity change. No visual layer reads it yet.
int32 MotionTrail = 0;

// +0x66d0 m_fFacingTime (`CNPC_VChangBros`) — the curtime stamp `UpdateFacingTimer` resets, and the
// clock `GetFacingTimeToTeleport`'s answer is measured against. Carried as double like every other
// stamp in this runtime.
double FacingTime = 0.0;

// --- The `CAI_Motor` facing seam ----------------------------------------------------------------
//
// `m_pMotor` (+0x5d44) is `ELYSIUM_NPC_WORD_CHAIN(0x5d44, "FElysiumScriptedCharacter::Motor")`, and
// that chain is `IElysiumNpcMotor` — a mover that takes a commanded yaw through `Face()` and keeps
// neither an ideal-yaw word nor a queue of facing targets. Retail's `CAI_Motor` keeps both:
// `m_IdealYaw` at motor+0x34 and the queued-facing-target list at motor+0x54. Every accessor below
// stands for one of its virtuals and answers nothing until that mover carries them.

// `CAI_Motor::DeltaIdealYaw` (`0x102e1f90`): `AngleDiff(m_IdealYaw, AngleMod(GetLocalAngles().y))`,
// and exactly 0 when the two are equal. **SEAM** — nothing in this substrate writes it, so it
// stands at retail's "already facing the ideal" answer, 0. It is a field rather than a query so the
// ladders below can be driven against a live number the day the mover carries one.
float MotorIdealYawDelta = 0.f;

// The seam's read side, so a body cites `0x102e1f90` at the point of use.
float MotorDeltaIdealYaw() const;

// `CAI_Motor` slots 12/13/14 — the three queued-facing-target overloads (`0x102e2150`,
// `0x102e2120`, `0x102e20f0`), each of which forwards its arguments into the motor's own
// `m_facingQueue` (motor+0x54). **SEAM**: this substrate models no facing queue, so each records
// the request and adds nothing. `FacingTargetRequests` is what the test reads; nothing else does.
struct FFacingTargetRequest
{
	int32 MotorSlot = 0;               // 12, 13 or 14 — which retail overload was reached
	FElysiumEntityHandle Target;       // the entity form, unset for the vector-only overload
	FVector Position = FVector::ZeroVector;
	float Duration = 0.f;
	float Ramp = 0.f;
	float Tolerance = 0.f;
};
TArray<FFacingTargetRequest> FacingTargetRequests;
void MotorAddFacingTarget(const FFacingTargetRequest& Request);

// The retail cvar the whole facing-target family is gated on (`0x10924f74`, read as
// `!IsCommand() && m_nValue != 0` — `ConVar::GetBool()` inlined): `debug_allow_move_facing`,
// shipped "1".
bool FacingTargetsEnabled() const;

// The retail cvar `CAI_BaseNPCTroika::SetTurnActivity` ORs with `m_bAllowTurningAnims`
// (`0x109247ec`, the same `ConVar::GetBool()` shape; also read by both `MaxYawSpeed` overrides).
// `debug_turning`, shipped "0" — which leaves `m_bAllowTurningAnims` (+0x65f9), an authored
// per-NPC key, as the live gate.
bool TurningAnimsEnabled() const;

// --- The animation seam the turn ladder ends in -------------------------------------------------

// `CBaseAnimating::SelectWeightedSequence(Activity, -1)` on the base line and
// `CAI_BaseNPCTroika`'s stat-filtered twin (`0x10295460`) on the Troika line: which sequence this
// body would play for an activity, or -1 when it authors none. The ladder branches on `!= -1`.
// **SEAM** — this substrate resolves activities by NAME through the action tables and its animating
// tier stands no sequence index, so this answers -1 and the ladder falls through to its `ACT_IDLE`
// tail, which is retail's own answer for a body with no turn clips.
int32 SelectWeightedSequenceForActivity(int32 Activity) const;

// `CAI_BaseNPC::SetIdealActivity` (`0x10272650`): store `m_IdealActivity` (+0x0ff0), then
// re-translate it into `m_nIdealSequence`/`m_IdealTranslatedActivity`/`m_IdealWeaponActivity`
// through `0x10272130`. The translation chain is story 29d's; this stores the word and stops,
// which is the half the turn ladder is measured by.
void SetIdealActivityNumber(int32 Activity);

// `CBaseAnimating::SetPoseParameter(name, value)` (retail vtable +0x564) — `SetAim`'s two writes.
// **SEAM**: this runtime's animating tier exposes no pose-parameter surface, so the writes are
// recorded and go no further. Read by the test and by nothing else.
struct FPoseParameterWrite
{
	FString Name;
	float Value = 0.f;
};
TArray<FPoseParameterWrite> PoseParameterWrites;
void SetPoseParameterByName(const TCHAR* Name, float Value);

// --- The turn-activity ladder (`SetTurnActivity`) -----------------------------------------------
//
// Both rungs of slot 572, as pure functions of the motor's yaw delta so every threshold is
// assertable without a mover. `HasSequence` is `SelectWeightedSequence(act) != -1`.

struct FTurnActivityPick
{
	int32 Activity = 1;            // ACT_IDLE is the tail of both ladders
	bool bTagsTurnMemory = false;  // `m_afMemory |= 0x2000` on the picks that take it
};

// `CAI_BaseNPC::SetTurnActivity` `0x10289d10` — the base line's ladder.
static FTurnActivityPick TurnActivityBaseLadder(float YawDelta,
	TFunctionRef<bool(int32)> HasSequence);
// `CAI_BaseNPCTroika::SetTurnActivity` `0x10297640` — the Troika line's, which slot 572 dispatches
// to for every spawnable species.
static FTurnActivityPick TurnActivityTroikaLadder(float YawDelta,
	TFunctionRef<bool(int32)> HasSequence);

// --- `CAI_BaseHumanoid`'s look-target list (slots 535/536) --------------------------------------
//
// `CAI_BaseHumanoid::vfunc536` `0x1025f760` (entity) and `vfunc535` `0x1025f8e0` (position). One
// 0x24-byte record per target in a `CUtlVector` at +0x5f88 with its count at +0x5f94. The Troika
// line's own fills of 535/536 are `return;` — that is 29c's verdict and the generator's body — so
// nothing a spawned `npc_*` dispatches through reaches this; it is `CAI_BaseHumanoid`'s branch
// answer, recovered and standing.
struct FLookTargetRecord
{
	int32 Kind = 0;                  // +0x00: 0 = entity, 1 = position
	FElysiumEntityHandle Target;     // +0x04
	FVector Position = FVector::ZeroVector;  // +0x08..+0x10
	double StartTime = 0.0;          // +0x14: curtime when the record was added
	double EndTime = 0.0;            // +0x18: curtime + duration
	float Rate = 0.f;                // +0x1c: influence / duration
	int32 Priority = 0;              // +0x20
};
TArray<FLookTargetRecord> LookTargets;

void AddLookTargetHumanoid(FElysiumEntity* Target, int32 Priority, float Duration, float Influence);
void AddLookTargetHumanoid(const FVector& Position, int32 Priority, float Duration,
	float Influence);

// --- `CAI_BaseHumanoid`'s cached head/eye basis (slots 370..373) ---------------------------------
//
// `0x1025e7b0` refreshes an attachment-derived head origin and the normalized direction from it to
// the eye point, latched behind two bits of +0x5f4c; `0x1025f160`/`0x1025f0b0` return the two
// cached vectors and `0x1025f0f0`/`0x1025f040` flatten them. **SEAM**: reaching the "head"
// attachment needs the animating tier's bone cache, which this substrate does not expose to the
// kernel, so the refresh answers "no attachment" and both cached vectors stay at the fallback
// retail itself takes in that case — `GetAbsAngles()`' forward.
mutable FVector HumanoidEyeDirection = FVector::ZeroVector;   // +0x5f5c..+0x5f64
mutable FVector HumanoidHeadDirection = FVector::ZeroVector;  // +0x5f68..+0x5f70
mutable uint32 HumanoidHeadCacheBits = 0;                     // +0x5f4c, bits 0x1 and 0x2

void RefreshHumanoidHeadCache() const;         // `0x1025e7b0`
FVector HeadDirection3DHumanoid() const;       // `CAI_BaseHumanoid#371` `0x1025f160`
FVector HeadDirection2DHumanoid() const;       // `CAI_BaseHumanoid#370` `0x1025f0f0`
FVector EyeDirection3DHumanoid() const;        // `CAI_BaseHumanoid#373` `0x1025f0b0`
FVector EyeDirection2DHumanoid() const;        // `CAI_BaseHumanoid#372` `0x1025f040`

// `CAI_BaseHumanoid::SetHeadDirection` `0x1025eaf0`, slot 537's branch override — the pose-parameter
// version, richer than the base at `0x1026af70` that slot 537 carries for the Troika line.
void SetHeadDirectionHumanoid(const FVector& LookTarget, float Interval);

// Whether the class this NPC actually is answers slots 370/371 with its BODY direction rather than
// with a head of its own — `CCineNPC`/`CCineAI`/`CCineAISchedule`, `CPayphone`, the three
// `CNPCMaker`s and `CNPC_VRat` all forward 370→368 and 371→369. The table carries the retail
// address of each class's own 370 and 371 bodies so a reader can check it against `slots.md`.
// Returns false for a class that keeps its own head aim (or for one the census does not hold).
bool HeadDirectionIsBodyDirection() const;

// What slots 370/371 answer for the class this NPC is, once the branch above is folded in.
// Slot 370/371 THEMSELVES are the Troika-line bodies (`0x10331cb0`/`0x10331b30`) and remain the
// generator's stubs; this is the recovered species half standing beside them.
bool RetailHeadDirection(bool b2D, FVector& OutDirection) const;

// --- Slot 465's species half --------------------------------------------------------------------
//
// `CAI_BaseNPCTroika::OnChangeActivity` `0x10295a60` is `return;` — 29c's verdict, and the body the
// generator emits for slot 465. Four species replace it, and this is their table. Every arm ends by
// chaining to the base, which is why the base's emptiness is the whole of the shared algorithm.
void OnChangeActivitySpecies(int32 Activity);

// `CNPC_VSabbatGunman::OnChangeActivity` `0x103a56f0`'s pick, as a pure function of its three
// species convars (`DAT_1093c104` speed threshold, `DAT_1093c14c` trail id, `DAT_1093c1f4`
// playback scalar) and the body's `m_flGroundSpeed` (+0x0654), so both arms are assertable
// without any of the four. `-1` is retail's own "no scalar" value on the stopped arm.
struct FMotionTrailPick
{
	int32 MotionTrail = 0;
	float PlaybackScalar = 0.f;
};
static FMotionTrailPick SabbatGunmanMotionTrail(float GroundSpeed, float SpeedThreshold,
	int32 TrailId, float TrailScalar);

// `CNPC_VMingXiao::OnChangeActivity` `0x103947b0`'s playback scalar, as a pure function of the
// activity, the discipline gate (`0x10398870`, which the oracle names "+0x6674"), the tentacle
// count (+0x670c) and the tuning record (`0x101e8da0(0x10739d08)`) read by field offset.
struct FMingXiaoPlayback
{
	float Scalar = 0.f;
	bool bWalkOrRun = false;      // the activity was ACT_WALK (9) or ACT_RUN (0x13)
	bool bSecondWriteSkipped = false;  // the gated ACT 0x4b arm writes once and leaves early
};
static FMingXiaoPlayback MingXiaoPlaybackScalar(int32 Activity, bool bDisciplineArm,
	int32 TentacleCount, TFunctionRef<float(int32)> TuningField);

// --- The facing readers and writers that fill no slot -------------------------------------------

// `CAI_Motor` slot 7 `0x102e11f0` — cancel the current facing-queue entry.
void ClearFacingTarget();
// `CAI_BaseNPC::FacingIdeal` `0x10278c80`.
bool FacingIdeal() const;
// `CNPC_VChangBros::GetFacingTimeToTeleport` `0x1036dc60`.
float GetFacingTimeToTeleport() const;
// `CNPC_VAndreiBlood::FacePlayerAdvance` `0x1035e5f0`.
void FacePlayerAdvance();
// `CNPC_VSabbatLeader::PlayerIsFacingMe` `0x103aaf50`.
bool PlayerIsFacingMe() const;
// `CNPC_VChangBros::UpdateFacingTimer` `0x1036d600`.
void UpdateFacingTimer();
