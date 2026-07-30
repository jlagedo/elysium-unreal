// 12.1 — `logic_choreographed_scene`: VtMB's cinematic and dialogue player.
//
// The class is **`CSceneEntity`** in `vampire.dll` (factory FUN_100802e0, object size 0x580,
// datamap 0x10548180) — Valve's own sceneentity, forked. It owns one `.vcd` scene, binds its actors
// to live entities by name, walks its events against the game clock, and reports through seven
// outputs. 122 of them are placed across 30 maps; map data only ever sends `Start` (131 wires) and
// `Cancel` (18), and level scripts start them the same way through the datamap
// (`Find("vv_dance_4").Start()` — an input name IS a Python attribute).
//
// The outputs are the load-bearing half. `OnCompletion` is wired 69 times and gates map flow: on
// sp_tutorial_1 the alley fight's completion triggers `logic_alley_cleanup`, so a scene that never
// reports finished is a soft-lock, not a missing animation.
//
// What is reproduced: the four inputs, the seven outputs, actor binding by name, the event timeline
// with the audio mixahead, `position_start`/`position_end` actor placement, and the nine live event
// types routed to the seams that exist.
//
// What is NOT, and why:
//   - `speak` resolves and plays through the audio seam, but no `Character/dlg/**` audio is
//     exported yet, so every line currently misses. The gain curve, subtitles, `full_sound` and
//     `fixedlength`-from-asset are 12.2's.
//   - `expression` (1,431 uses) and `silence`/`loud` (21,898) are parsed, counted and deliberately
//     not dispatched — they need the flex evaluator (12.3) and the jaw track (12.5).
//   - `gesture` and `sequence` both play through the one clip player. Source layers a gesture
//     additively over a sequence; this runtime has a single clip slot, so the two collapse — the
//     same class of stated simplification as `scripted_sequence`'s missing locomotion.
//   - `position_start` round-trips origin and angles only. VtMB also saves and restores each
//     actor's solidity and solid flags; there is no per-entity solid state here to save.
//   - The `m_bAutomated` pause-automation block and the intro-skip global at 0x106e7e91 are not
//     reproduced — nothing in any map or script reaches either.
//
// Format, event enum, keyvalues, the completion contract: `docs/choreographed_scenes.md`.

#include "ElysiumClassRegistry.h"
#include "ElysiumContentPaths.h"
#include "ElysiumLineService.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMapActor.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumPlayer.h"
#include "ElysiumSaveArchive.h"
#include "Substrate/ElysiumSceneData.h"
#include "Substrate/ElysiumScenePlayer.h"

#include "Components/SkeletalMeshComponent.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"

#include <type_traits>

DEFINE_LOG_CATEGORY_STATIC(LogElysiumChoreo, Log, All);

namespace
{
	// The sound system's lead. VtMB caches `snd_mixahead` in the scene's constructor and hands it to
	// the scene every think, which pulls each speak event's start earlier by that much so the sample
	// reaches the ear on time. 0.1 is Source's own default.
	TAutoConsoleVariable<float> CVarSceneMixahead(
		TEXT("elysium.SceneMixahead"),
		0.1f,
		TEXT("Audio lead (seconds) a choreo scene schedules its speak events against."),
		ECVF_Default);

	// A ceiling on how long a scene may run before it is allowed to finish. The corpus contains
	// authored ranges that are plainly wrong — one event runs to ~1.5 million seconds — and without
	// a bound such a scene never completes and never fires OnCompletion.
	TAutoConsoleVariable<float> CVarSceneMaxDuration(
		TEXT("elysium.SceneMaxDuration"),
		600.f,
		TEXT("Longest a choreo scene may run before it is forced to completion (seconds)."),
		ECVF_Default);

	// A/B for the actor half only, in the mould of `elysium.SeqTeleport`. 0 runs the timeline and
	// every output while touching no actor transform and playing no animation — which is how the
	// alley fight's trigger and completion wiring can be verified while its cinematic animation
	// asset is still missing.
	TAutoConsoleVariable<int32> CVarSceneActors(
		TEXT("elysium.SceneActors"),
		1,
		TEXT("A choreo scene drives its actors (1, default) or runs as timing + outputs only (0)."),
		ECVF_Default);

	// `hide_ents` hides the NPCs around a scene for its duration. The mechanism is RE'd
	// (FUN_100826b0 / FUN_10082520 / FUN_100828d0) and so is most of the selection: skip the dead,
	// skip anything flagged 0x20, skip the scene's own actors, and skip an NPC already in a
	// conversation (the dialogue-partner handle the `!dialogpartner` lookup reads). What is NOT
	// decoded is the final class/relationship gate — two virtuals on the NPC's character object,
	// one returning a small enum (2/3/0x0b/0x0e pass, 4 is excluded) and one tested against the
	// player. Hiding every NPC in the alley on a guess is worse than not hiding, and no scene on
	// the acceptance path sets the key, so this ships off until that gate is decoded.
	TAutoConsoleVariable<int32> CVarSceneHideEnts(
		TEXT("elysium.SceneHideEnts"),
		0,
		TEXT("Honour a choreo scene's hide_ents keyvalue (selection filter is only partly decoded)."),
		ECVF_Default);

	// Subclass-member field accessor. File-unique name so every one of these can land in one unity
	// blob — same reason as AddSeqField / AddNpcField / AddLogicField.
	template <typename TClass, typename TMember>
	void AddSceneField(FElysiumClassDesc& D, const TCHAR* Name, TMember TClass::* Member)
	{
		static_assert(std::is_base_of_v<FElysiumEntity, TClass>, "TClass must derive from FElysiumEntity");
		FElysiumFieldAccessor Acc;
		Acc.ApplyFlags(ElysiumFieldDefault);
		if constexpr (std::is_same_v<TMember, int32>)
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
		else if constexpr (std::is_same_v<TMember, bool>)
		{
			Acc.Type = EElysiumVariantType::Bool;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::Bool(static_cast<const TClass&>(E).*Member); };
			// A map keyvalue always arrives as a String, and Source reads a bool key numerically
			// (`atoi(v) != 0`). FElysiumVariant::ToBool is Python truthiness — correct for the
			// expression layer, but it makes the literal "0" true, so a bool keyfield must not
			// use it on a string.
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V)
			{
				static_cast<TClass&>(E).*Member = V.IsString() ? (V.ToInt() != 0) : V.ToBool();
			};
		}
		else if constexpr (std::is_same_v<TMember, FString>)
		{
			Acc.Type = EElysiumVariantType::String;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::String(static_cast<const TClass&>(E).*Member); };
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToString(); };
		}
		else
		{
			static_assert(sizeof(TMember) == 0, "AddSceneField: unsupported member type");
		}
		D.Fields.Add(FName(Name), MoveTemp(Acc));
	}

	// `position_end` — where the actors are left at completion (FUN_100821f0). The corpus is
	// effectively 0-or-3 (60 and 43 uses); 1 and 2 appear once and twice.
	enum : int32
	{
		POSEND_LEAVE = 0,     // leave the actors where the animation ended
		POSEND_SCENE = 1,     // move each actor to the scene entity's origin/angles
		POSEND_RESTORE = 2,   // restore the transform saved at Start
		POSEND_BIP01 = 3,     // settle each actor onto its own bip01 bone
	};
}

