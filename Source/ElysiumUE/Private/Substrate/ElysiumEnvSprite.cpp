// env_sprite -- `CSprite` (`walks/L0-r013.md`, story L0.effects_world.sprite-lifecycle): glow coronas,
// light shafts, candle flames, cop flashers, lightning.
//
// The billboard is the bake's (`AElysiumSpriteActor`, tagged with this entity's index); the entity
// owns CSprite's server words and their retail writers: `CSprite::Spawn` 0x1042e550 (solid, movetype,
// `Precache` 0x1042e8c0, `SetModel` 0x1042e850, `m_flMaxFrame`, the on/off choice, the yaw-into-roll
// angle arm, the scale clamp, brightness and scale), TurnOn `FUN_1042ef70` (EF_NODRAW cleared, the
// animation schedule, the frame reset), TurnOff `FUN_1042ef40` (`m_flNextThink = 0`, EF_NODRAW set),
// `CSprite::Use` 0x1042f030 with `CBaseEntity::ShouldToggle` 0x100a98f0, the inputs 0x1042f080
// (HideSprite) / 0x1042f0a0 (ShowSprite) / 0x1042f0c0 (ToggleSprite) / 0x1042f110 (TurnOn) /
// 0x1042f130 (TurnOff), and the think TurnOn installs: `AnimateThink` `FUN_1042eca0` over
// `AnimateFrame` `FUN_1042ee50`. The client's read of `m_fEffects & EF_NODRAW` is the draw switch:
// every change publishes `!(m_fEffects & 0x40) && !IsInert()` through
// `IElysiumEmbodiment::SetBakedSpriteVisible`, which finds the bake-placed sprite actor by entity
// index. The frame the client draws (`m_flFrame`) is a visual the billboard does not yet animate.

#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumClassFields.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumSprite, Log, All);

namespace
{
	constexpr int32 SF_SPRITE_START_ON = 0x1;     // spawnflag 1: a named sprite starts on
	constexpr int32 SF_SPRITE_ONCE = 0x2;         // spawnflag 2: animate once, TurnOff at the last frame
	constexpr int32 EF_NODRAW = 0x40;             // `m_fEffects` (+0x19c) bit the client's draw tests
	// `m_pfnThink` <- `0x10011757`, the thunk (`jmp 0x1042eca0`) `ThinkSet` stores for AnimateThink.
	constexpr int32 GAnimateThinkThunk = 0x10011757;
	// `_DAT_104454c4` 0.0f, `_DAT_104454c0` 1.0f, `_DAT_10449280` the DOUBLE 1.0, `DAT_10516cec` 8.0f
	// (decoded, `walks/L0-r013.md`).
	constexpr float GZero = 0.0f;
	constexpr float GOne = 1.0f;
	constexpr double GScheduleMaxFrame = 1.0;
	constexpr float GMaxScale = 8.0f;
	// USE_TYPE as `ShouldToggle` 0x100a98f0 reads it: 0 OFF, 1 ON, 2 SET, 3 TOGGLE.
	constexpr int32 USE_OFF = 0;
	constexpr int32 USE_ON = 1;
	constexpr int32 USE_SET = 2;
	constexpr int32 USE_TOGGLE = 3;
	// `VEngineServer014` slot 21's model types, as `CSprite::SetModel` 0x1042e850 (2) and
	// `CBaseEntity::SetModel` 0x100ad460 (1) compare them: 1 brush, 2 sprite, 3 studio.
	constexpr int32 GModelTypeBrush = 1;
	constexpr int32 GModelTypeSprite = 2;
	constexpr int32 GModelTypeStudio = 3;

	int32 ModelTypeOfName(const FString& Name)
	{
		// The engine types a model by its name (`*N` a brush model, `.spr`/`.vmt` a sprite, `.mdl` a
		// studio model); an empty or unknown name resolves no model (type 0).
		if (Name.StartsWith(TEXT("*"))) return GModelTypeBrush;
		if (Name.EndsWith(TEXT(".vmt"), ESearchCase::IgnoreCase) || Name.EndsWith(TEXT(".spr"), ESearchCase::IgnoreCase)) return GModelTypeSprite;
		if (Name.EndsWith(TEXT(".mdl"), ESearchCase::IgnoreCase)) return GModelTypeStudio;
		return 0;
	}

