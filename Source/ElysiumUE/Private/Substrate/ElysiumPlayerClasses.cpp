// The registration site for the player entity and the two chain nodes above it (S3).
//
// `docs/vtmb/script_api.md` is the input inventory. The three classes here are ordinary registry
// nodes: nothing about the
// player is special-cased — `pc.MoneyAdd(50)` from a level script, `MoneyAdd` on a Hammer wire and
// `elysium.ent_fire !player MoneyAdd 50` from the console are one input, reached by one R2 walk.
//
// The three implementations live beside this file, one class per `.cpp`:
// `ElysiumAnimatingImpl.cpp`, `ElysiumCombatCharacter.cpp` and `ElysiumPlayerEntity.cpp`. What
// remains here is the chain's field/input/output declaration, the datamap helpers those
// declarations share, and `FElysiumSheet`'s clan naming.
//
// Economy, inventory/barter, disposition reactions and the look-at rig are not implemented here.
// Their inputs register and log; the field they write is already in place.

#include "ElysiumPlayer.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumSheetSlots.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumClassFields.h"
#include "Substrate/ElysiumDisciplines.h"
#include "Substrate/ElysiumItemClasses.h"   // FElysiumItem — Holster's carried-weapon fallback
#include "Substrate/ElysiumPendingInput.h"
#include "Substrate/ElysiumPlayerLog.h"

// The one category the whole chain writes on. Declared in `Substrate/ElysiumPlayerLog.h` because
// the implementations are four files; defined here, at the registration site the chain is named by.
DEFINE_LOG_CATEGORY(LogElysiumPlayer);

namespace
{
	// Indexed by the level-script encoding pc.clan uses: 2 = Brujah ... 8 = Ventrue.
	const TCHAR* GClanNames[] = { TEXT("?"), TEXT("?"), TEXT("Brujah"), TEXT("Gangrel"),
		TEXT("Malkavian"), TEXT("Nosferatu"), TEXT("Toreador"), TEXT("Tremere"), TEXT("Ventrue") };
	constexpr int32 GClanMin = 2;
	constexpr int32 GClanMax = 8;

	// One trait slot on FElysiumCombatCharacter::Sheet, as VtMB's datamap exposes it: the current
	// value under the bare name, the base under a `base_` prefix. Both halves are keyable and both
	// are saved, which is what the recovered datamap flags say.
	void AddSlotField(FElysiumClassDesc& D, const TCHAR* Name,
		EElysiumTraitContainer Container, int32 Slot, bool bBase)
	{
		FElysiumFieldAccessor Acc;
		Acc.ApplyFlags(ElysiumFieldDefault);
		Acc.Type = EElysiumVariantType::Int;
		Acc.Get = [Container, Slot, bBase](const FElysiumEntity& E)
		{
			const FElysiumSheet& S = static_cast<const FElysiumCombatCharacter&>(E).Sheet;
			return FElysiumVariant::Int(bBase ? S.GetBase(Container, Slot) : S.GetCurrent(Container, Slot));
		};
		Acc.Set = [Container, Slot](FElysiumEntity& E, const FElysiumVariant& V)
		{
			// A write lands on the base either way: a keyvalue and a script assignment both set the
			// character sheet, and the current value is derived from it.
			static_cast<FElysiumCombatCharacter&>(E).Sheet.SetBase(Container, Slot, V.ToInt());
		};
		D.Fields.Add(FName(Name), MoveTemp(Acc));
	}

	// Every compiled slot in every container, twice over. 74 slots -> 148 fields on
	// CBaseCombatCharacter, which is the whole sheet reachable through one R2 walk.
	void AddSheetFields(FElysiumClassDesc& D)
	{
		for (uint8 i = 0; i < (uint8)EElysiumTraitContainer::Count; ++i)
		{
			const EElysiumTraitContainer Container = (EElysiumTraitContainer)i;
			for (const FElysiumSheetSlot& Slot : ElysiumSheetSlots(Container))
			{
				AddSlotField(D, Slot.Datamap, Container, Slot.Index, /*bBase=*/false);
				AddSlotField(D, *FString::Printf(TEXT("base_%s"), Slot.Datamap),
					Container, Slot.Index, /*bBase=*/true);
				if (Slot.Alias)
				{
					AddSlotField(D, Slot.Alias, Container, Slot.Index, /*bBase=*/false);
					AddSlotField(D, *FString::Printf(TEXT("base_%s"), Slot.Alias),
						Container, Slot.Index, /*bBase=*/true);
				}
			}
		}
	}

