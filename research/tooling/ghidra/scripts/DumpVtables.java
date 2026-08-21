import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Namespace;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.program.model.symbol.SymbolTable;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.TreeSet;

// Every class vftable, slot by slot, as JSONL -- the ground truth that lets the corpus
// reconstruct the call graph's missing half.
//
// VtMB dispatches its game logic virtually, so a direct-call graph is blind exactly where the
// rules live: 79.6% of vampire.dll's class methods have no direct caller at all. The decompiler
// prints those sites as `(**(code **)(*(int *)this + 0xNN))()`, which names a SLOT and not a
// function. This pass supplies the other half of that join -- (class, slot) -> implementation --
// so `corpus build` can turn the slot back into an edge.
//
// The walk is the same one ApplyDatamapTypes uses to type `this`, with the same two hard-won
// bounds. Both scripts carry it because a Ghidra script compiles alone; a change to one belongs
// in the other.
//
// Also names unambiguous slots when `name=1`. The rule is deliberately narrow: a function is
// named `<Class>::vfunc<slot>` only when it appears in exactly ONE class's vftable and is still
// `FUN_*`. An inherited implementation shows up in every derived class's table, and naming it
// after whichever one the iteration reached first would be a claim the image does not make; those
// are counted as ambiguous and left alone.
//
// Args (key=value, space separated):
//   out=<path>    JSONL output file (one row per slot)
//   name=1        name unambiguous slots (default: report only)
//   create=1      make a function at a slot target that has none
//   report=<path> human-readable summary
public class DumpVtables extends GhidraScript {

    private TreeSet<Long> tableAddresses;

    public void run() throws Exception {
        Map<String, String> a = new HashMap<>();
        String[] raw = getScriptArgs();
        // Headless splits `key=value` on the '=' before the script sees it, so both forms have
        // to be accepted or every argument reads null.
        for (int i = 0; i < raw.length; i++) {
            int eq = raw[i].indexOf('=');
            if (eq > 0) a.put(raw[i].substring(0, eq), raw[i].substring(eq + 1));
            else if (i + 1 < raw.length) { a.put(raw[i], raw[i + 1]); i++; }
        }
        String outPath = a.get("out");
        if (outPath == null) {
            printerr("DumpVtables: out=<path> is required");
            return;
        }
        boolean name = "1".equals(a.get("name"));
        boolean create = "1".equals(a.get("create"));
        String reportPath = a.getOrDefault("report", outPath + ".txt");
        String module = currentProgram.getName();

        SymbolTable symbols = currentProgram.getSymbolTable();
        List<Symbol> tables = new ArrayList<>();
        SymbolIterator all = symbols.getSymbolIterator("vftable_*", true);
        while (all.hasNext()) tables.add(all.next());
        if (tables.isEmpty()) {
            printerr("DumpVtables: no vftable_* label in " + module + "; run DumpRtti first");
            return;
        }

        new File(outPath).getParentFile().mkdirs();
        PrintWriter out = new PrintWriter(new BufferedWriter(new FileWriter(outPath), 1 << 20));
        PrintWriter report = new PrintWriter(new FileWriter(reportPath));
        report.println("// " + module + "  " + tables.size() + " vftables"
                + (name ? "  [naming]" : "  [report only]"));

        // Which classes each function is a slot of. A function in more than one is inherited,
        // and inheritance is exactly what makes a name unsafe to assign.
        Map<Address, Set<String>> owners = new HashMap<>();
        Map<Address, Integer> slotOf = new HashMap<>();
        Map<String, Integer> perClass = new TreeMap<>();
        int rows = 0, unresolved = 0, made = 0;

        for (Symbol table : tables) {
            String label = table.getName().substring("vftable_".length());
            // A secondary base's table is labelled `_at<offset>`; its slots are that subobject's
            // and reach the real methods through adjustor thunks, so the subobject offset is
            // carried rather than folded into the class name.
            int subobject = 0;
            int at = label.lastIndexOf("_at");
            if (at > 0) {
                try {
                    subobject = Integer.parseInt(label.substring(at + 3));
                    label = label.substring(0, at);
                } catch (NumberFormatException ignored) {
                    // not a subobject suffix; the class name legitimately ends in "_at..."
                }
            }
            long base = table.getAddress().getOffset();
            long limit = nextTableAfter(base);
            for (int slot = 0; slot < 600; slot++) {
                long entry = base + slot * 4L;
                if (entry >= limit) break;
                long value;
                try { value = u32(entry); } catch (Exception e) { break; }
                Address target;
                try { target = toAddr(value); } catch (Exception e) { break; }
                MemoryBlock block = getMemoryBlock(target);
                if (block == null || !block.isExecute()) break;

                // A slot IS the evidence that its target is a function, and it is evidence
                // nothing else carries: code reached only through a data pointer is never a CALL
                // target, so neither the analyzer nor MakeFuncs' seeds ever reach it. Those slots
                // otherwise resolve to nothing at all -- 2,663 of client.dll's, 1,963 of
                // GameUI's -- and each one is both a function missing from the corpus and a
                // virtual edge that cannot be joined.
                if (create && getFunctionAt(target) == null) {
                    try {
                        if (getInstructionAt(target) == null) disassemble(target);
                        if (getInstructionAt(target) != null && createFunction(target, null) != null) {
                            made++;
                        }
                    } catch (Exception e) {
                        report.println("slot " + slot + " of " + label + " at " + target
                                + " not made a function: " + e.getMessage());
                    }
                }

                Address resolved = resolve(target);
                if (resolved == null) { unresolved++; continue; }
                boolean thunk = !resolved.equals(target);
                out.println("{\"t\":\"" + table.getAddress() + "\",\"cls\":" + json(label)
                        + ",\"sub\":" + subobject
                        + ",\"slot\":" + slot
                        + ",\"f\":\"" + resolved + "\""
                        + ",\"raw\":\"" + target + "\""
                        + ",\"thunk\":" + thunk + "}");
                rows++;
                perClass.merge(label, 1, Integer::sum);
                owners.computeIfAbsent(resolved, k -> new LinkedHashSet<>()).add(label);
                slotOf.putIfAbsent(resolved, slot);
            }
        }
        out.close();

        int named = 0, ambiguous = 0, alreadyNamed = 0;
        if (name) {
            for (Map.Entry<Address, Set<String>> entry : owners.entrySet()) {
                Function function = getFunctionAt(entry.getKey());
                if (function == null) continue;
                if (!function.getName().startsWith("FUN_")) { alreadyNamed++; continue; }
                if (entry.getValue().size() != 1) { ambiguous++; continue; }
                String className = entry.getValue().iterator().next();
                try {
                    Namespace parent = symbols.getNamespace(className,
                            currentProgram.getGlobalNamespace());
                    if (parent == null) {
                        parent = symbols.createClass(currentProgram.getGlobalNamespace(),
                                className, SourceType.ANALYSIS);
                    }
                    function.setParentNamespace(parent);
                    function.setName("vfunc" + slotOf.get(entry.getKey()), SourceType.ANALYSIS);
                    named++;
                } catch (Exception e) {
                    report.println(className + "  " + entry.getKey()
                            + " not named: " + e.getMessage());
                }
            }
        } else {
            for (Map.Entry<Address, Set<String>> entry : owners.entrySet()) {
                Function function = getFunctionAt(entry.getKey());
                if (function == null) continue;
                if (!function.getName().startsWith("FUN_")) alreadyNamed++;
                else if (entry.getValue().size() != 1) ambiguous++;
                else named++;
            }
        }

        report.println("\n//======== slots per class ========");
        for (Map.Entry<String, Integer> entry : perClass.entrySet()) {
            report.printf("%-52s %4d%n", entry.getKey(), entry.getValue());
        }
        String summary = tables.size() + " vftables, " + rows + " slots, " + owners.size()
                + " distinct implementations; " + named + (name ? " named" : " nameable")
                + ", " + ambiguous + " ambiguous (in more than one class's table), "
                + alreadyNamed + " already named, " + made + " functions made from a slot, " + unresolved + " slots whose target"
                + " resolved to nothing";
        report.println("\n// " + summary);
        report.close();
        println("DumpVtables: " + module + " -> " + summary);
        println("DumpVtables: " + outPath);
    }

