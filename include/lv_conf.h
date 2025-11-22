#if 1
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*--------------------
 * Color settings
 *--------------------*/
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0

/*--------------------
 * Display resolution
 *--------------------*/
#define LV_HOR_RES_MAX 456
#define LV_VER_RES_MAX 280

/*--------------------
 * Memory settings
 *--------------------*/
#define LV_MEM_SIZE (64U * 1024U)

/*--------------------
 * Feature usage
 *--------------------*/
#define LV_USE_LOG 0
#define LV_USE_GPU_STM32_DMA2D 0

/*--------------------
 * Tick configuration
 *--------------------*/
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

/*--------------------
 * Misc settings
 *--------------------*/
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_32 1

#define LV_USE_THEME_DEFAULT 1

#endif // LV_CONF_H
#endif // 1
