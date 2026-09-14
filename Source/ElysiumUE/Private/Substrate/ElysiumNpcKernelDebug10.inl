// Story 29d, family **Debug10** — the declarations of this family's layer 10–18 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl` and story 29c-1's family `.inl`s. A body that fills a Troika-line
// vtable slot is NOT declared here: the generator already declares that virtual and this family only
// defines it. What lands here is the non-slot half — the base-tier body beside the Troika override,
// the per-species arms the one slot method dispatches to, the debug ring, and the seams the arms
// read through.
//
// The definitions are in `Substrate/ElysiumNpcKernelDebug10.cpp` (the ring, the trace messages, the
// seams and every slot-124 TEXT body) and `Substrate/ElysiumNpcKernelDebug10_2.cpp` (every slot-123
// GEOMETRY body and `NPCThinkDebugPre`). The tests are `Tests/ElysiumNpcKernelDebug10Tests.cpp`.
//
// --- What this family is ---------------------------------------------------------------------------
//
// Fifteen rows: the four big overlay dumps (`CAI_BaseNPC::DrawDebugTextOverlays` 0x102767d0,
// `CAI_BaseNPCTroika::DrawDebugTextOverlays` 0x1029d4e0, `CAI_BaseNPCTroika::DrawDebugGeometryOverlays`
// 0x1029ca50 and `CAI_BaseNPCTroika::NPCThinkDebugPre` 0x10292500), the three Troika trace-message
// slots (17/18/20), the per-entity debug ring `0x1027ef20`, and seven species arms of slots 123/124.
//
// A debug overlay is a RULE, not a picture: the line ORDER, the format strings, the line INDICES the
// body returns (which is the budget every later override consumes), the gates each arm stands behind
// and the constants it pushes are all observable. Every emission goes through story 29c-1's ONE
// output seam, `FElysiumNpc::FDebugLine` — `EmitEntityText`, `EmitDevMsg`, `EmitOverlayBox`,
// `EmitOverlayBoxDirection`, `EmitOverlayLine`, `EmitOverlayText` — so a test reads a capture rather
// than a screen. No second seam is invented here.
//
// **What `FDebugLine` does not carry, and why that is allowed.** Retail's `NDebugOverlay` entry
// points take a trailing `flDuration` (0.5 s on the witness boxes, 0.2 s on the eye/ideal pair,
// 0.1 s on the enemy body-target boxes, 0.0 elsewhere). A duration decides how long a picture stays
// on a screen this runtime does not have; it changes no order, no value and no state. The recovered
// numbers are named as constants beside each call so the reader has them, and are not carried on the
// line. NAMED, VISUAL-ONLY.
//
// **What `DAT_1070b22c`+0x8c actually is.** Story 29c-1 recorded it as
// `IVEngineServer::AddEntityTextOverlay(edict, line, …)`. The listing says otherwise on every call
// site in this band: it is `IVEngineServer::IndexOfEdict(edict)`, whose answer is then the FIRST
// argument of `NDebugOverlay::EntityText(entIndex, line, text, duration, r, g, b, a)` (`0x101434b0`)
// — which is why every one of these blocks pushes eight dwords and cleans `0x20`. The seam is the
// same question and `EmitEntityText` is reused unchanged; the correction is recorded here and in
// `docs/vtmb/npc-ai/shape.md` rather than by editing 29c-1's file.

// --- Slot 124 `DrawDebugTextOverlays` — one slot, eight retail bodies ------------------------------
//
// `FElysiumNpc::DrawDebugTextOverlays()` (the generated virtual, defined by this family) is the one
// port method. It resolves the census body for this NPC's retail class
// (`ElysiumNpcKernelClass::BodyOf(RetailClass(), 124)`) and runs that arm; the arms themselves are
// named below because three of them are several hundred lines. `CScriptedTarget#124` (`0x1034ddf0`)
// is story 29c-1's `ScriptedTargetDrawDebugTextOverlays` and is dispatched to, not re-ported.

