// P4.5 — the tutorial logic + point/brush classes that climb the sp_tutorial_1 histogram:
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
// empty slots, wrapping 0..15), then fires that case's OnCaseNN. See docs/entity_io.md.

#include "ElysiumBrushComponent.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"

#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

#include <type_traits>

DEFINE_LOG_CATEGORY_STATIC(LogElysiumLogic, Log, All);

namespace
{
	// Register a field backed by a *subclass* member (FElysiumClassDesc::Field only takes base
	// FElysiumEntity members; leaf classes carry their own state). Mirrors AddSubclassField in
	// ElysiumStarterClasses.cpp / AddDoorSubclassField in ElysiumMover.cpp — file-unique name so all
	// three can land in one unity blob. Always valid: a class's field table is only walked for
	// entities of that class or a subclass.
	template <typename TClass, typename TMember>
	void AddLogicField(FElysiumClassDesc& D, const TCHAR* Name, TMember TClass::* Member, bool bKeyable = true)
	{
		static_assert(std::is_base_of_v<FElysiumEntity, TClass>, "TClass must derive from FElysiumEntity");
		FElysiumFieldAccessor Acc;
		Acc.bKeyable = bKeyable;
		if constexpr (std::is_same_v<TMember, bool>)
		{
			Acc.Type = EElysiumVariantType::Bool;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::Bool(static_cast<const TClass&>(E).*Member); };
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToInt() != 0; };
		}
		else if constexpr (std::is_same_v<TMember, float>)
		{
			Acc.Type = EElysiumVariantType::Float;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::Float(static_cast<const TClass&>(E).*Member); };
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToFloat(); };
		}
		else if constexpr (std::is_same_v<TMember, int32>)
		{
			Acc.Type = EElysiumVariantType::Int;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::Int(static_cast<const TClass&>(E).*Member); };
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToInt(); };
		}
		else
		{
			static_assert(sizeof(TMember) == 0, "AddLogicField: unsupported member type");
		}
		D.Fields.Add(FName(Name), MoveTemp(Acc));
	}

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

// ============================================================================================
// math_counter — CMathCounter (7 on the tutorial; stock Source). Holds a float, clamps to
// [min,max] when either bound is set, fires OutValue (the value) on every change, and OnHitMax/
// OnHitMin on the edge into a clamped bound. Its OutValue feeds logic_case_toggle.InValue.
// ============================================================================================

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

// ============================================================================================
// logic_timer — CTimerEntity (1 on the tutorial; stock Source). Fires OnTimer every RefireTime
// seconds while enabled, on the substrate clock (R4). UseRandomTime picks each interval in
// [LowerRandomBound, UpperRandomBound].
// ============================================================================================

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
		return bUseRandomTime ? FMath::FRandRange(LowerRandomBound, UpperRandomBound) : RefireTime;
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

// ============================================================================================
// logic_case / logic_case_toggle — the demultiplexer (8 logic_case_toggle on the tutorial).
// A shared base carries the 16 Case-value strings + the OnCase01..OnCase16 / OnDefault outputs.
// logic_case (stock): InValue matches the value against the case strings, fires the matching
//   OnCaseNN (else OnDefault); PickRandom fires a random configured case.
// logic_case_toggle (VtMB, FUN_101344f0/FUN_101346e0): a current-case pointer initialised from
//   InitialCase; InValue is a *delta* that advances the pointer that many configured cases
//   (skipping empty slots, wrapping 0..15), then fires the new current case's OnCaseNN.
// ============================================================================================

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
		const FString In = A.Param.ToString();
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
			FireCase(Live[FMath::RandRange(0, Live.Num() - 1)], A.Activator, A.Param);
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
	int32 InitialCase = 0;   // InitialCase keyvalue (1-based in the .ents; 0 = none)

	virtual void Spawn() override
	{
		FElysiumLogicCaseBase::Spawn();
		// The current-case pointer starts at InitialCase (1-based) - 1; clamp into range, default 0.
		CurrentCase = FMath::Clamp(InitialCase - 1, 0, NumCases - 1);
	}

	// InValue is a delta (FUN_101346e0): advance the pointer that many *configured* cases, skipping
	// empty slots and wrapping 0..15, then fire the new current case. A zero/absent delta re-fires
	// the current case (matching the decompile's warn-and-fall-through).
	void InputInValue(const FElysiumInputArgs& A)
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
			CurrentCase = Live[FMath::RandRange(0, Live.Num() - 1)];
			FireCase(CurrentCase, A.Activator, A.Param);
		}
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("Kind"), TEXT("delta-advance (VtMB toggle)"));
		Out.Emplace(TEXT("Current case"), FString::Printf(TEXT("%02d%s"), CurrentCase + 1,
			bCaseSet[CurrentCase] ? TEXT("") : TEXT(" (empty)")));
		Out.Emplace(TEXT("Cases set"), FString::Printf(TEXT("%d / %d"), ConfiguredCount(), NumCases));
		Out.Emplace(TEXT("Initial case"), FString::Printf(TEXT("%d"), InitialCase));
	}

