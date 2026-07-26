// B3 — Minimal NPC presence: the `npc_*` character leaf and the `npc_maker` spawner.
//
// The first-beat path needs Jack (npc_VVampire) and blueblood_maker (npc_maker) to stop parsing as
// inert records so `trig_off_porch.OnEndTouch` resolves — WillTalk / UseInteresting /
// StartPlayerDialogRemote at Jack, Spawn at the maker — instead of dropping as `[no input]`, and so
// the characters stand their real model on the map. This is the 8.5 carve-out the beat needs: NO AI,
// no pathing, no combat. An NPC stands its glTF skeletal body (out/npc/<stem>.glb via
// IElysiumEmbodiment::BuildNpcVisual, the 8.2 path) at its origin, latches the dialog-gating inputs,
// and begins/ends a dialog "session" that fires OnDialogBegin/OnDialogEnd. `npc_maker.Spawn`
// synthesizes one child NPC at runtime.
//
// Deliberately out of scope (8.5 / B4 / B6): all AI, scripted_sequence anim-at-marker, the +use talk
// path (an NPC has no use-body yet — the porch trigger drives dialog directly), the real .dlg runner
// (B4 replaces the manual EndDialog seam), and feeding (OnFedUpon*, B6). SpawnFrequency /
// MaxLiveChildren / MaxNPCCount are ignored — a maker spawns exactly one child per Spawn input.

#include "ElysiumClassRegistry.h"
#include "ElysiumContentPaths.h"
#include "ElysiumDlg.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumWorldServices.h"

#include "Components/SkeletalMeshComponent.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"

#include <type_traits>

DEFINE_LOG_CATEGORY_STATIC(LogElysiumNpcEnt, Log, All);

// A/B toggle for the B3 NPC skeletal bodies (mirrors elysium.BrushBodies). Read in the leaf's Spawn,
// so it takes effect on the next map load: 1 stands the models, 0 leaves the NPCs bodiless records
// (their I/O still resolves — this only gates the visual).
static TAutoConsoleVariable<int32> CVarNpcBodies(
	TEXT("elysium.NpcBodies"),
	1,
	TEXT("Stand NPC glTF skeletal bodies at their origins at map load (1, default) or skip them (0)."),
	ECVF_Default);

namespace
{
	// Register a field backed by a subclass member (the base FElysiumClassDesc::Field only reaches
	// FElysiumEntity members). Mirrors AddSignField / AddLogicField — file-unique name so all of them
	// can land in one unity blob.
	template <typename TClass, typename TMember>
	void AddNpcField(FElysiumClassDesc& D, const TCHAR* Name, TMember TClass::* Member, bool bKeyable = true)
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
		else if constexpr (std::is_same_v<TMember, int32>)
		{
			Acc.Type = EElysiumVariantType::Int;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::Int(static_cast<const TClass&>(E).*Member); };
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToInt(); };
		}
		else if constexpr (std::is_same_v<TMember, FString>)
		{
			Acc.Type = EElysiumVariantType::String;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::String(static_cast<const TClass&>(E).*Member); };
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToString(); };
		}
		else
		{
			static_assert(sizeof(TMember) == 0, "AddNpcField: unsupported member type");
		}
		D.Fields.Add(FName(Name), MoveTemp(Acc));
	}
}

// ============================================================================================
// FElysiumNpc — the AI-free character leaf shared by every living `npc_*` classname. It stands a
// skeletal model at its origin and latches the dialog-gating inputs; dialogue itself is B4.
// ============================================================================================

