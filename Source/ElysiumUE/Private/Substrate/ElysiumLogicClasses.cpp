// The tutorial logic + point/brush classes that climb the sp_tutorial_1 histogram:
// math_counter, logic_timer, logic_case (+ the VtMB logic_case_toggle), env_fade, func_brush,
// and point_teleport. Each is a plain-C++ FElysiumEntity leaf (R1) registered by a module-static
// FElysiumClassRegistrar, reaching the base Kill/ScriptHide/ScriptUnhide + keyfields through the
// class chain (R2). Follows ElysiumStarterClasses.cpp / ElysiumMover.cpp.
//
// Provenance: the class factories + datamaps were read out of the decompiled vampire.dll
// (math_counter FUN_10133350, logic_timer FUN_10131390, logic_case FUN_10133ac0,
// logic_case_toggle FUN_101344f0 + its delta-advance core FUN_101346e0, env_fade FUN_10100e10,
// func_brush FUN_1013dd30, point_teleport FUN_1018d940). math_counter / logic_timer are stock
// Source semantics (confirmed present, unmodified); logic_case_toggle is a VtMB divergence —
// InValue is a *delta* that advances a current-case pointer over the configured cases (skipping
// empty slots, wrapping 0..15), then fires that case's OnCaseNN. See docs/vtmb/entity_io.md.

#include "ElysiumBrushComponent.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumRng.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumPlayer.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumClassFields.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumLogic, Log, All);

namespace
{
	// A raw keyvalue read (values that aren't mapped base/leaf fields stay on the def's Keys map).
	float KeyFloat(const FElysiumEntityDef* Def, const TCHAR* Key, float Default)
	{
		if (Def)
		{
			if (const FString* V = Def->Keys.Find(Key))
			{
				return FCString::Atof(**V);
			}
		}
		return Default;
	}
}

// math_counter — CMathCounter (7 on the tutorial; stock Source). Holds a float, clamps to
// [min,max] when either bound is set, fires OutValue (the value) on every change, and OnHitMax/
// OnHitMin on the edge into a clamped bound. Its OutValue feeds logic_case_toggle.InValue.

class FElysiumMathCounter final : public FElysiumEntity
{
public:
	float Value    = 0.0f;   // startvalue
	float MinValue  = 0.0f;  // min
	float MaxValue  = 0.0f;  // max

	void InputAdd(const FElysiumInputArgs& A)        { Set(Value + A.Param.ToFloat(), A.Activator, true); }
	void InputSubtract(const FElysiumInputArgs& A)   { Set(Value - A.Param.ToFloat(), A.Activator, true); }
	void InputMultiply(const FElysiumInputArgs& A)   { Set(Value * A.Param.ToFloat(), A.Activator, true); }
	void InputDivide(const FElysiumInputArgs& A)
	{
		const float D = A.Param.ToFloat();
		Set(FMath::IsNearlyZero(D) ? Value : Value / D, A.Activator, true);
	}
	void InputSetValue(const FElysiumInputArgs& A)       { Set(A.Param.ToFloat(), A.Activator, true); }
	void InputSetValueNoFire(const FElysiumInputArgs& A) { Set(A.Param.ToFloat(), A.Activator, false); }
	void InputSetHitMax(const FElysiumInputArgs& A)      { MaxValue = A.Param.ToFloat(); Set(Value, A.Activator, true); }
	void InputSetHitMin(const FElysiumInputArgs& A)      { MinValue = A.Param.ToFloat(); Set(Value, A.Activator, true); }
	void InputGetValue(const FElysiumInputArgs& A)
	{
		static const FName OnGetValue(TEXT("OnGetValue"));
		FireOutput(OnGetValue, A.Activator, FElysiumVariant::Float(Value));
	}

	virtual void Serialize(FElysiumSaveArchive& Ar) override
	{
		// bHitMax/bHitMin are the edge latches Set() reads to decide whether OnHitMax/OnHitMin is a
		// new edge or a repeat. Without them, a restore at an already-clamped bound reads false and
		// the next in-bound Set() re-fires the output as if the bound had just been reached.
		Ar << bHitMax;
		Ar << bHitMin;
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("Value"), FString::SanitizeFloat(Value));
		Out.Emplace(TEXT("Range"), ClampActive()
			? FString::Printf(TEXT("[%s .. %s]"), *FString::SanitizeFloat(MinValue), *FString::SanitizeFloat(MaxValue))
			: TEXT("(unclamped)"));
		Out.Emplace(TEXT("At bound"), bHitMax ? TEXT("max") : (bHitMin ? TEXT("min") : TEXT("no")));
	}

