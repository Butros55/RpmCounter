#include "touch_s3.h"

#include <Arduino.h>
#include <Wire.h>

extern "C"
{
#include <lvgl.h>
}

namespace
{
    constexpr uint8_t FT3267_ADDR = 0x38;
    constexpr int TOUCH_SDA = 9;
    constexpr int TOUCH_SCL = 8;
    constexpr int TOUCH_INT = -1;

    constexpr uint16_t RAW_WIDTH = 280;
    constexpr uint16_t RAW_HEIGHT = 456;

    struct TouchPoint
    {
        uint16_t x = 0;
        uint16_t y = 0;
        bool pressed = false;
    };

    TouchPoint last_point{};

    bool read_touch_raw(TouchPoint &point)
    {
        Wire.beginTransmission(FT3267_ADDR);
        Wire.write(0x02);
        if (Wire.endTransmission(false) != 0)
        {
            return false;
        }

        uint8_t data[6] = {0};
        if (Wire.requestFrom(FT3267_ADDR, static_cast<uint8_t>(6)) != 6)
        {
            return false;
        }

        for (uint8_t i = 0; i < 6 && Wire.available(); ++i)
        {
            data[i] = Wire.read();
        }

        uint8_t touches = data[0] & 0x0F;
        if (touches == 0)
        {
            point.pressed = false;
            return false;
        }

        uint16_t x = static_cast<uint16_t>(((data[1] & 0x0F) << 8) | data[2]);
        uint16_t y = static_cast<uint16_t>(((data[3] & 0x0F) << 8) | data[4]);

        point.x = x;
        point.y = y;
        point.pressed = true;
        return true;
    }

    void map_to_display(TouchPoint &point)
    {
        // Display is in landscape; rotate touch data accordingly.
        uint16_t mapped_x = point.y;
        uint16_t mapped_y = RAW_HEIGHT > point.x ? (RAW_HEIGHT - point.x) : 0;

        mapped_x = map(mapped_x, 0, RAW_HEIGHT, 0, 456);
        mapped_y = map(mapped_y, 0, RAW_WIDTH, 0, 280);

        point.x = mapped_x;
        point.y = mapped_y;
    }

    void touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
    {
        TouchPoint point{};
        bool pressed = read_touch_raw(point);
        if (pressed)
        {
            map_to_display(point);
            last_point = point;
            data->state = LV_INDEV_STATE_PR;
            data->point.x = point.x;
            data->point.y = point.y;
        }
        else
        {
            data->state = LV_INDEV_STATE_REL;
            data->point.x = last_point.x;
            data->point.y = last_point.y;
        }

        LV_UNUSED(indev_drv);
    }
}

void touch_s3_init()
{
    Wire.begin(TOUCH_SDA, TOUCH_SCL);

    if (TOUCH_INT >= 0)
    {
        pinMode(TOUCH_INT, INPUT);
    }

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touchpad_read;
    lv_indev_drv_register(&indev_drv);
}
