// `CBaseEntity`'s keyvalue surface (L0.entity_core.datamap-keyvalues, `walks/L0-r017.md`): slot 107
// `ParseMapData` 0x1009e280, slot 110 `KeyValue(char*, char*)` 0x1009e430 with its `angle` rewrite,
// the datamap walker `FUN_101a5a80` 0x101a5a80, slot 121 `ReadKeyField` 0x100acab0 with the variant
// marshal `FUN_100d0390` 0x100d0390, slot 82 `GetDataDescMap` 0x100a2290, and the output-action
// parser the walker's custom branch reaches (`CEventsSaveDataOps::vfunc4` 0x100cdb20 ->
// `FUN_100cd6d0` 0x100cd6d0 -> `FUN_100ccf90` 0x100ccf90 over the splitter `FUN_101d16c0`). The
// three number parsers (`FUN_101d0310`, `FUN_101d0570`, `FUN_101d0630`) are inline in
// `ElysiumClassRegistry.h`; the absolute-pose setters the `angles` / `origin` arms call (slots 216 /
// 218) sit beside the pose arithmetic in `ElysiumEntity.cpp`.

#include "ElysiumEntity.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"

#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumKeyValue, Log, All);

namespace
{
	// `ent_debugkeys` (convar object 0x106cf420, name string 0x10555504, default `""`, constructed by
	// `FUN_1009e3c0`): a classname. While it is non-empty, 0x1009e430's datamap walk prints every key
	// it handles, `"(%s) key: %-16s value: %s\n"` (0x10555514), and every key it does not,
	// `"!! (%s) key not handled: \"%s\" \"%s\"\n"` (0x10555534), for an entity whose `m_iClassname`
	// (+0x11c) or whose datamap level's `dataClassName` (+8) matches it (`__strcmpi`). The answers are
	// the plain walk's; the gate reads the convar through `parent->vtbl[1]()` and the
	// `FCVAR_NEVER_AS_STRING` (0x1000) flag, which this port's console variable has no need of.
	TAutoConsoleVariable<FString> GEntDebugKeys(TEXT("ent_debugkeys"), TEXT(""),
		TEXT("VtMB ent_debugkeys (0x106cf420): print every map key CBaseEntity::KeyValue 0x1009e430 walks for this classname"));

	FString KeyValueVec(const FVector& V)
	{
		return FString::Printf(TEXT("%g,%g,%g"), static_cast<float>(V.X), static_cast<float>(V.Y), static_cast<float>(V.Z));
	}

	// A map origin as retail's `m_vecAbsOrigin` spells it: Source inches, the Y reflection undone.
	FString KeyValueSourceVec(const FVector& Cm)
	{
		return FString::Printf(TEXT("%g,%g,%g"), static_cast<float>(Cm.X / ElysiumMove::U),
			static_cast<float>(-Cm.Y / ElysiumMove::U), static_cast<float>(Cm.Z / ElysiumMove::U));
	}

	const TCHAR* KeyValueTypeName(uint8 Type)
	{
		switch (Type)
		{
		case ElysiumRetailFieldType::Float:          return TEXT("FLOAT");
		case ElysiumRetailFieldType::String:         return TEXT("STRING");
		case ElysiumRetailFieldType::Vector:         return TEXT("VECTOR");
		case ElysiumRetailFieldType::Integer:        return TEXT("INTEGER");
		case ElysiumRetailFieldType::Boolean:        return TEXT("BOOLEAN");
		case ElysiumRetailFieldType::Short:          return TEXT("SHORT");
		case ElysiumRetailFieldType::Character:      return TEXT("CHARACTER");
		case ElysiumRetailFieldType::Color32:        return TEXT("COLOR32");
		case ElysiumRetailFieldType::Embedded:       return TEXT("EMBEDDED");
		case ElysiumRetailFieldType::Custom:         return TEXT("CUSTOM");
		case ElysiumRetailFieldType::ClassPtr:       return TEXT("CLASSPTR");
		case ElysiumRetailFieldType::EHandle:        return TEXT("EHANDLE");
		case ElysiumRetailFieldType::PositionVector: return TEXT("POSITION_VECTOR");
		case ElysiumRetailFieldType::Time:           return TEXT("TIME");
		case ElysiumRetailFieldType::ModelName:      return TEXT("MODELNAME");
		case ElysiumRetailFieldType::SoundName:      return TEXT("SOUNDNAME");
		default:                                     return TEXT("VOID");
		}
	}
}

// --- slot 82 ------------------------------------------------------------------------------------

void* FElysiumEntity::GetDataDescMap()
{
	// `CBaseEntity::GetDataDescMap` 0x100a2290: `MOV EAX,0x10552e18; RET` -- `&datamap_CBaseEntity`
	// (110 rows, 40 of them KEY; `baseMap` 0). Every class's override answers its own static
	// `datamap_t` whose `baseMap` (+0xc) chains to its parent's (`CAISound`'s slot 82: `MOV
	// EAX,0x10599f48; RET`, two rows, `baseMap` = 0x10552e18). This port's datamap is the class
	// descriptor the registry built for the entity's classname: its `Fields` are the level's rows and
	// `BaseName` its `baseMap`, so the dispatch answers the leaf's descriptor, and
	// `FElysiumClassRegistry::Find(BaseName)` is the `+0xc` link the two walkers follow.
	return const_cast<FElysiumClassDesc*>(Class);
}

