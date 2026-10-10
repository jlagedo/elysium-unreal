// The blood effect chain, `walks/L0-r013.md` + `walks/L0-r014.md`: `FUN_102699e0` -> `FUN_101cfb30` ->
// `FUN_101cf9b0`, and the `CEffectsServer` temp entities the mechanical arm sends.

#include "Substrate/ElysiumBloodEffects.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumRetailSite.h"
#include "ElysiumRng.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumNpcDamageShared.h"
#include "HAL/IConsoleManager.h"

// The two engine cvars the gate reads (`engine.dll` `staticinit_200ed990` / `staticinit_200ed9d0`:
// `FUN_20040490("violence_hblood", "1", 0, "Draw human blood")` / `("violence_ablood", "1", 0, "Draw
// alien blood")`; `DAT_20197438` is the shared default string "1"). Retail stores `atof` at `+0x28` and
// its `ftol` at `+0x2c`; the gate reads the int. The engine host init (`FUN_2008ec20`) re-sets all four
// violence cvars from the registry tokens "User Token 2" / "User Token 3" (both empty -> 1); a stock
// install admits blood, which is the default carried here.
static TAutoConsoleVariable<int32> CVarViolenceHBlood(TEXT("violence_hblood"), 1, TEXT("Draw human blood"), ECVF_Default);
static TAutoConsoleVariable<int32> CVarViolenceABlood(TEXT("violence_ablood"), 1, TEXT("Draw alien blood"), ECVF_Default);

namespace
{
	constexpr uint32 GDontBleed = 0xFFFFFFFFu;    // -1, DONT_BLEED
	constexpr uint32 GBloodRed = 0xF7u;           // 247, the human colour (`violence_hblood`)
	constexpr uint32 GBloodMech = 0x14u;          // 20, BLOOD_COLOR_MECH
	constexpr uint32 GBloodYellow = 0xC3u;        // 195
	constexpr int32 GAmountCap = 0xff;
	constexpr float GOne = 1.0f;                  // `_DAT_104454c0` = 0x3f800000
	constexpr float GSmokeFramerate = 10.0f;      // the `0x41200000` word left for `FUN_101cf640`
	constexpr float GSmokeScale = 0.1f;           // `_DAT_104491b4` = 0x3dcccccd, `CEffectsServer::vfunc2`
	// `DAT_1070ba34`: BSS, read by this function, two gib helpers and `CBloodSplat::Remove`, written by
	// NOTHING in the corpus -- it reads 0, so the 247 -> 0 remap never fires in shipped code.
	constexpr int32 GBloodRemapFlag = 0;
	// `DAT_1070ba0c->vslot21()`: `g_pGameRules`, built by `CWorld::Precache` 0x1023c020 through the
	// factory `FUN_10352f70` as `CHalfLife2` (vtable 0x104a308c) when `gpGlobals+0x30 == 0`, whose slot 21
	// `FUN_101abcc0` is `XOR AL,AL; RET` (IsMultiplayer). The x5 and the (255, 32, 32) triple are dead.
	constexpr int32 GRulesIsMultiplayer = 0;
	// `DAT_1088ae52`, the smoke sprite's model index (`MOVSX` of a 16-bit word): 12 readers (this wrapper
	// and the TE Test methods), no writer; 0 in the image. Its runtime value is UNRECOVERED.
	constexpr int16 GSmokeModelIndex = 0;
	// `[DAT_1070b228+0x14]`, the client-slot count: 1 in single player, the only mode the game ships.
	constexpr int32 GMaxClients = 1;

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

	IElysiumEmbodiment* EmbodimentOf(FElysiumEntityWorld* World)
	{
		return World != nullptr ? World->Embodiment() : nullptr;
	}
}

