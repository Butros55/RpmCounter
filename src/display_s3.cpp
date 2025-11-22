#include "display_s3.h"

#include <Arduino_GFX_Library.h>
#include <lvgl.h>

// Pin configuration adapted from Waveshare ESP32-S3 Touch AMOLED 1.64" demo
// Adjust to match the official demo if needed.
#define LCD_BL          38
#define LCD_CS          6
#define LCD_SCK         7
#define LCD_D0          8
#define LCD_D1          9
#define LCD_D2          10
#define LCD_D3          11
#define LCD_RST         5
#define LCD_DC          4

#define LCD_WIDTH       280
#define LCD_HEIGHT      456

// LVGL draw buffer height (in rows)
#define LVGL_BUFFER_ROWS 40

static Arduino_ESP32QSPI bus(LCD_CS, LCD_SCK, LCD_D0, LCD_D1, LCD_D2, LCD_D3);
static Arduino_RM67162 display(&bus, LCD_RST, LCD_DC);

static lv_disp_draw_buf_t draw_buf;
static lv_color_t lvgl_buffer[LCD_WIDTH * LVGL_BUFFER_ROWS];
static lv_disp_drv_t disp_drv;

/**
 * @brief LVGL flush callback. Sends the rendered area to the AMOLED panel.
 */
static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    // Arduino_GFX expects 16-bit color data; LVGL default is RGB565, so we can forward directly.
    display.draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p[0], w, h);
    lv_disp_flush_ready(drv);
}

/**
 * @brief Initialize LVGL draw buffers and register the display driver.
 */
static void init_lvgl()
{
    lv_init();

    lv_disp_draw_buf_init(&draw_buf, lvgl_buffer, nullptr, LCD_WIDTH * LVGL_BUFFER_ROWS);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_WIDTH;
    disp_drv.ver_res = LCD_HEIGHT;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);
}

void display_init()
{
    // Backlight
    pinMode(LCD_BL, OUTPUT);
    digitalWrite(LCD_BL, HIGH);

    display.begin();
    display.fillScreen(BLACK);

    init_lvgl();
}

void display_loop()
{
    lv_tick_inc(5);
    lv_timer_handler();
    delay(5);
}