/** `CAI_BaseNPC::DrawDebugTextOverlays` (`0x102767d0`) — slot 124's BASE body, a distinct retail
 *  function beside the Troika override that owns the slot. Every slot-124 body in the census reaches
 *  it: the Troika one calls it first for its starting line index, and `CNPC_Crow`'s calls it INSTEAD
 *  of the Troika one. Returns the next free entity-text line. */
int32 BaseDrawDebugTextOverlays();

/** `CAI_BaseNPCTroika::DrawDebugTextOverlays` (`0x1029d4e0`) — the Troika-line body proper, 3,754
 *  bytes. Chains `0x102767d0` for the first free line and returns it unchanged unless
 *  `m_debugOverlays` bit 0 is set. Returns the next free line. */
int32 TroikaDrawDebugTextOverlays();

/** `0x1029d4e0`'s `0x10000000` block — the two condition-mask walks and the fixed 26-id watch list,
 *  eight three-character abbreviations per line. Split out because it is a third of the body, and
 *  because its three recovered asymmetries (an `INT COND` header, an inverted upper-case probe on
 *  the interrupt arm, and a final flush gated on the WRONG loop's leftover) are what the row is
 *  observable for. Takes the first free line and returns the next one. */
int32 EmitConditionDump(int32 FirstLine);

/** `CNPC_Crow::DrawDebugTextOverlays` (`0x10358f90`) — chains the BASE (`0x102767d0`), not the
 *  Troika body, and adds `morale:` and `enemy (dist):` under bit 0. */
int32 CrowDrawDebugTextOverlays();

/** `CNPC_VHengeyokai::DrawDebugTextOverlays` (`0x10383560`) — the Troika body, then slot 9's string
 *  on one further line under bit 0. */
int32 HengeyokaiDrawDebugTextOverlays();

/** `CNPC_VNewscaster::DrawDebugTextOverlays` (`0x103a1250`) — a scope-trace push, the Troika body,
 *  then `0x103a0ff0`'s story-queue lines added to the SAME line budget under bit 0. */
int32 NewscasterDrawDebugTextOverlays();

/** `CNPC_VTzimisce::DrawDebugTextOverlays` (`0x103c08d0`) — the Troika body, then one `Body - …`
 *  line under bit 0 built from a cross-NPC latch pair of globals. */
int32 TzimisceDrawDebugTextOverlays();

/** `CNPC_VZombie::DrawDebugTextOverlays` (`0x103e0e80`) — the Troika body, then one `Cond: %s` line
 *  per set bit of the 0..0xbf condition bitfield at `+0x5c5c`, under `m_debugOverlays & 0x40000`. */
int32 ZombieDrawDebugTextOverlays();

// --- Slot 123 `DrawDebugGeometryOverlays` — one slot, five retail bodies ---------------------------

/** `CAI_BaseNPCTroika::DrawDebugGeometryOverlays` (`0x1029ca50`) — the Troika-line body, 2,129
 *  bytes: four independent `m_debugOverlays` masks, two ConVar-gated blocks, the hint facing pair and
 *  the relationship-line walk, then `CAI_BaseNPC::DrawDebugGeometryOverlays` (`0x10275760`, story
 *  29c-1's `BaseDrawDebugGeometryOverlays`). */
void TroikaDrawDebugGeometryOverlays();

/** `CNPC_VCop::DrawDebugGeometryOverlays` (`0x10372f00`) — the relationship label above the cop's
 *  head, then the Troika body unconditionally. */
void VCopDrawDebugGeometryOverlays();

/** `CNPC_VMingXiao::DrawDebugGeometryOverlays` (`0x10399d40`) — four range rings under
 *  `m_debugOverlays & 0x20000000` (NOT bit 0 like its siblings), then the Troika body. */
void MingXiaoDrawDebugGeometryOverlays();