private:
	// Clamp is live only when a bound is set (Source: m_flMin != 0 || m_flMax != 0), so the tutorial's
	// min==max==0 counters count freely.
	bool ClampActive() const { return MinValue != 0.0f || MaxValue != 0.0f; }

	void Set(float NewValue, const FElysiumEntityHandle& Activator, bool bFire)
	{
		Value = NewValue;
		if (ClampActive())
		{
			Value = FMath::Clamp(Value, MinValue, MaxValue);
		}
		if (!bFire)
		{
			return;
		}
		static const FName OutValue(TEXT("OutValue"));
		FireOutput(OutValue, Activator, FElysiumVariant::Float(Value));

		if (ClampActive())
		{
			// Edge-fire OnHitMax/OnHitMin only when the value crosses into the bound (Source m_bHitMax).
			const bool bAtMax = Value >= MaxValue;
			const bool bAtMin = Value <= MinValue;
			if (bAtMax && !bHitMax) { static const FName OnHitMax(TEXT("OnHitMax")); FireOutput(OnHitMax, Activator); }
			if (bAtMin && !bHitMin) { static const FName OnHitMin(TEXT("OnHitMin")); FireOutput(OnHitMin, Activator); }
			bHitMax = bAtMax;
			bHitMin = bAtMin;
		}
	}

	bool bHitMax = false;
	bool bHitMin = false;
};

// logic_timer — CTimerEntity (1 on the tutorial; stock Source). Fires OnTimer every RefireTime
// seconds while enabled, on the substrate clock (R4). UseRandomTime picks each interval in
// [LowerRandomBound, UpperRandomBound].

class FElysiumLogicTimer final : public FElysiumEntity
{
public:
	bool  bDisabled = false;          // StartDisabled
	float RefireTime = 1.0f;          // RefireTime
	bool  bUseRandomTime = false;     // UseRandomTime
	float LowerRandomBound = 0.0f;    // LowerRandomBound
	float UpperRandomBound = 0.0f;    // UpperRandomBound

	void InputEnable()  { if (bDisabled) { bDisabled = false; Reschedule(); } }
	void InputDisable() { bDisabled = true; NextThink = ELYSIUM_NEVER_THINK; }
	void InputToggle()  { if (bDisabled) { InputEnable(); } else { InputDisable(); } }
	// FireTimer fires OnTimer now (and, if running, restarts the interval); RefireTimer just restarts.
	void InputFireTimer(const FElysiumEntityHandle& Activator)
	{
		FireTick(Activator);
		if (!bDisabled) { Reschedule(); }
	}
	void InputResetTimer() { if (!bDisabled) { Reschedule(); } }

	virtual void Spawn() override
	{
		if (!bDisabled)
		{
			Reschedule();
		}
	}

	virtual void Think() override
	{
		if (bDisabled || IsInert())
		{
			NextThink = ELYSIUM_NEVER_THINK;
			return;
		}
		FireTick(Handle);
		Reschedule();
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("Enabled"), bDisabled ? TEXT("no") : TEXT("yes"));
		Out.Emplace(TEXT("Interval"), bUseRandomTime
			? FString::Printf(TEXT("random [%.2f .. %.2f] s"), LowerRandomBound, UpperRandomBound)
			: FString::Printf(TEXT("%.2f s"), RefireTime));
		if (!bDisabled && NextThink < ELYSIUM_NEVER_THINK)
		{
			const double Now = World ? World->NowSeconds() : 0.0;
			Out.Emplace(TEXT("Next fire"), FString::Printf(TEXT("in %.2f s"), FMath::Max(0.0, NextThink - Now)));
		}
	}

private:
	float Interval() const
	{
		// S8 — an owned, seeded stream, not FMath: the interval is game-visible time, so a load has to
		// reproduce the sequence the save was in the middle of.
		return bUseRandomTime
			? ElysiumRng::Stream(EElysiumRngStream::LogicTimer).FRandRange(LowerRandomBound, UpperRandomBound)
			: RefireTime;
	}
	void Reschedule()
	{
		const double Now = World ? World->NowSeconds() : 0.0;
		NextThink = Now + FMath::Max(Interval(), 0.01f);   // guard a 0-interval timer against a tight loop
	}
	void FireTick(const FElysiumEntityHandle& Activator)
	{
		static const FName OnTimer(TEXT("OnTimer"));
		FireOutput(OnTimer, Activator);
	}
};

