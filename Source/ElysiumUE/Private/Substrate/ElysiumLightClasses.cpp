// light / light_spot / light_dynamic -- the light entities.
//
// `light` and `light_spot` are CLight (vampire.dll; walked in `docs/specs/layers/L0-entity/walks/
// L0-r012.md`): KeyValue `0x101303c0`, Spawn `0x10130460`, Use `0x10130580`, Kill `0x101306d0`,
// InputSetPattern `0x10130780`, InputFadeToPattern `0x10130800`, with the helpers TurnOn `0x10130610`,
// TurnOff `0x10130690`, Toggle `0x101306f0`, FadeThink `0x101308d0`, ScriptHide `0x101305d0` and
// ScriptUnhide `0x101305f0`. The entity owns no light of its own: VRAD baked every named light's
// sources into lump 15 under the style it assigned (>= 32), and the bake tagged those sources
// `elysium.style=<s>`, so the whole of the leaf's job is the engine's `LightStyle(style, pattern)`
// (`VEngineServer014` slot 62, engine.dll `0x201096d0`: stores the pattern pointer, sends message
// `0x0c`) -- one write per call, by style, through `IElysiumEmbodiment::SetLightStylePattern` to the
// rig's clock. Retail gates on style 32 in exactly two places, Spawn and Use; the inputs and the
// helpers publish whatever the style is.
//
// `light_dynamic` is CDynamicLight (100568a0 KeyValue, 10056a90 Spawn, 10056a00 TurnOn): the one
// light with no lump-15 row. It stands a runtime point/spot through the legacy `ApplyToSource`
// derivation (`BuildDynamicLight`) and toggles it by visibility. Its magnitude convention is the
// doc's; nothing here is a knob.

#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumRetailSite.h"           // the `light_*` taps
#include "ElysiumSaveArchive.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumClassFields.h"

#include "Components/LightComponent.h"

namespace
{
	constexpr int32 SF_LIGHT_START_OFF = 0x1;   // m_spawnflags +0x204 bit 0: the on/off memory
	// `0x10453b74`, the float 0.05 `CLight::Spawn` floors `fade_time` against (asm `0x1013047e`:
	// `FCOMP`, then the constant when fade_time <= 0.05 or NaN). A literal in `.rdata`, not a cvar.
	constexpr float LightFadeFloor = 0.05f;
	// The signed `CMP 0x20` of Spawn (`0x101304aa`) and Use (`0x10130583`): VRAD's first
	// entity-switched style. Nothing else in CLight tests it.
	constexpr int32 FirstSwitchedStyle = 32;
	// `.rdata` strings: `0x10540a3c` "a" (dark), `0x105758c4` "m" (full), `0x106b8540` "" (the NULL
	// fallback every reader of m_iszPattern substitutes).
	const TCHAR* const PatternDark = TEXT("a");
	const TCHAR* const PatternFull = TEXT("m");

	// `m_iszPattern` is a `char*`: NULL when unset (an absent key, or `pattern ""` through
	// `AllocPooledString` `0x1042bff0`, or a non-string input), else the text. Every retail reader maps
	// NULL to "" (TurnOn, FadeThink, SetPattern's publish, FadeToPattern's first byte), and Spawn's
	// NULL test runs before any writer, so one `FString` carries both words here. The first byte,
	// as the signed `char` FadeThink compares.
	int8 FirstByte(const FString& Text)
	{
		return Text.IsEmpty() ? 0 : static_cast<int8>(static_cast<uint8>(Text[0]));
	}
}

class FElysiumLight final : public FElysiumEntity
{
public:
	int32 Style = 0;          // m_iStyle +0x450, `style` (row 2): VRAD's assignment
	FString Pattern;          // m_iszPattern +0x454, `pattern` (row 3); see FirstByte for the NULL word
	float FadeTime = 0.f;     // m_flFadeTime +0x45c, `fade_time` (row 4): seconds per FadeThink step

