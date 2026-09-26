// Story 29c-1, family **Facing** — the declarations of this family's layer 0–9 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual, and this family only defines it. What lands here is
// the non-slot half — the retail helpers, free functions and non-virtual methods of layers 0–9
// whose overlay row names a port method on `FElysiumNpc` that did not exist before this story.
//
// The definitions are in `Substrate/ElysiumNpcFacing.cpp` and the tests in
// `Tests/ElysiumNpcKernelFacingTests.cpp`. One file per family rather than 915 declarations
// appended to an already-oversized header: the family boundary is what this story ports by.
//
// The family is the facing-target queue and the turn-activity ladder. Some of its rows fill slots
// the GENERATOR already defines (`HeadDirection2D`/`HeadDirection3D` at 370/371, `OnChangeActivity`
// at 465): 29c's verdict for those is the TROIKA-LINE body, and the rows this family carries are
// other branches' fills of the same slot — the species overrides. (`CAI_BaseHumanoid`'s own fills,
// including its `AddLookTarget` at 535/536, were deleted by 0019 story 5 step 1: the class has no
// instance.)
// Those land as named methods here, because a second definition of a generated slot is a duplicate
// symbol, and each says in its comment which slot it is the branch answer for.

// --- Words this family needed that 29b did not declare ------------------------------------------
//
// All four sit outside the hand-written shape map's band (`ElysiumNpcKernelShapeMap.cpp` binds
// `0x1a40`..`0x665a`, the words `CAI_BaseNPC` itself carries): two are `CBaseAnimating`-tier and
// two are a species leaf's own.

// +0x0848 m_viewtarget — the world point `SetViewtarget` (slot 277) copies in. Retail networks it
// to the client, which is the hop this runtime makes with `FElysiumCombatCharacter::CurEyeTarget`;
// this is retail's own word, written by the kernel and read by nothing in this substrate yet.
FVector Viewtarget = FVector::ZeroVector;

// +0x1484 m_nMotionTrail — the motion-trail id `CNPC_VSabbatGunman::OnChangeActivity` picks per
// activity change. No visual layer reads it yet.
int32 MotionTrail = 0;

// --- The `CAI_Motor` facing seam ----------------------------------------------------------------
//
// `m_pMotor` (+0x5d44) is `ELYSIUM_NPC_WORD_CHAIN(0x5d44, "FElysiumScriptedCharacter::Motor")`, and
// that chain is `IElysiumNpcMotor` — a mover that takes a commanded yaw through `Face()` and keeps
// neither an ideal-yaw word nor a queue of facing targets. Retail's `CAI_Motor` keeps both:
// `m_IdealYaw` at motor+0x34 and the queued-facing-target list at motor+0x54. Every accessor below
// stands for one of its virtuals and answers nothing until that mover carries them.

// The seam's read side, so a body cites `0x102e1f90` at the point of use.
float MotorDeltaIdealYaw() const;

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

// `CAI_BaseNPC::SetTurnActivity` `0x10289d10` — the base line's ladder.
static FTurnActivityPick TurnActivityBaseLadder(float YawDelta,
	TFunctionRef<bool(int32)> HasSequence);
// `CAI_BaseNPCTroika::SetTurnActivity` `0x10297640` — the Troika line's, which slot 572 dispatches
// to for every spawnable species.
static FTurnActivityPick TurnActivityTroikaLadder(float YawDelta,
	TFunctionRef<bool(int32)> HasSequence);

// Slots 370/371 that forward to 368/369 — `CNPC_VRat` (`0x103ad7f0`/`0x103ad820`) and `CPayphone`
// (`0x101aa7f0`/`0x101aa820`) override `HeadDirection2D`/`HeadDirection3D` on their C++ classes
// (story 5 step 3). `CCineNPC`/`CCineAI`/`CCineAISchedule` and the three `CNPCMaker`s forward the
// same way; they fold in steps 9 and 8.

// --- Slot 465's species half --------------------------------------------------------------------

// `CNPC_VSabbatGunman::OnChangeActivity` `0x103a56f0`'s pick, as a pure function of its three
// species convars (`DAT_1093c104` speed threshold, `DAT_1093c14c` trail id, `DAT_1093c1f4`
// playback scalar) and the body's `m_flGroundSpeed` (+0x0654), so both arms are assertable
// without any of the four. `-1` is retail's own "no scalar" value on the stopped arm.
struct FMotionTrailPick
{
	int32 MotionTrail = 0;
	float PlaybackScalar = 0.f;
};

// `CNPC_VMingXiao::OnChangeActivity` `0x103947b0`'s playback scalar, as a pure function of the
// activity, the discipline gate (`0x10398870`, which the oracle names "+0x6674"), the tentacle
// count (+0x670c) and the tuning record (`0x101e8da0(0x10739d08)`) read by field offset.
struct FMingXiaoPlayback
{
	float Scalar = 0.f;
	bool bWalkOrRun = false;      // the activity was ACT_WALK (9) or ACT_RUN (0x13)
	bool bSecondWriteSkipped = false;  // the gated ACT 0x4b arm writes once and leaves early
};

// --- The facing readers and writers that fill no slot -------------------------------------------

// `CAI_Motor` slot 7 `0x102e11f0` — cancel the current facing-queue entry.
void ClearFacingTarget();
// `CAI_BaseNPC::FacingIdeal` `0x10278c80`.
bool FacingIdeal() const;
