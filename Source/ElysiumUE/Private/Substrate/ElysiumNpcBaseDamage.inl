// `CAI_BaseNPC`'s declarations of the `Damage` family (story 5 step 5),
// moved from `ElysiumNpcDamage*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseDamage.cpp`.

// +0x01fc m_takedamage (datamap, CBaseEntity). 0 = DAMAGE_NO, 1 = DAMAGE_EVENTS_ONLY,
// 2 = DAMAGE_YES. Retail's default for a live NPC is 2, which is what this seeds.
int32 TakeDamageMode = 2;

// `CNPC_VAndreiBlood`'s `SelectIdealState` tag. The port's mind transition trace does not carry
// retail's `{selector, file, line}` triple — the shape map calls `+0x1b38` ABSENT — so the one word
// `0x1035d150` writes is kept here so the arm is measurable.
int32 SelectIdealStateSelector = 0;          // +0x1b38 m_SelectIdealStateTrace.m_iSelector (walked)