bool ElysiumBlood::Admit(uint32 Color, IElysiumRetailSiteSink* Sink)
{
	// `FUN_101cf9b0` (asm 101cf9b0-101cfa1e), arms in retail order. The answer is AL only.
	// 1. `color == 0xFFFFFFFF` (`CMP EAX,-1` @101cf9b5) -> 0. No cvar read.
	if (Color == GDontBleed)
	{
		Emit(Sink, TEXT("blood.gate"), TEXT("FUN_101cf9b0"), 0x101cf9b0u, TEXT("return"),
			FString::Printf(TEXT("color=%d cvar=none cvar_int=0 admit=0"), static_cast<int32>(Color)));
		return false;
	}
	// 2. `color == 0xF7` -> "violence_hblood" (0x105a03bc); 3. otherwise "violence_ablood" (0x105a03a8).
	//    `ref = VEngineCvar001.FindVar(name)` (engine `CCvar::vfunc2` 0x20043920: a case-insensitive walk
	//    of the cvar list, NULL for a command or an unknown name) -> 0. `obj = *(ref+4)` is the ConVar's
	//    `m_pParent` (= itself for a root cvar); admit iff `obj->vslot1()` (IsCommand, engine 0x20040640,
	//    false for a ConVar) is false AND `*(int*)(obj+0x2c) != 0`, the cvar's `m_nValue`. Any non-zero
	//    integer admits, not only 1.
	const TCHAR* Name = Color == GBloodRed ? TEXT("violence_hblood") : TEXT("violence_ablood");
	const IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name);
	const int32 Value = Var ? Var->GetInt() : 0;
	const bool bAdmit = Var != nullptr && Value != 0;
	Emit(Sink, TEXT("blood.gate"), TEXT("FUN_101cf9b0"), 0x101cf9b0u, TEXT("return"),
		FString::Printf(TEXT("color=%d cvar=%s cvar_int=%d admit=%d"), static_cast<int32>(Color), Name, Value, bAdmit ? 1 : 0));
	return bAdmit;
}

int32 ElysiumBlood::PvsFilterRecipients(const FVector& PosUnits, FElysiumEntityWorld* World, IElysiumRetailSiteSink* Sink)
{
	// `FUN_1019ce00` (the `CRecipientFilter` ctor: an empty `CUtlVector<int>` of recipients, the
	// `FUN_1019ced0` flag reset) with its vftable swapped for `CPVSFilter`'s (0x10455e54), then
	// `FUN_1019d210(pos)`: `[DAT_1070b228+0x14] == 1` -> `FUN_1019cff0`, which clears the list and adds
	// every slot 1..maxclients that `FUN_101cd9e0` resolves to a live player (`VEngineServer014` slot 38
	// edict-by-index, not free, with a server class); otherwise `VEngineServer014` slot 64 answers the
	// PVS mask of `pos` and `FUN_1019d1c0` adds the players of its set bits. The game ships single
	// player, so the PVS of `pos` is never consulted: the recipient is the seated player, whoever is
	// in view. The engine transport behind the list is replaced by the port's one process.
	const int32 Recipients = (World != nullptr && World->FindPlayer() != nullptr) ? 1 : 0;
	Emit(Sink, TEXT("blood.filter"), TEXT("FUN_1019d210"), 0x1019d210u, TEXT("branch"),
		FString::Printf(TEXT("pos=%s maxclients=%d arm=all_players recipients=%d"), *Vec(PosUnits), GMaxClients, Recipients));
	return Recipients;
}

void ElysiumBlood::Sparks(const FVector& PosUnits, int32 Magnitude, int32 TrailLength, const FVector* DirUnits,
	FElysiumEntityWorld* World, IElysiumRetailSiteSink* Sink)
{
	// `CEffectsServer::vfunc3` 0x100f6050 (`PTR_DAT_10566258` -> the static `DAT_106eb584`, ctor
	// `FUN_100f5b20`), in order:
	// 1. `CPVSFilter filter(origin)` (0x1019ce00, 0x1019d210).
	const int32 Recipients = PvsFilterRecipients(PosUnits, World, Sink);
	// 2. `if (this+0x10 <= 0 && this+0xc != 0)`: the suppress-host test (`FUN_1019d470` reads the
	//    filter's `+0x29` byte, `FUN_1019d130` removes the host's slot, then the filter's vslot2 count).
	//    `DAT_106eb590` / `DAT_106eb594` are written by the ctor alone (0), so the arm never opens.
	// 3. `CTempEntsSystem` slot 35 (+0x8c) 0x10059860 `(filter, 0.0 delay, origin, magnitude, trail,
	//    dir)`: its own copy of the suppress test (`DAT_106be9f0`, also ctor-only), then `FUN_10067b70`
	//    writes the `CTESparks` statics -- a NULL `dir` reads `vec3_origin` `DAT_1070d1b0..b8` -- and
	//    `CTESparks::vfunc4` 0x100677e0 (Create) hands the entity to `VEngineServer014` slot 103
	//    (+0x19c) with its server class: the networked temp entity. The client's draw is UNRECOVERED;
	//    the embodiment stands its own burst (`EmitTempEntitySparks`, a named visual modernization).
	const FVector Dir = DirUnits != nullptr ? *DirUnits : FVector::ZeroVector;
	FElysiumEffectHandle Drawn;
	if (IElysiumEmbodiment* Embodiment = EmbodimentOf(World))
	{
		Drawn = Embodiment->EmitTempEntitySparks(PosUnits * ElysiumMove::U, Magnitude, TrailLength, Dir);
	}
	Emit(Sink, TEXT("blood.sparks"), TEXT("CEffectsServer::vfunc3"), 0x100f6050u, TEXT("emit"),
		FString::Printf(TEXT("pos=%s magnitude=%d trail=%d dir=%s recipients=%d te=CTempEntsSystem::vfunc35 te_va=0x10059860 create=CTESparks::vfunc4 create_va=0x100677e0 drawn=%d"),
			*Vec(PosUnits), Magnitude, TrailLength, *Vec(Dir), Recipients, Drawn.IsValid() ? 1 : 0));
}

