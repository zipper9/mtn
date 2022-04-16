#include "scan_dir.h"
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

void scan_dir(const char *path, scan_dir_func_t func, void *context, int max_depth)
{
    DIR *d = opendir(path);
    if (!d) return;

    struct dirent *de = readdir(d);
    size_t path_len = strlen(path);
    char *file_path = (char *) malloc(path_len + sizeof(de->d_name) + 2);
    memcpy(file_path, path, path_len);
    if (file_path[path_len-1] != '/')
        file_path[path_len++] = '/';

    while (de)
    {
        struct stat s;
        strcpy(file_path + path_len, de->d_name);
        if (!stat(file_path, &s))
        {
            if (S_ISDIR(s.st_mode))
            {
                if (max_depth && strcmp(de->d_name, "..") && strcmp(de->d_name, "."))
                    scan_dir(file_path, func, context, max_depth - 1);
            }
            else if (S_ISREG(s.st_mode))
                func(context, file_path);
        }
        de = readdir(d);
    }
    closedir(d);
    free(file_path);
}
