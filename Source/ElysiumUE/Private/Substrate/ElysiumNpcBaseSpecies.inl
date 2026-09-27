// `CAI_BaseNPC`'s declarations of the `Species` family (story 5 step 5),
// moved from `ElysiumNpcSpecies*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseSpecies.cpp`.

/** `CAI_BaseNPC::FUN_10344dd0` `0x10344dd0`, slot 323 — the four codes retail returns for a movement
 *  direction relative to the body's own facing, by the angle band the direction falls in. Every
 *  boundary below was read out of the pinned `vampire.dll`'s `.rdata`, so the bands are recovered
 *  numbers and the NAMES are this port's reading of them (`+yaw` is counter-clockwise in Source, so
 *  a relative yaw of 90 is to the body's left). The NUMBERS are retail's own return values.
 *
 *  Note `_DAT_1049e8a4` is **316**, not 315: the four bands are 89, 90, 90 and 91 degrees wide, not
 *  a clean quarter split. That is retail's number and it is kept. */
enum class EMoveDirectionCode : int32
{
	Behind = 0,   // (135, 225]                — `_DAT_1049e8a0` .. `_DAT_1049e89c`, and the too-slow arm
	Right = 1,    // (225, 316]                — above `_DAT_1049e89c`
	Ahead = 2,    // (316, 360) and [0, 45]    — above `_DAT_1049e8a4`, or at-or-below `_DAT_1049949c`
	Left = 3,     // (45, 135]                 — at-or-below `_DAT_1049e8a0`
};
