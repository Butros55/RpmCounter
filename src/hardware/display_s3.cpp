#include "display_s3.h"

#if defined(CONFIG_IDF_TARGET_ESP32S3)

#include <Arduino.h>
#include <esp_err.h>
#include <lvgl.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <Arduino_GFX_Library.h>
#include <algorithm>
#include <cstring>

#include "core/logging.h"
#include "core/wifi.h"
#include "core/config.h"
#include "core/state.h"
#include "hardware/display.h"
#include "platform/esp32/ui_runtime_esp32.h"
#include <ui/ui_s3_main.h>

namespace
{
    // Native panel resolution (portrait) for Waveshare ESP32-S3 Touch AMOLED 1.64"
    constexpr int LCD_H_RES = 280;
    constexpr int LCD_V_RES = 456;
    constexpr int LCD_COL_OFFSET1 = 20;
    constexpr int LCD_ROW_OFFSET1 = 0;
    constexpr int LCD_COL_OFFSET2 = 0;
    constexpr int LCD_ROW_OFFSET2 = 0;
    constexpr int LCD_BIT_PER_PIXEL = 16;
    constexpr int LVGL_BUFFER_LINES = 60;
    constexpr int LVGL_BUF_WIDTH = LCD_H_RES;
    constexpr int LVGL_TICK_PERIOD_MS = 2;

    constexpr gpio_num_t PIN_LCD_CS = GPIO_NUM_9;
    constexpr gpio_num_t PIN_LCD_CLK = GPIO_NUM_10;
    constexpr gpio_num_t PIN_LCD_D0 = GPIO_NUM_11;
    constexpr gpio_num_t PIN_LCD_D1 = GPIO_NUM_12;
    constexpr gpio_num_t PIN_LCD_D2 = GPIO_NUM_13;
    constexpr gpio_num_t PIN_LCD_D3 = GPIO_NUM_14;
    constexpr gpio_num_t PIN_LCD_RST = GPIO_NUM_21;

    constexpr gpio_num_t PIN_TOUCH_SDA = GPIO_NUM_47;
    constexpr gpio_num_t PIN_TOUCH_SCL = GPIO_NUM_48;
    constexpr uint8_t TOUCH_ADDR = 0x38;
    constexpr uint32_t TOUCH_I2C_SPEED = 100000; // slow down for stability
    constexpr i2c_port_num_t TOUCH_I2C_PORT = I2C_NUM_0;
    constexpr lv_disp_rot_t LCD_ROTATION = LV_DISP_ROT_NONE;

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
    static Arduino_DataBus *g_bus = nullptr;
    static Arduino_GFX *g_gfx = nullptr;
    static esp_timer_handle_t g_lvglTickTimer = nullptr;
    static bool g_tickFallback = false;
    static bool g_buffersAllocated = false;
    static bool g_panelReady = false;
    static bool g_initAttempted = false;
    static lv_disp_rot_t g_rotation = LV_DISP_ROT_NONE;
    static String g_lastError = F("init-not-started");

    struct TouchPoint;

    bool ft3168_init();
    TouchPoint ft3168_read_touch();
    void touch_read_cb(lv_indev_drv_t *, lv_indev_data_t *);
    void display_flush_cb(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p);
    TouchPoint map_touch_to_display(const TouchPoint &raw);
    void applyPanelBrightness(uint8_t value)
    {
        if (!g_gfx)
            return;
        Arduino_CO5300 *panel = static_cast<Arduino_CO5300 *>(g_gfx);
        panel->setBrightness(value);
    }

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

