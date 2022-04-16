#include "file_utils.h"

#ifndef _WIN32
#include <sys/stat.h>
#include <string.h>
#endif

int is_reg(const tchar_t *file)
{
#ifdef _WIN32
    DWORD attr = GetFileAttributes(file);
    if (attr == INVALID_FILE_ATTRIBUTES) return 0;
    return (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
#else
    struct stat buf;
    if (stat(file, &buf)) return 0;
    return S_ISREG(buf.st_mode);
#endif
}

int is_dir(const tchar_t *file)
{
#ifdef _WIN32
    DWORD attr = GetFileAttributes(file);
    if (attr == INVALID_FILE_ATTRIBUTES) return 0;
    return (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat buf;
    if (stat(file, &buf)) return 0;
    return S_ISDIR(buf.st_mode);
#endif
}

int is_reg_newer(const tchar_t *path, filetime_t ft)
{
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesEx(path, GetFileExInfoStandard, &data) || (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        return 0;
    uint64_t ft2 = *(const uint64_t *) &data.ftLastWriteTime;
    return ft2 >= ft;
#else
    struct stat buf;
    if (stat(path, &buf))
        return 0;
    return S_ISREG(buf.st_mode) && buf.st_mtime >= ft;
#endif
}

#ifdef _WIN32
filetime_t get_current_filetime()
{
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return (uint64_t) ft.dwHighDateTime << 32 | ft.dwHighDateTime;
}
#else
filetime_t get_current_filetime()
{
    return time(NULL);
}
#endif

int create_directory(const tchar_t *name)
{
#ifdef _WIN32
    return CreateDirectory(name, NULL) ? 0 : -1;
#else
    return mkdir(name, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
#endif
}

int delete_file(const tchar_t *name)
{
#ifdef _WIN32
    return DeleteFile(name) ? 0 : -1;
#else
    return unlink(name);
#endif
}

const char *basename(const char *path)
{
    if (!path || !*path)
        return path;
#ifdef _WIN32
    size_t len = strlen(path);
    while (len)
    {
        if (path[len-1] == '\\' || path[len-1] == '/')
            return path + len;
        len--;
    }
    return path;
#else
    const char *sep = strrchr(path, '/');
    return sep ? sep + 1 : path;
#endif
}

#ifdef _WIN32
const tchar_t *tbasename(const tchar_t *path)
{
    if (!path || !*path)
        return path;
    size_t len = _tcslen(path);
    while (len)
    {
        if (path[len-1] == _T('\\') || path[len-1] == _T('/'))
            return path + len;
        len--;
    }
    return path;
}
#endif
