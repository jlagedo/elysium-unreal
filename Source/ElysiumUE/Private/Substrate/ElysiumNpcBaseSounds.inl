// `CAI_BaseNPC`'s declarations of the `Sounds` family (story 5 step 5),
// moved from `ElysiumNpcSounds*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseSounds.cpp`.

// `CAI_BaseNPC::FOkToMakeSound` (`0x1027a5c0`), slot 486's base body. The sound-wait clock, the
// squad partner's copy of it, and `SF_NPC_GAG` outside combat. NOT reached on the Troika line:
// `0x102b4c10` replaces it outright rather than calling it.
// Declared by the generated slot surface (`ElysiumNpcBaseSlots.inl`, slot 486); defined by hand as
// `FElysiumNpcBase::FOkToMakeSound`.

// `CAI_BaseNPC::JustMadeSound` (`0x1027a640`), slot 487's base body: `m_flSoundWaitTime =
// curtime + RandomFloat(1.5, 2.0)`, and a SECOND independent draw for the connected squad's copy.
// Not reached on the Troika line (`0x102b4c40` replaces it with a 0.25–0.75 draw).
// Declared by the generated slot surface (`ElysiumNpcBaseSlots.inl`, slot 487); defined by hand as
// `FElysiumNpcBase::JustMadeSound`.

// `CAI_BaseNPC::GetBestSound` (`0x1026aef0`), slot 474's base body: `m_pSenses->GetClosestSound(
// /*bScent*/ false)` with a "NULL Return from GetBestSound" dev warning on null. Not reached on the
// Troika line, whose `0x102b4520` answers `&m_BestSound` instead.
// Declared by the generated slot surface (`ElysiumNpcBaseSlots.inl`, slot 474); defined by hand as
// `FElysiumNpcBase::GetBestSound`.

// `CAI_BaseNPC::ShouldPlayIdleSound` (`0x1027a420`), slot 509's base body — REACHED, because the
// Troika override `0x10294040` delegates to it whenever the NPC is not in dialogue.
// Declared by the generated slot surface (`ElysiumNpcBaseSlots.inl`, slot 509); defined by hand as
// `FElysiumNpcBase::ShouldPlayIdleSound`.

// `CAI_BaseNPC::ShouldPlayFloatSound` (`0x1027a530`), slot 510's base body — REACHED, because the
// Troika override `0x10294070` tail-calls it once its own seven gates pass.
// Declared by the generated slot surface (`ElysiumNpcBaseSlots.inl`, slot 510); defined by hand as
// `FElysiumNpcBase::ShouldPlayFloatSound`.
