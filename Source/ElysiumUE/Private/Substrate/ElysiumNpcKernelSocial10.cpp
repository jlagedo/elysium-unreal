#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"

// Story 29d, family **Social10** — talking, the tweak file and the dialogue packet. See
// `Substrate/ElysiumNpcKernelSocial10.inl` for what this family is and where its six non-NPC rows
// live; the walked prose is `docs/vtmb/npc-ai/social.md`.

namespace
{
	// The census addresses of the bodies that fill slot 295, as `slots.md` records them.
	const TCHAR* const GSocial10Body_PayphoneCanTalk = TEXT("0x101aaee0");

	// `m_bfNPCStateFlags` bit 2 (`102c222c MOV EAX,[ESI+0x5b64] / SHR EAX,2 / TEST AL,1`) — the
	// per-state busy bit. `CPayphone::CanTalk` reads the same bit, and `ElysiumNpcKernelDialogue.cpp`
	// names it there; repeated as a literal here rather than reaching into that file's statics.
	constexpr uint8 GSocial10StateFlagBusy = 0x4;

	// `m_bfAINPCFlags & 0x00080000` (NO_DIALOG) and `m_bfAINPCFlags2 & 0x10000000`
	// (NO_DIALOG_PERSISTENT) — gates 8 and 12. They are read through `FElysiumNpc::NpcFlags` by
	// name, not by mask, because the flag words are decoded.

	// Slot 158 `IsAlive()` on an entity that may be an NPC or not. The port's slot 158 is an
	// `FElysiumNpc` virtual, so an NPC activator answers the ported body; anything else — a player,
	// most of all — has only `FElysiumEntity::IsInert()`, which is `bDead || bHidden`. Named rather
	// than hidden at the call site: slot 158 on a non-NPC entity is `CBaseEntity`'s own body
	// (`m_lifeState == 0`) and this substrate states it as the inert flag pair.
	bool Social10EntityIsAlive(const FElysiumEntity* Entity)
	{
		if (Entity == nullptr)
		{
			return false;
		}
		if (FElysiumNpc* Npc = const_cast<FElysiumEntity*>(Entity)->AsNpc())
		{
			return Npc->IsAlive();
		}
		return !Entity->IsInert();
	}

	// Retail's `Disposition_t`: `D_ER 0`, `D_HT 1`, `D_FR 2`, `D_LI 3`, `D_NU 4`. `CanTalk`'s last
	// gate refuses 1 and 2.
	constexpr int32 GSocial10_D_HT = 1;
	constexpr int32 GSocial10_D_FR = 2;

	// The eleven tweak-file keys, at their `.rdata` addresses, in the order `0x1029aa10` compares
	// them. `__strcmpi`, so the compare is case-insensitive.
	const TCHAR* const GSocial10Tweak_Capabilities = TEXT("CAPABILITIES");   // 0x105d976c
	const TCHAR* const GSocial10Tweak_Goals = TEXT("GOALS");                 // 0x105d96e8
	const TCHAR* const GSocial10Tweak_NpcPerception = TEXT("NPCPERCEPTION"); // 0x105d96a0
	const TCHAR* const GSocial10Tweak_Vision = TEXT("VISION");               // 0x105d9618
	const TCHAR* const GSocial10Tweak_Hearing = TEXT("HEARING");             // 0x105d957c
	const TCHAR* const GSocial10Tweak_Squad = TEXT("SQUAD");                 // 0x105d94ec
	const TCHAR* const GSocial10Tweak_IpGroups = TEXT("IPGROUPS");           // 0x105d94e0
	const TCHAR* const GSocial10Tweak_HintGroups = TEXT("HINTGROUPS");       // 0x105d94d0
	const TCHAR* const GSocial10Tweak_TpMoveTimer = TEXT("TPMOVETIMER");     // 0x105d94c0
	const TCHAR* const GSocial10Tweak_IgnoreAttack = TEXT("IGNOREATTACK");   // 0x105d94b0
	const TCHAR* const GSocial10Tweak_NoAlertState = TEXT("NOALERTSTATE");   // 0x105d94a0
}