	FString Ang(const FVector& A)
	{
		return FString::Printf(TEXT("%g,%g,%g"), A.X == 0.0 ? 0.0 : A.X, A.Y == 0.0 ? 0.0 : A.Y, A.Z == 0.0 ? 0.0 : A.Z);
	}
}

class FElysiumEnvSprite final : public FElysiumEntity
{
public:
	// `m_fEffects` (+0x19c, key `effects`) is the base word `EffectsWord` (L0-r017): TurnOn clears 0x40
	// in it and TurnOff sets it.
	// `m_flSpriteFramerate` (+0x458, key `framerate`), `m_flSpriteScale` (+0x46c, key `scale`).
	float SpriteFramerate = 0.0f;
	float SpriteScale = 0.0f;
	// `m_clrRender.a` (+0x1a3, key `renderamt`): the alpha `0x1042eee0` copies into `m_nBrightness`.
	// `renderamt` is `m_clrRender`'s alpha byte (`RenderColor >> 24`), written by `CBaseEntity::KeyValue`
	// 0x1009e430's arm 1b (L0-r017).
	// `m_flFrame` (+0x45c), `m_flMaxFrame` (+0x478), `m_flLastTime` (+0x474).
	float Frame = 0.0f;
	float MaxFrame = 0.0f;
	float LastTime = 0.0f;
	// `m_nBrightness` (+0x464), `m_flBrightnessTime` (+0x468), `m_flScaleTime` (+0x470).
	int32 Brightness = 0;
	float BrightnessTime = 0.0f;
	float ScaleTime = 0.0f;
	// `m_hAttachedToEntity` (+0x450) and `m_nAttachment` (+0x454), `CSprite::Precache`'s writes.
	FElysiumEntityHandle AttachedToEntity;
	int32 Attachment = 0;
	// `m_pfnThink` (+0x118) as the function word retail stores (the port's think identity is
	// `ThinkCallback`; this is the value a record reads).
	int32 ThinkFn = 0;

	virtual void Spawn() override;
	virtual void ThinkAt(double Now) override;
	virtual void Use(const FElysiumEntityHandle& Activator) override
	{
		// The +use press: `CBasePlayer::PlayerUse` dispatches slot 173 with USE_TOGGLE.
		UseByType(Activator, Activator, USE_TOGGLE, 0.0f);
	}
	virtual void UseByType(const FElysiumEntityHandle& Activator, const FElysiumEntityHandle& Caller,
		int32 UseType, float Value) override;

	void TurnOn();            // FUN_1042ef70
	void TurnOff();           // FUN_1042ef40
	void InputToggleSprite(); // CSprite::InputToggleSprite 0x1042f0c0

	virtual void OnDormancyChanged() override
	{
		FElysiumEntity::OnDormancyChanged();
		Publish();
	}

	virtual void Serialize(FElysiumSaveArchive& Ar) override
	{
		Ar << EffectsWord;
		Ar << Frame;
		Ar << MaxFrame;
		Ar << LastTime;
		Ar << ThinkFn;
		Ar << Brightness;
		Ar << BrightnessTime;
		Ar << SpriteScale;
		Ar << ScaleTime;
		if (Ar.IsLoading())
		{
			Publish();
		}
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("Sprite"), (EffectsWord & EF_NODRAW) ? TEXT("off") : TEXT("on"));
		Out.Emplace(TEXT("Drawn"), IsVisible() ? TEXT("yes") : TEXT("no"));
		Out.Emplace(TEXT("Frame"), FString::Printf(TEXT("%g / %g"), Frame, MaxFrame));
	}

	// The client's draw test: `m_fEffects & EF_NODRAW` hides; the port's dormancy switch hides too.
	bool IsVisible() const { return (EffectsWord & EF_NODRAW) == 0 && !IsInert(); }

