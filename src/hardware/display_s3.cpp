#include "display_s3.h"

#if defined(CONFIG_IDF_TARGET_ESP32S3)

#include <Arduino.h>
#include <lvgl.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include <esp_heap_caps.h>

#include "core/wifi.h"
#include "core/state.h"
#include "ui/ui_main.h"

namespace
{
    constexpr uint16_t DISPLAY_WIDTH = 456;
    constexpr uint16_t DISPLAY_HEIGHT = 280;
    constexpr size_t LVGL_BUFFER_LINES = 40;
    constexpr uint8_t DISPLAY_ROTATION = 1; // Landscape

    // Waveshare ESP32-S3 1.64" AMOLED (RM67162) QSPI pins
    constexpr int PIN_LCD_CS = 6;
    constexpr int PIN_LCD_SCK = 47;
    constexpr int PIN_LCD_D0 = 18;
    constexpr int PIN_LCD_D1 = 8;
    constexpr int PIN_LCD_D2 = 16;
    constexpr int PIN_LCD_D3 = 15;
    constexpr int PIN_LCD_RST = -1;
    constexpr int PIN_LCD_BL = 38;

    // Capacitive touch (CST816)
    constexpr uint8_t TOUCH_ADDR = 0x15;
    constexpr int TOUCH_SDA = 8;
    constexpr int TOUCH_SCL = 18;

    Arduino_ESP32QSPI *bus = nullptr;
    Arduino_GFX *gfx = nullptr;

    lv_color_t *lv_buf1 = nullptr;
    lv_color_t *lv_buf2 = nullptr;
    lv_disp_draw_buf_t draw_buf;
    lv_disp_drv_t disp_drv;
    lv_disp_t *disp = nullptr;
    bool displayReady = false;
    uint32_t last_tick = 0;

    struct TouchPoint
    {
        bool touched = false;
        uint16_t x = 0;
        uint16_t y = 0;
    };

    void init_backlight()
    {
        if (PIN_LCD_BL >= 0)
        {
            pinMode(PIN_LCD_BL, OUTPUT);
            digitalWrite(PIN_LCD_BL, HIGH);
        }
    }

    void display_flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *color_p)
    {
        Arduino_GFX *target = static_cast<Arduino_GFX *>(driver->user_data);
        if (!target)
        {
            lv_disp_flush_ready(driver);
            return;
        }

        uint16_t w = static_cast<uint16_t>(area->x2 - area->x1 + 1);
        uint16_t h = static_cast<uint16_t>(area->y2 - area->y1 + 1);
        target->draw16bitRGBBitmap(area->x1, area->y1, reinterpret_cast<uint16_t *>(color_p), w, h);

        lv_disp_flush_ready(driver);
    }

    TouchPoint read_touch()
    {
        TouchPoint p{};
        Wire.beginTransmission(TOUCH_ADDR);
        Wire.write(0x01); // start with gesture register
        if (Wire.endTransmission(false) != 0)
        {
            return p;
        }

        if (Wire.requestFrom((uint8_t)TOUCH_ADDR, (uint8_t)6) < 6)
        {
            return p;
        }

        Wire.read(); // gesture id (unused)
        uint8_t points = Wire.read();
        if (points == 0)
        {
            Wire.read();
            Wire.read();
            Wire.read();
            Wire.read();
            return p;
        }

        uint8_t xh = Wire.read();
        uint8_t xl = Wire.read();
        uint8_t yh = Wire.read();
        uint8_t yl = Wire.read();

        p.touched = true;
        p.x = static_cast<uint16_t>(((xh & 0x0F) << 8) | xl);
        p.y = static_cast<uint16_t>(((yh & 0x0F) << 8) | yl);

        // Rotate to landscape orientation
        uint16_t mapped_x = p.y;
        uint16_t mapped_y = (p.x < DISPLAY_HEIGHT) ? (DISPLAY_HEIGHT - 1 - p.x) : 0;
        mapped_x = map(mapped_x, 0, DISPLAY_HEIGHT, 0, DISPLAY_WIDTH);
        mapped_y = map(mapped_y, 0, DISPLAY_WIDTH, 0, DISPLAY_HEIGHT);

        p.x = mapped_x;
        p.y = mapped_y;
        return p;
    }

    void touch_read_cb(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
    {
        static TouchPoint lastPoint{};
        TouchPoint p = read_touch();

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

        LV_UNUSED(indev_drv);
    }
}

void display_s3_init()
{
    if (displayReady)
        return;

    lv_init();

    lv_buf1 = static_cast<lv_color_t *>(heap_caps_malloc(DISPLAY_WIDTH * LVGL_BUFFER_LINES * sizeof(lv_color_t), MALLOC_CAP_DMA));
    lv_buf2 = static_cast<lv_color_t *>(heap_caps_malloc(DISPLAY_WIDTH * LVGL_BUFFER_LINES * sizeof(lv_color_t), MALLOC_CAP_DMA));
    if (!lv_buf1 || !lv_buf2)
    {
        if (!lv_buf1)
            lv_buf1 = new lv_color_t[DISPLAY_WIDTH * LVGL_BUFFER_LINES];
        if (!lv_buf2)
            lv_buf2 = new lv_color_t[DISPLAY_WIDTH * LVGL_BUFFER_LINES];
    }

    lv_disp_draw_buf_init(&draw_buf, lv_buf1, lv_buf2, DISPLAY_WIDTH * LVGL_BUFFER_LINES);

    bus = new Arduino_ESP32QSPI(PIN_LCD_CS, PIN_LCD_SCK, PIN_LCD_D0, PIN_LCD_D1, PIN_LCD_D2, PIN_LCD_D3);
    gfx = new Arduino_RM67162(bus, PIN_LCD_RST, DISPLAY_ROTATION, true /* IPS */);
    gfx->begin();
    gfx->fillScreen(0x0000);
    init_backlight();

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = DISPLAY_WIDTH;
    disp_drv.ver_res = DISPLAY_HEIGHT;
    disp_drv.flush_cb = display_flush;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.user_data = gfx;
    disp = lv_disp_drv_register(&disp_drv);

    Wire.begin(TOUCH_SDA, TOUCH_SCL);
    lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read_cb;
    lv_indev_drv_register(&indev_drv);

    if (disp)
    {
        lv_disp_set_default(disp);
        lv_disp_set_bg_color(disp, lv_color_black());
    }

    ui_main_init(disp);

    displayReady = true;
    last_tick = millis();
}

bool display_s3_ready()
{
    return displayReady;
}

void display_s3_loop()
{
    if (!displayReady)
        return;

    const uint32_t now = millis();
    lv_tick_inc(now - last_tick);
    last_tick = now;

    WifiStatus wifiStatus = getWifiStatus();
    const bool wifiConnected = wifiStatus.staConnected || wifiStatus.apActive;
    const bool wifiConnecting = wifiStatus.staConnecting || wifiStatus.scanRunning;

    ui_main_update_status(wifiConnected, wifiConnecting, g_connected, g_bleConnectInProgress);
    ui_main_loop();
    lv_timer_handler();
}

#endif