	// The same shape for FElysiumPlayer::Law, read-only (the SetCriminalLevel family writes it).
	void AddLawField(FElysiumClassDesc& D, const TCHAR* Name, int32 FElysiumLawState::* Member)
	{
		FElysiumFieldAccessor Acc;
		// Neither keyable nor saved: the setter is a deliberate no-op, and the counters' durable home
		// is FElysiumPlayerRecord::Law in the Player block, not the entity field walk.
		Acc.ApplyFlags(EElysiumField::None);
		Acc.Type = EElysiumVariantType::Int;
		Acc.Get = [Member](const FElysiumEntity& E)
		{
			return FElysiumVariant::Int(static_cast<const FElysiumPlayer&>(E).Law.*Member);
		};
		Acc.Set = [](FElysiumEntity&, const FElysiumVariant&) {};
		D.Fields.Add(FName(Name), MoveTemp(Acc));
	}

	// The feed transaction's state, as Save-flagged chain fields (K8: a registered field, the
	// session record, or a declared save block, and nothing else). The save schema names
	// `m_flNextFeedPulse` and `m_flFeedStartTime`, which is what makes an in-progress feed survive a
	// restore without duplicating a pulse; the rest of the block is registered beside them so the
	// victim link and the accelerating interval come back with it. None of them is keyable — no map
	// authors a feed — so the whole block is engine-written and save-enumerated only.
	void AddFeedFields(FElysiumClassDesc& D)
	{
		using FC = FElysiumCombatCharacter;

		auto AddFloat = [&D](const TCHAR* Name, float FElysiumFeedState::* Member)
		{
			FElysiumFieldAccessor Acc;
			Acc.ApplyFlags(EElysiumField::Save);
			Acc.Type = EElysiumVariantType::Float;
			Acc.Get = [Member](const FElysiumEntity& E)
			{
				return FElysiumVariant::Float(static_cast<const FC&>(E).FeedState.*Member);
			};
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V)
			{
				static_cast<FC&>(E).FeedState.*Member = V.ToFloat();
			};
			D.Fields.Add(FName(Name), MoveTemp(Acc));
		};
		auto AddInt = [&D](const TCHAR* Name, int32 FElysiumFeedState::* Member)
		{
			FElysiumFieldAccessor Acc;
			Acc.ApplyFlags(EElysiumField::Save);
			Acc.Type = EElysiumVariantType::Int;
			Acc.Get = [Member](const FElysiumEntity& E)
			{
				return FElysiumVariant::Int(static_cast<const FC&>(E).FeedState.*Member);
			};
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V)
			{
				static_cast<FC&>(E).FeedState.*Member = V.ToInt();
			};
			D.Fields.Add(FName(Name), MoveTemp(Acc));
		};
		auto AddBool = [&D](const TCHAR* Name, bool FElysiumFeedState::* Member)
		{
			FElysiumFieldAccessor Acc;
			Acc.ApplyFlags(EElysiumField::Save);
			Acc.Type = EElysiumVariantType::Bool;
			Acc.Get = [Member](const FElysiumEntity& E)
			{
				return FElysiumVariant::Bool(static_cast<const FC&>(E).FeedState.*Member);
			};
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V)
			{
				static_cast<FC&>(E).FeedState.*Member = V.ToInt() != 0;
			};
			D.Fields.Add(FName(Name), MoveTemp(Acc));
		};
		auto AddHandle = [&D](const TCHAR* Name, FElysiumEntityHandle FElysiumFeedState::* Member)
		{
			// A handle field, like `m_hActiveWeapon`: the applier re-stamps the saved index against
			// the live epoch, and an index that no longer exists reads Invalid.
			FElysiumFieldAccessor Acc;
			Acc.ApplyFlags(EElysiumField::Save);
			Acc.Type = EElysiumVariantType::Handle;
			Acc.Get = [Member](const FElysiumEntity& E)
			{
				return FElysiumVariant::Handle(static_cast<const FC&>(E).FeedState.*Member);
			};
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V)
			{
				static_cast<FC&>(E).FeedState.*Member = V.ToHandle();
			};
			D.Fields.Add(FName(Name), MoveTemp(Acc));
		};

		AddFloat(TEXT("m_flNextFeedPulse"), &FElysiumFeedState::NextPulse);
		AddFloat(TEXT("m_flFeedStartTime"), &FElysiumFeedState::StartTime);
		// The interval at +0x1494 and the counter at +0x14a0 are recovered by offset; the save
		// schema's own names for them are not, so the spelling here follows the two it does name.
		AddFloat(TEXT("m_flFeedInterval"),  &FElysiumFeedState::Interval);
		AddInt(TEXT("m_iBloodStolen"),      &FElysiumFeedState::BloodStolen);
		AddHandle(TEXT("m_hFeedTarget"),    &FElysiumFeedState::Target);
		AddBool(TEXT("m_bFeedContinue"),    &FElysiumFeedState::bContinuation);
		// The pairing half. Retail keeps the peer on the common paired-action state; this runtime
		// has no grapple router, so the link and the phase schedule are ours and are named as such.
		AddHandle(TEXT("m_hFeedPeer"),      &FElysiumFeedState::Peer);
		AddBool(TEXT("m_bFeedVictim"),      &FElysiumFeedState::bVictim);
		AddBool(TEXT("m_bFeedFroze"),       &FElysiumFeedState::bFrozenByFeed);
		AddFloat(TEXT("m_flFeedPhaseEnd"),  &FElysiumFeedState::PhaseDeadline);
		{
			FElysiumFieldAccessor Acc;
			Acc.ApplyFlags(EElysiumField::Save);
			Acc.Type = EElysiumVariantType::Int;
			Acc.Get = [](const FElysiumEntity& E)
			{
				return FElysiumVariant::Int(
					static_cast<int32>(static_cast<const FC&>(E).FeedState.Phase));
			};
			Acc.Set = [](FElysiumEntity& E, const FElysiumVariant& V)
			{
				static_cast<FC&>(E).FeedState.Phase =
					static_cast<EElysiumFeedPhase>(FMath::Clamp(V.ToInt(), 0,
						static_cast<int32>(EElysiumFeedPhase::ReleaseTail)));
			};
			D.Fields.Add(FName(TEXT("m_iFeedPhase")), MoveTemp(Acc));
		}
	}
}

