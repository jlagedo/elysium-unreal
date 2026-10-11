#include "ElysiumPhysicsSurfaceProps.h"

#include "ElysiumContentPaths.h"
#include "ElysiumKeyValues.h"
#include "Misc/FileHelper.h"

namespace ElysiumSurfaceParser
{
	using FParameters = FElysiumPhysicsSurfaceParameters;
	struct FFloatField { const TCHAR* Name; float FParameters::* Member; };
	struct FSoundField { const TCHAR* Name; uint16 FParameters::* Member; };
	// 26015170: authored assignments, independent of the old backend's memory layout.
	constexpr FFloatField Floats[] = {
		{TEXT("thickness"), &FParameters::Thickness}, {TEXT("density"), &FParameters::Density},
		{TEXT("elasticity"), &FParameters::Elasticity}, {TEXT("friction"), &FParameters::Friction},
		{TEXT("maxspeedfactor"), &FParameters::MaxSpeedFactor}, {TEXT("jumpfactor"), &FParameters::JumpFactor},
		{TEXT("audioreflectivity"), &FParameters::AudioReflectivity},
		{TEXT("audiohardnessfactor"), &FParameters::AudioHardnessFactor},
		{TEXT("audiohardminvelocity"), &FParameters::AudioHardMinVelocity},
		{TEXT("audioroughnessfactor"), &FParameters::AudioRoughnessFactor},
		{TEXT("scraperoughthreshold"), &FParameters::ScrapeRoughThreshold},
		{TEXT("impacthardthreshold"), &FParameters::ImpactHardThreshold}, {TEXT("dampening"), &FParameters::Dampening}
	};
	constexpr FSoundField Sounds[] = {
		{TEXT("stepleft"), &FParameters::StepLeft}, {TEXT("stepright"), &FParameters::StepRight},
		{TEXT("impact"), &FParameters::Impact}, {TEXT("impactsoft"), &FParameters::ImpactSoft},
		{TEXT("impacthard"), &FParameters::ImpactHard}, {TEXT("scrape"), &FParameters::Scrape},
		{TEXT("scrapesmooth"), &FParameters::ScrapeSmooth}, {TEXT("scraperough"), &FParameters::ScrapeRough},
		{TEXT("bulletimpact"), &FParameters::BulletImpact}, {TEXT("rolling"), &FParameters::Rolling}
	};
	constexpr const TCHAR* Weapons[] = {TEXT("metal"), TEXT("wood"), TEXT("blade"),
		TEXT("fist"), TEXT("bullet"), TEXT("claw")}; // 260d0184
	constexpr const TCHAR* Outcomes[] = {TEXT("norm"), TEXT("soak"), TEXT("crit")}; // 260d0178

	bool NumericSpace(TCHAR Character)
	{
		return Character == ' ' || (Character >= '\t' && Character <= '\r');
	}
	float Atof(const FString& Text)
	{
		// 260b55f7 (_atof) skips all six C whitespace bytes before __fltin260b99b4
		// -> ___strgtold12 260c0525: the same decimal/D-or-E exponent grammar as VC6 strtod.
		int32 Start = 0; while (Start < Text.Len() && NumericSpace(Text[Start])) ++Start;
		int32 End; return float(ElysiumKeyValues::RetailStrtod(Text.Mid(Start), End));
	}
	int32 Atoi(const FString& Text)
	{
		// 260b5532 -> _atol260b54a7: decimal only; 32-bit multiplication wraps, no clamp.
		int32 Position = 0; while (Position < Text.Len() && NumericSpace(Text[Position])) ++Position;
		const bool Negative = Position < Text.Len() && Text[Position] == '-';
		if (Position < Text.Len() && (Text[Position] == '-' || Text[Position] == '+')) ++Position;
		uint32 Accumulator = 0;
		while (Position < Text.Len() && Text[Position] >= '0' && Text[Position] <= '9')
			Accumulator = Accumulator * 10u + uint32(Text[Position++] - '0');
		return int32(Negative ? 0u - Accumulator : Accumulator);
	}

	// 26024fd0 -> 26001850 is the same recovered delimiter/comment/quote byte grammar
	// as vampire's lexer. This wrapper lowercases BOTH tokens and treats } as one token.
	FString Lower(const FString& Text)
	{
		// __strlwr260c6620, initial C locale: only ASCII upper-case bytes change.
		FString Result = Text;
		for (TCHAR& Character : Result) if (Character >= 'A' && Character <= 'Z') Character += 'a' - 'A';
		return Result;
	}
	void Pair(ElysiumKeyValues::FKvReader& Reader, FString& Key, FString& Value)
	{
		Key = Lower(ElysiumKeyValues::ReadToken(Reader, nullptr));
		if (Key == TEXT("}")) Value.Empty();
		else Value = Lower(ElysiumKeyValues::ReadToken(Reader, nullptr));
	}
}

FElysiumPhysicsSurfaceProps::FElysiumPhysicsSurfaceProps()
{
	// 26014c10: symbol zero is the interned empty string, before any filename/name/sound.
	Intern(TEXT(""));
}

