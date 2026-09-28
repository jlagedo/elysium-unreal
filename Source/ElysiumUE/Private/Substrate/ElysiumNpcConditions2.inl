// Story 0019/8 (29e under the strict verdict), family **Conditions19** -- `CAI_BaseNPCTroika`'s
// helper declarations.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`; the definitions are in
// `ElysiumNpcConditions2.cpp`, or generated in the slot files for a slot body.
//
// Owns (Conditions19's `rule` rows): 0x102b27f0 CAI_BaseNPCTroika::GatherConditions. The named
// condition constants the family pushes by number are in `ElysiumNpcBaseConditions2.inl` (the base
// class), which this class inherits. Walked prose:
// `docs/vtmb/npc-ai/conditions-and-states.md` § "Story 8, family Conditions19".

// --- The Troika half of `CAI_BaseNPC::GatherConditions` (`0x1026ec30`, `1026eec1..1026efa4`) --------

/** `1026eec1..1026efa4`, run on `m_pBaseNPCTroika (+0x98)` after the target check: slot 586
 *  `GetBestSeeUnknown()` resolved (dead-inclusive, the handle-table test) feeds `0x1028e480`; the
 *  handle at `+0x6240` (`ScheduleHost.MoveTarget`) resolved feeds `0x1028e980`; then `0x1028e790`
 *  unconditionally. */
void Conditions19TroikaGoalUpkeep();

/** SEAM for `0x1028e480` (checklist-19-29 `mechanism`, "goal-actor tracking of
 *  UPathFollowingComponent behind the 0018 nav seam"): re-points / re-aims a GOALTYPE 7 navigator goal
 *  at the see-unknown entity. The kernel has no navigator goal word (`m_pNavigator +0x5d34 +0x18`),
 *  so this records the call and moves nothing. */
void Conditions19UpdateApproachGoalPos(FElysiumEntity* Target);
int32 Conditions19ApproachGoalCalls = 0;

/** SEAM for `0x1028e980` (checklist-19-29 `mechanism`): refreshes the navigator's goal position when
 *  the move-target entity moved farther than `_DAT_104454c8`. Same absence; records the call. */
void Conditions19RefreshGoalPosition(FElysiumEntity* Target);
int32 Conditions19RefreshGoalCalls = 0;

/** `0x1028e790` (126 bytes, no checklist row of its own): with an enemy (slot 167), the report delay
 *  `0x1028e700(ENEMY_OCCLUDED 0x48, &m_flOccludedReportTimeE +0x62cc)`, and when `ENEMY_OCCLUDED` no
 *  longer stands the occlusion edge `0x10270180(GetEnemy(), false)`; then, with a live
 *  `m_hTargetEnt`, `0x1028e700(TARGET_OCCLUDED 0x49, &m_flOccludedReportTimeT +0x62d0)`. */
void Conditions19OcclusionReportUpkeep();

// `0x1028e700` is the landed `RefreshOccludedCondition(Cond, Stamp, Now)`
// (`ElysiumNpcConditionsBodies.cpp`); `0x1028e790` calls it.

// --- `CAI_BaseNPCTroika::GatherConditions` (`0x102b27f0`)'s callees -------------------------------

/** `0x1028fa50` (no checklist row of its own; the packet's verdict miscalled it a second law sweep):
 *  the SEE_CORPSE sweep. `curtime >= m_flCorpseConditionTimer (+0x6608)` re-arms it to `curtime +
 *  RandomFloat(2.0, 2.5)`, clears `SEE_CORPSE 0x3d` and `SEE_CORPSE_FRIEND 0x3e`, and walks the
 *  corpse query's answers from the last down: a null corpse or one with no `+0x98` raises 0x3e then
 *  0x3d; otherwise slot 404 `IRelationType` 3/4 raises 0x3e then 0x3d, 1/2 raises 0x3d (jump table
 *  `0x1028fb20`), anything else nothing. */
void Conditions19GatherCorpse(double Now);

/** SEAM for `0x102cabd0(DAT_109253f8, this, out, 4)`, the corpse query (up to four). No corpse
 *  registry stands on the kernel; answers 0 corpses. */
int32 Conditions19CorpseQuery(FElysiumEntity** OutCorpses, int32 MaxCorpses);

/** `0x102b2730` (no checklist row): the squad sweep. Only with a connected squad (`+0x5bb0 < 1` and
 *  `m_pSquad +0x5da4`): with an enemy (slot 168), `GetEnemies()->LastTimeSeen(enemy)` (`0x102e0150`)
 *  `+ 0.2` (`_DAT_10451ab4`) not before `curtime` raises `SQUAD_SEE_ENEMY 0x31`, and `SQUAD_LOS_ENEMY
 *  0x32` when `HAVE_ENEMY_LOS` stands; a dead `HasCondition(SEE_ENEMY)` closes the body. Clears
 *  neither. */
void Conditions19GatherSquad(double Now);

/** SEAM for `m_pNavigator +0x14`, the squared distance to the route's end written by `0x102ed430`.
 *  Read only behind `NavigatorGoalIsActive()` (`0x102ee6a0`), which reads the mover's active-goal
 *  bit since the L05 integration; answers 0. With a route live and `+0x6324` above zero this raises
 *  `COND 0x15` (`102b297f`); nothing in the port writes `+0x6324` non-zero yet, and the mover keeps
 *  no readable route end (`NavGoalPosition` answers none) to measure. */
float Conditions19NavPathEndDistSqrUnits() const;

/** `+0x0ff8 m_hBodyFireParticles[18]` (CBaseAnimating's burning-body particle handles). No port
 *  system spawns body fire particles, so no slot is ever written and none resolves. */
static constexpr int32 Conditions19BodyFireParticleCount = 18;
FElysiumEntityHandle BodyFireParticles[Conditions19BodyFireParticleCount];

/** SEAM for the particle's `+0x484` word, the "burning" test `0x102b2cee` reads. Unrecovered field;
 *  answers false (reachable only through a resolved handle, which nothing writes). */
bool Conditions19FireParticleLive(const FElysiumEntity& Particle) const;

/** The Troika body's constants. */
static constexpr double Cond19StopBackupDot = 0.707;              // double `[0x1049ae78]`
static constexpr double Cond19WallTraceInterval = 3.0;            // `_DAT_10449258`
static constexpr float Cond19WallTraceLengthUnits = 32.0f;        // `_DAT_10462990`
static constexpr int32 Cond19WallTraceMask = 0x2000b;             // `PUSH 0x2000b` at `0x102b2f3b`
static constexpr float Cond19DetectedAttackDelayMin = 0.9f;       // `PUSH 0x3f666666`
static constexpr float Cond19DetectedAttackDelayMax = 1.3f;       // `PUSH 0x3fa66666`
static constexpr float Cond19CorpseRetimeMin = 2.0f;              // `PUSH 0x40000000` at `0x1028fa7d`
static constexpr float Cond19CorpseRetimeMax = 2.5f;              // `PUSH 0x40200000` at `0x1028fa78`
static constexpr double Cond19SquadSeenWindow = 0.2;              // `_DAT_10451ab4`

