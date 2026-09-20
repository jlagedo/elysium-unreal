# -*- coding: utf-8 -*-
"""Emit the contents-signature table: one source, three consumers.

A brush is read by the four questions retail's own trace masks ask of it, and its four answers
are its SIGNATURE (`research/tooling/data/contents_masks.json`). 0018 story 3 computes that
signature once, at the seam, and nothing downstream tests a contents bit again -- so the masks,
the signature spelling and the collision profile each signature wears have to be one fact shared
by the exporter, the bake and the runtime rather than three transcriptions of it.

This generator writes:

  * `Source/ElysiumUE/Public/ElysiumContentsSignature.h` -- the enum, `ElysiumSignatureOf` and
    the profile name per signature, for the payload, the components and the tests;
  * `pipeline/src/elysium_pipeline/formats/contents_signature.py` -- the same three things for
    the exporter and the stage;
  * the marked block in `Config/DefaultEngine.ini` -- the two new channels and one named profile
    per signature. It must be a named PROFILE and not per-channel responses, because only a
    profile name survives a `.umap` save/load round trip (the note above `ElysiumPickOnly`), and
    story 3's bodies are static components saved in the level.

The profile rules, and why each is what it is:

  * `Pawn` blocks iff the NPC answer does. Unreal decides navigation relevance by
    `IsQueryCollisionEnabled() && (blocks ECC_Pawn || blocks ECC_Vehicle)`
    (`UPrimitiveComponent::IsNavigationRelevant`), so keeping NPCs on `ECC_Pawn` makes "blocks an
    NPC" and "cuts the NavMesh" the same fact, with no per-component override anywhere.
  * `Vehicle` is Ignore for every signature. VtMB ships no vehicle, and a stray Block there would
    silently make a sight-only body navigable through the same engine test.
  * `ElysiumPlayer` blocks iff the player answer does. It exists because retail asks the two
    questions separately and `ECC_Pawn` cannot answer both.
  * `ElysiumSight` blocks iff the sight answer does -- the point of the exercise: a window stops
    both pawns and no sight trace, an unsolid OPAQUE brush stops sight and neither pawn.
  * `ElysiumUse` is Ignore everywhere: the world must not steal the +use ray from the door and
    button brushes, which is what `ElysiumPickOnly` was introduced for. `ElysiumPick` likewise.
  * Collision is enabled for physics only where the brush is solid to the player; an NPC clip and
    a sight blocker are query-only, and a pedestrian volume carries no collision at all -- it is
    rows for a nav modifier, nothing more.

Usage::

    uv run elysium research gen_contents_signatures
    uv run elysium research gen_contents_signatures --check
"""
from __future__ import annotations

import argparse
import difflib
import json
from pathlib import Path

from elysium_pipeline.paths import repo_root

MASKS_JSON = ("research", "tooling", "data", "contents_masks.json")
HEADER = ("Source", "ElysiumUE", "Public", "ElysiumContentsSignature.h")
CHANNELS_HEADER = ("Source", "ElysiumUE", "Public", "ElysiumCollisionChannels.h")
PY_MIRROR = ("pipeline", "src", "elysium_pipeline", "formats", "contents_signature.py")
ENGINE_INI = ("Config", "DefaultEngine.ini")

BEGIN_MARKER = "; BEGIN GENERATED contents signatures - uv run elysium research gen_contents_signatures"
END_MARKER = "; END GENERATED contents signatures"

#: The two channels story 3 adds. `ElysiumPlayer` is an OBJECT type (the player pawn's own), the
#: others are trace channels; that is what `bTraceType` says.
NEW_CHANNELS = (
    ("ECC_GameTraceChannel3", "ElysiumSight", "ECR_Ignore", True),
    ("ECC_GameTraceChannel4", "ElysiumPlayer", "ECR_Block", False),
)

#: Every channel a generated profile lists. An omitted channel falls back to its default
#: response, which for `ElysiumUse` and `Visibility` is Block, so none may be omitted.
PROFILE_CHANNELS = (
    "WorldStatic", "WorldDynamic", "Pawn", "Visibility", "Camera", "PhysicsBody",
    "Vehicle", "Destructible", "ElysiumUse", "ElysiumPick", "ElysiumSight", "ElysiumPlayer",
)

