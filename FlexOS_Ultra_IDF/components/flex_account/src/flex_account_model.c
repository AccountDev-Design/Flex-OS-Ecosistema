// Flex OS Ultra · Flex Account: el nucleo puro (ver flex_account.h). Cada funcion repite la de FlexOS_Account.cpp; las pruebas de host las comparan con las de Arduino
// compiladas tal cual. BACKEND CRIPTOGRAFICO: en la placa mbedTLS (con el acelerador SHA del P4 por debajo), en el PC OpenSSL; las dos rutas comprueban EXACTAMENTE lo
// mismo sobre los mismos bytes.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "flex_account.h"

#if defined(__has_include)
#if __has_include(<mbedtls/ecdsa.h>)
#define FXA_BACKEND_MBEDTLS 1
#elif __has_include(<openssl/evp.h>)
#define FXA_BACKEND_OPENSSL 1
#endif
#endif
#if !defined(FXA_BACKEND_MBEDTLS) && !defined(FXA_BACKEND_OPENSSL)
#error "flex_account: falta un backend criptografico (mbedTLS u OpenSSL)"
#endif
#if FXA_BACKEND_MBEDTLS
#include "mbedtls/bignum.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"
#include "mbedtls/sha256.h"
#include "mbedtls/version.h"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <openssl/sha.h>
#endif

// Misma clave publica anclada que Flex Store. La privada existe solamente en Flex Developer Studio: por eso el enlace sigue siendo autentico incluso si el certificado del
// hosting rota (el P4 no acepta un codigo ni un estado de cuenta que no lleven una firma ES256 valida)
static const uint8_t ACCOUNT_PUBLIC_KEY[65] = {
    0x04, 0x70, 0x9d, 0xf4, 0x80, 0xde, 0x8d, 0x66, 0x05, 0x38, 0x6b, 0x01, 0xb3, 0xf8, 0x9f, 0x20, 0xf7, 0x29, 0x08, 0x7f, 0x76, 0xff, 0x76, 0xfd, 0x43, 0x9d, 0x50, 0xa8, 0x30,
    0x9e, 0xac, 0xc7, 0x60, 0xf5, 0x4d, 0x60, 0xbb, 0x85, 0xde, 0xa2, 0xd3, 0x79, 0x8e, 0x15, 0xab, 0xad, 0x89, 0xd5, 0x97, 0xa4, 0xc0, 0x7c, 0xfa, 0x66, 0xc1, 0x70, 0x6d, 0xe6,
    0x48, 0xb3, 0x8e, 0x40, 0x28, 0x2c, 0x05};

#ifdef FLEX_HOST_TEST
static uint8_t s_test_key[65];
static bool s_test_key_set;
void flex_account_test_set_key(const uint8_t pub[65])
{
    memcpy(s_test_key, pub, 65);
    s_test_key_set = true;
}
#endif
static const uint8_t *account_key(void)
{
#ifdef FLEX_HOST_TEST
    if (s_test_key_set) {
        return s_test_key;
    }
#endif
    return ACCOUNT_PUBLIC_KEY;
}

// ---- nucleo puro ----
flex_session_verdict_t flex_account_classify(int http_status, const char *error_code)
{
    if (http_status >= 200 && http_status < 300) {
        return FLEX_SESSION_OK;
    }
    if (http_status == 401 || http_status == 403) {
        if (error_code && !strcmp(error_code, "token_expired")) {
            return FLEX_SESSION_TOKEN_EXPIRED;
        }
        // 401/403 sin un codigo reconocible del servicio puede venir de un proxy o de una pagina de error del alojamiento: no se desvincula por eso
        if (error_code && (!strcmp(error_code, "device_revoked") || !strcmp(error_code, "auth_required") || !strcmp(error_code, "invalid_token") ||
                           !strcmp(error_code, "device_unknown"))) {
            return FLEX_SESSION_AUTH_REQUIRED;
        }
        return FLEX_SESSION_UNAVAILABLE;
    }
    return FLEX_SESSION_UNAVAILABLE;
}

