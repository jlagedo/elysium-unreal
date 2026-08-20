import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;

// Enumerates every `datamap_t` in a module, names it, chains it to its base, and finds the
// builder that fills it.
//
// `DumpDatamap` reads one map whose address you already know, and needs the record base and
// count found by hand first. This is the pass that makes those arguments unnecessary: a
// datamap identifies itself in the un-run image even though its records do not. `dataDesc`
// and `dataNumFields` are written at static-init and read back zero, but `dataClassName` and
// `baseMap` are statically initialized -- so scanning for the shape recovers every class's
// map and the whole inheritance chain without a decompiler pass.
//
// The builder is then one xref away. It ends with `mov [map+4], <count>; mov [map], <recs>`,
// and both immediates are readable straight off the instruction, which is exactly the
// `--recs` and `--count` that `parse_datamap_builder.py` asks for. The emitted command line
// per class is ready to run.
//
// Layout, as `DumpDatamap` pins it:
//   datamap_t    dataDesc@0  dataNumFields@4  dataClassName@8  baseMap@0xC
//
// Each map is labelled `datamap_<Class>` and its builder renamed `datamap_<Class>_builder`
// when it carries no name of its own, so the constructor census's `staticinit_<hex>` entries
// become readable in every later dump. A class may carry more than one datamap_t, and those
// labels are qualified by address. Re-running is idempotent.
//
// Args (key=value, space separated):
//   out=<path>   output file
//   name=0       report only; create no labels and rename nothing
public class DumpDatamaps extends GhidraScript {

    private final List<Address> blockStarts = new ArrayList<>();
    private final List<byte[]> blockBytes = new ArrayList<>();