#: The seven signatures the shipped maps carry, most brushes first. `P---` is absent because
#: PLAYERCLIP never ships without MONSTERCLIP; the generator emits all 16 combinations anyway --
#: the runtime must answer for a word it has never seen rather than fall off its table.
SHIPPED = ("PNS-", "PN--", "PN-p", "--S-", "-N-p", "-N--", "---p")

#: The profile the player's hull wears: `Pawn`, on the player's own object channel.
PLAYER_PROFILE = "ElysiumPlayerPawn"

#: Every engine profile whose response to `Pawn` is NOT the default Block, with that response.
#:
#: `UCollisionProfile::FillProfileData` gives every profile
#: `FCollisionResponseContainer::DefaultResponseContainer` and then applies its own
#: `CustomResponses`, so a channel a profile does not name falls back to that channel's
#: DefaultResponse -- Block, for `ElysiumPlayer`. Moving the player off `ECC_Pawn` would
#: therefore make every trigger, overlap volume and ragdoll BLOCK the player, because those
#: profiles only ever say "Overlap" or "Ignore" about `Pawn`.
#:
#: The rule is simply that `ElysiumPlayer` answers as `Pawn` does. Read from UE 5.8's
#: `Engine/Config/BaseEngine.ini`; the nine profiles that leave `Pawn` at the default Block need
#: no entry, and `test_gen_contents_signatures` re-reads that file, when the engine is on the
#: machine, to prove the list is still complete.
PAWN_MIRROR_PROFILES = (
    ("OverlapAll", "ECR_Overlap"),
    ("OverlapAllDynamic", "ECR_Overlap"),
    ("OverlapOnlyPawn", "ECR_Overlap"),
    ("IgnoreOnlyPawn", "ECR_Ignore"),
    ("Spectator", "ECR_Ignore"),
    ("CharacterMesh", "ECR_Ignore"),
    ("Trigger", "ECR_Overlap"),
    ("Ragdoll", "ECR_Ignore"),
    ("UI", "ECR_Overlap"),
    ("WaterBodyCollision", "ECR_Overlap"),
)


def load() -> dict:
    return json.loads(repo_root().joinpath(*MASKS_JSON).read_text(encoding="utf-8"))


def spell(bits: int, letters: str) -> str:
    """`bits` as its four-letter signature, a dash where the answer is no."""

    return "".join(letter if bits & (1 << index) else "-" for index, letter in enumerate(letters))


def profile_name(bits: int, letters: str) -> str:
    """`ElysiumSig_PNS`, `ElysiumSig_None` for the word that answers nothing."""

    kept = "".join(letter for index, letter in enumerate(letters) if bits & (1 << index))
    return f"ElysiumSig_{kept or 'None'}"


def dynamic_profile_name(bits: int, letters: str) -> str:
    """The mover twin: `ElysiumSigDyn_PNS`. A brush entity moves, so it is WorldDynamic."""

    return profile_name(bits, letters).replace("ElysiumSig_", "ElysiumSigDyn_", 1)


def responses(bits: int, letters: str) -> dict[str, str]:
    """The response this signature gives on every listed channel."""

    answer = {letter: bool(bits & (1 << index)) for index, letter in enumerate(letters)}
    player, npc, sight = answer["P"], answer["N"], answer["S"]
    block = "ECR_Block"
    ignore = "ECR_Ignore"
    return {
        # Solid-to-the-player questions: a monster clip is not a physics surface, so these
        # follow the PLAYER answer and not the NPC one.
        "WorldStatic": block if player else ignore,
        "WorldDynamic": block if player else ignore,
        "PhysicsBody": block if player else ignore,
        "Destructible": block if player else ignore,
        "Visibility": block if player else ignore,
        "Camera": block if player else ignore,
        # The two pawns, asked separately -- this is the whole point of the signature.
        "Pawn": block if npc else ignore,
        "ElysiumPlayer": block if player else ignore,
        # Sight, on its own channel, answered by brush contents at last.
        "ElysiumSight": block if sight else ignore,
        # Never the world's: the +use ray and the debug pick belong to entities and to the
        # baked render geometry (`ElysiumPickOnly`).
        "ElysiumUse": ignore,
        "ElysiumPick": ignore,
        # VtMB ships no vehicle, and a Block here would make a body navigation-relevant through
        # `IsNavigationRelevant`'s second test without blocking a single NPC.
        "Vehicle": ignore,
    }


