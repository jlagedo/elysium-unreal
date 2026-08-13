// Placeholder classes for the classnames the shipped maps wire but no leaf implements yet.
//
// Dispatch already fails loudly without this file: `FElysiumEntityWorld::DeliverInputTo` reports
// every input the R2 walk cannot resolve, so an unregistered classname's wires are on the stub work
// list the moment they fire. What this file adds is the list *before* it fires — the surface is
// enumerable from `elysium.classes` and `elysium.stubs` on a map that never reaches the scene, so
// the gap can be scoped without playing to it.
//
// The table is derived from the shipped content, not from a wishlist: every row is a classname with
// no `Substrate/` implementation that at least one exported map fires a **non-base** input at. Two
// exclusions matter and are deliberate:
//
//   * Base inputs (`Kill`, `ScriptHide`, `ScriptUnhide`, `PlayDialogFile`, `SetSoundOverrideEnt`,
//     `SetFakeSilence`) never appear here. They resolve through the base chain and *work*; naming
//     one in a stub row would shadow a real implementation with a warning.
//   * A classname whose every wire is a base input is absent entirely — `inspection_node`,
//     `trigger_stealth_mod` and `info_node_cover_corner` take only `Kill`/`ScriptHide`, so no wire
//     of theirs is dead even though their behaviour is unbuilt. Their gap is a body/AI gap, not an
//     I/O one, and overstating it here would make the work list lie.
//
// A row claims nothing about behaviour. It says: this name is fired, it lands nowhere, and here is
// what it was aimed at. `FElysiumClassRegistry::RegisterStub` skips a name an implementation has
// already claimed, so a row left behind when its class lands is inert rather than harmful.

#include "ElysiumClassRegistry.h"

#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumStub.h"

namespace
{
	struct FStubClassRow
	{
		const TCHAR* ClassName;
		const TCHAR* Inputs;   // space-separated; every one a non-base input a shipped map fires
		const TCHAR* Owner;    // what would implement it, for the stub report's owner line
	};

