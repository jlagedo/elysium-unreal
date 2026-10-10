// `CBaseEntity`'s hand-written slot bodies and the members they reach (story 5 step 6),
// moved up the chain from `FElysiumNpcBase`. Included inside `class FElysiumEntity`
// (`ElysiumEntity.h`), after its generated slot surface; the definitions are in
// `Private/Substrate/ElysiumEntitySlotBodies.cpp`.

/** What slot 333's eye-maintenance arms asked of the seams they stand for (`Elysium.Substrate.
 *  NpcKernelClosure.*` asserts the seam was reached). Read by the tests and by nothing else, written
 *  by no rule, and never saved. The plumbing refusals it also counted (slots 79, 80, 82, 88, 102,
 *  184, 225, 346) were closed in 0019/6 (to a service or a one-line forward), and their counters
 *  went with them. */
struct FClosureRefusals
{
	int32 BlinkCadence = 0;            // slot 333 `0x102bff20` arm 1 -> `FElysiumBlinkSchedule`
	int32 EyeFidgetDriver = 0;         // slot 333 `0x102c0010` -> the saccade layer
	int32 BaseEyeMaintainer = 0;       // slot 333 tail `0x1026b810` -> `TickGaze`
};

/** Retail's `trace_t` as the same bodies read it. `+0x0c` is `endpos`, `+0x44` the hitgroup,
 *  `+0x48` the physics bone (a `short`, sign-extended by `0x102667e7`) and `+0x50` the ammo type
 *  `TraceAttack`'s tail copies into the sub-packet through `0x101c2a10`. */
struct FElysiumTraceHit
{
	FVector EndPosUnits = FVector::ZeroVector;  // +0x0c  SOURCE units
	FVector PlaneNormal = FVector::ZeroVector;  // the surface normal the decal seam needs
	int32 HitGroup = 0;                         // +0x44
	int32 PhysicsBone = 0;                      // +0x48
	int32 AmmoType = INDEX_NONE;                // +0x50
};

/** What `TraceBleed` decided on each pass that got past its three refusals: the noise half-width and
 *  the trace count its damage band selected, and the segment of the last trace it ran. Retail's
 *  effect is a blood decal on a wall, which the decal seam cannot answer for headless; the TABLE is
 *  the recovered concern and this is what makes it measurable. */
struct FTraceBleedPass
{
	float Noise = 0.f;
	int32 TraceCount = 0;
	FVector LastStartUnits = FVector::ZeroVector;
	FVector LastEndUnits = FVector::ZeroVector;
};

/** Retail's `FireBulletsInfo_t` as `0x10268900` reads it, each word at its retail offset. The
 *  generated slot signature spells it `void*` because the type does not exist in this runtime.
 *  Only the words the body touches are carried; the gaps are named. */
struct FElysiumFireBulletsInfo
{
	int32 Repeats = 0;                          // +0x00  the OUTER loop count
	int32 Bullets = 0;                          // +0x04  the INNER loop count
	FVector SrcUnits = FVector::ZeroVector;     // +0x08  m_vecSrc, SOURCE units
	FVector DirShooting = FVector::ZeroVector;  // +0x14  the forward the basis is built from
	FVector DirCurrent = FVector::ZeroVector;   // +0x20  the per-shot working direction (written)
	FVector EndUnits = FVector::ZeroVector;     // +0x2c  the per-shot endpoint (written)
	FVector Spread = FVector::ZeroVector;       // +0x38  m_vecSpread; only x and y are read
	float DistanceUnits = 0.f;                  // +0x44  how far the endpoint is thrown
	int32 AmmoFlags = 0;                        // +0x58  WRITTEN from the ammo def
	int32 AmmoType = 0;                         // +0x8c  the ammo-def index
	FElysiumEntityHandle Attacker;              // +0x94  defaulted to the shooter when unset
	// +0xa0's low byte. Bit 0 forces the tracer arm on its own.
	int32 TracerFlags = 0;
	// +0xa4, the per-shot scalar retail divides by the skill table and hands the tracer effect as
	// its fifth argument. **Unrecovered:** its retail name — `0x101ef900`'s signature is not pinned
	// by this call site.
	float TracerScale = 0.f;
	FString TracerName;                         // +0xa8  empty disables the tracer arm
	FElysiumEntityHandle LastVictim;            // +0xbc  what the trace pass hit, read back as the
	                                            //        tally key
};