def collision_enabled(bits: int, letters: str) -> str:
    answer = {letter: bool(bits & (1 << index)) for index, letter in enumerate(letters)}
    if answer["P"]:
        return "QueryAndPhysics"
    if answer["N"] or answer["S"]:
        return "QueryOnly"
    return "NoCollision"


def describe(bits: int, letters: str) -> str:
    answer = {letter: bool(bits & (1 << index)) for index, letter in enumerate(letters)}
    parts = [name for letter, name in
             (("P", "the player"), ("N", "an NPC"), ("S", "sight")) if answer[letter]]
    blocks = ", ".join(parts) if parts else "nothing"
    volume = "; a pedestrian volume" if answer["p"] else ""
    return f"blocks {blocks}{volume}"


def emit_header(document: dict, letters: str) -> str:
    masks = document["masks"]
    out = [
        "// Generated by `uv run elysium research gen_contents_signatures`. Do not hand-edit.",
        "//",
        "// A brush's SIGNATURE is its four answers to the retail trace masks, computed once at the",
        "// seam. Nothing downstream of the bake tests a contents bit again: the payload stores a",
        "// signature per body, the level's components wear the matching collision profile, and the",
        "// NavMesh follows from the profile because Unreal calls a body navigation-relevant exactly",
        "// when it blocks ECC_Pawn (UPrimitiveComponent::IsNavigationRelevant).",
        "//",
        "// The masks, with the retail sites that push them:",
    ]
    for row in masks:
        sites = ", ".join(row["retail"][:3])
        out.append(f"//   {row['letter']}  {row['hex']:<12} {row['question']:<38} {sites}")
    out += [
        "//",
        "// Only the BRUSH bits of each mask are kept. The entity bits (MONSTER 0x2000000) select",
        "// which entities a trace hits and are answered by those entities' own collision.",
        "",
        "#pragma once",
        "",
        "#include \"CoreMinimal.h\"",
        "",
        "/** One answer per retail mask; a brush's signature is the OR of the answers it gives. */",
        "enum class EElysiumContentsSignature : uint8",
        "{",
        "\tNone = 0,",
    ]
    for index, row in enumerate(masks):
        out.append(f"\t{row['key'].capitalize()} = 1 << {index},"
                   f"\t// {row['hex']} — {row['question']}")
    out += [
        "};",
        "ENUM_CLASS_FLAGS(EElysiumContentsSignature)",
        "",
        "namespace ElysiumContents",
        "{",
        "\t/** The four masks, in signature order. */",
    ]
    for row in masks:
        out.append(f"\tinline constexpr uint32 {row['key'].capitalize()}Mask = {row['hex']};"
                   f"\t// {row['sdkName'] or 'VtMB'}")
    out += [
        "",
        "\t/** Retail's standable normal, borrowed from the player movement layer: the AI's own",
        "\t    ground and stand tests read no surface normal, so a rasterised mesh has no retail",
        "\t    slope to reproduce and takes this one. */",
        f"\tinline constexpr float StandableNormalZ = {document['slope']['standableNormalZ']}f;"
        f"\t// {document['slope']['retail']}",
        "",
        "\t/** Step height, Source units. Every NPC walks with the base; the graph was laid down",
        f"\t    by CAI_TestHull, which steps {document['stepHeight']['graphBuildProbe']['value']:.0f},"
        " so a link can assert a rise no agent can climb. */",
        f"\tinline constexpr float StepHeightUnits = "
        f"{document['stepHeight']['base']['value']}f;"
        f"\t// {document['stepHeight']['base']['body']}",
        f"\tinline constexpr float GraphBuildStepHeightUnits = "
        f"{document['stepHeight']['graphBuildProbe']['value']}f;"
        f"\t// {document['stepHeight']['graphBuildProbe']['body']}",
        "",
        "\t/** The signature of a contents word. */",
        "\tinline EElysiumContentsSignature SignatureOf(uint32 Contents)",
        "\t{",
        "\t\tEElysiumContentsSignature Signature = EElysiumContentsSignature::None;",
    ]
    for row in masks:
        name = row["key"].capitalize()
        out += [
            f"\t\tif ((Contents & {name}Mask) != 0)",
            "\t\t{",
            f"\t\t\tSignature |= EElysiumContentsSignature::{name};",
            "\t\t}",
        ]
    out += [
        "\t\treturn Signature;",
        "\t}",
        "",
        "\t/** `PNSp`, a dash where the answer is no — the spelling the census and the pins use. */",
        "\tinline FString Spell(EElysiumContentsSignature Signature)",
        "\t{",
        "\t\tconst uint8 Bits = static_cast<uint8>(Signature);",
        f"\t\tconst TCHAR* const Letters = TEXT(\"{letters}\");",
        "\t\tFString Out;",
        f"\t\tfor (int32 Index = 0; Index < {len(letters)}; ++Index)",
        "\t\t{",
        "\t\t\tOut.AppendChar((Bits & (1 << Index)) != 0 ? Letters[Index] : TCHAR('-'));",
        "\t\t}",
        "\t\treturn Out;",
        "\t}",
        "",
        "\t/** The collision profile a body of this signature wears. Only a profile NAME survives",
        "\t    a `.umap` save/load, so the level's static bodies carry these and never a set of",
        "\t    per-channel responses. */",
        "\tinline FName ProfileName(EElysiumContentsSignature Signature)",
        "\t{",
        "\t\tswitch (static_cast<uint8>(Signature))",
        "\t\t{",
    ]
    for bits in range(1 << len(letters)):
        out.append(f"\t\tcase {bits}: return FName(TEXT(\"{profile_name(bits, letters)}\"));"
                   f"\t// {spell(bits, letters)} — {describe(bits, letters)}")
    out += [
        "\t\tdefault: return NAME_None;",
        "\t\t}",
        "\t}",
        "",
        "\t/** The profile a MOVER of this signature wears -- a solid brush entity. Same answers,",
        "\t    WorldDynamic, and Block on the +use ray and the debug pick so a door's own knob",
        "\t    stays addressable. */",
        "\tinline FName DynamicProfileName(EElysiumContentsSignature Signature)",
        "\t{",
        "\t\tswitch (static_cast<uint8>(Signature))",
        "\t\t{",
    ]
    for bits in range(1 << len(letters)):
        out.append(f"\t\tcase {bits}: return FName(TEXT(\"{dynamic_profile_name(bits, letters)}\"));"
                   f"\t// {spell(bits, letters)}")
    out += [
        "\t\tdefault: return NAME_None;",
        "\t\t}",
        "\t}",
        "",
        "\t/** True when a body of this signature cuts the NavMesh: it blocks an NPC, and nothing",
        "\t    else in the engine's relevance test is ever set (see the Vehicle note above). */",
        "\tinline bool AffectsNavigation(EElysiumContentsSignature Signature)",
        "\t{",
        "\t\treturn EnumHasAnyFlags(Signature, EElysiumContentsSignature::Npc);",
        "\t}",
        "}",
        "",
    ]
    return "\n".join(out)


