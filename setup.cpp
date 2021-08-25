#include <Windows.h>
#include <Shlobj.h>
#include <Shlwapi.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string>
#include <vector>
#include <functional>
#include <time.h>
#include "unzipTool.h"

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

bool makeShortcut (const char *cmd, const char *link, const char *desc) { 
    IShellLink* shellLnkInterf; 
    bool result = true;

    // Get a pointer to the IShellLink interface. It is assumed that CoInitialize
    // has already been called.
    auto hr = CoCreateInstance (CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (void **) & shellLnkInterf);
    if (SUCCEEDED (hr)) { 
        IPersistFile *fileInterf; 

        // Set the path to the shortcut target and add the description. 
        shellLnkInterf->SetPath (cmd); 
        shellLnkInterf->SetDescription (desc); 

        // Query IShellLink for the IPersistFile interface, used for saving the 
        // shortcut in persistent storage. 
        if (SUCCEEDED (shellLnkInterf->QueryInterface (IID_IPersistFile, (void **) & fileInterf))) { 
            WCHAR uncodePath [MAX_PATH];
            char path [MAX_PATH];

            //SHGetFolderPath (GetConsoleWindow (), CSIDL_COMMON_PROGRAMS, 0, SHGFP_TYPE_DEFAULT, path);
            //PathAppend (path, link);
            strcpy (path, link);

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
            result = SUCCEEDED (fileInterf->Save (uncodePath, TRUE));

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

bool isAdmin () {
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

bool checkElevate (bool *exiting) {
    bool result;

    if (isAdmin ()) {
        if (exiting) *exiting = false;
        
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

        if (exiting) *exiting = true;

        exit (0);
    }

    return result;
}

void checkCreateFolder (const char *path) {
    if (!PathFileExists (path)) {
        wchar_t unicodePath [MAX_PATH];
        MultiByteToWideChar (CP_ACP, 0, path, -1, unicodePath, MAX_PATH);
        SHCreateDirectory (GetConsoleWindow (), unicodePath);
    }
}

void copyFolder (const char *dest, const char *source, std::function<void (char *msg)> cb) {
    char sourcePath [MAX_PATH], destPath [MAX_PATH];
    char pattern [MAX_PATH];
    char msg [1000];
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
                copyFolder (destPath, sourcePath, cb);
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
                    sprintf (msg, "Copying %s...%s.", destPath, CopyFile (sourcePath, destPath, FALSE) ? "ok" : "failed");
                } else {
                    sprintf (msg, "Skipped %s.", sourcePath);
                }

                if (cb) cb (msg);
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

void registerApp (
    const char *appKey,
    const char *appName,
    const char *uninstCmd,
    const char *location,
    const char *publisher,
    u_long verMajor,
    u_long verMinor
) {
    HKEY uninstallKey, productKey;
    char locationCopy [MAX_PATH];
    char uninstCmdCopy[MAX_PATH];

    ExpandEnvironmentStrings (location, locationCopy, sizeof (locationCopy));
    ExpandEnvironmentStrings (uninstCmd, uninstCmdCopy, sizeof (uninstCmdCopy));

    if (RegOpenKeyEx (HKEY_LOCAL_MACHINE, "Software\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall", 0, KEY_ALL_ACCESS, & uninstallKey) == ERROR_SUCCESS) {
        if (RegCreateKeyEx (uninstallKey, appKey, 0, 0, 0, KEY_ALL_ACCESS, 0, & productKey, 0) == ERROR_SUCCESS) {
            RegSetValueEx (productKey, "DisplayName", 0, REG_SZ, (const BYTE *) appName, strlen (appName) + 1);
            RegSetValueEx (productKey, "UninstallPath", 0, REG_EXPAND_SZ, (const BYTE *) uninstCmdCopy, strlen (uninstCmdCopy) + 1);
            RegSetValueEx (productKey, "InstallLocation", 0, REG_EXPAND_SZ, (const BYTE *) locationCopy, strlen (locationCopy) + 1);
            RegSetValueEx (productKey, "Publisher", 0, REG_SZ, (const BYTE *) publisher, strlen (publisher) + 1);
            RegSetValueEx (productKey, "VersionMajor", 0, REG_DWORD, (const BYTE *) & verMajor, sizeof (verMajor));
            RegSetValueEx (productKey, "VersionMinor", 0, REG_DWORD, (const BYTE *) & verMinor, sizeof (verMinor));
            RegCloseKey (productKey);
        }

        RegCloseKey (uninstallKey);
    }
}