	// `CLight::vfunc110` `0x101303c0` arm 1 (`strcmpi(key, "pitch")`, `0x101303d1`), the slot-110
	// KeyValue override. Retail runs it per key from `CBaseEntity::ParseMapData` `0x1009e280` in the
	// lump's key order; the port's key pass is `Construct`, which applies the class datamap rows in the
	// def's key order, so this row runs there, in sequence with the base `angles` row. Every light of
	// sp_tutorial_1 (211) and sm_hub_1 (375) that authors `pitch` authors `angles` before it, so the
	// pitch is what stands.
	//   a. `GetAngles` (slot 221 `0x100b3110`, the LOCAL word +0x428) -> {X, Y, Z};
	//   b. `X = (float)atof(value)` (`0x10130400`); Y and Z keep theirs;
	//   c. `SetAbsAngles` (slot 218 `0x100b2510`) writes m_angAbsRotation +0x410 and derives the local
	//      word -- at the key pass no parent is linked yet (`MapEntity_ParseAllEntities` `0x10136650`
	//      parents after every row is parsed), so the two words are one and `Angles` is both;
	//   d. return 1 (`MOV AL, 1` at `0x1013041c`).
	// Any other key is the base `CBaseEntity::KeyValue` `0x1009e430`: the datamap rows, which the
	// key pass applies on its own.
	void KeyValuePitch(const FString& Value)
	{
		const FVector Local = LocalAnglesWord();
		Angles = FVector(FCString::Atof(*Value), Local.Y, Local.Z);
		// The key pass runs before the entity has a world to report through; `Spawn` reports the
		// `light_keyvalue` return for it, the next thing retail does to the entity.
		bPitchKeyed = true;
		PitchKeyText = Value;
	}

	// `CLight::Spawn` `0x10130460` (slot 103), the arms in retail's order.
	virtual void Spawn() override
	{
		if (bPitchKeyed)
		{
			Site(TEXT("light_keyvalue"), TEXT("CLight::vfunc110"), 0x101303c0u, TEXT("return"),
				FString::Printf(TEXT("key=pitch value=%s angles=[%g %g %g] result=1"), *PitchKeyText,
					Angles.X, Angles.Y, Angles.Z));
			bPitchKeyed = false;
		}
		// 1. `m_iName == 0` (`0x10130463`): `UTIL_RemoveImmediate(this)` (`0x1000e255` -> `0x101cd970`)
		//    and return -- no floor, no publish, no pattern write. `hooks.tsv:297` lists the call as
		//    the L0 -> L4 hook of this function; its body is the immediate removal (KILLME `+0x268`
		//    bit 0, `UpdateOnRemove` slot 180, the deleting destructor slot 5) with no NPC logic in it,
		//    and the port's stand-in for `0x101cd970` is the base `Kill` everywhere it is reached
		//    (`CAmbientGeneric::Spawn`'s `0x1000e255`, `ElysiumNpcBosses.cpp`, `ElysiumInterestingPlace.cpp`).
		//    The BASE Kill, deliberately: `CLight::Kill` `0x101306d0` is the deferred `UTIL_Remove` with
		//    a TurnOff in front, and retail does not run it here.
		if (TargetName.IsEmpty())
		{
			Site(TEXT("light_spawn"), TEXT("CLight::Spawn"), 0x10130460u, TEXT("branch"),
				FString::Printf(TEXT("arm=name_zero hook=0x1000e255 fn=UTIL_RemoveImmediate va=0x101cd970 style=%d"), Style));
			FElysiumEntity::Kill();
			return;
		}
		// 2. `m_flFadeTime = max(0.05, m_flFadeTime)` (`0x10130478-0x101304a1`), named lights only,
		//    before the style test. The `FCOMP` takes the constant for a NaN too.
		FadeTime = (LightFadeFloor < FadeTime) ? FadeTime : LightFadeFloor;
		Site(TEXT("light_spawn"), TEXT("CLight::Spawn"), 0x10130460u, TEXT("write"),
			FString::Printf(TEXT("field=m_flFadeTime value=%g"), FadeTime));
		// 3. `m_iStyle < 32` (signed, `0x101304aa`): return. No publish.
		if (Style < FirstSwitchedStyle)
		{
			Site(TEXT("light_spawn"), TEXT("CLight::Spawn"), 0x10130460u, TEXT("branch"),
				FString::Printf(TEXT("arm=style_lt_32 style=%d"), Style));
			return;
		}
		// 4. START_OFF (`0x101304b0`): `LightStyle(style, "a")` (`0x101304c7`), then m_iszPattern = "a"
		//    (`0x10540a3c`): the authored pattern is overwritten.
		if (SpawnFlags & SF_LIGHT_START_OFF)
		{
			Publish(TEXT("CLight::Spawn"), 0x101304c7u, PatternDark);
			Pattern = PatternDark;
			Site(TEXT("light_spawn"), TEXT("CLight::Spawn"), 0x10130460u, TEXT("write"),
				FString::Printf(TEXT("arm=start_off field=m_iszPattern value=\"%s\""), *Pattern));
			return;
		}
		// 5. `m_iszPattern != NULL` (`0x101304e3-0x101304ed`): publish it (`0x10130502`), no store. An
		//    authored `pattern ""` is NULL (`AllocPooledString` `0x1042bff0` stores NULL for an empty
		//    value), so an empty `Pattern` takes arm 6 exactly as retail does.
		if (!Pattern.IsEmpty())
		{
			Publish(TEXT("CLight::Spawn"), 0x10130502u, Pattern);
			Site(TEXT("light_spawn"), TEXT("CLight::Spawn"), 0x10130460u, TEXT("return"),
				FString::Printf(TEXT("arm=authored m_iszPattern=\"%s\""), *Pattern));
			return;
		}
		// 6. `LightStyle(style, "m")` (`0x10130518`), then m_iszPattern = "m" (`0x105758c4`).
		Publish(TEXT("CLight::Spawn"), 0x10130518u, PatternFull);
		Pattern = PatternFull;
		Site(TEXT("light_spawn"), TEXT("CLight::Spawn"), 0x10130460u, TEXT("write"),
			FString::Printf(TEXT("arm=full field=m_iszPattern value=\"%s\""), *Pattern));
	}