uint32_t flex_account_backoff_ms(uint8_t failures)
{
    if (failures == 0) {
        return 0;
    }
    uint32_t ms = 30000u;
    for (uint8_t i = 1; i < failures && ms < 15u * 60u * 1000u; i++) {
        ms *= 2u;
    }
    if (ms > 15u * 60u * 1000u) {
        ms = 15u * 60u * 1000u;
    }
    return ms;
}

size_t flex_nvs_str_len(size_t nvs_returned, const char *buf, size_t cap)
{
    if (nvs_returned == 0 || !buf || cap == 0) {
        return 0;   // 0 = no existe o no cabia: buf no es fiable
    }
    size_t n = 0;
    while (n < cap && buf[n]) {
        n++;
    }
    if (n >= cap) {
        return 0;   // sin terminador: no se acepta
    }
    // nvs_get_str cuenta el terminador; un valor coherente cumple n + 1 == devuelto. Si una version futura devolviera strlen, n == devuelto tambien vale
    if (n + 1 != nvs_returned && n != nvs_returned) {
        return 0;
    }
    return n;
}

const char *flex_account_link_label(flex_account_link_t link)
{
    switch (link) {
    case FLEX_LINK_LINKED:
        return "Conectada";
    case FLEX_LINK_LINKED_OFFLINE:
        return "Vinculada \xC2\xB7 sin conexi\xC3\xB3n";
    case FLEX_LINK_NETWORK_UNAVAILABLE:
        return "Vinculada \xC2\xB7 servicio no disponible";
    case FLEX_LINK_AUTH_REQUIRED:
        return "Vuelve a iniciar sesi\xC3\xB3n";
    case FLEX_LINK_TOKEN_EXPIRED:
        return "Sesi\xC3\xB3n caducada";
    case FLEX_LINK_ERROR:
        return "No se pudo leer la cuenta";
    default:
        return "Sin cuenta vinculada";
    }
}

const char *flex_account_verdict_detail(flex_session_verdict_t v)
{
    switch (v) {
    case FLEX_SESSION_OK:
        return "Sesion validada";
    case FLEX_SESSION_AUTH_REQUIRED:
        return "Flex Account ya no reconoce este aparato: vuelve a vincularlo";
    case FLEX_SESSION_TOKEN_EXPIRED:
        return "La sesion de este aparato caduco: vuelve a vincularlo";
    default:
        return "Flex Account no responde; se reintentara";
    }
}

// ---- criptografia ----
bool flex_account_sha256(const uint8_t *data, size_t len, uint8_t out[32])
{
    if (!data || !out) {
        return false;
    }
#if FXA_BACKEND_MBEDTLS
#if MBEDTLS_VERSION_NUMBER >= 0x03000000
    return mbedtls_sha256(data, len, out, 0) == 0;
#else
    return mbedtls_sha256_ret(data, len, out, 0) == 0;
#endif
#else
    return SHA256(data, len, out) != NULL;
#endif
}

void flex_account_hex32(const uint8_t data[32], char out[65])
{
    static const char h[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        out[i * 2] = h[data[i] >> 4];
        out[i * 2 + 1] = h[data[i] & 15];
    }
    out[64] = 0;
}

bool flex_account_make_bearer(const uint8_t random32[32], char out[48], char hash_hex[65])
{
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    size_t used = 0;
    uint32_t acc = 0;
    int bits = 0;
    for (size_t i = 0; i < 32; i++) {
        acc = (acc << 8) | random32[i];
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            out[used++] = table[(acc >> bits) & 63u];
        }
    }
    if (bits) {
        out[used++] = table[(acc << (6 - bits)) & 63u];
    }
    out[used] = 0;
    uint8_t digest[32];
    bool ok = used == 43 && flex_account_sha256((const uint8_t *)out, used, digest);
    if (ok) {
        flex_account_hex32(digest, hash_hex);
    }
    memset(digest, 0, sizeof(digest));
    return ok;
}