def emit_channels_header() -> str:
    """The channel constants, emitted beside the ini that declares them so they cannot drift."""

    out = [
        "// Generated by `uv run elysium research gen_contents_signatures`. Do not hand-edit.",
        "//",
        "// The project's collision channels. These are emitted rather than hand-written because",
        "// the same generator writes the +DefaultChannelResponses lines in DefaultEngine.ini that",
        "// give them their names: a constant naming GameTraceChannel3 while the ini calls that",
        "// channel something else would compile, run, and answer the wrong question.",
        "//",
        "// The two pawn channels exist because retail asks two questions. 0x1400b asks whether a",
        "// brush blocks the PLAYER and 0x2400b whether it blocks an NPC; 4,145 shipped brushes",
        "// carry MONSTERCLIP and 3,775 carry PLAYERCLIP, so the answers genuinely differ. NPCs",
        "// keep ECC_Pawn because UPrimitiveComponent::IsNavigationRelevant reads that channel,",
        "// which makes \"blocks an NPC\" and \"cuts the NavMesh\" one fact instead of two.",
        "",
        "#pragma once",
        "",
        "#include \"CoreMinimal.h\"",
        "#include \"Engine/EngineTypes.h\"",
        "",
        "namespace ElysiumCollision",
        "{",
    ]
    for channel, name, default, trace in NEW_CHANNELS:
        kind = "trace channel" if trace else "object channel"
        constant = f"{name.removeprefix('Elysium')}Channel"
        out += [
            f"\t/** `{name}`, a {kind}; DefaultEngine.ini declares it with default {default}. */",
            f"\tinline constexpr ECollisionChannel {constant} = {channel};",
            "",
        ]
    out += [
        "\t/** The profile the player's hull wears: the engine's Pawn, on the player's channel. */",
        f"\tinline const FName PlayerPawnProfile = FName(TEXT(\"{PLAYER_PROFILE}\"));",
        "}",
        "",
    ]
    return "\n".join(out)


