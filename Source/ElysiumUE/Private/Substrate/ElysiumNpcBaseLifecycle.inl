// `CAI_BaseNPC`'s declarations of the `Lifecycle` family (story 5 step 5),
// moved from `ElysiumNpcLifecycle*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseLifecycle.cpp`.

/** `+0x1ddc`, read by `FUN_10160680` — a float scaled by the compiled constant `DAT_10725c9c`.
 *  **Unrecovered**: the body has one direct caller, no vtable slot, and neither the retail field
 *  name nor the class that owns the offset is settled. Declared by offset, as 29b declares an
 *  unsettled word. */
float Field_0x1ddc = 0.f;

/** What `CBaseEntity::KeyValue(const char*, const char*)` (`0x1009e430`) did with one key — the
 *  cascade slot 110 runs for `CAISound`, `CAI_Hint`, `CAI_InterestingPlace`,
 *  `CAI_InterestingPlaceConverstation` and `CAI_StandoffGoal`. */
enum class EKeyValueArm : uint8
{
	RenderColor,     // rendercolor / rendercolor32 -> m_clrRender RGB
	RenderAmt,       // renderamt -> m_clrRender alpha, atoi
	DisableShadows,  // disableshadows, nonzero -> m_fEffects |= 0x20
	DisableReceiveShadows,  // disablereceiveshadows, nonzero -> m_fEffects |= 0x80
	Mins,            // mins -> SetCollisionBounds(value, current maxs)
	Maxs,            // maxs -> SetCollisionBounds(current mins, value)
	Angle,           // angle -> rewritten as angles and re-dispatched
	Angles,          // angles -> vtable +0x368 SetAbsAngles
	Origin,          // origin -> vtable +0x360 SetAbsOrigin
	DataMap,         // no literal matched: walk the datamap chain (vtable +0x148)
};