// =================================================================================================
// Slot 295 `CanTalk` — `CAI_BaseNPCTroika::CanTalk` (`0x102c21c0`).
// =================================================================================================

bool FElysiumNpc::ActivatorControllerBusy(const FElysiumEntity* Activator) const
{
	// SEAM — see the `.inl`. `0x10175180` on the ACTIVATOR; the word it reads sits on the NPC here.
	(void)Activator;
	return false;
}

bool FElysiumNpc::CanTalkObserverPredicate(const FElysiumEntity* Activator) const
{
	// SEAM — see the `.inl`. `0x10146b20(activator, this)`, whose producers spec 0006 owns.
	(void)Activator;
	return true;
}

int32 FElysiumNpc::DialogMenuBlockWord() const
{
	// SEAM — see the `.inl`. `0x1023bd00()->+0x4ac`.
	return 0;
}

bool FElysiumNpc::IsInDialog() const
{
	// `CAI_BaseNPCTroika::IsInDialog` (`0x102c1170`).
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	return Dialogue.bInDialog || IsTalking(Now);
}

bool FElysiumNpc::CanTalk(FElysiumEntity* Activator)
{
	// Slot 295, `vtable +0x49c`. Retail fills it with two bodies: the Troika line's `0x102c21c0`
	// (60 census classes) and `CPayphone::CanTalk` (`0x101aaee0`), which story 29c-1's family
	// Dialogue already ported as `PayphoneCanTalk`. Dispatched here, not restated.
	if (const TCHAR* const SlotBody = ElysiumNpcKernelClass::BodyOf(RetailClass(), 295))
	{
		if (FCString::Strcmp(SlotBody, GSocial10Body_PayphoneCanTalk) == 0)
		{
			return PayphoneCanTalk(Activator);
		}
	}

	// The Troika-line body: fourteen gates, every failure falling out to `XOR AL,AL` at `102c22ad`
	// and only the innermost line reaching `MOV AL,1`. In the listing's order.

	// 1 and 2. `102c21c8` the activator is non-null, `102c21d0` `m_iDialog (+0x128)` is non-zero.
	//          CORRECTION: the checklist walk puts `m_iDialog` at `+0x5b64`; `vtmb_fields
	//          CAI_BaseNPCTroika` and the listing both put it at `+0x128`, which is the authored
	//          `dialogname` key.
	if (Activator == nullptr || DialogName.IsEmpty())
	{
		return false;
	}
	// 3. `102c21de` slot 158 `IsAlive()` on THIS NPC (`vtable +0x278`).
	if (!IsAlive())
	{
		return false;
	}
	// 4. `102c21ee` the same slot 158 on the ACTIVATOR — `MOV ECX,EDI`, a dispatch on the other
	//    object.
	if (!Social10EntityIsAlive(Activator))
	{
		return false;
	}
	// 5. `102c2200` `CBaseCombatCharacter::IsUnconscious` on this NPC — family Conditions10's
	//    `IsUnconsciousMiscFlag` (`0x10341aa0`), the misc-flag bit-0 read. Called, not restated.
	if (IsUnconsciousMiscFlag())
	{
		return false;
	}
	// 6. `102c220f` `thunk_FUN_100b5190(this)` — a seven-byte getter of `+0xf4 m_bScriptHidden`,
	//    which this runtime spells `IsHidden()`, the same reading `PayphoneCanTalk` took.
	if (IsHidden())
	{
		return false;
	}
	// 7. `102c221e` `m_bWillTalk (+0x1088)` is set. CORRECTION: the checklist walk puts it at
	//    `+0x128`.
	if (!bWillTalk)
	{
		return false;
	}
	// 8. `102c222c` bit 2 of `m_bfNPCStateFlags (+0x5b64)` is clear. CORRECTION: the walk puts the
	//    flag word at `+0x1088`.
	if ((NpcStateFlags() & GSocial10StateFlagBusy) != 0)
	{
		return false;
	}
	// 9. `102c2239` `m_bfAINPCFlags (+0x14b8) & 0x80000` — NO_DIALOG — is clear. Word ONE alone;
	//    `HasDialogSuppressFlag()` ORs in the persistent bit, which is gate 12 and a separate test.
	if (NpcFlags.Has(EElysiumNpcFlag::NO_DIALOG))
	{
		return false;
	}
	// 10. `102c2245` `IsInDialog()` (`0x102c1170`) is false.
	if (IsInDialog())
	{
		return false;
	}
	// 11. `102c2250` the ACTIVATOR's controller test `0x10175180` is false. SEAM.
	if (ActivatorControllerBusy(Activator))
	{
		return false;
	}
	// 12. `102c225b` `0x10146b20(activator, this)` is TRUE. SEAM, admitting.
	if (!CanTalkObserverPredicate(Activator))
	{
		return false;
	}
	// 13. `102c2267` `CBaseCombatCharacter::IsBusyWithDiscipline` is false — `0x1033e2b0`, the
	//     `D_IS_BUSY` bit, already a named predicate on this leaf.
	if (IsBusyWithDiscipline())
	{
		return false;
	}
	// 14. `102c2272` `m_bfAINPCFlags2 (+0x14bc) & 0x10000000` — NO_DIALOG_PERSISTENT — is clear.
	if (NpcFlags.Has(EElysiumNpcFlag2::NO_DIALOG_PERSISTENT))
	{
		return false;
	}
	// 15. `102c227e` the singleton `0x1023bd00()` is null OR its `+0x4ac` is zero. SEAM, admitting.
	if (DialogMenuBlockWord() != 0)
	{
		return false;
	}
	// 16. `102c2291` slot 404 `IRelationType` dispatched on THIS NPC with the activator as the
	//     argument — CORRECTION: the checklist walk reads the receiver as the activator. The answer
	//     must be neither `D_HT` (1) nor `D_FR` (2). Family Conditions10's slot-404 body answers.
	const int32 Relation = IRelationType(Activator);
	return Relation != GSocial10_D_HT && Relation != GSocial10_D_FR;
}