	// Ordered by how hard the shipped content leans on the class (total wires across the exported
	// maps), so the head of the table is the head of the work list.
	const FStubClassRow GStubClasses[] =
	{
		{ TEXT("env_sprite"),                    TEXT("HideSprite ShowSprite TurnOn TurnOff"),
		  TEXT("sprite visibility — no env_sprite class; the bake carries the sprite, nothing toggles it") },
		{ TEXT("ambient_soundscheme"),           TEXT("FadeIn FadeOut Disable"),
		  TEXT("the SoundScheme manager owns the scheme file, but no entity class receives its wires") },
		{ TEXT("light_spot"),                    TEXT("FadeToPattern TurnOff TurnOn SetPattern"),
		  TEXT("lights are re-derived from .lights at load; no entity class, so light switching is unbuilt") },
		{ TEXT("camera_cinematic"),              TEXT("EndShot StartShot"),
		  TEXT("the scripted-shot channel") },
		{ TEXT("func_particle"),                 TEXT("TurnOff TurnOn"),
		  TEXT("brush-bound particle emitters") },
		{ TEXT("env_physimpact"),                TEXT("Impact"),
		  TEXT("scripted physics impulses") },
		{ TEXT("intersting_place_conversation"), TEXT("PlayOneOffSound"),
		  TEXT("ambient-conversation places") },
		{ TEXT("env_shooter"),                   TEXT("Shoot"),
		  TEXT("gib/debris emitters") },
		{ TEXT("env_particle_hud"),              TEXT("TurnOn"),
		  TEXT("the HUD particle layer") },
		{ TEXT("npc_VVampireBoss"),              TEXT("StartPlayerDialog TransformModel ClearPatrolPath FollowPatrolPath SetBodyAsCameraTarget SetupPatrolType WillTalk"),
		  TEXT("the boss NPC leaf") },
		{ TEXT("light"),                         TEXT("SetPattern TurnOff Toggle TurnOn"),
		  TEXT("lights are re-derived from .lights at load; no entity class, so light switching is unbuilt") },
		{ TEXT("env_shake"),                     TEXT("StartShake StopShake"),
		  TEXT("screen shake") },
		{ TEXT("prop_slashable"),                TEXT("Skin"),
		  TEXT("the slashable-prop leaf") },
		{ TEXT("npc_payphone"),                  TEXT("WillTalk"),
		  TEXT("the payphone dialogue prop") },
		{ TEXT("security_camera"),               TEXT("Disable Enable"),
		  TEXT("the security-camera leaf") },
		{ TEXT("aiscripted_schedule"),           TEXT("StartSchedule"),
		  TEXT("AI scheduling") },
		{ TEXT("env_physexplosion"),             TEXT("Explode"),
		  TEXT("scripted physics explosions") },
		{ TEXT("npc_VCamera"),                   TEXT("TweakParam"),
		  TEXT("the camera NPC leaf") },
		{ TEXT("info_node_crosswalk"),           TEXT("DontWalk Walk"),
		  TEXT("pedestrian crosswalk AI") },
		{ TEXT("prop_mover"),                    TEXT("MoveToDest"),
		  TEXT("the scripted mover prop") },
		{ TEXT("trigger_discipline_context"),    TEXT("Disable Enable"),
		  TEXT("P13 — disciplines") },
		{ TEXT("func_areaportal"),               TEXT("Open Close"),
		  TEXT("visibility portals — the renderer has no equivalent to drive") },
		{ TEXT("npc_VProneDialog"),              TEXT("StartPlayerDialogRemote StartPlayerDialog"),
		  TEXT("the prone-dialogue NPC leaf") },
		{ TEXT("trigger_player_activity_level"), TEXT("Enable Disable"),
		  TEXT("the player activity-level trigger") },
		{ TEXT("trigger_inventory_check"),       TEXT("Disable Enable"),
		  TEXT("9.8 — inventory") },
		{ TEXT("phys_convert"),                  TEXT("ConvertTarget"),
		  TEXT("runtime physics conversion") },
		{ TEXT("hud_timer"),                     TEXT("Hide RestartTimer Show"),
		  TEXT("8.9 — the HUD") },
		{ TEXT("prop_radio"),                    TEXT("Deactivate"),
		  TEXT("the radio prop") },
		{ TEXT("func_breakable"),                TEXT("Break"),
		  TEXT("breakable brushes") },
		{ TEXT("npc_VLasombra"),                 TEXT("SpawnTempParticle"),
		  TEXT("the Lasombra NPC leaf") },
		{ TEXT("trigger_teleport"),              TEXT("Disable Enable"),
		  TEXT("the teleport trigger") },
		{ TEXT("func_movelinear"),               TEXT("Open"),
		  TEXT("linear movers") },
		{ TEXT("info_node_cover_low"),           TEXT("DisableHint"),
		  TEXT("AI cover hints") },
		{ TEXT("game_ui"),                       TEXT("Activate"),
		  TEXT("the direct player-control handoff") },
		{ TEXT("prop_padlock"),                  TEXT("Unlock"),
		  TEXT("the padlock prop") },
		{ TEXT("logic_visibility_test"),         TEXT("CheckVisibility"),
		  TEXT("the line-of-sight test entity") },
		{ TEXT("func_physbox"),                  TEXT("Wake"),
		  TEXT("physics brushes") },
		{ TEXT("game_text"),                     TEXT("DisplayWindow"),
		  TEXT("8.9 — the HUD") },
		{ TEXT("phys_ballsocket"),               TEXT("Break"),
		  TEXT("physics constraints") },
		{ TEXT("phys_constraint"),               TEXT("Break"),
		  TEXT("physics constraints") },
		{ TEXT("trigger_checkvolume"),           TEXT("CheckNow"),
		  TEXT("the volume-occupancy test") },
		{ TEXT("point_explosion"),               TEXT("Explode"),
		  TEXT("scripted explosions") },
	};

	// One thunk for all 74 rows. It reads its own name off the dispatch context rather than being
	// generated per row, which is the whole reason FElysiumInputArgs carries `Input`.
	void StubInput(FElysiumEntity& Self, const FElysiumInputArgs& Args)
	{
		const FString Owner = Self.Class ? Self.Class->StubOwner : FString();
		ElysiumStub::Fired(TEXT("input"),
			FString::Printf(TEXT("%s.%s"),
				Self.Def ? *Self.Def->Classname : TEXT("?"), *Args.Input.ToString()),
			Self.DebugString(), ElysiumStub::DescribeInput(Args), Owner);
	}

	// Registration runs once at module load, before any Create/lookup, like every other registrar.
	struct FStubClassRegistrar
	{
		FStubClassRegistrar()
		{
			FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
			for (const FStubClassRow& Row : GStubClasses)
			{
				FElysiumClassDesc* Desc = Reg.RegisterStub(FName(Row.ClassName), ElysiumBaseClassName());
				if (!Desc)
				{
					continue;   // an implementation owns this name now — nothing to stand in for
				}
				Desc->StubOwner = Row.Owner;
				FString Rest(Row.Inputs);
				while (!Rest.IsEmpty())
				{
					FString Name;
					if (!Rest.Split(TEXT(" "), &Name, &Rest)) { Name = MoveTemp(Rest); Rest.Reset(); }
					if (!Name.IsEmpty()) { Desc->Input(FName(*Name), &StubInput); }
				}
			}
		}
	};

	const FStubClassRegistrar GStubClassRegistrar;
}
