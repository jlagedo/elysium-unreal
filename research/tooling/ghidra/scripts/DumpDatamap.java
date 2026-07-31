import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;
import java.io.*;
import java.util.*;

// Walk a VtMB datamap_t and print its typedescription_t records (the class's Hammer
// keyfields / inputs / outputs), following the baseMap chain to the root.
//
// VtMB layouts (empirically confirmed against CBaseLandmark's builder FUN_100b7220):
//   datamap_t            dataDesc@0  numFields@4  className@8  baseMap@0xC
//   typedescription_t    44 (0x2C) bytes:
//       fieldType@0  internalName@4  fieldOffset@8  flags@0x12(short)
//       externalName@0x14  inputFunc@0x1C
//   flags bit 0x8 = keyable/writable (RE3).  A non-zero inputFunc makes the record an
//   input; fieldType 10 marks an output.
//
// Entry points (either works):
//   map=<hex>      a datamap_t address (what GetDataDescMap returns)
//   vtable=<hex>   a class vftable; reads slot +0x148 (GetDataDescMap), follows a
//                  5-byte JMP thunk if present, and reads the `MOV EAX,imm32; RET` body
//
// Most VtMB datamaps are HALF static: the leading records (inputs/keyfields) are
// statically initialized in .data, while the trailing output records AND the
// datamap_t's own dataDesc/numFields are written at static-init time by a builder
// function -- so a raw image read reports numFields 0. Use recs=/count= to read the
// static half directly (find the builder + its record base by xref'ing the datamap_t).
//
// Args (key=value, space separated; one value each -- see README on arg splitting):
//   map=<hex> | vtable=<hex>
//   recs=<hex>     override the record-array address (else datamap_t.dataDesc)
//   count=<int>    override the record count (else datamap_t.numFields)
//   depth=<int>    how many baseMap links to follow (default 6)
//   out=<path>     output file
public class DumpDatamap extends GhidraScript {

    static final int REC_SIZE = 0x2C;

    Memory mem;

    public void run() throws Exception {
        Map<String,String> a = new HashMap<>();
        String[] raw = getScriptArgs();
        for (int i = 0; i < raw.length; i++) {
            int eq = raw[i].indexOf('=');
            if (eq > 0) a.put(raw[i].substring(0, eq), raw[i].substring(eq + 1));
            else if (i + 1 < raw.length) { a.put(raw[i], raw[i + 1]); i++; }
        }
        String outPath = a.getOrDefault("out",
            "E:/dev/elysium-unreal/$ELYSIUM_WORK_ROOT/research/ghidra/$ELYSIUM_EXPORT_ROOT/datamap.txt");
        int depth = Integer.parseInt(a.getOrDefault("depth", "6"));
        mem = currentProgram.getMemory();

        long mapAddr;
        if (a.containsKey("map")) {
            mapAddr = Long.parseLong(a.get("map"), 16);
        } else if (a.containsKey("vtable")) {
            mapAddr = resolveFromVtable(Long.parseLong(a.get("vtable"), 16));
        } else {
            println("DumpDatamap: need map=<hex> or vtable=<hex>");
            return;
        }

        new File(outPath).getParentFile().mkdirs();
        PrintWriter out = new PrintWriter(new FileWriter(outPath));
        out.println("// program: " + currentProgram.getName()
            + "  imagebase " + currentProgram.getImageBase());
        out.printf("// datamap_t @ %08x%n", mapAddr);

        long recsOverride  = a.containsKey("recs")
            ? Long.parseLong(a.get("recs"), 16) : 0;
        long countOverride = a.containsKey("count")
            ? Long.parseLong(a.get("count")) : 0;

        long cur = mapAddr;
        for (int level = 0; level < depth && cur != 0; level++) {
            // Overrides apply to the entry map only; the base chain reads itself.
            cur = dumpOne(out, cur, level,
                level == 0 ? recsOverride : 0,
                level == 0 ? countOverride : 0);
        }
        out.close();
        println("DumpDatamap: wrote " + outPath);
    }