// =================================================================================================
// `CAI_BaseNPCTroika::FinishTalking` (`0x102c0ca0`).
// =================================================================================================

void FElysiumNpc::FinishTalking()
{
	// `102c0d0c` / `102c0d12`: the dialog partner handle `m_hDialogScene (+0x6554)` and the LATCH of
	// `m_bIsTalking (+0x64c0)`, read together and BEFORE anything is written. Every later arm reads
	// the live handle again; only the talking byte is latched, and it is what picks the notify.
	const bool bWasTalking = bIsTalking;

	FElysiumEntity* Partner = World != nullptr ? World->Resolve(Dialogue.DialogScene) : nullptr;
	if (Partner != nullptr)
	{
		// `102c0d79 MOV AL,[ECX + 0x498]` — the partner's "this speech scene has finished its line"
		// byte, which `FElysiumNpcDialogue::DialogSceneReportsDone` is the seam for.
		if (!Dialogue.DialogSceneReportsDone(*this))
		{
			// `102c0e5a` — `UTIL_Remove(partner)` (`0x101cd940`). A partner that has NOT reported
			// done is destroyed outright. SEAM: the scene is not the kernel's to destroy in this
			// runtime, so the request is counted and named.
			++DialogPartnerRemovals;
		}
		else
		{
			// `102c0db1` the partner's slot `0x3d4` (245), `102c0dee`
			// `CBaseEntity::ThinkSet(partner, 0x101c0b10, 0.0)` and `102c0e2f`
			// `partner->m_flNextThink (+0x17c) = curtime + 0.1`. SEAM, counted.
			++DialogPartnerStopRequests;
		}
	}

	// `102c0e62`..`102c0e8b`, unconditional and in this order.
	Dialogue.DialogScene = FElysiumEntityHandle::Invalid();   // +0x6554 = -1
	Dialogue.DialogQue.Reset();                               // +0x64ec m_szDialogQue[0] = 0
	bIsTalking = false;                                       // +0x64c0 = 0
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	// `+0x64cc = curtime`. NOT a clear: retail STAMPS the word with the current time, and
	// `CAI_BaseNPCTroika::IsTalking` (`0x102c0aa0`) tests `curtime < m_flTalkEnd` STRICTLY, so the
	// stamp's own instant already answers "not talking". `FElysiumNpc::IsTalking` spells the same
	// comparison with `<=` and the two disagree by exactly one instant — a difference
	// `ElysiumNpcDialogue.h` already records; the STAMP is what retail writes and is what lands.
	TalkingUntil = Now;
	// `102c0e8b` `CBaseEntity::ResetScriptedSoundOverrideEnt(this)`. SEAM: retail's
	// `m_iszScriptedSoundOverrideEnt` (`+0x0108`) has no member in this runtime — the shape map
	// binds no port word to that offset — so the reset is counted and named rather than faked.
	++ScriptedSoundOverrideResets;

	// `102c0e92 TEST BL,BL` — the LATCH, not the freshly cleared byte. Both arms first resolve
	// `UTIL_PlayerByIndex(1)` (`0x101cd9e0`) and return without notifying when there is no player;
	// `thunk_FUN_10178120` then reaches the conversation object hanging off that player.
	if (World == nullptr || World->FindPlayer() == nullptr)
	{
		LastFinishTalkingNotify = EFinishTalkingNotify::None;
		return;
	}
	// `102c0e97 JZ 0x102c0ebb` sends the CLEAR latch to `0x10015aeb`
	// (`CDialog::CallPendingNPCEventScript`) and falls through on the SET latch to `0x1001091f`
	// (`CDialog::NPCNotifyDoneTalking`).
	//
	// SEAM on the delivery side only, and the choice is the whole recovery: this runtime's dialogue
	// continuation is driven by `FElysiumEntityWorld::UpdateDialogueAutomatic`
	// (`ElysiumEntityWorldDialogue.cpp`), which the checklist verdicts `present` against
	// `0x100e4780` arm for arm — it already runs `FlushDialogueVoiceCompletion` (the
	// `CallPendingNPCEventScript` half) first and unconditionally, then the Pick/Release
	// continuation. Re-entering it from here would run the turn's continuation twice. What
	// `FinishTalking` contributes that the world does not is WHICH of the two was asked for, so that
	// is what it records.
	LastFinishTalkingNotify = bWasTalking
		? EFinishTalkingNotify::NpcNotifyDoneTalking        // `102c0eae` — 0x100e4780
		: EFinishTalkingNotify::CallPendingNpcEventScript;  // `102c0ed0` — 0x100e49b0
}

