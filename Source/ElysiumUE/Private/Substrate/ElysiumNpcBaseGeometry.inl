// `CAI_BaseNPC`'s declarations of the `Geometry` family (story 5 step 5),
// moved from `ElysiumNpcGeometry*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseGeometry.cpp`.

/** `CBaseEntity::EyePosition` (`0x100b4b40`, slot 193) with the family's species override in front
 *  of it. The Troika-line body is `GetAbsOrigin() + m_vecViewOffset`, which this chain already
 *  answers through `FElysiumCombatCharacter::EyePosition()`; what lands here is the dispatch and
 *  the override:
 *
 *    * `CPayphone::vfunc193` (`0x101aae60`) looks up the bone `"Phone_bone_01"` and answers its
 *      world position, falling back to the base body when `LookupBone` answers -1.
 *
 *  `CAI_BaseHumanoid::vfunc193` (`0x1025e8e0`) was deleted by 0019 story 5 step 1: the class has no
 *  instance (`population.md`); its census row remains.
 *
 *  CENTIMETRES, because every caller of `EyePosition()` in this runtime is. */
virtual FVector EyePosition() const override;

/** `CAI_BaseNPC::FUN_102789c0`'s blend, lifted out of the body so the formula can be measured
 *  without a world and so the two random draws are the caller's. Every argument is CENTIMETRES and
 *  the answer is too.
 *
 *  `AnchorCm` is retail's `E`: `WorldSpaceCenter() - 0.25 * (WorldSpaceCenter() - GetAbsOrigin())`,
 *  a point a quarter of the way back down from the bounds centre toward the feet. `EyeCm` is slot
 *  193. `Noise1`/`Noise2` are the two independent `RandomFloat(0, 0.5)` draws the noisy arm makes,
 *  and are ignored by the other two arms. */
static FVector BodyTargetBlend(const FVector& AnchorCm, const FVector& EyeCm, bool bNoisy,
	bool bAimAtEyeExactly, float Noise1, float Noise2);

/** `E` itself, the anchor both non-noisy arms interpolate from. Exposed because the 0.25 is the
 *  half of this body a reader is most likely to get wrong: the delta is measured from
 *  `WorldSpaceCenter()` to `GetAbsOrigin()` and then subtracted from `WorldSpaceCenter()` AGAIN,
 *  through a SECOND slot-192 dispatch, which is why the body calls slot 192 twice. */
static FVector BodyTargetAnchor(const FVector& CentreCm, const FVector& OriginCm);

/** SEAM for slot 513 (vtable `+0x804`), the debug-overlay bit field `FUN_10274db0` tests `0x8000000`
 *  against. This runtime stands no per-NPC overlay word; answers 0, so the override arm never
 *  fires and every activity answers `m_vDefaultEyeOffset` — which is retail's own answer with the
 *  overlay off, and the shipped default. */
uint32 DebugOverlayBits() const;

