#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 1
void *test_calloc(size_t count, size_t size);
#define heap_caps_calloc(n,s,caps) test_calloc(n,s)
