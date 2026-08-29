#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"

// The +use context-icon layer.
//
// `use_icon`/`locked_icon` index a 72-entry table of `hud/Context_Icons/<name>` materials
// (entity_io.md "use_icon enum", recovered from the client.dll pointer array at file offset
// 0x2700b4). `use_icon N` selects entry N-1; `use_icon 0` means no icon. `locked_icon` is the
// reticle when the entity is use-locked (VtMB GetUseIcon = FUN_100c8940 returns locked_icon when
// the locked byte +0x5c4 is set). The offline `UE_use_icons.py` bakes the 72 materials + the ring/
// back frame into `out/hud/use_icons.png` + `.json` (per-icon atlas UVs); the runtime HUD draws
// the cell for GetUseIcon() over the reticle, and the Cog Entity Inspector names the index below.

// The dedicated +use trace channel. Declared in Config/DefaultEngine.ini as
// ECC_GameTraceChannel1 = "ElysiumUse" (a trace type, DefaultResponse Block). Keep the two in sync.
inline constexpr ECollisionChannel ELYSIUM_USE_CHANNEL = ECC_GameTraceChannel1;

// The 72-entry use_icon name table (1-based, matching the keyvalue). Names are the engine-neutral
// `hud/Context_Icons/<name>` identifiers from entity_io.md; 42/43 (phonograph), 49/56 (bustopmap)
// and 54/55 (breakable) are genuine duplicate slots. Returns "(none)" for 0 and "?" out of range.
// Used by the Cog inspector to label an entity's use_icon/locked_icon; the HUD reads atlas UVs from
// the JSON, so this table carries no layout — only the human-readable name.
inline const TCHAR* ElysiumUseIconName(int32 N)
{
	static const TCHAR* Names[] = {
		TEXT("CarryBody"), TEXT("Hacking"), TEXT("Intrusion"), TEXT("Key"), TEXT("accesscard"),
		TEXT("Lootable"), TEXT("Monitor"), TEXT("Phone"), TEXT("PhysicsHand"), TEXT("Portal"),
		TEXT("Stakeable"), TEXT("Switchable"), TEXT("Sewer"), TEXT("Talk_Female"), TEXT("Talk_Male"),
		TEXT("Nosferatu_Warning"), TEXT("Use_Bomb"), TEXT("Note"), TEXT("stealth_succeed"),
		TEXT("stealth_chance"), TEXT("Button_1"), TEXT("Button_2"), TEXT("Button_3"), TEXT("Button_4"),
		TEXT("Button_5"), TEXT("Button_6"), TEXT("Button_7"), TEXT("Button_8"), TEXT("Button_G"),
		TEXT("Button_Locked"), TEXT("Button_Up"), TEXT("Button_Down"), TEXT("stop"), TEXT("drop"),
		TEXT("arrowright"), TEXT("arrowleft"), TEXT("reeltoreel"), TEXT("key"), TEXT("clipboard"),
		TEXT("printedpapers"), TEXT("spotlight"), TEXT("phonograph"), TEXT("phonograph"),
		TEXT("cashregister"), TEXT("giovanbook"), TEXT("payphone"), TEXT("webcam"),
		TEXT("button_penthouse"), TEXT("bustopmap"), TEXT("sewermap"), TEXT("deadbody"),
		TEXT("electroniclock"), TEXT("electroniclocked"), TEXT("breakable"), TEXT("breakable"),
		TEXT("bustopmap"), TEXT("sewerlines"), TEXT("door_playerwanted"), TEXT("door_transition"),
		TEXT("sewer_transition"), TEXT("pedestal"), TEXT("dance_male"), TEXT("dance_female"),
		TEXT("switch"), TEXT("valve"), TEXT("malkchaos"), TEXT("malkkey"), TEXT("malkmind"),
		TEXT("malkorder"), TEXT("malksight"), TEXT("malktime"), TEXT("push"),
	};
	if (N == 0) { return TEXT("(none)"); }
	if (N < 1 || N > UE_ARRAY_COUNT(Names)) { return TEXT("?"); }
	return Names[N - 1];
}
