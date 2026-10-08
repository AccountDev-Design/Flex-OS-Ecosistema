// Verificacion de la clave en una tarea aparte: la de UI nunca espera al PBKDF2.
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "flex_core.h"
#include "flex_inbox.h"
#include "flex_passcode.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

typedef struct {
    char secret[FLEX_LOCK_SECRET_MAX];
    flex_lock_result_cb_t cb;
    void *user;
    bool ok;
} job_t;

static volatile bool s_busy;

static void deliver(void *arg)
{
    job_t *j = arg;
    j->cb(j->ok, j->user);
    free(j);
}

static void verify_task(void *arg)
{
    job_t *j = arg;
    j->ok = flex_lock_verify_alone(j->secret);
    flex_lock_wipe(j->secret, sizeof(j->secret));
    s_busy = false;
    while (!flex_inbox_post(deliver, j)) {
        vTaskDelay(pdMS_TO_TICKS(10));   // el buzon se vacia en la siguiente vuelta de la UI
    }
    vTaskDelete(NULL);
}

bool flex_lock_verify_async(const char *secret, flex_lock_result_cb_t cb, void *user)
{
    if (!secret || !cb || strlen(secret) >= FLEX_LOCK_SECRET_MAX || s_busy) {
        return false;
    }
    job_t *j = calloc(1, sizeof(*j));
    if (!j) {
        return false;
    }
    memcpy(j->secret, secret, strlen(secret) + 1);
    j->cb = cb;
    j->user = user;
    s_busy = true;
    // Nucleo 0, prioridad baja: la UI (nucleo 1) sigue a 60 Hz mientras tanto.
    if (xTaskCreatePinnedToCore(verify_task, "lockverify", 4096, j, 2, NULL, 0) != pdPASS) {
        s_busy = false;
        flex_lock_wipe(j->secret, sizeof(j->secret));
        free(j);
        return false;
    }
    return true;
}