// logic_case / logic_case_toggle — the demultiplexer (8 logic_case_toggle on the tutorial).
// A shared base carries the 16 Case-value strings + the OnCase01..OnCase16 / OnDefault outputs.
// logic_case (stock): InValue matches the value against the case strings, fires the matching
//   OnCaseNN (else OnDefault); PickRandom fires a random configured case.
// logic_case_toggle (VtMB, FUN_101344f0/FUN_10134620): InValue retains logic_case's value-match
//   behavior and updates a current-case pointer. Its added InValueDelta input advances that pointer
//   by the requested number of configured cases (skipping empty slots, wrapping 0..15).

class FElysiumLogicCaseBase : public FElysiumEntity
{
public:
	static constexpr int32 NumCases = 16;

	virtual void Spawn() override
	{
		// Cache the configured Case01..Case16 strings (a slot is "configured" when its key is present).
		for (int32 i = 0; i < NumCases; ++i)
		{
			const FString Key = FString::Printf(TEXT("Case%02d"), i + 1);
			if (const FString* V = Def ? Def->Keys.Find(Key) : nullptr)
			{
				CaseValues[i] = *V;
				bCaseSet[i] = true;
			}
		}
	}

protected:
	static FString CaseString(const FElysiumVariant& Value)
	{
		// variant_t::String() uses compact `%g` formatting for floats. That distinction is
		// observable: math_counter OutValue(2) must match a Hammer CaseNN value of "2".
		return Value.Type == EElysiumVariantType::Float
			? FString::Printf(TEXT("%g"), static_cast<double>(Value.AsFloat))
			: Value.ToString();
	}

	// Fire OnCase<1-based idx> for a configured slot; no-op otherwise. `idx` is 0-based.
	void FireCase(int32 Idx, const FElysiumEntityHandle& Activator, const FElysiumVariant& Value)
	{
		if (Idx >= 0 && Idx < NumCases && bCaseSet[Idx])
		{
			const FName Out(*FString::Printf(TEXT("OnCase%02d"), Idx + 1));
			FireOutput(Out, Activator, Value);
		}
	}
	void FireDefault(const FElysiumEntityHandle& Activator, const FElysiumVariant& Value)
	{
		static const FName OnDefault(TEXT("OnDefault"));
		FireOutput(OnDefault, Activator, Value);
	}
	int32 ConfiguredCount() const
	{
		int32 N = 0;
		for (int32 i = 0; i < NumCases; ++i) { N += bCaseSet[i] ? 1 : 0; }
		return N;
	}

	FString CaseValues[NumCases];
	bool    bCaseSet[NumCases] = {};
};

class FElysiumLogicCase final : public FElysiumLogicCaseBase
{
public:
	// InValue: fire the first case whose value string equals the incoming value, else OnDefault.
	void InputInValue(const FElysiumInputArgs& A)
	{
		const FString In = CaseString(A.Param);
		for (int32 i = 0; i < NumCases; ++i)
		{
			if (bCaseSet[i] && CaseValues[i] == In)
			{
				FireCase(i, A.Activator, A.Param);
				return;
			}
		}
		FireDefault(A.Activator, A.Param);
	}
	void InputPickRandom(const FElysiumInputArgs& A)
	{
		TArray<int32> Live;
		for (int32 i = 0; i < NumCases; ++i) { if (bCaseSet[i]) { Live.Add(i); } }
		if (Live.Num() > 0)
		{
			FireCase(Live[ElysiumRng::Stream(EElysiumRngStream::LogicCase).RandRange(0, Live.Num() - 1)],
				A.Activator, A.Param);
		}
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("Kind"), TEXT("match (stock logic_case)"));
		Out.Emplace(TEXT("Cases set"), FString::Printf(TEXT("%d / %d"), ConfiguredCount(), NumCases));
	}
};

class FElysiumLogicCaseToggle final : public FElysiumLogicCaseBase
{
public:
	int32 InitialCase = 0;

	virtual void Spawn() override
	{
		FElysiumLogicCaseBase::Spawn();
		// FUN_10134620 validates InitialCase as 0..15, moves the raw index back one, then advances
		// backward to the preceding configured slot. This prepositions the pointer for a later
		// positive InValueDelta; it is separate from the value-matching InValue path.
		CurrentCase = FMath::Clamp(InitialCase, 0, NumCases - 1) - 1;
		if (ConfiguredCount() > 0)
		{
			Advance(-1);
		}
		else
		{
			// Retail would spin while seeking a configured case. Fail closed for malformed maps.
			CurrentCase = 0;
		}
	}

