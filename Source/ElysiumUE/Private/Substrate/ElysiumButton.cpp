// func_button — the pressable switch (CBaseButton). 162 uses / 6 on the tutorial.
//
// Reference: animation_and_movers.md B.4 (the press → TriggerAndWait → return/latch cycle) + B.5,
// reconciled against the decompiled CBaseButton::Spawn (vampire.dll FUN_100c8d60). The spawnflag
// bits below are per-bit confirmed from that decompile — notably 0x100=TOUCH / 0x400=USE (VtMB
// matches stock Source, NOT swapped: the 0x400-armed handler FUN_100c9250 gates on
// CBaseEntity::PassesUseFilter, i.e. it is the +use path; all six tutorial buttons carry 0x400 +
// use_icon). It is a CBaseToggle mover (press-in via the base LinearMove); the physical slide is
// best-effort and untested because every exported func_button is DONTMOVE (the logical path).

#include "Substrate/ElysiumMover.h"

#include "ElysiumBrushComponent.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "Substrate/ElysiumClassFields.h"

class FElysiumButton final : public FElysiumMoverBase
{
public:
	// Button spawnflags (B.5 — decompiled, per-bit confirmed in FUN_100c8d60).
	static constexpr int32 SF_DONTMOVE = 0x1;      // pressed pose == rest pose (no physical slide)
	static constexpr int32 SF_TOGGLE   = 0x20;     // stays pressed until re-used (toggle up<->down)
	static constexpr int32 SF_TOUCH    = 0x100;    // touch-activates (m_pfnTouch handler)
	static constexpr int32 SF_DAMAGE   = 0x200;    // shootable (damage-activates; FUN_100c8b80)
	static constexpr int32 SF_USE      = 0x400;    // +use-activates (m_pfnUse; gated by PassesUseFilter)
	static constexpr int32 SF_LOCKED   = 0x800;    // starts locked (byte +0x5c4)
	static constexpr int32 SF_USEGATE  = 0x1000;   // secondary use-gate: activator must carry a flag (+0x5c5)

	// Keyfields (B.2). Speed is the press-in speed in Source in/s; wait is the pressed hold before
	// spring-back (-1 = latch open); lip trims the press-in travel.
	float Speed = 40.0f;
	float Wait  = 3.0f;
	float Lip   = 4.0f;
	bool  bLocked = false;

	// Explicit press sounds (B.2): `unlocked_sound` = the press-success WAV, `locked_sound` = the
	// press-denied WAV (direct sound-relative paths, e.g. environmental/electronic/button_beep.wav).
	// These take priority over the soundgroup on/off when set (a keypad ships explicit beeps, a switch
	// a soundgroup). Sound-relative, `\`→`/`.
	FString UnlockedSoundRel;
	FString LockedSoundRel;

	// The activator that last pressed the button — propagated onto OnPressed (Source m_hActivator).
	FElysiumEntityHandle LastActivator;

	enum class EState : uint8 { Rest, Pressing, Pressed, Returning };
	EState State() const { return ButtonState; }

	// --- Inputs (registered on func_button) --------------------------------------------
	void InputLock()   { bLocked = true; }
	void InputUnlock() { bLocked = false; }
	// A scripted press (ent_fire / a wire), bypassing the +use look-cursor's SF_USE gate — the way
	// the button is exercised before P4.4's full +use HUD, mirroring how doors expose Use.
	void InputPress(const FElysiumEntityHandle& Activator) { ActivateButton(Activator); }

