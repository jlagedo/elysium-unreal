// Story 29c-1, family **Geometry** — the declarations of this family's layer 0–9 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual, and this family only defines it. What lands here is
// the non-slot half — the retail helpers, free functions and non-virtual methods of layers 0–9
// whose overlay row names a port method on `FElysiumNpc` that did not exist before this story.
//
// The definitions are in `Substrate/ElysiumNpcKernelGeometry.cpp` and the tests in
// `Tests/ElysiumNpcKernelGeometryTests.cpp`. One file per family rather than 915 declarations
// appended to an already-oversized header: the family boundary is what this story ports by.
//
// The family is **where a body sits and how big it is**: the six eye/anchor points (slots 193, 194,
// 195, 197, 533 and the `CNPC_Crow` override of 192), the hull-bit query (slot 337), `SetSize`
// (slot 213), and the four bodies that push one body out of another —
// `CAI_BaseNPCTroika::ResolveStandingOnHead`, `CNPC_VAsianVampire::StandingOnPlayer`,
// `CNPC_VWerewolf::UpdateFakeHull` and `CNPC_VMingXiao`'s two severed-tentacle scatter notices.
//
// FOUR STANDING FACTS OF THIS FAMILY, stated once here rather than at thirty call sites.
//
//   * **Distances are SOURCE UNITS in retail and CENTIMETRES here.** `ElysiumMove::U` bridges them
//     at the point of use so the recovered number stays visible; a ratio (0.25, 0.5, 1.25, 0.707,
//     0.25 dot) is dimensionless and is written bare.
//   * **Every `.rdata` cell this family reads was read out of the pinned image** (base
//     `0x10000000`, `.rdata` VA `0x10445000` at file offset `0x445000`), the method families
//     Species and Senses used. The ones that were only ever named in the decompiled C are listed at
//     the head of `ElysiumNpcKernelGeometry.cpp`, one line each.
//   * **`DAT_1070d1b0` is `vec3_origin`.** It has 371 readers and exactly one writer, the static
//     initialiser `0x101370b0`, and it lives past `.data`'s raw size, so it is the image's shared
//     zero vector. `CNPC_VWerewolf::UpdateFakeHull` reads it three times and each read means a
//     different thing (a transform input, a cache reset, a cache-is-empty test); the walked
//     paragraph says which.
//   * **THE DECOMPILER DROPPED THREE `OR AH` INSTRUCTIONS.** The decompiled C for
//     `CNPC_VTzimisce`/`VTzimisceHeadClaw`/`VTzimisceRunner`'s slot 337 shows a bare forward to the
//     Troika body with "no species bit added" — the LISTING (`vtmb_asm`) shows `OR AH,0x4`,
//     `OR AH,0x8` and `OR AH,0x20`, i.e. `| 0x400`, `| 0x800` and `| 0x2000`. The table below
//     carries the listing's answer.

// --- Words this family's bodies touch that 29b did not declare -----------------------------------
//
// 29b declared every word of the flattened `CAI_BaseNPCTroika` layout, which is the `+0x1a40`
// upward band. These five live outside it — three are `CBaseEntity` words below the band and two
// are species words above `+0x665c`, where one leaf carries every classname and the same offset
// means different things per species. Each is named for the class it belongs to, exactly as
// families Bosses, Damage and Motor record theirs.

/** `+0x038c m_vecSize` and the two words after it — the box `CBaseEntity::SetSize` (`0x100b1890`,
 *  slot 213) writes. A `CBaseEntity` word, below 29b's band, and written by this one body and read
 *  by `GetSize` (slot 214) alone in layers 0–9. CENTIMETRES, like every other length on this
 *  struct; Unreal's collision component is the eventual host and this member is what the kernel
 *  sees until then. */
FVector SizeCm = FVector::ZeroVector;

