#ifndef MEASURE_TIME_H_
#define MEASURE_TIME_H_

#include <stdint.h>

int64_t get_current_time();
int64_t diff_time_usec(int64_t t1, int64_t t2);
double diff_time_sec(int64_t t1, int64_t t2);

#endif /* MEASURE_TIME_H_ */
