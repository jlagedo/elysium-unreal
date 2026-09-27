// Story 29d, family **Sounds10** — the declarations of this family's layer 10–18 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual and this family only defines it. What lands here is
// the non-slot half — the shared helpers, the two packets and the seams the slot bodies go through.
//
// The definitions are in `Substrate/ElysiumNpcSounds10.cpp` and the tests in
// `Tests/ElysiumNpcKernelSounds10Tests.cpp`. The walked prose is `docs/vtmb/npc-ai/senses.md`
// § "Story 29d, family Sounds10 — …".
//
// This family is **the seventeen sound hooks of slots 488–507, the three `KeyValue` overloads
// (slots 108/109/110) with the two `CBaseEntity` formatters behind them, slot 185 `FireBullets`,
// and the two `CNPC_VWerewolf` species arms over them**. It is NOT family
// **Sounds** (story 29c-1), which owns the layer 0–9 gates in front of these hooks — slot 486
// `FOkToMakeSound`, slot 487 `JustMadeSound`, slots 509/510 and the per-species vocalization table
// — and whose `ElysiumNpcSounds.cpp` still carries the two slot definitions (488 and 506)
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
 *  called from `FElysiumNpc::DeathSound` in family **Sounds**' file; `CNPC_VTzimisce` (`0x103b92a0`)
 *  and `CNPC_VCamera` (`0x103680b0`) override that slot on their C++ classes (story 5 step 3). */
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

// --- Slot 185 `FireBullets` --------------------------------------------------------------------

