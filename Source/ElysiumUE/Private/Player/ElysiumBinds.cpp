#include "ElysiumBinds.h"

const TArray<FElysiumDefaultBind>& ElysiumBinds::Defaults()
{
	static const TArray<FElysiumDefaultBind> Table = {
		// Movement.
		{ EKeys::W,            TEXT("+forward"),          TEXT("w") },
		{ EKeys::S,            TEXT("+back"),             TEXT("s") },
		{ EKeys::A,            TEXT("+moveleft"),         TEXT("a") },
		{ EKeys::D,            TEXT("+moveright"),        TEXT("d") },
		{ EKeys::Up,           TEXT("+forward"),          TEXT("UPARROW") },
		{ EKeys::Down,         TEXT("+back"),             TEXT("DOWNARROW") },
		// The patch swaps the arrows and the comma/period pair: arrows strafe, comma/period turn.
		{ EKeys::Left,         TEXT("+moveleft"),         TEXT("LEFTARROW") },
		{ EKeys::Right,        TEXT("+moveright"),        TEXT("RIGHTARROW") },
		{ EKeys::Comma,        TEXT("+left"),             TEXT(",") },
		{ EKeys::Period,       TEXT("+right"),            TEXT(".") },
		{ EKeys::Apostrophe,   TEXT("+moveup"),           TEXT("'") },
		{ EKeys::Slash,        TEXT("+movedown"),         TEXT("/") },
		{ EKeys::B,            TEXT("+moveup"),           TEXT("b") },
		{ EKeys::V,            TEXT("+movedown"),         TEXT("v") },
		{ EKeys::SpaceBar,     TEXT("+jump"),             TEXT("SPACE") },
		{ EKeys::LeftControl,  TEXT("+duck"),             TEXT("CTRL") },
		{ EKeys::RightControl, TEXT("+duck"),             TEXT("CTRL") },
		{ EKeys::LeftShift,    TEXT("+speed"),            TEXT("SHIFT") },
		{ EKeys::RightShift,   TEXT("+speed"),            TEXT("SHIFT") },
		{ EKeys::End,          TEXT("+strafe"),           TEXT("END") },
		{ EKeys::U,            TEXT("+lookup"),           TEXT("u") },
		{ EKeys::J,            TEXT("+lookdown"),         TEXT("j") },
		{ EKeys::Semicolon,    TEXT("+mlook"),            TEXT(";") },

		// Combat, items, disciplines.
		{ EKeys::LeftMouseButton,  TEXT("+attack"),       TEXT("MOUSE1") },
		{ EKeys::Enter,            TEXT("+attack"),       TEXT("ENTER") },
		{ EKeys::RightMouseButton, TEXT("vm_discipline"), TEXT("MOUSE2") },
		{ EKeys::Tab,          TEXT("+wpn_secondaryatk"), TEXT("TAB") },
		{ EKeys::R,            TEXT("+reload"),           TEXT("r") },
		{ EKeys::E,            TEXT("+use"),              TEXT("e") },
		{ EKeys::F,            TEXT("vm_feed"),           TEXT("f") },
		{ EKeys::G,            TEXT("vm_passives"),       TEXT("g") },
		{ EKeys::H,            TEXT("holster"),           TEXT("h") },
		{ EKeys::BackSpace,    TEXT("dropitem"),          TEXT("BACKSPACE") },
		{ EKeys::I,            TEXT("slot6"),             TEXT("i") },
		{ EKeys::LeftBracket,  TEXT("invnext"),           TEXT("[") },
		{ EKeys::RightBracket, TEXT("invprev"),           TEXT("]") },
		{ EKeys::Backslash,    TEXT("lastinv"),           TEXT("\\") },
		{ EKeys::MouseScrollUp,   TEXT("invprev"),        TEXT("MWHEELUP") },
		{ EKeys::MouseScrollDown, TEXT("invnext"),        TEXT("MWHEELDOWN") },
		{ EKeys::T,            TEXT("toggleuiside"),      TEXT("t") },
		{ EKeys::One,          TEXT("vhotkey #1"),        TEXT("1") },
		{ EKeys::Two,          TEXT("vhotkey #2"),        TEXT("2") },
		{ EKeys::Three,        TEXT("vhotkey #3"),        TEXT("3") },
		{ EKeys::Four,         TEXT("vhotkey #4"),        TEXT("4") },
		{ EKeys::Five,         TEXT("vhotkey #5"),        TEXT("5") },
		{ EKeys::Six,          TEXT("vhotkey #6"),        TEXT("6") },
		{ EKeys::Seven,        TEXT("vhotkey #7"),        TEXT("7") },
		{ EKeys::Eight,        TEXT("vhotkey #8"),        TEXT("8") },
		{ EKeys::Nine,         TEXT("vhotkey #9"),        TEXT("9") },
		{ EKeys::Zero,         TEXT("vhotkey #10"),       TEXT("0") },
		{ EKeys::K,            TEXT("showhotkeys"),       TEXT("k") },
		// Retail's own F1 carries `slot2` (melee), but bare F1 is also Cog's hardcoded shell toggle
		// (`FCogWindow_Settings::Shortcut_ToggleImguiInput`) and that shortcut is not reachable through
		// this table to move — it fires inside Cog's own input handling, ahead of anything bound here.
		// F1 stays exclusively Cog's (`ReservedKeys` below); `slot2` moves onto F6, which retail/the
		// patch leave carrying the dead `slot1` (the commented-out Disciplines category — see
		// `ElysiumInventorySections.h`). Every other F-key keeps its retail/patch category verbatim.
		{ EKeys::F2,           TEXT("slot3"),             TEXT("F2") },
		{ EKeys::F3,           TEXT("slot5"),             TEXT("F3") },
		{ EKeys::F4,           TEXT("slot6"),             TEXT("F4") },
		{ EKeys::F5,           TEXT("slot4"),             TEXT("F5") },
		{ EKeys::F6,           TEXT("slot2"),             TEXT("F6") },
		{ EKeys::F8,           TEXT("vdiscipline_endall"),TEXT("F8") },

		// UI, camera, system.
		{ EKeys::Escape,       TEXT("cancelselect"),      TEXT("ESCAPE") },
		{ EKeys::C,            TEXT("+chareditor"),       TEXT("c") },
		{ EKeys::L,            TEXT("+questlog"),         TEXT("l") },
		{ EKeys::Home,         TEXT("dlghist"),           TEXT("HOME") },
		{ EKeys::PageUp,       TEXT("dlgscrlup"),         TEXT("PGUP") },
		{ EKeys::PageDown,     TEXT("dlgscrldn"),         TEXT("PGDN") },
		{ EKeys::Z,            TEXT("togglecamera"),      TEXT("z") },
		{ EKeys::F9,           TEXT("save quick"),        TEXT("F9") },
		{ EKeys::F12,          TEXT("load quick"),        TEXT("F12") },
		{ EKeys::F10,          TEXT("snapshot"),          TEXT("F10") },
		{ EKeys::P,            TEXT("skip"),              TEXT("p") },
		{ EKeys::Pause,        TEXT("pause"),             TEXT("PAUSE") },
		{ EKeys::NumPadFive,   TEXT("cam_restore"),       TEXT("KP_5") },
		{ EKeys::NumPadEight,  TEXT("+camin"),            TEXT("KP_UPARROW") },
		{ EKeys::NumPadTwo,    TEXT("+camout"),           TEXT("KP_DOWNARROW") },
		{ EKeys::NumPadFour,   TEXT("cam_rotateleft"),    TEXT("KP_LEFTARROW") },
		{ EKeys::NumPadSix,    TEXT("cam_rotateright"),   TEXT("KP_RIGHTARROW") },
	};
	return Table;
}

const TArray<FKey>& ElysiumBinds::ReservedKeys()
{
	static const TArray<FKey> Keys = {
		// The console. VtMB's own `toggleconsole` bind and the engine's `ConsoleKeys`, which is why
		// there is nothing bound to it in the table above either.
		EKeys::Tilde,
		// The console's second key, for layouts that put no `` ` `` left of `1` (ABNT2 puts `'` there,
		// which UE resolves to `EKeys::Apostrophe` — a bind, not the console). F7 is the one function
		// key neither this table nor VtMB's own `default.cfg` claims.
		EKeys::F7,
		// Cog's shell toggle (`FCogWindow_Settings::Shortcut_ToggleImguiInput`, bare F1, no modifier).
		// Cog reads it inside its own input handling, ahead of this table, so a default bind landing
		// here would fire both the toggle and the game verb on the same press. `slot2` moved to F6.
		EKeys::F1,
	};
	return Keys;
}

bool ElysiumBinds::IsReserved(const FKey& Key)
{
	return ReservedKeys().Contains(Key);
}
