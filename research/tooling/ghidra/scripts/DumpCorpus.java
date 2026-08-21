import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.decompiler.parallel.DecompileConfigurer;
import ghidra.app.decompiler.parallel.DecompilerCallback;
import ghidra.app.decompiler.parallel.ParallelDecompiler;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.DataTypeComponent;
import ghidra.program.model.data.StringDataInstance;
import ghidra.program.model.data.Structure;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.util.task.TaskMonitor;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;

// Materializes a whole module: every function decompiled, with its callers, callees, signature
// and the decompiler's own warnings about it, plus every string, every named global, and every
// class structure the datamap pass wrote. Four JSONL files, which the `corpus` driver loads into
// SQLite.
//
// This is the pass that ends the slice-at-a-time loop. A question like "what else reads this
// field" or "who calls this" stops being a headless run that takes the project lock and becomes
// a local query, and a claim that NOTHING else reads a field becomes provable rather than a
// sweep repeated per question.
//
// Run it LAST. It captures names and types as they stand, so `crt_fid apply`, `DumpRtti`,
// `DumpDatamaps`, `NameFromStrings` and `ApplyDatamapTypes` all belong before it -- otherwise
// the corpus preserves `FUN_` and raw displacements and has to be regenerated.
//
// Decompilation runs through ParallelDecompiler, and functions are written in chunks so a large
// module's C never accumulates in memory.
//
// Args (key=value, space separated):
//   out=<dir>      output directory (three files are written into it)
//   chunk=<int>    functions decompiled per batch (default 2000)
//   limit=<int>    stop after this many functions (0 = all; for a smoke run)
//   nodecomp=1     skip decompilation, emit structure and reference data only
public class DumpCorpus extends GhidraScript {

    private int written;
    private int damaged;

