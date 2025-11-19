#ifndef LED_BAR_H
#define LED_BAR_H

#include <Adafruit_NeoPixel.h>

void initLeds();
void ledBarLoop();
void updateRpmBar(int rpm);
void setStatusLED(bool on);
void setLedTargetBrightness(int value);

extern Adafruit_NeoPixel strip;

#endif // LED_BAR_H
