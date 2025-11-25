#include "display_s3.h"

#if defined(CONFIG_IDF_TARGET_ESP32S3)

#include <Wire.h>
#include <esp_err.h>
#include <lvgl.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include <driver/spi_master.h>

#include "core/wifi.h"
#include "core/state.h"
#include "hardware/display.h"
#include "ui/ui_main.h"
#include "esp_lcd_sh8601.h"

namespace
{
    constexpr int LCD_H_RES = 280;
    constexpr int LCD_V_RES = 456;
    constexpr int LCD_X_OFFSET = 0x14;
    constexpr int LVGL_BUFFER_LINES = 40;

    constexpr int PIN_LCD_CS = 9;
    constexpr int PIN_LCD_CLK = 10;
    constexpr int PIN_LCD_D0 = 11;
    constexpr int PIN_LCD_D1 = 12;
    constexpr int PIN_LCD_D2 = 13;
    constexpr int PIN_LCD_D3 = 14;
    constexpr int PIN_LCD_RST = 21;
    constexpr int PIN_TOUCH_SDA = 47;
    constexpr int PIN_TOUCH_SCL = 48;
    constexpr uint8_t TOUCH_ADDR = 0x38;

    static const char *TAG = "display_s3";

    static lv_disp_draw_buf_t g_drawBuf;
    static lv_disp_drv_t g_dispDrv;
    static lv_color_t *g_buf1 = nullptr;
    static lv_color_t *g_buf2 = nullptr;
    static lv_disp_t *g_disp = nullptr;
    static bool g_displayReady = false;
    static bool g_touchReady = false;
    static int g_cachedGear = 0;
    static bool g_cachedShift = false;
    static bool g_logoRequested = false;
    static uint32_t g_lastTick = 0;
    static esp_lcd_panel_handle_t g_panel = nullptr;
    static esp_lcd_panel_io_handle_t g_panelIo = nullptr;

    struct TouchPoint
    {
        bool touched = false;
        uint16_t x = 0;
        uint16_t y = 0;

        TouchPoint() = default;
        TouchPoint(bool t, uint16_t px, uint16_t py) : touched(t), x(px), y(py) {}
    };

    bool i2c_write_reg(uint8_t addr, uint8_t reg, const uint8_t *data, size_t len)
    {
        Wire.beginTransmission(addr);
        Wire.write(reg);
        for (size_t i = 0; i < len; ++i)
        {
            Wire.write(data[i]);
        }
        return Wire.endTransmission() == 0;
    }

    bool i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *out, size_t len)
    {
        Wire.beginTransmission(addr);
        Wire.write(reg);
        if (Wire.endTransmission(false) != 0)
        {
            return false;
        }
        size_t read = Wire.requestFrom(addr, static_cast<uint8_t>(len));
        if (read != len)
            return false;
        for (size_t i = 0; i < len; ++i)
        {
            out[i] = Wire.read();
        }
        return true;
    }

    bool ft3168_init()
    {
        uint8_t mode = 0x00;
        bool ok = i2c_write_reg(TOUCH_ADDR, 0x00, &mode, 1);
        if (!ok)
        {
            ESP_LOGW(TAG, "FT3168 init failed");
        }
        return ok;
    }

    TouchPoint ft3168_read_touch()
    {
        TouchPoint p{false, 0, 0};
        if (!g_touchReady)
        {
            return p;
        }

        uint8_t points = 0;
        if (!i2c_read_reg(TOUCH_ADDR, 0x02, &points, 1) || points == 0)
        {
            return p;
        }

        uint8_t buf[4] = {0};
        if (!i2c_read_reg(TOUCH_ADDR, 0x03, buf, sizeof(buf)))
        {
            return p;
        }

        uint16_t x = static_cast<uint16_t>(((buf[0] & 0x0F) << 8) | buf[1]);
        uint16_t y = static_cast<uint16_t>(((buf[2] & 0x0F) << 8) | buf[3]);

        // Swap to match screen orientation (portrait)
        p.x = y;
        p.y = x;
        p.touched = true;

        if (p.x >= LCD_H_RES)
            p.x = LCD_H_RES - 1;
        if (p.y >= LCD_V_RES)
            p.y = LCD_V_RES - 1;

        return p;
    }

    bool notify_flush_ready(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *user_ctx)
    {
        lv_disp_drv_t *drv = static_cast<lv_disp_drv_t *>(user_ctx);
        lv_disp_flush_ready(drv);
        return false;
    }

    void display_flush_cb(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
    {
        if (!g_panel)
        {
            lv_disp_flush_ready(disp);
            return;
        }

        if (area->x2 < 0 || area->y2 < 0 || area->x1 >= LCD_H_RES || area->y1 >= LCD_V_RES)
        {
            lv_disp_flush_ready(disp);
            return;
        }

        esp_lcd_panel_handle_t panel = static_cast<esp_lcd_panel_handle_t>(disp->user_data);
        esp_err_t err = esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_p);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "panel_draw_bitmap failed: %d", static_cast<int>(err));
            lv_disp_flush_ready(disp);
        }
    }

    void touch_read_cb(lv_indev_drv_t *, lv_indev_data_t *data)
    {
        static TouchPoint lastPoint{false, 0, 0};
        TouchPoint p = ft3168_read_touch();
        if (p.touched)
        {
            data->state = LV_INDEV_STATE_PRESSED;
            data->point.x = p.x;
            data->point.y = p.y;
            lastPoint = p;
        }
        else
        {
            data->state = LV_INDEV_STATE_RELEASED;
            data->point.x = lastPoint.x;
            data->point.y = lastPoint.y;
        }
    }
} // namespace

