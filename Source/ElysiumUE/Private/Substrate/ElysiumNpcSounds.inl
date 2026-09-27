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

// --- The species vocalization hooks (slots 488–508, 620, 621) ----------------------------------
//
// Retail's species bodies at these slots are plain overrides on their classes (0019 story 5): the
// nineteen one-byte `RET` bodies of `CNPC_VCamera` (inherited by `CNPC_VCameraSecurity`), the two
// `CHAN_BODY` wav hooks `CNPC_VSabbatLeader` introduces at 620/621, and `CNPC_VTzimisce`'s two
// sentence-group hooks. Every wav-table species is dead. The Troika-line bodies BEHIND these
// slots (`0x10293ec0`, `0x10293f80`, `0x10294280`, …) are layer 14 and story 29d's; a species
// override replaces the Troika body and never calls up. The shared wav arm is
// `NpcKernelSoundsShared::SoundsEmitSpeciesWav`.