	// `CLight::Use` `0x10130580` (slot 173). `CBaseEntity::InputUse` `0x100ac9f0` -- the `Use` input --
	// reaches it with USE_TOGGLE and value 0; the `Toggle` / `TurnOn` / `TurnOff` inputs do NOT pass
	// through here (they call the helpers directly, `0x10130760` / `0x10130720` / `0x10130740`).
	//   1. `m_iStyle < 32` (signed, `0x10130583`): return, nothing happens.
	//   2. `ShouldToggle(useType, !(m_spawnflags & START_OFF))` (`0x10130592-0x1013059f`): the "on"
	//      memory is the START_OFF bit, never the live lightstyle. 0 -> return.
	//   3. `Toggle` (`0x101306f0`).
	virtual void UseByType(const FElysiumEntityHandle& Activator, const FElysiumEntityHandle& Caller,
		int32 UseType, float Value) override
	{
		if (Style < FirstSwitchedStyle)
		{
			return;
		}
		const bool bStateOn = !(SpawnFlags & SF_LIGHT_START_OFF);
		const bool bShouldToggle = ShouldToggle(UseType, bStateOn);   // 0x100a98f0
		Site(TEXT("light_use"), TEXT("CLight::Use"), 0x10130580u, TEXT("branch"),
			FString::Printf(TEXT("use_type=%d state_on=%d should_toggle=%d"), UseType, bStateOn ? 1 : 0, bShouldToggle ? 1 : 0));
		if (!bShouldToggle)
		{
			return;
		}
		Toggle();
	}

	// The one-argument `Use` (the player's +use and a scripted direct call) is a slot-173 call from
	// outside `InputUse`. Which of those sites reach a light, and with which use type, is unrecovered
	// (walk L0-r012, open 14); Source's `CBasePlayer::PlayerUse` passes USE_TOGGLE, so that is what
	// this forwards until a retail caller says otherwise.
	virtual void Use(const FElysiumEntityHandle& Activator) override
	{
		UseByType(Activator, Activator, ElysiumUseType::Toggle, 0.f);
	}

	// `CLight::Kill` `0x101306d0` (slot 119): `TurnOff` (`CALL 0x100012cb` -> `0x10130690`, no style
	// gate -- publishes "a" and sets START_OFF), then the tail jump to `CBaseEntity::Kill` `0x100acf90`
	// (`0x10007ad6`), the deferred `UTIL_Remove` `0x101cd940`: the mark, `UpdateOnRemove`, `SetName(NULL)`
	// and the end-of-frame delete list -- the base body here. A hidden light publishes "a" too.
	virtual void Kill() override
	{
		Site(TEXT("light_kill"), TEXT("CLight::Kill"), 0x101306d0u, TEXT("entry"),
			FString::Printf(TEXT("m_spawnflags=%d hidden=%d"), SpawnFlags, IsHidden() ? 1 : 0));
		TurnOff();
		FElysiumEntity::Kill();
	}

	// `CLight::ScriptHide` `0x101305d0` (slot 77): `TurnOff`, then `CBaseEntity::ScriptHide` `0x100a8710`.
	virtual void ScriptHide() override
	{
		TurnOff();
		FElysiumEntity::ScriptHide();
	}

