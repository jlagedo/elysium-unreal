import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.SourceType;

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

// Recovers the C++ class hierarchy MSVC left in the image, and binds each vftable to the
// class that owns it.
//
// A recon pass that greps ".?AV" strings recovers class *names*; that is a flat set. The
// rest of the record is still there: every polymorphic vftable stores a pointer to a
// complete object locator in the slot *before* its first method, and that locator names
// the class, the offset of this subobject within the complete object, and the full base
// list with each base's displacement. Multiple inheritance is where a name list stops
// being enough -- a secondary base's vftable sits at a non-zero offset and its slots are
// reached through adjustor thunks, so attributing an override by chasing thunk targets
// mis-assigns exactly those classes.
//
// Structures (32-bit MSVC):
//   TypeDescriptor            +0 vftable  +4 spare  +8 name (".?AVCFoo@@")
//   CompleteObjectLocator     +0 signature(0)  +4 offset  +8 cdOffset
//                             +12 TypeDescriptor*  +16 ClassHierarchyDescriptor*
//   ClassHierarchyDescriptor  +0 signature(0)  +4 attributes  +8 numBaseClasses
//                             +12 BaseClassArray*
//   BaseClassDescriptor       +0 TypeDescriptor*  +4 numContainedBases
//                             +8 PMD{mdisp,pdisp,vdisp}  +20 attributes
//
// Labels each located vftable and gives it a plate comment carrying the base chain, so a
// later dump of that vftable says which class it belongs to. Pass name=0 to report only.
//
// Args (key=value, space separated):
//   out=<path>   output file
//   name=0       report only; create no labels or comments
public class DumpRtti extends GhidraScript {

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
        String outPath = a.getOrDefault("out", "E:/tmp/elysium_ghidra_out/rtti.txt");
        boolean label = !"0".equals(a.get("name"));

        cacheDataBlocks();

        // 1) type descriptors, by address, from their own mangled name string
        Map<Long, String> types = new TreeMap<>();
        for (int b = 0; b < blockBytes.size(); b++) {
            byte[] bytes = blockBytes.get(b);
            long start = blockStarts.get(b).getOffset();
            for (int i = 0; i + 3 < bytes.length; i++) {
                monitor.checkCancelled();
                if (bytes[i] != '.' || bytes[i + 1] != '?' || bytes[i + 2] != 'A') continue;
                char kind = (char) bytes[i + 3];
                if (kind != 'V' && kind != 'U') continue;
                String mangled = readString(bytes, i);
                if (mangled == null || !mangled.endsWith("@@") || i < 8) continue;
                types.put(start + i - 8, mangled);
            }
        }

        // 2) complete object locators: a dword pointing at a type descriptor, 12 bytes
        //    into a record whose signature is zero and whose hierarchy descriptor parses
        Set<Long> typeAddrs = types.keySet();
        List<Col> locators = new ArrayList<>();
        for (int b = 0; b < blockBytes.size(); b++) {
            byte[] bytes = blockBytes.get(b);
            long start = blockStarts.get(b).getOffset();
            for (int i = 0; i + 4 <= bytes.length; i += 4) {
                monitor.checkCancelled();
                if (!typeAddrs.contains(dword(bytes, i))) continue;
                int col = i - 12;
                if (col < 0 || dword(bytes, col) != 0) continue;
                long chd = dword(bytes, col + 16);
                int[] hierarchy = readHierarchy(chd);
                if (hierarchy == null) continue;
                locators.add(new Col(start + col, types.get(dword(bytes, i)),
                        (int) dword(bytes, col + 4), hierarchy[0], hierarchy[1], hierarchy[2]));
            }
        }