def emit_python(document: dict, letters: str) -> str:
    masks = document["masks"]
    out = [
        '"""Generated by `uv run elysium research gen_contents_signatures`. Do not hand-edit.',
        "",
        "The offline half of the contents-signature table: the same four masks, the same spelling",
        "and the same profile names the runtime's `ElysiumContentsSignature.h` carries, so the",
        "exporter and the stage partition brushes exactly as the level and the payload will.",
        '"""',
        "",
        "from __future__ import annotations",
        "",
        "#: The four masks, in signature order, with the retail sites that push them.",
        "MASKS = (",
    ]
    for row in masks:
        out.append(f"    ({row['key']!r}, {row['letter']!r}, {row['hex']}),"
                   f"  # {row['question']} — {row['retail'][0]}")
    out += [
        ")",
        "",
        "#: `PNSp`, the order a signature spells its answers in.",
        f"LETTERS = {letters!r}",
        "",
        "#: The seven signatures the shipped maps carry; a map is refused if it needs another.",
        f"SHIPPED = {SHIPPED!r}",
        "",
        "#: Retail's standable normal, borrowed from the player movement layer"
        f" ({document['slope']['retail']}).",
        f"STANDABLE_NORMAL_Z = {document['slope']['standableNormalZ']}",
        "",
        "#: Step height in Source units: what every NPC walks with, and what the graph was built",
        f"#: with ({document['stepHeight']['graphBuildProbe']['class']},"
        f" {document['stepHeight']['graphBuildProbe']['body']}).",
        f"STEP_HEIGHT_UNITS = {document['stepHeight']['base']['value']}",
        f"GRAPH_BUILD_STEP_HEIGHT_UNITS = {document['stepHeight']['graphBuildProbe']['value']}",
        "",
        "",
        "def signature_of(contents: int) -> int:",
        '    """The signature bits of a contents word."""',
        "",
        "    bits = 0",
        "    for index, (_key, _letter, mask) in enumerate(MASKS):",
        "        if contents & mask:",
        "            bits |= 1 << index",
        "    return bits",
        "",
        "",
        "def spell(bits: int) -> str:",
        '    """`PNSp`, a dash where the answer is no."""',
        "",
        "    return \"\".join(letter if bits & (1 << index) else \"-\"",
        "                   for index, letter in enumerate(LETTERS))",
        "",
        "",
        "def signature_str(contents: int) -> str:",
        "    return spell(signature_of(contents))",
        "",
        "",
        "#: The collision profile a body of each signature wears, by signature bits.",
        "PROFILE_NAMES = {",
    ]
    for bits in range(1 << len(letters)):
        out.append(f"    {bits}: {profile_name(bits, letters)!r},"
                   f"  # {spell(bits, letters)} — {describe(bits, letters)}")
    out += [
        "}",
        "",
        "",
        "def profile_name(bits: int) -> str:",
        "    return PROFILE_NAMES[bits]",
        "",
        "",
        "def affects_navigation(bits: int) -> bool:",
        '    """A body cuts the NavMesh exactly when it blocks an NPC."""',
        "",
        "    return bool(bits & (1 << 1))",
        "",
    ]
    return "\n".join(out)


