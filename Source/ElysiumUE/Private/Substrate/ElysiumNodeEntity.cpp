#include "Substrate/ElysiumNodeEntity.h"

#include "ElysiumEntityDefs.h"

namespace ElysiumNodeEntity
{
	const TCHAR* const HintClassname = TEXT("ai_hint");

	namespace
	{
		// Every `info_node*` string in vampire.dll's entity-factory table (`0x1052fd0f` ..
		// `0x10530002`) except `info_node_link`, which the factory binds to `CAI_DynamicLink`, plus
		// `info_hint` — the classnames `CNodeEnt` is created for.
		const TCHAR* const GNodeClassnames[] =
		{
			TEXT("info_hint"),
			TEXT("info_node"),
			TEXT("info_node_air"),
			TEXT("info_node_air_hint"),
			TEXT("info_node_bach_run_1"),
			TEXT("info_node_bach_run_2"),
			TEXT("info_node_bach_teleport_1"),
			TEXT("info_node_bach_teleport_2"),
			TEXT("info_node_bach_teleport_3"),
			TEXT("info_node_bach_teleport_4"),
			TEXT("info_node_chang_column"),
			TEXT("info_node_chang_jumpbase"),
			TEXT("info_node_chang_ledge"),
			TEXT("info_node_chang_teleport"),
			TEXT("info_node_climb"),
			TEXT("info_node_cover_corner"),
			TEXT("info_node_cover_low"),
			TEXT("info_node_cover_med"),
			TEXT("info_node_crosswalk"),
			TEXT("info_node_hint"),
			TEXT("info_node_kick_at"),
			TEXT("info_node_kick_over"),
			TEXT("info_node_manbat_fly_to_point"),
			TEXT("info_node_patrol_point"),
			TEXT("info_node_sabbat_arch"),
			TEXT("info_node_sabbat_bottom"),
			TEXT("info_node_sabbat_dive"),
			TEXT("info_node_sabbat_hide"),
			TEXT("info_node_sabbat_nojump"),
			TEXT("info_node_sabbat_top"),
			TEXT("info_node_shoot_at"),
			TEXT("info_node_tzimisce"),
			TEXT("info_node_tzimisce_claw_left"),
			TEXT("info_node_tzimisce_claw_right"),
			TEXT("info_node_werewolf"),
			TEXT("info_node_werewolf_hint"),
		};

		// `FUN_102d7d30`'s class → forced hint type rows, in the body's own order. `info_node` and
		// `info_node_werewolf` force 0; `info_node_werewolf_hint` is special-cased below.
		struct FForcedType
		{
			const TCHAR* Classname;
			int32 HintType;
		};
		const FForcedType GForcedTypes[] =
		{
			{ TEXT("info_node"), 0 },
			{ TEXT("info_node_cover_med"), 100 },
			{ TEXT("info_node_cover_low"), 0x65 },
			{ TEXT("info_node_cover_corner"), 0x27d8 },
			{ TEXT("info_node_crosswalk"), 11000 },
			{ TEXT("info_node_tzimisce_claw_left"), 14000 },
			{ TEXT("info_node_tzimisce_claw_right"), 0x36b1 },
			{ TEXT("info_node_kick_over"), 0x283c },
			{ TEXT("info_node_kick_at"), 0x283d },
			{ TEXT("info_node_shoot_at"), 0x28a0 },
			{ TEXT("info_node_werewolf"), 0 },
			{ TEXT("info_node_sabbat_bottom"), 16000 },
			{ TEXT("info_node_sabbat_top"), 0x3e81 },
			{ TEXT("info_node_sabbat_arch"), 0x3e82 },
			{ TEXT("info_node_sabbat_hide"), 0x3e83 },
			{ TEXT("info_node_sabbat_nojump"), 0x3e84 },
			{ TEXT("info_node_sabbat_dive"), 0x3e85 },
			{ TEXT("info_node_bach_teleport_1"), 17000 },
			{ TEXT("info_node_bach_teleport_2"), 0x4269 },
			{ TEXT("info_node_bach_teleport_3"), 0x426a },
			{ TEXT("info_node_bach_teleport_4"), 0x426b },
			{ TEXT("info_node_bach_run_1"), 0x426c },
			{ TEXT("info_node_bach_run_2"), 0x426d },
			{ TEXT("info_node_chang_jumpbase"), 18000 },
			{ TEXT("info_node_chang_column"), 0x4651 },
			{ TEXT("info_node_chang_teleport"), 0x4652 },
			{ TEXT("info_node_chang_ledge"), 0x4653 },
			{ TEXT("info_node_manbat_fly_to_point"), 20000 },
		};

