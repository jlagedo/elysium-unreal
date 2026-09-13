// Story 29c-1, family **Sounds** — the declarations of this family's layer 0–9 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual, and this family only defines it. What lands here is
// the non-slot half — the retail helpers, free functions and non-virtual methods of layers 0–9
// whose overlay row names a port method on `FElysiumNpc` that did not exist before this story.
//
// The definitions are in `Substrate/ElysiumNpcKernelSounds.cpp` and the tests in
// `Tests/ElysiumNpcKernelSoundsTests.cpp`. One file per family rather than 915 declarations
// appended to an already-oversized header: the family boundary is what this story ports by.

// --- The base-line halves of the four gates the Troika line replaces ---------------------------
//
// Four of this family's slots carry TWO retail bodies: `CAI_BaseNPC`'s and `CAI_BaseNPCTroika`'s.
// The leaf's virtual is the Troika one, because every `npc_V*` class in the family descends from
// `CAI_BaseNPCTroika`; the base body is declared here so it has a body and a test of its own, and
// because two of the four are still REACHED — the Troika override of slot 509 delegates to the
// base one, and the base body of slot 509 dispatches slot 510, whose Troika body tail-calls the
// base one. Nothing else in this runtime may call the three that are not reached.

// `CAI_BaseNPC::FOkToMakeSound` (`0x1027a5c0`), slot 486's base body. The sound-wait clock, the
// squad partner's copy of it, and `SF_NPC_GAG` outside combat. NOT reached on the Troika line:
// `0x102b4c10` replaces it outright rather than calling it.
bool BaseFOkToMakeSound() const;

// `CAI_BaseNPC::JustMadeSound` (`0x1027a640`), slot 487's base body: `m_flSoundWaitTime =
// curtime + RandomFloat(1.5, 2.0)`, and a SECOND independent draw for the connected squad's copy.
// Not reached on the Troika line (`0x102b4c40` replaces it with a 0.25–0.75 draw).
void BaseJustMadeSound();

// `CAI_BaseNPC::GetBestSound` (`0x1026aef0`), slot 474's base body: `m_pSenses->GetClosestSound(
// /*bScent*/ false)` with a "NULL Return from GetBestSound" dev warning on null. Not reached on the
// Troika line, whose `0x102b4520` answers `&m_BestSound` instead.
void* BaseGetBestSound();

// `CAI_BaseNPC::ShouldPlayIdleSound` (`0x1027a420`), slot 509's base body — REACHED, because the
// Troika override `0x10294040` delegates to it whenever the NPC is not in dialogue.
bool BaseShouldPlayIdleSound();

// `CAI_BaseNPC::ShouldPlayFloatSound` (`0x1027a530`), slot 510's base body — REACHED, because the
// Troika override `0x10294070` tail-calls it once its own seven gates pass.
bool BaseShouldPlayFloatSound() const;

// --- `CBaseCombatCharacter`'s two float-sound words --------------------------------------------
//
// `m_iFloatSoundFrequency` (`+0x10e8`, keyfield `floatfreq`) and `m_flNextFloatSoundTime`
// (`+0x10ec`, `FIELD_TIME`). Retail declares both on `CBaseCombatCharacter`, so the 388-word
// `CAI_BaseNPCTroika` shape map does not cover them and 29b declared neither.
//
// NAMED DECISION: they are carried on the NPC leaf rather than on `FElysiumCombatCharacter`.
// Every reader and every writer retail has for the pair is an NPC virtual — slot 510 reads both,
// slot 507 (`FloatSound`, story 29d) re-arms the stamp — and the player's copy is dead weight.
int32 FloatSoundFrequency = 0;
// SEAM: the one writer is slot 507 `FloatSound` (`0x10294f40`, layer 14, story 29d), which re-arms
// it from the `Float_Sound_Info` rule rows. Nothing in this runtime writes it yet, so the window
// test in `BaseShouldPlayFloatSound` always passes.
double NextFloatSoundTime = 0.0;