public:
	virtual void Precache() override;        // CSprite::Precache 0x1042e8c0, slot 104
private:
	void SetSpriteModel(const FString& Name); // CSprite::SetModel 0x1042e850
	void AnimateThink(double Now);           // FUN_1042eca0
	void AnimateFrame(float Df);             // FUN_1042ee50
	bool ShouldToggle(int32 UseType, bool bCurrentState) const; // CBaseEntity::ShouldToggle 0x100a98f0
	void Site(const TCHAR* Tag, const TCHAR* Fn, uint32 Va, const TCHAR* Phase, const FString& Payload) const
	{
		if (World)
		{
			World->EmitRetailSite(*this, Tag, Fn, Va, Phase, Payload);
		}
	}
	void Publish() const
	{
		if (IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr)
		{
			Embodiment->SetBakedSpriteVisible(Handle.Index, IsVisible());
		}
	}
};

static TUniquePtr<FElysiumEntity> MakeEnvSprite()
{
	return MakeUnique<FElysiumEnvSprite>();
}

static FElysiumClassRegistrar GRegEnvSprite(
	TEXT("env_sprite"), ElysiumBaseClassName(), &MakeEnvSprite,
	[](FElysiumClassDesc& D)
	{
		// The input table rows: `HideSprite` -> 0x1042f080 (TurnOff), `ShowSprite` -> 0x1042f0a0
		// (TurnOn), `ToggleSprite` -> 0x1042f0c0, `TurnOn` -> `CSprite::InputTurnOn` 0x1042f110,
		// `TurnOff` -> `CSprite::InputTurnOff` 0x1042f130; each a call and `RET 4`, the inputdata unread.
		D.Input(TEXT("HideSprite"), [](FElysiumEntity& E, const FElysiumInputArgs&) { static_cast<FElysiumEnvSprite&>(E).TurnOff(); });
		D.Input(TEXT("ShowSprite"), [](FElysiumEntity& E, const FElysiumInputArgs&) { static_cast<FElysiumEnvSprite&>(E).TurnOn(); });
		D.Input(TEXT("TurnOn"), [](FElysiumEntity& E, const FElysiumInputArgs&) { static_cast<FElysiumEnvSprite&>(E).TurnOn(); });
		D.Input(TEXT("TurnOff"), [](FElysiumEntity& E, const FElysiumInputArgs&) { static_cast<FElysiumEnvSprite&>(E).TurnOff(); });
		D.Input(TEXT("ToggleSprite"), [](FElysiumEntity& E, const FElysiumInputArgs&) { static_cast<FElysiumEnvSprite&>(E).InputToggleSprite(); });
		// `CSprite`'s datamap rows by their retail key or member name (`m_flSpriteFramerate` +0x458 and
		// `m_flSpriteScale` +0x46c are SAVE|KEY). `effects` and `rendercolor` are the base table's rows
		// 18 / 19 over the base words; `renderamt` is the base `KeyValue`'s arm 1b (L0-r017).
		ElysiumAddClassField(D, TEXT("framerate"), &FElysiumEnvSprite::SpriteFramerate, EElysiumField::Save | EElysiumField::MapKey);
		ElysiumAddClassField(D, TEXT("scale"), &FElysiumEnvSprite::SpriteScale, EElysiumField::Save | EElysiumField::MapKey);
		ElysiumAddClassField(D, TEXT("m_flFrame"), &FElysiumEnvSprite::Frame, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("m_flMaxFrame"), &FElysiumEnvSprite::MaxFrame, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("m_flLastTime"), &FElysiumEnvSprite::LastTime, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("m_nBrightness"), &FElysiumEnvSprite::Brightness, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("m_flBrightnessTime"), &FElysiumEnvSprite::BrightnessTime, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("m_flScaleTime"), &FElysiumEnvSprite::ScaleTime, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("m_hAttachedToEntity"), &FElysiumEnvSprite::AttachedToEntity, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("m_nAttachment"), &FElysiumEnvSprite::Attachment, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("m_pfnThink"), &FElysiumEnvSprite::ThinkFn, EElysiumField::None);
		// `m_Collision`'s solid type (+0x2b0, the int `SetSolid` 0x100dc480 writes), by Source's member name.
		ElysiumAddClassFieldVia<FElysiumEnvSprite>(D, TEXT("m_nSolidType"), [](auto& E) -> auto& { return E.RetailSolidType; }, EElysiumField::None);
	});

