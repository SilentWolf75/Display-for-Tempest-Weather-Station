#pragma once
#include <time.h>
#include <stddef.h>
#include <stdint.h>
#ifdef _WIN32
static inline struct tm *localtime_r(const time_t *t, struct tm *out) {
    return localtime_s(out, t) == 0 ? out : NULL;
}
#endif
extern time_t test_now;
extern int test_fail_alloc;
time_t test_time(time_t *out);
#define time test_time
