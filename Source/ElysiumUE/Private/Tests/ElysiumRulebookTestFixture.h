#pragma once

// The shared mechanism behind every fabricated-rulebook fixture in this test tier:
// `ElysiumSheetRules::BindTables` takes a `const FBoundTables&` of bare pointers as the
// process-wide fallback every combat leaf reads when no rulebook subsystem is in reach (the
// readers are plain-C++ leaves holding no session pointer, the same reason `ElysiumRng`'s streams
// are module-static). Binding a table is one call; unbinding it on every exit path — including an
// early `return` out of a test case — is what makes this a type rather than a helper function.
//
// What each suite BINDS is not shared, and this type does not try to make it so:
// `ElysiumDisciplineTests.cpp`, `ElysiumLawTests.cpp`, `ElysiumStealthTests.cpp`,
// `ElysiumWeaponTests.cpp` and `ElysiumMeleeTestHelpers.h` each fabricate their own table CONTENTS
// and pass them to `Bind`. A suite's fabricated numbers are what its assertions are stated
// against, so merging the contents across suites would silently change what those assertions
// prove; only the bind/unbind lifetime is common ground.

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Substrate/ElysiumSheetMath.h"

namespace ElysiumRulebookTest
{
	// Binds whatever `FBoundTables` the owning fixture passes to `Bind`, for as long as this
	// object lives, and unconditionally unbinds on destruction.
	//
	// Held BY VALUE inside the owning fixture (or as a member declared after every other member
	// whose destruction should still see the bound tables — a fixture that also owns a
	// `FElysiumEntityWorld` needs the unbind to happen before that world tears down, exactly as it
	// did when each suite unbound by hand in its own destructor).
	struct FScopedRulebookBinding
	{
		FScopedRulebookBinding() = default;
		~FScopedRulebookBinding() { ElysiumSheetRules::BindTables(ElysiumSheetRules::FBoundTables()); }

		FScopedRulebookBinding(const FScopedRulebookBinding&) = delete;
		FScopedRulebookBinding& operator=(const FScopedRulebookBinding&) = delete;

		// Re-point the fallback at Bound. Exposed as a method rather than only a constructor
		// argument because a fixture that edits its own tables after construction, or that wants a
		// different SET of members bound, has not moved the tables — only their contents or
		// membership — and needs a way to say so without a second RAII object.
		static void Bind(const ElysiumSheetRules::FBoundTables& Bound)
		{
			ElysiumSheetRules::BindTables(Bound);
		}
	};
}

#endif   // WITH_DEV_AUTOMATION_TESTS
