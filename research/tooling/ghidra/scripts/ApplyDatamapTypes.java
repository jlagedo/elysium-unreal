import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.ArrayDataType;
import ghidra.program.model.data.CategoryPath;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.DataTypeConflictHandler;
import ghidra.program.model.data.DataTypeManager;
import ghidra.program.model.data.StructureDataType;
import ghidra.program.model.data.Undefined1DataType;
import ghidra.program.model.data.Undefined2DataType;
import ghidra.program.model.data.Undefined4DataType;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.listing.GhidraClass;
import ghidra.program.model.symbol.Namespace;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.program.model.symbol.SymbolTable;

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

// Turns every `datamap_t` in a module into a real Ghidra structure and hangs it on the class,
// so a decompiled member access reads as a name instead of a displacement:
// `*(int *)(this + 0x274)` becomes `this->m_iMode`.
//
// The records supply the names and the offsets. They do NOT supply a width -- VtMB's
// `typedescription_t` predates `fieldSizeInBytes`, and its `+0x24` is zero on every record --
// so the width per `fieldType` is DERIVED from the corpus rather than assumed from a Source SDK
// header: sort each class's records by offset, and where a field is followed immediately by the
// next one, `(next.offset - offset) / count` is that type's width. Across a module's datamaps
// that is thousands of samples per type; the modal width wins, and a type whose samples do not
// agree is left `undefined4` and reported. Nothing here is taken on faith from a later engine.
//
// `datamap_<Class>` and `datamap_<Class>_builder` are DumpDatamaps' labels -- run it first.
// A map whose records are assigned at static-init reads back zero from the image, so the count
// and record base are recovered from the builder's own tail (`mov [map+4],<count>;
// mov [map],<recs>`) exactly as `parse_datamap_builder` does.
//
// The class half: the pass converts each `<Class>` namespace into a GhidraClass, writes the
// structure into the ROOT category under the class's own name, and sets `__thiscall` on every
// function the class owns -- the ones NameFromStrings named into the namespace, plus every slot
// of the `vftable_<Class>` DumpRtti labelled. The decompiler then types `this` from it.
//
// The root category is not a stylistic choice. Ghidra's class-structure lookup reads there, and
// making a namespace a class mints an EMPTY same-named structure at the root; a structure filed
// anywhere else loses to that placeholder, and `this` prints as a one-byte type (`this[0xaa1]`)
// while the real 6,576-byte structure sits unused. Any surviving empty duplicate is removed.
//
// A structure is written with REPLACE_HANDLER, so re-running corrects an earlier run.
//
// Args (key=value, space separated):
//   out=<path>    report file
//   apply=1       write structures and signatures; the default reports only
//   class=<name>  restrict to one class (its own records only, base chain still flattened)
public class ApplyDatamapTypes extends GhidraScript {

    private static final int REC_SIZE = 0x2C;
    // A record's own count field: an array field states its element count here, so a width
    // derived from the offset delta has to divide by it.
    private static final int REC_COUNT = 0x10;

    private Memory memory;
    private Map<Long, Long> stores;
    private final java.util.Set<Long> registerStores = new java.util.HashSet<>();
    private int unresolvedRecords;
    private final java.util.Set<Address> claimed = new java.util.HashSet<>();
    private final Map<String, DataType> written = new LinkedHashMap<>();
    private java.util.TreeSet<Long> tableAddresses;
    private DataTypeManager types;

