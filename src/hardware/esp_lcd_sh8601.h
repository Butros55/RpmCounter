/*
 * Minimal SH8601 panel definitions (ported from Espressif esp-iot-solution)
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_lcd_panel_vendor.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    int cmd;
    const void *data;
    size_t data_bytes;
    unsigned int delay_ms;
} sh8601_lcd_init_cmd_t;

typedef struct
{
    const sh8601_lcd_init_cmd_t *init_cmds;
    uint16_t init_cmds_size;
    struct
    {
        unsigned int use_qspi_interface : 1;
    } flags;
} sh8601_vendor_config_t;

#define ESP_LCD_SH8601_VER_MAJOR (1)
#define ESP_LCD_SH8601_VER_MINOR (0)
#define ESP_LCD_SH8601_VER_PATCH (0)

esp_err_t esp_lcd_new_panel_sh8601(const esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *panel_dev_config, esp_lcd_panel_handle_t *ret_panel);

#define SH8601_PANEL_BUS_SPI_CONFIG(sclk, mosi, max_trans_sz) \
    {                                                         \
        .sclk_io_num = sclk,                                  \
        .mosi_io_num = mosi,                                  \
        .miso_io_num = -1,                                    \
        .quadhd_io_num = -1,                                  \
        .quadwp_io_num = -1,                                  \
        .max_transfer_sz = max_trans_sz,                      \
    }
#define SH8601_PANEL_BUS_QSPI_CONFIG(sclk, d0, d1, d2, d3, max_trans_sz) \
    {                                                                    \
        .data0_io_num = d0,                                              \
        .data1_io_num = d1,                                              \
        .sclk_io_num = sclk,                                             \
        .data2_io_num = d2,                                              \
        .data3_io_num = d3,                                              \
        .data4_io_num = -1,                                              \
        .data5_io_num = -1,                                              \
        .data6_io_num = -1,                                              \
        .data7_io_num = -1,                                              \
        .max_transfer_sz = max_trans_sz,                                 \
        .flags = 0,                                                      \
        .intr_flags = 0,                                                 \
    }

#define SH8601_PANEL_IO_SPI_CONFIG(cs, dc, cb, cb_ctx) \
    {                                                  \
        .cs_gpio_num = cs,                             \
        .dc_gpio_num = dc,                             \
        .spi_mode = 0,                                 \
        .pclk_hz = 40 * 1000 * 1000,                   \
        .trans_queue_depth = 10,                       \
        .on_color_trans_done = cb,                     \
        .user_ctx = cb_ctx,                            \
        .lcd_cmd_bits = 8,                             \
        .lcd_param_bits = 8,                           \
    }
#define SH8601_PANEL_IO_QSPI_CONFIG(cs, cb, cb_ctx) \
    {                                               \
        .cs_gpio_num = cs,                          \
        .dc_gpio_num = -1,                          \
        .spi_mode = 0,                              \
        .pclk_hz = 40 * 1000 * 1000,                \
        .trans_queue_depth = 10,                    \
        .on_color_trans_done = cb,                  \
        .user_ctx = cb_ctx,                         \
        .lcd_cmd_bits = 32,                         \
        .lcd_param_bits = 8,                        \
        .flags = {0},                               \
    }

#ifdef __cplusplus
}
#endif