	// `CLight::ScriptUnhide` `0x101305f0` (slot 78): `TurnOn`, then `CBaseEntity::ScriptUnhide`
	// `0x100a8990`, which reinstates the saved think and makes it due now: a light whose m_pfnThink is
	// `FadeThink` (once faded) runs it on the next pass and re-publishes m_iszPattern (`ThinkAt`).
	virtual void ScriptUnhide() override
	{
		TurnOn();
		FElysiumEntity::ScriptUnhide();
	}

	// `FUN_10130610` TurnOn (the `TurnOn` input `0x10130720`, Toggle's on arm, ScriptUnhide). With
	// `p = m_iszPattern ?: ""`: publishes `p` when `p[0] != 'a'` (`0x10130622`) and `strlen(p) >= 2`
	// (`0x10130638`, unsigned `JBE` on len - 1), otherwise "m"; the call at `0x10130651`. Then clears
	// START_OFF (`0x10130657`). No style gate. A pattern starting with 'a', a one-character pattern,
	// or none at all, turns on as "m".
	void TurnOn()
	{
		const bool bUsePattern = !Pattern.IsEmpty() && Pattern[0] != TEXT('a') && Pattern.Len() >= 2;
		Publish(TEXT("FUN_10130610"), 0x10130651u, bUsePattern ? Pattern : FString(PatternFull));
		SpawnFlags &= ~SF_LIGHT_START_OFF;
	}

	// `FUN_10130690` TurnOff (the `TurnOff` input `0x10130740`, Toggle's off arm, Kill, ScriptHide):
	// `LightStyle(style, "a")` (`0x101306a7`), then sets START_OFF. No style gate; m_iszPattern untouched.
	void TurnOff()
	{
		Publish(TEXT("FUN_10130690"), 0x101306a7u, PatternDark);
		SpawnFlags |= SF_LIGHT_START_OFF;
	}

	// `FUN_101306f0` Toggle (the `Toggle` input `0x10130760`, Use's arm 3): START_OFF set -> TurnOn,
	// else TurnOff.
	void Toggle()
	{
		if (SpawnFlags & SF_LIGHT_START_OFF) { TurnOn(); } else { TurnOff(); }
	}

	// `CLight::InputSetPattern` `0x10130780`, datamap row 6 (`SetPattern`, FIELD_STRING). `AcceptInput`
	// has already refused a float / int / bool / vector / color parameter and converted a VOID one to a
	// NULL string; an EHANDLE arrives unconverted (type 12). No style gate (no `CMP 0x20` in the body).
	//   1. `value = (input->type == 2) ? input->str : NULL` (`0x1013078a-0x10130798`);
	//   2. `m_iszPattern = value`;
	//   3. `LightStyle(style, value ?: "")` (`0x101307a8` -> "", call `0x101307bd`);
	//   4. clear START_OFF.
	void InputSetPattern(const FElysiumInputArgs& A)
	{
		const FString Value = A.Param.IsString() ? A.Param.AsString : FString();
		Pattern = Value;
		Publish(TEXT("CLight::InputSetPattern"), 0x101307bdu, Value);
		SpawnFlags &= ~SF_LIGHT_START_OFF;
		Site(TEXT("light_setpattern"), TEXT("CLight::InputSetPattern"), 0x10130780u, TEXT("write"),
			FString::Printf(TEXT("m_iszPattern=\"%s\" m_spawnflags=%d param_type=%d"), *Pattern, SpawnFlags,
				static_cast<int32>(A.Param.Type)));
	}

