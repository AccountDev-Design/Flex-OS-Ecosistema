// Simulador: la verificacion de la clave se hace en el acto y el resultado
// llega por el buzon de la UI, igual que en la placa (donde la hace una tarea).
#include <stdlib.h>
#include <string.h>
#include "flex_inbox.h"
#include "flex_passcode.h"

typedef struct {
    flex_lock_result_cb_t cb;
    void *user;
    bool ok;
} job_t;

static void deliver(void *arg)
{
    job_t *j = arg;
    j->cb(j->ok, j->user);
    free(j);
}

static bool launch(const char *secret, int set_type, flex_lock_result_cb_t cb, void *user)
{
    if (!secret || !cb || strlen(secret) >= FLEX_LOCK_SECRET_MAX) {
        return false;
    }
    job_t *j = calloc(1, sizeof(*j));
    if (!j) {
        return false;
    }
    j->cb = cb;
    j->user = user;
    j->ok = set_type ? flex_lock_set(secret, set_type) : flex_lock_verify_alone(secret);
    if (!flex_inbox_post(deliver, j)) {
        free(j);
        return false;
    }
    return true;
}

bool flex_lock_verify_async(const char *secret, flex_lock_result_cb_t cb, void *user)
{
    return launch(secret, 0, cb, user);
}

bool flex_lock_set_async(const char *secret, int type, flex_lock_result_cb_t cb, void *user)
{
    if (type != FLEX_LOCK_PIN && type != FLEX_LOCK_PASS) {
        return false;
    }
    return launch(secret, type, cb, user);
}