    bool allocateLvglBuffers()
    {
        if (g_buffersAllocated)
        {
            return true;
        }

        const size_t bufSize = LVGL_BUF_WIDTH * LVGL_BUFFER_LINES * sizeof(lv_color_t);
        g_buf1 = static_cast<lv_color_t *>(heap_caps_malloc(bufSize, MALLOC_CAP_DMA));
        g_buf2 = static_cast<lv_color_t *>(heap_caps_malloc(bufSize, MALLOC_CAP_DMA));
        if (!g_buf1 || !g_buf2)
        {
            // fallback to non-DMA heap if necessary
            if (!g_buf1)
                g_buf1 = new lv_color_t[LVGL_BUF_WIDTH * LVGL_BUFFER_LINES];
            if (!g_buf2)
                g_buf2 = new lv_color_t[LVGL_BUF_WIDTH * LVGL_BUFFER_LINES];
        }

        if (!g_buf1 || !g_buf2)
        {
            ESP_LOGE(TAG, "Failed to allocate LVGL buffers");
            setLastError("lvgl-buffer-alloc-failed");
            return false;
        }

        lv_disp_draw_buf_init(&g_drawBuf, g_buf1, g_buf2, LVGL_BUF_WIDTH * LVGL_BUFFER_LINES);
        g_buffersAllocated = true;
        return true;
    }

    bool initPanel()
    {
        if (g_panelReady)
        {
            return true;
        }

        g_bus = new Arduino_ESP32QSPI(PIN_LCD_CS, PIN_LCD_CLK, PIN_LCD_D0, PIN_LCD_D1, PIN_LCD_D2, PIN_LCD_D3);
        g_gfx = new Arduino_CO5300(
            g_bus,
            PIN_LCD_RST,
            0,
            LCD_H_RES,
            LCD_V_RES,
            LCD_COL_OFFSET1,
            LCD_ROW_OFFSET1,
            LCD_COL_OFFSET2,
            LCD_ROW_OFFSET2);

        if (!g_gfx->begin())
        {
            ESP_LOGE(TAG, "Arduino_GFX begin failed");
            setLastError("gfx-begin-failed");
            return false;
        }

        Arduino_CO5300 *panel = static_cast<Arduino_CO5300 *>(g_gfx);
        if (panel)
        {
            panel->setBrightness(static_cast<uint8_t>(cfg.displayBrightness));
        }
        g_panelReady = true;
        ESP_LOGI(TAG, "Panel ready: %dx%d offsets L%d/R%d/T%d/B%d", LCD_H_RES, LCD_V_RES, LCD_COL_OFFSET1, LCD_COL_OFFSET2, LCD_ROW_OFFSET1, LCD_ROW_OFFSET2);
        return true;
    }

    void drawStartupPattern()
    {
        if (!g_gfx)
        {
            return;
        }

        const uint16_t colors[] = {0xF800, 0x07E0, 0x001F, 0xFFE0};
        const int barWidth = LCD_H_RES / 4;
        for (int i = 0; i < 4; ++i)
        {
            g_gfx->fillRect(i * barWidth, 0, barWidth, LCD_V_RES, colors[i]);
        }
        g_gfx->fillRect(0, LCD_V_RES - 40, LCD_H_RES, 40, 0x0000);
        g_gfx->setCursor(10, LCD_V_RES - 30);
        g_gfx->setTextSize(2);
        g_gfx->setTextColor(0xFFFF);
        g_gfx->println(F("AMOLED init..."));
    }

    static lv_indev_drv_t g_indev_drv; // Must be static to persist!

    void initTouch()
    {
        Serial.println("[S3] initTouch()");
        g_touchReady = ft3168_init();
        if (g_touchReady)
        {
            lv_indev_drv_init(&g_indev_drv);
            g_indev_drv.type = LV_INDEV_TYPE_POINTER;
            g_indev_drv.read_cb = touch_read_cb;
            g_indev_drv.disp = g_disp;
            lv_indev_t *indev = lv_indev_drv_register(&g_indev_drv);
            if (indev != nullptr)
            {
                ESP_LOGI(TAG, "FT3168 touch ready (LVGL indev registered)");
                Serial.println("[S3] FT3168 touch ready (indev registered)");
            }
            else
            {
                ESP_LOGE(TAG, "FT3168 init ok but LVGL indev register FAILED");
                Serial.println("[S3] FT3168 LVGL indev register FAILED");
                g_touchReady = false;
            }
        }
        else
        {
            ESP_LOGW(TAG, "Touch controller not ready, LVGL indev NOT registered");
            Serial.println("[S3] Touch controller not ready");
        }
    }

