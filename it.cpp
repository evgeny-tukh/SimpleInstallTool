#include <Windows.h>
#include <Shlobj.h>
#include <Shlwapi.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string>
#include <vector>

struct Cfg {
    std::string sourceDir, script, destDir, defDestDir;

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

void splitLine (std::string& line, std::vector<std::string>& parts, char separator) {
    parts.clear ();
    parts.emplace_back ();

    for (auto chr: line) {
        if (chr == separator) {
            parts.emplace_back ();
        } else {
            parts.back () += chr;
        }
    }
}

bool makeShortcut (const char *cmd, const char *link, const char *desc) 
{ 
    IShellLink* shellLnkInterf; 
    bool result = true;

    // Get a pointer to the IShellLink interface. It is assumed that CoInitialize
    // has already been called.
    if (SUCCEEDED (CoCreateInstance (CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (void **) & shellLnkInterf))) { 
        IPersistFile *fileInterf; 

        // Set the path to the shortcut target and add the description. 
        shellLnkInterf->SetPath (cmd); 
        shellLnkInterf->SetDescription (desc); 

        // Query IShellLink for the IPersistFile interface, used for saving the 
        // shortcut in persistent storage. 
        if (SUCCEEDED (shellLnkInterf->QueryInterface (IID_IPersistFile, (void **) & fileInterf))) { 
            WCHAR uncodePath [MAX_PATH];
            char path [MAX_PATH];

            SHGetFolderPath (GetConsoleWindow (), CSIDL_COMMON_PROGRAMS, 0, SHGFP_TYPE_DEFAULT, path);
            PathAppend (path, link);

            // Make sure that path exust
            char folder [MAX_PATH];
            wchar_t folderUnicode [MAX_PATH];

            strcpy (folder, path);
            PathRemoveFileSpec (folder);
            MultiByteToWideChar (CP_ACP, 0, folder, -1, folderUnicode, MAX_PATH);
            SHCreateDirectory (GetConsoleWindow (), folderUnicode);

            // Ensure that the string is Unicode. 
            MultiByteToWideChar (CP_ACP, 0, path, -1, uncodePath, MAX_PATH); 

            // Save the link by calling IPersistFile::Save. 
            fileInterf->Save (uncodePath, TRUE); 
            fileInterf->Release(); 
        } else {
            result = false;
        }
        shellLnkInterf->Release(); 
    } else {
        result = false;
    } 
    return result; 
}

bool isAdmin ()
{
    BOOL admin = false;
    unsigned long error = ERROR_SUCCESS;
    PSID adminGroup = 0;

    // Allocate and initialize a SID of the administrators group.
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (!AllocateAndInitializeSid (& ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, & adminGroup)) {
        error = GetLastError ();
    } else {
        // Determine whether the SID of administrators group is enabled in 
        // the primary access token of the process.
        if (!CheckTokenMembership (0, adminGroup, & admin)) {
            error = GetLastError ();
        }
    }

    // Centralized cleanup for all allocated resources.
    if (adminGroup) {
        FreeSid (adminGroup);
        adminGroup = 0;
    }

    // Throw the error if something failed in the function.
    if (ERROR_SUCCESS != error) throw error;

    return admin;
}

bool checkElevate () {
    bool result;

    if (isAdmin ()) {
        result = true;
    } else {
        char path [MAX_PATH];

        GetModuleFileName (0, path, sizeof (path));

        // Launch itself as admin
        SHELLEXECUTEINFO info;
        
        memset (& info, 0, sizeof (info));

        info.cbSize = sizeof (info);
        info.lpVerb = "runas";
        info.lpFile = path;
        info.nShow = SW_NORMAL;
        
        result = ShellExecuteEx (& info) == ERROR_SUCCESS;

        exit (0);
    }

    return result;
}

void showUsageAndExit () {
    printf ("USAGE:\n\n");
    exit (0);
}

void checkCreateFolder (const char *path) {
    if (!PathFileExists (path)) {
        wchar_t unicodePath [MAX_PATH];
        MultiByteToWideChar (CP_ACP, 0, path, -1, unicodePath, MAX_PATH);
        SHCreateDirectory (GetConsoleWindow (), unicodePath);
    }
}

void copyFolder (const char *dest, const char *source) {
    char sourcePath [MAX_PATH], destPath [MAX_PATH];
    char pattern [MAX_PATH];
    WIN32_FIND_DATA data;

    PathCombine (pattern, source, "*.*");

    auto find = FindFirstFile (pattern, & data);

    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (data.cFileName [0] == '.' && (data.cFileName [1] == '.' || data.cFileName [1] == '\0')) continue;

            PathCombine (sourcePath, source, data.cFileName);
            PathCombine (destPath, dest, data.cFileName);

            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                checkCreateFolder (destPath);
                copyFolder (destPath, sourcePath);
            } else {
                auto getFileVersion = [] (const char *path, uint32_t& low, uint32_t high) {
                    unsigned long dummy;
                    auto verBlockSize = GetFileVersionInfoSizeA (path, & dummy);
                    uint32_t size;
                    VS_FIXEDFILEINFO *buffer;

                    if (verBlockSize == 0) {
                        low = high = 0;
                    } else {
                        void *block = malloc (verBlockSize);

                        GetFileVersionInfo (path, dummy, verBlockSize, block);
                        VerQueryValue (block, "\\", (void **) & buffer, & size);

                        high = buffer->dwFileVersionMS;
                        low = buffer->dwFileVersionLS;

                        free (block);
                    }
                };
                auto getFileTime = [] (const char *path, FILETIME& fileTime) {
                    WIN32_FILE_ATTRIBUTE_DATA data;
                    if (GetFileAttributesEx (path, GetFileExInfoStandard, & data)) {
                        fileTime = data.ftLastWriteTime;
                    } else {
                        fileTime.dwHighDateTime = fileTime.dwLowDateTime = 0;
                    }
                };
                auto isVersionable = [] (const char *path) {
                    const char *extension = PathFindExtension (path);
                    static char *extentsionList [] {
                        ".exe",
                        ".com",
                        ".dll",
                        ".ttf",
                        ".ocx",
                        ".bin",
                    };
                    static size_t numOfExt = sizeof (extentsionList) / sizeof (*extentsionList);

                    for (auto i = 0; i < numOfExt; ++ i) {
                        if (stricmp (extension, extentsionList [i]) == 0) return true;
                    }
                    
                    return false;
                };
                auto isSourceIsNewerThanDest = [isVersionable, getFileTime, getFileVersion] (const char *sourcePath, const char *destPath) {
                    if (!PathFileExists (sourcePath)) return false;
                    if (!PathFileExists (destPath)) return true;

                    if (isVersionable (sourcePath) && isVersionable (destPath)) {
                        uint32_t sourceVerLow = 0;
                        uint32_t sourceVerHigh = 0;
                        uint32_t destVerLow = 0;
                        uint32_t destVerHigh = 0;
                        
                        getFileVersion (sourcePath, sourceVerLow, sourceVerHigh);
                        getFileVersion (destPath, destVerLow, destVerHigh);

                        uint64_t sourceVer = sourceVerLow + ((uint64_t) sourceVerHigh << 32);
                        uint64_t destVer = destVerLow + ((uint64_t) destVerHigh << 32);

                        if (sourceVer != destVer) return sourceVer > destVer;
                    }

                    FILETIME sourceTime;
                    FILETIME destTime;

                    getFileTime (sourcePath, sourceTime);
                    getFileTime (destPath, destTime);

                    uint64_t sourceTime64 = sourceTime.dwLowDateTime + ((uint64_t) sourceTime.dwHighDateTime << 32);
                    uint64_t destTime64 = destTime.dwLowDateTime + ((uint64_t) destTime.dwHighDateTime << 32);

                    return sourceTime64 > destTime64;
                };

                if (isSourceIsNewerThanDest (sourcePath, destPath)) {
                    printf ("Copying %s => %s...%s\n", sourcePath, destPath, CopyFile (sourcePath, destPath, FALSE) ? "ok" : "failed");
                } else {
                    printf ("Skipped %s.\n", sourcePath);
                }
            }
        } while (FindNextFile (find, & data));

        FindClose (find);
    }
}

std::string getFolderLocation (ITEMIDLIST *folder) {
    char path [MAX_PATH];
    
    SHGetPathFromIDList (folder, path);

    return std::string (path);
}

std::string browseForFolder (const char *title) {
    BROWSEINFO browseInfo;
    std::string result;
    
    memset (& browseInfo, 0, sizeof (browseInfo));
    
    browseInfo.hwndOwner = GetConsoleWindow ();
    browseInfo.lpszTitle = title ? title : "Please locate folder";
    browseInfo.ulFlags = BIF_NEWDIALOGSTYLE | BIF_RETURNONLYFSDIRS | BIF_USENEWUI;
    
    auto destFolder = SHBrowseForFolder (& browseInfo);
    
    if (destFolder) {
        IMalloc *mallocInterface;
        char path [MAX_PATH];
        
        SHGetPathFromIDList (destFolder, path);
        SHGetMalloc (& mallocInterface);
        
        mallocInterface->Free (destFolder);
        mallocInterface->Release ();

        result = path;
    }

    return result;
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

                copyFolder (destFolder.c_str (), PathCombine (path, cfg.sourceDir.c_str (), sourceSubFolder.c_str ()));
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