// Story 29d, family **Sounds10** — the declarations of this family's layer 10–18 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual and this family only defines it. What lands here is
// the non-slot half — the shared helpers, the two packets and the seams the slot bodies go through.
//
// The definitions are in `Substrate/ElysiumNpcKernelSounds10.cpp` and the tests in
// `Tests/ElysiumNpcKernelSounds10Tests.cpp`. The walked prose is `docs/vtmb/npc-ai/senses.md`
// § "Story 29d, family Sounds10 — …".
//
// This family is **the seventeen sound hooks of slots 488–507, the three `KeyValue` overloads
// (slots 108/109/110) with the two `CBaseEntity` formatters behind them, slot 185 `FireBullets`,
// and the two `CNPC_VWerewolf` / one `CNPC_Crow` species arms over them**. It is NOT family
// **Sounds** (story 29c-1), which owns the layer 0–9 gates in front of these hooks — slot 486
// `FOkToMakeSound`, slot 487 `JustMadeSound`, slots 509/510 and the per-species vocalization table
// — and whose `ElysiumNpcKernelSounds.cpp` still carries the two slot definitions (488 and 506)
// that 29c-1 moved out of the generated file so their species prologue had somewhere to live.
// Those two definitions now call `TroikaDeathSound()` / `TroikaSlot506()` below, which is this
// family's body for them.
//
// THREE STANDING FACTS OF THIS FAMILY, stated once here rather than at nineteen call sites.
//
//   * **Every sound hook is the same two statements.** A lazily-cached lookup of a CONCEPT NAME in
//     the global VSound concept list (`DAT_1073dc40`, count `DAT_1073dc3c`), then one call to the
//     VSound play entry (`0x101f5950`) on the table object `DAT_1073dc28` with that id, channel 2,
//     volume 1.0 and a fifth argument that is `1.25` on sixteen of the seventeen. The DIFFERENCES
//     are the concept name, the gate in front (two hooks have one), and slot 507's computed fifth
//     argument plus its state write — and those are what the bodies below spell out one by one.
//   * **The concept names carry UNDERSCORES.** Read out of the pinned image at `0x105d8c30`…:
//     `Death`, `Target_Suspect`, `Idle_Calm`, `Pain`, `Fear_Start`, `Target_Lost`,
//     `Target_Reacquired`, `Surprised`, `Target_Acquired`, `Flee`, `Idle_Agitated`, `Riled`,
//     `Comfort`, `Upset`, `Target_GiveUp`, `Float`, and `Exert_Heavy` / `Exert_Light` at
//     `0x1057a1a0` / `0x1057a1b0`. The checklist's one-line walks spelled ten of them with spaces;
//     the bytes are what these constants carry.
//   * **`+0x0098` is `m_pBaseNPCTroika` and `+0x00a8` is `m_pPlayer`.** They are not "the firing
//     NPC" and "a player record" — the shape map (`ElysiumNpcKernelShape.cpp`) names both. Every
//     entity that reaches `FElysiumNpc::FireBullets` therefore answers non-null for the first and
//     null for the second, which decides three of that body's arms outright.

// --- Slots 108/109/110 `KeyValue`: the two words the Troika line's own arms write ---------------
//
// `CBaseToggle`'s pair, flattened onto `CAI_BaseNPCTroika` by the shape map at `+0x04fc` and
// `+0x0504`. Slot 110 (`0x101c1480`) is their only writer in the whole kernel closure, and nothing
// in this runtime reads them yet — the motor's move distance and a door's lip are two other
// subsystems' words that happen to live on this leaf because retail's NPC derives from
// `CBaseToggle`. They are carried so the two `atof` arms have somewhere real to land.
float MoveDistance = 0.f;   // +0x04fc m_flMoveDistance, keyfield `distance`
float Lip = 0.f;            // +0x0504 m_flLip, keyfield `lip`

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

// --- The VSound concept hooks (slots 488–507) --------------------------------------------------

/** One `0x101f5950` request, recorded because this runtime cannot answer it. Retail's body is
 *  `CBaseEntity::GetVSoundTableIdx(entity)` into the table array at `this+0x1c`/`+0x20`, then
 *  `0x101f4600` picks one wav out of that table's group for the concept (`"%s/%s.wav"` or
 *  `"%s/%s_%d.wav"` with `RandomInt(1, N)`), then `CPASAttenuationFilter(GetSoundEmissionOrigin(),
 *  0.8)` and `EmitSound(edict, channel, wav, volume, <fifth>, 0, 100, 0, 0, 1, 0)`. */
