#include "ElysiumCommands.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumWorldServices.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumCmd, Log, All);

const TCHAR* LexToString(EElysiumCmdKind Kind)
{
	switch (Kind)
	{
	case EElysiumCmdKind::Once:       return TEXT("once");
	case EElysiumCmdKind::ButtonPair: return TEXT("pair");
	}
	return TEXT("?");
}

const TCHAR* LexToString(EElysiumCmdGroup Group)
{
	switch (Group)
	{
	case EElysiumCmdGroup::Movement:  return TEXT("movement");
	case EElysiumCmdGroup::Combat:    return TEXT("combat");
	case EElysiumCmdGroup::Camera:    return TEXT("camera");
	case EElysiumCmdGroup::Interface: return TEXT("interface");
	case EElysiumCmdGroup::System:    return TEXT("system");
	case EElysiumCmdGroup::Cheat:     return TEXT("cheat");
	}
	return TEXT("?");
}

FName ElysiumCommands::Canonical(const FString& Word)
{
	return FName(*Word.ToLower());
}

bool ElysiumCommands::TeleportPlayer(FElysiumEntityWorld& World, const FString& Args)
{
	FElysiumPlayer* Player = World.FindPlayer();
	if (!Player || !World.Embodiment())
	{
		UE_LOG(LogElysiumCmd, Warning, TEXT("teleport_player: no world"));
		return false;
	}

	TArray<FString> Tokens;
	Args.ParseIntoArrayWS(Tokens);
	if (Tokens.Num() == 3)
	{
		const FVector Destination(
			FCString::Atod(*Tokens[0]), FCString::Atod(*Tokens[1]), FCString::Atod(*Tokens[2]));
		Player->SetRuntimeTransform(Destination, Player->Angles);
		UE_LOG(LogElysiumCmd, Display, TEXT("teleport_player -> %s"), *Destination.ToString());
		return true;
	}
	if (Tokens.Num() == 6)
	{
		const FVector Destination(
			FCString::Atod(*Tokens[0]), FCString::Atod(*Tokens[1]), FCString::Atod(*Tokens[2]));
		const FVector SourceAngles(
			FCString::Atod(*Tokens[3]), FCString::Atod(*Tokens[4]), FCString::Atod(*Tokens[5]));
		Player->SetRuntimeTransform(Destination, SourceAngles);
		UE_LOG(LogElysiumCmd, Display, TEXT("teleport_player -> %s angles %s"),
			*Destination.ToString(), *SourceAngles.ToString());
		return true;
	}
	if (Tokens.Num() != 1)
	{
		UE_LOG(LogElysiumCmd, Warning,
			TEXT("teleport_player <targetname> | <x> <y> <z> [<pitch> <yaw> <roll>]"));
		return false;
	}

	const FElysiumEntity* Destination = World.FindByName(Tokens[0]);
	if (!Destination)
	{
		// The image's own message, verbatim.
		UE_LOG(LogElysiumCmd, Warning, TEXT("Could not find entity named %s"), *Tokens[0]);
		return false;
	}

	Player->SetRuntimeTransform(Destination->Origin, Destination->Angles);
	UE_LOG(LogElysiumCmd, Display, TEXT("teleport_player -> %s at %s"),
		*Tokens[0], *Destination->Origin.ToString());
	return true;
}

// =====================================================================================
// The inventory
// =====================================================================================

namespace
{
	using EK = EElysiumCmdKind;
	using EG = EElysiumCmdGroup;
	using EB = EElysiumButton;

	struct FRow
	{
		const TCHAR* Name;
		EK Kind;
		EG Group;
		EB Button;      // EElysiumButton::None for a verb the user command does not carry
		const TCHAR* Help;
	};

