"""Pre-Ghidra reconnaissance of a VtMB binary (the user's own vampire.dll, local
reference-only). Pure Python, no deps. Parses the PE, dumps named exports, recovers
C++ class names from RTTI, and buckets strings (leaked source paths, dice/WoD
resolution, disciplines, stats/skills, verbs, vdata) so a subsequent Ghidra pass can
target functions surgically instead of scanning thousands of unnamed ones.

Usage:  uv run elysium research dll_recon [path-to-dll]
Default target: <game>/Vampire/dlls/vampire.dll
"""
import os, struct, re, sys
from elysium_pipeline.formats import install

def main():
    P = sys.argv[1] if len(sys.argv) > 1 else \
        os.path.join(install.GAME, "dlls", "vampire.dll")
    d = open(P, "rb").read()

    # ---------- PE parse ----------
    e = struct.unpack_from("<I", d, 0x3C)[0]
    machine, nsect = struct.unpack_from("<HH", d, e+4)
    opt_size = struct.unpack_from("<H", d, e+20)[0]
    opt = e + 24
    image_base = struct.unpack_from("<I", d, opt+28)[0]
    exp_rva, exp_size = struct.unpack_from("<II", d, opt+96)      # data dir[0] = exports
    sec_off = opt + opt_size
    sections = []
    for i in range(nsect):
        b = sec_off + i*40
        name = d[b:b+8].rstrip(b"\x00").decode("latin-1")
        vsize, va, rsize, rptr = struct.unpack_from("<IIII", d, b+8)
        sections.append((name, va, vsize, rptr, rsize))

    def rva2off(rva):
        for name, va, vsize, rptr, rsize in sections:
            if va <= rva < va + max(vsize, rsize):
                return rptr + (rva - va)
        return None

    print("image_base=%#x  sections=%s" % (image_base, [s[0] for s in sections]))

    # ---------- exports ----------
    print("\n=== EXPORTS ===")
    if exp_rva:
        eo = rva2off(exp_rva)
        n_names = struct.unpack_from("<I", d, eo+24)[0]
        no = rva2off(struct.unpack_from("<I", d, eo+32)[0])
        names = []
        for i in range(n_names):
            o = rva2off(struct.unpack_from("<I", d, no + i*4)[0])
            names.append(d[o:d.index(b"\x00", o)].decode("latin-1"))
        print("named exports:", len(names))
        for s in names[:60]:
            print("  ", s)

    # ---------- RTTI class names ----------
    def demangle(x):
        parts = [p for p in x[4:-2].split("@") if p]
        return "::".join(reversed(parts))
    rtti = sorted(set(m.decode("latin-1") for m in re.findall(rb"\.\?A[VU][\w@?$]{2,120}@@", d)))
    classes = sorted(set(demangle(x) for x in rtti))
    KEY = ("dice","roll","combat","discipline","disc","vampire","player","npc","stat",
           "attrib","skill","blood","feed","masquer","celerity","auspex","dominate",
           "obfusc","potence","presence","protean","fortitude","thaum","dement","animal",
           "inventory","item","weapon","damage","health","lockpick","hack","save")
    print("\n=== RTTI CLASSES (%d total) ===" % len(classes))
    for c in [c for c in classes if any(k in c.lower() for k in KEY)][:120]:
        print("  ", c)

    # ---------- strings ----------
    strs = [(m.start(), m.group().decode("latin-1")) for m in re.finditer(rb"[\x20-\x7e]{5,}", d)]
    buckets = {
      "LEAKED SOURCE PATHS / asserts": lambda s: (".cpp" in s.lower() or re.search(r"[a-z]:\\", s.lower())
            or "assert" in s.lower() or "\\src\\" in s.lower()),
      "DICE / ROLL / WoD":  lambda s: re.search(r"\b(dice|roll|botch|d10|success|difficulty|soak)\b", s, re.I),
      "DISCIPLINES":        lambda s: re.search(r"celerity|auspex|dominate|obfuscat|potence|presence|protean|fortitude|thaumaturg|dementation|animalism|bloodbuff", s, re.I),
      "STATS / SKILLS":     lambda s: re.search(r"strength|dexterity|stamina|charisma|manipulation|perception|intelligence|wits|melee|firearms|dodge|stealth|security|persuasion", s, re.I),
      "VERBS":              lambda s: re.search(r"lockpick|\bhack|computers|research|unlock", s, re.I),
      "vdata files":        lambda s: re.search(r"vdata|traiteffects|clandoc", s, re.I),
    }
    for label, pred in buckets.items():
        seen = {}
        for o, s in strs:
            if pred(s) and s not in seen:
                seen[s] = o
        print("\n=== %s (%d) ===" % (label, len(seen)))
        for s, o in list(seen.items())[:35]:
            print("   @%#08x  %s" % (o, s[:96]))


if __name__ == '__main__':
    main()
