"""The Source symbol tables the six sound-script tables spend on `channel`, `soundlevel`,
`pitch`, `volume` and the DSP processor grammar.

`seam_map_sound_script.md` says a table's vocabulary is "the ones the file's own header comment
lists". Read literally per file that is incomplete: `CHAN_*` is documented in `sounds.txt`'s
header but not in `game_sounds_surfaceproperties.txt`'s, and neither file's header documents
`PITCH_*` or `VOL_NORM` even though both use them (`game_sounds_surfaceproperties.txt`'s own
header comment states `VOL_NORM 1.0f` without ever using it; `sounds.txt` uses `VOL_NORM` without
documenting it). This module collects the symbol tables once, across the family the six tables
form, sourced from whichever header actually states them; `PITCH_*` is undocumented in every
`scripts/*.txt` header shipped and is carried here from the Source SDK reference
(`public/soundflags.h`) instead.
"""

from __future__ import annotations

#: `sounds.txt`'s own header comment; `game_sounds_surfaceproperties.txt` does not restate it.
CHANNELS: dict[str, int] = {
    "CHAN_AUTO": 0,
    "CHAN_WEAPON": 1,
    "CHAN_VOICE": 2,
    "CHAN_ITEM": 3,
    "CHAN_BODY": 4,
    "CHAN_STREAM": 5,
    "CHAN_STATIC": 6,
}

#: Both files' header comment, identically.
SOUND_LEVELS: dict[str, int] = {
    "SNDLVL_NONE": 0,
    "SNDLVL_25dB": 25,
    "SNDLVL_30dB": 30,
    "SNDLVL_35dB": 35,
    "SNDLVL_40dB": 40,
    "SNDLVL_45dB": 45,
    "SNDLVL_50dB": 50,
    "SNDLVL_55dB": 55,
    "SNDLVL_IDLE": 60,
    "SNDLVL_TALKING": 60,
    "SNDLVL_60dB": 60,
    "SNDLVL_65dB": 65,
    "SNDLVL_STATIC": 66,
    "SNDLVL_70dB": 70,
    "SNDLVL_NORM": 75,
    "SNDLVL_75dB": 75,
    "SNDLVL_80dB": 80,
    "SNDLVL_85dB": 85,
    "SNDLVL_90dB": 90,
    "SNDLVL_95dB": 95,
    "SNDLVL_100dB": 100,
    "SNDLVL_105dB": 105,
    "SNDLVL_120dB": 120,
    "SNDLVL_130dB": 130,
    "SNDLVL_GUNFIRE": 140,
    "SNDLVL_140dB": 140,
    "SNDLVL_150dB": 150,
}

#: Undocumented in any shipped `scripts/*.txt` header; carried from the Source SDK reference
#: (`public/soundflags.h`), which is the [SDK] evidence tier `docs/vtmb/audio_pipeline.md` names.
PITCHES: dict[str, int] = {
    "PITCH_NORM": 100,
    "PITCH_LOW": 95,
    "PITCH_HIGH": 120,
}

#: `game_sounds_surfaceproperties.txt`'s own header comment (`// VOL_NORM 1.0f`); the spec's
#: vocabulary line for `volume` names only "a number or range" and omits this symbol, which
#: `sounds.txt` uses eight times.
VOLUME_SYMBOLS: dict[str, float] = {"VOL_NORM": 1.0}

#: `dsp_presets.txt`'s own header comment.
PROCESSOR_TYPES: dict[str, int] = {
    "NULL": 0,
    "DLY": 1,
    "RVA": 2,
    "FLT": 3,
    "CRS": 4,
    "PTC": 5,
    "ENV": 6,
    "LFO": 7,
    "EFO": 8,
    "MDY": 9,
    "DFR": 10,
    "AMP": 11,
}