    public void run() throws Exception {
        Map<String, String> a = new HashMap<>();
        String[] raw = getScriptArgs();
        for (int i = 0; i < raw.length; i++) {
            int eq = raw[i].indexOf('=');
            if (eq > 0) a.put(raw[i].substring(0, eq), raw[i].substring(eq + 1));
            else if (i + 1 < raw.length) { a.put(raw[i], raw[i + 1]); i++; }
        }
        String outPath = a.getOrDefault("out", "E:/tmp/elysium_ghidra_out/datamap_types.txt");
        boolean apply = "1".equals(a.get("apply"));
        String only = a.get("class");

        memory = currentProgram.getMemory();
        types = currentProgram.getDataTypeManager();

        // 1. Every datamap the census labelled, with its records resolved.
        Map<String, Klass> classes = new TreeMap<>();
        SymbolIterator symbols = currentProgram.getSymbolTable().getSymbolIterator("datamap_*", true);
        List<Symbol> maps = new ArrayList<>();
        while (symbols.hasNext()) {
            Symbol symbol = symbols.next();
            if (!symbol.getName().endsWith("_builder")) maps.add(symbol);
        }
        if (maps.isEmpty()) {
            printerr("ApplyDatamapTypes: no datamap_* label in " + currentProgram.getName()
                    + "; run DumpDatamaps first");
            return;
        }
        for (Symbol symbol : maps) {
            // A class carrying more than one map has its labels address-qualified, and the
            // qualifier has to come off or the base chain never links: a base states the bare
            // class name, so `CBasePlayer_10583618` would never find `CBaseCombatCharacter`.
            String className = unqualify(symbol.getName().substring("datamap_".length()));
            if (only != null && !only.equals(className)) continue;
            Klass klass = classes.computeIfAbsent(className, Klass::new);
            readRecords(symbol.getAddress(), klass);
        }

        // 2. The width table, derived from the corpus rather than assumed.
        Map<Integer, Map<Integer, Integer>> samples = new TreeMap<>();
        for (Klass klass : classes.values()) {
            List<Record> ordered = new ArrayList<>(klass.records.values());
            ordered.sort(Comparator.comparingLong(r -> r.offset));
            for (int i = 0; i + 1 < ordered.size(); i++) {
                Record current = ordered.get(i), next = ordered.get(i + 1);
                long delta = next.offset - current.offset;
                if (delta <= 0 || delta > 4096 || current.count <= 0) continue;
                if (delta % current.count != 0) continue;
                long width = delta / current.count;
                if (width != 1 && width != 2 && width != 4 && width != 12 && width != 16) continue;
                samples.computeIfAbsent(current.fieldType, k -> new TreeMap<>())
                        .merge((int) width, 1, Integer::sum);
            }
        }
        Map<Integer, Integer> widths = new TreeMap<>();
        Map<Integer, String> widthEvidence = new TreeMap<>();
        for (Map.Entry<Integer, Map<Integer, Integer>> entry : samples.entrySet()) {
            int total = 0;
            for (int votes : entry.getValue().values()) total += votes;
            // The SMALLEST supported width wins, not the modal one. Padding can only enlarge
            // the gap to the next field, never shrink it, so a one-byte bool followed by three
            // bytes of alignment votes "4" as loudly as a real int does -- and a plurality then
            // types every bool in the module four bytes wide. A width is taken as real once it
            // carries a twentieth of the type's samples, which rejects the handful of deltas
            // that come from a mis-paired record without rejecting a genuine narrow field.
            int chosen = 0, chosenCount = 0;
            for (Map.Entry<Integer, Integer> vote : entry.getValue().entrySet()) {
                if (vote.getValue() * 20 >= total) {
                    chosen = vote.getKey();
                    chosenCount = vote.getValue();
                    break;                      // the samples map is sorted ascending by width
                }
            }
            boolean decided = total >= 5 && chosen != 0;
            if (decided) widths.put(entry.getKey(), chosen);
            widthEvidence.put(entry.getKey(), (decided ? "width " + chosen : "UNDECIDED")
                    + "  " + chosenCount + "/" + total + " " + entry.getValue());
        }

        new File(outPath).getParentFile().mkdirs();
        PrintWriter out = new PrintWriter(new FileWriter(outPath));
        out.println("// program: " + currentProgram.getName() + "  " + classes.size()
                + " datamap classes" + (apply ? "  [applying]" : "  [report only]"));
        out.println("\n//======== field-type widths, derived from adjacent-offset deltas ========");
        for (Map.Entry<Integer, String> entry : widthEvidence.entrySet()) {
            out.printf("type %-3d %s%n", entry.getKey(), entry.getValue());
        }

        // 3. One structure per class, its base chain flattened in.
        out.println("\n//======== classes ========");
        int built = 0, typedFunctions = 0, undecidedFields = 0;
        for (Klass klass : classes.values()) {
            Map<Long, Record> flat = new LinkedHashMap<>();
            for (Klass link = klass; link != null; link = classes.get(link.baseName)) {
                for (Record record : link.records.values()) flat.putIfAbsent(record.offset, record);
                if (link.baseName == null || link.baseName.equals(link.name)) break;
            }
            if (flat.isEmpty()) continue;

            long span = 0;
            for (Record record : flat.values()) {
                int width = widths.getOrDefault(record.fieldType, 4);
                span = Math.max(span, record.offset + (long) width * Math.max(1, record.count));
            }
            if (span <= 0 || span > 0x20000) {
                out.println(klass.name + "  implausible span " + span + " - skipped");
                continue;
            }

            // The root category, because that is where Ghidra's own class-structure lookup
            // reads: a class whose structure sits in a category of its own gets an EMPTY
            // placeholder minted beside it at the root, and `this` binds to the placeholder.
            StructureDataType structure = new StructureDataType(
                    CategoryPath.ROOT, klass.name, (int) span, types);
            // Placed in offset order, because the field that follows is the hard bound on how
            // wide this one can be: a derived width is a modal guess, the neighbour is a fact.
            List<Record> ordered = new ArrayList<>(flat.values());
            ordered.sort(Comparator.comparingLong(r -> r.offset));
            int placed = 0;
            for (int i = 0; i < ordered.size(); i++) {
                Record record = ordered.get(i);
                Integer decided = widths.get(record.fieldType);
                if (decided == null) undecidedFields++;
                int width = decided == null ? 4 : decided;
                int count = record.count;
                long room = (i + 1 < ordered.size() ? ordered.get(i + 1).offset : span)
                        - record.offset;
                if (room <= 0) continue;
                if ((long) width * count > room) {
                    long each = room % count == 0 ? room / count : 0;
                    if (each == 1 || each == 2 || each == 4) {
                        width = (int) each;
                    } else {
                        width = 1;
                        count = (int) room;
                    }
                }
                if (width > 4) { count *= width / 4; width = 4; }
                DataType element = width == 1 ? new Undefined1DataType()
                        : width == 2 ? new Undefined2DataType() : new Undefined4DataType();
                DataType dt = count > 1 ? new ArrayDataType(element, count, width) : element;
                int length = dt.getLength();
                if (record.offset + length > span) continue;
                try {
                    structure.replaceAtOffset((int) record.offset, dt, length, record.name,
                            "fieldType " + record.fieldType
                                    + (record.external.isEmpty() ? "" : "  key " + record.external));
                    placed++;
                } catch (Exception e) {
                    out.println(klass.name + "  +0x" + Long.toHexString(record.offset) + " "
                            + record.name + " not placed: " + e.getMessage());
                }
            }
            out.printf("%-40s %4d fields (%d own)  span 0x%x  base %s%n", klass.name, placed,
                    klass.records.size(), span, klass.baseName == null ? "-" : klass.baseName);
            if (!apply) continue;

            DataType stored = types.addDataType(structure, DataTypeConflictHandler.REPLACE_HANDLER);
            built++;
            reconcile(klass.name, stored, out);
            written.put(klass.name, stored);
        }

        if (apply) {
            // Named methods first, table members second: a table claim never overrides a name.
            for (String className : written.keySet()) typedFunctions += bindClass(className, out, false);
            for (String className : written.keySet()) typedFunctions += bindClass(className, out, true);
        }
        int inputs = nameInputs(classes, out, apply);

        out.println("\n// " + classes.size() + " classes, " + built + " structures written, "
                + typedFunctions + " functions given a typed this, " + undecidedFields
                + " fields of an undecided type left undefined4, "
                + unresolvedRecords + " records whose name is stored from a register, "
                + inputs + " input handlers " + (apply ? "named" : "nameable"));
        out.close();
        println("ApplyDatamapTypes: " + classes.size() + " classes, " + built + " structures, "
                + typedFunctions + " typed functions, " + inputs + " input handlers -> " + outPath);
    }