// ============================================================================================
// FElysiumChoreoScene
// ============================================================================================

class FElysiumChoreoScene final : public FElysiumEntity, public IElysiumChoreoCallback
{
public:
	// --- Keyfields (datamap names, applied at Construct) ------------------------------------
	FString SceneFile;              // m_iszSceneFile
	FString Target1;                // m_iszTarget1 … an author-side cast manifest, live only
	FString Target2;                // m_iszTarget2   through the !targetN aliases, which no
	FString Target3;                // m_iszTarget3   shipped scene uses. NOT the actor binding.
	FString Target4;                // m_iszTarget4
	FString OverrideSpeechTarget;   // override_speech_target — seeds !dialogpartner
	FString BaseAnim;               // m_iszBaseAnimSet — the whole-cast cinematic model
	FString MaleAnim;               // m_iszAnimSetForMalePlayer
	FString FemaleAnim;             // m_iszAnimSetForFemalePlayer
	int32   PositionStart = 0;      // 1 = the scene owns its actors' transforms for its duration
	int32   PositionEnd = 0;        // where the actors are left at completion
	bool    bHideEnts = false;      // hide the surrounding NPCs for the scene's duration
	bool    bForceLod = false;      // force_lod_2 — LOD is not reproduced; read for the inspector
	bool    bFullSound = false;     // full_sound — 12.2's (the speak path's attenuation)

	// --- Live state --------------------------------------------------------------------------
	TSharedPtr<const FElysiumSceneData> Scene;
	FElysiumScenePlayer Player;
	bool bPlaying = false;
	bool bPaused = false;
	double StartTime = 0.0;                 // absolute game seconds the scene began
	FElysiumEntityHandle Activator;         // whoever started it, carried to every output

	// One per scene actor, index-aligned with Scene->Actors.
	struct FBoundActor
	{
		FElysiumEntityHandle Handle;
		FVector SavedOrigin = FVector::ZeroVector;
		FVector SavedAngles = FVector::ZeroVector;
		bool bSaved = false;
		bool bPlayingClip = false;          // we started a clip on it and owe it a reset
		bool bCachedBip01 = false;
		FVector CachedBipOrigin = FVector::ZeroVector;
		FVector CachedBipAngles = FVector::ZeroVector;
	};
	TArray<FBoundActor> Bound;

	// Entities hidden by hide_ents, restored at completion or cancel.
	TArray<FElysiumEntityHandle> Hidden;

	// Diagnostics surfaced in the inspector rather than spammed to the log.
	int32 NumUnresolvedActors = 0;
	int32 NumUnresolvedClips = 0;
	int32 NumUnresolvedSpeak = 0;
	mutable bool bLoggedMissingActor = false;
	mutable bool bLoggedMissingClip = false;
	mutable bool bLoggedMissingSpeak = false;
	// Event indices, not pointers, make active animation and voice ownership save-stable.
	TSet<int32> ActiveClipEvents;
	TMap<int32, FElysiumAudioVoiceHandle> Voices;
	TMap<int32, float> RestoredVoiceOffsets;
	FElysiumEntityHandle SavedControllerRelationship;

	// ---------------------------------------------------------------------------------------
	// Spawn — parse the scene up front. VtMB's Start gates on an already-parsed scene pointer
	// (FUN_100829e0 tests +0x4bc first), so a scene that will not load is inert from map load
	// rather than failing on the first trigger.
	// ---------------------------------------------------------------------------------------
	virtual void Spawn() override
	{
		if (!SceneFile.IsEmpty())
		{
			Scene = ElysiumScene::Load(SceneFile);
			if (!Scene.IsValid())
			{
				UE_LOG(LogElysiumChoreo, Warning, TEXT("%s: SceneFile '%s' did not resolve"),
					*DebugString(), *SceneFile);
			}
		}
	}

	bool HasScene() const { return Scene.IsValid() && Scene->bValid; }

	// --- Actor binding ----------------------------------------------------------------------
	// By NAME. `targetN` is an author-side manifest: over the 114 resolvable entity/scene pairs 86
	// match and 28 differ (by ordering, by a stray control character, or because the entity lists
	// no targets while the scene names an actor). Binding by name alone reproduces every shipped
	// scene; the !targetN aliases exist and nothing uses them.
	FElysiumEntity* ResolveActorByName(const FString& Name) const
	{
		if (World == nullptr || Name.IsEmpty())
		{
			return nullptr;
		}
		if (Name.Equals(TEXT("Player"), ESearchCase::IgnoreCase)
			|| Name.Equals(TEXT("!player"), ESearchCase::IgnoreCase))
		{
			return reinterpret_cast<FElysiumEntity*>(World->FindPlayer());
		}
		if (Name.Equals(TEXT("!playercontroller"), ESearchCase::IgnoreCase))
		{
			return World->FindPlayerController();
		}
		if (Name.Equals(TEXT("!dialogpartner"), ESearchCase::IgnoreCase))
		{
			if (!OverrideSpeechTarget.IsEmpty())
			{
				return World->FindByName(OverrideSpeechTarget);
			}
			return World->Resolve(World->GetOpenDialogOwner());
		}
		if (Name.Equals(TEXT("!target1"), ESearchCase::IgnoreCase)) { return World->FindByName(Target1); }
		if (Name.Equals(TEXT("!target2"), ESearchCase::IgnoreCase)) { return World->FindByName(Target2); }
		if (Name.Equals(TEXT("!target3"), ESearchCase::IgnoreCase)) { return World->FindByName(Target3); }
		if (Name.Equals(TEXT("!target4"), ESearchCase::IgnoreCase)) { return World->FindByName(Target4); }
		return World->FindByName(Name);
	}

