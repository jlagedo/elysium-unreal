#pragma once

#include "CoreMinimal.h"

struct IElysiumRetailSiteSink;

// The blood effect chain (`walks/L0-r013.md`, story L0.effects_world.blood-effects): the `SpawnBlood`
// wrapper `FUN_102699e0` 0x102699e0 every `TraceAttack` calls, the dispatcher `FUN_101cfb30`
// 0x101cfb30 (the shape of Source's `UTIL_BloodDrips`; the retail name is UNRECOVERED) and the
// admission gate `FUN_101cf9b0` 0x101cf9b0 over the engine cvars `violence_hblood` / `violence_ablood`
// (engine.dll `staticinit_200ed990` / `_200ed9d0`, both default "1", flags 0). Free functions, as
// retail's are; each reports its arms through the sink the caller hands it (the NPC's own entity
// column from `TraceAttack`, a utility name from a record).
namespace ElysiumBlood
{
	// `FUN_101cf9b0`: 1 when colour `Color`'s blood is admitted. `0xFFFFFFFF` (DONT_BLEED) refuses;
	// 0xF7 (BLOOD_COLOR_RED, the human) asks `violence_hblood`, every other colour `violence_ablood`:
	// `FindVar(name)` (`VEngineCvar001` vslot 2) null refuses, else admit iff the object is not a
	// command (`vslot1()` false for a cvar) and its INT value (`+0x2c`, `ftol(atof(string))`) is non-zero.
	bool Admit(uint32 Color, IElysiumRetailSiteSink* Sink);

	// `FUN_101cfb30(pos, dir, color, amount)`: gate, `-1`, `amount == 0`, the `DAT_1070ba34` remap, the
	// rules' `IsMultiplayer` x5, the 255 clamp, the mechanical arm (colour 0x14: `IEffects` Sparks, a
	// `RandomFloat(0, 2) < 1` early-out, `RandomInt(10, 15)` smoke), else the colour triple, the PVS
	// filter, `clamp(amount / 10, 3, 16)` and `CTempEntsSystem` slot 14 -- whose body `FUN_1005df40` is
	// a bare `ret`: the spray emits nothing in retail. `PosUnits` / `DirUnits` are Source units.
	void Drips(const FVector& PosUnits, const FVector& DirUnits, uint32 Color, int32 Amount, IElysiumRetailSiteSink* Sink);

	// `FUN_102699e0(pos, color, damage)`: `amount = __ftol(damage)` (truncation toward zero), `dir =
	// &DAT_1070ba40` (the attack direction the `OnTakeDamage` family writes; this port's
	// `NpcKernelDamageShared::GDeathThrowImpulse`), then `Drips`.
	void SpawnBlood(const FVector& PosUnits, uint32 Color, float Damage, IElysiumRetailSiteSink* Sink);
}