/** `CNPCMaker::DrawDebugGeometryOverlays` (`0x1034bd30`), the fourth census body at slot 123.
 *  **SEAM**: its verdict is `registry:123` in band 0–4 — it is the maker's story, not this one — and
 *  it is reached here only so a spawned `npc_maker` does not silently take the Troika body. */
void MakerDrawDebugGeometryOverlays();

// --- `CAI_BaseNPCTroika::NPCThinkDebugPre` (`0x10292500`) -------------------------------------------

/** The pre-think debug pass, 1,782 bytes, read from the LISTING (the decompiler does not settle its
 *  type propagation). Two witness boxes, three ConVar-gated blocks and the ring-dump tail. The ONLY
 *  state it touches is the `+0x5b55` dump-request byte, which story 29b recorded absent. */
void TroikaNPCThinkDebugPre();

// --- The debug ring, `+0x1b4e` / `+0x5b50` / `+0x5b54` ----------------------------------------------
//
// Story 29b recorded the 16 KB per-entity AI debug ring and its three cursors **absent with a
// reason** (`ElysiumNpcKernelShapeMap.cpp`: `ELYSIUM_NPC_WORD_ABSENT(0x1b4e)`, `0x5b50`, `0x5b54`,
// `0x5b55` — "retail's 16 KB in-memory AI debug ring; this runtime logs through its own channels").
// So the ring itself stays absent and the LINES it would have held go to `FDebugLine` instead, which
// is that channel. What is carried is the ring's one observable RULE — the cursor advance and the
// wrap — as a pure function, so the recovery is checkable without standing 16 KB per NPC.

/** `0x1027ef20` — append one line to the NPC's own debug ring. A null line does nothing at all.
 *  Retail `sprintf`s it at `this + 0x1b4e + cursor`, advances the cursor at `+0x5b50` by the byte
 *  count, and on a cursor past `0x3dff` zero-fills the rest of the 0x4000-byte buffer, sets the wrap
 *  latch at `+0x5b54` and resets the cursor to 0. The ring is absent; the line lands on the
 *  `DevMsg` channel and `DebugLogRingAdvance` below is the arm. */
void AppendDebugLogLine(const TCHAR* Text);

/** `0x1027ee20` — the same append against the GLOBAL trace ring slots 17 and 19 use instead of the
 *  per-entity one. Seventy-nine bytes, no verdict row of its own, and story 29c-1's slot-19 body
 *  already records it absent for the same reason. */
void AppendGlobalDebugLogLine(const TCHAR* Text) const;

/** `0x1027ef20`'s cursor arm, as a pure function so the recovered rule is exercised without the
 *  ring: given the cursor before the write and the number of bytes `sprintf` returned, answer the
 *  cursor after, and set `bOutWrapped` when the wrap latch (`+0x5b54`) was raised. The test is
 *  `> 0x3dff`, not `>=`, and the reset is to 0. */
static int32 DebugLogRingAdvance(int32 Cursor, int32 Written, bool& bOutWrapped);

/** `0x1027efb0` — the ring DUMP `NPCThinkDebugPre`'s tail runs, which walks the 16 KB buffer from
 *  the `+0x5b50` cursor in 512-byte chunks. **ABSENT**: the ring is absent (above), and the row's own
 *  verdict in band 0–4 is `mechanism → UE_LOG`. Recorded so the arm is visible. */
void DumpDebugLogRing() const;

/** `+0x5b55`, the ring's DUMP REQUEST byte — the ONE word `NPCThinkDebugPre` writes. **ABSENT**
 *  (story 29b: "the dump request for that debug ring"), so the question answers false and the clear
 *  is recorded rather than made. */
bool DebugRingDumpRequested() const;
void ClearDebugRingDumpRequest();

