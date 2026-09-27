// Story 29c-1, family **Sounds** — the declarations of this family's layer 0–9 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual, and this family only defines it. What lands here is
// the non-slot half — the retail helpers, free functions and non-virtual methods of layers 0–9
// whose overlay row names a port method on `FElysiumNpc` that did not exist before this story.
//
// The definitions are in `Substrate/ElysiumNpcSounds.cpp` and the tests in
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

// `CBaseCombatCharacter`'s two float-sound words (`FloatSoundFrequency`, `NextFloatSoundTime`) are
// declared on `FElysiumCombatCharacter`, where retail declares them (story 5 step 5).

// --- The species vocalization table (slots 488–508, 620, 621) ----------------------------------
//
// Twenty-one retail sound hooks, filled per species. Retail's bodies are one behaviour written
// many times: build a `CPASAttenuationFilter` at `GetSoundEmissionOrigin()` (slot 222), pick one
// wav out of a fixed table with `RandomInt(0, N)`, and `IEngineSound::EmitSound(filter, entindex,
// channel, wav, volume, attenuation 0.8, flags 0, pitch 100)`. Two species answer a SENTENCE GROUP
// instead of a wav pool, and one pair of species answers silence.
//
// So it is ONE body plus a table, keyed on the retail class name: since story 5 step 3 each row is
// the body of its class's override of the hook (`SpeciesVocalize`), which a subclass inherits.
//
// The Troika-line bodies BEHIND these slots (`0x10293ec0`, `0x10293f80`, `0x10294280`, …) are
// layer 14 and belong to story 29d, so their generated stubs still stand in
// `ElysiumNpcKernelSlots.cpp`. A species row REPLACES the Troika body: every override here returns
// without calling up.
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

// The row a retail class answers at a slot: the class's OWN row (a subclass inherits its base's
// through the C++ override — `CNPC_VCameraSecurity` takes `FElysiumNpcCamera`'s nineteen). Null
// when the class fills no row at the slot.
static const FVocalization* VocalizationFor(const TCHAR* RetailClass, int32 Slot);

// The body of a species class's vocalization override: `SpeciesClass`'s row at `Slot`. True means
// a row ran (including a `Mute` row and a row the sound gate refused); false that there was none.
bool SpeciesVocalize(const TCHAR* SpeciesClass, int32 Slot);

