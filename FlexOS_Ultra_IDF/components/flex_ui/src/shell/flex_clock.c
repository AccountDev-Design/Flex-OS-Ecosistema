#include "flex_clock.h"

#include <stdio.h>
#include "flex_time.h"
#include "flex_theme.h"

// Semilla de fabrica (Clock.h:52-62)
#define SEED_UTC 1783189380u   // 2026-07-04 13:23 en Lima = 18:23 UTC
#define MIN_EPOCH 1767225600u                 // 2026-01-01: por debajo, la hora no es valida

static uint32_t now_utc(void)
{
    return flex_time_utc();
}

void flex_clock_init(void)
{
#ifndef FLEX_SIM
    if (now_utc() < MIN_EPOCH) {
        flex_time_set_utc(SEED_UTC, false);   // la semilla de fabrica: no es hora de red
    }
#endif
}

void flex_clock_set_utc(uint32_t utc)
{
    flex_time_set_utc(utc, true);
}

uint32_t flex_clock_utc(void)
{
    return now_utc();
}

bool flex_clock_synced(void)
{
    return flex_time_synced();
}

void flex_clock_now(struct tm *out)
{
    time_t local = (time_t)((int64_t)now_utc() + FLEX_TZ_OFFSET_SEC);
    gmtime_r(&local, out);
}

int32_t flex_clock_minute(void)
{
    return (int32_t)(((int64_t)now_utc() + FLEX_TZ_OFFSET_SEC) / 60);
}

void flex_clock_str_big(char *out, int cap)
{
    struct tm t;
    flex_clock_now(&t);
    int h = t.tm_hour;
    if (!flex_look()->h24) {
        h %= 12;
        h = h ? h : 12;
    }
    snprintf(out, (size_t)cap, "%d:%02d", h, t.tm_min);
}

void flex_clock_str_bar(char *out, int cap)
{
    struct tm t;
    flex_clock_now(&t);
    if (flex_look()->h24) {
        snprintf(out, (size_t)cap, "%d:%02d", t.tm_hour, t.tm_min);
        return;
    }
    int h = t.tm_hour % 12;
    snprintf(out, (size_t)cap, "%d:%02d %s", h ? h : 12, t.tm_min, t.tm_hour < 12 ? "AM" : "PM");
}