// --- Slots 17 / 18 / 20 — the three Troika trace-message bodies ------------------------------------
//
// Four slots, two pairs, and the asymmetry between them is retail's:
//   18 `0x1028de10` (non-const) — format, then the PER-ENTITY ring (`0x1027ef20`) or `DevMsg`.
//   17 `0x1028de90` (const)     — format, then the GLOBAL ring (`0x1027ee20`) with the **RAW**
//                                 message, so the ring never sees the formatted text; only the
//                                 `DevMsg` arm does.
//   20 `0x1028df30` (non-const) — no format at all: the raw message, per-entity ring or `DevMsg`.
//   19 `0x1028dfb0` (const)     — byte-identical to 20 but the GLOBAL ring; already written by story
//                                 29c-1 at `ElysiumNpcKernelBaseHelpers.cpp`.

/** `0x1028d990` — the 906-byte formatter slots 17 and 18 push their message through: curtime at
 *  `%6.2f`, the indent level clamped at 0, a `CONDS:` list, the 32-glyph `PIS_PF_T_L…` mask over
 *  `m_afMemory` (`+0x5d8c`), the 30-glyph `RSCPFCNFIPCD…` mask over `m_bfAINPCFlags` (`+0x14b8`) and
 *  a `NAV %s %s` pair. **SEAM**: the body is family **Conditions10**'s row
 *  (`0x1028d990` → `FElysiumNpc::BuildConditionDebugString`) in this same story band, so it is not
 *  written twice. Until it lands this answers the message unchanged, which is retail's own answer
 *  for the shortest of its three format arms with every optional block empty. */
FString TraceMessageFormat(const TCHAR* Message, int32 IndentLevel) const;

/** `DAT_10920534` — retail's verbose-trace toggle: set routes a trace message to a ring, clear to
 *  `DevMsg`. **SEAM**: no console byte here. Answers the port's ring-vs-DevMsg choice through
 *  `DebugConVar`, so a test can drive both arms; the shipped default is clear. */
bool TraceMessagesGoToRing() const;

// --- The five debug ConVars these bodies gate on ---------------------------------------------------
//
// `NPCThinkDebugPre` reads three (`DAT_1092479c`, `DAT_109244c4`, `DAT_1092435c`), the Troika
// geometry body one (`DAT_10924f24`) and the Troika text body one (`DAT_1092429c`); the trace
// messages read the byte `DAT_10920534`. Every one is tested the same way — `cv->vtable[4]()` must
// answer 0 (the object is a variable, not a command) and the int at `cv + 0x2c` must be non-zero —
// which is the same idiom story 29c recorded for `DAT_1092053c` and friends.
//
// **SEAM**: this runtime has no console-variable registry. Each answers its shipped default, 0
// (every one of these arms is off until a developer types the command), and a test sets the value it
// wants. The value is the ConVar's own `+0x2c` int, which is exactly the word retail's arms read —
// `DAT_1092435c` is not a bool but selects `0x1028e030` on 1 and `0x1028e060` on 2.

/** The current value of one of retail's debug ConVars, by its global's address. 0 when unset. */
static int32 DebugConVar(const TCHAR* RetailGlobal);

/** Set one, for a test. Clears every value when `RetailGlobal` is null. */
static void SetDebugConVar(const TCHAR* RetailGlobal, int32 Value);

// --- The seams the bodies read through ------------------------------------------------------------

/** `CBaseAnimating::GetPoseParameter01(name)` (`0x1000108c`), the three pose reads of the Troika
 *  text body. **SEAM**: family Anim records that this runtime stands no studio header and therefore
 *  no pose-parameter table; answers 0.0, and the three lines still print because retail's gate is
 *  `GetModelPtr() != NULL`, not the pose value. */
float PoseParameter01(const TCHAR* PoseName) const;

/** `m_flGroundSpeed` (`+0x0654`), the `ground speed: %.3f` line. **SEAM**: family Positions records
 *  the same word as an input it has no store for; answers 0.0. */
float RetailGroundSpeed() const;

