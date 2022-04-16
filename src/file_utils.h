#ifndef FILE_UTILS_H_
#define FILE_UTILS_H_

#ifdef _WIN32
#include <windows.h>
#include <tchar.h>
typedef TCHAR tchar_t;
#else
#include <unistd.h>
#include "fake_tchar.h"
typedef char tchar_t;
#endif

#include <stdint.h>

#if defined(_WIN32) && defined(_UNICODE)
#include "utf8_win.h"
#    define utf8_to_tchar utf8_to_wstr
#    define tchar_to_utf8 wstr_to_utf8
#    define free_conv_result(x) free((void *) x)
#else
#    define utf8_to_tchar(x) (x)
#    define tchar_to_utf8(x) (x)
#    define free_conv_result(x)
#endif

int is_reg(const tchar_t *path);
int is_dir(const tchar_t *path);

#ifdef _WIN32
typedef uint64_t filetime_t;
#else
#include <time.h>
typedef time_t filetime_t;
#endif

int is_reg_newer(const tchar_t *path, filetime_t ft);
filetime_t get_current_filetime();

int delete_file(const tchar_t *path);
int create_directory(const tchar_t *path);

const char *basename(const char *path);

#ifdef _WIN32
const tchar_t *tbasename(const tchar_t *path);
#else
#define tbasename basename
#endif

#endif /* FILE_UTILS_H_ */
