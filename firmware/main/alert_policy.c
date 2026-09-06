#include "alert_policy.h"
#include <stdio.h>
#include <string.h>

int nws_alert_rank(const nws_alert_t *a)
{
    int rank = strcmp(a->severity, "Extreme") == 0 ? 400 :
               strcmp(a->severity, "Severe") == 0 ? 300 :
               strcmp(a->severity, "Moderate") == 0 ? 200 : 100;
    if (strstr(a->event, "Tornado") && strstr(a->event, "Warning")) rank += 1000;
    else if (strstr(a->event, "Warning") || strstr(a->event, "Emergency")) rank += 50;
    else if (strstr(a->event, "Watch")) rank += 25;
    return rank;
}

bool nws_alert_live(const nws_alert_t *a, int64_t now)
{
    return a->active && (a->expires_epoch <= 0 || now < a->expires_epoch);
}

int64_t nws_parse_iso(const char *iso)
{
    int y, mo, d, h, mi, sec, used = 0;
    if (!iso || sscanf(iso, "%d-%d-%dT%d:%d:%d%n", &y, &mo, &d,
                       &h, &mi, &sec, &used) != 6) return 0;
    if (y < 1970 || y > 9999 || mo < 1 || mo > 12 || h < 0 || h > 23 ||
        mi < 0 || mi > 59 || sec < 0 || sec > 59) return 0;
    const int lengths[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    bool leap = y % 4 == 0 && (y % 100 != 0 || y % 400 == 0);
    if (d < 1 || d > lengths[mo-1] + (mo == 2 && leap)) return 0;
    int prev = y - 1;
    int days = 365 * (y - 1970) + prev / 4 - prev / 100 + prev / 400 -
               (1969 / 4 - 1969 / 100 + 1969 / 400) + d - 1;
    for (int m = 1; m < mo; m++) days += lengths[m-1] + (m == 2 && leap);
    int offset = 0;
    const char *zone = iso + used;
    if (*zone == '+' || *zone == '-') {
        int zh, zm;
        if (sscanf(zone + 1, "%d:%d", &zh, &zm) != 2 ||
            zh < 0 || zh > 23 || zm < 0 || zm > 59) return 0;
        offset = (zh * 60 + zm) * 60 * (*zone == '+' ? 1 : -1);
    } else if (*zone != 'Z') return 0;
    return (int64_t)days * 86400 + h * 3600 + mi * 60 + sec - offset;
}
