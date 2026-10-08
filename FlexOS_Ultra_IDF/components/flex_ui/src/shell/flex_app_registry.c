// Flex OS Ultra · registro de apps (APP_REG, docs/spec/01a §15).
#include <stddef.h>
#include "flex_app.h"
#include "flex_i18n.h"

// Apps ya migradas: cada una define su tabla de operaciones (weak: si la app
// aun no esta enlazada, queda la pantalla "en construccion").
#define APP_OPS(sym) extern const flex_app_ops_t sym __attribute__((weak))
APP_OPS(flex_app_reloj_ops);
APP_OPS(flex_app_galeria_ops);
APP_OPS(flex_app_multimedia_ops);
APP_OPS(flex_app_almacen_ops);
APP_OPS(flex_app_modopc_ops);
APP_OPS(flex_app_notas_ops);
APP_OPS(flex_app_nav_ops);
APP_OPS(flex_app_brujula_ops);
APP_OPS(flex_app_paint_ops);
APP_OPS(flex_app_juegos_ops);
APP_OPS(flex_app_ajustes_ops);
APP_OPS(flex_app_calc_ops);
APP_OPS(flex_app_calend_ops);
APP_OPS(flex_app_camara_ops);
APP_OPS(flex_app_clima_ops);
APP_OPS(flex_app_flexstore_ops);
APP_OPS(flex_app_flexphone_ops);
APP_OPS(flex_app_devcare_ops);
APP_OPS(flex_app_musica_ops);

#define CH  FLEX_APP_CUSTOM_HEADER
#define OT  FLEX_APP_OWN_TOUCH
#define LND FLEX_APP_LAND
#define FX  FLEX_APP_FLEX
#define BG  FLEX_APP_BG_KEEP
#define IM  FLEX_APP_IMMERSIVE

static const flex_app_def_t k_reg[FLEX_APP_N] = {
    {IC_RELOJ, FX, FLEX_CAT_ESSENTIALS, true, 0, &flex_app_reloj_ops},
    {IC_GALERIA, FX | BG, FLEX_CAT_MEDIA, true, 2, &flex_app_galeria_ops},
    {IC_MULTIMEDIA, CH | OT, FLEX_CAT_MEDIA, true, 2, &flex_app_multimedia_ops},
    {IC_ALMACEN, FX, FLEX_CAT_SYSTEM, true, 0, &flex_app_almacen_ops},
    {IC_MODOPC, CH, FLEX_CAT_SYSTEM, true, 2, &flex_app_modopc_ops},
    {IC_NOTAS, CH | OT, FLEX_CAT_PRODUCTIVITY, true, 0, &flex_app_notas_ops},
    {IC_NAV, FX | OT | IM, FLEX_CAT_ESSENTIALS, true, 2, &flex_app_nav_ops},
    {IC_BRUJULA, CH | OT, FLEX_CAT_ESSENTIALS, true, 0, &flex_app_brujula_ops},
    {IC_PAINT, CH | OT, FLEX_CAT_FUN, true, 1, &flex_app_paint_ops},
    {IC_JUEGOS, OT | CH | LND, FLEX_CAT_FUN, true, 1, &flex_app_juegos_ops},
    {IC_AJUSTES, CH, FLEX_CAT_SYSTEM, false, 0, &flex_app_ajustes_ops},
    {IC_CALC, FX, FLEX_CAT_PRODUCTIVITY, false, 0, &flex_app_calc_ops},
    {IC_CALEND, FX, FLEX_CAT_PRODUCTIVITY, false, 0, &flex_app_calend_ops},
    {IC_CAMARA, CH | OT, FLEX_CAT_MEDIA, false, 2, &flex_app_camara_ops},
    {IC_CLIMA, CH | OT, FLEX_CAT_ESSENTIALS, false, 0, &flex_app_clima_ops},
    {IC_FLEXSTORE, CH | OT | FX, FLEX_CAT_SYSTEM, true, 1, &flex_app_flexstore_ops},
    {IC_FLEXPHONE, CH | OT | FX, FLEX_CAT_ESSENTIALS, false, 0, &flex_app_flexphone_ops},
    {IC_DEVCARE, CH | OT | FX, FLEX_CAT_SYSTEM, true, 0, &flex_app_devcare_ops},
    {IC_MUSICA, FX | BG, FLEX_CAT_MEDIA, false, 1, &flex_app_musica_ops},
};

// APP_CAT_NAME (AppFramework.h:652): ES, EN, FR, PT, IT
static const char *const k_cat[FLEX_CAT_N][5] = {
    {"Esenciales", "Essentials", "Essentiels", "Essenciais", "Essenziali"},
    {"Multimedia", "Media", "M\xC3\xA9" "dias", "M\xC3\xAD" "dia", "Multimedia"},
    {"Productividad", "Productivity", "Productivit\xC3\xA9", "Produtividade", "Produttivit\xC3\xA0"},
    {"Sistema", "System", "Syst\xC3\xA8me", "Sistema", "Sistema"},
    {"Ocio", "Fun", "Loisirs", "Lazer", "Svago"},
};

const flex_app_def_t *flex_app_def(int id)
{
    if (id < 0 || id >= FLEX_APP_N) {
        return NULL;
    }
    const flex_app_def_t *d = &k_reg[id];
    // Una app sin ops reales (simbolo weak sin definir) apunta a NULL.
    return d;
}

const char *flex_app_cat_name(int cat)
{
    if (cat < 0 || cat >= FLEX_CAT_N) {
        cat = FLEX_CAT_SYSTEM;
    }
    return k_cat[cat][flex_li()];
}

uint32_t flex_app_factory_fav(void)
{
    uint32_t m = 0;
    for (int i = 0; i < FLEX_APP_N; i++) {
        if (k_reg[i].fav_default) {
            m |= 1u << i;
        }
    }
    return m;
}