bool FElysiumSheet::IsValidClan(int32 Clan)
{
	return Clan >= GClanMin && Clan <= GClanMax;
}

const TCHAR* FElysiumSheet::ClanName(int32 Clan)
{
	return IsValidClan(Clan) ? GClanNames[Clan] : TEXT("(unset)");
}

int32 FElysiumSheet::ClanFromName(const FString& Name)
{
	// A bare number is taken as the 2..8 encoding directly, so both forms work.
	if (Name.IsNumeric())
	{
		const int32 N = FCString::Atoi(*Name);
		return (N >= GClanMin && N <= GClanMax) ? N : 0;
	}
	for (int32 i = GClanMin; i <= GClanMax; ++i)
	{
		if (Name.Equals(GClanNames[i], ESearchCase::IgnoreCase))
		{
			return i;
		}
	}
	return 0;
}

static TUniquePtr<FElysiumEntity> MakePlayer() { return MakeUnique<FElysiumPlayer>(); }
static TUniquePtr<FElysiumEntity> MakeViewModel() { return MakeUnique<FElysiumAnimating>(); }

// CBaseAnimating — a chain node, never a `.ents` classname, so it needs no factory.
static FElysiumClassRegistrar GRegAnimating(
	ElysiumAnimatingClassName(), ElysiumBaseClassName(), nullptr,
	[](FElysiumClassDesc& D)
	{
		// `skin` is KEY and INPUT with a null inputFunc — the keyvalue, the wire and `.skin =` are
		// the same direct write, so the field alone serves all three (entity_io.md).
		ElysiumAddClassField(D, TEXT("skin"), &FElysiumAnimating::Skin);
		ElysiumAddClassField(D, TEXT("default_disposition"), &FElysiumAnimating::Disposition);
		// Project save-only companion for SetDisposition's second argument. It is deliberately not a
		// script field: retail exposes the pair through the method, not as two writable attributes.
		ElysiumAddClassField(D, TEXT("elysium_disposition_level"),
			&FElysiumAnimating::DispositionLevel, EElysiumField::Save);

		D.Input(TEXT("SetAnimation"), [](FElysiumEntity& E, const FElysiumInputArgs& A)
			{
				// VtMB's SetAnimation sets the model's *current* sequence rather than firing a
				// one-shot: the arguments the corpus passes are resting poses (`cower_idle`,
				// `dance0N`) that have to persist, so it loops.
				E.PlayAnimClip(A.Param.ToString(), /*bLoop=*/true);
			});
	});