	void BindActors()
	{
		Bound.Reset();
		NumUnresolvedActors = 0;
		if (!HasScene())
		{
			return;
		}
		Bound.SetNum(Scene->Actors.Num());
		for (int32 i = 0; i < Scene->Actors.Num(); ++i)
		{
			FElysiumEntity* E = ResolveActorByName(Scene->Actors[i].Name);
			if (E != nullptr)
			{
				Bound[i].Handle = E->Handle;
			}
			else
			{
				++NumUnresolvedActors;
				// VtMB drops that actor's events and lets the rest of the scene play. One diagnostic
				// per Start keeps a missing cast member visible without flooding every binding.
				if (!bLoggedMissingActor)
				{
					bLoggedMissingActor = true;
					UE_LOG(LogElysiumChoreo, Log, TEXT("%s: unable to find actor '%s' (further misses counted)"),
						*DebugString(), *Scene->Actors[i].Name);
				}
			}
		}
	}

	// Re-resolved every use, so an actor that died mid-scene reads null instead of dangling.
	FElysiumEntity* ActorAt(int32 Index) const
	{
		if (World == nullptr || !Bound.IsValidIndex(Index))
		{
			return nullptr;
		}
		return const_cast<FElysiumEntityWorld*>(World)->Resolve(Bound[Index].Handle);
	}

	FElysiumEntity* ActorOf(const FElysiumSceneEvent& Event) const { return ActorAt(Event.ActorIndex); }

	int32 EventIndex(const FElysiumSceneEvent& Event) const
	{
		if (!Scene.IsValid() || Scene->Events.Num() == 0)
		{
			return INDEX_NONE;
		}
		const FElysiumSceneEvent* First = Scene->Events.GetData();
		const ptrdiff_t Delta = &Event - First;
		return Delta >= 0 && Delta < Scene->Events.Num() ? static_cast<int32>(Delta) : INDEX_NONE;
	}

	FElysiumEntityHandle RebaseSavedHandle(const FElysiumEntityHandle& Saved) const
	{
		if (World == nullptr || !Saved.IsSet())
		{
			return FElysiumEntityHandle::Invalid();
		}
		const FElysiumEntityHandle Live(Saved.Index, World->GetEpoch());
		return World->Resolve(Live) != nullptr ? Live : FElysiumEntityHandle::Invalid();
	}

	bool ActorsEnabled() const { return CVarSceneActors.GetValueOnGameThread() != 0; }

	// Which anim set this run uses. FUN_100843d0 picks the male or female model off the local
	// player's IsMale() and falls back to BaseAnim; 25 entities carry each of the gendered keys and
	// 73 carry BaseAnim.
	const FString& ResolveAnimSetModel() const
	{
		const bool bFemale = World && World->FindPlayer() != nullptr
			&& !World->FindPlayer()->Sheet.IsMale();
		const FString& Gendered = bFemale ? FemaleAnim : MaleAnim;
		return Gendered.IsEmpty() ? BaseAnim : Gendered;
	}

	// --- position_start: the scene owns its actors' transforms --------------------------------
	// 70 of the 105 entities that set the key use 1, including the alley fight and all twelve of
	// sp_theatre's. At Start the actors are saved and teleported onto the scene entity's own
	// transform so the shared cinematic animation lines up; every frame they are re-pinned there.
	void SaveAndPlaceActors()
	{
		if (PositionStart != 1 || !ActorsEnabled())
		{
			return;
		}
		for (int32 i = 0; i < Bound.Num(); ++i)
		{
			if (FElysiumEntity* A = ActorAt(i))
			{
				Bound[i].SavedOrigin = A->Origin;
				Bound[i].SavedAngles = A->Angles;
				Bound[i].bSaved = true;
				A->SetRuntimeOrigin(Origin);
				A->SetRuntimeAngles(Angles);
			}
		}
	}

	void RepinActors()
	{
		if (PositionStart != 1 || !ActorsEnabled())
		{
			return;
		}
		for (int32 i = 0; i < Bound.Num(); ++i)
		{
			if (FElysiumEntity* A = ActorAt(i))
			{
				A->SetRuntimeOrigin(Origin);
				A->SetRuntimeAngles(Angles);
			}
		}
	}

	void ApplyPositionEnd()
	{
		if (!ActorsEnabled())
		{
			return;
		}
		for (int32 i = 0; i < Bound.Num(); ++i)
		{
			FElysiumEntity* A = ActorAt(i);
			if (A == nullptr)
			{
				continue;
			}
			switch (PositionEnd)
			{
			case POSEND_SCENE:
				A->SetRuntimeOrigin(Origin);
				A->SetRuntimeAngles(Angles);
				break;
			case POSEND_RESTORE:
				if (Bound[i].bSaved)
				{
					A->SetRuntimeOrigin(Bound[i].SavedOrigin);
					A->SetRuntimeAngles(Bound[i].SavedAngles);
				}
				break;
			case POSEND_BIP01:
				// EndEvent caches this before the last cinematic clip is stopped. Reading the socket
				// here would otherwise observe the restored idle, not the authored final pose.
				if (Bound[i].bCachedBip01)
				{
					A->SetRuntimeOrigin(Bound[i].CachedBipOrigin);
					A->SetRuntimeAngles(Bound[i].CachedBipAngles);
				}
				break;
			case POSEND_LEAVE:
			default:
				break;
			}
		}
	}

	// --- hide_ents ----------------------------------------------------------------------------
	void HideSurroundings()
	{
		Hidden.Reset();
		if (!bHideEnts || CVarSceneHideEnts.GetValueOnGameThread() == 0 || World == nullptr)
		{
			return;
		}
		for (const TUniquePtr<FElysiumEntity>& Ent : World->Entities())
		{
			FElysiumEntity* E = Ent.Get();
			if (E == nullptr || E->IsInert() || E == this)
			{
				continue;
			}
			if (E->AsCombatCharacter() == nullptr)
			{
				continue;   // VtMB walks the NPC list, not every entity
			}
			if (World->FindPlayer() == reinterpret_cast<FElysiumPlayer*>(E))
			{
				continue;
			}
			bool bIsActor = false;
			for (const FBoundActor& B : Bound)
			{
				if (B.Handle == E->Handle) { bIsActor = true; break; }
			}
			if (bIsActor)
			{
				continue;   // the scene's own cast is never hidden
			}
			Hidden.Add(E->Handle);
			E->ScriptHide();
		}
	}

	void UnhideSurroundings()
	{
		if (World != nullptr)
		{
			for (const FElysiumEntityHandle& H : Hidden)
			{
				if (FElysiumEntity* E = World->Resolve(H))
				{
					E->ScriptUnhide();
				}
			}
		}
		Hidden.Reset();
	}

