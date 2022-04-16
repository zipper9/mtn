#ifndef SCAN_DIR_H_
#define SCAN_DIR_H_

#include "file_utils.h"

typedef void (*scan_dir_func_t)(void *context, const tchar_t *path);

void scan_dir(const tchar_t *path, scan_dir_func_t func, void *context, int max_depth);

#ifdef _WIN32
void scan_dir_pattern(const tchar_t *path, const tchar_t *pattern, scan_dir_func_t func, void *context, int max_depth);
#endif

#endif /* SCAN_DIR_H_ */