		// `CNodeEnt::Spawn`'s standalone set: a hint with network id -1, made only for a non-zero type.
		const TCHAR* const GStandaloneClassnames[] =
		{
			TEXT("info_hint"),
			TEXT("info_node_kick_over"),
			TEXT("info_node_kick_at"),
			TEXT("info_node_shoot_at"),
		};

		// `m_eHintType` (`+0x450`) is an int field the keyvalue parser fills with `atoi`, and both
		// `FUN_102d7d30` and `CNodeEnt::Spawn` read it through a `short`.
		int32 AuthoredHintType(const TMap<FString, FString>& Keys)
		{
			const FString* Raw = Keys.Find(TEXT("hinttype"));
			return Raw != nullptr ? FCString::Atoi(**Raw) : 0;
		}

		// `m_strGroup` (`+0x458`) is a pooled string; an empty keyvalue pools to null.
		bool AuthoredGroup(const TMap<FString, FString>& Keys)
		{
			const FString* Raw = Keys.Find(TEXT("Group"));
			return Raw != nullptr && !Raw->IsEmpty();
		}
	}

	bool IsNodeClassname(const FString& Classname)
	{
		for (const TCHAR* Name : GNodeClassnames)
		{
			if (Classname.Equals(Name, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	int32 ClassHintType(const FString& Classname, int32 AuthoredHintType, bool& bForced)
	{
		bForced = true;
		if (Classname.Equals(TEXT("info_node_werewolf_hint"), ESearchCase::IgnoreCase))
		{
			const int16 Short = static_cast<int16>(AuthoredHintType);
			return (Short < 15000 || Short > 0x3aaa) ? 0 : Short;
		}
		for (const FForcedType& Row : GForcedTypes)
		{
			if (Classname.Equals(Row.Classname, ESearchCase::IgnoreCase))
			{
				return Row.HintType;
			}
		}
		bForced = false;
		return AuthoredHintType;
	}

	bool MakesHint(const FString& Classname, const TMap<FString, FString>& Keys)
	{
		if (!IsNodeClassname(Classname))
		{
			return false;
		}
		// `CNodeEnt::Spawn`'s first rewrite: `info_node_tzimisce` spawns as `info_node`.
		const FString Effective = Classname.Equals(TEXT("info_node_tzimisce"), ESearchCase::IgnoreCase)
			? FString(TEXT("info_node")) : Classname;
		bool bForced = false;
		const int16 Type = static_cast<int16>(ClassHintType(Effective, AuthoredHintType(Keys), bForced));
		for (const TCHAR* Standalone : GStandaloneClassnames)
		{
			if (Effective.Equals(Standalone, ESearchCase::IgnoreCase))
			{
				// Otherwise `DevMsg("WARNING: Hint node with no hint type")` and no hint.
				return Type != 0;
			}
		}
		return Type != 0 || AuthoredGroup(Keys);
	}

	bool ApplyHintReplacement(FElysiumEntityDef& Def)
	{
		if (!MakesHint(Def.Classname, Def.Keys))
		{
			return false;
		}
		// The hint parses the SAME raw block (`CNodeEnt::ParseMapData` `0x102d7890` stashes it), so
		// its keys — and the authored `hinttype`, not the class-forced one — pass through unchanged.
		Def.SourceClassname = Def.Classname;
		Def.Classname = HintClassname;
		return true;
	}
}