    /** Name every input handler a datamap record points at: `<Class>::Input<KeyName>`.

        This is a name the image STATES rather than one inferred from a call graph or a table
        position -- the record binds the class, the handler address and the input's own external
        name together in one 44-byte structure. It is also the surface authored maps address by
        name, so a handler that reads as `FUN_10123456` is a keyfield nobody can trace.

        A pointer-to-member-function is a plain address under MSVC single inheritance, but a slot
        commonly holds the 5-byte incremental-link thunk instead of the body, so the jump is
        followed the same way the vftable walk follows it. */
    private int nameInputs(Map<String, Klass> classes, PrintWriter out, boolean apply) {
        SymbolTable symbols = currentProgram.getSymbolTable();
        out.println("\n//======== input handlers ========");
        int named = 0, alreadyNamed = 0, missing = 0;
        for (Klass klass : classes.values()) {
            for (Record record : klass.inputs) {
                Address target;
                try { target = toAddr(record.inputFunc); } catch (Exception e) { continue; }
                ghidra.program.model.mem.MemoryBlock block = getMemoryBlock(target);
                if (block == null || !block.isExecute()) continue;
                long jumped = followJump(record.inputFunc);
                if (jumped != 0) target = toAddr(jumped);
                Function function = getFunctionAt(target);
                if (function == null) {
                    out.println(klass.name + "::Input" + sanitize(record.external) + "  " + target
                            + " is not a function - not named");
                    missing++;
                    continue;
                }
                String wanted = "Input" + sanitize(record.external);
                if (!function.getName().startsWith("FUN_")) {
                    if (!function.getName().equals(wanted)) alreadyNamed++;
                    continue;
                }
                out.printf("%-44s %s%n", klass.name + "::" + wanted, target);
                named++;
                if (!apply) continue;
                try {
                    Namespace parent = symbols.getNamespace(klass.name,
                            currentProgram.getGlobalNamespace());
                    if (parent == null) {
                        parent = symbols.createClass(currentProgram.getGlobalNamespace(),
                                klass.name, SourceType.ANALYSIS);
                    }
                    function.setParentNamespace(parent);
                    function.setName(wanted, SourceType.ANALYSIS);
                } catch (Exception e) {
                    out.println("  not named: " + e.getMessage());
                    named--;
                }
            }
        }
        out.println("// " + named + (apply ? " named" : " nameable") + ", " + alreadyNamed
                + " already carry a name, " + missing + " point at code with no function");
        return named;
    }