/** `+0x66dc`/`+0x66e0`/`+0x66e4` — `CNPC_VWerewolf`'s cached fake-hull point, the world position of
 *  the `Bip01` bone as of the previous `UpdateFakeHull`. The retail NAME is **unrecovered**: the
 *  word is not in `CNPC_VWerewolf`'s datamap and no corpus body declares it. SOURCE units, because
 *  the whole body is. */
FVector WerewolfFakeHullPosUnits = FVector::ZeroVector;

/** `+0x66f4` — the stamp `UpdateFakeHull` writes after it pushes damage, so the push repeats at
 *  most once a second. Also not in the datamap; the retail NAME is **unrecovered**. An absolute
 *  curtime stamp, carried as double like every other stamp on this struct. */
double WerewolfFakeHullPushTime = 0.0;

/** `+0x668c CNPC_VMingXiaoTentacle::m_vecScatterCenter` — the point a severed tentacle is told to
 *  scatter away from. Family **Squad** owns `+0x668c` as `CNPC_VMingXiao::m_rhProxies[6]` and
 *  family **Bosses** owns it as `CNPC_VManBat::m_hPickupTarget`; this is a THIRD species' word at
 *  the same offset, so it stands beside them rather than replacing either. SOURCE units. */
FVector TentacleScatterCenterUnits = FVector::ZeroVector;

/** `+0x665c CNPC_VMingXiaoTentacle::m_hMingXiao` — the owner `0x1039ede0` resolves before it can
 *  notify the owner's other severed tentacles. Family Bosses owns `+0x665c` as
 *  `CNPC_VBaseBoss::m_BlacklistedEntities`; same situation as `m_vecScatterCenter` above. */
FElysiumEntityHandle TentacleMingXiao;

/** `+0x6670 CNPC_VMingXiaoTentacle::m_ePhase` — the phase word `0x103998d0` requires to read 2
 *  before it will scatter a tentacle. What the OTHER phase values mean is **unrecovered**; only the
 *  2 is a fact of this family's rows. */
int32 TentaclePhase = 0;

// --- Slot 193 `EyePosition`, and its two species overrides ---------------------------------------

/** `CBaseEntity::EyePosition` (`0x100b4b40`, slot 193) with the family's two species overrides in
 *  front of it. The Troika-line body is `GetAbsOrigin() + m_vecViewOffset`, which this chain
 *  already answers through `FElysiumCombatCharacter::EyePosition()`; what lands here is the
 *  dispatch and the two overrides:
 *
 *    * `CPayphone::vfunc193` (`0x101aae60`) looks up the bone `"Phone_bone_01"` and answers its
 *      world position, falling back to the base body when `LookupBone` answers -1.
 *    * `CAI_BaseHumanoid::vfunc193` (`0x1025e8e0`) refreshes a lazily-invalidated cache
 *      (`0x1025e7b0`) and answers the cached vector at `+0x5f50`/`+0x5f54`/`+0x5f58`.
 *
 *  CENTIMETRES, because every caller of `EyePosition()` in this runtime is. */
virtual FVector EyePosition() const override;

/** One row of slot 193's species table: the census class and the retail body that fills 193 for it,
 *  checkable against `docs/vtmb/npc-kernel/slots.md`. */
struct FEyePositionSpecies
{
	const TCHAR* RetailClass = nullptr;
	const TCHAR* Body = nullptr;
};
static const FEyePositionSpecies* EyePositionSpeciesRows(int32& OutCount);
static const FEyePositionSpecies* EyePositionSpeciesOf(const TCHAR* InRetailClass);

/** SEAM for `CBaseAnimating::LookupBone(name)` + `CBaseAnimating::GetBonePosition02(bone, &pos,
 *  &ang)` — the pair `CPayphone::vfunc193` runs — and, with the local offset already folded in, for
 *  `UpdateFakeHull`'s `GetBoneTransform(bone, m)` plus the two `VectorTransform` calls that turn
 *  `vec3_origin` into the `Bip01` bone's world point. This substrate's animating tier exposes no
 *  bone table to the kernel; answers false and leaves `OutPositionCm` untouched, which is retail's
 *  own `LookupBone == -1` arm for the payphone and, for the werewolf, the arm that leaves the fake
 *  hull where it was. `BoneWorldPositionCalls` is what a test reads to prove the seam was asked. */
