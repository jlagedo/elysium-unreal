// `CAI_BaseNPC`'s declarations of the `EntityChain` family (story 5 step 5),
// moved from `ElysiumNpcEntityChain*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseEntityChain.cpp`.

// +0x01b0 `m_NetworkChangeState` (`CEntityNetworkChangeState`, `layout.md` `+0x01b0`) — an
// 8-byte record the SDK does not declare: a bool at +0, `m_bChanged` at +1, a second bool at +2, a
// short interval at +4 and a short countdown at +6. Slot 88 (`0x10026b50`) asks whether a send is
// due; slot 89 — this family's — clears the two flag bytes. SEAM-ADJACENT: nothing in this runtime
// networks an entity, so the interval/countdown stand at 0 and only the two flags are written, by
// the one body that writes them in retail.
struct FNetworkChangeState
{
	bool bByte0 = false;       // +0x00 — set by the static prop/brush Spawns through 0x101466e0
	bool bChanged = false;     // +0x01 m_bChanged — SetAbsOrigin/SetModel/SetLocalVelocity set it
	bool bByte2 = false;       // +0x02 — the second flag 0x10146790 clears; its writer is unrecovered
	int16 IntervalTicks = 0;   // +0x04
	int16 CountdownTicks = 0;  // +0x06
};

/** `IPhysicsObject::GetPosition(&origin, &angles)` — the physics object's own transform, which slot
 *  226's movetype-7 arm reads and writes back through slots 216/218. **SEAM**: this runtime stands
 *  no `IPhysicsObject`; the pointer arrives through the generated `void*` and nothing can be read
 *  off it, so this answers false and the arm writes nothing. */
bool PhysicsObjectPosition(const void* PhysicsObject, FVector& OutOrigin, FRotator& OutAngles) const;

/** `0x101a6d00` — slot 580's BASE body (`CAI_BaseNPC`), `return &DAT_1090ff08`, as the port's typed
 *  slot-580 virtual; `FElysiumNpc` overrides it with the Troika's `0x101aa790` (story 5 step 5). The base answers a
 *  DIFFERENT `CAI_ClassScheduleIdSpace` from the Troika line's `&DAT_10924248`, and family
 *  Schedule's table has no row for it, so this is the row and the reading of it. */
virtual const FElysiumLocalIdSpace* ClassScheduleIdSpace() const;

/** This NPC's live `CAI_LocalIdSpace` for one category, out of the loaded corpus. Slot 580's own
 *  answer, and never null while a corpus is loaded. */
const FElysiumLocalIdSpace* IdSpace(EElysiumIdCategory Category) const;

/** `0x101a8930` — `CCineNPC::CanInterrupt`. `m_interruptable` (+0x5f90) set AND the resolved
 *  `m_hTargetEnt` (+0x5ce4) answering slot 158 `IsAlive`. A missing target is false, not true. */
bool CineCanInterrupt() const;

/** `0x100994c0`'s companion read, exposed so a case can assert slot 271 against the table's own
 *  starting index. `GetFirstGestureLayer()` (slot 267) answers 0 for every class in the hierarchy;
 *  retail's scan starts THERE and refuses outright when it is 4 or more. */
int32 FirstGestureLayerOrRefusal() const;

/** The easing inside `0x101c10d0`, on its own so the cubic is assertable without a mover:
 *  `(t*t + 1)*t - (t/D)*(D*D + 1)*t`. Zero at `t == 0` and at `t == D`. */
static float MoveReboundBlend(float T, float Duration);

/** `CCineNPC::m_interruptable` (+0x5f90), read through the scripted-sequence entity that already
 *  stores the word. A missing or non-sequence owner answers false. */
bool CineIsInterruptable() const;

/** The `CBaseEntity` mover words slot 135 reads. Not one of them has a port member
 *  (`docs/vtmb/npc-kernel/layout.md` types them on `CBaseEntity`; the shape map's band starts at
 *  `+0x1a40`), so they arrive through one seam rather than seven. */
