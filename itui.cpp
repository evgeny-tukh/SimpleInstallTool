#include <stdio.h>
#include <time.h>
#include <windows.h>
#include <commctrl.h>
#include <Shlwapi.h>
#include <vector>
#include <thread>
#include <memory.h>
#include "resource.h"
#include "cfg.h"
#include "setup.h"
#include "unzipTool.h"

struct Ctx {
    HINSTANCE instance;
    HBITMAP image;
    FILE *log;
    bool exiting, initialized, done, uinstallMode;
    std::string uninstPath;

    Ctx (HINSTANCE inst): exiting (false), initialized (false), instance (inst), done (false), uinstallMode (false), uninstPath () {
        image = LoadBitmap (instance, MAKEINTRESOURCE (IDB_SHIP));

        char path [MAX_PATH];

        GetModuleFileName (0, path, sizeof (path));
        PathRenameExtension (path, ".log");

        log = fopen (path, "wt");
    }
    virtual ~Ctx () {
        DeleteObject (image);

        if (log) fclose (log);
    }
    void logString (const char *text) {
        fprintf (log, "%s\n", text);
    }
    void flushLog () {
        fflush (log);
    }
    void closeLog () {
        fclose (log);
    }
};

void installProc (HWND wnd);
void uninstallProc (HWND wnd);

void doCommand (HWND wnd, uint16_t command) {
    Ctx *ctx = (Ctx *) GetWindowLongPtr (wnd, GWLP_USERDATA);

    if (!ctx->initialized) return;

    switch (command) {
        case IDC_CANCEL: {
            if (ctx->exiting || ctx->done || MessageBox (wnd, "Exit from the istallation?", "Question", MB_YESNO | MB_ICONQUESTION) == IDYES) DestroyWindow (wnd);
            break;
        }
        case IDC_INSTALL: {
            std::thread installer (ctx->uinstallMode ? uninstallProc : installProc, wnd);
            installer.detach ();
            break;
        }
    }
}

void paintWindow (HWND wnd) {
    PAINTSTRUCT data;
    Ctx *ctx = (Ctx *) GetWindowLongPtr (wnd, GWLP_USERDATA);
    HDC paintCtx = BeginPaint (wnd, & data);
    HDC tempCtx = CreateCompatibleDC (paintCtx);
    RECT client;

    GetClientRect (wnd, & client);
    SelectObject (paintCtx, GetStockObject (BLACK_PEN));
    Rectangle (paintCtx, 9, 9, 311, client.bottom - 9);
    SelectObject (tempCtx, ctx->image);
    BitBlt (paintCtx, 10, 10, 300, client.bottom - 20, tempCtx, 300, 100, SRCCOPY);
    SelectObject (tempCtx, (HBITMAP) 0);
    DeleteDC (tempCtx);
    EndPaint (wnd, & data);
}

void updateWindow (HWND wnd, bool installation, bool done, bool uninstall) {
    ShowWindow (GetDlgItem (wnd, IDC_PRG_GROUP), !uninstall && installation || done ? SW_HIDE : SW_SHOW);
    ShowWindow (GetDlgItem (wnd, IDC_PRG_GROUP_LBL), !uninstall && installation || done ? SW_HIDE : SW_SHOW);
    ShowWindow (GetDlgItem (wnd, IDC_RUN_LBL), !uninstall && installation || done ? SW_HIDE : SW_SHOW);
    ShowWindow (GetDlgItem (wnd, IDC_LOG), installation && !done ? SW_SHOW : SW_HIDE);
    ShowWindow (GetDlgItem (wnd, IDC_INSTALL), !installation && !done ? SW_SHOW : SW_HIDE);

    for (auto i = 0; i < 10; ++ i) {
        ShowWindow (GetDlgItem (wnd, IDC_FIRST_TASK + i), !uninstall && installation || done ? SW_HIDE : SW_SHOW);
    }
}