def emit_ini_block(letters: str) -> str:
    out = [
        BEGIN_MARKER,
        "; The two channels story 3 adds, and one named profile per contents signature. A brush's",
        "; signature is its four answers to the retail masks; the profile is how those answers",
        "; reach Unreal. It must be a profile and not per-channel responses set on the component,",
        "; because only a profile name survives the .umap save/load round trip -- and these bodies",
        "; are static components saved in the level.",
        "; Pawn is the NPC's channel and ElysiumPlayer the player's, so the two questions retail",
        "; asks separately are answered separately. Navigation follows for free: Unreal calls a",
        "; body navigation-relevant exactly when it blocks ECC_Pawn (or ECC_Vehicle, which nothing",
        "; here ever does), so 'blocks an NPC' and 'cuts the mesh' are one fact.",
    ]
    for channel, name, default, trace in NEW_CHANNELS:
        kind = "trace channel" if trace else "object channel (the player pawn's own type)"
        out.append(f"; {name}: {kind}")
        out.append(f"+DefaultChannelResponses=(Channel={channel},DefaultResponse={default},"
                   f"bTraceType={'True' if trace else 'False'},bStaticObject=False,Name=\"{name}\")")
    for bits in range(1 << len(letters)):
        mark = spell(bits, letters)
        if mark not in SHIPPED:
            continue
        table = responses(bits, letters)
        custom = ",".join(f"(Channel=\"{channel}\",Response={table[channel]})"
                          for channel in PROFILE_CHANNELS)
        out.append(f"+Profiles=(Name=\"{profile_name(bits, letters)}\","
                   f"CollisionEnabled={collision_enabled(bits, letters)},"
                   f"ObjectTypeName=\"WorldStatic\",CustomResponses=({custom}),"
                   f"HelpMessage=\"Contents signature {mark}: {describe(bits, letters)}.\","
                   f"bCanModify=False)")

    out += [
        "; The mover twin of each signature: a SOLID brush entity wears one of these. Same answers",
        "; to the four retail questions -- all three movement masks carry MOVEABLE and so does the",
        "; sight mask, which is how a door stops both pawns and sight while a glass func_brush stops",
        "; neither pawn's sight -- but WorldDynamic, because a mover moves, and Block on the +use",
        "; ray and the debug pick, because a door's own knob has to stay addressable.",
    ]
    for bits in range(1 << len(letters)):
        mark = spell(bits, letters)
        if mark not in SHIPPED:
            continue
        table = dict(responses(bits, letters))
        table["ElysiumUse"] = "ECR_Block"
        table["ElysiumPick"] = "ECR_Block"
        custom = ",".join(f"(Channel=\"{channel}\",Response={table[channel]})"
                          for channel in PROFILE_CHANNELS)
        out.append(f"+Profiles=(Name=\"{dynamic_profile_name(bits, letters)}\","
                   f"CollisionEnabled={collision_enabled(bits, letters)},"
                   f"ObjectTypeName=\"WorldDynamic\",CustomResponses=({custom}),"
                   f"HelpMessage=\"Contents signature {mark}, on a mover: "
                   f"{describe(bits, letters)}.\",bCanModify=False)")

    out += [
        "; The player's hull: the engine's Pawn profile on the player's own object channel, so a",
        "; brush can stop one pawn and not the other. NPCs stay on ECC_Pawn, which is what keeps",
        "; 'blocks an NPC' and 'cuts the NavMesh' one fact.",
        "; Visibility=Ignore because the engine's own Pawn profile sets it: this profile mirrors",
        "; Pawn, and an unnamed channel falls back to Block, which would make the player an",
        "; occluder for every ECC_Visibility trace that Pawn is invisible to.",
        f"+Profiles=(Name=\"{PLAYER_PROFILE}\",CollisionEnabled=QueryAndPhysics,"
        f"ObjectTypeName=\"ElysiumPlayer\","
        f"CustomResponses=((Channel=\"Visibility\",Response=ECR_Ignore),"
        f"(Channel=\"ElysiumUse\",Response=ECR_Ignore),"
        f"(Channel=\"ElysiumPick\",Response=ECR_Ignore),"
        f"(Channel=\"ElysiumSight\",Response=ECR_Ignore)),"
        f"HelpMessage=\"The player's hull: Pawn, on the player's own object channel.\","
        f"bCanModify=False)",
        "; The player does NOT occlude sight, and that is a NAMED DIVERGENCE rather than retail's",
        "; arrangement (corrected 2026-09-20; this note claimed the opposite and was wrong).",
        "; Retail's one sight mask is 0x2804091, which carries MONSTER 0x2000000, so in retail a",
        "; body standing between two points DOES break the line. 0x804091 is that mask's brush",
        "; half -- all a world body can answer, and all the contents signature is computed from.",
        "; Job 7 wires the character arm; until then this channel is answered by brush contents,",
        "; movers and props alone.",
        "",
        "; ElysiumPlayer answers exactly as Pawn does in every engine profile. A profile that does",
        "; not name a channel falls back to that channel's DefaultResponse, which is Block here, so",
        "; without these a trigger volume would stop the player dead instead of overlapping them.",
        "; The nine engine profiles that leave Pawn at the default Block need no entry.",
    ]
    for name, response in PAWN_MIRROR_PROFILES:
        out.append(f"+EditProfiles=(Name=\"{name}\","
                   f"CustomResponses=((Channel=\"ElysiumPlayer\",Response={response})))")
    out.append(END_MARKER)
    return "\n".join(out)


