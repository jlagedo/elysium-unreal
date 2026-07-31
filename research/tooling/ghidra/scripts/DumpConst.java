import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.HashMap;
import java.util.Map;

// Dumps the raw dword at each given address, decoded as int / float / hex. The
// decompiler renders float literals loaded from .rdata as opaque _DAT_ symbols,
// so read the bytes when the actual constant (0.0, 1.0, 360.0, ...) is the
// thing being established.
//
// Args (key=value, space separated):
//   addrs=<hex;...>  addresses to read
//   out=<path>       output file
public class DumpConst extends GhidraScript {

    public void run() throws Exception {
        Map<String, String> a = new HashMap<>();
        String[] raw = getScriptArgs();
        for (int i = 0; i < raw.length; i++) {
            int eq = raw[i].indexOf('=');
            if (eq > 0) a.put(raw[i].substring(0, eq), raw[i].substring(eq + 1));
            else if (i + 1 < raw.length) { a.put(raw[i], raw[i + 1]); i++; }
        }
        String outPath = a.getOrDefault("out", "E:/dev/elysium/$ELYSIUM_WORK_ROOT/research/ghidra/$ELYSIUM_EXPORT_ROOT/const.txt");
        new File(outPath).getParentFile().mkdirs();
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        for (String s : (a.getOrDefault("addrs", "")).split("[;,]")) {
            if (s.isEmpty()) continue;
            Address addr = toAddr(s);
            try {
                int v = getInt(addr);
                out.printf("%s  hex=0x%08x  int=%d  float=%s%n",
                        addr, v, v, Float.intBitsToFloat(v));
            } catch (Exception e) {
                out.println(addr + "  <unreadable: " + e + ">");
            }
        }
        out.close();
        println("DumpConst: wrote " + outPath);
    }
}