    public void run() throws Exception {
        Map<String, String> a = new HashMap<>();
        String[] raw = getScriptArgs();
        for (int i = 0; i < raw.length; i++) {
            int eq = raw[i].indexOf('=');
            if (eq > 0) a.put(raw[i].substring(0, eq), raw[i].substring(eq + 1));
            else if (i + 1 < raw.length) { a.put(raw[i], raw[i + 1]); i++; }
        }
        String outPath = a.getOrDefault("out", "E:/tmp/elysium_ghidra_out/datamaps.txt");
        boolean rename = !"0".equals(a.get("name"));

        cacheDataBlocks();

        // 1) candidates by shape: a class name at +8, reached from code at least once
        Map<Long, Datamap> maps = new TreeMap<>();
        for (int b = 0; b < blockBytes.size(); b++) {
            byte[] bytes = blockBytes.get(b);
            long start = blockStarts.get(b).getOffset();
            for (int i = 0; i + 16 <= bytes.length; i += 4) {
                monitor.checkCancelled();
                String name = identifier(dword(bytes, i + 8));
                if (name == null) continue;
                if (!plausibleRecords(dword(bytes, i), dword(bytes, i + 4))) continue;
                long address = start + i;
                if (!reachedFromCode(address)) continue;
                maps.put(address, new Datamap(address, name, dword(bytes, i + 12),
                        dword(bytes, i), dword(bytes, i + 4)));
            }
        }

        // 2) a base pointer has to name another map, otherwise the candidate is noise.
        //    Snapshot the keys first: filtering against a map being filtered would make the
        //    result depend on iteration order.
        Set<Long> candidates = new HashSet<>(maps.keySet());
        maps.values().removeIf(map -> map.base != 0 && !candidates.contains(map.base));

        // A class can carry more than one datamap_t, so a bare class name is not a unique
        // label; qualify the duplicates by address rather than stacking identical labels.
        Map<String, Integer> nameCounts = new HashMap<>();
        for (Datamap map : maps.values()) nameCounts.merge(map.name, 1, Integer::sum);

        // 3) the builder, and the record base and count it assigns
        int renamed = 0;
        for (Datamap map : maps.values()) {
            monitor.checkCancelled();
            map.records = assignment(toAddr(map.address), map);
            map.count = assignment(toAddr(map.address + 4), map);
            if (map.records == null && staticRecords(map)) {
                map.records = map.staticDesc;
                map.count = map.staticCount;
            }
            map.label = "datamap_" + map.name
                    + (nameCounts.get(map.name) > 1 ? "_" + Long.toHexString(map.address) : "");
            if (rename) {
                // Drop unconditionally, then relabel: a record a looser earlier run labelled
                // has to lose that label when this run cannot substantiate it.
                dropStaleLabels(toAddr(map.address), map.records == null ? null : map.label);
                if (map.records != null) {
                    createLabel(toAddr(map.address), map.label, true, SourceType.ANALYSIS);
                    if (map.builder != null && isUnnamed(map.builder)) {
                        map.builder.setName(map.label + "_builder", SourceType.ANALYSIS);
                        renamed++;
                    }
                }
            }
        }

        new File(outPath).getParentFile().mkdirs();
        PrintWriter out = new PrintWriter(new FileWriter(outPath));
        out.println("// " + currentProgram.getName() + "  " + maps.size() + " datamaps, "
                + renamed + " builders renamed");
        if (maps.isEmpty()) {
            out.println("// module defines no datamap_t");
            println("DumpDatamaps: " + currentProgram.getName() + " defines no datamap");
        }

        List<Datamap> confirmed = new ArrayList<>();
        List<Datamap> unconfirmed = new ArrayList<>();
        for (Datamap map : maps.values()) {
            (map.records != null ? confirmed : unconfirmed).add(map);
        }
        confirmed.sort((x, y) -> x.name.compareTo(y.name));
        unconfirmed.sort((x, y) -> x.name.compareTo(y.name));

        out.println("\n//======== datamaps ========");
        for (Datamap map : confirmed) {
            out.printf("%n%s%n", map.name);
            out.println("  datamap " + toAddr(map.address) + "   chain " + chain(maps, map));
            out.println("  builder " + (map.builder == null ? "<records are wholly static>"
                    : map.builder.getEntryPoint() + "  " + map.builder.getName()));
            out.println("  recs " + toAddr(map.records) + "   fields " + map.count);
            out.println("  uv run elysium research parse_datamap_builder <dump> --recs "
                    + Long.toHexString(map.records) + " --count " + map.count);
        }

        // The {zero, zero, name, base} shape is not unique to datamap_t -- vgui's own chained
        // per-class maps look identical, and GameUI.dll, which defines no entity at all, is
        // made entirely of them. Without a record array these cannot be told apart, so they
        // are listed rather than labelled: naming one datamap_t would assert a type nothing
        // here establishes.
        out.println("\n//======== name-and-base records with no record array ========");
        for (Datamap map : unconfirmed) {
            out.printf("%-44s %s   chain %s%n", map.name, toAddr(map.address), chain(maps, map));
        }

        int duplicated = 0;
        for (Map.Entry<String, Integer> entry : nameCounts.entrySet()) {
            if (entry.getValue() > 1) duplicated++;
        }
        out.println("\n// " + confirmed.size() + " datamaps with a record array, "
                + unconfirmed.size() + " unidentified name-and-base records");
        out.println("// " + duplicated + " names occur more than once"
                + "; those labels are qualified by address");
        out.close();
        println("DumpDatamaps: " + confirmed.size() + " datamaps (" + unconfirmed.size()
                + " unidentified), " + renamed + " builders renamed -> " + outPath);
    }

    /** True when the image already holds the record array, so no builder ever writes it.

        The check is what separates a real datamap_t from another chained name-and-base
        record: the first `typedescription_t` has to carry a readable `internalName`. */
    private boolean staticRecords(Datamap map) {
        if (map.staticDesc == 0 || map.staticCount <= 0 || map.staticCount > 4096) return false;
        Long internalName = peek(map.staticDesc + 4);
        return internalName != null && identifier(internalName) != null;
    }

    private Long peek(long address) {
        for (int b = 0; b < blockBytes.size(); b++) {
            long index = address - blockStarts.get(b).getOffset();
            byte[] bytes = blockBytes.get(b);
            if (index < 0 || index + 4 > bytes.length) continue;
            return dword(bytes, (int) index);
        }
        return null;
    }

    /** Keep re-running idempotent: drop this script's own earlier label when it differs. */
    private void dropStaleLabels(Address address, String keep) throws Exception {
        for (Symbol symbol : currentProgram.getSymbolTable().getSymbols(address)) {
            if (symbol.getSource() == SourceType.ANALYSIS
                    && symbol.getName().startsWith("datamap_")
                    && !symbol.getName().equals(keep)) {
                removeSymbol(address, symbol.getName());
            }
        }
    }

