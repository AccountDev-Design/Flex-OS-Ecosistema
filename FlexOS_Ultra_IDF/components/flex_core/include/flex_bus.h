// Flex OS Ultra · bus de eventos del sistema (plan, seccion 5.4).
//
//   servicio --flex_bus_post()--> bucle "flex_bus" (nucleo 0) --> manejadores
//
// Para difundir cambios de estado (Wi-Fi conectado, nube sin cuota, ajuste
// cambiado...). Publicar nunca bloquea: si la cola esta llena el evento se
// descarta y se cuenta. Los manejadores corren en la tarea del bus: deben ser
// cortos y no pueden llamar a LVGL (la UI recibe los eventos por su buzon,
// flex_inbox.h). Los datos del evento se copian: el emisor puede liberar los
// suyos al volver.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

ESP_EVENT_DECLARE_BASE(FLEX_EV_SYSTEM);    // arranque, memoria, temperatura, reinicio
ESP_EVENT_DECLARE_BASE(FLEX_EV_STORAGE);   // NVS / LittleFS
ESP_EVENT_DECLARE_BASE(FLEX_EV_SETTINGS);  // un ajuste ha cambiado
ESP_EVENT_DECLARE_BASE(FLEX_EV_NET);       // Wi-Fi / C6 / IP / hora
ESP_EVENT_DECLARE_BASE(FLEX_EV_ACCOUNT);   // Flex Account
ESP_EVENT_DECLARE_BASE(FLEX_EV_CLOUD);     // Flex Cloud
ESP_EVENT_DECLARE_BASE(FLEX_EV_MEDIA);     // reproductor, biblioteca, camara
ESP_EVENT_DECLARE_BASE(FLEX_EV_AUDIO);     // codec / volumen
ESP_EVENT_DECLARE_BASE(FLEX_EV_SENSOR);    // IMU, brujula, caidas, robo
ESP_EVENT_DECLARE_BASE(FLEX_EV_POWER);     // brillo, suspension, apagado
ESP_EVENT_DECLARE_BASE(FLEX_EV_OTA);       // actualizaciones
ESP_EVENT_DECLARE_BASE(FLEX_EV_NOTIF);     // notificaciones del sistema y del telefono

// Datos maximos que se copian con un evento. Lo mayor viaja como puntero a un
// estado que el servicio expone con su propia funcion de copia.
#define FLEX_BUS_MAX_DATA 128

esp_err_t flex_bus_init(void);
esp_err_t flex_bus_post(esp_event_base_t base, int32_t id, const void *data, size_t len);
esp_err_t flex_bus_subscribe(esp_event_base_t base, int32_t id, esp_event_handler_t handler, void *arg);
esp_err_t flex_bus_unsubscribe(esp_event_base_t base, int32_t id, esp_event_handler_t handler);
uint32_t  flex_bus_dropped(void);

#ifdef __cplusplus
}
#endif