/** SEAM: `RangedDamagePerVictim` (`0x10268330`) — the once-per-victim-per-repeat damage call whose
 *  fraction is `hits / Bullets`. Not a row of this family and not a vtable slot; recorded with the
 *  victim and the fraction, which is what `FireBullets` decides. */
struct FRangedDamagePerVictimCall
{
	FElysiumEntityHandle Victim;
	float Fraction = 0.f;
};

/** SEAM: `0x10267b60`, the real per-bullet trace-and-damage pass — the trace itself, the water
 *  splitting, the impact effects, the decal and the damage commit. This runtime has no bullet
 *  trace pipeline at all (nothing fires a weapon yet), so the pass is RECORDED — the segment and
 *  the scalar are what `FireBullets` itself decided — and it reports no victim, which leaves
 *  `Info.LastVictim` unset and the tally empty exactly as a bullet that hit the sky does. */
struct FFireBulletsTrace
{
	FVector SrcUnits = FVector::ZeroVector;
	FVector EndUnits = FVector::ZeroVector;
	FVector DirUnits = FVector::ZeroVector;
	float TracerScale = 0.f;
	int32 FilterWord = 0;
};

/** SEAM: the tracer effect `0x101ef900` builds and `0x101ef540` sends, with `1.0 / Bullets` as the
 *  per-shot fraction (`_DAT_104454c0` is `1.0`). No tracer effect exists in this runtime; the call
 *  is recorded with retail's own fraction. */
struct FBulletTracerCall
{
	FString Name;
	FVector EndUnits = FVector::ZeroVector;
	float Fraction = 0.f;
	float TracerScale = 0.f;
};

/** Where slot 215's answer LIVES, so the slot can hand out an address at all.
 *
 *  `0x100b4c30` and slot 192's `0x10027160` are the SAME body compiled twice: identical arms,
 *  identical arithmetic, differing only in how the answer leaves — 192 writes through the hidden
 *  struct-return pointer, 215 returns a pointer into the image's rotating temp-vector ring
 *  (`DAT_109f0cc0`, 128 entries, index `DAT_106b856c` advanced with `& 0x7f`). The ring is retail's
 *  way of returning a `const Vector&` from a value computation; nothing in the port has one.
 *
 *  **Named modernization.** This is a ONE-DEEP PER-ENTITY cache rather than a 128-deep global ring,
 *  so the reference slot 215 hands out stays valid for exactly as long as this NPC does and is
 *  invalidated by the next slot-215 call on the SAME body instead of by the 128th call on any body.
 *  The retail aliasing that difference removes — a caller holding the reference across 128
 *  intervening `WorldSpaceCenter()` calls and silently reading someone else's centre — has no
 *  reachable instance in layers 0–9: every one of the 49 virtual call sites reads the answer
 *  before it calls anything. `mutable` because retail's slot is `const` and still writes the ring.
 *  CENTIMETRES, like every other length on this struct. */
mutable FVector WorldSpaceCentreCacheCm = FVector::ZeroVector;

/** Session-only, never saved; see `FClosureRefusals`. */
FClosureRefusals ClosureRefusals;

// +0x01fc m_takedamage (datamap, CBaseEntity). 0 = DAMAGE_NO, 1 = DAMAGE_EVENTS_ONLY,
// 2 = DAMAGE_YES. Retail's default for a live NPC is 2, which is what this seeds.
int32 TakeDamageMode = 2;

int32 RenderMode = 0;         // +0x016c m_nRenderMode (CBaseEntity); 4 is kRenderTransAlpha

TArray<FTraceBleedPass> TraceBleedPasses;

/** `+0x8c m_hUseActivator` (`docs/vtmb/npc-kernel/layout.md:34`) — the handle of whoever last began a
 *  `+use` on this entity, written by slot 39 and cleared to `-1` by slot 42 and by a null activator;
 *  the base constructor `0x1009d980` writes `-1` (`1009d9ad`). Not a datamap field. It has no reader
 *  in this family's closure — the two slots are its only toucher — so it is carried as the recorded
 *  write rather than wired to a consumer. */