// --- slot 107 -----------------------------------------------------------------------------------

void FElysiumEntity::ParseMapData(void* MapData)
{
	// `CBaseEntity::ParseMapData` 0x1009e280 (209 B). The scope frame `"CBaseEntity::ParseMapData"`
	// (0x105554e4, with `m_iName` / `""` / `"NULL ENTITY"`) writes no game state. Then
	// `ok = GetFirstKey(map, key[256], value[256])` -- 0x10136e80: `cursor := start`, then
	// `CEntityMapData::GetNextKey` 0x10136ee0 -- and `while (ok) { vtbl[0x1B8](key, value); ok =
	// GetNextKey(map, key, value); }` (1009e321 / 1009e336): ONE virtual slot-110 `KeyValue` per pair,
	// in authored order, repeated keys included, the result discarded. `GetNextKey`'s own rules -- the
	// tokenizer `FUN_10136ce0`'s delimiters `{}()'` (0x105794c0) and escapes, the 255-character cap of
	// key and value (`Q_strncpy(.., 0x100)`), the KEY's trailing-space trim (never the value's), `}` as
	// the key ending the entity, `}` as the value warning `"closing brace without data"` (0x105795d8),
	// EOF warning `"EOF without closing brace"` (0x1057961c) -- ran in the exporter: the baked map
	// arrives as parsed pairs (`audit.tsv` 0x10136ce0 `engine_replaced`), in authored order, a repeated
	// key folded to its LAST occurrence, which is the word retail's last write leaves for every arm
	// but the two `|=` ones (`disableshadows 1` then `disableshadows 0` keeps 0x20 in retail and loses
	// it here -- a named representation, authored by no shipped map).
	const FElysiumEntityMapData* Map = static_cast<const FElysiumEntityMapData*>(MapData);
	if (Map == nullptr || Map->Keys == nullptr)
	{
		return;                                                                 // GetFirstKey: no text, no pair
	}
	int32 Index = 0;
	for (const TPair<FString, FString>& Pair : *Map->Keys)
	{
		if (World != nullptr)
		{
			World->EmitRetailSite(*this, TEXT("kv_pair"), TEXT("CBaseEntity::ParseMapData"), 0x1009e280u, TEXT("dispatch"),
				FString::Printf(TEXT("index=%d key=%s value=\"%s\""), Index, *Pair.Key, *Pair.Value));
		}
		(void)KeyValue(*Pair.Key, *Pair.Value);                                 // 1009e321 vtbl[0x1b8], slot 110
		++Index;
	}
}

// --- slot 110 -----------------------------------------------------------------------------------

FString FElysiumEntity::RewriteAngleKey(double AngleValue, const FVector& CurrentLocalAngles)
{
	// 0x1009e430's `angle` arm, 1009e676-1009e746. `v = atof(value)` (0x1043136f, a double in ST0),
	// stored as a float for the format below; the compare `FCOMP float [0x104454c4]` (0.0f) runs on the
	// UNROUNDED double (`FNSTSW; AND EAX,0x100; JNZ`, 1009e6b0-1009e6c0).
	if (AngleValue < 0.0)
	{
		// `n = __ftol(v)` (0x10431320, truncation toward zero): `n == -1` selects `"-90 0 0"`
		// (0x10555578), any other negative `"90 0 0"` (0x10555570); `Q_strncpy(DAT_106cf040, literal,
		// 0x40)`. So `angle -1` and `angle -1.5` are (-90, 0, 0); `angle -2` and `angle -0.5` are (90, 0, 0).
		const int32 N = AngleValue <= static_cast<double>(TNumericLimits<int32>::Min()) ? TNumericLimits<int32>::Min()
			: static_cast<int32>(AngleValue);
		return N == -1 ? FString(TEXT("-90 0 0")) : FString(TEXT("90 0 0"));
	}
	// `Q_snprintf(DAT_106cf040, 0x40, "%f %f %f" (0x10555584), GetAngles()[0], (float) v, GetAngles()[2])`:
	// slot 221 (`+0x374`) twice, `CBaseEntity::GetAngles` 0x100b3110 = `&m_angRotation` (+0x428), the
	// LOCAL angles. Pitch and roll kept, yaw := v, through six-decimal text into a 64-byte buffer.
	return FString::Printf(TEXT("%f %f %f"), static_cast<double>(static_cast<float>(CurrentLocalAngles.X)),
		static_cast<double>(static_cast<float>(AngleValue)), static_cast<double>(static_cast<float>(CurrentLocalAngles.Z))).Left(63);
}