void initWindow (HWND wnd, void *data) {
    Cfg cfg;
    Ctx *ctx = (Ctx *) data;
    RECT client;

    GetClientRect (wnd, & client);

    auto createControl = [&wnd, &ctx] (const char *className, const char *text, uint32_t style, bool visible, int x, int y, int width, int height, uint64_t id) {
        style |= WS_CHILD;

        if (visible) style |= WS_VISIBLE;
        
        CreateWindow (className, text, style, x, y, width, height, wnd, (HMENU) id, ctx->instance, 0);
    };

    SetWindowLongPtr (wnd, GWLP_USERDATA, (LONG_PTR) data);

    char title [256];
    sprintf (title, "%s %sinstallation", cfg.getString (SECTION_MAIN, "appName").c_str (), ctx->uinstallMode ? "un" : "");
    SetWindowText (wnd, title);
    createControl ("BUTTON", ctx->uinstallMode ? "&Uninstall" : "&Install", WS_TABSTOP | BS_PUSHBUTTON | BS_DEFPUSHBUTTON, true, client.right - 220, client.bottom - 45, 100, 35, IDC_INSTALL);
    createControl ("BUTTON", "&Cancel", WS_TABSTOP | BS_PUSHBUTTON, true, client.right - 110, client.bottom - 45, 100, 35, IDC_CANCEL);
    createControl ("LISTBOX", "", WS_BORDER, false, 320, 10, client.right - 330, client.bottom - 55, IDC_LOG);
    createControl ("EDIT", cfg.getString (SECTION_SHORTCUTS, "DefProgramGroup").c_str (), WS_TABSTOP | WS_BORDER | ES_LEFT, !ctx->uinstallMode, 450, 10, client.right - 460, 25, IDC_PRG_GROUP);
    createControl ("STATIC", "Program group", SS_LEFT, !ctx->uinstallMode, 320, 10, 100, 25, IDC_PRG_GROUP_LBL);
    createControl ("STATIC", "Run after instalation the following tasks:", SS_LEFT, !ctx->uinstallMode, 320, 50, 400, 25, IDC_RUN_LBL);

    auto numOfTasks = cfg.getInt (SECTION_TASKS, "number");

    for (auto i = 1, y = 80; i <= numOfTasks; ++ i, y += 40) {
        auto info = cfg.getString (SECTION_TASKS, std::to_string (i).c_str ());

        std::vector<std::string> parts;

        splitLine (info, parts, ',');

        if (parts.size () > 0) {
            createControl ("BUTTON", parts [0].c_str (), WS_TABSTOP | BS_CHECKBOX | BS_AUTOCHECKBOX, !ctx->uinstallMode, 320, y, 400, 35, IDC_FIRST_TASK + i - 1);
        }
    }
    
    ctx->initialized = true;
}

LRESULT wndProc (HWND wnd, UINT msg, WPARAM param1, LPARAM param2) {
    LRESULT result = 0;

    switch (msg) {
        case WM_PAINT:
            paintWindow (wnd); break;
        case WM_COMMAND:
            doCommand (wnd, LOWORD (param1)); break;
        case WM_CREATE:
            initWindow (wnd, ((CREATESTRUCT *) param2)->lpCreateParams); break;
        case WM_DESTROY:
            PostQuitMessage (9); break;
        default:
            result = DefWindowProc (wnd, msg, param1, param2);
    }
    
    return result;
}

void registerClass (HINSTANCE instance) {
    WNDCLASS classInfo;

    memset (&classInfo, 0, sizeof (classInfo));

    classInfo.hbrBackground = (HBRUSH) GetStockObject (WHITE_BRUSH);
    classInfo.hCursor = (HCURSOR) LoadCursor (0, IDC_ARROW);
    classInfo.hIcon = (HICON) LoadIcon (instance, MAKEINTRESOURCE (IDI_SETUP));
    classInfo.hInstance = instance;
    classInfo.lpfnWndProc = wndProc;
    classInfo.lpszClassName = CLS_NAME;
    classInfo.style = CS_HREDRAW | CS_VREDRAW;

    RegisterClass (&classInfo);
}