	// FUN_10134780: match the incoming string against Case01..Case16, update the pointer, and fire
	// that case. A miss sets the pointer to -1 and emits OnDefault.
	void InputInValue(const FElysiumInputArgs& A)
	{
		const FString In = CaseString(A.Param);
		for (int32 i = 0; i < NumCases; ++i)
		{
			if (bCaseSet[i] && CaseValues[i].Equals(In, ESearchCase::IgnoreCase))
			{
				CurrentCase = i;
				FireCase(i, A.Activator, A.Param);
				return;
			}
		}
		CurrentCase = -1;
		FireDefault(A.Activator, A.Param);
	}

	// FUN_101348a0/FUN_101346e0: advance by configured slots, then fire the selected case. A zero
	// delta warns in retail but deliberately re-fires the current case.
	void InputInValueDelta(const FElysiumInputArgs& A)
	{
		Advance(A.Param.ToInt());
		FireCase(CurrentCase, A.Activator, A.Param);
	}
	void InputPickRandom(const FElysiumInputArgs& A)
	{
		TArray<int32> Live;
		for (int32 i = 0; i < NumCases; ++i) { if (bCaseSet[i]) { Live.Add(i); } }
		if (Live.Num() > 0)
		{
			CurrentCase = Live[ElysiumRng::Stream(EElysiumRngStream::LogicCase).RandRange(0, Live.Num() - 1)];
			FireCase(CurrentCase, A.Activator, A.Param);
		}
	}

	virtual void Serialize(FElysiumSaveArchive& Ar) override
	{
		Super::Serialize(Ar);
		// CurrentCase is the advanced-selection pointer InValue/InValueDelta/PickRandom move; Spawn()
		// only seeds it from InitialCase. Restore runs after Spawn() (ApplySnapshot applies leaf state
		// once the def-array spawn pass, or the runtime-entity recreation pass, has already completed),
		// so this rides on top of the InitialCase seed rather than being clobbered by it.
		Ar << CurrentCase;
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("Kind"), TEXT("match + delta-advance (VtMB toggle)"));
		Out.Emplace(TEXT("Current case"), CurrentCase >= 0
			? FString::Printf(TEXT("%02d%s"), CurrentCase + 1,
				bCaseSet[CurrentCase] ? TEXT("") : TEXT(" (empty)"))
			: TEXT("(default)"));
		Out.Emplace(TEXT("Cases set"), FString::Printf(TEXT("%d / %d"), ConfiguredCount(), NumCases));
		Out.Emplace(TEXT("Initial case"), FString::Printf(TEXT("%d"), InitialCase));
	}

private:
	using Super = FElysiumLogicCaseBase;

	// FUN_101346e0: step the pointer by `Delta`, counting only configured cases, wrapping 0..15.
	// Guards against an all-empty table (would otherwise spin forever).
	void Advance(int32 Delta)
	{
		if (ConfiguredCount() == 0)
		{
			return;
		}
		while (Delta > 0)
		{
			++CurrentCase;
			if (CurrentCase < 0 || CurrentCase >= NumCases)
			{
				CurrentCase = 0;
			}
			if (bCaseSet[CurrentCase]) { --Delta; }
		}
		while (Delta < 0)
		{
			--CurrentCase;
			if (CurrentCase < 0 || CurrentCase >= NumCases)
			{
				CurrentCase = NumCases - 1;
			}
			if (bCaseSet[CurrentCase]) { ++Delta; }
		}
	}

	int32 CurrentCase = 0;
};

// env_fade — CEnvFade (8 on the tutorial: 27 Fade wires in, 16 OnBeginFade out). Its one input,
// Fade, starts a full-screen colour fade on the entity world (drawn by AElysiumHUD) and then fires
// its one output, OnBeginFade, with no delay — the whole class, per its 4-record datamap
// (duration/holdtime/Fade/OnBeginFade). The tutorial's warp to the downtown alley hangs off those
// OnBeginFade wires.
//
// SF_FADE_STAYOUT is the flag that makes the fade come *back*: CEnvFade::InputFade maps it to the
// client's auto-reverse bit, which FadeCalculate uses to flip a finished fade into a fade-in rather
// than dropping it. Without it the fade is simply dropped when its hold expires. The client's
// stay-covered-forever bit is a different one that env_fade never sets.

class FElysiumEnvFade final : public FElysiumEntity
{
public:
	static constexpr int32 SF_FADE_IN       = 0x1;   // hold the colour flat, no ramp (see GetScreenFade)
	static constexpr int32 SF_FADE_MODULATE = 0x2;   // modulate blend (deferred — drawn as alpha)
	static constexpr int32 SF_FADE_ONLYONE  = 0x4;   // fade the activator alone — the player, in one-player
	static constexpr int32 SF_FADE_STAYOUT  = 0x8;   // uncover again once the hold expires