	// `CLight::InputFadeToPattern` `0x10130800`, datamap row 7 (`FadeToPattern`, FIELD_STRING; the same
	// reachable variants as `SetPattern`). No `LightStyle` call and no style gate here.
	//   1. `m_iCurrentFade = m_iszPattern ? m_iszPattern[0] : 0` (`0x10130804-0x10130819`): the OLD
	//      pattern's first char;
	//   2. the target text: `input->str ?: ""` for a string (NULL -> "", so a parameterless input gives
	//      target 0), else `variant_t::ToString` (`0x10008805` -> `0x100d0a00`), which an EHANDLE reaches
	//      as `"%s"` of `GetDebugName` `0x1000b5cd` or `"(null entity)"`; `m_iTargetFade = t[0]`
	//      (`0x1013083e`): the first byte only;
	//   3. `m_iszPattern = (type == 2) ? input->str : NULL` (`0x1013085a-0x10130867`): a non-string's
	//      converted text is NOT stored;
	//   4. `ThinkSet(CLightFadeThink, 0.0, NULL)` (`0x1013086d` -> `0x100ac4e0`): m_pfnThink = FadeThink,
	//      the time ignored with a NULL context;
	//   5. `m_flNextThink = gpGlobals->curtime` (`0x10130872-0x10130883`): the next dispatcher pass;
	//   6. clear START_OFF (`0x10130889`).
	void InputFadeToPattern(const FElysiumInputArgs& A)
	{
		CurrentFade = FirstByte(Pattern);
		FString TargetText;
		if (A.Param.IsString())
		{
			TargetText = A.Param.AsString;
		}
		else if (A.Param.IsHandle())
		{
			const FElysiumEntity* Named = World ? World->Resolve(A.Param.AsHandle) : nullptr;
			TargetText = Named ? (Named->TargetName.IsEmpty() ? Named->Def->Classname : Named->TargetName)
				: FString(TEXT("(null entity)"));
		}
		else
		{
			TargetText = A.Param.ToString();   // unreachable past AcceptInput's Convert; retail's ToString shape
		}
		TargetFade = FirstByte(TargetText);
		Pattern = A.Param.IsString() ? A.Param.AsString : FString();
		bFadeThinkInstalled = true;
		NextThink = World ? static_cast<float>(World->NowSeconds()) : 0.f;
		SpawnFlags &= ~SF_LIGHT_START_OFF;
		Site(TEXT("light_fade"), TEXT("CLight::InputFadeToPattern"), 0x10130800u, TEXT("write"),
			FString::Printf(TEXT("cur=%d tgt=%d m_iszPattern=\"%s\" m_pfnThink=CLightFadeThink m_flNextThink=%g m_spawnflags=%d"),
				CurrentFade, TargetFade, *Pattern, NextThink, SpawnFlags));
	}

	// The think dispatch: `CBaseEntity::Think` `0x10026c70` (slot 134) is `if (m_pfnThink) call it`, and
	// the only function CLight ever installs is `FadeThink` `0x101308d0` (`CLightFadeThink`, function-table
	// row 5), which stays installed after a fade has arrived. `PhysicsRunSpecificThink` `0x10033de0`
	// fires when `0 < m_flNextThink <= curtime + frametime`, clears it, and rebinds `gpGlobals->curtime`
	// to `max(m_flNextThink, curtime)` for the call -- in this dispatcher that is the tick's `Now` (it
	// fires only once `NextThink <= Now`, never the frame early retail may).
	//   1. cur / tgt as SIGNED bytes (`JGE` / `JLE`): `cur < tgt` -> `cur++`; `cur > tgt` -> `cur--`;
	//   2. `cur == tgt` (`0x101308f8`): `LightStyle(style, m_iszPattern ?: "")` (`0x1013091b`),
	//      `m_flNextThink = 0.0` (`0x10130921`) -- the dispatcher's "no think", which the port's
	//      FLT_MAX sentinel also is -- and return, m_pfnThink left installed;
	//   3. else `LightStyle(style, [cur, 0])` (`0x1013094b`), `m_flNextThink = m_flFadeTime + curtime`
	//      (`0x10130957-0x10130960`).
	virtual void ThinkAt(double Now) override
	{
		if (!bFadeThinkInstalled)
		{
			return;
		}
		if (CurrentFade < TargetFade) { ++CurrentFade; }
		else if (CurrentFade > TargetFade) { --CurrentFade; }
		if (CurrentFade == TargetFade)
		{
			Publish(TEXT("FUN_101308d0"), 0x1013091bu, Pattern);
			NextThink = ELYSIUM_NEVER_THINK;
			Site(TEXT("light_fadethink"), TEXT("FUN_101308d0"), 0x101308d0u, TEXT("step"),
				FString::Printf(TEXT("cur=%d tgt=%d arrived=1 m_flNextThink=0"), CurrentFade, TargetFade));
			return;
		}
		Publish(TEXT("FUN_101308d0"), 0x1013094bu,
			FString::Chr(static_cast<TCHAR>(static_cast<uint8>(CurrentFade))));
		NextThink = static_cast<float>(Now + FadeTime);
		Site(TEXT("light_fadethink"), TEXT("FUN_101308d0"), 0x101308d0u, TEXT("step"),
			FString::Printf(TEXT("cur=%d tgt=%d arrived=0 m_flNextThink=%g"), CurrentFade, TargetFade, NextThink));
	}

