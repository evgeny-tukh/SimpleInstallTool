#pragma once

#include <Windows.h>
#include <Shlobj.h>
#include <Shlwapi.h>
#include <string>
#include <vector>
#include <functional>

#pragma pack(1)
namespace Journal {
    enum Action {
        COPY_FILE = 'F',
        CREATE_DIR = 'D',
        CREATE_SHORTCUT = 'S',
    };
    struct Rec {
        uint32_t size;
        uint8_t action;
        char subject [1];
    };
}
#pragma pack()

void splitLine (std::string& line, std::vector<std::string>& parts, char separator);
bool makeShortcut (const char *cmd, const char *args, const char *link, const char *desc);
bool isAdmin ();
bool checkElevate (bool *exiting = 0, char *cmdLine = 0);
void checkCreateFolder (const char *path);
void copyFolder (const char *dest, const char *source, std::function<void (char *msg)> cb);
std::string getFolderLocation (ITEMIDLIST *folder);
std::string browseForFolder (const char *title);
void registerAction (Journal::Action action, char *subject);
void unregisterApp (const char *appKey);
void registerApp (
    const char *appKey,
    const char *appName,
    const char *appIcon,
    const char *uninstCmd,
    const char *location,
    const char *publisher,
    u_long verMajor,
    u_long verMinor
);
bool copySetupToolTo (const char *dest);
bool copyJournalTo (const char *dest);
char *expandPath (const char *path, char *dest, struct Cfg *cfg);
void addUninstallShortcut (Cfg *cfg);
char *getProgramGroupPath (struct Cfg *cfg, char *dest);

extern const char *CLS_NAME;
extern const char *SECTION_MAIN;
extern const char *SECTION_TASKS;
extern const char *SECTION_FILE_GROUPS;
extern const char *SECTION_SHORTCUTS;
extern const char *ERROR_TEXT;
extern const char *STARTUP;
extern const char *PRGGRP;
extern const size_t STARTUP_LEN;
extern const size_t PRGGRP_LEN;
extern const char *UNINST_SHORTCUT;
extern const char *UNINST_JOURNAL;