#pragma once
#include "nws_alerts.h"
int nws_alert_rank(const nws_alert_t *alert);
bool nws_alert_live(const nws_alert_t *alert, int64_t now);
int64_t nws_parse_iso(const char *iso);
