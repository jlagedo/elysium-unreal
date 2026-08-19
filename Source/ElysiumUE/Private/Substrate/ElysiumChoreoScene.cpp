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
//   - `silence`/`loud` (21,898 uses) drive the jaw through `mstudiomouth_t`'s flexdesc rather than
//     through the flex controllers the expression track writes. Two divergences ride on that track
//     and are named where they are made: the envelope a `speak` event borrows from its own per-line
//     `.vcd` (ResolveLineEnvelope), and the smoothing between marker levels (`elysium.JawSmoothing`).
//   - `gesture` and `sequence` both play through the one clip player. Source layers a gesture
//     additively over a sequence; this runtime has a single clip slot, so the two collapse — the
//     same class of stated simplification as `scripted_sequence`'s missing locomotion.
//   - `position_start` holds its cast by rewriting the transform every frame. VtMB places each
//     actor once and immobilises it (MOVETYPE_NONE / SOLID_NONE / FSOLID_NOT_SOLID), restoring
//     those at completion; the end state matches, the mechanism does not.
//   - The `m_bAutomated` pause-automation block and the intro-skip global at 0x106e7e91 are not
//     reproduced — nothing in any map or script reaches either.
//
// Format, event enum, keyvalues, the completion contract: `docs/vtmb/choreographed_scenes.md`.

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
#include "Substrate/ElysiumLipTrack.h"
#include "Substrate/ElysiumSceneData.h"
#include "Substrate/ElysiumScenePlayer.h"
#include "Visual/ElysiumExpressionTable.h"

#include "Components/SkeletalMeshComponent.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include <type_traits>

DEFINE_LOG_CATEGORY_STATIC(LogElysiumChoreo, Log, All);

