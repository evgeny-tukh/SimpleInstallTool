#include <stdlib.h>
#include <stdio.h>
#include <ShlObj.h>
#include <Shlwapi.h>
#include <stdint.h>
#include "zlib\\zip.h"
#include "zlib\\unzip.h"
#include "zlib\\iowin32.h"

bool checkIfFileFolderExists (char *path)
{
    char folder [MAX_PATH];

    strcpy (folder, path);
    PathRemoveFileSpec (folder);

    auto error = SHCreateDirectoryExA (0, folder, 0);
    
    return error == ERROR_SUCCESS || error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS;
}

int unzipArchive (
    const char *path,
    const bool updateOnly,
    const bool extractFullPath,
    const char *destPath,
    void (*cb) (size_t extracted, size_t total, int state)
) {
    unz_global_info globalInfo;
    unz_file_info fileInfo;
    zlib_filefunc_def funcDef;
    char archiveName [MAX_PATH];
    char                //chDrive     [_MAX_DRIVE],
                        //chDir       [_MAX_DIR],
                        chName        [_MAX_FNAME],
                        chExt         [_MAX_EXT],
                        chFileTitle   [MAX_PATH],
                        chDestPath    [MAX_PATH],
                        chArchiveName [MAX_PATH],
                        fileName    [256];
    int actualSize, filesExtracted = 0;
    unsigned long bytesWritten;
    
    fill_win32_filefunc (& funcDef);
    strncpy (archiveName, path, sizeof (archiveName));
    PathRenameExtension (archiveName, ".zip");

    auto file = unzOpen2 (archiveName, & funcDef);
    
    if (file) {
        if (unzGetGlobalInfo (file, & globalInfo) == UNZ_OK) {
            for (auto i = 0; i < globalInfo.number_entry; ++ i) {
                char fileName [MAX_PATH];
                
                // Get next file
                unzGetCurrentFileInfo (file, & fileInfo, fileName, sizeof (fileName), NULL, 0, NULL, 0);

				for (auto j = 0; fileName [j] && j < sizeof (fileName); ++ j) {
					if (fileName [j] == '/')
						fileName [j] = '\\';
				}

                if (cb) cb (i, globalInfo.number_entry, 1);
                    
                if ((fileInfo.external_fa & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                    auto buffer = malloc (fileInfo.uncompressed_size);
                    char dest [MAX_PATH];

                    if (!extractFullPath) PathStripPath (fileName);
                    PathCombine (dest, destPath, fileName);
                    checkIfFileFolderExists (dest);
                    
                    if (updateOnly) {
                        WIN32_FIND_DATA findData;
                        auto find = FindFirstFileA (dest, & findData);
                        
                        if (find != INVALID_HANDLE_VALUE) {
                            SYSTEMTIME sysTime;
                            FILETIME fileTime;

                            sysTime.wYear = fileInfo.tmu_date.tm_year;
                            sysTime.wMonth = fileInfo.tmu_date.tm_mon + 1;
                            sysTime.wDay = fileInfo.tmu_date.tm_mday;
                            sysTime.wHour = fileInfo.tmu_date.tm_hour;
                            sysTime.wMinute = fileInfo.tmu_date.tm_min;
                            sysTime.wSecond = fileInfo.tmu_date.tm_sec;
                            sysTime.wMilliseconds = sysTime.wDayOfWeek = 0;

                            SystemTimeToFileTime (& sysTime, & fileTime);

                            if (fileTime.dwHighDateTime < findData.ftLastWriteTime.dwHighDateTime ||
                                fileTime.dwHighDateTime == findData.ftLastWriteTime.dwHighDateTime &&
                                fileTime.dwLowDateTime < findData.ftLastWriteTime.dwLowDateTime
                            ) {
                                *dest = '\0';
                            }
                                
                            FindClose (find);
                        }
                    }
                    
                    if (*dest) {
                        auto dataFile = CreateFileA (dest, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);

                        if (dataFile != INVALID_HANDLE_VALUE) {
                            unzOpenCurrentFile (file);
                            
                            actualSize = unzReadCurrentFile (file, buffer, fileInfo.uncompressed_size);
                            
                            if (cb) cb (i, globalInfo.number_entry, 2);

                            WriteFile (dataFile, buffer, actualSize, & bytesWritten, NULL);
                            CloseHandle (dataFile);

                            if (cb) cb (i, globalInfo.number_entry, 3);
                        
                            ++ filesExtracted;
                        }
                    }
                    
                    free (buffer);
                }
                
                if (i < globalInfo.number_entry - 1) unzGoToNextFile (file);
            }
        }
        unzClose (file);
    }
    
    return filesExtracted;
}