    bool initLvglDriver()
    {
        lv_disp_drv_init(&g_dispDrv);
        g_dispDrv.hor_res = LCD_H_RES;
        g_dispDrv.ver_res = LCD_V_RES;
        g_dispDrv.flush_cb = display_flush_cb;
        g_dispDrv.draw_buf = &g_drawBuf;
        g_dispDrv.user_data = g_gfx;
        g_dispDrv.rounder_cb = lv_rounder_cb;
        g_dispDrv.sw_rotate = 0;

        g_disp = lv_disp_drv_register(&g_dispDrv);
        if (!g_disp)
        {
            setLastError("lvgl-register-failed");
            return false;
        }

        g_rotation = LCD_ROTATION;
        lv_disp_set_rotation(g_disp, g_rotation);
        return true;
    }

    void startLvglTick()
    {
        const esp_timer_create_args_t tick_args = {
            .callback = [](void *)
            { lv_tick_inc(LVGL_TICK_PERIOD_MS); },
            .name = "lvgl_tick"};
        if (esp_timer_create(&tick_args, &g_lvglTickTimer) == ESP_OK &&
            esp_timer_start_periodic(g_lvglTickTimer, LVGL_TICK_PERIOD_MS * 1000) == ESP_OK)
        {
            g_tickFallback = false;
        }
        else
        {
            g_tickFallback = true;
            ESP_LOGW(TAG, "Failed to start LVGL tick timer, using loop fallback");
        }
    }

    struct TouchPoint
    {
        bool touched = false;
        uint16_t x = 0;
        uint16_t y = 0;

        TouchPoint() = default;
        TouchPoint(bool t, uint16_t px, uint16_t py) : touched(t), x(px), y(py) {}
    };

    // Neuer I2C-Treiber (driver_ng)
    static i2c_master_bus_handle_t g_touchBus = nullptr;
    static i2c_master_dev_handle_t g_touchDev = nullptr;
    static void release_touch_dev()
    {
        if (g_touchDev)
        {
            i2c_master_bus_rm_device(g_touchDev);
            g_touchDev = nullptr;
        }
    }

    uint16_t clamp_to(uint16_t value, uint16_t maxValue)
    {
        return static_cast<uint16_t>(std::min<uint32_t>(value, maxValue));
    }

    // Low‑Level Init: Bus + Device (nur EINMAL)
    static bool ft3168_ll_init()
    {
        if (g_touchDev)
        {
            return true; // schon fertig
        }

        if (!g_touchBus)
        {
            i2c_master_bus_config_t bus_cfg = {};
            bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
            bus_cfg.i2c_port = TOUCH_I2C_PORT;
            bus_cfg.scl_io_num = PIN_TOUCH_SCL;
            bus_cfg.sda_io_num = PIN_TOUCH_SDA;
            bus_cfg.glitch_ignore_cnt = 7;
            // Use 0 for synchronous blocking I2C (no queue, no overflow)
            bus_cfg.trans_queue_depth = 0;
            bus_cfg.flags.enable_internal_pullup = true;

            esp_err_t err = i2c_new_master_bus(&bus_cfg, &g_touchBus);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
                return false;
            }
            LOG_INFO("DISPLAY", "TOUCH_BUS_OK", String("sda=") + PIN_TOUCH_SDA + " scl=" + PIN_TOUCH_SCL + " hz=" + TOUCH_I2C_SPEED);
        }

        i2c_device_config_t dev_cfg = {};
        dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        dev_cfg.device_address = TOUCH_ADDR;
        dev_cfg.scl_speed_hz = TOUCH_I2C_SPEED;
        dev_cfg.scl_wait_us = 0;
        dev_cfg.flags.disable_ack_check = false;

