import ghidra.app.cmd.function.ApplyFunctionSignatureCmd;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.ArrayDataType;
import ghidra.program.model.data.CategoryPath;
import ghidra.program.model.data.CharDataType;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.DoubleDataType;
import ghidra.program.model.data.FloatDataType;
import ghidra.program.model.data.FunctionDefinitionDataType;
import ghidra.program.model.data.IntegerDataType;
import ghidra.program.model.data.LongDataType;
import ghidra.program.model.data.LongLongDataType;
import ghidra.program.model.data.ParameterDefinition;
import ghidra.program.model.data.ParameterDefinitionImpl;
import ghidra.program.model.data.PointerDataType;
import ghidra.program.model.data.StructureDataType;
import ghidra.program.model.data.Undefined4DataType;
import ghidra.program.model.data.UnsignedIntegerDataType;
import ghidra.program.model.data.UnsignedLongDataType;
import ghidra.program.model.data.VoidDataType;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.SourceType;

import java.io.File;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Set;

// Apply CPython 2.1.2's own declared C API to the functions this module imports from
// `vampire_python21.dll`, and make the strings those calls pass readable.
//
// Ghidra imports a DLL's functions with no signature at all, and for the Python bridge that is
// not cosmetic. `PyArg_ParseTuple(PyObject *, char *, ...)` carries the FORMAT STRING that
// states a script-API function's argument list; with the parameter untyped the operand stays
// `&DAT_10590cb8` and the signature is unreadable. Typing the import is what turns those into
// `"Osfffff"`.
//
// Two passes, in order:
//
//   1. Signatures. Each prototype comes from `pyapi.py`, which reads them out of the released
//      2.1.2 headers -- so nothing here is guessed. A name the file does not carry is counted
//      and skipped rather than invented.
//
//   2. Strings. Ghidra's string analyzer has a minimum length (5 by default) and every one of
//      these formats is shorter -- `"OO"`, `"Os"`, `"i"` -- so the analyzer never made them.
//      For each call site of a function that takes a `char *`, the preceding pushes are scanned
//      and any that points at a NUL-terminated printable run is made a string. Creating a
//      string where a valid C string already is cannot be wrong, and the bound (16 instructions,
//      64 bytes) keeps it from wandering.
//
// Args (key=value, space separated):
//   api=<path>    the prototype file written by `pyapi.py`   (required)
//   apply=1       write to the project (default: report only)
//   report=<path> human-readable summary
public class ApplyPythonApi extends GhidraScript {

    private static final CategoryPath CATEGORY = new CategoryPath("/python21");

    /** The CPython structures this pass built, by name, so a `PyObject *` parameter resolves. */
    private final Map<String, DataType> defined = new LinkedHashMap<>();

    private static final int LOOKBACK = 16;
    private static final int MAX_STRING = 64;

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
        String apiPath = a.get("api");
        if (apiPath == null) {
            printerr("ApplyPythonApi: api=<path> is required (write it with `corpus pyapi`)");
            return;
        }
        boolean apply = "1".equals(a.get("apply"));
        String reportPath = a.getOrDefault("report", apiPath + "." + currentProgram.getName()
                + ".txt");

        Map<String, String> prototypes = new HashMap<>();
        List<String> structures = new ArrayList<>();
        for (String line : Files.readAllLines(new File(apiPath).toPath(),
                StandardCharsets.UTF_8)) {
            line = line.trim();
            if (line.isEmpty() || line.startsWith("//")) continue;
            // A struct DEFINITION, not a prototype that happens to return one: the extractor
            // reduces `struct node *` to a bare `struct *`, so the brace is what tells them apart.
            if (line.startsWith("struct ") && line.indexOf('{') > 0) {
                structures.add(line);
                continue;
            }
            int open = line.indexOf('(');
            if (open < 0) continue;
            String head = line.substring(0, open).trim();
            int space = Math.max(head.lastIndexOf(' '), head.lastIndexOf('*'));
            if (space < 0) continue;
            prototypes.put(head.substring(space + 1).trim(), line);
        }
        if (prototypes.isEmpty()) {
            printerr("ApplyPythonApi: no prototypes in " + apiPath);
            return;
        }

        PrintWriter report = new PrintWriter(reportPath, "UTF-8");
        report.println("// " + currentProgram.getName() + "  " + prototypes.size()
                + " declared prototypes" + (apply ? "  [applying]" : "  [report only]"));