void FElysiumEnvSprite::Spawn()
{
	// `CSprite::Spawn` 0x1042e550 (`walks/L0-r013.md`), arms in retail order.
	const bool bNamed = !TargetName.IsEmpty();
	const bool bStartOn = (SpawnFlags & SF_SPRITE_START_ON) != 0;
	Site(TEXT("sprite.spawn"), TEXT("CSprite::Spawn"), 0x1042e550u, TEXT("entry"),
		FString::Printf(TEXT("model=%s named=%d flag1=%d angles=%s scale=%g renderamt=%d effects=0x%x"),
			Model.IsEmpty() ? TEXT("(empty)") : *Model, bNamed ? 1 : 0, bStartOn ? 1 : 0, *Ang(LocalAnglesWord()), SpriteScale, (RenderColor >> 24) & 0xffu, EffectsWord));
	// 1. `SetSolid(&m_Collision, 0)` (thunk 0x1000428c -> 0x100dc480, under a "CBaseEntity::SetSolid"
	//    scope frame): SOLID_NONE; acts only when the solid differs. The port's collision words.
	RetailSolidType = 0;
	++RetailSolidSets;
	// 2. `SetMoveType(0, 0)` (slot 93, 0x100aad70): MOVETYPE_NONE, written only on change.
	SetMoveType(0, 0);
	// 3. `m_flFrame = 0`.
	Frame = GZero;
	// 4. `Precache()` (slot 104, `CSprite::Precache` 0x1042e8c0).
	Precache();
	// 5. `SetModel(GetModelName() or "")` (slot 105, `CSprite::SetModel` 0x1042e850).
	SetSpriteModel(Model);
	// 6. `m_flMaxFrame = (float)frames - 1.0f`: `VEngineServer014` slot 26 over `GetModelIndex()`
	//    (slot 8); a sprite model answers its frame count, an unresolved model 1 (so `m_flMaxFrame` 0).
	const int32 Frames = World ? World->SpriteModelFrameCount(Model, Handle.Index) : 1;
	MaxFrame = static_cast<float>(Frames) - GOne;
	Site(TEXT("sprite.spawn"), TEXT("CSprite::Spawn"), 0x1042e550u, TEXT("frames"),
		FString::Printf(TEXT("frames=%d m_flMaxFrame=%g m_MoveType=%d m_nSolidType=%d m_flFrame=%g"), Frames, MaxFrame, GetMoveType(), RetailSolidType, Frame));
	// 7. `if (m_iName == 0 || (m_spawnflags & 1)) TurnOn() else TurnOff()`. Spawn reads neither
	//    `m_bStartHidden` (+0xe0, `CBaseEntity::PostSpawn`'s) nor any other `m_fEffects` bit.
	Site(TEXT("sprite.spawn"), TEXT("CSprite::Spawn"), 0x1042e550u, TEXT("branch"),
		FString::Printf(TEXT("arm=%s"), (!bNamed || bStartOn) ? TEXT("turnon") : TEXT("turnoff")));
	if (!bNamed || bStartOn)
	{
		TurnOn();
	}
	else
	{
		TurnOff();
	}
	// 8. The angle arm: `p = GetAngles()` (slot 221, the LOCAL word); `if (p.yaw != 0.0f && p.roll ==
	//    0.0f) SetAngles((pitch, 0, yaw))` -- the yaw moves into the roll slot and yaw becomes 0 (asm
	//    1042e6a7-1042e6d2); `SetAngles` 0x100b2d00 finds the vector different and writes it, with
	//    the EFL invalidation and `+0x1b1`. An authored `angles "0 90 0"` ends as `(0, 0, 90)`.
	const FVector Before = LocalAnglesWord();
	const bool bAngleFix = static_cast<float>(Before.Y) != GZero && static_cast<float>(Before.Z) == GZero;
	if (bAngleFix)
	{
		SetAngles(FRotator(Before.X, 0.0, Before.Y));
	}
	Site(TEXT("sprite.spawn"), TEXT("CSprite::Spawn"), 0x1042e550u, TEXT("angles"),
		FString::Printf(TEXT("before=%s after=%s fixed=%d"), *Ang(Before), *Ang(LocalAnglesWord()), bAngleFix ? 1 : 0));
	// 9. The scale clamp on a local copy of `m_flSpriteScale`: outside [0.0f, 8.0f] -> `DevMsg(1, "LEVEL
	//    DESIGN ERROR: Sprite %s with bad scale %.f [0..%.f]\n", GetDebugName(), s, 8.0)`, and the result
	//    is 0 below, 8 above. An authored 0 passes through as 0 (Spawn has no 0 -> 1 rule; the bake's
	//    reading of 0 as 1 is the client draw's, UNRECOVERED here).
	float Scale = SpriteScale;
	const bool bScaleError = Scale < GZero || GMaxScale < Scale;
	if (bScaleError)
	{
		UE_LOG(LogElysiumSprite, Log, TEXT("LEVEL DESIGN ERROR: Sprite %s with bad scale %.f [0..%.f]"),
			*DebugString(), static_cast<double>(SpriteScale), static_cast<double>(GMaxScale));
		Scale = GMaxScale;
		if (SpriteScale <= GMaxScale)
		{
			Scale = GZero <= SpriteScale ? SpriteScale : 0.0f;
		}
	}
	Site(TEXT("sprite.spawn"), TEXT("CSprite::Spawn"), 0x1042e550u, TEXT("scale"),
		FString::Printf(TEXT("in=%g out=%g error=%d"), SpriteScale, Scale, bScaleError ? 1 : 0));
	// 10. `0x1042eee0(this, rendercolor.a, 0)`: `m_nBrightness = m_clrRender.a`, `m_flBrightnessTime = 0`.
	Brightness = static_cast<int32>((RenderColor >> 24) & 0xffu);
	BrightnessTime = GZero;
	// 11. `0x1042ef10(this, clamped, 0)`: `m_flSpriteScale = clamped`, `m_flScaleTime = 0`.
	SpriteScale = Scale;
	ScaleTime = GZero;
	Site(TEXT("sprite.spawn"), TEXT("CSprite::Spawn"), 0x1042e550u, TEXT("return"),
		FString::Printf(TEXT("m_nBrightness=%d m_flBrightnessTime=%g m_flSpriteScale=%g m_flScaleTime=%g m_flFrame=%g m_flMaxFrame=%g"),
			Brightness, BrightnessTime, SpriteScale, ScaleTime, Frame, MaxFrame));
}

