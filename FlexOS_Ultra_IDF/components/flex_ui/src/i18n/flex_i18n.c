#include "flex_i18n.h"

#include <stdio.h>
#include "flex_storage.h"

static int s_lang;

void flex_i18n_init(void)
{
    int32_t l = flex_cfg_get_i32("lang", 0);
    s_lang = (l >= 0 && l < FLEX_NLANG) ? (int)l : 0;
}

int flex_lang(void)
{
    return s_lang;
}

void flex_set_lang(int lang)
{
    if (lang >= 0 && lang < FLEX_NLANG && lang != s_lang) {
        s_lang = lang;
        flex_cfg_set_i32("lang", lang);
    }
}

const char *flex_app_name(int id)
{
    return (id >= 0 && id < FLEX_APP_N) ? flex_i18n_app[id][flex_li()] : "";
}

void flex_date_long(char *out, int cap, int wday, int mday, int mon)
{
    int li = flex_li();
    const char *wd = flex_i18n_wd_full[li][wday % 7], *mo = flex_i18n_mo_full[li][(mon + 11) % 12];
    switch (s_lang) {
    case 0:
    case 3: snprintf(out, (size_t)cap, "%s, %d de %s", wd, mday, mo); break;   // ES, PT
    case 2:
    case 4: snprintf(out, (size_t)cap, "%s %d %s", wd, mday, mo); break;      // FR, IT
    default: snprintf(out, (size_t)cap, "%s, %s %d", wd, mo, mday); break;    // EN / ZH
    }
}

void flex_date_short(char *out, int cap, int wday, int mday, int mon)
{
    int li = flex_li();
    snprintf(out, (size_t)cap, "%s, %d %s", flex_i18n_wd_short[li][wday % 7], mday,
             flex_i18n_mo_short[li][(mon + 11) % 12]);
}
