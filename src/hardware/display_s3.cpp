#include "display_s3.h"

#if defined(CONFIG_IDF_TARGET_ESP32S3)

#include <lvgl.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <esp_heap_caps.h>

#include "core/wifi.h"
#include "core/state.h"
#include "ui/ui_main.h"

#ifndef S3_TFT_WIDTH
#define S3_TFT_WIDTH 320
#endif

#ifndef S3_TFT_HEIGHT
#define S3_TFT_HEIGHT 170
#endif

#ifndef S3_TFT_MOSI
#define S3_TFT_MOSI 11
#endif

#ifndef S3_TFT_SCLK
#define S3_TFT_SCLK 12
#endif

#ifndef S3_TFT_CS
#define S3_TFT_CS 10
#endif

#ifndef S3_TFT_DC
#define S3_TFT_DC 9
#endif

#ifndef S3_TFT_RST
#define S3_TFT_RST 14
#endif

#ifndef S3_TFT_ROTATION
#define S3_TFT_ROTATION 1
#endif

#ifndef S3_TOUCH_SDA
#define S3_TOUCH_SDA 8
#endif

#ifndef S3_TOUCH_SCL
#define S3_TOUCH_SCL 18
#endif

#ifndef S3_TOUCH_ADDR
#define S3_TOUCH_ADDR 0x15 // CST816/Capacitive touch controllers often use 0x15
#endif

#ifndef LVGL_BUFFER_LINES
#define LVGL_BUFFER_LINES 40
#endif

static Adafruit_ST7789 g_tft(S3_TFT_CS, S3_TFT_DC, S3_TFT_RST);
static lv_disp_draw_buf_t g_drawBuf;
static lv_color_t *g_buf1 = nullptr;
static bool g_displayReady = false;
static uint32_t g_lastTick = 0;

struct TouchPoint
{
    bool touched;
    uint16_t x;
    uint16_t y;
};

static void display_flush_cb(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
    uint32_t w = static_cast<uint32_t>(area->x2 - area->x1 + 1);
    uint32_t h = static_cast<uint32_t>(area->y2 - area->y1 + 1);

    g_tft.startWrite();
    g_tft.setAddrWindow(area->x1, area->y1, w, h);
    g_tft.pushColors(reinterpret_cast<uint16_t *>(color_p), w * h, true);
    g_tft.endWrite();

    lv_disp_flush_ready(disp);
}

static TouchPoint readTouch()
{
    TouchPoint p{false, 0, 0};

    Wire.beginTransmission(S3_TOUCH_ADDR);
    Wire.write(0x01); // start with gesture register
    if (Wire.endTransmission(false) != 0)
        return p;

    if (Wire.requestFrom(S3_TOUCH_ADDR, static_cast<uint8_t>(6)) < 6)
        return p;

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

    if (S3_TFT_ROTATION == 1)
    {
        uint16_t tmp = p.x;
        p.x = S3_TFT_WIDTH - 1 - p.y;
        p.y = tmp;
    }
    else if (S3_TFT_ROTATION == 2)
    {
        p.x = S3_TFT_WIDTH - 1 - p.x;
        p.y = S3_TFT_HEIGHT - 1 - p.y;
    }
    else if (S3_TFT_ROTATION == 3)
    {
        uint16_t tmp = p.x;
        p.x = p.y;
        p.y = S3_TFT_HEIGHT - 1 - tmp;
    }

    if (p.x >= S3_TFT_WIDTH)
        p.x = S3_TFT_WIDTH - 1;
    if (p.y >= S3_TFT_HEIGHT)
        p.y = S3_TFT_HEIGHT - 1;

    return p;
}

static void touch_read_cb(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    static TouchPoint lastPoint{false, 0, 0};
    TouchPoint p = readTouch();

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

void display_s3_init()
{
    if (g_displayReady)
        return;

    lv_init();

    size_t bufSize = S3_TFT_WIDTH * LVGL_BUFFER_LINES * sizeof(lv_color_t);
    g_buf1 = static_cast<lv_color_t *>(heap_caps_malloc(bufSize, MALLOC_CAP_DMA));
    if (!g_buf1)
    {
        g_buf1 = new lv_color_t[S3_TFT_WIDTH * LVGL_BUFFER_LINES];
    }

    lv_disp_draw_buf_init(&g_drawBuf, g_buf1, nullptr, S3_TFT_WIDTH * LVGL_BUFFER_LINES);

    SPI.begin(S3_TFT_SCLK, -1, S3_TFT_MOSI, S3_TFT_CS);
    g_tft.init(S3_TFT_WIDTH, S3_TFT_HEIGHT);
    g_tft.setRotation(S3_TFT_ROTATION);
    g_tft.fillScreen(ST77XX_BLACK);

    lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = S3_TFT_WIDTH;
    disp_drv.ver_res = S3_TFT_HEIGHT;
    disp_drv.flush_cb = display_flush_cb;
    disp_drv.draw_buf = &g_drawBuf;
    lv_disp_t *disp = lv_disp_drv_register(&disp_drv);

    Wire.begin(S3_TOUCH_SDA, S3_TOUCH_SCL);
    lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read_cb;
    lv_indev_t *indev = lv_indev_drv_register(&indev_drv);

    LV_UNUSED(disp);
    LV_UNUSED(indev);

    ui_main_init(disp);

    g_displayReady = true;
    g_lastTick = millis();
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
    lv_tick_inc(now - g_lastTick);
    g_lastTick = now;

    WifiStatus wifiStatus = getWifiStatus();
    bool wifiConnected = wifiStatus.staConnected || wifiStatus.apActive;
    bool wifiConnecting = wifiStatus.staConnecting || wifiStatus.scanRunning;

    ui_main_update_status(wifiConnected, wifiConnecting, g_connected, g_bleConnectInProgress);
    ui_main_loop();

    lv_timer_handler();
}

#endif