        esp_err_t err = i2c_master_bus_add_device(g_touchBus, &dev_cfg, &g_touchDev);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "i2c_master_bus_add_device failed: %s", esp_err_to_name(err));
            g_touchDev = nullptr;
            return false;
        }
        LOG_INFO("DISPLAY", "TOUCH_DEV_OK", String("addr=0x") + String(TOUCH_ADDR, HEX));

        return true;
    }

    static bool ft3168_write_reg(uint8_t reg, const uint8_t *data, size_t len)
    {
        if (!g_touchDev)
        {
            if (!ft3168_ll_init())
                return false;
        }

        if (len > 7)
            len = 7; // Sicherheit, wir brauchen max. 1 Byte

        uint8_t buf[8];
        buf[0] = reg;
        if (data && len)
        {
            memcpy(&buf[1], data, len);
        }

        // Use -1 for infinite timeout (blocking until complete)
        esp_err_t err = i2c_master_transmit(
            g_touchDev,
            buf,
            len + 1,
            -1); // Blocking call

        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "FT3168 write reg=0x%02X len=%u err=%s",
                     reg, (unsigned)len, esp_err_to_name(err));
            return false;
        }
        return true;
    }

    static bool ft3168_read_reg(uint8_t reg, uint8_t *data, size_t len)
    {
        if (!g_touchDev)
        {
            if (!ft3168_ll_init() || !data || len == 0)
                return false;
        }
        if (!data || len == 0)
            return false;

        // Use -1 for infinite timeout (blocking until complete)
        esp_err_t err = i2c_master_transmit_receive(
            g_touchDev,
            &reg,
            1,
            data,
            len,
            -1); // Blocking call

        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "FT3168 read reg=0x%02X len=%u err=%s",
                     reg, (unsigned)len, esp_err_to_name(err));
            return false;
        }
        return true;
    }

    bool ft3168_init()
    {
        Serial.println("[TOUCH] ---- FT3168 Init Start ----");
        Serial.printf("[TOUCH] Pins: SDA=%d, SCL=%d, Addr=0x%02X\n", PIN_TOUCH_SDA, PIN_TOUCH_SCL, TOUCH_ADDR);

        constexpr int MAX_ATTEMPTS = 5;
        constexpr uint32_t RETRY_DELAY_MS = 100;

        // The FT3168 needs a proper power-up sequence. Give it time to stabilize.
        delay(200);

        for (int attempt = 1; attempt <= MAX_ATTEMPTS; ++attempt)
        {
            Serial.printf("[TOUCH] Attempt %d/%d...\n", attempt, MAX_ATTEMPTS);

            if (!ft3168_ll_init())
            {
                setLastError("touch-bus-init-failed");
                Serial.println("[TOUCH] ERROR: I2C bus init failed");
                return false;
            }

            // Wait for the touch controller to be ready after bus init
            delay(50);

            // Try to read ID first (register 0xA3) - some FT3168 variants need this
            uint8_t id = 0;
            if (!ft3168_read_reg(0xA3, &id, 1))
            {
                Serial.printf("[TOUCH] Attempt %d: Read ID failed, retrying...\n", attempt);
                release_touch_dev();
                delay(RETRY_DELAY_MS);
                continue;
            }

            // ID 0x00 means no proper response - controller not ready
            if (id == 0x00 && attempt < MAX_ATTEMPTS)
            {
                Serial.printf("[TOUCH] Attempt %d: ID=0x00 (not ready), retrying...\n", attempt);
                release_touch_dev();
                delay(RETRY_DELAY_MS * 2); // Wait longer for controller to wake up
                continue;
            }

            // Set normal operating mode (register 0x00 = 0x00)
            uint8_t mode = 0x00;
            if (!ft3168_write_reg(0x00, &mode, 1))
            {
                Serial.printf("[TOUCH] Attempt %d: Write mode failed (non-critical)\n", attempt);
                // Continue anyway - some controllers work without explicit mode set
            }

            // Also try setting interrupt mode for better touch detection
            uint8_t intMode = 0x01; // Trigger mode
            ft3168_write_reg(0xA4, &intMode, 1);

            Serial.println("[TOUCH] ---- FT3168 Init Success ----");
            Serial.printf("[TOUCH] Controller ID: 0x%02X\n", id);
            Serial.println();

            // Valid IDs for FocalTech touch controllers: 0x11, 0x36, 0x64, 0x70, etc.
            // ID 0x00 might still work on some boards
            return true;
        }

        Serial.println("[TOUCH] ERROR: Init failed after all attempts!");
        setLastError("touch-init-failed");
        return false;
    }

    TouchPoint ft3168_read_touch()
    {
        TouchPoint p{false, 0, 0};

        uint8_t status = 0;
        if (!ft3168_read_reg(0x02, &status, 1))
            return p;

        uint8_t touchCount = status & 0x0F;
        if (touchCount == 0)
            return p;

        uint8_t buf[4] = {};
        if (!ft3168_read_reg(0x03, buf, sizeof(buf)))
            return p;

        uint16_t x = (((uint16_t)buf[0] & 0x0F) << 8) | buf[1];
        uint16_t y = (((uint16_t)buf[2] & 0x0F) << 8) | buf[3];

        p.x = clamp_to(x, LCD_H_RES - 1);
        p.y = clamp_to(y, LCD_V_RES - 1);
        p.touched = true;
        return p;
    }

    TouchPoint map_touch_to_display(const TouchPoint &raw)
    {
        TouchPoint mapped = raw;
        const uint16_t maxX = lv_disp_get_hor_res(g_disp) - 1;
        const uint16_t maxY = lv_disp_get_ver_res(g_disp) - 1;

        switch (g_rotation)
        {
        case LV_DISP_ROT_90:
            mapped.x = clamp_to(LCD_V_RES - 1 - raw.y, maxX);
            mapped.y = clamp_to(raw.x, maxY);
            break;
        case LV_DISP_ROT_180:
            mapped.x = clamp_to(LCD_H_RES - 1 - raw.x, maxX);
            mapped.y = clamp_to(LCD_V_RES - 1 - raw.y, maxY);
            break;
        case LV_DISP_ROT_270:
            mapped.x = clamp_to(raw.y, maxX);
            mapped.y = clamp_to(LCD_H_RES - 1 - raw.x, maxY);
            break;
        case LV_DISP_ROT_NONE:
        default:
            mapped.x = clamp_to(raw.x, maxX);
            mapped.y = clamp_to(raw.y, maxY);
            break;
        }
        return mapped;
    }

    void display_flush_cb(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p)
    {
        if (!g_gfx)
        {
            ESP_LOGW(TAG, "flush_cb: no panel");
            lv_disp_flush_ready(disp_drv);
            return;
        }

        const int width = area->x2 - area->x1 + 1;
        const int height = area->y2 - area->y1 + 1;

        // Match LV_COLOR_16_SWAP so colors are correct on the AMOLED
#if LV_COLOR_16_SWAP
        g_gfx->draw16bitBeRGBBitmap(area->x1, area->y1, const_cast<uint16_t *>(reinterpret_cast<const uint16_t *>(&color_p->full)), width, height);
#else
        g_gfx->draw16bitRGBBitmap(area->x1, area->y1, const_cast<uint16_t *>(reinterpret_cast<const uint16_t *>(&color_p->full)), width, height);
#endif
        lv_disp_flush_ready(disp_drv);
    }

    void touch_read_cb(lv_indev_drv_t *, lv_indev_data_t *data)
    {
        static TouchPoint lastPoint{false, 0, 0};
        static bool wasTouched = false;
        static uint32_t lastTouchLogMs = 0;
        static uint32_t touchCount = 0;

        TouchPoint p = ft3168_read_touch();
        if (p.touched)
        {
            TouchPoint mapped = map_touch_to_display(p);
            data->state = LV_INDEV_STATE_PRESSED;
            data->point.x = mapped.x;
            data->point.y = mapped.y;
            lastPoint = mapped;

            // Log touch start and periodically during hold (max once per 500ms)
            uint32_t now = millis();
            if (!wasTouched)
            {
                touchCount++;
                Serial.println("----------------------------------------");
                Serial.printf("[TOUCH] #%lu PRESSED at (%u, %u)\n", touchCount, mapped.x, mapped.y);
                Serial.println("----------------------------------------");
                lastTouchLogMs = now;
            }
            else if (now - lastTouchLogMs > 500)
            {
                Serial.printf("[TOUCH] Holding at (%u, %u)\n", mapped.x, mapped.y);
                lastTouchLogMs = now;
            }
            wasTouched = true;
        }
        else
        {
            data->state = LV_INDEV_STATE_RELEASED;
            data->point.x = lastPoint.x;
            data->point.y = lastPoint.y;

            if (wasTouched)
            {
                Serial.printf("[TOUCH] Released at (%u, %u)\n", lastPoint.x, lastPoint.y);
            }
            wasTouched = false;
        }
    }
} // namespace