struct FVSoundSpeak
{
	// The concept name the body asked for, verbatim.
	const TCHAR* Concept = nullptr;
	// What `VSoundConceptId` answered. `-1` is retail's own "no such concept" id, which retail
	// then passes to the play entry unchanged.
	int32 ConceptId = INDEX_NONE;
	// Retail's third argument. `2` (`CHAN_VOICE`) on every hook in this family.
	int32 Channel = 0;
	// Retail's fourth argument. `1.0` on every hook in this family.
	float Volume = 0.f;
	// Retail's fifth argument, `EmitSound`'s attenuation slot: `1.25` on sixteen of the seventeen
	// hooks, `0.0` on both `CNPC_VWerewolf` arms, and slot 507's computed `1.0` / `0.0`.
	float Attenuation = 0.f;
};
TArray<FVSoundSpeak> VSoundSpeakCalls;

/** SEAM: the concept-id lookup every hook opens with — `__strcmpi` down the global VSound concept
 *  list (`DAT_1073dc40`, count `DAT_1073dc3c`, each entry `{ int id; const char* name; }`, a null
 *  name read as the empty string), answering the matching entry's FIRST WORD and `0xffffffff` when
 *  nothing matches.
 *
 *  Retail caches the answer behind a per-hook once-flag byte, which makes the walk a
 *  process-lifetime magic static. This port re-resolves on every call, exactly as family **Sounds**
 *  chose for `Float_Sound_Info`'s identical guard: with a pure lookup the cache is unobservable and
 *  a process-lifetime cache is one more thing a map reload cannot invalidate.
 *
 *  **This runtime loads no VSound concept list at all** — nothing parses one, `PrecacheSoundTable`
 *  (slot 71) is still a generated stub and `m_iVSoundTableIdx` (`+0x00bc`) is never written. So the
 *  list is empty, which is retail's own count-zero case, and the answer is retail's own miss: `-1`.
 *  Named rather than inlined so the day a concept list is parsed every hook answers at once. */
static int32 VSoundConceptId(const TCHAR* ConceptName);

/** SEAM: the VSound play entry `0x101f5950`. With no table this cannot resolve a wav, and retail's
 *  own out-of-bounds arm (`"ERROR: VSnd: Play: %s Table out of bounds: %d\n"`, taken whenever
 *  `GetVSoundTableIdx(entity) >= this->m_nTables`) is the arm an entity with no table index takes —
 *  so this refuses exactly where retail refuses. The request is recorded, because the concept, the
 *  channel, the volume and the fifth argument ARE the recovered half. */
void SpeakVSound(const TCHAR* ConceptName, int32 ConceptId, int32 Channel, float Volume,
	float Attenuation);

/** The two statements sixteen of the seventeen hooks are, and the tail of the seventeenth:
 *  `SpeakVSound(name, VSoundConceptId(name), CHAN_VOICE, 1.0, Attenuation)`. Every hook below calls
 *  it explicitly rather than sharing one dispatcher, so each body's own arms stay readable. */
void SpeakSoundConcept(const TCHAR* ConceptName, float Attenuation);

/** `CAI_BaseNPCTroika::FUN_10293ec0` (`0x10293ec0`), slot 488's Troika-line body. Defined here and
 *  called from `FElysiumNpc::DeathSound` in family **Sounds**' file, which carries the slot's
 *  species prologue (`SpeciesDeathSound`, `CNPC_VTzimisce` `0x103b92a0`) that story 29c-1 put
 *  there. */
void TroikaDeathSound();

/** `CAI_BaseNPCTroika::FUN_10294e70` (`0x10294e70`), slot 506's Troika-line body, for the same
 *  reason — `FElysiumNpc::Slot506` in family **Sounds**' file carries `CNPC_VCamera`'s empty
 *  override in front of it. */
void TroikaSlot506();

/** Slot 507's fifth argument, computed (`10294f9f`..`10294fdc`) rather than a constant:
 *
 *      t   = m_iDialog ? 0xe : 0
 *      if (t + 0x42 <= 0x32)  ->  the double at 0x10449148 (4.0)
 *      else                   ->  (float)(0x14 / (t + 0x10))     — an INTEGER divide
 *
 *  `t + 0x42` is `0x42` or `0x50`, both above `0x32`, so the first arm is **unreachable** and the
 *  answer is `20/16 = 1` → `1.0f` with no dialogue name and `20/30 = 0` → `0.0f` with one. */