// Engine-owned first-person slots. They are real entities because patch Python finds them through
// the ordinary class lookup and writes `model` on slot 3. Their visual bodies remain owned by the
// first-person viewmodel programme; a bodiless entity is the faithful API boundary in the meantime.
static FElysiumClassRegistrar GRegViewModel(
	ElysiumViewModelClassName(), ElysiumAnimatingClassName(), &MakeViewModel,
	[](FElysiumClassDesc&) {});

// CBaseCombatCharacter — datamap 0x1061664c, 25 inputs (`docs/vtmb/script_api.md`).
static FElysiumClassRegistrar GRegCombatCharacter(
	ElysiumCombatCharacterClassName(), ElysiumAnimatingClassName(), nullptr,
	[](FElysiumClassDesc& D)
	{
		using FC = FElysiumCombatCharacter;

		// The eight with a field behind them.
		D.Input(TEXT("MoneyAdd"),              [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FC&>(E).InputMoneyAdd(A); });
		D.Input(TEXT("MoneyRemove"),           [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FC&>(E).InputMoneyRemove(A); });
		D.Input(TEXT("HumanityAdd"),           [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FC&>(E).InputHumanityAdd(A); });
		D.Input(TEXT("ChangeMasqueradeLevel"), [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FC&>(E).InputChangeMasqueradeLevel(A); });
		D.Input(TEXT("Bloodloss"),             [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FC&>(E).InputBloodloss(A); });
		D.Input(TEXT("Bloodgain"),             [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FC&>(E).InputBloodgain(A); });
		D.Input(TEXT("BloodHeal"),             [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FC&>(E).InputBloodHeal(A); });
		D.Input(TEXT("WillTalk"),              [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FC&>(E).InputWillTalk(A); });
		// CLASSPTR — an entity-valued detach that never destroys.
		D.Input(TEXT("Inventory_Remove"),      [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FC&>(E).InputInventoryRemove(A); });

		// The sixteen whose system has not landed. They register so the name resolves through the
		// R2 walk and reaches a defined place — fail-closed, not missing. An input thunk is a
		// captureless function pointer, so each row states its own name and owner.
		ELYSIUM_PENDING_INPUT(FC, FrenzyTrigger,          "P13 — disciplines and frenzy");
		ELYSIUM_PENDING_INPUT(FC, FrenzyCheck,            "P13 — disciplines and frenzy");
		// HungerCheck shares FrenzyCheck's handler in VtMB — two external names, one behaviour.
		ELYSIUM_PENDING_INPUT(FC, HungerCheck,            "P13 — disciplines and frenzy");
		ELYSIUM_PENDING_INPUT(FC, FrenzyUpdate,           "P13 — disciplines and frenzy");

		// The one real teardown. `vdiscipline_endall` and this input converge on it: remove the
		// owned expiry events, drop every trait-effect group both families installed, zero the
		// thirteen active slots and recompute.
		D.Input(TEXT("ClearActiveDisciplines"), [](FElysiumEntity& E, const FElysiumInputArgs&)
			{ ElysiumDisciplines::ClearAll(static_cast<FC&>(E)); });

		// The owned Discipline expiry, delivered to `!self` through the one queue (R4/K11). It is a
		// project-owned input rather than a recovered datamap name — no recovered
		// CBaseCombatCharacter input carries an expiry — and it is registered on the chain so the
		// queue, the inspector and the save all see one shape, the way the weapon commit is.
		D.Input(ElysiumDisciplines::ExpiryInput(), [](FElysiumEntity& E, const FElysiumInputArgs& A)
			{ ElysiumDisciplines::CommitExpiry(static_cast<FC&>(E), A.Param.ToInt()); });
		ELYSIUM_PENDING_INPUT(FC, BarterBegin,            "9.8 — barter");
		ELYSIUM_PENDING_INPUT(FC, BarterEnd,              "9.8 — barter");
		ELYSIUM_PENDING_INPUT(FC, PlayFloat,              "8.9 — the floating HUD readout");
		// The four `SetAsCameraTarget` wires (SC3). `Set*` pass a zero fade and `Fade*` pass the
		// wire's own value; all four broadcast the character onto the player's camera-TARGET
		// channel, which — unlike the view channel — does not cancel a live cine shot. Head picks
		// `CalcLookData` and body picks `WorldSpaceCenter()` for the published aim point.
		// `sm_hub_1` fires `!playercontroller.SetBodyAsCameraTarget`, so this is shipped content.
		D.Input(TEXT("SetHeadAsCameraTarget"),  [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FC&>(E).InputSetHeadAsCameraTarget(A); });
		D.Input(TEXT("SetBodyAsCameraTarget"),  [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FC&>(E).InputSetBodyAsCameraTarget(A); });
		D.Input(TEXT("FadeHeadAsCameraTarget"), [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FC&>(E).InputFadeHeadAsCameraTarget(A); });
		D.Input(TEXT("FadeBodyAsCameraTarget"), [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FC&>(E).InputFadeBodyAsCameraTarget(A); });
		// The four scripted look-at inputs. Center is registered separately from Eye even though it
		// behaves identically, because the identical behaviour is retail's own defect rather than a
		// simplification of ours — see InputLookAtEntityCenter.
		D.Input(TEXT("LookAtEntityEye"),     [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FC&>(E).InputLookAtEntityEye(A); });
		D.Input(TEXT("LookAtEntityCenter"),  [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FC&>(E).InputLookAtEntityCenter(A); });
		D.Input(TEXT("LookAtEntityOrigin"),  [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FC&>(E).InputLookAtEntityOrigin(A); });
		D.Input(TEXT("LookAtEntityDefault"), [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FC&>(E).InputLookAtEntityDefault(A); });

		// `money` is `m_iMoney`, the one counter `stats.txt` does not carry as a Stat. Humanity,
		// blood, masquerade, clan and sex are all trait slots, and arrive with the rest of the sheet.
		ElysiumAddClassField(D, TEXT("money"), &FC::Money);

		// `trigger_stealth_mod`'s raw aggregate (`+0x1084`). Registered on THIS chain node
		// rather than on the player, because the trigger's own increment is guarded by
		// combat-character embodiment and every character can therefore carry a contribution. Saved
		// through the ordinary field walk, which is what carries an NPC's across a map snapshot;
		// the player entity is excluded from that snapshot, so its copy rides the player record
		// beside the surface it feeds.
		ElysiumAddClassField(D, TEXT("m_nRawStealthModifier"), &FC::StealthModRaw);

		// The camera-target pair, at the datamap names retail carries them under (`+0x10d0` and
		// `+0x10d4`, both in `datamap_CBaseCombatCharacter_builder`). Engine-written by
		// `SetAsCameraTarget`, never authored — the same Save-only posture the stance pair takes.
		ElysiumAddClassField(D, TEXT("m_flCameraOverrideFadeTime"), &FC::CameraOverrideFadeTime,
			EElysiumField::Save);
		ElysiumAddClassField(D, TEXT("m_bCameraTargetIsHead"), &FC::bCameraTargetIsHead,
			EElysiumField::Save);

		// The runtime's authoritative once-only death latch. Saving it is required by npc_maker's
		// owner notification: a dead child restored and later Kill'd must not refund a live slot.
		{
			FElysiumFieldAccessor Acc;
			Acc.ApplyFlags(EElysiumField::Save);
			Acc.Type = EElysiumVariantType::Bool;
			Acc.Get = [](const FElysiumEntity& E)
			{
				return FElysiumVariant::Bool(static_cast<const FC&>(E).HasReportedDeath());
			};
			Acc.Set = [](FElysiumEntity& E, const FElysiumVariant& V)
			{
				static_cast<FC&>(E).SetDeathReportedForRestore(V.ToInt() != 0);
			};
			D.Fields.Add(FName(TEXT("m_bDeathReported")), MoveTemp(Acc));
		}

		// The active-weapon handle (+0x19a4). Saved as a handle so the equipped item survives a
		// restore; the 224-slot list beside it is re-derived from the items' own owner/position
		// fields (FElysiumInventory::RebuildFrom), so only this one needs a field.
		{
			FElysiumFieldAccessor Acc;
			Acc.ApplyFlags(ElysiumFieldDefault);
			Acc.Type = EElysiumVariantType::Handle;
			Acc.Get = [](const FElysiumEntity& E)
			{
				return FElysiumVariant::Handle(static_cast<const FC&>(E).Inventory.ActiveWeapon);
			};
			Acc.Set = [](FElysiumEntity& E, const FElysiumVariant& V)
			{
				static_cast<FC&>(E).Inventory.ActiveWeapon = V.ToHandle();
			// A script writing `m_hActiveWeapon` is an equip like any other, so it re-arbitrates.
			static_cast<FC&>(E).PublishEquippedCameraClass();
			};
			D.Fields.Add(FName(TEXT("m_hActiveWeapon")), MoveTemp(Acc));
		}

		// The gaze state, at the offsets the datamap carries them: the commanded and smoothed eye
		// targets and the integration rate. `m_hEyeLookTarget` is a handle, which the registry has no
		// field type for, so the saved form is the targetname a restore would have to re-resolve
		// anyway.
		ElysiumAddClassField(D, TEXT("m_vEyeLookTarget"), &FC::EyeLookTarget);
		ElysiumAddClassField(D, TEXT("m_vCurEyeTarget"), &FC::CurEyeTarget);
		ElysiumAddClassField(D, TEXT("m_flEyeIntegRate"), &FC::EyeIntegRate);
		ElysiumAddClassField(D, TEXT("m_hEyeLookTarget"), &FC::EyeLookTargetName);
		// The scripted-mode int sits at 0x0E68 and retail's datamap does NOT carry it, so a scripted
		// look-at does not survive a save. Registered with no flags so it is inspectable but neither
		// keyable nor saved, which reproduces that exactly.
		ElysiumAddClassField(D, TEXT("m_iEyeLookMode"), &FC::EyeLookMode, EElysiumField::None);

		AddFeedFields(D);
		AddSheetFields(D);
	});