void ElysiumBlood::Smoke(const FVector& PosUnits, int32 ModelIndex, float Scale, float Framerate,
	FElysiumEntityWorld* World, IElysiumRetailSiteSink* Sink)
{
	// `CEffectsServer::vfunc2` 0x100f5e80, in order:
	// 1. `CPVSFilter filter(origin)`.
	const int32 Recipients = PvsFilterRecipients(PosUnits, World, Sink);
	// 2. The suppress-host test, dead as in `Sparks`.
	// 3. `CTempEntsSystem` slot 34 (+0x88) 0x100597d0 `(filter, 0.0, origin, modelIndex, scale *
	//    _DAT_104491b4, _ftol(framerate))`: 0.1f, and the framerate truncated toward zero. Then
	//    `FUN_100675b0` writes the `CTESmoke` statics and `CTESmoke::vfunc4` 0x10067200 (Create) sends.
	const float TeScale = Scale * GSmokeScale;
	const int32 TeFramerate = static_cast<int32>(Framerate);
	FElysiumEffectHandle Drawn;
	if (IElysiumEmbodiment* Embodiment = EmbodimentOf(World))
	{
		Drawn = Embodiment->EmitTempEntitySmoke(PosUnits * ElysiumMove::U, ModelIndex, TeScale, TeFramerate);
	}
	Emit(Sink, TEXT("blood.smoke"), TEXT("CEffectsServer::vfunc2"), 0x100f5e80u, TEXT("emit"),
		FString::Printf(TEXT("pos=%s model_index=%d scale_in=%g scale_te=%g framerate_in=%g framerate=%d recipients=%d te=CTempEntsSystem::vfunc34 te_va=0x100597d0 create=CTESmoke::vfunc4 create_va=0x10067200 drawn=%d"),
			*Vec(PosUnits), ModelIndex, Scale, TeScale, Framerate, TeFramerate, Recipients, Drawn.IsValid() ? 1 : 0));
}

void ElysiumBlood::SmokeIndexed(const FVector& PosUnits, float Scale, float Framerate,
	FElysiumEntityWorld* World, IElysiumRetailSiteSink* Sink)
{
	// `FUN_101cf640` 0x101cf640: `PTR_DAT_10566258->vslot2(pos, (int)(int16)DAT_1088ae52, scale, framerate)`
	// (`MOVSX EDX, word ptr [0x1088ae52]` @101cf652). Its one caller is the dispatcher's mechanical arm.
	Emit(Sink, TEXT("blood.smoke"), TEXT("FUN_101cf640"), 0x101cf640u, TEXT("entry"),
		FString::Printf(TEXT("pos=%s model_index=%d model_index_writer=none scale=%g framerate=%g"), *Vec(PosUnits), GSmokeModelIndex, Scale, Framerate));
	Smoke(PosUnits, GSmokeModelIndex, Scale, Framerate, World, Sink);
}

