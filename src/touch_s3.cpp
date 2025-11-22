#include "touch_s3.h"

#include <Wire.h>
#include <lvgl.h>

// Pin and address definitions based on the Waveshare demo
#define TOUCH_SDA 39
#define TOUCH_SCL 40
#define TOUCH_INT 41
#define TOUCH_RST 42
#define TOUCH_ADDR 0x38

// Display resolution for coordinate conversion
#define TOUCH_MAX_X 456
#define TOUCH_MAX_Y 280

static lv_indev_drv_t indev_drv;
static bool touch_pressed = false;
static uint16_t touch_x = 0;
static uint16_t touch_y = 0;

/**
 * @brief Minimal FT3267 register read.
 */
static bool ft3267_read_points()
{
    Wire.beginTransmission(TOUCH_ADDR);
    Wire.write(0x02); // Start from touch status
    if (Wire.endTransmission(false) != 0)
    {
        return false;
    }

    const uint8_t to_read = 5; // status + XH + XL + YH + YL
    uint8_t read = Wire.requestFrom(TOUCH_ADDR, to_read);
    if (read != to_read)
    {
        return false;
    }

    uint8_t status = Wire.read();
    uint8_t xh = Wire.read();
    uint8_t xl = Wire.read();
    uint8_t yh = Wire.read();
    uint8_t yl = Wire.read();

    uint8_t touches = status & 0x0F;
    if (touches == 0)
    {
        touch_pressed = false;
        return true;
    }

    touch_x = ((xh & 0x0F) << 8) | xl;
    touch_y = ((yh & 0x0F) << 8) | yl;

    // Align to portrait orientation used by display (280x456). Swap axis accordingly.
    uint16_t aligned_x = touch_y;
    uint16_t aligned_y = TOUCH_MAX_X - touch_x;

    touch_x = aligned_x;
    touch_y = aligned_y;
    touch_pressed = true;
    return true;
}

/**
 * @brief LVGL read callback bridging touch data into the input driver.
 */
static void lvgl_touch_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;
    ft3267_read_points();

    data->state = touch_pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
    data->point.x = touch_x;
    data->point.y = touch_y;
}

void touch_init()
{
    pinMode(TOUCH_RST, OUTPUT);
    digitalWrite(TOUCH_RST, LOW);
    delay(10);
    digitalWrite(TOUCH_RST, HIGH);

    Wire.begin(TOUCH_SDA, TOUCH_SCL);
    Wire.setClock(400000);

    ft3267_read_points();

    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = lvgl_touch_read;
    lv_indev_drv_register(&indev_drv);
}