bool FElysiumEntity::KeyValue(const TCHAR* InKey, const TCHAR* InValue)
{
	// `CBaseEntity::KeyValue(char*, char*)` 0x1009e430 (1530 B; the corpus files it as
	// `CAISound::FUN_1009e430`, the body 137 classes hold at slot 110 and 20 per-class overrides tail
	// into). The scope frame `"CBaseEntity::KeyValue"` (0x105555f4) writes no game state. Arms in
	// retail's order (`walks/L0-r017.md`); every literal arm answers true on every path, the walk
	// answers its own result, nothing matched answers false.
	FString Key(InKey != nullptr ? InKey : TEXT(""));
	const FString Value(InValue != nullptr ? InValue : TEXT(""));
	// 0. `strchr(key, '#')` (0x10431f30, `PUSH 0x23` at 1009e458): found -> `*p = 0`, the caller's key
	//    buffer truncated in place, so every compare below sees the truncated name (`origin#2` is `origin`).
	int32 Hash = INDEX_NONE;
	if (Key.FindChar(TEXT('#'), Hash))
	{
		Key.LeftInline(Hash);
	}
	auto Arm = [this, &Key, &Value](const TCHAR* ArmName, bool bResult) -> bool
	{
		if (World != nullptr)
		{
			World->EmitRetailSite(*this, TEXT("kv_arm"), TEXT("CBaseEntity::KeyValue"), 0x1009e430u, TEXT("branch"),
				FString::Printf(TEXT("arm=%s key=%s value=\"%s\" result=%d"), ArmName, *Key, *Value, bResult ? 1 : 0));
		}
		return bResult;
	};
	auto Write = [this](const TCHAR* Field, const FString& Text)
	{
		if (World != nullptr)
		{
			World->EmitRetailSite(*this, TEXT("kv_write"), TEXT("CBaseEntity::KeyValue"), 0x1009e430u, TEXT("write"),
				FString::Printf(TEXT("field=%s value=%s"), Field, *Text));
		}
	};
	// 1. `if (key[0] == 'r')` -- a CASE-SENSITIVE byte compare (`CMP byte [EBP],0x72`, 1009e4b6). A key
	//    spelled `RenderColor` or `RenderAmt` skips 1a and 1b and reaches the walk, where CBaseEntity's
	//    row 19 (`rendercolor`, COLOR32, flags 6) takes `RenderColor` with all FOUR bytes written.
	if (Key.Len() > 0 && Key[0] == TEXT('r'))
	{
		// 1a. `__strcmpi(key, "rendercolor")` (0x105546b8) or `"rendercolor32"` (0x105555e4):
		//     `FUN_101d0630(&tmp, value)` (thunk 0x10009462), then bytes +0x1a0 / +0x1a1 / +0x1a2 := R, G, B
		//     (1009e4e4-1009e4fa). ALPHA (+0x1a3) is NOT written. Return 1.
		if (Key.Equals(TEXT("rendercolor"), ESearchCase::IgnoreCase) || Key.Equals(TEXT("rendercolor32"), ESearchCase::IgnoreCase))
		{
			uint8 Rgba[4];
			ElysiumParseRgba(Value, Rgba);                                      // 0x101d0630 -> 0x101d0570 (count 4)
			if (World != nullptr)
			{
				World->EmitRetailSite(*this, TEXT("kv_rgba"), TEXT("FUN_101d0630"), 0x101d0630u, TEXT("return"),
					FString::Printf(TEXT("r=%d g=%d b=%d a=%d"), Rgba[0], Rgba[1], Rgba[2], Rgba[3]));
			}
			RenderColor = (RenderColor & 0xff000000u) | static_cast<uint32>(Rgba[0]) | (static_cast<uint32>(Rgba[1]) << 8)
				| (static_cast<uint32>(Rgba[2]) << 16);
			Write(TEXT("m_clrRender"), FString::Printf(TEXT("0x%08x"), RenderColor));
			return Arm(TEXT("rendercolor"), true);
		}
		// 1b. `"renderamt"` (0x105555d8): `atoi(value)`; its low byte to +0x1a3 (1009e526). Return 1.
		//     `renderamt 300` is 44, `renderamt -1` is 255.
		if (Key.Equals(TEXT("renderamt"), ESearchCase::IgnoreCase))
		{
			const uint8 Alpha = static_cast<uint8>(ElysiumParseRetailAtoi(*Value));   // 0x10431447
			RenderColor = (RenderColor & 0x00ffffffu) | (static_cast<uint32>(Alpha) << 24);
			Write(TEXT("m_clrRender"), FString::Printf(TEXT("0x%08x"), RenderColor));
			return Arm(TEXT("renderamt"), true);
		}
	}
	// 2. `"disableshadows"` (0x105555c4): `atoi(value) != 0` -> `m_fEffects |= 0x20` (1009e5a3 / 1009e660).
	//    Return 1 either way; `disableshadows 0` and `disableshadows abc` leave the word.
	if (Key.Equals(TEXT("disableshadows"), ESearchCase::IgnoreCase))
	{
		if (ElysiumParseRetailAtoi(*Value) != 0)
		{
			EffectsWord |= 0x20u;
			Write(TEXT("m_fEffects"), FString::Printf(TEXT("0x%x"), EffectsWord));
		}
		return Arm(TEXT("disableshadows"), true);
	}
	// 3. `"mins"` (0x105555bc): `FUN_101d03e0(&local, value)` (thunk 0x10010b9a), `maxs = cp.vtable[2]()`
	//    (`CCollisionProperty::vfunc2` 0x100dc830 = `&m_vecMaxs`), `FUN_100dc770(cp, local, maxs)` (1009e5c2-
	//    1009e5d2): the box setter with the OTHER side unchanged (`walks/L0-r016.md`). Return 1.
	if (Key.Equals(TEXT("mins"), ESearchCase::IgnoreCase))
	{
		int32 Tokens = 0;
		const FVector Mins = ElysiumParseVec3(Value, &Tokens);
		if (World != nullptr)
		{
			World->EmitRetailSite(*this, TEXT("kv_parse"), TEXT("FUN_101d0310"), 0x101d0310u, TEXT("return"),
				FString::Printf(TEXT("count=3 tokens=%d zero_fill=%d out=%s"), Tokens, 3 - Tokens, *KeyValueVec(Mins)));
		}
		SetCollisionBounds(Mins, CollMaxs);                                     // 1009e5d2 -> 0x100dc770
		return Arm(TEXT("mins"), true);
	}
	// 4. `"maxs"` (0x105555b4): `mins = cp.vtable[1]()` (`vfunc1` 0x100dc810 = `&m_vecMins`), the parse,
	//    `FUN_100dc770(cp, mins, local)` (1009e61d-1009e62a). Return 1.
	if (Key.Equals(TEXT("maxs"), ESearchCase::IgnoreCase))
	{
		int32 Tokens = 0;
		const FVector Maxs = ElysiumParseVec3(Value, &Tokens);
		if (World != nullptr)
		{
			World->EmitRetailSite(*this, TEXT("kv_parse"), TEXT("FUN_101d0310"), 0x101d0310u, TEXT("return"),
				FString::Printf(TEXT("count=3 tokens=%d zero_fill=%d out=%s"), Tokens, 3 - Tokens, *KeyValueVec(Maxs)));
		}
		SetCollisionBounds(CollMins, Maxs);                                     // 1009e62a -> 0x100dc770
		return Arm(TEXT("maxs"), true);
	}
	// 5. `"disablereceiveshadows"` (0x10555598): `atoi != 0` -> `m_fEffects |= 0x80` (1009ea1b / 1009e660).
	//    Return 1 either way.
	if (Key.Equals(TEXT("disablereceiveshadows"), ESearchCase::IgnoreCase))
	{
		if (ElysiumParseRetailAtoi(*Value) != 0)
		{
			EffectsWord |= 0x80u;
			Write(TEXT("m_fEffects"), FString::Printf(TEXT("0x%x"), EffectsWord));
		}
		return Arm(TEXT("disablereceiveshadows"), true);
	}
	// 6. `"angle"` (0x10555590), reached only when 5 did not match: the rewrite (`RewriteAngleKey`), then
	//    `key := "angles"` (the literal 0x10555568), `value := DAT_106cf040` (1009e73a-1009e746) and the
	//    fall into arm 7. The walk below never sees a rewritten key: arm 7 always takes it.
	FString ArmKey = Key;
	FString ArmValue = Value;
	if (Key.Equals(TEXT("angle"), ESearchCase::IgnoreCase))
	{
		const double V = FCString::Atod(*Value);                                // 0x1043136f atof
		ArmValue = RewriteAngleKey(V, GetAngles());                             // slot 221 twice, the LOCAL +0x428 word
		ArmKey = TEXT("angles");
		if (World != nullptr)
		{
			World->EmitRetailSite(*this, TEXT("kv_arm"), TEXT("CBaseEntity::KeyValue"), 0x1009e430u, TEXT("rewrite"),
				FString::Printf(TEXT("arm=angle key=angle value=\"%s\" v=%g branch=%s angles=\"%s\""), *Value, V,
					V < 0.0 ? TEXT("literal") : TEXT("format"), *ArmValue));
		}
	}
	// 7. `"angles"` (0x10555568): `FUN_101d03e0(&local, value)`, then slot 218 (`+0x368`) `SetAbsAngles(&local)`
	//    0x100b2510 (1009e777-1009e78c). Return 1. Pitch, yaw, roll in Source degrees, as `QAngle` is.
	if (ArmKey.Equals(TEXT("angles"), ESearchCase::IgnoreCase))
	{
		int32 Tokens = 0;
		const FVector A = ElysiumParseVec3(ArmValue, &Tokens);
		if (World != nullptr)
		{
			World->EmitRetailSite(*this, TEXT("kv_parse"), TEXT("FUN_101d0310"), 0x101d0310u, TEXT("return"),
				FString::Printf(TEXT("count=3 tokens=%d zero_fill=%d out=%s"), Tokens, 3 - Tokens, *KeyValueVec(A)));
		}
		FRotator AbsAngles(static_cast<float>(A.X), static_cast<float>(A.Y), static_cast<float>(A.Z));   // pitch, yaw, roll
		SetAbsAngles(AbsAngles);                                                // 1009e78c slot 218
		return Arm(TEXT("angles"), true);
	}
	// 8. `"origin"` (0x10555560): the parse, then slot 216 (`+0x360`) `SetAbsOrigin(&local)` 0x100b2300
	//    (1009e7b6-1009e7cb). Return 1. The value is Source inches on Source axes; this port's absolute
	//    origin word is Unreal centimetres, so the parsed vector is converted (`x*U, -y*U, z*U`). A def's
	//    own authored `origin` key is the text the exporter already converted into `FElysiumEntityDef::
	//    Origin` -- the 3D-skybox miniature's placement transform included (`ElysiumEntityDefs.h`) -- so
	//    that key's write is the def's vector, the same point without a second rounding; any other
	//    `origin` key (a `#`-suffixed one, a later repeat) converts the parsed text itself.
	if (ArmKey.Equals(TEXT("origin"), ESearchCase::IgnoreCase))
	{
		int32 Tokens = 0;
		const FVector Src = ElysiumParseVec3(ArmValue, &Tokens);
		if (World != nullptr)
		{
			World->EmitRetailSite(*this, TEXT("kv_parse"), TEXT("FUN_101d0310"), 0x101d0310u, TEXT("return"),
				FString::Printf(TEXT("count=3 tokens=%d zero_fill=%d out=%s"), Tokens, 3 - Tokens, *KeyValueVec(Src)));
		}
		const bool bDefPlacement = Def != nullptr && Def->Keys.FindRef(TEXT("origin")) == ArmValue;
		FVector Cm = bDefPlacement ? Def->Origin
			: FVector(Src.X * ElysiumMove::U, -Src.Y * ElysiumMove::U, Src.Z * ElysiumMove::U);
		SetAbsOrigin(Cm);                                                       // 1009e7cb slot 216
		return Arm(TEXT("origin"), true);
	}
	// 9. The datamap walk: `for (m = vtbl[0x148](); m; m = m->baseMap) if (walker(this, m->dataDesc,
	//    m->count, key, value)) return 1; return 0;` (1009e80f-1009e9dd), under the `ent_debugkeys` gate:
	//    with the convar empty the plain walk; otherwise the same walk, printing a handled key when the
	//    convar names this entity's `m_iClassname` (1009e8c6) or the level's `dataClassName` (1009e969),
	//    and the miss (1009e9c8) when it named either. Slot 82 answers the leaf descriptor; `BaseName` is `+0xc`.
	const FString DebugClass = GEntDebugKeys.GetValueOnGameThread();
	bool bTrace = false;
	FString TraceName;
	if (!DebugClass.IsEmpty() && Def != nullptr && DebugClass.Equals(Def->Classname, ESearchCase::IgnoreCase))
	{
		bTrace = true;
		TraceName = Def->Classname;
	}
	const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
	for (const FElysiumClassDesc* Level = static_cast<const FElysiumClassDesc*>(GetDataDescMap()); Level != nullptr;
		Level = Level->BaseName.IsNone() ? nullptr : Reg.Find(Level->BaseName))
	{
		if (!bTrace && !DebugClass.IsEmpty() && DebugClass.Equals(Level->ClassName.ToString(), ESearchCase::IgnoreCase))
		{
			bTrace = true;
			TraceName = Level->ClassName.ToString();
		}
		if (KeyValueDatamapWalk(*Level, *Key, *Value))                          // thunk 0x1000ed59 -> 0x101a5a80
		{
			if (bTrace)
			{
				UE_LOG(LogElysiumKeyValue, Display, TEXT("(%s) key: %-16s value: %s"), *TraceName, *Key, *Value);   // 0x10555514
			}
			return Arm(TEXT("walk"), true);
		}
	}
	if (bTrace)
	{
		UE_LOG(LogElysiumKeyValue, Display, TEXT("!! (%s) key not handled: \"%s\" \"%s\""), *TraceName, *Key, *Value);   // 0x10555534
	}
	return Arm(TEXT("walk"), false);                                            // 1009e9dd
}