bool BoneWorldPosition(const TCHAR* BoneName, FVector& OutPositionCm) const;
mutable int32 BoneWorldPositionCalls = 0;

/** SEAM for `0x1025e7b0`, the cache refresh `CAI_BaseHumanoid::vfunc193` runs in front of its read:
 *  `GetAttachment01(DAT_105c8ed8, &m_vecHumanoidEye, &ang)` — a NAMED attachment whose string the
 *  decompiler folded away and which is therefore **unrecovered** — with `CBaseEntity::EyePosition()`
 *  plus `GetAngles()` as the attachment-missing fallback, then a second lazy half that caches the
 *  vector from the eye to slot 278 (`+0x458`). Answers false; the humanoid arm then answers the
 *  fallback the retail body itself answers when the attachment is missing, which is the base
 *  `EyePosition()`. `+0x5f50..+0x5f58` has no shape-map row — it is a `CAI_BaseHumanoid` word and
 *  29b's band is `CAI_BaseNPC`'s — so nothing is cached here either. */
bool HumanoidEyeCache(FVector& OutPositionCm) const;

// --- Slot 194 / 195, the two angle aliases -------------------------------------------------------
//
// `0x100b4bc0` is `MOV EAX,[ECX]; JMP [EAX+0x36c]` and `0x100b4be0` is the same with `+0x374`:
// eight bytes each, a tail call and nothing else. 82 and 80 classes respectively fill the slot and
// every one of them with that body. The definitions forward to slots 219 and 221 verbatim.

// --- Slot 197 `BodyTarget` -----------------------------------------------------------------------

/** `CAI_BaseNPC::FUN_102789c0`'s blend, lifted out of the body so the formula can be measured
 *  without a world and so the two random draws are the caller's. Every argument is CENTIMETRES and
 *  the answer is too.
 *
 *  `AnchorCm` is retail's `E`: `WorldSpaceCenter() - 0.25 * (WorldSpaceCenter() - GetAbsOrigin())`,
 *  a point a quarter of the way back down from the bounds centre toward the feet. `EyeCm` is slot
 *  193. `Noise1`/`Noise2` are the two independent `RandomFloat(0, 0.5)` draws the noisy arm makes,
 *  and are ignored by the other two arms. */
static FVector BodyTargetBlend(const FVector& AnchorCm, const FVector& EyeCm, bool bNoisy,
	bool bAimAtEyeExactly, float Noise1, float Noise2);

/** `E` itself, the anchor both non-noisy arms interpolate from. Exposed because the 0.25 is the
 *  half of this body a reader is most likely to get wrong: the delta is measured from
 *  `WorldSpaceCenter()` to `GetAbsOrigin()` and then subtracted from `WorldSpaceCenter()` AGAIN,
 *  through a SECOND slot-192 dispatch, which is why the body calls slot 192 twice. */
static FVector BodyTargetAnchor(const FVector& CentreCm, const FVector& OriginCm);

// --- Slot 192 `WorldSpaceCenter`, `CNPC_Crow`'s override -----------------------------------------

/** `CNPC_Crow::vfunc192` (`0x10357760`) — three chained dispatches of slot 220 `GetOrigin()`, whose
 *  X is taken from the second call, Y from the first and Z from the third plus `_DAT_1046bac0`
 *  (6.0 Source units). All three answer the same vector, so the whole of it is
 *  `GetOrigin() + (0, 0, 6)`.
 *
 *  **This is NOT the slot definition.** Slot 192's Troika-line body (`0x10027160`) is another
 *  story's row and the generator already emits `FElysiumNpc::WorldSpaceCenter()` in
 *  `ElysiumNpcKernelSlots.cpp`, so defining it here would be a duplicate symbol. The Crow override
 *  lands as a named method that walks the census chain itself and answers the base value for every
 *  other class. */