	// --- Inputs --------------------------------------------------------------------------------
	// FUN_100829e0's order, which is what decides when OnStart fires relative to actor placement:
	// reset -> bind -> anim set -> position_start -> arm the think -> hide_ents -> OnStart LAST.
	void InputStart(const FElysiumInputArgs& Args)
	{
		if (IsInert() || bPlaying || !HasScene())
		{
			return;   // VtMB's two gates: already playing, and no parsed scene
		}

		Activator = Args.Activator;
		Player.Begin(Scene, CVarSceneMixahead.GetValueOnGameThread(),
			CVarSceneMaxDuration.GetValueOnGameThread());
		bPlaying = true;
		bPaused = false;
		StartTime = World ? World->NowSeconds() : 0.0;
		NumUnresolvedActors = 0;
		NumUnresolvedClips = 0;
		NumUnresolvedSpeak = 0;
		bLoggedMissingActor = false;
		bLoggedMissingClip = false;
		bLoggedMissingSpeak = false;
		ActiveClipEvents.Reset();
		Voices.Reset();
		RestoredVoiceOffsets.Reset();
		SavedControllerRelationship = World ? World->PlayerControllerHandle() : FElysiumEntityHandle::Invalid();

		BindActors();
		SaveAndPlaceActors();
		NextThink = static_cast<float>(StartTime);
		HideSurroundings();

		UE_LOG(LogElysiumChoreo, Verbose, TEXT("%s: start '%s' (%d actors, %d events, %.2fs)"),
			*DebugString(), *Scene->SourceRel, Scene->Actors.Num(), Scene->Events.Num(),
			Player.GetLatest());

		static const FName OnStart(TEXT("OnStart"));
		FireOutput(OnStart, Activator);
	}

	// FUN_10082ad0 is one store — nothing else happens, and the automation block behind it is
	// unreachable from any map or script.
	void InputPause(const FElysiumInputArgs&)
	{
		if (bPlaying && !bPaused)
		{
			bPaused = true;
		}
	}

	// FUN_10082b00 re-stamps m_flLastUpdateTime (+0x4a4), NOT m_flStartTime (+0x4a8) — so paused
	// time IS counted, and a resumed scene snaps forward and dispatches the pause's events in one
	// burst. Faithful, and unobservable in shipped content: no map wires Pause or Resume.
	void InputResume(const FElysiumInputArgs&)
	{
		if (bPlaying && bPaused)
		{
			bPaused = false;
			NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
		}
	}

	// FUN_10082b80: stop, unhide, fire OnCanceled — and deliberately do NOT apply position_end or
	// fire OnCompletion. A cancelled scene must not unlock what its completion would have.
	void InputCancel(const FElysiumInputArgs&)
	{
		if (!bPlaying)
		{
			return;
		}
		Player.StopActiveEvents(*this);
		ReleaseActorClips();
		UnhideSurroundings();
		bPlaying = false;
		bPaused = false;
		NextThink = ELYSIUM_NEVER_THINK;

		static const FName OnCanceled(TEXT("OnCanceled"));
		FireOutput(OnCanceled, Activator);
		UE_LOG(LogElysiumChoreo, Verbose, TEXT("%s: cancelled"), *DebugString());
	}

	void ReleaseActorClips()
	{
		if (!ActorsEnabled())
		{
			return;
		}
		for (int32 i = 0; i < Bound.Num(); ++i)
		{
			if (Bound[i].bPlayingClip)
			{
				CacheActorFinalPose(i);
				Bound[i].bPlayingClip = false;
				if (FElysiumEntity* A = ActorAt(i))
				{
					A->StopCinematicClip();
				}
			}
		}
		ActiveClipEvents.Reset();
	}

	// FUN_10081b60: stop -> final placement -> restore cast -> OnCompletion -> unhide.
	void OnSceneFinished()
	{
		Player.StopActiveEvents(*this);
		bPlaying = false;
		bPaused = false;
		NextThink = ELYSIUM_NEVER_THINK;

		ApplyPositionEnd();
		ReleaseActorClips();
		Player.Reset();

		static const FName OnCompletion(TEXT("OnCompletion"));
		FireOutput(OnCompletion, Activator);
		UnhideSurroundings();
		UE_LOG(LogElysiumChoreo, Verbose, TEXT("%s: completed"), *DebugString());
	}

	// --- The frame ------------------------------------------------------------------------------
	// The scene thinks every frame while playing: VtMB's playback think re-arms itself to curtime
	// on every call. Scene time is absolute elapsed wall-clock (curtime - m_flStartTime), never an
	// accumulator, so it cannot drift and cannot be scaled.
	virtual void Think() override
	{
		if (!bPlaying)
		{
			return;   // leave NextThink at NEVER
		}

		const double Now = World ? World->NowSeconds() : 0.0;
		// Re-arm FIRST: RunThinks clears NextThink before calling, so every path below has to keep
		// the scene thinking, including the paused one.
		NextThink = static_cast<float>(Now);
		if (bPaused)
		{
			return;
		}

		const float SceneTime = static_cast<float>(Now - StartTime);
		Player.AdvanceTo(SceneTime, *this);

		// Past the clamp with events still live: stop them so the scene can report finished. This
		// is the only thing standing between an authored 1.5-million-second range and a soft-lock.
		if (SceneTime > Player.GetLatest() && Player.ActiveEvents() > 0)
		{
			Player.StopActiveEvents(*this);
		}

		if (Player.IsFinished())
		{
			OnSceneFinished();
			return;
		}
		RepinActors();
	}

	// ============================================================================================
	// IElysiumChoreoCallback — what an event MEANS.
	// ============================================================================================

	virtual void StartEvent(const FElysiumSceneData&, const FElysiumSceneEvent& Event, float SceneTime) override
	{
		switch (Event.Type)
		{
		case EElysiumChoreoEvent::FireTrigger:
			FireTrigger(Event);
			break;

		case EElysiumChoreoEvent::Sequence:
		case EElysiumChoreoEvent::Gesture:
			PlayActorClip(Event, SceneTime);
			break;

		case EElysiumChoreoEvent::Speak:
			SpeakLine(Event, SceneTime);
			break;

		case EElysiumChoreoEvent::BodySound:
			BodySound(Event, SceneTime);
			break;

		case EElysiumChoreoEvent::Python:
			if (World != nullptr && !Event.Param.IsEmpty())
			{
				World->EnqueuePython(Event.Param, 0.0, Activator, Handle);
			}
			break;

		case EElysiumChoreoEvent::Expression:
		case EElysiumChoreoEvent::Silence:
		case EElysiumChoreoEvent::Loud:
		default:
			break;
		}
	}

