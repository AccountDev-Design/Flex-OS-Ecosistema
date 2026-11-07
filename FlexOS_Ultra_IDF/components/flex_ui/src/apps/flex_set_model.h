// Flex OS Ultra · Ajustes: contenido de las pantallas, acciones de fila, ciclos de valores, sesion y transicion (C portable, con pruebas
// de host frente a Arduino).
//
// FlexOS_Ultra_AppSettings.h:51-93 (textos y geometria), :134-147 (fecha y actividad), :239-270 y :273-409 (filas y contenido de cada
// categoria), :418-452 (lista y cabecera de categoria), :539-590 (transicion), :634-696 (acciones) y :745-782 (sesion ajustes.bin v1).
//
// El modelo NO dibuja: entrega una LISTA DE OPERACIONES (tarjeta, glifo, texto, chevron) con sus coordenadas exactas en
// coordenadas de CONTENIDO (y relativa al inicio de la banda con scroll) y la interfaz LVGL las pinta tal cual. Asi lo que se
// compara en el PC con Arduino es lo que se ve. Los valores vivos (idioma, brillo, estado de la hora, Wi-Fi...) entran en un
// `flex_set_sys_t` que rellena la app desde los servicios reales.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---- geometria (480x800) ------------------------------------------------------------------------------------------------
#define FLEX_SET_NCAT        12
#define FLEX_SET_CARD_X      14
#define FLEX_SET_CARD_W      452
#define FLEX_SET_LIST_TOP    104
#define FLEX_SET_LIST_BOT    730    // exclusivo
#define FLEX_SET_DLIST_TOP   128
#define FLEX_SET_DLIST_BOT   730
#define FLEX_SET_CATCARD_H   66
#define FLEX_SET_CATCARD_GAP 8
#define FLEX_SET_BACK_W      52
#define FLEX_SET_ROW_MAX     16
#define FLEX_SET_NAV_MS      270
#define FLEX_SET_ANIM_Y0     40
#define FLEX_SET_ANIM_Y1     740    // exclusivo (SCR_H - 60)
#define FLEX_SET_PARALLAX    134    // 28 % del ancho

extern const char *const flex_set_cat[FLEX_SET_NCAT];    // SET_CAT
extern const char *const flex_set_sub[FLEX_SET_NCAT];    // SET_SUB
extern const char *const flex_set_desc[FLEX_SET_NCAT];   // SET_DESC
extern const uint16_t flex_set_accent[FLEX_SET_NCAT];    // SET_ACCENT (RGB565)

// glifos de fila (RI_*)
typedef enum { FLEX_RI_GLOBE = 0, FLEX_RI_CAL, FLEX_RI_CLOCK, FLEX_RI_PIN, FLEX_RI_REFRESH, FLEX_RI_CLOUD, FLEX_RI_RESET, FLEX_RI_DOT } flex_ri_t;
// roles de color del texto (los resuelve el tema: TH_TXT, TH_TXT2, TH_MUTE)
typedef enum { FLEX_SR_HI = 0, FLEX_SR_LO, FLEX_SR_MUTE, FLEX_SR_DIS, FLEX_SR_ONACC } flex_sr_t;   // + TH_DIS (fila apagada por hardware) y TH_ONACC (sobre el acento)

// ---- lo que las filas leen del sistema -----------------------------------------------------------------------------------
#define FLEX_LW_CLOCK   0x01
#define FLEX_LW_WEATHER 0x02
#define FLEX_LW_CAL     0x04
#define FLEX_LW_NOTIF   0x08
#define FLEX_AUTOLOCK_N 6
extern const uint32_t flex_set_autolock_ms[FLEX_AUTOLOCK_N];
extern const char *const flex_set_autolock_name[FLEX_AUTOLOCK_N];