    /** Read one datamap's records, resolving the record base from the builder when the image
        holds zero -- which it does whenever the records are assigned at static-init. */
    private void readRecords(Address map, Klass klass) throws Exception {
        long base = map.getOffset();
        long recs = u32(base);
        long count = u32(base + 4);
        long namePtr = u32(base + 8);
        long baseMap = u32(base + 0xC);
        if (klass.baseName == null && baseMap != 0) {
            long baseNamePtr = u32(baseMap + 8);
            if (baseNamePtr != 0) klass.baseName = cstr(baseNamePtr);
        }
        if (namePtr != 0 && klass.stated == null) klass.stated = cstr(namePtr);

        // The builder's assignment wins wherever it exists. A map is only HALF static: the
        // image's own `dataNumFields` counts the statically initialized leading records, so
        // trusting it reports 12 fields for a class carrying 305.
        long[] fromBuilder = fromBuilder(base);
        if (fromBuilder != null) { recs = fromBuilder[0]; count = fromBuilder[1]; }
        if (recs == 0 || count <= 0 || count > 4096) return;

        for (long i = 0; i < count; i++) {
            long rec = recs + i * REC_SIZE;
            // A record past the statically initialized prefix reads back zero; the builder's
            // own store is where its name and offset are stated.
            long fieldType = orStored(rec, u32(rec));
            long internal = orStored(rec + 4, u32(rec + 4));
            long offset = orStored(rec + 8, u32(rec + 8));
            int elements = (int) orStored(rec + REC_COUNT, u16(rec + REC_COUNT));
            long external = orStored(rec + 0x14, u32(rec + 0x14));
            // `inputFunc` at +0x1C is the handler an input keyfield fires, and +0x14 is the
            // input's own name. Read them BEFORE the field guard below, because a
            // DEFINE_INPUTFUNC record maps to no member: its `fieldName` is null and its
            // `fieldOffset` is 0, so the guard that (correctly) refuses to place it in a
            // structure would also discard the entity's whole input surface.
            long inputFunc = orStored(rec + 0x1C, u32(rec + 0x1C));
            if (inputFunc != 0 && external != 0) {
                Record input = new Record();
                input.inputFunc = inputFunc;
                input.external = cstr(external);
                input.offset = offset;
                if (!input.external.isEmpty()) klass.inputs.add(input);
            }
            if (internal == 0 || offset == 0 || offset > 0x20000) {
                if (internal == 0 && registerStores.contains(rec + 4)) unresolvedRecords++;
                continue;
            }
            String name = cstr(internal);
            if (name.isEmpty()) continue;
            Record record = new Record();
            record.fieldType = (int) fieldType;
            record.offset = offset;
            record.count = Math.max(1, elements);
            record.name = sanitize(name);
            record.external = external == 0 ? "" : cstr(external);
            record.inputFunc = inputFunc;
            klass.records.putIfAbsent(offset, record);
        }
    }