FElysiumEntityHandle UseActivator;   // +0x8c m_hUseActivator

/** `+0x038c m_vecSize` and the two words after it — the box `CBaseEntity::SetSize` (`0x100b1890`,
 *  slot 213) writes and `CBaseEntity::GetSize` (`0x100b1960`, slot 214) returns the address of.
 *  SOURCE UNITS (inches), the retail word: slot 213's one caller is `UTIL_SetSize` `0x101cf3c0`,
 *  which hands it `maxs - mins` of the box it just gave `SetCollisionBounds`, and the readers
 *  (`CBaseDoor::vfunc103` 0x100ef260 / `vfunc245` 0x100f0a40, `CBaseButton::Spawn` 0x100c8d60,
 *  `CFuncMoveLinear::Spawn` 0x10116030) subtract retail's 2.0 inset from it. The base constructor
 *  never writes it (zero from the `calloc` allocation; `walks/L0-r016.md`). */
FVector SizeUnits = FVector::ZeroVector;

/** `+0x1ddc`, read by `FUN_10160680` — a float scaled by the compiled constant `DAT_10725c9c`.
 *  **Unrecovered**: the body has one direct caller, no vtable slot, and neither the retail field
 *  name nor the class that owns the offset is settled. Declared by offset, as 29b declares an
 *  unsettled word. */
float Field_0x1ddc = 0.f;

/** `+0x0368 m_CollisionGroup`, a `CBaseEntity` word below the NPC table. The ONE input of slot 91
 *  `ShouldCollide` (`0x100b4de0`). Nothing in this runtime writes it yet: this substrate's collision
 *  is Unreal's channel set on the body, so the retail group number has no producer. Declared so the
 *  rule has the word it reads rather than a guess. Retail's `COLLISION_GROUP_DEBRIS` is 1. */
int32 CollisionGroup = 0;

/** `DAT_1072cb48`, the file-static filter word `FireBullets` writes before it traces
 *  (`flags | 0x1000`) and every trace in the pass reads. A global in retail, carried per NPC here
 *  because nothing else in this runtime reads it and a shared cell with one writer is the same
 *  observation. The two trace filters pushed beside it (`0x101c2c60`, `0x101c2c30`) have no
 *  counterpart: this runtime's traces take a channel, not a filter stack. */
int32 FireBulletsFilterWord = 0;

TArray<FFireBulletsTrace> FireBulletsTraces;

TArray<FBulletTracerCall> BulletTracerCalls;

TArray<FRangedDamagePerVictimCall> RangedDamagePerVictimCalls;

/** `edict_t + 0x40`'s `IServerNetworkable::GetBaseEntity()` (`+0x10`) — the hop slot 165 makes
 *  before dispatching slot 166. **SEAM**: there are no edicts here. The generated signature hands
 *  the edict in as `void*`; this answers null, which takes retail's own "no networkable" arm and
 *  dispatches slot 166 with 0 — the SAME call retail makes, not a refusal of it. */
FElysiumEntity* EntityOfEdict(const void* Edict) const;

/** `FUN_100b5110` (`0x100b5110`) — the SDK's `CBaseEntity::IsBSPModel` (`walks/L0-r021.md`), the
 *  helper slots 159, 163 (`IsViewable`) and 164 end in. `GetSolid() == 1` (`SOLID_BSP`) answers true
 *  outright; else `m = VModelInfoServer001 slot 2 GetModel(GetModelIndex())`, and a second
 *  `GetSolid() == 6` (`SOLID_VPHYSICS`) with `slot 6 GetModelType(m) == 1` (brush) answers true;
 *  everything else false. (`DAT_1070b250` is `VModelInfoServer001`, not a physics interface.) */
bool IsStandableSolid() const;

/** `FUN_10160680` — `*(float*)(this+0x1ddc) * DAT_10725c9c`. **Unrecovered**: one direct caller, no
 *  slot, and neither the field's retail name nor the constant's value is pinned. The constant is
 *  named at the definition and the scaling is the whole body. */
float ScaleField_0x1ddc() const;