private:
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
			CurrentCase = (CurrentCase + 1) % NumCases;
			if (bCaseSet[CurrentCase]) { --Delta; }
		}
		while (Delta < 0)
		{
			CurrentCase = (CurrentCase + NumCases - 1) % NumCases;
			if (bCaseSet[CurrentCase]) { ++Delta; }
		}
	}

	int32 CurrentCase = 0;
};

// ============================================================================================
// env_fade — CEnvFade (1 on the tutorial, 22 Fade wires). Its Fade input starts a full-screen
// colour fade on the entity world (drawn by AElysiumHUD). SF_FADE_IN reveals, SF_FADE_STAYOUT
// holds at full after covering (the map-transition fade-to-black the tutorial's `fade_out` uses).
// ============================================================================================

class FElysiumEnvFade final : public FElysiumEntity
{
public:
	static constexpr int32 SF_FADE_IN       = 0x1;   // reveal (fade FROM the colour back to normal)
	static constexpr int32 SF_FADE_MODULATE = 0x2;   // modulate blend (deferred — drawn as alpha)
	static constexpr int32 SF_FADE_STAYOUT  = 0x8;   // stay covered at full after the fade-in

	float    Duration = 2.0f;   // duration
	float    HoldTime = 0.0f;   // holdtime
	FLinearColor Color = FLinearColor::Black;
	float    MaxAlpha = 1.0f;

	void InputFade(const FElysiumInputArgs&)        { StartFade(false); }
	void InputReverseFade(const FElysiumInputArgs&) { StartFade(true); }

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
		if (SpawnFlags & SF_FADE_STAYOUT)  { FlagNames.Add(TEXT("STAYOUT")); }
		Out.Emplace(TEXT("Spawnflags"), FlagNames.Num() ? FString::Join(FlagNames, TEXT(" | ")) : TEXT("(none)"));
	}

private:
	void StartFade(bool bForceReverse)
	{
		if (!World)
		{
			return;
		}
		const bool bReverse = bForceReverse || (SpawnFlags & SF_FADE_IN) != 0;
		const bool bStayOut = (SpawnFlags & SF_FADE_STAYOUT) != 0;
		World->StartScreenFade(Color, Duration, HoldTime, MaxAlpha, bReverse, bStayOut);
	}

	static FLinearColor ParseColor255(const FString& S)
	{
		const FVector V = ElysiumParseVec3(S);   // "r g b" (0..255), missing -> zero (black)
		return FLinearColor(V.X / 255.0f, V.Y / 255.0f, V.Z / 255.0f, 1.0f);
	}
};

// ============================================================================================
// func_brush — CFuncBrush (19 on the tutorial). A toggleable solid brush. ScriptHide/ScriptUnhide/
// Kill are the base dormancy switch (the only inputs the tutorial wires); Enable/Disable/Toggle
// gate its collision, unified with dormancy through the body's one SetDormant switch. Solidity:
// 0 = toggle (follows enabled), 1 = never solid, 2 = always solid.
// ============================================================================================

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

// ============================================================================================
// point_teleport — CPointTeleport (22 on the tutorial). Its Teleport input moves the entity named
// by `target` (almost always !player) to this point's origin + `angles` facing. Reaches the pawn
// through the world seam (the player is not an entity yet, P4-later).
// ============================================================================================

