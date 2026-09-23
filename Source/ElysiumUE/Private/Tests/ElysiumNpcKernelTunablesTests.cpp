#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Substrate/ElysiumNpcKernelTunables.h"

// Spec 0019 story 4 — the tunables table. Every value here was read out of the pinned retail
// `vampire.dll` by `gen_kernel_tunables --check`; these cases hold the generated C++ to the few
// rows a regeneration must never silently change, and the ConVar store to retail's `ConVar`
// semantics: a default parsed `atof` / `atoi`, and `SetValue(float)` writing `(int)value`.

static constexpr EAutomationTestFlags GElysiumNpcKernelTunablesFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// The width is the reading instruction's: `0x10449280` is `FADD double ptr`, and its low dword
// alone would read as a float 0.0 — the misreading this table exists to make impossible.
static_assert(ElysiumNpcTunables::OneDouble == 1.0, "0x10449280 is the DOUBLE 1.0");
static_assert(ElysiumNpcTunables::SixtyFourDouble == 64.0, "0x1049ae28 is the DOUBLE 64.0");
static_assert(ElysiumNpcTunables::StepHeightBase == 18.0f, "0x10453b94, slot 522's step height");

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelTunablesConVarsTest,
	"Elysium.Substrate.NpcKernelTunables.ConVars", GElysiumNpcKernelTunablesFlags)
bool FElysiumNpcKernelTunablesConVarsTest::RunTest(const FString&)
{
	using ElysiumNpcTunables::EConVar;
	ElysiumNpcTunables::ResetConVars();

	// The shipped defaults (`docs/vtmb/npc-ai/convars.md`). No shipped config, script or vdata file
	// sets any of these, so the default IS what retail runs.
	TestEqual(TEXT("debug_allow_move_facing ships 1"),
		ElysiumNpcTunables::ConVarInt(EConVar::DebugAllowMoveFacing), 1);
	TestEqual(TEXT("debug_hunting_aggressive ships 1"),
		ElysiumNpcTunables::ConVarInt(EConVar::DebugHuntingAggressive), 1);
	TestEqual(TEXT("debug_alert_aggressive ships 0"),
		ElysiumNpcTunables::ConVarInt(EConVar::DebugAlertAggressive), 0);
	TestEqual(TEXT("npc_hit_buildup_amount ships 2"),
		ElysiumNpcTunables::ConVarInt(EConVar::NpcHitBuildupAmount), 2);
	TestEqual(TEXT("flex_minplayertime ships 5"),
		ElysiumNpcTunables::ConVarFloat(EConVar::FlexMinplayertime), 5.f);
	TestEqual(TEXT("flex_maxplayertime ships 7"),
		ElysiumNpcTunables::ConVarFloat(EConVar::FlexMaxplayertime), 7.f);
	TestEqual(TEXT("debug_melee_advance_combatmove_dist ships 100"),
		ElysiumNpcTunables::ConVarFloat(EConVar::DebugMeleeAdvanceCombatmoveDist), 100.f);
	TestEqual(TEXT("debug_viewcone_back_dist ships 40"),
		ElysiumNpcTunables::ConVarFloat(EConVar::DebugViewconeBackDist), 40.f);
	TestEqual(TEXT("ent_trace_conditions ships 1"),
		ElysiumNpcTunables::ConVarInt(EConVar::EntTraceConditions), 1);
	TestEqual(TEXT("werewolf_draw_hints ships 0 — the image, not the oracle's 40"),
		ElysiumNpcTunables::ConVarInt(EConVar::WerewolfDrawHints), 0);
	TestEqual(TEXT("debug_tentacle_mask ships -1"),
		ElysiumNpcTunables::ConVarInt(EConVar::DebugTentacleMask), -1);

	// `atoi`, not the truncated float: ".15" is 0.15 at `+0x28` and 0 at `+0x2c`.
	TestEqual(TEXT("debug_turn_scalar's float is .15"),
		ElysiumNpcTunables::ConVarFloat(EConVar::DebugTurnScalar), 0.15f);
	TestEqual(TEXT("... and its int is atoi(\".15\") = 0"),
		ElysiumNpcTunables::ConVarInt(EConVar::DebugTurnScalar), 0);
	const ElysiumNpcTunables::FConVarRow& Row = ElysiumNpcTunables::ConVarRow(EConVar::DebugTurnScalar);
	TestEqual(TEXT("the row keeps the console name"), FString(Row.ConsoleName),
		FString(TEXT("debug_turn_scalar")));
	TestEqual(TEXT("and the default string verbatim"), FString(Row.Default), FString(TEXT(".15")));
	TestEqual(TEXT("and the OBJECT address (the oracle's DAT_ pointer is object + 4)"), Row.Object,
		0x10924c90u);

	// `ConVar::SetValue(float)`: `m_fValue = value; m_nValue = (int)value`.
	ElysiumNpcTunables::SetConVar(EConVar::DebugHuntingAggressive, 0.f);
	TestEqual(TEXT("a set value is read back"),
		ElysiumNpcTunables::ConVarInt(EConVar::DebugHuntingAggressive), 0);
	ElysiumNpcTunables::SetConVar(EConVar::DebugForceAnim, 2.9f);
	TestEqual(TEXT("the int of a set float is truncated"),
		ElysiumNpcTunables::ConVarInt(EConVar::DebugForceAnim), 2);
	TestEqual(TEXT("an untouched ConVar keeps its default"),
		ElysiumNpcTunables::ConVarInt(EConVar::DebugAllowMoveFacing), 1);

	// A reset restores the DEFAULTS, not zero.
	ElysiumNpcTunables::ResetConVars();
	TestEqual(TEXT("reset restores the shipped 1"),
		ElysiumNpcTunables::ConVarInt(EConVar::DebugHuntingAggressive), 1);
	TestEqual(TEXT("and the shipped 0"), ElysiumNpcTunables::ConVarInt(EConVar::DebugForceAnim), 0);
	return true;
}

#endif