	// `docs/vtmb/controls.md` § "What is bindable" in declaration order. A verb with no implementation names
	// the roadmap task that owns it, so `elysium.commands` reads as a work list.
	const FRow GInventory[] = {
		// --- Movement ---------------------------------------------------------------------
		{ TEXT("forward"),      EK::ButtonPair, EG::Movement, EB::Forward,      TEXT("move forward") },
		{ TEXT("back"),         EK::ButtonPair, EG::Movement, EB::Back,         TEXT("move back") },
		{ TEXT("moveleft"),     EK::ButtonPair, EG::Movement, EB::MoveLeft,     TEXT("strafe left") },
		{ TEXT("moveright"),    EK::ButtonPair, EG::Movement, EB::MoveRight,    TEXT("strafe right") },
		{ TEXT("moveup"),       EK::ButtonPair, EG::Movement, EB::MoveUp,       TEXT("swim/ladder up; noclip ascend") },
		{ TEXT("movedown"),     EK::ButtonPair, EG::Movement, EB::MoveDown,     TEXT("swim/ladder down; noclip descend") },
		{ TEXT("left"),         EK::ButtonPair, EG::Movement, EB::Left,         TEXT("turn left (cl_yawspeed)") },
		{ TEXT("right"),        EK::ButtonPair, EG::Movement, EB::Right,        TEXT("turn right (cl_yawspeed)") },
		{ TEXT("lookup"),       EK::ButtonPair, EG::Movement, EB::LookUp,       TEXT("keyboard pitch up (cl_pitchspeed)") },
		{ TEXT("lookdown"),     EK::ButtonPair, EG::Movement, EB::LookDown,     TEXT("keyboard pitch down (cl_pitchspeed)") },
		{ TEXT("speed"),        EK::ButtonPair, EG::Movement, EB::Speed,        TEXT("walk modifier -- held selects the slow gait") },
		{ TEXT("strafe"),       EK::ButtonPair, EG::Movement, EB::Strafe,       TEXT("strafe modifier -- the turn keys strafe while held") },
		// `duck` stays an ordinary pair carrying an ordinary button. **The crouch's retention is not
		// here.** A command is one frame of requested input (`docs/architecture/animation-architecture.md`
		// § 3), so `IN_DUCK` means "the key is down this frame" and nothing more; the mover's press
		// edge is what toggles, and the retained state it toggles is `bDuckRequested` beside
		// `m_bDucked`'s own. Faking a held button here would put a stance in the command stream and
		// make a recording claim a key was down on frames it was not.
		{ TEXT("duck"),         EK::ButtonPair, EG::Movement, EB::Duck,         TEXT("crouch -- 36u hull, eye at 30u") },
		{ TEXT("jump"),         EK::ButtonPair, EG::Movement, EB::Jump,         TEXT("jump") },
		{ TEXT("klook"),        EK::ButtonPair, EG::Movement, EB::KLook,        TEXT("keyboard look mode") },
		{ TEXT("mlook"),        EK::ButtonPair, EG::Movement, EB::MLook,        TEXT("mouse look mode -- on from a cold start (default.cfg ends with +mlook)") },
		{ TEXT("jlook"),        EK::ButtonPair, EG::Movement, EB::JLook,        TEXT("joystick look mode") },
		{ TEXT("centerview"),   EK::Once,       EG::Movement, EB::None,         TEXT("recentre pitch") },
		{ TEXT("force_centerview"), EK::Once,   EG::Movement, EB::None,         TEXT("recentre pitch, unconditional") },
		{ TEXT("impulse"),      EK::Once,       EG::Movement, EB::None,         TEXT("classic impulse channel -- no consumer") },

		// --- Combat and items -------------------------------------------------------------
		{ TEXT("attack"),       EK::ButtonPair, EG::Combat,   EB::Attack,       TEXT("primary fire; dismisses an open sign panel") },
		{ TEXT("attack2"),      EK::ButtonPair, EG::Combat,   EB::Attack2,      TEXT("secondary fire -- 4.9") },
		{ TEXT("wpn_secondaryatk"), EK::ButtonPair, EG::Combat, EB::SecondaryAtk, TEXT("secondary attack mode -- 4.9") },
		{ TEXT("reload"),       EK::ButtonPair, EG::Combat,   EB::Reload,       TEXT("reload -- 4.9") },
		{ TEXT("use"),          EK::ButtonPair, EG::Combat,   EB::Use,          TEXT("world interaction -- captured after camera focus settles") },
		{ TEXT("feed"),         EK::ButtonPair, EG::Combat,   EB::Feed,         TEXT("feeding -- the press acquires and attempts a victim; the release only clears the continuation latch") },
		{ TEXT("slot1"),        EK::Once,       EG::Combat,   EB::None,         TEXT("disciplines category -- 9.8") },
		{ TEXT("slot2"),        EK::Once,       EG::Combat,   EB::None,         TEXT("melee category -- 9.8") },
		{ TEXT("slot3"),        EK::Once,       EG::Combat,   EB::None,         TEXT("ranged category -- 9.8") },
		{ TEXT("slot4"),        EK::Once,       EG::Combat,   EB::None,         TEXT("thrown category -- 9.8") },
		{ TEXT("slot5"),        EK::Once,       EG::Combat,   EB::None,         TEXT("armor/clothing category -- 9.8") },
		{ TEXT("slot6"),        EK::Once,       EG::Combat,   EB::None,         TEXT("general inventory category -- 9.8") },
		{ TEXT("slot7"),        EK::Once,       EG::Combat,   EB::None,         TEXT("inventory category 7 -- 9.8") },
		{ TEXT("slot8"),        EK::Once,       EG::Combat,   EB::None,         TEXT("inventory category 8 -- 9.8") },
		{ TEXT("invnext"),      EK::Once,       EG::Combat,   EB::None,         TEXT("next inventory selection -- 9.8") },
		{ TEXT("invprev"),      EK::Once,       EG::Combat,   EB::None,         TEXT("previous inventory selection -- 9.8") },
		{ TEXT("lastinv"),      EK::Once,       EG::Combat,   EB::None,         TEXT("last inventory selection -- 9.8") },
		{ TEXT("holster"),      EK::Once,       EG::Combat,   EB::None,         TEXT("holster -- 9.8") },
		{ TEXT("dropitem"),     EK::Once,       EG::Combat,   EB::None,         TEXT("drop the selected item -- 9.8") },
		{ TEXT("inven_drop_curr"), EK::Once,    EG::Combat,   EB::None,         TEXT("drop the current item -- 9.8") },
		{ TEXT("inven_drop"),   EK::Once,       EG::Combat,   EB::None,         TEXT("drop item N -- 9.8") },
		{ TEXT("toggleinven"),  EK::Once,       EG::Combat,   EB::None,         TEXT("toggle the selection UI -- 9.8") },
		{ TEXT("toggleuiside"), EK::Once,       EG::Combat,   EB::None,         TEXT("swap the selection UI side -- 9.8") },
		{ TEXT("showinventory"), EK::Once,      EG::Combat,   EB::None,         TEXT("show inventory page N -- 9.8") },
		{ TEXT("vhotkey"),      EK::Once,       EG::Combat,   EB::None,         TEXT("fire hotkey slot #N -- 9.7") },
		{ TEXT("vhotkey_int"),  EK::Once,       EG::Combat,   EB::None,         TEXT("fire hotkey slot N (integer form) -- 9.7") },
		{ TEXT("showhotkeys"),  EK::Once,       EG::Combat,   EB::None,         TEXT("open the hotkey window -- 9.7") },
		{ TEXT("hidehotkeys"),  EK::Once,       EG::Combat,   EB::None,         TEXT("close the hotkey window -- 9.7") },
		{ TEXT("sethotkeys"),   EK::Once,       EG::Combat,   EB::None,         TEXT("assign hotkeys -- 9.7") },
		{ TEXT("init_hotkeys"), EK::Once,       EG::Combat,   EB::None,         TEXT("rebuild the hotkey window -- 9.7") },
		{ TEXT("vdiscipline"),  EK::Once,       EG::Combat,   EB::None,         TEXT("open the discipline selector -- 9.7") },
		{ TEXT("vdiscipline_last"), EK::Once,   EG::Combat,   EB::None,         TEXT("cast the last discipline -- 9.7") },
		{ TEXT("vdiscipline_endall"), EK::Once, EG::Combat,   EB::None,         TEXT("end every active discipline -- 9.7") },
		{ TEXT("vdiscipline_int"), EK::Once,    EG::Combat,   EB::None,         TEXT("cast discipline <name> -- 9.7") },

		// --- Camera -----------------------------------------------------------------------
		{ TEXT("togglecamera"), EK::Once,       EG::Camera,   EB::None,         TEXT("flip first/third person") },
		{ TEXT("thirdperson"),  EK::Once,       EG::Camera,   EB::None,         TEXT("go third person") },
		{ TEXT("firstperson"),  EK::Once,       EG::Camera,   EB::None,         TEXT("go first person") },
		{ TEXT("camortho"),     EK::Once,       EG::Camera,   EB::None,         TEXT("orthographic camera -- no such path in the recovered client") },
		{ TEXT("snapto"),       EK::Once,       EG::Camera,   EB::None,         TEXT("return the orbit to its cvars and re-seed") },
		{ TEXT("cam_command"),  EK::Once,       EG::Camera,   EB::None,         TEXT("one-shot mode request (1 third, 2 first)") },
		{ TEXT("camin"),        EK::ButtonPair, EG::Camera,   EB::CamIn,        TEXT("dolly in") },
		{ TEXT("camout"),       EK::ButtonPair, EG::Camera,   EB::CamOut,       TEXT("dolly out") },
		{ TEXT("campitchup"),   EK::ButtonPair, EG::Camera,   EB::CamPitchUp,   TEXT("orbit up") },
		{ TEXT("campitchdown"), EK::ButtonPair, EG::Camera,   EB::CamPitchDown, TEXT("orbit down") },
		{ TEXT("camyawleft"),   EK::ButtonPair, EG::Camera,   EB::CamYawLeft,   TEXT("orbit left") },
		{ TEXT("camyawright"),  EK::ButtonPair, EG::Camera,   EB::CamYawRight,  TEXT("orbit right") },
		{ TEXT("cammousemove"), EK::ButtonPair, EG::Camera,   EB::CamMouseMove, TEXT("mouse-driven orbit -- 10.6") },
		{ TEXT("camdistance"),  EK::ButtonPair, EG::Camera,   EB::CamDistance,  TEXT("mouse-driven dolly -- 10.6") },
		{ TEXT("commandermousemove"), EK::ButtonPair, EG::Camera, EB::CommanderMouseMove, TEXT("commander-mode mouse camera -- 10.6") },

		// --- Interface --------------------------------------------------------------------
		{ TEXT("chareditor"),   EK::ButtonPair, EG::Interface, EB::CharEditor,  TEXT("character sheet -- 9.4") },
		{ TEXT("togglechareditor"), EK::Once,   EG::Interface, EB::None,        TEXT("toggle the character sheet -- 9.4") },
		{ TEXT("questlog"),     EK::ButtonPair, EG::Interface, EB::QuestLog,    TEXT("quest log -- 9.6") },
		// The genesis map's `newplayer` trigger reaches this through `ccmd.createplayer`. Deliberately
		// NOT in `docs/vtmb/controls.md`'s bindable inventory, so it gets no default bind: it is a verb the
		// content calls, not one the player presses.
		{ TEXT("createplayer"), EK::Once,       EG::Interface, EB::None,        TEXT("open character creation") },
		{ TEXT("dlghist"),      EK::Once,       EG::Interface, EB::None,        TEXT("dialogue history -- 9.2") },
		{ TEXT("dlgscrlup"),    EK::Once,       EG::Interface, EB::None,        TEXT("scroll dialogue history up -- 9.2") },
		{ TEXT("dlgscrldn"),    EK::Once,       EG::Interface, EB::None,        TEXT("scroll dialogue history down -- 9.2") },
		{ TEXT("cancelselect"), EK::Once,       EG::Interface, EB::None,        TEXT("the Escape verb -- close the top screen, else pause") },
		{ TEXT("togglemainmenu"), EK::Once,     EG::Interface, EB::None,        TEXT("open/close the menu") },

		// --- System -----------------------------------------------------------------------
		{ TEXT("pause"),        EK::Once,       EG::System,   EB::None,         TEXT("hold the world") },
		{ TEXT("save"),         EK::Once,       EG::System,   EB::None,         TEXT("save <slot|quick> -- 11.9") },
		{ TEXT("load"),         EK::Once,       EG::System,   EB::None,         TEXT("load <slot|quick> -- 11.9") },
		{ TEXT("snapshot"),     EK::Once,       EG::System,   EB::None,         TEXT("screenshot to Saved/Screenshots") },
		{ TEXT("toggleconsole"), EK::Once,      EG::System,   EB::None,         TEXT("the console key -- the engine's viewport client owns it (plane 0)") },

		// --- The vampire.dll verbs the patch's aliases and the debug binds call -------------
		{ TEXT("noclip"),       EK::Once,       EG::Cheat,    EB::None,         TEXT("fly through geometry") },
		{ TEXT("god"),          EK::Once,       EG::Cheat,    EB::None,         TEXT("the player's unkillable latch") },
		{ TEXT("vstats"),       EK::Once,       EG::Cheat,    EB::None,         TEXT("dump the player sheet -- 9.4") },
		{ TEXT("vdmg"),         EK::Once,       EG::Cheat,    EB::None,         TEXT("apply damage -- 4.9") },
		{ TEXT("giftxp"),       EK::Once,       EG::Cheat,    EB::None,         TEXT("grant XP -- 9.4") },
		{ TEXT("faith"),        EK::Once,       EG::Cheat,    EB::None,         TEXT("set humanity -- 9.4") },
		{ TEXT("blood"),        EK::Once,       EG::Cheat,    EB::None,         TEXT("set blood pool -- 9.4") },
		{ TEXT("skill"),        EK::Once,       EG::Cheat,    EB::None,         TEXT("set an attribute/ability -- 9.4") },
		{ TEXT("player_sequence"), EK::Once,    EG::Cheat,    EB::None,         TEXT("play an animation on the player -- 12.x") },
		// The chargen wizard's own exit: on close it runs `teleport_player firetrans`, which is what
		// carries the run out of genesis (`docs/vtmb/level_transitions.md`). Content calls it, so no default bind.
		{ TEXT("teleport_player"), EK::Once,    EG::Cheat,    EB::None,         TEXT("teleport the player to a named entity, or to an X Y Z") },
		{ TEXT("infobar_message"), EK::Once,    EG::Cheat,    EB::None,         TEXT("post an info-bar message -- 9.2") },
	};
}