void initCommonControls () {
    INITCOMMONCONTROLSEX data;
	
    data.dwSize = sizeof (INITCOMMONCONTROLSEX);
	data.dwICC = 0xFFFFFFFF;
	
    InitCommonControlsEx (& data);
}

bool isMsi (char *path) {
    char pathU [MAX_PATH];

    strcpy (pathU, path);
    strupr (pathU);

    return strstr (pathU, ".MSI") != 0;
}

void executeTaskAndWait (const char *tasksFolder, const char *cmd) {
    char *buffer = _strdup (cmd);
    char *param;
    char cmdPath [MAX_PATH];

    if (buffer [0] == '"') {
        param = strchr (buffer + 1, '"');

        if (param) {
            ++ param;
            while (*param == ' ') *(param++) = '\0';
        } else {
            // No closing quote
            return;
        }
    } else {
        param = strchr (buffer + 1, ' ');

        if (param) {
            while (*param == ' ') *(param++) = '\0';
        }
    }

    if (param && !*param) param = 0;

    PathCombine (cmdPath, tasksFolder, buffer);

    STARTUPINFO startupInfo;
    PROCESS_INFORMATION processInfo;
    memset (& startupInfo, 0, sizeof (startupInfo));
    memset (& processInfo, 0, sizeof (processInfo));

    startupInfo.cb = sizeof (startupInfo);
    
    unsigned long error;

    char workingFolder [MAX_PATH];

    strcpy (workingFolder, cmdPath);
    PathRemoveFileSpec (workingFolder);

    if (isMsi (cmdPath)) {
        SHELLEXECUTEINFO info;
        memset (& info, 0, sizeof (info));
        info.cbSize = sizeof (info);
        info.lpVerb = "open";
        info.lpFile = cmdPath;
        info.lpParameters = param;
        info.lpDirectory = workingFolder;
        info.nShow = SW_SHOW;
        info.fMask = SEE_MASK_NOCLOSEPROCESS;

        if (ShellExecute (0, "open", cmdPath, param, workingFolder, SW_SHOW)) {
            WaitForSingleObject (info.hProcess, INFINITE);
            CloseHandle (info.hProcess);
        }
        /*std::string msiPath (cmdPath);

        if (param) {
            msiPath += ' ';
            msiPath += param;
        }
        result = CreateProcess (0, (char *) msiPath.c_str (), 0, 0, 0, 0, 0, workingFolder, & startupInfo, & processInfo);*/
    } else {
        if (CreateProcess (cmdPath, param, 0, 0, 0, 0, 0, workingFolder, & startupInfo, & processInfo)) {
            WaitForSingleObject (processInfo.hProcess, INFINITE);
            CloseHandle (processInfo.hProcess);
            CloseHandle (processInfo.hThread);
        }
    }
}