	virtual void ProcessEvent(const FElysiumSceneData&, const FElysiumSceneEvent& Event, float SceneTime) override
	{
		if (Event.Type == EElysiumChoreoEvent::Sequence || Event.Type == EElysiumChoreoEvent::Gesture)
		{
			SeekActorClip(Event, SceneTime);
		}
	}

	virtual void RestoreEvent(const FElysiumSceneData&, const FElysiumSceneEvent& Event, float SceneTime) override
	{
		// Only continuous state is rebuilt. Past firetriggers and Python calls remain latched and
		// silent, which is the load-time exactly-once contract.
		if (Event.Type == EElysiumChoreoEvent::Sequence || Event.Type == EElysiumChoreoEvent::Gesture)
		{
			PlayActorClip(Event, SceneTime);
		}
		else if (Event.Type == EElysiumChoreoEvent::Speak)
		{
			SpeakLine(Event, SceneTime);
		}
		else if (Event.Type == EElysiumChoreoEvent::BodySound)
		{
			BodySound(Event, SceneTime);
		}
	}

	virtual void EndEvent(const FElysiumSceneData&, const FElysiumSceneEvent& Event, float) override
	{
		if (Event.Type == EElysiumChoreoEvent::Speak || Event.Type == EElysiumChoreoEvent::BodySound)
		{
			StopLine(Event);
		}
		else if (Event.Type == EElysiumChoreoEvent::Sequence || Event.Type == EElysiumChoreoEvent::Gesture)
		{
			EndActorClip(Event);
		}
	}

	// `firetrigger` param 1..4 -> OnTrigger1..OnTrigger4. Note the plain %d — unlike logic_case's
	// OnCase%02d. Firing an output the map never wired is a safe no-op, which is what makes
	// sp_tutorial_1's triggers 3 and 4 (authored, unwired) cost nothing.
	void FireTrigger(const FElysiumSceneEvent& Event)
	{
		const int32 N = FCString::Atoi(*Event.Param);
		if (N < 1 || N > 4)
		{
			UE_LOG(LogElysiumChoreo, Verbose, TEXT("%s: firetrigger param '%s' out of range"),
				*DebugString(), *Event.Param);
			return;
		}
		FireOutput(FName(*FString::Printf(TEXT("OnTrigger%d"), N)), Activator);
	}

	void PlayActorClip(const FElysiumSceneEvent& Event, float SceneTime)
	{
		if (!ActorsEnabled() || Event.Param.IsEmpty())
		{
			return;
		}
		FElysiumEntity* A = ActorOf(Event);
		if (A == nullptr)
		{
			return;
		}

		bool bResolved = false;
		const FString& AnimSet = ResolveAnimSetModel();
		if (!AnimSet.IsEmpty() && Bound.IsValidIndex(Event.ActorIndex))
		{
			const FString& Root = Scene->Actors[Event.ActorIndex].BoneFrom;
			bResolved = A->PlayCinematicClip(AnimSet, Root, Event.Param, /*bLoop=*/false);
		}
		if (!bResolved)
		{
			bResolved = A->PlayAnimClip(Event.Param, /*bLoop=*/false);
		}
		if (!bResolved)
		{
			++NumUnresolvedClips;
			if (!bLoggedMissingClip)
			{
				bLoggedMissingClip = true;
				UE_LOG(LogElysiumChoreo, Log,
					TEXT("%s: clip '%s' did not resolve on %s (further misses counted, not logged)"),
					*DebugString(), *Event.Param, *A->DebugString());
			}
			return;
		}

		const int32 EvIndex = EventIndex(Event);
		if (EvIndex != INDEX_NONE)
		{
			ActiveClipEvents.Add(EvIndex);
		}
		if (Bound.IsValidIndex(Event.ActorIndex))
		{
			Bound[Event.ActorIndex].bPlayingClip = true;
			Bound[Event.ActorIndex].bCachedBip01 = false;
		}
		SeekActorClip(Event, SceneTime);
	}

	void SeekActorClip(const FElysiumSceneEvent& Event, float SceneTime)
	{
		const int32 EvIndex = EventIndex(Event);
		if (!ActiveClipEvents.Contains(EvIndex))
		{
			return;
		}
		if (FElysiumEntity* A = ActorOf(Event))
		{
			A->SeekCinematicClip(FMath::Max(0.f, SceneTime - Event.StartTime));
		}
	}

	void CacheActorFinalPose(int32 ActorIndex)
	{
		if (!Bound.IsValidIndex(ActorIndex))
		{
			return;
		}
		FElysiumEntity* A = ActorAt(ActorIndex);
		USkeletalMeshComponent* Skel = A ? A->GetSkeletalBody() : nullptr;
		const FName Bip01(TEXT("Bip01"));
		if (Skel == nullptr || !Skel->DoesSocketExist(Bip01))
		{
			return;
		}
		Skel->TickAnimation(0.f, /*bNeedsValidRootMotion=*/false);
		Skel->RefreshBoneTransforms();
		const FTransform Root = Skel->GetSocketTransform(Bip01, RTS_World);
		Bound[ActorIndex].CachedBipOrigin = Root.GetLocation();
		Bound[ActorIndex].CachedBipAngles = FVector(0.f, -Root.Rotator().Yaw, 0.f);
		Bound[ActorIndex].bCachedBip01 = true;
	}

	void EndActorClip(const FElysiumSceneEvent& Event)
	{
		const int32 EvIndex = EventIndex(Event);
		if (!ActiveClipEvents.Remove(EvIndex))
		{
			return;
		}
		const int32 ActorIndex = Event.ActorIndex;
		CacheActorFinalPose(ActorIndex);
		bool bActorStillPlaying = false;
		for (int32 Other : ActiveClipEvents)
		{
			if (Scene->Events.IsValidIndex(Other) && Scene->Events[Other].ActorIndex == ActorIndex)
			{
				bActorStillPlaying = true;
				break;
			}
		}
		if (Bound.IsValidIndex(ActorIndex))
		{
			Bound[ActorIndex].bPlayingClip = bActorStillPlaying;
		}
		if (!bActorStillPlaying)
		{
			if (FElysiumEntity* A = ActorAt(ActorIndex))
			{
				A->StopCinematicClip();
			}
		}
	}