void FElysiumEnvSprite::Precache()
{
	// `CSprite::Precache` 0x1042e8c0. (a) A non-null model name -> `PrecacheModel` (0x100adfd0): the
	// engine's model table, replaced by the bake. (b) `m_hAimEnt` (+0x37c) a live handle that
	// re-resolves: `m_hAttachedToEntity = aim->GetRefEHandle()`, `m_nAttachment` rewritten with its own
	// value, `SetAimEnt(aim)` 0x1009ee80, `SetMoveType(0xb, 0)` (MOVETYPE_FOLLOW); a live handle whose
	// entity is null returns with nothing written. (b') Otherwise `-1` and `0`. UNRECOVERED: which
	// retail code writes `m_hAimEnt` before Spawn (no map row does).
	FElysiumEntity* Aim = World && AimEnt.IsSet() ? World->Resolve(AimEnt) : nullptr;
	FString Arm;
	if (AimEnt.IsSet())
	{
		if (Aim == nullptr)
		{
			Arm = TEXT("aim_null");
		}
		else
		{
			AttachedToEntity = Aim->Handle;
			SetAimEnt(Aim);
			SetMoveType(11, 0);
			Arm = TEXT("aim");
		}
	}
	else
	{
		AttachedToEntity = FElysiumEntityHandle::Invalid();
		Attachment = 0;
		Arm = TEXT("none");
	}
	Site(TEXT("sprite.precache"), TEXT("CSprite::Precache"), 0x1042e8c0u, TEXT("return"),
		FString::Printf(TEXT("model=%s arm=%s m_hAttachedToEntity=%s m_nAttachment=%d m_MoveType=%d"),
			Model.IsEmpty() ? TEXT("(empty)") : *Model, *Arm,
			AttachedToEntity.IsSet() ? *AttachedToEntity.ToString() : TEXT("-1"), Attachment, GetMoveType()));
}