void displayInit()
{
    display_s3_init();
}

void displayClear()
{
    // Keep LVGL access single-threaded on the Arduino loop task. These wrappers
    // can be hit from BLE/LED worker tasks, so they only update shared state.
    g_cachedShift = false;
    g_shiftBlinkActive = false;
}

void displayShowTestLogo()
{
    g_logoRequested = true;
    if (!g_displayReady)
        return;

    ui_s3_show_logo();
}

void displaySetGear(int gear)
{
    if (gear < 0)
        gear = 0;

    g_cachedGear = gear;
}

void displaySetShiftBlink(bool active)
{
    g_cachedShift = active;
    g_shiftBlinkActive = active;
}

void display_s3_init()
{
    g_initAttempted = true;
    if (g_displayReady)
        return;

    Serial.println();
    Serial.println("[DISPLAY] ---- S3 AMOLED Init Start ----");

    lv_init();
    Serial.printf("[DISPLAY] LVGL initialized (depth=%d, swap=%d)\n", LV_COLOR_DEPTH, LV_COLOR_16_SWAP);

    if (!allocateLvglBuffers())
    {
        Serial.println("[DISPLAY] ERROR: Buffer allocation failed!");
        return;
    }
    Serial.println("[DISPLAY] Buffers allocated OK");

    if (!initPanel())
    {
        Serial.println("[DISPLAY] ERROR: Panel init failed!");
        return;
    }
    Serial.println("[DISPLAY] Panel initialized OK");

    drawStartupPattern();

    g_gfx->fillScreen(0x0000);

    if (!initLvglDriver())
    {
        Serial.println("[DISPLAY] ERROR: LVGL driver init failed!");
        return;
    }
    Serial.println("[DISPLAY] LVGL driver OK");

    lv_disp_set_default(g_disp);

    initTouch();
    startLvglTick();

    UiDisplayHooks hooks{};
    hooks.setBrightness = [](uint8_t value, void *)
    {
        cfg.displayBrightness = value;
        applyPanelBrightness(value);
    };
    hooks.saveSettings = [](const UiSettings &settings, void *)
    {
        saveEsp32UiSettings(settings);
    };
    ui_s3_init(g_disp, hooks, makeEsp32UiState());
    ui_s3_set_gear(g_cachedGear);
    ui_s3_set_shiftlight(g_cachedShift);
    if (g_logoRequested)
    {
        ui_s3_show_logo();
    }

    g_displayReady = true;
    g_lastLvglRun = millis();
    setLastError("");

    Serial.println("[DISPLAY] ---- S3 AMOLED Init Complete ----");
    Serial.printf("[DISPLAY] Resolution: %dx%d\n", LCD_H_RES, LCD_V_RES);
    Serial.println();
}

bool display_s3_ready()
{
    return g_displayReady;
}

void display_s3_loop()
{
    if (!g_displayReady)
        return;

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

    ui_s3_loop(makeEsp32UiState());
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
    lv_disp_set_default(g_disp);

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
