#include "display_s3.h"

#if defined(CONFIG_IDF_TARGET_ESP32S3)

#include <esp_err.h>
#include <lvgl.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_types.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <driver/gpio.h>
#include <driver/i2c.h>
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
    constexpr int LCD_BIT_PER_PIXEL = 16;
    constexpr int LVGL_BUFFER_LINES = LCD_V_RES / 4;
    constexpr int LVGL_TICK_PERIOD_MS = 2;

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
    constexpr i2c_port_t TOUCH_PORT = I2C_NUM_0;

// Enable to show a minimal debug UI instead of the full application UI
#define DISPLAY_DEBUG_SIMPLE_UI 1

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
    static uint32_t g_lastLvglRun = 0;
    static esp_lcd_panel_handle_t g_panel = nullptr;
    static esp_lcd_panel_io_handle_t g_panelIo = nullptr;
    static esp_timer_handle_t g_lvglTickTimer = nullptr;
    static bool g_tickFallback = false;
    static bool g_buffersAllocated = false;
    static bool g_panelReady = false;
    static bool g_initAttempted = false;
    static bool g_debugSimpleUi = DISPLAY_DEBUG_SIMPLE_UI;
    static String g_lastError = F("init-not-started");

    void setLastError(const char *msg)
    {
        g_lastError = msg;
    }

    void lv_rounder_cb(lv_disp_drv_t *disp_drv, lv_area_t *area)
    {
        (void)disp_drv;

        uint16_t x1 = area->x1;
        uint16_t x2 = area->x2;
        uint16_t y1 = area->y1;
        uint16_t y2 = area->y2;

        // Startkoordinaten auf gerade Werte runden
        area->x1 = (x1 >> 1) << 1;
        area->y1 = (y1 >> 1) << 1;

        // Endkoordinaten auf „2N+1“ runden
        area->x2 = ((x2 >> 1) << 1) + 1;
        area->y2 = ((y2 >> 1) << 1) + 1;
    }

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
        const size_t bufSize = len + 1;
        uint8_t *buf = static_cast<uint8_t *>(malloc(bufSize));
        if (!buf)
        {
            return false;
        }
        buf[0] = reg;
        for (size_t i = 0; i < len; ++i)
        {
            buf[i + 1] = data[i];
        }
        esp_err_t ret = i2c_master_write_to_device(TOUCH_PORT, addr, buf, bufSize, 1000);
        free(buf);
        return ret == ESP_OK;
    }

    bool i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *out, size_t len)
    {
        esp_err_t ret = i2c_master_write_read_device(TOUCH_PORT, addr, &reg, 1, out, len, 1000);
        return ret == ESP_OK;
    }

    bool ft3168_init()
    {
        i2c_config_t conf = {};
        conf.mode = I2C_MODE_MASTER;
        conf.sda_io_num = PIN_TOUCH_SDA;
        conf.scl_io_num = PIN_TOUCH_SCL;
        conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
        conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
        conf.master.clk_speed = 300 * 1000;
        conf.clk_flags = 0;

        if (i2c_param_config(TOUCH_PORT, &conf) != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to configure I2C for touch");
            return false;
        }
        if (i2c_driver_install(TOUCH_PORT, conf.mode, 0, 0, 0) != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to install I2C driver for touch");
            return false;
        }

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

        p.x = x;
        p.y = y;
        p.touched = true;

        if (p.x > LCD_H_RES)
            p.x = LCD_H_RES;
        if (p.y > LCD_V_RES)
            p.y = LCD_V_RES;

        return p;
    }

    void display_flush_cb(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
    {
        if (!g_panel)
        {
            ESP_LOGW(TAG, "flush_cb: no panel");
            lv_disp_flush_ready(disp);
            return;
        }

        const int offsetx1 = area->x1 + LCD_X_OFFSET;
        const int offsetx2 = area->x2 + LCD_X_OFFSET;
        const int offsety1 = area->y1;
        const int offsety2 = area->y2;

        esp_lcd_panel_handle_t panel = static_cast<esp_lcd_panel_handle_t>(disp->user_data);
        esp_err_t err = esp_lcd_panel_draw_bitmap(panel, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_p);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "panel_draw_bitmap failed: %d", static_cast<int>(err));
        }

        // Always unblock LVGL in case the IO callback doesn't fire (e.g., early errors)
        lv_disp_flush_ready(disp);
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

    if (g_debugSimpleUi)
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

    if (g_debugSimpleUi)
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

    if (g_debugSimpleUi)
        return;

    ui_main_set_gear(gear);
}

void displaySetShiftBlink(bool active)
{
    g_cachedShift = active;
    if (!g_displayReady)
        return;

    if (g_debugSimpleUi)
        return;

    ui_main_set_shiftlight(active);
}