struct FMoveRebound
{
	float LocalTime = 0.f;        // m_flLocalTime
	float MoveDoneTime = 0.f;     // m_flMoveDoneTime
	float StartTime = 0.f;        // m_flMoveReboundStartTime
	float Duration = 0.f;         // m_flMoveReboundDuration
	FVector Velocity = FVector::ZeroVector;      // m_flMoveReboundVelocity[3]
	FVector AngVelocity = FVector::ZeroVector;   // m_flMoveReboundAngVelocity[3]
	FVector FinalDest = FVector::ZeroVector;     // m_vecFinalDest[3]
	FVector FinalAngle = FVector::ZeroVector;    // m_vecFinalAngle[3]
	FVector LocalOrigin = FVector::ZeroVector;   // slot 220 GetLocalOrigin
	FVector LocalAngle = FVector::ZeroVector;    // slot 221 GetLocalAngles
};

FNetworkChangeState NetworkChangeState;   // +0x01b0

/** `CBaseEntity::PhysicsTouchTriggers(0)` (`0x100b0f30`) and `CBaseEntity::PhysicsRelinkChildren`,
 *  the two calls slot 226's movetype-7 arm ends on, IN THAT ORDER. **SEAM**: this runtime's overlap
 *  routing is the world's, not the entity's, and it carries no child relink. Recorded so the ORDER
 *  is assertable; read by the test and by nothing else. */
TArray<FString> PhysicsUpdateCalls;

/** `CBaseEntity::VPhysicsUpdatePusher(physicsObject)` — the arm movetypes 1 and 8 take. **SEAM**:
 *  the same missing physics object. Recorded into `PhysicsUpdateCalls`. */
void VPhysicsUpdatePusher(const void* PhysicsObject);

/** `edict_t + 0x40`'s `IServerNetworkable::GetBaseEntity()` (`+0x10`) — the hop slot 165 makes
 *  before dispatching slot 166. **SEAM**: there are no edicts here. The generated signature hands
 *  the edict in as `void*`; this answers null, which takes retail's own "no networkable" arm and
 *  dispatches slot 166 with 0 — the SAME call retail makes, not a refusal of it. */
FElysiumEntity* EntityOfEdict(const void* Edict) const;

/** `IPhysics`'s "is this vphysics object static/asleep" query — `(*DAT_1070b250 + 0x18)(index)`,
 *  the second arm of `0x100b5110`. **SEAM**: answers false, so a `SOLID_VPHYSICS` entity is not
 *  standable, which is retail's answer for a moving one. */
bool PhysicsObjectIsStandable(const FElysiumEntity& Entity) const;

/** `DAT_1072b360`, the single global word slot 240 returns. It is the CPython interop side of the
 *  entity — whatever the embedded interpreter last stored there — and this runtime embeds no
 *  interpreter. **SEAM**: answers null, which is the global's own pre-interpreter value. */
void* PythonInteropObject() const;

/** `FUN_100b5110` (`0x100b5110`) — the shared standability helper slots 159 and 164 both end in.
 *  `GetSolid() == 1` (`SOLID_BSP`) is standable outright; `SOLID_VPHYSICS` (6) asks the physics
 *  object; everything else is not. NAMED from what it does, not from a recovered symbol. */
bool IsStandableSolid() const;

/** **SEAM**: answers the resting state and FALSE, which makes slot 135's first gate refuse — which
 *  is what retail answers for an NPC that is not rebounding. */
bool MoveReboundState(FMoveRebound& Out) const;

/** `CBaseEntity::SetLocalVelocity` / `SetLocalAngularVelocity`, slot 135's two writes. The port's
 *  `FElysiumEntity::Velocity` and `AngularVelocity` ARE those words. */
void SetLocalVelocity(const FVector& NewVelocity);

void SetLocalAngularVelocity(const FVector& NewAngularVelocity);

/** `thunk_FUN_10075b70(record.event)` — the release both scene-event removers call before they
 *  compact. **SEAM**: the port's scene events are parsed data owned by the scene asset and are not
 *  reference-counted; the call is recorded so the SEQUENCE is assertable. */
void ReleaseSceneEvent(const FSceneEventRecord& Record);

/** How many times that release ran, which is the only observable it has here. */
int32 SceneEventReleases = 0;

/** `0x102ea280` — the GLOBAL-to-LOCAL range translation slots 447 and 450 both forward into.
 *  A null space is retail's end-of-chain and answers -1. */
static int32 GlobalToLocalId(const FElysiumLocalIdSpace* Space, int32 GlobalId);