    public void run() throws Exception {
        String outDir = null;
        int chunk = 2000, limit = 0;
        boolean decompile = true;
        // Headless splits a `key=value` argument on the '=' before the script sees it, so both
        // forms have to be accepted or every argument reads null.
        String[] raw = getScriptArgs();
        for (int i = 0; i < raw.length; i++) {
            String arg = raw[i];
            int eq = arg.indexOf('=');
            String key, value;
            if (eq > 0) {
                key = arg.substring(0, eq);
                value = arg.substring(eq + 1);
            } else if (i + 1 < raw.length) {
                key = arg;
                value = raw[++i];
            } else {
                continue;
            }
            if (key.equals("out")) outDir = value;
            else if (key.equals("chunk")) chunk = Integer.parseInt(value);
            else if (key.equals("limit")) limit = Integer.parseInt(value);
            else if (key.equals("nodecomp")) decompile = !value.equals("1");
        }
        if (outDir == null) {
            printerr("DumpCorpus: out=<dir> is required");
            return;
        }
        String module = currentProgram.getName();
        new File(outDir).mkdirs();

        List<Function> functions = new ArrayList<>();
        FunctionIterator all = currentProgram.getFunctionManager().getFunctions(true);
        while (all.hasNext()) {
            functions.add(all.next());
            if (limit > 0 && functions.size() >= limit) break;
        }

        PrintWriter out = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "functions-" + module + ".jsonl")), 1 << 20));
        if (decompile) {
            // The callback returns the function's own address beside its C. ParallelDecompiler
            // does not return results in the order the functions were handed to it, so pairing
            // by index silently files every function's body under a neighbour's name.
            DecompilerCallback<String[]> callback = new DecompilerCallback<String[]>(currentProgram,
                    new DecompileConfigurer() {
                        public void configure(DecompInterface decompiler) {
                            decompiler.toggleCCode(true);
                            decompiler.toggleSyntaxTree(true);
                            decompiler.setSimplificationStyle("decompile");
                        }
                    }) {
                public String[] process(DecompileResults results, TaskMonitor taskMonitor) {
                    if (results == null || results.getDecompiledFunction() == null
                            || results.getFunction() == null) {
                        return null;
                    }
                    return new String[] {
                        results.getFunction().getEntryPoint().toString(),
                        results.getDecompiledFunction().getC()
                    };
                }
            };
            for (int start = 0; start < functions.size(); start += chunk) {
                List<Function> batch = functions.subList(start,
                        Math.min(start + chunk, functions.size()));
                List<String[]> code = ParallelDecompiler.decompileFunctions(callback, batch, monitor);
                java.util.Map<String, String> byAddress = new java.util.HashMap<>();
                for (String[] pair : code) {
                    if (pair != null) byAddress.put(pair[0], pair[1]);
                }
                for (Function function : batch) {
                    emit(out, function, byAddress.get(function.getEntryPoint().toString()));
                }
                out.flush();
                println("DumpCorpus: " + written + "/" + functions.size() + " functions");
            }
            callback.dispose();
        } else {
            for (Function function : functions) emit(out, function, null);
        }
        out.close();

        // Strings, with the functions that reference each -- the other half of "who touches
        // this", and what a name or a message is searched by.
        PrintWriter strings = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "strings-" + module + ".jsonl")), 1 << 20));
        int stringCount = 0;
        DataIterator data = currentProgram.getListing().getDefinedData(true);
        while (data.hasNext() && !monitor.isCancelled()) {
            Data item = data.next();
            StringDataInstance instance = StringDataInstance.getStringDataInstance(item);
            if (instance == null || instance.getStringLength() <= 0) continue;
            String text = instance.getStringValue();
            if (text == null || text.isEmpty()) continue;
            Set<String> referencing = new LinkedHashSet<>();
            for (Reference reference : getReferencesTo(item.getAddress())) {
                MemoryBlock block = getMemoryBlock(reference.getFromAddress());
                if (block == null || !block.isExecute()) continue;
                Function owner = getFunctionContaining(reference.getFromAddress());
                referencing.add(owner == null ? "?" + reference.getFromAddress() : owner.getEntryPoint().toString());
            }
            strings.println("{\"a\":\"" + item.getAddress() + "\",\"t\":" + json(text)
                    + ",\"refs\":" + jsonList(referencing) + "}");
            stringCount++;
        }
        strings.close();

        // Every named datum outside the code, with the functions that reach it. A cvar object, a
        // vftable, a datamap, a global counter -- "who touches this" is the same question the
        // call graph and the field ledger answer for functions and members, and it was the one
        // part of the module with no index at all.
        PrintWriter globals = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "globals-" + module + ".jsonl")), 1 << 20));
        int globalCount = 0;
        DataIterator data2 = currentProgram.getListing().getDefinedData(true);
        while (data2.hasNext() && !monitor.isCancelled()) {
            Data item = data2.next();
            MemoryBlock block = getMemoryBlock(item.getAddress());
            if (block == null || block.isExecute()) continue;
            // Strings already have their own table, with the same reference index.
            if (StringDataInstance.getStringDataInstance(item) != null
                    && StringDataInstance.getStringDataInstance(item).getStringLength() > 0) {
                continue;
            }
            ghidra.program.model.symbol.Symbol label = getSymbolAt(item.getAddress());
            // A real label only. Ghidra mints a dynamic `DAT_<addr>` for anything referenced, and
            // those carry no information the address does not already carry -- they would bury
            // the labels that mean something (`cvar_<name>`, `vftable_<Class>`,
            // `datamap_<Class>`) under tens of thousands of rows.
            if (label == null || label.isDynamic()) continue;
            Set<String> referencing = new LinkedHashSet<>();
            for (Reference reference : getReferencesTo(item.getAddress())) {
                MemoryBlock from = getMemoryBlock(reference.getFromAddress());
                if (from == null || !from.isExecute()) continue;
                Function owner = getFunctionContaining(reference.getFromAddress());
                if (owner != null) referencing.add(owner.getEntryPoint().toString());
            }
            DataType type = item.getDataType();
            globals.println("{\"a\":\"" + item.getAddress() + "\",\"n\":" + json(label.getName())
                    + ",\"t\":" + json(type == null ? "" : type.getName())
                    + ",\"sz\":" + item.getLength()
                    + ",\"refs\":" + jsonList(referencing) + "}");
            globalCount++;
        }
        globals.close();

        // Every class structure the datamap pass wrote, component by component: the field
        // ledger's own vocabulary.
        PrintWriter fields = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(outDir, "fields-" + module + ".jsonl")), 1 << 16));
        int fieldCount = 0, structureCount = 0;
        Iterator<Structure> structures = currentProgram.getDataTypeManager()
                .getAllStructures();
        while (structures.hasNext()) {
            Structure structure = structures.next();
            // Root category only: that is where ApplyDatamapTypes writes a class structure, and
            // everything else under a category is Ghidra's own archive (PE headers, CRT types).
            if (!structure.getCategoryPath().isRoot()) continue;
            if (structure.getNumDefinedComponents() == 0) continue;
            structureCount++;
            for (DataTypeComponent component : structure.getDefinedComponents()) {
                if (component.getFieldName() == null) continue;
                DataType type = component.getDataType();
                fields.println("{\"cls\":" + json(structure.getName())
                        + ",\"off\":" + component.getOffset()
                        + ",\"name\":" + json(component.getFieldName())
                        + ",\"len\":" + component.getLength()
                        + ",\"type\":" + json(type == null ? "" : type.getName())
                        + ",\"note\":" + json(component.getComment() == null ? "" : component.getComment())
                        + "}");
                fieldCount++;
            }
        }
        fields.close();

        println("DumpCorpus: " + module + " -> " + written + " functions, " + stringCount
                + " strings, " + globalCount + " globals, " + fieldCount + " fields in "
                + structureCount + " structures, " + damaged + " damaged decompilations");
    }

    /** The decompiler's own warnings about the C it just produced.

        These are not cosmetic. `Removing unreachable block` means the C IS NOT THE WHOLE
        FUNCTION, and `Could not recover jumptable` means its control flow is wrong -- a switch
        printed as a call. Handing that back with no signal is how a reader concludes a case is
        unhandled when it is handled, so the warnings travel with the code. */
    private static List<String> warnings(String code) {
        List<String> found = new ArrayList<>();
        int at = 0;
        while ((at = code.indexOf("WARNING:", at)) >= 0) {
            int end = code.indexOf('\n', at);
            if (end < 0) end = code.length();
            String line = code.substring(at + 8, end).trim();
            if (line.endsWith("*/")) line = line.substring(0, line.length() - 2).trim();
            if (!line.isEmpty() && !found.contains(line)) found.add(line);
            at = end;
        }
        return found;
    }

    private void emit(PrintWriter out, Function function, String code) throws Exception {
        Set<String> callers = new LinkedHashSet<>(), callees = new LinkedHashSet<>();
        for (Function caller : function.getCallingFunctions(monitor)) {
            callers.add(caller.getEntryPoint().toString());
        }
        for (Function callee : function.getCalledFunctions(monitor)) {
            callees.add(callee.getEntryPoint().toString());
        }
        StringBuilder line = new StringBuilder(256);
        line.append("{\"a\":\"").append(function.getEntryPoint()).append('"')
            .append(",\"n\":").append(json(function.getName()))
            .append(",\"ns\":").append(json(function.getParentNamespace() == null
                    ? "" : function.getParentNamespace().getName()))
            .append(",\"sz\":").append(function.getBody().getNumAddresses())
            .append(",\"cc\":").append(json(function.getCallingConventionName()))
            .append(",\"thunk\":").append(function.isThunk())
            .append(",\"callers\":").append(jsonList(callers))
            .append(",\"callees\":").append(jsonList(callees));
        if (code != null) {
            List<String> found = warnings(code);
            if (!found.isEmpty()) {
                damaged++;
                line.append(",\"w\":").append(jsonList(new LinkedHashSet<>(found)));
            }
            line.append(",\"c\":").append(json(code));
        }
        line.append('}');
        out.println(line);
        written++;
    }

    private static String jsonList(Set<String> values) {
        StringBuilder text = new StringBuilder("[");
        boolean first = true;
        for (String value : values) {
            if (!first) text.append(',');
            first = false;
            text.append('"').append(value).append('"');
        }
        return text.append(']').toString();
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
