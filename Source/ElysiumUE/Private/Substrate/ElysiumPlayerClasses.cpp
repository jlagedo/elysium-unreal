// 11.4 — the player entity and the two chain nodes above it (S3).
//
// `docs/architecture/runtime-architecture.md` sections 5-6 is the design; `docs/vtmb/script_api.md` is the input inventory. The
// three classes here are ordinary registry nodes: nothing about the player is special-cased, which
// is the whole point — `pc.MoneyAdd(50)` from a level script, `MoneyAdd` on a Hammer wire and
// `elysium.ent_fire !player MoneyAdd 50` from the console are one input, reached by one R2 walk.
//
// What is deliberately NOT here: the economy (9.10), the vdata sheet and its meters (9.4),
// inventory and barter (9.8), disposition reactions (9.9), disciplines/frenzy (P13) and the
// look-at rig (P12). Their inputs register and log; the field they will write is already in place.

#include "ElysiumPlayer.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"          // ElysiumMove::U / StandViewZ — the one units conversion
#include "Substrate/ElysiumDamage.h"        // FElysiumDmg + the shared apply path
#include "Substrate/ElysiumDisposition.h"   // FElysiumEyeTargetTuning, the gaze layer's content
#include "Substrate/ElysiumItemClasses.h"   // FElysiumItem — Inventory_Remove's entity parameter
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumSheetSlots.h"
#include "ElysiumSkeletalBasis.h"
#include "ElysiumStub.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumPendingInput.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumSheetMath.h"
#include "Visual/ElysiumExpressionTable.h"
#include "Visual/ElysiumNpcVisual.h"

#include "ChaosClothAsset/ClothComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Misc/Paths.h"

#include <type_traits>

DEFINE_LOG_CATEGORY_STATIC(LogElysiumPlayer, Log, All);

namespace
{
	// Indexed by the level-script encoding pc.clan uses: 2 = Brujah ... 8 = Ventrue.
	const TCHAR* GClanNames[] = { TEXT("?"), TEXT("?"), TEXT("Brujah"), TEXT("Gangrel"),
		TEXT("Malkavian"), TEXT("Nosferatu"), TEXT("Toreador"), TEXT("Tremere"), TEXT("Ventrue") };
	constexpr int32 GClanMin = 2;
	constexpr int32 GClanMax = 8;

	// Register a field backed by a subclass member (FElysiumClassDesc::Field only reaches
	// FElysiumEntity members). Mirrors AddSubclassField / AddNpcField / AddLogicField — file-unique
	// name so all of them can land in one unity blob.
	template <typename TClass, typename TMember>
	void AddCharField(FElysiumClassDesc& D, const TCHAR* Name, TMember TClass::* Member, EElysiumField Flags = ElysiumFieldDefault)
	{
		static_assert(std::is_base_of_v<FElysiumEntity, TClass>, "TClass must derive from FElysiumEntity");
		FElysiumFieldAccessor Acc;
		Acc.ApplyFlags(Flags);
		if constexpr (std::is_same_v<TMember, bool>)
		{
			Acc.Type = EElysiumVariantType::Bool;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::Bool(static_cast<const TClass&>(E).*Member); };
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToInt() != 0; };
		}
		else if constexpr (std::is_same_v<TMember, int32>)
		{
			Acc.Type = EElysiumVariantType::Int;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::Int(static_cast<const TClass&>(E).*Member); };
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToInt(); };
		}
		else if constexpr (std::is_same_v<TMember, float>)
		{
			Acc.Type = EElysiumVariantType::Float;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::Float(static_cast<const TClass&>(E).*Member); };
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToFloat(); };
		}
		else if constexpr (std::is_same_v<TMember, FVector>)
		{
			Acc.Type = EElysiumVariantType::Vector;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::Vector(static_cast<const TClass&>(E).*Member); };
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToVector(); };
		}
		else if constexpr (std::is_same_v<TMember, FString>)
		{
			Acc.Type = EElysiumVariantType::String;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::String(static_cast<const TClass&>(E).*Member); };
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToString(); };
		}
		else
		{
			static_assert(sizeof(TMember) == 0, "AddCharField: unsupported member type");
		}
		D.Fields.Add(FName(Name), MoveTemp(Acc));
	}

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

	// B6 — the feed transaction's state, as Save-flagged chain fields (K8: a registered field, the
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

// --- FElysiumSheet ---------------------------------------------------------------------------

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

// ============================================================================================
// FElysiumAnimating — CBaseAnimating
// ============================================================================================

FString FElysiumAnimating::ModelStem() const
{
	return FPaths::GetBaseFilename(Model).ToLower();
}

void FElysiumAnimating::BuildBody()
{
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!Embodiment || !Def || Model.IsEmpty())
	{
		return;   // bare test world, no embodiment, or a bodiless character (npc_VCamera has no model)
	}

	// Source `angles` is [pitch yaw roll]; a standing character needs yaw only, and a baked body's
	// authored forward is its own component +X, so the placement is the reflected yaw and nothing
	// else (ElysiumSkeletalBasis).
	const FRotator Rot = ElysiumSkeletalBasis::FromSourceAngles(Angles);
	Visual = Embodiment->BuildNpcVisual(ModelStem(), Origin, Rot, Embodiment->BodyScaleFor(*Def),
		Disposition, IdleVariant());
	if (Visual)
	{
		World->RegisterNpcBody(Visual);
		Embodiment->UpdateNpcDisposition(Visual, Disposition, DispositionLevel);
		RefreshDispositionExpression();
		if (IsInert())
		{
			GateVisual();   // born hidden (start_hidden / a Spawn()-time Kill)
		}
	}
}

bool FElysiumAnimating::PlayAnimClip(const FString& ClipName, bool bLoop, float* OutSeconds)
{
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!Embodiment || !Visual || ClipName.IsEmpty())
	{
		return false;
	}
	return Embodiment->PlayNpcClip(Visual, ModelStem(), ClipName, bLoop, OutSeconds);
}

bool FElysiumAnimating::PreloadAnimClip(const FString& ClipName)
{
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	return Embodiment && Visual && !ClipName.IsEmpty()
		&& Embodiment->PreloadNpcClip(Visual, ModelStem(), ClipName);
}

bool FElysiumAnimating::PlayCinematicClip(const FString& AnimSetModel, const FString& BoneRoot,
	const FString& ClipName, bool bLoop, float* OutSeconds)
{
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!Embodiment || !Visual || AnimSetModel.IsEmpty() || ClipName.IsEmpty())
	{
		return false;
	}
	return Embodiment->PlayCinematicClip(Visual, ModelStem(), AnimSetModel, BoneRoot, ClipName,
		bLoop, OutSeconds);
}

bool FElysiumAnimating::PreloadCinematicClip(const FString& AnimSetModel, const FString& BoneRoot,
	const FString& ClipName)
{
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	return Embodiment && Visual && !AnimSetModel.IsEmpty() && !ClipName.IsEmpty()
		&& Embodiment->PreloadCinematicClip(
			Visual, ModelStem(), AnimSetModel, BoneRoot, ClipName);
}

bool FElysiumAnimating::SeekCinematicClip(float PositionSeconds)
{
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	return Embodiment && Visual && Embodiment->SeekCinematicClip(Visual, PositionSeconds);
}

void FElysiumAnimating::StopCinematicClip()
{
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!Embodiment || !Visual)
	{
		return;
	}
	// Crossfade out of the cinematic pose; only tear the player down when there is no idle to go to.
	// Stopping first empties the animation host, which makes the idle behind it SNAP in from nothing
	// (the host has nothing to blend from, so it treats the idle as a first clip) and discards the
	// outgoing pose that the next scene's opening clip has to blend out of. Between two chained
	// scenes that is two hard pops a frame apart, which is what the theatre's courtroom hand-offs
	// read as. With no idle resolved, StopCinematicClip leaves the body in its reference pose, so it
	// stays the fallback rather than the first move.
	if (!ResetAnimToIdle())
	{
		Embodiment->StopCinematicClip(Visual);
	}
}

int32 FElysiumAnimating::SetFlexControllers(TArrayView<const FElysiumFlexWrite> Writes,
	TArray<FString>* OutMissing)
{
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!Embodiment || !Visual)
	{
		return INDEX_NONE;   // a bodiless or headless character has no face to move
	}
	return Embodiment->SetFlexControllers(Visual, Writes, OutMissing);
}

bool FElysiumAnimating::SetMouthOpen(float Open)
{
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!Embodiment || !Visual)
	{
		return false;   // a bodiless or headless character has no jaw to move
	}
	return Embodiment->SetMouthOpen(Visual, Open);
}

bool FElysiumAnimating::GetPhonemeFilter(float& OutMin, float& OutMax) const
{
	const IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!Embodiment || !Visual)
	{
		return false;   // a bodiless or headless character speaks with no filter to read
	}
	return Embodiment->GetPhonemeFilter(Visual, OutMin, OutMax);
}

bool FElysiumAnimating::ResetAnimToIdle()
{
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!Embodiment || !Visual)
	{
		return false;
	}
	return Embodiment->RefreshNpcIdle(
		Visual, ModelStem(), Disposition, DispositionLevel, IdleVariant());
}

bool FElysiumAnimating::SetDispositionName(const FString& NewDisposition)
{
	return SetDisposition(NewDisposition, 1);
}