	// FUN_10081700: build sound/<param>, swap the extension for .mp3, and play that if the
	// filesystem has it — else the authored .wav. The shipped lines are .mp3 on disk while every
	// `.vcd` names a `.wav`, so this rule is what makes any of them resolve at all.
	float VoiceOffset(const FElysiumSceneEvent& Event, float SceneTime)
	{
		const int32 Index = EventIndex(Event);
		if (float* Restored = RestoredVoiceOffsets.Find(Index))
		{
			const float Offset = FMath::Max(0.f, *Restored);
			RestoredVoiceOffsets.Remove(Index);
			return Offset;
	}
		return FMath::Max(0.f, SceneTime - Event.StartTime);
	}

	void SpeakLine(const FElysiumSceneEvent& Event, float SceneTime)
	{
		if (World == nullptr || World->Lines() == nullptr || Event.Param.IsEmpty())
		{
			return;
		}
		USceneComponent* AttachTo = nullptr;
		FVector LineOrigin = Origin;
		if (FElysiumEntity* A = ActorOf(Event))
		{
			AttachTo = A->GetSkeletalBody();
			LineOrigin = A->Origin;
		}
		// param2 is a dB level ("70dB"). Parsed and carried; the dB->gain curve and full_sound are
		// 12.2's, so the line plays at the seam's own level for now.
		const FString Session = FString::Printf(TEXT("scene:%u:%d:event:%d"),
			Handle.Epoch, Handle.Index, EventIndex(Event));
		const FElysiumAudioVoiceHandle Voice = World->Lines()->PlayDirect(
			Session, Event.Param, LineOrigin, AttachTo, EElysiumAudioCategory::Dialogue,
			VoiceOffset(Event, SceneTime));
		if (Voice.IsValid())
		{
			Voices.Add(EventIndex(Event), Voice);
		}
		else
		{
			++NumUnresolvedSpeak;
		}
	}

	void StopLine(const FElysiumSceneEvent& Event)
	{
		if (World == nullptr || World->Audio() == nullptr)
		{
			return;
		}
		const int32 Index = EventIndex(Event);
		if (const FElysiumAudioVoiceHandle* Voice = Voices.Find(Index))
		{
			World->Audio()->StopVoice(*Voice, 0.f);
			Voices.Remove(Index);
		}
	}

	// One authored use corpus-wide. Same resolution as speak; param2 is a dB level defaulting to
	// 80 and floored at 75.
	void BodySound(const FElysiumSceneEvent& Event, float SceneTime)
	{
		if (World == nullptr || World->Lines() == nullptr || Event.Param.IsEmpty())
		{
			return;
		}
		USceneComponent* AttachTo = nullptr;
		FVector LineOrigin = Origin;
		if (FElysiumEntity* A = ActorOf(Event))
		{
			AttachTo = A->GetSkeletalBody();
			LineOrigin = A->Origin;
		}
		const FString Session = FString::Printf(TEXT("scene:%u:%d:event:%d"),
			Handle.Epoch, Handle.Index, EventIndex(Event));
		const FElysiumAudioVoiceHandle Voice = World->Lines()->PlayDirect(
			Session, Event.Param, LineOrigin, AttachTo, EElysiumAudioCategory::Sfx,
			VoiceOffset(Event, SceneTime));
		if (Voice.IsValid())
		{
			Voices.Add(EventIndex(Event), Voice);
		}
		else
		{
			++NumUnresolvedSpeak;
		}
	}


	// --- Persistence ------------------------------------------------------------------------
	// Elapsed is the wall-clock base (pause never rebases it); PlayerTime is the last processed
	// event time. Saving both is what restores a paused pose and still catches up faithfully later.
	virtual void Serialize(FElysiumSaveArchive& Ar) override
	{
		float Elapsed = bPlaying ? static_cast<float>((World ? World->NowSeconds() : 0.0) - StartTime) : 0.f;
		Ar << bPlaying;
		Ar << bPaused;
		Ar << Elapsed;

		// Version-6 leaf state ended here. Its outer payload remains readable: derive active ranges
		// from elapsed time, but never replay a past instantaneous output.
		if (Ar.IsLoading() && Ar.AtEnd())
		{
			if (bPlaying && HasScene())
			{
				Player.Begin(Scene, CVarSceneMixahead.GetValueOnGameThread(),
					CVarSceneMaxDuration.GetValueOnGameThread());
				BindActors();
				const double Now = World ? World->NowSeconds() : 0.0;
				StartTime = Now - Elapsed;
				Player.RestoreTo(Elapsed, *this);
				NextThink = static_cast<float>(Now);
			}
			else
			{
				bPlaying = false;
				bPaused = false;
				NextThink = ELYSIUM_NEVER_THINK;
			}
			return;
		}

		float PlayerTime = Player.GetTime();
		TArray<uint8> Started;
		TArray<uint8> Active;
		if (Ar.IsSaving())
		{
			Player.CaptureLatches(Started, Active);
			SavedControllerRelationship = World
				? World->PlayerControllerHandle() : FElysiumEntityHandle::Invalid();
		}
		Ar << PlayerTime;
		Ar << Activator;
		Ar << Started;
		Ar << Active;

		int32 BoundCount = Bound.Num();
		Ar << BoundCount;
		if (Ar.IsLoading())
		{
			Bound.SetNum(FMath::Max(0, BoundCount));
		}
		for (FBoundActor& B : Bound)
		{
			Ar << B.Handle;
			Ar << B.SavedOrigin;
			Ar << B.SavedAngles;
			Ar << B.bSaved;
			Ar << B.bPlayingClip;
			Ar << B.bCachedBip01;
			Ar << B.CachedBipOrigin;
			Ar << B.CachedBipAngles;
		}

		int32 HiddenCount = Hidden.Num();
		Ar << HiddenCount;
		if (Ar.IsLoading())
		{
			Hidden.SetNum(FMath::Max(0, HiddenCount));
		}
		for (FElysiumEntityHandle& H : Hidden)
		{
			Ar << H;
		}
		Ar << SavedControllerRelationship;

		TArray<int32> VoiceEvents;
		TArray<float> VoiceOffsets;
		if (Ar.IsSaving() && Scene.IsValid())
		{
			for (const TPair<int32, FElysiumAudioVoiceHandle>& Pair : Voices)
			{
				if (Pair.Value.IsValid() && Scene->Events.IsValidIndex(Pair.Key))
				{
					VoiceEvents.Add(Pair.Key);
					VoiceOffsets.Add(FMath::Max(0.f, Elapsed - Scene->Events[Pair.Key].StartTime));
				}
			}
		}
		Ar << VoiceEvents;
		Ar << VoiceOffsets;

		if (!Ar.IsLoading())
		{
			return;
		}

		Activator = RebaseSavedHandle(Activator);
		for (FBoundActor& B : Bound)
		{
			B.Handle = RebaseSavedHandle(B.Handle);
		}
		for (FElysiumEntityHandle& H : Hidden)
		{
			H = RebaseSavedHandle(H);
		}
		Hidden.RemoveAll([](const FElysiumEntityHandle& H) { return !H.IsSet(); });
		SavedControllerRelationship = RebaseSavedHandle(SavedControllerRelationship);
		Voices.Reset();
		ActiveClipEvents.Reset();
		RestoredVoiceOffsets.Reset();
		for (int32 i = 0; i < FMath::Min(VoiceEvents.Num(), VoiceOffsets.Num()); ++i)
		{
			RestoredVoiceOffsets.Add(VoiceEvents[i], VoiceOffsets[i]);
		}

		if (bPlaying && HasScene())
		{
			Player.Begin(Scene, CVarSceneMixahead.GetValueOnGameThread(),
				CVarSceneMaxDuration.GetValueOnGameThread());
			if (Bound.Num() != Scene->Actors.Num())
			{
				BindActors();
			}
			const double Now = World ? World->NowSeconds() : 0.0;
			StartTime = Now - Elapsed;
			Player.RestoreLatches(PlayerTime, Started, Active, *this);
			NextThink = static_cast<float>(Now);
		}
		else
		{
			bPlaying = false;
			bPaused = false;
			NextThink = ELYSIUM_NEVER_THINK;
		}
	}