	virtual void Serialize(FElysiumSaveArchive& Ar) override
	{
		// Rows 0 / 1 (m_iCurrentFade / m_iTargetFade, save-only bytes), the pattern, and m_pfnThink as
		// the FUNCTION-SAVE identity `CLightFadeThink` (installed or not).
		Ar << Pattern << bFadeThinkInstalled << CurrentFade << TargetFade;
		if (Ar.IsLoading() && Style >= FirstSwitchedStyle)
		{
			// The rig's table is per map load; the saved state re-publishes what it last wrote. A port
			// mechanism (the lightstyle restore of SG-37 is not recovered), so no `light_publish` site.
			if (IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr)
			{
				Embodiment->SetLightStylePattern(Style, (SpawnFlags & SF_LIGHT_START_OFF) ? FString(PatternDark) : Pattern);
			}
		}
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("Style"), FString::Printf(TEXT("%d%s"), Style,
			Style >= FirstSwitchedStyle ? TEXT(" (switched)") : TEXT("")));
		Out.Emplace(TEXT("Pattern"), Pattern);
		Out.Emplace(TEXT("State"), (SpawnFlags & SF_LIGHT_START_OFF) ? TEXT("off") : TEXT("on"));
		Out.Emplace(TEXT("Fade"), FString::Printf(TEXT("%s cur=%d tgt=%d"),
			bFadeThinkInstalled ? TEXT("FadeThink installed") : TEXT("no think"), CurrentFade, TargetFade));
		if (IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr)
		{
			Out.Emplace(TEXT("Rig pattern"), Embodiment->LightStylePattern(Style));
		}
	}

private:
	// `(*VEngineServer014)->LightStyle(style, pattern)` (`DAT_1070b22c`, vtable +0xf8), tagged with the
	// CALLER's function and call address: the one observable write of every CLight arm. The rig
	// refuses an empty pattern (`UElysiumLightRig::SetStylePattern`), so a published "" leaves the
	// last pattern standing there; the client side of an empty lightstyle is unrecovered.
	void Publish(const TCHAR* CallerFn, uint32 CallVa, const FString& Text) const
	{
		Site(TEXT("light_publish"), CallerFn, CallVa, TEXT("write"),
			FString::Printf(TEXT("style=%d pattern=\"%s\""), Style, *Text));
		if (IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr)
		{
			Embodiment->SetLightStylePattern(Style, Text);
		}
	}

	void Site(const TCHAR* Tag, const TCHAR* RetailFn, uint32 RetailVa, const TCHAR* Phase, const FString& Payload) const
	{
		if (World != nullptr)
		{
			World->EmitRetailSite(*this, Tag, RetailFn, RetailVa, Phase, Payload);
		}
	}

	bool bFadeThinkInstalled = false;   // m_pfnThink +0x118 == CLightFadeThink (`0x10012215`)
	int8 CurrentFade = 0;               // m_iCurrentFade +0x458, a signed byte
	int8 TargetFade = 0;                // m_iTargetFade +0x459, a signed byte
	bool bPitchKeyed = false;           // a `pitch` key landed at Construct; Spawn reports its site
	FString PitchKeyText;
};

class FElysiumLightDynamic final : public FElysiumEntity
{
public:
	FString LightColor;        // `_light` "r g b [a]" -- the render colour; the fourth term is not read
	int32 Brightness = 0;      // `brightness`, the dlight's ColorRGBExp32 exponent
	float Distance = 0.f;      // `distance`, Source inches
	float InnerCone = 0.f;     // `_inner_cone`, degrees
	float Cone = 0.f;          // `_cone`, degrees; 0 = a point light
	float SpotRadius = 0.f;    // `spotlight_radius`, carried
	float Pitch = 0.f;         // `pitch`, degrees; -90 = straight down
	int32 Style = 0;
	bool bOn = true;           // m_On: on at spawn

	virtual void Spawn() override
	{
		bOn = true;
		Stand();
	}

	void TurnOn()  { bOn = true;  Publish(); }
	void TurnOff() { bOn = false; Publish(); }
	void Toggle()  { bOn = !bOn;  Publish(); }

	virtual USceneComponent* GetAttachChild() const override
	{
		return Light.IsValid() ? static_cast<USceneComponent*>(Light.Get()) : FElysiumEntity::GetAttachChild();
	}

	virtual void OnDormancyChanged() override
	{
		FElysiumEntity::OnDormancyChanged();
		Publish();
	}