FILTER_TYPES: dict[str, int] = {"LP": 0, "HP": 1, "BP": 2}
FILTER_QUALITY: dict[str, int] = {"LO": 0, "MED": 1, "HI": 2, "VHI": 3}
DELAY_TYPES: dict[str, int] = {
    "PLAIN": 0,
    "ALLPASS": 1,
    "LOWPASS": 2,
    "DLINEAR": 3,
    "FLINEAR": 4,
    "LOWPASS_4TAP": 5,
    "PLAIN_4TAP": 6,
}
LFO_TYPES: dict[str, int] = {
    "SIN": 0, "TRI": 1, "SQR": 2, "SAW": 3, "RND": 4, "LOG_IN": 5, "LOG_OUT": 6,
    "LIN_IN": 7, "LIN_OUT": 8,
}
ENVELOPE_TYPES: dict[str, int] = {"LIN": 0, "EXP": 1}
PRESET_CONFIGURATIONS: dict[str, int] = {
    "SIMPLE": 0,
    "LINEAR": 1,
    "PARALLEL2": 5,
    "PARALLEL4": 6,
    "PARALLEL5": 7,
    "FEEDBACK": 8,
    "FEEDBACK3": 9,
    "FEEDBACK4": 10,
    "MOD": 11,
    "MOD2": 12,
    "MOD3": 13,
}

#: One processor type's declared parameter names, in the order `dsp_presets.txt`'s own
#: "description of parameters for all processor types" header documents them. An instance
#: carrying a different token count than `len(...)` here is `parameter-count-mismatch`, not a
#: reason to invent a wider table: several presets append extra columns (a filter tail on `DLY`,
#: a `gain` on `DFR`) that no header paragraph documents.
PROCESSOR_PARAMETERS: dict[str, tuple[str, ...]] = {
    "FLT": ("ftype", "cutoff", "qwidth", "quality", "gain"),
    "DLY": ("dtype", "delay", "feedback", "gain"),
    "RVA": (
        "sizeMax", "sizeMin", "numDelays", "feedback", "gain", "fparallel", "cutoff",
        "fmoddly", "rate", "width", "depth", "height", "fbWidth", "fbDepth", "fbHeight", "ftaps",
    ),
    "DFR": ("size", "numDelays", "feedback"),
    "AMP": (
        "gain", "vthresh", "distmix", "vfeed", "modrate", "moddepth", "modglide", "rand",
    ),
    "LFO": ("wavtype", "rate", "foneshot", "gain"),
    "PTC": ("pitch", "timeslice", "xfade"),
    "ENV": (
        "etype", "amp1", "amp2", "amp3", "attack", "decay", "sustain", "release", "exp",
    ),
    "MDY": ("dtype", "delay", "feedback", "gain", "modrate", "moddepth", "modglide"),
    "CRS": ("lfowav", "rate", "depth", "mix"),
    "EFO": ("threshold", "attack", "decay", "exp"),
}

#: `NULL`/`0` is the documented pass-through: "must be 0", with no parameter list of its own.
NULL_PROCESSOR = "NULL"

#: Which processor-parameter field draws from a symbol table rather than a plain number, keyed
#: by `(processor type, PROCESSOR_PARAMETERS field name)`. A token that names a field here and
#: does not parse as a number is resolved against the named table before it is given up as a
#: `typedUnidentified` row -- the published evidence that a table entry went unresolved.
PROCESSOR_FIELD_SYMBOLS: dict[tuple[str, str], dict[str, int]] = {
    ("FLT", "ftype"): FILTER_TYPES,
    ("FLT", "quality"): FILTER_QUALITY,
    ("DLY", "dtype"): DELAY_TYPES,
    ("MDY", "dtype"): DELAY_TYPES,
    ("LFO", "wavtype"): LFO_TYPES,
    ("CRS", "lfowav"): LFO_TYPES,
    ("ENV", "etype"): ENVELOPE_TYPES,
}

#: The preset header's own fixed fields, in file order, before its processor list.
PRESET_FIELDS = ("id", "configuration", "mixMin", "mixMax", "duration", "fadeTime", "dbMin", "dbMixdrop")


def resolve_symbol(token: str, table: dict[str, int]) -> int | None:
    """Case-insensitive symbol lookup; `None` when the token names no symbol in `table`."""

    upper = token.strip().upper()
    for name, value in table.items():
        if name.upper() == upper:
            return value
    return None


def resolve_symbol_key(token: str, table: dict[str, int]) -> str | None:
    """The table's own canonical spelling for `token`, or `None` when it names no symbol.

    A record publishes this rather than an upper-cased copy of the source token, because the
    tables spell some symbols in mixed case (`SNDLVL_60dB`) and an upper-cased copy is a spelling
    neither the source nor the table uses.
    """

    upper = token.strip().upper()
    for name in table:
        if name.upper() == upper:
            return name
    return None