bool FElysiumAnimating::CommitDisposition(const FString& NewDisposition, int32 NewLevel,
	bool& bOutChanged, FElysiumDisposition* OutOld, FElysiumDisposition* OutNew)

{
	bOutChanged = false;
	if (NewDisposition.IsEmpty())
	{
		return false;
	}
	FElysiumDisposition OldRow;
	FElysiumDisposition NewRow;
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	const bool bOldResolved = Embodiment
		&& Embodiment->ResolveDisposition(Disposition, DispositionLevel, OldRow);
	const bool bNewResolved = Embodiment
		&& Embodiment->ResolveDisposition(NewDisposition, FMath::Max(1, NewLevel), NewRow);
	if (OutOld)
	{
		*OutOld = bOldResolved ? OldRow : FElysiumDisposition();
	}
	if (OutNew)
	{
		*OutNew = bNewResolved ? NewRow : FElysiumDisposition();
	}

	const FString ResolvedName = bNewResolved ? NewRow.Name : NewDisposition;
	const int32 ResolvedLevel = bNewResolved ? NewRow.Level : FMath::Max(1, NewLevel);
	bOutChanged = !Disposition.Equals(ResolvedName, ESearchCase::IgnoreCase)
		|| DispositionLevel != ResolvedLevel;
	if (!bOutChanged)
	{
		return true;
	}
	Disposition = ResolvedName;
	DispositionLevel = ResolvedLevel;
	if (Embodiment && Visual)
	{
		Embodiment->UpdateNpcDisposition(Visual, Disposition, DispositionLevel);
	}
	RefreshDispositionExpression();
	return true;
}

bool FElysiumAnimating::SetDisposition(const FString& NewDisposition, int32 NewLevel)
{
	bool bChanged = false;
	if (!CommitDisposition(NewDisposition, NewLevel, bChanged))
	{
		return false;
	}
	if (bChanged)
	{
		ResetAnimToIdle();
	}
	return true;
}

void FElysiumAnimating::SetDispositionTalking(bool bTalking)
{
	if (bDispositionTalking == bTalking)
	{
		return;
	}
	bDispositionTalking = bTalking;
	RefreshDispositionExpression();
}

void FElysiumAnimating::AccumulateDispositionFacialPose(TMap<FString, float>& InOutPose) const
{
	for (const TPair<FString, float>& Key : DispositionFacialPose)
	{
		InOutPose.Add(Key.Key, Key.Value);
	}
}

void FElysiumAnimating::RefreshDispositionExpression()
{
	TMap<FString, float> Next;
	FElysiumDisposition Row;
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (Embodiment && Visual
		&& Embodiment->ResolveDisposition(Disposition, DispositionLevel, Row))
	{
		const FString Expression = bDispositionTalking && !Row.TalkingExpression.IsEmpty()
			? Row.TalkingExpression : Row.DefaultExpression;
		const TSharedPtr<const FElysiumExpressionTable> Table =
			ElysiumExpressions::Load(ModelStem(), TEXT("expressions"));
		const int32 Index = Table.IsValid() ? Table->FindRow(Expression) : INDEX_NONE;
		if (Table.IsValid() && Table->Rows.IsValidIndex(Index))
		{
			const FElysiumExpressionRow& ExpressionRow = Table->Rows[Index];
			for (int32 Key = 0; Key < Table->Keys.Num(); ++Key)
			{
				const float Influence = FMath::Clamp(
					ExpressionRow.Weights[Key] * Row.ExpressionIntensity, 0.f, 1.f);
				if (Influence > 0.f)
				{
					Next.Add(Table->Keys[Key], ExpressionRow.Values[Key] * Influence);
				}
			}
		}
	}

	TArray<FElysiumFlexWrite> Writes;
	for (const TPair<FString, float>& Key : Next)
	{
		Writes.Add({ Key.Key, Key.Value });
	}
	for (const TPair<FString, float>& Key : DispositionFacialPose)
	{
		if (!Next.Contains(Key.Key))
		{
			Writes.Add({ Key.Key, 0.f });
		}
	}
	if (!Writes.IsEmpty())
	{
		SetFlexControllers(Writes, nullptr);
	}
	DispositionFacialPose = MoveTemp(Next);

	// The body policy above is the emotional/presentation transaction. Relationship and RPG
	// reaction remain independent stores; no value is derived from this one.
}

void FElysiumAnimating::OnRuntimeTransformChanged()
{
	FElysiumEntity::OnRuntimeTransformChanged();
	if (Visual)
	{
		Visual->SetRelativeLocation(Origin);
		Visual->SetRelativeRotation(ElysiumSkeletalBasis::FromSourceAngles(Angles));
	}
}

void FElysiumAnimating::OnRuntimeModelChanged()
{
	if (!World)
	{
		return;
	}
	IElysiumEmbodiment* Embodiment = World->Embodiment();
	if (!Embodiment)
	{
		return;   // bare test world — the logical Model field is still updated
	}
	// A model swap replaces the body, it does not remove it, so anything parented to this character
	// (PostSpawn attaches a child to GetAttachBody() = Visual) has to survive onto the new one.
	// Nothing tracks an entity's children, so read them off the component before it is destroyed and
	// carry their offsets across — a `parentname` child keeps its relative pose, not its world pose.
	// The socket travels with the child: a bone-attached env_particle re-parented to the bare root
	// would silently stop tracking the bone the first time the level script re-models the character,
	// which is exactly when the cinematic emitters are live.
	struct FCarriedChild
	{
		TWeakObjectPtr<USceneComponent> Component;
		FTransform RelativeTransform;
		FName Socket;
	};
	TArray<FCarriedChild> Carried;
	if (Visual)
	{
		for (USceneComponent* Child : Visual->GetAttachChildren())
		{
			if (Child)
			{
				Carried.Add({ Child, Child->GetRelativeTransform(), Child->GetAttachSocketName() });
			}
		}
		Visual->DestroyComponent();
		Visual = nullptr;
	}
	BuildBody();
	if (Visual)
	{
		for (const FCarriedChild& Child : Carried)
		{
			if (USceneComponent* Live = Child.Component.Get())
			{
				const bool bKeepSocket = Child.Socket != NAME_None
					&& Visual->DoesSocketExist(Child.Socket);
				Live->AttachToComponent(Visual, FAttachmentTransformRules::KeepRelativeTransform,
					bKeepSocket ? Child.Socket : NAME_None);
				Live->SetRelativeTransform(Child.RelativeTransform);
			}
		}

		// Runtime animation objects are bound to the old body's transient USkeleton. Re-walk the
		// dormant plan against this replacement immediately so compression for a Python recast can
		// overlap the authored lead to its scene; InputStart owns the completion barrier.
		if (World->IsActive())
		{
			World->RefreshAnimationPreload();
		}
	}
}

void FElysiumAnimating::OnDormancyChanged()
{
	FElysiumEntity::OnDormancyChanged();
	GateVisual();
}

void FElysiumAnimating::GateVisual()
{
	if (Visual)
	{
		const bool bShown = !IsInert();
		Visual->SetVisibility(bShown);
		Visual->SetComponentTickEnabled(bShown);   // pause the idle clip while hidden
		ElysiumNpcVisual::GateLeaderCloth(Visual, bShown);
	}
}

// ============================================================================================
// FElysiumCombatCharacter — CBaseCombatCharacter
// ============================================================================================

void FElysiumCombatCharacter::PendingInput(const TCHAR* Input, const TCHAR* Owner,
	const FElysiumInputArgs& Args, const TCHAR* DeclaringClass) const
{
	// Registered so the name resolves through the R2 walk, but nothing behind it yet — the same
	// condition as an unregistered classname's input, so it reports through the same surface and
	// lands in the same work list. Keyed on the class the input is declared on, not on the
	// receiver, so one row covers every NPC that receives it.
	ElysiumStub::Fired(TEXT("input"),
		FString::Printf(TEXT("%s.%s"),
			DeclaringClass ? DeclaringClass : *ElysiumCombatCharacterClassName().ToString(), Input),
		DebugString(), ElysiumStub::DescribeInput(Args), Owner);
}

void FElysiumCombatCharacter::AddMoney(int32 Delta)
{
	// `CBaseCombatCharacter::MoneyAdd` (`10340E50`) is a raw `+=` with no floor, which is what the
	// quest `AwardMoney` path reaches. `InputMoneyRemove` adds its own floor for the subtracting
	// direction; this one deliberately does not.
	if (Delta != 0) { Money += Delta; }
}

void FElysiumCombatCharacter::InputMoneyAdd(const FElysiumInputArgs& Args)
{
	AddMoney(Args.Param.ToInt());
}

void FElysiumCombatCharacter::InputMoneyRemove(const FElysiumInputArgs& Args)
{
	const int32 N = Args.Param.ToInt();
	// The floor is the runtime's, not a recovered rule: VtMB's MoneyRemove is reached through the
	// barter/quest paths that check affordability first (9.10 owns those checks).
	if (N != 0) { Money = FMath::Max(0, Money - N); }
}

const FElysiumStatTable* FElysiumCombatCharacter::SheetRules() const
{
	UElysiumGameStateSubsystem* GameState = World ? World->GetGameState() : nullptr;
	return GameState ? GameState->Stats() : nullptr;
}

// The rulebook this character reads its rules out of, or null in a bare world.
static UElysiumRulebookSubsystem* CharRulebook(const FElysiumCombatCharacter& Char)
{
	UElysiumGameStateSubsystem* GameState = Char.World ? Char.World->GetGameState() : nullptr;
	return GameState ? GameState->Rulebook() : nullptr;
}