/** `+0x00a8 m_pPlayer` — retail's "this entity is the player" self-pointer. Never set on this leaf,
 *  which is what makes `FireBullets`' spread gate read "spread unless the `0x2000000` bit is set",
 *  its tracer gate "flag bit 0, or skill above 2", and its `0x10160560` tail unreachable. */
bool FireBulletsShooterIsPlayer() const;

/** `0x10268170` — the unscaled spread offset one bullet adds to the forward. A rejection sample:
 *  two independent sums of two `RandomFloat(-0.5, 0.5)` draws, redrawn while `x*x + y*y > 1.0`,
 *  then `right * (spread.x * x) + up * (spread.y * y)`. A PLAYER shooter replaces BOTH spread
 *  components with `ScaleField_0x1ddc()` (`0x10160680`); this leaf is never the player, so the
 *  info's own pair is what is used and the substitution is named rather than run. */
FVector BulletSpreadOffset(const FVector& SpreadUnits, const FVector& RightAxis,
	const FVector& UpAxis);

/** SEAM: `GetAmmoDef()->Flags(index)` — the ammo-def singleton (`DAT_1070ba0c` vtable `+0xdc`) then
 *  `0x104276a0`, whose whole body is `(0 < i && i < m_nAmmoIndex) ? m_AmmoType[i].nFlags : 0`.
 *  Family **Damage** already recorded that this runtime keys ammo by the authored TYPE NAME and
 *  that the `CAmmoDef` index order is **unrecovered**; with no table every index is out of range,
 *  so this answers retail's own out-of-range answer, `0`. */
int32 AmmoDefFlags(int32 AmmoTypeIndex) const;

/** SEAM: the ranged skill `FireBullets` reads once per bullet — `0x101cda50` answers the local
 *  player when the server is single-player and not dedicated, and the body then walks that
 *  player's stat lists (`+0x13bc` count, `+0x13c0` table) for the first whose `+0x10` is `3` and
 *  asks it for stat `3` (`0x102012d0` `GetValue`); a player with no such list falls back to a
 *  lazily-built EMPTY `CVStatList_t` (`DAT_109f0b40`), whose `GetValue` answers `0`.
 *
 *  This runtime's sheet carries no `CVStatList_t` container keyed by retail's list type, so the
 *  join is **unrecovered** and the answer is `0` — which is retail's own answer both when the
 *  server is not single-player and when the player carries no type-3 list, i.e. the ADMITTING
 *  value: at `0` the tracer latch falls to the flag bit alone and the divisor is `1.0`. */
int32 ShooterRangedSkill() const;

void FireBulletsTracePass(FElysiumFireBulletsInfo& Info);

void EmitBulletTracer(const FElysiumFireBulletsInfo& Info, float Fraction);

void RangedDamagePerVictim(const FElysiumEntityHandle& Victim,
	const FElysiumFireBulletsInfo& Info, float Fraction);

/** `VectorVectors(forward, right, up)` (`0x10138a90`) — Source's basis-from-a-forward, with
 *  retail's degenerate arm reproduced: a forward whose x AND y are zero answers `right = (1,0,0)`
 *  and `up = (0, -forward.z, 0)`, which is NOT normalized and NOT orthogonal to a vertical
 *  forward. Otherwise `right = normalize(forward.y, -forward.x, 0)` and
 *  `up = normalize(cross(right, forward))`. */
static void VectorVectors(const FVector& Forward, FVector& OutRight, FVector& OutUp);

// +0x0050 `m_vecAttackExtents` -- the attack-partition margin `CBaseEntity::SetAttackExtents`
// (`0x1009af40`, slot 15) writes and `GetAttackExtents` (`0x1009b030`, slot 16) reads. A `CBaseEntity`
// word: step 5 had carried it on the base NPC's schedule host (0019 story 5 step 6). Port units.
FVector AttackExtentsCm = FVector::ZeroVector;

// `CBaseEntity::SetAttackExtents` `0x1009af40`, slot 15: the attack partition only, never the motor
// capsule.
void SetAttackExtents(const FVector& MarginCm) { AttackExtentsCm = MarginCm; }