class FElysiumNpc final : public FElysiumEntity
{
public:
	bool  bWillTalk = false;          // WillTalk latch — the NPC will start dialog when engaged
	bool  bUseInteresting = false;    // use_interesting — the NPC is a look/use target (seeded from the key)
	bool  bInDialog = false;          // a dialog session is open (OnDialogBegin fired, OnDialogEnd pending)
	int32 DialogFlags = 0;            // the StartPlayerDialogRemote param, kept for B4's runner
	int32 TimesTalked = 0;            // times_talked — dialogue interaction count (engine-written; script-read)
	FString StatTemplate;             // stattemplate — the RPG stat block name (data only in B3)
	// default_disposition — the NPC's emotional stance toward the player (authored on 242 of the
	// 243 `npc_*` entities across the exported maps; 239 of them `Neutral`). It selects the
	// animation set the NPC idles in through `vdata/system/dispositiontable.txt` (8.5). Held as
	// runtime state rather than a spawn-time constant because 9.9's `SetDisposition` — 2,510 calls,
	// 2,467 of them a `.dlg` line's action — writes it mid-conversation.
	FString Disposition;

	// The standing skeletal body, or null (bodiless npc_* like npc_VCamera, elysium.NpcBodies 0, or a
	// missing glb). Owned by the map actor; the world tears it down. This leaf only gates its visibility.
	USkeletalMeshComponent* Visual = nullptr;

	// SetAnimation(<clip>) — 21 script call sites, on props (`shelf.SetAnimation("showguns")`) and
	// on NPCs (`E.SetAnimation("cower_idle")`). On an NPC it is a plain "play this named sequence",
	// resolved through the manifest so a shared bank's clip plays as readily as one of its own.
	// Loops: VtMB's SetAnimation sets the model's *current* sequence rather than firing a one-shot
	// — the arguments the corpus passes are resting poses (`cower_idle`, `cower2_idle`, `dance0N`)
	// that have to persist. A one-shot would freeze on its last frame instead.
	void InputSetAnimation(const FElysiumInputArgs& Args) { PlayClip(Args.Param.ToString(), /*bLoop=*/true); }


	// Play a named clip on this NPC's body. False when the name resolves nothing (logged by the
	// resolver), so a caller can fall back. The seam SetAnimation, the SetGesture Character method
	// and scripted_sequence's m_iszPlay all reach animation through.
	virtual bool PlayAnimClip(const FString& ClipName, bool bLoop, float* OutSeconds) override
	{
		return PlayClip(ClipName, bLoop, OutSeconds);
	}
	virtual bool ResetAnimToIdle() override { return RefreshIdle(); }
	virtual bool SetDispositionName(const FString& NewDisposition) override
	{
		SetDispositionFromScript(NewDisposition);
		return true;
	}

	bool PlayClip(const FString& ClipName, bool bLoop, float* OutSeconds = nullptr)
	{
		IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
		if (!Embodiment || !Visual || ClipName.IsEmpty())
		{
			return false;
		}
		return Embodiment->PlayNpcClip(Visual, FPaths::GetBaseFilename(Model).ToLower(), ClipName, bLoop, OutSeconds);
	}

	// Re-run the default-idle policy — what a disposition change means for the body. 9.9 owns the
	// emotional-state half of SetDisposition; this is its animation half, and it is what makes the
	// 2,467 `.dlg` column-4 SetDisposition actions visible.
	bool RefreshIdle()
	{
		IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
		if (!Embodiment || !Visual)
		{
			return false;
		}
		return Embodiment->RefreshNpcIdle(Visual, FPaths::GetBaseFilename(Model).ToLower(), Disposition,
			FMath::Max(0, Handle.Index));
	}

	// The script-facing disposition write. Records the new stance and follows it on the body.
	void SetDispositionFromScript(const FString& NewDisposition)
	{
		if (NewDisposition.IsEmpty() || Disposition.Equals(NewDisposition, ESearchCase::IgnoreCase))
		{
			return;
		}
		Disposition = NewDisposition;
		RefreshIdle();
	}

	void InputWillTalk(const FElysiumInputArgs& Args)        { bWillTalk = Args.Param.ToInt() != 0; }
	void InputUseInteresting(const FElysiumInputArgs& Args)  { bUseInteresting = Args.Param.ToInt() != 0; }

