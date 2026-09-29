// Story 29c-1, family **Geometry** — the declarations of this family's layer 0–9 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual, and this family only defines it. What lands here is
// the non-slot half — the retail helpers, free functions and non-virtual methods of layers 0–9
// whose overlay row names a port method on `FElysiumNpc` that did not exist before this story.
//
// The definitions are in `Substrate/ElysiumNpcGeometry.cpp` and the tests in
// `Tests/ElysiumNpcKernelGeometryTests.cpp`. One file per family rather than 915 declarations
// appended to an already-oversized header: the family boundary is what this story ports by.
//
// The family is **where a body sits and how big it is**: the eye/anchor points (slots 193, 194,
// 195, 197, 533 and the species-dispatched 192), the hull-bit query (slot 337), `SetSize`
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
//     the head of `ElysiumNpcGeometry.cpp`, one line each.
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

/** `+0x668c CNPC_VMingXiaoTentacle::m_vecScatterCenter` — the point a severed tentacle is told to
 *  scatter away from. Family **Squad** owns `+0x668c` as `CNPC_VMingXiao::m_rhProxies[6]` and
 *  family **Bosses** owns it as `CNPC_VManBat::m_hPickupTarget`; this is a THIRD species' word at
 *  the same offset, so it stands beside them rather than replacing either. SOURCE units. */
FVector TentacleScatterCenterUnits = FVector::ZeroVector;

// --- Slot 193 `EyePosition`, and its species override --------------------------------------------

/** SEAM for `CBaseAnimating::LookupBone(name)` + `CBaseAnimating::GetBonePosition02(bone, &pos,
 *  &ang)` — the pair `CPayphone::vfunc193` runs — and, with the local offset already folded in, for
 *  `UpdateFakeHull`'s `GetBoneTransform(bone, m)` plus the two `VectorTransform` calls that turn
 *  `vec3_origin` into the `Bip01` bone's world point. This substrate's animating tier exposes no
 *  bone table to the kernel; answers false and leaves `OutPositionCm` untouched, which is retail's
 *  own `LookupBone == -1` arm for the payphone and, for the werewolf, the arm that leaves the fake
 *  hull where it was. `BoneWorldPositionCalls` is what a test reads to prove the seam was asked. */
bool BoneWorldPosition(const TCHAR* BoneName, FVector& OutPositionCm) const;
mutable int32 BoneWorldPositionCalls = 0;

// --- Slot 194 / 195, the two angle aliases -------------------------------------------------------
//
// `0x100b4bc0` is `MOV EAX,[ECX]; JMP [EAX+0x36c]` and `0x100b4be0` is the same with `+0x374`:
// eight bytes each, a tail call and nothing else. 82 and 80 classes respectively fill the slot and
// every one of them with that body. The definitions forward to slots 219 and 221 verbatim.

// --- Slot 197 `BodyTarget` -----------------------------------------------------------------------

// --- Slot 192 `WorldSpaceCenter`, species-dispatched ---------------------------------------------

/** Slot 192 as the species line dispatches it. Its only census override, `CNPC_Crow::vfunc192`
 *  (`0x10357760`), is on a class no map stands and carries no arm, so every class answers the
 *  Troika-line body (`0x10027160`).
 *
 *  **This is NOT the slot definition.** Slot 192's Troika-line body is another story's row and the
 *  generator already emits `FElysiumNpc::WorldSpaceCenter()` in `ElysiumNpcKernelSlots.cpp`, so
 *  defining it here would be a duplicate symbol. */
FVector SpeciesWorldSpaceCenter() const;

// --- Slot 213 `SetSize` --------------------------------------------------------------------------
//
// `0x100b1890` is a scope-trace push, three stores into `m_vecSize` and a scope-trace pop. The
// scope trace is retail's crash-report breadcrumb stack and has no observable effect; the three
// stores are the body.

// --- Slot 337 `GetUsedHullBits` ------------------------------------------------------------------

/** `CBaseCombatCharacter::GetUsedHullBits` (`0x10341710`), the bottom of the chain: a scope-trace
 *  pair and `return 1`. `CAI_BaseNPC`'s ORs `0x1` onto it and `CAI_BaseNPCTroika`
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

// --- `CNPC_VWerewolf::UpdateFakeHull` (`0x103d93b0`) ---------------------------------------------

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

// --- `CNPC_VMingXiao`'s two severed-tentacle scatter notices -------------------------------------

/** `FUN_1039ef90` — the notice itself: ask `ent_trace_melee`'s `IsCommand()` (answer dropped) at `DAT_10924a6c + 4`, set
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