def splice_ini(current: str, block: str) -> str:
    """Replace the marked block, or append it to the CollisionProfile section."""

    begin = current.find(BEGIN_MARKER)
    if begin != -1:
        end = current.index(END_MARKER, begin) + len(END_MARKER)
        return current[:begin] + block + current[end:]
    anchor = current.index("[/Script/Engine.CollisionProfile]")
    section_end = current.find("\n\n", anchor)
    return current[:section_end] + "\n" + block + current[section_end:]


def _emit(output: Path, text: str, check: bool) -> int:
    if check:
        if not output.is_file():
            print(f"CHECK FAILED: {output} is missing")
            return 1
        with open(output, encoding="utf-8", newline="") as handle:
            current = handle.read().replace("\r\n", "\n")
        if current != text:
            print(f"CHECK FAILED: {output} is stale; regenerate it")
            diff = list(difflib.unified_diff(current.splitlines(), text.splitlines(),
                                             "committed", "generated", lineterm=""))
            for line in diff[:40]:
                print("  " + line)
            if len(diff) > 40:
                print(f"  … {len(diff) - 40} more diff lines")
            return 1
        print(f"check: {output.name} matches the masks")
        return 0
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(text, encoding="utf-8", newline="\n")
    print(f"wrote {output}")
    return 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(prog="gen_contents_signatures", add_help=False)
    parser.add_argument("--check", action="store_true",
                        help="verify the committed files match; write nothing")
    args = parser.parse_args(argv)

    document = load()
    for row in document["masks"]:
        if row["mask"] != int(row["hex"], 16):
            print(f"FAIL: mask {row['key']}: {row['mask']} != {row['hex']}")
            return 1
    letters = "".join(row["letter"] for row in document["masks"])

    repo = repo_root()
    status = _emit(repo.joinpath(*HEADER), emit_header(document, letters), args.check)
    status |= _emit(repo.joinpath(*CHANNELS_HEADER), emit_channels_header(), args.check)
    status |= _emit(repo.joinpath(*PY_MIRROR), emit_python(document, letters), args.check)

    ini_path = repo.joinpath(*ENGINE_INI)
    with open(ini_path, encoding="utf-8", newline="") as handle:
        current = handle.read()
    spliced = splice_ini(current.replace("\r\n", "\n"), emit_ini_block(letters))
    status |= _emit(ini_path, spliced, args.check)
    return status


if __name__ == "__main__":
    raise SystemExit(main())