class FElysiumPointTeleport final : public FElysiumEntity
{
public:
	void InputTeleport(const FElysiumInputArgs&)
	{
		const FVector DestOrigin = Def ? Def->Origin : FVector::ZeroVector;
		// Source SetMovedir-style angles are (pitch, yaw, roll); the load transform reflects Y, so the
		// Unreal facing yaw is the negated Source yaw (matches source_dir_to_unreal for a yaw-only turn).
		const float Yaw = -Angles.Y;

		const FString TgtName = Def ? Def->Keys.FindRef(TEXT("target")) : FString();
		if (TgtName.IsEmpty() || TgtName.Equals(TEXT("!player"), ESearchCase::IgnoreCase) ||
			TgtName.Equals(TEXT("!activator"), ESearchCase::IgnoreCase))
		{
			TeleportPawn(DestOrigin, Yaw);
			return;
		}
		// A named entity target: move its brush body, if it has one (logic/point ents can't be placed).
		if (FElysiumEntity* Ent = World ? World->FindByName(TgtName) : nullptr)
		{
			if (Ent->Body)
			{
				Ent->Body->SetWorldLocationAndRotation(DestOrigin, FRotator(0.0f, Yaw, 0.0f));
			}
		}
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("Target"), Def ? Def->Keys.FindRef(TEXT("target")) : FString(TEXT("(none)")));
		Out.Emplace(TEXT("Dest"), FString::Printf(TEXT("%s  yaw %.0f"),
			*(Def ? Def->Origin : FVector::ZeroVector).ToString(), -Angles.Y));
	}

private:
	void TeleportPawn(const FVector& Origin, float Yaw)
	{
		APawn* Pawn = World ? World->GetPlayerPawn() : nullptr;
		if (!Pawn)
		{
			return;
		}
		// Source places the entity's absorigin (feet); an Unreal capsule is centred, so lift by the
		// capsule half-height to seat the player on the destination rather than in the floor.
		FVector Dest = Origin;
		if (const ACharacter* Char = Cast<ACharacter>(Pawn))
		{
			Dest.Z += Char->GetDefaultHalfHeight();
		}
		Pawn->SetActorLocation(Dest, false, nullptr, ETeleportType::TeleportPhysics);
		if (APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
		{
			PC->SetControlRotation(FRotator(0.0f, Yaw, 0.0f));
		}
	}
};

// ============================================================================================
// Registration
// ============================================================================================

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
		AddLogicField(D, TEXT("startvalue"), &FElysiumMathCounter::Value);
		AddLogicField(D, TEXT("min"),        &FElysiumMathCounter::MinValue);
		AddLogicField(D, TEXT("max"),        &FElysiumMathCounter::MaxValue);
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
		AddLogicField(D, TEXT("StartDisabled"),    &FElysiumLogicTimer::bDisabled);
		AddLogicField(D, TEXT("RefireTime"),       &FElysiumLogicTimer::RefireTime);
		AddLogicField(D, TEXT("UseRandomTime"),    &FElysiumLogicTimer::bUseRandomTime);
		AddLogicField(D, TEXT("LowerRandomBound"), &FElysiumLogicTimer::LowerRandomBound);
		AddLogicField(D, TEXT("UpperRandomBound"), &FElysiumLogicTimer::UpperRandomBound);
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
		D.Input(TEXT("PickRandom"), [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumLogicCaseToggle&>(E).InputPickRandom(A); });
		AddLogicField(D, TEXT("InitialCase"), &FElysiumLogicCaseToggle::InitialCase);
	});

static FElysiumClassRegistrar GRegEnvFade(
	TEXT("env_fade"), ElysiumBaseClassName(), &MakeEnvFade,
	[](FElysiumClassDesc& D)
	{
		D.Input(TEXT("Fade"),        [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumEnvFade&>(E).InputFade(A); });
		D.Input(TEXT("ReverseFade"), [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumEnvFade&>(E).InputReverseFade(A); });
	});

static FElysiumClassRegistrar GRegFuncBrush(
	TEXT("func_brush"), ElysiumBaseClassName(), &MakeFuncBrush,
	[](FElysiumClassDesc& D)
	{
		D.Input(TEXT("Enable"),  [](FElysiumEntity& E, const FElysiumInputArgs&) { static_cast<FElysiumFuncBrush&>(E).InputEnable(); });
		D.Input(TEXT("Disable"), [](FElysiumEntity& E, const FElysiumInputArgs&) { static_cast<FElysiumFuncBrush&>(E).InputDisable(); });
		D.Input(TEXT("Toggle"),  [](FElysiumEntity& E, const FElysiumInputArgs&) { static_cast<FElysiumFuncBrush&>(E).InputToggle(); });
		AddLogicField(D, TEXT("Solidity"),      &FElysiumFuncBrush::Solidity);
		AddLogicField(D, TEXT("StartDisabled"), &FElysiumFuncBrush::bStartDisabled);
	});

static FElysiumClassRegistrar GRegPointTeleport(
	TEXT("point_teleport"), ElysiumBaseClassName(), &MakePointTeleport,
	[](FElysiumClassDesc& D)
	{
		D.Input(TEXT("Teleport"), [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumPointTeleport&>(E).InputTeleport(A); });
	});