    /** The function a slot actually reaches: past a Ghidra thunk, past a raw `E9` stub, or the
        target itself.

        A slot usually holds the 5-byte incremental-link thunk rather than the body, so a slot
        read at face value records the stub instead of the method. Ghidra models only some of
        those as thunk functions -- the player's busy predicate hangs off a plain `E9` at slot
        412 that it did not -- so the jump is followed from the bytes when the function model
        does not offer it. */
    private Address resolve(Address target) {
        Function function = getFunctionAt(target);
        if (function != null && function.isThunk() && function.getThunkedFunction(true) != null) {
            return function.getThunkedFunction(true).getEntryPoint();
        }
        long jumped = followJump(target.getOffset());
        if (jumped != 0) return toAddr(jumped);
        return function != null ? target : null;
    }

    /** A 5-byte `E9 <rel32>` stub's destination, or 0 when the address does not hold one. */
    private long followJump(long at) {
        try {
            if ((getByte(toAddr(at)) & 0xFF) != 0xE9) return 0;
            long delta = getInt(toAddr(at + 1));
            long destination = at + 5 + delta;
            MemoryBlock block = getMemoryBlock(toAddr(destination));
            return block != null && block.isExecute() ? destination : 0;
        } catch (Exception e) {
            return 0;
        }
    }

    /** The address of the next `vftable_*` label after a table, which bounds its walk.

        Walking to the next table rather than to the first gap is load-bearing in both
        directions. Stopping at the first slot that is not a Function ends the walk early on any
        table that has one, and a deep virtual then never appears -- the player's busy predicate
        sits at slot 412. Stopping only when the value leaves executable memory is the opposite
        failure: the walk runs into the next class's table and claims its methods. */
    private long nextTableAfter(long table) {
        if (tableAddresses == null) {
            tableAddresses = new TreeSet<>();
            SymbolIterator all = currentProgram.getSymbolTable()
                    .getSymbolIterator("vftable_*", true);
            while (all.hasNext()) tableAddresses.add(all.next().getAddress().getOffset());
        }
        Long next = tableAddresses.higher(table);
        return next == null ? Long.MAX_VALUE : next;
    }

    private long u32(long value) throws Exception {
        return currentProgram.getMemory().getInt(toAddr(value)) & 0xFFFFFFFFL;
    }

    private static String json(String value) {
        StringBuilder text = new StringBuilder(value.length() + 16).append('"');
        for (int i = 0; i < value.length(); i++) {
            char c = value.charAt(i);
            switch (c) {
                case '"': text.append("\\\""); break;
                case '\\': text.append("\\\\"); break;
                case '\n': text.append("\\n"); break;
                case '\r': text.append("\\r"); break;
                case '\t': text.append("\\t"); break;
                default:
                    if (c < 0x20 || c > 0x7e) text.append(String.format("\\u%04x", (int) c));
                    else text.append(c);
            }
        }
        return text.append('"').toString();
    }
}