void uninstallProc (HWND wnd) {
    Ctx *ctx = (Ctx *) GetWindowLongPtr (wnd, GWLP_USERDATA);
    Cfg cfg;
    FILE *journal = fopen (ctx->uninstPath.c_str (), "rb");

    auto addStringToLog = [&wnd, ctx] (const char *line) {
        auto item = SendDlgItemMessage (wnd, IDC_LOG, LB_ADDSTRING, 0, (LPARAM) line);
        SendDlgItemMessage (wnd, IDC_LOG, LB_SETCURSEL, item, 0);
        ctx->logString (line);
    };
    auto addErrorToLog = [&wnd, ctx, addStringToLog] (const char *line) {
        addStringToLog (line);
        MessageBox (wnd, line, ERROR_TEXT, MB_ICONSTOP);
    };

    updateWindow (wnd, true, false, true);

    if (journal) {
        char signature [2];

        if (fread (signature, 1, 2, journal) < 2) {
            addErrorToLog ("Installation journal is invalid");
        } else {
            std::vector<std::vector<uint8_t>> records;
            std::string appName;
            Journal::Rec record;
            char subject [MAX_PATH], chr = 0xFF;
            
            while (chr) {
                if (!fread (& chr, 1, 1, journal)) {
                    addErrorToLog ("Installation journal is invalid, no product name found");
                    fclose (journal);
                    return;
                }

                if (chr) appName += chr;
            }

            appName += " uninstallation";

            SetWindowText (wnd, appName.c_str ());

            while (fread (& record, sizeof (record) - 1, 1, journal) > 0) {
                memset (subject, 0, sizeof (subject));
                if (fread (subject, record.size - sizeof (record) + 1, 1, journal) == 0) {
                    addErrorToLog ("Unexpected end of the installation journal found");
                }

                records.emplace_back ();
                records.back ().insert (records.back ().end (), (uint8_t *) & record, ((uint8_t *) & record) + (sizeof (record) - 1));
                records.back ().insert (records.back ().end (), (uint8_t *) subject, ((uint8_t *) subject) + strlen (subject));
                records.back ().push_back (0);
            }

            for (auto rec = records.rbegin (); rec != records.rend (); ++ rec) {
                Journal::Rec *record = (Journal::Rec *) rec->data ();
                char msg [1000];
                char *objectType;

                switch (record->action) {
                    case Journal::Action::CREATE_DIR: objectType = "folder"; break;
                    case Journal::Action::COPY_FILE: objectType = "file"; break;
                    case Journal::Action::CREATE_SHORTCUT: objectType = "shortcut"; break;
                    default: continue;
                }

                switch (record->action) {
                    case Journal::Action::CREATE_DIR:
                    case Journal::Action::COPY_FILE:
                    case Journal::Action::CREATE_SHORTCUT: {
                        if (PathFileExists (record->subject)) {
                            sprintf (msg, "%s %s: %s", objectType, record->subject, DeleteFile (record->subject) ? "uninstalled" : "failed to uninstall");
                        } else {
                            sprintf (msg, "%s %s not found", objectType, subject);
                        }

                        auto item = SendDlgItemMessage (wnd, IDC_LOG, LB_ADDSTRING, 0, (LPARAM) msg);
                        SendDlgItemMessage (wnd, IDC_LOG, LB_SETCURSEL, item, 0);
                        addStringToLog (msg);
                        break;
                    }
                }
            }

            addStringToLog ("Unregistering application...");
            unregisterApp (cfg.getString (SECTION_MAIN, "appKey").c_str ());

            addStringToLog ("Installation done.");
            ctx->done = true;
            ctx->closeLog ();

            updateWindow (wnd, false, true, true);
            SetDlgItemText (wnd, IDC_CANCEL, "E&xit");
            MessageBox (wnd, "Uninstallation completed", "Information", MB_ICONINFORMATION);
        }
    } else {
        addErrorToLog ("Unable to open installation journal");
    }
}