// --- the walker ---------------------------------------------------------------------------------

bool FElysiumEntity::KeyValueDatamapWalk(const FElysiumClassDesc& Level, const TCHAR* Key, const TCHAR* Value)
{
	// `FUN_101a5a80` 0x101a5a80 (456 B; `uint walk(this, desc, count, key, value)`, `ADD ESP,0x14` at
	// the caller). Per row `i` (0x2c apart):
	//   A. Embedded: `fieldType == 9 && fieldSize == 1` with a `td` (+0x20) -> `walk(this + offset, S->dataDesc,
	//      S->count, key, value)` for each sub-map `S` down its `baseMap` (101a5aa8-101a5ae0); handled ->
	//      return 1; not handled -> fall through to B on the same row. CBaseEntity's one embedded row is
	//      `m_Collision` (row 5, `datamap_CCollisionProperty` 0x10560c20, 11 rows, one KEY row `solid`
	//      INTEGER +0x40): this port registers that key on the base descriptor itself (`Embedded`), so the
	//      descent is that row, one level flat -- the same word, the same answer.
	//   B. `(flags & 4) && __strcmpi(externalName, key) == 0` (101a5b02 / 101a5b0a): the type switch.
	//      Range `1 <= type <= 0x11`, a byte table at 0x101a5c70 into ten handlers at 0x101a5c48:
	//      1, 15: `atof` -> float. 2, 16, 17: the string pool `FUN_1042bff0` (this port's `FString`).
	//      3, 14: `FUN_101d03e0` (three floats, zero-filled). 4: `atoi` -> int32. 5: `atoi != 0` -> bool
	//      byte (`SETNZ`). 6: `atoi` -> int16. 7: `atoi` -> int8. 8: `FUN_101d0630` (four RGBA bytes).
	//      10: `ops->vtbl[4](&{this+offset, this, &row}, value)` (101a5c22-101a5c3b), for the output rows
	//      `CEventsSaveDataOps::vfunc4` 0x100cdb20 -> `FUN_100cd6d0`; any other type-10 row's ops object
	//      is UNRECOVERED (no CBaseEntity / CAISound row has one). Each returns 1.
	//      Anything else (0, 9 past A, 11, 12, 13, 18 and up): `Warning("Bad field in entity!!\n")`
	//      (0x105934c4) and the scan CONTINUES with the next row -- no return.
	//   C. Next row; the end returns 0.
	// Rows are unique by external name within a level here (`checkf` in the registration), so the
	// table order within a level cannot change the answer: the one match is the first match.
	const FElysiumFieldAccessor* Row = Level.Fields.Find(FName(Key));          // FName folds case: __strcmpi
	if (Row == nullptr || !Row->bMapKey)
	{
		return false;                                                           // the scan reaches the table's end
	}
	const uint8 Type = Row->RetailFieldType();
	const FString Text(Value);
	FElysiumVariant Parsed;
	FString Shown;
	switch (Type)
	{
	case ElysiumRetailFieldType::Float:
	case ElysiumRetailFieldType::Time:
		Parsed = FElysiumVariant::Float(static_cast<float>(FCString::Atod(*Text)));   // 101a5b3e atof -> FSTP float
		Shown = FString::Printf(TEXT("%g"), Parsed.AsFloat);
		break;
	case ElysiumRetailFieldType::String:
	case ElysiumRetailFieldType::ModelName:
	case ElysiumRetailFieldType::SoundName:
		Parsed = FElysiumVariant::String(Text);                                 // 101a5b5e FUN_1042bff0 (the pool)
		Shown = FString::Printf(TEXT("\"%s\""), *Text);
		break;
	case ElysiumRetailFieldType::Vector:
	case ElysiumRetailFieldType::PositionVector:
	{
		int32 Tokens = 0;
		const FVector V = ElysiumParseVec3(Text, &Tokens);                      // 101a5b7e FUN_101d03e0
		if (World != nullptr)
		{
			World->EmitRetailSite(*this, TEXT("kv_parse"), TEXT("FUN_101d0310"), 0x101d0310u, TEXT("return"),
				FString::Printf(TEXT("count=3 tokens=%d zero_fill=%d out=%s"), Tokens, 3 - Tokens, *KeyValueVec(V)));
		}
		Parsed = FElysiumVariant::Vector(V);
		Shown = KeyValueVec(V);
		break;
	}
	case ElysiumRetailFieldType::Integer:
		Parsed = FElysiumVariant::Int(ElysiumParseRetailAtoi(*Text));          // 101a5b97 atoi
		Shown = FString::FromInt(Parsed.AsInt);
		break;
	case ElysiumRetailFieldType::Boolean:
		Parsed = FElysiumVariant::Bool(ElysiumParseRetailAtoi(*Text) != 0);    // 101a5bb2 SETNZ
		Shown = Parsed.AsBool ? TEXT("1") : TEXT("0");
		break;
	case ElysiumRetailFieldType::Short:
		Parsed = FElysiumVariant::Int(static_cast<int16>(ElysiumParseRetailAtoi(*Text)));   // 101a5bd2
		Shown = FString::FromInt(Parsed.AsInt);
		break;
	case ElysiumRetailFieldType::Character:
		Parsed = FElysiumVariant::Int(static_cast<int8>(ElysiumParseRetailAtoi(*Text)));    // 101a5bf3
		Shown = FString::FromInt(Parsed.AsInt);
		break;
	case ElysiumRetailFieldType::Color32:
	{
		uint8 Rgba[4];
		const uint32 Packed = ElysiumParseRgba(Text, Rgba);                     // 101a5c13 FUN_101d0630: all FOUR bytes
		if (World != nullptr)
		{
			World->EmitRetailSite(*this, TEXT("kv_rgba"), TEXT("FUN_101d0630"), 0x101d0630u, TEXT("return"),
				FString::Printf(TEXT("r=%d g=%d b=%d a=%d"), Rgba[0], Rgba[1], Rgba[2], Rgba[3]));
		}
		Parsed = FElysiumVariant::Int(static_cast<int32>(Packed));
		Shown = FString::Printf(TEXT("0x%08x"), Packed);
		break;
	}
	case ElysiumRetailFieldType::Custom:
		Parsed = FElysiumVariant::String(Text);                                 // 101a5c3b ops->vtbl[4](field, value)
		Shown = FString::Printf(TEXT("\"%s\""), *Text);
		break;
	default:
		// 101a5c44: `Warning("Bad field in entity!!\n")`, then the next row. No CBaseEntity or CAISound
		// KEY row is typed 0, 9, 11, 12 or 13, so on those chains this arm is unreachable.
		UE_LOG(LogElysiumKeyValue, Warning, TEXT("Bad field in entity!! (%s %s %s)"), *Level.ClassName.ToString(), Key, *DebugString());
		if (World != nullptr)
		{
			World->EmitRetailSite(*this, TEXT("kv_warn"), TEXT("FUN_101a5a80"), 0x101a5a80u, TEXT("warn"),
				FString::Printf(TEXT("level=%s row=%s type=%d msg=\"Bad field in entity!!\""), *Level.ClassName.ToString(), Key, Type));
		}
		return false;
	}
	if (Row->Set)
	{
		Row->Set(*this, Parsed);                                                // the store at `this + offset`
	}
	if (World != nullptr)
	{
		World->EmitRetailSite(*this, TEXT("kv_walk"), TEXT("FUN_101a5a80"), 0x101a5a80u, TEXT("branch"),
			FString::Printf(TEXT("level=%s row=%s type=%d key=%s value=%s result=1"), *Level.ClassName.ToString(),
				Key, Type, Key, *Shown));
	}
	return true;
}

