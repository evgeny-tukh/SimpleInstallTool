#include <stdio.h>
#include <time.h>
#include <windows.h>
#include <commctrl.h>
#include <Shlwapi.h>
#include <vector>
#include <thread>
#include "resource.h"
#include "cfg.h"
#include "setup.h"
#include "unzipTool.h"

const char *CLS_NAME = "SiInToWi";

struct Ctx {
    HINSTANCE instance;
    HBITMAP image;
    FILE *log;
    bool exiting, initialized, done;

    Ctx (HINSTANCE inst): exiting (false), initialized (false), instance (inst), done (false) {
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
};

void installProc (HWND wnd);

void doCommand (HWND wnd, uint16_t command) {
    Ctx *ctx = (Ctx *) GetWindowLongPtr (wnd, GWLP_USERDATA);

    if (!ctx->initialized) return;

    switch (command) {
        case IDC_CANCEL: {
            if (ctx->exiting || ctx->done || MessageBox (wnd, "Exit from the istallation?", "Question", MB_YESNO | MB_ICONQUESTION) == IDYES) DestroyWindow (wnd);
            break;
        }
        case IDC_INSTALL: {
            std::thread installer (installProc, wnd);
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

void updateWindow (HWND wnd, bool installation, bool done) {
    ShowWindow (GetDlgItem (wnd, IDC_PRG_GROUP), installation || done ? SW_HIDE : SW_SHOW);
    ShowWindow (GetDlgItem (wnd, IDC_PRG_GROUP_LBL), installation || done ? SW_HIDE : SW_SHOW);
    ShowWindow (GetDlgItem (wnd, IDC_RUN_LBL), installation || done ? SW_HIDE : SW_SHOW);
    ShowWindow (GetDlgItem (wnd, IDC_LOG), installation && !done ? SW_SHOW : SW_HIDE);
    ShowWindow (GetDlgItem (wnd, IDC_INSTALL), !installation && !done ? SW_SHOW : SW_HIDE);

    for (auto i = 0; i < 10; ++ i) {
        ShowWindow (GetDlgItem (wnd, IDC_FIRST_TASK + i), installation || done ? SW_HIDE : SW_SHOW);
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
    createControl ("BUTTON", "&Install", WS_TABSTOP | BS_PUSHBUTTON | BS_DEFPUSHBUTTON, true, client.right - 220, client.bottom - 45, 100, 35, IDC_INSTALL);
    createControl ("BUTTON", "&Cancel", WS_TABSTOP | BS_PUSHBUTTON, true, client.right - 110, client.bottom - 45, 100, 35, IDC_CANCEL);
    createControl ("LISTBOX", "", WS_BORDER, false, 320, 10, client.right - 330, client.bottom - 55, IDC_LOG);
    createControl ("EDIT", cfg.getString ("Shortcuts", "DefProgramGroup").c_str (), WS_TABSTOP | WS_BORDER | ES_LEFT, true, 450, 10, client.right - 460, 25, IDC_PRG_GROUP);
    createControl ("STATIC", "Program group", SS_LEFT, true, 320, 10, 100, 25, IDC_PRG_GROUP_LBL);
    createControl ("STATIC", "Run after instalation the following tasks:", SS_LEFT, true, 320, 50, 400, 25, IDC_RUN_LBL);

    auto numOfTasks = cfg.getInt ("Tasks", "number");

    for (auto i = 1, y = 80; i <= numOfTasks; ++ i, y += 40) {
        auto info = cfg.getString ("Tasks", std::to_string (i).c_str ());

        std::vector<std::string> parts;

        splitLine (info, parts, ',');

        if (parts.size () > 0) {
            createControl ("BUTTON", parts [0].c_str (), WS_TABSTOP | BS_CHECKBOX | BS_AUTOCHECKBOX, true, 320, y, 400, 35, IDC_FIRST_TASK + i - 1);
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

void installProc (HWND wnd) {
    Ctx *ctx = (Ctx *) GetWindowLongPtr (wnd, GWLP_USERDATA);
    Cfg cfg;
    char workingDir [MAX_PATH];
    char scriptPath [MAX_PATH];
    char programGroup [100];
    auto log = GetDlgItem (wnd, IDC_LOG);
    auto prgGroup = GetDlgItem (wnd, IDC_PRG_GROUP);

    char msg [200];
    auto now = time (0);
    sprintf (msg, "Started at %s", ctime (& now));
    ctx->logString (msg);

    auto numOfTasks = cfg.getInt ("Tasks", "number");
    std::vector<bool> taskFlags;

    for (auto i = 0; i < numOfTasks; ++ i) {
        taskFlags.push_back (IsDlgButtonChecked (wnd, IDC_FIRST_TASK + i) == BST_CHECKED);
    }

    CoInitialize (0);
    GetWindowText (prgGroup, programGroup, sizeof (programGroup));
    ShowWindow (prgGroup, SW_HIDE);
    ShowWindow (log, SW_SHOW);
    UpdateWindow (log);
    updateWindow (wnd, true, false);

    GetModuleFileName (0, workingDir, sizeof (workingDir));
    PathRemoveFileSpec (workingDir);
    PathCombine (scriptPath, workingDir, "install.isc");

    cfg.script = scriptPath;
    cfg.defDestDir = cfg.getString ("main", "defaultDestDir");
    cfg.sourceDir = cfg.getString ("main", "sourceDir", (std::string (workingDir) + "\\sources").c_str ());

    /*if (MessageBox (GetConsoleWindow (), "Do you want to select non-standard destination folder?", "Destination", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        cfg.destDir = browseForFolder ("Select destination folder");

        if (cfg.destDir.empty ()) exit (0);
    } else*/ {
        char path [MAX_PATH];

        GetEnvironmentVariable ("ProgramFiles(x86)", path, sizeof (path));
        PathAppend (path, "CAIM");
        
        cfg.destDir = path;
    }

    auto addStringToLog = [&wnd, ctx] (const char *line) {
        auto item = SendDlgItemMessage (wnd, IDC_LOG, LB_ADDSTRING, 0, (LPARAM) line);
        SendDlgItemMessage (wnd, IDC_LOG, LB_SETCURSEL, item, 0);
        ctx->logString (line);
    };

    addStringToLog ("Extracting the package...");

    auto packagePath = cfg.getString ("Main", "package");

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

                copyFolder (destFolder.c_str (), PathCombine (path, tempPath, sourceSubFolder.c_str ()), addStringToLog);
            }
        }
    }

    addStringToLog ("Creating shortcuts...");

    auto numOfShortcuts = cfg.getInt ("Shortcuts", "Number");

    for (auto i = 1; i <= numOfShortcuts; ++ i) {
        auto info = cfg.getString ("Shortcuts", std::to_string (i).c_str ());
        char buffer [1000];
        std::vector<std::string> parts;

        static const char *STARTUP = "__statrtup__";
        static const char *PRGGRP = "__prggrp__";
        static size_t STARTUP_LEN = strlen (STARTUP);
        static size_t PRGGRP_LEN = strlen (PRGGRP);

        if (strnicmp (info.c_str (), STARTUP, STARTUP_LEN) == 0) {
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

    addStringToLog ("Executing tasks...");

    for (auto i = 1, y = 80; i <= numOfTasks; ++ i, y += 40) {
        auto info = cfg.getString ("Tasks", std::to_string (i).c_str ());

        std::vector<std::string> parts;

        splitLine (info, parts, ',');

        char tasksFolder [MAX_PATH];
        PathCombine (tasksFolder, tempPath, "tasks");
        
        if (parts.size () > 0) {
            if (taskFlags [i-1]) {
                addStringToLog (parts [0].c_str ());

                executeTaskAndWait (tasksFolder, parts [1].c_str ());
            }
        }
    }

    addStringToLog ("Registering application...");
    std::string location = cfg.getString ("Main", "uninstPath").c_str ();
    char uninstPath [MAX_PATH];
    PathCombine (uninstPath, location.c_str (), "uninst.exe");
    registerApp (
        cfg.getString ("Main", "appKey").c_str (),
        cfg.getString ("Main", "appName").c_str (),
        uninstPath,
        location.c_str (),
        cfg.getString ("Main", "publisher").c_str (),
        cfg.getInt ("Main", "verMajor"),
        cfg.getInt ("Main", "verMinor")
    );

    addStringToLog ("Installation done.");
    ctx->done = true;
    ctx->flushLog ();

    updateWindow (wnd, false, true);
    SetDlgItemText (wnd, IDC_CANCEL, "E&xit");
    MessageBox (wnd, "Installation completed", "Information", MB_ICONINFORMATION);
}

int APIENTRY WinMain (HINSTANCE instance, HINSTANCE prev, char *cmdLine, int showCmd) {
    Ctx ctx (instance);

    checkElevate (& ctx.exiting);

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