void ElysiumBlood::Drips(const FVector& PosUnits, const FVector& DirUnits, uint32 Color, int32 Amount,
	FElysiumEntityWorld* World, IElysiumRetailSiteSink* Sink)
{
	// `FUN_101cfb30(pos, dir, color, amount)` (asm 101cfb30-101cfcfd), arms in retail order.
	Emit(Sink, TEXT("blood.dispatch"), TEXT("FUN_101cfb30"), 0x101cfb30u, TEXT("entry"),
		FString::Printf(TEXT("color=%d amount=%d pos=%s dir=%s"), static_cast<int32>(Color), Amount, *Vec(PosUnits), *Vec(DirUnits)));
	// 1. `admit = gate(color)` (`CALL 0x100158a2` @101cfb3a, `TEST AL,AL`); 0 -> return (@101cfb44).
	if (!Admit(Color, Sink))
	{
		Emit(Sink, TEXT("blood.dispatch"), TEXT("FUN_101cfb30"), 0x101cfb30u, TEXT("branch"), TEXT("arm=refused_gate"));
		return;
	}
	// 2. `color == 0xFFFFFFFF` (@101cfb4a) -> return (@101cfb4d). Dead after the gate, kept in retail's order.
	if (Color == GDontBleed)
	{
		Emit(Sink, TEXT("blood.dispatch"), TEXT("FUN_101cfb30"), 0x101cfb30u, TEXT("branch"), TEXT("arm=refused_color"));
		return;
	}
	// 3. `amount == 0` (`TEST ESI,ESI` @101cfb57) -> return (@101cfb59). An EQUALITY test: a negative
	//    amount passes. Reachable only through the callers without the 1.0 floor (`CBaseEntity::TraceAttack`
	//    0x100a7de0, `CBasePlayer::TraceAttack` 0x10162c30, `FUN_10162fa0`); the NPC route truncates >= 1.
	if (Amount == 0)
	{
		Emit(Sink, TEXT("blood.dispatch"), TEXT("FUN_101cfb30"), 0x101cfb30u, TEXT("branch"), TEXT("arm=refused_amount"));
		return;
	}
	// 4. `DAT_1070ba34 == 1 && color == 0xF7` -> `color = 0` (@101cfb5f-70). The flag has no writer (reads 0).
	if (GBloodRemapFlag == 1 && Color == GBloodRed)
	{
		Color = 0;
	}
	// 5. `g_pGameRules->vslot21()` (@101cfb7a, IsMultiplayer; `CHalfLife2` answers 0) -> `amount *= 5`
	//    (`LEA ECX,[ESI+ESI*4]` @101cfb81).
	if (GRulesIsMultiplayer != 0)
	{
		Amount *= 5;
	}
	// 6. `amount > 0xFF` (signed, `CMP ESI,0xff` @101cfb8a) -> `amount = 0xFF` (@101cfb92).
	if (Amount > GAmountCap)
	{
		Amount = GAmountCap;
	}
	// 7. `color == 0x14` (`CMP EDI,0x14` @101cfb9a): the mechanical arm, which always returns from inside.
	if (Color == GBloodMech)
	{
		// (a) `PTR_DAT_10566258->vslot3(pos, 1, 1, NULL)` (@101cfbb2): `CEffectsServer::vfunc3` Sparks.
		Sparks(PosUnits, 1, 1, nullptr, World, Sink);
		// (b) `r = VEngineRandom001.vslot1(0.0f, 2.0f)` (@101cfbc4; engine 0x20019370 `RET 8` -> VSTDLIB
		//     `RandomFloat`); `FCOMP [0x104454c0]` (1.0f): `r < 1.0f` -> return (@101cfbd4). About half.
		const float R = ElysiumRng::Stream(EElysiumRngStream::Effects).FRandRange(0.0f, 2.0f);
		if (R < GOne)
		{
			Emit(Sink, TEXT("blood.mech"), TEXT("FUN_101cfb30"), 0x101cfb30u, TEXT("branch"),
				FString::Printf(TEXT("arm=mech color_after_remap=%d amount_after=%d sparks=1 rand=%g second=0"), static_cast<int32>(Color), Amount, R));
			return;
		}
		// (c) `n = VEngineRandom001.vslot2(10, 15)` (@101cfbeb; engine 0x20019390 `RET 8` -> VSTDLIB
		//     `RandomInt`, INCLUSIVE: `lo + n mod (hi - lo + 1)`); the `10.0f` word pushed first
		//     (@101cfbe0) is not the callee's: `RET 8` leaves it on the stack for (d). `n` overwrites the
		//     `amount` slot (@101cfbee).
		const int32 N = ElysiumRng::Stream(EElysiumRngStream::Effects).RandRange(10, 15);
		Emit(Sink, TEXT("blood.mech"), TEXT("FUN_101cfb30"), 0x101cfb30u, TEXT("branch"),
			FString::Printf(TEXT("arm=mech color_after_remap=%d amount_after=%d sparks=1 rand=%g second=1 count=%d scale=%g framerate=%g"),
				static_cast<int32>(Color), Amount, R, N, static_cast<float>(N), GSmokeFramerate));
		// (d) `thunk_FUN_101cf640(pos, (float)n, 10.0f)` (@101cfbfb -> 0x10013223 -> 0x101cf640): the
		//     indexed Smoke, which scales by 0.1f and `_ftol`s the framerate before the TE. Return (@101cfc08).
		SmokeIndexed(PosUnits, static_cast<float>(N), GSmokeFramerate, World, Sink);
		return;
	}
	// 8. The colour triple (`ESI`, `EDI`, `EBX`, @101cfc09): 0xC3 -> (0x80, 0x80, 0) (@101cfc38-3f); else
	//    the rules' slot 21 again (@101cfc1a): 0 -> (0x40, 0, 0) (@101cfc2f-36); non-zero -> (0xFF, 0x20,
	//    0x20) (@101cfc21-2d), dead in single player.
	int32 R = 0x40, G = 0, B = 0;
	if (Color == GBloodYellow)
	{
		R = 0x80; G = 0x80; B = 0;
	}
	else if (GRulesIsMultiplayer != 0)
	{
		R = 0xFF; G = 0x20; B = 0x20;
	}
	// 9. `CPVSFilter filter(pos)` (ctor 0x1019ce00 @101cfc46, vftable 0x10455e54 @101cfc54,
	//    `AddRecipientsByPVS` 0x1019d210 @101cfc5c): the recipient set.
	const int32 Recipients = PvsFilterRecipients(PosUnits, World, Sink);
	// 10. `nAmount = amount / 10` (signed, the 0x66666667 magic multiply @101cfc61-72), then `< 3` -> 3,
	//     `>= 16` -> 16 (@101cfc76-8c). A negative amount gives 3.
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
		FString::Printf(TEXT("arm=spray color_after_remap=%d amount_after=%d rgb=%d,%d,%d nAmount=%d recipients=%d"), static_cast<int32>(Color), Amount, R, G, B, NAmount, Recipients));
	// 11. `PTR_DAT_10540528->vslot14(&filter, 0.0f, pos, dir, r, g, b, 0xFF, nAmount)` (`CALL [EDX+0x38]`
	//     @101cfcb0, `CTempEntsSystem::vfunc14` 0x10058ba0, nine arguments): the (dead) suppress test, then
	//     `thunk_FUN_1005df40` (0x100038fa) -> `FUN_1005df40`, a bare `RET` (bytes c3 90 90 90). This is the
	//     BloodSprite temp entity with its body removed: the spray EMITS NOTHING in retail (slot 13
	//     0x10058b00 is the real `CTEBloodStream`, reached only from the dead `FUN_101cfa40`). Nothing is
	//     drawn here either.
	Emit(Sink, TEXT("blood.dispatch"), TEXT("FUN_101cfb30"), 0x101cfb30u, TEXT("emit"),
		FString::Printf(TEXT("fn=CTempEntsSystem::vfunc14 va=0x10058ba0 body=FUN_1005df40 ret delay=0 rgb=%d,%d,%d a=255 nAmount=%d drawn=0"), R, G, B, NAmount));
	// 12. The filter's `CUtlVector` purge (@101cfcb3-cf5): `Plat_Free` on the recipient buffer when the
	//     grow size `+0x10 != -1` and the buffer `+8 != 0`; the second guard re-tests with 0 and never
	//     frees. No game state. 13. Return void (@101cfcf8).
}