void display_s3_init()
{
    g_initAttempted = true;
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
        setLastError("lvgl-buffer-alloc-failed");
        return;
    }

    g_buffersAllocated = true;

    lv_disp_draw_buf_init(&g_drawBuf, g_buf1, g_buf2, LCD_H_RES * LVGL_BUFFER_LINES);

    spi_bus_config_t buscfg = SH8601_PANEL_BUS_QSPI_CONFIG(
        PIN_LCD_CLK,
        PIN_LCD_D0,
        PIN_LCD_D1,
        PIN_LCD_D2,
        PIN_LCD_D3,
        LCD_H_RES * LCD_V_RES * LCD_BIT_PER_PIXEL / 8);
    esp_err_t err = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %d", static_cast<int>(err));
        setLastError("spi-bus-init-failed");
        return;
    }

    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = PIN_LCD_CS,
        .dc_gpio_num = -1,
        .spi_mode = 0,
        .pclk_hz = 40 * 1000 * 1000,
        .trans_queue_depth = 10,
        .on_color_trans_done = nullptr,
        .user_ctx = nullptr,
        .lcd_cmd_bits = 32,
        .lcd_param_bits = 8,
    };
    err = esp_lcd_new_panel_io_spi(reinterpret_cast<esp_lcd_spi_bus_handle_t>(SPI2_HOST), &io_config, &g_panelIo);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "new_panel_io_spi failed: %d", static_cast<int>(err));
        setLastError("panel-io-init-failed");
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
        .bits_per_pixel = LCD_BIT_PER_PIXEL,
        .flags = {
            .reset_active_high = 0,
        },
        .vendor_config = &vendor_config,
    };
    err = esp_lcd_new_panel_sh8601(g_panelIo, &panel_config, &g_panel);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "new_panel_sh8601 failed: %d", static_cast<int>(err));
        setLastError("panel-new-failed");
        return;
    }
    if (esp_lcd_panel_reset(g_panel) != ESP_OK)
    {
        ESP_LOGE(TAG, "panel_reset failed");
        setLastError("panel-reset-failed");
        return;
    }
    if (esp_lcd_panel_init(g_panel) != ESP_OK)
    {
        ESP_LOGE(TAG, "panel_init failed");
        setLastError("panel-init-failed");
        return;
    }
    if (esp_lcd_panel_disp_on_off(g_panel, true) != ESP_OK)
    {
        ESP_LOGE(TAG, "panel_disp_on failed");
        setLastError("panel-disp-on-failed");
        return;
    }

    g_panelReady = true;

    lv_disp_drv_init(&g_dispDrv);
    g_dispDrv.hor_res = LCD_H_RES;
    g_dispDrv.ver_res = LCD_V_RES;
    g_dispDrv.flush_cb = display_flush_cb;
    g_dispDrv.draw_buf = &g_drawBuf;
    g_dispDrv.user_data = g_panel;

    // WICHTIG: Rounder wie im offiziellen Beispiel
    g_dispDrv.rounder_cb = lv_rounder_cb;

    g_disp = lv_disp_drv_register(&g_dispDrv);

    if (!g_disp)
    {
        setLastError("lvgl-register-failed");
        return;
    }

    g_touchReady = ft3168_init();
    if (g_touchReady)
    {
        lv_indev_drv_t indev_drv;
        lv_indev_drv_init(&indev_drv);
        indev_drv.type = LV_INDEV_TYPE_POINTER;
        indev_drv.read_cb = touch_read_cb;
        indev_drv.disp = g_disp;
        lv_indev_drv_register(&indev_drv);
        ESP_LOGI(TAG, "FT3168 touch ready");
    }
    else
    {
        ESP_LOGW(TAG, "Touch controller not ready");
    }

    const esp_timer_create_args_t tick_args = {
        .callback = [](void *)
        { lv_tick_inc(LVGL_TICK_PERIOD_MS); },
        .name = "lvgl_tick"};
    if (esp_timer_create(&tick_args, &g_lvglTickTimer) == ESP_OK)
    {
        esp_timer_start_periodic(g_lvglTickTimer, LVGL_TICK_PERIOD_MS * 1000);
    }
    else
    {
        g_tickFallback = true;
        ESP_LOGW(TAG, "Failed to start LVGL tick timer, using loop fallback");
    }

#if DISPLAY_DEBUG_SIMPLE_UI
    lv_obj_t *dbg = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(dbg, lv_color_hex(0x101820), 0);
    lv_obj_set_style_text_color(dbg, lv_color_white(), 0);
    lv_obj_clear_flag(dbg, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(dbg);
    lv_label_set_text(label, "HELLO AMOLED");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);

    lv_disp_load_scr(dbg);
#else
    ui_main_init(g_disp);
    ui_main_set_gear(g_cachedGear);
    ui_main_set_shiftlight(g_cachedShift);
    if (g_logoRequested)
    {
        ui_main_show_test_logo();
    }
