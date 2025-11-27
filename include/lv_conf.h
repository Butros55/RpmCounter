/**
 * @file lv_conf.h
 * Configuration file for LVGL
 */

#if 1 /*Set it to "1" to enable content*/

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

#define LV_HOR_RES_MAX (280)
#define LV_VER_RES_MAX (456)

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0
#define LV_COLOR_SCREEN_TRANSP 0

#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (48U * 1024U)
#define LV_MEM_ADR 0

#define LV_DISP_DEF_REFR_PERIOD 30

#define LV_INDEV_DEF_READ_PERIOD 30

#define LV_TICK_CUSTOM 0
#define LV_TICK_CUSTOM_INCLUDE "stdint.h"

#define LV_LOG_LEVEL LV_LOG_LEVEL_NONE

#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_MONTSERRAT_48 1

#define LV_FONT_DEFAULT &lv_font_montserrat_16

#define LV_THEME_DEFAULT_DARK 0
#define LV_THEME_DEFAULT_LIGHT 1

#define LV_USE_GESTURE 1

#endif /*LV_CONF_H*/

#endif /*LV_CONF_H_ENABLE*/