	// StartPlayerDialogRemote opens a dialog session: fire OnDialogBegin, then run the NPC's `.dlg`
	// conversation (B4). When the `dialogname` file is missing/unloadable the session falls back to the
	// B3 seam — it waits for a manual EndDialog (ent_fire), so the beat is still driveable by hand.
	void InputStartDialog(const FElysiumInputArgs& Args)
	{
		if (IsInert() || bInDialog)
		{
			return;
		}
		bInDialog = true;
		DialogFlags = Args.Param.ToInt();
		static const FName OnDialogBegin(TEXT("OnDialogBegin"));
		FireOutput(OnDialogBegin, Args.Activator);
		UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s StartPlayerDialogRemote(%d)"), *DebugString(), DialogFlags);

		OpenConversation(Args.Activator);
	}

	// The dialog session ends: increment times_talked and fire OnDialogEnd. Reached both by the runner
	// (World::EndDialogSession routes EndDialog to `!self` when the conversation closes) and by a manual
	// ent_fire. Jack's OnDialogEnd wires DialogPostProcess(), which reads the `G` flags the dialogue's
	// field-5 actions wrote and warps the player.
	void InputEndDialog(const FElysiumInputArgs& Args)
	{
		if (!bInDialog)
		{
			return;
		}
		bInDialog = false;
		++TimesTalked;
		static const FName OnDialogEnd(TEXT("OnDialogEnd"));
		FireOutput(OnDialogEnd, Args.Activator);
		UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s EndDialog (times_talked=%d)"), *DebugString(), TimesTalked);
	}

	// Load this NPC's `dialogname` `.dlg`, open a branch conversation bound to the installed script host,
	// and hand it to the world (the visual-novel box renders it; the runner fires EndDialog on close).
	// Returns false when there is no dialogue to run, leaving bInDialog latched for the B3 manual seam.
	bool OpenConversation(const FElysiumEntityHandle& Activator)
	{
		if (!World || !Def)
		{
			return false;
		}
		const FString DialogName = Def->Keys.FindRef(TEXT("dialogname"));
		if (DialogName.IsEmpty())
		{
			return false;   // an NPC with no dialogue file — nothing to open
		}

		const FString Path = FElysiumContentPaths::DlgFromDialogname(DialogName);
		TSharedRef<FElysiumDlgFile> DlgFile = MakeShared<FElysiumDlgFile>();
		FString Err;
		if (!FElysiumDlgFile::LoadFile(Path, DlgFile.Get(), &Err))
		{
			UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s dialog load failed: %s"), *DebugString(), *Err);
			return false;
		}

		// Player gender + clan drive text selection: VtMB shows col-2 for a female PC, and the col-12
		// Malkavian variant for a Malkavian PC. Clan is the 2..8 sheet encoding (Malkavian = 4).
		const UElysiumGameStateSubsystem* GameState = World->GetGameState();
		const bool bMale = GameState ? GameState->PlayerSheet().bMale : true;
		const bool bMalk = GameState
			&& GameState->PlayerSheet().Clan == FElysiumPlayerSheet::ClanFromName(TEXT("Malkavian"));
		const FElysiumEntityHandle Self = Handle;
		FElysiumEntityWorld* W = World;

		// Field-4 conditions eval, field-4(NPC)/field-5 actions exec — both through the installed host
		// (EvalCondition also execs statements), so they land in the same `G` the level script reads and
		// obey the same live/off switch and eval log as field-6. dlgexpr -> Python via the normalizer.
		auto Cond = [W, Self, Activator](const FString& Raw) -> bool
		{
			return W->EvalCondition(ElysiumDlgExpr::ConditionToPython(Raw), Self, Activator).ToBool();
		};
		auto Act = [W, Self, Activator](const FString& Raw)
		{
			W->EvalCondition(ElysiumDlgExpr::ActionToPython(Raw), Self, Activator);
		};

		TSharedRef<FElysiumDlgConversation> Conv =
			MakeShared<FElysiumDlgConversation>(DlgFile, bMale, bMalk, MoveTemp(Cond), MoveTemp(Act));
		Conv->Start();
		World->OpenDialog(Self, Conv);
		UE_LOG(LogElysiumNpcEnt, Log, TEXT("%s opened dialogue '%s' (%d rows)"),
			*DebugString(), *DialogName, DlgFile->Lines.Num());
		return true;
	}