// +`m_MoveType` / `m_MoveCollide`, the two `CBaseEntity` words `SetMoveType` (`0x100aad70`, slot 93)
// writes. **SEAM**: this runtime moves no entity by movetype; the words are carried so the one
// retail writer records what it was asked, and nothing reads them but the tests.
int32 RetailMoveType = 0;
// `m_hGroundEntity` (`+0x384`): the entity the last ground test found under this one. Slot 208
// `SetGroundEntity` 0x100b1420 writes it, slot 209 `GetGroundEntity` 0x100b1510 resolves it; the
// floor-facts seam's `GroundEntityHandle` lands here through `CheckOnGround` (0019/6).
FElysiumEntityHandle RetailGroundEntity;
int32 RetailMoveCollide = 0;

// `m_Collision` (`+0x270`, the embedded `CCollisionProperty`, `0x64` bytes; `walks/L0-r015.md`
// "Shared layout"), flat on the entity. Offsets are `coll+N` (= entity `+0x270+N`). The datamap
// names (`vtmb_fields CCollisionProperty`) are retail's; `coll+0x3c` (the owner) and `coll+0x46` (the
// spatial-partition handle) have no datamap row and no recovered name. `FUN_100dc190` 0x100dc190 is
// the member constructor (vftable, handle `0xffff`, then `FUN_100dc300(this, 0)`); `FUN_100dc300`
// 0x100dc300 the initializer the base constructor runs again with `owner = this`
// (`ConstructCollisionProperty` / `InitCollisionProperty` below).
FElysiumEntity* CollisionOwner = nullptr;                  // coll+0x3c, the owner entity
FVector CollMins = FVector::ZeroVector;               // coll+0x04 m_vecMins
FVector CollMaxs = FVector::ZeroVector;               // coll+0x10 m_vecMaxs
uint8 SurroundType = 0;                                    // coll+0x1c m_nSurroundType
float TriggerBloat = 0.f;                                  // coll+0x20 m_flTriggerBloat
FVector SpecifiedSurroundingMins = FVector::ZeroVector;    // coll+0x24 m_vecSpecifiedSurroundingMins
FVector SpecifiedSurroundingMaxs = FVector::ZeroVector;    // coll+0x30 m_vecSpecifiedSurroundingMaxs
// coll+0x40 `m_Solid` (abs `+0x2b0`, SolidType_t: NONE 0, BSP 1, BBOX 2, OBB 3, OBB_YAW 4, CUSTOM 5,
// VPHYSICS 6) and coll+0x44 `m_usSolidFlags` (abs `+0x2b4`, a SHORT; `FSOLID_NOT_SOLID 0x4`,
// `FSOLID_TRIGGER 0x8`; `0x1`, `0x10`, `0x40`, `0x80`, `0x100` unnamed in the retail docs). Kept under
// their historical port names: `RetailSolidType` / `RetailSolidFlags`. The setters are `SetSolid`
// (`FUN_100dc480` 0x100dc480) and `SetSolidFlags` (`FUN_100dc580` 0x100dc580) below; several NPC
// Spawn bodies still write the words directly with their own retail citations (`ElysiumNpcSpawn.cpp`,
// `ElysiumNpcSpawnSpecies.cpp`, `ElysiumNpcTestHull.cpp`, `ElysiumNpcPedestrian.cpp`), which skips
// the setters' change tails. `RetailSolidSets` counts the `SetSolid` calls (port-only, for
// `IsRetailNotSolid`). Independent of `RetailMoveType` above: retail's `SetMoveType` touches neither.
int32 RetailSolidType = 0;
uint32 RetailSolidFlags = 0;
int32 RetailSolidSets = 0;
// coll+0x46: the spatial-partition handle, `0xffff` = none. Written `0xffff` by `FUN_100dc190` and
// never anything else here: the engine's `CreateHandle` (the relink) is the one writer of a real
// handle, and that partition is engine-replaced (Unreal's overlap queries). `FUN_100ddc40` 0x100ddc40
// therefore always takes its first exit.
uint16 PartitionHandle = 0xffff;
float CollisionRadius = 0.f;                               // coll+0x48 m_flRadius
FVector SurroundingMins = FVector::ZeroVector;             // coll+0x4c m_vecSurroundingMins
FVector SurroundingMaxs = FVector::ZeroVector;             // coll+0x58 m_vecSurroundingMaxs