#endif

    g_displayReady = true;
    g_lastLvglRun = millis();
    setLastError("");
    ESP_LOGI(TAG, "Display + LVGL init done (S3 AMOLED)");
}

bool display_s3_ready()
{
    return g_displayReady;
}

void display_s3_loop()
{
    if (!g_displayReady)
        return;

    if (!g_debugSimpleUi)
    {
        WifiStatus wifiStatus = getWifiStatus();
        const bool wifiConnected = wifiStatus.staConnected || wifiStatus.apActive;
        const bool wifiConnecting = wifiStatus.staConnecting || wifiStatus.scanRunning;

        ui_main_update_status(wifiConnected, wifiConnecting, g_connected, g_bleConnectInProgress);
    }

    const uint32_t now = millis();
    if (g_tickFallback)
    {
        lv_tick_inc(now - g_lastLvglRun);
    }

    if (now - g_lastLvglRun >= 5)
    {
        lv_timer_handler();
        g_lastLvglRun = now;
    }

    if (!g_debugSimpleUi)
    {
        ui_main_loop();
    }
}

DisplayDebugInfo displayGetDebugInfo()
{
    DisplayDebugInfo info{};
    info.initAttempted = g_initAttempted;
    info.ready = g_displayReady;
    info.buffersAllocated = g_buffersAllocated;
    info.panelInitialized = g_panelReady;
    info.touchReady = g_touchReady;
    info.tickFallback = g_tickFallback;
    info.debugSimpleUi = g_debugSimpleUi;
    info.lastLvglRunMs = g_lastLvglRun;
    info.lastError = g_lastError;
    return info;
}

static lv_obj_t *create_base_debug_screen()
{
    lv_obj_t *scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0f1114), 0);
    lv_obj_set_style_text_color(scr, lv_color_white(), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(scr, 8, 0);
    return scr;
}

void displayShowDebugPattern(DisplayDebugPattern pattern)
{
    if (!g_displayReady)
    {
        setLastError("display-not-ready");
        return;
    }

    lv_obj_t *scr = create_base_debug_screen();

    switch (pattern)
    {
    case DisplayDebugPattern::ColorBars:
    {
        const uint32_t colors[] = {0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00};
        const int barWidth = LCD_H_RES / 4;
        for (int i = 0; i < 4; ++i)
        {
            lv_obj_t *bar = lv_obj_create(scr);
            lv_obj_set_size(bar, barWidth, LCD_V_RES);
            lv_obj_set_style_bg_color(bar, lv_color_hex(colors[i]), 0);
            lv_obj_set_style_border_width(bar, 0, 0);
            lv_obj_set_style_radius(bar, 0, 0);
            lv_obj_align(bar, LV_ALIGN_LEFT_MID, i * barWidth, 0);
        }

        lv_obj_t *label = lv_label_create(scr);
        lv_label_set_text(label, "Testbild: Farb-Balken");
        lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 6);
        break;
    }
    case DisplayDebugPattern::Grid:
    {
        const int cols = 6;
        const int rows = 10;
        const int cellW = LCD_H_RES / cols;
        const int cellH = LCD_V_RES / rows;
        for (int y = 0; y < rows; ++y)
        {
            for (int x = 0; x < cols; ++x)
            {
                lv_obj_t *cell = lv_obj_create(scr);
                lv_obj_set_size(cell, cellW - 2, cellH - 2);
                uint32_t shade = (x + y) % 2 == 0 ? 0x303841 : 0x1c1f24;
                lv_obj_set_style_bg_color(cell, lv_color_hex(shade), 0);
                lv_obj_set_style_border_width(cell, 1, 0);
                lv_obj_set_style_border_color(cell, lv_color_hex(0x555555), 0);
                lv_obj_set_style_radius(cell, 2, 0);
                lv_obj_align(cell, LV_ALIGN_TOP_LEFT, x * cellW + 1, y * cellH + 1);
            }
        }

        lv_obj_t *label = lv_label_create(scr);
        lv_label_set_text(label, "Testbild: Raster / Helligkeit");
        lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 6);
        break;
    }
    case DisplayDebugPattern::UiLabel:
    default:
    {
        lv_obj_t *title = lv_label_create(scr);
        lv_label_set_text(title, "Display-Debug");
        lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

        lv_obj_t *lbl = lv_label_create(scr);
        String msg = String("Ready: ") + (g_displayReady ? "yes" : "no") + "\n";
        msg += String("Panel: ") + (g_panelReady ? "ok" : "fail") + "\n";
        msg += String("Touch: ") + (g_touchReady ? "ok" : "fail") + "\n";
        msg += String("Tick: ") + (g_tickFallback ? "loop" : "timer") + "\n";
        msg += String("Error: ") + (g_lastError.length() ? g_lastError : "none");
        lv_label_set_text(lbl, msg.c_str());
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(lbl, LCD_H_RES - 20);
        lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 60);
        break;
    }
    }

    lv_disp_load_scr(scr);
}

#endif