	virtual void OnRuntimeTransformChanged() override
	{
		FElysiumEntity::OnRuntimeTransformChanged();
		if (Light.IsValid())
		{
			Light->SetWorldLocation(Origin);
		}
	}

	virtual void Serialize(FElysiumSaveArchive& Ar) override
	{
		Ar << bOn;
		if (Ar.IsLoading())
		{
			Publish();
		}
	}

	virtual ~FElysiumLightDynamic() override
	{
		if (Light.IsValid())
		{
			if (IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr)
			{
				Embodiment->DestroyDynamicLight(Light.Get());
			}
		}
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("Light"), FString::Printf(TEXT("%s, %s"), Cone > 0.f ? TEXT("spot") : TEXT("point"),
			bOn ? TEXT("on") : TEXT("off")));
		Out.Emplace(TEXT("Colour / brightness"), FString::Printf(TEXT("%s / %d"), *LightColor, Brightness));
		Out.Emplace(TEXT("Reach"), FString::Printf(TEXT("%.0f in, cone %.0f/%.0f"), Distance, InnerCone, Cone));
	}

	// The spec the doc states: colour normalised, reach in cm, cosines of the cones, a forward
	// from (pitch, yaw) in the reflected frame, and the magnitude convention
	// `pow(c/255, 2.2) x 100 x 2^brightness x 100 / 2.55`.
	FElysiumDynamicLightSpec MakeSpec() const
	{
		FElysiumDynamicLightSpec Spec;
		Spec.LocationCm = Origin;
		float Rgb[3] = { 255.f, 255.f, 255.f };
		TArray<FString> Parts;
		LightColor.ParseIntoArray(Parts, TEXT(" "), true);
		for (int32 I = 0; I < 3 && I < Parts.Num(); ++I)
		{
			Rgb[I] = FMath::Clamp(FCString::Atof(*Parts[I]), 0.f, 255.f);
		}
		const float Max = FMath::Max3(Rgb[0], Rgb[1], Rgb[2]);
		Spec.Color = Max > 0.f ? FLinearColor(Rgb[0] / Max, Rgb[1] / Max, Rgb[2] / Max) : FLinearColor::White;
		const float Scale = 100.f * FMath::Pow(2.f, static_cast<float>(FMath::Clamp(Brightness, 0, 16)));
		Spec.Mag = Max > 0.f ? FMath::Pow(Max / 255.f, 2.2f) * Scale * (100.f / 2.55f) : 0.f;
		Spec.RadiusCm = FMath::Max(Distance, 0.f) * 2.54f;
		Spec.Style = Style;
		Spec.bSpot = Cone > 0.f;
		if (Spec.bSpot)
		{
			Spec.StopDot2 = FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(Cone, 0.f, 89.f)));
			Spec.StopDot = FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(InnerCone, 0.f, Cone)));
		}
		// CDynamicLight negates `pitch` into its angles, and AngleVectors' forward.z is -sin(pitch):
		// the FGD's "-90 = straight down". Yaw is the `angles` triple's; Y flips for the frame.
		const float P = FMath::DegreesToRadians(Pitch);
		const float Y = FMath::DegreesToRadians(static_cast<float>(Angles.Y));
		Spec.Forward = FVector(FMath::Cos(P) * FMath::Cos(Y), -FMath::Cos(P) * FMath::Sin(Y), FMath::Sin(P))
			.GetSafeNormal(UE_SMALL_NUMBER, FVector(1.f, 0.f, 0.f));
		return Spec;
	}

private:
	void Stand()
	{
		IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
		if (!Embodiment || Light.IsValid())
		{
			return;
		}
		Light = Embodiment->BuildDynamicLight(MakeSpec(), nullptr);
		Publish();
	}

	void Publish() const
	{
		if (Light.IsValid())
		{
			Light->SetVisibility(bOn && !IsInert());
		}
	}

	// Weak: the map actor owns the component and may tear it down before the substrate does.
	TWeakObjectPtr<ULightComponent> Light;
};

static TUniquePtr<FElysiumEntity> MakeLight() { return MakeUnique<FElysiumLight>(); }
static TUniquePtr<FElysiumEntity> MakeLightDynamic() { return MakeUnique<FElysiumLightDynamic>(); }