	virtual void Spawn() override
	{
		// Keyfields (model/angles/use_interesting/stattemplate) are already applied. Stand the body.
		if (CVarNpcBodies.GetValueOnGameThread() == 0 || !World || Model.IsEmpty())
		{
			return;   // gated off, no world, or bodiless npc_* (e.g. npc_VCamera has no model)
		}
		IElysiumEmbodiment* Embodiment = World->Embodiment();
		if (!Embodiment || !Def)
		{
			return;   // bare test world / no embodiment to build components on
		}

		// out/npc/<stem>.glb, stem = the model file's lowercased basename (verified 1:1 for every
		// tutorial NPC — no manifest lookup needed).
		const FString Stem = FPaths::GetBaseFilename(Model).ToLower();
		// Source `angles` is [pitch yaw roll]; a standing NPC needs yaw only. The Source->Unreal Y
		// reflection negates yaw (docs/rebuild-strategy.md); exact facing is cosmetic for B3.
		const FRotator Rot(0.0f, -Angles.Y, 0.0f);

		// Spread the cast across the three standing idles VtMB authors per disposition. Seeded from
		// the entity's own index so it is stable across a reload and a save/restore — a cop that
		// stood with its arms crossed must still be doing so after a load.
		Visual = Embodiment->BuildNpcVisual(Stem, Def->Origin, Rot, Embodiment->BodyScaleFor(*Def), Disposition,
			/*IdleVariant=*/FMath::Max(0, Handle.Index));
		if (Visual)
		{
			World->RegisterNpcBody(Visual);
			if (IsInert())
			{
				GateVisual();   // born hidden (start_hidden / a Spawn()-time Kill)
			}
		}
	}

	// Mirror the whole-entity dormancy switch onto the body (R6): a ScriptHidden/dead NPC is undrawn.
	virtual void OnDormancyChanged() override
	{
		FElysiumEntity::OnDormancyChanged();
		GateVisual();
	}

	// SetOrigin/SetAngles: the skeletal body is Movable, so follow it (bradbury/cemetery/downtown warp
	// and re-face NPCs). Source `angles` is [pitch yaw roll]; a standing NPC needs yaw, negated by the
	// Source->Unreal Y reflection (matches the Spawn()-time facing).
	virtual void OnRuntimeTransformChanged() override
	{
		FElysiumEntity::OnRuntimeTransformChanged();
		if (Visual)
		{
			Visual->SetRelativeLocation(Origin);
			Visual->SetRelativeRotation(FRotator(0.0f, -Angles.Y, 0.0f));
		}
	}

	// SetModel: swap the NPC's appearance (bradbury Heather goth/normal, cemetery prostitute, downtown
	// Nines). Tear the old body down and stand the new model at the same origin/facing — BuildNpcVisual
	// caches meshes per stem, so a repeated swap is cheap.
	virtual void OnRuntimeModelChanged() override
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
		if (Visual)
		{
			Visual->DestroyComponent();
			Visual = nullptr;
		}
		if (CVarNpcBodies.GetValueOnGameThread() == 0 || Model.IsEmpty())
		{
			return;   // gated off or now modelless
		}
		const FString Stem = FPaths::GetBaseFilename(Model).ToLower();
		Visual = Embodiment->BuildNpcVisual(Stem, Origin, FRotator(0.0f, -Angles.Y, 0.0f),
			/*UniformScale=*/1.f, /*Disposition=*/FString(), /*IdleVariant=*/0);
		if (Visual)
		{
			World->RegisterNpcBody(Visual);
			if (IsInert())
			{
				GateVisual();
			}
		}
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("WillTalk"), bWillTalk ? TEXT("yes") : TEXT("no"));
		Out.Emplace(TEXT("UseInteresting"), bUseInteresting ? TEXT("yes") : TEXT("no"));
		Out.Emplace(TEXT("In dialog"), bInDialog ? FString::Printf(TEXT("YES (flags %d)"), DialogFlags) : TEXT("no"));
		Out.Emplace(TEXT("Times talked"), FString::FromInt(TimesTalked));
		if (!StatTemplate.IsEmpty())
		{
			Out.Emplace(TEXT("Stat template"), StatTemplate);
		}
		Out.Emplace(TEXT("Model"), Model.IsEmpty() ? TEXT("(none)") : Model);
		Out.Emplace(TEXT("Body"), Visual ? TEXT("skeletal (standing)") : TEXT("(none)"));
	}