        // 3) vftables: the slot before a vftable holds its locator
        Map<Long, Col> byAddress = new HashMap<>();
        for (Col col : locators) byAddress.put(col.address, col);
        int labelled = 0;
        for (int b = 0; b < blockBytes.size(); b++) {
            byte[] bytes = blockBytes.get(b);
            long start = blockStarts.get(b).getOffset();
            for (int i = 0; i + 8 <= bytes.length; i += 4) {
                monitor.checkCancelled();
                Col col = byAddress.get(dword(bytes, i));
                if (col == null) continue;
                Address vftable = toAddr(start + i + 4);
                MemoryBlock target;
                try { target = getMemoryBlock(toAddr(dword(bytes, i + 4))); }
                catch (Exception e) { continue; }                      // slot is not a pointer
                if (target == null || !target.isExecute()) continue;   // not a method table
                col.vftables.add(vftable);
                if (label) { labelVftable(vftable, col); labelled++; }
            }
        }

        new File(outPath).getParentFile().mkdirs();
        PrintWriter out = new PrintWriter(new FileWriter(outPath));
        out.println("// " + currentProgram.getName() + "  " + types.size() + " type descriptors, "
                + locators.size() + " complete object locators, " + labelled + " vftables labelled");

        // An empty result has two very different causes, and the caller has to be able to
        // tell them apart: a module built without /GR carries no descriptors at all, which
        // is an ordinary absence, whereas descriptors without locators means the walk failed.
        if (types.isEmpty()) {
            out.println("// module carries no RTTI: compiled without /GR");
            println("DumpRtti: " + currentProgram.getName() + " carries no RTTI (built without /GR)");
        } else if (locators.isEmpty()) {
            printerr("DumpRtti: " + currentProgram.getName() + " has " + types.size()
                    + " type descriptors but no complete object locator parsed; the walk failed");
        }