void ElysiumCommands::DeclareVtmbInventory(FElysiumCommands& Registry)
{
	for (const FRow& Row : GInventory)
	{
		FElysiumCommandDef Def;
		Def.Name   = FName(Row.Name);
		Def.Kind   = Row.Kind;
		Def.Group  = Row.Group;
		Def.Button = static_cast<uint64>(Row.Button);
		Def.Help   = Row.Help;
		Registry.Declare(Def);
	}
}

// =====================================================================================
// FElysiumCommands
// =====================================================================================

FElysiumCommands& FElysiumCommands::Get()
{
	static FElysiumCommands Registry;
	static bool bSeeded = false;
	if (!bSeeded)
	{
		// Set before declaring: Declare() does not re-enter, but a future declaration that logs
		// through anything reaching Get() would, and this is the cheapest guard against it.
		bSeeded = true;
		ElysiumCommands::DeclareVtmbInventory(Registry);
	}
	return Registry;
}

void FElysiumCommands::Declare(const FElysiumCommandDef& Def)
{
	const FName Key = ElysiumCommands::Canonical(Def.Name.ToString());
	if (Index.Contains(Key))
	{
		return;
	}
	FElysiumCommandDef Stored = Def;
	Stored.Name = Key;
	Index.Add(Key, Entries.Num());
	FEntry& Entry = Entries.AddDefaulted_GetRef();
	Entry.Def = Stored;
	Defs.Add(Stored);
}