// `FUN_100dc190` 0x100dc190: the `CCollisionProperty` member constructor the base constructor
// `0x1009d980` runs first (`1009da53`): vftable, `coll+0x46 = 0xffff`, `FUN_100dc300(this, 0)`.
void ConstructCollisionProperty();
// `FUN_100dc300` 0x100dc300: owner, then every field zeroed in retail's store order, the two
// surrounding vectors from the zero-vector global `DAT_1070d1b0`. Runs twice per construction
// (owner 0 from `FUN_100dc190`, then owner `this` from the base constructor at `1009db06`).
void InitCollisionProperty(FElysiumEntity* Owner);
// `FUN_100dc480` 0x100dc480 (`"CBaseEntity::SetSolid"` is its scope-trace label): the solid-type
// setter. Returns early on no change; `FUN_100dda20`; `SOLID_BSP` under a live move parent becomes
// `SOLID_VPHYSICS`; the store; the physics object's slot 25; `FUN_100ddc40`; and `FUN_100dc430` only
// when the "solid" boolean (`m_Solid != 0 && !(flags & 4)`) flipped.
void SetSolid(int32 Type);
// `FUN_100dc580` 0x100dc580 (`"CBaseEntity::SetSolidFlags"` 0x10548fb4 is its scope-trace label):
// the solid-word setter. Store first; nothing else on no change; `FUN_100dda20` on a change of
// `0x180`; the physics object's slot 25 on a change of `0x4`; `FUN_100ddc40` then `FUN_100dc430` on a
// change of `0xc`.
void SetSolidFlags(uint16 Word);
// `CBaseEntity::SetCollisionBounds` 0x1009edc0 (a scope-trace frame around `thunk_FUN_100dc770`) ->
// `FUN_100dc770` 0x100dc770, the bounds setter: `m_vecMins` / `m_vecMaxs` stored, `m_flRadius =
// f32(0.5 * sqrt(f32((dz*dz + dx*dx) + dy*dy)))` with `d = maxs - mins` (the x87 grouping, 0.5 =
// `0x104454d0`), then `FUN_100dda20` (the `0x14000` OR and the edict-gated `0x8000` / dirty-list
// arm). Never writes `m_vecSize`. Source units (`walks/L0-r016.md`). Its callers: the base
// constructor (`1009dca4`, zero box), `UTIL_SetSize` 0x101cf3c0 and the `KeyValue` 0x1009e430
// `mins` / `maxs` arms (`Construct`'s key walk).
void SetCollisionBounds(const FVector& MinsUnits, const FVector& MaxsUnits);
// `UTIL_SetSize` `FUN_101cf3c0` 0x101cf3c0 (`FUN_101cf390` 0x101cf390 is its 3-argument wrapper; the
// fourth argument is never read): per axis `maxs[i] < mins[i]` -> `Error("backwards mins/maxs")`
// (fatal in retail; logged and refused here), then `SetCollisionBounds(mins, maxs)`, then slot 213
// `SetSize(maxs - mins)` (`101cf459`) -- the ONE writer of `m_vecSize` from a box. `UTIL_SetModel`
// 0x101cf4a0 ends in it with the model's bounds (or `vec3_origin` twice for a NULL model), the NPC
// hull setters 0x10273070 / 0x10273180 with the hull row.
void UtilSetSize(const FVector& MinsUnits, const FVector& MaxsUnits);
// `UTIL_SetModel` `FUN_101cf4a0` 0x101cf4a0, the tail of `CBaseEntity::SetModel` 0x100ad460 (slot 105):
// a NULL or empty name returns untouched; else the model index (slot 10) and name (slot 212) are
// re-set and the collision box is the model's bounds -- `UTIL_SetSize(this, mins, maxs, 1)`
// (`101cf54f`), or `UTIL_SetSize(this, vec3_origin, vec3_origin, 1)` (`101cf56a`) when the engine has
// no model for the name. The index is the world's model table (`FElysiumEntityWorld::ModelTableIndex`);
// the model's bounds are the def's hulls (`FElysiumEntityDef::Hulls`, the brush bake, cm on the Unreal
// axes), so a def with none takes the NULL-model arm.
void UtilSetModel(const FString& Name);
// `CBaseEntity::ForceTransmit` 0x1009d1e0: `m_flForceTransmitUntil (+0x90) = gpGlobals->curtime +
// 1.0f` (`_DAT_104454c0`). Hide 0x1009d2a0, ScriptHide 0x100a8710 and ScriptUnhide 0x100a8990 call it.
void ForceTransmit();
// `CBaseEntity::SimulateAngles` 0x1003f810 (not a slot; its callers are `PhysicsNoclip` 0x100397c0,
// `PhysicsToss` 0x1003f920 and `PhysicsStepRunTimestep` 0x1003b190): `T = interval * m_vecAngVelocity +
// GetAngles()` per component, each stored to f32, then slot 64 `SetAngles(T)`.
void SimulateAngles(float Interval);
// `FUN_100b51b0` 0x100b51b0 (`__fastcall`, `SetAngles` 0x100b2d00's second walk): per node, a switch on
// `m_Collision.m_nSurroundType` (+0x28c) through the 7-entry jump table at 0x100b5254 -- 0, 1, 6 test
// (`m_usSolidFlags` bit 0x80 clear and `m_Solid` neither SOLID_BBOX 2 nor SOLID_NONE 0), 2, 4 and > 6
// always, 3 and 5 never call `FUN_100dda20` (`MarkCollisionBoundsDirty`) -- then every move child
// (`m_pMoveChild`, each `m_pMovePeer`, recursing).
void MarkSurroundingBoundsTreeDirty();
// A retail vector as a site payload spells it: `x,y,z`, each as the f32 `%g`.
static FString RetailVectorText(const FVector& V);
// `FUN_100dda20` 0x100dda20: `owner->m_iEFlags |= 0x14000`, then tail-jumps into `FUN_100ddd20`.
// SDK analogue `MarkSurroundingBoundsDirty` (inference).
void MarkCollisionBoundsDirty();
// `FUN_100ddd20` 0x100ddd20: when `IndexOfEdict(owner->+0x2e0)` is non-zero and bit `0x8000` is
// clear, set it and append the owner to the dirty-partition list `DAT_106e8144`. SDK analogue
// `MarkPartitionHandleDirty` (inference). The list's consumer (`FUN_100dbac0`) is engine-replaced.
void MarkPartitionHandleDirty();
// `FUN_100ddc40` 0x100ddc40: remove the handle from every partition list, then re-insert it by the
// solid words (`0x10` always; `1` / `2` / `3` by `FSOLID_NOT_SOLID` / `FSOLID_TRIGGER`). Engine-
// replaced (`SpatialPartition001`): with no handle it exits at its first test, as it does here.
void UpdatePartitionMembership();
// `FUN_100dc430` 0x100dc430: when the entity is not solid (`m_Solid == 0 || flags & 4`) and not a
// trigger (`flags & 8` clear) and `IsCurrentlyTouching()` (slot 207), `SetCheckUntouch(true)` (slot 6).
void CheckForUntouchOnSolidChange();
// `IndexOfEdict(this->+0x2e0)` (`VEngineServer014` slot 35, engine `0x20109110`: 0 for a NULL edict)
// as `FElysiumDecal::EdictIndex` answers it: 0 while the edict is not attached (`bEdictAttached`,
// the whole of the base constructor), 0 for the world, else the entity's index. There are no edicts
// here; every non-world entity is networked, as every `CreateEntityByName` entity is in retail.
int32 EdictIndex() const;
// `CBaseEntity::PhysicsCheckForEntityUntouch` 0x1003d490: expire every touchlink whose stamp is not
// `m_touchStamp` (the other side's EndTouch, the link freed), then `SetCheckUntouch(false)`
// (`1003d5c9`). Run by the touch manager's frame pass `FUN_100f8ec0`.
void PhysicsCheckForEntityUntouch();