static float FloatSoundAttenuation(bool bHasDialogName);

/** Slot 507's re-arm term: `Float_Sound_Info` **row 1** (`FloatSoundMinDelay`, 5.0 s), truncated to
 *  an int by retail's `__ftol` and added to the clock with `FIADD` — an INTEGER add
 *  (`1029505f`). The checklist's walk did not name the row; `1029502c` pushes `1`. The table is
 *  `Clamping`, so the row index is clamped rather than missed. */
int32 FloatSoundMinDelaySeconds() const;

// --- `CNPC_Crow`'s slot 511 arm ----------------------------------------------------------------

/** SEAM: `CBaseEntity::StopSound(const char*)` (`0x101b0d80`) — stop the named SOUNDSCRIPT this
 *  entity is playing. `IElysiumAudio::StopEntitySounds` is "stop everything this entity is
 *  playing" and cannot express "stop this one script"; nothing in this substrate indexes a live
 *  voice by script name. Records the request and answers nothing, which is retail's answer for a
 *  script that is not playing. */
TArray<FString> StopNamedSoundCalls;
void StopNamedSound(const TCHAR* SoundScript);

// --- Slot 185 `FireBullets` --------------------------------------------------------------------

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

/** `+0x0098 m_pBaseNPCTroika` — retail's "this entity is an NPC" self-pointer, whose `+0x65f0`
 *  `m_iFakeReloadCount` `FireBullets` decrements. Every entity on this leaf IS an NPC, so this is
 *  true and the decrement always happens. Named rather than inlined because it is the arm. */
bool FireBulletsShooterIsNpc() const;

/** `+0x00a8 m_pPlayer` — retail's "this entity is the player" self-pointer. Never set on this leaf,
 *  which is what makes `FireBullets`' spread gate read "spread unless the `0x2000000` bit is set",
 *  its tracer gate "flag bit 0, or skill above 2", and its `0x10160560` tail unreachable. */
bool FireBulletsShooterIsPlayer() const;

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

/** `VectorVectors(forward, right, up)` (`0x10138a90`) — Source's basis-from-a-forward, with
 *  retail's degenerate arm reproduced: a forward whose x AND y are zero answers `right = (1,0,0)`
 *  and `up = (0, -forward.z, 0)`, which is NOT normalized and NOT orthogonal to a vertical
 *  forward. Otherwise `right = normalize(forward.y, -forward.x, 0)` and
 *  `up = normalize(cross(right, forward))`. */
static void VectorVectors(const FVector& Forward, FVector& OutRight, FVector& OutUp);

/** `0x10268170` — the unscaled spread offset one bullet adds to the forward. A rejection sample:
 *  two independent sums of two `RandomFloat(-0.5, 0.5)` draws, redrawn while `x*x + y*y > 1.0`,
 *  then `right * (spread.x * x) + up * (spread.y * y)`. A PLAYER shooter replaces BOTH spread
 *  components with `ScaleField_0x1ddc()` (`0x10160680`); this leaf is never the player, so the
 *  info's own pair is what is used and the substitution is named rather than run. */
FVector BulletSpreadOffset(const FVector& SpreadUnits, const FVector& RightAxis,
	const FVector& UpAxis);

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
TArray<FFireBulletsTrace> FireBulletsTraces;
void FireBulletsTracePass(FElysiumFireBulletsInfo& Info);

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
TArray<FBulletTracerCall> BulletTracerCalls;
void EmitBulletTracer(const FElysiumFireBulletsInfo& Info, float Fraction);

/** SEAM: `RangedDamagePerVictim` (`0x10268330`) — the once-per-victim-per-repeat damage call whose
 *  fraction is `hits / Bullets`. Not a row of this family and not a vtable slot; recorded with the
 *  victim and the fraction, which is what `FireBullets` decides. */
struct FRangedDamagePerVictimCall
{
	FElysiumEntityHandle Victim;
	float Fraction = 0.f;
};
TArray<FRangedDamagePerVictimCall> RangedDamagePerVictimCalls;
void RangedDamagePerVictim(const FElysiumEntityHandle& Victim,
	const FElysiumFireBulletsInfo& Info, float Fraction);