FElysiumCommands::FEntry* FElysiumCommands::FindEntry(FName Name)
{
	const int32* Slot = Index.Find(ElysiumCommands::Canonical(Name.ToString()));
	return Slot ? &Entries[*Slot] : nullptr;
}

const FElysiumCommands::FEntry* FElysiumCommands::FindEntry(FName Name) const
{
	const int32* Slot = Index.Find(ElysiumCommands::Canonical(Name.ToString()));
	return Slot ? &Entries[*Slot] : nullptr;
}

const FElysiumCommandDef* FElysiumCommands::Find(FName Name) const
{
	const FEntry* Entry = FindEntry(Name);
	return Entry ? &Entry->Def : nullptr;
}

FElysiumCommandBinding FElysiumCommands::Bind(FName Name, FElysiumCommandHandler Handler)
{
	FEntry* Entry = FindEntry(Name);
	if (!Entry)
	{
		UE_LOG(LogElysiumCmd, Warning,
			TEXT("Bind('%s') refused: not a declared verb. The inventory is the whitelist -- add it to "
			     "ElysiumCommands.cpp's table (and to controls.md) rather than binding behind it."),
			*Name.ToString());
		return FElysiumCommandBinding();
	}
	FElysiumCommandBinding Binding;
	Binding.Id = NextBindingId++;
	Binding.Name = Entry->Def.Name;
	Entry->Impls.Add({ Binding.Id, MoveTemp(Handler) });
	return Binding;
}