/** `m_LastTakeDamageInfo + 0x40` (`+0x664c`), the `HB` half of the `HG - %d : HB - %d` line —
 *  `0x101c2a30` is `return *(int*)(this + 0x40)` over the 0x4c-byte `CTakeDamageInfo` the Troika
 *  constructor builds at `+0x660c`. **SEAM**: the port's `FElysiumNpcMemory::LastDamageAttacker`
 *  carries the attacker, not the whole packet; answers 0. The `HG` half is real —
 *  `LastHitGroup` (`+0x1594`, family Damage). */
int32 RetailLastDamageInfoWord40() const;

/** `m_flFieldOfView` (`+0x1574`), the cone half-angle cosine the three `0x1029c4a0` calls pass.
 *  Answered from `ElysiumNpcSense::DefaultViewConeDot`, which story 29c-1's view-cone arm already
 *  uses for the same word. */
float RetailFieldOfViewDot() const;

/** `m_eHull`'s alternate at `+0x156c` — the hull the `0x1000` arm draws a second box for when it
 *  differs from `m_eHull` (`+0x1568`). **SEAM**: no port word carries a second hull; answers
 *  `HullKind`, which makes the two equal and takes retail's "no second box" arm. */
int32 RetailAlternateHullKind() const;

/** `CBaseAnimating::GetBonePosition01("Bip01", &pos, &ang)` (`0x1000f263`), the root-bone box of the
 *  `0x1000` arm. **SEAM**: no skeleton is readable from the kernel surface; answers false and the
 *  box is not drawn, which is what retail draws for a model with no such bone. */
bool RetailBonePosition(const TCHAR* BoneName, FVector& OutPositionUnits,
	FVector& OutAnglesDegrees) const;

/** The active weapon's four range words — `+0x8c0`/`+0x8c4` (the pair the `0x20000000` arm takes the
 *  MAX of) and `+0x8b8`/`+0x8bc` (the pair it takes the MIN of), in source units. **SEAM**: story
 *  29c-1's `ActiveWeaponEntity` answers null, so this answers false and the arm draws retail's
 *  unarmed rings, 2000.0 and 0.0. */
bool ActiveWeaponRangeRingsUnits(float& OutFarUnits, float& OutNearUnits) const;

/** The active weapon's five text-overlay words — the name slot 0x570 answers and the four ammo
 *  numbers `Weapon: %s (%d/%d) (%d/%d)` prints: `m_iClip1` (`+0x74c`), the count for
 *  `m_iPrimaryAmmoType` (`+0x744`), `m_iClip2` (`+0x750`) and the count for `m_iSecondaryAmmoType`
 *  (`+0x748`). A negative ammo TYPE answers -1 without asking for a count, which is retail's own
 *  guard. **SEAM**: answers false, and the line becomes retail's `UNARMED`. */
bool ActiveWeaponTextWords(FString& OutName, int32& OutClip1, int32& OutAmmo1, int32& OutClip2,
	int32& OutAmmo2) const;

/** `m_pSquad` (`+0x5da4`) and its name at `+0x4`, which the `0x80000` arm appends to `Squad: %c : `.
 *  Retail reads the squad OBJECT here and does NOT apply the `m_iSquadDisconnected` gate
 *  `ConnectedSquad()` applies — the gate only picks the `%c`. The port has no squad object; the
 *  recovered mapping is that `InitSquad` stands one for any NPC whose `m_SquadName` is set and that
 *  the object's `+0x4` IS that name, so a non-empty `SquadName` answers true with it. */
bool SquadObjectName(FString& OutName) const;

/** `CNPC_Crow::m_nMorale` (`+0x5f50`) and `m_flEnemyDist` (`+0x5f4c`), the crow overlay's two words.
 *  **SEAM**: `CNPC_Crow` stands no port words (it is not a registered spawn leaf here); the morale
 *  answers 0 and the distance 0.0. */
int32 CrowMorale() const;
float CrowEnemyDistUnits() const;

/** `CNPC_VCop::m_hPursuitPlayer` (`+0x6664`), the ` Pursuit` suffix of the cop's relationship label.
 *  **SEAM**: no port word; answers null. */
