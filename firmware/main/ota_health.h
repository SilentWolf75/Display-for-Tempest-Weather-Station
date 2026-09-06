#pragma once
#include <stdbool.h>
void ota_mark_valid(void);
void ota_reject_pending(void);
void ota_validate_boot(bool display_ready, bool ui_ready, bool tick_ready);