// `CNPC_VManBat::m_bHasPlayedFlyBySound` (`+0x66b8`, `FIELD_BOOLEAN`). A SPECIES word: `+0x66b8` is
// claimed by four different classes in the census and this is CNPC_VManBat's reading of it.
bool bHasPlayedFlyBySound = false;
// `FUN_10390040` (`0x10390040`), whose whole body is `this->m_bHasPlayedFlyBySound = false`. It has
// no caller in the image — the fly-by sound that would set it is unrecovered — so this is the state
// reset and nothing more.
void ClearHasPlayedFlyBySound();

// --- The species vocalization table (slots 488–508, 620, 621) ----------------------------------
//
// Twenty-one retail sound hooks, filled per species. Retail's bodies are one behaviour written
// many times: build a `CPASAttenuationFilter` at `GetSoundEmissionOrigin()` (slot 222), pick one
// wav out of a fixed table with `RandomInt(0, N)`, and `IEngineSound::EmitSound(filter, entindex,
// channel, wav, volume, attenuation 0.8, flags 0, pitch 100)`. Two species answer a SENTENCE GROUP
// instead of a wav pool, and one pair of species answers silence.
//
// So it is ONE method plus a table, keyed on the retail class name and resolved through
// `Substrate/ElysiumNpcKernelClassLookup.h` — never a subclass; `FElysiumNpc` is `final` and a
// species is data in this runtime.
//
// The Troika-line bodies BEHIND these slots (`0x10293ec0`, `0x10293f80`, `0x10294280`, …) are
// layer 14 and belong to story 29d, so their generated stubs still stand in
// `ElysiumNpcKernelSlots.cpp`. `EmitVocalization` is what 29d's bodies call first: a species row
// REPLACES the Troika body (every override here returns without calling up), so a true answer
// means "handled, do not run the base".
enum class EVocalization : uint8
{
	// The override's whole body is `return` — the species makes no sound at this hook.
	Mute,
	// `RandomInt(0, WavCount - 1)` over `Wavs`, then one `EmitSound`.
	WavPool,
	// `SENTENCEG_PlayRndSz(edict, group, volume, soundlevel, 0, pitch)` — a named sentence group
	// rather than a wav path.
	Sentence,
};

// One species row. Every row carries the retail class it came from AND the retail address of the
// body, so a reader can check it against `docs/vtmb/npc-kernel/slots.md`.
struct FVocalization
{
	const TCHAR* RetailClass = nullptr;
	int32 Slot = 0;
	const TCHAR* RetailAddress = nullptr;
	EVocalization Kind = EVocalization::Mute;
	// The wav table in retail's own order; `RandomInt(0, WavCount - 1)` indexes it.
	const TCHAR* const* Wavs = nullptr;
	int32 WavCount = 0;
	// The sentence group name for an `EVocalization::Sentence` row.
	const TCHAR* Sentence = nullptr;
	// `EmitSound`'s `flVolume`. 1.0 everywhere except the two death hooks, which pass 0.5.
	float Volume = 1.0f;
	// `EmitSound`'s `flAttenuation`. 0.8 (`ATTN_NORM`) on every recovered row.
	float Attenuation = 0.8f;
	// Source's `CHAN_*`: 2 `CHAN_VOICE` on the vocalizations, 4 `CHAN_BODY` on the Sabbat leader's
	// two hooks.
	int32 Channel = 2;
	// The override opens with `if (!FOkToMakeSound()) return;` (slot 486, vtable `+0x798`).
	bool bGatedByFOkToMakeSound = false;
	// The override ends with `JustMadeSound()` (slot 487, vtable `+0x79c`) inside the gate.
	bool bCallsJustMadeSound = false;
};

// The whole table, in (class, slot) order. Public so the suite can walk every row by name.
static const FVocalization* Vocalizations(int32& OutCount);

// The row a retail class answers at a slot: the class's own row, else the nearest ancestor's,
// which is the vtable's own rule and what makes `CNPC_VCameraSecurity` inherit `CNPC_VCamera`'s
// nineteen mute rows. Null when no class in the chain fills the slot.
static const FVocalization* VocalizationFor(const TCHAR* RetailClass, int32 Slot);

// The same for THIS NPC's species (`RetailClass()`). Null for a classname no family class claims.
const FVocalization* VocalizationFor(int32 Slot) const;

// Run the species override of `Slot`, if this species has one. True means the species body ran
// (including a `Mute` row and a row the sound gate refused) and the Troika-line body must not.
bool EmitVocalization(int32 Slot);
