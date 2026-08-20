import ghidra.app.script.GhidraScript;
import ghidra.feature.fid.db.FidDB;
import ghidra.feature.fid.db.FidFile;
import ghidra.feature.fid.db.FidFileManager;
import ghidra.feature.fid.service.FidPopulateResult;
import ghidra.feature.fid.service.FidPopulateResult.Disposition;
import ghidra.feature.fid.service.FidService;
import ghidra.feature.fid.service.Location;
import ghidra.framework.model.DomainFile;
import ghidra.framework.model.DomainFolder;
import ghidra.program.database.ProgramContentHandler;
import ghidra.program.model.lang.LanguageID;
import ghidra.util.task.TaskMonitor;

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.TreeSet;

// Hashes every analyzed program under a project folder into one library of a FID database,
// creating and attaching the database on first use. This is the headless equivalent of
// "Tools -> Function ID -> Populate FidDb from programs", with the dialog's answers as args.
//
// Args (key=value, space separated):
//   fidb=<path>      the .fidb to populate; created if absent
//   folder=<path>    project folder holding the imported+analyzed objects (e.g. /vc6sp5/libc)
//   name=<str>       library family name    (e.g. vc6)
//   version=<str>    library version        (e.g. sp5)
//   variant=<str>    library variant        (e.g. libc)
//   lang=<id>        LanguageID filter      (default x86:LE:32:default)
//   common=<path>    optional common-symbols file; suppresses relations on ubiquitous names
//   out=<path>       optional report file (defaults to console)
public class BuildCrtFid extends GhidraScript {

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
        String folderPath = a.get("folder");
        String name = a.get("name");
        String version = a.get("version");
        String variant = a.get("variant");
        if (fidbPath == null || folderPath == null || name == null || version == null || variant == null) {
            printerr("BuildCrtFid: fidb=, folder=, name=, version= and variant= are all required");
            return;
        }
        LanguageID languageId = new LanguageID(a.getOrDefault("lang", "x86:LE:32:default"));

        List<String> commonSymbols = null;
        String commonPath = a.get("common");
        if (commonPath != null) {
            File common = new File(commonPath);
            if (!common.isFile()) {
                printerr("BuildCrtFid: no such common-symbols file: " + common);
                return;
            }
            commonSymbols = new ArrayList<>();
            for (String line : Files.readAllLines(common.toPath())) {
                if (!line.isEmpty()) commonSymbols.add(line);
            }
        }

        DomainFolder folder = folderPath.equals("/")
                ? state.getProject().getProjectData().getRootFolder()
                : state.getProject().getProjectData().getFolder(folderPath);
        if (folder == null) {
            printerr("BuildCrtFid: no such project folder: " + folderPath);
            return;
        }
        List<DomainFile> programs = new ArrayList<>();
        findPrograms(programs, folder);
        if (programs.isEmpty()) {
            printerr("BuildCrtFid: no programs under " + folderPath);
            return;
        }

        File fidb = new File(fidbPath);
        FidFileManager manager = FidFileManager.getInstance();
        if (!fidb.exists()) {
            fidb.getParentFile().mkdirs();
            manager.createNewFidDatabase(fidb);
        }
        FidFile fidFile = manager.addUserFidFile(fidb);
        if (fidFile == null) {
            printerr("BuildCrtFid: not a usable FID database: " + fidb);
            return;
        }

        PrintWriter out = null;
        String outPath = a.get("out");
        if (outPath != null) {
            new File(outPath).getParentFile().mkdirs();
            out = new PrintWriter(new FileWriter(outPath));
        }
        FidDB fidDb = fidFile.getFidDB(true);
        try {
            FidPopulateResult result = new FidService().createNewLibraryFromPrograms(
                    fidDb, name, version, variant, programs, null, languageId, null,
                    commonSymbols, TaskMonitor.DUMMY);
            report(out, result, programs.size(), folderPath);
            fidDb.saveDatabase("Saving", monitor);
        } finally {
            fidDb.close();
            if (out != null) out.close();
        }
        println("BuildCrtFid: " + name + ':' + version + ':' + variant + " -> " + fidb);
    }

    private void report(PrintWriter out, FidPopulateResult result, int programCount, String folderPath) {
        emit(out, "library  " + result.getLibraryRecord().getLibraryFamilyName() + ':'
                + result.getLibraryRecord().getLibraryVersion() + ':'
                + result.getLibraryRecord().getLibraryVariant());
        emit(out, "source   " + folderPath + "  (" + programCount + " programs)");
        emit(out, "visited  " + result.getTotalAttempted());
        emit(out, "added    " + result.getTotalAdded());
        emit(out, "excluded " + result.getTotalExcluded());
        for (Entry<Disposition, Integer> entry : result.getFailures().entrySet()) {
            if (entry.getKey() != Disposition.INCLUDED) {
                emit(out, "    " + entry.getKey() + ": " + entry.getValue());
            }
        }
        TreeSet<String> unresolved = new TreeSet<>();
        for (Location location : result.getUnresolvedSymbols()) {
            unresolved.add(location.getFunctionName());
        }
        emit(out, "unresolved symbols (" + unresolved.size() + ")");
        for (String symbol : unresolved) {
            emit(out, "    " + symbol);
        }
    }

    private void emit(PrintWriter out, String line) {
        if (out != null) out.println(line);
        else println(line);
    }

    private void findPrograms(List<DomainFile> programs, DomainFolder folder) throws Exception {
        for (DomainFile file : folder.getFiles()) {
            monitor.checkCancelled();
            if (file.getContentType().equals(ProgramContentHandler.PROGRAM_CONTENT_TYPE)) {
                programs.add(file);
            }
        }
        for (DomainFolder child : folder.getFolders()) {
            monitor.checkCancelled();
            findPrograms(programs, child);
        }
    }
}