void FElysiumCombatCharacter::RebuildEffects()
{
	UElysiumRulebookSubsystem* Rules = CharRulebook(*this);
	if (Effects.IsEmpty() || !Rules)
	{
		EffectLayer.Reset();
		RecomputeSheet();
		return;
	}
	if (!EffectLayer.IsValid())
	{
		EffectLayer = MakeShared<FElysiumSheetEffects>();
	}
	EffectLayer->Build(Rules->TraitEffects(), Effects, &Rules->Feats());
	RecomputeSheet();
}

void FElysiumCombatCharacter::RecomputeSheet()
{
	Sheet.RecomputeCurrent(SheetRules(), SheetEffects());
	SyncHealthFromSheet();
}

void FElysiumCombatCharacter::AddTrait(EElysiumTraitContainer Container, int32 Slot, int32 Delta)
{
	// AddBase carries the gate — the effective max on a gain, bypassed on a loss — and re-derives
	// every current value from the new base. The bounds are `stats.txt`'s own (Humanity 0..10,
	// Masquerade 0..5, BloodPool 0..15), tightened by whatever the clan's trait effects cap.
	Sheet.AddBase(Container, Slot, Delta, SheetRules(), SheetEffects());
}

int32 FElysiumCombatCharacter::CalcFeat(const FString& Name) const
{
	UElysiumRulebookSubsystem* Rules = CharRulebook(*this);
	if (!Rules)
	{
		return 0;   // no rulebook: every check fails closed, as an unresolved gate does
	}
	bool bResolved = false;
	bool bIsFeat = false;
	const int32 Value = ElysiumFeats::Calc(Rules->Feats(), Sheet, SheetEffects(), Name,
		bResolved, bIsFeat);
	if (!bResolved)
	{
		// VtMB raises `AttributeError("invalid feat name -- %s")`. A raise here would abort the
		// whole conversation line, so the name is reported and the gate fails closed — the
		// error-to-false posture the rest of the scripting surface takes.
		UE_LOG(LogElysiumPlayer, Verbose, TEXT("%s CalcFeat(\"%s\") — no feat and no trait of that name"),
			*DebugString(), *Name);
	}
	return Value;
}

int32 FElysiumCombatCharacter::BumpStat(const FString& Stat, int32 Times)
{
	// A count below 1 does nothing — VtMB raises `"invalid args in BumpStat"`, and the loop it
	// guards cannot run backwards, so **BumpStat cannot decrement**.
	if (Stat.IsEmpty() || Times < 1)
	{
		UE_LOG(LogElysiumPlayer, Log, TEXT("%s BumpStat(\"%s\", %d) — invalid args"),
			*DebugString(), *Stat, Times);
		return 0;
	}
	EElysiumTraitContainer Container;
	int32 Slot = INDEX_NONE;
	if (!ElysiumFindSheetSlot(*Stat, Container, Slot))
	{
		UE_LOG(LogElysiumPlayer, Log, TEXT("%s BumpStat(\"%s\") — no such trait"), *DebugString(), *Stat);
		return 0;
	}

	const FElysiumStatTable* Rules = SheetRules();
	int32 Landed = 0;
	for (int32 i = 0; i < Times; ++i)
	{
		// The ceiling is BumpStat's own, hardcoded and independent of the stat's authored `Max`:
		// each pass is skipped unless the BASE is under 5. A stat whose Max is higher still stops
		// here, which is why this cannot be folded into IncBase.
		if (Sheet.GetBase(Container, Slot) >= 5)
		{
			break;
		}
		if (!Sheet.IncBase(Container, Slot, Rules, SheetEffects()))
		{
			break;
		}
		++Landed;
	}
	SyncHealthFromSheet();
	// One client notification fires after the loop, not per dot (8.9 owns the readout).
	return Landed;
}

int32 FElysiumCombatCharacter::GetMasqueradeLevel() const
{
	return Sheet.GetCurrent(EElysiumTraitContainer::Attributes, ElysiumSlot::Masquerade);
}

void FElysiumCombatCharacter::AddHumanity(int32 Delta)
{
	if (Delta == 0)
	{
		return;
	}
	// One flag doubles both directions: Toreador's gift (gains) and its bane (losses) are the same
	// `Fx_Humanity_Mods_Doubled +1`, and no other shipped group sets it.
	const FElysiumSheetEffects* Layer = SheetEffects();
	const int32 Scaled = (Layer && Layer->Flag(TEXT("Fx_Humanity_Mods_Doubled")) > 0) ? Delta * 2 : Delta;
	AddTrait(EElysiumTraitContainer::Attributes, ElysiumSlot::Humanity, Scaled);
}

void FElysiumCombatCharacter::ChangeMasqueradeLevel(int32 Delta)
{
	if (Delta == 0)
	{
		return;
	}
	AddTrait(EElysiumTraitContainer::Attributes, ElysiumSlot::Masquerade, Delta);

	// The counter reaching its authored ceiling is the second loss condition. The check is on the
	// clamped current value, so a `+9` and a `+1` at 4 both land on exactly 5.
	int32 Min = 0, Max = 0;
	Sheet.BoundsFor(EElysiumTraitContainer::Attributes, ElysiumSlot::Masquerade,
		SheetRules(), SheetEffects(), Min, Max);
	if (Max >= Min && GetMasqueradeLevel() >= Max && Delta > 0)
	{
		OnMasqueradeBreached();
	}
}

void FElysiumCombatCharacter::OnMasqueradeBreached()
{
	// Only the player's masquerade ends a run; an NPC has the counter because the sheet is shared.
	UE_LOG(LogElysiumPlayer, Log, TEXT("%s masquerade at %d"), *DebugString(), GetMasqueradeLevel());
}

void FElysiumCombatCharacter::AddBlood(int32 Delta)
{
	if (Delta != 0)
	{
		// The ceiling is `BloodPool`'s authored Max (15). The per-generation ceiling the file
		// carries as `Generation_Blood_Pool_Max` is **commented out in the shipped data**, so it is
		// not in force and nothing here consults it.
		AddTrait(EElysiumTraitContainer::Attributes, ElysiumSlot::BloodPool, Delta);
	}
}

int32 FElysiumCombatCharacter::BloodHeal(int32 Blood)
{
	if (Blood <= 0)
	{
		return 0;
	}
	// `VampHeal_Info.VampFeedingHeal_Info` — `UsesRatio 1`, `BloodToHealthRatio 10`, i.e. one blood
	// point buys ten points of damage healed. The ratio is data; only the default is code.
	int32 Ratio = 10;
	if (UElysiumRulebookSubsystem* Rules = CharRulebook(*this))
	{
		Ratio = Rules->Rules().Int(TEXT("VampHeal_Info.VampFeedingHeal_Info"),
			TEXT("BloodToHealthRatio"), Ratio);
	}

	const int32 Spent = FMath::Min(Blood, Sheet.GetCurrent(EElysiumTraitContainer::Attributes, ElysiumSlot::BloodPool));
	if (Spent <= 0)
	{
		return 0;
	}
	AddBlood(-Spent);
	return HealDamage(Spent * FMath::Max(1, Ratio));
}

int32 FElysiumCombatCharacter::HealDamage(int32 Points)
{
	if (Points <= 0)
	{
		return 0;
	}
	// `Health` counts damage TAKEN, so healing is a subtraction from it — and a subtraction
	// bypasses the gain gate, which is exactly what makes the floor the authored `Min` of 0.
	const int32 Damage = Sheet.GetCurrent(EElysiumTraitContainer::Attributes, ElysiumSlot::Health);
	const int32 Healed = FMath::Min(Damage, Points);
	if (Healed <= 0)
	{
		return 0;
	}
	AddTrait(EElysiumTraitContainer::Attributes, ElysiumSlot::Health, -Healed);
	SyncHealthFromSheet();
	return Healed;
}

void FElysiumCombatCharacter::InputHumanityAdd(const FElysiumInputArgs& Args)
{
	const int32 N = Args.Param.ToInt();
	if (N != 0) { AddHumanity(N); }
}

void FElysiumCombatCharacter::InputChangeMasqueradeLevel(const FElysiumInputArgs& Args)
{
	ChangeMasqueradeLevel(Args.Param.ToInt());
}

void FElysiumCombatCharacter::InputBloodloss(const FElysiumInputArgs& Args)
{
	AddBlood(-Args.Param.ToInt());
}

void FElysiumCombatCharacter::InputBloodgain(const FElysiumInputArgs& Args)
{
	AddBlood(Args.Param.ToInt());
}

void FElysiumCombatCharacter::InputBloodHeal(const FElysiumInputArgs& Args)
{
	// VtMB's internal name is BloodHealIn: the argument is the blood to spend, and the conversion
	// is the rulebook's ratio.
	BloodHeal(Args.Param.ToInt());
}

void FElysiumCombatCharacter::InputWillTalk(const FElysiumInputArgs& Args)
{
	bWillTalk = Args.Param.ToInt() != 0;
}