// --- the output rows' custom op -----------------------------------------------------------------

void FElysiumEntity::ParseOutputAction(const FString& Value, FElysiumOutputDef& Row)
{
	// `FUN_100ccf90` 0x100ccf90 (`CEventAction::CEventAction(const char*)`): the record's defaults first
	// -- `+0x1c` link 0, `+0x18` id `++DAT_106e70e8`, `+0x10` delay 0, `+0x00` target / `+0x04` input /
	// `+0x08` param / `+0x0c` python null, `+0x14` times -1 -- then, when the text is non-null, six
	// `FUN_101d16c0(buf, p, ',')` splits: copy-until-the-next-comma, trims nothing, knows no quotes; an
	// empty or exhausted source yields "" (`docs/vtmb/entity_io.md` "The row parser, field by field").
	Row.Target.Reset();
	Row.Input.Reset();
	Row.Param.Reset();
	Row.Delay = 0.0f;
	Row.Times = -1;
	Row.Python.Reset();
	const TCHAR* P = *Value;
	auto Split = [&P](FString& OutToken)
	{
		// `FUN_101d16c0` 0x101d16c0: a null or empty source writes "" and answers NULL; otherwise copy up
		// to the comma or NUL, write "", and answer the byte after the comma (or the NUL itself).
		OutToken.Reset();
		if (P == nullptr || *P == 0)
		{
			P = nullptr;
			return;
		}
		while (*P != 0 && *P != TEXT(','))
		{
			OutToken.AppendChar(*P++);
		}
		if (*P != 0)
		{
			++P;
		}
	};
	FString Token;
	Split(Token);
	if (!Token.IsEmpty()) { Row.Target = Token; }                                // field 0, interned
	Split(Token);
	Row.Input = Token.IsEmpty() ? FString(TEXT("Use")) : Token;                  // field 1: empty -> `"Use"` (0x10555f7c)
	Split(Token);
	if (!Token.IsEmpty()) { Row.Param = Token; }                                 // field 2
	Split(Token);
	if (!Token.IsEmpty()) { Row.Delay = static_cast<float>(FCString::Atod(*Token)); }   // field 3: `_atof`, skipped when empty
	Split(Token);
	if (!Token.IsEmpty())
	{
		const int32 Times = ElysiumParseRetailAtoi(*Token);                      // field 4: `_atoi`, 0 rewritten to -1
		Row.Times = Times == 0 ? -1 : Times;
	}
	Split(Token);
	if (!Token.IsEmpty()) { Row.Python = Token; }                                // field 5: the Python call string
}

