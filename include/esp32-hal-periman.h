#pragma once

// Compatibility shim for Arduino_GFX on platforms where the ESP-IDF based
// Arduino core does not ship the esp32-hal-periman header yet (e.g. older
// framework-arduinoespressif32 releases). The upstream library only requires
// the header to exist; none of the periman symbols are used by this project.
//
// When the real header is available in the toolchain we defer to it.
#if defined(__has_include)
#  if __has_include_next("esp32-hal-periman.h")
#    include_next "esp32-hal-periman.h"
#    define ESP32_HAL_PERIMAN_HAS_REAL 1
#  endif
#endif

#ifndef ESP32_HAL_PERIMAN_HAS_REAL
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  ESP32_BUS_TYPE_INIT,
  ESP32_BUS_TYPE_GPIO,
  ESP32_BUS_TYPE_MAX
} peripheral_bus_type_t;

static inline const char *perimanGetTypeName(peripheral_bus_type_t) { return "unknown"; }
static inline bool perimanSetPinBus(uint8_t, peripheral_bus_type_t, void *, int8_t, int8_t) { return true; }
static inline void *perimanGetPinBus(uint8_t, peripheral_bus_type_t) { return NULL; }
static inline peripheral_bus_type_t perimanGetPinBusType(uint8_t) { return ESP32_BUS_TYPE_INIT; }
static inline int8_t perimanGetPinBusNum(uint8_t) { return -1; }
static inline int8_t perimanGetPinBusChannel(uint8_t) { return -1; }
static inline bool perimanSetBusDeinit(peripheral_bus_type_t, bool (*)(void *)) { return true; }
static inline bool perimanPinIsValid(uint8_t) { return true; }
static inline bool perimanSetPinBusExtraType(uint8_t, const char *) { return true; }
static inline const char *perimanGetPinBusExtraType(uint8_t) { return NULL; }

#ifdef __cplusplus
}
#endif
#endif // ESP32_HAL_PERIMAN_HAS_REAL