void FElysiumCombatCharacter::InputInventoryRemove(const FElysiumInputArgs& Args)
{
	// The recovered field type is CLASSPTR, so the parameter is an entity and nothing else. A wire
	// carrying a string cannot convert to one in Source either, so a non-handle parameter performs
	// nothing rather than being reinterpreted as a classname — that would be a contract this input
	// does not have. No shipped map fires it (0 wires game-wide), so the handle form is the whole
	// surface a runtime caller reaches.
	FElysiumEntity* Named = (World && Args.Param.IsHandle()) ? World->Resolve(Args.Param.ToHandle()) : nullptr;
	FElysiumItem* Item = Named ? Named->AsItem() : nullptr;
	if (!Item)
	{
		UE_LOG(LogElysiumPlayer, Log, TEXT("%s Inventory_Remove — the parameter is not an item entity"),
			*DebugString());
		return;
	}
	Inventory.Detach(*this, *Item);
}

// ============================================================================================
// Gaze — the selection cascade, the saccade layer and the integrator (12.4)
// ============================================================================================

namespace
{
	// The ±30° cone every candidate is gated by, as a dot rather than an angle — retail's own
	// test is `dot(headForward, normalize(p - headPos)) > 0.866`.
	constexpr float GGazeConeDot = 0.866f;

	// Distances, in Source units converted at the one place the project converts them. The scan
	// sweeps a 300-unit sphere centred 300 units ahead of the eyes; the straight-ahead fallback is
	// 500 units out; a fidget cell is projected 25 units from the head.
	constexpr float GScanReach = 300.f * ElysiumMove::U;
	constexpr float GScanRadius = 300.f * ElysiumMove::U;
	constexpr float GAheadReach = 500.f * ElysiumMove::U;
	constexpr float GFidgetReach = 25.f * ElysiumMove::U;

	// A fidget cell is a numeric keypad seen from the character's point of view: 5 is dead ahead,
	// each column is 20° of yaw and each row 20° of pitch.
	//
	//     7 8 9      up
	//     4 5 6
	//     1 2 3      down
	//
	// Cell 0 is not a direction at all — the table's comment defines it as "fall back to normal
	// look behavior", so it is handled by the caller and never reaches here.
	FVector FidgetCellDirection(int32 Cell, const FVector& HeadForward)
	{
		const int32 Clamped = FMath::Clamp(Cell, 1, 9);
		const int32 Column = (Clamped - 1) % 3;   // 0 left, 1 centre, 2 right
		const int32 Row = (Clamped - 1) / 3;      // 0 bottom, 1 middle, 2 top
		FRotator Aim = HeadForward.Rotation();
		Aim.Yaw += static_cast<float>(Column - 1) * 20.f;
		Aim.Pitch += static_cast<float>(Row - 1) * 20.f;
		return Aim.Vector();
	}

	bool InsideGazeCone(const FVector& HeadPos, const FVector& HeadForward, const FVector& Point)
	{
		const FVector To = Point - HeadPos;
		if (To.IsNearlyZero())
		{
			return false;
		}
		return FVector::DotProduct(HeadForward, To.GetSafeNormal()) > GGazeConeDot;
	}
}

FVector FElysiumCombatCharacter::EyePosition() const
{
	// `GetAbsOrigin() + m_vecViewOffset`. The standing view offset is 64 units — the ducked 30 is
	// the player's crouched value and belongs to the player leaf, not to every character.
	return Origin + FVector(0.f, 0.f, ElysiumMove::StandViewZ);
}

void FElysiumCombatCharacter::InputLookAtEntityEye(const FElysiumInputArgs& Args)
{
	EyeLookTargetName = Args.Param.ToString();
	EyeLookMode = 1;
}

void FElysiumCombatCharacter::InputLookAtEntityCenter(const FElysiumInputArgs& Args)
{
	// Reproduced defect. Mode 2 is the one that would resolve `WorldSpaceCenter()`, but the shipped
	// handler pushes the Eye constant and no handler anywhere passes 2 — so all 10 authored
	// `LookAtEntityCenter` firings behave exactly as `LookAtEntityEye`. Kept as its own input so the
	// wire still resolves by name and so the divergence is visible here rather than implied.
	EyeLookTargetName = Args.Param.ToString();
	EyeLookMode = 1;
}

void FElysiumCombatCharacter::InputLookAtEntityOrigin(const FElysiumInputArgs& Args)
{
	EyeLookTargetName = Args.Param.ToString();
	EyeLookMode = 3;
}

void FElysiumCombatCharacter::InputLookAtEntityDefault(const FElysiumInputArgs&)
{
	// Clears the scripted target and restores autonomous behaviour. The smoothed point is left
	// where it is so the eyes glide off the old target rather than snapping.
	EyeLookTargetName.Reset();
	EyeLookMode = 0;
}

FVector FElysiumCombatCharacter::TickGaze(float Now, float DeltaSeconds,
	const FVector& HeadPos, const FVector& HeadForward, const FElysiumEyeTargetTuning& Tuning,
	const FVector* DialogPovPoint)
{
	const FVector Ahead = HeadPos + HeadForward * GAheadReach;

	// --- Selection ---------------------------------------------------------------------------
	// The priority cascade. Three of retail's arms have nothing to read yet and are marked rather
	// than faked: `enemy` needs the combat layer (P13), `navigation goal` needs a move-goal
	// accessor on FElysiumNpc, and `heard sound` needs a sound record. Each would sit here, in this
	// order, between the scripted target and the autonomous scan. Their absence makes a character
	// fall through to the scan, which is the same thing retail does when those arms find nothing.
	FVector Commanded = Ahead;
	bool bResolved = false;

	// 1. The dialogue partner, at their EyePosition() — eye height on the entity, not a head bone,
	//    so the aim holds still through the partner's animation the way retail's does.
	if (World != nullptr)
	{
		const FElysiumEntityHandle DialogOwner = World->GetOpenDialogOwner();
		if (DialogOwner.IsSet())
		{
			const FElysiumEntity* Partner = nullptr;
			if (DialogOwner.Index == Handle.Index)
			{
				// This character is the one talking; its partner is the player.
				Partner = World->FindPlayer();
			}
			else if (World->PlayerHandle().Index == Handle.Index)
			{
				Partner = World->Resolve(DialogOwner);
			}
			if (Partner != nullptr && Partner != this)
			{
				// `DialogPOV` on the shot in effect redirects this arm to the camera. It replaces the
				// *player* as the subject and nothing else, so a character being looked at by the
				// player still resolves normally, and every other arm of the cascade is untouched.
				const bool bPartnerIsPlayer = World->PlayerHandle().IsSet()
					&& Partner->Handle.Index == World->PlayerHandle().Index;
				Commanded = (bPartnerIsPlayer && DialogPovPoint != nullptr)
					? *DialogPovPoint
					: Partner->EyePosition();
				bResolved = true;
			}
		}
	}

	// 2. A scripted look-at. It still passes the cone test, and falls back to straight ahead
	//    outside it; it also yields back to autonomous on its own when the entity goes away.
	if (!bResolved && EyeLookMode != 0 && !EyeLookTargetName.IsEmpty() && World != nullptr)
	{
		if (const FElysiumEntity* Scripted = World->FindByName(EyeLookTargetName))
		{
			const FVector Point = (EyeLookMode == 3) ? Scripted->Origin : Scripted->EyePosition();
			Commanded = InsideGazeCone(HeadPos, HeadForward, Point) ? Point : Ahead;
			bResolved = true;
		}
		else
		{
			EyeLookTargetName.Reset();
			EyeLookMode = 0;
		}
	}

	// 3. The autonomous scan: nearest qualifying entity inside a 300-unit sphere centred 300 units
	//    ahead of the eyes, re-picked every 1-5 seconds; nothing found means straight ahead and a
	//    retry in half a second.
	if (!bResolved)
	{
		if (Now >= NextEyeLookTime)
		{
			const FVector Centre = HeadPos + HeadForward * GScanReach;
			const FElysiumEntity* Best = nullptr;
			float BestDistanceSq = TNumericLimits<float>::Max();
			if (World != nullptr)
			{
				for (const TUniquePtr<FElysiumEntity>& Candidate : World->Entities())
				{
					// Retail's filter is `entity->+0x94 != 0 || (GetFlags() & FL_CLIENT)`. The first
					// half is an unrecovered field, so the recovered half stands on its own: the
					// player always qualifies, and beyond that only other characters are treated as
					// worth looking at. Widening this is a content decision, not a maths one.
					const FElysiumEntity* E = Candidate.Get();
					if (E == nullptr || E == this || E->IsInert())
					{
						continue;
					}
					const bool bIsPlayer = World->PlayerHandle().IsSet()
						&& E->Handle.Index == World->PlayerHandle().Index;
					if (!bIsPlayer && const_cast<FElysiumEntity*>(E)->AsCombatCharacter() == nullptr)
					{
						continue;
					}
					const FVector Point = E->EyePosition();
					if (FVector::DistSquared(Point, Centre) > GScanRadius * GScanRadius
						|| !InsideGazeCone(HeadPos, HeadForward, Point))
					{
						continue;
					}
					const float DistanceSq = FVector::DistSquared(Point, HeadPos);
					if (Best == nullptr || DistanceSq < BestDistanceSq
						|| (FMath::IsNearlyEqual(DistanceSq, BestDistanceSq)
							&& E->Handle.Index < Best->Handle.Index))
					{
						Best = E;
						BestDistanceSq = DistanceSq;
					}
				}
			}
			if (Best != nullptr)
			{
				Commanded = Best->EyePosition();
				NextEyeLookTime = Now + static_cast<float>(FMath::RandRange(1, 5));
				FidgetStep = -1;
			}
			else
			{
				Commanded = Ahead;
				NextEyeLookTime = Now + 0.5f;
			}
			EyeLookTarget = Commanded;
		}
		// Between re-picks the commanded point stands, so the scan does not jitter frame to frame.
		// The re-pick above always runs on the first call (NextEyeLookTime starts at zero), so this
		// is never reading an unset value.
		Commanded = EyeLookTarget;
	}

	// --- Fidget ------------------------------------------------------------------------------
	// The saccade layer engages once the eyes have actually converged — retail waits for the
	// smoothed point to come within a unit of the commanded one, so a character crossing a room
	// tracks cleanly and only starts flicking about after it has settled. It applies to whatever
	// the cascade chose, autonomous subject included; it is a layer over the aim, not a mode.
	{
		const bool bConverged = FVector::Dist(CurEyeTarget, Commanded) <= ElysiumMove::U;
		if (bConverged && FidgetStep < 0 && Now >= NextFidgetTime)
		{
			FidgetStep = 0;
			NextFidgetTime = Now + FMath::FRandRange(Tuning.HoldMin, Tuning.HoldMax);
			FidgetCell = Tuning.FidgetPoints[0] < 0 ? FMath::RandRange(1, 9) : Tuning.FidgetPoints[0];
		}
		else if (FidgetStep >= 0 && Now >= NextFidgetTime)
		{
			++FidgetStep;
			if (FidgetStep > 2)
			{
				// Sequence exhausted: back to the default direction and hold for the disposition's
				// own interval before the next one.
				FidgetStep = -1;
				NextFidgetTime = Now + FMath::FRandRange(Tuning.MinInterval, Tuning.MaxInterval);
			}
			else
			{
				NextFidgetTime = Now + FMath::FRandRange(Tuning.HoldMin, Tuning.HoldMax);
				const int32 Authored = Tuning.FidgetPoints[FidgetStep];
				FidgetCell = Authored < 0 ? FMath::RandRange(1, 9) : Authored;
			}
		}
		// Cell 0 means "fall back to normal look behavior", so it leaves the commanded point alone.
		if (FidgetStep >= 0 && FidgetCell > 0)
		{
			Commanded = HeadPos + FidgetCellDirection(FidgetCell, HeadForward) * GFidgetReach;
		}
	}

	EyeLookTarget = Commanded;

	// --- Integration -------------------------------------------------------------------------
	// A fixed-timestep lerp, not a rate: per 0.1 s of accumulated interval,
	// `m_vCurEyeTarget += rate × (m_vEyeLookTarget − m_vCurEyeTarget)`. Reproducing the fixed step
	// matters — folding the rate into a per-frame lerp would make the convergence speed depend on
	// frame rate, which is exactly what the accumulator exists to avoid.
	EyeIntegRate = Tuning.TurnRate;
	if (!bCurEyeTargetSeeded)
	{
		CurEyeTarget = Commanded;
		bCurEyeTargetSeeded = true;
	}
	EyeIntegAccumulator += DeltaSeconds;
	int32 Steps = 0;
	while (EyeIntegAccumulator >= 0.1f && Steps < 16)
	{
		EyeIntegAccumulator -= 0.1f;
		CurEyeTarget += (EyeLookTarget - CurEyeTarget) * EyeIntegRate;
		++Steps;
	}
	if (Steps >= 16)
	{
		// A long hitch would otherwise spin this loop; land on the target and drop the backlog.
		CurEyeTarget = EyeLookTarget;
		EyeIntegAccumulator = 0.f;
	}

	// --- Head turn, which drives nothing -------------------------------------------------------
	// Retail integrates m_flHeadYaw/m_flHeadPitch every think through this 0.8/0.2 filter and
	// applies them with SetBoneController(0, …) and (1, …) — bone controllers, not pose parameters.
	// No shipped model declares a single bone controller, so the lookup fails and the value never
	// reaches the skeleton. Visible head movement in VtMB dialogue is animation and choreography,
	// not this path. Reproduced, including the unclamped filter and its lone `> 360 → 0` guard, so
	// the state is inspectable and so nobody later mistakes its absence for a missing feature.
	const FRotator ToTarget = (EyeLookTarget - HeadPos).Rotation();
	const FRotator HeadNow = HeadForward.Rotation();
	HeadYaw = HeadYaw * 0.8f + (ToTarget.Yaw - HeadNow.Yaw) * 0.2f;
	HeadPitch = HeadPitch * 0.8f + (ToTarget.Pitch - HeadNow.Pitch) * 0.2f;
	if (HeadYaw > 360.f) { HeadYaw = 0.f; }
	if (HeadPitch > 360.f) { HeadPitch = 0.f; }

	return CurEyeTarget;
}

