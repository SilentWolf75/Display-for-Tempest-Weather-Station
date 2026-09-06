#pragma once
typedef void *SemaphoreHandle_t;
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
static inline int xSemaphoreTake(SemaphoreHandle_t s, unsigned t) { return 1; }
static inline int xSemaphoreGive(SemaphoreHandle_t s) { return 1; }
