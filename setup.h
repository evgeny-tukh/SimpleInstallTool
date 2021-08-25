#pragma once

#include <Windows.h>
#include <Shlobj.h>
#include <Shlwapi.h>
#include <string>
#include <vector>
#include <functional>

void splitLine (std::string& line, std::vector<std::string>& parts, char separator);
bool makeShortcut (const char *cmd, const char *link, const char *desc);
bool isAdmin ();
bool checkElevate (bool *exiting = 0);
void checkCreateFolder (const char *path);
void copyFolder (const char *dest, const char *source, std::function<void (char *msg)> cb);
std::string getFolderLocation (ITEMIDLIST *folder);
std::string browseForFolder (const char *title);

void registerApp (
    const char *appKey,
    const char *appName,
    const char *uninstCmd,
    const char *location,
    const char *publisher,
    u_long verMajor,
    u_long verMinor
);