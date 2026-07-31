# Ghidra headless post-script: extract VtMB combat / discipline / feeding mechanics
# from the user's own vampire.dll (local, reference-only).
#
# Usage (from a Ghidra install's support/ dir):
#   analyzeHeadless <projDir> vtmb -import "E:\dev_game\...\Vampire\dlls\vampire.dll" \
#       -postScript ghidra_extract_mechanics.py <out_dir>
#
# Strategy: each anchor string below is a printf/ConVar/ConCommand that sits inside
# (or beside) a mechanics function. Walk xrefs string -> function, decompile the
# function and its direct callees, dump C pseudocode grouped by anchor.
#
# Runs under Ghidra's Jython (Python 2.7) with the flat API.

from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
import os

# Anchors found by the pre-Ghidra recon (offsets are file offsets in the recon dump;
# here we match by content so they resolve to whatever address analysis assigns).
ANCHORS = [
    # --- core dice / resolution ---
    "Processes a Vampire Dice Roll",
    "Dice Results:",
    "Warning: Dice roll count out of bounds",
    "Botch",
    "Lists the available Vampire Botch Tables",
    # --- per-check-type resolution (each is a debug ConVar next to its function) ---
    "Set > 0 to override dice rolled for all ranged combat checks.",
    "Set > 0 to override dice rolled for all defense checks.",
    "Set > 0 to override dice rolled for all brawl checks.",
    "Set > 0 to override dice rolled for all melee combat checks.",
    "Set > 0 to override dice rolled for all soak checks.",
    "Defense Roll successes = %d",
    # --- vampire systems ---
    "Computing frenzy check for %s of %d difficulty.",
    "Feed attempt!",
    "Feed succeeded without a roll",
    "Target is Stealth Killable",
    "Testing Fortitude Soak Particles",
]

args = getScriptArgs()
outdir = args[0] if args else "."
if not os.path.isdir(outdir):
    os.makedirs(outdir)

prog = currentProgram
listing = prog.getListing()
monitor = ConsoleTaskMonitor()

deco = DecompInterface()
deco.openProgram(prog)

def decompile(func):
    if func is None:
        return None
    r = deco.decompileFunction(func, 90, monitor)
    if r and r.decompileCompleted():
        return r.getDecompiledFunction().getC()
    return None

# index defined strings once
string_addrs = []  # (text, address)
for data in listing.getDefinedData(True):
    if data.hasStringValue():
        try:
            string_addrs.append((str(data.getValue()), data.getAddress()))
        except:
            pass

emitted = set()
blocks = []
for anchor in ANCHORS:
    matches = [addr for (txt, addr) in string_addrs if anchor in txt]
    for addr in matches:
        for ref in getReferencesTo(addr):
            func = getFunctionContaining(ref.getFromAddress())
            if func is None:
                continue
            ep = func.getEntryPoint()
            if ep in emitted:
                continue
            emitted.add(ep)
            c = decompile(func)
            if not c:
                continue
            blocks.append("// ===== anchor: %r\n// FUNC %s @ %s\n%s" %
                          (anchor, func.getName(), ep, c))
            # one level of callees to capture the roll-math helpers
            for callee in func.getCalledFunctions(monitor):
                cep = callee.getEntryPoint()
                if cep in emitted:
                    continue
                emitted.add(cep)
                cc = decompile(callee)
                if cc:
                    blocks.append("//   -- callee of %s: %s @ %s\n%s" %
                                  (func.getName(), callee.getName(), cep, cc))

outpath = os.path.join(outdir, "vtmb_mechanics.c")
f = open(outpath, "w")
f.write("\n\n".join(blocks))
f.close()
print("[extract] wrote %d functions to %s" % (len(emitted), outpath))