    /** The builder's tail states both arguments: `mov [map+4],<count>; mov [map],<recs>`.

        Found by scanning the code bytes for the `C7 05 <abs> <imm32>` encoding rather than by
        following a reference. MSVC splits some per-class builders across several static-init
        fragments, and a decompiler pass then returns only the fragment it was asked for; the
        bytes state the whole assignment wherever the compiler put it. */
    private long[] fromBuilder(long map) throws Exception {
        long recs = immediateStoredTo(map);
        long count = immediateStoredTo(map + 4);
        return recs != 0 && count != 0 && count <= 4096 ? new long[] { recs, count } : null;
    }

    private long immediateStoredTo(long target) throws Exception {
        if (stores == null) loadCode();
        Long value = stores.get(target);
        return value == null ? 0 : value;
    }

    /** Every `mov [<abs>],<imm>` in the module's code, indexed by the address written.

        One scan answers two questions: what a builder assigns to a `datamap_t`, and what it
        assigns into an individual record. A record's own fields are static-initialized only for
        the leading part of the array; everything past that reads back zero from the image, and
        this index is where those names actually live. Register stores (`mov [<abs>],eax`) carry
        no immediate and are counted as unresolved rather than passed over in silence. */
    private void loadCode() throws Exception {
        stores = new HashMap<>();
        for (ghidra.program.model.mem.MemoryBlock block : memory.getBlocks()) {
            if (!block.isExecute() || !block.isInitialized()) continue;
            byte[] bytes = new byte[(int) block.getSize()];
            block.getBytes(block.getStart(), bytes);
            for (int i = 0; i + 10 <= bytes.length; i++) {
                // C7 05 <abs32> <imm32>
                if (bytes[i] == (byte) 0xC7 && bytes[i + 1] == 0x05) {
                    stores.put(le32(bytes, i + 2), le32(bytes, i + 6));
                    i += 9;
                }
                // 66 C7 05 <abs32> <imm16>
                else if (bytes[i] == (byte) 0x66 && bytes[i + 1] == (byte) 0xC7
                        && bytes[i + 2] == 0x05) {
                    stores.put(le32(bytes, i + 3), le16(bytes, i + 7));
                    i += 8;
                }
                // 89 05|0D|15|1D|25|2D|35|3D <abs32> -- a register store, so the value is not
                // in the instruction. Recorded as unresolved so a gap is visible.
                else if (bytes[i] == (byte) 0x89 && (bytes[i + 1] & 0xC7) == 0x05) {
                    registerStores.add(le32(bytes, i + 2));
                    i += 5;
                }
            }
        }
    }

    private static long le32(byte[] bytes, int at) {
        long value = 0;
        for (int b = 3; b >= 0; b--) value = (value << 8) | (bytes[at + b] & 0xFFL);
        return value;
    }

    private static long le16(byte[] bytes, int at) {
        return (bytes[at] & 0xFFL) | ((bytes[at + 1] & 0xFFL) << 8);
    }

