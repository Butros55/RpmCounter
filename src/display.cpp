#include "display.h"

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <math.h>

namespace
{
    constexpr int TFT_MOSI = 23;
    constexpr int TFT_SCLK = 18;
    constexpr int TFT_CS = 15;
    constexpr int TFT_DC = 4;
    constexpr int TFT_RST = 16;

    bool g_displayInitialized = false;
}

Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);

void displayInit()
{
    SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
    tft.init(240, 240);
    tft.setRotation(2);
    g_displayInitialized = true;
    displayClear();
}

void displayClear()
{
    if (!g_displayInitialized)
        return;

    tft.fillScreen(ST77XX_BLACK);
}

void displayShowTestLogo()
{
    if (!g_displayInitialized)
        return;

    displayClear();

    const int16_t cx = tft.width() / 2;
    const int16_t cy = tft.height() / 2;
    const int16_t outerRadius = 90;
    const int16_t ringThickness = 10;
    const int16_t emblemRadius = outerRadius - ringThickness;
    const uint16_t bmwBlue = tft.color565(0, 105, 180);

    tft.fillCircle(cx, cy, outerRadius, ST77XX_WHITE);
    tft.fillCircle(cx, cy, outerRadius - 4, ST77XX_WHITE);
    tft.fillCircle(cx, cy, outerRadius - ringThickness, ST77XX_BLACK);
    tft.fillCircle(cx, cy, emblemRadius, ST77XX_WHITE);

    for (int16_t y = 0; y <= emblemRadius; ++y)
    {
        int16_t span = static_cast<int16_t>(sqrtf(static_cast<float>(emblemRadius * emblemRadius - y * y)));
        if (span <= 0)
            continue;

        int16_t yTop = cy - y;
        int16_t yBottom = cy + y;

        tft.drawFastHLine(cx - span, yTop, span, bmwBlue);
        tft.drawFastHLine(cx + 1, yBottom, span, bmwBlue);
    }

    tft.drawFastVLine(cx, cy - emblemRadius, emblemRadius * 2, ST77XX_BLACK);
    tft.drawFastHLine(cx - emblemRadius, cy, emblemRadius * 2, ST77XX_BLACK);

    tft.drawCircle(cx, cy, outerRadius, ST77XX_WHITE);
    tft.drawCircle(cx, cy, outerRadius - 1, ST77XX_WHITE);
    tft.drawCircle(cx, cy, emblemRadius, ST77XX_WHITE);
}

