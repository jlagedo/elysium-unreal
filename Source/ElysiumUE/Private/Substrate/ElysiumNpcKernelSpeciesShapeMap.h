#pragma once

#include "CoreMinimal.h"

#include "Containers/ArrayView.h"

// Which port member carries which word of an introduced species' own datamap.
//
// The Troika map (`Substrate/ElysiumNpcKernelShapeMap.h`) is keyed by offset alone, because
// `CAI_BaseNPC` and `CAI_BaseNPCTroika` are one chain and an offset names one word. Above `+0x665c`
// that stops being true: every species lays its own words over the same offsets (`+0x6664` is
// fourteen different words across fourteen classes). So a species row is keyed by the retail class
// that DECLARES the word and the offset together, and the binding generator
// (`research/tooling/gen_kernel_bindings.py`) registers it on that class's descriptor only;
// descendants inherit it through the descriptor chain and siblings never see it.
//
// **Hand-maintained, and compile-checked**, like the Troika map: a row names the port class and
// member as identifiers, so a rename or a move is a build error. One row per record of an
// introduced species' own datamap (`docs/specs/0019-npc-kernel-rework/story-5/fields-step4.tsv`
// is the reviewed record behind each row).

// What this runtime does with a species word.
enum class EElysiumNpcSpeciesWordHome : uint8
{
	// A member carries it: the declaring class's own, or storage `FElysiumNpc` still holds for a
	// deferred class or a Troika reader (the row's note says which).
	Member,
	// The species datamap re-declares a `CAI_BaseNPCTroika` word; the row binds the inherited storage
	// explicitly on the declaring class, as retail's datamap does.
	Shadow,
	// No port member. The reason is on the row and names the word's retail accessors.
	Absent,
	Count
};

// One species word, bound.
struct FElysiumNpcSpeciesWordBinding
{
	// The retail class whose datamap declares the word.
	const TCHAR* RetailClass = nullptr;
	// The offset in that class, as the datamap replay records it.
	int32 Offset = 0;
	// `Class::Member`, or null for `Absent`.
	const TCHAR* PortPath = nullptr;
	EElysiumNpcSpeciesWordHome Home = EElysiumNpcSpeciesWordHome::Absent;
	// `sizeof` the port member (the whole array for an array word), or 0 with no member.
	int32 PortSize = 0;
	const TCHAR* Note = nullptr;
};

#define ELYSIUM_NPC_SPECIES_WORD(InClass, InOffset, InType, InMember) \
	FElysiumNpcSpeciesWordBinding{ TEXT(#InClass), InOffset, TEXT(#InType "::" #InMember), \
		EElysiumNpcSpeciesWordHome::Member, static_cast<int32>(sizeof(decltype(InType::InMember))), \
		nullptr }

#define ELYSIUM_NPC_SPECIES_WORD_NOTED(InClass, InOffset, InType, InMember, InNote) \
	FElysiumNpcSpeciesWordBinding{ TEXT(#InClass), InOffset, TEXT(#InType "::" #InMember), \
		EElysiumNpcSpeciesWordHome::Member, static_cast<int32>(sizeof(decltype(InType::InMember))), \
		TEXT(InNote) }

#define ELYSIUM_NPC_SPECIES_WORD_SHADOW(InClass, InOffset, InMember) \
	FElysiumNpcSpeciesWordBinding{ TEXT(#InClass), InOffset, TEXT("FElysiumNpc::" #InMember), \
		EElysiumNpcSpeciesWordHome::Shadow, \
		static_cast<int32>(sizeof(decltype(FElysiumNpc::InMember))), nullptr }

#define ELYSIUM_NPC_SPECIES_WORD_ABSENT(InClass, InOffset, InNote) \
	FElysiumNpcSpeciesWordBinding{ TEXT(#InClass), InOffset, nullptr, \
		EElysiumNpcSpeciesWordHome::Absent, 0, TEXT(InNote) }

namespace ElysiumNpcKernelSpeciesShapeMap
{
	TArrayView<const FElysiumNpcSpeciesWordBinding> Bindings();
}
