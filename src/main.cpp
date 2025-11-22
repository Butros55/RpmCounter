#include <Arduino.h>

#include "display_s3.h"
#include "touch_s3.h"
#include "ui_main.h"

void setup()
{
    Serial.begin(115200);
    delay(200);

    display_init();
    touch_init();
    ui_init();
}

void loop()
{
    display_loop();
}