void FElysiumCombatCharacter::SyncHealthFromSheet()
{
	// `CBaseCombatCharacter::HealthToPercent` projects the sheet pair onto Source's engine-space
	// health (`docs/vtmb/game_runtime.md` section 3). Our `health` / `max_health` keyfields ARE that engine
	// space — what the save walk enumerates, what a `.ents` `health` key writes, and what the body
	// reads — so they are derived, never the truth.
	MaxHealth = Sheet.GetCurrent(EElysiumTraitContainer::Attributes, ElysiumSlot::MaxHealth);
	const int32 Damage = Sheet.GetCurrent(EElysiumTraitContainer::Attributes, ElysiumSlot::Health);
	Health = FMath::Max(0, MaxHealth - Damage);
}

bool FElysiumCombatCharacter::IsKindred() const
{
	// The base answer is the sheet's own clan slot: a character carrying one of the seven playable
	// clans is Kindred, and everything else is mortal. This is the player's real classification —
	// `clandoc000.txt` gives every player template a clan — and the fallback for an NPC with no
	// `stattemplate`, whose authored `Kindred` key the NPC leaf reads instead.
	return FElysiumSheet::IsValidClan(Sheet.Clan());
}

void FElysiumCombatCharacter::TakeDamage(const FElysiumDmg& Dmg, FElysiumCombatCharacter* Attacker)
{
	if (IsInert())
	{
		return;
	}
	// B6 — incoming damage while paired tears the feed down BEFORE the damage commits, whichever
	// half of the pair is hit (`docs/vtmb/feeding.md` § "Interruption, completion and outputs").
	BreakFeed();

	FElysiumDmg Resolved = Dmg;
	if (!ElysiumDamage::Apply(Resolved, Attacker, *this, FElysiumDamageContext::FromCharacter(*this)))
	{
		return;   // Apply reported why
	}
	CommitDamage(Resolved);
}

void FElysiumCombatCharacter::TakeDamage(float Amount)
{
	if (Amount <= 0.f || IsInert())
	{
		return;
	}
	BreakFeed();

	// The scalar fallback: retail's alive path takes its positive damage EITHER from the descriptor
	// apply callback or from here, so this route does not enter the resolver at all. The descriptor
	// exists so the commit has one shape to spend: direct input, no mask (hence no aggravated
	// tracking and no soak bypass), and a forced soak of zero, which is what "the number as given"
	// means in descriptor terms.
	FElysiumDmg Dmg;
	Dmg.Family = EElysiumDmgFamily::Bashing;
	Dmg.Flags = ElysiumDamage::FlagDirectInput;
	Dmg.ExtraInput = FMath::Max(1, FMath::RoundToInt(Amount));
	Dmg.ForcedSoak = 0;
	Dmg.RolledSuccesses = Dmg.ExtraInput;
	Dmg.Remainder = Dmg.ExtraInput;
	Dmg.AppliedDamage = Dmg.ExtraInput;
	Dmg.bResolved = true;
	CommitDamage(Dmg);
}