void ElysiumBlood::SpawnBlood(const FVector& PosUnits, uint32 Color, float Damage,
	FElysiumEntityWorld* World, IElysiumRetailSiteSink* Sink)
{
	// `FUN_102699e0(pos, color, damage)` 0x102699e0 (cdecl, the Vector by value): `amount = __ftol(damage)`
	// (CRT `_ftol` 0x10431320, truncation toward zero) and `dir = &DAT_1070ba40`, the FILE-STATIC attack
	// direction the `OnTakeDamage` family writes (`CBaseEntity::OnTakeDamage` 0x100a0e40,
	// `CBaseCombatCharacter::OnTakeDamage_Alive` 0x103302e0, `CBreakable::OnTakeDamage` 0x1010e100,
	// `CAI_BaseNPC::OnTakeDamage_Dead` 0x102664c0); a non-fatal hit passes whatever the last one left.
	// The colour is the victim's slot 145 `BloodColor()` (`CBaseCombatCharacter` 0x1014fa10 reads
	// `m_bloodColor` +0x1570; `CBaseEntity` 0x10026d90 answers -1).
	const int32 Amount = static_cast<int32>(Damage);
	const FVector& Dir = NpcKernelDamageShared::GDeathThrowImpulse;
	Emit(Sink, TEXT("blood.spawn"), TEXT("FUN_102699e0"), 0x102699e0u, TEXT("entry"),
		FString::Printf(TEXT("color=%d damage=%g int_amount=%d dir=%s"), static_cast<int32>(Color), Damage, Amount, *Vec(Dir)));
	Drips(PosUnits, Dir, Color, Amount, World, Sink);
}