	// --- Inspector ----------------------------------------------------------------------------
	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("Scene file"), SceneFile.IsEmpty() ? TEXT("(none)") : SceneFile);
		Out.Emplace(TEXT("Resolved"), HasScene()
			? FElysiumContentPaths::SceneFile(Scene->SourceRel) : FString(TEXT("MISSING")));
		if (HasScene())
		{
			Out.Emplace(TEXT("Parsed"), FString::Printf(TEXT("%d actors, %d channels, %d events (v%d, fps %g)"),
				Scene->Actors.Num(), Scene->Channels.Num(), Scene->Events.Num(), Scene->Version, Scene->Fps));
		}
		Out.Emplace(TEXT("State"), bPlaying ? (bPaused ? TEXT("paused") : TEXT("playing")) : TEXT("idle"));
		if (bPlaying)
		{
			Out.Emplace(TEXT("Scene time"), FString::Printf(TEXT("%.2f / %.2f s (active %d)"),
				Player.GetTime(), Player.GetLatest(), Player.ActiveEvents()));
		}

		for (int32 i = 0; i < Bound.Num(); ++i)
		{
			const FElysiumEntity* A = ActorAt(i);
			Out.Emplace(FString::Printf(TEXT("Actor \"%s\""), *Scene->Actors[i].Name),
				A != nullptr ? A->DebugString() : FString(TEXT("unresolved")));
		}

		if (HasScene())
		{
			// The counters that say what this task deliberately does not do yet.
			FString Unrouted;
			const TPair<EElysiumChoreoEvent, const TCHAR*> Deferred[] = {
				{ EElysiumChoreoEvent::Expression, TEXT("expression (12.3)") },
				{ EElysiumChoreoEvent::Silence,    TEXT("silence (12.5)") },
				{ EElysiumChoreoEvent::Loud,       TEXT("loud (12.5)") },
			};
			for (const TPair<EElysiumChoreoEvent, const TCHAR*>& D : Deferred)
			{
				if (const int32 N = Scene->CountOf(D.Key))
				{
					Unrouted += FString::Printf(TEXT("%s %d  "), D.Value, N);
				}
			}
			Out.Emplace(TEXT("Unrouted"), Unrouted.IsEmpty() ? TEXT("none") : *Unrouted);
		}

		Out.Emplace(TEXT("Unresolved"), FString::Printf(TEXT("actors %d, clips %d, lines %d"),
			NumUnresolvedActors, NumUnresolvedClips, NumUnresolvedSpeak));
		Out.Emplace(TEXT("Placement"), FString::Printf(TEXT("position_start %d, position_end %d"),
			PositionStart, PositionEnd));
		Out.Emplace(TEXT("Flags"), FString::Printf(TEXT("hide_ents %d%s, full_sound %d, force_lod_2 %d (not read)"),
			bHideEnts ? 1 : 0,
			(bHideEnts && CVarSceneHideEnts.GetValueOnGameThread() == 0) ? TEXT(" [off]") : TEXT(""),
			bFullSound ? 1 : 0, bForceLod ? 1 : 0));
		if (!BaseAnim.IsEmpty())
		{
			Out.Emplace(TEXT("Anim set"), BaseAnim);
		}
	}
};

// --- Registration -------------------------------------------------------------------------------

static TUniquePtr<FElysiumEntity> MakeChoreoScene() { return MakeUnique<FElysiumChoreoScene>(); }

static void BuildChoreoSceneClass(FElysiumClassDesc& D)
{
	D.Input(TEXT("Start"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumChoreoScene&>(E).InputStart(Args); });
	D.Input(TEXT("Pause"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumChoreoScene&>(E).InputPause(Args); });
	D.Input(TEXT("Resume"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumChoreoScene&>(E).InputResume(Args); });
	D.Input(TEXT("Cancel"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumChoreoScene&>(E).InputCancel(Args); });

	// The map data spells these exactly as Hammer wrote them; the registry folds case.
	AddSceneField(D, TEXT("SceneFile"),              &FElysiumChoreoScene::SceneFile);
	AddSceneField(D, TEXT("target1"),                &FElysiumChoreoScene::Target1);
	AddSceneField(D, TEXT("target2"),                &FElysiumChoreoScene::Target2);
	AddSceneField(D, TEXT("target3"),                &FElysiumChoreoScene::Target3);
	AddSceneField(D, TEXT("target4"),                &FElysiumChoreoScene::Target4);
	AddSceneField(D, TEXT("override_speech_target"), &FElysiumChoreoScene::OverrideSpeechTarget);
	AddSceneField(D, TEXT("BaseAnim"),               &FElysiumChoreoScene::BaseAnim);
	AddSceneField(D, TEXT("MaleAnim"),               &FElysiumChoreoScene::MaleAnim);
	AddSceneField(D, TEXT("FemaleAnim"),             &FElysiumChoreoScene::FemaleAnim);
	AddSceneField(D, TEXT("position_start"),         &FElysiumChoreoScene::PositionStart);
	AddSceneField(D, TEXT("position_end"),           &FElysiumChoreoScene::PositionEnd);
	AddSceneField(D, TEXT("hide_ents"),              &FElysiumChoreoScene::bHideEnts);
	AddSceneField(D, TEXT("force_lod_2"),            &FElysiumChoreoScene::bForceLod);
	AddSceneField(D, TEXT("full_sound"),             &FElysiumChoreoScene::bFullSound);
	// `force_lod` is deliberately absent: 61 entities set it and no such string exists in
	// vampire.dll — the keyvalue lookup drops the wire, exactly like `OnEnterMapHere` on a
	// point_teleport. Only `force_lod_2` is real.
}