bool FElysiumCommands::Unbind(FElysiumCommandBinding& Binding)
{
	if (!Binding.IsValid())
	{
		return false;
	}
	bool bRemoved = false;
	if (FEntry* Entry = FindEntry(Binding.Name))
	{
		const int32 Id = Binding.Id;
		bRemoved = Entry->Impls.RemoveAll([Id](const FImpl& Impl) { return Impl.Id == Id; }) > 0;
	}
	Binding.Reset();
	return bRemoved;
}

bool FElysiumCommands::IsBound(FName Name) const
{
	const FEntry* Entry = FindEntry(Name);
	return Entry && Entry->Impls.Num() > 0;
}

bool FElysiumCommands::Resolve(const FString& Word, FName& OutName, bool& bOutPressed) const
{
	if (Word.IsEmpty())
	{
		return false;
	}

	// A leading +/- names the edge of a pair, and only of a pair: `-speed` is a release, while a
	// hypothetical `+togglecamera` is simply not a verb, which is also what VtMB's console reports.
	const TCHAR Lead = Word[0];
	if (Lead == TEXT('+') || Lead == TEXT('-'))
	{
		const FName Bare = ElysiumCommands::Canonical(Word.Mid(1));
		const FElysiumCommandDef* Def = Find(Bare);
		if (Def && Def->Kind == EElysiumCmdKind::ButtonPair)
		{
			OutName = Bare;
			bOutPressed = (Lead == TEXT('+'));
			return true;
		}
		return false;
	}

	const FName Bare = ElysiumCommands::Canonical(Word);
	if (const FElysiumCommandDef* Def = Find(Bare))
	{
		OutName = Bare;
		// A pair invoked without a sign is a press, which is how a `.dlg` action or a script spells
		// a momentary verb it has no release for.
		bOutPressed = true;
		return true;
	}
	return false;
}