private:
	void GateVisual()
	{
		if (Visual)
		{
			const bool bShown = !IsInert();
			Visual->SetVisibility(bShown);
			Visual->SetComponentTickEnabled(bShown);   // pause the idle clip while hidden
		}
	}
};

// ============================================================================================
// FElysiumNpcMaker — npc_maker: a template that spawns one child NPC per Spawn input. The child's
// class is NPCType, its targetname NPCTargetname, standing the maker's model at the maker origin.
// ============================================================================================

class FElysiumNpcMaker final : public FElysiumEntity
{
public:
	bool bEnabled = true;   // Flag_StartDisabled 1 -> starts disabled (latch only; Spawn ignores it)

	virtual void Spawn() override
	{
		// Flag_StartDisabled is a maker spawnflag surrogate, not a CBaseEntity keyfield — read it raw.
		if (Def)
		{
			if (const FString* V = Def->Keys.Find(TEXT("Flag_StartDisabled")))
			{
				bEnabled = FCString::Atoi(**V) == 0;
			}
		}
	}

	// Spawn one child NPC. Retail fires blueblood_maker.Spawn without enabling it first, so Spawn does
	// not gate on bEnabled — the enabled latch would only govern an auto-spawn timer we do not model.
	void InputSpawn(const FElysiumInputArgs&)
	{
		if (!World || !Def)
		{
			return;
		}
		const FString ChildClass = Def->Keys.FindRef(TEXT("NPCType"));
		if (ChildClass.IsEmpty())
		{
			UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s Spawn: no NPCType"), *DebugString());
			return;
		}

		FElysiumEntityDef Child;
		Child.Classname = ChildClass;
		Child.TargetName = Def->Keys.FindRef(TEXT("NPCTargetname"));
		Child.Origin = Def->Origin;
		// Carry the template's appearance/identity so the child stands the maker's model.
		for (const TCHAR* Key : { TEXT("model"), TEXT("stattemplate"), TEXT("base_gender"), TEXT("use_interesting") })
		{
			if (const FString* V = Def->Keys.Find(Key))
			{
				Child.Keys.Add(Key, *V);
			}
		}

		const FElysiumEntityHandle H = World->SpawnRuntimeEntity(MoveTemp(Child));
		UE_LOG(LogElysiumNpcEnt, Log, TEXT("%s Spawn -> %s"), *DebugString(), *World->DescribeHandle(H));
	}

	void InputEnable(const FElysiumInputArgs&)  { bEnabled = true; }
	void InputDisable(const FElysiumInputArgs&) { bEnabled = false; }
	void InputToggle(const FElysiumInputArgs&)  { bEnabled = !bEnabled; }

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("Enabled"), bEnabled ? TEXT("yes") : TEXT("no"));
		if (Def)
		{
			Out.Emplace(TEXT("NPCType"), Def->Keys.FindRef(TEXT("NPCType")));
			Out.Emplace(TEXT("NPCTargetname"), Def->Keys.FindRef(TEXT("NPCTargetname")));
		}
	}
};

// --- Registration -----------------------------------------------------------------------------