FVector SpeciesWorldSpaceCenter() const;

// --- Slot 213 `SetSize` --------------------------------------------------------------------------
//
// `0x100b1890` is a scope-trace push, three stores into `m_vecSize` and a scope-trace pop. The
// scope trace is retail's crash-report breadcrumb stack and has no observable effect; the three
// stores are the body.

// --- Slot 337 `GetUsedHullBits` ------------------------------------------------------------------

/** One row of slot 337's species table. `bReplaces` is the difference between the two shapes retail
 *  uses: `CAI_BaseNPC`/`CAI_BaseNPCTroika` and the Tzimisce/Rat/MingXiao line call the base body
 *  and OR a bit onto its answer, while seven species answer a bare constant and never call up.
 *  `Body` is the retail address, so a row can be checked against `npc-kernel/slots.md`. */
struct FUsedHullBitsSpecies
{
	const TCHAR* RetailClass = nullptr;
	const TCHAR* Body = nullptr;
	int32 Bits = 0;
	bool bReplaces = false;
};
static const FUsedHullBitsSpecies* UsedHullBitsSpeciesRows(int32& OutCount);
static const FUsedHullBitsSpecies* UsedHullBitsSpeciesOf(const TCHAR* InRetailClass);

/** `CBaseCombatCharacter::GetUsedHullBits` (`0x10341710`), the bottom of the chain: a scope-trace
 *  pair and `return 1`. `CAI_BaseNPC` (`0x10270820`) ORs `0x1` onto it and `CAI_BaseNPCTroika`
 *  (`0x1029a050`) ORs `0x1` onto THAT, so the Troika line's answer is 1 and the two ORs are a
 *  no-op over the base — a recovered fact, not a transcription slip. */
static constexpr int32 BaseCombatCharacterHullBits = 1;

// --- Slot 533 `EyeOffset` ------------------------------------------------------------------------

/** The six hint-schedule activities `CAI_BaseNPCTroika::FUN_102b4ab0` special-cases, in retail's
 *  `switch` order, `nullptr`-terminated by a count. Every other activity falls through to
 *  `CAI_BaseNPC::FUN_10274db0`. */
static const int32* HintEyeOffsetActivities(int32& OutCount);

/** `CAI_BaseNPC::FUN_10274db0`, the Troika line's base eye offset: with debug bit `0x8000000` set
 *  on slot 513 (`+0x804`) AND the activity `0x57` or `8`, a fixed `(0, 0, 1.5)` override; otherwise
 *  `m_vDefaultEyeOffset` (`+0x5d60`). CENTIMETRES. */
FVector BaseEyeOffset(int32 Activity) const;

/** SEAM for slot 513 (vtable `+0x804`), the debug-overlay bit field `FUN_10274db0` tests `0x8000000`
 *  against. This runtime stands no per-NPC overlay word; answers 0, so the override arm never
 *  fires and every activity answers `m_vDefaultEyeOffset` — which is retail's own answer with the
 *  overlay off, and the shipped default. */
uint32 DebugOverlayBits() const;

/** `m_vDefaultEyeOffset` (`+0x5d60`). The shape map binds that word to `FElysiumEntity` with
 *  "no stored view offset; the eye point is the chain's virtual `EyePosition()`", so this answers
 *  `EyePosition() - Origin` rather than standing a second copy of the same fact. CENTIMETRES. */
FVector DefaultEyeOffsetCm() const;

// --- `CAI_BaseNPCTroika::ResolveStandingOnHead` (`0x102bf820`) -----------------------------------

