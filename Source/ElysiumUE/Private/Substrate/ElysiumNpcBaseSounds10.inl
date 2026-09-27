// `CAI_BaseNPC`'s declarations of the `Sounds10` family (story 5 step 5),
// moved from `ElysiumNpcSounds10*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseSounds10.cpp`.

/** `CAISound::FUN_1009eca0` (`0x1009eca0`), slot 108 on the `CAISound` / `CAI_Hint` /
 *  `CAI_InterestingPlace` / `CAI_StandoffGoal` line and the body slot 108 on the NPC line
 *  (`0x1004fbb0`) forwards to: `Q_snprintf(buf, 256, "%f %f %f", v.x, v.y, v.z)` under a
 *  `CBaseEntity::KeyValue` scope-trace frame. Answers the formatted buffer; the dispatch of slot
 *  110 with it is the caller's half. */
static FString FormatKeyValueVector(const FVector& Value);

/** `CAISound::FUN_1009ebb0` (`0x1009ebb0`), slot 109 on the same line and the body `0x1004fbf0`
 *  forwards to: the same frame with the format `"%f"` at `DAT_10554f28`. */
static FString FormatKeyValueFloat(float Value);

/** `CBaseEntity::KeyValue(const char*, const char*)` (`0x1009e430`) — the tail slot 110's third arm
 *  returns verbatim. Ten arms, of which this runtime owns one: family **Lifecycle** already
 *  recovered the classification (`ClassifyKeyValue`, which performs retail's `#` truncation FIRST)
 *  and recorded that the `DataMap` arm — retail's `GetDataDescMap` chain walk offering the key to
 *  each level's `ParseKeyvalue` — IS this runtime's own class-chain field table. That arm is run
 *  here. The other nine (`rendercolor`, `renderamt`, `disableshadows`, `disablereceiveshadows`,
 *  `mins`, `maxs`, `angle`, `angles`, `origin`) are `CBaseEntity`'s own story and are NOT this
 *  kernel's row; each answers **true** here, which is retail's answer for a key it matched, so a
 *  matched key is never reported to the caller as unhandled. */
bool BaseEntityKeyValue(const TCHAR* Key, const TCHAR* Value);

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

//
// `CBaseToggle`'s pair, flattened onto `CAI_BaseNPCTroika` by the shape map at `+0x04fc` and
// `+0x0504`. Slot 110 (`0x101c1480`) is their only writer in the whole kernel closure, and nothing
// in this runtime reads them yet — the motor's move distance and a door's lip are two other
// subsystems' words that happen to live on this leaf because retail's NPC derives from
// `CBaseToggle`. They are carried so the two `atof` arms have somewhere real to land.
float MoveDistance = 0.f;   // +0x04fc m_flMoveDistance, keyfield `distance`

float Lip = 0.f;            // +0x0504 m_flLip, keyfield `lip`

/** SEAM: `GetAmmoDef()->Flags(index)` — the ammo-def singleton (`DAT_1070ba0c` vtable `+0xdc`) then
 *  `0x104276a0`, whose whole body is `(0 < i && i < m_nAmmoIndex) ? m_AmmoType[i].nFlags : 0`.
 *  Family **Damage** already recorded that this runtime keys ammo by the authored TYPE NAME and
 *  that the `CAmmoDef` index order is **unrecovered**; with no table every index is out of range,
 *  so this answers retail's own out-of-range answer, `0`. */
int32 AmmoDefFlags(int32 AmmoTypeIndex) const;

/** `DAT_1072cb48`, the file-static filter word `FireBullets` writes before it traces
 *  (`flags | 0x1000`) and every trace in the pass reads. A global in retail, carried per NPC here
 *  because nothing else in this runtime reads it and a shared cell with one writer is the same
 *  observation. The two trace filters pushed beside it (`0x101c2c60`, `0x101c2c30`) have no
 *  counterpart: this runtime's traces take a channel, not a filter stack. */
int32 FireBulletsFilterWord = 0;

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

TArray<FFireBulletsTrace> FireBulletsTraces;

void FireBulletsTracePass(FElysiumFireBulletsInfo& Info);

TArray<FBulletTracerCall> BulletTracerCalls;

void EmitBulletTracer(const FElysiumFireBulletsInfo& Info, float Fraction);

TArray<FRangedDamagePerVictimCall> RangedDamagePerVictimCalls;

void RangedDamagePerVictim(const FElysiumEntityHandle& Victim,
	const FElysiumFireBulletsInfo& Info, float Fraction);

/** `VectorVectors(forward, right, up)` (`0x10138a90`) — Source's basis-from-a-forward, with
 *  retail's degenerate arm reproduced: a forward whose x AND y are zero answers `right = (1,0,0)`
 *  and `up = (0, -forward.z, 0)`, which is NOT normalized and NOT orthogonal to a vertical
 *  forward. Otherwise `right = normalize(forward.y, -forward.x, 0)` and
 *  `up = normalize(cross(right, forward))`. */
static void VectorVectors(const FVector& Forward, FVector& OutRight, FVector& OutUp);
