// `CAI_BaseNPC`'s declarations of the `Precache10` family (story 5 step 5),
// moved from `ElysiumNpcPrecache10*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBasePrecache10.cpp`.

/** `CAI_BaseNPC::Precache` (`0x1027bb50`), slot 104's body on the nine `CAI_BaseNPC`-line classes
 *  and the tail `0x10298ad0` chains into. A DISTINCT retail function beside the Troika override, so
 *  it is ported under its own name.
 *
 *  Three steps, in order:
 *    1. `m_spawnEquipment` (`+0x5dec`) goes through `UTIL_PrecacheOther` when it is non-null AND is
 *       not the one-character sentinel `"0"` (`DAT_105399a0`).
 *    2. slot 452 `LoadedSchedules` (vtable `+0x710`) decides the rest. **False** prints
 *       `DevMsg("ERROR: Rejecting spawn of %s as error in NPC's schedules.\n", GetDebugName())`,
 *       `UTIL_Remove`s the entity and **returns without chaining the base at all**.
 *    3. True falls through to `CBaseCombatCharacter::Precache`.
 *
 *  The port answers `LoadedSchedules` **true always by design** (`ElysiumNpcSchedule.cpp:297`
 *  — nothing here parses schedule text, so no flag can be cleared), so the reject arm is
 *  UNREACHABLE in this runtime today. It is ported anyway, because the day a schedule parser lands
 *  the arm is what a malformed schedule reaches, and a test drives it through the same seam.
 *
 *  `CBaseCombatCharacter::Precache` (`0x10011324`) is `CBaseCombatCharacter::PrecacheOnce`'s
 *  once-guarded global block — the discipline, damage-effect and HUD emitters every character
 *  shares. It is NOT an NPC-kernel row and has no port body; see the definition. */
// Declared by the generated slot surface (`ElysiumNpcBaseSlots.inl`, slot 104); defined by hand as
// `FElysiumNpcBase::Precache`.

/** The engine entry one precache request goes through. Retail reaches four different functions and
 *  WHICH one is part of the recovered body — a model index is returned and stored, a particle
 *  system is not, and `UTIL_PrecacheOther` spawns a whole entity to run ITS slot 104. */
enum class EPrecacheChannel : uint8
{
	/** `(*DAT_1070b22c)+0x34` — `IVEngineServer::PrecacheModel(name, preload)`. Answers a model
	 *  index, which three bodies in this family store. */
	Model,
	/** `(*DAT_1070b248)+0x00` — `CSoundEmitterSystem::PrecacheScriptSound(name, flag)`. */
	Sound,
	/** `(*DAT_1070b22c)+0x44` — the particle-system precache. `Preload` is 1 on the boss emitters
	 *  and 0 on Ming Xiao's, the Hengeyokai's and the Zombie's, and that split is retail data. */
	Particle,
	/** `UTIL_PrecacheOther` `0x101d0ec0` — create the entity by classname, dispatch ITS vtable
	 *  `+0x1a0` (slot 104), then remove it. A classname that creates nothing prints
	 *  `"NULL Ent in UTIL_PrecacheOther: %s"`. */
	Other,
	/** `0x101d0f10` — glob `"<dir>/*<ext>"` through the filesystem and precache every non-directory
	 *  hit as a sound. See `PrecacheDirectory`. */
	Directory,
};

/** One request, as retail issued it. */
struct FPrecacheOp
{
	EPrecacheChannel Channel = EPrecacheChannel::Model;
	/** The name retail pushed, verbatim. A null `string_t` reads as the empty string
	 *  (`DAT_106b8540`), so an empty name here is retail's own argument and not a gap. */
	FString Name;
	/** The engine call's second argument: the preload flag for `Model` and `Particle`, the sound
	 *  flag for `Sound`, and `0x101d0f10`'s FOURTH argument for `Directory`. */
	int32 Flag = 0;
	/** `Directory` only: the extension pattern (`".wav"` `DAT_10598a30` or `".mp3"`
	 *  `DAT_10548ed4`). */
	FString Extension;
	/** `Directory` only: `0x101d0f10`'s THIRD argument, which picks the name format each hit is
	 *  precached under — `"*%s/%s"` when set, `"%s/%s"` when clear, both over `dir + 6` (retail
	 *  skips the literal `"sound/"` the directory always opens with). The Troika body passes 1 and
	 *  the Werewolf passes 0, which is a real difference between two call sites of one function. */
	bool bStarPrefix = false;
};

/** Every request this NPC's precache issued, in retail's order. Never cleared by a body: retail's
 *  precaches are cumulative on the engine side and a second `Precache()` appends, as retail's
 *  second call would. */
TArray<FPrecacheOp> PrecacheLog;

/** SEAM — the acquisition half of every request above, and the one place a future asset path hooks
 *  in. It records the op and does nothing else.
 *
 *  There is no per-entity precache entry point in this substrate to route to: model residency is
 *  the map epoch's, derived from the entity defs before any NPC stands
 *  (`FElysiumMapActor::PreparePropAndWieldModels`, whose own comment says "residency is
 *  entity-derived, as retail's per-entity Precache was"), sounds are baked soundscripts the bake
 *  resolves by name, and this runtime stands no Source particle systems at all. So the four engine
 *  entries named on `EPrecacheChannel` have no callee here and this answers NOTHING for all four.
 *  What it does carry is the recovered half — the name, the channel, the flag and the ORDER. */
void IssuePrecache(const FPrecacheOp& Op);