/** The pure rule, so the spring can be measured without a ground entity and without the RNG: given
 *  where I am and where the thing under me is, which way do I go and how far this interval.
 *
 *  `DiagonalRoll` is retail's `RandomInt(0, 3)`, consumed ONLY when the two bodies are exactly
 *  co-located in XY; `JitterX`/`JitterY` are its two `RandomFloat(-0.1, 0.1)` draws, consumed only
 *  when they are not. `PreviousTimerSeconds` is `m_flStandingOnHeadTimer` (`+0x65fc`) on the way
 *  in. Positions and the answer are CENTIMETRES. */
struct FStandingOnHeadStep
{
	FVector Direction = FVector::ZeroVector;   // unit, Z always exactly 0
	float TimerSeconds = 0.f;                  // the ramp after `min(prev + interval, 5)`
	FVector DeltaCm = FVector::ZeroVector;     // `Direction * Timer * 40 units * Interval`
	FVector StartCm = FVector::ZeroVector;     // my origin lifted 0.1 units on Z
};
static FStandingOnHeadStep StandingOnHeadStep(const FVector& MyOriginCm,
	const FVector& GroundOriginCm, float PreviousTimerSeconds, float IntervalSeconds,
	int32 DiagonalRoll, float JitterX, float JitterY);

/** The four diagonals `RandomInt(0, 3)` picks between when the two origins share an XY position,
 *  in retail's `DEC EAX` order: roll 0 `(+0.707, +0.707)`, 1 `(-0.707, +0.707)`, 2
 *  `(+0.707, -0.707)`, 3 `(-0.707, -0.707)`. `_DAT_1049aea8` is `+0.707` and `_DAT_1049aea4` is
 *  `-0.707`, both read out of `.rdata`. */
static FVector StandingOnHeadDiagonal(int32 DiagonalRoll);

/** `0x102bf820` itself. `IntervalSeconds` is retail's one stack argument, a float the decompiler
 *  lost to `fStack_4`/`[ESP+0xac]`. Writes `m_flStandingOnHeadTimer` and, when the hull trace is
 *  clear, the origin. */
void ResolveStandingOnHead(float IntervalSeconds);

// --- `CNPC_VAsianVampire::StandingOnPlayer` (`0x10362730`) ---------------------------------------

/** The body: is the closest player's collision box overlapping mine in XY? Retail compares the 2-D
 *  distance between the two origins against `0.5 * |playerMaxs.xy - playerMins.xy|` plus
 *  `0.5 * |myMaxs.xy - myMins.xy|` — the two half-diagonals of the XY footprints, NOT their radii,
 *  and `_DAT_104454d0` is 0.5. Answers false with no closest player, which is retail's own arm. */
bool StandingOnPlayer() const;

/** The rule behind it, so the threshold is measurable without a world. All four extents are the
 *  collideable's OBB mins/maxs in CENTIMETRES; only X and Y are read. */
static bool StandingOnPlayerOverlap(const FVector& MyOriginCm, const FVector& OtherOriginCm,
	const FVector& MyMinsCm, const FVector& MyMaxsCm, const FVector& OtherMinsCm,
	const FVector& OtherMaxsCm);

// --- `CNPC_VWerewolf::UpdateFakeHull` (`0x103d93b0`) ---------------------------------------------

/** `FUN_10240250` — the axis-aligned box overlap the werewolf's fake hull is tested with, verbatim:
 *  true iff `BMax >= AMin` and `BMin <= AMax` on all three axes, with the comparisons in retail's
 *  order (X max, X min, Y max, Y min, Z max, Z min) and every one of them inclusive. */
static bool BoxesOverlap(const FVector& AMin, const FVector& AMax, const FVector& BMin,
	const FVector& BMax);

/** `UpdateFakeHull`'s activity pick: slot 323 (`vtable +0x50c`) classifies the direction from my
 *  `WorldSpaceCenter()` to the pushed entity's origin, and its answer selects the knockback
 *  activity handed to slot 320. 1 answers `0x7a`, 3 answers `0x7b`, anything else `0x79`. */