FElysiumEntity* CopPursuitPlayer() const;

/** The cop class's two STATIC globals — `DAT_1093ac3c`, the provoker handle every cop shares, and
 *  `_DAT_1093aca8`, the curtime it expires at. `CNPC_VCop::OnSeeEntity` (`0x10370560`, family
 *  Senses10) is the writer and both this body and `CNPC_VCop::IRelationType` (family Conditions10)
 *  are the readers. **SEAM**: neither global is stood here; answers false, which drops the
 *  ` Suspect` suffix. */
bool CopSuspectIs(const FElysiumEntity* Candidate) const;

/** `0x1017f8d0` (`curtime < player->m_flHeightenedAlertExpireTimer`, `+0x1d1c`) and `0x1017f770`
 *  (`player->m_iCopsInPursuitCount`, `+0x1d10`) off the candidate's `+0xa8 m_pPlayer` — the ` Alert`
 *  and ` Count%d` suffixes. Both words exist on `FElysiumPlayer`; a non-player candidate answers
 *  false / 0, which is retail's null-`+0xa8` arm. */
bool PlayerHeightenedAlert(const FElysiumEntity* Candidate) const;
int32 PlayerCopsInPursuitCount(const FElysiumEntity* Candidate) const;

/** `CNPC_VTzimisce`'s carry bit (`0x103be130`) — the gate on the `Body - …` line's latch. **SEAM**:
 *  the body has no verdict row and no port counterpart; answers false, so the latch never updates
 *  and the two globals keep whatever the last Tzimisce put there. */
bool TzimisceIsCarryingBody() const;

/** `CNPC_VNewscaster`'s story-queue overlay (`0x103a0ff0`), whose lines the newscaster's slot-124
 *  body adds to the SAME budget — the return is a COUNT, not a line index. **SEAM**: the body is
 *  family Species' row (band 5–9, `FElysiumNpc::FUN_103a0ff0`) and is already ported there; this
 *  answers 0 until the two are wired, so the newscaster's return is the Troika body's unchanged. */
int32 NewscasterStoryOverlayLines(int32 FirstLine);

/** `0x1029c4a0` — the 400-byte view-cone/fan draw the `0x400000` arm calls three times. It has no
 *  verdict row (it is not one of the band's core functions), so its body is not written here; the
 *  CALL and its eight arguments are recorded verbatim on the overlay channel, because the arm and
 *  its constants are what slot 123 is observable for. */
void EmitViewConeOverlay(float LengthUnits, float FovDot, uint32 Arg3, int32 Arg4, int32 Arg5,
	int32 Arg6, int32 Arg7, int32 Arg8) const;

/** `0x1029c9f0` (the look distance scaled for a specific player) and `0x1029ca30` (the third cone's
 *  own length). Neither has a verdict row. **SEAM**: both answer the distance handed in, so the
 *  three cones differ only by the constants the arm pushes, which is what the row records. */
float ViewConeLengthForPlayer(float BaseLengthUnits, const FElysiumEntity* Player) const;
float ViewConeThirdLength(float BaseLengthUnits) const;

/** `CAI_Hint`'s two overlay words for the facing pair: `m_nHintType` (`+0x5dc`) and the yaw at
 *  `+0x454`, plus `0x102d12e0`'s own yaw and the hint's origin. **SEAM**: family Hints records that
 *  hints are node INDICES here with no type store; answers false and the arm is skipped. */
bool HintOverlayWords(int32& OutHintType, float& OutHintYawDegrees, float& OutNodeYawDegrees,
	FVector& OutOriginUnits) const;

/** `CBaseEntity::WorldSpaceCenter()` (slot 192, vtable `+0x300`) in SOURCE units, the endpoints of
 *  the relationship-line walk. `FElysiumEntity` stands no collision OBB (story 29c-1's
 *  `CollisionObbExtentsUnits` says the same), so this answers the entity's ORIGIN — which is what
 *  retail's own body answers for an entity whose OBB is a point. */
