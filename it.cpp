#include <Windows.h>
#include <Shlobj.h>
#include <Shlwapi.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string>
#include <vector>
#include <time.h>
#include "unzipTool.h"
#include "setup.h"
#include "cfg.h"

void showUsageAndExit () {
    printf ("USAGE:\n\n");
    exit (0);
}

int main (int argCount, char *args []) {
    void *dummy = 0;

    CoInitialize (dummy);

    checkElevate ();
    
    printf ("Simplified ECDIS installer\nCopyright (C) 2021 by CAIM\n");

    Cfg cfg;
    char workingDir [MAX_PATH];

    GetModuleFileName (0, workingDir, sizeof (workingDir));
    PathRemoveFileSpec (workingDir);

    if (argCount > 1) {
        if (stricmp (args [1], "-h") == 0 || stricmp (args [1], "/h") == 0) {
            showUsageAndExit ();
        } else if (PathFileExists (args [1])) {
            cfg.script = args [1];
        } else {
            printf ("File %s not found\n", args [1]);
        }
    } else {
        char scriptPath [MAX_PATH];

        PathCombine (scriptPath, workingDir, "ecdis.isc");

        cfg.script = scriptPath;
    }

    cfg.defDestDir = cfg.getString ("main", "defaultDestDir");
    cfg.sourceDir = cfg.getString ("main", "sourceDir", (std::string (workingDir) + "\\sources").c_str ());

    if (MessageBox (GetConsoleWindow (), "Do you want to select non-standard destination folder?", "Destination", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        cfg.destDir = browseForFolder ("Select destination folder");

        if (cfg.destDir.empty ()) exit (0);
    } else {
        char path [MAX_PATH];

        GetEnvironmentVariable ("ProgramFiles(x86)", path, sizeof (path));
        PathAppend (path, "CAIM");
        
        cfg.destDir = path;
    }

    printf ("Extracting the package...\n");

    auto packagePath = cfg.getString ("Main", "package");

    if (packagePath.empty ()) {
        printf ("Package is not specified in the script.'n");
        exit (0);
    }

    char tempPath [MAX_PATH];
    char tempFolder [100];
    GetTempPath (sizeof (tempPath), tempPath);
    PathAppend (tempPath, std::to_string (time (0)).c_str ());
    PathRenameExtension (tempPath, ".tmp");
    CreateDirectory (tempPath, 0);
    unzipArchive (packagePath.c_str (), false, true, tempPath, 0);

    printf ("Copying files...\n");

    auto numOfFileGroups = cfg.getInt ("FileGroups", "Number");

    for (auto i = 1; i <= numOfFileGroups; ++ i) {
        auto info = cfg.getString ("FileGroups", std::to_string (i).c_str ());

        if (!info.empty ()) {
            auto commaPos = info.find (',');

            if (commaPos != std::string::npos) {
                auto sourceSubFolder = info.substr (0, commaPos);
                auto destFolder = info.substr (commaPos + 1);
                char path [MAX_PATH];

                if (destFolder.find ('%') != std::string::npos) {
                    ExpandEnvironmentStrings (destFolder.c_str (), path, sizeof (path));

                    destFolder = path;
                }

                copyFolder (destFolder.c_str (), PathCombine (path, tempPath, sourceSubFolder.c_str ()), [] (char *msg) { printf ("%s\n", msg); });
            }
        }
    }

    printf ("Creating shortcuts...\n");

    auto numOfShortcuts = cfg.getInt ("Shortcuts", "Number");

    for (auto i = 1; i <= numOfShortcuts; ++ i) {
        auto info = cfg.getString ("Shortcuts", std::to_string (i).c_str ());
        char buffer [1000];
        std::vector<std::string> parts;

        if (strnicmp (info.c_str (), "shortcut", 8) == 0) {
            SHGetFolderPath (GetConsoleWindow (), CSIDL_COMMON_STARTUP, 0, SHGFP_TYPE_DEFAULT, buffer);
            std::string temp (buffer);

            temp += info.substr (8);
            info = temp;
        }

        ExpandEnvironmentStrings (info.c_str (), buffer, sizeof (buffer));

        info = buffer;

        splitLine (info, parts, ',');

        if (parts.size () > 2) {
            auto& shortcutPath = parts [0];
            auto& cmd = parts [1];
            auto& desc = parts [2];

            makeShortcut (cmd.c_str (), shortcutPath.c_str (), desc.c_str ());
        }
    }

    printf ("Installation done\n");
}