static int32 FakeHullKnockbackActivity(int32 DirectionClass);

/** `0x103d93b0`. `Now` is `gpGlobals->curtime` (`DAT_1070b228 + 0xc`), which every stamp on this
 *  struct is measured in. */
void UpdateFakeHull(double Now);

/** `0x103d93b0`'s overlap arm (`103d9643`..`103d9829`), split out because it is a body of its own:
 *  offset the pushed entity, classify the direction, pick the knockback activity, and — behind a
 *  one-second gate that the knockback is NOT behind — push damage. `BonePosUnits` is the `Bip01`
 *  point this call measured, in SOURCE units, and the delta against `m_vecFakeHullPos` is what
 *  becomes the force. */
void ApplyFakeHullPush(const FVector& BonePosUnits, double Now);

/** What `UpdateFakeHull`'s seams were asked, so a test can prove the body reached each one and that
 *  the refusal was the recovered one. Read by the test suite and by nothing else. */
struct FFakeHullSeamLedger
{
	int32 DebugHullDraws = 0;       // `DrawDebugHullAtPoint` behind the `DAT_1093f73c` cvar
	int32 NearestPointCalls = 0;    // `CollisionProperty::CalcNearestPoint` (`0x100dd000`)
	int32 DirectionClassCalls = 0;  // slot 323 on the pushed entity
	int32 KnockbackCalls = 0;       // slot 320 on the pushed entity
	int32 LastKnockbackActivity = 0;
	int32 DamagePushes = 0;         // `CBaseEntity::TakeDamage` past the one-second gate
	FVector LastDamageForceUnits = FVector::ZeroVector;
};
mutable FFakeHullSeamLedger FakeHullSeams;

/** SEAM for `CBaseEntity::GetEnemy()->+0xa8` — the entity `UpdateFakeHull` actually pushes, which
 *  is NOT the enemy itself: the body asks slot 167 for the enemy, tests the overlap against the
 *  ENEMY's collision box, then reads a pointer out of the enemy at `+0xa8` and offsets, classifies
 *  and damages THAT. `+0xa8` is in no datamap in the corpus and no body in layers 0–9 writes it, so
 *  which field it is is **unrecovered**. Answers the enemy itself, which is what a null `+0xa8`
 *  would make retail dereference — stated rather than guessed, and the ledger records the ask. */
FElysiumEntity* FakeHullPushTarget() const;

/** SEAM for `CollisionProperty::CalcNearestPoint` (`0x100dd000`) — the nearest point on an entity's
 *  OBB to a world point, which becomes the damage POSITION. No collision property here; answers the
 *  point unchanged, which is `CalcNearestPoint`'s own answer for a point already inside the box. */
FVector NearestPointOnEntity(const FElysiumEntity* Entity, const FVector& PointCm) const;

/** SEAM for slot 323 (`0x10344dd0`, `int vfunc323(const Vector&)`) and slot 320
 *  (`PlayerKnockbackReaction(CBaseCombatCharacter*, Activity)`) dispatched on the PUSHED entity.
 *  Both slots exist on this leaf and are 29e's stubs; when the pushed entity is an NPC they are
 *  called for real and when it is not they record and answer 0 / false. */
int32 PushedEntityDirectionClass(FElysiumEntity* Pushed, const FVector& DeltaCm) const;
bool PushedEntityKnockback(FElysiumEntity* Pushed, int32 Activity);

/** SEAM for `CBaseEntity::TakeDamage(CTakeDamageInfo)` with the packet `UpdateFakeHull` builds:
 *  20 damage, type `1`, sub-type `2`, `0x101c2b10(1)`, scale 1.0, a force of
 *  `(bonePos - m_vecFakeHullPos) * 500` and a position of the nearest point. This runtime's damage
 *  path is `ElysiumDamage::Apply`, which needs a `FElysiumDmg` descriptor and a dice context a
 *  kernel geometry body has no source for; the force and the position are the recovered halves and
 *  are what the ledger records. */