bool flex_account_json_escape(const char *in, char *out, size_t cap)
{
    if (!in || !out || cap < 2) {
        return false;
    }
    size_t used = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p; ++p) {
        const char *esc = NULL;
        if (*p == '\"') {
            esc = "\\\"";
        } else if (*p == '\\') {
            esc = "\\\\";
        } else if (*p == '\n') {
            esc = "\\n";
        } else if (*p == '\r') {
            esc = "\\r";
        } else if (*p == '\t') {
            esc = "\\t";
        }
        if (esc) {
            if (used + 2 >= cap) {
                return false;
            }
            out[used++] = esc[0];
            out[used++] = esc[1];
        } else {
            if (*p < 0x20 || used + 1 >= cap) {
                return false;
            }
            out[used++] = (char)*p;
        }
    }
    out[used] = 0;
    return true;
}

int flex_account_b64_value(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 52;
    }
    if (c == '-' || c == '+') {
        return 62;
    }
    if (c == '_' || c == '/') {
        return 63;
    }
    return -1;
}

bool flex_account_b64url_decode(const char *text, uint8_t **out, size_t *out_len, size_t max_out)
{
    if (!text || !out || !out_len) {
        return false;
    }
    size_t n = strlen(text);
    size_t encoded = n;
    while (encoded > 0 && text[encoded - 1] == '=') {
        encoded--;
    }
    size_t remainder = encoded & 3u;
    if (remainder == 1u) {
        return false;
    }
    size_t expected = (encoded / 4u) * 3u + (remainder ? remainder - 1u : 0u);
    if (expected > max_out) {
        return false;
    }
    uint8_t *data = (uint8_t *)malloc(expected + 1u);
    if (!data) {
        return false;
    }
    uint32_t acc = 0;
    int bits = 0;
    size_t used = 0;
    for (size_t i = 0; i < n; i++) {
        if (text[i] == '=') {
            for (size_t j = i; j < n; j++) {
                if (text[j] != '=') {
                    free(data);
                    return false;
                }
            }
            break;
        }
        int value = flex_account_b64_value(text[i]);
        if (value < 0) {
            free(data);
            return false;
        }
        acc = (acc << 6) | (uint32_t)value;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (used >= expected) {
                free(data);
                return false;
            }
            data[used++] = (uint8_t)(acc >> bits);
        }
    }
    if (used != expected) {
        free(data);
        return false;
    }
    data[used] = 0;
    *out = data;
    *out_len = used;
    return true;
}