bool FElysiumCommands::Execute(const FString& Statement)
{
	FString S = Statement;
	S.TrimStartAndEndInline();
	if (S.IsEmpty())
	{
		return false;
	}

	int32 Split = 0;
	while (Split < S.Len() && !FChar::IsWhitespace(S[Split]))
	{
		++Split;
	}
	const FString Word = S.Left(Split);

	FName Name;
	bool bPressed = true;
	if (!Resolve(Word, Name, bPressed))
	{
		return false;
	}

	FString Args = S.Mid(Split);
	Args.TrimStartAndEndInline();
	Args = Args.TrimQuotes();
	return Invoke(Name, bPressed, Args);
}

bool FElysiumCommands::Invoke(FName Name, bool bPressed, const FString& Args)
{
	FEntry* Entry = FindEntry(Name);
	if (!Entry)
	{
		return false;
	}
	++Entry->Calls;

	// The latch is the verb's, not its implementation's: `+forward` fills the user command whether
	// or not anything has bound a handler to it, and a headless world with no sink just drops it.
	if (Entry->Def.Kind == EElysiumCmdKind::ButtonPair && Entry->Def.Button != 0)
	{
		if (FElysiumUserCmdBuilder* Sink = UserCmdSink)
		{
			Sink->SetButtonBits(Entry->Def.Button, bPressed);
		}
	}

	if (Entry->Impls.Num() > 0)
	{
		FElysiumCommandCall Call;
		Call.Name = Entry->Def.Name;
		Call.Args = Args;
		Call.bPressed = bPressed;
		Call.Kind = Entry->Def.Kind;
		// Copied, not referenced: a handler is allowed to unbind itself (a screen closing on its own
		// verb), which would otherwise reallocate the array out from under the call.
		FElysiumCommandHandler Handler = Entry->Impls.Last().Handler;
		Handler(Call);
		return true;
	}

	// Unimplemented but declared. Verbose, not Warning: this is most of the inventory today, and the
	// coverage report (`elysium.commands`) is where it is meant to be read, not the log.
	UE_LOG(LogElysiumCmd, Verbose, TEXT("'%s%s%s' has no implementation (%s)"),
		Entry->Def.Kind == EElysiumCmdKind::ButtonPair ? (bPressed ? TEXT("+") : TEXT("-")) : TEXT(""),
		*Entry->Def.Name.ToString(),
		Args.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" %s"), *Args),
		Entry->Def.Help ? Entry->Def.Help : TEXT("no owner recorded"));
	return true;
}