        // ---- pass 0: the structures -------------------------------------------------------
        // Two sweeps, because these types reference each other: `PyObject` names `PyTypeObject`
        // in its own second word, and `PyTypeObject`'s head IS a `PyObject`. Every name is
        // registered empty first so a member can resolve to a structure defined further down.
        Map<String, StructureDataType> building = new LinkedHashMap<>();
        for (String line : structures) {
            String name = line.substring("struct ".length(), line.indexOf('{')).trim();
            StructureDataType structure = new StructureDataType(CATEGORY, name, 0);
            building.put(name, structure);
            defined.put(name, structure);
        }
        int built = 0, members = 0, loose = 0;
        for (String line : structures) {
            String name = line.substring("struct ".length(), line.indexOf('{')).trim();
            StructureDataType structure = building.get(name);
            String body = line.substring(line.indexOf('{') + 1, line.lastIndexOf('}')).trim();
            for (String member : body.split(";")) {
                member = member.trim();
                if (member.isEmpty()) continue;
                int cut = member.lastIndexOf(' ');
                if (cut < 0) continue;
                String kind = member.substring(0, cut).trim();
                String field = member.substring(cut + 1).trim();
                int count = 1;
                int bracket = field.indexOf('[');
                if (bracket >= 0) {
                    count = Integer.parseInt(field.substring(bracket + 1, field.indexOf(']')));
                    field = field.substring(0, bracket);
                }
                DataType resolved = type(kind);
                if (resolved == null) {
                    // A callback typedef -- `destructor`, `PyCFunction`. Pointer-sized and
                    // carrying no layout of its own, so the SLOT is kept at the right width
                    // rather than the member being dropped, which would shift everything after.
                    resolved = Undefined4DataType.dataType;
                    loose++;
                }
                if (count > 1) resolved = new ArrayDataType(resolved, count, resolved.getLength());
                structure.add(resolved, field, null);
                members++;
            }
            if (apply) {
                currentProgram.getDataTypeManager().addDataType(
                        structure, ghidra.program.model.data.DataTypeConflictHandler
                                .REPLACE_HANDLER);
            }
            report.println("struct " + name + "  " + structure.getLength() + " bytes");
            built++;
        }

        // ---- pass 1: signatures ----------------------------------------------------------
        List<Function> imported = new ArrayList<>();
        for (Function one : currentProgram.getFunctionManager().getExternalFunctions()) {
            imported.add(one);
        }
        int applied = 0, unknown = 0, unparsed = 0;
        Set<Function> takesString = new HashSet<>();
        for (Function function : imported) {
            String prototype = prototypes.get(function.getName());
            if (prototype == null) {
                unknown++;
                continue;
            }
            FunctionDefinitionDataType definition = build(function.getName(), prototype);
            if (definition == null) {
                unparsed++;
                report.println("could not parse: " + prototype);
                continue;
            }
            for (ParameterDefinition one : definition.getArguments()) {
                if (isCharPointer(one.getDataType())) {
                    takesString.add(function);
                    break;
                }
            }
            if (apply) {
                ApplyFunctionSignatureCmd command = new ApplyFunctionSignatureCmd(
                        function.getEntryPoint(), definition, SourceType.IMPORTED);
                if (!command.applyTo(currentProgram, monitor)) {
                    report.println("could not apply to " + function.getName() + ": "
                            + command.getStatusMsg());
                    continue;
                }
            }
            applied++;
        }

        // ---- pass 2: the strings those calls pass ---------------------------------------
        Listing listing = currentProgram.getListing();
        int made = 0, already = 0;
        Set<Address> considered = new HashSet<>();
        for (Function function : takesString) {
            // Both the import itself and every thunk that stands in for it are call targets.
            List<Address> entries = new ArrayList<>();
            entries.add(function.getEntryPoint());
            for (Function thunk : thunkFunctions(function)) {
                entries.add(thunk.getEntryPoint());
            }
            for (Address entry : entries) {
                for (Reference reference : getReferencesTo(entry)) {
                    if (!reference.getReferenceType().isCall()) continue;
                    Instruction at = getInstructionAt(reference.getFromAddress());
                    for (int step = 0; step < LOOKBACK && at != null; step++) {
                        at = at.getPrevious();
                        if (at == null) break;
                        if (!"PUSH".equals(at.getMnemonicString())) continue;
                        Object[] operands = at.getOpObjects(0);
                        if (operands.length != 1 || !(operands[0] instanceof Scalar)) continue;
                        Address target;
                        try {
                            target = toAddr(((Scalar) operands[0]).getUnsignedValue());
                        } catch (Exception e) { continue; }
                        if (!considered.add(target)) continue;
                        MemoryBlock block = getMemoryBlock(target);
                        if (block == null || !block.isInitialized() || block.isExecute()) continue;
                        Data existing = listing.getDataAt(target);
                        if (existing != null && existing.hasStringValue()) { already++; continue; }
                        if (existing != null && existing.isDefined()) continue;
                        int length = printableRun(target);
                        if (length <= 0) continue;
                        if (apply) {
                            try {
                                createAsciiString(target, length + 1);
                                made++;
                            } catch (Exception e) {
                                report.println("string at " + target + ": " + e.getMessage());
                            }
                        } else {
                            made++;
                        }
                    }
                }
            }
        }