void displayInit()
{
    display_s3_init();
}

void displayClear()
{
    if (!g_displayReady)
        return;

    g_cachedShift = false;
    ui_main_set_shiftlight(false);
    ui_main_set_gear(g_cachedGear);
}

void displayShowTestLogo()
{
    g_logoRequested = true;
    if (!g_displayReady)
        return;

    ui_main_show_test_logo();
}

void displaySetGear(int gear)
{
    if (gear < 0)
        gear = 0;

    g_cachedGear = gear;
    if (!g_displayReady)
        return;

    ui_main_set_gear(gear);
}

void displaySetShiftBlink(bool active)
{
    g_cachedShift = active;
    if (!g_displayReady)
        return;

    ui_main_set_shiftlight(active);
}

void display_s3_init()
{
    if (g_displayReady)
        return;

    lv_init();

    size_t bufSize = LCD_H_RES * LVGL_BUFFER_LINES * sizeof(lv_color_t);
    g_buf1 = static_cast<lv_color_t *>(heap_caps_malloc(bufSize, MALLOC_CAP_DMA));
    g_buf2 = static_cast<lv_color_t *>(heap_caps_malloc(bufSize, MALLOC_CAP_DMA));
    if (!g_buf1 || !g_buf2)
    {
        // fallback to non-DMA heap if necessary
        if (!g_buf1)
            g_buf1 = new lv_color_t[LCD_H_RES * LVGL_BUFFER_LINES];
        if (!g_buf2)
            g_buf2 = new lv_color_t[LCD_H_RES * LVGL_BUFFER_LINES];
    }

    if (!g_buf1 || !g_buf2)
    {
        ESP_LOGE(TAG, "Failed to allocate LVGL buffers");
        return;
    }

    lv_disp_draw_buf_init(&g_drawBuf, g_buf1, g_buf2, LCD_H_RES * LVGL_BUFFER_LINES);

    spi_bus_config_t buscfg = SH8601_PANEL_BUS_QSPI_CONFIG(
        PIN_LCD_CLK,
        PIN_LCD_D0,
        PIN_LCD_D1,
        PIN_LCD_D2,
        PIN_LCD_D3,
        LCD_H_RES * LVGL_BUFFER_LINES * sizeof(lv_color_t));
    esp_err_t err = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %d", static_cast<int>(err));
        return;
    }

    esp_lcd_panel_io_spi_config_t io_config = SH8601_PANEL_IO_QSPI_CONFIG(PIN_LCD_CS, notify_flush_ready, &g_dispDrv);
    err = esp_lcd_new_panel_io_spi(reinterpret_cast<esp_lcd_spi_bus_handle_t>(SPI2_HOST), &io_config, &g_panelIo);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "new_panel_io_spi failed: %d", static_cast<int>(err));
        return;
    }

    static const uint8_t cmd_11[] = {0x00};
    static const uint8_t cmd_c4[] = {0x80};
    static const uint8_t cmd_35[] = {0x00};
    static const uint8_t cmd_53[] = {0x20};
    static const uint8_t cmd_63[] = {0xFF};
    static const uint8_t cmd_51_0[] = {0x00};
    static const uint8_t cmd_29[] = {0x00};
    static const uint8_t cmd_51_ff[] = {0xFF};
    static const sh8601_lcd_init_cmd_t lcd_init_cmds[] = {
        {0x11, cmd_11, sizeof(cmd_11), 80},
        {0xC4, cmd_c4, sizeof(cmd_c4), 0},
        {0x35, cmd_35, sizeof(cmd_35), 0},
        {0x53, cmd_53, sizeof(cmd_53), 1},
        {0x63, cmd_63, sizeof(cmd_63), 1},
        {0x51, cmd_51_0, sizeof(cmd_51_0), 1},
        {0x29, cmd_29, sizeof(cmd_29), 10},
        {0x51, cmd_51_ff, sizeof(cmd_51_ff), 0},
    };

    sh8601_vendor_config_t vendor_config = {
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
        .flags = {.use_qspi_interface = 1},
    };

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_LCD_RST,
        .color_space = ESP_LCD_COLOR_SPACE_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_config,
    };
    err = esp_lcd_new_panel_sh8601(g_panelIo, &panel_config, &g_panel);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "new_panel_sh8601 failed: %d", static_cast<int>(err));
        return;
    }
    if (esp_lcd_panel_reset(g_panel) != ESP_OK)
    {
        ESP_LOGE(TAG, "panel_reset failed");
        return;
    }
    if (esp_lcd_panel_init(g_panel) != ESP_OK)
    {
        ESP_LOGE(TAG, "panel_init failed");
        return;
    }
    if (esp_lcd_panel_set_gap(g_panel, LCD_X_OFFSET, 0) != ESP_OK)
    {
        ESP_LOGE(TAG, "panel_set_gap failed");
        return;
    }
    if (esp_lcd_panel_disp_on_off(g_panel, true) != ESP_OK)
    {
        ESP_LOGE(TAG, "panel_disp_on failed");
        return;
    }

    lv_disp_drv_init(&g_dispDrv);
    g_dispDrv.hor_res = LCD_H_RES;
    g_dispDrv.ver_res = LCD_V_RES;
    g_dispDrv.flush_cb = display_flush_cb;
    g_dispDrv.draw_buf = &g_drawBuf;
    g_dispDrv.user_data = g_panel;
    g_disp = lv_disp_drv_register(&g_dispDrv);

    g_touchReady = Wire.begin(PIN_TOUCH_SDA, PIN_TOUCH_SCL, 400000U) && ft3168_init();
    if (g_touchReady)
    {
        lv_indev_drv_t indev_drv;
        lv_indev_drv_init(&indev_drv);
        indev_drv.type = LV_INDEV_TYPE_POINTER;
        indev_drv.read_cb = touch_read_cb;
        indev_drv.disp = g_disp;
        lv_indev_drv_register(&indev_drv);
    }
    else
    {
        ESP_LOGW(TAG, "Touch controller not ready");
    }

    ui_main_init(g_disp);
    ui_main_set_gear(g_cachedGear);
    ui_main_set_shiftlight(g_cachedShift);
    if (g_logoRequested)
    {
        ui_main_show_test_logo();
    }

    g_displayReady = true;
    g_lastTick = millis();
    ESP_LOGI(TAG, "Display + touch initialized");
}

bool display_s3_ready()
{
    return g_displayReady;
}

void display_s3_loop()
{
    if (!g_displayReady)
        return;

    uint32_t now = millis();
    uint32_t elapsed = now - g_lastTick;
    lv_tick_inc(elapsed);
    g_lastTick = now;

    WifiStatus wifiStatus = getWifiStatus();
    const bool wifiConnected = wifiStatus.staConnected || wifiStatus.apActive;
    const bool wifiConnecting = wifiStatus.staConnecting || wifiStatus.scanRunning;

    ui_main_update_status(wifiConnected, wifiConnecting, g_connected, g_bleConnectInProgress);
    ui_main_loop();
}

#endif
