import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSetView;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.Namespace;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.SourceType;

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

// Splits a function Ghidra ran into its neighbours back into the functions it is made of.
//
// A missed `noreturn`, a bad jump table or an unrecognised tail call lets a function's body swallow
// everything after it. The result reads as one enormous function: vampire.dll's worst is 68,194
// bytes against a p99.9 of 3,096, and it has no caller because nothing calls the middle of a
// blob. Everything inside is then invisible -- the corpus files it under one address, `callers`
// answers for the wrong function, and the code that was actually wanted has no name of its own.
//
// The seeds are the same conservative two MakeFuncs uses, restricted to the body being split:
// the target of a CALL made from OUTSIDE the body (something calls it, so it is a function), and
// the first instruction after a run of int3/nop padding (the linker aligned it, so it starts
// something). Nothing is disassembled that was not already; only ownership changes.
//
// Splitting requires removing the oversized function first, because Ghidra will not create a
// function inside another's body. The entry point's own name and namespace are captured and put
// back, so a named blob does not lose its identity to the repair.
//
// Args (key=value, space separated):
//   out=<path>     report file
//   min=<int>      consider functions at least this many bytes (default 4096)
//   funcs=<hex;..> split exactly these instead of scanning by size
//   apply=1        perform the split; the default reports only
public class SplitFuncs extends GhidraScript {

    public void run() throws Exception {
        Map<String, String> a = new HashMap<>();
        String[] raw = getScriptArgs();
        for (int i = 0; i < raw.length; i++) {
            int eq = raw[i].indexOf('=');
            if (eq > 0) a.put(raw[i].substring(0, eq), raw[i].substring(eq + 1));
            else if (i + 1 < raw.length) { a.put(raw[i], raw[i + 1]); i++; }
        }
        String outPath = a.get("out");
        if (outPath == null) {
            printerr("SplitFuncs: out=<path> is required");
            return;
        }
        boolean apply = "1".equals(a.get("apply"));
        int min = Integer.parseInt(a.getOrDefault("min", "4096"));
        int padNeeded = Integer.parseInt(a.getOrDefault("pad", "3"));

        List<Function> targets = new ArrayList<>();
        String explicit = a.get("funcs");
        if (explicit != null && !explicit.isEmpty()) {
            for (String text : explicit.split("[;,]")) {
                if (text.isEmpty()) continue;
                Function function = getFunctionContaining(toAddr(text));
                if (function == null) {
                    printerr("SplitFuncs: no function at " + text);
                    continue;
                }
                targets.add(function);
            }
        } else {
            FunctionIterator all = currentProgram.getFunctionManager().getFunctions(true);
            while (all.hasNext()) {
                Function function = all.next();
                if (function.getBody().getNumAddresses() >= min) targets.add(function);
            }
        }

        new File(outPath).getParentFile().mkdirs();
        PrintWriter out = new PrintWriter(new FileWriter(outPath));
        out.println("// " + currentProgram.getName() + "  " + targets.size()
                + " oversized functions" + (apply ? "  [applying]" : "  [report only]"));

        Listing listing = currentProgram.getListing();
        int split = 0, created = 0, left = 0;
        for (Function function : targets) {
            if (monitor.isCancelled()) break;
            Address entry = function.getEntryPoint();
            long size = function.getBody().getNumAddresses();
            Set<Address> seeds = seeds(listing, function, padNeeded);
            seeds.remove(entry);
            out.printf("%s  %-46s %7d bytes  %d interior seeds%n", entry,
                    function.getName(true), size, seeds.size());
            for (Address seed : seeds) out.println("    seed " + seed);
            if (seeds.isEmpty()) {
                // Genuinely one large function, or a blob whose interior nothing calls and the
                // linker did not pad. Either way there is no evidence to split on, and inventing
                // a boundary would be worse than the oversized body.
                left++;
                continue;
            }
            if (!apply) continue;

            String name = function.getName();
            Namespace namespace = function.getParentNamespace();
            SourceType source = function.getSymbol().getSource();
            try {
                removeFunction(function);
            } catch (Exception e) {
                out.println("    not removed: " + e.getMessage());
                left++;
                continue;
            }
            Function head = null;
            try {
                head = createFunction(entry, null);
            } catch (Exception e) {
                out.println("    entry not recreated: " + e.getMessage());
            }
            if (head == null) {
                printerr("SplitFuncs: " + entry + " lost its function and could not be recreated");
                out.println("    ENTRY LOST - " + entry + " is no longer a function");
                continue;
            }
            if (!name.startsWith("FUN_")) {
                try {
                    head.setParentNamespace(namespace);
                    head.setName(name, source);
                } catch (Exception e) {
                    out.println("    name not restored: " + e.getMessage());
                }
            }
            for (Address seed : seeds) {
                if (getFunctionAt(seed) != null) continue;
                if (getInstructionAt(seed) == null) continue;
                try {
                    if (createFunction(seed, null) != null) created++;
                } catch (Exception e) {
                    out.println("    " + seed + " not created: " + e.getMessage());
                }
            }
            split++;
            out.printf("    -> %s now %d bytes, %d functions created%n", entry,
                    head.getBody().getNumAddresses(), seeds.size());
        }

        String summary = targets.size() + " oversized functions, " + split + " split, "
                + created + " functions created, " + left + " left whole for want of evidence";
        out.println("\n// " + summary);
        out.close();
        println("SplitFuncs: " + summary);
        println("SplitFuncs: " + outPath);
    }

    /** Interior addresses that something else already treats as a function start. */
    private Set<Address> seeds(Listing listing, Function function, int padNeeded) {
        AddressSetView body = function.getBody();
        Set<Address> seeds = new LinkedHashSet<>();

        // Called from outside the body: a call target is a function by definition, and a call
        // from within could be a genuine internal loop the compiler shaped like one.
        for (Address address : body.getAddresses(true)) {
            for (Reference reference : getReferencesTo(address)) {
                if (!reference.getReferenceType().isCall()) continue;
                if (body.contains(reference.getFromAddress())) continue;
                seeds.add(address);
                break;
            }
        }

        int padRun = 0;
        Address previousEnd = null;
        for (Instruction instruction : listing.getInstructions(body, true)) {
            if (previousEnd != null
                    && instruction.getAddress().subtract(previousEnd) >= padNeeded) {
                seeds.add(instruction.getAddress());
            }
            previousEnd = instruction.getMaxAddress().add(1);
            String mnemonic = instruction.getMnemonicString();
            if (mnemonic.equals("INT3") || mnemonic.equals("NOP")) {
                padRun += instruction.getLength();
                continue;
            }
            if (padRun >= padNeeded) seeds.add(instruction.getAddress());
            padRun = 0;
        }
        return seeds;
    }
}