FElysiumWorldMaterialMapping::FElysiumWorldMaterialMapping()
{
	// Manager ctor26015ba0; map absence must not construct a fresh manager.
	for (int32 Slot = 0; Slot < 128; ++Slot) Slots[Slot] = uint16(Slot);
}

bool FElysiumPhysicsSurfaceProps::InstallWorldMaterialTable(FElysiumWorldMaterialMapping& Mapping,
	bool bPresent, const TArray<FElysiumWorldMaterialDeclaration>& Declarations, FString& OutError) const
{
	OutError.Reset();
	if (!bPresent) return true;
	FElysiumWorldMaterialMapping Installed;
	for (uint16& Entry : Installed.Slots) Entry = 0; // world101587bb, not manager ctor.
	for (const FElysiumWorldMaterialDeclaration& Declaration : Declarations)
	{
		// Parser26024150 ->atoi: ASCII whitespace, optional sign, decimal prefix.
		const TCHAR* Cursor = *Declaration.SlotText;
		while (*Cursor == ' ' || *Cursor == '\t' || *Cursor == '\r' || *Cursor == '\n'
			|| *Cursor == '\v' || *Cursor == '\f') ++Cursor;
		const bool bNegative = *Cursor == '-';
		if (*Cursor == '-' || *Cursor == '+') ++Cursor;
		int64 Slot = 0;
		while (*Cursor >= '0' && *Cursor <= '9')
		{
			Slot = Slot * 10 + (*Cursor++ - '0');
			if (Slot > MAX_int32) break;
		}
		if ((bNegative && Slot != 0) || Slot > MAX_int32)
		{
			// No shipped negative/overflow witness: refuse memory-OOB without partial install.
			OutError = FString::Printf(TEXT("materialtable slot '%s' is negative or overflows signed atoi"),
				*Declaration.SlotText);
			return false;
		}
		if (Slot < 128) Installed.Slots[Slot] = uint16(GetSurfaceIndex(Declaration.SurfaceName));
	}
	Mapping = Installed; // #8 26015b90 ->26015ce0 copies low16; duplicates last-wins.
	return true;
}

const FElysiumPhysicsSurfaceData* FElysiumPhysicsSurfaceProps::ResolveWorldMaterial(
	const FElysiumWorldMaterialMapping& Mapping, uint8 Selector,
	const FElysiumPhysicsSurfaceData* BodyMaterial) const
{
	// Contact2604eb60: feature zero is the current body input, not mapped slot zero.
	if (Selector == 0) return BodyMaterial;
	return GetSurfaceData(Selector < 128 ? Mapping.Slots[Selector] : Selector); //26015ca0
}

uint16 FElysiumPhysicsSurfaceProps::Intern(const FString& Text)
{
	// 26022b40 / 260229d0: ctor260228e0 selects byte comparator260227b0 (case-sensitive).
	for (int32 Index = 0; Index < Symbols.Num(); ++Index)
		if (Symbols[Index].Equals(Text, ESearchCase::CaseSensitive)) return uint16(Index);
	const int32 Index = Symbols.Add(Text);
	return uint16(Index);
}

FString FElysiumPhysicsSurfaceProps::GetString(uint16 Symbol) const
{
	// 26022d10: invalid symbol resolves to the empty string.
	return Symbol == 0xffff ? FString() : Symbols[Symbol];
}

int32 FElysiumPhysicsSurfaceProps::GetSurfaceIndex(const FString& Name) const
{
	// #3 26014ed0 -> 260150b0: only this $-name has a reserved index; unknown is -1.
	if (Name.StartsWith(TEXT("$")) && ElysiumSurfaceParser::Lower(Name) == TEXT("$material_index_shadow"))
		return 0xf000;
	for (int32 Index = 0; Index < Surfaces.Num(); ++Index)
		if (GetString(Surfaces[Index].NameSymbol).Equals(Name, ESearchCase::CaseSensitive)) return Index;
	return INDEX_NONE;
}

const FElysiumPhysicsSurfaceData* FElysiumPhysicsSurfaceProps::GetSurfaceData(int32 Index) const
{
	// #10 260150e0 -> 26014f60 / 26015080 / 260150f0. >127 is NOT an ordinary row.
	if (Index > 127) Index = Index == 0xf000 ? ShadowIndex : 0;
	return Surfaces.IsValidIndex(Index) ? &Surfaces[Index] : nullptr;
}

FString FElysiumPhysicsSurfaceProps::GetSurfaceName(int32 Index) const
{
	const FElysiumPhysicsSurfaceData* Row = GetSurfaceData(Index); // #7 26014f30
	return Row ? GetString(Row->NameSymbol) : FString();
}