static FVector RetailWorldSpaceCenterUnits(const FElysiumEntity* Entity);

/** `CBaseCombatCharacter::BodyTarget(pos, bNoisy, …)` (slot 197, vtable `+0x314`) in SOURCE units —
 *  the five points the enemy-body arm boxes. **SEAM**: no port body-target blend stands here;
 *  answers the entity's `EyePosition()`, which is retail's own answer when the blend weight is 1.
 *  Retail passes an UNINITIALISED stack vector as `posSrc`; that read is not reproduced. */
static FVector RetailBodyTargetUnits(const FElysiumEntity* Entity);

/** `CBaseEntity::FindInSphere`-style walk (`0x100f8490` over `gEntList` `0x106eb5d8`) with mask
 *  `0x820` and radius 4096.0 source units, the relationship-line arm's iteration, in the order the
 *  entity list yields. The port's `FElysiumEntityWorld` IS that list, so the walk is real; what is a
 *  seam is the `0x820` mask, which is answered as "has an NPC" — the arm's own next test. */
TArray<FElysiumNpc*> RelationshipLineCandidates(const FVector& CentreUnits,
	float RadiusUnits) const;

/** `CAI_BaseNPCTroika`'s eye-to-eye trace against `m_hClosestPlayer` in the `0x5a` arm of
 *  `NPCThinkDebugPre`: `UTIL_TraceLine(EyePosition(), player->EyePosition(), MASK_ALL, filter, &tr)`
 *  then `tr.fraction != 1.0`. **SEAM**: the substrate has no ray cast on the kernel surface (family
 *  Senses records the same); answers false with a null blocker, which is retail's clear-line arm and
 *  therefore the GREEN line. */
bool EyeToEyeBlocker(const FElysiumEntity* Player, FElysiumEntity*& OutBlocker) const;

/** `0x10278650(out, in, 0.0, 0.0)` — the head-adjusted anchor the second `NPCThinkDebugPre` ConVar
 *  block draws its ±2 box at. **SEAM**: the body is family **Motor10**'s row in this same band
 *  (`0x10278650` → `FElysiumNpc::ComputeStandoffAnchorOffset`); answers the position handed in until
 *  the two are wired. */
FVector RetailStandoffAnchorUnits(const FVector& EyeUnits) const;

/** `0x1028e030` (mode 1) and `0x1028e060` (mode 2), the two bodies the third `NPCThinkDebugPre`
 *  ConVar selects between. Neither has a verdict row. **SEAM**: recorded on the `DevMsg` channel by
 *  address so the selection is visible; nothing is drawn. */
void RunThinkDebugPreExtra(int32 Mode) const;

/** The choreo scene the Troika text body's tail asks for: the scene entity's `+0x450` name and the
 *  `CChoreoScene*` at `+0x4bc` with `GetTime()` (`0x1007dfa0`). Story 29c-1's Troika STAT body seams
 *  the same object and takes the same refusal. Answers whether the scene ENTITY resolved, and fills
 *  the name; `bOutScenePlaying` stays false because no scene object is stood. */
bool DialogSceneWords(FString& OutSceneName, bool& bOutScenePlaying, double& OutSceneTime) const;

/** Slot 9's string, the one line `CNPC_VHengeyokai#124` adds. Retail takes the FIRST word of
 *  whatever slot 9 returns and substitutes the empty string for null. **SEAM**: slot 9 is a
 *  generated stub owned by another story; answers the empty string, which is retail's null arm. */
FString HengeyokaiSlot9String() const;

/** `CNPC_VZombie`'s own condition bitfield at `+0x5c5c`, walked 0..0xbf. **SEAM**: `+0x5c5c` is one
 *  of the schedule block's six words with no port member of its own; answers false for every id. */
bool ZombieConditionBit(int32 ConditionId) const;
