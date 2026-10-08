// Pruebas de host: ajustes en memoria (la cache real flex_kv, sin NVS).
#include <string.h>
#include "flex_kv.h"
#include "flex_storage.h"

flex_kv_t g_test_kv;
static int s_init;
static flex_kv_t *kv(void)
{
    if (!s_init) {
        flex_kv_init(&g_test_kv, NULL, NULL, NULL);
        s_init = 1;
    }
    return &g_test_kv;
}
void test_cfg_reset(void)
{
    flex_kv_clear(kv());
}
int32_t flex_cfg_get_i32(const char *k, int32_t d)
{
    int64_t v;
    return flex_kv_get_num(kv(), FLEX_NVS_NS, k, FLEX_KV_I32, &v) ? (int32_t)v : d;
}
bool flex_cfg_get_bool(const char *k, bool d)
{
    int64_t v;
    return flex_kv_get_num(kv(), FLEX_NVS_NS, k, FLEX_KV_U8, &v) ? v != 0 : d;
}
size_t flex_cfg_get_blob(const char *k, void *o, size_t c)
{
    size_t len = 0;
    return flex_kv_get_buf(kv(), FLEX_NVS_NS, k, FLEX_KV_BLOB, o, c, &len) ? len : 0;
}
esp_err_t flex_cfg_set_i32(const char *k, int32_t v)
{
    flex_kv_set_num(kv(), FLEX_NVS_NS, k, FLEX_KV_I32, v);
    return 0;
}
esp_err_t flex_cfg_set_bool(const char *k, bool v)
{
    flex_kv_set_num(kv(), FLEX_NVS_NS, k, FLEX_KV_U8, v);
    return 0;
}
esp_err_t flex_cfg_set_blob(const char *k, const void *d, size_t l)
{
    flex_kv_set_buf(kv(), FLEX_NVS_NS, k, FLEX_KV_BLOB, d, l);
    return 0;
}
