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

/** `+0x8c` — the handle of whoever last began a `+use` on this entity, written by slot 39 and
 *  cleared to `-1` by slot 42 and by a null activator. Not a datamap field and not in
 *  `ElysiumNpcKernelShapeMap.cpp` (which covers the `CAI_BaseNPC` band only); its retail name is
 *  **unrecovered**. It has no reader in this family's closure — the two slots are its only
 *  toucher — so it is carried as the recorded write rather than wired to a consumer. */
FElysiumEntityHandle UseActivator;   // +0x8c

/** `+0x038c m_vecSize` and the two words after it — the box `CBaseEntity::SetSize` (`0x100b1890`,
 *  slot 213) writes. A `CBaseEntity` word, below 29b's band, and written by this one body and read
 *  by `GetSize` (slot 214) alone in layers 0–9. CENTIMETRES, like every other length on this
 *  struct; Unreal's collision component is the eventual host and this member is what the kernel
 *  sees until then. */
FVector SizeCm = FVector::ZeroVector;

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

/** `IPhysics`'s "is this vphysics object static/asleep" query — `(*DAT_1070b250 + 0x18)(index)`,
 *  the second arm of `0x100b5110`. **SEAM**: answers false, so a `SOLID_VPHYSICS` entity is not
 *  standable, which is retail's answer for a moving one. */
bool PhysicsObjectIsStandable(const FElysiumEntity& Entity) const;

/** `FUN_100b5110` (`0x100b5110`) — the shared standability helper slots 159 and 164 both end in.
 *  `GetSolid() == 1` (`SOLID_BSP`) is standable outright; `SOLID_VPHYSICS` (6) asks the physics
 *  object; everything else is not. NAMED from what it does, not from a recovered symbol. */
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

// SEAM for `m_Collision` (`+0x270`, `CCollisionProperty`) and its solid-flag word `+0x2b4` — the
// `CBaseEntity` words `SetSolid` / `SetSolidFlags` / `AddSolidFlags` write, each under a
// `"CBaseEntity::SetSolid"`-style scope-trace frame. This substrate carries no solid type and no
// solid flags, so these three record what was asked:
// `RetailSolidType` the last `SetSolid` value, `RetailSolidFlags` the flag word as the
// read-OR-pass-back writers leave it, `RetailSolidSets` how many `SetSolid` calls ran. Writers:
// `CAI_TestHull::Spawn` `0x102d72f0`, `CNPC_VPedestrian::OnRestore` `0x103a25a0`, `CNPCMaker::Spawn`
// `0x1034afe0`. Independent of `RetailMoveType` above: retail's `SetMoveType` touches neither.
int32 RetailSolidType = 0;
uint32 RetailSolidFlags = 0;
int32 RetailSolidSets = 0;

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
