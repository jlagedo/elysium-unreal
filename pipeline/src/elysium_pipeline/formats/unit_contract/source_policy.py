"""Named source-selection exceptions; ordinary units always use the install winner.

The index retains and hashes both the winner and its shadowed sources. An exception selects
one of those measured candidates; it never exempts a source from byte/origin verification.
"""
RETAIL_PARTICLE_KEYS = frozenset({"waterbigsplash_emitter"})
RETAIL_SPLASH_POLICY = "retail-water-splash-water-architecture-N3"


def unit_source_policy(asset, path):
    if asset == "vtmb:particle:waterbigsplash_emitter" and path == "particles/waterbigsplash_emitter.txt":
        return RETAIL_SPLASH_POLICY
    return None


def selected_source(member, asset):
    if unit_source_policy(asset, member.get("path")):
        for candidate in [member, *member.get("shadowed", ())]:
            if candidate.get("origin", {}).get("kind") == "vpk":
                return candidate
    return member