	float    Duration = 2.0f;   // duration
	float    HoldTime = 0.0f;   // holdtime
	FLinearColor Color = FLinearColor::Black;
	float    MaxAlpha = 1.0f;

	void InputFade(const FElysiumInputArgs& A)
	{
		StartFade();
		static const FName OnBeginFade(TEXT("OnBeginFade"));
		FireOutput(OnBeginFade, A.Activator);
	}

	virtual void Spawn() override
	{
		Duration = KeyFloat(Def, TEXT("duration"), 2.0f);
		HoldTime = KeyFloat(Def, TEXT("holdtime"), 0.0f);
		MaxAlpha = FMath::Clamp(KeyFloat(Def, TEXT("renderamt"), 255.0f) / 255.0f, 0.0f, 1.0f);
		Color = ParseColor255(Def ? Def->Keys.FindRef(TEXT("rendercolor")) : FString());
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("Colour"), FString::Printf(TEXT("(%.0f %.0f %.0f) a=%.2f"),
			Color.R * 255.f, Color.G * 255.f, Color.B * 255.f, MaxAlpha));
		Out.Emplace(TEXT("Timing"), FString::Printf(TEXT("dur %.2f · hold %.2f"), Duration, HoldTime));
		TArray<FString> FlagNames;
		if (SpawnFlags & SF_FADE_IN)       { FlagNames.Add(TEXT("FADE_IN")); }
		if (SpawnFlags & SF_FADE_MODULATE) { FlagNames.Add(TEXT("MODULATE")); }
		if (SpawnFlags & SF_FADE_ONLYONE)  { FlagNames.Add(TEXT("ONLYONE")); }
		if (SpawnFlags & SF_FADE_STAYOUT)  { FlagNames.Add(TEXT("STAYOUT")); }
		Out.Emplace(TEXT("Spawnflags"), FlagNames.Num() ? FString::Join(FlagNames, TEXT(" | ")) : TEXT("(none)"));
	}

private:
	void StartFade()
	{
		if (!World)
		{
			return;
		}
		// SF_FADE_ONLYONE restricts the fade to the activator; the only client is the player, so it
		// lands on the same screen either way and needs no branch here.
		const bool bFadeIn = (SpawnFlags & SF_FADE_IN) != 0;
		const bool bAutoReverse = !bFadeIn && (SpawnFlags & SF_FADE_STAYOUT) != 0;
		World->StartScreenFade(Color, Duration, HoldTime, MaxAlpha, bFadeIn, bAutoReverse);
	}

	static FLinearColor ParseColor255(const FString& S)
	{
		const FVector V = ElysiumParseVec3(S);   // "r g b" (0..255), missing -> zero (black)
		return FLinearColor(V.X / 255.0f, V.Y / 255.0f, V.Z / 255.0f, 1.0f);
	}
};

// func_brush — CFuncBrush (19 on the tutorial). A toggleable solid brush. ScriptHide/ScriptUnhide/
// Kill are the base dormancy switch (the only inputs the tutorial wires); Enable/Disable/Toggle
// gate its collision, unified with dormancy through the body's one SetDormant switch. Solidity:
// 0 = toggle (follows enabled), 1 = never solid, 2 = always solid.

class FElysiumFuncBrush final : public FElysiumEntity
{
public:
	static constexpr int32 SOLIDITY_TOGGLE = 0;
	static constexpr int32 SOLIDITY_NEVER  = 1;
	static constexpr int32 SOLIDITY_ALWAYS = 2;

	int32 Solidity = SOLIDITY_TOGGLE;   // Solidity
	bool  bStartDisabled = false;       // StartDisabled

	void InputEnable()  { bEnabled = true;  ApplyBrushSolidity(); }
	void InputDisable() { bEnabled = false; ApplyBrushSolidity(); }
	void InputToggle()  { bEnabled = !bEnabled; ApplyBrushSolidity(); }

	virtual void Spawn() override
	{
		bEnabled = !bStartDisabled;
		// The brush body is built AFTER the spawn pass, so it can't be gated here — defer the initial
		// solidity to the first think (a born-hidden brush is already dormant from BuildBrushBody, and
		// won't think until ScriptUnhide, which re-applies through OnDormancyChanged).
		NextThink = 0.0f;
	}

	virtual void Think() override
	{
		NextThink = ELYSIUM_NEVER_THINK;   // one-shot: seat the initial solidity once the body exists
		ApplyBrushSolidity();
	}