    /** Make the namespace a class, and give every function it owns a `this` of this type. */
    /** Make the namespace a class so `this` types from the structure, and claim its methods.

        Two passes, because the evidence differs in strength. A function NameFromStrings put in
        the class namespace is that class's by name. A function reached through `vftable_<Class>`
        is that class's by table membership -- which is what DumpRtti established -- and it is
        only claimed when nothing else owns it, so a named method is never re-parented. */
    private int bindClass(String className, PrintWriter out, boolean fromTables) {
        SymbolTable symbols = currentProgram.getSymbolTable();
        Namespace namespace = symbols.getNamespace(className, currentProgram.getGlobalNamespace());
        int typed = 0;
        if (!fromTables) {
            if (namespace != null && !(namespace instanceof GhidraClass)) {
                try { namespace = symbols.convertNamespaceToClass(namespace); }
                catch (Exception e) {
                    out.println(className + "  not converted to a class: " + e.getMessage());
                }
            }
            if (namespace == null) return 0;
            for (Symbol symbol : symbols.getSymbols(namespace)) {
                Function function = getFunctionAt(symbol.getAddress());
                if (function == null || !claimed.add(function.getEntryPoint())) continue;
                if (setThisCall(function, className, out)) typed++;
            }
            return typed;
        }

        if (namespace == null) {
            // No named method reached this class, so the table is the only claim on it. The
            // namespace has to exist for `this` to type at all.
            try { namespace = symbols.createClass(currentProgram.getGlobalNamespace(),
                    className, SourceType.ANALYSIS); }
            catch (Exception e) {
                out.println(className + "  no class namespace: " + e.getMessage());
                return 0;
            }
        } else if (!(namespace instanceof GhidraClass)) {
            try { namespace = symbols.convertNamespaceToClass(namespace); }
            catch (Exception e) {
                out.println(className + "  not converted to a class: " + e.getMessage());
                return 0;
            }
        }
        for (Address slot : vftableSlots(className)) {
            Function function = getFunctionAt(slot);
            if (function == null || !claimed.add(function.getEntryPoint())) continue;
            try {
                if (function.getParentNamespace() == currentProgram.getGlobalNamespace()) {
                    function.setParentNamespace(namespace);
                }
            } catch (Exception e) {
                out.println(className + "  " + function.getEntryPoint()
                        + " kept its namespace: " + e.getMessage());
                continue;
            }
            if (setThisCall(function, className, out)) typed++;
        }
        return typed;
    }

    private boolean setThisCall(Function function, String className, PrintWriter out) {
        try {
            function.setCallingConvention("__thiscall");
            return true;
        } catch (Exception e) {
            out.println(className + "  " + function.getEntryPoint()
                    + " kept its convention: " + e.getMessage());
            return false;
        }
    }

    /** Drop the empty placeholder Ghidra creates for a class, so one structure carries the name.

        Making a namespace a class makes Ghidra mint an empty same-named structure in the root
        category, and that placeholder then competes with the one built here. The decompiler
        resolves the name to whichever it finds, so a `this` typed with the real 6,576-byte
        structure still prints `this[0xaa1]` -- the size of the empty one. */
    private void reconcile(String className, DataType keep, PrintWriter out) {
        List<DataType> named = new ArrayList<>();
        types.findDataTypes(className, named);
        for (DataType candidate : named) {
            if (candidate == keep) continue;
            // Anything else wearing this class's name is either Ghidra's empty placeholder or a
            // copy an earlier run filed in the class's own category; both shadow the real one.
            if (candidate.getLength() > 1
                    && !candidate.getCategoryPath().getPath().equals("/" + className)) continue;
            if (!types.remove(candidate)) {
                out.println(className + "  empty placeholder structure not removed at "
                        + candidate.getCategoryPath() + " - `this` may still read as one byte");
            }
        }
    }

    /** Every function a class's vftable points at, from the label DumpRtti wrote. */
    /** The address of the next `vftable_*` label at or after a table, which bounds its walk. */
    private long nextTableAfter(long table) {
        if (tableAddresses == null) {
            tableAddresses = new java.util.TreeSet<>();
            SymbolIterator all = currentProgram.getSymbolTable().getSymbolIterator("vftable_*", true);
            while (all.hasNext()) tableAddresses.add(all.next().getAddress().getOffset());
        }
        Long next = tableAddresses.higher(table);
        return next == null ? Long.MAX_VALUE : next;
    }