// `CBaseEntity::IsSolid()` inverted, as far as this port records it: `m_nSolidType == SOLID_NONE ||
// (flags & FSOLID_NOT_SOLID 0x4)`. A trace or a hull test never reports such an entity. The port's
// NPC `Spawn` bodies do not record their own `SetSolid(SOLID_BBOX)`, so an entity that never
// recorded a `SetSolid` is NOT read as `SOLID_NONE`; one whose last recorded `SetSolid` was
// `SOLID_NONE` is — the makers (`CNPCMaker::Spawn` `0x1034afe0`, story 5 fold A4) — and so is one
// carrying `FSOLID_NOT_SOLID` — the script directors (`CCineNPC::Spawn` `0x101a6f10`, fold A3).
bool IsRetailNotSolid() const
{
	return (RetailSolidFlags & 0x4u) != 0 || (RetailSolidSets > 0 && RetailSolidType == 0);
}

// --- The voice-table words' readers (`Substrate/ElysiumEntityVSound.cpp`, `walks/L0-r007.md`) -----

// `CBaseEntity::GetVSoundTableIdx` 0x1009d5e0: slot 71 below 0, then `+0xbc`.
int32 GetVSoundTableIdx();
// `CBaseEntity::GetVSoundGroup` 0x1009d6a0: slot 71 below -1, then `+0xb4`.
int32 GetVSoundGroup();
// `CBaseEntity::GetVSoundGroupFemale` 0x1009d760: slot 71 below -1, then `+0xb8`.
int32 GetVSoundGroupFemale();
// `thunk_FUN_101f55a0(&DAT_1073dc28, this, group, flag)` -- the group seam on `SndScheme_Char`
// (`FElysiumEntityWorld::VSoundCharRegistry`, the L2 data hook). What slot 71's tail and the species
// writers (`CNPC_VWerewolf::Precache`, `CNPC_VZombie::SetModel`) call.
int32 VSoundGroupIndexFor(const TCHAR* Group, EElysiumVSoundSex Sex);
// Port-only: slot 71 is running on this entity, so a lazy getter it reaches (through the seam's
// `GetVSoundTableIdx`) must not re-dispatch it -- retail's arm 6 recursion, refused once here.
bool bInPrecacheSoundTable = false;