typedef struct {
    int lang;                 // cfgLang 0..5
    bool h24;                 // g24h
    int bright;               // gBright
    bool glass, dark;         // uiGlass, gDark
    int navmode;              // gNavMode 0 botones / 1 gestos
    int lockwidgets;          // gLockWidgets (FLEX_LW_*)
    int iconstyle;            // gIconStyle 0 plano / 1 vidrio
    int animstyle;            // gAnimStyle 0 zoom / 1 fundido / 2 deslizar
    int kbsize;               // gKbSize (FLEX_KB_SIZE_*)
    int locktype;             // gLockType 0 deslizar / 1 PIN / 2 contrasena
    uint32_t autolock_ms;     // gAutoLockMs
    bool poffpin;             // gPoffPin
    bool airplane;            // gAirplane
    bool ble_enabled;         // FLEXOS_ENABLE_BLE (el ESP32-P4 no tiene radio BLE: false)
    char name[40];            // cfgName
    int rtc_d, rtc_mo, rtc_y, rtc_h, rtc_min;
    bool time_nvs_ok;         // gTimeNvsOk
    bool ntp_busy;            // ntpIsBusy()
    bool ntp_available;       // hay servicio de hora (Arduino siempre lo tiene; aqui aun no esta migrado: la fila lo dice y no hace nada)
    char ntp_state[64];       // ntpStateText
    char ntp_last[48];        // ntpLastSyncText
    char ota_text[48];        // flexOtaStatusText
    char account_text[64];    // accountSettingsText
    char wifi_sub[64];        // connWifiSub (con sus parentesis, si los trae)
    char ble_sub[64];         // connBleSub
    char theft[48];           // theftRowValue
    bool audio_avail;         // flexAudioAvailable
    int volume;               // flexAudioVolume
    bool muted;               // flexAudioMuted
    char audio_error[48];     // flexAudioError
    bool fs_ready;            // flexFsReady
    char fs_used[16], fs_total[16], trash_size[16];   // flexFsFmtSize de lo usado, el total y la papelera
    char uptime[24];          // buildUptime
    unsigned free_kb, psram_free_mb, psram_total_mb;
} flex_set_sys_t;

// ---- operaciones de dibujo -----------------------------------------------------------------------------------------------
typedef enum {
    FLEX_SO_CARD = 0,    // tarjeta (vidrio o plana): x, y, w, h, radio en `r`
    FLEX_SO_GLYPH,       // glifo de fila `glyph` centrado en (x, y) con `color`
    FLEX_SO_CATICON,     // icono de categoria `glyph` en (x, y), lado `w`, con `color`
    FLEX_SO_TEXT,        // drawText / drawTextClip: x, y, `size`, `role`, recorte en `max_right` (0 = sin recorte)
    FLEX_SO_TEXT_R,      // drawTextR: alineado a la derecha en x
    FLEX_SO_CHEV,        // chevron ">": centro (x, y)
    FLEX_SO_BACK,        // chevron "<" de la cabecera de categoria
    FLEX_SO_FRAME,       // contorno redondeado (campo enfocado): x, y, w, h, r; color de acento
    FLEX_SO_BTN,         // boton relleno: x, y, w, h, r; glyph 0 = primario, 1 = peligro
    FLEX_SO_TEXT_C,      // drawTextC: texto CENTRADO en x
} flex_so_kind_t;

typedef struct {
    uint8_t kind, size, role, glyph;
    int16_t x, y, w, h, r, max_right;
    uint16_t color;
    char text[72];
} flex_set_op_t;

#define FLEX_SET_OPS_MAX 160
typedef struct {
    flex_set_op_t op[FLEX_SET_OPS_MAX];
    int n;
    int rows;                              // filas registradas (el indice de fila es el ORDEN de creacion)
    int row_y0[FLEX_SET_ROW_MAX], row_y1[FLEX_SET_ROW_MAX];   // rango de cada fila, relativo al inicio del contenido
    int row_op[FLEX_SET_ROW_MAX];          // primera operacion de cada fila (su tarjeta)
    int card_op[FLEX_SET_NCAT];            // (lista) primera operacion de cada tarjeta
    int content_h;                         // setContentH / setListH
} flex_set_page_t;

// Pagina de detalle de la categoria `cat`: y de las operaciones y de las filas relativas a DLIST_TOP - scroll (la y de pantalla es
// DLIST_TOP - scroll + y). content_h es setContentH.
void flex_set_detail_build(flex_set_page_t *p, int cat, const flex_set_sys_t *s);
// Cabecera de la categoria abierta (chevron, titulo y descripcion): coordenadas de pantalla.
void flex_set_detail_head(flex_set_page_t *p, int cat);
// Lista de categorias: y relativas a LIST_TOP - scroll; content_h = setListH. Cada tarjeta lleva su fondo, icono, nombre, subtitulo y chevron.
void flex_set_list_build(flex_set_page_t *p);
void flex_set_list_head(flex_set_page_t *p);   // el titulo "Ajustes" (coordenadas de pantalla)
bool flex_set_card_visible(int i, int list_scroll);   // Arduino no dibuja las tarjetas que caen fuera de la ventana
int flex_set_max_scroll(int content_h, bool list);   // setMaxScroll