        locators.sort((x, y) -> x.name.compareTo(y.name));
        int multiple = 0;
        out.println("\n//======== classes ========");
        for (Col col : locators) {
            if ((col.attributes & 1) != 0) multiple++;
            out.printf("%n%s%s%n", demangle(col.name), col.offset == 0 ? "" : "  (subobject at +" + col.offset + ")");
            out.println("  locator " + toAddr(col.address)
                    + "   bases " + col.baseCount
                    + "   attributes " + attributeText(col.attributes));
            for (Address vftable : col.vftables) out.println("  vftable " + vftable);
            for (Base base : readBases(col.baseArray, col.baseCount)) {
                out.printf("    +%-6d %s%n", base.displacement, demangle(base.name));
            }
        }
        out.println("\n// " + multiple + " classes use multiple inheritance"
                + " (secondary vftables sit at a non-zero offset and reach methods through adjustor thunks)");
        out.close();
        println("DumpRtti: " + locators.size() + " classes, " + labelled + " vftables labelled -> " + outPath);
    }

    /** attributes/numBaseClasses/pBaseClassArray of a class hierarchy descriptor, or null. */
    private int[] readHierarchy(long address) {
        Integer signature = peek(address);
        Integer attributes = peek(address + 4);
        Integer count = peek(address + 8);
        Integer array = peek(address + 12);
        if (signature == null || attributes == null || count == null || array == null) return null;
        if (signature != 0 || count <= 0 || count > 64) return null;
        if (peek(Integer.toUnsignedLong(array)) == null) return null;
        return new int[] { attributes, count, array };
    }

    private List<Base> readBases(int arrayAddress, int count) {
        List<Base> bases = new ArrayList<>();
        long array = Integer.toUnsignedLong(arrayAddress);
        for (int i = 0; i < count; i++) {
            Integer descriptor = peek(array + 4L * i);
            if (descriptor == null) break;
            long bcd = Integer.toUnsignedLong(descriptor);
            Integer type = peek(bcd);
            Integer displacement = peek(bcd + 8);
            if (type == null || displacement == null) break;
            String name = readTypeName(Integer.toUnsignedLong(type));
            if (name == null) break;
            bases.add(new Base(name, displacement));
        }
        return bases;
    }

    private String readTypeName(long typeDescriptor) {
        for (int b = 0; b < blockBytes.size(); b++) {
            long start = blockStarts.get(b).getOffset();
            byte[] bytes = blockBytes.get(b);
            long index = typeDescriptor + 8 - start;
            if (index < 0 || index >= bytes.length) continue;
            return readString(bytes, (int) index);
        }
        return null;
    }

    private void labelVftable(Address vftable, Col col) throws Exception {
        String suffix = col.offset == 0 ? "" : "_at" + col.offset;
        createLabel(vftable, "vftable_" + sanitize(demangle(col.name)) + suffix, true,
                SourceType.ANALYSIS);
        StringBuilder plate = new StringBuilder(demangle(col.name));
        if (col.offset != 0) plate.append("  (subobject at +").append(col.offset).append(')');
        plate.append("\nattributes ").append(attributeText(col.attributes));
        for (Base base : readBases(col.baseArray, col.baseCount)) {
            plate.append("\n  +").append(base.displacement).append(' ').append(demangle(base.name));
        }
        setPlateComment(vftable, plate.toString());
    }

    private void cacheDataBlocks() {
        for (MemoryBlock block : currentProgram.getMemory().getBlocks()) {
            if (!block.isInitialized() || block.isExecute()) continue;
            int size = (int) Math.min(block.getSize(), Integer.MAX_VALUE);
            byte[] bytes = new byte[size];
            try { block.getBytes(block.getStart(), bytes); }
            catch (Exception e) { printerr("DumpRtti: unreadable block " + block.getName()); continue; }
            blockStarts.add(block.getStart());
            blockBytes.add(bytes);
        }
    }

    /** The dword at an image address, or null when it is outside every cached data block. */
    private Integer peek(long address) {
        for (int b = 0; b < blockBytes.size(); b++) {
            long index = address - blockStarts.get(b).getOffset();
            byte[] bytes = blockBytes.get(b);
            if (index < 0 || index + 4 > bytes.length) continue;
            return (int) dword(bytes, (int) index);
        }
        return null;
    }

    private static long dword(byte[] bytes, int index) {
        return (bytes[index] & 0xffL) | ((bytes[index + 1] & 0xffL) << 8)
                | ((bytes[index + 2] & 0xffL) << 16) | ((bytes[index + 3] & 0xffL) << 24);
    }

    private static String readString(byte[] bytes, int index) {
        StringBuilder sb = new StringBuilder();
        for (int i = index; i < bytes.length && sb.length() < 512; i++) {
            byte c = bytes[i];
            if (c == 0) return sb.toString();
            if (c < 0x20 || c > 0x7e) return null;
            sb.append((char) c);
        }
        return null;
    }

    /** ".?AVCBaseEntity@@" -> "CBaseEntity"; the qualifier list is stored innermost first. */
    private static String demangle(String mangled) {
        String body = mangled.substring(4, mangled.length() - 2);
        String[] parts = body.split("@");
        StringBuilder sb = new StringBuilder();
        for (int i = parts.length - 1; i >= 0; i--) {
            if (parts[i].isEmpty()) continue;
            if (sb.length() > 0) sb.append("::");
            sb.append(parts[i]);
        }
        return sb.length() == 0 ? mangled : sb.toString();
    }

    private static String sanitize(String name) {
        StringBuilder sb = new StringBuilder();
        for (char c : name.toCharArray()) {
            sb.append(Character.isLetterOrDigit(c) || c == '_' ? c : '_');
        }
        return sb.toString();
    }

    private static String attributeText(int attributes) {
        if (attributes == 0) return "single";
        StringBuilder sb = new StringBuilder();
        if ((attributes & 1) != 0) sb.append("multiple ");
        if ((attributes & 2) != 0) sb.append("virtual ");
        if ((attributes & 4) != 0) sb.append("ambiguous ");
        return sb.length() == 0 ? Integer.toString(attributes) : sb.toString().trim();
    }

    private static final class Col {
        final long address;
        final String name;
        final int offset;
        final int attributes;
        final int baseCount;
        final int baseArray;
        final List<Address> vftables = new ArrayList<>();

        Col(long address, String name, int offset, int attributes, int baseCount, int baseArray) {
            this.address = address;
            this.name = name;
            this.offset = offset;
            this.attributes = attributes;
            this.baseCount = baseCount;
            this.baseArray = baseArray;
        }
    }

    private static final class Base {
        final String name;
        final int displacement;

        Base(String name, int displacement) {
            this.name = name;
            this.displacement = displacement;
        }
    }
}