	// Dormancy and solidity both resolve to the body's collision switch, so combine them here rather
	// than letting the base toggle collision on its own (which would fight a non-solid func_brush).
	// Still notifies the retained gizmo layer, as the base does.
	virtual void OnDormancyChanged() override
	{
		ApplyBrushSolidity();
		if (World)
		{
			World->NotifyVisualChanged(*this);
		}
	}

	virtual void Serialize(FElysiumSaveArchive& Ar) override
	{
		// bEnabled is the runtime Enable/Disable/Toggle latch; bStartDisabled (a saved field) only
		// seeds it in Spawn(). ApplySnapshot calls OnDormancyChanged unconditionally once every
		// restored field and this leaf state has landed, and this override already routes it through
		// ApplyBrushSolidity() — which reads bEnabled — so restoring the member here is enough to
		// re-seat the body's physical solidity; no separate re-apply hook is needed.
		Ar << bEnabled;
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		static const TCHAR* Names[] = { TEXT("toggle"), TEXT("never solid"), TEXT("always solid") };
		Out.Emplace(TEXT("Solidity"), Names[FMath::Clamp(Solidity, 0, 2)]);
		Out.Emplace(TEXT("Enabled"), bEnabled ? TEXT("yes") : TEXT("no"));
		Out.Emplace(TEXT("Collides"), IsSolidNow() ? TEXT("yes") : TEXT("no"));
	}

private:
	bool IsSolidNow() const
	{
		if (IsInert() || Solidity == SOLIDITY_NEVER)
		{
			return false;
		}
		if (Solidity == SOLIDITY_ALWAYS)
		{
			return true;
		}
		return bEnabled;   // SOLIDITY_TOGGLE
	}
	void ApplyBrushSolidity()
	{
		if (Body)
		{
			Body->SetDormant(!IsSolidNow());   // dormant == collision off
		}
	}

	bool bEnabled = true;
};

// point_teleport — CPointTeleport (22 on the tutorial). Its Teleport input moves the entity named
// by `target` — 48 of the 49 across the exported maps name `!player` — to this point's origin and
// `angles` facing.
//
// `!player` is the player entity's real targetname, so it resolves through the ordinary name
// index and the move is one atomic `SetRuntimeTransform`, exactly as it is for an NPC or a
// brush. Whether the moved entity carries a pawn, a skeletal body or a brush body is the
// entity's own business.

class FElysiumPointTeleport final : public FElysiumEntity
{
public:
	virtual bool ActivationStateMustPersist() const override { return true; }

	virtual void Activate() override
	{
		if (bCachedDestination)
		{
			return; // restored cache: activation must not replace the original residency destination
		}
		if (Target.IsEmpty())
		{
			UE_LOG(LogElysiumLogic, Warning, TEXT("ERROR: %s given no target. Deleted"), *DebugString());
			Kill();
			return;
		}

		CachedOrigin = Origin;
		CachedAngles = Angles;
		bCachedDestination = true;
		FElysiumEntity* Ent = ResolveTarget(nullptr);
		if (!Ent)
		{
			return; // a named runtime target may appear before the input arrives
		}
		if (!CanTeleport(*Ent))
		{
			return;
		}
		if ((SpawnFlags & 0x1) != 0)
		{
			CachedOrigin = Ent->Origin;
			CachedAngles = Ent->Angles;
		}
	}

	void InputTeleport(const FElysiumInputArgs& Args)
	{
		if (!World || !bCachedDestination)
		{
			return;
		}
		FElysiumEntity* Ent = ResolveTarget(&Args);
		if (!Ent || !CanTeleport(*Ent))
		{
			return;
		}
		Ent->SetRuntimeTransform(CachedOrigin, CachedAngles);
		// `CPointTeleport::InputTeleport` `0x1018dc00`: after the origin and angle writes, the body
		// takes a player-only arm — gated on the teleported entity's `+0xa8` player component, with
		// an RTTI fallback through its controller. That arm snaps the view, breaks a grapple, and
		// ends with `thunk_FUN_10167fd0(player)`, the one release body
		// (`slice-bc-decompiles.md` §4.3). A player yanked across the map cannot still be standing
		// at the terminal he was hacking.
		if (Ent->Handle == World->PlayerHandle())
		{
			if (FElysiumPlayer* Player = World->FindPlayer())
			{
				if (Player->FeedState.IsPaired()) Player->BreakFeed();
				else Player->LeaveGrapplePair();
			}
			World->EndPlayerUseSession(FElysiumEntityHandle::Invalid(),
				EElysiumUseEndReason::Interrupted);
		}
	}