    // vftable +0x148 = GetDataDescMap. The slot usually points at a 5-byte JMP thunk;
    // the real body is `MOV EAX,<datamap_t>; RET` (B8 imm32 C3).
    long resolveFromVtable(long vtable) throws Exception {
        long slot = u32(vtable + 0x148);
        long body = slot;
        if ((u8(slot) & 0xFF) == 0xE9) {              // JMP rel32
            long rel = u32(slot + 1);
            body = slot + 5 + (int) rel;
        }
        if ((u8(body) & 0xFF) == 0xB8) {              // MOV EAX,imm32
            return u32(body + 1);
        }
        println(String.format(
            "DumpDatamap: GetDataDescMap @ %08x is not a MOV EAX,imm32 stub", body));
        return 0;
    }

    // Prints one datamap_t's records; returns its baseMap (0 = root).
    long dumpOne(PrintWriter out, long mapAddr, int level,
                 long recsOverride, long countOverride) throws Exception {
        long dataDesc = recsOverride  != 0 ? recsOverride  : u32(mapAddr);
        long num      = countOverride != 0 ? countOverride : u32(mapAddr + 4);
        long clsPtr   = u32(mapAddr + 8);
        long baseMap  = u32(mapAddr + 0xC);
        String cls = clsPtr != 0 ? cstr(clsPtr) : "(unnamed)";

        out.println();
        out.printf("======== [%d] %s   datamap_t %08x  records %08x  count %d  base %08x ========%n",
            level, cls, mapAddr, dataDesc, num, baseMap);

        if (num <= 0 || num > 4096 || dataDesc == 0) {
            out.println("  (no records / implausible count -- stopping)");
            return 0;
        }

        List<String> keys = new ArrayList<>(), inputs = new ArrayList<>(), outputs = new ArrayList<>();
        for (long i = 0; i < num; i++) {
            long rec       = dataDesc + i * REC_SIZE;
            long fieldType = u32(rec);
            long internal  = u32(rec + 4);
            long offset    = u32(rec + 8);
            int  flags     = (int) (u16(rec + 0x12) & 0xFFFF);
            long external  = u32(rec + 0x14);
            long inputFunc = u32(rec + 0x1C);

            String iname = internal != 0 ? cstr(internal) : "";
            String ename = external != 0 ? cstr(external) : "";
            String line = String.format(
                "  %-32s %-34s type=%-3d off=0x%04x flags=0x%04x%s%s",
                ename.isEmpty() ? "(no external name)" : ename,
                iname,
                fieldType, offset, flags,
                (flags & 0x8) != 0 ? " keyable" : "",
                inputFunc != 0 ? String.format(" inputFunc=%08x", inputFunc) : "");

            if (inputFunc != 0)      inputs.add(line);
            else if (fieldType == 10) outputs.add(line);
            else                      keys.add(line);
        }

        out.println("---- INPUTS (" + inputs.size() + ") ----");
        for (String s : inputs)  out.println(s);
        out.println("---- OUTPUTS (" + outputs.size() + ") ----");
        for (String s : outputs) out.println(s);
        out.println("---- FIELDS (" + keys.size() + ") ----");
        for (String s : keys)    out.println(s);

        return baseMap;
    }

    Address at(long v) { return currentProgram.getAddressFactory()
        .getDefaultAddressSpace().getAddress(v); }
    long u32(long v) throws Exception { return mem.getInt(at(v)) & 0xFFFFFFFFL; }
    int  u16(long v) throws Exception { return mem.getShort(at(v)) & 0xFFFF; }
    int  u8 (long v) throws Exception { return mem.getByte(at(v)) & 0xFF; }

    String cstr(long v) {
        StringBuilder sb = new StringBuilder();
        try {
            for (int i = 0; i < 128; i++) {
                int c = u8(v + i);
                if (c == 0) break;
                sb.append((char) c);
            }
        } catch (Exception e) { return "(unreadable)"; }
        return sb.toString();
    }
}
