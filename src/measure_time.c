#include "measure_time.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#include <stdlib.h>
#endif

int64_t get_current_time()
{
#ifdef _WIN32
    int64_t ft;
    GetSystemTimeAsFileTime((FILETIME *) &ft);
    return ft;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t) tv.tv_sec * 1000000 + tv.tv_usec;
#endif
}

int64_t diff_time_usec(int64_t t1, int64_t t2)
{
#ifdef _WIN32
    return (t2 - t1) / 10;
#else
    return t2 - t1;
#endif
}

double diff_time_sec(int64_t t1, int64_t t2)
{
    return diff_time_usec(t1, t2) / 1000000.0;
}

