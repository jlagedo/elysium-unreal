import ghidra.app.script.GhidraScript;
import ghidra.feature.fid.cmd.ApplyFidEntriesCommand;
import ghidra.feature.fid.db.FidFile;
import ghidra.feature.fid.db.FidFileManager;
import ghidra.feature.fid.service.FidService;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressIterator;
import ghidra.program.model.address.AddressSetView;
import ghidra.program.model.listing.Function;

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.HashMap;
import java.util.Map;

// Names a program's statically linked library functions from an attached FID database.
// This is what the "Function ID" analyzer does, run on demand, for programs that were
// imported and analyzed before the database existed.
//
// Args (key=value, space separated):
//   fidb=<path>    FID database to attach first (optional if already attached)
//   score=<float>  minimum match score, roughly an instruction count (default 14.6)
//   multi=<float>  higher bar a match must clear when several names conflict (default 30)
//   force=1        apply FID labels even where an imported or user-defined label exists
//   out=<path>     optional listing of every named address
public class ApplyFid extends GhidraScript {

    public void run() throws Exception {
        // analyzeHeadless splits a key=value argument on the '=' before the script sees
        // it, so a pair may arrive as one token or as two; accept both.
        Map<String, String> a = new HashMap<>();
        String[] raw = getScriptArgs();
        for (int i = 0; i < raw.length; i++) {
            int eq = raw[i].indexOf('=');
            if (eq > 0) a.put(raw[i].substring(0, eq), raw[i].substring(eq + 1));
            else if (i + 1 < raw.length) { a.put(raw[i], raw[i + 1]); i++; }
        }
        String fidbPath = a.get("fidb");
        if (fidbPath != null) {
            File fidb = new File(fidbPath);
            if (!fidb.isFile()) {
                printerr("ApplyFid: no such FID database: " + fidb);
                return;
            }
            FidFile fidFile = FidFileManager.getInstance().addUserFidFile(fidb);
            if (fidFile == null) {
                printerr("ApplyFid: not a usable FID database: " + fidb);
                return;
            }
        }
        FidService service = new FidService();
        if (!service.canProcess(currentProgram.getLanguage())) {
            printerr("ApplyFid: no attached FID database covers " + currentProgram.getLanguageID());
            return;
        }

        float score = Float.parseFloat(a.getOrDefault("score",
                Float.toString(service.getDefaultScoreThreshold())));
        float multi = Float.parseFloat(a.getOrDefault("multi",
                Float.toString(service.getDefaultMultiNameThreshold())));
        boolean force = "1".equals(a.get("force"));

        AddressSetView set = currentProgram.getMemory().getExecuteSet();
        if (set.isEmpty()) set = currentProgram.getMemory().getLoadedAndInitializedAddressSet();

        ApplyFidEntriesCommand cmd = new ApplyFidEntriesCommand(set, score, multi, force, true);
        if (!cmd.applyTo(currentProgram, monitor)) {
            printerr("ApplyFid: command failed: " + cmd.getStatusMsg());
            return;
        }

        PrintWriter out = null;
        String outPath = a.get("out");
        if (outPath != null) {
            new File(outPath).getParentFile().mkdirs();
            out = new PrintWriter(new FileWriter(outPath));
        }
        int matched = 0;
        AddressIterator addresses = cmd.getFIDLocations().getAddresses(true);
        while (addresses.hasNext()) {
            Address address = addresses.next();
            Function function = getFunctionAt(address);
            if (function == null) continue;
            matched++;
            if (out != null) out.printf("%s  %s%n", address, function.getName());
        }
        if (out != null) out.close();
        println("ApplyFid: " + currentProgram.getName() + " named " + matched + " functions"
                + (outPath == null ? "" : " -> " + outPath));
    }
}