#if FXA_BACKEND_MBEDTLS
bool flex_account_verify_signature(const uint8_t *payload, size_t payload_len, const uint8_t signature[64])
{
    static const uint8_t P256_ORDER[32] = {0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                           0xbc, 0xe6, 0xfa, 0xad, 0xa7, 0x17, 0x9e, 0x84, 0xf3, 0xb9, 0xca, 0xc2, 0xfc, 0x63, 0x25, 0x51};
    static const uint8_t P256_HALF_ORDER[32] = {0x7f, 0xff, 0xff, 0xff, 0x80, 0x00, 0x00, 0x00, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                                0xde, 0x73, 0x7d, 0x56, 0xd3, 0x8b, 0xcf, 0x42, 0x79, 0xdc, 0xe5, 0x61, 0x7e, 0x31, 0x92, 0xa8};
    uint8_t digest[32];
    if (!flex_account_sha256(payload, payload_len, digest)) {
        return false;
    }
    mbedtls_ecp_group group;
    mbedtls_ecp_group_init(&group);
    mbedtls_ecp_point key;
    mbedtls_ecp_point_init(&key);
    mbedtls_mpi r, s, order, half_order;
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);
    mbedtls_mpi_init(&order);
    mbedtls_mpi_init(&half_order);
    int rc = mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1);
    if (rc == 0) {
        rc = mbedtls_ecp_point_read_binary(&group, &key, account_key(), sizeof(ACCOUNT_PUBLIC_KEY));
    }
    if (rc == 0) {
        rc = mbedtls_ecp_check_pubkey(&group, &key);
    }
    if (rc == 0) {
        rc = mbedtls_mpi_read_binary(&r, signature, 32);
    }
    if (rc == 0) {
        rc = mbedtls_mpi_read_binary(&s, signature + 32, 32);
    }
    if (rc == 0) {
        rc = mbedtls_mpi_read_binary(&order, P256_ORDER, sizeof(P256_ORDER));
    }
    if (rc == 0) {
        rc = mbedtls_mpi_read_binary(&half_order, P256_HALF_ORDER, sizeof(P256_HALF_ORDER));
    }
    if (rc == 0 && mbedtls_mpi_cmp_mpi(&s, &half_order) > 0) {
        rc = mbedtls_mpi_sub_mpi(&s, &order, &s);
    }
    if (rc == 0) {
        rc = mbedtls_ecdsa_verify(&group, digest, sizeof(digest), &key, &r, &s);
    }
    mbedtls_mpi_free(&half_order);
    mbedtls_mpi_free(&order);
    mbedtls_mpi_free(&s);
    mbedtls_mpi_free(&r);
    mbedtls_ecp_point_free(&key);
    mbedtls_ecp_group_free(&group);
    memset(digest, 0, sizeof(digest));
    return rc == 0;
}
#else
// Ruta SOLO DE PC: la misma comprobacion sobre los mismos bytes (OpenSSL acepta tambien S alto, que es la misma firma)
bool flex_account_verify_signature(const uint8_t *payload, size_t payload_len, const uint8_t signature[64])
{
    uint8_t digest[32];
    if (!flex_account_sha256(payload, payload_len, digest)) {
        return false;
    }
    bool ok = false;
    EC_KEY *key = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    EC_POINT *pt = key ? EC_POINT_new(EC_KEY_get0_group(key)) : NULL;
    ECDSA_SIG *sig = ECDSA_SIG_new();
    BIGNUM *r = BN_bin2bn(signature, 32, NULL);
    BIGNUM *s = BN_bin2bn(signature + 32, 32, NULL);
    if (key && pt && sig && r && s && EC_POINT_oct2point(EC_KEY_get0_group(key), pt, account_key(), sizeof(ACCOUNT_PUBLIC_KEY), NULL) == 1 &&
        EC_KEY_set_public_key(key, pt) == 1 && ECDSA_SIG_set0(sig, r, s) == 1) {
        r = s = NULL;   // ahora son de sig
        ok = ECDSA_do_verify(digest, 32, sig, key) == 1;
    }
    BN_free(r);
    BN_free(s);
    ECDSA_SIG_free(sig);
    EC_POINT_free(pt);
    EC_KEY_free(key);
    return ok;
}
#endif

bool flex_account_signed_payload(const uint8_t *envelope, size_t envelope_len, uint8_t **payload, size_t *payload_len)
{
    *payload = NULL;
    *payload_len = 0;
    cJSON *root = cJSON_ParseWithLength((const char *)envelope, envelope_len);
    if (!root) {
        return false;
    }
    cJSON *schema = cJSON_GetObjectItemCaseSensitive(root, "schema");
    cJSON *algorithm = cJSON_GetObjectItemCaseSensitive(root, "algorithm");
    cJSON *key_id = cJSON_GetObjectItemCaseSensitive(root, "keyId");
    cJSON *encoded = cJSON_GetObjectItemCaseSensitive(root, "payload");
    cJSON *signature = cJSON_GetObjectItemCaseSensitive(root, "signature");
    bool ok = cJSON_IsNumber(schema) && schema->valuedouble == 2.0 && cJSON_IsString(algorithm) && !strcmp(algorithm->valuestring, "ES256") && cJSON_IsString(key_id) &&
              !strcmp(key_id->valuestring, FLEX_ACCOUNT_KEY_ID) && cJSON_IsString(encoded) && cJSON_IsString(signature);
    uint8_t *sig = NULL;
    size_t sig_len = 0;
    if (ok) {
        ok = flex_account_b64url_decode(encoded->valuestring, payload, payload_len, FLEX_ACCOUNT_ENVELOPE_MAX) &&
             flex_account_b64url_decode(signature->valuestring, &sig, &sig_len, 64) && sig_len == 64;
    }
    cJSON_Delete(root);
    if (ok) {
        ok = flex_account_verify_signature(*payload, *payload_len, sig);
    }
    free(sig);
    if (!ok) {
        free(*payload);
        *payload = NULL;
        *payload_len = 0;
    }
    return ok;
}