void FElysiumEntity::AddOutputAction(FName Output, const FString& Value)
{
	// `FUN_100cd6d0` 0x100cd6d0 (`CBaseEntityOutput::ParseEventAction`): `new (0x20) CEventAction(text)`
	// (`FUN_100cd220`, `FUN_100ccf90`), then `action->next = m_ActionList (+0x14); m_ActionList = action`
	// -- a PREPEND, so repeated keys for one output fire in reverse authored order.
	FElysiumRuntimeOutput Action;
	Action.Row.Name = Output.ToString();
	ParseOutputAction(Value, Action.Row);
	Action.TimesRemaining = Action.Row.Times;
	RuntimeOutputs.Insert(MoveTemp(Action), 0);
	if (World != nullptr)
	{
		// `count` is THIS output's list length (retail keeps one `m_ActionList` per output object; this
		// port holds every output's runtime actions in one array, so the count filters by name).
		int32 Count = 0;
		for (const FElysiumRuntimeOutput& Held : RuntimeOutputs)
		{
			if (FName(*Held.Row.Name) == Output) { ++Count; }
		}
		const FElysiumOutputDef& R = RuntimeOutputs[0].Row;
		World->EmitRetailSite(*this, TEXT("kv_output"), TEXT("FUN_100cd6d0"), 0x100cd6d0u, TEXT("write"),
			FString::Printf(TEXT("output=%s target=%s input=%s param=%s delay=%g times=%d python=\"%s\" count=%d"),
				*R.Name, *R.Target, *R.Input, *R.Param, R.Delay, R.Times, *R.Python, Count));
	}
}

