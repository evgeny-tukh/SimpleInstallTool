#pragma once

#include <string>
#include <Windows.h>
#include <stdint.h>

struct Cfg {
    std::string sourceDir, script, destDir, defDestDir;

    Cfg () {
        char scriptPath [MAX_PATH], workingDir [MAX_PATH];

        GetModuleFileName (0, workingDir, sizeof (workingDir));
        PathRemoveFileSpec (workingDir);
        PathCombine (scriptPath, workingDir, "install.isc");

        script = scriptPath;
    }

    std::string getString (const char *section, const char *key, const char *def = "") {
        char buffer [1000];

        GetPrivateProfileString (section, key, def, buffer, sizeof (buffer), script.c_str ());

        return buffer;
    }

    int32_t getInt (const char *section, const char *key, int32_t def = 0) {
        auto data = getString (section, key, std::to_string (def).c_str ());
        
        return std::atoi (data.c_str ());
    }
};