void FElysiumCombatCharacter::CommitDamage(const FElysiumDmg& Dmg)
{
	using EC = EElysiumTraitContainer;

	int32 Remaining = Dmg.CommittedDamage();
	if (Remaining <= 0)
	{
		return;
	}
	if (MaxHealth <= 0)
	{
		// No health track: the rulebook did not load, or the character was built without a sheet.
		// Damage is recorded rather than applied — a character with no health model must not die of
		// arithmetic.
		UE_LOG(LogElysiumPlayer, Verbose, TEXT("%s took %d damage with no health track"),
			*DebugString(), Remaining);
		return;
	}

	// 1. HealthBuffer absorbs first. Exhausting it clears the counter and ends Bloodshield, which
	//    is the discipline that filled it; a partial absorption only reduces it.
	const int32 Buffer = Sheet.GetCurrent(EC::Attributes, ElysiumSlot::HealthBuffer);
	if (Buffer > 0)
	{
		const int32 Absorbed = FMath::Min(Buffer, Remaining);
		Sheet.SetBase(EC::Attributes, ElysiumSlot::HealthBuffer, Buffer - Absorbed);
		Remaining -= Absorbed;
		if (Buffer - Absorbed <= 0)
		{
			EndBloodshield();
		}
		RecomputeSheet();
	}

	if (Remaining > 0)
	{
		const int32 Taken = Sheet.GetBase(EC::Attributes, ElysiumSlot::Health);
		// 2. Unkillable caps the damage-TAKEN counter at the retail literal. It is not a one-hit-
		//    point floor and not a percentage: with the default Max_Health of 100 it leaves 25.
		const int32 Cap = bUnkillable ? ElysiumDamage::UnkillableDamageCap : MaxHealth;
		// 3. The remainder lands on the damage counter. The authored ceiling is `Max_Health`, which
		//    the sheet's own clamp applies whenever the rules table is loaded; the clamp here keeps
		//    a bare (rulebook-less) world reading the same numbers.
		const int32 Committed = FMath::Clamp(Taken + Remaining, 0, FMath::Max(Cap, 0));
		Sheet.SetBase(EC::Attributes, ElysiumSlot::Health, Committed);

		// 4. A Kindred victim also accumulates aggravated damage for the mask that takes no soak.
		if (IsKindred() && Dmg.TakesNoSoak())
		{
			const int32 Aggravated = Sheet.GetBase(EC::Attributes, ElysiumSlot::HealthAggDmg);
			Sheet.SetBase(EC::Attributes, ElysiumSlot::HealthAggDmg,
				Aggravated + (Committed - Taken));
		}
		// 5. `HealthToPercent` — the sheet pair projected back onto the engine-space keyfields.
		RecomputeSheet();
	}

	// The senses/memory record the schedule kernel reads. A no-op on the base.
	OnDamageCommitted(Dmg);

	// 6. The outputs, from their real producer. Retail fires them from the NPC alive commit and the
	//    player wires neither, but FireOutput is inert for an output an entity did not wire, so the
	//    shared commit is where they belong. OnHalfHealth is OFFERED on every damaging hit while the
	//    projected health sits at or below half, not only on the crossing edge.
	static const FName OnDamaged(TEXT("OnDamaged"));
	static const FName OnHalfHealth(TEXT("OnHalfHealth"));
	FireOutput(OnDamaged, Dmg.Source);
	if (MaxHealth > 0 && Health * 2 <= MaxHealth)
	{
		FireOutput(OnHalfHealth, Dmg.Source);
	}

	// 7. Death is the RPG comparison, not the engine-space projection: the damage counter reaching
	//    the ceiling is what selects it.
	const int32 Taken = Sheet.GetCurrent(EC::Attributes, ElysiumSlot::Health);
	const int32 Ceiling = Sheet.GetCurrent(EC::Attributes, ElysiumSlot::MaxHealth);
	if (!bUnkillable && Ceiling > 0 && Taken >= Ceiling)
	{
		OnKilled();
	}
}

void FElysiumCombatCharacter::EndBloodshield()
{
	// The exhausted buffer ends the power that filled it. Two spellings name the same power in the
	// shipped data — the discipline's own InternalName and the trait-effect group the discipline
	// installs — and the effect list can legitimately carry either, so both are removed.
	static const TCHAR* const Names[] =
	{
		TEXT("Thaumaturgy_Bloodshield"),
		TEXT("Discipline (Thaumaturgy-Bloodshield)"),
	};
	int32 Removed = 0;
	for (const TCHAR* Name : Names)
	{
		Removed += Effects.RemoveAll([Name](const FString& Entry)
			{ return Entry.Equals(Name, ESearchCase::IgnoreCase); });
	}
	if (Removed > 0)
	{
		RebuildEffects();
	}
}

void FElysiumCombatCharacter::OnKilled()
{
	if (bDeathReported)
	{
		return;
	}
	bDeathReported = true;
	static const FName OnDeath(TEXT("OnDeath"));
	FireOutput(OnDeath, Handle);   // one of CAI_BaseNPC's 16 outputs; the player wires none
	// Preserve producer order: the child's own OnDeath rows enter the queue before its maker's
	// OnNPCDied rows. Retail's relative order is still an open live-capture question; this is the
	// existing producer first, followed by the newly recovered owner notification.
	NotifyOwnerOfTermination(EElysiumOwnedEntityTermination::Died);
	UE_LOG(LogElysiumPlayer, Log, TEXT("%s died"), *DebugString());
}

bool FElysiumCombatCharacter::GetDynamicField(FName Name, FElysiumVariant& Out) const
{
	// Every compiled slot is a registered field, so the R2 walk has already answered by the time
	// this runs. What is left is a `base_*` name the shipped `stats.txt` does not own — which still
	// reads 0 rather than raising, the same default-on-miss `G` has, because the gates that ask
	// (`pc.base_Celerity > 0`) are written against a sheet where every name resolves.
	if (const int32* V = Sheet.Extra.Find(Name))
	{
		Out = FElysiumVariant::Int(*V);
		return true;
	}
	if (Name.ToString().StartsWith(TEXT("base_"), ESearchCase::CaseSensitive))
	{
		// Reads 0 rather than raising (see the header), but a name that lands here is a name the
		// shipped `stats.txt` does not own — either a slot we have not built or a script's
		// misspelling of one we have. Both are silent divergences, so both get reported: the read
		// still answers, and the tally says which names answered on nothing.
		ElysiumStub::Fired(TEXT("field"),
			FString::Printf(TEXT("%s.%s"), *ElysiumCombatCharacterClassName().ToString(), *Name.ToString()),
			DebugString(), FString(),
			TEXT("no compiled sheet slot owns this name — reads 0"));
		Out = FElysiumVariant::Int(0);
		return true;
	}
	return false;
}

bool FElysiumCombatCharacter::SetDynamicField(FName Name, const FElysiumVariant& Value)
{
	if (Sheet.Extra.Contains(Name) || Name.ToString().StartsWith(TEXT("base_"), ESearchCase::CaseSensitive))
	{
		Sheet.Extra.Add(Name, Value.ToInt());
		return true;
	}
	return false;
}

void FElysiumCombatCharacter::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	using EC = EElysiumTraitContainer;
	Out.Emplace(TEXT("Clan"), FString::Printf(TEXT("%d (%s), %s"), Sheet.Clan(),
		FElysiumSheet::ClanName(Sheet.Clan()), Sheet.IsMale() ? TEXT("male") : TEXT("female")));
	Out.Emplace(TEXT("Health"), FString::Printf(TEXT("%d / %d%s"), Health, MaxHealth,
		bUnkillable ? TEXT("  (unkillable)") : TEXT("")));
	Out.Emplace(TEXT("Money"), FString::FromInt(Money));
	Out.Emplace(TEXT("Blood"), FString::FromInt(Sheet.GetCurrent(EC::Attributes, ElysiumSlot::BloodPool)));
	Out.Emplace(TEXT("Humanity"), FString::FromInt(Sheet.GetCurrent(EC::Attributes, ElysiumSlot::Humanity)));
	Out.Emplace(TEXT("Masquerade"), FString::FromInt(Sheet.GetCurrent(EC::Attributes, ElysiumSlot::Masquerade)));
	Out.Emplace(TEXT("Physical"), FString::Printf(TEXT("str %d  dex %d  sta %d"),
		Sheet.GetCurrent(EC::Attributes, ElysiumSlot::Strength),
		Sheet.GetCurrent(EC::Attributes, ElysiumSlot::Dexterity),
		Sheet.GetCurrent(EC::Attributes, ElysiumSlot::Stamina)));
	Out.Emplace(TEXT("Effects"), Effects.IsEmpty()
		? FString(TEXT("(none)"))
		: FString::Printf(TEXT("%s  (%d rows)"), *FString::Join(Effects, TEXT(", ")),
			EffectLayer.IsValid() ? EffectLayer->NumRows() : 0));
	Out.Emplace(TEXT("WillTalk"), bWillTalk ? TEXT("yes") : TEXT("no"));
	Out.Emplace(TEXT("Disposition"), Disposition.IsEmpty() ? TEXT("(none)") : Disposition);
	Out.Emplace(TEXT("Unnamed stats"), FString::FromInt(Sheet.Extra.Num()));
}

// ============================================================================================
// FElysiumPlayer — CBasePlayer / CHL2_Player
// ============================================================================================

void FElysiumPlayer::Spawn()
{
	// The body is the pawn, already standing: nothing to build, and the first SyncFromBody puts the
	// entity where the pawn is.
	//
	// The health ceiling is read, not derived: `Max_Health` is an ordinary stat with `Default 100`
	// and no formula anywhere in `vdata` — nothing derives health from Stamina (RE24). A run that
	// has been through New Game arrives with the record's seeded sheet; one that has not (a map
	// loaded straight from the console) seeds here so the damage path has a track.
	if (Sheet.GetCurrent(EElysiumTraitContainer::Attributes, ElysiumSlot::MaxHealth) <= 0)
	{
		if (const FElysiumStatTable* Table = SheetRules())
		{
			Sheet.SeedFrom(*Table);
		}
	}
	// The clan is a sheet slot, so the effect layer it names can only be resolved once the sheet is
	// in place — which is here, whether it arrived from the record or was just seeded.
	RefreshClanEffects();
	SyncHealthFromSheet();
	SyncFromBody();

	if (!Model.IsEmpty())
	{
		if (IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr)
		{
			Visual = Embodiment->BuildPlayerVisual(ModelStem(), Disposition, IdleVariant());
			if (Visual)
			{
				World->RegisterNpcBody(Visual);
				GateVisual();
			}
		}
	}
}

void FElysiumPlayer::Think()
{
	// The player's only autonomous work today is the feed transaction. It runs here rather than off
	// a timer because the pulse deadline is simulation state (R4/S8): the same think that advances
	// it is the one the save's clock restores, so a load cannot duplicate or skip a pulse.
	TickFeed(World ? World->NowSeconds() : 0.0);
}