    /** The immediate a `mov [address], imm32` stores, recording the storing function. */
    private Long assignment(Address address, Datamap map) {
        for (Reference reference : getReferencesTo(address)) {
            Instruction instruction = getInstructionAt(reference.getFromAddress());
            if (instruction == null || !instruction.getMnemonicString().equalsIgnoreCase("MOV")) continue;
            // Operand 0 must be the memory destination and operand 1 a bare immediate; that
            // excludes `mov eax, offset map`, which every reader of the map also emits.
            if (!mentions(instruction.getOpObjects(0), address.getOffset())) continue;
            Object[] source = instruction.getOpObjects(1);
            if (source.length != 1 || !(source[0] instanceof Scalar)) continue;
            Function function = getFunctionContaining(reference.getFromAddress());
            if (function != null && map.builder == null) map.builder = function;
            return ((Scalar) source[0]).getUnsignedValue();
        }
        return null;
    }

    /** VtMB writes dataDesc and dataNumFields at static-init, so they read back zero; a
        map whose records are wholly static carries a pointer and a sane field count instead. */
    private boolean plausibleRecords(long dataDesc, long fieldCount) {
        if (dataDesc == 0 && fieldCount == 0) return true;
        return fieldCount > 0 && fieldCount <= 4096 && identifiedBlock(dataDesc);
    }

    private boolean identifiedBlock(long address) {
        for (int b = 0; b < blockBytes.size(); b++) {
            long index = address - blockStarts.get(b).getOffset();
            if (index >= 0 && index < blockBytes.get(b).length) return true;
        }
        return false;
    }

    private static boolean mentions(Object[] operands, long value) {
        for (Object operand : operands) {
            if (operand instanceof Scalar && ((Scalar) operand).getUnsignedValue() == value) return true;
            if (operand instanceof Address && ((Address) operand).getOffset() == value) return true;
        }
        return false;
    }

    private String chain(Map<Long, Datamap> maps, Datamap map) {
        StringBuilder sb = new StringBuilder(map.name);
        Datamap current = map;
        int guard = 0;
        while (current.base != 0 && guard++ < 64) {
            current = maps.get(current.base);
            if (current == null) { sb.append(" -> <unresolved>"); break; }
            sb.append(" -> ").append(current.name);
        }
        return sb.toString();
    }

    private boolean reachedFromCode(long address) {
        for (Reference reference : getReferencesTo(toAddr(address))) {
            MemoryBlock block = getMemoryBlock(reference.getFromAddress());
            if (block != null && block.isExecute()) return true;
        }
        return false;
    }

    private boolean isUnnamed(Function function) {
        if (function.getSymbol() == null) return true;
        return function.getSymbol().getSource() == SourceType.DEFAULT
                || function.getName().startsWith("staticinit_");
    }

    private void cacheDataBlocks() {
        for (MemoryBlock block : currentProgram.getMemory().getBlocks()) {
            if (!block.isInitialized() || block.isExecute()) continue;
            byte[] bytes = new byte[(int) Math.min(block.getSize(), Integer.MAX_VALUE)];
            try { block.getBytes(block.getStart(), bytes); }
            catch (Exception e) { printerr("DumpDatamaps: unreadable block " + block.getName()); continue; }
            blockStarts.add(block.getStart());
            blockBytes.add(bytes);
        }
    }

    /** The C identifier at an image address, or null when it is not one. */
    private String identifier(long address) {
        for (int b = 0; b < blockBytes.size(); b++) {
            long index = address - blockStarts.get(b).getOffset();
            byte[] bytes = blockBytes.get(b);
            if (index < 0 || index >= bytes.length) continue;
            StringBuilder sb = new StringBuilder();
            for (int i = (int) index; i < bytes.length && sb.length() <= 64; i++) {
                byte c = bytes[i];
                if (c == 0) break;
                boolean ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
                        || (c >= '0' && c <= '9') || c == '_';
                if (!ok) return null;
                if (sb.length() == 0 && c >= '0' && c <= '9') return null;
                sb.append((char) c);
            }
            return sb.length() >= 3 && sb.length() <= 64 ? sb.toString() : null;
        }
        return null;
    }

    private static long dword(byte[] bytes, int index) {
        return (bytes[index] & 0xffL) | ((bytes[index + 1] & 0xffL) << 8)
                | ((bytes[index + 2] & 0xffL) << 16) | ((bytes[index + 3] & 0xffL) << 24);
    }

    private static final class Datamap {
        final long address;
        final String name;
        final long base;
        final long staticDesc;
        final long staticCount;
        Function builder;
        Long records;
        Long count;
        String label;

        Datamap(long address, String name, long base, long staticDesc, long staticCount) {
            this.address = address;
            this.name = name;
            this.base = base;
            this.staticDesc = staticDesc;
            this.staticCount = staticCount;
        }
    }
}