void installProc (HWND wnd) {
    Ctx *ctx = (Ctx *) GetWindowLongPtr (wnd, GWLP_USERDATA);
    Cfg cfg;
    char workingDir [MAX_PATH];
    char scriptPath [MAX_PATH];
    char programGroup [100];
    auto log = GetDlgItem (wnd, IDC_LOG);
    auto prgGroupCtl = GetDlgItem (wnd, IDC_PRG_GROUP);

    char msg [200];
    auto now = time (0);
    sprintf (msg, "Started at %s", ctime (& now));
    ctx->logString (msg);

    auto numOfTasks = cfg.getInt (SECTION_TASKS, "number");
    std::vector<bool> taskFlags;

    for (auto i = 0; i < numOfTasks; ++ i) {
        taskFlags.push_back (IsDlgButtonChecked (wnd, IDC_FIRST_TASK + i) == BST_CHECKED);
    }

    CoInitialize (0);
    GetWindowText (prgGroupCtl, programGroup, sizeof (programGroup));
    cfg.programGroupName = programGroup;
    /*ShowWindow (prgGroupCtl, SW_HIDE);
    ShowWindow (log, SW_SHOW);
    UpdateWindow (log);*/
    updateWindow (wnd, true, false, false);

    GetModuleFileName (0, workingDir, sizeof (workingDir));
    PathRemoveFileSpec (workingDir);
    PathCombine (scriptPath, workingDir, "install.isc");

    cfg.script = scriptPath;
    cfg.defDestDir = cfg.getString (SECTION_MAIN, "defaultDestDir");
    cfg.sourceDir = cfg.getString (SECTION_MAIN, "sourceDir", (std::string (workingDir) + "\\sources").c_str ());

    /*if (MessageBox (GetConsoleWindow (), "Do you want to select non-standard destination folder?", "Destination", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        cfg.destDir = browseForFolder ("Select destination folder");

        if (cfg.destDir.empty ()) exit (0);
    } else*/ {
        char path [MAX_PATH];

        GetEnvironmentVariable ("ProgramFiles(x86)", path, sizeof (path));
        PathAppend (path, "MARIS");
        
        cfg.destDir = path;
    }

    auto addStringToLog = [&wnd, ctx] (const char *line) {
        auto item = SendDlgItemMessage (wnd, IDC_LOG, LB_ADDSTRING, 0, (LPARAM) line);
        SendDlgItemMessage (wnd, IDC_LOG, LB_SETCURSEL, item, 0);
        ctx->logString (line);
    };

    addStringToLog ("Extracting the package...");

    auto packagePath = cfg.getString (SECTION_MAIN, "package");

    if (packagePath.empty ()) {
        addStringToLog ("Package is not specified in the script."); return;
    }

    char tempPath [MAX_PATH];
    char tempFolder [100];
    GetTempPath (sizeof (tempPath), tempPath);
    PathAppend (tempPath, std::to_string (time (0)).c_str ());
    PathRenameExtension (tempPath, ".tmp");
    CreateDirectory (tempPath, 0);
    unzipArchive (packagePath.c_str (), false, true, tempPath, 0);

    addStringToLog ("Copying files...");

    auto numOfFileGroups = cfg.getInt (SECTION_FILE_GROUPS, "Number");

    for (auto i = 1; i <= numOfFileGroups; ++ i) {
        auto info = cfg.getString (SECTION_FILE_GROUPS, std::to_string (i).c_str ());

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

                copyFolder (destFolder.c_str (), PathCombine (path, tempPath, sourceSubFolder.c_str ()), addStringToLog);
            }
        }
    }

    bool copySetupTool = cfg.getInt (SECTION_FILE_GROUPS, "copySetupTool");
    std::string uninstPath = cfg.getString (SECTION_MAIN, "uninstPath");

    if (copySetupTool) copySetupToolTo (uninstPath.c_str ());

    addStringToLog ("Creating shortcuts...");

    auto numOfShortcuts = cfg.getInt (SECTION_SHORTCUTS, "Number");

    for (auto i = 1; i <= numOfShortcuts; ++ i) {
        auto info = cfg.getString (SECTION_SHORTCUTS, std::to_string (i).c_str ());
        char buffer [1000];
        std::vector<std::string> parts;

        info = expandPath (info.c_str (), buffer, & cfg);
        /*if (strnicmp (info.c_str (), STARTUP, STARTUP_LEN) == 0) {
            SHGetFolderPath (GetConsoleWindow (), CSIDL_COMMON_STARTUP, 0, SHGFP_TYPE_DEFAULT, buffer);
            std::string temp (buffer);

            temp += info.substr (STARTUP_LEN);
            info = temp;
        } else if (strnicmp (info.c_str (), PRGGRP, PRGGRP_LEN) == 0) {
            SHGetFolderPath (GetConsoleWindow (), CSIDL_COMMON_PROGRAMS, 0, SHGFP_TYPE_DEFAULT, buffer);
            PathAppend (buffer, programGroup);
            std::string temp (buffer);

            temp += info.substr (PRGGRP_LEN);
            info = temp;
        }*/

        ExpandEnvironmentStrings (info.c_str (), buffer, sizeof (buffer));

        info = buffer;

        splitLine (info, parts, ',');

        if (parts.size () > 2) {
            auto& shortcutPath = parts [0];
            auto& cmd = parts [1];
            auto& desc = parts [2];

            makeShortcut (cmd.c_str (), 0, shortcutPath.c_str (), desc.c_str ());
        }
    }

    if (cfg.getInt (SECTION_SHORTCUTS, "addUninstShortcut")) {
        addUninstallShortcut (& cfg);
    }

    addStringToLog ("Executing tasks...");

    for (auto i = 1, y = 80; i <= numOfTasks; ++ i, y += 40) {
        auto info = cfg.getString (SECTION_TASKS, std::to_string (i).c_str ());

        std::vector<std::string> parts;

        splitLine (info, parts, ',');

        char tasksFolder [MAX_PATH];
        PathCombine (tasksFolder, tempPath, SECTION_TASKS);
        
        if (parts.size () > 0) {
            if (taskFlags [i-1]) {
                addStringToLog (parts [0].c_str ());

                executeTaskAndWait (tasksFolder, parts [1].c_str ());
            }
        }
    }

    addStringToLog ("Registering application...");
    std::string location = cfg.getString (SECTION_MAIN, "uninstPath").c_str ();
    char uninstCmd [MAX_PATH];
    PathCombine (uninstCmd, location.c_str (), "setup.exe");
    strcat (uninstCmd, " -uninst");
    registerApp (
        cfg.getString (SECTION_MAIN, "appKey").c_str (),
        cfg.getString (SECTION_MAIN, "appName").c_str (),
        cfg.getString (SECTION_MAIN, "appIcon").c_str (),
        uninstCmd,
        location.c_str (),
        cfg.getString (SECTION_MAIN, "publisher").c_str (),
        cfg.getInt (SECTION_MAIN, "verMajor"),
        cfg.getInt (SECTION_MAIN, "verMinor")
    );

    addStringToLog ("Installation done.");
    ctx->done = true;
    ctx->flushLog ();

    if (copySetupTool) copyJournalTo (uninstPath.c_str ());

    updateWindow (wnd, false, true, false);
    SetDlgItemText (wnd, IDC_CANCEL, "E&xit");
    MessageBox (wnd, "Installation completed", "Information", MB_ICONINFORMATION);
}