static TUniquePtr<FElysiumEntity> MakeNpc()      { return MakeUnique<FElysiumNpc>(); }
static TUniquePtr<FElysiumEntity> MakeNpcMaker() { return MakeUnique<FElysiumNpcMaker>(); }

static void BuildNpcClass(FElysiumClassDesc& D)
{
	D.Input(TEXT("WillTalk"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpc&>(E).InputWillTalk(Args); });
	D.Input(TEXT("UseInteresting"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpc&>(E).InputUseInteresting(Args); });
	D.Input(TEXT("StartPlayerDialogRemote"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpc&>(E).InputStartDialog(Args); });
	// The un-suffixed spelling, wired by 4 `scripted_sequence.OnEndSequence` rows (Chunk after the
	// gallery lockpick, Jack after each tutorial walk) and 19 more across the maps. Routed to the same
	// session opener. NOT RE-confirmed as identical to the Remote form in VtMB — the assumption is
	// that both open the NPC's `dialogname` conversation, which is all this runtime does with either.
	D.Input(TEXT("StartPlayerDialog"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpc&>(E).InputStartDialog(Args); });
	D.Input(TEXT("EndDialog"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpc&>(E).InputEndDialog(Args); });
	D.Input(TEXT("SetAnimation"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpc&>(E).InputSetAnimation(Args); });

	AddNpcField(D, TEXT("use_interesting"), &FElysiumNpc::bUseInteresting);
	AddNpcField(D, TEXT("stattemplate"),    &FElysiumNpc::StatTemplate);
	AddNpcField(D, TEXT("default_disposition"), &FElysiumNpc::Disposition);
	// times_talked: santamonica/chinatown/e3/demo read `npc.times_talked` to branch first-vs-repeat
	// dialogue. Register it read-only (engine-written, script-read) so the read resolves to a defined
	// value instead of raising AttributeError. B4's dialogue runner drives the count; it stays 0 until then.
	AddNpcField(D, TEXT("times_talked"), &FElysiumNpc::TimesTalked, /*bKeyable*/ false);
}

static void BuildNpcMakerClass(FElysiumClassDesc& D)
{
	D.Input(TEXT("Spawn"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpcMaker&>(E).InputSpawn(Args); });
	D.Input(TEXT("Enable"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpcMaker&>(E).InputEnable(Args); });
	D.Input(TEXT("Disable"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpcMaker&>(E).InputDisable(Args); });
	D.Input(TEXT("Toggle"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpcMaker&>(E).InputToggle(Args); });
}

// One shared leaf per living-NPC classname (a class-for-class registration, so the registry's exact
// case-folded Find resolves each). npc_VCamera is a camera control entity with no model — left as an
// inert record for now. The two maker classnames share the maker leaf.
struct FElysiumNpcRegistrar
{
	FElysiumNpcRegistrar()
	{
		FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();

		static const TCHAR* const NpcClasses[] = {
			TEXT("npc_VVampire"), TEXT("npc_VPedestrian"), TEXT("npc_VHumanCombatant"),
			TEXT("npc_VRat"), TEXT("npc_VDialogPedestrian"), TEXT("npc_VCop"),
			TEXT("npc_VTaxiDriver"), TEXT("npc_VHuman"), TEXT("npc_VHunter"),
			TEXT("npc_VTzimisceRunner"), TEXT("npc_VNewscaster"), TEXT("npc_VAnimal"),
			TEXT("npc_VSabbatLeader"), TEXT("npc_VAndreiBlood"),
		};
		for (const TCHAR* Name : NpcClasses)
		{
			BuildNpcClass(Reg.Register(FName(Name), ElysiumBaseClassName(), &MakeNpc));
		}

		static const TCHAR* const MakerClasses[] = { TEXT("npc_maker"), TEXT("npc_maker_fleshpile") };
		for (const TCHAR* Name : MakerClasses)
		{
			BuildNpcMakerClass(Reg.Register(FName(Name), ElysiumBaseClassName(), &MakeNpcMaker));
		}
	}
};

static FElysiumNpcRegistrar GElysiumNpcRegistrar;