// =================================================================================================
// Slot 585 `ProcessTweakParam` — `CAI_BaseNPCTroika::ProcessTweakParam` (`0x1029aa10`).
// =================================================================================================

FElysiumNpc::ETweakParamKey FElysiumNpc::TweakParamKeyOf(const TCHAR* Key)
{
	if (Key == nullptr)
	{
		return ETweakParamKey::Unknown;
	}
	// `__strcmpi` against eleven literals, in exactly this order (`1029aa17` down to `1029acc1`).
	struct FRow { const TCHAR* Literal; ETweakParamKey Value; };
	static const FRow Rows[] =
	{
		{ GSocial10Tweak_Capabilities,  ETweakParamKey::Capabilities  },
		{ GSocial10Tweak_Goals,         ETweakParamKey::Goals         },
		{ GSocial10Tweak_NpcPerception, ETweakParamKey::NpcPerception },
		{ GSocial10Tweak_Vision,        ETweakParamKey::Vision        },
		{ GSocial10Tweak_Hearing,       ETweakParamKey::Hearing       },
		{ GSocial10Tweak_Squad,         ETweakParamKey::Squad         },
		{ GSocial10Tweak_IpGroups,      ETweakParamKey::IpGroups      },
		{ GSocial10Tweak_HintGroups,    ETweakParamKey::HintGroups    },
		{ GSocial10Tweak_TpMoveTimer,   ETweakParamKey::TpMoveTimer   },
		{ GSocial10Tweak_IgnoreAttack,  ETweakParamKey::IgnoreAttack  },
		{ GSocial10Tweak_NoAlertState,  ETweakParamKey::NoAlertState  },
	};
	for (const FRow& Row : Rows)
	{
		if (FCString::Stricmp(Key, Row.Literal) == 0)
		{
			return Row.Value;
		}
	}
	return ETweakParamKey::Unknown;
}

