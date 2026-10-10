#pragma once

#include "CoreMinimal.h"

class FElysiumEntityWorld;
struct IElysiumRetailSiteSink;

// The blood effect chain (`walks/L0-r013.md`, `walks/L0-r014.md`, story L0.effects_world.blood-effects):
// the `SpawnBlood` wrapper `FUN_102699e0` 0x102699e0 every `TraceAttack` calls, the dispatcher
// `FUN_101cfb30` 0x101cfb30 (the shape of Source's `UTIL_BloodDrips`; the retail name is UNRECOVERED),
// the admission gate `FUN_101cf9b0` 0x101cf9b0 over the engine cvars `violence_hblood` /
// `violence_ablood` (engine.dll `staticinit_200ed990` / `_200ed9d0`, both default "1", flags 0), and
// the two `CEffectsServer` temp entities the mechanical arm sends (Sparks 0x100f6050, Smoke
// 0x100f5e80). Free functions, as retail's are; each reports its arms through the sink the caller
// hands it (the NPC's own entity column from `TraceAttack`, a utility name from a record). `World`
// is where the recipients (the players) and the embodiment that draws an effect are found; null is
// a world with neither (a unit fixture), which retail's empty-recipient path also knows.
namespace ElysiumBlood
{
	// `FUN_101cf9b0`: 1 when colour `Color`'s blood is admitted. `0xFFFFFFFF` (DONT_BLEED) refuses;
	// 0xF7 (BLOOD_COLOR_RED, the human) asks `violence_hblood`, every other colour `violence_ablood`:
	// `FindVar(name)` (`VEngineCvar001` vslot 2) null refuses, else admit iff the object is not a
	// command (`vslot1()` false for a cvar) and its INT value (`+0x2c`, `ftol(atof(string))`) is non-zero.
	// Retail answers in AL only (the upper bits of EAX are residue); every caller tests AL.
	bool Admit(uint32 Color, IElysiumRetailSiteSink* Sink);

	// `CRecipientFilter` ctor `FUN_1019ce00` + `CPVSFilter::AddRecipientsByPVS` `FUN_1019d210(pos)`:
	// with one client slot (`[DAT_1070b228+0x14] == 1`, single player) `FUN_1019cff0` adds every player
	// slot that `FUN_101cd9e0` resolves; otherwise the engine's PVS mask (`VEngineServer014` slot 64)
	// picks them. Answers the recipient count: the seated player, or 0 without one.
	int32 PvsFilterRecipients(const FVector& PosUnits, FElysiumEntityWorld* World, IElysiumRetailSiteSink* Sink);

	// `CEffectsServer::vfunc3` 0x100f6050 Sparks(origin, magnitude, trailLength, dir): a `CPVSFilter`,
	// the (dead) suppress-host test, then `CTempEntsSystem` slot 35 0x10059860 -> `FUN_10067b70` (a
	// NULL `dir` reads `vec3_origin` `DAT_1070d1b0`) -> `CTESparks::Create` 0x100677e0, the networked
	// temp entity. Drawn through `IElysiumEmbodiment::EmitTempEntitySparks`.
	void Sparks(const FVector& PosUnits, int32 Magnitude, int32 TrailLength, const FVector* DirUnits,
		FElysiumEntityWorld* World, IElysiumRetailSiteSink* Sink);

	// `CEffectsServer::vfunc2` 0x100f5e80 Smoke(origin, modelIndex, scale, framerate): a `CPVSFilter`,
	// the (dead) suppress-host test, then slot 34 0x100597d0 with `scale * 0.1f` (`_DAT_104491b4`) and
	// `_ftol(framerate)` -> `FUN_100675b0` -> `CTESmoke::Create` 0x10067200. Drawn through
	// `IElysiumEmbodiment::EmitTempEntitySmoke`.
	void Smoke(const FVector& PosUnits, int32 ModelIndex, float Scale, float Framerate,
		FElysiumEntityWorld* World, IElysiumRetailSiteSink* Sink);

	// `FUN_101cf640(pos, scale, framerate)` 0x101cf640: `Smoke(pos, (int16)DAT_1088ae52, scale, framerate)`.
	// `DAT_1088ae52` (the smoke sprite's model index) has 12 readers and NO writer in the corpus: it
	// reads 0, and its runtime value is UNRECOVERED.
	void SmokeIndexed(const FVector& PosUnits, float Scale, float Framerate,
		FElysiumEntityWorld* World, IElysiumRetailSiteSink* Sink);

	// `FUN_101cfb30(pos, dir, color, amount)`: gate, `-1`, `amount == 0`, the `DAT_1070ba34` remap, the
	// rules' `IsMultiplayer` x5, the 255 clamp, the mechanical arm (colour 0x14: `Sparks(pos, 1, 1,
	// NULL)`, a `RandomFloat(0, 2) < 1` early-out, `RandomInt(10, 15)` -> `FUN_101cf640(pos, n, 10.0f)`),
	// else the colour triple, the PVS filter, `clamp(amount / 10, 3, 16)` and `CTempEntsSystem` slot 14
	// -- whose body `FUN_1005df40` is a bare `ret`: the spray emits nothing in retail. `PosUnits` /
	// `DirUnits` are Source units.
	void Drips(const FVector& PosUnits, const FVector& DirUnits, uint32 Color, int32 Amount,
		FElysiumEntityWorld* World, IElysiumRetailSiteSink* Sink);

	// `FUN_102699e0(pos, color, damage)`: `amount = __ftol(damage)` (truncation toward zero), `dir =
	// &DAT_1070ba40` (the attack direction the `OnTakeDamage` family writes -- four bodies; this port's
	// `NpcKernelDamageShared::GDeathThrowImpulse`), then `Drips`.
	void SpawnBlood(const FVector& PosUnits, uint32 Color, float Damage,
		FElysiumEntityWorld* World, IElysiumRetailSiteSink* Sink);
}
