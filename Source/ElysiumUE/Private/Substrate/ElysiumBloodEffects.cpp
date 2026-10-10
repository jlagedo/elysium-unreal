// The blood effect chain, `walks/L0-r013.md`: `FUN_102699e0` -> `FUN_101cfb30` -> `FUN_101cf9b0`.

#include "Substrate/ElysiumBloodEffects.h"

#include "ElysiumRetailSite.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpcDamageShared.h"
#include "HAL/IConsoleManager.h"

// The two engine cvars the gate reads (`engine.dll` `staticinit_200ed990` / `staticinit_200ed9d0`:
// `FUN_20040490("violence_hblood", "1", 0, "Draw human blood")` / `("violence_ablood", "1", 0, "Draw
// alien blood")`; `DAT_20197438` is the shared default string "1"). Retail stores `atof` at `+0x28` and
// its `ftol` at `+0x2c`; the gate reads the int.
static TAutoConsoleVariable<int32> CVarViolenceHBlood(TEXT("violence_hblood"), 1, TEXT("Draw human blood"), ECVF_Default);
static TAutoConsoleVariable<int32> CVarViolenceABlood(TEXT("violence_ablood"), 1, TEXT("Draw alien blood"), ECVF_Default);

namespace
{
	constexpr uint32 GDontBleed = 0xFFFFFFFFu;    // -1, DONT_BLEED
	constexpr uint32 GBloodRed = 0xF7u;           // 247, the human colour (`violence_hblood`)
	constexpr uint32 GBloodMech = 0x14u;          // 20, BLOOD_COLOR_MECH
	constexpr uint32 GBloodYellow = 0xC3u;        // 195
	constexpr int32 GAmountCap = 0xff;
	constexpr float GOne = 1.0f;                  // `_DAT_104454c0`
	constexpr float GSmokeFramerate = 10.0f;      // the `0x41200000` word left for `FUN_101cf640`
	// `DAT_1070ba34`: BSS, read by this function, two gib helpers and `CBloodSplat::Remove`, written by
	// NOTHING in the corpus -- it reads 0, so the 247 -> 0 remap never fires in shipped code.
	constexpr int32 GBloodRemapFlag = 0;
	// `DAT_1070ba0c->vslot21()`: the game-rules object `CWorld::Precache` builds is `CHalfLife2` in
	// single player (factory 0x100107bc, `gpGlobals+0x30 == 0`), whose slot 21 `FUN_101abcc0` is
	// `return 0`. The x5 and the (255, 32, 32) triple are dead in single player.
	constexpr int32 GRulesIsMultiplayer = 0;

	void Emit(IElysiumRetailSiteSink* Sink, const TCHAR* Tag, const TCHAR* Fn, uint32 Va, const TCHAR* Phase, const FString& Payload)
	{
		if (Sink)
		{
			Sink->Site(Tag, Fn, Va, Phase, Payload);
		}
	}

	FString Vec(const FVector& V)
	{
		return FString::Printf(TEXT("%g,%g,%g"), V.X == 0.0 ? 0.0 : V.X, V.Y == 0.0 ? 0.0 : V.Y, V.Z == 0.0 ? 0.0 : V.Z);
	}
}

bool ElysiumBlood::Admit(uint32 Color, IElysiumRetailSiteSink* Sink)
{
	// `FUN_101cf9b0` (asm 101cf9b0-101cfa1e), arms in retail order.
	// 1. `color == 0xFFFFFFFF` -> 0.
	if (Color == GDontBleed)
	{
		Emit(Sink, TEXT("blood.gate"), TEXT("FUN_101cf9b0"), 0x101cf9b0u, TEXT("return"),
			FString::Printf(TEXT("color=%d cvar=none cvar_int=0 admit=0"), static_cast<int32>(Color)));
		return false;
	}
	// 2. `color == 0xF7` -> "violence_hblood" (0x105a03bc); 3. otherwise "violence_ablood" (0x105a03a8).
	//    `ref = VEngineCvar001.FindVar(name)` (engine `CCvar::vfunc2` 0x20043920, NULL for a command or
	//    an unknown name) -> 0. `obj = *(ref+4)` (the ConVar itself); admit iff `obj->vslot1()` ("is
	//    command") is false AND `*(int*)(obj+0x2c) != 0`, the cvar's int value.
	const TCHAR* Name = Color == GBloodRed ? TEXT("violence_hblood") : TEXT("violence_ablood");
	const IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name);
	const int32 Value = Var ? Var->GetInt() : 0;
	const bool bAdmit = Var != nullptr && Value != 0;
	Emit(Sink, TEXT("blood.gate"), TEXT("FUN_101cf9b0"), 0x101cf9b0u, TEXT("return"),
		FString::Printf(TEXT("color=%d cvar=%s cvar_int=%d admit=%d"), static_cast<int32>(Color), Name, Value, bAdmit ? 1 : 0));
	return bAdmit;
}