int APIENTRY WinMain (HINSTANCE instance, HINSTANCE prev, char *cmdLine, int showCmd) {
    Ctx ctx (instance);

    static char const *NO_ELEVATION = "-NOELEVATION";
    static char const *UNINST = "-UNINST";
    static char const *UNINST2 = "/UNINST";

    char *cmdLineUpper = strupr (_strdup (cmdLine));

    if (strstr (cmdLineUpper, NO_ELEVATION) == 0) checkElevate (& ctx.exiting, cmdLine);
 
    ctx.uinstallMode = strstr (cmdLineUpper, UNINST) || strstr (cmdLineUpper, UNINST2);

    if (ctx.uinstallMode) {
        char path [MAX_PATH];

        GetModuleFileName (0, path, sizeof (path));
        PathRemoveFileSpec (path);
        PathAppend (path, "install.jou");

        ctx.uninstPath = path;
    }

    free (cmdLineUpper);

    INITCOMMONCONTROLSEX ctlData { sizeof (INITCOMMONCONTROLSEX), ICC_WIN95_CLASSES };

    CoInitialize (0);
    InitCommonControlsEx (& ctlData);
    registerClass (instance);

    auto mainWnd = CreateWindow (
        CLS_NAME,
        "Installation Tool",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        900,
        600,
        0,
        0,
        instance,
        & ctx
    );

    ShowWindow (mainWnd, SW_SHOW);
    UpdateWindow (mainWnd);

    MSG msg;

    while (GetMessage (&msg, 0, 0, 0)) {
        TranslateMessage (&msg);
        DispatchMessage (&msg);
    }
}