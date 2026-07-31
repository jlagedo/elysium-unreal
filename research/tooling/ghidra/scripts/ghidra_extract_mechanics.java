// Ghidra headless GhidraScript: extract VtMB combat / discipline / feeding mechanics
// from the user's own vampire.dll (local, reference-only).
//
// Usage:
//   analyzeHeadless <projDir> vtmb -process vampire.dll -noanalysis \
//       -scriptPath E:\dev\elysium-unreal\research\tooling\ghidra\scripts \
//       -postScript ghidra_extract_mechanics.java <out_dir>
//
// Strategy: each anchor string is a printf/ConVar/ConCommand that lives inside (or
// beside) a mechanics function. Walk xrefs string -> function, decompile the
// function and its direct callees, dump C pseudocode grouped by anchor.

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.address.Address;
import ghidra.program.model.symbol.Reference;
import ghidra.util.task.ConsoleTaskMonitor;

import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

public class ghidra_extract_mechanics extends GhidraScript {

    static final String[] ANCHORS = {
        // core dice / resolution
        "Processes a Vampire Dice Roll",
        "Dice Results:",
        "Warning: Dice roll count out of bounds",
        "Lists the available Vampire Botch Tables",
        "Defense Roll successes = %d",
        // per-check-type resolution (debug ConVars beside each function)
        "Set > 0 to override dice rolled for all ranged combat checks.",
        "Set > 0 to override dice rolled for all defense checks.",
        "Set > 0 to override dice rolled for all brawl checks.",
        "Set > 0 to override dice rolled for all melee combat checks.",
        "Set > 0 to override dice rolled for all soak checks.",
        // vampire systems
        "Computing frenzy check for %s of %d difficulty.",
        "Feed attempt!",
        "Feed succeeded without a roll",
        "Target is Stealth Killable",
        "Testing Fortitude Soak Particles",
    };

    private DecompInterface deco;
    private ConsoleTaskMonitor mon;

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String outdir = args.length > 0 ? args[0] : ".";
        new File(outdir).mkdirs();

        mon = new ConsoleTaskMonitor();
        deco = new DecompInterface();
        deco.openProgram(currentProgram);

        // index defined strings once
        List<Object[]> strings = new ArrayList<>();
        DataIterator it = currentProgram.getListing().getDefinedData(true);
        while (it.hasNext()) {
            Data d = it.next();
            Object v = d.getValue();
            if (v instanceof String) {
                strings.add(new Object[]{ (String) v, d.getAddress() });
            }
        }
        println("[extract] indexed " + strings.size() + " defined strings");

        Set<Address> emitted = new HashSet<>();
        StringBuilder sb = new StringBuilder();

        for (String anchor : ANCHORS) {
            for (Object[] pair : strings) {
                String txt = (String) pair[0];
                if (!txt.contains(anchor)) continue;
                Address sAddr = (Address) pair[1];
                for (Reference r : getReferencesTo(sAddr)) {
                    Function f = getFunctionContaining(r.getFromAddress());
                    if (f == null || emitted.contains(f.getEntryPoint())) continue;
                    emitted.add(f.getEntryPoint());
                    String c = dc(f);
                    if (c == null) continue;
                    sb.append("// ===== anchor: ").append(anchor).append("\n")
                      .append("// FUNC ").append(f.getName()).append(" @ ")
                      .append(f.getEntryPoint()).append("\n").append(c).append("\n\n");
                    // one level of callees to capture roll-math helpers
                    for (Function callee : f.getCalledFunctions(mon)) {
                        if (emitted.contains(callee.getEntryPoint())) continue;
                        emitted.add(callee.getEntryPoint());
                        String cc = dc(callee);
                        if (cc == null) continue;
                        sb.append("//   -- callee of ").append(f.getName()).append(": ")
                          .append(callee.getName()).append(" @ ")
                          .append(callee.getEntryPoint()).append("\n").append(cc).append("\n\n");
                    }
                }
            }
        }

        File out = new File(outdir, "vtmb_mechanics.c");
        PrintWriter pw = new PrintWriter(out);
        pw.print(sb.toString());
        pw.close();
        println("[extract] wrote " + emitted.size() + " functions to " + out.getAbsolutePath());
    }

    private String dc(Function f) {
        DecompileResults r = deco.decompileFunction(f, 90, mon);
        if (r != null && r.decompileCompleted()) {
            return r.getDecompiledFunction().getC();
        }
        return null;
    }
}