	// --- +use look-cursor terminus (P4.2) ----------------------------------------------
	// Only USE-armed buttons (0x400) show the reticle / take a +use; touch-only buttons do not.
	virtual bool IsUsable() const override { return (SpawnFlags & SF_USE) != 0; }
	virtual bool IsUseLocked() const override { return bLocked; }   // locked_icon on the reticle (P4.4)
	virtual void OnUseCursorEnter() override
	{
		static const FName OnIn(TEXT("OnIn"));
		FireOutput(OnIn, FElysiumEntityHandle::Invalid());   // arms the reticle (player activator)
	}
	virtual void OnUseCursorLeave() override
	{
		static const FName OnOut(TEXT("OnOut"));
		FireOutput(OnOut, FElysiumEntityHandle::Invalid());
	}
	virtual void Use(const FElysiumEntityHandle& Activator) override { ActivateButton(Activator); }

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		static const TCHAR* StateNames[] = { TEXT("Rest"), TEXT("Pressing"), TEXT("Pressed"), TEXT("Returning") };
		Out.Emplace(TEXT("Button state"), StateNames[(uint8)ButtonState]);
		Out.Emplace(TEXT("Locked"), bLocked ? TEXT("yes") : TEXT("no"));
		Out.Emplace(TEXT("Latching"), (SpawnFlags & SF_TOGGLE) || Wait < 0.0f ? TEXT("yes (toggle / wait -1)") : TEXT("no (spring-back)"));
		TArray<FString> FlagNames;
		auto Add = [&](int32 Bit, const TCHAR* Name) { if (SpawnFlags & Bit) { FlagNames.Add(Name); } };
		Add(SF_DONTMOVE, TEXT("DONTMOVE")); Add(SF_TOGGLE, TEXT("TOGGLE")); Add(SF_TOUCH, TEXT("TOUCH"));
		Add(SF_DAMAGE, TEXT("DAMAGE")); Add(SF_USE, TEXT("USE")); Add(SF_LOCKED, TEXT("LOCKED"));
		Add(SF_USEGATE, TEXT("USEGATE"));
		Out.Emplace(TEXT("Spawnflags"), FString::Printf(TEXT("%d = %s"), SpawnFlags,
			FlagNames.Num() ? *FString::Join(FlagNames, TEXT(" | ")) : TEXT("(none)")));
		AppendSoundDebug(Out);
		if (!UnlockedSoundRel.IsEmpty()) { Out.Emplace(TEXT("Unlocked sound"), UnlockedSoundRel); }
		if (!LockedSoundRel.IsEmpty())   { Out.Emplace(TEXT("Locked sound"), LockedSoundRel); }
	}

	virtual void Spawn() override
	{
		bLocked = (SpawnFlags & SF_LOCKED) != 0;
		ButtonState = EState::Rest;
		// Mover sounds (P6.4): soundgroup on/off from usable/switches/<soundgroup>/ (RE: CBaseButton::
		// Spawn FUN_100c8810 reads "on"/"off"), plus the explicit locked/unlocked press WAVs. Buttons
		// have no SILENT spawnflag (0x1000 is USEGATE here), so pass 0.
		InitMoverSounds(TEXT("switches"), 0);
		auto Key = [this](const TCHAR* K) { return Def ? Def->Keys.FindRef(K).Replace(TEXT("\\"), TEXT("/")) : FString(); };
		UnlockedSoundRel = Key(TEXT("unlocked_sound"));
		LockedSoundRel   = Key(TEXT("locked_sound"));
		// Rest/pressed poses are captured lazily on the first press: BuildBrushBody runs after Spawn(),
		// so the body (and its bounds, needed for the press-in travel) does not exist yet here.
	}

	virtual void Think() override
	{
		const double Now = World ? World->NowSeconds() : 0.0;
		if (IsMoving())
		{
			TickMove(Now);
			return;
		}
		// The only resting think a button schedules is the pressed-state autoclose.
		if (ButtonState == EState::Pressed)
		{
			ButtonReturn();
		}
	}

protected:
	virtual void MoveDone() override
	{
		if (ButtonState == EState::Pressing)
		{
			ButtonState = EState::Pressed;
			TriggerAndWait();
		}
		else if (ButtonState == EState::Returning)
		{
			ButtonBackHome();
		}
	}