int32 FElysiumCommands::CallCount(FName Name) const
{
	const FEntry* Entry = FindEntry(Name);
	return Entry ? Entry->Calls : 0;
}

FString FElysiumCommands::Describe(const FString& Filter) const
{
	FString Out;
	int32 Shown = 0;
	int32 Implemented = 0;
	for (const FEntry& Entry : Entries)
	{
		const FString Name = Entry.Def.Name.ToString();
		if (Entry.Impls.Num() > 0)
		{
			++Implemented;
		}
		if (!Filter.IsEmpty() && !Name.Contains(Filter) &&
			!FString(LexToString(Entry.Def.Group)).Contains(Filter))
		{
			continue;
		}
		++Shown;
		Out += FString::Printf(TEXT("  %-20s %-5s %-10s %-4s calls=%-5d %s\n"),
			*Name, LexToString(Entry.Def.Kind), LexToString(Entry.Def.Group),
			Entry.Impls.Num() > 0 ? TEXT("impl") : TEXT("--"), Entry.Calls,
			Entry.Def.Help ? Entry.Def.Help : TEXT(""));
	}
	return FString::Printf(TEXT("commands: %d declared, %d implemented, %d shown\n%s"),
		Entries.Num(), Implemented, Shown, *Out);
}

void FElysiumCommands::ResetImplementations()
{
	for (FEntry& Entry : Entries)
	{
		Entry.Impls.Reset();
		Entry.Calls = 0;
	}
	UserCmdSink = nullptr;
}
