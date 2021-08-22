#pragma once

int unzipArchive (
    const char *path,
    const bool updateOnly,
    const bool extractFullPath,
    const char *destPath,
    void (*cb) (size_t extracted, size_t total, int state)
);
