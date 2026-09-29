# R1c report

All 23 addresses de-cited in the files the brief names. Comments only; no behaviour change.

- 0x1004fbb0/0x1004fbf0/0x1009eca0/0x1009ebb0: Sounds10.cpp header now lists only slot 110 and says slots 108/109 and formatters are dead.
- 0x102775e0, 0x103681d0 (KernelBaseHelpers.cpp), 0x10294e70 (Sounds.cpp, Sounds10.cpp, SpeciesTests), 0x102e1110 (MoveStepTests), 0x102eea30, 0x10345460 (BaseHelpers.cpp), 0x10364550 (MiscTests), 0x103a0ff0 (Newscaster.cpp/.h), 0x103b0560, 0x103b9270, 0x103d5130 (Werewolf2Species.cpp, SpeciesMisc10Tests): reworded to "dead twin/overlay (0019/6)" or the address dropped; live addresses kept.
- 0x1034bd30, 0x10375440, 0x103dc950: orphan slot comments deleted (Maker.h, FrenzyShadow.h, WolfMorph.h).
- 0x103dc770/0x103dc8f0, 0x103d1ca0/0x103d1d60: dropped from header lists.
- 0x1028d990: Flags.h tables kept (parsed by the NPCFlag keyfield; legend string only confirms bit order); comments reworded. `RawWord1` has live users, so kept. Conditions10Tests comment reworded.

## Needs another owner (hand files not in my list, still cite dead rows)
- Private/Substrate/ElysiumNpcConditions10.inl:19 `0x1028d990`: drop "(`0x1028d990`)".
- ElysiumNpcBaseMotor.inl:199 `0x102e1110`: drop it from the comment.
- ElysiumNpcSpeciesMisc10.inl:159 `0x103d5130 (slot 76),`: remove.
- ElysiumNpcThinkSpecies.cpp:279 `FUN_103a0ff0`: drop the name (form not matched by the address regex).
- Public/ElysiumAnimatingSlotBodies.inl:97-104 and *Slots.* / KernelShape.cpp are generated; their cites stay.