// The player. Its classname is VtMB's own (`player`); nothing in a `.ents` file carries it, because
// the player is created by the engine at map build, not authored into the map.
static FElysiumClassRegistrar GRegPlayer(
	ElysiumPlayerClassName(), ElysiumCombatCharacterClassName(), &MakePlayer,
	[](FElysiumClassDesc& D)
	{
		using FP = FElysiumPlayer;

		D.Input(TEXT("GiveItem"),             [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FP&>(E).InputGiveItem(A); });
		D.Input(TEXT("AwardExperience"),      [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FP&>(E).InputAwardExperience(A); });
		D.Input(TEXT("SetCriminalLevel"),     [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FP&>(E).InputSetCriminalLevel(A); });
		D.Input(TEXT("SetInvestigateLevel"),  [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FP&>(E).InputSetInvestigateLevel(A); });
		D.Input(TEXT("SetSupernaturalLevel"), [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FP&>(E).InputSetSupernaturalLevel(A); });

		// The rest of the recovered ten: no system yet. (The datamap header states 11 inputs and
		// only 10 were recovered from the builder dump — the eleventh is still unidentified,
		// `docs/vtmb/script_api.md`.)
		D.Input(TEXT("Whisper"),         [](FElysiumEntity& E, const FElysiumInputArgs& A)
			{
				if (!E.World || !E.World->Audio())
				{
					return;
				}
				FElysiumAudioRequest Request;
				Request.Source = FElysiumAudioSource::Event(
					EElysiumAudioSourceDomain::Whisper, A.Param.ToString());
				Request.Owner.Kind = EElysiumAudioOwnerKind::GameplaySystem;
				Request.Owner.StableId =
					FString::Printf(TEXT("player.whisper:%u:%d"), E.Handle.Epoch, E.Handle.Index);
				Request.Category = EElysiumAudioCategory::Dialogue;
				Request.Placement.bSpatialized = false;
				Request.Routing = EElysiumAudioRouting::NoGameplayNoise;
				Request.ConcurrencyKey = TEXT("player.whisper");
				E.World->Audio()->Submit(MoveTemp(Request));
			});
		// RemoveCamera — the other half of `SetCamera`: hand the view back to the player. It clears
		// the map's one scripted camera whether a script, a wire or the theatre put it up.
		D.Input(TEXT("RemoveCamera"),    [](FElysiumEntity& E, const FElysiumInputArgs&)
			{ if (E.World) { E.World->ClearScriptedCamera(); } });
		D.Input(TEXT("PlayHUDParticle"), [](FElysiumEntity& E, const FElysiumInputArgs& A)
			{ static_cast<FP&>(E).PendingInput(TEXT("PlayHUDParticle"), TEXT("8.9 — the HUD"), A); });
		D.Input(TEXT("StopHUDParticle"), [](FElysiumEntity& E, const FElysiumInputArgs& A)
			{ static_cast<FP&>(E).PendingInput(TEXT("StopHUDParticle"), TEXT("8.9 — the HUD"), A); });
		// Holster — put the drawn weapon away. Retail's own semantics are UNRECOVERED: the world's
		// forced-unarmed policy (`combat-and-damage.md` § "World-area weapon admission") equips
		// `item_w_unarmed` and refuses every other candidate, and that is the only observed behaviour
		// that reads as holstering. So this does exactly that much — switch the active weapon to a
		// carried `item_w_unarmed` — and refuses audibly when the character carries none, rather
		// than inventing an empty-handed state the inventory has no representation for. The switch
		// itself goes through `SetActiveWeapon`, the one equip funnel `ElysiumInventorySelect.cpp`'s
		// selector also commits through: reimplementing its outgoing/incoming pair by hand here
		// silently dropped `PreviousWeapon`, so `lastinv` after a holster returned to whatever was
		// active two switches back instead of the weapon just put away.
		D.Input(TEXT("Holster"),         [](FElysiumEntity& E, const FElysiumInputArgs&)
			{
				FP& Player = static_cast<FP&>(E);
				FElysiumItem* Unarmed =
					Player.Inventory.FindOrdinary(Player, TEXT("item_w_unarmed"));
				if (!Unarmed)
				{
					UE_LOG(LogElysiumPlayer, Warning,
						TEXT("%s Holster: no carried item_w_unarmed to fall back to — the active "
							"weapon is unchanged"), *Player.DebugString());
					return;
				}
				Player.Inventory.SetActiveWeapon(Player, *Unarmed);
			});

		// The three law counters as read-only fields, so `pc.criminal_level` reads a number. They
		// are engine-written (the inputs above are the only writers), which is what !bKeyable says.
		AddLawField(D, TEXT("criminal_level"),     &FElysiumLawState::Criminal);
		AddLawField(D, TEXT("supernatural_level"), &FElysiumLawState::Supernatural);
		AddLawField(D, TEXT("investigate_level"),  &FElysiumLawState::Investigate);

		// `vhistory` is `m_iVHistoryID`: the chargen History row's index into `histories000.txt`.
		// It is not a sheet slot — `stats.txt` carries no Stat for it — so it reads off the player
		// record, where chargen writes it and the save's Player block persists it. Read-only for
		// the same reason as the law counters: chargen is its only writer.
		//
		// This is the field `chooseSire()` branches on to set `G.Player_Homo` / `Player_Insane` /
		// `Player_Batshit` (row 1 is `Homosexual_Player`), which four of sp_theatre's twelve
		// `logic_pythoncheck` gates then read.
		{
			FElysiumFieldAccessor Acc;
			Acc.ApplyFlags(EElysiumField::None);
			Acc.Type = EElysiumVariantType::Int;
			Acc.Get = [](const FElysiumEntity& E)
			{
				const UElysiumGameStateSubsystem* State = E.World ? E.World->GetGameState() : nullptr;
				return FElysiumVariant::Int(State ? State->PlayerRecord().HistoryId : INDEX_NONE);
			};
			Acc.Set = [](FElysiumEntity&, const FElysiumVariant&) {};
			D.Fields.Add(FName(TEXT("vhistory")), MoveTemp(Acc));
		}
	});
