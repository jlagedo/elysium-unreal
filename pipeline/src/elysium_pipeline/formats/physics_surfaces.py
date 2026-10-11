"""Authored surface semantics, independent of backend row layout or allocator identity."""
from copy import deepcopy
import math
import re
import struct

FLOAT_KEYS = ("friction", "elasticity", "density", "thickness", "dampening",
              "audioreflectivity", "audiohardnessfactor", "audioroughnessfactor",
              "scraperoughthreshold", "impacthardthreshold", "audiohardminvelocity",
              "maxspeedfactor", "jumpfactor")
WEAPONS = ("metal", "wood", "blade", "fist", "bullet", "claw")
OUTCOMES = ("norm", "soak", "crit")
SOUND_KEYS = ("stepleft", "stepright", "impact", "impactsoft", "impacthard", "scrape",
              "scrapesmooth", "scraperough", "bulletimpact", "rolling") + tuple(
                  f"{weapon}_{outcome}_impact" for weapon in WEAPONS for outcome in OUTCOMES)
_LOWER = str.maketrans("ABCDEFGHIJKLMNOPQRSTUVWXYZ", "abcdefghijklmnopqrstuvwxyz")
_NUMBER = re.compile(r"[+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[dDeE][+-]?[0-9]+)?")


def _float(text):
    match = _NUMBER.match(text.lstrip(" \t\n\v\f\r"))
    value = float(match[0].replace("d", "e").replace("D", "e")) if match else 0.
    try:
        value = struct.unpack("<f", struct.pack("<f", value))[0]
    except OverflowError:
        value = math.copysign(math.inf, value)
    return value if math.isfinite(value) else ("-inf" if value < 0 else "inf")


def _integer(text):
    match = re.match(r"[+-]?[0-9]+", text.lstrip(" \t\n\v\f\r"))
    return int(match[0]) & 0xffffffff if match else 0


class _Reader:
    """26001850's live table B and signed-byte whitespace; no KeyValues tree flattening."""
    def __init__(self, data):
        self.text = data.decode("latin-1").split("\0", 1)[0]
        self.position = 0
        self.last = ""

    def token(self):
        if self.position is None:
            return self.last
        text, length = self.text, len(self.text)
        position = self.position
        while True:
            while position < length and (ord(text[position]) <= 32 or ord(text[position]) >= 128):
                position += 1
            if position >= length:
                self.position, self.last = None, ""
                return self.last
            if text.startswith("//", position):
                end = text.find("\n", position + 2)
                position = length if end < 0 else end
                continue
            if text.startswith("/*", position):
                end = text.find("*/", position + 2)
                position = length if end < 0 else end + 2
                continue
            break
        if text[position] == '"':
            position += 1
            value = []
            while position < length:
                if text.startswith('\\"', position):
                    value.append('"'); position += 2
                elif text[position] == '"':
                    position += 1; break
                else:
                    value.append(text[position]); position += 1
            token = "".join(value)
        elif text[position] in "{}()':":
            token = text[position]; position += 1
        else:
            start = position
            while position < length and 32 < ord(text[position]) < 128 and text[position] not in "{}()':":
                position += 1
            token = text[start:position]
        self.position, self.last = position, token
        return token

    def pair(self):
        key = self.token().translate(_LOWER)
        return key, "" if key == "}" else self.token().translate(_LOWER)


def decode_semantics(data: bytes) -> dict:
    """One first-load table: copied defaults/base position, last fields, first entry, shadow."""
    reader, rows = _Reader(data), []

    def lookup(name):
        if name.translate(_LOWER) == "$material_index_shadow":
            return rows[0] if rows else None  # shadow index starts at0 until first parse completes.
        return next((row for row in rows if row["name"] == name), None)

    def parameters():
        return {"physics": {key: 0. for key in FLOAT_KEYS},
                "sounds": {key: "" for key in SOUND_KEYS}, "gamematerial": 0, "climbable": 0}

    while reader.position is not None:
        name, opening = reader.pair()
        if opening != "{":
            continue
        default = lookup("default")
        row = deepcopy(default) if default else parameters()
        row["name"] = name
        while reader.position is not None:
            key, value = reader.pair()
            if key == "}":
                if name.translate(_LOWER) != "$material_index_shadow" and lookup(name) is None:
                    rows.append(row)
                break
            if key == "base":
                base = lookup(value)
                if base:
                    row = deepcopy(base); row["name"] = name
            elif key in FLOAT_KEYS:
                row["physics"][key] = _float(value)
            elif key in SOUND_KEYS:
                row["sounds"][key] = value
            elif key == "climbable":
                row[key] = _integer(value) & 255
            elif key == "gamematerial":
                row[key] = ((ord(value) - 32 if "a" <= value <= "z" else ord(value))
                            if len(value) == 1 and not "0" <= value <= "9"
                            else _integer(value)) & 65535
    default = lookup("default")
    shadow = deepcopy(default) if default else parameters()
    shadow["name"] = "$MATERIAL_INDEX_SHADOW"
    shadow["physics"].update(friction=_float("0.8"), elasticity=0.)
    rows.append(shadow)
    return {"schemaVersion": "1.0.0", "rows": rows}