// The CLight datamap `0x105754f8` (builder `0x10130330`, 11 rows): the three keyed rows, the five
// input rows, and the base `Use` row's route. The void inputs are one-line wrappers around the
// helpers -- `InputTurnOn` `0x10130720` -> `0x10130610`, `InputTurnOff` `0x10130740` -> `0x10130690`,
// `InputToggle` `0x10130760` -> `0x101306f0` -- bypassing `Use`, `ShouldToggle` and the style gate.
static void BuildLightDesc(FElysiumClassDesc& D)
{
	D.Input(TEXT("TurnOn"), [](FElysiumEntity& E, const FElysiumInputArgs&) { static_cast<FElysiumLight&>(E).TurnOn(); });
	D.Input(TEXT("TurnOff"), [](FElysiumEntity& E, const FElysiumInputArgs&) { static_cast<FElysiumLight&>(E).TurnOff(); });
	D.Input(TEXT("Toggle"), [](FElysiumEntity& E, const FElysiumInputArgs&) { static_cast<FElysiumLight&>(E).Toggle(); });
	// Rows 6 / 7 are FIELD_STRING: `AcceptInput` `0x100abc90` refuses a float / int / bool / vector /
	// color parameter before the function runs, and hands a VOID one over as a NULL string.
	D.TypedInput(TEXT("SetPattern"), EElysiumVariantType::String,
		[](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumLight&>(E).InputSetPattern(A); });
	D.TypedInput(TEXT("FadeToPattern"), EElysiumVariantType::String,
		[](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumLight&>(E).InputFadeToPattern(A); });
	// `CBaseEntity::InputUse` `0x100ac9f0`, the base datamap row `Use` (builder `0x100a22f0`): slot 173
	// with `(activator, caller, USE_TOGGLE, 0)`.
	D.Input(TEXT("Use"), [](FElysiumEntity& E, const FElysiumInputArgs& A)
		{ E.UseByType(A.Activator, A.Caller, ElysiumUseType::Toggle, 0.f); });
	ElysiumAddClassField(D, TEXT("style"), &FElysiumLight::Style, EElysiumField::None);
	ElysiumAddClassField(D, TEXT("pattern"), &FElysiumLight::Pattern, EElysiumField::None);
	ElysiumAddClassField(D, TEXT("fade_time"), &FElysiumLight::FadeTime, EElysiumField::None);
	// `pitch` is not a datamap row: it is `CLight::vfunc110` `0x101303c0`'s one own arm. It joins the
	// class table so the key pass reaches it in the def's key order (see `KeyValuePitch`); not keyable
	// at runtime, not saved (the angles are), its read the X it wrote.
	FElysiumFieldAccessor PitchRow;
	PitchRow.Type = EElysiumVariantType::Float;
	PitchRow.ApplyFlags(EElysiumField::None);
	PitchRow.Get = [](const FElysiumEntity& E) { return FElysiumVariant::Float(static_cast<float>(E.Angles.X)); };
	PitchRow.Set = [](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<FElysiumLight&>(E).KeyValuePitch(V.ToString()); };
	D.Fields.Add(FName(TEXT("pitch")), MoveTemp(PitchRow));
}

static FElysiumClassRegistrar GRegLight(TEXT("light"), ElysiumBaseClassName(), &MakeLight, &BuildLightDesc);
static FElysiumClassRegistrar GRegLightSpot(TEXT("light_spot"), ElysiumBaseClassName(), &MakeLight, &BuildLightDesc);

static FElysiumClassRegistrar GRegLightDynamic(
	TEXT("light_dynamic"), ElysiumBaseClassName(), &MakeLightDynamic,
	[](FElysiumClassDesc& D)
	{
		D.Input(TEXT("TurnOn"), [](FElysiumEntity& E, const FElysiumInputArgs&) { static_cast<FElysiumLightDynamic&>(E).TurnOn(); });
		D.Input(TEXT("TurnOff"), [](FElysiumEntity& E, const FElysiumInputArgs&) { static_cast<FElysiumLightDynamic&>(E).TurnOff(); });
		D.Input(TEXT("Toggle"), [](FElysiumEntity& E, const FElysiumInputArgs&) { static_cast<FElysiumLightDynamic&>(E).Toggle(); });
		ElysiumAddClassField(D, TEXT("_light"), &FElysiumLightDynamic::LightColor, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("brightness"), &FElysiumLightDynamic::Brightness, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("distance"), &FElysiumLightDynamic::Distance, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("_inner_cone"), &FElysiumLightDynamic::InnerCone, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("_cone"), &FElysiumLightDynamic::Cone, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("spotlight_radius"), &FElysiumLightDynamic::SpotRadius, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("pitch"), &FElysiumLightDynamic::Pitch, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("style"), &FElysiumLightDynamic::Style, EElysiumField::None);
	});