namespace
{
	// The sound system's lead. VtMB caches `snd_mixahead` in the scene's constructor and hands it to
	// the scene every think, which pulls each speak event's start earlier by that much so the sample
	// reaches the ear on time.
	//
	// 12.2b — the behaviour that reproduces is "speech is heard at the authored instant"; the 0.1 is
	// Source's *mixer's* lead, and inheriting it while running Unreal's mixer reproduces the wrong
	// half. Negative therefore means "ask the audio path what its own lead is", which is the default;
	// a value >= 0 forces a constant, which is how VtMB's 0.1 stays A/B-able beside the derived one.
	TAutoConsoleVariable<float> CVarSceneMixahead(
		TEXT("elysium.SceneMixahead"),
		-1.f,
		TEXT("Audio lead (seconds) a choreo scene schedules its speak events against. "
			 "Negative derives it from the running audio device; >= 0 forces that constant."),
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

	// A/B for the facial track alone, in the mould of `elysium.SceneActors`: 0 leaves every scene
	// actor's face at rest so a body-only performance can be looked at without the expression layer
	// on top of it.
	TAutoConsoleVariable<int32> CVarSceneExpressions(
		TEXT("elysium.SceneExpressions"),
		1,
		TEXT("A choreo scene drives its actors' faces from its expression events (1, default) or leaves them at rest (0)."),
		ECVF_Default);

	// The same A/B for lipsync (12.5). Independent of the expression switch for the same reason the
	// jaw is: the phoneme track and the expression track write the same controllers through the same
	// push, and telling them apart on a live face means being able to turn one off.
	TAutoConsoleVariable<int32> CVarSceneLipsync(
		TEXT("elysium.SceneLipsync"),
		1,
		TEXT("A choreo scene drives its actors' mouths from each line's .lip phoneme track (1, default) or leaves the phoneme controllers at rest (0)."),
		ECVF_Default);

	// The same A/B for the jaw. Independent of the expression switch: the two tracks meet on one face
	// but at different layers, and either alone is a thing worth looking at.
	TAutoConsoleVariable<int32> CVarSceneJaw(
		TEXT("elysium.SceneJaw"),
		1,
		TEXT("A choreo scene drives its actors' jaws from the line's amplitude envelope (1, default) or leaves them shut (0)."),
		ECVF_Default);

	// Whether a `speak` event may take its envelope from the per-line `.vcd` beside its own audio when
	// the scene it is playing in carries none. See ResolveLineEnvelope — this is a divergence.
	TAutoConsoleVariable<int32> CVarSceneJawFromLine(
		TEXT("elysium.SceneJawFromLine"),
		1,
		TEXT("A speak event with no silence/loud track in its own scene reads the envelope out of the "
		     "per-line .vcd beside its audio (1, default) or flaps only on its scene's own markers (0)."),
		ECVF_Default);

	// The jaw's resting level while a line is being spoken and no marker claims the instant.
	//
	// Measured, not chosen. Decoding each line's audio and taking the RMS envelope normalised to the
	// line's own 99th percentile, over 110 lines of the dialogue corpus and 35 of the courtroom cast's
	// own: a `silence` span averages **0.037**, a `loud` span **0.640**, and the uncovered time
	// between them **0.312**. Pinning `loud` at a fully open jaw, because that is what the marker
	// means and its own span peak runs above the line's p99 reference, puts the between level at
	// 0.312 / 0.640 = **0.49** and silence at 0.06, i.e. shut.
	TAutoConsoleVariable<float> CVarJawSpeechLevel(
		TEXT("elysium.JawSpeechLevel"),
		0.49f,
		TEXT("Jaw weight while a line plays and no silence/loud marker claims the instant."),
		ECVF_Default);

	// **A divergence, and the only invented mechanism in this track.** Nothing in the `.vcd` says a
	// marker ramps — `event_ramp` exists and all 1,401 authored uses sit on `expression` and
	// `gesture`, never on `silence` or `loud` — and what the engine does with the event after handing
	// it to the actor's AI object is not decoded. Stepping between the three levels at the span
	// boundaries is therefore the literal reading, and it reads as a hinge rather than a jaw.
	//
	// The constant is measured rather than picked: across 187 lines the wav's own envelope takes a
	// median **100 ms** to rise 10->90 % into a `loud` span and **150 ms** to fall 90->10 % into a
	// `silence` span. A first-order lag covers 10->90 % in ln(9) = 2.2 time constants, so those are
	// tau = 45 ms and tau = 68 ms; one constant of 50 ms sits between them and still lets the
	// shortest authored `loud` span (50 ms) reach most of its target.
	TAutoConsoleVariable<float> CVarJawSmoothing(
		TEXT("elysium.JawSmoothing"),
		0.05f,
		TEXT("Time constant (seconds) the jaw lags its target by; 0 steps at the span boundary."),
		ECVF_Default);

	// Subclass-member field accessor. Deliberately file-local rather than the shared
	// ElysiumAddClassField (Substrate/ElysiumClassFields.h): its bool setter keeps Python
	// truthiness for non-string variants, where the shared template always coerces via ToInt.
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
	virtual const TCHAR* SaveBlockReason() const override
	{
		return bPlaying ? TEXT("a choreographed scene is active") : nullptr;
	}
	double StartTime = 0.0;                 // absolute game seconds the scene began
	FElysiumEntityHandle Activator;         // whoever started it, carried to every output

	// One per scene actor, index-aligned with Scene->Actors.
	struct FBoundActor
	{
		FElysiumEntityHandle Handle;
		FVector SavedOrigin = FVector::ZeroVector;
		FVector SavedAngles = FVector::ZeroVector;
		bool bSaved = false;
		bool bBodyFrozen = false;           // position_start immobilised it and owes it a thaw
		bool bPlayingClip = false;          // we started a clip on it and owe it a reset
		bool bClaimed = false;              // we stamped ScriptOwner on it and owe it a release
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
	int32 NumUnresolvedExpressions = 0;
	int32 NumMissingFlexKeys = 0;
	int32 NumLinesWithLip = 0;
	int32 NumLinesWithoutLip = 0;
	int32 NumUnresolvedPhonemes = 0;
	mutable bool bLoggedMissingActor = false;
	mutable bool bLoggedMissingClip = false;
	mutable bool bLoggedMissingSpeak = false;
	mutable bool bLoggedMissingExpression = false;
	mutable bool bLoggedMissingFlexKey = false;
	mutable bool bLoggedUnresolvedPhoneme = false;

	// --- the facial track (12.3) --------------------------------------------------------------
	// One live `expression` event: its table and the row inside it, resolved once when the event
	// starts. `param`/`param2` never change, and the table cache is shared across every scene.
	struct FLiveExpression
	{
		TSharedPtr<const FElysiumExpressionTable> Table;
		int32 Row = INDEX_NONE;
	};
	// Keyed by event index, like ActiveClipEvents, so the set is save-stable and a restored scene
	// rebuilds it through RestoreEvent.
	TMap<int32, FLiveExpression> ActiveExpressions;
	// Per actor index, the last controller pose this scene pushed. Kept for two reasons: an unchanged
	// pose costs no rig evaluation, and when an expression ends the scene has to write the keys it
	// was driving back to zero — nothing else knows which those were.
	TMap<int32, TMap<FString, float>> ActorFacialPose;
	// --- lipsync (12.5 slice 2) ---------------------------------------------------------------
	// Per live `speak` event, its `.lip` phoneme track joined to the speaker's own phoneme table.
	// Keyed by event index like everything else here, so `RestoreEvent` rebuilds it for free by
	// routing a restored Speak back through SpeakLine. Absent for a line that resolved neither.
	TMap<int32, FElysiumLipSyncBinding> ActiveLipsync;
	// --- the amplitude jaw (12.5 slice 1) -----------------------------------------------------
	// One authored span of the line's amplitude envelope, on the SCENE clock.
	struct FJawSpan
	{
		float Start = 0.f;
		float End = 0.f;
		bool  bLoud = false;
	};
	// Live `silence`/`loud` events of this scene's own, by event index — the player owns the latch,
	// exactly as it does for clips and voices, so the set survives a save.
	TSet<int32> ActiveEnvelope;
	// Live `speak` events, by event index. Tracked separately from `Voices` because a line whose audio
	// did not resolve still has a mouth to move, and because the jaw reads the event's AUTHORED window
	// rather than the dispatch that the mixahead pulled earlier.
	TSet<int32> ActiveSpeakEvents;
	// Per speak event, the envelope its own per-line `.vcd` carries, event-relative and resolved once.
	// Empty for a line whose `.vcd` has no Speech Triggers channel; absent for one not looked up.
	TMap<int32, TArray<FJawSpan>> LineEnvelopes;
	// Per actor index, the smoothed weight this scene last pushed to that face.
	TMap<int32, float> ActorJaw;
	// Game seconds at the last jaw update — the smoothing's own dt, which is the frame's, not the
	// substrate think interval's.
	double JawTime = 0.0;
	int32 NumLineEnvelopes = 0;
	int32 NumLinesWithoutEnvelope = 0;

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

	virtual void PreloadForActivation() override
	{
		if (!HasScene() || World == nullptr)
		{
			return;
		}
		const FString& AnimSet = ResolveAnimSetModel();
		IElysiumEmbodiment* Embodiment = World->Embodiment();
		for (const FElysiumSceneEvent& Event : Scene->Events)
		{
			if (!Event.bActive
				|| (Event.Type != EElysiumChoreoEvent::Sequence
					&& Event.Type != EElysiumChoreoEvent::Gesture)
				|| Event.Param.IsEmpty() || !Scene->Actors.IsValidIndex(Event.ActorIndex)
				|| !Scene->Actors[Event.ActorIndex].bActive)
			{
				continue;
			}

			const FElysiumSceneActor& SceneActor = Scene->Actors[Event.ActorIndex];
			FElysiumEntity* Actor = ResolveActorByName(SceneActor.Name);
			bool bResolved = false;
			if (Actor && !AnimSet.IsEmpty())
			{
				bResolved = Actor->PreloadCinematicClip(
					AnimSet, SceneActor.BoneFrom, Event.Param);
			}
			if (Actor && !bResolved)
			{
				Actor->PreloadAnimClip(Event.Param);
				continue;
			}

			// events_player creates this stand-in immediately before the scene starts. Resolve the
			// ordinary NPC mesh permutation now from the player's model without mutating the dormant
			// entity world or adding a save-visible proxy entity.
			if (!Actor && Embodiment
				&& SceneActor.Name.Equals(TEXT("!playercontroller"), ESearchCase::IgnoreCase))
			{
				const FElysiumPlayer* PlayerEntity = World->FindPlayer();
				const FString Stem = PlayerEntity ? PlayerEntity->ModelStem() : FString();
				if (!Stem.IsEmpty() && !AnimSet.IsEmpty())
				{
					bResolved = Embodiment->PreloadCinematicClipForModel(Stem,
						/*bPlayerMaterial=*/false, AnimSet, SceneActor.BoneFrom, Event.Param);
				}
				if (!bResolved && !Stem.IsEmpty())
				{
					Embodiment->PreloadNpcClipForModel(
						Stem, /*bPlayerMaterial=*/false, Event.Param);
				}
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
	// transform so the shared cinematic animation lines up, and this runtime holds them there by
	// rewriting the transform each frame (VtMB instead immobilises the body — see the header).
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
				// The placement is the only one the scene makes. What holds the actor here for the
				// next two minutes is that its body stops simulating, exactly as VtMB's
				// SetMoveType(MOVETYPE_NONE)/SetSolid(SOLID_NONE)/AddSolidFlags(FSOLID_NOT_SOLID)
				// does — not a per-frame rewrite of a transform a live movement body is fighting.
				A->SetBodyFrozen(true);
				Bound[i].bBodyFrozen = true;
			}
		}
	}

	// --- who owns the cast's pose -------------------------------------------------------------
	// A running scene and a `scripted_sequence` beat both drive an NPC's animation, and until both
	// stamp the same claim neither can see the other. `scripted_sequence` writes `ScriptOwner`
	// (VtMB's m_pCine) already; a scene writing it too is what lets the two arbitrate at the seam
	// where one hands over to the next — sp_theatre fires its walk-out beats while the courtroom
	// chain is still finishing.
	//
	// Claimed UNLOCKED on purpose. VtMB only refuses a challenger for a priority script or an
	// authored next-script, and a scene that refused every beat outright would stall the map's flow
	// on outputs a refused `BeginSequence` never fires. The claim is a marker of who is driving, not
	// a lock: a beat may still take an actor, and this scene then stops animating the one it lost
	// instead of fighting it frame by frame.
	//
	// An actor a beat already holds is left alone rather than taken: it stays unclaimed here, which
	// is the "nobody stamped anything" case the scene has always run in, so the arbitration only ever
	// adds refusals it can act on. That also makes this safe to re-run on a load, where a restored
	// `scripted_sequence` re-stamps its own claim and the two orders are not controllable.
	void ClaimActors()
	{
		if (!ActorsEnabled())
		{
			return;
		}
		for (int32 i = 0; i < Bound.Num(); ++i)
		{
			FElysiumEntity* A = ActorAt(i);
			if (A == nullptr || (A->ScriptOwner.IsSet() && !(A->ScriptOwner == Handle)))
			{
				continue;
			}
			A->ScriptOwner = Handle;
			A->bScriptOwnerLocked = false;
			Bound[i].bClaimed = true;
		}
	}

	// Give the cast back. Runs on completion AND on cancel, like ThawActors, and only releases a
	// claim still standing in this scene's name — an actor a beat took mid-scene belongs to that
	// beat now, and clearing it here would strand the beat's own release.
	void ReleaseActors()
	{
		for (int32 i = 0; i < Bound.Num(); ++i)
		{
			if (!Bound[i].bClaimed)
			{
				continue;
			}
			Bound[i].bClaimed = false;
			FElysiumEntity* A = ActorAt(i);
			if (A != nullptr && A->ScriptOwner == Handle)
			{
				A->ScriptOwner = FElysiumEntityHandle::Invalid();
				A->bScriptOwnerLocked = false;
			}
		}
	}

	// Is this scene still the thing driving that actor's pose? False once a `scripted_sequence` beat
	// has taken it — the scene then leaves the clip it is playing alone rather than re-seeking a body
	// somebody else is now animating.
	bool DrivesActor(int32 ActorIndex) const
	{
		if (!Bound.IsValidIndex(ActorIndex) || !Bound[ActorIndex].bClaimed)
		{
			return true;   // never claimed (actors disabled, or an actor that did not resolve at Start)
		}
		const FElysiumEntity* A = ActorAt(ActorIndex);
		return A == nullptr || !A->ScriptOwner.IsSet() || A->ScriptOwner == Handle;
	}

	// A load rebuilds every body from the payload, so a scene restored mid-play owns actors whose
	// bodies are simulating again. The record says they should be frozen; make it true.
	void RefreezeRestoredActors()
	{
		for (int32 i = 0; i < Bound.Num(); ++i)
		{
			if (Bound[i].bBodyFrozen)
			{
				if (FElysiumEntity* A = ActorAt(i))
				{
					A->SetBodyFrozen(true);
				}
			}
		}
	}

	// Hand the bodies back. Runs on completion AND on cancel: a cancelled scene skips `position_end`
	// but must never leave its cast frozen, or the map keeps a cast it can no longer move.
	void ThawActors()
	{
		for (int32 i = 0; i < Bound.Num(); ++i)
		{
			if (!Bound[i].bBodyFrozen)
			{
				continue;
			}
			Bound[i].bBodyFrozen = false;
			if (FElysiumEntity* A = ActorAt(i))
			{
				A->SetBodyFrozen(false);
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

	// The lead this scene's speak events are scheduled with, resolved once per Begin — VtMB caches
	// `snd_mixahead` in the scene's constructor for the same reason, so a mid-scene device change
	// cannot shift a running timeline under itself.
	float SceneLead() const
	{
		const float Forced = CVarSceneMixahead.GetValueOnGameThread();
		if (Forced >= 0.f)
		{
			return Forced;
		}
		return (World && World->Audio()) ? World->Audio()->OutputLeadSeconds()
										 : ElysiumAudioLatency::FallbackLeadSeconds;
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

		// A level script may have replaced one or more actors since the dormant map walk (theatre's
		// castUnderstudy/courtroomSire/fillSeats do exactly that). Runtime meshes own distinct
		// USkeletons, so the map's old skeleton-bound sequences cannot serve the replacement body.
		// Re-resolve this scene against the cast that will actually play it, and complete the batch
		// before assigning StartTime: a load can delay the shot, but can never advance its clock.
		PreloadForActivation();
		if (IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr)
		{
			Embodiment->FinishAnimationPreload();
		}

		Activator = Args.Activator;
		Player.Begin(Scene, SceneLead(),
			CVarSceneMaxDuration.GetValueOnGameThread());
		bPlaying = true;
		bPaused = false;
		StartTime = World ? World->NowSeconds() : 0.0;
		NumUnresolvedActors = 0;
		NumUnresolvedClips = 0;
		NumUnresolvedSpeak = 0;
		NumUnresolvedExpressions = 0;
		NumMissingFlexKeys = 0;
		NumLinesWithLip = 0;
		NumLinesWithoutLip = 0;
		NumUnresolvedPhonemes = 0;
		NumLineEnvelopes = 0;
		NumLinesWithoutEnvelope = 0;
		bLoggedMissingActor = false;
		bLoggedMissingClip = false;
		bLoggedMissingSpeak = false;
		bLoggedMissingExpression = false;
		bLoggedMissingFlexKey = false;
		bLoggedUnresolvedPhoneme = false;
		ActiveClipEvents.Reset();
		ActiveExpressions.Reset();
		ActorFacialPose.Reset();
		ResetJaw();
		Voices.Reset();
		RestoredVoiceOffsets.Reset();
		SavedControllerRelationship = World ? World->PlayerControllerHandle() : FElysiumEntityHandle::Invalid();

		BindActors();
		ClaimActors();
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
		ThawActors();          // Cancel skips position_end (FUN_10082b80) but still frees the cast
		ReleaseActorClips();
		ReleaseActors();
		RefreshFacialPose();   // the live set is empty now, so this writes every driven key back to rest
		ShutJaws();
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
				if (FElysiumEntity* A = DrivesActor(i) ? ActorAt(i) : nullptr)
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
		ThawActors();          // FUN_10081b60's order: position_end, then the cast's body state back
		ReleaseActorClips();
		// After the clips: ReleaseActorClips crossfades each actor out of its cinematic pose, and it
		// may only do that for an actor this scene still drives.
		ReleaseActors();
		RefreshFacialPose();   // as in Cancel: the faces this scene drove go back to rest
		ShutJaws();
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
		// After the dispatch, so an expression that started this frame is already in the live set, and
		// on the scene clock, so a ramp is sampled once per frame rather than once per event.
		RefreshFacialPose();
		RefreshJaw(SceneTime, static_cast<float>(Now - JawTime));
		JawTime = Now;
		// No per-frame placement pass. VtMB's equivalent think step (FUN_100846c0) resolves the cast
		// and refreshes each actor's animation layers; it never touches origin or angles. The actors
		// hold position because SaveAndPlaceActors froze their bodies.
	}

	virtual bool BlocksNpcMakerSpawns() const override { return bPlaying; }

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
			BeginExpression(Event);
			break;

		// The line's amplitude envelope, cut from the wav at author time. It drives the jaw through
		// the model's own `mstudiomouth_t` — a flexdesc, so it lands below the flex-rule layer the
		// expression track above feeds, not beside it. The span is latched here and read by
		// RefreshJaw; nothing is pushed on the dispatch frame itself, because the target is a
		// function of the clock rather than of the crossing.
		case EElysiumChoreoEvent::Silence:
		case EElysiumChoreoEvent::Loud:
			if (const int32 EvIndex = EventIndex(Event); EvIndex != INDEX_NONE)
			{
				ActiveEnvelope.Add(EvIndex);
			}
			break;

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
		// An expression needs no per-frame callback: its intensity is a function of the scene clock,
		// and RefreshFacialPose re-reads the clock for every live event once per frame.
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
		else if (Event.Type == EElysiumChoreoEvent::Expression)
		{
			BeginExpression(Event);
		}
		else if (Event.Type == EElysiumChoreoEvent::Silence || Event.Type == EElysiumChoreoEvent::Loud)
		{
			// Continuous state, so it is rebuilt: a save taken mid-span restores the same jaw.
			if (const int32 EvIndex = EventIndex(Event); EvIndex != INDEX_NONE)
			{
				ActiveEnvelope.Add(EvIndex);
			}
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
		else if (Event.Type == EElysiumChoreoEvent::Expression)
		{
			// Dropping it out of the live set is all this does; the keys it was driving go back to
			// zero on the next RefreshFacialPose, which is also what the completion path runs.
			ActiveExpressions.Remove(EventIndex(Event));
		}
		else if (Event.Type == EElysiumChoreoEvent::Silence || Event.Type == EElysiumChoreoEvent::Loud)
		{
			ActiveEnvelope.Remove(EventIndex(Event));
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
		if (!ActorsEnabled() || Event.Param.IsEmpty() || !DrivesActor(Event.ActorIndex))
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
		if (!ActiveClipEvents.Contains(EvIndex) || !DrivesActor(Event.ActorIndex))
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
		if (!bActorStillPlaying && DrivesActor(ActorIndex))
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
		if (Event.Param.IsEmpty())
		{
			return;
		}
		// Before the audio gate: a line whose sound is missing, muted or headless still has a mouth,
		// and the jaw's clock is the scene's rather than the mixer's.
		if (const int32 EvIndex = EventIndex(Event); EvIndex != INDEX_NONE)
		{
			ActiveSpeakEvents.Add(EvIndex);
			ResolveLineEnvelope(Event, EvIndex);
			ResolveLineLipsync(Event, EvIndex);
		}
		if (World == nullptr || World->Lines() == nullptr)
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
		// The jaw half first, for the same reason SpeakLine latches before the audio gate.
		if (Event.Type == EElysiumChoreoEvent::Speak)
		{
			ActiveSpeakEvents.Remove(EventIndex(Event));
			// The controllers it was driving go back to zero on the next RefreshFacialPose, the same
			// way an expression's do.
			ActiveLipsync.Remove(EventIndex(Event));
		}
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

	// --- expression: the facial track (12.3) ---------------------------------------------------
	//
	// `param` names a Faceposer weight table under `expressions/` and `param2` a row inside it; the
	// row is a value + an influence per flex controller, and `event_ramp` is the event's own
	// intensity envelope over its span (1,393 of the 1,401 authored ramps sit on `expression`).
	// Table format and the controller chain it feeds: `docs/vtmb/facial_animation.md`.
	//
	// The resolve happens once, when the event starts. An unresolvable reference is authored
	// breakage, not a format question — corpus-wide 6 events name a table that does not exist
	// (`dialog`) and 23 name a row their table does not carry — so it is counted and reported rather
	// than warned about per frame.
	void BeginExpression(const FElysiumSceneEvent& Event)
	{
		const int32 EvIndex = EventIndex(Event);
		if (EvIndex == INDEX_NONE || Event.ActorIndex == INDEX_NONE || Event.Param.IsEmpty())
		{
			return;
		}
		FLiveExpression Live;
		Live.Table = ElysiumExpressions::Load(Event.Param, ElysiumExpressions::ExpressionClass);
		Live.Row = Live.Table.IsValid() ? Live.Table->FindRow(Event.Param2) : INDEX_NONE;
		if (Live.Row == INDEX_NONE)
		{
			++NumUnresolvedExpressions;
			if (!bLoggedMissingExpression)
			{
				bLoggedMissingExpression = true;
				UE_LOG(LogElysiumChoreo, Log,
					TEXT("%s: expression '%s' / '%s' did not resolve to a %s (further misses counted, not logged)"),
					*DebugString(), *Event.Param, *Event.Param2,
					Live.Table.IsValid() ? TEXT("row") : TEXT("table"));
			}
			return;
		}
		ActiveExpressions.Add(EvIndex, MoveTemp(Live));
	}

	// Compose every live expression into one controller pose per actor and push the result.
	//
	// Two expressions can overlap on one face — LaCroix runs a brow track beside a mouth track
	// through the courtroom — so the rows are blended rather than summed, in authored start order,
	// by Faceposer's own influence rule:
	//
	//     pose[key] = pose[key] * (1 - w) + value * w,   w = influence x ramp
	//
	// which is why a key at influence 0 leaves whatever the earlier row put there instead of pulling
	// it to zero. Influence is exactly 0 or 1 on all 267,755 shipped entries, so on this corpus the
	// blend reduces to `value x influence x ramp` over a zeroed base; it is written as the blend
	// because that is what the data means and what a fractional influence would need.
	void RefreshFacialPose()
	{
		// ActiveLipsync belongs in this guard: most of sp_theatre's scenes carry no `expression` event
		// at all, and without it the whole phoneme track would silently never tick on them.
		if (World == nullptr || !HasScene()
			|| (ActiveExpressions.IsEmpty() && ActiveLipsync.IsEmpty() && ActorFacialPose.IsEmpty()))
		{
			return;
		}
		const bool bEnabled = ActorsEnabled() && CVarSceneExpressions.GetValueOnGameThread() != 0;
		const float SceneTime = Player.GetTime();

		TMap<int32, TMap<FString, float>> Next;
		if (bEnabled)
		{
			// Walked in scene-event order rather than in the map's, because the events are sorted by
			// authored start time and that order is what decides which of two overlapping rows wins a
			// key they share.
			for (int32 i = 0; i < Scene->Events.Num(); ++i)
			{
				const FLiveExpression* Live = ActiveExpressions.Find(i);
				if (Live == nullptr || !Live->Table.IsValid() || !Live->Table->Rows.IsValidIndex(Live->Row))
				{
					continue;
				}
				const FElysiumSceneEvent& Ev = Scene->Events[i];
				const FElysiumExpressionRow& Row = Live->Table->Rows[Live->Row];
				// The ramp's times are the event's own, not the scene's.
				const float Intensity = FMath::Clamp(Ev.RampAt(SceneTime - Ev.StartTime), 0.f, 1.f);
				TMap<FString, float>& Pose = Next.FindOrAdd(Ev.ActorIndex);
				for (int32 k = 0; k < Live->Table->Keys.Num(); ++k)
				{
					const float Influence = FMath::Clamp(Row.Weights[k] * Intensity, 0.f, 1.f);
					float& Slot = Pose.FindOrAdd(Live->Table->Keys[k]);
					Slot = Slot * (1.f - Influence) + Row.Values[k] * Influence;
				}
			}
		}

		// Lipsync composes ON TOP of the expression pose, into the same map and out through the same
		// push — which is what retail does too: `SetupWeights` lerps and remaps the controllers first
		// (step 4) and accumulates visemes onto the result (step 6). Feeding `Next` rather than
		// pushing separately also inherits the release-to-zero below, the unchanged-pose skip, and the
		// missing-key accounting; a second `SetFlexControllers` call would be undone by the first.
		//
		// Its own gate, not the expression one: a scene may carry only lines, or only expressions.
		if (ActorsEnabled() && CVarSceneLipsync.GetValueOnGameThread() != 0)
		{
			TArray<FString> Unresolved;
			for (const TPair<int32, FElysiumLipSyncBinding>& Line : ActiveLipsync)
			{
				if (!Scene->Events.IsValidIndex(Line.Key) || !Line.Value.IsValid())
				{
					continue;
				}
				const FElysiumSceneEvent& Ev = Scene->Events[Line.Key];
				if (Ev.ActorIndex == INDEX_NONE)
				{
					continue;
				}
				// A speak event dispatched early by the mixahead is not speaking yet — the same gate
				// the jaw uses, and for the same reason. `.lip` times are seconds from the start of
				// the audio, and the authored start is when that audio is meant to be HEARD.
				const float LineSeconds = SceneTime - Ev.StartTime;
				if (LineSeconds < 0.f || LineSeconds > Line.Value.Track->LatestTime)
				{
					continue;
				}
				Unresolved.Reset();
				Line.Value.Accumulate(LineSeconds, Next.FindOrAdd(Ev.ActorIndex), &Unresolved);
				if (!Unresolved.IsEmpty())
				{
					NumUnresolvedPhonemes += Unresolved.Num();
					if (!bLoggedUnresolvedPhoneme)
					{
						bLoggedUnresolvedPhoneme = true;
						UE_LOG(LogElysiumChoreo, Log,
							TEXT("%s: %s carries no phoneme row named %s (further misses counted, not logged)"),
							*DebugString(), *Line.Value.Table->Stem,
							*FString::Join(Unresolved, TEXT(", ")));
					}
				}
			}
		}

		// Push per actor, including the actors that just lost their last expression: a key this scene
		// drove and no longer drives has to be written back to zero, or the face keeps the last frame
		// of an expression that ended.
		TArray<FElysiumFlexWrite> Writes;
		TArray<FString> Missing;
		for (const TPair<int32, TMap<FString, float>>& Prev : ActorFacialPose)
		{
			const TMap<FString, float>* New = Next.Find(Prev.Key);
			Writes.Reset();
			for (const TPair<FString, float>& Key : Prev.Value)
			{
				if (New == nullptr || New->Find(Key.Key) == nullptr)
				{
					Writes.Add({ Key.Key, 0.f });
				}
			}
			if (!Writes.IsEmpty())
			{
				if (FElysiumEntity* A = ActorAt(Prev.Key))
				{
					A->SetFlexControllers(Writes, nullptr);
				}
			}
		}
		for (const TPair<int32, TMap<FString, float>>& Actor : Next)
		{
			const TMap<FString, float>* Prev = ActorFacialPose.Find(Actor.Key);
			bool bChanged = Prev == nullptr || Prev->Num() != Actor.Value.Num();
			Writes.Reset(Actor.Value.Num());
			for (const TPair<FString, float>& Key : Actor.Value)
			{
				const float* Was = Prev != nullptr ? Prev->Find(Key.Key) : nullptr;
				bChanged |= Was == nullptr || *Was != Key.Value;
				Writes.Add({ Key.Key, Key.Value });
			}
			// A settled ramp re-composes to the same numbers every frame; skipping the identical push
			// keeps the rule/ramp chain from re-evaluating for a face that is not moving.
			if (!bChanged)
			{
				continue;
			}
			FElysiumEntity* A = ActorAt(Actor.Key);
			if (A == nullptr)
			{
				continue;
			}
			Missing.Reset();
			// INDEX_NONE is a face that does not exist — `!playercontroller` (20 of sp_theatre's
			// expression events sit on it and no player body carries a flexdesc), a bodiless actor, or
			// a model with no facial sidecar. All ordinary.
			if (A->SetFlexControllers(Writes, &Missing) != INDEX_NONE && !Missing.IsEmpty())
			{
				NumMissingFlexKeys += Missing.Num();
				if (!bLoggedMissingFlexKey)
				{
					bLoggedMissingFlexKey = true;
					UE_LOG(LogElysiumChoreo, Log,
						TEXT("%s: %s carries no flex controller named %s (further misses counted, not logged)"),
						*DebugString(), *A->DebugString(), *FString::Join(Missing, TEXT(", ")));
				}
			}
		}
		ActorFacialPose = MoveTemp(Next);
	}

	// --- silence / loud: the amplitude jaw (12.5 slice 1) --------------------------------------
	//
	// `SILENCE` and `LOUD` are not markers to interpret: their `param` is the span's own duration as a
	// string (exact on 21,877 of the 21,898 authored uses) and they sit on a `Speech Triggers` channel
	// beside the line's `speak`. They are the amplitude envelope of the line, cut from the wav at
	// author time, and VtMB hands them to the actor's own AI object rather than consuming them in the
	// scene — the same dispatch arm `python` takes.
	//
	// What they are NOT is a tiling. Measured over the exported corpus, consecutive spans on one actor
	// abut 2 times, overlap once and leave a gap 1,220 times; `silence` spans run 0.11-1.39 s (p50
	// 0.27) and `loud` spans 0.05-0.20 s (p50 0.11); together they cover a median 17 % of the line's
	// audio. Against the audio itself, normalised to each line's own p99 RMS, a `silence` span
	// averages 0.037, a `loud` span 0.640 and the gaps between them 0.312. So the track marks the
	// pauses and the peaks of a line and says nothing about the rest of it: roughly two of each per
	// line, not a per-syllable flap. The dense motion is the `.lip` phoneme track's, which is 12.5's
	// second slice and not here.
	//
	// Hence three levels, in this precedence: a live `loud` opens the jaw fully, a live `silence`
	// shuts it, and a line playing under neither holds `elysium.JawSpeechLevel`. `loud` outranks
	// `silence` because it is the stronger claim and because the two overlap once in the whole corpus.
	// Cancel and completion: every jaw this scene moved shuts NOW rather than lagging down over the
	// smoothing constant, because there is no next frame to lag it in — the scene stops thinking.
	void ShutJaws()
	{
		for (const TPair<int32, float>& Actor : ActorJaw)
		{
			if (FElysiumEntity* A = ActorAt(Actor.Key))
			{
				A->SetMouthOpen(0.f);
			}
		}
		ResetJaw();
	}

	void ResetJaw()
	{
		ActiveEnvelope.Reset();
		ActiveSpeakEvents.Reset();
		LineEnvelopes.Reset();
		ActorJaw.Reset();
		// Lipsync is keyed by the same live `speak` set, so it is torn down with it rather than on a
		// path of its own — which is what puts it in InputStart, both save-restore paths, ShutJaws,
		// cancel and completion without any of them naming it.
		ActiveLipsync.Reset();
		// Not the counters: they are diagnostics, and the completion path runs through here, so
		// zeroing them would leave the inspector reporting nothing about the scene that just played.
		// InputStart clears them alongside every other one.
		JawTime = World ? World->NowSeconds() : 0.0;
	}

	// A `speak` event's own per-line `.vcd` — `line1015_col_f.mp3` -> `line1015_col_f.vcd` beside it,
	// which is the unit VtMB calls a spoken line.
	//
	// **A divergence, on `elysium.SceneJawFromLine`.** Retail's map scenes only ever flap on markers
	// authored in the scene being played, and the shipped cinematics are split about evenly on whether
	// they carry any: of the 30 map-placed scene files the exported maps resolve, 16 carry an envelope
	// (all 13 of sm_hub_1's, sm_diner_1's, two of sp_tutorial_1's) and 14 do not — **including all
	// eleven of sp_theatre's**, whose 21 `speak` events would leave the whole courtroom cast
	// stone-jawed. The per-line `.vcd` beside each of those lines does carry one, cut from the same
	// wav by the same tool, so this reads the line's own authored envelope rather than inventing a
	// substitute for it. What retail does there instead is `mstudiomouth_t` driven live off the
	// playing sample by the sound engine, which this slice does not reproduce; the outcome — a jaw
	// that moves on the line's own amplitude — is the same, the source of the envelope is not.
	void ResolveLineEnvelope(const FElysiumSceneEvent& Event, int32 EvIndex)
	{
		if (EvIndex == INDEX_NONE || LineEnvelopes.Contains(EvIndex)
			|| CVarSceneJawFromLine.GetValueOnGameThread() == 0)
		{
			return;
		}
		TArray<FJawSpan>& Spans = LineEnvelopes.Add(EvIndex);
		const FString Rel = FPaths::SetExtension(ElysiumScene::NormalizeSceneRel(Event.Param), TEXT("vcd"));
		TSharedPtr<const FElysiumSceneData> Line = ElysiumScene::Load(Rel);
		if (!Line.IsValid())
		{
			++NumLinesWithoutEnvelope;
			return;
		}
		// Every envelope event in the file, whatever actor it was authored under: a per-line scene is
		// one speaker, and this is that speaker's line.
		for (const FElysiumSceneEvent& Ev : Line->Events)
		{
			if (!Ev.bActive
				|| (Ev.Type != EElysiumChoreoEvent::Silence && Ev.Type != EElysiumChoreoEvent::Loud))
			{
				continue;
			}
			FJawSpan& Span = Spans.AddDefaulted_GetRef();
			Span.Start = Ev.StartTime;
			Span.End = Ev.bHasEnd ? FMath::Max(Ev.EndTime, Ev.StartTime) : Ev.StartTime;
			Span.bLoud = Ev.Type == EElysiumChoreoEvent::Loud;
		}
		if (Spans.IsEmpty()) { ++NumLinesWithoutEnvelope; } else { ++NumLineEnvelopes; }
	}

	// --- lipsync: the three-file join for one line (12.5 slice 2) ------------------------------
	//
	// `.lip` for the phoneme timing, `expressions/<model stem>_phonemes.txt` for the weights, and the
	// model's own phoneme filter for the blend width. Resolved once when the line starts, beside
	// ResolveLineEnvelope and for the same reason: before the audio gate, because a muted or
	// unresolved line still has a mouth.
	//
	// The table is chosen by the SPEAKING ENTITY'S model, matching `client.dll`'s FUN_100C4210
	// (`"expressions/%s_%s.vfe"` over the model basename). Not by the scene's `faceposermodel`, which
	// names the cinematic animation model — `Courtroom_bip2.mdl` — and occurs once corpus-wide.
	void ResolveLineLipsync(const FElysiumSceneEvent& Event, int32 EvIndex)
	{
		if (EvIndex == INDEX_NONE || ActiveLipsync.Contains(EvIndex)
			|| CVarSceneLipsync.GetValueOnGameThread() == 0)
		{
			return;
		}
		const FElysiumEntity* Actor = ActorOf(Event);
		if (Actor == nullptr)
		{
			return;
		}
		FElysiumLipSyncBinding Binding;
		Binding.Track = ElysiumLip::Load(Event.Param);
		const FString Stem = FPaths::GetBaseFilename(Actor->Model).ToLower();
		if (!Stem.IsEmpty())
		{
			Binding.Table = ElysiumExpressions::Load(Stem, ElysiumLip::PhonemeClass);
		}
		// `phonemes` / `phonemes_male` are the fallbacks client.dll names literally. Every rigged
		// character in the shipped cast carries its own table, so this is reached only by a model
		// whose stem has none.
		if (!Binding.Table.IsValid())
		{
			Binding.Table = ElysiumExpressions::Load(TEXT("phonemes"), ElysiumLip::PhonemeClass);
		}
		// The blend width is this speaker's own `studiohdr` +232/+236 pair, read off its rig. A body
		// with no rig keeps the binding's modal default, which is what sp_theatre's three speakers
		// carry anyway — so this changes nothing here and corrects the third of the rigged cast that
		// ships the wider floor.
		Actor->GetPhonemeFilter(Binding.BlendMin, Binding.BlendMax);
		if (!Binding.IsValid())
		{
			++NumLinesWithoutLip;
			return;
		}
		++NumLinesWithLip;
		ActiveLipsync.Add(EvIndex, MoveTemp(Binding));
	}

	// The jaw's target for one actor this frame, over both envelope sources.
	//
	// Every time here is UNOFFSET scene time. Only `speak` takes the mixahead — VtMB schedules the
	// sample early so it is HEARD at the authored instant — so the jaw runs on the authored clock and
	// the audio catches up to it. Offsetting the jaw too would double the lead.
	void JawTargetsAt(float SceneTime, TMap<int32, float>& OutTargets) const
	{
		// What the two envelope sources say about one actor this instant.
		struct FJawState { bool bLoud = false; bool bSilence = false; bool bSpeaking = false; };
		TMap<int32, FJawState> State;

		// This scene's own markers, from the player's live latch.
		for (int32 EvIndex : ActiveEnvelope)
		{
			if (!Scene->Events.IsValidIndex(EvIndex))
			{
				continue;
			}
			const FElysiumSceneEvent& Ev = Scene->Events[EvIndex];
			FJawState& S = State.FindOrAdd(Ev.ActorIndex);
			(Ev.Type == EElysiumChoreoEvent::Loud ? S.bLoud : S.bSilence) = true;
		}

		// Live lines: the speaking baseline, plus whatever the line's own `.vcd` marks. A speak event
		// dispatched early by the mixahead is not speaking yet, so its authored start gates both.
		for (int32 EvIndex : ActiveSpeakEvents)
		{
			if (!Scene->Events.IsValidIndex(EvIndex))
			{
				continue;
			}
			const FElysiumSceneEvent& Ev = Scene->Events[EvIndex];
			if (SceneTime < Ev.StartTime)
			{
				continue;
			}
			FJawState& S = State.FindOrAdd(Ev.ActorIndex);
			S.bSpeaking = true;
			const TArray<FJawSpan>* Spans = LineEnvelopes.Find(EvIndex);
			if (Spans == nullptr)
			{
				continue;
			}
			const float LineTime = SceneTime - Ev.StartTime;
			for (const FJawSpan& Span : *Spans)
			{
				if (LineTime >= Span.Start && LineTime <= Span.End)
				{
					(Span.bLoud ? S.bLoud : S.bSilence) = true;
				}
			}
		}

		const float SpeechLevel = FMath::Clamp(CVarJawSpeechLevel.GetValueOnGameThread(), 0.f, 1.f);
		for (const TPair<int32, FJawState>& Pair : State)
		{
			const FJawState& S = Pair.Value;
			const float Level = S.bLoud ? 1.f : (S.bSilence ? 0.f : (S.bSpeaking ? SpeechLevel : 0.f));
			OutTargets.Add(Pair.Key, Level);
		}
	}

	// Compose the targets, lag them, and push what changed. Runs after the dispatch, like
	// RefreshFacialPose, so a marker that started this frame is already in the live set.
	void RefreshJaw(float SceneTime, float DeltaSeconds)
	{
		if (World == nullptr || !HasScene()
			|| (ActiveEnvelope.IsEmpty() && ActiveSpeakEvents.IsEmpty() && ActorJaw.IsEmpty()))
		{
			return;
		}
		const bool bEnabled = ActorsEnabled() && CVarSceneJaw.GetValueOnGameThread() != 0;

		TMap<int32, float> Targets;
		if (bEnabled)
		{
			JawTargetsAt(SceneTime, Targets);
		}

		// A first-order lag, framerate-independent. Tau 0 steps, which is the literal reading of the
		// authored spans and the A/B baseline for the smoothing divergence.
		const float Tau = FMath::Max(0.f, CVarJawSmoothing.GetValueOnGameThread());
		const float Alpha = (Tau <= 0.f || DeltaSeconds <= 0.f)
			? 1.f : 1.f - FMath::Exp(-DeltaSeconds / Tau);

		TArray<int32> Settled;
		for (TPair<int32, float>& Actor : ActorJaw)
		{
			const float* Want = Targets.Find(Actor.Key);
			Actor.Value += ((Want != nullptr ? *Want : 0.f) - Actor.Value) * Alpha;
			// Below a thousandth of a morph weight the jaw is shut; dropping the row is what stops a
			// finished scene from writing an asymptote to every face it ever posed, every frame.
			if (Want == nullptr && Actor.Value < 0.001f)
			{
				Actor.Value = 0.f;
				Settled.Add(Actor.Key);
			}
		}
		for (const TPair<int32, float>& Want : Targets)
		{
			if (!ActorJaw.Contains(Want.Key))
			{
				ActorJaw.Add(Want.Key, Want.Value * Alpha);
			}
		}
		for (const TPair<int32, float>& Actor : ActorJaw)
		{
			if (FElysiumEntity* A = ActorAt(Actor.Key))
			{
				A->SetMouthOpen(Actor.Value);
			}
		}
		for (int32 Key : Settled)
		{
			ActorJaw.Remove(Key);
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
				Player.Begin(Scene, SceneLead(),
					CVarSceneMaxDuration.GetValueOnGameThread());
				BindActors();
				const double Now = World ? World->NowSeconds() : 0.0;
				StartTime = Now - Elapsed;
				ActiveExpressions.Reset();
				ActorFacialPose.Reset();
				ResetJaw();
				ClaimActors();
				Player.RestoreTo(Elapsed, *this);
				RefreshFacialPose();
				RefreshJaw(Elapsed, 0.f);   // dt 0 snaps: a restored jaw is where the save left it
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
			Ar << B.bBodyFrozen;
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
		ActiveExpressions.Reset();
		// Not the pose: the restored actors' faces are at rest, so the scene owes them nothing to
		// clear and RefreshFacialPose below rebuilds what the restored latches imply.
		ActorFacialPose.Reset();
		ResetJaw();
		RestoredVoiceOffsets.Reset();
		for (int32 i = 0; i < FMath::Min(VoiceEvents.Num(), VoiceOffsets.Num()); ++i)
		{
			RestoredVoiceOffsets.Add(VoiceEvents[i], VoiceOffsets[i]);
		}

		if (bPlaying && HasScene())
		{
			Player.Begin(Scene, SceneLead(),
				CVarSceneMaxDuration.GetValueOnGameThread());
			if (Bound.Num() != Scene->Actors.Num())
			{
				BindActors();
			}
			const double Now = World ? World->NowSeconds() : 0.0;
			StartTime = Now - Elapsed;
			Player.RestoreLatches(PlayerTime, Started, Active, *this);
			RefreshFacialPose();
			RefreshJaw(Elapsed, 0.f);
			NextThink = static_cast<float>(Now);
			RefreezeRestoredActors();
			// Re-derived rather than serialized: `bClaimed` is a marker over live entity state, and a
			// restored beat re-stamps its own claim in its own Serialize, so re-running the claim is
			// both cheaper than a payload field and correct in either order.
			ClaimActors();
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
			// The jaw's two envelope sources, side by side, because which one a scene is running on is
			// the first thing to know when a face is not moving: this scene's own markers, or the
			// per-line `.vcd`s its speak events borrow one from.
			FString Jaws;
			for (const TPair<int32, float>& Actor : ActorJaw)
			{
				Jaws += FString::Printf(TEXT("%s %.2f  "),
					Scene->Actors.IsValidIndex(Actor.Key) ? *Scene->Actors[Actor.Key].Name : TEXT("?"),
					Actor.Value);
			}
			Out.Emplace(TEXT("Jaw"), FString::Printf(
				TEXT("%d silence + %d loud authored, %d live; %d line envelope(s), %d line(s) with none%s"),
				Scene->CountOf(EElysiumChoreoEvent::Silence), Scene->CountOf(EElysiumChoreoEvent::Loud),
				ActiveEnvelope.Num(), NumLineEnvelopes, NumLinesWithoutEnvelope,
				CVarSceneJaw.GetValueOnGameThread() == 0 ? TEXT(" [off]") : TEXT("")));
			if (!Jaws.IsEmpty())
			{
				Out.Emplace(TEXT("Jaws open"), Jaws);
			}
		}

		Out.Emplace(TEXT("Unresolved"), FString::Printf(TEXT("actors %d, clips %d, lines %d, expressions %d"),
			NumUnresolvedActors, NumUnresolvedClips, NumUnresolvedSpeak, NumUnresolvedExpressions));
		// Both facial tracks share `ActorFacialPose`, so the pose row is reported for either — a
		// lipsync-only scene is the common case (none of sp_theatre's eleven carries an expression).
		if (HasScene() && (Scene->CountOf(EElysiumChoreoEvent::Expression) > 0
			|| Scene->CountOf(EElysiumChoreoEvent::Speak) > 0))
		{
			FString Faces;
			for (const TPair<int32, TMap<FString, float>>& Actor : ActorFacialPose)
			{
				Faces += FString::Printf(TEXT("%s %d keys  "),
					Scene->Actors.IsValidIndex(Actor.Key) ? *Scene->Actors[Actor.Key].Name : TEXT("?"),
					Actor.Value.Num());
			}
			if (Scene->CountOf(EElysiumChoreoEvent::Expression) > 0)
			{
				Out.Emplace(TEXT("Expressions"), FString::Printf(TEXT("%d authored, %d live%s%s"),
					Scene->CountOf(EElysiumChoreoEvent::Expression), ActiveExpressions.Num(),
					NumMissingFlexKeys > 0
						? *FString::Printf(TEXT(", %d key(s) the model lacks"), NumMissingFlexKeys) : TEXT(""),
					CVarSceneExpressions.GetValueOnGameThread() == 0 ? TEXT(" [off]") : TEXT("")));
			}
			if (Scene->CountOf(EElysiumChoreoEvent::Speak) > 0)
			{
				Out.Emplace(TEXT("Lipsync"), FString::Printf(TEXT("%d line(s) joined, %d without a .lip, %d live%s%s"),
					NumLinesWithLip, NumLinesWithoutLip, ActiveLipsync.Num(),
					NumUnresolvedPhonemes > 0
						? *FString::Printf(TEXT(", %d phoneme(s) with no row"), NumUnresolvedPhonemes) : TEXT(""),
					CVarSceneLipsync.GetValueOnGameThread() == 0 ? TEXT(" [off]") : TEXT("")));
			}
			if (!Faces.IsEmpty())
			{
				Out.Emplace(TEXT("Faces posed"), Faces);
			}
		}
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