void ElysiumBlood::Drips(const FVector& PosUnits, const FVector& DirUnits, uint32 Color, int32 Amount, IElysiumRetailSiteSink* Sink)
{
	// `FUN_101cfb30(pos, dir, color, amount)` (asm 101cfb30-101cfcfd), arms in retail order.
	Emit(Sink, TEXT("blood.dispatch"), TEXT("FUN_101cfb30"), 0x101cfb30u, TEXT("entry"),
		FString::Printf(TEXT("color=%d amount=%d pos=%s dir=%s"), static_cast<int32>(Color), Amount, *Vec(PosUnits), *Vec(DirUnits)));
	// 1. `admit = gate(color)`; 0 -> return.
	if (!Admit(Color, Sink))
	{
		Emit(Sink, TEXT("blood.dispatch"), TEXT("FUN_101cfb30"), 0x101cfb30u, TEXT("branch"), TEXT("arm=refused_gate"));
		return;
	}
	// 2. `color == 0xFFFFFFFF` -> return (dead after the gate, kept in retail's order).
	if (Color == GDontBleed)
	{
		Emit(Sink, TEXT("blood.dispatch"), TEXT("FUN_101cfb30"), 0x101cfb30u, TEXT("branch"), TEXT("arm=refused_color"));
		return;
	}
	// 3. `amount == 0` -> return. An EQUALITY test (`TEST ESI,ESI`): a negative amount passes.
	if (Amount == 0)
	{
		Emit(Sink, TEXT("blood.dispatch"), TEXT("FUN_101cfb30"), 0x101cfb30u, TEXT("branch"), TEXT("arm=refused_amount"));
		return;
	}
	// 4. `DAT_1070ba34 == 1 && color == 0xF7` -> `color = 0`. The flag has no writer (reads 0).
	if (GBloodRemapFlag == 1 && Color == GBloodRed)
	{
		Color = 0;
	}
	// 5. `g_pGameRules->vslot21()` (IsMultiplayer; `CHalfLife2` answers 0) -> `amount *= 5`.
	if (GRulesIsMultiplayer != 0)
	{
		Amount *= 5;
	}
	// 6. `amount > 0xFF` (signed) -> `amount = 0xFF`.
	if (Amount > GAmountCap)
	{
		Amount = GAmountCap;
	}
	// 7. `color == 0x14` (mechanical).
	if (Color == GBloodMech)
	{
		// (a) `PTR_DAT_10566258->vslot3(pos, 1, 1, NULL)`: the effects interface, the shape of
		//     `IEffects::Sparks(pos, 1, 1, NULL)` -- a client temp entity whose retail draw is
		//     UNRECOVERED (client.dll unread); no Unreal visual stands for it yet.
		// (b) `r = VEngineRandom001.vslot1(0.0f, 2.0f)` (`RET 8`, a float draw); `r < 1.0f` -> return.
		const float R = ElysiumRng::Stream(EElysiumRngStream::Effects).FRandRange(0.0f, 2.0f);
		if (R < GOne)
		{
			Emit(Sink, TEXT("blood.mech"), TEXT("FUN_101cfb30"), 0x101cfb30u, TEXT("branch"),
				FString::Printf(TEXT("arm=mech color_after_remap=%d amount_after=%d sparks=1 rand=%g second=0"), static_cast<int32>(Color), Amount, R));
			return;
		}
		// (c) `n = VEngineRandom001.vslot2(10, 15)` (`RET 8`, an int draw); the `10.0f` word pushed first
		//     is not the callee's: `RET 8` leaves it for (d).
		const int32 N = ElysiumRng::Stream(EElysiumRngStream::Effects).RandRange(10, 15);
		// (d) `FUN_101cf640(pos, (float)n, 10.0f)` -> `PTR_DAT_10566258->vslot2(pos, (int16)DAT_1088ae52,
		//     (float)n, 10.0f)`: the shape of `IEffects::Smoke(pos, modelIndex, scale, framerate)`.
		//     `DAT_1088ae52` (a 16-bit model index, `MOVSX`) has no writer in the corpus: UNRECOVERED.
		Emit(Sink, TEXT("blood.mech"), TEXT("FUN_101cfb30"), 0x101cfb30u, TEXT("branch"),
			FString::Printf(TEXT("arm=mech color_after_remap=%d amount_after=%d sparks=1 rand=%g second=1 count=%d scale=%g framerate=%g model_index=unrecovered"),
				static_cast<int32>(Color), Amount, R, N, static_cast<float>(N), GSmokeFramerate));
		return;
	}
	// 8. The colour triple (`ESI`, `EDI`, `EBX`): 0xC3 -> (0x80, 0x80, 0); else the rules' slot 21 again:
	//    0 -> (0x40, 0, 0); non-zero -> (0xFF, 0x20, 0x20), dead in single player.
	int32 R = 0x40, G = 0, B = 0;
	if (Color == GBloodYellow)
	{
		R = 0x80; G = 0x80; B = 0;
	}
	else if (GRulesIsMultiplayer != 0)
	{
		R = 0xFF; G = 0x20; B = 0x20;
	}
	// 9. `CPVSFilter(pos)` (ctor 0x1019ce00, `AddRecipientsByPVS` 0x1019d210): the recipient set.
	// 10. `nAmount = clamp(amount / 10, 3, 16)`: a signed divide truncating toward zero; below 3 is 3
	//     (so a negative amount gives 3), at or above 16 is 16.
	int32 NAmount = Amount / 10;
	if (NAmount < 3)
	{
		NAmount = 3;
	}
	else if (NAmount >= 16)
	{
		NAmount = 16;
	}
	Emit(Sink, TEXT("blood.dispatch"), TEXT("FUN_101cfb30"), 0x101cfb30u, TEXT("branch"),
		FString::Printf(TEXT("arm=spray color_after_remap=%d amount_after=%d rgb=%d,%d,%d nAmount=%d"), static_cast<int32>(Color), Amount, R, G, B, NAmount));
	// 11. `PTR_DAT_10540528->vslot14(&filter, 0.0f, pos, dir, r, g, b, 0xFF, nAmount)` (`CALL [EDX+0x38]`,
	//     `CTempEntsSystem::vfunc14` 0x10058ba0): the recipient check, then `thunk_FUN_1005df40` ->
	//     `FUN_1005df40`, a bare `ret`. The spray EMITS NOTHING in retail (slot 13, 0x10058b00, is the
	//     `CTEBloodStream` body this call does not reach). Nothing is drawn here either.
	Emit(Sink, TEXT("blood.dispatch"), TEXT("FUN_101cfb30"), 0x101cfb30u, TEXT("emit"),
		FString::Printf(TEXT("fn=CTempEntsSystem::vfunc14 va=0x10058ba0 body=FUN_1005df40 ret delay=0 rgb=%d,%d,%d a=255 nAmount=%d drawn=0"), R, G, B, NAmount));
	// 12. `Plat_Free` on the filter's recipient buffer. No game state.
}

void ElysiumBlood::SpawnBlood(const FVector& PosUnits, uint32 Color, float Damage, IElysiumRetailSiteSink* Sink)
{
	// `FUN_102699e0(pos, color, damage)`: `amount = __ftol(damage)` -- truncation toward zero -- and
	// `dir = &DAT_1070ba40`, the FILE-STATIC attack direction the `OnTakeDamage` family writes
	// (`CBaseEntity::OnTakeDamage` 0x100a0e40, `CAI_BaseNPC::OnTakeDamage_Dead` 0x102664c0, ...).
	const int32 Amount = static_cast<int32>(Damage);
	const FVector& Dir = NpcKernelDamageShared::GDeathThrowImpulse;
	Emit(Sink, TEXT("blood.spawn"), TEXT("FUN_102699e0"), 0x102699e0u, TEXT("entry"),
		FString::Printf(TEXT("color=%d damage=%g int_amount=%d dir=%s"), static_cast<int32>(Color), Damage, Amount, *Vec(Dir)));
	Drips(PosUnits, Dir, Color, Amount, Sink);
}
