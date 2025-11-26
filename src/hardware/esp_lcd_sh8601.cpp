/*
 * SH8601 panel driver (ported from Espressif esp-iot-solution)
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <sys/cdefs.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_commands.h"
#include "esp_log.h"
#include "esp_bit_defs.h"

#include "esp_lcd_sh8601.h"

#define LCD_OPCODE_WRITE_CMD (0x02ULL)
#define LCD_OPCODE_READ_CMD (0x03ULL)
#define LCD_OPCODE_WRITE_COLOR (0x32ULL)

static const char *TAG = "sh8601";

static esp_err_t panel_sh8601_del(esp_lcd_panel_t *panel);
static esp_err_t panel_sh8601_reset(esp_lcd_panel_t *panel);
static esp_err_t panel_sh8601_init(esp_lcd_panel_t *panel);
static esp_err_t panel_sh8601_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end, const void *color_data);
static esp_err_t panel_sh8601_invert_color(esp_lcd_panel_t *panel, bool invert_color_data);
static esp_err_t panel_sh8601_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y);
static esp_err_t panel_sh8601_swap_xy(esp_lcd_panel_t *panel, bool swap_axes);
static esp_err_t panel_sh8601_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap);
static esp_err_t panel_sh8601_disp_on_off(esp_lcd_panel_t *panel, bool off);

typedef struct
{
    esp_lcd_panel_t base;
    esp_lcd_panel_io_handle_t io;
    int reset_gpio_num;
    int x_gap;
    int y_gap;
    uint8_t fb_bits_per_pixel;
    uint8_t madctl_val;
    uint8_t colmod_val;
    const sh8601_lcd_init_cmd_t *init_cmds;
    uint16_t init_cmds_size;
    struct
    {
        unsigned int use_qspi_interface : 1;
        unsigned int reset_level : 1;
    } flags;
} sh8601_panel_t;

extern "C" esp_err_t esp_lcd_new_panel_sh8601(const esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *panel_dev_config, esp_lcd_panel_handle_t *ret_panel)
{
    if (!(io && panel_dev_config && ret_panel))
    {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ESP_OK;
    uint8_t fb_bits_per_pixel = 0;
    sh8601_vendor_config_t *vendor_config = nullptr;
    sh8601_panel_t *sh8601 = static_cast<sh8601_panel_t *>(calloc(1, sizeof(sh8601_panel_t)));
    if (!sh8601)
    {
        return ESP_ERR_NO_MEM;
    }

    if (panel_dev_config->reset_gpio_num >= 0)
    {
        gpio_config_t io_conf = {};
        io_conf.pin_bit_mask = 1ULL << panel_dev_config->reset_gpio_num;
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.intr_type = GPIO_INTR_DISABLE;
        ret = gpio_config(&io_conf);
        if (ret != ESP_OK)
        {
            goto cleanup;
        }
    }

    switch (panel_dev_config->color_space)
    {
    case ESP_LCD_COLOR_SPACE_RGB:
        sh8601->madctl_val = 0;
        break;
    case ESP_LCD_COLOR_SPACE_BGR:
        sh8601->madctl_val |= LCD_CMD_BGR_BIT;
        break;
    default:
        ret = ESP_ERR_NOT_SUPPORTED;
        goto cleanup;
    }

    switch (panel_dev_config->bits_per_pixel)
    {
    case 16:
        sh8601->colmod_val = 0x55;
        fb_bits_per_pixel = 16;
        break;
    case 18:
        sh8601->colmod_val = 0x66;
        fb_bits_per_pixel = 24;
        break;
    case 24:
        sh8601->colmod_val = 0x77;
        fb_bits_per_pixel = 24;
        break;
    default:
        ret = ESP_ERR_NOT_SUPPORTED;
        goto cleanup;
    }

    sh8601->io = io;
    sh8601->reset_gpio_num = panel_dev_config->reset_gpio_num;
    sh8601->fb_bits_per_pixel = fb_bits_per_pixel;
    vendor_config = (sh8601_vendor_config_t *)panel_dev_config->vendor_config;
    if (vendor_config)
    {
        sh8601->init_cmds = vendor_config->init_cmds;
        sh8601->init_cmds_size = vendor_config->init_cmds_size;
        sh8601->flags.use_qspi_interface = vendor_config->flags.use_qspi_interface;
    }
    sh8601->flags.reset_level = panel_dev_config->flags.reset_active_high;
    sh8601->base.del = panel_sh8601_del;
    sh8601->base.reset = panel_sh8601_reset;
    sh8601->base.init = panel_sh8601_init;
    sh8601->base.draw_bitmap = panel_sh8601_draw_bitmap;
    sh8601->base.invert_color = panel_sh8601_invert_color;
    sh8601->base.set_gap = panel_sh8601_set_gap;
    sh8601->base.mirror = panel_sh8601_mirror;
    sh8601->base.swap_xy = panel_sh8601_swap_xy;
    sh8601->base.disp_on_off = panel_sh8601_disp_on_off;
    *ret_panel = &(sh8601->base);
    ESP_LOGD(TAG, "new sh8601 panel @%p", sh8601);
    ESP_LOGI(TAG, "LCD panel create success, version: %d.%d.%d", ESP_LCD_SH8601_VER_MAJOR, ESP_LCD_SH8601_VER_MINOR, ESP_LCD_SH8601_VER_PATCH);
    return ESP_OK;

cleanup:
    if (sh8601)
    {
        if (panel_dev_config->reset_gpio_num >= 0)
        {
            gpio_reset_pin(static_cast<gpio_num_t>(panel_dev_config->reset_gpio_num));
        }
        free(sh8601);
    }
    return ret;
}

static esp_err_t tx_param(sh8601_panel_t *sh8601, esp_lcd_panel_io_handle_t io, int lcd_cmd, const void *param, size_t param_size)
{
    if (sh8601->flags.use_qspi_interface)
    {
        lcd_cmd &= 0xff;
        lcd_cmd <<= 8;
        lcd_cmd |= LCD_OPCODE_WRITE_CMD << 24;
    }
    return esp_lcd_panel_io_tx_param(io, lcd_cmd, param, param_size);
}

static esp_err_t tx_color(sh8601_panel_t *sh8601, esp_lcd_panel_io_handle_t io, int lcd_cmd, const void *param, size_t param_size)
{
    if (sh8601->flags.use_qspi_interface)
    {
        lcd_cmd &= 0xff;
        lcd_cmd <<= 8;
        lcd_cmd |= LCD_OPCODE_WRITE_COLOR << 24;
    }
    return esp_lcd_panel_io_tx_color(io, lcd_cmd, param, param_size);
}

static esp_err_t panel_sh8601_del(esp_lcd_panel_t *panel)
{
    sh8601_panel_t *sh8601 = __containerof(panel, sh8601_panel_t, base);

    if (sh8601->reset_gpio_num >= 0)
    {
        gpio_reset_pin(static_cast<gpio_num_t>(sh8601->reset_gpio_num));
    }
    ESP_LOGD(TAG, "del sh8601 panel @%p", sh8601);
    free(sh8601);
    return ESP_OK;
}

static esp_err_t panel_sh8601_reset(esp_lcd_panel_t *panel)
{
    sh8601_panel_t *sh8601 = __containerof(panel, sh8601_panel_t, base);
    esp_lcd_panel_io_handle_t io = sh8601->io;

    if (sh8601->reset_gpio_num >= 0)
    {
        gpio_set_level(static_cast<gpio_num_t>(sh8601->reset_gpio_num), sh8601->flags.reset_level);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(static_cast<gpio_num_t>(sh8601->reset_gpio_num), !sh8601->flags.reset_level);
        vTaskDelay(pdMS_TO_TICKS(150));
    }
    else
    {
        ESP_RETURN_ON_ERROR(tx_param(sh8601, io, LCD_CMD_SWRESET, nullptr, 0), TAG, "send command failed");
        vTaskDelay(pdMS_TO_TICKS(80));
    }

    return ESP_OK;
}

static const uint8_t init_44[] = {0x00, 0xc8};
static const uint8_t init_35[] = {0x00};
static const uint8_t init_53[] = {0x20};
static const sh8601_lcd_init_cmd_t vendor_specific_init_default[] = {
    {0x44, init_44, sizeof(init_44), 0},
    {0x35, init_35, sizeof(init_35), 0},
    {0x53, init_53, sizeof(init_53), 25},
};

static esp_err_t panel_sh8601_init(esp_lcd_panel_t *panel)
{
    sh8601_panel_t *sh8601 = __containerof(panel, sh8601_panel_t, base);
    esp_lcd_panel_io_handle_t io = sh8601->io;
    const sh8601_lcd_init_cmd_t *init_cmds = nullptr;
    uint16_t init_cmds_size = 0;
    bool is_cmd_overwritten = false;

    uint8_t madctl_val = sh8601->madctl_val;
    uint8_t colmod_val = sh8601->colmod_val;
    ESP_RETURN_ON_ERROR(tx_param(sh8601, io, LCD_CMD_MADCTL, &madctl_val, 1), TAG, "send command failed");
    ESP_RETURN_ON_ERROR(tx_param(sh8601, io, LCD_CMD_COLMOD, &colmod_val, 1), TAG, "send command failed");

    if (sh8601->init_cmds)
    {
        init_cmds = sh8601->init_cmds;
        init_cmds_size = sh8601->init_cmds_size;
    }
    else
    {
        init_cmds = vendor_specific_init_default;
        init_cmds_size = sizeof(vendor_specific_init_default) / sizeof(sh8601_lcd_init_cmd_t);
    }

    for (int i = 0; i < init_cmds_size; i++)
    {
        switch (init_cmds[i].cmd)
        {
        case LCD_CMD_MADCTL:
            is_cmd_overwritten = true;
            sh8601->madctl_val = ((uint8_t *)init_cmds[i].data)[0];
            break;
        case LCD_CMD_COLMOD:
            is_cmd_overwritten = true;
            sh8601->colmod_val = ((uint8_t *)init_cmds[i].data)[0];
            break;
        default:
            is_cmd_overwritten = false;
            break;
        }

        if (is_cmd_overwritten)
        {
            ESP_LOGW(TAG, "The %02Xh command has been used and will be overwritten by external initialization sequence", init_cmds[i].cmd);
        }

        ESP_RETURN_ON_ERROR(tx_param(sh8601, io, init_cmds[i].cmd, init_cmds[i].data, init_cmds[i].data_bytes), TAG, "send command failed");
        vTaskDelay(pdMS_TO_TICKS(init_cmds[i].delay_ms));
    }
    ESP_LOGD(TAG, "send init commands success");

    return ESP_OK;
}

static esp_err_t panel_sh8601_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end, const void *color_data)
{
    sh8601_panel_t *sh8601 = __containerof(panel, sh8601_panel_t, base);
    assert((x_start < x_end) && (y_start < y_end) && "start position must be smaller than end position");
    esp_lcd_panel_io_handle_t io = sh8601->io;

    x_start += sh8601->x_gap;
    x_end += sh8601->x_gap;
    y_start += sh8601->y_gap;
    y_end += sh8601->y_gap;

    uint8_t caset[4] = {
        static_cast<uint8_t>((x_start >> 8) & 0xFF),
        static_cast<uint8_t>(x_start & 0xFF),
        static_cast<uint8_t>(((x_end - 1) >> 8) & 0xFF),
        static_cast<uint8_t>((x_end - 1) & 0xFF),
    };
    ESP_RETURN_ON_ERROR(tx_param(sh8601, io, LCD_CMD_CASET, caset, sizeof(caset)), TAG, "send command failed");

    uint8_t raset[4] = {
        static_cast<uint8_t>((y_start >> 8) & 0xFF),
        static_cast<uint8_t>(y_start & 0xFF),
        static_cast<uint8_t>(((y_end - 1) >> 8) & 0xFF),
        static_cast<uint8_t>((y_end - 1) & 0xFF),
    };
    ESP_RETURN_ON_ERROR(tx_param(sh8601, io, LCD_CMD_RASET, raset, sizeof(raset)), TAG, "send command failed");
    size_t len = (x_end - x_start) * (y_end - y_start) * sh8601->fb_bits_per_pixel / 8;
    tx_color(sh8601, io, LCD_CMD_RAMWR, color_data, len);

    return ESP_OK;
}

static esp_err_t panel_sh8601_invert_color(esp_lcd_panel_t *panel, bool invert_color_data)
{
    sh8601_panel_t *sh8601 = __containerof(panel, sh8601_panel_t, base);
    esp_lcd_panel_io_handle_t io = sh8601->io;
    int command = invert_color_data ? LCD_CMD_INVON : LCD_CMD_INVOFF;
    ESP_RETURN_ON_ERROR(tx_param(sh8601, io, command, nullptr, 0), TAG, "send command failed");
    return ESP_OK;
}

static esp_err_t panel_sh8601_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y)
{
    sh8601_panel_t *sh8601 = __containerof(panel, sh8601_panel_t, base);
    esp_lcd_panel_io_handle_t io = sh8601->io;
    esp_err_t ret = ESP_OK;

    if (mirror_x)
    {
        sh8601->madctl_val |= BIT(6);
    }
    else
    {
        sh8601->madctl_val &= ~BIT(6);
    }
    if (mirror_y)
    {
        ESP_LOGE(TAG, "mirror_y is not supported by this panel");
        ret = ESP_ERR_NOT_SUPPORTED;
    }
    uint8_t madctl[] = {sh8601->madctl_val};
    ESP_RETURN_ON_ERROR(tx_param(sh8601, io, LCD_CMD_MADCTL, madctl, sizeof(madctl)), TAG, "send command failed");
    return ret;
}

static esp_err_t panel_sh8601_swap_xy(esp_lcd_panel_t *panel, bool swap_axes)
{
    (void)panel;
    (void)swap_axes;
    ESP_LOGE(TAG, "swap_xy is not supported by this panel");
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t panel_sh8601_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap)
{
    sh8601_panel_t *sh8601 = __containerof(panel, sh8601_panel_t, base);
    sh8601->x_gap = x_gap;
    sh8601->y_gap = y_gap;
    return ESP_OK;
}

static esp_err_t panel_sh8601_disp_on_off(esp_lcd_panel_t *panel, bool on_off)
{
    sh8601_panel_t *sh8601 = __containerof(panel, sh8601_panel_t, base);
    esp_lcd_panel_io_handle_t io = sh8601->io;
    int command = on_off ? LCD_CMD_DISPON : LCD_CMD_DISPOFF;
    ESP_RETURN_ON_ERROR(tx_param(sh8601, io, command, nullptr, 0), TAG, "send command failed");
    return ESP_OK;
}