// --- slot 121 -----------------------------------------------------------------------------------

bool FElysiumEntity::ReadKeyField(TCHAR* Name, void* Out)
{
	// The slot's `variant_t*` is this port's `FElysiumKeyFieldValue`.
	FElysiumKeyFieldValue Scratch;
	FElysiumKeyFieldValue& Variant = Out != nullptr ? *static_cast<FElysiumKeyFieldValue*>(Out) : Scratch;
	return ReadKeyFieldTyped(Name, Variant);
}

bool FElysiumEntity::ReadKeyFieldTyped(const TCHAR* Name, FElysiumKeyFieldValue& Out)
{
	// `CBaseEntity::ReadKeyField(char*, variant_t*)` 0x100acab0 (273 B). The scope frame
	// `"CBaseEntity::ReadKeyField"` (0x1055726c) writes no game state. `name != NULL`: for each map `m`
	// from slot 82 down `baseMap` (+0xc), for each row `i < m->count`: `(flags_i & 0x14) != 0` (KEY or
	// OUTPUT, the byte at row +0x12; 100acb1f) `&& __strcmpi(externalName_i, name) == 0` (100acb2e) ->
	// `FUN_100d0390(out, fieldType_i, this + fieldOffset_i)`, return true. No match or a null name:
	// return false. Derived before base, first match wins, case-insensitive; embedded maps are NOT
	// descended (a row is only tested for its own name), so `solid` reads false. Its one confirmed
	// caller is `CC_Ent_Dump_Sub` 0x100af340 (the `ent_dump` console command); the Python attribute
	// reads go through `FUN_10195940`, not this slot (`docs/vtmb/python_bridge.md`).
	if (Name == nullptr)
	{
		return false;
	}
	const FName Wanted(Name);
	const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
	for (const FElysiumClassDesc* Level = static_cast<const FElysiumClassDesc*>(GetDataDescMap()); Level != nullptr;
		Level = Level->BaseName.IsNone() ? nullptr : Reg.Find(Level->BaseName))
	{
		const FElysiumFieldAccessor* Row = Level->Fields.Find(Wanted);
		if (Row == nullptr || !(Row->bMapKey || Row->bOutput) || Row->bEmbedded)
		{
			continue;
		}
		const uint8 Type = Row->RetailFieldType();
		const FElysiumVariant Live = Row->Get ? Row->Get(*this) : FElysiumVariant::Void();
		// `FUN_100d0390` 0x100d0390: `out+0x10 := type` first, then the switch on `type - 1` through the
		// table at 0x100d0438 (types above 14 skip it): 1, 2, 4, 8 copy a dword to `+0`; 3, 14 copy three;
		// 5 one byte zero-extended; 12 (EHANDLE) the dword to `+0xc`; 11 (CLASSPTR) the pointee's handle
		// to `+0xc`, or -1 for null; anything else (0, 6, 7, 9, 10, 13, 15, 16, 17, 18, 19) `+0 := 0`
		// and `type := 0` (VOID). The read still answers true.
		Out.FieldType = Type;
		switch (Type)
		{
		case ElysiumRetailFieldType::Float:
			Out.Value = FElysiumVariant::Float(Live.ToFloat());
			break;
		case ElysiumRetailFieldType::String:
			Out.Value = FElysiumVariant::String(Live.ToString());
			break;
		case ElysiumRetailFieldType::Integer:
		case ElysiumRetailFieldType::Color32:
			Out.Value = FElysiumVariant::Int(Live.ToInt());
			break;
		case ElysiumRetailFieldType::Vector:
		case ElysiumRetailFieldType::PositionVector:
			Out.Value = FElysiumVariant::Vector(Live.ToVector());
			break;
		case ElysiumRetailFieldType::Boolean:
			Out.Value = FElysiumVariant::Int(Live.ToBool() ? 1 : 0);          // the byte, zero-extended into the dword
			break;
		case ElysiumRetailFieldType::EHandle:
		case ElysiumRetailFieldType::ClassPtr:
			Out.Value = FElysiumVariant::Handle(Live.ToHandle());
			break;
		default:
			Out.FieldType = 0;
			Out.Value = FElysiumVariant::Void();
			break;
		}
		if (World != nullptr)
		{
			World->EmitRetailSite(*this, TEXT("kv_variant"), TEXT("FUN_100d0390"), 0x100d0390u, TEXT("write"),
				FString::Printf(TEXT("type=%d value=%s"), Out.FieldType, *Out.Value.ToString()));
			World->EmitRetailSite(*this, TEXT("kv_read"), TEXT("CBaseEntity::ReadKeyField"), 0x100acab0u, TEXT("return"),
				FString::Printf(TEXT("name=%s level=%s row=%s flags=0x%x type=%d(%s) result=1"), Name,
					*Level->ClassName.ToString(), *Wanted.ToString(),
					(Row->bSave ? 0x2u : 0u) | (Row->bMapKey ? 0x4u : 0u) | (Row->bKeyable ? 0x8u : 0u) | (Row->bOutput ? 0x10u : 0u),
					Type, KeyValueTypeName(Type)));
		}
		return true;
	}
	if (World != nullptr)
	{
		World->EmitRetailSite(*this, TEXT("kv_read"), TEXT("CBaseEntity::ReadKeyField"), 0x100acab0u, TEXT("return"),
			FString::Printf(TEXT("name=%s result=0"), Name));
	}
	return false;
}
