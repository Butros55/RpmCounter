#include "display_s3.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

extern "C"
{
#include <lvgl.h>
}

namespace
{
    constexpr uint16_t DISPLAY_WIDTH = 456;
    constexpr uint16_t DISPLAY_HEIGHT = 280;
    constexpr size_t LVGL_BUFFER_LINES = 40;
    constexpr uint8_t DISPLAY_ROTATION = 1; // Landscape

    // Board specific pins for the Waveshare ESP32-S3 Touch AMOLED 1.64"
    constexpr int PIN_LCD_CS = 6;
    constexpr int PIN_LCD_SCK = 47;
    constexpr int PIN_LCD_D0 = 18;
    constexpr int PIN_LCD_D1 = 8;
    constexpr int PIN_LCD_D2 = 16;
    constexpr int PIN_LCD_D3 = 15;
    constexpr int PIN_LCD_RST = -1;
    constexpr int PIN_LCD_BL = 38;

    Arduino_ESP32QSPI *bus = nullptr;
    Arduino_GFX *gfx = nullptr;

    lv_color_t lv_buf1[DISPLAY_WIDTH * LVGL_BUFFER_LINES];
    lv_color_t lv_buf2[DISPLAY_WIDTH * LVGL_BUFFER_LINES];
    lv_disp_draw_buf_t draw_buf;
    lv_disp_drv_t disp_drv;
    lv_disp_t *disp = nullptr;

    uint32_t last_tick = 0;

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

        target->startWrite();
        target->writeAddrWindow(area->x1, area->y1, w, h);
        target->writePixels(reinterpret_cast<uint16_t *>(color_p), static_cast<uint32_t>(w) * h);
        target->endWrite();

        lv_disp_flush_ready(driver);
    }

    void init_backlight()
    {
        if (PIN_LCD_BL >= 0)
        {
            pinMode(PIN_LCD_BL, OUTPUT);
            digitalWrite(PIN_LCD_BL, HIGH);
        }
    }
}

void display_s3_init()
{
    if (gfx)
        return;

    lv_init();

    bus = new Arduino_ESP32QSPI(PIN_LCD_CS, PIN_LCD_SCK, PIN_LCD_D0, PIN_LCD_D1, PIN_LCD_D2, PIN_LCD_D3);
    gfx = new Arduino_RM67162(bus, PIN_LCD_RST, DISPLAY_ROTATION, true /* IPS */, DISPLAY_WIDTH, DISPLAY_HEIGHT);

    gfx->begin();
    gfx->fillScreen(BLACK);
    init_backlight();

    lv_disp_draw_buf_init(&draw_buf, lv_buf1, lv_buf2, DISPLAY_WIDTH * LVGL_BUFFER_LINES);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = DISPLAY_WIDTH;
    disp_drv.ver_res = DISPLAY_HEIGHT;
    disp_drv.flush_cb = display_flush;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.user_data = gfx;
    disp = lv_disp_drv_register(&disp_drv);

    if (disp)
    {
        lv_disp_set_default(disp);
        lv_disp_set_bg_color(disp, lv_color_black());
    }

    last_tick = millis();
}

void display_s3_loop()
{
    if (!gfx)
        return;

    const uint32_t now = millis();
    uint32_t diff = now - last_tick;
    last_tick = now;

    lv_tick_inc(diff);
    lv_timer_handler();
}