void FElysiumEnvSprite::SetSpriteModel(const FString& Name)
{
	// `CSprite::SetModel` 0x1042e850: `VEngineServer014` slot 20 (name -> index) and 21 (index -> type);
	// a type other than 2 -> `Msg("Setting CSprite to non-sprite model %s")`. Then the base set
	// `FUN_101cf4a0`: a non-empty name re-writes the model index (slot 10), the name (slot 212) and the
	// collision bounds from the model (`SetCollisionBounds`); an empty name does nothing. The engine's
	// model table is the bake's: the name is the port's model word already, and a billboard has no
	// collision bounds to set. `CSprite::SetModel` does not write `+0x1b1`.
	const int32 Type = ModelTypeOfName(Name);
	const bool bMsg = Type != GModelTypeSprite;
	if (bMsg)
	{
		UE_LOG(LogElysiumSprite, Log, TEXT("Setting CSprite to non-sprite model %s"), *Name);
	}
	Site(TEXT("sprite.setmodel"), TEXT("CSprite::SetModel"), 0x1042e850u, TEXT("branch"),
		FString::Printf(TEXT("model=%s type=%d msg=%d set=%d"), Name.IsEmpty() ? TEXT("(empty)") : *Name, Type, bMsg ? 1 : 0, Name.IsEmpty() ? 0 : 1));
}

void FElysiumEnvSprite::TurnOn()
{
	// `FUN_1042ef70` (`__fastcall`, no stack argument), arms in retail order.
	// 1. `m_fEffects &= ~0x40` (EF_NODRAW cleared), unconditional.
	EffectsWord &= ~static_cast<uint32>(EF_NODRAW);
	// 2. `S = (m_flSpriteFramerate != 0.0f && (double)m_flMaxFrame > 1.0) || (m_spawnflags & 2)`. The
	//    comparand is the DOUBLE 1.0 at 0x10449280: a 2-frame sprite (`m_flMaxFrame` 1.0) does not
	//    schedule; the smallest scheduling frame count is 3.
	const bool bFrames = SpriteFramerate != GZero && static_cast<double>(MaxFrame) > GScheduleMaxFrame;
	const bool bOnce = (SpawnFlags & SF_SPRITE_ONCE) != 0;
	const double Now = World ? World->NowSeconds() : 0.0;
	FString Next = TEXT("untouched");
	if (bFrames || bOnce)
	{
		// 3. `ThinkSet(this, 0x10011757, 0.0, NULL)` writes `m_pfnThink` only (a NULL context ignores the
		//    time); `m_flNextThink = curtime`; `m_flLastTime = curtime`.
		ThinkFn = GAnimateThinkThunk;
		NextThink = static_cast<float>(Now);
		LastTime = static_cast<float>(Now);
		Next = FString::Printf(TEXT("%.4f"), Now);
	}
	// 4. `m_flFrame = 0`, always.
	Frame = GZero;
	// 5. The tail jump to `CBaseEntity::ForceTransmit` (0x1000e859 -> 0x1009d1e0): `+0x90 = curtime +
	//    1.0f`, the transmit-until stamp `ShouldTransmit` 0x100ab020 reads -- networking, replaced by
	//    Unreal's replication; not carried.
	Site(TEXT("sprite.turnon"), TEXT("FUN_1042ef70"), 0x1042ef70u, TEXT("branch"),
		FString::Printf(TEXT("sched=%s effects=0x%x m_pfnThink=0x%x next=%s last=%.4f frame=%g framerate=%g max=%g"),
			bFrames ? TEXT("frames") : bOnce ? TEXT("flag2") : TEXT("none"), EffectsWord, ThinkFn, *Next, LastTime, Frame,
			SpriteFramerate, MaxFrame));
	Publish();
}