void FElysiumPhysicsSurfaceProps::Apply(FElysiumPhysicsSurfaceData& Row, const FString& Key, const FString& Value)
{
	using namespace ElysiumSurfaceParser;
	if (Key == TEXT("base"))
	{
		// 26015282 -> 26015140: copy all authored parameters NOW; invalid base leaves them alone.
		if (const FElysiumPhysicsSurfaceData* Base = GetSurfaceData(GetSurfaceIndex(Value)))
			Row.Values = Base->Values;
		return;
	}
	for (const FFloatField& Field : Floats)
	{
		if (Key == Field.Name)
		{
			Row.Values.*(Field.Member) = Atof(Value);
			return;
		}
	}
	if (Key == TEXT("climbable"))
	{
		Row.Values.Climbable = uint8(Atoi(Value)); return;
	}
	for (const FSoundField& Field : Sounds)
	{
		if (Key == Field.Name) { Row.Values.*(Field.Member) = Intern(Value); return; }
	}
	if (Key == TEXT("gamematerial"))
	{
		// 260157a0: one non-digit char -> toupper; all other strings -> atoi, truncate ushort.
		const int32 Code = Value.Len() == 1 && !(Value[0] >= '0' && Value[0] <= '9')
			? int32(Value[0] >= 'a' && Value[0] <= 'z' ? Value[0] - ('a' - 'A') : Value[0]) : Atoi(Value);
		Row.Values.GameMaterial = uint16(Code); return;
	}
	for (int32 Weapon = 0; Weapon < 6; ++Weapon)
	{
		// 26015850: strstr must match the beginning, then full key compares each outcome.
		if (!Key.StartsWith(Weapons[Weapon])) continue;
		for (int32 Outcome = 0; Outcome < 3; ++Outcome)
		{
			if (Key == FString::Printf(TEXT("%s_%s_impact"), Weapons[Weapon], Outcomes[Outcome]))
			{
				Row.Values.WeaponImpact[Weapon][Outcome] = Intern(Value); return;
			}
		}
	}
	// 260158c1: bad key diagnoses, then continues with the next ordered pair.
	Messages.Add(FString::Printf(TEXT("Bad surfaceprop key %s (%s)"), *Key, *Value));
}

int32 FElysiumPhysicsSurfaceProps::ParseSurfaceData(const FString& Filename, const FString& Text)
{
	using namespace ElysiumSurfaceParser;
	// #1 26015170 -> 26014df0: filename is registered before parsing, even for empty/bad text.
	const uint16 FileSymbol = Intern(Filename);
	if (ParsedFiles.Contains(FileSymbol)) return 0;
	ParsedFiles.Add(FileSymbol);
	ElysiumKeyValues::FKvReader Reader(Text);
	FString Key, Value;
	while (Reader.Pos != INDEX_NONE)
	{
		Pair(Reader, Key, Value);
		if (Value != TEXT("{")) continue;
		FElysiumPhysicsSurfaceData Row; // 260151f7: zero parameters before copying current default.
		Row.NameSymbol = Intern(Key);
		if (const FElysiumPhysicsSurfaceData* Default = GetSurfaceData(GetSurfaceIndex(TEXT("default"))))
			Row.Values = Default->Values;
		while (Reader.Pos != INDEX_NONE)
		{
			Pair(Reader, Key, Value);
			if (Key == TEXT("}"))
			{
				// 26015917: duplicate definition is parsed/interned, but the first stored row wins.
				if (GetSurfaceIndex(GetString(Row.NameSymbol)) < 0) Surfaces.Add(Row);
				break;
			}
			Apply(Row, Key, Value);
		}
	}
	if (!bCreatedShadow)
	{
		// 2601598c: once only, even an empty first file; copy current default, then overwrite.
		bCreatedShadow = true;
		FElysiumPhysicsSurfaceData Row;
		Row.NameSymbol = Intern(TEXT("$MATERIAL_INDEX_SHADOW"));
		if (const FElysiumPhysicsSurfaceData* Default = GetSurfaceData(GetSurfaceIndex(TEXT("default"))))
			Row.Values = Default->Values;
		Row.Values.Friction = 0.8f; Row.Values.Elasticity = 0.f;
		ShadowIndex = Surfaces.Add(Row);
	}
	return Surfaces.Num();
}

bool FElysiumPhysicsSurfaceProps::LoadDeployed(FString& OutError)
{
	// CPhysicsHook::Init1002d7d0 loads source, parses #1, then frees the file. Project-only resident data.
	TArray<uint8> Bytes;
	const FString Path = FElysiumContentPaths::CorpusRoot() / TEXT("physics/surfaceproperties.txt");
	if (!FFileHelper::LoadFileToArray(Bytes, *Path))
	{
		OutError = FString::Printf(TEXT("missing physics surface table %s; run uv run elysium export_v2 physics-surfaces-glb and uv run elysium import physics-surfaces"), *Path);
		return false;
	}
	FString Text; Text.Reserve(Bytes.Num());
	for (uint8 Byte : Bytes) { if (Byte == 0) break; Text.AppendChar(TCHAR(Byte)); }
	ParseSurfaceData(TEXT("scripts/surfaceproperties.txt"), Text);
	OutError.Empty(); return true;
}

FElysiumPhysicsSurfaceProps& FElysiumPhysicsSurfaceProps::ProcessService()
{
	static FElysiumPhysicsSurfaceProps Service; // factory26014be0 singleton260f0f40
	return Service;
}
