#include "scan_dir.h"
#include <string.h>

void scan_dir_pattern(const tchar_t *path, const tchar_t *pattern, scan_dir_func_t func, void *context, int max_depth)
{
    WIN32_FIND_DATA data;
    size_t path_len = _tcslen(path);
    size_t pattern_len = _tcslen(pattern);

    tchar_t *tmp = (tchar_t *) malloc((path_len + pattern_len + 2) * sizeof(tchar_t) + sizeof(data.cFileName));
    memcpy(tmp, path, path_len * sizeof(tchar_t));
    if (tmp[path_len-1] != _T('\\'))
        tmp[path_len++] = _T('\\');
    memcpy(tmp + path_len, pattern, (pattern_len + 1) * sizeof(tchar_t));

    HANDLE handle = FindFirstFile(tmp, &data);
    if (handle != INVALID_HANDLE_VALUE)
    {
        while (1)
        {
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            { 
                if (max_depth && _tcscmp(data.cFileName, _T("..")) && _tcscmp(data.cFileName, _T(".")))
                {
                    _tcscpy(tmp + path_len, data.cFileName);
                    scan_dir(tmp, func, context, max_depth - 1);
                }
            }  
            else
            {
                _tcscpy(tmp + path_len, data.cFileName);
                func(context, tmp);
            }
            if (!FindNextFile(handle, &data))
                break;
        }
        FindClose(handle);
    }
    free(tmp);
}

void scan_dir(const tchar_t *path, scan_dir_func_t func, void *context, int max_depth)
{
    scan_dir_pattern(path, _T("*.*"), func, context, max_depth);
}