void FElysiumEnvSprite::TurnOff()
{
	// `FUN_1042ef40` (`__fastcall`): `m_flNextThink = 0; m_fEffects |= 0x40`. It touches neither
	// `m_pfnThink`, `m_flFrame` nor the timestamp. A next-think of 0 is Source's "no think"
	// (`PhysicsRunSpecificThink`: `thinktime <= 0` returns), the port's `ELYSIUM_NEVER_THINK`.
	NextThink = ELYSIUM_NEVER_THINK;
	EffectsWord |= static_cast<uint32>(EF_NODRAW);
	Site(TEXT("sprite.turnoff"), TEXT("FUN_1042ef40"), 0x1042ef40u, TEXT("return"),
		FString::Printf(TEXT("effects=0x%x next=0 m_pfnThink=0x%x frame=%g"), EffectsWord, ThinkFn, Frame));
	Publish();
}

void FElysiumEnvSprite::InputToggleSprite()
{
	// `CSprite::InputToggleSprite` 0x1042f0c0: `CMP [this+0x19c], 0x40; JZ TurnOn; else TurnOff` -- an
	// EXACT compare of the whole effects word, so a word of 0x60 (NODRAW|NOSHADOW) always takes the
	// TurnOff arm and Toggle never shows that sprite.
	const bool bOff = EffectsWord != static_cast<uint32>(EF_NODRAW);
	Site(TEXT("sprite.toggle"), TEXT("CSprite::InputToggleSprite"), 0x1042f0c0u, TEXT("branch"),
		FString::Printf(TEXT("effects=0x%x arm=%s"), EffectsWord, bOff ? TEXT("turnoff") : TEXT("turnon")));
	if (bOff)
	{
		TurnOff();
	}
	else
	{
		TurnOn();
	}
}

bool FElysiumEnvSprite::ShouldToggle(int32 UseType, bool bCurrentState) const
{
	// `CBaseEntity::ShouldToggle` 0x100a98f0 (thunk 0x100076a3): 0 only when `useType` is neither
	// TOGGLE (3) nor SET (2) AND (`state == 0 && useType == OFF`) or (`state != 0 && useType == ON`);
	// otherwise 1. OFF on->off, OFF off->no-op, ON off->on, ON on->no-op, TOGGLE and SET flip.
	bool bResult = true;
	if (UseType != USE_TOGGLE && UseType != USE_SET)
	{
		if ((!bCurrentState && UseType == USE_OFF) || (bCurrentState && UseType == USE_ON))
		{
			bResult = false;
		}
	}
	Site(TEXT("sprite.should_toggle"), TEXT("CBaseEntity::ShouldToggle"), 0x100a98f0u, TEXT("return"),
		FString::Printf(TEXT("usetype=%d state=%d result=%d"), UseType, bCurrentState ? 1 : 0, bResult ? 1 : 0));
	return bResult;
}