    /** A 5-byte `E9 <rel32>` stub's destination, or 0 when the address does not hold one. */
    private long followJump(long at) {
        try {
            if ((getByte(toAddr(at)) & 0xFF) != 0xE9) return 0;
            long delta = getInt(toAddr(at + 1));
            long destination = at + 5 + delta;
            ghidra.program.model.mem.MemoryBlock block = getMemoryBlock(toAddr(destination));
            return block != null && block.isExecute() ? destination : 0;
        } catch (Exception e) {
            return 0;
        }
    }

    private List<Address> vftableSlots(String className) {
        List<Address> slots = new ArrayList<>();
        SymbolIterator symbols = currentProgram.getSymbolTable().getSymbols("vftable_" + className);
        while (symbols.hasNext()) {
            Address table = symbols.next().getAddress();
            // Walk to the next table, not to the first gap. Stopping at the first slot that is
            // not a Function ends the walk early on any table with one, and a deep virtual then
            // never gets typed -- the player's busy predicate sits at slot 412. Stopping only
            // when the value leaves executable memory is the opposite failure: the walk runs
            // into the next class's table and claims its methods.
            long limit = nextTableAfter(table.getOffset());
            for (int i = 0; i < 600; i++) {
                if (table.getOffset() + i * 4L >= limit) break;
                long value;
                try { value = u32(table.getOffset() + i * 4L); }
                catch (Exception e) { break; }
                Address target;
                try { target = toAddr(value); } catch (Exception e) { break; }
                ghidra.program.model.mem.MemoryBlock block = getMemoryBlock(target);
                if (block == null || !block.isExecute()) break;
                // A slot usually holds the 5-byte incremental-link thunk rather than the body,
                // so a slot read at face value types the stub and leaves the method untyped.
                // Ghidra models only some of those as thunk functions -- the player's busy
                // predicate hangs off a plain `E9` at slot 412 that it did not -- so the jump is
                // followed from the bytes when the function model does not offer it.
                Function slot = getFunctionAt(target);
                if (slot != null && slot.isThunk() && slot.getThunkedFunction(true) != null) {
                    slots.add(slot.getThunkedFunction(true).getEntryPoint());
                } else {
                    long jumped = followJump(value);
                    if (jumped != 0) slots.add(toAddr(jumped));
                }
                if (slot != null) slots.add(target);
            }
        }
        return slots;
    }

    /** The image's value, or the builder's store when the image holds nothing. */
    private long orStored(long at, long imageValue) throws Exception {
        return imageValue != 0 ? imageValue : immediateStoredTo(at);
    }

    private long u32(long value) throws Exception { return memory.getInt(toAddr(value)) & 0xFFFFFFFFL; }

    private int u16(long value) throws Exception { return memory.getShort(toAddr(value)) & 0xFFFF; }

    private String cstr(long value) {
        StringBuilder text = new StringBuilder();
        for (int i = 0; i < 128; i++) {
            byte c;
            try { c = getByte(toAddr(value + i)); } catch (Exception e) { return text.toString(); }
            if (c == 0) break;
            if (c < 0x20 || c > 0x7e) return text.toString();
            text.append((char) c);
        }
        return text.toString();
    }

    /** Drop the `_<hex>` qualifier DumpDatamaps adds when one class carries several maps. */
    private static String unqualify(String name) {
        int cut = name.lastIndexOf('_');
        if (cut <= 0 || cut == name.length() - 1) return name;
        String tail = name.substring(cut + 1);
        if (tail.length() < 6) return name;
        for (int i = 0; i < tail.length(); i++) {
            if (Character.digit(tail.charAt(i), 16) < 0) return name;
        }
        return name.substring(0, cut);
    }

    private static String sanitize(String name) {
        StringBuilder text = new StringBuilder();
        for (char c : name.toCharArray()) {
            text.append(Character.isLetterOrDigit(c) || c == '_' ? c : '_');
        }
        return text.length() == 0 ? "unnamed" : text.toString();
    }

    private static final class Klass {
        final String name;
        String stated, baseName;
        final Map<Long, Record> records = new LinkedHashMap<>();
        // Input records are kept apart from the field map: they are keyed by nothing (a
        // DEFINE_INPUTFUNC has offset 0) and several of them share that key.
        final List<Record> inputs = new ArrayList<>();

        Klass(String name) { this.name = name; }
    }

    private static final class Record {
        int fieldType, count;
        long offset;
        long inputFunc;
        String name, external;
    }
}