private:
	// The CBaseButton activation gate + direction pick (B.4). Locked/inert swallow it; otherwise a
	// rest button presses in, and a TOGGLE button that is already pressed springs back (re-use).
	void ActivateButton(const FElysiumEntityHandle& Activator)
	{
		if (IsInert())
		{
			return;   // hidden = disarmed
		}
		if (bLocked)
		{
			// Locked press: the deny sound (explicit `locked_sound`), no state change (P4.4 also shows
			// the locked_icon on the reticle). Like the door's locked path, this plays only the locked
			// sfx and fires no output (a locked door/button emits no OnLockedUse — that output is
			// prop_switch's alone).
			PlayMoverSoundRel(LockedSoundRel);
			return;
		}
		switch (ButtonState)
		{
		case EState::Rest:
			ButtonActivate(Activator);
			break;
		case EState::Pressed:
			if (SpawnFlags & SF_TOGGLE)
			{
				ButtonReturn();   // toggle release
			}
			break;
		case EState::Pressing:
		case EState::Returning:
			break;                // mid-motion re-use is ignored (no NO_AUTO_RETURN equivalent here)
		}
	}

	void ButtonActivate(const FElysiumEntityHandle& Activator)
	{
		LastActivator = Activator;
		EnsurePositions();
		// Press sound: the explicit `unlocked_sound` (a keypad beep) if set, else the soundgroup `on`
		// (RE: CBaseButton plays the "on" index DAT_106e6f78 on press). The spring-back plays `off`.
		static const FName On(TEXT("on"));
		if (!UnlockedSoundRel.IsEmpty()) { PlayMoverSoundRel(UnlockedSoundRel); }
		else                            { PlayMoverSound(On); }
		if ((SpawnFlags & SF_DONTMOVE) || !Body)
		{
			ButtonState = EState::Pressed;
			TriggerAndWait();   // no physical slide — fire immediately (the tutorial path)
		}
		else
		{
			ButtonState = EState::Pressing;
			LinearMove(PressedLoc, Speed * MoverInchToCm);   // MoveDone -> TriggerAndWait
		}
	}

	// At the pressed pose: fire OnPressed, then either latch (TOGGLE or wait -1) or schedule the
	// spring-back after `wait` seconds on the substrate clock (R4, no engine timer).
	void TriggerAndWait()
	{
		static const FName OnPressed(TEXT("OnPressed"));
		FireOutput(OnPressed, LastActivator);

		if ((SpawnFlags & SF_TOGGLE) || Wait < 0.0f)
		{
			NextThink = ELYSIUM_NEVER_THINK;   // stays pressed (toggle: until re-used; wait -1: latched)
		}
		else
		{
			NextThink = (World ? World->NowSeconds() : 0.0) + Wait;
		}
	}

	void ButtonReturn()
	{
		static const FName Off(TEXT("off"));
		PlayMoverSound(Off);   // spring-back / toggle-release sound (RE: "off" index DAT_106e6f7c)
		if ((SpawnFlags & SF_DONTMOVE) || !Body)
		{
			ButtonState = EState::Returning;
			ButtonBackHome();
		}
		else
		{
			ButtonState = EState::Returning;
			LinearMove(RestLoc, Speed * MoverInchToCm);   // MoveDone -> ButtonBackHome
		}
	}

	void ButtonBackHome()
	{
		ButtonState = EState::Rest;
		NextThink = ELYSIUM_NEVER_THINK;   // re-armed; waits for the next press
	}

	// Capture the rest pose and derive the pressed pose once the body exists. DONTMOVE (every
	// exported button) collapses the two. The movedir derivation matches the door's shared
	// SetMovedir helper; the moving path remains unexercised by shipped data — no exported
	// func_button clears DONTMOVE.
	void EnsurePositions()
	{
		if (bPositionsCached || !Body)
		{
			return;
		}
		bPositionsCached = true;
		RestLoc = Body->GetRelativeLocation();
		if (SpawnFlags & SF_DONTMOVE)
		{
			PressedLoc = RestLoc;
			return;
		}
		// movedir from `angles` (Unreal space) — the same shared helper the sliding door derives
		// its slide direction through, sentinels and the Source→Unreal Y reflection included.
		const FVector Dir = SourceAnglesToUnrealDir(Angles);
		// Travel = the body's depth along movedir minus the lip (both in cm). Bounds are world-axis;
		// good enough for the axis-aligned press this approximates until content exercises it.
		const FVector Ext = Body->Bounds.BoxExtent;
		const double Depth = 2.0 * FMath::Abs(FVector::DotProduct(Ext, Dir.GetAbs()));
		const double Travel = FMath::Max(0.0, Depth - Lip * MoverInchToCm);
		PressedLoc = RestLoc + Dir * Travel;
	}

	EState  ButtonState = EState::Rest;
	bool    bPositionsCached = false;
	FVector RestLoc = FVector::ZeroVector;
	FVector PressedLoc = FVector::ZeroVector;
};

static TUniquePtr<FElysiumEntity> MakeFuncButton() { return MakeUnique<FElysiumButton>(); }

// func_button is concrete (unlike CBaseDoor). Registered directly on the base chain; when
// func_rot_button lands it derives from FElysiumButton the same way the door leaves derive from
// CBaseDoor. Press/Lock/Unlock are the stock CBaseButton inputs; the +use path is driven by the
// look-cursor (IsUsable/Use), so no Use input is registered here.
static FElysiumClassRegistrar GRegFuncButton(
	TEXT("func_button"), ElysiumBaseClassName(), &MakeFuncButton,
	[](FElysiumClassDesc& D)
	{
		D.Input(TEXT("Press"),  [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumButton&>(E).InputPress(A.Activator); });
		D.Input(TEXT("Lock"),   [](FElysiumEntity& E, const FElysiumInputArgs&)   { static_cast<FElysiumButton&>(E).InputLock(); });
		D.Input(TEXT("Unlock"), [](FElysiumEntity& E, const FElysiumInputArgs&)   { static_cast<FElysiumButton&>(E).InputUnlock(); });
		ElysiumAddClassField(D, TEXT("speed"), &FElysiumButton::Speed);
		ElysiumAddClassField(D, TEXT("wait"),  &FElysiumButton::Wait);
		ElysiumAddClassField(D, TEXT("lip"),   &FElysiumButton::Lip);
	});
