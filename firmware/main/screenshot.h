#pragma once

#include "esp_err.h"

/* Starts background UART listener for screenshot commands. */
esp_err_t screenshot_init(void);

/* Direct trigger function to capture and dump screen over UART. */
void screenshot_dump(void);