// --- The data-object registry (L0.entity_core.data-object-registry, `walks/L0-r019.md`) ---------
// `m_fDataObjectTypes` (+0x444; the ledger's name, `layout.md:188` -- no class datamap has a row at
// 0x444, so no save block carries it): one bit per data-object type, `1 << (type & 31)`. Written only
// by `AddDataObjectType` and `RemoveDataObjectType`; the constructor's initialiser is UNRECOVERED
// (the `CBaseEntity` ctor is unnamed in the corpus) and 0 is assumed. The blocks the bits stand for
// live in the world's `CDataObjectAccessSystem` (`Substrate/ElysiumDataObjects.h`), keyed by this
// entity. Every decompiled caller passes type 1, the touch-link list head: `PhysicsMarkEntityAsTouched`
// 0x1003dc70 (Get, Create), `PhysicsCheckForEntityUntouch` 0x1003d490 / `PhysicsNotifyOtherOfUntouch`
// 0x1003d640 / `PhysicsRemoveTouchedList` 0x1003d8f0 (Get, Destroy), and slot 207 `IsCurrentlyTouching`
// 0x1003d3d0, which is `HasDataObjectType(this, 1)` -- the seam the touch-lifecycle story's slot body
// reads. Definitions: `Private/Substrate/ElysiumDataObjects.cpp`.
uint32 DataObjectTypes = 0;
void AddDataObjectType(int32 Type);          // 0x1003cbc0: `mask |= 1 << (type & 31)`, no checks
bool HasDataObjectType(int32 Type) const;    // 0x1003cb00: `(mask & (1 << (type & 31))) != 0`
void RemoveDataObjectType(int32 Type);       // 0x1003cc80: `mask &= ~(1 << (type & 31))`
// 0x1003cd50: bit gate, signed range 0..31, registered accessor, then the accessor's get; else 0.
void* GetDataObject(int32 Type);
// 0x1003cf50: `AddDataObjectType` FIRST (for every type, valid or not), then range, accessor, create; else 0.
void* CreateDataObject(int32 Type);
// 0x1003d130: bit gate (no clear when absent); range and accessor skips; the accessor's destroy with the
// bit still set; then `RemoveDataObjectType` ALWAYS, after the dispatch or after either skip.
void DestroyDataObject(int32 Type);
