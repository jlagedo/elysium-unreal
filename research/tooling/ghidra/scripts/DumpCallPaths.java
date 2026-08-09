import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

// Finds bounded direct-call paths from seed functions to named/addressed sinks.
// This is useful when a virtual handler delegates its activity decision to a
// class helper: dumping only the virtual body would otherwise hide the actual
// SetIdealActivity/SetActivity call.
//
// Args (key=value, space separated):
//   funcs=<hex+...>    seed entry points (plus survives headless tokenization)
//   sinks=<text+...>   substrings matched against function name and entry address
//   slots=<hex+...>    indirect-call displacement text to match (for example 4d8)
//   depth=<int>        maximum direct-call depth (default 4)
//   out=<path>         output file
public class DumpCallPaths extends GhidraScript {

    private static class State {
        Function function;
        List<Function> path;

        State(Function function, List<Function> path) {
            this.function = function;
            this.path = path;
        }
    }

    public void run() throws Exception {
        Map<String, String> args = new HashMap<>();
        String[] raw = getScriptArgs();
        for (int i = 0; i < raw.length; i++) {
            int eq = raw[i].indexOf('=');
            if (eq > 0) args.put(raw[i].substring(0, eq), raw[i].substring(eq + 1));
            else if (i + 1 < raw.length) { args.put(raw[i], raw[i + 1]); i++; }
        }

        String outPath = args.getOrDefault("out", "E:/tmp/elysium_ghidra_out/call_paths.txt");
        int maxDepth = Integer.parseInt(args.getOrDefault("depth", "4"));
        String[] sinks = split(args.getOrDefault("sinks", "10272650+10289ee0+102725d0+10272490"));
        String[] slots = split(args.getOrDefault("slots", ""));
        new File(outPath).getParentFile().mkdirs();

        try (PrintWriter out = new PrintWriter(new FileWriter(outPath))) {
            for (String value : split(args.getOrDefault("funcs", ""))) {
                Address address = toAddr(value);
                Function seed = getFunctionAt(address);
                if (seed == null) seed = getFunctionContaining(address);
                out.println("\n//======== seed " + address + " " +
                        (seed == null ? "<none>" : seed.getName(true)) + " ========");
                if (seed == null) continue;

                ArrayDeque<State> work = new ArrayDeque<>();
                List<Function> first = new ArrayList<>();
                first.add(seed);
                work.add(new State(seed, first));
                Set<String> expanded = new HashSet<>();
                int paths = 0;

                while (!work.isEmpty()) {
                    State state = work.removeFirst();
                    Function function = state.function;
                    String identity = function.getEntryPoint() + ":" + (state.path.size() - 1);
                    if (!expanded.add(identity)) continue;

                    if (state.path.size() > 1 &&
                            (matches(function, sinks) || matchesSlot(function, slots))) {
                        out.println(formatPath(state.path));
                        paths++;
                        continue;
                    }
                    if (state.path.size() - 1 >= maxDepth || function.isExternal()) continue;

                    for (Function callee : function.getCalledFunctions(monitor)) {
                        if (callee == null || callee.isExternal() || contains(state.path, callee)) continue;
                        List<Function> next = new ArrayList<>(state.path);
                        next.add(callee);
                        work.addLast(new State(callee, next));
                    }
                }
                out.println("// " + paths + " sink paths");
            }
        }
        println("DumpCallPaths: wrote " + outPath);
    }

    private boolean matches(Function function, String[] sinks) {
        String candidate = function.getName(true) + "@" + function.getEntryPoint();
        for (String sink : sinks) if (!sink.isEmpty() && candidate.contains(sink)) return true;
        return false;
    }

    private boolean matchesSlot(Function function, String[] slots) {
        if (slots.length == 0 || function.getBody() == null) return false;
        InstructionIterator instructions = currentProgram.getListing().getInstructions(function.getBody(), true);
        while (instructions.hasNext()) {
            Instruction instruction = instructions.next();
            if (!instruction.getMnemonicString().startsWith("CALL")) continue;
            String rendered = instruction.toString().toLowerCase();
            for (String slot : slots) {
                String normalized = slot.toLowerCase().replace("0x", "");
                if (!normalized.isEmpty() &&
                        (rendered.contains("0x" + normalized) || rendered.contains("+" + normalized))) {
                    return true;
                }
            }
        }
        return false;
    }

    private boolean contains(List<Function> path, Function candidate) {
        for (Function function : path) if (function.equals(candidate)) return true;
        return false;
    }

    private String formatPath(List<Function> path) {
        List<String> items = new ArrayList<>();
        for (Function function : path) {
            items.add(function.getEntryPoint() + " " + function.getName(true));
        }
        return String.join(" -> ", items);
    }

    private static String[] split(String value) {
        if (value == null || value.isEmpty()) return new String[0];
        return value.split("[;,+]");
    }
}