static FElysiumClassRegistrar GRegChoreoScene(
	TEXT("logic_choreographed_scene"), ElysiumBaseClassName(), &MakeChoreoScene, &BuildChoreoSceneClass);

// --- elysium.scene ------------------------------------------------------------------------------
//
// The reader's own verb, in the role `elysium.ents` plays for the entity defs: verify the parse
// before the world consumes it. `parse` needs no world and no map, which is what makes it usable
// over the whole 5,444-file corpus rather than the three scenes a map happens to place.

static void DumpSceneData(const FElysiumSceneData& S, FOutputDevice& Ar)
{
	Ar.Logf(TEXT("  %s  (v%d, fps %g, snap %s)  %d actors, %d channels, %d events, latest %.2fs"),
		*S.SourceRel, S.Version, S.Fps, S.bSnap ? TEXT("on") : TEXT("off"),
		S.Actors.Num(), S.Channels.Num(), S.Events.Num(), S.LatestTime);
	for (const FElysiumSceneActor& A : S.Actors)
	{
		FString Extra;
		if (!A.BoneFrom.IsEmpty()) { Extra = FString::Printf(TEXT("  bonerename %s -> %s"), *A.BoneFrom, *A.BoneTo); }
		Ar.Logf(TEXT("    actor \"%s\"%s"), *A.Name, *Extra);
	}
	for (const FElysiumSceneEvent& E : S.Events)
	{
		Ar.Logf(TEXT("    %8.3f %s %-13s \"%s\"  param='%s'%s%s"),
			E.StartTime,
			E.bHasEnd ? *FString::Printf(TEXT("-%8.3f"), E.EndTime) : TEXT("  (instant)"),
			ElysiumScene::EventTypeName(E.Type), *E.Name, *E.Param,
			E.Param2.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" param2='%s'"), *E.Param2),
			E.bFixedLength ? TEXT(" fixedlength") : TEXT(""));
	}
	if (S.NumDegenerate > 0)
	{
		Ar.Logf(TEXT("    (%d degenerate time range(s) clamped)"), S.NumDegenerate);
	}
}

static FAutoConsoleCommandWithWorldArgsAndOutputDevice GElysiumSceneCmd(
	TEXT("elysium.scene"),
	TEXT("Choreo scenes: `elysium.scene` lists the live ones, `<name>` dumps one, "
	     "`parse <SceneFile>` reads any path off disk, `cache [clear]` reports the parse cache."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
		{
			if (Args.Num() >= 1 && Args[0].Equals(TEXT("cache"), ESearchCase::IgnoreCase))
			{
				if (Args.Num() >= 2 && Args[1].Equals(TEXT("clear"), ESearchCase::IgnoreCase))
				{
					ElysiumScene::ClearCache();
					Ar.Logf(TEXT("scene cache cleared"));
					return;
				}
				int32 Entries = 0, Hits = 0, Misses = 0;
				ElysiumScene::CacheStats(Entries, Hits, Misses);
				Ar.Logf(TEXT("scene cache: %d entries, %d hits, %d misses"), Entries, Hits, Misses);
				return;
			}

			if (Args.Num() >= 2 && Args[0].Equals(TEXT("parse"), ESearchCase::IgnoreCase))
			{
				// No world needed: this is the corpus-facing half.
				const FString Rel = ElysiumScene::NormalizeSceneRel(Args[1]);
				FString Text;
				const FString Full = FElysiumContentPaths::SceneFile(Rel);
				if (!FFileHelper::LoadFileToString(Text, *Full))
				{
					Ar.Logf(ELogVerbosity::Warning, TEXT("no scene at %s"), *Full);
					return;
				}
				FElysiumSceneData S;
				ElysiumScene::ParseText(Text, Rel, S);
				Ar.Logf(TEXT("%s"), S.bValid ? TEXT("parsed:") : TEXT("parsed (INVALID — names no actor):"));
				DumpSceneData(S, Ar);
				return;
			}

			UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
			UElysiumMapSubsystem* Maps = GI ? GI->GetSubsystem<UElysiumMapSubsystem>() : nullptr;
			AElysiumMapActor* Map = Maps ? Maps->GetCurrentMap() : nullptr;
			FElysiumEntityWorld* EW = Map ? Map->GetEntityWorld() : nullptr;
			if (EW == nullptr)
			{
				Ar.Logf(ELogVerbosity::Warning, TEXT("no live entity world (try `elysium.scene parse <SceneFile>`)"));
				return;
			}

			int32 Found = 0;
			for (const TUniquePtr<FElysiumEntity>& Ent : EW->Entities())
			{
				FElysiumChoreoScene* S = Ent.IsValid() && Ent->Class != nullptr
					&& Ent->Class->ClassName == FName(TEXT("logic_choreographed_scene"))
					? static_cast<FElysiumChoreoScene*>(Ent.Get()) : nullptr;
				if (S == nullptr)
				{
					continue;
				}
				if (Args.Num() >= 1 && !S->TargetName.Equals(Args[0], ESearchCase::IgnoreCase))
				{
					continue;
				}
				++Found;

				if (Args.Num() >= 1)
				{
					Ar.Logf(TEXT("%s"), *S->DebugString());
					TArray<TPair<FString, FString>> State;
					S->GetDebugState(State);
					for (const TPair<FString, FString>& Row : State)
					{
						Ar.Logf(TEXT("  %-14s %s"), *Row.Key, *Row.Value);
					}
					if (S->HasScene())
					{
						DumpSceneData(*S->Scene, Ar);
					}
				}
				else
				{
					Ar.Logf(TEXT("  %-28s %-8s %s"), *S->TargetName,
						S->bPlaying ? (S->bPaused ? TEXT("paused") : TEXT("playing")) : TEXT("idle"),
						S->HasScene() ? *S->Scene->SourceRel : TEXT("(no scene)"));
				}
			}
			if (Found == 0)
			{
				Ar.Logf(TEXT("no logic_choreographed_scene%s on this map"),
					Args.Num() >= 1 ? TEXT(" by that name") : TEXT(""));
			}
		}));
