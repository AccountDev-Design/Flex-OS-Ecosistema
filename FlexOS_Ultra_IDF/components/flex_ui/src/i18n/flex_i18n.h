// Flex OS Ultra · idiomas y textos de la interfaz.
//
// Seis idiomas como la version Arduino: 0 ES, 1 EN, 2 FR, 3 PT, 4 IT, 5 ZH
// (ZH usa los textos en ingles: la fuente no tiene glifos chinos). Las tablas
// salen de la version Arduino sin cambios (tools/gen_i18n.py).
#pragma once

#include <stdint.h>
#include "flex_app_ids.h"
#include "flex_i18n_strings.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_NLANG 6

extern const char *const flex_i18n_lang_endonym[FLEX_NLANG];
extern const char *const flex_i18n_ch[FLEX_S_NSTR][5];
extern const char *const flex_i18n_app[FLEX_APP_N][5];
extern const char *const flex_i18n_wd_full[5][7];
extern const char *const flex_i18n_wd_short[5][7];
extern const char *const flex_i18n_mo_full[5][12];
extern const char *const flex_i18n_mo_short[5][12];

// Idioma (NVS "lang", int 0..5, def. 0). Cargar tras flex_storage_init().
void flex_i18n_init(void);
int  flex_lang(void);
void flex_set_lang(int lang);
// Columna de las tablas (ZH -> EN).
static inline int flex_li(void)
{
    int l = flex_lang();
    return l == 5 ? 1 : l;
}
static inline const char *flex_t(int id) { return flex_i18n_ch[id][flex_li()]; }
const char *flex_app_name(int id);

// Fechas localizadas como la version Arduino (buildLongDate/buildShortDate).
void flex_date_long(char *out, int cap, int wday, int mday, int mon);
void flex_date_short(char *out, int cap, int wday, int mday, int mon);

#ifdef __cplusplus
}
#endif
