#ifdef _WIN32

#include "utf8_win.h"

#include <string.h>
#include <stdlib.h>
#include <assert.h>

char *wstr_to_utf8(const WCHAR *ws)
{
    size_t in_size = wcslen(ws);
    int out_size = WideCharToMultiByte(CP_UTF8, 0, ws, in_size + 1, NULL, 0, NULL, NULL);
    if (out_size <= 0) return NULL;
    char *s = (char *) malloc(out_size);
    if (!s) return NULL;
    out_size = WideCharToMultiByte(CP_UTF8, 0, ws, in_size + 1, s, out_size, NULL, NULL);
    if (out_size <= 0)
    {
        assert(0);
        free(s);
        s = NULL;
    }
    return s;
}

WCHAR *utf8_to_wstr(const char *s)
{
    size_t in_size = strlen(s);
    int out_size = MultiByteToWideChar(CP_UTF8, 0, s, in_size + 1, NULL, 0);
    if (out_size <= 0) return NULL;
    WCHAR *ws = (WCHAR *) malloc(out_size * sizeof(WCHAR));
    if (!ws) return NULL;
    out_size = MultiByteToWideChar(CP_UTF8, 0, s, in_size + 1, ws, out_size);
    if (out_size <= 0)
    {
        assert(0);
        free(ws);
        ws = NULL;
    }
    return ws;
}

#endif /* _WIN32 */