        String summary = built + " structure(s) built (" + members + " members, " + loose
                + " callback typedefs kept at pointer width); " + applied
                + " import(s) given their declared signature, " + unknown
                + " not declared by CPython 2.1.2 (Troika's own additions and ordinal imports), "
                + unparsed + " failed to parse; " + takesString.size()
                + " of them take a char *, and " + made + " string(s) "
                + (apply ? "created" : "would be created") + " at what those calls pass ("
                + already + " already were strings)";
        report.println("\n// " + summary);
        report.close();
        println("ApplyPythonApi: " + currentProgram.getName() + " -> " + summary);
    }


    /** A prototype from `pyapi.py` as a Ghidra function definition.

        Built directly rather than through Ghidra's C parser: headless has no data-type service
        for that parser to resolve against, and it rejected all 552 declarations. The vocabulary
        the extractor emits is small and closed, so mapping it is exact -- and a type the map
        does not carry is REPORTED, never silently defaulted, because a wrong parameter width is
        worse than no signature at all. */
    private FunctionDefinitionDataType build(String name, String prototype) {
        int open = prototype.indexOf('(');
        int close = prototype.lastIndexOf(')');
        if (open < 0 || close < open) return null;
        String head = prototype.substring(0, open).trim();
        if (!head.endsWith(name)) return null;
        DataType returns = type(head.substring(0, head.length() - name.length()).trim());
        if (returns == null) return null;
        FunctionDefinitionDataType definition = new FunctionDefinitionDataType(name);
        definition.setReturnType(returns);
        String body = prototype.substring(open + 1, close).trim();
        List<ParameterDefinition> parameters = new ArrayList<>();
        boolean varargs = false;
        if (!body.isEmpty() && !"void".equals(body)) {
            String[] parts = body.split(",");
            for (int i = 0; i < parts.length; i++) {
                String one = parts[i].trim();
                if ("...".equals(one)) { varargs = true; continue; }
                DataType kind = type(one);
                if (kind == null) return null;
                parameters.add(new ParameterDefinitionImpl("a" + i, kind, null));
            }
        }
        definition.setArguments(parameters.toArray(new ParameterDefinition[0]));
        definition.setVarArgs(varargs);
        return definition;
    }

    private boolean isCharPointer(DataType kind) {
        return kind instanceof PointerDataType
                && ((PointerDataType) kind).getDataType() instanceof CharDataType;
    }

    /** One C type from the extractor's closed vocabulary, or null when it is not in it. */
    private DataType type(String text) {
        text = text.trim();
        int stars = 0;
        if (text.endsWith("[]")) { stars++; text = text.substring(0, text.length() - 2).trim(); }
        while (text.endsWith("*")) { stars++; text = text.substring(0, text.length() - 1).trim(); }
        if (text.startsWith("const ")) text = text.substring(6).trim();
        DataType base;
        DataType structure = defined.get(text);
        if (structure != null) {
            DataType kind = structure;
            for (int i = 0; i < stars; i++) kind = new PointerDataType(kind);
            return kind;
        }
        switch (text) {
            case "void":            base = VoidDataType.dataType; break;
            case "char":            base = CharDataType.dataType; break;
            case "int":             base = IntegerDataType.dataType; break;
            case "unsigned":        base = UnsignedIntegerDataType.dataType; break;
            case "long":            base = LongDataType.dataType; break;
            case "unsigned long":   base = UnsignedLongDataType.dataType; break;
            case "LONG_LONG":       base = LongLongDataType.dataType; break;
            case "size_t":          base = UnsignedIntegerDataType.dataType; break;
            case "double":          base = DoubleDataType.dataType; break;
            case "float":           base = FloatDataType.dataType; break;
            case "wchar_t":         base = UnsignedIntegerDataType.dataType; break;
            default:
                // A CPython struct or parser type the extractor left named. Every one is only
                // ever reached through a pointer, so it is pointer-sized and opaque here; a
                // bare one would be a size this pass cannot state, and is refused.
                if (stars == 0) return null;
                base = Undefined4DataType.dataType;
                stars--;
                break;
        }
        DataType kind = base;
        for (int i = 0; i < stars; i++) kind = new PointerDataType(kind);
        return kind;
    }

    /** Every thunk standing in for an external function. */
    private List<Function> thunkFunctions(Function function) {
        List<Function> found = new ArrayList<>();
        Address[] addresses = function.getFunctionThunkAddresses(true);
        if (addresses == null) return found;
        for (Address one : addresses) {
            Function thunk = getFunctionAt(one);
            if (thunk != null) found.add(thunk);
        }
        return found;
    }

    /** The length of a NUL-terminated printable run at an address, or 0 when there is none. */
    private int printableRun(Address at) {
        try {
            for (int i = 0; i < MAX_STRING; i++) {
                int b = getByte(at.add(i)) & 0xFF;
                if (b == 0) return i;                       // a run of length 0 is not a string
                if (b < 0x20 || b > 0x7e) return 0;
            }
        } catch (Exception e) {
            return 0;
        }
        return 0;
    }
}
