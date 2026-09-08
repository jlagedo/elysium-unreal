#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Templates/Function.h"

// The footstep subsystem's log category. Declared beside the tuning rather than inside the
// producers for `LogElysiumMovement`'s reason: the pure half owns the cvar surface and has to be
// able to refuse a value out loud.
ELYSIUMUE_API DECLARE_LOG_CATEGORY_EXTERN(LogElysiumFootsteps, Log, All);

// VtMB's footstep cvars, reproduced 1:1 by name and default.
//
// Same posture as `FElysiumMoveTuning` (`ElysiumMoveSolve.h`): they are **declared into the VtMB
// console store** (`ElysiumCommandBus::Console`), not registered as engine `elysium.*` cvars, so a
// user's `config.cfg` keeps governing and `elysium.cmd footstep_pc_vol 0.8` is the same write the
// game itself would make. `ElysiumFootstep::CvarDefs()` is the declaration; `LoadFrom` is the read.
//
// **Distances stay in SOURCE UNITS.** `FElysiumMoveTuning` converts to centimetres at `LoadFrom`
// because its values are lengths the mover integrates. These two are not lengths anything moves
// along: they are the arguments of `ElysiumSoundLevel::FromDistanceUnits`, whose `20*log10(d/36)`
// is written against Source units and would answer a different level for the same authored
// distance if it were handed centimetres. The conversion happens inside `MakeAttenuation`, once.
//
// The retail sources, all read out of the shipped `vampire.dll` image:
//   `footstep_normal_vol`         `0x1026d1b0`, default "0.5"
//   `footstep_normal_dist`        `0x1026d240`, default "256"   (string at `0x105cbe74`)
//   `footstep_heavy_vol`          `0x1026d2d0`, default "0.85"  (string at `0x105cbe90`)
//   `footstep_heavy_dist`         `0x1026d360`, default "512"   (string at `0x105cbeb0`)
//   `footstep_npc_use_templates`  `0x1026d3f0`, default "1"
//   `footstep_pc_vol`             `0x1011d7e0`, default "0.5"
//   `sv_footsteps`                the stock Source gate `UpdateStepSound` reads, default "1"
//
// The cvar pair is the FALLBACK source for an NPC step: with `footstep_npc_use_templates` non-zero
// (its default), `0x1026d460` reads `NormalFootfallVol/Dist` and `HeavyFootfallVol/Dist` off the
// NPC's character template instead, and these are never consulted.
struct FElysiumFootstepTuning
{
	// `footstep_npc_use_templates` — 1 = read the four footfall keys off the NPC's template,
	// 0 = use the four cvars below.
	bool  bNpcUseTemplates = true;

	// The "normal" footfall (animation events 2050/2051).
	float NormalVolume = 0.5f;
	float NormalDistanceUnits = 256.0f;

	// The "heavy" footfall (2052/2053 — every clip that carries them is a `*_run`).
	float HeavyVolume = 0.85f;
	float HeavyDistanceUnits = 512.0f;

	// `footstep_pc_vol` — the final multiplier on the player's own dry step volume
	// (`CBasePlayer::UpdateStepSound`, `0x1011e940`), applied after the gamematerial pair and the
	// duck scale and before the clamp to 1.
	float PlayerVolume = 0.5f;

	// `sv_footsteps` — zero silences the player's step clock entirely. It does NOT gate the NPC
	// animation-event path, which `0x1026d460` runs without consulting it.
	bool  bServerFootsteps = true;

	// The volume/distance pair one footfall mode draws from these cvars. `bHeavy` is
	// `0x1026d460`'s `param_1`.
	float VolumeFor(bool bHeavy) const { return bHeavy ? HeavyVolume : NormalVolume; }
	float DistanceUnitsFor(bool bHeavy) const { return bHeavy ? HeavyDistanceUnits : NormalDistanceUnits; }

	// Re-read the whole surface. `Lookup` returns a cvar's value string, or empty for a name the
	// store does not carry — which is how the console itself answers, and an empty read keeps the
	// default so a run with no `out/cfg` behaves like a stock install. A value that is PRESENT and
	// unparsable also keeps the default and says so once per name, `FElysiumMoveTuning::LoadFrom`'s
	// rule and for its reason: this is re-read while the game runs, and a line per frame would
	// repeat at the frame rate.
	void LoadFrom(TFunctionRef<FString(const TCHAR*)> Lookup);

private:
	TSet<FName> ReportedMalformed;
};

namespace ElysiumFootstep
{
	// One row of the cvar surface: the VtMB name, its default **as typed**, and what it does. The
	// same shape `ElysiumMove::FCvarDef` has, declared at the same site
	// (`ElysiumCommandBus::Console`).
	struct FCvarDef
	{
		const TCHAR* Name;
		const TCHAR* Default;
		const TCHAR* Help;
	};
	TArrayView<const FCvarDef> CvarDefs();

	// The four `General` keys `0x101d3f10` reads off a character template, and the defaults it
	// writes when the block does not author them. Named here because both the template reader
	// (`FElysiumClanTemplate::GeneralFloat`) and the cvar fallback above have to agree about what
	// "the normal footfall distance" is.
	//
	// Read from the listing as immediates: `0x3ee66666` = 0.45, `0x43800000` = 256,
	// `0x3f59999a` = 0.85, `0x44000000` = 512. The shipped `npctemplate*.txt` author 0.45/300 and
	// 0.85/600 and inherit them through `ParentTemplateName`.
	inline const TCHAR* KeyNormalVol  = TEXT("NormalFootfallVol");
	inline const TCHAR* KeyNormalDist = TEXT("NormalFootfallDist");
	inline const TCHAR* KeyHeavyVol   = TEXT("HeavyFootfallVol");
	inline const TCHAR* KeyHeavyDist  = TEXT("HeavyFootfallDist");

	inline constexpr float TemplateNormalVolDefault  = 0.45f;
	inline constexpr float TemplateNormalDistDefault = 256.0f;
	inline constexpr float TemplateHeavyVolDefault   = 0.85f;
	inline constexpr float TemplateHeavyDistDefault  = 512.0f;
}
