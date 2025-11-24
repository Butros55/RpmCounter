// file: include/esp_private/periph_ctrl.h
#pragma once

// Hole den offiziellen periph_module_t-Typ aus dem SDK
#include "soc/periph_defs.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // Minimal-Stubs: werden von Arduino_GFX aufgerufen,
    // müssen für dich aber nichts tun.
    static inline void periph_module_enable(periph_module_t) {}
    static inline void periph_module_disable(periph_module_t) {}
    static inline void periph_module_reset(periph_module_t) {}

#ifdef __cplusplus
}
#endif