void FElysiumEnvSprite::UseByType(const FElysiumEntityHandle& Activator, const FElysiumEntityHandle& Caller,
	int32 UseType, float Value)
{
	// `CSprite::Use` 0x1042f030 (slot 173, `RET 0x10`): `state = (m_fEffects != 0x40)` -- the exact
	// compare, any bit beyond 0x40 counts as on -- then `ShouldToggle(useType, state)`; refused: return;
	// else `state` ? TurnOff : TurnOn. `activator`, `caller` and `value` are not read.
	(void)Caller;
	(void)Value;
	const bool bState = EffectsWord != static_cast<uint32>(EF_NODRAW);
	Site(TEXT("sprite.use"), TEXT("CSprite::Use"), 0x1042f030u, TEXT("entry"),
		FString::Printf(TEXT("usetype=%d state=%d effects=0x%x activator=%s"), UseType, bState ? 1 : 0, EffectsWord,
			Activator.IsSet() ? *Activator.ToString() : TEXT("none")));
	if (!ShouldToggle(UseType, bState))
	{
		Site(TEXT("sprite.use"), TEXT("CSprite::Use"), 0x1042f030u, TEXT("branch"), TEXT("arm=refused"));
		return;
	}
	Site(TEXT("sprite.use"), TEXT("CSprite::Use"), 0x1042f030u, TEXT("branch"),
		FString::Printf(TEXT("arm=%s"), bState ? TEXT("turnoff") : TEXT("turnon")));
	if (bState)
	{
		TurnOff();
	}
	else
	{
		TurnOn();
	}
}

void FElysiumEnvSprite::ThinkAt(double Now)
{
	// `m_pfnThink`: TurnOn installs `0x10011757` (`jmp 0x1042eca0`, AnimateThink); nothing else is
	// ever stored in a sprite's think word.
	if (ThinkFn == GAnimateThinkThunk)
	{
		AnimateThink(Now);
	}
}

void FElysiumEnvSprite::AnimateThink(double Now)
{
	// `FUN_1042eca0` (`__fastcall`): `AnimateFrame((curtime - m_flLastTime) * m_flSpriteFramerate)`;
	// then `m_flNextThink = curtime; m_flLastTime = curtime` -- AFTER AnimateFrame, so a once-sprite
	// that TurnOff'd inside it (next-think 0) is re-armed and keeps thinking every tick while hidden,
	// its frame growing past the maximum (retail's own order; `walks/L0-r013.md` § Once-sprite end).
	const float Df = (static_cast<float>(Now) - LastTime) * SpriteFramerate;
	Site(TEXT("sprite.animate"), TEXT("FUN_1042eca0"), 0x1042eca0u, TEXT("entry"),
		FString::Printf(TEXT("df=%g frame_before=%g max=%g curtime=%.4f last=%.4f"), Df, Frame, MaxFrame, Now, LastTime));
	AnimateFrame(Df);
	NextThink = static_cast<float>(Now);
	LastTime = static_cast<float>(Now);
}

void FElysiumEnvSprite::AnimateFrame(float Df)
{
	// `FUN_1042ee50` (`RET 4`): `m_flFrame += df`; if `m_flFrame > m_flMaxFrame`: spawnflag 2 ->
	// TurnOff and return (no wrap); else if `m_flMaxFrame > 0.0f` -> `m_flFrame = fmod(m_flFrame,
	// m_flMaxFrame)` (`FUN_10431eea`, the CRT `fmod` entry at 0x106b0a70). The two thresholds differ:
	// TurnOn schedules at `max > 1.0`, this wraps at `max > 0.0`.
	Frame += Df;
	bool bWrapped = false;
	bool bOnceEnd = false;
	if (MaxFrame < Frame)
	{
		if ((SpawnFlags & SF_SPRITE_ONCE) != 0)
		{
			bOnceEnd = true;
			Site(TEXT("sprite.frame"), TEXT("FUN_1042ee50"), 0x1042ee50u, TEXT("return"),
				FString::Printf(TEXT("frame=%g wrapped=0 once_end=1"), Frame));
			TurnOff();
			return;
		}
		if (GZero < MaxFrame)
		{
			Frame = FMath::Fmod(Frame, MaxFrame);
			bWrapped = true;
		}
	}
	Site(TEXT("sprite.frame"), TEXT("FUN_1042ee50"), 0x1042ee50u, TEXT("return"),
		FString::Printf(TEXT("frame=%g wrapped=%d once_end=%d"), Frame, bWrapped ? 1 : 0, bOnceEnd ? 1 : 0));
}