	virtual void Serialize(FElysiumSaveArchive& Ar) override
	{
		Ar << bCachedDestination << CachedOrigin << CachedAngles;
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("Target"), Target.IsEmpty() ? TEXT("(none)") : Target);
		Out.Emplace(TEXT("Dest"), bCachedDestination
			? FString::Printf(TEXT("%s  angles %s"), *CachedOrigin.ToString(), *CachedAngles.ToString())
			: TEXT("(not activated)"));
	}

private:
	FElysiumEntity* ResolveTarget(const FElysiumInputArgs* Args) const
	{
		if (!World)
		{
			return nullptr;
		}
		if (Target.Equals(TEXT("!activator"), ESearchCase::IgnoreCase))
		{
			return Args ? World->Resolve(Args->Activator) : nullptr;
		}
		return World->FindByName(Target);
	}
	bool CanTeleport(const FElysiumEntity& Ent) const
	{
		if (!Ent.MoveParent.IsSet())
		{
			return true;
		}
		UE_LOG(LogElysiumLogic, Warning, TEXT("ERROR: %s can't teleport object (%s) which has a parent"),
			*DebugString(), *Ent.DebugString());
		return false;
	}

	bool bCachedDestination = false;
	FVector CachedOrigin = FVector::ZeroVector;
	FVector CachedAngles = FVector::ZeroVector;
};

// --- Registration ---

static TUniquePtr<FElysiumEntity> MakeMathCounter()    { return MakeUnique<FElysiumMathCounter>(); }
static TUniquePtr<FElysiumEntity> MakeLogicTimer()     { return MakeUnique<FElysiumLogicTimer>(); }
static TUniquePtr<FElysiumEntity> MakeLogicCase()      { return MakeUnique<FElysiumLogicCase>(); }
static TUniquePtr<FElysiumEntity> MakeLogicCaseToggle(){ return MakeUnique<FElysiumLogicCaseToggle>(); }
static TUniquePtr<FElysiumEntity> MakeEnvFade()        { return MakeUnique<FElysiumEnvFade>(); }
static TUniquePtr<FElysiumEntity> MakeFuncBrush()      { return MakeUnique<FElysiumFuncBrush>(); }
static TUniquePtr<FElysiumEntity> MakePointTeleport()  { return MakeUnique<FElysiumPointTeleport>(); }

static FElysiumClassRegistrar GRegMathCounter(
	TEXT("math_counter"), ElysiumBaseClassName(), &MakeMathCounter,
	[](FElysiumClassDesc& D)
	{
		D.Input(TEXT("Add"),            [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumMathCounter&>(E).InputAdd(A); });
		D.Input(TEXT("Subtract"),       [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumMathCounter&>(E).InputSubtract(A); });
		D.Input(TEXT("Multiply"),       [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumMathCounter&>(E).InputMultiply(A); });
		D.Input(TEXT("Divide"),         [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumMathCounter&>(E).InputDivide(A); });
		D.Input(TEXT("SetValue"),       [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumMathCounter&>(E).InputSetValue(A); });
		D.Input(TEXT("SetValueNoFire"), [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumMathCounter&>(E).InputSetValueNoFire(A); });
		D.Input(TEXT("SetHitMax"),      [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumMathCounter&>(E).InputSetHitMax(A); });
		D.Input(TEXT("SetHitMin"),      [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumMathCounter&>(E).InputSetHitMin(A); });
		D.Input(TEXT("GetValue"),       [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumMathCounter&>(E).InputGetValue(A); });
		ElysiumAddClassField(D, TEXT("startvalue"), &FElysiumMathCounter::Value);
		ElysiumAddClassField(D, TEXT("min"),        &FElysiumMathCounter::MinValue);
		ElysiumAddClassField(D, TEXT("max"),        &FElysiumMathCounter::MaxValue);
	});