void FElysiumNpc::RecomputePerceptionDistances()
{
	// SEAM for `thunk_FUN_1028fb70` (`InitPerceptionDistances`) and `thunk_FUN_1028fc90`. This
	// runtime resolves the perception distances lazily off the three authored words, so a recompute
	// is stating that the resolve is stale.
	++PerceptionRecomputes;
	Senses.Perception.bResolved = false;
}

void FElysiumNpc::ProcessTweakParam(const TCHAR* Key, const TCHAR* Value)
{
	// Slot 585, the tweak-file key dispatch. `Value` is retail's `char*` and reaches `atoi`/`atof`
	// unchecked; a null one is the empty string here, which is what both answer 0 for.
	const FString ValueText = Value != nullptr ? FString(Value) : FString();

	switch (TweakParamKeyOf(Key))
	{
	case ETweakParamKey::Capabilities:
		// `1029aa31` — `DevMsg(2, "Maybe this should be in a func/input SetCapability!\n")`
		// (`0x105d972c`) and then `JMP 0x1029aa3d`, the IGNORE message. Recognised, and still
		// ignored: both messages print.
		UE_LOG(LogElysiumNpcEnt, Verbose,
			TEXT("Maybe this should be in a func/input SetCapability!"));
		break;
	case ETweakParamKey::Goals:
		// `1029aa67` — `DevMsg(2, "Hey foo!  You need to implement some goals!\n")` (`0x105d96b0`),
		// then the same fall-through.
		UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("Hey foo!  You need to implement some goals!"));
		break;

	case ETweakParamKey::NpcPerception:
	{
		// `1029aa85` `atoi` straight into `m_iNPCPerception (+0x63b0)`, then TWO clamps that each
		// re-read the field.
		AuthoredPerception = FCString::Atoi(*ValueText);
		if (AuthoredPerception < TweakParamPerceptionMin)
		{
			// `1029aa9e` — `Error("ProcessTweakParam:  %s - You specified an invalid NPCPERCEPTION "
			// "parameter (%d).  Must be between 1 and %d\n", GetDebugName(), value, 10)`
			// (`0x105d9620`). The `10` is a PUSHED LITERAL, not the field.
			//
			// NAMED CRASH GUARD, and the one divergence in this body: retail's `Error()` does not
			// return, so the clamp below is dead code in retail and the game exits. This runtime
			// logs and clamps, because a tweak file with a bad value must not take the process down.
			++TweakParamErrors;
			UE_LOG(LogElysiumNpcEnt, Error,
				TEXT("ProcessTweakParam:  %s - You specified an invalid NPCPERCEPTION parameter "
					 "(%d).  Must be between 1 and %d"),
				*DebugString(), AuthoredPerception, TweakParamPerceptionMax);
			AuthoredPerception = TweakParamPerceptionMin;
		}
		// `1029aabd` — the field is RE-READ, so the two clamps cannot both fire.
		if (AuthoredPerception > TweakParamPerceptionMax)
		{
			++TweakParamErrors;
			UE_LOG(LogElysiumNpcEnt, Error,
				TEXT("ProcessTweakParam:  %s - You specified an invalid NPCPERCEPTION parameter "
					 "(%d).  Must be between 1 and %d"),
				*DebugString(), AuthoredPerception, TweakParamPerceptionMax);
			AuthoredPerception = TweakParamPerceptionMax;
		}
		RecomputePerceptionDistances();
		break;
	}

	case ETweakParamKey::Vision:
	{
		// `1029ab12` `atof`, then `FCOM [0x104454c4]` and `FST [ESI + 0x63b4]` — the STORE happens
		// BEFORE the branch and on every path, so a refused value is still written.
		const float Parsed = FCString::Atof(*ValueText);
		AuthoredVision = Parsed;
		if (Parsed < TweakParamNegativeFloor && Parsed != TweakParamDeriveSentinel)
		{
			++TweakParamErrors;
			UE_LOG(LogElysiumNpcEnt, Error,
				TEXT("ProcessTweakParam:  %s - You specified a negative VISION parameter.  NPCs are "
					 "not allowed to view their inner-selves."), *DebugString());
		}
		// `1029abd7` — the two recomputes run on BOTH the error path and the accepting path.
		RecomputePerceptionDistances();
		break;
	}

	case ETweakParamKey::Hearing:
	{
		// `1029ab83` — the identical shape at `m_flHearingScalarBase (+0x63bc)`.
		const float Parsed = FCString::Atof(*ValueText);
		AuthoredHearing = Parsed;
		if (Parsed < TweakParamNegativeFloor && Parsed != TweakParamDeriveSentinel)
		{
			++TweakParamErrors;
			UE_LOG(LogElysiumNpcEnt, Error,
				TEXT("ProcessTweakParam:  %s - You specified a negative HEARING parameter.  Inside "
					 "of your ears there are drums."), *DebugString());
		}
		RecomputePerceptionDistances();
		break;
	}

	case ETweakParamKey::Squad:
		// `1029ac04` — `0x1029a930`, family Squad's `SetSquad`. Called, not restated.
		SetSquad(ValueText);
		break;
	case ETweakParamKey::IpGroups:
		// `1029ac28` — `0x10298910`, the interesting-place group list.
		SetInterestingPlaceGroups(ValueText);
		break;
	case ETweakParamKey::HintGroups:
		// `1029ac4c` — `0x102989e0`, the hint group list.
		SetHintGroups(ValueText);
		break;

	case ETweakParamKey::TpMoveTimer:
		// `1029ac6e` `atof`, `1029ac7c FADD [gpGlobals + 0xc]`, `1029ac80 FSTP [ESI + 0x65dc]`. An
		// ABSOLUTE curtime, which is how `ShouldThinkFrequently` reads the word.
		TeleportMoveTimer = static_cast<float>(
			FCString::Atof(*ValueText) + (World != nullptr ? World->NowSeconds() : 0.0));
		break;
	case ETweakParamKey::IgnoreAttack:
		// `1029aca2` `atoi != 0` into `m_bIgnoreDetectedAttack (+0x65f5)`.
		bIgnoreDetectedAttack = FCString::Atoi(*ValueText) != 0;
		break;
	case ETweakParamKey::NoAlertState:
		// `1029acd6` `atoi != 0` into `m_bNoAlertState (+0x65f6)`.
		bNoAlertState = FCString::Atoi(*ValueText) != 0;
		break;

	case ETweakParamKey::Unknown:
	default:
		break;
	}

	// `1029aa3d` — the ignore message is reached by `CAPABILITIES`, by `GOALS` and by every
	// unrecognised key, and by nothing else: each of the nine acting arms `RET`s from inside its own
	// block.
	switch (TweakParamKeyOf(Key))
	{
	case ETweakParamKey::Capabilities:
	case ETweakParamKey::Goals:
	case ETweakParamKey::Unknown:
		++TweakParamsIgnored;
		UE_LOG(LogElysiumNpcEnt, Verbose,
			TEXT("ProcessTweakParam(%s, %s) ignored by base class."),
			Key != nullptr ? Key : TEXT(""), *ValueText);
		break;
	default:
		break;
	}
}