static bool copy_json_string(cJSON *object, const char *key, char *out, size_t cap, bool required)
{
    cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (cJSON_IsNull(value) && !required) {
        out[0] = 0;
        return true;
    }
    if (!cJSON_IsString(value) || !value->valuestring || strlen(value->valuestring) >= cap) {
        if (!required) {
            out[0] = 0;
        }
        return !required;
    }
    memcpy(out, value->valuestring, strlen(value->valuestring) + 1);
    return true;
}

bool flex_account_valid_address(const char *address)
{
    size_t len = address ? strlen(address) : 0;
    return len >= 6 && strstr(address, "@flex") == address + len - 5;
}

bool flex_account_parse_code(const uint8_t *payload, size_t len, const char *expected_hash, char code[9], char activation_url[192])
{
    cJSON *root = cJSON_ParseWithLength((const char *)payload, len);
    if (!root) {
        return false;
    }
    cJSON *schema = cJSON_GetObjectItemCaseSensitive(root, "schema");
    cJSON *purpose = cJSON_GetObjectItemCaseSensitive(root, "purpose");
    char token_hash[65] = "";
    bool ok = cJSON_IsNumber(schema) && schema->valuedouble == 1.0 && cJSON_IsString(purpose) && !strcmp(purpose->valuestring, "flex-device-code") &&
              copy_json_string(root, "code", code, 9, true) && copy_json_string(root, "tokenHash", token_hash, sizeof(token_hash), true) &&
              copy_json_string(root, "activationUrl", activation_url, 192, true) && !strcmp(token_hash, expected_hash) && strlen(code) >= 5 &&
              !strncmp(activation_url, FLEX_ACCOUNT_ACTIVATE_PREFIX, strlen(FLEX_ACCOUNT_ACTIVATE_PREFIX));
    for (size_t i = 0; ok && code[i]; i++) {
        char c = code[i];
        ok = (c >= 'A' && c <= 'Z') || (c >= '2' && c <= '9');
    }
    cJSON_Delete(root);
    return ok;
}

flex_poll_t flex_account_parse_status(const uint8_t *payload, size_t len, char address[48], char display_name[64])
{
    cJSON *root = cJSON_ParseWithLength((const char *)payload, len);
    if (!root) {
        return FLEX_POLL_INVALID;
    }
    cJSON *schema = cJSON_GetObjectItemCaseSensitive(root, "schema");
    cJSON *purpose = cJSON_GetObjectItemCaseSensitive(root, "purpose");
    cJSON *state = cJSON_GetObjectItemCaseSensitive(root, "state");
    flex_poll_t result = FLEX_POLL_INVALID;
    if (cJSON_IsNumber(schema) && schema->valuedouble == 1.0 && cJSON_IsString(purpose) && !strcmp(purpose->valuestring, "flex-device-status") && cJSON_IsString(state)) {
        if (!strcmp(state->valuestring, "pending")) {
            result = FLEX_POLL_PENDING;
        } else if (!strcmp(state->valuestring, "expired")) {
            result = FLEX_POLL_EXPIRED;
        } else if (!strcmp(state->valuestring, "approved")) {
            if (copy_json_string(root, "flexAddress", address, 48, true) && copy_json_string(root, "displayName", display_name, 64, false) &&
                flex_account_valid_address(address)) {
                result = FLEX_POLL_APPROVED;
            }
        }
    }
    cJSON_Delete(root);
    return result;
}

#if FXA_BACKEND_OPENSSL
#pragma GCC diagnostic pop
#endif