// ---- acciones de fila (settingsRowAction) ----------------------------------------------------------------------------------
#define FLEX_SF_SAVE_LANG      (1u << 0)
#define FLEX_SF_SAVE_H24       (1u << 1)
#define FLEX_SF_SAVE_BRIGHT    (1u << 2)
#define FLEX_SF_SAVE_LOCKW     (1u << 3)
#define FLEX_SF_SAVE_NAV       (1u << 4)
#define FLEX_SF_SAVE_ICON      (1u << 5)
#define FLEX_SF_SAVE_ANIM      (1u << 6)
#define FLEX_SF_SAVE_AUTOLOCK  (1u << 7)
#define FLEX_SF_SAVE_POFFPIN   (1u << 8)
#define FLEX_SF_THEME          (1u << 9)    // themeChanged(): guarda, invalida cachés y repinta
#define FLEX_SF_HOME_DIRTY     (1u << 10)   // gHomeDirty
#define FLEX_SF_BACKLIGHT      (1u << 11)   // setBacklight(bright)
#define FLEX_SF_TOUCH_TIMER    (1u << 12)   // gLastTouchMs = millis()
typedef enum {
    FLEX_SX_NONE = 0,
    FLEX_SX_NTP_SYNC,      // ntpRequestSync(true)
    FLEX_SX_OTA,           // flexOtaOpenSettings()
    FLEX_SX_FACTORY,       // frEnterWizard()
    FLEX_SX_ACCOUNT,       // accountSettingsEnter()
    FLEX_SX_CONN,          // connEnter()
    FLEX_SX_WIFI,          // wifiSettingsEnter()
    FLEX_SX_BLE_TOGGLE,    // flexBleStart / Stop
    FLEX_SX_KEYBOARD,      // kbsEnter()
    FLEX_SX_LOCKSETUP,     // lsuEnter()
    FLEX_SX_THEFT,         // theftEnter()
} flex_sx_t;
typedef enum { FLEX_SRD_NONE = 0, FLEX_SRD_BAND, FLEX_SRD_FULL } flex_srd_t;
typedef struct {
    uint32_t flags;        // FLEX_SF_*
    uint8_t open;          // flex_sx_t: pantalla o servicio que hay que abrir
    uint8_t render;        // flex_srd_t: lo que hay que repintar
} flex_set_act_t;
// Aplica al estado el ciclo de la fila (cat, idx) y dice que hacer. idx es el orden de creacion de la fila en esa categoria.
void flex_set_row_action(flex_set_sys_t *s, int cat, int idx, flex_set_act_t *out);
int flex_set_autolock_idx(uint32_t ms);           // autoLockIdx (un valor fuera de la lista = 1 minuto)
uint32_t flex_set_autolock_norm(uint32_t ms);     // autoLockNormalize

// ---- textos de ayuda ----------------------------------------------------------------------------------------------------------
const char *flex_set_lang_name(int lang);                                  // langNameCur (el chino sale como "Chinese")
void flex_set_datetime_str(char *out, size_t n, int d, int mo, int y, int h, int min);   // settingsDateTimeStr (siempre 12 h)
void flex_set_uptime(unsigned long ms, char *out, size_t n);              // buildUptime
// connWifiSub / connBleSub vienen con parentesis: aqui se quitan (la fila ya tiene su titulo)
const char *flex_set_unparen(char *s);

// ---- transicion entre pantallas (settingsAnimate) -------------------------------------------------------------------------------
float flex_set_ease(float p);   // ease-in-out cubica
// Desplazamientos de la pagina que se va (out) y de la que entra (in) para dir = +1 (abrir) / -1 (volver) tras `ms` milisegundos.
void flex_set_anim_offsets(int dir, uint32_t ms, int *out_off, int *in_off);
int flex_set_shadow_alpha(int k);   // effShadow(70 - 8 k) sin el escalado del tema: 70 - 8 k, k = 1..8
// Arrastre desde el borde izquierdo para volver: decision al soltar.
bool flex_set_back_swipe_armed(int start_x);
bool flex_set_back_swipe_done(int start_x, int start_y, int x, int y);

// ---- sesion ajustes.bin v1 (8 bytes) -------------------------------------------------------------------------------------------
#define FLEX_SET_SESS_VER 1
typedef struct { int16_t view, sel, scroll, list_scroll; } flex_set_sess_t;
void flex_set_sess_pack(flex_set_sess_t *s, int view, int sel, int scroll, int list_scroll);
// setLoadSess: false = sesion incoherente (se ignora entera).
bool flex_set_sess_normalize(flex_set_sess_t *s);

#ifdef __cplusplus
}
#endif