static FElysiumClassRegistrar GRegLogicTimer(
	TEXT("logic_timer"), ElysiumBaseClassName(), &MakeLogicTimer,
	[](FElysiumClassDesc& D)
	{
		D.Input(TEXT("Enable"),    [](FElysiumEntity& E, const FElysiumInputArgs&)   { static_cast<FElysiumLogicTimer&>(E).InputEnable(); });
		D.Input(TEXT("Disable"),   [](FElysiumEntity& E, const FElysiumInputArgs&)   { static_cast<FElysiumLogicTimer&>(E).InputDisable(); });
		D.Input(TEXT("Toggle"),    [](FElysiumEntity& E, const FElysiumInputArgs&)   { static_cast<FElysiumLogicTimer&>(E).InputToggle(); });
		D.Input(TEXT("FireTimer"), [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumLogicTimer&>(E).InputFireTimer(A.Activator); });
		D.Input(TEXT("RefireTimer"), [](FElysiumEntity& E, const FElysiumInputArgs&) { static_cast<FElysiumLogicTimer&>(E).InputResetTimer(); });
		D.Input(TEXT("ResetTimer"),  [](FElysiumEntity& E, const FElysiumInputArgs&) { static_cast<FElysiumLogicTimer&>(E).InputResetTimer(); });
		ElysiumAddClassField(D, TEXT("StartDisabled"),    &FElysiumLogicTimer::bDisabled);
		ElysiumAddClassField(D, TEXT("RefireTime"),       &FElysiumLogicTimer::RefireTime);
		ElysiumAddClassField(D, TEXT("UseRandomTime"),    &FElysiumLogicTimer::bUseRandomTime);
		ElysiumAddClassField(D, TEXT("LowerRandomBound"), &FElysiumLogicTimer::LowerRandomBound);
		ElysiumAddClassField(D, TEXT("UpperRandomBound"), &FElysiumLogicTimer::UpperRandomBound);
	});

// Shared Case field table for both logic_case leaves (the Case value strings are cached in Spawn()
// off the raw keys, so only InitialCase needs a field accessor here).
static FElysiumClassRegistrar GRegLogicCase(
	TEXT("logic_case"), ElysiumBaseClassName(), &MakeLogicCase,
	[](FElysiumClassDesc& D)
	{
		D.Input(TEXT("InValue"),    [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumLogicCase&>(E).InputInValue(A); });
		D.Input(TEXT("PickRandom"), [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumLogicCase&>(E).InputPickRandom(A); });
	});

static FElysiumClassRegistrar GRegLogicCaseToggle(
	TEXT("logic_case_toggle"), ElysiumBaseClassName(), &MakeLogicCaseToggle,
	[](FElysiumClassDesc& D)
	{
		D.Input(TEXT("InValue"),    [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumLogicCaseToggle&>(E).InputInValue(A); });
		D.Input(TEXT("InValueDelta"), [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumLogicCaseToggle&>(E).InputInValueDelta(A); });
		D.Input(TEXT("PickRandom"), [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumLogicCaseToggle&>(E).InputPickRandom(A); });
		ElysiumAddClassField(D, TEXT("InitialCase"), &FElysiumLogicCaseToggle::InitialCase);
	});

static FElysiumClassRegistrar GRegEnvFade(
	TEXT("env_fade"), ElysiumBaseClassName(), &MakeEnvFade,
	[](FElysiumClassDesc& D)
	{
		// `Fade` is the whole input surface — CEnvFade's datamap carries exactly one input func.
		D.Input(TEXT("Fade"), [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumEnvFade&>(E).InputFade(A); });
		// The other two of the datamap's four records (dataDesc 0x10568c54): both keyable, so a
		// script reaches them by name through the Entity attribute namespace, not just at spawn.
		ElysiumAddClassField(D, TEXT("duration"), &FElysiumEnvFade::Duration);
		ElysiumAddClassField(D, TEXT("holdtime"), &FElysiumEnvFade::HoldTime);
	});

static FElysiumClassRegistrar GRegFuncBrush(
	TEXT("func_brush"), ElysiumBaseClassName(), &MakeFuncBrush,
	[](FElysiumClassDesc& D)
	{
		D.Input(TEXT("Enable"),  [](FElysiumEntity& E, const FElysiumInputArgs&) { static_cast<FElysiumFuncBrush&>(E).InputEnable(); });
		D.Input(TEXT("Disable"), [](FElysiumEntity& E, const FElysiumInputArgs&) { static_cast<FElysiumFuncBrush&>(E).InputDisable(); });
		D.Input(TEXT("Toggle"),  [](FElysiumEntity& E, const FElysiumInputArgs&) { static_cast<FElysiumFuncBrush&>(E).InputToggle(); });
		ElysiumAddClassField(D, TEXT("Solidity"),      &FElysiumFuncBrush::Solidity);
		ElysiumAddClassField(D, TEXT("StartDisabled"), &FElysiumFuncBrush::bStartDisabled);
	});

static FElysiumClassRegistrar GRegPointTeleport(
	TEXT("point_teleport"), ElysiumBaseClassName(), &MakePointTeleport,
	[](FElysiumClassDesc& D)
	{
		D.Input(TEXT("Teleport"), [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumPointTeleport&>(E).InputTeleport(A); });
	});
