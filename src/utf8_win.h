#ifndef UTF8_WIN_H_
#define UTF8_WIN_H_

#include <windows.h>

char *wstr_to_utf8(const WCHAR *ws);
WCHAR *utf8_to_wstr(const char *s);

#endif /* UTF8_WIN_H_ */