void FElysiumPlayer::RefreshClanEffects()
{
	UElysiumGameStateSubsystem* GameState = World ? World->GetGameState() : nullptr;
	UElysiumRulebookSubsystem* Rules = GameState ? GameState->Rulebook() : nullptr;
	if (!Rules)
	{
		RebuildEffects();   // no rulebook: the layer still has to match the names, which is nothing
		return;
	}
	// `clandoc000.txt` names the player templates `Player_<Clan>`, and each one names the
	// `TraitEffectGroup` carrying that clan's gift and bane — which is where every bane lives:
	// nothing about a clan is special-cased in code (`docs/vtmb/game_runtime.md` section 3).
	FString Group;
	FElysiumClanTemplate Resolved;
	if (Rules->Clans().Resolve(FString::Printf(TEXT("Player_%s"), FElysiumSheet::ClanName(Sheet.Clan())), Resolved))
	{
		Group = Resolved.GeneralStr(TEXT("ClanEffect"));
	}

	// One clan group at a time: re-running this after a clan change must replace, not accumulate.
	Effects.RemoveAll([](const FString& Name) { return Name.StartsWith(TEXT("Clan (")); });
	if (!Group.IsEmpty())
	{
		Effects.Add(Group);
	}
	RebuildEffects();
}

bool FElysiumPlayer::HasAwarded(const FString& Key) const
{
	// `Q_strnicmp` over the STORED key's length — a prefix compare, which is latent breakage VtMB
	// gets away with because no shipped key prefixes another (`Elysium.Content.Rulebook` asserts
	// the premise still holds). Reproduced as authored, not "fixed".
	for (const FElysiumXpEntry& Entry : ExperienceLog)
	{
		if (!Entry.Entry.IsEmpty() && Key.StartsWith(Entry.Entry, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}

int32 FElysiumPlayer::AwardExperience(const FString& Key)
{
	if (Key.IsEmpty())
	{
		return 0;
	}
	// 1. Give-once is the LEDGER, not an encoding: every key is give-once, unconditionally — the
	//    trailing `01` every real row carries has nothing to do with it.
	if (HasAwarded(Key))
	{
		UE_LOG(LogElysiumPlayer, Verbose, TEXT("%s AwardExperience(\"%s\") — already given"),
			*DebugString(), *Key);
		return 0;
	}

	UElysiumGameStateSubsystem* GameState = World ? World->GetGameState() : nullptr;
	UElysiumRulebookSubsystem* Rules = GameState ? GameState->Rulebook() : nullptr;
	const FElysiumExperienceEntry* Row = Rules ? Rules->Experience().Find(Key) : nullptr;
	if (!Row)
	{
		// 2. A key the table does not hold awards nothing AND APPENDS nothing, so it retries on
		//    every fire. That is the engine's own behaviour, not a leniency.
		UE_LOG(LogElysiumPlayer, Log, TEXT("%s AwardExperience(\"%s\") — no such experience_table row"),
			*DebugString(), *Key);
		return 0;
	}

	// 3. The `Experience_Modifier` bonus, above 2 XP — the file's own "the value without extra
	//    experience points". The threshold is on the RAW value, which is in hundredths.
	const int32 Value = ElysiumXp::WithModifier(Row->Value,
		Sheet.GetCurrent(EElysiumTraitContainer::Attributes, ElysiumSlot::ExpModifier));

	// 4. The key joins the ledger with the amount it was worth.
	FElysiumXpEntry Entry;
	Entry.Entry = Key;
	Entry.Amount = Value;
	ExperienceLog.Add(MoveTemp(Entry));

	// `AddExperience`: the raw value accumulates untouched, the /100 keeps its remainder, and only
	// whole points reach the sheet. Every real row being `N01`, each award banks 0.01 XP of residue
	// and one bonus point falls out per 100 awards.
	const int32 Whole = ElysiumXp::Bank(Value, ExperienceRemainder, LifetimeExperience);
	if (Whole > 0)
	{
		AddTrait(EElysiumTraitContainer::Attributes, ElysiumSlot::Experience, Whole);
	}
	UE_LOG(LogElysiumPlayer, Log, TEXT("%s AwardExperience(\"%s\") — %d raw, +%d XP (%.0f left over)"),
		*DebugString(), *Key, Value, Whole, ExperienceRemainder);
	return Whole;
}

void FElysiumPlayer::OnMasqueradeBreached()
{
	FElysiumCombatCharacter::OnMasqueradeBreached();
	// The second loss condition. Same shape as death: the substrate reports, and the session owns
	// what it means to the application (11.3's GameOver state, with its own reason).
	if (UElysiumGameStateSubsystem* State = World ? World->GetGameState() : nullptr)
	{
		State->NotifyMasqueradeBreach();
	}
}

void FElysiumPlayer::Hydrate(const FElysiumPlayerRecord& Record)
{
	Sheet      = Record.Sheet;
	Money      = Record.Money;
	Health     = Record.Health;
	MaxHealth  = Record.MaxHealth;
	Law        = Record.Law;
	ExperienceLog = Record.ExperienceLog;
	Effects    = Record.Effects;
	EmailFlags = Record.EmailFlags;
	ExperienceRemainder = Record.ExperienceRemainder;
	LifetimeExperience  = Record.LifetimeExperience;
	bUnkillable = Record.bUnkillable;
	bDeathReported = false;
	// B6 — a feed only resumes into the map it was taken in, and its handles have to be re-stamped
	// against this world's epoch (the record's copy carries a dead one, exactly as a saved handle in
	// the map snapshot does). Anything else drops the pair rather than pointing it at a stranger.
	FeedState = FElysiumFeedState();
	if (World && !Record.FeedMap.IsEmpty() && Record.FeedMap == World->MapName())
	{
		FeedState = Record.Feed;
		auto Rebase = [this](FElysiumEntityHandle& H)
		{
			H = (H.IsSet() && World->Resolve(FElysiumEntityHandle(H.Index, World->GetEpoch())))
				? FElysiumEntityHandle(H.Index, World->GetEpoch())
				: FElysiumEntityHandle::Invalid();
		};
		Rebase(FeedState.Target);
		Rebase(FeedState.Peer);
		if (!FeedState.IsPaired())
		{
			FeedState = FElysiumFeedState();
		}
		else if (!FeedState.bVictim)
		{
			// The player record owns the feeder half, while the map snapshot owns its victim. Preserve
			// the absolute phase/pulse deadlines and re-arm the player think at their earlier boundary;
			// otherwise a correctly restored pair can remain inert behind ELYSIUM_NEVER_THINK.
			NextThink = FeedState.PhaseDeadline;
			if (FeedState.IsTransacting())
			{
				NextThink = FMath::Min(NextThink, FeedState.NextPulse);
			}
		}
	}
	// The names crossed the boundary; their resolution did not — the rulebook is re-read at load,
	// which is what lets a patched rulebook re-apply to a run that started before it. Going through
	// RefreshClanEffects rather than RebuildEffects reconciles the clan group with the clan slot
	// that just arrived, so a New Game into a different clan cannot keep the old one's bane.
	RefreshClanEffects();
}

void FElysiumPlayer::Dehydrate(FElysiumPlayerRecord& Record) const
{
	Record.Sheet      = Sheet;
	Record.Money      = Money;
	Record.Health     = Health;
	Record.MaxHealth  = MaxHealth;
	Record.Law        = Law;
	Record.ExperienceLog = ExperienceLog;
	Record.Effects    = Effects;
	Record.EmailFlags = EmailFlags;
	Record.ExperienceRemainder = ExperienceRemainder;
	Record.LifetimeExperience  = LifetimeExperience;
	Record.bUnkillable = bUnkillable;
	Record.Feed = FeedState;
	Record.FeedMap = (World && FeedState.IsPaired()) ? World->MapName() : FString();
}

void FElysiumPlayer::SyncFromBody()
{
	const IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	FVector Feet; FRotator View = FRotator::ZeroRotator;
	if (!Embodiment || !Embodiment->GetPlayerFeetTransform(Feet, View))
	{
		return;   // no pawn (a menu backdrop, a headless world): the entity keeps its last position
	}
	// Written straight into the fields: SetRuntimeOrigin would call OnRuntimeTransformChanged, which
	// teleports the pawn — every frame, to where it already is.
	Origin = Feet;
	Angles = ElysiumPlayerView::ToSource(View);
}

void FElysiumPlayer::OnRuntimeTransformChanged()
{
	// The skeletal surface is attached to the pawn, so moving the pawn carries it. Do not run
	// FElysiumAnimating::OnRuntimeTransformChanged, which would treat its relative transform as a
	// map-root world transform and double-apply the placement.
	FElysiumEntity::OnRuntimeTransformChanged();
	// CreateControllerNPC snapshots a scene-owned duplicate that RemoveControllerNPC later uses as
	// the player's final pose anchor. An explicit player transform (point_teleport, console teleport,
	// or script SetOrigin/SetAngles) is authoritative while that relationship exists; carry it onto
	// the duplicate so delayed teardown cannot restore the pre-teleport mark. Ordinary pawn movement
	// reaches SyncFromBody instead and deliberately leaves a scene-staged controller independent.
	if (FElysiumEntity* Controller = World ? World->FindPlayerController() : nullptr)
	{
		Controller->SetRuntimeTransform(Origin, Angles);
	}
	if (IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr)
	{
		Embodiment->TeleportPlayer(Origin, ElysiumPlayerView::ToUnreal(Angles));
	}
}

void FElysiumPlayer::OnRuntimeModelChanged()
{
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!Embodiment)
	{
		return; // a headless world still keeps the logical model string
	}
	Embodiment->ClearPlayerVisual();
	Visual = nullptr;
	if (!Model.IsEmpty())
	{
		Visual = Embodiment->BuildPlayerVisual(ModelStem(), Disposition, IdleVariant());
		if (Visual)
		{
			World->RegisterNpcBody(Visual);
			GateVisual();
		}
	}
}

void FElysiumPlayer::GateVisual()
{
	// The pawn owns the surface. Publishing the entity's hide state and letting the pawn combine it
	// with the camera's eligibility is what keeps one flag from having two writers — the defect that
	// let a scene clip un-hide a body the camera had just put away.
	if (IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr)
	{
		Embodiment->SetPlayerBodyEntityHidden(IsInert());
	}
}

void FElysiumPlayer::OnKilled()
{
	FElysiumCombatCharacter::OnKilled();
	// The run is lost. The session owns what that means to the application (11.3's GameOver state
	// holds the world and raises its screen); the substrate only reports it, and only through the
	// game-state subsystem it was already handed.
	if (UElysiumGameStateSubsystem* State = World ? World->GetGameState() : nullptr)
	{
		State->NotifyPlayerKilled();
	}
}

void FElysiumPlayer::InputGiveItem(const FElysiumInputArgs& Args)
{
	// STRING — the item's `vdata/items` key, 126 wires game-wide. `GiveItem` exists twice, as this
	// input and as a Character method, and the two need not share an implementation; both reach the
	// one player-only service, `GiveNamedItem`.
	const FString Classname = Args.Param.ToString();
	if (!Inventory.GiveNamedItem(*this, Classname).IsSet())
	{
		// Retail's own line for a grant that did not land.
		UE_LOG(LogElysiumPlayer, Log, TEXT("%s Could not give item (\"%s\")"), *DebugString(), *Classname);
	}
}

void FElysiumPlayer::InputAwardExperience(const FElysiumInputArgs& Args)
{
	// STRING, not an amount: it names an entry the engine looks up in the experience table. The
	// handler takes the variant's string when the field type is STRING and otherwise stringifies
	// it, which a Hammer wire's string parameter satisfies either way.
	AwardExperience(Args.Param.ToString());
}

void FElysiumPlayer::InputSetCriminalLevel(const FElysiumInputArgs& Args)
{
	Law.Criminal = Args.Param.ToInt();
}

void FElysiumPlayer::InputSetInvestigateLevel(const FElysiumInputArgs& Args)
{
	Law.Investigate = Args.Param.ToInt();
}

void FElysiumPlayer::InputSetSupernaturalLevel(const FElysiumInputArgs& Args)
{
	Law.Supernatural = Args.Param.ToInt();
}

void FElysiumPlayer::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	FElysiumCombatCharacter::GetDebugState(Out);
	Out.Emplace(TEXT("Origin"), Origin.ToString());
	Out.Emplace(TEXT("Facing"), FString::Printf(TEXT("yaw %.0f"), -Angles.Y));
	Out.Emplace(TEXT("Law"), FString::Printf(TEXT("criminal %d / supernatural %d / investigate %d"),
		Law.Criminal, Law.Supernatural, Law.Investigate));
	Out.Emplace(TEXT("XP"), FString::Printf(TEXT("%d spent-able, %d awards, %.0f raw (%.0f pending)"),
		Sheet.GetCurrent(EElysiumTraitContainer::Attributes, ElysiumSlot::Experience),
		ExperienceLog.Num(), LifetimeExperience, ExperienceRemainder));
	Out.Emplace(TEXT("Body"), (World && World->Embodiment()) ? TEXT("pawn") : TEXT("(none)"));
}

// ============================================================================================
// Registration
// ============================================================================================

static TUniquePtr<FElysiumEntity> MakePlayer() { return MakeUnique<FElysiumPlayer>(); }
static TUniquePtr<FElysiumEntity> MakeViewModel() { return MakeUnique<FElysiumAnimating>(); }

// CBaseAnimating — a chain node, never a `.ents` classname, so it needs no factory.
static FElysiumClassRegistrar GRegAnimating(
	ElysiumAnimatingClassName(), ElysiumBaseClassName(), nullptr,
	[](FElysiumClassDesc& D)
	{
		// `skin` is KEY and INPUT with a null inputFunc — the keyvalue, the wire and `.skin =` are
		// the same direct write, so the field alone serves all three (entity_io.md).
		AddCharField(D, TEXT("skin"), &FElysiumAnimating::Skin);
		AddCharField(D, TEXT("default_disposition"), &FElysiumAnimating::Disposition);
		// Project save-only companion for SetDisposition's second argument. It is deliberately not a
		// script field: retail exposes the pair through the method, not as two writable attributes.
		AddCharField(D, TEXT("elysium_disposition_level"),
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
		// CLASSPTR — an entity-valued detach that never destroys (9.8).
		D.Input(TEXT("Inventory_Remove"),      [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FC&>(E).InputInventoryRemove(A); });

		// The sixteen whose system has not landed. They register so the name resolves through the
		// R2 walk and reaches a defined place — fail-closed, not missing (roadmap 11.4). An input
		// thunk is a captureless function pointer, so each row states its own name and owner.
		ELYSIUM_PENDING_INPUT(FC, FrenzyTrigger,          "P13 — disciplines and frenzy");
		ELYSIUM_PENDING_INPUT(FC, FrenzyCheck,            "P13 — disciplines and frenzy");
		// HungerCheck shares FrenzyCheck's handler in VtMB — two external names, one behaviour.
		ELYSIUM_PENDING_INPUT(FC, HungerCheck,            "P13 — disciplines and frenzy");
		ELYSIUM_PENDING_INPUT(FC, FrenzyUpdate,           "P13 — disciplines and frenzy");
		ELYSIUM_PENDING_INPUT(FC, ClearActiveDisciplines, "P13 — disciplines");
		ELYSIUM_PENDING_INPUT(FC, BarterBegin,            "9.8 — barter");
		ELYSIUM_PENDING_INPUT(FC, BarterEnd,              "9.8 — barter");
		ELYSIUM_PENDING_INPUT(FC, PlayFloat,              "8.9 — the floating HUD readout");
		ELYSIUM_PENDING_INPUT(FC, SetHeadAsCameraTarget,  "11.7 — the scripted-shot channel");
		ELYSIUM_PENDING_INPUT(FC, SetBodyAsCameraTarget,  "11.7 — the scripted-shot channel");
		ELYSIUM_PENDING_INPUT(FC, FadeHeadAsCameraTarget, "11.7 — the scripted-shot channel");
		ELYSIUM_PENDING_INPUT(FC, FadeBodyAsCameraTarget, "11.7 — the scripted-shot channel");
		// The four scripted look-at inputs. Center is registered separately from Eye even though it
		// behaves identically, because the identical behaviour is retail's own defect rather than a
		// simplification of ours — see InputLookAtEntityCenter.
		D.Input(TEXT("LookAtEntityEye"),     [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FC&>(E).InputLookAtEntityEye(A); });
		D.Input(TEXT("LookAtEntityCenter"),  [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FC&>(E).InputLookAtEntityCenter(A); });
		D.Input(TEXT("LookAtEntityOrigin"),  [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FC&>(E).InputLookAtEntityOrigin(A); });
		D.Input(TEXT("LookAtEntityDefault"), [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FC&>(E).InputLookAtEntityDefault(A); });

		// `money` is `m_iMoney`, the one counter `stats.txt` does not carry as a Stat. Humanity,
		// blood, masquerade, clan and sex are all trait slots, and arrive with the rest of the sheet.
		AddCharField(D, TEXT("money"), &FC::Money);

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
		AddCharField(D, TEXT("m_vEyeLookTarget"), &FC::EyeLookTarget);
		AddCharField(D, TEXT("m_vCurEyeTarget"), &FC::CurEyeTarget);
		AddCharField(D, TEXT("m_flEyeIntegRate"), &FC::EyeIntegRate);
		AddCharField(D, TEXT("m_hEyeLookTarget"), &FC::EyeLookTargetName);
		// The scripted-mode int sits at 0x0E68 and retail's datamap does NOT carry it, so a scripted
		// look-at does not survive a save. Registered with no flags so it is inspectable but neither
		// keyable nor saved, which reproduces that exactly.
		AddCharField(D, TEXT("m_iEyeLookMode"), &FC::EyeLookMode, EElysiumField::None);

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
		// RemoveCamera — the other half of `SetCamera`: hand the view back to the player. It clears the
		// map's one scripted camera whether a script, a wire or the theatre put it up (11.7).
		D.Input(TEXT("RemoveCamera"),    [](FElysiumEntity& E, const FElysiumInputArgs&)
			{ if (E.World) { E.World->ClearScriptedCamera(); } });
		D.Input(TEXT("PlayHUDParticle"), [](FElysiumEntity& E, const FElysiumInputArgs& A)
			{ static_cast<FP&>(E).PendingInput(TEXT("PlayHUDParticle"), TEXT("8.9 — the HUD"), A); });
		D.Input(TEXT("StopHUDParticle"), [](FElysiumEntity& E, const FElysiumInputArgs& A)
			{ static_cast<FP&>(E).PendingInput(TEXT("StopHUDParticle"), TEXT("8.9 — the HUD"), A); });
		D.Input(TEXT("Holster"),         [](FElysiumEntity& E, const FElysiumInputArgs& A)
			{ static_cast<FP&>(E).PendingInput(TEXT("Holster"), TEXT("9.8 — equipped weapons"), A); });

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