void PushFakeHullDamage(FElysiumEntity* Pushed, float Damage, const FVector& ForceUnits,
	const FVector& PositionUnits);

/** The `DAT_1093f73c` cvar `UpdateFakeHull` gates its `DrawDebugHullAtPoint` on
 *  (`!cvar->IsCommand() && cvar->m_nValue != 0`, retail's inlined `ConVar::GetInt`):
 *  `werewolf_show_debug`, shipped "0", which closes the draw. */
int32 FakeHullDebugCvar() const;

// --- `CNPC_VMingXiao`'s two severed-tentacle scatter notices -------------------------------------

/** `FUN_10397e00` — one severed tentacle moved; tell the owner's OTHER severed tentacles where it
 *  is. Walks `m_rhSeveredTentacles[6]` (`+0x66a8`, family **Squad**'s member), skips an unresolved
 *  handle and skips `Moved` itself, and hands each survivor `Moved`'s own `GetAbsOrigin()`. A null
 *  `Moved` does nothing, which is retail's first test.
 *
 *  The name is 29c's overlay target. `0x1039ef60` is the entry point above it: it resolves the
 *  moved tentacle's `m_hMingXiao` (`+0x665c`) and calls this ON THE OWNER, which is why the scatter
 *  centre handed out is the MOVED entity's position and not this NPC's. */
void NotifyOwnedCopiesOfOwnerMove(FElysiumEntity* Moved);

/** `0x1039ef60` — that entry point, so the owner walk can be entered the way retail enters it. */
void NotifyOwnerOfMyMove();

/** `FUN_103998d0` — `CNPC_VMingXiao::CoordinateTroops`'s severed-tentacle half, which the same
 *  overlay row names. Named by address because the behaviour is not the one above: it scatters ONE
 *  tentacle away from ME, and only when every gate holds —
 *
 *    * the tentacle's `m_iForcedSchedule` (`+0x65c8`) is neither `0x163` nor `0x165`;
 *    * its `m_ePhase` (`+0x6670`) is exactly 2;
 *    * the distance from me to it is at most `_DAT_1046dcd0` = 128 Source units;
 *    * the 2-D dot of the unit direction with `m_vecForward` (`+0x6290`) is at least
 *      `_DAT_10449260`, which is a **DOUBLE** and reads **0.25** — read as a float that cell is
 *      0.0 and the gate would admit the whole forward half-plane. */
void FUN_103998d0(FElysiumEntity* Tentacle);

/** The gate above as a pure rule, so the 128 and the 0.25 are measurable without a world.
 *  `DeltaCm` is the tentacle's origin minus mine; `Forward` is `m_vecForward`. */
static bool ScatterTentacleGate(const FVector& DeltaCm, const FVector& Forward);

/** `FUN_1039ef90` — the notice itself: fire the global melee-ish event at `DAT_10924a6c + 4`, set
 *  condition `0x78` on the notified tentacle and write `m_vecScatterCenter` (`+0x668c`). The
 *  condition write and the scatter centre are real here; the global event has no home in this
 *  substrate and is counted. */
void NotifyScatterCenter(FElysiumEntity* Tentacle, const FVector& PositionCm);

/** SEAM for `(**(code **)(*DAT_10924a6c + 4))()` — the global object `0x1039ef90` pokes before it
 *  touches the tentacle. `DAT_10924a6c` lives past `.data`'s raw size and no corpus body constructs
 *  it, so WHAT it is is **unrecovered**; the call is counted so a test can prove it was reached. */
int32 ScatterNoticeEvents = 0;

/** Retail's condition number `0x1039ef90` sets on the notified tentacle (`SetCondition`,
 *  `0x10269a20`). What condition `0x78` MEANS is not a fact of this family's rows; it is written
 *  through the port's condition set by number. */
static constexpr int32 ScatterNoticeCondition = 0x78;
